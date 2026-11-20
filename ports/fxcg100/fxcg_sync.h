#ifndef CGBA_FXCG_SYNC_H
#define CGBA_FXCG_SYNC_H

/* Keeping the picture from tearing on the fx-CG100 and fx-CG50 (R61524).
 *
 * Measured on fx-CG100 hardware (Renesas R61524, device code 0x1524):
 *   - The panel refreshes 224 visible rows top-to-bottom (0 -> 223), plus
 *     vertical blanking set in R008 (default 0x0808: 8 BP + 8 FP = 240 rows
 *     total) at 61.154 Hz (16351.9 us/frame, 68.133 us/row).
 *   - The controller's FMARK pin is wired to SH7305 Port R bit 5 (PR.5,
 *     0xA405013C bit 5), right next to the LCD RS pin (PR.4). Casio's OS
 *     configures PR.5 as input and R009=0x8001 / R00D=0x8000 (FMARKOE=1,
 *     FMP=0), so PR.5 emits a 1-row (~68.2 us) active-high pulse at the
 *     start of Back Porch (just before row 0) on every refresh.
 *   - Reading PR.5 hits only the on-chip PFC register: zero CS5A LCD bus
 *     traffic, zero conflict with active DMAC0 transfers, and unaffected by
 *     overclocked LCD read waits.
 *
 * Because the R61524 has no readable scanline counter register, we track the
 * panel's phase in software against a free-running hardware down-counter
 * (TMU at Pphi/4) anchored to the latest observed PR.5 rising edge. Any PR.5
 * edge observed while waiting for a strip DMA or a safe window re-anchors
 * the phase and smoothly tracks small crystal/temperature drift.
 *
 * Both the panel scan and the strip DMA advance top-to-bottom. A frame whose
 * picture occupies panel rows [y0, y0 + h) and takes `write_ticks` to send
 * avoids crossing the scanline iff it starts within a safe phase window
 * computed by cg_sync_safe_window(). When the current phase is already in
 * that window, presentation starts with zero wait. */

#include <stdint.h>

#include "lcd_sync_common.h"

#define CG_SYNC_VISIBLE_ROWS     224u
#define CG_SYNC_DEFAULT_BP       8u
#define CG_SYNC_DEFAULT_FP       8u
#define CG_SYNC_DEFAULT_TOTAL    (CG_SYNC_VISIBLE_ROWS + CG_SYNC_DEFAULT_BP + CG_SYNC_DEFAULT_FP)

/* Safety margins on the two edges of the safe window, in scanlines:
 *   - LO (12 rows, ~0.8 ms at 59.73 Hz): guards the leading edge where fast
 *     DMA could otherwise overtake the scan beam in the bottom half of the
 *     screen (especially when phase-locked 1:1 at 59.73 Hz).
 *   - HI (6 rows, ~0.4 ms): guards the trailing edge while keeping phase 0
 *     (PR.5 rising edge, 8 BP rows before row 0) inside the safe window even
 *     for a full 224-row frame. */
#define CG_SYNC_MARGIN_ROWS      6u
#define CG_SYNC_MARGIN_LO_ROWS   12u
#define CG_SYNC_MARGIN_HI_ROWS   6u
#define CG_SYNC_MIN_WINDOW_ROWS  8u

/* Re-acquire a hardware PR.5 rising edge:
 *   - Soft threshold (45 refreshes, ~0.75 s): catch PR.5 opportunistically
 *     whenever it is already imminent (<= ~1 ms away) or during idle pacing.
 *   - Hard fallback (180 refreshes, ~3.0 s): only block for a full rising edge
 *     if no opportunistic edge was seen for ~3 seconds (prevents the periodic
 *     16.7 ms frame-drop stalls caused by aggressive blocking resyncs). */
#define CG_SYNC_SOFT_RESYNC_PERIODS 45u
#define CG_SYNC_RESYNC_PERIODS      180u

/* Scaling modes in gint_platform.c: 0 = Original, 1 = 4:3, 2 = Full Screen. */
#define CG_SYNC_MODE_COUNT       3u

typedef struct cg_sync_state {
	uint32_t enabled;
	uint32_t period_ticks;       /* measured PR.5 period in down-counter ticks */
	uint32_t total_rows;         /* visible + BP + FP (default 240) */
	uint32_t bp_rows;            /* back porch rows after PR.5 rising edge */
	uint32_t anchor_tcnt;        /* down-counter reading at last PR.5 edge */
	int      anchor_valid;
	int      prev_fmark;         /* previous PR.5 level for edge detection */
	uint32_t pace_due_tcnt;      /* 1:1 panel-locked frame deadline (down-counter) */
	int      pace_valid;
	uint32_t write_ticks[CG_SYNC_MODE_COUNT];      /* upper envelope (write_max) */
	uint32_t write_min_ticks[CG_SYNC_MODE_COUNT];  /* lower envelope (write_min) */
} cg_sync_state;

/* Safe bounds when tuning R008 (FP/BP) and R010 (RTNI) on R61524:
 *   - Like Renesas R61509/R61505 (Display Control 2, R008h), the high byte
 *     is FP (Front Porch, after visible rows) and the low byte is BP (Back
 *     Porch, before visible rows, immediately following the PR.5 FMARK pulse).
 *   - Hardware testing on fx-CG100 showed lowering below 8 (0x0607) causes a
 *     white screen, while keeping FP untouched (>= 8), raising BP up to 24
 *     (0x0818, 256 rows), and live-tuning R010 (RTNI) down to 16 (0x10,
 *     ~206 Hz) all work cleanly.
 *   - Keeping FP unchanged and restricting BP to [orig_bp, 24] and RTNI to
 *     [16, 63] guarantees safe operation across all selectable rates while
 *     tracking the true BP row offset relative to PR.5. */
#define CG_SYNC_MAX_TUNE_BP      24u
#define CG_SYNC_MAX_TUNE_FP      CG_SYNC_MAX_TUNE_BP
#define CG_SYNC_MIN_TUNE_RTNI    16u
#define CG_SYNC_MAX_TUNE_RTNI    63u

/* Himax-family fx-CG100 OS 2.10 panel (ID 0x32/0x52, 0xB9: FF 52 98):
 * 224 visible + 76 VBP (0x4C) + 75 VFP (0x4B) = 375 rows, OS RTN range
 * 15..255 (0x0F..0xFF, stock 0xFF). Allow raising VFP up to +24 rows (0x63)
 * for fine-grained 59.73 Hz / 119.46 Hz locking.
 *
 * OS 2.10 configures MIPI DCS TEON (0x35) = 0x00 (Mode 0, V-Blanking only)
 * with STE (0x44) = 0x0000: TE rises at the start of V-Blank (start of VFP,
 * after row 223) and stays high across VFP + VBP before active row 0. */
#define CG_HIMAX_VBP             76u
#define CG_HIMAX_STOCK_VFP       75u
#define CG_HIMAX_MAX_TUNE_VFP    99u
#define CG_HIMAX_MIN_TUNE_RTN    15u
#define CG_HIMAX_MAX_TUNE_RTN    255u
#define CG_HIMAX_STOCK_TOTAL     (CG_SYNC_VISIBLE_ROWS + CG_HIMAX_VBP + CG_HIMAX_STOCK_VFP)
#define CG_HIMAX_TE_BLANK(vfp)   (CG_HIMAX_VBP + (uint32_t)(vfp))

/* Target refresh rates in millihertz (aliases of lcd_sync_common.h). */
#define CG_SYNC_RATE_GBA_MHZ     CGBA_SYNC_RATE_GBA_MHZ
#define CG_SYNC_RATE_75_MHZ      CGBA_SYNC_RATE_75_MHZ
#define CG_SYNC_RATE_90_MHZ      CGBA_SYNC_RATE_90_MHZ
#define CG_SYNC_RATE_120_MHZ     CGBA_SYNC_RATE_120_MHZ
#define CG_SYNC_RATE_150_MHZ     CGBA_SYNC_RATE_150_MHZ

/* Decode R61524 R008 (Display Control 2: high byte = FP, low byte = BP,
 * matching Renesas R61509/R61505). Hardware testing on fx-CG100 shows the low
 * byte accepts at least 5 bits (0x18 = 24); fall back to the measured 8/8
 * defaults if a read looks out of range. */
static inline void cg_sync_parse_r008(uint16_t r008, uint32_t *bp_out,
	uint32_t *total_out)
{
	uint32_t fp = ((uint32_t)r008 >> 8) & 0x1fu;
	uint32_t bp = (uint32_t)r008 & 0x1fu;

	if(bp < 2u || bp > 24u || fp < 2u || fp > 24u) {
		bp = CG_SYNC_DEFAULT_BP;
		fp = CG_SYNC_DEFAULT_FP;
	}
	*bp_out = bp;
	*total_out = CG_SYNC_VISIBLE_ROWS + bp + fp;
}

/* Jointly solve (R008, R010) to hit `target_period_ticks` as closely as
 * possible from a measured stock baseline (orig_r008, orig_r010,
 * orig_period_ticks). Keeps FP (high byte) untouched and tunes BP (low byte,
 * hardware-verified up to 24), returning the true BP in `*bp_out`. */
static inline void cg_sync_solve_timing(uint16_t orig_r008, uint16_t orig_r010,
	uint32_t orig_period_ticks, uint32_t target_period_ticks,
	uint16_t *out_r008, uint16_t *out_r010,
	uint32_t *bp_out, uint32_t *total_out)
{
	uint32_t orig_bp, orig_total, orig_fp, orig_rtn;
	uint32_t best_bp, best_rtn, best_total;
	cgba_timing_spec spec;

	cg_sync_parse_r008(orig_r008, &orig_bp, &orig_total);
	orig_fp = orig_total - CG_SYNC_VISIBLE_ROWS - orig_bp;
	orig_rtn = (uint32_t)orig_r010 & 0x3fu;
	if(orig_rtn == 0u)
		orig_rtn = 64u;

	/* Pass fixed porch (orig_fp) in bp_rows and tuned porch (orig_bp) in
	 * orig_fp so the shared solver varies BP across [orig_bp, 24]. */
	spec.visible_rows = CG_SYNC_VISIBLE_ROWS;
	spec.bp_rows = orig_fp;
	spec.orig_fp = orig_bp;
	spec.orig_rtn = orig_rtn;
	spec.rtn_bias = 0u;
	spec.min_fp = orig_bp;
	spec.max_fp = CG_SYNC_MAX_TUNE_BP;
	spec.min_rtn = CG_SYNC_MIN_TUNE_RTNI;
	spec.max_rtn = CG_SYNC_MAX_TUNE_RTNI;

	cgba_sync_solve_vfp_rtn(&spec, orig_period_ticks, target_period_ticks,
		&best_bp, &best_rtn, &best_total);

	*out_r008 = (uint16_t)((orig_r008 & ~0x1fu) | (best_bp & 0x1fu));
	*out_r010 = (uint16_t)((orig_r010 & ~0x3fu) | (best_rtn & 0x3fu));
	*bp_out = best_bp;
	*total_out = best_total;
}

/* Jointly solve (VFP, RTN) for fx-CG100 OS 2.10 Himax-family panels
 * (ID 0x32/0x52, 0xB2 + 0xDF, stock VBP=76 / VFP=75 / RTN=255). */
static inline void cg_sync_solve_himax(uint32_t orig_period_ticks,
	uint32_t target_period_ticks, uint8_t *vfp_out, uint8_t *rtn_out,
	uint32_t *total_out)
{
	uint32_t best_fp, best_rtn, best_total;
	cgba_timing_spec spec;

	spec.visible_rows = CG_SYNC_VISIBLE_ROWS;
	spec.bp_rows = CG_HIMAX_VBP;
	spec.orig_fp = CG_HIMAX_STOCK_VFP;
	spec.orig_rtn = 0xffu;
	spec.rtn_bias = CGBA_HIMAX_RTN_BIAS;
	spec.min_fp = CG_HIMAX_STOCK_VFP;
	spec.max_fp = CG_HIMAX_MAX_TUNE_VFP;
	spec.min_rtn = CG_HIMAX_MIN_TUNE_RTN;
	spec.max_rtn = CG_HIMAX_MAX_TUNE_RTN;

	cgba_sync_solve_vfp_rtn(&spec, orig_period_ticks, target_period_ticks,
		&best_fp, &best_rtn, &best_total);

	*vfp_out = (uint8_t)best_fp;
	*rtn_out = (uint8_t)best_rtn;
	*total_out = best_total;
}

/* Predict the new PR.5 period in ticks after changing (total, rtn) relative
 * to a measured stock baseline (orig_total, orig_rtn, orig_period_ticks). */
static inline uint32_t cg_sync_expected_period(uint32_t orig_period_ticks,
	uint32_t orig_total, uint32_t orig_rtn, uint32_t new_total,
	uint32_t new_rtn, uint32_t rtn_bias)
{
	uint64_t orig_clocks = (uint64_t)orig_total * (orig_rtn + rtn_bias);
	uint64_t new_clocks = (uint64_t)new_total * (new_rtn + rtn_bias);

	if(!orig_period_ticks || !orig_clocks || !new_clocks)
		return 0u;
	return (uint32_t)(((uint64_t)orig_period_ticks * new_clocks +
		orig_clocks / 2u) / orig_clocks);
}

/* True if a measured PR.5 period after live timing adjustment agrees with
 * the solver's expected period within +/-1.0 % (catches silently ignored
 * controller writes such as locked 0xB9 on Himax or read-only R008/R010). */
static inline int cg_sync_period_matches(uint32_t measured, uint32_t expected)
{
	uint64_t err;

	if(!measured || !expected)
		return 0;
	err = (measured >= expected)
		? (uint64_t)(measured - expected)
		: (uint64_t)(expected - measured);
	return err * 100u <= (uint64_t)expected;
}

/* Effective target refresh rate in millihertz for a (refresh_mode, vsync_on)
 * pair. Mode 0 (Auto) targets 59.728 Hz when VSync is on and leaves stock
 * clocks alone when VSync is off. Returns 0 for stock panel timing. */
static inline uint32_t cg_sync_target_mhz(uint32_t refresh_mode,
	uint32_t vsync_on)
{
	switch(refresh_mode) {
	case 0u: return vsync_on ? CG_SYNC_RATE_GBA_MHZ : 0u; /* Auto */
	case 1u: return 0u;                                   /* System */
	case 2u: return CG_SYNC_RATE_GBA_MHZ;                 /* 59.7 Hz */
	case 3u: return CG_SYNC_RATE_75_MHZ;                  /* 75 Hz */
	case 4u: return CG_SYNC_RATE_90_MHZ;                  /* 90 Hz */
	case 5u: return CG_SYNC_RATE_120_MHZ;                 /* 120 Hz (2x GBA) */
	case 6u: return CG_SYNC_RATE_150_MHZ;                 /* 150 Hz */
	default: return 0u;
	}
}

/* Compute the modular phase [0, period) from a down-counting timer. */
static inline uint32_t cg_sync_phase_from_tcnt(uint32_t anchor_tcnt,
	uint32_t now_tcnt, uint32_t period)
{
	if(!period)
		return 0u;
	return (uint32_t)(anchor_tcnt - now_tcnt) % period;
}

/* True if the software phase anchor is old enough for an opportunistic
 * low-cost PR.5 edge catch (without blocking for a full refresh). */
static inline int cg_sync_wants_soft_resync(const cg_sync_state *s,
	uint32_t now_tcnt)
{
	uint32_t elapsed;

	if(!s->anchor_valid || !s->period_ticks)
		return 1;
	elapsed = (uint32_t)(s->anchor_tcnt - now_tcnt);
	return elapsed >= s->period_ticks * CG_SYNC_SOFT_RESYNC_PERIODS;
}

/* True if the software phase anchor is old enough that we should re-lock to
 * a hardware PR.5 edge to prevent drift. */
static inline int cg_sync_needs_resync(const cg_sync_state *s,
	uint32_t now_tcnt)
{
	uint32_t elapsed;

	if(!s->anchor_valid || !s->period_ticks)
		return 1;
	elapsed = (uint32_t)(s->anchor_tcnt - now_tcnt);
	return elapsed >= s->period_ticks * CG_SYNC_RESYNC_PERIODS;
}

/* Advance the 1:1 panel-locked frame deadline by one panel refresh and
 * compute either the remaining ticks to wait (returned > 0, *late_out = 0)
 * or how many whole refreshes the frame ran behind (returned == 0,
 * *late_out in [0, max_late + 1], matching cgba_frame_limiter_wait()). */
static inline uint32_t cg_sync_pace_advance(cg_sync_state *s,
	uint32_t now_tcnt, int max_late, int *late_out)
{
	uint32_t per = s->period_ticks;
	int32_t rem;

	if(!per) {
		if(late_out)
			*late_out = -1;
		return 0u;
	}
	if(!s->pace_valid) {
		s->pace_due_tcnt = now_tcnt;
		s->pace_valid = 1;
		if(late_out)
			*late_out = 0;
		return 0u;
	}
	s->pace_due_tcnt -= per;
	rem = (int32_t)(now_tcnt - s->pace_due_tcnt);
	if(rem > 0) {
		if((uint32_t)rem > per * 2u) {
			s->pace_due_tcnt = now_tcnt;
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
		uint32_t late = behind / per;

		if(max_late >= 0 && late > (uint32_t)max_late) {
			s->pace_due_tcnt = now_tcnt + per * (uint32_t)max_late;
			late = (uint32_t)(max_late + 1);
		}
		if(late_out)
			*late_out = (int)late;
		return 0u;
	}
}

/* Record an observed PR.5 rising edge (at down-counter timestamp `tcnt`).
 * Supports both single-frame intervals and multi-frame intervals (k = 1..128
 * refreshes since the last observed edge): whenever dt is unambiguously close
 * to k * period_ticks (within +/-12.5 % of one refresh), refines period_ticks
 * toward dt / k so phase-locked 59.73 Hz play locks to the panel's true RC
 * oscillator instead of drifting between resyncs. */
static inline void cg_sync_note_edge(cg_sync_state *s, uint32_t edge_tcnt)
{
	if(s->anchor_valid && s->period_ticks) {
		uint32_t dt = (uint32_t)(s->anchor_tcnt - edge_tcnt);
		uint32_t per = s->period_ticks;
		uint32_t k = (dt + per / 2u) / per;

		if(k >= 1u && k <= 128u) {
			uint64_t expected = (uint64_t)k * per;
			uint64_t err = ((uint64_t)dt >= expected)
				? ((uint64_t)dt - expected) : (expected - (uint64_t)dt);

			if(err * 8u < (uint64_t)per) {
				uint32_t est = (uint32_t)(((uint64_t)dt + k / 2u) / k);

				if(k >= 16u)
					s->period_ticks = (per + est + 1u) / 2u;
				else if(k >= 4u)
					s->period_ticks = (per * 3u + est + 2u) / 4u;
				else
					s->period_ticks = (per * 7u + est + 4u) / 8u;
			}
		}
	}
	s->anchor_tcnt = edge_tcnt;
	s->anchor_valid = 1;
}

/* Conservative initial write-time estimate (in ticks) before the first frame
 * in a scaling mode has been timed. */
static inline uint32_t cg_sync_default_write_ticks(uint32_t period,
	uint32_t mode)
{
	if(mode == 0u)
		return period * 30u / 100u;    /* 1:1 240x160 */
	if(mode == 1u)
		return period * 50u / 100u;    /* 4:3 320x212 */
	return period * 60u / 100u;        /* Full Screen 384x216 */
}

/* Smooth the per-mode write duration bounds:
 *   - `measured_max` feeds the upper envelope (write_ticks) so slow frames
 *     tighten the trailing edge (`hi`) immediately.
 *   - `measured_min` feeds the lower envelope (write_min_ticks) so fast
 *     frames tighten the leading edge (`lo`) immediately. */
static inline void cg_sync_update_write_bounds(cg_sync_state *s, uint32_t mode,
	uint32_t measured_min, uint32_t measured_max)
{
	if(mode >= CG_SYNC_MODE_COUNT || !s->period_ticks)
		return;
	s->write_ticks[mode] = cgba_sync_smooth(s->write_ticks[mode],
		measured_max, s->period_ticks * 2u);
	s->write_min_ticks[mode] = cgba_sync_smooth_min(s->write_min_ticks[mode],
		measured_min, s->period_ticks * 2u);
}

static inline void cg_sync_update_write_ticks(cg_sync_state *s, uint32_t mode,
	uint32_t measured)
{
	cg_sync_update_write_bounds(s, mode, measured, measured);
}

/* Compute the tear-free start window [safe_lo, safe_lo + safe_len) modulo
 * `period` for a top-down write of panel rows [y0, y0 + h) whose actual
 * duration may vary anywhere in [write_min, write_max].
 *
 * Intersects the safe windows of both bounds:
 *   - Earliest safe start (`lo`) is governed by `write_min` so a fast frame
 *     cannot overtake the scan beam in the bottom half of the screen.
 *   - Latest safe start (`hi`) is governed by `write_max` so a slow frame
 *     cannot be overtaken by the next refresh before finishing y0 + h. */
static inline int cg_sync_safe_window_range(uint32_t period,
	uint32_t total_rows, uint32_t bp_rows, uint32_t y0, uint32_t h,
	uint32_t write_min, uint32_t write_max,
	uint32_t *safe_lo, uint32_t *safe_len)
{
	uint32_t t_top, t_bot, scan_h, margin_lo, margin_hi, min_win, lo, hi;

	if(!period || !total_rows || !h || bp_rows + y0 + h > total_rows)
		return 0;
	if(write_max < write_min)
		write_max = write_min;

	t_top = (uint32_t)(((uint64_t)(bp_rows + y0) * period) / total_rows);
	t_bot = (uint32_t)(((uint64_t)(bp_rows + y0 + h) * period) / total_rows);
	scan_h = t_bot - t_top;
	margin_lo = (uint32_t)(((uint64_t)CG_SYNC_MARGIN_LO_ROWS * period) / total_rows);
	margin_hi = (uint32_t)(((uint64_t)CG_SYNC_MARGIN_HI_ROWS * period) / total_rows);
	min_win = (uint32_t)(((uint64_t)CG_SYNC_MIN_WINDOW_ROWS * period) / total_rows);

	if(write_min <= scan_h)
		lo = t_bot - write_min + margin_lo;
	else
		lo = t_top + margin_lo;

	if(write_max <= scan_h)
		hi = period + t_top - margin_hi;
	else if(period + t_bot > write_max + margin_hi)
		hi = period + t_bot - write_max - margin_hi;
	else
		return 0;

	if(hi <= lo || hi - lo < min_win)
		return 0;
	*safe_lo = lo % period;
	*safe_len = hi - lo;
	return 1;
}

static inline int cg_sync_safe_window(uint32_t period, uint32_t total_rows,
	uint32_t bp_rows, uint32_t y0, uint32_t h, uint32_t write_ticks,
	uint32_t *safe_lo, uint32_t *safe_len)
{
	return cg_sync_safe_window_range(period, total_rows, bp_rows, y0, h,
		write_ticks, write_ticks, safe_lo, safe_len);
}

/* Ticks to wait from `phase` to enter [safe_lo, safe_lo + safe_len) mod
 * `period`. Returns 0 when already inside the safe window. */
static inline uint32_t cg_sync_wait_needed(uint32_t phase, uint32_t period,
	uint32_t safe_lo, uint32_t safe_len)
{
	uint32_t rel;

	if(!period || safe_len >= period)
		return 0u;
	rel = (phase % period + period - safe_lo % period) % period;
	if(rel < safe_len)
		return 0u;
	return period - rel;
}

#endif
