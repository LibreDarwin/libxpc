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

#ifndef __XPC_SESSION_H__
#define __XPC_SESSION_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

#ifndef __BLOCKS__
#error "XPC Session require Blocks support."
#endif /* __BLOCKS__ */

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

#pragma mark Constants

XPC_SWIFT_NOEXPORT
XPC_FLAGS_ENUM(xpc_session_create_flags, uint64_t,
	XPC_SESSION_CREATE_NONE XPC_SWIFT_NAME("none") = 0,
	XPC_SESSION_CREATE_INACTIVE XPC_SWIFT_NAME("inactive") = (1 << 0),
	XPC_SESSION_CREATE_MACH_PRIVILEGED XPC_SWIFT_NAME("privileged") = (1 << 1)
);

#pragma mark Handlers

/*!
 * @typedef xpc_session_cancel_handler_t
 * Invoked when the session has been cancelled and is being torn down.
 */
typedef void (^xpc_session_cancel_handler_t)(xpc_rich_error_t error) XPC_SWIFT_NOEXPORT;

/*!
 * @typedef xpc_session_incoming_message_handler_t
 * Invoked for each message received from the peer while no reply to a
 * previous message is pending.
 */
typedef void (^xpc_session_incoming_message_handler_t)(xpc_object_t message) XPC_SWIFT_NOEXPORT;

/*!
 * @typedef xpc_session_reply_handler_t
 * Invoked when the reply to a message sent with
 * xpc_session_send_message_with_reply_async() arrives.
 */
typedef void (^xpc_session_reply_handler_t)(xpc_object_t _Nullable reply,
		xpc_rich_error_t _Nullable error) XPC_SWIFT_NOEXPORT;

#pragma mark Helpers

/*!
 * @function xpc_session_copy_description
 * Returns a human-readable description of the session. The caller is
 * responsible for releasing the returned string with free(3).
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_WARN_RESULT
char * _Nullable
xpc_session_copy_description(xpc_session_t session);

#pragma mark Client Session Creation

/*!
 * @function xpc_session_create_xpc_service
 * Creates a client session for the named XPC service.
 *
 * @param name
 * The service name, whose binary is found in the main bundle.
 *
 * @param target_queue
 * The queue on which session handlers are delivered. If NULL, the internal
 * target queue is used.
 *
 * @param flags
 * XPC_SESSION_CREATE_* flags controlling session behavior.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new session, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_session_t _Nullable
xpc_session_create_xpc_service(const char *name,
		dispatch_queue_t _Nullable target_queue,
		xpc_session_create_flags_t flags,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_session_create_mach_service
 * Creates a client session for the named Mach service.
 *
 * @param mach_service
 * The Mach service name.
 *
 * @param target_queue
 * The queue on which session handlers are delivered. If NULL, the internal
 * target queue is used.
 *
 * @param flags
 * XPC_SESSION_CREATE_* flags controlling session behavior.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new session, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_session_t _Nullable
xpc_session_create_mach_service(const char *mach_service,
		dispatch_queue_t _Nullable target_queue,
		xpc_session_create_flags_t flags,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

#pragma mark Session Configuration

/*!
 * @function xpc_session_set_incoming_message_handler
 * Sets the handler invoked for each message received from the peer.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_session_set_incoming_message_handler(xpc_session_t session,
		xpc_session_incoming_message_handler_t handler);

/*!
 * @function xpc_session_set_cancel_handler
 * Sets the handler invoked when the session is cancelled.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_session_set_cancel_handler(xpc_session_t session,
		xpc_session_cancel_handler_t cancel_handler);

/*!
 * @function xpc_session_set_target_queue
 * Changes the queue on which session handlers are delivered.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_session_set_target_queue(xpc_session_t session,
		dispatch_queue_t _Nullable target_queue);

#pragma mark Lifecycle

/*!
 * @function xpc_session_activate
 * Activates the session. This must be the first call on the session.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
bool
xpc_session_activate(xpc_session_t session,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_session_cancel
 * Cancels the session. The session becomes invalid, the peer is notified,
 * and the cancel handler is invoked once the teardown completes.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_session_cancel(xpc_session_t session);

#pragma mark Message Send

/*!
 * @function xpc_session_send_message
 * Sends a message to the peer.
 *
 * @result
 * NULL on success, or the rich error describing the failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_rich_error_t _Nullable
xpc_session_send_message(xpc_session_t session, xpc_object_t message);

/*!
 * @function xpc_session_send_message_with_reply_sync
 * Sends a message to the peer and blocks until the reply arrives.
 *
 * @result
 * The reply, or NULL on failure with error_out populated.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_object_t _Nullable
xpc_session_send_message_with_reply_sync(xpc_session_t session,
		xpc_object_t message, xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_session_send_message_with_reply_async
 * Sends a message to the peer and invokes reply_handler with the reply.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
void
xpc_session_send_message_with_reply_async(xpc_session_t session,
		xpc_object_t message, xpc_session_reply_handler_t reply_handler);

/*!
 * @function xpc_session_set_peer_code_signing_requirement
 * Sets a code-signing requirement the peer must satisfy. Returns 0 on
 * success.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_session_set_peer_code_signing_requirement(xpc_session_t session, const char *requirement);

/*!
 * @function xpc_session_set_peer_requirement
 * Sets a peer requirement the peer must satisfy; failures terminate the
 * session.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_NONNULL_ALL
void
xpc_session_set_peer_requirement(xpc_session_t session, xpc_peer_requirement_t requirement);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __XPC_SESSION_H__ */