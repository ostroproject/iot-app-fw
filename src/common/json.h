/*
 * Copyright (c) 2012, Intel Corporation
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

#ifndef __IOT_JSON_H__
#define __IOT_JSON_H__

#include <stdarg.h>
#include <stdbool.h>

#include "iot/config.h"

#ifndef JSON_INCLUDE_PATH_JSONC
#    include <json/json.h>
#    include <json/linkhash.h>
/* workaround for older broken json-c not exposing json_object_iter */
#    include <json/json_object_private.h>
#else
#    include <json-c/json.h>
#    include <json-c/linkhash.h>
/* workaround for older broken json-c not exposing json_object_iter */
#    include <json-c/json_object_private.h>
#endif

#include <iot/common/macros.h>

IOT_CDECL_BEGIN

/*
 * We use json-c as the underlying json implementation, However, we do
 * not want direct json-c dependencies to spread all over the code base
 * (at least not yet). So we try to define here an envelop layer that
 * hides json-c underneath.
 */

/** Type of a JSON object. */
typedef json_object iot_json_t;

/** JSON object/member types. */
typedef enum {
    IOT_JSON_NULL    = json_type_null,
    IOT_JSON_STRING  = json_type_string,
    IOT_JSON_BOOLEAN = json_type_boolean,
    IOT_JSON_INTEGER = json_type_int,
    IOT_JSON_DOUBLE  = json_type_double,
    IOT_JSON_OBJECT  = json_type_object,
    IOT_JSON_ARRAY   = json_type_array
} iot_json_type_t;

/** Type for a JSON member iterator. */
typedef json_object_iter iot_json_iter_t;

/** Create a new JSON object of the given type. */
iot_json_t *iot_json_create(iot_json_type_t type, ...);

/** Clone the given JSON object, creating a new private copy of it. */
iot_json_t *iot_json_clone(iot_json_t *o);

/** Deserialize a string to a JSON object. */
iot_json_t *iot_json_string_to_object(const char *str, int len);

/** Serialize a JSON object to a string. */
const char *iot_json_object_to_string(iot_json_t *o);

/** Add a reference to the given JSON object. */
iot_json_t *iot_json_ref(iot_json_t *o);

/** Remove a reference from the given JSON object, freeing if it was last. */
void iot_json_unref(iot_json_t *o);

/** Get the type of a JSON object. */
iot_json_type_t iot_json_get_type(iot_json_t *o);

/** Check if a JSON object has the given type. */
int iot_json_is_type(iot_json_t *o, iot_json_type_t type);

/** Convenience macros to create values of JSON objects of basic types. */
#define iot_json_string(s) iot_json_create(IOT_JSON_STRING, s, -1)
#define iot_json_integer(i) iot_json_create(IOT_JSON_INTEGER, (int)i)
#define iot_json_double(d) iot_json_create(IOT_JSON_DOUBLE, (double)d)
#define iot_json_boolean(b) iot_json_create(IOT_JSON_BOOLEAN, b ? 1 : 0)

/** Convenience macros to get values of JSON objects of basic types. */
#define iot_json_string_value(o)  json_object_get_string(o)
#define iot_json_integer_value(o) json_object_get_int(o)
#define iot_json_double_value(o)  json_object_get_double(o)
#define iot_json_boolean_value(o) json_object_get_boolean(o)

/** Set a member of a JSON object. */
void iot_json_add(iot_json_t *o, const char *key, iot_json_t *m);

/** Create a new JSON object and set it as a member of another object. */
iot_json_t *iot_json_add_member(iot_json_t *o, const char *key,
                                iot_json_type_t type, ...);

/** Convenience macros to add members of various basic types. */
#define iot_json_add_string(o, key, s) \
    iot_json_add_member(o, key, IOT_JSON_STRING, s, -1)

#define iot_json_add_string_slice(o, key, s, l)         \
    iot_json_add_member(o, key, IOT_JSON_STRING, s, l)

#define iot_json_add_integer(o, key, i) \
    iot_json_add_member(o, key, IOT_JSON_INTEGER, i)

#define iot_json_add_double(o, key, d) \
    iot_json_add_member(o, key, IOT_JSON_DOUBLE, d)

#define iot_json_add_boolean(o, key, b) \
    iot_json_add_member(o, key, IOT_JSON_BOOLEAN, (int)b)

/** Let'em use this for regularity of the naming convention. */
#define iot_json_add_object iot_json_add

/** Add an array member from a native C array of the given type. */
iot_json_t *iot_json_add_array(iot_json_t *o, const char *key,
                               iot_json_type_t type, ...);

/** Convenience macros for adding arrays of various basic types. */
#define iot_json_add_string_array(o, key, arr, size) \
    iot_json_add_array(o, key, IOT_JSON_STRING, arr, size)

#define iot_json_add_int_array(o, key, arr, size) \
    iot_json_add_array(o, key, IOT_JSON_INTEGER, arr, size)

#define iot_json_add_double_array(o, key, arr, size) \
    iot_json_add_array(o, key, IOT_JSON_DOUBLE, arr, size)

#define iot_json_add_boolean_array(o, key, arr, size) \
    iot_json_add_array(o, key, IOT_JSON_BOOLEAN, arr, size)

/** Get the member of a JSON object as a json object. */
iot_json_t *iot_json_get(iot_json_t *o, const char *key);

/** Get the member of a JSON object in a type specific format. */
int iot_json_get_member(iot_json_t *o, const char *key,
                        iot_json_type_t type, ...);

/** Convenience macros to get members of various types. */
#define iot_json_get_string(o, key, sptr)               \
    iot_json_get_member(o, key, IOT_JSON_STRING, sptr)

#define iot_json_get_integer(o, key, iptr)              \
    iot_json_get_member(o, key, IOT_JSON_INTEGER, iptr)

#define iot_json_get_double(o, key, dptr)               \
    iot_json_get_member(o, key, IOT_JSON_DOUBLE, dptr)

#define iot_json_get_boolean(o, key, bptr)              \
    iot_json_get_member(o, key, IOT_JSON_BOOLEAN, bptr)

#define iot_json_get_array(o, key, aptr)                \
    iot_json_get_member(o, key, IOT_JSON_ARRAY, aptr)

#define iot_json_get_object(o, key, optr)               \
    iot_json_get_member(o, key, IOT_JSON_OBJECT, optr)

/** Delete a member of a JSON object. */
void iot_json_del_member(iot_json_t *o, const char *key);

/** Get the number of fields of an object. */
int iot_json_object_length(iot_json_t *o);

/** Get the length of a JSON array object. */
int iot_json_array_length(iot_json_t *a);

/** Append a JSON object to an array object. */
int iot_json_array_append(iot_json_t *a, iot_json_t *v);

/** Create and append a new item to a JSON array object. */
iot_json_t *iot_json_array_append_item(iot_json_t *a, iot_json_type_t type,
                                       ...);

/** Convenience macros for appending array items of basic types. */
#define iot_json_array_append_string(a, s) \
    iot_json_array_append_item(a, IOT_JSON_STRING, s, -1)

#define iot_json_array_append_string_slice(a, s, l)       \
    iot_json_array_append_item(a, IOT_JSON_STRING, s, l)


#define iot_json_array_append_integer(a, i) \
    iot_json_array_append_item(a, IOT_JSON_INTEGER, (int)i)

#define iot_json_array_append_double(a, d) \
    iot_json_array_append_item(a, IOT_JSON_DOUBLE, 1.0*d)

#define iot_json_array_append_boolean(a, b) \
    iot_json_array_append_item(a, IOT_JSON_BOOLEAN, (int)b)

/** Add a JSON object to a given index of an array object. */
int iot_json_array_set(iot_json_t *a, int idx, iot_json_t *v);

/** Add a JSON object to a given index of an array object. */
int iot_json_array_set_item(iot_json_t *a, int idx, iot_json_type_t type, ...);

/** Get the object at a given index of a JSON array object. */
iot_json_t *iot_json_array_get(iot_json_t *a, int idx);

/** Get the element of a JSON array object at an index. */
int iot_json_array_get_item(iot_json_t *a, int idx, iot_json_type_t type, ...);

/** Convenience macros to get items of certain types from an array. */
#define iot_json_array_get_string(a, idx, sptr) \
    iot_json_array_get_item(a, idx, IOT_JSON_STRING, sptr)

#define iot_json_array_get_integer(a, idx, iptr) \
    iot_json_array_get_item(a, idx, IOT_JSON_INTEGER, iptr)

#define iot_json_array_get_double(a, idx, dptr) \
    iot_json_array_get_item(a, idx, IOT_JSON_DOUBLE, dptr)

#define iot_json_array_get_boolean(a, idx, bptr) \
    iot_json_array_get_item(a, idx, IOT_JSON_BOOLEAN, bptr)

#define iot_json_array_get_array(a, idx, aptr) \
    iot_json_array_get_item(a, idx, IOT_JSON_ARRAY, aptr)

#define iot_json_array_get_object(a, idx, optr) \
    iot_json_array_get_item(a, idx, IOT_JSON_OBJECT, optr)

/** Iterate through the members of an object. */
#define iot_json_foreach_member(o, _k, _v, it)                  \
    for (it.entry = json_object_get_object((o))->head;          \
         (it.entry ?                                            \
          (_k = it.key = (char *)it.entry->k,                   \
           _v = it.val = (iot_json_t *)it.entry->v,             \
           it.entry) : 0);                                      \
         it.entry = it.entry->next)

/** Parse a JSON object from the given string. */
int iot_json_parse_object(char **str, int *len, iot_json_t **op);

IOT_CDECL_END

#endif /* __IOT_JSON_H__ */
