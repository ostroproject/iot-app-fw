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


const char *symtbl_enoent = "<symbol table: no such symbol>";
const char *symtbl_einval = "<symbol table: invalid symbol type>";


int symtbl_create(smpl_t *smpl)
{
    smpl->symtbl = smpl_alloct(typeof(*smpl->symtbl));

    return smpl->symtbl != NULL ? 0 : -1;
}


void symtbl_destroy(smpl_t *smpl)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    int            i;

    if (tbl == NULL)
        return;

    for (i = 0, s = tbl->symbols; i < tbl->nsymbol; i++, s++) {
        if (s->values != NULL) {
            while (!smpl_list_empty(s->values))
                symtbl_pop(smpl, SMPL_SYMBOL_FIELD | i);
            smpl_free(s->values);
        }

        smpl_free(s->symbol);
    }

    smpl_free(tbl->symbols);
    smpl_free(tbl);

    smpl->symtbl = NULL;
}


smpl_symbol_t *symtbl_symbol(smpl_t *smpl, const char *name)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *sym;
    int            i;

    for (i = 0, sym = tbl->symbols; i < tbl->nsymbol; i++, sym++)
        if (!strcmp(sym->symbol, name))
            return sym;

    return NULL;
}


smpl_sym_t symtbl_add(smpl_t *smpl, const char *name, int32_t tag)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    smpl_sym_t     id;
    char          *end;
    int            idx;

    if (tag == SMPL_SYMBOL_FIELD || tag == SMPL_SYMBOL_INDEX) {
        idx = (int)strtol(name, &end, 0);

        if (!*end)
            return idx | SMPL_SYMBOL_INDEX;
    }

    if ((s = symtbl_symbol(smpl, name)) != NULL) {
        s->mask |= tag;
        id = s - tbl->symbols;
    }
    else {
        if (!smpl_reallocz(tbl->symbols, tbl->nsymbol, tbl->nsymbol + 1))
            goto nomem;

        s = tbl->symbols + tbl->nsymbol;
        s->symbol = smpl_strdup(name);

        if (s->symbol == NULL)
            goto nomem;

        s->mask = tag;

        id = tbl->nsymbol++;
    }

    return id | tag;

 nomem:
    return -1;
}


smpl_sym_t symtbl_find(smpl_t *smpl, const char *name, int mask)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    int            idx, tag;

    s = symtbl_symbol(smpl, name);

    if (s == NULL)
        goto notfound;

    if (!mask)
        mask = -1;

    tag = s->mask & mask;

    if (!tag)
        goto notfound;

    idx = s - tbl->symbols;

    return idx | tag;

 notfound:
    errno = ENOENT;
    return -1;
}


smpl_symbol_t *symtbl_entry(smpl_t *smpl, smpl_sym_t sym)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    int            idx, tag;

    tag = SMPL_SYMBOL_TAG(sym);
    idx = SMPL_SYMBOL_IDX(sym);

    if (idx < 0 || idx >= tbl->nsymbol)
        goto noent;

    s = tbl->symbols + idx;

    if (!(s->mask & tag))
        goto invalid;

    return s;

 noent:
    errno = ENOENT;
    return NULL;

 invalid:
    errno = EINVAL;
    return NULL;
}


const char *symtbl_get(smpl_t *smpl, smpl_sym_t sym)
{
    smpl_symbol_t *s;

    s = symtbl_entry(smpl, sym);

    if (s != NULL)
        return s->symbol;

    switch (errno) {
    case ENOENT:
        return symtbl_enoent;
    case EINVAL:
    default:
        return symtbl_einval;
    }
}


static smpl_symbol_t *push_value(smpl_t *smpl, smpl_sym_t sym, smpl_value_t *v)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    int            idx, tag;

    tag = SMPL_SYMBOL_TAG(sym);
    idx = SMPL_SYMBOL_IDX(sym);

    if (idx < 0 || idx >= tbl->nsymbol)
        goto no_symbol;

    s = tbl->symbols + idx;

    switch (tag) {
    case SMPL_SYMBOL_DATA:
        if (s->values != NULL && !smpl_list_empty(s->values))
            goto already_set;
    case SMPL_SYMBOL_FIELD:
    case SMPL_SYMBOL_LOOP:
    case SMPL_SYMBOL_ARG:
        break;
    default:
        goto invalid_symbol;
    }

    if (s->values == NULL) {
        if ((s->values = smpl_alloct(typeof(*s->values))) == NULL)
            goto nomem;

        smpl_list_init(s->values);
    }

    smpl_list_init(&v->hook);
    smpl_list_prepend(s->values, &v->hook);

    return s;

 no_symbol:
    smpl_fail(NULL, smpl, ENOENT, "no symbol with id 0x%x", sym);

 already_set:
    smpl_fail(NULL, smpl, EBUSY, "external data already set");

 invalid_symbol:
    smpl_fail(NULL, smpl, EINVAL, "can't set value for symbol 0x%x", sym);

 nomem:
    return NULL;
}


int symtbl_push(smpl_t *smpl, smpl_sym_t sym, smpl_value_type_t type, void *val)
{
    smpl_value_t *v;

    if (sym == 0)
        return 0;

    v = smpl_alloct(typeof(*v));

    if (v == NULL)
        goto nomem;

    smpl_list_init(&v->hook);
    v->type = type;

    switch (v->type) {
    case SMPL_VALUE_STRING:
        v->str = val;
        break;
    case SMPL_VALUE_INTEGER:
        v->i32 = *(int32_t *)val;
        break;
    case SMPL_VALUE_DOUBLE:
        v->dbl = *(double *)val;
        break;
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        v->json = val;
        break;
    default:
        goto invalid_value;
    }

    if (push_value(smpl, sym, v) == NULL)
        goto failed;

    return 0;

 invalid_value:
    smpl_free(v);
    smpl_fail(-1, smpl, EINVAL, "invalid value, type 0x%x, sym 0x%x", type, sym);

 nomem:
 failed:
    return -1;
}


int symtbl_push_loop(smpl_t *smpl, smpl_sym_t sym,
                     smpl_value_type_t type, void *val, int *loopflags)
{
    smpl_value_t *v;

    if (sym == 0)
        return 0;

    v = smpl_alloct(typeof(*v));

    if (v == NULL)
        goto nomem;

    smpl_list_init(&v->hook);
    v->type = type;

    switch (v->type) {
    case SMPL_VALUE_STRING:
        v->str = val;
        break;
    case SMPL_VALUE_INTEGER:
        v->i32 = *(int32_t *)val;
        break;
    case SMPL_VALUE_DOUBLE:
        v->dbl = *(double *)val;
        break;
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        v->json = val;
        break;
    default:
        goto invalid_value;
    }

    v->loopflags = loopflags;

    if (push_value(smpl, sym, v) == NULL)
        goto failed;

    return 0;

 invalid_value:
    smpl_free(v);
    smpl_fail(-1, smpl, EINVAL, "invalid value, type 0x%x, sym 0x%x", type, sym);

 failed:
    smpl_free(v);
 nomem:
    return -1;
}


int symtbl_loopflag(smpl_t *smpl, smpl_sym_t sym, int flag)
{
    smpl_symbol_t *s;
    smpl_value_t  *v;

    s = symtbl_entry(smpl, sym);

    if (s == NULL || s->values == NULL || smpl_list_empty(s->values))
        return 0;

    v = smpl_list_entry(s->values->prev, typeof(*v), hook);

    if (v->loopflags == NULL)
        return 0;

    return *v->loopflags & flag;
}


int symtbl_pop(smpl_t *smpl, smpl_sym_t sym)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    smpl_value_t  *v;
    int            idx, tag;

    if (sym == 0)
        return 0;

    tag = SMPL_SYMBOL_TAG(sym);
    idx = SMPL_SYMBOL_IDX(sym);

    if (idx < 0 || idx >= tbl->nsymbol)
        goto no_symbol;

    switch (tag) {
    case SMPL_SYMBOL_DATA:
    case SMPL_SYMBOL_FIELD:
    case SMPL_SYMBOL_LOOP:
    case SMPL_SYMBOL_ARG:
        break;
    default:
        goto invalid_symbol;
    }

    s = tbl->symbols + idx;

    if (s->values == NULL || smpl_list_empty(s->values))
        goto no_values;

    v = smpl_list_entry(s->values->next, typeof(*v), hook);

    switch (v->type) {
    default:
        break;
    }

    smpl_list_delete(&v->hook);
    smpl_free(v);

    return 0;

 no_symbol:
    smpl_fail(-1, smpl, ENOENT, "no symbol with id 0x%x", sym);

 invalid_symbol:
    smpl_fail(-1, smpl, EINVAL, "can't get value for symbol 0x%x", sym);

 no_values:
    smpl_fail(-1, smpl, ENOENT, "no value to pop for symbol 0x%x", sym);
}


int symbtl_value(smpl_t *smpl, smpl_sym_t sym, smpl_value_t *val)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    smpl_value_t  *v;
    int            idx, tag;

    tag = SMPL_SYMBOL_TAG(sym);
    idx = SMPL_SYMBOL_IDX(sym);

    if (idx < 0 || idx >= tbl->nsymbol)
        goto no_symbol;

    s = tbl->symbols + idx;

    switch (tag) {
    case SMPL_SYMBOL_DATA:
    case SMPL_SYMBOL_FIELD:
    case SMPL_SYMBOL_LOOP:
        break;
    case SMPL_SYMBOL_STRING:
        val->type = SMPL_VALUE_STRING;
        val->str  = s->symbol;
        goto out;
    default:
        goto invalid_symbol;
    }

    if (s->values == NULL || smpl_list_empty(s->values))
        goto no_values;

    v = smpl_list_entry(s->values->next, typeof(*v), hook);

    val->type = v->type;

    switch (val->type) {
    case SMPL_VALUE_STRING:
        val->str = v->str;
        break;
    case SMPL_VALUE_INTEGER:
        val->i32 = v->i32;
        break;
    case SMPL_VALUE_DOUBLE:
        val->dbl = v->dbl;
        break;
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        val->json = v->json;
        break;
    default:
        goto invalid_value;
    }

 out:
    return val->type;

 no_symbol:
    return (val->type = SMPL_VALUE_UNSET);

 invalid_symbol:
    val->type = -1;
    smpl_fail(-1, smpl, EINVAL, "can't get value for symbol 0x%x", sym);

 no_values:
    return (val->type =  SMPL_VALUE_UNSET);

 invalid_value:
    val->type = -1;
    smpl_fail(-1, smpl, EINVAL, "invalid value for symbol 0x%x", sym);
}


int symtbl_resolve(smpl_t *smpl, smpl_varref_t *vref, smpl_value_t *val)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_sym_t    *sym;
    smpl_symbol_t *s;
    smpl_value_t  *v;
    int            i, idx, tag;

    sym = vref->symbols;
    tag = SMPL_SYMBOL_TAG(*sym);
    idx = SMPL_SYMBOL_IDX(*sym);

    if (idx < 0 || idx >= tbl->nsymbol)
        goto no_symbol;

    s = tbl->symbols + idx;

    switch (tag) {
    case SMPL_SYMBOL_LOOP:
    case SMPL_SYMBOL_FIELD:
        break;
    default:
        goto invalid_symbol;
    }

    if (s->values == NULL || smpl_list_empty(s->values))
        goto no_values;

    v = smpl_list_entry(s->values->next, typeof(*v), hook);

    switch (val->type = v->type) {
    case SMPL_VALUE_STRING:
        val->str = v->str;
        break;
    case SMPL_VALUE_INTEGER:
        val->i32 = v->i32;
        break;
    case SMPL_VALUE_DOUBLE:
        val->dbl = v->dbl;
        break;
    case SMPL_VALUE_OBJECT:
    case SMPL_VALUE_ARRAY:
        val->json = v->json;
        break;
    default:
        goto invalid_value;
    }

    for (i = 1, sym++; i < vref->nsymbol; i++, sym++) {
        tag = SMPL_SYMBOL_TAG(*sym);
        idx = SMPL_SYMBOL_IDX(*sym);

        s = tbl->symbols + idx;

        switch (tag) {
        case SMPL_SYMBOL_FIELD:
            if (val->type != SMPL_VALUE_OBJECT)
                goto invalid_value;

            val->json = smpl_json_get(val->json, s->symbol);

            if (val->json == NULL)
                goto no_values;
            break;

        case SMPL_SYMBOL_INDEX:
            if (val->type != SMPL_VALUE_ARRAY)
                goto invalid_value;

            val->json = smpl_json_array_get(val->json, idx);

            if (val->json == NULL)
                goto no_values;
            break;

        default:
            goto invalid_value;
        }

        switch (smpl_json_type(val->json)) {
        case SMPL_JSON_ARRAY:
            val->type = SMPL_VALUE_ARRAY;
            break;
        case SMPL_JSON_OBJECT:
            val->type = SMPL_VALUE_OBJECT;
            break;
        case SMPL_JSON_STRING:
            val->type = SMPL_VALUE_STRING;
            val->str = (char *)smpl_json_string_value(val->json);
            break;
        case SMPL_JSON_INTEGER:
            val->type = SMPL_VALUE_INTEGER;
            val->i32  = smpl_json_integer_value(val->json);
            break;
        case SMPL_JSON_DOUBLE:
            val->type = SMPL_VALUE_DOUBLE;
            val->dbl  = smpl_json_double_value(val->json);
            break;
        case SMPL_JSON_BOOLEAN:
            val->type = SMPL_VALUE_INTEGER;
            val->i32  = smpl_json_boolean_value(val->json) ? 1 : 0;
            break;
        default:
            goto invalid_value;
        }
    }

    return val->type;

 no_symbol:
    return (val->type = SMPL_VALUE_UNSET);

 no_values:
    return (val->type = SMPL_VALUE_UNSET);

 invalid_symbol:
    val->type = -1;
    smpl_fail(-1, smpl, EINVAL, "can't get value for symbol 0x%x", *sym);

 invalid_value:
    val->type = -1;
    smpl_fail(-1, smpl, EINVAL, "invalid value for symbol 0x%x", *sym);
}


void symtbl_flush(smpl_t *smpl)
{
    smpl_symtbl_t *tbl = smpl->symtbl;
    smpl_symbol_t *s;
    int            i;

    if (tbl == NULL)
        return;

    for (s = tbl->symbols, i = 0; i < tbl->nsymbol; i++, s++) {
        if (s->values == NULL)
            continue;

        while (!smpl_list_empty(s->values))
            symtbl_pop(smpl, SMPL_SYMBOL_FIELD | i);
    }
}


