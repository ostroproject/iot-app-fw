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

#include <smpl/macros.h>
#include <smpl/types.h>


static SMPL_LIST(functions);             /* globally available functions */


int function_register(smpl_t *smpl, char *name, smpl_fn_t fn, void *user_data)
{
    smpl_function_t *f;

    f = smpl_alloct(typeof(*f));

    if (f == NULL)
        goto nomem;

    smpl_list_init(&f->hook);
    f->cb        = fn;
    f->user_data = user_data;
    f->name      = smpl_strdup(name);

    if (f->name == NULL)
        goto nomem;

    if (smpl == NULL)
        smpl_list_append(&functions, &f->hook);
    else
        smpl_list_append(&functions, &f->hook);

    return 0;

 nomem:
    smpl_free(f);
    return -1;
}


int function_unregister(smpl_t *smpl, char *name, smpl_fn_t fn)
{
    smpl_list_t     *p, *n;
    smpl_function_t *f;

    if (smpl != NULL) {
        smpl_list_foreach(&smpl->functions, p, n) {
            f = smpl_list_entry(p, typeof(*f), hook);

            if (strcmp(f->name, name))
                continue;

            if (fn != NULL && f->cb != fn)
                goto invalid_fn;

            smpl_list_delete(&f->hook);
            smpl_free(f->name);
            smpl_free(f);

            return 0;
        }
    }
    else {
        f = function_find(NULL, name);

        if (f != NULL) {
            if (fn != NULL && f->cb != fn)
                goto invalid_fn;

            smpl_list_delete(&f->hook);
            smpl_free(f->name);
            smpl_free(f);

            return 0;
        }
    }

 invalid_fn:
    return -1;
}


void function_purge(smpl_list_t *fns)
{
    smpl_list_t     *p, *n;
    smpl_function_t *f;

    smpl_list_foreach(fns, p, n) {
        f = smpl_list_entry(p, typeof(*f), hook);

        smpl_list_delete(&f->hook);
        smpl_free(f->name);
        smpl_free(f);
    }
}


smpl_function_t *function_find(smpl_t *smpl, const char *name)
{
    smpl_list_t     *l, *p, *n;
    smpl_function_t *f;

 retry:
    l = smpl ? &smpl->functions : &functions;
    smpl_list_foreach(l, p, n) {
        f = smpl_list_entry(p, typeof(*f), hook);

        if (!strcmp(f->name, name))
            return f;
    }

    if (smpl != NULL) {
        smpl = NULL;
        goto retry;
    }

    return NULL;
}


int function_parse_ref(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_call_t *c;
    char             *name;
    smpl_expr_t      *e;
    smpl_token_t      end;

    c = smpl_alloct(typeof(*c));

    if (c == NULL)
        goto nomem;

    smpl_list_init(&c->hook);
    c->type = SMPL_INSN_FUNCREF;

    e    = NULL;
    name = t->str;
    c->f = t->f;

    if (c->m->narg >= 0) {
        /* kludge: push back name, let expr_parse do everything for us */
        if (parser_push_token(smpl, t) < 0)
            goto failed;

        e = expr_parse(smpl, &end);

        if (e == NULL || e->type != SMPL_VALUE_FUNCREF)
            goto invalid_expr;

        if (e->call.f != c->f)
            goto invalid_expr;

        c->expr = e;
    }

    smpl_list_append(block, &c->hook);

    return 0;

 nomem:
 failed:
    return -1;

 invalid_expr:
    expr_free(e);
    smpl_fail(-1, smpl, EINVAL, "failed to parse call of function '%s'", name);
}


void function_dump_ref(smpl_t *smpl, int fd, smpl_insn_call_t *c, int indent)
{
    char buf[1024];

    expr_print(smpl, c->expr, buf, sizeof(buf));
    dprintf(fd, SMPL_INDENT_FMT"<macro call>%s\n",
            SMPL_INDENT_ARG(indent), buf);
}


void function_free_ref(smpl_insn_t *insn)
{
    smpl_insn_call_t *c = &insn->call;

    if (c == NULL)
        return;

    smpl_list_delete(&c->hook);
    expr_free(c->expr);
    smpl_free(c);
}


int function_call(smpl_t *smpl, smpl_function_t *f, int narg, smpl_value_t *args,
                  smpl_value_t *rv)
{
    smpl_value_t *argv, *a;
    int           i, r;

    smpl_debug("call %p/'%s': %d arguments, %sreturn value", f->cb, f->name,
               narg, rv ? "a " : "no ");

    if (rv != NULL)
        value_set(rv, SMPL_VALUE_UNSET);

    argv = narg ? alloca(narg * sizeof(*argv)) : NULL;
    a    = args;

    for (i = 0; i < narg; i++) {
        if (expr_eval(smpl, a, argv + i) < 0)
            goto invalid_expr;
        a = smpl_list_entry(a->hook.next, typeof(*a), hook);
    }


    smpl->callbacks++;
    r = f->cb(smpl, narg, argv, rv, f->user_data);
    smpl->callbacks--;

    for (i = 0; i < narg; i++)
        value_reset(argv + i);

    if (r < 0)
        goto function_failed;

    return 0;

 invalid_expr: {
        int j;
        for (j = 0; j < i - 1; i++)
            value_reset(argv + j);
    }
    smpl_fail(-1, smpl, EINVAL, "function '%s': failed to evaluate arg %d",
              f->name, i);

 function_failed:
    smpl_fail(-1, smpl, EINVAL, "call to function '%s' failed", f->name);
}


int function_eval(smpl_t *smpl, smpl_insn_call_t *c)
{
    smpl_function_t *f    = c->f;
    smpl_value_t    *args = c->expr->call.args;
    int              narg = c->expr->call.narg;
    smpl_value_t     rv;
    int              r;

    if (function_call(smpl, f, narg, args, &rv) < 0)
        return -1;

    r = value_eval(smpl, &rv);
    value_reset(&rv);

    return r;
}
