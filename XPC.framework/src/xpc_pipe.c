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

#include "xpc_internal.h"

struct _xpc_pipe_s { mach_port_t port; uint64_t flags; bool valid; };

xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags) {
    xpc_pipe_t p = calloc(1, sizeof(*p));
    if (p) { p->port = port; p->flags = flags; p->valid = true; }
    return p;
}

int xpc_pipe_invalidate(xpc_pipe_t p) {
    if (!p) return KERN_INVALID_ARGUMENT;
    p->valid = false; free(p); return KERN_SUCCESS;
}

static int send(xpc_pipe_t p, xpc_object_t object, xpc_object_t *reply,
    uint32_t message_id) {
    if (reply) *reply = NULL;
    if (!p || !p->valid || !object) return KERN_INVALID_ARGUMENT;

    size_t length = 0;
    uint8_t *bytes = xpc_wire_serialize(object, message_id, &length);
    if (!bytes) return KERN_INVALID_ARGUMENT;
    mach_msg_header_t *message = (mach_msg_header_t *)(void *)bytes;
    message->msgh_remote_port = p->port;
    message->msgh_local_port = MACH_PORT_NULL;
    message->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);

    mach_msg_return_t result;
    if (message_id == XPC_PIPE_ID_SIMPLEROUTINE) {
        result = mach_msg(message, MACH_SEND_MSG, message->msgh_size, 0,
            MACH_PORT_NULL, 0, MACH_PORT_NULL);
        free(bytes);
        return result;
    }

    mach_port_t reply_port = MACH_PORT_NULL;
    result = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
        &reply_port);
    if (result != KERN_SUCCESS) { free(bytes); return result; }
    message->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
        MACH_MSG_TYPE_MAKE_SEND);
    message->msgh_local_port = reply_port;

    uint8_t receive_buffer[65536] __attribute__((aligned(16)));
    mach_msg_header_t *received = (mach_msg_header_t *)(void *)receive_buffer;
    result = mach_msg(message, MACH_SEND_MSG | MACH_RCV_MSG,
        message->msgh_size, sizeof(receive_buffer), reply_port, 0,
        MACH_PORT_NULL);
    free(bytes);
    mach_port_mod_refs(mach_task_self(), reply_port, MACH_PORT_RIGHT_RECEIVE, -1);
    if (result != KERN_SUCCESS) return result;
    if (received->msgh_id != XPC_PIPE_ID_REPLY) return KERN_INVALID_ARGUMENT;
    if (reply) *reply = xpc_wire_deserialize(receive_buffer,
        received->msgh_size);
    return reply && *reply ? KERN_SUCCESS : KERN_INVALID_ARGUMENT;
}

int xpc_pipe_simpleroutine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r) {
    return send(p, o, r, XPC_PIPE_ID_SIMPLEROUTINE);
}
int xpc_pipe_routine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r) {
    return send(p, o, r, XPC_PIPE_ID_ROUTINE);
}
