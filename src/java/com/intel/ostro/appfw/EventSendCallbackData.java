package com.intel.ostro.appfw;
/**
* Convenience class used by AppFw class to manage event send callbacks and their 
* optional user data
* <p> 
* This class is internal for the application framework and should not be used
* otherwise. 
* </p>
*
*/
class EventSendCallbackData {
    private static final IDCounter COUNTER = new IDCounter();

    private EventSendCallback callback;
    private Object userData;
    private final int id;

    /**
    * Class constructor
    *
    * @param callback callback function that is invoked by the framework 
    * @param userData optional data that is provided to the callback
    */
    public EventSendCallbackData(EventSendCallback callback, Object userData) {
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
    */
    public void invoke(int id, int status, String message) {
        if (callback != null) {
            callback.invoke(id, status, message, userData);
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