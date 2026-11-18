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
	cp_scan_probe probe = { 0, 0, 0, 0, 0 };
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

	/* A frame that takes most of two refreshes cannot be placed. */
	{
		uint32_t lo, hi;

		CHECK(!cp_sync_window(TOTAL, 480, 1100, 0, &lo, &hi),
			"a window for a frame of two refreshes");
		CHECK(!cp_sync_window(TOTAL, 212, 400, 1, &lo, &hi),
			"a window for a slow frame written against the refresh");
	}

	/* The probe: a counter going round 15 times in a quarter second... */
	for(i = 0; i <= 15u * TOTAL * 4u; i++)
		cp_scan_probe_add(&probe, (i / 4u) % TOTAL);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == TOTAL && hz == 60,
		"probe: %u Hz", (unsigned)hz);
	/* ...a register that always answers the same... */
	probe = (cp_scan_probe){ 0, 0, 0, 0, 0 };
	for(i = 0; i < 20000u; i++)
		cp_scan_probe_add(&probe, 0x16);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == 0, "probe took a constant");
	/* ...and noise. */
	probe = (cp_scan_probe){ 0, 0, 0, 0, 0 };
	for(i = 0; i < 20000u; i++)
		cp_scan_probe_add(&probe, (i * 2654435761u >> 22) & 0x3ffu);
	CHECK(cp_scan_probe_result(&probe, 32, &hz) == 0, "probe took noise");

	if(failures)
		return 1;
	printf("classpad_sync_test: all checks passed\n");
	return 0;
}
