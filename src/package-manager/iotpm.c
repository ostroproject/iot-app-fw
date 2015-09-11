#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <libgen.h>

#include <iot/common.h>

#include "options.h"
#include "backend.h"

static bool iotpm_init(iotpm_t **, int, char **);
static void iotpm_exit(iotpm_t *);

static int install_package(iotpm_t *);
static int upgrade_package(iotpm_t *);
static int remove_package(iotpm_t *);
static int db_check(iotpm_t *);
static int db_plant(iotpm_t *);
static int list(iotpm_t *);


int main(int argc, char **argv)
{
    iotpm_t *iotpm;
    int rc;

    if (!iotpm_init(&iotpm, argc,argv)        ||
	!iotpm_options_init(iotpm, argc,argv) ||
	!iotpm_backend_init(iotpm)             )
    {
        return EINVAL;
    }

    switch (iotpm->mode) {
    case IOTPM_MODE_INSTALL:  rc = install_package(iotpm);       break;
    case IOTPM_MODE_UPGRADE:  rc = upgrade_package(iotpm);       break;
    case IOTPM_MODE_REMOVE:   rc = remove_package(iotpm);        break;
    case IOTPM_MODE_DBCHECK:  rc = db_check(iotpm);              break;
    case IOTPM_MODE_DBPLANT:  rc = db_plant(iotpm);              break;
    case IOTPM_MODE_LIST:     rc = list(iotpm);                  break;
    default:                  rc = EIO;                          break;
    }

    iotpm_backend_exit(iotpm);
    iotpm_options_exit(iotpm);
    iotpm_exit(iotpm);

    return rc;
}


static bool iotpm_init(iotpm_t **iotpm_ret, int argc, char **argv)
{
    iotpm_t *iotpm;
    struct passwd *pwd;
    char homedir[IOTPM_PATH_MAX], *h;
    size_t lastchar;
    const char *error;

    IOT_UNUSED(argc);

    if (!iotpm_ret)
        return false;

    if (!(pwd = getpwuid(getuid())) || !pwd->pw_name ||
        !pwd->pw_dir || !pwd->pw_dir[0])
    {
        error = "missing or broken user account information";
        goto failed;
    }


    if (!(iotpm = iot_allocz(sizeof(iotpm_t)))            ||
        !(iotpm->prognam = iot_strdup(basename(argv[0]))) ||
        !(iotpm->username = iot_strdup(pwd->pw_name))     ||
        !(iotpm->homedir = iot_strdup(pwd->pw_dir))        )
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
    
    *iotpm_ret = iotpm;
    return true;
    
 failed:
    iot_log_error("%s", error);
    *iotpm_ret = NULL;
    return false;
}

static void iotpm_exit(iotpm_t *iotpm)
{
    if (iotpm) {
        iot_free((void *)iotpm->prognam);
        iot_free((void *)iotpm->username);
        iot_free((void *)iotpm->homedir);

        iot_free((void *)iotpm);
    }
}

static int install_package(iotpm_t *iotpm)
{
    const char *pkg = iotpm->argv[0];
    iotpm_pkginfo_t *info = NULL;
    char name[1024];
    int rc = EIO;
    
    if (!pkg)
        goto out;

    info = iotpm_backend_pkginfo_create(iotpm, true, pkg);
    
    if (info->sts < 0)
        goto out;
    
    if (!iotpm_pkginfo_verify(info))
        goto out;
    
    strncpy(name, info->name, sizeof(name));
    name[sizeof(name)-1] = 0;
    
    iotpm_backend_pkginfo_destroy(info);
    info = NULL;
    
    if (!iotpm_backend_install_package(iotpm, pkg))
        goto out;
    
    info = iotpm_backend_pkginfo_create(iotpm, false, name);
    
    if (info->sts < 0 || !iotpm_backend_seed_create(info)) {
        iotpm_backend_remove_package(iotpm, info->name);
        goto out;
    }
    
    rc = 0;
 out:
    iotpm_backend_pkginfo_destroy(info);
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
    int rc = EIO;
    
    info = iotpm_backend_pkginfo_create(iotpm, false, pkg);
    
    if (info->sts < 0)
        goto out;
    
    if (!iotpm_backend_remove_package(iotpm, pkg))
        goto out;
    
    if (!iotpm_backend_seed_destroy(info))
        goto out;
    
    rc = 0;
    
 out:
    iotpm_backend_pkginfo_destroy(info);
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
    return 0;
}
