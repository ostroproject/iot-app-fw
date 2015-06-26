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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>
#include <iot/common/json.h>
#include <iot/common/utils.h>

#include "launcher/iot-launch.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif


/*
 * launcher runtime context
 */

typedef struct {
    iot_mainloop_t    *ml;               /* mainloop */
    iot_transport_t   *t;                /* transport to privileged */
    const char        *addr;             /* address we listen on */
    const char        *appid;            /* application id */
    const char        *argv0;            /* us... */
    int                argc;             /* number of arguments for exec */
    char             **argv;             /* arguments for exec */

    int                log_mask;         /* what to log */
    const char        *log_target;       /* where to log it to */
    int                agent;            /* running as cgroup release agent */
    int                cleanup;          /* whether launching or cleaning up */
    iot_sighandler_t  *sh_int;           /* SIGINT handler */
    iot_sighandler_t  *sh_term;          /* SIGHUP handler */
} launcher_t;


/*
 * command line processing
 */

static void print_usage(launcher_t *l, int exit_code, const char *fmt, ...)
{
    va_list ap;
    const char *base;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }

    if ((base = strrchr(l->argv0, '/')) != NULL) {
        base++;
        if (!strncmp(base, "lt-", 3))
            base += 3;
    }
    else
        base = l->argv0;

    printf("usage:\n");
    printf("  %s [options] --appid <app> [-- args]\n", base);
    printf("  %s [options] [--cleanup] <cgroup-path>\n\n", base);
    printf("The possible options are:\n"
           "  -s, --server=<SERVER>          server transport address\n"
           "  -l, --log-level=<LEVELS>       logging level to use\n"
           "      LEVELS is a comma separated list of info, error and warning\n"
           "  -t, --log-target=<TARGET>      log target to use\n"
           "      TARGET is one of stderr,stdout,syslog, or a logfile path\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
           "  -h, --help                     show (this) help on usage\n\n");

    printf("In first (--appid) mode, the application corresponding to the\n"
           "given appid will be launched with the given arguments. In the\n"
           "second (--cleanup) mode, the environment for the application\n"
           "corresponding to the given cgroup path will be cleaned up.\n");

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}

static void config_set_defaults(launcher_t *l, char *argv0)
{
    l->argv0 = argv0;
    l->addr  = IOT_LAUNCH_ADDRESS;

    if (strstr(argv0, "/src/iot-launch") != NULL ||
        strstr(argv0, "/src/.libs/lt-iot-launch") != NULL) {
        iot_log_mask_t saved = iot_log_set_mask(IOT_LOG_MASK_WARNING);
        iot_log_warning("*** Setting defaults for running from source tree...");
        iot_log_set_mask(saved);

        l->log_mask   = IOT_LOG_UPTO(IOT_LOG_INFO);
        l->log_target = IOT_LOG_TO_STDERR;
    }
    else {
        l->log_mask   = IOT_LOG_MASK_ERROR;
        l->log_target = IOT_LOG_TO_STDERR;
    }

    l->agent = strstr(argv0, "iot-launch-agent") == NULL ? FALSE : TRUE;
}


static void parse_cmdline(launcher_t *l, int argc, char **argv, char **envp)
{
#   define OPTIONS "s:a:cl:t:vd:h"
    struct option options[] = {
        { "server"           , required_argument, NULL, 's' },
        { "appid"            , required_argument, NULL, 'a' },
        { "cleanup"          , no_argument      , NULL, 'c' },
        { "log-level"        , required_argument, NULL, 'l' },
        { "log-target"       , required_argument, NULL, 't' },
        { "verbose"          , optional_argument, NULL, 'v' },
        { "debug"            , required_argument, NULL, 'd' },
        { "help"             , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt, help;

    IOT_UNUSED(envp);

    config_set_defaults(l, argv[0]);
    iot_log_set_mask(l->log_mask);
    iot_log_set_target(l->log_target);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 's':
            l->addr = optarg;
            break;

        case 'a':
            if (l->agent)
                print_usage(l, EINVAL,
                            "appid (%s) given in release agent mode", optarg);
            l->appid = optarg;
            break;

        case 'c':
            l->cleanup = TRUE;
            break;

        case 'v':
            l->log_mask <<= 1;
            l->log_mask  |= 1;
            iot_log_set_mask(l->log_mask);
            break;

        case 'l':
            l->log_mask = iot_log_parse_levels(optarg);
            if (l->log_mask < 0)
                print_usage(l, EINVAL, "invalid log level '%s'", optarg);
            else
                iot_log_set_mask(l->log_mask);
            break;

        case 't':
            l->log_target = optarg;
            break;

        case 'd':
            l->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

        case 'h':
            help++;
            break;

        default:
            print_usage(l, EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(l, -1, "");
        exit(0);
    }

    l->argc = argc - optind;
    l->argv = argv + optind;
}


static void setup_logging(launcher_t *l)
{
    const char *target;

    target = iot_log_parse_target(l->log_target);

    if (!target)
        iot_log_error("invalid log target: '%s'", l->log_target);
    else
        iot_log_set_target(target);
}


static void set_linebuffered(FILE *stream)
{
    fflush(stream);
    setvbuf(stream, NULL, _IOLBF, 0);
}


static void set_nonbuffered(FILE *stream)
{
    fflush(stream);
    setvbuf(stream, NULL, _IONBF, 0);
}


static void signal_handler(iot_sighandler_t *h, int signum, void *user_data)
{
    iot_mainloop_t *ml = iot_get_sighandler_mainloop(h);
    launcher_t     *l  = (launcher_t *)user_data;

    IOT_UNUSED(l);

    switch (signum) {
    case SIGINT:
        iot_log_info("Received SIGINT, exiting...");
        if (ml != NULL)
            iot_mainloop_quit(ml, 0);
        else
            exit(0);
        break;

    case SIGTERM:
        iot_log_info("Received SIGTERM, exiting...");
        iot_mainloop_quit(ml, 0);
        break;
    }
}


static void run_mainloop(launcher_t *l)
{
    iot_mainloop_run(l->ml);
}


static void setup_signals(launcher_t *l)
{
    l->sh_int  = iot_add_sighandler(l->ml, SIGINT , signal_handler, l);
    l->sh_term = iot_add_sighandler(l->ml, SIGTERM, signal_handler, l);
}


static void clear_signals(launcher_t *l)
{
    iot_del_sighandler(l->sh_int);
    iot_del_sighandler(l->sh_term);

    l->sh_int  = NULL;
    l->sh_term = NULL;
}


static int launch_process(launcher_t *l)
{
    char *argv[l->argc + 1];

    memcpy(argv, l->argv, sizeof(l->argv[0]) * l->argc);
    argv[l->argc] = NULL;
    clear_signals(l);

    return execv(argv[0], argv);
}


static void close_connection(launcher_t *l)
{
    iot_transport_disconnect(l->t);
    iot_transport_destroy(l->t);
    l->t = NULL;
}


static void closed_cb(iot_transport_t *t, int error, void *user_data)
{
    launcher_t *l = (launcher_t *)user_data;

    IOT_UNUSED(t);

    if (error != 0)
        iot_log_error("Connection closed with error %d (%s).",
                      error, strerror(error));
    else
        iot_log_info("Connection closed.");

    close_connection(l);
}


static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    launcher_t  *l = (launcher_t *)user_data;
    const char  *type, *error, *sep;
    int          status;
    char         cmd[1024], *p;
    int          i, n, len;

    IOT_UNUSED(t);

    iot_debug("received message: %s", iot_json_object_to_string(msg));

    if (!iot_json_get_string(msg, "type", &type) || strcmp(type, "status") != 0)
        goto malformed;
    if (!iot_json_get_integer(msg, "status", &status))
        goto malformed;

    if (status != 0) {
        if (!iot_json_get_string(msg, "message", &error))
            error = NULL;
        iot_log_error("%s request failed with error %d (%s).",
                      l->cleanup ? "Cleanup" : "Launch",  status,
                      error ? error : "unknown error");
        exit(status);
    }

    if (!l->cleanup) {
        len = sizeof(cmd) - 1;
        sep = "";
        p   = cmd;
        for (i = 0; i < l->argc; i++) {
            n = snprintf(p, len, "%s%s", sep, l->argv[i]);

            if (n < 0 || n >= len)
                break;

            p   += n;
            len -= n;
            sep  = " ";
        }

        iot_log_info("Launching %s (as '%s')...", l->argv[0], cmd);
        exit(launch_process(l));
    }
    else {
        iot_log_info("Cleaning up '%s' done.", l->argv[0]);
        exit(0);
    }

 malformed:
    iot_log_error("Received unknown/malformed reply from the server (%s).",
                  iot_json_object_to_string(msg));
    exit(1);
}


static void setup_transport(launcher_t *l)
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

    len = iot_transport_resolve(NULL, l->addr, &addr, sizeof(addr), &type);

    if (len <= 0) {
        iot_log_error("Failed to resolve transport address for '%s'.", l->addr);

        exit(1);
    }

    flags = IOT_TRANSPORT_MODE_JSON;
    l->t = iot_transport_create(l->ml, type, &evt, l, flags);

    if (l->t == NULL) {
        iot_log_error("Failed to create transport for address '%s'.", l->addr);

        exit(1);
    }

    if (!iot_transport_connect(l->t, &addr, len)) {
        iot_log_error("Failed to connect to transport '%s'.", l->addr);
        iot_transport_destroy(l->t);
        l->t = NULL;

        exit(1);
    }
}


static int send_request(launcher_t *l)
{
    iot_json_t *msg = iot_json_create(IOT_JSON_OBJECT);
    int         status;

    if (!l->cleanup) {
        iot_json_add_string      (msg, "type"   , "setup");
        iot_json_add_integer     (msg, "seqno"  , 1);
        iot_json_add_string      (msg, "appid"  , l->appid);
        iot_json_add_string_array(msg, "command", l->argv, l->argc);
    }
    else {
        iot_json_add_string (msg, "type" , "cleanup");
        iot_json_add_integer(msg, "seqno", 1);
        iot_json_add_string (msg, "path" , l->argv[0]);
    }

    status = iot_transport_sendjson(l->t, msg);
    iot_json_unref(msg);

    if (!status) {
        errno = EIO;
        return -1;
    }
    else
        return 0;
}


void resolve_binary(launcher_t *l)
{
    if (l->appid == NULL) {
        if (l->argc == 0)
            print_usage(l, EINVAL, "neither appid nor binary given to launch");

        l->appid = "unknown";
    }
    else {
        if (l->argc == 0) {
        nobinary:
            iot_log_error("Can't resolve appid (%s) to binary path. This "
                          "feature has not been", l->appid);
            iot_log_error("implemented yet. Please supply the absoute path to "
                          "the binary as the");
            iot_log_error("first argument for the time being...\n");
            exit(1);
        }

        if (*l->argv[0] != '/')
            goto nobinary;
    }
}


void resolve_cgroup(launcher_t *l)
{
    l->cleanup = TRUE;

    if (l->argc != 1) {
        iot_log_error("In cleanup/agent mode, a single cgroup path expected, "
                      "(got %d arguments).", l->argc);
        exit(1);
    }

    if (*l->argv[0] != '/') {
        iot_log_error("In cleanup/agent mode, expecting a cgroup path (got %s).",
                      l->argv[0]);
        exit(1);
    }
}


int main(int argc, char *argv[], char **envp)
{
    launcher_t l;

    iot_clear(&l);
    l.ml = iot_mainloop_create();

    parse_cmdline(&l, argc, argv, envp);

    if (!l.agent && !l.cleanup)
        resolve_binary(&l);
    else
        resolve_cgroup(&l);

    setup_signals(&l);
    setup_logging(&l);
    set_linebuffered(stdout);
    set_nonbuffered(stderr);

    setup_transport(&l);
    send_request(&l);

    run_mainloop(&l);

    return 0;
}

