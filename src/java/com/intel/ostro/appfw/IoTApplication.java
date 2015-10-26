package com.intel.ostro.appfw;
/**
* Class containing the values for an individual IoT application. Used by 
* {@link #ListCallback ListCallback}  
*
*/
// this class and its constructor are used from C code. Do not rename variables
// or change types, or lookup will fail.
public class IoTApplication {
    private final String appID;
    private final String description;
    private final String desktop;
    private final int userID;
    private final String [] args;
    /**
    * Class constructor
    *
    * @param appID       id
    * @param description description
    * @param desktop     desktop entry directory path
    * @param userID      user id
    * @param args        command line arguments 
    *
    */
    public IoTApplication(
                String appID, 
                String description, 
                String desktop,
                int userID,
                String [] args ) {
        this.appID = appID;
        this. description = description;
        this.desktop = desktop;
        this.userID = userID;
        this.args = args;
    }

    public String getAppID() {
        return appID;
    }

    public String getDescription() {
        return description;
    }

    public String getDesktop() {
        return desktop;
    }

    public int getUserID() {
        return userID;
    }

    public String [] getArgs() {
        return args;
    }
}