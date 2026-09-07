/*
 * probe2.c -- wire-format interrogator, launchd-free edition.
 *
 * probe.c failed: this macOS refuses bootstrap registration of arbitrary
 * XPC service names, so a named-service listener can't come up in a cav
 * process.  libxpc's private API has the answer -- the whole modern
 * session/listener layer works without launchd:
 *
 *   xpc_listener_create_anonymous()              -> xpc_listener_t
 *   xpc_listener_create_endpoint(listener)       -> xpc_endpoint_t
 *   xpc_connection_create_from_endpoint(ep, q)   -> real client connection
 *
 * The two ends are full-blown libxpc objects; the interposer captures the
 * check-in, the payloads, the port descriptors -- byte for byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

typedef xpc_listener_t (*listener_create_anonymous_fn)(void *,
    dispatch_queue_t, xpc_handler_t);
typedef xpc_endpoint_t (*listener_create_endpoint_fn)(xpc_listener_t);
typedef xpc_connection_t (*conn_from_endpoint_fn)(xpc_endpoint_t,
    dispatch_queue_t);
typedef mach_port_t (*endpoint_copy_listener_port_fn)(xpc_endpoint_t);
typedef void (*listener_activate_fn)(xpc_listener_t);
typedef void (*listener_set_handler_fn)(xpc_listener_t, xpc_handler_t);
typedef void (*session_set_handler_fn)(void *, xpc_handler_t);
typedef void (*session_send_fn)(void *, xpc_object_t);
typedef void (*session_activate_fn)(void *);

static listener_create_anonymous_fn fn_listener_create_anonymous;
static listener_create_endpoint_fn fn_listener_create_endpoint;
static conn_from_endpoint_fn fn_conn_from_endpoint;
static endpoint_copy_listener_port_fn fn_endpoint_copy_listener_port;
static listener_activate_fn fn_listener_activate;
static listener_set_handler_fn fn_listener_set_incoming_session_handler;
static session_set_handler_fn fn_session_set_incoming_message_handler;
static session_send_fn fn_session_send_message;
static session_activate_fn fn_session_activate;

static void resolve_private(void)
{
	void *h = dlopen("/usr/lib/system/libxpc.dylib", RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "dlopen libxpc: %s\n", dlerror());
		exit(1);
	}
	fn_listener_create_anonymous = (listener_create_anonymous_fn)
	    dlsym(h, "xpc_listener_create_anonymous");
	fn_listener_create_endpoint = (listener_create_endpoint_fn)
	    dlsym(h, "xpc_listener_create_endpoint");
	fn_conn_from_endpoint = (conn_from_endpoint_fn)
	    dlsym(h, "xpc_connection_create_from_endpoint");
	fn_endpoint_copy_listener_port = (endpoint_copy_listener_port_fn)
	    dlsym(h, "xpc_endpoint_copy_listener_port_4sim");
	fn_listener_activate = (listener_activate_fn)
	    dlsym(h, "xpc_listener_activate");
	fn_listener_set_incoming_session_handler = (listener_set_handler_fn)
	    dlsym(h, "xpc_listener_set_incoming_session_handler");
	fn_session_set_incoming_message_handler = (session_set_handler_fn)
	    dlsym(h, "xpc_session_set_incoming_message_handler");
	fn_session_send_message = (session_send_fn)
	    dlsym(h, "xpc_session_send_message");
	fn_session_activate = (session_activate_fn)
	    dlsym(h, "xpc_session_activate");

	if (!fn_listener_create_anonymous) fprintf(stderr, "missing create_anonymous\n");
	if (!fn_listener_create_endpoint) fprintf(stderr, "missing create_endpoint\n");
	if (!fn_conn_from_endpoint) fprintf(stderr, "missing conn_from_endpoint\n");
	if (!fn_endpoint_copy_listener_port) fprintf(stderr, "missing copy_listener_port\n");
	if (!fn_listener_activate) fprintf(stderr, "missing listener_activate\n");
	if (!fn_listener_set_incoming_session_handler) fprintf(stderr, "missing set_incoming\n");
	if (!fn_session_set_incoming_message_handler) fprintf(stderr, "missing sess_set_msg\n");
	if (!fn_session_send_message) fprintf(stderr, "missing sess_send\n");
	if (!fn_session_activate) fprintf(stderr, "missing sess_activate\n");
	if (!fn_listener_create_anonymous || !fn_listener_create_endpoint ||
	    !fn_conn_from_endpoint || !fn_endpoint_copy_listener_port ||
	    !fn_listener_activate || !fn_listener_set_incoming_session_handler ||
	    !fn_session_set_incoming_message_handler || !fn_session_send_message ||
	    !fn_session_activate) {
		exit(1);
	}
}

static void build_corpus(xpc_object_t dict)
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
	    xpc_date_create(1234567890.25));
}

int main(int argc, char **argv)
{
	resolve_private();
	dispatch_queue_t q = dispatch_queue_create("probe2", NULL);
	__block void *session = NULL;
	xpc_listener_t listener;
	xpc_endpoint_t ep;
	xpc_connection_t sender;
	xpc_object_t dict, reply;

	(void)argc;
	(void)argv;

	fprintf(stderr, "step: create_anonymous\n");
	listener = fn_listener_create_anonymous(NULL, q,
	    ^(xpc_object_t incoming) {
		printf("listener: incoming: %s\n",
		    xpc_copy_description(incoming));
		fflush(stdout);
	});

	fn_listener_set_incoming_session_handler(listener,
	    ^(xpc_object_t s) {
		printf("listener: new session\n");
		fflush(stdout);
		session = s;
		fn_session_set_incoming_message_handler(s,
		    ^(xpc_object_t m) {
			if (xpc_get_type(m) == XPC_TYPE_ERROR) {
				printf("session error: %s\n",
				    xpc_dictionary_get_string(m,
				    XPC_ERROR_KEY_DESCRIPTION));
			} else {
				printf("session msg: %s\n",
				    xpc_copy_description(m));
				fflush(stdout);
				xpc_object_t r =
				    xpc_dictionary_create_reply(m);
				if (r) {
					xpc_dictionary_set_string(r,
					    "reply", "from listener");
					fn_session_send_message(s, r);
				}
			}
		});
		fn_session_activate(s);
	});
	fn_listener_activate(listener);

	usleep(500000);

	ep = fn_listener_create_endpoint(listener);
	fprintf(stderr, "step: copy port\n");
	fflush(stderr);
	printf("listener port = 0x%x\n",
	    fn_endpoint_copy_listener_port(ep));
	fflush(stdout);

	fprintf(stderr, "step: conn from endpoint\n");
	fflush(stderr);
	sender = fn_conn_from_endpoint(ep, q);
	printf("sender = %p\n", (void *)sender);
	fflush(stdout);
	if (!sender) {
		fprintf(stderr, "connection create failed\n");
		return 1;
	}
	xpc_connection_set_event_handler(sender, ^(xpc_object_t obj) {
		if (xpc_get_type(obj) == XPC_TYPE_ERROR) {
			const char *s = xpc_dictionary_get_string(obj,
			    XPC_ERROR_KEY_DESCRIPTION);
			printf("sender error: %s\n", s ? s : "?");
		} else {
			printf("sender msg: %s\n", xpc_copy_description(obj));
		}
		fflush(stdout);
	});
	xpc_connection_resume(sender);

	usleep(500000);

	dict = xpc_dictionary_create(NULL, NULL, 0);
	build_corpus(dict);
	printf(">>> send corpus\n");
	fflush(stdout);
	xpc_connection_send_message(sender, dict);

	usleep(300000);

	dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(dict, "ask", "reply-me");
	printf(">>> sync reply request\n");
	fflush(stdout);
	reply = xpc_connection_send_message_with_reply_sync(sender, dict);
	printf("sync reply: %s\n",
	    reply ? xpc_copy_description(reply) : "(null)");
	fflush(stdout);

	usleep(300000);

	dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_value(dict, "ep",
	    xpc_endpoint_create(sender));
	printf(">>> endpoint transfer\n");
	fflush(stdout);
	xpc_connection_send_message(sender, dict);

	sleep(2);
	return 0;
}