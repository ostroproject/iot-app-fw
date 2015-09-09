#ifndef __IOTPM_OPTIONS_H__
#define __IOTPM_OPTIONS_H__

#include "iotpm.h"

bool iotpm_options_init(iotpm_t *iotpm, int argc, char **argv);
void iotpm_options_exit(iotpm_t *iotpm);

#endif	/* __IOTPM_OPTIONS_H__ */
