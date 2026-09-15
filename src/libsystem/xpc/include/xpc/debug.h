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

#ifndef __XPC_DEBUG_H__
#define __XPC_DEBUG_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
// For HeaderDoc.
#include <xpc/base.h>
#endif /* __XPC_INDIRECT__ */

__BEGIN_DECLS

/*!
 * @function xpc_debugger_api_misuse_info
 * Returns a string describing the reason XPC aborted the calling process. On
 * macOS this is the same string found in the "Application Specific
 * Information" section of the crash report.
 *
 * @result
 * A human-readable string describing the reason the caller was aborted, or
 * NULL if XPC was not responsible for the program's termination.
 *
 * @discussion
 * This function is only callable from within a debugger. It is not meant to be
 * called by the program directly.
 */
XPC_DEBUGGER_EXCL
const char *
xpc_debugger_api_misuse_info(void);

__END_DECLS

#endif /* __XPC_DEBUG_H__ */