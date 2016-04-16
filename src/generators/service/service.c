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
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/mm.h>
#include <iot/common/file-utils.h>

#include "generator.h"


service_t *service_create(generator_t *g, const char *provider, const char *app,
                          const char *dir, const char *src, iot_json_t *manifest)
{
    service_t  *s;
    iot_json_t *o;
    char        path[PATH_MAX];

    s = iot_allocz(sizeof(*s));

    if (s == NULL)
        goto nomem;

    iot_list_init(&s->hook);

    s->g        = g;
    s->m        = manifest;
    s->provider = iot_strdup(provider);
    s->app      = iot_strdup(app);
    s->appdir   = iot_strdup(dir);
    s->src      = iot_strdup(src);
    s->data     = iot_json_create(IOT_JSON_OBJECT);

    if (!s->provider || !s->app || !s->appdir || !s->src || !s->data)
        goto nomem;

    iot_json_add(s->data, "path", o = iot_json_create(IOT_JSON_OBJECT));

    if (o == NULL)
        goto nomem;

    iot_json_add(s->data, "manifest", manifest);

    snprintf(path, sizeof(path), "%s/%s", g->path_template, "/service.template");

    if (!iot_json_add_string(o, "generator", g->argv0))
        goto nomem;
    if (!iot_json_add_string(o, "template", path))
        goto nomem;
    if (!iot_json_add_string(o, "manifest", src))
        goto nomem;
    if (!iot_json_add_string(o, "application", s->appdir))
        goto nomem;
    if (!iot_json_add_string(o, "container", PATH_CONTAINER))
        goto nomem;

    if (!iot_json_add_string(s->data, "provider", s->provider))
        goto nomem;
    if (!iot_json_add_string(s->data, "application", s->app))
        goto nomem;

    iot_list_append(&g->services, &s->hook);

    if (s->g->dry_run)
        strcpy(path, "/proc/self/fd/1");
    else
        fs_service_path(s, path, sizeof(path));

    smpl_init_result(&s->result, path);

    return s;

 nomem:
    if (s) {
        iot_free(s->provider);
        iot_free(s->app);
        iot_free(s->appdir);
        iot_free(s);
    }

    iot_json_unref(manifest);

    return NULL;
}


static int service_link(service_t *s)
{
    char srv[PATH_MAX], lnk[PATH_MAX];

    if (!s->autostart || s->g->dry_run)
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

    if (fs_service_path(s, path, sizeof(path)))
        unlink(path);

    /* XXX TODO unlink also addons */
}


static int service_uptodate(service_t *s)
{
    generator_t *g = s->g;
    char         path[PATH_MAX];
    struct stat  manifest, service;

    if (!g->update)
        return 0;

    if (!fs_service_path(s, path, sizeof(path)))
        return 0;

    if (stat(path, &service) < 0 || stat(s->src, &manifest) < 0)
        return 0;

    return service.st_mtime >= manifest.st_mtime;
}


int service_generate(generator_t *g)
{
    iot_list_hook_t *p, *n;
    service_t *s;

    iot_list_foreach(&g->services, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        if (service_uptodate(s))
            log_info("Skipping up-to-date service file for %s/%s...",
                     s->provider, s->app);
        else {
            log_info("Generating service file for %s/%s...",
                     s->provider, s->app);

            template_eval(s);
        }
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


int service_write(generator_t *g)
{
    iot_list_hook_t *p, *n;
    service_t *s;

    if (service_mkdir(g) < 0)
        return -1;

    iot_list_foreach(&g->services, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        if (smpl_write_result(&s->result, O_CREAT) < 0 || service_link(s) < 0)
            g->status = -1;

        iot_debug("* %s-%s:", s->provider, s->app);
        iot_debug("    autostart: %s", s->autostart ? "yes" : "no");
        iot_debug("    firewall:  %s", s->firewall  ? "yes" : "no");
    }

    return 0;
}
