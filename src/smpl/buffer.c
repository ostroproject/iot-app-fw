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
#include <stdarg.h>

#include <smpl/macros.h>
#include <smpl/types.h>


smpl_buffer_t *buffer_create(int size)
{
    smpl_buffer_t *b;

    b = smpl_alloct(typeof(*b));

    if (b == NULL)
        goto nomem;

    smpl_list_init(&b->hook);

    b->buf = b->p = smpl_allocz(size);

    if (b->buf == NULL)
        goto nomem;

    b->size = size;
    return b;

 nomem:
    smpl_free(b);
    return NULL;
}


void buffer_destroy(smpl_buffer_t *b)
{
    if (b == NULL)
        return;

    smpl_list_delete(&b->hook);
    smpl_free(b->buf);
    smpl_free(b);
}


char *buffer_alloc(smpl_list_t *bufs, int size)
{
    smpl_buffer_t *b;
    smpl_list_t   *p, *n;
    int            l;
    char          *buf;

    b = NULL;

    if (bufs != NULL) {
        smpl_list_foreach(bufs, p, n) {
            b = smpl_list_entry(p, typeof(*b), hook);
            l = b->size - (b->p - b->buf);

            if (l < size)
                b = NULL;
            else
                break;
        }
    }

    if (b == NULL) {
        l = size < SMPL_BUFFER_SIZE ? SMPL_BUFFER_SIZE : size + SMPL_BUFFER_SIZE;
        b = buffer_create(l);

        if (b == NULL)
            goto nomem;

        if (bufs != NULL)
            smpl_list_append(bufs, &b->hook);
    }

    buf   = b->p;
    b->p += size;

    return buf;

 nomem:
    smpl_free(b);
    return NULL;
}


int buffer_printf(smpl_buffer_t *b, const char *fmt, ...)
{
    va_list ap;
    int     l, n, d;

 print:
    va_start(ap, fmt);
    l = b->size - (b->p - b->buf);
    n = vsnprintf(b->p, l, fmt, ap);
    va_end(ap);

    if (n + 1 > l) {
        d = n + 1 - l;

        if (d < SMPL_BUFFER_SIZE)
            d = SMPL_BUFFER_SIZE;
        else
            d = SMPL_BUFFER_SIZE + n + 1;

        n = b->p - b->buf;

        if (smpl_reallocz(b->buf, b->size, b->size + d) == NULL)
            goto nomem;

        b->p     = b->buf + n;
        b->size += d;

        goto print;
    }

    b->p += n;

    return 0;

 nomem:
    return -1;
}


char *buffer_steal(smpl_buffer_t *b)
{
    char *buf = b->buf;

    b->buf  = b->p = NULL;
    b->size = 0;

    return buf;
}


void buffer_purge(smpl_list_t *bufs)
{
    smpl_buffer_t *b;
    smpl_list_t   *p, *n;

    smpl_list_foreach(bufs, p, n) {
        b = smpl_list_entry(p, typeof(*b), hook);
        buffer_destroy(b);
    }
}
