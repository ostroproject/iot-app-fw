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

#ifndef __SMPL_TYPES_H__
#define __SMPL_TYPES_H__

#include <iot/common/json.h>
#include <smpl/macros.h>

SMPL_CDECL_BEGIN

#ifndef SMPL_TEMPLATE_MAX
#    define SMPL_TEMPLATE_MAX (256 * 1024)
#endif

#ifndef SMPL_BUFFER_SIZE
#    define SMPL_BUFFER_SIZE 8192
#endif

typedef struct smpl_s             smpl_t;
#if 0
typedef enum   smpl_format_e      smpl_format_t;
typedef struct smpl_data_s        smpl_data_t;
#else
typedef        iot_json_t         smpl_json_t;
typedef        smpl_json_t        smpl_data_t;
#endif
typedef struct smpl_symtbl_s      smpl_symtbl_t;
typedef enum   smpl_symbol_type_e smpl_symbol_type_t;
typedef        int32_t            smpl_sym_t;
typedef struct smpl_symbol_s      smpl_symbol_t;
typedef enum   smpl_value_type_e  smpl_value_type_t;
typedef struct smpl_value_s       smpl_value_t;
typedef struct smpl_parser_s      smpl_parser_t;
typedef struct smpl_input_s       smpl_input_t;
typedef struct smpl_buffer_s      smpl_buffer_t;
typedef enum   smpl_parser_flag_e smpl_parser_flag_t;
typedef enum   smpl_token_type_e  smpl_token_type_t;
typedef struct smpl_token_s       smpl_token_t;
typedef struct smpl_macro_s       smpl_macro_t;
typedef struct smpl_function_s    smpl_function_t;
typedef struct smpl_varref_s      smpl_varref_t;
typedef struct smpl_expr_type_e   smpl_expr_type_t;
typedef        smpl_value_t       smpl_expr_t;
typedef enum   smpl_insn_type_e   smpl_insn_type_t;
typedef struct smpl_insn_text_s   smpl_insn_text_t;
typedef struct smpl_insn_vref_s   smpl_insn_vref_t;
typedef struct smpl_insn_branch_s smpl_insn_branch_t;
typedef struct smpl_insn_for_s    smpl_insn_for_t;
typedef struct smpl_insn_switch_s smpl_insn_switch_t;
typedef struct smpl_insn_case_s   smpl_insn_case_t;
typedef struct smpl_insn_call_s   smpl_insn_call_t;
typedef union  smpl_insn_u        smpl_insn_t;

typedef int (*smpl_fn_t)(smpl_t *smpl, int argc, smpl_value_t *argv,
                         smpl_value_t *rv, void *user_data);


struct smpl_s {
    smpl_symtbl_t   *symtbl;             /* template symbol table */
    smpl_sym_t       data;               /* global data symbol id */
    smpl_list_t      macros;             /* template macros */
    smpl_list_t      functions;          /* template-specific functions */
    void            *user_data;          /* function callback user data */
    smpl_list_t      body;               /* template body to evaluate */
    char          ***errors;             /* user error buffer */
    int              nerror;             /* number of errors */
    smpl_parser_t   *parser;             /* associated template parser */
    smpl_buffer_t   *result;             /* template evaluation result */
    int              callbacks;          /* active function callbacks */
};


#if 0
enum smpl_format_e {
    SMPL_FORMAT_UNKNOWN = 0,             /* data in unknown format */
    SMPL_FORMAT_JSON,                    /* data in JSON format */
    SMPL_FORMAT_XML,                     /* data in XML format */
    SMPL_FORMAT_INI,                     /* data in .ini format */
};


struct smpl_data_s {
    smpl_format_t   format;              /* format of user data */
    union {                              /* actual user data */
        iot_json_t *json;                /*   in JSON format */
        void       *xml;                 /*   in XML format */
        void       *ini;                 /*   in .ini format */
    };
};
#endif


struct smpl_symtbl_s {
    smpl_symbol_t *symbols;              /* symbol table entries */
    int            nsymbol;              /* symbol table size */
};


enum smpl_symbol_type_e {
    SMPL_SYMBOL_UNKNOWN  =   -1,         /* unknown symbol */
    SMPL_SYMBOL_INDEX    = 0x00 << 24,   /* an index, not a symbol */
    SMPL_SYMBOL_NAME     = 0x01 << 24,   /* a variable name/field */
    SMPL_SYMBOL_FIELD    = 0x01 << 24,   /* a variable name/field */
    SMPL_SYMBOL_MACRO    = 0x02 << 24,   /* a macro name */
    SMPL_SYMBOL_FUNCTION = 0x04 << 24,   /* a function name */
    SMPL_SYMBOL_STRING   = 0x08 << 24,   /* a quoted string */
    SMPL_SYMBOL_LOOP     = 0x10 << 24,   /* a for(each) loop variable */
    SMPL_SYMBOL_DATA     = 0x20 << 24,   /* global substitution data */
    SMPL_SYMBOL_ARG      = 0x40 << 24,   /* macro/function argument name */
    SMPL_SYMBOL_MASK     = 0xff << 24,   /* symbol type mask */
};

#define SMPL_SYMBOL_TAG(id) ((id) &  SMPL_SYMBOL_MASK)
#define SMPL_SYMBOL_IDX(id) ((id) & ~SMPL_SYMBOL_MASK)

struct smpl_symbol_s {
    int32_t        mask;                 /* symbol type mask */
    char          *symbol;               /* symbol name */
    smpl_list_t   *values;               /* (runtime) value stack */
};


enum smpl_value_type_e {
    SMPL_VALUE_UNKNOWN = -1,             /* unknown value */
    SMPL_VALUE_UNSET,                    /* unset value */
    SMPL_VALUE_VARREF,                   /* variable reference */
    SMPL_VALUE_STRING,                   /* string value */
    SMPL_VALUE_INTEGER,                  /* integer value */
    SMPL_VALUE_DOUBLE,                   /* double-prec. floating point value */
    SMPL_VALUE_OBJECT,                   /* object/dictionary value */
    SMPL_VALUE_ARRAY,                    /* array value */
    SMPL_VALUE_AND,                      /* expression, logical and  */
    SMPL_VALUE_OR,                       /* expression, logical or */
    SMPL_VALUE_EQUAL,                    /* expression, equality test */
    SMPL_VALUE_NOTEQ,                    /* expression, inequality test */
    SMPL_VALUE_NOT,                      /* expression, negated value */
    SMPL_VALUE_IS,                       /* expression, value */
    SMPL_VALUE_FIRST,                    /* expression, first check */
    SMPL_VALUE_LAST,                     /* expression, last check */
    SMPL_VALUE_TRAIL,                    /* expression, trail check */
    SMPL_VALUE_MACROREF,                 /* expression, macro call */
    SMPL_VALUE_FUNCREF,                  /* expression, function call */
    SMPL_VALUE_ARGLIST,
    SMPL_VALUE_DYNAMIC = 0x1000,         /* actual valuye needs to be freed */
};


#define SMPL_LOOP_FIRST 0x01
#define SMPL_LOOP_LAST  0x10

struct smpl_value_s {
    smpl_value_type_t      type;         /* type of this value */
    smpl_list_t            hook;         /* to any list/queue/stack */
    struct {
        union {                          /* type-specific value */
            smpl_varref_t *ref;          /*   variable reference */
            char          *str;          /*   string */
            int32_t        i32;          /*   integer */
            smpl_sym_t     sym;          /*   symbol id */
            double         dbl;          /*   double-prec. floating point */
            smpl_json_t   *json;         /*   external JSON data */
            struct {                     /*   expression */
                smpl_value_t *arg1;      /*     argument */
                smpl_value_t *arg2;      /*     argument */
            } expr;
            struct {                     /*   call-like macro or function call */
                union {
                    smpl_macro_t    *m;  /*     referenced macro */
                    smpl_function_t *f;  /*     function to call */
                };
                int           narg;      /*     number of arguments */
                smpl_value_t *args;      /*     arguments */
            } call;
        };
        int *loopflags;                  /* loop first/last indicator */
        int  dynamic;                    /* actual value needs to be freed */
    };
};


struct smpl_parser_s {
    smpl_t        *smpl;                 /* template being parsed */
    char          *mbeg;                 /* directive start marker */
    int            lbeg;                 /*   and its length */
    char          *mend;                 /* directive end marker */
    int            lend;                 /*   and its length */
    char          *mtab;                 /* tabulation marker */
    int            ltab;                 /*   and its length */
    smpl_input_t  *in;                   /* active input buffer */
    smpl_list_t    inq;                  /* input buffer queue */
    smpl_buffer_t *buf;                  /* active token buffer */
    smpl_list_t    bufq;                 /* token buffer queue */
    smpl_list_t    tknq;                 /* token pushback queue */
};


struct smpl_input_s {
    char        *p;                      /* buffer read pointer */
    char        *buf;                    /* preprocessed input */
    int          size;                   /* buffer size */
    char        *path;                   /* source of input */
    int          line;                   /* current line number */
    dev_t        dev;                    /* input file device */
    ino_t        ino;                    /* input file inode */
    smpl_list_t  hook;                   /* to input buffer queue */
};


struct smpl_buffer_s {
    char        *p;                      /* insertion pointer */
    char        *buf;                    /* allocated buffer space */
    int          size;                   /* buffer size */
    smpl_list_t  hook;                   /* to list of buffers */
};


enum smpl_parser_flag_e {
    SMPL_ALLOW_MACROS    = 0x0100,
    SMPL_ALLOW_INCLUDE   = 0x0200,
    SMPL_SKIP_WHITESPACE = 0x0400,
    SMPL_BLOCK_DO        = 0x0800,
    SMPL_BLOCK_ELSE      = 0x1000,
    SMPL_BLOCK_END       = 0x2000,
    SMPL_BLOCK_DOELSEEND = SMPL_BLOCK_DO|SMPL_BLOCK_ELSE|SMPL_BLOCK_END,
    SMPL_BLOCK_ELSEEND   =               SMPL_BLOCK_ELSE|SMPL_BLOCK_END,
    SMPL_BLOCK_DOEND     = SMPL_BLOCK_DO|                SMPL_BLOCK_END,

    SMPL_PARSE_BLOCK   = 0,
    SMPL_PARSE_MAIN    = SMPL_PARSE_BLOCK|SMPL_ALLOW_INCLUDE|SMPL_ALLOW_MACROS,
    SMPL_PARSE_MACRO   = SMPL_PARSE_BLOCK,
    SMPL_PARSE_NAME    = SMPL_PARSE_BLOCK + 1,
    SMPL_PARSE_ARGS,
    SMPL_PARSE_EXPR,
    SMPL_PARSE_SWITCH,
};


enum smpl_token_type_e {
    SMPL_TOKEN_ERROR       = -1,
    SMPL_TOKEN_EOF         = 0,

    SMPL_TOKEN_PAREN_OPEN  = '(',
    SMPL_TOKEN_PAREN_CLOSE = ')',
    SMPL_TOKEN_INDEX_OPEN  = '[',
    SMPL_TOKEN_INDEX_CLOSE = ']',
    SMPL_TOKEN_DOT         = '.',
    SMPL_TOKEN_COLON       = ':',
    SMPL_TOKEN_COMMA       = ',',
    SMPL_TOKEN_NOT         = '!',
    SMPL_TOKEN_IS          = '?',

    SMPL_TOKEN_COMMENT = 128,
    SMPL_TOKEN_INCLUDE,
    SMPL_TOKEN_MACRO,
    SMPL_TOKEN_IF,
    SMPL_TOKEN_FOR,
    SMPL_TOKEN_SWITCH,
    SMPL_TOKEN_IN,
    SMPL_TOKEN_DO,
    SMPL_TOKEN_ELSE,
    SMPL_TOKEN_END,
    SMPL_TOKEN_CASE,
    SMPL_TOKEN_FIRST,
    SMPL_TOKEN_LAST,
    SMPL_TOKEN_TRAIL,

    SMPL_TOKEN_MACROREF,
    SMPL_TOKEN_FUNCREF,
    SMPL_TOKEN_TEXT,
    SMPL_TOKEN_ESCAPE,
    SMPL_TOKEN_NAME,
    SMPL_TOKEN_VARREF,
    SMPL_TOKEN_STRING,
    SMPL_TOKEN_INTEGER,
    SMPL_TOKEN_DOUBLE,

    SMPL_TOKEN_AND,
    SMPL_TOKEN_OR,
    SMPL_TOKEN_EQUAL,
    SMPL_TOKEN_NOTEQ,
};


struct smpl_token_s {
    smpl_token_type_t    type;           /* token type */
    char                *str;            /* token literal/string value */
    const char          *path;           /* file of occurence */
    int                  line;           /* line of occurence */
    union {                              /* type-specific value */
        int32_t          i32;            /*   integer value */
        double           dbl;            /*   floating point value */
        smpl_sym_t       sym;            /*   symbol id */
        smpl_macro_t    *m;              /*   macro */
        smpl_function_t *f;              /* function */
    };
    smpl_list_t          hook;           /* for pushback to token queue */
};


struct smpl_macro_s {
    smpl_sym_t  name;                    /* macro name symbol */
    smpl_sym_t *args;                    /* arguments */
    int         narg;                    /* number of arguments */
    smpl_list_t body;                    /* macro body to evaluate */
    smpl_list_t hook;                    /* to list of macros */
};


struct smpl_function_s {
    char        *name;                   /* exposed with this name */
    smpl_fn_t    cb;                     /* function to call back */
    void        *user_data;              /* opaque user data */
    smpl_list_t  hook;                   /* to list of functions */
};


enum smpl_insn_type_e {
    SMPL_INSN_INVALID = -1,
    SMPL_INSN_TEXT,
    SMPL_INSN_VARREF,
    SMPL_INSN_BRANCH,
    SMPL_INSN_FOR,
    SMPL_INSN_SWITCH,
    SMPL_INSN_MACROREF,
    SMPL_INSN_FUNCREF,
};


struct smpl_varref_s {
    smpl_sym_t *symbols;                 /* reference symbols/indices */
    int         nsymbol;                 /* number of symbols/indices */
};


#define SMPL_INSN_COMMON                                                \
    smpl_insn_type_t  type;              /* instruction type */        \
    const char       *path;              /* path of occurence */       \
    int               line;              /* line of occurence */       \
    smpl_list_t       hook               /* to block of instructions */


struct smpl_insn_text_s {
    SMPL_INSN_COMMON;                    /* generic instruction fields */
    char *text;                          /* text to insert */
};


struct smpl_insn_vref_s {
    SMPL_INSN_COMMON;                    /* generic instruction fields */
    smpl_varref_t *ref;                  /* variable to substitute */
};


struct smpl_insn_branch_s {
    SMPL_INSN_COMMON;                    /* generic instruction fields */
    smpl_expr_t *test;                   /* expression to branch on */
    smpl_list_t  posbr;                  /* body of positive branch */
    smpl_list_t  negbr;                  /* body of negative branch */
};


struct smpl_insn_for_s {
    SMPL_INSN_COMMON;                    /* generic expression fields */
    smpl_sym_t     key;                  /* loop variable for key */
    smpl_sym_t     val;                  /* loop variable for value */
    smpl_varref_t *ref;                  /* variable to loop through */
    smpl_list_t    body;                 /* body of loop */
};


struct smpl_insn_switch_s {
    SMPL_INSN_COMMON;                    /* generic expression fields */
    smpl_expr_t *test;                   /* expression to switch on */
    smpl_list_t  cases;                  /* value-specific branches */
    smpl_list_t  defbr;                  /* default branch */
};


struct smpl_insn_case_s {
    smpl_expr_t *expr;                   /* case test value */
    smpl_list_t  body;                   /* case body */
    smpl_list_t  hook;                   /* to list of cases */
};


struct smpl_insn_call_s {
    SMPL_INSN_COMMON;                    /* generic expression fields */
    union {
        smpl_macro_t    *m;              /* macro to evaluate */
        smpl_function_t *f;              /* function to call */
    };
    smpl_expr_t  *expr;                  /* macro call expression */
};


union smpl_insn_u {
    smpl_insn_type_t   type;             /* instruction type */
    struct {                             /* type-agnostic common fields */
        SMPL_INSN_COMMON;
    }                  any;
    smpl_insn_text_t   text;             /* verbatim text insert */
    smpl_insn_vref_t   vref;             /* variable substitution */
    smpl_insn_branch_t branch;           /* if-else branch */
    smpl_insn_for_t    loop;             /* foreach loop */
    smpl_insn_switch_t swtch;            /* switch branch */
    smpl_insn_call_t   call;             /* function/macro call */
};


/** Record and error related for input file <path>:<line>. */
#define smpl_return_error(_r, _smpl, _error, _path, _line, ...) do {    \
        smpl_errmsg(_smpl, _error, _path, _line, __VA_ARGS__);          \
        return _r;                                                      \
    } while (0)


/** Bail out from the current function, recording an error. */
#define smpl_fail(_r, _smpl, _error, ...) do {                          \
        smpl_return_error(_r, _smpl, _error,                            \
                          _smpl->parser && smpl->parser->in ?           \
                          _smpl->parser->in->path : NULL,               \
                          _smpl->parser && smpl->parser->in ?           \
                          _smpl->parser->in->line : 0,                  \
                          __VA_ARGS__);                                 \
    } while (0)


void smpl_errmsg(smpl_t *smpl, int error, const char *path, int line,
                 const char *fmt, ...) SMPL_PRINTF_LIKE(5, 6);
void smpl_errors_free(char **errors);

int symtbl_create(smpl_t *smpl);
void symtbl_destroy(smpl_t *smpl);
smpl_sym_t symtbl_add(smpl_t *smpl, const char *name, int32_t tag);
smpl_sym_t symtbl_find(smpl_t *smpl, const char *name, int mask);
const char *symtbl_get(smpl_t *smpl, smpl_sym_t id);
int symtbl_push(smpl_t *smpl, smpl_sym_t sym, smpl_value_type_t type, void *v);
int symtbl_push_loop(smpl_t *smpl, smpl_sym_t sym,
                     smpl_value_type_t type, void *val, int *loopflags);
int symtbl_loopflag(smpl_t *smpl, smpl_sym_t sym, int flag);
int symtbl_pop(smpl_t *smpl, smpl_sym_t sym);

int symbtl_value(smpl_t *smpl, smpl_sym_t sym, smpl_value_t *val);
int symtbl_resolve(smpl_t *smpl, smpl_varref_t *vref, smpl_value_t *val);
void symtbl_flush(smpl_t *smpl);

smpl_varref_t *varref_parse(smpl_t *smpl, char *str, const char *path, int line);
void varref_free(smpl_varref_t *vref);
char *varref_print(smpl_t *smpl, smpl_varref_t *vref, char *buf, size_t size);
char *varref_string(smpl_t *smpl, smpl_varref_t *vref, char *buf, size_t size);

smpl_expr_t *expr_parse(smpl_t *smpl, smpl_token_t *end);
smpl_expr_t *expr_first_parse(smpl_t *smpl, smpl_token_t *t, smpl_token_t *name);
smpl_expr_t *expr_trail_parse(smpl_t *smpl, smpl_token_t *t);
void expr_free(smpl_expr_t *expr);
int expr_print(smpl_t *smpl, smpl_expr_t *expr, char *buf, size_t size);
int expr_eval(smpl_t *smpl, smpl_expr_t *e, smpl_value_t *v);
int expr_test(smpl_t *smpl, smpl_expr_t *e, smpl_value_t *v);
int expr_compare_values(smpl_value_t *v1, smpl_value_t *v2);
int value_eval(smpl_t *smpl, smpl_expr_t *e);
smpl_value_t *value_set(smpl_value_t *v, int type, ...);
smpl_value_t *value_setv(smpl_value_t *v, int type, va_list ap);
smpl_value_t *value_copy(smpl_value_t *dst, smpl_value_t *src);
void value_reset(smpl_value_t *v);

smpl_parser_t *parser_create(smpl_t *smpl);
void parser_destroy(smpl_t *smpl);
int parse_markers(smpl_t *smpl, char *buf, const char *path);
int parse_block(smpl_t *smpl, int flags, smpl_list_t *block, smpl_token_t *end);
int parser_pull_token(smpl_t *smpl, int flags, smpl_token_t *t);
int parser_push_token(smpl_t *smpl, smpl_token_t *tkn);
void parser_skip_newline(smpl_t *smpl);
const char *token_name(int type);

int preproc_push_file(smpl_t *smpl, const char *file);
int preproc_pull(smpl_t *smpl);
void preproc_trim(smpl_t *smpl);
void preproc_purge(smpl_t *smpl);

int template_parse(smpl_t *smpl);
void template_free(smpl_list_t *body);
int template_evaluate(smpl_t *smpl);
void template_dump(smpl_t *smpl, int fd);
int template_print(smpl_t *smpl, int fd);

#define block_parse parse_block
void block_free(smpl_list_t *block);
void block_dump(smpl_t *smpl, int fd, smpl_list_t *block, int indent);
int block_eval(smpl_t *smpl, smpl_list_t *block);

int macro_parse(smpl_t *smpl);
void macro_free(smpl_macro_t *m);
int macro_parse_ref(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void macro_dump_ref(smpl_t *smpl, int fd, smpl_insn_call_t *c, int indent);
void macro_free_ref(smpl_insn_t *insn);
int macro_eval(smpl_t *smpl, smpl_insn_call_t *c);
void macro_purge(smpl_list_t *macros);
void macro_dump(smpl_t *smpl, int fd, smpl_macro_t *m);
smpl_macro_t *macro_find(smpl_t *smpl, smpl_sym_t sym);
smpl_macro_t *macro_by_name(smpl_t *smpl, const char *name);


int function_register(smpl_t *smpl, char *name, smpl_fn_t fn, void *user_data);
int function_unregister(smpl_t *smpl, char *name, smpl_fn_t fn);
void function_purge(smpl_list_t *functions);
smpl_function_t *function_find(smpl_t *smpl, const char *name);
int function_parse_ref(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void function_dump_ref(smpl_t *smpl, int fd, smpl_insn_call_t *c, int indent);
void function_free_ref(smpl_insn_t *insn);
int function_call(smpl_t *smpl, smpl_function_t *f, int narg, smpl_value_t *args,
                  smpl_value_t *rv);
int function_eval(smpl_t *smpl, smpl_insn_call_t *c);

void builtin_register(void);

int text_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void text_free(smpl_insn_t *insn);
void text_dump(smpl_t *smpl, int fd, smpl_insn_text_t *text, int indent);
int text_eval(smpl_t *smpl, smpl_insn_text_t *text);

int vref_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void vref_free(smpl_insn_t *insn);
void vref_dump(smpl_t *smpl, int fd, smpl_insn_vref_t *ref, int indent);
int vref_eval(smpl_t *smpl, smpl_insn_vref_t *ref);

int branch_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void branch_free(smpl_insn_t *insn);
void branch_dump(smpl_t *smpl, int fd, smpl_insn_branch_t *branch, int indent);
int branch_eval(smpl_t *smpl, smpl_insn_branch_t *branch);

int loop_parse(smpl_t *smpl, smpl_token_t *t, smpl_list_t *block);
void loop_free(smpl_insn_t *insn);
void loop_dump(smpl_t *smpl, int fd, smpl_insn_for_t *loop, int indent);
int loop_eval(smpl_t *smpl, smpl_insn_for_t *loop);

int switch_parse(smpl_t *smpl, smpl_list_t *block);
void switch_free(smpl_insn_t *insn);
void switch_dump(smpl_t *smpl, int fd, smpl_insn_switch_t *sw, int indent);
int switch_eval(smpl_t *smpl, smpl_insn_switch_t *sw);

smpl_json_t *smpl_json_load(const char *path, char ***errors);
void smpl_json_free(smpl_json_t *json);

smpl_buffer_t *buffer_create(int size);
void buffer_destroy(smpl_buffer_t *b);
char *buffer_alloc(smpl_list_t *bufs, int size);
int buffer_printf(smpl_buffer_t *b, const char *fmt, ...);
int buffer_vprintf(smpl_buffer_t *b, const char *fmt, va_list ap);
char *buffer_steal(smpl_buffer_t *b);
void buffer_purge(smpl_list_t *bufs);


#define SMPL_INDENT_FMT        "%*.*s"
#define SMPL_INDENT_ARG(level) 2*(level), 2*(level), ""


SMPL_CDECL_END

#endif /* __SMPL_TYPES_H__ */
