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

iot_json_t *status_error(int code, const char *fmt, ...)
{
    iot_json_t *s;
    char       *msg, buf[128];
    int         n;
    va_list     ap;

    if ((s = iot_json_create(IOT_JSON_OBJECT)) == NULL)
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

    iot_json_add_integer(s, "status" , code ? code : EINVAL);
    iot_json_add_string (s, "message", msg);

    return s;
}


iot_json_t *status_ok(int code, iot_json_t *data, const char *fmt, ...)
{
    iot_json_t *s;
    char       *msg, buf[128];
    int         n;
    va_list     ap;

    if ((s = iot_json_create(IOT_JSON_OBJECT)) == NULL)
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

    iot_json_add_integer(s, "status" , code);
    iot_json_add_string (s, "message", msg);

    if (data)
        iot_json_add_object(s, "data", data);

    return s;
}


iot_json_t *event_msg(const char *name, iot_json_t *data)
{
    iot_json_t *e, *o;

    e = iot_json_create(IOT_JSON_OBJECT);

    if (e == NULL)
        return NULL;

    iot_json_add_string (e, "type" , "event");
    iot_json_add_integer(e, "seqno", 0);
    if ((o = iot_json_create(IOT_JSON_OBJECT)) == NULL) {
        iot_json_unref(e);
        return NULL;
    }
    iot_json_add_object(e, "event", o);
    iot_json_add_string(o, "event", name);
    iot_json_add_object (o, "data" , iot_json_ref(data));

    return e;
}


int status_reply(iot_json_t *rpl, const char **msg, iot_json_t **data)
{
    const char *type;
    iot_json_t *s;
    int         code;

    if (!iot_json_get_string(rpl, "type", &type) || strcmp(type, "status"))
        goto malformed;

    if (!iot_json_get_object (rpl, "status" , &s   ) ||
        !iot_json_get_integer(s  , "status" , &code) ||
        !iot_json_get_string (s  , "message", msg  )) {
    malformed:
        *msg = "malformed message";
        return EINVAL;
    }

    if (code == 0) {
        if (data != NULL && !iot_json_get_object(s, "data", data))
            *data = NULL;
    }

    return code;
}
