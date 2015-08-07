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

#include <unistd.h>
#include <limits.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/transport.h"
#include "launcher/daemon/message.h"
#include "launcher/daemon/cgroup.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

static IOT_LIST_HOOK(handlers);

int application_init(launcher_t *l)
{
    app_handler_t   *h;
    iot_list_hook_t *p, *n;

    IOT_UNUSED(l);

    iot_list_foreach(&handlers, p, n) {
        h = iot_list_entry(p, typeof(*h), hook);

        if (h->init) {
            if (h->init() < 0)
                return -1;
        }
    }

    return 0;
}


void application_exit(launcher_t *l)
{
    app_handler_t   *h;
    iot_list_hook_t *p, *n;

    IOT_UNUSED(l);

    iot_list_foreach(&handlers, p, n) {
        h = iot_list_entry(p, typeof(*h), hook);

        if (h->exit)
            h->exit();
    }

    iot_list_init(&handlers);
}


application_t *application_find_by_cgroup(launcher_t *l, const char *path)
{
    iot_list_hook_t *p, *n;
    application_t   *app;

    iot_list_foreach(&l->apps, p, n) {
        app = iot_list_entry(p, typeof(*app), hook);

        if (!strcmp(app->id.cgroup, path))
            return app;
    }

    return NULL;
}


int application_setup(client_t *c, setup_req_t *req, reply_t *rpl)
{
    launcher_t      *l = c->l;
    application_t   *app;
    app_handler_t   *h;
    iot_list_hook_t *p, *n;
    const char      *argv0, *base;
    char             id[PATH_MAX];

    app = iot_allocz(sizeof(*app));

    if (app == NULL)
        goto oom;

    iot_list_init(&app->hook);
    app->l = l;
    app->id.user    = c->id.user;
    app->id.group   = c->id.group;
    app->id.process = c->id.process;

    argv0 = req->args[0];

    iot_log_info("Setting up application:");
    iot_log_info("  command: '%s'", req->args[0]);
    iot_log_info("      pid: %u", app->id.process);
    iot_log_info("      uid: %u", app->id.user);
    iot_log_info("      gid: %u", app->id.group);

    if ((base = strrchr(argv0, '/')) != NULL)
        base++;
    else
        base = argv0;

    if (cgroup_mkdir(l, app->id.user, base, app->id.process, id, sizeof(id)) < 0)
        goto failed;

    app->id.cgroup = iot_strdup(id);
    app->id.binary = iot_strdup(argv0);

    if (app->id.cgroup == NULL || app->id.binary == NULL)
        goto oom;

    iot_list_foreach(&handlers, p, n) {
        h = iot_list_entry(p, typeof(*h), hook);

        if (h->setup) {
            if (h->setup(app) < 0)
                goto failed;
        }
    }

    iot_list_append(&l->apps, &app->hook);

    reply_set_status(rpl, req->seqno, 0, "OK", NULL);

    return 0;

 failed:
    reply_set_status(rpl, req->seqno, EINVAL, "Failed", NULL);
    iot_free(app);

    return -1;

 oom:
    reply_set_status(rpl, req->seqno, ENOMEM, "Out of memory", NULL);
    iot_free(app->id.cgroup);
    iot_free(app->id.binary);
    iot_free(app);

    return -1;
}


int application_cleanup(client_t *c, cleanup_req_t *req, reply_t *rpl)
{
    launcher_t      *l    = c->l;
    const char      *path = req->cgpath;
    const char      *id   = path + 1;
    application_t   *app  = application_find_by_cgroup(l, id);
    app_handler_t   *h;
    iot_list_hook_t *p, *n;

    iot_log_info("Cleaning up application:");
    iot_log_info("    id: '%s'", id);

    if (app != NULL) {
        iot_list_delete(&app->hook);

        iot_list_foreach(&handlers, p, n) {
            h = iot_list_entry(p, typeof(*h), hook);

            if (h->cleanup)
                h->cleanup(app);
        }

        iot_free(app->id.cgroup);
        iot_free(app->id.binary);
        iot_free(app);
    }

    cgroup_rmdir(l, id);

    reply_set_status(rpl, req->seqno, 0, "OK", NULL);

    return 0;
}


void application_register_handler(app_handler_t *h)
{
    iot_list_append(&handlers, &h->hook);
}


static int list_running(client_t *c, list_req_t *req, reply_t *rpl)
{
    launcher_t      *l = c->l;
    application_t   *app;
    iot_list_hook_t *p, *n;
    iot_json_t      *data;

    data = iot_json_create(IOT_JSON_ARRAY);

    if (data == NULL)
        reply_set_status(rpl, req->seqno, ENOMEM, "Out of memory", NULL);

    iot_list_foreach(&l->apps, p, n) {
        app = iot_list_entry(p, typeof(*app), hook);

         /* XXX TODO for now we add id.binary instead of unset id.appid */
        if (!iot_json_array_append_string(data, app->id.binary))
            goto fail;
    }

    reply_set_status(rpl, req->seqno, 0, "OK", data);

    return 0;

 fail:
    iot_json_unref(data);
    reply_set_status(rpl, req->seqno, errno, "Failed", NULL);

    return -1;
}


static int list_all(client_t *c, list_req_t *req, reply_t *rpl)
{
    const char *apps[] = {
        "/usr/bin/foo", "/usr/bin/bar", "/usr/bin/foobar", "/usr/bin/barfoo",
        NULL
    };
    iot_json_t *data;
    int         i;

    data = iot_json_create(IOT_JSON_ARRAY);

    if (data == NULL)
        reply_set_status(rpl, req->seqno, ENOMEM, "Out of memory", NULL);

    for (i = 0; apps[i] != NULL; i++) {
        if (!iot_json_array_append_string(data, apps[i]))
            goto fail;
    }

    reply_set_status(rpl, req->seqno, 0, "OK", data);

    return 0;

 fail:
    iot_json_unref(data);
    reply_set_status(rpl, req->seqno, errno, "Failed", NULL);

    return -1;
}


int application_list(client_t *c, list_req_t *req, reply_t *rpl)
{
    if (req->type == REQUEST_LIST_RUNNING)
        return list_running(c, req, rpl);
    else
        return list_all(c, req, rpl);
}
