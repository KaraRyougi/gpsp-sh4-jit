#include "gpsp_runner.h"

#include <stdio.h>
#include <string.h>

#include "fxcg100_platform.h"
#include "nor_rom.h"
#include "test_rom/input_probe.h"
#include "test_rom/mode3_smoke.h"
#include "vendor/gpsp/common.h"

typedef struct cgba_rom_source {
	const char *name;
	const uint8_t *data;
	uint32_t size;
	int lcd_test;
	int mode3_debug_copy;
} cgba_rom_source;

static const cgba_rom_source cgba_rom_sources[] = {
	[CGBA_GPSP_ROM_MODE3_SMOKE] = {
		"MODE3 SMOKE",
		cgba_mode3_smoke_gba,
		cgba_mode3_smoke_gba_len,
		0,
		1,
	},
	[CGBA_GPSP_ROM_INPUT_PROBE] = {
		"INPUT PROBE",
		cgba_input_probe_gba,
		cgba_input_probe_gba_len,
		0,
		1,
	},
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
static uint16_t *cgba_active_framebuffer;

unsigned cgba_gpsp_refresh_roms(void)
{
	cgba_storage_roms_scanned = 1;
	return cgba_nor_rom_scan_gba(&cgba_storage_roms);
}

static void ensure_storage_roms_scanned(void)
{
	if(!cgba_storage_roms_scanned)
		cgba_gpsp_refresh_roms();
}

const char *cgba_gpsp_rom_name(unsigned rom_id)
{
	ensure_storage_roms_scanned();

	if(rom_id < CGBA_GPSP_ROM_BUILTIN_COUNT)
		return cgba_rom_sources[rom_id].name;

	rom_id -= CGBA_GPSP_ROM_BUILTIN_COUNT;
	if(rom_id < cgba_storage_roms.count)
		return cgba_storage_roms.entries[rom_id].label;

	return cgba_rom_sources[CGBA_GPSP_ROM_INPUT_PROBE].name;
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
	ensure_storage_roms_scanned();

	if(rom_id < CGBA_GPSP_ROM_BUILTIN_COUNT)
		rom = &cgba_rom_sources[rom_id];
	else {
		unsigned storage_id = rom_id - CGBA_GPSP_ROM_BUILTIN_COUNT;
		if(storage_id < cgba_storage_roms.count)
			nor_entry = &cgba_storage_roms.entries[storage_id];
	}
	if(!rom && !nor_entry) {
		rom_id = CGBA_GPSP_ROM_INPUT_PROBE;
		rom = &cgba_rom_sources[rom_id];
	}

	memset(framebuffer, 0, CGBA_GBA_BUFFER_PIXELS * sizeof(*framebuffer));
	gba_screen_pixels = framebuffer;
	cgba_active_framebuffer = framebuffer;
	cgba_lcd_test_active = rom ? rom->lcd_test : 0;
	cgba_mode3_debug_copy_active = rom ? rom->mode3_debug_copy : 0;

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
		if(load_gamepak_from_pages(cgba_current_nor_rom.pages,
				cgba_current_nor_rom.padded_size,
				FEAT_DISABLE, FEAT_DISABLE,
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
			FEAT_DISABLE, FEAT_DISABLE,
			SERIAL_MODE_DISABLED) != 0)
		return -2;

	selected_boot_mode = boot_game;
	dynarec_enable = 0;
	sprite_limit = 1;
	reset_gba();
	return 0;
}

const char *cgba_gpsp_last_error(void)
{
	return cgba_last_error[0] ? cgba_last_error : NULL;
}

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

void cgba_gpsp_run_frame(uint32_t gba_buttons, int render_video)
{
	if(cgba_lcd_test_active) {
		static uint32_t test_frame;
		fill_lcd_test_frame(test_frame++);
		return;
	}

	gpsp_set_input_state_bits(gba_buttons & 0x3ff);
	update_input();
	skip_next_frame = render_video ? 0 : 1;
	clear_gamepak_stickybits();
	execute_arm(execute_cycles);
	skip_next_frame = 0;
	if(render_video && cgba_mode3_debug_copy_active)
		copy_mode3_vram_to_framebuffer();
}

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

void cgba_gpsp_shutdown(void)
{
	if(!cgba_lcd_test_active)
		memory_term();
	cgba_nor_rom_close(&cgba_current_nor_rom);
	cgba_lcd_test_active = 0;
	cgba_mode3_debug_copy_active = 0;
	cgba_active_framebuffer = NULL;
}
