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
#   include/         SPI declarations no SDK ships, for the Apple sources

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
LAUNCHD_SRC	:= ${BUILD}/launchd-src
LAUNCHD_GEN	:= ${BUILD}/gen/launchd
LAUNCHD_PATCHES!=	ls ${.CURDIR}/mk/patches/launchd/*.patch 2>/dev/null || true

${LAUNCHD_SRC}/.patched: ${LAUNCHD_PATCHES}
	@mkdir -p ${LAUNCHD_SRC}
	@rsync -a --delete --exclude .git src/apple/launchd/ ${LAUNCHD_SRC}/
	@for p in ${LAUNCHD_PATCHES}; do \
	    ${ECHO} "  apply $${p}"; \
	    (cd ${LAUNCHD_SRC} && patch -s -p1 --forward < "$$p") || exit 1; \
	done
	@touch $@

patch-apple: ${LAUNCHD_SRC}/.patched

# liblaunch: launchd's liblaunch, libvproc and libbootstrap, built from the
# patched copy into libsystem_xpc -- on modern Darwin the launch_*, vproc_*
# and bootstrap_* API lives in libxpc.  They are Apple's sources, so they
# build with Apple's flags (liblaunch.xcconfig) rather than our -Werror.
#
# Their private headers come from xcode-tools' internal SDK, searched after
# the public SDK so it only fills gaps; include/ comes first, for the SPI no
# SDK carries.  A built xcode-tools is found beside this tree, or inside
# LibreDarwin; INTERNAL_SDK=<path> names another.
.if !defined(INTERNAL_SDK)
.for _xct in ${.CURDIR}/../xcode-tools ${.CURDIR}/../../Developer/xcode-tools
_isdk:=	${_xct}/build/release/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.Internal.sdk
.if !defined(INTERNAL_SDK) && exists(${_isdk}/usr/include)
INTERNAL_SDK:=	${_isdk}
.endif
.endfor
.endif

LIBLAUNCH_SRCS	:= ${LAUNCHD_SRC}/liblaunch/liblaunch.c \
		   ${LAUNCHD_SRC}/liblaunch/libvproc.c \
		   ${LAUNCHD_SRC}/liblaunch/libbootstrap.c \
		   ${LAUNCHD_GEN}/jobUser.c \
		   ${LAUNCHD_GEN}/helperUser.c \
		   ${LAUNCHD_GEN}/helperServer.c
LIBLAUNCH_OBJS	:= ${LIBLAUNCH_SRCS:T:R:S,^,${OBJDIR}/liblaunch/,:S,$,.o,}
LIBLAUNCH_CFLAGS:= -isysroot ${SDK_PATH} -fblocks -g -O0 -fvisibility=hidden \
		   -I${LAUNCHD_GEN} -I${.CURDIR}/include \
		   -I${LAUNCHD_SRC}/src -I${LAUNCHD_SRC}/liblaunch \
		   -idirafter ${INTERNAL_SDK}/usr/include \
		   -idirafter ${INTERNAL_SDK}/usr/local/include \
		   -D__MigTypeCheck=1 -Dmig_external=__private_extern__ \
		   -D_DARWIN_USE_64_BIT_INODE=1 -D__DARWIN_NON_CANCELABLE=1 \
		   -DXPC_BUILDING_LAUNCHD=1

${LAUNCHD_GEN}/.mig: ${LAUNCHD_SRC}/.patched
	@test -d "${INTERNAL_SDK}/usr/include" || { \
	    ${ECHO} "liblaunch: no internal SDK -- build xcode-tools, or pass INTERNAL_SDK=<path>"; \
	    exit 1; }
	@mkdir -p ${LAUNCHD_GEN}
.for _d in job helper
	cd ${LAUNCHD_GEN} && mig -isysroot ${SDK_PATH} -DXPC_BUILDING_LAUNCHD=1 \
	    -I${LAUNCHD_SRC}/src -I${LAUNCHD_SRC}/liblaunch \
	    -user ${_d}User.c -header ${_d}.h \
	    -server ${_d}Server.c -sheader ${_d}Server.h \
	    ${LAUNCHD_SRC}/src/${_d}.defs
.endfor
	@touch $@

.for _s in ${LIBLAUNCH_SRCS}
${OBJDIR}/liblaunch/${_s:T:R}.o: ${LAUNCHD_GEN}/.mig
	@mkdir -p ${.TARGET:H}
	${CC} ${LIBLAUNCH_CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

.PHONY: all libxpc launchctl launchd test release patch-apple clean ${LIBS}

all: libxpc launchctl launchd release

libxpc: ${LIBS}

launchctl: ${LAUNCHCTL}

launchd: ${LAUNCHD}

# The component Makefile owns the object dependency graph (including its
# .d files), so the root always delegates; the sub-make decides freshness.
${LIBS}: ${LIBLAUNCH_OBJS}
	${.MAKE} -C src/libsystem/xpc RELEASE=${RELEASE} OBJDIR=${OBJDIR} \
	    EXTRA_OBJS="${LIBLAUNCH_OBJS}"

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
