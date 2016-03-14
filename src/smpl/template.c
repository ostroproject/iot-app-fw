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


int template_parse(smpl_t *smpl)
{
    if (block_parse(smpl, SMPL_PARSE_MAIN, &smpl->body, NULL) == SMPL_TOKEN_EOF)
        return 0;

    smpl_fail(-1, smpl, EINVAL, "failed to parse template");
}


void template_free(smpl_list_t *body)
{
    block_free(body);
}


int template_print(smpl_t *smpl, int fd)
{
    SMPL_UNUSED(smpl);
    SMPL_UNUSED(fd);

    return 0;
}


void template_dump(smpl_t *smpl, int fd)
{
    smpl_macro_t *m = NULL;
    smpl_list_t  *p, *n;

    smpl_list_foreach(&smpl->macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);
        macro_dump(smpl, fd, m);
    }

    if (m != NULL)
        dprintf(fd, "\n");

    block_dump(smpl, fd, &smpl->body, 0);
}


int template_evaluate(smpl_t *smpl)
{
    return block_eval(smpl, &smpl->body);
}


