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


void block_free(smpl_list_t *block)
{
    smpl_list_t *p, *n;
    smpl_insn_t *insn;

    smpl_list_foreach(block, p, n) {
        insn = smpl_list_entry(p, typeof(*insn), any.hook);

        switch (insn->type) {
        case SMPL_INSN_TEXT:     text_free(insn);         break;
        case SMPL_INSN_VARREF:   vref_free(insn);         break;
        case SMPL_INSN_BRANCH:   branch_free(insn);       break;
        case SMPL_INSN_FOR:      loop_free(insn);         break;
        case SMPL_INSN_SWITCH:   switch_free(insn);       break;
        case SMPL_INSN_MACROREF: macro_free_ref(insn);    break;
        case SMPL_INSN_FUNCREF:  function_free_ref(insn); break;
        default:
            break;
        }
    }
}


void block_dump(smpl_t *smpl, int fd, smpl_list_t *block, int indent)
{
    smpl_list_t *p, *n;
    smpl_insn_t *insn;

    smpl_list_foreach(block, p, n) {
        insn = smpl_list_entry(p, typeof(*insn), any.hook);

        switch (insn->type) {
        case SMPL_INSN_TEXT:
            text_dump(smpl, fd, &insn->text, indent + 1);
            break;
        case SMPL_INSN_VARREF:
            vref_dump(smpl, fd, &insn->vref, indent + 1);
            break;
        case SMPL_INSN_BRANCH:
            branch_dump(smpl, fd, &insn->branch, indent + 1);
            break;
        case SMPL_INSN_FOR:
            loop_dump(smpl, fd, &insn->loop, indent + 1);
            break;
        case SMPL_INSN_SWITCH:
            switch_dump(smpl, fd, &insn->swtch, indent + 1);
            break;
        default:
            break;
        }
    }
}


int block_eval(smpl_t *smpl, smpl_list_t *block)
{
    smpl_list_t *p, *n;
    smpl_insn_t *insn;
    int          r;

    smpl_list_foreach(block, p, n) {
        insn = smpl_list_entry(p, typeof(*insn), any.hook);

        switch (insn->type) {
        case SMPL_INSN_TEXT:
            r = text_eval(smpl, &insn->text);
            break;
        case SMPL_INSN_VARREF:
            r = vref_eval(smpl, &insn->vref);
            break;
        case SMPL_INSN_BRANCH:
            r = branch_eval(smpl, &insn->branch);
            break;
        case SMPL_INSN_FOR:
            r = loop_eval(smpl, &insn->loop);
            break;
        case SMPL_INSN_SWITCH:
            r = switch_eval(smpl, &insn->swtch);
            break;

        case SMPL_INSN_MACROREF:
            r = macro_eval(smpl, &insn->call);
            break;
        case SMPL_INSN_FUNCREF:
            r = function_eval(smpl, &insn->call);
            break;

        default:
            r = -1;
            errno = EINVAL;
        }

        if (r < 0)
            goto fail;
    }

    return 0;

 fail:
    smpl_fail(-1, smpl, errno, "failed to evaluate instruction block");
}
