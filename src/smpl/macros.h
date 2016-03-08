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

#ifndef __SMPL_MACROS_H__
#define __SMPL_MACROS_H__

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/debug.h>
#include <iot/common/list.h>

/* macros.h */
#define SMPL_UNUSED      IOT_UNUSED
#define SMPL_CDECL_BEGIN IOT_CDECL_BEGIN
#define SMPL_CDECL_END   IOT_CDECL_END
#define SMPL_PRINTF_LIKE IOT_PRINTF_LIKE

/* mm.h */
#define smpl_clear         iot_clear
#define smpl_allocz        iot_allocz
#define smpl_alloct(_type) ((_type *)iot_allocz(sizeof(_type)))
#define smpl_free          iot_free
#define smpl_strdup        iot_strdup
#define smpl_strndup       iot_strndup
#define smpl_reallocz      iot_reallocz
#define smpl_alloc_array   iot_alloc_array
#define smpl_allocz_array  iot_allocz_array

/* log.h */
#define SMPL_LOG_MASK_INFO    IOT_LOG_MASK_INFO
#define SMPL_LOG_MASK_WARNING IOT_LOG_MASK_WARNING
#define SMPL_LOG_MASK_ERROR   IOT_LOG_MASK_ERROR
#define SMPL_LOG_MASK_DEBUG   IOT_LOG_MASK_DEBUG
#define smpl_log_get_mask     iot_log_get_mask
#define smpl_log_set_mask     iot_log_set_mask
#define smpl_info  iot_log_info
#define smpl_warn  iot_log_warning
#define smpl_error iot_log_error
#define smpl_fatal(_exit_code, _fmt, _args...) do {     \
        iot_log_error("fatal error: "_fmt , ## _args);  \
        exit(_exit_code);                               \
    } while(0)


/* debug.h */
#define smpl_debug        iot_debug
#define smpl_debug_enable iot_debug_enable
#define smpl_debug_set    iot_debug_set_config

/* list.h */
#define smpl_list_t       iot_list_hook_t
#define SMPL_LIST         IOT_LIST_HOOK
#define SMPL_LIST_INIT    IOT_LIST_INIT
#define smpl_list_init    iot_list_init
#define smpl_list_append  iot_list_append
#define smpl_list_prepend iot_list_prepend
#define smpl_list_insert_after iot_list_insert_after
#define smpl_list_delete  iot_list_delete
#define smpl_list_move    iot_list_move
#define smpl_list_empty   iot_list_empty
#define smpl_list_entry   iot_list_entry
#define smpl_list_foreach      iot_list_foreach
#define smpl_list_foreach_back iot_list_foreach_back

/* json.h */
#define SMPL_JSON_STRING        IOT_JSON_STRING
#define SMPL_JSON_INTEGER       IOT_JSON_INTEGER
#define SMPL_JSON_DOUBLE        IOT_JSON_DOUBLE
#define SMPL_JSON_BOOLEAN       IOT_JSON_BOOLEAN
#define SMPL_JSON_OBJECT        IOT_JSON_OBJECT
#define SMPL_JSON_ARRAY         IOT_JSON_ARRAY
#define smpl_json_load_file     iot_json_load_file
#define smpl_json_unref         iot_json_unref
#define smpl_json_type          iot_json_get_type
#define smpl_json_get           iot_json_get
#define smpl_json_string_value  iot_json_string_value
#define smpl_json_integer_value iot_json_integer_value
#define smpl_json_double_value  iot_json_double_value
#define smpl_json_boolean_value iot_json_boolean_value
#define smpl_json_object_length iot_json_object_length
#define smpl_json_array_length  iot_json_array_length
#define smpl_json_array_get     iot_json_array_get
#define smpl_json_t             iot_json_t
#define smpl_json_iter_t        iot_json_iter_t
#define smpl_json_foreach       iot_json_foreach_member
#define smpl_json_iter_last(it) ((it).entry->next == NULL)

#endif /* __SMPL_MACROS_H__ */

