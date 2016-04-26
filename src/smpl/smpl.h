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

#ifndef __SMPL_H__
#define __SMPL_H__

#include <errno.h>

#include <smpl/macros.h>
#include <smpl/types.h>

SMPL_CDECL_BEGIN

/**
 * \addtogroup ScriptableTemplates
 * @{
 *
 * @file smpl.h
 *
 * @brief Scriptable template library.
 *
 */

/** Maximum default allowed size for a template file. */
#ifndef SMPL_TEMPLATE_MAXSIZE
#    define SMPL_TEMPLATE_MAXSIZE (128 * 1024)
#endif

/** Maximum default allowed size for a data file. */
#ifndef SMPL_DATA_MAXSIZE
#    define SMPL_DATA_MAXSIZE (128 * 1024)
#endif

/**
 * @brief Load a template file.
 *
 * Parse the given template file and load it for further evaluation.
 *
 * @param [in]  path    template file to load
 * @param [in]  notify  evaluation-time addon notification callback
 * @param [out] errors  pointer used for returning errors, can be @NULL
 *
 * @return Returns the parsed template upon success. Upon error @errno
 *         is set to a value approximating the reason for failure. If
 *         @errors is not @NULL it is set to a NULL-terminated array
 *         of messages describing the reason for failure.
 */
smpl_t *smpl_load_template(const char *path, smpl_addon_cb_t notify,
                           char ***errors);

/**
 * @brief Load substitution/evaluation data from a file.
 *
 * Load data for template evaluation from the given file. Currently
 * data needs to be in JSON format, with support for .ini and XML
 * being added in the future.
 *
 * @param [in]   path    path to data file
 * @param [out]  errors  pointer used for returning errors, can be @NULL
 *
 * @return Returns the data loaded from the file upon success. In case
 *         of error @errno is set to a value approximating the reason
 *         for failure. If @errors is not @NULL, it is set to an NULL-
 *         terminated array of messages describing the reason for .
 */
smpl_data_t *smpl_load_data(const char *path, char ***errors);

/**
 * @brief Reset the template search path to the given list.
 *
 * Reset the template search path to the path which is a colon-
 * separated list of directories. If @smpl is NULL, the global
 * search path is reset.
 *
 * @param [in] smpl  template to set search path for or NULL
 * @param [in] path  path to set search path to
 *
 * @return Return 0 upon succes, -1 upon error.
 */
int smpl_set_search_path(smpl_t *smpl, const char *paths);

/**
 * @brief Add the given list to the template search path.
 *
 * Append the given path list to the current template search path.
 * @path is a colon-separated list of directories. If @smpl is NULL,
 * the global search path is reset.
 *
 * @param [in] smpl  template to set search path for or NULL
 * @param [in] path  path to set search path to
 *
 * @return Return 0 upon succes, -1 upon error.
 */
int smpl_add_search_path(smpl_t *smpl, const char *paths);

/**
 * @brief Evaluate a template with data.
 *
 * Evaluate the given template in the context of the given data.
 *
 * @param [in]  smpl       template to evaluete
 * @parma [in]  data_name  name used to refer to data in templates
 * @param [in]  data       data to use for evaluation
 * @param [in]  user_data  opaque user_data for function and addon callbacks
 * @param [out] result     evaluation result
 *
 * @return Returns the string produced by template evaluation. In case
 *         of error, @NULL is returned and @errno is set to a value
 *         approximating the reason of failure. If errors is not @NULL,
 *         it is set to a NULL-terminated array of messages describing
 *         the reason for failure.
 */
int smpl_evaluate(smpl_t *smpl, const char *data_symbol, smpl_data_t *data,
                  void *user_data, smpl_result_t *result);

/**
 * @brief Free a template.
 *
 * Free the given template realasing all resources associated
 * with it.
 *
 * @param [in] smpl  template to free
 */
void smpl_free_template(smpl_t *smpl);


smpl_result_t *smpl_init_result(smpl_result_t *r, const char *destination);
void smpl_free_result(smpl_result_t *r);
char *smpl_steal_result_output(smpl_result_t *r);
char **smpl_steal_result_errors(smpl_result_t *r);
char **smpl_result_errors(smpl_result_t *r);
int smpl_write_result(smpl_result_t *r, int flags);


/**
 * @brief Free the output from a template evaluation result.
 *
 * Free the output returned in a template evaluation result
 * by @smpl_evaluate.
 *
 * @param [in] o  template evaluation result output
 */
#define smpl_free_output(o) smpl_free(o)

/**
 * @brief Free substitution/evaluation data.
 *
 * Free template data loaded by smpl_load_data.
 *
 * @param [in] data  data to be freed
 */
void smpl_free_data(smpl_data_t *data);

/**
 * @brief Free an array of error messages.
 *
 * Free the array of error messages returned by the library.
 *
 * @param [in] errors  array of error messages to free
 */
void smpl_free_errors(char **errors);

void smpl_append_errors(smpl_t *smpl, char **errors);

/**
 * @brief Register a template processing function.
 *
 * Register the given function with the given name and make it
 * available for calling from templates.
 *
 * @param [in] name       name used to refer to this function
 * @param [in] fn         pointer to function handler
 * @param [in] user_data  opaque user data to pass to @fn
 *
 * @return Return 0 upon success, -1 upon error.
 */
int smpl_register_function(char *name, smpl_fn_t fn, void *user_data);

/**
 * @brief Unregister the given template processing function.
 *
 * @param [in] name  function name to unregister
 * @param [in] fn    pointer to double-check upon unregistering
 *
 * @return Returns 0 upon success, -1 otherwise.
 */
int smpl_unregister_function(char *name, smpl_fn_t fn);

smpl_value_t *smpl_value_set(smpl_value_t *v, int type, ...);
int smpl_printf(smpl_t *smpl, const char *fmt, ...);

/**
 * @brief Print/write a template in human-readable form.
 *
 * Print out the given template in a format suitable for reading back
 * by the parser.
 *
 * @param [in] smpl  template to be printed
 * @param [in] fd    file descriptor to use for printing
 *
 * @return Returns the length of the printed template on success, -1 on error.
 */
int smpl_print_template(smpl_t *smpl, int fd);

/**
 * @brief Dump a template.
 *
 * Print out the internal representation of the given template.
 *
 * @param [in] smpl  template to be dumped
 * @param [in] fd    file descriptor to use for dumping
 *
 */
void smpl_dump_template(smpl_t *smpl, int fd);

const char *smpl_addon_name(smpl_addon_t *a);
const char *smpl_addon_template(smpl_addon_t *a);
const char *smpl_addon_destination(smpl_addon_t *a);
int smpl_addon_set_destination(smpl_addon_t *a, const char *destination);


SMPL_CDECL_END

#endif /* __SMPL_H__ */
