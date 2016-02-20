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
#include <errno.h>
#include <stdarg.h>

#include <smpl/macros.h>
#include <smpl/types.h>


void smpl_errmsg(smpl_t *smpl, int error, const char *path, int line,
                 const char *fmt, ...)
{
    va_list ap;
    char    buf[1024], *p;
    int     n, l;

    if (error > 0)
        errno = error;

    if (smpl->errors == NULL)
        return;

    if (smpl_reallocz(*smpl->errors, smpl->nerror, smpl->nerror + 2) == NULL)
        return;

    if (path == NULL && line <= 0) {
        if (smpl->parser != NULL && smpl->parser->in != NULL) {
            path = smpl->parser->in->path;
            line = smpl->parser->in->line;
        }
    }

    p = buf;
    l = sizeof(buf) - 1;

    if (path != NULL) {
        n = snprintf(p, l, "%s:%d: ", path, line);

        if (n < 0)
            goto msgfail;
        if (n >= l)
            goto overflow;

        p += n;
        l -= n;
    }

    va_start(ap, fmt);
    n = vsnprintf(p, l, fmt, ap);
    va_end(ap);

    if (n < 0) {
    msgfail:
        snprintf(buf, sizeof(buf), "<unknown error>");
    }
    if (n >= l) {
    overflow:
        p = buf + l;
        p[-3] = '.';
        p[-2] = '.';
        p[-1] = '.';
        p[ 0] = '\0';
    }

    (*smpl->errors)[smpl->nerror++] = smpl_strdup(buf);
}


void smpl_errors_free(char **errors)
{
    char **e;

    if (errors == NULL)
        return;

    for (e = errors; *e != NULL; e++)
        smpl_free(*e);

    smpl_free(errors);
}
