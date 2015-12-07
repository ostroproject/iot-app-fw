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
#include <math.h>
#include <limits.h>

#include <iot/common/mm.h>

#include "generator.h"

#define RW 1
#define RO 0

static manifest_key_t *keys = NULL;
static int             nkey = 0;


int key_register(manifest_key_t *key)
{
    manifest_key_t *k;

    if (iot_reallocz(keys, nkey, nkey + 1) == NULL)
        return -1;

    log_debug("Registering manifest key '%s'...", key->key);

    k = keys + nkey++;

    k->key     = key->key;
    k->handler = key->handler;

    return 0;
}


manifest_key_t *key_lookup(const char *key)
{
    manifest_key_t *k;

    for (k = keys; k - keys < nkey; k++)
        if (!strcmp(k->key, key))
            return k;

    return NULL;
}


static int application_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);
    IOT_UNUSED(s);

    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int description_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    if (section_add(&s->unit, NULL, "Description", "%s \\\n"
                    "    Application '%s' by provider '%s'.",
                    iot_json_string_value(o), s->app, s->provider) == NULL)
        return -1;
    else
        return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int usrgrp_handler(service_t *s, const char *key, iot_json_t *o)
{
    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    if (!strcasecmp(key, "User"))
        s->user  = iot_json_string_value(o);
    else
        s->group = iot_json_string_value(o);

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int command_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_ARRAY)
        goto invalid;

    s->argv = o;
    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int autostart_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_BOOLEAN)
        goto invalid;

    s->autostart = iot_json_boolean_value(o) ? 1 : 0;
    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int environment_handler(service_t *s, const char *key, iot_json_t *o)
{
    const char *k;
    iot_json_t *v;
    iot_json_iter_t it;

    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_OBJECT)
        goto invalid;

    iot_json_foreach_member(o, k, v, it) {
        if (iot_json_get_type(v) != IOT_JSON_STRING)
            goto invalid;

        if (!section_add(&s->service, NULL, "Environment", "%s=%s",
                         k, iot_json_string_value(v)))
            return -1;
    }

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


REGISTER_KEY(application, application_handler);
REGISTER_KEY(description, description_handler);
REGISTER_KEY(user       , usrgrp_handler     );
REGISTER_KEY(group      , usrgrp_handler     );
REGISTER_KEY(command    , command_handler    );
REGISTER_KEY(autostart  , autostart_handler  );
REGISTER_KEY(environment, environment_handler);
