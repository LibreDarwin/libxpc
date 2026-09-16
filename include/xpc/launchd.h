/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 */

/*
 * xpc/launchd.h -- the launchd side of libxpc's private interface.
 *
 * launchd-842 includes this for the event, process and jetsam routines it
 * serves and the few libxpc calls it makes.  Apple shipped it only in the
 * 10.9 internal SDK, and the modern internal SDK's xpc/private.h no longer
 * carries most of it, so it is written here.
 *
 * The routine keys, operation codes and service types are a contract
 * between our launchd and our libxpc, which asks launchd for them; they
 * are not Apple's values, and libxpc's client side must use these.
 * xpc_service_type_t itself comes from xpc/private.h.
 */

#ifndef __XPC_LAUNCHD_H__
#define __XPC_LAUNCHD_H__

#include <mach/mach.h>
#include <xpc/xpc.h>

__BEGIN_DECLS

#define EXNOERROR	0
#define EXNOMEM		1
#define EXINVAL		2
#define EXSRCH		3
#define EXMAX		EXSRCH

typedef char event_name_t[64];
typedef int xpc_jetsam_band_t;

enum {
	XPC_SERVICE_TYPE_LAUNCHD = 1,
	XPC_SERVICE_TYPE_BUNDLED,
	XPC_SERVICE_TYPE_APP,
};

/* In launchd's priority map order; XPC_JETSAM_BAND_LAST bounds the range. */
enum {
	XPC_JETSAM_BAND_SUSPENDED = 10,
	XPC_JETSAM_BAND_BACKGROUND_OPPORTUNISTIC,
	XPC_JETSAM_BAND_BACKGROUND,
	XPC_JETSAM_BAND_MAIL,
	XPC_JETSAM_BAND_PHONE,
	XPC_JETSAM_BAND_UI_SUPPORT,
	XPC_JETSAM_BAND_FOREGROUND_SUPPORT,
	XPC_JETSAM_BAND_FOREGROUND,
	XPC_JETSAM_BAND_AUDIO,
	XPC_JETSAM_BAND_ACCESSORY,
	XPC_JETSAM_BAND_CRITICAL,
	XPC_JETSAM_BAND_TELEPHONY,
	XPC_JETSAM_BAND_LAST,
};

/* Event routine operations. */
enum {
	XPC_EVENT_GET_NAME,
	XPC_EVENT_SET,
	XPC_EVENT_COPY,
	XPC_EVENT_CHECK_IN,
	XPC_EVENT_LOOK_UP,
	XPC_EVENT_PROVIDER_CHECK_IN,
	XPC_EVENT_PROVIDER_SET_STATE,
	XPC_EVENT_COPY_ENTITLEMENTS,
};

/* Process routine operations. */
enum {
	XPC_PROCESS_JETSAM_SET_BAND,
	XPC_PROCESS_JETSAM_SET_MEMORY_LIMIT,
	XPC_PROCESS_SERVICE_ATTACH,
	XPC_PROCESS_SERVICE_DETACH,
	XPC_PROCESS_SERVICE_GET_PROPERTIES,
	XPC_PROCESS_SERVICE_KILL,
};

#define XPC_EVENT_FLAG_ENTITLEMENTS			0x1

#define XPC_EVENT_ROUTINE_KEY_STREAM			"stream"
#define XPC_EVENT_ROUTINE_KEY_TOKEN			"token"
#define XPC_EVENT_ROUTINE_KEY_NAME			"name"
#define XPC_EVENT_ROUTINE_KEY_ENTITLEMENTS		"entitlements"
#define XPC_EVENT_ROUTINE_KEY_EVENT			"event"
#define XPC_EVENT_ROUTINE_KEY_EVENTS			"events"
#define XPC_EVENT_ROUTINE_KEY_FLAGS			"flags"
#define XPC_EVENT_ROUTINE_KEY_PORT			"port"
#define XPC_EVENT_ROUTINE_KEY_STATE			"state"
#define XPC_EVENT_ROUTINE_KEY_OP			"op"
#define XPC_EVENT_ROUTINE_KEY_ERROR			"error"

#define XPC_PROCESS_ROUTINE_KEY_ARGV			"argv"
#define XPC_PROCESS_ROUTINE_KEY_ERROR			"error"
#define XPC_PROCESS_ROUTINE_KEY_HANDLE			"handle"
#define XPC_PROCESS_ROUTINE_KEY_LABEL			"label"
#define XPC_PROCESS_ROUTINE_KEY_MEMORY_LIMIT		"memory-limit"
#define XPC_PROCESS_ROUTINE_KEY_NAME			"name"
#define XPC_PROCESS_ROUTINE_KEY_NEW_INSTANCE_PORT	"new-instance-port"
#define XPC_PROCESS_ROUTINE_KEY_OP			"op"
#define XPC_PROCESS_ROUTINE_KEY_PATH			"path"
#define XPC_PROCESS_ROUTINE_KEY_PID			"pid"
#define XPC_PROCESS_ROUTINE_KEY_PRIORITY_BAND		"priority-band"
#define XPC_PROCESS_ROUTINE_KEY_RCDATA			"rcdata"
#define XPC_PROCESS_ROUTINE_KEY_TYPE			"type"

#define XPC_SERVICE_ENTITLEMENT_ATTACH	"com.apple.private.xpc.attach"
#define XPC_SERVICE_RENDEZVOUS_TOKEN	"XPC_SERVICE_RENDEZVOUS_TOKEN"
#define XPC_SERVICE_ENV_ATTACHED	"XPC_SERVICE_ENV_ATTACHED"

const char *xpc_strerror(int error);

/* launchd's own SPI; src/launchd/xpc_launchd.c. */
kern_return_t xpc_call_wakeup(mach_port_t rport, int error);
struct _launch_data;
xpc_object_t ld2xpc(struct _launch_data *data);

__END_DECLS

#endif /* __XPC_LAUNCHD_H__ */
