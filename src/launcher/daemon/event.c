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

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/list.h>
#include <iot/common/transport.h>
#include <iot/common/mask.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/msg.h"
#include "launcher/daemon/transport.h"
#include "launcher/daemon/event.h"


/*
 * a registered event
 */

typedef struct {
    iot_list_hook_t  hook;               /* to list of known events */
    char            *name;               /* public event name */
    int              id;                 /* internal event id */
} event_t;

static IOT_LIST_HOOK(events);            /* registered events */
static int           nevent;             /* number of registered events */


int event_id(const char *name, int register_missing)
{
    iot_list_hook_t *p, *n;
    event_t         *e;

    iot_list_foreach(&events, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        if (!strcmp(e->name, name))
            return e->id;
    }

    if (!register_missing) {
        errno = ENOENT;
        return -1;
    }

    if (nevent >= MAX_EVENTS) {
        errno = ENOSPC;
        return -1;
    }

    e = iot_allocz(sizeof(*e));

    if (e == NULL)
        return -1;

    e->name = iot_strdup(name);

    if (e->name == NULL) {
        iot_free(e);
        return -1;
    }

    iot_list_init(&e->hook);
    e->id = nevent++;

    iot_list_append(&events, &e->hook);

    return e->id;
}


const char *event_name(int id)
{
    iot_list_hook_t *p, *n;
    event_t         *e;

    if (id < 0 || id >= nevent)
        goto notfound;

    iot_list_foreach(&events, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        if (e->id == id)
            return e->name;
    }

 notfound:
    errno = ENOENT;
    return NULL;
}


int event_send(launcher_t *l, pid_t pid, const char *event, iot_json_t *data)
{
    client_t        *t;
    iot_list_hook_t *p, *n;
    iot_json_t      *e;


    iot_list_foreach(&l->clients, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);

        if (t->id.pid != pid)
            continue;

        e = msg_event_create(event, data);

        if (e == NULL)
            return -1;

        iot_debug("sending event: '%s'", iot_json_object_to_string(e));

        transport_send(t, e);

        iot_json_unref(e);

        return 0;
    }

    errno = ENOENT;
    return -1;
}


iot_json_t *event_route(client_t *c, iot_json_t *req)
{
    launcher_t      *l = c->l;
    client_t        *t;
    iot_list_hook_t *p, *n;
    const char      *event;
    identity_t       dst;
    iot_json_t      *e, *data;
    int              id, cnt;

    if (!iot_json_get_string(req, "event", &event))
        return msg_status_error(EINVAL, "malformed request, missing 'event'");

    id = event_lookup(event);

    if (id < 0)
        return msg_status_error(EINVAL, "unknown event '%s'", event);

    iot_clear(&dst);
    dst.uid = NO_UID;
    dst.gid = NO_GID;

    iot_json_get_string (req, "label"  , &dst.label);
    iot_json_get_string (req, "appid"  , &dst.app);
    iot_json_get_integer(req, "user"   , &dst.uid);
    iot_json_get_integer(req, "group"  , &dst.gid);
    iot_json_get_integer(req, "process", &dst.pid);
    iot_json_get_object (req, "data"   , &data);

    cnt = 0;
    e   = NULL;

    iot_list_foreach(&l->clients, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);

        if (!iot_mask_test(&t->mask, id))
            continue;

        if (!((dst.uid == NO_UID || dst.uid == t->id.uid) &&
              (dst.gid == NO_GID || dst.gid == t->id.gid) &&
              (dst.pid == NO_PID || dst.pid == t->id.pid)))
            continue;

        if (e == NULL && (e = msg_event_create(event, data)) == NULL)
            return msg_status_error(EINVAL, "failed to create event message");

        iot_debug("sending event: '%s'", iot_json_object_to_string(e));

        transport_send(t, e);
        cnt++;
    }

    iot_json_unref(e);

    return msg_status_ok(NULL);
}
