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

#include <Python.h>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <cmath>

#include <iot/app.h>
#include <iot/common/glib-glue.h>
#include <iot/common/debug.h>

struct python_iot_app {
    PyObject* app = NULL;
    PyObject* event_handler = NULL;
    PyObject* status_handler = NULL;
    PyObject* send_handler = NULL;
    PyObject* list_handler = NULL;
    PyObject* AppfwError = NULL;
    std::unordered_set<int> send_ids;
};

/**
 * @brief The IoT application context attached to the library.
 */
static iot_app_t* iot_app = NULL;
/**
 * @brief The Python application context attached to the library.
 */
static python_iot_app python_app;

/**
 * @brief Forwards events to the Python module
 * @details This callback function is always used to convert and forward IoT
 * application framework event notifications to Python compatible events.
 * The function conforms to the type of @iot_app_event_cb_t as described in 
 * app.h.
 * 
 * @param [in] app      IoT application context
 * @param [in] event    name of the event received
 * @param [in] data     JSON data attached to the event
 */
void event_handler_wrapper(iot_app_t* app, const char* event,
                           iot_json_t* data)
{
    (void) (app);

    iot_debug("event_handler_wrapper");
    
    if (python_app.event_handler)
    {
        const char* json_string = iot_json_object_to_string(data);

        iot_debug("Received json data as string: %s", json_string ? json_string : "<empty>");

        PyObject* function = PyMethod_Function(python_app.event_handler);
        PyObject* args = Py_BuildValue("(Oss)", python_app.app, event, json_string);

        PyObject* ret = PyObject_CallObject(function, args);
        Py_DECREF(args);

        if (!ret)
        {
            PyErr_SetString(python_app.AppfwError, "Error while calling IotApp event handler");
        }
        Py_DECREF(ret);
    }
}

/**
 * @brief Forwards status messages to the Python module.
 * @details This callback function is always used to convert and forward the
 * IoT application framework status notifications to the Python module. This
 * function conforms to the type of @iot_app_status_cb_t as described in app.h.
 * 
 * @param [in] app          IoT application context.
 * @param [in] seqno        sequence number of associated request
 * @param [in] status       request status (0 ok, non-zero error)
 * @param [in] msg          error message
 * @param [in] data         optional request-specific status data
 * @param [in] user_data    opaque user data supplied for target (unused)
 */
void status_callback_wrapper(iot_app_t* app, int seqno, int status,
                             const char* msg, iot_json_t* data, 
                             void* user_data)
{
    (void) (app);
    (void) (user_data);

    iot_debug("status_callback_wrapper");

    if (python_app.status_handler)
    {
        const char* json_string = iot_json_object_to_string(data);

        PyObject* function = PyMethod_Function(python_app.status_handler);
        PyObject* args = Py_BuildValue("(Oiiss)", python_app.app, seqno, status,
                                 msg, json_string);

        PyObject* ret = PyObject_CallObject(function, args);
        Py_DECREF(args);

        if (!ret)
        {
            PyErr_SetString(python_app.AppfwError, "Error while calling IotApp status callback");
        }
        Py_DECREF(ret);
    }
}

/**
 * @brief Forwards event delivery notifications to the Python module.
 * @details This callback function is always used to convert and forward the
 * IoT application framework event delivery notifications to the Python 
 * module. Internally only an ID number is attached to each send call as the
 * @user_data. The Python module is responsible of storing the actual 
 * @user_data if deemed necessary. This function conforms to the type of 
 * @iot_app_send_cb_t as described in app.h
 * 
 * @param [in] app          IoT application context
 * @param [in] id           the ID of message delivered to the appfw server
 * @param [in] status       ???
 * @param [in] msg          ???
 * @param [in] user_data    internally used as a key for user_data
 */
void send_callback_wrapper(iot_app_t* app, int id, int status,
                           const char* msg, void* user_data)
{
    (void) (app);

    iot_debug("send_callback_wrapper");

    if (python_app.send_handler)
    {
        int send_id = *(static_cast<int*>(user_data));

        PyObject* function = PyMethod_Function(python_app.send_handler);
        PyObject* args = Py_BuildValue("(Oiiis)", python_app.app, send_id, id, status, msg);

        python_app.send_ids.erase(send_id);
        PyObject* ret = PyObject_CallObject(function, args);
        Py_DECREF(args);

        if (!ret)
        {
            PyErr_SetString(python_app.AppfwError, "Error while calling IotApp send callback");
        }
        Py_DECREF(ret);
    }
}

/**
 * @brief Initializes an IoT application context for the Python application.
 * @details Initialize a new IoT application context with a glib mainloop and
 * register a Python instance and callback methods for the module. This 
 * function has to be called before any other functions are invoked.
 * 
 * @param [in] self     (unused)
 * @param [in] args     Expects four arguments:
 *                      -# Instance of the class which owns the callback methods
 *                      -# Event callback method
 *                      -# Status notification callback method
 *                      -# Event delivery notification callback method
 * 
 * @return              Py_None on success, @NULL on error.
 */
static PyObject* iot_py_app_init(PyObject* self, PyObject* args)
{
    (void) (self);

    iot_debug("called iot_py_app_init");

    if (!PyArg_ParseTuple(args, "OOOO:_init_appfw", 
                         &python_app.app, &python_app.event_handler,
                         &python_app.status_handler, &python_app.send_handler))
    {
        return NULL;
    }
    else //verify inputs
    {
        PyTypeObject* py_app_type = python_app.app->ob_type;

        if (!PyMethod_Check(python_app.event_handler) || 
            !PyObject_TypeCheck(PyMethod_Self(python_app.event_handler), 
                                              py_app_type))
        {
            PyErr_SetString(PyExc_TypeError, "Event_handler is not a method of given Python app.");
            return NULL;
        }
        Py_INCREF(python_app.event_handler);

        if (!PyMethod_Check(python_app.status_handler) ||
            !PyObject_TypeCheck(PyMethod_Self(python_app.status_handler), 
                                              py_app_type))
        {
            PyErr_SetString(PyExc_TypeError, "Status_handler is not a method of given Python app.");
            return NULL;
        }
        Py_INCREF(python_app.status_handler);

        if (!PyMethod_Check(python_app.send_handler) ||
            !PyObject_TypeCheck(PyMethod_Self(python_app.send_handler), 
                                              py_app_type))
        {
            PyErr_SetString(PyExc_TypeError, "Send_handler is not a method of given Python app.");
            return NULL;
        };
        Py_INCREF(python_app.send_handler);

        iot_debug("Python app initialized.");
    }

    //Setup iot_app owned by this library
    GMainLoop* g_main_loop = g_main_loop_new(NULL, false);
    if (g_main_loop == NULL)
    {
        PyErr_SetString(python_app.AppfwError, "GLib mainloop creation failed.");
        return NULL;
    }

    iot_mainloop_t* iot_ml = iot_mainloop_glib_get(g_main_loop);
    if (iot_ml == NULL)
    {
        PyErr_SetString(python_app.AppfwError, "Failed to attach GLib mainloop to Iot application");
        return NULL;
    }

    iot_app = iot_app_create(iot_ml, NULL);
    if (iot_app == NULL)
    {
        PyErr_SetString(python_app.AppfwError, "Iot application creation failed.");
        return NULL;
    }

    iot_app_event_set_handler(iot_app, event_handler_wrapper);

    iot_debug("Library initialized.");

    Py_INCREF(Py_None);
    return Py_None;
}

/**
 * @brief Destroys the application context used by the library
 * @details Destroy the application context attached to this library and free
 * the resources associated with it.
 * 
 * @param self  (unused)
 * @param args  (unused)
 * 
 * @return      Py_None
 */
static PyObject* iot_py_app_clear(PyObject* self, PyObject* args)
{
    iot_debug("called iot_py_app_clear");
    (void) (self);
    (void) (args);

    iot_app_destroy(iot_app);

    Py_DECREF(python_app.event_handler);
    Py_DECREF(python_app.status_handler);
    Py_DECREF(python_app.send_handler);

    Py_INCREF(Py_None);
    return Py_None;
}

/**
 * @brief Update the event subscriptions attached to the application context.
 * @details Sets the events the Python application is interested in receiving.
 * The status callback method is called after the subscription to report 
 * possible errors.
 * 
 * @param self  (unused)
 * @param args  Expects a list of strings
 * 
 * @return      Py_None on success, @NULL on error.
 */
static PyObject* iot_py_app_subscribe_events(PyObject* self, PyObject* args)
{
    iot_debug("called iot_py_app_subscribe_events");

    (void) (self);

    PyObject* event_string_list = NULL;
    if (!PyArg_ParseTuple(args, "O!:_subscribe_event", &PyList_Type, 
                          &event_string_list))
    {
        return NULL;
    }

    //parse strings
    int event_count = PyList_Size(event_string_list);
    std::vector<const char*> events;
    for (int i = 0; i < event_count; i++)
    {
        PyObject* event_string_object = PyList_GetItem(event_string_list, i);
        const char* event_string = PyString_AsString(event_string_object);
        if (!event_string) //NULL if fails
        {
            return NULL;
        }
        events.push_back(event_string);
    }
    //App-fw expects a NULL-terminated array
    events.push_back((const char*)NULL);

    //const_cast required for C-compatibility.
    int ret = iot_app_event_subscribe(iot_app, const_cast<char**>(&events.front()), 
                                                status_callback_wrapper, NULL);
    if (ret == -1)
    {
        PyErr_SetString(python_app.AppfwError, "Event subscription failed.");
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

/**
 * @brief Request the delivery of certain signals as events.
 * @details Request the delivery of SIGHUP and SIGTERM signals to the Python
 * application as IoT events. The signals are delivered to the event callback
 * method.
 * 
 * @param self  (unused)
 * @param args  (unused)
 * 
 * @return      Py_None on success, @NULL on error.
 */
static PyObject* iot_py_app_bridge_signals(PyObject* self, PyObject* args)
{
    iot_debug("called iot_py_app_bridge_signals");
    (void) (self);
    (void) (args);

    if (!iot_app)
    {
        PyErr_SetString(python_app.AppfwError, 
                        "Signal bridging request on uninitialized app");
        return NULL;
    }

    int err = iot_app_bridge_signals(iot_app);

    if (err)
    {
        PyErr_SetString(python_app.AppfwError, "Signal bridging request failed");
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

/**
 * @brief Send an IoT event to other IoT applications.
 * @details Send an IoT @event for all the applications matching @target with 
 * @data attached to it if the security layer allows it. The send callback
 * method is called once emitting the events has finished.
 * 
 * @param self      (unused)
 * @param args      Expects three arguments:
 *                  -# event - the name of the event as a string
 *                  -# json_string - the JSON data attached to the event
 *                  -# send_id - the ID number delivered to the callback.
 * @param keywords  Accepts five keyword arguments. Specify the @target application(s):
 *                  -# label - SMACK label, default: @NULL
 *                  -# appid - application id, default: @NULL
 *                  -# binary - executed binary, default: @NULL
 *                  -# user - effective user id, default: -1
 *                  -# process - process id, default: 0
 *                  Note that at least one of above must be specified.
 * @return          Py_None on success, @NULL on error.
 */
static PyObject* iot_py_app_send_event(PyObject* self, PyObject* args,
                                       PyObject* keywords)
{
    iot_debug("called iot_py_app_send_event");
    (void) (self);

    iot_app_id_t app_id = {NULL, NULL, NULL, (uid_t)-1, 0};

    char* id_keywords[] = { (char*)"event", (char*)"json_string", 
                            (char*)"send_id", (char*)"label", 
                            (char*)"appid", (char*)"binary", 
                            (char*)"user", (char*)"process", NULL };

    const char* event = NULL;
    const char* string_data = NULL;
    int send_id = -1;

    if (!PyArg_ParseTupleAndKeywords(args, keywords, "s|sisssii:_send_event", 
                                    id_keywords, &event, &string_data, 
                                    &send_id, &app_id.label, 
                                    &app_id.appid, &app_id.binary, 
                                    &app_id.user, &app_id.process))
    {
        return NULL;
    }
    else
    {
        iot_json_t* json_data = iot_json_string_to_object(string_data, 
                                                          strlen(string_data));

        iot_debug("Sent json data as string: %s", 
                  iot_json_object_to_string(json_data));

        std::pair<std::unordered_set<int>::iterator, bool> insert_ret;
        insert_ret = python_app.send_ids.insert(send_id);
        if (insert_ret.second == false)
        {
            PyErr_SetString(python_app.AppfwError, "Creation of wrapper library send_id failed");
            return NULL;
        }

        //const_cast required for C-compatibility
        int err = iot_app_event_send(iot_app, event, json_data, &app_id, 
                                     send_callback_wrapper, 
                                     const_cast<int*>(&(*(insert_ret.first))));
        if (!err)
        {
            PyErr_SetString(python_app.AppfwError, "Synchronous failure while sending event");
            return NULL;
        }
        
        Py_INCREF(Py_None);
        return Py_None;
    }

    return NULL;
}

static PyObject* iot_py_app_list_running(PyObject* self, PyObject* args)
{
    (void) (self);
    (void) (args);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* iot_py_app_list_all(PyObject* self, PyObject* args)
{
    (void) (self);
    (void) (args);
    Py_INCREF(Py_None);
    return Py_None;
}

/**
 * @brief Enable debugging messages of the IoT application framework.
 * @details Enable the IoT debugging and attach a list of debug sites to it.
 * 
 * @param self  (unused)
 * @param args  Expects a list of debug site strings.
 * 
 * @return      Py_None on success, @NULL on error.
 */
static PyObject* iot_py_enable_debug(PyObject* self, PyObject* args)
{
    (void) (self);

    PyObject* debug_sites = NULL;

    if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &debug_sites))
    {
        return NULL;
    }
    else
    {
        int site_count = PyList_Size(debug_sites);

        if (site_count > 0)
        {
            iot_log_enable(IOT_LOG_MASK_DEBUG);
            iot_debug_enable(true);
        }

        for (int site_index = 0; site_index < site_count; site_index++)
        {
            PyObject* debug_site_object = PyList_GetItem(debug_sites,
                                                           site_index);
            const char* debug_site_string = PyString_AsString(debug_site_object);
            if (!debug_site_string) //NULL if fails
            {
                return NULL;
            }
            iot_debug_set_config(debug_site_string);
        }


        Py_INCREF(Py_None);
        return Py_None;
    }

    return NULL;
}

#ifdef __cplusplus
extern "C" {

static PyMethodDef appFwMethods[] = {
    {
        "init",
        iot_py_app_init,
        METH_VARARGS,
        "Initialize and start the iot_app."
    },
    {
        "clear",
        iot_py_app_clear,
        METH_VARARGS,
        "Destroy the iot_app."
    },
    {
        "subscribe_events",
        iot_py_app_subscribe_events,
        METH_VARARGS,
        "Set the events the iot_app receives messages from."
    },
    {
        "enable_signals",
        iot_py_app_bridge_signals,
        METH_VARARGS,
        "Receive events from SIGTERM and SIGHUP."
    },
    {
        "send_event",
        (PyCFunction)iot_py_app_send_event,
        METH_VARARGS | METH_KEYWORDS,
        "Send event."
    },
    {
        "list_running",
        iot_py_app_list_running,
        METH_VARARGS,
        "List running IoT applications."
    },
    {
        "list_all",
        iot_py_app_list_all,
        METH_VARARGS,
        "List all installed IoT applications."
    },
    {
        "enable_debug",
        iot_py_enable_debug,
        METH_VARARGS,
        "Enable Iot debug information recording."
    },
    {
        NULL, 
        NULL, 
        0, 
        NULL
    }   // Sentinel
};

PyMODINIT_FUNC
init_appfwwrapper(void)
{
    PyObject* module = Py_InitModule("_appfwwrapper", appFwMethods);
    if (module == NULL)
    {
        return;
    }

    python_app.AppfwError = PyErr_NewException((char*)"appfwwrapper.error", NULL, NULL);
    Py_INCREF(python_app.AppfwError);
    PyModule_AddObject(module, (char*)"error", python_app.AppfwError);
}

} //extern "C"
#endif