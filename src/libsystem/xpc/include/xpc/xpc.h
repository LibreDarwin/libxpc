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
 * xpc/xpc.h — public API for the reimplemented XPC framework.
 *
 * This is the umbrella header, parity-shaped with Apple's xpc/xpc.h: it
 * declares the core object model, the scalar/container value types, the
 * object lifecycle API, and then pulls in the sub-framework headers
 * (connection, listener, endpoint, activity, session, rich_error,
 * peer_requirement, debug) so a single #include covers the whole surface.
 *
 * This is an independent reimplementation; it is NOT Apple's code.  The
 * byte-level wire format it speaks is documented in docs/WIRE_FORMAT.md
 * (reverse-engineered and cross-validated against the real libxpc).
 */

#ifndef __XPC_XPC_H__
#define __XPC_XPC_H__

#define __XPC_INDIRECT__ 1
#include <xpc/base.h>
#undef __XPC_INDIRECT__

#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <uuid/uuid.h>
#include <bsm/audit.h>

#ifdef __cplusplus
extern "C" {
#endif

XPC_ASSUME_NONNULL_BEGIN

#pragma mark Types

/*!
 * @typedef xpc_object_t
 * @abstract The type of all XPC objects: values, containers, connections.
 */
typedef void *xpc_object_t;

/*!
 * @typedef xpc_type_t
 * @abstract The type of an XPC object type.
 */
typedef const struct _xpc_type_s *xpc_type_t;

XPC_DECL(xpc_connection);
XPC_DECL(xpc_endpoint);
XPC_DECL(xpc_rich_error);
XPC_DECL(xpc_activity);
XPC_DECL(xpc_session);
XPC_DECL(xpc_listener);

/*!
 * @typedef xpc_handler_t
 * @abstract A block invoked with an XPC object (message or event).
 */
typedef void (^xpc_handler_t)(xpc_object_t _XPC_NONNULL object);

/*!
 * @typedef xpc_connection_handler_t
 * @abstract A function invoked for each new connection established with an
 * XPC service (see xpc_main()).
 */
typedef void (*xpc_connection_handler_t)(xpc_connection_t connection);

/*!
 * @typedef xpc_finalizer_t
 * @abstract A function called with the context previously set on an object
 * when that object is disposed.
 */
typedef void (*xpc_finalizer_t)(void *_XPC_NULLABLE value);

#define XPC_ARRAY_APPEND ((size_t)(-1))

#pragma mark Object lifecycle

XPC_EXPORT XPC_NONNULL1
xpc_type_t
xpc_get_type(xpc_object_t object);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
bool
xpc_equal(xpc_object_t object1, xpc_object_t object2);

XPC_EXPORT XPC_NONNULL1
uint64_t
xpc_hash(xpc_object_t object);

XPC_EXPORT XPC_NONNULL1 XPC_MALLOC
char *
xpc_copy_description(xpc_object_t object);

XPC_EXPORT XPC_NONNULL1
xpc_object_t _XPC_NULLABLE
xpc_retain(xpc_object_t object);

XPC_EXPORT XPC_NONNULL1
void
xpc_release(xpc_object_t object);

XPC_EXPORT
xpc_object_t
xpc_copy(xpc_object_t object);

XPC_EXPORT XPC_NONNULL1
const char *
xpc_type_get_name(xpc_type_t type);

#pragma mark Types

XPC_EXPORT
XPC_TYPE(_xpc_type_null);
XPC_EXPORT
XPC_TYPE(_xpc_type_bool);
XPC_EXPORT
XPC_TYPE(_xpc_type_int64);
XPC_EXPORT
XPC_TYPE(_xpc_type_uint64);
XPC_EXPORT
XPC_TYPE(_xpc_type_double);
XPC_EXPORT
XPC_TYPE(_xpc_type_date);
XPC_EXPORT
XPC_TYPE(_xpc_type_data);
XPC_EXPORT
XPC_TYPE(_xpc_type_string);
XPC_EXPORT
XPC_TYPE(_xpc_type_uuid);
XPC_EXPORT
XPC_TYPE(_xpc_type_fd);
XPC_EXPORT
XPC_TYPE(_xpc_type_shmem);
XPC_EXPORT
XPC_TYPE(_xpc_type_array);
XPC_EXPORT
XPC_TYPE(_xpc_type_dictionary);
XPC_EXPORT
XPC_TYPE(_xpc_type_error);
XPC_EXPORT
XPC_TYPE(_xpc_type_connection);
XPC_EXPORT
XPC_TYPE(_xpc_type_endpoint);
XPC_EXPORT
XPC_TYPE(_xpc_type_activity);
XPC_EXPORT
XPC_TYPE(_xpc_type_session);
XPC_EXPORT
XPC_TYPE(_xpc_type_listener);
XPC_EXPORT
XPC_TYPE(_xpc_type_rich_error);

#define XPC_TYPE_NULL (&_xpc_type_null)
#define XPC_TYPE_BOOL (&_xpc_type_bool)
#define XPC_TYPE_INT64 (&_xpc_type_int64)
#define XPC_TYPE_UINT64 (&_xpc_type_uint64)
#define XPC_TYPE_DOUBLE (&_xpc_type_double)
#define XPC_TYPE_DATE (&_xpc_type_date)
#define XPC_TYPE_DATA (&_xpc_type_data)
#define XPC_TYPE_STRING (&_xpc_type_string)
#define XPC_TYPE_UUID (&_xpc_type_uuid)
#define XPC_TYPE_FD (&_xpc_type_fd)
#define XPC_TYPE_SHMEM (&_xpc_type_shmem)
#define XPC_TYPE_ARRAY (&_xpc_type_array)
#define XPC_TYPE_DICTIONARY (&_xpc_type_dictionary)
#define XPC_TYPE_ERROR (&_xpc_type_error)
#define XPC_TYPE_CONNECTION (&_xpc_type_connection)
#define XPC_TYPE_ENDPOINT (&_xpc_type_endpoint)
#define XPC_TYPE_ACTIVITY (&_xpc_type_activity)
#define XPC_TYPE_SESSION (&_xpc_type_session)
#define XPC_TYPE_LISTENER (&_xpc_type_listener)
#define XPC_TYPE_RICH_ERROR (&_xpc_type_rich_error)

#define XPC_BOOL_TRUE XPC_GLOBAL_OBJECT(_xpc_bool_true)
XPC_EXPORT
XPC_TYPE(_xpc_bool_true);
#define XPC_BOOL_FALSE XPC_GLOBAL_OBJECT(_xpc_bool_false)
XPC_EXPORT
XPC_TYPE(_xpc_bool_false);

/* Error dictionaries carry a "description" string under this key. */
#define XPC_ERROR_KEY_DESCRIPTION _xpc_error_key_description
XPC_EXPORT
const char *const _xpc_error_key_description;

/* Event dictionaries carry their event name/subsystem under these keys. */
#define XPC_EVENT_KEY_NAME _xpc_event_key_name
XPC_EXPORT
const char *const _xpc_event_key_name;
#define XPC_EVENT_KEY_SUBSYSTEM _xpc_event_key_subsystem
XPC_EXPORT
const char *const _xpc_event_key_subsystem;

/* Well-known event streams (see xpc_set_event_stream_handler). */
#define XPC_EVENT_STREAM_NAME_MANAGED _xpc_event_stream_name_managed
XPC_EXPORT
const char *const _xpc_event_stream_name_managed;
#define XPC_EVENT_STREAM_NAME_GLOBAL _xpc_event_stream_name_global
XPC_EXPORT
const char *const _xpc_event_stream_name_global;
#define XPC_EVENT_STREAM_NAME_MAINTENANCE _xpc_event_stream_name_maintenance
XPC_EXPORT
const char *const _xpc_event_stream_name_maintenance;

#pragma mark Null

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_null_create(void);

#pragma mark Boolean

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_bool_create(bool value);

XPC_EXPORT XPC_NONNULL1
bool
xpc_bool_get_value(xpc_object_t xbool);

#pragma mark Signed integer

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_int64_create(int64_t value);

XPC_EXPORT XPC_NONNULL1
int64_t
xpc_int64_get_value(xpc_object_t xint);

#pragma mark Unsigned integer

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_uint64_create(uint64_t value);

XPC_EXPORT XPC_NONNULL1
uint64_t
xpc_uint64_get_value(xpc_object_t xuint);

#pragma mark Double

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_double_create(double value);

XPC_EXPORT XPC_NONNULL1
double
xpc_double_get_value(xpc_object_t xdouble);

#pragma mark Date

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_date_create(int64_t interval);

XPC_EXPORT
xpc_object_t
xpc_date_create_from_current(void);

XPC_EXPORT XPC_NONNULL1
int64_t
xpc_date_get_value(xpc_object_t xdate);

#pragma mark Data

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_data_create(const void *_XPC_NULLABLE bytes, size_t length);

XPC_EXPORT
xpc_object_t
xpc_data_create_with_dispatch_data(dispatch_data_t ddata);

XPC_EXPORT XPC_NONNULL1
size_t
xpc_data_get_length(xpc_object_t xdata);

XPC_EXPORT XPC_NONNULL1
const void *
xpc_data_get_bytes_ptr(xpc_object_t xdata);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
bool
xpc_data_get_bytes(xpc_object_t xdata, void *buffer, size_t off, size_t length);

#pragma mark String

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_string_create(const char *string);

XPC_EXPORT XPC_PRINTF(1, 2) XPC_MALLOC
xpc_object_t
xpc_string_create_with_format(const char *fmt, ...);

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_string_create_with_format_and_arguments(const char *fmt, va_list ap);

XPC_EXPORT XPC_NONNULL1
size_t
xpc_string_get_length(xpc_object_t xstring);

XPC_EXPORT XPC_NONNULL1
const char *
xpc_string_get_string_ptr(xpc_object_t xstring);

#pragma mark UUID

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_uuid_create(const uuid_t XPC_NONNULL_ARRAY uuid);

XPC_EXPORT XPC_NONNULL1
const uint8_t *
xpc_uuid_get_bytes(xpc_object_t xuuid);

#pragma mark File descriptors

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_fd_create(int fd);

XPC_EXPORT XPC_NONNULL1
int
xpc_fd_dup(xpc_object_t xfd);

#pragma mark Shared memory

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_shmem_create(void *region, size_t length);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
int
xpc_shmem_map(xpc_object_t xshmem, void *_XPC_NONNULL *_XPC_NULLABLE region);

#pragma mark Array

typedef bool (^xpc_array_applier_t)(size_t index, xpc_object_t _XPC_NONNULL value);

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_array_create(const xpc_object_t _XPC_NONNULL *XPC_COUNTEDBY(count) _XPC_NULLABLE objects, size_t count);

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_array_create_empty(void);

XPC_EXPORT XPC_NONNULL1
size_t
xpc_array_get_count(xpc_object_t xarray);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_array_get_value(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_value(xpc_object_t xarray, size_t index, xpc_object_t value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_array_append_value(xpc_object_t xarray, xpc_object_t value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
bool
xpc_array_apply(xpc_object_t xarray, XPC_NOESCAPE xpc_array_applier_t applier);

XPC_EXPORT XPC_NONNULL1
bool
xpc_array_get_bool(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
int64_t
xpc_array_get_int64(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
uint64_t
xpc_array_get_uint64(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
double
xpc_array_get_double(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
const char *_XPC_NULLABLE
xpc_array_get_string(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL3
const void *
xpc_array_get_data(xpc_object_t xarray, size_t index, size_t *_XPC_NULLABLE length);

XPC_EXPORT XPC_NONNULL1
const uint8_t *
xpc_array_get_uuid(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_array_get_date(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_array_get_array(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_array_get_dictionary(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_bool(xpc_object_t xarray, size_t index, bool value);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_int64(xpc_object_t xarray, size_t index, int64_t value);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_uint64(xpc_object_t xarray, size_t index, uint64_t value);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_double(xpc_object_t xarray, size_t index, double value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL3
void
xpc_array_set_string(xpc_object_t xarray, size_t index, const char *string);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_data(xpc_object_t xarray, size_t index, const void *_XPC_NULLABLE bytes, size_t length);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_uuid(xpc_object_t xarray, size_t index,
	const uuid_t XPC_NONNULL_ARRAY uuid);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_date(xpc_object_t xarray, size_t index, int64_t value);

XPC_EXPORT XPC_NONNULL1
void
xpc_array_set_fd(xpc_object_t xarray, size_t index, int fd);

XPC_EXPORT XPC_NONNULL1
int
xpc_array_dup_fd(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1
xpc_connection_t
xpc_array_create_connection(xpc_object_t xarray, size_t index);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL3
void
xpc_array_set_connection(xpc_object_t xarray, size_t index, xpc_connection_t connection);

#pragma mark Dictionary

typedef bool (^xpc_dictionary_applier_t)(const char *_XPC_NONNULL key,
    xpc_object_t _XPC_NONNULL value);

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_dictionary_create(const char *_XPC_NONNULL *const XPC_COUNTEDBY(count) _XPC_NULLABLE keys,
    const xpc_object_t _XPC_NONNULL *XPC_COUNTEDBY(count) _XPC_NULLABLE values, size_t count);

XPC_EXPORT XPC_MALLOC
xpc_object_t
xpc_dictionary_create_empty(void);

XPC_EXPORT XPC_NONNULL1
size_t
xpc_dictionary_get_count(xpc_object_t xdict);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_dictionary_get_value(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_value(xpc_object_t xdict, const char *key, xpc_object_t _XPC_NULLABLE value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
bool
xpc_dictionary_apply(xpc_object_t xdict, XPC_NOESCAPE xpc_dictionary_applier_t applier);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
bool
xpc_dictionary_get_bool(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
int64_t
xpc_dictionary_get_int64(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
uint64_t
xpc_dictionary_get_uint64(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
double
xpc_dictionary_get_double(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
const char *_XPC_NULLABLE
xpc_dictionary_get_string(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2 XPC_NONNULL3
const void *
xpc_dictionary_get_data(xpc_object_t xdict, const char *key, size_t *_XPC_NULLABLE length);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
const uint8_t *
xpc_dictionary_get_uuid(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
xpc_object_t
xpc_dictionary_get_date(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
xpc_object_t
xpc_dictionary_get_array(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
xpc_object_t
xpc_dictionary_get_dictionary(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_bool(xpc_object_t xdict, const char *key, bool value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_uint64(xpc_object_t xdict, const char *key, uint64_t value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_double(xpc_object_t xdict, const char *key, double value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *string);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_data(xpc_object_t xdict, const char *key, const void *_XPC_NULLABLE bytes, size_t length);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2 XPC_NONNULL3
void
xpc_dictionary_set_uuid(xpc_object_t xdict, const char *key,
	const uuid_t XPC_NONNULL_ARRAY uuid);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_date(xpc_object_t xdict, const char *key, int64_t value);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_fd(xpc_object_t xdict, const char *key, int fd);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
int
xpc_dictionary_dup_fd(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
void
xpc_dictionary_set_mach_send(xpc_object_t xdict, const char *key, mach_port_t port);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
mach_port_t
xpc_dictionary_copy_mach_send(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2
xpc_connection_t
xpc_dictionary_create_connection(xpc_object_t xdict, const char *key);

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL2 XPC_NONNULL3
void
xpc_dictionary_set_connection(xpc_object_t xdict, const char *key, xpc_connection_t connection);

XPC_EXPORT XPC_NONNULL1
xpc_object_t
xpc_dictionary_create_reply(xpc_object_t original);

XPC_EXPORT XPC_NONNULL1
xpc_connection_t
xpc_dictionary_get_remote_connection(xpc_object_t xdict);

#pragma mark Runtime

XPC_EXPORT XPC_NORETURN XPC_NONNULL1
void
xpc_main(xpc_connection_handler_t handler);

#pragma mark Transactions

XPC_EXPORT
void
xpc_transaction_begin(void);

XPC_EXPORT
void
xpc_transaction_end(void);

#pragma mark Events

XPC_EXPORT XPC_NONNULL1 XPC_NONNULL3
void
xpc_set_event_stream_handler(const char *stream, dispatch_queue_t _XPC_NULLABLE targetq,
    XPC_NOESCAPE xpc_handler_t handler);

XPC_ASSUME_NONNULL_END

/* Sub-framework headers (parity with the SDK include graph). */
#if !defined(__XPC_BUILDING_XPC__) || !__XPC_BUILDING_XPC__
#define __XPC_INDIRECT__ 1
#include <xpc/endpoint.h>
#include <xpc/debug.h>
#if __BLOCKS__
#include <xpc/activity.h>
#include <xpc/peer_requirement.h>
#include <xpc/connection.h>
#include <xpc/rich_error.h>
#include <xpc/session.h>
#include <xpc/listener.h>
#endif /* __BLOCKS__ */
#undef __XPC_INDIRECT__
#if __has_include(<launch.h>)
#include <launch.h>
#endif /* __has_include(<launch.h>) */
#endif /* !__XPC_BUILDING_XPC__ */

#ifdef __cplusplus
}
#endif

#endif /* __XPC_XPC_H__ */