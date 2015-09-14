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
#include <iot/launcher/daemon/msg.h>
#include <iot/app/app.h>

/*
 * IoT application context - opaque to the application
 */
struct iot_app_s {
    iot_mainloop_t     *ml;              /* mainloop to use for async events */
    iot_transport_t    *t;               /* communication channel to server */
    int                 seqno;           /* sequence number for next request */
    iot_list_hook_t     pendq;           /* queue of pending requests */
    iot_app_event_cb_t  event_cb;        /* app. event notification callback */
    void               *data;            /* application provided opaque data */
    iot_sighandler_t   *sig_hup;         /* signal handler for SIGHUP */
    iot_sighandler_t   *sig_term;        /* signal handler for SIGTERM */
};


/*
 * a pending request - a request sent, yet unanswered by the server
 */
typedef struct {
    iot_list_hook_t  hook;               /* to queue of pending requests */
    iot_json_t      *req;                /* original request */
    void            *user_data;          /* notification user data */
    union {                              /* completion notification callback */
        iot_app_send_cb_t    send;       /*     for sending events */
        iot_app_list_cb_t    list;       /*     for listing applications */
        iot_app_status_cb_t  status;     /*     for other requests */
        void                *any;        /*     for any of the above */
    } notify;
} pending_t;


static int transport_connect(iot_app_t *app, const char *server);
static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data);
static void closed_cb(iot_transport_t *t, int error, void *user_data);

static pending_t *pending_enq(iot_app_t *app, iot_json_t *req, void *cb,
                              void *user_data);
static void pending_notify(iot_app_t *app, int seqno, int status,
                           const char *msg, iot_json_t *data);
static void pending_purge(iot_app_t *app);


iot_app_t *iot_app_create(iot_mainloop_t *ml, void *data)
{
    iot_app_t *app;

    if (ml == NULL)
        goto invalid;

    app = iot_allocz(sizeof(*app));

    if (app == NULL)
        return NULL;

    iot_list_init(&app->pendq);
    app->ml    = ml;
    app->data  = data;
    app->seqno = 1;

    return app;

 invalid:
    errno = EINVAL;
    return NULL;
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
                                             iot_app_event_cb_t cb)
{
    iot_app_event_cb_t prev;

    if (app == NULL)
        goto invalid;

    prev = app->event_cb;
    app->event_cb = cb;

    return prev;

 invalid:
    errno = EINVAL;
    return NULL;
}


int iot_app_event_subscribe(iot_app_t *app, char **events,
                            iot_app_status_cb_t cb, void *user_data)
{
    iot_json_t *req, *pl;
    pending_t  *pnd;
    int         seq;

    if (app == NULL)
        goto invalid;

    if (app->event_cb == NULL)
        goto invalid;

    if (transport_connect(app, IOT_APPFW_ADDRESS) < 0)
        return -1;

    req = NULL;
    pl  = iot_json_create(IOT_JSON_OBJECT);

    if (pl == NULL)
        goto failed;

    if (!iot_json_add_string_array(pl, "events", events, (ssize_t)-1))
        goto failed;

    req = msg_request_create("subscribe-events", seq = app->seqno++, pl);

    if (req == NULL)
        goto failed;

    pnd = pending_enq(app, req, cb, user_data);

    if (pnd == NULL)
        goto failed;

    iot_json_unref(req);
    return seq;

 invalid:
    errno = EINVAL;
    return -1;

 failed:
    if (req == NULL)
        iot_json_unref(req);
    else
        iot_json_unref(pl);
    return -1;
}


int iot_app_event_send(iot_app_t *app, const char *event, iot_json_t *data,
                       iot_app_id_t *target, iot_app_send_cb_t cb,
                       void *user_data)
{
    iot_json_t *req, *pl;
    pending_t  *pnd;
    int         seq;

    if (!event || !*event || !target)
        goto invalid;

    if (transport_connect(app, IOT_APPFW_ADDRESS) < 0)
        return -1;

    pl = iot_json_create(IOT_JSON_OBJECT);

    if (pl == NULL)
        goto failed;

    if (!iot_json_add_string(pl, "event", event))
        goto failed;

    req = msg_request_create("send-event", seq = app->seqno++, pl);

    if (req == NULL)
        goto failed;

    if (data != NULL)
        iot_json_add_object(pl, "data", data);

    if (target) {
        if (target->label)
            if (!iot_json_add_string(pl, "label", target->label))
                goto failed;
        if (target->appid)
            if (!iot_json_add_string(pl, "appid", target->appid))
                goto failed;
        if (target->binary)
            if (!iot_json_add_string(pl, "binary", target->binary))
                goto failed;
        if (target->user != (uid_t)-1)
            if (!iot_json_add_integer(pl, "user", target->user))
                goto failed;
        if (target->process != 0)
            if (!iot_json_add_integer(pl, "process", target->process))
                goto failed;
    }

    pnd = pending_enq(app, req, cb, user_data);

    if (pnd == NULL)
        goto failed;

    iot_json_unref(req);
    return seq;

 invalid:
    errno = EINVAL;
 failed:
    if (req)
        iot_json_unref(req);
    else
        iot_json_unref(pl);
    return -1;
}


static int app_list(iot_app_t *app, int running, iot_app_list_cb_t cb,
                    void *user_data)
{
    iot_json_t *req, *pl;
    pending_t  *pnd;
    int         seq;

    if (transport_connect(app, IOT_APPFW_ADDRESS) < 0)
        return -1;

    pl = iot_json_create(IOT_JSON_OBJECT);

    if (pl == NULL)
        goto failed;

    if (!iot_json_add_string(pl, "type", running ? "running" : "installed"))
        goto failed;

    req = msg_request_create("list", seq = app->seqno++, pl);

    if (req == NULL)
        goto failed;

    pnd = pending_enq(app, req, cb, user_data);

    if (pnd == NULL)
        goto failed;

    iot_json_unref(req);
    return seq;

 failed:
    if (req)
        iot_json_unref(req);
    else
        iot_json_unref(pl);
    return -1;
}


int iot_app_list_running(iot_app_t *app, iot_app_list_cb_t cb, void *user_data)
{
    return app_list(app, true, cb, user_data);
}


int iot_app_list_all(iot_app_t *app, iot_app_list_cb_t cb, void *user_data)
{
    return app_list(app, false, cb, user_data);
}


static void bridge_signal(iot_sighandler_t *h, int signum, void *user_data)
{
    iot_app_t  *app = (iot_app_t *)user_data;
    const char *event;

    IOT_UNUSED(h);

    switch (signum) {
    case SIGHUP:  event = "system::reload";    break;
    case SIGTERM: event = "system::terminate"; break;
    default:
        return;
    }

    iot_debug("bridging system signal %d as event '%s'...", signum, event);

    app->event_cb(app, event, NULL);
}


int iot_app_bridge_signals(iot_app_t *app)
{
    if (app->sig_hup != NULL)
        return 0;

    if (app->event_cb == NULL)
        goto invalid;

    app->sig_hup  = iot_sighandler_add(app->ml, SIGHUP , bridge_signal, app);
    app->sig_term = iot_sighandler_add(app->ml, SIGTERM, bridge_signal, app);

    if (app->sig_hup == NULL || app->sig_term == NULL)
        goto failed;

    iot_debug("installed signal handler for bridging SIGHUP and SIGTERM");

    return 0;

 invalid:
    errno = EINVAL;
 failed:
    return -1;
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


static int transport_connect(iot_app_t *app, const char *server)
{
    static iot_transport_evt_t evt = {
        { .recvjson     = recv_cb },
        { .recvjsonfrom = NULL    },
          .closed       = closed_cb,
    };
    iot_sockaddr_t  addr;
    socklen_t       alen;
    const char     *type;
    int             flags;

    if (app == NULL)
        goto invalid;

    if (app->t != NULL)
        return 0;

    alen = iot_transport_resolve(NULL, server, &addr, sizeof(addr), &type);

    if (alen <= 0)
        goto invalid;

    flags  = IOT_TRANSPORT_MODE_JSON | IOT_TRANSPORT_REUSEADDR;
    app->t = iot_transport_create(app->ml, type, &evt, app, flags);

    if (app->t == NULL)
        goto invalid;

    if (!iot_transport_connect(app->t, &addr, alen))
        goto failed;

    iot_debug("connection to server established");

    return 0;

 invalid:
    if (app && app->t) {
        iot_transport_destroy(app->t);
        app->t = NULL;
    }
    errno = EINVAL;
    return -1;

 failed:
    if (app && app->t) {
        iot_transport_destroy(app->t);
        app->t = NULL;
    }
    return -1;
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
    const char *type, *message, *event;
    int         seqno, status;
    iot_json_t *data;

    IOT_UNUSED(t);

    dump_message(msg, "received message: ");

    type = msg_type(msg);

    if (type == NULL)
        return;

    if (!strcmp(type, "status")) {
        status = msg_reply_parse(msg, &seqno, &message, &data);

        if (status < 0)
            return;

        pending_notify(app, seqno, status, message, data);

        return;
    }

    if (!strcmp(type, "event")) {
        status = msg_event_parse(msg, &event, &data);

        if (status < 0)
            return;

        app->event_cb(app, event, data);

        return;
    }
}


static int extract_applist(iot_json_t *data, iot_app_info_t **appsptr)
{
    iot_app_info_t *apps, *a;
    iot_json_t     *app, *argv;
    int             napp, argc, i, j;

    apps = NULL;
    napp = iot_json_array_length(data);

    if (napp == 0)
        return 0;

    if (napp < 0)
        goto invalid;

    apps = iot_alloc_array(iot_app_info_t, napp);

    if (app == NULL)
        goto failed;

    for (i = 0; i < napp; i++) {
        a = apps + i;
        if (!iot_json_array_get_object(data, i, &app))
            goto invalid;

        if (!iot_json_get_string (app, "app"        , &a->appid      ) ||
            !iot_json_get_string (app, "description", &a->description) ||
            !iot_json_get_string (app, "desktop"    , &a->desktop    ) ||
            !iot_json_get_integer(app, "user"       , &a->user       ) ||
            !iot_json_get_array  (app, "argv"       , &argv          ))
            goto invalid;

        argc = iot_json_array_length(argv);

        if (argc < 0)
            goto invalid;

        a->argv = iot_allocz_array(const char *, argc);

        if (a->argv == NULL)
            goto failed;

        for (j = 0; j < argc; j++)
            if (!iot_json_array_get_string(argv, j, a->argv + j))
                goto invalid;

        a->argc = argc;
    }

    *appsptr = apps;
    return napp;

 invalid:
    errno = EINVAL;
 failed:
    if (apps != NULL) {
        for (i = 0; i < napp; i++) {
            a = apps + i;
            iot_free(a->argv);
        }
        iot_free(apps);
    }
    *appsptr = NULL;
    return -1;
}


static void free_applist(int napp, iot_app_info_t *apps)
{
    int i;

    if (apps == NULL)
        return;

    for (i = 0; i < napp; i++)
        iot_free(apps[i].argv);

    iot_free(apps);
}


static pending_t *pending_enq(iot_app_t *app, iot_json_t *req, void *cb,
                              void *user_data)
{
    pending_t *pnd;

    pnd = iot_allocz(sizeof(*pnd));

    if (pnd == NULL)
        goto failed;

    if (!iot_transport_sendjson(app->t, req))
        goto ioerr;

    iot_list_init(&pnd->hook);
    pnd->req        = iot_json_ref(req);
    pnd->notify.any = cb;
    pnd->user_data  = user_data;
    iot_list_append(&app->pendq, &pnd->hook);

    return pnd;

 ioerr:
    errno = EIO;

 failed:
    iot_free(pnd);
    return NULL;
}


static void pending_deq(pending_t *pnd)
{
    if (pnd == NULL)
        return;

    iot_list_delete(&pnd->hook);
    iot_json_unref(pnd->req);
    iot_free(pnd);
}


static void pending_notify(iot_app_t *app, int seqno, int status,
                           const char *msg, iot_json_t *data)
{
    iot_list_hook_t *p, *n;
    pending_t       *pnd;
    iot_json_t      *req;
    const char      *type;
    int              seq;
    iot_app_info_t  *apps;
    int              napp;

    iot_list_foreach(&app->pendq, p, n) {
        pnd = iot_list_entry(p, typeof(*pnd), hook);
        req = pnd->req;

        if (!iot_json_get_string (req, "type" , &type) ||
            !iot_json_get_integer(req, "seqno", &seq ))
            continue;

        if (seqno != -1 && seq != seqno)
            continue;

        if (!strcmp(type, "send-event")) {
            pnd->notify.send(app, seq, status, msg, pnd->user_data);
        }
        else if (!strcmp(type, "list")) {
            napp = 0;
            apps = NULL;

            if (status != 0)
                goto notify_applist;

            napp = extract_applist(data, &apps);

            if (napp < 0) {
                status = errno;
                msg    = "Failed to extract application list.";
            }

        notify_applist:
            pnd->notify.list(app, seq, status, msg, napp, apps, pnd->user_data);

            free_applist(napp, apps);
        }
        else {
            pnd->notify.status(app, seq, status, msg, data, pnd->user_data);
        }

        pending_deq(pnd);
    }
}


static void pending_purge(iot_app_t *app)
{
   iot_list_hook_t *p, *n;
    pending_t       *pnd;

    iot_list_foreach(&app->pendq, p, n) {
        pnd = iot_list_entry(p, typeof(*pnd), hook);
        pending_deq(pnd);
    }
}


