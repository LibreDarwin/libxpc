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
 *
 * Same-task resolver: arm64e refuses to mach_vm_map a task's *own*
 * memory entry (KERN_INVALID_NAME), which is exactly what the in-repo
 * launchd_stub's local-routine bridge does — the version-cmd client and
 * the PRINT handler share one task, so the entry it deserializes was
 * created in this very process.  For those entries the origin region
 * (recorded at xpc_shmem_create) IS the mapping: the pages are already
 * resident at that address.  xpc_shmem_map therefore resolves entries
 * whose origin we know directly, and only falls back to mach_vm_map for
 * genuinely foreign rights (the normal cross-process launchd case,
 * which probe9/probe10 exercise against real launchd).
 */

#include "xpc_internal.h"

#include <mach/mach_vm.h>

/* Port → origin-region table for locally-created entries.  Bounded and
 * intentionally simple: the only same-task callers are the stub's
 * single-threaded local-routine bridge. */
#define XPC_SHMEM_ORIGIN_MAX 8

struct xpc_shmem_origin {
    mach_port_t port;
    void *region;
};

static struct xpc_shmem_origin g_shmem_origins[XPC_SHMEM_ORIGIN_MAX];
static size_t g_shmem_origin_count;

static void
shmem_origin_add(mach_port_t port, void *region)
{
    if (g_shmem_origin_count >= XPC_SHMEM_ORIGIN_MAX) {
        return;
    }
    g_shmem_origins[g_shmem_origin_count].port = port;
    g_shmem_origins[g_shmem_origin_count].region = region;
    g_shmem_origin_count++;
}

static void
shmem_origin_remove(mach_port_t port)
{
    for (size_t i = 0; i < g_shmem_origin_count; i++) {
        if (g_shmem_origins[i].port == port) {
            g_shmem_origins[i] = g_shmem_origins[--g_shmem_origin_count];
            return;
        }
    }
}

static void *
shmem_origin_lookup(mach_port_t port)
{
    for (size_t i = 0; i < g_shmem_origin_count; i++) {
        if (g_shmem_origins[i].port == port) {
            return g_shmem_origins[i].region;
        }
    }
    return NULL;
}

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

/* Rights received from the wire: dispose on release.  If the right was
 * created in this same task (the stub's local-routine bridge), the
 * origin region is recorded instead: the entry's pages are already
 * mapped here, and the creating object stays the sole deallocator. */
xpc_object_t
xpc_shmem_create_owned(mach_port_t port, uint64_t size)
{
    void *origin = shmem_origin_lookup(port);
    if (origin) {
        xpc_object_t obj = xpc_shmem_create_internal(port, size, false);
        if (obj) {
            XPC_CAST(xpc_shmem_t, obj)->origin = origin;
        }
        return obj;
    }
    return xpc_shmem_create_owned_internal(port, size);
}

mach_port_t
xpc_shmem_get_port(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_shmem)) return MACH_PORT_NULL;
    return XPC_CAST(xpc_shmem_t, obj)->port;
}

/* Release path for XPC_KIND_SHMEM (xpc_object.c): deallocate the entry
 * right this object owns, and forget its origin record. */
void
xpc_shmem_dispose(xpc_shmem_t *s)
{
    if (s && s->dispose) {
        shmem_origin_remove(s->port);
        mach_port_deallocate(mach_task_self(), s->port);
    }
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
    xpc_object_t obj = xpc_shmem_create_internal(entry, (uint64_t)size, true);
    if (!obj) {
        mach_port_deallocate(mach_task_self(), entry);
        return NULL;
    }
    XPC_CAST(xpc_shmem_t, obj)->origin = region;
    shmem_origin_add(entry, region);
    return obj;
}

/* Public API (xpc_private.h): map the memory-entry right back into this
 * process.  Returns 0 and fills *region and *length on success.
 *
 * The mapped span is the entry's stored page-aligned size (set from the
 * audited mach_make_memory_entry_64 in/out parameter, or read from the
 * wire — never the caller's original, possibly-unrounded length).
 *
 * Entries created in this task (same-task bridge) resolve to their
 * origin region directly; arm64e refuses to re-map a task's own memory
 * entry, and the origin is the mapping. */
int
xpc_shmem_map(xpc_object_t obj, void **region, size_t *length)
{
    if (!region || !length) return KERN_INVALID_ARGUMENT;
    *region = NULL;
    *length = 0;
    xpc_shmem_t *s = XPC_CAST(xpc_shmem_t, obj);
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_shmem)) return KERN_INVALID_ARGUMENT;

    uint64_t size = s->size ? s->size : 0x1000;
    if (s->origin) {
        *region = s->origin;
        *length = (size_t)size;
        return KERN_SUCCESS;
    }

    mach_port_t port = s->port;
    if (!MACH_PORT_VALID(port)) return MACH_PORT_NULL;

    mach_vm_address_t addr = 0;
    kern_return_t kr = mach_vm_map(mach_task_self(), &addr, size, 0,
        VM_FLAGS_ANYWHERE, port, 0, false,
        VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) return kr;
    *region = (void *)(uintptr_t)addr;
    *length = size;
    return KERN_SUCCESS;
}