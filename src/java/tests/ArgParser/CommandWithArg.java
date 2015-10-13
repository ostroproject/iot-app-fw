package ArgParser;

import java.util.Iterator;

public class CommandWithArg extends Command {

    public CommandWithArg(String longForm, String help) {
        super(longForm, help);
    }

    @Override
    public String parse(Iterator<String> iter) {
        if (!iter.hasNext()) {
            throw new IllegalArgumentException("Command requires an argument: "
                + toString());
        }

        String arg = iter.next();
        if (arg.length() == 0 || arg.charAt(0) == '-') {
            throw new IllegalArgumentException("Invalid argument '" + arg + "'"); 
        }
        return arg;
    }
}