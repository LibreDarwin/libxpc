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
 * xpc_dictionary.c — dictionary containers for the reimplemented XPC
 * framework.
 *
 * Keys are C-string copies (keeping Apple's exact "key == strcmp"
 * semantics), values are strong references.  Insertion order is
 * preserved — xpc_dictionary_apply iterates in set order.
 */

#include "xpc_internal.h"

static size_t
xpc_dictionary_index(xpc_dictionary_t *d, const char *key)
{
    for (size_t i = 0; i < d->count; i++) {
        if (strcmp(d->keys[i], key) == 0) return i;
    }
    return (size_t)-1;
}

static bool
xpc_dictionary_grow(xpc_dictionary_t *d, size_t need)
{
    if (need <= d->capacity) return true;
    size_t newcap = d->capacity ? d->capacity * 2 : 8;
    while (newcap < need) newcap *= 2;

    char **nk = realloc(d->keys, newcap * sizeof(char *));
    if (!nk) return false;
    xpc_object_t *nv = realloc(d->values, newcap * sizeof(xpc_object_t));
    if (!nv) {
        d->keys = nk;   /* realloc succeeded for keys; keep old values */
        return false;
    }
    memset(nk + d->count, 0, (newcap - d->count) * sizeof(char *));
    memset(nv + d->count, 0, (newcap - d->count) * sizeof(xpc_object_t));
    d->keys = nk;
    d->values = nv;
    d->capacity = newcap;
    return true;
}

xpc_object_t
xpc_dictionary_create(const char *const *keys, const xpc_object_t *values,
    size_t count)
{
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t,
        xpc_object_alloc(&_xpc_type_dictionary, sizeof(xpc_dictionary_t)));
    if (!d) return NULL;

    if (count > 0 && !xpc_dictionary_grow(d, count)) {
        free(d);
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        if (!keys[i] || !values[i]) continue;
        d->keys[d->count] = strdup(keys[i]);
        if (!d->keys[d->count]) continue;
        d->values[d->count] = xpc_retain(values[i]);
        d->count++;
    }
    return (xpc_object_t)d;
}

void
xpc_dictionary_set_value(xpc_object_t dict, const char *key, xpc_object_t value)
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary) || !key) return;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, dict);

    size_t i = xpc_dictionary_index(d, key);
    if (i != (size_t)-1) {
        xpc_object_t old = d->values[i];
        d->values[i] = value ? xpc_retain(value) : NULL;
        xpc_release(old);
        if (!value) {   /* remove the pair entirely */
            free(d->keys[i]);
            for (size_t j = i; j + 1 < d->count; j++) {
                d->keys[j] = d->keys[j + 1];
                d->values[j] = d->values[j + 1];
            }
            d->count--;
        }
        return;
    }

    if (!xpc_dictionary_grow(d, d->count + 1)) return;
    d->keys[d->count] = strdup(key);
    if (!d->keys[d->count]) return;
    d->values[d->count] = value ? xpc_retain(value) : NULL;
    d->count++;
}

xpc_object_t
xpc_dictionary_get_value(xpc_object_t dict, const char *key)
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary) || !key) return NULL;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, dict);
    size_t i = xpc_dictionary_index(d, key);
    return (i == (size_t)-1) ? NULL : d->values[i];
}

size_t
xpc_dictionary_get_count(xpc_object_t dict)
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary)) return 0;
    return XPC_CAST(xpc_dictionary_t, dict)->count;
}

bool
xpc_dictionary_apply(xpc_object_t dict,
    bool (^applier)(const char *key, xpc_object_t value))
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary) || !applier) return false;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, dict);
    for (size_t i = 0; i < d->count; i++) {
        if (!applier(d->keys[i], d->values[i])) return false;
    }
    return true;
}

void
xpc_dictionary_apply_f(xpc_object_t dict, xpc_dictionary_applier_f_t applier,
    void *ctx)
{
    if (!XPC_OBJECT_CHECK(dict, &_xpc_type_dictionary) || !applier) return;
    xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, dict);
    for (size_t i = 0; i < d->count; i++) {
        applier(d->keys[i], d->values[i], ctx);
    }
}
