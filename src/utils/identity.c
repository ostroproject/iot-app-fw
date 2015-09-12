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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include <iot/common/macros.h>
#include <iot/utils/identity.h>


uid_t iot_get_userid(const char *name)
{
    struct passwd pwd, *found;
    char          buf[4096], *e;
    int           id;

    if (getpwnam_r(name, &pwd, buf, sizeof(buf), &found) == 0)
        return pwd.pw_uid;

    id = strtol(name, &e, 10);

    if (e && !*e)
        return (uid_t)id;

    return (uid_t)-1;
}


const char *iot_get_username(uid_t uid, char *namebuf, size_t size)
{
    struct passwd pwd, *found;
    char          buf[4096];
    int           n;

    if (uid == (uid_t)-1) {
        snprintf(namebuf, size, "<no-user>");
        return "<no-user>";
    }

    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &found) == 0) {
        n = snprintf(namebuf, size, "%s", pwd.pw_name);

        if (n < 0 || n >= (int)size) {
            errno = ENOSPC;
            return NULL;
        }

        return namebuf;
    }

    return NULL;
}


gid_t iot_get_groupid(const char *name)
{
    struct group gr, *found;
    char         buf[4096], *e;
    int          id;

    if (getgrnam_r(name, &gr, buf, sizeof(buf), &found) == 0)
        return gr.gr_gid;

    id = strtol(name, &e, 10);

    if (e && !*e)
        return (gid_t)id;

    return (gid_t)-1;
}


int iot_get_groups(const char *names, gid_t *gids, size_t size)
{
    const char *b, *n, *e;
    char        name[64];
    int         i, l;
    gid_t       gid;

    b = names;
    for (i = 0; b != NULL; i++) {
        while (*b == ' ' || *b == '\t')
            b++;

        n = strchr(b, ',');

        if (n != NULL) {
            e = n;
            while (e > b && (*e == ' ' || *e == '\t'))
                e--;;
            l = e - b + 1;
        }
        else
            l = strlen(n);

        if (l >= (int)sizeof(name)) {
            errno = EINVAL;
            return -1;
        }

        strncpy(name, b, l);
        name[l] = '\0';

        gid = iot_get_groupid(name);

        if (gid == (gid_t)-1) {
            errno = ENOENT;
            return -1;
        }

        if (i < (int)size)
            gids[i] = gid;

        b = n ? n + 1 : NULL;
    }

    return i;
}
