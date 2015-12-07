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
#include <fcntl.h>
#include <limits.h>

#include <iot/common/mm.h>
#include <iot/common/file-utils.h>

#include "generator.h"


static entry_t *entry_create(const char *k, const char *v)
{
    entry_t *e;

    e = iot_allocz(sizeof(*e));

    if (e == NULL)
        goto nomem;

    iot_list_init(&e->hook);
    e->key   = iot_strdup(k);
    e->value = iot_strdup(v);

    if (e->key == NULL || e->value == NULL)
        goto nomem;

    return e;

 nomem:
    if (e) {
        iot_free(e->key);
        iot_free(e->value);
        iot_free(e);
    }

    return NULL;
}


int section_append(iot_list_hook_t *s, const char *k, const char *v)
{
    entry_t *e = entry_create(k, v);

    if (e == NULL)
        return -1;

    iot_list_append(s, &e->hook);

    return 0;
}


int section_prepend(iot_list_hook_t *s, const char *k, const char *v)
{
    entry_t *e = entry_create(k, v);

    if (e == NULL)
        return -1;

    iot_list_prepend(s, &e->hook);

    return 0;
}


int service_append(service_t *s, section_t type, const char *k, const char *v)
{
    iot_list_hook_t *sec;

    switch (type) {
    case SECTION_UNIT:    sec = &s->unit;    break;
    case SECTION_SERVICE: sec = &s->service; break;
    case SECTION_INSTALL: sec = &s->install; break;
    default:              errno = EINVAL;    return -1;
    }

    return section_append(sec, k, v);
}


int service_prepend(service_t *s, section_t type, const char *k, const char *v)
{
    iot_list_hook_t *sec;

    switch (type) {
    case SECTION_UNIT:    sec = &s->unit;    break;
    case SECTION_SERVICE: sec = &s->service; break;
    case SECTION_INSTALL: sec = &s->install; break;
    default:              errno = EINVAL;    return -1;
    }

    return section_prepend(sec, k, v);
}


static int service_process(generator_t *g, service_t *s)
{
    iot_json_t *m = s->m;
    iot_json_iter_t it;
    const char *key;
    iot_json_t *val;
    key_handler_t h;

    if (iot_json_get_type(m) != IOT_JSON_OBJECT)
        goto invalid;

    iot_json_foreach_member(m, key, val, it) {
        h = key_handler(key);

        if (h == NULL)
            goto invalid;

        if (h(s, key, val) != 0)
            g->status = -1;
    }

    section_append(&s->install, "WantedBy", "applications.target");

    return g->status;

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

        log_debug("Generating service file for '%s'...", s->appdir);
        service_process(g, s);
    }

    return g->status;
}


static int service_mkdir(generator_t *g)
{
    char path[PATH_MAX];
    int n;

    n = snprintf(path, sizeof(path), "%s/applications.target.wants",
                 g->dir_service);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    log_debug("Creating directory '%s'...", path);

    if (iot_mkdir(path, 0755, NULL) < 0)
        return -1;

    return 0;
}


static int section_dump(int fd, const char *header, iot_list_hook_t *s)
{
    iot_list_hook_t *p, *n;
    entry_t *e;

    dprintf(fd, "[%s]\n", header);
    iot_list_foreach(s, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        dprintf(fd, "%s = %s\n", e->key, e->value);
    }

    return 0;
}


int service_write(generator_t *g)
{
    iot_list_hook_t *p, *n;
    service_t *s;
    char path[PATH_MAX], lnk[PATH_MAX];
    int l, fd;

    if (service_mkdir(g) < 0)
        return -1;

    iot_list_foreach(&g->services, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        l = snprintf(path, sizeof(path), "%s/%s-%s.service", g->dir_service,
                     s->provider, s->app);

        if (l < 0 || l >= (int)sizeof(path)) {
        skip:
            g->status = -1;
            continue;
        }

        l = snprintf(lnk, sizeof(lnk),
                     "%s/applications.target.wants/%s-%s.service",
                     g->dir_service, s->provider, s->app);

        if (l < 0 || l >= (int)sizeof(lnk))
            goto skip;

        fd = open(path, O_CREAT | O_WRONLY, 0644);

        if (fd < 0)
            goto skip;

        log_debug("Writing service file '%s'...", path);

        section_dump(fd, "Unit", &s->unit);
        section_dump(fd, "Service", &s->service);
        section_dump(fd, "Install", &s->install);

        close(fd);

        if (symlink(path, lnk) < 0)
            g->status = -1;
    }

    return 0;
}
