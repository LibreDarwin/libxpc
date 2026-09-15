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

#ifndef __XPC_RICH_ERROR_H__
#define __XPC_RICH_ERROR_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

#ifndef __BLOCKS__
#error "XPC Rich Errors require Blocks support."
#endif /* __BLOCKS__ */

XPC_ASSUME_NONNULL_BEGIN
__BEGIN_DECLS

#pragma mark Properties

/*!
 * @function xpc_rich_error_copy_description
 * Copies the string description of an error.
 *
 * @param error
 * The error to be examined.
 *
 * @result
 * The underlying C string for the provided error. This string should be
 * released with free(3) when done. Returns NULL if a string description could
 * not be generated.
 */
XPC_EXPORT XPC_WARN_RESULT
char * _Nullable
xpc_rich_error_copy_description(xpc_rich_error_t error);

/*!
 * @function xpc_rich_error_can_retry
 * Whether the operation the error originated from can be retried.
 *
 * @param error
 * The error to be inspected.
 *
 * @result
 * Whether the operation the error originated from can be retried.
 */
XPC_EXPORT XPC_WARN_RESULT
bool
xpc_rich_error_can_retry(xpc_rich_error_t error);

__END_DECLS
XPC_ASSUME_NONNULL_END

#endif /* __XPC_RICH_ERROR_H__ */