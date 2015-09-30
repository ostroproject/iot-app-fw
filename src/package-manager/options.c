#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/common.h>

#include "options.h"

#define LOG_MASK_DEFAULT    (IOT_LOG_MASK_ERROR | \
			     IOT_LOG_MASK_INFO  )
#define LOG_TARGET_DEFAULT  "stderr"

static void set_defaults(iotpm_t *, int, char **);
static void parse_cmdline(iotpm_t *, int, char **);
static void check_configuration(iotpm_t *);
static void print_usage(iotpm_t *, int, char *, ...);
static void set_mode(iotpm_t *, iotpm_mode_t);
static void set_flag(iotpm_t *, iotpm_flag_t);
static void set_log_mask(iotpm_t *, const char *);
static void set_log_target(iotpm_t *, const char *);
static void set_debug(iotpm_t *iotpm, const char *);

bool iotpm_options_init(iotpm_t *iotpm, int argc, char **argv)
{
    if (!iotpm || argc < 1 || !argv)
        return false;

    set_defaults(iotpm, argc,argv);
    parse_cmdline(iotpm, argc,argv);
    check_configuration(iotpm);

    return true;
}

void iotpm_options_exit(iotpm_t *iotpm)
{
    int i;

    if (iotpm) {
        iot_log_set_mask(0);
	iot_log_set_target("stderr");

	free((void *)iotpm->log_target);

	for (i = 0;  i < iotpm->argc;  i++)
	    iot_free((void *)iotpm->argv[i]);
	iot_free((void *)iotpm->argv);

	iotpm->mode = IOTPM_MODE_NONE;
	iotpm->log_target = NULL;
	iotpm->log_mask = 0;
	iotpm->argc = 0;
	iotpm->argv = NULL;
    }
}

static void set_defaults(iotpm_t *iotpm, int argc, char **argv)
{
    IOT_UNUSED(argc);
    IOT_UNUSED(argv);

    iotpm->log_mask = LOG_MASK_DEFAULT;
}

static void parse_cmdline(iotpm_t *iotpm, int argc, char **argv)
{
#define OPTIONS "isurcpLl:t:d:h"
#define INVALID "inavlid option '%c'", opt

    static struct option options[] = {
        { "install"          ,  no_argument      ,  NULL,  'i' },
        { "register-security",  no_argument      ,  NULL,  's' },
	{ "upgrade"          ,  no_argument      ,  NULL,  'u' },
	{ "remove"           ,  no_argument      ,  NULL,  'r' },
	{ "db-check"         ,  no_argument      ,  NULL,  'c' },
	{ "db-plant"         ,  no_argument      ,  NULL,  'p' },
	{ "list"             ,  optional_argument,  NULL,  'L' },
	{ "log-level"        ,  required_argument,  NULL,  'l' },
	{ "log-target"       ,  required_argument,  NULL,  't' },
	{ "debug"            ,  required_argument,  NULL,  'd' },
	{ "help"             ,  no_argument      ,  NULL,  'h' },
	{   NULL             ,       0           ,  NULL,   0  }
    };

    int opt, i;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {

        case 'i':    set_mode(iotpm, IOTPM_MODE_POSTINST);        break;
        case 's':    set_mode(iotpm, IOTPM_MODE_PREINST);         break;
        case 'u':    set_mode(iotpm, IOTPM_MODE_UPGRADE);         break;
	case 'r':    set_mode(iotpm, IOTPM_MODE_REMOVE);          break;
	case 'c':    set_mode(iotpm, IOTPM_MODE_DBCHECK);         break;
	case 'p':    set_mode(iotpm, IOTPM_MODE_DBPLANT);         break;
	case 'L':    set_mode(iotpm, IOTPM_MODE_LIST);            break;
        case 'l':    set_log_mask(iotpm, optarg);                 break;
        case 't':    set_log_target(iotpm, optarg);               break;
	case 'd':    set_debug(iotpm, optarg);                    break;
	case 'h':    print_usage(iotpm, 0, NULL);                 break;

	default:     print_usage(iotpm, EINVAL, INVALID);         break;

	} /* switch opt */
    }

    iotpm->argc = argc - optind;
    iotpm->argv = iot_allocz(sizeof(char *) * (iotpm->argc + 1));

    for (i = 0;  i < iotpm->argc;  i++) {
        if (!(iotpm->argv[i] = iot_strdup(argv[optind + i]))) {
	    fprintf(stderr, "failed to allocate memory for package name\n");
	    exit(ENOMEM);
	}
    }

#undef INVALID
#undef OPTIONS
}

static void check_configuration(iotpm_t *iotpm)
{
    int n;

    switch (iotpm->mode) {

    case IOTPM_MODE_POSTINST:
    case IOTPM_MODE_UPGRADE:
        if ((n = iotpm->argc) != 1) {
	    print_usage(iotpm, EINVAL, "%s <package file>",
                        n ? "too many" : "missing");
        }
        break;

    case IOTPM_MODE_PREINST:
    case IOTPM_MODE_REMOVE:
        if ((n = iotpm->argc) != 1) {
	    print_usage(iotpm, EINVAL, "%s <package name>",
                        n ? "too many" :"missing");
        }
	break;

    case IOTPM_MODE_DBCHECK:
    case IOTPM_MODE_DBPLANT:
        if (iotpm->argc)
	    print_usage(iotpm, EINVAL, "can't specify <package>");
	break;

    case IOTPM_MODE_LIST:
        if (iotpm->argc > 1)
	    print_usage(iotpm, EINVAL, "to many filetring pattern");
        break;

    default:
        print_usage(iotpm, EINVAL, "missing <mode-option>");
	break;
    }

    if (!iotpm->log_target)
        iotpm->log_target = iot_strdup(LOG_TARGET_DEFAULT);

    if (!iot_log_set_target(iotpm->log_target)) {
        fprintf(stderr, "failed to set log target '%s'\n", iotpm->log_target);
	exit(EINVAL);
    }

    iot_log_set_mask(iotpm->log_mask);

    if (iotpm->debugging) {
        iotpm->log_mask |= IOT_LOG_MASK_DEBUG;
        iot_log_set_mask(iotpm->log_mask);
        iot_debug_enable(TRUE);
    }
}

static void print_usage(iotpm_t *iotpm, int exit_code, char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
        va_end(ap);

	fprintf(stderr, "\n\n");
    }

    fprintf(stderr, "usage:\n"
        "  %s <mode-option> [<log-options>] [<package>]\n"
	"  %s {-h | --help}\n\n"
	"where <mode-option> is one of\n"
	"  -i or --install           (<package> is path to package file)\n"
	"  -s or --register-security (<package> is the name of the package)\n"
	"  -u or --upgrade           (<package> is path to package file)\n"
	"  -r or --remove            (<package> is the name of the package)\n"
	"  -c or --db-check          (no <package> can be specified)\n"
	"  -p or --db-plant          (no <package> can be specified)\n"
	"  -L or --list              (no <package> can be specified\n"
	"<log-options> are\n"
	"  -t <target>  or --log-target=<target> where\n"
	"       <target> is one of stderr,stdout,syslog or a logfile path\n"
	"  -l <levels> or --log-level=<levels> where\n"
	"       <levels> is a comma separated list of info, error or warning\n"
	"  -d or --debug <site> enable given debug site\n",
	iotpm->prognam, iotpm->prognam);

    if (exit_code >= 0)
        exit(exit_code);

    return;
}

static void set_mode(iotpm_t *iotpm, iotpm_mode_t mode)
{
    if (iotpm->mode != IOTPM_MODE_NONE)
        print_usage(iotpm, EINVAL, "attempt to set multiple modes");

    iotpm->mode = mode;
}

static void set_flag(iotpm_t *iotpm, iotpm_flag_t flag)
{
    if (flag) {
        if ((iotpm->flags & flag) ==  flag)
            print_usage(iotpm, EINVAL, "attempt to set option multiple times");

        iotpm->flags |= flag;
    }
}

static void set_log_mask(iotpm_t *iotpm, const char *level)
{
    iot_log_mask_t log_mask;

    if ((log_mask = iot_log_parse_levels(level)) < 0)
        print_usage(iotpm, EINVAL, "invalid log level '%s'", level);

    iotpm->log_mask = log_mask;
}

static void set_log_target(iotpm_t *iotpm, const char *target)
{
    if (target) {
        iot_free((void *)iotpm->log_target);
	iotpm->log_target = iot_strdup(target);

	if (!iotpm->log_target) {
	    fprintf(stderr, "failed to allocate memory for log target\n");
	    exit(ENOMEM);
	}
    }
}

static void set_debug(iotpm_t *iotpm, const char *debug_cmd)
{
    if (debug_cmd) {
        iotpm->debugging = true;
        iot_debug_set_config(debug_cmd);
    }
}
