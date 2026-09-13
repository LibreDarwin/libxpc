/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * probe9.c — version routine (`PRINT` 0x33c) through OUR libxpc with a
 * genuine shared-memory value, against the live bootstrap port.
 *
* This is §11.3 of docs/WIRE_FORMAT.md re-run with this tree's own
 * serializer: the request maps a vm region as a Mach memory entry
 * (wire kind 0xc000), launchd maps that entry and writes its version
 * string back into the region, and the reply is the plain dict
 * {"bytes-written": N}.
 *
 *  Optional argv[1] sets the region size (default 0x1000).  A two-page
 *  region (0x8000) validates that the wire's 8-byte shmem size field
 *  carries the entry's real page-aligned span, not a constant.
 *
 *  Build: cc -o probe9 probe9.c -I../.. -L../../build/release -lsystem_xpc \
 *             -Wl,-rpath,../../build/release
 *  Run:   DYLD_INSERT_LIBRARIES=tools/probe/interpose.dylib ./probe9 [size]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <mach/mach.h>
#include <xpc_internal.h>
#include <xpc_private.h>

int
main(int argc, char **argv)
{
    mach_port_t bp = MACH_PORT_NULL;
    kern_return_t kr = task_get_bootstrap_port(mach_task_self(), &bp);
    fprintf(stderr, "bootstrap port: kr=0x%x port=0x%x\n", kr, bp);
    if (kr != KERN_SUCCESS || !MACH_PORT_VALID(bp)) return 1;

    /* Version region: launchd maps the memory entry and writes into it. */
    vm_size_t region_len = 0x1000;
    if (argc >= 2) {
        region_len = (vm_size_t)strtoul(argv[1], NULL, 0);
        if (region_len == 0) region_len = 0x1000;
    }
    vm_address_t region = 0;
    kr = vm_allocate(mach_task_self(), &region, region_len, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "vm_allocate: 0x%x\n", kr);
        return 1;
    }
    memset((void *)region, 0, region_len);

    xpc_object_t shmem = xpc_shmem_create((void *)region, region_len);
    if (!shmem) {
        fprintf(stderr, "xpc_shmem_create failed\n");
        return 1;
    }
    fprintf(stderr, "shmem entry port=0x%x region_len=0x%zx\n",
        xpc_shmem_get_port(shmem), (size_t)region_len);

    xpc_object_t req = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(req, "handle", 0);
    xpc_dictionary_set_value(req, "shmem", shmem);
    xpc_dictionary_set_uint64(req, "type", 1);
    xpc_dictionary_set_bool(req, "version", true);

    xpc_pipe_t pipe = xpc_pipe_create_from_port(bp, 0);
    if (!pipe) {
        fprintf(stderr, "pipe create failed\n");
        return 1;
    }

    alarm(5);
    xpc_object_t reply = NULL;
    int rc = xpc_pipe_routine(pipe, req, &reply, XPC_ROUTINE_PRINT);
    alarm(0);

    fprintf(stderr, "routine rc=%d\n", rc);
    if (reply) {
        char *desc = xpc_copy_description(reply);
        fprintf(stderr, "REPLY: %s\n", desc ? desc : "(nil)");
        free(desc);
        xpc_release(reply);
    } else {
        fprintf(stderr, "reply=nil\n");
    }

    const char *version = (const char *)region;
    fprintf(stderr, "region: %s\n",
        version[0] ? version : "(empty)");
    bool ok = (rc == 0) && version[0];

    xpc_release(req);
    xpc_release(shmem);
    vm_deallocate(mach_task_self(), region, region_len);
    return ok ? 0 : 1;
}