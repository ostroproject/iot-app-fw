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

#define LEFT   -1
#define RIGHT  +1
#define UNSPEC  0


static inline int isop(int type)
{
    switch (type) {
    case SMPL_TOKEN_OR:
    case SMPL_TOKEN_AND:
    case SMPL_TOKEN_EQUAL:
    case SMPL_TOKEN_NOTEQ:
    case SMPL_TOKEN_NOT:
    case SMPL_TOKEN_IS:
        return 1;
    default:
        return  0;
    }
}


static inline int assoc(smpl_token_t *t)
{
    switch (t->type) {
    case SMPL_TOKEN_OR:
    case SMPL_TOKEN_AND:
    case SMPL_TOKEN_EQUAL:
    case SMPL_TOKEN_NOTEQ:
        return LEFT;

    case SMPL_TOKEN_IS:
    case SMPL_TOKEN_NOT:
        return RIGHT;

    default:
        return UNSPEC;
    }
}


static inline int prec(smpl_token_t *t)
{
    switch (t->type) {
    case SMPL_TOKEN_OR:
        return 1;

    case SMPL_TOKEN_AND:
        return 2;

    case SMPL_TOKEN_EQUAL:
    case SMPL_TOKEN_NOTEQ:
        return 3;

    case SMPL_TOKEN_IS:
    case SMPL_TOKEN_NOT:
        return 4;

    default:
        return -1;
    }
}


static smpl_token_t *token_copy(smpl_token_t *t)
{
    smpl_token_t *c;

    if (t == NULL || (c = smpl_alloct(typeof(*c))) == NULL)
        return NULL;

    *c = *t;
    smpl_list_init(&c->hook);

    return c;
}


static void token_free(smpl_token_t *t)
{
    if (t == NULL)
        return;

    smpl_list_delete(&t->hook);
    smpl_free(t);
}


static smpl_token_t *token_push(smpl_list_t *q, smpl_token_t *t)
{
    if (t != NULL) {
        smpl_list_init(&t->hook);
        smpl_list_append(q, &t->hook);
    }

    return t;
}


static int token_peek(smpl_list_t *q, smpl_token_t **tp)
{
    smpl_token_t *t;

    if (smpl_list_empty(q))
        t = NULL;
    else
        t = smpl_list_entry(q->prev, typeof(*t), hook);

    *tp = t;

    return t ? t->type : SMPL_TOKEN_EOF;
}


static int token_pop(smpl_list_t *q, smpl_token_t **tp)
{
    if (token_peek(q, tp) == SMPL_TOKEN_EOF)
        return SMPL_TOKEN_EOF;

    smpl_list_delete(&(*tp)->hook);
    smpl_list_init(&(*tp)->hook);

    return (*tp)->type;
}


static void token_purgeq(smpl_list_t *q)
{
    smpl_token_t *t;
    smpl_list_t  *p, *n;

    smpl_list_foreach(q, p, n) {
        t = smpl_list_entry(p, typeof(*t), hook);
        smpl_list_delete(&t->hook);
        smpl_free(t);
    }
}


static inline void token_unque(smpl_token_t *t)
{
    smpl_list_delete(&t->hook);
}


#if 0
static void value_dumpq(smpl_t *smpl, smpl_list_t *q)
{
    smpl_value_t *v;
    smpl_list_t  *p, *n;

    smpl_debug("RPN queue:");
    smpl_list_foreach(q, p, n) {
        v = smpl_list_entry(p, typeof(*v), hook);

        switch (v->type) {
        case SMPL_VALUE_VARREF: {
            char buf[256];

            smpl_debug("  <VARREF %s>",
                       varref_print(smpl, v->ref, buf, sizeof(buf)));
        }
            break;

        case SMPL_VALUE_STRING:
            smpl_debug("  <STRING '%s'>", v->str);
            break;

        case SMPL_VALUE_INTEGER:
            smpl_debug("  <INTEGER %d>", v->i32);
            break;

        case SMPL_VALUE_DOUBLE:
            smpl_debug("  <DOUBLE %.4f", v->dbl);
            break;

        case SMPL_VALUE_AND:   smpl_debug("  &&"); break;
        case SMPL_VALUE_OR:    smpl_debug("  ||"); break;
        case SMPL_VALUE_NOT:   smpl_debug("  !" ); break;
        case SMPL_VALUE_IS:    smpl_debug("  ?" ); break;
        case SMPL_VALUE_EQUAL: smpl_debug("  =="); break;
        case SMPL_VALUE_NOTEQ: smpl_debug("  !="); break;
        default:
            smpl_debug("  <unknown value>");
            break;
        }
    }
}
#endif


static smpl_value_t *value_push(smpl_t *smpl, smpl_list_t *q, smpl_token_t *t)
{
    smpl_value_t *v, *a1, *a2;
    smpl_list_t  *p1, *p2;
    smpl_value_t *arg;
    smpl_list_t  *p, *n;
    int           narg;

    smpl_debug("VALUE %s (%s)", token_name(t->type), t->str);

    v = smpl_alloct(typeof(*v));

    if (v == NULL)
        goto nomem;

    smpl_list_init(&v->hook);

    switch (t->type) {
    case SMPL_TOKEN_VARREF:
        v->type = SMPL_VALUE_VARREF;
        v->ref  = varref_parse(smpl, t->str, t->path, t->line);

        if (v->ref == NULL)
            goto invalid_ref;
        break;

    case SMPL_TOKEN_STRING:
    case SMPL_TOKEN_INTEGER:
    case SMPL_TOKEN_DOUBLE:
        v->type = SMPL_VALUE_STRING + (t->type - SMPL_TOKEN_STRING);

        if (v->type == SMPL_VALUE_STRING)
            v->str = t->str;
        else
            v->dbl = t->dbl;
        break;

    case SMPL_TOKEN_AND:
    case SMPL_TOKEN_OR:
    case SMPL_TOKEN_EQUAL:
    case SMPL_TOKEN_NOTEQ:
        v->type = SMPL_VALUE_AND + (t->type - SMPL_TOKEN_AND);
        p1 = q->prev;
        p2 = p1->prev;

        if (p1 == q || p2 == q)
            goto invalid_rpnq;

        a1 = smpl_list_entry(p1, typeof(*a1), hook);
        a2 = smpl_list_entry(p2, typeof(*a2), hook);
        smpl_list_delete(&a1->hook);
        smpl_list_delete(&a2->hook);
        v->expr.arg1 = a2;
        v->expr.arg2 = a1;
        break;

    case SMPL_TOKEN_NOT:
    case SMPL_TOKEN_IS:
        v->type = SMPL_VALUE_NOT + (t->type - SMPL_TOKEN_NOT);
        p1 = q->prev;

        if (p1 == q)
            goto invalid_rpnq;

        a1 = smpl_list_entry(p1, typeof(*a1), hook);
        smpl_list_delete(&a1->hook);
        v->expr.arg1 = a1;
        break;

        case '(':
            v->type = SMPL_VALUE_ARGLIST;
            break;

    case SMPL_TOKEN_MACROREF:
        v->type   = SMPL_VALUE_MACROREF;
        v->call.m = t->m;

        narg = 0;
        smpl_list_foreach_back(q, p, n) {
            arg = smpl_list_entry(p, typeof(*arg), hook);

            smpl_list_delete(&arg->hook);

            if (arg->type == SMPL_VALUE_ARGLIST) {
                smpl_free(arg);
                break;
            }

            if (v->call.args == NULL)
                v->call.args = arg;
            else
                smpl_list_append(&v->call.args->hook, &arg->hook);

            narg++;
        }

        if (v->call.m->narg >= 0 && narg != v->call.m->narg) {
            smpl_error("macro '%s' called with %d args, declared with %d.",
                      t->str, narg, v->call.m->narg);
            goto narg_mismatch;
        }
        v->call.narg = narg;
        break;

    case SMPL_TOKEN_FUNCREF:
        v->type   = SMPL_VALUE_FUNCREF;
        v->call.f = t->f;

        narg = 0;
        smpl_list_foreach_back(q, p, n) {
            arg = smpl_list_entry(p, typeof(*arg), hook);

            smpl_list_delete(&arg->hook);

            if (arg->type == SMPL_VALUE_ARGLIST) {
                smpl_free(arg);
                break;
            }

            if (v->call.args == NULL)
                v->call.args = arg;
            else
                smpl_list_append(&v->call.args->hook, &arg->hook);

            narg++;
        }
        v->call.narg = narg;
        break;

    default:
        goto invalid_token;
    }

    smpl_list_append(q, &v->hook);

    return v;

 nomem:
    return NULL;

 invalid_ref:
    smpl_free(v);
    smpl_return_error(NULL, smpl, EINVAL, t->path, t->line,
                      "invalid variable reference '%s'", t->str);

 invalid_token:
    smpl_free(v);
    smpl_return_error(NULL, smpl, EINVAL, t->path, t->line,
                      "invalid token type 0x%x in expression", t->type);

 narg_mismatch:
    smpl_free(v);
    smpl_return_error(NULL, smpl, EINVAL, t->path, t->line,
                      "macro '%s' called with incorrect number of arguments",
                      t->str);

 invalid_rpnq:
    smpl_free(v);
    smpl_return_error(NULL, smpl, EINVAL, t->path, t->line, "invalid RPN queue");
}


static void value_purgeq(smpl_list_t *q)
{
    smpl_value_t *v;
    smpl_list_t  *p, *n;

    smpl_list_foreach(q, p, n) {
        v = smpl_list_entry(p, typeof(*v), hook);
        smpl_list_delete(&v->hook);
        if (v->type == SMPL_VALUE_VARREF)
            varref_free(v->ref);
        smpl_free(v);
    }
}


static int parse_rpn(smpl_t *smpl, smpl_list_t *valq, smpl_token_t *end)
{
    smpl_list_t   tknq;
    smpl_token_t *tkn, *t;
    int           type, nparen;
    smpl_token_t  paren = {
        .type = SMPL_TOKEN_PAREN_OPEN,
        .str  = "(",
        .path = "<internal arglist terminator>",
        .m    = NULL,
    };

    smpl_list_init(&tknq);
    smpl_list_init( valq);

    /*
     * Notes:
     *
     * This is a rough approximation of Dijkstra's Shunting-yard
     * algorithm. It parses an expression in infix notation into
     * RPN.
     */

    nparen = 0;
    tkn = end;

    while (1) {
        parser_pull_token(smpl, SMPL_PARSE_EXPR, tkn);

        smpl_debug("token %s ('%s')", token_name(tkn->type), tkn->str);
        smpl_debug("* nparen = %d", nparen);

        switch (tkn->type) {
        case SMPL_TOKEN_VARREF:
        case SMPL_TOKEN_TEXT:
        case SMPL_TOKEN_STRING:
        case SMPL_TOKEN_INTEGER:
        case SMPL_TOKEN_DOUBLE:
            if (!value_push(smpl, valq, tkn))
                goto push_failed;
            break;

        case SMPL_TOKEN_MACROREF:
        case SMPL_TOKEN_FUNCREF:
            if (!token_push(&tknq, token_copy(tkn)))
                goto push_failed;
            if (!value_push(smpl, valq, token_copy(&paren)))
                goto push_failed;
            break;

        case ',':
            while ((type = token_peek(&tknq, &t)) != '(') {
                smpl_debug("poke %s ('%s')", token_name(t->type), t->str);

                if (type == SMPL_TOKEN_EOF || type == SMPL_TOKEN_ERROR)
                    goto invalid_arglist;

                if (!isop(t->type)) {
                    smpl_warn("expecting operator, got token %s",
                              token_name(t->type));
                }

                if (!value_push(smpl, valq, t))
                    goto push_failed;
                token_free(t);
            }
            break;

        case SMPL_TOKEN_NOT:
        case SMPL_TOKEN_IS:
        case SMPL_TOKEN_AND:
        case SMPL_TOKEN_OR:
        case SMPL_TOKEN_EQUAL:
        case SMPL_TOKEN_NOTEQ:
            while (isop(token_peek(&tknq, &t)) &&
                   ((assoc(tkn) == LEFT  && prec(tkn) <= prec(t)) ||
                    (assoc(tkn) == RIGHT && prec(tkn) <  prec(t)))) {
                if (!value_push(smpl, valq, t))
                    goto push_failed;
                token_free(t);
            }
            if (!token_push(&tknq, token_copy(tkn)))
                goto push_failed;
            break;

        case '(':
            if (!token_push(&tknq, token_copy(tkn)))
                goto push_failed;
            nparen++;
            break;

        case ')':
            while ((type = token_peek(&tknq, &t)) != '(') {
                if (type == SMPL_TOKEN_EOF)
                    goto mismatched_paren;
                else {
                    if (!value_push(smpl, valq, t))
                        goto push_failed;
                    token_free(t);
                }
            }
            token_free(t);

            type = token_peek(&tknq, &t);
            if (type == SMPL_TOKEN_MACROREF || type == SMPL_TOKEN_FUNCREF) {
                if (!value_push(smpl, valq, t))
                    goto push_failed;
                token_free(t);
            }

            nparen--;
            if (!nparen)
                goto check_tokenq;
            break;

        case SMPL_TOKEN_ERROR:
            goto parse_error;

        default:
            if (nparen > 0)
                goto invalid_argument;
            parser_push_token(smpl, tkn); /* push back terminating non-')' */
            goto check_tokenq;
        }
    }

 check_tokenq:
    while (token_pop(&tknq, &t) != SMPL_TOKEN_EOF) {
        if (t->type == '(' || t->type == ')')
            goto mismatched_paren;
        else {
            if (!value_push(smpl, valq, t))
                goto push_failed;
            token_free(t);
        }
    }

    token_purgeq(&tknq);

    return 0;

 push_failed:
    token_purgeq(&tknq);
    value_purgeq( valq);

    end->type = SMPL_TOKEN_ERROR;
    end->str  = "<parse error>";
    smpl_fail(-1, smpl, errno, "failed to parse expression");

 mismatched_paren:
    token_purgeq(&tknq);
    value_purgeq( valq);

    end->type = SMPL_TOKEN_ERROR;
    end->str  = "<parse error>";
    smpl_fail(-1, smpl, EINVAL, "unbalanced parenthesis");

 invalid_arglist:
    token_purgeq(&tknq);
    value_purgeq( valq);

    end->type = SMPL_TOKEN_ERROR;
    end->str  = "<parse error>";
    smpl_fail(-1, smpl, EINVAL, "misplaced comma or parenthesis in arglist");

 invalid_argument:
    token_purgeq(&tknq);
    value_purgeq( valq);

    end->type = SMPL_TOKEN_ERROR;
    end->str  = "<parse error>";
    smpl_fail(-1, smpl, EINVAL, "invalid argument in arglist");

 parse_error:
    token_purgeq(&tknq);
    value_purgeq( valq);

    end->type = SMPL_TOKEN_ERROR;
    end->str = "<parse error>";
    smpl_fail(-1, smpl, EINVAL, "failed to parse expression");
}


smpl_expr_t *expr_parse(smpl_t *smpl, smpl_token_t *end)
{
    smpl_value_t *v;
    smpl_list_t   rpnq;
    smpl_token_t  endt;

    if (end == NULL)
        end = &endt;

    if (parse_rpn(smpl, &rpnq, end) < 0)
        goto parse_error;

    v = NULL;

    if (rpnq.next == &rpnq || rpnq.next->next != &rpnq)
        goto invalid_rpnq;

    v = smpl_list_entry(rpnq.next, typeof(*v), hook);
    smpl_list_delete(&v->hook);

    return v;

 invalid_rpnq:
    smpl_fail(NULL, smpl, EINVAL, "invalid RPN queue");

 parse_error:
    return NULL;
}


smpl_expr_t *expr_first_parse(smpl_t *smpl, smpl_token_t *t, smpl_token_t *name)
{
    smpl_expr_t *expr;
    char        *p;

    p = t->str;

    if (*p == '!' || *p == '?')
        p++;

    expr = smpl_alloct(typeof(*expr));

    if (expr == NULL)
        goto nomem;

    smpl_list_init(&expr->hook);
    expr->type = !strcmp(p, "first") ? SMPL_VALUE_FIRST : SMPL_VALUE_LAST;
    expr->sym  = symtbl_add(smpl, name->str, SMPL_SYMBOL_LOOP);

    if (expr->sym < 0)
        goto invalid_name;

    return expr;

 nomem:
    return NULL;

 invalid_name:
    smpl_free(expr);
    smpl_fail(NULL, smpl, EINVAL, "invalid loop variable name '%s'", name->str);
}


smpl_expr_t *expr_trail_parse(smpl_t *smpl, smpl_token_t *t)
{
    smpl_expr_t *expr;
    char        *p;

    SMPL_UNUSED(smpl);

    p = t->str;

    if (*p == '!')
        p++;

    p += sizeof("trail:") - 1;

    expr = smpl_alloct(typeof(*expr));

    smpl_list_init(&expr->hook);
    expr->type = SMPL_VALUE_TRAIL;
    expr->str  = smpl_strdup(p);

    if (expr->str == NULL)
        goto nomem;

    return expr;

 nomem:
    smpl_free(expr);
    return NULL;
}


void expr_free(smpl_expr_t *expr)
{
    smpl_list_t  *n;
    smpl_value_t *a;

    if (expr == NULL)
        return;

    switch (expr->type) {
    case SMPL_VALUE_AND:
    case SMPL_VALUE_OR:
    case SMPL_VALUE_EQUAL:
    case SMPL_VALUE_NOTEQ:
        expr_free(expr->expr.arg2);
    case SMPL_VALUE_NOT:
    case SMPL_VALUE_IS:
        expr_free(expr->expr.arg1);
        break;

    case SMPL_VALUE_VARREF:
        varref_free(expr->ref);
        break;

    case SMPL_VALUE_MACROREF:
    case SMPL_VALUE_FUNCREF:
        a = expr->call.args;

        while (a != NULL) {
            if (smpl_list_empty(&a->hook))
                n = NULL;
            else
                n = a->hook.next;

            expr_free(a);

            a = n ? smpl_list_entry(n, typeof(*a), hook) : NULL;
        }
        break;

    case SMPL_VALUE_TRAIL:
        smpl_free(expr->str);
        break;

    default:
        break;
    }

    smpl_list_delete(&expr->hook);

    smpl_free(expr);
}


int expr_print(smpl_t *smpl, smpl_expr_t *e, char *buf, size_t size)
{
    char        *op, arg1[512], arg2[512], *p;
    smpl_expr_t *a;
    int          n, l;

    if (e == NULL)
        goto null_expr;

    switch (e->type) {
    case SMPL_VALUE_VARREF:
        return snprintf(buf, size, "{%s}", varref_print(smpl, e->ref,
                                                        arg1, sizeof(arg1)));
    case SMPL_VALUE_STRING:
        return snprintf(buf, size, "'%s'", e->str);
    case SMPL_VALUE_INTEGER:
        return snprintf(buf, size, "%d", e->i32);
    case SMPL_VALUE_DOUBLE:
        return snprintf(buf, size, "%.4f", e->dbl);

    case SMPL_VALUE_NOT: op = "!"; goto print_unary;
    case SMPL_VALUE_IS:  op = "?";
    print_unary:
        expr_print(smpl, e->expr.arg1, arg1, sizeof(arg1));
        return snprintf(buf, size, "%s(%s)", op, arg1);

    case SMPL_VALUE_AND:   op = "&&"; goto print;
    case SMPL_VALUE_OR:    op = "||"; goto print;
    case SMPL_VALUE_EQUAL: op = "=="; goto print;
    case SMPL_VALUE_NOTEQ: op = "!=";
    print:
        expr_print(smpl, e->expr.arg1, arg1, sizeof(arg1));
        expr_print(smpl, e->expr.arg2, arg2, sizeof(arg2));
        return snprintf(buf, size, "(%s %s %s)", arg1, op, arg2);

    case SMPL_VALUE_TRAIL:
        return snprintf(buf, size, "trail:%s", e->str);

    case SMPL_VALUE_FIRST:
    case SMPL_VALUE_LAST:
        return snprintf(buf, size, "{%s} %s",
                        e->type == SMPL_VALUE_FIRST ? "first" : "last",
                        symtbl_get(smpl, e->sym));

    case SMPL_VALUE_MACROREF:
    case SMPL_VALUE_FUNCREF:
        p = buf;
        l = (int)size;
        if (e->type == SMPL_VALUE_MACROREF)
            n = snprintf(p, l, "{%s}(", symtbl_get(smpl, e->call.m->name));
        else
            n = snprintf(p, l, "{%s}(", e->call.f->name);

        if (n >= l)
            goto overflow;

        p += n;
        l -= n;

        a = e->call.args;
        while (a != NULL) {
            if (l <= 2)
                goto overflow;

            if (a != e->call.args) {
                *p++ = ',';
                *p++ = ' ';
                l -= 2;
            }

            n = expr_print(smpl, a, p, l);

            if (n >= l)
                goto overflow;

            p += n;
            l -= n;

            if (a->hook.next != &e->call.args->hook)
                a = smpl_list_entry(a->hook.next, typeof(*a), hook);
            else
                a = NULL;
        }

        if (l <= 2)
            goto overflow;

        *p++ = ')';
        *p   = '\0';

        return p - buf;

    default:
        break;
    }

    return snprintf(buf, size, "<unknown expression (type 0x%x)>", e->type);

 null_expr:
    return snprintf(buf, size, "<null expression>");

 overflow:
    return snprintf(buf, size, "<expression buffer overflow>");
}


static inline int logical_value(smpl_t *smpl, int type,
                                smpl_value_t *arg1, smpl_value_t *arg2)
{
    smpl_value_t v1, v2;
    int          val1, val2;

    if (expr_eval(smpl, arg1, &v1) < 0)
        goto fail;

    switch (v1.type) {
    case SMPL_VALUE_STRING:  val1 = v1.str && v1.str[0]; break;
    case SMPL_VALUE_INTEGER: val1 = v1.i32 != 0;          break;
    case SMPL_VALUE_DOUBLE:  val1 = v1.i32 != 0;          break;
    case SMPL_VALUE_OBJECT:
        val1 = smpl_json_object_length(v1.json) != 0;
        break;
    case SMPL_VALUE_ARRAY:
        val1 = smpl_json_array_length(v1.json) != 0;
        break;
    default:
        val1 = 0;
        break;
    }

    value_reset(&v1);

    if ((type == SMPL_VALUE_AND && !val1) || (type == SMPL_VALUE_OR && val1))
        return val1;

    if (expr_eval(smpl, arg2, &v2) < 0)
        goto fail;

    switch (v2.type) {
    case SMPL_VALUE_STRING:  val2 = v2.str && v2.str[0]; break;
    case SMPL_VALUE_INTEGER: val2 = v2.i32 != 0;         break;
    case SMPL_VALUE_DOUBLE:  val2 = v2.i32 != 0;         break;
    case SMPL_VALUE_OBJECT:
        val2 = smpl_json_object_length(v2.json) != 0;
        break;
    case SMPL_VALUE_ARRAY:
        val2 = smpl_json_array_length(v2.json) != 0;
        break;
    default:
        val2 = 0;
    }

    value_reset(&v2);

    return type == SMPL_VALUE_AND ? (val1 && val2) : (val1 || val2);

 fail:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate expression");
}


static inline int comparison_value(smpl_value_t *v1, smpl_value_t *v2, int type)
{
    int eq;

    if (v1->type != v2->type)
        return 0;

    switch (v1->type) {
    case SMPL_VALUE_STRING:  eq = !strcmp(v1->str, v2->str); break;
    case SMPL_VALUE_INTEGER: eq = v1->i32 == v2->i32;        break;
    case SMPL_VALUE_DOUBLE:  eq = v1->dbl == v2->dbl;        break;
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:   eq = v1->json == v2->json;      break;
    default:                 eq = 0;
    }

    return type == SMPL_VALUE_EQUAL ? eq : !eq;
}


int expr_compare_values(smpl_value_t *v1, smpl_value_t *v2)
{
    return comparison_value(v1, v2, SMPL_VALUE_EQUAL);
}


static inline int negative_value(smpl_value_t *v)
{
    int neg;

    switch (v->type) {
    case SMPL_VALUE_STRING:  neg = !v->str || !v->str[0];             break;
    case SMPL_VALUE_INTEGER: neg = !v->i32;                           break;
    case SMPL_VALUE_DOUBLE:  neg =  v->dbl == 0.0;                    break;
    case SMPL_VALUE_OBJECT:  neg = !smpl_json_object_length(v->json); break;
    case SMPL_VALUE_ARRAY:   neg = !smpl_json_array_length(v->json);  break;
    case SMPL_VALUE_UNSET:   neg = 1;                                 break;
    default:
        neg = 0;
        break;
    }

    return neg;
}


int expr_eval(smpl_t *smpl, smpl_expr_t *e, smpl_value_t *v)
{
    smpl_value_t   arg1, arg2;
    smpl_buffer_t *obuf;
    int32_t        lv;

    switch (e->type) {
    case SMPL_VALUE_UNSET:
        return value_set(v, SMPL_VALUE_UNSET)->type;

    case SMPL_VALUE_VARREF:
        if (symtbl_resolve(smpl, e->ref, v) < 0)
            goto invalid_ref;
        return v->type;

    case SMPL_VALUE_STRING:
    case SMPL_VALUE_INTEGER:
    case SMPL_VALUE_DOUBLE:
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        return value_copy(v, e)->type;

    case SMPL_VALUE_AND:
    case SMPL_VALUE_OR:
        lv = logical_value(smpl, e->type, e->expr.arg1, e->expr.arg2) ? 1 : 0;

        if (lv < 0)
            return value_set(v, SMPL_VALUE_UNKNOWN)->type;
        else
            return value_set(v, SMPL_VALUE_INTEGER, lv)->type;

    case SMPL_VALUE_EQUAL:
    case SMPL_VALUE_NOTEQ:
        if (expr_eval(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;
        if (expr_eval(smpl, e->expr.arg2, &arg2) < 0)
            goto fail;

        lv = comparison_value(&arg1, &arg2, e->type) ? 1 : 0;
        value_reset(&arg1);
        value_reset(&arg2);

        return value_set(v, SMPL_VALUE_INTEGER, lv)->type;

    case SMPL_VALUE_NOT:
        if (expr_test(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;

        lv = negative_value(&arg1) ? 1 : 0;
        value_reset(&arg1);

        return value_set(v, SMPL_VALUE_INTEGER, lv)->type;

    case SMPL_VALUE_IS:
        if (expr_test(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;

        lv = !negative_value(&arg1) ? 1 : 0;
        value_reset(&arg1);

        return value_set(v, SMPL_VALUE_INTEGER, lv)->type;

    case SMPL_VALUE_FUNCREF:
        if (function_call(smpl, e->call.f, e->call.narg, e->call.args, v) < 0)
            goto fail;
        return v->type;

    case SMPL_VALUE_MACROREF:
        obuf = buffer_create(4096);
        if (obuf == NULL)
            goto nomem;
        if (macro_call(smpl, e->call.m, e->call.args, obuf) < 0)
            goto macro_fail;

        v->type    = SMPL_VALUE_STRING;
        v->str     = buffer_steal(obuf);
        v->dynamic = 1;

        return v->type;

    default:
        goto invalid_expr;
    }

 invalid_ref:
    v->type = -1;
    v->str  = "<invalid variable reference>";
    smpl_fail(-1, smpl, EINVAL, "invalid variable reference");

 invalid_expr:
    v->type = -1;
    v->str  = "<invalid value in expression>";
    smpl_fail(-1, smpl, EINVAL, "invalid value (0x%x) in expression", e->type);

 macro_fail:
    buffer_destroy(obuf);
    smpl_fail(-1, smpl, EINVAL, "macro call failed");

 nomem:
 fail:
    return -1;
}


int expr_test(smpl_t *smpl, smpl_expr_t *e, smpl_value_t *v)
{
    smpl_value_t arg1, arg2, rv;
    int32_t      lv;

    v->type    = SMPL_VALUE_INTEGER;
    v->dynamic = 0;

    switch (e->type) {
    case SMPL_VALUE_VARREF:
        if (symtbl_resolve(smpl, e->ref, &arg1) < 0)
            goto invalid_ref;
        lv = !negative_value(&arg1);
        break;

    case SMPL_VALUE_STRING:
    case SMPL_VALUE_INTEGER:
    case SMPL_VALUE_DOUBLE:
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        lv = !negative_value(e);
        break;

    case SMPL_VALUE_AND:
    case SMPL_VALUE_OR:
        lv = logical_value(smpl, e->type, e->expr.arg1, e->expr.arg2);
        break;

    case SMPL_VALUE_EQUAL:
    case SMPL_VALUE_NOTEQ:
        if (expr_eval(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;
        if (expr_eval(smpl, e->expr.arg2, &arg2) < 0)
            goto fail;

        lv = comparison_value(&arg1, &arg2, e->type);

        value_reset(&arg1);
        value_reset(&arg2);
        break;

    case SMPL_VALUE_NOT:
        if (expr_test(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;

        lv = negative_value(&arg1);
        value_reset(&arg1);
        break;

    case SMPL_VALUE_IS:
        if (expr_test(smpl, e->expr.arg1, &arg1) < 0)
            goto fail;

        lv = !negative_value(&arg1);
        value_reset(&arg1);
        break;

    case SMPL_VALUE_TRAIL: {
        int   len = strlen(e->str);
        char *p   = smpl->result->p - len;

        if (len > smpl->result->size)
            lv = 0;
        else
            lv = !strncmp(p, e->str, len);
        break;
    }

    case SMPL_VALUE_FIRST:
        lv = symtbl_loopflag(smpl, e->sym, SMPL_LOOP_FIRST);
        break;

    case SMPL_VALUE_LAST:
        lv = symtbl_loopflag(smpl, e->sym, SMPL_LOOP_LAST);
        break;

    case SMPL_VALUE_FUNCREF:
        if (function_call(smpl, e->call.f, e->call.narg, e->call.args, &rv) < 0)
            goto fail;
        lv = expr_test(smpl, &rv, v);
        value_reset(&rv);
        break;

    default:
        goto invalid_expr;
    }

    if (lv < 0)
        return value_set(v, SMPL_VALUE_UNKNOWN)->type;
    else
        return value_set(v, SMPL_VALUE_INTEGER, (int32_t)(lv ? 1 : 0))->type;

 invalid_ref:
    v->type = -1;
    v->str  = "<invalid variable reference>";
    smpl_fail(-1, smpl, EINVAL, "invalid variable reference");

 invalid_expr:
    v->type = -1;
    v->str  = "<invalid value in expression>";
    smpl_fail(-1, smpl, EINVAL, "invalid value (0x%x) in expression", e->type);

 fail:
    return -1;
}


int value_eval(smpl_t *smpl, smpl_expr_t *e, smpl_buffer_t *obuf)
{
    smpl_value_t v;
    int          r;

    if (obuf == NULL)
        obuf = smpl->result;

    if (expr_eval(smpl, e, &v) < 0)
        goto invalid_value;

    switch (v.type) {
    case SMPL_VALUE_UNKNOWN:
        goto invalid_value;

    case SMPL_VALUE_UNSET:
        r = 0;
        break;
    case SMPL_VALUE_STRING:
        r = buffer_printf(obuf, "%s", v.str);
        break;
    case SMPL_VALUE_INTEGER:
        r = buffer_printf(obuf, "%d", v.i32);
        break;
    case SMPL_VALUE_DOUBLE:
        r = buffer_printf(obuf, "%d", v.dbl);
        break;

    default:
        goto unprintable_value;
    }

    value_reset(&v);

    return r;

 invalid_value:
    return -1;

 unprintable_value:
    smpl_fail(-1, smpl, EINVAL, "unprintable value of type 0x%x in evaluation",
              v.type);
}


smpl_value_t *value_setv(smpl_value_t *v, int type, va_list aq)
{
    va_list ap;

    if (v == NULL)
        return NULL;

    v->type    = type & ~SMPL_VALUE_DYNAMIC;
    v->dynamic = type &  SMPL_VALUE_DYNAMIC;

    va_copy(ap, aq);

    switch (v->type) {
    case SMPL_VALUE_UNKNOWN:
    case SMPL_VALUE_UNSET:
        break;

    case SMPL_VALUE_INTEGER:
        v->i32 = va_arg(ap, int32_t);
        break;

    case SMPL_VALUE_DOUBLE:
        v->dbl = va_arg(ap, double);
        break;

    case SMPL_VALUE_STRING:
        v->str = va_arg(ap, char *);
        if (v->dynamic)
            v->str = smpl_strdup(v->str);
        break;

    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        v->json = va_arg(ap, smpl_json_t *);
        if (v->dynamic)
            smpl_json_ref(v->json);
        break;

    default:
        v->type = SMPL_VALUE_UNKNOWN;
        break;
    }

    va_end(ap);

    return v;
}


smpl_value_t *value_set(smpl_value_t *v, int type, ...)
{
    va_list ap;

    if (v != NULL) {
        va_start(ap, type);
        v = value_setv(v, type, ap);
        va_end(ap);
    }

    return v;
}


smpl_value_t *value_copy(smpl_value_t *dst, smpl_value_t *src)
{
    if (dst != src) {
        *dst = *src;
        smpl_list_init(&dst->hook);
    }

    if (!dst->dynamic)
        return dst;

    switch (dst->type) {
    case SMPL_VALUE_STRING:
        dst->str = smpl_strdup(dst->str);
        break;

    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        smpl_json_ref(dst->json);
        break;

    default:
        dst->type = SMPL_VALUE_UNKNOWN;
        break;
    }

    return dst;
}


void value_reset(smpl_value_t *v)
{

    if (v->dynamic != 0 && v->dynamic != 1)
        smpl_error("%s(): dynamic = %d !!!", __func__, v->dynamic);

    if (v->dynamic) {
        switch (v->type) {
        case SMPL_VALUE_STRING:
            smpl_free(v->str);
            break;
        case SMPL_VALUE_OBJECT:
        case SMPL_VALUE_ARRAY:
            smpl_json_unref(v->json);
            break;
        default:
            break;
        }
    }

    v->type = SMPL_VALUE_UNSET;
}
