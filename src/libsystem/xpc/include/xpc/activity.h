/*
 * Copyright (c) 2026, xnuports
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This header is part of the xnuports reimplementation of the XPC public
 * API. Function signatures and types match the Apple SDK contract for
 * source compatibility; the text is original.
 */

#ifndef __XPC_ACTIVITY_H__
#define __XPC_ACTIVITY_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

#ifdef __BLOCKS__

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

/* Dictionary keys for the XPC activity criteria dictionary. */

#define XPC_ACTIVITY_INTERVAL "XPC_ACTIVITY_INTERVAL"
#define XPC_ACTIVITY_REPEATING "XPC_ACTIVITY_REPEATING"
#define XPC_ACTIVITY_DELAY "XPC_ACTIVITY_DELAY"
#define XPC_ACTIVITY_GRACE_PERIOD "XPC_ACTIVITY_GRACE_PERIOD"

/* Predefined interval constants. */

#define XPC_ACTIVITY_INTERVAL_1_MIN (1 * 60)
#define XPC_ACTIVITY_INTERVAL_5_MIN (5 * 60)
#define XPC_ACTIVITY_INTERVAL_15_MIN (15 * 60)
#define XPC_ACTIVITY_INTERVAL_30_MIN (30 * 60)
#define XPC_ACTIVITY_INTERVAL_1_HOUR (60 * 60)
#define XPC_ACTIVITY_INTERVAL_4_HOURS (4 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_8_HOURS (8 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_1_DAY (24 * 60 * 60)
#define XPC_ACTIVITY_INTERVAL_7_DAYS (7 * 24 * 60 * 60)

/* Criteria keys controlling scheduling and priority. */

#define XPC_ACTIVITY_PRIORITY "XPC_ACTIVITY_PRIORITY"
#define XPC_ACTIVITY_PRIORITY_MAINTENANCE "XPC_ACTIVITY_PRIORITY_MAINTENANCE"
#define XPC_ACTIVITY_PRIORITY_UTILITY "XPC_ACTIVITY_PRIORITY_UTILITY"
#define XPC_ACTIVITY_ALLOW_BATTERY "XPC_ACTIVITY_ALLOW_BATTERY"
#define XPC_ACTIVITY_REQUIRE_SCREEN_SLEEP "XPC_ACTIVITY_REQUIRE_SCREEN_SLEEP" /* bool */
#define XPC_ACTIVITY_PREVENT_DEVICE_SLEEP "XPC_ACTIVITY_PREVENT_DEVICE_SLEEP" /* bool */

/* Deprecated, unimplemented criteria keys. */

#define XPC_ACTIVITY_REQUIRE_BATTERY_LEVEL "XPC_ACTIVITY_REQUIRE_BATTERY_LEVEL" /* int (%) */
#define XPC_ACTIVITY_REQUIRE_HDD_SPINNING "XPC_ACTIVITY_REQUIRE_HDD_SPINNING" /* bool */

#define XPC_TYPE_ACTIVITY (&_xpc_type_activity)
XPC_EXPORT
XPC_TYPE(_xpc_type_activity);
XPC_DECL(xpc_activity);

/*!
 * @typedef xpc_activity_handler_t
 * The block invoked to run or evaluate an XPC activity.
 */
XPC_NONNULL1
typedef void (^xpc_activity_handler_t)(xpc_activity_t activity);

/* Global constant typed so handlers can compare against it with ==. */
XPC_EXPORT
const xpc_object_t XPC_ACTIVITY_CHECK_IN;

/*!
 * @function xpc_activity_register
 * Registers a handler for an XPC activity identified by the given name and
 * criteria dictionary. The handler is invoked by the system when the
 * activity fires.
 */
XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2 XPC_NONNULL3
void
xpc_activity_register(const char *identifier, xpc_object_t criteria,
	xpc_activity_handler_t handler);

/*!
 * @function xpc_activity_copy_criteria
 * Returns a copy of the criteria dictionary for the given activity.
 */
XPC_EXPORT XPC_WARN_RESULT XPC_RETURNS_RETAINED XPC_NONNULL1
xpc_object_t _Nullable
xpc_activity_copy_criteria(xpc_activity_t activity);

/*!
 * @function xpc_activity_set_criteria
 * Updates the criteria dictionary for the given activity.
 */
XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_activity_set_criteria(xpc_activity_t activity, xpc_object_t criteria);

enum {
	XPC_ACTIVITY_STATE_CHECK_IN,
	XPC_ACTIVITY_STATE_WAIT,
	XPC_ACTIVITY_STATE_RUN,
	XPC_ACTIVITY_STATE_DEFER,
	XPC_ACTIVITY_STATE_CONTINUE,
	XPC_ACTIVITY_STATE_DONE,
};
typedef long xpc_activity_state_t;

/*!
 * @function xpc_activity_get_state
 * Returns the current state of the given activity.
 */
XPC_EXPORT XPC_WARN_RESULT XPC_NONNULL1
xpc_activity_state_t
xpc_activity_get_state(xpc_activity_t activity);

/*!
 * @function xpc_activity_set_state
 * Transitions the activity to a new state. Returns whether the transition
 * was accepted.
 */
XPC_EXPORT XPC_WARN_RESULT XPC_NONNULL1
bool
xpc_activity_set_state(xpc_activity_t activity, xpc_activity_state_t state);

/*!
 * @function xpc_activity_should_defer
 * Returns whether the activity should be deferred because the system needs
 * its resources (for example, the battery is low).
 */
XPC_EXPORT XPC_WARN_RESULT XPC_NONNULL1
bool
xpc_activity_should_defer(xpc_activity_t activity);

/*!
 * @function xpc_activity_unregister
 * Unregisters the handler for the named activity.
 */
XPC_EXPORT XPC_NONNULL1
void
xpc_activity_unregister(const char *identifier);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __BLOCKS__ */

#endif /* __XPC_ACTIVITY_H__ */