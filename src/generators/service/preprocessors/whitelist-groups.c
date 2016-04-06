/*
 * Copyright (c) 2015-2016, Intel Corporation
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

#include <errno.h>
#include <string.h>
#include <grp.h>

#include <iot/common/debug.h>
#include <iot/generators/service/generator.h>

#define MANIFEST_GROUPS  "groups"
#define WHITELIST_GROUPS "GroupWhitelist"

#ifndef WHITELIST_CONFIG_PATH
#    define WHITELIST_CONFIG_PATH "/etc/iot-app-fw/generator.cfg"
#endif

static iot_json_t *whitelist;


static inline iot_json_t *load_config(void)
{
    return iot_json_load_file(WHITELIST_CONFIG_PATH);
}


static inline int whitelisted(iot_json_t *gl, const char *grp)
{
    const char *wle, *p;
    int         i, l;

    if (gl == NULL)
        return 1;

    l = iot_json_array_length(gl);

    for (i = 0; i < l; i++) {
        if (!iot_json_array_get_string(gl, i, &wle))
            goto invalid;

        iot_debug("Checking whitelist: group '%s',  wl '%s'", grp, wle);

        if (!strcmp(grp, wle))
            return 1;

        if ((p = strchr(wle, '*')) != NULL) {
            if (p[1] != '\0') {
                log_error("Invalid group whitelist entry: '%s'", wle);
                goto invalid;
            }

            if (!strncmp(grp, wle, (int)(p - wle)))
                return 1;
        }
    }

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static inline int group_id(const char *name)
{
    struct group grp, *found;
    char         buf[4096];

    if (getgrnam_r(name, &grp, buf, sizeof(buf), &found) == 0 && found)
        return (int)grp.gr_gid;
    else
        return -1;
}


static iot_json_t *whitelist_groups(generator_t *g, iot_json_t *m, void *data)
{
    static int  loaded = 0;
    iot_json_t *requested, *filtered;
    const char *name;
    int         l, i, n, keep;

    IOT_UNUSED(data);

    filtered = NULL;

    if (!loaded) {
        iot_json_t *cfg = g->cfg ? g->cfg : load_config();

        if (cfg != NULL)
            whitelist = iot_json_get(cfg, WHITELIST_GROUPS);

        loaded = 1;

        if (whitelist != NULL) {
            if (iot_json_get_type(whitelist) != IOT_JSON_ARRAY) {
                whitelist = NULL;
                goto invalid_whitelist;
            }
        }
    }

    requested = iot_json_get(m, MANIFEST_GROUPS);

    if (requested == NULL)
        return m;

    switch (iot_json_get_type(requested)) {
    case IOT_JSON_STRING:
        name = iot_json_string_value(requested);

        keep = whitelisted(whitelist, name);

        if (keep < 0)
            goto invalid_whitelist;

        if (keep == 0)
            log_warn("group '%s' not whitelisted, dropping it...", name);
        else {
            if (group_id(name) < 0) {
                log_warn("group '%s' does not exist, dropping it...", name);
                keep = 0;
            }
        }

        if (keep == 0)
            iot_json_del_member(m, MANIFEST_GROUPS);

        return m;

    case IOT_JSON_ARRAY:
        filtered = iot_json_create(IOT_JSON_ARRAY);
        l = iot_json_array_length(requested);

        for (i = n = 0; i < l; i++) {
            if (!iot_json_array_get_string(requested, i, &name))
                goto invalid_grouplist;

            keep = whitelisted(whitelist, name);

            if (keep < 0)
                goto invalid_whitelist;

            if (keep == 0)
                log_warn("group '%s' not whitelisted, dropping it...", name);
            else {
                log_debug("group '%s' whitelisted...", name);

                if (group_id(name) < 0) {
                    log_warn("group '%s' does not exist, dropping it...", name);
                    keep = 0;
                }
            }

            if (keep > 0) {
                if (!iot_json_array_append_string(filtered, name))
                    goto nomem;

                n++;
            }
        }

        if (n != l) {
            iot_json_del_member(m, MANIFEST_GROUPS);

            if (n > 0)
                iot_json_add(m, MANIFEST_GROUPS, filtered);
        }

        return m;

    default:
        goto invalid_grouplist;
    }

 invalid_whitelist:
    log_error("Invalid group whitelist.");
    errno = EINVAL;
    goto fail;
 invalid_grouplist:
    log_error("Invalid group list in manifest.");
    errno = EINVAL;
 nomem:
 fail:
    iot_json_unref(filtered);
    return NULL;
}


PREPROCESSOR_REGISTER("whitelist-groups", whitelist_groups, 0, NULL);
