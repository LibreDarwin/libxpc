/*
 * probe3.c -- pipe-based wire capture.
 *
 * xpc_pipe is the raw serialization path inside libxpc: no check-in, no
 * bootstrap, no listener.  You hand it a mach port and it sends
 * serialized objects directly.  A receiver thread owns the port, dumps
 * every message byte-for-byte, and echoes a reply so the sender's
 * simpleroutine call unblocks.
 *
 * The captured message body IS the payload format: "CPX@" magic,
 * `xpc_mach_msg` framing, descriptors -- the exact bytes a connection
 * serializes once its handshake is done, and the basis for our
 * reimplementation's serialization core.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/message.h>
#include <xpc/xpc.h>

typedef void *(*pipe_create_from_port_fn)(mach_port_t, int);
typedef int (*pipe_simpleroutine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef void (*pipe_invalidate_fn)(void *);
typedef int (*pipe_routine_fn)(void *, xpc_object_t, xpc_object_t *);

static pipe_create_from_port_fn fn_pipe_create_from_port;
static pipe_simpleroutine_fn fn_pipe_simpleroutine;
static pipe_routine_fn fn_pipe_routine;
static pipe_invalidate_fn fn_pipe_invalidate;

static int g_fd = -1;

static void dump_msg(const char *tag, const void *buf, size_t len)
{
	const unsigned char *p = buf;
	char line[128];
	size_t i;
	int n;

	n = snprintf(line, sizeof(line), "--- %s len=%zu ---\n", tag, len);
	write(g_fd, line, (size_t)n);
	for (i = 0; i < len; i += 16) {
		size_t j, chunk = len - i < 16 ? len - i : 16;
		n = snprintf(line, sizeof(line), "%08zx ", i);
		write(g_fd, line, (size_t)n);
		for (j = 0; j < chunk; j++) {
			n = snprintf(line, sizeof(line), "%02x ", p[i + j]);
			write(g_fd, line, (size_t)n);
		}
		write(g_fd, "\n", 1);
	}
}

static void *rx_thread(void *arg)
{
	mach_port_t port = (mach_port_t)(uintptr_t)arg;
	char buf[65536];
	mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
	kern_return_t kr;

	for (;;) {
		memset(buf, 0, sizeof(buf));
		kr = mach_msg(hdr, MACH_RCV_MSG, 0, sizeof(buf), port,
		    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
		if (kr != KERN_SUCCESS) {
			break;
		}

		dump_msg("recv", hdr, hdr->msgh_size);

		/*
		 * Echo a reply back to whoever asked (the simpleroutine's
		 * reply port is the received message's local port).  The
		 * reply keeps the same id; the pipe layer accepts a reply
		 * whose canonical payload is a "nil" object.
		 */
		if (hdr->msgh_remote_port != MACH_PORT_NULL) {
			mach_msg_header_t *r = malloc(hdr->msgh_size);
			if (r) {
				memcpy(r, hdr, hdr->msgh_size);
				r->msgh_remote_port = hdr->msgh_local_port;
				r->msgh_local_port = MACH_PORT_NULL;
				r->msgh_voucher_port = MACH_PORT_NULL;
				r->msgh_id = 0;
				kr = mach_msg(r, MACH_SEND_MSG, hdr->msgh_size,
				    0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
				free(r);
			}
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	mach_port_t port;
	kern_return_t kr;
	void *pipe;
	pthread_t th;
	xpc_object_t dict, reply = NULL;
	int rc;

	(void)argc;
	(void)argv;

	void *h = dlopen("/usr/lib/system/libxpc.dylib", RTLD_NOW | RTLD_LOCAL);
	fn_pipe_create_from_port = (pipe_create_from_port_fn)
	    dlsym(h, "xpc_pipe_create_from_port");
	fn_pipe_simpleroutine = (pipe_simpleroutine_fn)
	    dlsym(h, "xpc_pipe_simpleroutine");
	fn_pipe_routine = (pipe_routine_fn) dlsym(h, "xpc_pipe_routine");
	fn_pipe_invalidate = (pipe_invalidate_fn) dlsym(h, "xpc_pipe_invalidate");

	const char *path = getenv("XPC_PROBE_LOG");
	g_fd = open(path ? path : "/tmp/xpc_pipe.log",
	    O_WRONLY | O_CREAT | O_TRUNC, 0644);

	kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
	    &port);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "port allocate failed: 0x%x\n", kr);
		return 1;
	}
	printf("rx port 0x%x\n", port);
	fflush(stdout);

	pthread_create(&th, NULL, rx_thread, (void *)(uintptr_t)port);

	usleep(200000);

	pipe = fn_pipe_create_from_port(port, 0);

	dict = xpc_dictionary_create(NULL, NULL, 0);
	{
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
		xpc_dictionary_set_value(dict, "date",
		    xpc_date_create(1234567890));
		xpc_dictionary_set_fd(dict, "fd",
		    open("/dev/null", O_RDONLY));
	}

	printf(">>> simpleroutine send\n");
	fflush(stdout);
	rc = fn_pipe_simpleroutine(pipe, dict, &reply);
	printf("simpleroutine rc=%d reply=%p %s\n", rc, (void *)reply,
	    reply ? xpc_copy_description(reply) : "");
	fflush(stdout);

	printf(">>> routine send\n");
	fflush(stdout);
	reply = NULL;
	rc = fn_pipe_routine(pipe, dict, &reply);
	printf("routine rc=%d reply=%p %s\n", rc, (void *)reply,
	    reply ? xpc_copy_description(reply) : "");
	fflush(stdout);

	fn_pipe_invalidate(pipe);

	usleep(300000);
	close(g_fd);
	return 0;
}