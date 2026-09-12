/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * probe10.c — debug harness: version PRINT round-trip against the stub,
 * with every step printed.  Mirrors launchctl version_cmd.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <xpc_private.h>

int
main(void)
{
    struct xpc_global_data *state = xpc_global_data();
    struct _xpc_pipe_s {
        mach_port_t port; uint64_t flags; bool valid;
    } *p = (struct _xpc_pipe_s *)(void *)state->xpc_bootstrap_pipe;
    fprintf(stderr, "pipe=%p port=%u valid=%d\n", (void *)state->xpc_bootstrap_pipe,
        p ? p->port : 0, p ? p->valid : -1);

    vm_address_t region = 0;
    vm_size_t region_len = 0x1000;
    kern_return_t kr = vm_allocate(mach_task_self(), &region, region_len,
        VM_FLAGS_ANYWHERE);
    fprintf(stderr, "vm_allocate kr=0x%x region=0x%llx\n", kr,
        (unsigned long long)region);
    memset((void *)region, 0, region_len);

    xpc_object_t shmem = xpc_shmem_create((void *)region, region_len);
    fprintf(stderr, "shmem=%p\n", (void *)shmem);
    if (shmem) {
        mach_port_t eport = xpc_shmem_get_port(shmem);
        fprintf(stderr, "entry port=0x%x\n", eport);
        mach_port_type_t t = 0;
        kr = mach_port_type(mach_task_self(), eport, &t);
        fprintf(stderr, "entry port type kr=0x%x type=0x%x\n", kr, t);
        /* Self-map sanity: map the entry right back in this process. */
        void *mapped = NULL;
        size_t mapped_len = 0;
        int mkr = xpc_shmem_map(shmem, &mapped, &mapped_len);
        fprintf(stderr, "self-map kr=0x%x region=%p len=%zu\n", mkr,
            mapped, mapped_len);
        if (mkr == 0 && mapped) {
            memcpy(mapped, "self-map-ok", 11);
            fprintf(stderr, "region now: %s\n", (char *)region);
            mach_vm_deallocate(mach_task_self(),
                (mach_vm_address_t)(uintptr_t)mapped, mapped_len);
        }
    }

    xpc_object_t request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(request, "handle", 0);
    xpc_dictionary_set_value(request, "shmem", shmem);
    xpc_dictionary_set_uint64(request, "type", 1);
    xpc_dictionary_set_bool(request, "version", true);

    xpc_object_t reply = NULL;
    int rc = xpc_pipe_routine(state->xpc_bootstrap_pipe, request, &reply,
        XPC_ROUTINE_PRINT);
    fprintf(stderr, "routine rc=%d reply=%p\n", rc, (void *)reply);
    if (reply) {
        char *desc = xpc_copy_description(reply);
        fprintf(stderr, "REPLY: %s\n", desc ? desc : "(nil)");
        free(desc);
    }
    const char *version = (const char *)region;
    fprintf(stderr, "region[0]=0x%02x region: %s\n",
        (unsigned char)version[0], version[0] ? version : "(empty)");

    (void)kr;
    return 0;
}