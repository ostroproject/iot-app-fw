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


int self_check_dir(generator_t *g)
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


int self_execute(service_t *s)
{
    char *script = smpl_steal_result_output(&s->result);

    if (scriptlet_run(s->g, script) < 0)
        return -1;

    for(;;)
        sleep(300);
}


static int self_prepare(generator_t *g, service_t *s)
{
    iot_json_t  *app;
    char         path[PATH_MAX];
    int          n;
    n = snprintf(path, sizeof(path), "%s/%s", g->path_self, g->name_manifest);

    if (n < 0 || n >= (int)sizeof(path))
        goto invalid_path;

    iot_clear(s);
    iot_list_init(&s->hook);
    smpl_init_result(&s->result, NULL);

    s->g = g;
    s->m = manifest_read(g, path);

    if (s->m == NULL)
        goto load_failed;

    app = iot_json_get(s->m, "application");

    if (app == NULL || iot_json_get_type(app) != IOT_JSON_OBJECT)
        goto malformed_manifest;

    s->provider = (char *)iot_json_string_value(iot_json_get(app, "origin"));
    s->app      = (char *)iot_json_string_value(iot_json_get(app, "name"));
    s->src      = path;
    s->appdir   = (char *)g->path_self;

    if (s->provider == NULL || s->app == NULL)
        goto malformed_manifest;

    if (service_prepare_data(s) < 0)
        goto malformed_manifest;

    return 0;

 invalid_path:
    log_error("Invalid manifest path %s/%s.", g->path_self, g->name_manifest);
    return -1;

 load_failed:
    log_error("Failed to load manifest '%s'.", path);
    return -1;

 malformed_manifest:
    log_error("Malformed manifest/missing data.");
    iot_json_unref(s->m);
    return -1;
}


static int self_evaluate(service_t *s)
{
    return template_eval(s);
}

static void self_cleanup(service_t *s)
{
    iot_json_unref(s->data);
    s->data = NULL;
}


int self_generate(generator_t *g)
{
    service_t s;

    if (self_prepare(g, &s) < 0)
        goto prepare_failed;

    if (self_evaluate(&s) < 0)
        goto evaluate_failed;

    if (self_execute(&s) < 0)
        goto execute_failed;

    self_cleanup(&s);

    return 0;

 prepare_failed:
    log_error("Failed to prepare services for '%s'.", g->path_self);
    self_cleanup(&s);
    return -1;

 evaluate_failed:
    log_error("Failed to evaluate template for '%s'.", g->path_self);
    self_cleanup(&s);
    return -1;

 execute_failed:
    log_error("Failed to execute services for '%s'.", g->path_self);
    self_cleanup(&s);
    return -1;
}
