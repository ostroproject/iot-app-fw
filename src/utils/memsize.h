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

#ifndef __IOT_UTILS_MEMSIZE_H__
#define __IOT_UTILS_MEMSIZE_H__

#include <sys/types.h>
#include <unistd.h>

#include <iot/config.h>
#include <iot/common/macros.h>

IOT_CDECL_BEGIN

/**
 * \addtogroup IoTCommonIfra
 *
 * @{
 */

#define IOT_MEMSIZE_EVENT_BUS             "memsize"
#define IOT_MEMSIZE_EVENT_DONE            "done"

typedef enum   iot_memsize_entry_type_e   iot_memsize_entry_type_t;
typedef struct iot_memsize_s              iot_memsize_t;
typedef struct iot_memsize_entry_s        iot_memsize_entry_t;

enum iot_memsize_entry_type_e {
    IOT_MEMSIZE_TOTAL,
    IOT_MEMSIZE_RESIDENT,
    IOT_MEMSIZE_SHARE,
    IOT_MEMSIZE_TEXT,
    IOT_MEMSIZE_DATA
};


struct iot_memsize_entry_s {
    size_t  min;
    size_t  mean;
    size_t  max;
};


iot_memsize_t *iot_memsize_check_start(pid_t pid,
                                       iot_mainloop_t *ml,
                                       unsigned int interval,
                                       unsigned int duration);

int iot_memsize_check_sample(iot_memsize_t *mem);

int iot_memsize_check_stop(iot_memsize_t *mem);

const char *iot_memsize_exe(iot_memsize_t *mem);
size_t iot_memsize_samples(iot_memsize_t *mem);
double iot_memsize_duration(iot_memsize_t *mem);
iot_memsize_entry_t *iot_memsize(iot_memsize_t *mem,
                                 iot_memsize_entry_type_t type,
                                 iot_memsize_entry_t *entry);

void iot_memsize_free(iot_memsize_t *mem);


/**
 * @}
 */


IOT_CDECL_END

#endif /* __IOT_UTILS_MEMSIZE_H__ */
