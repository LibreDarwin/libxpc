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

typedef struct { const uint8_t *p, *end, *base; } xpc_reader_t;

typedef struct {
    xpc_reader_t r;
    const mach_port_t *ports;   /* send rights from the OOL_PORTS descriptor */
    mach_msg_size_t nports;
} xpc_deser_t;

static bool read_bytes(xpc_reader_t *r, size_t n, const uint8_t **out) {
    if (n > (size_t)(r->end - r->p)) return false;
    *out = r->p; r->p += n; return true;
}
static bool read_u32(xpc_reader_t *r, uint32_t *out) {
    const uint8_t *p; if (!read_bytes(r, 4, &p)) return false;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); return true;
}
static bool read_u64(xpc_reader_t *r, uint64_t *out) {
    const uint8_t *p; if (!read_bytes(r, 8, &p)) return false;
    *out = 0; for (int i = 0; i < 8; i++) *out |= (uint64_t)p[i] << (i * 8);
    return true;
}
static bool align4(xpc_reader_t *r) {
    size_t off = (size_t)(r->p - r->base), n = (4 - (off & 3)) & 3;
    const uint8_t *unused; return read_bytes(r, n, &unused);
}

static xpc_object_t read_value(xpc_deser_t *d);

static xpc_object_t read_value(xpc_deser_t *d) {
    xpc_reader_t *r = &d->r;
    uint32_t tag, n; uint64_t q; const uint8_t *p;
    if (!read_u32(r, &tag)) return NULL;
    /* Value tags are type<<8; the low byte carries the descriptor-table
     * slot for port-backed types (0xd000 = mach-send slot 0). */
    switch (tag & 0xff00) {
    case XPC_WIRE_NULL: return xpc_null_create();
    case XPC_WIRE_BOOL: {
        uint32_t b; if (!read_u32(r, &b)) return NULL; return xpc_bool_create(b != 0);
    }
    case XPC_WIRE_INT64: if (!read_u64(r, &q)) return NULL; return xpc_int64_create((int64_t)q);
    case XPC_WIRE_UINT64: if (!read_u64(r, &q)) return NULL; return xpc_uint64_create(q);
    case XPC_WIRE_DOUBLE: {
        double dbl; if (!read_u64(r, &q)) return NULL; memcpy(&dbl, &q, 8); return xpc_double_create(dbl);
    }
    case XPC_WIRE_DATE: if (!read_u64(r, &q)) return NULL; return xpc_date_create((int64_t)q);
    case XPC_WIRE_UUID: if (!read_bytes(r, 16, &p)) return NULL; return xpc_uuid_create(p);
    case XPC_WIRE_MACH_SEND: {
        /* Slot value: the tag's low byte is the index into the message's
         * port table (port descriptors or OOL_PORTS, per §11.2).  A
         * captured system-libxpc request ends with a bare 0xd000 tag for
         * slot 0.  Without that table the value is unresolvable, so
         * treat it as malformed input. */
        uint32_t idx = tag & 0xff;
        if (!d->ports || idx >= d->nports) return NULL;
        return xpc_mach_send_create_owned(d->ports[idx]);
    }
    case XPC_WIRE_DATA:
        if (!read_u32(r, &n) || !read_bytes(r, n, &p) || !align4(r)) return NULL;
        return xpc_data_create(p, n);
    case XPC_WIRE_STRING:
        if (!read_u32(r, &n) || n == 0 || !read_bytes(r, n, &p) || !align4(r)) return NULL;
        if (p[n - 1] != 0) return NULL;
        return xpc_string_create_with_length((const char *)p, n - 1);
    case XPC_WIRE_ARRAY: {
        uint32_t body_len, count; const uint8_t *body;
        if (!read_u32(r, &body_len) || !read_bytes(r, body_len, &body)) return NULL;
        xpc_deser_t inner = { { body, body + body_len, body }, d->ports, d->nports };
        if (!read_u32(&inner.r, &count)) return NULL;
        xpc_object_t a = xpc_array_create(NULL, 0);
        for (uint32_t i = 0; i < count; i++) {
            xpc_object_t v = read_value(&inner);
            if (!v) { xpc_release(a); return NULL; }
            xpc_array_append_value(a, v); xpc_release(v);
        }
        return a;
    }
    case XPC_WIRE_DICT: {
        uint32_t body_len, count;
        const uint8_t *body;
        if (!read_u32(r, &body_len) || !read_bytes(r, body_len, &body)) return NULL;
        xpc_deser_t inner = { { body, body + body_len, body }, d->ports, d->nports };
        if (!read_u32(&inner.r, &count)) return NULL;
        xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *key = inner.r.p; size_t key_len = 0;
            while (inner.r.p < inner.r.end && *inner.r.p) { inner.r.p++; key_len++; }
            if (inner.r.p >= inner.r.end) { xpc_release(dict); return NULL; }
            inner.r.p++;
            if (!align4(&inner.r)) { xpc_release(dict); return NULL; }
            xpc_object_t v = read_value(&inner);
            if (!v) { xpc_release(dict); return NULL; }
            char *copy = malloc(key_len + 1);
            if (!copy) { xpc_release(v); xpc_release(dict); return NULL; }
            memcpy(copy, key, key_len); copy[key_len] = 0;
            xpc_dictionary_set_value(dict, copy, v);
            free(copy); xpc_release(v);
        }
        return dict;
    }
    default: return NULL;
    }
}

/*
 * Envelope location:  real launchd sends large replies as an OOL
 * descriptor whose region begins with the naked envelope (magic at
 * offset 0), while small replies arrive inline after the mach header
 * (magic at offset 24).  Return the envelope offset, or -1.
 */
static long
xpc_envelope_offset(const uint8_t *b, size_t len)
{
    if (len >= 16 && memcmp(b, XPC_WIRE_MAGIC, 4) == 0) return 0;
    if (len >= 40 && memcmp(b + 24, XPC_WIRE_MAGIC, 4) == 0) return 24;
    return -1;
}

xpc_object_t
xpc_wire_deserialize_with_ports(const void *bytes, size_t len,
    const mach_port_t *ports, mach_msg_size_t nports)
{
    if (!bytes || len < 16) return NULL;
    const uint8_t *b = bytes;
    long env_off = xpc_envelope_offset(b, len);
    if (env_off < 0) return NULL;
    const uint8_t *env = b + env_off;
    uint32_t body_len;
    body_len = (uint32_t)env[12] | ((uint32_t)env[13] << 8) |
        ((uint32_t)env[14] << 16) | ((uint32_t)env[15] << 24);
    size_t body_off = (size_t)env_off + 16;
    if ((size_t)body_len > len - body_off) return NULL;
    xpc_deser_t de = { { b + body_off, b + body_off + body_len,
        b + body_off }, ports, nports };
    xpc_reader_t *r = &de.r;
    uint32_t count; if (!read_u32(r, &count)) return NULL;
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *key = r->p; size_t key_len = 0;
        while (r->p < r->end && *r->p) { r->p++; key_len++; }
        if (r->p >= r->end) { xpc_release(d); return NULL; }
        r->p++; if (!align4(r)) { xpc_release(d); return NULL; }
        xpc_object_t v = read_value(&de);
        if (!v) { xpc_release(d); return NULL; }
        char *copy = malloc(key_len + 1);
        if (!copy) { xpc_release(v); xpc_release(d); return NULL; }
        memcpy(copy, key, key_len); copy[key_len] = 0;
        xpc_dictionary_set_value(d, copy, v);
        free(copy); xpc_release(v);
    }
    return d;
}

xpc_object_t
xpc_wire_deserialize(const void *bytes, size_t len)
{
    return xpc_wire_deserialize_with_ports(bytes, len, NULL, 0);
}
