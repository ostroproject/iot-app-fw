/*
 * Copyright (c) 2012-2015, Intel Corporation
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

#ifndef __IOT_FILEUTILS_H__
#define __IOT_FILEUTILS_H__

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iot/common/macros.h>

IOT_CDECL_BEGIN

/**
 * \addtogroup IoTCommonInfra
 * @{
 *
 * @file file-utils.h
 *
 * @brief Utilities for various file- and directory-related tasks.
 *
 * file-utils.[hc] provides functions for scaning directories, creating
 * directories or directory hierarchies, and normalizing a path names.
 */


/**
 * @brief Bitmasks for diretory entry types.
 */

/** Directory entry types. */
typedef enum {
    IOT_DIRENT_UNKNOWN = 0,                      /**< unknown */
    IOT_DIRENT_NONE = 0x00,                      /**< unknown */
    IOT_DIRENT_FIFO = 0x01,                      /**< FIFO */
    IOT_DIRENT_CHR  = 0x02,                      /**< character device */
    IOT_DIRENT_DIR  = 0x04,                      /**< directory */
    IOT_DIRENT_BLK  = 0x08,                      /**< block device */
    IOT_DIRENT_REG  = 0x10,                      /**< regular file */
    IOT_DIRENT_LNK  = 0x20,                      /**< symbolic link */
    IOT_DIRENT_SOCK = 0x40,                      /**< socket */
    IOT_DIRENT_ANY  = 0xff,                      /**< mask of all types */

    IOT_DIRENT_FOLLOW_LNK = 0x000,               /**< follow symlinks */
    IOT_DIRENT_ACTUAL_LNK = 0x100,               /**< don't follow symlinks */
    IOT_DIRENT_IGNORE_LNK = 0x200,               /**< ignore symlinks */
    IOT_DIRENT_ACTION_LNK = 0x300,               /**< symlink action mask */
} iot_dirent_type_t;


/**
 * @brief Explicit pattern prefix for a shell-like globbing pattern.
 */
#define IOT_PATTERN_GLOB  "glob:"

/**
 * @brief Explicit pattern prefix for a regexp pattern.
 */
#define IOT_PATTERN_REGEX "regex:"


/**
 * @brief Type of a directory scanning callback function.
 *
 * During a directory scan for every matching entry found a callback of a
 * function of this type will be called.
 *
 * @param [in] dir        path of the directory being scanned
 * @param [in] entry      name of the found matching entry
 * @param [in] type       type of the found entry
 * @param [in] user_data  opaque callback user data
 *
 * @param The callback should return > 0 for continuing the scan, 0 for
 *        stopping the scan normally, and -1 for aborting the scan with
 *        an error.
 */
typedef int (*iot_scan_dir_cb_t)(const char *dir, const char *entry,
                                 iot_dirent_type_t type, void *user_data);

/**
 * @brief Scan a directory for matching entries.
 *
 * Scan the given directory for entries matching the given type @mask
 * and @pattern. Pattern can be a regexp pattern, a shell-like globbing
 * pattern, or @NULL to match everything. The type @mask can include the
 * following special flags for specifying how symbolic links should be
 * treated by the scan:
 *
 *    IOT_DIRENT_FOLLOW_LNK: dereference links
 *    IOT_DIRENT_IGNORE_LNK: ignore links
 *    IOT_DIRENT_ACTUAL_LNK: don't follow links, pass them to the callback
 *
 * By default symlinks are followed.
 *
 * @param [in] path       path of the directory to scan
 * @param [in] pattern    regexp or globbing pattern, or @NULL
 * @param [in] mask       which entry types to match
 * @param [in] cb         callback to call for matching entries
 * @param [in] user_data  opaque user data to pass to @cb
 *
 * @return Returns 0 for success, -1 upon errors.
 */
int iot_scan_dir(const char *path, const char *pattern, iot_dirent_type_t mask,
                 iot_scan_dir_cb_t cb, void *user_data);

/**
 * @brief Create a directory, creating leading path as necessary.
 *
 * Create the given directory, creating any missing intermediate component
 * as necessary. This function pretty closely resembles 'mkdir -p'. Unlike
 * 'mkdir -p', however, it tries to clean up after itself upon failure,
 * removing directories it created. It does not treat '..' in a special way.
 * This allows one to create full directory hierarchies atomically, either
 * creating the full hierarchy, or not modifying the existing hierarchy
 * at all.
 *
 * @param [in] path  directory hierarchy to create
 * @param [in] mode  mode used for creating missing directories
 *
 * @return Returns 0 upon sucess, -1 otherwise.
 */
int iot_mkdir(const char *path, mode_t mode);

/**
 * @brief Normalize a path name, removing any '..' and '.' components.
 *
 * This function parses the given path an returns an equivalent normalized
 * path that does not contain any '..' or '..' components.
 *
 * @param [out] buf     buffer to return normalized path in
 * @param [in]  size    space available in @buf
 * @param [in]  path    path to normalize
 *
 * @return Returns @buf upon success, @NULL otherwise.
 */
char *iot_normalize_path(char *buf, size_t size, const char *path);

IOT_CDECL_END

/**
 * @}
 */

#endif /* __IOT_FILEUTILS_H__ */
