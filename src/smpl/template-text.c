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

#include <smpl/macros.h>
#include <smpl/types.h>


int text_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_text_t *text;

    SMPL_UNUSED(smpl);

    text = smpl_alloct(typeof(*text));

    if (text == NULL)
        goto nomem;

    smpl_list_init(&text->hook);
    text->type = SMPL_INSN_TEXT;
    text->path = t->path;
    text->line = t->line;
    text->text = t->str;

    smpl_list_append(block, &text->hook);

    return 0;

 nomem:
    return -1;
}


void text_free(smpl_insn_t *insn)
{
    smpl_insn_text_t *text = &insn->text;

    if (text == NULL)
        return;

    smpl_list_delete(&text->hook);
    smpl_free(text);
}


void text_dump(smpl_t *smpl, int fd, smpl_insn_text_t *text, int indent)
{
    SMPL_UNUSED(smpl);

    dprintf(fd, SMPL_INDENT_FMT"<text '%s'>\n", SMPL_INDENT_ARG(indent),
            text->text);
}


int text_eval(smpl_t *smpl, smpl_insn_text_t *text)
{
    return buffer_printf(smpl->result, "%s", text->text);
}


