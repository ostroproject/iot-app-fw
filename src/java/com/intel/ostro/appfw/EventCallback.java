package com.intel.ostro.appfw;
/**
*
* Interface for callbacks which are invoked when an event occurs that the 
* application has subscribed to
*
*/
@FunctionalInterface
public interface EventCallback {
    /**
    *
    * The callback method that gets invoked when subscribed event occurs
    * 
    * @param event    Event name
    * @param json     Optional event data
    * @param userData userData Optional user provided object which was provided 
    *                          during the initial application list call.
    *
    */
    void invoke(String event, String json, Object userData);
}