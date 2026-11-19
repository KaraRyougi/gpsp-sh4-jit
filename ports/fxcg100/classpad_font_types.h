#ifndef CGBA_CLASSPAD_FONT_TYPES_H
#define CGBA_CLASSPAD_FONT_TYPES_H

/* The layout of the 4-bit glyph tables that tools/classpad_font_gen.c
 * generates: classpad_font.h (fx-CP400 interface) and cg_font.h (fx-CG
 * interface). Kept apart from the tables so that classpad_gfx.h can draw
 * either set without pulling in the other's data.
 *
 * A glyph is its tight bitmap, two pixels to a byte with the first pixel in
 * the top four bits, rows padded to a byte, coverage 0..15. */

#include <stdint.h>

typedef struct cp_glyph {
	uint16_t offset;      /* into the font's data */
	uint8_t w, h;         /* bitmap size */
	int8_t left, top;     /* bitmap origin from the pen and the line top */
	uint8_t advance;
} cp_glyph;

typedef struct cp_font {
	const cp_glyph *glyphs;
	const uint8_t *data;
	uint8_t height;       /* line height */
	uint8_t ascent;
} cp_font;

#define CP_FONT_FIRST 32
#define CP_FONT_LAST 126

#endif /* CGBA_CLASSPAD_FONT_TYPES_H */
