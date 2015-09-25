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
    char hdir[IOTPM_PATH_MAX];
    char mdir[IOTPM_PATH_MAX];
    size_t len_min, len_max, len, plen, mlen, alen;
    bool home, local, conf;
    int i;

    if (!strcmp(iotpm->username, "root"))
        return true;

    if (!info->manifest) {
        iot_log_error("could not find manifest file in the package");
        success = false;
    }

    alen = strlen(IOTPM_APPDIR);

    len_min = strlen(iotpm->homedir) + (alen > 0 ? 1 : 0) + alen;
    len_max = snprintf(hdir, sizeof(hdir), IOTPM_APPLICATION_HOME,
                       iotpm->homedir, info->name);

    mlen = snprintf(mdir, sizeof(mdir), "%s", backend->path.manifest);
    if (mlen > 0 && mdir[mlen-1] == '/')
        mdir[--mlen] = 0;

    for (i = 0;  i < info->nfile;  i++) {
        f = info->files + i;
        path = f->path;
        mode = f->mode;
        plen = strlen(path);
        len  = (plen > len_max) ? len_max : plen;

        if (f && f == info->manifest) {
            if (!S_ISREG(mode)) {
                iot_log_error("manifest file '%s' is not regular", f->path);
                success = false;
            }

            if (strcmp(f->user, "root")) {
                iot_log_error("manifest file '%s' supposed to be owned "
                              "by 'root' not '%s'", f->path, f->user);
                success = false;
            }

            if ((mode & 0777) != 0644) {
                iot_log_error("manifest file '%s' mode supposed to be 644 "
                              "not %03o", path, (mode & 0777));
                success = false;
            }
        }
        else if (!strncmp(path, mdir, plen > mlen ? mlen : plen)) {
            if (!S_ISDIR(mode)) {
                iot_log_error("attempt to replace something on path '%s'",
                              mdir);
                success = false;
            }
        }
        else {
            if (!strncmp(path, hdir, len)) {
                if (len <= len_min) {
                    home = true;
                    local = false;
                }
                else {
                    home = false;
                    local = true;
                }
            }

            conf = !strcmp(path, "/etc") || !strncmp(path, "/etc/", 5);

            if (!home && !local && !conf) {
                iot_log_error("'%s' is neither on path '%s' nor on '/etc/'",
                              path, hdir);
                success = false;
            }

            if (home) {
                if (!S_ISDIR(mode)) {
                    iot_log_error("attempt to replace something on "
                                  "path '%s'", hdir);
                    success = false;
                }

                if (strlen(path) > strlen(iotpm->homedir) &&
                    strcmp(f->user, iotpm->username))
                {
                    iot_log_error("owner of '%s' supposed to be '%s' not '%s'",
                                  path, iotpm->username, f->user);
                    success = false;
                }
            }

            if (local) {
                if (strcmp(f->user, iotpm->username)) {
                    iot_log_error("owner of '%s' supposed to be '%s' not '%s'",
                                  path, iotpm->username, f->user);
                    success = false;
                }
            }

            if ((mode & S_IWOTH)) {
                iot_log_error("file '%s' can be written by anyone", path);
                success = false;
            }
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
