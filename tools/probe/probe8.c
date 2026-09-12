/*
 * tools/probe/probe8.c — byte-exact capture of the REAL libxpc complex
 * routine request carrying a mach-send value (the launchctl `list`
 * shape), interposed at mach_msg, so the 0xd000 value encoding can be
 * matched byte-for-byte by our serializer.
 *
 * Build: cc -o probe8 probe8.c -undefined,dynamic_lookup
 * Run:   DYLD_INSERT_LIBRARIES=interpose.dylib \
 *        XPC_PROBE_LOG=/tmp/probe8.log ./probe8
 */
#include <dlfcn.h>
#include <errno.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xpc/xpc.h>

typedef int (*domain_routine_fn)(uint64_t, xpc_object_t, xpc_object_t *);

extern void xpc_dictionary_set_mach_send(xpc_object_t object,
    const char *key, mach_port_t port);

int
main(void)
{
	void *h;
	domain_routine_fn domain_routine;
	xpc_object_t request, reply;
	mach_port_t bp = MACH_PORT_NULL;
	char *desc;
	int rc;

	h = dlopen("libxpc.dylib", RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		h = dlopen("/usr/lib/system/libxpc.dylib",
		    RTLD_NOW | RTLD_GLOBAL);
	}
	if (!h) {
		fprintf(stderr, "dlopen libxpc: %s\n", dlerror());
		return 1;
	}

	domain_routine = (domain_routine_fn)dlsym(RTLD_DEFAULT,
	    "_xpc_domain_routine");
	if (!domain_routine) {
		fprintf(stderr, "dlsym _xpc_domain_routine failed\n");
		return 1;
	}

	if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS ||
	    !MACH_PORT_VALID(bp)) {
		fprintf(stderr, "no bootstrap port\n");
		return 1;
	}

	/* The real launchctl `list` request shape: handle, type, legacy,
	 * domain-port (mach send).  Keep this order and value set. */
	request = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_uint64(request, "handle", 0);
	xpc_dictionary_set_uint64(request, "type", 7);
	xpc_dictionary_set_bool(request, "legacy", true);
	xpc_dictionary_set_mach_send(request, "domain-port", bp);

	reply = NULL;
	rc = domain_routine(0x32f, request, &reply);

	printf("rc=%d\n", rc);
	if (reply) {
		desc = xpc_copy_description(reply);
		printf("reply=%s\n", desc);
		free(desc);
		xpc_release(reply);
	} else {
		printf("reply=(null) errno=%d\n", errno);
	}
	xpc_release(request);
	return rc ? 1 : 0;
}