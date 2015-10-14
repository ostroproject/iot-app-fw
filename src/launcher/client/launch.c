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
#define __USE_GNU    /* setresuid */
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/capability.h>

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
#  include <sys/smack.h>
#  include <security-manager/security-manager.h>
#endif

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif


/*
 * logging and debugging macros
 */

#define launch_print printf
#define launch_info  iot_log_info
#define launch_error iot_log_error
#define launch_warn  iot_log_warning
#define launch_debug iot_debug

#define launch_fail(_l, _error, _usage, ...) do { \
        if (_usage)                               \
            print_usage(_l, _error, __VA_ARGS__); \
        else {                                    \
            iot_log_error(__VA_ARGS__);           \
            exit(_error);                         \
        }                                         \
    } while (0)


/*
 * launcher modes
 */

typedef enum {
    LAUNCHER_SETUP = 0,                  /* start application */
    LAUNCHER_STOP,                       /* stop application */
    LAUNCHER_CLEANUP,                    /* cleanup after application */
    LAUNCHER_LIST_INSTALLED,             /* list installed applications */
    LAUNCHER_LIST_RUNNING,               /* list running applications */
} launcher_mode_t;


/*
 * launcher runtime context
 */

typedef struct {
    iot_mainloop_t    *ml;               /* mainloop */
    iot_transport_t   *t;                /* transport to daemon */
    int                seqno;            /* next message sequence number */
    const char        *addr;             /* address we listen on */
    const char        *argv0;            /* us, the launcher client */
    launcher_mode_t    mode;             /* start/stop/cleanup */
    bool               foreground;       /* stay in foreground */

    /* application options */
    const char        *appid;            /* application id */
    int                argc;             /* number of extra arguments */
    char             **argv;             /* the extra arguments */
    char              *cgroup;           /* cgroup path when in agent mode */

    uid_t              uid;              /* resolved user */
    gid_t             *gids;             /* resolved groups */
    int                ngid;
    iot_manifest_t    *m;                /* application manifest */
    const char       **app_argv;         /* (extra) application arguments */
    int                app_argc;         /* number of application arguments */

    const char        *pkg;              /* package name */
    const char        *app;              /* application name (within manifest) */
    const char        *fqai;             /* fully qualified application id */

    /* development mode options */
    const char        *label;            /* run with this SMACK label */
    const char        *user;             /* run as this user */
    const char        *groups;           /* run with this group */
    const char        *privileges;       /* run with these privileges */
    const char        *manifest;         /* run with this manifest */
    int                shell : 1;        /* run a shell instead of the app */
    int                bringup : 1;      /* run in SMACK bringup mode */
    int                unconfined : 1;   /* run in SMACK unconfined mode */

    int                log_mask;         /* what to log */
    const char        *log_target;       /* where to log it */

    iot_sighandler_t  *sig_int;          /* SIGINT handler */
    iot_sighandler_t  *sig_term;         /* SIGHUP handler */
} launcher_t;


static bool iot_development_mode(void)
{
#ifdef DEVEL_MODE_ENABLED
    return (getuid() == 0);              /* XXX temporary hack */
#else
    return false;
#endif
}


/*
 * command line processing
 */

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
    printf("  %s [options] <pkg>[:<app>] [-- extra-args]\n", base);
    printf("To stop an application:\n");
    printf("  %s [options] --stop <pkg>[:<app>]\n", base);
    printf("To clean up after an application has exited:\n");
    printf("  %s [options] [--cleanup] <cgroup-path>\n\n", base);
    printf("The possible options are:\n"
           "  -s, --server=<SERVER>        server transport address\n"
           "  -F, --fork                   fork before execing\n"
           "  -l, --log-level=<LEVELS>     what messages to log\n"
           "    LEVELS is a comma-separated list of info, error and warning\n"
           "  -t, --log-target=<TARGET>    where to log messages\n"
           "    TARGET is one of stderr, stdout, syslog, or a logfile path\n"
           "  -v, --verbose                increase logging verbosity\n"
           "  -d, --debug=<SITE>           turn on debugging for the give site\n"
           "    SITE can be of the form 'function', '@file-name', or '*'\n"
           "  -h, --help                   show this help message\n");

#ifdef DEVEL_MODE_ENABLED
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
#endif /* DEVEL_MODE_ENABLED */
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
    l->foreground = true;
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


static void get_valid_options(launcher_t *l, const char **optstr,
                              struct option **options)
{
#   define STDOPTS "s:Fk:cQ::l:t:v::d:h"
#   define DEVOPTS "SUBL:U:G:P:M:"
#   define STDOPTIONS                                                   \
        { "server"           , required_argument, NULL, 's' },          \
        { "fork"             , no_argument      , NULL, 'F' },          \
        { "stop"             , no_argument      , NULL, 'k' },          \
        { "cleanup"          , no_argument      , NULL, 'c' },          \
        { "list"             , optional_argument, NULL, 'Q' },          \
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

    IOT_UNUSED(l);

#ifdef DEVEL_MODE_ENABLED
    static struct option  stdopts[] = { STDOPTIONS, ENDOPTIONS };
    static struct option  devopts[] = { STDOPTIONS, DEVOPTIONS, ENDOPTIONS };

    if (iot_development_mode()) {
        *optstr  = STDOPTS""DEVOPTS;
        *options = devopts;
    }
    else {
        *optstr  = STDOPTS;
        *options = stdopts;
    }
#else /* !DEVEL_MODE_ENABLED */
    static struct option stdopts[] = { STDOPTIONS, ENDOPTIONS };

    *optstr  = STDOPTS;
    *options = stdopts;
#endif /* !DEVEL_MODE_ENABLED */
}


static void parse_cmdline(launcher_t *l, int argc, char **argv, char **envp)
{
    const char    *OPTIONS;
    struct option *options;

    int opt, help;

    IOT_UNUSED(envp);
    IOT_UNUSED(iot_development_mode);

    help = FALSE;

    get_valid_options(l, &OPTIONS, &options);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 's':
            l->addr = optarg;
            break;

        case 'F':
            l->foreground = false;
            break;

        case 'k':
            l->mode = LAUNCHER_STOP;
            break;

        case 'c':
            l->mode = LAUNCHER_CLEANUP;
            break;

        case 'Q':
            if (optarg == NULL) {
            list_running:
                l->mode = LAUNCHER_LIST_RUNNING;
            }
            else {
                if (!strcmp(optarg, "running"))
                    goto list_running;
                if (!strcmp(optarg, "installed"))
                    l->mode = LAUNCHER_LIST_INSTALLED;
                else
                    print_usage(l, EINVAL, "invalid list mode '%s'", optarg);
            }
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

#ifdef DEVEL_MODE_ENABLED
            /*
             * development mode options
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
            l->groups = optarg;
            break;

        case 'P':
            l->privileges = optarg;
            break;

        case 'M':
            l->manifest = optarg;
            break;
#endif

        default:
            print_usage(l, EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(l, -1, "");
        exit(0);
    }

    if (l->mode == LAUNCHER_SETUP || l->mode == LAUNCHER_STOP) {
        if (optind >= argc)
            print_usage(l, EINVAL, "error: application id not specified");

        l->appid = argv[optind];
        l->argc  = argc - (optind + 1);
        l->argv  = argv + (optind + 1);
    }
    else if (l->mode == LAUNCHER_CLEANUP) {
        l->argc = argc - optind;
        l->argv = argv + optind;
    }
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


static void install_signal_handlers(launcher_t *l)
{
    l->sig_int  = iot_add_sighandler(l->ml, SIGINT , signal_handler, l);
    l->sig_term = iot_add_sighandler(l->ml, SIGTERM, signal_handler, l);

    if (l->sig_int == NULL || l->sig_term == NULL)
        launch_fail(l, EINVAL, false, "Failed to install signal handlers.");
}


static void remove_signal_handlers(launcher_t *l)
{
    iot_del_sighandler(l->sig_int);
    iot_del_sighandler(l->sig_term);

    l->sig_int  = NULL;
    l->sig_term = NULL;
}


static int set_groups(launcher_t *l)
{
    gid_t gids[64];
    int   ngid, saved_errno;

    saved_errno = errno;
    if (setgroups(l->ngid, l->gids) < 0) {
        if (errno != EPERM)
            return -1;

        ngid    = getgroups(IOT_ARRAY_SIZE(gids) - 1, gids + 1);
        gids[0] = getgid();
        ngid++;

        if (ngid != l->ngid)
            return -1;

        if (memcmp(gids, l->gids, ngid * sizeof(gids[0])) != 0)
            return -1;
    }
    errno = saved_errno;

    return 0;
}


static int drop_privileges(launcher_t *l)
{
    cap_t c;

    IOT_UNUSED(l);

    if ((c = cap_init()) == NULL)
        return -1;

    if (cap_clear(c) < 0)
        return -1;

    if (cap_set_proc(c) < 0)
        return -1;

    return 0;
}


#ifdef ENABLE_SECURITY_MANAGER

static int set_user(launcher_t *l)
{
    return setresuid(l->uid, l->uid, l->uid);
}


static int set_smack_label(launcher_t *l)
{
    return smack_set_label_for_self(l->fqai);
}


static void security_setup(launcher_t *l)
{
    iot_switch_userid(IOT_USERID_SUID);

#ifdef DEVEL_MODE_ENABLED
    if (l->label || l->user || l->groups || l->privileges) {
        if (!iot_development_mode())
            launch_fail(l, EINVAL, false, "Hmm... not in development mode.");

        if (set_smack_label(l) < 0)
            launch_fail(l, errno, false, "Failed to set SMACK label (%d: %s).",
                        errno, strerror(errno));

        if (set_groups(l) < 0)
            launch_fail(l, errno, false, "Failed to set groups (%d: %s).",
                        errno, strerror(errno));

        if (set_user(l) < 0)
            launch_fail(l, errno, false, "Failed to set user id (%d: %s).",
                        errno, strerror(errno));

        if (drop_privileges(l) < 0)
            launch_fail(l, errno, false, "Failed to drop privileges (%d: %s).",
                        errno, strerror(errno));

        return;
    }
#endif /* DEVEL_MODE_ENABLED */

    if (security_manager_set_process_label_from_appid(l->fqai) != 0)
        launch_fail(l, 1, false, "Failed to set SMACK label.");

    if (security_manager_set_process_groups_from_appid(l->fqai) != 0)
        launch_fail(l, 1, false, "Failed to set groups.");

    if (iot_switch_userid(IOT_USERID_DROP) < 0)
        launch_fail(l, errno, false, "Failed to switch user id (%d: %s).",
                    errno, strerror(errno));

    if (security_manager_drop_process_privileges() != 0)
        launch_fail(l, 1, false, "Failed to drop privileges.");
}

#else /* !ENABLE_SECURITY_MANAGER */

static void security_setup(launcher_t *l)
{
    iot_switch_userid(IOT_USERID_SUID);

    launch_warn("Support for Security-Manager is disabled.");

    if (set_groups(l) < 0)
        launch_fail(l, errno, false, "Failed to set groups (%d: %s).",
                    errno, strerror(errno));

    if (iot_switch_userid(IOT_USERID_DROP) < 0)
        launch_fail(l, errno, false, "Failed to switch to real uid (%d: %s).",
                    errno, strerror(errno));

    if (drop_privileges(l) < 0)
        launch_fail(l, errno, false, "Failed to drop privileges (%d: %s).",
                    errno, strerror(errno));

}

#endif /* !ENABLE_SECURITY_MANAGER */

static int launch_process(launcher_t *l)
{
    char *argv[l->app_argc + 1];
    const char *shell = "/bin/bash";

    if (l->shell) {
        if (access(shell, X_OK) != 0)
            shell = "/bin/sh";
        launch_warn("Launching debug/development shell (%s)...", shell);
        argv[0] = (char *)shell;
        argv[1] = NULL;
    }
    else {
        memcpy(argv, l->app_argv, sizeof(l->app_argv[0]) * l->app_argc);
        argv[l->app_argc] = NULL;
    }

    remove_signal_handlers(l);

    if (!l->foreground) {
        switch (fork()) {
        case 0:
            break;
        case -1:
            launch_fail(l, errno, false, "fork() failed (%d: %s).",
                        errno, strerror(errno));
            break;
        default:
            return 0;
        }
    }

    return execv(argv[0], argv);
}


static void stop_app_check(launcher_t *l, const char *message, iot_json_t *data)
{
    IOT_UNUSED(l);
    IOT_UNUSED(data);

    if (!strcmp(message, "OK")) {
        printf("Application stopped.\n");
        exit(0);
    }

    if (!strcmp(message, "SIGNALLED")) {
        printf("Application signalled.\n");
        return;
    }
}


static void list_apps(launcher_t *l, iot_json_t *data)
{
    iot_json_t *a, *argv;
    int         i;
    const char *app, *descr, *desktop;
    char        user[64], *argv0;
    uid_t       uid;

    IOT_UNUSED(l);

    for (i = 0; iot_json_array_get_object(data, i, &a); i++) {
        app = descr = desktop = NULL;
        uid = (uid_t)-1;

        iot_json_get_string (a, "app"        , &app);
        iot_json_get_string (a, "description", &descr);
        iot_json_get_string (a, "desktop"    , &desktop);
        iot_json_get_integer(a, "user"       , &uid);
        iot_json_get_array  (a, "argv"       , &argv);
        iot_json_array_get_string(argv, 0, &argv0);

        printf("Application '%s':\n", app ? app : "?");
        printf("    description: '%s'\n", descr);
        printf("    desktop: '%s'\n", desktop && *desktop ? desktop : "-");
        printf("    user id: %d (%s)\n", uid,
               iot_get_username(uid, user, sizeof(user)));
        printf("    argv[0]: '%s'\n", argv0);
    }
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
        launch_fail(l, error, false, "Connection to closed with error %d: %s.",
                    error, strerror(error));
    else
        launch_info("Connection closed.");

    close_connection(l);
}


static void recv_cb(iot_transport_t *t, iot_json_t *msg, void *user_data)
{
    launcher_t *l = (launcher_t *)user_data;
    int         seqno, status;
    const char *message, *type, *event;
    iot_json_t *data;

    IOT_UNUSED(t);

    launch_debug("received message: %s", iot_json_object_to_string(msg));

    type = msg_type(msg);

    if (type == NULL)
        return;

    if (!strcmp(type, "status")) {
        status = msg_reply_parse(msg, &seqno, &message, &data);

        if (status < 0)
            launch_fail(l, -1, false, "Request failed.");

        if (status != 0)
            launch_fail(l, status, false,
                        "Request failed (%d: %s).", status, message);

        switch (l->mode) {
        case LAUNCHER_SETUP:
            security_setup(l);
            exit(launch_process(l));
            break;

        case LAUNCHER_STOP:
            stop_app_check(l, message, data);
            break;

        case LAUNCHER_CLEANUP:
            exit(0);
            break;

        case LAUNCHER_LIST_INSTALLED:
        case LAUNCHER_LIST_RUNNING:
            list_apps(l, data);
            exit(0);
            break;

        default:
            break;
        }

        return;
    }

    if (!strcmp(type, "event")) {
        status = msg_event_parse(msg, &event, &data);

        if (status < 0)
            return;

        if (!strcmp(event, "stopped")) {
            if (l->mode == LAUNCHER_STOP) {
                printf("Application stopped.\n");
                exit(0);
            }
        }
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

    if (len <= 0)
        launch_fail(l, EINVAL, false,
                    "Failed to resolve trasnport address '%s'.", l->addr);

    flags = IOT_TRANSPORT_MODE_JSON;
    l->t  = iot_transport_create(l->ml, type, &evt, l, flags);

    if (l->t == NULL)
        launch_fail(l, EINVAL, false,
                    "Failed to create transport for address '%s'.", l->addr);

    if (!iot_transport_connect(l->t, &addr, len))
        launch_fail(l, EINVAL, false,
                    "Failed to connect to transport address '%s'.", l->addr);
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
    static gid_t gids[64];
    int          ngid;

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
        l->uid = getuid();

    if (l->groups != NULL) {
        ngid = iot_get_groups(l->groups, gids, IOT_ARRAY_SIZE(gids));

        if (ngid < 0 || ngid >= (int)IOT_ARRAY_SIZE(gids))
            print_usage(l, EINVAL, "invalid group/group list '%s'", l->groups);
    }
    else {
        ngid = getgroups(IOT_ARRAY_SIZE(gids) - 1, gids + 1);

        if (ngid < 0)
            print_usage(l, EINVAL, "failed to get supplementary group list");

        gids[0] = getgid();
        ngid++;
    }

    l->ngid = ngid;
    l->gids = gids;
}


static void resolve_manifest(launcher_t *l)
{
    static char app[128];
    char pkg[128];

    if (l->appid == NULL)
        print_usage(l, EINVAL, "No appid, cannot resolve manifest.");

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
    l->m   = iot_manifest_get(l->uid, pkg);

    if (l->m == NULL)
        print_usage(l, EINVAL, "failed to load manifest for user %d", l->uid);
}


static void override_manifest(launcher_t *l)
{
    static char  app[128];
    const  char *apps[1];
    char         pkg[128];
    int          n;

    /*
     * Notes:
     *   Validate and load an overridden manifest.
     *
     *   In development mode if a manifest was given validate and load it
     *   here. Also if the application was omitted, pick the first one we
     *   find in the loaded manifest (which is not guaranteed to be the
     *   first one in the actual manifest file).
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


static void resolve_appid(launcher_t *l)
{
    static char fqai[1024];

    l->pkg = iot_manifest_package(l->m);

    if (iot_application_id(fqai, sizeof(fqai), l->uid, l->pkg, l->app) == NULL)
        launch_fail(l, EINVAL, false, "Can't determine appid for %d:%s:%s.",
                    l->uid, l->pkg, l->app);

    l->fqai = fqai;
}


static void resolve_cgroup_path(launcher_t *l)
{
    if (l->argc != 1)
        launch_fail(l, EINVAL, false, "Agent expects a single cgroup path.");

    if (*l->argv[0] != '/')
        launch_fail(l, EINVAL, false, "Agent expects an absolute cgroup path.");

    l->cgroup = l->argv[0];
}


static void launcher_init(launcher_t *l, const char *argv0, char **envp)
{
    IOT_UNUSED(envp);

    iot_clear(l);

    l->ml = iot_mainloop_create();

    if (l->ml == NULL)
        launch_fail(l, EINVAL, false, "Failed to create launcher mainloop.");

    l->seqno = 1;

    install_signal_handlers(l);
    config_set_defaults(l, argv0);
}


static iot_json_t *debug_options(launcher_t *l)
{
    iot_json_t *dbg;

    if (l->label || l->user || l->groups || l->privileges || l->manifest ||
        l->shell || l->bringup || l->unconfined) {
        dbg = iot_json_create(IOT_JSON_OBJECT);

        if (dbg == NULL)
            launch_fail(l, EINVAL, false, "Failed to create debug submessage.");

        if (l->label)
            iot_json_add_string(dbg, "label", l->label);

        if (l->user)
            iot_json_add_integer(dbg, "user", l->uid);
        if (l->groups)
            iot_json_add_integer(dbg, "group", l->gids[0]);

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
    static const char *argv[128];
    int          argc, i;
    size_t       size;
    iot_json_t  *req, *dbg;

    size = IOT_ARRAY_SIZE(argv);
    argc = iot_manifest_arguments(l->m, l->app, argv, size);

    if (argc < 0)
        launch_fail(l, EINVAL, false, "Failed to determine launch arguments.");

    l->app_argv = argv;
    l->app_argc = argc + l->argc;

    if (l->app_argc > (int)size)
        launch_fail(l, EINVAL, false, "Too many launch arguments (%d > %d).",
                    l->app_argc, (int)size);

    for (i = 0; i < l->argc; i++)
        argv[argc + i] = l->argv[i];

    req = iot_json_create(IOT_JSON_OBJECT);

    if (req == NULL)
        launch_fail(l, ENOMEM, false, "Failed to create setup request.");

    iot_json_add_integer(req, "user"     , l->uid);
    iot_json_add_integer(req, "group"    , l->gids[0]);
    iot_json_add_string (req, "manifest" , iot_manifest_path(l->m));
    iot_json_add_string (req, "app"      , l->app);

    iot_json_add_string_array(req, "exec", l->app_argv, l->app_argc);

    if ((dbg = debug_options(l)) != NULL)
        iot_json_add(req, "debug", dbg);

    return create_request(l, "setup", req);
}


static iot_json_t *create_stop_request(launcher_t *l)
{
    iot_json_t *req = iot_json_create(IOT_JSON_OBJECT);
    char        appid[512];
    int         n;

    if (req == NULL)
        launch_fail(l, ENOMEM, false, "Failed to create stop request.");

    n = snprintf(appid, sizeof(appid), "%s:%s", l->pkg, l->app);

    if (n < 0 || n >= (int)sizeof(appid))
        launch_fail(l, EINVAL, false, "Failed to create appid.");

    iot_json_add_string(req, "app", appid);

    return create_request(l, "stop", req);
}


static iot_json_t *create_cleanup_request(launcher_t *l)
{
    iot_json_t *req = iot_json_create(IOT_JSON_OBJECT);

    if (req == NULL)
        launch_fail(l, ENOMEM, false, "Failed to create cleanup request.");

    iot_json_add_string(req, "cgroup", l->cgroup);

    return create_request(l, "cleanup", req);
}


static iot_json_t *create_list_request(launcher_t *l)
{
    iot_json_t *req = iot_json_create(IOT_JSON_OBJECT);
    const char *type;

    if (req == NULL)
        launch_fail(l, ENOMEM, false, "Failed to create list request.");

    type = (l->mode == LAUNCHER_LIST_INSTALLED ? "installed" : "running");

    iot_json_add_string(req, "type", type);

    return create_request(l, "list", req);
}


int main(int argc, char *argv[], char **envp)
{
    launcher_t  l;
    iot_json_t *req;

    iot_switch_userid(IOT_USERID_REAL);

    launcher_init(&l, argv[0], envp);
    parse_cmdline(&l, argc, argv, envp);
    setup_logging(&l);

    if (l.mode == LAUNCHER_SETUP || l.mode == LAUNCHER_STOP) {
        resolve_identities(&l);

        if (l.manifest == NULL)
            resolve_manifest(&l);
        else
            override_manifest(&l);

        resolve_appid(&l);
    }

    switch (l.mode) {
    case LAUNCHER_SETUP:
        req = create_setup_request(&l);
        break;

    case LAUNCHER_STOP:
        req = create_stop_request(&l);
        break;

    case LAUNCHER_CLEANUP:
        resolve_cgroup_path(&l);
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

