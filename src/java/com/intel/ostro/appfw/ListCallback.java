package com.intel.ostro.appfw;
/**
*
* Interface for callbacks which are invoked when requesting for application list
*
*/
@FunctionalInterface
public interface ListCallback {
    /**
    * The callback method that gets invoked when framework has retrieved the 
    * application list
    * 
    * @param id           request id number
    * @param status       request status. 0 if successful, non-zero on failure
    * @param message      error message if status was non-zero 
    * @param applications IoT applications
    * @param userData     Optional user provided object which was provided 
    *                     during the initial application list call.
    */
    void invoke(
        int id, 
        int status, 
        String message, 
        IoTApplication[] applications, 
        Object userData);
}