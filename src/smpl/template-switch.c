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

#include <stdio.h>
#include <errno.h>

#include <smpl/macros.h>
#include <smpl/types.h>

static int case_parse(smpl_t *smpl, smpl_list_t *cases);
static void case_free(smpl_insn_case_t *c);
static void case_dump(smpl_t *smpl, int fd, smpl_insn_case_t *c, int indent);
static int default_parse(smpl_t *smpl, smpl_list_t *d);
static void default_free(smpl_list_t *d);
static void default_dump(smpl_t *smpl, int fd, smpl_list_t *d, int indent);


int switch_parse(smpl_t *smpl, smpl_list_t *block)
{
    smpl_insn_switch_t *sw;
    smpl_token_t        t, end;

    sw = smpl_alloct(typeof(*sw));

    if (sw == NULL)
        goto nomem;

    sw->type = SMPL_INSN_SWITCH;
    smpl_list_init(&sw->hook);
    smpl_list_init(&sw->cases);
    smpl_list_init(&sw->defbr);

    if ((sw->test = expr_parse(smpl, &end)) == NULL)
        goto invalid_expression;

    if (parser_pull_token(smpl, SMPL_PARSE_SWITCH, &t) != SMPL_TOKEN_IN)
        goto missing_in;

    while (parser_pull_token(smpl, SMPL_PARSE_SWITCH, &t) != SMPL_TOKEN_END) {
        switch (t.type) {
        case SMPL_TOKEN_CASE:
            if (case_parse(smpl, &sw->cases) < 0)
                goto invalid_case;
            break;

        case SMPL_TOKEN_ELSE:
            if (!smpl_list_empty(&sw->defbr))
                goto multiple_defaults;
            if (default_parse(smpl, &sw->defbr) < 0)
                goto invalid_default;
            break;

        default:
            goto parse_error;
        }
    }

    smpl_list_append(block, &sw->hook);

    return 0;

 nomem:
    return -1;

 invalid_expression:
    smpl_fail(-1, smpl, EINVAL, "failed to parse switch expression");

 missing_in:
    smpl_fail(-1, smpl, EINVAL, "missing do keyword in switch");

 invalid_case:
    smpl_fail(-1, smpl, EINVAL, "failed to parse switch case");

 multiple_defaults:
    smpl_fail(-1, smpl, EINVAL, "multiple default branches for switch");

 invalid_default:
    smpl_fail(-1, smpl, EINVAL, "failed to parse switch 'default' branch");

 parse_error:
    smpl_fail(-1, smpl, EINVAL, "failed to parse switch statement");
}


void switch_free(smpl_insn_t *insn)
{
    smpl_insn_switch_t *sw = &insn->swtch;
    smpl_insn_case_t   *c;
    smpl_list_t        *p, *n;

    if (sw == NULL)
        return;

    smpl_list_delete(&sw->hook);

    expr_free(sw->test);

    smpl_list_foreach(&sw->cases, p, n) {
        c = smpl_list_entry(p, typeof(*c), hook);
        case_free(c);
    }

    default_free(&sw->defbr);

    smpl_free(sw);
}


void switch_dump(smpl_t *smpl, int fd, smpl_insn_switch_t *sw, int indent)
{
    char              expr[1024];
    smpl_insn_case_t *c;
    smpl_list_t      *p, *n;

    expr_print(smpl, sw->test, expr, sizeof(expr));
    dprintf(fd, SMPL_INDENT_FMT"<switch %s>\n", SMPL_INDENT_ARG(indent), expr);
    smpl_list_foreach(&sw->cases, p, n) {
        c = smpl_list_entry(p, typeof(*c), hook);
        case_dump(smpl, fd, c, indent + 1);
    }
    default_dump(smpl, fd, &sw->defbr, indent + 1);
}


int switch_eval(smpl_t *smpl, smpl_insn_switch_t *sw, smpl_buffer_t *obuf)
{
    smpl_insn_case_t *c;
    smpl_list_t      *p, *n;
    smpl_value_t      test, expr;
    int               r;

    if (obuf == NULL)
        obuf = smpl->result;

    if (expr_eval(smpl, sw->test, &test) < 0)
        goto invalid_test;

    smpl_list_foreach(&sw->cases, p, n) {
        c = smpl_list_entry(p, typeof(*c), hook);

        if (expr_eval(smpl, c->expr, &expr) < 0)
            goto invalid_case;
        r = expr_compare_values(&test, &expr);
        value_reset(&expr);

        if (r) {
            if (block_eval(smpl, &c->body, obuf) < 0)
                goto case_failed;
            else
                goto out;
        }
    }

    if (block_eval(smpl, &sw->defbr, obuf) < 0)
        goto default_failed;

 out:
    value_reset(&test);
    return 0;

 invalid_test:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate switch test expression");

 invalid_case:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate switch case expression");

 case_failed:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate switch case body");

 default_failed:
    smpl_fail(-1, smpl, EINVAL, "failed to evaluate switch default body");
}


static int case_parse(smpl_t *smpl, smpl_list_t *cases)
{
    smpl_insn_case_t *c;
    smpl_token_t      end;
    int               flags;

    c = smpl_alloct(typeof(*c));

    if (c == NULL)
        goto nomem;

    smpl_list_init(&c->hook);
    smpl_list_init(&c->body);

    if ((c->expr = expr_parse(smpl, &end)) == NULL)
        goto invalid_expression;

    flags  = SMPL_SKIP_WHITESPACE | SMPL_ALLOW_INCLUDE;
    flags |= SMPL_PARSE_BLOCK | SMPL_BLOCK_DOEND;
    if (block_parse(smpl, flags, &c->body, NULL) < 0)
        goto invalid_block;

    smpl_list_append(cases, &c->hook);

    return 0;

 nomem:
    return -1;

 invalid_expression:
    smpl_fail(-1, smpl, EINVAL, "failed to parse case expression");

 invalid_block:
    smpl_fail(-1, smpl, EINVAL, "failed to parse case block");
}


static void case_free(smpl_insn_case_t *c)
{
    smpl_list_delete(&c->hook);

    expr_free(c->expr);
    block_free(&c->body);

    smpl_free(c);
}


static void case_dump(smpl_t *smpl, int fd, smpl_insn_case_t *c, int indent)
{
    char expr[1024];

    expr_print(smpl, c->expr, expr, sizeof(expr));
    dprintf(fd, SMPL_INDENT_FMT"<case %s>:\n", SMPL_INDENT_ARG(indent), expr);
    block_dump(smpl, fd, &c->body, indent + 1);
}


static int default_parse(smpl_t *smpl, smpl_list_t *d)
{
    int flags;

    flags  = SMPL_SKIP_WHITESPACE | SMPL_ALLOW_INCLUDE;
    flags |= SMPL_PARSE_BLOCK | SMPL_BLOCK_DOEND;

    return block_parse(smpl, flags, d, NULL);
}


static void default_free(smpl_list_t *d)
{
    block_free(d);
}


static void default_dump(smpl_t *smpl, int fd, smpl_list_t *d, int indent)
{
    dprintf(fd, SMPL_INDENT_FMT"<default>:\n", SMPL_INDENT_ARG(indent));
    block_dump(smpl, fd, d, indent + 1);
}
