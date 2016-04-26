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

    fprintf(stderr, "usage: %s [options] normal early late\n"
            "\n"
            "Search for application manifests in the give application or self "
            "directory.\n"
            "If a self directory is found, generate a systemd service or a set "
            "of actions to execute.\n"
            "Otherwise if applicaiton manifests are found, generate a systemd "
            "service file and potential\n"
            "addon services for each application found.\n"
            "file for each application found.\n"
            "\n"
            "The possible opions are:\n"
            "  -c, --config <path>       configuration to load\n"
            "  -T, --templates <dir>     directory to search for templates\n"
            "  -t  --template <name>     name of template to use\n"
            "  -A, --applications <dir>  root directory for applications\n"
            "  -S, --self <dir>          root directory for container app\n"
            "  -m  --manifest <name>     name of manifest to look for\n"
            "  -C, --containers <dir>    container root directory\n"
            "  -n, --dry-run             just print, don't generate anything\n"
            "  -u, --update              process only touched manifests\n"
            "  -l, --log <path>          where to log to (default: /dev/kmsg)\n"
            "  -v, --verbose             increase logging verbosity\n"
            "  -d, --debug <site>        enable deugging for <site>\n"
            "  -h, --help                print this help message\n", argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void set_defaults(generator_t *g, char **argv, char *env[])
{
    iot_clear(g);
    iot_list_init(&g->services);
    iot_list_init(&g->preprocessors);

    g->env             = (const char **)env;
    g->argv0           = argv[0];
    g->path_apps       = PATH_APPS;
    g->path_self       = PATH_SELF;
    g->path_config     = PATH_CONFIG;
    g->path_template   = PATH_TEMPLATE_DIR;
    g->path_containers = PATH_CONTAINER;
    g->name_manifest   = NAME_MANIFEST;

    iot_log_set_mask(IOT_LOG_MASK_ERROR | IOT_LOG_MASK_WARNING);
}


static void update_defaults(generator_t *g, int argc, char **argv, int optind)
{
    int self = self_check_dir(g) > 0;

    if (g->name_template == NULL) {
        if (self)
            g->name_template = "self/"NAME_TEMPLATE;
        else
            g->name_template = "host/"NAME_TEMPLATE;
    }

    if (!self) {
        if (argc != optind + 3)
            print_usage(argv[0], EINVAL, "Need 3 arguments in non-agent mode.");

        g->dir_normal  = argv[optind];
        g->dir_early   = argv[optind + 1];
        g->dir_late    = argv[optind + 2];
        g->dir_service = g->dir_normal;
    }
}


int config_parse_cmdline(generator_t *g, int argc, char *argv[], char *env[])
{
#   define OPTIONS "c:T:A:S:C:t:m:nul:vd:h"
    static struct option options[] = {
        { "config"      , required_argument, NULL, 'c' },
        { "templates"   , required_argument, NULL, 'T' },
        { "applications", required_argument, NULL, 'A' },
        { "containers  ", required_argument, NULL, 'C' },
        { "template"    , required_argument, NULL, 't' },
        { "manifest"    , required_argument, NULL, 'm' },
        { "dry-run"     , no_argument      , NULL, 'n' },
        { "update"      , no_argument      , NULL, 'u' },
        { "log"         , required_argument, NULL, 'l' },
        { "verbose"     , no_argument      , NULL, 'v' },
        { "debug"       , required_argument, NULL, 'd' },
        { "help"        , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;

    set_defaults(g, argv, env);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            g->path_config = optarg;
            break;

        case 'T':
            g->path_template = optarg;
            break;

        case 'A':
            g->path_apps = optarg;
            break;

        case 'S':
            g->path_self = optarg;
            break;

        case 'C':
            g->path_containers = optarg;
            break;

        case 't':
            g->name_template = optarg;
            break;

        case 'm':
            g->name_manifest = optarg;
            break;

        case 'n':
            g->dry_run = 1;
            break;

        case 'u':
            g->update = 1;
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

    if (g->log_path == NULL)
        g->log_path = g->dry_run ? "/proc/self/fd/1" : "/dev/kmsg";

    update_defaults(g, argc, argv, optind);

    return 0;
}


int config_file_load(generator_t *g)
{
    g->cfg = iot_json_load_file(g->path_config);

    if (g->cfg == NULL)
        return errno == ENOENT ? 0 : -1;

    return 0;
}
