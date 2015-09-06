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
#include <sys/types.h>
#include <sys/stat.h>
#define __USE_GNU
#include <sys/socket.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/transport.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/msg.h"
#include "launcher/daemon/cgroup.h"
#include "launcher/daemon/event.h"
#include "launcher/daemon/client.h"


static int get_credentials(client_t *c);


client_t *client_create(launcher_t *l, iot_transport_t *t)
{
    client_t *c     = iot_allocz(sizeof(*c));
    int       flags = IOT_TRANSPORT_REUSEADDR | IOT_TRANSPORT_CLOEXEC;

    if (c == NULL)
        goto reject;

    iot_list_init(&c->hook);
    c->l = l;
    c->t = iot_transport_accept(t, c, flags);

    if (c->t == NULL)
        goto fail;

    c->type = (c->t == l->lnc ? CLIENT_LAUNCHER : CLIENT_IOTAPP);

    if (get_credentials(c) < 0)
        goto fail;

    iot_mask_init(&c->mask);
    iot_list_append(&l->clients, &c->hook);

    return c;

 reject: {
        iot_transport_t *rt;

        rt = iot_transport_accept(t, NULL, IOT_TRANSPORT_REUSEADDR);
        iot_transport_disconnect(rt);
        iot_transport_destroy(rt);
    }

 fail:
    client_destroy(c);

    return NULL;
}


void client_destroy(client_t *c)
{
    if (c == NULL)
        return;

    iot_list_delete(&c->hook);

    iot_transport_disconnect(c->t);
    iot_transport_destroy(c->t);

    iot_free(c->id.label);
    iot_free(c->id.cgrp);

    iot_free(c);
}


iot_json_t *client_subscribe(client_t *c, iot_json_t *req)
{
    iot_json_t *events;
    const char *e;
    int         i, n;

    if (!iot_json_get_array(req, "events", &events))
        return status_error(EINVAL, "malformed request, missing 'events'");

    n = iot_json_array_length(events);
    for (i = 0; i < n; i++) {
        if (!iot_json_array_get_string(events, i, &e))
            return status_error(EINVAL, "failed to get list of events");

        if (!iot_mask_set(&c->mask, event_register(e)))
            return status_error(EINVAL, "failed to subscribe for '%s'", e);
    }

    return status_ok(0, NULL, "OK");
}


static int get_credentials(client_t *c)
{
    struct ucred creds;
    socklen_t    size;
    char         label[256], dir[PATH_MAX];

    size = sizeof(label) - 1;
    if (iot_transport_getopt(c->t, "peer-sec", label, &size)) {
        label[size] = '\0';

        c->id.label = iot_strdup(label);

        if (c->id.label == NULL)
            return -1;
    }

    size = sizeof(creds);
    if (!iot_transport_getopt(c->t, "peer-cred", &creds, &size))
        return -1;

    c->id.uid = creds.uid;
    c->id.gid = creds.gid;
    c->id.pid = creds.pid;

    if (c->type == CLIENT_IOTAPP) {
        if (cgroup_path(dir, sizeof(dir), CGROUP_DIR, c->id.pid) == NULL)
            return -1;

        c->id.cgrp = iot_strdup(dir);

        if (c->id.cgrp == NULL)
            return -1;
    }

    return 0;
}

