# XPC Dictionary Wire Format — Byte-Level Specification

Reverse-engineered from Apple's libxpc on macOS 26.5 (arm64).
All multi-byte integers are **little-endian**.

---

## 1. Mach Message Envelope

Every XPC message sits inside a standard mach message. The first 24 bytes are the mach message header:

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x00 | 4 | msgh_bits | `MACH_MSGH_BITS(remote, local)` with voucher bits |
| 0x04 | 4 | msgh_size | Total message size in bytes |
| 0x08 | 4 | msgh_remote_port | Send/send-once right to remote endpoint |
| 0x0c | 4 | msgh_local_port | Local port (reply port or 0) |
| 0x10 | 4 | msgh_voucher_port | Mach voucher |
| 0x14 | 4 | msgh_id | Message type identifier (see §6) |

### msgh_bits Conventions

| Value | Meaning |
|-------|---------|
| `0x130013` | simpleroutine — remote=COPY_SEND, local=COPY_SEND, voucher=COPY_SEND |
| `0x131513` | routine — remote=COPY_SEND, local=MAKE_SEND (carries reply port), voucher=COPY_SEND |
| `0x0012` | reply — remote=SEND_ONCE (one-shot reply port), no local/voucher |

---

## 2. XPC Envelope

Immediately after the 24-byte mach header, the XPC payload begins:

| Offset (from msg start) | Size | Field | Value |
|--------------------------|------|-------|-------|
| 0x18 | 4 | magic | `CPX@` (0x40585043 in LE = `43 50 58 40`) |
| 0x1c | 4 | version | `5` (u32 LE) |
| 0x20 | 4 | flags | `0xf000` for dictionary messages |
| 0x24 | 4 | body_len | Byte length of the body (count + slots) |
| 0x28 | 4 | count | Number of top-level key-value slots |

**body_len** = 4 (count field) + total size of all slot data.
Total inline data = 0x2c (28) bytes of header + body_len.
Combined SEND+RECV messages add inline receive space after the body.

---

## 3. Key Encoding

Each key is a NUL-terminated C string, followed by padding to a **4-byte alignment**.

```
key_bytes = strlen(key) + 1    // include NUL
key_size  = (key_bytes + 3) & ~3
```

Key-value pairs are stored **without** any explicit key-length prefix. The decoder scans for the NUL byte, then aligns forward.

---

## 4. Value Types

After each aligned key, the value begins with a 4-byte little-endian type tag:

| Tag | Type | Payload Size | Payload Encoding |
|-----|------|-------------|------------------|
| `0x1000` | NULL | 0 | (none) |
| `0x2000` | BOOL | 4 | u32: `0` = false, `1` = true |
| `0x3000` | INT64 | 8 | i64 LE |
| `0x4000` | UINT64 | 8 | u64 LE |
| `0x5000` | DOUBLE | 8 | f64 LE (IEEE 754) |
| `0x7000` | DATE | 8 | i64 LE (nanoseconds, typically epoch-anchored) |
| `0x8000` | DATA | 4 + N (aligned) | u32 LE byte count + raw bytes, padded to 4B |
| `0x9000` | STRING | 4 + N (aligned) | u32 LE byte count + NUL-terminated UTF-8, padded to 4B |
| `0xa000` | UUID | 16 | Raw 16-byte UUID |
| `0xe000` | ARRAY | 4 + body_len | u32 LE body length + array body (see §5) |
| `0xf000` | DICT | 4 + body_len | u32 LE body length + dict body (see §5) |

### Padding Rules

- **DATA**: payload = `4 (len) + align4(N)` where N is the byte count
- **STRING**: payload = `4 (len) + align4(N)` where N is the byte count (including NUL terminator)
- **ARRAY/DICT**: payload = `4 (body_len) + body_len` — no alignment padding on the body itself
- All other types are fixed-size with no padding

### Total Slot Size (for navigation)

```
slot_size = align4(key_strlen + 1)   // key
          + 4                          // type tag
          + value_payload_size
```

---

## 5. Nested Structures

### Array Body

| Offset | Size | Field |
|--------|------|-------|
| +0 | 4 | inner_count — number of elements |
| +4 | varies | Repeated typed values (tag + payload, same encoding as §4) |

Array elements are **tagged values** without keys. Each element is just a type tag followed by its payload, using the same type encodings as top-level slots.

### Dictionary Body

| Offset | Size | Field |
|--------|------|-------|
| +0 | 4 | inner_count — number of key-value pairs |
| +4 | varies | Repeated key-value slots (same encoding as §3-4) |

Nested dictionaries use **identical** key-value slot encoding as the top level.

### Body Length Scope

`body_len` covers everything from the inner `count` field to the end of the last slot. It does **not** include the 4-byte `body_len` field itself.

---

## 6. Message Type IDs (msgh_id)

| ID | Direction | Meaning |
|----|-----------|---------|
| `0x10000000` | SEND | Simpleroutine request — fire-and-forget, no reply port in msgh_local_port |
| `0x40000000` | SEND | Routine request — reply port carried in msgh_local_port |
| `0x20000000` | SEND (reply) | Routine reply — sent on the send-once right of the reply port |

---

## 7. Dispatch Path

XPC dispatches on the `flags` field (0x20 in the XPC envelope at offset 0x20):

| flags | msgh_id | Dispatch behavior |
|-------|---------|-------------------|
| `0xf000` | `0x10000000` | Simpleroutine — no reply expected |
| `0xf000` | `0x40000000` | Routine — blocks for reply on send-once right |

---

## 8. Send/Receive Options

The `mach_msg` option parameter passed to `__xpc_send_serializer`:

| Flag | Option Value | Meaning |
|------|-------------|---------|
| flag & 2 == 0 | `0x1` (MACH_SEND_MSG) | Send only |
| flag & 2 == 2 | `0x10001` (MACH_SEND_MSG \| MACH_RCV_MSG) | Combined send+receive |

For combined send+receive, the msgh_size in the mach header encodes the total inline buffer size including receive space.

---

## 9. Complete Layout Diagram (396-byte simpleroutine)

```
Offset  Hex    Field
------  -----  ----------------------------
0x00    13..   msgh_bits = 0x130013
0x04    8c 01  msgh_size = 0x18c = 396
0x08    03 1e  msgh_remote_port
0x0c    00 00  msgh_local_port = 0
0x10    03 1c  msgh_voucher_port
0x14    00 10  msgh_id = 0x10000000
0x18    43 50  magic = "CPX@"
0x1c    05 00  version = 5
0x20    00 f0  flags = 0xf000
0x24    64 01  body_len = 0x164 = 356
0x28    0c 00  count = 12
0x2c    ...    slot 0: null (key="null", type=0x1000)
0x38    ...    slot 1: double (key="double", type=0x5000, 8B)
0x4c    ...    slot 2: string (key="string", type=0x9000, 4B+12B)
0x68    ...    slot 3: data (key="data", type=0x8000, 4B+16B)
0x88    ...    slot 4: array (key="array", type=0xe000, 4B+56B)
0xd0    ...    slot 5: bool (key="bool_true", type=0x2000, 4B)
0xe4    ...    slot 6: uint64 (key="uint64", type=0x4000, 8B)
0xf8    ...    slot 7: dict (key="dict", type=0xf000, 4B+44B)
0x134   ...    slot 8: int64 (key="int64", type=0x3000, 8B)
0x148   ...    slot 9: date (key="date", type=0x7000, 8B)
0x15c   ...    slot 10: uuid (key="uuid", type=0xa000, 16B)
0x178   ...    slot 11: bool (key="bool_false", type=0x2000, 4B)
0x18c         end = 0x2c + 0x164 = 396 ✓
```

---

## 10. Reply Construction

A routine reply is built by:

1. Allocate a mach message header
2. `msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0)` = `0x00000012`
3. `msgh_remote_port` = the send-once right from the original request's `msgh_local_port`
4. `msgh_id = 0x20000000`
5. `msgh_local_port = 0`, `msgh_voucher_port = 0`
6. Inline payload = XPC envelope (`CPX@`, version=5, flags=0xf000) + dictionary body

The reply does **not** go through xpc_pipe — it is sent as a raw `mach_msg` on the send-once right.

### 10.1 Example 64-Byte Reply (`{"reply": 1}`)

```
Offset  Hex        Field
------  ---------  ----------------------------
0x00    12 00 00   msgh_bits = 0x12 (MOVE_SEND_ONCE)
0x04    40 00 00   msgh_size = 64
0x08    03 1a      msgh_remote_port (send-once from request)
0x0c    00 00      msgh_local_port = 0
0x10    00 00      msgh_voucher_port = 0
0x14    00 00 00   msgh_id = 0x20000000
0x18    43 50 58   magic = "CPX@"
0x1c    05 00 00   version = 5
0x20    00 f0      flags = 0xf000
0x24    18 00 00   body_len = 24
0x28    01 00 00   count = 1
0x2c    72 65 70   key = "reply\0\0\0" (8B, padded)
0x34    00 30      type = 0x3000 (int64)
0x38    01 00      value = 1 (8B)
0x40              end
```

Note: `msgh_size` must equal `64` exactly — earlier builds used `60`, which
truncated the 8-byte int64 to 4 bytes and produced a corrupt (though
tolerated-by-libxpc) message. The size must cover the entire inline payload.

---

## Appendix: Observed Correlation with libxpc Internals

| Function | Address | Role |
|----------|---------|------|
| `__xpc_pipe_pack_message` | 0x1801d4b58 | Serializes xpc_object_t into mach message buffer |
| `__xpc_send_serializer` | 0x1801d50b0 | Wraps `mach_msg` call with flag-based option selection |
| `__xpc_pipe_mach_msg` | 0x1801f34b4 | Combined send+receive: `mach_msg(msg, option, msg->msgh_size, rcv_size, rcv_name, 0, 0)` |
| `__xpc_pipe_routine` | 0x1801f2ca8 | Full routine lifecycle: serialize → send → wait for reply (checks msgh_id == 0x20000000) |
| `_xpc_pipe_routine_reply` | 0x1801f2e40 | Builds and sends reply via `__xpc_pipe_pack_message(0,0,dict,0,0,0)` → `__xpc_send_serializer` |
| `__xpc_send_serializer` call site | 0x1801d5118 | Sets option: `(flag & 2) ? 0x10001 : 0x1` |

---

*Document generated from byte-exact captures of real XPC traffic, validated against libxpc disassembly, and confirmed by feeding hand-built messages back into libxpc's parser.*
