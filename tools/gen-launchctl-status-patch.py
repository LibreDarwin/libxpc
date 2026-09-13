#!/usr/bin/env python3
"""Generate mk/patches/launchd/0001-*.patch by line surgery on a copy of
apple-oss launchctl.c.  Learned tab layout: copies separator runs from the
sibling cmds[] row so the patch bytes match the file's real style."""
import re
import sys

src = sys.argv[1]
dst = sys.argv[2]

with open(src, "r") as f:
    lines = f.readlines()

out = []

# --- Hunk 1: includes ---------------------------------------------------
i = 0
while i < len(lines):
    out.append(lines[i])
    if lines[i].rstrip("\n") == '#include "launch_internal.h"':
        # the blank line that follows it
        assert lines[i + 1].strip() == "", "expected blank after launch_internal.h"
        out.append(lines[i + 1])
        out.append('#include "xpc.h"\n')
        out.append('#include "xpc_private.h"\n')
        i += 1
    i += 1

lines = out
out = []

# --- Hunk 2: forward declaration -----------------------------------------
for line in lines:
    out.append(line)
    if line.rstrip("\n").startswith("static int help_cmd"):
        out.append("static int status_cmd(int argc, char *const argv[]);\n")

lines = out
out = []

# --- Hunk 3: cmds[] entry ------------------------------------------------
LIST_RE = re.compile(r'^(\t*)\{ "list",(\t+)list_cmd,(\t+)"List jobs.*')

new_row = None
for line in lines:
    if new_row is None:
        m = LIST_RE.match(line)
        if m:
            lead, sep1, sep2 = m.group(1), m.group(2), m.group(3)
            new_row = lead + '{ "status",' + sep1 + 'status_cmd,' + sep2 + \
                '"Query the launchd instance (housekeeping probe)." },\n'
    out.append(line)

lines = out
out = []

# insert the status row immediately after the "list" row
for i, line in enumerate(lines):
    out.append(line)
    if LIST_RE.match(line) and i + 1 < len(lines):
        out.append(new_row)

lines = out
out = []

# --- Hunk 4: status_cmd definition before list_cmd ------------------------
FUNC = '''static int
status_cmd(int argc, char *const argv[])
{
	struct xpc_global_data *state;
	xpc_object_t request = NULL;
	xpc_object_t reply = NULL;
	uuid_t instance = {0};
	const char *banner;
	const char *status;
	int64_t pid;
	int64_t rerr;
	int error;

	(void)argc;
	(void)argv;

	state = xpc_global_data();
	if (state == NULL || state->xpc_bootstrap_pipe == NULL) {
		launchctl_log(LOG_ERR, "launchctl status: no bootstrap pipe");
		return 1;
	}

	request = xpc_dictionary_create(NULL, NULL, 0);
	if (request == NULL) {
		launchctl_log(LOG_ERR, "launchctl status: out of memory");
		return 1;
	}

	/* The housekeeping probe real launchctl emits before every command
	 * (XPC_ROUTINE_SERVICE_STATUS, 0x400000cf): a dict of handle, type,
	 * flags, targetpid, name and instance, plus the bootstrap port under
	 * "domain-port" when not root.  Observed wire layout: WIRE_FORMAT.md,
	 * section 11.2. */
	xpc_dictionary_set_uint64(request, "handle", 0);
	xpc_dictionary_set_uint64(request, "type", 1); /* system domain */
	xpc_dictionary_set_uint64(request, "flags", 0);
	xpc_dictionary_set_uint64(request, "targetpid", 0);
	xpc_dictionary_set_string(request, "name", "");
	xpc_dictionary_set_uuid(request, "instance", instance);
	if (geteuid() != 0) {
		mach_port_t bootstrap = MACH_PORT_NULL;
		task_get_bootstrap_port(mach_task_self(), &bootstrap);
		xpc_dictionary_set_mach_send(request, "domain-port", bootstrap);
	}

	error = xpc_pipe_routine(state->xpc_bootstrap_pipe, request, &reply,
		XPC_ROUTINE_SERVICE_STATUS);
	if (error) {
		launchctl_log(LOG_ERR, "launchctl status: %s", xpc_strerror(error));
		xpc_release(request);
		return error;
	}

	rerr = reply ? xpc_dictionary_get_int64(reply, "error") : 0;
	if (rerr != 0) {
		launchctl_log(LOG_ERR, "launchctl status: %lld: %s",
			(long long)rerr, xpc_strerror((int)rerr));
		error = (int)rerr;
		goto out;
	}

	banner = reply ? xpc_dictionary_get_string(reply, "launchd") : NULL;
	status = reply ? xpc_dictionary_get_string(reply, "status") : NULL;
	pid = reply ? xpc_dictionary_get_int64(reply, "pid") : 0;
	printf("%s is running (%s, pid %lld)\\n",
		banner ? banner : "launchd", status ? status : "ok",
		(long long)pid);
	error = 0;
out:
	if (reply != NULL) {
		xpc_release(reply);
	}
	xpc_release(request);
	return error;
}
'''

done = False
for i, line in enumerate(lines):
    if line.rstrip("\n") == "list_cmd(int argc, char *const argv[])":
        # ensure a blank line separates the new function from the previous one
        if out and out[-1].strip() != "":
            out.append("\n")
        out.append(FUNC)
        if lines[i - 1].strip() != "":
            out.append("\n")
        done = True
    out.append(line)

assert done, "list_cmd definition not found"

with open(dst, "w") as f:
    f.writelines(out)

print("surgery complete: {} -> {} ({} lines)".format(src, dst, len(out)))