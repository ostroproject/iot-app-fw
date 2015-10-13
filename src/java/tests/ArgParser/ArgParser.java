package ArgParser;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
/**
* Simple command line argument parser for test cases.
* <p>
* Primary motivation is to avoid dependencies to external libraries. Code is 
* intentionally "good enough" and as such is not really designed to be a 
* general purpose argument parser.
* </p>
*/
public class ArgParser {

    private List<String> args;
    private List<Command> commands;
    private Map<String, String> parsedArgs;

    public ArgParser(String [] args) {
        this.args = Arrays.asList(args);

        commands = new ArrayList<>();
        parsedArgs = new HashMap<>();
    }


    // add command and check if it conflicts with old ones
    public void addCommand(Command newCommand) {
        // Check for commands that might share long\short form
        for (Command c : commands) {
            if (c.shortForm == newCommand.shortForm ||
                c.longForm.equals(newCommand.longForm)) {
                throw new IllegalArgumentException(
                    "\nArg parser init failure: Ambiguous argument:\n  " + c 
                    + "\n  " + newCommand);
            }
        }

        commands.add(newCommand);
    }

    public Map<String, String> parse() {
        Iterator<String> iter  = args.iterator();
        if (args.size() == 0) {
            return parsedArgs;
        }

        while (iter.hasNext()) {
            String arg = iter.next();

            if (arg == null || arg.length() < 2 || arg.charAt(0) != '-' || 
                (arg.length() == 2 && arg.charAt(1) == '-')) {
                throw new IllegalArgumentException(
                    "Invalid command line argument '" + arg + "'");
            }

            if (arg.charAt(1) == '-') {
                parseLongForm(arg, iter);
            } else {
                parseShortForm(arg, iter);
            }
        }

        return parsedArgs;        
    }

    void parseLongForm(String arg, Iterator<String> iter) {
        String longForm = arg.substring(2);

        
        // could be optimized from O(n) to O(1) with hashmap, but should
        // not matter for this case
        for (Command c : commands) {
            if (c.longForm.equals(longForm)) {
                addArgument(c, iter);
                return;
            }
        }

        throw new IllegalArgumentException("Invalid argument --" 
            + longForm);       
    }

    void parseShortForm(String arg, Iterator<String> iter) {
        String commandList = arg.substring(1);

        outer:
        for (int i = 0; i < commandList.length(); ++i) {
    
            // could be optimized from O(n) to O(1) with hashmap, but should
            // not matter for this case
            for (Command c : commands) {
                if (c.shortForm == commandList.charAt(i)) {
                    addArgument(c, iter);                       
                    continue outer;
                }
            }
                throw new IllegalArgumentException("Invalid argument -" 
                    + commandList.charAt(i));
        }
    }

    private void addArgument(Command c, Iterator<String> iter) {
        if (parsedArgs.containsKey(c.longForm)) {
            throw new IllegalArgumentException(
                "Command line argument present multiple times: " + c);
        }
        parsedArgs.put(c.longForm, c.parse(iter));                        
             
    }

    public void printHelp() {
        System.out.print("Valid command line arguments:\n\n");
        for (Command c : commands) {
            System.out.println(c);
        }
    }
}
