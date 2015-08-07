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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#define __USE_GNU
#include <sys/socket.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/json.h>
#include <iot/common/transport.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/message.h"


static int parse_setup(setup_req_t *req)
{
    iot_json_t *msg = req->msg;
    iot_json_t *cmd;
    int         n, i;

    req->type = REQUEST_SETUP;

    if (!iot_json_get_string(msg, "appid", &req->appid)) {
        iot_log_error("Malformed setup request, missing appid.");
        errno = EINVAL;
        return -1;
    }

    if (!iot_json_get_array(msg, "command", &cmd)) {
        iot_log_error("Malformed setup request, missing command.");
        errno = EINVAL;
        return -1;
    }

    n = iot_json_array_length(cmd);

    if ( n <= 0) {
        iot_log_error("Malformed setup request, empty command.");
        errno = EINVAL;
        return -1;
    }

    req->args = iot_allocz_array(char *, n + 1);

    if (req->args == NULL)
        return -1;

    for (i = 0; i < n; i++) {
        if (!iot_json_array_get_string(cmd, i, req->args + i)) {
            iot_log_error("Malformed command in setup request.");
            return -1;
        }
    }

    return 0;
}


static int parse_cleanup(cleanup_req_t *req)
{
    iot_json_t *msg = req->msg;

    req->type = REQUEST_CLEANUP;

    if (!iot_json_get_string(msg, "path", &req->cgpath)) {
        iot_log_error("Malformed cleanup request, missing cgroup path.");
        errno = EINVAL;
        return -1;
    }

    return 0;
}


static int parse_subscribe(event_sub_req_t *req)
{
    iot_json_t *msg = req->msg;
    iot_json_t *events;
    int         n, i;

    req->type = REQUEST_SUBSCRIBE;

    if (!iot_json_get_array(msg, "events", &events)) {
        iot_log_error("Malformed subscribe request, missing events.");
        errno = EINVAL;
        return -1;
    }

    n = iot_json_array_length(events);

    if (n <= 0) {
        iot_log_error("Malformed subscribe request, empty events.");
        errno = EINVAL;
        return -1;
    }

    req->events = iot_allocz_array(char *, n + 1);

    if (req->events == NULL)
        return -1;

    for (i = 0; i < n; i++) {
        if (!iot_json_array_get_string(events, i, req->events + i)) {
            iot_log_error("Malformed events in subscribe request.");
            return -1;
        }
    }

    return 0;
}


static int parse_send(event_send_req_t *req)
{
    iot_json_t *msg = req->msg;

    req->type = REQUEST_SEND;

    if (!iot_json_get_string(msg, "event", &req->event)) {
        iot_log_error("Malformed send request, missing event.");
        errno = EINVAL;
        return -1;
    }

    iot_json_get_string (msg, "label"  , &req->target.label);
    iot_json_get_string (msg, "appid"  , &req->target.appid);
    iot_json_get_integer(msg, "user"   , &req->target.user);
    iot_json_get_integer(msg, "process", &req->target.process);

    req->data = iot_json_get(msg, "data");

    return 0;
}


static int parse_list_running(list_req_t *req)
{
    req->type = REQUEST_LIST_RUNNING;

    return 0;
}


static int parse_list_all(list_req_t *req)
{
    req->type = REQUEST_LIST_ALL;

    return 0;
}


static void free_setup(setup_req_t *req)
{
    iot_free(req->args);
    req->args = NULL;
}


static void free_cleanup(cleanup_req_t *req)
{
    IOT_UNUSED(req);
}


static void free_subscribe(event_sub_req_t *req)
{
    iot_free(req->events);
    req->events = NULL;
}


static void free_send(event_send_req_t *req)
{
    IOT_UNUSED(req);
}


static void free_list(list_req_t *req)
{
    IOT_UNUSED(req);
}


void request_free(request_t *req)
{
    if (req == NULL)
        return;

    switch (req->type) {
    case REQUEST_SETUP:
        free_setup(&req->setup);
        break;
    case REQUEST_CLEANUP:
        free_cleanup(&req->cleanup);
        break;
    case REQUEST_SUBSCRIBE:
        free_subscribe(&req->subscribe);
        break;
    case REQUEST_SEND:
        free_send(&req->send);
        break;
    case REQUEST_LIST_RUNNING:
    case REQUEST_LIST_ALL:
        free_list(&req->list);
        break;
    default:
        break;
    }

    iot_json_unref(req->any.msg);
    req->any.msg = NULL;
    iot_free(req);
}


request_t *request_parse(iot_transport_t *t, iot_json_t *msg)
{
    request_t    *req;
    const char   *type;
    int           seq;
    struct ucred  uc;
    socklen_t     len;

    if (!iot_json_get_string (msg, "type", &type) ||
        !iot_json_get_integer(msg, "seqno", &seq)) {
        iot_log_error("Malformed request, failed to parse.");
        return NULL;
    }

    len = sizeof(uc);
    if (!iot_transport_getopt(t, "peer-cred", &uc, &len)) {
        iot_log_error("Failed to get request peer credentials.");
        return NULL;
    }

    req = iot_allocz(sizeof(*req));

    if (req == NULL)
        return NULL;

    req->any.seqno = seq;

    req->any.uid = uc.uid;
    req->any.gid = uc.gid;
    req->any.pid = uc.pid;

    req->any.msg = iot_json_ref(msg);

    if (!strcmp(type, "setup")) {
        if (parse_setup(&req->setup) < 0)
            goto fail;
    }
    else if (!strcmp(type, "cleanup")) {
        if (parse_cleanup(&req->cleanup) < 0)
            goto fail;
    }
    else if (!strcmp(type, "subscribe-events")) {
        if (parse_subscribe(&req->subscribe) < 0)
            goto fail;
    }
    else if (!strcmp(type, "send-event")) {
        if (parse_send(&req->send) < 0)
            goto fail;
    }
    else if (!strcmp(type, "list-running")) {
        if (parse_list_running(&req->list) < 0)
            goto fail;
    }
    else if (!strcmp(type, "list-all")) {
        if (parse_list_all(&req->list) < 0)
            goto fail;
    }
    else
        goto fail;

    return req;

 fail:
    request_free(req);

    return NULL;
}


reply_t *reply_set_status(reply_t *rpl, int seqno, int status, const char *msg,
                          iot_json_t *data)
{
    rpl->any.type  = REPLY_STATUS;
    rpl->any.seqno = seqno;

    rpl->status.status = status;
    rpl->status.msg    = (char *)msg;
    rpl->status.data   = data;

    return rpl;
}


iot_json_t *reply_create(reply_t *rpl)
{
    iot_json_t *jrpl;
    const char *msg;

    switch (rpl->type) {
    case REPLY_STATUS:
        jrpl = iot_json_create(IOT_JSON_OBJECT);
        iot_json_add_string (jrpl, "type"   , "status");
        iot_json_add_integer(jrpl, "seqno"  , rpl->status.seqno);
        iot_json_add_integer(jrpl, "status" , rpl->status.status);

        if (rpl->status.status != 0) {
            msg = rpl->status.msg ? rpl->status.msg : "unknown error";
            iot_json_add_string(jrpl, "message" , msg);
        }
        else {
            if (rpl->status.data != NULL)
                iot_json_add_object(jrpl, "data", rpl->status.data);
        }

        return jrpl;
    default:
        return NULL;
    }
}
