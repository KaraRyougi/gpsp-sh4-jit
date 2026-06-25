ROOT := ../..
GPSP := $(ROOT)/vendor/gpsp
BUILD := build

SH_PREFIX ?= sh-elf
CC := $(SH_PREFIX)-gcc
CXX := $(SH_PREFIX)-g++
OBJCOPY := $(SH_PREFIX)-objcopy
MKG3A ?= mkg3a

OPT ?= -O2
G3A_BASIC_NAME ?= cgba
G3A_INTERNAL_NAME ?= CGBA
G3A_VERSION ?= 00.01.0000
CGBA_FULL_GPSP ?= 0
CGBA_DEBUG_PORT ?= 0
CGBA_RUN_JIT_PROBE ?= 0
CGBA_USE_LOADER_STACK ?= 1
CGBA_RETURN_TO_LOADER ?= 1

ifeq ($(CGBA_FULL_GPSP),1)
LINKER_SCRIPT ?= linker_full.ld
else
LINKER_SCRIPT ?= linker.ld
endif

COMMON_BASE_FLAGS := -mb -m4a-nofpu $(OPT) -g \
  -fno-pie -ffreestanding -nostdlib \
  -ffunction-sections -fdata-sections -fno-builtin \
  -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -DMSB_FIRST=1 -DCGBA_FXCG100=1 -DROM_BUFFER_SIZE=1 \
  -DCGBA_FULL_GPSP=$(CGBA_FULL_GPSP) \
  -DCGBA_DEBUG_PORT=$(CGBA_DEBUG_PORT) \
  -DCGBA_RUN_JIT_PROBE=$(CGBA_RUN_JIT_PROBE) \
  -DCGBA_USE_LOADER_STACK=$(CGBA_USE_LOADER_STACK) \
  -DCGBA_RETURN_TO_LOADER=$(CGBA_RETURN_TO_LOADER) \
  -I. -I$(ROOT) -I$(GPSP) -I$(GPSP)/libretro \
  -I$(GPSP)/libretro/libretro-common/include

COMMON_FLAGS := $(COMMON_BASE_FLAGS) -include stdint.h
CFLAGS := $(COMMON_FLAGS) -std=gnu99
CXXFLAGS := $(COMMON_FLAGS) -std=gnu++11 -fno-exceptions -fno-rtti
ASFLAGS := $(COMMON_BASE_FLAGS)
LDFLAGS := -mb -m4a-nofpu -nostdlib -T $(LINKER_SCRIPT) \
  -Wl,--gc-sections -Wl,-Map=$(BUILD)/cgba.map

PORT_C := \
  fxcg100_main.c \
  fxcg100_runtime.c \
  fxcg100_video.c \
  fxcg100_input.c \
  fxcg100_keys.c \
  fxcg100_config.c \
  fxcg100_menu.c \
  jit_probe.c

ifeq ($(CGBA_FULL_GPSP),1)
PORT_C += \
  fxcg100_sound_stub.c \
  fxcg100_gpsp_stubs.c
endif

PORT_S := \
  start.S \
  sh4/sh4_stub.S

ifeq ($(CGBA_FULL_GPSP),1)
GPSP_C := \
  main.c \
  gba_memory.c \
  input.c \
  cheats.c \
  gbp.c \
  serial.c \
  serial_proto.c \
  rfu.c \
  savestate.c \
  gba_cc_lut.c

GPSP_CC := \
  cpu.cc \
  video.cc

GPSP_S := \
  bios_data.S
else
GPSP_C :=
GPSP_CC :=
GPSP_S :=
endif

OBJS := \
  $(addprefix $(BUILD)/port/,$(PORT_C:.c=.o)) \
  $(addprefix $(BUILD)/port/,$(PORT_S:.S=.o)) \
  $(addprefix $(BUILD)/gpsp/,$(GPSP_C:.c=.o)) \
  $(addprefix $(BUILD)/gpsp/,$(GPSP_CC:.cc=.o)) \
  $(addprefix $(BUILD)/gpsp/,$(GPSP_S:.S=.o))

.PHONY: all clean raw-g3a

all: $(BUILD)/cgba.g3a $(BUILD)/cgba.raw.g3a
raw-g3a: $(BUILD)/cgba.raw.g3a

$(BUILD)/cgba.elf: $(OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) -lgcc

$(BUILD)/cgba.bin: $(BUILD)/cgba.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/cgba.g3a: $(BUILD)/cgba.bin
	$(MKG3A) -n basic:"$(G3A_BASIC_NAME)" -n internal:"$(G3A_INTERNAL_NAME)" -V "$(G3A_VERSION)" $< $@

$(BUILD)/cgba.raw.g3a: $(BUILD)/cgba.bin
	( dd if=/dev/zero bs=28672 count=1 2>/dev/null; cat $< ) > $@

$(BUILD)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/port/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/gpsp/%.o: $(GPSP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gpsp/%.o: $(GPSP)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/gpsp/%.o: $(GPSP)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -Wa,-I$(GPSP) -c $< -o $@

clean:
	rm -rf $(BUILD)
