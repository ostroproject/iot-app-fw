/*
 * Copyright (c) 2012, Intel Corporation
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

#define _GNU_SOURCE
#include <link.h>
#include <elf.h>

#include <stdarg.h>
#include <limits.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/log.h>
#include <iot/common/hashtbl.h>
#include <iot/common/utils.h>
#include <iot/common/debug.h>

#define WILDCARD  "*"

int iot_debug_stamp = 0;                    /* debug config stamp */

static int         debug_enabled;           /* debug messages enabled */
static iot_htbl_t *rules_on;                /* enabling rules */
static iot_htbl_t *rules_off;               /* disabling rules */

static void free_rule_cb(void *key, void *entry)
{
    IOT_UNUSED(key);

    iot_free(entry);
}


static int init_rules(void)
{
    iot_htbl_config_t hcfg;

    iot_clear(&hcfg);
    hcfg.comp = iot_string_comp;
    hcfg.hash = iot_string_hash;
    hcfg.free = free_rule_cb;

    rules_on  = iot_htbl_create(&hcfg);
    rules_off = iot_htbl_create(&hcfg);

    if (rules_on == NULL || rules_off == NULL)
        return FALSE;
    else
        return TRUE;
}


static void reset_rules(void)
{
    if (rules_on != NULL) {
        iot_htbl_destroy(rules_on , TRUE);
        rules_on = NULL;
    }
    if (rules_off != NULL) {
        iot_htbl_destroy(rules_off, TRUE);
        rules_off = NULL;
    }
}


void iot_debug_reset(void)
{
    debug_enabled = FALSE;
    reset_rules();
}


int iot_debug_enable(int enabled)
{
    int prev = debug_enabled;

    debug_enabled = !!enabled;
    iot_log_enable(IOT_LOG_MASK_DEBUG);
    iot_debug_stamp++;

    return prev;
}


static int add_rule(const char *func, const char *file, int line, int off)
{
    iot_htbl_t  *ht;
    char        *rule, *r, buf[PATH_MAX * 2];

    if (rules_on == NULL)
        if (!init_rules())
            return FALSE;

    r = rule = NULL;

    if (!off)
        ht = rules_on;
    else
        ht = rules_off;

    if (func != NULL && file == NULL && line == 0) {
        r    = iot_htbl_lookup(ht, (void *)func);
        rule = (char *)func;
    }
    else if (func != NULL && file != NULL && line == 0) {
        snprintf(buf, sizeof(buf), "%s@%s", func, file);
        r    = iot_htbl_lookup(ht, (void *)buf);
        rule = buf;
    }
    else if (func == NULL && file != NULL && line == 0) {
        snprintf(buf, sizeof(buf), "@%s", file);
        r    = iot_htbl_lookup(ht, (void *)buf);
        rule = buf;
    }
    else if (func == NULL && file != NULL && line > 0) {
        snprintf(buf, sizeof(buf), "%s:%d", file, line);
        r    = iot_htbl_lookup(ht, (void *)buf);
        rule = buf;
    }

    if (r != NULL)
        return FALSE;

    rule = iot_strdup(rule);
    if (rule == NULL)
        return FALSE;

    if (iot_htbl_insert(ht, rule, rule)) {
        iot_debug_stamp++;

        return TRUE;
    }
    else {
        iot_free(rule);

        return FALSE;
    }
}


static int del_rule(const char *func, const char *file, int line, int off)
{
    iot_htbl_t  *ht;
    char        *r, buf[PATH_MAX * 2];

    if (rules_on == NULL)
        if (!init_rules())
            return FALSE;

    r = NULL;

    if (!off)
        ht = rules_on;
    else
        ht = rules_off;

    if (func != NULL && file == NULL && line == 0) {
        r = iot_htbl_remove(ht, (void *)func, TRUE);
    }
    else if (func != NULL && file != NULL && line == 0) {
        snprintf(buf, sizeof(buf), "%s@%s", func, file);
        r = iot_htbl_remove(ht, (void *)buf, TRUE);
    }
    else if (func == NULL && file != NULL && line == 0) {
        snprintf(buf, sizeof(buf), "@%s", file);
        r = iot_htbl_remove(ht, (void *)buf, TRUE);
    }
    else if (func == NULL && file != NULL && line > 0) {
        snprintf(buf, sizeof(buf), "%s:%d", file, line);
        r = iot_htbl_remove(ht, (void *)buf, TRUE);
    }

    if (r != NULL) {
        iot_debug_stamp++;

        return TRUE;
    }
    else
        return FALSE;
}


int iot_debug_set_config(const char *cmd)
{
    char    buf[2 * PATH_MAX + 1], *colon, *at, *eq;
    char   *func, *file, *end;
    size_t  len;
    int     del, off, line;

    if (*cmd == '+' || *cmd == '-')
        del = (*cmd++ == '-');
    else
        del = FALSE;

    eq = strchr(cmd, '=');

    if (eq == NULL) {
        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        off = FALSE;
    }
    else {
        if (!strcmp(eq + 1, "on"))
            off = FALSE;
        else if (!strcmp(eq + 1, "off"))
            off = TRUE;
        else
            return FALSE;

        len = eq - cmd;
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;

        strncpy(buf, cmd, len);
        buf[len] = '\0';
    }

    colon = strchr(buf, ':');

    if (colon != NULL) {
        if (strchr(buf, '@') != NULL)
            return FALSE;

        *colon = '\0';
        func   = NULL;
        file   = buf;
        line   = strtoul(colon + 1, &end, 10);

        if (end && *end)
            return FALSE;

        iot_log_info("%s file='%s', line=%d, %s", del ? "del" : "add",
                     file, line, off ? "off" : "on");
    }
    else {
        at = strchr(buf, '@');

        if (at != NULL) {
            *at = '\0';
            func = (at == buf ? NULL : buf);
            file = at + 1;
            line = 0;

            iot_log_info("%s func='%s', file='%s', %s", del ? "del" : "add",
                         func ? func : "", file, off ? "off" : "on");
        }
        else {
            func = buf;
            file = NULL;
            line = 0;

            iot_log_info("%s func='%s' %s", del ? "del" : "add",
                         func, off ? "off" : "on");
        }
    }

    if (!del)
        return add_rule(func, file, line, off);
    else
        return del_rule(func, file, line, off);

    return TRUE;
}


typedef struct {
    FILE *fp;
    int   on;
} dump_t;


static int dump_rule_cb(void *key, void *object, void *user_data)
{
    dump_t     *d     = (dump_t *)user_data;
    FILE       *fp    = d->fp;
    const char *state = d->on ? "on" : "off";

    IOT_UNUSED(key);

    fprintf(fp, "    %s %s\n", (char *)object, state);

    return IOT_HTBL_ITER_MORE;
}


int iot_debug_dump_config(FILE *fp)
{
    dump_t d;

    fprintf(fp, "Debugging is %sabled\n", debug_enabled ? "en" : "dis");

    if (rules_on != NULL) {
        fprintf(fp, "Debugging rules:\n");

        d.fp = fp;
        d.on = TRUE;
        iot_htbl_foreach(rules_on , dump_rule_cb, &d);
        d.on = FALSE;
        iot_htbl_foreach(rules_off, dump_rule_cb, &d);
    }
    else
        fprintf(fp, "No debugging rules defined.\n");

    return TRUE;
}


void iot_debug_msg(const char *file, int line, const char *func,
                   const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    iot_log_msgv(IOT_LOG_DEBUG, file, line, func, format, ap);
    va_end(ap);
}


int iot_debug_check(const char *func, const char *file, int line)
{
    char  buf[2 * PATH_MAX], *base;
    void *key;

    if (!debug_enabled || rules_on == NULL)
        return FALSE;

    base = NULL;
    key  = (void *)func;
    if (iot_htbl_lookup(rules_on, key) != NULL)
        goto check_suppress;

    base = strrchr(file, '/');
    if (base != NULL)
        base++;

    key = buf;

    snprintf(buf, sizeof(buf), "@%s", file);
    if (iot_htbl_lookup(rules_on, key) != NULL)
        goto check_suppress;

    if (base != NULL) {
        snprintf(buf, sizeof(buf), "@%s", base);
        if (iot_htbl_lookup(rules_on, key) != NULL)
            goto check_suppress;
    }

    snprintf(buf, sizeof(buf), "%s@%s", func, file);
    if (iot_htbl_lookup(rules_on, key) != NULL)
        goto check_suppress;

    snprintf(buf, sizeof(buf), "%s:%d", file, line);
    if (iot_htbl_lookup(rules_on, key) != NULL)
        goto check_suppress;

    if (iot_htbl_lookup(rules_on, (void *)WILDCARD) == NULL)
        return FALSE;


 check_suppress:
    if (rules_off == NULL)
        return TRUE;

    key = (void *)func;
    if (iot_htbl_lookup(rules_off, key) != NULL)
        return FALSE;

    key = buf;

    snprintf(buf, sizeof(buf), "@%s", file);
    if (iot_htbl_lookup(rules_off, key) != NULL)
        return FALSE;

    if (base != NULL) {
        snprintf(buf, sizeof(buf), "@%s", base);
        if (iot_htbl_lookup(rules_off, key) != NULL)
            return FALSE;
    }

    snprintf(buf, sizeof(buf), "%s@%s", func, file);
    if (iot_htbl_lookup(rules_off, key) != NULL)
        return FALSE;

    snprintf(buf, sizeof(buf), "%s:%d", file, line);
    if (iot_htbl_lookup(rules_off, key) != NULL)
        return FALSE;

    return TRUE;
}


