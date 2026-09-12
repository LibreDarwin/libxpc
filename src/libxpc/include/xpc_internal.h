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
 * xpc_internal.h — private internals of the reimplemented XPC framework.
 *
 * Object model:
 *   Every xpc_object_t points to a struct _xpc_object_s whose first two
 *   words are (isa, refs).  `isa` points to one of the _xpc_type_*
 *   singletons in xpc_types.c; `refs` is an atomic refcount.
 *
 * Wire format:
 *   Serialization follows docs/WIRE_FORMAT.md exactly — the CPX@
 *   envelope, the 4-byte aligned keys, and the type tags below.
 *   Numbers are little-endian on all supported platforms.
 */

#ifndef __XPC_INTERNAL_H__
#define __XPC_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include <mach/mach.h>
#include "xpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Object model

/* Kind of object, mirrors the public type singletons. */
typedef enum xpc_kind {
    XPC_KIND_NULL = 0,
    XPC_KIND_BOOL,
    XPC_KIND_INT64,
    XPC_KIND_UINT64,
    XPC_KIND_DOUBLE,
    XPC_KIND_DATE,
    XPC_KIND_DATA,
    XPC_KIND_STRING,
    XPC_KIND_UUID,
    XPC_KIND_MACH_SEND,
    XPC_KIND_SHMEM,
    XPC_KIND_ARRAY,
    XPC_KIND_DICTIONARY,
    XPC_KIND_ERROR,
    XPC_KIND_CONNECTION,
    XPC_KIND_ENDPOINT,
    XPC_KIND_ACTIVITY,
    XPC_KIND_SESSION,
    XPC_KIND_LISTENER,
    XPC_KIND_COUNT,
} xpc_kind_t;

/* Type descriptor: named singletons per XPC kind. */
struct _xpc_type_s {
    const char *name;       /* e.g. "null", "int64" — used by description */
    xpc_kind_t kind;
};

typedef bool (*xpc_array_applier_t)(size_t index, xpc_object_t value);
typedef bool (*xpc_dictionary_applier_t)(const char *key, xpc_object_t value);
typedef void (*xpc_dictionary_applier_f_t)(const char *key,
    xpc_object_t value, void *context);

struct _xpc_object_s {
    xpc_type_t isa;             /* one of the _xpc_type_* singletons */
    _Atomic(uint64_t) refs;     /* atomic retain count */
};

/* Every concrete object type embeds this header first. */
#define XPC_OBJECT_HEADER(kindptr) \
    { .isa = (kindptr), .refs = 1 }

/* Downcast helper — behaves like Apple's _xpc_object_cast(). */
#define XPC_CAST(type, obj) \
    ((type *)(void *)(obj))
#define XPC_OBJECT_CHECK(obj, typeptr) \
    (obj && ((xpc_object_t)(obj))->isa == (typeptr))

/* Internal storage layouts (public bits expose a subset of these). */

typedef struct _xpc_scalar_s {
    struct _xpc_object_s hdr;
    union {
        bool bval;
        int64_t i64;
        uint64_t u64;
        double dbl;
        int64_t date_ns;
    } v;
} xpc_scalar_t;

typedef struct _xpc_string_s {
    struct _xpc_object_s hdr;
    char *data;         /* NUL-terminated */
    size_t length;      /* strlen(data) */
} xpc_string_t;

typedef struct _xpc_data_s {
    struct _xpc_object_s hdr;
    uint8_t *data;
    size_t length;
} xpc_data_t;

typedef struct _xpc_uuid_s {
    struct _xpc_object_s hdr;
    uuid_t uuid;
} xpc_uuid_t;

typedef struct _xpc_mach_send_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* send right */
    bool dispose;           /* true: release deallocates the right
                             * (rights received from the wire) */
} xpc_mach_send_t;

typedef struct _xpc_shmem_s {
    struct _xpc_object_s hdr;
    mach_port_t port;       /* memory-entry send right */
    uint64_t size;          /* page-aligned span of the entry */
    bool dispose;           /* true: release deallocates the right */
} xpc_shmem_t;

typedef struct _xpc_array_s {
    struct _xpc_object_s hdr;
    xpc_object_t *items;
    size_t count;
    size_t capacity;
} xpc_array_t;

typedef struct _xpc_dictionary_s {
    struct _xpc_object_s hdr;
    char **keys;
    xpc_object_t *values;
    size_t count;
    size_t capacity;
    audit_token_t audit_token;  /* sender token, set on receipt (§xpc_routines) */
    bool has_audit_token;
} xpc_dictionary_t;

typedef struct _xpc_error_s {
    struct _xpc_object_s hdr;
    char *desc;         /* human-readable failure message */
    int code;
} xpc_error_t;

typedef struct _xpc_endpoint_s {
    struct _xpc_object_s hdr;
    mach_port_t port;
} xpc_endpoint_t;

typedef struct _xpc_connection_s {
    struct _xpc_object_s hdr;
    mach_port_t port;
} xpc_connection_t;

#pragma mark - Type singletons (xpc_types.c)

extern const struct _xpc_type_s _xpc_type_null;
extern const struct _xpc_type_s _xpc_type_bool;
extern const struct _xpc_type_s _xpc_type_int64;
extern const struct _xpc_type_s _xpc_type_uint64;
extern const struct _xpc_type_s _xpc_type_double;
extern const struct _xpc_type_s _xpc_type_date;
extern const struct _xpc_type_s _xpc_type_data;
extern const struct _xpc_type_s _xpc_type_string;
extern const struct _xpc_type_s _xpc_type_uuid;
extern const struct _xpc_type_s _xpc_type_mach_send;
extern const struct _xpc_type_s _xpc_type_shmem;
extern const struct _xpc_type_s _xpc_type_array;
extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s _xpc_type_error;
extern const struct _xpc_type_s _xpc_type_connection;
extern const struct _xpc_type_s _xpc_type_endpoint;
extern const struct _xpc_type_s _xpc_type_activity;
extern const struct _xpc_type_s _xpc_type_session;
extern const struct _xpc_type_s _xpc_type_listener;

xpc_kind_t xpc_kind_from_type(xpc_type_t t);

#pragma mark - Construction helpers (xpc_object.c)

xpc_object_t xpc_object_alloc(xpc_type_t t, size_t size);
xpc_object_t xpc_object_alloc_scalar(xpc_type_t t);

/* Mach-send construction.  The public xpc_mach_send_create() borrows the
 * right; xpc_mach_send_create_owned() takes a received right (COPY_SEND
 * from an OOL_PORTS descriptor) and deallocates it on release. */
xpc_object_t xpc_mach_send_create_owned(mach_port_t port);

/* Same-task bridge (see xpc_pipe.c): a registered handler answers
 * serialized routine requests in place of a mach_msg reply hop. */
typedef uint8_t *(*xpc_local_routine_handler_t)(const uint8_t *msg,
    size_t msg_len, uint32_t msgh_id, size_t *reply_len);
void xpc_pipe_set_local_handler(xpc_local_routine_handler_t handler);

/* Shared-memory values (wire kind 0xc000).  xpc_shmem_create maps a
 * region as a Mach memory entry (launchd v7 maps it and writes the
 * version string); xpc_shmem_create_owned wraps a memory-entry right
 * received from the wire and deallocates it on release. */
xpc_object_t xpc_shmem_create_owned(mach_port_t port, uint64_t size);
mach_port_t xpc_shmem_get_port(xpc_object_t obj);

#pragma mark - Serialization (xpc_serialize.c)

/* Type tags as they appear on the wire.  (docs/WIRE_FORMAT.md §4) */
enum {
    XPC_WIRE_NULL   = 0x1000,
    XPC_WIRE_BOOL   = 0x2000,
    XPC_WIRE_INT64  = 0x3000,
    XPC_WIRE_UINT64 = 0x4000,
    XPC_WIRE_DOUBLE = 0x5000,
    XPC_WIRE_DATE   = 0x7000,
    XPC_WIRE_DATA   = 0x8000,
    XPC_WIRE_STRING = 0x9000,
    XPC_WIRE_UUID   = 0xa000,
    XPC_WIRE_SHMEM  = 0xc000,   /* shared-memory region (launchd v7 uses for version replies) */
    XPC_WIRE_MACH_SEND = 0xd000, /* mach send right; value = index into port-descriptor table */
    XPC_WIRE_ARRAY  = 0xe000,
    XPC_WIRE_DICT   = 0xf000,
};

/*
 * Serialize a dictionary (or array) into the wire format.
 * Returns a heap buffer; caller frees with free().  *out_len receives
 * the total message size (mach header + envelope + body).
 *
 * msg_id selects the header msgh_id:
 *   0x10000000 simpleroutine, 0x40000000 routine, 0x20000000 reply.
 */
uint8_t *xpc_wire_serialize(xpc_object_t object, uint32_t msg_id,
    size_t *out_len);

/* Envelope constants (docs/WIRE_FORMAT.md §2). */
#define XPC_WIRE_MAGIC "CPX@"
#define XPC_WIRE_VERSION 5u
#define XPC_WIRE_FLAGS_DICT 0xf000u

#pragma mark - Deserialization (xpc_deserialize.c)

/*
 * Parse a wire message back into an xpc_object_t.  Expects full mach
 * header + envelope.  Returns NULL (and sets *err cause) on malformed
 * input.  Caller owns the result.
 */
xpc_object_t xpc_wire_deserialize(const void *bytes, size_t len);

/*
 * Variant that can resolve mach-send values (wire tag 0x6000): ports is
 * the array of send rights carried in the message's OOL_PORTS descriptor.
 * Mach-send objects created from the array borrow the rights; the caller
 * keeps ownership.  ports may be NULL when the message carried none.
 */
xpc_object_t xpc_wire_deserialize_with_ports(const void *bytes, size_t len,
    const mach_port_t *ports, mach_msg_size_t nports);

#pragma mark - Description (xpc_description.c)

char *xpc_description_create(xpc_object_t object);

#pragma mark - Pipe layer (xpc_pipe.c)

typedef struct _xpc_pipe_s *xpc_pipe_t;

/* Public-ish pipe API (matches internal libxpc surface). */
xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags);
int xpc_pipe_simpleroutine(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply);
int xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply, uint32_t routine);
int xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t obj,
    xpc_object_t *reply, uint64_t flags, uint32_t routine);
int xpc_pipe_invalidate(xpc_pipe_t pipe);

/*
 * Same-task bridge for the launchd stub (launchd_stub.c).  A Mach reply
 * port's send-once right is invisible to the receiver when client and
 * server share one task (receive clobbers msgh_local_port with the
 * received-on port name), so routine requests to a local destination are
 * dispatched through this hook instead of mach_msg.  The wire round-trip
 * (serialize -> descriptor walk -> deserialize -> handle -> serialize
 * reply -> reply walk -> deserialize) is fully preserved; only the port
 * hop is skipped.  The handler receives the complete serialized request
 * message and returns a complete serialized reply message.
 */
typedef uint8_t *(*xpc_local_routine_handler_t)(const uint8_t *msg,
    size_t msg_len, uint32_t msgh_id, size_t *reply_len);
void xpc_pipe_set_local_handler(xpc_local_routine_handler_t handler);

/* Stash the sender's audit token onto a received dictionary. */
void xpc_dictionary_set_audit_token(xpc_object_t dict,
    const audit_token_t *token);

/*
 * msgh_id values (docs/WIRE_FORMAT.md §6, §11).  Confirmed against Apple's
 * __xpc_pipe_pack_message (libxpc.dylib): base ids are
 * 0x10000000 (simpleroutine) / 0x40000000 (routine); a reply id of
 * 0x20000000 is AND'd in when a reply port is present.  The routine path
 * ORs the routine number into the low 16 bits (real launchctl "list"
 * observes as 0x400000cf); Apple's classic xpc_pipe_routine sends the
 * bare base id, so the low bits are cosmetic for the server, which demuxes
 * on the dict's "subsystem"/"routine" keys.
 */
enum {
    XPC_PIPE_ID_SIMPLEROUTINE = 0x10000000,
    XPC_PIPE_ID_ROUTINE       = 0x40000000,
    XPC_PIPE_ID_REPLY         = 0x20000000,
};

#ifdef __cplusplus
}
#endif

#endif /* __XPC_INTERNAL_H__ */
