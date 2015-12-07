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
#include <stdarg.h>
#include <math.h>
#include <fcntl.h>
#include <limits.h>

#include <iot/common/mm.h>
#include <iot/common/file-utils.h>

#include "generator.h"


service_t *service_create(generator_t *g, const char *provider, const char *app,
                          const char *dir, const char *src, iot_json_t *manifest)
{
    service_t *s;

    s = iot_allocz(sizeof(*s));

    if (s == NULL)
        goto nomem;

    iot_list_init(&s->hook);
    iot_list_init(&s->unit);
    iot_list_init(&s->service);
    iot_list_init(&s->install);

    s->g        = g;
    s->m        = manifest;
    s->fd       = -1;
    s->provider = iot_strdup(provider);
    s->app      = iot_strdup(app);
    s->appdir   = iot_strdup(dir);

    if (s->provider == NULL || s->app == NULL || s->appdir == NULL)
        goto nomem;

    section_add(&s->unit, NULL, "SourcePath", "%s", src);

    iot_list_append(&g->services, &s->hook);

    return s;

 nomem:
    if (s) {
        iot_free(s->provider);
        iot_free(s->app);
        iot_free(s->appdir);
    }

    iot_json_unref(manifest);

    return NULL;
}


static int service_open(service_t *s)
{
    char path[PATH_MAX];

    if (s->g->dry_run)
        s->fd = dup(fileno(stdout));
    else {
        if (!fs_service_path(s, path, sizeof(path)))
            goto fail;

        log_info("Writing service file %s...", path);

        s->fd = open(path, O_CREAT | O_WRONLY, 0644);
    }

    if (s->fd < 0)
        goto fail;

    return 0;

 fail:
    return -1;
}


static int service_link(service_t *s)
{
    char srv[PATH_MAX], lnk[PATH_MAX];

    if (!s->autostart)
        return 0;

    if (!fs_service_path(s, srv, sizeof(srv)) ||
        !fs_service_link(s, lnk, sizeof(lnk)))
        goto fail;

    if (s->g->dry_run)
        log_debug("Should 'ln -s %s %s' for autostarting...", srv, lnk);
    else {
        log_info("Enabling automatic startup of %s/%s...", s->provider,
                 s->app);

        unlink(lnk);
        if (symlink(srv, lnk) < 0)
            goto fail;
    }

    return 0;

 fail:
    return -1;
}


void service_abort(service_t *s)
{
    char path[PATH_MAX];

    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }

    if (fs_service_path(s, path, sizeof(path)))
        unlink(path);
}


static entry_t *entry_create(emit_t emit, const char *k, const void *data,
                             va_list ap)
{
    entry_t *e;
    char v[4096];
    int n;

    e = NULL;
    v[0] = '\0';

    if (emit == NULL) {
        if (data == NULL)
            goto invalid;

        n = vsnprintf(v, sizeof(v), (const char *)data, ap);

        if (n < 0)
            goto fail;
        if (n >= (int)sizeof(v))
            goto overflow;

        data = iot_strdup(v);

        if (data == NULL)
            goto nomem;
    }

    e = iot_allocz(sizeof(*e));

    if (e == NULL)
        goto nomem;

    iot_list_init(&e->hook);
    e->key   = iot_strdup(k);
    e->data  = (void *)data;
    e->emit  = emit;

    if (e->key == NULL || (emit == NULL && e->data == NULL))
        goto nomem;

    return e;

 invalid:
    errno = EINVAL;
    return NULL;
 overflow:
    errno = EOVERFLOW;
 nomem:
    if (e) {
        iot_free(e->key);
        if (emit == NULL)
            iot_free(e->data);
        iot_free(e);
    }
 fail:
    return NULL;
}


entry_t *section_add(iot_list_hook_t *s, emit_t emit, const char *k,
                     const void *data, ...)
{
    entry_t *e;
    va_list ap;

    va_start(ap, data);
    e = entry_create(emit, k, data, ap);
    va_end(ap);

    if (e == NULL)
        return NULL;

    iot_list_append(s, &e->hook);

    return e;
}


static int service_process(service_t *s)
{
    iot_json_t *m = s->m;
    iot_json_iter_t it;
    const char *key;
    iot_json_t *val;
    manifest_key_t *mk;

    if (iot_json_get_type(m) != IOT_JSON_OBJECT)
        goto invalid;

    iot_json_foreach_member(m, key, val, it) {
        mk = key_lookup(key);

        if (mk == NULL)
            goto invalid;

        if (mk->handler(s, key, val) != 0)
            s->g->status = -1;
    }

    section_add(&s->install, NULL, "WantedBy", "applications.target");

    return s->g->status;

 invalid:
    errno = EINVAL;
    return -1;
}


int service_generate(generator_t *g)
{
    iot_list_hook_t *p, *n;
    service_t *s;

    iot_list_foreach(&g->services, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        log_info("Generating service file for %s/%s...", s->provider, s->app);

        service_process(s);
    }

    return g->status;
}


static int service_mkdir(generator_t *g)
{
    char path[PATH_MAX];
    int n;

    if (g->dry_run)
        return 0;

    n = snprintf(path, sizeof(path), "%s/applications.target.wants",
                 g->dir_service);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    log_debug("Creating directory '%s'...", path);

    return fs_mkdirp(0755, "%s/applications.target.wants", g->dir_service);
}


mount_t *service_mount(iot_list_hook_t *l, const char *dst, int rw,
                       int type, ...)
{
    mount_t *m;
    va_list ap;

    m = iot_allocz(sizeof(*m));

    if (m == NULL)
        goto nomem;

    m->dst = iot_strdup(dst);

    if (m->dst == NULL)
        goto nomem;

    m->type = (mount_type_t)type;
    m->rw   = rw;

    va_start(ap, type);
    switch (m->type) {
    case MOUNT_TYPE_BIND: {
        const char *src = va_arg(ap, const char *);
        m->bind.src = iot_strdup(src);
        if (m->bind.src == NULL && src != NULL)
            goto popandfail;
    }
        break;
    case MOUNT_TYPE_OVERLAY: {
        const char *low = va_arg(ap, const char *);
        const char *up  = va_arg(ap, const char *);

        m->overlay.low = iot_strdup(low);
        m->overlay.up  = iot_strdup(up);
        if ((!m->overlay.low && low) || (!m->overlay.up && up))
            goto popandfail;
    }
        break;
    case MOUNT_TYPE_TMPFS:
        m->tmpfs.mode = va_arg(ap, mode_t);
        break;
    default:
    popandfail:
        errno = EINVAL;
        va_end(ap);
        goto fail;
    }
    va_end(ap);

    iot_list_init(&m->hook);

    if (l != NULL)
        iot_list_append(l, &m->hook);

    return m;

 fail:
 nomem:
    if (m != NULL) {
        iot_free(m->dst);
        iot_free(m->overlay.low);        /* also takes care of m->bind.src */
        iot_free(m->overlay.up);
        iot_free(m);
    }
    return NULL;
}


static int section_dump(service_t *s, iot_list_hook_t *sec, const char *name)
{
    iot_list_hook_t *p, *n;
    entry_t *e;

    dprintf(s->fd, "[%s]\n", name);
    iot_list_foreach(sec, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        if (e->emit == NULL)
            dprintf(s->fd, "%s=%s\n", e->key, e->value);
        else
            e->emit(s->fd, s, e);
    }
    dprintf(s->fd, "\n");

    return 0;
}


static int service_dump(service_t *s)
{
    if (service_open(s) < 0)
        goto fail;

    if (section_dump(s, &s->unit   , "Unit"   ) < 0 ||
        section_dump(s, &s->service, "Service") < 0 ||
        section_dump(s, &s->install, "Install") < 0)
        goto fail;

    if (service_link(s) < 0)
        goto fail;

    return 0;

 fail:
    service_abort(s);
    return -1;
}


int service_write(generator_t *g)
{
    iot_list_hook_t *p, *n;
    service_t *s;

    if (service_mkdir(g) < 0)
        return -1;

    iot_list_foreach(&g->services, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        if (service_dump(s) < 0)
            g->status = -1;
    }

    return 0;
}
