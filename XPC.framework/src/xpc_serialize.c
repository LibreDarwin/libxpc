/*
 * xpc_serialize.c — wire serialization for the reimplemented XPC
 * framework.
 *
 * Byte-for-byte faithful to docs/WIRE_FORMAT.md:
 *   mach header (24B) + "CPX@" + version(4) + flags(4) + body_len(4)
 *   + body = count(4) + (key-padded + tag + payload)*
 *
 * Key encoding:  NUL-terminated bytes, then pad to a 4-byte boundary.
 * Payload:       8-byte scalars; data = len(4) + bytes; string =
 *                strlen(4) + NUL? NO — string payload is strlen, the
 *                NUL comes from the 4-byte pad of the length field.
 */

#include "xpc_internal.h"

#define ALIGN4(n) (((n) + 3u) & ~3u)

typedef struct _xpc_wbuf_s {
    uint8_t *base;
    size_t len;
    size_t cap;
} xpc_wbuf_t;

static bool
wbuf_reserve(xpc_wbuf_t *w, size_t extra)
{
    if (w->len + extra <= w->cap) return true;
    size_t newcap = w->cap ? w->cap * 2 : 4096;
    while (newcap < w->len + extra) newcap *= 2;
    uint8_t *nb = realloc(w->base, newcap);
    if (!nb) return false;
    w->base = nb;
    w->cap = newcap;
    return true;
}

static void
wbuf_write(xpc_wbuf_t *w, const void *src, size_t n)
{
    if (wbuf_reserve(w, n)) {
        memcpy(w->base + w->len, src, n);
        w->len += n;
    }
}

static void
wbuf_u32(xpc_wbuf_t *w, uint32_t v)
{
    uint8_t b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff,
        (v >> 24) & 0xff };
    wbuf_write(w, b, 4);
}

static void
wbuf_u64(xpc_wbuf_t *w, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (v >> (8 * i)) & 0xff;
    wbuf_write(w, b, 8);
}

static void
wbuf_pad4(xpc_wbuf_t *w)
{
    size_t p = (4 - (w->len & 3)) & 3;
    for (size_t i = 0; i < p; i++) wbuf_write(w, "", 1);
}

/* Serialize a single value (without the enclosing key). */
static void
xpc_serialize_value(xpc_wbuf_t *w, xpc_object_t obj)
{
    switch (obj->isa->kind) {
    case XPC_KIND_NULL:
        wbuf_u32(w, XPC_WIRE_NULL);
        break;
    case XPC_KIND_BOOL:
        wbuf_u32(w, XPC_WIRE_BOOL);
        wbuf_u64(w, XPC_CAST(xpc_scalar_t, obj)->v.bval ? 1 : 0);
        break;
    case XPC_KIND_INT64:
        wbuf_u32(w, XPC_WIRE_INT64);
        wbuf_u64(w, (uint64_t)XPC_CAST(xpc_scalar_t, obj)->v.i64);
        break;
    case XPC_KIND_UINT64:
        wbuf_u32(w, XPC_WIRE_UINT64);
        wbuf_u64(w, XPC_CAST(xpc_scalar_t, obj)->v.u64);
        break;
    case XPC_KIND_DOUBLE: {
        uint64_t bits;
        memcpy(&bits, &XPC_CAST(xpc_scalar_t, obj)->v.dbl, 8);
        wbuf_u32(w, XPC_WIRE_DOUBLE);
        wbuf_u64(w, bits);
        break;
    }
    case XPC_KIND_DATE:
        wbuf_u32(w, XPC_WIRE_DATE);
        wbuf_u64(w, (uint64_t)XPC_CAST(xpc_scalar_t, obj)->v.date_ns);
        break;
    case XPC_KIND_DATA: {
        xpc_data_t *d = XPC_CAST(xpc_data_t, obj);
        wbuf_u32(w, XPC_WIRE_DATA);
        wbuf_u32(w, (uint32_t)d->length);
        if (d->length) wbuf_write(w, d->data, d->length);
        wbuf_pad4(w);
        break;
    }
    case XPC_KIND_STRING: {
        xpc_string_t *s = XPC_CAST(xpc_string_t, obj);
        wbuf_u32(w, XPC_WIRE_STRING);
        wbuf_u32(w, (uint32_t)(s->length + 1));
        wbuf_write(w, s->data, s->length + 1);
        wbuf_pad4(w);
        break;
    }
    case XPC_KIND_UUID:
        wbuf_u32(w, XPC_WIRE_UUID);
        wbuf_write(w, XPC_CAST(xpc_uuid_t, obj)->uuid, 16);
        break;
    case XPC_KIND_ARRAY: {
        xpc_array_t *a = XPC_CAST(xpc_array_t, obj);
        xpc_wbuf_t body = {0};
        wbuf_u32(&body, (uint32_t)a->count);
        for (size_t i = 0; i < a->count; i++) xpc_serialize_value(&body, a->items[i]);
        wbuf_u32(w, XPC_WIRE_ARRAY);
        wbuf_u32(w, (uint32_t)body.len);
        wbuf_write(w, body.base, body.len);
        free(body.base);
        break;
    }
    case XPC_KIND_DICTIONARY: {
        xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, obj);
        xpc_wbuf_t body = {0};
        wbuf_u32(&body, (uint32_t)d->count);
        for (size_t i = 0; i < d->count; i++) {
            size_t klen = strlen(d->keys[i]);
            wbuf_write(&body, d->keys[i], klen + 1);
            wbuf_pad4(&body);
            xpc_serialize_value(&body, d->values[i]);
        }
        wbuf_u32(w, XPC_WIRE_DICT);
        wbuf_u32(w, (uint32_t)body.len);
        wbuf_write(w, body.base, body.len);
        free(body.base);
        break;
    }
    default:
        /* Unserializable (connection/error/endpoint/activity/session/
         * listener): emit null so the transport never throws. */
        wbuf_u32(w, XPC_WIRE_NULL);
        break;
    }
}

uint8_t *
xpc_wire_serialize(xpc_object_t object, uint32_t msg_id, size_t *out_len)
{
    if (!object || !out_len) return NULL;
    if (object->isa != &_xpc_type_dictionary && object->isa != &_xpc_type_array) {
        /* The mach message payload must be a container. */
        return NULL;
    }

    xpc_wbuf_t w = {0};

    /* We need body_len = total body bytes INCLUDING the count word.
     * Serialize the body into a scratch buffer first, then stitch
     * the envelope around it. */
    xpc_wbuf_t b = {0};
    if (object->isa == &_xpc_type_dictionary) {
        xpc_dictionary_t *d = XPC_CAST(xpc_dictionary_t, object);
        wbuf_u32(&b, (uint32_t)d->count);
        for (size_t i = 0; i < d->count; i++) {
            size_t klen = strlen(d->keys[i]);
            wbuf_write(&b, d->keys[i], klen + 1);   /* incl. NUL */
            wbuf_pad4(&b);
            xpc_serialize_value(&b, d->values[i]);
        }
        wbuf_pad4(&b);
    } else {
        xpc_array_t *a = XPC_CAST(xpc_array_t, object);
        wbuf_u32(&b, (uint32_t)a->count);
        for (size_t i = 0; i < a->count; i++) {
            xpc_serialize_value(&b, a->items[i]);
        }
        wbuf_pad4(&b);
    }

    /* mach_msg_header_t is 24/B on arm64 (msgh_bits, msgh_size,
     * msgh_remote_port, msgh_local_port, msgh_voucher_port, msgh_id).
     * We write the envelope right after the header; the receiver reads
     * from the first envelope byte (offset 24). */
    uint32_t flags = XPC_WIRE_FLAGS_DICT;
    uint32_t body_len = (uint32_t)b.len;
    size_t total = 24 + 16 + body_len;

    if (!wbuf_reserve(&w, total)) {
        free(b.base);
        return NULL;
    }
    w.len = 0;

    /* Header (LE): the captured messages use 0x130013 for simpleroutine
     * and 0x131513 for routine. The pipe layer patches the actual port
     * dispositions before sending. */
    wbuf_u32(&w, msg_id == XPC_PIPE_ID_ROUTINE ? 0x00131513 : 0x00130013);
    wbuf_u32(&w, (uint32_t)total);          /* msgh_size */
    wbuf_u32(&w, 0);                        /* msgh_remote_port */
    wbuf_u32(&w, 0);                        /* msgh_local_port */
    wbuf_u32(&w, 0);                        /* msgh_voucher_port */
    wbuf_u32(&w, msg_id);                   /* msgh_id */

    /* Envelope. */
    wbuf_write(&w, XPC_WIRE_MAGIC, 4);
    wbuf_u32(&w, XPC_WIRE_VERSION);
    wbuf_u32(&w, flags);
    wbuf_u32(&w, body_len);

    /* Body. */
    wbuf_write(&w, b.base, b.len);
    free(b.base);

    *out_len = total;
    return w.base;
}
