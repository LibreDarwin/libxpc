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

#ifndef __XPC_LISTENER_H__
#define __XPC_LISTENER_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

#ifndef __BLOCKS__
#error "XPC Listener require Blocks support."
#endif /* __BLOCKS__ */

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

#pragma mark Constants

/*!
 * @typedef xpc_listener_create_flags_t
 * Attributes used to control the behavior of a listener created with
 * xpc_listener_create().
 */
XPC_SWIFT_NOEXPORT
XPC_FLAGS_ENUM(xpc_listener_create_flags, uint64_t,
	XPC_LISTENER_CREATE_NONE XPC_SWIFT_NAME("none") = 0,
	XPC_LISTENER_CREATE_INACTIVE XPC_SWIFT_NAME("inactive") = (1 << 0),
	XPC_LISTENER_CREATE_FORCE_MACH XPC_SWIFT_NAME("mach") = (1 << 1),
	XPC_LISTENER_CREATE_FORCE_XPCSERVICE XPC_SWIFT_NAME("xpcservice") = (1 << 2),
);

#pragma mark Handlers

/*!
 * @typedef xpc_listener_incoming_session_handler_t
 * Invoked when a peer is attempting to establish a session with this
 * listener. The peer session is automatically accepted and activated when
 * the handler returns, unless it was explicitly rejected with
 * xpc_listener_reject_peer() or cancelled with xpc_session_cancel().
 * Before returning, the handler must set a message handler on the peer
 * session with xpc_session_set_incoming_message_handler(), or cancel the
 * session.
 */
typedef void (^xpc_listener_incoming_session_handler_t)(xpc_session_t peer);

#pragma mark Helpers

/*!
 * @function xpc_listener_copy_description
 * Returns a human-readable description of the listener. The caller is
 * responsible for releasing the returned string with free(3).
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_WARN_RESULT
char * _Nullable
xpc_listener_copy_description(xpc_listener_t listener);

#pragma mark Listener Creation

/*!
 * @function xpc_listener_create
 * Creates a listener for the Mach service or XPC service named by
 * service.
 *
 * @param service
 * The Mach service or XPC service name to create the listener with.
 *
 * @param target_queue
 * The queue onto which listener events are submitted. This may be a
 * concurrent queue, and may be NULL, in which case libdispatch's default
 * target queue is used.
 *
 * @param flags
 * XPC_LISTENER_CREATE_* flags controlling listener behavior.
 *
 * @param incoming_session_handler
 * The handler invoked for each incoming session. This parameter is
 * mandatory.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new, pre-activated listener, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_listener_t _Nullable
xpc_listener_create(const char *service,
		dispatch_queue_t _Nullable target_queue,
		xpc_listener_create_flags_t flags,
		xpc_listener_incoming_session_handler_t incoming_session_handler,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

#pragma mark Lifecycle

/*!
 * @function xpc_listener_activate
 * Activates the listener. A listener created with
 * XPC_LISTENER_CREATE_INACTIVE must be activated before it will accept
 * incoming sessions; activating an already-active listener is undefined.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
bool
xpc_listener_activate(xpc_listener_t listener,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_listener_cancel
 * Cancels the listener. The listener becomes invalid, pending sessions are
 * terminated, and the listener is torn down once all in-flight handlers
 * have returned.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_listener_cancel(xpc_listener_t listener);

/*!
 * @function xpc_listener_reject_peer
 * Rejects the given peer session, terminating it. Must be called from
 * within an incoming session handler.
 *
 * @param peer
 * The peer session to reject.
 *
 * @param reason
 * A human-readable reason for the rejection.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_listener_reject_peer(xpc_session_t peer, const char *reason);

#pragma mark Peer Requirements

/*!
 * @function xpc_listener_set_peer_code_signing_requirement
 * Sets a code-signing requirement every incoming peer must satisfy;
 * session establishment fails for peers that do not match. Returns 0 on
 * success.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_listener_set_peer_code_signing_requirement(xpc_listener_t listener, const char *requirement);

/*!
 * @function xpc_listener_set_peer_requirement
 * Sets a peer requirement every incoming peer must satisfy. It is a
 * programming error to call this more than once on a given listener.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_NONNULL_ALL
void
xpc_listener_set_peer_requirement(xpc_listener_t listener, xpc_peer_requirement_t requirement);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __XPC_LISTENER_H__ */