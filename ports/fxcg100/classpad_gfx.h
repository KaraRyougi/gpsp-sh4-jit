#ifndef CGBA_CLASSPAD_GFX_H
#define CGBA_CLASSPAD_GFX_H

/* Drawing for the fx-CP400 touch keys and interface: filled and rounded
 * rectangles with anti-aliased corners, outlines, small anti-aliased lines
 * and triangles for icons, and proportional text from the 4-bit glyph tables
 * in classpad_font.h.
 *
 * Everything is drawn in the coordinates the player sees, into a surface
 * that stores its pixels in *panel* order. That way a surface can be pushed
 * to the LCD as-is in either orientation: the whole of gint's VRAM is one
 * such surface (320x528 seen upright, 528x320 seen turned), and so is the
 * small buffer a single touch key is rendered into.
 *
 * Pure RGB565 integer code, shared with the host tests. */

#include <stdint.h>

#include "classpad_font_types.h"
#include "classpad_layout.h"

#define CP_RGB(r, g, b) \
	((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

enum {
	CP_SURFACE_UPRIGHT = 0,    /* portrait */
	CP_SURFACE_KEYS_RIGHT,     /* landscape, calculator turned anticlockwise */
	CP_SURFACE_KEYS_LEFT,      /* landscape, calculator turned clockwise */
	CP_SURFACE_FLIPPED         /* portrait, calculator upside down */
};

typedef struct cp_surface {
	uint16_t *pixels;
	int w, h;                  /* size as the player sees it */
	int mode;                  /* CP_SURFACE_* */
	cp_rect clip;              /* drawing is limited to this rectangle */
	int ox, oy;                /* added to every coordinate drawn (not to the
	                              clip): lets a whole screen slide */
} cp_surface;

static inline int cp_surface_mode(unsigned view, unsigned rotation)
{
	if (cp_view_is_flipped(view, rotation))
		return CP_SURFACE_FLIPPED;
	if (!cp_view_is_landscape(view))
		return CP_SURFACE_UPRIGHT;
	return rotation == CP_ROTATION_KEYS_LEFT ? CP_SURFACE_KEYS_LEFT
		: CP_SURFACE_KEYS_RIGHT;
}

static inline void cp_surface_init(cp_surface *s, uint16_t *pixels, int w,
	int h, int mode)
{
	s->pixels = pixels;
	s->w = w;
	s->h = h;
	s->mode = mode;
	s->clip = (cp_rect){ 0, 0, (int16_t)w, (int16_t)h };
	s->ox = 0;
	s->oy = 0;
}

/* Limit drawing to a rectangle (intersected with the surface). */
static inline void cp_surface_clip(cp_surface *s, cp_rect r)
{
	int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
	int x1 = r.x + r.w > s->w ? s->w : r.x + r.w;
	int y1 = r.y + r.h > s->h ? s->h : r.y + r.h;

	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	s->clip = (cp_rect){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0),
		(int16_t)(y1 - y0) };
}

static inline void cp_surface_unclip(cp_surface *s)
{
	s->clip = (cp_rect){ 0, 0, (int16_t)s->w, (int16_t)s->h };
}

static inline int cp_surface_visible(const cp_surface *s, int x, int y)
{
	return x >= s->clip.x && x < s->clip.x + s->clip.w &&
		y >= s->clip.y && y < s->clip.y + s->clip.h;
}

/* Address of a visible pixel. The panel scans what is a column to the player
 * in landscape, hence the two turned mappings. */
static inline uint16_t *cp_surface_pixel(const cp_surface *s, int x, int y)
{
	if (s->mode == CP_SURFACE_UPRIGHT)
		return &s->pixels[y * s->w + x];
	if (s->mode == CP_SURFACE_FLIPPED)
		return &s->pixels[(s->h - 1 - y) * s->w + (s->w - 1 - x)];
	if (s->mode == CP_SURFACE_KEYS_RIGHT)
		return &s->pixels[x * s->h + (s->h - 1 - y)];
	return &s->pixels[(s->w - 1 - x) * s->h + y];
}

/* dst + (src - dst) * cover / full, per channel. */
static inline uint16_t cp_blend(uint16_t dst, uint16_t src, int cover, int full)
{
	int r = (dst >> 11) & 31, g = (dst >> 5) & 63, b = dst & 31;

	r += ((((src >> 11) & 31) - r) * cover) / full;
	g += ((((src >> 5) & 63) - g) * cover) / full;
	b += (((src & 31) - b) * cover) / full;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

/* The functions named cp__* take surface coordinates (offset already
 * applied); the public ones add the surface offset first. */

/* Blend one pixel with `cover` of 4, if it is inside the clip. */
static inline void cp__plot(const cp_surface *s, int x, int y, uint16_t color,
	int cover)
{
	uint16_t *p;

	if (cover <= 0 || !cp_surface_visible(s, x, y))
		return;
	p = cp_surface_pixel(s, x, y);
	*p = cover >= 4 ? color : cp_blend(*p, color, cover, 4);
}

/* Solid rectangle, written in memory order whichever way the surface is
 * turned. */
static inline void cp__fill(const cp_surface *s, int x, int y, int w, int h,
	uint16_t color)
{
	int x0 = x < s->clip.x ? s->clip.x : x;
	int y0 = y < s->clip.y ? s->clip.y : y;
	int x1 = x + w > s->clip.x + s->clip.w ? s->clip.x + s->clip.w : x + w;
	int y1 = y + h > s->clip.y + s->clip.h ? s->clip.y + s->clip.h : y + h;
	int i, n;

	if (x1 <= x0 || y1 <= y0)
		return;
	if (s->mode == CP_SURFACE_UPRIGHT) {
		for (; y0 < y1; y0++) {
			uint16_t *p = &s->pixels[y0 * s->w + x0];

			for (n = x1 - x0; n > 0; n--)
				*p++ = color;
		}
		return;
	}
	if (s->mode == CP_SURFACE_FLIPPED) {
		for (; y0 < y1; y0++) {
			uint16_t *p = &s->pixels[(s->h - 1 - y0) * s->w + (s->w - x1)];

			for (n = x1 - x0; n > 0; n--)
				*p++ = color;
		}
		return;
	}
	for (i = x0; i < x1; i++) {
		uint16_t *p = s->mode == CP_SURFACE_KEYS_RIGHT
			? &s->pixels[i * s->h + (s->h - y1)]
			: &s->pixels[(s->w - 1 - i) * s->h + y0];

		for (n = y1 - y0; n > 0; n--)
			*p++ = color;
	}
}

static inline void cp_fill(const cp_surface *s, int x, int y, int w, int h,
	uint16_t color)
{
	cp__fill(s, x + s->ox, y + s->oy, w, h, color);
}

/* How much of pixel (i, j) a w x h rounded rectangle of radius r covers, in
 * quarters: 4 inside, 0 outside, in between on the corner arcs. Distances are
 * in half pixels so pixel centres are integers. */
static inline int cp_rr_cover(int i, int j, int w, int h, int r)
{
	int px = 2 * i + 1, py = 2 * j + 1, dx = 0, dy = 0, d2, inner, outer;

	if (i < 0 || j < 0 || i >= w || j >= h)
		return 0;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	if (px < 2 * r)
		dx = 2 * r - px;
	else if (px > 2 * (w - r))
		dx = px - 2 * (w - r);
	if (py < 2 * r)
		dy = 2 * r - py;
	else if (py > 2 * (h - r))
		dy = py - 2 * (h - r);
	if (!dx || !dy)
		return 4;
	d2 = dx * dx + dy * dy;
	inner = (2 * r - 1) * (2 * r - 1);
	outer = (2 * r + 1) * (2 * r + 1);
	if (d2 <= inner)
		return 4;
	if (d2 >= outer)
		return 0;
	return 1 + (3 * (outer - d2)) / (outer - inner);
}

/* Filled rounded rectangle with anti-aliased corners, blended over what is
 * already there. A radius of half the size gives a circle. Only the four
 * corner squares are done pixel by pixel; the rest is plain fills. */
static inline void cp_round_rect(const cp_surface *s, int x, int y, int w,
	int h, int r, uint16_t color)
{
	int i, j;

	x += s->ox;
	y += s->oy;
	if (w <= 0 || h <= 0)
		return;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	if (r <= 0) {
		cp__fill(s, x, y, w, h, color);
		return;
	}
	cp__fill(s, x, y + r, w, h - 2 * r, color);
	cp__fill(s, x + r, y, w - 2 * r, r, color);
	cp__fill(s, x + r, y + h - r, w - 2 * r, r, color);
	if (y + r <= s->clip.y && y + h - r >= s->clip.y + s->clip.h)
		return;                       /* no corner is on screen */
	for (j = 0; j < r; j++)
		for (i = 0; i < r; i++) {
			int cover = cp_rr_cover(i, j, 2 * r, 2 * r, r);

			if (!cover)
				continue;
			cp__plot(s, x + i, y + j, color, cover);
			cp__plot(s, x + w - 1 - i, y + j, color, cover);
			cp__plot(s, x + i, y + h - 1 - j, color, cover);
			cp__plot(s, x + w - 1 - i, y + h - 1 - j, color, cover);
		}
}

/* The outline alone of a rounded rectangle, `t` pixels thick, drawn inside
 * its bounds. */
static inline void cp_round_ring(const cp_surface *s, int x, int y, int w,
	int h, int r, int t, uint16_t color)
{
	int c, i, j, ri;

	x += s->ox;
	y += s->oy;
	if (w <= 2 * t || h <= 2 * t)
		return;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	ri = r > t ? r - t : 0;
	c = r > t ? r : t;                /* size of a corner block */
	if (2 * c > w) c = w / 2;
	if (2 * c > h) c = h / 2;
	cp__fill(s, x + c, y, w - 2 * c, t, color);
	cp__fill(s, x + c, y + h - t, w - 2 * c, t, color);
	cp__fill(s, x, y + c, t, h - 2 * c, color);
	cp__fill(s, x + w - t, y + c, t, h - 2 * c, color);
	for (j = 0; j < c; j++)
		for (i = 0; i < c; i++) {
			int cover = cp_rr_cover(i, j, w, h, r) -
				cp_rr_cover(i - t, j - t, w - 2 * t, h - 2 * t, ri);

			cp__plot(s, x + i, y + j, color, cover);
			cp__plot(s, x + w - 1 - i, y + j, color, cover);
			cp__plot(s, x + i, y + h - 1 - j, color, cover);
			cp__plot(s, x + w - 1 - i, y + h - 1 - j, color, cover);
		}
}

/* A rounded outline `t` pixels thick: the shape in `edge` with the inside
 * refilled in `fill`. */
static inline void cp_round_frame(const cp_surface *s, int x, int y, int w,
	int h, int r, int t, uint16_t edge, uint16_t fill)
{
	cp_round_rect(s, x, y, w, h, r, edge);
	cp_round_rect(s, x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0,
		fill);
}

static inline void cp_disc(const cp_surface *s, int cx, int cy, int r,
	uint16_t color)
{
	cp_round_rect(s, cx - r, cy - r, 2 * r, 2 * r, r, color);
}

/* Paint what lies outside a quarter circle in the r x r square at (x, y), so
 * a plain rectangle drawn earlier gets a rounded corner. `corner`: bit 0 set
 * for a right-hand corner, bit 1 for a bottom one. */
static inline void cp_corner(const cp_surface *s, int x, int y, int r,
	int corner, uint16_t color)
{
	int i, j;

	x += s->ox;
	y += s->oy;
	if (y >= s->clip.y + s->clip.h || y + r <= s->clip.y)
		return;
	for (j = 0; j < r; j++)
		for (i = 0; i < r; i++)
			cp__plot(s, x + ((corner & 1) ? r - 1 - i : i),
				y + ((corner & 2) ? r - 1 - j : j), color,
				4 - cp_rr_cover(i, j, 2 * r, 2 * r, r));
}

static inline int cp_isqrt(int v)
{
	int r = 0, bit = 1 << 30;

	while (bit > v)
		bit >>= 2;
	for (; bit; bit >>= 2) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
	}
	return r;
}

/* A translucent disc of radius `rr` about (cx, cy), kept inside a rounded
 * rectangle: the "ripple" that spreads over a control from where it is
 * touched. `alpha` is out of 256. */
static inline void cp_ripple(const cp_surface *s, int x, int y, int w, int h,
	int r, int cx, int cy, int rr, uint16_t color, int alpha)
{
	int x0, y0, x1, y1, i, j;

	x += s->ox;
	y += s->oy;
	cx += s->ox;
	cy += s->oy;
	x0 = x < s->clip.x ? s->clip.x : x;
	y0 = y < s->clip.y ? s->clip.y : y;
	x1 = x + w > s->clip.x + s->clip.w ? s->clip.x + s->clip.w : x + w;
	y1 = y + h > s->clip.y + s->clip.h ? s->clip.y + s->clip.h : y + h;
	if (x0 < cx - rr) x0 = cx - rr;
	if (y0 < cy - rr) y0 = cy - rr;
	if (x1 > cx + rr + 1) x1 = cx + rr + 1;
	if (y1 > cy + rr + 1) y1 = cy + rr + 1;

	for (j = y0; j < y1; j++)
		for (i = x0; i < x1; i++) {
			uint16_t *p;

			if ((i - cx) * (i - cx) + (j - cy) * (j - cy) > rr * rr ||
					cp_rr_cover(i - x, j - y, w, h, r) < 4)
				continue;
			p = cp_surface_pixel(s, i, j);
			*p = cp_blend(*p, color, alpha, 256);
		}
}

/* Anti-aliased line with round ends, `width` pixels thick, between pixel
 * centres. Meant for small icons: every pixel of the bounding box is
 * tested. Distances are in eighths of a pixel. */
static inline void cp_line(const cp_surface *s, int x0, int y0, int x1,
	int y1, int width, uint16_t color)
{
	int dx = (x1 - x0) * 8, dy = (y1 - y0) * 8;
	int len2 = dx * dx + dy * dy, len = cp_isqrt(len2);
	int bx0 = (x0 < x1 ? x0 : x1) - width, bx1 = (x0 > x1 ? x0 : x1) + width;
	int by0 = (y0 < y1 ? y0 : y1) - width, by1 = (y0 > y1 ? y0 : y1) + width;
	int i, j;

	for (j = by0; j <= by1; j++)
		for (i = bx0; i <= bx1; i++) {
			int px = (i - x0) * 8, py = (j - y0) * 8;
			int along = px * dx + py * dy, d, cover;

			if (along <= 0 || !len) {
				d = cp_isqrt(px * px + py * py);
			} else if (along >= len2) {
				d = cp_isqrt((px - dx) * (px - dx) + (py - dy) * (py - dy));
			} else {
				d = px * dy - py * dx;
				d = (d < 0 ? -d : d) / len;
			}
			cover = (width * 4 + 4 - d) / 2;
			cp__plot(s, i + s->ox, j + s->oy, color, cover > 4 ? 4 : cover);
		}
}

/* Filled triangle, anti-aliased by testing four points per pixel. For
 * icons. */
static inline void cp_triangle(const cp_surface *s, int x0, int y0, int x1,
	int y1, int x2, int y2, uint16_t color)
{
	int bx0 = x0 < x1 ? x0 : x1, bx1 = x0 > x1 ? x0 : x1;
	int by0 = y0 < y1 ? y0 : y1, by1 = y0 > y1 ? y0 : y1;
	int i, j, k;

	if (x2 < bx0) bx0 = x2;
	if (x2 > bx1) bx1 = x2;
	if (y2 < by0) by0 = y2;
	if (y2 > by1) by1 = y2;
	for (j = by0; j <= by1; j++)
		for (i = bx0; i <= bx1; i++) {
			int cover = 0;

			for (k = 0; k < 4; k++) {
				/* Sample points at quarter-pixel offsets, in quarter pixels
				 * relative to the vertices' pixel centres. */
				int qx = (i - x0) * 4 + ((k & 1) ? 1 : -1);
				int qy = (j - y0) * 4 + ((k & 2) ? 1 : -1);
				int ax = (x1 - x0) * 4, ay = (y1 - y0) * 4;
				int bx = (x2 - x0) * 4, by = (y2 - y0) * 4;
				int e0 = ax * qy - ay * qx;
				int e1 = (bx - ax) * (qy - ay) - (by - ay) * (qx - ax);
				int e2 = -(bx * qy - by * qx);

				if ((e0 >= 0 && e1 >= 0 && e2 >= 0) ||
						(e0 <= 0 && e1 <= 0 && e2 <= 0))
					cover++;
			}
			cp__plot(s, i + s->ox, j + s->oy, color, cover);
		}
}

/* ---- text ----------------------------------------------------------------- */

static inline const cp_glyph *cp_font_glyph(const cp_font *f, char c)
{
	int code = (unsigned char)c;

	if (code < CP_FONT_FIRST || code > CP_FONT_LAST)
		code = '?';
	return &f->glyphs[code - CP_FONT_FIRST];
}

/* Width in pixels of the first `len` characters of `text` (all of it when
 * len is negative). */
static inline int cp_text_width_n(const cp_font *f, const char *text, int len)
{
	int w = 0, i;

	for (i = 0; text[i] && (len < 0 || i < len); i++)
		w += cp_font_glyph(f, text[i])->advance;
	return w;
}

static inline int cp_text_width(const cp_font *f, const char *text)
{
	return cp_text_width_n(f, text, -1);
}

/* How much ink a glyph pixel gets (of 32) for its coverage (of 15).
 *
 * Blending is done on the RGB565 values, which are not linear in light: a
 * half-covered pixel of light text on a dark ground comes out darker than
 * half, so thin strokes look thinner and uneven. Light text therefore gets
 * more ink in the partly covered pixels (coverage to the power 0.6, a little
 * short of the full correction, which looks bold). Dark text on a light
 * ground errs the other way and is left alone. */
static const uint8_t cp_text_ink_light[16] = {
	0, 6, 10, 12, 14, 17, 18, 20, 22, 24, 25, 27, 28, 29, 31, 32
};
static const uint8_t cp_text_ink_dark[16] = {
	0, 2, 4, 6, 9, 11, 13, 15, 17, 19, 21, 23, 26, 28, 30, 32
};

/* Draw text with its top-left corner at (x, y), blending the glyph coverage
 * over what is already there. Returns the x after the last glyph. */
static inline int cp_text_n(const cp_surface *s, const cp_font *f, int x,
	int y, const char *text, int len, uint16_t color)
{
	int light = ((color >> 11) & 31) * 3 + ((color >> 5) & 63) * 3 +
		(color & 31) > 150;
	const uint8_t *ink = light ? cp_text_ink_light : cp_text_ink_dark;
	int i;

	x += s->ox;
	y += s->oy;
	for (i = 0; text[i] && (len < 0 || i < len); i++) {
		const cp_glyph *g = cp_font_glyph(f, text[i]);
		const uint8_t *row = f->data + g->offset;
		int bytes = (g->w + 1) / 2, gx, gy;

		for (gy = 0; gy < g->h; gy++, row += bytes) {
			int py = y + g->top + gy;

			if (py < s->clip.y || py >= s->clip.y + s->clip.h)
				continue;
			for (gx = 0; gx < g->w; gx++) {
				int cover = (row[gx / 2] >> (4 - 4 * (gx % 2))) & 15;
				int px = x + g->left + gx;
				uint16_t *p;

				if (!cover || px < s->clip.x || px >= s->clip.x + s->clip.w)
					continue;
				p = cp_surface_pixel(s, px, py);
				*p = cover == 15 ? color : cp_blend(*p, color, ink[cover], 32);
			}
		}
		x += g->advance;
	}
	return x - s->ox;
}

static inline int cp_text(const cp_surface *s, const cp_font *f, int x, int y,
	const char *text, uint16_t color)
{
	return cp_text_n(s, f, x, y, text, -1, color);
}

/* Text centred in a rectangle, horizontally and on its line box. */
static inline void cp_text_centered(const cp_surface *s, const cp_font *f,
	int x, int y, int w, int h, const char *text, uint16_t color)
{
	cp_text(s, f, x + (w - cp_text_width(f, text)) / 2,
		y + (h - f->height) / 2, text, color);
}

/* Text that must fit `max_w`: drawn whole if it does, otherwise cut and
 * finished with "...". */
static inline void cp_text_fit(const cp_surface *s, const cp_font *f, int x,
	int y, int max_w, const char *text, uint16_t color)
{
	int dots = cp_text_width(f, "..."), n = 0, w = 0;

	if (cp_text_width(f, text) <= max_w) {
		cp_text(s, f, x, y, text, color);
		return;
	}
	while (text[n] && w + cp_font_glyph(f, text[n])->advance + dots <= max_w)
		w += cp_font_glyph(f, text[n++])->advance;
	x = cp_text_n(s, f, x, y, text, n, color);
	cp_text(s, f, x, y, "...", color);
}

#endif /* CGBA_CLASSPAD_GFX_H */
