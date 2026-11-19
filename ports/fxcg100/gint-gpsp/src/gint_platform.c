#include "fxcg100_platform.h"
#include "../../fxcg100_scale.h"
#include "../../fxcg100_lcd_cleanup.h"
#include "../../classpad_sync.h"

#ifndef CGBA_HOST_TEST
#include <gint/rtc.h>
#endif
#include <gint/dma.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/keyboard.h>
#include <gint/keycodes.h>

#include <stdio.h>
#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define GBA_W 240
#define GBA_H 160
#define LCD_DATA_REGISTER ((uintptr_t)0xb4000000u)
#define GBA_FRAME_BYTES (GBA_W * GBA_H * (int)sizeof(uint16_t))
#define GBA_FRAME_DMA_BLOCKS (GBA_FRAME_BYTES / 32)

/*
 * The hardware-proven presenter gives each DMA buffer a full 8 KiB on-chip
 * bank. If the experimental resident fastmem image claims XRAM, fall back to
 * two 4 KiB halves of YRAM; that layout costs substantially more DMA launches
 * in the scaled modes and is therefore not the release default.
 */
#if defined(CGBA_HOST_TEST)
/* Host build (tests/fxcg_present_test.c): the banks are ordinary arrays. */
static uint16_t host_strips[2][0x1000] __attribute__((aligned(32)));
# define STRIP_LINES 16
# define STRIP_43_LINES 12
# define STRIP_FULL_LINES 8
# define STRIP_BANK_BYTES 0x2000
# define STRIP_BUF0 ((uintptr_t)host_strips[0])
# define STRIP_BUF1 ((uintptr_t)host_strips[1])
#elif defined(CGBA_SH4_FASTMEM_XYRAM) && CGBA_SH4_FASTMEM_XYRAM
# define STRIP_LINES 8
# define STRIP_43_LINES 4
# define STRIP_FULL_LINES 4
# define STRIP_BANK_BYTES 0x1000
# define STRIP_BUF0 ((uintptr_t)0xe5010000u)
# define STRIP_BUF1 ((uintptr_t)0xe5011000u)
#else
# define STRIP_LINES 16
# define STRIP_43_LINES 12
# define STRIP_FULL_LINES 8
# define STRIP_BANK_BYTES 0x2000
# define STRIP_BUF0 ((uintptr_t)0xe5007000u)
# define STRIP_BUF1 ((uintptr_t)0xe5017000u)
#endif
_Static_assert(STRIP_LINES * GBA_W * 2 <= STRIP_BANK_BYTES,
	"GBA strip must fit its on-chip DMA bank");
_Static_assert(STRIP_LINES % 4 == 0,
	"strip height must stay 4-row aligned for the R61524 DMA window");
_Static_assert(STRIP_43_LINES * 320 * 2 <= STRIP_BANK_BYTES,
	"4:3 strip must fit its on-chip DMA bank");
_Static_assert(STRIP_FULL_LINES * 384 * 2 <= STRIP_BANK_BYTES,
	"fullscreen strip must fit its on-chip DMA bank");

static int lcd_dma_pending;
/* The gameplay blit narrows the R61524 GRAM window to the GBA rectangle via
 * r61524_start_frame(). gint's dclear/dtext/dupdate menu rendering assumes the
 * full 396x224 window, so we must restore it before any gint push or the menu
 * shows a black screen with a stray line. Tracked here, healed at push time. */
static int lcd_window_partial;
/* The OS paints its BFile busy indicator directly into LCD GRAM during a
 * world switch. Centered gameplay does not cover that top-right border, so
 * remember the activity until a full update or gameplay blit removes it. */
static int lcd_os_overlay_dirty;

static void wait_lcd_dma(void)
{
	if(!lcd_dma_pending)
		return;

	dma_transfer_wait(0);
	lcd_dma_pending = 0;
}

/* Hand the panel back to gint: finish any direct blit DMA and restore the full
 * display window. No-op unless a gameplay blit narrowed the window. */
static void restore_full_window(void)
{
	if(!lcd_window_partial)
		return;

	wait_lcd_dma();
	r61524_win_set(0, DWIDTH - 1, 0, DHEIGHT - 1);
	lcd_window_partial = 0;
}

#ifndef CGBA_FXCG50
static int keycode_from_cg100_skin_code(int basic_keycode)
{
	static const int cg100_rows_9_to_5[][6] = {
		{ KEY_PAGEUP, KEY_NEXTTAB, KEY_UP, KEY_PREVTAB, KEY_HOME, KEY_ON },
		{ KEY_PAGEDOWN, KEY_RIGHT, KEY_OK, KEY_LEFT, KEY_BACK, KEY_SETTINGS },
		{ KEY_TOOLS, KEY_CATALOG, KEY_DOWN, KEY_VARS, KEY_ALPHA, KEY_SHIFT },
		{ KEY_EXPFUN, KEY_SQUARE, KEY_POWER, KEY_SQRT, KEY_FRAC, KEY_XOT },
		{ KEY_RIGHTP, KEY_LEFTP, KEY_TAN, KEY_COS, KEY_SIN, KEY_COMMA },
	};
	int row = basic_keycode % 10;
	int col = basic_keycode / 10 - 1;

	if(row < 0 || row > 9 || col < 1 || col > 6)
		return 0;

	/*
	 * Keymaps are stored as CG100 skin positions (eg. 49 = Up,
	 * 48 = OK, 79 = ON). gint's keyboard API expects logical keycodes on
	 * Graph Math+, not raw fx-CG20-style matrix positions.
	 */
	if(row >= 5)
		return cg100_rows_9_to_5[9 - row][col - 1];
	if(basic_keycode == 34)
		return KEY_ACON;
	if(basic_keycode == 41)
		return KEY_FORMAT;

	return (row << 4) + (7 - col);
}

#endif /* !CGBA_FXCG50 */

int fxcg100_key_down(int basic_keycode)
{
#ifdef CGBA_FXCG50
	/* fx-CG50: keymaps hold gint KEY_* codes directly (the classic matrix). */
	return basic_keycode ? (keydown(basic_keycode) ? 1 : 0) : 0;
#else
	int keycode = keycode_from_cg100_skin_code(basic_keycode);

	return keycode ? keydown(keycode) : 0;
#endif
}

uint32_t fxcg100_poll_app_keys(void)
{
	uint32_t keys = 0;

	clearevents();

#ifdef CGBA_FXCG50
	/* fx-CG50 shell navigation. Only APPKEY_ON is read during gameplay (to
	 * open the emulator menu), so it must be a non-GBA key -> AC/ON. The rest
	 * are polled only inside the menu, where GBA input is paused, so they can
	 * reuse the arrows / EXE / EXIT. */
	if(keydown(KEY_SHIFT))
		keys |= FXCG100_APPKEY_SHIFT;
	if(keydown(KEY_MENU))
		keys |= FXCG100_APPKEY_HOME;
	if(keydown(KEY_ACON))
		keys |= FXCG100_APPKEY_AC | FXCG100_APPKEY_ON;
	if(keydown(KEY_EXE))
		keys |= FXCG100_APPKEY_EXE;
	if(keydown(KEY_OPTN))
		keys |= FXCG100_APPKEY_MENU;
	if(keydown(KEY_UP))
		keys |= FXCG100_APPKEY_UP;
	if(keydown(KEY_DOWN))
		keys |= FXCG100_APPKEY_DOWN;
	if(keydown(KEY_LEFT))
		keys |= FXCG100_APPKEY_LEFT;
	if(keydown(KEY_RIGHT))
		keys |= FXCG100_APPKEY_RIGHT;
	if(keydown(KEY_EXIT))
		keys |= FXCG100_APPKEY_BACK;
	if(keydown(KEY_OPTN))
		keys |= FXCG100_APPKEY_SETTINGS;
	if(keydown(KEY_DEL))
		keys |= FXCG100_APPKEY_DEL;
	{
		static const int digits[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
			KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };

		for(int i = 0; i < 10; i++)
			if(keydown(digits[i]))
				keys |= FXCG100_APPKEY_DIGIT(i);
	}
#else
	if(fxcg100_key_down(77))
		keys |= FXCG100_APPKEY_SHIFT;
	if(fxcg100_key_down(69))
		keys |= FXCG100_APPKEY_HOME;
	if(fxcg100_key_down(34))
		keys |= FXCG100_APPKEY_AC;
	if(fxcg100_key_down(31) || fxcg100_key_down(48))
		keys |= FXCG100_APPKEY_EXE;
	if(fxcg100_key_down(27))
		keys |= FXCG100_APPKEY_MENU;
	if(fxcg100_key_down(49))
		keys |= FXCG100_APPKEY_UP;
	if(fxcg100_key_down(47))
		keys |= FXCG100_APPKEY_DOWN;
	if(fxcg100_key_down(58))
		keys |= FXCG100_APPKEY_LEFT;
	if(fxcg100_key_down(38))
		keys |= FXCG100_APPKEY_RIGHT;
	if(fxcg100_key_down(68))
		keys |= FXCG100_APPKEY_BACK;
	if(fxcg100_key_down(FXCG100_PHYSKEY_ON))
		keys |= FXCG100_APPKEY_ON;
	if(fxcg100_key_down(78))
		keys |= FXCG100_APPKEY_SETTINGS;
	if(fxcg100_key_down(44))
		keys |= FXCG100_APPKEY_DEL;
	{
		/* Skin positions of 0..9 (see fxcg100_keys.c). */
		static const uint8_t digits[10] = { 71, 72, 62, 52, 73, 63, 53, 74,
			64, 54 };

		for(int i = 0; i < 10; i++)
			if(fxcg100_key_down(digits[i]))
				keys |= FXCG100_APPKEY_DIGIT(i);
	}
#endif

	return keys;
}

void fxcg100_lcd_init(void)
{
	lcd_os_overlay_dirty = 0;
}

void fxcg100_lcd_shutdown(void)
{
	restore_full_window();
	wait_lcd_dma();
}

void fxcg100_lcd_clear(uint16_t color)
{
	wait_lcd_dma();
	dclear((int)color);
}

void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
	uint16_t color)
{
	if(x >= LCD_W || y >= LCD_H || w == 0 || h == 0)
		return;

	wait_lcd_dma();
	if(x + w > LCD_W)
		w = LCD_W - x;
	if(y + h > LCD_H)
		h = LCD_H - y;

	drect((int)x, (int)y, (int)(x + w - 1), (int)(y + h - 1),
		(int)color);
}

void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
	uint16_t fg, uint16_t bg)
{
	wait_lcd_dma();
	dtext_opt((int)x, (int)y, (int)fg, (int)bg,
		DTEXT_LEFT, DTEXT_TOP, text ? text : "");
}

void fxcg100_lcd_update(void)
{
	restore_full_window();
	wait_lcd_dma();
	dupdate();
	lcd_os_overlay_dirty = 0;
}

void fxcg100_lcd_update_rows(unsigned y, unsigned h)
{
	unsigned y1 = y + h;

	if(y >= DHEIGHT || h == 0)
		return;
	if(y1 > DHEIGHT)
		y1 = DHEIGHT;
	/* r61524_display() sends whole rows by DMA in groups of four. */
	y &= ~3u;
	y1 = (y1 + 3u) & ~3u;
	if(y1 > DHEIGHT)
		y1 = DHEIGHT;
	restore_full_window();
	wait_lcd_dma();
	r61524_display(gint_vram, (int)y, (int)(y1 - y), R61524_DMA_WAIT);
}

void fxcg100_lcd_status(const char *text)
{
	uint16_t bg = C_RGB(0, 10, 22);
	uint16_t fg = C_WHITE;

	restore_full_window();
	wait_lcd_dma();
	drect(0, 0, DWIDTH - 1, 13, bg);
	dtext_opt(4, 2, fg, bg, DTEXT_LEFT, DTEXT_TOP, text ? text : "");
	dupdate();
	lcd_os_overlay_dirty = 0;
}

void fxcg100_lcd_note_os_activity(void)
{
	lcd_os_overlay_dirty = 1;
}

/* ---- 128 Hz ticks, for the refresh sync's probe and time-outs -------------- */

#ifndef CGBA_HOST_TEST
/* The fx-CG's RTC runs on every model, and its 128 Hz count does not follow
 * the clock settings; the frame pacing uses it too. */
uint32_t fxcg100_ticks(void)
{
	return rtc_ticks();
}
#endif

/* ---- refresh sync (classpad_sync.h) ---------------------------------------
 * The panel shows its memory row after row, about sixty times a second,
 * whatever is being written to it, and a frame goes out in strips. Where the
 * row being shown and the row being written meet while a frame goes out, the
 * screen below that point still shows the frame before: a tear. As on the
 * fx-CP400 (classpad_platform.c, docs/classpad-port.md), the controller is
 * asked which row it is showing (DCS get_scanline, 0x45), and a frame is
 * started at a moment from which, given how many rows are shown while one
 * goes out (measured on the frame before of the same kind), the two do not
 * meet.
 *
 * What the fx-CP400 settled and the fx-CG still has to:
 *   - whether the controller answers 0x45 at all. gint drives the 2025-2026
 *     panels (ID 50 and 82, 0x32 and 0x52) with the DCS commands of the
 *     fx-CP400's later panels (IDs 0x16 and 0x52), whose 0x45 is a row
 *     counter. The R61524 has registers at those numbers instead. Nothing is
 *     assumed: a quarter of a second of reads, the first time a frame is to
 *     wait, tells whether the values count up, come round 20 to 200 times a
 *     second and agree with themselves (cp_scan_probe_result_min). Until
 *     then, and if they do not, nothing is read and nothing waits.
 *   - which way the refresh runs. Positions are counted in refresh order. A
 *     DCS panel reports its address mode (0x0b); with MY set the OS has the
 *     rows the other way round, which on the fx-CP400 means the refresh
 *     starts at the last row. The calibration page can flip that, in case it
 *     is not the whole story. The strips of a frame that waits are then sent
 *     in the order the refresh takes them.
 *   - where the picture lies in the count, which includes blanking rows:
 *     the refresh position, sixteenths of the count, set by eye. */

#define LCD_DATA_PORT ((volatile uint16_t *)LCD_DATA_REGISTER)
#define LCD_PRDR      ((volatile uint8_t *)0xa405013cu)  /* bit 4 = LCD RS */
#define LCD_ROWS 224u           /* rows of the panel's memory (DHEIGHT) */
#define SYNC_MIN_LAST 200u      /* the panel shows 216 rows */
enum { SYNC_KINDS = 3 };        /* one per display scaling mode */

static uint32_t lcd_sync;         /* the option: wait before each frame */
static uint32_t lcd_sync_offset;  /* 0..15, sixteenths of a refresh */
static uint32_t lcd_sync_flip;    /* refresh direction the other way round */
/* The calibration page is open: frames wait whatever the option says, with
 * half a step to spare and at the two ends of the window in turn. */
static int sync_calibrating;
/* Which point of the window the next frame of that page starts at. */
static uint32_t sync_point;

enum { SCAN_UNKNOWN = 0, SCAN_USABLE, SCAN_NONE };
static int scan_state;
static uint32_t scan_total, scan_hz;
static int panel_known, panel_dcs;
static uint32_t panel_id, panel_mode;
static cp_sync_track sync_track;
static int sync_tracking;
static int sync_upward;
static unsigned sync_kind;
static uint32_t sync_lines[SYNC_KINDS];   /* rows shown during the last frame */
static uint32_t sync_strips[SYNC_KINDS];  /* strips it was sent in */

#ifndef CGBA_HOST_TEST
static inline void lcd_synco(void)
{
	__asm__ volatile("synco");
}

/* Select a controller register; RS returns high so data can follow. The same
 * sequence as gint's R61524 driver. */
static void lcd_select(unsigned reg)
{
	*LCD_PRDR &= (uint8_t)~0x10;
	lcd_synco();
	*LCD_DATA_PORT = (uint16_t)reg;
	lcd_synco();
	*LCD_PRDR |= 0x10;
	lcd_synco();
}

/* One command's reply: a dummy read, then the byte. */
static uint32_t lcd_read_byte(unsigned reg)
{
	lcd_select(reg);
	(void)*LCD_DATA_PORT;
	return *LCD_DATA_PORT & 0xffu;
}

/* The panel's ID as gint reads it (r61524_get_version: register 0xda, the
 * first read discarded), and for the DCS panels their address mode. */
static void panel_identify(void)
{
	if(panel_known)
		return;
	lcd_select(0xda);
	(void)*LCD_DATA_PORT;
	panel_id = *LCD_DATA_PORT;
	panel_dcs = panel_id == 50 || panel_id == 82;
	panel_mode = panel_dcs ? lcd_read_byte(0x0b) : 0;
	panel_known = 1;
}

/* The row being shown: DCS get_scanline, two bytes after a dummy read. */
static uint32_t lcd_scanline(void)
{
	uint32_t hi, lo;

	lcd_select(0x45);
	(void)*LCD_DATA_PORT;
	hi = *LCD_DATA_PORT & 0xffu;
	lo = *LCD_DATA_PORT & 0xffu;
	return ((hi << 8) | lo) & 0x3ffu;
}
#else
/* Host build (tests/fxcg_present_test.c): a simulated panel. */
uint32_t cg_host_scanline(void);
uint32_t cg_host_panel_id(void);
uint32_t cg_host_panel_mode(void);

static void panel_identify(void)
{
	if(panel_known)
		return;
	panel_id = cg_host_panel_id();
	panel_dcs = panel_id == 50 || panel_id == 82;
	panel_mode = panel_dcs ? cg_host_panel_mode() : 0;
	panel_known = 1;
}

static uint32_t lcd_scanline(void)
{
	return cg_host_scanline();
}
#endif

/* A quarter of a second of reading 0x45, the first time a frame is to wait
 * and again whenever the calibration page opens: is it a row counter, how
 * many rows are there in a refresh, how often does it come round. Only reads
 * that the next one agrees with count (classpad_sync.h). */
static void scan_probe(void)
{
	cp_scan_probe probe = { 0 };
	uint32_t start, ticks = 0, n;

	wait_lcd_dma();
	panel_identify();
	for(n = 0; n < 8u; n++)
		(void)lcd_scanline();
	start = fxcg100_ticks();
	for(n = 0; n < 4000000u && ticks < 32u; n++) {
		cp_scan_probe_add(&probe, lcd_scanline());
		if((n & 63u) == 0)
			ticks = fxcg100_ticks() - start;
	}
	scan_total = cp_scan_probe_result_min(&probe, ticks, &scan_hz,
		SYNC_MIN_LAST);
	scan_state = scan_total ? SCAN_USABLE : SCAN_NONE;
	if(!scan_total)
		scan_total = probe.max ? probe.max + 1u : 1u;
	/* Measured against the count before, which may not be this one. */
	memset(sync_lines, 0, sizeof(sync_lines));
	memset(sync_strips, 0, sizeof(sync_strips));
}

/* Where the refresh is, in refresh order from the picture's first row of
 * the count: two reads that agree, four more at the most. */
static uint32_t scan_position(void)
{
	uint32_t shift = scan_total * lcd_sync_offset / 16u;
	uint32_t row = lcd_scanline() % scan_total;
	int tries;

	for(tries = 0; tries < 4; tries++) {
		uint32_t again = lcd_scanline() % scan_total;
		int agree = cp_scan_agree(row, again, scan_total);

		row = again;
		if(agree)
			break;
	}
	return (row + scan_total - shift) % scan_total;
}

/* Where panel rows [r0, r0 + rows) start in refresh order. */
static uint32_t sync_row_position(int r0, int rows)
{
	return sync_upward ? LCD_ROWS - (uint32_t)(r0 + rows) : (uint32_t)r0;
}

/* Before a frame whose picture is the panel rows [top, top + rows), of one
 * of the SYNC_KINDS. Waits for the moment to start it if VSync is on (or
 * the calibration page is open) and the panel has a row counter. Returns
 * nonzero if the strips must go up the panel, last one first: the way the
 * refresh goes. Without VSync nothing is read and the strips go down as
 * they always have. */
static int sync_begin(int top, int rows, unsigned kind)
{
	uint32_t lo, hi, strip, first;

	sync_tracking = 0;
	if(!lcd_sync && !sync_calibrating)
		return 0;
	wait_lcd_dma();
	if(scan_state == SCAN_UNKNOWN)
		scan_probe();
	if(scan_state != SCAN_USABLE)
		return 0;

	sync_upward = ((panel_mode & 0x80u) != 0) ^ (lcd_sync_flip != 0);
	sync_kind = kind;
	first = sync_row_position(top, rows);
	/* Going up, each strip is filled from its top row down, against the
	 * refresh: the window keeps a strip clear (classpad_sync.h). Going
	 * down, it is filled the way the refresh goes. */
	strip = sync_upward && sync_strips[kind]
		? ((uint32_t)rows + sync_strips[kind] - 1u) / sync_strips[kind] : 0;
	if(cp_sync_window_part(scan_total, (uint32_t)rows, sync_lines[kind], 0,
			sync_calibrating ? CP_SYNC_PART_CALIBRATE : CP_SYNC_PART_GAME,
			strip, &lo, &hi)) {
		uint32_t start = fxcg100_ticks(), n;

		/* Setting the position: points a step apart across the window, in
		 * turn, so that a position out in either direction, or by half a
		 * refresh, tears (cp_sync_window_point). */
		if(sync_calibrating)
			cp_sync_window_point(&lo, &hi, sync_point++, scan_total / 16u);
		/* At most one refresh; given up after 47 ms whatever happens. */
		for(n = 0; n < 400000u; n++) {
			uint32_t at = (scan_position() + scan_total - first) %
				scan_total;

			if(at >= lo && at < hi)
				break;
			if((n & 63u) == 63u && fxcg100_ticks() - start > 6u)
				break;
		}
	}
	cp_sync_track_begin(&sync_track, scan_total);
	sync_tracking = 1;
	return sync_upward;
}

/* Before each strip of a frame that waited: how far the refresh has come. */
static void sync_strip(int r0, int rows)
{
	if(!sync_tracking)
		return;
	wait_lcd_dma();
	cp_sync_track_strip(&sync_track, scan_position(),
		sync_row_position(r0, rows));
}

static void sync_end(void)
{
	if(!sync_tracking)
		return;
	sync_tracking = 0;
	sync_lines[sync_kind] = cp_sync_track_lines(&sync_track);
	sync_strips[sync_kind] = sync_track.strips;
}

void fxcg100_lcd_set_sync(uint32_t wait, uint32_t offset)
{
	lcd_sync = wait ? 1u : 0u;
	lcd_sync_offset = offset & 15u;
}

void fxcg100_lcd_set_sync_flip(uint32_t flip)
{
	lcd_sync_flip = flip ? 1u : 0u;
}

void fxcg100_lcd_sync_calibrate(uint32_t on)
{
	/* Count the rows again as the page opens, so that what it shows, and
	 * what the position is set against, is this start's count. */
	if(on && !sync_calibrating)
		scan_state = SCAN_UNKNOWN;
	sync_calibrating = on != 0;
}

int fxcg100_lcd_sync_info(uint32_t *rows, uint32_t *hz)
{
	*rows = scan_state == SCAN_USABLE ? scan_total : 0;
	*hz = scan_state == SCAN_UNKNOWN ? 0 : scan_hz;
	return scan_state == SCAN_USABLE ? 1 : scan_state == SCAN_NONE ? -1 : 0;
}

int fxcg100_lcd_sync_panel(uint32_t *id, uint32_t *mode, int *upward)
{
	*id = panel_id;
	*mode = panel_mode;
	*upward = ((panel_mode & 0x80u) != 0) ^ (lcd_sync_flip != 0);
	return panel_known;
}

/* Start the DMA of one prepared YRAM strip to the LCD window. Falls back to CPU
 * writes if the DMA channel is unavailable. */
static void start_strip_dma(const uint16_t *strip, int x, int y,
	int width, int rows)
{
	unsigned blocks = (unsigned)(width * rows) * sizeof(uint16_t) / 32u;

	sync_strip(y, rows);
	r61524_start_frame(x, x + width - 1, y, y + rows - 1);
	if(dma_transfer_async(0, DMA_32B, blocks, (void *)strip, DMA_INC,
			(void *)LCD_DATA_REGISTER, DMA_FIXED, GINT_CALL_NULL)) {
		lcd_dma_pending = 1;
		return;
	}

	volatile uint16_t *display = (volatile uint16_t *)LCD_DATA_REGISTER;
	for(int i = 0; i < width * rows; i++)
		display[0] = strip[i];
}

/* ---- Scaled presentation --------------------------------------------------
 * Geometry (menu DISPLAY SCALING, fxcg100_lcd_set_scale):
 *   0  1:1 centered 240x160 (below)
 *   1  4:3: 320x212, exact 3->4 groups both axes
 *   2  fullscreen: 384x216, 5->8 horizontal taps, 27/20 vertical map
 * Filter (menu SCALE FILTER, fxcg100_lcd_set_filter): SMOOTH / SHARP / CRISP
 * selects the horizontal row scaler and the vertical tap composition (see
 * fxcg100_scale.h). Scaled rows are composed directly into the XY-RAM strip
 * banks while the previous strip's DMA is in flight, so the filter cost
 * hides under the LCD transfer. Strip heights stay multiples of 4 (R61524
 * DMA window). */

static uint32_t lcd_scale_mode;
static uint32_t lcd_scale_filter;   /* CGBA_SCALE_FILTER_{SMOOTH,SHARP,CRISP} */

/* Erase only the top-right border that can retain the OS BFile icon. In
 * fullscreen the next frame covers the icon itself, so that mode has no extra
 * LCD traffic. The 32-row bound comfortably covers the indicator while keeping
 * the one-shot cleanup far smaller than a full-screen push. */
static void clear_os_overlay_before_blit(void)
{
	int viewport_right;
	int rows = (int)fxcg100_lcd_cleanup_rows(DHEIGHT);
	volatile uint16_t *display = (volatile uint16_t *)LCD_DATA_REGISTER;

	if(!lcd_os_overlay_dirty)
		return;
	lcd_os_overlay_dirty = 0;
	viewport_right = (int)fxcg100_lcd_cleanup_right_start(lcd_scale_mode,
		DWIDTH);
	if(viewport_right >= DWIDTH)
		return;

	wait_lcd_dma();
	r61524_start_frame(viewport_right, DWIDTH - 1, 0, rows - 1);
	for(int i = 0; i < (DWIDTH - viewport_right) * rows; i++)
		display[0] = 0;
	lcd_window_partial = 1;
}

void fxcg100_lcd_set_scale(uint32_t mode)
{
	lcd_scale_mode = (mode <= 2) ? mode : 0;
}

void fxcg100_lcd_set_filter(uint32_t filter)
{
	lcd_scale_filter = (filter <= CGBA_SCALE_FILTER_CRISP) ? filter : 0;
}

typedef void (*cgba_hscale_fn)(const uint16_t *, uint16_t *);

static cgba_hscale_fn hscale_320_for(uint32_t f)
{
	if (f == CGBA_SCALE_FILTER_CRISP) return cgba_scale_row_240_320_crisp;
	if (f == CGBA_SCALE_FILTER_SHARP) return cgba_scale_row_240_320_sharp;
	return cgba_scale_row_240_320;
}

static cgba_hscale_fn hscale_384_for(uint32_t f)
{
	if (f == CGBA_SCALE_FILTER_CRISP) return cgba_scale_row_240_384_crisp;
	if (f == CGBA_SCALE_FILTER_SHARP) return cgba_scale_row_240_384_sharp;
	return cgba_scale_row_240_384;
}

/* One 320px scratch row (4:3 middle row) + two cached hscaled rows with
 * their source indices (fullscreen). Regular RAM: reads/writes stay cached. */
static uint16_t scale_scratch[LCD_W] __attribute__((aligned(4)));
static uint16_t scale_hrow[2][LCD_W] __attribute__((aligned(4)));
static int scale_hrow_idx[2];
static cgba_hscale_fn scale_hrow_fn;   /* selected fullscreen hscaler */

/* 4:3: 53 groups of 3 source rows -> 4 output rows. Horizontal filter via
 * the selected 240->320 scaler; vertical taps {A, .75, 1.5, 2.25} composed
 * per filter (SMOOTH blends r1+r2, SHARP only r2, CRISP neither). 320px
 * wide, centered (ox=32), top-aligned; the window stays 4-row aligned. */
static void blit_gba_scaled_43(const uint16_t *pixels)
{
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	cgba_hscale_fn hs = hscale_320_for(lcd_scale_filter);
	uint32_t filter = lcd_scale_filter;
	int ox = (DWIDTH - 320) / 2;
	int oy = ((DHEIGHT - 212) / 2) & ~3;    /* 4-aligned DMA window */
	int bi = 0, i;
	const int count = (212 + STRIP_43_LINES - 1) / STRIP_43_LINES;
	/* Strips go up the panel when the refresh does (sync_begin). Every
	 * strip starts on a group of four rows, so any order composes the same
	 * rows. */
	int upward = sync_begin(oy, 212, 1);

	wait_lcd_dma();
	for (i = 0; i < count; i++) {
		int y = (upward ? count - 1 - i : i) * STRIP_43_LINES;
		int rows = (212 - y < STRIP_43_LINES) ? 212 - y : STRIP_43_LINES;
		int group = y / 4;
		uint16_t *sp = strips[bi];
		int g;

		for (g = 0; g < rows / 4; g++, group++) {
			const uint16_t *A = pixels + (group * 3 + 0) * GBA_W;
			const uint16_t *B = pixels + (group * 3 + 1) * GBA_W;
			const uint16_t *C = pixels + (group * 3 + 2) * GBA_W;
			uint16_t *r0 = sp + (g * 4 + 0) * 320;
			uint16_t *r1 = sp + (g * 4 + 1) * 320;
			uint16_t *r2 = sp + (g * 4 + 2) * 320;
			uint16_t *r3 = sp + (g * 4 + 3) * 320;

			hs(A, r0);
			hs(B, scale_scratch);
			hs(C, r3);
			if (filter == CGBA_SCALE_FILTER_SMOOTH) {
				cgba_scale_row_avg(r0, scale_scratch, r1, 320);
				cgba_scale_row_avg(scale_scratch, r3, r2, 320);
			} else if (filter == CGBA_SCALE_FILTER_SHARP) {
				memcpy(r1, scale_scratch, 320 * sizeof(uint16_t));
				cgba_scale_row_avg(scale_scratch, r3, r2, 320);
			} else {                               /* CRISP: A,B,C,C */
				memcpy(r1, scale_scratch, 320 * sizeof(uint16_t));
				memcpy(r2, r3, 320 * sizeof(uint16_t));
			}
		}
		wait_lcd_dma();
		start_strip_dma(strips[bi], ox, oy + y, 320, rows);
		bi ^= 1;
	}
	sync_end();
	lcd_window_partial = 1;
}

/* Fullscreen: hscaled source rows are cached in two slots keyed by
 * (source row & 1), so a blend pair (s, s+1) never collides. */
static const uint16_t *fs_hrow(const uint16_t *pixels, unsigned s)
{
	unsigned slot = s & 1;

	if (scale_hrow_idx[slot] != (int)s) {
		scale_hrow_fn(pixels + s * GBA_W, scale_hrow[slot]);
		scale_hrow_idx[slot] = (int)s;
	}
	return scale_hrow[slot];
}

static void blit_gba_scaled_full(const uint16_t *pixels)
{
	static uint16_t vmap[216];
	static int vmap_filter = -1;
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	int ox = (DWIDTH - LCD_W) / 2;
	int oy = ((DHEIGHT - LCD_H) / 2) & ~3;  /* 4-aligned DMA window */
	int bi = 0, i, upward;
	const int count = (LCD_H + STRIP_FULL_LINES - 1) / STRIP_FULL_LINES;

	if (vmap_filter != (int)lcd_scale_filter) {
		cgba_scale_build_vmap216_f(vmap, (int)lcd_scale_filter);
		vmap_filter = (int)lcd_scale_filter;
	}
	scale_hrow_fn = hscale_384_for(lcd_scale_filter);
	scale_hrow_idx[0] = scale_hrow_idx[1] = -1;   /* new frame contents */

	/* Strips go up the panel when the refresh does (sync_begin); the row
	 * cache is keyed by source row, so any order gives the same rows. */
	upward = sync_begin(oy, LCD_H, 2);
	wait_lcd_dma();
	for (i = 0; i < count; i++) {
		uint16_t *sp = strips[bi];
		int y = (upward ? count - 1 - i : i) * STRIP_FULL_LINES;
		int rows = (LCD_H - y < STRIP_FULL_LINES)
			? LCD_H - y : STRIP_FULL_LINES;
		int r;

		for (r = 0; r < rows; r++) {
			uint16_t v = vmap[y + r];
			unsigned src = v & CGBA_SCALE_VROW;
			uint16_t *out = sp + r * LCD_W;

			if (v & CGBA_SCALE_VBLEND)
				cgba_scale_row_avg(fs_hrow(pixels, src),
					fs_hrow(pixels, src + 1), out, LCD_W);
			else
				memcpy(out, fs_hrow(pixels, src),
					LCD_W * sizeof(uint16_t));
		}
		wait_lcd_dma();
		start_strip_dma(strips[bi], ox, oy + y, LCD_W, rows);
		bi ^= 1;
	}
	sync_end();
	lcd_window_partial = 1;
}

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
	int ox = (DWIDTH - GBA_W) / 2;   /* 78 */
	int oy = (DHEIGHT - GBA_H) / 2;  /* 32 (4-aligned) */
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	const int count = (GBA_H + STRIP_LINES - 1) / STRIP_LINES;
	int i, bi = 0, upward;

	if(!pixels)
		return;
	clear_os_overlay_before_blit();
	if(lcd_scale_mode == 1) {
		blit_gba_scaled_43(pixels);
		return;
	}
	if(lcd_scale_mode == 2) {
		blit_gba_scaled_full(pixels);
		return;
	}

	/*
	 * Present the frame as strips DMA'd out of the on-chip banks selected above.
	 * Double-buffered: prepare the next strip while the current DMA runs. With
	 * VSync the frame may first wait, and its strips then go the way the
	 * panel's refresh goes (sync_begin).
	 */
	upward = sync_begin(oy, GBA_H, 0);
	wait_lcd_dma();

	for(i = 0; i < count; i++) {
		int y = (upward ? count - 1 - i : i) * STRIP_LINES;
		int rows = (y + STRIP_LINES > GBA_H) ? (GBA_H - y) : STRIP_LINES;

		memcpy(strips[bi], &pixels[y * GBA_W],
			(size_t)rows * GBA_W * sizeof(uint16_t));
		wait_lcd_dma();   /* wait for the previous strip's DMA */
		start_strip_dma(strips[bi], ox, oy + y, GBA_W, rows);
		bi ^= 1;
	}
	sync_end();

	/*
	 * Do NOT wait for the final strip's DMA here. The whole frame is already
	 * copied into the on-chip strips, so we leave the last DMA in flight and
	 * return -- the next frame's run_frame() then overlaps it (the prizoop
	 * trick). Every consumer that could collide already waits first: the next
	 * blit's leading wait_lcd_dma() before it reuses a strip bank, and the
	 * menu/status path's restore_full_window() before any gint push. The frame
	 * buffer itself is free to be overwritten (the DMA reads on-chip RAM).
	 */
	lcd_window_partial = 1;   /* window narrowed; menu restores it */
}

/* ---- Scanline-streamed unscaled presentation ---------------------------
 * update_scanline() calls us only after a row is architecturally complete.
 * A 16-row group is therefore byte-for-byte the same data that the normal
 * end-of-frame blit copies, but its LCD DMA overlaps emulation of the next
 * group. The final DMA remains in flight under the same ownership contract as
 * fxcg100_lcd_blit_gba(). */
#ifdef CGBA_LCD_SCANLINE_STREAM
static int lcd_stream_active;
static unsigned lcd_stream_next_y;
static unsigned lcd_stream_bank;

int fxcg100_lcd_stream_begin(void)
{
	/* A streamed frame cannot wait for the refresh. */
	if(lcd_scale_mode != 0 || lcd_sync || sync_calibrating)
		return 0;
	wait_lcd_dma();
	clear_os_overlay_before_blit();
	lcd_stream_next_y = 0;
	lcd_stream_bank = 0;
	lcd_stream_active = 1;
	return 1;
}

void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y)
{
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	unsigned rows;
	int ox = (DWIDTH - GBA_W) / 2;
	int oy = (DHEIGHT - GBA_H) / 2;

	if(!lcd_stream_active || !row || y >= GBA_H)
		return;
	if(y + 1 < lcd_stream_next_y + STRIP_LINES && y + 1 != GBA_H)
		return;
	if(y + 1 > lcd_stream_next_y + STRIP_LINES && y + 1 != GBA_H) {
		lcd_stream_active = 0;       /* out-of-order/missing row: fall back */
		return;
	}

	rows = y + 1 - lcd_stream_next_y;
	if(rows == 0 || rows > STRIP_LINES) {
		lcd_stream_active = 0;
		return;
	}
	memcpy(strips[lcd_stream_bank], row - (rows - 1) * GBA_W,
		(size_t)rows * GBA_W * sizeof(uint16_t));
	if(lcd_stream_next_y != 0)
		wait_lcd_dma();
	start_strip_dma(strips[lcd_stream_bank], ox,
		oy + (int)lcd_stream_next_y, GBA_W, (int)rows);
	lcd_stream_bank ^= 1;
	lcd_stream_next_y += rows;
	lcd_window_partial = 1;
}

int fxcg100_lcd_stream_end(void)
{
	int complete = lcd_stream_active && lcd_stream_next_y == GBA_H;
	lcd_stream_active = 0;
	return complete;
}
#else
int fxcg100_lcd_stream_begin(void) { return 0; }
void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y)
{ (void)row; (void)y; }
int fxcg100_lcd_stream_end(void) { return 0; }
#endif
