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


static char *skip_whitespace(char **strp)
{
    char *p = *strp;

    while (*p == ' ' || *p == '\t')
        p++;

    *strp = p;

    return p;
}


static char *trim_whitespace(char **begp, char **endp)
{
    char *b, *e;

    b = *begp;
    e = endp ? *endp : NULL;

    while ((!e || b < e) && (*b == ' ' || *b == '\t'))
        b++;
    if (e)
        while (e > b && (*e == ' ' || *b == '\t'))
            e--;

    *begp = b;
    if (e)
        *endp = e;

    return b;
}


static char *find_end(char *p)
{
    if (*p == '[')
        while (*p && *p != ']')
            p++;
    else
        while (*p && (*p != '[' && *p != '.'))
            p++;

    return p;
}


smpl_varref_t *varref_parse(smpl_t *smpl, char *str, const char *path, int line)
{
    smpl_varref_t *vref;
    smpl_alias_t  *a;
    smpl_sym_t    *syms, sym;
    int            nsym;
    char          *p, *n, *b, *e, name[256], unaliased[4096];
    int            l, len;

    SMPL_UNUSED(path);
    SMPL_UNUSED(line);

    smpl_debug("varref '%s'", str);

    e = strchr(str, '.');
    if (e != NULL)
        len = e - str;
    else
        len = -1;

    a = varref_find_alias(smpl, str, len);

    if (a != NULL) {
        if (e != NULL)
            l = snprintf(unaliased, sizeof(unaliased), "%s%s", a->value, e);
        else
            l = snprintf(unaliased, sizeof(unaliased), "%s", a->value);

        if (l >= (int)sizeof(unaliased))
            goto unaliasedtoolong;

        smpl_debug("unaliased varref: '%s' = '%s'", str, unaliased);

        return varref_parse(smpl, unaliased, path, line);
    }

    vref = NULL;
    syms = NULL;
    nsym = 0;
    p    = str;

    while (*p) {
        skip_whitespace(&p);
        n = find_end(p);

        if (*p == '[') {
            if (*n != ']')
                goto unterm_index;

            b = p + 1;
            e = n - 1;
            trim_whitespace(&b, &e);

            if (*b == '\'' || *b == '"') {
                if (*e != *b)
                    goto invalid_index;
                b++;
                e--;
            }
        }
        else {
            if (*n && *n != '.' && *n != '[')
                goto invalid_name;

            b = p;
            e = n - 1;
            trim_whitespace(&b, &e);
        }

        if (e < b)
            goto invalid_index;
        l = e - b + 1;
        if (l >= (int)sizeof(name) - 1)
            goto nametoolong;

        strncpy(name, b, l);
        name[l] = '\0';

        sym = symtbl_add(smpl, name, SMPL_SYMBOL_FIELD);

        smpl_debug("symbol '%s' => 0x%x", name, sym);

        if (sym < 0)
            goto invalid_index;

        if (!smpl_reallocz(syms, nsym, nsym + 1))
            goto nomem;

        syms[nsym++] = sym;

        switch (*n) {
        case '.':
            p = n + 1;
            break;
        case ']':
            p = n + 1;
            if (*p == '.')
                p++;
            break;
        default:
            p = n;
            break;
        }
    }

    vref = smpl_alloct(typeof(*vref));

    if (vref == NULL)
        goto nomem;

    vref->symbols = syms;
    vref->nsymbol = nsym;

    return vref;

 nomem:
    smpl_free(syms);
    smpl_free(vref);
    return NULL;

 unterm_index:
    smpl_fail(NULL, smpl, EINVAL, "unterminated index ('%s')", str);

 invalid_index:
    smpl_fail(NULL, smpl, EINVAL, "invalid index ('%s')", str);

 invalid_name:
    smpl_fail(NULL, smpl, EINVAL, "invalid name ('%s')", name);

 nametoolong:
    smpl_fail(NULL, smpl, ENAMETOOLONG, "name overflow ('%*.*s')", l, l, b);

 unaliasedtoolong:
    smpl_fail(NULL, smpl, ENAMETOOLONG, "unaliased varref too long");
}


void varref_free(smpl_varref_t *vref)
{
    if (vref == NULL)
        return;

    smpl_free(vref->symbols);
    smpl_free(vref);
}


char *varref_print(smpl_t *smpl, smpl_varref_t *vref, char *buf, size_t size)
{
    smpl_sym_t  sym;
    char       *p;
    int         i, l, n;

    l = (int)size;
    p = buf;

    for (i = 0; i < vref->nsymbol; i++) {
        sym = vref->symbols[i];

        if (SMPL_SYMBOL_TAG(sym) == SMPL_SYMBOL_INDEX)
            n = snprintf(p, l, "[%d]", sym);
        else
            n = snprintf(p, l, "%s%s", i > 0 ? "." : "", symtbl_get(smpl, sym));

        if (n < 0 || n >= l)
            return "<varref_print() failed>";

        p += n;
        l -= n;
    }

    *p = '\0';
    return buf;
}


int varref_value(smpl_t *smpl, smpl_varref_t *vref, smpl_value_t *v)
{
    return symtbl_resolve(smpl, vref, v);
}


char *varref_string(smpl_t *smpl, smpl_varref_t *vref, char *buf, size_t size)
{
    smpl_value_t v;
    int          n;

    switch (symtbl_resolve(smpl, vref, &v)) {
    case SMPL_VALUE_UNKNOWN:
    case SMPL_VALUE_UNSET:
        *buf = '\0';
        n    = 0;
        break;
    case SMPL_VALUE_STRING:
        n = snprintf(buf, size, "%s", v.str);
        break;
    case SMPL_VALUE_INTEGER:
        n = snprintf(buf, size, "%d", v.i32);
        break;
    case SMPL_VALUE_DOUBLE:
        n = snprintf(buf, size, "%f", v.dbl);
        break;
    default:
        n = snprintf(buf, size, "<invalid value (type 0x%x)>", v.type);
        break;
    }

    if (n < 0 || n >= (int)size)
        goto overflow;

    return buf;

 overflow:
    smpl_fail(NULL, smpl, EOVERFLOW, "no buffer space for varref value");
}


smpl_alias_t *varref_find_alias(smpl_t *smpl, const char *name, int len)
{
    smpl_list_t  *p, *n;
    smpl_alias_t *a;

    smpl_list_foreach(&smpl->aliasen, p, n) {
        a = smpl_list_entry(p, typeof(*a), hook);

        if ((len < 0 && !strcmp(name, a->name)) ||
            (len > 0 && !strncmp(name, a->name, len) && name[len] == '.'))
            return a;
    }

    return NULL;
}


int varref_add_alias(smpl_t *smpl, const char *name, const char *value)
{
    smpl_alias_t *a;

    if (varref_find_alias(smpl, name, -1) != NULL)
        goto already_defined;

    a = smpl_alloct(typeof(*a));
    if (a == NULL)
        goto nomem;

    smpl_list_init(&a->hook);
    a->name  = smpl_strdup(name);
    a->value = smpl_strdup(value);

    if (a->name == NULL || a->value == NULL)
        goto nomem;

    smpl_list_append(&smpl->aliasen, &a->hook);

    smpl_debug("added varref alias '%s' ('%s')", name, value);

    return 0;

 already_defined:
    smpl_fail(-1, smpl, EBUSY, "macro (alias) '%s' already defined", name);

 nomem:
    if (a != NULL) {
        smpl_free(a->name);
        smpl_free(a->value);
        smpl_free(a);
    }
    return -1;
}


void varref_purge_aliasen(smpl_t *smpl)
{
    smpl_list_t  *p, *n;
    smpl_alias_t *a;

    smpl_list_foreach(&smpl->aliasen, p, n) {
        a = smpl_list_entry(p, typeof(*a), hook);

        smpl_list_delete(&a->hook);
        smpl_free(a->name);
        smpl_free(a->value);
        smpl_free(a);
    }
}
