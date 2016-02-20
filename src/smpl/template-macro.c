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


int macro_parse(smpl_t *smpl)
{
    smpl_macro_t *m;
    smpl_token_t  name;

    m = smpl_alloct(typeof(*m));

    if (m == NULL)
        goto nomem;

    smpl_list_init(&m->hook);
    smpl_list_init(&m->body);

    if (parser_pull_token(smpl, SMPL_PARSE_NAME, &name) < 0)
        goto name_failed;

    m->name = symtbl_add(smpl, name.str, SMPL_SYMBOL_MACRO);

    if (m->name < 0)
        goto name_failed;

    if (block_parse(smpl, SMPL_PARSE_BLOCK, &m->body, NULL) != SMPL_TOKEN_END)
        goto failed;

    smpl_list_append(&smpl->macros, &m->hook);

    return 0;

 nomem:
    return -1;

 name_failed:
    smpl_fail(-1, smpl, errno, "failed to parse/add macro name");

 failed:
    smpl_fail(-1, smpl, errno, "failed to parse body of macro '%s'", name.str);
}


void macro_free(smpl_macro_t *m)
{
    if (m == NULL)
        return;

    smpl_list_delete(&m->hook);
    block_free(&m->body);

    smpl_free(m);
}


void macro_purge(smpl_list_t *macros)
{
    smpl_macro_t *m;
    smpl_list_t  *p, *n;

    if (macros == NULL)
        return;

    smpl_list_foreach(macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);
        macro_free(m);
    }
}


void macro_dump(smpl_t *smpl, int fd, smpl_macro_t *m)
{
    dprintf(fd, "<macro '%s'>\n", symtbl_get(smpl, m->name));
    block_dump(smpl, fd, &m->body, 1);
}


smpl_macro_t *macro_find(smpl_t *smpl, smpl_sym_t sym)
{
    smpl_list_t  *p, *n;
    smpl_macro_t *m;

    if (SMPL_SYMBOL_TAG(sym) != SMPL_SYMBOL_MACRO)
        goto notfound;

    smpl_list_foreach(&smpl->macros, p, n) {
        m = smpl_list_entry(p, typeof(*m), hook);

        if (m->name == sym)
            return m;
    }

 notfound:
    return NULL;
}
