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

#include <smpl/macros.h>
#include <smpl/types.h>
#include <smpl/smpl.h>


smpl_t *smpl_create(char ***errbuf)
{
    smpl_t *smpl;

    if (errbuf != NULL)
        *errbuf = NULL;

    smpl = smpl_alloct(typeof(*smpl));

    if (smpl == NULL)
        goto nomem;

    if (symtbl_create(smpl) < 0)
        goto nomem;

    smpl_list_init(&smpl->macros);
    smpl_list_init(&smpl->body);

    smpl->data   = symtbl_add(smpl, "data", SMPL_SYMBOL_DATA);
    smpl->errors = errbuf;

    if (smpl->data < 0)
        goto nodata;

    return smpl;

 nomem:
    smpl_free(smpl);
    return NULL;

 nodata:
    symtbl_destroy(smpl);
    smpl_free(smpl);
    return NULL;
}


void smpl_destroy(smpl_t *smpl)
{
    if (smpl == NULL)
        return;

    parser_destroy(smpl);
    symtbl_destroy(smpl);

    template_free(&smpl->body);

    if (smpl->errors != NULL)
        smpl_errors_free(*smpl->errors);

    smpl_free(smpl);
}


smpl_t *smpl_load_template(const char *path, char ***errbuf)
{
    smpl_t *smpl;

    smpl = smpl_create(errbuf);

    if (smpl == NULL)
        goto nomem;

    smpl->parser = parser_create(smpl);

    if (smpl->parser == NULL)
        goto nomem;

    if (preproc_push_file(smpl, path) < 0)
        goto cleanup;

    if (template_parse(smpl) < 0)
        goto cleanup;

    preproc_trim(smpl);

    return smpl;

 cleanup:
    smpl->errors = NULL;
    smpl_destroy(smpl);

 nomem:
    return NULL;
}


void smpl_free_template(smpl_t *smpl)
{
    if (smpl == NULL)
        return;

    smpl_destroy(smpl);
}


smpl_data_t *smpl_load_data(const char *path, char ***errors)
{
    return smpl_json_load(path, errors);
}


void smpl_free_data(smpl_data_t *data)
{
    smpl_json_free(data);
}


char *smpl_evaluate(smpl_t *smpl, smpl_data_t *data, char ***errors)
{
    char *result;

    if (errors != NULL)
        *errors = NULL;

    smpl->errors = errors;
    smpl->result = buffer_create(8192);

    if (smpl->result == NULL)
        goto nomem;

    symtbl_flush(smpl);

    if (symtbl_push(smpl, smpl->data, SMPL_VALUE_OBJECT, data) < 0)
        goto push_fail;

    if (template_evaluate(smpl) < 0)
        goto eval_fail;

    result = buffer_steal(smpl->result);

    buffer_destroy(smpl->result);
    smpl->result = NULL;

    return result;

 nomem:
    return NULL;

 push_fail:
    smpl_fail(NULL, smpl, errno, "failed to set data");

 eval_fail:
    smpl_fail(NULL, smpl, errno, "failed to evaluate template");
}


void smpl_free_errors(char **errors)
{
    char **e;

    if (errors == NULL)
        return;

    for (e = errors; *e != NULL; e++)
        smpl_free(*e);

    smpl_free(errors);
}


int smpl_print_template(smpl_t *smpl, int fd)
{
    return template_print(smpl, fd);
}


void smpl_dump_template(smpl_t *smpl, int fd)
{
    template_dump(smpl, fd);
}

