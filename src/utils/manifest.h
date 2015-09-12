/*
 * Copyright (c) 2015, Intel Corporation
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

#ifndef __IOT_UTILS_MANIFEST_H__
#define __IOT_UTILS_MANIFEST_H__

#include <iot/config.h>
#include <iot/common/macros.h>
#include <iot/common/json.h>
#include <iot/common/mainloop.h>
#include <iot/common/hash-table.h>

#define IOT_MANIFEST_MAXSIZE (16 * 1024) /**< limit for manifest JSON data */


IOT_CDECL_BEGIN

/**
 * \addtogroup IoTCommonIfra
 *
 * @{
 */

/**
 * @brief Default location for commonly accessible/installed manifests.
 */
#ifndef IOT_MANIFEST_COMMON_PATH
#    define IOT_MANIFEST_COMMON_PATH "/usr/share/iot/applications"
#endif

/**
 * @brief Default location for per-user accessible/installed manifest.
 */
#ifndef IOT_MANIFEST_USER_PATH
#    define IOT_MANIFEST_USER_PATH "/usr/share/iot/users"
#endif

/**
 * @brief Set the common and user-specific manifest directories.
 *
 * Set the directories where manifests for commonly installed and
 * user-specific applications are to be located.
 *
 * @param [in] common  manifest directory for commonly installed applications
 * @param [in] user    top manifest directory for user-specific applications
 *
 * @return Returns 0 upon success, -1 upon error.
 */
int iot_manifest_set_directories(const char *common, const char *user);

/**
 * @brief Control caching of manifests.
 *
 * Controls whether manifests will be cached internally by the library.
 *
 * @param [in] enabled  controls whether caching should be turned on or off
 * @param [in] ml       if enabled and given, used for tracking manifest changes
 *
 * @return Returns 0 upon success, -1 otherwise.
 */
int iot_manifest_caching(bool enabled);

/**
 * @brief Controls automatic tracking of manifest changes.
 *
 * If a mainloop is given, caching will be enabled and manifest changes
 * will be automatically tracked by the library.
 *
 * @param [in] ml  mainloop for asynchronous change notifications
 *
 * @return Returns 0 upon success, -1 otherwise.
 */
int iot_manifest_tracking(iot_mainloop_t *ml);

/**
 * @brief Prepopulate the manifest cache.
 *
 * Scan the common and user directories for manifests and populate the
 * manifest cache.
 *
 * @return Returns 0 upon success, -1 otherwise.
 */
int iot_manifest_populate_cache(void);


/**
 * @brief Reset the manifest cache.
 *
 * Reset the manifest cache, purging any potentially cached entries.
 */
void iot_manifest_reset_cache(void);


/**
 * @brief A manifest read from the filesystem, and potentially cached.
 */
typedef struct iot_manifest_s iot_manifest_t;

/**
 * @brief Get the manifest for the given user and package.
 *
 * Get the manifest for the given user and package.
 *
 * @param [in] usr  uid of the user, or -1 for common applications
 * @param [in] pkg  name of the package to get the manifest for
 *
 * @return Returns the manifest for the given user and package, or @NULL
 *         upon error. Once done with the manifest, the user must release
 *         it using @iot_manifest_unref.
 */
iot_manifest_t *iot_manifest_get(uid_t usr, const char *pkg);

/**
 * @brief Unreference the given loaded manifest.
 *
 * Decrement the reference count on the given manifest. If the last
 * reference is gone the manifest will be freed.
 *
 * @param [in] m  the manifest to decrement the refcount on
 */
void iot_manifest_unref(iot_manifest_t *m);

/**
 * @brief Get the user for this manifest.
 *
 * Return the uid of the user this manifest was installed for.
 *
 * @param [in] m  manifest to return the uid for
 *
 * @return Returns the uid of the user this manifest was installed for,
 *         or -1 if this is a commonly installed manifest.
 */
uid_t iot_manifest_user(iot_manifest_t *m);

/**
 * @brief Get the package name for this manifest.
 *
 * Return the name of the package this manifest corresponds to.
 *
 * @param [in] m  manifest to return the package name for
 *
 * @return Returns the name of the package this manifest corresponds to.
 */
const char *iot_manifest_package(iot_manifest_t *m);

/**
 * @brief Get the path this manifest was loaded from.
 *
 * Return the path to the manifest file in the filesystem.
 *
 * @param [in] m  manifest to return the path for
 *
 * @return Returns the path to the manifest file in the filesystem.
 */
const char *iot_manifest_path(iot_manifest_t *m);

/**
 * @brief Get the list of applications from the given manifest.
 *
 * Fetch the list of applications from the given manifest and store
 * it in the given buffer.
 *
 * @param [in]  m     manifest to get the applications from
 * @param [out] buf   buffer to store applications in
 * @param [in]  size  amount of space in buffer
 *
 * @return Returns the total number of applications in the manifest.
 *         Note that this might be larger than @size if an insufficient
 *         buffer was provided.
 */
int iot_manifest_applications(iot_manifest_t *m, char **buf, size_t size);

/**
 * @brief Get the description for the given application from the manifest.
 *
 * Fetch the description for the given application from the manifest.
 *
 * @param [in] m    manifest to get the privileges from
 * @param [in] app  application to fetch description for
 *
 * @return Returns the description found. The returned data is valid
 *         only until the manifest is freed.
 */
const char *iot_manifest_description(iot_manifest_t *m, const char *app);

/**
 * @brief Get the list of privileges for the given application.
 *
 * Fetch the list of privileges for the given application from the manifest
 * and store them in the given buffer.
 *
 * @param [in]  m     manifest to get the privileges from
 * @param [out] buf   buffer to store privileges in
 * @param [in]  size  amount of space in buffer
 *
 * @return Returns the total number of privileges for the application,
 *         or -1 on error. Note that the number of privileges might be
 *         larger than @size if an insufficient buffer was provided.
 *         The returned data is valid only until the manifest is freed.
 */
int iot_manifest_privileges(iot_manifest_t *m, const char *app,
                            char **buf, size_t size);

/**
 * @brief Get the list of arguments for the given application.
 *
 * Fetch the list of arguments for starting the application from
 * the manifest and store them in the given buffer.
 *
 * @param [in]  m     manifest to get the privileges from
 * @param [out] buf   buffer to store arguments in
 * @param [in]  size  amount of space in buffer
 *
 * @return Returns the total number of arguments for the application,
 *         or -1 on error. Note that the number of arguments might be
 *         larger than @size if an insufficient buffer was provided.
 *         The returned data is valid only until the manifest is freed.
 */
int iot_manifest_arguments(iot_manifest_t *m, const char *app,
                           char **buf, size_t size);

/**
 * @brief Get JSON data from the manifest.
 *
 * Get the JSON data from the manifest, for the given applciation,
 * or for all applications if @NULL is passed.
 *
 * @param [in] m    manifest to fetch raw JSON data from
 * @param [in] app  application name to get JSON data for
 *
 * @return Return the JSON data found. The data is refcounted on behalf of
 *         the caller. Once done with it, the caller must release the data
 *         by calling @iot_json_unref on it.
 */
iot_json_t *iot_manifest_data(iot_manifest_t *m, const char *app);

/**
 * @brief Get the desktop file for the given application from the manifest.
 *
 * Fetch the path to the desktop file for the given application from
 * the manifest.
 *
 * @param [in] m    manifest to get the desktop file path from
 * @param [in] app  application to fetch the path for
 *
 * @return Returns the path found. The returned data is valid
 *         only until the manifest is freed.
 */
const char *iot_manifest_desktop_path(iot_manifest_t *m, const char *app);

/**
 * @brief Bitmasks describing manifest validation results.
 */
typedef enum {
    IOT_MANIFEST_OK                = 0x000,  /**< checks OK */
    IOT_MANIFEST_FAILED            = 0x001,  /**< unknown error */
    IOT_MANIFEST_MISNAMED          = 0x002,  /**< not named *.manifest */
    IOT_MANIFEST_UNLOADABLE        = 0x004,  /**< failed to load manifet */
    IOT_MANIFEST_MALFORMED         = 0x008,  /**< not an array or object */
    IOT_MANIFEST_MISSING_FIELD     = 0x010,  /**< missing mandatory field */
    IOT_MANIFEST_INVALID_FIELD     = 0x020,  /**< field with invalid type */
    IOT_MANIFEST_INVALID_BINARY    = 0x040,  /**< invalid/unexecutable binary */
    IOT_MANIFEST_INVALID_PRIVILEGE = 0x080,  /**< invalid/unknown privilege */
    IOT_MANIFEST_INVALID_DESKTOP   = 0x100,  /**< invalid desktop file */
} iot_manifest_status_t;

/**
 * @brief Validate the given manifest file.
 *
 * Load temporarily the given manifest file and perform a number
 * of sanity checks on it, then unload it.
 *
 * @param [in] usr   user for this manifest, or -1
 * @param [in] path  path to manifest file
 *
 * @return Returns IOT_MANIFEST_OK (0) if the manifest passed all tests,
 *         or a bitwise or indicating which tests the manifest failed.
 */
int iot_manifest_validate_file(uid_t usr, const char *path);

/**
 * @brief Validate the given loaded manifest.
 *
 * Perform a number of sanity checks on the given manifest.
 *
 * @param [in] m  manifest to validate.
 *
 * @return Returns IOT_MANIFEST_OK (0) if the manifest passed all tests,
 *         or a bitwise or indicating which tests the manifest failed.
 */
int iot_manifest_validate(iot_manifest_t *m);

/**
 * @brief Read the given manifest file.
 *
 * Read the given manifest file. Do not consult the cache, neither cache
 * the resulting manifest.
 *
 * @param [in] path  Path of the manifest file to read.
 *
 * @return Returns the read manifest or @NULL upon failure.
 */
iot_manifest_t *iot_manifest_read(const char *path);

/**
 * @brief Type for a manifest cache iterator.
 */
typedef iot_hashtbl_iter_t iot_manifest_iter_t;

/**
 * @brief Helper function to begin the interation of the manifest cache.
 *
 * Initialize the given iterator for going through all entries in the
 * manifest cache.
 *
 * @param [in] it  iterator to initialize.
 *
 * Note that probably you should use the @IOT_MANIFEST_CACHE_FOREACH macro
 * for iterating the cache instead of this function.
 */
void _iot_manifest_cache_begin(iot_manifest_iter_t *it);

/**
 * @brief Helper function to continue the interation of the manifest cache.
 *
 * Continue the iteration of the manifest cache, moving to the next
 * entry.
 *
 * @param [in] it  iterator used in for iterating the cache
 *
 * @return Returns @NULL once all entries have been iterated through.
 *
 * Note that probably you should use the @IOT_MANIFEST_CACHE_FOREACH macro
 * for iterating the cache instead of this function.
 */
void *_iot_manifest_cache_next(iot_manifest_iter_t *it, iot_manifest_t **m);

/**
 * @brief Macro for iterating through all manifest cache entries.
 *
 * Iterate through all entries in the manifest cache.
 *
 * @param [in,out] _it  iterator used for iterating the cache
 * @param [out]    _m   variable to successively set to the cache entries
 *
 */
#define IOT_MANIFEST_CACHE_FOREACH(_it, _m)                             \
    for (_iot_manifest_cache_begin((_it)),                              \
             _iot_manifest_cache_next((_it), (_m));                     \
         (_it)->b != NULL;                                              \
         _iot_manifest_cache_next((_it), (_m)))

/**
 * @}
 */

IOT_CDECL_END

#endif /* __IOT_UTILS_MANIFEST_H__ */
