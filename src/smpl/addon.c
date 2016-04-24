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

#include <limits.h>

#include <smpl/macros.h>
#include <smpl/types.h>
#include <smpl/smpl.h>


static int addon_notify(smpl_t *smpl, smpl_addon_t *a)
{
    if (smpl->addon_notify == NULL)
        return 1;
    else
        return smpl->addon_notify(smpl, a, smpl->user_data);
}


int addon_create(smpl_t *smpl, const char *name, const char *template,
                 const char *destination, smpl_json_t *data)
{
    smpl_addon_t *a;
    int           verdict;

    a = smpl_alloct(typeof(*a));

    if (a == NULL)
        goto nomem;

    smpl_list_init(&a->hook);

    if (!result_init(&a->result, destination))
        goto nomem;

    a->name     = smpl_strdup(name);
    a->template = smpl_strdup(template);
    a->data     = data;

    if (!a->name || (!a->template && template))
        goto nomem;

    verdict = addon_notify(smpl, a);

    if (verdict < 0)
        goto notifier_error;

    if (verdict > 0) {
        smpl_debug("addon '%s' registered", a->name);
        smpl_list_append(&smpl->addons, &a->hook);
    }
    else {
        smpl_debug("addon %s rejected by notifier callback", a->name);
        addon_free(a);
    }

    return verdict > 0 ? 1 : 0;

 nomem:
    smpl_free(a);
    return -1;

 notifier_error:
    smpl_fail(-1, smpl, -verdict, "addon notifier failed");
}


void addon_free(smpl_addon_t *a)
{
    if (a == NULL)
        return;

    smpl_list_delete(&a->hook);
    result_free(&a->result);
    smpl_free(a->name);
    smpl_free(a->template);
    smpl_json_unref(a->data);
    smpl_free(a);
}


int addon_set_destination(smpl_addon_t *a, const char *destination)
{
    return result_set_destination(&a->result, destination);
}


int addon_set_template(smpl_addon_t *a, const char *template)
{
    smpl_free(a->template);
    a->template = smpl_strdup(template);

    if (a->template == NULL && template != NULL)
        return -1;

    return 0;
}


smpl_t *addon_load(smpl_t *smpl, smpl_addon_t *a)
{
    smpl_t *addon;
    char   *template, buf[PATH_MAX], **errors;
    int     n;

    smpl_debug("loading addon '%s'...", a->name);

    if (a->template != NULL)
        template = a->template;
    else {
        n = snprintf(buf, sizeof(buf), "%s.template", a->name);

        if (n < 0 || n >= (int)sizeof(buf))
            goto name_error;

        template = buf;
    }

    addon = smpl_load_template(template, smpl->addon_notify, &errors);

    if (addon == NULL)
        goto load_error;

    return addon;

 name_error:
    smpl_fail(NULL, smpl, EINVAL, "failed to get template file name");

 load_error:
    smpl_append_errors(smpl, errors);
    smpl_free_errors(errors);
    smpl_fail(NULL, smpl, EINVAL, "failed to load addon template '%s'",
              template);
}


int addon_evaluate(smpl_t *smpl, smpl_addon_t *a, const char *data_name,
                   smpl_data_t *data)
{
    smpl_t *ampl;
    int     r;

    ampl = addon_load(smpl, a);

    if (ampl == NULL)
        goto load_error;

    smpl_debug("evaluating addon template '%s'...", a->name);

    if (a->template != NULL)
        smpl_json_add_string(a->data, "template", a->template);

    smpl_json_add_object(data, "addon", smpl_json_ref(a->data));
    r = smpl_evaluate(ampl, data_name, data, smpl->user_data, &a->result);
    smpl_json_del_member(data, "addon");

    smpl_free_template(ampl);

    if (r < 0)
        goto eval_error;

    return 0;

 load_error:
    return -1;

 eval_error:
    smpl_append_errors(smpl, a->result.errors);
    smpl_free_errors(a->result.errors);
    a->result.errors = NULL;

    smpl_fail(-1, smpl, EINVAL, "failed to evaluate addon '%s'", a->name);
}

