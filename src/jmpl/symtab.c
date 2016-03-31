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

    if (tag != JMPL_SYMBOL_FIELD &&
        tag != JMPL_SYMBOL_STRING &&
        tag != JMPL_SYMBOL_LOOP)
        return symtab_error;

    if (idx >= nsymbol)
        return symtab_error;

    sym = symbols + idx;

    if (!(sym->tags & tag))
        return symtab_error;

    return sym->str;
}


static jmpl_symval_t *push_value(int32_t id, int type, void *v)
{
    jmpl_symbol_t *sym;
    jmpl_symval_t *val;
    int32_t        tag, idx;

    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_FIELD && tag != JMPL_SYMBOL_LOOP)
        goto invalid_id;

    if (idx >= nsymbol)
        goto invalid_id;

    sym = symbols + idx;

    if (!sym->values) {
        sym->values = iot_allocz(sizeof(*sym->values));

        if (sym->values == NULL)
            goto nomem;

        iot_list_init(sym->values);
    }

    val = iot_allocz(sizeof(*val));

    if (val == NULL)
        goto nomem;

    iot_list_init(&val->hook);

    val->type = type;

    switch (type) {
    case JMPL_SYMVAL_STRING:
        val->s = v;
        break;
    case JMPL_SYMVAL_INTEGER:
        val->i = *(int *)v;
        break;
    case JMPL_SYMVAL_JSON:
        val->j = v;
        break;
    default:
        goto invalid_value;
    }

    iot_list_append(sym->values, &val->hook);

    return val;

 invalid_value:
    iot_free(val);
 invalid_id:
    errno = EINVAL;
 nomem:
    return NULL;
}


int symtab_push(int32_t id, int type, void *v)
{
    return push_value(id, type, v) != NULL ? 0 : -1;
}


int symtab_push_loop(int32_t id, int type, void *v, int *firstp, int *lastp)
{
    jmpl_symval_t *val;

    val = push_value(id, type, v);

    if (val == NULL)
        return -1;

    val->firstp = firstp;
    val->lastp  = lastp;
    return 0;
}


int symtab_check_loop(int32_t id, int *firstp, int *lastp)
{
    jmpl_symbol_t *sym;
    jmpl_symval_t *val;
    int32_t        tag, idx;

    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_LOOP)
        goto incompatible_id;

    if (idx >= nsymbol)
        goto invalid_id;

    sym = symbols + idx;

    if (!(sym->tags & tag))
        goto symtab_error;

    if (!sym->values || iot_list_empty(sym->values))
        goto empty_stack;

    val = iot_list_entry(sym->values->prev, typeof(*val), hook);

    if (firstp)
        *firstp = val->firstp ? *val->firstp : -1;
    if (lastp)
        *lastp  = val->lastp  ? *val->lastp  : -1;

    return 0;

 invalid_id:
 incompatible_id:
 symtab_error:    errno = EINVAL; goto out;
 empty_stack:     errno = ENOENT; goto out;
 out:
    if (firstp)
        *firstp = -1;
    if (lastp)
        *lastp = -1;
    return -1;
}


int symtab_pop(int32_t id)
{
    jmpl_symbol_t *sym;
    jmpl_symval_t *val;
    int32_t        tag, idx;

    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_FIELD && tag != JMPL_SYMBOL_LOOP)
        goto invalid_id;

    if (idx >= nsymbol)
        goto invalid_id;

    sym = symbols + idx;

    if (!sym->values || iot_list_empty(sym->values))
        goto empty_stack;

    val = iot_list_entry(sym->values->prev, typeof(*val), hook);

    iot_list_delete(&val->hook);
    iot_free(val);

    return 0;

 invalid_id:
 empty_stack:
    errno = EINVAL;
    return -1;
}


void symtab_flush(void)
{
    jmpl_symbol_t   *sym;
    jmpl_symval_t   *val;
    iot_list_hook_t *p, *n;
    int              i;

    for (i = 0, sym = symbols; i < nsymbol; i++, sym++) {
        if (!sym->values || iot_list_empty(sym->values))
            continue;

        iot_list_foreach(sym->values, p, n) {
            val = iot_list_entry(p, typeof(*val), hook);
            iot_list_delete(p);
            iot_free(val);
        }

        iot_free(sym->values);
        sym->values = NULL;
    }
}


int symtab_entry(int32_t id, void **valp)
{
    jmpl_symbol_t *sym;
    jmpl_symval_t *val;
    int32_t        tag, idx;

    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_FIELD && tag != JMPL_SYMBOL_LOOP)
        goto invalid_id;

    if (idx >= nsymbol)
        goto invalid_id;

    sym = symbols + idx;

    if (!sym->values || iot_list_empty(sym->values))
        goto empty_stack;

    val = iot_list_entry(sym->values->prev, typeof(*val), hook);

    switch (val->type) {
    case JMPL_SYMVAL_STRING:
        *valp = (char *)val->s;
        break;
    case JMPL_SYMVAL_INTEGER:
        *valp = &val->i;
        break;
    case JMPL_SYMVAL_JSON:
        *valp = val->j;
        break;
    default:
        *valp = NULL;
        goto invalid_value;
    }

    return val->type;

 invalid_id:
 empty_stack:
 invalid_value:
    errno = EINVAL;
    return -1;
}


int symtab_resolve(jmpl_ref_t *r, void **valp)
{
    void    *v;
    int      type, i;
    int32_t  tag, idx, id;

    if (r == NULL)
        goto noref;

    id  = r->ids[0];
    tag = JMPL_SYMBOL_TAG(id);
    idx = JMPL_SYMBOL_IDX(id);

    if (tag != JMPL_SYMBOL_FIELD && tag != JMPL_SYMBOL_LOOP)
        goto invalid_id;

    type = symtab_entry(r->ids[0], &v);

    if (type < 0)
        goto invalid_ref;

    for (i = 1; i < r->nid; i++) {
        if (i == 1 && type != JMPL_SYMVAL_JSON)
            goto invalid_ref;

        id  = r->ids[i];
        tag = JMPL_SYMBOL_TAG(id);

        switch (tag) {
        case JMPL_SYMBOL_FIELD:
        case JMPL_SYMBOL_STRING:
            if (iot_json_get_type(v) != IOT_JSON_OBJECT)
                goto invalid_ref;

            v = iot_json_get(v, symtab_get(id));
            break;

        case JMPL_SYMBOL_INDEX:
            if (iot_json_get_type(v) != IOT_JSON_ARRAY)
                goto invalid_ref;

            idx = JMPL_SYMBOL_IDX(id);

            if (idx > iot_json_array_length(v))
                goto invalid_ref;

            v = iot_json_array_get(v, idx);
            break;

        }
    }

    if (v == NULL)
        type = JMPL_SYMVAL_UNKNOWN;

    *valp = v;
    return type;

 noref:
    *valp = NULL;
    errno = ENOENT;
    return -1;

 invalid_id:
 invalid_ref:
    *valp = NULL;
    errno = EINVAL;
    return -1;
}
