/*
 * Copyright (c) 2016, Intel Corporation
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

#include <smpl/macros.h>
#include <smpl/smpl.h>


typedef struct {
    smpl_t      *smpl;
    smpl_data_t *data;
    int          dump;
    const char  *path_template;
    const char  *path_data;
} smpl_test_t;


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    fprintf(stderr, "usage: %s [options] template-file JSON-file\n"
            "\n"
            "Instantiate <template-file> with <JSON-file> and print it.\n"
            "\n"
            "The possible options are:\n"
            "  -v, --verbose       increase logging verbosity\n"
            "  -d, --debug <site>  enable debugging for <site>\n"
            "  -D, --dump          dump internal jmpl data for debugging\n"
            "  -h, --help          print (this) help on usage\n", argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void parse_cmdline(smpl_test_t *t, int argc, char *argv[])
{
#define OPTIONS "vd:Dh"
    static struct option options[] = {
        { "verbose" , no_argument      , NULL, 'v' },
        { "debug"   , required_argument, NULL, 'd' },
        { "debug"   , required_argument, NULL, 'd' },
        { "dump"    , no_argument      , NULL, 'D' },
        { "help"    , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    int opt;

    smpl_clear(t);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'v': {
            int m, d;

            m = smpl_log_get_mask();
            d = m & SMPL_LOG_MASK_DEBUG;

            smpl_log_set_mask((m << 1) | 0x1);

            if (!d && (smpl_log_get_mask()) & SMPL_LOG_MASK_DEBUG) {
                smpl_debug_enable(true);
                smpl_debug_set("*");
            }

            break;
        }

        case 'd':
            smpl_debug_enable(true);
            smpl_debug_set(optarg);
            break;

        case 'D':
            t->dump = true;
            break;

        case 'h':
            print_usage(argv[0], 0, "");
            break;

        case '?':
            print_usage(argv[0], EINVAL, "invalid argument '%c'", optopt);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid argument '%c'", opt);
            break;
        }
    }

    if (argc - optind != 2)
        print_usage(argv[0], EINVAL, "");

    t->path_template = argv[optind];
    t->path_data     = argv[optind + 1];
}



int main(int argc, char *argv[])
{
    char        *str, **errors;
    smpl_test_t  t;

    parse_cmdline(&t, argc, argv);

    t.data = smpl_load_data(t.path_data, &errors);

    if (t.data == NULL) {
        smpl_error("Failed to load data file '%s'.", t.path_data);
        goto dump_errors;
    }

    t.smpl = smpl_load_template(t.path_template, &errors);

    if (t.smpl == NULL) {
        smpl_error("Failed to load template file '%s'.", t.path_template);
        goto dump_errors;
    }

    smpl_dump_template(t.smpl, fileno(stdout));

    str = smpl_evaluate(t.smpl, t.data, &errors);

    if (str == NULL) {
        smpl_error("Failed to evaluate template file '%s' with data '%s'.",
                   t.path_template, t.path_data);
        goto dump_errors;
    }

    printf("Template '%s' evaluated with data '%s' produced:\n%s\n",
           t.path_template, t.path_data, str);

    return 0;

 dump_errors:
    if (errors != NULL) {
        int i;

        for (i = 0; errors[i] != NULL; i++)
            smpl_error("%s", errors[i]);

        smpl_errors_free(errors);
    }
    exit(1);
}
