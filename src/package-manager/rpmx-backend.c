static bool file_write(int fd,
		       const char *file,
		       const void *data,
		       ssize_t length)
{
    ssize_t l, offs = 0;

    if (fd >= 0 && data && length > 0) {
        for (;;) {
	    l = write(fd, (void *)((char *)data + offs), length);

	    if (l < 0) {
	        if (errno == EINTR)
		    continue;
		else
		    break;
	    }

	    if (l == 0) {
	        errno = EIO;
	        break;
	    }

	    offs += l;

	    if (offs >= length)
	        return true;
	}

	iot_log_error("failed to write file '%s': %s", file, strerror(errno));
    }

    return false;
}

static bool file_read(int fd,
		      const char *file,
		      void *data,
		      ssize_t length)
{
    ssize_t l, offs = 0;

    if (fd >= 0 && data && length > 0) {
        for (;;) {
	    l = read(fd, (void *)((char *)data + offs), length);

	    if (l < 0) {
	        if (errno == EINTR)
		    continue;
		else
		    break;
	    }

	    if (l == 0) {
	        errno = EIO;
	        break;
	    }

	    offs += l;

	    if (offs >= length)
	        return true;
	}

	iot_log_error("failed to read file '%s': %s", file, strerror(errno));
    }

    return false;
}

static bool file_copy(const char *src, const char *dst, const char *label)
{
    int sfd = -1;
    int dfd = -1;
    ssize_t len;
    char buf[65536];

    if (!src || !dst)
        goto failed;

    if ((sfd = open(src, O_RDONLY)) < 0) {
        iot_log_error("failed to open '%s': %s", src, strerror(errno));
        goto failed;
    }

    if ((dfd = open(dst, O_RDWR|O_CREAT|O_EXCL, 0644)) < 0) {
        iot_log_error("failed to open '%s': %s", dst, strerror(errno));
        goto failed;
    }

    for (;;) {
        if ((len = read(sfd, buf, sizeof(buf))) < 0) {
            if (errno == EINTR)
                continue;

            iot_log_error("failed to read file '%s': %s", src,strerror(errno));

            goto failed;
        }

        if (len == 0)
            break;

        if (!file_write(dfd, dst, buf, sizeof(buf)))
            goto failed;
    }

    close(sfd);
    close(dfd);

    sfd = dfd = -1;

    // set label here ...

    return true;

 failed:
    if (sfd >= 0)
        close(sfd);
    if (dfd >= 0) {
        close(dfd);
        unlink(dst);
    }
    return false;
}

static int database_copy_callback(const char *src,
                                  const char *entry,
                                  iot_dirent_type_t type,
                                  void *user_data)
{
    dbcopy_t *dbcopy = (dbcopy_t *)user_data;
    char src_path[IOTPM_PATH_MAX];
    char dst_path[IOTPM_PATH_MAX];

    snprintf(src_path, sizeof(src_path), "%s/%s", src, entry);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dbcopy->dst, entry);

    if ((type & IOT_DIRENT_DIR)) {
        iot_debug("copying directory '%s' => '%s", src_path, dst_path);

        iot_mkdir(dbcopy->dst, 0755, dbcopy->label);

        if (!database_copy(src_path, dst_path, dbcopy->label))
            return -1;
    } else

    if ((type & IOT_DIRENT_REG)) {
        iot_debug("copying file '%s' => '%s", src_path, dst_path);
        if (!file_copy(src_path, dst_path, dbcopy->label))
            return -1;
    }

    return 1;
}

static bool database_copy(const char *src, const char *dst, const char *label)
{
#define PATTERN   "^[^.].*"
#define FILTER    (IOT_DIRENT_DIR | IOT_DIRENT_REG | IOT_DIRENT_IGNORE_LNK)

    dbcopy_t *dbcopy;

    if (!(dbcopy = iot_allocz(sizeof(dbcopy_t)))) {
        iot_log_error("RPM database copy '%s' => '%s' failed: %s",
                      src, dst, strerror(errno));
        return false;
    }

    dbcopy->dst = dst;
    dbcopy->label = label;

    if (iot_scan_dir(src, PATTERN,FILTER, database_copy_callback,dbcopy) < 0) {
        iot_log_error("RPM database copy '%s' => '%s' failed: %s",
                      src, dst, strerror(errno));
        iot_free(dbcopy);
        return false;
    }

    iot_log_info("RPM database copied: '%s' => '%s'", src, dst);

    iot_free(dbcopy);
    return true;

#undef FILTER
#undef PATTERN
}

static int database_remove_callback(const char *dir,
                                    const char *entry,
                                    iot_dirent_type_t type,
                                    void *user_data)
{
    char path[IOTPM_PATH_MAX];

    IOT_UNUSED(user_data);

    snprintf(path, sizeof(path), "%s/%s", dir, entry);

    if ((type & IOT_DIRENT_DIR)) {
        if (!database_remove(path))
            return -1;
    }
    else {
        if (!unlink(path))
            return -1;
    }

    return 1;
}

static bool database_remove(const char *dir)
{
#define PATTERN   ".*"
#define FILTER    (IOT_DIRENT_DIR | IOT_DIRENT_REG | \
                   IOT_DIRENT_LNK | IOT_DIRENT_ACTUAL_LNK)

    if (iot_scan_dir(dir, PATTERN,FILTER, database_remove_callback,NULL) < 0) {
        iot_log_error("RPM database remove of '%s' failed: %s",
                      dir, strerror(errno));
        return false;
    }

    iot_log_info("RPM database '%s' removed", dir);

    return true;

#undef FILTER
#undef PATTERN
}
