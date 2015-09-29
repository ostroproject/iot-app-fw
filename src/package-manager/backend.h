#ifndef __IOTPM_BACKEND_H__
#define __IOTPM_BACKEND_H__

#include <iot/common/regexp.h>

#include "pkginfo.h"


struct iotpm_backend_s {
    iotpm_t *iotpm;
    struct {
        const char *name;
        const char *version;
    } pkgmgr;
    struct {
        char *db; /* should be 'const' but we pass this to argv which is not */
        const char *seed;
        const char *manifest;
    } path;
};

struct iotpm_pkglist_s {
    int sts;
    iotpm_backend_t *backend;
    iot_regexp_t *re;
    int nentry;
    iotpm_pkglist_entry_t *entries;
    struct {
        int name;
        int version;
    } max_width;
};

struct iotpm_pkglist_entry_s {
    const char *name;
    const char *version;
    time_t install_time;
};


bool iotpm_backend_init(iotpm_t *iotpm);
void iotpm_backend_exit(iotpm_t *iotpm);

iotpm_pkginfo_t *iotpm_backend_pkginfo_create(iotpm_t *iotpm,
					      bool file,
					      const char *pkg);

void iotpm_backend_pkginfo_destroy(iotpm_pkginfo_t *info);

bool iotpm_backend_install_package(iotpm_t *iotpm, const char *pkg);
bool iotpm_backend_upgrade_package(iotpm_t *iotpm, const char *pkg);
bool iotpm_backend_remove_package(iotpm_t *iotpm, const char *pkg);

bool iotpm_backend_seed_create(iotpm_pkginfo_t *info);
bool iotpm_backend_seed_destroy(iotpm_pkginfo_t *info);
bool iotpm_backend_seed_plant(iotpm_t *iotpm, const char *pkg);

bool iotpm_backend_verify_db(iotpm_t *iotpm);

iotpm_pkglist_t *iotpm_backend_pkglist_create(iotpm_t *iotpm,iot_regexp_t *re);
void iotpm_backend_pkglist_destroy(iotpm_pkglist_t *list);

#endif /* __IOTPM_BACKEND_H__ */

