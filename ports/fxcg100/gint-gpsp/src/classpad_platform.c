/*
 * fx-CP400 (ClassPad II) display, touch and key layer. This is the ClassPad
 * counterpart of gint_platform.c and implements the same fxcg100_lcd_* and
 * key-polling interface, so the menu, the runner and main() stay shared.
 *
 * Display. The panel is a 320x528 R61523 scanned in portrait order. Menus
 * and status screens are drawn with gint into its VRAM and then presented
 * either as-is (portrait) or turned a quarter turn (landscape). Gameplay
 * bypasses VRAM: the scaled picture is produced in strips and sent to the
 * panel by DMA, the way the OS itself refreshes this screen (its LCD routine
 * runs DMAC channel 0 in 32-byte units to the LCD data port; gint's R61523
 * driver just does not use it). Strips are built in the two on-chip RAM banks
 * and double-buffered, so each strip is computed while the previous one is
 * on the bus, and the last strip of a frame is left in flight under the next
 * frame's emulation - the same scheme as the fx-CG presenter. The touch keys
 * around the picture are painted once and then only repainted when their
 * pressed state changes.
 *
 * Touch. gint's key scanner samples the resistive panel at 128 Hz. We read
 * the latest sample each frame, map it to the touch keys of the current
 * layout (classpad_layout.h) and merge the result into the GBA buttons.
 */

#include "fxcg100_platform.h"
#include "classpad_layout.h"
#include "classpad_clock.h"
#include "classpad_font.h"
#include "classpad_gfx.h"
#include "classpad_scale.h"
#include "classpad_theme.h"
#include "classpad_sync.h"
#ifndef CGBA_HOST_TEST
#include "classpad_touch_guard.h"
#include "classpad_touch_poll.h"
#endif

#ifndef CGBA_HOST_TEST
#include <gint/clock.h>
#include <gint/rtc.h>
#include <gint/timer.h>
#endif
#include <gint/cpu.h>
#include <gint/display.h>
#include <gint/dma.h>
#include <gint/drivers/r61523.h>
#include <gint/drivers/touch.h>
#include <gint/keyboard.h>
#include <gint/keycodes.h>

#include <string.h>

#define LCD_DATA   ((volatile uint16_t *)0xb4000000u)
#define LCD_PRDR   ((volatile uint8_t *)0xa405013cu)   /* bit 4 = LCD RS */
#define LCD_REG_WRITE 0x2c

/* Width of the gint-drawn canvas (menus, status). In landscape the canvas is
 * the top 320x320 of VRAM, shown centred on the 528-wide screen. */
#define CANVAS_W CP_PANEL_W
#define CANVAS_LANDSCAPE_H CP_PANEL_W
#define CANVAS_LANDSCAPE_X ((CP_PANEL_H - CANVAS_W) / 2)

static uint32_t cp_view = CP_VIEW_PORTRAIT_43;
static uint32_t cp_rotation = CP_ROTATION_KEYS_RIGHT;
static uint32_t cp_filter;
static uint32_t cp_touch_keys = 1;
/* Clear opens the menu, unless the key map has given it another job. */
static uint32_t cp_clear_is_menu = 1;
/* Nonzero once gpSP reads the touch controller itself
 * (classpad_touch_poll.h); zero while gint's driver, under the guard, still
 * does. */
static int touch_polled;
/* Touch-key colours, from the interface's colour scheme (classpad_theme.h). */
static cp_key_colors key_colors;
static uint32_t cp_theme_index = ~0u;
/* Gameplay strips go out by DMA (0) or by CPU writes (1). Measured on an
 * fx-CP400 (docs/classpad-port.md): CPU writes are faster pixel for pixel,
 * DMA leaves the CPU free but slows memory-bound work while it runs, so
 * which one gives more frames depends on the game. */
static uint32_t cp_cpu_transfer;

/* Nonzero while the panel shows the gameplay layout (black background, touch
 * keys painted). Any canvas present clears it; the next blit rebuilds it. */
static int cp_game_screen;
/* GBA button bits held on the touch keys this frame. */
static uint32_t cp_touch_mask;
/* Icons of the strip under the LCD being touched (1 << CP_ICON_*). */
static uint32_t cp_strip;
/* First touch point on the LCD, in panel coordinates. */
static int cp_touch_down, cp_touch_px, cp_touch_py;

/* ---- panel access --------------------------------------------------------- */

/* One strip is at most 4096 pixels: an 8 KiB on-chip RAM bank. DMA moves
 * 32-byte units, so every strip is a multiple of 16 pixels. */
#define STRIP_PIXELS 4096

static int lcd_dma_pending;
static unsigned strip_bank;

static void wait_lcd_dma(void)
{
	if(!lcd_dma_pending)
		return;
	dma_transfer_wait(0);
	lcd_dma_pending = 0;
}

#ifdef CGBA_HOST_TEST
/* Host build (tests/classpad_render_test.c): the bus is a simulated R61523. */
void cp_host_lcd_begin(int c0, int c1, int r0, int r1);
void cp_host_lcd_write(uint16_t pixel);
static uint16_t host_strips[2][STRIP_PIXELS] __attribute__((aligned(32)));
#define STRIP_BUF0 ((uintptr_t)host_strips[0])
#define STRIP_BUF1 ((uintptr_t)host_strips[1])
static int cp_host_refresh_bottom_up = 1;

static inline int lcd_refresh_bottom_up(void)
{
	return cp_host_refresh_bottom_up;
}

static void lcd_begin(int c0, int c1, int r0, int r1)
{
	wait_lcd_dma();
	cp_host_lcd_begin(c0, c1, r0, r1);
}

static inline void lcd_write(uint16_t pixel)
{
	cp_host_lcd_write(pixel);
}
#else
/* X and Y RAM, the same banks the fx-CG presenter uses. */
#define STRIP_BUF0 ((uintptr_t)0xe5007000u)
#define STRIP_BUF1 ((uintptr_t)0xe5017000u)

static uint32_t lcd_cpu_mhz_cached;

/* Current CPU clock in MHz, rounded up and floored at stock speed, so busy
 * waits for LCD bus timing never under-wait even under overclock. */
static uint32_t lcd_cpu_mhz(void)
{
	if(!lcd_cpu_mhz_cached) {
		cp_clock clock;
		uint32_t hz;

		fxcg100_clock_read(&clock);
		hz = cp_clock_hz(&clock, CP_CLK_CPU);
		lcd_cpu_mhz_cached = (hz + 999999u) / 1000000u;
		if(lcd_cpu_mhz_cached < 118u)
			lcd_cpu_mhz_cached = 118u;
	}
	return lcd_cpu_mhz_cached;
}

/* Busy-wait at least `ns` nanoseconds. Each loop iteration runs at least a
 * `dt; bf/s; nop` (2+ cycles on SH-4A), so ceil(ns * MHz / 2000) iterations
 * is a safe lower bound at every clock speed. */
static void lcd_delay_ns(uint32_t ns)
{
	uint32_t loops = (ns * lcd_cpu_mhz() + 1999u) / 2000u;

	while(loops--)
		__asm__ volatile("nop");
}

static inline void lcd_synco(void)
{
	__asm__ volatile("synco");
}

/* Select a controller register; RS returns high so data can follow. */
static void lcd_select(unsigned reg)
{
	*LCD_PRDR &= (uint8_t)~0x10;
	lcd_synco();
	*LCD_DATA = (uint16_t)reg;
	lcd_synco();
	*LCD_PRDR |= 0x10;
	lcd_synco();
}

static inline void lcd_param(unsigned value)
{
	*LCD_DATA = (uint16_t)value;
	lcd_synco();
}

/* Read one word from the R61523 data port. The R61523 DBI Type B read cycle
 * needs trc >= 450 ns and trdh >= 250 ns (datasheet Table 119, p.248), far
 * slower than writes (twc >= 60 ns), and overclocking raises Bphi without
 * raising CS5a waits. Used only on R61523 paths so 0x16/0x52 stays untouched. */
static uint16_t r61523_read_word(void)
{
	uint16_t value;

	lcd_synco();
	value = *LCD_DATA;
	lcd_synco();
	lcd_delay_ns(450);
	return value;
}

/* The fx-CP400 shipped with two panel families that are told apart by ID
 * register 0xda. The original one is the R61523, whose 360-column memory
 * shows the picture from column 40. Later units answer 0x16 or 0x52 and
 * start at column 0. This mirrors the OS (LCD_SetDrawingBounds, 0x80038a10,
 * OS 02.01.7003): add 40 to X unless the ID is 0x16 or 0x52. Both families
 * take the same window (0x2a/0x2b) and write (0x2c) commands.
 *
 * gint's r61523_win_set() is not used: its ID test is always true, so it
 * adds the 40 on every panel. */
static int lcd_first_column(void)
{
	static int first_column = -1;

	if(first_column < 0) {
		unsigned id;

		wait_lcd_dma();
		lcd_select(0xda);
		id = *LCD_DATA;          /* the OS discards the first read too */
		id = *LCD_DATA & 0xffu;
		first_column = (id == 0x16 || id == 0x52) ? 0 : 40;
	}
	return first_column;
}

static inline int lcd_is_r61523(void)
{
	return lcd_first_column() != 0;
}

/* Whether logical rows 0..527 are refreshed bottom-to-top (527 -> 0).
 * On 0x16/0x52 the OS sets 0xc0 and bottom-up refresh is already verified on
 * hardware, so 0x0b is never probed there. On R61523, read MIPI DCS
 * get_address_mode (0x0b, User Command: needs no MCAP unlock) twice. */
static int lcd_refresh_bottom_up(void)
{
	static int bottom_up = -1;

	if(bottom_up < 0) {
		if(!lcd_is_r61523()) {
			bottom_up = 1;
		} else {
			uint8_t m1, m2;

			wait_lcd_dma();
			lcd_select(0x0b);
			(void)r61523_read_word();
			m1 = (uint8_t)(r61523_read_word() & 0xffu);
			lcd_select(0x0b);
			(void)r61523_read_word();
			m2 = (uint8_t)(r61523_read_word() & 0xffu);
			bottom_up = cp_r61523_refresh_bottom_up(m1, m1 == m2);
		}
	}
	return bottom_up;
}

/* R61523 Manufacturer Command Access Protect (B0h, datasheet p.135).
 * Default MCAP = 3'b011 treats B1h..BFh (including B9h backlight control)
 * as NOP. Unlock B1h..BFh only (0x02) and restore the previous value. */
static uint8_t r61523_mcap_unlock(void)
{
	uint8_t saved;

	lcd_select(0xb0);
	(void)r61523_read_word();
	saved = cp_r61523_mcap_restore_value(
		(uint8_t)(r61523_read_word() & 0xffu));
	if(saved != 0x02u) {
		lcd_select(0xb0);
		lcd_param(0x02u);
	}
	return saved;
}

static void r61523_mcap_restore(uint8_t saved)
{
	if(saved != 0x02u) {
		lcd_select(0xb0);
		lcd_param(saved);
	}
}

/* Open a write window (bounds inclusive, panel coordinates) and leave the
 * controller ready to receive its pixels in row-major order. Any strip still
 * on the bus is allowed to finish first, so every panel access may simply
 * start with this. */
static void lcd_begin(int c0, int c1, int r0, int r1)
{
	int first;

	wait_lcd_dma();
	first = lcd_first_column();
	c0 += first;
	c1 += first;

	lcd_select(0x2a);
	lcd_param((c0 >> 8) & 3);
	lcd_param(c0 & 0xff);
	lcd_param((c1 >> 8) & 3);
	lcd_param(c1 & 0xff);
	lcd_select(0x2b);
	lcd_param((r0 >> 8) & 3);
	lcd_param(r0 & 0xff);
	lcd_param((r1 >> 8) & 3);
	lcd_param(r1 & 0xff);
	lcd_select(LCD_REG_WRITE);
}

static inline void lcd_write(uint16_t pixel)
{
	*LCD_DATA = pixel;
}
#endif

static inline void lcd_put_line(const uint16_t *line, unsigned n)
{
	unsigned i;

	for(i = 0; i < n; i++)
		lcd_write(line[i]);
}

/* The bank to build the next strip in. The other bank may still be on the
 * bus; this one is free. */
static inline uint16_t *strip_buffer(void)
{
	return (uint16_t *)(strip_bank ? STRIP_BUF1 : STRIP_BUF0);
}

/* Order strips to follow the panel's refresh direction (from 0x0b
 * get_address_mode), so a frame written the way it is shown can be kept
 * clear of the refresh (classpad_sync.h). */
static inline int strip_order(int index, int count, int in_order)
{
	return in_order ? index : count - 1 - index;
}

/* Refresh sync, further down. A frame is either the game picture or the
 * interface; how long each takes to send is kept apart. */
enum { SYNC_GAME = 0, SYNC_CANVAS, SYNC_KINDS };
#ifndef CGBA_HOST_TEST
static void sync_begin(int top, int rows, int kind);
static void sync_strip(int r0, int rows);
static void sync_end(void);
#endif

/* Send the strip just built to a panel window and return while it transfers.
 * Falls back to CPU writes if the DMA channel cannot be had. */
static void strip_send(int c0, int r0, int width, int rows)
{
	uint16_t *strip = strip_buffer();
	unsigned pixels = (unsigned)(width * rows);

#ifndef CGBA_HOST_TEST
	sync_strip(r0, rows);
#endif
	lcd_begin(c0, c0 + width - 1, r0, r0 + rows - 1);
	if(!cp_cpu_transfer && (pixels & 15u) == 0 &&
			dma_transfer_async(0, DMA_32B, pixels / 16u,
			strip, DMA_INC, (void *)LCD_DATA, DMA_FIXED, GINT_CALL_NULL))
		lcd_dma_pending = 1;
	else
		lcd_put_line(strip, pixels);
	strip_bank ^= 1u;
}

static void lcd_fill_panel(cp_rect p, uint16_t color)
{
	int n = p.w * p.h;

	if(n <= 0)
		return;
	lcd_begin(p.x, p.x + p.w - 1, p.y, p.y + p.h - 1);
	while(n--)
		lcd_write(color);
}

#ifndef CGBA_HOST_TEST
/* ---- clock ---------------------------------------------------------------- */

_Static_assert(sizeof(cp_clock) == sizeof(struct cpg_overclock_setting),
	"cp_clock must mirror gint's struct cpg_overclock_setting");

void fxcg100_clock_read(struct cp_clock *clock)
{
	cpg_get_overclock_setting((struct cpg_overclock_setting *)clock);
}

void fxcg100_clock_write(const struct cp_clock *clock)
{
	wait_lcd_dma();
	cpg_set_overclock_setting((const struct cpg_overclock_setting *)clock);
	lcd_cpu_mhz_cached = 0;
	/* gint has rescaled its timers. The polled touch read takes the clock
	 * of the moment by itself; gint's driver has to be told. */
	if(!touch_polled)
		cp_touch_guard_set_clock((uint32_t)clock_freq()->Pphi_f);
}

/* 128 Hz ticks for the interface and the frame pacing.
 *
 * The RTC would be the natural source (it does not change with the clock
 * settings), but nothing guarantees it is running on this calculator, and a
 * count that stands still stops every animation and every timeout. So a
 * timer counts ticks as well; the RTC is only used once it has been seen to
 * move, and is offset so the value does not jump when it takes over. */
static volatile uint32_t timer_ticks;
static int tick_timer = -1;

uint32_t fxcg100_touch_recoveries(void)
{
	return cp_touch_guard_events() + cp_touch_poll_failures;
}

int fxcg100_touch_is_polled(void)
{
	return touch_polled;
}

static int tick_callback(void)
{
	timer_ticks++;
	if(!touch_polled)
		cp_touch_guard_tick();
	return TIMER_CONTINUE;
}

static void ticks_start(void)
{
	if(tick_timer >= 0)
		return;
	/* Read the touch controller ourselves if gint's own reads can be
	 * switched off; otherwise leave them on, under the guard. */
	{
		/* Where the program starts in RAM (the linker script's _brom). */
		extern uint32_t cp_program_start[] __asm__("_brom");

		touch_polled = cp_touch_poll_begin(cp_program_start,
			(uint32_t *)(uintptr_t)&cp_touch_poll_pen_up);
	}
	if(!touch_polled) {
		cp_touch_guard_install();
		/* The clock may already have been changed by another tool. */
		cp_touch_guard_set_clock((uint32_t)clock_freq()->Pphi_f);
	}
	/* A TMU on purpose: its interrupt outranks gint's key scan, which the
	 * touch guard relies on (classpad_touch_guard.h). */
	tick_timer = timer_configure(TIMER_TMU, 1000000 / 128,
		GINT_CALL(tick_callback));
	if(tick_timer >= 0)
		timer_start(tick_timer);
}

static void ticks_stop(void)
{
	if(tick_timer >= 0)
		timer_stop(tick_timer);
	tick_timer = -1;
}

uint32_t fxcg100_ticks(void)
{
	enum { UNKNOWN = 0, WATCHING, LIVE, DEAD };
	static int rtc_state;
	static uint32_t rtc_first, timer_first, offset;
	uint32_t count = timer_ticks, rtc;

	if(tick_timer < 0)
		return rtc_ticks();           /* no timer to be had: all there is */
	if(rtc_state == LIVE)
		return rtc_ticks() + offset;
	if(rtc_state == DEAD)
		return count;

	rtc = rtc_ticks();
	if(rtc_state == UNKNOWN) {
		rtc_first = rtc;
		timer_first = count;
		rtc_state = WATCHING;
	} else if(rtc != rtc_first) {
		offset = count - rtc;
		rtc_state = LIVE;
	} else if(count - timer_first >= 16) {
		rtc_state = DEAD;             /* an eighth of a second, no movement */
	}
	return count;
}

/* ---- clock test -----------------------------------------------------------
 * What "Test" on the clock page runs while the calculator is on the setting
 * being tried. It takes a few seconds, on purpose: a setting that is only
 * just too fast fails rarely.
 *
 *   RAM, uncached   64 KiB written and read back through the P2 mirror, so
 *                   that every access goes to the SDRAM and none is answered
 *                   by the cache: four patterns, 48 passes.
 *   RAM, cached     the same buffer through the cache, in a scattered order,
 *                   which is how a program really uses memory (line fills
 *                   and write-backs at arbitrary addresses).
 *   Signature       a number that must come out the same on the stock clock
 *                   and on the one under test: a checksum of 2 MiB of the OS
 *                   in flash read uncached (the CS0 wait), the emulator code
 *                   in RAM, and the result of a long calculation (the core
 *                   itself). */

#define STRESS_WORDS 16384u
#define STRESS_RAM_PASSES 48
#define STRESS_CACHED_PASSES 24
#define STRESS_FLASH_PASSES 1
#define STRESS_CPU_ROUNDS 1500000u

static uint32_t stress_buffer[STRESS_WORDS] __attribute__((aligned(32)));

/* Drop the cache's copy of the buffer, so nothing written through it earlier
 * can land on top of what is written through the mirror. */
static void stress_purge(void)
{
	uintptr_t line = (uintptr_t)stress_buffer;
	uintptr_t end = line + sizeof(stress_buffer);

	for(; line < end; line += 32)
		__asm__ volatile("ocbp @%0" : : "r"(line));
}

/* Zero if the RAM held everything; 1 if the uncached part failed, 2 if the
 * cached part did. */
int fxcg100_stress_ram(void)
{
	static const uint32_t seeds[4] = {
		0x00000000u, 0xffffffffu, 0xa5a5a5a5u, 0x3c96c369u
	};
	volatile uint32_t *direct = (volatile uint32_t *)
		((uintptr_t)stress_buffer | 0x20000000u);
	uint32_t i, x, sum, expect;
	int pass, s;

	stress_purge();
	for(pass = 0; pass < STRESS_RAM_PASSES; pass++) {
		for(s = 0; s < 4; s++) {
			uint32_t seed = seeds[s] ^ ((uint32_t)pass * 0x01000193u);

			for(i = 0; i < STRESS_WORDS; i++)
				direct[i] = seed ^ (i * 0x9e3779b9u);
			for(i = 0; i < STRESS_WORDS; i++)
				if(direct[i] != (seed ^ (i * 0x9e3779b9u)))
					return 1;
		}
	}

	/* Scattered: 5 is coprime with the size, so i * 5 + 3 visits every
	 * word once, a cache line apart from the one before. */
	for(pass = 0; pass < STRESS_CACHED_PASSES; pass++) {
		x = (uint32_t)pass * 0x9e3779b9u + 1u;
		sum = expect = 0;
		for(i = 0; i < STRESS_WORDS; i++) {
			uint32_t at = (i * 5u + 3u) & (STRESS_WORDS - 1u);

			x = x * 1664525u + 1013904223u;
			stress_buffer[at] = x;
			expect += x ^ at;
		}
		for(i = 0; i < STRESS_WORDS; i++) {
			uint32_t at = (i * 13u + 7u) & (STRESS_WORDS - 1u);

			sum += stress_buffer[at] ^ at;
		}
		if(sum != expect)
			return 2;
	}
	stress_purge();
	return 0;
}

/* How fast the processor really runs: how many rounds of a register-only
 * loop fit into a quarter of a second, timed by an ETMU. Those timers count
 * the 32768 Hz crystal directly (gint's key scan runs on one), so they keep
 * their rate whatever the clock settings are - unlike the TMUs, which count
 * the peripheral clock, and without depending on the RTC's counter, which
 * does not run on every calculator. Zero if no timer could be had. The
 * number means nothing by itself; the clock page compares it with the one
 * it got on the stock setting. */
static volatile int measure_over;

static int measure_callback(void)
{
	measure_over = 1;
	return TIMER_STOP;
}

uint32_t fxcg100_measure_speed(void)
{
	/* Where the loop's result goes, so the compiler keeps the loop. */
	static volatile uint32_t sink;
	uint32_t rounds = 0, x = 1;
	int timer;

	measure_over = 0;
	timer = timer_configure(TIMER_ETMU, 250000, GINT_CALL(measure_callback));
	if(timer < 0) {
		/* No such timer free: the RTC's 128 Hz counter, if it counts. */
		uint32_t start = rtc_ticks(), guard;

		for(guard = 0; rtc_ticks() == start; guard++)
			if(guard > 100000u)
				return 0;
		start = rtc_ticks();
		do {
			uint32_t i;

			for(i = 0; i < 1000u; i++)
				x = x * 1664525u + 1013904223u;
			sink = x;
			rounds++;
		} while(rtc_ticks() - start < 32u && rounds < 8000000u);
		return rounds;
	}
	timer_start(timer);
	while(!measure_over && rounds < 8000000u) {
		uint32_t i;

		for(i = 0; i < 1000u; i++)
			x = x * 1664525u + 1013904223u;
		sink = x;
		rounds++;
	}
	if(!measure_over) {
		timer_stop(timer);
		return 0;
	}
	return rounds;
}

uint32_t fxcg100_core_checksum(void);

uint32_t fxcg100_stress_signature(void)
{
	const volatile uint32_t *flash = (const volatile uint32_t *)0xa0020000u;
	uint32_t sum = 0, x = 0x2545f491u, y = 0x9e3779b9u, i;
	int pass;

	/* Flash, uncached: every word is a bus read with the CS0 wait. */
	for(pass = 0; pass < STRESS_FLASH_PASSES; pass++)
		for(i = 0; i < (2u << 20) / 4u; i++)
			sum = (sum << 1 | sum >> 31) ^ flash[i];
	/* The core: multiplies, shifts and a division in a long chain, where
	 * one wrong result changes everything after it. */
	for(i = 0; i < STRESS_CPU_ROUNDS; i++) {
		x = x * 1664525u + 1013904223u;
		y ^= x >> 7;
		y = (y << 5 | y >> 27) + (x % ((y & 255u) + 3u));
	}
	return sum ^ y ^ fxcg100_core_checksum();
}

uint32_t fxcg100_core_checksum(void)
{
	extern uint8_t cgba_core_start[], cgba_core_end[];
	const uint32_t *p = (const uint32_t *)(const void *)cgba_core_start;
	const uint32_t *end = (const uint32_t *)(const void *)cgba_core_end;
	uint32_t sum = 0;

	for(; p < end; p++)
		sum = (sum << 1 | sum >> 31) ^ *p;
	return sum;
}

/* Crash report: show gint's VRAM without relying on interrupts or on the
 * window being in any particular state. A strip that was on the bus when the
 * exception hit is abandoned by disabling DMA channel 0 (CHCR0.DE). gint's
 * own full-frame push is not used: on the 0x16/0x52 panels its window
 * request is out of range, which leaves the last gameplay strip window in
 * effect. */
void cgba_classpad_panic_present(void)
{
	volatile uint32_t *chcr0 = (volatile uint32_t *)0xfe00802cu;

	*chcr0 &= ~1u;
	lcd_dma_pending = 0;
	lcd_begin(0, CP_PANEL_W - 1, 0, CP_PANEL_H - 1);
	lcd_put_line(gint_vram, CP_PANEL_W * CP_PANEL_H);
}
#endif

/* ---- canvas (menus, status screens) -------------------------------------- */

/* Landscape: show VRAM rows 0..319 turned a quarter turn and centred. The
 * columns left and right of the canvas repeat its edge pixels, so header bars
 * and backgrounds run the full width of the screen. */
static void present_canvas_landscape(void)
{
	int keys_left = cp_rotation == CP_ROTATION_KEYS_LEFT;
	int r;

	lcd_begin(0, CP_PANEL_W - 1, 0, CP_PANEL_H - 1);
	for(r = 0; r < CP_PANEL_H; r++) {
		int ux = keys_left ? CP_PANEL_H - 1 - r : r;
		int cx = ux - CANVAS_LANDSCAPE_X;
		const uint16_t *col;
		int c;

		if(cx < 0)
			cx = 0;
		if(cx > CANVAS_W - 1)
			cx = CANVAS_W - 1;
		col = gint_vram + cx;
		if(keys_left) {
			for(c = 0; c < CANVAS_LANDSCAPE_H; c++, col += CANVAS_W)
				lcd_write(*col);
		} else {
			col += (CANVAS_LANDSCAPE_H - 1) * CANVAS_W;
			for(c = 0; c < CANVAS_LANDSCAPE_H; c++, col -= CANVAS_W)
				lcd_write(*col);
		}
	}
}

void fxcg100_lcd_init(void)
{
	cp_game_screen = 0;
	cp_touch_mask = 0;
	if(cp_theme_index >= CP_THEME_COUNT)
		fxcg100_lcd_set_theme(0);
#ifndef CGBA_HOST_TEST
	lcd_cpu_mhz_cached = 0;
	ticks_start();
#endif
}

/* ---- backlight ------------------------------------------------------------
 * Both panel families drive the backlight from the display controller, and
 * the OS sets it there (OS 02.01.7003, 0x80038550 and 0x8003861e):
 *
 *   0x16/0x52   brightness is DCS 0x51, one byte; the PWM rate is the second
 *               and third parameter of 0xc9, with 0x04 before and 0x01 after
 *   R61523      both are in 0xb9 (backlight control 2, behind MCAP 0xb0):
 *               the second parameter is the brightness (BDCV), the third the
 *               PWM divider (PWMDIV)
 *
 * The system's 14 brightness steps write 11..103 of 255, so its brightest
 * is 40 % of what the backlight can do. The 9 non-default entries in the OS
 * PWM table (0x8063c184) match the R61523 PWMDIV frequencies (Table 54,
 * p.154) via 16 MHz / N from 0.43 kHz (0x9159 / 0x7f) up to 33.3 kHz
 * (0x01e0 / 0x00), so rate 1..9 rises monotonically on both families. On
 * R61523, PWMDIV = 0x00 (33.3 kHz, rate 9) caps PWM resolution to 210 steps
 * (Table 54 Note 2), so brightness levels 14..16 saturate at full duty; rate
 * 8 (27.4 kHz, 0x01) keeps all 256 steps. */
#ifndef CGBA_HOST_TEST
static const uint8_t backlight_pwm_new[10][3] = {
	{ 0x14, 0x29, 0xa0 },   /* 0: what the OS sets when it starts the panel */
	{ 0x04, 0x91, 0x59 },   /* 1: 0.43 kHz */
	{ 0x04, 0x48, 0xad },   /* 2: 0.86 kHz */
	{ 0x04, 0x24, 0x56 },   /* 3: 1.72 kHz */
	{ 0x04, 0x12, 0x39 },   /* 4: 3.43 kHz */
	{ 0x04, 0x09, 0x1c },   /* 5: 6.86 kHz */
	{ 0x04, 0x04, 0x90 },   /* 6: 13.7 kHz */
	{ 0x04, 0x03, 0x6a },   /* 7: 18.3 kHz */
	{ 0x04, 0x02, 0x48 },   /* 8: 27.4 kHz */
	{ 0x04, 0x01, 0xe0 },   /* 9: 33.3 kHz */
};
/* R61523 PWMDIV for rate 1..9 (0.43 kHz .. 33.3 kHz); 0xff (0.21 kHz) is
 * omitted. */
static const uint8_t backlight_pwm_old[9] = {
	0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x02, 0x01, 0x00,
};
static int backlight_saved;
static int backlight_b9_ok;              /* R61523: 0xb9 read passed checks */
static uint8_t backlight_system;         /* brightness found at the start */
static uint8_t backlight_b9[4];          /* R61523: 0xb9 as found */
static uint32_t backlight_level, backlight_rate;

static void backlight_read_b9(uint8_t out[4])
{
	uint8_t mcap = r61523_mcap_unlock();
	int i;

	lcd_select(0xb9);
	(void)r61523_read_word();
	for(i = 0; i < 4; i++)
		out[i] = (uint8_t)(r61523_read_word() & 0xffu);
	r61523_mcap_restore(mcap);
}

static void backlight_write(uint32_t level, uint32_t rate)
{
	/* Level 1..16 is 15, 31, .. 255; 0 is what the system had. */
	unsigned value = level ? level * 16u - 1u : backlight_system;

	wait_lcd_dma();
	if(!lcd_is_r61523()) {
		lcd_select(0x51);
		lcd_param(value);
		lcd_select(0xc9);
		lcd_param(backlight_pwm_new[rate][0]);
		lcd_param(backlight_pwm_new[rate][1]);
		lcd_param(backlight_pwm_new[rate][2]);
		lcd_param(0x01);
	} else if(backlight_b9_ok) {
		uint8_t mcap = r61523_mcap_unlock();

		lcd_select(0xb9);
		lcd_param(backlight_b9[0]);
		lcd_param(value);
		lcd_param(rate ? backlight_pwm_old[rate - 1u] : backlight_b9[2]);
		lcd_param(backlight_b9[3]);
		r61523_mcap_restore(mcap);
	}
}
#endif

void fxcg100_lcd_set_backlight(uint32_t level, uint32_t pwm)
{
#ifndef CGBA_HOST_TEST
	uint32_t min_level;

	if(level > 16u)
		level = 0;
	if(pwm > 9u)
		pwm = 0;
	min_level = cp_backlight_min_level(pwm);
	if(level < min_level)
		level = min_level;
	if(!backlight_saved) {
		if(level == 0 && pwm == 0)
			return;                  /* nothing asked for: touch nothing */
		wait_lcd_dma();
		if(!lcd_is_r61523()) {
			lcd_select(0x52);        /* read display brightness */
			(void)*LCD_DATA;
			backlight_system = (uint8_t)*LCD_DATA;
		} else {
			backlight_read_b9(backlight_b9);
			backlight_b9_ok = cp_r61523_b9_valid(backlight_b9);
			backlight_system = backlight_b9[1];
		}
		/* The system only ever writes 11..103. Anything else was not
		 * read properly: go back to its middle step when leaving. */
		if(backlight_system < 8u || backlight_system > 110u)
			backlight_system = 52u;
		backlight_saved = 1;
	} else if(level == backlight_level && pwm == backlight_rate) {
		return;
	}
	backlight_level = level;
	backlight_rate = pwm;
	backlight_write(level, pwm);
#else
	(void)level;
	(void)pwm;
#endif
}

void fxcg100_lcd_shutdown(void)
{
	wait_lcd_dma();
#ifndef CGBA_HOST_TEST
	/* Give the system its backlight back. */
	if(backlight_saved && (backlight_level || backlight_rate)) {
		backlight_level = backlight_rate = 0;
		backlight_write(0, 0);
	}
#endif
	cp_game_screen = 0;
#ifndef CGBA_HOST_TEST
	ticks_stop();
#endif
}

void fxcg100_lcd_clear(uint16_t color)
{
	wait_lcd_dma();
	dclear((int)color);
}

void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
	uint16_t color)
{
	if(x >= DWIDTH || y >= DHEIGHT || w == 0 || h == 0)
		return;
	if(x + w > DWIDTH)
		w = DWIDTH - x;
	if(y + h > DHEIGHT)
		h = DHEIGHT - y;
	drect((int)x, (int)y, (int)(x + w - 1), (int)(y + h - 1), (int)color);
}

void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
	uint16_t fg, uint16_t bg)
{
	dtext_opt((int)x, (int)y, (int)fg, (int)bg, DTEXT_LEFT, DTEXT_TOP,
		text ? text : "");
}

void fxcg100_lcd_update(void)
{
	if(cp_view_is_landscape(cp_view)) {
		present_canvas_landscape();
	} else if(cp_view_is_flipped(cp_view, cp_rotation)) {
		/* gint drew the canvas upright: a half turn is the same pixels
		 * from the last to the first. */
		int i;

		lcd_begin(0, CP_PANEL_W - 1, 0, CP_PANEL_H - 1);
		for(i = CP_PANEL_W * CP_PANEL_H - 1; i >= 0; i--)
			lcd_write(gint_vram[i]);
	} else {
		lcd_begin(0, CP_PANEL_W - 1, 0, CP_PANEL_H - 1);
		lcd_put_line(gint_vram, CP_PANEL_W * CP_PANEL_H);
	}
	cp_game_screen = 0;
	cp_touch_mask = 0;
}

/* Show gint's VRAM as it is. The interface draws it in panel order for the
 * current orientation (classpad_gfx.h), so no turning is needed here. */
void fxcg100_lcd_present_frame(void)
{
	/* In bands following the panel's refresh direction, like the game
	 * picture (see strip_order). */
	enum { BAND = 16 };
	int bands = CP_PANEL_H / BAND;
	int in_order = !lcd_refresh_bottom_up();
	int i;

#ifndef CGBA_HOST_TEST
	sync_begin(0, CP_PANEL_H, SYNC_CANVAS);
#endif
	for(i = 0; i < bands; i++) {
		int r = strip_order(i, bands, in_order) * BAND;
#ifndef CGBA_HOST_TEST
		sync_strip(r, BAND);
#endif
		lcd_begin(0, CP_PANEL_W - 1, r, r + BAND - 1);
		lcd_put_line(gint_vram + r * CP_PANEL_W, CP_PANEL_W * BAND);
	}
#ifndef CGBA_HOST_TEST
	sync_end();
#endif
	cp_game_screen = 0;
	cp_touch_mask = 0;
}

void fxcg100_lcd_status(const char *text)
{
	uint16_t bg = C_RGB(0, 10, 22);

	drect(0, 0, DWIDTH - 1, 13, bg);
	dtext_opt(4, 2, C_WHITE, bg, DTEXT_LEFT, DTEXT_TOP, text ? text : "");
	fxcg100_lcd_update();
}

void fxcg100_lcd_note_os_activity(void)
{
}

void fxcg100_lcd_set_scale(uint32_t mode)
{
	if(mode >= CP_VIEW_COUNT)
		mode = CP_VIEW_PORTRAIT_43;
	if(mode != cp_view)
		cp_game_screen = 0;
	cp_view = mode;
}

void fxcg100_lcd_set_filter(uint32_t filter)
{
	cp_filter = (filter <= CGBA_SCALE_FILTER_CRISP) ? filter : 0;
}

void fxcg100_lcd_set_rotation(uint32_t rotation)
{
	if(rotation >= CP_ROTATION_COUNT)
		rotation = CP_ROTATION_KEYS_RIGHT;
	if(rotation != cp_rotation)
		cp_game_screen = 0;
	cp_rotation = rotation;
}

void fxcg100_lcd_set_transfer(uint32_t cpu_writes)
{
	cp_cpu_transfer = cpu_writes ? 1 : 0;
}

void fxcg100_lcd_set_menu_key(uint32_t clear_opens_menu)
{
	cp_clear_is_menu = clear_opens_menu ? 1 : 0;
}

void fxcg100_lcd_set_touch_keys(uint32_t enabled)
{
	enabled = enabled ? 1 : 0;
	if(enabled != cp_touch_keys)
		cp_game_screen = 0;
	cp_touch_keys = enabled;
}

/* ---- touch keys ----------------------------------------------------------- */

#define PAD_BACKGROUND 0x0000

#define KEY_FACE      (key_colors.face)
#define KEY_FACE_LIT  (key_colors.face_lit)
#define KEY_INK       (key_colors.ink)
#define KEY_INK_LIT   (key_colors.ink_lit)
#define KEY_WELL      (key_colors.well)
#define KEY_CAPTION   (key_colors.caption)

void fxcg100_lcd_set_theme(uint32_t theme)
{
	if(theme >= CP_THEME_COUNT)
		theme = 0;
	if(theme == cp_theme_index)
		return;
	cp_theme_index = theme;
	key_colors = cp_theme_key_colors(cp_theme_get(theme));
	cp_game_screen = 0;
}

/* One key is rendered into this buffer in panel orientation and pushed with a
 * single window. */
static uint16_t key_buffer[CP_MAX_KEY_PIXELS];

/* A key body: one flat tone, a step brighter while it is held. */
static void key_body(const cp_surface *k, int x, int y, int w, int h, int r,
	int lit)
{
	cp_round_rect(k, x, y, w, h, r, lit ? KEY_FACE_LIT : KEY_FACE);
}

/* Solid triangle of the given height centred on (cx, cy), pointing in a GBA
 * direction. */
static void key_arrow(const cp_surface *k, int cx, int cy, int size,
	uint32_t direction, uint16_t ink)
{
	int h = size / 2;

	switch(direction) {
	case CP_GBA_UP:
		cp_triangle(k, cx, cy - h, cx - size, cy + h, cx + size, cy + h, ink);
		break;
	case CP_GBA_DOWN:
		cp_triangle(k, cx, cy + h, cx - size, cy - h, cx + size, cy - h, ink);
		break;
	case CP_GBA_LEFT:
		cp_triangle(k, cx - h, cy, cx + h, cy - size, cx + h, cy + size, ink);
		break;
	default:
		cp_triangle(k, cx + h, cy, cx - h, cy - size, cx - h, cy + size, ink);
		break;
	}
}

/* The D-pad: a cross in one piece on a round well. `lit` holds the direction
 * bits whose arms are pressed. */
static void key_dpad(const cp_surface *k, unsigned lit)
{
	static const struct { uint32_t bit; int8_t dx, dy; } arms[4] = {
		{ CP_GBA_UP, 0, -1 }, { CP_GBA_DOWN, 0, 1 },
		{ CP_GBA_LEFT, -1, 0 }, { CP_GBA_RIGHT, 1, 0 },
	};
	int size = k->w;
	int arm = size / 3;                  /* arm width: 48 of 144 */
	int off = (size - arm) / 2;
	int c = size / 2, i;

	cp_round_rect(k, 0, 0, size, size, size / 2, KEY_WELL);
	cp_round_rect(k, off, 0, arm, size, 10, KEY_FACE);
	cp_round_rect(k, 0, off, size, arm, 10, KEY_FACE);

	for(i = 0; i < 4; i++) {
		int on = (lit & arms[i].bit) != 0;
		/* The outer part of this arm, from the hub outwards. */
		int x0 = arms[i].dx < 0 ? 0 : arms[i].dx > 0 ? off + arm : off;
		int y0 = arms[i].dy < 0 ? 0 : arms[i].dy > 0 ? off + arm : off;
		int w = arms[i].dx ? off : arm;
		int h = arms[i].dy ? off : arm;

		if(on)
			cp_round_rect(k, x0, y0, w, h, 10, KEY_FACE_LIT);
		key_arrow(k, c + arms[i].dx * (c - 20), c + arms[i].dy * (c - 20), 8,
			arms[i].bit, on ? KEY_INK_LIT : KEY_INK);
	}
	cp_disc(k, c, c, 9, KEY_WELL);
}

/* Start drawing a user-space rectangle; returns zero if it cannot be drawn. */
static int canvas_begin(cp_surface *k, cp_rect user)
{
	int i;

	if(user.w <= 0 || user.h <= 0 || user.w * user.h > CP_MAX_KEY_PIXELS)
		return 0;
	cp_surface_init(k, key_buffer, user.w, user.h,
		cp_surface_mode(cp_view, cp_rotation));
	for(i = 0; i < user.w * user.h; i++)
		key_buffer[i] = PAD_BACKGROUND;
	return 1;
}

static void canvas_push(cp_rect user)
{
	cp_rect p = cp_user_to_panel_rect(cp_view, cp_rotation, user);

	lcd_begin(p.x, p.x + p.w - 1, p.y, p.y + p.h - 1);
	lcd_put_line(key_buffer, (unsigned)(p.w * p.h));
}

static void draw_touch_key(const cp_key *key, unsigned state)
{
	cp_surface k;
	int w = key->frame.w, h = key->frame.h;
	uint16_t ink = state ? KEY_INK_LIT : KEY_INK;

	if(!canvas_begin(&k, key->frame))
		return;

	switch(key->shape) {
	case CP_KEY_DPAD:
		key_dpad(&k, state);
		break;
	case CP_KEY_ROUND:
		key_body(&k, 0, 0, w, h, w / 2, (int)state);
		cp_text_centered(&k, &cp_font_title, 0, 0, w, h, key->label, ink);
		break;
	case CP_KEY_SHOULDER_L:
		key_body(&k, -14, 0, w + 14, h, 14, (int)state);
		cp_text_centered(&k, &cp_font_title, 0, 0, w, h, key->label, ink);
		break;
	case CP_KEY_SHOULDER_R:
		key_body(&k, 0, 0, w + 14, h, 14, (int)state);
		cp_text_centered(&k, &cp_font_title, 0, 0, w, h, key->label, ink);
		break;
	case CP_KEY_PILL:
		key_body(&k, 0, 0, w, h, h / 2, (int)state);
		break;
	default:
		key_body(&k, 0, 0, w, h, 10, (int)state);
		cp_text_centered(&k, cp_text_width(&cp_font_title, key->label) >
			w - 8 ? &cp_font_small : &cp_font_title, 0, 0, w, h,
			key->label, ink);
		break;
	}
	canvas_push(key->frame);
}

/* A pill's name sits under it; it never changes, so it is painted once. */
static void draw_key_caption(const cp_key *key)
{
	cp_rect area = { key->frame.x, (int16_t)(key->frame.y + key->frame.h + 2),
		key->frame.w, CP_KEY_CAPTION_H };
	cp_surface k;

	if(key->shape != CP_KEY_PILL || !canvas_begin(&k, area))
		return;
	cp_text_centered(&k, &cp_font_small, 0, 0, area.w, area.h, key->label,
		KEY_CAPTION);
	canvas_push(area);
}

/* Paint state of each key as last drawn (see cp_key_state). */
static uint8_t cp_key_drawn[CP_MAX_KEYS];

static void prepare_game_screen(void)
{
	cp_layout l = cp_layout_for(cp_view, cp_rotation);
	unsigned i;

	lcd_fill_panel((cp_rect){ 0, 0, CP_PANEL_W, CP_PANEL_H }, PAD_BACKGROUND);
	for(i = 0; i < l.key_count && cp_touch_keys; i++) {
		draw_touch_key(&l.keys[i], 0);
		draw_key_caption(&l.keys[i]);
	}
	memset(cp_key_drawn, 0, sizeof(cp_key_drawn));
	cp_game_screen = 1;
}

static void poll_touch(void)
{
	struct _touch_addots dots;
	cp_layout l;

	/* The scanner updates the sample from its timer interrupt. A sample
	 * from a read that was cut short is replaced by the one before it. */
	{
		static struct _touch_addots good;
#ifndef CGBA_HOST_TEST
		/* If reads keep failing a key must not stay held: after a few the
		 * touch counts as lifted. */
		static int bad_run;
		static uint32_t read_tick = ~0u;
		int bad;

		if(touch_polled) {
			/* Our own read, at most once a tick however often this is
			 * called in a frame. */
			uint32_t tick = fxcg100_ticks();

			if(tick == read_tick) {
				dots = good;
				bad = 0;
			} else {
				read_tick = tick;
				bad = !cp_touch_poll_read(&dots);
			}
		} else {
			cpu_atomic_start();
			touch_get_last_scan(NULL, NULL, &dots);
			bad = cp_touch_guard_take_bad_sample();
			cpu_atomic_end();
		}
		if(!bad)
			bad_run = 0;
		else if(++bad_run <= 4)
			dots = good;
		else
			memset(&dots, 0, sizeof(dots));
#else
		touch_get_last_scan(NULL, NULL, &dots);
#endif
		good = dots;
		(void)good;
	}

	/* The icon strip works everywhere, the keys only over the game screen.
	 * An icon counts only for a touch that began on the strip (a thumb
	 * sliding off Select or Start must not open the menu) and has been on
	 * the same icon for two polls running (a brush does nothing). */
	{
		static int was_touching, began_on_strip;
		static uint32_t last_icons;
		uint32_t icons = 0;

		if(dots.touches < 1) {
			began_on_strip = 0;
		} else {
			if(!was_touching)
				began_on_strip = cp_strip_icon(dots.x1, dots.y1) >= 0;
			if(began_on_strip)
				icons = cp_strip_icons(1, dots.x1, dots.y1, 0, 0);
		}
		was_touching = dots.touches >= 1;
		cp_strip = icons == last_icons ? icons : 0;
		last_icons = icons;
	}
	cp_touch_down = dots.touches >= 1 && dots.x1 >= 0 &&
		dots.x1 < CP_PANEL_W && dots.y1 >= 0 && dots.y1 < CP_PANEL_H;
	cp_touch_px = dots.x1;
	cp_touch_py = dots.y1;
	cp_touch_mask = 0;
	if(!cp_game_screen || !cp_touch_keys)
		return;

	l = cp_layout_for(cp_view, cp_rotation);
	cp_touch_mask = cp_layout_buttons(&l, cp_view, cp_rotation, dots.touches,
		dots.x1, dots.y1, dots.x2, dots.y2);
}

/* Paint the touch keys for the buttons that are held, whichever way they
 * are held: a key on the keypad lights its touch key as well. Only keys
 * whose look changes are redrawn. */
void fxcg100_touch_show_buttons(uint32_t buttons)
{
	cp_layout l;
	unsigned i;

	if(!cp_game_screen || !cp_touch_keys)
		return;
	l = cp_layout_for(cp_view, cp_rotation);
	for(i = 0; i < l.key_count; i++) {
		unsigned state = cp_key_state(&l.keys[i], buttons);

		if(state != cp_key_drawn[i]) {
			draw_touch_key(&l.keys[i], state);
			cp_key_drawn[i] = (uint8_t)state;
		}
	}
}

/* Position of the first touch on the LCD in the coordinates the player sees;
 * returns zero when the screen is not being touched. */
int fxcg100_touch_point(int *x, int *y)
{
	if(!cp_touch_down)
		return 0;
	cp_panel_to_user_point(cp_view, cp_rotation, cp_touch_px, cp_touch_py,
		x, y);
	return 1;
}

uint32_t fxcg100_touch_gba_buttons(void)
{
	return cp_touch_mask & 0x3ffu;
}

_Static_assert(FXCG100_STRIP_SETTINGS == 1u << CP_ICON_SETTINGS &&
	FXCG100_STRIP_MENU == 1u << CP_ICON_MENU &&
	FXCG100_STRIP_MAIN == 1u << CP_ICON_MAIN &&
	FXCG100_STRIP_RESIZE == 1u << CP_ICON_RESIZE &&
	FXCG100_STRIP_SWAP == 1u << CP_ICON_SWAP &&
	FXCG100_STRIP_ROTATE == 1u << CP_ICON_ROTATE &&
	FXCG100_STRIP_ESC == 1u << CP_ICON_ESC,
	"FXCG100_STRIP_* must match the CP_ICON_* order");

uint32_t fxcg100_touch_strip_icons(void)
{
	return cp_strip;
}

/* ---- keys ----------------------------------------------------------------- */

/* Keymaps hold gint KEY_* codes. The four arrows mean screen directions, so in
 * landscape they are read from whichever cursor key now points that way. */
static int physical_keycode(int keycode)
{
	static const uint8_t arrows[4] = {
		KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_LEFT
	};
	unsigned direction;

	switch(keycode) {
	case KEY_UP:    direction = 0; break;
	case KEY_RIGHT: direction = 1; break;
	case KEY_DOWN:  direction = 2; break;
	case KEY_LEFT:  direction = 3; break;
	default:        return keycode;
	}
	return arrows[cp_physical_direction(cp_view, cp_rotation, direction)];
}

int fxcg100_key_down(int basic_keycode)
{
	if(!basic_keycode)
		return 0;
	return keydown(physical_keycode(basic_keycode)) ? 1 : 0;
}

uint32_t fxcg100_poll_app_keys(void)
{
	uint32_t keys = 0;

	clearevents();
	poll_touch();

	/* The Menu/Settings icons open the gpSP menu, and so does Clear (the
	 * power key) while no function has been mapped to it.
	 * The remaining shell keys are only read inside the menu. */
	if(keydown(KEY_SHIFT))
		keys |= FXCG100_APPKEY_SHIFT;
	if(keydown(KEY_KBD))
		keys |= FXCG100_APPKEY_HOME;
	if((cp_clear_is_menu && keydown(KEY_CLEAR)) ||
			(cp_strip & (FXCG100_STRIP_MENU | FXCG100_STRIP_SETTINGS)))
		keys |= FXCG100_APPKEY_AC | FXCG100_APPKEY_ON;
	if(keydown(KEY_EXE))
		keys |= FXCG100_APPKEY_EXE;
	if(fxcg100_key_down(KEY_UP))
		keys |= FXCG100_APPKEY_UP;
	if(fxcg100_key_down(KEY_DOWN))
		keys |= FXCG100_APPKEY_DOWN;
	if(fxcg100_key_down(KEY_LEFT))
		keys |= FXCG100_APPKEY_LEFT;
	if(fxcg100_key_down(KEY_RIGHT))
		keys |= FXCG100_APPKEY_RIGHT;
	if(keydown(KEY_DEL) || (cp_strip & FXCG100_STRIP_ESC))
		keys |= FXCG100_APPKEY_BACK;

	return keys;
}

void fxcg100_wait_keys_released(void)
{
	for(unsigned poll = 0; poll < 20000u; poll++) {
		if(fxcg100_poll_app_keys() == 0)
			break;
		for(volatile unsigned i = 0; i < 2000; i++)
			;
	}
}

/* ---- gameplay presentation ------------------------------------------------ */

/* Send a strip built for the upright picture: `rows` rows of `width` at
 * (x, y) as the player sees them. Held upside down, the same rows go to the
 * mirrored place with their pixels in reverse order. */
static void portrait_strip_send(int x, int y, int width, int rows)
{
	if(cp_view_is_flipped(cp_view, cp_rotation)) {
		uint16_t *a = strip_buffer(), *b = a + width * rows - 1;

		for(; a < b; a++, b--) {
			uint16_t t = *a;

			*a = *b;
			*b = t;
		}
		x = CP_PANEL_W - x - width;
		y = CP_PANEL_H - y - rows;
	}
	strip_send(x, y, width, rows);
}

/* Portrait 1:1: the framebuffer is already in panel order; 16 rows a strip. */
#if !defined(CGBA_HOST_TEST)
extern void cgba_copy_32b_sh4(void *dst, const void *src, uint32_t nbytes);
#endif
static void blit_portrait_1x(const uint16_t *pixels, cp_rect img)
{
	/* portrait_strip_send() mirrors y when flipped, so strips follow the
	 * panel's refresh direction when (flipped == bottom_up). */
	int in_order = cp_view_is_flipped(cp_view, cp_rotation)
		== lcd_refresh_bottom_up();
	int s, y;

	for(s = 0; s < CP_GBA_H / 16; s++) {
		y = strip_order(s, CP_GBA_H / 16, in_order) * 16;
#if !defined(CGBA_HOST_TEST)
		cgba_copy_32b_sh4(strip_buffer(), pixels + y * CP_GBA_W,
			(uint32_t)(16 * CP_GBA_W * sizeof(uint16_t)));
#else
		memcpy(strip_buffer(), pixels + y * CP_GBA_W,
			16 * CP_GBA_W * sizeof(uint16_t));
#endif
		portrait_strip_send(img.x, img.y + y, CP_GBA_W, 16);
	}
}

/* Portrait 4:3: 53 groups of 3 source rows -> 4 output rows, 240 -> 320
 * across; three groups (12 rows) a strip. Same tap composition as the fx-CG
 * 4:3 mode, composed in place in the strip. */
static void blit_portrait_43(const uint16_t *pixels, cp_rect img)
{
	void (*hs)(const uint16_t *, uint16_t *) = cgba_scale_row_240_320;
	int groups = img.h / 4, group;
	int strips = (groups + 2) / 3, s;
	int in_order = cp_view_is_flipped(cp_view, cp_rotation)
		== lcd_refresh_bottom_up();

	if(cp_filter == CGBA_SCALE_FILTER_CRISP)
		hs = cgba_scale_row_240_320_crisp;
	else if(cp_filter == CGBA_SCALE_FILTER_SHARP)
		hs = cgba_scale_row_240_320_sharp;

	for(s = 0; s < strips; s++) {
		uint16_t *strip = strip_buffer();
		int first = strip_order(s, strips, in_order) * 3, n;

		group = first;

		for(n = 0; n < 3 && group < groups; n++, group++) {
			const uint16_t *src = pixels + group * 3 * CP_GBA_W;
			uint16_t *r0 = strip + n * 4 * 320;
			uint16_t *r1 = r0 + 320, *r2 = r1 + 320, *r3 = r2 + 320;

			hs(src, r0);
			hs(src + CP_GBA_W, r1);
			hs(src + 2 * CP_GBA_W, r3);
			if(cp_filter == CGBA_SCALE_FILTER_SMOOTH) {
				cgba_scale_row_avg(r1, r3, r2, 320);
				cgba_scale_row_avg(r0, r1, r1, 320);
			} else if(cp_filter == CGBA_SCALE_FILTER_SHARP) {
				cgba_scale_row_avg(r1, r3, r2, 320);
			} else {
				memcpy(r2, r3, 320 * sizeof(uint16_t));
			}
		}
		portrait_strip_send(img.x, img.y + first * 4, 320, n * 4);
	}
}

/* Landscape: the panel scans across what the player sees as columns, so the
 * frame is emitted one GBA column at a time (see classpad_scale.h).
 *   2x    a column becomes two identical 320-pixel panel rows; 6 columns
 *         (12 rows) a strip
 *   1.5x  a column pair becomes three 240-pixel panel rows; 5 pairs
 *         (15 rows) a strip */
static void blit_landscape(const uint16_t *pixels, cp_rect img, int doubled)
{
	static uint16_t column[CP_GBA_H] __attribute__((aligned(4)));
	cp_rect p = cp_user_to_panel_rect(cp_view, cp_rotation, img);
	int keys_left = cp_rotation == CP_ROTATION_KEYS_LEFT;
	int in_order = !lcd_refresh_bottom_up();
	int width = p.w;                       /* panel row length */
	int per_strip = doubled ? 6 : 10;      /* GBA columns per strip */
	int rows_per_strip = doubled ? 12 : 15;
	int strips, s, i = 0, row = 0;

	if(cp_view == CP_VIEW_LANDSCAPE_1X) {
		/* 1:1: a GBA column is a panel row as it is; 16 of them a strip. */
		strips = CP_GBA_W / 16;
		for(s = 0; s < strips; s++) {
			uint16_t *line = strip_buffer();
			int k;

			i = strip_order(s, strips, in_order) * 16;

			for(k = 0; k < 16; k++)
				cp_gather_column(pixels, keys_left ? CP_GBA_W - 1 - (i + k)
					: i + k, !keys_left, line + k * width);
			strip_send(p.x, p.y + i, width, 16);
		}
		return;
	}

	strips = CP_GBA_W / per_strip;
	for(s = 0; s < strips; s++) {
		uint16_t *line = strip_buffer();
		int end;

		row = strip_order(s, strips, in_order);
		i = row * per_strip;
		end = i + per_strip;

		for(; i < end; i += doubled ? 1 : 2) {
			unsigned sx = keys_left ? CP_GBA_W - 1 - i : i;

			cp_gather_column(pixels, sx, !keys_left, column);
			if(doubled) {
				cp_line_double(column, line, CP_GBA_H);
				memcpy(line + width, line, width * sizeof(uint16_t));
				line += 2 * width;
			} else {
				cp_line_2to3(column, line, CP_GBA_H, (int)cp_filter);
				cp_gather_column(pixels, keys_left ? sx - 1 : sx + 1,
					!keys_left, column);
				cp_line_2to3(column, line + 2 * width, CP_GBA_H,
					(int)cp_filter);
				cp_line_between(line, line + 2 * width, line + width,
					(unsigned)width, (int)cp_filter);
				line += 3 * width;
			}
		}
		strip_send(p.x, p.y + row * rows_per_strip, width, rows_per_strip);
	}
}

/* ---- refresh sync (classpad_sync.h) --------------------------------------- */

static uint32_t cp_sync;             /* the option: wait before each frame */
/* Where the picture's rows are in the panel's count of rows shown, in
 * sixteenths of a refresh: the count includes blanking rows, and where they
 * are is not known, so this is set by eye (Settings). */
static uint32_t cp_sync_offset;
/* The page that sets it is open: frames wait whatever the option says, and
 * with next to no room to spare, so that an offset a step out already shows
 * as tearing. */
static int sync_calibrating;

#ifndef CGBA_HOST_TEST
/* Which end of the window the next frame of that page starts at. */
static int sync_late;
enum { SCAN_UNKNOWN = 0, SCAN_USABLE, SCAN_NONE };
static int scan_state;
static uint32_t scan_total, scan_hz;
static cp_sync_track sync_track;
static int sync_tracking;
static uint32_t sync_lines[SYNC_KINDS];   /* rows shown during the last frame */
static uint32_t sync_strips[SYNC_KINDS];  /* strips it was sent in */
static int sync_kind;

/* The row the panel is showing: DCS get_scanline, two bytes after a dummy
 * read, as for the ID register above. On R61523 (datasheet p.131), the
 * scanline counter runs on the 1 MHz internal oscillator and requires
 * >= 3 us after writing 0x45 before the dummy read, and >= 3 us after the
 * second parameter before issuing 0x45 again. */
static uint32_t lcd_scanline(void)
{
	uint32_t hi, lo;

	wait_lcd_dma();
	lcd_select(0x45);
	if(!lcd_is_r61523()) {
		hi = *LCD_DATA;
		hi = *LCD_DATA & 0xffu;
		lo = *LCD_DATA & 0xffu;
	} else {
		lcd_delay_ns(3500);
		(void)r61523_read_word();
		hi = r61523_read_word() & 0xffu;
		lo = r61523_read_word() & 0xffu;
		lcd_delay_ns(3500);
	}
	return ((hi << 8) | lo) & 0x3ffu;
}

/* A quarter of a second of reading it, once a start and again whenever the
 * page that sets the offset opens: is it a row counter, how many rows are
 * there in a refresh, how often does it come round. */
static void scan_probe(void)
{
	cp_scan_probe probe = { 0 };
	uint32_t start, ticks = 0, n;

	/* Pixels have just been written: the first reads are the least to be
	 * trusted. The probe does not believe a lone wrong read anyway. */
	for(n = 0; n < 8u; n++)
		(void)lcd_scanline();
	start = fxcg100_ticks();
	for(n = 0; n < 4000000u && ticks < 32u; n++) {
		cp_scan_probe_add(&probe, lcd_scanline());
		if((n & 63u) == 0)
			ticks = fxcg100_ticks() - start;
	}
	scan_total = cp_scan_probe_result(&probe, ticks, &scan_hz);
	scan_state = scan_total ? SCAN_USABLE : SCAN_NONE;
	if(!scan_total)
		scan_total = probe.max ? probe.max : 1u;
	/* Measured against the count before, which may not be this one. */
	memset(sync_lines, 0, sizeof(sync_lines));
	memset(sync_strips, 0, sizeof(sync_strips));
}

/* Positions are counted in the panel's refresh order (from 527 downwards
 * when bottom-up, from 0 upwards when top-down), plus the offset. */
static uint32_t scan_position(void)
{
	uint32_t shift = scan_total * cp_sync_offset / 16u;
	uint32_t row = lcd_scanline() % scan_total;
	int tries;

	/* Two reads that agree (classpad_sync.h); four more at the most. */
	for(tries = 0; tries < 4; tries++) {
		uint32_t again = lcd_scanline() % scan_total;
		int agree = cp_scan_agree(row, again, scan_total);

		row = again;
		if(agree)
			break;
	}
	return (row + scan_total - shift) % scan_total;
}

static void sync_strip(int r0, int rows)
{
	if(sync_tracking) {
		uint32_t row = lcd_refresh_bottom_up()
			? (uint32_t)(CP_PANEL_H - r0 - rows) : (uint32_t)r0;

		cp_sync_track_strip(&sync_track, scan_position(), row);
	}
}

/* Before a frame whose picture is the panel rows [top, top + rows), sent
 * in the panel's refresh order. */
static void sync_begin(int top, int rows, int kind)
{
	uint32_t lo, hi, strip;
	int bottom_up = lcd_refresh_bottom_up();
	uint32_t first = bottom_up
		? (uint32_t)(CP_PANEL_H - top - rows) : (uint32_t)top;

	if(scan_state == SCAN_UNKNOWN)
		scan_probe();
	if(scan_state != SCAN_USABLE)
		return;

	sync_kind = kind;
	/* When the refresh is bottom-up, each strip's window is still filled
	 * top-to-bottom (against the refresh), so the window keeps one strip's
	 * height and time clear (classpad_sync.h). Top-down fills with the
	 * refresh and needs no intra-strip penalty. */
	strip = bottom_up && sync_strips[kind]
		? ((uint32_t)rows + sync_strips[kind] - 1u) / sync_strips[kind] : 0;
	if((cp_sync || sync_calibrating) && cp_sync_window_part(scan_total,
			(uint32_t)rows, sync_lines[kind], 0,
			sync_calibrating ? CP_SYNC_PART_CALIBRATE
			: kind == SYNC_GAME ? CP_SYNC_PART_GAME : CP_SYNC_PART,
			strip, &lo, &hi)) {
		uint32_t start = fxcg100_ticks(), n;

		/* Setting the offset: the two ends of the window in turn
		 * (cp_sync_window_end), so a position too high tears as well
		 * as one too low. */
		if(sync_calibrating) {
			cp_sync_window_end(&lo, &hi, sync_late);
			sync_late = !sync_late;
		}
		/* At most one refresh; given up after 47 ms whatever happens. */
		for(n = 0; n < 400000u; n++) {
			uint32_t at = (scan_position() + scan_total - first) % scan_total;

			if(at >= lo && at < hi)
				break;
			if((n & 63u) == 63u && fxcg100_ticks() - start > 6u)
				break;
		}
	}
	cp_sync_track_begin(&sync_track, scan_total);
	sync_tracking = 1;
}

static void sync_end(void)
{
	if(!sync_tracking)
		return;
	sync_tracking = 0;
	sync_lines[sync_kind] = cp_sync_track_lines(&sync_track);
	sync_strips[sync_kind] = sync_track.strips;
}
#endif

void fxcg100_lcd_set_sync(uint32_t wait, uint32_t offset)
{
	wait = wait ? 1 : 0;
	offset &= 15u;
	cp_sync = wait;
	cp_sync_offset = offset;
}

void fxcg100_lcd_sync_calibrate(uint32_t on)
{
#ifndef CGBA_HOST_TEST
	/* Count the rows again as the page opens, so that what it shows, and
	 * what the offset is set against, is this start's count. */
	if(on && !sync_calibrating)
		scan_state = SCAN_UNKNOWN;
#endif
	sync_calibrating = on != 0;
}

int fxcg100_lcd_sync_info(uint32_t *rows, uint32_t *hz)
{
#ifndef CGBA_HOST_TEST
	*rows = scan_state == SCAN_USABLE ? scan_total : 0;
	*hz = scan_state == SCAN_UNKNOWN ? 0 : scan_hz;
	return scan_state == SCAN_USABLE ? 1 : scan_state == SCAN_NONE ? -1 : 0;
#else
	*rows = 0;
	*hz = 0;
	return 0;
#endif
}

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
	cp_layout l;

	if(!pixels)
		return;
	if(!cp_game_screen)
		prepare_game_screen();

	l = cp_layout_for(cp_view, cp_rotation);
#ifndef CGBA_HOST_TEST
	{
		cp_rect p = cp_user_to_panel_rect(cp_view, cp_rotation, l.image);

		sync_begin(p.y, p.h, SYNC_GAME);
	}
#endif
	switch(cp_view) {
	case CP_VIEW_PORTRAIT_43:
		blit_portrait_43(pixels, l.image);
		break;
	case CP_VIEW_LANDSCAPE_15X:
	case CP_VIEW_LANDSCAPE_1X:
		blit_landscape(pixels, l.image, 0);
		break;
	case CP_VIEW_LANDSCAPE_2X:
		blit_landscape(pixels, l.image, 1);
		break;
	default:
		blit_portrait_1x(pixels, l.image);
		break;
	}
#ifndef CGBA_HOST_TEST
	sync_end();
#endif
	/* The last strip is left in flight on purpose: its data is in on-chip
	 * RAM, and every later panel access goes through lcd_begin(), which
	 * waits for it. The next frame's emulation overlaps the transfer. */
}

/* Scanline streaming (an fx-CG experiment) is not implemented here. */
int fxcg100_lcd_is_synced(void) { return 0; }
int fxcg100_lcd_stream_begin(void) { return 0; }
void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y, int dirty)
{ (void)row; (void)y; (void)dirty; }
int fxcg100_lcd_stream_end(void) { return 0; }
