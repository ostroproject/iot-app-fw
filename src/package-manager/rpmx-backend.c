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

#define DB_CONFIG_FILE "DB_CONFIG"

static bool dbdir_copy(const char *, const char *, const char *);

static bool is_database(const char *file)
{
    static uint32_t magics[] = { 0x00061561, 0x00053162, 0x00042253, 0 };

    int fd = -1;
    uint32_t magic = 0;
    bool db = false;
    ssize_t l;
    int i;
    uint32_t val;

    if ((fd = open(file, O_RDONLY)) >= 0 && lseek(fd, 12, SEEK_SET) == 12) {
        while ((l = read(fd, (void *)&magic, sizeof(magic))) < 0 &&
               errno == EINTR)
            ;

        if (l == (ssize_t)sizeof(magic)) {
            for (i = 0;  (val = magics[i]);  i++) {
                if (magic == val) {
                    db = true;
                    break;
                }
            }
        }

        close(fd);
    }

    return db;
}

static bool file_copy(const char *src,
                      const char *dst,
                      const char *label,
                      bool silent)
{
    int sfd = -1;
    int dfd = -1;
    ssize_t len;
    char buf[65536];

    if (!src || !dst)
        goto failed;

    iot_debug("copying file '%s' => '%s", src, dst);

    if ((sfd = open(src, O_RDONLY)) < 0) {
        if (!silent)
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

        if (!file_write(dfd, dst, buf, len))
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

static bool db_copy(const char *src, const char *dst, const char *label)
{
    FILE *stream;
    char buf[IOTPM_PATH_MAX];
    char *home, *dbfile;
    char cmd[IOTPM_PATH_MAX * 2 + 128];
    char log[1024];

    if (!src || src[0] != '/' || !dst || dst[0] != '/')
        return false;

    strncpy(buf, dst, sizeof(buf));
    buf[sizeof(buf) - 1] = 0;

    home = buf;
    if (!(dbfile = strrchr(buf, '/')) || dbfile == buf)
        return false;
    *dbfile++ = 0;

    iot_debug("copying database '%s' => '%s", src, dst);

    snprintf(cmd, sizeof(cmd), "db_dump %s | db_load -h %s %s",
             src, home, dbfile);

    if (!(stream = popen(cmd, "r"))) {
        iot_log_error("failed to execute '%s': %s", cmd, strerror(errno));
        return false;
    }

    while (fgets(log, sizeof(log), stream))
        iot_log_info("%s", log);

    pclose(stream);

    return true;
}

static int dbdir_copy_callback(const char *src,
                               const char *entry,
                               iot_dirent_type_t type,
                               void *user_data)
{
    dbcopy_t *dbcopy = (dbcopy_t *)user_data;
    char src_path[IOTPM_PATH_MAX];
    char dst_path[IOTPM_PATH_MAX];
    bool copied;

    snprintf(src_path, sizeof(src_path), "%s/%s", src, entry);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dbcopy->dst, entry);

    if ((type & IOT_DIRENT_DIR)) {
        iot_debug("copying directory '%s' => '%s", src_path, dst_path);

        iot_mkdir(dst_path, 0755, dbcopy->label);

        if (!dbdir_copy(src_path, dst_path, dbcopy->label))
            return -1;
    } else

    if ((type & IOT_DIRENT_REG)) {
        if (strcmp(entry, DB_CONFIG_FILE) &&
            strncmp(entry, "__db.", 5)    &&
            strncmp(entry, "log.", 4)     &&
            entry[0] != '.'                )
        {
            if (is_database(src_path))
                copied = db_copy(src_path, dst_path, dbcopy->label);
            else
                copied = file_copy(src_path, dst_path, dbcopy->label, false);

            if (!copied)
                return -1;
        }
    }

    return 1;
}


static bool dbdir_copy(const char *src, const char *dst, const char *label)
{
#define PATTERN   "^[^.].*"
#define FILTER    (IOT_DIRENT_DIR | IOT_DIRENT_REG | IOT_DIRENT_IGNORE_LNK)

    dbcopy_t *dbcopy;
    bool success = true;

    if (!(dbcopy = iot_allocz(sizeof(dbcopy_t))))
        success = false;
    else {
        dbcopy->dst = dst;
        dbcopy->label = label;

        if (iot_scan_dir(src, PATTERN,FILTER, dbdir_copy_callback, dbcopy) < 0)
            success = false;

        iot_free(dbcopy);
    }

    return success;

#undef FILTER
#undef PATTERN
}

static bool database_copy(const char *src, const char *dst, const char *label)
{
    char src_path[IOTPM_PATH_MAX];
    char dst_path[IOTPM_PATH_MAX];

    bool success;

    iot_log_info("copy RPM database '%s' => '%s'", src, dst);

    snprintf(src_path, sizeof(src_path), "%s/%s", src, DB_CONFIG_FILE);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, DB_CONFIG_FILE);

    file_copy(src_path, dst_path, label, true);

    success = dbdir_copy(src, dst, label);

    if (success)
        iot_log_info("RPM database successfully copied");
    else
        iot_log_error("RPM database copy failed");

    return success;
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

    iot_log_info("remove RPM database '%s'", dir);

    if (iot_scan_dir(dir, PATTERN,FILTER, database_remove_callback,NULL) < 0) {
        iot_log_error("RPM database remove of '%s' failed: %s",
                      dir, strerror(errno));
        return false;
    }

    iot_log_info("RPM database '%s' succesfully removed", dir);

    return true;

#undef FILTER
#undef PATTERN
}
