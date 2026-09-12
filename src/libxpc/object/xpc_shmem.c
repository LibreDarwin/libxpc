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
 * xpc_shmem.c — shared-memory values for the reimplemented XPC framework.
 *
 * Mirrors Apple's xpc_shmem_create(3): the region becomes a Mach memory
 * entry (a send right to a memory-object port), which travels inside a
 * dictionary as a 0xc000 wire value and lets launchd map it and write
 * into it (the `version` routine is the canonical user).  Like
 * xpc_mach_send, xpc_shmem_create borrows the right and the caller keeps
 * ownership of the region; rights minted by the wire
 * (xpc_shmem_create_owned) deallocate on release.
 */

#include "xpc_internal.h"

#include <mach/mach_vm.h>

static xpc_object_t
xpc_shmem_create_internal(mach_port_t port, uint64_t size, bool dispose)
{
    xpc_shmem_t *s = XPC_CAST(xpc_shmem_t,
        xpc_object_alloc(&_xpc_type_shmem, sizeof(xpc_shmem_t)));
    if (!s) return NULL;
    s->port = port;
    s->size = size;
    s->dispose = dispose;
    return (xpc_object_t)s;
}

static xpc_object_t
xpc_shmem_create_owned_internal(mach_port_t port, uint64_t size)
{
    return xpc_shmem_create_internal(port, size, true);
}

/* Rights received from the wire: dispose on release. */
xpc_object_t
xpc_shmem_create_owned(mach_port_t port, uint64_t size)
{
    return xpc_shmem_create_owned_internal(port, size);
}

mach_port_t
xpc_shmem_get_port(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_shmem)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_shmem_t, obj)->port;
}

/* Public API (xpc_private.h): map *region of *length bytes as a memory
 * entry and wrap the resulting send right.  Return NULL if the region or
 * entry cannot be created. */
xpc_object_t
xpc_shmem_create(void *region, size_t length)
{
    if (!region || length == 0) return NULL;
    mach_port_t entry = MACH_PORT_NULL;
    /* Audited MIG: size is in/out (clamped up to a page on return). */
    memory_object_size_t size = (memory_object_size_t)length;
    kern_return_t kr = mach_make_memory_entry_64(mach_task_self(), &size,
        (memory_object_offset_t)(uintptr_t)region,
        VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS || !MACH_PORT_VALID(entry)) return NULL;
    /* size (in/out) holds the entry's clamped, page-aligned span. */
    return xpc_shmem_create_internal(entry, (uint64_t)size, true);
}

/* Public API (xpc_private.h): map the memory-entry right back into this
 * process.  Returns 0 and fills *region and *length on success. */
int
xpc_shmem_map(xpc_object_t obj, void **region, size_t *length)
{
    if (!region || !length) return KERN_INVALID_ARGUMENT;
    *region = NULL;
    *length = 0;
    mach_port_t port = xpc_shmem_get_port(obj);
    if (!MACH_PORT_VALID(port)) return MACH_PORT_NULL;

    vm_size_t size = 0x1000;
    mach_vm_address_t addr = 0;
    kern_return_t kr = mach_vm_map(mach_task_self(), &addr, size, 0,
        VM_FLAGS_ANYWHERE, port, 0, false,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) return kr;
    /* The entry's usable span may exceed the first page; ask the region
     * API for its true extent (mach_vm_size does not exist here). */
    mach_vm_size_t region_len = 0;
    struct vm_region_basic_info_64 info;
    mach_msg_type_number_t info_cnt = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name = MACH_PORT_NULL;
    kr = mach_vm_region(mach_task_self(), &addr, &region_len,
        VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &info_cnt,
        &object_name);
    if (kr != KERN_SUCCESS) {
        mach_vm_deallocate(mach_task_self(), addr, size);
        return kr;
    }
    *region = (void *)(uintptr_t)addr;
    *length = region_len;
    return KERN_SUCCESS;
}