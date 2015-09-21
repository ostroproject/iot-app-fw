#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <iot/common.h>

#include "manifest.h"


bool iotpm_manifest_init(iotpm_t *iotpm)
{
    char manhome[IOTPM_PATH_MAX];

    if (!iotpm)
        return false;

    if (iot_manifest_set_directories(NULL, IOT_MANIFEST_USER_PATH) < 0) {
        iot_log_error("can't allocate memory for manifest directory names");
        return false;
    }

    snprintf(manhome, sizeof(manhome), IOTPM_MANIFEST_HOME, iotpm->username);

    iot_switch_userid(IOT_USERID_SUID);

    if (iot_mkdir(IOT_MANIFEST_USER_PATH, 0755, "_") < 0)
        goto failed;

    iot_switch_userid(IOT_USERID_REAL);

    if (iot_mkdir(manhome, 0755, "User") < 0)
        goto failed;

    return true;

 failed:
    iot_switch_userid(IOT_USERID_REAL);
    iot_log_error("failed to create manifest home '%s'", manhome);
    return false;
}

void iotpm_manifest_exit(iotpm_t *iotpm)
{
    if (iotpm) {
        iot_manifest_set_directories(NULL, NULL);
    }
}


iot_manifest_t *iotpm_manifest_load(iotpm_t *iotpm,
                                    const char *pkg,
                                    const char *path)
{
    typedef struct {
        int mask;
        const char *problem;
    } err_def_t;

    static err_def_t err_defs[] = {
        { IOT_MANIFEST_MISNAMED,           "misnamed"          },
        { IOT_MANIFEST_UNLOADABLE,         "unloadable"        },
        { IOT_MANIFEST_MALFORMED,          "malformed"         },
        { IOT_MANIFEST_MISSING_FIELD,      "missing field"     },
        { IOT_MANIFEST_INVALID_FIELD,      "invalid field"     },
        { IOT_MANIFEST_INVALID_BINARY,     "invalid field"     },
        { IOT_MANIFEST_INVALID_PRIVILEGE,  "invalid privilege" },
        { IOT_MANIFEST_INVALID_DESKTOP,    "invalid desktop"   },
        {        0,                              NULL          }
    };

    iot_manifest_t *man = NULL;
    const char *manpath;
    int stat;
    err_def_t *ed;
    char error[256], *p, *e, *s;

    if (!iotpm || !path)
        return NULL;

    if (!(man = iot_manifest_get(iotpm->userid, pkg))) {
        snprintf(error, sizeof(error), "failed to load manifest file '%s': %s",
                 path, strerror(errno));
        goto failed;
    }

    if (!(manpath = iot_manifest_path(man)) || strcmp(path, manpath)) {
        snprintf(error, sizeof(error), "internal error: got confused with "
                 "manifest paths ('%s' vs. '%s')",
                 path, manpath ? manpath : "<null>");
        goto failed;
    }

    if ((stat = iot_manifest_validate(man))) {
        e = (p = error) + sizeof(error);

        p += snprintf(p, e-p, "invalid manifest file '%s'", path);

        for (ed = err_defs, s = ":";  ed->mask && p < e;  ed++) {
            if ((stat & ed->mask)) {
                p += snprintf(p, e-p, "%s %s", s, ed->problem);
                s  = ",";
            }
        }

        goto failed;
    }

    return man;

 failed:
    iot_log_error("%s", error);
    if (man)
        iot_manifest_unref(man);
    return NULL;
}

void iotpm_manifest_free(iot_manifest_t *man)
{
    if (man)
        iot_manifest_unref(man);
}
