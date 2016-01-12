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

#ifndef __JMPL_TYPES_H__
#define __JMPL_TYPES_H__

#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/json.h>


typedef struct jmpl_expr_s jmpl_expr_t;
typedef struct jmpl_ref_s  jmpl_ref_t;
typedef struct jmpl_val_s  jmpl_val_t;


struct jmpl_s {
    iot_list_hook_t fragments;
};


typedef enum {
    JMPL_TYPE_TEXT = 0,                  /* plain text to produce */
    JMPL_TYPE_SUBST,
    JMPL_TYPE_IFSET,
    JMPL_TYPE_IF,
    JMPL_TYPE_FOREACH,
} jmpl_type_t;


typedef struct {
    iot_list_hook_t  hook;
    jmpl_type_t      type;
    char            *text;
} jmpl_text_t;


typedef struct {
    iot_list_hook_t  hook;
    jmpl_type_t      type;
    jmpl_ref_t      *ref;
} jmpl_subst_t;


typedef struct {
    iot_list_hook_t  hook;
    jmpl_type_t      type;
    jmpl_ref_t      *test;
    jmpl_t          *pos_branch;
    jmpl_t          *neg_branch;
} jmpl_ifset_t;


typedef struct {
    iot_list_hook_t  hook;
    jmpl_type_t      type;
    jmpl_expr_t     *test;
    jmpl_t          *pos_branch;
    jmpl_t          *neg_branch;
} jmpl_if_t;


typedef struct {
    iot_list_hook_t  hook;
    jmpl_type_t      type;
    char            *key;
    char            *val;
    jmpl_t          *body;
} jmpl_foreach_t;


typedef enum {
    JMPL_FIELD_NAME = 0,
    JMPL_FIELD_INDEX,
} jmpl_field_type_t;


typedef struct {
    jmpl_field_type_t type;
    union {
        char *name;
        int   idx;
    };
} jmpl_field_t;


struct jmpl_ref_s {
    jmpl_field_t *fields;
    int           nfield;
};


typedef enum {
    JMPL_REF = 0,
    JMPL_CONST,
} jmpl_value_type_t;


typedef struct {
    jmpl_value_type_t type;
    union {
        jmpl_ref_t *r;
        char       *c;
    };
} jmpl_value_t;


typedef enum {
    JMPL_EXPR_EQ,
    JMPL_EXPR_NEQ,
    JMPL_AND,
    JMPL_OR,
    JMPL_NOT,
} jmpl_expr_type_t;


struct jmpl_expr_s {
    jmpl_expr_type_t  type;
    jmpl_val_t       *lhs;
    jmpl_val_t       *rhs;
};

struct jmpl_val_s {
    int type;
    union {
        char       *val;
        jmpl_ref_t *ref;
    };
};


#endif /* __JMPL_TYPES_H__ */
