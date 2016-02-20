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
 * @param [out] errors  pointer used for returning errors, can be @NULL
 *
 * @return Returns the parsed template upon success. Upon error @errno
 *         is set to a value approximating the reason for failure. If
 *         @errors is not @NULL it is set to a NULL-terminated array
 *         of messages describing the reason for failure.
 */
smpl_t *smpl_load_template(const char *path, char ***errors);

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
 * @brief Evaluate a template with data.
 *
 * Evaluate the given template in the context of the given data.
 *
 * @param [in]  smpl    template to evaluete
 * @param [in]  data    data to use for evaluation
 * @param [out] errors  pointer used for returning errors, can be @NULL
 *
 * @return Returns the string produced by template evaluation. In case
 *         of error, @NULL is returned and @errno is set to a value
 *         approximating the reason of failure. If errors is not @NULL,
 *         it is set to a NULL-terminated array of messages describing
 *         the reason for failure.
 */
char *smpl_evaluate(smpl_t *smpl, smpl_data_t *data, char ***errors);

/**
 * @brief Free a template.
 *
 * Free the given template realasing all resources associated
 * with it.
 *
 * @param [in] smpl  template to free
 */
void smpl_free_template(smpl_t *smpl);

/**
 * @brief Free the result of template evaluation.
 *
 * Free the result returned by @smpl_evaluate.
 *
 * @param [in] result  template evaluation result
 */
#define smpl_free_result(_r) smpl_free(_r)

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




SMPL_CDECL_END

#endif /* __SMPL_H__ */
