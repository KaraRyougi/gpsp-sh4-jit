#include <gint/display.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "frame_pacing.h"
#include "gpsp_runner.h"
#ifdef CGBA_DYNAREC
#include "sh4/sh4_diff_harness.h"

/* AUTOMATIC backup-save flush cadence (guest frames). ~10s at 60fps; only
 * writes NOR when the save is dirty, so idle play never touches flash. */
#define CGBA_BACKUP_AUTO_FRAMES 600u

extern int dynarec_enable;   /* gpSP: 0 = interpreter, 1 = SH4 recompiler */
extern uint32_t execute_cycles;
extern uint32_t reg[64];
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
extern uint32_t cgba_dynarec_rom_flush_count;
extern uint32_t cgba_sh4_bios_fallback_call_count;
extern uint32_t cgba_sh4_bios_fallback_cycle_count;
extern uint32_t cgba_dynarec_cold_interp_count;
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
extern void cgba_sh4_prof_reset(void);
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
extern uint32_t cgba_sh4_thumb_io16_store_count[512];
extern uint32_t cgba_sh4_dma_ctl_count[4];
extern uint32_t cgba_sh4_dma_ctl_enable_count[4];
extern uint32_t cgba_sh4_dma_ctl_value[4][4];
extern uint32_t cgba_sh4_dma_ctl_value_count[4][4];
extern uint32_t cgba_sh4_hle_div_zero_count;
extern uint32_t cgba_sh4_hle_div_one_count;
extern uint32_t cgba_sh4_hle_div_neg_one_count;
extern uint32_t cgba_sh4_hle_div_pow2_count;
extern uint32_t cgba_sh4_hle_div_other_count;
extern uint32_t cgba_sh4_prof_entry_count;
extern uint32_t cgba_sh4_prof_overflow_count;
extern uint32_t read_memory16(uint32_t address);
struct cgba_sh4_prof_row {
	uint32_t key;
	uint32_t count;
};
extern unsigned cgba_sh4_prof_top(struct cgba_sh4_prof_row *out, unsigned max);
#endif
#endif

/* Menu frameskip types (order matches frameskip_options[] in fxcg100_menu.c). */
#define CGBA_FRAMESKIP_AUTOMATIC 0
#define CGBA_FAST_FORWARD_RENDER_PERIOD 8u

#define CGBA_HIGH_BSS __attribute__((section(".cgba.highbss"), aligned(32)))
#define CGBA_HIGHRAM_SAFE_START ((uintptr_t)0x8c200000u)
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c780000u)

extern char cgba_highbss_start[];
extern char cgba_highbss_end[];

/* 4-byte aligned: the scaled presenters read rows as packed u32 pairs. */
static uint16_t cgba_framebuffer[CGBA_GBA_BUFFER_PIXELS] CGBA_HIGH_BSS
	__attribute__((aligned(4)));

/* FPS metrics meter (emulated + drawn frame rate), shown when the menu's
 * "SHOW FPS" option is on. Reset on each gameplay entry. */
static cgba_fps_meter cgba_fps;

static void wait_briefly(void)
{
	for(volatile unsigned i = 0; i < 60000000; i++)
		;
}

static uint32_t read_stack_pointer(void)
{
	uint32_t sp;

#if defined(__sh__)
	__asm__ volatile("mov r15, %0" : "=r"(sp));
#else
	sp = (uint32_t)(uintptr_t)&sp;
#endif
	return sp;
}

static int gpsp_highbss_range_ok(void)
{
	uintptr_t start = (uintptr_t)cgba_highbss_start;
	uintptr_t end = (uintptr_t)cgba_highbss_end;

	return start >= CGBA_HIGHRAM_SAFE_START &&
		end > start &&
		end <= CGBA_HIGHRAM_SAFE_END;
}

static void clear_gpsp_highbss(void)
{
	memset(cgba_highbss_start, 0,
		(size_t)(cgba_highbss_end - cgba_highbss_start));
}

static void draw_status(const char *line1, const char *line2)
{
	dclear(C_WHITE);
	drect(0, 0, DWIDTH - 1, 21, C_RGB(0, 10, 22));
	dtext(8, 5, C_WHITE, "gpSP");
	dtext(16, 44, C_BLACK, line1);
	if(line2)
		dtext(16, 62, C_BLACK, line2);
	fxcg100_lcd_update();   /* restores the full gint window then dupdate() */
}

static void blit_gba_frame(const uint16_t *pixels, unsigned frame,
	uint32_t gba_buttons)
{
	(void)frame;
	(void)gba_buttons;
	fxcg100_lcd_blit_gba(pixels);
}

static void enter_gameplay_display(const uint16_t *framebuffer, unsigned frame)
{
	cgba_fps_init(&cgba_fps);
	fxcg100_lcd_clear(C_BLACK);
	fxcg100_lcd_update();
	blit_gba_frame(framebuffer, frame, FXCG100_GBA_BUTTON_NONE);
}

static void wait_status(void)
{
	for(volatile unsigned i = 0; i < 12000000; i++)
		;
}

static void wait_for_keys_released(void)
{
	for(unsigned poll = 0; poll < 20000; poll++) {
		if(fxcg100_poll_app_keys() == 0)
			break;
		for(volatile unsigned i = 0; i < 2000; i++)
			;
	}
}

static int exit_to_os(int code)
{
	fxcg100_lcd_shutdown();
	return code;
}

static unsigned normalize_rom_id(unsigned rom_id)
{
	unsigned count = cgba_gpsp_rom_count();

	return count ? rom_id % count : 0;
}

static int start_gpsp(uint16_t *framebuffer, unsigned rom_id)
{
	draw_status("clearing gpSP memory", NULL);

	if(!gpsp_highbss_range_ok()) {
		draw_status("unsafe gpSP RAM map", "not starting");
		wait_briefly();
		return -1;
	}

	clear_gpsp_highbss();

	draw_status("loading ROM", cgba_gpsp_rom_name(rom_id));
	if(cgba_gpsp_init(framebuffer, rom_id) != 0) {
		const char *detail = cgba_gpsp_last_error();
		draw_status("ROM load failed",
			detail ? detail : cgba_gpsp_rom_name(rom_id));
		wait_briefly();
		return -1;
	}

#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
	cgba_dynarec_rom_flush_count = 0;
	cgba_dynarec_ram_flush_count = 0;
	cgba_dynarec_arm_translate_count = 0;
	cgba_dynarec_thumb_translate_count = 0;
	cgba_dynarec_lookup_arm_count = 0;
	cgba_dynarec_lookup_thumb_count = 0;
	cgba_dynarec_lookup_dual_count = 0;
	cgba_dynarec_icache_sync_count = 0;
	cgba_dynarec_icache_sync_bytes = 0;
	cgba_dynarec_ibh_arm_hit_count = 0;
	cgba_dynarec_ibh_arm_slow_count = 0;
	cgba_dynarec_ibh_thumb_hit_count = 0;
	cgba_dynarec_ibh_thumb_slow_count = 0;
	cgba_dynarec_ibh_dual_arm_hit_count = 0;
	cgba_dynarec_ibh_dual_thumb_hit_count = 0;
	cgba_dynarec_ibh_dual_slow_count = 0;
	cgba_dynarec_ibh_dual_hot_arm_count = 0;
	cgba_dynarec_ibh_dual_hot_thumb_count = 0;
	{
		extern u32 cgba_bios_hle_irq_in, cgba_bios_hle_irq_out;
		extern u32 cgba_bios_hle_swi_count;
		cgba_bios_hle_irq_in = 0;
		cgba_bios_hle_irq_out = 0;
		cgba_bios_hle_swi_count = 0;
	}
	cgba_sh4_prof_reset();
	cgba_sh4_helper_thumb_ldst_count = 0;
	cgba_sh4_helper_thumb_block_count = 0;
	cgba_sh4_helper_thumb_shift_count = 0;
	cgba_sh4_helper_thumb_dp_count = 0;
	cgba_sh4_helper_arm_ldst_count = 0;
	cgba_sh4_helper_arm_block_count = 0;
	cgba_sh4_helper_arm_dp_count = 0;
	cgba_sh4_helper_arm_mul_count = 0;
	cgba_sh4_helper_arm_psr_count = 0;
	cgba_sh4_helper_arm_swap_count = 0;
	cgba_sh4_helper_hle_div_count = 0;
	cgba_sh4_helper_arm_ldst_load_count = 0;
	cgba_sh4_helper_arm_ldst_store_count = 0;
	cgba_sh4_helper_arm_ldst_ram_count = 0;
	cgba_sh4_helper_arm_ldst_io_count = 0;
	cgba_sh4_helper_arm_ldst_video_count = 0;
	cgba_sh4_helper_arm_ldst_rom_count = 0;
	cgba_sh4_helper_arm_ldst_other_count = 0;
	cgba_sh4_helper_arm_block_load_count = 0;
	cgba_sh4_helper_arm_block_store_count = 0;
	cgba_sh4_helper_thumb_ldst_load_count = 0;
	cgba_sh4_helper_thumb_ldst_store_count = 0;
	cgba_sh4_helper_thumb_ldst_ram_count = 0;
	cgba_sh4_helper_thumb_ldst_io_count = 0;
	cgba_sh4_helper_thumb_ldst_video_count = 0;
	cgba_sh4_helper_thumb_ldst_rom_count = 0;
	cgba_sh4_helper_thumb_ldst_other_count = 0;
	cgba_sh4_helper_thumb_ldst_unmapped_count = 0;
	cgba_sh4_helper_thumb_ldst_guest_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_host_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_unsafe_region_count = 0;
	cgba_sh4_helper_thumb_ldst_smc_count = 0;
	cgba_sh4_helper_thumb_ldst_native_ready_count = 0;
	cgba_sh4_helper_thumb_ldst_word_count = 0;
	cgba_sh4_helper_thumb_ldst_byte_count = 0;
	cgba_sh4_helper_thumb_ldst_half_count = 0;
	cgba_sh4_helper_thumb_ldst_pc_count = 0;
	cgba_sh4_helper_thumb_ldst_sp_count = 0;
	cgba_sh4_helper_thumb_ldst_reg_count = 0;
	cgba_sh4_helper_thumb_ldst_imm_count = 0;
	cgba_sh4_native_thumb_const_io_count = 0;
	cgba_sh4_native_thumb_runtime_io_count = 0;
	cgba_sh4_native_thumb_push_iwram_count = 0;
	memset(cgba_sh4_thumb_io16_store_count, 0,
		sizeof(uint32_t) * 512u);
	memset(cgba_sh4_dma_ctl_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_enable_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_value, 0, sizeof(uint32_t) * 16u);
	memset(cgba_sh4_dma_ctl_value_count, 0, sizeof(uint32_t) * 16u);
	cgba_sh4_hle_div_zero_count = 0;
	cgba_sh4_hle_div_one_count = 0;
	cgba_sh4_hle_div_neg_one_count = 0;
	cgba_sh4_hle_div_pow2_count = 0;
	cgba_sh4_hle_div_other_count = 0;
#endif
	return 0;
}

#ifdef CGBA_GPSP_DIAG
/* Hardware debug overlay: run a few frames so gpSP boots, then report the
 * load/ROM/CPU/framebuffer state through gint's (proven) dtext path and hold
 * until EXE. Set the CGBA_GPSP_DIAG CMake option OFF once the bug is found. */
static void show_diag_overlay(void)
{
	static char lines[20][CGBA_DIAG_LINE_MAX];
	unsigned n, i;

	/* Render the final frame (render_video=1): update_scanline() early-returns
	 * while skip_next_frame is set, so a skipped run leaves the framebuffer stale
	 * and fbhash/center would read blank. */
	for(i = 0; i < 30; i++)
		cgba_gpsp_run_frame(FXCG100_GBA_BUTTON_NONE, i == 29);

	n = cgba_gpsp_diag(lines, 20);
	dclear(C_WHITE);
	dtext(2, 2, C_BLACK, "gpSP DIAG  (EXE = continue)");
	for(i = 0; i < n; i++)
		dtext(2, 22 + 16 * (int)i, C_BLACK, lines[i]);
	dupdate();

	while(!(fxcg100_poll_app_keys() & FXCG100_APPKEY_EXE))
		for(volatile unsigned z = 0; z < 100000; z++)
			;
	wait_for_keys_released();
}
#endif

#ifdef CGBA_GPSP_HEADLESS_TEST
#ifndef CGBA_GPSP_HEADLESS_FRAMES
#define CGBA_GPSP_HEADLESS_FRAMES 48u
#endif

#ifndef CGBA_GPSP_HEADLESS_FRAME_BASE
#define CGBA_GPSP_HEADLESS_FRAME_BASE 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_LOG_EVERY
#define CGBA_GPSP_HEADLESS_LOG_EVERY 1u
#endif

#ifndef CGBA_GPSP_HEADLESS_START_FRAME
#define CGBA_GPSP_HEADLESS_START_FRAME 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_START_HOLD
#define CGBA_GPSP_HEADLESS_START_HOLD 0u
#endif
/* START mashing (menu open/close stress): within [START_FRAME, START_FRAME +
 * START_HOLD), press START for START_PRESS frames every START_PERIOD frames
 * instead of holding it. 0 = plain hold (existing behavior). */
#ifndef CGBA_GPSP_HEADLESS_START_PERIOD
#define CGBA_GPSP_HEADLESS_START_PERIOD 0u
#endif
#ifndef CGBA_GPSP_HEADLESS_START_PRESS
#define CGBA_GPSP_HEADLESS_START_PRESS 2u
#endif

#ifndef CGBA_GPSP_HEADLESS_A_FRAME
#define CGBA_GPSP_HEADLESS_A_FRAME 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_A_HOLD
#define CGBA_GPSP_HEADLESS_A_HOLD 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_A_PERIOD
#define CGBA_GPSP_HEADLESS_A_PERIOD 12u
#endif

#ifndef CGBA_GPSP_HEADLESS_A_PRESS
#define CGBA_GPSP_HEADLESS_A_PRESS 2u
#endif

#ifndef CGBA_GPSP_HEADLESS_DUMP_EVERY
#define CGBA_GPSP_HEADLESS_DUMP_EVERY 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_STAT_EVERY
#define CGBA_GPSP_HEADLESS_STAT_EVERY 0u
#endif

/* Render 1 frame in (FRAMESKIP+1): deep-progression soaks (reaching real
 * gameplay needs ~17000+ frames of held A) spend most emulator time in the
 * renderer otherwise. Stat-checkpoint frames always render so the pixel-hash
 * comparisons stay valid. Default 3 = the old hardcoded (frame %% 4) == 0. */
#ifndef CGBA_GPSP_HEADLESS_FRAMESKIP
#define CGBA_GPSP_HEADLESS_FRAMESKIP 3u
#endif

/* In-world movement (user recipe): from RUN_FRAME on, hold LEFT so the
 * character actually runs through rooms instead of standing at the ship,
 * flipping direction every RUN_FLIP frames to bounce between walls; the
 * A-press cadence keeps firing/confirming on top. 0 = off. */
#ifndef CGBA_GPSP_HEADLESS_RUN_FRAME
#define CGBA_GPSP_HEADLESS_RUN_FRAME 0u
#endif
#ifndef CGBA_GPSP_HEADLESS_RUN_FLIP
#define CGBA_GPSP_HEADLESS_RUN_FLIP 900u
#endif

#ifndef CGBA_GPSP_HEADLESS_STATE_EVERY
#define CGBA_GPSP_HEADLESS_STATE_EVERY 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_STATE_START
#define CGBA_GPSP_HEADLESS_STATE_START 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_STATE_END
#define CGBA_GPSP_HEADLESS_STATE_END 0xffffffffu
#endif

#ifndef CGBA_GPSP_HEADLESS_PHASE_START
#define CGBA_GPSP_HEADLESS_PHASE_START 0xffffffffu
#endif

#ifndef CGBA_GPSP_HEADLESS_PHASE_END
#define CGBA_GPSP_HEADLESS_PHASE_END 0xffffffffu
#endif

#ifndef CGBA_GPSP_HEADLESS_DYNAREC
#define CGBA_GPSP_HEADLESS_DYNAREC -1
#endif

#ifndef CGBA_GPSP_HEADLESS_DIFF_FRAME
#define CGBA_GPSP_HEADLESS_DIFF_FRAME -1
#endif

#ifndef CGBA_GPSP_HEADLESS_DIFF_BLOCKS
#define CGBA_GPSP_HEADLESS_DIFF_BLOCKS 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME
#define CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME -1
#endif

#ifndef CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME
#define CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME -1
#endif
/* Alternating input: from ALT_FRAME on, tap A at the start of even 60-frame
 * windows and START (or LEFT with CGBA_GPSP_HEADLESS_ALT_LEFT) at the start of
 * odd ones (window length = ALT_PERIOD, tap length = ALT_PRESS). 0 = off. */
#ifndef CGBA_GPSP_HEADLESS_ALT_PERIOD
#define CGBA_GPSP_HEADLESS_ALT_PERIOD 0u
#endif
#ifndef CGBA_GPSP_HEADLESS_ALT_PRESS
#define CGBA_GPSP_HEADLESS_ALT_PRESS 2u
#endif
#ifndef CGBA_GPSP_HEADLESS_ALT_FRAME
#define CGBA_GPSP_HEADLESS_ALT_FRAME 60u
#endif
#ifndef CGBA_GPSP_HEADLESS_ALT_LEFT
#define CGBA_GPSP_HEADLESS_ALT_LEFT 0
#endif
#ifndef CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME
#define CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME -1
#endif

#ifndef CGBA_GPSP_HEADLESS_LOAD_STATE
#define CGBA_GPSP_HEADLESS_LOAD_STATE 0
#endif

#ifndef CGBA_GPSP_HEADLESS_BENCH_FRAMES
#define CGBA_GPSP_HEADLESS_BENCH_FRAMES 0u
#endif

/* Emulator-only validation: skip the menu, auto-boot the first storage ROM, run
 * a few frames, and stream cgba_gpsp_diag() to the host via the emulator's
 * 0xb7000000 debug-putchar port. Lets run-headless.sh confirm the NOR load /
 * gather path without KEYSC key injection. Never compiled into shipping builds. */
static void hputc_dbg(char c)
{
	*(volatile unsigned char *)0xb7000000u = (unsigned char)c;
}

static void hputs_dbg(const char *s)
{
	while(*s)
		hputc_dbg(*s++);
	hputc_dbg('\n');
}

#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
static void cgba_print_thumb_io16_top(void)
{
	int used[8];
	char buf[96];

	for(int slot = 0; slot < 8; slot++) {
		uint32_t best = 0;
		int best_i = -1;

		used[slot] = -1;
		for(int i = 0; i < 512; i++) {
			int seen = 0;
			for(int j = 0; j < slot; j++)
				if(used[j] == i)
					seen = 1;
			if(!seen && cgba_sh4_thumb_io16_store_count[i] > best) {
				best = cgba_sh4_thumb_io16_store_count[i];
				best_i = i;
			}
		}
		if(best_i < 0 || best == 0)
			break;
		used[slot] = best_i;
		snprintf(buf, sizeof buf, "jit thumb io16 #%d reg=%03X n=%lu",
			slot, best_i * 2, (unsigned long)best);
		hputs_dbg(buf);
	}
}

static void cgba_print_prof_top(void)
{
	struct cgba_sh4_prof_row rows[8];
	char buf[96];
	unsigned n;

	snprintf(buf, sizeof buf, "jit prof entries=%lu ovf=%lu",
		(unsigned long)cgba_sh4_prof_entry_count,
		(unsigned long)cgba_sh4_prof_overflow_count);
	hputs_dbg(buf);
	n = cgba_sh4_prof_top(rows, 8);
	for(unsigned i = 0; i < n; i++) {
		uint32_t key = rows[i].key;
		if(key == 0xffffffffu) {
			snprintf(buf, sizeof buf, "jit prof #%u ovf n=%lu",
				i, (unsigned long)rows[i].count);
		}
		else {
			char mode = (key & 1u) ? 'T' : 'A';
			uint32_t pc = key & 0x0fffffffu;
			if(mode == 'T')
				pc &= ~1u;
			snprintf(buf, sizeof buf,
				"jit prof #%u %c%08lX n=%lu op=%04lX %04lX %04lX %04lX",
				i, mode, (unsigned long)pc,
				(unsigned long)rows[i].count,
				(unsigned long)(read_memory16(pc) & 0xffffu),
				(unsigned long)(read_memory16(pc + 2) & 0xffffu),
				(unsigned long)(read_memory16(pc + 4) & 0xffffu),
				(unsigned long)(read_memory16(pc + 6) & 0xffffu));
		}
		hputs_dbg(buf);
	}
}
#endif

static void hput_hex4_dbg(uint16_t value)
{
	static const char hex[] = "0123456789ABCDEF";

	hputc_dbg(hex[(value >> 12) & 0x0f]);
	hputc_dbg(hex[(value >> 8) & 0x0f]);
	hputc_dbg(hex[(value >> 4) & 0x0f]);
	hputc_dbg(hex[value & 0x0f]);
}

static void hput_hex2_dbg(uint8_t value)
{
	static const char hex[] = "0123456789ABCDEF";

	hputc_dbg(hex[(value >> 4) & 0x0f]);
	hputc_dbg(hex[value & 0x0f]);
}

static unsigned headless_frame_base(void)
{
	return (unsigned)CGBA_GPSP_HEADLESS_FRAME_BASE;
}

static unsigned headless_frame_end(void)
{
	return headless_frame_base() + (unsigned)CGBA_GPSP_HEADLESS_FRAMES;
}

static int headless_last_frame(unsigned frame)
{
	return frame + 1u == headless_frame_end();
}

#ifdef CGBA_DYNAREC
#define CGBA_HEADLESS_BFILE_FILE 1
#define CGBA_HEADLESS_BFILE_READ_ONLY 0x01
#define CGBA_HEADLESS_BFILE_WRITE_ONLY 0x02

#define CGBA_HEADLESS_BFILE_OPEN   ((uintptr_t)0x803338d0u)
#define CGBA_HEADLESS_BFILE_SIZE   ((uintptr_t)0x80333b04u)
#define CGBA_HEADLESS_BFILE_CREATE ((uintptr_t)0x80333ef0u)
#define CGBA_HEADLESS_BFILE_REMOVE ((uintptr_t)0x80334212u)
#define CGBA_HEADLESS_BFILE_READ   ((uintptr_t)0x80333dc2u)
#define CGBA_HEADLESS_BFILE_WRITE  ((uintptr_t)0x80333f9eu)
#define CGBA_HEADLESS_BFILE_CLOSE  ((uintptr_t)0x80333a4eu)

typedef int (*cgba_headless_bfile_open_t)(const uint16_t *path, int mode);
typedef int (*cgba_headless_bfile_size_t)(int fd);
typedef int (*cgba_headless_bfile_create_t)(const uint16_t *path, int type,
	int *size);
typedef int (*cgba_headless_bfile_remove_t)(const uint16_t *path);
typedef int (*cgba_headless_bfile_read_t)(int fd, void *dst, int size,
	int offset);
typedef int (*cgba_headless_bfile_write_t)(int fd, const void *src, int size);
typedef int (*cgba_headless_bfile_close_t)(int fd);

static const uint16_t headless_checkpoint_path[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'C', 'G', 'B', 'A', 'C', 'H', 'K', '.', 'S', 'A', 'V', 0
};

static int headless_bfile_open(const uint16_t *path, int mode)
{
	cgba_headless_bfile_open_t fn =
		(cgba_headless_bfile_open_t)CGBA_HEADLESS_BFILE_OPEN;
	return fn(path, mode);
}

static int headless_bfile_size(int fd)
{
	cgba_headless_bfile_size_t fn =
		(cgba_headless_bfile_size_t)CGBA_HEADLESS_BFILE_SIZE;
	return fn(fd);
}

static int headless_bfile_create(const uint16_t *path, int type, int *size)
{
	cgba_headless_bfile_create_t fn =
		(cgba_headless_bfile_create_t)CGBA_HEADLESS_BFILE_CREATE;
	return fn(path, type, size);
}

static void headless_bfile_remove(const uint16_t *path)
{
	cgba_headless_bfile_remove_t fn =
		(cgba_headless_bfile_remove_t)CGBA_HEADLESS_BFILE_REMOVE;
	fn(path);
}

static int headless_bfile_read(int fd, void *dst, int size, int offset)
{
	cgba_headless_bfile_read_t fn =
		(cgba_headless_bfile_read_t)CGBA_HEADLESS_BFILE_READ;
	return fn(fd, dst, size, offset);
}

static int headless_bfile_read_exact_ok(int result, int size)
{
	return result == size || result == 0;
}

static int headless_bfile_write(int fd, const void *src, int size)
{
	cgba_headless_bfile_write_t fn =
		(cgba_headless_bfile_write_t)CGBA_HEADLESS_BFILE_WRITE;
	return fn(fd, src, size);
}

static void headless_bfile_close(int fd)
{
	cgba_headless_bfile_close_t fn =
		(cgba_headless_bfile_close_t)CGBA_HEADLESS_BFILE_CLOSE;
	fn(fd);
}

static int headless_storage_blob_size(const uint16_t *path)
{
	int fd = headless_bfile_open(path, CGBA_HEADLESS_BFILE_READ_ONLY);
	int size;

	if(fd < 0)
		return -1;
	size = headless_bfile_size(fd);
	headless_bfile_close(fd);
	return size;
}

static int headless_read_blob_contents(const uint16_t *path, void *dst,
	unsigned size)
{
	int fd = headless_bfile_open(path, CGBA_HEADLESS_BFILE_READ_ONLY);
	int file_size, read_result, ok;

	if(fd < 0)
		return 0;
	file_size = headless_bfile_size(fd);
	if(file_size != (int)size) {
		headless_bfile_close(fd);
		return 0;
	}
	read_result = headless_bfile_read(fd, dst, (int)size, 0);
	ok = headless_bfile_read_exact_ok(read_result, (int)size);
	headless_bfile_close(fd);
	return ok;
}

static int headless_write_blob_contents(const uint16_t *path, const void *src,
	unsigned size)
{
	int fd = headless_bfile_open(path, CGBA_HEADLESS_BFILE_WRITE_ONLY);
	int ok;

	if(fd < 0)
		return 0;
	ok = headless_bfile_write(fd, src, (int)size) == (int)size;
	headless_bfile_close(fd);
	return ok;
}

static int headless_create_blob(const uint16_t *path, unsigned size)
{
	int create_size = (int)size;
	return headless_bfile_create(path, CGBA_HEADLESS_BFILE_FILE,
		&create_size) >= 0;
}

static int headless_save_checkpoint(unsigned frame)
{
	void *state = cgba_sh4_checkpoint_buffer();
	unsigned size = cgba_sh4_checkpoint_size();
	int existing_size = headless_storage_blob_size(headless_checkpoint_path);
	int ok = 0;
	char buf[128];

	cgba_sh4_checkpoint_capture();
	if(existing_size == (int)size) {
		ok = headless_write_blob_contents(headless_checkpoint_path, state, size);
	} else {
		if(existing_size >= 0)
			headless_bfile_remove(headless_checkpoint_path);
		if(!headless_create_blob(headless_checkpoint_path, size) &&
				existing_size < 0) {
			headless_bfile_remove(headless_checkpoint_path);
			(void)headless_create_blob(headless_checkpoint_path, size);
		}
		ok = headless_write_blob_contents(headless_checkpoint_path, state, size);
	}

	snprintf(buf, sizeof buf,
		"@@CGBA_CHECKPOINT save frame=%u ok=%d size=%u existing=%d",
		frame, ok, size, existing_size);
	hputs_dbg(buf);
	if(!ok) {
		const uint8_t *p = (const uint8_t *)state;

		snprintf(buf, sizeof buf,
			"@@CGBA_CHECKPOINT_HEX_BEGIN frame=%u size=%u",
			frame, size);
		hputs_dbg(buf);
		for(unsigned i = 0; i < size; i++) {
			hput_hex2_dbg(p[i]);
			if((i & 63u) == 63u)
				hputc_dbg('\n');
		}
		if(size & 63u)
			hputc_dbg('\n');
		snprintf(buf, sizeof buf,
			"@@CGBA_CHECKPOINT_HEX_END frame=%u", frame);
		hputs_dbg(buf);
	}
	return ok;
}

#ifdef CGBA_DYNAREC
unsigned cgba_state_compress(const u8 *raw, unsigned rawsz, u8 *out, unsigned cap);
int cgba_state_decompress(const u8 *in, unsigned insz, u8 *raw, unsigned rawsz);
void flush_translation_cache_rom(void);
#endif

/* Accept a word-RLE compressed checkpoint too (a state saved on the
 * calculator is compressed now; copying it in as CGBACHK.SAV still works). */
static int headless_read_checkpoint(void *state, unsigned size)
{
	if(headless_read_blob_contents(headless_checkpoint_path, state, size))
		return 1;                        /* raw image */
#ifdef CGBA_DYNAREC
	{
		extern u8 rom_translation_cache[];
		u8 *comp = rom_translation_cache;
		int fd = headless_bfile_open(headless_checkpoint_path,
			CGBA_HEADLESS_BFILE_READ_ONLY);
		int fsz, rd;

		if(fd < 0)
			return 0;
		fsz = headless_bfile_size(fd);
		if(fsz <= 8 || fsz > (int)size + 64) {
			headless_bfile_close(fd);
			return 0;
		}
		rd = headless_bfile_read(fd, comp, fsz, 0);
		headless_bfile_close(fd);
		if(!headless_bfile_read_exact_ok(rd, fsz))
			return 0;
		if(!cgba_state_decompress(comp, (unsigned)fsz, state, size))
			return 0;
		flush_translation_cache_rom();   /* comp staging clobbered it */
		return 1;
	}
#else
	return 0;
#endif
}

static int headless_load_checkpoint(void)
{
	void *state = cgba_sh4_checkpoint_buffer();
	unsigned size = cgba_sh4_checkpoint_size();
	int read_ok = headless_read_checkpoint(state, size);
	int load_ok = read_ok ? cgba_sh4_checkpoint_restore() : 0;
	char buf[128];

	snprintf(buf, sizeof buf,
		"@@CGBA_CHECKPOINT load ok=%d read=%d size=%u",
		load_ok, read_ok, size);
	hputs_dbg(buf);
	return load_ok;
}
#endif

static void headless_dump_framebuffer(unsigned frame, const uint16_t *framebuffer)
{
	if(!framebuffer)
		return;
#if CGBA_GPSP_HEADLESS_DUMP_EVERY == 0
	(void)frame;
	return;
#else
	char buf[96];

	if((frame % (unsigned)CGBA_GPSP_HEADLESS_DUMP_EVERY) != 0 &&
			!headless_last_frame(frame))
		return;

	snprintf(buf, sizeof buf,
		"@@CGBA_FRAME_BEGIN frame=%u w=%u h=%u pitch=%u fmt=rgb565",
		frame, (unsigned)CGBA_GBA_WIDTH, (unsigned)CGBA_GBA_HEIGHT,
		(unsigned)CGBA_GBA_PITCH);
	hputs_dbg(buf);
	for(unsigned y = 0; y < CGBA_GBA_HEIGHT; y++) {
		const uint16_t *row = framebuffer + y * CGBA_GBA_PITCH;
		for(unsigned x = 0; x < CGBA_GBA_WIDTH; x++)
			hput_hex4_dbg(row[x]);
		hputc_dbg('\n');
	}
	snprintf(buf, sizeof buf, "@@CGBA_FRAME_END frame=%u", frame);
	hputs_dbg(buf);
#endif
}

static void headless_log_framebuffer_stat(unsigned frame,
	const uint16_t *framebuffer)
{
#if CGBA_GPSP_HEADLESS_STAT_EVERY == 0
	(void)frame;
	(void)framebuffer;
#else
	char buf[128];
	uint32_t hash = 2166136261u;
	unsigned black = 0;
	const unsigned stat_every = (unsigned)CGBA_GPSP_HEADLESS_STAT_EVERY;

	if(!framebuffer)
		return;
	if((frame % stat_every) != 0 && !headless_last_frame(frame))
		return;

	for(unsigned y = 0; y < CGBA_GBA_HEIGHT; y++) {
		const uint16_t *row = framebuffer + y * CGBA_GBA_PITCH;
		for(unsigned x = 0; x < CGBA_GBA_WIDTH; x++) {
			uint16_t px = row[x];
			black += px == 0;
			hash = (hash ^ (uint8_t)(px >> 8)) * 16777619u;
			hash = (hash ^ (uint8_t)px) * 16777619u;
		}
	}

	snprintf(buf, sizeof buf,
		"@@CGBA_FBSTAT frame=%u black=%u/%u hash=%08lX p00=%04X p11=%04X pc=%04X",
		frame, black, (unsigned)(CGBA_GBA_WIDTH * CGBA_GBA_HEIGHT),
		(unsigned long)hash, framebuffer[0],
		framebuffer[1 * CGBA_GBA_PITCH + 1],
		framebuffer[(CGBA_GBA_HEIGHT / 2) * CGBA_GBA_PITCH +
			(CGBA_GBA_WIDTH / 2)]);
	hputs_dbg(buf);
#endif
}

static int headless_state_frame(unsigned frame)
{
#if CGBA_GPSP_HEADLESS_STATE_EVERY == 0
	(void)frame;
	return 0;
#else
	if(CGBA_GPSP_HEADLESS_STATE_EVERY == 0)
		return 0;
	if(frame < (unsigned)CGBA_GPSP_HEADLESS_STATE_START ||
			frame > (unsigned)CGBA_GPSP_HEADLESS_STATE_END)
		return 0;
	return (frame % (unsigned)CGBA_GPSP_HEADLESS_STATE_EVERY) == 0 ||
		headless_last_frame(frame);
#endif
}

static void headless_log_state(unsigned frame, const char *phase,
	const uint16_t *framebuffer)
{
	char lines[8][CGBA_STATE_LINE_MAX];
	unsigned n, i;

	if(!headless_state_frame(frame))
		return;
	n = cgba_gpsp_state_lines(frame, phase, framebuffer, lines, 8);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
}

#ifdef CGBA_DYNAREC
enum {
	CGBA_HEADLESS_REG_PC = 15,
	CGBA_HEADLESS_CPU_HALT_STATE = 18,
	CGBA_HEADLESS_REG_SLEEP_CYCLES = 24,
	CGBA_HEADLESS_CPU_ACTIVE = 0,
};

static void headless_window_diff(unsigned frame)
{
#if CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME >= 0
	char lines[24][48];
	char buf[128];
	uint32_t cycles;
	unsigned n, i;

	if((int)frame != CGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME)
		return;

	cycles = (reg[CGBA_HEADLESS_CPU_HALT_STATE] == CGBA_HEADLESS_CPU_ACTIVE) ?
		execute_cycles : (uint32_t)-64;
	snprintf(buf, sizeof buf,
		"@@CGBA_WINDOW_DIFF_BEGIN frame=%u cycles=%ld pc=%08lX halt=%lu sleep=%08lX",
		frame, (long)(int32_t)cycles,
		(unsigned long)reg[CGBA_HEADLESS_REG_PC],
		(unsigned long)reg[CGBA_HEADLESS_CPU_HALT_STATE],
		(unsigned long)reg[CGBA_HEADLESS_REG_SLEEP_CYCLES]);
	hputs_dbg(buf);
	n = cgba_sh4_diff_window(cycles, lines, 24);
	for(i = 0; i < n; i++) {
		snprintf(buf, sizeof buf, "@@CGBA_WINDOW_DIFF frame=%u line=%u %s",
			frame, i, lines[i]);
		hputs_dbg(buf);
	}
	snprintf(buf, sizeof buf, "@@CGBA_WINDOW_DIFF_END frame=%u", frame);
	hputs_dbg(buf);
#else
	(void)frame;
#endif
}
#endif

static void headless_log_phase(unsigned frame, const char *phase)
{
	char buf[64];

	if(frame < (unsigned)CGBA_GPSP_HEADLESS_PHASE_START ||
			frame > (unsigned)CGBA_GPSP_HEADLESS_PHASE_END)
		return;

	snprintf(buf, sizeof buf, "phase frame=%u %s", frame, phase);
	hputs_dbg(buf);
}

static int headless_log_frame(unsigned frame)
{
#if CGBA_GPSP_HEADLESS_LOG_EVERY == 0
	return headless_last_frame(frame);
#else
	return (frame % (unsigned)CGBA_GPSP_HEADLESS_LOG_EVERY) == 0 ||
		headless_last_frame(frame);
#endif
}

static int headless_a_down(unsigned frame)
{
	const unsigned start = (unsigned)CGBA_GPSP_HEADLESS_A_FRAME;
	const unsigned hold = (unsigned)CGBA_GPSP_HEADLESS_A_HOLD;
#if CGBA_GPSP_HEADLESS_A_PERIOD != 0
	const unsigned period = (unsigned)CGBA_GPSP_HEADLESS_A_PERIOD;
#endif
	const unsigned press = (unsigned)CGBA_GPSP_HEADLESS_A_PRESS;
	unsigned rel;

	if(hold == 0 || press == 0 || frame < start || frame >= start + hold)
		return 0;
	rel = frame - start;
#if CGBA_GPSP_HEADLESS_A_PERIOD == 0
	(void)rel;
	return 1;
#else
	if(period == 0)
		return 1;
	return (rel % period) < press;
#endif
}

static int headless_a_edge(unsigned frame)
{
	return headless_a_down(frame) &&
		(frame == 0 || !headless_a_down(frame - 1));
}

#ifndef CGBA_GPSP_HEADLESS_FUZZ_SEED
#define CGBA_GPSP_HEADLESS_FUZZ_SEED 0
#endif
/* Tolerate an empty macro from stale build caches. */
#if (CGBA_GPSP_HEADLESS_FUZZ_SEED + 0) > 0
#define CGBA_HEADLESS_FUZZ_ON 1
#else
#define CGBA_HEADLESS_FUZZ_ON 0
#endif

#if CGBA_HEADLESS_FUZZ_ON
/* Seeded input monkey: hold one direction for 12..43 frames while
 * occasionally tapping A/B (interaction) and rarely Start (menus). Unlike
 * the fixed-key harnesses it actually wanders and interacts, reaching
 * deeper game states; the seed makes every run reproducible. */
static uint32_t headless_fuzz_buttons(unsigned frame)
{
	static uint32_t rng = (uint32_t)CGBA_GPSP_HEADLESS_FUZZ_SEED;
	static unsigned until, dir;
	static unsigned tap_until, tap_mask;
	uint32_t buttons;

	if(frame >= until) {
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		dir = (rng >> 3) & 3;                   /* R/L/U/D */
		until = frame + 12 + ((rng >> 8) & 31);
	}
	buttons = FXCG100_GBA_BUTTON_RIGHT << dir;
	if(frame >= tap_until) {
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		{
			unsigned r = (rng >> 4) & 0xFF;
			if(r < 64)       tap_mask = FXCG100_GBA_BUTTON_A;
			else if(r < 96)  tap_mask = FXCG100_GBA_BUTTON_B;
			else if(r < 104) tap_mask = FXCG100_GBA_BUTTON_START;
			else             tap_mask = 0;
		}
		tap_until = frame + 6 + ((rng >> 12) & 15);
	}
	if(tap_mask && frame + 4 >= tap_until)
		buttons |= tap_mask;
	return buttons;
}
#endif

static uint32_t headless_buttons_for_frame(unsigned frame)
{
	const unsigned start = (unsigned)CGBA_GPSP_HEADLESS_START_FRAME;
	const unsigned hold = (unsigned)CGBA_GPSP_HEADLESS_START_HOLD;
	uint32_t buttons = FXCG100_GBA_BUTTON_NONE;

#if CGBA_HEADLESS_FUZZ_ON
	return headless_fuzz_buttons(frame);
#endif

	if(hold != 0 && frame >= start && frame < start + hold) {
#if CGBA_GPSP_HEADLESS_START_PERIOD > 0
		if(((frame - start) % (unsigned)CGBA_GPSP_HEADLESS_START_PERIOD) <
		   (unsigned)CGBA_GPSP_HEADLESS_START_PRESS)
			buttons |= FXCG100_GBA_BUTTON_START;
#else
		buttons |= FXCG100_GBA_BUTTON_START;
#endif
	}
	if(headless_a_down(frame))
		buttons |= FXCG100_GBA_BUTTON_A;
#if CGBA_GPSP_HEADLESS_ALT_PERIOD > 0
#if CGBA_GPSP_HEADLESS_ALT_FRAME > 0
	if(frame >= (unsigned)CGBA_GPSP_HEADLESS_ALT_FRAME) {
#endif
		unsigned rel = frame - (unsigned)CGBA_GPSP_HEADLESS_ALT_FRAME;
		unsigned win = rel / (unsigned)CGBA_GPSP_HEADLESS_ALT_PERIOD;
		if((rel % (unsigned)CGBA_GPSP_HEADLESS_ALT_PERIOD) <
		   (unsigned)CGBA_GPSP_HEADLESS_ALT_PRESS)
#if CGBA_GPSP_HEADLESS_ALT_LEFT
			buttons |= (win & 1) ? FXCG100_GBA_BUTTON_LEFT
				: FXCG100_GBA_BUTTON_A;
#else
			buttons |= (win & 1) ? FXCG100_GBA_BUTTON_START
				: FXCG100_GBA_BUTTON_A;
#endif
#if CGBA_GPSP_HEADLESS_ALT_FRAME > 0
	}
#endif
#endif
#if CGBA_GPSP_HEADLESS_RUN_FRAME > 0
	if(frame >= (unsigned)CGBA_GPSP_HEADLESS_RUN_FRAME) {
#if CGBA_GPSP_HEADLESS_RUN_FLIP > 0
		unsigned phase = (frame - (unsigned)CGBA_GPSP_HEADLESS_RUN_FRAME) /
			(unsigned)CGBA_GPSP_HEADLESS_RUN_FLIP;
#else
		unsigned phase = 0;                     /* 0 = never flip */
#endif
		buttons |= (phase & 1) ? FXCG100_GBA_BUTTON_RIGHT
			: FXCG100_GBA_BUTTON_LEFT;
	}
#endif

	return buttons;
}

static int cgba_headless_test(uint16_t *framebuffer)
{
	static char lines[20][CGBA_DIAG_LINE_MAX];
	char buf[128];
	unsigned n, i, rom, frame_base, frame_end;

	fxcg100_lcd_init();
	cgba_crash_reporting_init();
	cgba_gpsp_refresh_roms();
	hputs_dbg("=== CGBA headless test ===");
	snprintf(buf, sizeof buf, "rom_count=%u builtin=%u",
		cgba_gpsp_rom_count(), (unsigned)CGBA_GPSP_ROM_BUILTIN_COUNT);
	hputs_dbg(buf);

	rom = (cgba_gpsp_rom_count() > CGBA_GPSP_ROM_BUILTIN_COUNT)
		? CGBA_GPSP_ROM_BUILTIN_COUNT : 0;
	if(start_gpsp(framebuffer, rom) != 0) {
		hputs_dbg("start_gpsp FAILED");
		hputs_dbg(cgba_gpsp_last_error() ? cgba_gpsp_last_error() : "(none)");
		for(;;)
			;
	}

#ifdef CGBA_DYNAREC
#if CGBA_GPSP_HEADLESS_DYNAREC >= 0
	dynarec_enable = CGBA_GPSP_HEADLESS_DYNAREC ? 1 : 0;
#endif
	snprintf(buf, sizeof buf, "dynarec=%d", dynarec_enable);
	hputs_dbg(buf);
#if CGBA_GPSP_HEADLESS_LOAD_STATE
	if(!headless_load_checkpoint()) {
		hputs_dbg("checkpoint load FAILED");
		for(;;)
			;
	}
#endif
	cgba_dynarec_rom_flush_count = 0;
	cgba_dynarec_ram_flush_count = 0;
	cgba_dynarec_arm_translate_count = 0;
	cgba_dynarec_thumb_translate_count = 0;
	cgba_dynarec_lookup_arm_count = 0;
	cgba_dynarec_lookup_thumb_count = 0;
	cgba_dynarec_lookup_dual_count = 0;
	cgba_dynarec_icache_sync_count = 0;
	cgba_dynarec_icache_sync_bytes = 0;
	cgba_dynarec_ibh_arm_hit_count = 0;
	cgba_dynarec_ibh_arm_slow_count = 0;
	cgba_dynarec_ibh_thumb_hit_count = 0;
	cgba_dynarec_ibh_thumb_slow_count = 0;
	cgba_dynarec_ibh_dual_arm_hit_count = 0;
	cgba_dynarec_ibh_dual_thumb_hit_count = 0;
	cgba_dynarec_ibh_dual_slow_count = 0;
	cgba_dynarec_ibh_dual_hot_arm_count = 0;
	cgba_dynarec_ibh_dual_hot_thumb_count = 0;
	cgba_sh4_prof_reset();
	cgba_sh4_helper_thumb_ldst_count = 0;
	cgba_sh4_helper_thumb_block_count = 0;
	cgba_sh4_helper_thumb_shift_count = 0;
	cgba_sh4_helper_thumb_dp_count = 0;
	cgba_sh4_helper_arm_ldst_count = 0;
	cgba_sh4_helper_arm_block_count = 0;
	cgba_sh4_helper_arm_dp_count = 0;
	cgba_sh4_helper_arm_mul_count = 0;
	cgba_sh4_helper_arm_psr_count = 0;
	cgba_sh4_helper_arm_swap_count = 0;
	cgba_sh4_helper_hle_div_count = 0;
	cgba_sh4_helper_arm_ldst_load_count = 0;
	cgba_sh4_helper_arm_ldst_store_count = 0;
	cgba_sh4_helper_arm_ldst_ram_count = 0;
	cgba_sh4_helper_arm_ldst_io_count = 0;
	cgba_sh4_helper_arm_ldst_video_count = 0;
	cgba_sh4_helper_arm_ldst_rom_count = 0;
	cgba_sh4_helper_arm_ldst_other_count = 0;
	cgba_sh4_helper_arm_block_load_count = 0;
	cgba_sh4_helper_arm_block_store_count = 0;
	cgba_sh4_helper_thumb_ldst_load_count = 0;
	cgba_sh4_helper_thumb_ldst_store_count = 0;
	cgba_sh4_helper_thumb_ldst_ram_count = 0;
	cgba_sh4_helper_thumb_ldst_io_count = 0;
	cgba_sh4_helper_thumb_ldst_video_count = 0;
	cgba_sh4_helper_thumb_ldst_rom_count = 0;
	cgba_sh4_helper_thumb_ldst_other_count = 0;
	cgba_sh4_helper_thumb_ldst_unmapped_count = 0;
	cgba_sh4_helper_thumb_ldst_guest_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_host_unaligned_count = 0;
	cgba_sh4_helper_thumb_ldst_unsafe_region_count = 0;
	cgba_sh4_helper_thumb_ldst_smc_count = 0;
	cgba_sh4_helper_thumb_ldst_native_ready_count = 0;
	cgba_sh4_helper_thumb_ldst_word_count = 0;
	cgba_sh4_helper_thumb_ldst_byte_count = 0;
	cgba_sh4_helper_thumb_ldst_half_count = 0;
	cgba_sh4_helper_thumb_ldst_pc_count = 0;
	cgba_sh4_helper_thumb_ldst_sp_count = 0;
	cgba_sh4_helper_thumb_ldst_reg_count = 0;
	cgba_sh4_helper_thumb_ldst_imm_count = 0;
	cgba_sh4_native_thumb_const_io_count = 0;
	cgba_sh4_native_thumb_runtime_io_count = 0;
	cgba_sh4_native_thumb_push_iwram_count = 0;
	memset(cgba_sh4_thumb_io16_store_count, 0,
		sizeof(uint32_t) * 512u);
	memset(cgba_sh4_dma_ctl_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_enable_count, 0, sizeof(uint32_t) * 4u);
	memset(cgba_sh4_dma_ctl_value, 0, sizeof(uint32_t) * 16u);
	memset(cgba_sh4_dma_ctl_value_count, 0, sizeof(uint32_t) * 16u);
	cgba_sh4_hle_div_zero_count = 0;
	cgba_sh4_hle_div_one_count = 0;
	cgba_sh4_hle_div_neg_one_count = 0;
	cgba_sh4_hle_div_pow2_count = 0;
	cgba_sh4_hle_div_other_count = 0;
	#endif
	snprintf(buf, sizeof buf,
		"input START f=%u h=%u A f=%u h=%u p=%u w=%u ALT f=%u p=%u w=%u L=%u",
		(unsigned)CGBA_GPSP_HEADLESS_START_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_START_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_A_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PERIOD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PRESS,
		(unsigned)CGBA_GPSP_HEADLESS_ALT_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_ALT_PERIOD,
		(unsigned)CGBA_GPSP_HEADLESS_ALT_PRESS,
		(unsigned)CGBA_GPSP_HEADLESS_ALT_LEFT);
	hputs_dbg(buf);

	frame_base = headless_frame_base();
	frame_end = headless_frame_end();
	snprintf(buf, sizeof buf, "loaded OK; running %u frames [%u,%u)",
		(unsigned)CGBA_GPSP_HEADLESS_FRAMES, frame_base, frame_end);
	hputs_dbg(buf);
#if (CGBA_GPSP_HEADLESS_SCALE + 0) > 0
	fxcg100_lcd_set_scale(CGBA_GPSP_HEADLESS_SCALE);
	fxcg100_lcd_set_filter(CGBA_GPSP_HEADLESS_FILTER + 0);
	snprintf(buf, sizeof buf, "display scale mode %u filter %u",
		(unsigned)CGBA_GPSP_HEADLESS_SCALE,
		(unsigned)(CGBA_GPSP_HEADLESS_FILTER + 0));
	hputs_dbg(buf);
#endif
	cgba_fps_init(&cgba_fps);
	for(i = 0; i < CGBA_GPSP_HEADLESS_FRAMES; i++) {
		unsigned frame = frame_base + i;
		int rendered = (frame % (CGBA_GPSP_HEADLESS_FRAMESKIP + 1u)) == 0
#if CGBA_GPSP_HEADLESS_STAT_EVERY > 0
			|| (frame % CGBA_GPSP_HEADLESS_STAT_EVERY) == 0
#endif
			;
		int log_frame = headless_log_frame(frame);
		uint32_t buttons = headless_buttons_for_frame(frame);

		if(log_frame) {
			snprintf(buf, sizeof buf, "frame %u before render=%d",
				frame, rendered);
			hputs_dbg(buf);
		}
		if(buttons & FXCG100_GBA_BUTTON_START) {
			snprintf(buf, sizeof buf, "frame %u START", frame);
			hputs_dbg(buf);
		}
		if(headless_a_edge(frame)) {
			snprintf(buf, sizeof buf, "frame %u A/SHIFT", frame);
			hputs_dbg(buf);
		}
#ifdef CGBA_DYNAREC
#if CGBA_GPSP_HEADLESS_DIFF_BLOCKS > 0
		if((int)frame == CGBA_GPSP_HEADLESS_DIFF_FRAME) {
			unsigned j;
			snprintf(buf, sizeof buf, "=== live block diff frame %u blocks %u ===",
				frame, (unsigned)CGBA_GPSP_HEADLESS_DIFF_BLOCKS);
			hputs_dbg(buf);
			n = cgba_sh4_diff_blocks_here(
				(unsigned)CGBA_GPSP_HEADLESS_DIFF_BLOCKS, lines, 20);
			for(j = 0; j < n; j++)
				hputs_dbg(lines[j]);
			snprintf(buf, sizeof buf, "=== live block diff done frame %u ===",
				frame);
			hputs_dbg(buf);
		}
#endif
#endif
		headless_log_phase(frame, "pre-run");
		headless_log_state(frame, "pre", framebuffer);
#ifdef CGBA_DYNAREC
		headless_window_diff(frame);
#endif
		cgba_gpsp_run_frame(buttons, rendered);
		headless_log_phase(frame, "post-run");
		headless_log_state(frame, "post", framebuffer);
		if((buttons & FXCG100_GBA_BUTTON_START) || headless_a_edge(frame)) {
			snprintf(buf, sizeof buf, "frame %u P1=%03lX buttons=%03lX",
				frame, (unsigned long)cgba_gpsp_keyinput(),
				(unsigned long)buttons);
			hputs_dbg(buf);
		}
		if(log_frame) {
			snprintf(buf, sizeof buf, "frame %u after", frame);
			hputs_dbg(buf);
		}
#ifdef CGBA_DYNAREC
#if CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME >= 0
		if((int)frame == CGBA_GPSP_HEADLESS_SAVE_STATE_FRAME)
			(void)headless_save_checkpoint(frame);
#endif
#if CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME >= 0
		/* Exercise the REAL calculator savestate path (per-ROM name +
		 * compression), unlike the raw checkpoint above. */
		if((int)frame == CGBA_GPSP_HEADLESS_SAVE_SLOT_FRAME) {
			int ok = cgba_gpsp_state_save(0);
			snprintf(buf, sizeof buf,
				"@@CGBA_SLOTSAVE frame=%u ok=%d", frame, ok);
			hputs_dbg(buf);
		}
#endif
#endif
		headless_dump_framebuffer(frame, framebuffer);
		headless_log_framebuffer_stat(frame, framebuffer);
		headless_log_phase(frame, "post-dump");
		cgba_fps_tick(&cgba_fps, rendered);
		headless_log_phase(frame, "post-fps");
		if(rendered) {
			/* Exercise the real blit path incl. the no-final-wait DMA overlap. */
			headless_log_phase(frame, "pre-overlay");
			fxcg100_lcd_overlay_fps(framebuffer, cgba_fps.emu_fps,
				cgba_fps.draw_fps);
			headless_log_phase(frame, "post-overlay");
			fxcg100_lcd_blit_gba(framebuffer);
			headless_log_phase(frame, "post-blit");
		}
		headless_log_phase(frame, "loop-end");
	}

	snprintf(buf, sizeof buf, "fps emu=%u draw=%u",
		(unsigned)cgba_fps.emu_fps, (unsigned)cgba_fps.draw_fps);
	hputs_dbg(buf);

	n = cgba_gpsp_diag(lines, 20);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
	#ifdef CGBA_DYNAREC
	{
	extern unsigned long cgba_em_const_small, cgba_em_const_large, cgba_em_const_bytes;
	extern unsigned long cgba_em_fcall_n, cgba_em_fcall_bytes;
	extern unsigned long cgba_em_fjmp_n, cgba_em_fjmp_bytes;
	extern unsigned long cgba_em_pj_n, cgba_em_pj_bytes;
	extern unsigned long cgba_em_fm_n, cgba_em_fm_bytes;
	extern unsigned long cgba_em_blk_n, cgba_em_blk_bytes;
	#if CGBA_GPSP_HEADLESS_BENCH_FRAMES > 0
	snprintf(buf, sizeof buf,
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu "
		"bios_n=%lu bios_kc=%lu cold_n=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count,
		(unsigned long)cgba_sh4_bios_fallback_call_count,
		(unsigned long)(cgba_sh4_bios_fallback_cycle_count / 1024u),
		(unsigned long)cgba_dynarec_cold_interp_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit emit blk=%lu blk_b=%lu cS=%lu cL=%lu c_b=%lu fc=%lu fc_b=%lu "
		"fj=%lu fj_b=%lu pj=%lu pj_b=%lu fm=%lu fm_b=%lu",
		cgba_em_blk_n, cgba_em_blk_bytes,
		cgba_em_const_small, cgba_em_const_large, cgba_em_const_bytes,
		cgba_em_fcall_n, cgba_em_fcall_bytes,
		cgba_em_fjmp_n, cgba_em_fjmp_bytes,
		cgba_em_pj_n, cgba_em_pj_bytes,
		cgba_em_fm_n, cgba_em_fm_bytes);
	hputs_dbg(buf);
	{
		extern unsigned long cgba_em_blk_n;   /* anchor: same block */
		extern u32 cgba_bios_entry_swi, cgba_bios_entry_irq, cgba_bios_entry_other;
		snprintf(buf, sizeof buf, "jit bios entries swi=%lu irq=%lu other=%lu",
			(unsigned long)cgba_bios_entry_swi,
			(unsigned long)cgba_bios_entry_irq,
			(unsigned long)cgba_bios_entry_other);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_interp_instr_bios, cgba_interp_instr_rom, cgba_interp_instr_ram;
		snprintf(buf, sizeof buf, "jit interp-instr bios=%lu rom=%lu ram=%lu",
			(unsigned long)cgba_interp_instr_bios,
			(unsigned long)cgba_interp_instr_rom,
			(unsigned long)cgba_interp_instr_ram);
		hputs_dbg(buf);
	}
	{
		extern unsigned long cgba_swi_interp_n[32];
		extern unsigned long cgba_swi_interp_words[32];
		unsigned si;
		for(si = 0; si < 32; si++) {
			if(!cgba_swi_interp_n[si])
				continue;
			snprintf(buf, sizeof buf, "jit swi-census %02X n=%lu words=%lu",
				si, cgba_swi_interp_n[si], cgba_swi_interp_words[si]);
			hputs_dbg(buf);
		}
	}
	{
		extern u32 cgba_cap_src[8];
		snprintf(buf, sizeof buf,
			"jit cap vid=%lu ser=%lu t0=%lu t1=%lu t2=%lu t3=%lu cyc=%lu small=%lu",
			(unsigned long)cgba_cap_src[0], (unsigned long)cgba_cap_src[1],
			(unsigned long)cgba_cap_src[2], (unsigned long)cgba_cap_src[3],
			(unsigned long)cgba_cap_src[4], (unsigned long)cgba_cap_src[5],
			(unsigned long)cgba_cap_src[6], (unsigned long)cgba_cap_src[7]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_update_gba_calls, cgba_update_gba_slices;
		extern u32 cgba_psr_fb[8];
		snprintf(buf, sizeof buf,
			"jit ev calls=%lu slices=%lu psr: mrs=%lu mi=%lu mc=%lu mf=%lu mcf=%lu sp=%lu o=%lu",
			(unsigned long)cgba_update_gba_calls, (unsigned long)cgba_update_gba_slices,
			(unsigned long)cgba_psr_fb[0], (unsigned long)cgba_psr_fb[1],
			(unsigned long)cgba_psr_fb[2], (unsigned long)cgba_psr_fb[3],
			(unsigned long)cgba_psr_fb[4], (unsigned long)cgba_psr_fb[5],
			(unsigned long)cgba_psr_fb[6]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_update_gba_halt_calls;
		extern u32 cgba_armldst_fb_pc[16], cgba_armldst_fb_op[16], cgba_armldst_fb_n[16];
		int k;
		snprintf(buf, sizeof buf, "jit ev halt-calls=%lu",
			(unsigned long)cgba_update_gba_halt_calls);
		hputs_dbg(buf);
		for(k = 0; k < 16; k++)
			if(cgba_armldst_fb_n[k] >= 1000) {
				snprintf(buf, sizeof buf, "jit armfb pc=%08lX op=%08lX n=%lu",
					(unsigned long)cgba_armldst_fb_pc[k],
					(unsigned long)cgba_armldst_fb_op[k],
					(unsigned long)cgba_armldst_fb_n[k]);
				hputs_dbg(buf);
			}
	}
#if CGBA_SH4_SWI_HLE_VERIFY
	{
		extern u32 cgba_swi_verify_checked, cgba_swi_verify_bad;
		snprintf(buf, sizeof buf, "jit swiv checked=%lu bad=%lu",
			(unsigned long)cgba_swi_verify_checked,
			(unsigned long)cgba_swi_verify_bad);
		hputs_dbg(buf);
	}
#endif
	{
		extern void cgba_sh4_dump_rom_blockmap(void);
		cgba_sh4_dump_rom_blockmap();
	}
	{
		extern u32 cgba_bios_hle_swi_count, cgba_bios_hle_irq_in, cgba_bios_hle_irq_out;
		extern u32 cgba_bios_other_pc[8];
		snprintf(buf, sizeof buf,
			"jit hle swi=%lu irqin=%lu irqout=%lu opc=%lX %lX %lX %lX",
			(unsigned long)cgba_bios_hle_swi_count,
			(unsigned long)cgba_bios_hle_irq_in,
			(unsigned long)cgba_bios_hle_irq_out,
			(unsigned long)cgba_bios_other_pc[0], (unsigned long)cgba_bios_other_pc[1],
			(unsigned long)cgba_bios_other_pc[2], (unsigned long)cgba_bios_other_pc[3]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_swi_miss[48];
		unsigned mi; int mn = 0;
		char *bp = buf;
		bp += snprintf(bp, sizeof buf, "jit swi-miss");
		for (mi = 0; mi < 48 && mn < 8; mi++)
			if (cgba_swi_miss[mi]) {
				bp += snprintf(bp, (size_t)(buf + sizeof buf - bp),
					" %02X=%lu", mi, (unsigned long)cgba_swi_miss[mi]);
				mn++;
			}
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_dp_fb_op[16];
		extern u32 cgba_dp_fb_pc, cgba_dp_fb_regshift, cgba_dp_fb_ror, cgba_dp_fb_s;
		snprintf(buf, sizeof buf,
			"jit dp-fb pc=%lu rs=%lu ror=%lu s=%lu op:0=%lu 1=%lu 2=%lu 4=%lu 8=%lu 9=%lu C=%lu D=%lu E=%lu",
			(unsigned long)cgba_dp_fb_pc, (unsigned long)cgba_dp_fb_regshift,
			(unsigned long)cgba_dp_fb_ror, (unsigned long)cgba_dp_fb_s,
			(unsigned long)cgba_dp_fb_op[0], (unsigned long)cgba_dp_fb_op[1],
			(unsigned long)cgba_dp_fb_op[2], (unsigned long)cgba_dp_fb_op[4],
			(unsigned long)cgba_dp_fb_op[8], (unsigned long)cgba_dp_fb_op[9],
			(unsigned long)cgba_dp_fb_op[12], (unsigned long)cgba_dp_fb_op[13],
			(unsigned long)cgba_dp_fb_op[14]);
		hputs_dbg(buf);
	}
	snprintf(buf, sizeof buf,
		"jit helpers arm ldst=%lu blk=%lu dp=%lu mul=%lu psr=%lu swp=%lu",
		(unsigned long)cgba_sh4_helper_arm_ldst_count,
		(unsigned long)cgba_sh4_helper_arm_block_count,
		(unsigned long)cgba_sh4_helper_arm_dp_count,
		(unsigned long)cgba_sh4_helper_arm_mul_count,
		(unsigned long)cgba_sh4_helper_arm_psr_count,
		(unsigned long)cgba_sh4_helper_arm_swap_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit helpers thumb ldst=%lu blk=%lu shift=%lu dp=%lu div=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_count,
		(unsigned long)cgba_sh4_helper_thumb_block_count,
		(unsigned long)cgba_sh4_helper_thumb_shift_count,
		(unsigned long)cgba_sh4_helper_thumb_dp_count,
		(unsigned long)cgba_sh4_helper_hle_div_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst detail load=%lu store=%lu ram=%lu io=%lu vid=%lu rom=%lu other=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_load_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_store_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_ram_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_io_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_video_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_rom_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_other_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst why unm=%lu ga=%lu ha=%lu unsafe=%lu smc=%lu ready=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_unmapped_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_guest_unaligned_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_host_unaligned_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_unsafe_region_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_smc_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_native_ready_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst kind word=%lu byte=%lu half=%lu src pc=%lu sp=%lu reg=%lu imm=%lu fast cio=%lu rio=%lu push=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_word_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_byte_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_half_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_pc_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_sp_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_reg_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_imm_count,
		(unsigned long)cgba_sh4_native_thumb_const_io_count,
		(unsigned long)cgba_sh4_native_thumb_runtime_io_count,
		(unsigned long)cgba_sh4_native_thumb_push_iwram_count);
	hputs_dbg(buf);
	cgba_print_thumb_io16_top();
	cgba_print_prof_top();
	snprintf(buf, sizeof buf,
		"jit div den zero=%lu one=%lu neg1=%lu pow2=%lu other=%lu",
		(unsigned long)cgba_sh4_hle_div_zero_count,
		(unsigned long)cgba_sh4_hle_div_one_count,
		(unsigned long)cgba_sh4_hle_div_neg_one_count,
		(unsigned long)cgba_sh4_hle_div_pow2_count,
		(unsigned long)cgba_sh4_hle_div_other_count);
	hputs_dbg(buf);
	for(unsigned dma = 0; dma < 4; dma++) {
		if(cgba_sh4_dma_ctl_count[dma] == 0)
			continue;
		snprintf(buf, sizeof buf,
			"jit dma%u ctl n=%lu en=%lu v=%04lX:%lu %04lX:%lu %04lX:%lu %04lX:%lu",
			dma,
			(unsigned long)cgba_sh4_dma_ctl_count[dma],
			(unsigned long)cgba_sh4_dma_ctl_enable_count[dma],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][0],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][0],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][1],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][1],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][2],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][2],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][3],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][3]);
		hputs_dbg(buf);
	}
	snprintf(buf, sizeof buf,
		"jit arm ldst detail load=%lu store=%lu ram=%lu io=%lu vid=%lu rom=%lu other=%lu",
		(unsigned long)cgba_sh4_helper_arm_ldst_load_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_store_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_ram_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_io_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_video_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_rom_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_other_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf, "jit arm block detail load=%lu store=%lu",
		(unsigned long)cgba_sh4_helper_arm_block_load_count,
		(unsigned long)cgba_sh4_helper_arm_block_store_count);
	hputs_dbg(buf);
	hputs_dbg("=== throughput bench ===");
	n = cgba_sh4_bench(CGBA_GPSP_HEADLESS_BENCH_FRAMES, lines, 20);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
	#else
	snprintf(buf, sizeof buf,
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu "
		"bios_n=%lu bios_kc=%lu cold_n=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count,
		(unsigned long)cgba_sh4_bios_fallback_call_count,
		(unsigned long)(cgba_sh4_bios_fallback_cycle_count / 1024u),
		(unsigned long)cgba_dynarec_cold_interp_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit emit blk=%lu blk_b=%lu cS=%lu cL=%lu c_b=%lu fc=%lu fc_b=%lu "
		"fj=%lu fj_b=%lu pj=%lu pj_b=%lu fm=%lu fm_b=%lu",
		cgba_em_blk_n, cgba_em_blk_bytes,
		cgba_em_const_small, cgba_em_const_large, cgba_em_const_bytes,
		cgba_em_fcall_n, cgba_em_fcall_bytes,
		cgba_em_fjmp_n, cgba_em_fjmp_bytes,
		cgba_em_pj_n, cgba_em_pj_bytes,
		cgba_em_fm_n, cgba_em_fm_bytes);
	hputs_dbg(buf);
	{
		extern unsigned long cgba_em_blk_n;   /* anchor: same block */
		extern u32 cgba_bios_entry_swi, cgba_bios_entry_irq, cgba_bios_entry_other;
		snprintf(buf, sizeof buf, "jit bios entries swi=%lu irq=%lu other=%lu",
			(unsigned long)cgba_bios_entry_swi,
			(unsigned long)cgba_bios_entry_irq,
			(unsigned long)cgba_bios_entry_other);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_interp_instr_bios, cgba_interp_instr_rom, cgba_interp_instr_ram;
		snprintf(buf, sizeof buf, "jit interp-instr bios=%lu rom=%lu ram=%lu",
			(unsigned long)cgba_interp_instr_bios,
			(unsigned long)cgba_interp_instr_rom,
			(unsigned long)cgba_interp_instr_ram);
		hputs_dbg(buf);
	}
	{
		extern unsigned long cgba_swi_interp_n[32];
		extern unsigned long cgba_swi_interp_words[32];
		unsigned si;
		for(si = 0; si < 32; si++) {
			if(!cgba_swi_interp_n[si])
				continue;
			snprintf(buf, sizeof buf, "jit swi-census %02X n=%lu words=%lu",
				si, cgba_swi_interp_n[si], cgba_swi_interp_words[si]);
			hputs_dbg(buf);
		}
	}
	{
		extern u32 cgba_cap_src[8];
		snprintf(buf, sizeof buf,
			"jit cap vid=%lu ser=%lu t0=%lu t1=%lu t2=%lu t3=%lu cyc=%lu small=%lu",
			(unsigned long)cgba_cap_src[0], (unsigned long)cgba_cap_src[1],
			(unsigned long)cgba_cap_src[2], (unsigned long)cgba_cap_src[3],
			(unsigned long)cgba_cap_src[4], (unsigned long)cgba_cap_src[5],
			(unsigned long)cgba_cap_src[6], (unsigned long)cgba_cap_src[7]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_update_gba_calls, cgba_update_gba_slices;
		extern u32 cgba_psr_fb[8];
		snprintf(buf, sizeof buf,
			"jit ev calls=%lu slices=%lu psr: mrs=%lu mi=%lu mc=%lu mf=%lu mcf=%lu sp=%lu o=%lu",
			(unsigned long)cgba_update_gba_calls, (unsigned long)cgba_update_gba_slices,
			(unsigned long)cgba_psr_fb[0], (unsigned long)cgba_psr_fb[1],
			(unsigned long)cgba_psr_fb[2], (unsigned long)cgba_psr_fb[3],
			(unsigned long)cgba_psr_fb[4], (unsigned long)cgba_psr_fb[5],
			(unsigned long)cgba_psr_fb[6]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_update_gba_halt_calls;
		extern u32 cgba_armldst_fb_pc[16], cgba_armldst_fb_op[16], cgba_armldst_fb_n[16];
		int k;
		snprintf(buf, sizeof buf, "jit ev halt-calls=%lu",
			(unsigned long)cgba_update_gba_halt_calls);
		hputs_dbg(buf);
		for(k = 0; k < 16; k++)
			if(cgba_armldst_fb_n[k] >= 1000) {
				snprintf(buf, sizeof buf, "jit armfb pc=%08lX op=%08lX n=%lu",
					(unsigned long)cgba_armldst_fb_pc[k],
					(unsigned long)cgba_armldst_fb_op[k],
					(unsigned long)cgba_armldst_fb_n[k]);
				hputs_dbg(buf);
			}
	}
#if CGBA_SH4_SWI_HLE_VERIFY
	{
		extern u32 cgba_swi_verify_checked, cgba_swi_verify_bad;
		snprintf(buf, sizeof buf, "jit swiv checked=%lu bad=%lu",
			(unsigned long)cgba_swi_verify_checked,
			(unsigned long)cgba_swi_verify_bad);
		hputs_dbg(buf);
	}
#endif
	{
		extern void cgba_sh4_dump_rom_blockmap(void);
		cgba_sh4_dump_rom_blockmap();
	}
	{
		extern u32 cgba_bios_hle_swi_count, cgba_bios_hle_irq_in, cgba_bios_hle_irq_out;
		extern u32 cgba_bios_other_pc[8];
		snprintf(buf, sizeof buf,
			"jit hle swi=%lu irqin=%lu irqout=%lu opc=%lX %lX %lX %lX",
			(unsigned long)cgba_bios_hle_swi_count,
			(unsigned long)cgba_bios_hle_irq_in,
			(unsigned long)cgba_bios_hle_irq_out,
			(unsigned long)cgba_bios_other_pc[0], (unsigned long)cgba_bios_other_pc[1],
			(unsigned long)cgba_bios_other_pc[2], (unsigned long)cgba_bios_other_pc[3]);
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_swi_miss[48];
		unsigned mi; int mn = 0;
		char *bp = buf;
		bp += snprintf(bp, sizeof buf, "jit swi-miss");
		for (mi = 0; mi < 48 && mn < 8; mi++)
			if (cgba_swi_miss[mi]) {
				bp += snprintf(bp, (size_t)(buf + sizeof buf - bp),
					" %02X=%lu", mi, (unsigned long)cgba_swi_miss[mi]);
				mn++;
			}
		hputs_dbg(buf);
	}
	{
		extern u32 cgba_dp_fb_op[16];
		extern u32 cgba_dp_fb_pc, cgba_dp_fb_regshift, cgba_dp_fb_ror, cgba_dp_fb_s;
		snprintf(buf, sizeof buf,
			"jit dp-fb pc=%lu rs=%lu ror=%lu s=%lu op:0=%lu 1=%lu 2=%lu 4=%lu 8=%lu 9=%lu C=%lu D=%lu E=%lu",
			(unsigned long)cgba_dp_fb_pc, (unsigned long)cgba_dp_fb_regshift,
			(unsigned long)cgba_dp_fb_ror, (unsigned long)cgba_dp_fb_s,
			(unsigned long)cgba_dp_fb_op[0], (unsigned long)cgba_dp_fb_op[1],
			(unsigned long)cgba_dp_fb_op[2], (unsigned long)cgba_dp_fb_op[4],
			(unsigned long)cgba_dp_fb_op[8], (unsigned long)cgba_dp_fb_op[9],
			(unsigned long)cgba_dp_fb_op[12], (unsigned long)cgba_dp_fb_op[13],
			(unsigned long)cgba_dp_fb_op[14]);
		hputs_dbg(buf);
	}
	snprintf(buf, sizeof buf,
		"jit helpers arm ldst=%lu blk=%lu dp=%lu mul=%lu psr=%lu swp=%lu",
		(unsigned long)cgba_sh4_helper_arm_ldst_count,
		(unsigned long)cgba_sh4_helper_arm_block_count,
		(unsigned long)cgba_sh4_helper_arm_dp_count,
		(unsigned long)cgba_sh4_helper_arm_mul_count,
		(unsigned long)cgba_sh4_helper_arm_psr_count,
		(unsigned long)cgba_sh4_helper_arm_swap_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit helpers thumb ldst=%lu blk=%lu shift=%lu dp=%lu div=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_count,
		(unsigned long)cgba_sh4_helper_thumb_block_count,
		(unsigned long)cgba_sh4_helper_thumb_shift_count,
		(unsigned long)cgba_sh4_helper_thumb_dp_count,
		(unsigned long)cgba_sh4_helper_hle_div_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst detail load=%lu store=%lu ram=%lu io=%lu vid=%lu rom=%lu other=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_load_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_store_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_ram_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_io_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_video_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_rom_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_other_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst why unm=%lu ga=%lu ha=%lu unsafe=%lu smc=%lu ready=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_unmapped_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_guest_unaligned_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_host_unaligned_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_unsafe_region_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_smc_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_native_ready_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf,
		"jit thumb ldst kind word=%lu byte=%lu half=%lu src pc=%lu sp=%lu reg=%lu imm=%lu fast cio=%lu rio=%lu push=%lu",
		(unsigned long)cgba_sh4_helper_thumb_ldst_word_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_byte_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_half_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_pc_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_sp_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_reg_count,
		(unsigned long)cgba_sh4_helper_thumb_ldst_imm_count,
		(unsigned long)cgba_sh4_native_thumb_const_io_count,
		(unsigned long)cgba_sh4_native_thumb_runtime_io_count,
		(unsigned long)cgba_sh4_native_thumb_push_iwram_count);
	hputs_dbg(buf);
	cgba_print_thumb_io16_top();
	cgba_print_prof_top();
	snprintf(buf, sizeof buf,
		"jit div den zero=%lu one=%lu neg1=%lu pow2=%lu other=%lu",
		(unsigned long)cgba_sh4_hle_div_zero_count,
		(unsigned long)cgba_sh4_hle_div_one_count,
		(unsigned long)cgba_sh4_hle_div_neg_one_count,
		(unsigned long)cgba_sh4_hle_div_pow2_count,
		(unsigned long)cgba_sh4_hle_div_other_count);
	hputs_dbg(buf);
	for(unsigned dma = 0; dma < 4; dma++) {
		if(cgba_sh4_dma_ctl_count[dma] == 0)
			continue;
		snprintf(buf, sizeof buf,
			"jit dma%u ctl n=%lu en=%lu v=%04lX:%lu %04lX:%lu %04lX:%lu %04lX:%lu",
			dma,
			(unsigned long)cgba_sh4_dma_ctl_count[dma],
			(unsigned long)cgba_sh4_dma_ctl_enable_count[dma],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][0],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][0],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][1],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][1],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][2],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][2],
			(unsigned long)cgba_sh4_dma_ctl_value[dma][3],
			(unsigned long)cgba_sh4_dma_ctl_value_count[dma][3]);
		hputs_dbg(buf);
	}
	snprintf(buf, sizeof buf,
		"jit arm ldst detail load=%lu store=%lu ram=%lu io=%lu vid=%lu rom=%lu other=%lu",
		(unsigned long)cgba_sh4_helper_arm_ldst_load_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_store_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_ram_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_io_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_video_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_rom_count,
		(unsigned long)cgba_sh4_helper_arm_ldst_other_count);
	hputs_dbg(buf);
	snprintf(buf, sizeof buf, "jit arm block detail load=%lu store=%lu",
		(unsigned long)cgba_sh4_helper_arm_block_load_count,
		(unsigned long)cgba_sh4_helper_arm_block_store_count);
	hputs_dbg(buf);
	#endif
	}
	#endif
	hputs_dbg("=== done ===");
	for(;;)   /* idle until the headless timeout kills us */
		;
	return 0;
}
#endif

int main(void)
{
	uint16_t *framebuffer = cgba_framebuffer;
	#ifdef CGBA_GPSP_HEADLESS_TEST
	return cgba_headless_test(framebuffer);
	#endif
	fxcg100_menu_state menu_state;
	uint32_t previous_app_keys = 0;
	uint32_t previous_hotkeys = 0;
	uint32_t last_hash = 0;
	unsigned current_rom;
	unsigned frame = 1;
	fxcg100_menu_result menu_result;
	cgba_pacer pacer;
	static fxcg100_debug_info debug_info;

	cgba_pacer_init(&pacer, 60, 9);

	fxcg100_lcd_init();
	cgba_crash_reporting_init();
#ifdef CGBA_GPSP_DIRECT_LCD_TEST
	if(start_gpsp(framebuffer, CGBA_GPSP_ROM_LCD_TEST) != 0)
		return exit_to_os(1);
	enter_gameplay_display(framebuffer, frame);
	for(;; frame++) {
		if(fxcg100_poll_app_keys() & FXCG100_APPKEY_ON)
			break;
		cgba_gpsp_run_frame(FXCG100_GBA_BUTTON_NONE, 1);
		blit_gba_frame(framebuffer, frame, FXCG100_GBA_BUTTON_NONE);
	}
	cgba_gpsp_shutdown();
	return exit_to_os(1);
#endif
	cgba_gpsp_refresh_roms();
	fxcg100_menu_init(&menu_state);
	if(cgba_gpsp_rom_count() > CGBA_GPSP_ROM_BUILTIN_COUNT)
		menu_state.rom_source = CGBA_GPSP_ROM_BUILTIN_COUNT;

	menu_result = fxcg100_menu_run(&menu_state, 0, 0, NULL);
	fxcg100_lcd_set_scale(menu_state.screen_scale);
	fxcg100_lcd_set_filter(menu_state.screen_filter);
	if(menu_result == FXCG100_MENU_QUIT)
		return exit_to_os(1);
	wait_for_keys_released();
	previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);

	current_rom = normalize_rom_id(menu_state.rom_source);
	if(menu_result == FXCG100_MENU_SAVE_STATE) {
		draw_status("nothing to save yet", "booting selected ROM");
		wait_status();
	}

	if(start_gpsp(framebuffer, current_rom) != 0)
		return exit_to_os(1);

	if(menu_result == FXCG100_MENU_LOAD_STATE) {
		draw_status("loading state...", NULL);
		if(!cgba_gpsp_state_load(menu_state.savestate_slot)) {
			draw_status("state load FAILED (no file?)",
				"starting from reset");
			wait_status();
		}
	}

#ifdef CGBA_GPSP_DIAG
	show_diag_overlay();
#endif

	enter_gameplay_display(framebuffer, frame);

	for(;; frame++) {
		uint32_t app_keys = fxcg100_poll_app_keys();
		uint32_t hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
		uint32_t hotkey_edge = hotkeys & ~previous_hotkeys;
		uint32_t gba_buttons = FXCG100_GBA_BUTTON_NONE;
		int menu_open_edge =
			(app_keys & FXCG100_APPKEY_ON) &&
			!(previous_app_keys & FXCG100_APPKEY_ON);

		if(menu_open_edge) {
			last_hash = cgba_gpsp_frame_hash(framebuffer);
			cgba_gpsp_refresh_roms();
			menu_state.rom_source = normalize_rom_id(menu_state.rom_source);
			cgba_gpsp_debug_menu(&debug_info, frame, last_hash,
				cgba_fps.emu_fps, cgba_fps.draw_fps, framebuffer,
				read_stack_pointer());
			fxcg100_menu_result result =
				fxcg100_menu_run(&menu_state, frame, last_hash, &debug_info);
			fxcg100_lcd_set_scale(menu_state.screen_scale);
			fxcg100_lcd_set_filter(menu_state.screen_filter);

			wait_for_keys_released();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			if(result == FXCG100_MENU_QUIT)
				break;
			if(result == FXCG100_MENU_RESET) {
				cgba_gpsp_shutdown();
				if(start_gpsp(framebuffer, current_rom) != 0)
					return exit_to_os(1);
				frame = 1;
				enter_gameplay_display(framebuffer, frame);
				continue;
			}
			if(result == FXCG100_MENU_LOAD_GAME) {
				current_rom = normalize_rom_id(menu_state.rom_source);
				cgba_gpsp_shutdown();
				if(start_gpsp(framebuffer, current_rom) != 0)
					return exit_to_os(1);
				frame = 1;
				enter_gameplay_display(framebuffer, frame);
				continue;
			}
			if(result == FXCG100_MENU_LOAD_STATE ||
					result == FXCG100_MENU_SAVE_STATE) {
				int save = result == FXCG100_MENU_SAVE_STATE;
				char slot_line[32];
				int ok;
				snprintf(slot_line, sizeof slot_line, "slot %u",
					(unsigned)menu_state.savestate_slot);
				draw_status(save ? "saving state (416KB)..."
					: "loading state...", slot_line);
				ok = save ? cgba_gpsp_state_save(menu_state.savestate_slot)
					: cgba_gpsp_state_load(menu_state.savestate_slot);
				draw_status(ok ? (save ? "state saved" : "state loaded")
					: (save ? "state save FAILED"
						: "state load FAILED (no file?)"), slot_line);
				wait_status();
			}
			enter_gameplay_display(framebuffer, frame);
			continue;
		}

		if((previous_hotkeys &
				FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD)) &&
				!(hotkeys &
				FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD)))
			cgba_pacer_reset(&pacer);

		if(hotkey_edge &
				FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_DISPLAY_FPS))
			menu_state.show_fps = menu_state.show_fps ? 0 : 1;

		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_LOAD_STATE)) {
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_PROFILE_COUNTERS)
			/* Profiling build: repurpose the unimplemented loadstate hotkey
			 * as the JIT memory canary (overclock triage; see gpsp_runner.c).
			 * The test overwrites the translation cache, so flush after. */
			{
				char line[48];
				extern void flush_dynarec_caches(void);
				uint32_t bad = cgba_jit_canary(line, sizeof line);
				flush_dynarec_caches();
				draw_status(bad ? "JIT CANARY: MEMORY CORRUPTION"
				                : "JIT canary passed", line);
			}
			wait_status();
			enter_gameplay_display(framebuffer, frame);
#else
			draw_status(cgba_gpsp_state_load(menu_state.savestate_slot)
				? "state loaded" : "state load FAILED (no file?)", NULL);
			wait_status();
			enter_gameplay_display(framebuffer, frame);
#endif
		}
		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_STATE)) {
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_PROFILE_COUNTERS)
			/* Profiling build: repurpose the (still unimplemented) savestate
			 * hotkey as a live JIT/interpreter A/B switch. The cores share
			 * reg[] and the switch lands on a frame boundary (PC committed),
			 * which is exactly how the diff harness alternates them. */
			dynarec_enable = !dynarec_enable;
			draw_status("core switched",
				dynarec_enable ? "JIT (dynarec)" : "INTERPRETER");
			wait_status();
			enter_gameplay_display(framebuffer, frame);
#else
			draw_status(cgba_gpsp_state_save(menu_state.savestate_slot)
				? "state saved" : "state save FAILED", NULL);
			wait_status();
			enter_gameplay_display(framebuffer, frame);
#endif
		}
		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_EXIT)) {
			draw_status("save+exit unavailable", "not implemented yet");
			wait_status();
			enter_gameplay_display(framebuffer, frame);
		}

		gba_buttons = fxcg100_poll_gba_buttons_mapped(menu_state.keymap);

		int render_video;
		if(hotkeys & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD))
			render_video = frame == 1 ||
				(frame % CGBA_FAST_FORWARD_RENDER_PERIOD) == 0;
		else if(frame == 1)
			render_video = 1;
		else if(menu_state.frameskip_type == CGBA_FRAMESKIP_AUTOMATIC)
			render_video = cgba_pacer_should_render(&pacer);
		else
			render_video = fxcg100_menu_should_blit(&menu_state, frame);

		cgba_gpsp_run_frame(gba_buttons, render_video);
		/* AUTOMATIC backup mode: flush the game's save to NOR when it has
		 * changed, ~every CGBA_BACKUP_AUTO_FRAMES frames. EXIT ONLY (default)
		 * relies solely on the shutdown flush. */
		if(menu_state.backup_update &&
				(frame % CGBA_BACKUP_AUTO_FRAMES) == 0)
			cgba_gpsp_backup_flush(0);
		cgba_fps_tick(&cgba_fps, render_video);
		if(render_video) {
			if(menu_state.show_fps)
				fxcg100_lcd_overlay_fps(framebuffer,
					cgba_fps.emu_fps, cgba_fps.draw_fps);
			blit_gba_frame(framebuffer, frame, gba_buttons);
		}
		previous_app_keys = app_keys;
		previous_hotkeys = hotkeys;
	}

	cgba_gpsp_shutdown();
	return exit_to_os(1);
}
