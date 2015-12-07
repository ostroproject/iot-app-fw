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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "generator.h"


static int mount_helper(generator_t *g, const char *action)
{
    pid_t cld;
    int status;

    switch (cld = fork()) {
    case -1:
        return -1;

    case 0:
        log_debug("Trying to execute '%s %s %s'...",
                  MOUNT_HELPER, action, g->dir_apps);
        exit(execl(MOUNT_HELPER, MOUNT_HELPER, action, g->dir_apps, NULL));
        break;

    default:
        if (waitpid(cld, &status, 0) != cld)
            goto invalid;

        if (!WIFEXITED(status))
            goto invalid;

        return WEXITSTATUS(status);
    }

 invalid:
    errno = EINVAL;
    return -1;
}


static int premount_check(generator_t *g)
{
    struct stat root, apps;

    if (stat("/", &root) < 0 || stat(g->dir_apps, &apps) < 0)
        return -1;

    return (g->apps_premounted = root.st_dev != apps.st_dev);
}


int mount_apps(generator_t *g)
{
    if (premount_check(g))
        return 0;

    return mount_helper(g, "mount");
}


int umount_apps(generator_t *g)
{
    if (g->apps_premounted)
        return 0;

    return mount_helper(g, "umount");
}
