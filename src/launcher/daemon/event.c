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
#include "launcher/daemon/event.h"

#define MAX_EVENTS 1024


/*
 * an event definition
 */

typedef struct {
    iot_list_hook_t  hook;               /* to list of known events */
    char            *name;               /* event name */
    int              id;                 /* event id */
} event_def_t;


static IOT_LIST_HOOK(events);
static int           nevent = 0;


int event_id(const char *name, int add_missing)
{
    iot_list_hook_t *p, *n;
    event_def_t     *e;

    iot_list_foreach(&events, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        if (!strcmp(e->name, name))
            return e->id;
    }

    if (!add_missing)
        return -1;

    e = iot_allocz(sizeof(*e));

    if (e == NULL)
        return -1;

    iot_list_init(&e->hook);

    e->name = iot_strdup(name);

    if (e->name == NULL) {
        iot_free(e);
        return -1;
    }

    e->id = nevent++;
    iot_list_append(&events, &e->hook);

    return e->id;
}


const char *event_name(int id)
{
    iot_list_hook_t *p, *n;
    event_def_t     *e;

    iot_list_foreach(&events, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        if (e->id == id)
            return e->name;
    }

    return NULL;
}


int event_route(launcher_t *l, uid_t user, const char *binary, pid_t process,
                const char *event, iot_json_t *data)
{
    iot_json_t      *msg = NULL;
    client_t        *c;
    iot_list_hook_t *cp, *cn;
    int              id, n;

    n  = 0;
    id = event_lookup(event);

    if (id < 0) {
        errno = ENOENT;
        return -1;
    }

    iot_list_foreach(&l->clients, cp, cn) {
        c = iot_list_entry(cp, typeof(*c), hook);

        iot_log_info("user: %u, %u, binary: %s, %s, pid: %u, %u",
                     user, c->id.uid,
                     binary ? binary : "<NULL>",
                     c->id.argv[0] ? c->id.argv[0] : "<NULL>",
                     process, c->id.pid);

        if (!((user == (uid_t)-1   || user == c->id.uid) &&
              (binary == NULL      || !strcmp(binary, c->id.argv[0])) &&
              (process == (pid_t)0 || process == c->id.pid)))
            continue;

        if (!iot_mask_test(&c->mask, id))
            continue;

        if (msg == NULL) {
            msg = iot_json_create(IOT_JSON_OBJECT);

            if (msg == NULL)
                return -1;

            iot_json_add_string (msg, "type" , "event");
            iot_json_add_integer(msg, "seqno", 0);
            iot_json_add_string (msg, "event", event);
            iot_json_add        (msg, "data" , iot_json_ref(data));
        }

        iot_log_info("Sending message to pid %u: %s...", c->id.pid,
                     iot_json_object_to_string(msg));

        iot_transport_sendjson(c->t, msg);
        n++;
    }

    iot_json_unref(msg);

    return n;
}
