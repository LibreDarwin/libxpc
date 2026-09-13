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
 * xpc_routines.c — launchd-facing routine gateway.
 *
 * Implements the private libxpc surface that launchctl talks to launchd
 * through: the once-initialized process global data block, the launchd
 * bootstrap pipe (host_get_launchctl_port), xpc_pipe_routine_checked with its
 * "subsystem"/"routine" request routing and reply origin verification, and
 * the xpc_domain_routine / xpc_service_routine wrappers.  Wire behavior is
 * byte-identical to Apple's implementation (docs/LAUNCHCTL.md).
 *
 * The audit token of a routine reply is captured from the mach receive
 * trailer (the reply must originate from PID 1, euid 0 — i.e. launchd).
 */

#include "xpc_internal.h"
#include "xpc_private.h"

#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * audit_token_to_*() are private libSystem SPI (declared in <bsm/libbsm.h>
 * but not exported by the public SDK), so read the canonical 8-word layout
 * directly (verified against task_info(TASK_AUDIT_TOKEN) and launchd's
 * trailer): auid -> val[0], euid -> val[1], egid -> val[2], ruid -> val[3],
 * rgid -> val[4], pid -> val[5], pidversion -> val[6], asid -> val[7].
 */
static inline uid_t
xpc_audit_token_euid(audit_token_t token)
{
    return (uid_t)token.val[1];
}

static inline pid_t
xpc_audit_token_pid(audit_token_t token)
{
    return (pid_t)token.val[5];
}

static inline pid_t
xpc_audit_token_pidversion(audit_token_t token)
{
    return (pid_t)token.val[6];
}

#pragma mark - global data

static pthread_once_t xpc_global_once = PTHREAD_ONCE_INIT;
static struct xpc_global_data xpc_global_memory;

static void
xpc_global_initializer(void)
{
    struct xpc_global_data *state = &xpc_global_memory;

    state->is_launchd = (getpid() == 1);
    state->task_bootstrap_port = MACH_PORT_NULL;
    task_get_bootstrap_port(mach_task_self(), &state->task_bootstrap_port);

    /*
     * The XPC bootstrap pipe wraps the task's bootstrap port — every
     * process's bootstrap port resolves to launchd, so routine messages
     * sent here reach launchd's xpc dispatcher.  (host_get_launchctl_port
     * is not wired up on modern macOS and returns KERN_FAILURE.)
     */
    if (MACH_PORT_VALID(state->task_bootstrap_port)) {
        state->xpc_bootstrap_pipe =
            xpc_pipe_create_from_port(state->task_bootstrap_port, 0);
    }
    /*
     * Test-harness override (tools/probe + src/launchd/launchd_stub):
     * XNUXPORTS_LAUNCHD_PORT names a local Mach port that stands in for
     * launchd.  The stub is not PID 1/euid 0, so reply-origin
     * verification must be skipped while the override is active.
     */
    {
        const char *stub_port = getenv("XNUXPORTS_LAUNCHD_PORT");
        if (stub_port && *stub_port) {
            unsigned long port = strtoul(stub_port, NULL, 10);
            if (port && port != MACH_PORT_NULL) {
                if (state->xpc_bootstrap_pipe) {
                    xpc_pipe_invalidate(state->xpc_bootstrap_pipe);
                }
                state->xpc_bootstrap_pipe =
                    xpc_pipe_create_from_port((mach_port_t)port, 0);
                state->skip_reply_audit_check = true;
            }
        }
    }
    if (!state->xpc_bootstrap_pipe) {
        state->null_bootstrap = true;
    }
    if (getenv("XPC_DEBUG")) {
        const char *stub_port = getenv("XNUXPORTS_LAUNCHD_PORT");
        mach_port_t pipe_port = MACH_PORT_NULL;
        if (state->xpc_bootstrap_pipe) {
            struct _xpc_pipe_s { mach_port_t port; uint64_t flags;
                bool valid; } *p =
                (struct _xpc_pipe_s *)(void *)state->xpc_bootstrap_pipe;
            pipe_port = p->port;
        }
        fprintf(stderr,
            "[xpc] bootstrap=%u pipe=%p pipe_port=%u env=%s null=%d "
            "launchd=%d\n",
            state->task_bootstrap_port, state->xpc_bootstrap_pipe, pipe_port,
            stub_port ? stub_port : "(none)", state->null_bootstrap,
            state->is_launchd);
    }
}

struct xpc_global_data *
xpc_global_data(void)
{
    pthread_once(&xpc_global_once, xpc_global_initializer);
    return &xpc_global_memory;
}

bool
xpc_global_data_has_bootstrap_pipe(void)
{
    return xpc_global_data()->xpc_bootstrap_pipe != NULL;
}

#pragma mark - audit token plumbing (dictionaries)

void
xpc_dictionary_set_audit_token(xpc_object_t dict, const audit_token_t *token)
{
    xpc_dictionary_t *d;
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary)) {
        return;
    }
    d = XPC_CAST(xpc_dictionary_t, dict);
    d->audit_token = *token;
    d->has_audit_token = true;
}

int
xpc_dictionary_get_audit_token(xpc_object_t dict, audit_token_t *token)
{
    xpc_dictionary_t *d;
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary) || !token) {
        return KERN_INVALID_ARGUMENT;
    }
    d = XPC_CAST(xpc_dictionary_t, dict);
    if (!d->has_audit_token) {
        return ENOENT;
    }
    *token = d->audit_token;
    return KERN_SUCCESS;
}

#pragma mark - routine plumbing

int
xpc_pipe_routine_with_flags(xpc_pipe_t pipe, xpc_object_t request,
    xpc_object_t *reply, uint64_t flags, uint32_t routine)
{
    (void)flags;
    return xpc_pipe_routine(pipe, request, reply, routine);
}

int
xpc_pipe_routine_checked(uint64_t subsystem, uint64_t routine,
    xpc_object_t request, xpc_object_t *reply, bool check_origin,
    uint64_t flags)
{
    struct xpc_global_data *state;
    xpc_object_t response;
    audit_token_t token;
    audit_token_t self_token;
    task_t self_task;
    mach_msg_type_number_t count;
    uint64_t req_pid;
    uint64_t rec_execcnt;
    pid_t self_pid;
    int self_pidversion;
    pid_t actual_pid;
    uid_t actual_euid;
    int result;

    if (reply) {
        *reply = NULL;
    }
    state = xpc_global_data();
    if (state->is_launchd || state->null_bootstrap) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[xpc] reentrancy guard: launchd=%d null=%d\n",
                state->is_launchd, state->null_bootstrap);
        }
        return XPC_LAUNCHD_ERROR_REENTRANCY_AVOIDED;
    }
    if (!state->direct_fetch) {
        xpc_dictionary_set_bool(request, "pre-exec", true);
    }

    /* launchd's xpc dispatcher reads these two keys. */
    xpc_dictionary_set_uint64(request, "subsystem", (uint32_t)subsystem);
    xpc_dictionary_set_uint64(request, "routine", (uint32_t)routine);

    if (getenv("XPC_DEBUG")) {
        char *req_desc = xpc_copy_description(request);
        fprintf(stderr, "[xpc] routine req: %s\n", req_desc);
        free(req_desc);
    }

    response = NULL;
    result = xpc_pipe_routine_with_flags(state->xpc_bootstrap_pipe,
        request, &response, flags, (uint32_t)routine);
    if (!result) {
        result = (int)xpc_dictionary_get_int64(response, "error");
        if (getenv("XPC_DEBUG")) {
            char *r = xpc_copy_description(response);
            fprintf(stderr, "[xpc] routine reply: %s\n", r);
            free(r);
        }
    }
    if (result) {
        /* Deliver the reply alongside the error: launchd replies carry
         * payload with error (e.g. kickstart returns the running pid with
         * EALREADY), and callers dispose of the object themselves. */
        if (reply) {
            *reply = response;
        } else if (response) {
            xpc_release(response);
        }
        return result;
    }

    state = xpc_global_data();
    if (!state->skip_reply_audit_check &&
        xpc_dictionary_get_audit_token(response, &token) != KERN_SUCCESS) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[xpc] audit: no trailer token on reply\n");
        }
        /* No trailer token: treat as unverifiable (defensive). */
        xpc_release(response);
        return XPC_LAUNCHD_ERROR_BAD_RESPONSE;
    }
    if (!state->skip_reply_audit_check) {
        actual_pid = xpc_audit_token_pid(token);
        actual_euid = xpc_audit_token_euid(token);
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[xpc] audit: reply pid=%d euid=%d tok=[%u %u %u %u %u %u %u %u]\n",
                actual_pid, actual_euid,
                token.val[0], token.val[1], token.val[2], token.val[3],
                token.val[4], token.val[5], token.val[6], token.val[7]);
        }
        if (actual_pid != 1 || actual_euid != 0) {
            xpc_release(response);
            return XPC_LAUNCHD_ERROR_BAD_RESPONSE;
        }
        if (check_origin) {
            req_pid = xpc_dictionary_get_uint64(response, "req_pid");
            rec_execcnt = xpc_dictionary_get_uint64(response, "rec_execcnt");
            if ((req_pid >> 31) || (rec_execcnt >> 31)) {
                xpc_release(response);
                return XPC_LAUNCHD_ERROR_BAD_RESPONSE;
            }
            self_task = mach_task_self();
            count = TASK_AUDIT_TOKEN_COUNT;
            if (task_info(self_task, TASK_AUDIT_TOKEN,
                (task_info_t)&self_token, &count) != KERN_SUCCESS) {
                xpc_release(response);
                return XPC_LAUNCHD_ERROR_BAD_RESPONSE;
            }
            self_pid = xpc_audit_token_pid(self_token);
            self_pidversion = xpc_audit_token_pidversion(self_token);
            if ((uint32_t)self_pid != (uint32_t)req_pid ||
                (uint32_t)self_pidversion != (uint32_t)rec_execcnt) {
                xpc_release(response);
                return XPC_LAUNCHD_ERROR_BAD_RESPONSE;
            }
        }
    }
    if (reply) {
        *reply = response;
    } else {
        xpc_release(response);
    }
    return 0;
}

int
xpc_domain_routine(uint64_t routine, xpc_object_t request, xpc_object_t *reply)
{
    return xpc_pipe_routine_checked(XPC_LAUNCHD_SUBSYSTEM_DOMAIN, routine,
        request, reply, false, 0);
}

int
xpc_service_routine(uint64_t routine, xpc_object_t request, xpc_object_t *reply)
{
    return xpc_pipe_routine_checked(XPC_LAUNCHD_SUBSYSTEM_SERVICE, routine,
        request, reply, false, 0);
}

#pragma mark - error descriptions

const char *
xpc_strerror(int error)
{
    switch (error) {
    case XPC_LAUNCHD_ERROR_INVALID_PATH:
        return "Invalid path.";
    case XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND:
        return "The specified domain does not exist.";
    case XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND:
        return "The specified service does not exist.";
    case XPC_LAUNCHD_ERROR_BAD_RESPONSE:
        return "Bad response from launchd.";
    case XPC_LAUNCHD_ERROR_REQUEST_UNSUPPORTED:
        return "The request is not supported.";
    case XPC_LAUNCHD_ERROR_REQUESTS_OUT_OF_ORDER:
        return "The requests were made out of order.";
    case XPC_LAUNCHD_ERROR_REENTRANCY_AVOIDED:
        return "Re-entrant launchd routine call avoided.";
    case XPC_LAUNCHD_ERROR_DEVELOPMENT_BUILD_REQUIRED:
        return "This operation is only available in the development build.";
    case XPC_LAUNCHD_ERROR_MISSING_ENTITLEMENT:
        return "The process is missing a required entitlement.";
    case XPC_LAUNCHD_ERROR_SIP_PROTECTED:
        return "The operation is protected by System Integrity Protection.";
    case XPC_LAUNCHD_ERROR_UNEXPECTED:
        return "Unexpected launchd error.";
    case XPC_LAUNCHD_ERROR_DOMAIN_UID_PROHIBITED:
        return "The specified uid may not be used for a domain.";
    default:
        if (error > 0 && error < 107) {
            return strerror(error);
        }
        return "Unknown error.";
    }
}