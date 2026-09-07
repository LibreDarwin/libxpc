# XPC.framework Handbook

This tree contains a small, C-based reimplementation of the core XPC object
model and its inline wire representation. It is intentionally independent of
Apple's libxpc implementation.

## Build

Use BSD make (`bmake`):

```sh
bmake release
bmake test
bmake clean
```

All intermediate objects and test executables are written below `build/`.
The release framework is assembled at `build/release/XPC.framework/`.

The build uses the public header in `XPC.framework/Headers/xpc.h`, the module
map in `XPC.framework/Modules`, and the framework metadata in `Resources`.

## Object model

Every object begins with:

```c
struct _xpc_object_s {
    xpc_type_t isa;
    _Atomic(uint64_t) refs;
};
```

Type descriptors are process-wide singletons. `xpc_get_type()` returns the
object's descriptor, while retain/release use an atomic reference count.
Containers own strong references to their children; replacing or removing a
value releases the old child.

## Values and containers

The core milestone implements null, boolean, signed and unsigned integers,
double, date, data, string, UUID, arrays, and dictionaries. Dictionaries
preserve insertion order. Array and dictionary convenience accessors are
implemented in terms of the typed value constructors.

`xpc_equal()` is structural for scalar and container values. `xpc_hash()` is
FNV-1a over a type-tagged representation. Descriptions are allocated strings
and must be released with `free()`.

## Wire representation

`xpc_wire_serialize()` and `xpc_wire_deserialize()` are private implementation
helpers used by the pipe layer and tests. The format is specified in
`docs/WIRE_FORMAT.md` and validated against captured Apple libxpc messages.

The message layout is:

1. 24-byte Mach message header.
2. `CPX@` magic, version 5, flags `0xf000`, and a 32-bit body length.
3. A body count followed by dictionary slots or tagged array values.

All integers are little-endian. Keys and variable payloads use four-byte
alignment. Data and strings carry a byte count; strings include their NUL in
that count. Nested arrays and dictionaries carry a body length before their
body, and their body length excludes that length field.

The implementation recognizes the three observed message IDs:

* `0x10000000`: simpleroutine request
* `0x40000000`: routine request
* `0x20000000`: routine reply

## Pipe boundary

`xpc_pipe_create_from_port()` and invalidation are present. Simpleroutine now
performs a Mach send, while routine allocates a receive right, sends with a
`MACH_SEND_MSG | MACH_RCV_MSG` transaction, validates reply ID `0x20000000`,
and deserializes the reply. Endpoint discovery and connection lifecycle remain
outside this milestone.

## Verification

`tests/test_core.c` constructs a nested dictionary containing scalar, string,
data, array, and nested dictionary values; serializes it; deserializes it; and
asserts structural equality. The standalone `tools/wiredecode` utility remains
the byte-level validator for captured Apple messages.

## Deliberate boundaries

Connection, endpoint, activity, session, listener, error, dispatch, and
Mach-port ownership semantics are outside this first framework slice. They
should be added only after the object/wire contract remains stable and have
dedicated tests for lifecycle, malformed input, and cross-process behavior.
