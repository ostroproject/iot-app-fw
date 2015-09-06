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
#include <sys/types.h>
#include <sys/stat.h>
#define __USE_GNU
#include <sys/socket.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/transport.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/cgroup.h"
#include "launcher/daemon/event.h"
#include "launcher/daemon/client.h"


static void reject_connection(iot_transport_t *lt)
{
    iot_transport_t *t;

    t = iot_transport_accept(lt, NULL, IOT_TRANSPORT_REUSEADDR);

    iot_transport_disconnect(t);
    iot_transport_destroy(t);
}


client_t *client_create(launcher_t *l, iot_transport_t *t)
{
    client_t     *c;
    struct ucred  uc;
    socklen_t     len;
    char          label[256], cgpath[PATH_MAX];

    c = iot_allocz(sizeof(*c));

    if (c == NULL) {
        reject_connection(t);
        return NULL;
    }

    iot_list_init(&c->hook);
    c->t = iot_transport_accept(t, c, IOT_TRANSPORT_REUSEADDR);

    if (c->t == NULL) {
        iot_free(c);

        return NULL;
    }

    len = sizeof(uc);
    if (!iot_transport_getopt(c->t, "peer-cred", &uc, &len)) {
        client_destroy(c);

        return NULL;
    }

    len = sizeof(label) - 1;
    if (iot_transport_getopt(c->t, "peer-sec", label, &len)) {
        label[len] = '\0';
        c->id.label = iot_strdup(label);

        if (c->id.label == NULL) {
            client_destroy(c);

            return NULL;
        }
    }

    c->l = l;
    c->id.uid = uc.uid;
    c->id.gid = uc.gid;
    c->id.pid = uc.pid;

    if (t == l->lnc)
        c->type = CLIENT_LAUNCHER;
    else {
        c->type = CLIENT_IOTAPP;

        if (!cgroup_path(cgpath, sizeof(cgpath), CGROUP_DIR, c->id.pid) ||
            !(c->id.cgrp = iot_strdup(cgpath))) {
            client_destroy(c);

            return NULL;
        }
    }

    iot_mask_init(&c->mask);
    iot_list_append(&l->clients, &c->hook);

    return c;
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


int client_subscribe(client_t *c, event_sub_req_t *req, reply_t *rpl)
{
    char *e;
    int   i;

    for (i = 0; (e = req->events[i]) != NULL; i++) {
        if (!iot_mask_set(&c->mask, event_register(e))) {
            reply_set_status(rpl, req->seqno, EINVAL, "Subscribe failed", NULL);
            return -1;
        }
        else
            iot_log_info("Subscribed for event '%s'...", e);
    }

    reply_set_status(rpl, req->seqno, 0, "OK", NULL);
    return 0;
}
