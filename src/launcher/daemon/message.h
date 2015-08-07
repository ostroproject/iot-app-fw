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

#ifndef __IOT_LAUNCHER_MESSAGE_H__
#define __IOT_LAUNCHER_MESSAGE_H__

#include <iot/common/json.h>

/*
 * request types
 */

typedef enum {
    REQUEST_UNKNOWN = 0,
    REQUEST_SETUP,                       /* application setup */
    REQUEST_CLEANUP,                     /* application cleanup */
    REQUEST_SUBSCRIBE,                   /* subscribe to events */
    REQUEST_SEND,                        /* send an event */
    REQUEST_LIST_RUNNING,                /* list running applications */
    REQUEST_LIST_ALL                     /* list all installed applications */
} req_type_t;


/*
 * fields common to all requests
 */

#define COMMON_REQUEST_FIELDS                                           \
    int         type;                    /* request type */             \
    int         seqno;                   /* request sequence number */  \
    char       *label;                   /* requestor SMACK label */    \
    uid_t       uid;                     /*       user ID */            \
    gid_t       gid;                     /*       primary group ID */   \
    pid_t       pid;                     /*       process ID */         \
    iot_json_t *msg                      /* request 'on the wire' */

/*
 * a generic request of any type
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
} any_req_t;


/*
 * request to set up an application
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
    char   *appid;                       /* application to set up */
    char  **args;                        /* arguments for application */
} setup_req_t;


/*
 * request to clean up an application
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
    char *cgpath;                        /* application cgroup path */
} cleanup_req_t;


/*
 * request to subscribe for events
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
    char **events;                       /* event names to subscribe for */
} event_sub_req_t;


/*
 * request to send an event
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
    char       *event;                   /* event to send */
    iot_json_t *data;                    /* data to attach to event */
    struct {
        char    *label;                  /* target SMACK label, or NULL */
        char    *appid;                  /* target application ID, or NULL */
        uid_t    user;                   /* target user ID, or -1 */
        pid_t    process;                /* target process ID, or 0 */
    } target;
} event_send_req_t;


/*
 * requet to list applications
 */

typedef struct {
    COMMON_REQUEST_FIELDS;
} list_req_t;

/*
 * a request
 */

typedef union {
    int              type;               /* request type */
    any_req_t        any;                /* generic request */
    setup_req_t      setup;              /* setup request */
    cleanup_req_t    cleanup;            /* cleanup request */
    event_sub_req_t  subscribe;          /* event subscription request */
    event_send_req_t send;               /* event send request */
    list_req_t       list;               /* application list request */
} request_t;


/*
 * reply types
 */

typedef enum {
    REPLY_UNKNOWN = 0,
    REPLY_STATUS,                        /* status reply (OK/error) */
} reply_type_t;


/*
 * fields common to all replies
 */

#define COMMON_REPLY_FIELDS                                            \
    int type;                            /* reply type */              \
    int seqno                            /* request sequence number */ \

/*
 * a genric reply of any type
 */

typedef struct {
    COMMON_REPLY_FIELDS;
} reply_any_t;


/*
 * a status reply
 */

typedef struct {
    COMMON_REPLY_FIELDS;
    int         status;                  /* reply status (0 = OK) */
    char       *msg;                     /* reply message (for errors) */
    iot_json_t *data;                    /* optional reply data */
} reply_status_t;


/*
 * a reply
 */

typedef union {
    int            type;                 /* reply type */
    reply_any_t    any;                  /* generic reply */
    reply_status_t status;               /* status reply */
} reply_t;


request_t *request_parse(iot_transport_t *t, iot_json_t *msg);
void request_free(request_t *req);

reply_t *reply_set_status(reply_t *rpl, int seqno, int status, const char *msg,
                          iot_json_t *data);

iot_json_t *reply_create(reply_t *rpl);

#endif /* __IOT_LAUNCHER_MESSAGE_H__ */
