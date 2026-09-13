# bmake (BSD make).  Builds xnuports libxpc and the launchctl
# reimplementation.

CC	?= clang
RM	= rm -rf
ECHO	= echo

BUILD	 := ${.CURDIR}/build
OBJDIR	 := ${BUILD}/obj
RELEASE	 := ${BUILD}/release
LIBS	 := ${RELEASE}/libxpc.dylib
LAUNCHCTL:= ${RELEASE}/launchctl
LAUNCHD	 := ${RELEASE}/launchd_stub
FRAMEWORK:= ${RELEASE}/XPC.framework

SDK_PATH!=	xcrun --show-sdk-path 2>/dev/null || true
INCLUDES := -I${.CURDIR}/src/libxpc/include -I${SDK_PATH}/usr/include
DEFINES := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS	:= -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
		-MMD -MP ${INCLUDES} ${DEFINES}
LDFLAGS	:= -dynamiclib -install_name @rpath/libxpc.dylib

LIB_SRCS!=	find ${.CURDIR}/src/libxpc/object ${.CURDIR}/src/libxpc/wire \
		${.CURDIR}/src/libxpc/pipe ${.CURDIR}/src/libxpc/connection \
		-name '*.c' | sort
OBJS	:= ${LIB_SRCS:T:S,.c$,.o,:S,^,${OBJDIR}/,}
DEPFILES:= ${OBJS:S,.o$,.d,}

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

.PHONY: all libxpc launchctl launchd test release patch-apple clean

all: libxpc launchctl launchd release

libxpc: ${LIBS}

launchctl: ${LAUNCHCTL}

launchd: ${LAUNCHD}

test: all
	sh tools/e2e-launchd.sh

${OBJDIR}:
	@mkdir -p $@

${RELEASE}:
	@mkdir -p $@

.for _s in ${LIB_SRCS}
${OBJDIR}/${_s:T:R}.o: ${_s}
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${_s} -o ${.TARGET}
.endfor

${LIBS}: ${OBJS} ${RELEASE}
	${CC} ${LDFLAGS} -o $@ ${OBJS}

${LAUNCHCTL}: src/launchctl/launchctl.c ${LIBS}
	${CC} ${CFLAGS} src/launchctl/launchctl.c -L${RELEASE} -lxpc \
	    -Wl,-rpath,${RELEASE} -o $@

${LAUNCHD}: src/launchd/launchd_stub.c src/launchctl/launchctl.c ${LIBS}
	${CC} ${CFLAGS} -DXNUXPORTS_EMBED -c src/launchctl/launchctl.c \
	    -o ${OBJDIR}/launchctl_embed.o
	${CC} ${CFLAGS} src/launchd/launchd_stub.c ${OBJDIR}/launchctl_embed.o \
	    -L${RELEASE} -lxpc -lpthread \
	    -Wl,-rpath,${RELEASE} -o $@

# Assemble a minimal XPC.framework bundle from the built dylib.
${FRAMEWORK}: ${LIBS} ${RELEASE}
	@mkdir -p $@/Versions/A/Headers $@/Versions/A/Modules $@/Versions/A/Resources
	cp ${LIBS} $@/Versions/A/libxpc
	cp src/libxpc/include/xpc.h $@/Versions/A/Headers/
	cp XPC.framework/Modules/module.modulemap $@/Versions/A/Modules/ 2>/dev/null || true
	cp XPC.framework/Resources/Info.plist $@/Versions/A/Resources/ 2>/dev/null || true
	ln -sfh A $@/Versions/Current
	ln -sfh Versions/Current/Headers $@/Headers
	ln -sfh Versions/Current/Modules $@/Modules
	ln -sfh Versions/Current/Resources $@/Resources
	ln -sfh Versions/Current/libxpc $@/libxpc

release: ${FRAMEWORK}

clean:
	${RM} ${BUILD}

-include ${DEPFILES}