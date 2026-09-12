/*
 * maptest.c — raw mach: make memory entry + same-task mach_vm_map.
 * Isolates whether xpc_shmem_map's failure is our code or kernel behavior.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    vm_address_t region = 0;
    vm_size_t size = 0x1000;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &region, size,
        VM_FLAGS_ANYWHERE);
    printf("alloc kr=%x region=%llx\n", kr, (unsigned long long)region);
    memset((void *)region, 0xAB, size);

    memory_object_size_t mo_size = size;
    mach_port_t entry = MACH_PORT_NULL;
    kr = mach_make_memory_entry_64(mach_task_self(), &mo_size, region,
        VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    printf("make kr=%x entry=%x mo_size=%llx\n", kr, entry,
        (unsigned long long)mo_size);

    mach_port_type_t t = 0;
    kr = mach_port_type(mach_task_self(), entry, &t);
    printf("type kr=%x type=%x\n", kr, t);

    /* Try mach_vm_map with returned mo_size (page-clamped) */
    mach_vm_address_t addr = 0;
    kr = mach_vm_map(mach_task_self(), &addr, mo_size, 0, VM_FLAGS_ANYWHERE,
        entry, 0, false, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL,
        VM_INHERIT_NONE);
    printf("map-mo kr=%x addr=%llx\n", kr, (unsigned long long)addr);

    /* Try mach_vm_map with returned mo_size + copy=TRUE */
    addr = 0;
    kr = mach_vm_map(mach_task_self(), &addr, mo_size, 0, VM_FLAGS_ANYWHERE,
        entry, 0, true, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL,
        VM_INHERIT_NONE);
    printf("map-mo-copy kr=%x addr=%llx\n", kr, (unsigned long long)addr);

    /* Try mach_vm_remap from same task - maps pages from source_addr */
    addr = 0;
    vm_prot_t cur_prot = VM_PROT_NONE, max_prot = VM_PROT_NONE;
    kr = mach_vm_remap(mach_task_self(), &addr, size, 0, VM_FLAGS_ANYWHERE,
        mach_task_self(), region, true, &cur_prot, &max_prot, VM_INHERIT_NONE);
    printf("remap kr=%x addr=%llx cur=%x max=%x\n", kr, (unsigned long long)addr, cur_prot, max_prot);

    /* Try vm_map (legacy) */
    {
        vm_map_t task = mach_task_self();
        vm_address_t vaddr = 0;
        kr = vm_map(task, &vaddr, size, 0, VM_FLAGS_ANYWHERE, entry, 0, false,
            VM_PROT_READ | VM_PROT_WRITE, VM_PROT_ALL, VM_INHERIT_NONE);
        printf("vm_map kr=%x addr=%llx\n", kr, (unsigned long long)vaddr);
    }
    return 0;
}