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
    smpl_sym_t    *syms, sym;
    int            nsym;
    char          *p, *n, *b, *e, name[256];
    int            l;

    SMPL_UNUSED(path);
    SMPL_UNUSED(line);

    smpl_debug("varref '%s'", str);

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
