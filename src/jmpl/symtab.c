/*
 * Copyright (c) 2015, Intel Corporation
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/debug.h>

#include "jmpl/jmpl.h"
#include "jmpl/parser.h"



static jmpl_symbol_t *symbols;
static int            nsymbol;

const char *symtab_error = "<symbol table: invalid access>";


int32_t symtab_add(const char *str, int32_t tag)
{
    jmpl_symbol_t *sym;
    int32_t        i;

    for (i = 0, sym = symbols; i < nsymbol; i++, sym++) {
        if (!strcmp(sym->str, str)) {
            sym->tags |= tag;
            return i | tag;
        }
    }

    if (iot_reallocz(symbols, nsymbol, nsymbol + 1) == NULL)
        return -1;

    sym = symbols + nsymbol;

    sym->tags = tag;
    sym->str  = iot_strdup(str);

    if (sym->str == NULL)
        return -1;

    nsymbol++;

    return i | tag;
}


const char *symtab_get(int32_t id)
{
    jmpl_symbol_t *sym;
    int32_t        tag, idx;

    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_FIELD && tag != JMPL_SYMBOL_STRING)
        return symtab_error;

    if (idx >= nsymbol)
        return symtab_error;

    sym = symbols + idx;

    if (!(sym->tags & tag))
        return symtab_error;

    return sym->str;
}

