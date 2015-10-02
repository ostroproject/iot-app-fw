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

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>
#include <iot/common/mask.h>
#include <iot/utils/manifest.h>

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif


#define MAX_EVENTS 1024                  /* max. events to register */


/*
 * launcher daemon runtime context
 */

typedef struct {
    iot_mainloop_t  *ml;                 /* our mainloop */
    iot_transport_t *lnc;                /* launcher transport */
    iot_transport_t *app;                /* IoT app. transport */
    iot_list_hook_t  clients;            /* clients */
    iot_list_hook_t  apps;               /* launched/tracked applications */
    iot_list_hook_t  hooks;              /* application hooks */

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
    void            *cyn;                /* cynara context */
} launcher_t;


/*
 * identification information for an application
 */

#define NO_UID ((uid_t)-1)
#define NO_GID ((gid_t)-1)
#define NO_PID ((pid_t) 0)

typedef struct {
    char   *label;                       /* SMACK label */
    uid_t   uid;                         /* user id (-1 for none) */
    gid_t   gid;                         /* group id (-1 for none) */
    pid_t   pid;                         /* process id (0) */
    char  **argv;                        /* command line arguments */
    int     argc;                        /* argument count */
    char   *cgrp;                        /* IoT cgroup (relative) path */
    char   *app;                         /* <pkg>:<app> */
} identity_t;

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
    identity_t       id;                 /* client identity */
    iot_mask_t       mask;               /* mask of subscribed events */
} client_t;


/*
 * a launched/tracked application
 */

typedef struct {
    iot_list_hook_t  hook;               /* to list of applications */
    launcher_t      *l;                  /* launcher context */
    client_t        *c;                  /* launcher client, if any */
    iot_manifest_t  *m;                  /* application manifest */
    char            *app;                /* application within manifest */
    identity_t       id;                 /* application identity */
    iot_timer_t     *stop;               /* stopping timer */
    pid_t            killer;             /* process that sent stop request */
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
 * application handling hooks
 */

typedef struct {
    iot_list_hook_t  hook;               /* to list of app-hooks */
    const char      *name;               /* descriptive name */
    /* optional hook setup and cleanup callbacks */
    int            (*init)(void);
    void           (*exit)(void);
    /* mandatory application setup and cleanup callbacks */
    int            (*setup)(application_t *app);
    int            (*cleanup)(application_t *app);
} app_hook_t;


#define IOT_REGISTER_APPHOOK(_prfx, _descr, _init, _exit, _setup, _cleanup) \
    static void _prfx##_register(void) IOT_INIT;                        \
                                                                        \
    static void _prfx##_register(void) {                                \
        static app_hook_t h = {                                         \
            .name    = _descr,                                          \
            .init    = _init,                                           \
            .exit    = _exit,                                           \
            .setup   = _setup,                                          \
            .cleanup = _cleanup,                                        \
        };                                                              \
                                                                        \
        application_hook_register(&h);                                  \
    }                                                                   \
    struct __iot_allow_trailing_semicolon


#endif /* __IOT_LAUNCHER_H__ */
