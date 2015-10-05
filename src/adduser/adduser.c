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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/utils/identity.h>

#ifdef ENABLE_SECURITY_MANAGER
#  include <sys/smack.h>
#  include <security-manager/security-manager.h>
#endif

#define addusr_print printf
#define addusr_info  iot_log_info
#define addusr_error iot_log_error
#define addusr_warn  iot_log_warning
#define addusr_debug iot_debug

#define addusr_fatal(_l, _error, _usage, ...) do { \
        if (_usage)                                \
            print_usage(_l, _error, __VA_ARGS__);  \
        else                                       \
            iot_log_error(__VA_ARGS__);            \
                                                   \
        exit(_error);                              \
    } while (0)


typedef struct {
    const char *argv0;
    const char *user;
    const char *type;
    int         remove;
    int         log_mask;
    const char *log_target;
} addusr_t;



static const char *addusr_base(const char *argv0)
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


static void print_usage(addusr_t *a, int exit_code, const char *fmt, ...)
{
    va_list     ap;
    const char *base;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }

    base = addusr_base(a->argv0);

    printf("usage: %s [options] user\n", base);
    printf("The possible options are:\n"
           "  -t, --type=<TYPE>           type of user to register\n"
           "      USER is one of normal, system, admin, and guest\n"
           "  -L, --log-level=<LEVELS>     what messages to log\n"
           "    LEVELS is a comma-separated list of info, error and warning\n"
           "  -T, --log-target=<TARGET>    where to log messages\n"
           "    TARGET is one of stderr, stdout, syslog, or a logfile path\n"
           "  -v, --verbose                increase logging verbosity\n"
           "  -d, --debug=<SITE>           turn on debugging for the give site\n"
           "    SITE can be of the form 'function', '@file-name', or '*'\n"
           "  -h, --help                   show this help message\n");

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void config_set_defaults(addusr_t *a, const char *argv0)
{
    a->argv0      = argv0;
    a->type       = "normal";
    a->log_mask   = IOT_LOG_UPTO(IOT_LOG_WARNING);
    a->log_target = "stderr";

    iot_log_set_mask(a->log_mask);
    iot_log_set_target(a->log_target);
}


static void parse_cmdline(addusr_t *a, int argc, char **argv, char **envp)
{
#   define OPTIONS "t:rL:T:v::d:h"
    struct option options[] = {
        { "type"           , required_argument, NULL, 't' },
        { "remove"         , no_argument      , NULL, 'r' },
        { "log-level"      , required_argument, NULL, 'L' },
        { "verbose"        , optional_argument, NULL, 'v' },
        { "log-target"     , required_argument, NULL, 'T' },
        { "debug"          , required_argument, NULL, 'd' },
        { "help"           , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt, help;

    IOT_UNUSED(envp);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 't':
            a->type = optarg;
            break;

        case 'r':
            a->remove = TRUE;
            break;

        case 'L':
            a->log_mask = iot_log_parse_levels(optarg);
            break;

        case 'v':
            a->log_mask <<= 1;
            a->log_mask  |= 1;
            break;

        case 'T':
            a->log_target = optarg;
            break;

        case 'd':
            a->log_mask |= IOT_LOG_MASK_DEBUG;
            iot_log_set_mask(a->log_mask);
            iot_debug_set_config(optarg);
            iot_debug_enable(TRUE);
            break;

        case 'h':
            help++;
            break;

        default:
            print_usage(a, EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(a, -1, "");
        exit(0);
    }

    if (optind >= argc)
        print_usage(a, EINVAL, "error: username not specified");

    if (optind != argc - 1)
        print_usage(a, EINVAL, "error: too many arguments");

    a->user = argv[optind];
}


static void setup_logging(addusr_t *a)
{
    const char *target;

    if (a->log_mask < 0)
        print_usage(a, EINVAL, "invalid log level '%s'", optarg);

    target = iot_log_parse_target(a->log_target);

    if (!target)
        print_usage(a, EINVAL, "invalid log target '%s'", a->log_target);

    iot_log_set_mask(a->log_mask);
    iot_log_set_target(target);
}


static void addusr_init(addusr_t *a, const char *argv0, char **envp)
{
    IOT_UNUSED(envp);

    iot_clear(a);
    config_set_defaults(a, argv0);
}


static void add_user(addusr_t *a)
{
#ifdef ENABLE_SECURITY_MANAGER
    user_req   *req;
    int         type;
#endif
    uid_t       uid;

    uid = iot_get_userid(a->user);

    if (uid == (uid_t)-1)
        addusr_fatal(a, EINVAL, FALSE, "Couldn't find user id for '%s'.",
                     a->user);

#ifdef ENABLE_SECURITY_MANAGER
    if (security_manager_user_req_new(&req) != 0)
        addusr_fatal(a, ENOMEM, FALSE, "could not create user add request");

    iot_debug("requesting addition of user %s (%u)", a->user, uid);

    if (security_manager_user_req_set_uid(req, uid) != 0)
        addusr_fatal(a, EINVAL, FALSE, "failed to set user id in request");

    if      (!strcmp(a->type, "normal")) type = SM_USER_TYPE_NORMAL;
    else if (!strcmp(a->type, "admin" )) type = SM_USER_TYPE_ADMIN;
    else if (!strcmp(a->type, "system")) type = SM_USER_TYPE_SYSTEM;
    else if (!strcmp(a->type, "guest" )) type = SM_USER_TYPE_GUEST;
    else
        addusr_fatal(a, EINVAL, TRUE, "invalid user type '%s'", a->type);

    iot_debug("requesting user type %s (%d)", a->type, type);

    if (security_manager_user_req_set_user_type(req, type) != 0)
        addusr_fatal(a, EINVAL, FALSE, "failed to set user type in request");

    if (security_manager_user_add(req) != 0)
        addusr_fatal(a, EINVAL, FALSE, "failed to register user");

    printf("'%s' (%u) added to security mananger as a %s (%d) user.\n",
           a->user, uid, a->type, type);
#else
    printf("If SM was enabled, I'd try to add '%s' (%d) as a %s user...\n",
           a->user, uid, a->type);
#endif
}


static void del_user(addusr_t *a)
{
#ifdef ENABLE_SECURITY_MANAGER
    user_req   *req;
    int         type;
#endif
    uid_t       uid;

    uid = iot_get_userid(a->user);

    if (uid == (uid_t)-1)
        addusr_fatal(a, EINVAL, FALSE, "Couldn't find user id for '%s'.",
                     a->user);

#ifdef ENABLE_SECURITY_MANAGER
    if (security_manager_user_req_new(&req) != 0)
        addusr_fatal(a, ENOMEM, FALSE, "could not create user add request");

    iot_debug("requesting removal of user %s (%u)", a->user, uid);

    if (security_manager_user_req_set_uid(req, uid) != 0)
        addusr_fatal(a, EINVAL, FALSE, "failed to set user id in request");

    if (security_manager_user_delete(req) != 0)
        addusr_fatal(a, EINVAL, FALSE, "failed to unregister user");

    printf("'%s' (%u) removed from security mananger\n", a->user, uid);
#else
    printf("If SM was enabled, I'd try to remove '%s' (%d) user...\n",
           a->user, uid);
#endif
}


int main(int argc, char *argv[], char **envp)
{
    addusr_t a;

    addusr_init(&a, argv[0], envp);
    parse_cmdline(&a, argc, argv, envp);
    setup_logging(&a);

    if (!a.remove)
        add_user(&a);
    else
        del_user(&a);

    return 0;
}

