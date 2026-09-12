# GNU make (make(1) on macOS is GNU make 3.81)
#
# Builds xnuports libxpc and the launchctl reimplementation.

CC        ?= clang
RM        ?= rm -rf

BUILD     := $(CURDIR)/build
OBJDIR    := $(BUILD)/obj
RELEASE   := $(BUILD)/release
LIBS      := $(RELEASE)/libxpc.dylib
LAUNCHCTL := $(RELEASE)/launchctl
LAUNCHD   := $(RELEASE)/launchd_stub
FRAMEWORK := $(RELEASE)/XPC.framework

INCLUDES  := -I$(CURDIR)/src/libxpc/include \
             -I$(shell xcrun --show-sdk-path 2>/dev/null)/usr/include
DEFINES   := -DMACOSX -DDARWIN64 -DDARWIN -DBUILD_DARWIN
CFLAGS    := -std=c11 -fblocks -g -O0 -Wall -Wextra -Werror \
             -MMD -MP $(INCLUDES) $(DEFINES)
LDFLAGS   := -dynamiclib -install_name @rpath/libxpc.dylib

LIB_SRCS  := $(sort $(wildcard src/libxpc/object/*.c src/libxpc/wire/*.c \
                       src/libxpc/pipe/*.c src/libxpc/connection/*.c))
OBJS      := $(patsubst %.c,$(OBJDIR)/%.o,$(notdir $(LIB_SRCS)))
DEPFILES  := $(OBJS:.o=.d)

vpath %.c src/libxpc/object src/libxpc/wire src/libxpc/pipe src/libxpc/connection

.PHONY: all libxpc launchctl launchd test release clean

all: libxpc launchctl launchd release

libxpc: $(LIBS)

launchctl: $(LAUNCHCTL)

launchd: $(LAUNCHD)

test: all
	sh tools/e2e-launchd.sh

$(OBJDIR):
	@mkdir -p $@

$(RELEASE):
	@mkdir -p $@

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBS): $(OBJS) | $(RELEASE)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(LAUNCHCTL): src/launchctl/launchctl.c $(LIBS)
	$(CC) $(CFLAGS) src/launchctl/launchctl.c -L$(RELEASE) -lxpc \
	    -Wl,-rpath,$(RELEASE) -o $@

$(LAUNCHD): src/launchd/launchd_stub.c src/launchctl/launchctl.c $(LIBS)
	$(CC) $(CFLAGS) -DXNUXPORTS_EMBED -c src/launchctl/launchctl.c \
	    -o $(OBJDIR)/launchctl_embed.o
	$(CC) $(CFLAGS) src/launchd/launchd_stub.c $(OBJDIR)/launchctl_embed.o \
	    -L$(RELEASE) -lxpc -lpthread \
	    -Wl,-rpath,$(RELEASE) -o $@

# Assemble a minimal XPC.framework bundle from the built dylib.
$(FRAMEWORK): $(LIBS) | $(RELEASE)
	@mkdir -p $@/Versions/A/Headers $@/Versions/A/Modules $@/Versions/A/Resources
	cp $(LIBS) $@/Versions/A/libxpc
	cp src/libxpc/include/xpc.h $@/Versions/A/Headers/
	cp XPC.framework/Modules/module.modulemap $@/Versions/A/Modules/ 2>/dev/null || true
	cp XPC.framework/Resources/Info.plist $@/Versions/A/Resources/ 2>/dev/null || true
	ln -sfh A $@/Versions/Current
	ln -sfh Versions/Current/Headers $@/Headers
	ln -sfh Versions/Current/Modules $@/Modules
	ln -sfh Versions/Current/Resources $@/Resources
	ln -sfh Versions/Current/libxpc $@/libxpc

release: $(FRAMEWORK)

clean:
	$(RM) $(BUILD)

-include $(DEPFILES)