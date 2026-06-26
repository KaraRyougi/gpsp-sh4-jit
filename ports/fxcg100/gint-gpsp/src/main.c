#include <gint/display.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "frame_pacing.h"
#include "gpsp_runner.h"
#ifdef CGBA_DYNAREC
#include "sh4/sh4_diff_harness.h"
extern int dynarec_enable;   /* gpSP: 0 = interpreter, 1 = SH4 recompiler */
extern uint32_t execute_cycles;
extern uint32_t reg[64];
#ifdef CGBA_GPSP_HEADLESS_TEST
extern uint32_t cgba_dynarec_rom_flush_count;
extern uint32_t cgba_dynarec_ram_flush_count;
extern uint32_t cgba_dynarec_arm_translate_count;
extern uint32_t cgba_dynarec_thumb_translate_count;
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

static uint16_t cgba_framebuffer[CGBA_GBA_BUFFER_PIXELS] CGBA_HIGH_BSS;

/* FPS metrics meter (emulated + drawn frame rate), shown when the menu's
 * "SHOW FPS" option is on. Reset on each gameplay entry. */
static cgba_fps_meter cgba_fps;

static void wait_briefly(void)
{
	for(volatile unsigned i = 0; i < 60000000; i++)
		;
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
	dtext(8, 5, C_WHITE, "CGBA GPSP");
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
	dtext(2, 2, C_BLACK, "CGBA DIAG  (EXE = continue)");
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

#ifndef CGBA_GPSP_HEADLESS_LOG_EVERY
#define CGBA_GPSP_HEADLESS_LOG_EVERY 1u
#endif

#ifndef CGBA_GPSP_HEADLESS_START_FRAME
#define CGBA_GPSP_HEADLESS_START_FRAME 0u
#endif

#ifndef CGBA_GPSP_HEADLESS_START_HOLD
#define CGBA_GPSP_HEADLESS_START_HOLD 0u
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

#ifndef CGBA_GPSP_HEADLESS_STATE_EVERY
#define CGBA_GPSP_HEADLESS_STATE_EVERY 0u
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

static void hput_hex4_dbg(uint16_t value)
{
	static const char hex[] = "0123456789ABCDEF";

	hputc_dbg(hex[(value >> 12) & 0x0f]);
	hputc_dbg(hex[(value >> 8) & 0x0f]);
	hputc_dbg(hex[(value >> 4) & 0x0f]);
	hputc_dbg(hex[value & 0x0f]);
}

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
			frame + 1 != CGBA_GPSP_HEADLESS_FRAMES)
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
	if((frame % stat_every) != 0 && frame + 1 != CGBA_GPSP_HEADLESS_FRAMES)
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
	return (frame % (unsigned)CGBA_GPSP_HEADLESS_STATE_EVERY) == 0 ||
		frame + 1 == CGBA_GPSP_HEADLESS_FRAMES;
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
	if(CGBA_GPSP_HEADLESS_LOG_EVERY == 0)
		return frame + 1 == CGBA_GPSP_HEADLESS_FRAMES;
	return (frame % (unsigned)CGBA_GPSP_HEADLESS_LOG_EVERY) == 0 ||
		frame + 1 == CGBA_GPSP_HEADLESS_FRAMES;
}

static int headless_a_down(unsigned frame)
{
	const unsigned start = (unsigned)CGBA_GPSP_HEADLESS_A_FRAME;
	const unsigned hold = (unsigned)CGBA_GPSP_HEADLESS_A_HOLD;
	const unsigned period = (unsigned)CGBA_GPSP_HEADLESS_A_PERIOD;
	const unsigned press = (unsigned)CGBA_GPSP_HEADLESS_A_PRESS;
	unsigned rel;

	if(hold == 0 || press == 0 || frame < start || frame >= start + hold)
		return 0;
	rel = frame - start;
	if(period == 0)
		return 1;
	return (rel % period) < press;
}

static int headless_a_edge(unsigned frame)
{
	return headless_a_down(frame) &&
		(frame == 0 || !headless_a_down(frame - 1));
}

static uint32_t headless_buttons_for_frame(unsigned frame)
{
	const unsigned start = (unsigned)CGBA_GPSP_HEADLESS_START_FRAME;
	const unsigned hold = (unsigned)CGBA_GPSP_HEADLESS_START_HOLD;
	uint32_t buttons = FXCG100_GBA_BUTTON_NONE;

	if(hold != 0 && frame >= start && frame < start + hold)
		buttons |= FXCG100_GBA_BUTTON_START;
	if(headless_a_down(frame))
		buttons |= FXCG100_GBA_BUTTON_A;

	return buttons;
}

static int cgba_headless_test(uint16_t *framebuffer)
{
	static char lines[20][CGBA_DIAG_LINE_MAX];
	char buf[128];
	unsigned n, i, rom;

	fxcg100_lcd_init();
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
	cgba_dynarec_rom_flush_count = 0;
	cgba_dynarec_ram_flush_count = 0;
	cgba_dynarec_arm_translate_count = 0;
	cgba_dynarec_thumb_translate_count = 0;
	#endif
	snprintf(buf, sizeof buf, "input START f=%u h=%u A/SHIFT f=%u h=%u p=%u w=%u",
		(unsigned)CGBA_GPSP_HEADLESS_START_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_START_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_A_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PERIOD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PRESS);
	hputs_dbg(buf);

	snprintf(buf, sizeof buf, "loaded OK; running %u frames",
		(unsigned)CGBA_GPSP_HEADLESS_FRAMES);
	hputs_dbg(buf);
	cgba_fps_init(&cgba_fps);
	for(i = 0; i < CGBA_GPSP_HEADLESS_FRAMES; i++) {
		int rendered = (i % 4) == 0;
		int log_frame = headless_log_frame(i);
		uint32_t buttons = headless_buttons_for_frame(i);

		if(log_frame) {
			snprintf(buf, sizeof buf, "frame %u before render=%d",
				i, rendered);
			hputs_dbg(buf);
		}
		if(buttons & FXCG100_GBA_BUTTON_START) {
			snprintf(buf, sizeof buf, "frame %u START", i);
			hputs_dbg(buf);
		}
		if(headless_a_edge(i)) {
			snprintf(buf, sizeof buf, "frame %u A/SHIFT", i);
			hputs_dbg(buf);
		}
#ifdef CGBA_DYNAREC
#if CGBA_GPSP_HEADLESS_DIFF_BLOCKS > 0
		if((int)i == CGBA_GPSP_HEADLESS_DIFF_FRAME) {
			unsigned j;
			snprintf(buf, sizeof buf, "=== live block diff frame %u blocks %u ===",
				i, (unsigned)CGBA_GPSP_HEADLESS_DIFF_BLOCKS);
			hputs_dbg(buf);
			n = cgba_sh4_diff_blocks_here(
				(unsigned)CGBA_GPSP_HEADLESS_DIFF_BLOCKS, lines, 20);
			for(j = 0; j < n; j++)
				hputs_dbg(lines[j]);
			snprintf(buf, sizeof buf, "=== live block diff done frame %u ===", i);
			hputs_dbg(buf);
		}
#endif
#endif
		headless_log_phase(i, "pre-run");
		headless_log_state(i, "pre", framebuffer);
#ifdef CGBA_DYNAREC
		headless_window_diff(i);
#endif
		cgba_gpsp_run_frame(buttons, rendered);
		headless_log_phase(i, "post-run");
		headless_log_state(i, "post", framebuffer);
		if((buttons & FXCG100_GBA_BUTTON_START) || headless_a_edge(i)) {
			snprintf(buf, sizeof buf, "frame %u P1=%03lX buttons=%03lX",
				i, (unsigned long)cgba_gpsp_keyinput(),
				(unsigned long)buttons);
			hputs_dbg(buf);
		}
		if(log_frame) {
			snprintf(buf, sizeof buf, "frame %u after", i);
			hputs_dbg(buf);
		}
		headless_dump_framebuffer(i, framebuffer);
		headless_log_framebuffer_stat(i, framebuffer);
		headless_log_phase(i, "post-dump");
		cgba_fps_tick(&cgba_fps, rendered);
		headless_log_phase(i, "post-fps");
		if(rendered) {
			/* Exercise the real blit path incl. the no-final-wait DMA overlap. */
			headless_log_phase(i, "pre-overlay");
			fxcg100_lcd_overlay_fps(framebuffer, cgba_fps.emu_fps,
				cgba_fps.draw_fps);
			headless_log_phase(i, "post-overlay");
			fxcg100_lcd_blit_gba(framebuffer);
			headless_log_phase(i, "post-blit");
		}
		headless_log_phase(i, "loop-end");
	}

	/* Exercise the FPS overlay so the font + framebuffer write are validated;
	 * px[1,1] should become white (0xFFFF, the 'F' glyph), px[0,0] black. */
	fxcg100_lcd_overlay_fps(framebuffer, cgba_fps.emu_fps, cgba_fps.draw_fps);
	snprintf(buf, sizeof buf, "fps emu=%u draw=%u px00=%04X px11=%04X",
		(unsigned)cgba_fps.emu_fps, (unsigned)cgba_fps.draw_fps,
		framebuffer[0], framebuffer[1 * 240 + 1]);
	hputs_dbg(buf);

	n = cgba_gpsp_diag(lines, 20);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
	#ifdef CGBA_DYNAREC
	#if CGBA_GPSP_HEADLESS_BENCH_FRAMES > 0
	snprintf(buf, sizeof buf,
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count);
	hputs_dbg(buf);
	hputs_dbg("=== throughput bench ===");
	n = cgba_sh4_bench(CGBA_GPSP_HEADLESS_BENCH_FRAMES, lines, 20);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
	#else
	snprintf(buf, sizeof buf,
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count);
	hputs_dbg(buf);
	#endif
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

	cgba_pacer_init(&pacer, 60, 9);

	fxcg100_lcd_init();
	cgba_gpsp_refresh_roms();
	fxcg100_menu_init(&menu_state);
	if(cgba_gpsp_rom_count() > CGBA_GPSP_ROM_BUILTIN_COUNT)
		menu_state.rom_source = CGBA_GPSP_ROM_BUILTIN_COUNT;

	menu_result = fxcg100_menu_run(&menu_state, 0, 0);
	if(menu_result == FXCG100_MENU_QUIT)
		return exit_to_os(1);
	wait_for_keys_released();
	previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);

	current_rom = normalize_rom_id(menu_state.rom_source);
	if(menu_result == FXCG100_MENU_LOAD_STATE ||
			menu_result == FXCG100_MENU_SAVE_STATE) {
		draw_status("savestate unavailable", "booting selected ROM");
		wait_status();
	}

	if(start_gpsp(framebuffer, current_rom) != 0)
		return exit_to_os(1);

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
			fxcg100_menu_result result =
				fxcg100_menu_run(&menu_state, frame, last_hash);

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
				draw_status("savestate unavailable", "not implemented yet");
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
			draw_status("savestate unavailable", "not implemented yet");
			wait_status();
			enter_gameplay_display(framebuffer, frame);
		}
		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_STATE)) {
			draw_status("savestate unavailable", "not implemented yet");
			wait_status();
			enter_gameplay_display(framebuffer, frame);
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
