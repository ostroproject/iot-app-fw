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

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/file-utils.h>
#include <iot/common/json.h>

#include "generator.h"


int self_check(generator_t *g)
{
    char path[PATH_MAX];
    int  n;

    n = snprintf(path, sizeof(path), "%s/%s", g->path_self, g->name_manifest);

    if (n < 0 || n >= (int)sizeof(path))
        return -1;

    if (access(path, R_OK) == 0)
        return 1;
    else
        return 0;
}


static int parse_command(const char *cmd, int len, char *buf, size_t size,
                         char **args, int narg)
{
    const char *p;
    char       *q, *b, delim;
    int         l, n, a;

    p = cmd;

    while (*p == ' ' || *p == '\t')
        p++;

    q = buf;
    n = 0;
    l = 0;

    b = q;
    a = 0;
    delim = 0;
    while (l < len && n < (int)size) {
        if (a >= narg)
            goto args_overflow;
        switch (*p) {
        case '\\':
            p++;
            if (*p == '\'' || *p == '"' || *p == ' ') {
                *q++ = *p++;
                l += 2;
                n++;
            }
            else
                goto invalid_escape;
            break;

        case ' ':
            p++;
            l++;
            *q++ = delim ? ' ' : '\0';
            n++;
            if (!delim) {
                args[a++] = b;
                b = q;
            }
            break;

        case '\'':
        case '"':
            if (!delim)
                delim = *p;
            else
                if (delim == *p)
                    delim = 0;
            p++;
            l++;
            break;

        default:
            *q++ = *p++;
            l++;
            n++;
            break;
        }
    }

    *q = '\0';
    if (*b)
        args[a++] = b;

    if (a >= narg)
        goto args_overflow;

    args[a] = NULL;

    return a;

 invalid_escape:
    log_error("Invalid escape sequence in command '%*.*s'", len, len, cmd);
    return -1;

 args_overflow:
    log_error("Too many arguments in command '%*.*s'.", len, len, cmd);
    return -1;
}


static int exec_command(generator_t *g, const char *cmd, int len)
{
    char  *argv[64], buf[1024];
    int    argc, i;
    pid_t  pid;

    IOT_UNUSED(g);

    argc = parse_command(cmd, len, buf, sizeof(buf),
                         argv + 1, IOT_ARRAY_SIZE(argv) - 1);

    if (argc < 0)
        goto parse_failed;

    argv[0] = argv[1];
    argc++;

    if (g->dry_run) {
        printf("should execute command: '%s'\n", cmd);
        for (i = 0; i < argc; i++) {
            printf("  #%d arg: '%s'\n", i, argv[i]);
        }
    }
    else {
        pid = fork();

        if (pid < 0)
            goto fork_failed;

        if (pid == 0) {
            close(0);
            dup2(0, open("/dev/null", O_RDONLY));
            exit(execv(argv[0], argv));
        }
    }

    return 0;

 parse_failed:
    log_error("Failed to parse command '%s'.", cmd);
    return -1;
 fork_failed:
    log_error("Failed to fork for executing command '%s'.", argv[0]);
    return -1;
}


static int set_user(generator_t *g, const char *id, int len)
{
    IOT_UNUSED(g);

    printf("should change user ID to '%*.*s'.\n", len, len, id);

    return 0;
}


static int set_groups(generator_t *g, const char *id, int len)
{
    IOT_UNUSED(g);

    printf("should change supplementary groups to '%*.*s'.\n", len, len, id);

    return 0;
}


int self_execute(service_t *s)
{
    generator_t *g = s->g;
    char        *script, *p, *b, *e;
    int          l;

    p = script = smpl_steal_result_output(&s->result);

    if (script == NULL)
        goto no_output;

    if (g->dry_run)
        printf("template generated result:\n%s\n", script);

    while (p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;

        if (!*p)
            break;

        b = strchr(p, ':');

        if (b == NULL)
            goto invalid_script;

        b++;
        e = strchr(b, '\n');

        if (e == NULL)
            l = (int)strlen(b);
        else
            l = e - b;

        if (!strncmp(p, "exec:", 5)) {
            if (exec_command(g, b, l) < 0)
                goto exec_failed;
        }
        else if (!strncmp(p, "setuser:", 8)) {
            if (set_user(g, b, l) < 0)
                goto setuser_failed;
        }
        else if (!strncmp(p, "setgroups:", 10)) {
            if (set_groups(g, b, l) < 0)
                goto setgroups_failed;
        }
        else {
            l = b - p - 1;
            log_error("Unknown directive '%*.*s'.", l, l, p);
        }

        p = e ? e + 1 : NULL;
    }

    return 0;

 no_output:
    log_error("Template '%s' produced no output.", g->name_template);
    return -1;

 exec_failed:
    return -1;

 setuser_failed:
    log_error("Failed to change user ID to '%*.*s'.", l, l, b);
    return -1;

  setgroups_failed:
    log_error("Failed to change group ID to '%*.*s'.", l, l, b);
    return -1;

invalid_script:
    log_error("Unknown directive '%s'.", p);
    return -1;
}


int self_generate(generator_t *g)
{
    service_t   s;
    iot_json_t *app;
    char        path[PATH_MAX];
    int         n, r;

    n = snprintf(path, sizeof(path), "%s/%s", g->path_self, g->name_manifest);

    if (n < 0 || n >= (int)sizeof(path))
        goto invalid_path;

    iot_clear(&s);
    iot_list_init(&s.hook);
    smpl_init_result(&s.result, NULL);

    s.g = g;
    s.m = manifest_read(g, path);

    if (s.m == NULL)
        goto load_failed;

    app = iot_json_get(s.m, "application");

    if (app == NULL || iot_json_get_type(app) != IOT_JSON_OBJECT)
        goto malformed_manifest;

    s.provider = (char *)iot_json_string_value(iot_json_get(app, "origin"));
    s.app      = (char *)iot_json_string_value(iot_json_get(app, "name"));
    s.src      = path;
    s.appdir   = (char *)g->path_self;

    if (s.provider == NULL || s.app == NULL)
        goto malformed_manifest;

    if (service_prepare_data(&s) < 0)
        goto malformed_manifest;

    if (template_eval(&s) < 0)
        goto template_failed;

    r = self_execute(&s);

    iot_json_unref(s.data);
    return r;

 invalid_path:
    log_error("Invalid manifest path %s/%s.", g->path_self, g->name_manifest);
    return -1;

 load_failed:
    log_error("Failed to load manifest '%s'.", path);
    return -1;

 malformed_manifest:
    log_error("Malformed manifest/missing data.");
    iot_json_unref(s.m);
    return -1;

 template_failed:
    log_error("Template evaluation failed.");
    iot_json_unref(s.data);
    return -1;
}
