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
#include <limits.h>

#include <iot/common/mm.h>

#include "generator.h"

#define RW 1
#define RO 0

typedef struct {
    const char      *type;               /* nspawn type */
    int              shared : 1;         /* '--shared-system' container */
    iot_list_hook_t  mounts;             /* container mounts */
    const char      *net;                /* network configuration */
} nspawn_t;



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


static int emit_prepare_directories(int fd, service_t *s, const char *cpath)
{
    int sbin, bin, lib, lib64;

    if (cpath == NULL)
        cpath = container_path(s);

    sbin  = !fs_symlink("/sbin" , NULL);
    bin   = !fs_symlink("/bin"  , NULL);
    lib   = !fs_symlink("/lib"  , NULL);
    lib64 = !fs_symlink("/lib64", NULL);

    dprintf(fd, "ExecStartPre="
            "/bin/mkdir -p %s/dev/../sys/../proc/.."
            "/etc/../usr/../var/../run/../tmp/../home/../root"
            "%s%s%s%s\n", cpath,
            sbin  ? "/../sbin"  : "",
            bin   ? "/../bin"   : "",
            lib   ? "/../lib"   : "",
            lib64 ? "/../lib64" : "");

    if (!sbin)
        dprintf(fd, "ExecStartPre="
                "/bin/ln -sf usr/sbin %s/sbin\n", cpath);

    if (!bin)
        dprintf(fd, "ExecStartPre="
                "/bin/ln -sf usr/bin %s/bin\n", cpath);

    if (!lib)
        dprintf(fd, "ExecStartPre="
                "/bin/ln -sf usr/lib %s/lib\n", cpath);

    if (!lib64)
        dprintf(fd, "ExecStartPre="
                "/bin/ln -sf usr/lib64 %s/lib64\n", cpath);

    return 0;
}


static int emit_machine_name(int fd, service_t *s)
{
   dprintf(fd, "    -M %s \\\n", machine_name(s));
   return 0;
}


static int emit_container_dir(int fd, service_t *s)
{
    dprintf(fd, "    -D %s \\\n", container_path(s));
    return 0;
}


static int emit_user(int fd, service_t *s, bool nspawn)
{
    if (s->user == NULL)
        return 0;

    if (nspawn)
        dprintf(fd, "    --user=%s \\\n", s->user);
    else
        dprintf(fd, "User=%s\n", s->user);

    return 0;
}


static int emit_group(int fd, service_t *s)
{
    if (s->group == NULL)
        return 0;

    dprintf(fd, "Group=%s\n", s->group);

    return 0;
}


static int emit_mounts(int fd, service_t *s, iot_list_hook_t *l)
{
    iot_list_hook_t *p, *n;
    mount_t *m;

    iot_list_foreach(l, p, n) {
        m = iot_list_entry(p, typeof(*m), hook);

        switch(m->type) {
        case MOUNT_TYPE_BIND: {
            const char *src = m->bind.src ? m->bind.src : m->dst;
            dprintf(fd, "    --bind%s=%s:%s \\\n", m->rw ? "" : "-ro",
                    m->dst, src);
        }
            break;

        case MOUNT_TYPE_OVERLAY: {
            const char *low = m->overlay.low ? m->overlay.low : m->dst;
            const char *up  = m->overlay.up  ? m->overlay.up  : m->dst;
            dprintf(fd, "    --overlay%s=%s:%s%s:%s \\\n", m->rw ? "" : "-ro",
                    m->dst, s->appdir, low, up);
        }
            break;

        case MOUNT_TYPE_TMPFS:
            dprintf(fd, "    --tmpfs=%s:mode=0%o \\\n", m->dst, m->tmpfs.mode);
            break;

        default:
            break;
        }
    }

    return 0;
}


static int emit_network(int fd, nspawn_t *n)
{
    const char *type = n->net;
    const char *arg;

    if (!strcmp(type, "veth") || !strcasecmp(type, "VirtualEthernet"))
        arg = "--network-veth --auto-dhcp";
    else
        goto invalid;

    dprintf(fd, "    %s \\\n", arg);

    return 0;

 invalid:
    return -1;
}


static int emit_command(int fd, iot_json_t *argv, bool nspawn)
{
    int i;
    const char *arg, *t;
    int argc;

    argc = argv ? iot_json_array_length(argv) : 0;

    if (argc > 0) {
        for (i = 0, t = nspawn ? "    " : ""; i < argc; i++, t = " ") {
            if (!iot_json_array_get_string(argv, i, &arg))
                goto fail;
            dprintf(fd, "%s%s", t, arg);
        }
    }
    else
        dprintf(fd, "    --boot");

    return 0;

 fail:
    return -1;
}


static int emit_extend_environment(int fd, service_t *s)
{
    dprintf(fd, "Evironment="
            "LD_LIBRARY_PATH=%s/usr/lib:%s/usr/lib64:"
            "/lib:/lib64:/usr/lib:/Usr/lib64\n", s->appdir, s->appdir);

    dprintf(fd, "Environment="
            "PATH=%s/usr/bin:%s/usr/sbin:"
            "/bin:/sbin:/usr/bin:/usr/sbin\n", s->appdir, s->appdir);

    dprintf(fd, "Environment=HOME=%s/home/%s\n", s->appdir, s->provider);

    return 0;
}


static int emit_nspawn(int fd, service_t *s, entry_t *e)
{
    nspawn_t *n = (nspawn_t *)e->data;

    emit_group(fd, s);
    emit_prepare_directories(fd, s, NULL);

    dprintf(fd, "Type=notify\n");
    dprintf(fd, "ExecStart=%s%s \\\n",
            PATH_NSPAWN, n->shared ? " --share-system" : "");

    emit_machine_name(fd, s);
    emit_container_dir(fd, s);
    emit_user(fd, s, true);
    emit_mounts(fd, s, &n->mounts);
    emit_network(fd, n);
    emit_command(fd, s->argv, true);
    dprintf(fd, "\n");

    return 0;
}


static int emit_none(int fd, service_t *s, entry_t *e)
{
    IOT_UNUSED(e);

    emit_user(fd, s, false);
    emit_group(fd, s);
    emit_extend_environment(fd, s);

    dprintf(fd, "Type=simple\n");
    dprintf(fd, "ExecStart=");
    emit_command(fd, s->argv, false);
    dprintf(fd, "\n");

    return 0;
}


static int nspawn_handler(service_t *s, const char *type, iot_json_t *o)
{
#define BIND_MOUNT(_p, _ro)         \
    (fs_symlink(_p, NULL) || mount_bind(&n->mounts, _p, _ro, NULL))
#define OVERLAY_MOUNT(_p, _ro)      \
    mount_overlay(&n->mounts, _p, _ro, NULL, NULL)
#define TMPFS_MOUNT(_p, _ro, _mode) \
    mount_tmpfs(&n->mounts, _p, _ro, _mode)

    nspawn_t *n;
    iot_json_t *shared, *net;

    IOT_UNUSED(type);

    n = iot_allocz(sizeof(*n));

    if (n == NULL)
        goto fail;

    iot_list_init(&n->mounts);
    n->type   = "nspawn";

    shared = iot_json_get(o, "sharedsystem");
    net    = iot_json_get(o, "network");

    if ((shared != NULL && iot_json_get_type(shared) != IOT_JSON_BOOLEAN) ||
        (net    != NULL && iot_json_get_type(net)    != IOT_JSON_STRING))
        goto invalid;

    if (!BIND_MOUNT("/lib", RO) || !BIND_MOUNT("/lib64", RO) ||
        !BIND_MOUNT("/bin", RO) || !BIND_MOUNT("/sbin" , RO))
        goto fail;

    if (!OVERLAY_MOUNT("/etc", RW) || !OVERLAY_MOUNT("/usr", RO) ||
        !OVERLAY_MOUNT("/var", RW) || !TMPFS_MOUNT  ("/tmp", RW, 0755))
        goto fail;

    n->net    = net    ? iot_json_string_value(net)     : "VirtualEthernet";
    n->shared = shared ? iot_json_boolean_value(shared) : 0;

    if (section_add(&s->service, emit_nspawn, "ExecStart", n) == NULL)
        goto fail;

    return 0;

 invalid:
    errno = EINVAL;
 fail:
    return -1;

#undef BIND_MOUNT
#undef OVERLAY_MOUNT
#undef TMPFS_MOUNT
}


static int none_handler(service_t *s, const char *type, iot_json_t *o)
{
    nspawn_t *n;

    IOT_UNUSED(type);
    IOT_UNUSED(o);

    n = iot_allocz(sizeof(*n));

    if (n == NULL)
        goto fail;

    iot_list_init(&n->mounts);
    n->type   = "none";
    n->shared = 0;

    if (section_add(&s->service, emit_none, "ExecStart", n) == NULL)
        goto fail;

    return 0;

 fail:
    return -1;
}


REGISTER_CONTAINER(nspawn, nspawn_handler);
REGISTER_CONTAINER(none  , none_handler  );
