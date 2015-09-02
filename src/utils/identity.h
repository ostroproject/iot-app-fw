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

#ifndef __IOT_UTILS_IDENTITIY_H__
#define __IOT_UTILS_IDENTITY_H__

#include <iot/config.h>
#include <iot/common/macros.h>

IOT_CDECL_BEGIN

/**
 * \addtogroup IoTCommonIfra
 *
 * @{
 */

/**
 * @brief Convenience function to resolve the user ID for a user name.
 *
 * Fetch the unique user ID corresponding to the given user name.
 *
 * @param [in] name  user name to resolve
 *
 * @return Returns the user ID for the given user on success, or
 *         (uid_t)-1 upon failure.
 */
uid_t iot_get_userid(const char *name);

/**
 * @brief Convenience function to resolve the group ID for a given group name.
 *
 * Fetch the unique group ID corresponding to the given group name.
 *
 * @param [in] name  group name to resolve
 *
 * @return Returns the group ID for the given group on success, or
 *         (gid_t)-1 upon failure.
 */
gid_t iot_get_groupid(const char *name);

/**
 * @}
 */


IOT_CDECL_END

#endif /* __IOT_UTILS_IDENTITY_H__ */
