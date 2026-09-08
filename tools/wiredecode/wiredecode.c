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
 * wiredecode — standalone decoder for libxpc dictionary wire format.
 *
 * Parses a raw byte stream (hex dump or binary file) into a printable
 * dictionary tree, using the format documented in docs/WIRE_FORMAT.md.
 *
 * Usage:
 *   wiredecode <file.hex>      # hex dump: "00000000 13 00 13 00 ..."
 *   wiredecode <file.bin>      # binary capture
 *   wiredecode -                # read binary from stdin
 *
 * The parser is byte-exact: it validates every alignment, tracks the
 * full 24-byte mach header, and verifies body_len arithmetic exactly as
 * libxpc does. If parsing of any slot fails, it reports the byte offset.
 *
 * Cross-validation: feed the parser a message captured from a real XPC
 * exchange and compare its output against the dictionary you sent, or
 * against xpc_copy_description() in a probe.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

/* ------------------------------------------------------------------ */
/* Parser context + forward declarations                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *p;
    size_t len;
    size_t off;
    int errors;
} ctx_t;

static int hexval(char c);
static const char *typname(uint32_t t);
static int parse_slot(ctx_t *c, int indent);
static int parse_value(ctx_t *c, int indent);

/* ------------------------------------------------------------------ */
/* Hex dump loading                                                     */
/* ------------------------------------------------------------------ */

/*
 * Accepts a hex dump file where each line looks like:
 *   00000000 13 00 13 00 8c 01 00 00 ...
 * Leading address token (followed by space) is optional; bytes may also
 * be separated by any whitespace. Returns a malloc'd buffer.
 */
static uint8_t *load_hex(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror(path); return NULL; }

    uint8_t *buf = malloc(1 << 20);
    size_t len = 0, cap = 1 << 20;
    char line[1024];

    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        /* Optional 8-hex-digit address + colon */
        for (int i = 0; i < 8; i++) {
            if (!isxdigit((unsigned char)p[i])) goto parse_bytes;
        }
        if (p[8] == ':' || p[8] == ' ') {
            p += 8;
            if (*p == ':') p++;
        }
parse_bytes:
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) {
                /* Non-hex token: stop line (e.g. ASCII column) */
                break;
            }
            uint8_t b = (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));
            if (len >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            buf[len++] = b;
            p += 2;
        }
    }
    fclose(fp);
    *out_len = len;
    return buf;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Binary loading                                                       */
/* ------------------------------------------------------------------ */

static uint8_t *load_bin(const char *path, size_t *out_len)
{
    FILE *fp;
    if (strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "rb");
        if (!fp) { perror(path); return NULL; }
    }
    uint8_t *buf = malloc(1 << 20);
    size_t len = 0, cap = 1 << 20;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    if (fp != stdin) fclose(fp);
    *out_len = len;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Wire format parser                                                   */
/* ------------------------------------------------------------------ */

static uint32_t rd_u32(ctx_t *c)
{
    uint32_t v;
    memcpy(&v, c->p + c->off, 4);
    c->off += 4;
    return v;
}

static uint64_t rd_u64(ctx_t *c)
{
    uint64_t v;
    memcpy(&v, c->p + c->off, 8);
    c->off += 8;
    return v;
}

static double rd_f64(ctx_t *c)
{
    uint64_t v = rd_u64(c);
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static int64_t rd_i64(ctx_t *c)
{
    return (int64_t)rd_u64(c);
}

static size_t align4(size_t n) { return (n + 3) & ~3UL; }

static int parse_slots(ctx_t *c, int indent, uint32_t count);

static void emit_indent(int indent)
{
    for (int i = 0; i < indent; i++) fputs("  ", stdout);
}

/*
 * Parse one key-value slot. Returns 0 on success.
 */
static int parse_slot(ctx_t *c, int indent)
{
    

    /* --- key --- */
    size_t ks = c->off;
    while (c->off < c->len && c->p[c->off] != 0) c->off++;
    if (c->off >= c->len) {
        fprintf(stderr, "  ERROR: unterminated key at offset %zu\n", ks);
        c->errors++;
        return -1;
    }
    size_t ke = c->off;
    c->off = ks + align4(ke - ks + 1);           /* NUL + pad to 4 */
    if (c->off > c->len) {
        fprintf(stderr, "  ERROR: key alignment overruns buffer at %zu\n", ks);
        c->errors++;
        return -1;
    }
    char key[256];
    size_t keylen = ke - ks;
    if (keylen >= sizeof key) keylen = sizeof key - 1;
    memcpy(key, c->p + ks, keylen);
    key[keylen] = 0;

    /* --- type tag --- */
    if (c->off + 4 > c->len) {
        fprintf(stderr, "  ERROR: truncated type tag at %zu\n", c->off);
        c->errors++;
        return -1;
    }
    uint32_t typ = rd_u32(c);
    size_t val_off = c->off;

    emit_indent(indent);
    printf("%s [%s]: ", key, typname(typ));

    switch (typ) {
    case 0x1000:  /* NULL */
        putchar('\n');
        break;

    case 0x2000:  { /* BOOL */
        if (c->off + 4 > c->len) goto short_read;
        printf("%s\n", rd_u32(c) ? "true" : "false");
        break;
    }

    case 0x3000:  { /* INT64 */
        if (c->off + 8 > c->len) goto short_read;
        printf("%" PRId64 "\n", rd_i64(c));
        break;
    }

    case 0x4000:  { /* UINT64 */
        if (c->off + 8 > c->len) goto short_read;
        printf("%#" PRIx64 "\n", rd_u64(c));
        break;
    }

    case 0x5000:  { /* DOUBLE */
        if (c->off + 8 > c->len) goto short_read;
        printf("%.17g\n", rd_f64(c));
        break;
    }

    case 0x7000:  { /* DATE */
        if (c->off + 8 > c->len) goto short_read;
        printf("%" PRId64 " (ns)\n", rd_i64(c));
        break;
    }

    case 0x8000:  { /* DATA */
        if (c->off + 4 > c->len) goto short_read;
        uint32_t n = rd_u32(c);
        if (c->off + n > c->len) goto short_read;
        printf("data[%u] =", n);
        for (uint32_t i = 0; i < n; i++) {
            if (i % 16 == 0) fputs("\n", stdout), emit_indent(indent + 1);
            printf(" %02x", c->p[c->off + i]);
        }
        putchar('\n');
        c->off += n;
        c->off = val_off + 4 + align4(n);
        break;
    }

    case 0x9000:  { /* STRING */
        if (c->off + 4 > c->len) goto short_read;
        uint32_t n = rd_u32(c);
        if (c->off + n > c->len) goto short_read;
        /* Strip trailing NUL(s) for display */
        uint32_t disp = n;
        while (disp > 0 && c->p[c->off + disp - 1] == 0) disp--;
        printf("\"");
        for (uint32_t i = 0; i < disp; i++) {
            uint8_t ch = c->p[c->off + i];
            if (ch == '"') fputs("\\\"", stdout);
            else if (ch == '\\') fputs("\\\\", stdout);
            else if (isprint(ch)) putchar(ch);
            else printf("\\x%02x", ch);
        }
        printf("\"\n");
        c->off += n;
        c->off = val_off + 4 + align4(n);
        break;
    }

    case 0xa000:  { /* UUID */
        if (c->off + 16 > c->len) goto short_read;
        const uint8_t *u = c->p + c->off;
        printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
               u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
               u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        c->off += 16;
        break;
    }

    case 0xe000:  { /* ARRAY */
        if (c->off + 4 > c->len) goto short_read;
        uint32_t body_len = rd_u32(c);
        if (val_off + 4 + body_len > c->len) goto short_read;
        printf("array(body=%u):\n", body_len);
        /* Reuse slot machinery with an empty key for each element */
        ctx_t sub = { c->p, c->len, c->off, 0 };
        uint32_t n = rd_u32(&sub);
        if (n > 1 << 24) {
            fprintf(stderr, "  ERROR: corrupt array count %u at %zu\n", n, val_off + 4);
            c->errors++;
            return -1;
        }
        for (uint32_t i = 0; i < n; i++) {
            emit_indent(indent + 1);
            printf("[%u]: ", i);
            parse_value(&sub, indent + 1);
        }
        c->off = val_off + 4 + body_len;
        break;
    }

    case 0xf000:  { /* DICT */
        if (c->off + 4 > c->len) goto short_read;
        uint32_t body_len = rd_u32(c);
        if (val_off + 4 + body_len > c->len) goto short_read;
        printf("dict(body=%u):\n", body_len);
        ctx_t sub = { c->p, c->len, c->off, 0 };
        uint32_t n = rd_u32(&sub);
        if (n > 1 << 24) {
            fprintf(stderr, "  ERROR: corrupt dict count %u at %zu\n", n, val_off + 4);
            c->errors++;
            return -1;
        }
        if (parse_slots(&sub, indent + 1, n) < 0) c->errors++;
        c->off = val_off + 4 + body_len;
        break;
    }

    default:
        emit_indent(indent);
        printf("?? unknown type %#x at %zu\n", typ, val_off - 4);
        c->errors++;
        return -1;
    }
    return 0;

short_read:
    fprintf(stderr, "  ERROR: truncated value at offset %zu\n", val_off);
    c->errors++;
    return -1;
}

/*
 * Parse an array element (tagged value with no key).
 */
static int parse_value(ctx_t *c, int indent)
{
    if (c->off + 4 > c->len) { c->errors++; return -1; }
    uint32_t typ = rd_u32(c);
    size_t val_off = c->off;

    switch (typ) {
    case 0x1000: printf("null\n"); break;
    case 0x2000: printf("%s\n", rd_u32(c) ? "true" : "false"); break;
    case 0x3000: printf("%" PRId64 "\n", rd_i64(c)); break;
    case 0x4000: printf("%#" PRIx64 "\n", rd_u64(c)); break;
    case 0x5000: printf("%.17g\n", rd_f64(c)); break;
    case 0x7000: printf("%" PRId64 " (ns)\n", rd_i64(c)); break;
    case 0x8000: {
        uint32_t n = rd_u32(c);
        printf("data[%u]\n", n);
        c->off += n;
        c->off = val_off + 4 + align4(n);
        break;
    }
    case 0x9000: {
        uint32_t n = rd_u32(c);
        if (c->off + n > c->len) { c->errors++; return -1; }
        /* Strip NUL for display */
        uint32_t disp = n;
        while (disp > 0 && c->p[c->off + disp - 1] == 0) disp--;
        printf("\"");
        for (uint32_t i = 0; i < disp; i++) {
            uint8_t ch = c->p[c->off + i];
            if (ch == '"') fputs("\\\"", stdout);
            else if (ch == '\\') fputs("\\\\", stdout);
            else if (isprint(ch)) putchar(ch);
            else printf("\\x%02x", ch);
        }
        printf("\"\n");
        c->off += n;
        c->off = val_off + 4 + align4(n);
        break;
    }
    case 0xa000:
        printf("uuid\n");
        c->off += 16;
        break;
    case 0xe000: case 0xf000: {
        uint32_t body_len = rd_u32(c);
        ctx_t sub = { c->p, c->len, c->off, 0 };
        printf("%s(body=%u):\n", typ == 0xe000 ? "array" : "dict", body_len);
        uint32_t n = rd_u32(&sub);
        if (typ == 0xe000) {
            for (uint32_t i = 0; i < n; i++) {
                emit_indent(indent + 1);
                printf("[%u]: ", i);
                parse_value(&sub, indent + 1);
            }
        } else {
            parse_slots(&sub, indent + 1, n);
        }
        c->off = val_off + 4 + body_len;
        break;
    }
    default:
        printf("?? unknown type %#x\n", typ);
        c->errors++;
        return -1;
    }
    return 0;
}

static int parse_slots(ctx_t *c, int indent, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (parse_slot(c, indent) < 0) return -1;
    }
    return 0;
}

static const char *typname(uint32_t t)
{
    switch (t) {
    case 0x1000: return "null";
    case 0x2000: return "bool";
    case 0x3000: return "int64";
    case 0x4000: return "uint64";
    case 0x5000: return "double";
    case 0x7000: return "date";
    case 0x8000: return "data";
    case 0x9000: return "string";
    case 0xa000: return "uuid";
    case 0xe000: return "array";
    case 0xf000: return "dict";
    default:     return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.hex|file.bin|->\n", argv[0]);
        fprintf(stderr, "  hex files: \"00000000 13 00 13 00 ...\" lines\n");
        fprintf(stderr, "  binary: raw mach message bytes (or '-' for stdin)\n");
        return 1;
    }

    size_t len;
    uint8_t *buf;

    /* Try as binary first; fall back to hex if it doesn't parse as a message */
    buf = load_bin(argv[1], &len);
    if (!buf) return 1;

    /* Detect text vs binary: hex dumps are almost entirely printable ASCII.
       A real mach message has msgh_bits = 0x13.. — high bytes are zero. */
    int nonprint = 0;
    {
        size_t sample = len < 512 ? len : 512;
        for (size_t i = 0; i < sample; i++) {
            if (!(buf[i] == '\n' || buf[i] == ' ' || isprint(buf[i]))) nonprint++;
        }
    }
    int is_text = (nonprint == 0);   /* zero non-printable bytes => hex dump */

    if (is_text) {
        /* Reload as hex — the first load already read raw text bytes */
        size_t hlen;
        uint8_t *hbuf = load_hex(argv[1], &hlen);
        if (hbuf) { free(buf); buf = hbuf; len = hlen; }
    }

    if (len < 24) {
        fprintf(stderr, "buffer too short for mach header (%zu bytes)\n", len);
        free(buf);
        return 1;
    }

    /* --- mach header --- */
    uint16_t bits = (uint16_t)(buf[0] | (buf[1] << 8));
    uint32_t size = (uint32_t)(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24));
    uint32_t remote = (uint32_t)(buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24));
    uint32_t local  = (uint32_t)(buf[12] | (buf[13] << 8) | (buf[14] << 16) | (buf[15] << 24));
    uint32_t voucher= (uint32_t)(buf[16] | (buf[17] << 8) | (buf[18] << 16) | (buf[19] << 24));
    uint32_t msgh_id= (uint32_t)(buf[20] | (buf[21] << 8) | (buf[22] << 16) | (buf[23] << 24));

    printf("=== mach message (%u bytes) ===\n", size);
    printf("  msgh_bits    = %#06x%s\n", bits,
           (bits & 0xff) == 0x12 ? "  (remote=MOVE_SEND_ONCE)" : "");
    printf("  msgh_size    = %u\n", size);
    printf("  msgh_remote  = %#x\n", remote);
    printf("  msgh_local   = %#x\n", local);
    printf("  msgh_voucher = %#x\n", voucher);
    printf("  msgh_id      = %#08x  %s\n", msgh_id,
           msgh_id == 0x10000000 ? "(simpleroutine request)" :
           msgh_id == 0x40000000 ? "(routine request)"       :
           msgh_id == 0x20000000 ? "(routine reply)"         : "?");

    /* --- XPC envelope --- */
    if (len < 28 || memcmp(buf + 24, "CPX@", 4) != 0) {
        fprintf(stderr, "no CPX@ magic at offset 24 — not an XPC message?\n");
        free(buf);
        return 1;
    }

    ctx_t c = { buf, len, 28, 0 };
    uint32_t version = rd_u32(&c);
    uint32_t flags   = rd_u32(&c);
    uint32_t body_len = rd_u32(&c);
    uint32_t count   = rd_u32(&c);

    printf("=== xpc envelope ===\n");
    printf("  version   = %u\n", version);
    printf("  flags     = %#06x\n", flags);
    printf("  body_len  = %u  (count + slots = %u + %u)\n",
           body_len, count, body_len - 4);
    printf("  count     = %u\n\n", count);

    /* Validate total size: body starts after mach header (24) + CPX@,ver,flags,body_len (16) = offset 40 */
    size_t body_end = 40 + body_len;
    if (body_end != size) {
        printf("  [!] msgh_size (%u) != envelope end (%zu)\n", size, body_end);
    }

    ctx_t slots = { buf, len, 44, 0 };
    parse_slots(&slots, 0, count);

    size_t consumed = slots.off - 44;
    printf("\n=== summary ===\n");
    printf("  slots parsed: %u\n", count);
    printf("  slot bytes  : %zu (%s)\n", consumed,
           consumed == body_len - 4 ? "matches body_len ✓" : "MISMATCH");
    printf("  errors      : %d\n", c.errors + slots.errors);
    if (c.errors + slots.errors == 0 && consumed == body_len - 4) {
        printf("  RESULT      : valid xpc message ✓\n");
    }

    free(buf);
    return (c.errors + slots.errors) ? 1 : 0;
}