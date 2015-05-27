/*
 * Copyright (c) 2012, Intel Corporation
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

#include <endian.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/fragbuf.h>

struct iot_fragbuf_s {
    void *data;                          /* actual data buffer */
    int   size;                          /* size of the buffer */
    int   used;                          /* amount of data in the bufer */
    int   framed : 1;                    /* whether data is framed */
};


static void *fragbuf_ensure(iot_fragbuf_t *buf, size_t size)
{
    int more;

    if (buf->size - buf->used < (int)size) {
        more = size - (buf->size - buf->used);

        if (iot_reallocz(buf->data, buf->size, buf->size + more) == NULL)
            return NULL;
        else
            buf->size += more;
    }

    return buf->data + buf->used;
}


size_t iot_fragbuf_used(iot_fragbuf_t *buf)
{
    return buf->used;
}


size_t iot_fragbuf_missing(iot_fragbuf_t *buf)
{
    void     *ptr;
    int       offs;
    uint32_t  size;

    if (!buf->framed || !buf->used)
        return 0;

    /* find the last frame */
    ptr  = buf->data;
    offs = 0;
    while (offs < buf->used) {
        size  = be32toh(*(uint32_t *)ptr);
        offs += sizeof(size) + size;
    }

    /* get the amount of data missing */
    return offs - buf->used;
}


int fragbuf_init(iot_fragbuf_t *buf, int framed, int pre_alloc)
{
    buf->data   = NULL;
    buf->size   = 0;
    buf->used   = 0;
    buf->framed = framed;

    if (pre_alloc <= 0 || fragbuf_ensure(buf, pre_alloc))
        return TRUE;
    else
        return FALSE;
}


iot_fragbuf_t *iot_fragbuf_create(int framed, size_t pre_alloc)
{
    iot_fragbuf_t *buf;

    buf = iot_allocz(sizeof(*buf));

    if (buf != NULL) {
        if (fragbuf_init(buf, framed, pre_alloc))
            return buf;

        iot_free(buf);
    }

    return NULL;
}


void iot_fragbuf_reset(iot_fragbuf_t *buf)
{
    if (buf != NULL) {
        iot_free(buf->data);
        buf->data = NULL;
        buf->size = 0;
        buf->used = 0;
    }
}

void iot_fragbuf_destroy(iot_fragbuf_t *buf)
{
    if (buf != NULL) {
        iot_free(buf->data);
        iot_free(buf);
    }
}


void *iot_fragbuf_alloc(iot_fragbuf_t *buf, size_t size)
{
    void *ptr;

    ptr = fragbuf_ensure(buf, size);

    if (ptr != NULL)
        buf->used += size;

    return ptr;
}


int iot_fragbuf_trim(iot_fragbuf_t *buf, void *ptr, size_t osize, size_t nsize)
{
    size_t diff;

    if (ptr + osize == buf->data + buf->used) { /* looks like the last alloc */
        if (nsize <= osize) {
            diff = osize - nsize;
            buf->used -= diff;

            return TRUE;
        }
    }

    return FALSE;
}


int iot_fragbuf_push(iot_fragbuf_t *buf, void *data, size_t size)
{
    void *ptr;

    ptr = fragbuf_ensure(buf, size);

    if (ptr != NULL) {
        memcpy(ptr, data, size);
        buf->used += size;

        return TRUE;
    }
    else
        return FALSE;
}


int iot_fragbuf_pull(iot_fragbuf_t *buf, void **datap, size_t *sizep)
{
    void     *data;
    uint32_t  size;

    if (buf == NULL || buf->used <= 0)
        return FALSE;

    if (IOT_UNLIKELY(*datap &&
                     (*datap < buf->data || *datap > buf->data + buf->used))) {
        iot_log_warning("%s(): *** looks like we're called with an unreset "
                        "datap pointer... ***", __FUNCTION__);
    }

    /* start of iteration */
    if (*datap == NULL) {
        if (!buf->framed) {
            *datap = buf->data;
            *sizep = buf->used;

            return TRUE;
        }
        else {
            if (buf->used < (int)sizeof(size))
                return FALSE;

            size = be32toh(*(uint32_t *)buf->data);

            if (buf->used >= (int)(sizeof(size) + size)) {
                *datap = buf->data + sizeof(size);
                *sizep = size;

                return TRUE;
            }
            else
                return FALSE;
        }
    }
    /* continue iteration */
    else {
        if (!buf->framed) {
            data = *datap + *sizep;

            if (buf->data <= data && data < buf->data + buf->used) {
                memmove(buf->data, data, buf->used - (data - buf->data));
                buf->used -= (data - buf->data);

                *datap = buf->data;
                *sizep = buf->used;

                return TRUE;
            }
            else {
                if (data == buf->data + buf->used)
                    buf->used = 0;

                return FALSE;
            }
        }
        else {
            if (*datap != buf->data + sizeof(size))
                return FALSE;

            size = be32toh(*(uint32_t *)buf->data);

            if ((int)(size + sizeof(size)) <= buf->used) {
                memmove(buf->data, buf->data + size + sizeof(size),
                        buf->used - (size + sizeof(size)));
                buf->used -= size + sizeof(size);
            }
            else
                return FALSE;

            if (buf->used <= (int)sizeof(size))
                return FALSE;

            size = be32toh(*(uint32_t *)buf->data);
            data = buf->data + sizeof(size);

            if (buf->used >= (int)(size + sizeof(size))) {
                *datap = data;
                *sizep = size;

                return TRUE;
            }

            return FALSE;
        }
    }
}
