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

#ifndef __XPC_CONNECTION_H__
#define __XPC_CONNECTION_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

#ifndef __BLOCKS__
#error "XPC connections require Blocks support."
#endif /* __BLOCKS__ */

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

/* Well-known error dictionaries sent to a connection's event handler. */

#define XPC_ERROR_CONNECTION_INTERRUPTED \
	XPC_GLOBAL_OBJECT(_xpc_error_connection_interrupted)
XPC_EXPORT
const struct _xpc_dictionary_s _xpc_error_connection_interrupted;

#define XPC_ERROR_CONNECTION_INVALID \
	XPC_GLOBAL_OBJECT(_xpc_error_connection_invalid)
XPC_EXPORT
const struct _xpc_dictionary_s _xpc_error_connection_invalid;

#define XPC_ERROR_TERMINATION_IMMINENT \
	XPC_GLOBAL_OBJECT(_xpc_error_termination_imminent)
XPC_EXPORT
const struct _xpc_dictionary_s _xpc_error_termination_imminent;

#define XPC_ERROR_PEER_CODE_SIGNING_REQUIREMENT \
	XPC_GLOBAL_OBJECT(_xpc_error_peer_code_signing_requirement)
XPC_EXPORT
const struct _xpc_dictionary_s _xpc_error_peer_code_signing_requirement;

/* Flags for xpc_connection_create_mach_service(). */

#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1 << 0)
#define XPC_CONNECTION_MACH_SERVICE_PRIVILEGED (1 << 1)

/*!
 * @typedef xpc_finalizer_t
 * Called with the connection's context when the connection is deallocated.
 */
typedef void (*xpc_finalizer_t)(void * _Nullable value);

/*!
 * @function xpc_connection_create
 * Creates a connection to a named service.
 *
 * @param name
 * The service name. Pass the NULL pointer to create an anonymous connection
 * suitable for use as a listener or for embedding an endpoint into a message.
 *
 * @param targetq
 * The queue on which the connection's event handler should be invoked. If
 * NULL, the target queue defaults to the main queue.
 *
 * @result
 * A new connection object. Starts suspended; call xpc_connection_resume()
 * before sending messages.
 */
XPC_EXPORT XPC_MALLOC XPC_RETURNS_RETAINED XPC_WARN_RESULT
xpc_connection_t
xpc_connection_create(const char * _Nullable name,
	dispatch_queue_t _Nullable targetq);

/*!
 * @function xpc_connection_create_mach_service
 * Creates a connection to a Mach service, or a listener for a Mach service.
 *
 * @param name
 * The Mach service name. For a listener, the service name must not be
 * prefixed with the application's service namespace.
 *
 * @param targetq
 * The queue on which the connection's event handler should be invoked.
 *
 * @param flags
 * XPC_CONNECTION_MACH_SERVICE_LISTENER for a listener, optionally OR'd with
 * XPC_CONNECTION_MACH_SERVICE_PRIVILEGED.
 *
 * @result
 * A new connection object.
 */
XPC_EXPORT XPC_MALLOC XPC_RETURNS_RETAINED XPC_WARN_RESULT XPC_NONNULL1
xpc_connection_t
xpc_connection_create_mach_service(const char *name,
	dispatch_queue_t _Nullable targetq, uint64_t flags);

/*!
 * @function xpc_connection_create_from_endpoint
 * Creates a connection from an endpoint that was embedded in a message.
 *
 * @param endpoint
 * The endpoint received from the remote process.
 *
 * @result
 * A new connection object for the peer that supplied the endpoint.
 */
XPC_EXPORT XPC_MALLOC XPC_RETURNS_RETAINED XPC_WARN_RESULT XPC_NONNULL_ALL
xpc_connection_t
xpc_connection_create_from_endpoint(xpc_endpoint_t endpoint);

/*!
 * @function xpc_connection_set_target_queue
 * Sets the queue on which the connection's event handler and reply blocks
 * are delivered.
 */
XPC_EXPORT XPC_NONNULL1
void
xpc_connection_set_target_queue(xpc_connection_t connection,
	dispatch_queue_t _Nullable targetq);

/*!
 * @function xpc_connection_set_event_handler
 * Sets the handler invoked for messages and errors on this connection.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_set_event_handler(xpc_connection_t connection,
	XPC_SWIFT_SENDABLE xpc_handler_t handler);

/*!
 * @function xpc_connection_activate
 * Activates the connection. Must be called on a suspended connection before
 * it will deliver events.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_activate(xpc_connection_t connection);

/*!
 * @function xpc_connection_suspend
 * Suspends the connection, deferring message delivery and event dispatch.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_suspend(xpc_connection_t connection);

/*!
 * @function xpc_connection_resume
 * Resumes a suspended connection. The first call also activates it.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_resume(xpc_connection_t connection);

/*!
 * @function xpc_connection_send_message
 * Sends a message over the connection.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message);

/*!
 * @function xpc_connection_send_barrier
 * Schedules a barrier block, executed after all previously sent messages
 * have been delivered.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_send_barrier(xpc_connection_t connection,
	dispatch_block_t barrier);

/*!
 * @function xpc_connection_send_message_with_reply
 * Sends a message with a reply handler.
 */
XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2 XPC_NONNULL4
void
xpc_connection_send_message_with_reply(xpc_connection_t connection,
	xpc_object_t message, dispatch_queue_t _Nullable replyq,
	XPC_SWIFT_SENDABLE xpc_handler_t handler);

/*!
 * @function xpc_connection_send_message_with_reply_sync
 * Sends a message and blocks the calling thread until the reply arrives.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT XPC_RETURNS_RETAINED
xpc_object_t
xpc_connection_send_message_with_reply_sync(xpc_connection_t connection,
	xpc_object_t message);

/*!
 * @function xpc_connection_cancel
 * Cancels the connection. Pending messages are discarded and the connection
 * becomes invalid.
 */
XPC_EXPORT XPC_NONNULL_ALL
void
xpc_connection_cancel(xpc_connection_t connection);

/*!
 * @function xpc_connection_get_name
 * Returns the name the connection was created with, or NULL for anonymous
 * connections.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
const char * _Nullable
xpc_connection_get_name(xpc_connection_t connection);

/*!
 * @function xpc_connection_get_euid
 * Returns the effective user ID of the remote process.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
uid_t
xpc_connection_get_euid(xpc_connection_t connection);

/*!
 * @function xpc_connection_get_egid
 * Returns the effective group ID of the remote process.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
gid_t
xpc_connection_get_egid(xpc_connection_t connection);

/*!
 * @function xpc_connection_get_pid
 * Returns the process identifier of the remote process.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
pid_t
xpc_connection_get_pid(xpc_connection_t connection);

/*!
 * @function xpc_connection_get_asid
 * Returns the audit session identifier of the remote process.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
au_asid_t
xpc_connection_get_asid(xpc_connection_t connection);

/*!
 * @function xpc_connection_set_context
 * Associates an opaque context pointer with the connection.
 */
XPC_EXPORT XPC_NONNULL1
void
xpc_connection_set_context(xpc_connection_t connection,
	void * _Nullable context);

/*!
 * @function xpc_connection_get_context
 * Returns the context pointer associated with the connection.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
void * _Nullable
xpc_connection_get_context(xpc_connection_t connection);

/*!
 * @function xpc_connection_set_finalizer_f
 * Sets the finalizer invoked with the context when the connection is
 * deallocated.
 */
XPC_EXPORT XPC_NONNULL1
void
xpc_connection_set_finalizer_f(xpc_connection_t connection,
	xpc_finalizer_t _Nullable finalizer);

/*!
 * @function xpc_connection_set_peer_code_signing_requirement
 * Sets a code-signing requirement the peer must satisfy, in the text form
 * understood by SecRequirementCreateWithString. Returns 0 on success.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_connection_set_peer_code_signing_requirement(xpc_connection_t connection, const char *requirement);

/*!
 * @function xpc_connection_set_peer_entitlement_exists_requirement
 * Requires that the peer hold the given entitlement, regardless of value.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_connection_set_peer_entitlement_exists_requirement(xpc_connection_t connection, const char *entitlement);

/*!
 * @function xpc_connection_set_peer_entitlement_matches_value_requirement
 * Requires that the peer hold the given entitlement with the given value.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_connection_set_peer_entitlement_matches_value_requirement(xpc_connection_t connection, const char *entitlement, xpc_object_t value);

/*!
 * @function xpc_connection_set_peer_team_identity_requirement
 * Requires that the peer be signed by a team of the given identifier, or any
 * team if signing_identifier is NULL.
 */
XPC_EXPORT XPC_NONNULL1 XPC_WARN_RESULT
int
xpc_connection_set_peer_team_identity_requirement(xpc_connection_t connection, const char * _Nullable signing_identifier);

/*!
 * @function xpc_connection_set_peer_platform_identity_requirement
 * Requires that the peer be a platform binary, optionally of a specific team.
 */
XPC_EXPORT XPC_NONNULL1 XPC_WARN_RESULT
int
xpc_connection_set_peer_platform_identity_requirement(xpc_connection_t connection, const char * _Nullable signing_identifier);

/*!
 * @function xpc_connection_set_peer_lightweight_code_requirement
 * Requires that the peer satisfy the given lightweight code requirement
 * object.
 */
XPC_EXPORT XPC_NONNULL_ALL XPC_WARN_RESULT
int
xpc_connection_set_peer_lightweight_code_requirement(xpc_connection_t connection, xpc_object_t lwcr);

/*!
 * @function xpc_connection_set_peer_requirement
 * Requires that the peer satisfy the given peer requirement object.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_NONNULL_ALL
void
xpc_connection_set_peer_requirement(xpc_connection_t connection,
		xpc_peer_requirement_t peer_requirement);

/*!
 * @function xpc_connection_copy_invalidation_reason
 * Returns a string describing why the connection became invalid, or NULL if
 * the connection is not invalid. The caller is responsible for releasing the
 * returned string with free(3).
 */
XPC_EXPORT XPC_NONNULL1 XPC_WARN_RESULT
char * _Nullable
xpc_connection_copy_invalidation_reason(xpc_connection_t connection);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __XPC_CONNECTION_H__ */