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
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <iot/common.h>

#include "memsize.h"

typedef struct mem_measure_s  mem_measure_t;

struct mem_measure_s {
    size_t size;                /* total program size ( VmSize ) */
    size_t resident;            /* resident set size  ( VmRSS  ) */
    size_t share;               /* shared pages                  */
    size_t text;                /* text (code)                   */
    size_t data;                /* data + stack                  */
};

struct iot_memsize_s {
    iot_mainloop_t *ml;
    iot_timer_t *tm;
    const char *exe;
    int fd;
    int err;
    mem_measure_t min;
    mem_measure_t max;
    struct {
        mem_measure_t sum;
        size_t samples;
    } mean;
    uint64_t period;
    uint64_t end;
};

static bool is_inactive(iot_memsize_t *);
static void stop_checking(iot_memsize_t *);
static bool read_cmdline(pid_t, char *, size_t);
static void measure_memory_usage(iot_memsize_t *);
static uint64_t get_current_time(void);
static void timer_callback(iot_timer_t *, void *);


static size_t page_size;


iot_memsize_t *iot_memsize_check_start(pid_t pid,
                                       iot_mainloop_t *ml,
                                       unsigned int interval,
                                       unsigned int duration)
{
    iot_memsize_t *mem = 0;
    char cmdline[8192];
    char path[1024];
    const char *exe = NULL;
    int fd = -1;
    iot_timer_t *tm = NULL;
    uint64_t now;

    if (!pid)
        pid = getpid();

    if (!page_size)
        page_size = sysconf(_SC_PAGESIZE);

    if (!read_cmdline(pid, cmdline, sizeof(cmdline)) ||
        !(exe = iot_strdup(cmdline))                  )
        goto failed;

    snprintf(path, sizeof(path), "/proc/%u/statm", pid);

    if ((fd = open(path, O_RDONLY)) < 0)
        goto failed;

    if (!(mem = iot_allocz(sizeof(iot_memsize_t))))
        goto failed;

    if (ml && interval) {
        if (!(tm = iot_add_timer(ml, interval, timer_callback, (void *)mem)))
            goto failed;
    }

    if (!(now = get_current_time()))
        goto failed;

    mem->ml = ml;
    mem->tm = tm;
    mem->exe = exe;
    mem->fd = fd;
    mem->period = now;

    memset(&mem->min, 0xff, sizeof(mem->min));

    if (duration)
        mem->end = now + (uint64_t)duration;

    measure_memory_usage(mem);

    return mem;

 failed:
    iot_free((void *)exe);

    if (fd >= 0)
        close(fd);

    iot_del_timer(tm);

    iot_free((void *)mem);

    return NULL;
}

int iot_memsize_check_sample(iot_memsize_t *mem)
{
    if (!is_inactive(mem))
        measure_memory_usage(mem);

    return mem->err ? -1 : 0;
}

int iot_memsize_check_stop(iot_memsize_t *mem)
{
    uint64_t now;
    bool inactive;
    int err = EINVAL;

    if (mem) {
        inactive = is_inactive(mem);

        stop_checking(mem);

        if (inactive)
            err = mem->err;
        else {
            if (!(now = get_current_time()))
                err = errno;
            else {
                IOT_ASSERT(now >= mem->period, "confused with time");

                err = mem->err;

                mem->period = now - mem->period;
            }
        }
    }

    if (err)
        errno = err;

    return err ? -1 : 0;
}

const char *iot_memsize_exe(iot_memsize_t *mem)
{
    if (!mem || !mem->exe || !mem->exe[0])
        return "???";

    return mem->exe;
}

size_t iot_memsize_samples(iot_memsize_t *mem)
{
    if (!mem || !is_inactive(mem))
        return 0;

    return mem->mean.samples;
}

double iot_memsize_duration(iot_memsize_t *mem)
{
    if (!mem || !is_inactive(mem))
        return 0.0;

    return (double)mem->period / 1000.0;
}

iot_memsize_entry_t *iot_memsize(iot_memsize_t *mem,
                                 iot_memsize_entry_type_t type,
                                 iot_memsize_entry_t *entry)
{
#define FILL_ENTRY_WITH(n)                                               \
    do {                                                                 \
        entry->min = mem->min.n * page_size;                             \
        entry->mean = (mem->mean.sum.n / mem->mean.samples) * page_size; \
        entry->max = mem->max.n * page_size;                             \
    } while(0)

    bool freeit = false;

    if (!mem) {
        errno = EINVAL;
        return NULL;
    }

    if (!entry) {
        if (!(entry = iot_allocz(sizeof(iot_memsize_entry_t))))
            return NULL;
        freeit = true;
    }


    switch (type) {

    case IOT_MEMSIZE_TOTAL:      FILL_ENTRY_WITH(size);        break;
    case IOT_MEMSIZE_RESIDENT:   FILL_ENTRY_WITH(resident);    break;
    case IOT_MEMSIZE_SHARE:      FILL_ENTRY_WITH(share);       break;
    case IOT_MEMSIZE_TEXT:       FILL_ENTRY_WITH(text);        break;
    case IOT_MEMSIZE_DATA:       FILL_ENTRY_WITH(data);        break;

    default:
        if (freeit)
            iot_free((void *)entry);
        errno = EINVAL;
        return NULL;
    }


    return entry;

#undef FILL_ENTRY_WITH
}


void iot_memsize_free(iot_memsize_t *mem)
{
    if (mem) {
        stop_checking(mem);
        iot_free((void *)mem->exe);
        iot_free(mem);
    }
}

static bool is_inactive(iot_memsize_t *mem)
{
    return (!mem || (mem->fd < 0 && !mem->tm));
}

static void stop_checking(iot_memsize_t *mem)
{
    if (mem) {
        if (mem->fd >= 0) {
            close(mem->fd);
            mem->fd = -1;
        }

        if (mem->tm) {
            iot_del_timer(mem->tm);
            mem->tm = NULL;
        }
    }
}

static bool read_statm(int fd, char *buf, size_t count)
{
    ssize_t len;

    if (fd < 0) {
        errno = EIO;
        return false;
    }

    if (lseek(fd, SEEK_SET, 0) == (off_t)-1)
        return false;

    for (;;) {
        len = read(fd, buf, count-1);

        if (len >= 0)
            break;

        if (errno != EINTR)
            return false;
    }

    buf[len] = 0;

    return true;
}

static bool read_cmdline(pid_t pid, char *buf, size_t count)
{
    int fd;
    ssize_t len;
    char path[8192];

    *buf = 0;

    snprintf(path, sizeof(path), "/proc/%u/cmdline", pid);

    if ((fd = open(path, O_RDONLY)) < 0)
        return false;

    for (;;) {
        len = read(fd, buf, count-1);

        if (len >= 0)
            break;

        if (errno != EINTR)
            return false;
    }

    buf[len] = 0;

    return true;
}

static bool get_value(char **buf, size_t *value, char sep)
{
    char *p = *buf;
    char *e;
    unsigned long int v;

    v = strtoul(p, &e, 10);

    if (e <= p || *e != sep)
        return false;

    *buf = e + (sep ? 1 : 0);
    *value = v;

    return true;
}

static void measure_memory_usage(iot_memsize_t *mem)
{
#define UPDATE(s,v,r)   if (v r mem->s.v) mem->s.v = v
#define SET_MIN(v)      UPDATE(min, v, <)
#define SET_MAX(v)      UPDATE(max, v, >)
#define SET_MEAN(v)     mem->mean.sum.v += v

    size_t size, resident, share, text, lib, data, dt;
    char *p;
    char buf[256];

    if (!mem || mem->fd < 0)
        return;

    p = buf;

    if (!read_statm(mem->fd, buf, sizeof(buf)) ||
        !get_value(&p, &size,     ' ' )        ||
        !get_value(&p, &resident, ' ' )        ||
        !get_value(&p, &share,    ' ' )        ||
        !get_value(&p, &text,     ' ' )        ||
        !get_value(&p, &lib,      ' ' )        ||
        !get_value(&p, &data,     ' ' )        ||
        !get_value(&p, &dt,       '\n')        ||
        lib != 0 || dt != 0                     )
    {
        mem->err = EINVAL;
        stop_checking(mem);
    }
    else {
        SET_MIN(size);
        SET_MIN(resident);
        SET_MIN(share);
        SET_MIN(text);
        SET_MIN(data);

        SET_MAX(size);
        SET_MAX(resident);
        SET_MAX(share);
        SET_MAX(text);
        SET_MAX(data);

        SET_MEAN(size);
        SET_MEAN(resident);
        SET_MEAN(share);
        SET_MEAN(text);
        SET_MEAN(data);

        mem->mean.samples++;
    }

#undef SET_MEAN
#undef SET_MAX
#undef SET_MIN
#undef UPDATE
}

static uint64_t get_current_time(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) < 0)
        return 0ULL;

    return (uint64_t)(tv.tv_sec * 1000) + (uint64_t)(tv.tv_usec / 1000);
}

static void timer_callback(iot_timer_t *tm, void *user_data)
{
    static iot_event_flag_t evflags = IOT_EVENT_ASYNCHRONOUS  |
                                      IOT_EVENT_FORMAT_CUSTOM ;

    iot_memsize_t *mem = (iot_memsize_t *)user_data;
    iot_event_bus_t *evbus;
    uint32_t evid;

    IOT_ASSERT(tm, "invalid argument");
    IOT_ASSERT(tm == mem->tm, "confused with data structures");

    if (mem->end) {
        if (get_current_time() > mem->end) {
            iot_memsize_check_stop(mem);

            evbus = iot_event_bus_get(mem->ml, IOT_MEMSIZE_EVENT_BUS);
            evid = iot_event_id(IOT_MEMSIZE_EVENT_DONE);

            if (!evbus || evid == IOT_EVENT_UNKNOWN) {
                iot_log_error("failed to connect to event bus");
                return;
            }

            if (iot_event_emit(evbus, evid, evflags, mem) < 0) {
                iot_log_error("failed to deliver '%s' event on bus'%s': %s",
                              IOT_MEMSIZE_EVENT_DONE, IOT_MEMSIZE_EVENT_BUS,
                              strerror(errno));
                return;
            }

            return;
        }
    }

    measure_memory_usage(mem);
}
