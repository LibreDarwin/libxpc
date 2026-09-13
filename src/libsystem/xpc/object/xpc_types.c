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
 * xpc_types.c — type singletons for the reimplemented XPC framework.
 *
 * One const struct per XPC kind.  Every xpc_object_t's ->isa points at
 * one of these, which lets xpc_get_type() work by pure pointer
 * comparison and gives xpc_copy_description() a stable printable name.
 */

#include "xpc_internal.h"

const struct _xpc_type_s _xpc_type_null       = { "null",       XPC_KIND_NULL };
const struct _xpc_type_s _xpc_type_bool       = { "bool",       XPC_KIND_BOOL };
const struct _xpc_type_s _xpc_type_int64      = { "int64",      XPC_KIND_INT64 };
const struct _xpc_type_s _xpc_type_uint64     = { "uint64",     XPC_KIND_UINT64 };
const struct _xpc_type_s _xpc_type_double     = { "double",     XPC_KIND_DOUBLE };
const struct _xpc_type_s _xpc_type_date       = { "date",       XPC_KIND_DATE };
const struct _xpc_type_s _xpc_type_data       = { "data",       XPC_KIND_DATA };
const struct _xpc_type_s _xpc_type_string     = { "string",     XPC_KIND_STRING };
const struct _xpc_type_s _xpc_type_uuid       = { "uuid",       XPC_KIND_UUID };
const struct _xpc_type_s _xpc_type_mach_send  = { "mach-send",  XPC_KIND_MACH_SEND };
const struct _xpc_type_s _xpc_type_shmem      = { "shmem",      XPC_KIND_SHMEM };
const struct _xpc_type_s _xpc_type_array      = { "array",      XPC_KIND_ARRAY };
const struct _xpc_type_s _xpc_type_dictionary = { "dictionary", XPC_KIND_DICTIONARY };
const struct _xpc_type_s _xpc_type_error      = { "error",      XPC_KIND_ERROR };
const struct _xpc_type_s _xpc_type_connection = { "connection", XPC_KIND_CONNECTION };
const struct _xpc_type_s _xpc_type_endpoint   = { "endpoint",   XPC_KIND_ENDPOINT };
const struct _xpc_type_s _xpc_type_activity   = { "activity",   XPC_KIND_ACTIVITY };
const struct _xpc_type_s _xpc_type_session    = { "session",    XPC_KIND_SESSION };
const struct _xpc_type_s _xpc_type_listener   = { "listener",   XPC_KIND_LISTENER };

xpc_kind_t
xpc_kind_from_type(xpc_type_t t)
{
    if (!t) return XPC_KIND_COUNT;
    return t->kind;
}