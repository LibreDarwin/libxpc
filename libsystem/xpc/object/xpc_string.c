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