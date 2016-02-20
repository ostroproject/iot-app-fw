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


int switch_parse(smpl_t *smpl, smpl_list_t *block)
{
    smpl_insn_switch_t *sw;

    SMPL_UNUSED(smpl);

    sw = smpl_alloct(typeof(*sw));

    if (sw == NULL)
        goto nomem;

    sw->type = SMPL_INSN_SWITCH;
    smpl_list_init(&sw->hook);
    smpl_list_init(&sw->cases);
    smpl_list_init(&sw->defbr);

    smpl_list_append(block, &sw->hook);

    return 0;

 nomem:
    return -1;
}


void switch_free(smpl_insn_t *insn)
{
    smpl_insn_switch_t *sw = &insn->swtch;

    if (sw == NULL)
        return;

    smpl_list_delete(&sw->hook);
    smpl_free(sw);
}


void switch_dump(smpl_t *smpl, int fd, smpl_insn_switch_t *sw, int indent)
{
    SMPL_UNUSED(smpl);
    SMPL_UNUSED(sw);

    dprintf(fd, SMPL_INDENT_FMT"<SWITCH>\n", SMPL_INDENT_ARG(indent));
}


int switch_eval(smpl_t *smpl, smpl_insn_switch_t *sw)
{
    SMPL_UNUSED(smpl);
    SMPL_UNUSED(sw);

    return 0;
}
