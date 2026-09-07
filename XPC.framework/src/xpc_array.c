/*
 * xpc_array.c — array containers for the reimplemented XPC framework.
 *
 * Arrays hold strong references in insertion order; count == items
 * actually stored, capacity == allocated slot count.
 */

#include "xpc_internal.h"

xpc_object_t
xpc_array_create(const xpc_object_t *objects, size_t count)
{
    xpc_array_t *a = XPC_CAST(xpc_array_t,
        xpc_object_alloc(&_xpc_type_array, sizeof(xpc_array_t)));
    if (!a) return NULL;

    if (count > 0) {
        size_t cap = count < 8 ? 8 : count;
        a->items = calloc(cap, sizeof(xpc_object_t));
        if (!a->items) {
            free(a);
            return NULL;
        }
        a->capacity = cap;
        if (objects) {
            for (size_t i = 0; i < count; i++) {
                a->items[i] = xpc_retain(objects[i]);
            }
        }
        a->count = count;
    }
    return (xpc_object_t)a;
}

static bool
xpc_array_grow(xpc_array_t *a, size_t need)
{
    if (need <= a->capacity) return true;
    size_t newcap = a->capacity ? a->capacity * 2 : 8;
    while (newcap < need) newcap *= 2;
    xpc_object_t *n = realloc(a->items, newcap * sizeof(xpc_object_t));
    if (!n) return false;
    memset(n + a->count, 0, (newcap - a->count) * sizeof(xpc_object_t));
    a->items = n;
    a->capacity = newcap;
    return true;
}

void
xpc_array_append_value(xpc_object_t array, xpc_object_t value)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array) || !value) return;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (!xpc_array_grow(a, a->count + 1)) return;
    a->items[a->count++] = xpc_retain(value);
}

void
xpc_array_set_value(xpc_object_t array, size_t index, xpc_object_t value)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (index >= a->count) return;   /* Apple: set only replaces existing */

    xpc_object_t old = a->items[index];
    a->items[index] = value ? xpc_retain(value) : NULL;
    xpc_release(old);
    if (!value) {   /* NULL value removes the slot */
        for (size_t i = index; i + 1 < a->count; i++) {
            a->items[i] = a->items[i + 1];
        }
        a->count--;
    }
}

xpc_object_t
xpc_array_get_value(xpc_object_t array, size_t index)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return NULL;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    if (index >= a->count) return NULL;
    return a->items[index];
}

size_t
xpc_array_get_count(xpc_object_t array)
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array)) return 0;
    return XPC_CAST(xpc_array_t, array)->count;
}

bool
xpc_array_apply(xpc_object_t array,
    bool (^applier)(size_t index, xpc_object_t value))
{
    if (!XPC_OBJECT_CHECK(array, &_xpc_type_array) || !applier) return false;
    xpc_array_t *a = XPC_CAST(xpc_array_t, array);
    for (size_t i = 0; i < a->count; i++) {
        if (!applier(i, a->items[i])) return false;
    }
    return true;
}
