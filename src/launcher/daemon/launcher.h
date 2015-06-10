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

#ifndef __IOT_LAUNCHER_H__
#define __IOT_LAUNCHER_H__

#include <unistd.h>
#include <limits.h>

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>
#include <iot/common/mask.h>

#include <iot/config.h>


/*
 * launcher daemon runtime context
 */

typedef struct {
    iot_mainloop_t  *ml;                 /* our mainloop */
    iot_transport_t *lnc;                /* launcher transport */
    iot_transport_t *app;                /* IoT app. transport */
    iot_list_hook_t  clients;            /* clients */
    iot_list_hook_t  apps;               /* launched/tracked applications */

    const char      *lnc_addr;           /* launcher transport address */
    const char      *app_addr;           /* IoT app. transport address */
    int              log_mask;           /* what to log */
    const char      *log_target;         /* where to log */
    int              foreground;         /* stay in foreground */
    const char      *cgroot;             /* cgroup fs mount point */
    const char      *cgdir;              /* our cgroup directory */
    const char      *cgagent;            /* our cgroup release agent */
    int              lnc_fd;             /* systemd-passed socket for lnc */
    int              app_fd;             /* systemd-passed socket for app */
} launcher_t;


/*
 * identification information for an application
 */

typedef struct {
    char  *label;                        /* SMACK label */
    char  *appid;                        /* application ID */
    char  *binary;                       /* executable path */
    uid_t  user;                         /* user ID */
    gid_t  group;                        /* (primary) group ID */
    pid_t  process;                      /* process ID */
    char  *cgroup;                       /* IoT cgroup relative path */
} appid_t;


/*
 * an IoT application or launcher client
 */

typedef enum {
    CLIENT_UNKNOWN,
    CLIENT_LAUNCHER,
    CLIENT_IOTAPP
} client_type_t;

typedef struct {
    int              type;               /* client type */
    iot_list_hook_t  hook;               /* to list of clients */
    launcher_t      *l;                  /* launcher context */
    iot_transport_t *t;                  /* transport to this client */
    appid_t          id;                 /* client identification data */
    iot_mask_t       mask;               /* mask of subscribed events */
} client_t;


/*
 * a launched/tracked application
 */

typedef struct {
    iot_list_hook_t  hook;               /* to list of applications */
    launcher_t      *l;                  /* launcher context */
    client_t        *c;                  /* launcher client, if any */
    appid_t          id;                 /* application identification */
    iot_list_hook_t  event;              /* event subscribers */
} application_t;


/*
 * a subscriber listening for events
 */

typedef struct {
    iot_list_hook_t  hook;               /* to event subscriber list */
    iot_mask_t       mask;               /* event mask for this subscriber */
    iot_transport_t *t;                  /* transport for this subscriber */
} subscriber_t;


/*
 * a registered application setup/cleanup handler
 */

typedef struct {
    iot_list_hook_t   hook;              /* to list of handlers */
    /* optional handler setup and cleanup callbacks */
    int             (*init)(void);
    void            (*exit)(void);
    /* mandatory application setup and optional cleanup callbacks */
    int             (*setup)(application_t *app);
    int             (*cleanup)(application_t *app);
} app_handler_t;


#define IOT_REGISTER_APPHANDLER(_prfx, _init, _exit, _setup, _cleanup)  \
    static void _prfx##_register(void) IOT_INIT;                        \
                                                                        \
    static void _prfx##_register(void) {                                \
        static app_handler_t h = {                                      \
            .init    = _init,                                           \
            .exit    = _exit,                                           \
            .setup   = _setup,                                          \
            .cleanup = _cleanup,                                        \
        };                                                              \
                                                                        \
        iot_list_init(&h.hook);                                         \
                                                                        \
        application_register_handler(&h);                               \
    }                                                                   \
    struct __iot_allow_trailing_semicolon



#endif /* __IOT_LAUNCHER_H__ */
