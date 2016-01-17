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

#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/debug.h>

#include "jmpl/jmpl.h"
#include "jmpl/parser.h"


static int eval_block(jmpl_t *jmpl, iot_list_hook_t *l);

static int jmpl_printf(jmpl_t *jmpl, const char *fmt, ...)
{
    static int  size = 256;
    char       *buf;
    int         n, d, s;
    va_list     ap;

    va_start(ap, fmt);

 retry:
    buf = alloca(size);
    n = vsnprintf(buf, size, fmt, ap);

    if (n >= size - 1) {
        size *= 2;
        goto retry;
    }

    iot_debug("produced '%s'...", buf);

    va_end(ap);

    s = jmpl->size - jmpl->used;

    if (n + 1 > s) {
        if (jmpl->size == 0) {
            if (n + 1 < 1024)
                d = 1024;
            else
                d = 1024 + n + 1;
        }
        else {
            if (n + 1 < (int)jmpl->size)
                d = jmpl->size;
            else
                d = n + 1 + jmpl->size;
        }

        if (iot_reallocz(jmpl->buf, jmpl->size, jmpl->size + d) == NULL)
            return -1;

        jmpl->size += d;
    }

    strcpy(jmpl->buf + jmpl->used, buf);
    jmpl->used += n;

    return n;
}


static int eval_ifset(jmpl_t *jmpl, jmpl_ifset_t *jif)
{
    void *v;
    int   type;

    iot_debug("evaluating <ifset>...");

    type = symtab_resolve(jif->test, &v);

    if (type >= 0)
        eval_block(jmpl, &jif->tbranch);
    else
        eval_block(jmpl, &jif->fbranch);

    return 0;
}


static const char *value_string(jmpl_value_t *v, char *buf, size_t size)
{
    int   type;
    void *val;

    switch (v->type) {
    case JMPL_VALUE_EXPR:
        return NULL;

    case JMPL_VALUE_CONST:
        return v->s;

    case JMPL_VALUE_REF:
        type = symtab_resolve(v->r, &val);

        switch (type) {
        case JMPL_SYMVAL_STRING:
            return val;

        case JMPL_SYMVAL_INTEGER:
            if (snprintf(buf, size, "%d", *(int *)val) >= (int)size)
                return NULL;
            return buf;

        case JMPL_SYMVAL_JSON:
            switch (iot_json_get_type(val)) {
            case IOT_JSON_STRING:
                return iot_json_string_value(val);

            case IOT_JSON_INTEGER:
                if (snprintf(buf, size, "%d",
                             iot_json_integer_value(val)) >= (int)size)
                    return NULL;
                return buf;

            case IOT_JSON_DOUBLE:
                if (snprintf(buf, size, "%f",
                             iot_json_double_value(val)) >= (int)size)
                    return NULL;
                return buf;

            case IOT_JSON_BOOLEAN:
                return (iot_json_boolean_value(val) ? "true" : "false");

            default:
                return NULL;
            }
        }

    default:
        return NULL;
    }
}


static int eval_relop(jmpl_t *jmpl, jmpl_expr_t *expr)
{
    char lbuf[256], rbuf[256];
    const char *lv, *rv;

    iot_debug("evaluating relational operation...");

    lv = value_string(expr->lhs, lbuf, sizeof(lbuf));
    rv = value_string(expr->rhs, rbuf, sizeof(rbuf));

    if (lv == NULL || rv == NULL)
        return -1;

    switch (expr->type) {
    case JMPL_EXPR_EQ:
        if (lv == NULL || rv == NULL)
            return 0;
        else
            return (strcmp(lv, rv) == 0);
    case JMPL_EXPR_NEQ:
        if (lv == NULL || rv == NULL)
            return 0;
        else
            return (strcmp(lv, rv) != 0);
    default:
        return -1;
    }
}


static int eval_expr(jmpl_t *jmpl, jmpl_expr_t *expr);

static int eval_logop(jmpl_t *jmpl, jmpl_expr_t *expr)
{
    int r;

    iot_debug("evaluating logical operation...");

    if (expr->rhs->type != JMPL_VALUE_EXPR ||
        expr->lhs->type != JMPL_VALUE_EXPR)
        return -1;

    switch (expr->type) {
    case JMPL_EXPR_AND:
        r = eval_expr(jmpl, expr->lhs->e);

        if (r <= 0)
            return r;

        return eval_expr(jmpl, expr->rhs->e);

    case JMPL_EXPR_OR:
        r = eval_expr(jmpl, expr->lhs->e);

        if (r < 0 || r > 0)
            return r;

        return eval_expr(jmpl, expr->rhs->e);

    default:
        return -1;
    }
}


static int eval_expr(jmpl_t *jmpl, jmpl_expr_t *expr)
{
    int r;

    iot_debug("expr");

    switch (expr->type) {
    case JMPL_EXPR_AND:
    case JMPL_EXPR_OR:
        return eval_logop(jmpl, expr);

    case JMPL_EXPR_EQ:
    case JMPL_EXPR_NEQ:
        return eval_relop(jmpl, expr);

    case JMPL_EXPR_NOT:
        r = eval_expr(jmpl, expr->lhs->e);

        return (r < 0 ? -1 : !r);

    default:
        return -1;

    }

    return 0;
}


static int eval_ifelse(jmpl_t *jmpl, jmpl_if_t *jif)
{
    int t;

    iot_debug("evaluating <if-else>...");

    t = eval_expr(jmpl, jif->test);

    if (t < 0)
        return -1;

    if (t > 0)
        return eval_block(jmpl, &jif->tbranch);
    else
        return eval_block(jmpl, &jif->fbranch);

    return 0;
}


static int eval_foreach(jmpl_t *jmpl, jmpl_for_t *jfor)
{
    int type;
    void *val;
    iot_json_iter_t it;
    char *k;
    iot_json_t *v;
    int i, n;

    iot_debug("evaluating <foreach>...");

    type = symtab_resolve(jfor->in, &val);

    if (type < 0)
        return 0;

    switch (type) {
    case JMPL_SYMVAL_JSON:
        switch (iot_json_get_type(val)) {
        case IOT_JSON_OBJECT:
            iot_json_foreach_member(val, k, v, it) {
                if (jfor->key)
                    symtab_push(jfor->key->ids[0], JMPL_SYMVAL_STRING, k);
                if (jfor->val)
                    symtab_push(jfor->val->ids[0], JMPL_SYMVAL_JSON, v);
                eval_block(jmpl, &jfor->body);
                if (jfor->key)
                    symtab_pop(jfor->key->ids[0]);
                if (jfor->val)
                    symtab_pop(jfor->val->ids[0]);
            }
            return 0;

        case IOT_JSON_ARRAY:
            n = iot_json_array_length(val);
            for (i = 0; i < n; i++) {
                v = iot_json_array_get(val, i);
                if (jfor->key)
                    symtab_push(jfor->key->ids[0], JMPL_SYMVAL_INTEGER, &i);
                if (jfor->val)
                    symtab_push(jfor->val->ids[0], JMPL_SYMVAL_JSON, v);
                eval_block(jmpl, &jfor->body);
                if (jfor->key)
                    symtab_pop(jfor->key->ids[0]);
                if (jfor->val)
                    symtab_pop(jfor->val->ids[0]);
            }
            return 0;

        default:
            return -1;
        }
        break;

    default:
        return -1;
    }

    return 0;
}


static int eval_subst(jmpl_t *jmpl, jmpl_subst_t *subst)
{
    void *val;
    int   type, r;

    iot_debug("evaluating <subst>...");

    type = symtab_resolve(subst->ref, &val);

    switch (type) {
    case JMPL_SYMVAL_STRING:
        r = jmpl_printf(jmpl, "%s", (char *)val);
        break;
    case JMPL_SYMVAL_INTEGER:
        r = jmpl_printf(jmpl, "%d", *(int *)val);
        break;
    case JMPL_SYMVAL_JSON:
        switch (iot_json_get_type(val)) {
        case IOT_JSON_STRING:
            r = jmpl_printf(jmpl, "%s", iot_json_string_value(val));
            break;
        case IOT_JSON_INTEGER:
            r = jmpl_printf(jmpl, "%d", iot_json_integer_value(val));
            break;
        case IOT_JSON_BOOLEAN:
            r = jmpl_printf(jmpl, "%s",
                            iot_json_boolean_value(val) ? "true" : "false");
            break;
        case IOT_JSON_DOUBLE:
            r = jmpl_printf(jmpl, "%f", iot_json_double_value(val));
            break;
        default:
            r = -1;
            break;
        }
        break;

    default:
        r = -1;
        break;
    }

    return r < 0 ? -1 : 0;
}


static int eval_text(jmpl_t *jmpl, jmpl_text_t *text)
{
    char *p, *b, *e;
    int   l, c;

    iot_debug("evaluating <text '%s'>...", text->text);

    b = text->text;

    if (jmpl->mtab == NULL || (e = strstr(b, jmpl->mtab)) == NULL)
        return jmpl_printf(jmpl, "%s", b) < 0 ? -1 : 0;

    while (b && *b) {
        while (e != NULL && (e == text->text || e[-1] != '\n'))
            e = strstr(e + 1, jmpl->mtab);

        if (e == NULL)
            return jmpl_printf(jmpl, "%s", b) < 0 ? -1 : 0;

        l = e - b;

        if (jmpl_printf(jmpl, "%*.*s", l, l, b) < 0)
            return -1;

        b = e + jmpl->ltab;
        c = *b;

        while (*b && *b == c)
            b++;

        e = strstr(b, jmpl->mtab);
    }

    return 0;
}


static int eval_insn(jmpl_t *jmpl, jmpl_insn_t *insn)
{
    switch (insn->any.type) {
    case JMPL_OP_IFSET:   return eval_ifset(jmpl, &insn->ifset);
    case JMPL_OP_IF:      return eval_ifelse(jmpl, &insn->ifelse);
    case JMPL_OP_FOREACH: return eval_foreach(jmpl, &insn->foreach);
    case JMPL_OP_SUBST:   return eval_subst(jmpl, &insn->subst);
    case JMPL_OP_TEXT:    return eval_text(jmpl, &insn->text);
    default:
        break;
    }

    errno = EINVAL;
    return -1;
}


static int eval_block(jmpl_t *jmpl, iot_list_hook_t *l)
{
    iot_list_hook_t *p, *n;
    jmpl_insn_t     *insn;

    iot_list_foreach(l, p, n) {
        insn = iot_list_entry(p, typeof(*insn), any.hook);
        if (eval_insn(jmpl, insn) < 0)
            return -1;
    }

    return 0;
}


char *jmpl_eval(jmpl_t *jmpl, json_t *json)
{
    char *output;

    symtab_push(jmpl->data, JMPL_SYMVAL_JSON, json);

    if (eval_block(jmpl, &jmpl->hook) < 0)
        output = NULL;
    else
        output = jmpl->buf;

    symtab_pop(jmpl->data);

    return output;
}
