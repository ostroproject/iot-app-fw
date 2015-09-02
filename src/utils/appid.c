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
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <iot/common/macros.h>
#include <iot/utils/appid.h>


int iot_appid_parse(const char *appid, char *usrbuf, size_t usrsize,
                    char *pkgbuf, size_t pkgsize, char *appbuf, size_t appsize)
{
    const char *usr, *pkg, *app;
    int         ul, pl, n;

    usr = appid;
    pkg = strchr(usr, ':');

    if (pkg == NULL) {
        usr = NULL;
        pkg = appid;
        app = appid;
        ul  = 0;
        pl  = strlen(pkg);
    }
    else {
        pkg++;
        app = strchr(pkg, ':');

        if (app == NULL) {
            usr = NULL;
            app = pkg;
            pkg = appid;
            ul  = 0;
            pl  = app - pkg - 1;
        }
        else {
            app++;

            ul = pkg - usr - 1;
            pl = app - pkg - 1;
        }
    }

    if (usrbuf != NULL) {
        if (usr == NULL)
            *usrbuf = '\0';
        else {
            n = snprintf(usrbuf, usrsize, "%*.*s", ul, ul, usr);

            if (n < 0 || n >= (int)usrsize) {
                errno = EOVERFLOW;
                return -1;
            }
        }
    }

    if (pkgbuf != NULL) {
        if (pkg == NULL)
            *pkgbuf = '\0';
        else {
            n = snprintf(pkgbuf, pkgsize, "%*.*s", pl, pl, pkg);

            if (n < 0 || n >= (int)pkgsize) {
                errno = EOVERFLOW;
                return -1;
            }
        }
    }

    if (appbuf != NULL) {
        if (app == NULL)
            *appbuf = '\0';
        else {
            n = snprintf(appbuf, appsize, "%s", app);

            if (n < 0 || n >= (int)appsize) {
                errno = EOVERFLOW;
                return -1;
            }
        }
    }

    return 0;
}


char *iot_appid_user(const char *appid, char *buf, size_t size)
{
    if (iot_appid_parse(appid, buf, size, NULL, 0, NULL, 0) < 0)
        return NULL;
    else
        return buf;
}


char *iot_appid_package(const char *appid, char *buf, size_t size)
{
    if (iot_appid_parse(appid, NULL, 0, buf, size, NULL, 0) < 0)
        return NULL;
    else
        return buf;
}


char *iot_appid_app(const char *appid, char *buf, size_t size)
{
    if (iot_appid_parse(appid, NULL, 0, NULL, 0, buf, size) < 0)
        return NULL;
    else
        return buf;
}

