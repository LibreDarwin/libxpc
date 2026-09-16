# XPC.framework Handbook

This tree contains a small, C-based reimplementation of the core XPC object
model and its inline wire representation. It is intentionally independent of
Apple's libxpc implementation, and is shaped like Apple's libSystem family:
the library component lives at `src/libsystem/xpc/` (producing
`libsystem_xpc.dylib`), with `launchctl` and the launchd test stub as
siblings under `src/` and the XPC.framework umbrella project at
`src/XPC.framework/`.

## Build

Use BSD make (`bmake`):

```sh
bmake release
bmake test
bmake clean
```

All intermediate objects and test executables are written below `build/`.
The component builds `build/release/libsystem_xpc.dylib`; the re-export
umbrella is assembled at `build/release/XPC.framework/`.

`libsystem_xpc.dylib` also carries liblaunch: launchd-842's `liblaunch.c`,
`libvproc.c` and `libbootstrap.c` with their `job` and `helper` MIG stubs,
the `launch_*`, `vproc_*` and `bootstrap_*` API that modern Darwin ships
inside libxpc. They build from the patched launchd copy with Apple's own
flags, not our `-Werror`. Their private headers come from xcode-tools'
internal SDK, searched after the public SDK; the build finds a built
xcode-tools beside this tree (`../xcode-tools`, or
`../../Developer/xcode-tools` inside LibreDarwin), or takes
`INTERNAL_SDK=<path>`.

`build/release/launchd` is Apple's launchd-842 itself, built from the same
patched copy with the same flags and linked against `libsystem_xpc`;
`launchd_stub` stays as the test double `bmake test` drives. Patches 0003
to 0005 in `mk/patches/launchd/` turn off quarantine, Sandbox and libauditd
(LibreDarwin's kernel has no such policy), fit launchd to a modern xnu and
SDK, and patch out the XPC domain subsystem, whose `domain.defs` Apple
never published. `include/` holds what no SDK carries for these sources:
`xpc/launchd.h` -- the routine keys, operations and jetsam bands launchd
serves, a contract our libxpc's client side shares -- and the SPI
availability macros. `src/launchd/xpc_launchd.c` is the libxpc SPI only
launchd calls, built into it: `ld2xpc` and `xpc_call_wakeup`.

launchd still calls libxpc SPI that `libsystem_xpc` does not export yet.
Linked on a Mac these bind to the host's libSystem; on LibreDarwin they
must come from here:

    xpc_pipe_try_receive  xpc_pipe_routine_reply
    xpc_dictionary_create_reply  xpc_dictionary_copy_mach_send
    xpc_dictionary_set_mach_recv  xpc_array_set_string
    xpc_array_set_uint64  xpc_copy_entitlement_for_token
    xpc_copy_entitlements_for_pid  xpc_fd_create  _xpc_bool_true

`nm -u build/release/launchd` against `nm -gU
build/release/libsystem_xpc.dylib` shows what is left.

The library links its `/usr/lib/system` siblings directly — the same
`LIBRARY_SEARCH_PATHS = $(SDKROOT)/usr/lib/system` line Apple's
Libsystem.xcconfig uses, resolving the libsystem_info / libsystem_notify /
libsystem_trace re-export stubs in the SDK. The remaining libSystem-family
libraries (log, nv, sbuf) have been merged into libSystem on modern Darwin
and are reached through `-lSystem` when features consume them.

The build uses the public header in `src/libsystem/xpc/include/xpc.h`, the
module map in `src/XPC.framework/Modules`, and the framework metadata in
`src/XPC.framework/Resources`. The framework binary is a thin dylib whose
only load command is an
`LC_REEXPORT_DYLIB` of our `libsystem_xpc.dylib` — the same shape as Apple's
own XPC.framework, which re-exports the libSystem symbol set.

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
