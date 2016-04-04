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

static int fn_error(smpl_t *smpl, int argc, smpl_value_t *argv,
                    smpl_value_t *rv, void *user_data)
{
    const char *msg;
    int         err;

    SMPL_UNUSED(user_data);

    err = -1;
    msg = "template evaluation failure";

    if (argc == 1) {
        switch (argv[0].type) {
        case SMPL_VALUE_INTEGER:
            err = argv[0].i32;
            break;
        case SMPL_VALUE_STRING:
            msg = argv[0].str;
            break;
        default:
            break;
        }
    }
    else if (argc == 2) {
        if (argv[0].type == SMPL_VALUE_INTEGER &&
            argv[1].type == SMPL_VALUE_STRING) {
            err = argv[0].i32;
            msg = argv[1].str;
        }
    }

    if (rv != NULL)
        rv->type = SMPL_VALUE_UNSET;

    smpl_fail(-1, smpl, err, "%s", msg);
}


static int fn_warning(smpl_t *smpl, int argc, smpl_value_t *argv,
                      smpl_value_t *rv, void *user_data)
{
    int i;

    SMPL_UNUSED(smpl);
    SMPL_UNUSED(user_data);

    for (i = 0; i < argc; i++) {
        if (argv[i].type == SMPL_VALUE_STRING)
            smpl_warn("template evaluation warning: %s", argv[i].str);
    }

    if (rv != NULL)
        rv->type = SMPL_VALUE_UNSET;

    return 0;
}


void builtin_register(void)
{
    function_register(NULL, "ERROR"  , fn_error  , NULL);
    function_register(NULL, "WARNING", fn_warning, NULL);
}
