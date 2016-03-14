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


int loop_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_for_t *loop;
    smpl_token_t     n;
    smpl_varref_t   *ref;
    smpl_sym_t       key, val;
    int              flags;

    SMPL_UNUSED(t);

    smpl_debug("for");

    key = val = -1;
    switch (parser_pull_token(smpl, SMPL_PARSE_EXPR, &n)) {
    case SMPL_TOKEN_NAME:
        if ((key = symtbl_add(smpl, n.str, SMPL_SYMBOL_LOOP)) < 0)
            goto invalid_name;
        break;
    case ':':
        goto get_val;
    default:
        goto invalid_name;
    }

    switch (parser_pull_token(smpl, SMPL_PARSE_EXPR, &n)) {
    case ':':
        goto get_val;
    case SMPL_TOKEN_IN:
        val = key;
        key = -1;
        goto get_var;
    default:
        goto invalid_name;
    }

 get_val:
    switch (parser_pull_token(smpl, SMPL_PARSE_EXPR, &n)) {
    case SMPL_TOKEN_NAME:
        val = symtbl_add(smpl, n.str, SMPL_SYMBOL_LOOP);
        if (val < 0)
            goto invalid_name;
        break;
    case SMPL_TOKEN_IN:
        goto get_var;
    default:
        goto invalid_name;
    }

    if (parser_pull_token(smpl, SMPL_PARSE_EXPR, &n) != SMPL_TOKEN_IN)
        goto missing_in;

 get_var:
    if (parser_pull_token(smpl, SMPL_PARSE_EXPR, &n) != SMPL_TOKEN_VARREF)
        goto missing_varref;

    if ((ref = varref_parse(smpl, n.str, n.path, n.line)) == NULL)
        goto invalid_varref;

    loop = smpl_alloct(typeof(*loop));

    if (loop == NULL)
        goto nomem;

    smpl_list_init(&loop->hook);
    smpl_list_init(&loop->body);
    loop->type = SMPL_INSN_FOR;
    loop->key  = key;
    loop->val  = val;
    loop->ref  = ref;

    flags = SMPL_SKIP_WHITESPACE|SMPL_PARSE_BLOCK|SMPL_BLOCK_DOEND;
    if (block_parse(smpl, flags, &loop->body, NULL) != SMPL_TOKEN_END)
        goto parse_error;

    smpl_list_append(block, &loop->hook);

    return 0;

 nomem:
    return -1;

 invalid_name:
    smpl_fail(-1, smpl, EINVAL, "invalid name '%s' in loop", n.str);

 missing_in:
    smpl_fail(-1, smpl, EINVAL, "missing in keyword in loop");

 missing_varref:
    smpl_fail(-1, smpl, EINVAL, "missing variable reference in loop");

 invalid_varref:
    smpl_fail(-1, smpl, EINVAL, "invalid variable reference '%s'", n.str);

 parse_error:
    smpl_fail(-1, smpl, EINVAL, "failed to parse for loop");
}


void loop_free(smpl_insn_t *insn)
{
    smpl_insn_for_t *loop = &insn->loop;

    if (loop == NULL)
        return;

    smpl_list_delete(&loop->hook);
    varref_free(loop->ref);
    block_free(&loop->body);
    smpl_free(loop);
}


void loop_dump(smpl_t *smpl, int fd, smpl_insn_for_t *loop, int indent)
{
    const char *key, *val, *ref;
    char        buf[256];

    if (loop->key < 0)
        key = NULL;
    else
        key = symtbl_get(smpl, loop->key);

    if (loop->val < 0)
        val = NULL;
    else
        val = symtbl_get(smpl, loop->val);

    ref = varref_print(smpl, loop->ref, buf, sizeof(buf));

    dprintf(fd, SMPL_INDENT_FMT"<for %s:%s in %s>\n", SMPL_INDENT_ARG(indent),
            key ? key : "", val ? val : "", ref);

    block_dump(smpl, fd, &loop->body, indent + 1);
}


static int loop_push(smpl_t *smpl, smpl_sym_t sym,
                     smpl_value_type_t type, void *val, int *loopflags)
{
    int32_t i32;
    double  dbl;

    if (sym <= 0)
        return 0;

    if (type != SMPL_VALUE_OBJECT && type != SMPL_VALUE_ARRAY)
        return symtbl_push_loop(smpl, sym, type, val, loopflags);

    switch (smpl_json_type(val)) {
    case SMPL_JSON_STRING:
        type = SMPL_VALUE_STRING;
        val  = (void *)smpl_json_string_value(val);
        break;
    case SMPL_JSON_INTEGER:
        type = SMPL_VALUE_STRING;
        i32  = smpl_json_integer_value(val);
        val  = &i32;
        break;
    case SMPL_JSON_DOUBLE:
        type = SMPL_VALUE_DOUBLE;
        dbl  = smpl_json_double_value(val);
        val  = &dbl;
        break;
    case SMPL_JSON_BOOLEAN:
        type = SMPL_VALUE_INTEGER;
        i32  = smpl_json_boolean_value(val) ? 1 : 0;
        val  = &i32;
        break;
    case SMPL_JSON_OBJECT:
        type = SMPL_VALUE_OBJECT;
        break;
    case SMPL_JSON_ARRAY:
        type = SMPL_VALUE_ARRAY;
        break;
    default:
        goto invalid_value;
    }

    return symtbl_push_loop(smpl, sym, type, val, loopflags);

 invalid_value:
    smpl_fail(-1, smpl, EINVAL, "invalid loop variable value type");
}


static int loop_pop(smpl_t *smpl, smpl_sym_t sym)
{
    if (sym <= 0)
        return 0;

    return symtbl_pop(smpl, sym);
}


int loop_eval(smpl_t *smpl, smpl_insn_for_t *loop)
{
    smpl_value_t      value;
    smpl_json_iter_t  it;
    smpl_json_t      *v;
    char             *k;
    void             *vptr;
    int               fl, i, n;

    if (symtbl_resolve(smpl, loop->ref, &value) < 0)
        goto invalid_reference;

    switch (value.type) {
    case SMPL_VALUE_OBJECT:
        fl = SMPL_LOOP_FIRST;
        smpl_json_foreach(value.json, k, v, it) {
            fl |= smpl_json_iter_last(it) ? SMPL_LOOP_LAST : 0;

            loop_push(smpl, loop->key, SMPL_VALUE_STRING, k, &fl);
            loop_push(smpl, loop->val, SMPL_VALUE_OBJECT, v, &fl);

            if (block_eval(smpl, &loop->body) < 0)
                goto fail;

            loop_pop(smpl, loop->key);
            loop_pop(smpl, loop->val);

            fl &= ~SMPL_LOOP_FIRST;
        }
        break;

    case SMPL_VALUE_ARRAY:
        n  = smpl_json_array_length(value.json);
        fl = SMPL_LOOP_FIRST;
        for (i = 0; i < n; i++) {
            if (i == n - 1)
                fl |= SMPL_LOOP_LAST;
            v = smpl_json_array_get(value.json, i);

            loop_push(smpl, loop->key, SMPL_VALUE_INTEGER, &i, &fl);
            loop_push(smpl, loop->val, SMPL_VALUE_OBJECT ,  v, &fl);

            if (block_eval(smpl, &loop->body) < 0)
                goto fail;

            loop_pop(smpl, loop->key);
            loop_pop(smpl, loop->val);

            fl &= ~SMPL_LOOP_FIRST;
        }
        break;

    case SMPL_VALUE_STRING:  vptr =  value.str; goto eval;
    case SMPL_VALUE_INTEGER: vptr = &value.i32; goto eval;
    case SMPL_VALUE_DOUBLE:  vptr = &value.dbl; goto eval;
    case SMPL_VALUE_UNSET:                      goto out;
    eval:
        fl = SMPL_LOOP_FIRST | SMPL_LOOP_LAST;
        loop_push(smpl, loop->key, SMPL_SYMBOL_STRING, ""  , &fl);
        loop_push(smpl, loop->val, value.type        , vptr, &fl);

        if (block_eval(smpl, &loop->body) < 0)
            goto fail;

        loop_pop(smpl, loop->key);
        loop_pop(smpl, loop->val);
        break;

    default:
        goto invalid_value;
    }

 out:
    return 0;

 invalid_reference:
    smpl_fail(-1, smpl, EINVAL, "invalid variable reference in loop");

 invalid_value:
    smpl_fail(-1, smpl, EINVAL, "invalid variable value in loop");

 fail:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate loop");
}
