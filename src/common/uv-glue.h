/*
 * Copyright (c) 2015, Intel Corporation
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

#ifndef __IOT_UV_H__
#define __IOT_UV_H__

#include <iot/config.h>
#include <iot/common/mainloop.h>

#ifndef LIBUV_ENABLED
#    error "libuv support has not been enabled"
#endif

#ifdef LIBUV_SHARED
#    include <uv.h>
#else
#    include <node/uv.h>
#endif

IOT_CDECL_BEGIN

/** Register the given IoT mainloop with the UV mainloop. */
int iot_mainloop_register_with_uv(iot_mainloop_t *ml, uv_loop_t *uv);

/** Unrgister the given IoT mainloop from the UV mainloop. */
int iot_mainloop_unregister_from_uv(iot_mainloop_t *ml);

/** Create a IoT mainloop and set it up with the UV mainloop. */
iot_mainloop_t *iot_mainloop_uv_get(uv_loop_t *uv);

IOT_CDECL_END

#endif /* __IOT_GLIB_H__ */
