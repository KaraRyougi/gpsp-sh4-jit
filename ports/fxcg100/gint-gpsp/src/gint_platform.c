#include "fxcg100_platform.h"

#include <gint/dma.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/keyboard.h>

#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define GBA_W 240
#define GBA_H 160
#define LCD_DATA_REGISTER ((uintptr_t)0xb4000000u)
#define GBA_FRAME_BYTES (GBA_W * GBA_H * (int)sizeof(uint16_t))
#define GBA_FRAME_DMA_BLOCKS (GBA_FRAME_BYTES / 32)

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

	return keys;
}

uint32_t fxcg100_poll_gba_buttons(void)
{
	uint32_t buttons = FXCG100_GBA_BUTTON_NONE;

	clearevents();

	if(fxcg100_key_down(49))
		buttons |= FXCG100_GBA_BUTTON_UP;
	if(fxcg100_key_down(47))
		buttons |= FXCG100_GBA_BUTTON_DOWN;
	if(fxcg100_key_down(58))
		buttons |= FXCG100_GBA_BUTTON_LEFT;
	if(fxcg100_key_down(38))
		buttons |= FXCG100_GBA_BUTTON_RIGHT;

	if(fxcg100_key_down(31) || fxcg100_key_down(48))
		buttons |= FXCG100_GBA_BUTTON_A;
	if(fxcg100_key_down(68) || fxcg100_key_down(34))
		buttons |= FXCG100_GBA_BUTTON_B;

	if(fxcg100_key_down(29))
		buttons |= FXCG100_GBA_BUTTON_START;
	if(fxcg100_key_down(69))
		buttons |= FXCG100_GBA_BUTTON_SELECT;
	if(fxcg100_key_down(59))
		buttons |= FXCG100_GBA_BUTTON_L;
	if(fxcg100_key_down(39))
		buttons |= FXCG100_GBA_BUTTON_R;

	return buttons;
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

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
	int ox = (DWIDTH - GBA_W) / 2;
	int oy = (DHEIGHT - GBA_H) / 2;
	uint16_t *vram = gint_vram;

	if(!pixels)
		return;

	wait_lcd_dma();

	/*
	 * Composite the 240x160 GBA frame into the centre of the full 396x224
	 * gint VRAM (black borders) and push the WHOLE width through gint's
	 * R61524 driver.
	 *
	 * The R61524 DMA path requires the full horizontal range. The previous
	 * version narrowed the GRAM window to the 240-wide sub-rectangle
	 * (r61524_start_frame at x=78..317) and DMA'd into it; the casio-emu DMA
	 * model tolerates that, but real R61524 hardware rejects a partial-width
	 * DMA and leaves the panel blank (the "white screen on a loaded game").
	 * Driving the full width via r61524_display() matches the menu path,
	 * which is why the menu shows correctly on hardware while gameplay did not.
	 */
	memset(vram, 0, (size_t)DWIDTH * DHEIGHT * sizeof(uint16_t));
	for(int y = 0; y < GBA_H; y++)
		memcpy(&vram[(oy + y) * DWIDTH + ox], &pixels[y * GBA_W],
			(size_t)GBA_W * sizeof(uint16_t));

	r61524_display(vram, 0, DHEIGHT, R61524_DMA_WAIT);
	lcd_window_partial = 0;   /* full-width push leaves the window full */
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
