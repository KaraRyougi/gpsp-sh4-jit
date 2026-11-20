/* classpad_sync.h against a simulated panel: a row counter that advances at
 * a fixed rate while a frame is written strip by strip at another. A frame
 * started inside the window the header works out must not tear; started
 * anywhere, the tracker must say whether it did. */
#include <stdint.h>
#include <stdio.h>

#include "ports/fxcg100/classpad_sync.h"

static int failures;
#define CHECK(cond, ...) do { if(!(cond)) { \
	printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
	printf("\n"); failures++; } } while(0)

#define TOTAL 560u       /* rows in a refresh, blanking included */

/* Write a picture of `rows` rows from panel row `top` in `strips` strips
 * while the panel shows `lines` rows, starting with the shown row `at` rows
 * past `top`. Returns whether the shown row and the written row met inside
 * the picture (the real thing, worked out in fine steps), and runs the
 * tracker the way the presenter does. */
static int frame(uint32_t top, uint32_t rows, uint32_t lines, int descending,
	uint32_t strips, uint32_t at, cp_sync_track *t)
{
	const uint32_t steps = 4096u;
	uint32_t i, tore = 0;
	long prev = 0;

	cp_sync_track_begin(t, TOTAL);
	for(i = 0; i <= steps; i++) {
		/* in 1/steps of a row, to keep to integers */
		uint64_t shown = (uint64_t)at * steps + (uint64_t)lines * i;
		uint64_t written = descending
			? (uint64_t)rows * (steps - i) : (uint64_t)rows * i;
		uint64_t whole = (uint64_t)TOTAL * steps;
		long gap = (long)(shown % whole) - (long)written;

		if(i && ((gap < 0) != (prev < 0)) &&
				(gap < 0 ? -gap : gap) < (long)(whole / 2) &&
				(prev < 0 ? -prev : prev) < (long)(whole / 2))
			tore = 1;
		prev = gap;
	}
	for(i = 0; i < strips; i++) {
		uint32_t shown = (top + at + lines * i / strips) % TOTAL;
		uint32_t row = descending ? top + rows - rows * (i + 1u) / strips
			: top + rows * i / strips;

		cp_sync_track_strip(t, shown, row);
	}
	return (int)tore;
}

/* As the presenter really writes: `strips` strips going the way of the
 * refresh, each one filled from its top row down, against it, in its share
 * of the `lines` rows shown while the frame goes out. The refresh starts
 * `at` rows past the picture's first row (in the order it shows them) on a
 * panel of `total` rows. Returns whether one sweep of the refresh showed
 * part of the picture new and part old. Times are in rows shown. */
static int frame_in_strips(uint32_t total, uint32_t rows, uint32_t lines,
	uint32_t strips, uint32_t at)
{
	uint32_t h = (rows + strips - 1u) / strips;
	double each = (double)lines / strips;
	long m;

	for(m = -1; m <= (long)(lines / total) + 2; m++) {
		int seen_new = 0, seen_old = 0;
		uint32_t p;

		for(p = 0; p < rows; p++) {
			uint32_t j = p / h;
			uint32_t end = (j + 1u) * h < rows ? (j + 1u) * h : rows;
			double written = j * each
				+ ((double)(end - 1u - p) + 0.5) * each / (end - j * h);
			double shown = (double)p - (double)at + (double)m * total;

			if(shown > written)
				seen_new = 1;
			else
				seen_old = 1;
		}
		if(seen_new && seen_old)
			return 1;
	}
	return 0;
}

/* As on a top-down panel (R61523 with B7=0, B4=0): strips go top-to-bottom
 * and each strip's window is also filled top-to-bottom, with the refresh. */
static int frame_in_strips_same_dir(uint32_t total, uint32_t rows,
	uint32_t lines, uint32_t strips, uint32_t at)
{
	uint32_t h = (rows + strips - 1u) / strips;
	double each = (double)lines / strips;
	long m;

	for(m = -1; m <= (long)(lines / total) + 2; m++) {
		int seen_new = 0, seen_old = 0;
		uint32_t p;

		for(p = 0; p < rows; p++) {
			uint32_t j = p / h;
			uint32_t end = (j + 1u) * h < rows ? (j + 1u) * h : rows;
			double written = j * each
				+ ((double)(p - j * h) + 0.5) * each / (end - j * h);
			double shown = (double)p - (double)at + (double)m * total;

			if(shown > written)
				seen_new = 1;
			else
				seen_old = 1;
		}
		if(seen_new && seen_old)
			return 1;
	}
	return 0;
}

int main(void)
{
	/* picture height, rows shown while it is written, strips, upside down */
	static const struct { uint32_t rows, lines, strips; int down; } cases[] = {
		{ 160, 110, 10, 0 },   /* portrait 1:1, written faster than shown */
		{ 160, 150, 10, 1 },   /* the same held upside down */
		{ 212, 260, 18, 0 },   /* portrait 4:3 */
		{ 212, 260, 18, 1 },
		{ 240, 170, 15, 0 },   /* landscape 1:1 */
		{ 360, 420, 24, 0 },   /* landscape 1.5x */
		{ 480, 700, 40, 0 },   /* landscape 2x: more than one refresh */
		{ 480, 470, 40, 0 },
	};
	unsigned c;
	cp_sync_track t;
	cp_scan_probe probe = { 0 };
	uint32_t hz, i;

	for(c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		uint32_t rows = cases[c].rows, lines = cases[c].lines;
		uint32_t lo, hi, at, top = 24u + 7u * c;
		int open = cp_sync_window(TOTAL, rows, lines, cases[c].down, &lo, &hi);
		unsigned inside = 0, caught = 0, tears = 0;

		CHECK(open, "case %u: no window", c);
		for(at = 0; at < TOTAL; at++) {
			int tore = frame(top, rows, lines, cases[c].down,
				cases[c].strips, at, &t);

			tears += (unsigned)tore;
			if(open && at >= lo && at < hi) {
				inside++;
				CHECK(!tore, "case %u: tore from %u, inside [%u, %u)", c,
					(unsigned)at, (unsigned)lo, (unsigned)hi);
				CHECK(!t.crossed, "case %u: tracker saw a tear from %u", c,
					(unsigned)at);
			}
			if(tore && t.crossed)
				caught++;
		}
		CHECK(inside > 0, "case %u: empty window", c);
		/* The tracker looks once a strip, so it may miss a meeting in the
		 * first or last strip; it must catch nearly all. */
		CHECK(tears == 0 || caught * 10u >= tears * 8u,
			"case %u: tracker caught %u of %u tears", c, caught, tears);

		/* And it measures the rows shown during the frame. */
		frame(top, rows, lines, cases[c].down, cases[c].strips, lo, &t);
		{
			uint32_t got = cp_sync_track_lines(&t);

			CHECK(got + 24u >= lines && got <= lines + 24u,
				"case %u: measured %u rows, %u were shown", c,
				(unsigned)got, (unsigned)lines);
		}
	}

	/* Strips filled against the refresh, on a ClassPad II's 548-row refresh:
	 * the calibration page (528 rows in 16-row bands) and game pictures.
	 * The clearance is there for an offset that is out (up to half a step
	 * while it is being set), so the strips must not use it up: with no
	 * clearance at all, every start inside the window kept with the
	 * strip's height and time must be clean, and without them some must
	 * tear. */
	{
		static const struct { uint32_t rows, lines, strips, part; } sc[] = {
			{ 528, 280, 33, CP_SYNC_PART_CALIBRATE },
			{ 528, 400, 33, CP_SYNC_PART_CALIBRATE },
			{ 528, 600, 33, CP_SYNC_PART_CALIBRATE },
			{ 212, 260, 18, CP_SYNC_PART_GAME },
			{ 240, 170, 15, CP_SYNC_PART_GAME },
		};
		const uint32_t total = 548u, none = total + 1u;

		for(c = 0; c < sizeof(sc) / sizeof(sc[0]); c++) {
			uint32_t strip = (sc[c].rows + sc[c].strips - 1u) / sc[c].strips;
			uint32_t lo, hi, at, torn = 0, bare_torn = 0;

			CHECK(cp_sync_window_part(total, sc[c].rows, sc[c].lines, 0,
				sc[c].part, strip, &lo, &hi), "strip case %u: no window", c);
			if(!cp_sync_window_part(total, sc[c].rows, sc[c].lines, 0,
					none, strip, &lo, &hi)) {
				CHECK(0, "strip case %u: no window without clearance", c);
				continue;
			}
			for(at = lo; at < hi; at++)
				torn += (unsigned)frame_in_strips(total, sc[c].rows,
					sc[c].lines, sc[c].strips, at);
			CHECK(torn == 0, "strip case %u: %u starts in [%u, %u) tore", c,
				(unsigned)torn, (unsigned)lo, (unsigned)hi);
			if(cp_sync_window_part(total, sc[c].rows, sc[c].lines, 0, none,
					0, &lo, &hi))
				for(at = lo; at < hi; at++)
					bare_torn += (unsigned)frame_in_strips(total,
						sc[c].rows, sc[c].lines, sc[c].strips, at);
			CHECK(bare_torn > 0, "strip case %u: the window without the "
				"strips' height and time did not tear", c);
		}
	}

	/* Setting the offset: frames start at the early and the late end of the
	 * window in turn. The right offset is clean at both; one step out
	 * either way (a picture that really starts 34 rows later or earlier
	 * than the offset says) tears at one of them. Starting wherever the
	 * window opens, as before, a step too high stayed clean. */
	{
		static const uint32_t lines_cases[] = { 250, 280, 400, 600 };
		const uint32_t total = 548u, rows = 528u, strips = 33u, strip = 16u;
		const uint32_t step = total / 16u;

		for(c = 0; c < sizeof(lines_cases) / sizeof(lines_cases[0]); c++) {
			uint32_t lines = lines_cases[c], lo, hi, elo, ehi, llo, lhi;
			uint32_t early, late;

			if(!cp_sync_window_part(total, rows, lines, 0,
					CP_SYNC_PART_CALIBRATE, strip, &lo, &hi)) {
				CHECK(0, "calibration %u: no window", (unsigned)lines);
				continue;
			}
			elo = llo = lo;
			ehi = lhi = hi;
			cp_sync_window_end(&elo, &ehi, 0);
			cp_sync_window_end(&llo, &lhi, 1);
			CHECK(elo == lo && ehi == lo + CP_SYNC_END_ROWS &&
				llo == hi - CP_SYNC_END_ROWS && lhi == hi,
				"calibration %u: ends [%u, %u) and [%u, %u) of [%u, %u)",
				(unsigned)lines, (unsigned)elo, (unsigned)ehi,
				(unsigned)llo, (unsigned)lhi, (unsigned)lo, (unsigned)hi);
			/* A frame starts at the first row of the end it waits for. */
			early = elo;
			late = llo;

			CHECK(!frame_in_strips(total, rows, lines, strips, early) &&
				!frame_in_strips(total, rows, lines, strips, late),
				"calibration %u: the right offset tore", (unsigned)lines);
			/* Offset a step too low: the picture is shown a step later
			 * than the count says, so the refresh is a step less ahead. */
			CHECK(frame_in_strips(total, rows, lines, strips,
					(early + total - step) % total) ||
				frame_in_strips(total, rows, lines, strips,
					(late + total - step) % total),
				"calibration %u: a step too low did not tear",
				(unsigned)lines);
			/* A step too high: the refresh is a step further ahead. */
			CHECK(frame_in_strips(total, rows, lines, strips,
					(early + step) % total) ||
				frame_in_strips(total, rows, lines, strips,
					(late + step) % total),
				"calibration %u: a step too high did not tear",
				(unsigned)lines);
			/* Only the late end shows a step too high. */
			CHECK(!frame_in_strips(total, rows, lines, strips,
					(early + step) % total),
				"calibration %u: the early end alone showed a step too "
				"high", (unsigned)lines);
		}
	}
	{
		uint32_t lo = 10, hi = 13;

		cp_sync_window_end(&lo, &hi, 1);
		CHECK(lo == 10 && hi == 13, "a window narrower than an end moved");
	}

	/* A frame that takes most of two refreshes cannot be placed. */
	{
		uint32_t lo, hi;

		CHECK(!cp_sync_window(TOTAL, 480, 1100, 0, &lo, &hi),
			"a window for a frame of two refreshes");
		CHECK(!cp_sync_window(TOTAL, 212, 400, 1, &lo, &hi),
			"a window for a slow frame written against the refresh");
	}

	/* The probe: a counter going round 15 times in a quarter second... */
	for(i = 0; i < 15u * TOTAL * 4u + 4u; i++)
		cp_scan_probe_add(&probe, (i / 4u) % TOTAL);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == TOTAL && hz == 60,
		"probe: %u Hz", (unsigned)hz);
	/* ...the same, read wrong now and then: first of all (as after pixels
	 * were written) above its last row, then across every carry, as a read
	 * of the high byte before it and the low byte after, and now and then
	 * anything at all. None of it may move the rows or the rate... */
	probe = (cp_scan_probe){ 0 };
	cp_scan_probe_add(&probe, 923);
	for(i = 0; i < 15u * TOTAL * 4u + 4u; i++) {
		uint32_t row = (i / 4u) % TOTAL;

		if(i % 4u == 3u && (row & 0xffu) == 0xffu)
			row &= ~0xffu;
		else if(i % 997u == 500u)
			row = (i * 2654435761u >> 22) & 0x3ffu;
		cp_scan_probe_add(&probe, row);
	}
	{
		uint32_t got = cp_scan_probe_result(&probe, 32, &hz);

		CHECK(got == TOTAL && hz == 60, "probe with misreads: %u rows, %u Hz",
			(unsigned)got, (unsigned)hz);
	}
	/* ...a register that always answers the same... */
	probe = (cp_scan_probe){ 0 };
	for(i = 0; i < 20000u; i++)
		cp_scan_probe_add(&probe, 0x16);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == 0, "probe took a constant");
	/* ...and noise. */
	probe = (cp_scan_probe){ 0 };
	for(i = 0; i < 20000u; i++)
		cp_scan_probe_add(&probe, (i * 2654435761u >> 22) & 0x3ffu);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == 0, "probe took noise");

	/* Reading the counter: the same row or a few later is believed, also
	 * across the end of a refresh; a carry read half done is not. */
	CHECK(cp_scan_agree(100, 100, TOTAL) && cp_scan_agree(100, 105, TOTAL),
		"two reads a few rows apart do not agree");
	CHECK(cp_scan_agree(TOTAL - 2u, 3, TOTAL),
		"two reads across the end of a refresh do not agree");
	CHECK(!cp_scan_agree(255, 511, TOTAL) && !cp_scan_agree(511, 256, TOTAL)
		&& !cp_scan_agree(105, 100, TOTAL),
		"a misread row was believed");

	/* The game's window keeps more clear than the interface's and lies
	 * inside it. */
	{
		uint32_t lo = 0, hi = 0, glo = 0, ghi = 0;

		CHECK(cp_sync_window(TOTAL, 240, 120, 0, &lo, &hi) &&
			cp_sync_window_game(TOTAL, 240, 120, 0, &glo, &ghi) &&
			glo > lo && ghi < hi && glo < ghi,
			"game window %u-%u is not inside %u-%u", (unsigned)glo,
			(unsigned)ghi, (unsigned)lo, (unsigned)hi);
		CHECK(glo == 120u + TOTAL / 6u && ghi == TOTAL - TOTAL / 6u,
			"game window is %u-%u", (unsigned)glo, (unsigned)ghi);
	}

	/* The least distance seen between the two during a frame. */
	cp_sync_track_begin(&t, TOTAL);
	cp_sync_track_strip(&t, 300, 0);
	cp_sync_track_strip(&t, 320, 100);
	cp_sync_track_strip(&t, 340, 400);
	CHECK(t.near == 60u, "closest approach is %u", (unsigned)t.near);

	/* Top-down refresh (R61523 with B7=0, B4=0): each strip's window is
	 * filled in the same direction as the refresh, so strip=0 needs no
	 * intra-strip margin and calibration still rejects a step out either way. */
	{
		static const uint32_t lines_cases[] = { 250, 280, 400, 600 };
		const uint32_t total = 548u, rows = 528u, strips = 33u;
		const uint32_t step = total / 16u, none = total + 1u;

		for(c = 0; c < sizeof(lines_cases) / sizeof(lines_cases[0]); c++) {
			uint32_t lines = lines_cases[c], lo, hi, elo, ehi, llo, lhi, at;
			unsigned torn = 0;

			CHECK(cp_sync_window_part(total, rows, lines, 0, none, 0,
				&lo, &hi), "top-down %u: no bare window", (unsigned)lines);
			for(at = lo; at < hi; at++)
				torn += (unsigned)frame_in_strips_same_dir(total, rows, lines,
					strips, at);
			CHECK(torn == 0, "top-down %u: %u starts in [%u, %u) tore",
				(unsigned)lines, torn, (unsigned)lo, (unsigned)hi);

			CHECK(cp_sync_window_part(total, rows, lines, 0,
				CP_SYNC_PART_CALIBRATE, 0, &lo, &hi),
				"top-down cal %u: no window", (unsigned)lines);
			elo = llo = lo;
			ehi = lhi = hi;
			cp_sync_window_end(&elo, &ehi, 0);
			cp_sync_window_end(&llo, &lhi, 1);
			CHECK(!frame_in_strips_same_dir(total, rows, lines, strips, elo) &&
				!frame_in_strips_same_dir(total, rows, lines, strips, llo),
				"top-down cal %u: right offset tore", (unsigned)lines);
			CHECK(frame_in_strips_same_dir(total, rows, lines, strips,
					(elo + total - step) % total) ||
				frame_in_strips_same_dir(total, rows, lines, strips,
					(llo + total - step) % total),
				"top-down cal %u: step too low did not tear", (unsigned)lines);
			CHECK(frame_in_strips_same_dir(total, rows, lines, strips,
					(elo + step) % total) ||
				frame_in_strips_same_dir(total, rows, lines, strips,
					(llo + step) % total),
				"top-down cal %u: step too high did not tear", (unsigned)lines);
		}
	}

	/* Full-window calibration probe (cp_sync_window_probe): eliminates the
	 * opposite-side false-clean band (e.g. positions 4..12 on a 555-row
	 * R61523 or on a 548-row 0x16 when |lines - rows| < total/2), while
	 * still leaving a clean position when the true start sits midway between
	 * two stops (even at the late edge of the 4-row polling slot). */
	{
		static const struct {
			uint32_t total, lines, strip;
			int same_dir;
		} probe_cases[] = {
			{ 555u, 440u,  0u, 1 }, /* R61523 faster write */
			{ 555u, 600u,  0u, 1 }, /* R61523 slower write (4..12 band) */
			{ 555u, 620u,  0u, 1 }, /* R61523 slower write */
			{ 555u, 680u,  0u, 1 }, /* R61523 slow write */
			{ 548u, 400u, 16u, 0 }, /* 0x16 faster write */
			{ 548u, 600u, 16u, 0 }, /* 0x16 slower write */
			{ 548u, 640u, 16u, 0 }, /* 0x16 stock write */
			{ 548u, 750u, 16u, 0 }, /* 0x16 very slow write */
		};
		const uint32_t rows = 528u, strips = 33u;

		for(c = 0; c < sizeof(probe_cases) / sizeof(probe_cases[0]); c++) {
			uint32_t total = probe_cases[c].total;
			uint32_t lines = probe_cases[c].lines;
			uint32_t strip = probe_cases[c].strip;
			int same_dir = probe_cases[c].same_dir;
			uint32_t lo, hi, pos, k, ts;

			CHECK(cp_sync_window_part(total, rows, lines, 0,
				CP_SYNC_PART_CALIBRATE, strip, &lo, &hi),
				"probe case %u: no window", c);

			/* True start at 0: position 0 is clean at every probe slot and
			 * every wrong position 1..15 tears on at least one slot. */
			for(pos = 0; pos < 16u; pos++) {
				uint32_t shift = total * pos / 16u;
				int tore = 0;

				for(k = 0; k < CP_SYNC_CALIBRATE_STEPS; k++) {
					uint32_t plo = lo, phi = hi;
					uint32_t at;

					cp_sync_window_probe(&plo, &phi, k);
					CHECK(plo >= lo && phi <= hi &&
						phi - plo <= CP_SYNC_END_ROWS,
						"probe case %u step %u: slot [%u,%u) outside [%u,%u)",
						c, (unsigned)k, (unsigned)plo, (unsigned)phi,
						(unsigned)lo, (unsigned)hi);
					at = (plo + shift) % total;
					if(same_dir ? frame_in_strips_same_dir(total, rows, lines,
							strips, at)
						: frame_in_strips(total, rows, lines, strips, at))
						tore = 1;
				}
				if(pos == 0)
					CHECK(!tore, "probe case %u: true position 0 tore", c);
				else
					CHECK(tore, "probe case %u: wrong position %u stayed clean",
						c, (unsigned)pos);
			}

			/* True start midway between stops 0 and 1 (ts = total/32): at
			 * least one of position 0 or 1 remains clean across all 16 slots,
			 * even when polling triggers at the last row of each 4-row slot. */
			ts = total / 32u;
			{
				int any_clean = 0;

				for(pos = 0; pos <= 1u; pos++) {
					uint32_t shift = total * pos / 16u;
					int tore = 0;

					for(k = 0; k < CP_SYNC_CALIBRATE_STEPS; k++) {
						uint32_t plo = lo, phi = hi;
						uint32_t at0, at1;

						cp_sync_window_probe(&plo, &phi, k);
						at0 = (plo + shift + total - ts) % total;
						at1 = (phi - 1u + shift + total - ts) % total;
						if(same_dir
							? (frame_in_strips_same_dir(total, rows, lines,
									strips, at0) ||
								frame_in_strips_same_dir(total, rows, lines,
									strips, at1))
							: (frame_in_strips(total, rows, lines, strips,
									at0) ||
								frame_in_strips(total, rows, lines, strips,
									at1)))
							tore = 1;
					}
					if(!tore)
						any_clean = 1;
				}
				CHECK(any_clean, "probe case %u: midway true start %u had no "
					"clean position", c, (unsigned)ts);
			}
		}
	}

	/* R61523 refresh direction from 0x0b (get_address_mode). */
	CHECK(cp_r61523_refresh_bottom_up(0x00, 1) == 0, "R61523 0x00 top-down");
	CHECK(cp_r61523_refresh_bottom_up(0x40, 1) == 0, "R61523 0x40 top-down");
	CHECK(cp_r61523_refresh_bottom_up(0x01, 1) == 0, "R61523 B0 flip stays top-down");
	CHECK(cp_r61523_refresh_bottom_up(0x10, 1) == 1, "R61523 B4=1 bottom-up");
	CHECK(cp_r61523_refresh_bottom_up(0x80, 1) == 1, "R61523 B7=1 bottom-up");
	CHECK(cp_r61523_refresh_bottom_up(0x90, 1) == 0, "R61523 B7=1 B4=1 top-down");
	CHECK(cp_r61523_refresh_bottom_up(0xc0, 0) == 0, "R61523 invalid fallback");

	/* R61523 B0h (MCAP) restore values: only 0, 2, 3, 4 are legal. */
	CHECK(cp_r61523_mcap_restore_value(0x00) == 0x00, "MCAP 0x00");
	CHECK(cp_r61523_mcap_restore_value(0x02) == 0x02, "MCAP 0x02");
	CHECK(cp_r61523_mcap_restore_value(0x03) == 0x03, "MCAP 0x03");
	CHECK(cp_r61523_mcap_restore_value(0x04) == 0x04, "MCAP 0x04");
	CHECK(cp_r61523_mcap_restore_value(0x01) == 0x03, "MCAP inhibited 0x01");
	CHECK(cp_r61523_mcap_restore_value(0x05) == 0x03, "MCAP inhibited 0x05");
	CHECK(cp_r61523_mcap_restore_value(0x07) == 0x03, "MCAP inhibited 0x07");
	CHECK(cp_r61523_mcap_restore_value(0xff) == 0x03, "MCAP garbage 0xff");

	/* R61523 B9h PWMDIV: exactly 10 values in Table 54 are legal. */
	{
		unsigned valid_count = 0;

		for(i = 0; i < 256u; i++)
			valid_count += (unsigned)cp_r61523_pwmdiv_valid((uint8_t)i);
		CHECK(valid_count == 10u, "PWMDIV had %u valid codes, expected 10",
			valid_count);
		CHECK(cp_r61523_pwmdiv_valid(0x00) && cp_r61523_pwmdiv_valid(0x01) &&
			cp_r61523_pwmdiv_valid(0x02) && cp_r61523_pwmdiv_valid(0x03) &&
			cp_r61523_pwmdiv_valid(0x07) && cp_r61523_pwmdiv_valid(0x0f) &&
			cp_r61523_pwmdiv_valid(0x1f) && cp_r61523_pwmdiv_valid(0x3f) &&
			cp_r61523_pwmdiv_valid(0x7f) && cp_r61523_pwmdiv_valid(0xff),
			"missing legal PWMDIV code");
		CHECK(!cp_r61523_pwmdiv_valid(0x04) && !cp_r61523_pwmdiv_valid(0x10) &&
			!cp_r61523_pwmdiv_valid(0x80), "accepted inhibited PWMDIV code");
	}

	/* R61523 B9h snapshot validation. */
	{
		static const uint8_t ok_auto[4] = { 0x00, 0x34, 0x01, 0x08 };
		static const uint8_t ok_manual[4] = { 0x01, 0x67, 0x07, 0x1d };
		static const uint8_t all_zero[4] = { 0x00, 0x00, 0x00, 0x00 };
		static const uint8_t all_ff[4] = { 0xff, 0xff, 0xff, 0xff };
		static const uint8_t bad_pwmon_res[4] = { 0x02, 0x34, 0x01, 0x08 };
		static const uint8_t bad_bdcv_zero[4] = { 0x01, 0x00, 0x01, 0x08 };
		static const uint8_t bad_pwmdiv[4] = { 0x01, 0x34, 0x04, 0x08 };
		static const uint8_t bad_no_ledpwme[4] = { 0x01, 0x34, 0x01, 0x00 };
		static const uint8_t bad_ctrl_res[4] = { 0x01, 0x34, 0x01, 0x28 };
		static const uint8_t bad_manual_off[4] = { 0x00, 0x34, 0x01, 0x18 };

		CHECK(cp_r61523_b9_valid(ok_auto), "valid B9h (PWMWM=0)");
		CHECK(cp_r61523_b9_valid(ok_manual), "valid B9h (PWMWM=1, PWMON=1)");
		CHECK(!cp_r61523_b9_valid(all_zero), "rejected all-zero B9h");
		CHECK(!cp_r61523_b9_valid(all_ff), "rejected all-0xff B9h");
		CHECK(!cp_r61523_b9_valid(bad_pwmon_res), "rejected reserved PWMON bits");
		CHECK(!cp_r61523_b9_valid(bad_bdcv_zero), "rejected BDCV=0");
		CHECK(!cp_r61523_b9_valid(bad_pwmdiv), "rejected inhibited PWMDIV");
		CHECK(!cp_r61523_b9_valid(bad_no_ledpwme), "rejected LEDPWME=0");
		CHECK(!cp_r61523_b9_valid(bad_ctrl_res), "rejected reserved ctrl bits");
		CHECK(!cp_r61523_b9_valid(bad_manual_off), "rejected PWMWM=1, PWMON=0");
	}

	/* Backlight PWM frequency table and minimum brightness floor per rate. */
	{
		static const uint32_t expect_hz[10] = {
			0, 430, 860, 1720, 3430, 6860, 13700, 18300, 27400, 33300
		};
		static const uint32_t expect_min[10] = {
			0, 0, 0, 0, 0, 1, 2, 3, 4, 5
		};

		for(i = 0; i < 10u; i++) {
			CHECK(cp_backlight_pwm_hz(i) == expect_hz[i],
				"pwm %u: got %u Hz", (unsigned)i,
				(unsigned)cp_backlight_pwm_hz(i));
			CHECK(cp_backlight_min_level(i) == expect_min[i],
				"pwm %u: got min level %u", (unsigned)i,
				(unsigned)cp_backlight_min_level(i));
		}
		CHECK(cp_backlight_pwm_hz(10u) == 0 && cp_backlight_min_level(10u) == 0,
			"out-of-range pwm rate");
	}

	/* Sparse strip tracking, line-count smoothing, and wait-loop backoff. */
	{
		cp_sync_track sparse;
		uint32_t est, cur = 0, b;

		cp_sync_track_begin(&sparse, TOTAL);
		cp_sync_track_strip(&sparse, 100u, 0u);
		cp_sync_track_strip(&sparse, 210u, 240u);
		cp_sync_track_strip(&sparse, 320u, 480u);
		est = cp_sync_track_lines_n(&sparse, 33u);
		/* span = 220 across 32 strip intervals -> 220 * 33 / 32 = 226. */
		CHECK(est == 226u, "sparse lines: got %u", (unsigned)est);

		cur = cp_sync_smooth_lines(cur, 200u, TOTAL);
		CHECK(cur == 200u, "smooth init: %u", (unsigned)cur);
		cur = cp_sync_smooth_lines(cur, 280u, TOTAL);
		CHECK(cur == 240u, "smooth fast rise: %u", (unsigned)cur);
		cur = cp_sync_smooth_lines(cur, 200u, TOTAL);
		CHECK(cur == 235u, "smooth slow decay: %u", (unsigned)cur);
		cur = cp_sync_smooth_lines(cur, TOTAL * 4u, TOTAL);
		CHECK(cur == 235u, "smooth ignores spike: %u", (unsigned)cur);
		{
			uint32_t cmin = 0, rlo = 0, rhi = 0, lo1 = 0, hi1 = 0;

			cmin = cp_sync_smooth_lines_min(cmin, 200u, TOTAL);
			cmin = cp_sync_smooth_lines_min(cmin, 280u, TOTAL);
			cmin = cp_sync_smooth_lines_min(cmin, 160u, TOTAL);
			CHECK(cmin < 200u, "min smooth drops fast: %u", (unsigned)cmin);
			CHECK(cp_sync_window_part_range(TOTAL, 212u, 120u, 260u, 0,
					CP_SYNC_PART_GAME, 12u, &rlo, &rhi),
				"dual-envelope classpad window exists");
			CHECK(cp_sync_window_part(TOTAL, 212u, 120u, 0,
					CP_SYNC_PART_GAME, 12u, &lo1, &hi1) &&
				rlo >= lo1 && rhi <= hi1,
				"dual window intersects min/max bounds");
		}

		/* Close to window start -> 0 backoff (tight poll). */
		CHECK(cp_sync_wait_backoff_us(90u, 100u, TOTAL, 60u) == 0u,
			"near window must not back off");
		CHECK(cp_sync_wait_backoff_us(75u, 100u, TOTAL, 100u) == 0u,
			"within 28 rows at 100 Hz must not back off");
		/* Far from window start -> positive bounded backoff (<= 2 ms) that
		 * stays below half the remaining distance even at 100 Hz. */
		b = cp_sync_wait_backoff_us(0u, 200u, TOTAL, 60u);
		CHECK(b > 1000u && b <= 2000u, "far backoff out of range: %u us",
			(unsigned)b);
		b = cp_sync_wait_backoff_us(0u, 100u, TOTAL, 100u);
		CHECK(b > 200u && b < 800u, "100 Hz backoff out of range: %u us",
			(unsigned)b);
	}

	/* Refresh rate presets (unified 0=Auto, 1=System, 2=59.73 Hz (GBA),
	 * 3..9=66..100 Hz), joint (VFP, RTN) solver, and shared Himax builder. */
	{
		static const uint32_t expect_hz[CP_REFRESH_COUNT] = {
			0, 0, 60, 66, 72, 78, 85, 90, 95, 100
		};
		static const uint8_t c1_ok[5] = { 0x00, 0x00, 0x1f, 0x00, 0x00 };
		static const uint8_t c1_lo[5] = { 0x00, 0x00, 17u, 0x00, 0x00 };
		static const uint8_t c1_hi[5] = { 0x00, 0x00, 64u, 0x00, 0x00 };
		uint8_t vfp = 0, rtn = 0, b2[6], df[12];
		uint32_t total = 0;

		CHECK(cp_refresh_target_mhz(0u, 0u) == 0u,
			"Auto + VSync Off -> stock");
		CHECK(cp_refresh_target_mhz(0u, 1u) == CGBA_SYNC_RATE_GBA_MHZ,
			"Auto + VSync On -> 59.73 Hz (GBA)");
		CHECK(cp_refresh_target_mhz(1u, 1u) == 0u,
			"System + VSync On -> stock");
		CHECK(cp_refresh_target_mhz(2u, 0u) == CGBA_SYNC_RATE_GBA_MHZ,
			"59.73 Hz (GBA) explicit");

		for(i = 0; i < CP_REFRESH_COUNT; i++) {
			uint8_t rn = cp_refresh_rtn_new(i);
			uint8_t ro = cp_refresh_rtn_r61523(i);

			CHECK(cp_refresh_hz(i) == expect_hz[i],
				"refresh %u: got %u Hz", (unsigned)i,
				(unsigned)cp_refresh_hz(i));
			CHECK(rn >= 50u && rn <= 253u,
				"refresh %u new RTN %u out of OS range", (unsigned)i,
				(unsigned)rn);
			CHECK(ro >= 18u && ro <= 63u,
				"refresh %u R61523 RTN %u out of OS range", (unsigned)i,
				(unsigned)ro);
			if(i >= 3u) {
				CHECK(rn < cp_refresh_rtn_new(i - 1u),
					"refresh %u new RTN not strictly decreasing", (unsigned)i);
				CHECK(ro < cp_refresh_rtn_r61523(i - 1u),
					"refresh %u R61523 RTN not strictly decreasing", (unsigned)i);
			}
		}
		CHECK(cp_refresh_hz(CP_REFRESH_COUNT) == 0u &&
			cp_refresh_rtn_new(CP_REFRESH_COUNT) == 0x9du &&
			cp_refresh_rtn_r61523(CP_REFRESH_COUNT) == 31u,
			"out-of-range refresh preset fallback");
		CHECK(cp_r61523_c1_valid(c1_ok), "accepted valid R61523 C1h");
		CHECK(!cp_r61523_c1_valid(c1_lo) && !cp_r61523_c1_valid(c1_hi),
			"rejected out-of-range R61523 C1h RTN");

		/* Measured 0x16 hardware baseline (57.32 Hz at VFP=8, RTN=0x9D):
		 * solving for 59.728 Hz (GBA) uses VFP tuning to hit within 0.05 Hz. */
		cp_refresh_solve_new(57320u, CGBA_SYNC_RATE_GBA_MHZ, &vfp, &rtn, &total);
		CHECK(vfp >= 8u && vfp <= 24u && rtn >= 50u && rtn <= 253u,
			"59.73 solve out of bounds: vfp=%u rtn=%u", vfp, rtn);
		{
			uint64_t stock_clk = 548ull * (0x9du + CGBA_HIMAX_RTN_BIAS);
			uint64_t new_clk = (uint64_t)total * (rtn + CGBA_HIMAX_RTN_BIAS);
			uint32_t got_mhz = (uint32_t)((57320ull * stock_clk +
				new_clk / 2u) / new_clk);
			uint32_t diff = got_mhz > CGBA_SYNC_RATE_GBA_MHZ
				? got_mhz - CGBA_SYNC_RATE_GBA_MHZ
				: CGBA_SYNC_RATE_GBA_MHZ - got_mhz;
			CHECK(diff <= 50u,
				"59.73 solve accuracy: got %u mHz (vfp=%u rtn=0x%02x total=%u)",
				(unsigned)got_mhz, vfp, rtn, (unsigned)total);
		}

		/* High-refresh stops (>= 66 Hz) keep VFP=8 (548 rows constant). */
		cp_refresh_solve_new(57320u, CGBA_SYNC_RATE_85_MHZ, &vfp, &rtn, &total);
		CHECK(vfp == 8u && total == 548u && rtn >= 70u && rtn <= 78u,
			"85 Hz solve: vfp=%u rtn=0x%02x total=%u", vfp, rtn, (unsigned)total);

		/* Stock restore returns exact OS default (VFP=8, RTN=0x9D, 548 rows). */
		cp_refresh_solve_new(57320u, 0u, &vfp, &rtn, &total);
		CHECK(vfp == 8u && rtn == 0x9du && total == 548u,
			"stock solve: vfp=%u rtn=0x%02x total=%u", vfp, rtn, (unsigned)total);

		/* Shared Himax 0xB2 / 0xDF payload matches OS 02.01.7003 @ 0x8003887e. */
		cgba_himax_build_b2(cgba_himax_classpad(), 0x0cu, 0x91u, b2);
		cgba_himax_build_df(cgba_himax_classpad(), 0x0cu, 0x91u, df);
		CHECK(b2[0] == 0x00u && b2[1] == 0x34u && b2[2] == 0x26u &&
			b2[3] == 0x08u && b2[4] == 0x0cu && b2[5] == 0x91u,
			"ClassPad B2 payload mismatch");
		CHECK(df[0] == 0x08u && df[4] == 0x2au && df[9] == 0x08u &&
			df[10] == 0x0cu && df[11] == 0x91u,
			"ClassPad DF payload mismatch");

		/* R61523 solver stays within OS bounds 18..63. */
		CHECK(cp_refresh_solve_r61523(60000u, CGBA_SYNC_RATE_85_MHZ, 31u) == 22u,
			"R61523 85 Hz solve: got %u",
			(unsigned)cp_refresh_solve_r61523(60000u, CGBA_SYNC_RATE_85_MHZ, 31u));
		CHECK(cp_refresh_solve_r61523(60000u, 0u, 31u) == 31u,
			"R61523 stock restore");

		/* Fractional probe mHz matches exact 60.000 Hz on synthetic 15-wrap run. */
		{
			cp_scan_probe p2 = { 0 };
			uint32_t hz2 = 0, tot2;

			for(i = 0; i < 15u * TOTAL * 4u + 4u; i++)
				cp_scan_probe_add(&p2, (i / 4u) % TOTAL);
			tot2 = cp_scan_probe_result(&p2, 32u, &hz2);
			CHECK(tot2 == TOTAL &&
				cp_scan_probe_mhz(&p2, 32u, tot2) == 60000u,
				"probe mHz: got %u (tot=%u)",
				(unsigned)cp_scan_probe_mhz(&p2, 32u, tot2), (unsigned)tot2);
		}

		/* 1:1 panel-locked pacer (cp_pace_unwrap_rows / cp_pace_advance):
		 * verify backward-read jitter immunity, early wait, wraparound, and
		 * late-frame clamping matching cgba_frame_limiter_wait(). */
		{
			cp_pace_state ps = { 0 };
			int late = -99;
			uint32_t rem;

			/* Minor backward scanline jitter near 0 elapsed clamps to 0. */
			CHECK(cp_pace_unwrap_rows(100u, 99u, 0u, 548u) == 0u,
				"backward jitter should clamp to 0");
			CHECK(cp_pace_unwrap_rows(540u, 10u, 18u, 548u) == 18u,
				"forward wrap across 0");
			CHECK(cp_pace_unwrap_rows(100u, 110u, 558u, 548u) == 558u,
				"full refresh + 10 rows");

			/* Init anchors on first frame with zero wait and late=0. */
			rem = cp_pace_advance(&ps, 1000u, 200u, 548u,
				CGBA_SYNC_RATE_GBA_MHZ, 3, &late);
			CHECK(rem == 0u && late == 0 && ps.valid,
				"cp_pace init: rem=%u late=%d", rem, late);

			/* Early frame (~300 rows = ~300 ETMU ticks at 59.73 Hz):
			 * waits remaining 248 rows with late=0. */
			rem = cp_pace_advance(&ps, 1000u - 300u, 500u, 548u,
				CGBA_SYNC_RATE_GBA_MHZ, 3, &late);
			CHECK(rem == 248u && late == 0,
				"cp_pace early: rem=%u late=%d", rem, late);

			/* Simulate finishing that wait at due_rows (row 200 after wrap). */
			ps.now_rows += rem;
			ps.last_row = (ps.last_row + rem) % 548u;
			ps.last_etmu -= 249u;

			/* Frame overruns by ~0.3 refreshes (~710 rows, ~710 ETMU ticks):
			 * late == 0 (matches ETMU limiter semantics). */
			rem = cp_pace_advance(&ps, ps.last_etmu - 710u,
				(ps.last_row + 710u) % 548u, 548u,
				CGBA_SYNC_RATE_GBA_MHZ, 3, &late);
			CHECK(rem == 0u && late == 0,
				"cp_pace 0.3 overrun: rem=%u late=%d", rem, late);

			/* Huge stall (6 refreshes) clamps to max_late+1 (4). */
			rem = cp_pace_advance(&ps, ps.last_etmu - 3300u,
				(ps.last_row + 548u * 6u) % 548u, 548u,
				CGBA_SYNC_RATE_GBA_MHZ, 3, &late);
			CHECK(rem == 0u && late == 4 &&
				ps.due_rows == ps.now_rows - 548u * 3u,
				"cp_pace clamp: rem=%u late=%d", rem, late);
		}
	}

	if(failures)
		return 1;
	printf("classpad_sync_test: all checks passed\n");
	return 0;
}
