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

#ifndef __SERVICE_GENERATOR_H__
#define __SERVICE_GENERATOR_H__

#include <syslog.h>
#include <utime.h>
#include <sys/types.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/list.h>
#include <iot/common/json.h>

#include "smpl/smpl.h"

#ifndef LIBDIR
#    define LIBDIR "/usr/lib"
#endif

#ifndef LIBEXECDIR
#    define LIBEXECDIR LIBDIR"/libexec"
#endif

#ifndef SYSCONFDIR
#    define SYSCONFDIR "/etc"
#endif

/* External helper we try to exec(3) for mounting PATH_APPS. */
#ifndef MOUNT_HELPER
#    define MOUNT_HELPER LIBEXECDIR"/iot-app-fw/mount-apps"
#endif

/* Optional generator configuration file. */
#ifndef PATH_CONFIG
#    define PATH_CONFIG SYSCONFDIR"/iot-app-fw/generator.cfg"
#endif

/* Directory for templates. */
#ifndef PATH_TEMPLATE_DIR
#    define PATH_TEMPLATE_DIR LIBEXECDIR"/iot-app-fw"
#endif

/* Service template name. */
#ifndef NAME_TEMPLATE
#    define NAME_TEMPLATE "service.template"
#endif

/* Application manifest name. */
#ifndef NAME_MANIFEST
#    define NAME_MANIFEST "manifest"
#endif

/* Directory where applications are installed. */
#ifndef PATH_APPS
#    define PATH_APPS "/apps"
#endif

#ifndef PATH_SELF
#    define PATH_SELF "/self"
#endif

/* Top directory under which we stitch together container images. */
#ifndef PATH_CONTAINER
#    define PATH_CONTAINER "/run/systemd/machines"
#endif

/* Directory to drop files into for systemd-sysusers. */
#define PATH_SYSUSERS LIBDIR"/sysusers.d"

/* Maximum allowed manifest file size. */
#define MANIFEST_MAXSIZE (16 * 1024)

/* Forward declarations for global types. */
typedef enum   mount_type_e   mount_type_t;
typedef struct mount_s        mount_t;
typedef struct generator_s    generator_t;
typedef struct service_s      service_t;
typedef struct preprocessor_s preprocessor_t;
typedef struct scriptlet_s    scriptlet_t;

typedef iot_json_t *(*preproc_t)(generator_t *g, iot_json_t *json, void *data);

/*
 * Generator runtime context.
 *
 * This is used to collect and pass around all the necessary runtime data
 * for discovering applications and generating service files for them.
 */
struct generator_s {
    const char     **env;                /* environment variables */
    const char      *argv0;              /* argv[0], our binary */
    const char      *dir_normal;         /* systemd 'normal' service dir */
    const char      *dir_early;          /* systemd 'early' service dir */
    const char      *dir_late;           /* systemd 'late' service dir */
    const char      *dir_service;        /* service (output) directory */
    const char      *path_config;        /* configuration path */
    const char      *path_apps;          /* application top dir */
    const char      *path_self;          /* self top directory */
    const char      *path_containers;    /* container root path */
    const char      *path_template;      /* template directory path */
    const char      *name_template;      /* service template name */
    const char      *name_manifest;      /* application manifest name */
    const char      *log_path;           /* where to log to */
    int              dry_run : 1;        /* just a dry-run, don't generate */
    int              update : 1;         /* whether to run in update mode */
    int              premounted : 1;     /* whether dir_apps was mounted */
    int              status;             /* service generation status */
    iot_json_t      *cfg;                /* optional configuration */
    smpl_t          *template;           /* service template */
    iot_list_hook_t  services;           /* generated service( file)s */
    iot_list_hook_t  preprocessors;      /* manifest preprocessors */
    iot_list_hook_t  scriptlets;         /* scriptlet command handlers */
};


/*
 * Logging/debugging macros... just simple wrappers around common.
 */
#define log_open(_path) iot_log_set_target(_path)
#define log_close() do { } while (0)

#define log_error iot_log_error
#define log_warn  iot_log_warning
#define log_info  iot_log_info
#define log_debug iot_debug


/*
 * Configuration handling.
 */
int config_parse_cmdline(generator_t *g, int argc, char *argv[], char *env[]);
int config_file_load(generator_t *g);


/*
 * Filesystem layout, paths, mounting, etc.
 */
char *fs_mkpath(char *path, size_t size, const char *fmt, ...);
int fs_mkdirp(mode_t mode, const char *fmt, ...);
int fs_same_device(const char *path1, const char *path2);
int fs_mount(const char *path);
int fs_umount(const char *path);
int fs_symlink(const char *path, const char *dst);
char *fs_service_path(service_t *s, char *path, size_t size);
char *fs_service_link(service_t *s, char *path, size_t size);
char *fs_firewall_path(service_t *s, char *path, size_t size);

#define fs_accessible(_path, _mode) (access((_path), (_mode)) == 0)
#define fs_readable(_path) fs_accessible(_path, R_OK)
#define fs_writable(_path) fs_accessible(_path, W_OK)
#define fs_execable(_path) fs_accessible(_path, X_OK)


/*
 * Application discovery.
 */
int application_mount(generator_t *g);
int application_umount(generator_t *g);
int application_discover(generator_t *g);


/*
 * Template handling.
 */
int template_config(generator_t *g);
int template_load(generator_t *g);
int template_eval(service_t *s);
void template_destroy(generator_t *g);


/*
 * Manifest handling.
 */

struct preprocessor_s {
    iot_list_hook_t  hook;
    char            *name;
    preproc_t        prep;
    int              prio;
    void            *data;
};

int preprocessor_setup(generator_t *g);
int preprocessor_register(generator_t *g, preprocessor_t *pp);

#define PREPROCESSOR_REGISTER(_name, _prep, _prio, _data)       \
    static void IOT_INIT register_##_prep(void)                 \
    {                                                           \
        static preprocessor_t pp = {                            \
            .name = _name ? _name : #_prep,                     \
            .prio = _prio,                                      \
            .prep = _prep,                                      \
            .data = _data,                                      \
        };                                                      \
        int r;                                                  \
                                                                \
        r = preprocessor_register(NULL, &pp);                   \
                                                                \
        IOT_ASSERT(r >= 0,                                      \
                   "Failed to register preprocessor '%s' "      \
                   "(%d: %s).", _name, errno, strerror(errno)); \
    }                                                           \
    struct __allow_trailing_semicolon



iot_json_t *manifest_read(generator_t *g, const char *path);


/*
 * scriptlet handling
 */

struct scriptlet_s {
    iot_list_hook_t  hook;               /* to list of commands */
    const char      *name;               /* command name */
    int              len;                /* command name length */
    void            *user_data;          /* opaque user data and handler */
    int            (*handler)(generator_t *g, const char *cmd, int len,
                              void *user_data);
};


int scriptlet_register(generator_t *g, scriptlet_t *s);
int scriptlet_run(generator_t *g, char *scriptlet);


/*
 * Service file generation.
 */

/*
 * A systemd service file.
 *
 * Data structure used for collecting the necessary data about an
 * application for generating its systemd service file. The primary
 * source of information for this is the application manifest.
 */
struct service_s {
    iot_list_hook_t  hook;               /* to list of generated services */
    generator_t     *g;                  /* context back pointer */
    char            *provider;           /* application provider */
    char            *app;                /* application name */
    char            *appdir;             /* application directory */
    char            *src;                /* mannifest source path */
    iot_json_t      *m;                  /* application manifest */
    iot_json_t      *data;               /* template configuration data */
    smpl_result_t    result;             /* template generation result */
    iot_json_t      *argv;               /* user command to execute */
    int              autostart : 1;      /* wants started on boot */
    int              firewall : 1;       /* needs firewall manipulation */
};


service_t *service_create(generator_t *g, const char *provider, const char *app,
                          const char *dir, const char *src,
                          iot_json_t *manifest);
void service_abort(service_t *s);
int service_generate(generator_t *g);
int service_write(generator_t *g);
int service_prepare_data(service_t *s);


int self_check_dir(generator_t *g);
int self_generate(generator_t *g);

#endif /* __SERVICE_GENERATOR_H__ */
