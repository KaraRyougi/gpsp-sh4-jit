cmake_minimum_required(VERSION 3.15)
project(cgba_gint_gpsp C CXX ASM)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(CGBA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(FXCG100 "${CGBA_ROOT}/ports/fxcg100")
set(GPSP "${CGBA_ROOT}/vendor/gpsp")
# Target model: fx-CG100 (default) or fx-CG50 / Graph 90+E. Both are SH7305 with
# 8 MiB RAM at 0x8c000000, so they share the gpSP core, the high-RAM arena, and
# the JIT; they differ in keyboard codes, storage backing, and the ROM source
# (CG100 memory-maps NOR; CG50 streams the .gba via BFile).
option(CGBA_FXCG50 "Build for fx-CG50 / Graph 90+E instead of fx-CG100" OFF)
if(CGBA_FXCG50)
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
  src/gint_platform.c
  src/nor_rom.c
  src/nor_filestream.c
  src/overclock.c
  src/gpsp_runner.c
  src/frame_pacing.c
  src/no_osheap.c
  src/crash_panic.c
  ${FXCG100}/fxcg100_keys.c
  ${FXCG100}/fxcg100_config.c
  ${FXCG100}/fxcg100_menu.c
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
option(CGBA_SH4_PIN_GUEST_R0
  "Keep guest ARM r0 resident in callee-saved SH-4 R11" ON)
option(CGBA_SH4_FASTMEM_XYRAM
  "Place generated resident fastmem routines in on-chip XYRAM (hardware A/B)" OFF)
option(CGBA_SH4_KNOWN_TARGET_CACHE
  "Cache known ARM and Thumb indirect targets before the ROM hash walk" ON)
option(CGBA_SH4_SEGMENTED_LITERAL_POOL
  "Deduplicate large SH-4 constants in bounded per-block literal segments" ON)
option(CGBA_SH4_COMPACT_FASTMEM_TUPLE
  "Pack single-fastmem params with a 16-bit cycle field to shrink JIT sites" ON)
option(CGBA_SH4_INTERP_STATS
  "Count interpreted guest instructions by region (diag; ~4.5% overhead)" OFF)
option(CGBA_SH4_DIAG_COUNTERS
  "Per-slice/per-helper diagnostic counters in headless builds (~0.5%)" OFF)
option(CGBA_DEBUG_MENU
  "Show the in-menu DEBUG INFO submenu + F:/H frame/hash overlay (dev; hidden in release)" OFF)
option(CGBA_SH4_SWI_HLE_VERIFY
  "Diag: predict faithful SWI HLE results, run the interpreted BIOS, compare" OFF)
option(CGBA_SH4_SWI_FORWARD_OVERLAP
  "Experimental faithful HLE for increasing-address overlapping CpuSet/FastSet" OFF)
option(CGBA_SH4_SWI_OAM_BULK
  "Experimental faithful bulk path for CpuSet halfword copies into OAM" OFF)
option(CGBA_SH4_THUMB_UDIV_FASTPATH
  "Experimental exact fast path for the Zelda/libgcc Thumb unsigned divide suffix" OFF)
option(CGBA_SH4_ARM_MIXER_FASTPATH
  "Experimental exact fast path for the Zelda/m4a ARM four-sample mixer loop" OFF)
option(CGBA_SH4_SWI_OBJAFFINE_HLE
  "Experimental SH4 HLE for BIOS ObjAffineSet (SWI 0x0F); default off until event-slice parity is fixed" OFF)
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
# The .cgba.highbss arena must end at or below 0x8c655300, the ONLY layout
# proven safe on real hardware. Growing the arena hard-reset the calculator
# twice: a 1.5 MB ROM cache (arena end 0x8c715300) overwrote the resident MPM
# loader at 0x8c700000, and even 1152 KB (end 0x8c6b5300) reset because live OS
# state sits below it. Do not raise the arena end without an on-device probe.
#
# The hybrid cartridge map keeps contiguous 32 KiB pages on the existing direct
# path and resolves fragmented pages through sanitized 4 KiB NOR pointers. Only
# unsafe blocks need the aligned gather fallback, so one 1 MiB / 32-page cache is
# sufficient. The 256 KiB embedded mini-ROM staging area is separate from that
# cache and from its 864 KiB savestate scratch use. Compared with the former
# 2 MiB shared cache, this layout recovers 768 KiB: 256 KiB grows the RAM JIT to
# the conservative 1024/512 profile and 512 KiB remains below the hardware-proven
# arena ceiling.
# The 8-dispatch cold gate plus 4-count flush leak is the July 2026 Yoshi
# A/LEFT cache-sim default: it trims repeated cold fallback after cache flushes
# while keeping leaky decay to avoid the threshold-0 translate-everything cliff.
set(CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE "1048576" CACHE STRING
  "SH4 dynarec ROM translation cache size in bytes")
set(CGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE "524288" CACHE STRING
  "SH4 dynarec RAM translation cache size in bytes")
set(CGBA_GPSP_ROM_BRANCH_HASH_BITS "12" CACHE STRING
  "SH4 dynarec ROM branch-hash = 2^N u32 entries; 12 is 16 KiB, which is friendlier to the SH7305 cache than the upstream 16 / 256 KiB table")
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
  dma_transfer=gpsp_dma_transfer
)
if(CGBA_FXCG50)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG50=1)
  target_sources(cgba-gint-gpsp PRIVATE
    src/cg50_syscall.S
    src/hottext.c)  # BFile trampoline + RAM-resident native-code loader
else()
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG100=1)
endif()
if(CGBA_FXCG100_STORAGE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG100_STORAGE=1)
endif()
if(CGBA_DEFAULT_OVERCLOCK)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_DEFAULT_OVERCLOCK=1)
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
    CGBA_SH4_PIN_GUEST_R0=$<BOOL:${CGBA_SH4_PIN_GUEST_R0}>
    CGBA_SH4_FASTMEM_XYRAM=$<BOOL:${CGBA_SH4_FASTMEM_XYRAM}>
    CGBA_SH4_KNOWN_TARGET_CACHE=$<BOOL:${CGBA_SH4_KNOWN_TARGET_CACHE}>
    CGBA_SH4_SEGMENTED_LITERAL_POOL=$<BOOL:${CGBA_SH4_SEGMENTED_LITERAL_POOL}>
    CGBA_SH4_COMPACT_FASTMEM_TUPLE=$<BOOL:${CGBA_SH4_COMPACT_FASTMEM_TUPLE}>
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
  if(DEFINED CGBA_SH4_SWI_MEM_HLE)
    target_compile_definitions(cgba-gint-gpsp PRIVATE
      CGBA_SH4_SWI_MEM_HLE=${CGBA_SH4_SWI_MEM_HLE})
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
  if(CGBA_SH4_SWI_OBJAFFINE_HLE)
    target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_SH4_SWI_OBJAFFINE_HLE=1)
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

# Cache palette colors multiplied by the two alpha-blend factors.  This
# removes two multiplies and two RGB565 expansion sequences per blended pixel
# when the palette/factors are stable, while an adaptive fallback avoids table
# rebuilds for raster effects which change them every scanline.
option(CGBA_VIDEO_BLEND_PALETTE_CACHE
  "Cache stable palette alpha-blend terms (experimental performance A/B)" OFF)
if(CGBA_VIDEO_BLEND_PALETTE_CACHE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_BLEND_PALETTE_CACHE=1)
endif()

# Base-layer 4-bpp text backgrounds replace each sub-palette's transparent slot
# with the backdrop in a lazily rebuilt shadow, removing the per-pixel
# transparency branch from the common Mode 0 FULLCOLOR path. Exhaustive host
# differentials and representative saved-game framebuffer A/Bs make this an
# exact production path; keep the broader palette blend cache experimental.
option(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
  "Branchless base-layer 4-bpp backdrop lookup (exact production path)" ON)
if(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_BACKDROP_SHADOW_PALETTE=1)
endif()

# Fully opaque 4-bpp tile rows need neither transparency tests nor per-pixel
# branches.  Keep the specialized renderer opt-in until representative-game
# and device cache behavior have both been measured.
option(CGBA_VIDEO_OPAQUE_TILE_FASTPATH
  "Specialize fully opaque 4-bpp tile rows (experimental performance A/B)" OFF)
if(CGBA_VIDEO_OPAQUE_TILE_FASTPATH)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_OPAQUE_TILE_FASTPATH=1)
endif()

option(CGBA_VIDEO_OPAQUE_ROW_UNROLL
  "Unroll the measured exact 4-bpp tile-row shapes" ON)
if(CGBA_VIDEO_OPAQUE_ROW_UNROLL)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_VIDEO_OPAQUE_ROW_UNROLL=1)
endif()

# Experimental unscaled-mode path that publishes each completed 16-row group
# from the renderer to the double-buffered YRAM presenter. Physical Ace
# Attorney A/B testing found this slower than the established end-of-frame DMA
# presenter, so release builds keep it compiled out by default.
option(CGBA_LCD_SCANLINE_STREAM
  "Stream completed unscaled scanline groups to LCD DMA (hardware A/B)" OFF)
if(CGBA_LCD_SCANLINE_STREAM)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_LCD_SCANLINE_STREAM=1)
endif()

# Game-specific idle-loop metadata is isolated behind an option so the timing
# shortcut can be parity-checked before becoming part of the normal database.
option(CGBA_GBA_OVER_AZLE_IDLE
  "Enable Zelda: A Link to the Past/Four Swords (AZLE) idle-loop hint (experimental A/B)" OFF)
if(CGBA_GBA_OVER_AZLE_IDLE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_GBA_OVER_AZLE_IDLE=1)
endif()

# Hardware-only diagnostic: bypass the menu/storage scan and boot the built-in
# LCD test directly. This keeps ROM/NOR syscalls out of the launch path while
# still exercising the display loop and the same add-in runtime.
option(CGBA_GPSP_DIRECT_LCD_TEST
  "Hardware debug: boot the built-in LCD test without scanning storage" OFF)
if(CGBA_GPSP_DIRECT_LCD_TEST)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_GPSP_DIRECT_LCD_TEST=1)
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
# production links use the hardware-proven 0x8c655300 ceiling.
if(CGBA_GPSP_HEADLESS_TEST OR CGBA_SH4_DIFF_HARNESS)
  target_link_options(cgba-gint-gpsp PRIVATE
    -Wl,--defsym=cgba_highram_limit=0x8c6F0000)
endif()

generate_g3a(TARGET cgba-gint-gpsp OUTPUT "gpSP.g3a"
  NAME "gpSP" VERSION "00.07.0713"
  ICONS icon-uns.png icon-sel.png)
