void iotpm_backend_pkginfo_destroy(iotpm_pkginfo_t *info)
{
    iotpm_pkginfo_filentry_t *f;

    if (info) {
        if (info->files) {
	    for (f = info->files;  f->path;  f++) {
	        iot_free((void *)f->path);
	        iot_free((void *)f->user);
	        iot_free((void *)f->group);
	        iot_free((void *)f->link);
            }

	    iot_free((void *)info->files);
	}

        iot_free((void *)info->name);
        iot_free((void *)info->ver);
        iot_free(info->data);

	if (info != &failed_info)
	    iot_free((void *)info);
    }
}

void iotpm_backend_pkglist_destroy(iotpm_pkglist_t *list)
{
    iotpm_pkglist_entry_t *e;

    if (list) {
        if (list->entries) {
            for (e = list->entries;  e->name;  e++) {
                iot_free((void *)e->name);
                iot_free((void *)e->version);
            }

            iot_free((void *)list->entries);
        }

        if (list != &failed_list)
            iot_free((void *)list);
    }
}


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
#define JOIN_FLAGS (DB_CREATE    | DB_JOINENV  | DB_USE_ENVIRON |    \
                    DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL  |    \
                    DB_INIT_TXN  | DB_RECOVER  )

static bool dbdir_copy_prepare(DB_ENV *, const char *, const char *,
                               const char *, size_t *, dbfile_t **);

static DB_ENV *dbenv_join(const char *home)
{
    DB_ENV *env = NULL;
    u_int32_t flags = JOIN_FLAGS;
    int ret;

    iot_debug("joining environment '%s'", home);

    if ((ret = db_env_create(&env, 0)) != 0) {
        iot_log_error("failed to create DB environment: %s", db_strerror(ret));
        return NULL;
    }

    if ((ret = env->open(env, home, flags, 0)) != 0) {
        iot_log_error("DB open failed: %s", db_strerror(ret));
        env->close(env, 0);
        return NULL;
    }

    iot_debug("successfully joined environment %p", env);

    return env;
}

static int dbenv_close(DB_ENV *env)
{
    int ret;

    iot_debug("closing environment %p", env);

    if ((ret = env->close(env, 0)) != 0) {
        iot_log_error("failed to close DB environment: %s", db_strerror(ret));
        return -1;
    }

    return 0;
}


static int dbenv_remove(const char *home)
{
    DB_ENV *env = NULL;
    int ret;

    iot_debug("removing env '%s'", home);

    if ((ret = db_env_create(&env, 0)) != 0) {
        iot_log_error("failed to obtain DB environment: %s", db_strerror(ret));
        return -1;
    }

    return env->remove(env, home, DB_USE_ENVIRON);
}

static int dbfile_log_reset(DB_ENV *env, const char *file)
{
    int ret;

    iot_debug("resetting LSN of DB file '%s'", file);

    if ((ret = env->lsn_reset(env, file, 0)) != 0) {
        iot_log_error("failed to reset LSN of DB file '%s': %s",
                      file, db_strerror(ret));
        return -1;
    }

    return 0;
}


static int dbfile_id_reset(DB_ENV *env, const char *file)
{
    int ret;

    iot_debug("resetting file ID of DB file '%s'", file);

    if ((ret = env->fileid_reset(env, file, 0)) != 0) {
        iot_log_error("failed to reset file ID of DB file '%s': %s",
                      file, db_strerror(ret));
        return -1;
    }

    return 0;
}


static int dbfile_reset(DB_ENV *env, const char *file)
{
    iot_debug("resetting DB file '%s'", file);

    if (dbfile_id_reset(env, file)  < 0 ||
        dbfile_log_reset(env, file) < 0  )
    {
        return -1;
    }

    return 0;
}


static int dbfile_sync(DB_ENV *env, const char *file)
{
    DB *db;
    int ret;

    iot_debug("syncing DB file '%s'", file);

    if ((ret = db_create(&db, env, 0)) != 0) {
        iot_log_error("failed to obtain DB (file '%s', environment %p): %s",
                      file, env, db_strerror(ret));
        return -1;
    }

    if ((ret = db->open(db, NULL, file, NULL, DB_UNKNOWN, 0, 0)) != 0) {
        iot_log_error("failed to open DB file '%s': %s",
                      file, db_strerror(ret));
        return -1;
    }

    if ((ret = db->sync(db, 0)) != 0) {
        iot_log_error("failed to sync DB file '%s': %s",
                      file, db_strerror(ret));
        return -1;
    }

    if ((ret = db->close(db, 0)) != 0) {
        iot_log_error("can't close DB file '%s': %s",
                      file, db_strerror(ret));
        return -1;
    }

    return 0;
}




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

    if (!silent)
        iot_debug("copying file '%s' => '%s", src, dst);

    if ((sfd = open(src, O_RDONLY)) < 0) {
        if (!silent)
            iot_log_error("failed to open '%s': %s", src, strerror(errno));
        goto failed;
    }

    if (silent)
        iot_debug("copying file '%s' => '%s", src, dst);

    if ((dfd = open(dst, O_RDWR|O_CREAT|O_EXCL, 0644)) < 0) {
        iot_log_error("failed to open '%s': %s", dst, strerror(errno));
        goto failed;
    }

    if (iot_set_label(dst, label, 0) < 0) {
        iot_log_error("failed to set label of file '%s' to '%s'", dst, label);
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

static bool db_copy_prepare(const char *name,
                            const char *src,
                            const char *dst,
                            dbcopy_t *dbcopy)
{
    size_t nfile = *(dbcopy->nfile);
    dbfile_t *files = *(dbcopy->files);
    dbfile_t *f;

    /*
     * append an entry to the dbfile list
     */
    if (!(files = iot_realloc(files, sizeof(dbfile_t) * (nfile + 2))))
        goto no_mem;

    memset((f = files + nfile), 0, sizeof(dbfile_t) * 2);

    if (!(f->name = iot_strdup(name)) ||
        !(f->src = iot_strdup(src))   ||
        !(f->dst = iot_strdup(dst))    )
        goto no_mem;

    *(dbcopy->nfile) = nfile + 1;
    *(dbcopy->files) = files;

    /*
     * sync the DB file
     */
    if (dbfile_sync(dbcopy->env, name) < 0)
        return false;

    return true;

 no_mem:
    iot_log_error("can't allocate memory while copying RPM DB");
    return false;
}

static int dbdir_copy_prepare_callback(const char *src,
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

        if (iot_mkdir(dst_path, 0755, dbcopy->label)  < 0 ||
            chown(dst_path, dbcopy->uid, dbcopy->gid) < 0  )
        {
            iot_log_error("failed to create directory '%s': %s",
                          dst_path, strerror(errno));
            return -1;
        }

        copied = dbdir_copy_prepare(dbcopy->env,
                                    src_path, dst_path,
                                    dbcopy->label,
                                    dbcopy->nfile, dbcopy->files);
        if (!copied)
            return -1;
    } else

    if ((type & IOT_DIRENT_REG)) {
        if (strcmp(entry, DB_CONFIG_FILE) &&
            strncmp(entry, "__db.", 5)    &&
            strncmp(entry, "log.", 4)     &&
            entry[0] != '.'                )
        {
            if (is_database(src_path)) {
                copied = db_copy_prepare(entry, src_path, dst_path, dbcopy);
            }
            else {
                copied = file_copy(src_path, dst_path, dbcopy->label, false);
            }

            if (!copied)
                return -1;
        }
    }

    return 1;
}


static bool dbdir_copy_prepare(DB_ENV *env,
                               const char *src,
                               const char *dst,
                               const char *label,
                               size_t *nfile,
                               dbfile_t **files)
{
#define PATTERN   "^[^.].*"
#define FILTER    (IOT_DIRENT_DIR | IOT_DIRENT_REG | IOT_DIRENT_IGNORE_LNK)

    dbcopy_t *dbcopy;
    bool success = true;
    int sts;

    if (!(dbcopy = iot_allocz(sizeof(dbcopy_t)))                   ||
        (!files && !(files = iot_allocz(sizeof(const char *)))) )
    {
        iot_log_error("can't allocate memory while copying DB");
        success = false;
    }
    else {
        dbcopy->env = env;
        dbcopy->dst = dst;
        dbcopy->label = label;
        dbcopy->nfile = nfile;
        dbcopy->files = files;

        sts = iot_scan_dir(src, PATTERN, FILTER,
                           dbdir_copy_prepare_callback, dbcopy);
        if (sts < 0)
            success = false;

        iot_free(dbcopy);
    }

    return success;

#undef FILTER
#undef PATTERN
}

static bool database_copy(const char *src, const char *dst, const char *label)
{
    DB_ENV *env = NULL;
    size_t nfile = 0;
    dbfile_t *files = iot_allocz(sizeof(dbfile_t));
    char src_path[IOTPM_PATH_MAX];
    char dst_path[IOTPM_PATH_MAX];
    dbfile_t *f;

    bool success;

    iot_log_info("copy RPM database '%s' => '%s'", src, dst);

    if (!files) {
        iot_log_error("can't allocate memory when copying RPM DB");
        return false;
    }

    /*
     * copy the config file, if any
     */
    snprintf(src_path, sizeof(src_path), "%s/%s", src, DB_CONFIG_FILE);
    snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, DB_CONFIG_FILE);

    file_copy(src_path, dst_path, label, true);


    /*
     * copy regular files; synch source DBs
     */
    iot_switch_userid(IOT_USERID_SUID);

    do {
        success = false;

        if (!(env = dbenv_join(src)))
            break;

        if (!dbdir_copy_prepare(env, src, dst, label, &nfile, &files))
            break;

        if (dbenv_close(env) < 0)
            break;

        if (!(env = dbenv_join(src)))
            break;


        for (f = files;  f->name;  f++) {
            if (dbfile_log_reset(env, f->name) < 0)
                break;
        }

        if (f->name)
            break;

        if (dbenv_remove(src) < 0)
            break;

        success = true;
    } while(0);


    iot_switch_userid(IOT_USERID_REAL);

    if (!success)
        goto out;


    /*
     * copy DB files
     */
    for (f = files;  f->name;  f++) {
        if (!file_copy(f->src, f->dst, label, false)) {
            success = false;
            goto out;
        }
    }

    /*
     * reset DB files
     */
    if (!(env = dbenv_join(dst)))
        goto out;

    for (f = files;  f->name;  f++) {
        if (dbfile_reset(env, f->name) < 0) {
            success = false;
            goto out;
        }
    }

    if (dbenv_close(env) < 0) {
        success = false;
        goto out;
    }

    success = true;

 out:
    for (f = files;  f->name;  f++) {
        iot_free((void *)f->name);
        iot_free((void *)f->src);
        iot_free((void *)f->dst);
    }
    iot_free((void *)files);

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
