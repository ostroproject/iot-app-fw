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
#include <alloca.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/mm.h>

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


void jmpl_close_template(jmpl_t *jmpl)
{
    if (jmpl != NULL) {
        iot_free(jmpl->mtab);
        iot_free(jmpl->buf);
        iot_free(jmpl);
    }
}


int jmpl_substitute(const char *src, json_t *data, const char *dst)
{
    char    path[PATH_MAX], *out, *p;
    jmpl_t *jmpl;
    int     fd, n;
    size_t  l;

    jmpl = jmpl_load_template(src);
    if (jmpl == NULL)
        return -1;
    out = jmpl_eval(jmpl, data);
    jmpl_close_template(jmpl);

    if (out == NULL)
        return -1;

    if (snprintf(path, sizeof(path), "%s.tmp", dst) >= (int)sizeof(path))
        goto nametoolong;

    fd = open(path, O_WRONLY | O_CREAT, 0644);

    if (fd < 0)
        goto ioerror;

    n = 0;
    l = strlen(out);
    p = out;
    while (l > 0) {
        n = write(fd, p, l);

        if (n < 0) {
            if (errno != EAGAIN)
                goto ioerror;
            else
                continue;
        }

        l -= n;
        p += n;
    }

    if (rename(path, dst) < 0)
        goto ioerror;

    close(fd);
    iot_free(out);

    return 0;

 nametoolong:
    errno = ENAMETOOLONG;
    iot_free(out);
    return -1;

 ioerror:
    close(fd);
    unlink(path);
    iot_free(out);

    return -1;
}
