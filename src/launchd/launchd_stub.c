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
 * launchd_stub — a minimal in-repo launchd stand-in.
 *
 * Speaks the legacy pipe routine protocol on a local Mach port: requests
 * arrive with msgh_id XPC_PIPE_ID_ROUTINE (CPX@-serialized dictionaries),
 * replies go back with msgh_id XPC_PIPE_ID_REPLY.  It answers
 * xpc_domain_routine / xpc_service_routine requests the way real launchd
 * does — same reply keys, same error codes — so launchctl's output and
 * exit statuses match launchctl(1) byte for byte (see
 * tools/e2e-launchd.sh, which encodes the captured real-launchd behavior).
 *
 * Real launchd gates legacy routines to platform binaries; this stub exists
 * so launchctl can round-trip end to end in-repo.  The client selects it by
 * setting XNUXPORTS_LAUNCHD_PORT=<port name> (see xpc_routines.c).
 *
 * Usage: launchd_stub [--launchctl PATH] [--env KEY=VALUE]... [--] <args...>
 *
 * The stub allocates a Mach port (receive + send rights under one name),
 * starts a serve thread, sets XNUXPORTS_LAUNCHD_PORT, and runs launchctl
 * in-process (embedded via launchctl.c, see XNUXPORTS_EMBED).  Port rights
 * do not survive fork+exec on modern macOS, so the client and server share
 * one task; the launchctl code path itself is unchanged and the wire
 * traffic (serialize/deserialize, mach_msg, reply audit trailers) crosses
 * the port exactly like a separate-process client would.
 */

#include "xpc.h"
#include "xpc_private.h"

#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Provided by src/launchctl/launchctl.c when compiled with XNUXPORTS_EMBED. */
int launchctl_main(int argc, char **argv);

/* Same trailer option as the client's xpc_pipe.c receive. */
#define XPC_RCV_TRAILER_OPTS \
    (MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) | \
     MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT))

#ifndef XPC_ROUTINE_SETENV
#define XPC_ROUTINE_SETENV 0x333
#endif

/* Version string written into the caller's shared-memory region by the
 * PRINT handler — the stub's stand-in for launchd's build banner. */
#define STUB_VERSION_STRING \
    "Darwin Bootstrapper Version 7.0.0: xnuports-stub launchd"

#pragma mark - canned domain state

struct stub_service {
    const char *label;
    int64_t pid;    /* 0 == not running */
    int64_t status; /* raw wait(3) status when not running */
    bool enabled;
};

/*
 * Deterministic canned state for `launchctl list`.  Statuses are wait(2)
 * encodings: 9 -> "-9" (WTERMSIG), 5 << 8 -> "5" (WEXITSTATUS), 0 -> "0".
 */
static struct stub_service g_services[] = {
    { "com.xnuports.stub.running",  502,   0, true },
    { "com.xnuports.stub.dead",       0,   0, true },
    { "com.xnuports.stub.signaled",   0,   9, true },
    { "com.xnuports.stub.exited",     0,   5 << 8, true },
};
#define SERVICE_COUNT (sizeof(g_services) / sizeof(g_services[0]))

struct stub_env {
    const char *key;
    char *value; /* NULL == unset */
};

static struct stub_env *g_env;
static size_t g_env_count;

static struct stub_service *
find_service(const char *label)
{
    size_t i;
    if (!label) {
        return NULL;
    }
    for (i = 0; i < SERVICE_COUNT; i++) {
        if (strcmp(g_services[i].label, label) == 0) {
            return &g_services[i];
        }
    }
    return NULL;
}

static struct stub_env *
find_env(const char *key)
{
    size_t i;
    for (i = 0; i < g_env_count; i++) {
        if (strcmp(g_env[i].key, key) == 0) {
            return &g_env[i];
        }
    }
    return NULL;
}

static void
set_domain_env(const char *key, const char *value)
{
    struct stub_env *e = find_env(key);
    if (!e) {
        struct stub_env *grown =
            realloc(g_env, (g_env_count + 1) * sizeof(*grown));
        if (!grown) {
            return;
        }
        g_env = grown;
        e = &g_env[g_env_count++];
        e->key = strdup(key);
        e->value = NULL;
        if (!e->key) {
            return;
        }
    }
    free(e->value);
    e->value = value ? strdup(value) : NULL;
}

static const char *
env_value(const char *key)
{
    struct stub_env *e = find_env(key);
    return e ? e->value : NULL;
}

#pragma mark - request handling

static xpc_object_t
make_service_dict(const struct stub_service *s)
{
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(dict, "active count", 1);
    xpc_dictionary_set_string(dict, "path", "/usr/bin/true");
    xpc_dictionary_set_string(dict, "program", "/usr/bin/true");
    xpc_dictionary_set_int64(dict, "pid", s->pid);
    xpc_dictionary_set_int64(dict, "status", s->status);
    return dict;
}

static void
add_error(xpc_object_t reply, const char *key, int64_t code)
{
    xpc_object_t errors = xpc_dictionary_get_value(reply, "errors");
    if (!errors) {
        errors = xpc_dictionary_create(NULL, NULL, 0);
        xpc_dictionary_set_value(reply, "errors", errors);
        xpc_release(errors);
    }
    xpc_dictionary_set_int64(errors, key, code);
}

static void
handle_list(xpc_object_t req, xpc_object_t reply)
{
    const char *name = xpc_dictionary_get_string(req, "name");
    size_t i;

    if (name) {
        struct stub_service *s = find_service(name);
        if (!s) {
            xpc_dictionary_set_int64(reply, "error",
                XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND);
            return;
        }
        xpc_dictionary_set_value(reply, "service", make_service_dict(s));
        return;
    }
    xpc_object_t services = xpc_dictionary_create(NULL, NULL, 0);
    for (i = 0; i < SERVICE_COUNT; i++) {
        xpc_object_t sd = make_service_dict(&g_services[i]);
        xpc_dictionary_set_value(services, g_services[i].label, sd);
        xpc_release(sd);
    }
    xpc_dictionary_set_value(reply, "services", services);
    xpc_release(services);
}

static void
handle_getenv(xpc_object_t req, xpc_object_t reply)
{
    const char *key = xpc_dictionary_get_string(req, "envvar");
    const char *value = env_value(key);
    if (value) {
        xpc_dictionary_set_string(reply, "value", value);
    }
    /* Unset variable: no "value" key, "error" stays 0.  launchctl prints
     * nothing and succeeds — matching real launchd. */
}

static void
handle_setenv(xpc_object_t req, xpc_object_t reply)
{
    xpc_object_t envvars = xpc_dictionary_get_value(req, "envvars");
    (void)reply;
    if (!envvars) {
        return;
    }
    xpc_dictionary_apply(envvars, ^bool(const char *key, xpc_object_t value) {
        const char *string = NULL;
        if (xpc_get_type(value) == &_xpc_type_string) {
            string = xpc_string_get_string_ptr(value);
        }
        set_domain_env(key, string);
        return true;
    });
}

static void
handle_kill(xpc_object_t req, xpc_object_t reply)
{
    uint64_t type = xpc_dictionary_get_uint64(req, "type");
    const char *name = xpc_dictionary_get_string(req, "name");
    struct stub_service *s = find_service(name);

    if (type == 1 /* LAUNCHCTL_DOMAIN_SYSTEM */) {
        /* Non-root callers cannot signal system-domain services. */
        xpc_dictionary_set_int64(reply, "error", EPERM);
        return;
    }
    if (!s) {
        xpc_dictionary_set_int64(reply, "error",
            XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND);
        return;
    }
    if (s->pid == 0) {
        xpc_dictionary_set_int64(reply, "error", ESRCH);
    }
}

static void
handle_enable_disable(xpc_object_t req, xpc_object_t reply, bool enable)
{
    uint64_t type = xpc_dictionary_get_uint64(req, "type");
    xpc_object_t names = xpc_dictionary_get_value(req, "names");
    size_t i, count;

    if (type == 1) {
        xpc_dictionary_set_int64(reply, "error", EPERM);
        return;
    }
    /* User-domain enable/disable always succeeds, even for unknown
     * services (real launchd: silent, exit 0). */
    if (!names) {
        return;
    }
    count = xpc_array_get_count(names);
    for (i = 0; i < count; i++) {
        const char *label = xpc_string_get_string_ptr(
            xpc_array_get_value(names, i));
        struct stub_service *s = find_service(label);
        if (s) {
            s->enabled = enable;
        }
    }
}

/* A plist "parses" for our purposes if it exists, is readable, and starts
 * like one: '<' for XML (the header is optional, so we don't require it) or
 * "bplist" for binary.  Anything else — missing, empty, or garbage — is the
 * EIO failure real launchd reports for unreadable/invalid service files. */
static bool
plist_is_readable(const char *path)
{
    FILE *f = fopen(path, "r");
    char head[6];
    size_t n;

    if (!f) {
        return false;
    }
    n = fread(head, 1, sizeof(head), f);
    fclose(f);
    if (n == 0) {
        return false;                  /* empty file */
    }
    if (head[0] == '<') {
        return true;                   /* XML plist */
    }
    if (n == sizeof(head) && memcmp(head, "bplist", 6) == 0) {
        return true;                   /* binary plist */
    }
    return false;
}

static void
handle_bootstrap(xpc_object_t req, xpc_object_t reply)
{
    xpc_object_t paths = xpc_dictionary_get_value(req, "paths");
    size_t i, count;

    if (!paths) {
        return; /* service-target bootstrap: accept silently */
    }
    count = xpc_array_get_count(paths);
    for (i = 0; i < count; i++) {
        const char *path = xpc_string_get_string_ptr(
            xpc_array_get_value(paths, i));
        const char *slash;
        const char *label;

        if (!path || !plist_is_readable(path)) {
            /* Missing, empty, or invalid plist: launchd reports EIO. */
            xpc_dictionary_set_int64(reply, "bootstrap-error", EIO);
            continue;
        }
        slash = strrchr(path, '/');
        label = slash ? slash + 1 : path;
        if (find_service(label)) {
            add_error(reply, path, EEXIST);
        }
    }
}

static void
handle_bootout(xpc_object_t req, xpc_object_t reply)
{
    uint64_t type = xpc_dictionary_get_uint64(req, "type");
    const char *name = xpc_dictionary_get_string(req, "name");
    xpc_object_t paths = xpc_dictionary_get_value(req, "paths");
    size_t i, count;

    if (type == 1) {
        xpc_dictionary_set_int64(reply, "bootout-error", EPERM);
        return;
    }
    if (name) {
        xpc_dictionary_set_int64(reply, "bootout-error",
            find_service(name) ? 0 : ESRCH);
        return;
    }
    if (!paths) {
        return;
    }
    count = xpc_array_get_count(paths);
    for (i = 0; i < count; i++) {
        const char *path = xpc_string_get_string_ptr(
            xpc_array_get_value(paths, i));
        const char *slash;
        const char *label;

        if (!path) {
            xpc_dictionary_set_int64(reply, "bootout-error", ESRCH);
            continue;
        }
        slash = strrchr(path, '/');
        label = slash ? slash + 1 : path;
        if (!find_service(label)) {
            xpc_dictionary_set_int64(reply, "bootout-error", ESRCH);
        }
    }
}

static void
handle_kickstart(xpc_object_t req, xpc_object_t reply)
{
    const char *name = xpc_dictionary_get_string(req, "name");
    struct stub_service *s = find_service(name);

    if (!s) {
        xpc_dictionary_set_int64(reply, "error",
            XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND);
        return;
    }
    if (s->pid != 0) {
        xpc_dictionary_set_int64(reply, "error", EALREADY);
        xpc_dictionary_set_int64(reply, "pid", s->pid);
    } else {
        xpc_dictionary_set_int64(reply, "pid", 777);
    }
}

static void
handle_print(xpc_object_t req, xpc_object_t reply)
{
    xpc_object_t shmem = xpc_dictionary_get_value(req, "shmem");
    void *region = NULL;
    size_t region_len = 0;
    size_t n;

    if (!shmem || xpc_get_type(shmem) != &_xpc_type_shmem) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[stub] print: no shmem value (obj=%p)\n",
                (void *)shmem);
        }
        xpc_dictionary_set_int64(reply, "error",
            XPC_LAUNCHD_ERROR_BAD_RESPONSE);
        return;
    }
    /* The caller maps a region as a Mach memory entry and passes it as a
     * shmem value; launchd maps that entry and writes its version string
     * back into the region.  This is the wire round-trip probe9 validates:
     * serialize (0xc000 tag + page-aligned size + port descriptor),
     * deserialize-with-port-table, map, write. */
    {
        int map_kr = xpc_shmem_map(shmem, &region, &region_len);
        if (map_kr != KERN_SUCCESS) {
            if (getenv("XPC_DEBUG")) {
                fprintf(stderr, "[stub] print: shmem_map kr=0x%x "
                    "(port=0x%x)\n", map_kr, xpc_shmem_get_port(shmem));
            }
            xpc_dictionary_set_int64(reply, "error",
                XPC_LAUNCHD_ERROR_BAD_RESPONSE);
            return;
        }
    }
    if (getenv("XPC_DEBUG")) {
        fprintf(stderr, "[stub] print: mapped %zu bytes at %p\n",
            region_len, region);
    }
    n = strlen(STUB_VERSION_STRING) + 1;
    if (n > region_len) n = region_len;
    memcpy(region, STUB_VERSION_STRING, n);
    xpc_dictionary_set_uint64(reply, "bytes-written", n);
}

static xpc_object_t
handle_request_with_id(xpc_object_t req, uint32_t msgh_id)
{
    uint64_t subsystem = xpc_dictionary_get_uint64(req, "subsystem");
    uint64_t routine = xpc_dictionary_get_uint64(req, "routine");
    xpc_object_t reply = xpc_dictionary_create(NULL, NULL, 0);

    xpc_dictionary_set_int64(reply, "error", 0);

    /* Route by msgh_id low 16 bits first, like real launchd's wire
     * dispatcher: the PRINT routine (0x33c) carries its request inline
     * with no subsystem/routine keys — it's identified purely by the
     * wire id (0x4000033c). */
    if ((msgh_id & 0xffff) == XPC_ROUTINE_PRINT) {
        handle_print(req, reply);
        return reply;
    }

    if (subsystem == XPC_LAUNCHD_SUBSYSTEM_SERVICE) {
        if (routine == XPC_ROUTINE_SERVICE_KICKSTART) {
            handle_kickstart(req, reply);
        } else {
            xpc_dictionary_set_int64(reply, "error",
                XPC_LAUNCHD_ERROR_REQUEST_UNSUPPORTED);
        }
        return reply;
    }
    if (subsystem != XPC_LAUNCHD_SUBSYSTEM_DOMAIN) {
        xpc_dictionary_set_int64(reply, "error",
            XPC_LAUNCHD_ERROR_REQUEST_UNSUPPORTED);
        return reply;
    }

    switch (routine) {
    case XPC_ROUTINE_LIST:
        handle_list(req, reply);
        break;
    case XPC_ROUTINE_GETENV:
        handle_getenv(req, reply);
        break;
    case XPC_ROUTINE_SETENV:
        handle_setenv(req, reply);
        break;
    case XPC_ROUTINE_KILL:
        handle_kill(req, reply);
        break;
    case XPC_ROUTINE_ENABLE:
        handle_enable_disable(req, reply, true);
        break;
    case XPC_ROUTINE_DISABLE:
        handle_enable_disable(req, reply, false);
        break;
    case XPC_ROUTINE_BOOTSTRAP:
        handle_bootstrap(req, reply);
        break;
    case XPC_ROUTINE_BOOTOUT:
        handle_bootout(req, reply);
        break;
    default:
        xpc_dictionary_set_int64(reply, "error",
            XPC_LAUNCHD_ERROR_REQUEST_UNSUPPORTED);
        break;
    }
    return reply;
}

#pragma mark - local routine bridge

/*
 * Same-task handler registered with the pipe layer.  A Mach reply port's
 * send-once right is not visible to the receiver when client and server
 * share one task (receive clobbers msgh_local_port with the received-on
 * port name), so the stub dispatches the fully serialized request here
 * instead of through mach_msg.  The complete wire round-trip is still
 * exercised: descriptor walk, deserialize with the port table, the
 * routine handler, and reply serialization.
 */
static uint8_t *
local_routine_handler(const uint8_t *msg, size_t msg_len, uint32_t msgh_id,
    size_t *reply_len)
{
    *reply_len = 0;
    if (msg_len < 24) return NULL;

    /* Locate the CPX@ envelope: simple messages carry it right after the
     * mach header (offset 24); complex messages carry a descriptor array
     * first, and any port descriptors become the deserializer's port
     * table for mach-send values (e.g. the routine request's
     * domain-port). */
    const mach_msg_header_t *hdr =
        (const mach_msg_header_t *)(void *)msg;
    const uint8_t *payload = msg + 24;
    size_t payload_len = msg_len - 24;
    mach_port_t ports[32];
    mach_msg_size_t nports = 0;
    if ((hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX) && msg_len >= 28) {
        const uint32_t *desc_count =
            (const uint32_t *)(void *)(msg + 24);
        const mach_msg_descriptor_t *d =
            (const mach_msg_descriptor_t *)(void *)(msg + 28);
        size_t off = 28;
        for (uint32_t di = 0; di < *desc_count && di < 32; di++) {
            size_t dsize = (d->out_of_line.type == MACH_MSG_PORT_DESCRIPTOR)
                ? sizeof(mach_msg_port_descriptor_t)
                : sizeof(mach_msg_descriptor_t);
            if (nports < 32 &&
                d->out_of_line.type == MACH_MSG_PORT_DESCRIPTOR) {
                ports[nports++] = d->port.name;
            }
            d = (const mach_msg_descriptor_t *)(void *)((const uint8_t *)d
                + dsize);
            off += dsize;
        }
        if (off < msg_len) {
            payload = msg + off;
            payload_len = msg_len - off;
        }
    }

    xpc_object_t req = xpc_wire_deserialize_with_ports(payload, payload_len,
        ports, nports);
    if (!req) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[stub] drop: deserialize failed (size=%zu)\n",
                msg_len);
        }
        return NULL;
    }
    xpc_object_t reply = handle_request_with_id(req, msgh_id);
    uint64_t handled_routine = xpc_dictionary_get_uint64(req, "routine");
    xpc_release(req);
    if (!reply) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[stub] drop: no reply object\n");
        }
        return NULL;
    }

    size_t length;
    uint8_t *out = xpc_wire_serialize(reply, XPC_PIPE_ID_REPLY, &length);
    xpc_release(reply);
    if (!out) {
        if (getenv("XPC_DEBUG")) {
            fprintf(stderr, "[stub] drop: serialize failed\n");
        }
        return NULL;
    }
    if (getenv("XPC_DEBUG")) {
        fprintf(stderr, "[stub] served id=0x%x routine=%llu -> %zu bytes\n",
            msgh_id, handled_routine, length);
    }
    *reply_len = length;
    return out;
}

#pragma mark - harness

static void
usage(void)
{
    fprintf(stderr,
        "usage: launchd_stub [--launchctl PATH] [--env KEY=VALUE]... [--] "
        "<launchctl args...>\n");
}

int
main(int argc, char **argv)
{
    const char *launchctl_path = "build/release/launchctl";
    mach_port_t server_port = MACH_PORT_NULL;
    kern_return_t kr;
    int i, rc;

    set_domain_env("XNUXPORTS_TEST_VAR", "hello-from-stub");

    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--launchctl") == 0 && i + 1 < argc) {
            launchctl_path = argv[i + 1];
            i += 2;
        } else if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
            char *kv = argv[i + 1];
            char *eq = strchr(kv, '=');
            if (!eq) {
                usage();
                return 1;
            }
            *eq = '\0';
            set_domain_env(kv, eq + 1);
            i += 2;
        } else if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        } else {
            break;
        }
    }
    if (i >= argc) {
        usage();
        return 1;
    }
    (void)launchctl_path; /* launchctl is embedded; path accepted for compat */

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
        &server_port);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "mach_port_allocate: %d\n", kr);
        return 1;
    }
    if (getenv("XPC_DEBUG")) {
        fprintf(stderr, "[stub] server_port=%u\n", server_port);
    }
    /* A send right under the same name, for this task's own client. */
    kr = mach_port_insert_right(mach_task_self(), server_port, server_port,
        MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "mach_port_insert_right: %d\n", kr);
        return 1;
    }

    {
        char portbuf[32];
        snprintf(portbuf, sizeof(portbuf), "%u", server_port);
        setenv("XNUXPORTS_LAUNCHD_PORT", portbuf, 1);
    }

    /* Same-task bridge: a reply port's send-once right is invisible to a
     * receiver in the same task (receive clobbers msgh_local_port with the
     * received-on port name), and fork() does not clone the Mach port
     * namespace on this macOS, so the pipe layer dispatches the serialized
     * request straight to local_routine_handler instead.  The client walks
     * the identical serialize path either way. */
    xpc_pipe_set_local_handler(local_routine_handler);

    /* Run the launchctl command in-process against the local handler. */
    char **launchctl_argv;
    int launchctl_argc = argc - i + 1;

    launchctl_argv = calloc((size_t)launchctl_argc, sizeof(*launchctl_argv));
    if (!launchctl_argv) {
        return 1;
    }
    launchctl_argv[0] = (char *)"launchctl";
    for (int j = 0; j < argc - i; j++) {
        launchctl_argv[j + 1] = argv[i + j];
    }
    rc = launchctl_main(launchctl_argc, launchctl_argv);
    free(launchctl_argv);

    mach_port_mod_refs(mach_task_self(), server_port, MACH_PORT_RIGHT_RECEIVE,
        -1);
    mach_port_destruct(mach_task_self(), server_port, 0, 0);

    return rc;
}