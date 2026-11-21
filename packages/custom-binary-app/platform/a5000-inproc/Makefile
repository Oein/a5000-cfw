# a5000 in-process backend -> a5kinproc.so (route (b), loaded into im.elf).
#
# Builds the platform-agnostic uicore sources + the in-process adapters into one
# glibc ARM shared object. im.elf is glibc (/lib/ld-2.11.2.so), so this uses the
# a5k-cross (glibc) toolchain, NOT bionic. The C++ runtime is linked STATICALLY
# (-static-libstdc++ -static-libgcc) so the .so has no libstdc++ NEEDED to satisfy
# on the guest -- only libc/libpthread/libm, which im.elf already has.
#
# Run inside docker: make            (image a5k-cross)
# Wrapper:           ../../build.sh a5000-inproc
NATIVE  := ../..
UICORE  := $(NATIVE)/uicore
CXX     := arm-linux-gnueabi-g++
CC      := arm-linux-gnueabi-gcc
ARCH    := -march=armv7-a -mthumb -mfpu=vfpv3-d16 -mfloat-abi=softfp
CXXFLAGS := $(ARCH) -Os -Wall -fPIC -fno-exceptions -fno-rtti -fno-stack-protector \
            -U_FORTIFY_SOURCE -DUICORE_ASSETS_MMAP \
            -I$(UICORE)/include -I.
# Debug build: make DEBUG=1 -> -DA5K_DEBUG (in-app dbgcmd/dbgout channel), separate output dir.
ifeq ($(DEBUG),1)
CXXFLAGS += -DA5K_DEBUG
OUT := build-debug
else
OUT := build
endif
LDFLAGS := $(ARCH) -shared -fPIC -static-libstdc++ -static-libgcc \
           -Wl,--hash-style=sysv -Wl,-soname,a5kinproc.so
LDLIBS  := -lpthread -lm

OBJDIR  := $(OUT)/obj
TARGETSO:= $(OUT)/a5kinproc.so

# uicore core (compiled here with -fPIC; the committed libuicore.a is non-PIC).
UICORE_SRCS := \
  src/action.cpp src/keymap.cpp src/keymap_shoot.cpp src/log.cpp src/canvas.cpp src/app.cpp \
  src/screen_test.cpp src/gfx/assets_loader.cpp src/gfx/gfx.cpp \
  src/model/catalog.cpp src/model/catalog_gen.cpp src/model/camera_model.cpp \
  src/ui/screens/shooting_osd.cpp src/ui/screens/menu_cascade.cpp src/ui/screens/key_config.cpp \
  src/ui/screens/playback.cpp src/ui/screens/wifi.cpp src/ui/screens/nfc.cpp src/ui/screens/menu_actions.cpp src/ui/widgets/text_input.cpp \
  src/model/menu_gen.cpp src/model/apply.cpp src/model/apply_gen.cpp

# this backend + reused a5000 data/sys
LOCAL_SRCS  := debug_channel.cpp inproc.cpp display_osd.cpp display_rigel.cpp input_createevent.cpp camera_inproc.cpp playback_inproc.cpp actions_inproc.cpp
CLOCAL_SRCS := nanojpeg.c            # baseline JPEG decoder (MIT, compiled as C)
REUSE_SRCS  := $(NATIVE)/platform/a5000/keymap.cpp $(NATIVE)/platform/a5000/sys_a5000.cpp

UICORE_OBJS := $(patsubst %.cpp,$(OBJDIR)/uicore/%.o,$(UICORE_SRCS))
LOCAL_OBJS  := $(patsubst %.cpp,$(OBJDIR)/local/%.o,$(LOCAL_SRCS))
CLOCAL_OBJS := $(patsubst %.c,$(OBJDIR)/local/%.o,$(CLOCAL_SRCS))
REUSE_OBJS  := $(OBJDIR)/reuse/keymap.o $(OBJDIR)/reuse/sys_a5000.o

.PHONY: all clean
all: $(TARGETSO)
	@echo "built $(TARGETSO)"

$(OBJDIR)/uicore/%.o: $(UICORE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OBJDIR)/local/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OBJDIR)/local/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -Os -Wall -fPIC -fno-stack-protector -U_FORTIFY_SOURCE -std=c99 -c -o $@ $<
$(OBJDIR)/reuse/keymap.o: $(NATIVE)/platform/a5000/keymap.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OBJDIR)/reuse/sys_a5000.o: $(NATIVE)/platform/a5000/sys_a5000.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGETSO): $(UICORE_OBJS) $(LOCAL_OBJS) $(CLOCAL_OBJS) $(REUSE_OBJS)
	@mkdir -p $(OUT)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf build build-debug
