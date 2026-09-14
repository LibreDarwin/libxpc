/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2026 Sunneva N. Mariu
 * All rights reserved.
 *
 * Shared CPX@ wire-format decoder core — see wirefmt.h for the format
 * summary and consumer list.
 */

#include "wirefmt.h"

#include <inttypes.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Parser context                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
	const uint8_t *p;
	size_t len;
	size_t off;
	int errors;
} wfctx_t;

static uint32_t rd_u32(wfctx_t *c)
{
	uint32_t v;
	memcpy(&v, c->p + c->off, 4);
	c->off += 4;
	return v;
}

static uint64_t rd_u64(wfctx_t *c)
{
	uint64_t v;
	memcpy(&v, c->p + c->off, 8);
	c->off += 8;
	return v;
}

static double rd_f64(wfctx_t *c)
{
	uint64_t v = rd_u64(c);
	double d;
	memcpy(&d, &v, 8);
	return d;
}

static int64_t rd_i64(wfctx_t *c)
{
	return (int64_t)rd_u64(c);
}

static size_t align4(size_t n) { return (n + 3) & ~3UL; }

/* Canonical tag: type_id << 12.  Port-backed types (mach_send, shmem,
 * endpoint, …) carry the descriptor-table index in the low byte, so a
 * value tag is `canonical | idx` (0xd003 = mach_send, slot 3).  The
 * 0xff00 mask used by the reference deserializer breaks for extended
 * tags (0x12000 & 0xff00 == 0x2000 == bool), hence 0xfff00 here. */
static uint32_t tag_base(uint32_t t) { return t & 0xfff00u; }
static uint32_t tag_idx(uint32_t t) { return t & 0xffu; }

static const char *typname(uint32_t t);

static void emit_indent(FILE *out, int indent)
{
	for (int i = 0; i < indent; i++) fputs("  ", out);
}

static int parse_slots(wfctx_t *c, int indent, uint32_t count, FILE *out);
static int parse_value(wfctx_t *c, int indent, FILE *out);

/* ------------------------------------------------------------------ */
/* Slot / value parsers                                                 */
/* ------------------------------------------------------------------ */

/*
 * Parse one key-value slot. Returns 0 on success.
 */
static int parse_slot(wfctx_t *c, int indent, FILE *out)
{
	/* --- key --- */
	size_t ks = c->off;
	while (c->off < c->len && c->p[c->off] != 0) c->off++;
	if (c->off >= c->len) {
		fprintf(out, "  ERROR: unterminated key at offset %zu\n", ks);
		c->errors++;
		return -1;
	}
	size_t ke = c->off;
	c->off = ks + align4(ke - ks + 1);           /* NUL + pad to 4 */
	if (c->off > c->len) {
		fprintf(out, "  ERROR: key alignment overruns buffer at %zu\n", ks);
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
		fprintf(out, "  ERROR: truncated type tag at %zu\n", c->off);
		c->errors++;
		return -1;
	}
	uint32_t typ = rd_u32(c);
	uint32_t tb = tag_base(typ);      /* canonical type id */
	uint32_t idx = tag_idx(typ);      /* descriptor-table slot */
	size_t val_off = c->off;

	emit_indent(out, indent);
	fprintf(out, "%s [%s]: ", key, typname(tb));

	switch (tb) {
	case 0x1000:  /* NULL */
		fputc('\n', out);
		break;

	case 0x2000:  { /* BOOL */
		if (c->off + 4 > c->len) goto short_read;
		fprintf(out, "%s\n", rd_u32(c) ? "true" : "false");
		break;
	}

	case 0x3000:  { /* INT64 */
		if (c->off + 8 > c->len) goto short_read;
		fprintf(out, "%" PRId64 "\n", rd_i64(c));
		break;
	}

	case 0x4000:  { /* UINT64 */
		if (c->off + 8 > c->len) goto short_read;
		fprintf(out, "%#" PRIx64 "\n", rd_u64(c));
		break;
	}

	case 0x5000:  { /* DOUBLE */
		if (c->off + 8 > c->len) goto short_read;
		fprintf(out, "%.17g\n", rd_f64(c));
		break;
	}

	case 0x7000:  { /* DATE */
		if (c->off + 8 > c->len) goto short_read;
		fprintf(out, "%" PRId64 " (ns)\n", rd_i64(c));
		break;
	}

	case 0x8000:  { /* DATA */
		if (c->off + 4 > c->len) goto short_read;
		uint32_t n = rd_u32(c);
		if (c->off + n > c->len) goto short_read;
		fprintf(out, "data[%u] =", n);
		for (uint32_t i = 0; i < n; i++) {
			if (i % 16 == 0) {
				fputc('\n', out);
				emit_indent(out, indent + 1);
			}
			fprintf(out, " %02x", c->p[c->off + i]);
		}
		fputc('\n', out);
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
		fputc('"', out);
		for (uint32_t i = 0; i < disp; i++) {
			uint8_t ch = c->p[c->off + i];
			if (ch == '"') fputs("\\\"", out);
			else if (ch == '\\') fputs("\\\\", out);
			else if (ch >= 0x20 && ch <= 0x7e) fputc(ch, out);
			else fprintf(out, "\\x%02x", ch);
		}
		fputs("\"\n", out);
		c->off += n;
		c->off = val_off + 4 + align4(n);
		break;
	}

	case 0xa000:  { /* UUID */
		if (c->off + 16 > c->len) goto short_read;
		const uint8_t *u = c->p + c->off;
		fprintf(out,
		    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
		    "%02x%02x%02x%02x%02x%02x\n",
		    u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
		    u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
		c->off += 16;
		break;
	}

	case 0xe000:  { /* ARRAY */
		if (c->off + 4 > c->len) goto short_read;
		uint32_t body_len = rd_u32(c);
		if (val_off + 4 + body_len > c->len) goto short_read;
		fprintf(out, "array(body=%u):\n", body_len);
		/* Reuse slot machinery with an empty key for each element */
		wfctx_t sub = { c->p, c->len, c->off, 0 };
		uint32_t n = rd_u32(&sub);
		if (n > 1 << 24) {
			fprintf(out, "  ERROR: corrupt array count %u at %zu\n",
			    n, val_off + 4);
			c->errors++;
			return -1;
		}
		for (uint32_t i = 0; i < n; i++) {
			emit_indent(out, indent + 1);
			fprintf(out, "[%u]: ", i);
			parse_value(&sub, indent + 1, out);
		}
		c->off = val_off + 4 + body_len;
		break;
	}

	case 0xf000:  { /* DICT */
		if (c->off + 4 > c->len) goto short_read;
		uint32_t body_len = rd_u32(c);
		if (val_off + 4 + body_len > c->len) goto short_read;
		fprintf(out, "dict(body=%u):\n", body_len);
		wfctx_t sub = { c->p, c->len, c->off, 0 };
		uint32_t n = rd_u32(&sub);
		if (n > 1 << 24) {
			fprintf(out, "  ERROR: corrupt dict count %u at %zu\n",
			    n, val_off + 4);
			c->errors++;
			return -1;
		}
		if (parse_slots(&sub, indent + 1, n, out) < 0) c->errors++;
		c->off = val_off + 4 + body_len;
		break;
	}

	case 0xb000:  /* FD — fileport mach port */
	case 0x11000: /* CONNECTION */
	case 0x15000: /* MACH_RECV — recv right */
		/* Layout not yet confirmed empirically — mark as known
		 * but unsized so the slot cursor can't advance safely. */
		fputc('\n', out);
		emit_indent(out, indent);
		fprintf(out, "[!] %s (%#x): value layout not decoded yet — "
		    "raw tail starts at %zu\n", typname(tb), tb, val_off);
		c->errors++;
		return -1;

	case 0xd000:  { /* MACH_SEND — zero-payload; right rides in the
	                 * message's port descriptor table.  The tag's low
	                 * byte is the table slot. */
		if (idx != 0) {
			fprintf(out, "(send right, port-table slot %u)\n",
			    idx);
		} else {
			fputs("(send right, port-table slot 0)\n", out);
		}
		break;
	}

	case 0xc000:  { /* SHMEM — memory entry right plus the entry's
	                 * page-aligned size as a u64 (probe_routine 0x33c
	                 * captures; launchd v7 version replies). */
		if (c->off + 8 > c->len) goto short_read;
		fprintf(out, "(shmem size 0x%llx, port-table slot %u)\n",
		    (unsigned long long)rd_u64(c), idx);
		break;
	}

	case 0x12000: /* ENDPOINT — zero-payload; port ref in msg descriptors */
	{
		fprintf(out, "(port ref, msg descriptor slot %u)\n", idx);
		break;
	}

	case 0x6000:  /* POINTER — internal, never on the wire */
	case 0x10000: /* ERROR */
	case 0x13000: /* SERIALIZER — internal */
	case 0x14000: /* PIPE */
	case 0x16000: /* BUNDLE */
	case 0x17000: /* SERVICE */
	case 0x18000: /* SERVICE_INSTANCE */
	case 0x19000: /* ACTIVITY */
	case 0x1a000: /* FILE_TRANSFER */
		/* Tag is recognized but the value layout is not yet
		 * decoded — name it and stop rather than misread bytes. */
		fputc('\n', out);
		emit_indent(out, indent);
		fprintf(out, "[!] %s (%#x): value layout not decoded yet — "
		    "raw tail starts at %zu\n", typname(tb), tb, val_off);
		c->errors++;
		return -1;

	default:
		emit_indent(out, indent);
		fprintf(out, "?? unknown type %#x at %zu\n", typ, val_off - 4);
		c->errors++;
		return -1;
	}
	return 0;

short_read:
	fprintf(out, "  ERROR: truncated value at offset %zu\n", val_off);
	c->errors++;
	return -1;
}

/*
 * Parse an array element (tagged value with no key).
 */
static int parse_value(wfctx_t *c, int indent, FILE *out)
{
	if (c->off + 4 > c->len) {
		fprintf(out, "  ERROR: truncated array element tag at %zu\n", c->off);
		c->errors++;
		return -1;
	}
	uint32_t typ = rd_u32(c);
	size_t val_off = c->off;

	switch (typ) {
	case 0x1000: fprintf(out, "null\n"); break;
	case 0x2000: fprintf(out, "%s\n", rd_u32(c) ? "true" : "false"); break;
	case 0x3000: fprintf(out, "%" PRId64 "\n", rd_i64(c)); break;
	case 0x4000: fprintf(out, "%#" PRIx64 "\n", rd_u64(c)); break;
	case 0x5000: fprintf(out, "%.17g\n", rd_f64(c)); break;
	case 0x7000: fprintf(out, "%" PRId64 " (ns)\n", rd_i64(c)); break;
	case 0x8000: {
		uint32_t n = rd_u32(c);
		if (c->off + n > c->len) { c->errors++; return -1; }
		fprintf(out, "data[%u]\n", n);
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
		fputc('"', out);
		for (uint32_t i = 0; i < disp; i++) {
			uint8_t ch = c->p[c->off + i];
			if (ch == '"') fputs("\\\"", out);
			else if (ch == '\\') fputs("\\\\", out);
			else if (ch >= 0x20 && ch <= 0x7e) fputc(ch, out);
			else fprintf(out, "\\x%02x", ch);
		}
		fputs("\"\n", out);
		c->off += n;
		c->off = val_off + 4 + align4(n);
		break;
	}
	case 0xa000:
		fprintf(out, "uuid\n");
		c->off += 16;
		break;
	case 0xe000: case 0xf000: {
		uint32_t body_len = rd_u32(c);
		wfctx_t sub = { c->p, c->len, c->off, 0 };
		fprintf(out, "%s(body=%u):\n",
		    typ == 0xe000 ? "array" : "dict", body_len);
		uint32_t n = rd_u32(&sub);
		if (typ == 0xe000) {
			for (uint32_t i = 0; i < n; i++) {
				emit_indent(out, indent + 1);
				fprintf(out, "[%u]: ", i);
				parse_value(&sub, indent + 1, out);
			}
		} else {
			parse_slots(&sub, indent + 1, n, out);
		}
		c->off = val_off + 4 + body_len;
		break;
	}
	default:
		fprintf(out, "?? unknown type %#x\n", typ);
		c->errors++;
		return -1;
	}
	return 0;
}

static int parse_slots(wfctx_t *c, int indent, uint32_t count, FILE *out)
{
	for (uint32_t i = 0; i < count; i++) {
		if (parse_slot(c, indent, out) < 0) return -1;
	}
	return 0;
}

static const char *typname(uint32_t t)
{
	switch (t) {
	case 0x1000:  return "null";
	case 0x2000:  return "bool";
	case 0x3000:  return "int64";
	case 0x4000:  return "uint64";
	case 0x5000:  return "double";
	case 0x6000:  return "pointer";
	case 0x7000:  return "date";
	case 0x8000:  return "data";
	case 0x9000:  return "string";
	case 0xa000:  return "uuid";
	case 0xb000:  return "fd";
	case 0xc000:  return "shmem";
	case 0xd000:  return "mach_send";
	case 0xe000:  return "array";
	case 0xf000:  return "dict";
	case 0x10000: return "error";
	case 0x11000: return "connection";
	case 0x12000: return "endpoint";
	case 0x13000: return "serializer";
	case 0x14000: return "pipe";
	case 0x15000: return "mach_recv";
	case 0x16000: return "bundle";
	case 0x17000: return "service";
	case 0x18000: return "service_instance";
	case 0x19000: return "activity";
	case 0x1a000: return "file_transfer";
	default:     return "unknown";
	}
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int
wirefmt_parse(const uint8_t *buf, size_t len, size_t hdr_off,
    int validate_size, FILE *out)
{
	if (buf == NULL || out == NULL || len < hdr_off + 4) {
		return 1;
	}

	/* --- envelope --- */
	if (memcmp(buf + hdr_off, "CPX@", 4) != 0) {
		fprintf(out, "no CPX@ magic at offset %zu — not an XPC body?\n",
		    hdr_off);
		return 1;
	}
	wfctx_t c = { buf, len, hdr_off + 4, 0 };
	uint32_t version  = rd_u32(&c);
	uint32_t flags    = rd_u32(&c);
	uint32_t body_len = rd_u32(&c);
	uint32_t count    = rd_u32(&c);

	fprintf(out, "=== xpc envelope ===\n");
	fprintf(out, "  version   = %u\n", version);
	fprintf(out, "  flags     = %#06x\n", flags);
	fprintf(out, "  body_len  = %u  (count + slots = %u + %u)\n",
	    body_len, count, body_len - 4);
	fprintf(out, "  count     = %u\n\n", count);

	/* --- bound + size validation --- */
	size_t body_end = hdr_off + 16 + body_len;
	if (body_end > len) {
		fprintf(out, "  [!] envelope end (%zu) exceeds capture (%zu)\n",
		    body_end, len);
		c.errors++;
		fprintf(out, "\n=== summary ===\n");
		fprintf(out, "  slots parsed: %u\n", count);
		fprintf(out, "  errors      : %d\n", c.errors);
		fprintf(out, "  RESULT      : %s\n",
		    c.errors ? "INVALID (truncated envelope)" :
		    "valid xpc message ✓");
		return 1;
	}
	if (validate_size && body_end != len) {
		fprintf(out, "  [!] capture end (%zu) != envelope end (%zu)\n",
		    len, body_end);
		c.errors++;
	}

	/* --- slots --- */
	wfctx_t slots = { buf, len, hdr_off + 20, 0 };
	parse_slots(&slots, 0, count, out);

	size_t consumed = slots.off - (hdr_off + 20);
	fprintf(out, "\n=== summary ===\n");
	fprintf(out, "  slots parsed: %u\n", count);
	fprintf(out, "  slot bytes  : %zu (%s)\n", consumed,
	    consumed == body_len - 4 ? "matches body_len ✓" : "MISMATCH");
	fprintf(out, "  errors      : %d\n", c.errors + slots.errors);
	fprintf(out, "  RESULT      : %s\n",
	    c.errors + slots.errors == 0 ? "valid xpc message ✓" :
	    "decode errors");
	return (c.errors + slots.errors) ? 1 : 0;
}