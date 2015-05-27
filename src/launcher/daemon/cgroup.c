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
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/cgroup.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

static int mount_cgdir(launcher_t *l);
static int umount_cgdir(launcher_t *l);
static int cgopen(launcher_t *l, const char *dir, const char *entry, int flags);

int cgroup_init(launcher_t *l)
{
    l->cgroot = CGROUP_ROOT;
    l->cgdir  = CGROUP_ROOT"/"CGROUP_DIR;

    if (mount_cgdir(l) < 0)
        return -1;

    return 0;
}


int cgroup_exit(launcher_t *l)
{
    IOT_UNUSED(l);

    umount_cgdir(l);

    return 0;
}


int cgroup_mkdir(launcher_t *l, uid_t uid, const char *base, pid_t pid,
                 char *idbuf, size_t idsize)
{
    char path[PATH_MAX];
    int  fd, n, u;

    n = snprintf(path, sizeof(path), "%s/user-%u", l->cgdir, uid);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    if (mkdir(path, 0755) < 0 && errno != EEXIST)
        return -1;

    u = n;
    n = snprintf(path + u, sizeof(path) - u, "/%s-%u", base, pid);

    if (n < 0 || n > (int)sizeof(path) - u)
        return -1;

    if (mkdir(path, 0755) < 0)
        goto cleanup;

    if (pid) {
        if ((fd = cgopen(l, path, "tasks", O_WRONLY)) < 0)
            goto cleanup;
        n = dprintf(fd, "%u\n", pid);
        close(fd);

        if (n < 0)
            goto cleanup;
    }

    if (idbuf != NULL) {
        n = snprintf(idbuf, idsize, "%s", path + strlen(l->cgdir) + 1);

        if (n < 0 || n >= (int)idsize)
            return -1;
    }

    return 0;

 cleanup:
    rmdir(path);
    path[u] = '\0';
    rmdir(path);

    return -1;
}


int cgroup_rmdir(launcher_t *l, const char *dir)
{
    char path[PATH_MAX];
    int  n;

    n = snprintf(path, sizeof(path), "%s/%s", l->cgdir, dir);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    if (rmdir(path) < 0)
        return -1;

    return 0;
}


static int rwmount(const char *path)
{
    unsigned long  flags = MS_REMOUNT|MS_NOSUID|MS_NODEV|MS_NOEXEC;
    const void    *data  = "mode=755";

    iot_log_info("Remounting %s as read-write...", path);

    return mount(NULL, path, "cgroup", flags, data);
}


static int romount(const char *path)
{
    unsigned long  flags = MS_REMOUNT|MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC;
    const void    *data  = "mode=755";

    iot_log_info("Remounting %s as read-only...", path);

    return mount(NULL, path, "cgroup", flags, data);
}


static int mkcgdir(launcher_t *l)
{
    int r;

    if (rwmount(l->cgroot) < 0)
        return -1;

    iot_log_info("Creating cgroup directory %s...", l->cgdir);
    r = mkdir(l->cgdir, 0755);

    romount(l->cgroot);

    return r;
}


static int rmcgdir(launcher_t *l)
{
    int r;

    if (rwmount(l->cgroot) < 0)
        return -1;

    iot_log_info("Removing cgroup directory %s...", l->cgdir);
    r = rmdir(l->cgdir);

    romount(l->cgroot);

    return r;
}


static int cgopen(launcher_t *l, const char *dir, const char *entry, int flags)
{
    char path[PATH_MAX];
    int  n;

    n = snprintf(path, sizeof(path), "%s/%s", dir ? dir : l->cgdir, entry);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    return open(path, flags);
}


static int mount_cgdir(launcher_t *l)
{
    unsigned long flags = MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_RELATIME;
    char          data[PATH_MAX + 64];
    int           fd, n;

    if (mkcgdir(l) < 0)
        return -1;

    n = snprintf(data, sizeof(data), CGROUP_DATA",release_agent=%s", l->cgagent);

    if (n < 0 || n >= (int)sizeof(data))
        goto removedir;

    if (mount("cgroup", l->cgdir, "cgroup", flags, data) < 0)
        goto removedir;

    if ((fd = cgopen(l, NULL, "notify_on_release", O_WRONLY)) < 0)
        goto removedir;
    n = write(fd, "1\n", 2);
    close(fd);

    if (n != 2)
        goto removedir;

    return 0;

 removedir:
    iot_log_error("Failed to mount %s with agent %s...", l->cgdir, l->cgagent);
    rmcgdir(l);
    return -1;
}


static int umount_cgdir(launcher_t *l)
{
    if (umount(l->cgdir) < 0)
        return -1;

    if (rmcgdir(l) < 0)
        return -1;

    return 0;
}


char *cgroup_path(char *buf, size_t size, const char *name, pid_t pid)
{
    char    cgroup[PATH_MAX], entry[1024], *p, *r;
    FILE   *fp;
    size_t  len;

    snprintf(cgroup, sizeof(cgroup), "/proc/%u/cgroup", pid);

    fp = fopen(cgroup, "r");

    if (fp == NULL)
        return NULL;

    len = strlen(name);
    r   = NULL;

    while (fgets(entry, sizeof(entry), fp) != NULL) {
        p = strstr(entry, ":name=");

        if (p == NULL)
            continue;

        p += 6;

        if (!strncmp(p, name, len) && p[len] == ':') {
            if (snprintf(buf, size, "%s", p + len + 1) < (int)size)
                r = buf;
            break;
        }
    }

    fclose(fp);

    return r;
}
