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

#ifndef __XPC_PEER_REQ_H__
#define __XPC_PEER_REQ_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

XPC_SWIFT_NOEXPORT
XPC_DECL(xpc_peer_requirement);

#pragma mark Constructors

/*!
 * @function xpc_peer_requirement_create_entitlement_exists
 * Creates a requirement satisfied by any peer that holds the named
 * entitlement, regardless of value.
 *
 * @param entitlement
 * The entitlement name, for example "com.example.app.extension".
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new requirement, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED
xpc_peer_requirement_t _Nullable
xpc_peer_requirement_create_entitlement_exists(const char *entitlement,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_peer_requirement_create_entitlement_matches_value
 * Creates a requirement satisfied by any peer whose value for the named
 * entitlement is equal to the given value.
 *
 * @param entitlement
 * The entitlement name.
 *
 * @param value
 * The value the entitlement must match.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new requirement, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED
xpc_peer_requirement_t _Nullable
xpc_peer_requirement_create_entitlement_matches_value(const char *entitlement,
		xpc_object_t value,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_peer_requirement_create_team_identity
 * Creates a requirement satisfied by any peer signed by the team of the
 * given identifier, or by any team when signing_identifier is NULL.
 *
 * @param signing_identifier
 * The team name, or NULL to accept any team.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new requirement, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED
xpc_peer_requirement_t _Nullable
xpc_peer_requirement_create_team_identity(
		const char * _Nullable signing_identifier,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_peer_requirement_create_platform_identity
 * Creates a requirement satisfied by any platform-signed peer, optionally
 * restricting to the team of the given identifier.
 *
 * @param signing_identifier
 * The team name, or NULL to accept any platform-signed peer.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new requirement, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED
xpc_peer_requirement_t _Nullable
xpc_peer_requirement_create_platform_identity(
		const char * _Nullable signing_identifier,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

/*!
 * @function xpc_peer_requirement_create_lwcr
 * Creates a requirement satisfied by any peer whose lightweight code
 * requirement is equal to the given object.
 *
 * @param lwcr
 * The lightweight code requirement object.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * A new requirement, or NULL on failure.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT XPC_RETURNS_RETAINED
xpc_peer_requirement_t _Nullable
xpc_peer_requirement_create_lwcr(xpc_object_t lwcr,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

#pragma mark Matching Peer Requirement on Received Messages

/*!
 * @function xpc_peer_requirement_match_received_message
 * Evaluates a peer requirement against the peer that sent the given message.
 *
 * @param peer_requirement
 * The requirement to evaluate.
 *
 * @param message
 * The received message whose sender should be checked.
 *
 * @param error_out
 * On failure, receives the rich error describing the failure.
 *
 * @result
 * Whether the sender of the message satisfies the requirement.
 */
XPC_EXPORT XPC_SWIFT_NOEXPORT
bool
xpc_peer_requirement_match_received_message(xpc_peer_requirement_t peer_requirement,
		xpc_object_t message,
		xpc_rich_error_t _Nullable XPC_GIVES_REFERENCE * _Nullable error_out);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __XPC_PEER_REQ_H__ */