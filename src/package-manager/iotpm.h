#ifndef __IOTPM_H__
#define __IOTPM_H__

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include <iot/utils/manifest.h>
#include <iot/utils/identity.h>

#ifndef IOTPM_APPDIR
#define IOTPM_APPDIR             "apps_rw"
#endif

#define IOTPM_PATH_MAX           1024

#define IOTPM_PACKAGE_HOME       "%s/lib"
#define IOTPM_MANIFEST_HOME      IOT_MANIFEST_USER_PATH "/%s"
#define IOTPM_APPLICATION_HOME   "%s/" IOTPM_APPDIR "/%s"

#define IOTPM_DEFAULT_LABEL      "User"

typedef enum   iotpm_mode_e                 iotpm_mode_t;
typedef enum   iotpm_flag_e                 iotpm_flag_t;
typedef struct iotpm_s                      iotpm_t;

typedef struct iotpm_backend_s              iotpm_backend_t;
typedef struct iotpm_pkglist_s              iotpm_pkglist_t;
typedef struct iotpm_pkglist_entry_s        iotpm_pkglist_entry_t;

typedef enum   iotpm_pkginfo_processing_e   iotpm_pkginfo_processing_t;
typedef struct iotpm_pkginfo_filentry_s     iotpm_pkginfo_filentry_t;
typedef struct iotpm_pkginfo_s              iotpm_pkginfo_t;


enum iotpm_mode_e {
    IOTPM_MODE_NONE = 0,
    IOTPM_MODE_POSTINST,
    IOTPM_MODE_PREINST,
    IOTPM_MODE_UPGRADE,
    IOTPM_MODE_REMOVE,
    IOTPM_MODE_DBCHECK,
    IOTPM_MODE_DBPLANT,
    IOTPM_MODE_LIST,
};

enum iotpm_flag_e {
    IOTPM_FLAGS_NONE = 0,
};

struct iotpm_s {
    const char *prognam;
    uid_t userid;
    const char *username;
    const char *homedir;
    int log_mask;
    const char *log_target;
    bool debugging;
    iotpm_backend_t *backend;
    iotpm_mode_t mode;
    iotpm_flag_t flags;
    const char *default_label;  /* default SMACK label for dirs, files */
    int argc;
    char **argv;
};

#endif /* __IOTPM_H__ */
