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

#include "generator.h"


const char *config_getstr(generator_t *g, const char *tag)
{
    const char **envp, *p;

    for (envp = g->env; *envp; envp++) {
        p = *envp;

        if (p[0] != 'I')
            continue;

        p++;

        if (strncmp(p, "OT_GENERATOR_", 13))
            continue;

        p += 13;

        while (*p != '=' && *tag)
            p++, tag++;

        if (*p == '=' && !*tag)
            return p + 1;
    }

    return NULL;
}


static void print_usage(generator_t *g, const char *argv0, int exit_code,
                        const char *fmt, ...)
{
    va_list ap;

    IOT_UNUSED(g);

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    fprintf(stderr, "usage: %s [options] early normal late\n\n"
            "The possible opions are:\n"
            "  -n, --dry-run       just a dry run, don't generate anything\n"
            "  -l, --log <PATH>    where to log to\n"
            "  -h, --help          print this help message\n", argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


int config_parse_cmdline(generator_t *g, int argc, char *argv[], char *env[])
{
    typedef enum {
        OPT_INVALID = 0,
        OPT_DRY_RUN,
        OPT_LOG_PATH,
        OPT_HELP,
    } opt_t;
    unsigned char optmap[] = {
#       define IGNORE(min, max) [min ... (max)] = 0
#       define OPTION(val, opt)   [(val)] = (opt)
        IGNORE(0, 'h' - 1),
        OPTION('h', OPT_HELP),
        IGNORE('h' + 1, 'l' - 1),
        OPTION('l', OPT_LOG_PATH),
        IGNORE('l' + 1, 'n' - 1),
        OPTION('n', OPT_DRY_RUN),
        IGNORE('n' + 1, 255)
#       undef IGNORE
#       undef OPTION
    };

#   define OPTIONS "nl:h"
    struct option options[] = {
        { "dry-run", no_argument      , NULL, 'n' },
        { "log"    , required_argument, NULL, 'l' },
        { "help"   , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int opt, help;

    g->env = (const char **)env;
    help = false;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch ((opt_t)optmap[opt]) {
        case 0:
            print_usage(g, argv[0], EINVAL, "invalid argument '%s'", opt);
            break;

        case OPT_DRY_RUN:
            g->dry_run = true;
            break;

        case OPT_LOG_PATH:
            g->log_path = optarg;
            break;

        case OPT_HELP:
            help = true;
            break;
        }
    }

    if (help) {
        print_usage(g, argv[0], -1, "");
        exit(0);
    }

    if (optind + 2 >= argc)
        print_usage(g, argv[0], EINVAL, "Too few arguments.");

    if (optind + 4 < argc)
        print_usage(g, argv[0], EINVAL, "Too many arguments.");

    g->dir_early  = argv[optind];
    g->dir_normal = argv[optind + 1];
    g->dir_late   = argv[optind + 2];
    g->dir_apps   = optind + 3 < argc ? argv[optind + 3] : PATH_APPS;

    g->dir_service = g->dir_normal;

    return 0;
}
