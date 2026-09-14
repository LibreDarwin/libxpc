/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * wirefmt — shared CPX@ wire-format decoder core.
 *
 * Used by two consumers:
 *   - tools/wiredecode (standalone byte-exact decoder for captured files)
 *   - tools/probe/interpose.c (live capture: renders chased serialized
 *     dictionaries inline, instead of raw hex)
 *
 * The parser implements the encoding documented in docs/WIRE_FORMAT.md:
 *
 *   envelope  = "CPX@" magic(4) version(4) flags(4) body_len(4)
 *               count(4) then count key-value slots totaling body_len - 4.
 *   slot      = NUL-terminated key padded to 4 | type tag (4, LE) | value
 *   tag       = 0x1000 null | 0x2000 bool | 0x3000 int64 | 0x4000 uint64
 *             | 0x5000 double | 0x7000 date | 0x8000 data | 0x9000 string
 *             | 0xa000 uuid | 0xe000 array | 0xf000 dict
 */

#ifndef WIREFMT_H
#define WIREFMT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Parse a CPX@-serialized xpc dictionary starting at buf + hdr_off.
 *
 *   hdr_off       - 0 for a raw serialized body (the OOL payload region
 *                   captured by the interposer's chase), 24 for a full
 *                   mach message whose envelope is inlined right after
 *                   the header.
 *   validate_size - nonzero: require the buffer to end exactly at the
 *                   envelope end (whole-message mode); zero: tolerate a
 *                   capture buffer that extends past body_len (OOL dump
 *                   mode, where the capture window is larger than the
 *                   dictionary).
 *
 * Renders the typed tree to out.  Returns 0 when the whole envelope
 * parsed without errors.
 */
int wirefmt_parse(const uint8_t *buf, size_t len, size_t hdr_off,
    int validate_size, FILE *out);

#endif /* WIREFMT_H */