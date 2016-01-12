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

#ifndef __IOT_MM_H__
#define __IOT_MM_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <iot/common/macros.h>

IOT_CDECL_BEGIN

#define IOT_MM_ALIGN 8                       /* object alignment */
#define IOT_MM_CONFIG_ENVVAR "__IOT_MM_CONFIG"

#define iot_alloc(size)        iot_mm_alloc((size), __LOC__)
#define iot_free(ptr)          iot_mm_free((ptr), __LOC__)
#define iot_strdup(s)          iot_mm_strdup((s), __LOC__)

#define iot_strndup(s, n) ({                                              \
            char *_p;                                                     \
                                                                          \
            _p = iot_allocz((n) + 1);                                     \
                                                                          \
            if (_p != NULL) {                                             \
                strncpy(_p, s, n);                                        \
                _p[(n)] = '\0';                                           \
            }                                                             \
                                                                          \
            _p;                                                           \
        })

#define iot_datadup(ptr, size) ({                                         \
            typeof(ptr) _ptr = iot_alloc(size);                           \
                                                                          \
            if (_ptr != NULL)                                             \
                memcpy(_ptr, ptr, size);                                  \
                                                                          \
            _ptr;                                                         \
        })

#define iot_allocz(size) ({                                               \
            void *_ptr;                                                   \
                                                                          \
            if ((_ptr = iot_mm_alloc(size, __LOC__)) != NULL)             \
                memset(_ptr, 0, size);                                    \
                                                                          \
            _ptr;})

#define iot_calloc(n, size) iot_allocz((n) * (size))

#define iot_reallocz(ptr, o, n) ({                                        \
            typeof(ptr) _ptr;                                             \
            typeof(o)   _o;                                               \
            typeof(n)   _n    = (n);                                      \
            size_t      _size = sizeof(*_ptr) * (_n);                     \
                                                                          \
            if ((ptr) != NULL)                                            \
                _o = o;                                                   \
            else                                                          \
                _o = 0;                                                   \
                                                                          \
            _ptr = (typeof(ptr))iot_mm_realloc(ptr, _size, __LOC__);      \
            if (_ptr != NULL || _n == 0) {                                \
                if ((unsigned)(_n) > (unsigned)(_o))                      \
                    memset(_ptr + (_o), 0,                                \
                           ((_n)-(_o)) * sizeof(*_ptr));                  \
                ptr = _ptr;                                               \
            }                                                             \
            _ptr; })

#define iot_realloc(ptr, size) ({                                         \
            typeof(ptr) _ptr;                                             \
            size_t      _size = size;                                     \
                                                                          \
            _ptr = (typeof(ptr))iot_mm_realloc(ptr, _size, __LOC__);      \
            if (_ptr != NULL || _size == 0)                               \
                ptr = _ptr;                                               \
                                                                          \
            _ptr; })

#define iot_memalign(ptrp, align, size)                                   \
    iot_mm_memalign(ptrp, align, size, __LOC__)

#define iot_memalignz(ptrp, align, size) ({                               \
            void *_ptrp;                                                  \
            int   _r;                                                     \
                                                                          \
            _r = iot_mm_memalign(&_ptrp, align, size, __LOC__);           \
            if (_r == 0)                                                  \
                memset(_ptrp, 0, size);                                   \
                                                                          \
            *ptrp = _ptrp;                                                \
            _r; })

#define iot_clear(obj) memset((obj), 0, sizeof(*(obj)))


#define iot_alloc_array(type, n)  ((type *)iot_alloc(sizeof(type) * (n)))
#define iot_allocz_array(type, n) ((type *)iot_allocz(sizeof(type) * (n)))

typedef enum {
    IOT_MM_PASSTHRU = 0,                 /* passthru allocator */
    IOT_MM_DEFAULT  = IOT_MM_PASSTHRU,   /* default is passthru */
    IOT_MM_DEBUG                         /* debugging allocator */
} iot_mm_type_t;


int iot_mm_config(iot_mm_type_t type);
void iot_mm_check(FILE *fp);
void iot_mm_dump(FILE *fp);

void *iot_mm_alloc(size_t size, const char *file, int line, const char *func);
void *iot_mm_realloc(void *ptr, size_t size, const char *file, int line,
                     const char *func);
char *iot_mm_strdup(const char *s, const char *file, int line,
                    const char *func);
int iot_mm_memalign(void **ptr, size_t align, size_t size, const char *file,
                    int line, const char *func);
void iot_mm_free(void *ptr, const char *file, int line, const char *func);




#define IOT_MM_OBJSIZE_MIN 16                    /* minimum object size */

enum {
    IOT_OBJPOOL_FLAG_POISON = 0x1,               /* poison free'd objects */
};


/*
 * object pool configuration
 */

typedef struct {
    char      *name;                             /* verbose pool name */
    size_t     limit;                            /* max. number of objects */
    size_t     objsize;                          /* size of a single object */
    size_t     prealloc;                         /* preallocate this many */
    int      (*setup)(void *);                   /* object setup callback */
    void     (*cleanup)(void *);                 /* object cleanup callback */
    uint32_t   flags;                            /* IOT_OBJPOOL_FLAG_* */
    int        poison;                           /* poisoning pattern */
} iot_objpool_config_t;


typedef struct iot_objpool_s iot_objpool_t;

/** Create a new object pool with the given configuration. */
iot_objpool_t *iot_objpool_create(iot_objpool_config_t *cfg);

/** Destroy an object pool, freeing all associated memory. */
void iot_objpool_destroy(iot_objpool_t *pool);

/** Allocate a new object from the pool. */
void *iot_objpool_alloc(iot_objpool_t *pool);

/** Free the given object. */
void iot_objpool_free(void *obj);

/** Grow @pool to accomodate @nobj new objects. */
int iot_objpool_grow(iot_objpool_t *pool, int nobj);

/** Shrink @pool by @nobj new objects, if possible. */
int iot_objpool_shrink(iot_objpool_t *pool, int nobj);

/** Get the value of a boolean key from the configuration. */
int iot_mm_config_bool(const char *key, int defval);

/** Get the value of a boolean key from the configuration. */
int32_t iot_mm_config_int32(const char *key, int32_t defval);

/** Get the value of a boolean key from the configuration. */
uint32_t iot_mm_config_uint32(const char *key, uint32_t defval);

/** Get the value of a string key from the configuration. */
int iot_mm_config_string(const char *key, const char *defval,
                         char *buf, size_t size);

IOT_CDECL_END

#endif /* __IOT_MM_H__ */

