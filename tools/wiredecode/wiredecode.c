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
 * The parser core (wirefmt.c) is shared with the probe interposer, so a
 * capture decoded inline by the interposer can be cross-validated here.
 *
 * Usage:
 *   wiredecode <file.hex>      # hex dump: "00000000 13 00 13 00 ..."
 *   wiredecode <file.bin>      # binary capture
 *   wiredecode -                # read binary from stdin
 *   wiredecode --raw <file>    # envelope at offset 0 (OOL capture body)
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

#include "wirefmt.h"

/* ------------------------------------------------------------------ */
/* Hex dump loading                                                     */
/* ------------------------------------------------------------------ */

static int hexval(char c);

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
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int raw = 0;
    const char *path;
    if (argc >= 2 && strcmp(argv[1], "--raw") == 0) {
        raw = 1;
        if (argc < 3) {
            fprintf(stderr, "usage: %s --raw <file.hex|file.bin|->\n", argv[0]);
            return 1;
        }
        path = argv[2];
    } else if (argc >= 2) {
        path = argv[1];
    } else {
        fprintf(stderr, "usage: %s [--raw] <file.hex|file.bin|->\n", argv[0]);
        fprintf(stderr, "  --raw: envelope at offset 0 (OOL capture body)\n");
        fprintf(stderr, "  default: full mach message, envelope at offset 24\n");
        fprintf(stderr, "  hex files: \"00000000 13 00 13 00 ...\" lines\n");
        return 1;
    }

    size_t len;
    uint8_t *buf;

    /* Try as binary first; fall back to hex if it doesn't parse as a message */
    buf = load_bin(path, &len);
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
        uint8_t *hbuf = load_hex(path, &hlen);
        if (hbuf) { free(buf); buf = hbuf; len = hlen; }
    }

    if (raw) {
        if (len < 16) {
            fprintf(stderr, "buffer too short for CPX@ envelope (%zu bytes)\n",
                len);
            free(buf);
            return 1;
        }
        int rc = wirefmt_parse(buf, len, 0, 0, stdout);
        free(buf);
        return rc;
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
    const char *id_kind;
    char id_buf[64];
    if (msgh_id == 0x20000000) {
        id_kind = "routine reply";
    } else if (msgh_id & 0x40000000) {
        snprintf(id_buf, sizeof(id_buf), "routine request; routine 0x%x",
            msgh_id & 0xffff);
        id_kind = id_buf;
    } else if (msgh_id & 0x10000000) {
        id_kind = "simpleroutine request";
    } else {
        id_kind = "?";
    }
    printf("  msgh_id      = %#08x  %s\n", msgh_id, id_kind);

    /* --- XPC envelope --- */
    if (len < 28 || memcmp(buf + 24, "CPX@", 4) != 0) {
        fprintf(stderr, "no CPX@ magic at offset 24 — not an XPC message?\n");
        free(buf);
        return 1;
    }

    int rc = wirefmt_parse(buf, len, 24, 1, stdout);
    free(buf);
    return rc;
}