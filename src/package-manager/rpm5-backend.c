#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <alloca.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

#include <popt.h>

#include <rpm/argv.h>
#include <rpm/rpmtypes.h>
#include <rpm/rpmio.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmte.h>
#include <rpm/rpmts.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmversion.h>
#include <rpm/rpmgi.h>
#include <rpm/rpmtxn.h>
//#include <rpm/rpmlock.h>
#define _RPMLOG_INTERNAL
#include <rpm/rpmlog.h>
#undef _RPMLOG_INTERNAL


#include <iot/common.h>

#include "backend.h"
#include "rpmx-backend.h"


#define RPM_DIR                   IOTPM_PACKAGE_HOME "/rpm/"
#define DB_DIR                    RPM_DIR "db"
#define SEED_DIR                  RPM_DIR "seed"
#define REPACKAGE_DIR             RPM_DIR "repackage"

#define HEADER_LENGTH_MAX         (32 * 1024 * 1024)

#define INSTALL                   false
#define UPGRADE                   true

#define RPMTAG_NOT_FOUND          (~(rpmTag)0)

#define POPT_PROGNAM              "rpm" /* iotpm->prognam */


/* since poptIO.h is not publicly available ... */
extern const char *rpmioRootDir;

static int pkginfo_fill(QVA_t, rpmts, Header);
static int pkglist_fill(QVA_t, rpmts, Header);

static bool install_package(iotpm_t *, bool, const char *);

static void *seed_read(const char *, size_t *);

static const char *headerGetString(Header, rpmTag);
static const char *headerGetAsString(Header, rpmTag);
static uint32_t *headerGetUint32Array(Header, rpmTag, uint32_t *);
static rpmTag headerNextTag(HeaderIterator);
static void headerDelete(Header, rpmTag);

static int log_callback(rpmlogRec, rpmlogCallbackData);



static iotpm_pkginfo_t  failed_info = {
    .sts = -1
};
static iotpm_pkglist_t  failed_list = {
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
    char dbdir[IOTPM_PATH_MAX];
    char dbpath[IOTPM_PATH_MAX];
    char seedpath[IOTPM_PATH_MAX];
    char repackpath[IOTPM_PATH_MAX];
    char manpath[IOTPM_PATH_MAX];
    char packages[IOTPM_PATH_MAX];
    char errbuf[256];

    if (!iotpm)
        return false;

    snprintf(dbdir, sizeof(dbdir), DB_DIR, iotpm->homedir);
    snprintf(dbpath, sizeof(dbpath), "_dbpath %s", dbdir);
    snprintf(seedpath, sizeof(seedpath), SEED_DIR, iotpm->homedir);
    snprintf(repackpath, sizeof(repackpath), REPACKAGE_DIR, iotpm->homedir);
    iot_manifest_dir(iotpm->userid, manpath, sizeof(manpath));
    snprintf(packages, sizeof(packages), "%s/Packages", dbdir);

    iotpm->backend = NULL;

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

    rpmlogSetMask(RPMLOG_UPTO(RPMLOG_WARNING));

    rpmlogSetCallback(log_callback, (void *)backend);

    error = errbuf;

    if (iot_mkdir((dir = dbdir)     , 0755, iotpm->default_label) < 0 ||
        iot_mkdir((dir = seedpath)  , 0755, iotpm->default_label) < 0 ||
        iot_mkdir((dir = repackpath), 0755, iotpm->default_label) < 0 ||
	iot_mkdir((dir = manpath)   , 0755, iotpm->default_label) < 0  )
    {
        snprintf(errbuf, sizeof(errbuf), "failed to create directory '%s': %s",
                 dir, strerror(errno));
        goto failed;
    }

    if (access(packages, R_OK|W_OK) < 0 && errno == ENOENT) {
        if (!database_copy(system_dbpath, dbdir, NULL)) {
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
    iotpm_pkginfo_t *info;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    QVA_t qva = &rpmQVKArgs;
    int qargc;
    char *qargv[16];
    ARGV_t arg;
    ARGV_t av;
    int ac;
    const char *n;
    poptContext optCtx;
    bool gst;

    if (!iotpm || !(backend = iotpm->backend))
        return NULL;

    if (!(info = iot_allocz(sizeof(iotpm_pkginfo_t))))
        return &failed_info;

    info->sts = -1;
    info->backend = backend;

    if (!pkg)
        return info;

    if (file) {
        n = rpmgiEscapeSpaces(pkg);
	gst = (rpmGlob(n, &ac, &av) == 0 && ac == 1);

	if (gst)
	    info->file = iot_strdup(av[0]);

	free((void *)n);
	argvFree(av);

	if (!gst || (gst && !info->file))
	    return info;
    }


    qargv[qargc=0] = POPT_PROGNAM;
    qargv[++qargc] = "--define";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = "-q";
    if (file)
        qargv[++qargc] = "-p";
    qargv[++qargc] = "-l";
    qargv[++qargc] = (char *)pkg;
    qargv[++qargc] = NULL;

    memset(qva, 0, sizeof(*qva));
    qva->qva_showPackage = pkginfo_fill;
    qva->qva_queryFormat = (char *)info;

    optCtx = rpmcliInit(qargc, qargv, queryOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmioRootDir);

    arg = (ARGV_t)poptGetArgs(optCtx);

    if (rpmcliQuery(ts, qva, arg) == 0)
        info->sts = 0;

    rpmtsFree(ts);
    rpmcliFini(optCtx);

    return info;
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
    struct rpmQVKArguments_s *ia = &rpmIArgs;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    poptContext optCtx;
    int qargc;
    char *qargv[16];
    const char **arg;
    char repackagedir[1024];
    bool success;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        return false;

    snprintf(repackagedir, sizeof(repackagedir),
	     "_repackage_dir " REPACKAGE_DIR, iotpm->homedir);

    memset(ia, 0, sizeof(*ia));

    qargv[qargc=0] = POPT_PROGNAM;
    qargv[++qargc] = "--define";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = "--define";
    qargv[++qargc] = repackagedir;
    qargv[++qargc] = "-e";
    qargv[++qargc] = (char *)pkg;
    qargv[++qargc] = NULL;

    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmioRootDir);

    arg = poptGetArgs(optCtx);

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

    if (!file_write(fd, path, info->data, info->length)) {
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
    const char *n = NULL;
    struct rpmQVKArguments_s *ia;
    int qargc;
    char *qargv[16];
    poptContext optCtx;
    rpmts ts;
    rpmtxn txn;
    rpmVSFlags vsflags;
    rpmdb rdb;
    Header h;
    int ac, i;
    bool success = false;
    char path[1024];
    void *buf;
    size_t length;
    uint32_t tid;
    const char *name;
    void *lock = NULL;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        goto out;

    snprintf(path,  sizeof(path),  "%s/%s", backend->path.seed, pkg);

    n = rpmgiEscapeSpaces(path);
    if (rpmGlob(n, &ac, &av) != 0 || ac < 1 || !av)
        goto out;

    qargv[qargc=0] = POPT_PROGNAM;
    qargv[++qargc] = "--define";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = "-i";
    qargv[++qargc] = "--nodeps";
    qargv[++qargc] = "--justdb";
    qargv[++qargc] = "--noscripts";
    qargv[++qargc] = NULL;

    memset(&rpmIArgs, 0, sizeof(rpmIArgs));
    memset(&rpmcliQueryFlags, 0, sizeof(rpmcliQueryFlags));
    ia = &rpmIArgs;
    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);
    ia->transFlags |= RPMTRANS_FLAG_NODOCS;

    ts = rpmtsCreate();

    rpmtsSetGoal(ts, TSM_INSTALL);
    rpmcliPackagesTotal = 0;

    rpmtsSetFlags(ts, ia->transFlags);
    rpmtsSetDFlags(ts, ia->depFlags);

    vsflags = rpmExpandNumeric("%{?_vsflags_install}");
    vsflags = 0;	/* RPM5 FIXME: ignore default disabler */
    rpmtsSetVSFlags(ts, vsflags /* | RPMVSF_NEEDPAYLOAD */);

    rpmfiAddRelocation(&ia->relocations, &ia->nrelocations, NULL, NULL);

    lock = rpmtsAcquireLock(ts);

    if (rpmtsOpenDB(ts, O_RDWR|O_CREAT) != 0 || !(rdb = rpmtsGetRdb(ts))) {
      iot_log_error("failed to plant seed '%s': DB opening failed", pkg);
    }
    else {
        if (rpmtxnBegin(rdb, NULL, NULL) != 0)
	    iot_log_error("failed to plant seed '%s': transaction error", pkg);
	else {
	    txn = rpmdbTxn(rdb);
	    rpmtsSetTxn(ts, txn);
	    tid = rpmtxnId(txn);

	    success = true;

	    iot_log_info("planting seeds of");

	    for (i = 0;  i < ac;   i++) {
	        iot_log_info("   - %s", av[i]);

		h = NULL;
		buf = NULL;

		do {
		    if (!(buf = seed_read(av[i], &length))) {
		        success = false;
			break;
		    }

		    if (!(h = headerLoad(buf))                   ||
			!(name = headerGetString(h, RPMTAG_NAME)) )
		    {
		        iot_log_error("failed to plant seed '%s': "
				      "header recovery failed", av[i]);
			success = false;
			break;
		    }

		    if (rpmdbCountPackages(rdb, name) > 0) {
		        iot_log_error("failed to plant seed '%s': "
				      "'%s' already installed", av[i], name);
			success = false;
			break;
		    }

		    //headerDelete(h, RPMTAG_INSTALLTID);

		    if (rpmdbAdd(rdb, tid, h, ts) != 0) {
		        iot_log_error("failed to plant seed '%s': "
				      "DB insertion failed", av[i]);
			success = false;
			break;
		    }
		} while(false);

		headerFree(h);
		iot_free(buf);
	    } /* for */

	    if (success)
	        rpmtxnCommit(txn);
	    else
	        rpmtxnAbort(txn);

	    rpmtsSetTxn(ts, NULL);
	}

	rpmtsCloseDB(ts);
    }

    rpmtsFreeLock(lock);

    rpmtsFree(ts);
    rpmcliFini(optCtx);

 out:
    free((void *)n);
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

	qargv[qargc=0] = POPT_PROGNAM;
	qargv[++qargc] = "--define";
	qargv[++qargc] = backend->path.db;
	qargv[++qargc] = "-V";
	qargv[++qargc] = "-a";
	qargv[++qargc] = NULL;

	optCtx = rpmcliInit(qargc, qargv, verifyOptionsTable);

	ts = rpmtsCreate();
	rpmtsSetRootDir(ts, rpmioRootDir);

	qva->qva_flags = VERIFY_ALL;

	ok = (rpmcliVerify(ts, qva, NULL) == 0) ? true : false;

	rpmtsFree(ts);
	rpmcliFini(optCtx);
    }

    return ok;
}


iotpm_pkglist_t *iotpm_backend_pkglist_create(iotpm_t *iotpm, iot_regexp_t *re)
{
    iotpm_pkglist_t *list;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    QVA_t qva = &rpmQVKArgs;
    int qargc;
    char *qargv[16];
    poptContext optCtx;

    if (!iotpm || !(backend = iotpm->backend))
        return NULL;

    if (!(list = iot_allocz(sizeof(iotpm_pkglist_t))))
        return &failed_list;

    list->sts = -1;
    list->backend = backend;
    list->re = re;

    qargv[qargc=0] = POPT_PROGNAM;
    qargv[++qargc] = "--define";
    qargv[++qargc] = backend->path.db;
    qargv[++qargc] = "-q";
    qargv[++qargc] = "-a";
    qargv[++qargc] = NULL;

    memset(qva, 0, sizeof(*qva));
    qva->qva_showPackage = pkglist_fill;
    qva->qva_queryFormat = (char *)list;

    optCtx = rpmcliInit(qargc, qargv, queryOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmioRootDir);

    if (rpmcliQuery(ts, qva, NULL) == 0)
        list->sts = 0;

    rpmtsFree(ts);
    rpmcliFini(optCtx);

    return list;
}


static int pkginfo_fill(QVA_t qva, rpmts ts, Header h)
{
    iotpm_pkginfo_t *info = (iotpm_pkginfo_t *)qva->qva_queryFormat;
    iotpm_backend_t *backend;
    HeaderIterator hi = NULL;
    rpmfi fi = NULL;
    int sts = 0;
    iotpm_pkginfo_processing_t proc;
    rpmTag tag;
    iotpm_pkginfo_filentry_t *f;
    const char *path, *user, *group, *link;
    size_t size;
    void *data;
    size_t length;
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
    hi = headerInit(h);
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

    fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, 0);

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
        if (!(data = headerUnload(h, &length)) || !length)
          sts = -1;
	else {
	    info->data = data;
	    info->length = length;
	}
    }


    return sts;
}

static int pkglist_fill(QVA_t qva, rpmts ts, Header h)
{
    iotpm_pkglist_t *list = (iotpm_pkglist_t *)qva->qva_queryFormat;
    iotpm_pkglist_entry_t *e;
    const char *name;
    const char *version;
    uint32_t *array;
    uint32_t dim;
    time_t install_time;
    size_t size;
    int len;

    IOT_UNUSED(ts);

    if (!(name = headerGetAsString(h, RPMTAG_NAME))      ||
	!(version = headerGetAsString(h, RPMTAG_VERSION)) )
        return -1;

    if (!(array = headerGetUint32Array(h, RPMTAG_INSTALLTIME, &dim)))
        install_time = 0;
    else {
        install_time = array[0];
        free(array);
    }

    if (!list->re || iot_regexp_matches(list->re, name, 0)) {
        size = sizeof(iotpm_pkglist_entry_t) * (list->nentry + 2);

        if (!(list->entries = iot_realloc(list->entries, size)))
            return -1;

        e = list->entries + list->nentry++;
        memset(e, 0, sizeof(iotpm_pkglist_entry_t) * 2);

        e->name = name;
        e->version = version;
        e->install_time = install_time;

        if ((len = strlen(name)) > list->max_width.name)
            list->max_width.name = len;

        if ((len = strlen(version)) > list->max_width.version)
            list->max_width.version = len;
    }

    return 0;
}


static bool install_package(iotpm_t *iotpm, bool upgrade, const char *pkg)
{
    struct rpmQVKArguments_s *ia = &rpmIArgs;
    iotpm_backend_t *backend;
    rpmts ts = NULL;
    poptContext optCtx;
    int qargc;
    char *qargv[16];
    const char *n;
    bool gst;
    char *file;
    ARGV_t av, arg;
    int ac;
    bool success;

    if (!iotpm || !(backend = iotpm->backend) || !pkg)
        return false;

    /* get the actual file name */
    n = rpmgiEscapeSpaces(pkg);
    gst = (rpmGlob(n, &ac, &av) == 0 && ac == 1);

    if (gst)
        file = iot_strdup(av[0]);

    free((void *)n);
    argvFree(av);

    memset(ia, 0, sizeof(*ia));

    qargv[qargc=0] = POPT_PROGNAM;
    qargv[++qargc] = "--define";
    qargv[++qargc] = backend->path.db;
#if 0
    qargv[++qargc] = "--define";
    qargv[++qargc] = "_rollback_transaction_on_failure 1";
#endif
    qargv[++qargc] = upgrade ? "-U" : "-i";
    qargv[++qargc] = file;
    qargv[++qargc] = NULL;

    optCtx = rpmcliInit(qargc, qargv, installOptionsTable);

    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rpmioRootDir);

    ia->depFlags = global_depFlags;
    if (ia->noDeps)
        ia->installInterfaceFlags |= INSTALL_NODEPS;

    rpmfiAddRelocation(&ia->relocations, &ia->nrelocations, NULL, NULL);

    arg = (ARGV_t)poptGetArgs(optCtx);

    success = (rpmcliInstall(ts, ia, arg) == 0) ? true : false;

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

    if (!S_ISREG(st.st_mode)            ||
	st.st_size < sizeof(magic) + 10 ||
	st.st_size > HEADER_LENGTH_MAX   )
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

static const char *headerGetString(Header h, rpmTag tag)
{
    HE_t he = (HE_t)memset(alloca(sizeof(*he)), 0, sizeof(*he));

    if (!h || !he)
        return NULL;

    he->tag = tag;

    if (!headerGet(h, he, 0) || he->t != RPM_STRING_TYPE)
        return NULL;

    return he->p.str;
}

static const char *headerGetAsString(Header h, rpmTag tag)
{
    const char *str = headerGetString(h, tag);

    return str ? iot_strdup(str) : NULL;
}

static uint32_t *headerGetUint32Array(Header h,rpmTag tag,uint32_t *dim)
{
    HE_t he = (HE_t)memset(alloca(sizeof(*he)), 0, sizeof(*he));

    if (!h || !he)
        return NULL;

    if (dim)
        *dim = 0;

    he->tag = tag;

    if (!headerGet(h, he, 0) || he->t != RPM_UINT32_TYPE || he->c < 1)
        return NULL;

    if (dim)
        *dim = he->c;

    return he->p.ui32p;
}

static rpmTag headerNextTag(HeaderIterator hi)
{
    HE_t he = (HE_t)memset(alloca(sizeof(*he)), 0, sizeof(*he));

    if (!hi || !he || !headerNext(hi, he, 0))
        return RPMTAG_NOT_FOUND;

    return he->tag;
}

static void headerDelete(Header h, rpmTag tag)
{
    HE_t he = (HE_t)memset(alloca(sizeof(*he)), 0, sizeof(*he));

    if (h && he) {
        he->tag = tag;
	headerDel(h, he, 0);
    }
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
      if ((msg = rec->message)) {
	    strncpy(buf, msg, sizeof(buf));
	    buf[sizeof(buf) - 1] = 0;

	    if ((nl = strrchr(buf, '\n')))
	        *nl = 0;

	    switch (rec->pri) {
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
