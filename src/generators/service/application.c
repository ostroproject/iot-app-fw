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
#include <string.h>
#include <limits.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/file-utils.h>
#include <iot/common/json.h>

#include "generator.h"

#define SCAN_CONTINUE  1
#define SCAN_END       0
#define SCAN_ABORT    -1

static char *dir_entry(char *path, size_t size, const char *dir, const char *e)
{
    int n;

    n = snprintf(path, size, "%s/%s", dir, e);

    if (n < 0 || n >= (int)size)
        return NULL;

    return path;
}


static int scan_app_cb(const char *dir, const char *e, iot_dirent_type_t type,
                       void *user_data)
{
    generator_t *g = (generator_t *)user_data;
    char appdir[PATH_MAX], manifest[PATH_MAX], *provider;
    iot_json_t *m;

    IOT_UNUSED(type);

    m = NULL;

    if (dir_entry(appdir, sizeof(appdir), dir, e) == NULL)
        goto out;

    if (dir_entry(manifest, sizeof(manifest), appdir, g->name_manifest) == NULL)
        goto out;

    log_debug("Found manifest '%s'...", manifest);

    m = manifest_read(g, manifest);

    if (m == NULL) {
        if (errno != ENOENT)
            log_error("Failed to open manifest '%s' (%d: %s).", manifest,
                      errno, strerror(errno));

        goto out;
    }

    if ((provider = strrchr(dir, '/')) == NULL)
        goto out;
    else
        provider++;

    if (!service_create(g, provider, e, appdir, manifest, m))
        goto nomem;

    log_info("Found application manifest '%s'...", manifest);

 out:
    return SCAN_CONTINUE;

 nomem:
    iot_json_unref(m);

    return SCAN_ABORT;
}


static int scan_applications(generator_t *g, const char *dir, const char *user)
{
    char path[PATH_MAX], *name;
    int mask;

    snprintf(path, sizeof(path), "%s/%s", dir, user);
    name = "[a-zA-Z0-0_][a-zA-Z0-9_-].*$";
    mask = IOT_DIRENT_DIR | IOT_DIRENT_IGNORE_LNK;

    log_debug("Scanning %s/%s for application manifests...", dir, user);

    iot_scan_dir(path, name, mask, scan_app_cb, g);

    return SCAN_CONTINUE;
}


static int scan_user_cb(const char *dir, const char *e, iot_dirent_type_t type,
                        void *user_data)
{
    generator_t *g = (generator_t *)user_data;
    const char *user = e;

    IOT_UNUSED(type);

    return scan_applications(g, dir, user);
}


static int scan_users(generator_t *g)
{
    const char *name;
    int mask;

    name = "[a-zA-Z0-9_][a-zA-Z0-9_-].*$";
    mask = IOT_DIRENT_DIR | IOT_DIRENT_IGNORE_LNK;

    log_debug("Scanning '%s' for application providers...", g->dir_apps);

    return iot_scan_dir(g->dir_apps, name, mask, scan_user_cb, g);
}


int application_discover(generator_t *g)
{
    return scan_users(g);
}


int application_mount(generator_t *g)
{
    g->premounted = (fs_same_device("/", g->dir_apps) == 0);

    if (!g->premounted)
        return fs_mount(g->dir_apps);
    else
        return 0;
}


int application_umount(generator_t *g)
{
    if (!g->premounted)
        return fs_umount(g->dir_apps);
    else
        return 0;
}

