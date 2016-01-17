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
#include <errno.h>
#include <alloca.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "jmpl/jmpl.h"
#include "jmpl/parser.h"


json_t *jmpl_load_json(const char *path)
{
    iot_json_t  *o;
    struct stat  st;
    char        *buf, *p;
    int          fd, n;

    if (stat(path, &st) < 0)
        return NULL;

    if (st.st_size > JMPL_MAX_JSONDATA) {
        errno = EOVERFLOW;
        return NULL;
    }

    buf = alloca(st.st_size + 1);
    fd   = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;

    n = read(fd, buf, st.st_size);
    close(fd);

    if (n != st.st_size)
        return NULL;

    buf[n] = '\0';
    p      = buf;

    if (iot_json_parse_object(&p, &n, &o) < 0)
        return NULL;

    while (*p == ' ' || *p == '\t' || *p == '\n')
        p++;

    if (!*p)
        return o;

    iot_json_unref(o);
    errno = EINVAL;

    return NULL;
}


jmpl_t *jmpl_load_template(const char *path)
{
    jmpl_t      *j;
    struct stat  st;
    char        *buf;
    int          fd, n;

    if (stat(path, &st) < 0)
        return NULL;

    if (st.st_size > JMPL_MAX_TEMPLATE) {
        errno = EOVERFLOW;
        return NULL;
    }

    buf = alloca(st.st_size + 1);
    fd   = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;

    n = read(fd, buf, st.st_size);
    close(fd);

    if (n != st.st_size)
        return NULL;

    buf[n] = '\0';

    j = jmpl_parse(buf);

    return j;
}

