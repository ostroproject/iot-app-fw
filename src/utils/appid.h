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

#ifndef __IOT_UTILS_APPID_H__
#define __IOT_UTILS_APPID_H__

#include <iot/config.h>
#include <iot/common/macros.h>

IOT_CDECL_BEGIN

/**
 * \addtogroup IoTCommonIfra
 *
 * @{
 */

/**
 * @brief Parse an application ID of the form [[<usr>:][<pkg>:]]<app>.
 *
 * Parse an application ID into usr, pkg, and app. By convention,
 * 'foo' is equivalent to the canonical ':foo:foo', and 'foo:bar' is
 * equivalent to the canonical ':foo:bar'. IOW, either <usr>, or both
 * <usr> and <app> can be omitted. In the latter case, <app> defaults
 * to <pkg>.
 *
 * @param [in]  appid    application ID to parse
 * @param [out] usrbuf   buffer to store <usr> into, or @NULL
 * @param [in]  usrsize  size of usrbuf
 * @param [out] pkgbuf   buffer to store <pkg> into, or @NULL
 * @param [in]  pkgsize  size of pkgbuf
 * @param [out] appbuf   buffer to store <app> into, or @NULL
 * @param [in]  appsize  size of appbuf
 *
 * @return Returns 0 upon sucess, or -1 upon an error in which case
 *         @errno is also set.
 */
int iot_appid_parse(const char *appid, char *usrbuf, size_t usrsize,
                    char *pkgbuf, size_t pkgsize, char *appbuf, size_t appsize);

/**
 * @brief Convenience function to get the <usr> part of an application ID.
 *
 * @param [in]  appid  application ID to parse
 * @param [out] buf    buffer to store <usr> into
 * @param [in]  size   size of buffer
 *
 * @return Returns buf upon success, NULL upon failure.
 */
char *iot_appid_user(const char *appid, char *buf, size_t size);

/**
 * @brief Convenience function to get the <pkg> part of an application ID.
 *
 * @param [in]  appid  application ID to parse
 * @param [out] buf    buffer to store <pkg> into
 * @param [in]  size   size of buffer
 *
 * @return Returns buf upon success, NULL upon failure.
 */
char *iot_appid_package(const char *appid, char *buf, size_t size);

/**
 * @brief Convenience function to get the <app> part of an application ID.
 *
 * @param [in]  appid  application ID to parse
 * @param [out] buf    buffer to store <app> into
 * @param [in]  size   size of buffer
 *
 * @return Returns buf upon success, NULL upon failure.
 */
char *iot_appid_app(const char *appid, char *buf, size_t size);


#endif /* __IOT_UTILS_APPID_H__ */
