/*
 * xpc_string.c — string values for the reimplemented XPC framework.
 *
 * Strings store a NUL-terminated copy plus its length, so both strlen
 * and byte-exact comparison are O(1) cheap.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_string_create(const char *value)
{
    return xpc_string_create_with_length(value, value ? strlen(value) : 0);
}

xpc_object_t
xpc_string_create_with_length(const char *value, size_t length)
{
    xpc_string_t *s = XPC_CAST(xpc_string_t,
        xpc_object_alloc(&_xpc_type_string, sizeof(xpc_string_t)));
    if (!s) return NULL;

    /* Allocate length+1 so we can always NUL-terminate. */
    char *copy = malloc(length + 1);
    if (!copy) {
        free(s);
        return NULL;
    }
    if (value) memcpy(copy, value, length);
    copy[length] = '\0';
    s->data = copy;
    s->length = length;
    return (xpc_object_t)s;
}

const char *
xpc_string_get_string_ptr(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_string)) return NULL;
    return XPC_CAST(xpc_string_t, obj)->data;
}

size_t
xpc_string_get_length(xpc_object_t obj)
{
    if (!XPC_OBJECT_CHECK(obj, &_xpc_type_string)) return 0;
    return XPC_CAST(xpc_string_t, obj)->length;
}