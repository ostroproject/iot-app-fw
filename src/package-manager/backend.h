#ifndef __IOTPM_BACKEND_H__
#define __IOTPM_BACKEND_H__


#include "pkginfo.h"


struct iotpm_backend_s {
    iotpm_t *iotpm;
    struct {
        const char  *name;
        const char *version;
    } pkgmgr;
    char *dbpath; /* should be 'const char *' but
		     we pass this to argv which is 'char *' :( */
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

#endif /* __IOTPM_BACKEND_H__ */

