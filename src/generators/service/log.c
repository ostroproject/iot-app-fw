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
#include <fcntl.h>
#include <syslog.h>
#include <sys/stat.h>

#include "generator.h"

static int log_fd = -1;

int log_open(generator_t *g, const char *path)
{
    if (log_fd >= 0)
        return log_fd;

    if (path == NULL)
        path = config_getstr(g, "LOG");
    if (path == NULL)
        path = "/dev/kmsg";

    log_fd = open(path, O_WRONLY | O_NOCTTY | O_CREAT, 0644);

    return log_fd;
}

void log_close(void)
{
    if (log_fd < 0)
        return;

    close(log_fd);
    log_fd = -1;
}

int log_msg(int level, const char *fmt, ...)
{
    char msg[1024], *p;
    va_list ap;
    int l, n;

    IOT_UNUSED(level);

    if (log_fd < 0)
        return 0;

    p = msg;
    l = sizeof(msg);
#if 0
    n = snprintf(p, l, "%c", level);

    if (n < 0)
        return -1;

    p += n;
    l -= n;
#endif

    va_start(ap, fmt);
    n = vsnprintf(p, l, fmt, ap);
    va_end(ap);

    if (n < 0 || n >= l)
        return -1;

    p += n;

    return write(log_fd, msg, p - msg);
}
