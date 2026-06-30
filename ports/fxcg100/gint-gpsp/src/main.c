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
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
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

static int headless_load_checkpoint(void)
{
	void *state = cgba_sh4_checkpoint_buffer();
	unsigned size = cgba_sh4_checkpoint_size();
	int read_ok = headless_read_blob_contents(headless_checkpoint_path,
		state, size);
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
	unsigned n, i, rom, frame_base, frame_end;

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
	#endif
	snprintf(buf, sizeof buf, "input START f=%u h=%u A/SHIFT f=%u h=%u p=%u w=%u",
		(unsigned)CGBA_GPSP_HEADLESS_START_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_START_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_FRAME,
		(unsigned)CGBA_GPSP_HEADLESS_A_HOLD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PERIOD,
		(unsigned)CGBA_GPSP_HEADLESS_A_PRESS);
	hputs_dbg(buf);

	frame_base = headless_frame_base();
	frame_end = headless_frame_end();
	snprintf(buf, sizeof buf, "loaded OK; running %u frames [%u,%u)",
		(unsigned)CGBA_GPSP_HEADLESS_FRAMES, frame_base, frame_end);
	hputs_dbg(buf);
	cgba_fps_init(&cgba_fps);
	for(i = 0; i < CGBA_GPSP_HEADLESS_FRAMES; i++) {
		unsigned frame = frame_base + i;
		int rendered = (frame % 4) == 0;
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
	#if CGBA_GPSP_HEADLESS_BENCH_FRAMES > 0
	snprintf(buf, sizeof buf,
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count);
	hputs_dbg(buf);
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
		"jit stats rom_flush=%lu ram_flush=%lu arm_tx=%lu thumb_tx=%lu",
		(unsigned long)cgba_dynarec_rom_flush_count,
		(unsigned long)cgba_dynarec_ram_flush_count,
		(unsigned long)cgba_dynarec_arm_translate_count,
		(unsigned long)cgba_dynarec_thumb_translate_count);
	hputs_dbg(buf);
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
			cgba_gpsp_debug_menu(&debug_info, frame, last_hash,
				cgba_fps.emu_fps, cgba_fps.draw_fps, framebuffer,
				read_stack_pointer());
			fxcg100_menu_result result =
				fxcg100_menu_run(&menu_state, frame, last_hash, &debug_info);

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
