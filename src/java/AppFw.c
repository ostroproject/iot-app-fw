/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "com_intel_ostro_appfw_AppFw.h"
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>

#include <iot/app.h>
#include <iot/common/glib-glue.h>
#include <iot/common/debug.h>

/************************************
* Globals
*************************************/
/*
Event handler callback is annoying in that we must call the java callback
but the event handler does not allow us to pass java context in.

This means we have to have hold whatever data we need in global variables.

Other globals are for convenience. Code could be restructured[1] to avoid these,
but since we need a global for the event handler callback anyway, we might as 
well make the function calls a bit simpler.

[1] Create struct that holds any extra variables and malloc an instance on app 
initialization. Give pointer to Java, pass pointer back to C code on each JNI 
function invokation. Release when app is being destroyed. 
*/

// reference to the java singleton
jobject java_fw_obj; 
// GLib main event loop
GMainLoop* main_loop;

// iot_app context
iot_app_t* iot_app;

/* 
Jvm can be safely used from a global context. Having a global variable
also means we can simply initialize this in JNI_OnLoad-function and then
have jvm ready for any function that may require it.
*/
JavaVM *jvm;

/**
* @brief Initialization function which is called on java VM load
*
* @param [in] vm         Pointer to the Java virtual machine
* @param [in] aReserved  Unused
*
* @return                Required Java native interface level 
*/
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void *aReserved)
{
    IOT_UNUSED(aReserved);
    // cache java VM for future use
    jvm = vm;
    return JNI_VERSION_1_8;
}


/************************************
* Helper functions and macros
*************************************/

/**
* @brief Checks if ____statement is NULL and prints error message and exits if 
*        so
*
* IOT_ASSERT defined in macros.h is similar, but it does nothing if NDEBUG is 
* defined. This one is usable even in release mode
*
*/
#define EXIT_IF_NULL(____statement, ____message) \
do \
{ \
    if((____statement) == NULL) { \
        fprintf(stderr, ____message); \
        fprintf(stderr, "\n"); \
        exit(-1); \
    } \
} while (false)

/**
* @brief Checks if ____statement is false and prints error message and exits if 
*        so
*
* IOT_ASSERT defined in macros.h is similar, but it does nothing if NDEBUG is 
* defined. This one is usable even in release mode
*
*/
#define EXIT_IF_FALSE(____statement, ____message) \
do \
{ \
    if(!(____statement)) { \
        fprintf(stderr, ____message); \
        fprintf(stderr, "\n"); \
        exit(-1); \
    } \
} while (false)


/**
* @brief Helper function for throwing IllegalArgumentExceptions.
* 
* This wraps the logic needed for throwing IA exceptions back to Java.
* If exception class is not found for some reason then a) something has gone
* horribly wrong and b) we print error message into stderr and exit
*
* 
* If JNIEnv is NULL, an error is printed to stderr and program exits. 
*
* @param [in] env      Java native interface function table
* @param [in] message  The exception message
*
*/
void throw_illegal_argument_exception( JNIEnv *env, const char *message )
{
    EXIT_IF_NULL(env, "JNIEnv is null when attempting to throw exception");

    jclass exClass;
    exClass = (*env)->FindClass( env, "java/lang/IllegalArgumentException");

    EXIT_IF_NULL(exClass, 
            "Could not find IllegalArgumentException class. This is bad.");

    EXIT_IF_FALSE((*env)->ThrowNew( env, exClass, message ) == 0, 
        "Failed to throw IllegalArgumentException");
}

/**
* @brief Helper function for throwing AppFw exceptions.
* 
* This wraps the logic needed for throwing AppFw exceptions back to Java.
* If exception class is not found for some reason then a) something has gone
* horribly wrong and b) we print error message into stderr and exit
*
* 
* If JNIEnv is NULL, an error is printed to stderr and program exits. 
*
* @param [in] env      Java native interface function table
* @param [in] message  The exception message
*
*/
void throw_appfw_exception( JNIEnv *env, const char *message )
{
    EXIT_IF_NULL(env, "JNIEnv is null when attempting to throw exception");

    jclass exClass;                   
    exClass = (*env)->FindClass( env, "com/intel/ostro/appfw/AppFwException");

    EXIT_IF_NULL(exClass, 
            "Could not find AppFwException class. This is bad.");

    EXIT_IF_FALSE((*env)->ThrowNew( env, exClass, message ) == 0, 
        "Failed to throw AppFwException");
}


/**
* @brief Helper function for getting JNI env from jvm.
*
* If env pointer could not be fetched, an error is printed to stderr and 
* program exits.
*
* Note: This function assumes we never call this from a different C thread.
* If it is called from another thread, getEnvSat has a value indicating env
* must be first attached to the JVM (and then deattached afterwards).
* This is currently not handled.  
*
*
* @return Java native interface 
*/
JNIEnv *get_jni_env(void) 
{
    JNIEnv * env;

    int getEnvStat = (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_8);

    EXIT_IF_FALSE(getEnvStat == JNI_OK, 
        "An error occured while attempting to get environment from JVM");
    return env;          
}




/**
* @brief Helper macro for reducing error handling verbosity
*
* Throws Java exception and then jumps to fail label 
*
* @param [in] env      JNI interface pointer 
* @param [in] pointer  Pointer for which the null check is performed
* @param [in] msg      C string containing the error message if pointer is null
*
*/
#define THROW_AND_GOTO_FAIL_IF_NULL(____env, ____pointer, ____msg) \
do \
{ \
    if ((____pointer) == NULL) \
    { \
        throw_illegal_argument_exception((____env), (____msg)); \
        goto fail; \
    } \
} while (false)

/**
* @brief Helper macro for reducing error handling verbosity
*
* Throws Java exception and then returns. This is a variant of 
* THROW_AND_GOTO_FAIL_IF_NULL for cases where no additional logic is required. 
*
* @param [in] env      JNI interface pointer 
* @param [in] pointer  Pointer for which the null check is performed
* @param [in] msg      C string containing the error message if pointer is null
*
*/
#define THROW_AND_RETURN_IF_NULL(____env, ____pointer, ____msg) \
do \
{ \
    if ((____pointer) == NULL) \
    { \
        throw_illegal_argument_exception((____env), (____msg)); \
        return; \
    } \
} while (false)


/************************************
* C callback wrappers
*************************************/

/**
 * @brief Java event callback wrapper
 *
 * Callback function to be invoked for notifying the application
 * about events received by the application.
 *
 * Forwards these events to Java EventCallback stored in AppFw class
 *
 * @param [in] app    Application context (unused)
 * @param [in] event  Event type
 * @param [in] data   Event data as JSON  
 *
 */
void event_callback_wrapper(iot_app_t* app, const char* event,
                           iot_json_t* data)
{
    IOT_UNUSED(app);
    iot_debug("event_callback_wrapper");
    JNIEnv *env = get_jni_env();

    // get the callback field from our java fw object
    jclass fw_class = (*env)->GetObjectClass(env, java_fw_obj);
    EXIT_IF_NULL(fw_class, "Event callback error: Failed to find AppFw class");

    jmethodID mid = (*env)->GetMethodID(
        env, 
        fw_class, 
        "eventCallbackWrapper", "(Ljava/lang/String;Ljava/lang/String;)V");

    EXIT_IF_NULL(mid, 
        "Event callback error: Failed to get method reference for event callback wrapper");
  
    // iot_json_object_to_string returns pointer to internal buffer stored
    // in data; this buffer will be released when the data object is released,
    // thus we do not need to worry about memory allocations.
    const char* json_string = iot_json_object_to_string(data);

    iot_debug("Received json data as string: %s", 
        json_string ? json_string : "<empty>");

    (*env)->CallVoidMethod(
        env, 
        java_fw_obj, 
        mid, 
        (*env)->NewStringUTF(env, event),
        (*env)->NewStringUTF(env, json_string));
}



/**
 * @brief Java status callback wrapper. 
 *
 * Forwards status updates to Java status callback
 *
 * @param [in] app        Application context (unused)
 * @param [in] id         Sequence number of associated request
 * @param [in] status     Request status (0 ok, non-zero error)
 * @param [in] msg        Error message
 * @param [in] data       Optional request-specific status data
 * @param [in] user_data  Opaque user data supplied for the request (unused) 
 */
void status_callback_wrapper(iot_app_t* app, int id, int status,
                             const char* msg, iot_json_t* data,
                             void* user_data)
{
    IOT_UNUSED(app);
    IOT_UNUSED(user_data);
    iot_debug("status_callback_wrapper");

    JNIEnv *env = get_jni_env();

    // get the status callback method from our java fw object
    jclass fw_class = (*env)->GetObjectClass(env, java_fw_obj);
    EXIT_IF_NULL(fw_class, 
        "Status callback error: Failed to find AppFw class");

    jmethodID mid = (*env)->GetMethodID(
        env, 
        fw_class, 
        "statusCallbackWrapper", "(IILjava/lang/String;Ljava/lang/String;)V");

    EXIT_IF_NULL(mid, 
        "Status callback error: Failed to get method reference for status callback wrapper");
    
    // iot_json_object_to_string returns pointer to internal buffer stored
    // in data; this buffer will be released when the data object is released,
    // thus we do not need to worry about memory allocations.
    const char* json_string = iot_json_object_to_string(data);

    (*env)->CallVoidMethod(
        env, 
        java_fw_obj, 
        mid, 
        (jint)id,
        (jint)status,
        (*env)->NewStringUTF(env, msg),
        (*env)->NewStringUTF(env, json_string));
}

/**
* @brief IoT event delivery notification callback wrapper
* 
* Forwards event delivery notifications to Java event send callback
*
* @param [in] app        Application context (unused)
* @param [in] id         Sequence number for event
* @param [in] status     Request status (0 ok, non-zero error)
* @param [in] msg        Error message
* @param [in] user_data  Opaque user data supplied for the request. Used to
*                        pass Java send event handler callback id (jint)
*/
void send_callback_wrapper(iot_app_t* app, int id, int status,
                           const char* msg, void* user_data)
{
    IOT_UNUSED(app);
    iot_debug("send_callback_wrapper");
    JNIEnv *env = get_jni_env();

    // get the event send callback method from our java fw object
    jclass fw_class = (*env)->GetObjectClass(env, java_fw_obj);

    EXIT_IF_NULL(fw_class, 
        "Send callback error: Failed to find AppFw class");

    jmethodID mid = (*env)->GetMethodID(
        env, 
        fw_class, 
        "eventSendCallbackWrapper", "(IIILjava/lang/String;)V");

    EXIT_IF_NULL(mid, 
        "Send callback error: Failed to get method reference");

    jint callback_id = (jint)(ptrdiff_t)user_data;

    (*env)->CallVoidMethod(
        env,
        java_fw_obj, 
        mid, 
        (jint)callback_id,
        (jint)id,
        (jint)status,
        (*env)->NewStringUTF(env, msg));
}

/**
* @brief Helper function for list_callback_wrapper that creates, populates and
*        returns a java object array with application data that C api provides.  
*
* @param [in] env   JNI interface pointer
* @param [in] napp  Number of applications in apps array
* @param [in] apps  Array of applications
*
* @return           jobjectArray of IotApplication objects, containing the
                    application information in a form JVM understands.
*/
jobjectArray get_app_array(JNIEnv *env, 
    const int napp, 
    const iot_app_info_t *apps)
{
    jclass iot_application_class = (*env)->FindClass(env, 
        "com/intel/ostro/appfw/IoTApplication");
    jclass string_class = (*env)->FindClass(env, "java/lang/String");

    EXIT_IF_NULL(iot_application_class, 
        "List callback error: Failed to find IoTApplication class");

    EXIT_IF_NULL(string_class, 
        "List callback error: Failed to find Java String class");

    jobjectArray app_array = (*env)->NewObjectArray(
                env, 
                (jsize)napp, 
                iot_application_class, 
                NULL);

    EXIT_IF_NULL(app_array, 
        "List callback error: Failed to create IotApplication array");

    jmethodID cstrID = (*env)->GetMethodID(
        env, 
        iot_application_class, 
        "<init>", 
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I[Ljava/lang/String;)V");


    for (int i = 0; i < napp; ++i)
    {
        if (apps[i].argc > 0)
        {
            EXIT_IF_NULL(apps[i].argv, 
                "List callback error: Uninitialized argument list");
        }

        jobjectArray arg_array = (*env)->NewObjectArray(
                env, 
                (jsize)apps[i].argc, 
                string_class, 
                NULL);

        EXIT_IF_NULL(arg_array, 
            "List callback error: Failed to create program argument array");

        for (int j = 0; j < apps[i].argc; ++j) 
        {
            (*env)->SetObjectArrayElement(
                env, 
                arg_array, 
                j, 
                (*env)->NewStringUTF(env, apps[i].argv[j]));
        }

        jobject obj = (*env)->NewObject(
                    env, 
                    iot_application_class,
                    cstrID,
                    (*env)->NewStringUTF(env, apps[i].appid),
                    (*env)->NewStringUTF(env, apps[i].description),
                    (*env)->NewStringUTF(env, apps[i].desktop),
                    (jint)apps[i].user,
                    arg_array
                    );

       (*env)->SetObjectArrayElement(
            env, 
            app_array, 
            i, 
            obj);    
    }    

    return app_array;
}
/**
* @brief IoT application listing notification callback wrapper
* 
* Forwards the requested application list to the Java program
*
* @param [in] app        Application context (unused)
* @param [in] id         Sequence number for event
* @param [in] status     Request status (0 ok, non-zero error)
* @param [in] msg        Error message
* @param [in] napp       Number of IoT apps in the array
* @param [in] apps       Array of IoT apps
* @param [in] user_data  User data supplied to the request. Used to pass Java
*                        list handler callback id (jint)  
*
*/
void list_callback_wrapper(iot_app_t *app, int id, int status, const char *msg,
                           int napp, iot_app_info_t *apps, void *user_data)
{
    IOT_UNUSED(app);
    JNIEnv *env = get_jni_env();

    iot_debug("list_callback_wrapper");

    if (napp > 0) 
    {
        EXIT_IF_NULL(apps,
            "List callback error: Uninitialized application list");
    }

    // get the list callback method from our java fw object
    jclass fw_class = (*env)->GetObjectClass(env, java_fw_obj);
    EXIT_IF_NULL(fw_class,
        "List callback error: Failed to find AppFw class");
    
    jmethodID mid = (*env)->GetMethodID(
        env, 
        fw_class, 
        "listCallbackWrapper", 
        "(IIILjava/lang/String;[Lcom/intel/ostro/appfw/IoTApplication;)V");
    EXIT_IF_NULL(mid,
        "List callback error: Failed to get method reference");

    jobjectArray app_array = get_app_array(env, napp, apps);

    int callback_id = (int)(ptrdiff_t)user_data;
    (*env)->CallVoidMethod(
        env, 
        java_fw_obj, 
        mid, 
        (jint)callback_id,
        (jint)id,
        (jint)status,
        (*env)->NewStringUTF(env, msg),
        app_array);
}


/************************************
* JNI functions
*************************************/


/**
* @brief JNI function for creating application context
*
* Initializes the framework and its various components.
*
* @param [in] env   JNI interface pointer
* @param [in] this  Caller object
* 
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_createAppFwContext
  (JNIEnv * env, jobject this) 
{

    iot_debug("called Java_AppFw_createAppFwContext");

    // Setup iot_app owned by this library
    main_loop = g_main_loop_new(NULL, false);
    EXIT_IF_NULL(main_loop,
        "GLib mainloop creation failed");

    iot_mainloop_t* iot_ml = iot_mainloop_glib_get(main_loop);
    EXIT_IF_NULL(iot_ml,
        "Failed to attach GLib mainloop to Iot application");

    iot_app = iot_app_create(iot_ml, NULL);
    EXIT_IF_NULL(iot_app,
        "IoT application creation failed");

    iot_app_event_set_handler(iot_app, event_callback_wrapper);

    // create global reference, thus preventing Java gc
    // from releasing this object. This also means we must 
    // explicitly release it or we get memory leaks
    java_fw_obj = (*env)->NewGlobalRef(env, this);

    iot_debug("Library initialized.");
}
/**
* @brief JNI function for destroying the application context
*
* Releases various resources and destroys the application context
*
* @param [in] env   JNI interface pointer
* @param [in] this  Caller object (unused)
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_destroyAppFwContext
  (JNIEnv * env, jobject this) 
{
    IOT_UNUSED(this);
    iot_debug("called Java_AppFw_destroyAppFwContext");
    iot_mainloop_unregister_from_glib(iot_app_get_mainloop(iot_app));

    iot_app_destroy(iot_app);
    g_main_loop_unref(main_loop);

    // release global references to prevent any memory leaks on java side
    (*env)->DeleteGlobalRef(env, java_fw_obj);
}

/**
* @brief JNI function for registering application event subscriptions
*
* @param [in] env            JNI interface pointer
* @param [in] this           Caller object (unused)
* @param [in] subscriptions  Java String array containing the events the 
*                            application wants to subscribe to
*/
JNIEXPORT void JNICALL 
Java_com_intel_ostro_appfw_AppFw_updateEventSubscriptionsNative
  (JNIEnv *env, jobject this, jobjectArray subscriptions) 
{
    IOT_UNUSED(this);
    iot_debug("called Java_AppFw_updateEventSubscriptionsNative");

    THROW_AND_RETURN_IF_NULL(env, subscriptions,
        "Subscription array cannot be null - aborting event subscription");

    const jsize length = (*env)->GetArrayLength(env, subscriptions);

    // event array must be null terminated, so we add one to the length so that
    // have space for this sentinel value
    const size_t event_array_length = (size_t)length + 1;
    
    char **events = NULL;
    events = iot_allocz(sizeof(char *)*event_array_length);
  
    // As this is currently the only case of oom error used, I felt creating
    // helper macro for it would be a waste. If you disagree, feel free to 
    // create one.
    EXIT_IF_NULL(events,
        "Failed to allocate memory for event list");

    for (size_t i = 0; i < event_array_length; ++i) 
    {
        events[i] = NULL;
    }

    for (int i = 0; i < length; ++i) 
    {
        jstring j_string = (jstring)(*env)->GetObjectArrayElement(
            env, 
            subscriptions, 
            i);
        // skipping null strings might be an alternative, but I feel that 
        // notifying the program(mer) of a potential bug is better
        THROW_AND_GOTO_FAIL_IF_NULL(env, j_string, 
            "Event strings cannot be null - aborting event registration");

        // silence warning when assigning const char * into char *
        events[i] = (char *)(*env)->GetStringUTFChars(env, j_string, NULL);
    }

    int err = iot_app_event_subscribe(
        iot_app, 
        events, 
        status_callback_wrapper, 
        NULL);

    if (err < 0)
    {
        throw_appfw_exception(env, "Event subscription failed");
        goto fail;
    }
    iot_debug("Subcribed to events");
    fail:

    // iot_app_event_subscribe copies the strings, so these pointers are now
    // useless and must be released to prevent memory leaks.
    for (int i = 0; i < length; ++i) 
    {
        // if there are null strings present, this means we jumped into
        // failure case and rest of the list is empty; we can break out
        if (events[i] == NULL) 
        {
            break;
        }
        
        jstring j_string = (jstring)(*env)->GetObjectArrayElement(
                    env, 
                    subscriptions, 
                    i);
        
        (*env)->ReleaseStringUTFChars(
            env, 
            j_string, 
            events[i]);

    }
    iot_free(events);
}


/**
*
* @brief JNI function for enabling signal bridging where SIGTERM and SIGHUP are 
*        converted into events. Currently broken for Java due to how JVM works.
*        Partial workaround: Use shutdown hooks for managing resources
*
* This does not actually work - signals are blocked for a JVM child process
* instead of the main JVM process. This means that when SIGTERM or SIGUP are 
* sent to the main process, JVM shuts down even if we have registered our signal 
* handlers. Expected behaviour is that main process ignores these signals and 
* they are instead picked up during event loop iteration.
*
* @param [in] env   JNI interface pointer
* @param [in] this  Caller object (unused)
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_enableSignalNative
  (JNIEnv *env, jobject this) 
{
    IOT_UNUSED(this);
    iot_debug("called Java_AppFw_enableSignalNative");

    int err = iot_app_bridge_signals(iot_app);

    if (err < 0)
    {
        throw_appfw_exception(env, "Failed to enable signal bridging");        
    }
}

// some constants for following functions
const int INVALID_USER_ID = -2;

/**
* @brief Helper function used by sendEventNative. 
*
* Fills target application id struct used by C api to determine the 
* application(s) the events are sent. Note that this function may use Java
* strings, which means this function MUST be followed by a call to 
* release_app_id_java_strings to release any strings to prevent any memory leaks 
*
* @param [in]  env               JNI interface pointer
* @param [out] app_id            C struct that will be filled with target 
*                                 application info.
* @param [in] targetApplication  Java object containing target application info
*
*/
void build_app_id(
    JNIEnv *env, 
    iot_app_id_t *app_id, 
    jobject targetApplication) 
{

    // get the callback field from our java fw object
    jclass target_class = (*env)->GetObjectClass(env, targetApplication);
    EXIT_IF_NULL(target_class,
        "Event send error: Failed to find TargetApplication class");
    
    jfieldID fid;

    fid = (*env)->GetFieldID(
        env, target_class, "label", "Ljava/lang/String;");
    EXIT_IF_NULL(fid, 
        "Event send error: Failed to get label field from TargetApplication");

    // may be null
    jstring label = (jstring)(*env)->GetObjectField(
        env, targetApplication, fid);

    fid = (*env)->GetFieldID(
            env, target_class, "appID", "Ljava/lang/String;");
    EXIT_IF_NULL(fid, 
        "Event send error: Failed to get appid field from TargetApplication");
    // may be null
    jstring appid = (jstring)(*env)->GetObjectField(
        env, targetApplication, fid);

    fid = (*env)->GetFieldID(
            env, target_class, "binary", "Ljava/lang/String;");
    EXIT_IF_NULL(fid, 
        "Event send error: Failed to get binary field from TargetApplication");
    // may be null
    jstring binary = (jstring)(*env)->GetObjectField(
        env, targetApplication, fid);

    fid = (*env)->GetFieldID(
            env, target_class, "user", "Ljava/lang/String;");
    EXIT_IF_NULL(fid, 
        "Event send error: Failed to get user field from TargetApplication");

    jstring user = (*env)->GetObjectField(env, targetApplication, fid);

    fid = (*env)->GetFieldID(
            env, target_class, "process", "I");
    EXIT_IF_NULL(fid, 
        "Event send error: Failed to get process field from TargetApplication");

    jint process = (*env)->GetIntField(env, targetApplication, fid);
    
    if (label != NULL) 
    {
        app_id->label = (char *)(*env)->GetStringUTFChars(env, label, NULL);
    }

    if (appid != NULL) 
    {
        app_id->appid = (char *)(*env)->GetStringUTFChars(env, appid, NULL);
    }

    if (binary != NULL)
    {
        app_id->binary = (char *)(*env)->GetStringUTFChars(env, binary, NULL);
    }
    
    
    if (user != NULL) {
        const char *user_name = (*env)->GetStringUTFChars(env, user, NULL);
        struct passwd *ret = getpwnam(user_name);  
        (*env)->ReleaseStringUTFChars(env, user, user_name);
        if (ret == NULL) {
            app_id->user = (uid_t)INVALID_USER_ID;
        } else {
            app_id->user = ret->pw_uid;
        }
    }

    // There is a slight change of truncation here. jint is defined to be 32 bit
    // signed integer, but pid_t is only defined to be a signed integer type.
    // On most platforms it is 32 bits or greater, but on some older\stranger 
    // systems these could be smaller.
    app_id->process = (pid_t) process;
}
/**
* @brief Helper function used by sendEventNative
*
* Releases any Java strings that were reserved by build_app_id-function
*
* @param [in]  env               JNI interface pointer
* @param [out] app_id            C struct that contains the pointers to C 
*                                 strings that are being borrowed from Java
*                                 strings.
* @param [in] targetApplication  Java object containing target application info
*
*/
void release_app_id_java_strings(
    JNIEnv *env, 
    iot_app_id_t *app_id, 
    jobject targetApplication)
{

    jclass target_class = (*env)->GetObjectClass(env, targetApplication);
    EXIT_IF_NULL(target_class,
        "Event send error: Failed to find TargetApplication class");

    jfieldID fid;
    jstring j_string;

    if (app_id->label != NULL) 
    {
        fid = (*env)->GetFieldID(
            env, target_class, "label", "Ljava/lang/String;");
        EXIT_IF_NULL(fid, 
            "Event send error: Failed to get label field from TargetApplication"
            );

        j_string = (jstring)(*env)->GetObjectField(
            env, targetApplication, fid);

        (*env)->ReleaseStringUTFChars(env, j_string, app_id->label);
    }

    if (app_id->appid != NULL) 
    {
        fid = (*env)->GetFieldID(
            env, target_class, "appID", "Ljava/lang/String;");
        EXIT_IF_NULL(fid, 
            "Event send error: Failed to get appid field from TargetApplication"
            );
        j_string = (jstring)(*env)->GetObjectField(
            env, targetApplication, fid);

        (*env)->ReleaseStringUTFChars(env, j_string, app_id->appid);   
    }

    if (app_id->binary != NULL)
    {
        fid = (*env)->GetFieldID(
            env, target_class, "binary", "Ljava/lang/String;");
        EXIT_IF_NULL(fid, 
            "Event send error: Failed to get binary field from TargetApplication"
            );

        j_string = (jstring)(*env)->GetObjectField(
            env, targetApplication, fid);

        (*env)->ReleaseStringUTFChars(env, j_string, app_id->binary);
    }
}


/**
* @brief JNI function for sending events to other applications
*
* @param [in] env                 JNI interface pointer
* @param [in] this                Caller object (unused)
* @param [in] event               String identifying the event
* @param [in] json                JSON data that is sent as part of the event
* @param [in] callback_id         Integer that identifies the java callback that
*                                 should be called once event has been sent
* @param [in] target_application  Java TargetApplication object containing info
*                                 on target application(s)
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_sendEventNative
  (JNIEnv *env, 
   jobject this, 
   jstring event, 
   jstring json, 
   jint callback_id, 
   jobject target_application) 
{
    IOT_UNUSED(this);
    // Java wrapper already checks these, but it's possible that someone for
    // some reason ends up calling this function through reflection and passes
    // invalid arguments. 
    THROW_AND_RETURN_IF_NULL(env, event,
        "Event string cannot be null - aborting event sending");

    THROW_AND_RETURN_IF_NULL(env, json,
        "Json data cannot be null - aborting event sending");

    THROW_AND_RETURN_IF_NULL(env, target_application,
        "Target application cannot be null - aborting event sending");
    
    iot_app_id_t app_id = { NULL, NULL, NULL, (uid_t)-1, 0};

    build_app_id(env, &app_id, target_application);

    // check that at least one field is actually set, otherwise throw and return
    if (app_id.label == NULL && app_id.appid == NULL && app_id.binary == NULL 
            && app_id.user == (uid_t)-1 && app_id.process == 0) {
        throw_illegal_argument_exception(env, 
            "No target application was specified");
        goto fail;
    }

    if (app_id.user == (uid_t)INVALID_USER_ID) {
        throw_appfw_exception(env, "Failed to convert user name to user id");
        goto fail;
    }



    char *c_event_str, *c_event_json_str;
    // silence warning when assigning const char * into char *    
    c_event_json_str = (char *)(*env)->GetStringUTFChars(env, json, NULL);
    
    iot_json_t* json_data = iot_json_string_to_object(c_event_json_str,
                                                      strlen(c_event_json_str));

    (*env)->ReleaseStringUTFChars(env, json, c_event_json_str);


    c_event_str = (char *)(*env)->GetStringUTFChars(env, event, NULL);

    int err = iot_app_event_send(iot_app, c_event_str, json_data, &app_id,
                                 send_callback_wrapper,
                                 (void *)(ptrdiff_t)callback_id);

    (*env)->ReleaseStringUTFChars(env, event, c_event_str);

    iot_debug("Sent json data as string: %s",
              iot_json_object_to_string(json_data));
                                 


    if (err < 0)
    {
        throw_appfw_exception(env, "Synchronous failure while sending event");
        goto fail;
    }

    fail: 
    release_app_id_java_strings(env, &app_id, target_application);
}

// helper enum for choosing correct c api function
typedef enum { ALL, RUNNING } list_type_t;

/**
* @brief Helper function used by get(Running|All)ApplicationsNative
*
* Actually performs the C api call to get the running/all application list
*
* @param [in] env          JNI interface pointer
* @param [in] callback_id  Integer that identifies the java callback that
*                          should be called with the application list as 
*                          parameter.
* @param [in] type         The type of list we want (running or all)
*
*/
void app_list_common(JNIEnv *env, jint callback_id, list_type_t type)
{
    int err = 0;
    switch (type)
    {
        case ALL:

            iot_debug("Helper - all");
            err = iot_app_list_all(iot_app, list_callback_wrapper,
                                   (void *)(ptrdiff_t)callback_id);
            break;
        case RUNNING:

            iot_debug("Helper - running");
            err = iot_app_list_running(iot_app, list_callback_wrapper,
                                       (void *)(ptrdiff_t)callback_id);
            break;
        default:
            EXIT_IF_NULL(NULL, 
                "Internal framework error: Invalid list type");
            return;
    }

    if (err < 0)
    {
        throw_appfw_exception(env, 
            "Failed to list applications");
        return;
    }
}
/**
* @brief JNI function for listing running IoT applications.
*
* @param [in] env          JNI interface pointer
* @param [in] this         Caller object (unused)
* @param [in] callback_id  Integer that identifies the java callback that
*                          should be called with the application list as 
*                          parameter.
*/
JNIEXPORT void JNICALL 
Java_com_intel_ostro_appfw_AppFw_getRunningApplicationsNative
  (JNIEnv *env, jobject this, jint callback_id) 
{    
    IOT_UNUSED(this);
    iot_debug("Requesting running applications");
    app_list_common(env, callback_id, RUNNING);
}

/**
* @brief JNI function for listing running IoT applications.
*
* @param [in] env          JNI interface pointer
* @param [in] this         Caller object (unused)
* @param [in] callback_id  Integer that identifies the java callback that
*                          should be called with the application list as 
*                          parameter.
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_getAllApplicationsNative
  (JNIEnv *env, jobject this, jint callback_id) 
{    
    IOT_UNUSED(this);
    iot_debug("Requesting all applications");
    app_list_common(env, callback_id, ALL);
}
/**
* @brief JNI function that enables debugging as specified by the argument list
*
* @param [in] env            JNI interface pointer
* @param [in] this           Caller object (unused)
* @param [in] debug_strings  Array of Java strings that that are passed to
*                            iot_debug_set_config
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_enableDebugNative
  (JNIEnv *env, jobject this, jobjectArray debug_strings)
{
    IOT_UNUSED(this);

    iot_debug("Enabling debugging");
    THROW_AND_RETURN_IF_NULL(env, debug_strings,
        "Debug string array cannot be null - aborting enabling debugging");
    
    const size_t size = (size_t)(*env)->GetArrayLength(env, debug_strings);
    
    if (size == 0) 
    {
        return;
    }

    iot_log_enable(IOT_LOG_MASK_DEBUG);
    iot_debug_enable(true);
    
    for (size_t i = 0; i < size; ++i) 
    {
        jstring str = (*env)->GetObjectArrayElement(env, debug_strings, i);
        THROW_AND_RETURN_IF_NULL(env, str,
            "Debug string cannot be null - aborting enabling debugging");
                 
        const char *c_str = (*env)->GetStringUTFChars(env, str, NULL);

        iot_debug_set_config(c_str);

        (*env)->ReleaseStringUTFChars(env, str, c_str);
    }

    iot_debug("Debugging enabled");
}

/**
* @brief JNI function that starts the GLIB main loop
* 
* @param [in] env   JNI interface pointer (unused)
* @param [in] this  Caller object (unused)
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_startMainLoopNative
  (JNIEnv *env, jobject this)
{
    IOT_UNUSED(env);
    IOT_UNUSED(this);
    iot_debug("Starting main loop");
    g_main_loop_run(main_loop);
}

/**
* @brief JNI function that stops the GLIB main loop
* 
* @param [in] env   JNI interface pointer (unused)
* @param [in] this  Caller object (unused)
*
*/
JNIEXPORT void JNICALL Java_com_intel_ostro_appfw_AppFw_stopMainLoopNative
  (JNIEnv *env, jobject this)
{
    IOT_UNUSED(env);
    IOT_UNUSED(this);
    iot_debug("Stopping main loop");
    g_main_loop_quit(main_loop);
}