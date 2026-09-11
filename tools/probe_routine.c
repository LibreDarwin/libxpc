/*
 * probe_routine.c — wire-format ground truth probe.
 *
 * Calls the REAL system libxpc private entry points via dlsym and prints the
 * request/reply dictionaries, so byte-identical behavior can be validated
 * against Apple's implementation. Non-destructive: query-only routines.
 *
 * Copyright (c) 2026, xnuports. SPDX-License-Identifier: BSD-2-Clause.
 */
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xpc/xpc.h>

typedef int (*domain_routine_fn)(uint64_t, xpc_object_t, xpc_object_t *);

/*
 * The SDK ships no libxpc.tbd, so the binary cannot link against the real
 * library. Pull it from the dyld shared cache at runtime instead; all
 * xpc_* references resolve via the -undefined,dynamic_lookup link option.
 */
static void *
load_real_libxpc(void)
{
	void *h = dlopen("libxpc.dylib", RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		h = dlopen("/usr/lib/system/libxpc.dylib",
		    RTLD_NOW | RTLD_GLOBAL);
	}
	if (!h) {
		fprintf(stderr, "dlopen libxpc: %s\n", dlerror());
	}
	return h;
}

int
main(int argc, char **argv)
{
	domain_routine_fn domain_routine;
	xpc_object_t request;
	xpc_object_t reply;
	const char *routine_hex;
	uint64_t routine;
	int rc;

	if (!load_real_libxpc()) {
		return 1;
	}

	if (argc < 2) {
		fprintf(stderr, "usage: %s <routine-hex> [type] [handle] [name]\n",
		    argv[0]);
		return 2;
	}
	routine_hex = argv[1];
	routine = strtoull(routine_hex, NULL, 16);

	domain_routine = (domain_routine_fn)dlsym(RTLD_DEFAULT,
	    "_xpc_domain_routine");
	if (!domain_routine) {
		perror("dlsym _xpc_domain_routine");
		return 1;
	}

	request = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_uint64(request, "type",
	    argc > 2 ? strtoull(argv[2], NULL, 10) : 1);
	xpc_dictionary_set_uint64(request, "handle",
	    argc > 3 ? strtoull(argv[3], NULL, 10) : 0);
	if (argc > 4) {
		xpc_dictionary_set_string(request, "name", argv[4]);
	}

	/*
	 * The version routine (0x33c) requires a shared-memory region:
	 * launchd writes the reply into it. Mirror the real launchctl
	 * request shape: {handle, shmem, type, version}. Map the region
	 * after the call to read back what launchd wrote.
	 */
	xpc_object_t shmem = NULL;
	void *region = NULL;
	if (routine == 0x33c) {
		region = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (region != MAP_FAILED) {
			memset(region, 0, 0x1000);
			shmem = xpc_shmem_create(region, 0x1000);
			xpc_dictionary_set_value(request, "shmem", shmem);
			xpc_dictionary_set_bool(request, "version", true);
		}
	}

	reply = NULL;
	rc = domain_routine(routine, request, &reply);

	printf("routine=0x%s type=%llu handle=%llu\n",
	    routine_hex,
	    (unsigned long long)(argc > 2 ? strtoull(argv[2], NULL, 10) : 1),
	    (unsigned long long)(argc > 3 ? strtoull(argv[3], NULL, 10) : 0));
	printf("rc=%d\n", rc);
	if (reply) {
		char *desc = xpc_copy_description(reply);
		printf("reply=%s\n", desc);
		free(desc);
		xpc_release(reply);
	} else {
		printf("reply=(null)\n");
	}
	if (region && region != MAP_FAILED) {
		/* launchd writes the version into our shared region. */
		printf("shmem[0x1000] = %.128s\n", (char *)region);
		munmap(region, 0x1000);
	}
	xpc_release(request);
	return rc ? 1 : 0;
}