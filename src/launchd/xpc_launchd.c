/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 */

/*
 * xpc_launchd.c -- the libxpc SPI only launchd calls.
 *
 * Apple's 10.9 libxpc carried these for launchd-842; nothing else uses
 * them, so they are built into launchd rather than libsystem_xpc.
 *
 * ld2xpc() turns job plist data launchd has already parsed into XPC
 * objects (job defaults, external events).  Each launch_data type maps to
 * its XPC counterpart; one with none yields NULL, which the callers'
 * existing type checks reject.
 *
 * xpc_call_wakeup() answers a requester blocked in a MIG routine with
 * just a return code: a mig_reply_error_t moved to its send-once reply
 * port.  launchd uses it for the XPC domain routines, which
 * mk/patches/launchd/0005 leaves undispatched, so today it is not reached.
 */

#include <launch.h>
#include <mach/mach.h>
#include <mach/mig_errors.h>
#include <string.h>
#include <xpc/xpc.h>

#include <xpc/launchd.h>

static void
ld2xpc_dict_add(const launch_data_t value, const char *key, void *context)
{
	xpc_object_t xv = ld2xpc(value);

	if (xv != NULL) {
		xpc_dictionary_set_value((xpc_object_t)context, key, xv);
		xpc_release(xv);
	}
}

xpc_object_t
ld2xpc(struct _launch_data *data)
{
	launch_data_t ld = data;
	xpc_object_t xo = NULL;
	size_t i, n;

	if (ld == NULL) {
		return NULL;
	}

	switch (launch_data_get_type(ld)) {
	case LAUNCH_DATA_DICTIONARY:
		xo = xpc_dictionary_create(NULL, NULL, 0);
		launch_data_dict_iterate(ld, ld2xpc_dict_add, xo);
		break;
	case LAUNCH_DATA_ARRAY:
		xo = xpc_array_create(NULL, 0);
		n = launch_data_array_get_count(ld);
		for (i = 0; i < n; i++) {
			xpc_object_t xv = ld2xpc(launch_data_array_get_index(ld, i));

			if (xv != NULL) {
				xpc_array_append_value(xo, xv);
				xpc_release(xv);
			}
		}
		break;
	case LAUNCH_DATA_FD:
		xo = xpc_fd_create(launch_data_get_fd(ld));
		break;
	case LAUNCH_DATA_INTEGER:
		xo = xpc_int64_create(launch_data_get_integer(ld));
		break;
	case LAUNCH_DATA_REAL:
		xo = xpc_double_create(launch_data_get_real(ld));
		break;
	case LAUNCH_DATA_BOOL:
		xo = xpc_bool_create(launch_data_get_bool(ld));
		break;
	case LAUNCH_DATA_STRING:
		xo = xpc_string_create(launch_data_get_string(ld));
		break;
	case LAUNCH_DATA_OPAQUE:
		xo = xpc_data_create(launch_data_get_opaque(ld),
		    launch_data_get_opaque_size(ld));
		break;
	case LAUNCH_DATA_ERRNO:
		xo = xpc_int64_create(launch_data_get_errno(ld));
		break;
	default:
		/* Mach ports have no public XPC value type. */
		break;
	}

	return xo;
}

kern_return_t
xpc_call_wakeup(mach_port_t rport, int error)
{
	mig_reply_error_t reply;

	memset(&reply, 0, sizeof(reply));
	reply.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
	reply.Head.msgh_size = sizeof(reply);
	reply.Head.msgh_remote_port = rport;
	reply.Head.msgh_local_port = MACH_PORT_NULL;
	reply.NDR = NDR_record;
	reply.RetCode = error;

	return mach_msg(&reply.Head, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
	    sizeof(reply), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
}
