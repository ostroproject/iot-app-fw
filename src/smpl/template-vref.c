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


int vref_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block)
{
    smpl_insn_vref_t *vref;

    vref = smpl_alloct(typeof(*vref));

    if (vref == NULL)
        goto nomem;

    smpl_list_init(&vref->hook);
    vref->type = SMPL_INSN_VARREF;
    vref->path = t->path;
    vref->line = t->line;
    vref->ref  = varref_parse(smpl, t->str, t->path, t->line);

    if (vref->ref == NULL)
        goto invalid;

    smpl_list_append(block, &vref->hook);

    return 0;

 nomem:
    return -1;

 invalid:
    smpl_fail(-1, smpl, EINVAL, "invalid index/variable reference");
    smpl_free(vref);
    return -1;
}


void vref_free(smpl_insn_t *insn)
{
    smpl_insn_vref_t *vref = &insn->vref;

    if (vref == NULL)
        return;

    smpl_list_delete(&vref->hook);
    varref_free(vref->ref);
    smpl_free(vref);
}


void vref_dump(smpl_t *smpl, int fd, smpl_insn_vref_t *vref, int indent)
{
    char buf[256];

    dprintf(fd, SMPL_INDENT_FMT"<varref '%s'>\n", SMPL_INDENT_ARG(indent),
            varref_print(smpl, vref->ref, buf, sizeof(buf)));
}


int vref_eval(smpl_t *smpl, smpl_insn_vref_t *vref)
{
    char buf[1024];

    return buffer_printf(smpl->result, "%s",
                         varref_string(smpl, vref->ref, buf, sizeof(buf)));
}

