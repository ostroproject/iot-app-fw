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

#ifndef __CONTAINER_GENERATOR_H__
#define __CONTAINER_GENERATOR_H__

#include <syslog.h>

#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/json.h>

/** External mount helper to mount IOT_DIR_APPS if necessary. */
#ifndef MOUNT_HELPER
#    define MOUNT_HELPER "/usr/libexec/iot-app-fw/mount-apps"
#endif

/** Path to systemd-nspawn. */
/*#define PATH_NSPAWN "/usr/bin/systemd-nspawn"*/
#define PATH_NSPAWN "/root/rpmbuild/BUILD/systemd-222/systemd-nspawn"

/** Directory where applications are installed. */
#define PATH_APPS "/apps"

/** The directory where we stitch together container images. */
#define PATH_CONTAINER "/run/systemd/machines"

/** Absolute path where we can drop *.conf files requesting user creation. */
#define PATH_SYSUSERS "/usr/lib/sysusers.d"

/** Maximum allowed manifest file size. */
#define MANIFEST_MAXSIZE (16 * 1024)

/** Forward for declarations for a few necessary types. */
typedef struct generator_s   generator_t;
typedef struct service_s     service_t;
typedef enum   section_e     section_t;
typedef struct entry_s       entry_t;
typedef struct skey_s        skey_t;

typedef int (*key_handler_t)(service_t *s, const char *key, iot_json_t *o);

/**
 * @brief Generator runtime context type.
 *
 * A type for collecting and passing information around for
 * discovering applications and generating systemd service files
 * for them.
 */
struct generator_s {
    const char     **env;                /**< environment */
    const char      *dir_early;          /**< systemd 'early' service dir */
    const char      *dir_normal;         /**< systemd 'normal' service dir */
    const char      *dir_late;           /**< systemd 'late' service dir */
    const char      *dir_apps;           /**< application top directory */
    const char      *dir_service;        /**< service (output) directory */
    const char      *log_path;           /**< where to log to */
    bool             dry_run;            /**< just a dry-run, don't generate */
    int              apps_premounted;    /**< whether dir_apps was mounted */
    iot_list_hook_t  services;           /**< generated service( file)s */
    int              status;             /**< service generation status */
};


/**
 * @brief A single service file to be generated.
 *
 * A type for collecting the content of a generated service file
 * before it gets written to disk.
 */
struct service_s {
    iot_list_hook_t  hook;               /**< to list of generated services */
    char            *provider;           /**< application provider */
    char            *app;                /**< application name */
    char            *appdir;             /**< application directory */
    iot_json_t      *m;                  /**< application manifest */
    iot_list_hook_t  unit;               /**< systemd 'Unit' section */
    iot_list_hook_t  service;            /**< systemd 'Service' section */
    iot_list_hook_t  install;            /**< systemd 'Install' section */
};


/**
 * @brief Logging functions and macros.
 */
int log_open(generator_t *g, const char *path);
void log_close(void);
int log_msg(int level, const char *fmt, ...);

#define log_info(fmt, args...)  log_msg(LOG_INFO   , fmt"\n" , ## args)
#define log_error(fmt, args...) log_msg(LOG_ERR    , fmt"\n" , ## args)
#define log_warn(fmt, args...)  log_msg(LOG_WARNING, fmt"\n" , ## args)
#define log_debug(fmt, args...) log_msg(LOG_DEBUG  , fmt"\n" , ## args)


/**
 * @brief Configuration functions.
 *
 * These functions parse the command line and search the environment for
 * the value of the given tag prefixed with 'IOT_GENERATOR_'.
 */
int config_parse_cmdline(generator_t *g, int argc, char *argv[], char *env[]);
const char *config_getstr(generator_t *g, const char *tag);


/**
 * @brief Mount helpers.
 *
 * These functions call out for an external binary/script if there is
 * a need to temporarily mount/umount the application directory. This
 * way we can avoid hardcoding knowledge/assumptions about the layout
 * of partitions into the generator binary.
 */
int mount_apps(generator_t *g);
int umount_apps(generator_t *g);


/**
 * @brief Application discovery.
 *
 * Scan the application root directory, looking for and collecting
 * application manifest files.
 */
int application_discover(generator_t *g);


/**
 * @brief Manifest handling functions.
 */
iot_json_t *manifest_read(const char *path);


/**
 * @brief Service file generation.
 *
 * Generate service files based on the collected manifests and
 * write them to the filesystem.
 */

/**
 * @brief Service file section types.
 */
enum section_e {
    SECTION_UNIT = 0,
    SECTION_SERVICE,
    SECTION_INSTALL,
};

struct entry_s {
    iot_list_hook_t  hook;               /**< to list of section entries */
    char            *key;                /**< entry key   */
    char            *value;              /**< entry value */
};

struct skey_s {
    const char  *key;
    int        (*handler)(service_t *s, const char *key, iot_json_t *o);
};

int service_generate(generator_t *g);
int service_write(generator_t *g);

int service_append(service_t *s, section_t type, const char *k, const char *v);
int service_prepend(service_t *s, section_t type, const char *k, const char *v);
int section_append(iot_list_hook_t *s, const char *k, const char *v);
int section_prepend(iot_list_hook_t *s, const char *k, const char *v);

/**
 * @brief Key handlers.
 */

int key_register(skey_t *key);
key_handler_t key_lookup(const char *key);

#define key_handler(_key) key_lookup(_key)

#define REGISTER_KEY(_key, _handler)                       \
    static IOT_INIT void register_##_key##_handler(void) { \
        static skey_t k = {                                \
            .key     = #_key,                              \
            .handler = _handler,                           \
        };                                                 \
                                                           \
        if (key_register(&k) < 0)                          \
            abort();                                       \
    }

#endif /* __CONTAINER_GENERATOR_H__ */
