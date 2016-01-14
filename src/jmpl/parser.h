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

#ifndef __JSON_TEMPLATE_PARSER_H__
#define __JSON_TEMPLATE_PARSER_H__

#include <iot/common/list.h>
#include <iot/common/json.h>

typedef struct jmpl_s       jmpl_t;
typedef enum   jmpl_op_e    jmpl_op_t;
typedef struct jmpl_ref_s   jmpl_ref_t;
typedef struct jmpl_subst_s jmpl_subst_t;
typedef struct jmpl_text_s  jmpl_text_t;

typedef struct jmpl_symbol_s jmpl_symbol_t;
typedef struct jmpl_parser_s jmpl_parser_t;

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
    int32_t *ids;                        /* field name id, or index */
    int      nid;                        /* number of ids */
};


typedef enum {
    SCAN_MAIN = 0,
    SCAN_IF_EXPR,
    SCAN_IF_BODY,
    SCAN_FOREACH,
    SCAN_FOREACH_BODY,
    SCAN_ID,
} scan_options;


struct jmpl_parser_s {
    jmpl_t          *jmpl;
    iot_list_hook_t  templates;
    char            *mbeg;               /* directive start marker */
    int              lbeg;               /* start marker length */
    char            *mend;               /* directive end marker */
    int              lend;               /* end marker length */
    char            *mtab;               /* directive tabulation marker */
    int              ltab;               /* tabulation marker length */
    char            *buf;                /* input buffer to parse */
    char            *p;                  /* parsing pointer into buf */
    char            *tokens;             /* token save buffer */
    char            *t;                  /* token buffer pointer */
    char            *error;              /* parser error */
};


typedef enum {
    JMPL_TKN_ERROR = -1,                 /* tokenization failure */
    JMPL_TKN_UNKNOWN,                    /* unknown token */
    JMPL_TKN_IFSET,                      /* #ifdef like if-set */
    JMPL_TKN_IF,                         /* ordinary if */
    JMPL_TKN_ELSE,                       /* else branch of if-set or if */
    JMPL_TKN_FOREACH,                    /* foreach */
    JMPL_TKN_IN,                         /* in */
    JMPL_TKN_DO,                         /* do */
    JMPL_TKN_END,                        /* end of if-set, if, or foreach */
    JMPL_TKN_ID,                         /* variable id */
    JMPL_TKN_STRING,                     /* quoted string */
    JMPL_TKN_OPEN,
    JMPL_TKN_CLOSE,
    JMPL_TKN_NOT,
    JMPL_TKN_NEQ,
    JMPL_TKN_EQ,
    JMPL_TKN_OR,
    JMPL_TKN_AND,
    JMPL_TKN_TEXT,                       /* plaintext */
    JMPL_TKN_SUBST,                      /* variable substitution */
    JMPL_TKN_EOF,                        /* end of file/input */
} jmpl_token_t;


struct jmpl_s {
    jmpl_op_t       type;
    iot_list_hook_t hook;
};



typedef struct jmpl_symtab_s jmpl_symtab_t;


enum {
    JMPL_SYMBOL_INDEX  = 0x00000000,
    JMPL_SYMBOL_FIELD  = 0x10000000,
    JMPL_SYMBOL_STRING = 0x20000000,
} jmpl_sym_type_t;


#define JMPL_SYMTAG_MASK (0x70000000)
#define JMPL_SYMBOL_TAG(id) ((id) &  JMPL_SYMTAG_MASK)
#define JMPL_SYMBOL_IDX(id) ((id) & ~JMPL_SYMTAG_MASK)

struct jmpl_symbol_s {
    int32_t  tags;
    char    *str;
};



int scan_next_token(jmpl_parser_t *jp, char **valp, int options);

static inline char *skip_whitespace(char *p, int eol)
{
    while (*p && (*p == ' ' || *p == '\t' || (eol && *p == '\n')))
        p++;

    return p;
}


static inline char *next_whitespace(char *p, int eol)
{
    while (*p && (*p != ' ' && *p != '\t') && (!eol || *p != '\n'))
        p++;

    return p;
}


static inline char *skip_newlines(char *p)
{
    while (*p == '\n')
        p++;

    return p;
}


static inline char *next_newline(char *p)
{
    while (*p && *p != '\n')
        p++;

    return p;
}


static inline char *next_quote(char *p, char quote)
{
    char c;

    while (*p && *p != quote) {
        if ((c = *p++) == '\\') {
            if (*p)
                p++;
        }
    }

    return p;
}


static inline char *parser_skip_whitespace(jmpl_parser_t *jp, int eol)
{
    return (jp->p = skip_whitespace(jp->p, eol));
}


static inline char *parser_next_whitespace(jmpl_parser_t *jp, int eol)
{
    return (jp->p = next_whitespace(jp->p, eol));
}


jmpl_t *jmpl_parse(const char *str);


int32_t symtab_add(const char *str, int32_t tag);
const char *symtab_get(int32_t id);

#endif /* __JSON_TEMPLATE_PARSER_H__ */
