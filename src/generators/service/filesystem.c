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
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <iot/common/file-utils.h>

#include "generator.h"


char *fs_mkpath(char *path, size_t size, const char *fmt, ...)
{
    static char buf[PATH_MAX];
    va_list ap;
    int n;

    if (path == NULL) {
        path = buf;
        size = sizeof(buf);
    }
    else if (size > PATH_MAX)
        size = PATH_MAX;

    va_start(ap, fmt);
    n = vsnprintf(path, size, fmt, ap);
    va_end(ap);

    if (n < 0 || n >= (int)size)
        goto nametoolong;

    return path;

 nametoolong:
    errno = ENAMETOOLONG;
    return NULL;
}


int fs_mkdirp(mode_t mode, const char *fmt, ...)
{
    va_list ap;
    char path[PATH_MAX];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);

    if (n < 0 || n >= (int)sizeof(path))
        goto nametoolong;

    return iot_mkdir(path, mode, NULL);

 nametoolong:
    errno = ENAMETOOLONG;
    return -1;
}


int fs_same_device(const char *path1, const char *path2)
{
    struct stat st1, st2;

    if (stat(path1, &st1) < 0 || stat(path2, &st2) < 0)
        return -1;

    return st1.st_dev == st2.st_dev ? 1 : 0;
}


static int mount_helper(const char *path, const char *action)
{
    pid_t cld;
    int status;

    if (access(MOUNT_HELPER, X_OK) != 0)
        goto fail;

    switch (cld = fork()) {
    case -1:
        goto fail;

    case 0:
        log_debug("Trying to %s '%s' with helper '%s'...", action, path,
                  MOUNT_HELPER);
        exit(execl(MOUNT_HELPER, MOUNT_HELPER, action, path, NULL));

    default:
        if (waitpid(cld, &status, 0) != cld)
            goto invalid;

        if (!WIFEXITED(status))
            goto invalid;

        return WEXITSTATUS(status);
    }

 invalid:
    errno = EINVAL;
 fail:
    return -1;
}


int fs_mount(const char *path)
{
    return mount_helper(path, "mount");
}


int fs_umount(const char *path)
{
    return mount_helper(path, "umount");
}


int fs_symlink(const char *path, const char *dst)
{
    struct stat stp, std;

    if (lstat(path, &stp) < 0)
        return -1;

    if (!S_ISLNK(stp.st_mode))
        return 0;

    if (dst == NULL)
        return 1;

    if (stat(path, &std) < 0)
        return 0;

    if (stat(path, &stp) < 0)
        return -1;

    if (stp.st_dev == std.st_dev && stp.st_ino == std.st_ino)
        return 1;
    else
        return 0;
}


char *fs_service_path(service_t *s, char *path, size_t size)
{
    return fs_mkpath(path, size, "%s/%s-%s.service", s->g->dir_service,
                     s->provider, s->app);
}


char *fs_service_link(service_t *s, char *path, size_t size)
{
    return fs_mkpath(path, size, "%s/applications.target.wants/%s-%s.service",
                     s->g->dir_service, s->provider, s->app);
}
