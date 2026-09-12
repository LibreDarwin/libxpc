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
#include "xpc_private.h"

#include <mach/mach.h>

struct _xpc_pipe_s {
    mach_port_t port;
    uint64_t    flags;
    bool        valid;
    bool        local; /* receive right in THIS task: stub bridge target */
};

static xpc_local_routine_handler_t g_local_handler;

void xpc_pipe_set_local_handler(xpc_local_routine_handler_t handler) {
    g_local_handler = handler;
}

/* Receive options: deliver a full audit trailer so routine replies can be
 * verified to originate from launchd (PID 1, euid 0). */
#define XPC_RCV_TRAILER_OPTS \
    (MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | \
     MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT))

/* Real launchd hands large replies back as complex messages carrying a
 * single OOL descriptor whose address/size point at the serialized env.
 * If we deserialize the 44-byte head we see the descriptor count where the
 * CPX@ magic should be, so follow the OOL region when present. */
typedef struct {
    const void *bytes;
    size_t      len;
    vm_address_t ool_addr;
    vm_size_t    ool_len;
    boolean_t    ool_deallocate;
    mach_port_t *ports;         /* send rights from a ports descriptor */
    mach_msg_size_t nports;
    vm_size_t    ports_len;     /* region length, when deallocating */
    boolean_t    ports_deallocate;
    boolean_t    ports_heap;    /* heap array (realloc) instead of OOL region */
} xpc_pipe_reply_t;

static int pipe_reply_payload(mach_msg_header_t *msg, xpc_pipe_reply_t *out) {
    out->bytes = msg;
    out->len = msg->msgh_size;
    out->ool_addr = 0; out->ool_len = 0; out->ool_deallocate = false;
    out->ports = NULL; out->nports = 0; out->ports_len = 0;
    out->ports_deallocate = false;
    out->ports_heap = false;
    if (!(msg->msgh_bits & MACH_MSGH_BITS_COMPLEX)) return KERN_SUCCESS;
    mach_msg_body_t *body = (mach_msg_body_t *)(void *)((uint8_t *)msg + 24);
    mach_msg_descriptor_t *d =
        (mach_msg_descriptor_t *)(void *)(body + 1);
    size_t inline_off = (size_t)((uint8_t *)d - (uint8_t *)msg);
    for (mach_msg_size_t i = 0; i < body->msgh_descriptor_count; i++) {
        switch (d->out_of_line.type) {
        case MACH_MSG_OOL_DESCRIPTOR:
            /* First OOL payload wins; keep scanning for a ports
             * descriptor that may follow it. */
            if (!out->ool_len) {
                if (getenv("XPC_DEBUG")) {
                    fprintf(stderr, "[pipe] reply payload via OOL desc "
                        "(size=%u)\n", d->out_of_line.size);
                }
                out->bytes = d->out_of_line.address;
                out->len = d->out_of_line.size;
                out->ool_addr = (vm_address_t)(uintptr_t)d->out_of_line.address;
                out->ool_len = d->out_of_line.size;
                out->ool_deallocate = d->out_of_line.deallocate;
            }
            break;
        case MACH_MSG_OOL_PORTS_DESCRIPTOR:
            out->ports = d->ool_ports.address;
            out->nports = d->ool_ports.count;
            out->ports_len = (vm_size_t)d->ool_ports.count *
                sizeof(mach_port_name_t);
            out->ports_deallocate = d->ool_ports.deallocate;
            break;
        case MACH_MSG_PORT_DESCRIPTOR: {
            /* Send right carried inline as a port descriptor (type 0,
             * what real launchctl emits in requests and launchd echoes
             * back in some replies).  Slot it into the same table the
             * deserializer indexes into. */
            mach_port_name_t *np;
            mach_msg_size_t n = out->nports + 1;
            np = realloc(out->ports, (size_t)n * sizeof(mach_port_name_t));
            if (!np) return KERN_FAILURE;
            out->ports = np;
            out->ports[n - 1] = d->port.name;
            out->nports = n;
            out->ports_len = out->nports * sizeof(mach_port_name_t);
            /* Port-descriptor rights are heap-copied above, not an OOL
             * region, so never vm_deallocate them. */
            out->ports_deallocate = false;
            out->ports_heap = true;
            break;
        }
        case MACH_MSG_OOL_VOLATILE_DESCRIPTOR:
            /* Same 16-byte descriptor footprint on LP64. */
            break;
        default:
            break;
        }
        /* Advance by the descriptor's actual footprint: OOL variants are
         * 16 bytes on LP64, port descriptors 12. */
        size_t dsize = (d->out_of_line.type == MACH_MSG_PORT_DESCRIPTOR)
            ? sizeof(mach_msg_port_descriptor_t)
            : sizeof(mach_msg_descriptor_t);
        d = (mach_msg_descriptor_t *)(void *)((uint8_t *)d + dsize);
        inline_off += dsize;
    }
    /* Complex but inline: the serialized object follows the descriptors. */
    if (!out->ool_len && inline_off < msg->msgh_size) {
        out->bytes = (uint8_t *)msg + inline_off;
        out->len = msg->msgh_size - inline_off;
        return KERN_SUCCESS;
    }
    if (!out->ool_len) return KERN_FAILURE;
    return KERN_SUCCESS;
}

xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags) {
    xpc_pipe_t p = calloc(1, sizeof(*p));
    if (p) {
        p->port = port; p->flags = flags; p->valid = true;
        mach_port_type_t t = 0;
        if (mach_port_type(mach_task_self(), port, &t) == KERN_SUCCESS &&
            (t & MACH_PORT_TYPE_RECEIVE)) {
            p->local = true; /* the stub bridge terminates here */
        }
    }
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

    /* Same-task stub bridge: dispatch the serialized message through the
     * registered handler instead of mach_msg (see xpc_internal.h). */
    if (p->local && g_local_handler) {
        size_t rlen = 0;
        uint8_t *reply_bytes = g_local_handler(bytes, length, message_id,
            &rlen);
        free(bytes);
        if (reply) *reply = NULL;
        if (!reply_bytes) return KERN_FAILURE;
        if (reply) {
            xpc_pipe_reply_t pl;
            mach_msg_header_t *rh =
                (mach_msg_header_t *)(void *)reply_bytes;
            if (pipe_reply_payload(rh, &pl) != KERN_SUCCESS) {
                free(reply_bytes);
                return KERN_INVALID_ARGUMENT;
            }
            *reply = xpc_wire_deserialize_with_ports(pl.bytes, pl.len,
                pl.ports, pl.nports);
            if (pl.ool_addr && pl.ool_deallocate) {
                vm_deallocate(mach_task_self(), pl.ool_addr, pl.ool_len);
            }
            if (pl.ports && pl.ports_deallocate) {
                vm_deallocate(mach_task_self(),
                    (vm_address_t)(uintptr_t)pl.ports, pl.ports_len);
            } else if (pl.ports && pl.ports_heap) {
                free(pl.ports);
            }
            /* The stub answers inline, so reply_bytes owns no OOL region;
             * release the message buffer itself. */
            free(reply_bytes);
            if (!*reply) {
                for (mach_msg_size_t i = 0; i < pl.nports; i++) {
                    mach_port_deallocate(mach_task_self(), pl.ports[i]);
                }
                return KERN_INVALID_ARGUMENT;
            }
        } else {
            free(reply_bytes);
        }
        return KERN_SUCCESS;
    }

    message->msgh_remote_port = p->port;
    message->msgh_local_port = MACH_PORT_NULL;
    /* Keep the COMPLEX bit (set by the serializer when the object graph
     * carries send rights) while forcing our standard dispositions. */
    message->msgh_bits = (message->msgh_bits & MACH_MSGH_BITS_COMPLEX) |
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);

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
    message->msgh_bits = (message->msgh_bits & MACH_MSGH_BITS_COMPLEX) |
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    message->msgh_local_port = reply_port;

    uint8_t receive_buffer[65536] __attribute__((aligned(16)));
    mach_msg_header_t *received = (mach_msg_header_t *)(void *)receive_buffer;

    /* Send, then receive the reply separately.  With MACH_SEND_MSG|MACH_RCV_MSG
     * combined, mach_msg receives into the same buffer it sent from, so a
     * distinct reply buffer requires two calls (the buffer just sent is freed
     * below once the send completes). */
    result = mach_msg(message, MACH_SEND_MSG, message->msgh_size, 0,
        MACH_PORT_NULL, 0, MACH_PORT_NULL);
    free(bytes);
    if (result != KERN_SUCCESS) {
        mach_port_mod_refs(mach_task_self(), reply_port,
            MACH_PORT_RIGHT_RECEIVE, -1);
        return result;
    }
    result = mach_msg(received, MACH_RCV_MSG | XPC_RCV_TRAILER_OPTS,
        0, sizeof(receive_buffer), reply_port, 0, MACH_PORT_NULL);
    mach_port_mod_refs(mach_task_self(), reply_port, MACH_PORT_RIGHT_RECEIVE, -1);
    if (result != KERN_SUCCESS) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[pipe] receive result=0x%x\n", result);
        }
        return result;
    }
    if (received->msgh_id != XPC_PIPE_ID_REPLY) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[pipe] bad reply id: got %#x want %#x (bits=%#x size=%u)\n",
                received->msgh_id, XPC_PIPE_ID_REPLY,
                received->msgh_bits, received->msgh_size);
        }
        return KERN_INVALID_ARGUMENT;
    }
    if (reply) {
        xpc_pipe_reply_t pl;
        if (pipe_reply_payload(received, &pl) != KERN_SUCCESS) {
            if (getenv("XPC_DEBUG")) {
                fprintf(stderr, "[pipe] no usable payload in reply\n");
            }
            return KERN_INVALID_ARGUMENT;
        }
        *reply = xpc_wire_deserialize_with_ports(pl.bytes, pl.len,
            pl.ports, pl.nports);
        if (pl.ool_addr && pl.ool_deallocate) {
            vm_deallocate(mach_task_self(), pl.ool_addr, pl.ool_len);
        }
        if (pl.ports && pl.ports_deallocate) {
            vm_deallocate(mach_task_self(), (vm_address_t)(uintptr_t)pl.ports,
                pl.ports_len);
        } else if (pl.ports && pl.ports_heap) {
            free(pl.ports);
        }
        if (!*reply) {
            /* Deserialization failed: the send rights we were granted are
             * orphaned; release them so they do not leak. */
            for (mach_msg_size_t i = 0; i < pl.nports; i++) {
                mach_port_deallocate(mach_task_self(), pl.ports[i]);
            }
            return KERN_INVALID_ARGUMENT;
        }
        /* Attach sender identity from the mach trailer, if present. */
        {
            mach_msg_audit_trailer_t *trailer =
                (mach_msg_audit_trailer_t *)(void *)
                ((uint8_t *)received + round_msg(received->msgh_size));
            if (trailer->msgh_trailer_type == MACH_MSG_TRAILER_FORMAT_0 &&
                trailer->msgh_trailer_size >=
                    (mach_msg_trailer_size_t)sizeof(*trailer)) {
                xpc_dictionary_set_audit_token(*reply, &trailer->msgh_audit);
            }
        }
    }
    return reply && *reply ? KERN_SUCCESS : KERN_INVALID_ARGUMENT;
}

int xpc_pipe_simpleroutine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r) {
    return send(p, o, r, XPC_PIPE_ID_SIMPLEROUTINE);
}
int xpc_pipe_routine(xpc_pipe_t p, xpc_object_t o, xpc_object_t *r,
    uint32_t routine) {
    /* Real launchd wire id is XPC_PIPE_ID_ROUTINE | (routine & 0xffff):
     * e.g. 0x400000cf for "list". Without the routine bits launchd's
     * dispatcher cannot demux the request. */
    return send(p, o, r, XPC_PIPE_ID_ROUTINE | (routine & 0xffff));
}
