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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <smpl/macros.h>
#include <smpl/types.h>

static int filter_tabulation(smpl_t *smpl, char *in, char *out);
int preproc_pending_file(smpl_t *smpl, const char *path);

static char *absolute_path(const char *file, char *path, size_t size)
{
    char cwd[PATH_MAX], dir[PATH_MAX], *p;
    int  n;

    if (file[0] == '/') {
        n = snprintf(path, size, "%s", file);

        if (n >= (int)size)
            goto toolong;

        return path;
    }

    n = snprintf(dir, size, "%s", file);

    if (n >= (int)size)
        goto toolong;

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        goto failed;

    p = strrchr(file, '/');

    if (p != NULL) {
        n = (int)(p - file);
        n = snprintf(dir, sizeof(dir), "%*.*s", n, n, file);

        if (n >= (int)sizeof(dir))
            goto toolong;

        if (chdir(dir) < 0)
            goto failed;

        if (getcwd(dir, sizeof(dir)) == NULL)
            goto failed;

        if (chdir(cwd) < 0)
            goto failed;

        while (*p == '/')
            p++;

        n = snprintf(path, size, "%s/%s", dir, p);

        if (n >= (int)size)
            goto toolong;
    }
    else {
        n = snprintf(path, size, "%s/%s", cwd, file);

        if (n >= (int)size)
            goto toolong;
    }

    return path;

 toolong:
    errno = ENAMETOOLONG;
 failed:
    return NULL;
}


char *preproc_resolve_path(smpl_t *smpl, const char *file, char *path,
                           size_t size)
{
    smpl_parser_t *p  = smpl->parser;
    smpl_input_t  *in = p ? p->in : NULL;
    char           dir[PATH_MAX], *tail;
    int            n;

    if (*file == '/') {
        while (*file == '/')
            file++;

        n = snprintf(path, size, "/%s", file);

        if (n >= (int)size)
            goto toolong;

        return path;
    }

    if (in == NULL || in->path == NULL) {
        if (getcwd(dir, sizeof(dir)) == NULL)
            goto failed;

        n = strlen(dir);

        if (n >= (int)size)
            goto toolong;
    }
    else {
        n = snprintf(dir, sizeof(dir), "%s", in->path);

        if (n >= (int)size)
            goto toolong;

        if ((tail = strrchr(dir, '/')) != NULL) {
            *tail = '\0';
            n = tail - dir;
        }
    }

    if (snprintf(dir + n, size - n, "/%s", file) >= (int)(size - n))
        goto toolong;

    return absolute_path(dir, path, size);

 toolong:
    errno = ENAMETOOLONG;
 failed:
    return NULL;
}


int preproc_pending_file(smpl_t *smpl, const char *path)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in;
    smpl_list_t   *p, *n;
    struct stat    st;

    if (smpl_list_empty(&parser->inq) || parser->in == NULL)
        return 0;

    if (stat(path, &st) < 0)
        return 0;

    /* Notes: we check for <path> in the input queue up to the current input. */
    smpl_list_foreach(&parser->inq, p, n) {
        in = smpl_list_entry(p, typeof(*in), hook);

        if (in->dev == st.st_dev && in->ino == st.st_ino)
            return 1;

        if (in == parser->in)
            break;
    }

    return 0;
}


int preproc_file(smpl_t *smpl, const char *path)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in     = NULL;
    int            fd     = -1;
    struct stat    st;
    char          *buf, *p;
    int            line, n, l;

    if (stat(path, &st) < 0)
        goto ioerror;

    if (st.st_size > SMPL_TEMPLATE_MAX)
        goto overflow;

    fd = open(path, O_RDONLY);

    if (fd < 0)
        goto ioerror;

    buf = alloca(st.st_size + 1);
    p   = buf;
    l   = st.st_size;

    while (l > 0) {
        n = read(fd, p, l);

        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            else
                goto ioerror;
        }

        p += n;
        l -= n;
    }

    close(fd);
    *p = '\0';
    l  = (int)st.st_size;

    if (parser->mbeg == NULL) {
        n = parse_markers(smpl, buf, path);

        if (n < 0)
            goto preproc_failed;

        buf += n;
        l   -= n;
        line = 2;

        smpl_debug("directive markers: '%s' '%s'", parser->mbeg, parser->mend);
        smpl_debug("tabulation marker: '%s'", parser->mtab ? parser->mtab : "");
    }
    else
        line = 1;

    in = smpl_alloct(typeof(*in));

    if (in == NULL)
        goto nomem;

    in->buf = smpl_allocz(l + 1);

    if (in->buf == NULL)
        goto nomem;

    in->path = smpl_strdup(path);

    if (in->path == NULL)
        goto nomem;

    l = filter_tabulation(smpl, buf, in->buf);

    if (l < 0)
        goto preproc_failed;

    smpl_list_init(&in->hook);
    in->p    = in->buf;
    in->line = line;
    in->size = l;
    in->dev  = st.st_dev;
    in->ino  = st.st_ino;

    if (parser->in == NULL)
        smpl_list_append(&parser->inq, &in->hook);
    else
        smpl_list_insert_after(&parser->in->hook, &in->hook);

    parser->in = in;

    return 0;

 overflow:
    smpl_fail(-1, smpl, ENOBUFS, "template file '%s' too large", path);

 ioerror:
    if (fd >= 0)
        close(fd);
    smpl_fail(-1, smpl, errno, "failed to read file '%s'", path);

 preproc_failed:
    smpl_fail(-1, smpl, errno, "failed to preprocess '%s'", path);

 nomem:
    if (in) {
        smpl_free(in->buf);
        smpl_free(in->path);
        smpl_free(in);
    }
    return -1;
}


int preproc_push_file(smpl_t *smpl, const char *file)
{
    char path[PATH_MAX];

    if (preproc_resolve_path(smpl, file, path, sizeof(path)) == NULL)
        goto notfound;

    if (preproc_pending_file(smpl, path))
        goto busy;

    if (preproc_file(smpl, path) < 0)
        goto failed;

    return 0;

 notfound:
    smpl_fail(-1, smpl, errno, "include file '%s' not found", file);

 busy:
    smpl_fail(-1, smpl, errno, "circular inclusion of '%s'", file);

 failed:
    smpl_fail(-1, smpl, errno, "failed to include '%s'", path);

}


int preproc_pull(smpl_t *smpl)
{
    smpl_parser_t *parser = smpl->parser;
    smpl_input_t  *in     = parser ? parser->in : NULL;
    smpl_list_t   *prev;

    if (parser == NULL || in == NULL)
        return 0;

    if ((prev = in->hook.prev) == &parser->inq)
        return 0;

    parser->in = smpl_list_entry(prev, typeof(*in), hook);

    return 1;
}


void preproc_trim(smpl_t *smpl)
{
    smpl_list_t  *p, *n;
    smpl_input_t *in;

    smpl_list_foreach(&smpl->parser->inq, p, n) {
        in = smpl_list_entry(p, typeof(*in), hook);

        smpl_free(in->buf);
        in->buf  = NULL;
        in->p    = NULL;
        in->size = 0;

        /*
         * Notes:
         *   path can be used by the parsed templates. We only
         *   free it when we finally free the template.
         */
    }
}


void preproc_purge(smpl_t *smpl)
{
    smpl_list_t  *p, *n;
    smpl_input_t *in;

    smpl_list_foreach(&smpl->parser->inq, p, n) {
        in = smpl_list_entry(p, typeof(*in), hook);

        smpl_list_delete(&in->hook);
        smpl_free(in->buf);
        smpl_free(in->path);

        smpl_free(in);
    }
}


static int filter_tabulation(smpl_t *smpl, char *in, char *out)
{
    smpl_parser_t *parser = smpl->parser;
    char          *tab, *p, *q, c;
    int            len, match;

    if (parser->mtab == NULL) {
        len = strlen(in);
        strcpy(out, in);

        return len + 1;
    }

    tab = parser->mtab;
    len = parser->ltab;
    p   = in;
    q   = out;

    match = 0;
    while (*p) {
        if (!match) {
            if (*p == *tab && (p == in || p[-1] == '\n')) {
                match = 1;
                p++;
            }
            else
                *q++ = *p++;

            continue;
        }

        if (match == len) {
            smpl_debug("skipping marked tabulation '%*.*s'",
                       match, match, p - match);
            c = *p;

            if (c != '\n' && c != '\0') {
                while (*p == c)
                    p++;
            }
            else if (c == '\n')
                *q++ = *p++;
            else if (c == '\0')
                *q = '\0';

            match = 0;

            continue;
        }

        if (*p == tab[match]) {
            p++;
            match++;
        }
        else {
            while (match > 0) {
                *q++ = p[-match];
                match--;
            }
        }
    }

    *q = '\0';

#if 0
    smpl_debug("original: [%s]", in);
    smpl_debug(" preproc: [%s]", out);
#endif

    return q - out + 1;
}
