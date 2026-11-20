#ifndef CGBA_LCD_SYNC_COMMON_H
#define CGBA_LCD_SYNC_COMMON_H

/* Shared VSync & panel refresh-rate primitives for ClassPad II and
 * fx-CG50 / fx-CG100.
 *
 * Both calculator lines share:
 *   1. Refresh-rate preset prefix:
 *        0 = Auto          (59.73 Hz (GBA) when VSync is On, System when Off)
 *        1 = System        (leave OS stock panel timing untouched)
 *        2 = 59.73 Hz (GBA) (1x native GBA, 59.7275 Hz)
 *        3..N = platform high-refresh presets (ClassPad up to 100 Hz,
 *               fx-CG up to 150 Hz)
 *   2. Asymmetric write-duration smoothing (track slowdowns quickly, decay
 *      speedups slowly) and last-strip duration scaling `span * N / (N - 1)`.
 *   3. Joint (VFP, RTN) timing solver supporting both pure-RTN controllers
 *      (Renesas R61524 / R61523, rtn_bias = 0) and Casio's newer Himax-family
 *      MIPI DCS controllers (ClassPad II 0x16/0x52 and fx-CG100 OS 2.10
 *      0x32/0x52, both unlocked by 0xB9: FF 52 98, line clocks = RTN + 98).
 *   4. Identical 6-byte 0xB2 and 12-byte 0xDF payload builders for the
 *      Himax-family panels, differing only in per-panel geometry (VBP/VFP and
 *      B2 bytes 1..2). */

#include <stdint.h>

/* Shared lcd_refresh preset prefix (packed in CGBA.CFG lcd_extra bits 20..23). */
#define CGBA_REFRESH_AUTO        0u
#define CGBA_REFRESH_SYSTEM      1u
#define CGBA_REFRESH_GBA         2u
#define CGBA_REFRESH_MAX_INDEX   9u

/* Target refresh rates in millihertz (0 = keep stock panel timing). */
#define CGBA_SYNC_RATE_GBA_MHZ   59728u   /* 1x native GBA (59.7275 Hz) */
#define CGBA_SYNC_RATE_66_MHZ    66000u
#define CGBA_SYNC_RATE_72_MHZ    72000u
#define CGBA_SYNC_RATE_75_MHZ    75000u
#define CGBA_SYNC_RATE_78_MHZ    78000u
#define CGBA_SYNC_RATE_85_MHZ    85000u
#define CGBA_SYNC_RATE_90_MHZ    90000u
#define CGBA_SYNC_RATE_95_MHZ    95000u
#define CGBA_SYNC_RATE_100_MHZ   100000u
#define CGBA_SYNC_RATE_120_MHZ   119455u  /* 2x native GBA (119.455 Hz) */
#define CGBA_SYNC_RATE_150_MHZ   150000u

/* Scale a duration measured across (total_strips - 1) strip intervals up to
 * the full `total_strips` frame duration. */
static inline uint32_t cgba_sync_scale_strips(uint32_t span,
	uint32_t total_strips)
{
	if(total_strips < 2u)
		return 0u;
	return (uint32_t)(((uint64_t)span * total_strips) / (total_strips - 1u));
}

/* Smooth a per-mode frame write duration upper bound (rows or TMU ticks):
 * follow increases quickly so slower frames tighten the upper sync edge right
 * away, and decay decreases slowly. Readings outside (0, max_valid] (e.g. OS
 * world-switch spikes) are ignored. */
static inline uint32_t cgba_sync_smooth(uint32_t cur, uint32_t measured,
	uint32_t max_valid)
{
	if(!measured || !max_valid || measured > max_valid)
		return cur;
	if(!cur)
		return measured;
	if(measured >= cur)
		return (cur + measured + 1u) / 2u;
	return (cur * 7u + measured + 4u) / 8u;
}

/* Smooth a per-mode frame write duration lower bound: follow decreases
 * quickly so fast frames tighten the lower sync edge (preventing fast DMA
 * from overtaking the scan beam in the bottom half of the screen), and let
 * increases rise slowly. */
static inline uint32_t cgba_sync_smooth_min(uint32_t cur, uint32_t measured,
	uint32_t max_valid)
{
	if(!measured || !max_valid || measured > max_valid)
		return cur;
	if(!cur)
		return measured;
	if(measured <= cur)
		return (cur + measured) / 2u;
	return (cur * 7u + measured + 4u) / 8u;
}

/* ---- joint (VFP, RTN) timing solver -------------------------------------- */

/* Panel timing geometry for period ~ (visible_rows + bp_rows + fp) *
 * (rtn + rtn_bias). Hardware measurements on ClassPad II (0x16, HZPROBE.TXT,
 * 41.64..98.75 Hz across RTN 0xFD..0x32) fit line_clocks = RTN + 98 with
 * <= 0.02 Hz residual; the same constant matches fx-CG100 OS 2.10's 0x32/0x52
 * stock timing (375 rows * (255 + 98) at 8 MHz -> ~60.4 Hz). */
#define CGBA_HIMAX_RTN_BIAS      98u

typedef struct cgba_timing_spec {
	uint32_t visible_rows;
	uint32_t bp_rows;
	uint32_t orig_fp;
	uint32_t orig_rtn;
	uint32_t rtn_bias;
	uint32_t min_fp;
	uint32_t max_fp;
	uint32_t min_rtn;
	uint32_t max_rtn;
} cgba_timing_spec;

/* Jointly solve (fp, rtn) in [min_fp..max_fp] x [min_rtn..max_rtn] to hit
 * `target_period` as closely as possible relative to `orig_period` measured
 * at (orig_fp, orig_rtn). Ties prefer slightly faster-than-target periods
 * (so a 60 fps limiter never falls behind) and smaller fp. */
static inline void cgba_sync_solve_vfp_rtn(const cgba_timing_spec *spec,
	uint32_t orig_period, uint32_t target_period,
	uint32_t *fp_out, uint32_t *rtn_out, uint32_t *total_out)
{
	uint32_t orig_total = spec->visible_rows + spec->bp_rows + spec->orig_fp;
	uint32_t orig_line = spec->orig_rtn + spec->rtn_bias;
	uint64_t orig_clocks = (uint64_t)orig_total * orig_line;
	uint64_t best_err = ~0ull;
	uint32_t best_fp = spec->orig_fp;
	uint32_t best_rtn = spec->orig_rtn;
	int best_fast = 0;
	uint32_t fp;

	*fp_out = spec->orig_fp;
	*rtn_out = spec->orig_rtn;
	*total_out = orig_total;

	if(!orig_period || !target_period || !orig_clocks)
		return;

	for(fp = spec->min_fp; fp <= spec->max_fp; fp++) {
		uint32_t total = spec->visible_rows + spec->bp_rows + fp;
		uint64_t ideal_clocks = (orig_clocks * target_period +
			(orig_period / 2u)) / orig_period;
		uint32_t ideal_line = (uint32_t)((ideal_clocks + (total / 2u)) / total);
		int base_rtn = (int)ideal_line - (int)spec->rtn_bias;
		int d;

		for(d = -1; d <= 1; d++) {
			int r = base_rtn + d;
			uint32_t rtn, cand_period;
			uint64_t err;
			int is_fast;

			if(r < (int)spec->min_rtn)
				rtn = spec->min_rtn;
			else if(r > (int)spec->max_rtn)
				rtn = spec->max_rtn;
			else
				rtn = (uint32_t)r;

			cand_period = (uint32_t)(((uint64_t)orig_period * total *
				(rtn + spec->rtn_bias) + (orig_clocks / 2u)) / orig_clocks);
			err = (cand_period >= target_period)
				? (uint64_t)(cand_period - target_period)
				: (uint64_t)(target_period - cand_period);
			is_fast = (cand_period <= target_period);

			if(err < best_err ||
			   (err == best_err && is_fast && !best_fast)) {
				best_err = err;
				best_fp = fp;
				best_rtn = rtn;
				best_fast = is_fast;
			}
		}
	}

	*fp_out = best_fp;
	*rtn_out = best_rtn;
	*total_out = spec->visible_rows + spec->bp_rows + best_fp;
}

/* Frequency-domain wrapper around cgba_sync_solve_vfp_rtn(): since
 * period ~ 1/freq, target_period / orig_period == stock_mhz / target_mhz. */
static inline void cgba_sync_solve_vfp_rtn_mhz(const cgba_timing_spec *spec,
	uint32_t stock_mhz, uint32_t target_mhz,
	uint32_t *fp_out, uint32_t *rtn_out, uint32_t *total_out)
{
	cgba_sync_solve_vfp_rtn(spec, target_mhz, stock_mhz,
		fp_out, rtn_out, total_out);
}

/* ---- Himax-family (0xB9: FF 52 98) 0xB2 / 0xDF payload builders ---------- */

/* Verified against Casio OS 02.01.7003 @ 0x8003887e (ClassPad II 0x16/0x52)
 * and Casio OS 02.10 @ 0x80074ab8 (fx-CG100 0x32/0x52). Both write the exact
 * same 6-byte 0xB2 and 12-byte 0xDF layout, differing only in geometry. */
typedef struct cgba_himax_panel {
	uint8_t b2_p1;     /* 0x34 ClassPad, 0x47 fx-CG100 */
	uint8_t b2_p2;     /* 0x26 ClassPad, 0x00 fx-CG100 */
	uint8_t vbp;       /* 0x08 ClassPad, 0x4C fx-CG100 */
	uint8_t stock_vfp; /* 0x08 ClassPad, 0x4B fx-CG100 */
	uint8_t stock_rtn; /* 0x9D ClassPad, 0xFF fx-CG100 */
} cgba_himax_panel;

static inline cgba_himax_panel cgba_himax_classpad(void)
{
	cgba_himax_panel p = { 0x34u, 0x26u, 0x08u, 0x08u, 0x9du };
	return p;
}

static inline cgba_himax_panel cgba_himax_fxcg100(void)
{
	cgba_himax_panel p = { 0x47u, 0x00u, 0x4cu, 0x4bu, 0xffu };
	return p;
}

static inline void cgba_himax_build_b2(cgba_himax_panel panel,
	uint8_t vfp, uint8_t rtn, uint8_t out[6])
{
	out[0] = 0x00u;
	out[1] = panel.b2_p1;
	out[2] = panel.b2_p2;
	out[3] = panel.vbp;
	out[4] = vfp;
	out[5] = rtn;
}

static inline void cgba_himax_build_df(cgba_himax_panel panel,
	uint8_t vfp, uint8_t rtn, uint8_t out[12])
{
	out[0]  = 0x08u;
	out[1]  = 0x00u;
	out[2]  = 0x73u;
	out[3]  = 0x33u;
	out[4]  = 0x2au;
	out[5]  = 0x77u;
	out[6]  = 0x73u;
	out[7]  = 0x77u;
	out[8]  = 0x73u;
	out[9]  = panel.vbp;
	out[10] = vfp;
	out[11] = rtn;
}

#endif /* CGBA_LCD_SYNC_COMMON_H */
