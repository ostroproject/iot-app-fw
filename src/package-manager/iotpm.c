#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <libgen.h>

#include <iot/common.h>

#include "options.h"
#include "backend.h"
#include "security-manager.h"
#include "manifest.h"

static bool iotpm_init(iotpm_t **, int, char **);
static void iotpm_exit(iotpm_t *);

static int post_install_package(iotpm_t *);
static int pre_install_package(iotpm_t *);
static int upgrade_package(iotpm_t *);
static int remove_package(iotpm_t *);
static int db_check(iotpm_t *);
static int db_plant(iotpm_t *);
static int list(iotpm_t *);
static int files(iotpm_t *);


int main(int argc, char **argv)
{
    iotpm_t *iotpm;
    int rc;

    iot_switch_userid(IOT_USERID_REAL);

    if (!iotpm_init(&iotpm, argc,argv)        ||
	!iotpm_options_init(iotpm, argc,argv) ||
	!iotpm_backend_init(iotpm)            ||
        !iotpm_manifest_init(iotpm)            )
    {
        return EINVAL;
    }

    switch (iotpm->mode) {
    case IOTPM_MODE_POSTINST:  rc = post_install_package(iotpm);  break;
    case IOTPM_MODE_PREINST:   rc = pre_install_package(iotpm);   break;
    case IOTPM_MODE_UPGRADE:   rc = upgrade_package(iotpm);       break;
    case IOTPM_MODE_REMOVE:    rc = remove_package(iotpm);        break;
    case IOTPM_MODE_DBCHECK:   rc = db_check(iotpm);              break;
    case IOTPM_MODE_DBPLANT:   rc = db_plant(iotpm);              break;
    case IOTPM_MODE_LIST:      rc = list(iotpm);                  break;
    case IOTPM_MODE_FILES:     rc = files(iotpm);                 break;
    default:                   rc = EINVAL;                       break;
    }

    iotpm_manifest_exit(iotpm);
    iotpm_backend_exit(iotpm);
    iotpm_options_exit(iotpm);
    iotpm_exit(iotpm);

    return rc;
}


static bool iotpm_init(iotpm_t **iotpm_ret, int argc, char **argv)
{
    uid_t userid = getuid();
    iotpm_t *iotpm;
    struct passwd *pwd;
    char homedir[IOTPM_PATH_MAX], *h;
    size_t lastchar;
    const char *error;

    IOT_UNUSED(argc);

    if (!iotpm_ret)
        return false;

    if (!(pwd = getpwuid(userid)) || !pwd->pw_name ||
        !pwd->pw_dir || !pwd->pw_dir[0])
    {
        error = "missing or broken user account information";
        goto failed;
    }


    if (!(iotpm = iot_allocz(sizeof(iotpm_t)))                    ||
        !(iotpm->prognam = iot_strdup(basename(argv[0])))         ||
        !(iotpm->username = iot_strdup(pwd->pw_name))             ||
        !(iotpm->homedir = iot_strdup(pwd->pw_dir))               ||
        !(iotpm->default_label = iot_strdup(IOTPM_DEFAULT_LABEL))  )
    {
        error = "can't allocate memory for iotpm";
        goto failed;
    }

    /* strip '/' at the end of homedir, if any */
    if (iotpm->homedir[(lastchar = strlen(iotpm->homedir) - 1)] == '/')
        ((char *)iotpm->homedir)[lastchar] = 0;

    /* see if the HOME environment were the same */
    error = "HOME environment variable and account mismatch";
    if (!(h = getenv("HOME")) || !h[0])
        goto failed;
    strncpy(homedir, h, sizeof(homedir));
    homedir[sizeof(homedir)-1] = 0;
    if (homedir[(lastchar = strlen(h) - 1)] == '/')
        homedir[lastchar] = 0;
    if (strcmp(homedir, iotpm->homedir))
        goto failed;

    iotpm->userid = userid;
    iotpm->groupid = pwd->pw_gid;

    *iotpm_ret = iotpm;
    return true;

 failed:
    iot_log_error("%s", error);
    *iotpm_ret = NULL;
    iotpm_exit(iotpm);
    return false;
}

static void iotpm_exit(iotpm_t *iotpm)
{
    if (iotpm) {
        iot_free((void *)iotpm->prognam);
        iot_free((void *)iotpm->username);
        iot_free((void *)iotpm->homedir);
        iot_free((void *)iotpm->default_label);

        iot_free((void *)iotpm);
    }
}

static int post_install_package(iotpm_t *iotpm)
{
    const char *pkg = iotpm->argv[0];
    iotpm_pkginfo_t *info = NULL;
    iot_manifest_t *man = NULL;
    iotpm_pkginfo_filentry_t *manfile;
    char name[1024];
    bool seed_created = false;
    int rc = EIO;

    if (!pkg)
        goto out;

    info = iotpm_pkginfo_create(iotpm, true, pkg);

    if (info->sts < 0)
        goto out;

    if (!iotpm_pkginfo_verify(info))
        goto out;

    strncpy(name, info->name, sizeof(name));
    name[sizeof(name)-1] = 0;

    iotpm_pkginfo_destroy(info);
    info = NULL;

    if (!iotpm_backend_install_package(iotpm, pkg))
        goto out;

    info = iotpm_pkginfo_create(iotpm, false, name);

    if (info->sts < 0 || !(manfile = info->manifest)                   ||
        !(man = iotpm_manifest_load(iotpm, info->name, manfile->path)) ||
        !(seed_created = iotpm_backend_seed_create(info))              ||
        iotpm_register_package(info, man) < 0                           )
    {
        if (seed_created)
            iotpm_backend_seed_destroy(info);
        goto cleanup;
    }

    rc = 0;
    goto out;

 cleanup:
    iotpm_backend_remove_package(iotpm, info->name);
 out:
    iotpm_pkginfo_destroy(info);
    iotpm_manifest_free(man);
    return rc;
}

static int pre_install_package(iotpm_t *iotpm)
{
    const char *pkg = iotpm->argv[0];
    iotpm_pkginfo_t *info = NULL;
    iot_manifest_t *man = NULL;
    iotpm_pkginfo_filentry_t *manfile;
    int rc = EIO;

    if (!pkg)
        goto out;

    info = iotpm_pkginfo_create(iotpm, false, pkg);

    if (info->sts < 0 || !(manfile = info->manifest)                   ||
        !(man = iotpm_manifest_load(iotpm, info->name, manfile->path)) ||
        iotpm_register_package(info, man) < 0                           )
    {
        goto out;
    }

    rc = 0;

 out:
    iotpm_pkginfo_destroy(info);
    iotpm_manifest_free(man);
    return rc;
}


static int upgrade_package(iotpm_t *iotpm)
{
    return 0;
}


static int remove_package(iotpm_t *iotpm)
{
    iotpm_pkginfo_t *info = NULL;
    const char *pkg = iotpm->argv[0];
    iot_manifest_t *man = NULL;
    iotpm_pkginfo_filentry_t *manfile;
    int rc = EIO;

    info = iotpm_pkginfo_create(iotpm, false, pkg);

    if (info->sts == 0 && (manfile = info->manifest)                 &&
        (man = iotpm_manifest_load(iotpm, info->name, manfile->path)))
        iotpm_unregister_package(info, man);

    if (!iotpm_backend_remove_package(iotpm, pkg))
        goto out;

    if (!iotpm_backend_seed_destroy(info))
        goto out;

    rc = 0;

 out:
    iotpm_pkginfo_destroy(info);
    return rc;
}


static int db_check(iotpm_t *iotpm)
{
    int rc;

    iot_log_info("verifying DB");

    if (!iotpm_backend_verify_db(iotpm)) {
        iot_log_error("package DB has issues ...");
        rc = EIO;
    }
    else {
        iot_log_info("package DB is OK");
        rc = 0;
    }

    return rc;
}

static int db_plant(iotpm_t *iotpm)
{
    int crc, prc = 0;

    prc = iotpm_backend_seed_plant(iotpm, "*") ? 0 : EIO;
    crc = db_check(iotpm);

    return prc ? prc : crc;
}

static int list(iotpm_t *iotpm)
{
#define NAME   "Package"
#define VERS   "Version"
#define TIME   "Installation time"
#define TFMT   "%d-%b-%y %T"

    iotpm_pkglist_t *list;
    iotpm_pkglist_entry_t *e;
    iot_regexp_t *re = NULL;
    char *pattern;
    char buf[256];
    char sep[1024];
    time_t epoch;
    struct tm tm;
    int sw, nw, vw, tw;
    int i;
    int rc = 0;

    if (iotpm->argc == 1 && (pattern = iotpm->argv[0])) {
        if (iot_regexp_glob(pattern, buf, sizeof(buf)) < 0) {
            iot_log_error("invalid package pattern '%s'", pattern);
            return EINVAL;
        }

        if (!(re = iot_regexp_compile(buf, 0))) {
            iot_log_error("failed to compile regular expression '%s'", buf);
            return EINVAL;
        }
    }

    if (!(list = iotpm_backend_pkglist_create(iotpm, re)) || list->sts < 0)
        rc = EIO;
    else {
        if (list->nentry > 0) {
            epoch = 0;
            localtime_r(&epoch, &tm);

            if ((nw = -list->max_width.name) > -(sizeof(NAME) - 1))
                nw = -(sizeof(NAME) - 1);

            if ((vw = -list->max_width.version) > -(sizeof(VERS) - 1))
                vw = -(sizeof(VERS) - 1);

            if ((tw = -strftime(buf,sizeof(buf),TFMT,&tm)) > -(sizeof(TIME)-1))
                tw = -(sizeof(TIME) - 1);

            if ((sw = 2 + -nw + 3 + -vw + 3 + -tw + 2) > sizeof(sep) - 1)
                sw = sizeof(sep) - 1;

            memset(sep, '-', sw);
            sep[sw] = '\0';
            sep[(i = 0)] = '+';
            sep[(i += 2 + -nw + 1)] = '+';
            sep[(i += 2 + -vw + 1)] = '+';
            sep[(i += 2 + -tw + 1)] = '+';


            printf("%s\n", sep);
            printf("| %*s | %*s | %*s |\n", nw,NAME, vw,VERS, tw,TIME);
            printf("%s\n", sep);

            for (e = list->entries;  e->name;   e++) {
                localtime_r(&e->install_time, &tm);
                strftime(buf, sizeof(buf), TFMT, &tm);

                printf("| %*s | %*s | %*s |\n",
                       nw,e->name, vw,e->version, tw,buf);
            }

            printf("%s\n", sep);
        }
    }

    iotpm_backend_pkglist_destroy(list);
    iot_regexp_free(re);

    return rc;

#undef TFMT
#undef TFMT
#undef NAME
#undef VERS
#undef TIME
}

static int files(iotpm_t *iotpm)
{
    iotpm_pkginfo_t *info;
    iotpm_pkginfo_filentry_t *f;
    char t;
    int w, len;
    char sep[1024];
    int rc = 0;

    info = iotpm_pkginfo_create(iotpm, false, iotpm->argv[0]);
    
    if (info->sts < 0) {
        iot_log_error("listing files of package '%s' failed: %s",
                      iotpm->argv[0], strerror(errno));
        rc = -1;
    }
    else {
        for (f = info->files, w = 0;  f->path;  f++) {
            if ((len = strlen(f->path)) > w)
                w = len;
        }

        if (w > sizeof(sep) - (8+3+1))
            w = sizeof(sep) - (8+3+1);

        if (w < 4)
            w = 4;
        
        memset(sep, '-', w + (8+3));
        sep[w + (8+3)] = 0;
        sep[0] = '+';
        sep[7] = '+';
        sep[w + (8+2)] = '+';
        
        w = -w;

        printf("%s\n", sep);
        printf("| Type | %*s |\n", w,"Path");
        printf("%s\n", sep);
        
        for (f = info->files;  f->path;  f++) {
            switch (f->type) {
            default:
            case IOTPM_FILENTRY_UNKNOWN:   t = '!';   break;
            case IOTPM_FILENTRY_USER:      t = 'U';   break;
            case IOTPM_FILENTRY_SYSCONF:   t = 'C';   break;
            case IOTPM_FILENTRY_MANIFEST:  t = 'M';   break;
            case IOTPM_FILENTRY_FOREIGN:   t = '-';   break;
            }

            printf("|  %c   | %*s |\n", t, w,f->path);
        }

        printf("%s\n", sep);
    }

    iotpm_pkginfo_destroy(info);

    return rc;
}
