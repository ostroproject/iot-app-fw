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

#ifndef __IOT_MACROS_H__
#define __IOT_MACROS_H__

#include <stddef.h>

#ifndef FALSE
#    define FALSE 0
#    define TRUE (!FALSE)
#endif

#ifdef __cplusplus
#  define typeof(expr) decltype(expr)
#endif

/** Align ptr to multiple of align. */
#define IOT_ALIGN(ptr, align) (((ptr) + ((align)-1)) & ~((align)-1))

/** Get the offset of the given member in a struct/union of the given type. */
#define IOT_OFFSET(type, member) ((ptrdiff_t)(&(((type *)0)->member)))

/** Determine the dimension of the given array. */
#define IOT_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#ifndef __GNUC__
#  define __FUNCTION__ __func__
#endif

#ifdef __GNUC__
     /** MAX that evalutes its arguments only once. */
#    define IOT_MAX(a, b) ({                                              \
            typeof(a) _a = (a);                                           \
            typeof(b) _b = (b);                                           \
            _a > _b ? _a : _b;                                            \
        })

     /** MIN that evalutes its arguments only once. */
#    define IOT_MIN(a, b) ({                                              \
            typeof(a) _a = (a), _b = (b);                                 \
            _a < _b ? _a : _b;                                            \
        })

     /** Likeliness branch-prediction hint for the compiler. */
#    define IOT_LIKELY(cond)   __builtin_expect((cond), 1)

     /** Unlikeliness branch-prediction hint for the compiler. */
#    define IOT_UNLIKELY(cond) __builtin_expect((cond), 0)

     /** Prevent symbol from being exported (overriden by linker scripts). */
#    define IOT_HIDDEN __attribute__ ((visibility ("hidden")))

     /** Request a symbol to be exported (overriden by linker scripts). */
#    define IOT_EXPORT __attribute__ ((visibility ("default")))

     /** Ask the compiler to check for the presence of a NULL-sentinel. */
#    define IOT_NULLTERM __attribute__((sentinel))

     /** Ask for printf-like format string checks of calls to this function. */
#    define IOT_PRINTF_LIKE(format_idx, first_arg_idx)                    \
         __attribute__ ((format (printf, format_idx, first_arg_idx)))

     /** Mark a function to be called before main is entered. */
#    define IOT_INIT __attribute__((constructor(32768)))
#    define IOT_INIT_AT(prio) __attribute__ ((constructor(prio)))

     /** Mark a function to be called after main returns, or exit is called. */
#    define IOT_EXIT __attribute__ ((destructor(32768)))
#    define IOT_EXIT_AT(prio) __attribute__ ((destructor(prio)))

/** Mark a variable unused. */
#    define IOT_UNUSED(var) (void)var
#else /* ! __GNUC__ */
#    define IOT_LIKELY(cond)   (cond)
#    define IOT_UNLIKELY(cond) (cond)
#    define IOT_HIDDEN
#    define IOT_EXPORT
#    define IOT_NULLTERM
#    define IOT_PRINTF_LIKE(format_idx, first_arg_idx)
#    define __FUNCTION__ __func__
#    define IOT_INIT
#    define IOT_INIT_AT
#    define IOT_EXIT
#    define IOT_EXIT_AT
#    define IOT_UNUSED(var)
#endif

/** Macro that can be used to pass the location of its usage. */
#    define __LOC__ __FILE__, __LINE__, __FUNCTION__

/** Assertions. */
#ifndef NDEBUG
#    define IOT_ASSERT(expr, fmt, args...) do {                           \
        if (!(expr)) {                                                    \
            printf("assertion '%s' failed at %s@%s:%d: "fmt"\n", #expr,   \
                   __FUNCTION__, __FILE__, __LINE__, ## args);          \
            abort();                                                      \
        }                                                                 \
    } while (0)
#else
#    define IOT_ASSERT(expr, msg) do { } while (0)
#endif

/** Create a version integer from a (major, minor, micro) tuple. */
#define IOT_VERSION_INT(maj, min, mic)                                    \
    ((((maj) & 0xff) << 16) | (((min) & 0xff) << 8) | ((mic) & 0xff))

/** Create a version string from a (const) (major, minor, micro) tuple.  */
#define IOT_VERSION_STRING(maj, min, mic) #maj"."#min"."#mic

/** Extract major version from a version integer. */
#define IOT_VERSION_MAJOR(ver) (((ver) >> 16) & 0xff)

/** Extract minor version from a version integer. */
#define IOT_VERSION_MINOR(ver) (((ver) >>  8) & 0xff)

/** Extract micro version from a version integer. */
#define IOT_VERSION_MICRO(ver) ((ver) & 0xff)

/** Macro to stringify a macro argument. */
#define IOT_STRINGIFY(arg) #arg

/** C++-compatibility macros. */
#ifdef __cplusplus
#    define IOT_CDECL_BEGIN extern "C" {
#    define IOT_CDECL_END   }
#else
#    define IOT_CDECL_BEGIN
#    define IOT_CDECL_END
#endif

#endif /* __IOT_MACROS_H__ */

