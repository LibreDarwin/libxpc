/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * probe7.c -- isolate which request key/encoding makes launchd destroy
 * our send-once reply right (0x47 notification instead of a real reply).
 *
 * Drives OUR libsystem_xpc.dylib against the live bootstrap port with variant
 * legacy-LIST dicts.  A variant that returns a dict is accepted; one that
 * errors 4 (KERN_INVALID_ARGUMENT from our bad-reply-id path) or blocks
 * is the offender.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <mach/mach.h>
#include <xpc_internal.h>
#include <xpc_private.h>

static xpc_pipe_t g_pipe;

static int
send_raw(xpc_object_t req, xpc_object_t *reply)
{
    return xpc_pipe_routine(g_pipe, req, reply, XPC_ROUTINE_LIST);
}

static int
send_checked(xpc_object_t req, xpc_object_t *reply)
{
    return xpc_pipe_routine_checked(3, XPC_ROUTINE_LIST, req, reply, false, 0);
}

static void
run(const char *label, int (*fn)(xpc_object_t, xpc_object_t *), xpc_object_t req)
{
    xpc_object_t reply = NULL;
    alarm(5);
    int rc = fn(req, &reply);
    alarm(0);
    char *desc = xpc_copy_description(req);
    fprintf(stderr, "=== %s\n    req: %s\n", label, desc);
    if (desc) free(desc);
    if (rc) {
        fprintf(stderr, "    error=%d\n", rc);
    } else if (reply) {
        desc = xpc_copy_description(reply);
        fprintf(stderr, "    REPLY: %s\n", desc ? desc : "(nil)");
        if (desc) free(desc);
        xpc_release(reply);
    } else {
        fprintf(stderr, "    reply=nil\n");
    }
    xpc_release(req);
}

static xpc_object_t
mkreq(uint64_t type, uint64_t handle, bool legacy, bool domain_port, bool name)
{
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(d, "type", type);
    xpc_dictionary_set_uint64(d, "handle", handle);
    if (legacy) xpc_dictionary_set_bool(d, "legacy", true);
    if (domain_port) {
        mach_port_t bp = MACH_PORT_NULL;
        task_get_bootstrap_port(mach_task_self(), &bp);
        xpc_dictionary_set_mach_send(d, "domain-port", bp);
    }
    if (name) xpc_dictionary_set_string(d, "name", "gui/501");
    return d;
}

int
main(void)
{
    mach_port_t bp = MACH_PORT_NULL;
    kern_return_t kr = task_get_bootstrap_port(mach_task_self(), &bp);
    fprintf(stderr, "bootstrap port: kr=0x%x port=0x%x\n", kr, bp);
    if (kr != KERN_SUCCESS || !MACH_PORT_VALID(bp)) return 1;

    g_pipe = xpc_pipe_create_from_port(bp, 0);
    if (!g_pipe) { fprintf(stderr, "pipe create failed\n"); return 1; }

    /* A: the doc's proven-minimal system-domain list, raw pipe. */
    run("A: raw {type:1, handle:0}", send_raw, mkreq(1, 0, 0, 0, 0));

    /* B: legacy port-domain list, raw pipe, no domain-port. */
    run("B: raw {type:7, handle:0, legacy}", send_raw, mkreq(7, 0, 1, 0, 0));

    /* C: B + domain-port mach-send (our OOL_PORTS encoding). */
    run("C: raw {type:7, handle:0, legacy, domain-port}",
        send_raw, mkreq(7, 0, 1, 1, 0));

    /* D: checked wrapper (injects pre-exec/subsystem/routine). */
    run("D: checked {type:7, handle:0, legacy}", send_checked,
        mkreq(7, 0, 1, 0, 0));

    /* E: full launchctl shape: checked + domain-port. */
    run("E: checked {type:7, handle:0, legacy, domain-port}", send_checked,
        mkreq(7, 0, 1, 1, 0));

    /* F: gui target through raw pipe. */
    run("F: raw {type:8, handle:501, legacy}", send_raw,
        mkreq(8, 501, 1, 0, 0));

    /* G: gui target + name (single-service lookup). */
    run("G: raw {type:8, handle:501, legacy, name}", send_raw,
        mkreq(8, 501, 1, 0, 1));

    return 0;
}