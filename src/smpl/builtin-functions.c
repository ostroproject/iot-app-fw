/*
 * Copyright (c) 2016, Intel Corporation
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
#include <pwd.h>
#include <sys/types.h>

#include <smpl/smpl.h>

#define FN_ERROR     "ERROR"
#define FN_WARNING   "WARNING"
#define FN_ADDON     "REQUEST-ADDON"
#define FN_COUNTER   "COUNTER"
#define FN_USER_HOME "USER-HOME"

static int fn_error(smpl_t *smpl, int argc, smpl_value_t *argv,
                    smpl_value_t *rv, void *user_data)
{
    char buf[4096], *p, *msg;
    int  l, n, err, i, idx;

    SMPL_UNUSED(rv);
    SMPL_UNUSED(user_data);

    err = -1;
    idx = 0;
    msg = "template evaluation failure";

    if (argc > 0) {
        switch (argv[0].type) {
        case SMPL_VALUE_INTEGER:
            err = argv[0].i32;
            idx = 1;
            break;
        case SMPL_VALUE_STRING:
            idx = 0;
            break;
        default:
            idx = 0;
            break;
        }
    }

    if (!err)
        err = -1;
    else if (err > 0)
        err = -err;

    p    = buf;
    l    = (int)sizeof(buf) - 1;
    p[l] = '\0';

    for (i = idx; i < argc; i++) {
        switch (argv[i].type) {
        case SMPL_VALUE_STRING:
            n = snprintf(p, l, "%s", argv[i].str);
            break;
        case SMPL_VALUE_INTEGER:
            n = snprintf(p, l, "%d", argv[i].i32);
            break;
        case SMPL_VALUE_DOUBLE:
            n = snprintf(p, l, "%f", argv[i].dbl);
            break;
        default:
            n = snprintf(p, l, "<invalid argument to %s>", FN_ERROR);
            break;
        }

        if (n >= l)
            break;

        p += n;
        l -= n;
    }

    if (l > 0) {
        *p = '\0';
        msg = buf;
    }

    smpl_fail(-1, smpl, err, "ERROR: %s", msg);
}


static int fn_warning(smpl_t *smpl, int argc, smpl_value_t *argv,
                      smpl_value_t *rv, void *user_data)
{
    int i;

    SMPL_UNUSED(smpl);
    SMPL_UNUSED(rv);
    SMPL_UNUSED(user_data);

    for (i = 0; i < argc; i++) {
        if (argv[i].type == SMPL_VALUE_STRING)
            smpl_warn("template evaluation warning: %s", argv[i].str);
    }

    return 0;
}


static int fn_addon(smpl_t *smpl, int argc, smpl_value_t *argv,
                    smpl_value_t *rv, void *user_data)
{
    smpl_addon_t *addon;
    smpl_value_t *arg;
    smpl_json_t  *data;
    const char   *tag, *val, *name, *template, *destination;
    int           len, n, verdict, i;

    SMPL_UNUSED(user_data);
    SMPL_UNUSED(rv);

    data  = smpl_json_create(SMPL_JSON_OBJECT);
    addon = smpl_alloct(typeof(*addon));

    if (data == NULL || addon == NULL)
        goto nomem;

    smpl_list_init(&addon->hook);

    name = template = destination = NULL;
    for (i = 0, arg = argv; i < argc; i++, arg++) {
        if (arg->type != SMPL_VALUE_STRING)
            goto invalid_arg;

        tag = arg->str;
        val = strchr(arg->str, ':');

        if (val == NULL)
            goto invalid_val;

        len = val - tag;
        val++;

        smpl_debug("addon tag:value: '%*.*s':'%s'", len, len, tag, val);

#define TAGGED(_tag) ((n = sizeof(_tag) - 1) == len && !strncmp(tag, _tag, len))
        if (TAGGED("name")) {
            smpl_json_add_string(data, "name", val);
            name = val;
        }
        else if (TAGGED("template"))
            template = val;
        else if (TAGGED("destination"))
            destination = val;
        else if (TAGGED("data")) {
            if (i >= argc - 1)
                goto missing_data;

            i++;
            arg++;

            switch (arg->type) {
            case SMPL_VALUE_STRING:
                smpl_json_add_string(data, val, arg->str);
                break;
            case SMPL_VALUE_INTEGER:
                smpl_json_add_integer(data, val, arg->i32);
                break;
            case SMPL_VALUE_DOUBLE:
                smpl_json_add_double(data, val, arg->dbl);
                break;
            case SMPL_VALUE_OBJECT:
            case SMPL_VALUE_ARRAY:
                smpl_json_add_object(data, val, arg->json);
                break;
            case SMPL_VALUE_UNSET:
                break;
            default:
                goto invalid_val;
            }
        }
        else
            goto invalid_tag;
    }
#undef TAGGED

    if (name == NULL)
        goto missing_name;

    verdict = addon_create(smpl, name, template, destination, data);

    if (verdict < 0)
        goto addon_error;

    return 0;

 invalid_arg:
    addon_free(addon);
    smpl_json_unref(data);
    smpl_fail(-1, smpl, EINVAL, "invalid argument type to %s", FN_ADDON);

 invalid_val:
    addon_free(addon);
    smpl_json_unref(data);
    smpl_fail(-1, smpl, EINVAL, "invalid argument value to %s", FN_ADDON);

 invalid_tag:
    addon_free(addon);
    smpl_json_unref(data);
    smpl_fail(-1, smpl, EINVAL, "unknown tag:value '%s' to %s", tag, FN_ADDON);

 missing_name:
    smpl_json_unref(data);
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "missing name to %s", FN_ADDON);

 missing_data:
    smpl_json_unref(data);
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "missing data for %s argument '%s'", FN_ADDON,
              val);

 addon_error:
    smpl_json_unref(data);
    smpl_fail(-1, smpl, -verdict, "failed to create addon");

 nomem:
    return -1;
}


static int fn_counter(smpl_t *smpl, int argc, smpl_value_t *argv,
                      smpl_value_t *rv, void *user_data)
{
    #define MAX_COUNTERS 64

    static struct {
        const char *name;
        int         cnt;
    } counters[] = {
        [0]                      = { "default", 0  },
        [1 ... MAX_COUNTERS - 1] = { NULL     , 0  },
        [MAX_COUNTERS]           = { NULL     , -1 },
    }, *cnt;
    const char *which = "default";
    int diff = 0;

    SMPL_UNUSED(user_data);

    if (argc > 2)
        goto toomany_args;

    if (argc > 0) {
        if (argv[0].type != SMPL_VALUE_STRING)
            goto invalid_arg;

        which = *argv[0].str ? argv[0].str : "default";
    }

    if (argc > 1) {
        if (argv[1].type != SMPL_VALUE_INTEGER)
            goto invalid_arg;

        diff = argv[1].i32;
    }

    for (cnt = counters; cnt->name && strcmp(cnt->name, which) != 0; cnt++)
        ;

    if (cnt->cnt < 0)
        goto invalid_counter;

    if (cnt->name == NULL)
        cnt->name = iot_strdup(which);

    rv->type = SMPL_VALUE_INTEGER;
    rv->i32  = cnt->cnt += diff;

    return 0;

 toomany_args:
    smpl_fail(-1, smpl, EINVAL, "too many arguments to function %s", FN_COUNTER);

 invalid_arg:
    smpl_fail(-1, smpl, EINVAL, "invalid argument to function %s", FN_COUNTER);

 invalid_counter:
    smpl_fail(-1, smpl, EINVAL, "invalid/unknown counter '%s'", which);
}


static int fn_user_home(smpl_t *smpl, int argc, smpl_value_t *argv,
                        smpl_value_t *rv, void *user_data)
{
    const char    *name;
    struct passwd  e, *r;
    char           buf[1024];

    SMPL_UNUSED(user_data);

    if (argc != 1)
        goto toomany_args;

    if (argv[0].type != SMPL_VALUE_STRING)
        goto invalid_arg;

    name = argv[0].str;

    if (getpwnam_r(name, &e, buf, sizeof(buf), &r) < 0)
        goto pwnam_error;

    if (r == NULL)
        goto unknown_user;

    rv->type    = SMPL_VALUE_STRING;
    rv->dynamic = 1;
    rv->str     = iot_strdup(r->pw_dir);

    if (rv->str == NULL)
        goto nomem;

    return 0;

 toomany_args:
    smpl_fail(-1, smpl, EINVAL, "too many arguments to function %s", FN_COUNTER);

 invalid_arg:
    smpl_fail(-1, smpl, EINVAL, "invalid argument to function %s", FN_COUNTER);

 pwnam_error:
    smpl_fail(-1, smpl, EINVAL, "failed to get passwd entry for user %s", name);

 unknown_user:
    smpl_fail(-1, smpl, ENOENT, "no passwd entry for unknown user %s", name);

 nomem:
    return -1;
}


void builtin_register(void)
{
    function_register(NULL, FN_ERROR    , fn_error    , NULL);
    function_register(NULL, FN_WARNING  , fn_warning  , NULL);
    function_register(NULL, FN_ADDON    , fn_addon    , NULL);
    function_register(NULL, FN_COUNTER  , fn_counter  , NULL);
    function_register(NULL, FN_USER_HOME, fn_user_home, NULL);
}
