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



static jmpl_ref_t *parse_reference(jmpl_parser_t *jp, char *val)
{
    jmpl_ref_t *r;
    int32_t    *ids, id;
    int         nid;
    char       *p, *b, *e, *n, *end, *dot, *idx;

    iot_debug("reference '%s'", val);

    ids = NULL;
    nid = 0;

    p = val;
    while (p && *p) {
        iot_debug("parsing @ '%s'", p);

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

            iot_debug("adding symbol '%s'", b);
            id = symtab_add(b, JMPL_SYMBOL_FIELD);

            if (id < 0)
                goto fail;

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
                iot_debug("adding symbol '%s'", b + 1);
                id = symtab_add(b + 1, JMPL_SYMBOL_FIELD);
                *e = *b;
            }
            else {
                id = strtol(b, &end, 10);

                if (id < 0 || end != e)
                    goto invalid;
            }

            p = n;
            break;
        }

        if (iot_reallocz(ids, nid, nid + 1) == NULL)
            goto fail;

        iot_debug("id #%d: 0x%x", nid, id);
        ids[nid++] = id;
    }

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


static int parser_init(jmpl_parser_t *jp, const char *str)
{
    char *p, *end;

    iot_clear(jp);
    iot_list_init(&jp->templates);

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


static int parse_ifset(jmpl_parser_t *jp)
{
    char *val;
    int   tkn, ebr;

    iot_debug("<if-set>");

    tkn = scan_next_token(jp, &val, SCAN_ID);

    if (tkn != JMPL_TKN_ID)
        return -1;

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
                return -1;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                return -1;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                return -1;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;
        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return 0;

 unexpected_else:
    return -1;

 unexpected_token:
    return -1;

 invalid_reference:
    return -1;
}


static int parse_if(jmpl_parser_t *jp)
{
    char *val;
    int   tkn, ebr, lvl;

    iot_debug("<if>");

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
                return -1;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                return -1;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                return -1;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                goto invalid_reference;
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return 0;

 missing_open:
    return -1;

 unexpected_else:
    return -1;

 unexpected_token:
    return -1;

 invalid_reference:
    return -1;
}


static int parse_foreach(jmpl_parser_t *jp)
{
    char *val;
    int   tkn;

    iot_debug("<foreach>");

    if ((tkn = scan_next_token(jp, &val, SCAN_ID)) != JMPL_TKN_ID)
        return -1;

    iot_debug("<id> '%s'", val);

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_IN)
        return -1;

    iot_debug("<in>");

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_SUBST)
        return -1;

    iot_debug("<subst> '%s'", val);

    if ((tkn = scan_next_token(jp, &val, SCAN_FOREACH)) != JMPL_TKN_DO)
        return -1;

    iot_debug("<do>");

    while ((tkn = scan_next_token(jp, &val, SCAN_FOREACH_BODY)) != JMPL_TKN_END) {
        switch (tkn) {
        case JMPL_TKN_END:
            iot_debug("<end>");
            return 0;

        case JMPL_TKN_IFSET:
            if (parse_ifset(jp) < 0)
                return -1;
            break;

        case JMPL_TKN_IF:
            if (parse_if(jp) < 0)
                return -1;
            break;

        case JMPL_TKN_FOREACH:
            if (parse_foreach(jp) < 0)
                return -1;

        case JMPL_TKN_SUBST:
            iot_debug("<subst> '%s'", val);
            if (parse_subst(jp, val) < 0)
                return -1;

            break;
        case JMPL_TKN_TEXT:
            iot_debug("<text> '%s'", val);
            break;

        default:
            goto unexpected_token;
        }
    }

    iot_debug("<end>");

    return 0;

 unexpected_else:
    return -1;

 unexpected_token:
    return -1;

 invalid:
    return -1;
}


static int parse_subst(jmpl_parser_t *jp, char *val)
{
    jmpl_subst_t *js;

    iot_debug("<subst> '%s'", val);

    js = iot_allocz(sizeof(*js));

    if (js == NULL)
        goto nomem;

    iot_list_init(&js->hook);
    js->type = JMPL_OP_SUBST;
    js->ref  = parse_reference(jp, val);

    if (js->ref == NULL)
        goto noref;

    return 0;

 nomem:
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

    return 0;

 nomem:
    iot_free(jt);

    return -1;
}


jmpl_t *jmpl_parse(const char *str)
{
    jmpl_parser_t  jp;
    jmpl_t        *j;
    char          *val;
    int            len, tkn;

    if (parser_init(&jp, str) < 0)
        return NULL;

    j = iot_allocz(sizeof(*j));

    if (j == NULL)
        goto nomem;

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

    return j;

 nomem:
    parser_exit(&jp);
    return NULL;

 parse_error:
    parser_exit(&jp);
    errno = EINVAL;
    return NULL;
}

