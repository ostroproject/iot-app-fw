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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>

#include "launcher/daemon/valgrind.h"


void valgrind(const char *vg_path, int argc, char **argv, int vg_offs,
              int saved_argc, char **saved_argv, char **envp)
{
#define VG_PUSH(a) do {                                                   \
        if (vg_argc < (int)IOT_ARRAY_SIZE(vg_argv))                       \
            vg_argv[vg_argc++] = a;                                       \
        else {                                                            \
            iot_log_error("Too many arguments passed through valgrind."); \
        }                                                                 \
    } while(0)

    char *vg_argv[256];
    int   vg_argc, normal_offs, i;

    if (vg_path == NULL)
        vg_path = VALGRIND_PATH;

    /*
     * Construct the argument list using
     *
     *   - the valgrind binary path,
     *   - valgrind options
     *   - executable path and options
     *   - a terminating NULL
     *
     * Save the offset to the executable path in case valgrind is not
     * found and we need to fall back to direct execution.
     */

    vg_argc = 0;
    VG_PUSH((char *)vg_path);

    for (i = vg_offs; i < argc; i++)
        VG_PUSH(argv[i]);
    normal_offs = vg_argc;
    for (i = 0; i < saved_argc; i++)
        VG_PUSH(saved_argv[i]);

    VG_PUSH(NULL);

    iot_log_info("Executing through valgrind ('%s')...", vg_argv[0]);
    execve(vg_argv[0], vg_argv, envp);

    /* fall back... */
    iot_log_error("Valgrind failed (error %d: %s), falling back...",
                  errno, strerror(errno));
    execve(vg_argv[normal_offs], vg_argv + normal_offs, envp);

    iot_log_error("Fallback to normal execution failed (error %d: %s).",
                  errno, strerror(errno));
    exit(1);
}
