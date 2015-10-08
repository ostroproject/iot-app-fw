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

#include "launcher/daemon/launcher.h"
#include "launcher/daemon/privilege.h"

#ifdef ENABLE_SECURITY_MANAGER

#include <cynara/cynara-client.h>

int privilege_init(launcher_t *l)
{
    if (l->cyn != NULL)
        return 0;

    if (cynara_initialize((cynara **)&l->cyn, NULL) != CYNARA_API_SUCCESS)
        return -1;

    return 0;
}


void privilege_exit(launcher_t *l)
{
    if (l->cyn != NULL)
        cynara_finish(l->cyn);

    l->cyn = NULL;
}


int privilege_check(launcher_t *l, const char *label, uid_t uid,
                    const char *privilege)
{
    char user[128];
    int  ok = CYNARA_API_ACCESS_ALLOWED;

    if (l->cyn == NULL)
        goto nocynara;

    iot_debug("checking acces from cynara: %s, %d, %s", label, uid, privilege);

    if (snprintf(user, sizeof(user), "%d", uid) < 0)
        return -1;

    if (cynara_simple_check(l->cyn, label, "connection", user, privilege) == ok)
        return 1;
    else
        return 0;

 nocynara:
    errno = ENOTCONN;
    return -1;
}

#else /* !ENABLE_SECURITY_MANAGER */

int privilege_init(launcher_t *l)
{
    l->cyn = NULL;

    return 0;
}


void privilege_exit(launcher_t *l)
{
    l->cyn = NULL;
}


int privilege_check(launcher_t *l, const char *label, uid_t uid,
                    const char *privilege)
{
    IOT_UNUSED(l);
    IOT_UNUSED(label);
    IOT_UNUSED(uid);
    IOT_UNUSED(privilege);

    return 1;
}

#endif /* !ENABLE_SECURITY_MANAGER */

