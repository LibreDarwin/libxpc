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
 * xpc_private.h — private launchd-facing surface of libxpc.
 *
 * This header models the runtime-private portion of Apple's libxpc that
 * launchctl and launchd communicate through: the per-process global data
 * block, the launchd "subsystem" encodings, and the xpc_domain_routine /
 * xpc_service_routine request wrappers.
 *
 * The layout of struct xpc_global_data is pinned with _Static_asserts so a
 * binary built against this library exposes the same OS_ALLOC_ONCE_KEY_LIBXPC
 * region layout that Apple's launchctl compatibility code expects
 * (offsets verified against macOS 26 ABI: launchctl.h).
 */

#ifndef __XPC_PRIVATE_H__
#define __XPC_PRIVATE_H__

#include <mach/mach.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xpc_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - launchd subsystem encodings

/*
 * The launchd XPC request protocol routes on two uint32 keys in the request
 * dictionary: "subsystem" selects the target (service vs. domain), "routine"
 * selects the operation.  (docs/LAUNCHCTL.md — routine tables.)
 */
enum xpc_launchd_subsystem {
    XPC_LAUNCHD_SUBSYSTEM_SERVICE = 2,
    XPC_LAUNCHD_SUBSYSTEM_DOMAIN  = 3,
};

/* Domain subsystem routines (xpc_domain_routine selector). */
enum xpc_launchd_domain_routine {
    /* Observed on the wire (real launchctl, live launchd, read-only cmds):
       id 0x400000cf, dict {handle, instance, flags, name, type, targetpid,
       domain-port}. launchctl emits this housekeeping probe before every
       command; not part of this implementation's dispatch. */
    XPC_ROUTINE_SERVICE_STATUS = 0xcf,
    XPC_ROUTINE_BOOTSTRAP   = 0x320, /* bootstrap a service/domain */
    XPC_ROUTINE_BOOTOUT     = 0x321, /* tear down a domain/service */
    XPC_ROUTINE_KILL        = 0x32c, /* signal a service instance */
    XPC_ROUTINE_LIST        = 0x32f, /* legacy list (services/one) */
    XPC_ROUTINE_SETENV      = 0x333, /* set/unset launchd env vars for
                                        domain (dict value is NULL for
                                        unset, string otherwise) */
    XPC_ROUTINE_GETENV      = 0x334, /* read launchd env for domain
                                        (0x336 is resolve-port) */
    XPC_ROUTINE_ENABLE      = 0x328, /* enable service by name */
    XPC_ROUTINE_DISABLE     = 0x329, /* disable service by name */
    XPC_ROUTINE_PRINT       = 0x33c, /* state dump via shared memory */
    XPC_ROUTINE_DUMPSTATE   = 0x342, /* crash-state dump via shmem */
    XPC_ROUTINE_ASUSER      = 0x343, /* resolve a user's bootstrap port */
    XPC_ROUTINE_BOOTSHELL   = 0x344, /* launchd shell attach */
    XPC_ROUTINE_QUERY       = 0x34a, /* specifier suggestion query */
};

/* Service subsystem routines (xpc_service_routine selector). */
enum xpc_launchd_service_routine {
    XPC_ROUTINE_SERVICE_KICKSTART = 0x2be, /* force start (-k kills) */
    XPC_ROUTINE_SERVICE_ATTACH    = 0x2bf, /* attach (debugger) */
    XPC_ROUTINE_SERVICE_BLAME     = 0x2c3, /* blame a service */
    XPC_ROUTINE_SERVICE_PRINT     = 0x2c4, /* per-service state dump */
    XPC_ROUTINE_SERVICE_EXISTS    = 0x2c8, /* exists probe */
};

#pragma mark - xpc_global_data

/*
 * Private per-process libxpc state, allocated once (the
 * OS_ALLOC_ONCE_KEY_LIBXPC region).  Only the fields used by launchd-facing
 * code are named; reserved bytes keep the pinned offset layout.
 */
struct xpc_global_data {
    bool is_launchd;
    bool is_xpcproxy;
    bool is_launchctl;
    uint8_t reserved_03;
    bool null_bootstrap;
    uint8_t reserved_05[3];
    uint64_t xpc_flags;
    mach_port_t task_bootstrap_port;
    uint32_t reserved_14;
    xpc_pipe_t xpc_bootstrap_pipe;
    void *attachment_endpoint;
    uint8_t reserved_28;
    union {
        struct {
            bool direct_fetch;
            bool use_reduced_exception_mask;
        };
        struct {
            uint8_t reserved_29_darwin20;
            bool direct_fetch_darwin20;
        };
    };
    uint8_t reserved_2b[0x30 - 0x2b];
    void *audit_token_once;
    audit_token_t audit_token;
    bool skip_reply_audit_check;
};

_Static_assert(offsetof(struct xpc_global_data, is_launchd) == 0x00,
    "xpc global is_launchd offset");
_Static_assert(offsetof(struct xpc_global_data, null_bootstrap) == 0x04,
    "xpc global null_bootstrap offset");
_Static_assert(offsetof(struct xpc_global_data, xpc_flags) == 0x08,
    "xpc_flags offset");
_Static_assert(offsetof(struct xpc_global_data, task_bootstrap_port) == 0x10,
    "task_bootstrap_port offset");
_Static_assert(offsetof(struct xpc_global_data, xpc_bootstrap_pipe) == 0x18,
    "xpc_bootstrap_pipe offset");
_Static_assert(offsetof(struct xpc_global_data, attachment_endpoint) == 0x20,
    "attachment_endpoint offset");
_Static_assert(offsetof(struct xpc_global_data, direct_fetch) == 0x29,
    "direct_fetch offset");
_Static_assert(offsetof(struct xpc_global_data,
    use_reduced_exception_mask) == 0x2a, "use_reduced_exception_mask offset");
_Static_assert(offsetof(struct xpc_global_data,
    direct_fetch_darwin20) == 0x2a, "direct_fetch_darwin20 offset");
_Static_assert(offsetof(struct xpc_global_data, audit_token_once) == 0x30,
    "audit_token_once offset");
_Static_assert(offsetof(struct xpc_global_data, audit_token) == 0x38,
    "audit_token offset");
_Static_assert(offsetof(struct xpc_global_data,
    skip_reply_audit_check) == 0x58, "skip_reply_audit_check offset");

/*
 * Fetch the once-initialized global data block.  First call creates the
 * launchd bootstrap pipe (host_get_launchctl_port).
 */
struct xpc_global_data *xpc_global_data(void);

/* Whether the bootstrap pipe is usable for launchd routines. */
bool xpc_global_data_has_bootstrap_pipe(void);

#pragma mark - launchd routine API

/*
 * Routed launchd request.  Sets the "subsystem"/"routine" keys, optionally
 * "pre-exec", sends as a 0x40000000 routine to the bootstrap pipe and verifies
 * the reply came from PID 1 / euid 0 (unless skip_reply_audit_check).
 * Returns 0 on success with *reply set, else an xpc_strerror()-decodeable
 * code (errno range or XPC_LAUNCHD_ERROR_*).
 */
int xpc_pipe_routine_checked(uint64_t subsystem, uint64_t routine,
    xpc_object_t request, xpc_object_t *reply, bool check_origin,
    uint64_t flags);

int xpc_domain_routine(uint64_t routine, xpc_object_t request,
    xpc_object_t *reply);
int xpc_service_routine(uint64_t routine, xpc_object_t request,
    xpc_object_t *reply);

/* Pipe variant with an explicit flag word (passthrough to mach_msg opts).
 * `routine` is OR'd into the msgh_id low 16 bits alongside
 * XPC_PIPE_ID_ROUTINE, matching real launchd wire ids (e.g. 0x400000cf). */
int xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t request,
    xpc_object_t *reply, uint64_t flags, uint32_t routine);

/* Human-readable description of a routine error code. */
const char *xpc_strerror(int error);

/* launchd-family error codes outside the errno range. */
enum {
    XPC_LAUNCHD_ERROR_INVALID_PATH        = 108,
    XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND    = 112,
    XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND   = 113,
    XPC_LAUNCHD_ERROR_BAD_RESPONSE        = 118,
    XPC_LAUNCHD_ERROR_REQUEST_UNSUPPORTED = 126,
    XPC_LAUNCHD_ERROR_REQUESTS_OUT_OF_ORDER = 131,
    XPC_LAUNCHD_ERROR_REENTRANCY_AVOIDED  = 141,
    XPC_LAUNCHD_ERROR_DEVELOPMENT_BUILD_REQUIRED = 142,
    XPC_LAUNCHD_ERROR_MISSING_ENTITLEMENT = 144,
    XPC_LAUNCHD_ERROR_SIP_PROTECTED       = 150,
    XPC_LAUNCHD_ERROR_UNEXPECTED          = 153,
    XPC_LAUNCHD_ERROR_DOMAIN_UID_PROHIBITED = 156,
};

#pragma mark - shared-memory objects (launchd state dumps)

/*
 * Shmem objects carry a writable shared region..  Requires the wire-layer
 * descriptor support (docs/WIRE_FORMAT.md — OOL memory regions).
 * xpc_shmem_create maps *region as a memory entry; xpc_shmem_map attaches a
 * received shmem object in this process.
 */
xpc_object_t xpc_shmem_create(void *region, size_t length);
int xpc_shmem_map(xpc_object_t shmem, void **region, size_t *length);

/* Receive-side attachment of reply senders' audit tokens. */
int xpc_dictionary_get_audit_token(xpc_object_t dict, audit_token_t *token);

#ifdef __cplusplus
}
#endif

#endif /* __XPC_PRIVATE_H__ */