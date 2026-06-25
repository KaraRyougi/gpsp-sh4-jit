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

if(CGBA_DYNAREC)
  # SH4_ARCH selects the SH4 host emitter in cpu_threaded.c. Shrink the code
  # caches to share the executable high-RAM arena with emulator state.
  target_compile_definitions(cgba-gint-gpsp PRIVATE
    CGBA_DYNAREC=1
    SH4_ARCH=1
    SMALL_TRANSLATION_CACHE=1
    ROM_TRANSLATION_CACHE_SIZE=1048576      # 1 MiB
    RAM_TRANSLATION_CACHE_SIZE=262144       # 256 KiB
  )
  # HAVE_DYNAREC only where it matters: main.c wires init_dynarec_caches/
  # init_emitter and savestate.c flushes on load. cpu.cc must NOT see it, so it
  # keeps owning the guest state arrays (reg[], ewram, ...) the dynarec links to.
  set_property(SOURCE ${GPSP}/main.c ${GPSP}/savestate.c
    APPEND PROPERTY COMPILE_DEFINITIONS HAVE_DYNAREC=1)
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
if(CGBA_GPSP_HEADLESS_TEST)
  target_compile_definitions(cgba-gint-gpsp PRIVATE CGBA_GPSP_HEADLESS_TEST=1)
endif()

target_compile_options(cgba-gint-gpsp PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
  -O2 -g -fomit-frame-pointer -ffunction-sections -fdata-sections
)

target_compile_options(cgba-gint-gpsp PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
  $<$<COMPILE_LANGUAGE:ASM>:-Wa,-I${GPSP}>
)

target_link_libraries(cgba-gint-gpsp Gint::Gint)

target_link_options(cgba-gint-gpsp PRIVATE
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/cgba-gint-gpsp.map
)

generate_g3a(TARGET cgba-gint-gpsp OUTPUT "CGBA-GPSP.g3a"
  NAME "CGBA-GPSP" VERSION "00.01.0000")
