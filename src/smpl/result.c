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

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <smpl/macros.h>
#include <smpl/types.h>
#include <smpl/smpl.h>


smpl_result_t *result_init(smpl_result_t *r, const char *destination)
{
    if (r == NULL)
        return NULL;

    smpl_clear(r);
    smpl_list_init(&r->addons);

    if (result_set_destination(r, destination) < 0)
        return NULL;

    return r;
}


void result_free(smpl_result_t *r)
{
    smpl_list_t  *p, *n;
    smpl_addon_t *a;

    if (r == NULL)
        return;

    smpl_free_output(r->output);
    smpl_free_errors(r->errors);
    smpl_free(r->destination);

    r->output      = NULL;
    r->errors      = NULL;
    r->destination = NULL;

    smpl_list_foreach(&r->addons, p, n) {
        a = smpl_list_entry(p, typeof(*a), hook);
        addon_free(a);
    }
}


int result_set_destination(smpl_result_t *r, const char *destination)
{
    smpl_free(r->destination);
    r->destination = smpl_strdup(destination);

    if (r->destination == NULL && destination != NULL)
        return -1;
    else
        return 0;
}


char *result_steal_output(smpl_result_t *r)
{
    char *output;

    if (r == NULL)
        return NULL;

    output = r->output;
    r->output = NULL;

    return output;
}


char **result_steal_errors(smpl_result_t *r)
{
    char **errors;

    errors = r->errors;
    r->errors = NULL;

    return errors;
}


char **result_errors(smpl_result_t *r)
{
    return r->errors;
}


int write_output(char *output, char *destination, int flags, mode_t mode)
{
    char *p;
    int   fd, l, n;

    if (!strncmp(destination, "/proc/", 6))
        fd = open(destination, O_WRONLY);
    else
        fd = open(destination, flags | O_WRONLY, mode);

    if (fd < 0)
        return -1;

    p = output;
    l = strlen(p);
    n = 0;

    while (l > 0) {
        n = write(fd, p, l);

        if (n < 0) {
            if (errno == EAGAIN)
                continue;
            else
                goto fail;
        }

        p += n;
        l -= n;
    }

    close(fd);

    return 0;

 fail:
    close(fd);
    return -1;
}


int result_write(smpl_result_t *r, int flags, int wflags)
{
    smpl_list_t  *p, *n;
    smpl_addon_t *a;

    if (wflags & SMPL_WRITE_MAIN) {
        if (r->output == NULL)
            goto no_output;

        if (r->destination == NULL)
            goto no_destination;

        smpl_debug("writing template output to %s...", r->destination);

        if (write_output(r->output, r->destination, flags, 0644) < 0)
            goto write_error;
    }

    if (wflags & SMPL_WRITE_ADDONS) {
        smpl_list_foreach(&r->addons, p, n) {
            a = smpl_list_entry(p, typeof(*a), hook);
            if (result_write(&a->result, flags | O_WRONLY, SMPL_WRITE_ALL) < 0)
                goto write_error;
        }
    }

    return 0;

 no_output:
    errno = EINVAL;
    return -1;

 no_destination:
    errno = EINVAL;
    return -1;

 write_error:
    return -1;
}


int result_process(smpl_result_t *r, int (*cb)(smpl_addon_t *addon,
                                               const char *output,
                                               const char *destination,
                                               const char *name,
                                               void *user_data),
                   void *user_data)
{
    smpl_addon_t *addon;
    smpl_list_t  *p, *n;
    const char   *out;
    const char   *dest;
    const char   *name;
    int           ar, mr;

    out  = r->output;
    dest = r->destination;
    name = "<main template>";

    mr = cb(NULL, out, dest, name, user_data);

    if (mr < 0)
        goto main_failed;

    smpl_list_foreach(&r->addons, p, n) {
        addon = smpl_list_entry(p, typeof(*addon), hook);
        out   = addon->result.output;
        dest  = addon->result.destination;
        name  = addon->name;

        ar = cb(addon, out, dest, name, user_data);

        switch (ar) {
        case SMPL_RESULT_OK:
            break;
        case SMPL_RESULT_FREE:
            addon_free(addon);
            break;
        case SMPL_RESULT_STOLEN:
            smpl_steal_result_output(&addon->result);
            break;
        default:
            goto addon_failed;
        }
    }

    switch (mr) {
    case SMPL_RESULT_OK:
        break;
    case SMPL_RESULT_FREE:
        result_free(r);
        break;
    case SMPL_RESULT_STOLEN:
        smpl_steal_result_output(r);
        break;
    default:
        goto main_failed;
    }

    return 0;

 main_failed:
    return -1;

 addon_failed:
    return -1;

}
