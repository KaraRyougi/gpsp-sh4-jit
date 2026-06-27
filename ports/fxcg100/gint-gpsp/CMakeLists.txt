cmake_minimum_required(VERSION 3.15)
project(cgba_gint_gpsp C CXX ASM)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

set(CGBA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(FXCG100 "${CGBA_ROOT}/ports/fxcg100")
set(GPSP "${CGBA_ROOT}/vendor/gpsp")
set(CGBA_GPSP_LINKER "${CMAKE_CURRENT_SOURCE_DIR}/fxcg100_gpsp.ld")

set_target_properties(Gint::Gint PROPERTIES
  INTERFACE_LINK_OPTIONS "-T;${CGBA_GPSP_LINKER}"
  INTERFACE_LINK_DEPENDS "${CGBA_GPSP_LINKER}"
)

set(PORT_SOURCES
  src/main.c
  src/gint_platform.c
  src/nor_rom.c
  src/nor_filestream.c
  src/gpsp_runner.c
  src/frame_pacing.c
  src/no_osheap.c
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
# shipping target until the dynarec is differentially validated.
option(CGBA_DYNAREC "Build the SH4 dynamic recompiler (bring-up)" OFF)
option(CGBA_SH4_ARM_LDST_NATIVE
  "Enable experimental SH4 native ARM single load/store fast path" OFF)
option(CGBA_SH4_THUMB_LDST_NATIVE
  "Enable experimental SH4 native Thumb byte load/store fast path" ON)
option(CGBA_FXCG100_STORAGE
  "Enable direct fx-CG100 BFile storage access for ROM/config files" OFF)
set(CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE "524288" CACHE STRING
  "SH4 dynarec ROM translation cache size in bytes")
set(CGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE "262144" CACHE STRING
  "SH4 dynarec RAM translation cache size in bytes")
if(CGBA_DYNAREC)
  list(APPEND GPSP_C_SOURCES
    ${GPSP}/cpu_threaded.c
    ${GPSP}/memmap.c
    ${FXCG100}/sh4/sh4_interp_helpers.c
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
  CGBA_FXCG100=1
  CGBA_FULL_GPSP=1
  ROM_BUFFER_SIZE=1
  dma_transfer=gpsp_dma_transfer
)
if(CGBA_FXCG100_STORAGE)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_FXCG100_STORAGE=1)
endif()

if(CGBA_DYNAREC)
  # SH4_ARCH selects the SH4 host emitter in cpu_threaded.c. The code caches live
  # in the executable high-RAM arena and are tunable for emulator soaks.
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_DYNAREC=1
    SH4_ARCH=1
    SMALL_TRANSLATION_CACHE=1
    ROM_TRANSLATION_CACHE_SIZE=${CGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE}
    RAM_TRANSLATION_CACHE_SIZE=${CGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE}
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
endif()

# Hardware debug: show an on-screen pipeline snapshot after loading a game (EXE
# to continue). Kept OFF for normal builds; flip ON to debug the load/render path.
option(CGBA_GPSP_DIAG "On-screen pipeline diagnostic overlay" OFF)
if(CGBA_GPSP_DIAG)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_GPSP_DIAG=1)
endif()

# Emulator-only: auto-boot the first storage ROM and stream cgba_gpsp_diag() to
# the host debug-putchar port (run-headless.sh). Keep OFF for hardware builds.
option(CGBA_GPSP_HEADLESS_TEST "Headless auto-boot + stdout diag (emulator)" OFF)
set(CGBA_GPSP_HEADLESS_FRAMES "48" CACHE STRING
  "Number of frames to run in CGBA_GPSP_HEADLESS_TEST")
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
set(CGBA_GPSP_HEADLESS_DUMP_EVERY "0" CACHE STRING
  "Headless debug: dump raw GBA framebuffer every N frames as RGB565 hex; 0 disables")
set(CGBA_GPSP_HEADLESS_STAT_EVERY "0" CACHE STRING
  "Headless debug: log framebuffer stats every N frames; 0 disables")
set(CGBA_GPSP_HEADLESS_STATE_EVERY "0" CACHE STRING
  "Headless debug: log CPU/scheduler/memory state every N frames; 0 disables")
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
set(CGBA_GPSP_HEADLESS_TRACE_PC "0" CACHE STRING
  "Headless debug: trace Thumb load/store helper state at this guest PC; 0 disables")
set(CGBA_GPSP_HEADLESS_TRACE_MASK "4095" CACHE STRING
  "Headless debug: log trace-PC helper state when (++count & mask) == 0")
set(CGBA_GPSP_HEADLESS_TRACE_JIT "0" CACHE STRING
  "Headless debug: log per-frame JIT pc/cpsr/cycle state; 0 disables")
set(CGBA_GPSP_HEADLESS_BENCH_FRAMES "0" CACHE STRING
  "Headless debug: run interpreter-vs-JIT throughput bench for this many frames; 0 disables")
if(CGBA_GPSP_HEADLESS_TEST)
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_GPSP_HEADLESS_TEST=1
    CGBA_GPSP_HEADLESS_FRAMES=${CGBA_GPSP_HEADLESS_FRAMES}
    CGBA_GPSP_HEADLESS_LOG_EVERY=${CGBA_GPSP_HEADLESS_LOG_EVERY}
    CGBA_GPSP_HEADLESS_START_FRAME=${CGBA_GPSP_HEADLESS_START_FRAME}
    CGBA_GPSP_HEADLESS_START_HOLD=${CGBA_GPSP_HEADLESS_START_HOLD}
    CGBA_GPSP_HEADLESS_A_FRAME=${CGBA_GPSP_HEADLESS_A_FRAME}
    CGBA_GPSP_HEADLESS_A_HOLD=${CGBA_GPSP_HEADLESS_A_HOLD}
    CGBA_GPSP_HEADLESS_A_PERIOD=${CGBA_GPSP_HEADLESS_A_PERIOD}
    CGBA_GPSP_HEADLESS_A_PRESS=${CGBA_GPSP_HEADLESS_A_PRESS}
    CGBA_GPSP_HEADLESS_DUMP_EVERY=${CGBA_GPSP_HEADLESS_DUMP_EVERY}
    CGBA_GPSP_HEADLESS_STAT_EVERY=${CGBA_GPSP_HEADLESS_STAT_EVERY}
    CGBA_GPSP_HEADLESS_STATE_EVERY=${CGBA_GPSP_HEADLESS_STATE_EVERY}
    CGBA_GPSP_HEADLESS_PHASE_START=${CGBA_GPSP_HEADLESS_PHASE_START}
    CGBA_GPSP_HEADLESS_PHASE_END=${CGBA_GPSP_HEADLESS_PHASE_END}
    CGBA_GPSP_HEADLESS_DYNAREC=${CGBA_GPSP_HEADLESS_DYNAREC}
    CGBA_GPSP_HEADLESS_DIFF_FRAME=${CGBA_GPSP_HEADLESS_DIFF_FRAME}
    CGBA_GPSP_HEADLESS_DIFF_BLOCKS=${CGBA_GPSP_HEADLESS_DIFF_BLOCKS}
    CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME=${CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME}
    CGBA_GPSP_HEADLESS_TRACE_PC=${CGBA_GPSP_HEADLESS_TRACE_PC}
    CGBA_GPSP_HEADLESS_TRACE_MASK=${CGBA_GPSP_HEADLESS_TRACE_MASK}
    CGBA_GPSP_HEADLESS_TRACE_JIT=${CGBA_GPSP_HEADLESS_TRACE_JIT}
    CGBA_GPSP_HEADLESS_BENCH_FRAMES=${CGBA_GPSP_HEADLESS_BENCH_FRAMES})
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
# So the default stays -O2 (option OFF); leave it OFF. (A finer experiment not yet
# run: -O3 on video.cc alone, whose tight pixel loops might benefit while the cpu.cc
# dispatch clearly does not.) (-g is intentionally retained: no runtime/g3a-size
# effect, preserves addr2line for crash triage.)
option(CGBA_TUNE_HOTFILES_O3 "Compile cpu.cc + video.cc at -O3 (measured: slight regression, keep OFF)" OFF)
if(CGBA_TUNE_HOTFILES_O3)
  set_source_files_properties(${GPSP}/cpu.cc ${GPSP}/video.cc
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

generate_g3a(TARGET cgba-gint-gpsp OUTPUT "CGBA-GPSP.g3a"
  NAME "CGBA-GPSP" VERSION "00.01.0000")
