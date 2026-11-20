/*
 * ClassPad II LCD refresh rate, high-refresh & VRR hardware probe.
 *
 * Verifies runtime RTN (and experimental VFP) tuning discovered in OS 02.01.7003
 * (0x8003887e / [FRAME Freq SET]):
 *   - New panel (ID 0x16 / 0x52, Himax-style): 0xB2 (6 B) + 0xDF (12 B),
 *     RTN in 50..253 (0x32..0xFD), stock 0x9D (~58 Hz nominal, ~56 Hz measured),
 *     VFP in byte 5 (stock 0x08).
 *   - Old panel (R61523): 0xC1 + 0xC3 byte 3 behind MCAP 0xB0 unlock,
 *     RTN in 18..63 (0x12..0x3F), stock 0x1F (~58 Hz nominal, ~60 Hz measured).
 *
 * Always restores stock RTN/VFP before OS switches and on exit.
 */

#include <gint/clock.h>
#include <gint/cpu.h>
#include <gint/display.h>
#include <gint/drivers/touch.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <gint/prof.h>
#include <gint/timer.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "classpad_sync.h"
#include "classpad_touch_guard.h"

HHK_NAME("LCD Hz Probe")
HHK_DESCRIPTION("ClassPad II refresh rate, high-Hz and VRR probe")
HHK_AUTHOR("KaraRyougi")
HHK_VERSION("1.0")

#define PANEL_W 320
#define PANEL_H 528

#define LCD_DATA ((volatile uint16_t *)0xb4000000u)
#define LCD_PRDR ((volatile uint8_t *)0xa405013cu)

/* On-chip X RAM (8 KiB): holds one 320x8 strip (5120 bytes) for fast motion bar. */
#define XRAM ((uint16_t *)0xe5007000u)
#define BAR_Y 392
#define BAR_H 128
#define BAR_STRIP_H 8
#define BAR_STRIPS (BAR_H / BAR_STRIP_H)

#define RGB565(r, g, b) \
	(uint16_t)((((r) & 31u) << 11) | (((g) & 63u) << 5) | ((b) & 31u))

extern int BFile_Open(const char *path, int flags);
extern int BFile_Write(int fd, void const *buf, int size);
extern int BFile_Close(int fd);
extern int BFile_Remove(char const *path);

/* ---- low-level timing & panel bus ----------------------------------------- */

static inline void lcd_synco(void)
{
	__asm__ volatile("synco");
}

static void lcd_delay_ns(uint32_t ns)
{
	/* Conservative 240 MHz upper bound: safe at both stock and overclocked Iphi. */
	uint32_t iters = (240u * ns + 2999u) / 3000u + 1u;

	while(iters--)
		__asm__ volatile("" ::: "memory");
}

static uint32_t prof_us_since(uint32_t start_tcnt)
{
	prof_t p = { 0, start_tcnt - *prof_tcnt };

	return prof_time(p);
}

static uint32_t prof_us_between(uint32_t start_tcnt, uint32_t end_tcnt)
{
	prof_t p = { 0, start_tcnt - end_tcnt };

	return prof_time(p);
}

static void lcd_select(unsigned reg)
{
	*LCD_PRDR &= (uint8_t)~0x10;
	lcd_synco();
	*LCD_DATA = (uint16_t)reg;
	lcd_synco();
	*LCD_PRDR |= 0x10;
	lcd_synco();
}

static inline void lcd_param(unsigned v)
{
	*LCD_DATA = (uint16_t)v;
	lcd_synco();
}

static uint16_t r61523_read_word(void)
{
	uint16_t v;

	lcd_synco();
	v = *LCD_DATA;
	lcd_synco();
	lcd_delay_ns(450);
	return v;
}

/* ---- panel state ---------------------------------------------------------- */

static uint8_t panel_id;
static int is_new_panel;
static int first_column;
static int bottom_up;
static int scan_timed;
static int scan_usable;
static uint32_t scan_total;
static uint32_t scan_hz_x100;

/* Saved R61523 0xC1 / 0xC3 (5 bytes each) for clean restore. */
static int r61523_c1_ok;
static uint8_t saved_c1[5];
static uint8_t saved_c3[5];
static uint8_t stock_rtn;
static uint8_t cur_rtn;
static uint8_t cur_vfp = 0x08u;

/* R61523 MCAP (B0h): 0x00 unlocks all manufacturer commands (B1h..FFh). */
static uint8_t r61523_mcap_unlock_all(void)
{
	uint8_t saved;

	lcd_select(0xb0);
	(void)r61523_read_word();
	saved = cp_r61523_mcap_restore_value(
		(uint8_t)(r61523_read_word() & 0xffu));
	if(saved != 0x00u) {
		lcd_select(0xb0);
		lcd_param(0x00u);
	}
	return saved;
}

static void r61523_mcap_restore(uint8_t saved)
{
	if(saved != 0x00u) {
		lcd_select(0xb0);
		lcd_param(saved);
	}
}

static void lcd_identify(void)
{
	unsigned prev = 0x100u, id = 0;

	lcd_select(0xda);
	(void)r61523_read_word();
	(void)r61523_read_word();
	for(int tries = 0; tries < 8; tries++) {
		lcd_select(0xda);
		(void)r61523_read_word();
		id = r61523_read_word() & 0xffu;
		if(id == prev)
			break;
		prev = id;
	}
	panel_id = (uint8_t)id;
	is_new_panel = (panel_id == 0x16u || panel_id == 0x52u);
	first_column = is_new_panel ? 0 : 40;

	if(is_new_panel) {
		bottom_up = 1;
		stock_rtn = 0x9du;
		cur_rtn = stock_rtn;
		cur_vfp = 0x08u;
	} else {
		uint8_t m1, m2, mcap;

		lcd_select(0x0b);
		(void)r61523_read_word();
		m1 = (uint8_t)(r61523_read_word() & 0xffu);
		lcd_select(0x0b);
		(void)r61523_read_word();
		m2 = (uint8_t)(r61523_read_word() & 0xffu);
		bottom_up = cp_r61523_refresh_bottom_up(m1, m1 == m2);

		mcap = r61523_mcap_unlock_all();
		lcd_select(0xc1);
		(void)r61523_read_word();
		for(int i = 0; i < 5; i++)
			saved_c1[i] = (uint8_t)(r61523_read_word() & 0xffu);
		lcd_select(0xc3);
		(void)r61523_read_word();
		for(int i = 0; i < 5; i++)
			saved_c3[i] = (uint8_t)(r61523_read_word() & 0xffu);
		r61523_mcap_restore(mcap);

		r61523_c1_ok = (saved_c1[2] >= 18u && saved_c1[2] <= 63u);
		stock_rtn = r61523_c1_ok ? saved_c1[2] : 31u;
		cur_rtn = stock_rtn;
	}
}

static void lcd_window(int c0, int c1, int r0, int r1)
{
	c0 += first_column;
	c1 += first_column;
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
	lcd_select(0x2c);
}

static void present_rows(int y0, int h)
{
	volatile uint16_t *lcd = LCD_DATA;
	const uint16_t *p = gint_vram + y0 * PANEL_W;
	int n = h * PANEL_W;

	lcd_window(0, PANEL_W - 1, y0, y0 + h - 1);
	for(int i = 0; i < n; i++)
		*lcd = p[i];
}

static void present_all(void)
{
	present_rows(0, PANEL_H);
}

/* ---- scanline counter & high-precision Hz probe --------------------------- */

static uint32_t lcd_scanline_mode(int timed)
{
	uint32_t hi, lo;

	lcd_select(0x45);
	if(!timed) {
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

static uint32_t scan_position(void)
{
	uint32_t total = scan_total ? scan_total : 548u;
	uint32_t row = lcd_scanline_mode(scan_timed) % total;

	for(int tries = 0; tries < 4; tries++) {
		uint32_t again = lcd_scanline_mode(scan_timed) % total;
		int agree = cp_scan_agree(row, again, total);

		row = again;
		if(agree)
			break;
	}
	return row;
}

typedef struct scan_meas {
	int ok;
	uint32_t total;
	uint32_t hz_x100;
	uint32_t wraps;
	uint32_t samples;
	uint32_t believed;
} scan_meas;

static void measure_scan_mode(int timed, uint32_t duration_ms, scan_meas *out)
{
	cp_scan_probe probe;
	uint32_t t0, first_wrap_t = 0, last_wrap_t = 0, prev_wraps = 0;
	uint32_t limit_us = duration_ms * 1000u;

	memset(&probe, 0, sizeof(probe));
	memset(out, 0, sizeof(*out));
	for(int i = 0; i < 8; i++)
		(void)lcd_scanline_mode(timed);

	t0 = *prof_tcnt;
	for(uint32_t n = 0; n < 4000000u; n++) {
		uint32_t v = lcd_scanline_mode(timed);

		cp_scan_probe_add(&probe, v);
		if(probe.wraps != prev_wraps) {
			uint32_t now = *prof_tcnt;

			if(prev_wraps == 0)
				first_wrap_t = now;
			last_wrap_t = now;
			prev_wraps = probe.wraps;
		}
		if((n & 31u) == 31u && prof_us_since(t0) >= limit_us)
			break;
	}

	out->samples = probe.samples;
	out->believed = probe.believed;
	out->wraps = probe.wraps;

	if(probe.samples >= 500u && probe.backward * 50u <= probe.samples &&
			probe.believed * 2u >= probe.samples &&
			probe.max >= 320u && probe.max <= 1022u &&
			probe.wraps >= 3u) {
		uint32_t span_us = prof_us_between(first_wrap_t, last_wrap_t);

		if(span_us > 10000u) {
			uint32_t hz_x100 = (uint32_t)(
				((uint64_t)(probe.wraps - 1u) * 100000000ull + span_us / 2u)
				/ span_us);
			if(hz_x100 >= 2000u && hz_x100 <= 20000u) {
				out->ok = 1;
				out->total = probe.max + 1u;
				out->hz_x100 = hz_x100;
			}
		}
	}
}

static void init_scan_probe(void)
{
	scan_meas m;
	int primary = !is_new_panel;

	scan_timed = primary;
	measure_scan_mode(primary, 260, &m);
	if(!m.ok) {
		scan_meas alt;

		measure_scan_mode(!primary, 260, &alt);
		if(alt.ok) {
			scan_timed = !primary;
			m = alt;
		}
	}
	scan_usable = m.ok;
	scan_total = m.ok ? m.total : 548u;
	scan_hz_x100 = m.ok ? m.hz_x100 : 0u;
}

static void refresh_scan_meas(uint32_t duration_ms)
{
	scan_meas m;

	if(!scan_usable)
		return;
	measure_scan_mode(scan_timed, duration_ms, &m);
	if(m.ok) {
		scan_total = m.total;
		scan_hz_x100 = m.hz_x100;
	}
}

/* ---- RTN / VFP programming (from OS 02.01.7003 @ 0x8003887e) -------------- */

static void wait_write_phase(int mid_frame)
{
	uint32_t t0;

	if(!scan_usable)
		return;
	t0 = *prof_tcnt;
	for(uint32_t n = 0; n < 200000u; n++) {
		uint32_t at = scan_position();

		if(!mid_frame) {
			if(at >= PANEL_H || at < 4u)
				break;
		} else {
			if(at >= 220u && at < 280u)
				break;
		}
		if((n & 31u) == 31u && prof_us_since(t0) > 30000u)
			break;
	}
}

static void apply_panel_timing(uint8_t rtn, uint8_t vfp, int mid_frame)
{
	if(is_new_panel) {
		if(rtn < 50u) rtn = 50u;
		if(rtn > 253u) rtn = 253u;
		if(vfp < 4u) vfp = 4u;
		if(vfp > 64u) vfp = 64u;

		wait_write_phase(mid_frame);

		/* CMD 0xB2 (6 bytes): 00 34 26 08 <vfp> <rtn> */
		lcd_select(0xb2);
		lcd_param(0x00);
		lcd_param(0x34);
		lcd_param(0x26);
		lcd_param(0x08);
		lcd_param(vfp);
		lcd_param(rtn);

		/* CMD 0xDF (first 12 bytes): 08 00 73 33 2a 77 73 77 73 08 <vfp> <rtn> */
		lcd_select(0xdf);
		lcd_param(0x08);
		lcd_param(0x00);
		lcd_param(0x73);
		lcd_param(0x33);
		lcd_param(0x2a);
		lcd_param(0x77);
		lcd_param(0x73);
		lcd_param(0x77);
		lcd_param(0x73);
		lcd_param(0x08);
		lcd_param(vfp);
		lcd_param(rtn);

		cur_rtn = rtn;
		cur_vfp = vfp;
	} else if(r61523_c1_ok) {
		uint8_t mcap;

		if(rtn < 18u) rtn = 18u;
		if(rtn > 63u) rtn = 63u;

		wait_write_phase(mid_frame);

		mcap = r61523_mcap_unlock_all();
		lcd_select(0xc1);
		lcd_param(saved_c1[0]);
		lcd_param(saved_c1[1]);
		lcd_param(rtn);
		lcd_param(saved_c1[3]);
		lcd_param(saved_c1[4]);

		lcd_select(0xc3);
		lcd_param(saved_c3[0]);
		lcd_param(saved_c3[1]);
		lcd_param(rtn);
		lcd_param(saved_c3[3]);
		lcd_param(saved_c3[4]);
		r61523_mcap_restore(mcap);

		cur_rtn = rtn;
	}
}

static void restore_stock_timing(void)
{
	if(is_new_panel) {
		apply_panel_timing(stock_rtn, 0x08u, 0);
	} else if(r61523_c1_ok) {
		uint8_t mcap = r61523_mcap_unlock_all();

		lcd_select(0xc1);
		for(int i = 0; i < 5; i++)
			lcd_param(saved_c1[i]);
		lcd_select(0xc3);
		for(int i = 0; i < 5; i++)
			lcd_param(saved_c3[i]);
		r61523_mcap_restore(mcap);
		cur_rtn = stock_rtn;
	}
}

/* Nominal Hz estimate for display only (real Hz is measured via 0x45). */
static uint32_t nominal_hz(uint8_t rtn, uint8_t vfp)
{
	if(is_new_panel) {
		uint32_t lines = 528u + 8u + (uint32_t)vfp;
		return 8000000u / (lines * ((uint32_t)rtn + 97u));
	}
	return 1800u / (uint32_t)rtn;
}

/* ---- presets -------------------------------------------------------------- */

typedef struct hz_preset {
	const char *label;
	uint8_t rtn;
} hz_preset;

static const hz_preset presets_new[] = {
	{ "42Hz (OS idle)", 0xfdu },
	{ "50Hz",           0xc5u },
	{ "56Hz",           0xa6u },
	{ "Stock (0x9D)",   0x9du },
	{ "60Hz nom",       0x94u },
	{ "62Hz (~60 real)",0x8cu },
	{ "67Hz",           0x7au },
	{ "72Hz",           0x6bu },
	{ "78Hz",           0x5cu },
	{ "86Hz",           0x4au },
	{ "95Hz",           0x3au },
	{ "100Hz max",      0x32u },
};

static const hz_preset presets_old[] = {
	{ "40Hz (OS idle)", 45u },
	{ "50Hz",           36u },
	{ "Stock (0x1F)",   31u },
	{ "60Hz nom",       30u },
	{ "64Hz",           28u },
	{ "72Hz",           25u },
	{ "82Hz",           22u },
	{ "100Hz max",      18u },
};

static const hz_preset *get_presets(int *count)
{
	if(is_new_panel) {
		*count = (int)(sizeof(presets_new) / sizeof(presets_new[0]));
		return presets_new;
	}
	*count = (int)(sizeof(presets_old) / sizeof(presets_old[0]));
	return presets_old;
}

static int find_nearest_preset(uint8_t rtn)
{
	int n, best = 0;
	unsigned best_d = 999u;
	const hz_preset *p = get_presets(&n);

	for(int i = 0; i < n; i++) {
		unsigned d = rtn > p[i].rtn ? (unsigned)(rtn - p[i].rtn)
			: (unsigned)(p[i].rtn - rtn);
		if(d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

/* ---- visual patterns & fast XRAM motion bar ------------------------------- */

static void draw_static_patterns(void)
{
	/* Background for pattern band (y = 176..391). */
	drect(0, 176, PANEL_W - 1, BAR_Y - 1, C_BLACK);

	/* 1) 50% mid-gray patch: most sensitive to VRR flicker / VCOM shift. */
	drect(8, 182, 155, 250, C_RGB(16, 16, 16));
	drect_border(8, 182, 155, 250, C_NONE, 1, C_RGB(24, 24, 24));
	dtext(14, 186, C_BLACK, "50% GRAY");
	dtext(14, 198, C_BLACK, "(flicker)");

	/* 2) 1x1 pixel checkerboard: exposes inversion / pixel charge deficit. */
	for(int y = 182; y <= 250; y++) {
		uint16_t *row = gint_vram + y * PANEL_W;
		for(int x = 164; x <= 311; x++)
			row[x] = ((x ^ y) & 1) ? 0xffffu : 0x0000u;
	}
	drect_border(164, 182, 311, 250, C_NONE, 1, C_RGB(24, 24, 24));

	/* 3) 1px horizontal stripes (left) & 1px vertical stripes (right). */
	for(int y = 258; y <= 312; y++) {
		uint16_t *row = gint_vram + y * PANEL_W;
		uint16_t hc = (y & 1) ? 0xffffu : 0x0000u;
		for(int x = 8; x <= 155; x++)
			row[x] = hc;
		for(int x = 164; x <= 311; x++)
			row[x] = (x & 1) ? 0xffffu : 0x0000u;
	}
	drect_border(8, 258, 155, 312, C_NONE, 1, C_RGB(20, 20, 20));
	drect_border(164, 258, 311, 312, C_NONE, 1, C_RGB(20, 20, 20));

	/* 4) Smooth R / G / B / Gray gradients (exposes gamma/contrast washout). */
	for(int x = 8; x <= 311; x++) {
		unsigned v5 = (unsigned)(x - 8) * 31u / 303u;
		unsigned v6 = (unsigned)(x - 8) * 63u / 303u;
		uint16_t cr = RGB565(v5, 0, 0);
		uint16_t cg = RGB565(0, v6, 0);
		uint16_t cb = RGB565(0, 0, v5);
		uint16_t cw = RGB565(v5, v6, v5);

		for(int y = 320; y < 335; y++) gint_vram[y * PANEL_W + x] = cr;
		for(int y = 336; y < 351; y++) gint_vram[y * PANEL_W + x] = cg;
		for(int y = 352; y < 367; y++) gint_vram[y * PANEL_W + x] = cb;
		for(int y = 368; y < 383; y++) gint_vram[y * PANEL_W + x] = cw;
	}
	present_rows(176, BAR_Y - 176);
}

static uint32_t bar_lines = 120u;

static void draw_motion_bar(int bar_x, int bar2_x, int use_vsync)
{
	uint32_t total = scan_total ? scan_total : 548u;
	uint32_t first = bottom_up ? (uint32_t)(PANEL_H - BAR_Y - BAR_H)
		: (uint32_t)BAR_Y;
	uint32_t lo = 0, hi = 0;
	cp_sync_track track;

	if(use_vsync && scan_usable && cp_sync_window_part(total, BAR_H, bar_lines,
			0, CP_SYNC_PART_GAME, bottom_up ? BAR_STRIP_H : 0u, &lo, &hi)) {
		/* Narrow start slot [lo, lo+24) locks 1:1 to panel refresh so 56/60/95 Hz
		 * motion cadence is directly visible without 2-frames-per-refresh. */
		uint32_t slot_hi = lo + 24u;
		uint32_t t0 = *prof_tcnt;

		if(slot_hi > hi)
			slot_hi = hi;
		for(uint32_t n = 0; n < 300000u; n++) {
			uint32_t at = (scan_position() + total - first) % total;

			if(at >= lo && at < slot_hi)
				break;
			if((n & 31u) == 31u && prof_us_since(t0) > 40000u)
				break;
		}
	}

	cp_sync_track_begin(&track, total);

	/* Render one 8-row strip template into XRAM, then push 16 strips in refresh order. */
	for(int s = 0; s < BAR_STRIPS; s++) {
		int idx = bottom_up ? (BAR_STRIPS - 1 - s) : s;
		int y0 = BAR_Y + idx * BAR_STRIP_H;
		volatile uint16_t *lcd = LCD_DATA;
		uint16_t *dst = XRAM;

		if(scan_usable) {
			uint32_t row = bottom_up
				? (uint32_t)(PANEL_H - y0 - BAR_STRIP_H) : (uint32_t)y0;
			cp_sync_track_strip(&track, scan_position(), row);
		}

		for(int r = 0; r < BAR_STRIP_H; r++) {
			int y = idx * BAR_STRIP_H + r;
			int border = (y == 0 || y == BAR_H - 1);
			int mid = (y == BAR_H / 2);

			for(int x = 0; x < PANEL_W; x++) {
				uint16_t c;

				if(border) {
					c = RGB565(12, 24, 14);
				} else if(x >= bar_x && x < bar_x + 24) {
					c = (x < bar_x + 4 || x >= bar_x + 20)
						? RGB565(0, 63, 31) : 0xffffu;
				} else if(x >= bar2_x && x < bar2_x + 12) {
					c = RGB565(31, 10, 6);
				} else if(mid || (x & 31) == 0) {
					c = RGB565(6, 12, 8);
				} else {
					c = RGB565(2, 4, 3);
				}
				*dst++ = c;
			}
		}

		lcd_window(0, PANEL_W - 1, y0, y0 + BAR_STRIP_H - 1);
		const uint16_t *src = XRAM;
		for(int i = 0; i < PANEL_W * BAR_STRIP_H; i += 8, src += 8) {
			*lcd = src[0]; *lcd = src[1]; *lcd = src[2]; *lcd = src[3];
			*lcd = src[4]; *lcd = src[5]; *lcd = src[6]; *lcd = src[7];
		}
	}

	if(scan_usable) {
		uint32_t measured = cp_sync_track_lines(&track);

		if(measured > 0 && measured < total * 2u)
			bar_lines = measured;
	}
}

/* ---- UI header ------------------------------------------------------------ */

static int vrr_mode;       /* 0=Off, 1=45..65Hz, 2=45..95Hz */
static int write_mid;      /* 0=VBlank write, 1=Mid-frame write */
static int bar_vsync = 1;  /* 1=VSync 1:1 locked, 0=uncapped */
static uint32_t bar_fps_x10;

static void draw_header(void)
{
	char buf[64];
	static const char *const vrr_names[3] = { "OFF", "45-65Hz", "45-95Hz" };
	uint32_t nom = nominal_hz(cur_rtn, cur_vfp);

	drect(0, 0, PANEL_W - 1, 175, C_BLACK);
	snprintf(buf, sizeof(buf), "LCD Hz Probe  ID=%02X (%s)",
		(unsigned)panel_id, is_new_panel ? "0x16/52" : "R61523");
	dtext(6, 4, C_RGB(0, 31, 31), buf);

	snprintf(buf, sizeof(buf), "RTN=0x%02X (%3u)  VFP=%2u  Nom~%lu Hz",
		(unsigned)cur_rtn, (unsigned)cur_rtn, (unsigned)cur_vfp,
		(unsigned long)nom);
	dtext(6, 20, C_WHITE, buf);

	if(scan_usable) {
		snprintf(buf, sizeof(buf), "Meas: %lu.%02lu Hz  (%lu rows)  %s",
			(unsigned long)(scan_hz_x100 / 100u),
			(unsigned long)(scan_hz_x100 % 100u),
			(unsigned long)scan_total,
			bottom_up ? "bot-up" : "top-dn");
		dtext(6, 36, C_RGB(0, 31, 0), buf);
	} else {
		dtext(6, 36, C_RGB(31, 16, 0), "Meas: 0x45 scanline unavailable");
	}

	snprintf(buf, sizeof(buf), "Bar: %lu.%lu fps  VSync:%s  Wr:%s",
		(unsigned long)(bar_fps_x10 / 10u),
		(unsigned long)(bar_fps_x10 % 10u),
		bar_vsync ? "ON" : "OFF",
		write_mid ? "MID" : "VBL");
	dtext(6, 52, C_RGB(31, 31, 0), buf);

	snprintf(buf, sizeof(buf), "VRR Sweep: %s  (scan_t=%d)",
		vrr_names[vrr_mode], scan_timed);
	dtext(6, 68, vrr_mode ? C_RGB(31, 12, 31) : C_WHITE, buf);

	dline(0, 84, PANEL_W - 1, 84, C_RGB(10, 10, 10));
	dtext(6, 90,  C_RGB(22, 22, 22), "Up/Dn: Preset   L/R: Hz -/+");
	dtext(6, 104, C_RGB(22, 22, 22), "(/): VFP -/+4   0: Reset stock");
	dtext(6, 118, C_RGB(22, 22, 22), "x: VRR sweep    /: Wr VBL/MID");
	dtext(6, 132, C_RGB(22, 22, 22), "+: Bar VSync    -: Auto sweep+log");
	dtext(6, 146, C_RGB(31, 31, 0),  "EXE / Clear: Restore stock & exit");
	dline(0, 166, PANEL_W - 1, 166, C_RGB(10, 10, 10));

	present_rows(0, 176);
}

/* ---- auto sweep benchmark & file log -------------------------------------- */

#define LOG_MAX 48
#define LOG_LEN 64
static char log_lines[LOG_MAX][LOG_LEN];
static int log_count;

static void log_add(const char *fmt, ...)
{
	va_list ap;

	if(log_count >= LOG_MAX)
		return;
	va_start(ap, fmt);
	vsnprintf(log_lines[log_count++], LOG_LEN, fmt, ap);
	va_end(ap);
}

static int save_log_os(void)
{
	static const char path[] = "\\fls0\\HZPROBE.TXT";
	int fd, ok = 1;

	BFile_Remove(path);
	fd = BFile_Open(path, 0x02 | 0x04);
	if(fd < 0)
		return 0;
	for(int i = 0; i < log_count && ok; i++) {
		int len = (int)strlen(log_lines[i]);

		ok = BFile_Write(fd, log_lines[i], len) == len &&
			BFile_Write(fd, "\r\n", 2) == 2;
	}
	return BFile_Close(fd) >= 0 && ok;
}

static int touched(void)
{
	struct _touch_addots dots;

	cpu_atomic_start();
	touch_get_last_scan(NULL, NULL, &dots);
	cpu_atomic_end();
	return dots.touches >= 1;
}

static void wait_untouched(void)
{
	int quiet = 0;

	while(quiet < 30) {
		clearevents();
		if(touched())
			quiet = 0;
		else
			quiet++;
		for(volatile int i = 0; i < 15000; i++)
			;
	}
}

static void run_auto_sweep(void)
{
	int count;
	const hz_preset *presets = get_presets(&count);
	const clock_frequency_t *freq = clock_freq();
	uint8_t prev_rtn = cur_rtn;
	uint8_t prev_vfp = cur_vfp;

	log_count = 0;
	log_add("ClassPad II LCD Hz Probe");
	log_add("ID=0x%02X (%s) bottom_up=%d timed=%d",
		(unsigned)panel_id, is_new_panel ? "0x16/52" : "R61523",
		bottom_up, scan_timed);
	log_add("I=%d B=%d P=%d kHz  stock_rtn=0x%02X",
		freq->Iphi_f / 1000, freq->Bphi_f / 1000, freq->Pphi_f / 1000,
		(unsigned)stock_rtn);
	if(!is_new_panel) {
		log_add("R61523 C1=%02X %02X %02X %02X %02X ok=%d",
			saved_c1[0], saved_c1[1], saved_c1[2], saved_c1[3], saved_c1[4],
			r61523_c1_ok);
	}
	log_add("----------------------------------------");
	log_add("Preset            RTN  VFP  Rows   Meas Hz");

	dclear(C_BLACK);
	dtext(8, 8, C_RGB(0, 31, 31), "Running Auto Sweep...");
	dtext(8, 28, C_WHITE, "Measuring all presets via 0x45...");
	present_all();

	for(int i = 0; i < count; i++) {
		scan_meas m;

		apply_panel_timing(presets[i].rtn, 0x08u, 0);
		/* Let two refreshes settle before measuring. */
		for(volatile int w = 0; w < 200000; w++)
			;
		measure_scan_mode(scan_timed, 240, &m);
		if(m.ok) {
			log_add("%-16s 0x%02X  %2u   %4lu  %3lu.%02lu Hz",
				presets[i].label, (unsigned)presets[i].rtn, 8u,
				(unsigned long)m.total,
				(unsigned long)(m.hz_x100 / 100u),
				(unsigned long)(m.hz_x100 % 100u));
		} else {
			log_add("%-16s 0x%02X  %2u   FAIL (%lu/%lu)",
				presets[i].label, (unsigned)presets[i].rtn, 8u,
				(unsigned long)m.believed, (unsigned long)m.samples);
		}
	}

	/* Experimental VFP porch probe on new panel at stock RTN. */
	if(is_new_panel) {
		static const uint8_t vfps[2] = { 16u, 32u };

		for(int i = 0; i < 2; i++) {
			scan_meas m;
			char lbl[20];

			snprintf(lbl, sizeof(lbl), "VFP=%u @0x9D", (unsigned)vfps[i]);
			apply_panel_timing(stock_rtn, vfps[i], 0);
			for(volatile int w = 0; w < 200000; w++)
				;
			measure_scan_mode(scan_timed, 240, &m);
			if(m.ok) {
				log_add("%-16s 0x%02X  %2u   %4lu  %3lu.%02lu Hz",
					lbl, (unsigned)stock_rtn, (unsigned)vfps[i],
					(unsigned long)m.total,
					(unsigned long)(m.hz_x100 / 100u),
					(unsigned long)(m.hz_x100 % 100u));
			} else {
				log_add("%-16s 0x%02X  %2u   FAIL", lbl,
					(unsigned)stock_rtn, (unsigned)vfps[i]);
			}
		}
	}

	/* Restore stock before displaying and optional OS save. */
	restore_stock_timing();
	refresh_scan_meas(220);

	dclear(C_BLACK);
	for(int i = 0; i < log_count && i < 38; i++)
		dtext(4, 4 + i * 13, i < 6 ? C_RGB(0, 31, 31) : C_WHITE, log_lines[i]);
	dtext(4, PANEL_H - 28, C_RGB(31, 31, 0), "EXE: Save \\fls0\\HZPROBE.TXT");
	dtext(4, PANEL_H - 14, C_RGB(22, 22, 22), "Other key: Back to live view");
	present_all();

	/* Wait for key release first. */
	while(keydown(KEY_SUB))
		clearevents();

	key_event_t ev = getkey();
	if(ev.key == KEY_EXE) {
		wait_untouched();
		int saved = gint_world_switch(GINT_CALL(save_log_os));
		drect(0, PANEL_H - 30, PANEL_W - 1, PANEL_H - 1, C_BLACK);
		dtext(4, PANEL_H - 22, saved ? C_RGB(0, 31, 0) : C_RGB(31, 0, 0),
			saved ? "Saved HZPROBE.TXT! Press any key"
			: "Save FAILED. Press any key");
		present_rows(PANEL_H - 30, 30);
		(void)getkey();
	}

	/* Restore whatever setting the user had before auto sweep. */
	apply_panel_timing(prev_rtn, prev_vfp, 0);
	refresh_scan_meas(220);
	draw_header();
	draw_static_patterns();
}

/* ---- touch guard timer ---------------------------------------------------- */

static int guard_tick(void)
{
	cp_touch_guard_tick();
	return TIMER_CONTINUE;
}

/* ---- key edge & repeat helper --------------------------------------------- */

enum {
	ACT_UP = 0, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
	ACT_VRR, ACT_WRMODE, ACT_VSYNC, ACT_SWEEP,
	ACT_VFP_DN, ACT_VFP_UP, ACT_RESET, ACT_EXIT,
	ACT_COUNT
};

static uint32_t poll_actions(void)
{
	static const uint16_t keys[ACT_COUNT] = {
		KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
		KEY_X, KEY_DIV, KEY_ADD, KEY_SUB,
		KEY_LEFTP, KEY_RIGHTP, KEY_0, KEY_EXE
	};
	static uint32_t prev_mask;
	static uint32_t hold_ticks[ACT_COUNT];
	uint32_t cur = 0, edge = 0;

	clearevents();
	for(int i = 0; i < ACT_COUNT; i++) {
		int down = keydown(keys[i]);
		if(i == ACT_EXIT && keydown(KEY_CLEAR))
			down = 1;
		if(down) {
			cur |= (1u << i);
			if(!(prev_mask & (1u << i))) {
				edge |= (1u << i);
				hold_ticks[i] = 0;
			} else if(i == ACT_LEFT || i == ACT_RIGHT) {
				hold_ticks[i]++;
				if(hold_ticks[i] > 16u && (hold_ticks[i] & 3u) == 0)
					edge |= (1u << i);
			}
		} else {
			hold_ticks[i] = 0;
		}
	}
	prev_mask = cur;
	return edge;
}

/* ---- main ----------------------------------------------------------------- */

int main(void)
{
	int guard_timer;
	int bar_x = 0, bar2_x = PANEL_W - 12;
	uint32_t fps_t0, fps_frames = 0;
	uint32_t vrr_step_div = 0;
	int vrr_dir = -1; /* -1 = decreasing RTN (higher Hz) */

	prof_init();
	lcd_identify();

	cp_touch_guard_install();
	guard_timer = timer_configure(TIMER_TMU, 1000000 / 128,
		GINT_CALL(guard_tick));
	if(guard_timer >= 0)
		timer_start(guard_timer);

	dclear(C_BLACK);
	dtext(8, 8, C_RGB(0, 31, 31), "LCD Hz Probe");
	dtext(8, 28, C_WHITE, "Probing 0x45 scanline counter...");
	present_all();

	init_scan_probe();
	draw_header();
	draw_static_patterns();

	fps_t0 = *prof_tcnt;

	for(;;) {
		uint32_t act = poll_actions();
		int changed_timing = 0, changed_ui = 0;

		if(act & (1u << ACT_EXIT))
			break;

		if(act & (1u << ACT_SWEEP)) {
			vrr_mode = 0;
			run_auto_sweep();
			fps_t0 = *prof_tcnt;
			fps_frames = 0;
			continue;
		}

		if(act & (1u << ACT_RESET)) {
			vrr_mode = 0;
			restore_stock_timing();
			changed_timing = 1;
		}

		if(act & (1u << ACT_UP)) {
			int count, idx;
			const hz_preset *p = get_presets(&count);

			vrr_mode = 0;
			idx = find_nearest_preset(cur_rtn);
			if(idx + 1 < count)
				idx++;
			apply_panel_timing(p[idx].rtn, cur_vfp, write_mid);
			changed_timing = 1;
		}
		if(act & (1u << ACT_DOWN)) {
			int count, idx;
			const hz_preset *p = get_presets(&count);

			vrr_mode = 0;
			idx = find_nearest_preset(cur_rtn);
			if(idx > 0)
				idx--;
			apply_panel_timing(p[idx].rtn, cur_vfp, write_mid);
			changed_timing = 1;
		}
		if(act & (1u << ACT_RIGHT)) {
			/* Decreasing RTN raises refresh rate. */
			vrr_mode = 0;
			if(cur_rtn > (is_new_panel ? 50u : 18u)) {
				apply_panel_timing((uint8_t)(cur_rtn - 1u), cur_vfp, write_mid);
				changed_timing = 1;
			}
		}
		if(act & (1u << ACT_LEFT)) {
			/* Increasing RTN lowers refresh rate. */
			vrr_mode = 0;
			if(cur_rtn < (is_new_panel ? 253u : 63u)) {
				apply_panel_timing((uint8_t)(cur_rtn + 1u), cur_vfp, write_mid);
				changed_timing = 1;
			}
		}
		if(is_new_panel && (act & (1u << ACT_VFP_UP))) {
			vrr_mode = 0;
			uint8_t nv = cur_vfp + 4u <= 64u ? (uint8_t)(cur_vfp + 4u) : 64u;
			apply_panel_timing(cur_rtn, nv, write_mid);
			changed_timing = 1;
		}
		if(is_new_panel && (act & (1u << ACT_VFP_DN))) {
			vrr_mode = 0;
			uint8_t nv = cur_vfp >= 8u ? (uint8_t)(cur_vfp - 4u) : 4u;
			apply_panel_timing(cur_rtn, nv, write_mid);
			changed_timing = 1;
		}
		if(act & (1u << ACT_VRR)) {
			vrr_mode = (vrr_mode + 1) % 3;
			changed_ui = 1;
		}
		if(act & (1u << ACT_WRMODE)) {
			write_mid = !write_mid;
			changed_ui = 1;
		}
		if(act & (1u << ACT_VSYNC)) {
			bar_vsync = !bar_vsync;
			changed_ui = 1;
		}

		if(changed_timing) {
			refresh_scan_meas(220);
			draw_header();
			fps_t0 = *prof_tcnt;
			fps_frames = 0;
		} else if(changed_ui) {
			draw_header();
		}

		/* Continuous VRR sweep: smoothly ramp RTN back and forth. */
		if(vrr_mode) {
			vrr_step_div++;
			if(vrr_step_div >= (is_new_panel ? 2u : 8u)) {
				uint8_t lo_rtn, hi_rtn;

				vrr_step_div = 0;
				if(is_new_panel) {
					hi_rtn = 0xd8u; /* ~46 Hz */
					lo_rtn = (vrr_mode == 1) ? 0x80u : 0x3au; /* ~64 / ~92 Hz */
				} else {
					hi_rtn = 40u;   /* ~45 Hz */
					lo_rtn = (vrr_mode == 1) ? 28u : 19u;     /* ~64 / ~95 Hz */
				}
				int next = (int)cur_rtn + vrr_dir;
				if(next <= (int)lo_rtn) {
					next = (int)lo_rtn;
					vrr_dir = 1;
				} else if(next >= (int)hi_rtn) {
					next = (int)hi_rtn;
					vrr_dir = -1;
				}
				apply_panel_timing((uint8_t)next, cur_vfp, write_mid);
			}
		}

		draw_motion_bar(bar_x, bar2_x, bar_vsync);
		bar_x += 4;
		if(bar_x >= PANEL_W - 24)
			bar_x = 0;
		bar2_x -= 2;
		if(bar2_x < 0)
			bar2_x = PANEL_W - 12;

		fps_frames++;
		uint32_t dt_us = prof_us_since(fps_t0);
		if(dt_us >= 500000u) {
			bar_fps_x10 = (uint32_t)(
				((uint64_t)fps_frames * 10000000ull + dt_us / 2u) / dt_us);
			fps_t0 = *prof_tcnt;
			fps_frames = 0;
			draw_header();
		}
	}

	/* Always restore stock refresh rate before returning to HollyHock-3 / OS. */
	restore_stock_timing();
	if(guard_timer >= 0)
		timer_stop(guard_timer);
	prof_quit();
	return 1;
}
