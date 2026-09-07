/*
 * probe4b.c -- full call/return wire capture with a raw receiver.
 *
 * Same corpus as probe4, but the receive side is a RAW mach_msg_receive
 * thread on the shared port.  Important: xpc_pipe_receive() on a pipe made
 * with xpc_pipe_create_from_port() always fails with KERN_INVALID_ARGUMENT
 * (rcv_name in mach_msg is the pipe OBJECT pointer, not a port: the pipe
 * only carries a receive port when built by xpc_pipe_create()).
 *
 * Flow:
 *   tx = xpc_pipe_create_from_port(P, 0)
 *   simpleroutine(tx, corpus)      -> sends 396B, no reply wait (local=0)
 *   routine(tx, corpus, &reply)    -> sends message WITH reply port, waits
 *   rx thread: mach_msg_receive(P) -> logs incoming bytes (interposer)
 *     if the incoming msg carries a reply port (msgh_remote_port != 0),
 *     replies with a RAW mach_msg: MOVE_SEND_ONCE of the reply port +
 *     a hand-built xpc dict {"reply":1} (id 0x20000000).  The reply port
 *     arrives as a send-once right, which xpc_pipe_create_from_port()
 *     cannot wrap (it walks an endpoint lookup requiring a send right),
 *     so a raw message is the only way through.
 *
 * Every mach_msg crossing lands in XPC_PROBE_LOG.
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

typedef void *(*pipe_create_from_port_fn)(mach_port_t port, int flags);
typedef int (*pipe_simpleroutine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef int (*pipe_routine_fn)(void *, xpc_object_t, xpc_object_t *);
typedef void (*pipe_invalidate_fn)(void *);

static pipe_create_from_port_fn fn_pipe_create_from_port;
static pipe_simpleroutine_fn fn_pipe_simpleroutine;
static pipe_routine_fn fn_pipe_routine;
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
	uint8_t buf[4096] __attribute__((aligned(16)));
	mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
	mach_port_t port = (mach_port_t)(uintptr_t)arg;
	int rc;

	printf("rx: thread alive, port 0x%x\n", port);
	fflush(stdout);
	for (;;) {
		memset(buf, 0, sizeof(buf));
		hdr->msgh_size = sizeof(buf);
		hdr->msgh_local_port = port;
		printf("rx: calling mach_msg_receive on 0x%x...\n", port);
		fflush(stdout);
		rc = mach_msg_receive(hdr);
		if (rc != KERN_SUCCESS) {
			printf("rx mach_msg_receive rc=0x%x\n", rc);
			fflush(stdout);
			continue;
		}
		printf("rx got id=0x%x remote=0x%x local=0x%x voucher=0x%x size=%u\n",
		    hdr->msgh_id, hdr->msgh_remote_port, hdr->msgh_local_port,
		    hdr->msgh_voucher_port, hdr->msgh_size);
		fflush(stdout);

		/* A reply channel means the sender expects an answer:
		 * respond with a raw xpc reply message.  (Sender's
		 * msgh_local_port appears as our msgh_remote_port and is a
		 * SEND-ONCE right -- directly sendable, MOVE_SEND_ONCE.) */
		if (hdr->msgh_remote_port) {
			mach_port_t rp = hdr->msgh_remote_port;
			printf("rx reply-port=0x%x\n", rp);
			fflush(stdout);

			/* Raw xpc reply {"reply": 1}.
			 * id = 0x20000000 (routine reply; checked by __xpc_pipe_routine:
			 * [hdr+0x14] must == 0x20000000, else KERN_INVALID_ARGUMENT).
			 * Body: "CPX@" ver=5 flags=0xf000 body_len count keys */
			uint8_t rbuf[128] __attribute__((aligned(16)));
			mach_msg_header_t *rh = (mach_msg_header_t *)rbuf;
			memset(rbuf, 0, sizeof(rbuf));
			rh->msgh_bits = MACH_MSGH_BITS(
			    MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
			rh->msgh_size = 64;
			rh->msgh_remote_port = rp;
			/* __xpc_pipe_routine's post-receive check:
			 * [hdr+0x14] (received msgh_id) must == 0x20000000,
			 * else it returns KERN_INVALID_ARGUMENT (5). */
			rh->msgh_id = 0x20000000;
			uint8_t *p = rbuf + 24;
			memcpy(p, "CPX@", 4); p += 4;
			*(uint32_t *)(void *)p = 5; p += 4;	/* version */
			*(uint32_t *)(void *)p = 0xf000; p += 4;/* flags */
			*(uint32_t *)(void *)p = 24; p += 4;	/* body_len */
			*(uint32_t *)(void *)p = 1; p += 4;	/* count */
			memcpy(p, "reply\0\0\0", 8); p += 8;	/* key */
			*(uint32_t *)(void *)p = 0x3000; p += 4;/* int64 */
			*(uint64_t *)(void *)p = 1; p += 8;	/* value */
			int sr = mach_msg(rh, MACH_SEND_MSG, rh->msgh_size,
			    0, 0, 0, 0);
			printf("reply send rc=0x%x\n", sr);
			fflush(stdout);
		}
	}
	return NULL;
}

int main(int argc, char **argv)
{
	mach_port_t port;
	kern_return_t kr;
	void *tx;
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
	if (!fn_pipe_create_from_port || !fn_pipe_simpleroutine ||
	    !fn_pipe_routine || !fn_pipe_invalidate) {
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
	printf("tx=%p\n", tx);
	fflush(stdout);

	int prc = pthread_create(&th, NULL, rx_thread, (void *)(uintptr_t)port);
	printf("pthread_create rc=%d\n", prc);
	fflush(stdout);
	usleep(300000);

	dict = build_corpus();

	printf(">>> simpleroutine\n");
	fflush(stdout);
	rc = fn_pipe_simpleroutine(tx, dict, &reply);
	printf("simpleroutine rc=0x%x reply=%s\n", rc,
	    reply ? xpc_copy_description(reply) : "(nil)");
	fflush(stdout);

	usleep(300000);

	printf(">>> routine\n");
	fflush(stdout);
	reply = NULL;
	rc = fn_pipe_routine(tx, dict, &reply);
	printf("routine rc=0x%x reply=%s\n", rc,
	    reply ? xpc_copy_description(reply) : "(nil)");
	fflush(stdout);

	sleep(1);
	fn_pipe_invalidate(tx);
	return 0;
}