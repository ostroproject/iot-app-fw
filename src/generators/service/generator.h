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

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/list.h>
#include <iot/common/json.h>

/* External helper we try to exec(3) for mounting PATH_APPS. */
#ifndef MOUNT_HELPER
#    define MOUNT_HELPER "/usr/libexec/iot-app-fw/mount-apps"
#endif

/* Absolute path to systemd-nspawn. */
#define PATH_NSPAWN "/usr/bin/systemd-nspawn"

/* Directory where applications are installed. */
#define PATH_APPS "/apps"

/* Top directory under which we stitch together container images. */
#define PATH_CONTAINER "/run/systemd/machines"

/* Directory to drop files into for systemd-sysusers. */
#define PATH_SYSUSERS "/usr/lib/sysusers.d"

/* Maximum allowed manifest file size. */
#define MANIFEST_MAXSIZE (16 * 1024)

/* Forward declarations for global types. */
typedef enum   mount_type_e   mount_type_t;
typedef struct mount_s        mount_t;
typedef struct generator_s    generator_t;
typedef struct service_s      service_t;
typedef enum   section_e      section_t;
typedef struct entry_s        entry_t;
typedef struct manifest_key_s manifest_key_t;
typedef struct container_s    container_t;

typedef int (*key_handler_t)(service_t *s, const char *key, iot_json_t *o);
typedef int (*container_handler_t)(service_t *s, const char *key, iot_json_t *o);
typedef int (*emit_t)(int fd, service_t *s, entry_t *e);


/*
 * Generator runtime context.
 *
 * This is used to collect and pass around all the necessary runtime data
 * for discovering applications and generating service files for them.
 */
struct generator_s {
    const char     **env;                /* environment variables */
    const char      *dir_normal;         /* systemd 'normal' service dir */
    const char      *dir_early;          /* systemd 'early' service dir */
    const char      *dir_late;           /* systemd 'late' service dir */
    const char      *dir_apps;           /* application top directory */
    const char      *dir_service;        /* service (output) directory */
    const char      *log_path;           /* where to log to */
    int              dry_run : 1;        /* just a dry-run, don't generate */
    int              premounted : 1;     /* whether dir_apps was mounted */
    int              status;             /* service generation status */
    iot_list_hook_t  services;           /* generated service( file)s */
};


/*
 * Application/container-specific mount.
 *
 * Data structure for administering a bind-, overlay-, or tmpfs-mount
 * specific to a single application/container.
 */
enum mount_type_e {
    MOUNT_TYPE_UNKNOWN,
    MOUNT_TYPE_BIND,
    MOUNT_TYPE_OVERLAY,
    MOUNT_TYPE_TMPFS,
};

struct mount_s {
    iot_list_hook_t  hook;               /* to list of mounts */
    mount_type_t     type;               /* mount type */
    char            *dst;                /* target directory */
    int              rw;                 /* whether to mount read-write */
    union {
        struct {                         /* bind-mount data */
            char    *src;                /*   source directory */
        } bind;
        struct {                         /* overlay-mount data */
            char    *low;                /*   lower layer */
            char    *up;                 /*   upper layer */
        } overlay;
        struct {                         /* tmpfs-mount data */
            mode_t   mode;               /*   access mode */
        } tmpfs;
    };
};

mount_t *service_mount(iot_list_hook_t *l, const char *dst, int rw,
                       int type, ...);

#define mount_bind(_l, _dst, _rw, _src)                 \
    service_mount(_l, _dst, _rw, MOUNT_TYPE_BIND,       \
                  _src ? _src : NULL)

#define mount_overlay(_l, _dst, _rw, _low, _up)         \
    service_mount(_l, _dst, _rw, MOUNT_TYPE_OVERLAY,    \
                  _low ? _low : NULL, _up ? _up : NULL)

#define mount_tmpfs(_l, _dst, _rw, _mode)               \
    service_mount(_l, _dst, _rw, MOUNT_TYPE_TMPFS,      \
                  _mode > 0 ? _mode : 0755)


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
 * Manifest processing, key handlers.
 *
 * Application manifests are processed by going through all keys
 * in a manifest and calling a registered handler for the key.
 * The handler is passed the service file being generated, the
 * key and the value for the key from the manifest. The handler
 * is responsible for generating and eventually emitting the
 * corresponding data to the service file.
 */
struct manifest_key_s {
    const char  *key;
    int        (*handler)(service_t *s, const char *key, iot_json_t *o);
};

iot_json_t *manifest_read(const char *path);
int key_register(manifest_key_t *key);
manifest_key_t *key_lookup(const char *key);

#define REGISTER_KEY(_key, _handler)                       \
    static IOT_INIT void register_##_key##_handler(void) { \
        static manifest_key_t k = {                        \
            .key     = #_key,                              \
            .handler = _handler,                           \
        };                                                 \
                                                           \
        IOT_ASSERT(key_register(&k) == 0,                  \
                   "failed to register key '%s'", #_key);  \
    }                                                      \
    struct __allow_trailing_semicolon


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
    iot_json_t      *m;                  /* application manifest */
    int              fd;                 /* service file */
    const char      *user;               /* user to run service as */
    const char      *group;              /* group to run service as */
    iot_json_t      *argv;               /* user command to execute */
    int              autostart : 1;      /* whether to start on boot */
    iot_list_hook_t  unit;               /* generated 'Unit' section */
    iot_list_hook_t  service;            /* generated 'Service' section */
    iot_list_hook_t  install;            /* generated 'Install' section */
};


/*
 * Service files sections and entries.
 */
enum section_e {
    SECTION_UNIT = 0,
    SECTION_SERVICE,
    SECTION_INSTALL,
};

struct entry_s {
    iot_list_hook_t  hook;               /* to list of section entries */
    char            *key;                /* entry key */
    union {
        char        *value;              /* entry value, or */
        void        *data;               /* data to emit */
    };
    emit_t           emit;               /* custom emitter or NULL */
};

service_t *service_create(generator_t *g, const char *provider, const char *app,
                          const char *dir, const char *src,
                          iot_json_t *manifest);
void service_abort(service_t *s);
int service_generate(generator_t *g);
int service_write(generator_t *g);

int service_add(service_t *s, section_t type, const char *key,
                const char *fmt, ...);

entry_t *section_add(iot_list_hook_t *s, emit_t emit, const char *k,
                     const void *data, ...);
int section_append_value(iot_list_hook_t *s, const char *k,
                         const char *fmt, ...);


/*
 * Handlers for containers of various types.
 *
 * These functions are used to register container types and generate
 * the corresponding sections of systemd service files for applications
 * that reference these containers in their manifests.
 */
struct container_s {
    char *type;
    int (*handler)(service_t *s, const char *type, iot_json_t *o);
};

int container_register(container_t *c);
container_t *container_lookup(const char *type);

#define REGISTER_CONTAINER(_type, _handler)                 \
    static IOT_INIT void register_##_type##_handler(void) { \
        static container_t c = {                            \
            .type    = #_type,                              \
            .handler = _handler,                            \
        };                                                  \
                                                            \
        IOT_ASSERT(container_register(&c) == 0,             \
                   "failed to register container '%s'",     \
                   #_type);                                 \
    }                                                       \
    struct __allow_trailing_semicolon


#endif /* __SERVICE_GENERATOR_H__ */
