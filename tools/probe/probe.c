/*
 * probe.c -- wire-format interrogator for Apple's reference libxpc.
 *
 * The whole point of this project is a byte-identical reimplementation of
 * libxpc's externally observable surface.  The most important part of that
 * surface -- the one that must match Apple's bytes exactly -- is the
 * serialization format used on the wire: the bytes libxpc writes into a
 * mach message body when you send an xpc_dictionary.
 *
 * Rather than trusting writeups or disassembly, this tool asks Apple's own
 * libxpc for its bytes.  It runs under interpose.dylib, which logs every
 * mach_msg the process makes; this program then drives a completely
 * ordinary XPC exchange -- listener + client, corpus dictionary, sync
 * reply, endpoint hand-off -- and the interposer records every message
 * that crosses, byte for byte: the connection handshake, the serialized
 * objects, the port descriptors.
 *
 * The log is the spec.  See docs/WIRE_FORMAT.md.
 *
 * This tool intentionally links the real /usr/lib/system/libxpc.dylib.
 * It is a research instrument, not part of the reimplementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/message.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------ */
/* The object corpus: every serializable type, nested where it helps.  */
/* ------------------------------------------------------------------ */

static xpc_object_t build_corpus(void)
{
	xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t arr, d, date_obj;
	xpc_object_t uuid_obj, null_obj;
	int fd;
	uuid_t uuid = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };

	d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(d, "inner", "value");
	xpc_dictionary_set_uint64(d, "n", 7);
	xpc_dictionary_set_value(dict, "dict", d);

	arr = xpc_array_create(NULL, 0);
	null_obj = xpc_null_create();
	xpc_array_set_string(arr, XPC_ARRAY_APPEND, "alpha");
	xpc_array_set_uint64(arr, XPC_ARRAY_APPEND, 42);
	xpc_array_set_double(arr, XPC_ARRAY_APPEND, 3.14);
	xpc_array_set_bool(arr, XPC_ARRAY_APPEND, true);
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

	fd = open("/dev/null", O_RDONLY);
	if (fd >= 0) {
		xpc_dictionary_set_fd(dict, "fd", fd);
		close(fd);
	}

	date_obj = xpc_date_create(1234567890.25);
	xpc_dictionary_set_value(dict, "date", date_obj);

	uuid_obj = xpc_uuid_create(uuid);
	xpc_dictionary_set_value(dict, "uuid", uuid_obj);

	return dict;
}

int main(int argc, char **argv)
{
	dispatch_queue_t q = dispatch_queue_create("probe", NULL);
	xpc_connection_t listener, client;
	xpc_object_t dict, reply;
	char name[128];
	__block const char *msg;

	(void)argc;
	(void)argv;

	snprintf(name, sizeof(name), "com.example.xpcprobe.%d", getpid());

	listener = xpc_connection_create_mach_service(name, q,
	    XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(listener, ^(xpc_object_t obj) {
		xpc_type_t t = xpc_get_type(obj);

		if (t == XPC_TYPE_CONNECTION) {
			xpc_connection_t peer = obj;

			printf("listener: new peer connection\n");
			xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
				if (xpc_get_type(m) == XPC_TYPE_ERROR) {
					printf("listener peer: error %s\n",
					    xpc_dictionary_get_string(m,
					    XPC_ERROR_KEY_DESCRIPTION));
				} else {
					printf("listener peer: message: %s\n",
					    xpc_copy_description(m));
					/* echo a reply so the sync send completes */
					xpc_object_t r =
					    xpc_dictionary_create_reply(m);
					if (r) {
						xpc_dictionary_set_string(r,
						    "reply", "from listener");
						xpc_connection_send_message(peer, r);
					}
				}
			});
			xpc_connection_resume(peer);
		} else if (t == XPC_TYPE_ERROR) {
			msg = xpc_dictionary_get_string(obj,
			    XPC_ERROR_KEY_DESCRIPTION);
			printf("listener error: %s\n", msg ? msg : "?");
		}
	});
	xpc_connection_resume(listener);

	usleep(300000);

	client = xpc_connection_create_mach_service(name, q, 0);
	xpc_connection_set_event_handler(client, ^(xpc_object_t obj) {
		xpc_type_t t = xpc_get_type(obj);

		if (t == XPC_TYPE_ERROR) {
			msg = xpc_dictionary_get_string(obj,
			    XPC_ERROR_KEY_DESCRIPTION);
			printf("client error: %s\n", msg ? msg : "?");
		} else {
			printf("client: message: %s\n", xpc_copy_description(obj));
		}
	});
	xpc_connection_resume(client);

	usleep(500000);

	dict = build_corpus();
	printf(">>> sending corpus (async)\n");
	xpc_connection_send_message(client, dict);
	fflush(stdout);

	usleep(300000);

	/* a message with a reply, to capture the sync/reply wiring */
	xpc_object_t ask = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(ask, "ask", "reply-me");
	printf(">>> sending sync reply request\n");
	fflush(stdout);
	reply = xpc_connection_send_message_with_reply_sync(client, ask);
	if (reply) {
		printf("sync reply: %s\n", xpc_copy_description(reply));
	} else {
		printf("sync reply: (null)\n");
	}
	fflush(stdout);

	sleep(1);

	/*
	 * Endpoint hand-off: the client sends an endpoint object; the
	 * receiver accepts it.  This forces the endpoint/connection
	 * serialization path into the wire bytes.
	 */
	printf(">>> sending endpoint object\n");
	xpc_object_t with_ep = xpc_dictionary_create(NULL, NULL, 0);
	xpc_endpoint_t ep = xpc_endpoint_create(client);
	xpc_dictionary_set_value(with_ep, "ep", ep);
	xpc_connection_send_message(client, with_ep);
	fflush(stdout);

	sleep(2);
	return 0;
}