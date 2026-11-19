# Host simulator: libuicore + host adapters -> a5kui_host, a5kctl.
#   make            (PPM stub display)
#   make SDL=1      (SDL2 window; needs sdl2-config)
TARGET := host
NATIVE := ../..
include ../../uicore/common.mk

OUT    := build
OBJDIR := $(OUT)/obj
UICORE := ../../uicore/build/$(TARGET)/libuicore.a
INCL   := -I../common

ifeq ($(SDL),1)
CXXFLAGS += -DHAVE_SDL $(shell sdl2-config --cflags 2>/dev/null)
SDLLIBS  := $(shell sdl2-config --libs 2>/dev/null)
endif

SRCS := host_adapters.cpp main.cpp keymap.cpp ../common/ctl.cpp ../common/display_stub.cpp ../common/fake_camera.cpp ../common/fake_media.cpp ../common/fake_wifi.cpp ../common/fake_nfc.cpp
CXXFLAGS += -DUICORE_ASSETS_DIR='"$(abspath ../../uicore/build/assets)"'
OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SRCS)))

.PHONY: all uicore clean
all: uicore $(OUT)/a5kui_host $(OUT)/a5kctl
	@echo "built $(OUT)/a5kui_host"

uicore:
	$(MAKE) -C ../../uicore TARGET=$(TARGET)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCL) -MMD -MP -c -o $@ $<
$(OBJDIR)/%.o: ../common/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCL) -MMD -MP -c -o $@ $<

# header-dependency tracking (-MMD): rebuild .o when an included header changes
-include $(OBJS:.o=.d)

$(OUT)/a5kui_host: $(OBJS) $(UICORE)
	@mkdir -p $(OUT)
	$(LINK) $(LDFLAGS) -o $@ $(OBJS) $(UICORE) $(LDLIBS) $(SDLLIBS) $(LDEND)

$(OUT)/a5kctl: ../common/a5kctl.cpp
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS) -c -o $(OBJDIR)/a5kctl.o $<
	$(LINK) $(LDFLAGS) -o $@ $(OBJDIR)/a5kctl.o $(CTLLIBS) $(LDEND)

clean:
	rm -rf build
