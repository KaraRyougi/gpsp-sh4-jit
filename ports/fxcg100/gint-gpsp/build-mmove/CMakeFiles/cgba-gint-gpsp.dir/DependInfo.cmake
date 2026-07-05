
# Consider dependencies only in project.
set(CMAKE_DEPENDS_IN_PROJECT_ONLY OFF)

# The set of languages for which implicit dependencies are needed:
set(CMAKE_DEPENDS_LANGUAGES
  "ASM"
  )
# The set of files for implicit dependencies of each language:
set(CMAKE_DEPENDS_CHECK_ASM
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/bios_data.S" "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/build-mmove/CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/bios_data.S.obj"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/sh4/sh4_stub.S" "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/build-mmove/CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/sh4/sh4_stub.S.obj"
  )
set(CMAKE_ASM_COMPILER_ID "GNU")

# Preprocessor definitions for this target.
set(CMAKE_TARGET_DEFINITIONS_ASM
  "CGBA_DYNAREC=1"
  "CGBA_FULL_GPSP=1"
  "CGBA_FXCG100=1"
  "CGBA_FXCG100_STORAGE=1"
  "CGBA_GPSP_HEADLESS_ALT_FRAME=60"
  "CGBA_GPSP_HEADLESS_ALT_PERIOD=0"
  "CGBA_GPSP_HEADLESS_ALT_PRESS=2"
  "CGBA_GPSP_HEADLESS_A_FRAME=1"
  "CGBA_GPSP_HEADLESS_A_HOLD=1150"
  "CGBA_GPSP_HEADLESS_A_PERIOD=12"
  "CGBA_GPSP_HEADLESS_A_PRESS=2"
  "CGBA_GPSP_HEADLESS_BENCH_FRAMES=0"
  "CGBA_GPSP_HEADLESS_DIFF_BLOCKS=0"
  "CGBA_GPSP_HEADLESS_DIFF_FRAME=-1"
  "CGBA_GPSP_HEADLESS_DUMP_EVERY=0"
  "CGBA_GPSP_HEADLESS_DYNAREC=1"
  "CGBA_GPSP_HEADLESS_FRAMES=3000"
  "CGBA_GPSP_HEADLESS_FRAMESKIP=15"
  "CGBA_GPSP_HEADLESS_FRAME_BASE=0"
  "CGBA_GPSP_HEADLESS_LOAD_STATE=1"
  "CGBA_GPSP_HEADLESS_LOG_EVERY=1"
  "CGBA_GPSP_HEADLESS_PHASE_END=4294967295"
  "CGBA_GPSP_HEADLESS_PHASE_START=4294967295"
  "CGBA_GPSP_HEADLESS_RUN_FLIP=600"
  "CGBA_GPSP_HEADLESS_RUN_FRAME=1"
  "CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME=-1"
  "CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME=-1"
  "CGBA_GPSP_HEADLESS_START_FRAME=0"
  "CGBA_GPSP_HEADLESS_START_HOLD=0"
  "CGBA_GPSP_HEADLESS_START_PERIOD=0"
  "CGBA_GPSP_HEADLESS_START_PRESS=2"
  "CGBA_GPSP_HEADLESS_STATE_END=4294967295"
  "CGBA_GPSP_HEADLESS_STATE_EVERY=0"
  "CGBA_GPSP_HEADLESS_STATE_START=0"
  "CGBA_GPSP_HEADLESS_STAT_EVERY=100"
  "CGBA_GPSP_HEADLESS_TEST=1"
  "CGBA_GPSP_HEADLESS_TRACE_JIT=0"
  "CGBA_GPSP_HEADLESS_TRACE_MASK=4095"
  "CGBA_GPSP_HEADLESS_TRACE_PC=0"
  "CGBA_GPSP_HEADLESS_TRACE_TIMER_IO=0"
  "CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME=-1"
  "CGBA_GPSP_HEADLESS_WJ_END=4294967295"
  "CGBA_GPSP_HEADLESS_WJ_START=0"
  "CGBA_SH4_ARM_LDST_NATIVE=1"
  "CGBA_SH4_HOT_THRESHOLD=64"
  "CGBA_SH4_PATCH_RESYNC=1"
  "CGBA_SH4_THUMB_BLOCK_NATIVE=1"
  "CGBA_SH4_THUMB_LDST_NATIVE=1"
  "FXCG50"
  "MSB_FIRST=1"
  "RAM_TRANSLATION_CACHE_SIZE=131072"
  "ROM_BRANCH_HASH_BITS=12"
  "ROM_BUFFER_SIZE=1"
  "ROM_TRANSLATION_CACHE_SIZE=917504"
  "SH4_ARCH=1"
  "SMALL_TRANSLATION_CACHE=1"
  "TARGET_FXCG50"
  "dma_transfer=gpsp_dma_transfer"
  )

# The include file search paths:
set(CMAKE_ASM_TARGET_INCLUDE_PATH
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/../../../ports/fxcg100"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/../../.."
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/../../../vendor/gpsp"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/../../../vendor/gpsp/libretro"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/../../../vendor/gpsp/libretro/libretro-common/include"
  )

# The set of dependency files which are needed:
set(CMAKE_DEPENDS_DEPENDENCY_FILES
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_config.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_config.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_config.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_gpsp_stubs.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_gpsp_stubs.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_gpsp_stubs.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_keys.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_keys.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_keys.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_menu.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_menu.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_menu.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_sound_stub.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_sound_stub.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/fxcg100_sound_stub.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_diff_harness.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_diff_harness.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_diff_harness.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_dynarec_state.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_dynarec_state.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_dynarec_state.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_fastmem.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_fastmem.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_fastmem.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_interp_helpers.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_interp_helpers.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/sh4/sh4_interp_helpers.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cheats.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cheats.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cheats.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu_threaded.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu_threaded.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu_threaded.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gba_memory.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gba_memory.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gba_memory.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gbp.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gbp.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/gbp.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/input.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/input.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/input.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/main.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/main.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/main.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/memmap.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/memmap.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/memmap.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/rfu.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/rfu.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/rfu.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/savestate.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/savestate.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/savestate.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial_proto.c" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial_proto.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/serial_proto.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/crash_panic.c" "CMakeFiles/cgba-gint-gpsp.dir/src/crash_panic.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/crash_panic.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/frame_pacing.c" "CMakeFiles/cgba-gint-gpsp.dir/src/frame_pacing.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/frame_pacing.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/gint_platform.c" "CMakeFiles/cgba-gint-gpsp.dir/src/gint_platform.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/gint_platform.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/gpsp_runner.c" "CMakeFiles/cgba-gint-gpsp.dir/src/gpsp_runner.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/gpsp_runner.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/main.c" "CMakeFiles/cgba-gint-gpsp.dir/src/main.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/main.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/no_osheap.c" "CMakeFiles/cgba-gint-gpsp.dir/src/no_osheap.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/no_osheap.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/nor_filestream.c" "CMakeFiles/cgba-gint-gpsp.dir/src/nor_filestream.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/nor_filestream.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/ports/fxcg100/gint-gpsp/src/nor_rom.c" "CMakeFiles/cgba-gint-gpsp.dir/src/nor_rom.c.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/src/nor_rom.c.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu.cc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu.cc.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/cpu.cc.obj.d"
  "/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/video.cc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/video.cc.obj" "gcc" "CMakeFiles/cgba-gint-gpsp.dir/Users/ryougi/Dev/cgba/.claude/worktrees/elegant-faraday-ea393c/vendor/gpsp/video.cc.obj.d"
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_LINKED_INFO_FILES
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_FORWARD_LINKED_INFO_FILES
  )

# Fortran module output directory.
set(CMAKE_Fortran_TARGET_MODULE_DIR "")
