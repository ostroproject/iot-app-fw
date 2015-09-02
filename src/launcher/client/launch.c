/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/mainloop.h>
#include <iot/common/transport.h>
#include <iot/common/json.h>
#include <iot/common/utils.h>
#include <iot/utils/manifest.h>
#include <iot/utils/identity.h>
#include <iot/utils/appid.h>

#include "launcher/iot-launch.h"

#ifdef ENABLE_SECURITY_MANAGER
#  include <security-manager/security-manager.h>
#endif

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif


/*
 * launcher runtime context
 */

typedef struct {
    iot_mainloop_t    *ml;               /* mainloop */
    iot_transport_t   *t;                /* transport to daemon */
    const char        *addr;             /* address we listen on */
    int                log_mask;         /* what to log */
    const char        *log_target;       /* where to log it */
    const char        *argv0;            /* us, the launcher client */
    /* application options */
    const char        *app;              /* application to start/stop */
    int                argc;             /* number of extra arguments */
    char             **argv;             /* the extra arguments */
    int                agent : 1;        /* running as cgroup release agent */
    int                cleanup : 1;      /* cleaning up instead of launching */
    /* special options for development mode */
    const char        *label;            /* run with this SMACK label */
    const char        *user;             /* run as this user */
    const char        *group;            /* run with this group */
    const char        *privileges;       /* run with these privilege */
    const char        *manifest;         /* run with this manifest */
    int                shell : 1;        /* run a shell instead of the app */
    int                bringup : 1;      /* run in SMACK bringup mode */
    int                unconfined : 1;   /* run in SMACK unconfined mode */

    const char        *appid;            /* application id */

    uid_t              uid;              /* resolved user */
    gid_t              gid;              /* resolved group */
    iot_manifest_t    *m;                /* application manifest */

    iot_sighandler_t  *sh_int;           /* SIGINT handler */
    iot_sighandler_t  *sh_term;          /* SIGHUP handler */
} launcher_t;


/*
 * command line processing
 */

/* XXX temporary hack */
static bool iot_development_mode(void)
{
    return true;
}


static bool from_source_tree(const char *argv0, char *base, size_t size)
{
    char *e;
    int   n;

    if ((e = strstr(argv0, "/src/iot-launch")) == NULL &&
        (e = strstr(argv0, "/src/.libs/lt-iot-launch")) == NULL)
        return false;

    if (base != NULL) {
        n = e - argv0;

        if (n >= (int)size)
            n = size - 1;

        snprintf(base, size, "%*.*s", n, n, argv0);
    }

    return true;
}


static bool is_cgroup_agent(const char *argv0)
{
    return strstr(argv0, "iot-launch-agent") != NULL;
}


static const char *launcher_base(const char *argv0)
{
    const char *base;

    base = strrchr(argv0, '/');

    if (base == NULL)
        return argv0;

    base++;
    if (!strncmp(base, "lt-", 3))
        return base + 3;
    else
        return base;
}


static void print_usage(launcher_t *l, int exit_code, const char *fmt, ...)
{
    va_list     ap;
    const char *base;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }

    base = launcher_base(l->argv0);

    printf("usage:\n");
    printf("To start an application:\n");
    printf("  %s [options] --app <pkg>[:<app>] [-- extra-args]\n", base);
    printf("To clean up after an application has exited:\n");
    printf("  %s [options] [--cleanup] <cgroup-path>\n\n", base);
    printf("The possible options are:\n"
           "  -s, --server=<SERVER>        server transport address\n"
           "  -a, --app=<APPID>            application to start\n"
           "  -l, --log-level=<LEVELS>     what messages to log\n"
           "    LEVELS is a comma-separated list of info, error and warning\n"
           "  -t, --log-target=<TARGET>    where to log messages\n"
           "    TARGET is one of stderr, stdout, syslog, or a logfile path\n"
           "  -v, --verbose                increase logging verbosity\n"
           "  -d, --debug=<SITE>           turn on debugging for the give site\n"
           "    SITE can be of the form 'function', '@file-name', or '*'\n"
           "  -h, --help                   show this help message\n");

    if (!iot_development_mode())
        goto out;

    printf("Development-mode options:\n");
    printf("  -S, --shell                  start a shell, not the application\n"
           "  -u, --unconfined             set the SMACK label to unconfined\n"
           "  -B, --bringup                run in SMACK bringup mode\n"
           "  -L, --label=<LABEL>          run with the given SMACK label\n"
           "  -U, --user=<USER>            run with the given user ID\n"
           "  -G, --group=<GROUP>          run with the given group ID\n"
           "  -P, --privilege=<PRIVILEGES> run with the given privileges\n"
           "  -M, --manifest=<PATH>        run with the given manifest\n");

 out:
    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void config_set_defaults(launcher_t *l, char *argv0)
{
    char common[PATH_MAX], user[PATH_MAX], base[PATH_MAX];

    l->argv0 = argv0;
    l->addr  = IOT_LAUNCH_ADDRESS;
    l->agent = is_cgroup_agent(argv0) ? 1 : 0;

    l->log_mask   = IOT_LOG_UPTO(IOT_LOG_WARNING);
    l->log_target = "stderr";

    iot_log_set_mask(l->log_mask);
    iot_log_set_target(l->log_target);

    if (from_source_tree(argv0, base, sizeof(base))) {
        iot_log_warning("*** Setting up defaults for a source tree run.");
        l->log_mask = IOT_LOG_UPTO(IOT_LOG_INFO);
        iot_log_set_mask(l->log_mask);

        snprintf(common, sizeof(common), "%s/manifests/common", base);
        snprintf(user, sizeof(user), "%s/manifests/user", base);
        iot_log_warning("common manifest directory set to '%s'", common);
        iot_log_warning("user manifest directory set to '%s'", user);

        iot_manifest_set_directories(common, user);
    }
}


static void parse_cmdline(launcher_t *l, int argc, char **argv, char **envp)
{
#   define STDOPTS "s:a:cl:t:v::d:h"
#   define DEVOPTS "SUBL:U:G:P:M:"
#   define STDOPTIONS                                                   \
        { "server"           , required_argument, NULL, 's' },          \
        { "app"              , required_argument, NULL, 'a' },          \
        { "cleanup"          , no_argument      , NULL, 'c' },          \
        { "log-level"        , required_argument, NULL, 'l' },          \
        { "log-target"       , required_argument, NULL, 't' },          \
        { "verbose"          , optional_argument, NULL, 'v' },          \
        { "debug"            , required_argument, NULL, 'd' },          \
        { "help"             , no_argument      , NULL, 'h' }
#   define DEVOPTIONS                                                   \
        { "shell"            , no_argument      , NULL, 'S' },          \
        { "unconfined"       , no_argument      , NULL, 'u' },          \
        { "bringup"          , no_argument      , NULL, 'B' },          \
        { "label"            , required_argument, NULL, 'L' },          \
        { "uid"              , required_argument, NULL, 'U' },          \
        { "gid"              , required_argument, NULL, 'G' },          \
        { "privilege"        , required_argument, NULL, 'P' },          \
        { "manifest"         , required_argument, NULL, 'M' }
#   define ENDOPTIONS { NULL , 0, NULL, 0 }

    struct option stdopts[] = { STDOPTIONS, ENDOPTIONS };
    struct option devopts[] = { STDOPTIONS, DEVOPTIONS, ENDOPTIONS };

    const char    *OPTIONS;
    struct option *options;

    int opt, help;

    IOT_UNUSED(envp);

    help = FALSE;
    config_set_defaults(l, argv[0]);

    if (!iot_development_mode()) {
        OPTIONS = STDOPTS;
        options = stdopts;
    }
    else {
        OPTIONS = STDOPTS""DEVOPTS;
        options = devopts;
    }

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 's':
            l->addr = optarg;
            break;

        case 'a':
            l->appid = optarg;
            break;

        case 'c':
            l->cleanup = 1;
            break;

            /*
             * logging, debugging and help
             */
        case 'l':
            l->log_mask = iot_log_parse_levels(optarg);
            break;

        case 'v':
            l->log_mask <<= 1;
            l->log_mask  |= 1;
            break;

        case 't':
            l->log_target = optarg;
            break;

        case 'd':
            l->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_log_set_mask(l->log_mask);
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

        case 'h':
            help++;
            break;

            /*
             * special debugging mode options
             */
        case 'S':
            l->shell = 1;
            break;

        case 'u':
            l->unconfined = 1;
            break;

        case 'B':
            l->bringup = 1;
            break;

        case 'L':
            l->label = optarg;
            break;

        case 'U':
            l->user = optarg;
            break;

        case 'G':
            l->group = optarg;
            break;

        case 'P':
            l->privileges = optarg;
            break;

        case 'M':
            l->manifest = optarg;
            break;

        default:
            print_usage(l, EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(l, -1, "");
        exit(0);
    }

    l->argc = argc - optind;
    l->argv = argv + optind;
}


static void set_linebuffered(FILE *stream)
{
    fflush(stream);
    setvbuf(stream, NULL, _IOLBF, 0);
}


static void set_nonbuffered(FILE *stream)
{
    fflush(stream);
    setvbuf(stream, NULL, _IONBF, 0);
}


static void setup_logging(launcher_t *l)
{
    const char *target;

    if (l->log_mask < 0)
        print_usage(l, EINVAL, "invalid log level '%s'", optarg);

    target = iot_log_parse_target(l->log_target);

    if (!target)
        print_usage(l, EINVAL, "invalid log target '%s'", l->log_target);

    iot_log_set_mask(l->log_mask);
    iot_log_set_target(target);

    set_linebuffered(stdout);
    set_nonbuffered(stderr);
}


static void signal_handler(iot_sighandler_t *h, int signum, void *user_data)
{
    iot_mainloop_t *ml = iot_get_sighandler_mainloop(h);
    launcher_t     *l  = (launcher_t *)user_data;

    IOT_UNUSED(l);

    switch (signum) {
    case SIGINT:
        iot_log_info("Received SIGINT, exiting...");
        if (ml != NULL)
            iot_mainloop_quit(ml, 0);
        else
            exit(0);
        break;

    case SIGTERM:
        iot_log_info("Received SIGTERM, exiting...");
        iot_mainloop_quit(ml, 0);
        break;
    }
}


static void run_mainloop(launcher_t *l)
{
    iot_mainloop_run(l->ml);
}


static void setup_signals(launcher_t *l)
{
    l->sh_int  = iot_add_sighandler(l->ml, SIGINT , signal_handler, l);
    l->sh_term = iot_add_sighandler(l->ml, SIGTERM, signal_handler, l);
}


static void clear_signals(launcher_t *l)
{
    iot_del_sighandler(l->sh_int);
    iot_del_sighandler(l->sh_term);

    l->sh_int  = NULL;
    l->sh_term = NULL;
}


static void security_setup(launcher_t *l)
{
#ifdef ENABLE_SECURITY_MANAGER
    int status;

    if (l->appid == NULL) {
        iot_log_error("missing application id for launch");
        exit(1);
    }

    iot_log_info("preparing security framework...");

    status = security_manager_prepare_app(l->appid);

    iot_log_info("security-manager returned status %d:", status);

    if (status != SECURITY_MANAGER_SUCCESS) {
        iot_log_error("security framework preparation failed");
        exit(1);
    }
#else
    IOT_UNUSED(l);
    iot_log_info("security framework disabled!");
#endif
}


static int launch_process(launcher_t *l)
{
    char *argv[l->argc + 1];

    memcpy(argv, l->argv, sizeof(l->argv[0]) * l->argc);
    argv[l->argc] = NULL;
    clear_signals(l);

    return execv(argv[0], argv);
}


static void close_connection(launcher_t *l)
{
    iot_transport_disconnect(l->t);
    iot_transport_destroy(l->t);
    l->t = NULL;
}


static void closed_cb(iot_transport_t *t, int error, void *user_data)
{
    launcher_t *l = (launcher_t *)user_data;

    IOT_UNUSED(t);

    if (error != 0)
        iot_log_error("Connection closed with error %d (%s).",
                      error, strerror(error));
    else
        iot_log_info("Connection closed.");

    close_connection(l);
}


static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    launcher_t  *l = (launcher_t *)user_data;
    const char  *type, *error, *sep;
    int          status;
    char         cmd[1024], *p;
    int          i, n, len;

    IOT_UNUSED(t);

    iot_debug("received message: %s", iot_json_object_to_string(msg));

    if (!iot_json_get_string(msg, "type", &type) || strcmp(type, "status") != 0)
        goto malformed;
    if (!iot_json_get_integer(msg, "status", &status))
        goto malformed;

    if (status != 0) {
        if (!iot_json_get_string(msg, "message", &error))
            error = NULL;
        iot_log_error("%s request failed with error %d (%s).",
                      l->cleanup ? "Cleanup" : "Launch",  status,
                      error ? error : "unknown error");
        exit(status);
    }

    if (!l->cleanup) {
        security_setup(l);

        len = sizeof(cmd) - 1;
        sep = "";
        p   = cmd;
        for (i = 0; i < l->argc; i++) {
            n = snprintf(p, len, "%s%s", sep, l->argv[i]);

            if (n < 0 || n >= len)
                break;

            p   += n;
            len -= n;
            sep  = " ";
        }

        iot_log_info("Launching %s (as '%s')...", l->argv[0], cmd);
        exit(launch_process(l));
    }
    else {
        iot_log_info("Cleaning up '%s' done.", l->argv[0]);
        exit(0);
    }

 malformed:
    iot_log_error("Received unknown/malformed reply from the server (%s).",
                  iot_json_object_to_string(msg));
    exit(1);
}


static void setup_transport(launcher_t *l)
{
    static iot_transport_evt_t evt = {
        { .recvjson     = recv_cb },
        { .recvjsonfrom = NULL    },
          .closed       = closed_cb,
    };

    iot_sockaddr_t  addr;
    socklen_t       len;
    const char     *type;
    int             flags;

    len = iot_transport_resolve(NULL, l->addr, &addr, sizeof(addr), &type);

    if (len <= 0) {
        iot_log_error("Failed to resolve transport address for '%s'.", l->addr);

        exit(1);
    }

    flags = IOT_TRANSPORT_MODE_JSON;
    l->t = iot_transport_create(l->ml, type, &evt, l, flags);

    if (l->t == NULL) {
        iot_log_error("Failed to create transport for address '%s'.", l->addr);

        exit(1);
    }

    if (!iot_transport_connect(l->t, &addr, len)) {
        iot_log_error("Failed to connect to transport '%s'.", l->addr);
        iot_transport_destroy(l->t);
        l->t = NULL;

        exit(1);
    }
}


static int send_request(launcher_t *l)
{
    iot_json_t *msg = iot_json_create(IOT_JSON_OBJECT);
    int         status;

    if (!l->cleanup) {
        iot_json_add_string      (msg, "type"   , "setup");
        iot_json_add_integer     (msg, "seqno"  , 1);
        iot_json_add_string      (msg, "appid"  , l->appid);
        iot_json_add_string_array(msg, "command", l->argv, l->argc);
    }
    else {
        iot_json_add_string (msg, "type" , "cleanup");
        iot_json_add_integer(msg, "seqno", 1);
        iot_json_add_string (msg, "path" , l->argv[0]);
    }

    status = iot_transport_sendjson(l->t, msg);
    iot_json_unref(msg);

    if (!status) {
        errno = EIO;
        return -1;
    }
    else
        return 0;
}


void resolve_binary(launcher_t *l)
{
    if (l->appid == NULL) {
        if (l->argc == 0)
            print_usage(l, EINVAL, "neither appid nor binary given to launch");

        l->appid = "unknown";
    }
    else {
        if (l->argc == 0) {
        nobinary:
            iot_log_error("Can't resolve appid (%s) to binary path. This "
                          "feature has not been", l->appid);
            iot_log_error("implemented yet. Please supply the absoute path to "
                          "the binary as the");
            iot_log_error("first argument for the time being...\n");
            exit(1);
        }

        if (*l->argv[0] != '/')
            goto nobinary;
    }
}


void resolve_cgroup(launcher_t *l)
{
    l->cleanup = TRUE;

    if (l->argc != 1) {
        iot_log_error("In cleanup/agent mode, a single cgroup path expected, "
                      "(got %d arguments).", l->argc);
        exit(1);
    }

    if (*l->argv[0] != '/') {
        iot_log_error("In cleanup/agent mode, expecting a cgroup path (got %s).",
                      l->argv[0]);
        exit(1);
    }
}


static void resolve_identities(launcher_t *l)
{
    /*
     * resolve user and/or group if given (in development mode)
     */
    if (l->user != NULL) {
        if ((l->uid = iot_get_userid(l->user)) == (uid_t)-1)
            print_usage(l, EINVAL, "invalid user/user ID '%s'", l->user);
    }
    else
        l->uid = (uid_t)-1;

    if (l->group != NULL) {
        if ((l->gid = iot_get_groupid(l->group)) == (gid_t)-1)
            print_usage(l, EINVAL, "invalid group/group ID '%s'", l->group);
    }
    else
        l->gid = (gid_t)-1;
}


static void resolve_application(launcher_t *l)
{
    static char app[128];
    char  pkg[128];
    uid_t uid;

    if (iot_appid_parse(l->appid, NULL, 0,
                        pkg, sizeof(pkg), app, sizeof(app)) < 0)
        print_usage(l, EINVAL, "failed to parse appid '%s'", l->appid);

    l->app = app;

    uid  = l->uid != (uid_t)-1 ? l->uid : getuid();
    l->m = iot_manifest_get(uid, pkg);

    if (l->m == NULL)
        print_usage(l, EINVAL, "failed to find/load manifest for user %d", uid);
}


static void override_manifest(launcher_t *l)
{
    static char app[128];
    char pkg[128], *apps[1];
    int  n;

    l->m = iot_manifest_read(l->manifest);

    if (l->m == NULL)
        print_usage(l, errno ? errno : EINVAL,
                    "failed to read/load manifest '%s'", l->manifest);

    *pkg = *app = '\0';

    if (l->appid != NULL) {
        iot_appid_parse(l->appid, NULL, 0, pkg, sizeof(pkg), app, sizeof(app));

        if (!*app)
            print_usage(l, EINVAL, "failed to parse appid '%s'", l->appid);

        l->app = app;
    }
    else {
        n = iot_manifest_applications(l->m, apps, sizeof(apps));

        if (n < 1)
            print_usage(l, EINVAL, "failed to pick default application");

        l->app = apps[0];
    }
}


static void resolve_startup_options(launcher_t *l)
{
    char *argv[128];
    int   argc, i;

    resolve_identities(l);

    if (l->manifest == NULL)
        resolve_application(l);
    else
        override_manifest(l);

    argc = iot_manifest_arguments(l->m, l->app, argv, sizeof(argv));

    if (argc < 0 || argc > (int)sizeof(argv))
        print_usage(l, EINVAL, "invalid manifest, too many exec arguments");

    if (argc + l->argc > (int)sizeof(argv))
        print_usage(l, EINVAL, "invalid usage, too many exec arguments");

    for (i = 0; i < l->argc; i++)
        argv[argc++ + i] = l->argv[i];

    for (i = 0; i < argc; i++)
        printf("argv[%d] = '%s'\n", i, argv[i]);
}



static void send_cleanup_request(launcher_t *l)
{
    exit(0);
}


static void send_startup_request(launcher_t *l)
{
    /*setup_transport(l);*/
    resolve_startup_options(l);

    exit(0);
}


int main(int argc, char *argv[], char **envp)
{
    launcher_t l;

    iot_clear(&l);
    l.ml = iot_mainloop_create();

    parse_cmdline(&l, argc, argv, envp);

    setup_signals(&l);
    setup_logging(&l);

    if (l.agent || l.cleanup) {
        resolve_cgroup(&l);
        send_cleanup_request(&l);
    }
    else
        send_startup_request(&l);

    setup_transport(&l);
    send_request(&l);

    run_mainloop(&l);

    return 0;
}

