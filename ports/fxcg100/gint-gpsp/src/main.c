#include <gint/display.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "frame_pacing.h"
#include "gpsp_runner.h"

/* Menu frameskip types (order matches frameskip_options[] in fxcg100_menu.c). */
#define CGBA_FRAMESKIP_AUTOMATIC 0
#define CGBA_FAST_FORWARD_RENDER_PERIOD 8u

#define CGBA_HIGH_BSS __attribute__((section(".cgba.highbss"), aligned(32)))
#define CGBA_HIGHRAM_SAFE_START ((uintptr_t)0x8c200000u)
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c780000u)

extern char cgba_highbss_start[];
extern char cgba_highbss_end[];

static uint16_t cgba_framebuffer[CGBA_GBA_BUFFER_PIXELS] CGBA_HIGH_BSS;

/* FPS metrics meter (emulated + rendered frame rate), shown when the menu's
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
	char lines[8][CGBA_DIAG_LINE_MAX];
	unsigned n, i;

	/* Render the final frame (render_video=1): update_scanline() early-returns
	 * while skip_next_frame is set, so a skipped run leaves the framebuffer stale
	 * and fbhash/center would read blank. */
	for(i = 0; i < 30; i++)
		cgba_gpsp_run_frame(FXCG100_GBA_BUTTON_NONE, i == 29);

	n = cgba_gpsp_diag(lines, 8);
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

static int cgba_headless_test(uint16_t *framebuffer)
{
	char lines[8][CGBA_DIAG_LINE_MAX];
	char buf[64];
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

	hputs_dbg("loaded OK; running frames");
	cgba_fps_init(&cgba_fps);
	for(i = 0; i < 48; i++) {
		int rendered = (i % 4) == 0;

		cgba_gpsp_run_frame(FXCG100_GBA_BUTTON_NONE, rendered);
		cgba_fps_tick(&cgba_fps, rendered);
		if(rendered) {
			/* Exercise the real blit path incl. the no-final-wait DMA overlap. */
			fxcg100_lcd_overlay_fps(framebuffer, cgba_fps.emu_fps,
				cgba_fps.vid_fps);
			fxcg100_lcd_blit_gba(framebuffer);
		}
	}

	/* Exercise the FPS overlay so the font + framebuffer write are validated;
	 * px[1,1] should become white (0xFFFF, the 'F' glyph), px[0,0] black. */
	fxcg100_lcd_overlay_fps(framebuffer, cgba_fps.emu_fps, cgba_fps.vid_fps);
	snprintf(buf, sizeof buf, "fps emu=%u vid=%u px00=%04X px11=%04X",
		(unsigned)cgba_fps.emu_fps, (unsigned)cgba_fps.vid_fps,
		framebuffer[0], framebuffer[1 * 240 + 1]);
	hputs_dbg(buf);

	n = cgba_gpsp_diag(lines, 8);
	for(i = 0; i < n; i++)
		hputs_dbg(lines[i]);
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
		return 1;
	wait_for_keys_released();
	previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);

	current_rom = normalize_rom_id(menu_state.rom_source);
	if(menu_result == FXCG100_MENU_LOAD_STATE ||
			menu_result == FXCG100_MENU_SAVE_STATE) {
		draw_status("savestate unavailable", "booting selected ROM");
		wait_status();
	}

	if(start_gpsp(framebuffer, current_rom) != 0)
		return 1;

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
					return 1;
				frame = 1;
				enter_gameplay_display(framebuffer, frame);
				continue;
			}
			if(result == FXCG100_MENU_LOAD_GAME) {
				current_rom = normalize_rom_id(menu_state.rom_source);
				cgba_gpsp_shutdown();
				if(start_gpsp(framebuffer, current_rom) != 0)
					return 1;
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
					cgba_fps.emu_fps, cgba_fps.vid_fps);
			blit_gba_frame(framebuffer, frame, gba_buttons);
		}
		previous_app_keys = app_keys;
		previous_hotkeys = hotkeys;
	}

	cgba_gpsp_shutdown();
	return 1;
}
