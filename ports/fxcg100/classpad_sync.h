#ifndef CGBA_CLASSPAD_SYNC_H
#define CGBA_CLASSPAD_SYNC_H

/* Keeping the picture from tearing on the fx-CP400.
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

#define CP_SYNC_MARGIN 24u

/* ---- is there a usable row counter? --------------------------------------- */

typedef struct cp_scan_probe {
	uint32_t samples, max, last;
	uint32_t wraps;      /* fell back by more than a few rows: a new refresh */
	uint32_t backward;   /* fell back a little: not a counter */
} cp_scan_probe;

static inline void cp_scan_probe_add(cp_scan_probe *p, uint32_t value)
{
	if(p->samples) {
		if(value + 64u < p->last)
			p->wraps++;
		else if(value < p->last)
			p->backward++;
	}
	if(value > p->max)
		p->max = value;
	p->last = value;
	p->samples++;
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
	if(rate < 20u || rate > 200u)
		return 0;
	if(p->max < 320u || p->max > 1022u)
		return 0;
	return p->max + 1u;
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
 * a quarter, nine interface frames in ten waited on an fx-CP400, and the
 * motion became uneven. */
static inline int cp_sync_window(uint32_t total, uint32_t rows, uint32_t lines,
	int descending, uint32_t *lo, uint32_t *hi)
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
	if(clear > total / 8u)
		clear = total / 8u;

	if(descending) {
		*lo = rows + clear;
		*hi = total - lines - clear;
	} else {
		/* Writing faster than it shows: it must be far enough ahead not
		 * to be caught. Slower: it must not come round and catch up. */
		*lo = (rows > lines ? rows - lines : 0) + clear;
		*hi = total - (lines > rows ? lines - rows : 0) - clear;
	}
	return *lo < *hi;
}

/* ---- watching one frame go out -------------------------------------------- */

typedef struct cp_sync_track {
	uint32_t total;
	uint32_t strips, first, last, wraps;
	uint32_t gap;        /* shown row minus written row, modulo total */
	int crossed;
} cp_sync_track;

static inline void cp_sync_track_begin(cp_sync_track *t, uint32_t total)
{
	t->total = total;
	t->strips = 0;
	t->wraps = 0;
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
	t->last = shown;
	t->gap = gap;
	t->strips++;
}

/* Rows shown while the frame was written: what was seen up to the start of
 * the last strip, scaled up by that strip. */
static inline uint32_t cp_sync_track_lines(const cp_sync_track *t)
{
	uint32_t span;

	if(t->strips < 2u)
		return 0;
	span = t->wraps * t->total + t->last - t->first;
	return span * t->strips / (t->strips - 1u);
}

#endif /* CGBA_CLASSPAD_SYNC_H */
