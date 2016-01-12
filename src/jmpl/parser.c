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

typedef struct jmpl_s     jmpl_t;
typedef enum   jmpl_op_e  jmpl_op_t;
typedef struct jmpl_ref_s jmpl_ref_t;

typedef struct jmpl_parser_s jmpl_parser_t;

struct jmpl_s {
    iot_list_hook_t ops;                 /* JSON template operations */
    iot_list_hook_t hook;                /* into template stack */
};


enum jmpl_op_e {
    JMPL_OP_TEXT = 0,                    /* plain text, copy verbatim */
    JMPL_OP_SUBST,                       /* a substitution */
    JMPL_OP_IFSET,                       /* akin to an #ifdef */
    JMPL_OP_IF,                          /* an if-else construct */
    JMPL_OP_FOREACH,                     /* a for-each construct */
};


struct jmpl_text_s {
    jmpl_op_t        type;               /* JMPL_OP_TEXT */
    iot_list_hook_t  hook;               /* to op list */
    char            *text;               /* text to produce */
};


struct jmpl_subst_s {
    jmpl_op_t        type;               /* JMPL_OP_SUBST */
    iot_list_hook_t  hook;               /* to op list */
    jmpl_ref_t      *ref;                /* variable reference */
};


struct jmpl_ref_s {
    uint32_t *ids;                       /* field name id, or index */
    int       nid;                       /* number of ids */
};


json_t *jmpl_load_json(const char *path)
{
    iot_json_t  *o;
    struct stat  st;
    char        *buf, *p;
    int          fd, n;

    if (stat(path, &st) < 0)
        return NULL;

    buf = alloca(st.st_size + 1);
    fd   = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;

    n = read(fd, buf, st.st_size);
    close(fd);

    if (n != st.st_size)
        return NULL;

    buf[n] = '\0';
    p      = buf;

    if (iot_json_parse_object(&p, &n, &o) < 0)
        return NULL;

    while (*p == ' ' || *p == '\t' || *p == '\n')
        p++;

    if (!*p)
        return o;

    iot_json_unref(o);
    errno = EINVAL;
    return NULL;
}


jmpl_t *jmpl_load_template(const char *path)
{
    jmpl_t      *j;
    struct stat  st;
    char        *buf;
    int          fd, n;

    if (stat(path, &st) < 0)
        return NULL;

    buf = alloca(st.st_size + 1);
    fd   = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;

    n = read(fd, buf, st.st_size);
    close(fd);

    if (n != st.st_size)
        return NULL;

    buf[n] = '\0';

    j = jmpl_parse(buf);

    return j;
}


struct jmpl_parser_s {
    jmpl_parser_t   *parent;
    jmpl_t          *jmpl;
    iot_list_hook_t  templates;
    char            *mbeg;               /* directive start marker */
    int              lbeg;               /* start marker length */
    char            *mend;               /* directive end marker */
    int              lend;               /* end marker length */
    char            *mtab;               /* directive tabulation marker */
    int              ltab;               /* tabulation marker length */
    char            *buf;                /* input buffer to parse */
    int              level;              /* nesting level */
    char            *p;                  /* parsing pointer into buf */
    char            *error;              /* parser error */
};


typedef enum {
    JMPL_TKN_ERROR = -1,                 /* tokenization failure */
    JMPL_TKN_UNKNOWN,                    /* unknown token */
    JMPL_TKN_IFSET,                      /* #ifdef like if-set */
    JMPL_TKN_IF,                         /* ordinary if */
    JMPL_TKN_ELSE,                       /* else branch of if-set or if */
    JMPL_TKN_FOREACH,                    /* foreach */
    JMPL_TKN_END,                        /* end of if-set, if, or foreach */
    JMPL_TKN_TEXT,                       /* plaintext */
    JMPL_TKN_SUBST,                      /* variable substitution */
    JMPL_TKN_ESCAPE,                     /* escaped plaintext */
    JMPL_TKN_EOF,                        /* end of file/input */
} jmpl_token_t;


#define PARSE_ERROR(_jp, _r, _ec, _em) do {     \
        (_jp)->error = (_em);                   \
        errno = (_ec);                          \
        return _r;                              \
    } while (0)



static int parser_init(jmpl_parser_t *jp, jmpl_parser_t *parent,
                       const char *str, int len)
{
    const char *b, *e;

    /*
     * Every JSON template file starts with the declaration of the
     * directive markers. The directive markers are used to delimit
     * template directives throughout the template. The directive
     * marker declaration is the first line of the template and is
     * a single line consisting of:
     *
     *   - the beginning marker
     *   - at least a single whitespace separating the markers
     *   - the end marker
     *   - a terminating newline
     *
     * Any character string is allowed as a marker with the following
     * limitations:
     *
     *   - a marker cannot contain any whitespace or a newline
     *   - neither marker can be a substring of the other
     */

    iot_clear(jp);
    iot_list_init(&jp->templates);
    jp->parent = parent;


    if (len < 0)
        len = strlen(str);

    if (parent != NULL) {
        jp->parent = parent;
        jp->mbeg   = parent->mbeg; jp->lbeg = parent->lbeg;
        jp->mend   = parent->mend; jp->lend = parent->lend;
        jp->mtab   = parent->mtab; jp->ltab = parent->ltab;
        jp->level  = parent->level;

        while ((*str == ' ' || *str == '\t' || *str == '\n') && len > 0)
            str++, len--;

        jp->buf = jp->p = iot_strndup(str, len);
        goto chkbuf;
    }

    for (b = str, e = b; *e && *e != ' ' && *e != '\t'; e++)
        ;

    if (!*e)
        goto invalid;

    jp->mbeg = iot_strndup(b, e - b);

    while (*e && (*e == ' ' || *e == '\t'))
        e++;

    if (!*e)
        goto invalid;

    for (b = e; *e && *e != '\n' && *e != ' ' && *e != '\t'; e++)
        ;

    if (!*e)
        goto invalid;

    jp->mend = iot_strndup(b, e - b);

    while (*e == ' ' || *e == '\t' || *e == '\n')
        e++;

    if (*e && *e != '\n') {
        jp->mtab = jp->mend;
        jp->mend = NULL;

        for (b = e; *e && *e != '\n' && *e != ' ' && *e != '\t'; e++)
            ;

        jp->mend = iot_strndup(b, e - b);

        while (*e == ' ' || *e == '\t' || *e == '\n')
            e++;
    }

    jp->buf = jp->p = iot_strdup(e);

 chkbuf:
    if (jp->buf == NULL)
        return -1;

    jp->lbeg = strlen(jp->mbeg);
    jp->lend = strlen(jp->mend);
    jp->ltab = jp->mtab ? strlen(jp->mtab) : 0;

    return 0;

 invalid:
    PARSE_ERROR(jp, -1, EINVAL, "invalid directive marker declaration");
}


static void parser_exit(jmpl_parser_t *jp)
{
    if (jp->parent == NULL) {
        iot_free(jp->mbeg);
        iot_free(jp->mend);
        iot_free(jp->mtab);
    }

    iot_free(jp->buf);
}


static void parser_push_template(jmpl_parser_t *jp, jmpl_t *j)
{
    if (jp->jmpl != NULL)
        iot_list_append(&jp->templates, &jp->jmpl->hook);

    iot_list_init(&j->hook);
    jp->jmpl = j;
}


static jmpl_t *parser_pop_template(jmpl_parser_t *jp)
{
    jmpl_t *j;

    if (!iot_list_empty(&jp->templates)) {
        j = iot_list_entry(jp->templates.prev, typeof(*j), hook);
        iot_list_delete(&j->hook);
    }
    else
        j = NULL;

    jp->jmpl = j;

    return j;
}


static int directive_type(jmpl_parser_t *jp, const char *token)
{
    static struct keyword {
        int         token;
        const char *str;
        size_t      len;
        int         args;
    } keywords[] = {
#define KEYWORD(_tkn, _kw, _args) { JMPL_TKN_##_tkn, _kw, sizeof(_kw)-1, _args }
        KEYWORD(IFSET  , "if-set" , TRUE),
        KEYWORD(IF     , "if"     , TRUE),
        KEYWORD(FOREACH, "foreach", TRUE),
        KEYWORD(ELSE   , "else"   , FALSE),
        KEYWORD(END    , "end"    , FALSE),
        { 0, NULL, 0, FALSE },
#undef KEYWORD
    }, *kw;

    int c;

    for (kw = keywords; kw->str != NULL; kw++) {
        if (strncmp(token, kw->str, kw->len))
            continue;

        if (kw->args && ((c = token[kw->len]) == ' ' || c == '\t' || c == '\n'))
            return kw->token;

        if (!kw->args && !strncmp(token + kw->len, jp->mend, jp->lend))
            return kw->token;
    }

    switch (*token) {
    case '\\':
        return JMPL_TKN_ESCAPE;
    case 'A'...'Z':
    case 'a'...'z':
    case '_':
        return JMPL_TKN_SUBST;
    default:
        break;
    }

    return JMPL_TKN_ERROR;
}


static char *skip_keyword(jmpl_parser_t *jp, int token)
{
    static struct keyword {
        int         token;
        const char *str;
        size_t      len;
        int         args;
    } keywords[] = {
#define KEYWORD(_tkn, _kw, _args) { JMPL_TKN_##_tkn, _kw, sizeof(_kw)-1, _args }
        KEYWORD(IFSET  , "if-set" , TRUE),
        KEYWORD(IF     , "if"     , TRUE),
        KEYWORD(FOREACH, "foreach", TRUE),
        KEYWORD(ELSE   , "else"   , FALSE),
        KEYWORD(END    , "end"    , FALSE),
        { 0, NULL, 0, FALSE },
#undef KEYWORD
    }, *kw;

    char *p;

    p = jp->p;

    for (kw = keywords; kw->str != NULL; kw++) {
        if (kw->token != token)
            continue;

        p += jp->lbeg;
        p += kw->len;

        if (kw->args) {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
        }

        jp->p = p;
    }

    return jp->p;
}


static int parser_get_token(jmpl_parser_t *jp, char **valp, int *lenp)
{
    char *b, *e, *p, *val;

    /*
     * our tokenizer logic is simple:
     *
     *   - at the end of input, we're done
     *   - at the beginning of a directive find its end to create a token
     *   - otherwise evrything till the next directive is plaintext
     */

    b = jp->p;

    if (!b || !*b)                              /* end of input, we're done */
        return JMPL_TKN_EOF;

    if (!strncmp(b, jp->mbeg, jp->lbeg)) {      /* at a directive */
        val = jp->p;
        b  += jp->lbeg;

        while (b && *b) {                       /* search for the end */
            p = b;
            b = strstr(p, jp->mbeg);
            e = strstr(p, jp->mend);

            if (e == NULL) {
                jp->error = "unterminated directive";
                return JMPL_TKN_ERROR;
            }

            if (b && b < e)
                b = e + jp->lend;
            else {
                e = e + jp->lend;

                *valp = val;
                *lenp = e - val;
                jp->p = e;

                return directive_type(jp, val + jp->lbeg);
            }
        }

        return JMPL_TKN_ERROR;
    }
    else {                                      /* not a directive, plaintext */
        e = strstr(jp->p, jp->mbeg);
        *valp = b;

        if (e == NULL) {
            *lenp  = strlen(b);
            jp->p += *lenp;
        }
        else {
            *lenp = e - b;
            jp->p = e;
        }

        return JMPL_TKN_TEXT;
    }
}


#if 0
jmpl_t *parse_template(jmpl_parser_t *parent, const char *str, int len)
{
    jmpl_parser_t  jp;
    jmpl_t        *j;
    char          *val;
    int            tkn;

    if (parser_init(&jp, parent, str, len) < 0)
        return NULL;

    while ((tkn = parser_get_token(&jp, &val, &len)) != JMPL_TKN_EOF) {
        switch (tkn) {
        case JMPL_TKN_ERROR:
        case JMPL_TKN_UNKNOWN:
            goto parse_error;

        case JMPL_TKN_IFSET:
            iot_debug("<ifset>: [%*.*s]", len, len, val);
            break;

        case JMPL_TKN_IF:
            iot_debug("<if>: [%*.*s]", len, len, val);
            break;

        case JMPL_TKN_FOREACH:
            iot_debug("<foreach>: [%*.*s]", len, len, val);
            break;

        case JMPL_TKN_ELSE:
            iot_debug("<else>");
            break;

        case JMPL_TKN_END:
            iot_debug("<end>");
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text>: [%*.*s]", len, len, val);
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst>: [%*.*s]", len, len, val);
            break;

        case JMPL_TKN_ESCAPE:
            iot_debug("<escape>");
            break;
        }
    }

    iot_debug("token <EOF>");

    return NULL;

 parse_error:
    parser_exit(&jp);
    errno = EINVAL;
    return NULL;
}
#endif


static int parse_ifset(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


static int parse_if(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


static int parse_foreach(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


static int parse_text(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


static int parse_subst(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


static int parse_escape(jmpl_parser_t *parent, char *str, int len)
{
    return 0;
}


jmpl_t *jmpl_parse(const char *str)
{
    jmpl_parser_t  jp;
    jmpl_t        *j;
    char          *val;
    int            len, tkn;

    if (parser_init(&jp, NULL, str, -1) < 0)
        return NULL;

    j = iot_allocz(sizeof(*j));

    if (j == NULL)
        goto nomem;

    iot_debug("begin marker: '%s'", jp.mbeg);
    iot_debug("  end marker: '%s'", jp.mend);
    iot_debug("  tab marker: '%s'", jp.mtab ? jp.mtab : "<none>");
    iot_debug("    template: %s"  , jp.buf);


    while ((tkn = parser_get_token(&jp, &val, &len)) != JMPL_TKN_EOF) {
        switch (tkn) {
        case JMPL_TKN_ERROR:
        case JMPL_TKN_UNKNOWN:
            goto parse_error;

        case JMPL_TKN_IFSET:
            iot_debug("<ifset>: [%*.*s]", len, len, val);

            if (parse_ifset(&jp, val, len) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_IF:
            iot_debug("<if>: [%*.*s]", len, len, val);

            if (parse_if(&jp, val, len) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_FOREACH:
            iot_debug("<foreach>: [%*.*s]", len, len, val);

            if (parse_foreach(&jp, val, len) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_ELSE:
            iot_debug("<else>");
            break;

        case JMPL_TKN_END:
            iot_debug("<end>");
            break;

        case JMPL_TKN_TEXT:
            iot_debug("<text>: [%*.*s]", len, len, val);

            if (parse_text(&jp, val, len) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_SUBST:
            iot_debug("<subst>: [%*.*s]", len, len, val);

            if (parse_subst(&jp, val, len) < 0)
                goto parse_error;
            break;

        case JMPL_TKN_ESCAPE:
            iot_debug("<escape>");

            if (parse_escape(&jp, val, len) < 0)
                goto parse_error;
            break;
        }
    }

    iot_debug("token <EOF>");

    return j;

 nomem:
    parser_exit(&jp);
    return NULL;

 parse_error:
    parser_exit(&jp);
    errno = EINVAL;
    return NULL;
}

