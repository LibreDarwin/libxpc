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
 * interpose.c -- mach_msg recorder.
 *
 * DYLD_INSERT_LIBRARIES shim that logs every mach_msg crossing in the
 * process, so the probe can capture the raw bytes libxpc puts on the wire.
 *
 * Architecture notes (this macOS, dyld4 + mach_msg2 migration):
 *
 *  - libxpc imports ONLY mach_msg / mach_msg_receive / mach_msg_destroy.
 *    Hooking those two (mach_msg + mach_msg_receive) covers all libxpc
 *    wire traffic.  mach_msg2 / mach_msg2_trap belong to other dylibs.
 *  - dlsym() returns the INTERPOSED function even when asked on a direct
 *    image handle (dyld4 applies interpose at bind-fixup time globally),
 *    so resolving "the real mach_msg" always recurses into ourselves.
 *  - CRITICAL: do NOT call mach_msg_overwrite_trap directly for the
 *    forward.  On this kernel (mach_msg2 migration) the classic
 *    mach_msg/mach_msg_receive behavior is provided by the C function
 *    mach_msg_overwrite(), which TRANSLATES the classic args into a
 *    mach_msg2 descriptor block and calls mach_msg2_internal ->
 *    mach_msg2_trap.  The legacy mach_msg_overwrite_trap syscall is
 *    still present but the classic 8-register layout no longer means
 *    what it used to; a raw call with classic args makes the kernel
 *    misread the registers (observed: blocking send forever).
 *  - Therefore the forward target is mach_msg_overwrite() itself, which
 *    is NOT interposed, so dlsym() returns the real wrapper.  mach_msg is
 *    exactly mach_msg_overwrite(msg, option, send_size, rcv_size,
 *    rcv_name, timeout, 0, rcv_msg=NULL), and mach_msg_receive is
 *    mach_msg_overwrite(msg, MACH_RCV_MSG, 0, msg->msgh_size,
 *    msg->msgh_local_port, timeout, notify, NULL).
 *  - libxpc's own initializer (_xpc_early_init -> pid-domain pipe
 *    check-in) calls mach_msg BEFORE our constructor runs, so all
 *    resolution is lazy.
 *
 * Async-signal-safe: everything goes through write(2) to an fd opened
 * from XPC_PROBE_LOG (falls back to fd 3).
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/message.h>

#define LOG_FD 3

static int g_log_fd = -1;

/* The real 8-arg mach_msg_overwrite wrapper, NOT subject to interpose. */
static kern_return_t (*real_msg_overwrite)(mach_msg_header_t *msg,
    mach_msg_option_t option, mach_msg_size_t send_size,
    mach_msg_size_t rcv_size, mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout, mach_port_name_t notify,
    mach_msg_header_t *rcv_msg);

static void resolve_real(void)
{
	if (real_msg_overwrite) {
		return;
	}
	void *handle = dlopen("/usr/lib/system/libsystem_kernel.dylib",
	    RTLD_NOW | RTLD_LOCAL);
	if (handle) {
		real_msg_overwrite = (kern_return_t (*)(mach_msg_header_t *,
		    mach_msg_option_t, mach_msg_size_t, mach_msg_size_t,
		    mach_port_name_t, mach_msg_timeout_t, mach_port_name_t,
		    mach_msg_header_t *))dlsym(handle, "mach_msg_overwrite");
	}
	if (handle == NULL || real_msg_overwrite == NULL) {
		_Exit(111);
	}
	if (g_log_fd < 0) {
		const char *path = getenv("XPC_PROBE_LOG");
		if (path) {
			g_log_fd = open(path,
			    O_WRONLY | O_CREAT | O_APPEND, 0644);
		} else {
			g_log_fd = LOG_FD;
		}
	}
}

static void emit(const void *buf, size_t len)
{
	if (g_log_fd < 0) {
		return;
	}
	const unsigned char *p = buf;
	char line[160];
	size_t i;
	int n;

	for (i = 0; i < len; i += 16) {
		size_t j, chunk = len - i < 16 ? len - i : 16;
		n = snprintf(line, sizeof(line), "%08zx ", i);
		write(g_log_fd, line, (size_t)n);
		for (j = 0; j < chunk; j++) {
			n = snprintf(line, sizeof(line), "%02x ", p[i + j]);
			write(g_log_fd, line, (size_t)n);
		}
		write(g_log_fd, "\n", 1);
	}
}

static void emit_text(const char *s)
{
	if (g_log_fd >= 0) {
		write(g_log_fd, s, strlen(s));
		write(g_log_fd, "\n", 1);
	}
}

static void log_header(const char *tag, mach_msg_header_t *msg,
    mach_msg_size_t size)
{
	char line[256];
	int n = snprintf(line, sizeof(line),
	    "%s size=%u id=0x%x bits=0x%x remote=0x%x local=0x%x voucher=0x%x",
	    tag, size, msg->msgh_id, msg->msgh_bits, msg->msgh_remote_port,
	    msg->msgh_local_port, msg->msgh_voucher_port);
	emit_text(line);
	emit(msg, size);
}

/*
 * The classic 7-arg mach_msg.  Log the outgoing bytes before the call,
 * log the landed bytes after when this is also a receive.
 */
kern_return_t probe_mach_msg(mach_msg_header_t *msg,
    mach_msg_option_t option, mach_msg_size_t send_size,
    mach_msg_size_t rcv_size, mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout, mach_port_name_t notify)
{
	kern_return_t kr;

	resolve_real();

	if (msg != NULL && (option & MACH_SEND_MSG)) {
		char line[160];
		int n = snprintf(line, sizeof(line),
		    "SEND_IN opt=0x%x send_size=%u rcv_size=%u rcv_name=0x%x "
		    "to=%u notify=0x%x\n",
		    option, send_size, rcv_size, rcv_name, timeout, notify);
		write(g_log_fd, line, (size_t)n);
		size_t body = send_size ? send_size : msg->msgh_size;
		log_header("SEND", msg, (mach_msg_size_t)body);
	}

	kr = real_msg_overwrite(msg, option, send_size, rcv_size, rcv_name,
	    timeout, notify, NULL);

	if (msg != NULL && (option & MACH_SEND_MSG)) {
		char line[128];
		int n = snprintf(line, sizeof(line),
		    "SEND_RET kr=0x%x\n", kr);
		write(g_log_fd, line, (size_t)n);
	}

	if (msg != NULL && (option & MACH_RCV_MSG) && kr == KERN_SUCCESS) {
		log_header("RECV", msg, msg->msgh_size);
	}
	return kr;
}

/*
 * The 4-arg receive-only path: overwrite(msg, MACH_RCV_MSG, 0,
 * msg->msgh_size, msg->msgh_local_port, timeout, notify).
 */
kern_return_t probe_mach_msg_receive(mach_msg_header_t *msg,
    mach_port_name_t rcv_name, mach_msg_timeout_t timeout,
    mach_port_name_t notify)
{
	kern_return_t kr;

	resolve_real();

	kr = real_msg_overwrite(msg, MACH_RCV_MSG, 0, msg->msgh_size,
	    msg->msgh_local_port, timeout, notify, NULL);

	if (kr == KERN_SUCCESS && msg != NULL) {
		log_header("RECV", msg, msg->msgh_size);
	}
	return kr;
}

/* Explicit interpose entries (equivalent to DYLD_INTERPOSE). */
__attribute__((used))
static struct interpose_entry {
	const void *replacement;
	const void *replacee;
} interpose_mach_msg_entry __attribute__((section("__DATA,__interpose"))) = {
	(const void *)&probe_mach_msg,
	(const void *)&mach_msg,
};

__attribute__((used))
static struct interpose_entry interpose_mach_msg_receive_entry
    __attribute__((section("__DATA,__interpose"))) = {
	(const void *)&probe_mach_msg_receive,
	(const void *)&mach_msg_receive,
};