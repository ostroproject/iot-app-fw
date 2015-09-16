#ifndef __IOTPM_MANIFEST_H__
#define __IOTPM_MANIFEST_H__

#include <iot/utils/manifest.h>

#include "iotpm.h"


bool iotpm_manifest_init(iotpm_t *iotpm);
void iotpm_manifest_exit(iotpm_t *iotpm);

iot_manifest_t *iotpm_manifest_load(iotpm_t *iotpm, const char *pkg,
                                    const char *path);
void iotpm_manifest_free(iot_manifest_t *man);


#endif	/* __IOTPM_MANIFEST_H__ */
