# Shared toolchain/flag definitions for uicore + platform builds.
# Set TARGET=host|ndk|cross before including. Paths are relative to app/native
# (NATIVE), which each including Makefile passes as NATIVE.
#
# ARMv7-a thumb, vfpv3-d16, no NEON. -Os -fno-exceptions -fno-rtti.

TARGET ?= host
NATIVE ?= ..
ARCH   := -march=armv7-a -mthumb -mfpu=vfpv3-d16 -mfloat-abi=softfp
COMMON := -Os -Wall -fno-exceptions -fno-rtti -I$(NATIVE)/uicore/include

ifeq ($(TARGET),host)
  CXX      ?= clang++
  CXXFLAGS := $(COMMON)
  LINK     := $(CXX)
  LDFLAGS  :=
  LDLIBS   :=
  CTLLIBS  :=
  LDEND    :=
  AR       ?= ar
endif

ifeq ($(TARGET),ndk)
  PLAT     := $(NDK)/platforms/android-9/arch-arm
  CXX      := arm-linux-androideabi-g++
  GCC      := arm-linux-androideabi-gcc
  AR       := arm-linux-androideabi-ar
  CXXFLAGS := $(ARCH) $(COMMON) -fno-stack-protector --sysroot=$(PLAT) -I$(NATIVE)/sdcom -DANDROID -DA5K_SDCOM_INLINE_FALLBACK
  DEV      := $(NATIVE)/sysroot-android/lib
  LINK     := $(CXX)
  LDFLAGS  := $(ARCH) -nostdlib -Wl,--dynamic-linker=/system/bin/linker -Wl,-z,nocopyreloc \
              -L$(DEV) -Wl,-rpath-link,$(DEV) $(PLAT)/usr/lib/crtbegin_dynamic.o
  LDLIBS   := -Wl,--no-as-needed -lsdcom -losal_uipc -ldl -llog -lstdc++ -lc -lm
  CTLLIBS  := -lc -lm -ldl
  LDEND    := $(shell $(GCC) -print-libgcc-file-name) $(PLAT)/usr/lib/crtend_android.o
endif

ifeq ($(TARGET),cross)
  CXX      := arm-linux-gnueabi-g++
  AR       := arm-linux-gnueabi-ar
  CXXFLAGS := $(ARCH) $(COMMON) -fno-stack-protector -U_FORTIFY_SOURCE -I$(NATIVE)/sdcom
  SYS      := $(NATIVE)/sysroot/lib
  LINK     := $(CXX)
  LDFLAGS  := $(ARCH) -nodefaultlibs -L$(SYS) -Wl,-rpath-link,$(SYS) \
              -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,--hash-style=sysv
  LDLIBS   := -Wl,--no-as-needed -lsdcom -losal_uipc -lpthread -lstdc++ -lm -lc \
              /usr/arm-linux-gnueabi/lib/libc_nonshared.a -lgcc_s -lgcc -ldl
  CTLLIBS  := -lc /usr/arm-linux-gnueabi/lib/libc_nonshared.a -lgcc_s -lgcc -ldl -lm
  LDEND    :=
endif
