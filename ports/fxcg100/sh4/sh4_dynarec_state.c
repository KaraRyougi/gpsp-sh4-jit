/*
 * Executable JIT code cache for the fx-CG100 SH-4A dynarec (subtask 1).
 *
 * gpSP's default translation caches are plain static arrays, which on the
 * fx-CG100 add-in land in the 0x081xxxxx RAM window. The OS maps that window
 * NO-EXECUTE, so a naive build faults on the first JMP into translated code.
 * The 0x8c2xxxxx P1 alias (the `.cgba.highbss` arena, also home to gpSP's
 * emulator RAM regions) is cached AND executable on the SH-4A, so we place the
 * code caches there via CGBA_HIGH_BSS. The host I-cache sync after emit
 * (platform_cache_sync -> cgba_sh4_cache_sync) makes freshly written bytes
 * executable.
 *
 * Sizes are shrunk to share the high-RAM arena with emulator state (see
 * docs/sh4-jit-optimization-plan.md PART G); the build overrides
 * ROM_TRANSLATION_CACHE_SIZE / RAM_TRANSLATION_CACHE_SIZE.
 */

#include "vendor/gpsp/common.h"
#include "vendor/gpsp/gpsp_config.h"

u8 rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE] CGBA_HIGH_BSS;
u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE] CGBA_HIGH_BSS;
