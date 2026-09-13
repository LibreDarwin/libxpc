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
 * xpc_extra.c — convenience APIs for endpoint, connection, activity,
 * session, and listener types in the reimplemented XPC framework.
 *
 * This file also supplies the typed accessors declared in xpc.h.
 */

#include "xpc_internal.h"

#pragma mark - Connection

xpc_object_t
xpc_connection_create_from_endpoint(xpc_object_t endpoint)
{
    if (!XPC_OBJECT_CHECK(endpoint, &_xpc_type_endpoint)) return NULL;
    xpc_connection_t *c = XPC_CAST(xpc_connection_t,
        xpc_object_alloc(&_xpc_type_connection, sizeof(*c)));
    if (c) c->port = XPC_CAST(xpc_endpoint_t, endpoint)->port;
    return (xpc_object_t)c;
}

xpc_object_t
xpc_connection_create(mach_port_t port)
{
    xpc_connection_t *c = XPC_CAST(xpc_connection_t,
        xpc_object_alloc(&_xpc_type_connection, sizeof(*c)));
    if (c) c->port = port;
    return (xpc_object_t)c;
}

mach_port_t
xpc_connection_get_port(xpc_object_t connection)
{
    if (!XPC_OBJECT_CHECK(connection, &_xpc_type_connection))
        return MACH_PORT_NULL;
    return XPC_CAST(xpc_connection_t, connection)->port;
}

void
xpc_connection_set_incoming_message_handler(xpc_object_t connection,
    xpc_handler_t handler)
{
    (void)connection; (void)handler;
}

void
xpc_connection_resume(xpc_object_t connection) { (void)connection; }
void
xpc_connection_suspend(xpc_object_t connection) { (void)connection; }
void
xpc_connection_cancel(xpc_object_t connection) { (void)connection; }

#pragma mark - Endpoint

xpc_object_t
xpc_endpoint_create(mach_port_t port)
{
    xpc_endpoint_t *e = XPC_CAST(xpc_endpoint_t,
        xpc_object_alloc(&_xpc_type_endpoint, sizeof(*e)));
    if (e) e->port = port;
    return (xpc_object_t)e;
}

mach_port_t
xpc_endpoint_get_port(xpc_object_t endpoint)
{
    if (!XPC_OBJECT_CHECK(endpoint, &_xpc_type_endpoint))
        return MACH_PORT_NULL;
    return XPC_CAST(xpc_endpoint_t, endpoint)->port;
}

xpc_object_t
xpc_endpoint_copy_listener_port(xpc_object_t endpoint)
{
    (void)endpoint;
    return NULL;
}

#pragma mark - Activity

xpc_object_t
xpc_activity_create(xpc_object_t connection)
{
    (void)connection;
    return xpc_null_create();
}

xpc_object_t
xpc_activity_create_from_endpoint(xpc_object_t endpoint)
{
    (void)endpoint;
    return xpc_null_create();
}

void
xpc_activity_resume(xpc_object_t activity) { (void)activity; }
void
xpc_activity_suspend(xpc_object_t activity) { (void)activity; }
void
xpc_activity_cancel(xpc_object_t activity) { (void)activity; }

#pragma mark - Session

xpc_object_t
xpc_session_create(xpc_object_t endpoint)
{
    (void)endpoint;
    return xpc_null_create();
}

void
xpc_session_set_incoming_message_handler(xpc_object_t session,
    xpc_handler_t handler)
{
    (void)session; (void)handler;
}

void
xpc_session_resume(xpc_object_t session) { (void)session; }
void
xpc_session_suspend(xpc_object_t session) { (void)session; }
void
xpc_session_cancel(xpc_object_t session) { (void)session; }

#pragma mark - Listener

xpc_object_t
xpc_listener_create(mach_port_t port)
{
    return xpc_endpoint_create(port);
}

xpc_object_t
xpc_listener_create_anonymous(void)
{
    return xpc_endpoint_create(MACH_PORT_NULL);
}

void
xpc_listener_set_incoming_session_handler(xpc_object_t listener,
    xpc_handler_t handler)
{
    (void)listener; (void)handler;
}

void
xpc_listener_resume(xpc_object_t listener) { (void)listener; }
void
xpc_listener_suspend(xpc_object_t listener) { (void)listener; }
void
xpc_listener_cancel(xpc_object_t listener) { (void)listener; }

#pragma mark - Typed dictionary accessors (declared in xpc.h)

void
xpc_dictionary_set_double(xpc_object_t object, const char *key,
    double value)
{
    xpc_object_t v = xpc_double_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_string(xpc_object_t object, const char *key,
    const char *value)
{
    xpc_object_t v = xpc_string_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_data(xpc_object_t object, const char *key,
    const void *bytes, size_t length)
{
    xpc_object_t v = xpc_data_create(bytes, length);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_uuid(xpc_object_t object, const char *key,
    const uuid_t uuid)
{
    xpc_object_t v = xpc_uuid_create(uuid);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

void
xpc_dictionary_set_date(xpc_object_t object, const char *key,
    int64_t value)
{
    xpc_object_t v = xpc_date_create(value);
    xpc_dictionary_set_value(object, key, v);
    xpc_release(v);
}

bool
xpc_dictionary_get_bool(xpc_object_t object, const char *key)
{
    return xpc_bool_get_value(xpc_dictionary_get_value(object, key));
}

int64_t
xpc_dictionary_get_int64(xpc_object_t object, const char *key)
{
    return xpc_int64_get_value(xpc_dictionary_get_value(object, key));
}

uint64_t
xpc_dictionary_get_uint64(xpc_object_t object, const char *key)
{
    return xpc_uint64_get_value(xpc_dictionary_get_value(object, key));
}

double
xpc_dictionary_get_double(xpc_object_t object, const char *key)
{
    return xpc_double_get_value(xpc_dictionary_get_value(object, key));
}

const char *
xpc_dictionary_get_string(xpc_object_t object, const char *key)
{
    return xpc_string_get_string_ptr(xpc_dictionary_get_value(object, key));
}

const void *
xpc_dictionary_get_data(xpc_object_t object, const char *key, size_t *length)
{
    xpc_object_t v = xpc_dictionary_get_value(object, key);
    if (length) *length = xpc_data_get_length(v);
    return xpc_data_get_bytes_ptr(v);
}

bool
xpc_dictionary_get_data_np(xpc_object_t object, const char *key,
    const void **bytes, size_t *length)
{
    xpc_object_t v = xpc_dictionary_get_value(object, key);
    if (!XPC_OBJECT_CHECK(v, &_xpc_type_data)) return false;
    if (bytes) *bytes = xpc_data_get_bytes_ptr(v);
    if (length) *length = xpc_data_get_length(v);
    return true;
}

const uint8_t *
xpc_dictionary_get_uuid(xpc_object_t object, const char *key)
{
    return xpc_uuid_get_bytes(xpc_dictionary_get_value(object, key));
}

xpc_object_t
xpc_dictionary_get_date(xpc_object_t object, const char *key)
{
    return xpc_dictionary_get_value(object, key);
}

void
xpc_dictionary_remove_value(xpc_object_t object, const char *key)
{
    xpc_dictionary_set_value(object, key, NULL);
}

char *
xpc_copy_description(xpc_object_t object)
{
    return xpc_description_create(object);
}