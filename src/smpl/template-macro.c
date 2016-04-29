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



static int arglist_parse(smpl_t *smpl, smpl_macro_t *m, smpl_token_t *end)
{
    smpl_token_t *arg  = end;
    int           narg =  0;
    int           varg = -1;
    char         *dot;

    if (parser_pull_token(smpl, SMPL_PARSE_ARGS, end) != '(')
        goto nolist;

    while (parser_pull_token(smpl, SMPL_PARSE_ARGS, arg) == SMPL_TOKEN_NAME) {
        smpl_debug("argument #%d: '%s'", narg++, arg->str);

        if (smpl_reallocz(m->args, m->narg, m->narg + 1) == NULL)
            goto nomem;

        if ((dot = strchr(arg->str, '.')) != NULL) {
            if (strcmp(dot, "...") != 0)
                goto invalid_vararg;

            if (varg >= 0)
                goto multiple_varargs;

            varg = m->narg;
            *dot = '\0';
        }
        else
            if (varg >= 0)
                goto nonlast_vararg;

        m->args[m->narg] = symtbl_add(smpl, arg->str, SMPL_SYMBOL_ARG);

        if (m->args[m->narg] < 0)
            goto arg_failed;


        m->narg++;

        switch (parser_pull_token(smpl, SMPL_PARSE_ARGS, arg)) {
        case ',':
            break;
        case ')':
            goto out;
        default:
            goto invalid_arglist;
        }

    }

 out:
    if (end->type != ')')
        goto invalid_arglist;

    m->varg = varg >= 0 ? 1 : 0;

    return 0;

 nolist:
    parser_push_token(smpl, end);
    m->narg = -1;
    return 0;

 nomem:
    return -1;

 arg_failed:
    smpl_fail(-1, smpl, errno, "failed to parse/add macro argument");

 invalid_arglist:
    smpl_fail(-1, smpl, EINVAL, "invalid argument list");

 invalid_vararg:
    smpl_fail(-1, smpl, EINVAL, "invalid varargish macro argument '%s'",
              arg->str);

 multiple_varargs:
    smpl_fail(-1, smpl, EINVAL, "multiple varargs in argument list:(#%d, #%d)",
              m->varg, narg);

 nonlast_vararg:
    smpl_fail(-1, smpl, EINVAL, "macro vararg must be last in argument list");
}


int macro_parse(smpl_t *smpl)
{
    smpl_macro_t *m;
    smpl_token_t  name, vref, end;
    int           flags;

    m = smpl_alloct(typeof(*m));

    if (m == NULL)
        goto nomem;

    smpl_list_init(&m->hook);
    smpl_list_init(&m->body);

    if (parser_pull_token(smpl, SMPL_PARSE_NAME, &name) < 0)
        goto name_failed;

    smpl_debug("parsing macro definition of '%s'", name.str);

    if (arglist_parse(smpl, m, &end) < 0)
        goto failed;

    if (m->narg == -1 && end.type == SMPL_TOKEN_VARREF) {
        /* no arglist, so last token was pushed back... need to pull it */
        if (parser_pull_token(smpl, SMPL_PARSE_ARGS, &vref) < 0 ||
            vref.type != SMPL_TOKEN_VARREF)
            goto failed;
        if (varref_add_alias(smpl, name.str, vref.str) < 0)
            goto failed;
        else
            return 0;
    }

    m->name = symtbl_add(smpl, name.str, SMPL_SYMBOL_MACRO);

    if (m->name < 0)
        goto name_failed;

    flags = SMPL_SKIP_WHITESPACE|SMPL_BLOCK_DOEND;
    if (block_parse(smpl, flags, &m->body, NULL) != SMPL_TOKEN_END)
        goto failed;

    smpl_list_append(&smpl->macros, &m->hook);

    return 0;

 nomem:
    return -1;

 name_failed:
    smpl_free(m);
    smpl_fail(-1, smpl, errno, "failed to parse/add macro name");

 failed:
    macro_free(m);
    smpl_fail(-1, smpl, errno, "failed to parse body of macro '%s'", name.str);
}


void macro_free(smpl_macro_t *m)
{
    if (m == NULL)
        return;

    smpl_list_delete(&m->hook);
    block_free(&m->body);

    smpl_free(m);
}


void macro_purge(smpl_list_t *macros)
{
    smpl_macro_t *m;
    smpl_list_t  *p, *n;

    if (macros == NULL)
        return;

    smpl_list_foreach(macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);
        macro_free(m);
    }
}


int macro_parse_ref(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_call_t *c;
    char             *name;
    smpl_expr_t      *e;
    smpl_token_t      end;

    c = smpl_alloct(typeof(*c));

    if (c == NULL)
        goto nomem;

    smpl_list_init(&c->hook);
    c->type = SMPL_INSN_MACROREF;

    e    = NULL;
    name = t->str;
    c->m = t->m;

    if (c->m->narg >= 0) {
        /* kludge: push back name, let expr_parse do everything for us */
        if (parser_push_token(smpl, t) < 0)
            goto failed;

        c->expr = e = expr_parse(smpl, &end);

        if (e == NULL || e->type != SMPL_VALUE_MACROREF)
            goto invalid_expr;

        if (e->call.m != c->m)
            goto invalid_expr;
    }

    if (c->m->narg >= 0)
        parser_skip_newline(smpl);

    smpl_list_append(block, &c->hook);

    return 0;

 nomem:
 failed:
    return -1;

 invalid_expr:
    macro_free_ref((smpl_insn_t *)c);
    smpl_fail(-1, smpl, EINVAL, "failed to parse reference to macro '%s'", name);
}


void macro_free_ref(smpl_insn_t *insn)
{
    smpl_insn_call_t *c = &insn->call;

    if (c == NULL)
        return;

    smpl_list_delete(&c->hook);
    expr_free(c->expr);
    smpl_free(c);
}


int macro_call(smpl_t *smpl, smpl_macro_t *m, int narg, smpl_value_t *args,
               smpl_buffer_t *obuf)
{
    smpl_expr_t  *a;
    smpl_value_t  v;
    int           i, r;
    void         *vp;
    int           type;

    if (obuf == NULL)
        obuf = smpl->result;

    a = narg > 0 ? smpl_list_entry(args->hook.prev, typeof(*args), hook) : NULL;

    for (i = 0; i <= m->narg - 1; i++) {
        if (i == m->narg - 1 && m->varg) {
            type = v.type = SMPL_VALUE_ARGLIST;
            v.call.m    = m;
            v.call.narg = narg - (m->narg - 1);
            v.call.args = a;
            vp = &v;
        }
        else {
            if (expr_eval(smpl, a, &v) < 0)
                goto invalid_arg;

            switch (v.type) {
            case SMPL_VALUE_STRING:  vp = v.str;  break;
            case SMPL_VALUE_INTEGER: vp = &v.i32; break;
            case SMPL_VALUE_DOUBLE:  vp = &v.dbl; break;
            case SMPL_VALUE_OBJECT:
            case SMPL_VALUE_ARRAY:   vp = v.json; break;
            case SMPL_VALUE_UNSET:   vp = NULL;   break;
            default:
                goto invalid_arg;
            }

            type = v.type | (v.dynamic ? SMPL_VALUE_DYNAMIC : 0);
        }

        if (symtbl_push(smpl, m->args[i], type, vp) < 0)
            goto push_failed;

        if (!(i == m->narg - 1 && m->varg))
            value_reset(&v);

        a = smpl_list_entry(a->hook.prev, typeof(*a), hook);
    }

    r = block_eval(smpl, &m->body, obuf);

    for (i = 0; i < m->narg; i++) {
        symtbl_pop(smpl, m->args[i]);
    }

    if (r < 0)
        goto failed;

    return 0;

 invalid_arg:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate macro argument #%d", i + 1);

 push_failed:
    smpl_fail(-1, smpl, EINVAL, "failed to push macro argument #%d", i + 1);

 failed:
    return -1;
}


int macro_eval(smpl_t *smpl, smpl_insn_call_t *c, smpl_buffer_t *obuf)
{
    smpl_value_t *args;
    int           narg;

    if (c->expr) {
        narg = c->expr->call.narg;
        args = c->expr->call.args;
    }
    else {
        narg = 0;
        args = NULL;
    }

    return macro_call(smpl, c->m, narg, args, obuf);

#if 0
    smpl_macro_t *m;
    int           i, r;
    smpl_expr_t  *a;
    smpl_value_t  v;
    void         *vp;
    int           type;

    if (obuf == NULL)
        obuf = smpl->result;

    m = c->m;
    a = c->expr ? c->expr->call.args : NULL;

    for (i = m->narg - 1; i >= 0; i--) {
        if (expr_eval(smpl, a, &v) < 0)
            goto invalid_arg;

        switch (v.type) {
        case SMPL_VALUE_STRING:  vp = v.str;  break;
        case SMPL_VALUE_INTEGER: vp = &v.i32; break;
        case SMPL_VALUE_DOUBLE:  vp = &v.dbl; break;
        case SMPL_VALUE_OBJECT:
        case SMPL_VALUE_ARRAY:   vp = v.json; break;
        case SMPL_VALUE_UNSET:   vp = NULL;   break;
        default:
            goto invalid_arg;
        }

        type = v.type | (v.dynamic ? SMPL_VALUE_DYNAMIC : 0);

        if (symtbl_push(smpl, m->args[i], type, vp) < 0)
            goto push_failed;

        value_reset(&v);

        a = smpl_list_entry(a->hook.next, typeof(*a), hook);
    }

    r = block_eval(smpl, &m->body, obuf);

    for (i = 0; i < m->narg; i++) {
        symtbl_pop(smpl, m->args[i]);
    }

    if (r < 0)
        goto failed;

    return 0;

 invalid_arg:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate macro argument #%d", i + 1);

 push_failed:
    smpl_fail(-1, smpl, EINVAL, "failed to push macro argument #%d", i + 1);

 failed:
    return -1;
#endif
}


static char *arglist_dump(smpl_t *smpl, char *buf, size_t size,
                          smpl_sym_t *args, int narg)
{
    char *p, *t;
    int   n, l, i;

    if (narg < 0)
        goto nolist;

    if (narg == 0)
        goto noargs;

    p = buf;
    l = (int)size;

    *p++ = '(';
    l--;

    for (i = 0, t = ""; i < narg; i++, t = ", ") {
        n = snprintf(p, l, "%s%s", t, symtbl_get(smpl, args[i]));

        if (n >= l - 2)
            return "<arglist buffer overflow>";

        p += n;
        l -= n;
    }

    *p++ = ')';
    *p   = '\0';

    return buf;

 nolist:
    *buf = '\0';
    return buf;

 noargs:
    buf[0] = '(';
    buf[1] = ')';
    buf[2] = '\0';
    return buf;
}


void macro_dump(smpl_t *smpl, int fd, smpl_macro_t *m)
{
    char args[512];

    dprintf(fd, "<macro '%s'%s>\n", symtbl_get(smpl, m->name),
            arglist_dump(smpl, args, sizeof(args), m->args, m->narg));
    block_dump(smpl, fd, &m->body, 1);
}


void macro_dump_ref(smpl_t *smpl, int fd, smpl_insn_call_t *c, int indent)
{
    char buf[1024];

    expr_print(smpl, c->expr, buf, sizeof(buf));
    dprintf(fd, SMPL_INDENT_FMT"<macro call>%s\n",
            SMPL_INDENT_ARG(indent), buf);
}


smpl_macro_t *macro_find(smpl_t *smpl, smpl_sym_t sym)
{
    smpl_list_t  *p, *n;
    smpl_macro_t *m;

    if (SMPL_SYMBOL_TAG(sym) != SMPL_SYMBOL_MACRO)
        goto notfound;

    smpl_list_foreach(&smpl->macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);

        if (m->name == sym)
            return m;
    }

 notfound:
    return NULL;
}


smpl_macro_t *macro_by_name(smpl_t *smpl, const char *name)
{
    smpl_list_t  *p, *n;
    smpl_macro_t *m;

    smpl_list_foreach(&smpl->macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);

        if (!strcmp(name, symtbl_get(smpl, m->name)))
            return m;
    }

    return NULL;
}
