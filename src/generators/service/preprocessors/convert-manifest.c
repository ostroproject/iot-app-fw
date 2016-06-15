/*
 * Copyright (c) 2015-2016, Intel Corporation
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

#include <errno.h>
#include <string.h>
#include <grp.h>

#include <iot/common/debug.h>
#include <iot/generators/service/generator.h>


static iot_json_t *convert_manifest(generator_t *g, iot_json_t *orig, void *data)
{
    iot_json_t *m, *a, *s, *c, *o, *start, *nw, *pmap, *pm, *ports, *po;
    const char *str, *proto, *t;
    char        cmd[4096], *p;
    int         as, n, l, i, port, map;

    IOT_UNUSED(g);
    IOT_UNUSED(data);

    if (iot_json_get(orig, "service") != NULL)
        return orig;

    m = iot_json_create(IOT_JSON_OBJECT);
    a = iot_json_create(IOT_JSON_OBJECT);
    s = iot_json_create(IOT_JSON_OBJECT);
    c = iot_json_create(IOT_JSON_OBJECT);
    start = iot_json_create(IOT_JSON_ARRAY);
    nw = NULL;

    if (m == NULL || a == NULL || s == NULL || c == NULL || start == NULL)
        goto nomem;

    /* manifest.application -> manifest.application.origin */
    if (!iot_json_get_string(orig, "provider", &str))
        goto invalid_manifest;
    if (!iot_json_add_string(a, "origin", str))
        goto nomem;

    /* manifest.application -> manifest.application.name */
    if (!iot_json_get_string(orig, "application", &str))
        goto invalid_manifest;
    if (!iot_json_add_string(a, "name", str))
        goto nomem;

    /* manifest.description -> manifest.application.description */
    if (!iot_json_get_string(orig, "description", &str))
        goto invalid_manifest;
    if (!iot_json_add_string(a, "description", str))
        goto nomem;

    /* manifest.groups -> manifest.service.groups */
    if ((o = iot_json_get(orig, "groups")) != NULL) {
        switch (iot_json_get_type(o)) {
        case IOT_JSON_STRING:
            if (!iot_json_add_string(s, "groups", iot_json_string_value(o)))
                goto nomem;
            break;
        case IOT_JSON_ARRAY:
            iot_json_ref(o);
            iot_json_del_member(orig, "groups");
            iot_json_add(s, "groups", o);
            break;
        default:
            goto invalid_manifest;
        }
    }

    /* manifest.environment -> manifest.service.environment */
    if ((o = iot_json_get(orig, "environment")) != NULL) {
        iot_json_ref(o);
        iot_json_del_member(orig, "environment");
        iot_json_add(s, "environment", o);
    }

    /* manifest.command -> manifest.service.start */
    if ((o = iot_json_get(orig, "command")) == NULL)
        goto invalid_manifest;

    switch (iot_json_get_type(o)) {
    case IOT_JSON_STRING:
        if (!iot_json_array_append_string(start, iot_json_string_value(o)))
            goto nomem;
        break;

    case IOT_JSON_ARRAY:
        p  = cmd;
        l  = sizeof(cmd);
        *p = '\0';
        for (i = 0, t = ""; i < iot_json_array_length(o); i++, t = " ") {
            if (!iot_json_array_get_string(o, i, &str))
                goto invalid_manifest;

            if (strchr(str, ' ') || strchr(str, '\t'))
                n = snprintf(p, l, "%s\"%s\"", t, str);
            else
                n = snprintf(p, l, "%s%s", t, str);

            if (n >= l)
                goto overflow;

            p += n;
            l -= n;
        }
        if (!iot_json_array_append_string(start, cmd))
            goto nomem;
        break;

    default:
        goto invalid_manifest;
    }

    /* manifest.autostart -> manifest.service.autostart */
    if ((o = iot_json_get(orig, "autostart")) != NULL) {
        as = 0;
        switch (iot_json_get_type(o)) {
        case IOT_JSON_STRING:
            if (!strcmp((str = iot_json_string_value(o)), "yes") ||
                !strcmp( str                            , "true"))
                as = 1;
            break;
        case IOT_JSON_BOOLEAN:
            as = iot_json_boolean_value(o);
            break;
        case IOT_JSON_INTEGER:
            as = !!iot_json_integer_value(o);
            break;
        default:
            break;
        }

        if (!iot_json_add_boolean(s, "autostart", as))
            goto nomem;
    }

    /* manifest.container -> manifest.container */
    if ((o = iot_json_get(orig, "container")) != NULL) {
        if (!iot_json_get_string(o, "type", &str))
            goto invalid_manifest;

        if (!strcmp(str, "none")) {
            if (!iot_json_add_string(c, "type", "none"))
                goto nomem;
        }
        else if (!strcmp(str, "nspawn-shared")) {
            if (!iot_json_add_string(c, "type", "nspawn-shared"))
                goto nomem;
        }
        else if (!strcmp(str, "nspawn")) {
            if (!iot_json_add_string(c, "type", "nspawn-app"))
                goto nomem;

        }
        else if (!strcmp(str, "nspawn-system")) {
            if (!iot_json_add_string(c, "type", "nspawn-system"))
                goto nomem;
        }
        else
            goto invalid_manifest;

        if (iot_json_get_string(o, "network", &str)) {
            if (strcasecmp(str, "VirtualEthernet"))
                goto invalid_manifest;

            if ((nw = iot_json_create(IOT_JSON_OBJECT)) == NULL)
                goto nomem;

            if (!iot_json_add_string(nw, "type", "VirtualEthernet"))
                goto nomem;

            if ((pmap = iot_json_get(o, "portmap")) != NULL) {
                if ((ports = iot_json_add_member(nw, "ports",
                                                 IOT_JSON_ARRAY)) == NULL)
                    goto nomem;

                switch (iot_json_get_type(pmap)) {
                case IOT_JSON_INTEGER:
                    port = iot_json_integer_value(pmap);

                    if ((po = iot_json_create(IOT_JSON_OBJECT)) == NULL)
                        goto nomem;
                    if (!iot_json_add_string(po, "proto", "tcp"))
                        goto nomem;
                    if (!iot_json_add_integer(po, "port", port))
                        goto nomem;
                    if (!iot_json_add_integer(po, "map", port))
                        goto nomem;
                    if (!iot_json_array_append(ports, po))
                        goto nomem;
                    break;

                case IOT_JSON_ARRAY:
                    for (i = 0; i < iot_json_array_length(pmap); i++) {
                        if ((po = iot_json_create(IOT_JSON_OBJECT)) == NULL)
                            goto nomem;

                        if (!iot_json_array_get_object(pmap, i, &pm))
                            goto invalid_manifest;

                        port = map = 0;

                        if (!iot_json_get_string(pm, "proto", &proto))
                            goto invalid_manifest;

                        iot_json_get_integer(pm, "host", &map);
                        iot_json_get_integer(pm, "container", &port);

                        if (!iot_json_add_string(po, "proto", proto))
                            goto nomem;

                        if (port != 0)
                            if (!iot_json_add_integer(po, "port", port))
                                goto nomem;

                        if (map != 0)
                            if (!iot_json_add_integer(po, "map", map))
                                goto nomem;

                        if (!iot_json_array_append(ports, po))
                            goto nomem;
                    }
                    break;

                default:
                    goto invalid_manifest;
                }

            }

            iot_json_add(c, "network", nw);
        }
    }

    iot_json_add_object(s, "start"      , start);
    iot_json_add_object(m, "application", a);
    iot_json_add_object(m, "service"    , s);
    iot_json_add_object(m, "container"  , c);

    iot_debug("original manifest: '%s'", iot_json_object_to_string(orig));
    iot_debug("converted manifest: '%s'", iot_json_object_to_string(m));

    return m;

 nomem:
    log_error("Failed to create manifest in new format.");
    goto cleanup;

 invalid_manifest:
    log_error("Invalid original manifest, failed to convert it.");
    log_error("original manifest: %s", iot_json_object_to_string(orig));
    goto cleanup;

 overflow:
    log_error("Service command line too long.");
    goto cleanup;

 cleanup:
    iot_json_unref(m);
    iot_json_unref(a);
    iot_json_unref(s);
    iot_json_unref(c);
    iot_json_unref(start);
    iot_json_unref(nw);

    return NULL;
}


PREPROCESSOR_REGISTER("convert-manifest", convert_manifest, -10, NULL);
