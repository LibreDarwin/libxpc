/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * xpc/base.h — attribute and declaration shims for the reimplemented XPC
 * framework.
 *
 * Mirrors the public surface of Apple's xpc/base.h so that source written
 * against the SDK compiles unchanged, but stands on its own: no dependency
 * on <os/base.h>, <os/object.h>, or any other Apple private header.  This is
 * an independent reimplementation, not Apple's code.
 *
 * Consumers must include <xpc/xpc.h> (the umbrella), never this file
 * directly.
 */

#ifndef __XPC_BASE_H__
#define __XPC_BASE_H__

#ifndef __XPC_INDIRECT__
#error "Please #include <xpc/xpc.h> instead of this file directly."
#endif /* __XPC_INDIRECT__ */

#include <sys/cdefs.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compiler feature probes used by the attribute ladder below. */
#if !defined(__has_attribute)
#define __has_attribute(x) 0
#endif
#if !defined(__has_feature)
#define __has_feature(x) 0
#endif
#if !defined(__has_extension)
#define __has_extension(x) 0
#endif

#pragma mark Attribute shims

#ifdef __GNUC__
#define XPC_CONSTRUCTOR __attribute__((constructor))
#define XPC_NORETURN __attribute__((__noreturn__))
#define XPC_NOTHROW __attribute__((__nothrow__))
#define XPC_NONNULL1 __attribute__((__nonnull__(1)))
#define XPC_NONNULL2 __attribute__((__nonnull__(2)))
#define XPC_NONNULL3 __attribute__((__nonnull__(3)))
#define XPC_NONNULL4 __attribute__((__nonnull__(4)))
#define XPC_NONNULL5 __attribute__((__nonnull__(5)))
#define XPC_NONNULL6 __attribute__((__nonnull__(6)))
#define XPC_NONNULL7 __attribute__((__nonnull__(7)))
#define XPC_NONNULL8 __attribute__((__nonnull__(8)))
#define XPC_NONNULL9 __attribute__((__nonnull__(9)))
#define XPC_NONNULL10 __attribute__((__nonnull__(10)))
#define XPC_NONNULL11 __attribute__((__nonnull__(11)))
#define XPC_NONNULL(n) __attribute__((__nonnull__(n)))
#define XPC_NONNULL_ALL __attribute__((__nonnull__))
#define XPC_SENTINEL __attribute__((__sentinel__))
#define XPC_PURE __attribute__((__pure__))
#define XPC_WARN_RESULT __attribute__((__warn_unused_result__))
#define XPC_MALLOC __attribute__((__malloc__))
#define XPC_UNUSED __attribute__((__unused__))
#define XPC_USED __attribute__((__used__))
#define XPC_PACKED __attribute__((__packed__))
#define XPC_PRINTF(m, n) __attribute__((format(printf, m, n)))
#define XPC_INLINE static __inline__ __attribute__((__always_inline__))
#define XPC_NOINLINE __attribute__((noinline))
#define XPC_NOIMPL __attribute__((unavailable))
#if __has_extension(attribute_unavailable_with_message)
#define XPC_UNAVAILABLE(m) __attribute__((unavailable(m)))
#else
#define XPC_UNAVAILABLE(m) XPC_NOIMPL
#endif
#if __has_attribute(noescape)
#define XPC_NOESCAPE __attribute__((__noescape__))
#else
#define XPC_NOESCAPE
#endif
#define XPC_EXPORT extern __attribute__((visibility("default")))
#define XPC_NOEXPORT __attribute__((visibility("hidden")))
#define XPC_WEAKIMPORT extern __attribute__((weak_import))
#define XPC_DEBUGGER_EXCL XPC_NOEXPORT XPC_USED
#define XPC_TRANSPARENT_UNION __attribute__((transparent_union))
#if __clang__
#define XPC_DEPRECATED(m) __attribute__((deprecated(m)))
#else
#define XPC_DEPRECATED(m) __attribute__((deprecated))
#endif
#ifndef XPC_TESTEXPORT
#define XPC_TESTEXPORT XPC_NOEXPORT
#endif
#if defined(__XPC_TEST__) && __XPC_TEST__
#define XPC_TESTSTATIC
#define XPC_TESTEXTERN extern
#define XPC_TESTNORETURN
#else
#define XPC_TESTSTATIC static
#define XPC_TESTEXTERN
#define XPC_TESTNORETURN XPC_NORETURN
#endif
#if __has_feature(objc_arc)
#define XPC_GIVES_REFERENCE __strong
#define XPC_UNRETAINED __unsafe_unretained
#define XPC_BRIDGE(xo) ((__bridge void *)(xo))
#define XPC_BRIDGEREF_BEGIN(xo) ((__bridge_retained void *)(xo))
#define XPC_BRIDGEREF_BEGIN_WITH_REF(xo) ((__bridge void *)(xo))
#define XPC_BRIDGEREF_MIDDLE(xo) ((__bridge id)(xo))
#define XPC_BRIDGEREF_END(xo) ((__bridge_transfer id)(xo))
#else /* !objc_arc */
#define XPC_GIVES_REFERENCE
#define XPC_UNRETAINED
#define XPC_BRIDGE(xo) (xo)
#define XPC_BRIDGEREF_BEGIN(xo) (xo)
#define XPC_BRIDGEREF_BEGIN_WITH_REF(xo) (xo)
#define XPC_BRIDGEREF_MIDDLE(xo) (xo)
#define XPC_BRIDGEREF_END(xo) (xo)
#endif
#else /* __GNUC__ */
#define XPC_CONSTRUCTOR
#define XPC_NORETURN
#define XPC_NOTHROW
#define XPC_NONNULL1
#define XPC_NONNULL2
#define XPC_NONNULL3
#define XPC_NONNULL4
#define XPC_NONNULL5
#define XPC_NONNULL6
#define XPC_NONNULL7
#define XPC_NONNULL8
#define XPC_NONNULL9
#define XPC_NONNULL10
#define XPC_NONNULL11
#define XPC_NONNULL(n)
#define XPC_NONNULL_ALL
#define XPC_SENTINEL
#define XPC_PURE
#define XPC_WARN_RESULT
#define XPC_MALLOC
#define XPC_UNUSED
#define XPC_USED
#define XPC_PACKED
#define XPC_PRINTF(m, n)
#define XPC_INLINE static inline
#define XPC_NOINLINE
#define XPC_NOIMPL
#define XPC_UNAVAILABLE(m)
#define XPC_NOESCAPE
#define XPC_EXPORT extern
#define XPC_NOEXPORT
#define XPC_WEAKIMPORT
#define XPC_DEBUGGER_EXCL
#define XPC_TRANSPARENT_UNION
#define XPC_DEPRECATED(m)
#define XPC_TESTEXPORT
#define XPC_TESTSTATIC static
#define XPC_TESTEXTERN
#define XPC_TESTNORETURN
#define XPC_GIVES_REFERENCE
#define XPC_UNRETAINED
#define XPC_BRIDGE(xo) (xo)
#define XPC_BRIDGEREF_BEGIN(xo) (xo)
#define XPC_BRIDGEREF_BEGIN_WITH_REF(xo) (xo)
#define XPC_BRIDGEREF_MIDDLE(xo) (xo)
#define XPC_BRIDGEREF_END(xo) (xo)
#endif /* __GNUC__ */

#if __has_feature(assume_nonnull)
#define XPC_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define XPC_ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")
#else
#define XPC_ASSUME_NONNULL_BEGIN
#define XPC_ASSUME_NONNULL_END
#endif

#if __has_feature(nullability)
#define _XPC_NULLABLE _Nullable
#define _XPC_NONNULL _Nonnull
#define _XPC_NULL_UNSPECIFIED _Null_unspecified
#else
#define _XPC_NULLABLE
#define _XPC_NONNULL
#define _XPC_NULL_UNSPECIFIED
#endif

#if __has_feature(nullability_on_arrays)
#define XPC_NONNULL_ARRAY _Nonnull
#else
#define XPC_NONNULL_ARRAY
#endif

#if defined(__has_ptrcheck) && __has_ptrcheck
#define XPC_PTR_ASSUMES_SINGLE __ptrcheck_abi_assume_single()
#define XPC_SINGLE __single
#define XPC_UNSAFE_INDEXABLE __unsafe_indexable
#define XPC_CSTRING XPC_UNSAFE_INDEXABLE
#define XPC_SIZEDBY(N) __sized_by(N)
#define XPC_COUNTEDBY(N) __counted_by(N)
#define XPC_UNSAFE_FORGE_SIZED_BY(_type, _ptr, _size) \
		__unsafe_forge_bidi_indexable(_type, _ptr, _size)
#define XPC_UNSAFE_FORGE_SINGLE(_type, _ptr) \
		__unsafe_forge_single(_type, _ptr)
#else /* !__has_ptrcheck */
#define XPC_PTR_ASSUMES_SINGLE
#define XPC_SINGLE
#define XPC_UNSAFE_INDEXABLE
#define XPC_CSTRING
#define XPC_SIZEDBY(N)
#define XPC_COUNTEDBY(N)
#define XPC_UNSAFE_FORGE_SIZED_BY(_type, _ptr, _size) ((_type)(_ptr))
#define XPC_UNSAFE_FORGE_SINGLE(_type, _ptr) ((_type)(_ptr))
#endif

#define XPC_FLAGS_ENUM(_name, _type, ...) \
	typedef _type _name##_t; \
	enum { __VA_ARGS__ }

#define XPC_ENUM(_name, _type, ...) \
	typedef _type _name##_t; \
	enum { __VA_ARGS__ }

#if __has_attribute(swift_name)
#define XPC_SWIFT_NAME(_name) __attribute__((swift_name(_name)))
#else
#define XPC_SWIFT_NAME(_name)
#endif

#if __has_attribute(swift_attr)
#define XPC_SWIFT_SENDABLE __attribute__((__swift_attr__("@Sendable")))
#else
#define XPC_SWIFT_SENDABLE
#endif

#if __has_attribute(availability) && defined(__clang__)
#define XPC_SWIFT_UNAVAILABLE(msg) __attribute__((availability(swift, unavailable, message = msg)))
#else
#define XPC_SWIFT_UNAVAILABLE(msg)
#endif
#define XPC_SWIFT_NOEXPORT XPC_SWIFT_UNAVAILABLE("Unavailable in Swift from the XPC C Module")

#if __has_feature(arc_cf_code_audited)
#define XPC_CF_RETURNS_RETAINED __attribute__((cf_returns_retained))
#else
#define XPC_CF_RETURNS_RETAINED
#endif

/* Public XPC object model (see xpc/xpc.h for the typedefs). */

/*
 * XPC_DECL(name) declares one of the XPC object families: e.g.
 * XPC_DECL(xpc_connection) makes `xpc_connection_t` a pointer to the opaque
 * struct _xpc_connection_s, matching the SDK's non-ObjC spelling.
 */
#define XPC_DECL(name) typedef struct _##name##_s * name##_t

/*
 * XPC_TYPE(type) declares one of the per-kind type singletons...
 */
#define XPC_TYPE(type) const struct _xpc_type_s type

/*
 * XPC_GLOBAL_OBJECT(object) bridges a C object declaration to xpc_object_t,
 * matching the SDK's XPC_ERROR_* / XPC_BOOL_* constant pattern.
 */
#define XPC_GLOBAL_OBJECT(object) (&(object))

/*
 * XPC_CLASS_DECL(objc_name) is accepted for source compatibility with SDK
 * code that spells the deprecated ObjC name; it maps to the C declaration.
 */
#define XPC_CLASS_DECL(name) XPC_DECL(name)

/*
 * XPC_RETURNS_RETAINED annotates creators of retained objects. The SDK spells
 * this as OS_OBJECT_RETURNS_RETAINED in ObjC mode and as nothing in C mode;
 * we only ship the C spelling, so it is empty.
 */
#define XPC_RETURNS_RETAINED

#ifdef __cplusplus
}
#endif

#endif /* __XPC_BASE_H__ */