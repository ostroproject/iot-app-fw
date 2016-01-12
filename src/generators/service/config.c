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
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/common/mm.h>
#include "generator.h"


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    fprintf(stderr, "usage: %s [options] normal early late [<apps-dir>]\n"
            "\n"
            "Search <apps-dir> for application manifests and generate a "
            "systemd service\n"
            "file for each application found. The default path for <apps-dir> "
            "is %s.\n"
            "\n"
            "The possible opions are:\n"
            "  -n, --dry-run       just print, don't generate anything\n"
            "  -l, --log <path>    where to log to (default: /dev/kmsg)\n"
            "  -v, --verbose       increase logging verbosity\n"
            "  -d, --debug <site>  enable deugging for <site>\n"
            "  -h, --help          print this help message\n", argv0,
            PATH_APPS);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void set_defaults(generator_t *g, char *env[])
{
    iot_clear(g);
    iot_list_init(&g->services);

    g->env      = (const char **)env;
    g->dir_apps = PATH_APPS;

    iot_log_set_mask(IOT_LOG_MASK_ERROR | IOT_LOG_MASK_WARNING);
}


int config_parse_cmdline(generator_t *g, int argc, char *argv[], char *env[])
{
#   define OPTIONS "nl:vd:h"
    static struct option options[] = {
        { "dry-run", no_argument      , NULL, 'n' },
        { "log"    , required_argument, NULL, 'l' },
        { "verbose", no_argument      , NULL, 'v' },
        { "debug"  , required_argument, NULL, 'd' },
        { "help"   , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;

    set_defaults(g, env);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'n':
            g->dry_run = 1;
            break;

        case 'l':
            g->log_path = optarg;
            break;

        case 'h':
            print_usage(argv[0], 0, "");
            break;

        case 'v': {
            int m, d;

            m = iot_log_get_mask();
            d = m & IOT_LOG_MASK_DEBUG;

            iot_log_set_mask((m << 1) | 0x1);

            if (!d && (iot_log_get_mask() & IOT_LOG_MASK_DEBUG)) {
                iot_debug_enable(true);
                iot_debug_set_config("*");
            }

            break;
        }

        case 'd':
            iot_debug_enable(true);
            iot_debug_set_config(optarg);
            break;

        case '?':
            print_usage(argv[0], EINVAL, "", opt);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid argument '%c'", opt);
            break;
        }
    }

    if (optind + 2 >= argc)
        print_usage(argv[0], EINVAL, "Too few arguments.");

    if (optind + 4 < argc)
        print_usage(argv[0], EINVAL, "Too many arguments.");

    g->dir_normal = argv[optind];
    g->dir_early  = argv[optind + 1];
    g->dir_late   = argv[optind + 2];
    g->dir_apps   = optind + 3 < argc ? argv[optind + 3] : PATH_APPS;

    g->dir_service = g->dir_normal;

    if (g->log_path == NULL)
        g->log_path = g->dry_run ? "/proc/self/fd/1" : "/dev/kmsg";

    return 0;
}
