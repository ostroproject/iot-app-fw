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



static jmpl_ref_t *parse_reference(char *val)
{
    jmpl_ref_t *r;
    int32_t    *ids, id;
    int         nid;
    char       *p, *b, *e, *n, *end, *dot, *idx;

    /*
     * Parse a JSON variable reference into an internal representation.
     *
     * Internally we represent variable references as a series of tagged
     * ids. Each id contains a tag and an index. The tag denotes what
     * type of information is encoded into the index: a string (field or
     * string index), or an integer index. Values of strings are interned
     * into a symbol table for fast lookup, and the symbol table index is
     * used as the index for id. Integer indices are used as such as the
     * index for id.
     */

    ids = NULL;
    nid = 0;

    p = val;
    while (p && *p) {
        iot_debug("@ '%s'", p);

        dot = strchr(p + 1, '.');
        idx = strchr(p + 1, '[');

        switch (*p) {
        case '.':
        default:
            b = (*p == '.' ? p + 1 : p);

            if (dot && idx) {
                if (dot < idx)
                    idx = NULL;
                else
                    dot = NULL;
            }

            if (dot)
                *dot = '\0';
            else if (idx)
                *idx = '\0';

            id = symtab_add(b, JMPL_SYMBOL_FIELD);

            if (id < 0)
                goto fail;

            iot_debug("symbol '%s' => 0x%x", b, id);

            if (dot) {
                *dot = '.';
                p    = dot;
            }
            else if (idx) {
                *idx = '[';
                p    = idx;
            }
            else
                p = NULL;

            break;

        case '[':
            b = p + 1;
            e = strchr(b, ']');

            if (e == NULL || e == b)
                goto invalid;

            n = e + 1;

            if (*b == '\'' || *b == '"') {
                e--;

                *e = '\0';
                id = symtab_add(b + 1, JMPL_SYMBOL_FIELD);
                iot_debug("symbol '%s' => 0x%x", b + 1, id);
                *e = *b;

                if (id < 0)
                    goto fail;

            }
            else {
                id = strtol(b, &end, 10);

                if (id < 0 || end != e)
                    goto invalid;

                iot_debug("index '%.*s' => 0x%x", (int)(e - b), b, id);
            }

            p = n;
            break;
        }

        if (iot_reallocz(ids, nid, nid + 1) == NULL)
            goto fail;

        ids[nid++] = id;
    }

    if (ids == NULL)
        goto invalid;

    r = iot_allocz(sizeof(*r));

    if (r == NULL)
        goto fail;

    r->ids = ids;
    r->nid = nid;

    return r;

 invalid:
    errno = EINVAL;
 fail:
    iot_free(ids);
    return NULL;
}


static inline void *jmpl_alloc(int type, size_t size)
{
    jmpl_any_t *jmpl;

    iot_debug("allocating instruction of type 0x%x, size %zd", type, size);

    jmpl = iot_allocz(size);

    if (jmpl == NULL)
        return NULL;

    jmpl->type = type;
    iot_list_init(&jmpl->hook);

    return jmpl;
}


static int parser_init(jmpl_parser_t *jp, const char *str)
{
    char *p, *end;

    iot_clear(jp);
    iot_list_init(&jp->templates);

    jp->jmpl = jmpl_alloc(JMPL_OP_MAIN, sizeof(*jp->jmpl));

    if (jp->jmpl == NULL)
        goto nomem;

    jp->insns = &jp->jmpl->hook;

    /*
     * Every JSON template file starts with the declaration of the
     * directive markers. The directive markers are used to delimit
     * template directives throughout the template. The directive
     * marker declaration is the first line of the template and is
     * a single line consisting of:
     *
     *   - the beginning marker,
     *   - the end marker,
     *   - an optional tabulation marker,
     *
     * All of these are separated from each other by whitespace.
     * single whitespace.
     *
     * Any character string is allowed as a marker with the following
     * limitations:
     *
     *   - a marker cannot contain any whitespace or a newline
     *   - a marker cannot be a substring of the other markers
     */

    str = skip_whitespace((char *)str, true);
    end = next_newline((char *)str);

    if (!str || !*str || !end || !*end)
        goto invalid;

    end++;

    jp->buf    = iot_strdup(end);
    jp->tokens = iot_allocz(strlen(end) + 1);
    jp->mbeg   = iot_strndup(str, end - str - 1);

    if (jp->buf == NULL || jp->tokens == NULL || jp->mbeg == NULL)
        goto nomem;

    p = jp->mbeg;
    p = next_whitespace(p, false);

    if (!*p)
        goto invalid;

    *p++ = '\0';

    p = skip_whitespace(p, false);

    if (!*p)
        goto invalid;

    jp->mend = p;

    p = next_whitespace(p, false);

    if (*p) {
        *p++ = '\0';

        p = skip_whitespace(p, false);

        if (*p)
            jp->mtab = p;

        p = next_whitespace(p, false);

        if (*p)
            *p = '\0';
    }

    jp->lbeg = strlen(jp->mbeg);
    jp->lend = strlen(jp->mend);
    jp->ltab = jp->mtab ? strlen(jp->mtab) : 0;

    jp->p = jp->buf;
    jp->t = jp->tokens;

    return 0;

 invalid:
    jp->error = "invalid directive markers, or no template data";
    errno = EINVAL;
 nomem:
    iot_free(jp->buf);
    iot_free(jp->tokens);
    iot_free(jp->mbeg);

    return -1;
}


static void parser_exit(jmpl_parser_t *jp)
{
    iot_free(jp->mbeg);
    iot_free(jp->buf);
    iot_free(jp->tokens);
}


static int parse_if(jmpl_parser_t *jp);
static int parse_ifset(jmpl_parser_t *jp);
static int parse_foreach(jmpl_parser_t *jp);
static int parse_subst(jmpl_parser_t *jp, char *val);
static int parse_text(jmpl_parser_t *jp, char *val);


static inline iot_list_hook_t *parser_enter(jmpl_parser_t *jp, jmpl_any_t *any)
{
    iot_list_hook_t *prev;

    iot_list_append(jp->insns, &any->hook);

    prev = jp->insns;
    jp->insns = &any->hook;

    return prev;
}


static inline void parser_restore(jmpl_parser_t *jp, iot_list_hook_t *prev)
{
    jp->insns = prev;
}


static int parse_ifset(jmpl_parser_t *jp)
{
    iot_list_hook_t *prev;
    jmpl_ifset_t    *jif;
    char            *val;
    int              tkn, ebr;

    iot_debug("<if-set>");

    jif = jmpl_alloc(JMPL_OP_IFSET, sizeof(*jif));

    if (jif == NULL)
        return -1;

    iot_list_init(&jif->tbranch);
    iot_list_init(&jif->fbranch);
    prev = parser_enter(jp, (jmpl_any_t *)jif);

    tkn = scan_next_token(jp, &val, SCAN_ID);

    if (tkn != JMPL_TKN_ID)
        goto missing_id;

    iot_debug("<id> '%s'", val);

    ebr = false;

    while ((tkn = scan_next_token(jp, &val, SCAN_IF_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_ELSE:
            iot_debug("<else>");

            if (ebr)
                goto unexpected_else;

            ebr = true;
            break;

        case JMPL_TKN_IFSET:
            if (parse_ifset(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                goto parse_error;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            if (parse_text(jp, val) < 0)
                goto parse_error;
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    parser_restore(jp, prev);

    return 0;

 missing_id:
 unexpected_else:
 unexpected_token:
 invalid_reference:

 parse_error:
    parser_restore(jp, prev);

    return -1;
}


static int parse_if(jmpl_parser_t *jp)
{
    iot_list_hook_t *prev;
    jmpl_if_t       *jif;
    char            *val;
    int              tkn, ebr, lvl;

    iot_debug("<if>");

    jif = jmpl_alloc(JMPL_OP_IF, sizeof(*jif));

    if (jif == NULL)
        return -1;

    iot_list_init(&jif->tbranch);
    iot_list_init(&jif->fbranch);
    prev = parser_enter(jp, (jmpl_any_t *)jif);

    tkn = scan_next_token(jp, &val, SCAN_IF_EXPR);

    if (tkn != JMPL_TKN_OPEN)
        goto missing_open;

    lvl = 1;
    while (lvl > 0) {
        tkn = scan_next_token(jp, &val, SCAN_IF_EXPR);

        switch (tkn) {
        case JMPL_TKN_OPEN:
            lvl++;
            iot_debug("(");
            break;

        case JMPL_TKN_CLOSE:
            iot_debug(")");
            lvl--;
            break;

        case JMPL_TKN_STRING:
            iot_debug("<string> '%s'", val);
            break;

        case JMPL_TKN_AND:
            iot_debug("&&");
            break;

        case JMPL_TKN_OR:
            iot_debug("||");
            break;

        case JMPL_TKN_NOT:
            iot_debug("!");
            break;

        case JMPL_TKN_NEQ:
            iot_debug("!=");
            break;

        case JMPL_TKN_EQ:
            iot_debug("==");
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;

        default:
            goto unexpected_token;
        }
    }

    ebr = false;

    while ((tkn = scan_next_token(jp, &val, SCAN_IF_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_ELSE:
            iot_debug("<else>");

            if (ebr)
                goto unexpected_else;

            ebr = true;
            break;

        case JMPL_TKN_IFSET:
            if (parse_ifset(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                goto parse_error;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            if (parse_text(jp, val) < 0)
                goto parse_error;
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    parser_restore(jp, prev);

    return 0;

 missing_open:
 unexpected_else:
 unexpected_token:
 invalid_reference:

 parse_error:
    parser_restore(jp, prev);

    return -1;
}


static int parse_foreach(jmpl_parser_t *jp)
{
    iot_list_hook_t *prev;
    jmpl_for_t      *jfor;
    char            *val;
    int              tkn;

    iot_debug("<foreach>");

    jfor = jmpl_alloc(JMPL_OP_FOREACH, sizeof(*jfor));

    if (jfor == NULL)
        return -1;

    iot_list_init(&jfor->body);
    prev = parser_enter(jp, (jmpl_any_t *)jfor);


    if ((tkn = scan_next_token(jp, &val, SCAN_ID)) != JMPL_TKN_ID)
        goto missing_id;

    iot_debug("<id> '%s'", val);

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_IN)
        goto missing_in;

    iot_debug("<in>");

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_SUBST)
        goto missing_reference;

    iot_debug("<subst> '%s'", val);

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_DO)
        goto missing_do;

    iot_debug("<do>");

    while ((tkn = scan_next_token(jp, &val, SCAN_FOREACH_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_IFSET:
            if (parse_ifset(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                goto parse_error;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;
        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            if (parse_text(jp, val) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_EOF:
            goto unexpected_eof;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    parser_restore(jp, prev);

    return 0;

 missing_id:
 missing_in:
 missing_reference:
 missing_do:
 invalid_reference:
 unexpected_eof:
 unexpected_token:

 parse_error:
    errno = EINVAL;

    parser_restore(jp, prev);
    return -1;
}


static int parse_escape(jmpl_parser_t *jp, char *val)
{
    jmpl_text_t *jt;

    iot_debug("<escape> '%s'", val);

    if (val[0] != '\\' || val[1] == '\0')
        return -1;

    switch (val[1]) {
    case 'n': val = "\n"; break;
    case 't': val = "\t"; break;
    case ' ': val = " ";  break;
    default:
        return -1;
    }

    jt = iot_allocz(sizeof(*jt));

    if (jt == NULL)
        goto nomem;

    iot_list_init(&jt->hook);
    jt->type = JMPL_OP_TEXT;
    jt->text = iot_strdup(val);

    if (jt->text == NULL)
        goto nomem;

    iot_list_append(jp->insns, &jt->hook);

    return 0;

 nomem:
    iot_free(jt);
    return -1;
}


static int parse_subst(jmpl_parser_t *jp, char *val)
{
    jmpl_subst_t *js;

    if (val[0] == '\\')
        return parse_escape(jp, val);

    iot_debug("<subst> '%s'", val);

    js = iot_allocz(sizeof(*js));

    if (js == NULL)
        return -1;

    iot_list_init(&js->hook);
    js->type = JMPL_OP_SUBST;
    js->ref  = parse_reference(val);

    if (js->ref == NULL)
        goto noref;

    iot_list_append(jp->insns, &js->hook);

    return 0;

 noref:
    iot_free(js);
    return -1;
}


static int parse_text(jmpl_parser_t *jp, char *val)
{
    jmpl_text_t *jt;

    iot_debug("<text> '%s'", val);

    jt = iot_allocz(sizeof(*jt));

    if (jt == NULL)
        goto nomem;

    iot_list_init(&jt->hook);
    jt->type = JMPL_OP_TEXT;
    jt->text = iot_strdup(val);

    if (jt->text == NULL)
        goto nomem;

    iot_list_append(jp->insns, &jt->hook);

    return 0;

 nomem:
    iot_free(jt);
    return -1;
}


jmpl_t *jmpl_parse(const char *str)
{
    jmpl_parser_t  jp;
    char          *val;
    int            tkn;

    if (parser_init(&jp, str) < 0)
        return NULL;

    iot_debug("begin marker: '%s'", jp.mbeg);
    iot_debug("  end marker: '%s'", jp.mend);
    iot_debug("  tab marker: '%s'", jp.mtab ? jp.mtab : "<none>");
    iot_debug("    template: %s"  , jp.buf);


    while ((tkn = scan_next_token(&jp, &val, SCAN_MAIN)) != JMPL_TKN_EOF) {
        switch (tkn) {
        case JMPL_TKN_ERROR:
        case JMPL_TKN_UNKNOWN:
            goto parse_error;

        case JMPL_TKN_IFSET:
            if (parse_ifset(&jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_IF:
            if (parse_if(&jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(&jp) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_SUBST:
            if (parse_subst(&jp, val) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_TEXT:
            if (parse_text(&jp, val) < 0)
                goto parse_error;
            break;

        default:
            goto parse_error;
        }
    }

    return (jmpl_t *)jp.jmpl;

 parse_error:
    parser_exit(&jp);
    errno = EINVAL;
    return NULL;
}

