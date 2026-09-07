/*
 * xpc_value.c — scalar value types for the reimplemented XPC framework.
 *
 * null, bool, int64, uint64, double, and date all share the
 * xpc_scalar_t storage (header + inline union member).
 */

#include "xpc_internal.h"

#pragma mark - null

xpc_object_t
xpc_null_create(void)
{
    return xpc_object_alloc_scalar(&_xpc_type_null);
}

#pragma mark - bool

xpc_object_t
xpc_bool_create(bool value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_bool));
    if (o) o->v.bval = value;
    return (xpc_object_t)o;
}

bool
xpc_bool_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_bool)) return false;
    return XPC_CAST(xpc_scalar_t, obj)->v.bval;
}

#pragma mark - int64

xpc_object_t
xpc_int64_create(int64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_int64));
    if (o) o->v.i64 = value;
    return (xpc_object_t)o;
}

int64_t
xpc_int64_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_int64)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.i64;
}

#pragma mark - uint64

xpc_object_t
xpc_uint64_create(uint64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_uint64));
    if (o) o->v.u64 = value;
    return (xpc_object_t)o;
}

uint64_t
xpc_uint64_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_uint64)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.u64;
}

#pragma mark - double

xpc_object_t
xpc_double_create(double value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_double));
    if (o) o->v.dbl = value;
    return (xpc_object_t)o;
}

double
xpc_double_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_double)) return 0.0;
    return XPC_CAST(xpc_scalar_t, obj)->v.dbl;
}

#pragma mark - date

xpc_object_t
xpc_date_create(int64_t value)
{
    xpc_scalar_t *o = XPC_CAST(xpc_scalar_t,
        xpc_object_alloc_scalar(&_xpc_type_date));
    if (o) o->v.date_ns = value;
    return (xpc_object_t)o;
}

int64_t
xpc_date_get_value(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_date)) return 0;
    return XPC_CAST(xpc_scalar_t, obj)->v.date_ns;
}