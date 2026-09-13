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
 * launchctl — launchd domain/service control, implemented on xnuports libxpc.
 *
 * Talks to the real launchd over the bootstrap pipe using the private
 * xpc_domain_routine / xpc_service_routine protocol (docs/LAUNCHCTL.md).
 * Target syntax and command semantics mirror Apple's launchctl(1).
 */

#include "xpc.h"
#include "xpc_private.h"

#include <errno.h>
#include <getopt.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>

#define LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED 10
#define LAUNCHCTL_STATUS_UNKNOWN_COMMAND 11

/* Domain type values sent in the "type" key. */
enum launchctl_domain_type {
    LAUNCHCTL_DOMAIN_SYSTEM = 1,
    LAUNCHCTL_DOMAIN_USER = 2,
    LAUNCHCTL_DOMAIN_LOGIN = 3,
    LAUNCHCTL_DOMAIN_PID = 5,
    LAUNCHCTL_DOMAIN_SELF = 7, /* legacy default: the caller's own domain */
    LAUNCHCTL_DOMAIN_GUI = 8,
};

#define LAUNCHCTL_DOMAIN_HANDLE_LOGINWINDOW ((uint64_t)(uid_t)-1)

struct command {
    const char *name;
    const char *description;
    const char *usage;
    int (*handler)(int argc, char **argv);
};

static int help_cmd(int argc, char **argv);
static int version_cmd(int argc, char **argv);
static int print_cmd(int argc, char **argv);
static int bootstrap_cmd(int argc, char **argv);
static int service_target_required_error(const char *cmd);
static int bootout_cmd(int argc, char **argv);
static int enable_disable_cmd(int argc, char **argv);
static int kickstart_cmd(int argc, char **argv);
static int kill_cmd(int argc, char **argv);
static int blame_cmd(int argc, char **argv);
static int list_cmd(int argc, char **argv);
static int getenv_cmd(int argc, char **argv);
static int setenv_cmd(int argc, char **argv);

static const struct command commands[] = {
    { "help", "Print this help.", "", help_cmd },
    { "version", "Print the version.", "", version_cmd },
    { "print", "Print the state of a domain.", "<domain-target>",
      print_cmd },
    { "bootstrap", "Bootstraps a domain or a service into a domain.",
      "<domain-target> [service-path ...]", bootstrap_cmd },
    { "bootout", "Tears down a domain or removes a service.",
      "<domain-target> [service-path ...] | <service-target>",
      bootout_cmd },
    { "enable", "Enables an existing service.", "<service-target>",
      enable_disable_cmd },
    { "disable", "Disables an existing service.", "<service-target>",
      enable_disable_cmd },
    { "kickstart", "Forces an existing service to start.",
      "[-k] [-p] [-s] <service-target>", kickstart_cmd },
    { "kill", "Sends a signal to the service instance.",
      "<signal-number|signal-name> <service-target>", kill_cmd },
    { "blame", "Describes what is preventing a service from running.",
      "<service-target>", blame_cmd },
    { "list", "Lists information about services.", "[service-name]",
      list_cmd },
    { "getenv", "Gets an environment variable from within launchd.",
      "<key>", getenv_cmd },
    { "setenv", "Set or clear an environment variable in launchd.",
      "<key> [value]", setenv_cmd },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static char *
xasprintf(const char *fmt, ...)
{
    va_list ap;
    char *out = NULL;
    va_start(ap, fmt);
    if (vasprintf(&out, fmt, ap) < 0) {
        out = NULL;
    }
    va_end(ap);
    return out;
}

#define REQUIRE_ARGS(n) \
    do { if (argc < (n)) { return 'u'; } } while (0)

#pragma mark - target parsing

/*
 * Parse "<domain>[/<handle>][/<service>]" into "type"/"handle"/"name"
 * request keys.  "user" with no handle defaults to the current uid.
 */
static int
parse_service_target(const char *target, xpc_object_t request,
    char **service_name)
{
    char *target_copy;
    char *parts[4] = { NULL, NULL, NULL, NULL };
    char *saveptr = NULL;
    char *token;
    char *endptr;
    enum launchctl_domain_type type = LAUNCHCTL_DOMAIN_SYSTEM;
    uint64_t handle = 0;
    char *service = NULL;
    size_t index = 0;
    int error = 0;

    if (service_name) {
        *service_name = NULL;
    }
    target_copy = strdup(target ? target : "");
    if (!target_copy) {
        return ENOMEM;
    }
    token = strtok_r(target_copy, "/", &saveptr);
    while (token && index < 4) {
        parts[index++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }
    if (!index) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
        goto out;
    }

    if (strcmp(parts[0], "system") == 0) {
        type = LAUNCHCTL_DOMAIN_SYSTEM;
        handle = 0;
        index = 1;
        goto success;
    }
    if (strcmp(parts[0], "user") == 0) {
        type = LAUNCHCTL_DOMAIN_USER;
        if (!parts[1]) {
            handle = (uint64_t)getuid();
            index = 1;
            goto success;
        }
        goto parse_handle;
    }
    if (strcmp(parts[0], "login") == 0) {
        type = LAUNCHCTL_DOMAIN_LOGIN;
        goto parse_handle;
    }
    if (strcmp(parts[0], "gui") == 0) {
        type = LAUNCHCTL_DOMAIN_GUI;
        goto parse_handle;
    }
    if (strcmp(parts[0], "pid") == 0) {
        type = LAUNCHCTL_DOMAIN_PID;
        goto parse_handle;
    }
    if (strcmp(parts[0], "loginwindow") == 0) {
        type = LAUNCHCTL_DOMAIN_USER;
        handle = LAUNCHCTL_DOMAIN_HANDLE_LOGINWINDOW;
        index = 1;
        goto success;
    }
    error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    goto out;

parse_handle:
    if (!parts[1]) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
        goto out;
    }
    endptr = NULL;
    handle = (uint64_t)strtoul(parts[1], &endptr, 10);
    if (endptr == parts[1] || *endptr) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
        goto out;
    }
    index = 2;

success:
    if (parts[index] && *parts[index]) {
        service = strdup(parts[index]);
        if (!service) {
            error = ENOMEM;
            goto out;
        }
        xpc_dictionary_set_string(request, "name", service);
    }
    xpc_dictionary_set_uint64(request, "type", (uint64_t)type);
    xpc_dictionary_set_uint64(request, "handle", handle);
    if (service_name) {
        *service_name = service; /* ownership transferred */
        service = NULL;
    }

out:
    free(service);
    free(target_copy);
    return error;
}

static void
set_legacy_domain_request(xpc_object_t request)
{
    /*
     * Legacy list/getenv resolve in the caller's own ("port") domain:
     * type 7 / handle 0.  Modern launchctl reports misses against this
     * domain as "Could not find service \"%s\" in domain for port".
     *
     * Non-root callers must also attach a "domain-port" mach-send
     * carrying their task bootstrap port so launchd can resolve the
     * caller's port namespace (verified against a live Apple launchd
     * trace and Procursus launchctl source).
     */
    if (geteuid() != 0) {
        mach_port_t bootstrap = MACH_PORT_NULL;
        task_get_bootstrap_port(mach_task_self(), &bootstrap);
        xpc_dictionary_set_uint64(request, "type", LAUNCHCTL_DOMAIN_SELF);
        xpc_dictionary_set_uint64(request, "handle", 0);
        xpc_dictionary_set_mach_send(request, "domain-port", bootstrap);
    } else {
        xpc_dictionary_set_uint64(request, "type", LAUNCHCTL_DOMAIN_SYSTEM);
        xpc_dictionary_set_uint64(request, "handle", 0);
    }
    /* Legacy routine requests must declare legacy mode or launchd
     * resolves them as domain routines and returns EINTR for named
     * lookups (verified against a live launchd trace of launchctl). */
    xpc_dictionary_set_bool(request, "legacy", true);
}

/*
 * launchd reports a missing service with XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND;
 * launchctl renders it with the domain's human name:
 *   system -> "system", gui/501 -> "user gui: 501", self -> "port".
 */
static void
print_service_not_found(const char *service, xpc_object_t request)
{
    uint64_t type = xpc_dictionary_get_uint64(request, "type");
    uint64_t handle = xpc_dictionary_get_uint64(request, "handle");

    switch (type) {
    case LAUNCHCTL_DOMAIN_SYSTEM:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for system\n", service);
        break;
    case LAUNCHCTL_DOMAIN_USER:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for uid: %llu\n",
            service, (unsigned long long)handle);
        break;
    case LAUNCHCTL_DOMAIN_LOGIN:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for login: %llu\n",
            service, (unsigned long long)handle);
        break;
    case LAUNCHCTL_DOMAIN_PID:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for pid: %llu\n",
            service, (unsigned long long)handle);
        break;
    case LAUNCHCTL_DOMAIN_GUI:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for user gui: %llu\n",
            service, (unsigned long long)handle);
        break;
    default:
        fprintf(stderr,
            "Could not find service \"%s\" in domain for port\n", service);
        break;
    }
}

static xpc_object_t
create_bootstrap_paths_array(int path_count, char *const paths[])
{
    xpc_object_t array = xpc_array_create(NULL, 0);
    char cwd[MAXPATHLEN];
    char *full_path;
    int i;

    if (!array) {
        return NULL;
    }
    if (!getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '\0';
    }
    for (i = 0; i < path_count; i++) {
        if (paths[i][0] == '/') {
            full_path = strdup(paths[i]);
        } else {
            full_path = xasprintf("%s/%s", cwd, paths[i]);
        }
        if (!full_path) {
            xpc_release(array);
            return NULL;
        }
        xpc_object_t str = xpc_string_create(full_path);
        xpc_array_append_value(array, str);
        xpc_release(str);
        free(full_path);
    }
    return array;
}

static void
print_service_errors(xpc_object_t response, const char *action)
{
    xpc_object_t errors = xpc_dictionary_get_value(response, "errors");
    if (!errors) {
        return;
    }
    xpc_dictionary_apply(errors, ^bool(const char *service_name,
            xpc_object_t error_value) {
        int64_t error = xpc_int64_get_value(error_value);
        if (error) {
            if (error == EALREADY || error == EEXIST) {
                fprintf(stderr, "%s: service already %s\n", service_name,
                    action);
            } else {
                fprintf(stderr, "%s: %s\n", service_name,
                    xpc_strerror((int)error));
            }
        }
        return true;
    });
}

#pragma mark - commands

static int
help_cmd(int argc, char **argv)
{
    size_t i;
    (void)argc;
    (void)argv;

    fprintf(stdout,
        "Usage: launchctl <command> [options] [arguments]\n\nCommands:\n");
    for (i = 0; i < COMMAND_COUNT; i++) {
        fprintf(stdout, "  %-10s %s\n", commands[i].name,
            commands[i].description);
    }
    fprintf(stdout,
        "\nTargets: system | user[/<uid>] | gui/<uid> | login/<uid> | "
        "pid/<pid> | loginwindow, optionally followed by a service name.\n");
    return 0;
}

static int
version_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct xpc_global_data *state;
    kern_return_t kr;
    xpc_object_t request = NULL;
    xpc_object_t reply = NULL;
    xpc_object_t shmem = NULL;
    vm_address_t region = 0;
    vm_size_t region_len = 0x1000;
    const char *version;
    int rc = 1;

    /* Version banner comes from launchd, written into a shared-memory
     * region mapped as a Mach memory entry (the PRINT routine's reply
     * channel, wire kind 0xc000).  Matches Apple's launchctl.  Goes
     * through the global bootstrap pipe so the launchd_stub harness
     * (XNUXPORTS_LAUNCHD_PORT) serves it too. */
    state = xpc_global_data();
    if (!state->xpc_bootstrap_pipe) {
        fprintf(stdout,
            "launchctl (xnuports libxpc) — Darwin service management\n");
        return 0;
    }
    kr = vm_allocate(mach_task_self(), &region, region_len, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        return 1;
    }
    memset((void *)region, 0, region_len);

    shmem = xpc_shmem_create((void *)region, region_len);
    if (!shmem) {
        goto out;
    }
    request = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(request, "handle", 0);
    xpc_dictionary_set_value(request, "shmem", shmem);
    xpc_dictionary_set_uint64(request, "type", 1);
    xpc_dictionary_set_bool(request, "version", true);

    if (xpc_pipe_routine(state->xpc_bootstrap_pipe, request, &reply,
            XPC_ROUTINE_PRINT) != 0) {
        goto out;
    }
    version = (const char *)region;
    if (!version[0]) {
        goto out;
    }
    fprintf(stdout, "%s\n", version);
    rc = 0;
out:
    if (reply) xpc_release(reply);
    if (request) xpc_release(request);
    if (shmem) xpc_release(shmem);
    if (region) vm_deallocate(mach_task_self(), region, region_len);
    return rc;
}

/*
 * print: dump a domain's (or service's) runtime state.  Same PRINT
 * transport and reply channel as version (launchd serializes the state
 * as text into the caller's shmem region), but target-driven: the
 * request carries the domain's type/handle, or a service's
 * type/handle/name.  Domain targets go through the raw PRINT routine
 * (0x33c); service targets through the SERVICE_PRINT routine (0x2c4)
 * via the service subsystem, whose wrapper surfaces payload errors.
 * The region is a 256 KB granule: real launchd fills the whole region
 * without a trailing NUL when a domain's state exceeds the span, so the
 * dump is emitted with an explicit byte bound (reply "bytes-written"
 * when present, else strnlen), never as a raw %s.  The stub writes
 * deterministic canned dumps in the same shape.
 */
static int
print_cmd(int argc, char **argv)
{
    struct xpc_global_data *state;
    kern_return_t kr;
    xpc_object_t request = NULL;
    xpc_object_t reply = NULL;
    xpc_object_t shmem = NULL;
    vm_address_t region = 0;
    vm_size_t region_len = 0x40000;
    char *service_name = NULL;
    const char *state_text;
    int error;

    REQUIRE_ARGS(2);
    state = xpc_global_data();
    if (!state->xpc_bootstrap_pipe) {
        fprintf(stderr, "Print failed: no bootstrap pipe\n");
        return 1;
    }
    request = xpc_dictionary_create(NULL, NULL, 0);
    error = parse_service_target(argv[1], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("print");
    }
    kr = vm_allocate(mach_task_self(), &region, region_len, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        error = kr;
        goto out;
    }
    memset((void *)region, 0, region_len);

    shmem = xpc_shmem_create((void *)region, region_len);
    if (!shmem) {
        error = ENOMEM;
        goto out;
    }
    xpc_dictionary_set_value(request, "shmem", shmem);

    if (!error && service_name) {
        /* Per-service state dump: SERVICE_PRINT via the service
         * subsystem.  xpc_service_routine sets subsystem/routine and
         * surfaces payload errors as its return value. */
        error = xpc_service_routine(XPC_ROUTINE_SERVICE_PRINT, request,
            &reply);
        if (error) {
            fprintf(stderr, "Print failed: %d: %s\n", error,
                xpc_strerror(error));
            goto out;
        }
    } else {
        error = xpc_pipe_routine(state->xpc_bootstrap_pipe, request, &reply,
            XPC_ROUTINE_PRINT);
        if (error) {
            fprintf(stderr, "Print failed: %d: %s\n", error, xpc_strerror(error));
            goto out;
        }
        /* launchd reports domain-print failures in the reply payload
         * ("error") while the transport itself succeeds — surface those. */
        {
            int64_t rerr = reply ? xpc_dictionary_get_int64(reply, "error") : 0;
            if (rerr != 0) {
                fprintf(stderr, "Print failed: %lld: %s\n", (long long)rerr,
                    xpc_strerror((int)rerr));
                error = (int)rerr;
                goto out;
            }
        }
    }
    /*
     * The dump is written into our region.  Real launchd can fill the
     * whole region without a trailing NUL when the state exceeds the
     * span, so never emit the region as a raw %s — it walks off the end
     * of the mapping.  Prefer the reply's "bytes-written" when present;
     * otherwise bound the read with strnlen.  A dump that hits the
     * bound is truncated.
     */
    state_text = (const char *)region;
    if (!state_text[0]) {
        error = XPC_LAUNCHD_ERROR_BAD_RESPONSE;
        goto out;
    }
    {
        size_t avail = region_len - 1;
        size_t n = 0;
        if (reply) {
            uint64_t bw = xpc_dictionary_get_uint64(reply, "bytes-written");
            if (bw && bw < (uint64_t)avail) {
                n = (size_t)bw;
            }
        }
        if (!n) {
            n = strnlen(state_text, avail);
        }
        fwrite(state_text, 1, n, stdout);
        fputc('\n', stdout);
    }
    error = 0;
out:
    if (reply) xpc_release(reply);
    if (request) xpc_release(request);
    if (shmem) xpc_release(shmem);
    if (region) vm_deallocate(mach_task_self(), region, region_len);
    free(service_name);
    return error;
}

static int
bootstrap_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t paths = NULL;
    xpc_object_t reply = NULL;
    char *service_name = NULL;
    int error;

    REQUIRE_ARGS(2);
    request = xpc_dictionary_create(NULL, NULL, 0);
    if (strcmp(argv[1], "--angel") == 0) {
        xpc_dictionary_set_bool(request, "angel", true);
        argv++;
        argc--;
        REQUIRE_ARGS(2);
    }
    error = parse_service_target(argv[1], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("bootstrap");
    }
    if (!error && service_name) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    }
    if (!error && argc > 2) {
        paths = create_bootstrap_paths_array(argc - 2, argv + 2);
        if (!paths) {
            error = ENOMEM;
        } else {
            xpc_dictionary_set_value(request, "paths", paths);
        }
    }
    if (!error) {
        error = xpc_domain_routine(XPC_ROUTINE_BOOTSTRAP, request, &reply);
        if (!error) {
            error = (int)xpc_dictionary_get_int64(reply, "bootstrap-error");
            print_service_errors(reply, "bootstrapped");
        }
        if (error && error != XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND) {
            fprintf(stderr, "Bootstrap failed: %d: %s\n", error,
                xpc_strerror(error));
            if (error == EIO) {
                fputs("Try re-running the command as root for richer errors.\n",
                    stderr);
            }
        }
    }
    if (reply) xpc_release(reply);
    if (paths) xpc_release(paths);
    free(service_name);
    xpc_release(request);
    return error;
}

static int
bootout_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t paths = NULL;
    xpc_object_t reply = NULL;
    char *service_name = NULL;
    int error;

    REQUIRE_ARGS(2);
    request = xpc_dictionary_create(NULL, NULL, 0);
    error = parse_service_target(argv[1], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("bootout");
    }
    if (!error && !service_name && argc > 2) {
        paths = create_bootstrap_paths_array(argc - 2, argv + 2);
        if (!paths) {
            error = ENOMEM;
        } else {
            xpc_dictionary_set_value(request, "paths", paths);
        }
    }
    if (!error) {
        xpc_dictionary_set_bool(request, "no-einprogress", true);
        error = xpc_domain_routine(XPC_ROUTINE_BOOTOUT, request, &reply);
        if (!error) {
            error = (int)xpc_dictionary_get_int64(reply, "bootout-error");
            print_service_errors(reply, "booted out");
        }
        if (error && error != XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND) {
            fprintf(stderr, "Boot-out failed: %d: %s\n", error,
                xpc_strerror(error));
        }
    }
    if (reply) xpc_release(reply);
    if (paths) xpc_release(paths);
    free(service_name);
    xpc_release(request);
    return error;
}

static int
enable_disable_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t names;
    xpc_object_t reply = NULL;
    char *service_name = NULL;
    bool is_disable = strcmp(argv[0], "enable") != 0;
    uint64_t routine = is_disable ? XPC_ROUTINE_DISABLE : XPC_ROUTINE_ENABLE;
    int error;

    REQUIRE_ARGS(2);
    request = xpc_dictionary_create(NULL, NULL, 0);
    error = parse_service_target(argv[1], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error(argv[0]);
    }
    if (!error && !service_name) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    }
    if (!error) {
        names = xpc_array_create(NULL, 0);
        xpc_dictionary_set_value(request, "names", names);
        xpc_object_t name = xpc_string_create(service_name);
        xpc_array_append_value(names, name);
        xpc_release(name);
        error = xpc_domain_routine(routine, request, &reply);
        if (error != XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND && error) {
            fprintf(stderr, "Could not %s service: %d: %s\n", argv[0],
                error, xpc_strerror(error));
        }
        xpc_release(names);
    }
    if (reply) xpc_release(reply);
    free(service_name);
    xpc_release(request);
    return error;
}

static int
kickstart_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t reply = NULL;
    char *service_name = NULL;
    bool print_pid = false;
    int error;
    int opt;
    int64_t pid;

    if (argc < 2) {
        return 'u';
    }
    request = xpc_dictionary_create(NULL, NULL, 0);
    while ((opt = getopt(argc, argv, "pks")) != -1) {
        if (opt == 'k') {
            xpc_dictionary_set_bool(request, "kill", true);
        } else if (opt == 'p') {
            print_pid = true;
        } else if (opt == 's') {
            xpc_dictionary_set_bool(request, "suspended", true);
        } else {
            xpc_release(request);
            return 'u';
        }
    }
    if (optind >= argc) {
        xpc_release(request);
        return 'u';
    }
    error = parse_service_target(argv[optind], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("kickstart");
    }
    if (!error && !service_name) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    }
    if (!error) {
        xpc_dictionary_set_bool(request, "unthrottle", true);
        error = xpc_service_routine(XPC_ROUTINE_SERVICE_KICKSTART, request,
            &reply);
        switch (error) {
        case 0:
        case EALREADY:
            error = 0;
            if (print_pid) {
                pid = xpc_dictionary_get_int64(reply, "pid");
                if (pid > 0) {
                    fprintf(stdout, "%lld\n", (long long)pid);
                } else {
                    error = XPC_LAUNCHD_ERROR_BAD_RESPONSE;
                }
            }
            break;
        default:
            if (error == XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
                print_service_not_found(service_name, request);
            } else {
                fprintf(stderr, "Kickstart failed: %d: %s\n", error,
                    xpc_strerror(error));
            }
            break;
        }
    }
    if (reply) xpc_release(reply);
    free(service_name);
    xpc_release(request);
    return error;
}

static int
kill_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t reply = NULL;
    const char *signal_name;
    char *service_name = NULL;
    int signal_number;
    int i;
    int error;

    REQUIRE_ARGS(3);
    signal_name = argv[1];
    if (*signal_name == '-') {
        signal_name++;
    }
    if (strlen(signal_name) >= 4 && strncasecmp(signal_name, "sig", 3) == 0) {
        signal_name += 3;
    }
    signal_number = (int)strtol(signal_name, NULL, 10);
    if (signal_number == 0) {
        for (i = 1; i < NSIG; i++) {
            if (strcasecmp(sys_signame[i], signal_name) == 0) {
                signal_number = i;
                break;
            }
        }
        if (signal_number == 0) {
            fprintf(stderr, "Unrecognized signal: %s\n", argv[1]);
            return LAUNCHCTL_STATUS_UNKNOWN_COMMAND;
        }
    }

    request = xpc_dictionary_create(NULL, NULL, 0);
    error = parse_service_target(argv[2], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("kill");
    }
    if (!error && !service_name) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    }
    if (!error) {
        xpc_dictionary_set_int64(request, "signal", signal_number);
        xpc_dictionary_set_string(request, "name", service_name);
        error = xpc_domain_routine(XPC_ROUTINE_KILL, request, &reply);
        if (error == EPERM) {
            fputs("Not privileged to signal service.\n", stderr);
        } else if (error == ESRCH) {
            fputs("No process to signal.\n", stderr);
        } else if (error == XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
            print_service_not_found(service_name, request);
        } else if (error) {
            fprintf(stderr, "Kill failed: %d: %s\n", error,
                xpc_strerror(error));
        }
    }
    if (reply) xpc_release(reply);
    free(service_name);
    xpc_release(request);
    return error;
}

/*
 * blame: report why a service is prevented from running (or is
 * crashing).  SERVICE_BLAME (0x2c3) via the service subsystem; launchd
 * replies with the dominant blame code for the service's current state
 * and a human-readable description of the reason.
 */
static int
blame_cmd(int argc, char **argv)
{
    xpc_object_t request = NULL;
    xpc_object_t reply = NULL;
    char *service_name = NULL;
    const char *blame_text;
    int64_t blame = 0;
    int error;

    REQUIRE_ARGS(2);
    request = xpc_dictionary_create(NULL, NULL, 0);
    error = parse_service_target(argv[1], request, &service_name);
    if (error == LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED) {
        xpc_release(request);
        return service_target_required_error("blame");
    }
    if (!error && !service_name) {
        error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
    }
    if (!error) {
        error = xpc_service_routine(XPC_ROUTINE_SERVICE_BLAME, request,
            &reply);
        if (error == XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
            print_service_not_found(service_name, request);
        } else if (error) {
            fprintf(stderr, "Blame failed: %d: %s\n", error,
                xpc_strerror(error));
        } else {
            blame = xpc_dictionary_get_int64(reply, "blame");
            blame_text = xpc_dictionary_get_string(reply, "blame-text");
            if (blame_text) {
                fprintf(stdout, "blame for service %s = %lld (%s)\n",
                    service_name, (long long)blame, blame_text);
            } else {
                fprintf(stdout, "blame for service %s = %lld\n",
                    service_name, (long long)blame);
            }
        }
    }
    if (reply) xpc_release(reply);
    free(service_name);
    xpc_release(request);
    return error;
}

static int
list_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t reply = NULL;
    xpc_object_t value = NULL;
    char *desc;
    int error;

    request = xpc_dictionary_create(NULL, NULL, 0);
    set_legacy_domain_request(request);
    if (argc >= 2) {
        xpc_dictionary_set_string(request, "name", argv[1]);
        error = xpc_domain_routine(XPC_ROUTINE_LIST, request, &reply);
        if (!error) {
            value = xpc_dictionary_get_value(reply, "service");
            if (value) {
                desc = xpc_copy_description(value);
                fprintf(stdout, "%s\n", desc);
                free(desc);
            } else {
                error = XPC_LAUNCHD_ERROR_BAD_RESPONSE;
            }
        } else if (error == XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
            print_service_not_found(argv[1], request);
        }
    } else {
        error = xpc_domain_routine(XPC_ROUTINE_LIST, request, &reply);
        if (!error) {
            value = xpc_dictionary_get_value(reply, "services");
            if (value) {
                fputs("PID\tStatus\tLabel\n", stdout);
                xpc_dictionary_apply(value, ^bool(const char *key,
                        xpc_object_t val) {
                    int64_t pid = xpc_dictionary_get_int64(val, "pid");
                    int64_t status = xpc_dictionary_get_int64(val, "status");
                    if (pid == 0) {
                        fputs("-\t", stdout);
                    } else {
                        fprintf(stdout, "%lld\t", (long long)pid);
                    }
                    if (WIFSTOPPED(status)) {
                        fputs("???\t", stdout);
                    } else if (WIFEXITED(status)) {
                        fprintf(stdout, "%d\t", WEXITSTATUS(status));
                    } else if (WIFSIGNALED(status)) {
                        fprintf(stdout, "-%d\t", WTERMSIG(status));
                    } else {
                        fputs("0\t", stdout);
                    }
                    fputs(key, stdout);
                    fputc('\n', stdout);
                    return true;
                });
            } else {
                error = XPC_LAUNCHD_ERROR_BAD_RESPONSE;
            }
        }
    }
    if (error && error != XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
        fprintf(stderr, "List failed: %d: %s\n", error, xpc_strerror(error));
    }
    if (reply) xpc_release(reply);
    xpc_release(request);
    return error;
}

/*
 * launchctl(1) rejects targets that are not "<domain>/[<handle>/]<service>"
 * with this exact message and EX_USAGE.
 */
static int
service_target_required_error(const char *cmd)
{
    fprintf(stderr,
        "Unrecognized target specifier.\n"
        "Usage: launchctl %s <service-target>\n"
        "<service-target> takes a form of <domain-target>/<service-id>.\n"
        "Please refer to `man launchctl` for explanation of the "
        "<domain-target> specifiers.\n", cmd);
    return 64; /* EX_USAGE */
}

static int
getenv_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t reply = NULL;
    const char *value;
    int error;

    REQUIRE_ARGS(2);
    request = xpc_dictionary_create(NULL, NULL, 0);
    set_legacy_domain_request(request);
    xpc_dictionary_set_string(request, "envvar", argv[1]);
    error = xpc_domain_routine(XPC_ROUTINE_GETENV, request, &reply);
    if (!error) {
        value = xpc_dictionary_get_string(reply, "value");
        if (value) {
            fprintf(stdout, "%s\n", value);
        }
        /* No "value" key == the variable is unset in the domain:
         * print nothing and succeed, exactly like launchctl(1). */
    } else {
        /* Legacy getenv is silent on routine errors. */
        error = 0;
    }
    if (reply) xpc_release(reply);
    xpc_release(request);
    return error;
}

/*
 * setenv: set (<key> <value>) or clear (<key>) an environment variable in
 * launchd's domain.  Wire form is the legacy domain request carrying
 * "envvars", a dict of key -> string (set) or null (clear); launchd's
 * SETENV handler treats a null value exactly as an unset (verified against
 * the live wire trace and the stub).  One or two arguments only — the
 * bare-key form is an idempotent unset, and launchctl(1) exits 0 either
 * way.
 */
static int
setenv_cmd(int argc, char **argv)
{
    xpc_object_t request;
    xpc_object_t envvars;
    xpc_object_t value = NULL;
    xpc_object_t reply = NULL;
    int error;

    if (argc < 2 || argc > 3) {
        return 'u';
    }
    request = xpc_dictionary_create(NULL, NULL, 0);
    set_legacy_domain_request(request);

    envvars = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_value(request, "envvars", envvars);
    if (argc == 3) {
        value = xpc_string_create(argv[2]);
    } else {
        /* Bare-key form clears the variable: null on the wire, which
         * launchd's SETENV handler treats as an unset. */
        value = xpc_null_create();
    }
    xpc_dictionary_set_value(envvars, argv[1], value);
    xpc_release(value);
    xpc_release(envvars);

    error = xpc_domain_routine(XPC_ROUTINE_SETENV, request, &reply);
    if (error && error != XPC_LAUNCHD_ERROR_DOMAIN_NOT_FOUND) {
        fprintf(stderr, "Setenv failed: %d: %s\n", error,
            xpc_strerror(error));
    }
    if (reply) xpc_release(reply);
    xpc_release(request);
    return error;
}

#pragma mark - dispatch

static const struct command *
find_command(const char *name)
{
    size_t i;
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

int
launchctl_main(int argc, char **argv)
{
    const struct command *cmd;
    int error;

    if (argc < 2) {
        return help_cmd(0, NULL) == 0 ? 1 : 1;
    }
    cmd = find_command(argv[1]);
    if (!cmd) {
        fprintf(stderr, "Unrecognized command: %s\n", argv[1]);
        return LAUNCHCTL_STATUS_UNKNOWN_COMMAND;
    }
    error = cmd->handler(argc - 1, argv + 1);
    if (error == 'u') {
        fprintf(stderr, "Usage: launchctl %s %s\n", cmd->name, cmd->usage);
        return 64; /* EX_USAGE — launchctl(1) exits 64 on usage errors. */
    }
    return error; /* launchd error codes pass through as the exit status. */
}

#ifndef XNUXPORTS_EMBED
int
main(int argc, char **argv)
{
    return launchctl_main(argc, argv);
}
#endif