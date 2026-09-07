#include "xpc_internal.h"

typedef struct { const uint8_t *p, *end, *base; } xpc_reader_t;

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

static xpc_object_t read_value(xpc_reader_t *r);

static xpc_object_t read_value(xpc_reader_t *r) {
    uint32_t tag, n; uint64_t q; const uint8_t *p;
    if (!read_u32(r, &tag)) return NULL;
    switch (tag) {
    case XPC_WIRE_NULL: return xpc_null_create();
    case XPC_WIRE_BOOL: if (!read_u64(r, &q)) return NULL; return xpc_bool_create(q != 0);
    case XPC_WIRE_INT64: if (!read_u64(r, &q)) return NULL; return xpc_int64_create((int64_t)q);
    case XPC_WIRE_UINT64: if (!read_u64(r, &q)) return NULL; return xpc_uint64_create(q);
    case XPC_WIRE_DOUBLE: {
        double d; if (!read_u64(r, &q)) return NULL; memcpy(&d, &q, 8); return xpc_double_create(d);
    }
    case XPC_WIRE_DATE: if (!read_u64(r, &q)) return NULL; return xpc_date_create((int64_t)q);
    case XPC_WIRE_UUID: if (!read_bytes(r, 16, &p)) return NULL; return xpc_uuid_create(p);
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
        xpc_reader_t inner = { body, body + body_len, body };
        if (!read_u32(&inner, &count)) return NULL;
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
        xpc_reader_t inner = { body, body + body_len, body };
        if (!read_u32(&inner, &count)) return NULL;
        xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *key = inner.p; size_t key_len = 0;
            while (inner.p < inner.end && *inner.p) { inner.p++; key_len++; }
            if (inner.p >= inner.end) { xpc_release(d); return NULL; }
            inner.p++;
            if (!align4(&inner)) { xpc_release(d); return NULL; }
            xpc_object_t v = read_value(&inner);
            if (!v) { xpc_release(d); return NULL; }
            char *copy = malloc(key_len + 1);
            if (!copy) { xpc_release(v); xpc_release(d); return NULL; }
            memcpy(copy, key, key_len); copy[key_len] = 0;
            xpc_dictionary_set_value(d, copy, v);
            free(copy); xpc_release(v);
        }
        return d;
    }
    default: return NULL;
    }
}

xpc_object_t xpc_wire_deserialize(const void *bytes, size_t len) {
    if (!bytes || len < 40) return NULL;
    const uint8_t *b = bytes; uint32_t body_len;
    if (memcmp(b + 24, XPC_WIRE_MAGIC, 4) != 0) return NULL;
    body_len = (uint32_t)b[36] | ((uint32_t)b[37] << 8) |
        ((uint32_t)b[38] << 16) | ((uint32_t)b[39] << 24);
    if ((size_t)body_len > len - 40) return NULL;
    xpc_reader_t r = { b + 40, b + 40 + body_len, b + 40 };
    uint32_t count; if (!read_u32(&r, &count)) return NULL;
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *key = r.p; size_t key_len = 0;
        while (r.p < r.end && *r.p) { r.p++; key_len++; }
        if (r.p >= r.end) { xpc_release(d); return NULL; }
        r.p++; if (!align4(&r)) { xpc_release(d); return NULL; }
        xpc_object_t v = read_value(&r);
        if (!v) { xpc_release(d); return NULL; }
        char *copy = malloc(key_len + 1);
        if (!copy) { xpc_release(v); xpc_release(d); return NULL; }
        memcpy(copy, key, key_len); copy[key_len] = 0;
        xpc_dictionary_set_value(d, copy, v);
        free(copy); xpc_release(v);
    }
    return d;
}
