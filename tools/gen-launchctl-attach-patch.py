#!/usr/bin/env python3
"""Generate mk/patches/launchd/0002-*.patch by line surgery on the
post-0001 copy of apple-oss launchctl.c: port parse_service_target +
attach_cmd after the status_cmd that 0001 grafted in."""
import re
import sys

src = sys.argv[1]   # copy of the launchd tree with 0001 already applied
mod = src + ".new"

with open(src, "r") as f:
    text = f.read()
lines = text.splitlines(keepends=True)

assert "static int status_cmd(int argc, char *const argv[]);" in text, \
    "0001 status prototype missing"
assert '{ "status",' in text, "0001 status cmds[] row missing"

out = []

# --- Hunk 1: attach prototype after status prototype ---------------------
for line in lines:
    out.append(line)
    if line.rstrip("\n") == "static int status_cmd(int argc, char *const argv[]);":
        out.append("static int attach_cmd(int argc, char *const argv[]);\n")

lines = out
out = []

# --- Hunk 2: cmds[] row after the status row ------------------------------
STATUS_ROW = re.compile(r'^(\t*)\{ "status",(\t+)status_cmd,(\t+)"[^"]*"\s*\},?$')

new_row = None
for i, line in enumerate(lines):
    if new_row is None:
        m = STATUS_ROW.match(line)
        if m:
            lead, sep1, sep2 = m.group(1), m.group(2), m.group(3)
            new_row = (lead + '{ "attach",' + sep1 + 'attach_cmd,' + sep2 +
                       '"Attach to a service\'s process for debugging" },\n')
    out.append(line)
    if new_row is not None and STATUS_ROW.match(line):
        out.append(new_row)
        new_row = None

assert new_row is None, "status cmds[] row not found"

lines = out
out = []

# --- Hunk 3: parse_service_target + attach_cmd before list_cmd ------------
BLOCK = '''#define LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED 10

/* Modern "type" domain values (xpc_domain_type), per WIRE_FORMAT.md. */
#define XN_XPC_DOMAIN_SYSTEM		1
#define XN_XPC_DOMAIN_USER		2
#define XN_XPC_DOMAIN_LOGIN		3
#define XN_XPC_DOMAIN_PID		5
#define XN_XPC_DOMAIN_SELF		7
#define XN_XPC_DOMAIN_GUI		8
#define XN_XPC_DOMAIN_HANDLE_LOGINWINDOW	((uint64_t)(uid_t)-1)

/*
 * Parse "<domain>[/<handle>][/<service>]" (system, user/<uid>, login,
 * gui/<uid>, pid/<pid>) into the "type"/"handle"/"name" keys the modern
 * service routines expect.  Mirrors the parser in Apple's current
 * launchctl, which this file otherwise predates.
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
	uint64_t type = XN_XPC_DOMAIN_SYSTEM;
	uint64_t handle = 0;
	char *service = NULL;
	size_t index = 0;
	int error = 0;

	if (service_name) {
		*service_name = NULL;
	}
	target_copy = strdup(target ? target : "");
	if (target_copy == NULL) {
		return ENOMEM;
	}
	token = strtok_r(target_copy, "/", &saveptr);
	while (token != NULL && index < 4) {
		parts[index++] = token;
		token = strtok_r(NULL, "/", &saveptr);
	}
	if (index == 0) {
		error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
		goto out;
	}

	if (strcmp(parts[0], "system") == 0) {
		type = XN_XPC_DOMAIN_SYSTEM;
		handle = 0;
		index = 1;
		goto success;
	}
	if (strcmp(parts[0], "user") == 0) {
		type = XN_XPC_DOMAIN_USER;
		if (parts[1] == NULL) {
			handle = (uint64_t)getuid();
			index = 1;
			goto success;
		}
		goto parse_handle;
	}
	if (strcmp(parts[0], "login") == 0) {
		type = XN_XPC_DOMAIN_LOGIN;
		goto parse_handle;
	}
	if (strcmp(parts[0], "gui") == 0) {
		type = XN_XPC_DOMAIN_GUI;
		goto parse_handle;
	}
	if (strcmp(parts[0], "pid") == 0) {
		type = XN_XPC_DOMAIN_PID;
		goto parse_handle;
	}
	if (strcmp(parts[0], "loginwindow") == 0) {
		type = XN_XPC_DOMAIN_USER;
		handle = XN_XPC_DOMAIN_HANDLE_LOGINWINDOW;
		index = 1;
		goto success;
	}
	error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
	goto out;

parse_handle:
	if (parts[1] == NULL) {
		error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
		goto out;
	}
	endptr = NULL;
	handle = (uint64_t)strtoul(parts[1], &endptr, 10);
	if (endptr == parts[1] || *endptr != '\\0') {
		error = LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
		goto out;
	}
	index = 2;

success:
	if (parts[index] != NULL && *parts[index] != '\\0') {
		service = strdup(parts[index]);
		if (service == NULL) {
			error = ENOMEM;
			goto out;
		}
		xpc_dictionary_set_string(request, "name", service);
	}
	xpc_dictionary_set_uint64(request, "type", type);
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

static int
attach_cmd(int argc, char *const argv[])
{
	xpc_object_t request = NULL;
	xpc_object_t reply = NULL;
	char *service_name = NULL;
	int error;

	if (argc < 2) {
		launchctl_log(LOG_ERR, "usage: launchctl attach <target>");
		return 1;
	}
	request = xpc_dictionary_create(NULL, NULL, 0);
	if (request == NULL) {
		launchctl_log(LOG_ERR, "launchctl attach: out of memory");
		return 1;
	}
	error = parse_service_target(argv[1], request, &service_name);
	if (error != 0) {
		launchctl_log(LOG_ERR, "launchctl attach: bad service target: %s",
			argv[1]);
		xpc_release(request);
		return error;
	}
	if (service_name == NULL) {
		launchctl_log(LOG_ERR,
			"launchctl attach: a service target is required");
		xpc_release(request);
		return LAUNCHCTL_STATUS_SERVICE_TARGET_REQUIRED;
	}
	error = xpc_service_routine(XPC_ROUTINE_SERVICE_ATTACH, request, &reply);
	if (error == XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND) {
		launchctl_log(LOG_ERR, "Could not find service \\"%s\\" in domain for %s",
			service_name, argv[1]);
		error = XPC_LAUNCHD_ERROR_SERVICE_NOT_FOUND;
	} else if (error != 0) {
		launchctl_log(LOG_ERR, "launchctl attach: %d: %s", error,
			xpc_strerror(error));
	} else {
		printf("Attached to %s\\n", service_name);
	}
	if (reply != NULL) {
		xpc_release(reply);
	}
	free(service_name);
	xpc_release(request);
	return error;
}

'''

inserted = False
for i, line in enumerate(lines):
    if not inserted and line.rstrip("\n") == "list_cmd(int argc, char *const argv[])":
        if out and out[-1].strip() != "":
            out.append("\n")
        out.append(BLOCK)
        inserted = True
    out.append(line)

assert inserted, "list_cmd definition not found"

with open(mod, "w") as f:
    f.writelines(out)

print("surgery complete: {} -> {} ({} lines)".format(src, mod, len(out)))