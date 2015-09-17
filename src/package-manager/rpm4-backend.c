#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <popt.h>

#include <rpm/rpmcli.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmps.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmfileutil.h>

#include <iot/common.h>

#include "backend.h"
#include "rpmx-backend.h"

#define RPM_DIR                   IOTPM_PACKAGE_HOME "/rpm/"
#define DB_DIR                    RPM_DIR "db"
#define SEED_DIR                  RPM_DIR "seed"

#define HEADER_LENGTH_MAX         (32 * 1024 * 1024)

#define INSTALL                   false
#define UPGRADE                   true


static int pkginfo_fill(QVA_t, rpmts, Header);

static bool install_package(iotpm_t *, bool, const char *);

static void *seed_read(const char *, size_t *);

static int log_callback(rpmlogRec, rpmlogCallbackData);



static iotpm_pkginfo_t  failed_info = {
    .sts = -1
};
static struct poptOption   queryOptionsTable[] = {
    { /* longName   */  NULL,
      /* shortName  */  '\0',
      /* argInfo    */  POPT_ARG_INCLUDE_TABLE,
      /* arg        */  rpmQVSourcePoptTable,
      /* val        */  0,
      /* descrip    */  NULL,
      /* argDescrip */  NULL },
    { /* longName   */  NULL,
      /* shortName  */  '\0',
      /* argInfo    */  POPT_ARG_INCLUDE_TABLE,
      /* arg        */  rpmQueryPoptTable,
      /* val        */  0,
      /* descrip    */  NULL,
      /* argDescrip */  NULL },
    { /* longName   */  NULL,
      /* shortName  */  '\0',
      /* argInfo    */  POPT_ARG_INCLUDE_TABLE,
      /* arg        */  rpmcliAllPoptTable,
      /* val        */  0,
      /* descrip    */  NULL,
      /* argDescrip */  NULL
    },
    POPT_TABLEEND
};
static struct poptOption   installOptionsTable[] = {
    { /* longName   */  NULL,
      /* shortName  */  '\0',
      /* argInfo    */  POPT_ARG_INCLUDE_TABLE,
      /* arg        */  rpmInstallPoptTable,
      /* val        */  0,
      /* descrip    */  NULL,
      /* argDescrip */  NULL },
    { /* longName   */  NULL,
      /* shortName  */  '\0',
      /* argInfo    */  POPT_ARG_INCLUDE_TABLE,
      /* arg        */  rpmcliAllPoptTable,
      /* val        */  0,
      /* descrip    */  NULL,
      /* argDescrip */  NULL
    },
    POPT_TABLEEND
};
static struct poptOption   *verifyOptionsTable = queryOptionsTable;

static const char *system_dbpath = "/var/lib/rpm";

bool iotpm_backend_init(iotpm_t *iotpm)
{
    iotpm_backend_t *backend;
    const char *error, *dir;
    char dbpath[IOTPM_PATH_MAX];
    char seedpath[IOTPM_PATH_MAX];
    char manpath[IOTPM_PATH_MAX];
    char packages[IOTPM_PATH_MAX];
    char errbuf[256];

    if (!iotpm)
        return false;

    iotpm->backend = NULL;

    snprintf(dbpath, sizeof(dbpath), DB_DIR, iotpm->homedir);
    snprintf(seedpath, sizeof(seedpath), SEED_DIR, iotpm->homedir);
    iot_manifest_dir(iotpm->userid, manpath, sizeof(manpath));
    snprintf(packages, sizeof(packages), "%s/Packages", dbpath);

    if (!(backend = iot_allocz(sizeof(iotpm_backend_t))) ||
	!(backend->pkgmgr.name = iot_strdup(rpmNAME))    ||
	!(backend->pkgmgr.version = iot_strdup(rpmEVR))  ||
	!(backend->path.db = iot_strdup(dbpath))         ||
	!(backend->path.seed = iot_strdup(seedpath))     ||
	!(backend->path.manifest = iot_strdup(manpath))   )
    {
        error = "can't allocate memory for RPM backend";
        goto failed;
    }

    backend->iotpm = iotpm;
    iotpm->backend = backend;

    rpmSetVerbosity(RPMLOG_WARNING);

    rpmlogSetCallback(log_callback, (void *)backend);

    error = errbuf;

    if (iot_mkdir((dir = dbpath)  , 0755, iotpm->default_label) < 0 ||
        iot_mkdir((dir = seedpath), 0755, iotpm->default_label) < 0 ||
	iot_mkdir((dir = manpath) , 0755, iotpm->default_label) < 0  )
    {
        snprintf(errbuf, sizeof(errbuf), "failed to create directory '%s': %s",
                 dir, strerror(errno));
        goto failed;
    }

    if (access(packages, R_OK|W_OK) < 0 && errno == ENOENT) {
        if (!database_copy(system_dbpath, dbpath, NULL)) {
            error = "database initialization failed";
            goto failed;
        }
    }

    return true;

 failed:
    iot_log_error("%s", error);
    iotpm_backend_exit(iotpm);
    return false;
}

void iotpm_backend_exit(iotpm_t *iotpm)
{
    iotpm_backend_t *backend;

    if (iotpm && (backend = iotpm->backend)) {
        iot_free((void *)backend->pkgmgr.name);
        iot_free((void *)backend->pkgmgr.version);
	iot_free((void *)backend->path.db);
	iot_free((void *)backend->path.seed);
	iot_free((void *)backend->path.manifest);

	iot_free((void *)backend);

	iotpm->backend = NULL;
    }
}


iotpm_pkginfo_t *iotpm_backend_pkginfo_create(iotpm_t *iotpm,
					      bool file,
					      const char *pkg)
{
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    QVA_t qva = &rpmQVKArgs;
    int qargc;
    char *qargv[16];
    ARGV_const_t arg;
    ARGV_t av;
    int ac;
    char *n;
    poptContext optCtx;
    bool gst;
    iotpm_pkginfo_t *info;

    if (!iotpm || !(backend = iotpm->backend))
        return NULL;

    if (!(info = iot_allocz(sizeof(iotpm_pkginfo_t))))
        return &failed_info;

    info->sts = -1;
    info->backend = backend;

    if (!pkg)
        return info;

    if (file) {
        n = rpmEscapeSpaces(pkg);
	gst = (rpmGlob(n, &ac, &av) == 0 && ac == 1);

	if (gst)
	    info->file = iot_strdup(av[0]);

	free((void *)n);
	argvFree(av);

	if (!gst || (gst && !info->file))
	    return info;
    }

    qargv[qargc=0] = (char *)iotpm->prognam;
    qargv[++qargc] = "-q";
    if (file)
        qargv[++qargc] = "-p";
    else {
        qargv[++qargc] = "--dbpath";
	qargv[++qargc] = backend->path.db;
    }
    qargv[++qargc] = "-l";
    qargv[++qargc] = (char *)pkg;
    qargv[++qargc] = NULL;

    memset(qva, 0, sizeof(*qva));
    qva->qva_showPackage = pkginfo_fill;
    qva->qva_queryFormat = (char *)info;

    optCtx = rpmcliInit(qargc, qargv, queryOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmcliRootDir);

    arg = (ARGV_const_t)poptGetArgs(optCtx);

    if (rpmcliQuery(ts, qva, arg) == 0)
        info->sts = 0;

    rpmtsFree(ts);
    rpmcliFini(optCtx);

    return info;
}

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

	    iot_free((void *)info->name);
	    iot_free((void *)info->ver);
	    iot_free((void *)info->files);
	    iot_free(info->data);
	}

	if (info != &failed_info)
	    iot_free((void *)info);
    }
}

bool iotpm_backend_install_package(iotpm_t *iotpm, const char *pkg)
{
    return install_package(iotpm, INSTALL, pkg);
}

bool iotpm_backend_upgrade_package(iotpm_t *iotpm, const char *pkg)
{
    return install_package(iotpm, UPGRADE, pkg);
}

bool iotpm_backend_remove_package(iotpm_t *iotpm, const char *pkg)
{
    struct rpmInstallArguments_s *ia = &rpmIArgs;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    poptContext optCtx;
    int qargc;
    char *qargv[16];
    ARGV_const_t arg;
    bool success;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        return false;

    memset(ia, 0, sizeof(*ia));

    qargv[qargc=0] = (char *)iotpm->prognam;
    qargv[++qargc] = "-e";
    qargv[++qargc] = "--dbpath";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = (char *)pkg;
    qargv[++qargc] = NULL;

    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmcliRootDir);

    arg = (ARGV_const_t)poptGetArgs(optCtx);

    success = (rpmErase(ts, ia, arg) == 0) ? true : false;

    rpmtsFree(ts);
    rpmcliFini(optCtx);

    return success;
}

bool iotpm_backend_seed_create(iotpm_pkginfo_t *info)
{
    int fd = -1;
    bool success = false;
    iotpm_backend_t *backend;
    iotpm_t *iotpm;
    char path[1024];
    struct stat st;

    if (!info || !(backend = info->backend) || !(iotpm = backend->iotpm) ||
	!info->data || !info->length)
    {
        iot_log_error("failed to create seed: internal error");
	goto out;
    }

    snprintf(path, sizeof(path), "%s/%s", backend->path.seed, info->name);

    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode)) {
	    iot_log_error("failed to create seed '%s': "
			  "already exists", path);
	}
	else {
	    iot_log_error("failed to create seed '%s': "
			  "there is something with the same", path);
	}
	goto out;
    }
    else {
        if (errno != ENOENT) {
	    iot_log_error("failed to create seed '%s': %s",
			  path, strerror(errno));
	    goto out;
	}
    }

    if ((fd = open(path, O_RDWR|O_CREAT|O_EXCL, 0644)) < 0) {
        iot_log_error("failed to create seed '%s': %s", path, strerror(errno));
	goto out;
    }

    if (!file_write(fd, path, rpm_header_magic, sizeof(rpm_header_magic)) ||
	!file_write(fd, path, info->data, info->length)                    )
    {
        unlink(path);
        goto out;
    }

    success = true;

 out:
    if (fd >= 0)
        close(fd);

    return success;
}


bool iotpm_backend_seed_destroy(iotpm_pkginfo_t *info)
{
    bool success = false;
    iotpm_backend_t *backend;
    iotpm_t *iotpm;
    char path[1024];
    struct stat st;

    if (!info || !(backend = info->backend) || !(iotpm = backend->iotpm)) {
        iot_log_error("failed to destroy seed: internal error");
	goto out;
    }

    snprintf(path, sizeof(path), "%s/%s", backend->path.seed, info->name);

    if (stat(path, &st) < 0) {
        iot_log_error("failed to destroy seed '%s': %s", path,strerror(errno));
	goto out;
    }

    if (!S_ISREG(st.st_mode)) {
        iot_log_error("failed to destroy seed '%s': not a regular file", path);
	goto out;
    }

    if (unlink(path) < 0) {
        iot_log_error("failed to destroy seed '%s': %s", path,strerror(errno));
        goto out;
    }

    success = true;

 out:
    return success;
}


bool iotpm_backend_seed_plant(iotpm_t *iotpm, const char *pkg)
{
    iotpm_backend_t *backend;
    ARGV_t av = NULL;
    char *n = NULL;
    struct rpmInstallArguments_s *ia;
    int qargc;
    char *qargv[16];
    poptContext optCtx;
    rpmts ts;
    rpmVSFlags vsflags;
    rpmdb rdb;
    Header h;
    int ac, i;
    bool success = false;
    char path[1024];
    void *buf;
    size_t length;
    const char *name;
    bool installed;
    rpmdbMatchIterator mi;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        goto out;

    snprintf(path,  sizeof(path),  "%s/%s", backend->path.seed, pkg);

    n = rpmEscapeSpaces(path);
    if (rpmGlob(n, &ac, &av) != 0 || ac < 1 || !av)
        goto out;

    success = true;

    qargv[qargc=0] = (char *)iotpm->prognam;
    qargv[++qargc] = "-i";
    qargv[++qargc] = "--justdb";
    qargv[++qargc] = "--dbpath";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = "--noscripts";
    qargv[++qargc] = NULL;

    memset(&rpmIArgs, 0, sizeof(rpmIArgs));
    memset(&rpmcliQueryFlags, 0, sizeof(rpmcliQueryFlags));
    ia = &rpmIArgs;
    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);
    ia->transFlags |= RPMTRANS_FLAG_NODOCS;

    ts = rpmtsCreate();

    vsflags = rpmExpandNumeric("%{?_vsflags_install}");
    rpmtsSetVSFlags(ts, vsflags /* | RPMVSF_NEEDPAYLOAD */);

    if (rpmtsOpenDB(ts, O_RDWR|O_CREAT) != 0 || !(rdb = rpmtsGetRdb(ts))) {
      iot_log_error("failed to plant seed '%s': DB opening failed", pkg);
    }
    else {
        iot_log_info("planting seeds of");

        for (i = 0;  i < ac;   i++) {
	    iot_log_info("   - %s", av[i]);

	    if (!(buf = seed_read(av[i], &length))) {
	        success = false;
		continue;
	    }

	    if (!(h = headerImport(buf, length, 0))      ||
		!(name = headerGetString(h, RPMTAG_NAME)) )
	    {
	        iot_log_error("failed to plant seed '%s': "
			      "header recovery failed", av[i]);
		success = false;
		iot_free(buf);
		continue;
	    }

	    installed = false;
	    if ((mi = rpmdbInitIterator(rdb, RPMDBI_NAME, name,strlen(name)))){
	        if (rpmdbGetIteratorCount(mi) > 0)
		    installed = true;
		rpmdbFreeIterator(mi);
	    }

	    if (installed) {
	        iot_log_error("failed to plant seed '%s': "
			      "'%s' already installed", av[i], name);
		success = false;
		iot_free(buf);
		continue;
	    }

	    if (rpmdbAdd(rdb, h) != 0) {
	        iot_log_error("failed to plant seed '%s': "
			      "DB insertion failed", av[i]);
		success = false;
	    }

	    headerFree(h);
	}

	rpmtsCloseDB(ts);
    }

    rpmtsFree(ts);
    rpmcliFini(optCtx);

 out:
    iot_free((void *)n);
    argvFree(av);
    return success;
}

bool iotpm_backend_verify_db(iotpm_t *iotpm)
{
    iotpm_backend_t *backend;
    QVA_t qva = &rpmQVKArgs;
    poptContext optCtx;
    rpmts ts;
    int qargc;
    char *qargv[16];
    bool ok = false;

    if (iotpm && (backend = iotpm->backend)) {
        memset(qva, 0, sizeof(*qva));

	qargv[qargc=0] = (char *)iotpm->prognam;
	qargv[++qargc] = "-V";
	qargv[++qargc] = "-a";
	qargv[++qargc] = "--dbpath";
	qargv[++qargc] = backend->path.db;
	qargv[++qargc] = NULL;

	optCtx = rpmcliInit(qargc, qargv, verifyOptionsTable);

	ts = rpmtsCreate();
	rpmtsSetRootDir(ts, rpmcliRootDir);

	qva->qva_flags = VERIFY_ALL;

	ok = (rpmcliVerify(ts, qva, NULL) == 0) ? true : false;

	rpmtsFree(ts);
	rpmcliFini(optCtx);
    }

    return ok;
}


static int pkginfo_fill(QVA_t qva, rpmts ts, Header h)
{
    iotpm_pkginfo_t *info = (iotpm_pkginfo_t *)qva->qva_queryFormat;
    iotpm_backend_t *backend;
    HeaderIterator hi = NULL;
    rpmfi fi = NULL;
    rpmfiFlags fiflags =  (RPMFI_NOHEADER | RPMFI_FLAGS_QUERY);
    int sts = 0;
    iotpm_pkginfo_processing_t proc;
    rpmTagVal tag;
    iotpm_pkginfo_filentry_t *f;
    const char *path, *user, *group, *link;
    size_t size;
    void *data;
    unsigned int length;
    char manfile[IOTPM_PATH_MAX];
    int i;


    if (!info || !(backend = info->backend))
        return -1;

    /* basic package info */
    if (!(info->name = headerGetAsString(h, RPMTAG_NAME))   ||
	!(info->ver = headerGetAsString(h, RPMTAG_VERSION))  )
    {
        return -1;
    }

    /* check for pre/post install/unistall scripts/programs */
    proc = IOTPM_PROCESSING_NONE;
    hi = headerInitIterator(h);
    while ((tag = headerNextTag(hi)) != RPMTAG_NOT_FOUND) {
        switch (tag) {
	case RPMTAG_PREIN:         proc |= IOTPM_PROCESSING_PREIN;       break;
	case RPMTAG_POSTIN:        proc |= IOTPM_PROCESSING_POSTIN;      break;
	case RPMTAG_PREUN:         proc |= IOTPM_PROCESSING_PREUN;       break;
	case RPMTAG_POSTUN:        proc |= IOTPM_PROCESSING_POSTUN;      break;
	case RPMTAG_PREINPROG:     proc |= IOTPM_PROCESSING_PREIN;       break;
	case RPMTAG_POSTINPROG:    proc |= IOTPM_PROCESSING_POSTIN;      break;
	case RPMTAG_PREUNPROG:     proc |= IOTPM_PROCESSING_PREUN;       break;
	case RPMTAG_POSTUNPROG:    proc |= IOTPM_PROCESSING_POSTUN;      break;
        default:                                                         break;
        }
    }
    info->proc = proc;

    /* build file list */
    snprintf(manfile, sizeof(manfile), "%s/%s.manifest",
             backend->path.manifest, info->name);

    fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, fiflags);

    if (rpmfiFC(fi) <= 0)
        info->files = iot_allocz(sizeof(iotpm_pkginfo_filentry_t));
    else {
        for (i = 0, fi = rpmfiInit(fi, 0);  rpmfiNext(fi) >= 0;  i++) {
	    size = sizeof(iotpm_pkginfo_filentry_t) * (i + 2);

	    if (!(info->files = iot_realloc(info->files, size))) {
	        sts = -1;
	        break;
	    }

	    f = info->files + i;
	    memset(f, 0, sizeof(iotpm_pkginfo_filentry_t) * 2);

	    f->flags = rpmfiFFlags(fi);
	    f->mode = rpmfiFMode(fi);

	    if (!(path  = rpmfiFN(fi))     || !(f->path  = iot_strdup(path)) ||
		!(user  = rpmfiFUser(fi))  || !(f->user  = iot_strdup(user)) ||
	        !(group = rpmfiFGroup(fi)) || !(f->group = iot_strdup(group)) )
	    {
	        sts = -1;
	        break;
	    }

	    if ((link = rpmfiFLink(fi)) && *link &&
		!(f->link = iot_strdup(link)))
	    {
	        sts = -1;
	        break;
	    }

            if (!strcmp(f->path, manfile))
                info->manifest = f;

	    info->nfile = i + 1;
        }
    }

    rpmfiFree(fi);

    if (sts == 0) {
        if (!(data = headerExport(h, &length)) || !length)
          sts = -1;
	else {
	    info->data = data;
	    info->length = length;
	}
    }


    return sts;
}

static bool install_package(iotpm_t *iotpm, bool upgrade, const char *pkg)
{
    struct rpmInstallArguments_s *ia = &rpmIArgs;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    poptContext optCtx;
    int qargc;
    char *qargv[16];
    char *n;
    bool gst;
    char *file;
    ARGV_t av, arg;
    int ac;
    bool success;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        return false;

    /* get the actual file name */
    n = rpmEscapeSpaces(pkg);
    gst = (rpmGlob(n, &ac, &av) == 0 && ac == 1);

    if (gst)
        file = iot_strdup(av[0]);

    iot_free((void *)n);
    argvFree(av);

    memset(ia, 0, sizeof(*ia));

    qargv[qargc=0] = (char *)iotpm->prognam;
    qargv[++qargc] = upgrade ? "-U" : "-i";
    qargv[++qargc] = "--dbpath";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = file;
    qargv[++qargc] = NULL;

    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmcliRootDir);

    arg = (ARGV_t)poptGetArgs(optCtx);

    success = (rpmInstall(ts, ia, arg) == 0) ? true : false;

    rpmtsFree(ts);
    rpmcliFini(optCtx);

    return success;
}


static void *seed_read(const char *path, size_t *length_ret)
{
    unsigned char magic[8];
    struct stat st;
    size_t length;
    int fd = -1;
    void *buf = NULL;

    if (!path || !length_ret)
        goto failed;

    if (stat(path, &st) < 0) {
        iot_log_error("failed to read seed '%s': %s", path, strerror(errno));
	goto failed;
    }

    if (!S_ISREG(st.st_mode)                   ||
	st.st_size < (off_t)sizeof(magic) + 10 ||
	st.st_size > HEADER_LENGTH_MAX          )
    {
        iot_log_error("failed to reed seed '%s': not a seed", path);
	goto failed;
    }

    length = st.st_size - sizeof(magic);

    if (!(buf = iot_alloc(length))) {
        iot_log_error("failed to read seed '%s': not enough memory", path);
	goto failed;
    }

    if ((fd = open(path, O_RDONLY)) < 0) {
        iot_log_error("failed to read seed '%s': %s", path, strerror(errno));
	goto failed;
    }

    if (!file_read(fd, path, magic, sizeof(magic))    ||
	memcmp(magic, rpm_header_magic, sizeof(magic)) )
    {
        iot_log_error("failed to read seed '%s': bad magic", path);
	goto failed;
    }

    if (!file_read(fd, path, buf, length)) {
        goto failed;
    }

    close(fd);

    *length_ret = length;
    return buf;

 failed:
    iot_free(buf);
    if (fd >= 0)
        close(fd);
    return NULL;
}

static int log_callback(rpmlogRec rec, rpmlogCallbackData userdata)
{
    iotpm_backend_t *backend =(iotpm_backend_t *)userdata;
    const char *msg;
    char *nl, buf[1024];
    int sts = 0;

    if (!rec || !backend) {
        iot_log_error("log_callback(): invalid argument");
        sts = RPMLOG_EXIT;
    }
    else {
        if ((msg = rpmlogRecMessage(rec))) {
	    strncpy(buf, msg, sizeof(buf));
	    buf[sizeof(buf) - 1] = 0;

	    if ((nl = strrchr(buf, '\n')))
	        *nl = 0;

	    switch (rpmlogRecPriority(rec)) {
	    case RPMLOG_EMERG:
	        sts = RPMLOG_EXIT;
		/* intentional fall over */
	    case RPMLOG_ALERT:
	    case RPMLOG_CRIT:
	    case RPMLOG_ERR:
	    default:
	        iot_log_error("%s", buf);
	        break;

	    case RPMLOG_WARNING:
	        iot_log_warning("%s", buf);
		break;


	    case RPMLOG_NOTICE:
	    case RPMLOG_INFO:
	        iot_log_info("%s", buf);
	        break;

	    case RPMLOG_DEBUG:
	        iot_debug("%s", buf);
	        break;
	    } /* case */
	}
    }

    return sts;
}

#include "rpmx-backend.c"
