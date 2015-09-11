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

#ifndef __IOT_LAUNCHER_MSG_H__
#define __IOT_LAUNCHER_MSG_H__

#include <stdarg.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/json.h>


/**
 * @brief Get the type of a message.
 *
 * Get the type of the given message.
 *
 * @param [in] msg  message to get type of
 *
 * @return Return the type of the given message, or @NULL upon error.
 */
const char *msg_type(iot_json_t *msg);


/**
 * @brief Create a new request message.
 *
 * Create a new request message with the given type, sequence number, and
 * optional payload.
 *
 * @param [in] type     request type
 * @param [in] seqno    sequence number, used for dispatching replies
 * @param [in] payload  optional payload to add to request, or @NULL
 *
 * @return Returns the newly created request, or @NULL upon failure. You can
 *         use @iot_json_unref to free the request without sending it.
 */
iot_json_t *msg_request_create(const char *type, int seqno, iot_json_t *payload);


/**
 * @brief Parse the given request.
 *
 * Parse the given request message fetching its type, sequence number,
 * and optionally its payload.
 *
 * @param [in]  req      request to parse
 * @param [out] type     pointer used to store request type
 * @param [out] seqno    pointer used to store request sequence number
 * @param [out] payload  pointer used to store request payload, or @NULL
 *
 * @return Returns 0 upon success, -1 upon error in which case errno is
 *         also set.
 */
int msg_request_parse(iot_json_t *req, const char **type, int *seqno,
                      iot_json_t **payload);


/**
 * @brief Create a new reply message.
 *
 * Create a reply message to a request with the given sequence number,
 * with the optional status data.
 *
 * @param [in] seqno   sequence number
 * @param [in] status  status data to include, or @NULL
 *
 * @return Returns the newly created reply message, or @NULL upon error. If
 *         you need to free the createed reply without sending it, call
 *         @iot_json_unref on it.
 */
iot_json_t *msg_reply_create(int seqno, iot_json_t *status);


/**
 * @brief Create a new error reply message.
 *
 * Create an error reply for the request with the given sequence number, with
 * the given error code and printf-like formatted erorr message.
 *
 * @param [in] seqno  request sequence number
 * @param [in] code   error code
 * @param [in] fmt    printf-like error message format strings
 * @param [in] ...    any arguments required by @fmt
 *
 * @return Returns the newly created error reply message, or @NULL upon error.
 *         If you need to free the reply without sending it, you can call
 *         @iot_json_unref on it.
 */
iot_json_t *msg_error_create(int seqno, int code, char *fmt, ...);


/**
 * @brief Create a new status reply.
 *
 * Create a new status reply with the given status code, printf-formatted
 * message and optional payload.
 *
 * @param [in] code     status code
 * @param [in] fmt      format string
 * @param [in] ...      any necessary parameters for the format string
 * @param [in] payload  payload (data) to add or @NULL
 *
 * @return Returns the created reply status or @NULL upon failure. If you
 *         need to free the reply status without adding it to a message,
 *         call @iot_json_unref on it.
 */
iot_json_t *msg_status_create(int code, iot_json_t *payload,
                              const char *fmt, ...);

/**
 * @brief Convenience macro to create an error reply.
 *
 * Wrapper macro for @msg_status_create, to create an error status reply
 * with an error code and a printf-formatted error message.
 *
 * @param [in] code  error code to put in status
 * @param [in] ...   error format string and arguments for it
 *
 * @return Returns the created status reply.
 */
#define msg_status_error(code, ...) msg_status_create(code, NULL, __VA_ARGS__)

/**
 * @brief Convenience macro to create a successful status reply with data.
 *
 * Wrapper macro for @msg_status_create, to create an successful status
 * reply with optional reply data content.
 *
 * @param [in] data  data to include in the reply, or @NULL
 *
 * @return Returns the created status reply.
 */
#define msg_status_ok(data) msg_status_create(0, data, "OK")

/**
 * @brief Create a new status reply payload.
 *
 */
iot_json_t *msg_payload_create(int type, ...);

/**
 * @brief Parse a reply message.
 *
 * Parse the given reply message, extracting its type, sequence number, and
 * payload.
 */
int msg_reply_parse(iot_json_t *rpl, int *seqno, const char **message,
                    iot_json_t **payload);

/**
 * @brief Create a new event message.
 *
 * Create a new event message with the given event name and optional
 * event data.
 *
 * @param [in] name  event name
 * @param [in] data  event data, or @NULL
 *
 * @return Returns the created event message.
 */
iot_json_t *msg_event_create(const char *name, iot_json_t *data);

/**
 * @brief Parse an event message.
 *
 * Parse the given event message, fetching its event name, and optional
 * payload.
 *
 * @param [in]  msg      event message to parse
 * @param [out] name     pointer to set to the event name
 * @param [out] payload  pointer to set to payload, or @NULL
 *
 * @return Returns 0 upon success, -1 otherwise.
 */
int msg_event_parse(iot_json_t *msg, const char **name, iot_json_t **payload);


#endif /* __IOT_LAUNCHER_MSG_H__ */
