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
#include <errno.h>
#include <string.h>

#include <iot/common/macros.h>
#include <iot/common/json.h>

#include "launcher/daemon/msg.h"


iot_json_t *msg_status_error(int code, const char *fmt, ...)
{
    iot_json_t *pl;
    char        buf[128], *msg;
    int         n;
    va_list     ap;

    if ((pl = iot_json_create(IOT_JSON_OBJECT)) == NULL)
        return NULL;

    if (fmt != NULL) {
        va_start(ap, fmt);
        n = snprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
    }
    else
        goto nomsg;

    if (n < 0 || n >= (int)sizeof(buf))
    nomsg:
        msg = "failed";
    else
        msg = buf;

    iot_json_add_integer(pl, "status" , code ? code : EINVAL);
    iot_json_add_string (pl, "message", msg);

    return pl;
}


iot_json_t *msg_status_ok(int code, iot_json_t *data, const char *fmt, ...)
{
    iot_json_t *pl;
    char       *msg, buf[128];
    int         n;
    va_list     ap;

    if ((pl = iot_json_create(IOT_JSON_OBJECT)) == NULL)
        return NULL;

    if (fmt != NULL) {
        va_start(ap, fmt);
        n = snprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
    }
    else
        goto nomsg;

    if (n < 0 || n >= (int)sizeof(buf))
    nomsg:
        msg = "OK";
    else
        msg = buf;

    iot_json_add_integer(pl, "status" , code);
    iot_json_add_string (pl, "message", msg);

    if (data)
        iot_json_add_object(pl, "data", data);

    return pl;
}


int msg_status_data(iot_json_t *hdr, const char **msg, iot_json_t **data)
{
    const char *type;
    iot_json_t *pl;
    int         code;

    if (!iot_json_get_string(hdr, "type", &type) || strcmp(type, "status"))
        goto malformed;

    if (!iot_json_get_object (hdr, "status" , &pl ) ||
        !iot_json_get_integer(pl , "status" , &code) ||
        !iot_json_get_string (pl , "message", msg  )) {
    malformed:
        *msg = "malformed message";
        if (data != NULL)
            *data = NULL;
        return EINVAL;
    }

    if (code == 0) {
        if (data != NULL && !iot_json_get_object(pl, "data", data))
            *data = NULL;
    }

    return code;
}


iot_json_t *msg_event(const char *name, iot_json_t *data)
{
    iot_json_t *hdr, *e;

    hdr = iot_json_create(IOT_JSON_OBJECT);

    if (hdr == NULL)
        return NULL;

    iot_json_add_string (hdr, "type" , "event");
    iot_json_add_integer(hdr, "seqno", 0);

    if ((e = iot_json_create(IOT_JSON_OBJECT)) == NULL) {
        iot_json_unref(hdr);
        return NULL;
    }

    iot_json_add_object(hdr, "event", e);
    iot_json_add_string(e  , "event", name);
    iot_json_add_object(e  , "data" , iot_json_ref(data));

    return hdr;
}


iot_json_t *msg_event_data(iot_json_t *hdr, const char **name)
{
    iot_json_t *e, *data;

    if (!iot_json_get_object(hdr, "event", &e))
        goto invalid;

    if (name != NULL)
        if (!iot_json_get_string(e, "event", name))
            goto invalid;

    if (!iot_json_get_object(e, "data", &data))
        data = NULL;

    return data;

 invalid:
    errno = EINVAL;
    if (name != NULL)
        *name = NULL;

    return NULL;
}


int msg_hdr(iot_json_t *hdr, const char **type, int *seqno)
{
    if (type != NULL)
        if (!iot_json_get_string(hdr, "type", type))
            goto invalid;

    if (seqno != NULL)
        if (!iot_json_get_integer(hdr, "seqno", seqno))
            goto invalid;

    return 0;

 invalid:
    if (type)
        *type = NULL;
    if (seqno)
        *seqno = -1;

    errno = EINVAL;
    return -1;
}


const char *msg_type(iot_json_t *hdr)
{
    const char *type;

    if (msg_hdr(hdr, &type, NULL) < 0)
        return NULL;
    else
        return type;
}


int msg_seqno(iot_json_t *hdr)
{
    int seqno;

    if (msg_hdr(hdr, NULL, &seqno) < 0)
        return -1;
    else
        return seqno;
}

