/*
 * Frame-buffer helpers shared by every display backend: the frame hash and the
 * FPS readout. Both work on the 240x160 GBA frame buffer only, so they are
 * independent of the panel and of how the frame reaches it.
 */

#include "cgba_hash.h"
#include "fxcg100_platform.h"

#include <stdio.h>
#include <string.h>

#define GBA_W 240
#define GBA_H 160

uint32_t fxcg100_frame_hash(const uint16_t *pixels)
{
	return cgba_fnv1a32_rgb565(pixels, GBA_W * GBA_H);
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

static const uint8_t *fps_glyph(char c)
{
	static const uint8_t F[FPS_GLYPH_H]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
	static const uint8_t P[FPS_GLYPH_H]  = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
	static const uint8_t S[FPS_GLYPH_H]  = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
	static const uint8_t E[FPS_GLYPH_H]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
	static const uint8_t V[FPS_GLYPH_H]  = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
	static const uint8_t D[FPS_GLYPH_H]  = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
	static const uint8_t M[FPS_GLYPH_H]  = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
	static const uint8_t A[FPS_GLYPH_H]  = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
	static const uint8_t X[FPS_GLYPH_H]  = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
	static const uint8_t GT[FPS_GLYPH_H] = {0x10,0x08,0x04,0x02,0x04,0x08,0x10};
	static const uint8_t DT[FPS_GLYPH_H] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
	static const uint8_t CO[FPS_GLYPH_H] = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
	static const uint8_t PCT[FPS_GLYPH_H]= {0x19,0x1A,0x04,0x0B,0x13,0x03,0x00};
	static const uint8_t SP[FPS_GLYPH_H] = {0,0,0,0,0,0,0};

	if(c >= '0' && c <= '9')
		return fps_font_digit[c - '0'];
	switch(c) {
	case 'F': return F;
	case 'P': return P;
	case 'S': return S;
	case 'E': return E;
	case 'V': return V;
	case 'D': return D;
	case 'M': return M;
	case 'A': return A;
	case 'X': return X;
	case '>': return GT;
	case '.': return DT;
	case ':': return CO;
	case '%': return PCT;
	default:  return SP;
	}
}

static void fps_draw_text_stride(uint16_t *buf, int stride, int max_w, int max_h,
	int x, int y, const char *s, uint16_t fg)
{
	for(; *s; s++) {
		const uint8_t *g = fps_glyph(*s);
		int r;

		for(r = 0; r < FPS_GLYPH_H; r++) {
			uint8_t bits = g[r];
			int py = y + r;
			int col;

			if(py < 0 || py >= max_h)
				continue;
			for(col = 0; col < FPS_GLYPH_W; col++) {
				int px = x + col;

				if((bits & (0x10 >> col)) &&
						px >= 0 && px < max_w)
					buf[py * stride + px] = fg;
			}
		}
		x += FPS_ADVANCE;
	}
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE && !defined(CGBA_HOST_TEST)
extern void video_invalidate_scanline_cache(uint32_t start_y, uint32_t end_y);
#endif

/*
 * Two-line metrics readout drawn into the frame buffer (no file/NOR I/O):
 *   line 1: emulated FPS (E, core throughput) and drawn FPS (D, post-skip)
 *   line 2: speed % of the 60 Hz target and average frame time in ms
 * The per-second emulated FPS is the stable A/B instrument for build tuning.
 * Cached as a pre-rendered badge and rebuilt only when the numbers change.
 */
#define FPS_BOX_H       (2 * FPS_GLYPH_H + 3)
#define FPS_BADGE_MAX_W 128

static uint16_t fps_badge[FPS_BOX_H][FPS_BADGE_MAX_W];
static unsigned fps_cached_emu;
static unsigned fps_cached_draw;
static int fps_cached_w;
static int fps_badge_valid;

void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
	unsigned draw_fps)
{
	const uint16_t fg = 0xFFFF;   /* white */
	int r;

	if(!pixels)
		return;
	if(emu_fps > 999)
		emu_fps = 999;
	if(draw_fps > 999)
		draw_fps = 999;

	if(!fps_badge_valid || emu_fps != fps_cached_emu ||
			draw_fps != fps_cached_draw) {
		char l1[24], l2[24];
		unsigned spd = emu_fps * 100u / 60u;
		unsigned ms = emu_fps ? 1000u / emu_fps : 0u;
		int box_w, w;

		snprintf(l1, sizeof(l1), "FPS E:%u D:%u", emu_fps, draw_fps);
		snprintf(l2, sizeof(l2), "SPD:%u%% %uMS", spd, ms);

		box_w = (int)strlen(l1);
		w = (int)strlen(l2);
		if(w > box_w)
			box_w = w;
		box_w = box_w * FPS_ADVANCE + 1;
		if(box_w > FPS_BADGE_MAX_W)
			box_w = FPS_BADGE_MAX_W;
		if(box_w > GBA_W)
			box_w = GBA_W;

		memset(fps_badge, 0, sizeof(fps_badge));
		fps_draw_text_stride(&fps_badge[0][0], FPS_BADGE_MAX_W,
			box_w, FPS_BOX_H, 1, 1, l1, fg);
		fps_draw_text_stride(&fps_badge[0][0], FPS_BADGE_MAX_W,
			box_w, FPS_BOX_H, 1, 1 + FPS_GLYPH_H + 1, l2, fg);
		fps_cached_emu = emu_fps;
		fps_cached_draw = draw_fps;
		fps_cached_w = box_w;
		fps_badge_valid = 1;
	}

	for(r = 0; r < FPS_BOX_H && r < GBA_H; r++)
		memcpy(&pixels[r * GBA_W], fps_badge[r],
			(size_t)fps_cached_w * sizeof(uint16_t));
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE && !defined(CGBA_HOST_TEST)
	video_invalidate_scanline_cache(0, (uint32_t)FPS_BOX_H);
#endif
}

/*
 * Fast-forward status badge drawn into the top-right of the 240x160 GBA frame
 * buffer so it never collides with the top-left FPS readout. Cached and
 * rebuilt only when the configured speed label changes.
 */
#define FF_BOX_H       (FPS_GLYPH_H + 4)
#define FF_BADGE_MAX_W 52

static uint16_t ff_badge[FF_BOX_H][FF_BADGE_MAX_W];
static uint32_t ff_cached_speed;
static int ff_cached_w;
static int ff_badge_valid;

static const char *ff_speed_badge_label(uint32_t speed_mode)
{
	switch(speed_mode) {
	case FXCG100_FF_SPEED_1_5X: return ">> 1.5X";
	case FXCG100_FF_SPEED_2X:   return ">> 2X";
	case FXCG100_FF_SPEED_3X:   return ">> 3X";
	case FXCG100_FF_SPEED_4X:   return ">> 4X";
	default:                    return ">> MAX";
	}
}

void fxcg100_lcd_overlay_fast_forward(uint16_t *pixels, uint32_t speed_mode)
{
	const uint16_t fg = 0xFFFF;
	int r, x0;

	if(!pixels)
		return;
	if(speed_mode >= FXCG100_FF_SPEED_COUNT)
		speed_mode = FXCG100_FF_SPEED_UNLIMITED;

	if(!ff_badge_valid || speed_mode != ff_cached_speed) {
		const char *label = ff_speed_badge_label(speed_mode);
		int box_w = (int)strlen(label) * FPS_ADVANCE + 3;

		if(box_w > FF_BADGE_MAX_W)
			box_w = FF_BADGE_MAX_W;
		if(box_w > GBA_W)
			box_w = GBA_W;

		memset(ff_badge, 0, sizeof(ff_badge));
		fps_draw_text_stride(&ff_badge[0][0], FF_BADGE_MAX_W,
			box_w, FF_BOX_H, 2, 2, label, fg);
		ff_cached_speed = speed_mode;
		ff_cached_w = box_w;
		ff_badge_valid = 1;
	}

	x0 = GBA_W - ff_cached_w;
	for(r = 0; r < FF_BOX_H && r < GBA_H; r++)
		memcpy(&pixels[r * GBA_W + x0], ff_badge[r],
			(size_t)ff_cached_w * sizeof(uint16_t));
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE && !defined(CGBA_HOST_TEST)
	video_invalidate_scanline_cache(0, (uint32_t)FF_BOX_H);
#endif
}
