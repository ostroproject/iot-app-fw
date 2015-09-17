#ifndef __IOTPM_RPMX_BACKEND_H__
#define __IOTPM_RPMX_BACKEND_H__

typedef struct {
    const char *dst;
    const char *label;
} dbcopy_t;

static bool file_write(int, const char *, const void *, ssize_t);
static bool file_read(int, const char *, void *, ssize_t);

static bool database_copy(const char *, const char *, const char *);
static bool database_remove(const char *);

#endif /* __IOTPM_RPMX_BACKEND_H__ */

