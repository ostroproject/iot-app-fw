package com.intel.ostro.appfw;
/**
*
* Interface for callbacks which are invoked when event has been sent 
*
*/
@FunctionalInterface
public interface EventSendCallback {
    /**
    * The callback method that gets invoked when event has been sent
    * 
    * @param id           request id number
    * @param status       request status. 0 if successful, non-zero on failure
    * @param message      error message if status was non-zero 
    * @param userData     Optional user provided object which was provided 
    *                     during the initial application list call.
    */
    void invoke(int id, int status, String message, Object userData);
}