/*
 * probe4.c -- peer-pipe wire capture.
 *
 * Two real xpc_pipe objects wired port-to-port in one process:
 *
 *   tx_pipe -> xpc_pipe_create_from_port(P, 0)   (sender)
 *   rx_pipe -> xpc_pipe_create_from_port(P, 0)   (receiver, same port)
 *
 * A thread calls xpc_pipe_receive(rx_pipe) so libxpc's own receive loop
 * answers the pipe check-in handshake properly; the main thread drives
 * xpc_pipe_simpleroutine / xpc_pipe_routine.  Stacked on interpose.dylib
 * (env XPC_PROBE_LOG), the entire exchange -- check-in, routine messages,
 * replies -- lands in the log byte for byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <xpc/xpc.h>

typedef void *(*pipe_create_fn)(void *name, mach_port_t port, int flags);
typedef void *(*pipe_create_from_port_fn)(mach_port_t port, int flags);
typedef int (*pipe_simpleroutine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef int (*pipe_routine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef int (*pipe_receive_fn)(void *, xpc_object_t *);
typedef void (*pipe_invalidate_fn)(void *);

static pipe_create_fn fn_pipe_create;
static pipe_create_from_port_fn fn_pipe_create_from_port;
static pipe_simpleroutine_fn fn_pipe_simpleroutine;
static pipe_routine_fn fn_pipe_routine;
static pipe_receive_fn fn_pipe_receive;
static pipe_invalidate_fn fn_pipe_invalidate;

static xpc_object_t build_corpus(void)
{
	xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t arr, d, null_obj;
	uuid_t uuid = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };

	d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(d, "inner", "value");
	xpc_dictionary_set_uint64(d, "n", 7);
	xpc_dictionary_set_value(dict, "dict", d);

	arr = xpc_array_create(NULL, 0);
	xpc_array_set_string(arr, XPC_ARRAY_APPEND, "alpha");
	xpc_array_set_uint64(arr, XPC_ARRAY_APPEND, 42);
	xpc_array_set_double(arr, XPC_ARRAY_APPEND, 3.14);
	xpc_array_set_bool(arr, XPC_ARRAY_APPEND, true);
	null_obj = xpc_null_create();
	xpc_array_append_value(arr, null_obj);
	xpc_dictionary_set_value(dict, "array", arr);

	xpc_dictionary_set_string(dict, "string", "hello, xpc");
	xpc_dictionary_set_int64(dict, "int64", -123456789012345678LL);
	xpc_dictionary_set_uint64(dict, "uint64", 0xdeadbeefcafebabeULL);
	xpc_dictionary_set_double(dict, "double", 2.718281828459045);
	xpc_dictionary_set_bool(dict, "bool_true", true);
	xpc_dictionary_set_bool(dict, "bool_false", false);
	xpc_dictionary_set_value(dict, "null", xpc_null_create());
	xpc_dictionary_set_data(dict, "data", "0123456789abcdef", 16);
	xpc_dictionary_set_value(dict, "uuid", xpc_uuid_create(uuid));
	xpc_dictionary_set_value(dict, "date", xpc_date_create(1234567890));

	return dict;
}

static void *rx_thread(void *arg)
{
	void *pipe = arg;
	xpc_object_t obj = NULL;
	int rc;

	for (;;) {
		obj = NULL;
		rc = fn_pipe_receive(pipe, &obj);
		if (rc != 0) {
			printf("rx rc=0x%x\n", rc);
			fflush(stdout);
			usleep(100000);
			continue;
		}
		printf("rx obj: %s\n",
		    obj ? xpc_copy_description(obj) : "(nil)");
		fflush(stdout);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	mach_port_t port;
	kern_return_t kr;
	void *tx, *rx;
	pthread_t th;
	xpc_object_t dict, reply = NULL;
	int rc;

	(void)argc;
	(void)argv;

	void *h = dlopen("/usr/lib/system/libxpc.dylib", RTLD_NOW | RTLD_LOCAL);
	fn_pipe_create = (pipe_create_fn) dlsym(h, "xpc_pipe_create");
	fn_pipe_create_from_port = (pipe_create_from_port_fn)
	    dlsym(h, "xpc_pipe_create_from_port");
	fn_pipe_simpleroutine = (pipe_simpleroutine_fn)
	    dlsym(h, "xpc_pipe_simpleroutine");
	fn_pipe_routine = (pipe_routine_fn) dlsym(h, "xpc_pipe_routine");
	fn_pipe_receive = (pipe_receive_fn) dlsym(h, "xpc_pipe_receive");
	fn_pipe_invalidate = (pipe_invalidate_fn) dlsym(h, "xpc_pipe_invalidate");

	if (!fn_pipe_create || !fn_pipe_create_from_port ||
	    !fn_pipe_simpleroutine || !fn_pipe_routine ||
	    !fn_pipe_receive || !fn_pipe_invalidate) {
		fprintf(stderr, "missing pipe symbol\n");
		return 1;
	}

	kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
	    &port);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "port allocate: 0x%x\n", kr);
		return 1;
	}
	kr = mach_port_insert_right(mach_task_self(), port, port,
	    MACH_MSG_TYPE_MAKE_SEND);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "port insert send right: 0x%x\n", kr);
		return 1;
	}
	printf("rx port 0x%x (+send)\n", port);
	fflush(stdout);

	tx = fn_pipe_create_from_port(port, 0);
	rx = fn_pipe_create_from_port(port, 0);
	printf("tx=%p rx=%p\n", tx, rx);
	fflush(stdout);

	pthread_create(&th, NULL, rx_thread, rx);
	usleep(300000);

	dict = build_corpus();

	printf(">>> simpleroutine\n");
	fflush(stdout);
	rc = fn_pipe_simpleroutine(tx, dict, &reply);
	printf("simpleroutine rc=0x%x reply=%s\n", rc,
	    reply ? xpc_copy_description(reply) : "(nil)");
	fflush(stdout);

	usleep(200000);

	printf(">>> routine\n");
	fflush(stdout);
	reply = NULL;
	rc = fn_pipe_routine(tx, dict, &reply);
	printf("routine rc=0x%x reply=%s\n", rc,
	    reply ? xpc_copy_description(reply) : "(nil)");
	fflush(stdout);

	sleep(1);
	fn_pipe_invalidate(tx);
	fn_pipe_invalidate(rx);
	return 0;
}