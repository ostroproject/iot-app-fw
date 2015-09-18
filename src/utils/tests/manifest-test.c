#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#define _GNU_SOURCE
#include <getopt.h>

#include <iot/common/macros.h>
#include <iot/common/log.h>
#include <iot/utils/manifest.h>
#include <iot/utils/identity.h>
#include <iot/utils/appid.h>


typedef struct {
    const char *common;
    const char *user;
    const char *appid;
    char        pkg[64];
    char        app[64];
    int         log_mask;
    int         fidx;
} config_t;


#define test_info iot_log_info
#define test_error iot_log_error
#define test_fail(_cfg, ...) do {                       \
        iot_log_error("fatal error: "__VA_ARGS__);      \
        exit(1);                                        \
    } while (0)



static void dump_manifest(iot_manifest_t *m)
{
    const char *apps[64], *privs[64], *args[64];
    const char *desktop, *descr;
    int   napp, npriv, narg, i, j;

    printf("manifest path: %s\n", iot_manifest_path(m));

    napp = iot_manifest_applications(m, apps, 64);
    printf("  %d apps:\n", napp);

    for (i = 0; i < napp; i++) {
        descr = iot_manifest_description(m, apps[i]);
        printf("    #%d: %s (%s)\n", i, apps[i], descr ? descr : "???");

        npriv = iot_manifest_privileges(m, apps[i], privs, 64);
        printf("      %d privileges:\n", npriv);
        for (j = 0; j < npriv; j++)
            printf("        #%d: %s\n", j, privs[j]);

        narg = iot_manifest_arguments(m, apps[i], args, 64);
        printf("      %d arguments:\n", narg);
        for (j = 0; j < narg; j++)
            printf("        #%d: %s\n", j, args[j]);

        desktop = iot_manifest_desktop_path(m, apps[i]);
        printf("      desktop file: %s\n", desktop ? desktop : "-");
    }
}


static void print_usage(config_t *cfg, const char *argv0, int exit_code,
                        const char *fmt, ...)
{
    va_list ap;

    IOT_UNUSED(cfg);

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }

    printf("usage: %s [options] [file-path-list]\n\n"
           "The possible options are:\n"
           "  -c, --common=<dir>             common manifest directory\n"
           "  -u, --user=<dir>               per-user manifest directiry\n"
           "  -a, --appid=<pkg:app>          application id\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
           "  -h, --help                     show help on usage\n",
           argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void config_set_defaults(config_t *cfg)
{
    cfg->common = "./manifests/common";
    cfg->user   = "./manifests/user";
    cfg->appid  = NULL;
    cfg->pkg[0] = '\0';
    cfg->app[0] = '\0';
    cfg->fidx   = 1;

    cfg->log_mask = IOT_LOG_UPTO(IOT_LOG_INFO);
}


static void parse_cmdline(config_t *cfg, int argc, char **argv, char **envp)
{
#   define OPTIONS "c:u:a:vd:h"
    struct option options[] = {
        { "common"  , required_argument, NULL, 'c' },
        { "user"    , required_argument, NULL, 'u' },
        { "appid"   , required_argument, NULL, 'a' },
        { "verbose" , optional_argument, NULL, 'v' },
        { "debug"   , required_argument, NULL, 'd' },
        { "help"    , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt, help;

    IOT_UNUSED(envp);

    config_set_defaults(cfg);
    iot_log_set_mask(cfg->log_mask);
    iot_log_set_target(IOT_LOG_TO_STDERR);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            cfg->common = optarg;
            break;

        case 'u':
            cfg->user = optarg;
            break;

        case 'a':
            cfg->appid = optarg;
            break;

        case 'v':
            cfg->log_mask <<= 1;
            cfg->log_mask  |= 1;
            iot_log_set_mask(cfg->log_mask);
            break;

        case 'd':
            cfg->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

        case 'h':
            help++;
            break;

        default:
            print_usage(cfg, argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(cfg, argv[0], -1, "");
        exit(0);
    }

    cfg->fidx = optind;
}


int main(int argc, char *argv[], char *env[])
{
    config_t        cfg;
    iot_manifest_t *m;
    int             i;
    const char     *path, *app, *type;

    parse_cmdline(&cfg, argc, argv, env);

    if (iot_appid_package(cfg.appid, cfg.pkg, sizeof(cfg.pkg)) == NULL)
        test_fail(&cfg, "failed to extract app from '%s'", cfg.appid);

    if (iot_appid_package(cfg.appid, cfg.app, sizeof(cfg.app)) == NULL)
        test_fail(&cfg, "failed to extract app from '%s'", cfg.appid);

    iot_manifest_set_directories(cfg.common, cfg.user);
    iot_manifest_populate_cache();
    m = iot_manifest_get(getuid(), cfg.pkg);

    if (m == NULL)
        test_fail(&cfg, "failed to get/load manifest");

    dump_manifest(m);

    for (i = cfg.fidx; i < argc; i++) {
        path = argv[i];

        if (iot_manifest_filetype(m, path, &app, &type) < 0)
            test_error("failed to get filetype for '%s'", path);
        else
            test_info("'%s': app %s, type %s", path, app, type);
    }

    iot_manifest_unref(m);

    return 0;
}
