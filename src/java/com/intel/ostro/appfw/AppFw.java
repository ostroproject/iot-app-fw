package com.intel.ostro.appfw;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
* This is the application framework singleton class. It provides access to the
* IoT application framework api
* <p>
* Note that this class is NOT thread safe. Caller MUST synchronize any calls
* if this class is used from multiple threads 
* <p/>
*
*
**/
public class AppFw {
    
    static {
        System.loadLibrary("javawrapper");
    }
    // This class is a singleton, as the C code relies on certain
    // global data structures that would get corrupted if multiple
    // instances of this class were created.
    private static AppFw instance = null;
    private Set<String> subscriptions;
    private Map<Integer, EventSendCallbackData> eventSendCallbacks;
    private Map<Integer, ListCallbackData> listCallbacks;


    private EventCallback eventCallback;
    private Object eventCallbackUserData; 

    private StatusCallback statusCallback;
    private Object statusCallbackUserData;


    // This function is accessed from C code. Do not change
    // the name or arguments unless C code is updated & recompiled.
    //
    // Helper function for event callback invocation
    private void eventCallbackWrapper(
                String event, 
                String json) {
        if (eventCallback != null) {
            eventCallback.invoke(event, json, eventCallbackUserData);
        }
    }

    // This function is accessed from C code. Do not change
    // the name or arguments unless C code is updated & recompiled.
    //
    // Helper function for status callback invocation
    private void statusCallbackWrapper(
                int id, 
                int status, 
                String message, 
                String jsonString) {
        if (statusCallback != null) {
            statusCallback.invoke(
                id, status, message, jsonString, statusCallbackUserData);
        }

    }
    // As above, accessed from C. Do not change the name or arguments.
    //
    // Helper function for event send callback invocation
    private void eventSendCallbackWrapper(
                int callbackID, 
                int eventID,
                int status,
                String message) {
        EventSendCallbackData data = eventSendCallbacks.get(callbackID);
        eventSendCallbacks.remove(callbackID);
        // This should never happen during normal execution
        if (data == null) {
            throw new RuntimeException(
                "Internal app framework error: Event send callback data" +
                " was not stored correctly. This is a bug.");
        }

        data.invoke(eventID, status, message);
    }

    // As above, accessed from C.
    private void listCallbackWrapper(
                int callbackID,
                int id,
                int status,
                String message,
                IoTApplication [] applications) {
        ListCallbackData data = listCallbacks.get(callbackID);
        listCallbacks.remove(callbackID);
        // This should never happen during normal execution.
        if (data == null) {
            throw new RuntimeException(
                "Internal app framework error: List callback data" +
                " was not stored correctly. This is a bug.");
        }

        data.invoke(id, status, message, applications);
    }

    /**
    * Returns the AppFw instance, if it is already created.
    * If not, also creates the instance.
    *
    * @return AppFw instance
    *
    **/
    public static AppFw getInstance() {
        if (instance == null) {
            instance = new AppFw();
        }
        return instance;
    }

    /**
    *
    * Class constructor
    *
    **/
    private AppFw() {
        createAppFwContext();
        eventSendCallbacks = new HashMap<>();
        listCallbacks = new HashMap<>();
    }
    /**
    * Sets the event callback that is called when an event this app has 
    * subscribed to happens, and the optional object which is provided to the
    * callback on invocation.
    *
    * @param callback the callback object
    * @param userData optional data for the callback
    *
    **/
    public void setEventCallback(EventCallback callback, Object userData) {
        eventCallback = callback;
        eventCallbackUserData = userData;
    }

    /**
    * Sets the status callback that is called when the evet subscriptions
    * are updated, and the optional object which is provided for the 
    * callback on invocation. 
    *
    * @param callback callback object
    * @param userData optional data for the callback
    *
    **/
    public void setStatusCallback(StatusCallback callback, Object userData) {
        statusCallback = callback;
        statusCallbackUserData = userData;
    }

    /**
    * Sends event to the other IoT applications
    * <p>
    * This is asynchronous operation. On completion, event send callback is
    * invoked.
    * </p>
    *
    * @param event              the name of the event that is being sent, 
    *                           not null
    * @param eventJsonData      optional event json data. Should be empty object 
    *                           if not needed, not null    
    * @param targetApplication  object specifying the target applications, 
    *                           not null
    * @param callback           callback that is invoked when operation is 
    *                           completed
    * @param userData           optional user data for the callback
    *
    **/
    public void sendEvent(
                String event, 
                String eventJsonData,                
                TargetApplication targetApplication, 
                EventSendCallback callback, 
                Object userData
                ) {

        if (event == null) {
            throw new IllegalArgumentException(
                "Event string cannot be null");
        }

        if (eventJsonData == null) {
            throw new IllegalArgumentException(
                "Event json data cannot be null");
        }

        if (targetApplication == null) {
            throw new IllegalArgumentException(
                "Target application cannot be null");
        }

        if (!targetApplication.targetSet()) {
            throw new IllegalArgumentException(
                "No target application was specified");
        }

        EventSendCallbackData data = 
            new EventSendCallbackData(callback, userData);
        eventSendCallbacks.put(data.getID(), data);
        sendEventNative(event, eventJsonData, data.getID(), targetApplication);
    }
    
    /**
    * Query application framework for running applications
    * <p>
    * This is asynchronous operation. On completion, list callback is invoked
    * </p>
    *
    * @param callback the callback object
    * @param userData optional user data for the callback
    */
    public void getRunningApplications(
                ListCallback callback, 
                Object userData) {
        ListCallbackData data = new ListCallbackData(callback, userData);
        
        listCallbacks.put(data.getID(), data);
        getRunningApplicationsNative(data.getID());
    }

    /**
    * Query application framework for all applications
    * <p>
    * This is asynchronous operation. On completion, list callback is invoked.
    * </p>
    *
    * @param callback the callback object
    * @param userData optional user data for the callback
    */
    public void getAllApplications(
                ListCallback callback, 
                Object userData) {
        ListCallbackData data = new ListCallbackData(callback, userData);
        
        listCallbacks.put(data.getID(), data);
        getAllApplicationsNative(data.getID());
    }

    /**
    * Sets the set of events the application subscribes to.
    * <p>
    * Calling this function automatically calls 
    * {@link #updateEventSubscriptions() updateEventSubscriptions()} 
    * </p>
    *
    * 
    * @param subscriptions set of events the program wants to subscribe to, 
    *                      not null
    *
    **/
    public void setEventSubscriptions(Set<String> subscriptions) {
        if (subscriptions == null) {
            throw new IllegalArgumentException(
                "Subscription set cannot be null");
        }
        this.subscriptions = subscriptions;
        updateEventSubscriptions();
    }

    /**
    * Returns the set of events this application has subscribed to.
    * <p>
    * Note that in-place modification does not automatically update the
    * subscriptions and call to 
    * {@link #updateEventSubscriptions() updateEventSubscriptions()} is required
    * </p>
    *
    * @return The set of events this application has subscribed to.
    *
    */
    public Set<String> getEventSubscriptions() {
        return subscriptions;
    }

    /**
    * Updates the event subscriptions
    * <p>
    * This is asynchronous operation. On completion, status callback is invoked.
    * <p/>
    *
    **/
    public void updateEventSubscriptions() {
        String [] strings = subscriptions.
            toArray(new String[subscriptions.size()]);
        if (Arrays.stream(strings)
            .anyMatch((s) -> s == null)) {
            throw new IllegalArgumentException("Event string cannot be null");
        }
        updateEventSubscriptionsNative(strings);
    }
    /**
    * Enables application framework debugging
    * <p>
    * Following formats are supported:<br/>
    * * - enables all debugging <br/>
    * {@literal @}Foo.c - enables logging in file Foo.c <br/>
    * doBar - enables logging in function doBar <br/>
    * </p>
    * @param debugStrings list of debug strings that control which parts of the
    *                     debugging is enabled, not null
    * 
    */
    public static void enableDebug(List<String> debugStrings) {
        if (debugStrings == null) {
            throw new IllegalArgumentException(
                "Debug string list cannot be null");
        }

        String [] strings = debugStrings.
            toArray(new String[debugStrings.size()]);

        if (Arrays.stream(strings)
            .anyMatch((s) -> s == null)) {
            throw new IllegalArgumentException("Debug string cannot be null");
        }

        enableDebugNative(strings);
    }
    /**
    * Starts the main event loop
    * <p>
    * This function does not return without call to 
    * {@link #stopMainLoop stopMainLoop}
    * </p>
    *
    */
    public void startMainLoop() {
        startMainLoopNative();
    }

    /**
    * Stops the main event loop
    */
    public void stopMainLoop() {
        stopMainLoopNative();
    }

    /**
    * Releases AppFw context and sets the instance to null.
    * <p>
    * This method must be called if {@link #getInstance() getInstance()} has 
    * been called at least once and once the program has no further need for
    * the application framework. Failure to do so will lead to resource leaks.
    * </p>
    * <p>
    * Calling this method is safe even if no AppFw instance is currently
    * in use. In this case the call does nothing.  
    * </p>
    **/
    public void close() {
        if (instance != null) {
            destroyAppFwContext();
            instance = null;       
        } 
    }

    private native void createAppFwContext();
    private native void destroyAppFwContext();

    private native void updateEventSubscriptionsNative(String [] subscriptions);
    private native void sendEventNative(
                String event, 
                String eventJsonData, 
                int callbackID,
                TargetApplication targetApplication);


    private native void getAllApplicationsNative(int callbackID);
    private native void getRunningApplicationsNative(int callbackID);

    private static native void enableDebugNative(String [] strings);     

    private native void startMainLoopNative();
    private native void stopMainLoopNative();
}