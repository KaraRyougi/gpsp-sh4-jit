#ifndef FXCG100_SCALE_H
#define FXCG100_SCALE_H

/* Pure RGB565 upscaling cores shared by the gint presenter and the host unit
 * test (tests/scale_test.c). Everything here is branch-light integer math so
 * a strip can be scaled while the previous strip's LCD DMA is in flight.
 *
 * Both modes use exact fixed pixel-group patterns (no per-pixel multiplies):
 *   4:3   240x160 -> 320x212  (3 -> 4 both axes, aspect-true)
 *   FULL  240x160 -> 384x216  (5 -> 8 horizontal, 20 -> 27 vertical)
 * Inserted pixels are 50/50 averages of their neighbours - the carry-safe
 * 565 mask trick, applied to TWO packed pixels per 32-bit op in the hot
 * loops. All rows involved (GBA framebuffer, XY-RAM strips, scratch) are
 * 4-byte aligned and even-width, so the u32 paths need no edge handling.
 * Word order within a u32 follows the build's endianness (SH-4 here is
 * big-endian; the host test works either way because packing and unpacking
 * use the same order). */

#include <stdint.h>

/* Per-channel floor average of two RGB565 pixels. */
static inline uint16_t cgba_avg565(uint16_t x, uint16_t y)
{
	return (uint16_t)((x & y) + (((x ^ y) & 0xF7DEu) >> 1));
}

/* Same, two packed 565 pixels at once. */
static inline uint32_t cgba_avg565x2(uint32_t x, uint32_t y)
{
	return (x & y) + (((x ^ y) & 0xF7DEF7DEu) >> 1);
}

/* dst[i] = avg(a[i], b[i]) - vertical blend of two already-scaled rows.
 * n must be even; a/b/dst 4-byte aligned. */
static inline void cgba_scale_row_avg(const uint16_t *a, const uint16_t *b,
	uint16_t *dst, unsigned n)
{
	const uint32_t *a32 = (const uint32_t *)(const void *)a;
	const uint32_t *b32 = (const uint32_t *)(const void *)b;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned i;

	for (i = 0; i < n / 2; i += 4) {
		d32[i + 0] = cgba_avg565x2(a32[i + 0], b32[i + 0]);
		d32[i + 1] = cgba_avg565x2(a32[i + 1], b32[i + 1]);
		d32[i + 2] = cgba_avg565x2(a32[i + 2], b32[i + 2]);
		d32[i + 3] = cgba_avg565x2(a32[i + 3], b32[i + 3]);
	}
}

#if defined(__BIG_ENDIAN__) || \
	(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define CGBA_SCALE_PACK2(first, second) \
	(((uint32_t)(first) << 16) | (uint32_t)(second))
#define CGBA_SCALE_HI16(w) ((uint16_t)((w) >> 16))   /* first pixel */
#define CGBA_SCALE_LO16(w) ((uint16_t)(w))           /* second pixel */
#else
#define CGBA_SCALE_PACK2(first, second) \
	(((uint32_t)(second) << 16) | (uint32_t)(first))
#define CGBA_SCALE_HI16(w) ((uint16_t)(w))
#define CGBA_SCALE_LO16(w) ((uint16_t)((w) >> 16))
#endif

/* 240 -> 320: {a, ab, bc, c} per 3 source pixels. Linear taps for out x in
 * {0, .75, 1.5, 2.25} rounded to nearest/50-50. Processes two groups (6 src
 * pixels, 3 u32 loads) into 8 out pixels (4 u32 stores) per iteration. */
static inline void cgba_scale_row_240_320(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 40; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);

		d32[0] = CGBA_SCALE_PACK2(a, cgba_avg565(a, b));
		d32[1] = CGBA_SCALE_PACK2(cgba_avg565(b, c), c);
		d32[2] = CGBA_SCALE_PACK2(d, cgba_avg565(d, e));
		d32[3] = CGBA_SCALE_PACK2(cgba_avg565(e, f), f);
		s32 += 3;
		d32 += 4;
	}
}

/* 240 -> 384: {a, ab, bc, c, cd, d, de, e} per 5 source pixels. Taps at
 * x = t*5/8: fractions in [.25,.75] blend, otherwise nearest; the final
 * tap (4.375) would cross the group boundary and clamps to e. Two groups
 * (10 src pixels, 5 u32 loads) -> 16 out pixels (8 u32 stores). */
static inline void cgba_scale_row_240_384(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 24; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint32_t w3 = s32[3], w4 = s32[4];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);
		uint16_t h = CGBA_SCALE_HI16(w3), i = CGBA_SCALE_LO16(w3);
		uint16_t j = CGBA_SCALE_HI16(w4), k = CGBA_SCALE_LO16(w4);

		d32[0] = CGBA_SCALE_PACK2(a, cgba_avg565(a, b));
		d32[1] = CGBA_SCALE_PACK2(cgba_avg565(b, c), c);
		d32[2] = CGBA_SCALE_PACK2(cgba_avg565(c, d), d);
		d32[3] = CGBA_SCALE_PACK2(cgba_avg565(d, e), e);
		d32[4] = CGBA_SCALE_PACK2(f, cgba_avg565(f, h));
		d32[5] = CGBA_SCALE_PACK2(cgba_avg565(h, i), i);
		d32[6] = CGBA_SCALE_PACK2(cgba_avg565(i, j), j);
		d32[7] = CGBA_SCALE_PACK2(cgba_avg565(j, k), k);
		s32 += 5;
		d32 += 8;
	}
}

/* ---- Scale filters --------------------------------------------------------
 * Orthogonal to the geometry (4:3 / fullscreen). For each output tap at
 * source position p (fractional part f) the three filters choose:
 *   SMOOTH  blend whenever f != 0            (softest; most inter-pixel blur)
 *   SHARP   blend only when 0.25 < f < 0.75  (crisp interiors, thin seams)
 *   CRISP   snap to pixel[round(p)]          (nearest-neighbour; exact colours)
 * All three stay within the AND/XOR/shift + 50/50-average primitive (no
 * multiplies), so any filter fits the same DMA-overlap budget. */
enum {
	CGBA_SCALE_FILTER_SMOOTH = 0,
	CGBA_SCALE_FILTER_SHARP  = 1,
	CGBA_SCALE_FILTER_CRISP  = 2
};

/* 240 -> 320 taps {a, .75, 1.5, 2.25} per group of 3 source pixels.
 *   SMOOTH {a, avg(a,b), avg(b,c), c}  (cgba_scale_row_240_320, above)
 *   SHARP  {a, b, avg(b,c), c}         (only the .5 boundary blends)
 *   CRISP  {a, b, c, c}                (nearest; last source doubles) */
static inline void cgba_scale_row_240_320_sharp(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 40; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);

		d32[0] = CGBA_SCALE_PACK2(a, b);
		d32[1] = CGBA_SCALE_PACK2(cgba_avg565(b, c), c);
		d32[2] = CGBA_SCALE_PACK2(d, e);
		d32[3] = CGBA_SCALE_PACK2(cgba_avg565(e, f), f);
		s32 += 3;
		d32 += 4;
	}
}

static inline void cgba_scale_row_240_320_crisp(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 40; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);

		d32[0] = CGBA_SCALE_PACK2(a, b);
		d32[1] = CGBA_SCALE_PACK2(c, c);
		d32[2] = CGBA_SCALE_PACK2(d, e);
		d32[3] = CGBA_SCALE_PACK2(f, f);
		s32 += 3;
		d32 += 4;
	}
}

/* 240 -> 384 taps {0,.625,1.25,1.875,2.5,3.125,3.75,4.375} per 5 source px.
 *   SMOOTH {a, ab, bc, c, cd, d, de, e}  (cgba_scale_row_240_384, above)
 *   SHARP  {a, ab, b, c, cd, d, e, e}    (blend only the near-.5 taps)
 *   CRISP  {a, b, b, c, d, d, e, e}      (nearest) */
static inline void cgba_scale_row_240_384_sharp(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 24; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint32_t w3 = s32[3], w4 = s32[4];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);
		uint16_t h = CGBA_SCALE_HI16(w3), i = CGBA_SCALE_LO16(w3);
		uint16_t j = CGBA_SCALE_HI16(w4), k = CGBA_SCALE_LO16(w4);

		d32[0] = CGBA_SCALE_PACK2(a, cgba_avg565(a, b));
		d32[1] = CGBA_SCALE_PACK2(b, c);
		d32[2] = CGBA_SCALE_PACK2(cgba_avg565(c, d), d);
		d32[3] = CGBA_SCALE_PACK2(e, e);
		d32[4] = CGBA_SCALE_PACK2(f, cgba_avg565(f, h));
		d32[5] = CGBA_SCALE_PACK2(h, i);
		d32[6] = CGBA_SCALE_PACK2(cgba_avg565(i, j), j);
		d32[7] = CGBA_SCALE_PACK2(k, k);
		s32 += 5;
		d32 += 8;
	}
}

static inline void cgba_scale_row_240_384_crisp(const uint16_t *src, uint16_t *dst)
{
	const uint32_t *s32 = (const uint32_t *)(const void *)src;
	uint32_t *d32 = (uint32_t *)(void *)dst;
	unsigned g;

	for (g = 0; g < 24; g++) {
		uint32_t w0 = s32[0], w1 = s32[1], w2 = s32[2];
		uint32_t w3 = s32[3], w4 = s32[4];
		uint16_t a = CGBA_SCALE_HI16(w0), b = CGBA_SCALE_LO16(w0);
		uint16_t c = CGBA_SCALE_HI16(w1), d = CGBA_SCALE_LO16(w1);
		uint16_t e = CGBA_SCALE_HI16(w2), f = CGBA_SCALE_LO16(w2);
		uint16_t h = CGBA_SCALE_HI16(w3), i = CGBA_SCALE_LO16(w3);
		uint16_t j = CGBA_SCALE_HI16(w4), k = CGBA_SCALE_LO16(w4);

		d32[0] = CGBA_SCALE_PACK2(a, b);
		d32[1] = CGBA_SCALE_PACK2(b, c);
		d32[2] = CGBA_SCALE_PACK2(d, d);
		d32[3] = CGBA_SCALE_PACK2(e, e);
		d32[4] = CGBA_SCALE_PACK2(f, h);
		d32[5] = CGBA_SCALE_PACK2(h, i);
		d32[6] = CGBA_SCALE_PACK2(j, j);
		d32[7] = CGBA_SCALE_PACK2(k, k);
		s32 += 5;
		d32 += 8;
	}
}

/* Fullscreen vertical map, 160 source rows -> 216 output rows (27/20).
 * Entry: source row in bits [8:0], bit 15 set = 50/50 blend with row+1.
 * The filter selects the blend rule per output row (see the filter enum). */
#define CGBA_SCALE_VBLEND 0x8000u
#define CGBA_SCALE_VROW   0x01FFu

static inline void cgba_scale_build_vmap216_f(uint16_t *vmap, int filter)
{
	unsigned t;

	for (t = 0; t < 216; t++) {
		unsigned num = t * 20u;
		unsigned s = num / 27u;
		unsigned frac27 = num % 27u;

		if (filter == CGBA_SCALE_FILTER_CRISP) {
			unsigned sr = (num + 13u) / 27u;   /* round to nearest row */
			vmap[t] = (uint16_t)(sr > 159u ? 159u : sr);
		} else {
			int blend = (filter == CGBA_SCALE_FILTER_SMOOTH)
				? (frac27 != 0u)
				: (frac27 >= 7u && frac27 <= 20u);
			uint16_t v = (uint16_t)s;
			if (blend && s + 1u < 160u)
				v |= CGBA_SCALE_VBLEND;
			vmap[t] = v;
		}
	}
}

/* Back-compat: the original SHARP-band vmap. */
static inline void cgba_scale_build_vmap216(uint16_t *vmap)
{
	cgba_scale_build_vmap216_f(vmap, CGBA_SCALE_FILTER_SHARP);
}

#endif /* FXCG100_SCALE_H */
