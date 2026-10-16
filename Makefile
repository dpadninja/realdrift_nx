#---------------------------------------------------------------------------------
# REAL DRIFT -- Nintendo Switch homebrew loader (wrapper port), realdrift_nx.
# Unity 2018.4.14f1 / IL2CPP / arm64-v8a. Forked from badpiggies_nx (ChanseyIsTheBest,
# MIT), itself from the colorsheep_nx / laytonbmr_nx / vln_nx so-loader lineage.
# Ships NO game code or assets: you supply the APK + OBB from your own copy of Real Drift.
#
# Switch build (repo root, needs devkitPro):
#   docker run --rm -v ./:/project -w /project devkitpro/devkita64:latest make
# Host targets (system C compiler + uv; no devkitPro):
#   make test             all unit tests (Python + host C, tests/)
#   make tools            build-host/find_patch_sites
#   make config-example   regenerate config.example.txt from the loader's settings table
#---------------------------------------------------------------------------------
.SUFFIXES:
HOST_GOALS := test tools config-example
ifneq ($(filter $(HOST_GOALS),$(MAKECMDGOALS)),)
HOST_BUILD := build-host
.PHONY: $(HOST_GOALS)
test:
	uv run pytest -q
tools:
	uv run python -m tools.hostc tools/find_patch_sites.c $(HOST_BUILD)/find_patch_sites
config-example:
	uv run python -m tools.hostc tests/c/gen_config_example.c $(HOST_BUILD)/gen_config_example
	$(HOST_BUILD)/gen_config_example config.example.txt
else
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO in your environment. (export DEVKITPRO=/opt/devkitpro)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := realdrift_nx
APP_TITLE   := Real Drift
APP_AUTHOR  := ndrift (fork of badpiggies_nx by ChanseyIsTheBest)
APP_VERSION := 0.1.0
APP_ICON    := $(TOPDIR)/icon.jpg
export APP_TITLE APP_AUTHOR APP_VERSION APP_ICON
BUILD    := build
SOURCES  := source
INCLUDES := source

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) \
           -DCRASH_LOG_PRINTF=debugPrintf \
           $(INCLUDE) -D__SWITCH__
# The reserved region where the game .so are loaded (keep in sync with so_util.c).
CFLAGS  += -DLOAD_ADDRESS=0xC0000000
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
# mesa's display/GPU waits, timed in source/rd_gpuwait.c
LDFLAGS  += -Wl,--wrap=nwindowDequeueBuffer -Wl,--wrap=nvFenceWait -Wl,--wrap=nwindowSetSwapInterval

# mesa GLES3 + EGL + nouveau (force the GLES path); SDL2 for window/HID/audio; zlib.
LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lpng -lz -lnx -lm

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD := $(CXX)
export OFILES  := $(SFILES:.s=.o) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
NROFLAGS := --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
# main.c logs __DATE__/__TIME__ as the build stamp -- rebuild it whenever
# any other object changes, so debug.log always identifies the exact build.
main.o : $(filter-out main.o,$(OFILES))
-include $(DEPENDS)
endif
endif
