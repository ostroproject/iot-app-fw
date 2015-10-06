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
#include <iot/utils/appid.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/transport.h"
#include "launcher/daemon/msg.h"
#include "launcher/daemon/event.h"
#include "launcher/daemon/cgroup.h"
#include "launcher/daemon/privilege.h"

#define STOPPED_EVENT "stopped"


/* list for collecting auto-registered application-handling hooks */
static IOT_LIST_HOOK(hooks);

typedef enum {
    HOOK_INIT = 0,
    HOOK_EXIT,
    HOOK_STARTUP,
    HOOK_CLEANUP
} hook_event_t;

static int application_sigterm(application_t *app);


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

    event_register(STOPPED_EVENT);

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

    if ((argv = iot_allocz_array(char *, argc + 1)) == NULL)
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


static void free_arguments(char **argv)
{
    int i;

    if (argv == NULL)
        return;

    for (i = 0; argv[0] != NULL; i++)
        iot_free(argv[i]);

    iot_free(argv);
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

    a = NULL;

    if (!iot_json_get_string (req, f="manifest", &manifest) ||
        !iot_json_get_string (req, f="app"     , &app     ) ||
        !iot_json_get_integer(req, f="user"    , &uid     ) ||
        !iot_json_get_integer(req, f="group"   , &gid     ) ||
        !iot_json_get_array  (req, f="exec"    , &exec    )) {
        s = msg_status_error(EINVAL, "malformed message, missing field %s", f);
        goto fail;
    }

    m = iot_manifest_read(manifest);

    if (m == NULL) {
        s = msg_status_error(EINVAL, "failed to load manifest '%s'", manifest);
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
        s = msg_status_error(errno, "failed to create cgroup directory");
        goto fail;
    }

    a->id.app  = a->app;
    a->id.cgrp = iot_strdup(dir);

    if (a->id.cgrp == NULL) {
        s = NULL;
        goto fail;
    }

    if (hook_trigger(l, a, HOOK_STARTUP) < 0) {
        s = msg_status_error(errno, "startup hook failed");
        goto fail;
    }

    iot_list_append(&l->apps, &a->hook);

    return msg_status_ok(NULL);

 fail:
    if (a) {
        iot_free(a->app);
        iot_free(a->id.cgrp);
        free_arguments(a->id.argv);
        iot_free(a);
    }

    return s;
}


iot_json_t *application_stop(client_t *c, iot_json_t *req)
{
    launcher_t *l = c->l;
    application_t *app, *a;
    iot_list_hook_t *p, *n;
    const char *appid;
    char pkg[128], id[128];

    if (!iot_json_get_string(req, "app", &appid))
        goto invalid;

    if (iot_appid_parse(appid, NULL, 0, pkg, sizeof(pkg), id, sizeof(id)) < 0)
        goto invalid;

    app = NULL;
    iot_list_foreach(&l->apps, p, n) {
        a = iot_list_entry(p, typeof(*a), hook);

        if (c->id.uid != a->id.uid && c->id.uid != 0)
            continue;

        if (!strcmp(iot_manifest_package(a->m), pkg) && !strcmp(a->id.app, id)) {
            app = a;
            break;
        }
    }

    if (app == NULL)
        goto notfound;

    if (app->killer != 0)
        goto busy;

    app->killer = c->id.pid;
    application_sigterm(app);

    return msg_status_create(0, NULL, "SIGNALLED");

 invalid:
    return msg_status_error(EINVAL, "invalid stop request");

 notfound:
    return msg_status_error(ENOENT, "no such process");

 busy:
    return msg_status_error(EBUSY, "already being stopped");
}


static void application_sigkill(iot_timer_t *t, void *user_data)
{
    application_t *app = (application_t *)user_data;

    IOT_UNUSED(t);

    cgroup_signal(app->l, app->id.cgrp, SIGKILL);
}


static int application_sigterm(application_t *app)
{
    int timeout = 3 * 1000;

    app->stop = iot_timer_add(app->l->ml, timeout, application_sigkill, app);

    if (cgroup_signal(app->l, app->id.cgrp, SIGTERM) == 0 && app->stop != NULL)
        return 0;
    else
        return -1;
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


static void send_stopped_event(application_t *a)
{
    iot_json_t *e;
    char        appid[256];

    e = iot_json_create(IOT_JSON_OBJECT);

    if (e == NULL)
        return;

    snprintf(appid, sizeof(appid), "%s:%s",
             iot_manifest_package(a->m), a->id.app);

    iot_json_add_string (e, "appid", appid);

    event_send(a->l, a->killer, STOPPED_EVENT, e);
}


iot_json_t *application_cleanup(client_t *c, iot_json_t *req)
{
    launcher_t    *l = c->l;
    application_t *a;
    const char    *f;
    const char    *cgrp;

    if (!iot_json_get_string(req, f="cgroup", &cgrp))
        return msg_status_error(EINVAL, "malformed request, missing '%s'", f);
    else
        cgrp++;

    a = application_for_cgroup(l, cgrp);

    if (a == NULL)
        return msg_status_ok(NULL);

    iot_timer_del(a->stop);
    a->stop = NULL;
    iot_list_delete(&a->hook);
    hook_trigger(l, a, HOOK_CLEANUP);

    send_stopped_event(a);

    cgroup_rmdir(l, cgrp);

     return msg_status_ok(NULL);
}


static iot_json_t *list_running(client_t *c)
{
    launcher_t      *l = c->l;
    application_t   *a;
    iot_list_hook_t *p, *n;
    iot_json_t      *apps, *app;
    const char      *descr, *desktop;
    char             appid[1024];

    apps = iot_json_create(IOT_JSON_ARRAY);

    if (apps == NULL)
        return NULL;

    iot_list_foreach(&l->apps, p, n) {
        a = iot_list_entry(p, typeof(*a), hook);

        if (c->id.uid != a->id.uid && c->id.uid != 0)
            continue;

        app = iot_json_create(IOT_JSON_OBJECT);

        if (app == NULL) {
            iot_json_unref(apps);
            return NULL;
        }

        descr   = iot_manifest_description(a->m, a->app);
        desktop = iot_manifest_desktop_path(a->m, a->app);
        snprintf(appid, sizeof(appid), "%s:%s",
                 iot_manifest_package(a->m), a->app);

        iot_json_add_string (app, "app"        , appid);
        iot_json_add_string (app, "description", descr);
        iot_json_add_string (app, "desktop"    , desktop ? desktop : "");
        iot_json_add_integer(app, "user"       , a->id.uid);
        iot_json_add_string_array(app, "argv", a->id.argv, a->id.argc);

        iot_json_array_append(apps, app);
    }

    return msg_status_ok(apps);
}


static iot_json_t *list_installed(client_t *c)
{
    launcher_t         *l = c->l;
    iot_json_t         *apps, *app;
    iot_manifest_t     *m;
    iot_hashtbl_iter_t  it;
    uid_t               uid;
    int                 nmapp, argc, i;
    const char         *mapps[64], *a, *argv[64], *descr, *desktop;
    char                appid[1024];

    IOT_UNUSED(l);

    if (iot_manifest_populate_cache() < 0)
        return NULL;

    apps = iot_json_create(IOT_JSON_ARRAY);

    if (apps == NULL)
        goto out;

    IOT_MANIFEST_CACHE_FOREACH(&it, &m) {
        iot_debug("checking manifest '%s'...", iot_manifest_path(m));

        uid = iot_manifest_user(m);

        if (uid != (uid_t)-1 && c->id.uid != 0 && c->id.uid != uid)
            continue;

        nmapp = iot_manifest_applications(m, mapps, IOT_ARRAY_SIZE(mapps));

        if (nmapp > (int)IOT_ARRAY_SIZE(mapps))
            goto fail;

        for (i = 0; i < nmapp; i++) {
            a   = mapps[i];
            app = iot_json_create(IOT_JSON_OBJECT);

            if (app == NULL)
                goto fail;

            descr   = iot_manifest_description(m, a);
            desktop = iot_manifest_desktop_path(m, a);
            snprintf(appid, sizeof(appid), "%s:%s",
                     iot_manifest_package(m), a);
            argc = iot_manifest_arguments(m, a, argv, IOT_ARRAY_SIZE(argv));

            iot_json_add_string (app, "app"        , appid);
            iot_json_add_string (app, "description", descr);
            iot_json_add_string (app, "desktop"    , desktop ? desktop : "");
            iot_json_add_integer(app, "user"       , uid);
            iot_json_add_string_array(app, "argv", argv, argc);

            iot_json_array_append(apps, app);
        }
    }

    goto out;

 fail:
    iot_json_unref(apps);
    apps = NULL;

 out:
    iot_manifest_reset_cache();

    return apps ? msg_status_ok(apps) : msg_status_error(EINVAL, "failed");
}


iot_json_t *application_list(client_t *c, iot_json_t *req)
{
    launcher_t *l = c->l;
    const char *which;

    if (!iot_json_get_string(req, "type", &which))
        return msg_status_error(EINVAL, "invalid list request");

    if (privilege_check(l, c->id.label, c->id.uid, IOT_PRIV_LIST_APPS) != 1)
        return msg_status_error(EPERM, "permission denied");

    if (!strcmp(which, "running"))
        return list_running(c);

    if (!strcmp(which, "installed"))
        return list_installed(c);

    return msg_status_error(EINVAL, "invalid list request '%s'", which);
}




#if 0
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
