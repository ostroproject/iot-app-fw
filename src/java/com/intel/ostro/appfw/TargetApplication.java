package com.intel.ostro.appfw;

import java.io.InputStream;
import java.io.IOException;
import java.util.NoSuchElementException;
import java.util.Scanner;
/**
*
* Used to specify the target application(s) for event sending.
*
**/
public class TargetApplication {
    public static final String LABEL_UNUSED = null;
    public static final String APPID_UNUSED = null;
    public static final String BINARY_UNUSED = null;
    public static final String USER_UNUSED = null;
    public static final int PROCESS_UNUSED = 0;
    private static final int USERID_UNUSED = -1; // used internally

    // These fields are accessed by name from C code. Do not rename them
    // or change their types without updating & recompiling C code.
    private String label;
    private String appID;
    private String binary;
    private String user;
    private int process;

    /**
    * Creates new TargetApplication object with desired values. 
    * At least one value must be specified.
    *
    * @param label   SMACK label
    * @param appID   application id
    * @param binary  executed binary
    * @param user    the name of the (linux) user of the target
    * @param process the process id.
    *
    * @throws  If user name was specified and it could not be 
    * converted into userid
    */
    public TargetApplication(
                String label, 
                String appID, 
                String binary, 
                String user,
                int process) {
        this.label = label;
        this.appID = appID;
        this.binary = binary;
        this.user = user;
        this.process = process;
    }   
    /**
    * Creates new TargetApplication object with all fields set as unused.
    * <p>
    * Desired fields should be set through setters. 
    * </p>
    */
    public TargetApplication() {
        this.label = LABEL_UNUSED;
        this.appID = APPID_UNUSED;
        this.binary = BINARY_UNUSED;
        this.user = USER_UNUSED;
        this.process = PROCESS_UNUSED;
    }  

    public void setLabel(String label) {
        this.label = label;
    }

    public void setAppID(String appID) {
        this.appID = appID;
    }

    public void setBinary(String binary) {
        this.binary = binary;
    }

    public void setUser(String user) {
        this.user = user;
    }

    public void setProcess(int id) {
        this.process = process;
    }
    /**
    * Returns true if at least one field is set.
    * 
    * @return True, if at least one field is set 
    */
    public boolean targetSet() {
        return !(label == LABEL_UNUSED && appID == APPID_UNUSED && 
            binary == BINARY_UNUSED && user == USER_UNUSED && 
            process == PROCESS_UNUSED);
    }
}
