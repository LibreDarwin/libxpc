/*
 * xpc_data.c — data (blob) values for the reimplemented XPC framework.
 *
 * The public user API (xpc_data_create) copies its input, matching
 * Apple's semantics; the internal one moves ownership.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_data_create(const void *bytes, size_t length)
{
    if (length > 0 && !bytes) return NULL;

    xpc_data_t *d = XPC_CAST(xpc_data_t,
        xpc_object_alloc(&_xpc_type_data, sizeof(xpc_data_t)));
    if (!d) return NULL;

    if (length == 0) {
        d->data = NULL;
        d->length = 0;
        return (xpc_object_t)d;
    }

    uint8_t *copy = malloc(length);
    if (!copy) {
        free(d);
        return NULL;
    }
    memcpy(copy, bytes, length);
    d->data = copy;
    d->length = length;
    return (xpc_object_t)d;
}

/* Internal: adopt an existing heap buffer (xpc_deserialize.c uses this). */
xpc_object_t
xpc_data_create_take(uint8_t *bytes, size_t length)
{
    xpc_data_t *d = XPC_CAST(xpc_data_t,
        xpc_object_alloc(&_xpc_type_data, sizeof(xpc_data_t)));
    if (!d) return NULL;
    d->data = bytes;
    d->length = length;
    return (xpc_object_t)d;
}

const void *
xpc_data_get_bytes_ptr(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_data)) return NULL;
    return XPC_CAST(xpc_data_t, obj)->data;
}

size_t
xpc_data_get_length(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_data)) return 0;
    return XPC_CAST(xpc_data_t, obj)->length;
}