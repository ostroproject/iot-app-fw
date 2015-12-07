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
#include <math.h>
#include <limits.h>

#include <iot/common/mm.h>

#include "generator.h"

static skey_t *keys = NULL;
static int     nkey = 0;


int key_register(skey_t *key)
{
    skey_t *k;

    if (iot_reallocz(keys, nkey, nkey + 1) == NULL)
        return -1;

    k = keys + nkey++;

    k->key     = key->key;
    k->handler = key->handler;

    return 0;
}


key_handler_t key_lookup(const char *key)
{
    skey_t *k;

    for (k = keys; k - keys < nkey; k++)
        if (!strcmp(k->key, key))
            return k->handler;

    return NULL;
}


static int application_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);
    IOT_UNUSED(s);

    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    /*return section_append(&s->unit, "Source", s->appdir);*/
    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static int description_handler(service_t *s, const char *key, iot_json_t *o)
{
    char descr[1024];
    int n;

    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    n = snprintf(descr, sizeof(descr), "Container for %s of provider %s. \\\n"
                 "    %s\n",
                 s->app, s->provider, iot_json_string_value(o));

    if (n < 0 || n >= (int)sizeof(descr))
        goto invalid;

    return section_append(&s->unit, "Description", descr);

 invalid:
    errno = EINVAL;
    return -1;
}


static int user_handler(service_t *s, const char *key, iot_json_t *o)
{
    IOT_UNUSED(key);

    if (iot_json_get_type(o) != IOT_JSON_STRING)
        goto invalid;

    return section_append(&s->service, "User", iot_json_string_value(o));

 invalid:
    errno = EINVAL;
    return -1;
}


static const char *container_path(service_t *s)
{
    static char path[PATH_MAX];
    int n;

    n = snprintf(path, sizeof(path), "%s/%s/%s", PATH_CONTAINER,
                 s->provider, s->app);

    if (n < 0 || n >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    return path;
}


static const char *machine_name(service_t *s)
{
    static char name[128];
    int n;

    n = snprintf(name, sizeof(name), "%s-%s", s->provider, s->app);

    if (n < 0 || n >= (int)sizeof(name)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    return name;
}


static const char *overlay_mount(service_t *s, const char *path, bool rw,
                                 char *buf, size_t size)
{
    int n;

    n = snprintf(buf, size, "--overlay%s=%s:%s%s:%s", rw ? "" : "-ro",
                 path, s->appdir, path, path);

    if (n < 0 || n >= (int)size) {
        errno = EOVERFLOW;
        return NULL;
    }

    return buf;
}


static const char *bind_mount(service_t *s, const char *path, bool rw,
                              char *buf, size_t size)
{
    int n;

    IOT_UNUSED(s);

    n = snprintf(buf, size, "--bind%s=%s", rw ? "" : "-ro", path);

    if (n < 0 || n >= (int)size) {
        errno = EOVERFLOW;
        return NULL;
    }

    return buf;
}


static int execute_nspawn_system(service_t *s, iot_json_t *o)
{
    char cmd[1024], etc[1024], bin[1024], var[1024], usr[1024], *p;
    int  l, n;

    IOT_UNUSED(o);

    if (section_append(&s->service, "Type", "notify") < 0)
        return -1;

    p = cmd;
    l = sizeof(cmd);

    n = snprintf(p, l,
                 "%s\\\n"
                 "    -D %s \\\n"
                 "    -M %s \\\n",
                 PATH_NSPAWN, container_path(s), machine_name(s));

    if (n < 0 || n >= l)
        goto overflow;

    p += n;
    l -= n;

    n = snprintf(p, l,
                 "    %s\\\n"
                 "    %s\\\n"
                 "    %s\\\n"
                 "    %s\\\n"
                 "    --tmpfs=/tmp \\\n"
                 "    --network-veth\\\n",
                 overlay_mount(s, "/etc", true , etc, sizeof(etc)),
                 bind_mount   (s, "/bin", false, bin, sizeof(bin)),
                 overlay_mount(s, "/var", true , var, sizeof(var)),
                 overlay_mount(s, "/usr", false, usr, sizeof(usr)));

    if (n < 0 || n >= l)
        goto overflow;

    return section_append(&s->service, "ExecStart", cmd);

 overflow:
    errno = EOVERFLOW;
    return -1;
}


static int execute_nspawn(service_t *s, iot_json_t *o, bool shared)
{
    char cmd[1024], etc[1024], bin[1024], var[1024], usr[1024], *p, *arg, *t;
    iot_json_t *exec;
    int  l, n, argc, i;

    if (!iot_json_get_array(o, "command", &exec))
        goto invalid;

    if ((argc = iot_json_array_length(exec)) < 1)
        goto invalid;

    if (section_append(&s->service, "Type", "notify") < 0)
        goto fail;

    p = cmd;
    l = sizeof(cmd);

    n = snprintf(p, l,
                 "%s %s\\\n"
                 "    -D %s \\\n"
                 "    -M %s \\\n", PATH_NSPAWN,
                 shared ? "    \\\n--share-system" : "",
                 container_path(s), machine_name(s));

    if (n < 0 || n >= l)
        goto overflow;

    p += n;
    l -= n;

    n = snprintf(p, l,
                 "    %s \\\n"
                 "    %s \\\n"
                 "    %s \\\n"
                 "    %s \\\n"
                 "    --tmpfs=/tmp \\\n"
                 "    --network-veth \\\n",
                 overlay_mount(s, "/etc", true , etc, sizeof(etc)),
                 bind_mount   (s, "/bin", false, bin, sizeof(bin)),
                 overlay_mount(s, "/var", true , var, sizeof(var)),
                 overlay_mount(s, "/usr", false, usr, sizeof(usr)));

    if (n < 0 || n >= l)
        goto overflow;

    p += n;
    l -= n;

    for (i = 0, t = ""; i < argc; i++, t = " ") {
        if (!iot_json_array_get_string(exec, i, &arg))
            goto invalid;

        n = snprintf(p, l, "%s%s", t, arg);

        if (n < 0 || n >= l)
            goto overflow;

        p += n;
        l -= n;
    }

    snprintf(p, l, "\n");

    return section_append(&s->service, "ExecStart", cmd);


 invalid:
    errno = EINVAL;
    return -1;
 overflow:
    errno = EOVERFLOW;
 fail:
    return -1;
}


static int execute_none(service_t *s, iot_json_t *o)
{
    char cmd[1024], lib[1024], path[1024], home[1024], *p, *t, *arg;
    iot_json_t *exec;
    int  l, n, argc, i;

    if (!iot_json_get_array(o, "command", &exec))
        goto invalid;

    if ((argc = iot_json_array_length(exec)) < 1)
        goto invalid;

    n = snprintf(lib, sizeof(lib),
                 "LD_LIBRARY_PATH=%s/usr/lib:%s/usr/lib64:"
                 "/lib:/lib64:/usr/lib:/usr/lib64", s->appdir, s->appdir);

    if (n < 0 || n >= (int)sizeof(lib))
        goto overflow;

    if (section_append(&s->service, "Environment", lib) < 0)
        goto fail;


    n = snprintf(path, sizeof(path), "PATH=%s/usr/bin:%s/usr/sbin:"
                 "/bin:/sbin:/usr/bin:/usr/sbin", s->appdir, s->appdir);

    if (n < 0 || n >= (int)sizeof(path))
        goto overflow;

    if (section_append(&s->service, "Environment", path) < 0)
        goto fail;


    n = snprintf(home, sizeof(home), "HOME=%s/home/%s", s->appdir, s->provider);

    if (n < 0 || n >= (int)sizeof(home))
        goto overflow;

    if (section_append(&s->service, "Environment", home) < 0)
        goto fail;

    p = cmd;
    l = sizeof(cmd);

    for (i = 0, t = ""; i < argc; i++, t = " ") {
        if (!iot_json_array_get_string(exec, i, &arg))
            goto invalid;

        n = snprintf(p, l, "%s%s", t, arg);

        if (n < 0 || n >= l)
            goto overflow;

        p += n;
        l -= n;
    }

    snprintf(p, l, "\n");

    return section_append(&s->service, "ExecStart", cmd);


 invalid:
    errno = EINVAL;
    return -1;
 overflow:
    errno = EOVERFLOW;
 fail:
    return -1;
}


static int execute_handler(service_t *s, const char *k, iot_json_t *o)
{
    const char *type;

    IOT_UNUSED(k);

    if (iot_json_get_type(o) != IOT_JSON_OBJECT)
        goto invalid;

    if (iot_json_get_string(o, "type", &type)) {
        if (!strcmp(type, "nspawn-shared"))
            return execute_nspawn(s, o, true);
        if (!strcmp(type, "nspawn"))
            return execute_nspawn(s, o, false);
        if (!strcmp(type, "nspawn-system"))
            return execute_nspawn_system(s, o);
        if (!strcmp(type, "none"))
            return execute_none(s, o);
    }
    else {
        if (errno == ENOENT)
            return execute_nspawn(s, o, true);
    }

 invalid:
    errno = EINVAL;
    return -1;
}


REGISTER_KEY(application, application_handler);
REGISTER_KEY(description, description_handler);
REGISTER_KEY(user       , user_handler       );
REGISTER_KEY(execute    , execute_handler    );
