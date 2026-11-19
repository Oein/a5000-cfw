# a5000 platform: libuicore + a5000 adapters -> a5kui (bionic/glibc ARM), a5kctl.
#   run inside docker: make TARGET=ndk   (bionic, default for the launcher)
#                      make TARGET=cross (glibc)
# Wrapper: ../../build.sh a5000-ndk  (handles docker).
TARGET ?= ndk
NATIVE := ../..
include ../../uicore/common.mk

OUT    := build/$(TARGET)
OBJDIR := $(OUT)/obj
UICORE := ../../uicore/build/$(TARGET)/libuicore.a
INCL   := -I../common

SRCS := main.cpp keymap.cpp display_rigel.cpp input_a5000.cpp camera_sdcom.cpp sys_a5000.cpp \
        ../common/ctl.cpp ../common/display_stub.cpp
OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SRCS)))

.PHONY: all uicore clean
all: uicore $(OUT)/a5kui $(OUT)/a5kctl
	@echo "built $(OUT)/a5kui"

uicore:
	$(MAKE) -C ../../uicore TARGET=$(TARGET)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCL) -c -o $@ $<
$(OBJDIR)/%.o: ../common/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCL) -c -o $@ $<

$(OUT)/a5kui: $(OBJS) $(UICORE)
	@mkdir -p $(OUT)
	$(LINK) $(LDFLAGS) -o $@ $(OBJS) $(UICORE) $(LDLIBS) $(LDEND)

$(OUT)/a5kctl: ../common/a5kctl.cpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -c -o $(OBJDIR)/a5kctl.o $<
	$(LINK) $(LDFLAGS) -o $@ $(OBJDIR)/a5kctl.o $(CTLLIBS) $(LDEND)

clean:
	rm -rf build
