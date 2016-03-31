/*
 * Copyright (c) 2012-2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <errno.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/regexp.h>


iot_regexp_t *iot_regexp_compile(const char *pattern, int flags)
{
    iot_regexp_t *re;

    if ((re = iot_allocz(sizeof(*re))) == NULL)
        goto nomem;

    if (regcomp(re, pattern, flags) != 0)
        goto invalid;

    return re;

 nomem:
    errno = ENOMEM;
    return NULL;

 invalid:
    iot_free(re);
    errno = EINVAL;
    return NULL;
}


void iot_regexp_free(iot_regexp_t *re)
{
    if (re == NULL)
        return;

    regfree(re);
    iot_free(re);
}


bool iot_regexp_matches(iot_regexp_t *re, const char *input, int flags)
{
    if (regexec(re, input, 0, NULL, flags) != 0)
        return false;
    else
        return true;
}


int iot_regexp_exec(iot_regexp_t *re, const char *input, iot_regmatch_t *matches,
                    size_t nmatch, int flags)
{
    int i;

    if (regexec(re, input, nmatch, matches, flags) != 0)
        return 0;
    else {
        if (nmatch > 0 && matches != NULL) {
            for (i = 0; i < (int)nmatch; i++)
                if (matches[i].rm_so == -1)
                    return i;
        }

        return 1;
    }
}


bool iot_regexp_match(iot_regmatch_t *matches, int idx, int *beg, int *end)
{
    bool valid = matches[idx].rm_so >= 0;

    if (beg != NULL)
        *beg = (int)matches[idx].rm_so;
    if (end != NULL)
        *end = (int)matches[idx].rm_eo;

    return valid;
}
