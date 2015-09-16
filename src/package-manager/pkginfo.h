#ifndef __IOTPM_PKGINFO_H__
#define __IOTPM_PKGINFO_H__

#include "iotpm.h"

enum iotpm_pkginfo_processing_e {
    IOTPM_PROCESSING_NONE = 0,
    IOTPM_PROCESSING_PREIN = 0x01,
    IOTPM_PROCESSING_POSTIN = 0x02,
    IOTPM_PROCESSING_PREUN = 0x04,
    IOTPM_PROCESSING_POSTUN = 0x08,
};


struct iotpm_pkginfo_filentry_s {
    uint32_t flags;
    mode_t  mode;
    const char *path;
    const char *user;
    const char *group;
    const char *link;
};


struct iotpm_pkginfo_s {
    int sts;
    iotpm_backend_t *backend;
    const char *name;
    const char *ver;
    const char *file;
    iotpm_pkginfo_processing_t proc;
    int nfile;
    iotpm_pkginfo_filentry_t *files;
    iotpm_pkginfo_filentry_t *manifest;
    void *data;
    size_t length;
};

bool iotpm_pkginfo_verify(iotpm_pkginfo_t *info);

#endif	/* __IOTPM_PKGINFO_H__ */
