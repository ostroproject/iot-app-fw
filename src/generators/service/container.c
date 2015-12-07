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

static container_t *containers = NULL;
static int          ncontainer = 0;


int container_register(container_t *cntr)
{
    container_t *c;

    if (!iot_reallocz(containers, ncontainer, ncontainer + 1))
        return -1;

    c = containers + ncontainer++;

    c->type    = cntr->type;
    c->handler = cntr->handler;

    return 0;
}


container_t *container_lookup(const char *type)
{
    container_t *c;

    for (c = containers; c - containers < ncontainer; c++)
        if (!strcmp(c->type, type))
            return c;

    return NULL;
}


static int container_handler(service_t *s, const char *k, iot_json_t *o)
{
    const char *type;
    container_t *c;

    IOT_UNUSED(k);
    IOT_UNUSED(s);

    if (iot_json_get_type(o) != IOT_JSON_OBJECT)
        goto invalid;

    if (!iot_json_get_string(o, "type", &type))
        goto invalid;

    if (!(c = container_lookup(type)))
        goto invalid;

    if (c->handler(s, type, o) < 0)
        goto fail;

    return 0;

 invalid:
    errno = EINVAL;
 fail:
    s->g->status = -1;
    return -1;
}


REGISTER_KEY(container, container_handler);
