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
CXX     ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -O2
ROOT    := ..
BIN     := /tmp/cgba-sh4
comma   := ,
DEAD_STRIP := $(if $(filter Darwin,$(shell uname -s)),\
	-Wl$(comma)-dead_strip,-Wl$(comma)--gc-sections)

.PHONY: all smoke audit core litpool thumb cond native-ldst input config-version storage lcd-cleanup scale classpad fxcg-ui txt-reader rtc backup-dirty nor-world-switch rom-library overclock cycle exec jit-boundaries jit-safety jit-ram-flush memory-pc \
	swi-overlap swi-oam udiv udiv-emitter idle-signature video-opaque \
	video-blend-cache video-backdrop-shadow video-sprite-decode video-tile-row-cache video-text-fastpaths video-dirty-scanline arm-mixer m4a-skip clean

all: smoke core litpool thumb cond native-ldst rtc pacing backup-dirty nor-world-switch rom-library overclock input config-version storage lcd-cleanup scale classpad fxcg-ui txt-reader cycle exec jit-boundaries jit-safety jit-ram-flush memory-pc swi-overlap swi-oam udiv \
	udiv-emitter idle-signature video-opaque video-blend-cache \
	video-backdrop-shadow video-sprite-decode video-tile-row-cache video-text-fastpaths video-dirty-scanline arm-mixer m4a-skip audit

# Execution oracle: run emitted native code in a mini SH4 interpreter against
# the C-helper semantics (closes the pixel-soak blind spot on the audio path).
exec: $(ROOT)/tests/sh4_exec_oracle.c $(ROOT)/ports/fxcg100/sh4/sh4_thumb_dp_emit.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_emit_glue.h $(ROOT)/ports/fxcg100/sh4/sh4_fastmem.h
	$(CC) $(CFLAGS) -DCGBA_SH4_ARM_LDST_NATIVE=1 -DCGBA_SH4_THUMB_LDST_NATIVE=1 \
	  -DCGBA_SH4_THUMB_BLOCK_NATIVE=1 -DCGBA_SH4_COMPACT_FASTMEM_TUPLE=1 \
	  -I$(ROOT) $(ROOT)/tests/sh4_exec_oracle.c -o $(BIN)-exec
	$(BIN)-exec

smoke: $(ROOT)/tests/sh4_codegen_smoke.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_smoke.c -o $(BIN)-smoke
	$(BIN)-smoke

# Literal-pool + guest reg[] addressing machinery (host-only, no toolchain dep).
core: $(ROOT)/tests/sh4_emit_core_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h \
      $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_emit_core_audit.c -o $(BIN)-core
	$(BIN)-core

litpool: $(ROOT)/tests/sh4_segmented_pool_test.c \
	 $(ROOT)/ports/fxcg100/sh4/sh4_emit_glue.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_segmented_pool_test.c \
	  -o $(BIN)-litpool
	$(BIN)-litpool

# Reference Thumb -> SH4 data-processing translation (host-only).
thumb: $(ROOT)/tests/sh4_thumb_mvp_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_thumb_mvp.h \
       $(ROOT)/ports/fxcg100/sh4/sh4_emit_core.h $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_thumb_mvp_audit.c -o $(BIN)-thumb
	$(BIN)-thumb

input: $(ROOT)/tests/fxcg100_input_maps_test.c \
       $(ROOT)/ports/fxcg100/fxcg100_keys.c \
       $(ROOT)/ports/fxcg100/fxcg100_platform.h \
       $(ROOT)/tests/stubs/gint/keycodes.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/fxcg100_input_maps_test.c -o $(BIN)-input
	$(BIN)-input
	$(CC) $(CFLAGS) -DCGBA_FXCG50=1 -I$(ROOT)/tests/stubs -I$(ROOT) \
	  $(ROOT)/tests/fxcg100_input_maps_test.c -o $(BIN)-input-cg50
	$(BIN)-input-cg50

config-version: $(ROOT)/tests/fxcg100_config_version_test.c \
		$(ROOT)/ports/fxcg100/fxcg100_config.c \
		$(ROOT)/ports/fxcg100/fxcg100_platform.h
	$(CC) $(CFLAGS) -I$(ROOT) \
	  $(ROOT)/tests/fxcg100_config_version_test.c -o $(BIN)-config-version
	$(BIN)-config-version

storage: $(ROOT)/tests/fxcg100_storage_test.c $(ROOT)/ports/fxcg100/fxcg100_config.c
	$(CC) $(CFLAGS) -I$(ROOT)/tests/stubs -I$(ROOT) \
	  $(ROOT)/tests/fxcg100_storage_test.c -o $(BIN)-storage
	$(BIN)-storage
	$(CC) $(CFLAGS) -DCGBA_FXCG50=1 -I$(ROOT)/tests/stubs -I$(ROOT) \
	  $(ROOT)/tests/fxcg100_storage_test.c -o $(BIN)-storage-cg50
	$(BIN)-storage-cg50

lcd-cleanup: $(ROOT)/tests/fxcg100_lcd_cleanup_test.c \
	     $(ROOT)/ports/fxcg100/fxcg100_lcd_cleanup.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/fxcg100_lcd_cleanup_test.c \
	  -o $(BIN)-lcd-cleanup
	$(BIN)-lcd-cleanup

scale: $(ROOT)/tests/scale_test.c $(ROOT)/ports/fxcg100/fxcg100_scale.h
	$(CC) $(CFLAGS) -O2 -I$(ROOT)/ports/fxcg100 \
	  $(ROOT)/tests/scale_test.c -o $(BIN)-scale
	$(BIN)-scale

# ClassPad II: the real presenter against a simulated R61523 and
# stub gint (layouts, rotation, scaler taps, touch keys and their highlight,
# and the touch interface: library, pause menu, settings, key mapping),
# plus the lookup that finds the RAM-resident core inside the .hh3, and the
# File_GetAddr symbol lookup and code check.
#   make -C tests classpad CLASSPAD_RENDER_DIR=/some/dir   also writes PPMs
#   make -C tests classpad CLASSPAD_OS_DUMP=nor.bin        also checks a dump
classpad: $(ROOT)/tests/classpad_render_test.c $(ROOT)/tests/classpad_elf_test.c \
	  $(ROOT)/tests/classpad_hhk_test.c $(ROOT)/ports/fxcg100/classpad_hhk.h \
	  $(ROOT)/ports/fxcg100/gint-gpsp/src/classpad_platform.c \
	  $(ROOT)/ports/fxcg100/classpad_layout.h \
	  $(ROOT)/ports/fxcg100/classpad_gfx.h \
	  $(ROOT)/ports/fxcg100/classpad_theme.h \
	  $(ROOT)/ports/fxcg100/classpad_clock.h \
	  $(ROOT)/ports/fxcg100/classpad_font.h \
	  $(ROOT)/ports/fxcg100/classpad_ui.c \
	  $(ROOT)/ports/fxcg100/txt_reader.c \
	  $(ROOT)/ports/fxcg100/txt_reader.h \
	  $(ROOT)/ports/fxcg100/txt_font_terminus.h \
	  $(ROOT)/ports/fxcg100/classpad_scale.h \
	  $(ROOT)/ports/fxcg100/classpad_elf.h \
	  $(ROOT)/ports/fxcg100/classpad_heap.h \
	  $(ROOT)/tests/classpad_heap_test.c \
	  $(ROOT)/ports/fxcg100/classpad_sync.h \
	  $(ROOT)/tests/classpad_sync_test.c \
	  $(ROOT)/ports/fxcg100/fxcg100_keys.c
	$(CC) $(CFLAGS) -O2 -DCGBA_HOST_TEST=1 -DCGBA_CLASSPAD=1 -DCGBA_FXCG50=1 \
	  -I$(ROOT)/tests/stubs -I$(ROOT)/ports/fxcg100 -I$(ROOT) \
	  $(ROOT)/tests/classpad_render_test.c -o $(BIN)-classpad-render
	$(BIN)-classpad-render $(CLASSPAD_RENDER_DIR)
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/classpad_elf_test.c \
	  -o $(BIN)-classpad-elf
	$(BIN)-classpad-elf
	$(CC) $(CFLAGS) -O2 -I$(ROOT) $(ROOT)/tests/classpad_heap_test.c \
	  -o $(BIN)-classpad-heap
	$(BIN)-classpad-heap
	$(CC) $(CFLAGS) -O2 -I$(ROOT) $(ROOT)/tests/classpad_sync_test.c \
	  -o $(BIN)-classpad-sync
	$(BIN)-classpad-sync
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/classpad_hhk_test.c \
	  -o $(BIN)-classpad-hhk
	$(BIN)-classpad-hhk $(CLASSPAD_OS_DUMP)
# The built-in Ptune 4 F5 profile must not downclock a faster launcher clock.
overclock: $(ROOT)/tests/overclock_policy_test.c \
	   $(ROOT)/ports/fxcg100/gint-gpsp/src/overclock.c \
	   $(ROOT)/tests/stubs/gint/clock.h
	$(CC) $(CFLAGS) -I$(ROOT)/tests/stubs -I$(ROOT) \
	  $(ROOT)/tests/overclock_policy_test.c -o $(BIN)-overclock
	$(BIN)-overclock

# fx-CG50 / fx-CG100 keypad interface (cg_ui.c) on a simulated panel: what
# each key does, that partial pushes leave nothing behind, baselines.
#   make -C tests fxcg-ui FXCG_UI_DIR=/some/dir   also writes PPMs
fxcg-ui: $(ROOT)/tests/fxcg_ui_test.c $(ROOT)/ports/fxcg100/cg_ui.c \
	  $(ROOT)/ports/fxcg100/cg_font.h $(ROOT)/ports/fxcg100/classpad_gfx.h \
	  $(ROOT)/ports/fxcg100/classpad_theme.h \
	  $(ROOT)/ports/fxcg100/txt_reader.c \
	  $(ROOT)/ports/fxcg100/txt_reader.h \
	  $(ROOT)/ports/fxcg100/txt_font_terminus.h \
	  $(ROOT)/ports/fxcg100/fxcg100_keys.c \
	  $(ROOT)/ports/fxcg100/fxcg100_platform.h
	$(CC) $(CFLAGS) -O2 -I$(ROOT)/tests/stubs_cgui -I$(ROOT)/ports/fxcg100 \
	  -I$(ROOT) $(ROOT)/tests/fxcg_ui_test.c -o $(BIN)-fxcg-ui
	$(BIN)-fxcg-ui $(FXCG_UI_DIR)
	$(CC) $(CFLAGS) -O2 -DCGBA_FXCG50=1 -I$(ROOT)/tests/stubs_cgui \
	  -I$(ROOT)/tests/stubs -I$(ROOT)/ports/fxcg100 -I$(ROOT) \
	  $(ROOT)/tests/fxcg_ui_test.c -o $(BIN)-fxcg-ui-cg50
	$(BIN)-fxcg-ui-cg50 $(FXCG_UI_DIR)

# Platform-independent in-game TXT reader engine (directory browsing, Direct
# NOR + BFile fallback streaming, Wrap On/Off layout, 1-line overlap paging,
# Jump % + undo, dynamic 0..32 bookmarks, Terminus 6x12/8x14 bitmap rendering).
txt-reader: $(ROOT)/tests/txt_reader_test.c \
	    $(ROOT)/ports/fxcg100/txt_reader.c \
	    $(ROOT)/ports/fxcg100/txt_reader.h \
	    $(ROOT)/ports/fxcg100/txt_font_terminus.h \
	    $(ROOT)/ports/fxcg100/classpad_gfx.h \
	    $(ROOT)/ports/fxcg100/fxcg100_platform.h
	$(CC) $(CFLAGS) -O2 -I$(ROOT)/ports/fxcg100 -I$(ROOT) \
	  $(ROOT)/tests/txt_reader_test.c -o $(BIN)-txt-reader
	$(BIN)-txt-reader

# The speed limiter and automatic frame skipping, on a virtual clock.
pacing: $(ROOT)/tests/frame_pacing_test.c \
	  $(ROOT)/ports/fxcg100/gint-gpsp/src/frame_pacing.c \
	  $(ROOT)/ports/fxcg100/gint-gpsp/src/frame_pacing.h
	$(CC) $(CFLAGS) -O2 -DCGBA_CLASSPAD=1 -I$(ROOT)/tests/stubs_pacing -I$(ROOT) \
	  $(ROOT)/tests/frame_pacing_test.c \
	  $(ROOT)/ports/fxcg100/gint-gpsp/src/frame_pacing.c -o $(BIN)-pacing
	$(BIN)-pacing

# Production gpSP cartridge GPIO/RTC and calculator-specific ROM loaders. Dead
# section elimination keeps the otherwise monolithic gba_memory.c host-linkable.
rtc: $(ROOT)/tests/rtc_test.c $(ROOT)/vendor/gpsp/gba_memory.c \
     $(ROOT)/vendor/gpsp/gba_memory.h $(ROOT)/vendor/gpsp/gba_over.h
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare \
	  -ffunction-sections -fdata-sections -DROM_BUFFER_SIZE=1 \
	  -DCGBA_RTC_WALLCLOCK=1 \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/rtc_test.c $(DEAD_STRIP) -o $(BIN)-rtc
	$(BIN)-rtc
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare \
	  -ffunction-sections -fdata-sections -DROM_BUFFER_SIZE=1 \
	  -DCGBA_RTC_WALLCLOCK=1 -DCGBA_FXCG100=1 -DCGBA_HOST_TEST=1 \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/rtc_test.c $(DEAD_STRIP) -o $(BIN)-rtc-fxcg100
	$(BIN)-rtc-fxcg100

backup-dirty: $(ROOT)/tests/backup_dirty_test.c $(ROOT)/vendor/gpsp/gba_memory.c
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare \
	  -ffunction-sections -fdata-sections -DROM_BUFFER_SIZE=1 \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/backup_dirty_test.c $(DEAD_STRIP) -o $(BIN)-backup-dirty
	$(BIN)-backup-dirty

# A 32 MiB ROM has 8192 Fugue blocks. Resolve all of them during one OS-world
# visit so overclock restore/reapply does not accumulate RTC tick loss.
nor-world-switch: $(ROOT)/tests/nor_rom_world_switch_test.c \
		  $(ROOT)/ports/fxcg100/gint-gpsp/src/nor_rom.c \
		  $(ROOT)/tests/stubs/gint/bfile.h \
		  $(ROOT)/tests/stubs/gint/gint.h
	$(CC) $(CFLAGS) -Wno-unused-function -ffunction-sections -fdata-sections \
	  -I$(ROOT)/tests/stubs -I$(ROOT)/ports/fxcg100 -I$(ROOT) \
	  $(ROOT)/tests/nor_rom_world_switch_test.c $(DEAD_STRIP) \
	  -o $(BIN)-nor-world-switch
	$(BIN)-nor-world-switch

rom-library: $(ROOT)/tests/rom_library_test.c \
	     $(ROOT)/ports/fxcg100/gint-gpsp/src/nor_rom.c \
	     $(ROOT)/ports/fxcg100/gint-gpsp/src/nor_rom.h \
	     $(ROOT)/ports/fxcg100/gint-gpsp/src/rom_meta.h \
	     $(ROOT)/ports/fxcg100/fxcg100_rom_sort.h \
	     $(ROOT)/tests/stubs/gint/bfile.h \
	     $(ROOT)/tests/stubs/gint/gint.h
	$(CC) $(CFLAGS) -Wno-unused-function -ffunction-sections -fdata-sections \
	  -I$(ROOT)/tests/stubs -I$(ROOT)/ports/fxcg100 -I$(ROOT) \
	  $(ROOT)/tests/rom_library_test.c $(DEAD_STRIP) \
	  -o $(BIN)-rom-library
	$(BIN)-rom-library


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

# Real WAITCNT mutation, savestate table restoration, and scheduled-DMA SMC
# invalidation through the production memory/scheduler implementations.
jit-boundaries: $(ROOT)/tests/jit_boundary_regression.c \
		$(ROOT)/vendor/gpsp/gba_memory.c $(ROOT)/vendor/gpsp/main.c \
		$(ROOT)/vendor/gpsp/savestate.c
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-sign-compare \
	  -Wno-unused-function -ffunction-sections -fdata-sections \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/jit_boundary_regression.c $(DEAD_STRIP) \
	  -o $(BIN)-jit-boundaries
	$(BIN)-jit-boundaries

jit-safety: $(ROOT)/tests/sh4_jit_safety_test.c \
	$(ROOT)/vendor/gpsp/sh4/sh4_jit_safety.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_jit_safety_test.c \
	  -o $(BIN)-jit-safety
	$(BIN)-jit-safety

# Exercise the real cpu_threaded.c RAM flush, including the valid translated
# block/tag case at guest offset zero that a predicate-only test cannot bind.
jit-ram-flush: $(ROOT)/tests/sh4_ram_flush_test.c \
	       $(ROOT)/vendor/gpsp/cpu_threaded.c \
	       $(ROOT)/vendor/gpsp/sh4/sh4_jit_safety.h
	$(CC) $(CFLAGS) -std=gnu11 -Wno-unused-parameter -Wno-sign-compare \
	  -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
	  -Wno-unused-label -ffunction-sections -fdata-sections \
	  -I$(ROOT) -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/sh4_ram_flush_test.c $(DEAD_STRIP) \
	  -o $(BIN)-jit-ram-flush
	$(BIN)-jit-ram-flush

# Production SH4 memory fallbacks must publish the interpreter-form PC before
# gpSP's BIOS-protection and open-bus paths inspect reg[REG_PC].
memory-pc: $(ROOT)/tests/sh4_memory_pc_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_interp_helpers.c
	$(CC) $(CFLAGS) -Wno-unused-function \
	  -ffunction-sections -fdata-sections -I$(ROOT) \
	  -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/sh4_memory_pc_test.c \
	  $(ROOT)/ports/fxcg100/sh4/sh4_interp_helpers.c \
	  $(DEAD_STRIP) -o $(BIN)-memory-pc
	$(BIN)-memory-pc

swi-overlap: $(ROOT)/tests/sh4_swi_overlap_test.c \
	      $(ROOT)/ports/fxcg100/sh4/sh4_swi_overlap.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_swi_overlap_test.c \
	  -o $(BIN)-swi-overlap
	$(BIN)-swi-overlap

swi-oam: $(ROOT)/tests/sh4_swi_oam_test.c \
	 $(ROOT)/ports/fxcg100/sh4/sh4_swi_oam.h
	$(CC) $(CFLAGS) -O2 -I$(ROOT) $(ROOT)/tests/sh4_swi_oam_test.c \
	  -o $(BIN)-swi-oam
	$(BIN)-swi-oam

udiv: $(ROOT)/tests/sh4_udiv_fastpath_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_thumb_udiv.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_udiv_fastpath_test.c \
	  -o $(BIN)-udiv
	$(BIN)-udiv

udiv-emitter: $(ROOT)/tests/sh4_udiv_emitter_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_thumb_udiv.h \
	$(ROOT)/ports/fxcg100/sh4/sh4_interp_helpers.c \
	$(ROOT)/vendor/gpsp/sh4/sh4_emit.h
	$(CC) $(CFLAGS) -Wno-unused-function \
	  -DCGBA_SH4_THUMB_UDIV_FASTPATH=1 \
	  -ffunction-sections -fdata-sections -I$(ROOT) \
	  -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(ROOT)/tests/sh4_udiv_emitter_test.c \
	  $(ROOT)/ports/fxcg100/sh4/sh4_interp_helpers.c \
	  $(DEAD_STRIP) -o $(BIN)-udiv-emitter
	$(BIN)-udiv-emitter

arm-mixer: $(ROOT)/tests/sh4_arm_mixer_fastpath_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_arm_mixer.h
	$(CC) $(CFLAGS) -O2 -I$(ROOT) \
	  $(ROOT)/tests/sh4_arm_mixer_fastpath_test.c -o $(BIN)-arm-mixer
	$(BIN)-arm-mixer

idle-signature: $(ROOT)/tests/sh4_idle_signature_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_idle_signature.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_idle_signature_test.c \
	  -o $(BIN)-idle-signature
	$(BIN)-idle-signature

video-opaque: $(ROOT)/tests/video_opaque_tile_test.cc \
	$(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_opaque_tile_test.cc \
	  -o $(BIN)-video-opaque
	$(BIN)-video-opaque

video-blend-cache: $(ROOT)/tests/video_blend_palette_cache_test.cc \
	$(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_blend_palette_cache_test.cc \
	  -o $(BIN)-video-blend-cache
	$(BIN)-video-blend-cache

video-backdrop-shadow: $(ROOT)/tests/video_backdrop_shadow_test.cc \
	$(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_backdrop_shadow_test.cc \
	  -o $(BIN)-video-backdrop-shadow
	$(BIN)-video-backdrop-shadow

video-sprite-decode: $(ROOT)/tests/video_sprite_decode_test.cc $(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_sprite_decode_test.cc \
	  -o $(BIN)-video-sprite-decode
	$(BIN)-video-sprite-decode

video-tile-row-cache: $(ROOT)/tests/video_tile_row_cache_test.cc $(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_tile_row_cache_test.cc \
	  -o $(BIN)-video-tile-row-cache
	$(BIN)-video-tile-row-cache

video-dirty-scanline: $(ROOT)/tests/video_dirty_scanline_test.cc $(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_dirty_scanline_test.cc \
	  -o $(BIN)-video-dirty-scanline
	$(BIN)-video-dirty-scanline

video-text-fastpaths: $(ROOT)/tests/video_text_fastpaths_test.cc $(ROOT)/vendor/gpsp/video.cc
	$(CXX) $(CXXFLAGS) -ffunction-sections -fdata-sections \
	  -I$(ROOT)/vendor/gpsp -I$(ROOT)/vendor/gpsp/libretro \
	  -I$(ROOT)/vendor/gpsp/libretro/libretro-common/include \
	  $(DEAD_STRIP) $(ROOT)/tests/video_text_fastpaths_test.cc \
	  -o $(BIN)-video-text-fastpaths
	$(BIN)-video-text-fastpaths

m4a-skip: $(ROOT)/tests/sh4_m4a_skip_test.c \
	$(ROOT)/ports/fxcg100/sh4/sh4_m4a_skip.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_m4a_skip_test.c \
	  -o $(BIN)-m4a-skip
	$(BIN)-m4a-skip

# Requires sh-elf-as + sh-elf-objcopy on PATH.
audit: $(ROOT)/tests/sh4_codegen_audit.c $(ROOT)/ports/fxcg100/sh4/sh4_codegen.h
	$(CC) $(CFLAGS) -I$(ROOT) $(ROOT)/tests/sh4_codegen_audit.c -o $(BIN)-audit
	$(BIN)-audit

clean:
	rm -f $(BIN)-smoke $(BIN)-core $(BIN)-thumb $(BIN)-cond $(BIN)-native-ldst \
	  $(BIN)-input $(BIN)-input-cg50 $(BIN)-config-version \
	  $(BIN)-rtc $(BIN)-rtc-fxcg100 $(BIN)-backup-dirty \
	  $(BIN)-storage $(BIN)-storage-cg50 \
	  $(BIN)-nor-world-switch $(BIN)-overclock $(BIN)-scale $(BIN)-cycle $(BIN)-exec \
	  $(BIN)-jit-boundaries $(BIN)-jit-safety $(BIN)-jit-ram-flush \
	  $(BIN)-memory-pc $(BIN)-swi-overlap $(BIN)-udiv \
	  $(BIN)-swi-oam $(BIN)-udiv-emitter $(BIN)-idle-signature \
	  $(BIN)-video-opaque $(BIN)-video-blend-cache \
	  $(BIN)-video-backdrop-shadow \
	  $(BIN)-video-sprite-decode $(BIN)-video-tile-row-cache $(BIN)-arm-mixer $(BIN)-audit
