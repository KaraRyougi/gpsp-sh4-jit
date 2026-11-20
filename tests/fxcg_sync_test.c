/* Unit tests for ports/fxcg100/fxcg_sync.h (fx-CG100 / fx-CG50 R61524 VSync).
 *
 * Simulates the R61524 top-down scanline beam (measured 240 rows = 8 BP + 224
 * visible + 8 FP at 61.154 Hz) against strip-DMA writes across all three
 * scaling geometries and both fast-DMA and slow-DMA regimes, verifying that
 * every start inside cg_sync_safe_window() is tear-free. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/fxcg_sync.h"

static int failures;
#define CHECK(cond, ...) do { if(!(cond)) { \
	printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
	printf("\n"); failures++; } } while(0)

/* Simulate one frame written linearly top-to-bottom across [y0, y0 + h) taking
 * `write_ticks`, starting at modular phase `start_phase` within a `period`-tick
 * refresh (bp_rows back porch before row 0, total_rows total).
 * Returns 1 if the panel beam crosses the active write row inside [y0, y0+h). */
static int sim_tears(uint32_t period, uint32_t total_rows, uint32_t bp_rows,
	uint32_t y0, uint32_t h, uint32_t write_ticks, uint32_t start_phase)
{
	const uint32_t steps = 2048u;
	uint32_t s;
	int have_prev = 0;
	double prev_diff = 0.0;

	for(s = 0; s <= steps; s++) {
		double t = (double)start_phase + (double)write_ticks * s / steps;
		double scan_pos = (t * (double)total_rows) / (double)period;
		/* Modular scanline within [0, total_rows) */
		double scan_mod = scan_pos - (double)((uint64_t)(scan_pos / total_rows)) * total_rows;
		double write_pos = (double)bp_rows + (double)y0 + (double)h * s / steps;
		/* Signed shortest modular distance: scan_mod - write_pos in [-total/2, total/2) */
		double diff = scan_mod - write_pos;

		while(diff >= (double)total_rows * 0.5) diff -= (double)total_rows;
		while(diff < -(double)total_rows * 0.5) diff += (double)total_rows;

		if(have_prev) {
			/* Crossing zero without wrapping the far side = beam met write */
			if((prev_diff <= 0.0 && diff >= 0.0 && (diff - prev_diff) < (double)total_rows * 0.25) ||
			   (prev_diff >= 0.0 && diff <= 0.0 && (prev_diff - diff) < (double)total_rows * 0.25))
				return 1;
		}
		prev_diff = diff;
		have_prev = 1;
	}
	return 0;
}

static void test_r008_parse(void)
{
	uint32_t bp = 0, total = 0;

	cg_sync_parse_r008(0x0808u, &bp, &total);
	CHECK(bp == 8u && total == 240u, "0x0808 -> bp=%u total=%u", bp, total);

	cg_sync_parse_r008(0x0A0Cu, &bp, &total);
	CHECK(bp == 12u && total == 246u, "0x0A0C (FP=10, BP=12) -> bp=%u total=%u", bp, total);

	/* Out-of-range values fall back safely to measured defaults. */
	cg_sync_parse_r008(0x0000u, &bp, &total);
	CHECK(bp == 8u && total == 240u, "0x0000 fallback");

	cg_sync_parse_r008(0xFFFFu, &bp, &total);
	CHECK(bp == 8u && total == 240u, "0xFFFF fallback");

	/* Hardware-verified 5-bit BP in low byte (0x0818 = FP 8, BP 24 -> 256 rows). */
	cg_sync_parse_r008(0x0818u, &bp, &total);
	CHECK(bp == 24u && total == 256u, "0x0818 -> bp=%u total=%u", bp, total);
}

static void test_timing_solver(void)
{
	uint16_t r8 = 0, r10 = 0;
	uint32_t bp = 0, total = 0;

	/* Measured fx-CG100 stock: 0x0808 / 0x0036 -> 16355.9 us (61.140 Hz).
	 * 1x GBA 59.7275 Hz (16743 us) -> 255 * 52 (0x0817 / 0x0034, BP=23) = 59.757 Hz! */
	cg_sync_solve_timing(0x0808u, 0x0036u, 16356u, 16743u,
		&r8, &r10, &bp, &total);
	CHECK(r8 == 0x0817u && r10 == 0x0034u && bp == 23u && total == 255u,
		"1x GBA: got %04X/%04X bp=%u total=%u", r8, r10, bp, total);

	/* 2x GBA 119.455 Hz (8371 us) -> 255 * 26 (0x0817 / 0x001A, BP=23) = 119.514 Hz! */
	cg_sync_solve_timing(0x0808u, 0x0036u, 16356u, 8371u,
		&r8, &r10, &bp, &total);
	CHECK(r8 == 0x0817u && r10 == 0x001Au && bp == 23u && total == 255u,
		"2x GBA: got %04X/%04X bp=%u total=%u", r8, r10, bp, total);

	/* 75 Hz (13333 us) -> 240 * 44 (0x0808 / 0x002C) = 75.038 Hz. */
	cg_sync_solve_timing(0x0808u, 0x0036u, 16356u, 13333u,
		&r8, &r10, &bp, &total);
	CHECK(r8 == 0x0808u && r10 == 0x002Cu && bp == 8u && total == 240u,
		"75Hz: got %04X/%04X bp=%u total=%u", r8, r10, bp, total);

	/* 150 Hz (6667 us) -> 240 * 22 (0x0808 / 0x0016) = 150.076 Hz. */
	cg_sync_solve_timing(0x0808u, 0x0036u, 16356u, 6667u,
		&r8, &r10, &bp, &total);
	CHECK(r8 == 0x0808u && r10 == 0x0016u && bp == 8u && total == 240u,
		"150Hz: got %04X/%04X bp=%u total=%u", r8, r10, bp, total);

	/* BP is never lowered below stock and FP/RTNI respect hardware bounds. */
	cg_sync_solve_timing(0x0808u, 0x0036u, 16356u, 3000u,
		&r8, &r10, &bp, &total);
	CHECK((r8 >> 8) == 8u && (r8 & 0x1fu) >= 8u && (r8 & 0x1fu) <= 24u,
		"fast clamp R008 out of bounds: %04X", r8);
	CHECK((r10 & 0x3fu) >= CG_SYNC_MIN_TUNE_RTNI &&
	      (r10 & 0x3fu) <= CG_SYNC_MAX_TUNE_RTNI,
		"fast clamp R010 out of bounds: %04X", r10);

	/* Target millihertz mapping for Auto / System / explicit presets. */
	CHECK(cg_sync_target_mhz(0u, 0u) == 0u, "Auto + VSync Off -> stock");
	CHECK(cg_sync_target_mhz(0u, 1u) == CG_SYNC_RATE_GBA_MHZ,
		"Auto + VSync On -> 59.73 Hz");
	CHECK(cg_sync_target_mhz(1u, 1u) == 0u, "System + VSync On -> stock");
	CHECK(cg_sync_target_mhz(2u, 0u) == CG_SYNC_RATE_GBA_MHZ,
		"59.73 Hz (GBA) explicit");
	CHECK(cg_sync_target_mhz(5u, 0u) == CG_SYNC_RATE_120_MHZ,
		"120 Hz explicit");
	CHECK(cg_sync_target_mhz(6u, 0u) == CG_SYNC_RATE_150_MHZ,
		"150 Hz explicit");

	/* OS 2.10 Himax-family panel (0x32/0x52): stock 375 rows * (255 + 98)
	 * at 8 MHz -> ~60.43 Hz (16547 us). Solve 1x GBA, 2x GBA, and 150 Hz. */
	{
		uint8_t vfp = 0, rtn = 0, b2[6], df[12];
		uint32_t htotal = 0;

		cg_sync_solve_himax(16547u, 16743u, &vfp, &rtn, &htotal);
		CHECK(vfp >= CG_HIMAX_STOCK_VFP && vfp <= CG_HIMAX_MAX_TUNE_VFP &&
			rtn >= CG_HIMAX_MIN_TUNE_RTN && rtn <= CG_HIMAX_MAX_TUNE_RTN &&
			htotal == 224u + CG_HIMAX_VBP + vfp,
			"Himax 1x GBA bounds: vfp=%u rtn=%u total=%u", vfp, rtn, htotal);
		cg_sync_solve_himax(16547u, 8371u, &vfp, &rtn, &htotal);
		CHECK(vfp >= CG_HIMAX_STOCK_VFP && rtn >= 60u && rtn <= 100u,
			"Himax 2x GBA: vfp=%u rtn=%u total=%u", vfp, rtn, htotal);
		cg_sync_solve_himax(16547u, 6667u, &vfp, &rtn, &htotal);
		CHECK(rtn >= CG_HIMAX_MIN_TUNE_RTN && rtn <= 60u,
			"Himax 150 Hz: vfp=%u rtn=%u total=%u", vfp, rtn, htotal);

		/* Shared payload matches fx-CG100 OS 2.10 @ 0x80074ab8. */
		cgba_himax_build_b2(cgba_himax_fxcg100(), 0x4bu, 0xffu, b2);
		cgba_himax_build_df(cgba_himax_fxcg100(), 0x4bu, 0xffu, df);
		CHECK(b2[0] == 0x00u && b2[1] == 0x47u && b2[2] == 0x00u &&
			b2[3] == 0x4cu && b2[4] == 0x4bu && b2[5] == 0xffu,
			"fx-CG100 B2 payload mismatch");
		CHECK(df[0] == 0x08u && df[4] == 0x2au && df[9] == 0x4cu &&
			df[10] == 0x4bu && df[11] == 0xffu,
			"fx-CG100 DF payload mismatch");
	}
}

static void test_safe_windows_all_modes(void)
{
	/* Measured fx-CG100 TMU ticks at Pphi/4 = 7.3728 MHz, 16351.9 us. */
	const uint32_t period = 120560u;
	const uint32_t total = 240u;
	const uint32_t bp = 8u;
	static const struct { const char *name; uint32_t y0, h; } modes[] = {
		{ "1:1",   32u, 160u },
		{ "4:3",    4u, 212u },
		{ "full",   4u, 216u },
		{ "panel",  0u, 224u },
	};
	/* Write durations spanning 2 ms (F5 1:1) up to 18 ms (slow CG50). */
	static const uint32_t write_ms[] = { 2u, 4u, 6u, 8u, 11u, 14u, 16u, 18u };
	unsigned m, w;

	for(m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
		for(w = 0; w < sizeof(write_ms) / sizeof(write_ms[0]); w++) {
			uint32_t write_ticks = (uint32_t)((uint64_t)period * write_ms[w] * 1000u / 16352u);
			uint32_t lo = 0, len = 0;
			int ok = cg_sync_safe_window(period, total, bp,
				modes[m].y0, modes[m].h, write_ticks, &lo, &len);

			if(write_ms[w] <= 16u) {
				CHECK(ok == 1, "%s %ums should have a safe window",
					modes[m].name, write_ms[w]);
			}
			if(!ok)
				continue;

			/* Check 64 start phases across [lo, lo + len). */
			unsigned k;
			for(k = 0; k < 64u; k++) {
				uint32_t ph = (lo + (uint32_t)((uint64_t)len * k / 64u)) % period;
				uint32_t wait = cg_sync_wait_needed(ph, period, lo, len);

				CHECK(wait == 0u, "%s %ums phase %u inside window waited %u",
					modes[m].name, write_ms[w], ph, wait);
				CHECK(!sim_tears(period, total, bp, modes[m].y0, modes[m].h,
						write_ticks, ph),
					"%s %ums tore at safe phase %u (lo=%u len=%u)",
					modes[m].name, write_ms[w], ph, lo, len);
			}

			/* Check wait_needed from every forbidden phase lands on safe_lo. */
			for(k = 0; k < 16u; k++) {
				uint32_t flen = period - len;
				uint32_t ph = (lo + len + (uint32_t)((uint64_t)flen * k / 16u)) % period;
				uint32_t wait = cg_sync_wait_needed(ph, period, lo, len);
				uint32_t landed = (ph + wait) % period;

				CHECK(wait > 0u && wait <= flen,
					"%s %ums forbidden wait %u out of range (flen=%u)",
					modes[m].name, write_ms[w], wait, flen);
				CHECK(landed == lo,
					"%s %ums landed=%u expected lo=%u",
					modes[m].name, write_ms[w], landed, lo);
			}
		}
	}

	/* Hardware-verified anchor: full 396x224 DMA (11.0 ms) at 0 ms delay is clean. */
	{
		uint32_t lo = 0, len = 0;
		uint32_t w11 = (uint32_t)((uint64_t)period * 11000u / 16352u);
		int ok = cg_sync_safe_window(period, total, bp, 0u, 224u, w11, &lo, &len);
		CHECK(ok == 1, "hardware 11ms window exists");
		CHECK(cg_sync_wait_needed(0u, period, lo, len) == 0u,
			"phase 0 ms must be inside safe window (lo=%u len=%u)", lo, len);
		CHECK(!sim_tears(period, total, bp, 0u, 224u, w11, 0u),
			"phase 0 ms must not tear");
	}
}

static void test_tcnt_wrap_and_drift(void)
{
	cg_sync_state s;
	uint32_t ph;

	memset(&s, 0, sizeof(s));
	s.enabled = 1u;
	s.period_ticks = 120560u;
	s.total_rows = 240u;
	s.bp_rows = 8u;

	/* Anchor near 0 so the down-counter wraps across 0 -> 0xFFFFFFFF. */
	cg_sync_note_edge(&s, 1000u);
	CHECK(s.anchor_valid == 1, "anchor valid after first edge");

	/* 2000 ticks later: now_tcnt = 1000 - 2000 = 0xFFFFFC18. */
	ph = cg_sync_phase_from_tcnt(s.anchor_tcnt, (uint32_t)(1000u - 2000u),
		s.period_ticks);
	CHECK(ph == 2000u, "wraparound phase=%u expected 2000", ph);

	/* Slight oscillator drift (+160 ticks) should be tracked by EMA. */
	cg_sync_note_edge(&s, (uint32_t)(1000u - 120720u));
	CHECK(s.period_ticks > 120560u && s.period_ticks <= 120720u,
		"EMA tracked drift: %u", s.period_ticks);

	/* Missed frame (2x period) must re-anchor without corrupting period_ticks. */
	{
		uint32_t before = s.period_ticks;
		cg_sync_note_edge(&s, s.anchor_tcnt - before * 2u);
		CHECK(s.period_ticks == before,
			"2x gap corrupted period: %u -> %u", before, s.period_ticks);
	}

	/* Soft vs hard resync thresholds. */
	CHECK(!cg_sync_wants_soft_resync(&s, s.anchor_tcnt - s.period_ticks * 10u),
		"fresh anchor should not soft-resync");
	CHECK(cg_sync_wants_soft_resync(&s,
			s.anchor_tcnt - s.period_ticks * CG_SYNC_SOFT_RESYNC_PERIODS),
		"45-frame anchor triggers soft resync");
	CHECK(!cg_sync_needs_resync(&s,
			s.anchor_tcnt - s.period_ticks * CG_SYNC_SOFT_RESYNC_PERIODS),
		"45-frame anchor must not force blocking hard resync");
	CHECK(cg_sync_needs_resync(&s,
			s.anchor_tcnt - s.period_ticks * CG_SYNC_RESYNC_PERIODS),
		"stale anchor must trigger hard resync");

	/* Write-time estimator tracks increases fast and decays decreases slowly. */
	cg_sync_update_write_ticks(&s, 0u, 30000u);
	CHECK(s.write_ticks[0] == 30000u, "first sample sets estimate");
	cg_sync_update_write_ticks(&s, 0u, 50000u);
	CHECK(s.write_ticks[0] == 40000u, "increase tracks fast: %u", s.write_ticks[0]);
	cg_sync_update_write_ticks(&s, 0u, 32000u);
	CHECK(s.write_ticks[0] == 39000u, "decrease decays slowly: %u", s.write_ticks[0]);
	/* Bogus world-switch spike is ignored. */
	cg_sync_update_write_ticks(&s, 0u, s.period_ticks * 5u);
	CHECK(s.write_ticks[0] == 39000u, "spike ignored: %u", s.write_ticks[0]);
	/* Lower envelope tracks fast frames while upper envelope stays high. */
	CHECK(s.write_min_ticks[0] < s.write_ticks[0],
		"write_min=%u should stay below write_max=%u",
		s.write_min_ticks[0], s.write_ticks[0]);

	/* Multi-period dt/k locking refines period_ticks after a 45-frame gap
	 * with a slight oscillator drift (+100 ticks/frame). */
	{
		cg_sync_state d;
		uint32_t true_per = 120660u;

		memset(&d, 0, sizeof(d));
		d.enabled = 1u;
		d.period_ticks = 120560u;
		cg_sync_note_edge(&d, 0x80000000u);
		cg_sync_note_edge(&d, 0x80000000u - true_per * 45u);
		CHECK(d.period_ticks == 120610u,
			"multi-period dt/k lock step 1: got %u", d.period_ticks);
		cg_sync_note_edge(&d, 0x80000000u - true_per * 90u);
		CHECK(d.period_ticks >= 120635u && d.period_ticks <= true_per,
			"multi-period dt/k lock step 2: got %u (true %u)",
			d.period_ticks, true_per);
	}

	/* Dual [write_min, write_max] safe window with real R008=0x0817 (BP=23,
	 * FP=8, 255 rows) prevents fast frames from overtaking the scan beam at
	 * the very bottom rows of Full Screen (y0=4, h=216). */
	{
		uint32_t period = 123400u; /* 59.73 Hz, 255 rows (23 BP + 224 + 8 FP) */
		uint32_t wmin = period * 75u / 167u;  /* 7.5 ms fast frame */
		uint32_t wmax = period * 115u / 167u; /* 11.5 ms slow frame */
		uint32_t lo = 0, len = 0, k;
		int ok = cg_sync_safe_window_range(period, 255u, 23u, 4u, 216u,
			wmin, wmax, &lo, &len);

		CHECK(ok == 1, "dual-envelope 59.73 Hz Full Screen window exists");
		CHECK(cg_sync_wait_needed(0u, period, lo, len) == 0u,
			"PR.5 rising edge (phase 0) stays inside BP=23 safe window");
		for(k = 0; k < 64u && ok; k++) {
			uint32_t ph = (lo + (uint32_t)((uint64_t)len * k / 64u)) % period;
			CHECK(!sim_tears(period, 255u, 23u, 4u, 216u, wmin, ph),
				"fast frame tore in dual window at phase %u", ph);
			CHECK(!sim_tears(period, 255u, 23u, 4u, 216u, wmax, ph),
				"slow frame tore in dual window at phase %u", ph);
		}
	}

	/* 1:1 panel-locked TMU pacer (cg_sync_pace_advance): verify early wait,
	 * 32-bit down-counter wraparound, overrun late accounting, and clamp. */
	{
		cg_sync_state p;
		int late = -99;
		uint32_t wait;

		memset(&p, 0, sizeof(p));
		p.period_ticks = 1000u;

		/* First call anchors deadline with zero wait and late=0. */
		wait = cg_sync_pace_advance(&p, 500u, 3, &late);
		CHECK(wait == 0u && late == 0 && p.pace_valid && p.pace_due_tcnt == 500u,
			"pace init: wait=%u late=%d due=%u", wait, late, p.pace_due_tcnt);

		/* Next frame finishes after 600 ticks (wrapping across 0 -> 0xFFFFFF9C):
		 * remaining = 400 ticks until due = 500 - 1000 = 0xFFFFFE0C. */
		wait = cg_sync_pace_advance(&p, (uint32_t)(500u - 600u), 3, &late);
		CHECK(wait == 400u && late == 0,
			"pace early wrap: wait=%u late=%d", wait, late);

		/* Frame overruns by 0.2 refreshes (1200 ticks after prior deadline):
		 * matches ETMU limiter (late == 0 until a full extra refresh is lost). */
		wait = cg_sync_pace_advance(&p, p.pace_due_tcnt - 1200u, 3, &late);
		CHECK(wait == 0u && late == 0,
			"pace 0.2 overrun: wait=%u late=%d", wait, late);

		/* Frame overruns by 1.3 more refreshes -> late == 1. */
		wait = cg_sync_pace_advance(&p, p.pace_due_tcnt - 2300u, 3, &late);
		CHECK(wait == 0u && late == 1,
			"pace 1.3 overrun: wait=%u late=%d", wait, late);

		/* Huge stall (6 refreshes behind) clamps to max_late+1 (4) and forgets
		 * excess time beyond max_late (3). */
		{
			uint32_t now = p.pace_due_tcnt - 7000u;
			wait = cg_sync_pace_advance(&p, now, 3, &late);
			CHECK(wait == 0u && late == 4 &&
				p.pace_due_tcnt == now + 3000u,
				"pace clamp: wait=%u late=%d due=%u expected=%u",
				wait, late, p.pace_due_tcnt, now + 3000u);
		}
	}
}

int main(void)
{
	test_r008_parse();
	test_timing_solver();
	test_safe_windows_all_modes();
	test_tcnt_wrap_and_drift();
	if(failures) {
		printf("fxcg_sync_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("fxcg_sync_test: OK\n");
	return 0;
}
