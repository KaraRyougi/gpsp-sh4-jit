#include <gint/display.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "frame_pacing.h"
#include "gpsp_runner.h"

/* Menu frameskip types (order matches frameskip_options[] in fxcg100_menu.c). */
#define CGBA_FRAMESKIP_AUTOMATIC 0

#define CGBA_HIGH_BSS __attribute__((section(".cgba.highbss"), aligned(32)))
#define CGBA_HIGHRAM_SAFE_START ((uintptr_t)0x8c200000u)
#define CGBA_HIGHRAM_SAFE_END   ((uintptr_t)0x8c780000u)

extern char cgba_highbss_start[];
extern char cgba_highbss_end[];

static uint16_t cgba_framebuffer[CGBA_GBA_BUFFER_PIXELS] CGBA_HIGH_BSS;

static int app_chord(uint32_t keys, uint32_t chord)
{
	return (keys & chord) == chord;
}

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

int main(void)
{
	uint16_t *framebuffer = cgba_framebuffer;
	fxcg100_menu_state menu_state;
	uint32_t previous_app_keys = 0;
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

	current_rom = normalize_rom_id(menu_state.rom_source);
	if(menu_result == FXCG100_MENU_LOAD_STATE ||
			menu_result == FXCG100_MENU_SAVE_STATE) {
		draw_status("savestate unavailable", "booting selected ROM");
		wait_status();
	}

	if(start_gpsp(framebuffer, current_rom) != 0)
		return 1;

	enter_gameplay_display(framebuffer, frame);

	for(;; frame++) {
		uint32_t app_keys = fxcg100_poll_app_keys();
		uint32_t gba_buttons = FXCG100_GBA_BUTTON_NONE;
		int menu_open_edge =
			((app_keys & FXCG100_APPKEY_MENU) &&
			 !(previous_app_keys & FXCG100_APPKEY_MENU)) ||
			(app_chord(app_keys, FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_EXE) &&
			 !app_chord(previous_app_keys,
				FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_EXE));

		if(app_chord(app_keys, FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_HOME))
			break;

		if(app_chord(app_keys, FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_AC) &&
				!app_chord(previous_app_keys,
				FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_AC)) {
			cgba_gpsp_shutdown();
			if(start_gpsp(framebuffer, current_rom) != 0)
				return 1;
			frame = 1;
			enter_gameplay_display(framebuffer, frame);
			previous_app_keys = app_keys;
			continue;
		}

		if(menu_open_edge) {
			last_hash = cgba_gpsp_frame_hash(framebuffer);
			cgba_gpsp_refresh_roms();
			menu_state.rom_source = normalize_rom_id(menu_state.rom_source);
			fxcg100_menu_result result =
				fxcg100_menu_run(&menu_state, frame, last_hash);

			wait_for_keys_released();
			previous_app_keys = fxcg100_poll_app_keys();
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

		if((app_keys & FXCG100_APPKEY_SHIFT) == 0)
			gba_buttons = fxcg100_poll_gba_buttons();

		int render_video;
		if(frame == 1)
			render_video = 1;
		else if(menu_state.frameskip_type == CGBA_FRAMESKIP_AUTOMATIC)
			render_video = cgba_pacer_should_render(&pacer);
		else
			render_video = fxcg100_menu_should_blit(&menu_state, frame);

		cgba_gpsp_run_frame(gba_buttons, render_video);
		if(render_video)
			blit_gba_frame(framebuffer, frame, gba_buttons);
		previous_app_keys = app_keys;
	}

	cgba_gpsp_shutdown();
	return 1;
}
