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

#ifdef ENABLE_SECURITY_MANAGER
#  include <sys/smack.h>
#  include <security-manager/security-manager.h>
#endif

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/utils/manifest.h>

#include "package-manager/pkginfo.h"



static int type_id(const char *type)
{
#define MAP(_type, _sm_type) \
    if (!strcmp(type, _type) return SECURITY_MANAGER_PATH_##_sm_type)

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
    uid_t         usr;
    char         *pkg, *app, *type, *fapp;
    char         *apps[256], fqai[256], *prvs[64], *argv[64], *paths[1024];
    int           napp, nprv, argc, npath, i;

    if (pkg == NULL || m == NULL)
        goto invalid;

    usr  = iot_manifest_user(m);
    pkg  = iot_manifest_package(m);
    napp = iot_manifest_applications(m, apps, IOT_ARRAY_SIZE(apps));

    if (usr == (uid_t)-1)
        goto nocommon;

    if (napp < 0)
        goto invalid;
    if (napp >= IOT_ARRAY_SIZE(apps))
        goto overflow;

    for (i = 0; i < (int)napp; i++) {
        app = apps[i];

        if (!iot_application_id(fqai, sizeof(fqai), uid, pkg, app))
            goto invalid;

        npath = 0;
        nprv  = iot_manifest_privileges(m, app, prvs, IOT_ARRAY_SIZE(prvs));
        argv  = iot_manifest_arguments (m, app, argv, IOT_ARRAY_SIZE(argv));

        if (nprv < 0)
            goto invalid;
        if (nprv >= IOT_ARRAY_SIZE(prvs))
            goto overflow;

        if (argc < 0)
            goto invalid;
        if (argc >= IOT_ARRAY_SIZE(argv))
            goto overflow;

        if (security_manager_app_inst_req_new(&req) != 0)
            goto failed;

        if (security_manager_app_inst_req_set_app_id(req, fqai) != 0)
            goto invalid;
        if (security_manager_app_inst_req_set_pkg_id(req, pkg) != 0)
            goto invalid;
        if (security_manager_app_inst_req_set_uid(req, uid) != 0)
            goto invalid;

        for (j = 0; j < nprv; j++)
            if (security_manager_app_inst_req_add_privilege(req, prvs[j]) != 0)
                goto failed;

        for (j = 0; j < pi->nfile; j++) {
            path = pi->files[i].path;

            if (iot_manifest_filetype(m, path, &fapp, &type) < 0)
                goto failed;

            if (!strcmp(fapp, app)) {
                t = type_id(type);
                if (security_manager_app_inst_req_add_path(req, path, t) != 0)
                    goto failed;
            }
        }

        if (security_manager_app_isntall(req) != 0)
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
 failed:
    iot_log_error("Security-manager application installation failed.");
    if (req != NULL)
        security_manager_app_inst_req_free(req);
    return -1;
}


int iotpm_unregister_package(iotpm_pkginfo_t *pi, iot_manifest_t *m)
{
    app_inst_req *req;
    uid_t         usr;
    char         *pkg, *apps[256], *app, fqai[256], *prvs[64], *argv[64];
    int           napp, nprv, argc, i;

    if (pkg == NULL || m == NULL)
        goto invalid;

    usr  = iot_manifest_user(m);
    pkg  = iot_manifest_package(m);
    napp = iot_manifest_applications(m, apps, IOT_ARRAY_SIZE(apps));

    if (usr == (uid_t)-1)
        goto nocommon;

    if (napp < 0)
        goto invalid;
    if (napp >= IOT_ARRAY_SIZE(apps))
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

        if (security_manager_app_uninstall(req))
            iot_log_error("Failed to uninstall application '%s'.", fqai);
        else
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

#endif /* !ENABLE_SECURITY_MANAGER */
