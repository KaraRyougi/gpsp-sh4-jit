/*
 * Frame-buffer helpers shared by every display backend: the frame hash and the
 * FPS readout. Both work on the 240x160 GBA frame buffer only, so they are
 * independent of the panel and of how the frame reaches it.
 */

#include "fxcg100_platform.h"

#include <stdio.h>
#include <string.h>

#define GBA_W 240
#define GBA_H 160

uint32_t fxcg100_frame_hash(const uint16_t *pixels)
{
	uint32_t hash = 2166136261u;

	for(unsigned i = 0; i < GBA_W * GBA_H; i++) {
		uint16_t px = pixels[i];
		hash ^= (uint8_t)(px >> 8);
		hash *= 16777619u;
		hash ^= (uint8_t)px;
		hash *= 16777619u;
	}

	return hash;
}

/*
 * FPS metrics overlay. Drawn directly into the 240x160 GBA frame buffer (top
 * left) so it is carried by the normal strip-DMA blit -- gint's dtext path
 * cannot compose with the DMA presenter. Minimal 5x7 bitmap font (row-major,
 * bit 4 = leftmost column) for just the glyphs the readout uses. White text on
 * a black box: both colors are byte-order invariant, so no RGB565 endianness
 * concern regardless of the panel's byte order.
 */
#define FPS_GLYPH_W 5
#define FPS_GLYPH_H 7
#define FPS_ADVANCE 6

static const uint8_t fps_font_digit[10][FPS_GLYPH_H] = {
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
	{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
	{0x1F,0x10,0x10,0x1E,0x01,0x11,0x0E}, /* 5 */
	{0x0E,0x11,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
	{0x0E,0x11,0x11,0x0F,0x01,0x11,0x0E}, /* 9 */
};

/* A to Z, for the VSync calibration page's readout. F, P, S, E, V, D and M are
 * the bitmaps the FPS readout always had. */
static const uint8_t fps_font_upper[26][FPS_GLYPH_H] = {
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
	{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
	{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
	{0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* N */
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
	{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
	{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
	{0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, /* Y */
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
};

static const uint8_t *fps_glyph(char c)
{
	static const uint8_t CO[FPS_GLYPH_H] = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
	static const uint8_t PCT[FPS_GLYPH_H]= {0x19,0x1A,0x04,0x0B,0x13,0x03,0x00};
	static const uint8_t SL[FPS_GLYPH_H] = {0x01,0x01,0x02,0x04,0x08,0x10,0x10};
	static const uint8_t MI[FPS_GLYPH_H] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
	static const uint8_t DO[FPS_GLYPH_H] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
	static const uint8_t LT[FPS_GLYPH_H] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02};
	static const uint8_t GT[FPS_GLYPH_H] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08};
	static const uint8_t LP[FPS_GLYPH_H] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
	static const uint8_t RP[FPS_GLYPH_H] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
	static const uint8_t SP[FPS_GLYPH_H] = {0,0,0,0,0,0,0};

	if(c >= '0' && c <= '9')
		return fps_font_digit[c - '0'];
	if(c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	if(c >= 'A' && c <= 'Z')
		return fps_font_upper[c - 'A'];
	switch(c) {
	case ':': return CO;
	case '%': return PCT;
	case '/': return SL;
	case '-': return MI;
	case '.': return DO;
	case '<': return LT;
	case '>': return GT;
	case '(': return LP;
	case ')': return RP;
	default:  return SP;
	}
}

static void fps_draw_text(uint16_t *pixels, int x, int y, const char *s,
	uint16_t fg)
{
	for(; *s; s++) {
		const uint8_t *g = fps_glyph(*s);
		int r;

		for(r = 0; r < FPS_GLYPH_H; r++) {
			uint8_t bits = g[r];
			int col;

			for(col = 0; col < FPS_GLYPH_W; col++) {
				int px = x + col;
				int py = y + r;

				if((bits & (0x10 >> col)) &&
						px >= 0 && px < GBA_W &&
						py >= 0 && py < GBA_H)
					pixels[py * GBA_W + px] = fg;
			}
		}
		x += FPS_ADVANCE;
	}
}

/*
 * Two-line metrics readout drawn into the frame buffer (no file/NOR I/O):
 *   line 1: emulated FPS (E, core throughput) and drawn FPS (D, post-skip)
 *   line 2: speed % of the 60 Hz target and average frame time in ms
 * The per-second emulated FPS is the stable A/B instrument for build tuning.
 */
void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
	unsigned draw_fps)
{
	const uint16_t fg = 0xFFFF;   /* white */
	const uint16_t bg = 0x0000;   /* black */
	char l1[24], l2[24];
	unsigned spd, ms;
	int box_w, w, r, col;

	if(!pixels)
		return;
	if(emu_fps > 999)
		emu_fps = 999;
	if(draw_fps > 999)
		draw_fps = 999;
	spd = emu_fps * 100u / 60u;
	ms = emu_fps ? 1000u / emu_fps : 0u;
	snprintf(l1, sizeof(l1), "FPS E:%u D:%u", emu_fps, draw_fps);
	snprintf(l2, sizeof(l2), "SPD:%u%% %uMS", spd, ms);

	box_w = (int)strlen(l1);
	w = (int)strlen(l2);
	if(w > box_w)
		box_w = w;
	box_w = box_w * FPS_ADVANCE + 1;
	if(box_w > GBA_W)
		box_w = GBA_W;

	for(r = 0; r < 2 * FPS_GLYPH_H + 3 && r < GBA_H; r++)
		for(col = 0; col < box_w; col++)
			pixels[r * GBA_W + col] = bg;

	fps_draw_text(pixels, 1, 1, l1, fg);
	fps_draw_text(pixels, 1, 1 + FPS_GLYPH_H + 1, l2, fg);
}

/* Lines of text in a black box at the top left of a GBA frame buffer, as the
 * FPS readout is drawn: the VSync calibration page's readout. */
void fxcg100_lcd_overlay_lines(uint16_t *pixels, const char *const *lines,
	unsigned count)
{
	const uint16_t fg = 0xFFFF;
	const uint16_t bg = 0x0000;
	int box_w = 0, box_h, r, col;
	unsigned i;

	if(!pixels || !count)
		return;
	for(i = 0; i < count; i++) {
		int w = (int)strlen(lines[i]);

		if(w > box_w)
			box_w = w;
	}
	box_w = box_w * FPS_ADVANCE + 1;
	if(box_w > GBA_W)
		box_w = GBA_W;
	box_h = (int)count * (FPS_GLYPH_H + 1) + 1;
	if(box_h > GBA_H)
		box_h = GBA_H;
	for(r = 0; r < box_h; r++)
		for(col = 0; col < box_w; col++)
			pixels[r * GBA_W + col] = bg;
	for(i = 0; i < count; i++)
		fps_draw_text(pixels, 1, 1 + (int)i * (FPS_GLYPH_H + 1), lines[i],
			fg);
}
