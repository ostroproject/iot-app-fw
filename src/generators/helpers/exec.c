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
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/common/mm.h>

typedef struct {
    char   *dhcp;                        /* start up DHCP client */
    char   *user;                        /* user to switch to */
    char  **groups;                      /* groups to switch to */
    int     nexe;                        /* number of executables */
    char  **argv;                        /* executable argv */
    char  **env;
    int     dry_run : 1;
} exec_t;


static void print_usage(exec_t *e, const char *argv0, int exit_code,
                        const char *fmt, ...)
{
    va_list ap;

    IOT_UNUSED(e);

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    fprintf(stderr, "usage: %s [options] early normal late\n\n"
            "The possible opions are:\n"
            "  -n, --dry-run       just a dry run, don't generate anything\n"
            "  -h, --help          print this help message\n", argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


int parse_cmdline(exec_t *e, int argc, char *argv[], char *env[])
{
#   define OPTIONS "d::u:nh"
    struct option options[] = {
        { "dhcp"   , optional_argument, NULL, 'd' },
        { "user"   , required_argument, NULL, 'u' },
        { "dry-run", no_argument      , NULL, 'n' },
        { "help"   , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int opt, help, i, ac;
    char *p, **av;

    e->env = (char **)env;
    help = false;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            if (optarg)
                e->dhcp = optarg;
            else {
                if (access(p = "/sbin/udhcpc"      , X_OK) == 0 ||
                    access(p = "/usr/sbin/dhclient", X_OK) == 0)
                    e->dhcp = p;
                else
                    e->dhcp = "dhclient";
            }
            break;

        case 'n':
            e->dry_run = true;
            break;

        case 'h':
            help = true;
            break;

        default:
            print_usage(e, argv[0], EINVAL, "invalid argument '%s'", opt);
            break;
        }
    }

    if (help) {
        print_usage(e, argv[0], -1, "");
        exit(0);
    }

    ac = 0;
    av = NULL;
    for (i = optind; i < argc; i++) {
        if (!iot_reallocz(e->argv, ac, ac + 1))
            goto nomem;

        if (argv[i][0] == ';' && argv[i][1] == '\0')
            goto nullterm;

        av = e->argv + ac++;

        if (!(*av = iot_strdup(argv[i])))
            goto nomem;

        if ((p = strrchr(*av, ';')) != NULL) {
            if (p[1] == '\0')
                *p = '\0';

        nullterm:
            e->nexe++;
            if (!iot_reallocz(e->argv, ac, ac + 1))
                goto nomem;

            av = e->argv + ac++;
        }
        else
            if (i == argc - 1)
                goto nullterm;
    }

    return 0;

 nomem:
    exit(1);
}


int exec_dhcp(exec_t *e)
{
    char *argv[] = { e->dhcp, NULL, NULL, NULL };
    pid_t pid;

    if (e->dhcp == NULL)
        return 0;

    if (strstr(e->dhcp, "udhcpc")) {       /* grrr... */
        argv[1] = "-i";
        argv[2] = "host0";
    }

    if (!e->dry_run) {
        switch ((pid = fork())) {
        case -1:
            return -1;
        case 0:
            printf("Starting '%s'...\n", argv[0]);
            exit(execvp(argv[0], argv));
            break;
        default:
            break;
        }
    }
    else
        printf("should run '%s'\n", argv[0]);

    return 0;
}


int exec_others(exec_t *e)
{
    char **argv, **next;
    const char *t;
    pid_t pid;
    int i;

    argv = e->argv;
    for (i = 0; i < e->nexe; i++) {
        next = argv;

        while (*next != NULL)
            next++;

        if (!e->dry_run) {
            if (i == e->nexe - 1)
                goto exec;
            switch ((pid = fork())) {
            case -1:
                return -1;
            case 0:
            exec:
                printf("Starting '%s'...\n", argv[0]);
                exit(execvp(argv[0], argv));
                break;
            default:
                break;
            }
        }
        else {
            next = argv;

            printf("should run '");
            t = "";
            while (*next != NULL) {
                printf( "%s%s", t, *next++);
                t = " ";
            }
            printf("'\n");
        }

        argv = next + 1;
    }

    return 0;
}


int main(int argc, char *argv[], char *envp[])
{
    exec_t exec, *e;

    iot_clear(&exec);
    e = &exec;

    parse_cmdline(e, argc, argv, envp);

    exec_dhcp(e);
    exec_others(e);

    return 0;
}
