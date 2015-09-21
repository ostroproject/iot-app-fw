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
#include <limits.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/utils/manifest.h>
#include <iot/utils/identity.h>

#ifdef ENABLE_SECURITY_MANAGER
#  include <sys/smack.h>
#  include <security-manager/security-manager.h>
#endif

#include "package-manager/pkginfo.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

#ifdef ENABLE_SECURITY_MANAGER

static int type_id(const char *type)
{
#define MAP(_type, _sm_type) \
    if (!strcmp(type, _type)) return SECURITY_MANAGER_PATH_##_sm_type

    MAP("private"  , PRIVATE  );
    MAP("public"   , PUBLIC   );
    MAP("public-ro", PUBLIC_RO);
    MAP("rw"       , RW       );
    MAP("ro"       , RO       );

    return SECURITY_MANAGER_PATH_PRIVATE;
}


int iotpm_register_package(iotpm_pkginfo_t *pi, iot_manifest_t *m)
{
    app_inst_req *req;
    uid_t         uid;
    const char   *pkg, *app, *type, *fapp;
    const char   *apps[256], *prvs[64], *argv[64], *path;
    char          fqai[256], home[PATH_MAX], pkgdir[PATH_MAX];
    int           napp, nprv, argc, dirlen, i, j, t;
    int           se = 0;

    req = NULL;

    if (pi == NULL || m == NULL)
        goto invalid;

    uid  = iot_manifest_user(m);
    pkg  = iot_manifest_package(m);
    napp = iot_manifest_applications(m, apps, IOT_ARRAY_SIZE(apps));

    if (uid == (uid_t)-1)
        goto nocommon;

    if (napp < 0)
        goto invalid;
    if (napp >= (int)IOT_ARRAY_SIZE(apps))
        goto overflow;

    if (!iot_get_userhome(uid, home, sizeof(home)))
        goto invalid;

    dirlen = snprintf(pkgdir, sizeof(pkgdir), "%s/%s", home, pkg);
    if (dirlen < 0 || dirlen >= sizeof(pkgdir))
        goto invalid;

    iot_debug("user %u, package %s, top directory: '%s'", uid, pkg, pkgdir);

    for (i = 0; i < (int)napp; i++) {
        app = apps[i];

        if (!iot_application_id(fqai, sizeof(fqai), uid, pkg, app))
            goto invalid;

        nprv = iot_manifest_privileges(m, app, prvs, IOT_ARRAY_SIZE(prvs));
        argc = iot_manifest_arguments (m, app, argv, IOT_ARRAY_SIZE(argv));

        if (nprv < 0)
            goto invalid;
        if (nprv >= (int)IOT_ARRAY_SIZE(prvs))
            goto overflow;

        if (argc < 0)
            goto invalid;
        if (argc >= (int)IOT_ARRAY_SIZE(argv))
            goto overflow;

        if ((se = security_manager_app_inst_req_new(&req)) != 0)
            goto failed;

        iot_debug("registering %s:%s", pkg, app);

        iot_debug("    app id '%s'...", fqai);
        if ((se = security_manager_app_inst_req_set_app_id(req, fqai)) != 0)
            goto invalid;

        iot_debug("    pkg '%s'...", pkg);
        if ((se = security_manager_app_inst_req_set_pkg_id(req, pkg)) != 0)
            goto invalid;

        iot_debug("    user id %d...", uid);
        if ((se = security_manager_app_inst_req_set_uid(req, uid)) != 0)
            goto invalid;

        for (j = 0; j < nprv; j++) {
            iot_debug("    privilege '%s'...", prvs[j]);
            se = security_manager_app_inst_req_add_privilege(req, prvs[j]);
            if (se != 0)
                goto failed;
        }

        for (j = 0; j < pi->nfile; j++) {
            path = pi->files[i].path;

            iot_debug("    checking file '%s'....", pi->files[i].path);

            if (strncmp(path, pkgdir, dirlen) != 0 ||
                (path[dirlen] != '/' && path[dirlen] != '\0')) {
                iot_debug("      non-package path... ignored", path);
                continue;
            }

            if (iot_manifest_filetype(m, path, &fapp, &type) < 0)
                goto failed;

            iot_debug("        type '%s', app '%s'...", type, fapp);

            if (!strcmp(fapp, app)) {
                t = type_id(type);
                iot_debug("    registering path as type %d...", t);
                se = security_manager_app_inst_req_add_path(req, path, t);
                if (se != 0)
                    goto failed;
            }
        }

        iot_debug("    registering with security framework");

        iot_switch_userid(IOT_USERID_SUID);
        se = security_manager_app_install(req);
        iot_switch_userid(IOT_USERID_REAL);

        if (se != 0)
            goto failed;

        security_manager_app_inst_req_free(req);
        req = NULL;
    }

    return 0;

 nocommon:
    iot_log_error("Common applications are unsupported.");
 invalid:
    errno = EINVAL;
    goto failed;
 overflow:
    errno = EOVERFLOW;
    goto failed;

 failed:
    iot_log_error("Security-manager application installation failed.");

    if (se != 0) {
        iot_log_error("Security-framework error: %d (%s).", se,
                      security_manager_strerror(se));
        errno = EACCES;
    }

    if (req != NULL)
        security_manager_app_inst_req_free(req);

    return -1;
}


int iotpm_unregister_package(iotpm_pkginfo_t *pi, iot_manifest_t *m)
{
    app_inst_req *req;
    uid_t         uid;
    const char   *pkg, *apps[256], *app;
    char          fqai[256];
    int           napp, i;
    int           se = 0;

    IOT_UNUSED(pi);

    req = NULL;

    if (pi == NULL || m == NULL)
        goto invalid;

    uid  = iot_manifest_user(m);
    pkg  = iot_manifest_package(m);
    napp = iot_manifest_applications(m, apps, IOT_ARRAY_SIZE(apps));

    if (uid == (uid_t)-1)
        goto nocommon;

    if (napp < 0)
        goto invalid;
    if (napp >= (int)IOT_ARRAY_SIZE(apps))
        goto overflow;

    for (i = 0; i < (int)napp; i++) {
        app = apps[i];

        if (!iot_application_id(fqai, sizeof(fqai), uid, pkg, app))
            goto invalid;

        if (security_manager_app_inst_req_new(&req) != 0)
            goto failed;

        if (security_manager_app_inst_req_set_app_id(req, fqai) != 0)
            goto failed;

        if (security_manager_app_inst_req_set_pkg_id(req, pkg) != 0)
            goto failed;

        if (security_manager_app_inst_req_set_uid(req, uid) != 0)
            goto failed;

        iot_switch_userid(IOT_USERID_SUID);
        se = security_manager_app_uninstall(req);
        iot_switch_userid(IOT_USERID_REAL);

        if (se != 0) {
            iot_log_error("Failed to uninstall application '%s'.", fqai);
            goto failed;
        }

        iot_log_info("Unregistered application '%s'.", fqai);

        security_manager_app_inst_req_free(req);
        req = NULL;
    }

    return 0;

 nocommon:
    iot_log_error("Common applications are unsupported.");
 invalid:
    errno = EINVAL;
    goto failed;
 overflow:
    errno = EOVERFLOW;
 failed:
    if (req != NULL)
        security_manager_app_inst_req_free(req);
    return -1;
}


#else /* !ENABLE_SECURITY_MANAGER */

int iotpm_register_package(iotpm_pkginfo_t *pi, iot_manifest_t *m)
{
    IOT_UNUSED(pi);
    IOT_UNUSED(m);

    return 0;
}

int iotpm_unregister_package(iotpm_pkginfo_t *pi, iot_manifest_t *m)
{
    IOT_UNUSED(pi);
    IOT_UNUSED(m);

    return 0;
}

#endif /* !ENABLE_SECURITY_MANAGER */
