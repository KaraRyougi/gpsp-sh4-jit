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
 * the length positions are taken modulo. (An fx-CP400 with panel ID 0x16
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
	uint32_t last;       /* the last row believed */
	uint32_t wraps;      /* believed rows went back to the start: a refresh */
	uint32_t backward;   /* a read fell back a little: not a counter */
} cp_scan_probe;

static inline void cp_scan_probe_add(cp_scan_probe *p, uint32_t value)
{
	if(p->samples) {
		if(value >= p->prev && value <= p->prev + CP_SCAN_STEP) {
			if(p->believed && value + 64u < p->last)
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
 * a quarter, nine interface frames in ten waited on an fx-CP400, and the
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

/* The share kept clear: by the interface, by the game, and while the offset
 * is being set by eye, when what is wanted is the opposite of safety: an
 * offset one step out (a sixteenth of a refresh) should already tear.
 *
 * While setting it, half a step: wherever the picture really starts, the
 * step nearest to it is then at most half a step out and gives a clean
 * screen, and every other step is more than that and tears. With less (a
 * 48th was tried), a start that falls between two steps leaves no step
 * clean at all, only some that tear less often than others, and which of
 * those looks clean depends on how long it is watched. */
#define CP_SYNC_PART 8u
#define CP_SYNC_PART_GAME 6u
#define CP_SYNC_PART_CALIBRATE 32u

/* While the offset is being set, a frame does not start wherever it finds
 * the window but at one end of it, the two ends in turn. Waiting for the
 * window and starting as it opens only ever tries the early end: there an
 * offset set too low shows, but one set too high only makes the frame
 * start later, with the refresh further ahead, and stays clean until the
 * delay uses up the whole window. The interface's frames always arrive
 * early enough to wait, so on an fx-CP400 positions 0 to 6 all looked
 * clean when only 0 was right. At the late end it is the other way round.
 *
 * The end is a few rows wide so that the polling cannot step over it; a
 * frame starts at the first of them. Narrows [*lo, *hi) to the early end,
 * or to the late one if `late`. */
#define CP_SYNC_END_ROWS 4u

static inline void cp_sync_window_end(uint32_t *lo, uint32_t *hi, int late)
{
	if(*hi - *lo <= CP_SYNC_END_ROWS)
		return;
	if(late)
		*lo = *hi - CP_SYNC_END_ROWS;
	else
		*hi = *lo + CP_SYNC_END_ROWS;
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
