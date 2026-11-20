#ifndef CGBA_CLASSPAD_SYNC_H
#define CGBA_CLASSPAD_SYNC_H

/* Keeping the picture from tearing on the ClassPad II.
 *
 * The panel shows its own memory, one row after the other, about sixty times
 * a second, whatever is being written to it. A frame is written row by row
 * too, in strips. If the row being shown passes the row being written (or
 * the other way round) while a frame is going out, the part of the screen
 * past that point still shows the frame before: a tear.
 *
 * The controller can be asked which row it is showing (get_scanline, 0x45).
 * With that, and with how many rows it shows while one frame is written
 * (measured on the frame before), a frame can be started at a moment from
 * which the two do not meet. That is all this header works out; reading the
 * panel is in classpad_platform.c.
 *
 * Positions are rows from the top of the picture's area on the panel, modulo
 * the number of rows in one refresh (more than the panel has: the blanking
 * rows count too). Where the blanking rows are is not known, so a margin is
 * kept on both sides. */

#include <stdint.h>

#include "lcd_sync_common.h"

#define CP_SYNC_MARGIN 24u

/* The counter is read as two bytes while it runs, and nothing says the
 * controller holds it still between them: a read across a carry can come
 * out a few hundred rows wrong, enough to start a frame at the wrong moment
 * or to make one look torn. So a row is believed only when two reads in a
 * row agree: the second at the first or a few rows after it. */
#define CP_SCAN_STEP 12u

/* ---- is there a usable row counter? --------------------------------------- */

/* The rows in a refresh are the highest row seen plus one, so one wrong
 * read above the real last row is enough to get it wrong, and with it
 * where every offset falls: it sets the sixteenths the offset counts in and
 * the length positions are taken modulo. (A ClassPad II with panel ID 0x16
 * has 548 rows a refresh, 528 of picture and 20 of blanking, at 56 Hz;
 * when every read counted it showed 856 rows at 112 Hz at one start and
 * 924 at 92 Hz at another, and the right offset moved with them.) So
 * only rows that the read after agrees with count, for the highest row and
 * for the refreshes counted: a misread (the first read after pixels were
 * written is often one, and so is a read across a carry) is followed by a
 * read that does not agree with it. */
typedef struct cp_scan_probe {
	uint32_t samples;
	uint32_t believed;   /* reads the one after agreed with */
	uint32_t max;        /* the highest row believed */
	uint32_t prev;       /* the read before, believed or not */
	uint32_t first;      /* the first row believed */
	uint32_t last;       /* the last row believed */
	uint32_t wraps;      /* believed rows went back to the start: a refresh */
	uint32_t backward;   /* a read fell back a little: not a counter */
} cp_scan_probe;

static inline void cp_scan_probe_add(cp_scan_probe *p, uint32_t value)
{
	if(p->samples) {
		if(value >= p->prev && value <= p->prev + CP_SCAN_STEP) {
			if(!p->believed)
				p->first = value;
			else if(value + 64u < p->last)
				p->wraps++;
			if(value > p->max)
				p->max = value;
			p->last = value;
			p->believed++;
		} else if(value < p->prev && value + 64u >= p->prev) {
			p->backward++;
		}
	}
	p->prev = value;
	p->samples++;
}

/* Fractional refresh rate in millihertz from a completed probe over `ticks`
 * 128ths of a second and detected `total` rows per refresh. Includes the
 * partial refresh (last - first) for ~0.05 Hz precision over a 250 ms probe. */
static inline uint32_t cp_scan_probe_mhz(const cp_scan_probe *p,
	uint32_t ticks, uint32_t total)
{
	int64_t rows;

	if(!ticks || !total || !p->believed)
		return 0u;
	rows = (int64_t)p->wraps * (int64_t)total + (int64_t)p->last -
		(int64_t)p->first;
	if(rows <= 0)
		return 0u;
	return (uint32_t)(((uint64_t)rows * 128000ull +
		((uint64_t)ticks * total) / 2u) / ((uint64_t)ticks * total));
}

/* After sampling for `ticks` 128ths of a second. Returns the rows in one
 * refresh, or 0 if the values were not those of a row counter; *hz is the
 * refresh rate either way. */
static inline uint32_t cp_scan_probe_result(const cp_scan_probe *p,
	uint32_t ticks, uint32_t *hz)
{
	uint32_t rate = ticks ? p->wraps * 128u / ticks : 0;

	*hz = rate;
	if(p->samples < 1000u || p->backward * 50u > p->samples)
		return 0;
	/* A counter read faster than it moves agrees with itself nearly every
	 * time; noise hardly ever does. */
	if(p->believed * 2u < p->samples)
		return 0;
	if(rate < 20u || rate > 200u)
		return 0;
	if(p->max < 320u || p->max > 1022u)
		return 0;
	return p->max + 1u;
}

/* ---- reading the counter -------------------------------------------------- */

static inline int cp_scan_agree(uint32_t first, uint32_t second,
	uint32_t total)
{
	return (second % total + total - first % total) % total <= CP_SCAN_STEP;
}

/* ---- when to start a frame ------------------------------------------------ */

/* The positions [*lo, *hi) of the row being shown from which a frame can be
 * started without the two meeting. `rows` is the height of the picture on
 * the panel, `lines` the rows shown while it is written, `descending`
 * nonzero if the strips go up the panel (the picture held upside down).
 * Returns 0 if there is no such position: the frame takes too long.
 *
 * Not meeting is not enough: strips go out in bursts and the estimate of
 * `lines` is a frame old, so a clearance is kept at both ends of the frame.
 * An eighth of a refresh where there is room, a third of what room there is
 * otherwise. More than that only makes more frames wait, and longer: with
 * a quarter, nine interface frames in ten waited on a ClassPad II, and the
 * motion became uneven.
 *
 * `part` is that share of a refresh: 8 for an eighth. The game's picture is
 * given a sixth (cp_sync_window_game): on an fx-CG500 with an eighth, a
 * picture that changed colour every frame still tore for a few seconds
 * every ten or so, always at the same rows, while the phase of the game's
 * frames against the panel's drifted past an end of the window.
 *
 * `strip` is the height of one strip, 0 if not known. Each strip goes into
 * its window row by row from its top, so, when the strips themselves go the
 * way of the refresh, each one is written against it: its top row as it
 * starts and its bottom row, the one the refresh comes to first, a strip's
 * time later. Ahead of the refresh, the writing reaches a strip further
 * than its start; behind it, the strip's first row is a strip's time late.
 * Both are kept clear on top of the clearance. Left out, they cost the
 * calibration page (16-row bands, 34 rows a step on a 548-row refresh)
 * every clean step for over a third of the places the picture could
 * start. */
static inline int cp_sync_window_part(uint32_t total, uint32_t rows,
	uint32_t lines, int descending, uint32_t part, uint32_t strip,
	uint32_t *lo, uint32_t *hi)
{
	uint32_t taken, clear;

	/* Rows of the refresh that the gap between the two sweeps through
	 * during the frame, and so cannot be had as clearance. */
	if(descending)
		taken = rows + lines;       /* going against it: outside the area */
	else
		taken = lines > rows ? lines - rows : rows - lines;
	if(taken + 3u * CP_SYNC_MARGIN > total)
		return 0;
	clear = (total - taken) / 3u;
	if(clear > total / part)
		clear = total / part;

	if(descending) {
		*lo = rows + clear;
		*hi = total - lines - clear;
	} else {
		/* Writing faster than it shows: it must be far enough ahead not
		 * to be caught. Slower: it must not come round and catch up. */
		*lo = (rows > lines ? rows - lines : 0) + clear;
		*hi = total - (lines > rows ? lines - rows : 0) - clear;
		if(strip && rows) {
			uint32_t late = (lines * strip + rows - 1u) / rows;

			if(*hi <= late)
				return 0;
			*lo += strip;
			*hi -= late;
		}
	}
	return *lo < *hi;
}

static inline int cp_sync_window_part_range(uint32_t total, uint32_t rows,
	uint32_t lines_min, uint32_t lines_max, int descending, uint32_t part,
	uint32_t strip, uint32_t *lo, uint32_t *hi)
{
	uint32_t lo1, hi1, lo2, hi2;

	if(lines_max < lines_min)
		lines_max = lines_min;
	if(!cp_sync_window_part(total, rows, lines_min, descending, part, strip,
			&lo1, &hi1))
		return 0;
	if(lines_max == lines_min) {
		*lo = lo1;
		*hi = hi1;
		return 1;
	}
	if(!cp_sync_window_part(total, rows, lines_max, descending, part, strip,
			&lo2, &hi2))
		return 0;
	*lo = lo1 > lo2 ? lo1 : lo2;
	*hi = hi1 < hi2 ? hi1 : hi2;
	return *lo < *hi;
}

/* The share kept clear: by the interface, by the game, and while the offset
 * is being set by eye, when what is wanted is the opposite of safety: an
 * offset one step out (a sixteenth of a refresh) should already tear.
 *
 * While setting it, a 24th (~23 rows at 555): slightly above half a step
 * (~17 rows) so a true start falling midway between two positions still
 * survives the 4-row polling slot and minor jitter without tearing, while
 * remaining well below a full step (~34 rows) so an offset one step out
 * always tears. */
#define CP_SYNC_PART 8u
#define CP_SYNC_PART_GAME 6u
#define CP_SYNC_PART_CALIBRATE 24u

/* While the offset is being set, a frame does not start wherever it finds
 * the window but sweeps across [lo, hi) in CP_SYNC_CALIBRATE_STEPS narrow
 * slots, visited in low-discrepancy order (early end, late end, middle,
 * quarters, ...). Testing only the early end leaves offsets too high clean
 * until they exhaust the window; testing only the two ends leaves an
 * opposite-side false-clean band whenever the forbidden zone is narrower
 * than half a refresh (for example positions 4..12 on a 555-row R61523),
 * because both ends clear the forbidden zone while the middle of the window
 * straddles it. Sweeping the whole window makes every wrong offset tear.
 *
 * Each slot is a few rows wide so that polling cannot step over it. */
#define CP_SYNC_END_ROWS 4u
#define CP_SYNC_CALIBRATE_STEPS 16u

static inline void cp_sync_window_probe(uint32_t *lo, uint32_t *hi,
	uint32_t step)
{
	static const uint8_t order[CP_SYNC_CALIBRATE_STEPS] = {
		0, 15, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7
	};
	uint32_t span, start;

	if(*hi - *lo <= CP_SYNC_END_ROWS)
		return;
	span = *hi - *lo - CP_SYNC_END_ROWS;
	start = *lo + span * order[step % CP_SYNC_CALIBRATE_STEPS]
		/ (CP_SYNC_CALIBRATE_STEPS - 1u);
	*lo = start;
	*hi = start + CP_SYNC_END_ROWS;
}

static inline void cp_sync_window_end(uint32_t *lo, uint32_t *hi, int late)
{
	cp_sync_window_probe(lo, hi, late ? 1u : 0u);
}

static inline int cp_sync_window(uint32_t total, uint32_t rows, uint32_t lines,
	int descending, uint32_t *lo, uint32_t *hi)
{
	return cp_sync_window_part(total, rows, lines, descending, CP_SYNC_PART,
		0, lo, hi);
}

static inline int cp_sync_window_game(uint32_t total, uint32_t rows,
	uint32_t lines, int descending, uint32_t *lo, uint32_t *hi)
{
	return cp_sync_window_part(total, rows, lines, descending,
		CP_SYNC_PART_GAME, 0, lo, hi);
}

/* ---- watching one frame go out -------------------------------------------- */

typedef struct cp_sync_track {
	uint32_t total;
	uint32_t strips, first, last, wraps;
	uint32_t gap;        /* shown row minus written row, modulo total */
	uint32_t near;       /* the least rows there were between the two */
	int crossed;
} cp_sync_track;

static inline void cp_sync_track_begin(cp_sync_track *t, uint32_t total)
{
	t->total = total;
	t->strips = 0;
	t->wraps = 0;
	t->near = total;
	t->crossed = 0;
}

/* Before each strip: the row being shown and the panel row the strip starts
 * at. The two have met if the gap between them went through zero. */
static inline void cp_sync_track_strip(cp_sync_track *t, uint32_t shown,
	uint32_t row)
{
	uint32_t gap;

	shown %= t->total;
	gap = (shown + t->total - row % t->total) % t->total;
	if(t->strips) {
		uint32_t step = gap > t->gap ? gap - t->gap : t->gap - gap;

		if(shown < t->last)
			t->wraps++;
		if(step > t->total / 2u)
			t->crossed = 1;
	} else {
		t->first = shown;
	}
	if(gap < t->near)
		t->near = gap;
	if(t->total - gap < t->near)
		t->near = t->total - gap;
	t->last = shown;
	t->gap = gap;
	t->strips++;
}

/* Raw rows shown up to the start of the last strip (strict lower bound on
 * actual frame completion, guarding the leading sync window edge). */
static inline uint32_t cp_sync_track_span(const cp_sync_track *t)
{
	if(t->strips < 2u)
		return 0u;
	return t->wraps * t->total + t->last - t->first;
}

/* Rows shown while the frame was written: what was seen up to the start of
 * the last strip, scaled up by that strip. `total_strips` allows sparse
 * sampling (e.g. first, midpoint, and last strip only) without losing the
 * exact 1/N last-strip scaling. */
static inline uint32_t cp_sync_track_lines_n(const cp_sync_track *t,
	uint32_t total_strips)
{
	uint32_t span = cp_sync_track_span(t);

	if(!span || total_strips < 2u)
		return 0u;
	return cgba_sync_scale_strips(span, total_strips);
}

static inline uint32_t cp_sync_track_lines(const cp_sync_track *t)
{
	return cp_sync_track_lines_n(t, t->strips);
}

/* Smooth per-kind frame write duration (in panel rows): delegates to the
 * shared cgba_sync_smooth() asymmetric smoother. */
static inline uint32_t cp_sync_smooth_lines(uint32_t cur, uint32_t measured,
	uint32_t total)
{
	return cgba_sync_smooth(cur, measured, total * 3u);
}

static inline uint32_t cp_sync_smooth_lines_min(uint32_t cur,
	uint32_t measured, uint32_t total)
{
	return cgba_sync_smooth_min(cur, measured, total * 3u);
}

/* Conservative backoff delay (in microseconds) while waiting for the panel
 * row `at` to reach window start `lo`. Returns 0 when within 28 rows of `lo`
 * so the caller tight-polls the final approach without overshooting.
 *
 * Uses 35 % of (delta - 24) rows (capped at 2 ms): the platform busy-wait
 * `lcd_delay_ns()` is a conservative lower bound (up to ~2x nominal on SH-4A),
 * so 35 % x 2 = 70 % stays strictly short of `lo` even at 100 Hz. */
static inline uint32_t cp_sync_wait_backoff_us(uint32_t at, uint32_t lo,
	uint32_t total, uint32_t hz)
{
	uint32_t delta, us;

	if(!total || !hz)
		return 0u;
	delta = (lo % total + total - at % total) % total;
	if(delta <= 28u)
		return 0u;
	us = (uint32_t)(((uint64_t)(delta - 24u) * 350000ull) /
		((uint64_t)total * hz));
	if(us > 2000u)
		us = 2000u;
	return us;
}

/* ---- R61523 refresh direction and register validation --------------------- */

/* Whether an R61523 refreshes logical panel rows 0..527 bottom-to-top
 * (527 -> 0), from MIPI DCS get_address_mode (0x0b):
 *   B7 (bit 7): page address order in frame memory
 *   B4 (bit 4): line refresh order (inverts memory read and gate scan together)
 * B0 (R61523 vertical flip) only inverts gate scan and does not change the
 * memory read order. So logical rows are scanned bottom-to-top iff (B7 ^ B4).
 *
 * When 0x0b reads disagree (valid == 0), default to top-down (0), matching
 * R61523's 360x640 memory with SCN = 0 (rows 0..527 in physical 0..527). */
static inline int cp_r61523_refresh_bottom_up(uint8_t mode, int valid)
{
	if(!valid)
		return 0;
	return (int)(((mode >> 7) ^ (mode >> 4)) & 1u);
}

/* Valid R61523 B0h (MCAP) values are 0x00, 0x02, 0x03 and 0x04; 0x01 and
 * 0x05..0x07 are setting inhibited (R61523 Rev.1.01, p.135). If a read does
 * not return a legal value, restore the hardware default 0x03 (Protect On). */
static inline uint8_t cp_r61523_mcap_restore_value(uint8_t raw)
{
	if(raw == 0x00u || raw == 0x02u || raw == 0x03u || raw == 0x04u)
		return raw;
	return 0x03u;
}

/* R61523 B9h param 3 (PWMDIV[7:0]): only these 10 divider codes are legal;
 * all others are setting inhibited (R61523 Rev.1.01, Table 54, p.154). */
static inline int cp_r61523_pwmdiv_valid(uint8_t div)
{
	switch(div) {
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x07:
	case 0x0f: case 0x1f: case 0x3f: case 0x7f: case 0xff:
		return 1;
	default:
		return 0;
	}
}

/* Sanity-check a 4-byte R61523 B9h (Backlight Control 2) snapshot read from
 * a lit panel before modifying it (R61523 Rev.1.01, pp.152-154):
 *   b9[0]: 0000_000_PWMON
 *   b9[1]: BDCV[7:0] (nonzero while lit)
 *   b9[2]: PWMDIV[7:0] (must be one of the 10 legal values)
 *   b9[3]: 000_PWMWM_LEDPWME_LEDPWMPOL_0_DIMON (LEDPWME must be 1 while the
 *          controller drives the backlight; if PWMWM = 1 then PWMON must be 1,
 *          as PWMWM = 1, PWMON = 0 forces 0% or is inhibited with BLCON = 1). */
static inline int cp_r61523_b9_valid(const uint8_t b9[4])
{
	if((b9[0] & 0xfeu) != 0)
		return 0;
	if(b9[1] == 0)
		return 0;
	if(!cp_r61523_pwmdiv_valid(b9[2]))
		return 0;
	if((b9[3] & 0xe2u) != 0 || (b9[3] & 0x08u) == 0)
		return 0;
	if((b9[3] & 0x10u) != 0 && (b9[0] & 0x01u) == 0)
		return 0;
	return 1;
}

/* Nominal backlight PWM frequency in Hz for rate 1..9 (0 = System). Both
 * panel families share these 9 rates (R61523 Table 54 and 16 MHz / N on
 * 0x16/0x52). */
static inline uint32_t cp_backlight_pwm_hz(uint32_t pwm)
{
	static const uint16_t hz[10] = {
		0, 430, 860, 1720, 3430, 6860, 13700, 18300, 27400, 33300
	};

	return pwm < 10u ? hz[pwm] : 0u;
}

/* Lowest Backlight stop (0..16) that keeps the LED boost driver lit at a
 * given PWM rate (0..9). At high PWM rates the period is short (30 us at
 * 33.3 kHz), so low duties fall below the driver's ~8-9 us minimum turn-on
 * pulse and black out the screen:
 *   pwm 9 (33300 Hz) -> >= 5, pwm 8 (27400 Hz) -> >= 4,
 *   pwm 7 (18300 Hz) -> >= 3, pwm 6 (13700 Hz) -> >= 2,
 *   pwm 5 ( 6860 Hz) -> >= 1, pwm 0..4         -> >= 0 (System allowed). */
static inline uint32_t cp_backlight_min_level(uint32_t pwm)
{
	return pwm >= 5u && pwm <= 9u ? pwm - 4u : 0u;
}

/* ---- panel refresh rate (OS 02.01.7003 @ 0x8003887e / [FRAME Freq SET]) --- */

/* Unified refresh-rate presets (aligned with fx-CG50 / fx-CG100):
 *   0 = Auto           (59.73 Hz (GBA) when VSync is On, System when Off)
 *   1 = System         (leave OS stock panel timing untouched)
 *   2 = 59.73 Hz (GBA) (1x native GBA, 59.7275 Hz)
 *   3..9 = 66, 72, 78, 85, 90, 95, 100 Hz high-refresh presets. */
#define CP_REFRESH_COUNT       10u
#define CP_REFRESH_NEW_STOCK_MHZ 57320u /* measured 0x16 unit at RTN=0x9D */
#define CP_REFRESH_NEW_VBP_ALL 12u      /* 548 - 528 - 8 VFP = 12 fixed rows */
#define CP_REFRESH_NEW_VFP_STOCK 8u
#define CP_REFRESH_NEW_VFP_MAX 24u      /* HZPROBE.TXT verified up to 32 */
#define CP_REFRESH_NEW_RTN_MIN 50u
#define CP_REFRESH_NEW_RTN_MAX 253u

/* Effective target refresh rate in millihertz for a (refresh_mode, vsync_on)
 * pair. Mode 0 (Auto) targets 59.728 Hz when VSync is on and leaves stock
 * timing alone when VSync is off. Returns 0 for stock panel timing. */
static inline uint32_t cp_refresh_target_mhz(uint32_t rate, uint32_t vsync_on)
{
	switch(rate) {
	case 0u: return vsync_on ? CGBA_SYNC_RATE_GBA_MHZ : 0u; /* Auto */
	case 1u: return 0u;                                     /* System */
	case 2u: return CGBA_SYNC_RATE_GBA_MHZ;                 /* 59.73 Hz (GBA) */
	case 3u: return CGBA_SYNC_RATE_66_MHZ;
	case 4u: return CGBA_SYNC_RATE_72_MHZ;
	case 5u: return CGBA_SYNC_RATE_78_MHZ;
	case 6u: return CGBA_SYNC_RATE_85_MHZ;
	case 7u: return CGBA_SYNC_RATE_90_MHZ;
	case 8u: return CGBA_SYNC_RATE_95_MHZ;
	case 9u: return CGBA_SYNC_RATE_100_MHZ;
	default: return 0u;
	}
}

/* Nominal integer Hz for display / scaling fallbacks (0 for Auto / System). */
static inline uint32_t cp_refresh_hz(uint32_t rate)
{
	static const uint8_t hz[CP_REFRESH_COUNT] = {
		0, 0, 60, 66, 72, 78, 85, 90, 95, 100
	};

	return rate < CP_REFRESH_COUNT ? (uint32_t)hz[rate] : 0u;
}

/* Nominal RTN byte for 0x16/0x52 (0xB2 byte 6 and 0xDF byte 12; OS range
 * 50..253, default 0x9D = 157, ~57.3 Hz real). Used as fallback table when
 * no measured stock frequency is available yet. */
static inline uint8_t cp_refresh_rtn_new(uint32_t rate)
{
	static const uint8_t rtn[CP_REFRESH_COUNT] = {
		0x9du, 0x9du, 0x92u, 0x7au, 0x6bu, 0x5cu, 0x4au, 0x42u, 0x3au, 0x32u
	};

	return rate < CP_REFRESH_COUNT ? rtn[rate] : 0x9du;
}

/* Nominal RTN byte for R61523 (0xC1/0xC3 byte 3; OS range 18..63, default 31). */
static inline uint8_t cp_refresh_rtn_r61523(uint32_t rate)
{
	static const uint8_t rtn[CP_REFRESH_COUNT] = {
		31u, 31u, 31u, 27u, 25u, 23u, 21u, 20u, 19u, 18u
	};

	return rate < CP_REFRESH_COUNT ? rtn[rate] : 31u;
}

/* Jointly solve (VFP, RTN) for ClassPad II 0x16/0x52 to hit `target_mhz`
 * from measured `stock_mhz` (falls back to 57320 mHz if 0). Allows VFP in
 * [8..24] for 59.73 Hz (GBA) precision (~0.01 Hz error) and keeps VFP = 8
 * (548 rows) for high-refresh stops >= 66 Hz to maximize the VSync window. */
static inline void cp_refresh_solve_new(uint32_t stock_mhz,
	uint32_t target_mhz, uint8_t *vfp_out, uint8_t *rtn_out,
	uint32_t *total_out)
{
	cgba_timing_spec spec;
	uint32_t fp, rtn, total;

	if(!target_mhz) {
		*vfp_out = (uint8_t)CP_REFRESH_NEW_VFP_STOCK;
		*rtn_out = 0x9du;
		*total_out = 528u + CP_REFRESH_NEW_VBP_ALL + CP_REFRESH_NEW_VFP_STOCK;
		return;
	}
	if(!stock_mhz)
		stock_mhz = CP_REFRESH_NEW_STOCK_MHZ;
	spec.visible_rows = 528u;
	spec.bp_rows = CP_REFRESH_NEW_VBP_ALL;
	spec.orig_fp = CP_REFRESH_NEW_VFP_STOCK;
	spec.orig_rtn = 0x9du;
	spec.rtn_bias = CGBA_HIMAX_RTN_BIAS;
	spec.min_fp = CP_REFRESH_NEW_VFP_STOCK;
	spec.max_fp = (target_mhz <= 62000u)
		? CP_REFRESH_NEW_VFP_MAX : CP_REFRESH_NEW_VFP_STOCK;
	spec.min_rtn = CP_REFRESH_NEW_RTN_MIN;
	spec.max_rtn = CP_REFRESH_NEW_RTN_MAX;

	cgba_sync_solve_vfp_rtn_mhz(&spec, stock_mhz, target_mhz,
		&fp, &rtn, &total);
	*vfp_out = (uint8_t)fp;
	*rtn_out = (uint8_t)rtn;
	*total_out = total;
}

/* Solve RTN (18..63) for ClassPad II R61523 (0xC1/0xC3 byte 3) to hit
 * `target_mhz` from measured `stock_mhz` and saved `orig_rtn`. */
static inline uint8_t cp_refresh_solve_r61523(uint32_t stock_mhz,
	uint32_t target_mhz, uint8_t orig_rtn)
{
	cgba_timing_spec spec;
	uint32_t fp, rtn, total;

	if(!target_mhz || orig_rtn < 18u || orig_rtn > 63u)
		return orig_rtn;
	if(!stock_mhz)
		stock_mhz = 60000u;
	spec.visible_rows = 528u;
	spec.bp_rows = 27u;
	spec.orig_fp = 0u;
	spec.orig_rtn = orig_rtn;
	spec.rtn_bias = 0u;
	spec.min_fp = 0u;
	spec.max_fp = 0u;
	spec.min_rtn = 18u;
	spec.max_rtn = 63u;

	cgba_sync_solve_vfp_rtn_mhz(&spec, stock_mhz, target_mhz,
		&fp, &rtn, &total);
	return (uint8_t)rtn;
}

/* Sanity-check a 5-byte R61523 C1h (Display Timing Setting for Normal Mode)
 * snapshot before modifying byte 3 (RTN[5:0], legal OS range 18..63). */
static inline int cp_r61523_c1_valid(const uint8_t c1[5])
{
	return c1[2] >= 18u && c1[2] <= 63u;
}

/* ---- 1:1 panel-locked frame pacing (59.73 Hz (GBA) + VSync) --------------- */

typedef struct cp_pace_state {
	int      valid;
	uint32_t last_etmu;   /* 32.768 kHz down-counter at last synced sample */
	uint32_t last_row;    /* panel scanline [0, total) at last synced sample */
	uint32_t now_rows;    /* unwrapped panel scanlines elapsed */
	uint32_t due_rows;    /* unwrapped panel scanline deadline */
} cp_pace_state;

/* Estimate elapsed panel rows from a 32.768 kHz down-counter delta (`hz_mhz`
 * in millihertz, e.g. 59728). Used only to disambiguate whole refreshes. */
static inline uint32_t cp_pace_approx_rows(uint32_t etmu_dt, uint32_t total,
	uint32_t hz_mhz)
{
	if(!total || !hz_mhz)
		return 0u;
	return (uint32_t)(((uint64_t)etmu_dt * total * hz_mhz + 16384000ull) /
		32768000ull);
}

/* Unwrap a live panel row reading `now_row` relative to `prev_row` using
 * `approx_rows` to disambiguate whole refreshes. */
static inline uint32_t cp_pace_unwrap_rows(uint32_t prev_row, uint32_t now_row,
	uint32_t approx_rows, uint32_t total)
{
	uint32_t mod_rows, wraps;

	if(!total)
		return 0u;
	mod_rows = (now_row % total + total - prev_row % total) % total;
	if(approx_rows + total / 2u < mod_rows)
		return 0u; /* minor backward read jitter near 0 elapsed */
	wraps = (approx_rows + total / 2u - mod_rows) / total;
	return wraps * total + mod_rows;
}

/* Advance the 1:1 panel-locked frame deadline by one refresh (`total` rows)
 * and update `s` with a fresh (etmu, row) sample. Returns remaining panel
 * rows to wait (> 0 when early, *late_out = 0), or 0 when on time / behind
 * (with *late_out in [0, max_late + 1], matching cgba_frame_limiter_wait()). */
static inline uint32_t cp_pace_advance(cp_pace_state *s, uint32_t now_etmu,
	uint32_t now_row, uint32_t total, uint32_t hz_mhz, int max_late,
	int *late_out)
{
	uint32_t dt, approx, exact;
	int32_t rem;

	if(!total || !hz_mhz) {
		if(late_out)
			*late_out = -1;
		return 0u;
	}
	if(!s->valid) {
		s->last_etmu = now_etmu;
		s->last_row = now_row % total;
		s->now_rows = 0u;
		s->due_rows = 0u;
		s->valid = 1;
		if(late_out)
			*late_out = 0;
		return 0u;
	}
	dt = (uint32_t)(s->last_etmu - now_etmu);
	approx = cp_pace_approx_rows(dt, total, hz_mhz);
	exact = cp_pace_unwrap_rows(s->last_row, now_row, approx, total);
	s->now_rows += exact;
	s->last_etmu = now_etmu;
	s->last_row = now_row % total;
	s->due_rows += total;

	rem = (int32_t)(s->due_rows - s->now_rows);
	if(rem > 0) {
		if((uint32_t)rem > total * 2u) {
			s->due_rows = s->now_rows;
			if(late_out)
				*late_out = 0;
			return 0u;
		}
		if(late_out)
			*late_out = 0;
		return (uint32_t)rem;
	}
	{
		uint32_t behind = (uint32_t)(-rem);
		uint32_t late = behind / total;

		if(max_late >= 0 && late > (uint32_t)max_late) {
			s->due_rows = s->now_rows - total * (uint32_t)max_late;
			late = (uint32_t)(max_late + 1);
		}
		if(late_out)
			*late_out = (int)late;
		return 0u;
	}
}

#endif /* CGBA_CLASSPAD_SYNC_H */
