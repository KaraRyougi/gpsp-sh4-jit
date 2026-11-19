#ifndef CGBA_CLASSPAD_SCALE_H
#define CGBA_CLASSPAD_SCALE_H

/* fx-CP400 scaling cores that the fx-CG presenter does not need. Pure RGB565
 * integer code shared with tests/classpad_scale_test.c.
 *
 * Portrait reuses the 3 -> 4 (240 -> 320) scalers in fxcg100_scale.h. The
 * landscape modes are new:
 *
 *   1.5x  2 -> 3 on both axes (240x160 -> 360x240)
 *   2x    1 -> 2 on both axes (240x160 -> 480x320)
 *
 * The panel is scanned in portrait order, so in landscape one panel row is one
 * GBA *column*. A frame is therefore produced column by column: gather a
 * column into a 160-pixel line, stretch the line to the panel-row length, then
 * repeat or blend whole lines for the other axis. */

#include <stdint.h>

#include "fxcg100_scale.h"

#define CP_GBA_W 240
#define CP_GBA_H 160

/* Gather GBA column `sx` as a line of 160 pixels. `bottom_up` selects the
 * direction the panel row runs in user space (see classpad_layout.h): with the
 * keys on the right a panel row runs from the bottom of the picture to the
 * top. */
static inline void cp_gather_column(const uint16_t *fb, unsigned sx,
	int bottom_up, uint16_t *line)
{
	const uint16_t *p = fb + sx;
	unsigned i;

	if (bottom_up) {
		p += (CP_GBA_H - 1) * CP_GBA_W;
		for (i = 0; i < CP_GBA_H; i++, p -= CP_GBA_W)
			line[i] = *p;
	} else {
		for (i = 0; i < CP_GBA_H; i++, p += CP_GBA_W)
			line[i] = *p;
	}
}

/* 1 -> 2: every pixel twice. dst holds 2*n pixels. */
static inline void cp_line_double(const uint16_t *src, uint16_t *dst,
	unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		uint16_t a = src[i];

		dst[2 * i] = a;
		dst[2 * i + 1] = a;
	}
}

/* 2 -> 3 over n source pixels (n even); dst holds n*3/2 pixels.
 *
 * Sampling at output pixel centres puts the three taps of each source pair
 * (a, b) at -1/6, 1/2 and 7/6, i.e. {a, a|b boundary, b}. Only the middle tap
 * straddles two source pixels:
 *   SMOOTH / SHARP  {a, avg(a,b), b}   (both blend rules agree at exactly .5)
 *   CRISP           {a, a, b}          (nearest; no new colours) */
static inline void cp_line_2to3(const uint16_t *src, uint16_t *dst,
	unsigned n, int filter)
{
	unsigned i;

	if (filter == CGBA_SCALE_FILTER_CRISP) {
		for (i = 0; i < n; i += 2, dst += 3) {
			uint16_t a = src[i], b = src[i + 1];

			dst[0] = a;
			dst[1] = a;
			dst[2] = b;
		}
	} else {
		for (i = 0; i < n; i += 2, dst += 3) {
			uint16_t a = src[i], b = src[i + 1];

			dst[0] = a;
			dst[1] = cgba_avg565(a, b);
			dst[2] = b;
		}
	}
}

/* Middle line of a 2 -> 3 group on the other axis: the blend of two already
 * stretched lines, or a copy of the first for CRISP. n is a multiple of 8. */
static inline void cp_line_between(const uint16_t *a, const uint16_t *b,
	uint16_t *dst, unsigned n, int filter)
{
	unsigned i;

	if (filter == CGBA_SCALE_FILTER_CRISP) {
		for (i = 0; i < n; i++)
			dst[i] = a[i];
	} else {
		cgba_scale_row_avg(a, b, dst, n);
	}
}

#endif /* CGBA_CLASSPAD_SCALE_H */
