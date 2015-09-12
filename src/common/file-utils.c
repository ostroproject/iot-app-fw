/*
 * Copyright (c) 2012-2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/macros.h>
#include <iot/common/debug.h>
#include <iot/common/regexp.h>
#include <iot/common/file-utils.h>


static inline iot_dirent_type_t dirent_type(mode_t mode)
{
#define MAP_TYPE(x, y) if (S_IS##x(mode)) return IOT_DIRENT_##y

    MAP_TYPE(REG, REG);
    MAP_TYPE(DIR, DIR);
    MAP_TYPE(LNK, LNK);
    MAP_TYPE(CHR, CHR);
    MAP_TYPE(BLK, BLK);
    MAP_TYPE(FIFO, FIFO);
    MAP_TYPE(SOCK, SOCK);

    return IOT_DIRENT_UNKNOWN;

#undef MAP_TYPE
}


int iot_scan_dir(const char *path, const char *pattern, iot_dirent_type_t mask,
                 iot_scan_dir_cb_t cb, void *user_data)
{
    DIR               *dp;
    struct dirent     *de;
    struct stat        st;
    iot_regexp_t      *re;
    const char        *prefix;
    char               glob[1024], file[PATH_MAX];
    size_t             size;
    int                status, flags;
    iot_dirent_type_t  type;

    if ((dp = opendir(path)) == NULL)
        return -1;

    if (pattern != NULL) {
        prefix = IOT_PATTERN_GLOB;
        size   = sizeof(IOT_PATTERN_GLOB) - 1;

        if (!strncmp(pattern, prefix, size)) {
            if (iot_regexp_glob(pattern + size, glob, sizeof(glob)) < 0) {
                closedir(dp);
                return -1;
            }

            pattern = glob;
        }
        else {
            prefix = IOT_PATTERN_REGEX;
            size   = sizeof(IOT_PATTERN_REGEX) - 1;

            if (!strncmp(pattern, prefix, size))
                pattern += size;
        }

        flags = IOT_REGEXP_EXTENDED | IOT_REGEXP_NOSUB;
        if ((re = iot_regexp_compile(pattern, flags)) == NULL) {
            closedir(dp);
            return -1;
        }
    }

    status = 1;
    while ((de = readdir(dp)) != NULL && status > 0) {
        if (pattern != NULL && !iot_regexp_matches(re, de->d_name, 0))
            continue;

        snprintf(file, sizeof(file), "%s/%s", path, de->d_name);

        /*
         * Notes: XXX TODO:
         *     I think it would be better to have 3 link-related modes:
         *       1) follow symlinks transparently
         *       2) pass symlinks to the callback
         *       3) ignore symlinks altogether
         *     Now IOT_DIRENT_LINK in mask lets us chose between 1 and 3.
         */
        if (((mask & IOT_DIRENT_LNK ? stat : lstat))(file, &st) != 0)
            continue;

        type = dirent_type(st.st_mode);
        if (!(type & mask))
            continue;

        status = cb(path, de->d_name, type, user_data);
    }

    closedir(dp);
    if (pattern != NULL)
        iot_regexp_free(re);

    if (status < 0) {
        errno = -status;
        return -1;
    }

    return 0;
}


int iot_mkdir(const char *path, mode_t mode)
{
    const char *p;
    char       *q, buf[PATH_MAX];
    int         n, undo[PATH_MAX / 2];
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        errno = path ? EINVAL : EFAULT;
        return -1;
    }

    /*
     * Notes:
     *     Our directory creation algorithm logic closely resembles what
     *     'mkdir -p' does. We simply walk the given path component by
     *     component, testing if each one exist. If an existing one is
     *     not a directory we bail out. Missing ones we try to create with
     *     the given mode, bailing out if we fail.
     *
     *     Unlike 'mkdir -p' whenever we fail we clean up by removing
     *     all directories we have created (or at least we try).
     *
     *     Similarly to 'mkdir -p' we don't try to be overly 'smart' about
     *     the path we're handling. Especially we never try to treat '..'
     *     in any special way. This is very much intentional and the idea
     *     is to let the caller try to create a full directory hierarchy
     *     atomically, either succeeeding creating the full hierarchy, or
     *     none of it. To see the consequences of these design choices,
     *     consider what are the possible outcomes of a call like
     *
     *       iot_mkdir("/home/kli/bin/../sbin/../scripts/../etc/../doc", 0755);
     */

    p = path;
    q = buf;
    n = 0;
    while (1) {
        if (q - buf >= (ptrdiff_t)sizeof(buf) - 1) {
            errno = ENAMETOOLONG;
            goto cleanup;
        }

        if (*p && *p != '/') {
            *q++ = *p++;
            continue;
        }

        *q = '\0';

        iot_debug("checking/creating '%s'...", buf);

        if (q != buf) {
            if (stat(buf, &st) < 0) {
                if (errno != ENOENT)
                    goto cleanup;

                if (mkdir(buf, mode) < 0)
                    goto cleanup;

                undo[n++] = q - buf;
            }
            else {
                if (!S_ISDIR(st.st_mode)) {
                    errno = ENOTDIR;
                    goto cleanup;
                }
            }
        }

        while (*p == '/')
            p++;

        if (!*p)
            break;

        *q++ = '/';
    }

    return 0;

 cleanup:
    while (--n >= 0) {
        buf[undo[n]] = '\0';
        iot_debug("cleaning up '%s'...", buf);
        rmdir(buf);
    }

    return -1;
}


char *iot_normalize_path(char *buf, size_t size, const char *path)
{
    const char *p;
    char       *q;
    int         n, back[PATH_MAX / 2];

    if (path == NULL)
        return NULL;

    if (*path == '\0') {
        if (size > 0) {
            *buf = '\0';
            return buf;
        }
        else {
        overflow:
            errno = ENAMETOOLONG;
            return NULL;
        }
    }

    p   = path;
    q   = buf;
    n   = 0;

    while (*p) {
        if (q - buf + 1 >= (ptrdiff_t)size)
            goto overflow;

        if (*p == '/') {
            back[n++] = q - buf;
            *q++ = *p++;

        skip_slashes:
            while (*p == '/')
                p++;

            /*
             * '.'
             *
             * We skip './' including all trailing slashes. Note that
             * the code is arranged so that whenever we skip trailing
             * slashes, we automatically check and skip also trailing
             * './'s too...
             */

            if (p[0] == '.' && (p[1] == '/' || p[1] == '\0')) {
                p++;
                goto skip_slashes;
            }

            /*
             * '..'
             *
             * We throw away the last incorrectly saved backtracking
             * point (we saved it for this '../'). Then if we can still
             * backtrack, we do so. Otherwise (we're at the beginning
             * already), if the path is absolute, we just ignore the
             * current '../' (can't go above '/'), otherwise we keep it
             * for relative pathes.
             */

            if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
                n--;                                /* throw away */
                if (n > 0) {                        /* can still backtrack */
                    if (back[n - 1] >= 0)           /* previous not a '..' */
                        q = buf + back[n - 1] + 1;
                }
                else {
                    if (q > buf && buf[0] == '/')   /* for absolute pathes */
                        q = buf + 1;                /*     reset to start */
                    else {                          /* for relative pathes */
                        if (q - buf + 4 >= (ptrdiff_t)size)
                            goto overflow;

                        q[0] = '.';                 /*     append this '..' */
                        q[1] = '.';
                        q[2] = '/';
                        q += 3;
                        back[n] = -1;               /*     block backtracking */
                    }
                }

                p += 2;
                goto skip_slashes;
            }
        }
        else
            *q++ = *p++;
    }

    /*
     * finally for other than '/' strip trailing slashes
     */

    if (p > path + 1 && p[-1] != '/')
        if (q > buf + 1 && q[-1] == '/')
            q--;

    *q = '\0';

    return buf;
}
