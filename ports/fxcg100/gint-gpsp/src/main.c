#include <gint/display.h>
#ifndef CGBA_CLASSPAD
#include <gint/bfile.h>
#include <gint/kmalloc.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "fxcg100_rom_sort.h"
#include "frame_pacing.h"
#include "gpsp_diag_counters.h"
#include "gpsp_runner.h"
#include "headless_test.h"
#include "overclock.h"

#if defined(CGBA_CLASSPAD)
#include <gint/keyboard.h>
#include "classpad_loader.h"
HHK_NAME("gpSP")
HHK_DESCRIPTION("Game Boy Advance emulator with an SH4 JIT")
HHK_AUTHOR("KaraRyougi")
HHK_VERSION("v01.14.1125")
#elif defined(CGBA_FXCG50)
extern int cgba_hottext_init(void);
#endif
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
extern uint32_t cgba_video_frame_dirty_rows;
extern uint32_t cgba_video_frame_dirty_row_mask[5];
#endif
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_DIFF_HARNESS)
#include "sh4/sh4_diff_harness.h"
#endif

/* Menu frameskip types (the order of the Frame skipping choices in cg_ui.c,
 * classpad_ui.c and fxcg100_menu.c). */
#define CGBA_FRAMESKIP_AUTOMATIC 0
#define CGBA_FAST_FORWARD_RENDER_PERIOD 8u

#define CGBA_HIGH_BSS __attribute__((section(".cgba.highbss"), aligned(32)))
#define CGBA_HIGHRAM_SAFE_START ((uintptr_t)0x8c200000u)
#if defined(CGBA_FXCG100) && !defined(CGBA_GPSP_HEADLESS_TEST) && \
	!defined(CGBA_SH4_DIFF_HARNESS)
/* Legacy-MPM fallback ceiling when the loader does not report loadinfo (_ld1).
 * MPM 1.1+ relocates to 0x8c7f0000 and passes [0x8c200000, romAddress) via
 * loadinfo, which cgba_claim_loader_arena() captures before highbss is used. */
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c655300u)
#elif defined(CGBA_FXCG100)
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c780000u)
#else
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c6f0000u)
#endif

extern char cgba_highbss_start[];
extern char cgba_highbss_end[];
#if defined(CGBA_CLASSPAD)
extern char cgba_arena_start[];
extern char cgba_arena_end[];
#elif defined(CGBA_FXCG50)
extern char ecgba_hottext[];
#elif defined(CGBA_FXCG100)
/* Bounds of the loader-provided RAM window reported by MPM 1.1+ via gint's
 * "_ld1" kmalloc arena. Zero if launched by an older loader or HLE emulator. */
static uintptr_t cgba_loader_ram_start;
static uintptr_t cgba_loader_ram_end;
static int cgba_loader_arena_busy;

/* Detach gint's "_ld1" arena before gpSP claims .cgba.highbss at 0x8c200000.
 * Otherwise clearing highbss wipes _ld1's heap metadata and any later kmalloc
 * fallback into _ld1 would corrupt live emulator memory.
 *
 * NOTE: gint's kinit() allocates the "_ld1" kmalloc_arena_t via kmalloc()
 * without zeroing arena->stats, so ld1->stats.live_blocks holds uninitialized
 * RAM garbage! Use kmalloc_get_gint_stats(ld1)->used_memory instead, which
 * kmalloc_init_arena(arena, true) explicitly zeroes. */
static void cgba_claim_loader_arena(void)
{
	kmalloc_arena_t *ld1 = kmalloc_get_arena("_ld1");
	kmalloc_gint_stats_t *gstats;

	if(!ld1)
		return;
	cgba_loader_ram_start = (uintptr_t)ld1->start;
	cgba_loader_ram_end = (uintptr_t)ld1->end;
	gstats = kmalloc_get_gint_stats(ld1);
	if(gstats && gstats->used_memory == 0) {
		kmalloc_remove_arena(ld1);
		kfree(ld1);
	} else {
		ld1->is_default = 0;
		cgba_loader_arena_busy = 1;
	}
}
#endif

/* 32-byte aligned: every 480-byte (240*2 = 15*32) GBA scanline starts on an
 * exact SH-4 cache-line boundary, avoiding split-line write-allocate misses
 * in strip renderers and scaled presenters. */
static uint16_t cgba_framebuffer[CGBA_GBA_BUFFER_PIXELS] CGBA_HIGH_BSS
	__attribute__((aligned(32)));

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
	#if defined(CGBA_CLASSPAD)
	/* ClassPad II: the arena is a heap block that classpad_loader.c verified
	 * to contain the linked range, so the link-time bounds are the proof. */
	uintptr_t end = (uintptr_t)cgba_highbss_end;

	return start >= (uintptr_t)cgba_arena_start && end > start &&
		end <= (uintptr_t)cgba_arena_end;
	#elif defined(CGBA_FXCG50)
	uintptr_t end = (uintptr_t)ecgba_hottext;

	return start >= CGBA_HIGHRAM_SAFE_START &&
		end > start &&
		end <= CGBA_HIGHRAM_SAFE_END;
	#else
	uintptr_t end = (uintptr_t)cgba_highbss_end;

	if(cgba_loader_arena_busy)
		return 0;
	if(cgba_loader_ram_end > cgba_loader_ram_start)
		return start >= cgba_loader_ram_start && end > start &&
			end <= cgba_loader_ram_end;
	return start >= CGBA_HIGHRAM_SAFE_START &&
		end > start &&
		end <= CGBA_HIGHRAM_SAFE_END;
	#endif
}

static void clear_gpsp_highbss(void)
{
	memset(cgba_highbss_start, 0,
		(size_t)(cgba_highbss_end - cgba_highbss_start));
}

/* The display options and colours the status card should follow; set once
 * the menu state exists. Before that the defaults are used. */
static const fxcg100_menu_state *status_state;
static const fxcg100_menu_state status_default_state;
#ifndef CGBA_CLASSPAD
/* While a game is on screen: its frame, which the fx-CG status box is drawn
 * over (dimmed), rather than over a plain page. */
static const uint16_t *status_frame;
#endif

static void draw_status(const char *line1, const char *line2)
{
#ifdef CGBA_CLASSPAD
	cp_ui_status(status_state ? status_state : &status_default_state, line1,
		line2);
#else
	cg_ui_status(status_state ? status_state : &status_default_state, line1,
		line2, status_frame);
#endif
}

/* Push the menu's display options to the presenter. */
static void apply_display_options(const fxcg100_menu_state *state)
{
	fxcg100_lcd_set_scale(state->screen_scale);
	fxcg100_lcd_set_filter(state->screen_filter);
	fxcg100_lcd_set_sync(state->lcd_sync, state->lcd_sync_offset);
	fxcg100_lcd_set_refresh(state->lcd_refresh);
	fxcg100_lcd_set_backlight(state->backlight, state->backlight_pwm);
	cgba_gpsp_set_bios_mode(state->bios_mode);
#ifdef CGBA_CLASSPAD
	fxcg100_lcd_set_rotation(state->rotation);
	fxcg100_lcd_set_touch_keys(state->touch_keys);
	fxcg100_lcd_set_transfer(state->lcd_cpu);
	fxcg100_lcd_set_theme(state->theme);
	{
		/* Clear is the menu key only while nothing else is mapped to it. */
		uint32_t i, free_key = 1;

		for(i = 0; i < FXCG100_GBA_KEY_COUNT; i++)
			if(state->keymap[i] == FXCG100_PHYSKEY_ON)
				free_key = 0;
		for(i = 0; i < FXCG100_HOTKEY_COUNT; i++)
			if(state->hotkey_map[i] == FXCG100_PHYSKEY_ON)
				free_key = 0;
		fxcg100_lcd_set_menu_key(free_key);
	}
#endif
}

#ifdef CGBA_CLASSPAD
#include "classpad_layout.h"

/* Display shortcuts on the icon strip under the screen, acted on when an
 * icon is first touched:
 *   Rotate  portrait -> clockwise -> counter-clockwise -> portrait, keeping
 *           the smaller/larger choice
 *   Resize  the other scale of the current orientation
 *   Swap    which side the keypad is on in landscape
 * The display modes are CP_VIEW_* (classpad_layout.h): once bit 1 was the
 * orientation, bit 0 the larger scale. */
static void classpad_strip_shortcuts(fxcg100_menu_state *state,
	uint32_t *previous)
{
	uint32_t strip = fxcg100_touch_strip_icons();
	uint32_t edge = strip & ~*previous;

	*previous = strip;
	if(edge & FXCG100_STRIP_ROTATE)
		cp_rotate_next(&state->screen_scale, &state->rotation);
	if(edge & FXCG100_STRIP_RESIZE)
		state->screen_scale = cp_view_resized(state->screen_scale);
	if(edge & FXCG100_STRIP_SWAP)
		state->rotation ^= 1u;
	if(edge & (FXCG100_STRIP_ROTATE | FXCG100_STRIP_RESIZE |
			FXCG100_STRIP_SWAP))
		apply_display_options(state);
}
#endif

static void blit_gba_frame(const uint16_t *pixels, unsigned frame,
	uint32_t gba_buttons)
{
	(void)frame;
	(void)gba_buttons;
	fxcg100_lcd_blit_gba(pixels);
}

static void enter_gameplay_display(const uint16_t *framebuffer, unsigned frame)
{
#ifdef CGBA_CLASSPAD
	/* The interface may have been drawn another way round than the game is
	 * set to be (it starts upright): the game gets its own view back. */
	if(status_state) {
		cp_ui_game_starting();
		apply_display_options(status_state);
	}
#endif
	cgba_fps_init(&cgba_fps);
	fxcg100_lcd_clear(C_BLACK);
	fxcg100_lcd_update();
	blit_gba_frame(framebuffer, frame, FXCG100_GBA_BUTTON_NONE);
#ifndef CGBA_CLASSPAD
	status_frame = framebuffer;
#endif
}

static void wait_status(void)
{
	for(volatile unsigned i = 0; i < 12000000; i++)
		;
}

static void wait_for_keys_released(void)
{
	fxcg100_wait_keys_released();
}

static void prepare_exit_to_os(const char *detail)
{
	cgba_frame_limiter_stop();
	draw_status("exiting gpSP...", detail);
	/* Do not enter Fugue or hand control back to the launcher while the menu
	 * activation key is still physically down. In particular, the initial-menu
	 * EXIT path used to return immediately with EXE/HOME held. */
	wait_for_keys_released();
	/* Finish direct-LCD DMA and restore the full panel window before any final
	 * BFile world switches. kquit() can then restore the OS drivers from a
	 * quiescent add-in state. */
	fxcg100_lcd_shutdown();
}

static int exit_to_os(int code)
{
	prepare_exit_to_os(NULL);
	return code;
}

static int shutdown_gpsp_and_exit(int code)
{
	/* The dirty backup flush in cgba_gpsp_shutdown() can take visibly longer
	 * than a clean exit. Publish the status first, then quiesce input/display
	 * before the ROM-close and save-file OS calls. */
	prepare_exit_to_os("saving backup if changed");
	cgba_gpsp_shutdown();
	return code;
}

static unsigned normalize_rom_id(unsigned rom_id)
{
	unsigned count = cgba_gpsp_rom_count();

	return count ? rom_id % count : 0;
}

int cgba_start_gpsp(uint16_t *framebuffer, unsigned rom_id)
{
	draw_status("clearing gpSP memory", NULL);

	if(!gpsp_highbss_range_ok()) {
#if defined(CGBA_FXCG100)
		char detail[48];
		if(cgba_loader_arena_busy)
			snprintf(detail, sizeof(detail), "_ld1 busy (%08lx)",
				(unsigned long)cgba_loader_ram_end);
		else if(cgba_loader_ram_end)
			snprintf(detail, sizeof(detail), "%08lx > %08lx",
				(unsigned long)(uintptr_t)cgba_highbss_end,
				(unsigned long)cgba_loader_ram_end);
		else
			snprintf(detail, sizeof(detail), "update mpm.bin (1.1+)");
		draw_status("unsafe gpSP RAM map", detail);
#else
		draw_status("unsafe gpSP RAM map", "not starting");
#endif
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

#ifdef CGBA_DYNAREC
	cgba_gpsp_jit_flush_mark();
#endif
#if defined(CGBA_DYNAREC) && \
	(defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS))
	cgba_diag_counters_reset(1);
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


static int gpsp_main(void)
{
	#if defined(CGBA_FXCG50) && !defined(CGBA_CLASSPAD)
	if(!cgba_hottext_init()) {
		dclear(C_WHITE);
		dtext(8, 8, C_BLACK, "gpSP RAM-code copy failed");
		dtext(8, 36, C_BLACK, "No emulator code was executed.");
		dupdate();
		return 1;
	}
	#endif
	uint16_t *framebuffer = cgba_framebuffer;
	#ifdef CGBA_GPSP_HEADLESS_TEST
	return cgba_headless_test(framebuffer);
	#endif
	cgba_overclock_init();
	fxcg100_menu_state menu_state;
	uint32_t previous_app_keys = 0;
	uint32_t previous_hotkeys = 0;
	int ff_toggled = 0;
	int previous_fast_forward = 0;
	int ff_overlay_clear_pending = 0;
#ifdef CGBA_CLASSPAD
	uint32_t previous_strip = 0;
#endif
	uint32_t last_hash = 0;
	unsigned current_rom;
	unsigned frame = 1;
	fxcg100_menu_result menu_result;
	cgba_frame_limiter frame_limiter;
	cgba_pacer pacer;
	static fxcg100_debug_info debug_info;

	cgba_frame_limiter_init(&frame_limiter);
	cgba_pacer_init(&pacer, 60, 9);

	fxcg100_lcd_init();
	cgba_crash_reporting_init();
	cgba_gpsp_refresh_roms();
	fxcg100_menu_init(&menu_state);
	status_state = &menu_state;
	fxcg100_lcd_set_backlight(menu_state.backlight, menu_state.backlight_pwm);
#ifdef CGBA_CLASSPAD
	cp_ui_apply_saved_clock();
#endif
	if(cgba_gpsp_rom_count() > 0) {
		uint32_t order[FXCG100_ROM_SORT_MAX];
		uint32_t n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
			menu_state.game_sort);
		if(n > 0)
			menu_state.rom_source = order[0];
	}

	menu_result = fxcg100_menu_run(&menu_state, 0, 0, NULL);
	apply_display_options(&menu_state);
	if(menu_result == FXCG100_MENU_QUIT)
		return exit_to_os(1);
	wait_for_keys_released();
	previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);

	current_rom = normalize_rom_id(menu_state.rom_source);
	if(menu_result == FXCG100_MENU_SAVE_STATE) {
		draw_status("nothing to save yet", "booting selected ROM");
		wait_status();
	}

	if(cgba_start_gpsp(framebuffer, current_rom) != 0)
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
		int reader_hotkey_edge =
			(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_TXT_READER)) != 0;

		if(menu_open_edge || reader_hotkey_edge) {
			if(reader_hotkey_edge && !menu_open_edge)
				fxcg100_menu_open_reader();
			last_hash = cgba_gpsp_frame_hash(framebuffer);
			/* The storage ROM list is immutable for this process. In particular,
			 * never rescan it while entering the menu: on fx-CG100 that would
			 * world-switch into Fugue while the physical ON key can still be held,
			 * which can leave the OS call stuck. The one startup scan is enough;
			 * restart gpSP to discover files added after launch. */
			menu_state.rom_source = normalize_rom_id(menu_state.rom_source);
			/* The frame clock must not run on while the menu is open. */
			cgba_frame_limiter_stop();
			fxcg100_lcd_sync_pace_reset();
			cgba_gpsp_debug_menu(&debug_info, frame, last_hash,
				cgba_fps.emu_fps, cgba_fps.draw_fps, framebuffer,
				read_stack_pointer());
#ifndef CGBA_CLASSPAD
			status_frame = NULL;
#endif
			fxcg100_menu_result result;
			int storage_failed = 0;
			for(;;) {
				result = fxcg100_menu_run(&menu_state, frame, last_hash,
					&debug_info);
#ifndef CGBA_CLASSPAD
				/* fx-CG: the menu stays open while a state is saved, and
				 * says how it went in its footer. Loading goes back to the
				 * game, or reports in the footer when it fails. */
				if(result == FXCG100_MENU_SAVE_STATE ||
						result == FXCG100_MENU_LOAD_STATE) {
					int save = result == FXCG100_MENU_SAVE_STATE, ok;
					unsigned slot = (unsigned)menu_state.savestate_slot;
					char line[96];

					snprintf(line, sizeof line, save
						? "Saving state to slot %u..."
						: "Loading state from slot %u...", slot);
					cg_ui_show_progress(line);
					/* Do not enter a Fugue world switch while OK/EXE is still
					 * physically held: on fx-CG100 that can leave the OS call
					 * or return stuck with the flash icon still on screen. */
					wait_for_keys_released();
					if(!save && !cgba_gpsp_storage_sync()) {
						storage_failed = 1;
						break;
					}
					ok = save ? cgba_gpsp_state_save(slot)
						: cgba_gpsp_state_load(slot);
					if(save && !ok && !cgba_gpsp_storage_sync()) {
						storage_failed = 1;
						break;
					}
					if(ok && !save) {
						result = FXCG100_MENU_RETURN;
						break;
					}
					snprintf(line, sizeof line, ok ? "State saved to slot %u"
						: save ? "Could not save to slot %u"
						: "Could not load slot %u", slot);
#ifdef CGBA_SAVE_TIMING
					if(save) {
						char t[64];

						/* The check mark says it was saved; a failure
						 * shows in the error colour. */
						cgba_save_timing_text(t, sizeof t);
						snprintf(line, sizeof line, "%s", t);
					}
#endif
					cg_ui_set_result(line, !ok);
					continue;
				}
#endif
				break;
			}
			apply_display_options(&menu_state);
			if(storage_failed) {
				draw_status("ROM remap FAILED", "restart or reset required");
				wait_status();
				break;
			}

			wait_for_keys_released();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			if(result == FXCG100_MENU_QUIT)
				break;
			if(menu_state.fast_forward_mode != FXCG100_FF_MODE_TOGGLE)
				ff_toggled = 0;
			if(result == FXCG100_MENU_RESET) {
				cgba_gpsp_shutdown();
				if(cgba_start_gpsp(framebuffer, current_rom) != 0)
					return exit_to_os(1);
				ff_toggled = 0;
				previous_fast_forward = 0;
				ff_overlay_clear_pending = 0;
				cgba_pacer_reset(&pacer);
				frame = 0;
				enter_gameplay_display(framebuffer, 1);
				continue;
			}
			if(result == FXCG100_MENU_LOAD_GAME) {
				current_rom = normalize_rom_id(menu_state.rom_source);
				cgba_gpsp_shutdown();
				if(cgba_start_gpsp(framebuffer, current_rom) != 0)
					return exit_to_os(1);
				ff_toggled = 0;
				previous_fast_forward = 0;
				ff_overlay_clear_pending = 0;
				cgba_pacer_reset(&pacer);
				frame = 0;
				enter_gameplay_display(framebuffer, 1);
				continue;
			}
			/* CONFIG SAVE also mutates Fugue while the menu is open. Before
			 * resuming the same game, refresh every direct NOR mapping even
			 * when no savestate/backup write followed it. */
			if(!cgba_gpsp_storage_sync()) {
				draw_status("ROM remap FAILED", "restart or reset required");
				wait_status();
				break;
			}
			if(result == FXCG100_MENU_LOAD_STATE ||
					result == FXCG100_MENU_SAVE_STATE) {
				int save = result == FXCG100_MENU_SAVE_STATE;
				char slot_line[64];
				int ok;
				snprintf(slot_line, sizeof slot_line, "slot %u",
					(unsigned)menu_state.savestate_slot);
				draw_status(save ? "saving state (416KB)..."
					: "loading state...", slot_line);
				ok = save ? cgba_gpsp_state_save(menu_state.savestate_slot)
					: cgba_gpsp_state_load(menu_state.savestate_slot);
				if(save && !ok && !cgba_gpsp_storage_sync()) {
					draw_status("ROM remap FAILED",
						"restart or reset required");
					wait_status();
					break;
				}
#ifdef CGBA_SAVE_TIMING
				if(save)
					cgba_save_timing_text(slot_line, sizeof slot_line);
#endif
				draw_status(ok ? (save ? "state saved" : "state loaded")
					: (save ? "state save FAILED"
						: "state load FAILED (no file?)"), slot_line);
				wait_status();
#ifdef CGBA_SAVE_TIMING
				wait_status();
				wait_status();
#endif
			}
			enter_gameplay_display(framebuffer, frame);
			continue;
		}

#ifdef CGBA_CLASSPAD
		classpad_strip_shortcuts(&menu_state, &previous_strip);
#endif

		if(hotkey_edge &
				FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_DISPLAY_FPS))
			menu_state.show_fps = menu_state.show_fps ? 0 : 1;

		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_EXIT)) {
			char slot_line[16];
			int ok, storage_ok;

			cgba_frame_limiter_stop();
			snprintf(slot_line, sizeof slot_line, "Slot %u",
				(unsigned)menu_state.savestate_slot);
			draw_status("Saving & exiting", slot_line);
			wait_for_keys_released();
			ok = cgba_gpsp_state_save_for_exit(menu_state.savestate_slot);
			if(ok)
				break;
			storage_ok = cgba_gpsp_storage_sync();
			draw_status(storage_ok ? "Could not save the state"
					: "ROM remap FAILED",
				storage_ok ? slot_line : "restart or reset required");
			wait_status();
			if(!storage_ok) {
				wait_status();
				break;
			}
			fxcg100_lcd_sync_pace_reset();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			enter_gameplay_display(framebuffer, frame);
			continue;
		}

		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_LOAD_STATE)) {
#if !(defined(CGBA_DYNAREC) && defined(CGBA_SH4_PROFILE_COUNTERS))
			char slot_line[16];

			snprintf(slot_line, sizeof slot_line, "Slot %u",
				(unsigned)menu_state.savestate_slot);
#endif
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
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			enter_gameplay_display(framebuffer, frame);
			continue;
#else
			cgba_frame_limiter_stop();
			fxcg100_lcd_sync_pace_reset();
			draw_status("Loading state", slot_line);
			wait_for_keys_released();
			draw_status(cgba_gpsp_state_load(menu_state.savestate_slot)
				? "State loaded" : "Could not load the state", slot_line);
			wait_status();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			enter_gameplay_display(framebuffer, frame);
			continue;
#endif
		}
		if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_STATE)) {
#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_PROFILE_COUNTERS)
			/* Profiling build: repurpose the (still unimplemented) savestate
			 * hotkey as a live JIT/interpreter A/B switch. The cores share
			 * reg[] and the switch lands on a frame boundary (PC committed),
			 * which is exactly how the diff harness alternates them. */
			dynarec_enable = !dynarec_enable;
			if(dynarec_enable) {
				extern void flush_translation_cache_ram(void);
				/* The interpreter does not invalidate translated RAM blocks
				 * on SMC stores while dynarec_enable == 0. */
				flush_translation_cache_ram();
			}
			draw_status("core switched",
				dynarec_enable ? "JIT (dynarec)" : "INTERPRETER");
			wait_status();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			enter_gameplay_display(framebuffer, frame);
			continue;
#else
			{
				char slot_line[16];
				int ok, storage_ok;

				cgba_frame_limiter_stop();
				fxcg100_lcd_sync_pace_reset();
				snprintf(slot_line, sizeof slot_line, "Slot %u",
					(unsigned)menu_state.savestate_slot);
				draw_status("Saving state", slot_line);
				wait_for_keys_released();
				ok = cgba_gpsp_state_save(menu_state.savestate_slot);
				storage_ok = ok || cgba_gpsp_storage_sync();
#ifdef CGBA_SAVE_TIMING
				char timing[64];

				cgba_save_timing_text(timing, sizeof timing);
#endif
				draw_status(ok ? "State saved" :
					(storage_ok ? "Could not save the state"
						: "ROM remap FAILED"),
					!storage_ok ? "restart or reset required"
#ifdef CGBA_SAVE_TIMING
					: timing);
				wait_status();
				wait_status();
				wait_status();
#else
					: slot_line);
#endif
				if(!storage_ok) {
					wait_status();
					break;
				}
			}
			wait_status();
			previous_app_keys = fxcg100_poll_app_keys();
			previous_hotkeys =
				fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
			enter_gameplay_display(framebuffer, frame);
			continue;
#endif
		}

		int fast_forward;
		if(menu_state.fast_forward_mode == FXCG100_FF_MODE_TOGGLE) {
			if(hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD))
				ff_toggled = !ff_toggled;
			fast_forward = ff_toggled;
		} else {
			ff_toggled = 0;
			fast_forward = (hotkeys &
				FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD)) != 0;
		}
		if(previous_fast_forward && !fast_forward) {
			cgba_pacer_reset(&pacer);
			ff_overlay_clear_pending = 1;
		}
		int limit_on = menu_state.frame_limit && !fast_forward;
		int vsync_paced = limit_on && fxcg100_lcd_sync_is_paced();
		if(!vsync_paced)
			fxcg100_lcd_sync_pace_reset();
		{
			uint32_t limit_us = 0u;

			if(fast_forward)
				limit_us = cgba_fast_forward_period_us(
					menu_state.fast_forward_speed);
			else if(limit_on && !vsync_paced)
				limit_us = CGBA_FRAME_LIMIT_US;
			cgba_frame_limiter_begin_period(&frame_limiter, limit_us);
		}
		gba_buttons = fxcg100_poll_gba_buttons_mapped(menu_state.keymap);

		int render_video;
		if(fast_forward) {
			uint32_t period = cgba_fast_forward_render_period(
				menu_state.fast_forward_speed);
			render_video = frame == 1 || !previous_fast_forward ||
				(frame % period) == 0;
		} else if(frame == 1 || ff_overlay_clear_pending) {
			render_video = 1;
		} else if(menu_state.frameskip_type == CGBA_FRAMESKIP_AUTOMATIC) {
			render_video = cgba_pacer_should_render(&pacer);
		} else {
			render_video = fxcg100_menu_should_blit(&menu_state, frame);
		}

		int overlay_active = menu_state.show_fps || fast_forward;
		cgba_gpsp_run_frame(gba_buttons, render_video);
		cgba_fps_tick(&cgba_fps, render_video);
		if(render_video) {
			if(overlay_active) {
				if(menu_state.show_fps)
					fxcg100_lcd_overlay_fps(framebuffer,
						cgba_fps.emu_fps, cgba_fps.draw_fps);
				if(fast_forward)
					fxcg100_lcd_overlay_fast_forward(framebuffer,
						menu_state.fast_forward_speed);
				blit_gba_frame(framebuffer, frame, gba_buttons);
				ff_overlay_clear_pending = 0;
			}
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
			else if(ff_overlay_clear_pending ||
				!fxcg100_lcd_is_synced()) {
				blit_gba_frame(framebuffer, frame, gba_buttons);
				ff_overlay_clear_pending = 0;
			} else if(cgba_video_frame_dirty_rows != 0) {
				fxcg100_lcd_blit_gba_dirty(framebuffer,
					cgba_video_frame_dirty_row_mask);
			}
#else
			else {
				blit_gba_frame(framebuffer, frame, gba_buttons);
				ff_overlay_clear_pending = 0;
			}
#endif
		}
		previous_app_keys = app_keys;
		previous_hotkeys = hotkeys;
		previous_fast_forward = fast_forward;
		if(vsync_paced) {
			int late = -1;
			if(fxcg100_lcd_sync_pace_wait(CGBA_LIMITER_MAX_LATE, &late))
				cgba_pacer_feedback(&pacer, late);
		} else {
			cgba_frame_limiter_wait(&frame_limiter);
			cgba_pacer_feedback(&pacer, fast_forward ? -1 : frame_limiter.late);
		}
	}

	/* Quit from the menu: a normal exit. */
	return shutdown_gpsp_and_exit(0);
}

int main(void)
{
#ifdef CGBA_CLASSPAD
	/* Nothing but gint may be used until the arena is reserved and the core
	 * is loaded, and nothing in the arena after it is released. */
	const char *error = cgba_classpad_arena_init();
	int code;

	if(error) {
		dclear(C_WHITE);
		dtext(8, 8, C_BLACK, "gpSP cannot start:");
		dtext(8, 28, C_BLACK, error);
		dtext(8, 56, C_BLACK, "No emulator code was executed.");
		/* Starting a built-in application makes the OS set its heap up
		 * again, which clears whatever is in the way. */
		dtext(8, 84, C_BLACK, "Open any built-in app (Main, for");
		dtext(8, 104, C_BLACK, "example), then start gpSP again.");
		dtext(8, 132, C_BLACK, "Press any key to exit.");
		dupdate();
		getkey();
		return 1;
	}
	code = gpsp_main();
	cgba_classpad_arena_release();
	return code;
#else
#if defined(CGBA_FXCG100)
	cgba_claim_loader_arena();
#endif
	return gpsp_main();
#endif
}
