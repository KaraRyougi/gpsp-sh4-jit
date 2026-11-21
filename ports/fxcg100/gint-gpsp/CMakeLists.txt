cmake_minimum_required(VERSION 3.15)
project(cgba_gint_gpsp C CXX ASM)

if(NOT "${FXSDK_PLATFORM_LONG}" STREQUAL "fxCP")
  include(GenerateG3A)
endif()
find_package(Gint 2.9 REQUIRED)

set(CGBA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(FXCG100 "${CGBA_ROOT}/ports/fxcg100")
set(GPSP "${CGBA_ROOT}/vendor/gpsp")
# Target model: fx-CG100 (default) or fx-CG50 / Graph 90+E. Both are SH7305 with
# 8 MiB RAM at 0x8c000000, so they share the gpSP core, the high-RAM arena, and
# the JIT; they differ in keyboard codes, storage backing, and the ROM source
# (CG100 memory-maps NOR; CG50 streams the .gba via BFile).
option(CGBA_FXCG50 "Build for fx-CG50 / Graph 90+E instead of fx-CG100" OFF)
# ClassPad II under HollyHock-3 (configure with the FXCP toolchain).
# Same SH7305, but 16 MiB of RAM that belongs to the OS heap, a 320x528
# touchscreen, and a POSIX-like file API. The core is configured
# like the fx-CG50 one (gint key codes, RAM-resident hot code), so this option
# implies the CGBA_FXCG50 define and overrides the platform layer.
option(CGBA_CLASSPAD "Build for ClassPad II (HollyHock-3 .hh3)" OFF)
if(CGBA_CLASSPAD)
  if(NOT "${FXSDK_PLATFORM_LONG}" STREQUAL "fxCP")
    message(FATAL_ERROR "CGBA_CLASSPAD requires the fxSDK FXCP toolchain file")
  endif()
  set(CGBA_FXCG50 ON)
  set(CGBA_GPSP_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/classpad_gpsp.ld")
elseif(CGBA_FXCG50)
  set(CGBA_GPSP_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/fxcg50_gpsp.ld")
else()
  set(CGBA_GPSP_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/fxcg100_gpsp.ld")
endif()

set_target_properties(Gint::Gint PROPERTIES
  INTERFACE_LINK_OPTIONS "-T;${CGBA_GPSP_LINKER}"
  INTERFACE_LINK_DEPENDS "${CGBA_GPSP_LINKER}"
)

set(PORT_SOURCES
  src/main.c
  src/frame_overlay.c
  src/nor_rom.c
  src/nor_filestream.c
  src/overclock.c
  src/gpsp_runner.c
  src/frame_pacing.c
  src/crash_panic.c
  ${FXCG100}/fxcg100_keys.c
  ${FXCG100}/fxcg100_config.c
  ${FXCG100}/txt_reader.c
  ${FXCG100}/fxcg100_gpsp_stubs.c
  ${FXCG100}/fxcg100_sound_stub.c
)

set(GPSP_C_SOURCES
  ${GPSP}/main.c
  ${GPSP}/gba_memory.c
  ${GPSP}/input.c
  ${GPSP}/cheats.c
  ${GPSP}/gbp.c
  ${GPSP}/serial.c
  ${GPSP}/serial_proto.c
  ${GPSP}/rfu.c
  ${GPSP}/savestate.c
  # gba_cc_lut.c (color-correction table) omitted: unused in this build and
  # ~64 KB of .rodata. --gc-sections already strips it; not compiling it too.
)

# SH4 dynarec bring-up (opt-in). Adds the threaded recompiler, its SH4 host
# emitter, the entry/exit trampoline, the executable code-cache placement and
# the C instruction helpers. Default OFF so the interpreter build stays the
# conservative shipping target; turn CGBA_DYNAREC on for hardware JIT artifacts.
option(CGBA_DYNAREC "Build the SH4 dynamic recompiler (bring-up)" OFF)
option(CGBA_SH4_ARM_LDST_NATIVE
  "Enable SH4 native ARM single load fast path" ON)
option(CGBA_SH4_THUMB_LDST_NATIVE
  "Enable SH4 native Thumb RAM load fast path" ON)
option(CGBA_SH4_THUMB_BLOCK_NATIVE
  "Enable SH4 native Thumb RAM block-load fast path" ON)
option(CGBA_SH4_SELECTIVE_R0_SYNC
  "Synchronize pinned r0 only when a fastmem transfer uses it" ON)
option(CGBA_SH4_REG_FORWARD "Adjacent ALU register forwarding" ON)
option(CGBA_SH4_BLOCK_PROFILE
  "Emit block-entry counters in profiling/headless builds" ON)
option(CGBA_SH4_PIN_GUEST_R0
  "Keep guest ARM r0 resident in callee-saved SH-4 R11" ON)
option(CGBA_SH4_PIN_GUEST_R1
  "Keep guest ARM r1 resident in callee-saved SH-4 R12" ON)
option(CGBA_SH4_FASTMEM_ILRAM
  "Pack hottest resident fastmem routines into free zero-wait-state ILRAM" ON)
option(CGBA_SH4_LAZY_CHAIN
  "Avoid spurious update_gba on unpatched exits and lazily chain ROM branches" ON)
option(CGBA_SH4_KNOWN_TARGET_CACHE
  "Cache known ARM and Thumb indirect targets before the ROM hash walk" ON)
option(CGBA_SH4_SEGMENTED_LITERAL_POOL
  "Deduplicate large SH-4 constants in bounded per-block literal segments" ON)
option(CGBA_SH4_COMPACT_FASTMEM_TUPLE
  "Pack single-fastmem params with a 16-bit cycle field to shrink JIT sites" ON)
option(CGBA_SH4_FASTMEM_OOL_DESC
  "Place non-WB fastmem slow descriptors out-of-line so fast hits return via RTS" ON)
option(CGBA_SH4_INTERP_STATS
  "Count interpreted guest instructions by region (diag; ~4.5% overhead)" OFF)
option(CGBA_SH4_DIAG_COUNTERS
  "Per-slice/per-helper diagnostic counters in headless builds (~0.5%)" OFF)
option(CGBA_SAVE_TIMING
  "Show how long each step of a savestate save took (pack, write, remap)" OFF)
option(CGBA_KEY_TEST
  "Show the raw keyboard matrix (KEYSC) and menu key bits in the menu footer" OFF)
option(CGBA_DEBUG_MENU
  "Show the in-menu DEBUG INFO submenu + F:/H frame/hash overlay (dev; hidden in release)" OFF)
option(CGBA_SH4_SWI_HLE_VERIFY
  "Diag: predict faithful SWI HLE results, run the interpreted BIOS, compare" OFF)
option(CGBA_SH4_SWI_FORWARD_OVERLAP
  "Faithful HLE for increasing-address overlapping CpuSet/FastSet" ON)
option(CGBA_SH4_SWI_OAM_BULK
  "Faithful bulk path for CpuSet halfword copies into OAM" ON)
option(CGBA_SH4_THUMB_UDIV_FASTPATH
  "Exact fast path for the Zelda/libgcc Thumb unsigned divide suffix" ON)
option(CGBA_SH4_ARM_MIXER_FASTPATH
  "Exact fast path for the Zelda/m4a ARM four-sample mixer loop" ON)
option(CGBA_SH4_M4A_MIX_SKIP
  "Skip discarded PCM arithmetic and buffer access in the signature-matched m4a mixer (only valid with the no-op sound backend)" ON)
option(CGBA_SH4_M4A_LOOP_FOLD
  "Fold whole iterations of the elided m4a reverb/mix loops in C (needs CGBA_SH4_M4A_MIX_SKIP)" ON)
set(CGBA_SH4_HEAT_QUIET_FRAMES "180" CACHE STRING
  "Cold-gate: frames after a ROM flush before heating switches to the fast increment")
set(CGBA_SH4_HEAT_SLOW "1" CACHE STRING
  "Cold-gate heat increment within the post-flush window")
set(CGBA_SH4_HEAT_FLUSH_LEAK "4" CACHE STRING
  "Cold-gate hot-table decay subtracted per ROM flush (leaky bucket)")
option(CGBA_SH4_ARM_DEAD_FLAGS
  "ARM dead-flag elimination (OFF forces all-flags-live; A/B diagnostic)" ON)
set(CGBA_SH4_HOT_THRESHOLD "8" CACHE STRING
  "Cold-code gate: translate ROM blocks after N dispatches (1-255; 0 = gate off)")
set(CGBA_SH4_COLD_CHUNK_CYCLES "512" CACHE STRING
  "Cold-code gate: max GBA cycles to interpret per cold fallback (0 = whole scheduler budget)")
set(CGBA_SH4_RAM_SCAN_CAP_INITIAL "1024" CACHE STRING
  "Initial maximum guest instructions per translated RAM block (32-1024; may halve after an overflow)")
set(CGBA_SH4_ROM_SCAN_CAP_INITIAL "1024" CACHE STRING
  "Initial maximum guest instructions per translated ROM block (32-1024; may halve after an overflow)")
option(CGBA_SH4_EXACT_CYCLE_BOUNDARIES
  "Headless/debug: check the SH4 dynarec cycle budget at each translated instruction boundary" OFF)
option(CGBA_SH4_DIFF_HARNESS
  "Emulator-only interp/JIT differential harness (uses 492 KiB beyond the fx-CG100 hardware-safe arena)" OFF)
option(CGBA_SH4_DIFF_DUMP_OPS
  "Headless/debug: dump block-diff op/cycle diagnostics" OFF)
option(CGBA_SH4_PATCH_RESYNC
  "Re-sync each unpublished instruction patch before final block publication" OFF)
option(CGBA_SH4_PROFILE_COUNTERS
  "Hardware/debug: count SH4 JIT translations, cache flushes, and helper calls for the ON-menu debug page" OFF)
option(CGBA_FXCG100_STORAGE
  "Enable gint_world_switch-backed fx-CG100 BFile storage for ROM/config files" ON)
option(CGBA_DEFAULT_OVERCLOCK
  "Apply the Ptune 4 F5 profile while gpSP is running" ON)
# MPM 1.1+ on fx-CG100 relocates to 0x8c7f0000 and places URAM in OS AppMemory
# (0x8c160000), loading gpSP's ROM near 0x8c740000 and reporting [0x8c200000,
# romAddress) via loadinfo (_ld1). That opens ~5.25 MiB of contiguous high RAM,
# allowing a 2.25 MiB (2304 KiB) ROM translation cache and 13-bit (32 KiB)
# branch hash (~0x8c6dd700 arena end, ~394 KiB below romAddress) while main.c
# checks the exact _ld1 ceiling at runtime. fx-CG50 keeps its 1 MiB ROM cache
# so its RAM-resident hottext stays within the conservative 0x8c6f0000 cap.
if(CGBA_FXCG50 AND NOT CGBA_CLASSPAD)
  set(_CGBA_DEFAULT_ROM_CACHE "1048576")
  set(_CGBA_DEFAULT_ROM_HASH_BITS "12")
else()
  set(_CGBA_DEFAULT_ROM_CACHE "2359296")
  set(_CGBA_DEFAULT_ROM_HASH_BITS "13")
endif()
set(CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE "${_CGBA_DEFAULT_ROM_CACHE}" CACHE STRING
  "SH4 dynarec ROM translation cache size in bytes")
set(CGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE "524288" CACHE STRING
  "SH4 dynarec RAM translation cache size in bytes")
set(CGBA_GPSP_ROM_BRANCH_HASH_BITS "${_CGBA_DEFAULT_ROM_HASH_BITS}" CACHE STRING
  "SH4 dynarec ROM branch-hash = 2^N u32 entries (12 = 16 KiB, 13 = 32 KiB)")
if(CGBA_CLASSPAD)
  # The ClassPad II arena is in the OS heap and not under the fx-CG ceiling
  # above, so the ROM cache is 1920 KiB (1.875 MiB) there: Pokemon Emerald was
  # seen to fill the 1 MiB one 11 times in a session, while leaving ~78 KiB
  # headroom in the 5.5 MiB arena for .cgba.core (incl. main.c + Mode 1/Phase 3
  # ASM) and .cgba.highbss. The hash table is doubled to keep chains short.
  set(CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE 1966080)
  set(CGBA_GPSP_ROM_BRANCH_HASH_BITS 13)
endif()
if(CGBA_DYNAREC)
  list(APPEND GPSP_C_SOURCES
    ${GPSP}/cpu_threaded.c
    ${GPSP}/memmap.c
    ${FXCG100}/sh4/sh4_interp_helpers.c
    ${FXCG100}/sh4/sh4_fastmem.c
    ${FXCG100}/sh4/sh4_dynarec_state.c
    ${FXCG100}/sh4/sh4_diff_harness.c
  )
endif()

set(GPSP_CXX_SOURCES
  ${GPSP}/cpu.cc
  ${GPSP}/video.cc
)

set(GPSP_ASM_SOURCES
  ${GPSP}/bios_data.S
  ${FXCG100}/sh4/sh4_video_4bpp.S
  ${FXCG100}/sh4/sh4_scale.S
)
if(CGBA_DYNAREC)
  list(APPEND GPSP_ASM_SOURCES ${GPSP}/sh4/sh4_stub.S)
endif()

add_executable(cgba-gint-gpsp
  ${PORT_SOURCES}
  ${GPSP_C_SOURCES}
  ${GPSP_CXX_SOURCES}
  ${GPSP_ASM_SOURCES}
)

target_include_directories(cgba-gint-gpsp PRIVATE
  ${FXCG100}
  ${CGBA_ROOT}
  ${GPSP}
  ${GPSP}/libretro
  ${GPSP}/libretro/libretro-common/include
)

target_compile_definitions(cgba-gint-gpsp PRIVATE
  MSB_FIRST=1
  CGBA_FULL_GPSP=1
  CGBA_RTC_WALLCLOCK=1
  ROM_BUFFER_SIZE=1
  CGBA_NO_MEMORY_ROM=1
  dma_transfer=gpsp_dma_transfer
)
option(CGBA_SILENT_TIMERS "Batch timer overflows for the sound-stub calculator port" ON)
option(CGBA_BULK_DMA "Bulk-copy contiguous ordinary-memory DMA spans" ON)
option(CGBA_VIDEO_SPRITE_DECODE_CACHE "Reuse sprite attributes until OAM changes" ON)
option(CGBA_VIDEO_TEXT_FAST4BPP
  "Fast 4-bpp FULLCOLOR text BG kernel (SH-4 asm + pair LUT + empty-run skip)" ON)
option(CGBA_VIDEO_DIRTY_SCANLINE
  "Skip unchanged scanlines, OAM reorders, and unchanged LCD blits" ON)
target_compile_definitions(cgba-gint-gpsp PRIVATE
  CGBA_VIDEO_SPRITE_DECODE_CACHE=$<BOOL:${CGBA_VIDEO_SPRITE_DECODE_CACHE}>
  CGBA_VIDEO_TEXT_FAST4BPP=$<BOOL:${CGBA_VIDEO_TEXT_FAST4BPP}>
  CGBA_VIDEO_DIRTY_SCANLINE=$<BOOL:${CGBA_VIDEO_DIRTY_SCANLINE}>
  CGBA_BULK_DMA=$<BOOL:${CGBA_BULK_DMA}>
  CGBA_SILENT_TIMERS=$<BOOL:${CGBA_SILENT_TIMERS}>)
if(CGBA_CLASSPAD)
  # gint uses the OS heap on this model (VRAM backup, the arena itself), so
  # the no_osheap.c stubs must not be linked.
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_CLASSPAD=1 CGBA_FXCG50=1)
  # The ClassPad has its own touch interface in place of the fx-CG menu.
  target_sources(cgba-gint-gpsp PRIVATE
    src/classpad_platform.c
    src/classpad_loader.c
    ${FXCG100}/classpad_ui.c)
else()
  target_sources(cgba-gint-gpsp PRIVATE
    src/gint_platform.c
    src/no_osheap.c
    ${FXCG100}/cg_ui.c)  # keypad interface; fxcg100_menu.c is the bare-metal build's
  if(CGBA_FXCG50)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG50=1)
    target_sources(cgba-gint-gpsp PRIVATE
      src/cg50_syscall.S
      src/hottext.c)  # BFile trampoline + RAM-resident native-code loader
  else()
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG100=1)
  endif()
endif()
if(CGBA_FXCG100_STORAGE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG100_STORAGE=1)
endif()
# The Ptune profile is tuned for the fx-CG bus and SDRAM; it is not applied on
# the ClassPad II, whose memory timings have not been characterised.
if(CGBA_DEFAULT_OVERCLOCK AND NOT CGBA_CLASSPAD)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_DEFAULT_OVERCLOCK=1)
endif()
if(CGBA_SAVE_TIMING)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SAVE_TIMING=1)
endif()
if(CGBA_KEY_TEST)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_KEY_TEST=1)
endif()
if(CGBA_DEBUG_MENU)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_DEBUG_MENU=1)
endif()

if(CGBA_DYNAREC)
  # SH4_ARCH selects the SH4 host emitter in cpu_threaded.c. The code caches live
  # in the executable high-RAM arena and are tunable for emulator soaks.
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_DYNAREC=1
    SH4_ARCH=1
    CGBA_SH4_SELECTIVE_R0_SYNC=$<BOOL:${CGBA_SH4_SELECTIVE_R0_SYNC}>
    CGBA_SH4_REG_FORWARD=$<BOOL:${CGBA_SH4_REG_FORWARD}>
    CGBA_SH4_BLOCK_PROFILE=$<BOOL:${CGBA_SH4_BLOCK_PROFILE}>
    CGBA_SH4_PIN_GUEST_R0=$<BOOL:${CGBA_SH4_PIN_GUEST_R0}>
    CGBA_SH4_PIN_GUEST_R1=$<BOOL:${CGBA_SH4_PIN_GUEST_R1}>
    CGBA_SH4_FASTMEM_ILRAM=$<BOOL:${CGBA_SH4_FASTMEM_ILRAM}>
    CGBA_SH4_LAZY_CHAIN=$<BOOL:${CGBA_SH4_LAZY_CHAIN}>
    CGBA_SH4_KNOWN_TARGET_CACHE=$<BOOL:${CGBA_SH4_KNOWN_TARGET_CACHE}>
    CGBA_SH4_SEGMENTED_LITERAL_POOL=$<BOOL:${CGBA_SH4_SEGMENTED_LITERAL_POOL}>
    CGBA_SH4_COMPACT_FASTMEM_TUPLE=$<BOOL:${CGBA_SH4_COMPACT_FASTMEM_TUPLE}>
    CGBA_SH4_FASTMEM_OOL_DESC=$<BOOL:${CGBA_SH4_FASTMEM_OOL_DESC}>
    SMALL_TRANSLATION_CACHE=1
    ROM_TRANSLATION_CACHE_SIZE=${CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE}
    RAM_TRANSLATION_CACHE_SIZE=${CGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE}
    ROM_BRANCH_HASH_BITS=${CGBA_GPSP_ROM_BRANCH_HASH_BITS}
  )
  # HAVE_DYNAREC only where it matters: main.c wires init_dynarec_caches/
  # init_emitter and savestate.c flushes on load. cpu.cc must NOT see it, so it
  # keeps owning the guest state arrays (reg[], ewram, ...) the dynarec links to.
  set_property(SOURCE ${GPSP}/main.c ${GPSP}/savestate.c
    APPEND PROPERTY COMPILE_DEFINITIONS HAVE_DYNAREC=1)
  if(CGBA_SH4_ARM_LDST_NATIVE)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_ARM_LDST_NATIVE=1)
  endif()
  if(CGBA_SH4_THUMB_LDST_NATIVE)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_THUMB_LDST_NATIVE=1)
  endif()
  if(CGBA_SH4_THUMB_BLOCK_NATIVE)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_THUMB_BLOCK_NATIVE=1)
  endif()
  if(CGBA_SH4_INTERP_STATS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_INTERP_STATS=1)
  endif()
  if(CGBA_SH4_DIAG_COUNTERS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_DIAG_COUNTERS=1)
  endif()
  if(DEFINED CGBA_SH4_SWI_CPUSET_FAITHFUL)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_SWI_CPUSET_FAITHFUL=${CGBA_SH4_SWI_CPUSET_FAITHFUL})
  endif()
  if(CGBA_SH4_SWI_HLE_VERIFY)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_SWI_HLE_VERIFY=1)
  endif()
  if(CGBA_SH4_SWI_FORWARD_OVERLAP)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_SWI_FORWARD_OVERLAP=1)
  endif()
  if(CGBA_SH4_SWI_OAM_BULK)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_SWI_OAM_BULK=1)
  endif()
  # Exact-boundary diagnostics intentionally return to update_gba after every
  # guest instruction; multi-instruction fusions cannot preserve that mode.
  if(CGBA_SH4_THUMB_UDIV_FASTPATH AND NOT CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_THUMB_UDIV_FASTPATH=1)
  endif()
  if(CGBA_SH4_ARM_MIXER_FASTPATH AND NOT CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_ARM_MIXER_FASTPATH=1)
  endif()
  if(CGBA_SH4_M4A_MIX_SKIP)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_M4A_MIX_SKIP=1)
    # Exact-boundary diagnostics return to update_gba after every guest
    # instruction; a multi-iteration fold cannot preserve that mode.
    if(CGBA_SH4_M4A_LOOP_FOLD AND NOT CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
      target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_M4A_LOOP_FOLD=1)
    endif()
  endif()
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_SH4_HEAT_QUIET_FRAMES=${CGBA_SH4_HEAT_QUIET_FRAMES}
    CGBA_SH4_HEAT_SLOW=${CGBA_SH4_HEAT_SLOW}
    CGBA_SH4_HEAT_FLUSH_LEAK=${CGBA_SH4_HEAT_FLUSH_LEAK})
  if(NOT CGBA_SH4_ARM_DEAD_FLAGS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_ARM_DEAD_FLAGS=0)
  endif()
  # Always pass the threshold: the old NOT-"0" guard made 0 fall back to the
  # in-code default of 64, so the gate could never actually be disabled.
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_SH4_HOT_THRESHOLD=${CGBA_SH4_HOT_THRESHOLD})
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_SH4_COLD_CHUNK_CYCLES=${CGBA_SH4_COLD_CHUNK_CYCLES})
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_SH4_RAM_SCAN_CAP_INITIAL=${CGBA_SH4_RAM_SCAN_CAP_INITIAL}
    CGBA_SH4_ROM_SCAN_CAP_INITIAL=${CGBA_SH4_ROM_SCAN_CAP_INITIAL})
  # Instruction patches are made before final block publication, so the full
  # range sync is sufficient for production. Keep the per-patch path as an
  # explicit hardware/overclock qualification switch.
  if(CGBA_SH4_PATCH_RESYNC)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_PATCH_RESYNC=1)
  endif()
  if(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_EXACT_CYCLE_BOUNDARIES=1)
  endif()
  if(CGBA_SH4_DIFF_HARNESS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_DIFF_HARNESS=1)
  endif()
  if(CGBA_SH4_DIFF_DUMP_OPS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_DIFF_DUMP_OPS=1)
  endif()
  if(CGBA_SH4_PROFILE_COUNTERS)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_PROFILE_COUNTERS=1)
  endif()
endif()

# Hardware debug: show an on-screen pipeline snapshot after loading a game (EXE
# to continue). Kept OFF for normal builds; flip ON to debug the load/render path.
option(CGBA_GPSP_DIAG "On-screen pipeline diagnostic overlay" OFF)
if(CGBA_GPSP_DIAG)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_GPSP_DIAG=1)
endif()

# Base-layer 4-bpp text backgrounds replace each sub-palette's transparent slot
# with the backdrop in a lazily rebuilt shadow, removing the per-pixel
# transparency branch from the common Mode 0 FULLCOLOR path.
option(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
  "Branchless base-layer 4-bpp backdrop lookup (exact production path)" ON)
if(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_BACKDROP_SHADOW_PALETTE=1)
endif()

# Overlay text layers are mostly transparent: step over runs of identical map
# entries whose tile row is empty instead of decoding every tile.
option(CGBA_VIDEO_EMPTY_RUN_SKIP
  "Skip runs of repeated empty overlay tiles in 4-bpp text backgrounds" ON)
if(CGBA_VIDEO_EMPTY_RUN_SKIP)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_EMPTY_RUN_SKIP=1)
endif()

option(CGBA_VIDEO_OPAQUE_ROW_UNROLL
  "Unroll the measured exact 4-bpp tile-row shapes" ON)
if(CGBA_VIDEO_OPAQUE_ROW_UNROLL)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_OPAQUE_ROW_UNROLL=1)
endif()

# Base-layer 4-bpp rows: one table load per packed byte (two pixels) instead
# of two nibble extractions and two palette loads. 16 KiB of BSS.
option(CGBA_VIDEO_PAIR_PALETTE
  "Two-pixel palette table for 4-bpp FULLCOLOR text rows" ON)
if(CGBA_VIDEO_PAIR_PALETTE AND CGBA_VIDEO_BACKDROP_SHADOW_PALETTE AND
   CGBA_VIDEO_OPAQUE_ROW_UNROLL)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_PAIR_PALETTE=1)
endif()

# Game-specific idle-loop metadata is isolated behind an option so the timing
# shortcut can be parity-checked before becoming part of the normal database.
option(CGBA_GBA_OVER_AZLE_IDLE
  "Enable Zelda: A Link to the Past/Four Swords (AZLE) signature-checked idle-loop hint" ON)
if(CGBA_GBA_OVER_AZLE_IDLE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_GBA_OVER_AZLE_IDLE=1)
endif()

# Hardware-only diagnostic: avoid all BFile/config/storage calls. If this boots
# through the normal menu while the regular build faults, the crash is isolated
# to the storage syscall path rather than LCD, key scan, menu drawing, or gpSP's
# built-in test runner.
option(CGBA_GPSP_DISABLE_STORAGE
  "Hardware debug: disable config load/save and ROM storage scans" OFF)
if(CGBA_GPSP_DISABLE_STORAGE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_GPSP_DISABLE_STORAGE=1)
endif()

# Emulator-only: auto-boot the first storage ROM and stream cgba_gpsp_diag() to
# the host debug-putchar port (run-headless.sh). Keep OFF for hardware builds.
option(CGBA_GPSP_HEADLESS_TEST "Headless auto-boot + stdout diag (emulator)" OFF)
option(CGBA_GPSP_HEADLESS_PROFILE_STOP
  "Headless profiling: stop HLE_PROFILE immediately after the measured frame loop" OFF)
set(CGBA_GPSP_HEADLESS_FRAMES "48" CACHE STRING
  "Number of frames to run in CGBA_GPSP_HEADLESS_TEST")
set(CGBA_GPSP_HEADLESS_FRAME_BASE "0" CACHE STRING
  "Absolute frame number for the first CGBA_GPSP_HEADLESS_TEST frame")
set(CGBA_GPSP_HEADLESS_LOG_EVERY "1" CACHE STRING
  "Frame log interval in CGBA_GPSP_HEADLESS_TEST; 0 logs only summary")
set(CGBA_GPSP_HEADLESS_START_FRAME "0" CACHE STRING
  "Frame at which CGBA_GPSP_HEADLESS_TEST injects GBA START")
set(CGBA_GPSP_HEADLESS_START_HOLD "0" CACHE STRING
  "Number of frames to hold START in CGBA_GPSP_HEADLESS_TEST; 0 disables")
set(CGBA_GPSP_HEADLESS_A_FRAME "0" CACHE STRING
  "Frame at which CGBA_GPSP_HEADLESS_TEST starts pressing GBA A/SHIFT")
set(CGBA_GPSP_HEADLESS_A_HOLD "0" CACHE STRING
  "Number of frames to keep tapping GBA A/SHIFT; 0 disables")
set(CGBA_GPSP_HEADLESS_A_PERIOD "12" CACHE STRING
  "A/SHIFT tap period in frames; 0 holds continuously")
set(CGBA_GPSP_HEADLESS_A_PRESS "2" CACHE STRING
  "Number of pressed frames per A/SHIFT tap period")
set(CGBA_GPSP_HEADLESS_DOWN_FRAME "0" CACHE STRING
  "Frame at which CGBA_GPSP_HEADLESS_TEST starts holding GBA Down")
set(CGBA_GPSP_HEADLESS_DOWN_HOLD "0" CACHE STRING
  "Number of frames to hold GBA Down; 0 disables")
set(CGBA_GPSP_HEADLESS_DUMP_EVERY "0" CACHE STRING
  "Headless debug: dump raw GBA framebuffer every N frames as RGB565 hex; 0 disables")
set(CGBA_GPSP_HEADLESS_FRAMESKIP "3" CACHE STRING
  "Headless: render 1 frame in N+1 (stat frames always render)")
set(CGBA_GPSP_HEADLESS_RUN_FRAME "0" CACHE STRING
  "Headless: hold LEFT/RIGHT (flipping) from this frame to run in-world (0=off)")
set(CGBA_GPSP_HEADLESS_RUN_FLIP "900" CACHE STRING
  "Headless: flip run direction every N frames")
set(CGBA_GPSP_HEADLESS_START_PERIOD "0" CACHE STRING
  "Headless: mash START every N frames within the START window (0 = hold)")
set(CGBA_GPSP_HEADLESS_START_PRESS "2" CACHE STRING
  "Headless: frames START stays down per mash")
set(CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME "-1" CACHE STRING
  "Headless: run the real slot-save path (per-ROM name + compression) at this frame")
set(CGBA_GPSP_HEADLESS_ALT_PERIOD "0" CACHE STRING
  "Headless: alternate A/START taps, one per N-frame window (0 = off)")
set(CGBA_GPSP_HEADLESS_ALT_PRESS "2" CACHE STRING
  "Headless: frames the alternating tap stays down")
set(CGBA_GPSP_HEADLESS_ALT_FRAME "60" CACHE STRING
  "Headless: first frame of the alternating input")
option(CGBA_GPSP_HEADLESS_ALT_LEFT
  "Headless: alternate A with LEFT instead of START" OFF)
if(CGBA_GPSP_HEADLESS_ALT_LEFT)
  set(CGBA_GPSP_HEADLESS_ALT_LEFT_VALUE 1)
else()
  set(CGBA_GPSP_HEADLESS_ALT_LEFT_VALUE 0)
endif()
set(CGBA_GPSP_HEADLESS_SCALE "0" CACHE STRING
  "Display scale mode forced in CGBA_GPSP_HEADLESS_TEST (0/1/2)")
set(CGBA_GPSP_HEADLESS_FILTER "0" CACHE STRING
  "Scale filter forced in CGBA_GPSP_HEADLESS_TEST (0=smooth 1=sharp 2=crisp)")
set(CGBA_GPSP_HEADLESS_STAT_EVERY "0" CACHE STRING
  "Headless debug: log framebuffer stats every N frames; 0 disables")
set(CGBA_GPSP_HEADLESS_STATE_EVERY "0" CACHE STRING
  "Headless debug: log CPU/scheduler/memory state every N frames; 0 disables")
set(CGBA_GPSP_HEADLESS_STATE_START "0" CACHE STRING
  "Headless debug: first frame for state logging")
set(CGBA_GPSP_HEADLESS_STATE_END "4294967295" CACHE STRING
  "Headless debug: last frame for state logging")
set(CGBA_GPSP_HEADLESS_PHASE_START "4294967295" CACHE STRING
  "Headless debug: first frame for per-phase loop breadcrumbs")
set(CGBA_GPSP_HEADLESS_PHASE_END "4294967295" CACHE STRING
  "Headless debug: last frame for per-phase loop breadcrumbs")
set(CGBA_GPSP_HEADLESS_DYNAREC "-1" CACHE STRING
  "Force dynarec in CGBA_GPSP_HEADLESS_TEST: -1 default, 0 off, 1 on")
set(CGBA_GPSP_HEADLESS_DIFF_FRAME "-1" CACHE STRING
  "Headless debug: frame at which to run live dynarec block diff; -1 disables")
set(CGBA_GPSP_HEADLESS_DIFF_BLOCKS "0" CACHE STRING
  "Headless debug: maximum live dynarec block-diff blocks; 0 disables")
set(CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME "-1" CACHE STRING
  "Headless debug: frame at which to run a preserving interp-vs-JIT window diff; -1 disables")
set(CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME "-1" CACHE STRING
  "Headless debug: absolute frame after which to save a reusable checkpoint; -1 disables")
set(CGBA_GPSP_HEADLESS_LOAD_STATE "0" CACHE STRING
  "Headless debug: load the reusable checkpoint before running frames; 0 disables")
set(CGBA_GPSP_HEADLESS_SCRIPT "" CACHE STRING
  "Headless input script: comma list of frame:key[:hold] (keys ABSEUDLR), e.g. 30:A,200:D:4")
set(CGBA_GPSP_HEADLESS_TRACE_PC "0" CACHE STRING
  "Headless debug: trace Thumb load/store helper state at this guest PC; 0 disables")
set(CGBA_GPSP_HEADLESS_TRACE_MASK "4095" CACHE STRING
  "Headless debug: log trace-PC helper state when (++count & mask) == 0")
set(CGBA_GPSP_HEADLESS_TRACE_JIT "0" CACHE STRING
  "Headless debug: log per-frame JIT pc/cpsr/cycle state; 0 disables")
set(CGBA_GPSP_HEADLESS_TRACE_TIMER_IO "0" CACHE STRING
  "Headless debug: log writes to GBA timer IO registers; 0 disables")
set(CGBA_GPSP_HEADLESS_BENCH_FRAMES "0" CACHE STRING
  "Headless debug: run interpreter-vs-JIT throughput bench for this many frames; 0 disables")
set(CGBA_GPSP_HEADLESS_WJ_START "0" CACHE STRING
  "Headless debug: first frame to log wild-jump resolver trace")
set(CGBA_GPSP_HEADLESS_WJ_END "4294967295" CACHE STRING
  "Headless debug: last frame to log wild-jump resolver trace")
if(CGBA_SH4_DIFF_HARNESS AND NOT CGBA_GPSP_HEADLESS_TEST)
  message(FATAL_ERROR
    "CGBA_SH4_DIFF_HARNESS exceeds the fx-CG100 hardware-safe RAM arena and is permitted only with CGBA_GPSP_HEADLESS_TEST=ON")
endif()
if(CGBA_GPSP_HEADLESS_TEST AND NOT CGBA_SH4_DIFF_HARNESS AND
   (CGBA_GPSP_HEADLESS_DIFF_BLOCKS GREATER 0 OR
    CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME GREATER_EQUAL 0 OR
    CGBA_GPSP_HEADLESS_BENCH_FRAMES GREATER 0))
  message(FATAL_ERROR
    "Headless diff/throughput diagnostics require -DCGBA_SH4_DIFF_HARNESS=ON (emulator only)")
endif()
if(CGBA_GPSP_HEADLESS_TEST)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_GPSP_HEADLESS_TEST=1
    CGBA_GPSP_HEADLESS_FRAMES=${CGBA_GPSP_HEADLESS_FRAMES}
    CGBA_GPSP_HEADLESS_FRAME_BASE=${CGBA_GPSP_HEADLESS_FRAME_BASE}
    CGBA_GPSP_HEADLESS_LOG_EVERY=${CGBA_GPSP_HEADLESS_LOG_EVERY}
    CGBA_GPSP_HEADLESS_START_FRAME=${CGBA_GPSP_HEADLESS_START_FRAME}
    CGBA_GPSP_HEADLESS_START_HOLD=${CGBA_GPSP_HEADLESS_START_HOLD}
    CGBA_GPSP_HEADLESS_A_FRAME=${CGBA_GPSP_HEADLESS_A_FRAME}
    CGBA_GPSP_HEADLESS_A_HOLD=${CGBA_GPSP_HEADLESS_A_HOLD}
    CGBA_GPSP_HEADLESS_A_PERIOD=${CGBA_GPSP_HEADLESS_A_PERIOD}
    CGBA_GPSP_HEADLESS_A_PRESS=${CGBA_GPSP_HEADLESS_A_PRESS}
    CGBA_GPSP_HEADLESS_DOWN_FRAME=${CGBA_GPSP_HEADLESS_DOWN_FRAME}
    CGBA_GPSP_HEADLESS_DOWN_HOLD=${CGBA_GPSP_HEADLESS_DOWN_HOLD}
    CGBA_GPSP_HEADLESS_DUMP_EVERY=${CGBA_GPSP_HEADLESS_DUMP_EVERY}
    CGBA_GPSP_HEADLESS_STAT_EVERY=${CGBA_GPSP_HEADLESS_STAT_EVERY}
    CGBA_GPSP_HEADLESS_SCALE=${CGBA_GPSP_HEADLESS_SCALE}
    CGBA_GPSP_HEADLESS_FILTER=${CGBA_GPSP_HEADLESS_FILTER}
    CGBA_GPSP_HEADLESS_FRAMESKIP=${CGBA_GPSP_HEADLESS_FRAMESKIP}
    CGBA_GPSP_HEADLESS_RUN_FRAME=${CGBA_GPSP_HEADLESS_RUN_FRAME}
    CGBA_GPSP_HEADLESS_RUN_FLIP=${CGBA_GPSP_HEADLESS_RUN_FLIP}
    CGBA_GPSP_HEADLESS_START_PERIOD=${CGBA_GPSP_HEADLESS_START_PERIOD}
    CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME=${CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME}
    CGBA_GPSP_HEADLESS_ALT_PERIOD=${CGBA_GPSP_HEADLESS_ALT_PERIOD}
    CGBA_GPSP_HEADLESS_FUZZ_SEED=${CGBA_GPSP_HEADLESS_FUZZ_SEED}
    CGBA_GPSP_HEADLESS_ALT_PRESS=${CGBA_GPSP_HEADLESS_ALT_PRESS}
    CGBA_GPSP_HEADLESS_ALT_FRAME=${CGBA_GPSP_HEADLESS_ALT_FRAME}
    CGBA_GPSP_HEADLESS_ALT_LEFT=${CGBA_GPSP_HEADLESS_ALT_LEFT_VALUE}
    CGBA_GPSP_HEADLESS_START_PRESS=${CGBA_GPSP_HEADLESS_START_PRESS}
    CGBA_GPSP_HEADLESS_STATE_EVERY=${CGBA_GPSP_HEADLESS_STATE_EVERY}
    CGBA_GPSP_HEADLESS_STATE_START=${CGBA_GPSP_HEADLESS_STATE_START}
    CGBA_GPSP_HEADLESS_STATE_END=${CGBA_GPSP_HEADLESS_STATE_END}
    CGBA_GPSP_HEADLESS_PHASE_START=${CGBA_GPSP_HEADLESS_PHASE_START}
    CGBA_GPSP_HEADLESS_PHASE_END=${CGBA_GPSP_HEADLESS_PHASE_END}
    CGBA_GPSP_HEADLESS_DYNAREC=${CGBA_GPSP_HEADLESS_DYNAREC}
    CGBA_GPSP_HEADLESS_DIFF_FRAME=${CGBA_GPSP_HEADLESS_DIFF_FRAME}
    CGBA_GPSP_HEADLESS_DIFF_BLOCKS=${CGBA_GPSP_HEADLESS_DIFF_BLOCKS}
    CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME=${CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME}
    CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME=${CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME}
    CGBA_GPSP_HEADLESS_LOAD_STATE=${CGBA_GPSP_HEADLESS_LOAD_STATE}
    CGBA_GPSP_HEADLESS_SCRIPT="${CGBA_GPSP_HEADLESS_SCRIPT}"
    CGBA_GPSP_HEADLESS_TRACE_PC=${CGBA_GPSP_HEADLESS_TRACE_PC}
    CGBA_GPSP_HEADLESS_TRACE_MASK=${CGBA_GPSP_HEADLESS_TRACE_MASK}
    CGBA_GPSP_HEADLESS_TRACE_JIT=${CGBA_GPSP_HEADLESS_TRACE_JIT}
    CGBA_GPSP_HEADLESS_TRACE_TIMER_IO=${CGBA_GPSP_HEADLESS_TRACE_TIMER_IO}
    CGBA_GPSP_HEADLESS_BENCH_FRAMES=${CGBA_GPSP_HEADLESS_BENCH_FRAMES}
    CGBA_GPSP_HEADLESS_WJ_START=${CGBA_GPSP_HEADLESS_WJ_START}
    CGBA_GPSP_HEADLESS_WJ_END=${CGBA_GPSP_HEADLESS_WJ_END})
  if(CGBA_GPSP_HEADLESS_PROFILE_STOP)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_GPSP_HEADLESS_PROFILE_STOP=1)
  endif()
endif()

target_compile_options(cgba-gint-gpsp PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
  -O2 -g -fomit-frame-pointer -ffunction-sections -fdata-sections
)

target_compile_options(cgba-gint-gpsp PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
  $<$<COMPILE_LANGUAGE:ASM>:-Wa,-I${GPSP}>
)

# Performance A/B (P0): compile the two hottest gpSP translation units -- the CPU
# interpreter and the scanline renderer -- at -O3 instead of the global -O2.
# MEASURED on fx-CG100 hardware: -O3 on both files slightly REGRESSED worst-case
# fps and added ~40 KB -- the predicted I-cache bloat on the giant dispatch switch.
# So the default stays -O2 (option OFF); leave it OFF. (-g is intentionally
# retained: no runtime/g3a-size effect, preserves addr2line for crash triage.)
option(CGBA_TUNE_HOTFILES_O3 "Compile selected gpSP/fxCG100 hot files at -O3 (benchmark before enabling by default)" OFF)
if(CGBA_TUNE_HOTFILES_O3)
  set_source_files_properties(${GPSP}/cpu.cc ${GPSP}/video.cc
    ${GPSP}/main.c ${FXCG100}/sh4/sh4_interp_helpers.c
    PROPERTIES COMPILE_OPTIONS "-O3")
endif()

# A narrower compiler A/B.  On the Zelda checkpoint this added 41,248 bytes of
# text and very slightly regressed the cache-sim model (36.081 -> 36.079 fps),
# so it remains a diagnostic knob rather than a release setting.
option(CGBA_TUNE_VIDEO_O3
  "Compile only the scanline renderer at -O3 (benchmark before enabling)" OFF)
if(CGBA_TUNE_VIDEO_O3 AND NOT CGBA_TUNE_HOTFILES_O3)
  set_source_files_properties(${GPSP}/video.cc
    PROPERTIES COMPILE_OPTIONS "-O3")
endif()

target_link_libraries(cgba-gint-gpsp Gint::Gint)

target_link_options(cgba-gint-gpsp PRIVATE
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/cgba-gint-gpsp.map
  # no_osheap.c intentionally replaces libgint's legacy-CASIOWIN syscall stubs.
  # Current libgint can still pull syscalls.S for unrelated symbols, so keep the
  # port's first-linked definitions and tolerate the later duplicate stubs.
  -Wl,--allow-multiple-definition
)

# Headless profiling retains extra counters, and the differential harness adds
# a full-state snapshot plus oracle copies. Both modes are emulator-only;
# production links use the MPM 1.1+ 0x8c720000 ceiling.
if(CGBA_GPSP_HEADLESS_TEST OR CGBA_SH4_DIFF_HARNESS)
  target_link_options(cgba-gint-gpsp PRIVATE
    -Wl,--defsym=cgba_highram_limit=0x8c780000)
endif()

if(CGBA_CLASSPAD)
  # A HollyHock-3 add-in is the stripped ELF itself. Section headers must
  # survive (strip -s keeps them): gpSP finds its own .cgba.core through them.
  string(REPLACE "gcc" "strip" CGBA_STRIP "${CMAKE_C_COMPILER}")
  add_custom_command(TARGET cgba-gint-gpsp POST_BUILD
    COMMAND ${CGBA_STRIP} -s -o "${CMAKE_CURRENT_SOURCE_DIR}/gpSP.hh3"
      "$<TARGET_FILE:cgba-gint-gpsp>")
else()
  generate_g3a(TARGET cgba-gint-gpsp OUTPUT "gpSP.g3a"
    NAME "gpSP" VERSION "01.12.1119"
    ICONS icon-uns.png icon-sel.png)
endif()
