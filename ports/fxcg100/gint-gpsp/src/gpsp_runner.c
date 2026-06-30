#include "gpsp_runner.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <streams/file_stream.h>

#include "fxcg100_platform.h"
#include "nor_rom.h"
#include "vendor/gpsp/common.h"

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
	dynarec_enable = 1;   /* JIT on by default (SAVE_STATE hotkey still toggles) */
	sprite_limit = 1;
	reset_gba();
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
			"siocnt=%04X irqcyc=%lu oamupd=%lu",
			frame, phase, read_ioreg(REG_DISPCNT),
			read_ioreg(REG_DISPSTAT), read_ioreg(REG_VCOUNT),
			read_ioreg(REG_P1), read_ioreg(REG_IE), read_ioreg(REG_IF),
			read_ioreg(REG_IME), read_ioreg(REG_WAITCNT),
			read_ioreg(REG_SIOCNT), (unsigned long)serial_get_irq_cycles(),
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
	if(render_video && cgba_mode3_debug_copy_active)
		copy_mode3_vram_to_framebuffer();
}

#ifdef CGBA_DYNAREC
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
		unsigned f = frame ? frame : 1;
		uint32_t arm_helpers = cgba_sh4_helper_arm_ldst_count +
			cgba_sh4_helper_arm_block_count + cgba_sh4_helper_arm_dp_count +
			cgba_sh4_helper_arm_mul_count + cgba_sh4_helper_arm_psr_count +
			cgba_sh4_helper_arm_swap_count;
		uint32_t thumb_helpers = cgba_sh4_helper_thumb_ldst_count +
			cgba_sh4_helper_thumb_block_count +
			cgba_sh4_helper_thumb_shift_count +
			cgba_sh4_helper_thumb_dp_count;
		debug_line(debug, "JIT FL R%lu M%lu TX A%lu T%lu",
			(unsigned long)cgba_dynarec_rom_flush_count,
			(unsigned long)cgba_dynarec_ram_flush_count,
			(unsigned long)cgba_dynarec_arm_translate_count,
			(unsigned long)cgba_dynarec_thumb_translate_count);
		debug_line(debug, "JIT TX/f A%lu T%lu H/f A%lu T%lu",
			(unsigned long)(cgba_dynarec_arm_translate_count / f),
			(unsigned long)(cgba_dynarec_thumb_translate_count / f),
			(unsigned long)(arm_helpers / f),
			(unsigned long)(thumb_helpers / f));
		debug_line(debug, "JIT LK A%lu T%lu D%lu IC%lu/%luk",
			(unsigned long)cgba_dynarec_lookup_arm_count,
			(unsigned long)cgba_dynarec_lookup_thumb_count,
			(unsigned long)cgba_dynarec_lookup_dual_count,
			(unsigned long)cgba_dynarec_icache_sync_count,
			(unsigned long)(cgba_dynarec_icache_sync_bytes / 1024u));
		debug_line(debug, "JIT IH hit A%lu T%lu DA%lu DT%lu",
			(unsigned long)cgba_dynarec_ibh_arm_hit_count,
			(unsigned long)cgba_dynarec_ibh_thumb_hit_count,
			(unsigned long)cgba_dynarec_ibh_dual_arm_hit_count,
			(unsigned long)cgba_dynarec_ibh_dual_thumb_hit_count);
		debug_line(debug, "JIT IH slow A%lu T%lu D%lu",
			(unsigned long)cgba_dynarec_ibh_arm_slow_count,
			(unsigned long)cgba_dynarec_ibh_thumb_slow_count,
			(unsigned long)cgba_dynarec_ibh_dual_slow_count);
		debug_line(debug, "JIT IH hot DA%lu DT%lu",
			(unsigned long)cgba_dynarec_ibh_dual_hot_arm_count,
			(unsigned long)cgba_dynarec_ibh_dual_hot_thumb_count);
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
				}
			}
		}
		debug_line(debug, "H ARM ld%lu st%lu blk%lu dp%lu",
			(unsigned long)cgba_sh4_helper_arm_ldst_load_count,
			(unsigned long)cgba_sh4_helper_arm_ldst_store_count,
			(unsigned long)cgba_sh4_helper_arm_block_count,
			(unsigned long)cgba_sh4_helper_arm_dp_count);
		debug_line(debug, "H ARM mul%lu psr%lu swp%lu",
			(unsigned long)cgba_sh4_helper_arm_mul_count,
			(unsigned long)cgba_sh4_helper_arm_psr_count,
			(unsigned long)cgba_sh4_helper_arm_swap_count);
		debug_line(debug, "H TH ld%lu blk%lu sh%lu dp%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_count,
			(unsigned long)cgba_sh4_helper_thumb_block_count,
			(unsigned long)cgba_sh4_helper_thumb_shift_count,
			(unsigned long)cgba_sh4_helper_thumb_dp_count);
		debug_line(debug, "H TLD l%lu s%lu ram%lu io%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_load_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_store_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_ram_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_io_count);
		debug_line(debug, "H TLD vid%lu rom%lu oth%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_video_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_rom_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_other_count);
		debug_line(debug, "H TWHY U%lu GA%lu HA%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_unmapped_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_guest_unaligned_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_host_unaligned_count);
		debug_line(debug, "H TWHY R%lu S%lu OK%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_unsafe_region_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_smc_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_native_ready_count);
		debug_line(debug, "H TK W%lu B%lu H%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_word_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_byte_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_half_count);
		debug_line(debug, "H TS PC%lu SP%lu R%lu I%lu",
			(unsigned long)cgba_sh4_helper_thumb_ldst_pc_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_sp_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_reg_count,
			(unsigned long)cgba_sh4_helper_thumb_ldst_imm_count);
		debug_line(debug, "H LD ram%lu io%lu vid%lu rom%lu oth%lu",
			(unsigned long)cgba_sh4_helper_arm_ldst_ram_count,
			(unsigned long)cgba_sh4_helper_arm_ldst_io_count,
			(unsigned long)cgba_sh4_helper_arm_ldst_video_count,
			(unsigned long)cgba_sh4_helper_arm_ldst_rom_count,
			(unsigned long)cgba_sh4_helper_arm_ldst_other_count);
		debug_line(debug, "H ABLK load%lu store%lu div%lu",
			(unsigned long)cgba_sh4_helper_arm_block_load_count,
			(unsigned long)cgba_sh4_helper_arm_block_store_count,
			(unsigned long)cgba_sh4_helper_hle_div_count);
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
#ifdef CGBA_DYNAREC
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
	if(!cgba_lcd_test_active)
		memory_term();
	cgba_nor_rom_close(&cgba_current_nor_rom);
	cgba_lcd_test_active = 0;
	cgba_mode3_debug_copy_active = 0;
	cgba_active_framebuffer = NULL;
}
