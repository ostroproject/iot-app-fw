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

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/list.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>

#include <iot/launcher/iot-launch.h>
#include <iot/app/app.h>

/*
 * IoT application context
 */

struct iot_app_s {
    iot_mainloop_t     *ml;              /* mainloop abstraction to use */
    void               *data;            /* opaque application data */
    iot_transport_t    *t;               /* transport to server (event relay) */
    int                 seqno;           /* next sequence number */
    iot_list_hook_t     pending;         /* queue of pending requests */
    iot_app_event_cb_t  event_cb;        /* event notification callback */
    iot_sighandler_t   *sigh;            /* SIGHUP handler */
    iot_sighandler_t   *sigt;            /* SIGTERM handler */
};


/*
 * a pending request waiting for a reply
 */

typedef enum {
    REQUEST_UNKNOWN = 0,
    REQUEST_SEND,                        /* event send request */
    REQUEST_SUBSCRIBE,                   /* event subscribe request */
} request_type_t;

typedef struct {
    iot_list_hook_t  hook;               /* to pending request queue */
    int              type;               /* associated request type */
    int              seqno;              /*     and sequence number */
    void            *user_data;          /* notification user data */
    union {                              /* completion notification */
        iot_app_send_cb_t    send;       /*     for event sending */
        iot_app_status_cb_t  status;     /*     for other requests */
        void                *any;        /*     for any request */
    } notify;
} pending_t;


static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data);
static void closed_cb(iot_transport_t *t, int error, void *user_data);

static pending_t *pending_create(int type, int seqno, void *cb, void *user_data);
static void pending_destroy(pending_t *pnd);
static void pending_purge(iot_app_t *app);
static pending_t *pending_find(iot_app_t *app, int seqno);
static void pending_notify(iot_app_t *app, int seqno, int status,
                           const char *msg, iot_json_t *data);
static void pending_enq(iot_app_t *app, pending_t *pending);

static void event_dispatch(iot_app_t *app, const char *event, iot_json_t *data);

static int transport_connect(iot_app_t *app, const char *server)
{
    static iot_transport_evt_t evt = {
        { .recvjson     = recv_cb },
        { .recvjsonfrom = NULL    },
          .closed       = closed_cb,
    };

    iot_sockaddr_t  addr;
    socklen_t       len;
    const char     *type;
    int             flags;

    len = iot_transport_resolve(NULL, server, &addr, sizeof(addr), &type);

    if (len <= 0)
        return -1;

    flags  = IOT_TRANSPORT_MODE_JSON;
    app->t = iot_transport_create(app->ml, type, &evt, app, flags);

    if (app->t == NULL)
        return -1;

    if (!iot_transport_connect(app->t, &addr, len)) {
        iot_transport_destroy(app->t);
        app->t = NULL;
        return -1;
    }

    iot_debug("connection to server established");

    return 0;
}


iot_app_t *iot_app_create(iot_mainloop_t *ml, void *data)
{
    iot_app_t *app;

    if (ml == NULL) {
        errno = EINVAL;
        return NULL;
    }

    app = iot_allocz(sizeof(*app));

    if (app == NULL)
        return NULL;

    iot_list_init(&app->pending);
    app->ml    = ml;
    app->data  = data;
    app->seqno = 1;

    return app;
}


void iot_app_destroy(iot_app_t *app)
{
    if (app == NULL)
        return;

    iot_transport_disconnect(app->t);
    iot_transport_destroy(app->t);

    pending_purge(app);

    iot_free(app);
}


void *iot_app_get_data(iot_app_t *app)
{
    if (app == NULL)
        return NULL;

    return app->data;
}


iot_mainloop_t *iot_app_get_mainloop(iot_app_t *app)
{
    if (app == NULL)
        return NULL;

    return app->ml;
}


iot_app_event_cb_t iot_app_event_set_handler(iot_app_t *app,
                                             iot_app_event_cb_t handler)
{
    iot_app_event_cb_t old;

    if (app == NULL)
        return NULL;

    old = app->event_cb;
    app->event_cb = handler;

    return old;
}


int iot_app_event_subscribe(iot_app_t *app, char **events,
                            iot_app_status_cb_t cb, void *user_data)
{
    iot_json_t *req;
    pending_t  *pnd;
    int         seq, cnt;

    if (app == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (app->event_cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (app->t == NULL) {
        if (transport_connect(app, IOT_APPFW_ADDRESS) < 0)
            return -1;
    }

    req = iot_json_create(IOT_JSON_OBJECT);
    pnd = NULL;

    if (req == NULL)
        goto fail;

    if (!iot_json_add_string(req, "type", "subscribe-events"))
        goto fail;
    if (!iot_json_add_integer(req, "seqno", seq = app->seqno++))
        goto fail;

    cnt = 0;
    if (events != NULL) {
        while (events[cnt] != NULL) {
            iot_debug("subscribing to event %s...", events[cnt]);
            cnt++;
        }
    }

    if (!iot_json_add_string_array(req, "events", events, cnt))
        goto fail;

    pnd = pending_create(REQUEST_SUBSCRIBE, seq, cb, user_data);

    if (pnd == NULL)
        goto fail;

    if (!iot_transport_sendjson(app->t, req)) {
        errno = EIO;
        goto fail;
    }

    pending_enq(app, pnd);
    iot_json_unref(req);

    return seq;

 fail:
    iot_json_unref(req);
    pending_destroy(pnd);

    return -1;
}


static void bridge_signals(iot_sighandler_t *h, int signum, void *user_data)
{
    iot_app_t  *app = (iot_app_t *)user_data;
    const char *event;

    IOT_UNUSED(h);

    switch (signum) {
    case SIGHUP:
        event = "system::reload";
        break;
    case SIGTERM:
        event = "system::terminate";
        break;
    default:
        return;
    }

    event_dispatch(app, event, NULL);
}


int iot_app_bridge_signals(iot_app_t *app)
{
    if (app->sigh != NULL)
        return 0;

    if (app->event_cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    app->sigh = iot_sighandler_add(app->ml, SIGHUP , bridge_signals, app);
    app->sigt = iot_sighandler_add(app->ml, SIGTERM, bridge_signals, app);

    if (app->sigh == NULL || app->sigt == NULL) {
        iot_sighandler_del(app->sigh);
        iot_sighandler_del(app->sigt);

        app->sigh = app->sigt = NULL;
        return -1;
    }

    return 0;
}


int iot_app_event_send(iot_app_t *app, const char *event, iot_json_t *data,
                       iot_app_id_t *target, iot_app_send_cb_t cb,
                       void *user_data)
{
    iot_json_t *req;
    pending_t  *pnd;
    int         seq;

    if (app->t == NULL) {
        if (transport_connect(app, IOT_APPFW_ADDRESS) < 0)
            return -1;
    }

    if (!event || !*event || !target ||
        !(target->label || target->appid || target->binary ||
          target->user != (uid_t)-1 || target->process != 0)) {
        errno = EINVAL;
        return -1;
    }

    req = iot_json_create(IOT_JSON_OBJECT);
    pnd = NULL;

    if (req == NULL)
        goto fail;

    if (!iot_json_add_string(req, "type", "send-event"))
        goto fail;
    if (!iot_json_add_integer(req, "seqno", seq = app->seqno++))
        goto fail;
    if (!iot_json_add_string(req, "event", event))
        goto fail;

    /*
     * Hmm... should we ref data here ? If we want caller-owns semantics,
     * we should... otherwise unreffing the request will unref (and free)
     * data as well unless the caller has added an extra reference.
     */
    if (data)
        iot_json_add_object(req, "data", data);

    if (target->label)
        if (!iot_json_add_string(req, "label", target->label))
            goto fail;
    if (target->appid)
        if (!iot_json_add_string(req, "appid", target->appid))
            goto fail;
    if (target->binary)
        if (!iot_json_add_string(req, "binary", target->binary))
            goto fail;
    if (target->user != (uid_t)-1)
        if (!iot_json_add_integer(req, "user", target->user))
            goto fail;
    if (target->process != 0)
        if (!iot_json_add_integer(req, "process", target->process))
            goto fail;

    pnd = pending_create(REQUEST_SEND, seq, cb, user_data);

    if (pnd == NULL)
        goto fail;

    if (!iot_transport_sendjson(app->t, req)) {
        errno = EIO;
        goto fail;
    }

    pending_enq(app, pnd);

    return seq;

 fail:
    iot_json_unref(req);
    pending_destroy(pnd);

    return -1;
}


static void event_dispatch(iot_app_t *app, const char *event, iot_json_t *data)
{
    if (app->event_cb == NULL)
        return;

    app->event_cb(app, event, data);
}


static void closed_cb(iot_transport_t *t, int error, void *user_data)
{
    iot_app_t *app = (iot_app_t *)user_data;

    IOT_UNUSED(t);

    iot_debug("connection to server down");

    pending_notify(app, -1, error ? error : ENOTCONN, "connection down", NULL);

    iot_transport_destroy(app->t);
    app->t = NULL;
}


static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    iot_app_t  *app = (iot_app_t *)user_data;
    iot_json_t *data;
    const char *type, *message, *event;
    int         seqno, status;

    IOT_UNUSED(t);

    iot_debug("got message: %s", iot_json_object_to_string(msg));

    if (!iot_json_get_string (msg, "type" , &type) ||
        !iot_json_get_integer(msg, "seqno", &seqno))
        return;

    if (!strcmp(type, "status")) {
        if (!iot_json_get_integer(msg, "status", &status))
            return;

        if (!iot_json_get_string(msg, "message", &message))
            message = NULL;

        data = iot_json_get(msg, "data");

        pending_notify(app, seqno, status, message, data);
    }
    else if (!strcmp(type, "event")) {
        if (!iot_json_get_string(msg, "event", &event))
            return;

        data = iot_json_get(msg, "data");

        event_dispatch(app, event, data);
    }
}


static pending_t *pending_create(int type, int seqno, void *cb, void *user_data)
{
    pending_t *pnd;

    pnd = iot_allocz(sizeof(*pnd));

    if (pnd == NULL)
        return NULL;

    iot_list_init(&pnd->hook);
    pnd->type       = type;
    pnd->seqno      = seqno;
    pnd->notify.any = cb;
    pnd->user_data  = user_data;

    return pnd;
}


static void pending_enq(iot_app_t *app, pending_t *pnd)
{
    iot_list_append(&app->pending, &pnd->hook);
}


static void pending_destroy(pending_t *pnd)
{
    if (pnd == NULL)
        return;

    iot_list_delete(&pnd->hook);
    iot_free(pnd);
}


static void pending_purge(iot_app_t *app)
{
    iot_list_hook_t *p, *n;
    pending_t       *pnd;

    iot_list_foreach(&app->pending, p, n) {
        pnd = iot_list_entry(p, typeof(*pnd), hook);

        pending_destroy(pnd);
    }
}


static pending_t *pending_find(iot_app_t *app, int seqno)
{
    iot_list_hook_t *p, *n;
    pending_t       *pnd;

    iot_list_foreach(&app->pending, p, n) {
        pnd = iot_list_entry(p, typeof(*pnd), hook);

        if (pnd->seqno == seqno)
            return pnd;
    }

    return NULL;
}


static void pending_notify(iot_app_t *app, int seqno, int status,
                           const char *msg, iot_json_t *data)
{
    iot_list_hook_t *p, *n;
    pending_t       *pnd;

    iot_list_foreach(&app->pending, p, n) {
        pnd = iot_list_entry(p, typeof(*pnd), hook);

        if (seqno != -1 && seqno != pnd->seqno)
            continue;

        if (pnd->notify.send != NULL) {
            switch (pnd->type) {
            case REQUEST_SEND:
                pnd->notify.send(app, pnd->seqno, status, msg, pnd->user_data);
                break;

            default:
                pnd->notify.status(app, pnd->seqno, status, msg, data,
                                   pnd->user_data);
                break;
            }
        }

        pending_destroy(pnd);
    }
}


