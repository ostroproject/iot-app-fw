#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iot/common.h>

#include "backend.h"

typedef struct script_s   script_t;

struct script_s {
    iotpm_pkginfo_processing_t mask;
    const char *name;
};


static bool verify_files(iotpm_pkginfo_t *);
static bool verify_scripts(iotpm_pkginfo_t *);

bool iotpm_pkginfo_verify(iotpm_pkginfo_t *info)
{
    bool success = true;

    if (!info || !info->name || !info->backend || !info->backend->iotpm) {
        iot_log_error("internal error");
	return false;
    }

    success &= verify_files(info);
    success &= verify_scripts(info);

    return success;
}


static bool verify_files(iotpm_pkginfo_t *info)
{
    iotpm_backend_t *backend = info->backend;
    iotpm_t *iotpm = backend->iotpm;
    bool success = true;
    iotpm_pkginfo_filentry_t *f;
    const char *path;
    mode_t mode;
    char dir[IOTPM_PATH_MAX];
    size_t len_min, len_max, len, plen;
    bool local, conf;
    int i;

    if (!strcmp(iotpm->username, "root"))
        return true;

    len_min = strlen(iotpm->homedir);
    len_max = snprintf(dir,sizeof(dir), "%s/%s/", iotpm->homedir, info->name);

    for (i = 0;  i < info->nfile;  i++) {
        f = info->files + i;
	path = f->path;
	mode = f->mode;
	plen = strlen(path);
	len  = (plen > len_max) ? len_max : plen;

	local = (len >= len_min && !strncmp(path, dir, len));
	conf = !strcmp(path, "/etc") || !strncmp(path, "/etc/", 5);

	if (!local && !conf) {
	    iot_log_error("file '%s' is neither on '%s' nor on '/etc/' paths",
			  path, dir);
	    success = false;
	}

	if (local && strcmp(f->user, iotpm->username)) {
	    iot_log_error("owner of file '%s' supposed to be '%s' not '%s'",
			  path, iotpm->username, f->user);
	    success = false;
	}

	if (local && (mode & S_IWOTH)) {
	    iot_log_error("file '%s' can be written by anyone", path);
	    success = false;
	}

	if ((mode & S_ISUID)) {
	    iot_log_error("setuid flag is set for file '%s'", path);
	    success = false;
	}

	if ((mode & S_ISGID)) {
	    iot_log_error("setgid flag is set for file '%s'", path);
	    success = false;
	}
    }

    return success;
}


static bool verify_scripts(iotpm_pkginfo_t *info)
{
    static script_t scripts[] = {
      { IOTPM_PROCESSING_PREIN,   "pre-install"    },
      { IOTPM_PROCESSING_POSTIN,  "post-install"   },
      { IOTPM_PROCESSING_PREUN,   "pre-uninstall"  },
      { IOTPM_PROCESSING_POSTUN,  "post-uninstall" },
      { IOTPM_PROCESSING_NONE,        NULL         }
    };

    iotpm_pkginfo_processing_t proc = info->proc;
    char *p, *e, *sep, buf[1024];
    script_t *s;
    bool success = true;

    e = (p = buf) + sizeof(buf);

    for (s = scripts, sep = "";   s->mask;   s++) {
        if ((proc & s->mask)) {
	    success = false;

	    if (p < e) {
	        p += snprintf(p, e-p, "%s%s", sep, s->name);
	        sep = ", ";
	    }
	}
    }

    if (!success) {
        iot_log_error("scripts are not allowed. This package has "
		      "the following scripts: %s", buf);
    }

    return success;
}
