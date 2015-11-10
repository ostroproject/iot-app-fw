#ifndef __IOTPM_RPMX_BACKEND_H__
#define __IOTPM_RPMX_BACKEND_H__

#include <unistd.h>
#include <sys/types.h>

#include <db.h>

typedef struct {
    const char *name;
    const char *src;
    const char *dst;
} dbfile_t;

typedef struct {
    DB_ENV *env;
    const char *dst;
    uid_t uid;
    gid_t gid;
    const char *label;
    size_t *nfile;
    dbfile_t **files;
} dbcopy_t;

static bool file_write(int, const char *, const void *, ssize_t);
static bool file_read(int, const char *, void *, ssize_t);

static bool database_copy(const char *, const char *, uid_t, gid_t,
                          const char *, bool);
static bool database_remove(const char *, bool);

#endif /* __IOTPM_RPMX_BACKEND_H__ */

