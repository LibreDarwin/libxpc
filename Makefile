# bmake (BSD make).  Top-level build for the xnuports Darwin userland.
#
# The tree is shaped like Apple's libSystem family:
#
#   src/libsystem/xpc/  the xpc component -> libsystem_xpc.dylib (own Makefile)
#   src/launchctl/   launchctl (system_cmds territory, NOT libSystem)
#   src/launchd/     launchd_stub test double (launchd territory, NOT libSystem)
#   src/XPC.framework/  re-export umbrella project (Modules + Resources)
#   src/apple/       pristine apple-oss submodules (reference sources only)
#   mk/patches/      numbered patch series applied to copies of src/apple/*

CC	?= clang
RM	= rm -rf
ECHO	= echo

BUILD	 := ${.CURDIR}/build
OBJDIR	 := ${BUILD}/obj
RELEASE	 := ${BUILD}/release
LIBS	 := ${RELEASE}/libsystem_xpc.dylib
LAUNCHCTL:= ${RELEASE}/launchctl
LAUNCHD	 := ${RELEASE}/launchd_stub
FRAMEWORK:= ${RELEASE}/XPC.framework

SDK_PATH!=	xcrun --show-sdk-path 2>/dev/null || true
INCLUDES := -I${.CURDIR}/src/libsystem/xpc/include -I${SDK_PATH}/usr/include
DEFINES := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS	:= -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
		-MMD -MP ${INCLUDES} ${DEFINES}

# Apple sources (src/apple/*) stay pristine.  The build copies the launchd
# submodule into build/launchd-src and applies mk/patches/launchd/*.patch
# in order to the copy, the way xcode-tools does it: a numbered patch
# series per component, applied with `patch -p1 --forward`.
LAUNCHD_PATCHES!=	ls ${.CURDIR}/mk/patches/launchd/*.patch 2>/dev/null || true

build/launchd-src/.patched: ${LAUNCHD_PATCHES}
	@mkdir -p build/launchd-src
	@rsync -a --delete --exclude .git src/apple/launchd/ build/launchd-src/
	@for p in ${LAUNCHD_PATCHES}; do \
	    ${ECHO} "  apply $${p}"; \
	    (cd build/launchd-src && patch -s -p1 --forward < "$$p") || exit 1; \
	done
	@touch $@

patch-apple: build/launchd-src/.patched

.PHONY: all libxpc launchctl launchd test release patch-apple clean ${LIBS}

all: libxpc launchctl launchd release

libxpc: ${LIBS}

launchctl: ${LAUNCHCTL}

launchd: ${LAUNCHD}

# The component Makefile owns the object dependency graph (including its
# .d files), so the root always delegates; the sub-make decides freshness.
${LIBS}:
	${.MAKE} -C src/libsystem/xpc RELEASE=${RELEASE} OBJDIR=${OBJDIR}

${RELEASE}:
	@mkdir -p $@

${LAUNCHCTL}: src/launchctl/launchctl.c src/libsystem/xpc/include/xpc.h ${LIBS}
	${CC} ${CFLAGS} src/launchctl/launchctl.c -L${RELEASE} -lsystem_xpc \
	    -Wl,-rpath,${RELEASE} -o $@

${LAUNCHD}: src/launchd/launchd_stub.c src/launchctl/launchctl.c \
    src/libsystem/xpc/include/xpc.h ${LIBS}
	${CC} ${CFLAGS} -DXNUXPORTS_EMBED -c src/launchctl/launchctl.c \
	    -o ${OBJDIR}/launchctl_embed.o
	${CC} ${CFLAGS} src/launchd/launchd_stub.c ${OBJDIR}/launchctl_embed.o \
	    -L${RELEASE} -lsystem_xpc -lpthread \
	    -Wl,-rpath,${RELEASE} -o $@

# XPC.framework is a re-export umbrella, like Apple's: a thin dylib whose
# only load command is LC_REEXPORT_DYLIB of our libsystem_xpc.
${FRAMEWORK}: ${LIBS} ${RELEASE}
	@mkdir -p $@/Versions/A/Headers $@/Versions/A/Modules $@/Versions/A/Resources
	${CC} -dynamiclib -install_name @rpath/XPC.framework/Versions/A/XPC \
	    -Wl,-reexport_library,${LIBS} -o $@/Versions/A/XPC
	cp src/libsystem/xpc/include/xpc.h $@/Versions/A/Headers/
	cp src/XPC.framework/Modules/module.modulemap $@/Versions/A/Modules/ 2>/dev/null || true
	cp src/XPC.framework/Resources/Info.plist $@/Versions/A/Resources/ 2>/dev/null || true
	ln -sfh A $@/Versions/Current
	ln -sfh Versions/Current/Headers $@/Headers
	ln -sfh Versions/Current/Modules $@/Modules
	ln -sfh Versions/Current/Resources $@/Resources
	ln -sfh Versions/Current/XPC $@/XPC

release: ${FRAMEWORK}

test: all
	sh tools/e2e-launchd.sh

clean:
	${RM} ${BUILD}