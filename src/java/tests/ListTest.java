import ArgParser.*;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import com.intel.ostro.appfw.*;

public class ListTest {
    public static void main(String [] args) {
        ArgParser parser = new ArgParser(args);
        addCommands(parser);
        Map<String, String> arguments = parser.parse();
        if (arguments.containsKey("help")) {
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

        if (arguments.containsKey("running")) {
            printRunningApps();
        } else {
            printAllApps();
        }
    }

    public static void addCommands(ArgParser parser) {
        parser.addCommand(new Command("help", "Prints help"));
        parser.addCommand(new Command("running", "Prints only running programs"));
        parser.addCommand(new CommandWithArg("debug", 
            "Turn debugging on (valid arguments are none, limited, all)"));
        
    }

    public static void commonInit(Map<String, String> arguments) {

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
    }

    public static void printRunningApps() {
        AppFw fw = AppFw.getInstance();
        fw.getRunningApplications((
                int id, 
                int status, 
                String message,
                IoTApplication [] applications,
                Object userData) -> {

            System.out.println("Running applications: ");

            System.out.println("ID: " + id + " status:" + status + 
                " message: " + message + " userdata: " + (String)userData + 
                " application count: " + applications.length);
            System.out.println("");
            for (IoTApplication app : applications) {
                System.out.println("App id: " + app.getAppID());
                System.out.println("App description: " + app.getDescription());
                System.out.println("App desktop: " + app.getDesktop());
                System.out.println("App user id: " + app.getUserID());
                System.out.println("App args: ");
                int i = 1;
                for (String arg : app.getArgs()) {
                    System.out.println("\t" + i++ + ": " + arg);
                }

                System.out.println("");
            }            

        },
        "callback_user_data");
        fw.startMainLoop();
    }

    public static void printAllApps() {
        AppFw fw = AppFw.getInstance();
                fw.getAllApplications((
                    int id, 
                    int status, 
                    String message,
                    IoTApplication [] applications,
                    Object userData) -> {
        
            System.out.println("All applications: ");

            System.out.println("ID: " + id + " status:" + status + 
                " message: " + message + " userdata: " + (String)userData + 
                " application count: " + applications.length);
            System.out.println("");
            for (IoTApplication app : applications) {
                System.out.println("App id: " + app.getAppID());
                System.out.println("App description: " + app.getDescription());
                System.out.println("App desktop: " + app.getDesktop());
                System.out.println("App user id: " + app.getUserID());
                System.out.println("App args: ");
                int i = 1;
                for (String arg : app.getArgs()) {
                    System.out.println("\t" + i++ + ": " + arg);
                }

                System.out.println("");
            }            

        },
        "callback_user_data");
        fw.startMainLoop();
    }
}

