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
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>
#include <iot/common.h>

#ifdef GLIB_ENABLED
#include <iot/common/glib-glue.h>
#endif
#ifdef UV_STANDALONE
#include <iot/common/uv-glue.h>
#endif

#include <iot/app.h>


/*
 * test application runtime context
 */

enum {
#ifdef GLIB_ENABLED
    APP_GLIB,
#endif
#ifdef UV_STANDALONE
    APP_UV,
#endif
    APP_INVALID
};

typedef struct {
    int          type;
    iot_app_t   *iot;                    /* IoT application context */
    void        *ml;                     /* native mainloop we use */
    int          server;                 /* subscribe for or send events */
    const char  *label;                  /* client event destination label */
    const char  *appid;                  /*     app. id */
    const char  *binary;                 /*     binary */
    uid_t        user;                   /*     user id */
    pid_t        process;                /*     proess id */
    int          log_mask;               /* logging mask */
    const char  *evlist;                 /* event list */
    char       **events;                 /*     parsed into an array */
    int          nevent;                 /*     number of events */
    int          nsend;                  /* total events to send */
    int          nburst;                 /* events in initial burst */
    int          ival;                   /* send interval in msecs */
} app_t;


void mainloop_create(app_t *app);
void mainloop_run(app_t *app);
void mainloop_quit(app_t *app);
void mainloop_destroy(app_t *app);

/**
 * @brief IoT event notification callback.
 *
 * This callback is used by the application framework to notify the
 * the application that it has received an IoT event.
 *
 * @param [in] iot    IoT application context
 * @param [in] event  name of the event received
 * @param [in] data   JSON data attached to the event
 *
 */
static void event_cb(iot_app_t *iot, const char *event, iot_json_t *data)
{
    app_t *app;

    iot_log_info("Received event <%s>, data: %s", event,
                 iot_json_object_to_string(data));

    if (!strcmp(event, "sayonara")) {
        app = iot_app_get_data(iot);
        mainloop_quit(app);
    }
}


/**
 * @brief IoT event subscribe status callback.
 *
 * This callback is used by the application framework to notify the
 * application about the status of an event subscription request.
 * Providing such a callback is optional. Applications that do not
 * care about the status of the subscription can pass @NULL to
 * @iot_app_event_subscribe, in which case they will not be aware
 * of asynchronous failure on the server side.
 *
 * @param [in] iot        IoT application context
 * @param [in] seqno      subscription request number
 * @param [in] status     request status, 0 for success, nonzero for failure
 * @param [in] msg        error message, if @status != 0
 * @param [in] data       optional request-specific response data
 * @param [in] user_data  opaque user data passed in to the request
 *
 * Since application usually subscribe for events just once, seqno can be
 * ignored (otherwise it could be used to tell which subscription request
 * the response is for). Data sent back from the server for a subscription
 * request will always be @NULL.
 */
static void subscribe_status(iot_app_t *iot, int seqno, int status,
                             const char *msg, iot_json_t *data, void *user_data)
{
    IOT_UNUSED(iot);
    IOT_UNUSED(seqno);
    IOT_UNUSED(data);
    IOT_UNUSED(user_data);

    if (status == 0)
        iot_log_info("Successfully subscribed for events.");
    else {
        iot_log_error("Event subscription failed (%d: %s).", status,
                      msg ? msg : "<unknown error>");
        exit(1);
    }
}


/*
 * set up for running in 'server' mode
 *
 * - subscribe for a set of events
 * - ask the framework to generate pseudoevents for SIGHUP and SIGTERM
 */
static void setup_server(app_t *app)
{
    char **events = app->events;

    iot_log_info("Subscribing for events...");

    iot_app_event_set_handler(app->iot, event_cb);

    if (iot_app_event_subscribe(app->iot, events, subscribe_status, NULL) < 0) {
        iot_log_error("Event subscription failed.");
        exit(1);
    }

    if (iot_app_bridge_signals(app->iot) < 0) {
        iot_log_error("System event signal subscription failed.");
        exit(1);
    }

    iot_log_info("Event subscription requests sent...");
}


/**
 * @brief Status notification callback for an event send request.
 *
 * This callback is used by the application framework to notify the
 * application about the status of an event send request. Providing
 * such a callback is optional. Applications that do not care about
 * whether sending an event succeeds should pass @NULL for the callback
 * to @iot_app_event_send.
 *
 * @param [in] iot        IoT application context
 * @param [in] seqno      event send request number
 * @param [in] status     request status, 0 for success, nonzero for failure
 * @param [in] msg        error message, if @status != 0
 * @param [in] user_data  opaque user data passed in to the request
 */
static void send_status(iot_app_t *iot, int seqno, int status, const char *msg,
                        void *user_data)
{
    IOT_UNUSED(iot);
    IOT_UNUSED(user_data);

    if (status == 0)
        iot_log_info("Event request #%d successfully delivered.", seqno);
    else
        iot_log_error("Event request #%d failed (%d: %s).", seqno,
                      status, msg ? msg : "<unknown error>");
}


/*
 * send an event to the matching application(s)
 *
 * - pick an event to send
 * - attach some test data (just a counter) to the event
 * - send it to matching applications (target id selectable on command line)
 */
static int send_event(app_t *app, int sayonara)
{
    static int   cnt    = 0;
    char       **events = app->events;
    iot_app_id_t id  = {
        .label   = app->label,
        .appid   = app->appid,
        .binary  = app->binary,
        .user    = app->user,
        .process = app->process,
    };
    char       *event;
    iot_json_t *data;
    int         seq, c;

    event   = !sayonara ? events[cnt % app->nevent] : "sayonara";
    data    = iot_json_create(IOT_JSON_OBJECT);
    iot_json_add_integer(data, "count", c = cnt++);

    seq = iot_app_event_send(app->iot, event, data, &id, send_status, NULL);

    if (seq < 0) {
        iot_log_error("Failed to send event request.");
        exit(1);
    }

    iot_log_info("Sending event <#%d:%s> (#%d) to { %s,%s,%s, user %d, pid %u }",
                 c, event, seq,
                 id.label  ? id.label  : "-",
                 id.appid  ? id.appid  : "-",
                 id.binary ? id.binary : "-",
                 id.user,
                 id.process);

    return cnt;
}


/*
 * client timer callback
 *
 * - send a new event until we're done
 * - once done
 *     * emit a final 'sayonara' event
 *     * quit the mainloop
 */
static void timer_cb(iot_timer_t *tmr, void *user_data)
{
    app_t *app = (app_t *)user_data;
    int    cnt;

    cnt = send_event(app, FALSE);

    if (cnt >= app->nsend) {
        iot_timer_del(tmr);
        send_event(app, TRUE);
        mainloop_quit(app);
    }
}


/*
 * set up for runnin in 'client' mode
 *
 * - set up a timer for sending events
 * - also send a few initial events in a tight loop for a starter
 */
static void setup_client(app_t *app)
{
    iot_mainloop_t *ml = iot_app_get_mainloop(app->iot);
    iot_timer_t    *tmr;
    int             delay, i;

    tmr = iot_timer_add(ml, delay = app->ival, timer_cb, app);

    if (tmr == NULL) {
        iot_log_error("Failed to create event sending timer.");
        exit(1);
    }

    iot_log_info("Event sending timer set up (interval %d)...", delay);

    for (i = 0; i < app->nburst; i++)
        send_event(app, FALSE);
}


/*
 * print help on usage
 */
static void print_usage(app_t *app, const char *argv0, int exit_code,
                        const char *fmt, ...)
{
    va_list ap;

    IOT_UNUSED(app);

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }

    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -s, --server                   subscribe and wait for events\n"
           "  -l, --label=<label>            target application label\n"
           "  -a, --appid=<appid>            target application id\n"
           "  -b, --binary=<path>            target application binary path\n"
           "  -u, --user=<user-name>         target application user\n"
           "  -p, --process=<process-id>     target application process id\n"
           "  -e, --events=<evt1,...,evtN>   events to send/subscribe for \n"
           "  -S, --send=<events>            number of event to send\n"
           "  -B, --burst=<events>           number of events in initial burst\n"
           "  -I, --interval=<msecs>         delay between sending\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
           "  -h, --help                     show help on usage\n",
           argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


/*
 * parse_events - parse the given set of comma-separated list of events
 */
static void parse_events(app_t *app)
{
    const char *list = app->evlist;
    char      **events = NULL;
    int         nevent = 0;
    const char *b, *e, *next;
    int         n, sayonara, extra;

    events   = NULL;
    nevent   = 0;
    sayonara = FALSE;

    b = list;
    while (b != NULL) {
        next = e = strchr(b, ',');

        while (*b == ' ' || *b == '\t')
            b++;

        if (e != NULL) {
            e--;
            while (*e == ' ' || *e == '\t')
                e--;
        }

        n = e ? e - b + 1 : (int)strlen(b);

        if (!iot_reallocz(events, nevent, nevent + 1)) {
            iot_log_error("Failed reallocate event array.");
            exit(1);
        }

        events[nevent] = iot_allocz(n + 1);

        if (events[nevent] == NULL) {
            iot_log_error("Failed to allocate event array entry.");
            exit(1);
        }

        strncpy(events[nevent], b, n);
        if (!strcmp(events[nevent], "sayonara"))
            sayonara = TRUE;

        iot_log_info("Added event '%s'...", events[nevent]);
        nevent++;

        e = next;
        b = e ? e + 1 : NULL;
    }

    extra = app->server && !sayonara;
    if (!iot_reallocz(events, nevent, nevent + extra + 1)) {
        iot_log_error("Failed to reallocate event array.");
        exit(1);
    }

    if (extra) {
        events[nevent] = iot_strdup("sayonara");
        if (events[nevent] == NULL) {
            iot_log_error("Failed to reallocate event array.");
            exit(1);
        }
        nevent++;
    }

    app->events = events;
    app->nevent = nevent;
}


/*
 * set up defaults - run in client mode, send events to our user id
 */
static void config_set_defaults(app_t *app)
{
    app->server   = 0;
    app->label    = NULL;
    app->appid    = NULL;
    app->binary   = NULL;
    app->user     = getuid();
    app->process  = 0;
    app->log_mask = IOT_LOG_UPTO(IOT_LOG_ERROR);

    app->nsend  = 25;
    app->nburst = 10;
    app->ival   = 1000;
}


/*
 * mainloop functions - create, run, quit mainloop
 */
void mainloop_create(app_t *app)
{
    iot_mainloop_t *ml = NULL;

    switch (app->type) {
#ifdef GLIB_ENABLED
    case APP_GLIB:
        app->ml = g_main_loop_new(NULL, FALSE);

        if (app->ml == NULL) {
            iot_log_error("Failed to create GMainLoop.");
            exit(1);
        }

        ml = iot_mainloop_glib_get(app->ml);

        if (ml == NULL) {
            iot_log_error("Failed to create IoT/glib mainloop.");
            exit(1);
        }
        break;
#endif

#ifdef UV_STANDALONE
    case APP_UV:
        app->ml = uv_default_loop();

        if (app->ml == NULL) {
            iot_log_error("Failed to create UV mainloop.");
            exit(1);
        }

        ml = iot_mainloop_uv_get(app->ml);

        if (ml == NULL) {
            iot_log_error("Failed to create IoT/UV mainloop.");
            exit(1);
        }
        break;
#endif

    case APP_INVALID:
    default:
        iot_log_error("Hey... you did not enable any mainloop I can use.");
        exit(1);
    }

    app->iot = iot_app_create(ml, app);

    if (app->iot == NULL) {
        iot_log_error("Failed to create IoT application context.");
        exit(1);
    }
}


void mainloop_run(app_t *app)
{
    switch (app->type) {
#ifdef GLIB_ENABLED
    case APP_GLIB:
        g_main_loop_run(app->ml);
        break;
#endif

#ifdef UV_STANDALONE
    case APP_UV:
        uv_run(app->ml, UV_RUN_DEFAULT);
        break;
#endif

    case APP_INVALID:
    default:
        iot_log_error("Hey... you did not enable any mainloop I can use.");
        exit(1);
    }
}


void mainloop_quit(app_t *app)
{
    switch (app->type) {
#ifdef GLIB_ENABLED
    case APP_GLIB:
        g_main_loop_quit(app->ml);
        break;
#endif

#ifdef UV_STANDALONE
    case APP_UV:
        uv_stop(app->ml);
        break;
#endif

    case APP_INVALID:
    default:
        iot_log_error("Hey... you did not enable any mainloop I can use.");
        exit(1);
    }
}


void mainloop_destroy(app_t *app)
{
    switch (app->type) {
#ifdef GLIB_ENABLED
    case APP_GLIB:
        g_main_loop_unref(app->ml);
        app->ml = NULL;
        break;
#endif

#ifdef UV_STANDALONE
 case APP_UV:
        uv_unref(app->ml);
        app->ml = NULL;
        break;
#endif

    case APP_INVALID:
    default:
        iot_log_error("Hey... you did not enable any mainloop I can use.");
        exit(1);
    }
}


/*
 * parse the command line
 */
static void parse_cmdline(app_t *app, int argc, char **argv, char **envp)
{
#ifdef GLIB_ENABLED
#    define OPT_GLIB "G"
#else
#    define OPT_GLIB ""
#endif
#ifdef UV_STANDALONE
#    define OPT_UV "U"
#else
#    define OPT_UV ""
#endif
#   define OPTIONS "sl:a:b:u:p:e:S:B:I:vd:h"OPT_GLIB""OPT_UV
    struct option options[] = {
        { "server"  , no_argument      , NULL, 's' },
        { "label"   , required_argument, NULL, 'l' },
        { "appid"   , required_argument, NULL, 'a' },
        { "binary"  , required_argument, NULL, 'b' },
        { "user"    , required_argument, NULL, 'u' },
        { "process" , required_argument, NULL, 'p' },
        { "events"  , required_argument, NULL, 'e' },
        { "send"    , required_argument, NULL, 'S' },
        { "burst"   , required_argument, NULL, 'B' },
        { "interval", required_argument, NULL, 'I' },
        { "verbose" , optional_argument, NULL, 'v' },
#ifdef GLIB_ENABLED
        { "glib"    , no_argument      , NULL, 'G' },
#endif
#ifdef UV_STANDALONE
        { "uv"      , no_argument      , NULL, 'U' },
#endif
        { "debug"   , required_argument, NULL, 'd' },
        { "help"    , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int   opt, help;
    char *e;

    IOT_UNUSED(envp);

    config_set_defaults(app);
    iot_log_set_mask(app->log_mask);
    iot_log_set_target(IOT_LOG_TO_STDERR);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 's':
            app->server = TRUE;
            break;

        case 'l':
            app->label = optarg;
            break;

        case 'a':
            app->appid = optarg;
            break;

        case 'b':
            app->binary = optarg;
            break;

        case 'u': {
            struct passwd *pw = getpwnam(optarg);

            if (pw == NULL) {
                iot_log_error("Unknown user: '%s'.", optarg);
                exit(1);
            }

            app->user = pw->pw_uid;
            break;
        }

        case 'p': {
            app->process = (pid_t)strtol(optarg, &e, 10);

            if (e && *e) {
                iot_log_error("invalid non-nunmeric process id: '%s'.", optarg);
                exit(1);
            }
            break;
        }

        case 'e':
            app->evlist = optarg;
            break;

        case 'S':
            app->nsend = strtoul(optarg, &e, 10);

            if (e && *e) {
                iot_log_error("invalid number of events to send: '%s'.", optarg);
                exit(1);
            }
            break;

        case 'B':
            app->nburst = strtoul(optarg, &e, 10);

            if (e && *e) {
                iot_log_error("invalid inital burst: '%s'.", optarg);
                exit(1);
            }
            break;

        case 'I':
            app->ival = strtoul(optarg, &e, 10);

            if (e && *e) {
                iot_log_error("invalid send interval: '%s'.", optarg);
                exit(1);
            }
            break;

        case 'v':
            app->log_mask <<= 1;
            app->log_mask  |= 1;
            iot_log_set_mask(app->log_mask);
            break;

        case 'd':
            app->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

#ifdef GLIB_ENABLED
        case 'G':
            app->type = APP_GLIB;
            iot_log_info("Using GLIB mainloop...");
            break;
#endif

#ifdef UV_STANDALONE
        case 'U':
            app->type = APP_UV;
            iot_log_info("Using UV mainloop...");
            break;
#endif

        case 'h':
            help++;
            break;

        default:
            print_usage(app, argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(app, argv[0], -1, "");
        exit(0);
    }

    if (app->evlist == NULL) {
        static const char *slist = "hello,ahoy,aloha,goodbye,sayonara";
        static const char *clist = "hello,howdy,ahoy,hallo,aloha,goodbye";

        app->evlist = app->server ? slist : clist;
    }

    parse_events(app);
}


/*
 * a very simple IoT event test
 *
 * - create native mainloop (GMainLoop in this case)
 * - put an IoT mainloop abstraction on top ot it
 * - create an IoT application context
 * - parse the command line
 * - go in to server (event subscribe) or client (event send) mode
 */
int main(int argc, char *argv[], char *envp[])
{
    app_t app;

    iot_clear(&app);

    parse_cmdline(&app, argc, argv, envp);

    mainloop_create(&app);

    if (app.server)
        setup_server(&app);
    else
        setup_client(&app);

    mainloop_run(&app);

    return 0;
}
