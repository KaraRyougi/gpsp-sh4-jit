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

	/* Strips filled against the refresh, on an fx-CP400's 548-row refresh:
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

	if(failures)
		return 1;
	printf("classpad_sync_test: all checks passed\n");
	return 0;
}
