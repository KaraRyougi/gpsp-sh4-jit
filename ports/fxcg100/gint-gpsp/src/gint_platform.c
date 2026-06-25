#include "fxcg100_platform.h"

#include <gint/dma.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/keyboard.h>

#include <stdio.h>
#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define GBA_W 240
#define GBA_H 160
#define LCD_DATA_REGISTER ((uintptr_t)0xb4000000u)
#define GBA_FRAME_BYTES (GBA_W * GBA_H * (int)sizeof(uint16_t))
#define GBA_FRAME_DMA_BLOCKS (GBA_FRAME_BYTES / 32)

/*
 * Direct-LCD strip DMA from on-chip XY-RAM (uncached, DMA-safe), mirroring the
 * working cgbc / prizoop presenter. DMAing the frame out of gint_vram is what
 * produced the white screen on real hardware: gint_vram is in cached RAM, so
 * the DMAC reads stale memory (casio-emu has no cache model, which is why it
 * looked fine in the emulator). XY-RAM is on-chip and uncached, so the DMA sees
 * the bytes we just wrote. Strips keep the R61524 4-row DMA window alignment
 * (the GBA frame origin oy=32 and STRIP_LINES are both multiples of 4).
 */
#define STRIP_LINES 12
#define STRIP_BUF0  ((uintptr_t)0xe5007000u)
#define STRIP_BUF1  ((uintptr_t)0xe5017000u)
_Static_assert(STRIP_LINES * GBA_W * 2 <= 0x2000,
	"GBA strip must fit one 8KB on-chip XY-RAM bank");
_Static_assert(STRIP_LINES % 4 == 0,
	"strip height must stay 4-row aligned for the R61524 DMA window");

static int lcd_dma_pending;
/* The gameplay blit narrows the R61524 GRAM window to the GBA rectangle via
 * r61524_start_frame(). gint's dclear/dtext/dupdate menu rendering assumes the
 * full 396x224 window, so we must restore it before any gint push or the menu
 * shows a black screen with a stray line. Tracked here, healed at push time. */
static int lcd_window_partial;

static void wait_lcd_dma(void)
{
	if(!lcd_dma_pending)
		return;

	dma_transfer_wait(0);
	lcd_dma_pending = 0;
}

/* Hand the panel back to gint: finish any direct blit DMA and restore the full
 * display window. No-op unless a gameplay blit narrowed the window. */
static void restore_full_window(void)
{
	if(!lcd_window_partial)
		return;

	wait_lcd_dma();
	r61524_win_set(0, DWIDTH - 1, 0, DHEIGHT - 1);
	lcd_window_partial = 0;
}

static int keycode_from_cg100_matrix_code(int basic_keycode)
{
	int row = basic_keycode % 10;
	int col = basic_keycode / 10 - 1;

	if(row < 0 || row > 9 || col < 0 || col > 6)
		return 0;

	return (row << 4) + (7 - col);
}

int fxcg100_key_down(int basic_keycode)
{
	int keycode = keycode_from_cg100_matrix_code(basic_keycode);

	return keycode ? keydown(keycode) : 0;
}

uint32_t fxcg100_poll_app_keys(void)
{
	uint32_t keys = 0;

	clearevents();

	if(fxcg100_key_down(77))
		keys |= FXCG100_APPKEY_SHIFT;
	if(fxcg100_key_down(69))
		keys |= FXCG100_APPKEY_HOME;
	if(fxcg100_key_down(34))
		keys |= FXCG100_APPKEY_AC;
	if(fxcg100_key_down(31) || fxcg100_key_down(48))
		keys |= FXCG100_APPKEY_EXE;
	if(fxcg100_key_down(27))
		keys |= FXCG100_APPKEY_MENU;
	if(fxcg100_key_down(49))
		keys |= FXCG100_APPKEY_UP;
	if(fxcg100_key_down(47))
		keys |= FXCG100_APPKEY_DOWN;
	if(fxcg100_key_down(58))
		keys |= FXCG100_APPKEY_LEFT;
	if(fxcg100_key_down(38))
		keys |= FXCG100_APPKEY_RIGHT;
	if(fxcg100_key_down(68))
		keys |= FXCG100_APPKEY_BACK;
	if(fxcg100_key_down(FXCG100_PHYSKEY_ON))
		keys |= FXCG100_APPKEY_ON;

	return keys;
}

void fxcg100_lcd_init(void)
{
}

void fxcg100_lcd_clear(uint16_t color)
{
	wait_lcd_dma();
	dclear((int)color);
}

void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
	uint16_t color)
{
	if(x >= LCD_W || y >= LCD_H || w == 0 || h == 0)
		return;

	wait_lcd_dma();
	if(x + w > LCD_W)
		w = LCD_W - x;
	if(y + h > LCD_H)
		h = LCD_H - y;

	drect((int)x, (int)y, (int)(x + w - 1), (int)(y + h - 1),
		(int)color);
}

void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
	uint16_t fg, uint16_t bg)
{
	wait_lcd_dma();
	dtext_opt((int)x, (int)y, (int)fg, (int)bg,
		DTEXT_LEFT, DTEXT_TOP, text ? text : "");
}

void fxcg100_lcd_update(void)
{
	restore_full_window();
	wait_lcd_dma();
	dupdate();
}

void fxcg100_lcd_status(const char *text)
{
	uint16_t bg = C_RGB(0, 10, 22);
	uint16_t fg = C_WHITE;

	restore_full_window();
	wait_lcd_dma();
	drect(0, 0, DWIDTH - 1, 13, bg);
	dtext_opt(4, 2, fg, bg, DTEXT_LEFT, DTEXT_TOP, text ? text : "");
	dupdate();
}

/* Start the DMA of one prepared strip (in on-chip RAM) to the LCD window.
 * Falls back to CPU writes if the DMA channel is unavailable. */
static void start_strip_dma(const uint16_t *strip, int x, int y,
	int width, int rows)
{
	unsigned blocks = (unsigned)(width * rows) * sizeof(uint16_t) / 32u;

	r61524_start_frame(x, x + width - 1, y, y + rows - 1);
	if(dma_transfer_async(0, DMA_32B, blocks, (void *)strip, DMA_INC,
			(void *)LCD_DATA_REGISTER, DMA_FIXED, GINT_CALL_NULL)) {
		lcd_dma_pending = 1;
		return;
	}

	volatile uint16_t *display = (volatile uint16_t *)LCD_DATA_REGISTER;
	for(int i = 0; i < width * rows; i++)
		display[0] = strip[i];
}

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
	int ox = (DWIDTH - GBA_W) / 2;   /* 78 */
	int oy = (DHEIGHT - GBA_H) / 2;  /* 32 (4-aligned) */
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	int y, bi;
	int rows;

	if(!pixels)
		return;

	/*
	 * Present the frame as 12-row strips DMA'd out of on-chip XY-RAM (see the
	 * note by STRIP_LINES). Double-buffered: copy the next strip into the
	 * other on-chip bank while the current strip's DMA runs.
	 */
	wait_lcd_dma();

	y = 0;
	rows = STRIP_LINES;
	memcpy(strips[0], &pixels[y * GBA_W],
		(size_t)rows * GBA_W * sizeof(uint16_t));
	start_strip_dma(strips[0], ox, oy + y, GBA_W, rows);

	bi = 1;
	y += rows;
	while(y < GBA_H) {
		rows = (y + STRIP_LINES > GBA_H) ? (GBA_H - y) : STRIP_LINES;
		memcpy(strips[bi], &pixels[y * GBA_W],
			(size_t)rows * GBA_W * sizeof(uint16_t));
		wait_lcd_dma();   /* wait for the previous strip's DMA */
		start_strip_dma(strips[bi], ox, oy + y, GBA_W, rows);
		bi ^= 1;
		y += rows;
	}

	wait_lcd_dma();           /* frame complete before returning */
	lcd_window_partial = 1;   /* window narrowed; menu restores it */
}

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

static const uint8_t *fps_glyph(char c)
{
	static const uint8_t F[FPS_GLYPH_H]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
	static const uint8_t P[FPS_GLYPH_H]  = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
	static const uint8_t S[FPS_GLYPH_H]  = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
	static const uint8_t E[FPS_GLYPH_H]  = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
	static const uint8_t V[FPS_GLYPH_H]  = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
	static const uint8_t CO[FPS_GLYPH_H] = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
	static const uint8_t SP[FPS_GLYPH_H] = {0,0,0,0,0,0,0};

	if(c >= '0' && c <= '9')
		return fps_font_digit[c - '0'];
	switch(c) {
	case 'F': return F;
	case 'P': return P;
	case 'S': return S;
	case 'E': return E;
	case 'V': return V;
	case ':': return CO;
	default:  return SP;
	}
}

void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
	unsigned vid_fps)
{
	const uint16_t fg = 0xFFFF;   /* white */
	const uint16_t bg = 0x0000;   /* black */
	char buf[24];
	int len, box_w, x, i, r, col;

	if(!pixels)
		return;
	if(emu_fps > 999)
		emu_fps = 999;
	if(vid_fps > 999)
		vid_fps = 999;
	snprintf(buf, sizeof(buf), "FPS E:%u V:%u", emu_fps, vid_fps);

	len = (int)strlen(buf);
	box_w = len * FPS_ADVANCE + 1;
	if(box_w > GBA_W)
		box_w = GBA_W;

	for(r = 0; r < FPS_GLYPH_H + 2 && r < GBA_H; r++)
		for(col = 0; col < box_w; col++)
			pixels[r * GBA_W + col] = bg;

	x = 1;
	for(i = 0; i < len; i++) {
		const uint8_t *g = fps_glyph(buf[i]);

		for(r = 0; r < FPS_GLYPH_H; r++) {
			uint8_t bits = g[r];

			for(col = 0; col < FPS_GLYPH_W; col++) {
				int px = x + col;
				int py = 1 + r;

				if((bits & (0x10 >> col)) &&
						px >= 0 && px < GBA_W &&
						py >= 0 && py < GBA_H)
					pixels[py * GBA_W + px] = fg;
			}
		}
		x += FPS_ADVANCE;
	}
}
