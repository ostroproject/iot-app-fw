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
#include "launcher/daemon/application.h"
#include "launcher/daemon/event.h"
#include "launcher/daemon/transport.h"


typedef iot_json_t *(*handler_t)(client_t *c, iot_json_t *req);

static void lnc_connect(iot_transport_t *lt, void *user_data);
static void lnc_closed(iot_transport_t *t, int error, void *user_data);
static void lnc_recv(iot_transport_t *t, iot_json_t *req, void *user_data);


void transport_init(launcher_t *l)
{
    static iot_transport_evt_t lnc_evt = {
        { .recvjson     = lnc_recv },
        { .recvjsonfrom = NULL     },
        .connection     = lnc_connect,
        .closed         = lnc_closed,
    };

    static iot_transport_evt_t app_evt = {
        { .recvjson     = lnc_recv },
        { .recvjsonfrom = NULL     },
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

    flags = IOT_TRANSPORT_REUSEADDR | IOT_TRANSPORT_NONBLOCK |  \
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


static inline const char *client_type(client_t *c)
{
    return c->type == CLIENT_LAUNCHER ? "launcher" : "IoT-app";
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
                     c->id.pid);
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


static inline void dump_message(iot_json_t *msg, const char *fmt, ...)
{
    va_list ap;
    char    buf[4096], *p;
    int     l, n, line;

    if (!iot_debug_check(__func__, __FILE__, line=__LINE__))
        return;

    p = buf;
    n = sizeof(buf);

    va_start(ap, fmt);
    l = vsnprintf(p, (size_t)n, fmt, ap);
    va_end(ap);

    if (l < 0 || l >= n)
        return;

    p += l;
    n -= l;

    l = snprintf(p, n, "%s", iot_json_object_to_string(msg));

    if (l < 0 || l >= n)
        return;

    iot_log_msg(IOT_LOG_DEBUG, __FILE__, line, __func__, "%s", buf);
}


static handler_t request_handler(const char *type)
{
    static struct {
        const char        *type;
        handler_t  fn;
    } handlers[] = {
        { "setup"           , application_setup   },
        { "cleanup"         , application_cleanup },
        { "list"            , application_list    },
#if 0
        { "stop"            , application_stop    },
        { "query"           , application_query   },
#endif
        { "send-event"      , event_route         },
        { "subscribe-events", client_subscribe    },

        { NULL, NULL },
    }, *h;

    for (h = handlers; h->type != NULL; h++)
        if (!strcmp(h->type, type))
            return h->fn;

    return NULL;
}


static void lnc_recv(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    client_t   *c = (client_t *)user_data;
    handler_t   h;
    const char *f, *type;
    iot_json_t *req, *rpl, *s;
    int         seq;

    IOT_UNUSED(t);

    dump_message(msg, "Received %s message: ", client_type(c));

    if (!iot_json_get_string (msg, f="type" , &type) ||
        !iot_json_get_integer(msg, f="seqno", &seq ) ||
        !iot_json_get_object (msg, f=type   , &req )) {
        iot_log_error("Malformed request from %s, missing field '%s'.",
                      client_type(c), f);
        return;
    }

    if ((h = request_handler(type)) == NULL) {
        iot_log_error("Unknown request of type '%s' from %s.", type,
                      client_type(c));
        return;
    }

    if ((s = h(c, req)) == NULL)
        return;

    if ((rpl = iot_json_create(IOT_JSON_OBJECT)) == NULL) {
        iot_json_unref(s);
        return;
    }

    iot_json_add_string (rpl, "type"  , "status");
    iot_json_add_integer(rpl, "seqno" , seq     );
    iot_json_add_object (rpl, "status", s       );

    transport_send(c, rpl);

    iot_json_unref(rpl);
}


int transport_send(client_t *c, iot_json_t *msg)
{
    dump_message(msg, "Sending %s message: ", client_type(c));

    if (!iot_transport_sendjson(c->t, msg)) {
        errno = EIO;
        return -1;
    }

    return 0;
}


