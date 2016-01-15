#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>

#include "jmpl/jmpl.h"


typedef struct {
    jmpl_t     *jmpl;
    iot_json_t *json;
} jmpl_test_t;


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list ap;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    fprintf(stderr, "usage: %s [options] template-file JSON-file\n"
            "\n"
            "Instantiate <template-file> with <JSON-file> and print it.\n"
            "\n"
            "The possible options are:\n"
            "  -v, --verbose       increase logging verbosity\n"
            "  -d, --debug <site>  enable debugging for <site>\n"
            "  -h, --help          print (this) help on usage\n", argv0);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void parse_cmdline(jmpl_test_t *t, int argc, char *argv[])
{
#define OPTIONS "vd:h"
    static struct option options[] = {
        { "verbose" , no_argument      , NULL, 'v' },
        { "debug"   , required_argument, NULL, 'd' },
        { "help"    , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    int opt;

    iot_clear(t);

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'v': {
            int m, d;

            m = iot_log_get_mask();
            d = m & IOT_LOG_MASK_DEBUG;

            iot_log_set_mask((m << 1) | 0x1);

            if (!d && (iot_log_get_mask()) & IOT_LOG_MASK_DEBUG) {
                iot_debug_enable(true);
                iot_debug_set_config("*");
            }

            break;
        }

        case 'd':
            iot_debug_enable(true);
            iot_debug_set_config(optarg);
            break;

        case 'h':
            print_usage(argv[0], 0, "");
            break;

        case '?':
            print_usage(argv[0], EINVAL, "invalid argument '%c'", optopt);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid argument '%c'", opt);
            break;
        }
    }

    if (argc - optind != 2)
        print_usage(argv[0], EINVAL, "");

    t->jmpl = jmpl_load_template(argv[optind]);

    if (t->jmpl == NULL) {
        iot_log_error("Failed to load JSON template '%s'.", argv[optind]);
        exit(1);
    }

    optind++;

    t->json = jmpl_load_json(argv[optind]);

    if (t->json == NULL) {
        iot_log_error("Failed to load JSON file '%s'.", argv[optind]);
        exit(1);
    }
}


int main(int argc, char *argv[])
{
    jmpl_test_t t;

    parse_cmdline(&t, argc, argv);

    printf("JSON data: '%s'\n", iot_json_object_to_string(t.json));

    return 0;
}
