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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#define __USE_GNU
#include <sys/socket.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>
#include <iot/common/json.h>
#include <iot/common/utils.h>

#include "launcher/iot-launch.h"
#include "launcher/daemon/config.h"
#include "launcher/daemon/signal.h"
#include "launcher/daemon/transport.h"
#include "launcher/daemon/application.h"
#include "launcher/daemon/cgroup.h"


static void launcher_init(launcher_t *l)
{
    iot_clear(l);
    iot_list_init(&l->clients);
    iot_list_init(&l->apps);

    l->ml = iot_mainloop_create();
}


static void daemonize(launcher_t *l)
{
    if (l->foreground)
        iot_log_info("Staying in the foreground.");
    else {
        iot_log_info("Switching to daemon mode.");
        iot_daemonize("/", "/dev/null", "/dev/null");
    }
}


int main(int argc, char *argv[], char **envp)
{
    launcher_t l;

    launcher_init(&l);
    signal_init(&l);
    config_parse(&l, argc, argv, envp);
    application_init(&l);
    transport_init(&l);
    cgroup_init(&l);
    daemonize(&l);

    iot_mainloop_run(l.ml);

    cgroup_exit(&l);

    return 0;
}

