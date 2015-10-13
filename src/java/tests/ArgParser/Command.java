package ArgParser;

import java.util.Iterator;

public class Command {
    public final String help;
    public final String longForm;   
    public final char shortForm;

    public Command(String longForm, String help) {
        this.longForm = longForm;
        this.shortForm = longForm.charAt(0);
        this.help = help;
    }

    @Override
    public String toString() {
        return "-"  + shortForm + " --" + longForm + ": " + help;
    } 

    public String parse(Iterator<String> iter) {
        return "";
    }
}