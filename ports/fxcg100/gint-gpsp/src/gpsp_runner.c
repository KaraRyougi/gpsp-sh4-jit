#include "gpsp_runner.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <gint/exc.h>

#include <streams/file_stream.h>

#include "fxcg100_platform.h"
#include "nor_rom.h"
#include "vendor/gpsp/common.h"
#include "vendor/gpsp/gba_memory.h"
#include "vendor/gpsp/savestate.h"
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_DIFF_HARNESS)
#include "ports/fxcg100/sh4/sh4_diff_harness.h"
#endif

#ifndef CGBA_GPSP_HEADLESS_TRACE_JIT
#define CGBA_GPSP_HEADLESS_TRACE_JIT 0
#endif

extern RFILE *gamepak_file_large;   /* gpSP ROM page-fault source (gba_memory.c) */
extern timer_type timer[4];
extern s32 video_count;
extern u32 instruction_count;
#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
extern uint32_t cgba_dynarec_rom_flush_count;
extern uint32_t cgba_dynarec_ram_flush_count;
extern uint32_t cgba_dynarec_arm_translate_count;
extern uint32_t cgba_dynarec_thumb_translate_count;
extern uint32_t cgba_dynarec_lookup_arm_count;
extern uint32_t cgba_dynarec_lookup_thumb_count;
extern uint32_t cgba_dynarec_lookup_dual_count;
extern uint32_t cgba_dynarec_icache_sync_count;
extern uint32_t cgba_dynarec_icache_sync_bytes;
extern uint32_t cgba_dynarec_ibh_arm_hit_count;
extern uint32_t cgba_dynarec_ibh_arm_slow_count;
extern uint32_t cgba_dynarec_ibh_thumb_hit_count;
extern uint32_t cgba_dynarec_ibh_thumb_slow_count;
extern uint32_t cgba_dynarec_ibh_dual_arm_hit_count;
extern uint32_t cgba_dynarec_ibh_dual_thumb_hit_count;
extern uint32_t cgba_dynarec_ibh_dual_slow_count;
extern uint32_t cgba_dynarec_ibh_dual_hot_arm_count;
extern uint32_t cgba_dynarec_ibh_dual_hot_thumb_count;
struct cgba_sh4_prof_row {
	uint32_t key;
	uint32_t count;
};
extern uint32_t cgba_sh4_prof_entry_count;
extern uint32_t cgba_sh4_prof_overflow_count;
extern unsigned cgba_sh4_prof_top(struct cgba_sh4_prof_row *out, unsigned max);
extern uint32_t cgba_sh4_helper_thumb_ldst_count;
extern uint32_t cgba_sh4_helper_thumb_block_count;
extern uint32_t cgba_sh4_helper_thumb_shift_count;
extern uint32_t cgba_sh4_helper_thumb_dp_count;
extern uint32_t cgba_sh4_helper_arm_ldst_count;
extern uint32_t cgba_sh4_helper_arm_block_count;
extern uint32_t cgba_sh4_helper_arm_dp_count;
extern uint32_t cgba_sh4_helper_arm_mul_count;
extern uint32_t cgba_sh4_helper_arm_psr_count;
extern uint32_t cgba_sh4_helper_arm_swap_count;
extern uint32_t cgba_sh4_helper_hle_div_count;
extern uint32_t cgba_sh4_helper_arm_ldst_load_count;
extern uint32_t cgba_sh4_helper_arm_ldst_store_count;
extern uint32_t cgba_sh4_helper_arm_ldst_ram_count;
extern uint32_t cgba_sh4_helper_arm_ldst_io_count;
extern uint32_t cgba_sh4_helper_arm_ldst_video_count;
extern uint32_t cgba_sh4_helper_arm_ldst_rom_count;
extern uint32_t cgba_sh4_helper_arm_ldst_other_count;
extern uint32_t cgba_sh4_helper_arm_block_load_count;
extern uint32_t cgba_sh4_helper_arm_block_store_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_load_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_store_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_ram_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_io_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_video_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_rom_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_other_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_unmapped_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_guest_unaligned_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_host_unaligned_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_unsafe_region_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_smc_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_native_ready_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_word_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_byte_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_half_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_pc_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_sp_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_reg_count;
extern uint32_t cgba_sh4_helper_thumb_ldst_imm_count;
extern uint32_t cgba_sh4_native_thumb_const_io_count;
extern uint32_t cgba_sh4_native_thumb_runtime_io_count;
extern uint32_t cgba_sh4_native_thumb_push_iwram_count;
extern uint32_t cgba_sh4_bios_fallback_call_count;
extern uint32_t cgba_sh4_bios_fallback_cycle_count;
#endif

#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
/* 5-frame profiling window: the ON-menu debug page shows counter DELTAS over
 * the last COMPLETED window instead of boot-cumulative totals, so a reading
 * taken in a slow scene describes THAT scene. The live counters keep
 * accumulating (the headless end-of-run "jit stats" line stays cumulative);
 * only the display snapshots here. The PROF PC-histogram is instead RESET at
 * each boundary — its ranking covers the current window, at most 5 frames
 * old at read time — which also keeps its 2048-slot table from overflowing. */
#define CGBA_PROF_WIN_FRAMES 5u
void cgba_sh4_prof_reset(void);   /* cpu_threaded.c */
#define CGBA_PROF_WIN_LIST(X) \
	X(cgba_dynarec_rom_flush_count) \
	X(cgba_dynarec_ram_flush_count) \
	X(cgba_dynarec_arm_translate_count) \
	X(cgba_dynarec_thumb_translate_count) \
	X(cgba_dynarec_lookup_arm_count) \
	X(cgba_dynarec_lookup_thumb_count) \
	X(cgba_dynarec_lookup_dual_count) \
	X(cgba_dynarec_icache_sync_count) \
	X(cgba_dynarec_icache_sync_bytes) \
	X(cgba_dynarec_ibh_arm_hit_count) \
	X(cgba_dynarec_ibh_arm_slow_count) \
	X(cgba_dynarec_ibh_thumb_hit_count) \
	X(cgba_dynarec_ibh_thumb_slow_count) \
	X(cgba_dynarec_ibh_dual_arm_hit_count) \
	X(cgba_dynarec_ibh_dual_thumb_hit_count) \
	X(cgba_dynarec_ibh_dual_slow_count) \
	X(cgba_dynarec_ibh_dual_hot_arm_count) \
	X(cgba_dynarec_ibh_dual_hot_thumb_count) \
	X(cgba_sh4_helper_thumb_ldst_count) \
	X(cgba_sh4_helper_thumb_block_count) \
	X(cgba_sh4_helper_thumb_shift_count) \
	X(cgba_sh4_helper_thumb_dp_count) \
	X(cgba_sh4_helper_arm_ldst_count) \
	X(cgba_sh4_helper_arm_block_count) \
	X(cgba_sh4_helper_arm_dp_count) \
	X(cgba_sh4_helper_arm_mul_count) \
	X(cgba_sh4_helper_arm_psr_count) \
	X(cgba_sh4_helper_arm_swap_count) \
	X(cgba_sh4_helper_hle_div_count) \
	X(cgba_sh4_helper_arm_ldst_load_count) \
	X(cgba_sh4_helper_arm_ldst_store_count) \
	X(cgba_sh4_helper_arm_ldst_ram_count) \
	X(cgba_sh4_helper_arm_ldst_io_count) \
	X(cgba_sh4_helper_arm_ldst_video_count) \
	X(cgba_sh4_helper_arm_ldst_rom_count) \
	X(cgba_sh4_helper_arm_ldst_other_count) \
	X(cgba_sh4_helper_arm_block_load_count) \
	X(cgba_sh4_helper_arm_block_store_count) \
	X(cgba_sh4_helper_thumb_ldst_load_count) \
	X(cgba_sh4_helper_thumb_ldst_store_count) \
	X(cgba_sh4_helper_thumb_ldst_ram_count) \
	X(cgba_sh4_helper_thumb_ldst_io_count) \
	X(cgba_sh4_helper_thumb_ldst_video_count) \
	X(cgba_sh4_helper_thumb_ldst_rom_count) \
	X(cgba_sh4_helper_thumb_ldst_other_count) \
	X(cgba_sh4_helper_thumb_ldst_unmapped_count) \
	X(cgba_sh4_helper_thumb_ldst_guest_unaligned_count) \
	X(cgba_sh4_helper_thumb_ldst_host_unaligned_count) \
	X(cgba_sh4_helper_thumb_ldst_unsafe_region_count) \
	X(cgba_sh4_helper_thumb_ldst_smc_count) \
	X(cgba_sh4_helper_thumb_ldst_native_ready_count) \
	X(cgba_sh4_helper_thumb_ldst_word_count) \
	X(cgba_sh4_helper_thumb_ldst_byte_count) \
	X(cgba_sh4_helper_thumb_ldst_half_count) \
	X(cgba_sh4_helper_thumb_ldst_pc_count) \
	X(cgba_sh4_helper_thumb_ldst_sp_count) \
	X(cgba_sh4_helper_thumb_ldst_reg_count) \
	X(cgba_sh4_helper_thumb_ldst_imm_count) \
	X(cgba_sh4_native_thumb_const_io_count) \
	X(cgba_sh4_native_thumb_runtime_io_count) \
	X(cgba_sh4_native_thumb_push_iwram_count) \
	X(cgba_sh4_bios_fallback_call_count) \
	X(cgba_sh4_bios_fallback_cycle_count)
enum {
#define CGBA_PROF_WIN_ENUM(n) cgba_win_##n,
	CGBA_PROF_WIN_LIST(CGBA_PROF_WIN_ENUM)
#undef CGBA_PROF_WIN_ENUM
	CGBA_PROF_WIN_N
};
static uint32_t *const cgba_prof_win_live[CGBA_PROF_WIN_N] = {
#define CGBA_PROF_WIN_PTR(n) &n,
	CGBA_PROF_WIN_LIST(CGBA_PROF_WIN_PTR)
#undef CGBA_PROF_WIN_PTR
};
static uint32_t cgba_prof_win_prev[CGBA_PROF_WIN_N];
static uint32_t cgba_prof_win_delta[CGBA_PROF_WIN_N];
static unsigned cgba_prof_win_tick;
#define WV(n) ((unsigned long)cgba_prof_win_delta[cgba_win_##n])

static void cgba_prof_window_frame(void)
{
	unsigned i;
	if(++cgba_prof_win_tick < CGBA_PROF_WIN_FRAMES)
		return;
	cgba_prof_win_tick = 0;
	for(i = 0; i < (unsigned)CGBA_PROF_WIN_N; i++) {
		uint32_t v = *cgba_prof_win_live[i];
		cgba_prof_win_delta[i] = v - cgba_prof_win_prev[i];
		cgba_prof_win_prev[i] = v;
	}
	cgba_sh4_prof_reset();
}
#endif

typedef struct cgba_rom_source {
	const char *name;
	const uint8_t *data;
	uint32_t size;
	int lcd_test;
	int mode3_debug_copy;
} cgba_rom_source;

static const cgba_rom_source cgba_rom_sources[] = {
	[CGBA_GPSP_ROM_LCD_TEST] = {
		"LCD TEST",
		NULL,
		0,
		1,
		0,
	},
};

static cgba_nor_rom cgba_current_nor_rom = { .fd = -1 };
static cgba_nor_rom_list cgba_storage_roms;
static int cgba_storage_roms_scanned;
static char cgba_last_error[96];
static int cgba_lcd_test_active;
static int cgba_mode3_debug_copy_active;
static unsigned cgba_loaded_rom_id;
static uint16_t *cgba_active_framebuffer;

static void debug_line(fxcg100_debug_info *debug, const char *fmt, ...)
{
	va_list ap;

	if(!debug || debug->count >= FXCG100_DEBUG_MENU_LINES)
		return;
	va_start(ap, fmt);
	vsnprintf(debug->lines[debug->count],
		sizeof(debug->lines[debug->count]), fmt, ap);
	va_end(ap);
	debug->count++;
}

/* ---- crash reporting -------------------------------------------------------
 * Custom gint panic handler: on any CPU exception, render the GUEST CPU state
 * next to the hardware exception info, so a crash photo pinpoints the guest
 * code path (e.g. a JIT wild jump) without a debugger. Also the target of the
 * explicit wild-jump trap below (a synthetic panic code). */
#define CGBA_EXC_WILD_JUMP 0x0CBAu

static uint32_t cgba_wild_jump_pc = 0xFFFFFFFFu;

extern int dynarec_enable;

/* Panic rendering must not depend on interrupts or the DMA driver: the
 * exception may have hit mid display-DMA with interrupts disabled, and a
 * dupdate()-based present can hang forever (observed on overclocked hardware
 * as "black screen for a few seconds" before the report appeared). The
 * renderer lives in crash_panic.c (own TU: <gint/display.h> clashes with
 * gpSP's typedefs) and presents through the R61524 CPU programmed-I/O path. */
void cgba_panic_draw_begin(void);
void cgba_panic_draw_line(int row, const char *text);
void cgba_panic_draw_present(void);

static void cgba_panic_text(int row, const char *fmt, ...)
{
	char line[44];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	cgba_panic_draw_line(row, line);
}

__attribute__((noreturn))
static void cgba_crash_panic(uint32_t code)
{
	uint32_t tea = *(volatile uint32_t *)0xFF00000Cu;   /* faulting address */
	uint32_t spc;                                       /* faulting host PC */
	int row = 0;

	__asm__ volatile("stc spc, %0" : "=r"(spc));

	cgba_panic_draw_begin();
	cgba_panic_text(row++, "gpSP CRASH  EXC=%03lX%s", (unsigned long)code,
		code == CGBA_EXC_WILD_JUMP ? " (JIT WILD JUMP)" : "");
	cgba_panic_text(row++, "HOST PC=%08lX TEA=%08lX",
		(unsigned long)spc, (unsigned long)tea);
	cgba_panic_text(row++, "CORE=%s  WILD=%08lX",
		dynarec_enable ? "JIT" : "INT", (unsigned long)cgba_wild_jump_pc);
	/* Under the JIT the authoritative CPSR lives in host R8 (sh4_emit_core.h)
	 * and memory is only synced at C-call boundaries, so a crash mid-block
	 * shows a CPSR that lags by up to a block (NZCV, and the Thumb bit across
	 * chained dual dispatches). Mark it so a photo doesn't mislead. */
	cgba_panic_text(row++, "GBA PC=%08lX CPSR%s%08lX",
		(unsigned long)reg[REG_PC], dynarec_enable ? "~" : "=",
		(unsigned long)reg[REG_CPSR]);
	cgba_panic_text(row++, "GBA LR=%08lX SP=%08lX",
		(unsigned long)reg[REG_LR], (unsigned long)reg[REG_SP]);
	cgba_panic_text(row++, "GBA R0=%08lX R1=%08lX",
		(unsigned long)reg[0], (unsigned long)reg[1]);
#ifdef CGBA_DYNAREC
	cgba_panic_text(row++, "JIT ROM=%luk RAM=%luk",
		(unsigned long)((uintptr_t)rom_translation_ptr -
			(uintptr_t)rom_translation_cache) / 1024u,
		(unsigned long)((uintptr_t)ram_translation_ptr -
			(uintptr_t)ram_translation_cache) / 1024u);
#endif
	cgba_panic_text(row++, "photo this screen; then RESTART");
	cgba_panic_draw_present();

	for(;;)
		;
}

void cgba_crash_reporting_init(void)
{
	gint_panic_set(cgba_crash_panic);
}

#ifdef CGBA_DYNAREC
/* Called by block_lookup_address_* (cpu_threaded.c) when an EXECUTED guest
 * branch lands on an untranslatable address: block_lookup_translate returns
 * the (u8 *)(~0) sentinel, which the dispatch stubs would otherwise JMP to
 * (a garbage host address -> random TLB miss / address error). Panic with a
 * recognizable code and the guest target instead. Speculative (never-executed)
 * exit resolution does not come through this path. */
__attribute__((noreturn))
void cgba_sh4_wild_jump(u32 pc)
{
	cgba_wild_jump_pc = pc;
	gint_panic(CGBA_EXC_WILD_JUMP);
	for(;;)                       /* gint_panic never returns */
		;
}

/* ---- JIT memory canary -----------------------------------------------------
 * Overclock triage: the JIT constantly WRITES code to SDRAM, cache-syncs it,
 * and EXECUTES it — the harshest memory-timing pattern on this machine (the
 * interpreter never executes freshly written SDRAM). This test hammers the
 * translation-cache arena with exactly that pattern and reports corruption:
 *   pass at stock + fail at overclock  => the Ptune profile's memory timings
 *                                         are past margin (not a cgba bug);
 *   pass at both                       => keep hunting cgba codegen.
 * Destroys translated code, so the caller must flush the dynarec caches after.
 * Returns 0 on pass; nonzero = number of failures, detail in out. */
#include "ports/fxcg100/sh4/sh4_cache.h"

uint32_t cgba_jit_canary(char *out, unsigned out_len)
{
	u8 *arena = rom_translation_cache;
	u32 span = ROM_TRANSLATION_CACHE_SIZE;
	u32 fails = 0, first_off = 0, exp = 0, got = 0;
	u32 seed = 0x2545F491u;
	unsigned iter;

	for(iter = 0; iter < 8 && fails == 0; iter++) {
		u32 i, v;

		/* 1) data pattern: fill, write back, read back. */
		v = seed + iter * 0x9E3779B9u;
		for(i = 0; i + 3 < span; i += 4) {
			*(volatile u32 *)(arena + i) = v;
			v = v * 1664525u + 1013904223u;
		}
		cgba_sh4_cache_sync(arena, arena + span);
		v = seed + iter * 0x9E3779B9u;
		for(i = 0; i + 3 < span; i += 4) {
			u32 r = *(volatile u32 *)(arena + i);
			if(r != v && !fails) {
				fails++;
				first_off = i;
				exp = v;
				got = r;
			}
			v = v * 1664525u + 1013904223u;
		}
		if(fails)
			break;

		/* 2) execute pattern: emit tiny functions computing a known value
		 *    (MOV #imm,R0; ADD #k,R0 x14; RTS; NOP = 17 insns / 34 bytes),
		 *    sync, call each, verify — stale/corrupt I-fetch shows up as a
		 *    wrong sum. Cover the whole arena. */
		for(i = 0; i + 64 <= span; i += 64) {
			u16 *p = (u16 *)(arena + i);
			int k, sum;
			p[0] = 0xE000 | ((iter + (i >> 6)) & 0x7F);   /* MOV #s,R0 */
			sum = (int)((iter + (i >> 6)) & 0x7F);
			for(k = 0; k < 14; k++) {
				p[1 + k] = 0x7000 | ((k + 1) & 0xFF);      /* ADD #k+1,R0 */
				sum += k + 1;
			}
			p[15] = 0x000B;                                /* RTS */
			p[16] = 0x0009;                                /* NOP (delay) */
			((u16 *)(arena + i))[17] = (u16)sum;           /* expected */
		}
		cgba_sh4_cache_sync(arena, arena + span);
		for(i = 0; i + 64 <= span; i += 64) {
			int (*fn)(void) = (int (*)(void))(arena + i);
			int r = fn();
			int want = (int)((u16 *)(arena + i))[17];
			if(r != want && !fails) {
				fails++;
				first_off = i | 1u;                         /* bit0 = exec phase */
				exp = (u32)want;
				got = (u32)r;
			}
		}
	}

	if(out && out_len) {
		if(fails)
			snprintf(out, out_len, "FAIL@%08lX exp=%08lX got=%08lX it%u",
				(unsigned long)((uintptr_t)arena + (first_off & ~1u)),
				(unsigned long)exp, (unsigned long)got, iter);
		else
			snprintf(out, out_len, "PASS %u iters x %luk wr+exec", iter,
				(unsigned long)(span / 1024));
	}
	return fails;
}
#endif

#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
static void debug_prof_opcodes(fxcg100_debug_info *debug, uint32_t pc)
{
	if(pc >= 0x10000000u)
		return;

	debug_line(debug, "OP %04lX %04lX %04lX %04lX",
		(unsigned long)(read_memory16(pc) & 0xffffu),
		(unsigned long)(read_memory16(pc + 2) & 0xffffu),
		(unsigned long)(read_memory16(pc + 4) & 0xffffu),
		(unsigned long)(read_memory16(pc + 6) & 0xffffu));
}
#endif

static unsigned framebuffer_black_pixels(const uint16_t *framebuffer)
{
	unsigned black = 0;

	if(!framebuffer)
		return 0;
	for(unsigned i = 0; i < CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT; i++)
		black += framebuffer[i] == 0;
	return black;
}

static char sh4_area_tag(uintptr_t value)
{
	if(value < 0x80000000u)
		return '0';
	if(value < 0xa0000000u)
		return '1';
	if(value < 0xc0000000u)
		return '2';
	if(value < 0xe0000000u)
		return '3';
	return '4';
}

#if defined(CGBA_DYNAREC) && CGBA_GPSP_HEADLESS_TRACE_JIT
static void hputc_dbg(char c)
{
	*(volatile uint8_t *)0xb7000000 = (uint8_t)c;
}

static void hputs_dbg(const char *s)
{
	while(*s)
		hputc_dbg(*s++);
	hputc_dbg('\n');
}

static void trace_jit_state(const char *phase, u32 cycles)
{
	char buf[128];

	snprintf(buf, sizeof buf,
		"JIT %s pc=%08lX cpsr=%08lX cycles=%ld halt=%lu",
		phase, (unsigned long)reg[REG_PC],
		(unsigned long)reg[REG_CPSR], (long)(s32)cycles,
		(unsigned long)reg[CPU_HALT_STATE]);
	hputs_dbg(buf);
}
#endif

unsigned cgba_gpsp_refresh_roms(void)
{
#ifdef CGBA_GPSP_DISABLE_STORAGE
	memset(&cgba_storage_roms, 0, sizeof(cgba_storage_roms));
	cgba_storage_roms_scanned = 1;
	return 0;
#else
	cgba_storage_roms_scanned = 1;
	return cgba_nor_rom_scan_gba(&cgba_storage_roms);
#endif
}

static void ensure_storage_roms_scanned(void)
{
	if(!cgba_storage_roms_scanned)
		cgba_gpsp_refresh_roms();
}

const char *cgba_gpsp_rom_name(unsigned rom_id)
{
	if(rom_id < CGBA_GPSP_ROM_BUILTIN_COUNT)
		return cgba_rom_sources[rom_id].name;

	ensure_storage_roms_scanned();
	rom_id -= CGBA_GPSP_ROM_BUILTIN_COUNT;
	if(rom_id < cgba_storage_roms.count)
		return cgba_storage_roms.entries[rom_id].label;

	return cgba_rom_sources[CGBA_GPSP_ROM_LCD_TEST].name;
}

unsigned cgba_gpsp_rom_count(void)
{
	ensure_storage_roms_scanned();
	return CGBA_GPSP_ROM_BUILTIN_COUNT + cgba_storage_roms.count;
}

uint32_t fxcg100_rom_source_count(void)
{
	return cgba_gpsp_rom_count();
}

const char *fxcg100_rom_source_label(uint32_t index)
{
	return cgba_gpsp_rom_name(index);
}

int cgba_gpsp_init(uint16_t *framebuffer, unsigned rom_id)
{
	const cgba_rom_source *rom = NULL;
	const cgba_nor_rom_entry *nor_entry = NULL;
	int nor_result;

	if(!framebuffer)
		return -1;

	cgba_last_error[0] = 0;

	if(rom_id < CGBA_GPSP_ROM_BUILTIN_COUNT)
		rom = &cgba_rom_sources[rom_id];
	else {
		ensure_storage_roms_scanned();
		unsigned storage_id = rom_id - CGBA_GPSP_ROM_BUILTIN_COUNT;
		if(storage_id < cgba_storage_roms.count)
			nor_entry = &cgba_storage_roms.entries[storage_id];
	}
	if(!rom && !nor_entry) {
		rom_id = CGBA_GPSP_ROM_LCD_TEST;
		rom = &cgba_rom_sources[rom_id];
	}

	memset(framebuffer, 0, CGBA_GBA_BUFFER_PIXELS * sizeof(*framebuffer));
	gba_screen_pixels = framebuffer;
	cgba_active_framebuffer = framebuffer;
	cgba_lcd_test_active = rom ? rom->lcd_test : 0;
	cgba_mode3_debug_copy_active = rom ? rom->mode3_debug_copy : 0;
	cgba_loaded_rom_id = rom_id;

	if(rom && rom->lcd_test)
		return 0;

	init_gamepak_buffer();
	init_sound();
	memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));

	cgba_nor_rom_close(&cgba_current_nor_rom);
	if(nor_entry) {
		nor_result = cgba_nor_rom_open_path(&cgba_current_nor_rom,
			nor_entry->path);
		if(nor_result != 0) {
			cgba_nor_rom_status(&cgba_current_nor_rom,
				cgba_last_error, sizeof(cgba_last_error));
			return -3;
		}
		/* Fragmented pages are left unmapped by load_gamepak_from_pages and
		 * page-faulted on demand; point gpSP's page source at the NOR gather. */
		cgba_gpsp_filestream_bind(&cgba_current_nor_rom);
		gamepak_file_large = filestream_open(NULL, 0, 0);
		if(load_gamepak_from_pages(cgba_current_nor_rom.pages,
				cgba_current_nor_rom.padded_size,
				FEAT_AUTODETECT, FEAT_DISABLE,
				SERIAL_MODE_DISABLED) != 0) {
			snprintf(cgba_last_error, sizeof(cgba_last_error),
				"NOR gpSP map s%u ps%u p%u",
				(unsigned)cgba_current_nor_rom.size,
				(unsigned)cgba_current_nor_rom.padded_size,
				(unsigned)cgba_current_nor_rom.page_count);
			cgba_nor_rom_close(&cgba_current_nor_rom);
			return -4;
		}
	}
	else if(load_gamepak_from_memory(rom->data,
			rom->size,
			FEAT_AUTODETECT, FEAT_DISABLE,
			SERIAL_MODE_DISABLED) != 0)
		return -2;

	selected_boot_mode = boot_game;
	dynarec_enable = 1;   /* JIT on by default (SAVE_STATE hotkey still toggles) */
	sprite_limit = 1;
	reset_gba();
	cgba_gpsp_backup_load();   /* restore the game's in-cart save, if any */
	return 0;
}

const char *cgba_gpsp_last_error(void)
{
	return cgba_last_error[0] ? cgba_last_error : NULL;
}

#ifdef CGBA_GPSP_HEADLESS_TEST
static uint32_t cgba_fnv1a32(const void *data, uint32_t bytes)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t h = 2166136261u;

	for(uint32_t i = 0; i < bytes; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

static uint32_t cgba_fnv1a16_pixels(const uint16_t *data, uint32_t pixels)
{
	uint32_t h = 2166136261u;

	if(!data)
		return 0;
	for(uint32_t i = 0; i < pixels; i++) {
		uint16_t px = data[i];
		h ^= (uint8_t)(px >> 8);
		h *= 16777619u;
		h ^= (uint8_t)px;
		h *= 16777619u;
	}
	return h;
}

unsigned cgba_gpsp_state_lines(unsigned frame, const char *phase,
	const uint16_t *framebuffer, char out[][CGBA_STATE_LINE_MAX],
	unsigned max_lines)
{
	unsigned n = 0;

	if(!phase)
		phase = "?";
	if(n < max_lines)
		snprintf(out[n++], CGBA_STATE_LINE_MAX,
			"@@CGBA_STATE frame=%u phase=%s pc=%08lX cpsr=%08lX mode=%lu "
			"halt=%lu sleep=%08lX exec=%lu cpu=%lu video=%ld fc=%lu "
			"instr=%lu r0=%08lX r1=%08lX r2=%08lX r3=%08lX r4=%08lX "
			"r5=%08lX r6=%08lX r7=%08lX r8=%08lX r9=%08lX r10=%08lX "
			"r11=%08lX r12=%08lX sp=%08lX lr=%08lX",
			frame, phase, (unsigned long)reg[REG_PC],
			(unsigned long)reg[REG_CPSR], (unsigned long)reg[CPU_MODE],
			(unsigned long)reg[CPU_HALT_STATE],
			(unsigned long)reg[REG_SLEEP_CYCLES],
			(unsigned long)execute_cycles, (unsigned long)cpu_ticks,
			(long)video_count, (unsigned long)frame_counter,
			(unsigned long)instruction_count, (unsigned long)reg[0],
			(unsigned long)reg[1], (unsigned long)reg[2],
			(unsigned long)reg[3], (unsigned long)reg[4],
			(unsigned long)reg[5], (unsigned long)reg[6],
			(unsigned long)reg[7], (unsigned long)reg[8],
			(unsigned long)reg[9], (unsigned long)reg[10],
			(unsigned long)reg[11], (unsigned long)reg[12],
			(unsigned long)reg[REG_SP], (unsigned long)reg[REG_LR]);
	if(n < max_lines)
		snprintf(out[n++], CGBA_STATE_LINE_MAX,
			"@@CGBA_IO frame=%u phase=%s dispcnt=%04X dispstat=%04X "
			"vcount=%u p1=%04X ie=%04X if=%04X ime=%04X wait=%04X "
			"siocnt=%04X win0h=%04X win0v=%04X winin=%04X winout=%04X "
			"bldcnt=%04X bldalpha=%04X bldy=%04X irqcyc=%lu oamupd=%lu",
			frame, phase, read_ioreg(REG_DISPCNT),
			read_ioreg(REG_DISPSTAT), read_ioreg(REG_VCOUNT),
			read_ioreg(REG_P1), read_ioreg(REG_IE), read_ioreg(REG_IF),
			read_ioreg(REG_IME), read_ioreg(REG_WAITCNT),
			read_ioreg(REG_SIOCNT), read_ioreg(REG_WIN0H),
			read_ioreg(REG_WIN0V), read_ioreg(REG_WININ),
			read_ioreg(REG_WINOUT), read_ioreg(REG_BLDCNT),
			read_ioreg(REG_BLDALPHA), read_ioreg(REG_BLDY),
			(unsigned long)serial_get_irq_cycles(),
			(unsigned long)reg[OAM_UPDATED]);
	if(n < max_lines)
		snprintf(out[n++], CGBA_STATE_LINE_MAX,
			"@@CGBA_HASH frame=%u phase=%s iw=%08lX ew=%08lX vr=%08lX "
			"pal=%08lX pconv=%08lX oam=%08lX io=%08lX fb=%08lX",
			frame, phase,
			(unsigned long)cgba_fnv1a32(iwram + 0x8000, 0x8000),
			(unsigned long)cgba_fnv1a32(ewram, 0x40000),
			(unsigned long)cgba_fnv1a32(vram, 1024 * 96),
			(unsigned long)cgba_fnv1a32(palette_ram, sizeof palette_ram),
			(unsigned long)cgba_fnv1a32(palette_ram_converted,
				sizeof palette_ram_converted),
			(unsigned long)cgba_fnv1a32(oam_ram, sizeof oam_ram),
			(unsigned long)cgba_fnv1a32(io_registers, sizeof io_registers),
			(unsigned long)cgba_fnv1a16_pixels(framebuffer,
				CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT));
	if(n < max_lines)
		snprintf(out[n++], CGBA_STATE_LINE_MAX,
			"@@CGBA_TIMER frame=%u phase=%s t0c=%ld t0r=%lu t0p=%lu t0s=%lu "
			"t1c=%ld t1r=%lu t1p=%lu t1s=%lu t2c=%ld t2r=%lu t2p=%lu "
			"t2s=%lu t3c=%ld t3r=%lu t3p=%lu t3s=%lu",
			frame, phase, (long)timer[0].count,
			(unsigned long)timer[0].reload, (unsigned long)timer[0].prescale,
			(unsigned long)timer[0].status, (long)timer[1].count,
			(unsigned long)timer[1].reload, (unsigned long)timer[1].prescale,
			(unsigned long)timer[1].status, (long)timer[2].count,
			(unsigned long)timer[2].reload, (unsigned long)timer[2].prescale,
			(unsigned long)timer[2].status, (long)timer[3].count,
			(unsigned long)timer[3].reload, (unsigned long)timer[3].prescale,
			(unsigned long)timer[3].status);
	if(n < max_lines)
		snprintf(out[n++], CGBA_STATE_LINE_MAX,
			"@@CGBA_DMA frame=%u phase=%s d0s=%lu d0src=%08lX d0dst=%08lX "
			"d0len=%lu d1s=%lu d1src=%08lX d1dst=%08lX d1len=%lu "
			"d2s=%lu d2src=%08lX d2dst=%08lX d2len=%lu "
			"d3s=%lu d3src=%08lX d3dst=%08lX d3len=%lu",
			frame, phase, (unsigned long)dma[0].start_type,
			(unsigned long)dma[0].source_address,
			(unsigned long)dma[0].dest_address,
			(unsigned long)dma[0].length, (unsigned long)dma[1].start_type,
			(unsigned long)dma[1].source_address,
			(unsigned long)dma[1].dest_address,
			(unsigned long)dma[1].length, (unsigned long)dma[2].start_type,
			(unsigned long)dma[2].source_address,
			(unsigned long)dma[2].dest_address,
			(unsigned long)dma[2].length, (unsigned long)dma[3].start_type,
			(unsigned long)dma[3].source_address,
			(unsigned long)dma[3].dest_address,
			(unsigned long)dma[3].length);
	return n;
}
#endif

static void fill_lcd_test_frame(uint32_t frame)
{
	uint16_t *dst = cgba_active_framebuffer;

	if(!dst)
		return;

	for(unsigned y = 0; y < CGBA_GBA_HEIGHT; y++) {
		for(unsigned x = 0; x < CGBA_GBA_WIDTH; x++) {
			uint16_t color;
			if(x < CGBA_GBA_WIDTH / 3)
				color = 0xf800;
			else if(x < (CGBA_GBA_WIDTH * 2) / 3)
				color = 0x07e0;
			else
				color = 0x001f;

			if(((x >> 4) ^ (y >> 4) ^ frame) & 1)
				color ^= 0xffff;
			dst[y * CGBA_GBA_PITCH + x] = color;
		}
	}
}

static void copy_mode3_vram_to_framebuffer(void)
{
	uint16_t *dst = cgba_active_framebuffer;
	uint16_t *src = (uint16_t *)vram;
	uint16_t dispcnt = read_ioreg(REG_DISPCNT);

	if(!dst || (dispcnt & 0x07) != 3 || !(dispcnt & 0x0400))
		return;

	for(unsigned i = 0; i < CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT; i++) {
		uint16_t gba = eswap16(src[i]);
		dst[i] = convert_palette(gba);
	}
}

/* ---- savestates -----------------------------------------------------------
 * Raw gba_save_state() image (GBA_STATE_MEM_SIZE = 416KB), written whole to
 * \\fls0\CGBAST<slot>.SAV — byte-compatible with the headless checkpoint
 * blob, so a state saved on the calculator loads directly in the emulator
 * harness (copy it over USB into the HLE_FLS0 dir as CGBACHK.SAV).
 *
 * Staging buffer: borrow the already-reserved fragmented-ROM page cache while
 * guest execution is stopped.  Acquiring/releasing it invalidates only pages
 * backed by that cache; direct NOR mappings remain intact.  This keeps the
 * production high-RAM arena at the hardware-proven 0x8c655300 endpoint instead
 * of retaining the emulator-only 492 KiB differential snapshot.  JIT builds
 * use the ROM translation cache separately for the compressed stream. */
static u8 *cgba_state_buffer(void)
{
	return cgba_gamepak_scratch_acquire(GBA_STATE_MEM_SIZE);
}

static void cgba_state_buffer_release(void)
{
	cgba_gamepak_scratch_release();
}

#ifdef CGBA_DYNAREC
void flush_translation_cache_rom(void);
void flush_translation_cache_ram(void);
void flush_dynarec_caches(void);
extern u32 rom_cache_watermark;

static u8 *cgba_state_comp_buffer(void)
{
	return rom_translation_cache + rom_cache_watermark;
}

static unsigned cgba_state_comp_capacity(void)
{
	if (rom_cache_watermark >= ROM_TRANSLATION_CACHE_SIZE)
		return 0;
	return ROM_TRANSLATION_CACHE_SIZE - rom_cache_watermark;
}
#endif

/* ---- savestate compression (word-RLE) -------------------------------------
 * GBA state images are dominated by zero/constant runs (EWRAM, VRAM, OAM),
 * and BFile flash writes cost time proportional to bytes, so a trivial
 * 32-bit-word RLE cuts save/load time ~3-5x. Stream format:
 *   u32 CGBA_STATE_COMP_MAGIC, u32 raw_size, then tokens:
 *     hdr with bit31 set  -> run:     (hdr & 0x7FFFFFFF) copies of next u32
 *     hdr with bit31 clear-> literal: hdr words follow verbatim
 * Raw-size files (== GBA_STATE_MEM_SIZE) are still written/accepted, so old
 * saves and the emulator-harness CGBACHK.SAV workflow keep working. */
#define CGBA_STATE_COMP_MAGIC 0x435A5331u              /* 'CZS1' */

unsigned cgba_state_compress(const u8 *raw, unsigned rawsz, u8 *out,
			     unsigned cap)
{
	const u32 *w = (const u32 *)(const void *)raw;
	u32 *o = (u32 *)(void *)out;
	unsigned n = rawsz / 4, i = 0, ow = 0, ocap = cap / 4;

	if (ocap < 4)
		return 0;
	o[ow++] = CGBA_STATE_COMP_MAGIC;
	o[ow++] = rawsz;
	while (i < n) {
		unsigned run = 1;
		while (i + run < n && w[i + run] == w[i] && run < 0x7FFFFFFFu)
			run++;
		if (run >= 3) {
			if (ow + 2 > ocap)
				return 0;
			o[ow++] = 0x80000000u | run;
			o[ow++] = w[i];
			i += run;
		} else {
			unsigned lit = i, litn = 0;
			while (i < n) {           /* literals until the next run */
				unsigned r = 1;
				while (i + r < n && w[i + r] == w[i] && r < 3)
					r++;
				if (r >= 3 && i + r < n && w[i + r] == w[i])
					break;
				if (r >= 3)
					break;
				i += r; litn += r;
			}
			if (ow + 1 + litn > ocap)
				return 0;
			o[ow++] = litn;
			while (litn--)
				o[ow++] = w[lit++];
		}
	}
	return ow * 4;
}

int cgba_state_decompress(const u8 *in, unsigned insz, u8 *raw, unsigned rawsz)
{
	const u32 *o = (const u32 *)(const void *)in;
	u32 *w = (u32 *)(void *)raw;
	unsigned iw = 0, icap = insz / 4, n = rawsz / 4, i = 0;

	if (icap < 2 || o[0] != CGBA_STATE_COMP_MAGIC || o[1] != rawsz)
		return 0;
	iw = 2;
	while (i < n && iw < icap) {
		u32 hdr = o[iw++];
		if (hdr & 0x80000000u) {
			u32 run = hdr & 0x7FFFFFFFu, v;
			if (iw >= icap || i + run > n)
				return 0;
			v = o[iw++];
			while (run--)
				w[i++] = v;
		} else {
			if (iw + hdr > icap || i + hdr > n)
				return 0;
			while (hdr--)
				w[i++] = o[iw++];
		}
	}
	return i == n;
}

/* Per-ROM savestate names: "<BASE><slot>.SVS" where BASE = up to 6 filtered
 * chars of the loaded ROM's label, so different games keep separate slots.
 * Legacy fixed-name CGBAST<slot>.SAV files are still tried on load. */
static void cgba_state_path(uint16_t *path, unsigned slot)
{
	static const char prefix[] = "\\\\fls0\\";
	const char *nm = cgba_gpsp_rom_name(cgba_loaded_rom_id);
	char base[7];
	unsigned i, b = 0;

	for (i = 0; nm && nm[i] && nm[i] != '.' && b < 6; i++) {
		char c = nm[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			base[b++] = c;
	}
	if (b == 0) {
		base[0] = 'C'; base[1] = 'G'; base[2] = 'B'; base[3] = 'A';
		b = 4;
	}
	base[b] = 0;

	for (i = 0; prefix[i]; i++)
		path[i] = (uint16_t)prefix[i];
	for (b = 0; base[b]; b++)
		path[i++] = (uint16_t)base[b];
	path[i++] = (uint16_t)('0' + (slot % 10));
	path[i++] = '.'; path[i++] = 'S'; path[i++] = 'V'; path[i++] = 'S';
	path[i] = 0;
}

static void cgba_state_path_legacy(uint16_t *path, unsigned slot)
{
	static const char tmpl[] = "\\\\fls0\\CGBAST0.SAV";
	unsigned i;

	for (i = 0; tmpl[i]; i++)
		path[i] = (uint16_t)tmpl[i];
	path[i] = 0;
	path[13] = (uint16_t)('0' + (slot % 10));
}

#ifdef CGBA_DYNAREC
/* Compressed staging borrows the ROM cache, separate from the raw snapshot. */
_Static_assert(GBA_STATE_MEM_SIZE + 64 <= ROM_TRANSLATION_CACHE_SIZE,
	"comp area must fit the worst case (all-literal) stream");
#endif


int cgba_gpsp_state_save(unsigned slot)
{
	u8 *buf = cgba_state_buffer();
	uint16_t path[24];
	int ok = 0;

	if (!buf)
		return 0;

	cgba_state_path(path, slot);
#ifdef CGBA_DYNAREC
	/* Clear RAM SMC tags before serializing, and drop direct chains before the
	 * compression buffer borrows executable cache memory. */
	flush_dynarec_caches();
#endif
	gba_save_state(buf);
#ifdef CGBA_DYNAREC
	{
		u8 *comp = cgba_state_comp_buffer();
		unsigned cap = cgba_state_comp_capacity();
		unsigned csz = cgba_state_compress(buf, GBA_STATE_MEM_SIZE,
			comp, cap);
		if (csz && csz < GBA_STATE_MEM_SIZE) {
			/* Round the FILE up to 64KB buckets: repeat saves of
			 * similar size hit write_blob's fast overwrite path
			 * instead of delete+create; the decoder stops at
			 * raw_size so the slack tail is ignored. */
			unsigned fsz = (csz + 0xFFFFu) & ~0xFFFFu;
			if (fsz <= cap) {
				memset(comp + csz, 0, fsz - csz);
				ok = fxcg100_storage_write_blob(path, comp, fsz);
			}
		} else {
			ok = fxcg100_storage_write_blob(path, buf,
				GBA_STATE_MEM_SIZE);
		}
	}
	flush_dynarec_caches();            /* comp buffer was executable cache */
#else
	ok = fxcg100_storage_write_blob(path, buf, GBA_STATE_MEM_SIZE);
#endif
	cgba_state_buffer_release();
	return ok;
}

static int cgba_state_load_from(const uint16_t *path)
{
	u8 *buf = cgba_state_buffer();
	int fsz = fxcg100_storage_blob_size(path);
	int ok = 0;

	if (!buf)
		return 0;

	if (fsz == (int)GBA_STATE_MEM_SIZE) {   /* raw (legacy / harness) */
		ok = fxcg100_storage_read_blob(path, buf,
			GBA_STATE_MEM_SIZE) && gba_load_state(buf);
	}
#ifdef CGBA_DYNAREC
	else if (fsz > 8 && fsz <= (int)GBA_STATE_MEM_SIZE + 64) {
		u8 *comp = cgba_state_comp_buffer();
		unsigned cap = cgba_state_comp_capacity();
		if ((unsigned)fsz <= cap) {
			flush_dynarec_caches();    /* comp buffer borrows ROM cache */
			ok = fxcg100_storage_read_blob(path, comp, (unsigned)fsz) &&
				cgba_state_decompress(comp, (unsigned)fsz, buf,
						      GBA_STATE_MEM_SIZE) &&
				gba_load_state(buf);
		}
	}
#endif
	cgba_state_buffer_release();
	return ok;
}

int cgba_gpsp_state_load(unsigned slot)
{
	uint16_t path[24];
	int ok;

	cgba_state_path(path, slot);
	ok = cgba_state_load_from(path);
	if (!ok) {                              /* legacy fixed-name fallback */
		cgba_state_path_legacy(path, slot);
		ok = cgba_state_load_from(path);
	}
#ifdef CGBA_DYNAREC
	flush_translation_cache_rom();      /* clobbered even on a failed load */
	/* gba_load_state only flushes when dynarec_enable is set; a state loaded
	 * while interpreting (PROF core-switch hotkey) would otherwise leave the
	 * RAM cache holding translations of the PREVIOUS state's RAM code. */
	flush_translation_cache_ram();
#endif
	return ok;
}

/* ---- GBA in-game backup save (SRAM / Flash / EEPROM) persistence ---------- */
/* The live save lives in gpSP's gamepak_backup[]; it is loaded when a ROM boots
 * and written back to \\fls0\<BASE>.SAV (BASE = the ROM stem, matching the
 * savestate naming) when it changes. Nothing wrote it to storage before, so
 * in-game saves were lost on power-off. */
static void cgba_backup_path(uint16_t *path)
{
	static const char prefix[] = "\\\\fls0\\";
	const char *nm = cgba_gpsp_rom_name(cgba_loaded_rom_id);
	char base[7];
	unsigned i, b = 0;

	for (i = 0; nm && nm[i] && nm[i] != '.' && b < 6; i++) {
		char c = nm[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			base[b++] = c;
	}
	if (b == 0) {
		base[0] = 'G'; base[1] = 'A'; base[2] = 'M'; base[3] = 'E';
		b = 4;
	}
	base[b] = 0;

	for (i = 0; prefix[i]; i++)
		path[i] = (uint16_t)prefix[i];
	for (b = 0; base[b]; b++)
		path[i++] = (uint16_t)base[b];
	path[i++] = '.'; path[i++] = 'S'; path[i++] = 'A'; path[i++] = 'V';
	path[i] = 0;
}

/* Bytes of gamepak_backup the current game actually uses, by detected type. */
static unsigned cgba_backup_size(void)
{
	switch (backup_type) {
	case BACKUP_SRAM:   return 0x8000u;                            /* 32 KiB */
	case BACKUP_FLASH:  return (unsigned)flash_bank_cnt * 0x10000u;/* 64/128 KiB */
	case BACKUP_EEPROM: return (unsigned)eeprom_size * 512u;       /* 512 B / 8 KiB */
	default:            return 0u;                                 /* UNKN: unused */
	}
}

void cgba_gpsp_backup_load(void)
{
	uint16_t path[24];
	int fsz;

	cgba_backup_path(path);
	fsz = fxcg100_storage_blob_size(path);
	/* Load whatever the file holds; gpSP detects the type on first access and
	 * reads it straight from gamepak_backup, so the bytes just need to be there. */
	if (fsz > 0 && fsz <= (int)sizeof(gamepak_backup))
		fxcg100_storage_read_blob(path, gamepak_backup, (unsigned)fsz);
	cgba_backup_dirty = 0;
}

void cgba_gpsp_backup_flush(int force)
{
	unsigned sz = cgba_backup_size();
	uint16_t path[24];

	if (sz == 0)
		return;                       /* game never touched its backup */
	if (!force && !cgba_backup_dirty)
		return;                       /* nothing new since the last flush */
	cgba_backup_path(path);
	if (fxcg100_storage_write_blob(path, gamepak_backup, sz))
		cgba_backup_dirty = 0;
}

void cgba_gpsp_run_frame(uint32_t gba_buttons, int render_video)
{
	u32 cycles = (reg[CPU_HALT_STATE] == CPU_ACTIVE) ? execute_cycles : (u32)-64;

	if(cgba_lcd_test_active) {
		static uint32_t test_frame;
		fill_lcd_test_frame(test_frame++);
		return;
	}

	gpsp_set_input_state_bits(gba_buttons & 0x3ff);
	update_input();
	skip_next_frame = render_video ? 0 : 1;
	clear_gamepak_stickybits();
#ifdef CGBA_DYNAREC
	/* Live interp/dynarec toggle (subtask 2). The interpreter stays the default
	 * and correctness oracle; flip dynarec_enable to exercise the recompiler. */
	if(dynarec_enable) {
#if CGBA_GPSP_HEADLESS_TRACE_JIT
		trace_jit_state("before", cycles);
#endif
		execute_arm_translate(cycles);
#if CGBA_GPSP_HEADLESS_TRACE_JIT
		trace_jit_state("after", cycles);
#endif
	} else
#endif
		execute_arm(cycles);
	skip_next_frame = 0;
#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
	cgba_prof_window_frame();
#endif
	if(render_video && cgba_mode3_debug_copy_active)
		copy_mode3_vram_to_framebuffer();
}

#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_DIFF_HARNESS)
#include "sh4/sh4_diff_harness.h"

/* Run the differential interp-vs-dynarec harness for a short window and format
 * the first divergence (or agreement) into a one-line result. Invoked from the
 * menu / diagnostics so the dynarec can be validated on casio-emu or hardware
 * without a host oracle. */
int cgba_gpsp_diff_test(uint32_t cycles, char *out, unsigned out_len)
{
	cgba_diff_result r;
	int diverged = cgba_sh4_diff_run(cycles, &r);

	if(diverged)
		snprintf(out, out_len, "D %s%d i%lX d%lX p%lX>%lX/%lX",
			cgba_sh4_diff_kind_name(r.kind), r.index,
			(unsigned long)r.interp_value, (unsigned long)r.dynarec_value,
			(unsigned long)r.start_pc, (unsigned long)r.interp_pc,
			(unsigned long)r.dynarec_pc);
	else
		snprintf(out, out_len, "MATCH %lu p%lX>%lX",
			(unsigned long)cycles, (unsigned long)r.start_pc,
			(unsigned long)r.interp_pc);
	return diverged;
}
#endif

uint32_t cgba_gpsp_keyinput(void)
{
	return read_ioreg(REG_P1) & 0x3ff;
}

uint32_t cgba_gpsp_frame_hash(const uint16_t *pixels)
{
	uint32_t hash = 2166136261u;

	for(unsigned i = 0; i < CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT; i++) {
		uint16_t px = pixels[i];
		hash ^= (uint8_t)(px >> 8);
		hash *= 16777619u;
		hash ^= (uint8_t)px;
		hash *= 16777619u;
	}

	return hash;
}

static uint32_t mode_sp(uint32_t mode)
{
	return reg[CPU_MODE] == mode ? reg[REG_SP] : REG_MODE(mode)[5];
}

static uint32_t mode_lr(uint32_t mode)
{
	return reg[CPU_MODE] == mode ? reg[REG_LR] : REG_MODE(mode)[6];
}

void cgba_gpsp_debug_menu(fxcg100_debug_info *debug, unsigned frame,
	uint32_t last_hash, unsigned emu_fps, unsigned draw_fps,
	const uint16_t *framebuffer, uint32_t host_sp)
{
	const uint16_t *fb = framebuffer ? framebuffer : cgba_active_framebuffer;
	const cgba_nor_rom *r = &cgba_current_nor_rom;
	uint32_t rom_probe_page = 0;
	const uint8_t *rom_probe_ptr = NULL;
	const uint8_t *rom_map_ptr = NULL;
	uint16_t center = fb ?
		fb[(CGBA_GBA_HEIGHT / 2) * CGBA_GBA_PITCH +
			(CGBA_GBA_WIDTH / 2)] : 0;
	unsigned black = framebuffer_black_pixels(fb);

	if(!debug)
		return;
	memset(debug, 0, sizeof(*debug));
	debug_line(debug, "ROM %s", cgba_gpsp_rom_name(cgba_loaded_rom_id));
	debug_line(debug, "FRAME %u HASH %08lX", frame, (unsigned long)last_hash);
	debug_line(debug, "FPS EMU=%u DRAW=%u", emu_fps, draw_fps);
	debug_line(debug, "HOST SP=%08lX", (unsigned long)host_sp);
	debug_line(debug, "PC=%08lX CPSR=%08lX",
		(unsigned long)reg[REG_PC], (unsigned long)reg[REG_CPSR]);
	debug_line(debug, "MODE=%02lX HALT=%lu SLEEP=%08lX",
		(unsigned long)reg[CPU_MODE],
		(unsigned long)reg[CPU_HALT_STATE],
		(unsigned long)reg[REG_SLEEP_CYCLES]);
	debug_line(debug, "GBA SP=%08lX LR=%08lX",
		(unsigned long)reg[REG_SP], (unsigned long)reg[REG_LR]);
	debug_line(debug, "SVC SP=%08lX LR=%08lX",
		(unsigned long)mode_sp(MODE_SUPERVISOR),
		(unsigned long)mode_lr(MODE_SUPERVISOR));
	debug_line(debug, "IRQ SP=%08lX LR=%08lX",
		(unsigned long)mode_sp(MODE_IRQ),
		(unsigned long)mode_lr(MODE_IRQ));
	debug_line(debug, "R0=%08lX R1=%08lX",
		(unsigned long)reg[0], (unsigned long)reg[1]);
	debug_line(debug, "R2=%08lX R3=%08lX",
		(unsigned long)reg[2], (unsigned long)reg[3]);
	debug_line(debug, "IO DISP=%04X STAT=%04X VCNT=%u",
		read_ioreg(REG_DISPCNT), read_ioreg(REG_DISPSTAT),
		read_ioreg(REG_VCOUNT));
	debug_line(debug, "IRQ IE=%04X IF=%04X IME=%04X",
		read_ioreg(REG_IE), read_ioreg(REG_IF), read_ioreg(REG_IME));
	debug_line(debug, "KEYIN=%03lX WAIT=%04X OAM=%lu",
		(unsigned long)cgba_gpsp_keyinput(), read_ioreg(REG_WAITCNT),
		(unsigned long)reg[OAM_UPDATED]);
	debug_line(debug, "FB BLACK=%u/%u C=%04X",
		black, (unsigned)(CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT), center);
	debug_line(debug, "DMA3 ST=%lu LEN=%lu",
		(unsigned long)dma[3].start_type, (unsigned long)dma[3].length);
	debug_line(debug, "TMR0=%ld TMR1=%ld",
		(long)timer[0].count, (long)timer[1].count);
	debug_line(debug, "TMR2=%ld TMR3=%ld",
		(long)timer[2].count, (long)timer[3].count);
#ifdef CGBA_DYNAREC
	if(r->fd >= 0 && r->page_count > 0) {
		uint32_t p;

		for(p = 1; p < r->page_count; p++) {
			if(r->pages[p]) {
				rom_probe_page = p;
				break;
			}
		}
		rom_probe_ptr = r->pages[rom_probe_page];
		rom_map_ptr = memory_map_read[(0x08000000u >> 15) + rom_probe_page];
	}
	debug_line(debug, "CORE=%s ECYC=%lu",
		dynarec_enable ? "JIT" : "INT", (unsigned long)execute_cycles);
	debug_line(debug, "JIT ROM=%luk/%luk RAM=%luk/%luk",
		(unsigned long)((uintptr_t)rom_translation_ptr -
			(uintptr_t)rom_translation_cache) / 1024u,
		(unsigned long)ROM_TRANSLATION_CACHE_SIZE / 1024u,
		(unsigned long)((uintptr_t)ram_translation_ptr -
			(uintptr_t)ram_translation_cache) / 1024u,
		(unsigned long)RAM_TRANSLATION_CACHE_SIZE / 1024u);
	debug_line(debug, "ADDR TC=%08lX/%c RC=%08lX/%c",
		(unsigned long)(uintptr_t)rom_translation_cache,
		sh4_area_tag((uintptr_t)rom_translation_cache),
		(unsigned long)(uintptr_t)ram_translation_cache,
		sh4_area_tag((uintptr_t)ram_translation_cache));
	debug_line(debug, "ADDR REG=%08lX/%c STK=%08lX/%c",
		(unsigned long)(uintptr_t)reg, sh4_area_tag((uintptr_t)reg),
		(unsigned long)(uintptr_t)host_sp, sh4_area_tag((uintptr_t)host_sp));
	debug_line(debug, "ADDR ROM%lu=%08lX/%c MAP=%08lX/%c",
		(unsigned long)rom_probe_page,
		(unsigned long)(uintptr_t)rom_probe_ptr,
		sh4_area_tag((uintptr_t)rom_probe_ptr),
		(unsigned long)(uintptr_t)rom_map_ptr,
		sh4_area_tag((uintptr_t)rom_map_ptr));
	debug_line(debug, "NOR DP=%lu/%lu F=%08lX/%c",
		(unsigned long)r->direct_page_count, (unsigned long)r->page_count,
		(unsigned long)r->first_address, sh4_area_tag(r->first_address));
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
	{
		/* All figures below are 5-frame-window deltas (WV), not cumulative. */
		unsigned f = CGBA_PROF_WIN_FRAMES;
		uint32_t arm_helpers = (uint32_t)(WV(cgba_sh4_helper_arm_ldst_count) +
			WV(cgba_sh4_helper_arm_block_count) + WV(cgba_sh4_helper_arm_dp_count) +
			WV(cgba_sh4_helper_arm_mul_count) + WV(cgba_sh4_helper_arm_psr_count) +
			WV(cgba_sh4_helper_arm_swap_count));
		uint32_t thumb_helpers = (uint32_t)(WV(cgba_sh4_helper_thumb_ldst_count) +
			WV(cgba_sh4_helper_thumb_block_count) +
			WV(cgba_sh4_helper_thumb_shift_count) +
			WV(cgba_sh4_helper_thumb_dp_count));
		debug_line(debug, "JIT W5 FL R%lu M%lu TX A%lu T%lu",
			WV(cgba_dynarec_rom_flush_count),
			WV(cgba_dynarec_ram_flush_count),
			WV(cgba_dynarec_arm_translate_count),
			WV(cgba_dynarec_thumb_translate_count));
		debug_line(debug, "JIT TX/f A%lu T%lu H/f A%lu T%lu",
			(unsigned long)(WV(cgba_dynarec_arm_translate_count) / f),
			(unsigned long)(WV(cgba_dynarec_thumb_translate_count) / f),
			(unsigned long)(arm_helpers / f),
			(unsigned long)(thumb_helpers / f));
		debug_line(debug, "JIT LK A%lu T%lu D%lu IC%lu/%luk",
			WV(cgba_dynarec_lookup_arm_count),
			WV(cgba_dynarec_lookup_thumb_count),
			WV(cgba_dynarec_lookup_dual_count),
			WV(cgba_dynarec_icache_sync_count),
			(unsigned long)(WV(cgba_dynarec_icache_sync_bytes) / 1024u));
		debug_line(debug, "JIT IH hit A%lu T%lu DA%lu DT%lu",
			WV(cgba_dynarec_ibh_arm_hit_count),
			WV(cgba_dynarec_ibh_thumb_hit_count),
			WV(cgba_dynarec_ibh_dual_arm_hit_count),
			WV(cgba_dynarec_ibh_dual_thumb_hit_count));
		debug_line(debug, "JIT IH slow A%lu T%lu D%lu",
			WV(cgba_dynarec_ibh_arm_slow_count),
			WV(cgba_dynarec_ibh_thumb_slow_count),
			WV(cgba_dynarec_ibh_dual_slow_count));
		debug_line(debug, "JIT IH hot DA%lu DT%lu",
			WV(cgba_dynarec_ibh_dual_hot_arm_count),
			WV(cgba_dynarec_ibh_dual_hot_thumb_count));
		debug_line(debug, "PROF entries=%lu ovf=%lu",
			(unsigned long)cgba_sh4_prof_entry_count,
			(unsigned long)cgba_sh4_prof_overflow_count);
		{
			struct cgba_sh4_prof_row rows[5];
			unsigned pn = cgba_sh4_prof_top(rows, 5);
			for(unsigned pi = 0; pi < pn; pi++) {
				uint32_t key = rows[pi].key;
				char mode = (key & 1u) ? 'T' : 'A';
				uint32_t pc = key & 0x0fffffffu;
				if(mode == 'T')
					pc &= ~1u;
				if(key == 0xffffffffu) {
					debug_line(debug, "PROF OVF %lu",
						(unsigned long)rows[pi].count);
					} else {
						debug_line(debug, "PROF %c%08lX %lu",
							mode, (unsigned long)pc,
							(unsigned long)rows[pi].count);
						debug_prof_opcodes(debug, pc);
					}
				}
			}
		debug_line(debug, "H ARM ld%lu st%lu blk%lu dp%lu",
			WV(cgba_sh4_helper_arm_ldst_load_count),
			WV(cgba_sh4_helper_arm_ldst_store_count),
			WV(cgba_sh4_helper_arm_block_count),
			WV(cgba_sh4_helper_arm_dp_count));
		debug_line(debug, "H ARM mul%lu psr%lu swp%lu",
			WV(cgba_sh4_helper_arm_mul_count),
			WV(cgba_sh4_helper_arm_psr_count),
			WV(cgba_sh4_helper_arm_swap_count));
		debug_line(debug, "H TH ld%lu blk%lu sh%lu dp%lu",
			WV(cgba_sh4_helper_thumb_ldst_count),
			WV(cgba_sh4_helper_thumb_block_count),
			WV(cgba_sh4_helper_thumb_shift_count),
			WV(cgba_sh4_helper_thumb_dp_count));
		debug_line(debug, "H TLD l%lu s%lu ram%lu io%lu",
			WV(cgba_sh4_helper_thumb_ldst_load_count),
			WV(cgba_sh4_helper_thumb_ldst_store_count),
			WV(cgba_sh4_helper_thumb_ldst_ram_count),
			WV(cgba_sh4_helper_thumb_ldst_io_count));
		debug_line(debug, "H TLD vid%lu rom%lu oth%lu",
			WV(cgba_sh4_helper_thumb_ldst_video_count),
			WV(cgba_sh4_helper_thumb_ldst_rom_count),
			WV(cgba_sh4_helper_thumb_ldst_other_count));
		debug_line(debug, "H TWHY U%lu GA%lu HA%lu",
			WV(cgba_sh4_helper_thumb_ldst_unmapped_count),
			WV(cgba_sh4_helper_thumb_ldst_guest_unaligned_count),
			WV(cgba_sh4_helper_thumb_ldst_host_unaligned_count));
		debug_line(debug, "H TWHY R%lu S%lu OK%lu",
			WV(cgba_sh4_helper_thumb_ldst_unsafe_region_count),
			WV(cgba_sh4_helper_thumb_ldst_smc_count),
			WV(cgba_sh4_helper_thumb_ldst_native_ready_count));
		debug_line(debug, "H TK W%lu B%lu H%lu",
			WV(cgba_sh4_helper_thumb_ldst_word_count),
			WV(cgba_sh4_helper_thumb_ldst_byte_count),
			WV(cgba_sh4_helper_thumb_ldst_half_count));
		debug_line(debug, "H TS PC%lu SP%lu R%lu I%lu",
			WV(cgba_sh4_helper_thumb_ldst_pc_count),
			WV(cgba_sh4_helper_thumb_ldst_sp_count),
			WV(cgba_sh4_helper_thumb_ldst_reg_count),
			WV(cgba_sh4_helper_thumb_ldst_imm_count));
		debug_line(debug, "H TF C%lu R%lu P%lu",
			WV(cgba_sh4_native_thumb_const_io_count),
			WV(cgba_sh4_native_thumb_runtime_io_count),
			WV(cgba_sh4_native_thumb_push_iwram_count));
		debug_line(debug, "H LD ram%lu io%lu vid%lu rom%lu oth%lu",
			WV(cgba_sh4_helper_arm_ldst_ram_count),
			WV(cgba_sh4_helper_arm_ldst_io_count),
			WV(cgba_sh4_helper_arm_ldst_video_count),
			WV(cgba_sh4_helper_arm_ldst_rom_count),
			WV(cgba_sh4_helper_arm_ldst_other_count));
		debug_line(debug, "H ABLK load%lu store%lu div%lu",
			WV(cgba_sh4_helper_arm_block_load_count),
			WV(cgba_sh4_helper_arm_block_store_count),
			WV(cgba_sh4_helper_hle_div_count));
		/* Interpreter-fallback residency: n calls / guest kilocycles in the
		 * window. All-zero JIT lines + big BIOS numbers = interpreted BIOS
		 * (soft-reset boot screen, IntrWait) — not a JIT stall. */
		debug_line(debug, "H BIOS n%lu kc%lu",
			WV(cgba_sh4_bios_fallback_call_count),
			(unsigned long)(WV(cgba_sh4_bios_fallback_cycle_count) / 1024u));
	}
#endif
#else
	debug_line(debug, "CORE=INTERPRETER");
#endif
}

unsigned cgba_gpsp_diag(char out[][CGBA_DIAG_LINE_MAX], unsigned max_lines)
{
	extern u32 reg[64];   /* gpSP ARM register file; reg[15] = PC */
	const cgba_nor_rom *r = &cgba_current_nor_rom;
	const uint8_t *rp = (r->fd >= 0 && r->page_count > 0) ? r->pages[0] : NULL;
	const uint16_t *fb = cgba_active_framebuffer;
	unsigned n = 0;

	if(n < max_lines)
		snprintf(out[n++], CGBA_DIAG_LINE_MAX,
			"load err=%d open=%d size=%d blk=%d",
			r->last_error, r->open_result, r->size_result, r->block_result);
	if(n < max_lines)
		snprintf(out[n++], CGBA_DIAG_LINE_MAX,
			"nor=%08lX pg=%lu dpg=%lu fb=%d",
			(unsigned long)r->first_address, (unsigned long)r->page_count,
			(unsigned long)r->direct_page_count, r->fallback_used);
	if(n < max_lines) {
		if(rp)
			snprintf(out[n++], CGBA_DIAG_LINE_MAX,
				"rom %02X %02X %02X %02X %02X %02X %02X %02X",
				rp[0], rp[1], rp[2], rp[3], rp[4], rp[5], rp[6], rp[7]);
		else
			snprintf(out[n++], CGBA_DIAG_LINE_MAX, "rom: <not mapped>");
	}
	if(n < max_lines)
		snprintf(out[n++], CGBA_DIAG_LINE_MAX,
			"PC=%08lX DISPCNT=%04X VCNT=%lu",
			(unsigned long)reg[15], (unsigned)read_ioreg(REG_DISPCNT),
			(unsigned long)read_ioreg(REG_VCOUNT));
	if(n < max_lines)
		snprintf(out[n++], CGBA_DIAG_LINE_MAX,
			"fbhash=%08lX center=%04X",
			(unsigned long)(fb ? cgba_gpsp_frame_hash(fb) : 0u),
			fb ? fb[80 * CGBA_GBA_PITCH + 120] : 0);
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_DIFF_HARNESS)
	/* One-frame interp-vs-dynarec diff so the diag overlay surfaces dynarec health
	 * on hardware/casio-emu: PCs + first divergent reg, then per-region (IWRAM /
	 * EWRAM / VRAM / IO) so a benign sound-buffer-only IWRAM diff is distinguishable
	 * at a glance from a real CPU/display divergence. */
	if(n < max_lines)
		n += cgba_sh4_diff_regions(280896, out + n, max_lines - n);
#endif
	return n;
}

void cgba_gpsp_shutdown(void)
{
	cgba_gpsp_backup_flush(0);   /* persist the game's save before teardown */
	if(!cgba_lcd_test_active)
		memory_term();
	cgba_nor_rom_close(&cgba_current_nor_rom);
	cgba_lcd_test_active = 0;
	cgba_mode3_debug_copy_active = 0;
	cgba_active_framebuffer = NULL;
}
