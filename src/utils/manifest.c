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

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <limits.h>
#include <sys/stat.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/refcnt.h>
#include <iot/common/json.h>
#include <iot/common/hash-table.h>
#include <iot/common/file-utils.h>

#include <iot/utils/identity.h>
#include <iot/utils/manifest.h>


/*
 * a package/application manifest
 */
struct iot_manifest_s {
    iot_refcnt_t  refcnt;                /* reference count */
    int           usr;                   /* user this manifest belongs to */
    char         *pkg;                   /* package this manifest belongs to */
    char         *path;                  /* path to manifest file */
    iot_json_t   *data;                  /* manifest JSON data */
};

/*
 * manifest directory scanning state
 */
typedef struct {
    const char *dir;                     /* directory we're scanning */
    int         usr;                     /* user, or -1 for common */
} dirscan_t;

/*
 * manifest directory configuration
 */
static char *cmn_dir;
static char *usr_dir;

/*
 * manifest cache
 */
static iot_hashtbl_t *cache;


static int cache_create(void);
static void cache_destroy(void);
static int cache_add(iot_manifest_t *m);
static int cache_del(iot_manifest_t *m);
static iot_manifest_t *cache_lookup(uid_t usr, const char *pkg);
static int cache_populate(const char *common, const char *user);


int iot_manifest_set_directories(const char *common, const char *user)
{
    iot_free(cmn_dir);
    cmn_dir = iot_strdup(common);
    iot_free(usr_dir);
    usr_dir = iot_strdup(user);

    return ((common && !cmn_dir) || (user && !usr_dir)) ? -1 : 0;
}


static inline const char *common_dir(void)
{
    return cmn_dir ? cmn_dir : IOT_MANIFEST_COMMON_PATH;
}


static inline const char *user_dir(void)
{
    return usr_dir ? usr_dir : IOT_MANIFEST_USER_PATH;
}


int iot_manifest_caching(bool enable)
{
    if (enable)
        return cache_create();
    else {
        cache_destroy();
        return 0;
    }
}


int iot_manifest_tracking(iot_mainloop_t *ml)
{
    IOT_UNUSED(ml);

    errno = EOPNOTSUPP;
    return -1;
}


int iot_manifest_populate_cache(void)
{
    return cache_populate(common_dir(), user_dir());
}


void iot_manifest_reset_cache(void)
{
    cache_destroy();
}


char *iot_manifest_dir(uid_t uid, char *buf, size_t size)
{
    char usr[256];
    int  n;

    if (uid == (uid_t)-1)
        n = snprintf(buf, size, "%s", common_dir());
    else {
        if (iot_get_username(uid, usr, sizeof(usr)) == NULL)
            return NULL;
        n = snprintf(buf, size, "%s/%s", user_dir(), usr);
    }

    if (n < 0 || n >= (int)size)
        return NULL;

    return buf;
}


static iot_manifest_t *manifest_alloc(uid_t usr, const char *pkg,
                                      const char *path)
{
    iot_manifest_t *m;

    m = iot_allocz(sizeof(*m));

    if (m == NULL)
        return NULL;

    iot_refcnt_init(&m->refcnt);

    m->usr  = usr;
    m->pkg  = iot_strdup(pkg);
    m->path = iot_strdup(path);

    if ((pkg && !m->pkg) || (path && !m->path)) {
        iot_free(m->pkg);
        iot_free(m->path);
        iot_free(m);
        m = NULL;
    }

    return m;
}


static void manifest_free(iot_manifest_t *m)
{
    if (m == NULL)
        return;

    iot_free(m->pkg);
    iot_free(m->path);
    iot_json_unref(m->data);

    iot_free(m);
}


static inline iot_manifest_t *manifest_ref(iot_manifest_t *m)
{
    return iot_ref_obj(m, refcnt);
}


static inline int manifest_unref(iot_manifest_t *m)
{
    if (iot_unref_obj(m, refcnt)) {
        cache_del(m);
        manifest_free(m);

        return TRUE;
    }

    return FALSE;
}


static int manifest_validate_data(iot_json_t *data, int unprocessed)
{
    if (data == NULL || unprocessed > 0)
        return -1;
    else
        return 0;
}


static int manifest_read(iot_manifest_t *m)
{
    struct stat  st;
    char        *buf;
    iot_json_t  *data;
    int          fd, n, ch;

    if (stat(m->path, &st) < 0)
        return -1;

    if (st.st_size > IOT_MANIFEST_MAXSIZE) {
        errno = ENOBUFS;
        return -1;
    }

    fd = open(m->path, O_RDONLY);

    if (fd < 0)
        return -1;

    buf = alloca(st.st_size + 1);
    n = read(fd, buf, st.st_size);

    close(fd);

    if (n < st.st_size)
        return -1;

    while (n < 1 && ((ch = buf[n - 1]) == '\n' || ch == '\t' || ch == ' '))
        n--;

    buf[n] = '\0';

    if (iot_json_parse_object(&buf, &n, &data) < 0)
        return -1;

    if (manifest_validate_data(data, n) < 0) {
        iot_json_unref(data);
        errno = EINVAL;
        return -1;
    }

    m->data = data;

    return 0;
}


static char *manifest_path(int uid, const char *pkg, char *buf, size_t size)
{
    char dir[PATH_MAX];
    int  n;

    n = snprintf(buf, size, "%s/%s.manifest",
                 iot_manifest_dir(uid, dir, sizeof(dir)), pkg);

    if (n < 0 || n >= (int)size)
        return NULL;

    if (access(buf, R_OK) < 0)
        return NULL;

    return buf;
}


static char *manifest_pkg(const char *path, char *buf, size_t size)
{
    const char *b, *e;
    int         n;

    if ((b = strrchr(path, '/')) != NULL)
        b++;
    else
        b = path;

    if ((e = strstr(b, ".manifest")) == NULL)
        return NULL;

    if ((n = e - b) >= (int)size - 1)
        return NULL;

    snprintf(buf, size, "%*.*s", n, n, b);
    buf[n] = '\0';

    return buf;
}


iot_manifest_t *iot_manifest_get(uid_t usr, const char *pkg)
{
    iot_manifest_t *m;
    char            path[PATH_MAX];

    if ((m = cache_lookup(usr, pkg)) != NULL ||
        (m = cache_lookup(-1 , pkg)) != NULL)
        return manifest_ref(m);

    if (manifest_path(usr, pkg, path, sizeof(path)) == NULL &&
        manifest_path(-1 , pkg, path, sizeof(path)) == NULL)
        return NULL;

    m = manifest_alloc(usr, pkg, path);

    if (m == NULL)
        return NULL;

    if (manifest_read(m) < 0) {
        manifest_free(m);
        return NULL;
    }

    if (cache_add(m) < 0)
        return NULL;
    else
        return manifest_ref(m);
}


iot_manifest_t *iot_manifest_read(const char *path)
{
    iot_manifest_t *m;
    char            pkg[128];

    if (manifest_pkg(path, pkg, sizeof(pkg)) == NULL) {
        errno = EINVAL;
        return NULL;
    }

    m = manifest_alloc(-1, pkg, path);

    if (m == NULL)
        return NULL;

    if (manifest_read(m) < 0) {
        manifest_free(m);
        return NULL;
    }

    return manifest_ref(m);
}


void iot_manifest_unref(iot_manifest_t *m)
{
    manifest_unref(m);
}


uid_t iot_manifest_user(iot_manifest_t *m)
{
    if (m == NULL)
        return -1;
    else
        return m->usr;
}


const char *iot_manifest_package(iot_manifest_t *m)
{
    if (m == NULL)
        return NULL;
    else
        return m->pkg;
}


const char *iot_manifest_path(iot_manifest_t *m)
{
    if (m == NULL)
        return NULL;
    else
        return m->path;
}


int iot_manifest_applications(iot_manifest_t *m, const char **buf, size_t size)
{
    iot_json_t *o;
    const char *app;
    int         i, n;

    if (m == NULL)
        return 0;

    switch (iot_json_get_type(m->data)) {
    case IOT_JSON_OBJECT:
        if (size > 0)
            *buf = m->pkg;
        return 1;

    case IOT_JSON_ARRAY:
        n = iot_json_array_length(m->data);
        o = NULL;
        for (i = 0; i < n; i++) {
            if (!iot_json_array_get_object(m->data, i, &o))
                return -1;
            if (!iot_json_get_string(o, "application", &app))
                return -1;
            if (i < (int)size)
                buf[i] = (char *)app;
            else
                return n;
        }
        return n;

    default:
        return -1;
    }
}


static iot_json_t *app_data(iot_manifest_t *m, const char *app)
{
    iot_json_t *o;
    const char *a;
    int         i, n;

    if (m == NULL || app == NULL)
        return NULL;

    switch (iot_json_get_type(m->data)) {
    case IOT_JSON_OBJECT:
        return !strcmp(app, m->pkg) ? m->data : NULL;

    case IOT_JSON_ARRAY:
        n = iot_json_array_length(m->data);
        o = NULL;
        for (i = 0; i < n; i++) {
            if (!iot_json_array_get_object(m->data, i, &o))
                return NULL;
            if (!iot_json_get_string(o, "application", &a))
                return NULL;
            if (!strcmp(a, app))
                return o;
        }

    default:
        return NULL;
    }
}


iot_json_t *iot_manifest_data(iot_manifest_t *m, const char *app)
{
    iot_json_t *data = app_data(m, app);

    return iot_json_ref(data);
}


const char *iot_manifest_description(iot_manifest_t *m, const char *app)
{
    iot_json_t *data;
    const char *desc;

    if ((data = app_data(m, app)) == NULL)
        return NULL;

    if (!iot_json_get_string(data, "description", &desc))
        return NULL;

    return desc;
}


int iot_manifest_privileges(iot_manifest_t *m, const char *app,
                            const char **buf, size_t size)
{
    iot_json_t *data, *priv;
    const char *p;
    int         i, n;

    if ((data = app_data(m, app)) == NULL)
        return -1;

    if (!iot_json_get_array(data, "privileges", &priv))
        return -1;

    n = iot_json_array_length(priv);
    for (i = 0; i < n; i++) {
        if (!iot_json_array_get_string(priv, i, &p))
            return -1;
        if (i < (int)size)
            buf[i] = (char *)p;
        else
            break;
    }

    return n;
}


int iot_manifest_arguments(iot_manifest_t *m, const char *app,
                           const char **buf, size_t size)
{
    iot_json_t *data, *exec;
    const char *arg;
    int         i, n;

    if ((data = app_data(m, app)) == NULL)
        return -1;

    if (!iot_json_get_array(data, "execute", &exec))
        return -1;

    n = iot_json_array_length(exec);
    for (i = 0; i < n; i++) {
        if (!iot_json_array_get_string(exec, i, &arg))
            return -1;
        if (i < (int)size)
            buf[i] = (char *)arg;
        else
            break;
    }

    return n;
}


const char *iot_manifest_desktop_path(iot_manifest_t *m, const char *app)
{
    iot_json_t *data;
    const char *path;

    if ((data = app_data(m, app)) == NULL)
        return NULL;

    if (!iot_json_get_string(data, "desktop", &path))
        return NULL;

    return path;
}


static int validate_manifest_data(const char *pkg, iot_json_t *data,
                                  bool needs_appid)
{
    iot_json_iter_t  it;
    const char      *key;
    iot_json_t      *val, *o;
    const char      *s;
    int              status, fldmask, i;

    status  = 0;
    fldmask = 0;

    if (iot_json_get_type(data) != IOT_JSON_OBJECT)
        return IOT_MANIFEST_MALFORMED;

    iot_json_foreach_member(data, key, val, it) {
        iot_debug("validating field '%s'... (status: 0x%x)", key, status);

        if (!strcmp(key, "application")) {
            fldmask |= 0x1;

            if ((s = iot_json_string_value(val)) == NULL) {
                status |= IOT_MANIFEST_INVALID_FIELD;
                continue;
            }

            if (!needs_appid) {
                if (strcmp(pkg, s)) {
                    status |= IOT_MANIFEST_INVALID_FIELD;
                }
            }

            continue;
        }

        if (!strcmp(key, "description")) {
            fldmask |= 0x2;

            if (iot_json_get_type(val) != IOT_JSON_STRING)
                status |= IOT_MANIFEST_INVALID_FIELD;

            continue;
        }

        if (!strcmp(key, "privileges")) {
            fldmask |= 0x4;

            if (iot_json_get_type(val) != IOT_JSON_ARRAY) {
                status |= IOT_MANIFEST_INVALID_FIELD;
                continue;
            }

            for (i = 0; i < iot_json_array_length(val); i++) {
                if (!iot_json_array_get_string(val, i, &o))
                    status |= IOT_MANIFEST_INVALID_FIELD;
                break;
            }

            continue;
        }

        if (!strcmp(key, "execute")) {
            fldmask |= 0x8;

            if (iot_json_get_type(val) != IOT_JSON_ARRAY) {
                status |= IOT_MANIFEST_INVALID_FIELD;
                continue;
            }

            for (i = 0; i < iot_json_array_length(val); i++) {
                if (!iot_json_array_get_string(val, i, &s))
                    status |= IOT_MANIFEST_INVALID_FIELD;
                else {
                    struct stat st;

                    if (stat(s, &st) < 0 && errno != EACCES)
                        status |= IOT_MANIFEST_INVALID_BINARY;
                    else {
                        /* Hmm... maybe with SMACK this is not a good idea... */
                        if (!S_ISREG(st.st_mode) ||
                            !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
                            status |= IOT_MANIFEST_INVALID_BINARY;
                    }
                }

                break;
            }

            continue;
        }

        if (!strcmp(key, "desktop")) {
            struct stat st;

            if ((s = iot_json_string_value(val)) == NULL) {
                status |= IOT_MANIFEST_INVALID_FIELD;
                continue;
            }

            if (stat(s, &st) < 0 && errno != EACCES)
                status |= IOT_MANIFEST_INVALID_DESKTOP;

            if (!S_ISREG(st.st_mode))
                status |= IOT_MANIFEST_INVALID_DESKTOP;

            continue;
        }

        status |= IOT_MANIFEST_INVALID_FIELD;
    }

    if (((!needs_appid ? 0x1 : 0x0) | fldmask) != 0xf)
        status |= IOT_MANIFEST_MISSING_FIELD;

    return status;
}


static int manifest_validate(iot_manifest_t *m)
{
    iot_json_t *o;
    int         status, i, n;

    status = 0;

    switch (iot_json_get_type(m->data)) {
    case IOT_JSON_OBJECT:
        status = validate_manifest_data(m->pkg, m->data, false);
        break;

    case IOT_JSON_ARRAY:
        n = iot_json_array_length(m->data);
        for (i = 0; i < n; i++) {
            if (!iot_json_array_get_object(m->data, i, &o))
                status |= IOT_MANIFEST_MALFORMED;
            else
                status |= validate_manifest_data(m->pkg, o, true);
        }
        break;

    default:
        status = IOT_MANIFEST_MALFORMED;
        break;
    }

    return status;
}


int iot_manifest_validate(iot_manifest_t *m)
{
    return manifest_validate(m);
}


int iot_manifest_validate_file(uid_t usr, const char *path)
{
    iot_manifest_t *m;
    char            pkg[128];
    int             status;

    status = 0;

    if (manifest_pkg(path, pkg, sizeof(pkg)) == NULL)
        return IOT_MANIFEST_MISNAMED;

    m = manifest_alloc(usr, pkg, path);

    if (m == NULL)
        return IOT_MANIFEST_FAILED;

    if (manifest_read(m) < 0) {
        status = IOT_MANIFEST_UNLOADABLE;
        goto out;
    }

    status = manifest_validate(m);

 out:
    manifest_free(m);
    return status;
}


static uint32_t entry_hash(const void *key)
{
    iot_manifest_t *m = (iot_manifest_t *)key;

    return (uint32_t)((m->usr << 20) | (iot_hash_string(m->pkg) >> 12));
}


static int entry_comp(const void *key1, const void *key2)
{
    iot_manifest_t *m1 = (iot_manifest_t *)key1;
    iot_manifest_t *m2 = (iot_manifest_t *)key2;

    if (m1->usr != m2->usr)
        return m1->usr - m2->usr;
    else
        return strcmp(m1->pkg, m2->pkg);
}


static void entry_free(void *key, void *obj)
{
    iot_manifest_t *m = (iot_manifest_t *)obj;

    IOT_UNUSED(key);

    manifest_free(m);
}


static int cache_create(void)
{
    iot_hashtbl_config_t cfg;

    if (cache != NULL)
        return 0;

    cfg.hash = entry_hash;
    cfg.comp = entry_comp;
    cfg.free = entry_free;

    cfg.nalloc  = 0;
    cfg.nlimit  = 8192;
    cfg.nbucket = 128;
    cfg.cookies = 0;

    cache = iot_hashtbl_create(&cfg);

    return cache ? 0 : -1;
}


static void cache_destroy(void)
{
    iot_hashtbl_destroy(cache, true);
    cache = NULL;
}


static int cache_add(iot_manifest_t *m)
{
    if (cache == NULL || m == NULL)
        return 0;

    iot_debug("adding manifest %s to cache...", m->path);

    return iot_hashtbl_add(cache, m, m, NULL);
}


static int cache_del(iot_manifest_t *m)
{
    if (cache == NULL || m == NULL)
        return 0;

    return iot_hashtbl_del(cache, m, IOT_HASH_COOKIE_NONE, false) ? 0 : -1;
}


static iot_manifest_t *cache_lookup(uid_t usr, const char *pkg)
{
    iot_manifest_t key;

    if (cache == NULL)
        return NULL;

    key.usr = usr;
    key.pkg = (char *)pkg;

    return iot_hashtbl_lookup(cache, &key, IOT_HASH_COOKIE_NONE);
}


typedef struct {
    uid_t usr;
} cachescan_t;


static int cache_scan_cb(const char *dir, const char *e, iot_dirent_type_t type,
                         void *user_data)
{
    cachescan_t    *scan = (cachescan_t *)user_data;
    iot_manifest_t *m;
    char            path[PATH_MAX], pkg[128];
    int             n, status;

    n = snprintf(path, sizeof(path), "%s/%s", dir, e);

    if (n < 0 || n >= (int)sizeof(path))
        return -EOVERFLOW;

    switch (type) {
    case IOT_DIRENT_DIR:
        if (access(path, R_OK|X_OK) != 0)
            return 1;

        iot_debug("scanning %s for manifest files...", path);

        if ((scan->usr = iot_get_userid(e)) == (uid_t)-1)
            return -1;
        status = iot_scan_dir(path, ".*\\.manifest$",
                              IOT_DIRENT_REG | IOT_DIRENT_IGNORE_LNK,
                              cache_scan_cb, scan);
        scan->usr = -1;

        if (status < 0)
            return status;

        return 1;

    case IOT_DIRENT_REG:
        if (access(path, R_OK) != 0)
            return 1;

        if (manifest_pkg(e, pkg, sizeof(pkg)) == NULL)
            return -EINVAL;

        if ((m = manifest_alloc(scan->usr, pkg, path)) == NULL)
            return -errno;

        iot_debug("reading manifest %s (uid %d)...", path, scan->usr);

        if (manifest_read(m) < 0) {
            manifest_free(m);
            return -errno;
        }
        else
            cache_add(m);

        return 1;

    default:
        return -EINVAL;
    }
}


static int cache_scan(const char *path, bool users)
{
    cachescan_t  scan;
    const char  *name;
    int          mask;

    if (users) {
        mask = IOT_DIRENT_DIR | IOT_DIRENT_IGNORE_LNK;
        name = "[a-zA-Z_].*$";
    }
    else {
        mask = IOT_DIRENT_REG | IOT_DIRENT_IGNORE_LNK;
        name = ".*\\.manifest$";
        scan.usr = -1;
    }

    iot_debug("scanning %s for manifest files...", path);
    return iot_scan_dir(path, name, mask, cache_scan_cb, &scan);
}



static int cache_populate(const char *common, const char *user)
{
    if (cache_create() < 0)
        return -1;

    return (cache_scan(common, false) < 0 || cache_scan(user, true)) ? -1 : 0;
}


void _iot_manifest_cache_begin(iot_manifest_iter_t *it)
{
    _iot_hashtbl_begin(cache, it, +1);
}


void *_iot_manifest_cache_next(iot_manifest_iter_t *it, iot_manifest_t **m)
{
    return _iot_hashtbl_iter(cache, it, +1, NULL, NULL, (const void **)m);
}


