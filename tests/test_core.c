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

#include "xpc_internal.h"
#include <assert.h>

int main(void) {
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(d, "answer", 42);
    xpc_dictionary_set_string(d, "text", "hello");
    const uint8_t bytes[] = { 1, 2, 3, 4, 5 };
    xpc_dictionary_set_data(d, "blob", bytes, sizeof(bytes));
    xpc_object_t a = xpc_array_create(NULL, 0);
    xpc_array_append_value(a, xpc_bool_create(true));
    xpc_object_t nested = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(nested, "n", 7);
    xpc_array_append_value(a, nested);
    xpc_release(nested);
    xpc_dictionary_set_value(d, "array", a);
    xpc_release(a);

    size_t len = 0;
    uint8_t *wire = xpc_wire_serialize(d, XPC_PIPE_ID_SIMPLEROUTINE, &len);
    assert(wire && len > 40);
    xpc_object_t copy = xpc_wire_deserialize(wire, len);
    assert(copy && xpc_equal(d, copy));
    assert(xpc_dictionary_get_int64(copy, "answer") == 42);
    assert(strcmp(xpc_dictionary_get_string(copy, "text"), "hello") == 0);
    free(wire);
    xpc_pipe_t pipe = xpc_pipe_create_from_port(MACH_PORT_NULL, 0);
    assert(pipe);
    assert(xpc_pipe_simpleroutine(pipe, d, NULL) != KERN_SUCCESS);
    assert(xpc_pipe_invalidate(pipe) == KERN_SUCCESS);
    xpc_release(copy); xpc_release(d);
    return 0;
}
