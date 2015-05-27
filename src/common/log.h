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

#ifndef __IOT_LOG_H__
#define __IOT_LOG_H__

/** \file
 * Logging functions and macros.
 */

#include <stdarg.h>

#include <iot/common/macros.h>
#include <iot/common/debug.h>

IOT_CDECL_BEGIN

#define IOT_LOG_NAME_ERROR   "error"             /**< name for error level */
#define IOT_LOG_NAME_WARNING "warning"           /**< name for warning level */
#define IOT_LOG_NAME_INFO    "info"              /**< name for info level */
#define IOT_LOG_NAME_DEBUG   "debug"             /**< name for debug level */


/**
 * Logging levels.
 */
typedef enum {
    IOT_LOG_ERROR = 0,                           /**< error log level */
    IOT_LOG_WARNING,                             /**< warning log level */
    IOT_LOG_INFO,                                /**< info log level  */
    IOT_LOG_DEBUG,                               /**< debug log level */
} iot_log_level_t;


/**
 * Logging masks.
 */
typedef enum {
    IOT_LOG_MASK_ERROR   = 0x01,                 /**< error logging mask */
    IOT_LOG_MASK_WARNING = 0x02,                 /**< warning logging mask */
    IOT_LOG_MASK_INFO    = 0x04,                 /**< info logging mask */
    IOT_LOG_MASK_DEBUG   = 0x08,                 /**< debug logging mask */
} iot_log_mask_t;

#define IOT_LOG_MASK(level) (1 << ((level)-1))   /**< mask of level */
#define IOT_LOG_UPTO(level) ((1 << (level+1))-1) /**< mask up to level */


/** Parse a string of comma-separated log level names to a log mask. */
iot_log_mask_t iot_log_parse_levels(const char *levels);

/** Write the given log mask as a string to the given buffer. */
const char *iot_log_dump_mask(iot_log_mask_t mask, char *buf, size_t size);

/** Clear current logging level and enable levels in mask. */
iot_log_mask_t iot_log_set_mask(iot_log_mask_t mask);

/** Enable logging for levels in mask. */
iot_log_mask_t iot_log_enable(iot_log_mask_t mask);

/** Disable logging for levels in mask. */
iot_log_mask_t iot_log_disable(iot_log_mask_t mask);

/** Get the current logging level mask. */
#define iot_log_get_mask() iot_log_disable(0)

/**
 * Logging target names.
 */
#define IOT_LOG_NAME_STDOUT  "stdout"
#define IOT_LOG_NAME_STDERR  "stderr"
#define IOT_LOG_NAME_SYSLOG  "syslog"

/**
 * Logging targets.
 */
#define IOT_LOG_TO_STDOUT     "stdout"
#define IOT_LOG_TO_STDERR     "stderr"
#define IOT_LOG_TO_SYSLOG     "syslog"
#define IOT_LOG_TO_FILE(path) ((const char *)(path))


/** Parse a log target name to IOT_LOG_TO_*. */
const char *iot_log_parse_target(const char *target);

/** Set logging target. */
int iot_log_set_target(const char *target);

/** Get the current log target. */
const char *iot_log_get_target(void);

/** Get all available logging targets. */
int iot_log_get_targets(const char **targets, size_t size);

/** Log an error. */
#define iot_log_error(fmt, args...) \
    iot_log_msg(IOT_LOG_ERROR, __LOC__, fmt , ## args)

/** Log a warning. */
#define iot_log_warning(fmt, args...) \
    iot_log_msg(IOT_LOG_WARNING, __LOC__, fmt , ## args)

/** Log an informational message. */
#define iot_log_info(fmt, args...) \
    iot_log_msg(IOT_LOG_INFO, __LOC__, fmt , ## args)

/** Generic logging function. */
void iot_log_msg(iot_log_level_t level,
                 const char *file, int line, const char *func,
                 const char *format, ...) IOT_PRINTF_LIKE(5, 6);

/** Generic logging function for easy wrapping. */
void iot_log_msgv(iot_log_level_t level, const char *file,
                  int line, const char *func, const char *format, va_list ap);

/** Type for custom logging functions. */
typedef void (*iot_logger_t)(void *user_data,
                             iot_log_level_t level, const char *file,
                             int line, const char *func, const char *format,
                             va_list ap);

/** Register a new logging target. */
int iot_log_register_target(const char *name, iot_logger_t logger,
                            void *user_data);

/** Unregister the given logging target. */
int iot_log_unregister_target(const char *name);

IOT_CDECL_END

#endif /* __IOT_LOG_H__ */
