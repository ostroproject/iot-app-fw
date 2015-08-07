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
#include <iot/common/log.h>
#include <iot/common/transport.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/client.h"
#include "launcher/daemon/message.h"
#include "launcher/daemon/application.h"
#include "launcher/daemon/event.h"

static void lnc_connect(iot_transport_t *lt, void *user_data);
static void lnc_closed(iot_transport_t *t, int error, void *user_data);
static void lnc_recv(iot_transport_t *t, iot_json_t *req, void *user_data);


void transport_init(launcher_t *l)
{
    static iot_transport_evt_t lnc_evt = {
        { .recvjson     = lnc_recv },
        { .recvjsonfrom = NULL    },
        .connection     = lnc_connect,
        .closed         = lnc_closed,
    };

    static iot_transport_evt_t app_evt = {
        { .recvjson     = lnc_recv },
        { .recvjsonfrom = NULL    },
        .connection     = lnc_connect,
        .closed         = lnc_closed,
    };

    iot_mainloop_t *ml = l->ml;
    iot_sockaddr_t  addr;
    socklen_t       alen;
    const char     *type;
    int             flags, state, sock;

    alen = iot_transport_resolve(NULL, l->lnc_addr, &addr, sizeof(addr), &type);

    if (alen <= 0) {
        iot_log_error("Failed to resolve transport address '%s'.", l->lnc_addr);
        exit(1);
    }

    flags  = IOT_TRANSPORT_REUSEADDR | IOT_TRANSPORT_NONBLOCK | \
        IOT_TRANSPORT_MODE_JSON;

    if (l->lnc_fd < 0) {
        l->lnc = iot_transport_create(ml, type, &lnc_evt, l, flags);

        if (l->lnc == NULL) {
            iot_log_error("Failed to create transport '%s'.", l->lnc_addr);
            exit(1);
        }

        if (!iot_transport_bind(l->lnc, &addr, alen)) {
            iot_log_error("Failed to bind to transport address '%s'.",
                          l->lnc_addr);
            exit(1);
        }

        if (!iot_transport_listen(l->lnc, 0)) {
            iot_log_info("Listen on transport '%s' failed.", l->lnc_addr);
            exit(1);
        }
    }
    else {
        state  = IOT_TRANSPORT_LISTENED;
        sock   = l->lnc_fd;
        l->lnc = iot_transport_create_from(ml, type, &sock, &lnc_evt, l,
                                           flags, state);
    }

    iot_log_info("Transport '%s' created and listening...", l->lnc_addr);


    alen = iot_transport_resolve(NULL, l->app_addr, &addr, sizeof(addr), &type);

    if (alen <= 0) {
        iot_log_error("Failed to resolve transport address '%s'.", l->app_addr);
        exit(1);
    }

    flags = IOT_TRANSPORT_REUSEADDR | IOT_TRANSPORT_NONBLOCK |  \
        IOT_TRANSPORT_MODE_JSON;

    if (l->app_fd < 0) {
        l->app = iot_transport_create(ml, type, &app_evt, l, flags);

        if (l->app == NULL) {
            iot_log_error("Failed to create transport '%s'.", l->app_addr);
            exit(1);
        }

        if (!iot_transport_bind(l->app, &addr, alen)) {
            iot_log_error("Failed to bind to transport address '%s'.",
                          l->app_addr);
            exit(1);
        }

        if (!iot_transport_listen(l->app, 0)) {
            iot_log_info("Listen on transport '%s' failed.", l->app_addr);
            exit(1);
        }
    }

    else {
        state  = IOT_TRANSPORT_LISTENED;
        sock   = l->app_fd;
        l->app = iot_transport_create_from(ml, type, &sock, &app_evt, l,
                                           flags, state);
    }


    iot_log_info("Transport '%s' created and listening...", l->app_addr);
}


int transport_reply(iot_transport_t *t, reply_t *rpl)
{
    iot_json_t *jrpl;
    int         r;

    jrpl = reply_create(rpl);

    r = iot_transport_sendjson(t, jrpl);

    iot_json_unref(jrpl);

    if (r)
        return 0;
    else {
        errno = EIO;
        return -1;
    }
}


static inline int allowed_request(client_t *c, request_t *req)
{
    if (c->type == CLIENT_LAUNCHER)
        return (req->type == REQUEST_SETUP || req->type == REQUEST_CLEANUP);
    else
        return (req->type != REQUEST_SETUP && req->type != REQUEST_CLEANUP);
}


static void lnc_connect(iot_transport_t *lt, void *user_data)
{
    launcher_t *l = (launcher_t *)user_data;
    client_t *c;

    IOT_UNUSED(lt);

    c = client_create(l, lt);

    if (c == NULL) {
        iot_log_error("Failed to accept client connection.");
        return;
    }
    else
        iot_log_info("Accepted launcher connection from process %u.",
                     c->id.process);
}


static void lnc_closed(iot_transport_t *t, int error, void *user_data)
{
    client_t *c = (client_t *)user_data;

    IOT_UNUSED(t);

    if (error != 0)
        iot_log_error("Client connection closed with error %d (%s).",
                      error, strerror(error));
    else
        iot_log_info("Client connection closed.");

    client_destroy(c);
}


static void lnc_recv(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    client_t   *c = (client_t *)user_data;
    const char *json;
    request_t  *req;
    reply_t     rpl;

    json = iot_json_object_to_string(msg);

    iot_log_info("Received client JSON message:");
    iot_log_info("  %s", json);

    req = request_parse(t, msg);

    if (req == NULL) {
        iot_log_error("Failed to parse client request.");
        return;
    }

    if (!allowed_request(c,req)) {
        reply_set_status(&rpl, req->any.seqno,
                         EPERM, "Permission denied", NULL);
        goto send_reply;
    }

    switch (req->type) {
    case REQUEST_SETUP:
        iot_log_info("Received an SETUP request...");
        application_setup(c, &req->setup, &rpl);
        break;

    case REQUEST_CLEANUP:
        iot_log_info("Received an CLEANUP request...");
        application_cleanup(c, &req->cleanup, &rpl);
        break;

    case REQUEST_SUBSCRIBE:
        iot_log_info("Received an event SUBSCRIBE request...");
        client_subscribe(c, &req->subscribe, &rpl);
        break;

    case REQUEST_SEND:
        iot_log_info("Received event SEND request...");
        event_route(c->l, req->send.target.user, NULL, req->send.target.process,
                    req->send.event, req->send.data);
        reply_set_status(&rpl, req->any.seqno, 0, "OK", NULL);
        break;

    case REQUEST_LIST_RUNNING:
        iot_log_info("Received LIST_RUNNING request...");
        application_list(c, &req->list, &rpl);
        break;

    case REQUEST_LIST_ALL:
        iot_log_info("Received LIST_ALL request...");
        application_list(c, &req->list, &rpl);
        break;

    default:
        iot_log_error("Recevied an UNKNOWN request...");
        reply_set_status(&rpl, req->any.seqno, EINVAL, "Unknown request", NULL);
        break;
    }

 send_reply:
    transport_reply(c->t, &rpl);
    request_free(req);
}

