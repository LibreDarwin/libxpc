/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * probe6.c -- system-libxpc vs. real launchd, decisive experiment.
 *
 * Drives the REAL /usr/lib/system/libxpc.dylib xpc_pipe_routine at the
 * caller's actual task bootstrap port with the exact legacy LIST request
 * /bin/launchctl builds.  If this hangs, modern launchd never replies to
 * a legacy LIST from a non-platform caller using this request shape, and
 * our reimplementation is not the problem.  If it returns data, we diff
 * the request our tool sends against this one byte-for-byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <xpc/xpc.h>

typedef void *(*pipe_create_from_port_fn)(mach_port_t, int);
typedef int (*pipe_routine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef int (*pipe_simpleroutine_fn)(void *, xpc_object_t, xpc_object_t *);

static pipe_create_from_port_fn fn_pipe_create_from_port;
static pipe_routine_fn fn_pipe_routine;
static pipe_simpleroutine_fn fn_pipe_simpleroutine;

int
main(void)
{
	void *h = dlopen("/usr/lib/system/libxpc.dylib",
	    RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "dlopen libxpc: %s\n", dlerror());
		return 1;
	}
	fn_pipe_create_from_port = (pipe_create_from_port_fn)
	    dlsym(h, "xpc_pipe_create_from_port");
	fn_pipe_routine = (pipe_routine_fn) dlsym(h, "xpc_pipe_routine");
	fn_pipe_simpleroutine = (pipe_simpleroutine_fn)
	    dlsym(h, "xpc_pipe_simpleroutine");
	if (!fn_pipe_create_from_port || !fn_pipe_routine) {
		fprintf(stderr, "dlsym failed\n");
		return 1;
	}

	mach_port_t bp = MACH_PORT_NULL;
	kern_return_t kr = task_get_bootstrap_port(mach_task_self(), &bp);
	fprintf(stderr, "bootstrap port: kr=0x%x port=0x%x\n", kr, bp);
	if (kr != KERN_SUCCESS || !MACH_PORT_VALID(bp)) return 1;

	void *pipe = fn_pipe_create_from_port(bp, 4);
	if (!pipe) {
		fprintf(stderr, "pipe create failed\n");
		return 1;
	}

	/* Exactly what launchctl builds for `launchctl list`. */
	xpc_object_t req = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_uint64(req, "type", 1);      /* system */
	xpc_dictionary_set_uint64(req, "handle", 0);
	xpc_dictionary_set_string(req, "name", "");

	fprintf(stderr, ">>> sending routine LIST via SYSTEM libxpc\n");
	xpc_object_t reply = NULL;
	int rc = fn_pipe_routine(pipe, req, &reply);
	fprintf(stderr, "routine rc=%d reply=%p\n", rc, (void *)reply);
	if (reply) {
		const char *desc = xpc_copy_description(reply);
		fprintf(stderr, "reply: %s\n", desc ? desc : "(no desc)");
		free((void *)desc);
		uint64_t err = 0;
		if (xpc_get_type(reply) == XPC_TYPE_DICTIONARY) {
			err = xpc_dictionary_get_uint64(reply, "error");
			fprintf(stderr, "error=%llu\n", err);
		}
	}

	/* Also simpleroutine list, like probe3. */
	reply = NULL;
	rc = fn_pipe_simpleroutine(pipe, req, &reply);
	fprintf(stderr, "simpleroutine rc=%d reply=%p\n", rc, (void *)reply);

	return 0;
}