# XPC.framework — BSD make build.
# Intermediates stay below build/; installable framework goes below
# build/release/ so source trees remain clean and reproducible.

CC?=clang
AR?=ar
BUILD?=${.CURDIR}/build
RELEASE?=${BUILD}/release
OBJDIR=${BUILD}/obj
FRAMEWORK=${RELEASE}/XPC.framework
LIB=${FRAMEWORK}/XPC
CFLAGS+=-std=c11 -fblocks -Wall -Wextra -Werror
CFLAGS+=-I${.CURDIR}/XPC.framework/Headers -I${.CURDIR}/XPC.framework/src
LDFLAGS?=

SRCS= xpc_array xpc_data xpc_description xpc_deserialize xpc_dictionary \
      xpc_extra xpc_object xpc_pipe xpc_serialize xpc_string xpc_types xpc_value
OBJS= ${OBJDIR}/xpc_array.o ${OBJDIR}/xpc_data.o \
      ${OBJDIR}/xpc_description.o ${OBJDIR}/xpc_deserialize.o \
      ${OBJDIR}/xpc_dictionary.o ${OBJDIR}/xpc_extra.o \
      ${OBJDIR}/xpc_object.o ${OBJDIR}/xpc_pipe.o \
      ${OBJDIR}/xpc_serialize.o ${OBJDIR}/xpc_string.o \
      ${OBJDIR}/xpc_types.o ${OBJDIR}/xpc_value.o

.PHONY: all release clean test

all: release

release: ${LIB} ${FRAMEWORK}/Headers/xpc.h ${FRAMEWORK}/Modules/module.modulemap ${FRAMEWORK}/Resources/Info.plist

${OBJDIR}:
	@mkdir -p ${OBJDIR}

${OBJDIR}/xpc_array.o: XPC.framework/src/xpc_array.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_data.o: XPC.framework/src/xpc_data.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_description.o: XPC.framework/src/xpc_description.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_deserialize.o: XPC.framework/src/xpc_deserialize.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_dictionary.o: XPC.framework/src/xpc_dictionary.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_extra.o: XPC.framework/src/xpc_extra.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_object.o: XPC.framework/src/xpc_object.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_pipe.o: XPC.framework/src/xpc_pipe.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_serialize.o: XPC.framework/src/xpc_serialize.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_string.o: XPC.framework/src/xpc_string.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_types.o: XPC.framework/src/xpc_types.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}
${OBJDIR}/xpc_value.o: XPC.framework/src/xpc_value.c
	@mkdir -p ${OBJDIR}
	${CC} ${CFLAGS} -c ${.ALLSRC} -o ${.TARGET}

${RELEASE}:
	@mkdir -p ${RELEASE}

${FRAMEWORK}/Headers/xpc.h: XPC.framework/Headers/xpc.h
	@mkdir -p ${RELEASE} ${FRAMEWORK}/Headers && cp ${.ALLSRC} ${.TARGET}

${FRAMEWORK}/Modules/module.modulemap: XPC.framework/Modules/module.modulemap
	@mkdir -p ${RELEASE} ${FRAMEWORK}/Modules && cp ${.ALLSRC} ${.TARGET}

${FRAMEWORK}/Resources/Info.plist: XPC.framework/Resources/Info.plist
	@mkdir -p ${RELEASE} ${FRAMEWORK}/Resources && cp ${.ALLSRC} ${.TARGET}

${LIB}: ${OBJS}
	@mkdir -p ${RELEASE} ${FRAMEWORK}
	${CC} -dynamiclib ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} -Wl,-install_name,@rpath/XPC.framework/XPC

test: release ${BUILD}/test_core
	${BUILD}/test_core

${BUILD}/test_core: tests/test_core.c ${OBJS}
	@mkdir -p ${BUILD}
	${CC} ${CFLAGS} tests/test_core.c ${OBJS} -o ${.TARGET}

clean:
	rm -rf ${BUILD}
