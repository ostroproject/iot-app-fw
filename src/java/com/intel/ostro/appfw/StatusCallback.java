package com.intel.ostro.appfw;
/**
* Interface for callbacks which are invoked on event subscription changes.
*
*/
@FunctionalInterface
public interface StatusCallback {    
    /**
    * The callback method that gets invoked on event subscription changes
    * 
    * @param id       request id number
    * @param status   request status. 0 if successful, non-zero on failure
    * @param message  error message if status was non-zero 
    * @param json     Optional request-specific status data
    * @param userData Optional user provided object which was provided during 
    *                 setStatusCallback call
    */
    void invoke(
        int id, 
        int status, 
        String message, 
        String json, 
        Object userData);
}