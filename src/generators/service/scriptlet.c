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
#include <sys/wait.h>

#include <iot/common/list.h>

#include "generator.h"

static int restart_child(child_t *c);

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

    iot_list_foreach(&g->scriptlets, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);

        if (s->len < len)
            continue;

        if (strncmp(script, s->name, s->len) == 0)
            return s;
    }

    return 0;
}


static int parse_settings(char *beg, char *end,
                          int (*cb)(char *name, char *value, void *user_data),
                          void *user_data)
{
    char *key, *value, *p, *q, buf[256];
    int   l;

    /* settings are of the format key[=value,[key[=value]]]...[:|\0] */

    if (beg == NULL)
        return 0;

    if (end == NULL)
        end = beg + strlen(beg);

    value = "";

    p = beg;
    while (p < end) {
        while (*p == ',' || *p == ' ')
            p++;

        if (p >= end || *p == ':')
            break;

        q = buf;
        l = sizeof(buf);
        *q = '\0';

        /* parse and copy key */
        key = q;
        while (p < end && l > 0 && *p != '=' && *p != ',' && *p != ':') {
            *q++ = *p++;
            l--;
        }

        if (!l)
            goto nobuf;

        if (!*key)
            goto invalid_settings;

        /* if there is a value, parse and copy it */
        if (*p == '=') {
            p++;
            *q++ = '\0';

            value = q;
            while (p < end && l > 0 && *p != ',' && *p != ':') {
                *q++ = *p++;
                l--;
            }

            if (!l)
                goto nobuf;

            *q = '\0';
        }
        else
            value = NULL;

        iot_debug("parsing setting '%s%s%s'.", key, value ? "=" : "",
                  value ? value : "");

        if (cb(key, value, user_data) < 0)
            goto invalid_settings;

    }

    return 0;

 nobuf:
    log_error("Not enough buffer space to parse scriptlet command settings.");
    return -1;

 invalid_settings:
    log_error("Invalid settings '%s%s%s.", key, value ? "=" : "",
              value ? value : "");
    return -1;
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


static int exec_settings(char *key, char *val, void *user_data)
{
    int *flagsp = (int *)user_data;

    if (!strcmp(key, "restart")) {
        if (!val)
            *flagsp |= CHILD_RESTART_ALWAYS;
        else if (!strcmp(val, "true") || !strcmp(val, "yes"))
            *flagsp |= CHILD_RESTART_ALWAYS;
        else if (!strcmp(val, "onfail") || !strcmp(val, "onerror"))
            *flagsp |= CHILD_RESTART_ONFAIL;
        else
            goto invalid_restart;
    }
    else if (!strcmp(key, "fail")) {
        if (!val)
            goto invalid_fail;
        if (!strcmp(val, "ignore") || !strcmp(val, "no"))
            *flagsp |= CHILD_FAILURE_IGNORE;
        else
            goto invalid_fail;
    }
    else
        goto invalid_key;

    return 0;

 invalid_restart:
    log_error("Invalid exec restart setting '%s'.", val ? val : "");
    return -1;

 invalid_fail:
    log_error("Invalid exec fail setting '%s'.", val ? val : "");
    return -1;

 invalid_key:
    log_error("Unknown exec setting '%s'.", key);
    return -1;
}


static int exec_handler(generator_t *g, const char *cmd, int len,
                        char *set_beg, char *set_end, void *user_data)
{
    char    *argv[64], buf[1024];
    int      argc, flags, i, l;
    child_t *c;

    IOT_UNUSED(user_data);

    if (g->dry_run)
        printf("  should execute '%*.*s'...\n", len, len, cmd);

    argc = IOT_ARRAY_SIZE(argv);
    argc = parse_cmdline(cmd, len, buf, sizeof(buf), argv, argc - 1);

    if (argc < 0)
        return -1;

    flags = 0;
    if (parse_settings(set_beg, set_end, exec_settings, &flags) < 0)
        goto invalid_settings;

    if (g->dry_run) {
        for (i = 0; argv[i] != NULL; i++)
            printf("    argv[%d] = '%s'\n", i, argv[i]);
        printf("    exec flags: 0x%x\n", flags);
    }
    else {
        c = iot_allocz(sizeof(*c));

        if (c == NULL)
            goto nomem;

        iot_list_init(&c->hook);
        c->argv = iot_allocz_array(char *, argc + 1);

        if (c->argv == NULL)
            goto nomem;

        for (i = 0; i < argc; i++)
            if ((c->argv[i] = iot_strdup(argv[i])) == NULL)
                goto nomem;

        c->argv[i] = NULL;
        c->flags   = flags;

        iot_list_append(&g->children, &c->hook);

        if (restart_child(c) < 0)
            goto exec_failed;
    }

    return 0;

 exec_failed:
    log_error("Failed to exec '%*.*s' (%d: %s).", len, len, cmd,
              errno, strerror(errno));
    exit(-1);

 invalid_settings:
    l = set_end - set_beg;
    log_error("Invalid exec settings '%*.*s'.", l, l, set_beg);
    return -1;

 nomem:
    exit(-1);
}


static int setuser_handler(generator_t *g, const char *cmd, int len,
                           char *set_beg, char *set_end, void *user_data)
{
    struct passwd pwd, *pwe;
    char          buf[1024], usr[64];

    IOT_UNUSED(set_end);
    IOT_UNUSED(user_data);

    if (set_beg != NULL)
        goto unknown_settings;

    if (g->dry_run)
        printf("  should set user to '%*.*s'...\n", len, len, cmd);
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

 unknown_settings:
    log_error("setuser scriptlet command does not take settings.");
    return -1;

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
                             char *set_beg, char *set_end, void *user_data)
{
    struct group  group, *ge;
    const char   *b, *e, *n, *end;
    char          grp[64], buf[1024];
    gid_t         gids[64];
    int           ngid, l;

    IOT_UNUSED(set_end);
    IOT_UNUSED(user_data);

    if (set_beg != NULL)
        goto unknown_settings;

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
                printf("  should set %sgroup '%s' (%d).\n",
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

 unknown_settings:
    log_error("setgroups scriptlet command does not take settings.");
    return -1;

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


static int service_handler(generator_t *g, const char *cmd, int len,
                           char *set_beg, char *set_end, void *user_data)
{
    char    *argv[64], buf[1024];
    int      argc, i;
    pid_t    pid;

    IOT_UNUSED(set_end);
    IOT_UNUSED(user_data);

    if (set_beg != NULL)
        goto unknown_settings;

    if (g->dry_run)
        printf("  service command '%*.*s'...\n", len, len, cmd);

    argc = IOT_ARRAY_SIZE(argv);
    argc = parse_cmdline(cmd, len, buf, sizeof(buf), argv + 1, argc - 2);

    if (argc < 0)
        return -1;

    argv[0] = "/bin/systemctl";

    if (g->dry_run) {
        for (i = 0; argv[i] != NULL; i++)
            printf("    service arg[%d] = '%s'\n", i, argv[i]);
    }
    else {
        pid = fork();

        if (pid < 0)
            goto fork_failed;

        if (pid == 0) {
            dup2(0, open("/dev/null", O_RDONLY));
            if (execv(argv[0], argv) < 0)
                goto exec_failed;
        }
    }

    return 0;

 unknown_settings:
    log_error("service scriptlet command does not take settings.");
    return -1;

 fork_failed:
    log_error("Failed to fork child for '%s'.", argv[0]);
    return -1;

 exec_failed:
    log_error("Failed to exec '%s' (%d: %s).", argv[0], errno, strerror(errno));
    return -1;
}


static int exit_handler(generator_t *g, const char *cmd, int len,
                           char *set_beg, char *set_end, void *user_data)
{
    int status;

    IOT_UNUSED(set_end);
    IOT_UNUSED(user_data);

    if (set_beg != NULL)
        goto unknown_settings;

    if (g->dry_run)
        printf("  should exit with status '%*.*s'...\n", len, len, cmd);
    else {
        while ((*cmd == ' ' || *cmd == '\t') && len > 0)
            cmd++;

        status = strtol(cmd, NULL, 10);

        exit(status);
    }

    return 0;

 unknown_settings:
    log_error("scriptlet exit command does not take settings.");
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
            .name      = "service",
            .handler   = service_handler,
            .user_data = NULL,
        },
        {
            .name      = "exit",
            .handler   = exit_handler,
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
    char        *p, *c, *o, *b, *e;
    int          l;

    register_builtin(g);

    if (!iot_list_empty(&scriptlets))
        iot_list_join(&g->scriptlets, &scriptlets);

    p = (char *)skip_whitespace(scriptlet);

    while (*p && *(b = (char *)skip_whitespace(p))) {
        c = strchr(b, ':');
        o = strchr(b, ',');

        if (o > c)
            o = NULL;

        if (o)
            l = o - b;
        else
            l = c ? c - b : (int)strlen(b);

        s = scriptlet_find(g, b, l);

        if (s == NULL)
            goto unknown_command;

        if (c)
            c++;

        e = c ? strchr(c, '\n') : NULL;
        l = e ? e - c : (int)strlen(c);

        if (s->handler(g, c, l, o, c, s->user_data) < 0)
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


static int pending = 0;


static void scriptlet_signalled(int signum)
{
    log_info("Received signal %d (%s)...", signum, strsignal(signum));

    switch (signum) {
    case SIGCHLD:
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
        pending |= (1 << signum);
        break;
    default:
        break;
    }
}


static child_t *find_child(generator_t *g, pid_t pid)
{
    child_t         *c;
    iot_list_hook_t *p, *n;

    iot_list_foreach(&g->children, p, n) {
        c = iot_list_entry(p, typeof(*c), hook);

        if (c->pid == pid)
            return c;
    }

    return NULL;
}


static void free_child(child_t *c)
{
    int i;

    iot_list_delete(&c->hook);

    for (i = 0; c->argv[i] != NULL; i++)
        iot_free(c->argv[i]);
    iot_free(c->argv);

    iot_free(c);
}


static int restart_child(child_t *c)
{
    if (c->restarts++ > 10)
        goto too_many;

    c->pid = fork();

    if (c->pid < 0)
        goto fork_failed;

    if (c->pid == 0) {
        dup2(0, open("/dev/null", O_RDONLY));
        if (execv(c->argv[0], c->argv) < 0)
            goto exec_failed;
    }

    return 0;

 fork_failed:
    log_error("Failed to fork child for starting '%s'...", c->argv[0]);
    return -1;

 exec_failed:
    log_error("Failed to exec '%s' (%d: %s).", c->argv[0],
              errno, strerror(errno));
    return -1;

 too_many:
    log_error("Child '%s' has been restarted too may times...", c->argv[0]);
    return 1;
}


static int needs_restart(child_t *c, int status)
{
    if (!c->flags)
        return 0;

    if (c->flags & CHILD_RESTART_ALWAYS)
        return 1;

    if (c->flags & CHILD_RESTART_ONFAIL) {
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            return 1;

        return 0;
    }

    if (WIFSIGNALED(status)) {
        switch (WTERMSIG(status)) {
        case SIGHUP:  case SIGINT:
        case SIGTERM: case SIGKILL:
            return 0;
        default:
            return 1;
        }
    }

    return 0;
}


static int check_child(generator_t *g)
{
    child_t *c;
    pid_t    pid;
    int      status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        c = find_child(g, pid);

        if (c == NULL)
            continue;

        if (needs_restart(c, status)) {
            if (restart_child(c) < 0)
                goto restart_failed;
        }
        else
            free_child(c);
    }

    return 0;

 restart_failed:
    return -1;
}


static int signal_children(generator_t *g, int sig)
{
    iot_list_hook_t *p, *n;
    child_t         *c;

    iot_list_foreach(&g->children, p, n) {
        c = iot_list_entry(p, typeof(*c), hook);

        kill(c->pid, sig);
    }

    return 0;
}


int scriptlet_wait(generator_t *g)
{
    int s, m;
    sigset_t pass;

    sigemptyset(&pass);
    sigaddset(&pass, SIGCHLD);
    sigaddset(&pass, SIGHUP);
    sigaddset(&pass, SIGINT);
    sigaddset(&pass, SIGTERM);

    sigprocmask(SIG_UNBLOCK, &pass, NULL);

    signal(SIGCHLD, scriptlet_signalled);
    signal(SIGHUP , scriptlet_signalled);
    signal(SIGINT , scriptlet_signalled);
    signal(SIGTERM, scriptlet_signalled);

    while (1) {
        sleep(300);

        if (pending & (m = (1 << SIGCHLD))) {
            if (check_child(g) < 0)
                goto child_failed;
            pending &= ~m;
        }

        if (pending & (m = (1 << (s=SIGHUP)))) {
            signal_children(g, s);
            pending &= ~m;
        }

        if (pending & (m = (1 << (s=SIGINT)))) {
            signal_children(g, s);
            pending &= ~m;
            break;
        }

        if (pending & (m = (1 << (s=SIGTERM)))) {
            signal_children(g, s);
            break;
        }
    }

    return 0;

 child_failed:
    return -1;
}
