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
 * probe11.c -- connection-object wire capture (listener + client modes).
 *
 * probe.c ran listener and client in ONE process against a name real
 * launchd does not know, so both sides failed at the handshake: the
 * listener's SERVICE_CHECK_IN (0x325) answered error=1 (no such service),
 * the client's SERVICE_LOOKUP (0x324) answered error=3.  Those bytes are
 * banked (see WIRE_FORMAT.md ss12).  What is still missing is the SUCCESS
 * path: check-in and lookup replies carrying the service port, and the
 * connection messages that flow over the resolved port (async send,
 * sync/async with-reply wiring, endpoint object encoding).
 *
 * A real launchd only grants check-in to the process launchd spawned for
 * that service name, so the listener mode of this probe is meant to run
 * INSIDE a launchd job: launchd_stub-style wrapper script sets
 * DYLD_INSERT_LIBRARIES (launchd strips DYLD_* from its own spawn, but a
 * wrapper re-exports them at exec), then execs `probe11 listen NAME`.
 *
 *         probe11 listen NAME        -- listener mode (job side)
 *         probe11 connect NAME [sync|async]
 *                                     -- client mode (shell side)
 *
 * The client mode resolves NAME, sends the type corpus, optionally drives
 * a routine-with-reply (sync blocks; async returns immediately and prints
 * via handler), and finally sends an endpoint object wrapping the client
 * connection.  Every mach_msg crossing lands in XPC_PROBE_LOG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

/* ------------------------------------------------------------------ */
/* The object corpus: every serializable type, nested where it helps.  */
/* ------------------------------------------------------------------ */

static xpc_object_t build_corpus(void)
{
	xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
	xpc_object_t arr, d, date_obj;
	xpc_object_t uuid_obj, null_obj;
	uuid_t uuid = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };

	d = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(d, "inner", "value");
	xpc_dictionary_set_uint64(d, "n", 7);
	xpc_dictionary_set_value(dict, "dict", d);
	xpc_release(d);

	arr = xpc_array_create(NULL, 0);
	null_obj = xpc_null_create();
	xpc_array_set_string(arr, XPC_ARRAY_APPEND, "alpha");
	xpc_array_set_uint64(arr, XPC_ARRAY_APPEND, 42);
	xpc_array_set_double(arr, XPC_ARRAY_APPEND, 3.14);
	xpc_array_set_bool(arr, XPC_ARRAY_APPEND, true);
	xpc_array_append_value(arr, null_obj);
	xpc_dictionary_set_value(dict, "array", arr);
	xpc_release(arr);

	xpc_dictionary_set_string(dict, "string", "hello, xpc");
	xpc_dictionary_set_int64(dict, "int64", -123456789012345678LL);
	xpc_dictionary_set_uint64(dict, "uint64", 0xdeadbeefcafebabeULL);
	xpc_dictionary_set_double(dict, "double", 2.718281828459045);
	xpc_dictionary_set_bool(dict, "bool_true", true);
	xpc_dictionary_set_bool(dict, "bool_false", false);
	xpc_dictionary_set_value(dict, "null", xpc_null_create());
	xpc_dictionary_set_data(dict, "data", "0123456789abcdef", 16);

	date_obj = xpc_date_create(1234567890.25);
	xpc_dictionary_set_value(dict, "date", date_obj);
	xpc_release(date_obj);

	uuid_obj = xpc_uuid_create(uuid);
	xpc_dictionary_set_value(dict, "uuid", uuid_obj);
	xpc_release(uuid_obj);

	return dict;
}

void
print_obj(const char *tag, xpc_object_t obj)
{
	xpc_type_t t = xpc_get_type(obj);
	if (t == XPC_TYPE_ERROR) {
		const char *msg = xpc_dictionary_get_string(obj,
		    XPC_ERROR_KEY_DESCRIPTION);
		printf("%s: error: %s\n", tag, msg ? msg : "?");
	} else {
		char *desc = xpc_copy_description(obj);
		printf("%s: %s\n", tag, desc ? desc : "?");
		free(desc);
	}
	fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Listener mode: registers NAME via launchd check-in (0x325).        */
/* ------------------------------------------------------------------ */

static void
listen(const char *name, int seconds)
{
	dispatch_queue_t q = dispatch_queue_create("probe11.listen", NULL);
	xpc_connection_t listener = xpc_connection_create_mach_service(name, q,
	    XPC_CONNECTION_MACH_SERVICE_LISTENER);

	xpc_connection_set_event_handler(listener, ^(xpc_object_t obj) {
		xpc_type_t t = xpc_get_type(obj);
		if (t == XPC_TYPE_CONNECTION) {
			xpc_connection_t peer = obj;
			printf("listener: new peer connection\n");
			fflush(stdout);
			xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
				if (xpc_get_type(m) == XPC_TYPE_ERROR) {
					print_obj("peer", m);
				} else {
					print_obj("peer msg", m);
					/* Reply so a sync/async with-reply
					 * send completes, and log the reply
					 * bytes on the wire. */
					xpc_object_t r =
					    xpc_dictionary_create_reply(m);
					if (r) {
						xpc_dictionary_set_string(r,
						    "reply", "from listener");
						xpc_connection_send_message(peer, r);
						xpc_release(r);
					}
				}
			});
			xpc_connection_resume(peer);
		} else {
			print_obj("listener", obj);
		}
	});
	xpc_connection_resume(listener);
	printf("listener up: %s (pid %d)\n", name, getpid());
	fflush(stdout);
	sleep((unsigned)seconds);
}

/* ------------------------------------------------------------------ */
/* Client mode: resolves NAME via launchd lookup (0x324), then talks. */
/* ------------------------------------------------------------------ */

static void
connect_and_chat(const char *name, bool sync_reply)
{
	dispatch_queue_t q = dispatch_queue_create("probe11.client", NULL);
	xpc_connection_t client =
	    xpc_connection_create_mach_service(name, q, 0);

	xpc_connection_set_event_handler(client, ^(xpc_object_t obj) {
		print_obj("client", obj);
	});
	xpc_connection_resume(client);
	usleep(500000);

	xpc_object_t dict = build_corpus();
	printf(">>> corpus (async)\n");
	fflush(stdout);
	xpc_connection_send_message(client, dict);

	usleep(300000);

	xpc_object_t ask = xpc_dictionary_create(NULL, NULL, 0);
	xpc_dictionary_set_string(ask, "ask", "reply-me");
	if (sync_reply) {
		printf(">>> with-reply (sync)\n");
		fflush(stdout);
		xpc_object_t reply =
		    xpc_connection_send_message_with_reply_sync(client, ask);
		print_obj("sync reply", reply);
		xpc_release(reply);
	} else {
		printf(">>> with-reply (async)\n");
		fflush(stdout);
		xpc_connection_send_message_with_reply(client, ask, q,
		    ^(xpc_object_t reply) {
			print_obj("async reply", reply);
		});
		sleep(1);
	}

	usleep(300000);

	xpc_object_t with_ep = xpc_dictionary_create(NULL, NULL, 0);
	xpc_endpoint_t ep = xpc_endpoint_create(client);
	xpc_dictionary_set_value(with_ep, "ep", ep);
	xpc_release(ep);
	printf(">>> endpoint object (async)\n");
	fflush(stdout);
	xpc_connection_send_message(client, with_ep);

	sleep(1);
	xpc_connection_cancel(client);
	xpc_release(client);
	dispatch_release(q);
}

int
main(int argc, char **argv)
{
	if (argc >= 3 && strcmp(argv[1], "listen") == 0) {
		listen(argv[2], argc >= 4 ? atoi(argv[3]) : 45);
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "connect") == 0) {
		bool sync_reply = (argc >= 4 && strcmp(argv[3], "sync") == 0);
		connect_and_chat(argv[2], sync_reply);
		return 0;
	}
	fprintf(stderr,
	    "usage: probe11 listen NAME [SECONDS] | connect NAME [sync|async]\n");
	return 1;
}