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
 * xpc_object.c — core object model for the reimplemented XPC framework.
 *
 * Retain/release, type identity, equality, hashing, and the constructors
 * that all concrete types build on.
 */

#include "xpc_internal.h"

#pragma mark - Lifecycle

xpc_object_t
xpc_object_alloc(xpc_type_t t, size_t size)
{
    xpc_object_t obj = calloc(1, size);
    if (!obj) return NULL;
    obj->isa = t;
    atomic_init(&obj->refs, 1);
    return obj;
}

xpc_object_t
xpc_object_alloc_scalar(xpc_type_t t)
{
    return xpc_object_alloc(t, sizeof(xpc_scalar_t));
}

xpc_object_t
xpc_retain(xpc_object_t obj)
{
    if (!obj) return NULL;
    atomic_fetch_add_explicit(&obj->refs, 1, memory_order_relaxed);
    return obj;
}

void
xpc_release(xpc_object_t obj)
{
    if (!obj) return;
    uint64_t old = atomic_fetch_sub_explicit(&obj->refs, 1,
        memory_order_acq_rel);
    if (old != 1) return;   /* not the last reference */

    switch (obj->isa->kind) {
    case XPC_KIND_STRING: {
        xpc_string_t *s = XPC_CAST(xpc_string_t, obj);
        free(s->data);
        break;
    }
    case XPC_KIND_DATA: {
        xpc_data_t *d = XPC_CAST(xpc_data_t, obj);
        free(d->data);
        break;
    }
    case XPC_KIND_ARRAY: {
        xpc_array_t *a = XPC_CAST(xpc_array_t, obj);
        for (size_t i = 0; i < a->count; i++) {
            xpc_release(a->items[i]);
        }
        free(a->items);
        break;
    }
    case XPC_KIND_DICTIONARY: {
        xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, obj);
        for (size_t i = 0; i < d->count; i++) {
            free(d->keys[i]);
            xpc_release(d->values[i]);
        }
        free(d->keys);
        free(d->values);
        break;
    }
    case XPC_KIND_ERROR: {
        xpc_error_t *e = XPC_CAST(xpc_error_t, obj);
        free(e->desc);
        break;
    }
    case XPC_KIND_MACH_SEND: {
        xpc_mach_send_t *m = XPC_CAST(xpc_mach_send_t, obj);
        if (m->dispose) {
            mach_port_deallocate(mach_task_self(), m->port);
        }
        break;
    }
    case XPC_KIND_ENDPOINT:
    case XPC_KIND_CONNECTION:
        /* mach ports are managed by the kernel; no heap payload. */
        break;
    default:
        break;
    }
    free(obj);
}

#pragma mark - Type

xpc_type_t
xpc_get_type(xpc_object_t obj)
{
    if (!obj) return NULL;
    return obj->isa;
}

#pragma mark - Equality

static bool
xpc_equal_scalar(xpc_object_t a, xpc_object_t b)
{
    xpc_scalar_t *sa = XPC_CAST(xpc_scalar_t, a);
    xpc_scalar_t *sb = XPC_CAST(xpc_scalar_t, b);
    switch (a->isa->kind) {
    case XPC_KIND_BOOL:   return sa->v.bval == sb->v.bval;
    case XPC_KIND_INT64:  return sa->v.i64 == sb->v.i64;
    case XPC_KIND_UINT64: return sa->v.u64 == sb->v.u64;
    case XPC_KIND_DOUBLE: return sa->v.dbl == sb->v.dbl;
    case XPC_KIND_DATE:   return sa->v.date_ns == sb->v.date_ns;
    default:              return false;
    }
}

bool
xpc_equal(xpc_object_t a, xpc_object_t b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->isa != b->isa) return false;

    switch (a->isa->kind) {
    case XPC_KIND_NULL:
        return true;
    case XPC_KIND_BOOL:
    case XPC_KIND_INT64:
    case XPC_KIND_UINT64:
    case XPC_KIND_DOUBLE:
    case XPC_KIND_DATE:
        return xpc_equal_scalar(a, b);
    case XPC_KIND_STRING: {
        xpc_string_t *sa = XPC_CAST(xpc_string_t, a);
        xpc_string_t *sb = XPC_CAST(xpc_string_t, b);
        return sa->length == sb->length &&
            memcmp(sa->data, sb->data, sa->length) == 0;
    }
    case XPC_KIND_DATA: {
        xpc_data_t *da = XPC_CAST(xpc_data_t, a);
        xpc_data_t *db = XPC_CAST(xpc_data_t, b);
        return da->length == db->length &&
            memcmp(da->data, db->data, da->length) == 0;
    }
    case XPC_KIND_UUID:
        return uuid_compare(XPC_CAST(xpc_uuid_t, a)->uuid,
            XPC_CAST(xpc_uuid_t, b)->uuid) == 0;
    case XPC_KIND_ARRAY: {
        xpc_array_t *aa = XPC_CAST(xpc_array_t, a);
        xpc_array_t *ba = XPC_CAST(xpc_array_t, b);
        if (aa->count != ba->count) return false;
        for (size_t i = 0; i < aa->count; i++) {
            if (!xpc_equal(aa->items[i], ba->items[i])) return false;
        }
        return true;
    }
    case XPC_KIND_DICTIONARY: {
        xpc_dictionary_t *da = XPC_CAST(xpc_dictionary_t, a);
        xpc_dictionary_t *db = XPC_CAST(xpc_dictionary_t, b);
        if (da->count != db->count) return false;
        for (size_t i = 0; i < da->count; i++) {
            xpc_object_t other = xpc_dictionary_get_value(b, da->keys[i]);
            if (!other || !xpc_equal(da->values[i], other)) return false;
        }
        return true;
    }
    default:
        /* connections/endpoints are identity-equal. */
        return false;
    }
}

#pragma mark - Hash

uint64_t
xpc_hash(xpc_object_t obj)
{
    if (!obj) return 0;

    /* FNV-1a over a type-tagged digest. */
    uint64_t h = 1469598103934665603ull;
    uint64_t tag = (uint64_t)obj->isa->kind + 1;

#define HASH_BYTE(b) do { \
        h ^= (uint64_t)(uint8_t)(b); \
        h *= 1099511628211ull; \
    } while (0)
#define HASH_NBYTES(p, n) do { \
        const uint8_t *_p = (const uint8_t *)(p); \
        for (size_t _i = 0; _i < (n); _i++) HASH_BYTE(_p[_i]); \
    } while (0)

    HASH_NBYTES(&tag, sizeof tag);

    switch (obj->isa->kind) {
    case XPC_KIND_BOOL:
        HASH_BYTE(XPC_CAST(xpc_scalar_t, obj)->v.bval ? 1 : 0);
        break;
    case XPC_KIND_INT64:
        HASH_NBYTES(&XPC_CAST(xpc_scalar_t, obj)->v.i64, 8);
        break;
    case XPC_KIND_UINT64:
        HASH_NBYTES(&XPC_CAST(xpc_scalar_t, obj)->v.u64, 8);
        break;
    case XPC_KIND_DOUBLE:
        HASH_NBYTES(&XPC_CAST(xpc_scalar_t, obj)->v.dbl, 8);
        break;
    case XPC_KIND_DATE:
        HASH_NBYTES(&XPC_CAST(xpc_scalar_t, obj)->v.date_ns, 8);
        break;
    case XPC_KIND_STRING: {
        xpc_string_t *s = XPC_CAST(xpc_string_t, obj);
        HASH_NBYTES(s->data, s->length);
        break;
    }
    case XPC_KIND_DATA: {
        xpc_data_t *d = XPC_CAST(xpc_data_t, obj);
        HASH_NBYTES(d->data, d->length);
        break;
    }
    case XPC_KIND_UUID:
        HASH_NBYTES(XPC_CAST(xpc_uuid_t, obj)->uuid, 16);
        break;
    case XPC_KIND_ARRAY: {
        xpc_array_t *a = XPC_CAST(xpc_array_t, obj);
        for (size_t i = 0; i < a->count; i++) {
            uint64_t sub = xpc_hash(a->items[i]);
            HASH_NBYTES(&sub, sizeof sub);
        }
        break;
    }
    case XPC_KIND_DICTIONARY: {
        xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, obj);
        for (size_t i = 0; i < d->count; i++) {
            uint64_t kh = xpc_hash(xpc_string_create(d->keys[i]));
            uint64_t vh = xpc_hash(d->values[i]);
            HASH_NBYTES(&kh, sizeof kh);
            HASH_NBYTES(&vh, sizeof vh);
        }
        break;
    }
    default:
        break;   /* identity hash for opaque types */
    }
    return h;

#undef HASH_BYTE
#undef HASH_NBYTES
}