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
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/list.h>

#include "generator.h"

static IOT_LIST_HOOK(scriptlets);


int scriptlet_register(generator_t *g, scriptlet_t *s)
{
    iot_list_init(&s->hook);

    s->len = strlen(s->name);

    if (g != NULL) {
        iot_list_join(&g->scriptlets, &scriptlets);
        iot_list_append(&g->scriptlets, &s->hook);
    }
    else
        iot_list_append(&scriptlets, &s->hook);

    return 0;
}


scriptlet_t *scriptlet_find(generator_t *g, char *script, int len)
{
    scriptlet_t     *s;
    iot_list_hook_t *p, *n;
    char             c;

    iot_list_foreach(&g->scriptlets, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        if (s->len < len)
            continue;

        if (strncmp(script, s->name, s->len) == 0 &&
            ((c = script[s->len]) == '\0' || c == ':'))
            return s;
    }

    return 0;
}


static inline const char *skip_newlines(const char *s)
{
    while (*s == '\n')
        s++;

    return s;
}


static const char *skip_whitespace(const char *s)
{
    s = (char *)skip_newlines(s);

    while (*s == ' ' || *s == '\t')
        s++;

    return s;
}


static char *copy_arg(char **sp, char **dp, int *size)
{
    char *s, *d;
    char  quote;

    s     = *sp;
    d     = *dp;
    quote = 0;

    while (*s && (quote || (*s != ' ' && *s != '\t')) &&
           *s != '\n' && *size > 0) {
        switch (*s) {
        case '\'':
        case '"':
            if (quote == *s) {
                quote = 0;
                s++;
            }
            else
                quote = *s++;
            break;

        case '\\':
            s++;
            if (!*s)
                goto invalid_arg;
            *d++ = *s++;
            *size -= 1;
            break;

        default:
            *d++ = *s++;
            *size -= 1;
            break;
        }
    }

    if (!(*size))
        goto overflow;

    *d++  = '\0';
    *size -= 1;
    *sp = s;
    *dp = d;

    return s;

 invalid_arg:
 overflow:
    return NULL;
}


static int parse_cmdline(const char *cmd, int len, char *buf, size_t size,
                         char **argv, int argc)
{
    const char *end = cmd + len;
    char       *p, *b, *a;
    int         narg, l;

    narg  = 0;
    p = b = (char *)skip_whitespace(cmd);
    a     = buf;
    l     = (int)size;

    argv[narg] = a;

    while (p < end && narg < argc) {
        if ((b = (char *)skip_whitespace(b)) >= end)
            break;

        if (copy_arg(&b, &a, &l) == NULL || b > end)
            goto invalid_cmdline;

        argv[++narg] = a;
    }

    if (narg >= argc - 1)
        goto overflow;

    argv[narg] = NULL;
    return narg;

 invalid_cmdline:
    log_error("Failed to parse command '%*.*s'.", len, len, cmd);
    return -1;

 overflow:
    log_error("Too many arguments in command '%*.*s'.", len, len, cmd);
    return -1;
}


static int exec_handler(generator_t *g, const char *cmd, int len,
                        void *user_data)
{
    char  *argv[64], buf[1024];
    int    argc;
    pid_t  pid;

    IOT_UNUSED(user_data);

    if (g->dry_run)
        printf("    should execute '%*.*s'...\n", len, len, cmd);

    argc = IOT_ARRAY_SIZE(argv);
    argc = parse_cmdline(cmd, len, buf, sizeof(buf), argv, argc - 1);

    if (argc < 0)
        return -1;

    if (g->dry_run) {
        int i;

        for (i = 0; argv[i] != NULL; i++)
            printf("    argv[%d] = '%s'\n", i, argv[i]);
    }
    else {
        pid = fork();

        if (pid < 0)
            goto fork_failed;

        if (pid == 0) {
            close(0);
            dup2(0, open("/dev/null", O_RDONLY));
            if (execv(argv[0], argv) < 0)
                goto exec_failed;
        }
    }

    return 0;

 fork_failed:
    log_error("Failed to fork child for starting '%*.*s'...", len, len, cmd);
    return -1;

 exec_failed:
    log_error("Failed to exec '%*.*s' (%d: %s).", len, len, cmd,
              errno, strerror(errno));
    exit(-1);
}


static int setuser_handler(generator_t *g, const char *cmd, int len,
                           void *user_data)
{
    struct passwd pwd, *pwe;
    char          buf[1024], usr[64];

    IOT_UNUSED(user_data);

    if (g->dry_run)
        printf("    should set user to '%*.*s'...\n", len, len, cmd);
    else {
        while ((*cmd == ' ' || *cmd == '\t') && len > 0)
            cmd++;

        if (!len)
            goto no_user;

        if (len > (int)sizeof(usr) - 1)
            goto overflow;

        strncpy(usr, cmd, len);
        usr[len] = '\0';

        if (getpwnam_r(usr, &pwd, buf, sizeof(buf), &pwe) < 0 || pwe == NULL)
            goto pwnam_failed;

        if (setreuid(pwe->pw_uid, pwe->pw_uid) < 0)
            goto setuid_failed;
    }

    return 0;

 overflow:
    log_error("User name '%*.*s' too long.", len, len, cmd);
    return -1;

 no_user:
    log_error("No user given for 'setuser' command.");
    return -1;

 pwnam_failed:
    log_error("Failed to find uid for user '%s'.", usr);
    return -1;

 setuid_failed:
    log_error("Failed to change user identity to '%s/%d (%d: %s).",
              usr, pwe->pw_uid, errno, strerror(errno));
    return -1;
}


static int setgroups_handler(generator_t *g, const char *cmd, int len,
                             void *user_data)
{
    struct group  group, *ge;
    const char   *b, *e, *n, *end;
    char          grp[64], buf[1024];
    gid_t         gids[64];
    int           ngid, l;

    IOT_UNUSED(user_data);

    ngid  = 0;
    end   = cmd + len;
    b     = skip_whitespace(cmd);

    while ((b = skip_whitespace(b)) < end && ngid < (int)IOT_ARRAY_SIZE(gids)) {
        b = skip_whitespace(b);

        if (b >= end)
            break;

        n = strchr(b, ',');

        if (n > end || n == NULL)
            n = end;

        e = n;
        while (e > b && (e[-1] == ' ' || e[-1] == '\t'))
            e--;

        l = e - b;

        if (l > (int)sizeof(grp) - 1)
            goto toolong;

        strncpy(grp, b, l);
        grp[l] = '\0';

        if (getgrnam_r(grp, &group, buf, sizeof(buf), &ge) < 0 || ge == NULL) {
            if (!g->dry_run)
                goto gwnam_failed;
        }
        else {
            gids[ngid++] = ge->gr_gid;

            if (g->dry_run)
                printf("    should set %sgroup '%s' (%d).\n",
                       ngid > 1 ? "supplementary " : "", grp, ge->gr_gid);
        }

        b = n + 1;
    }

    if (ngid == 0)
        goto no_groups;

    if (ngid >= (int)IOT_ARRAY_SIZE(gids))
        goto overflow;

    if (!g->dry_run)
        if (setgid(gids[0]) < 0 || setgroups(ngid, gids) < 0)
            goto setgrp_failed;

    return 0;

 overflow:
    log_error("Too many groups in '%*.*s'.", len, len, cmd);
    return -1;

 toolong:
    log_error("Group name '%*.*s' too long.", l, l, b);
    return -1;

 gwnam_failed:
    log_error("Failed to find gid for group '%s'.", grp);
    return -1;

 no_groups:
    log_error("No group names given '%*.*s'.", len, len, cmd);
    return -1;

 setgrp_failed:
    log_error("Failed to set supplementary groups (%d: %s).",
              errno, strerror(errno));
    return -1;
}


static void register_builtin(generator_t *g)
{
    static scriptlet_t builtin[] = {
        {
            .name      = "exec",
            .handler   = exec_handler,
            .user_data = NULL,
        },
        {
            .name      = "setuser",
            .handler   = setuser_handler,
            .user_data = NULL,
        },
        {
            .name      = "setgroups",
            .handler   = setgroups_handler,
            .user_data = NULL,
        },
        {
            .name      = NULL
        }
    }, *s;
    int done = 0;

    if (done)
        return;

    for (s = builtin; s->name; s++)
        scriptlet_register(g, s);

    done = 1;
}


int scriptlet_run(generator_t *g, char *scriptlet)
{
    scriptlet_t *s;
    char        *p, *c, *b, *e;
    int          l;

    register_builtin(g);

    if (!iot_list_empty(&scriptlets))
        iot_list_join(&g->scriptlets, &scriptlets);

    p = (char *)skip_whitespace(scriptlet);

    while (*p && *(b = (char *)skip_whitespace(p))) {
        c = strchr(b, ':');
        l = c ? c - b : (int)strlen(b);

        s = scriptlet_find(g, b, l);

        if (s == NULL)
            goto unknown_command;

        if (c)
            c++;

        e = c ? strchr(c, '\n') : NULL;
        l = e ? e - c : (int)strlen(c);

        if (s->handler(g, c, l, s->user_data) < 0)
            goto command_failed;

        p = e ? e + 1 : e + l;
    }

    return 0;

 unknown_command:
    log_error("Unknown scriptlet command '%*.*s'.", l, l, b);
    return -1;

 command_failed:
    log_error("Execution of scriptlet '%*.*s' failed.", l, l, c);
    return -1;
}


