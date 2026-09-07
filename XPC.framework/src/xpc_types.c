/*
 * xpc_types.c — type singletons for the reimplemented XPC framework.
 *
 * One const struct per XPC kind.  Every xpc_object_t's ->isa points at
 * one of these, which lets xpc_get_type() work by pure pointer
 * comparison and gives xpc_copy_description() a stable printable name.
 */

#include "xpc_internal.h"

const struct _xpc_type_s _xpc_type_null       = { "null",       XPC_KIND_NULL };
const struct _xpc_type_s _xpc_type_bool       = { "bool",       XPC_KIND_BOOL };
const struct _xpc_type_s _xpc_type_int64      = { "int64",      XPC_KIND_INT64 };
const struct _xpc_type_s _xpc_type_uint64     = { "uint64",     XPC_KIND_UINT64 };
const struct _xpc_type_s _xpc_type_double     = { "double",     XPC_KIND_DOUBLE };
const struct _xpc_type_s _xpc_type_date       = { "date",       XPC_KIND_DATE };
const struct _xpc_type_s _xpc_type_data       = { "data",       XPC_KIND_DATA };
const struct _xpc_type_s _xpc_type_string     = { "string",     XPC_KIND_STRING };
const struct _xpc_type_s _xpc_type_uuid       = { "uuid",       XPC_KIND_UUID };
const struct _xpc_type_s _xpc_type_array      = { "array",      XPC_KIND_ARRAY };
const struct _xpc_type_s _xpc_type_dictionary = { "dictionary", XPC_KIND_DICTIONARY };
const struct _xpc_type_s _xpc_type_error      = { "error",      XPC_KIND_ERROR };
const struct _xpc_type_s _xpc_type_connection = { "connection", XPC_KIND_CONNECTION };
const struct _xpc_type_s _xpc_type_endpoint   = { "endpoint",   XPC_KIND_ENDPOINT };
const struct _xpc_type_s _xpc_type_activity   = { "activity",   XPC_KIND_ACTIVITY };
const struct _xpc_type_s _xpc_type_session    = { "session",    XPC_KIND_SESSION };
const struct _xpc_type_s _xpc_type_listener   = { "listener",   XPC_KIND_LISTENER };

xpc_kind_t
xpc_kind_from_type(xpc_type_t t)
{
    if (!t) return XPC_KIND_COUNT;
    return t->kind;
}