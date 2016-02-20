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


smpl_expr_t *first_parse(smpl_t *smpl, smpl_token_t *t)
{
    smpl_token_t end;
#if 0
    smpl_expr_t   *expr;
    smpl_token_t  *tkn, name;
    smpl_sym_t     sym;

    if (parser_pull_token(smpl, SMPL_PARSE_NAME, &name) != SMPL_TOKEN_NAME)
        goto invalid_name;

    if ((sym = symtbl_add(smpl, name.str, SMPL_SYMBOL_LOOP)) < 0)
        goto invalid_name;

    if ((tkn = smpl_alloct(typeof(*tkn))) == NULL)
        goto nomem;

    return expr_parse(smpl, &end);

 invalid_name:
    smpl_fail(NULL, smpl, EINVAL, "missing/invalid symbol name in '%s' test",
              t->type == SMPL_TOKEN_FIRST ? "first" : "last");
 nomem:
    return NULL;
#else
    SMPL_UNUSED(t);
    return expr_parse(smpl, &end);
#endif
}


smpl_expr_t *trail_parse(smpl_t *smpl, smpl_token_t *t)
{
    SMPL_UNUSED(smpl);
    SMPL_UNUSED(t);

    return NULL;
}


int branch_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_branch_t *br;
    smpl_expr_t        *test;
    smpl_token_t        var, end;
    smpl_list_t        *blks[2];
    int                 neg;

    smpl_debug("branch %s", t->str);

    test = NULL;
    switch (t->type) {
    case SMPL_TOKEN_IF:
        if ((test = expr_parse(smpl, &end)) == NULL)
            goto parse_error;
        if (end.type != SMPL_TOKEN_DO)
            goto missing_do;
        neg = 0;
        break;

    case SMPL_TOKEN_FIRST:
    case SMPL_TOKEN_LAST:
        if (parser_pull_token(smpl, SMPL_PARSE_NAME, &var) != SMPL_TOKEN_NAME)
            goto parse_error;
        if ((test = expr_first_parse(smpl, t, &var)) == NULL)
            goto parse_error;
        neg = t->str[0] == '!' ? 1 : 0;
        break;

    case SMPL_TOKEN_TRAIL:
        test = expr_trail_parse(smpl, t);
        neg = t->str[0] == '!' ? 1 : 0;
        break;

    default:
        goto invalid_branch;
    }

#if 0
    if (test == NULL)
        goto parse_error;
#endif

    br = smpl_alloct(typeof(*br));

    if (br == NULL)
        goto nomem;

    smpl_list_init(&br->hook);
    smpl_list_init(&br->posbr);
    smpl_list_init(&br->negbr);

    br->type = SMPL_INSN_BRANCH;
    br->test = test;

    blks[ neg] = &br->posbr;
    blks[!neg] = &br->negbr;

    if (parse_block(smpl, SMPL_PARSE_BLOCK, blks[0], &end) < 0)
        goto parse_error;

    if (end.type == SMPL_TOKEN_ELSE)
        if (parse_block(smpl, SMPL_PARSE_BLOCK, blks[1], &end) < 0)
            goto parse_error;

    if (end.type != SMPL_TOKEN_END)
        goto parse_error;

    smpl_list_append(block, &br->hook);

    return 0;

 nomem:
    smpl_free(br);
    return -1;

 invalid_branch:
    smpl_fail(-1, smpl, EINVAL, "invalid branch type");

 missing_do:
    smpl_fail(-1, smpl, EINVAL, "missing do keyword after if");

 parse_error:
    smpl_fail(-1, smpl, EINVAL, "failed to parse branch");
}


void branch_free(smpl_insn_t *insn)
{
    smpl_insn_branch_t *br = &insn->branch;

    if (br == NULL)
        return;

    smpl_list_delete(&br->hook);
    expr_free(br->test);
    block_free(&br->posbr);
    block_free(&br->negbr);
    smpl_free(br);
}


void branch_dump(smpl_t *smpl, int fd, smpl_insn_branch_t *branch, int indent)
{
    char expr[1024];

    expr_print(smpl, branch->test, expr, sizeof(expr));
    dprintf(fd, SMPL_INDENT_FMT"<if %s>\n", SMPL_INDENT_ARG(indent), expr);
    block_dump(smpl, fd, &branch->posbr, indent + 1);
    if (!smpl_list_empty(&branch->negbr)) {
        dprintf(fd, SMPL_INDENT_FMT"<else>\n", SMPL_INDENT_ARG(indent));
        block_dump(smpl, fd, &branch->negbr, indent + 1);
    }
    dprintf(fd, SMPL_INDENT_FMT"<end>\n", SMPL_INDENT_ARG(indent));
}


int branch_eval(smpl_t *smpl, smpl_insn_branch_t *br)
{
    smpl_value_t v;

    if (expr_test(smpl, br->test, &v) < 0 || v.type != SMPL_VALUE_INTEGER)
        goto invalid_test;

    if (v.i32)
        return block_eval(smpl, &br->posbr);
    else
        return block_eval(smpl, &br->negbr);

 invalid_test:
/*
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate branch test expression");
*/
    return 0;
}


