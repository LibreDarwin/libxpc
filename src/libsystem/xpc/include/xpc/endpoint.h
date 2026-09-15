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

#ifndef __XPC_ENDPOINT_H__
#define __XPC_ENDPOINT_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

__BEGIN_DECLS

/*!
 * @function xpc_endpoint_create
 * Creates an endpoint from a connection, suitable for embedding into messages.
 *
 * @param connection
 * Only connections obtained through calls to xpc_connection_create*() may be
 * given to this API. Passing any other kind of connection is unsupported and
 * results in undefined behavior.
 *
 * @result
 * A new endpoint object.
 */
XPC_EXPORT XPC_MALLOC XPC_RETURNS_RETAINED XPC_WARN_RESULT XPC_NONNULL1
xpc_endpoint_t _Nonnull
xpc_endpoint_create(xpc_connection_t _Nonnull connection);

__END_DECLS

#endif /* __XPC_ENDPOINT_H__ */