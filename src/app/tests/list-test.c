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

#include <iot/utils.h>
#include <iot/app.h>

/*
 * test application runtime context
 */

enum {
    APP_INVALID,
#ifdef GLIB_ENABLED
    APP_GLIB,
#endif
#ifdef UV_STANDALONE
    APP_UV,
#endif
};

typedef struct {
    int          type;
    iot_app_t   *iot;                    /* IoT application context */
    void        *ml;                     /* native mainloop we use */
    int          running;
    int          log_mask;               /* logging mask */
} app_t;


void mainloop_create(app_t *app);
void mainloop_run(app_t *app);
void mainloop_quit(app_t *app);
void mainloop_destroy(app_t *app);


/**
 * @brief List notification callback.
 *
 * This callback is used by the application framework to notify the
 * application about the result of an application list query.
 *
 * @param [in] iot        IoT application context
 * @param [in] seqno      list request number
 * @param [in] status     request status, 0 for success, nonzero for failure
 * @param [in] msg        error message, if @status != 0
 * @param [in] napp       number of applications retrieved (if @status != 0)
 * @param [in] apps       applications retrieved (if @status != 0)
 * @param [in] user_data  opaque user data passed in to the request
 */

static void list_cb(iot_app_t *iot, int id, int status, const char *msg,
                    int napp, iot_app_info_t *apps, void *user_data)
{
    app_t *app = (app_t *)user_data;
    int    i;

    IOT_UNUSED(iot);
    IOT_UNUSED(id);

    if (status != 0) {
        iot_log_error("Application listing failed (error %d: %s).",
                      status, msg ? msg : "unknown error");
    }
    else {
        printf("Got list of %d applications:\n", napp);
        for (i = 0; i < napp; i++) {
            char usr[64];
            printf("#%d.\n", i + 1);
            printf("        appid: %s\n", apps[i].appid);
            printf("  description: %s\n", apps[i].description);
            printf("      desktop: %s\n", apps[i].desktop);
            printf("         user: %s\n",
                   iot_get_username(apps[i].user, usr, sizeof(usr)));
            printf("      argv[0]: %s\n", apps[i].argv[0]);
            printf("\n");
        }
    }

    mainloop_quit(app);
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
           "  -r, --running            list running applications\n"
           "  -a, --all                list all installed applications\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
#ifdef GLIB_ENABLED
           "  -G, --glib                     use a GMainLoop\n"
#endif
#ifdef UV_STANDALONE
           "  -U, --uv                       use a libuv mainloop\n"
#endif
           "  -h, --help                     show help on usage\n",
           argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


/*
 * set up defaults - run in client mode, send events to our user id
 */
static void config_set_defaults(app_t *app)
{
    app->running = true;
    app->log_mask = IOT_LOG_UPTO(IOT_LOG_ERROR);
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
        ml = app->ml = iot_mainloop_create();

        if (app->ml == NULL) {
            iot_log_error("Failed to create mainloop.");
            exit(1);
        }
        break;

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
        iot_mainloop_run(app->ml);
        break;

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
        iot_mainloop_quit(app->ml, 0);
        break;

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
        iot_mainloop_destroy(app->ml);
        app->ml = NULL;
        break;

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
#   define OPTIONS "ravd:h"OPT_GLIB""OPT_UV
    struct option options[] = {
        { "running", no_argument      , NULL, 'r' },
        { "all"    , no_argument      , NULL, 'a' },
#ifdef GLIB_ENABLED
        { "glib"   , no_argument      , NULL, 'G' },
#endif
#ifdef UV_STANDALONE
        { "uv"     , no_argument      , NULL, 'U' },
#endif
        { "verbose", no_argument      , NULL, 'v' },
        { "debug"  , required_argument, NULL, 'd' },
        { "help"   , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt, help;

    IOT_UNUSED(envp);

    config_set_defaults(app);
    iot_log_set_mask(app->log_mask);
    iot_log_set_target(IOT_LOG_TO_STDERR);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'r':
            app->running = true;
            break;

        case 'a':
            app->running = false;
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
}


/*
 * a very simple IoT application listing test
 *
 * - create a mainloop
 * - create an IoT application context
 * - call app listing with a callback
 * - run mainloop (until reply) if app listing request succeeded
 */
int main(int argc, char *argv[], char *envp[])
{
    app_t app;
    int   id;

    iot_clear(&app);

    parse_cmdline(&app, argc, argv, envp);

    mainloop_create(&app);

    if (app.running)
        id = iot_app_list_running(app.iot, list_cb, &app);
    else
        id = iot_app_list_all(app.iot, list_cb, &app);

    if (!id) {
        iot_log_error("Failed to send application list request.");
        exit(1);
    }

    mainloop_run(&app);

    return 0;
}
