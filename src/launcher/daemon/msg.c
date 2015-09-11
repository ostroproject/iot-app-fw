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
#include <iot/common/log.h>
#include <iot/common/json.h>

#include "launcher/daemon/msg.h"


const char *msg_type(iot_json_t *msg)
{
    const char *type;

    if (!iot_json_get_string(msg, "type", &type)) {
        errno = EINVAL;
        type = NULL;
    }

    return type;
}


iot_json_t *msg_request_create(const char *type, int seqno, iot_json_t *payload)
{
    iot_json_t *req;

    req = iot_json_create(IOT_JSON_OBJECT);

    if (req == NULL)
        goto nomem;

    if (!iot_json_add_string(req, "type", type))
        goto nomem;

    if (!iot_json_add_integer(req, "seqno", seqno))
        goto nomem;

    if (payload != NULL) {
        if (iot_json_get_type(payload) != IOT_JSON_OBJECT)
            goto invalid_payload;

        iot_json_add_object(req, type, payload);
    }

    return req;

 nomem:
    iot_json_unref(req);
    errno = ENOMEM;
    return NULL;

 invalid_payload:
    iot_json_unref(req);
    errno = EINVAL;
    return NULL;
}


int msg_request_parse(iot_json_t *req, const char **type, int *seqno,
                      iot_json_t **payload)
{
    if (!iot_json_get_string(req, "type", type))
        goto invalid;

    if (!iot_json_get_integer(req, "seqno", seqno))
        goto invalid;

    if (payload != NULL) {
        if (!iot_json_get_object(req, *type, payload))
            goto invalid;
    }

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


iot_json_t *msg_reply_create(int seqno, iot_json_t *status)
{
    iot_json_t *rpl;

    rpl = iot_json_create(IOT_JSON_OBJECT);

    if (rpl == NULL)
        goto nomem;

    if (!iot_json_add_string(rpl, "type", "status"))
        goto nomem;

    if (!iot_json_add_integer(rpl, "seqno", seqno))
        goto nomem;

    if (status != NULL) {
        if (iot_json_get_type(status) != IOT_JSON_OBJECT)
            goto invalid_status;

        iot_json_add_object(rpl, "status", status);
    }

    return rpl;

 nomem:
    iot_json_unref(rpl);
    errno = ENOMEM;
    return NULL;

 invalid_status:
    iot_json_unref(rpl);
    errno = EINVAL;
    return NULL;
}


iot_json_t *msg_error_create(int seqno, int code, char *fmt, ...)
{
    iot_json_t *rpl, *s;
    va_list     ap;
    char        buf[1024];

    rpl = iot_json_create(IOT_JSON_OBJECT);

    if (rpl == NULL)
        goto nomem;

    if (!iot_json_add_string(rpl, "type", "status"))
        goto nomem;

    if (!iot_json_add_integer(rpl, "seqno", seqno))
        goto nomem;

    va_start(ap, fmt);
    if (fmt != NULL) {
        vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
        buf[sizeof(buf) - 1] = '\0';
    }
    else
        snprintf(buf, sizeof(buf), "failed, unknown error");
    va_end(ap);

    s = iot_json_create(IOT_JSON_OBJECT);

    if (s == NULL)
        goto nomem;

    iot_json_add_object(rpl, "status", s);

    if (!iot_json_add_integer(s, "status", code))
        goto nomem;
    if (!iot_json_add_string(s, "message", buf))
        goto nomem;

    return rpl;

 nomem:
    iot_json_unref(rpl);
    errno = ENOMEM;
    return NULL;
}


iot_json_t *msg_status_create(int code, iot_json_t *payload,
                              const char *fmt, ...)
{
    iot_json_t *s;
    char        buf[1024];
    va_list     ap;

    s = iot_json_create(IOT_JSON_OBJECT);

    if (s == NULL)
        goto nomem;

    va_start(ap, fmt);
    if (fmt != NULL) {
        vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
        buf[sizeof(buf) - 1] = '\0';
    }
    else
        snprintf(buf, sizeof(buf), "failed, unknown error");
    va_end(ap);

    if (!iot_json_add_integer(s, "status", code))
        goto nomem;

    if (!iot_json_add_string(s, "message", buf))
        goto nomem;

    if (payload != NULL)
        iot_json_add_object(s, "data", payload);

    return s;

 nomem:
    iot_json_unref(s);
    errno = ENOMEM;
    return NULL;
}


enum {
    MSG_TYPE_END = -1,
    MSG_TYPE_NONE,
    MSG_TYPE_INTEGER,
    MSG_TYPE_BOOLEAN,
    MSG_TYPE_STRING,
    MSG_TYPE_DOUBLE,
    MSG_TYPE_ARRAY,
    MSG_TYPE_OBJECT,
    MSG_TYPE_ARRAY_INLINED,
    MSG_TYPE_OBJECT_INLINED,
};


static iot_json_t *array_create(va_list *ap);

static iot_json_t *object_create(va_list *ap)
{
    iot_json_t *o;
    int         type, at;
    const char *name;
    int         i;
    const char *s;
    bool        b;
    double      d;
    void       *av;
    size_t      ac;
    iot_json_t *ov;

    o = iot_json_create(IOT_JSON_OBJECT);

    if (o == NULL)
        return NULL;

    while ((name = va_arg(ap, const char *)) != NULL) {
        type = va_arg(*ap, int);

        switch (type) {
        case MSG_TYPE_INTEGER:
            i = va_arg(*ap, int);
            if (!iot_json_add_integer(o, name, i))
                goto nomem;
            break;

        case MSG_TYPE_STRING:
            s = va_arg(*ap, const char *);
            if (!iot_json_add_string(o, name, s))
                goto nomem;

        case MSG_TYPE_BOOLEAN:
            b = va_arg(*ap, bool);
            if (!iot_json_add_boolean(o, name, b))
                goto nomem;
            break;

        case MSG_TYPE_DOUBLE:
            d = va_arg(*ap, double);
            if (!iot_json_add_double(o, name, d))
                goto nomem;
            break;

        case MSG_TYPE_ARRAY:
            at = va_arg(*ap, int);
            av = va_arg(*ap, void *);
            ac = va_arg(*ap, size_t);
            if (!iot_json_add_array(o, name, at, av, ac))
                goto nomem;
            break;

        case MSG_TYPE_OBJECT:
            ov = va_arg(*ap, iot_json_t *);
            iot_json_add_object(o, name, ov);
            break;

        case MSG_TYPE_ARRAY_INLINED:
            if (!(av = array_create(ap)))
                goto fail;
            iot_json_add_object(o, name, av);
            break;

        case MSG_TYPE_OBJECT_INLINED:
            if (!(ov = object_create(ap)))
                goto fail;
            iot_json_add_object(o, name, ov);
            break;

        default:
            errno = EINVAL;
            goto fail;
        }
    }

    return o;

 nomem:
    iot_json_unref(o);
    errno = ENOMEM;
    return NULL;

 fail:
    iot_json_unref(o);
    return NULL;
}


static iot_json_t *array_create(va_list *ap)
{
    iot_json_t *a;
    int         type, cnt, i;

    a = iot_json_create(IOT_JSON_ARRAY);

    if (a == NULL)
        return NULL;

    type = va_arg(*ap, int);
    cnt  = va_arg(*ap, int);

    for (i = 0; i < cnt; i++) {
        switch (type) {
        case MSG_TYPE_INTEGER:
            if (!iot_json_array_append_integer(a, va_arg(*ap, int)))
                goto nomem;
            break;

        case MSG_TYPE_STRING:
            if (!iot_json_array_append_string(a, va_arg(*ap, const char *)))
                goto nomem;
            break;

        case MSG_TYPE_BOOLEAN:
            if (!iot_json_array_append_boolean(a, va_arg(*ap, bool)))
                goto nomem;
            break;

        case MSG_TYPE_DOUBLE:
            if (!iot_json_array_append_double(a, va_arg(*ap, double)))
                goto nomem;
            break;

        case MSG_TYPE_ARRAY:
            iot_log_error("MSG_TYPE_ARRAY for %s() not implemented", __func__);
            errno = EINVAL;
            goto fail;

        case MSG_TYPE_OBJECT:
            iot_log_error("MSG_TYPE_OBJECT for %s() not implemented", __func__);
            errno = EINVAL;
            goto fail;

        default:
            iot_log_error("type %d for %s() not implemented", type, __func__);
        }
    }

 nomem:
    iot_json_unref(a);
    errno = ENOMEM;
    return NULL;

 fail:
    iot_json_unref(a);
    return NULL;
}


iot_json_t *msg_payload_create(int type, ...)
{
    iot_json_t *pl;
    va_list     ap;

    va_start(ap, type);
    pl = object_create(&ap);
    va_end(ap);

    return pl;
}


int msg_reply_parse(iot_json_t *rpl, int *seqno, const char **message,
                    iot_json_t **payload)
{
    iot_json_t *s;
    int         code;
    const char *type, *msg;

    if (!iot_json_get_string(rpl, "type", &type))
        goto invalid;

    if (strcmp(type, "status"))
        goto invalid;

    if (!iot_json_get_integer(rpl, "seqno", seqno))
        goto invalid;

    if (!iot_json_get_object(rpl, "status", &s))
        goto invalid;

    if (!iot_json_get_integer(s, "status", &code))
        goto invalid;

    if (!iot_json_get_string(s, "message", message ? message : &msg))
        goto invalid;

    if (code == 0) {
        if (payload != NULL)
            *payload = iot_json_get(s, "data");
    }
    else {
        if (payload != NULL)
            *payload = s;
    }

    return code;

 invalid:
    errno = EINVAL;
    return -1;
}


iot_json_t *msg_event_create(const char *name, iot_json_t *data)
{
    iot_json_t *msg, *e;

    msg = iot_json_create(IOT_JSON_OBJECT);

    if (msg == NULL)
        return NULL;

    iot_json_add_string (msg, "type" , "event");
    iot_json_add_integer(msg, "seqno", 0);

    e = iot_json_create(IOT_JSON_OBJECT);

    if (e == NULL) {
        iot_json_unref(msg);
        return NULL;
    }

    iot_json_add_object(msg, "event", e);
    iot_json_add_string(e  , "event", name);
    iot_json_add_object(e  , "data" , iot_json_ref(data));

    return msg;
}


int msg_event_parse(iot_json_t *msg, const char **name, iot_json_t **payload)
{
    iot_json_t *e;
    const char *event;

    if (!iot_json_get_object(msg, "event", &e))
        goto invalid;

    if (!iot_json_get_string(e, "event", &event))
        goto invalid;

    if (name != NULL)
        *name = event;

    if (payload != NULL)
        if (!iot_json_get_object(e, "data", payload))
            *payload = NULL;

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}

