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

typedef enum   jmpl_op_e         jmpl_op_t;
typedef enum   jmpl_expr_type_e  jmpl_expr_type_t;
typedef enum   jmpl_value_type_e jmpl_value_type_t;

typedef struct jmpl_s        jmpl_t;
typedef struct jmpl_any_s    jmpl_any_t;
typedef struct jmpl_main_s   jmpl_main_t;
typedef struct jmpl_ref_s    jmpl_ref_t;
typedef struct jmpl_subst_s  jmpl_subst_t;
typedef struct jmpl_text_s   jmpl_text_t;
typedef struct jmpl_ifset_s  jmpl_ifset_t;
typedef struct jmpl_if_s     jmpl_if_t;
typedef struct jmpl_for_s    jmpl_for_t;
typedef union  jmpl_insn_u   jmpl_insn_t;
typedef struct jmpl_expr_s   jmpl_expr_t;
typedef struct jmpl_value_s  jmpl_value_t;

typedef struct jmpl_symbol_s jmpl_symbol_t;
typedef enum   jmpl_symval_type_e jmpl_symval_type_t;
typedef struct jmpl_symval_s jmpl_symval_t;


typedef struct jmpl_parser_s jmpl_parser_t;

enum jmpl_op_e {
    JMPL_OP_MAIN = 0,
    JMPL_OP_TEXT,                        /* plain text, copy verbatim */
    JMPL_OP_SUBST,                       /* a substitution */
    JMPL_OP_IFSET,                       /* akin to an #ifdef */
    JMPL_OP_IF,                          /* an if-else construct */
    JMPL_OP_FOREACH,                     /* a for-each construct */
};


struct jmpl_any_s  {
    jmpl_op_t        type;
    iot_list_hook_t  hook;
};

struct jmpl_main_s {
    jmpl_op_t        type;               /* JMPL_OP_MAIN */
    iot_list_hook_t  hook;               /* instructions to execute */
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


struct jmpl_ifset_s {
    jmpl_op_t        type;               /* JMPL_OF_IFSET */
    iot_list_hook_t  hook;               /* to op list */
    jmpl_ref_t      *test;               /* variable to test */
    iot_list_hook_t  tbranch;            /* true branch */
    iot_list_hook_t  fbranch;            /* false branch */
};


struct jmpl_if_s {
    jmpl_op_t        type;               /* JMPL_OP_IF */
    iot_list_hook_t  hook;               /* to op list */
    jmpl_expr_t     *test;               /* expression to test */
    iot_list_hook_t  tbranch;            /* true branch */
    iot_list_hook_t  fbranch;            /* false branch */
};


struct jmpl_for_s {
    jmpl_op_t        type;               /* JMPL_OP_FOREACH */
    iot_list_hook_t  hook;               /* to op list */
    jmpl_ref_t      *key;                /* key */
    jmpl_ref_t      *val;                /* value */
    jmpl_ref_t      *in;                 /* variable reference */
    iot_list_hook_t  body;               /* foreach body */
};


union jmpl_insn_u {
    jmpl_any_t   any;
    jmpl_ifset_t ifset;
    jmpl_if_t    ifelse;
    jmpl_for_t   foreach;
    jmpl_text_t  text;
    jmpl_subst_t subst;
};


struct jmpl_ref_s {
    int32_t *ids;                        /* field name id, or index */
    int      nid;                        /* number of ids */
};

#if 1

enum jmpl_value_type_e {
    JMPL_VALUE_EXPR = 0,                 /* expression as a value */
    JMPL_VALUE_REF,                      /* variable reference */
    JMPL_VALUE_CONST,                    /* const value */
};


struct jmpl_value_s {
    jmpl_value_type_t type;              /* value type */
    union {                              /* type-specific value */
        jmpl_ref_t  *r;                  /*   variable reference */
        char        *s;                  /*   constant value */
        jmpl_expr_t *e;                  /*   expression */
    };
};


enum jmpl_expr_type_e {
    JMPL_EXPR_EQ = 0,                    /* equality test */
    JMPL_EXPR_NEQ,                       /* inequality test */
    JMPL_EXPR_OR,                        /* logical or */
    JMPL_EXPR_AND,                       /* logical and */
    JMPL_EXPR_NOT,                       /* negation */
    JMPL_EXPR_VALUE,
};


struct jmpl_expr_s {
    jmpl_expr_type_t  type;              /* expression type */
    jmpl_value_t     *lhs;               /* left-hand side value */
    jmpl_value_t     *rhs;               /* right-hand side value */
};

#else

struct jmpl_expr_s {
    int foo;
};

#endif

typedef enum {
    SCAN_MAIN = 0,
    SCAN_IF_EXPR,
    SCAN_IF_BODY,
    SCAN_FOREACH,
    SCAN_FOREACH_BODY,
    SCAN_ID,
} scan_options;


struct jmpl_parser_s {
    iot_list_hook_t  templates;
    jmpl_any_t      *jmpl;               /* top level instructions */
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
    int              tkn;                /* pushed back next token */
    char            *val;                /* pushed back token value */
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
    jmpl_op_t        type;
    iot_list_hook_t  hook;
    int32_t          data;
    char            *buf;
    size_t           size;
    size_t           used;
};



enum {
    JMPL_SYMBOL_INDEX  = 0x00000000,
    JMPL_SYMBOL_FIELD  = 0x10000000,
    JMPL_SYMBOL_STRING = 0x20000000,
} jmpl_sym_type_t;


#define JMPL_SYMTAG_MASK (0x70000000)
#define JMPL_SYMBOL_TAG(id) ((id) &  JMPL_SYMTAG_MASK)
#define JMPL_SYMBOL_IDX(id) ((id) & ~JMPL_SYMTAG_MASK)

struct jmpl_symbol_s {
    int32_t          tags;
    char            *str;
    iot_list_hook_t *values;
};


enum jmpl_symval_type_e {
    JMPL_SYMVAL_UNKNOWN = -1,
    JMPL_SYMVAL_STRING,
    JMPL_SYMVAL_INTEGER,
    JMPL_SYMVAL_JSON
};

struct jmpl_symval_s {
    jmpl_symval_type_t type;             /* runtime type in this frame */
    iot_list_hook_t    hook;             /* to value stack */
    union {                              /* type-specific value */
        const char *s;
        int         i;
        iot_json_t *j;
    };
};


int scan_next_token(jmpl_parser_t *jp, char **valp, int options);
int scan_push_token(jmpl_parser_t *jp, int tkn, char *val);

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

int symtab_push(int32_t id, int type, void *v);
int symtab_pop(int32_t id);
int symtab_resolve(jmpl_ref_t *r, void **valp);

#endif /* __JSON_TEMPLATE_PARSER_H__ */
