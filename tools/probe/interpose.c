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
#include <sys/stat.h>
#include <sys/mman.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/fat.h>
#include <libkern/OSByteOrder.h>

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

/*
 * mach_msg2 path (macOS 15+/26 libxpc uses mach_msg2_internal directly
 * for connection data messages; classic mach_msg still goes through
 * mach_msg_overwrite -> mach_msg2_internal internally).
 *
 * mach_msg2_internal(data, option64, bits_and_send_size,
 *     remote_and_local_port, voucher_and_id, desc_count_and_rcv_name,
 *     rcv_size_and_priority, timeout)
 *
 * Packed halves (AArch64 LO/HI): send_size = HI32(bits_and_send_size),
 * bits = LO32; remote = LO32(remote_and_local), local = HI32;
 * voucher = LO32(voucher_and_id), id = HI32;
 * desc_count = LO32(dc_rn), rcv_name = HI32(dc_rn);
 * rcv_size = LO32(rs_pr), priority = HI32(rs_pr).
 */
#define MACH64_SEND_MSG 0x0000000000000100ull
#define MACH64_RCV_MSG  0x0000000000000200ull

typedef kern_return_t (*mach_msg2_internal_fn)(void *data,
    uint64_t option64, uint64_t bits_and_send_size,
    uint64_t remote_and_local_port, uint64_t voucher_and_id,
    uint64_t desc_count_and_rcv_name, uint64_t rcv_size_and_priority,
    uint64_t timeout);

static mach_msg2_internal_fn real_msg2_internal;

struct mach_msg2_trap_args {
	void *data;
	uint64_t options;
	uint64_t msgh_bits_and_send_size;
	uint64_t msgh_remote_and_local_port;
	uint64_t msgh_voucher_and_id;
	uint64_t desc_count_and_rcv_name;
	uint64_t rcv_size_and_priority;
	uint64_t timeout;
};

/* Kernel writes the reply (msg2_return_t) to *ret on success — separate
 * from the args. The probe must pass BOTH pointers through. */
struct msg2_return {
	uint64_t w[24];
};

typedef kern_return_t (*mach_msg2_trap_fn)(struct msg2_return *ret,
    struct mach_msg2_trap_args *args);

static mach_msg2_trap_fn real_msg2_trap;

/*
 * dyld4 applies the interpose override at bind-fixup time GLOBALLY, so
 * dlsym(handle,...), dlsym(RTLD_NEXT,...) AND even dlsym(RTLD_DEFAULT,...)
 * on an interposed name all return OUR probe (stack-overflow recursion).
 * Bypass dyld entirely: read the on-disk Mach-O symbol table ourselves
 * and slide the file offsets onto the runtime image using an anchor
 * symbol that is NOT interposed (mach_port_deallocate).
 */
static uint64_t symtab_value(const char *path, const char *name)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return 0;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		close(fd);
		return 0;
	}
	uint8_t *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE,
	    fd, 0);
	close(fd);
	if (m == MAP_FAILED) {
		return 0;
	}
	uint64_t result = 0;
	struct mach_header_64 *mh = (struct mach_header_64 *)m;
	if (mh->magic == FAT_MAGIC || mh->magic == FAT_CIGAM) {
		/*
		 * Universal binary: prefer the arm64e slice, fall back to
		 * plain arm64. CRITICAL: the shared-cache image this probe
		 * runs against is arm64e (cpusubtype 0x80000002). Using the
		 * plain-arm64 slice's n_values shifts every resolved symbol
		 * by (arm64e_off - arm64_off) relative to the anchor --
		 * 0x1458 for _mach_msg2_internal vs _mach_port_deallocate --
		 * which pointed "real" msg2_internal mid-function into
		 * os_packet_get_packetid and caused the memmove SIGSEGV we
		 * chased for days.
		 */
		struct fat_header *fh = (struct fat_header *)m;
		uint32_t nfat = OSSwapBigToHostInt32(fh->nfat_arch);
		struct fat_arch *fa = (struct fat_arch *)(m +
		    sizeof(struct fat_header));
		uint32_t want_sub = CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_PTRAUTH_ABI;
		int got_arm64e = 0;
		for (uint32_t i = 0; i < nfat; i++) {
			cpu_type_t cpu = (cpu_type_t)
			    OSSwapBigToHostInt32(fa[i].cputype);
			cpu_subtype_t sub = (cpu_subtype_t)
			    OSSwapBigToHostInt32(fa[i].cpusubtype);
			if (cpu != CPU_TYPE_ARM64) {
				continue;
			}
			uint32_t off = OSSwapBigToHostInt32(fa[i].offset);
			if (sub == want_sub && !got_arm64e) {
				mh = (struct mach_header_64 *)(m + off);
				got_arm64e = 1;
				break;
			}
			if (mh == (struct mach_header_64 *)m) {
				/* first plain-arm64 slice: keep as fallback */
				mh = (struct mach_header_64 *)(m + off);
			}
		}
	}
	if (mh->magic == MH_MAGIC_64 && mh->ncmds > 0) {
		uint64_t symoff = 0, stroff = 0, strsize = 0;
		uint32_t nsyms = 0;
		struct load_command *lc = (struct load_command *)((uint8_t *)mh +
		    sizeof(*mh));
		for (uint32_t i = 0; i < mh->ncmds; i++) {
			if (lc->cmd == LC_SYMTAB) {
				struct symtab_command *st_cmd =
				    (struct symtab_command *)lc;
				/* In a fat file the symtab offsets are
				 * slice-relative: rebase onto the file. */
				symoff = st_cmd->symoff + (uint64_t)((uint8_t *)mh - m);
				stroff = st_cmd->stroff + (uint64_t)((uint8_t *)mh - m);
				strsize = st_cmd->strsize;
				nsyms = st_cmd->nsyms;
				break;
			}
			lc = (struct load_command *)((uint8_t *)lc +
			    lc->cmdsize);
		}
		{
			char dbg[256];
			int dn = snprintf(dbg, sizeof(dbg),
			    "SYMDBG m=%p mh_off=%llu symoff=%llu stroff=%llu "
			    "strsize=%llu nsyms=%u size=%lld\n",
			    (void *)m,
			    (unsigned long long)((uint8_t *)mh - m),
			    (unsigned long long)symoff,
			    (unsigned long long)stroff,
			    (unsigned long long)strsize, nsyms,
			    (long long)st.st_size);
			write(2, dbg, (size_t)dn);
		}
		if (nsyms > 0 && symoff + (uint64_t)nsyms *
		    sizeof(struct nlist_64) <= (uint64_t)st.st_size &&
		    stroff + strsize <= (uint64_t)st.st_size) {
			struct nlist_64 *sl = (struct nlist_64 *)(m + symoff);
			char *strtab = (char *)(m + stroff);
			for (uint32_t i = 0; i < nsyms; i++) {
				if (!(sl[i].n_type & N_EXT)) {
					continue;
				}
				if (sl[i].n_un.n_strx >= strsize) {
					continue;
				}
				if (strcmp(strtab + sl[i].n_un.n_strx,
				    name) == 0) {
					result = sl[i].n_value;
					break;
				}
			}
		}
	}
	munmap(m, (size_t)st.st_size);
	return result;
}

static void resolve_real2(void)
{
	if (real_msg2_internal || real_msg2_trap) {
		return;
	}
	{
		char dbg[128];
		int dn = snprintf(dbg, sizeof(dbg),
		    "R2DBG enter real_msg2_off=%p\n", (void *)real_msg2_internal);
		write(2, dbg, (size_t)dn);
	}
	const char *path = "/usr/lib/system/libsystem_kernel.dylib";
	uint64_t msg_off = symtab_value(path, "_mach_msg2_internal");
	{
		char dbg[128];
		int dn = snprintf(dbg, sizeof(dbg), "R2DBG msg_off=%llx\n",
		    (unsigned long long)msg_off);
		write(2, dbg, (size_t)dn);
	}
	uint64_t trap_off = symtab_value(path, "_mach_msg2_trap");
	uint64_t anchor_off = symtab_value(path, "_mach_port_deallocate");
	/* mach_port_deallocate is NOT interposed: dlsym gives the real one. */
	void *anchor = dlsym(RTLD_DEFAULT, "mach_port_deallocate");
	{
		char dbg[200];
		int dn = snprintf(dbg, sizeof(dbg),
		    "R2DBG trap=%llx anchor_off=%llx anchor_rt=%p\n",
		    (unsigned long long)trap_off,
		    (unsigned long long)anchor_off, (void *)anchor);
		write(2, dbg, (size_t)dn);
	}
	if (msg_off == 0 || trap_off == 0 || anchor_off == 0 ||
	    anchor == NULL) {
		_Exit(112);
	}
	intptr_t slide = (intptr_t)anchor - (intptr_t)anchor_off;
	real_msg2_internal = (mach_msg2_internal_fn)(
	    (uintptr_t)msg_off + (uintptr_t)slide);
	real_msg2_trap = (mach_msg2_trap_fn)(
	    (uintptr_t)trap_off + (uintptr_t)slide);
	{
		char dbg[256];
		int dn = snprintf(dbg, sizeof(dbg),
		    "R2DBG msg=%llx trap=%llx anchor=%llx anchor_rt=%p slide=%llx\n",
		    (unsigned long long)msg_off, (unsigned long long)trap_off,
		    (unsigned long long)anchor_off, (void *)anchor,
		    (unsigned long long)slide);
		write(2, dbg, (size_t)dn);
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

static void log_msg2(const char *tag, void *data, uint64_t option64,
    uint64_t bits_and_send_size, uint64_t remote_and_local_port,
    uint64_t voucher_and_id, uint64_t desc_count_and_rcv_name,
    uint64_t rcv_size_and_priority)
{
	mach_msg_header_t *hdr = (mach_msg_header_t *)data;
	mach_msg_size_t send_size = (mach_msg_size_t)(bits_and_send_size >> 32);
	mach_msg_size_t size = send_size ? send_size : hdr->msgh_size;
	char line[256];
	int n = snprintf(line, sizeof(line),
	    "MSG2 %s opt=0x%llx size=%u id=0x%x bits=0x%x remote=0x%x "
	    "local=0x%x voucher=0x%x desc=%u rcv_name=0x%x",
	    tag, (unsigned long long)option64, size, hdr->msgh_id,
	    hdr->msgh_bits, hdr->msgh_remote_port, hdr->msgh_local_port,
	    hdr->msgh_voucher_port,
	    (unsigned int)(uint32_t)desc_count_and_rcv_name,
	    (unsigned int)(uint32_t)(desc_count_and_rcv_name >> 32));
	emit_text(line);
	emit(hdr, size);
}

kern_return_t probe_mach_msg2_internal(void *data, uint64_t option64,
    uint64_t bits_and_send_size, uint64_t remote_and_local_port,
    uint64_t voucher_and_id, uint64_t desc_count_and_rcv_name,
    uint64_t rcv_size_and_priority, uint64_t timeout)
{
	kern_return_t kr;

	resolve_real2();

	if (data) {
		char line[128];
		int n = snprintf(line, sizeof(line),
		    "MSG2I enter opt=0x%llx data=%p\n",
		    (unsigned long long)option64, data);
		write(g_log_fd, line, (size_t)n);
	}

	if (data && (option64 & MACH64_SEND_MSG)) {
		log_msg2("SEND", data, option64, bits_and_send_size,
		    remote_and_local_port, voucher_and_id,
		    desc_count_and_rcv_name, rcv_size_and_priority);
	}

	kr = real_msg2_internal(data, option64, bits_and_send_size,
	    remote_and_local_port, voucher_and_id, desc_count_and_rcv_name,
	    rcv_size_and_priority, timeout);

	if (kr == KERN_SUCCESS && data && (option64 & MACH64_RCV_MSG)) {
		char line[128];
		int n = snprintf(line, sizeof(line), "SEND2_RET kr=0x%x\n", kr);
		write(g_log_fd, line, (size_t)n);
		mach_msg_header_t *hdr = (mach_msg_header_t *)data;
		log_header("RECV2", hdr, hdr->msgh_size);
	}
	return kr;
}

kern_return_t probe_mach_msg2_trap(struct msg2_return *ret,
    struct mach_msg2_trap_args *args)
{
	kern_return_t kr;

	resolve_real2();

	{
		char line[128];
		int n = snprintf(line, sizeof(line),
		    "MSG2T enter opt=0x%llx data=%p\n",
		    (unsigned long long)args->options, args->data);
		write(g_log_fd, line, (size_t)n);
	}

	if (args->data && (args->options & MACH64_SEND_MSG)) {
		log_msg2("SEND", args->data, args->options,
		    args->msgh_bits_and_send_size,
		    args->msgh_remote_and_local_port,
		    args->msgh_voucher_and_id,
		    args->desc_count_and_rcv_name,
		    args->rcv_size_and_priority);
	}

	if (real_msg2_trap) {
		kr = real_msg2_trap(ret, args);
	} else {
		kr = real_msg2_internal(args->data, args->options,
		    args->msgh_bits_and_send_size,
		    args->msgh_remote_and_local_port,
		    args->msgh_voucher_and_id,
		    args->desc_count_and_rcv_name,
		    args->rcv_size_and_priority, args->timeout);
	}

	if (kr == KERN_SUCCESS && args->data &&
	    (args->options & MACH64_RCV_MSG)) {
		char line[128];
		int n = snprintf(line, sizeof(line), "TRAP2_RET kr=0x%x\n", kr);
		write(g_log_fd, line, (size_t)n);
		mach_msg_header_t *hdr = (mach_msg_header_t *)args->data;
		log_header("RECV2", hdr, hdr->msgh_size);
	}
	return kr;
}

/* Private mach_msg2 API (not in the public SDK headers on this OS). */
extern kern_return_t mach_msg2_internal(void *data, uint64_t option64,
    uint64_t bits_and_send_size, uint64_t remote_and_local_port,
    uint64_t voucher_and_id, uint64_t desc_count_and_rcv_name,
    uint64_t rcv_size_and_priority, uint64_t timeout);
extern kern_return_t mach_msg2_trap(struct msg2_return *ret,
    struct mach_msg2_trap_args *args);

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

__attribute__((used))
static struct interpose_entry interpose_mach_msg2_internal_entry
    __attribute__((section("__DATA,__interpose"))) = {
	(const void *)&probe_mach_msg2_internal,
	(const void *)&mach_msg2_internal,
};

__attribute__((used))
static struct interpose_entry interpose_mach_msg2_trap_entry
    __attribute__((section("__DATA,__interpose"))) = {
	(const void *)&probe_mach_msg2_trap,
	(const void *)&mach_msg2_trap,
};