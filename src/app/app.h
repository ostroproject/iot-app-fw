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

#ifndef __IOT_APP_APP_H__
#define __IOT_APP_APP_H__

#include <iot/common/macros.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>

IOT_CDECL_BEGIN

/**
 * @brief Opaque IoT application context.
 */
typedef struct iot_app_s iot_app_t;


/**
 * @brief Create an IoT application context.
 *
 * Create a new IoT application context, initialize it to use the given
 * mainloop abstraction and associate it with an optional opaque pointer
 * to application-specific data.
 *
 * @param [in] ml        mainloop abstraction to use
 * @param [in] app_data  opaque application data to associate with the context
 *
 * @return Returns the newly created application context, or @NULL upon error-
 */
iot_app_t *iot_app_create(iot_mainloop_t *ml, void *data);

/**
 * @brief Destroy the given application context.
 *
 * Destroy the given application context, freeing all resources associated
 * with it.
 *
 * @param [in] app  the application context to destroy
 */
void iot_app_destroy(iot_app_t *app);

/**
 * @brief Get the mainloop associated with an application context.
 *
 * Fetch the mainloop associated with the given application context.
 * This is provided merely for convenience so you don't need extra
 * bookkeeping for the mainloop.
 *
 * @param [in] app  application context to get mainloop for
 *
 * @return Returns the mainloop associated with @app.
 */
iot_mainloop_t *iot_app_get_mainloop(iot_app_t *app);

/**
 * @brief Retrieve application data associated with a context.
 *
 * Retrieve the opaque pointer to application-specific data associated
 * with the given context.
 *
 * @param [in] app  application context
 *
 * @return Returns the application data associated with this context.
 */
void *iot_app_get_data(iot_app_t *app);

/**
 * @brief Type for an event notification callback function.
 */
typedef void (*iot_app_event_cb_t)(iot_app_t *app, const char *event,
                                   iot_json_t *data);

/**
 * @brief Set application event handler notification callback.
 *
 * Set the callback function to be invoked for notifying the application
 * about events received by the application.
 *
 * @param [in] app      application context
 * @param [in] handler  callback function to pass received events to
 */
iot_app_event_cb_t iot_app_event_set_handler(iot_app_t *app,
                                             iot_app_event_cb_t handler);

/**
 * @brief Type for a request status notification.
 *
 * @param [in] app        application context
 * @param [in] seqno      sequence number of associated request
 * @param [in] status     request status (0 ok, non-zero error)
 * @param [in] msg        error message
 * @param [in] data       optional request-specific status data
 * @param [in] user_data  opaque user data supplied for the request
 */
typedef void (*iot_app_status_cb_t)(iot_app_t *app, int seqno, int status,
                                    const char *msg, iot_json_t *data,
                                    void *user_data);

/**
 * @brief Set the events this application is subscribed to.
 *
 * Set the set of events this application is interested in receiving. Note
 * that you must set up an event handler using @iot_app_event_set_handler
 * before trying to subscribe to any events.
 *
 * @param [in] app     IoT application context
 * @param [in] events  events, @NULL-terminate array of event names
 *
 * @return Returns > 0 request number of the subscription request sent to
 *         the server, or -1 upon failure.
 */
int iot_app_event_subscribe(iot_app_t *app, char **events,
                            iot_app_status_cb_t cb, void *user_data);

/**
 * @brief Request the delivery of certain signals as as IoT events.
 *
 * Request the delivery of SIGHUP and SIGTERM signals as IoT events. Note
 * that you must set up an evnet handler using @iot_app_event_set_handler
 * before trying to bridge these signals as events.
 *
 * @param [in] app  IoT application context
 *
 * @return Returns 0 upon success, -1 upon failure.
 */
int iot_app_bridge_signals(iot_app_t *app);

/**
 * @brief IoT event delivery notification callback type.
 */
typedef void (*iot_app_send_cb_t)(iot_app_t *app, int id, int status,
                                  const char *msg, void *user_data);

/**
 * @brief Type to specify source or destination IoT applications.
 */
typedef struct {
    const char *label;                   /**< SMACK label or @NULL */
    const char *appid;                   /**< application id or @NULL */
    const char *binary;                  /**< executed binary, or @NULL */
    uid_t       user;                    /**< effective user id, or -1 */
    pid_t       process;                 /**< process id, or 0 */
} iot_app_id_t;

/**
 * @brief Send an IoT event to one or more IoT applications.
 *
 * Send the specified @event with @data attached to all the applications
 * matching @target for which sending an event is allowed by security layer.
 * If @notify is specified it will be called once emitting the event(s, not
 * processing by the receivers) has finished.
 *
 * @param [in] app        IoT application context
 * @param [in] event      name of the event to send
 * @param [in] data       JSON data to attach to the event
 * @param [in] notify     callback to call once sending is finished, or @NULL
 * @param [in] user_data  opaque data to pass to @notify
 *
 * @return Returns a non-zero event id on success, 0 upon synchronous failure.
 *         Note that asynchronous failure can be reported/detected only via
 *         the @notify callback.
 */
int iot_app_event_send(iot_app_t *app, const char *event, iot_json_t *data,
                       iot_app_id_t *target, iot_app_send_cb_t notify,
                       void *user_data);

/**
 * @brief Application information returned by listing applications.
 */
typedef struct {
    const char  *appid;
    const char  *description;
    const char  *desktop;
    uid_t        user;
    const char **argv;
    int          argc;
} iot_app_info_t;


/**
 * @brief IoT application listing notification callback type.
 */
typedef void (*iot_app_list_cb_t)(iot_app_t *app, int id, int status,
                                  const char *msg, int napp,
                                  iot_app_info_t *apps, void *user_data);

/**
 * @brief List running IoT applications.
 *
 * List the currently running IoT applications.
 *
 * @param [in] app        IoT application context
 * @param [in] notify     callback to call with the list of applications
 * @param [in] user_data  opaque data to pass to @notify
 *
 * @return Returns a non-zero request id on success, 0 upon synchronous
 *         failure. Note that asynchronous errors are reported via the
 *         @notify callback.
 */
int iot_app_list_running(iot_app_t *app, iot_app_list_cb_t notify,
                         void *user_data);

/**
 * @brief List all installed IoT applications.
 *
 * List all installed IoT applications.
 *
 * @param [in] app        IoT application context
 * @param [in] notify     callback to call with the list of applications
 * @param [in] user_data  opaque data to pass to @notify
 *
 * @return Returns a non-zero request id on success, 0 upon synchronous
 *         failure. Note that asynchronous errors are reported via the
 *         @notify callback.
 */
int iot_app_list_all(iot_app_t *app, iot_app_list_cb_t notify,
                     void *user_data);


IOT_CDECL_END

#endif /* __IOT_APP_H__ */
