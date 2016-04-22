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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

#include <smpl/macros.h>
#include <smpl/types.h>


smpl_parser_t *parser_create(smpl_t *smpl)
{
    smpl_parser_t *p;

    p = smpl_alloct(typeof(*p));

    if (p == NULL)
        goto nomem;

    smpl_list_init(&p->inq);
    smpl_list_init(&p->bufq);
    smpl_list_init(&p->tknq);

    p->smpl = smpl;

    return p;

 nomem:
    return NULL;
}


void parser_destroy(smpl_t *smpl)
{
    smpl_parser_t *p;

    if (smpl == NULL || (p = smpl->parser) == NULL)
        return;

    preproc_purge(smpl);
    buffer_purge(&p->bufq);

    smpl_free(p->mbeg);
    smpl_free(p->mend);
    smpl_free(p->mtab);
    smpl_free(p);


    smpl->parser = NULL;
}


int parse_markers(smpl_t *smpl, char *buf, const char *path)
{
    smpl_parser_t *parser = smpl->parser;
    char          *b, *e, line[64];
    int            n, l;

    for (n = 0; n < (int)sizeof(line) - 1 && *buf; buf++, n++) {
        if (*buf != '\n')
            line[n] = *buf;
        else {
            line[n] = '\0';
            break;
        }
    }

    if (n >= (int)sizeof(line) - 1)
        goto invalid_markers;

    b = line;
    e = strchr(b, ' ');

    if (*b == ' ' || *b == '\t' || e == NULL)
        goto invalid_markers;

    l = e - b;
    parser->mbeg = smpl_strndup(b, l);
    parser->lbeg = l;

    if (parser->mbeg == NULL)
        goto nomem;

    b = e + 1;
    if (*b == ' ' || *b == '\t')
        goto invalid_markers;
    e = strchr(b, ' ');

    if (e != NULL)
        l = e - b;
    else {
        l = strlen(b);
        e = b + l;
    }

    parser->mend = smpl_strndup(b, l);
    parser->lend = l;

    if (parser->mend == NULL)
        goto nomem;

    if (e != NULL) {
        b = e + 1;
        if (*b == ' ' || *b == '\t' || *b == '\0')
            goto invalid_markers;
        e = strchr(b, ' ');

        if (e != NULL)
            l = e - b;
        else {
            l = strlen(b);
            e = b + l;
        }

        parser->mtab = smpl_strndup(b, l);
        parser->ltab = l;

        if (parser->mtab == NULL)
            goto nomem;
    }

    return n + 1;

 invalid_markers:
    line[sizeof(line) - 1] = '\0';
    smpl_return_error(-1, smpl, EINVAL, path ? path : "<input string>", 1,
                      "invalid marker declaration '%s'", line);

 nomem:
    return -1;
}


const char *token_name(int type)
{
    switch (type) {
    case SMPL_TOKEN_ERROR:       return "<ERROR>";
    case SMPL_TOKEN_EOF:         return "<EOF>";
    case SMPL_TOKEN_PAREN_OPEN:  return "<(>";
    case SMPL_TOKEN_PAREN_CLOSE: return "<)>";
    case SMPL_TOKEN_INDEX_OPEN:  return "<[>";
    case SMPL_TOKEN_INDEX_CLOSE: return "<]>";
    case SMPL_TOKEN_DOT:         return "<.>";
    case SMPL_TOKEN_COLON:       return "<:>";
    case SMPL_TOKEN_COMMA:       return "<,>";
    case SMPL_TOKEN_NOT:         return "<!>";
    case SMPL_TOKEN_IS:          return "<?>";

    case SMPL_TOKEN_COMMENT:     return "<COMMENT>";
    case SMPL_TOKEN_INCLUDE:     return "<INCLUDE>";
    case SMPL_TOKEN_MACRO:       return "<MACRO>";
    case SMPL_TOKEN_IF:          return "<IF>";
    case SMPL_TOKEN_FOR:         return "<FOR>";
    case SMPL_TOKEN_SWITCH:      return "<SWITCH>";
    case SMPL_TOKEN_IN:          return "<IN>";
    case SMPL_TOKEN_DO:          return "<DO>";
    case SMPL_TOKEN_ELSE:        return "<ELSE>";
    case SMPL_TOKEN_END:         return "<END>";
    case SMPL_TOKEN_CASE:        return "<CASE>";
    case SMPL_TOKEN_FIRST:       return "<FIRST>";
    case SMPL_TOKEN_LAST:        return "<LAST>";
    case SMPL_TOKEN_TRAIL:       return "<TRAIL>";

    case SMPL_TOKEN_MACROREF:    return "<MACRO-CALL>";
    case SMPL_TOKEN_FUNCREF:     return "<FUNCTION-CALL>";
    case SMPL_TOKEN_TEXT:        return "<TEXT>";
    case SMPL_TOKEN_NAME:        return "<NAME>";
    case SMPL_TOKEN_VARREF:      return "<VARREF>";
    case SMPL_TOKEN_STRING:      return "<STRING>";
    case SMPL_TOKEN_INTEGER:     return "<INTEGER>";
    case SMPL_TOKEN_DOUBLE:      return "<DOUBLE>";

    case SMPL_TOKEN_AND:         return "<AND>";
    case SMPL_TOKEN_OR:          return "<OR>";
    case SMPL_TOKEN_EQUAL:       return "<EQUAL>";
    case SMPL_TOKEN_NOTEQ:       return "<NOTEQ>";
    default:                     return "<UNKNOWN-TOKEN>";
    }
}


static inline char *skip_whitespace(smpl_t *smpl)
{
    smpl_input_t *in = smpl->parser->in;
    char          c;

    /* don't skip anything if we have pushed back tokens pending */
    if (smpl_list_empty(&smpl->parser->tknq)) {
        while ((c = *in->p) == ' ' || c == '\t' || c == '\n') {
            in->p++;
            if (c == '\n')
                in->line++;
        }
    }

    return in->p;
}


static inline char *skip_newline(smpl_t *smpl)
{
    smpl_input_t *in = smpl->parser->in;

    /* don't skip anything if we have pushed back tokens pending */
    if (smpl_list_empty(&smpl->parser->tknq)) {
        if (*in->p == '\n') {
            in->p++;
            in->line++;
        }
    }

    return in->p;
}


void parser_skip_newline(smpl_t *smpl)
{
    skip_newline(smpl);
}


static char *store_token(smpl_t *smpl, char *value, int len)
{
    char *s;
    int   size;

    if (len < 0)
        size = strlen(value) + 1;
    else
        size = len + 1;

    s = buffer_alloc(&smpl->parser->bufq, size);

    if (s == NULL)
        return NULL;

    strncpy(s, value, len);
    s[len] = '\0';

    return s;
}


static int collect_string(smpl_t *smpl, smpl_token_t *t)
{
    smpl_input_t *in = smpl->parser->in;
    char         *b, *e, *s, q;
    int           n;

    b = in->p;

    if ((q = *b) != '\'' && q != '"')
        goto invalid;

    e = b + 1;
    while (*e) {
        if (*e == q)
            break;

        if (*e == '\\') {
            if (!e[1])
                goto invalid;
            if (e[1] == '\n')
                in->line++;
            e += 2;
        }
        else
            e++;
    }

    if (*e != q)
        goto invalid;

    n = e - (b + 1);
    s = buffer_alloc(&smpl->parser->bufq, n + 1);

    if (s == NULL)
        goto nomem;

    strncpy(s, b + 1, n);
    s[n] = '\0';

    t->type = SMPL_TOKEN_STRING;
    t->str  = s;
    in->p   = e + 1;

    return t->type;

 invalid:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<invalid quoted string>";
    smpl_fail(-1, smpl, EINVAL, "invalid quoted string");

 nomem:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<out of memory>";
    return -1;
}


static int collect_number(smpl_t *smpl, smpl_token_t *t)
{
    smpl_input_t *in = smpl->parser->in;
    char         *e;
    long          l;
    double        d;

    t->type = SMPL_TOKEN_INTEGER;
    l       = (int32_t)strtol(in->p, &e, 0);

    if (errno == ERANGE && (l == LONG_MIN || l == LONG_MAX))
        goto overflow;

    if (*e == '.') {
        t->type = SMPL_TOKEN_DOUBLE;
        d       = strtod(in->p, &e);

        if (errno == ERANGE && d == HUGE_VAL)
            goto overflow;

        if (e == in->p + 1)
            goto invalid;

        t->dbl = d;
    }
    else if (e == in->p)
        goto invalid;
    else
        t->i32 = (int32_t)l;

    switch (*e) {
    case 'a'...'z':
    case 'A'...'Z':
    case '.':
    case '_':
        goto invalid;
    default:
        break;
    }

    t->str = store_token(smpl, in->p, e - in->p);

    if (t->str == NULL)
        goto nomem;

    in->p = e;

    return t->type;

 overflow:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<integer/floating over/underflow>";
    smpl_fail(-1, smpl, errno, "number out of range");

 invalid:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<invalid number>";
    smpl_fail(-1, smpl, 0, "invalid number");

 nomem:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<out of memory>";
    return -1;
}


static int collect_text(smpl_t *smpl, smpl_token_t *t)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in = parser->in;
    char          *b, *e, *p, *q;
    int            l;

    b = in->p;
    e = strstr(b, parser->mbeg);

    if (e == NULL)
        l = strlen(b);
    else
        l = e - b;

    t->str = buffer_alloc(&smpl->parser->bufq, l + 1);

    if (t->str == NULL)
        goto nomem;

    t->type = SMPL_TOKEN_TEXT;
    for (p = b, q = t->str; l > 0; l--)
        if ((*q++ = *p++) == '\n')
            in->line++;

    in->p = p;

    return t->type;

 nomem:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<out of memory>";
    return -1;
}


static int collect_escape(smpl_t *smpl, char *b, int len, smpl_token_t *t)
{
    char buf[len + 1], *p;

    p = buf;
    while (len > 0) {
        if (*b == '\\') {
            if (len == 1) {
                t->type = SMPL_TOKEN_TEXT;
                t->str  = "";
                goto out;
            }
            switch (b[1]) {
            case 'n':  *p++ = '\n'; break;
            case 't':  *p++ = '\t'; break;
            case 'r':  *p++ = '\r'; break;
            case '\0': *p++ = '\\'; break;
            default:
                if (p != b)
                    goto invalid_sequence;
                else
                    *p++ = b[1];
                break;
            }
            b   += 2;
            len -= 2;
        }
        else {
            *p++ = *b++;
            len--;
        }
    }

    *p = '\0';

    t->type = SMPL_TOKEN_TEXT;
    t->str  = store_token(smpl, buf, p - buf);

 out:
    return t->type;

 invalid_sequence:
    smpl_fail(-1, smpl, EINVAL, "invalid escape sequence '%*.*s'", len, len, b);
    return -1;
}


static int collect_directive(smpl_t *smpl, smpl_token_t *t)
{
#define KEYWORD(_s, _t) { _s, (int)sizeof(_s) - 1, SMPL_TOKEN_##_t, false  }
#define KEYHEAD(_s, _t) { _s, (int)sizeof(_s) - 1, SMPL_TOKEN_##_t, true   }
    static struct {
        const char        *str;
        int                size;
        smpl_token_type_t  token;
        bool               trail;
    } directives[] = {
        KEYWORD("#"      , COMMENT),
        KEYWORD("//"     , COMMENT),
        KEYWORD("*"      , COMMENT),
        KEYWORD("include", INCLUDE),
        KEYWORD("macro"  , MACRO  ),
        KEYWORD("if"     , IF     ),
        KEYWORD("for"    , FOR    ),
        KEYWORD("foreach", FOR    ),
        KEYWORD("switch" , SWITCH ),
        KEYWORD("in"     , IN     ),
        KEYWORD("do"     , DO     ),
        KEYWORD("then"   , DO     ),
        KEYWORD("else"   , ELSE   ),
        KEYWORD("end"    , END    ),
        KEYWORD("case"   , CASE   ),
        KEYWORD("default", ELSE   ),
        KEYWORD("first"  , FIRST  ),
        KEYWORD("?first" , FIRST  ),
        KEYWORD("!first" , FIRST  ),
        KEYWORD("last"   , LAST   ),
        KEYWORD("?last"  , LAST   ),
        KEYWORD("!last"  , LAST   ),
        KEYHEAD("trail:" , TRAIL  ),
        KEYHEAD("?trail:", TRAIL  ),
        KEYHEAD("!trail:", TRAIL  ),
        KEYHEAD("\\"     , ESCAPE ),
        KEYWORD("\\"     , ESCAPE ),
        KEYHEAD(""       , VARREF ),
        { NULL, 0, 0, false }
    }, *dir;

    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in     = parser->in;
    char         *b, *e, *n, *p;
    int           l;

    b = strstr(in->p, parser->mbeg);

    if (b != in->p)
        goto no_directive;

    b += parser->lbeg;
    e  = strstr(b, parser->mend);

    if (e == NULL)
        goto missing_end;

    l = e - b;
    n = e + parser->lend;

    for (dir = directives; dir->str; dir++) {
        if (!dir->trail && l == dir->size && !strncmp(b, dir->str, l))
            break;
        if (dir->trail && l > dir->size && !strncmp(b, dir->str, dir->size))
            break;
    }

    if (dir->str == NULL)             /* shouldn't happen... (varref catchall) */
        goto unknown_directive;

    switch (dir->token) {
    case SMPL_TOKEN_COMMENT:
        t->type = SMPL_TOKEN_COMMENT;

        if (*b == '/' || *b == '#') {
            p = strchr(n, '\n');

            if (p != NULL) {
                t->str = store_token(smpl, n, p - n);
                in->p  = p + 1;
            }
            else {
                t->str = store_token(smpl, n, -1);
                in->p  = in->buf + in->size - 1;
            }
        }
        else {
            char end[parser->lbeg + 1 + parser->lend + 1];

            snprintf(end, sizeof(end), "%s*%s", parser->mbeg, parser->mend);

            b = n;
            e = strstr(b, end);

            if (e == NULL)
                goto unterm_comment;

            p = b;
            while (p < e)
                if (*p++ == '\n')
                    in->line++;

            l = e - b;
            t->str = buffer_alloc(&parser->bufq, l + 1);

            if (t->str == NULL)
                goto nomem;

            strncpy(t->str, b, l);
            t->str[l] = '\0';

            in->p = e + sizeof(end) - 1;
        }

        return t->type;

    case SMPL_TOKEN_INCLUDE:
        in->p = n;
        n = skip_whitespace(smpl);

        if (collect_string(smpl, t) < 0)
            goto invalid_include;

        if (*in->p == '\n') {
            in->p++;
            in->line++;
        }

        t->type = SMPL_TOKEN_INCLUDE;
        return t->type;

    case SMPL_TOKEN_MACRO:
    case SMPL_TOKEN_IF:
    case SMPL_TOKEN_FOR:
    case SMPL_TOKEN_SWITCH:
    case SMPL_TOKEN_IN:
    case SMPL_TOKEN_DO:
    case SMPL_TOKEN_ELSE:
    case SMPL_TOKEN_END:
    case SMPL_TOKEN_CASE:
    case SMPL_TOKEN_FIRST:
    case SMPL_TOKEN_LAST:
    case SMPL_TOKEN_TRAIL:
    case SMPL_TOKEN_VARREF:
        t->type = dir->token;
        t->str  = store_token(smpl, b, e - b);
        in->p   = n;

        if (t->type != SMPL_TOKEN_VARREF) {
            if (*in->p == '\n') {
                in->p++;
                in->line++;
            }
        }
        else {
            smpl_function_t *f = function_find(smpl, t->str);
            smpl_macro_t    *m = macro_by_name(smpl, t->str);

            if (m != NULL) {
                t->type = SMPL_TOKEN_MACROREF;
                t->m    = m;
            }
            else if (f != NULL) {
                t->type = SMPL_TOKEN_FUNCREF;
                t->f    = f;
            }
        }

        return t->type;

    case SMPL_TOKEN_ESCAPE:
        if (collect_escape(smpl, b, e - b, t) < 0)
            goto invalid_escape;

        in->p = n;

        if (e >= b + 2 && e[-2] == '\\' && e[-1] == 'n') {
                if (*in->p == '\n') {
                    in->p++;
                    in->line++;
                }
        }
        else if (e >= b + 1 && e[-1] == '\\') {
            if (*in->p == '\n') {
                in->p++;
                in->line++;
            }
        }

        return t->type;

    default:
        break;
    }

 unknown_directive:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<unknown directive>";
    smpl_fail(-1, smpl, EINVAL, "unknown directive '%*.*s'", l, l, b);

 no_directive:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<expecting directive, none found>";
    smpl_fail(-1, smpl, EINVAL, "expecting directive, none found");

 missing_end:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<missing directive end>";
    smpl_fail(-1, smpl, EINVAL, "missing directive end");

 invalid_include:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<invalid include>>";
    smpl_fail(-1, smpl, EINVAL, "invalid include directive");

 unterm_comment:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<unterminated block comment>";
    smpl_fail(-1, smpl, EINVAL, "unterminated block comment");

 invalid_escape:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<unknown directive>";
    smpl_fail(-1, smpl, EINVAL, "invalid escape sequence '%*.*s'", l, l, b);

 nomem:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<out of memory>";
    return -1;
}


static int collect_name(smpl_t *smpl, smpl_token_t *t, int arg)
{
    smpl_input_t *in = smpl->parser->in;
    char         *b, *e;
    int           l;

    skip_whitespace(smpl);

    b = in->p;
    e = b;

    e = b;
    while (isalpha(*e) || (e > b && isdigit(*e)) || *e == '_' || *e == '-')
        e++;

    if (e == b)
        goto invalid_name;

    if (arg) {
        while (*e == '.')
            e++;
    }

    l      = e - b;
    t->str = buffer_alloc(&smpl->parser->bufq, l + 1);

    if (t->str == NULL)
        goto nomem;

    t->type = SMPL_TOKEN_NAME;
    strncpy(t->str, b, l);
    t->str[l] = '\0';

    in->p = e;

    return t->type;

 invalid_name:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<invalid name>";
    smpl_fail(-1, smpl, EINVAL, "expected name token");

 nomem:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<out of memory>";
    return -1;
}


static int collect_expr(smpl_t *smpl, smpl_token_t *t)
{
    smpl_input_t *in = smpl->parser->in;
    char         *p;

    skip_whitespace(smpl);
    p = in->p;

 next_token:
    switch (*p) {
    case '\'':
    case '"':
        return collect_string(smpl, t);

    case '0'...'9':
    case '+':
    case '-':
        return collect_number(smpl, t);

    case 'a'...'z':
    case 'A'...'Z':
    case '_':
        return collect_name(smpl, t, false);

    case '(':
    case ')':
        t->str  = (*p == '(' ? "(" : ")");
        t->type = *t->str;
        p++;
        break;

    case '[':
    case ']':
        t->str  = (*p == '[' ? "[" : "]");
        t->type = *t->str;
        p++;
        break;

    case '.':
        t->str  = ".";
        t->type = *t->str;
        p++;
        break;

    case ':':
        t->str  = ":";
        t->type = *t->str;
        p++;
        break;

    case ',':
        t->str  = ",";
        t->type = *t->str;
        p++;
        break;

    case ';':
        t->str  = ";";
        t->type = *t->str;
        p++;
        break;

    case '?':
        t->str  = "?";
        t->type = *t->str;
        p++;
        break;

    case '!':
        if (p[1] == '=') {
            t->str  = "!=";
            t->type = SMPL_TOKEN_NOTEQ;
            p += 2;
        }
        else {
            t->str  = "!";
            t->type = *t->str;
            p++;
        }
        break;

    case '=':
        if (p[1] != '=')
            goto invalid;
        t->str  = "==";
        t->type = SMPL_TOKEN_EQUAL;
        p += 2;
        break;

    case '&':
        if (p[1] != '&')
            goto invalid;
        t->str  = "&&";
        t->type = SMPL_TOKEN_AND;
        p += 2;
        break;

    case '|':
        if (p[1] != '|')
            goto invalid;
        t->str  = "||";
        t->type = SMPL_TOKEN_OR;
        p += 2;
        break;

    case '\n':
        p++;
        in->line++;
        goto next_token;

    default:
        if (!strncmp(p, smpl->parser->mbeg, smpl->parser->lbeg))
            return collect_directive(smpl, t);
        goto invalid;
    }

    in->p = p;
    return t->type;

 invalid:
    t->type = SMPL_TOKEN_ERROR;
    t->str  = "<invalid expression>";
    smpl_fail(-1, smpl, EINVAL, "invalid expression ('%10.10s')", p);
}


static int collect_arg(smpl_t *smpl, smpl_token_t *t)
{
    smpl_input_t *in = smpl->parser->in;
    char         *p;

    skip_whitespace(smpl);

    p = in->p;

    switch (*p) {
    case '(':
        t->str  = "(";
        t->type = '(';
        p++;
        break;
    case ')':
        t->str  = ")";
        t->type = ')';
        p++;
        break;
    case ',':
        t->str  = ",";
        t->type = ',';
        p++;
        break;
    default:
        return collect_name(smpl, t, true);
    }

    in->p = p;
    skip_whitespace(smpl);

    return t->type;
}


int parser_pull_token(smpl_t *smpl, int flags, smpl_token_t *t)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in;
    smpl_token_t  *tkn;
    const char    *path;
    int            line;

    if (!smpl_list_empty(&parser->tknq)) {
        tkn = smpl_list_entry(parser->tknq.prev, typeof(*tkn), hook);
        smpl_list_delete(&tkn->hook);
        *t = *tkn;
        smpl_list_init(&t->hook);
        smpl_free(tkn);

        return t->type;
    }

    in   = parser->in;
    path = in->path;
    line = in->line;

    if (!*in->p || in->p > in->buf + in->size) {
        t->type = SMPL_TOKEN_EOF;
        t->str  = "";

        goto out;
    }

    if (strncmp(in->p, parser->mbeg, parser->lbeg) == 0) {
        collect_directive(smpl, t);
        goto out;
    }

    switch (flags) {
    case SMPL_PARSE_BLOCK:
        collect_text(smpl, t);
        goto out;

    case SMPL_PARSE_NAME:
        collect_name(smpl, t, false);
        skip_whitespace(smpl);
        goto out;

    case SMPL_PARSE_EXPR:
        collect_expr(smpl, t);
        goto out;

    case SMPL_PARSE_SWITCH:
        skip_whitespace(smpl);
        collect_directive(smpl, t);
        goto out;

    case SMPL_PARSE_ARGS:
        collect_arg(smpl, t);
        goto out;

    default:
        smpl_fail(-1, smpl, EINVAL, "unknown parser flag 0x%x", flags);
    }

 out:
    smpl_debug("token %s ('%s')", token_name(t->type), t->str);
    t->path = path;
    t->line = line;

    return t->type;
}


int parser_push_token(smpl_t *smpl, smpl_token_t *tkn)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_token_t  *t;

    smpl_debug("pushing back token %s ('%s')",
               token_name(tkn->type), tkn->str);

    t = smpl_alloct(typeof(*t));

    if (t == NULL)
        goto nomem;

    *t = *tkn;
    smpl_list_init(&t->hook);
    smpl_list_append(&parser->tknq, &t->hook);

    return 0;

 nomem:
    return -1;
}


int parse_block(smpl_t *smpl, int flags, smpl_list_t *block, smpl_token_t *end)
{
    smpl_token_t t, e;
    int          macros, include, skipws, delim;

    if (end == NULL)
        end = &e;

    include = flags & SMPL_ALLOW_INCLUDE;
    macros  = flags & SMPL_ALLOW_MACROS;
    skipws  = flags & SMPL_SKIP_WHITESPACE;
    delim   = flags & (SMPL_BLOCK_DO | SMPL_BLOCK_ELSE | SMPL_BLOCK_END);

    flags  &= ~(SMPL_ALLOW_INCLUDE | SMPL_ALLOW_MACROS | SMPL_SKIP_WHITESPACE);
    flags  &= ~delim;

    if (skipws) {
        smpl_debug("skipping whitespace");
        skip_whitespace(smpl);
    }

    if (delim) {
        smpl_debug("block delimiters: %s%s%s",
                   delim & SMPL_BLOCK_DO   ? "do "   : "",
                   delim & SMPL_BLOCK_ELSE ? "else " : "",
                   delim & SMPL_BLOCK_END  ? "end"   : "");
    }

    if (delim & (SMPL_BLOCK_DO | SMPL_BLOCK_ELSE)) {
        if (parser_pull_token(smpl, flags, &t) < 0)
            goto parse_error;

        if (delim & SMPL_BLOCK_DO) {
            if (t.type != SMPL_TOKEN_DO)
                goto missing_do;
        }
        else if ((delim & SMPL_BLOCK_ELSE) && t.type != SMPL_TOKEN_ELSE)
            goto missing_else;
    }

    while (parser_pull_token(smpl, flags, &t) >= SMPL_TOKEN_EOF) {
        smpl_debug("token %s ('%s')", token_name(t.type), t.str);

        switch (t.type) {
        case SMPL_TOKEN_COMMENT:
            break;

        case SMPL_TOKEN_INCLUDE:
            if (!include)
                goto misplaced_include;
            if (preproc_push_file(smpl, t.str) < 0)
                goto failed_include;
            break;

        case SMPL_TOKEN_EOF:
            if (!preproc_pull(smpl)) {
                *end = t;
                goto out;
            }
            break;

        case SMPL_TOKEN_DO:
            goto misplaced_do;

        case SMPL_TOKEN_ELSE:
            *end = t;
            goto out;

        case SMPL_TOKEN_END:
            *end = t;
            goto out;

        case SMPL_TOKEN_MACRO:
            if (!macros)
                goto misplaced_macro;
            if (macro_parse(smpl) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_VARREF:
            if (vref_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_MACROREF:
            if (macro_parse_ref(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_FUNCREF:
            if (function_parse_ref(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_TEXT:
            if (text_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_IF:
            if (branch_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_FOR:
            if (loop_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_SWITCH:
            if (switch_parse(smpl, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_FIRST:
            if (branch_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_LAST:
            if (branch_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        case SMPL_TOKEN_TRAIL:
            if (branch_parse(smpl, &t, block) < 0)
                goto parse_error;
            break;

        default:
            goto parse_error;
        }
    }

 out:
    switch (t.type) {
    case SMPL_TOKEN_END:
        if (!(delim & SMPL_BLOCK_END))
            goto misplaced_end;
        return t.type;

    case SMPL_TOKEN_ELSE:
        if (!(delim & SMPL_BLOCK_ELSE))
            goto misplaced_else;
        return t.type;

    case SMPL_TOKEN_EOF:
        if (delim & SMPL_BLOCK_END)
            goto missing_end;
        return t.type;

    case SMPL_TOKEN_ERROR:
        return t.type;

    default:
        if (delim & SMPL_BLOCK_END)
            goto missing_end;
        return t.type;
    }

 misplaced_include:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL,
              "misplaced include of '%s', not allowed here", t.str);

 failed_include:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "failed to include file '%s'", t.str);

 misplaced_macro:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "misplaced macro definition, not allowed here");

 missing_do:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "expected do keyword, got %s",
              token_name(t.type));

 missing_else:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "expected else keyword, got %s",
              token_name(t.type));

 missing_end:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "expected end keyword, got %s",
              token_name(t.type));

 misplaced_do:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "misplaced do keyword, not expected here");

 misplaced_else:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "misplaced else keyword, not expected here");

 misplaced_end:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "misplaced end keyword, not expected here");

 parse_error:
    end->type = SMPL_TOKEN_ERROR;
    smpl_fail(-1, smpl, EINVAL, "failed to parse template");
}
