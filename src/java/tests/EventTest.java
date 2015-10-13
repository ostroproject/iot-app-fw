import ArgParser.*;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import com.intel.ostro.appfw.*;

public class EventTest {
    public static void main(String [] args) {
        ArgParser parser = new ArgParser(args);
        addCommands(parser);
        Map<String, String> arguments = parser.parse();
        if (arguments.isEmpty() || arguments.containsKey("help")) {
            parser.printHelp();
            return;
        }

        if (arguments.containsKey("debug")) {
            AppFw fw = AppFw.getInstance();
            switch (arguments.get("debug")) {
                case "all":
                    fw.enableDebug(Arrays.asList("*"));
                    break;
                case "limited":
                    fw.enableDebug(Arrays.asList("@AppFw.c"));
                    break;
                case "none":
                    break;
                default:
                    throw new IllegalArgumentException(
                        "Invalid argument for debug: " + arguments.get("debug"));
            }
        }


        commonInit(arguments);

        if (arguments.containsKey("server")) {
            beServer(arguments);
        } else {
            beClient(arguments);
        }
    }

    public static void addCommands(ArgParser parser) {
        parser.addCommand(new Command("help", "Prints help"));
        parser.addCommand(new Command("server", "Subscribe and wait for events"));
        parser.addCommand(new CommandWithArg("label", "Target application label"));
        parser.addCommand(new CommandWithArg("user", 
            "Target application user"));
        parser.addCommand(new CommandWithArg("appid", 
            "Target application appid"));
        parser.addCommand(new CommandWithArg("binary", 
            "Target application binary path"));
        parser.addCommand(new CommandWithArg("process", 
            "Target application process id"));
        parser.addCommand(new CommandWithArg("events", 
            "Comma separated list of events that will be subscribed or sent"));
        parser.addCommand(new CommandWithArg("nevent", "Number of bundles to send"));
        parser.addCommand(new CommandWithArg("debug", 
            "Turn debugging on (valid arguments are none, limited, all)"));
        parser.addCommand(new CommandWithArg("Interval", 
            "Interval between event bundles"));
        
    }

    public static void commonInit(Map<String, String> arguments) {
        if (!arguments.containsKey("events") 
            || arguments.get("events").split(",").length == 0) {
            throw new IllegalArgumentException("No event subscriptions were provided for the server");
        }

        AppFw fw = AppFw.getInstance();

        // clean up code
        Runtime.getRuntime().addShutdownHook(new Thread() {
        @Override
            public void run() {
                System.out.println("Releasing resources...");
                fw.stopMainLoop();
                fw.close();
                System.out.println("Done");
            }   
        });

        // callback for event notifications
        fw.setEventCallback((String event, String json, Object userData) -> {
            System.out.println("Event: " + event 
                + " json data: " + json
                + " user data: " + (String)userData);
        },
        "this is my optional user data");
        
        // callback for event subscription change notifications
        fw.setStatusCallback(
                (int id, 
                 int status, 
                 String msg, 
                 String json, 
                 Object userData) -> {
            System.out.println("Got status update with id " + id + " and json: " + json);
            if (status == 0) {
                System.out.println(
                    "Status OK, succesfully subscribed to events " 
                    + ((AppFw)userData).getEventSubscriptions());
            } else {
                System.out.println(
                    "An error occured while subscribing to events: " + msg);
            }
        }, fw.getInstance()); // pass as user data for demonstration purposes
    }

    public static void beServer(Map<String, String> arguments) {

        String [] events = arguments.get("events").split(",");

        AppFw fw = AppFw.getInstance();
        fw.setEventSubscriptions(new HashSet<String>(Arrays.asList(events)));        
        fw.startMainLoop();
    }


    public static void beClient(Map<String, String> arguments) {
        AppFw fw = AppFw.getInstance();

        int values = 0;

        TargetApplication targetApplication = new TargetApplication();

        if (arguments.containsKey("label")) {
            targetApplication.setLabel(arguments.get("label"));
            ++values;
        }

        if (arguments.containsKey("user")) {
            targetApplication.setUser(arguments.get("user"));
            ++values;
        }

        if (arguments.containsKey("appid")) {
            targetApplication.setAppID(arguments.get("appid"));
            ++values;
        }

        if (arguments.containsKey("binary")) {
            targetApplication.setBinary(arguments.get("binary"));
            ++values;
        }        

        if (arguments.containsKey("process")) {
            int processID = 0;
            try {
                processID = Integer.parseInt(arguments.get("process"));
            } catch (NumberFormatException ex) {
                throw new IllegalArgumentException(
                    "Process argument must be an integer");
            }
            targetApplication.setProcess(processID);
            ++values;
        }        

        if (values == 0) {
            throw new IllegalArgumentException("At least one of label, user, "
                + "appid, binary path or process id must be provided when "
                + "sending events");
        }

        sendIntervalEvents(arguments, targetApplication);
        

        fw.startMainLoop();
    }

    public static void sendIntervalEvents(Map<String, String> arguments, 
        final TargetApplication targetApplication) {
        int interval;
        int eventBundles = 0;
        try {
            interval = Integer.parseInt(arguments.get("Interval"));
        } catch (NumberFormatException ex) {
            throw new IllegalArgumentException("Interal argument must be an integer");
        }
        try {
            if (arguments.containsKey("nevent")) {
                eventBundles = Integer.parseInt(arguments.get("nevent"));
            } else {
                eventBundles = 1;
            }
        } catch (NumberFormatException ex) {
            throw new IllegalArgumentException("nevents argument must be an integer");
        }


        final int finalInterval = interval;
        final int finalEventBundles = eventBundles;
        final String [] events = arguments.get("events").split(",");
        // AppFw is not thread safe and native access must be synchronized
        new Thread(() -> {
            for (int i = finalEventBundles; i > 0; --i) {
                // at this point, fw has been created so this merely returns the reference
                AppFw fw = AppFw.getInstance();
                synchronized (fw) {
                    for (int j = 0; j < events.length; ++j) {
                        fw.sendEvent(
                            events[j], 
                            "{\"example_key\": \"Example value\"}",
                            targetApplication,
                            (int id, int status, String message, Object userData) -> {
                                System.out.println(
                                    "Event send callback called with status "
                                    + status + ", message " + message 
                                    + " and user data " + (String)userData);
                            },
                            "user_data_here"
                            );
                    }
                }

                try {
                    Thread.sleep(finalInterval);;
                } catch (InterruptedException ex) {
                    System.out.println("Interval sleep was interrupted...");
                }
            }
            System.out.println("Interval sending finished");
        }).start();
    }
}

