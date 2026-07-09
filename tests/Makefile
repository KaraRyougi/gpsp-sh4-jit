# Host-side tests for the cgba core and SH4 code emitter.
#
#   make -C tests          # build + run all
#   make -C tests audit    # only the sh-elf-as differential audit
#   make -C tests rtc      # Pokemon-style cartridge GPIO/RTC protocol
#
# Run from the repository root context; the test sources include the encoder via
# its repo-relative path, so the include root is the parent directory.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra
ROOT    := ..
BIN     := /tmp/cgba-sh4

ifeq ($(shell uname -s),Darwin)
RTC_GC_LDFLAGS := -Wl,-dead_strip
else
RTC_GC_LDFLAGS := -Wl,--gc-sections
endif

.PHONY: all smoke audit core thumb cond native-ldst input rtc cycle exec clean

all: smoke core thumb cond native-ldst rtc input cycle exec audit

# Execution oracle: run emitted native code in a mini SH4 interpreter against
# the C-helper semantics (closes the pixel-soak blind spot on the audio path).
exec: $(ROOT)/tests/sh4_exec_oracle.c $(ROOT)/ports/fxcg100/sh4/sh4_thumb_dp_emit.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_emit_glue.h $(ROOT)/ports/fxcg100/sh4/sh4_fastmem.h
	$(CC) $(CFLAGS) -DCGBA_SH4_ARM_LDST_NATIVE=1 -DCGBA_SH4_THUMB_LDST_NATIVE=1 \
	  -DCGBA_SH4_THUMB_BLOCK_NATIVE=1 -I$(ROOT) $(ROOT)/tests/sh4_exec_oracle.c -o $(BIN)-exec
	$(BIN)-exec

smoke: $(ROOT)/tests/sh4_codegen_smoke.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_smoke.c -o $(BIN)-smoke
	$(BIN)-smoke

# Literal-pool + guest reg[] addressing machinery (host-only, no toolchain dep).
core: $(ROOT)/tests/sh4_emit_core_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_emit_core_audit.c -o $(BIN)-core
	$(BIN)-core

# Reference Thumb -> SH4 data-processing translation (host-only).
thumb: $(ROOT)/tests/sh4_thumb_mvp_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_thumb_mvp.h \
       $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_thumb_mvp_audit.c -o $(BIN)-thumb
	$(BIN)-thumb

input: $(ROOT)/tests/fxcg100_input_maps_test.c \
       $(ROOT)/ports/fxcg100/fxcg100_keys.c \
       $(ROOT)/ports/fxcg100/fxcg100_platform.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/fxcg100_input_maps_test.c -o $(BIN)-input
	$(BIN)-input

# Production gpSP cartridge GPIO/RTC and calculator-specific ROM loaders. Dead
# section elimination keeps the otherwise monolithic gba_memory.c host-linkable.
rtc: $(ROOT)/tests/rtc_test.c $(ROOT)/vendor/gpsp/gba_memory.c \
     $(ROOT)/vendor/gpsp/gba_memory.h $(ROOT)/vendor/gpsp/gba_over.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare \
	  -ffunction-sections -fdata-sections -DROM_BUFFER_SIZE=1 \
	  -DCGBA_RTC_WALLCLOCK=1 \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/rtc_test.c $(RTC_GC_LDFLAGS) -o $(BIN)-rtc
	$(BIN)-rtc

# ARM condition-code -> SH4 T-bit emission, every cond x every NZCV (host-only).
cond: $(ROOT)/tests/sh4_cond_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_emit_glue.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_cond_audit.c -o $(BIN)-cond
	$(BIN)-cond

native-ldst: $(ROOT)/tests/sh4_native_ldst_audit.c \
	      $(ROOT)/ports/fxcg100/sh4/sh4_arm_ldst_emit.h \
	      $(ROOT)/ports/fxcg100/sh4/sh4_arm_block_emit.h \
	      $(ROOT)/ports/fxcg100/sh4/sh4_thumb_dp_emit.h \
	      $(ROOT)/ports/fxcg100/sh4/sh4_emit_glue.h \
	      $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h \
	      $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_native_ldst_audit.c -o $(BIN)-native-ldst
	$(BIN)-native-ldst

# Static GBA-cycle audit: dynarec emitted charge vs interpreter formula, per
# instruction class (host-only, no toolchain dep, no emulator).
cycle: $(ROOT)/tests/sh4_cycle_audit.c
	$(CC) $(CFLAGS) $(ROOT)/tests/sh4_cycle_audit.c -o $(BIN)-cycle
	$(BIN)-cycle

# Requires sh-elf-as + sh-elf-objcopy on PATH.
audit: $(ROOT)/tests/sh4_codegen_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_audit.c -o $(BIN)-audit
	$(BIN)-audit

clean:
	rm -f $(BIN)-smoke $(BIN)-core $(BIN)-thumb $(BIN)-cond $(BIN)-native-ldst $(BIN)-input $(BIN)-rtc $(BIN)-cycle $(BIN)-audit
