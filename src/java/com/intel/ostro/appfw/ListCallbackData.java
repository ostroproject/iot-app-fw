package com.intel.ostro.appfw;
/**
* Convenience class used by AppFw class to manage list callbacks and their 
* optional user data
* <p> 
* This class is internal for the application framework and should not be used
* otherwise. 
* </p>
*
*/
class ListCallbackData {
    private static final IDCounter COUNTER = new IDCounter();

    private ListCallback callback;
    private Object userData;
    private final int id;

    /**
    * Class constructor
    *
    * @param callback callback function that is invoked by the framework 
    * @param userData optional data that is provided to the callback
    */
    public ListCallbackData(ListCallback callback, Object userData) {
        this.callback = callback;
        this.userData = userData;
        id = COUNTER.nextID();
    } 

    /**
    * Invokes the callback
    * 
    * @param id           request id
    * @param status       request status, 0 on success, non-zero on failure
    * @param message      error message
    * @param applications IoT applications that were requested
    */
    public void invoke(
                int id, 
                int status, 
                String message, 
                IoTApplication [] applications) {
        if (callback != null) {
            callback.invoke(id, status, message, applications, userData);
        }
    }
    
    /**
    * Returns callback id
    *
    * @return callback id
    **/
    public int getID() {
        return id;
    }
}