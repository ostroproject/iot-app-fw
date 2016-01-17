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



int scan_directive(jmpl_parser_t *jp, char **valp)
{
    char *b, *e, *p, *t;
    size_t n;
    int tkn;

    p = jp->p;
    t = jp->t;
    *valp = t;

    if (!strncmp(p, jp->mbeg, jp->lbeg)) {
        b = p + jp->lbeg;
        e = strstr(b, jp->mend);

        if (e == NULL)
            goto parse_error;

        n = e - b;
        strncpy(t, b, n);
        t += n;
        *t++ = '\0';

        p += jp->lbeg + n + jp->lend;

        if (!strcmp(*valp, "if-set"))
            tkn = JMPL_TKN_IFSET;
        else if (!strcmp(*valp, "end"))
            tkn = JMPL_TKN_END;
        else if (!strcmp(*valp, "if"))
            tkn = JMPL_TKN_IF;
        else if (!strcmp(*valp, "else"))
            tkn = JMPL_TKN_ELSE;
        else if (!strcmp(*valp, "foreach"))
            tkn = JMPL_TKN_FOREACH;
        else if (!strcmp(*valp, "in"))
            tkn = JMPL_TKN_IN;
        else if (!strcmp(*valp, "do"))
            tkn = JMPL_TKN_DO;
        else
            tkn = JMPL_TKN_SUBST;

        jp->p = p;
        jp->t = t;

        return tkn;
    }
    else
        return JMPL_TKN_UNKNOWN;

 parse_error:
    return JMPL_TKN_ERROR;
}


int scan_next_token(jmpl_parser_t *jp, char **valp, int options)
{
    char *b, *e, *p, *q, *t;
    size_t n;
    int tkn;

    if (jp->tkn != JMPL_TKN_UNKNOWN) {
        tkn   = jp->tkn;
        *valp = jp->val;

        jp->tkn = JMPL_TKN_UNKNOWN;
        jp->val = NULL;

        return tkn;
    }

    switch (options) {
    case SCAN_IF_EXPR:
    case SCAN_FOREACH:
    case SCAN_ID:
        parser_skip_whitespace(jp, true);
    default:
        break;
    }

    p = jp->p;
    t = jp->t;
    *valp = t;

    if (!*p)                                     /* end of input */
        goto eof;

    /*iot_debug("p: '%s'", jp->p);*/

#if 0
 try_directive:
    if (!strncmp(p, jp->mbeg, jp->lbeg)) {       /* at a directive */
    scan_directive:
        b = p + jp->lbeg;
        e = strstr(b, jp->mend);

        if (e == NULL)
            goto parse_error;

        n = e - b;
        strncpy(t, b, n);
        t += n;
        *t++ = '\0';

        p += jp->lbeg + n + jp->lend;

        if (!strcmp(*valp, "if-set"))
            tkn = JMPL_TKN_IFSET;
        else if (!strcmp(*valp, "end"))
            tkn = JMPL_TKN_END;
        else if (!strcmp(*valp, "if"))
            tkn = JMPL_TKN_IF;
        else if (!strcmp(*valp, "else"))
            tkn = JMPL_TKN_ELSE;
        else if (!strcmp(*valp, "foreach"))
            tkn = JMPL_TKN_FOREACH;
        else if (!strcmp(*valp, "in"))
            tkn = JMPL_TKN_IN;
        else if (!strcmp(*valp, "do"))
            tkn = JMPL_TKN_DO;
        else
            tkn = JMPL_TKN_SUBST;

        goto out;
    }
#else
    tkn = scan_directive(jp, valp);

    if (tkn == JMPL_TKN_ERROR)
        goto parse_error;

    if (tkn != JMPL_TKN_UNKNOWN)
        return tkn;
#endif

    switch (options) {
    case SCAN_MAIN:         goto scan_verbatim;
    case SCAN_IF_EXPR:      goto scan_if_expr;
    case SCAN_IF_BODY:      goto scan_body;
    case SCAN_FOREACH:      goto scan_foreach;
    case SCAN_FOREACH_BODY: goto scan_body;
    case SCAN_ID:           goto scan_id;
    default:                goto parse_error;
    }

 scan_verbatim:
    tkn = JMPL_TKN_TEXT;
    b   = p;
    e   = strstr(b, jp->mbeg);

    if (e == NULL) {
        n = strlen(b);
        e = b + n;
    }
    else
        n = e - b;

    strncpy(t, b, n);
    t += n;
    *t++ = '\0';
    p += n;

    goto out;

 scan_if_expr:
    p = skip_whitespace(p, true);

#if 0
    if (!strncmp(p, jp->mbeg, jp->lbeg))
        goto scan_directive;
#else
    tkn = scan_directive(jp, valp);

    if (tkn == JMPL_TKN_ERROR)
        goto parse_error;

    if (tkn != JMPL_TKN_UNKNOWN)
        return tkn;
#endif

    switch (*p) {
    case '\'':
    case '"':
        tkn = JMPL_TKN_STRING;
        q   = p++;
        b   = p;
        e   = next_quote(p, *q);

        if (!*e)
            goto missing_quote;

        n = e - b;

        strncpy(t, b, n);
        t += n;
        *t++ = '\0';
        p += n + 1;

        goto out;

    case '(':
    case ')':
        tkn = (*p == '(' ? JMPL_TKN_OPEN : JMPL_TKN_CLOSE);
        p++;
        goto out;

    case '!':
        if (p[1] == '=') {
            tkn = JMPL_TKN_NEQ;
            p += 2;
        }
        else {
            tkn = JMPL_TKN_NOT;
            p++;
        }
        goto out;

    case '=':
        if (p[1] != '=')
            goto parse_error;

        tkn = JMPL_TKN_EQ;
        p += 2;
        goto out;

    case '|':
    case '&':
        if (p[1] != *p)
            goto parse_error;

        tkn = (*p == '|' ? JMPL_TKN_OR : JMPL_TKN_AND);
        p += 2;
        goto out;

    default:
        break;
    }
    goto parse_error;

 scan_foreach:
    p = parser_skip_whitespace(jp, true);

    tkn = scan_directive(jp, valp);

    if (tkn == JMPL_TKN_ERROR)
        goto parse_error;

    if (tkn != JMPL_TKN_UNKNOWN)
        return tkn;

    goto scan_verbatim;

 scan_body:
#if 0
    goto try_directive;
#else
    tkn = scan_directive(jp, valp);

    if (tkn == JMPL_TKN_ERROR)
        goto parse_error;

    if (tkn != JMPL_TKN_UNKNOWN)
        return tkn;

    goto scan_verbatim;
#endif

 scan_id:
    b = skip_whitespace(p, true);
    e = next_whitespace(b, true);

    if (!*e)
        goto parse_error;

    n = e - b;
    strncpy(t, b, n);
    t += n;
    *t++ = '\0';
    p = e;

    tkn = JMPL_TKN_ID;

    goto out;

 out:
    jp->p = p;
    jp->t = t;

    return tkn;

 missing_quote:
    errno = EINVAL;
    return JMPL_TKN_ERROR;

 eof:
    *t = '\0';
    return JMPL_TKN_EOF;

 parse_error:
    errno = EINVAL;
    return JMPL_TKN_ERROR;
}


int scan_push_token(jmpl_parser_t *jp, int tkn, char *val)
{
    if (jp->tkn != JMPL_TKN_UNKNOWN)
        return -1;

    jp->tkn = tkn;
    jp->val = val;

    return 0;
}
