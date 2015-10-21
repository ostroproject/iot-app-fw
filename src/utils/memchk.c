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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <errno.h>

#define __USE_GNU
#include <fcntl.h>
#include <unistd.h>

#include <iot/common.h>

#include "memsize.h"

typedef struct option_s       option_t;
typedef struct check_s        check_t;
typedef struct entry_def_s    entry_def_t;
typedef struct range_def_s    range_def_t;

struct option_s {
    const char *prognam;
    pid_t pid;
    unsigned int duration;
    unsigned int interval;
    int argc;
    char **argv;
};

struct check_s {
    bool printed;
    option_t *opt;
    iot_mainloop_t *ml;
    iot_memsize_t *mem;
    pid_t child_pid;
};

struct entry_def_s {
    iot_memsize_entry_type_t type;
    const char *name;
};

struct range_def_s {
    uint64_t max;
    double div;
    const char *fmt;
    const char *unit;
};

static option_t *parse_cmdline(int, char **);
static unsigned long int parse_number(const char *, const char *,const char *);
static void print_usage(const char *, int, char *, ...);

static pid_t exec_cmd(int, char **, char **, int *);

static void signal_handler(iot_sighandler_t *, int, void *);
static void check_done_callback(iot_event_watch_t *,uint32_t,int,void *,void*);

static void print_results(check_t *);
static size_t print_memory_footprint(iot_memsize_t *, char *, size_t);
static size_t print_value(size_t, bool, char *, size_t);


int main(int argc, char **argv, char **envp)
{
    option_t *opt = parse_cmdline(argc, argv);
    iot_event_bus_t *evbus = NULL;
    iot_event_watch_t *evwatch = NULL;
    uint32_t evdone;
    iot_mainloop_t *ml;
    iot_sighandler_t *sigint, *sigterm, *sigchld;
    check_t check;
    pid_t pid;
    int syncfd;
    int err;
    char dummy[1];

    if (!(ml = iot_mainloop_create())) {
        iot_log_error("failed to create mainloop");
        exit(errno);
    }

    memset(&check, 0, sizeof(check));
    check.opt = opt;
    check.ml = ml;

    sigint  = iot_add_sighandler(ml, SIGINT,  signal_handler, &check);
    sigterm = iot_add_sighandler(ml, SIGTERM, signal_handler, &check);
    sigchld = iot_add_sighandler(ml, SIGCHLD, signal_handler, &check);

    if (!sigint || !sigterm || !sigchld) {
        iot_log_error("failed to register signal handlers");
        exit(EIO);
    }

    evbus = iot_event_bus_create(ml, IOT_MEMSIZE_EVENT_BUS);
    evdone = iot_event_register(IOT_MEMSIZE_EVENT_DONE);

    if (!evbus || evdone == IOT_EVENT_UNKNOWN) {
        iot_log_error("failed to setup event mechanism");
        exit(EIO);
    }

    if (opt->duration) {
        evwatch = iot_event_add_watch(evbus, evdone,
                                      check_done_callback, &check);
        if (!evwatch) {
            iot_log_error("failed to add event watcher");
            exit(EIO);
        }
    }

    if (!(pid = opt->pid)) {
        pid = exec_cmd(opt->argc, opt->argv, envp, &syncfd);
        check.child_pid = pid;
        read(syncfd, dummy, sizeof(dummy));
    }

    check.mem = iot_memsize_check_start(pid, ml, opt->interval, opt->duration);

    if (!check.mem) {
        iot_log_error("failed to initialize mem.check: %s\n", strerror(errno));
        exit(errno);
    }


    if ((err = iot_mainloop_run(ml)))
        iot_log_error("mainloop failed: %s", strerror(errno));
    else {
        if (iot_memsize_check_stop(check.mem) < 0) {
            iot_log_error("memory checking failed: %s\n", strerror(errno));
            exit(errno);
        }

        print_results(&check);
    }

    return 0;
}

static option_t *parse_cmdline(int argc, char **argv)
{
#define OPTIONS "+p:t:i:h"
#define INVALID "invalid option '%c'", o

    static struct option options[] = {
        { "pid"     ,  required_argument,  NULL,  'p' },
        { "time"    ,  required_argument,  NULL,  't' },
        { "interval",  required_argument,  NULL,  'i' },
	{ "help"    ,  no_argument      ,  NULL,  'h' },
        { NULL      ,  0                ,  NULL,   0  }
    };

    option_t *opt;
    const char *pn;
    int o, i;

    if (!(opt = iot_allocz(sizeof(option_t)))           ||
        !(opt->prognam = pn = iot_strdup(basename(argv[0])))  )
    {
        fprintf(stderr,"failed to allocate memory for options");
        exit(ENOMEM);
    };

    while((o = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (o) {

        case 'p': opt->pid      = parse_number(optarg,pn,"pid");      break;
        case 't': opt->duration = parse_number(optarg,pn,"time");     break;
        case 'i': opt->interval = parse_number(optarg,pn,"interval"); break;

        case 'h': print_usage(pn, 0, NULL);                           exit(0);

        default:  print_usage(pn, EINVAL, INVALID);                   break;
        }
    }

    opt->argc = argc - optind;
    opt->argv = iot_allocz(sizeof(char *) * (opt->argc + 1));

    for (i = 0;  i < opt->argc;  i++) {
        if (!(opt->argv[i] = iot_strdup(argv[optind + i]))) {
	    fprintf(stderr, "failed to allocate memory for options\n");
	    exit(ENOMEM);
	}
    }

    if (!opt->pid && opt->argc == 0)
        print_usage(pn, ENOMEDIUM,"either <pid> or <cmd> should be specified");

    if (opt->pid && opt->argc > 0)
        print_usage(pn, EINVAL, "<pid> and <cmd> are mutually exclusive");

    iot_log_enable(IOT_LOG_UPTO(IOT_LOG_MASK_INFO));

    return opt;

#undef INVALID
#undef OPTIONS
}

static unsigned long int parse_number(const char *str,
                                      const char *prognam,
                                      const char *arg)
{
    unsigned long int number = 0;
    char *e;

    number = strtoul(str, &e, 10);

    if (str == (const char *)e || *e) {
        print_usage(prognam, EINVAL, "invalid value '%s' for option '%s'",
                    str, arg);
    }

    return number;
}

static void print_usage(const char *prognam, int exit_code, char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
        va_end(ap);

	fprintf(stderr, "\n\n");
    }

    fprintf(stderr, "usage:\n"
        "  %s -p <pid> [-t <time>] [-i <interval>]\n"
        "  %s [-i <interval>] <cmd> [<options> ...]\n"
        "  %s -h\n"
	"where\n"
	"  -p <pid>      or --pid=<pid>            process to check\n"
        "  -t <time>     or --time=<time>          duration of checking\n"
 	"  -i <interval> or --interval=<interval>  sampling interval in ms\n"
        "  <cmd> and <options>                     command to execute\n"
        "use of <pid> and <cmd> is mutually exclusive\n",
        prognam, prognam, prognam);

    if (exit_code >= 0)
        exit(exit_code);

    return;
}

static pid_t exec_cmd(int argc, char **argv, char **envp, int *syncfd)
{
    pid_t pid;
    int pipefd[2] = { -1, -1 };

    IOT_ASSERT(argv[0], "internal error: no command to execute");
    IOT_ASSERT(argv[argc] == NULL, "internal error: invalid argv");

    if (syncfd) {
        if (pipe2(pipefd, O_CLOEXEC) < 0) {
            iot_log_error("can-t create syncing pipe: %s", strerror(errno));
            exit(errno);
        }
    }

    if ((pid = fork()) == (pid_t)-1) {
        iot_log_error("failed to fork: %s", strerror(errno));
        exit(errno);
    }

    if (!pid) {
        char buf[16];

        if (pipefd[0] >= 0)
            close(pipefd[0]);

        execvpe(argv[0], argv, envp);

        iot_log_error("failed to exec '%s': %s", argv[0], strerror(errno));

        exit(errno);
    }

    if (syncfd) {
        if (pipefd[1] >= 0)
            close(pipefd[1]);
        *syncfd = pipefd[0];
    }

    return pid;
}

static void signal_handler(iot_sighandler_t *h, int signum, void *userdata)
{
    check_t *check = (check_t *)userdata;
    int status;

    IOT_UNUSED(h);

    if (check) {
        switch (signum) {

        case SIGCHLD:
            if (check->child_pid &&
                waitpid(check->child_pid,&status,WNOHANG) == check->child_pid)
            {
                iot_memsize_check_stop(check->mem);
                iot_mainloop_quit(check->ml, 0);
            }
            return;

        case SIGTERM:
            check->printed = true;
        case SIGINT:
            iot_memsize_check_stop(check->mem);
            if (check->child_pid)
                kill(check->child_pid, signum);
            else
                iot_mainloop_quit(check->ml, 0);
            return;

        default:
            break;
        }
    }

    iot_log_error("got bogus signal %d", signum);
}

static void check_done_callback(iot_event_watch_t *evwatch,
                                uint32_t evid,
                                int format,
                                void *data,
                                void *userdata)
{
    iot_memsize_t *mem = (iot_memsize_t *)data;
    check_t *check = (check_t *)userdata;
    const char *evname;
    char buf[8192];

    IOT_UNUSED(evwatch);
    IOT_UNUSED(format);

    evname = iot_event_name(evid);

    if (!check || !check->mem || !mem || strcmp(evname,IOT_MEMSIZE_EVENT_DONE))
        iot_log_error("got spurious event '%s'", evname);
    else {
        IOT_ASSERT(mem == check->mem, "confused with mem");

        if (check->opt->pid)
            iot_mainloop_quit(check->ml, 0);
        else
            print_results(check);
    }
}

static void print_results(check_t *check)
{
    iot_memsize_t *mem;
    char *p, *e;
    char buf[8192];

    if ((mem = check->mem) && !check->printed) {
        check->printed = true;

        e = (p = buf) + sizeof(buf);

        p += snprintf(p, e-p, "\nmemory footprint of '%s' calculated\n"
                      "from %u samples taken in %.2lf second\n\n",
                      iot_memsize_exe(mem),
                      iot_memsize_samples(mem),
                      iot_memsize_duration(mem));

        if (p < e)
            p += print_memory_footprint(mem, p, e-p);

        printf("%s", buf);
    }
}

static size_t print_memory_footprint(iot_memsize_t *mem, char *buf, size_t len)
{
    static entry_def_t defs[] = {
        { IOT_MEMSIZE_TOTAL,     "total"    },
        { IOT_MEMSIZE_RESIDENT,  "resident" },
        { IOT_MEMSIZE_SHARE,     "share"    },
        { IOT_MEMSIZE_TEXT,      "text"     },
        { IOT_MEMSIZE_DATA,      "data"     },
        {      -1,               NULL       }
    };
    static char seplin[] = "+-----------+--------+--------+--------+\n";
    static char header[] = "+ footprint +   min  +  mean  +   max  +\n";

    entry_def_t *d;
    iot_memsize_entry_t en;
    char *p, *e;

    e = (p = buf) + len;

    if (p < e)
        p += snprintf(p, e-p, seplin);

    if (p < e)
        p += snprintf(p, e-p, header);

    if (p < e)
        p += snprintf(p, e-p, seplin);


    for (d = defs;  d->name;   d++) {
        if (!iot_memsize(mem, d->type, &en)) {
            iot_log_error("could not get '%s' footprint: %s",
                          d->name, strerror(errno));
            break;
        }

        if (p >= e) break;
        p += snprintf(p, e-p, "| %9s ", d->name);

        if (p >= e) break;
        p += print_value(en.min, false, p, e-p);

        if (p >= e) break;
        p += print_value(en.mean, false, p, e-p);

        if (p >= e) break;
        p += print_value(en.max, true, p, e-p);
    }

    if (p < e)
        p += snprintf(p, e-p, seplin);

    return p - buf;
}


static size_t print_value(size_t value, bool last, char *buf, size_t len)
{
    static range_def_t ranges[] = {
        {             1000ULL,             1.0, "| %3.0lf  %s %s", " "  },
        {          1000000ULL,          1024.0, "| %5.1lf%s %s"  , "K"  },
        {       1000000000ULL,       1048576.0, "| %5.1lf%s %s"  , "M"  },
        {    1000000000000ULL,    1073741824.0, "| %5.1lf%s %s"  , "G"  },
        { 1000000000000000ULL, 1099511627776.0, "| %5.1lf%s %s"  , "T"  },
        {                0ULL,             0.0,        NULL      , NULL }
    };

    uint64_t v64;
    double dv;
    range_def_t *r;
    char *sep;

    v64 = value;
    sep = last ? "|\n" : "";

    for (r = ranges;  r->div > 0.0;  r++) {
        if (v64 < r->max) {
            dv = (double)value / r->div;
            return snprintf(buf, len, r->fmt, dv, r->unit, sep);
        }
    }

    return snprintf(buf, len, "|   -    %s", sep);
}
