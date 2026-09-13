/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xpc_uuid.c — UUID values for the reimplemented XPC framework.
 *
 * 16 raw bytes copied into the object payload, matching Apple's
 * xpc_uuid_create(3).  The wire tag (XPC_WIRE_UUID = 0xa000), equality,
 * hashing, and the type singleton were already wired in; only the two
 * public entry points were missing.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_uuid_create(const uuid_t uuid)
{
    xpc_uuid_t *u = XPC_CAST(xpc_uuid_t,
        xpc_object_alloc(&_xpc_type_uuid, sizeof(xpc_uuid_t)));
    if (!u) return NULL;
    memcpy(u->uuid, uuid, sizeof(uuid_t));
    return (xpc_object_t)u;
}

const uint8_t *
xpc_uuid_get_bytes(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_uuid)) return NULL;
    return XPC_CAST(xpc_uuid_t, obj)->uuid;
}