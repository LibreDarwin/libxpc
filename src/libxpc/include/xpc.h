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
 * xpc.h — public API for the reimplemented XPC framework.
 *
 * Drop-in compatible subset of Apple's libxpc public interface,
 * covering the core object model, value types, and containers.
 * Wire serialization / pipe layer lives in the private header.
 *
 * This is an independent reimplementation; it is NOT Apple's code.
 * The byte-level wire format it speaks is documented in
 * docs/WIRE_FORMAT.md (reverse-engineered and cross-validated
 * against the real libxpc on macOS 26).
 */

#ifndef __XPC_XPC_H__
#define __XPC_XPC_H__

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <uuid/uuid.h>
#include <mach/mach.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma mark - Types
/*!
 * @typedef xpc_object_t
 * @abstract First-class XPC objects.
 */
typedef struct _xpc_object_s *xpc_object_t;


/*!
 * @typedef xpc_type_t
 * @abstract The type of an XPC object.
 */
typedef const struct _xpc_type_s *xpc_type_t;

#define XPC_ARRAY_APPEND ((size_t)-1)

/*!
 * @typedef xpc_handler_t
 * @abstract Call block with an object.
 */
typedef void (^xpc_handler_t)(xpc_object_t object);

#pragma mark - XPC object lifecycle

xpc_type_t xpc_get_type(xpc_object_t object);

bool xpc_equal(xpc_object_t object1, xpc_object_t object2);
uint64_t xpc_hash(xpc_object_t object);

char *xpc_copy_description(xpc_object_t object);

xpc_object_t xpc_retain(xpc_object_t object);
void xpc_release(xpc_object_t object);

#pragma mark - XPC types

extern const struct _xpc_type_s _xpc_type_null;
extern const struct _xpc_type_s _xpc_type_bool;
extern const struct _xpc_type_s _xpc_type_int64;
extern const struct _xpc_type_s _xpc_type_uint64;
extern const struct _xpc_type_s _xpc_type_double;
extern const struct _xpc_type_s _xpc_type_date;
extern const struct _xpc_type_s _xpc_type_data;
extern const struct _xpc_type_s _xpc_type_string;
extern const struct _xpc_type_s _xpc_type_uuid;
extern const struct _xpc_type_s _xpc_type_array;
extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s _xpc_type_error;
extern const struct _xpc_type_s _xpc_type_connection;
extern const struct _xpc_type_s _xpc_type_endpoint;
extern const struct _xpc_type_s _xpc_type_activity;
extern const struct _xpc_type_s _xpc_type_session;
extern const struct _xpc_type_s _xpc_type_listener;

#pragma mark - Connection

xpc_object_t xpc_connection_create_from_endpoint(xpc_object_t endpoint);
xpc_object_t xpc_connection_create(mach_port_t port);
mach_port_t xpc_connection_get_port(xpc_object_t connection);
void xpc_connection_set_incoming_message_handler(xpc_object_t connection,
    xpc_handler_t handler);
void xpc_connection_resume(xpc_object_t connection);
void xpc_connection_suspend(xpc_object_t connection);
void xpc_connection_cancel(xpc_object_t connection);

#pragma mark - Endpoint

xpc_object_t xpc_endpoint_create(mach_port_t port);
mach_port_t xpc_endpoint_get_port(xpc_object_t endpoint);
xpc_object_t xpc_endpoint_copy_listener_port(xpc_object_t endpoint);

#pragma mark - Activity

xpc_object_t xpc_activity_create(xpc_object_t connection);
xpc_object_t xpc_activity_create_from_endpoint(xpc_object_t endpoint);
void xpc_activity_resume(xpc_object_t activity);
void xpc_activity_suspend(xpc_object_t activity);
void xpc_activity_cancel(xpc_object_t activity);

#pragma mark - Session

xpc_object_t xpc_session_create(xpc_object_t endpoint);
void xpc_session_set_incoming_message_handler(xpc_object_t session,
    xpc_handler_t handler);
void xpc_session_resume(xpc_object_t session);
void xpc_session_suspend(xpc_object_t session);
void xpc_session_cancel(xpc_object_t session);

#pragma mark - Listener

xpc_object_t xpc_listener_create(mach_port_t port);
xpc_object_t xpc_listener_create_anonymous(void);
void xpc_listener_set_incoming_session_handler(xpc_object_t listener,
    xpc_handler_t handler);
void xpc_listener_resume(xpc_object_t listener);
void xpc_listener_suspend(xpc_object_t listener);
void xpc_listener_cancel(xpc_object_t listener);

#pragma mark - Null

xpc_object_t xpc_null_create(void);

#pragma mark - Boolean

xpc_object_t xpc_bool_create(bool value);
bool xpc_bool_get_value(xpc_object_t object);

#pragma mark - Signed integer

xpc_object_t xpc_int64_create(int64_t value);
int64_t xpc_int64_get_value(xpc_object_t object);

#pragma mark - Unsigned integer

xpc_object_t xpc_uint64_create(uint64_t value);
uint64_t xpc_uint64_get_value(xpc_object_t object);

#pragma mark - Double

xpc_object_t xpc_double_create(double value);
double xpc_double_get_value(xpc_object_t object);

#pragma mark - Date

xpc_object_t xpc_date_create(int64_t interval);
xpc_object_t xpc_date_create_from_timespec(struct timespec *ts);
int64_t xpc_date_get_value(xpc_object_t object);
void xpc_date_get_timespec(xpc_object_t object, struct timespec *ts);

#pragma mark - Data

xpc_object_t xpc_data_create(const void *bytes, size_t length);
xpc_object_t xpc_data_create_with_bytes(const void *bytes, size_t length);
size_t xpc_data_get_length(xpc_object_t object);
const void *xpc_data_get_bytes_ptr(xpc_object_t object);
bool xpc_data_get_bytes(xpc_object_t object, void *bytes, size_t offset,
    size_t length);

#pragma mark - String

xpc_object_t xpc_string_create(const char *string);
xpc_object_t xpc_string_create_with_format(const char *fmt, ...);
xpc_object_t xpc_string_create_with_length(const char *string, size_t length);
size_t xpc_string_get_length(xpc_object_t object);
const char *xpc_string_get_string_ptr(xpc_object_t object);

#pragma mark - UUID

xpc_object_t xpc_uuid_create(const uuid_t uuid);
const uint8_t *xpc_uuid_get_bytes(xpc_object_t object);

#pragma mark - Array

xpc_object_t xpc_array_create(const xpc_object_t *objects,
    size_t count);
xpc_object_t xpc_array_create_np(const xpc_object_t *objects,
    size_t count);
size_t xpc_array_get_count(xpc_object_t object);
void xpc_array_set_value(xpc_object_t object, size_t index,
    xpc_object_t value);
void xpc_array_append_value(xpc_object_t object, xpc_object_t value);
xpc_object_t xpc_array_get_value(xpc_object_t object, size_t index);
bool xpc_array_get_bool(xpc_object_t object, size_t index);
int64_t xpc_array_get_int64(xpc_object_t object, size_t index);
uint64_t xpc_array_get_uint64(xpc_object_t object, size_t index);
double xpc_array_get_double(xpc_object_t object, size_t index);
const char *xpc_array_get_string(xpc_object_t object, size_t index);
const void *xpc_array_get_data(xpc_object_t object, size_t index,
    size_t *length);
bool xpc_array_get_data_np(xpc_object_t object, size_t index,
    const void **bytes, size_t *length);
const uint8_t *xpc_array_get_uuid(xpc_object_t object, size_t index);
xpc_object_t xpc_array_get_date(xpc_object_t object, size_t index);
bool xpc_array_apply(xpc_object_t object,
    bool (^applier)(size_t index, xpc_object_t value));

#pragma mark - Dictionary

xpc_object_t xpc_dictionary_create(const char *const *keys,
    const xpc_object_t *values, size_t count);
size_t xpc_dictionary_get_count(xpc_object_t object);
void xpc_dictionary_set_value(xpc_object_t object, const char *key,
    xpc_object_t value);
xpc_object_t xpc_dictionary_get_value(xpc_object_t object,
    const char *key);
void xpc_dictionary_remove_value(xpc_object_t object, const char *key);
bool xpc_dictionary_get_bool(xpc_object_t object, const char *key);
int64_t xpc_dictionary_get_int64(xpc_object_t object, const char *key);
uint64_t xpc_dictionary_get_uint64(xpc_object_t object, const char *key);
double xpc_dictionary_get_double(xpc_object_t object, const char *key);
const char *xpc_dictionary_get_string(xpc_object_t object,
    const char *key);
const void *xpc_dictionary_get_data(xpc_object_t object, const char *key,
    size_t *length);
bool xpc_dictionary_get_data_np(xpc_object_t object, const char *key,
    const void **bytes, size_t *length);
const uint8_t *xpc_dictionary_get_uuid(xpc_object_t object,
    const char *key);
xpc_object_t xpc_dictionary_get_date(xpc_object_t object,
    const char *key);
bool xpc_dictionary_apply(xpc_object_t object,
    bool (^applier)(const char *key, xpc_object_t value));
void xpc_dictionary_set_bool(xpc_object_t object, const char *key,
    bool value);
void xpc_dictionary_set_int64(xpc_object_t object, const char *key,
    int64_t value);
void xpc_dictionary_set_uint64(xpc_object_t object, const char *key,
    uint64_t value);
void xpc_dictionary_set_double(xpc_object_t object, const char *key,
    double value);
void xpc_dictionary_set_string(xpc_object_t object, const char *key,
    const char *string);
void xpc_dictionary_set_data(xpc_object_t object, const char *key,
    const void *bytes, size_t length);
void xpc_dictionary_set_uuid(xpc_object_t object, const char *key,
    const uuid_t uuid);
void xpc_dictionary_set_date(xpc_object_t object, const char *key,
    int64_t value);
void xpc_dictionary_set_mach_send(xpc_object_t object, const char *key,
    mach_port_t port);

#pragma mark - Mach-Send

/*
 * Wrap a send right for transport.  The object borrows the right; the
 * caller keeps ownership and remains responsible for it.  Mirrors
 * Apple's xpc_mach_send_create(3).
 */
xpc_object_t xpc_mach_send_create(mach_port_t port);
mach_port_t xpc_mach_send_get_port(xpc_object_t object);

#ifdef __cplusplus
}
#endif

#endif /* __XPC_XPC_H__ */
