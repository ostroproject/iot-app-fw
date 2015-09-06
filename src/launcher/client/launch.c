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
#include "launcher/daemon/msg.h"

#ifdef ENABLE_SECURITY_MANAGER
#  include <security-manager/security-manager.h>
#endif

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif


/*
 * launcher modes and runtime context
 */

typedef enum {
    LAUNCHER_SETUP = 0,                  /* start application */
    LAUNCHER_STOP,                       /* stop application */
    LAUNCHER_CLEANUP,                    /* cleanup after application */
    LAUNCHER_LIST_INSTALLED,             /* list installed applications */
    LAUNCHER_LIST_RUNNING,               /* list running applications */
} launcher_mode_t;

typedef struct {
    iot_mainloop_t    *ml;               /* mainloop */
    iot_transport_t   *t;                /* transport to daemon */
    int                seqno;            /* next message sequence number */
    const char        *addr;             /* address we listen on */
    const char        *argv0;            /* us, the launcher client */
    launcher_mode_t    mode;             /* start/stop/cleanup */

    /* application options */
    const char        *appid;            /* application id */
    int                argc;             /* number of extra arguments */
    char             **argv;             /* the extra arguments */

    char              *cgroup;           /* cgroup path when in agent mode */

    uid_t              uid;              /* resolved user */
    gid_t              gid;              /* resolved group */
    iot_manifest_t    *m;                /* application manifest */
    const char        *app;              /* application to start/stop */
    char             **app_argv;         /* application exec arguments */
    int                app_argc;         /* number of application arguments */

    /* special options for development mode */
    const char        *label;            /* run with this SMACK label */
    const char        *user;             /* run as this user */
    const char        *group;            /* run with this group */
    const char        *privileges;       /* run with these privilege */
    const char        *manifest;         /* run with this manifest */
    int                shell : 1;        /* run a shell instead of the app */
    int                bringup : 1;      /* run in SMACK bringup mode */
    int                unconfined : 1;   /* run in SMACK unconfined mode */

    int                log_mask;         /* what to log */
    const char        *log_target;       /* where to log it */

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
    printf("To stop an application:\n");
    printf("  %s [options] --stop --app <pkg>[:<app>]\n", base);
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


static void config_set_defaults(launcher_t *l, const char *argv0)
{
    char common[PATH_MAX], user[PATH_MAX], base[PATH_MAX];

    l->argv0      = argv0;
    l->addr       = IOT_LAUNCH_ADDRESS;
    l->mode       = is_cgroup_agent(argv0) ? LAUNCHER_CLEANUP : LAUNCHER_SETUP;
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
#   define STDOPTS "s:a:k:cl:t:v::d:h"
#   define DEVOPTS "SUBL:U:G:P:M:"
#   define STDOPTIONS                                                   \
        { "server"           , required_argument, NULL, 's' },          \
        { "app"              , required_argument, NULL, 'a' },          \
        { "stop"             , no_argument      , NULL, 'k' },          \
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

        case 'k':
            l->mode = LAUNCHER_STOP;
            break;

        case 'c':
            l->mode = LAUNCHER_CLEANUP;
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
             * special development mode options
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
    char *argv[l->app_argc + 1];

    memcpy(argv, l->app_argv, sizeof(l->app_argv[0]) * l->app_argc);
    argv[l->app_argc] = NULL;
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


static void recv_cb(iot_transport_t *t, iot_json_t *rpl, void *user_data)
{
    launcher_t  *l = (launcher_t *)user_data;
    int          status;
    const char  *msg;
    iot_json_t  *data;

    const char  *sep;
    char         cmd[1024], *p;
    int          i, n, len;

    IOT_UNUSED(t);

    iot_debug("received message: %s", iot_json_object_to_string(rpl));

    status = status_reply(rpl, &msg, &data);

    if (status != 0) {
        iot_log_error("%s request failed with error %d (%s).",
                      l->mode == LAUNCHER_CLEANUP ? "Cleanup":"Launch",  status,
                      msg ? msg : "unknown error");
        exit(status);
    }

    if (l->mode != LAUNCHER_CLEANUP) {
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


static int send_request(launcher_t *l, iot_json_t *req)
{
    int status;

    if (!iot_transport_sendjson(l->t, req)) {
        errno = EIO;
        status = -1;
    }
    else
        status = 0;

    iot_json_unref(req);

    return status;
}


static void resolve_identities(launcher_t *l)
{
    /*
     * Notes:
     *   Resolve potentially textual user and group identities to unique
     *   numeric ones.
     *
     *   In development mode one can override the identity the application
     *   is started under. We check here if the user or group identities
     *   were overridden and if so resolve the given ones to unique numeric
     *   identities.
     *
     *   If we're not in development mode (or no overrides were given), we
     *   use the default identities inherited from the user.
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


static void resolve_manifest(launcher_t *l)
{
    static char app[128];
    char  pkg[128];
    uid_t uid;

    /*
     * Notes:
     *   Resolve, validate and load manifest.
     *
     *   On the normal startup path (no manifest override, parse the given
     *   application identifier (<pkg>, or <pkg>:<app>), then use <pkg> to
     *   resolve the manifest from the normal per-user or common manifest
     *   directories.
     */

    if (iot_appid_parse(l->appid, NULL, 0,
                        pkg, sizeof(pkg), app, sizeof(app)) < 0)
        print_usage(l, EINVAL, "failed to parse appid '%s'", l->appid);

    l->app = app;
    uid    = l->uid != (uid_t)-1 ? l->uid : getuid();
    l->m   = iot_manifest_get(uid, pkg);

    if (l->m == NULL)
        print_usage(l, EINVAL, "failed to find/load manifest for user %d", uid);
}


static void override_manifest(launcher_t *l)
{
    static char app[128];
    char pkg[128], *apps[1];
    int  n;

    /*
     * Notes:
     *   Validate and load an overridden manifest.
     *
     *   In development mode one if a manifest path was given validate and
     *   load the manifest here. Also if the application was omitted, pick
     *   the first one from the loaded manifest (which is not guaranteed to
     *   be the first one in the manifest file).
     */

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


void resolve_cgroup_path(launcher_t *l)
{
    if (l->argc != 1 || *l->argv[0] != '/')
        print_usage(l, EINVAL, "agent expects a singe absolute cgroup path");

    l->cgroup = l->argv[0];
}


static void launcher_init(launcher_t *l, const char *argv0, char **envp)
{
    IOT_UNUSED(envp);

    iot_clear(l);

    l->seqno = 1;
    l->ml = iot_mainloop_create();

    if (l->ml == NULL) {
        iot_log_error("Failed to initialize launcher (%d: %s).",
                      errno, strerror(errno));
        exit(1);
    }

    setup_signals(l);
    config_set_defaults(l, argv0);
}


static iot_json_t *debug_options(launcher_t *l)
{
    iot_json_t *dbg;

    if (l->label || l->user || l->group || l->privileges || l->manifest ||
        l->shell || l->bringup || l->unconfined) {
        dbg = iot_json_create(IOT_JSON_OBJECT);

        if (dbg == NULL) {
            iot_log_error("Failed to create debug submessage.");
            exit(1);
        }

        if (l->label)
            iot_json_add_string(dbg, "label", l->label);

        if (l->user)
            iot_json_add_integer(dbg, "user", l->uid);
        if (l->group)
            iot_json_add_integer(dbg, "group", l->gid);

        if (l->privileges)
            iot_json_add_string(dbg, "privileges", l->privileges);

        if (l->manifest)
            iot_json_add_string(dbg, "manifest", l->manifest);

        if (l->shell)
            iot_json_add_boolean(dbg, "shell", true);
        if (l->bringup)
            iot_json_add_boolean(dbg, "bringup", true);
        if (l->unconfined)
            iot_json_add_boolean(dbg, "unconfined", true);

        return dbg;
    }
    else
        return NULL;
}


static iot_json_t *create_request(launcher_t *l, const char *type, iot_json_t *r)
{
    iot_json_t *msg = iot_json_create(IOT_JSON_OBJECT);

    if (msg == NULL) {
        iot_json_unref(r);
        return NULL;
    }

    iot_json_add_string (msg, "type" , type);
    iot_json_add_integer(msg, "seqno", l->seqno++);
    iot_json_add_object (msg, type   , r);

    return msg;
}


static iot_json_t *create_setup_request(launcher_t *l)
{
    static char *argv[128];
    int          argc, i;
    size_t       size;
    iot_json_t  *req, *dbg;

    size = IOT_ARRAY_SIZE(argv);
    argc = iot_manifest_arguments(l->m, l->app, argv, size);

    if (argc < 0) {
        iot_log_error("Failed to determine application launch arguments.");
        exit(1);
    }

    l->app_argv = argv;
    l->app_argc = argc + l->argc;

    if (l->app_argc > (int)size) {
        iot_log_error("Too many launch arguments.");
        exit(1);
    }

    for (i = 0; i < l->argc; i++)
        argv[argc + i] = l->argv[i];

    req = iot_json_create(IOT_JSON_OBJECT);

    if (req == NULL) {
        iot_log_error("Failed to create setup request.");
        exit(1);
    }

    iot_json_add_integer(req, "user" , l->uid);
    iot_json_add_integer(req, "group", l->gid);
    iot_json_add_string (req, "manifest", iot_manifest_path(l->m));
    iot_json_add_string (req, "app"     , l->app);

    iot_json_add_string_array(req, "exec", l->app_argv, l->app_argc);

    if ((dbg = debug_options(l)) != NULL)
        iot_json_add(req, "debug", dbg);

    return create_request(l, "setup", req);
}


static iot_json_t *create_stop_request(launcher_t *l)
{
    IOT_UNUSED(l);

    iot_log_error("stop request not implemented yet\n");
    exit(1);
}


static iot_json_t *create_cleanup_request(launcher_t *l)
{
    iot_json_t *req = iot_json_create(IOT_JSON_OBJECT);

    if (req == NULL) {
        iot_log_error("Failed to create cleanup request.");
        exit(1);
    }

    iot_json_add_string(req, "cgroup", l->cgroup);

    return create_request(l, "cleanup", req);
}


static iot_json_t *create_list_request(launcher_t *l)
{
    iot_json_t *req = iot_json_create(IOT_JSON_OBJECT);
    const char *type;

    if (req == NULL) {
        iot_log_error("Failed to create list request.");
        exit(1);
    }

    type = (l->mode == LAUNCHER_LIST_INSTALLED ? "installed" : "running");

    iot_json_add_string(req, "type", type);

    return create_request(l, "list", req);
}


int main(int argc, char *argv[], char **envp)
{
    launcher_t  l;
    iot_json_t *req;

    launcher_init(&l, argv[0], envp);
    parse_cmdline(&l, argc, argv, envp);
    setup_logging(&l);

    if (l.mode != LAUNCHER_CLEANUP) {
        resolve_identities(&l);

        if (l.manifest == NULL)
            resolve_manifest(&l);
        else
            override_manifest(&l);
    }

    switch (l.mode) {
    case LAUNCHER_SETUP:
        req = create_setup_request(&l);
        break;

    case LAUNCHER_STOP:
        req = create_stop_request(&l);
        break;

    case LAUNCHER_CLEANUP:
        req = create_cleanup_request(&l);
        break;

    case LAUNCHER_LIST_INSTALLED:
    case LAUNCHER_LIST_RUNNING:
        req = create_list_request(&l);
        break;

    default:
        print_usage(&l, EINVAL, "Hmm... don't know what to do.");
        exit(1);
    }

    setup_transport(&l);
    send_request(&l, req);

    run_mainloop(&l);

    return 0;
}

