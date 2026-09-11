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
 * xpc_mach_send.c — Mach send-right values for the reimplemented XPC
 * framework.
 *
 * Wraps a send right so it can travel inside a dictionary and be
 * transported to launchd via the OOL_PORTS descriptor.  Mirrors
 * Apple's xpc_mach_send_create(3): the object borrows the right and the
 * caller keeps ownership, so xpc_release() never touches the port
 * (a deallocating variant would nuke the task's bootstrap port if one
 * were ever wrapped).
 *
 * Rights that arrive from the wire (xpc_mach_send_create_owned) were
 * minted for us by the kernel's COPY_SEND disposition; those objects
 * deallocate the right when released.
 */

#include "xpc_internal.h"

static xpc_object_t
xpc_mach_send_create_internal(mach_port_t port, bool dispose)
{
    xpc_mach_send_t *m = XPC_CAST(xpc_mach_send_t,
        xpc_object_alloc(&_xpc_type_mach_send, sizeof(xpc_mach_send_t)));
    if (!m) return NULL;
    m->port = port;
    m->dispose = dispose;
    return (xpc_object_t)m;
}

xpc_object_t
xpc_mach_send_create(mach_port_t port)
{
    return xpc_mach_send_create_internal(port, false);
}

xpc_object_t
xpc_mach_send_create_owned(mach_port_t port)
{
    return xpc_mach_send_create_internal(port, true);
}

mach_port_t
xpc_mach_send_get_port(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_mach_send)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_mach_send_t, obj)->port;
}

void
xpc_dictionary_set_mach_send(xpc_object_t dict, const char *key,
    mach_port_t port)
{
    xpc_object_t obj = xpc_mach_send_create(port);
    xpc_dictionary_set_value(dict, key, obj);
    xpc_release(obj);
}