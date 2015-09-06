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
#include "launcher/daemon/msg.h"
#include "launcher/daemon/message.h"
#include "launcher/daemon/cgroup.h"


/* list for collecting auto-registered application-handling hooks */
static IOT_LIST_HOOK(hooks);

typedef enum {
    HOOK_INIT = 0,
    HOOK_EXIT,
    HOOK_STARTUP,
    HOOK_CLEANUP
} hook_event_t;


void application_hook_register(app_hook_t *h)
{
    iot_list_init(&h->hook);
    iot_list_append(&hooks, &h->hook);
}


static int hook_trigger(launcher_t *l, application_t *a, hook_event_t e)
{
    iot_list_hook_t *p, *n;
    app_hook_t      *h;

    iot_list_join(&l->hooks, &hooks);

    iot_list_foreach(&l->hooks, p, n) {
        h = iot_list_entry(p, typeof(*h), hook);

        switch (e) {
        case HOOK_INIT:
            if (h->init && h->init() < 0)
                return -1;
            break;
        case HOOK_EXIT:
            if (h->exit)
                h->exit();
            break;
        case HOOK_STARTUP:
            if (h->setup(a) < 0)
                return -1;
            break;
        case HOOK_CLEANUP:
            if (h->cleanup(a) < 0)
                return -1;
            break;
        default:
            return -1;
        }
    }

    return 0;
}


int application_init(launcher_t *l)
{
    iot_list_move(&l->hooks, &hooks);

    if (hook_trigger(l, NULL, HOOK_INIT) < 0)
        return -1;

    return 0;
}


void application_exit(launcher_t *l)
{
    hook_trigger(l, NULL, HOOK_EXIT);

    iot_list_init(&hooks);
    iot_list_init(&l->hooks);
}


static int copy_arguments(iot_json_t *args, char ***argvp)
{
    char **argv;
    int    argc, i;

    if (args == NULL || (argc = iot_json_array_length(args)) < 0)
        return -1;

    if (argc == 0) {
        *argvp = NULL;
        return 0;
    }

    if ((argv = iot_allocz_array(char *, argc)) == NULL)
        return -1;

    for (i = 0; i < argc; i++) {
        if (!iot_json_array_get_string(args, i, argv + i))
            goto fail;
        argv[i] = iot_strdup(argv[i]);
        if (argv[i] == NULL)
            goto fail;
    }

    *argvp = argv;
    return argc;

 fail:
    for (i = 0; i < argc; i++)
        iot_free(argv[i]);
    return -1;
}


iot_json_t *application_setup(client_t *c, iot_json_t *req)
{
    launcher_t     *l = c->l;
    iot_json_t     *s;
    application_t  *a;
    iot_manifest_t *m;
    const char     *f, *manifest, *app, *base;
    uid_t           uid;
    gid_t           gid;
    iot_json_t     *exec, *dbg;
    char            dir[PATH_MAX];

    if (!iot_json_get_string (req, f="manifest", &manifest) ||
        !iot_json_get_string (req, f="app"     , &app     ) ||
        !iot_json_get_integer(req, f="user"    , &uid     ) ||
        !iot_json_get_integer(req, f="group"   , &gid     ) ||
        !iot_json_get_array  (req, f="exec"    , &exec    )) {
        s = status_error(EINVAL, "malformed message, missing field %s", f);
        goto fail;
    }

    m = iot_manifest_read(manifest);

    if (m == NULL) {
        s = status_error(EINVAL, "failed to load manifest '%s'", manifest);
        goto fail;
    }

    a = iot_allocz(sizeof(*a));

    if (a == NULL)
        return NULL;

    iot_list_init(&a->hook);
    a->l = l;
    a->c = c;
    a->m = m;

    a->app     = iot_strdup(app);
    a->id.argc = copy_arguments(exec, &a->id.argv);

    if (a->app == NULL || a->id.argc < 0) {
        s = NULL;
        goto fail;
    }

    /* XXX TODO: should handle identity from dbg here... */
    iot_json_get_object(req, "dbg", &dbg);

    a->id.uid = (uid != NO_UID ? uid : c->id.uid);
    a->id.gid = (gid != NO_GID ? gid : c->id.gid);
    a->id.pid = c->id.pid;

    if ((base = strrchr(a->id.argv[0], '/')) != NULL)
        base++;
    else
        base = a->id.argv[0];

    if (cgroup_mkdir(l, a->id.uid, base, a->id.pid, dir, sizeof(dir)) < 0) {
        s = status_error(errno, "failed to create cgroup directory");
        goto fail;
    }

    a->id.cgrp = iot_strdup(dir);

    if (a->id.cgrp == NULL) {
        s = NULL;
        goto fail;
    }

    if (hook_trigger(l, a, HOOK_STARTUP) < 0) {
        s = status_error(errno, "startup hook failed");
        goto fail;
    }

    iot_list_append(&l->apps, &a->hook);

    return status_ok(0, NULL, "OK");

 fail:
    if (a) {
        iot_free(a);
    }

    return s;
}


static application_t *application_for_cgroup(launcher_t *l, const char *cgrp)
{
    iot_list_hook_t *p, *n;
    application_t   *a;

    iot_list_foreach(&l->apps, p, n) {
        a = iot_list_entry(p, typeof(*a), hook);

        if (!strcmp(a->id.cgrp, cgrp))
            return a;
    }

    return NULL;
}


iot_json_t *application_cleanup(client_t *c, iot_json_t *req)
{
    launcher_t    *l = c->l;
    application_t *a;
    const char    *f;
    const char    *cgrp;

    if (!iot_json_get_string(req, f="cgroup", &cgrp))
        return status_error(EINVAL, "malformed request, missing field %s", f);
    else
        cgrp++;

    a = application_for_cgroup(l, cgrp);

    if (a == NULL)
        return status_ok(0, NULL, "already gone");

    iot_list_delete(&a->hook);
    hook_trigger(l, a, HOOK_CLEANUP);

    cgroup_rmdir(l, cgrp);

    return status_ok(0, NULL, "OK");
}


iot_json_t *application_list(client_t *c, iot_json_t *req)
{
    IOT_UNUSED(c);
    IOT_UNUSED(req);

    return status_error(EOPNOTSUPP, "not implemented");
}






#if 0
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


iot_json_t *application_setup(client_t *c, iot_json_t *req)
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
#endif
