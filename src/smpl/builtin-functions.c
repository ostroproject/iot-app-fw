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

#include <smpl/smpl.h>

#define FN_ERROR   "ERROR"
#define FN_WARNING "WARNING"
#define FN_ADDON   "REQUEST-ADDON"


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
    const char   *tag, *val, *name, *template, *destination;
    int           len, n, verdict, i;

    SMPL_UNUSED(user_data);
    SMPL_UNUSED(rv);

    addon = smpl_alloct(typeof(*addon));

    if (addon == NULL)
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
        if (TAGGED("name"))
            name = val;
        else if (TAGGED("template"))
            template = val;
        else if (TAGGED("destination"))
            destination = val;
        else
            goto invalid_tag;
    }
#undef TAGGED

    if (name == NULL)
        goto missing_name;

    verdict = addon_create(smpl, name, template, destination);

    if (verdict < 0)
        goto addon_error;

    return 0;

 invalid_arg:
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "invalid argument type to %s", FN_ADDON);

 invalid_val:
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "invalid argument value to %s", FN_ADDON);

 invalid_tag:
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "unknown tag:value '%s' to %s", tag, FN_ADDON);

 missing_name:
    addon_free(addon);
    smpl_fail(-1, smpl, EINVAL, "missing name to %s", FN_ADDON);

 addon_error:
    smpl_fail(-1, smpl, -verdict, "failed to create addon");

 nomem:
    return -1;
}


void builtin_register(void)
{
    function_register(NULL, FN_ERROR  , fn_error  , NULL);
    function_register(NULL, FN_WARNING, fn_warning, NULL);
    function_register(NULL, FN_ADDON  , fn_addon  , NULL);
}
