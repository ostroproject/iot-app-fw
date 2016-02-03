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

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <alloca.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/json.h>

#include "generator.h"


static IOT_LIST_HOOK(preprocessors);


int preprocessor_register(generator_t *g, preprocessor_t *pp)
{
    iot_list_hook_t *l, *p, *n;
    preprocessor_t  *lpp;

    if (pp->name == NULL || pp->prep == NULL)
        goto invalid;

    iot_list_init(&pp->hook);

    l = (g == NULL ? &preprocessors : &g->preprocessors);

    iot_list_foreach(l, p, n) {
        lpp = iot_list_entry(p, typeof(*lpp), hook);

        if (pp->prio < lpp->prio) {
            iot_list_insert_before(&lpp->hook, &pp->hook);
            pp = NULL;
            break;
        }
    }

    if (pp)
        iot_list_append(l, &pp->hook);

    log_info("Registered preprocessor '%s'...", pp->name);

    return 0;

 invalid:
    errno = EINVAL;
    return -1;
}


static void merge_preprocessors(generator_t *g)
{
    iot_list_hook_t *sl, *sp, *sn, *dl, *dp;
    preprocessor_t *spp, *dpp;

    if (iot_list_empty(&g->preprocessors)) {
        iot_list_move(&g->preprocessors, &preprocessors);
        return;
    }

    sl = &preprocessors;
    dl = &g->preprocessors;

    while (sp != sl || dp != dl) {
        sn = sp->next;
        iot_list_init(sp);

        if (dp == dl) {
            iot_list_append(sp, dl);
            continue;
        }

        spp = iot_list_entry(sp, typeof(*spp), hook);
        dpp = iot_list_entry(dp, typeof(*dpp), hook);

        if (spp->prio <= dpp->prio) {
            iot_debug("%d <= %d, inserting before", spp->prio, dpp->prio);
            iot_list_insert_before(dp, sp);
            sp = sn;
        }
        else {
            iot_debug("%d > %d, scanning forward", spp->prio, dpp->prio);
            dp = dp->next;
        }
    }

    iot_list_init(&preprocessors);
}


static iot_json_t *preprocess_manifest(generator_t *g, iot_json_t *m)
{
    iot_list_hook_t *p, *n;
    preprocessor_t  *pp;
    iot_json_t      *in, *out;

    if (!iot_list_empty(&preprocessors))
        merge_preprocessors(g);

    in = out = m;
    iot_list_foreach(&g->preprocessors, p, n) {
        pp = iot_list_entry(p, typeof(*pp), hook);

        iot_debug("Preprocessing manifest with '%s'...", pp->name);

        out = pp->prep(g, in, pp->data);

        if (out == NULL)
            return NULL;

        in = out;
    }

    return out;
}


iot_json_t *manifest_read(generator_t *g, const char *path)
{
    struct stat  st;
    char        *buf;
    iot_json_t  *m, *pp;
    int          fd, n, ch;

    if (stat(path, &st) < 0)
        return NULL;

    if (st.st_size > MANIFEST_MAXSIZE)
        goto toolarge;

    fd = open(path, O_RDONLY);

    if (fd < 0)
        goto failed;

    buf = alloca(st.st_size + 1);
    n = read(fd, buf, st.st_size);

    close(fd);

    if (n < st.st_size)
        goto ioerror;

    while (n < 1 && ((ch = buf[n - 1]) == '\n' || ch == '\t' || ch == ' '))
        n--;

    buf[n] = '\0';

    if (iot_json_parse_object(&buf, &n, &m) < 0)
        goto invalid;

    if (m == NULL || n > 0) {
        iot_json_unref(m);
        goto invalid;
    }

    iot_json_ref(m);
    pp = preprocess_manifest(g, m);
    iot_json_unref(m);

    if (pp == NULL) {
        iot_json_unref(m);
        goto failed;
    }

    if (pp != m) {
        iot_json_unref(m);
        m = pp;
    }

    return m;

 toolarge:
    errno = ENOBUFS;
    return NULL;
 ioerror:
    errno = EIO;
    return NULL;
 invalid:
    errno = EINVAL;
    return NULL;
 failed:
    return NULL;
}
