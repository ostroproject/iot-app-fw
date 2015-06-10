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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>

#ifdef SYSTEMD_ENABLED
#    include <systemd/sd-daemon.h>
#endif

#include <iot/common/macros.h>
#include <iot/common/log.h>

#include "launcher/iot-launch.h"
#include "launcher/daemon/config.h"
#include "launcher/daemon/valgrind.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

#ifndef IOT_LIBDIR
#    if ULONG_MAX > 0xffffffff
#        define IOT_LIBDIR "/usr/lib64"
#    else
#        define IOT_LIBDIR "/usr/lib"
#    endif
#endif

#ifndef IOT_LIBEXECDIR
#    define IOT_LIBEXECDIR "/usr/libexec"
#endif

static void print_usage(launcher_t *l, const char *argv0, int exit_code,
                        const char *fmt, ...)
{
    va_list ap;

    IOT_UNUSED(l);

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }

    printf("usage: %s [options] [-V [valgrind-path] [valgrind-options]]\n\n"
           "The possible options are:\n"
           "  -L  --launcher=<addr>           launcher socket address\n"
           "  -E  --event=<addr>              event relay socket address\n"
           "  -A  --agent=<path>             cgroup notification agent\n"
           "  -t, --log-target=<target>      log target to use\n"
           "      TARGET is one of stderr,stdout,syslog, or a logfile path\n"
           "  -l, --log-level=<levels>       logging level to use\n"
           "      LEVELS is a comma separated list of info, error and warning\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
           "  -f, --foreground               don't daemonize\n"
           "  -h, --help                     show help on usage\n"
           "  -V, --valgrind                 run through valgrind\n"
#ifdef SYSTEMD_ENABLED
           "  -S, --sockets=var1[,var2]      systemd socket-activation order\n"
           "      var1 and var2 are one of lnc, app\n"
#endif
           , argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void config_set_defaults(launcher_t *l, char *argv0)
{
    static char  agent[PATH_MAX];
    char        *p;

    l->lnc_addr = IOT_LAUNCH_ADDRESS;
    l->app_addr = IOT_APPFW_ADDRESS;
#ifdef SYSTEMD_ENABLED
    l->lnc_fd   = -1;
    l->app_fd   = -1;
#endif

    if (strstr(argv0, "/iot-app-fw/src/iot-launch") ||
        strstr(argv0, "/iot-app-fw/src/.libs/")) {
        iot_log_mask_t saved = iot_log_set_mask(IOT_LOG_MASK_WARNING);
        iot_log_warning("*** Setting defaults for running from source tree...");

        strcpy(agent, argv0);
        p = strstr(agent, "iot-app-fw/src/");
        strcpy(p, "iot-app-fw/src/iot-launch-agent");

        iot_log_warning("launcher path: %s", argv0);
        iot_log_warning("agent path: %s", agent);

        iot_log_set_mask(saved);

        l->cgagent    = agent;
        l->log_mask   = IOT_LOG_UPTO(IOT_LOG_INFO);
        l->log_target = IOT_LOG_TO_STDERR;
        l->foreground = TRUE;
    }
    else {
        l->cgagent    = IOT_LIBEXECDIR"/iot-launcher/io-launch-agent";
        l->log_mask   = IOT_LOG_MASK_ERROR;
        l->log_target = IOT_LOG_TO_STDERR;
        l->foreground = FALSE;
    }

    iot_log_set_mask(l->log_mask);
    iot_log_set_target(l->log_target);

}


static void set_passed_sockets(launcher_t *l, const char *order,
                               const char *argv0)
{
#ifdef SYSTEMD_ENABLED
    const char *b, *e;
    char        key[256];
    int         nfd, i;
    size_t      len;

    nfd = sd_listen_fds(0);

    if (nfd <= 0) {
        iot_log_warning("Looks like we were not socket-activated...");
        return;
    }

    i = 0;
    b = order;
    while (b && *b) {
        while (*b == ',' || *b == ' ' || *b == '\t')
            b++;

        if (!*b)
            return;

        if (i >= nfd)
            return;

        if ((e = strchr(b, ',')) != NULL)
            len = e - b;
        else
            len = strlen(b);

        if (len >= sizeof(key)) {
            print_usage(l, argv0, EINVAL, "socket variables '%s'", order);
        }

        strncpy(key, b, len);
        key[len] = '\0';

        if (!strcmp(key, "launcher") ||
            !strcmp(key, "launch") ||
            !strcmp(key, "lnc")) {
            l->lnc_fd = SD_LISTEN_FDS_START + i;
            iot_log_info("Got socket-activated fd %d for launcher.", l->lnc_fd);
        }
        else if (!strcmp(key, "application") ||
                 !strcmp(key, "appfw") ||
                 !strcmp(key, "app")) {
            l->app_fd = SD_LISTEN_FDS_START + i;
            iot_log_info("Got socket-activated fd %d for appfw.", l->app_fd);
        }
        else {
            print_usage(l, argv0, EINVAL, "socket variable '%s'", key);
        }

        b = e;
        i++;
    }
#else
    print_usage(l, argv[0], EOPNOTSUPP, "socket activation disabled");
#endif
}


static void parse_cmdline(launcher_t *l, int argc, char **argv, char **envp)
{
#define MAX_ARGS 256
#ifdef SYSTEMD_ENABLED
    #define OPT_SOCKETS "S"
#endif

#define OPTIONS "L:A:a:l:t:vd:fhV"OPT_SOCKETS
    struct option options[] = {
        { "launcher"         , required_argument, NULL, 'L' },
        { "appfw"            , required_argument, NULL, 'A' },
        { "agent"            , required_argument, NULL, 'a' },
        { "log-level"        , required_argument, NULL, 'l' },
        { "log-target"       , required_argument, NULL, 't' },
        { "verbose"          , optional_argument, NULL, 'v' },
        { "debug"            , required_argument, NULL, 'd' },
        { "foreground"       , no_argument      , NULL, 'f' },
        { "help"             , no_argument      , NULL, 'h' },
        { "valgrind"         , optional_argument, NULL, 'V' },
#ifdef SYSTEMD_ENABLED
        { "sockets"          , required_argument, NULL, 'S' },
#endif
        { NULL, 0, NULL, 0 }
    };

#   define SAVE_ARG(a) do {                                     \
        if (saved_argc >= MAX_ARGS)                             \
            print_usage(l, argv[0], EINVAL,                     \
                        "too many command line arguments");     \
        else                                                    \
            saved_argv[saved_argc++] = a;                       \
    } while (0)
#   define SAVE_OPT(o)       SAVE_ARG(o)
#   define SAVE_OPTARG(o, a) SAVE_ARG(o); SAVE_ARG(a)
    char *saved_argv[MAX_ARGS];
    int   saved_argc;

    int opt, help;

    help = FALSE;
    saved_argc = 0;
    saved_argv[saved_argc++] = argv[0];

    config_set_defaults(l, argv[0]);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'L':
            SAVE_OPTARG("-L", optarg);
            l->lnc_addr = optarg;
            break;

        case 'A':
            SAVE_OPTARG("-A", optarg);
            l->app_addr = optarg;
            break;

        case 'a':
            SAVE_OPTARG("-a", optarg);
            l->cgagent = optarg;
            break;

        case 'v':
            SAVE_OPT("-v");
            l->log_mask <<= 1;
            l->log_mask  |= 1;
            iot_log_set_mask(l->log_mask);
            break;

        case 'l':
            SAVE_OPTARG("-l", optarg);
            l->log_mask = iot_log_parse_levels(optarg);
            if (l->log_mask < 0)
                print_usage(l, argv[0], EINVAL,
                            "invalid log level '%s'", optarg);
            else
                iot_log_set_mask(l->log_mask);
            break;

        case 't':
            SAVE_OPTARG("-t", optarg);
            l->log_target = optarg;
            iot_log_set_target(l->log_target);
            break;

        case 'd':
            SAVE_OPTARG("-d", optarg);
            l->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

        case 'f':
            SAVE_OPT("-f");
            l->foreground = TRUE;
            break;

        case 'h':
            SAVE_OPT("-h");
            help = TRUE;
            break;

        case 'V':
            valgrind(optarg, argc, argv, optind, saved_argc, saved_argv, envp);
            break;

#ifdef SYSTEMD_ENABLED
        case 'S':
            SAVE_OPTARG("-S", optarg);
            set_passed_sockets(l, optarg, argv[0]);
            break;
#endif

        default:
            print_usage(l, argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(l, argv[0], -1, "");
        exit(0);
    }
}


void config_parse(launcher_t *l, int argc, char **argv, char **envp)
{
    parse_cmdline(l, argc, argv, envp);
}
