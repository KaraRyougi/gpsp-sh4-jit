/*
 * Panic-screen drawing, isolated from gpSP headers: <gint/display.h> clashes
 * with gpSP's fixed-size typedefs (u32 ...), so the renderer lives in its own
 * translation unit and gpsp_runner.c only passes formatted strings.
 *
 * Panic rendering must not depend on interrupts or the DMA driver: the
 * exception may have hit mid display-DMA with interrupts disabled, and a
 * dupdate()-based present can hang forever (observed on overclocked hardware
 * as "black screen for a few seconds" before the report appeared). Draw into
 * gint's VRAM with plain stores and present through the R61524 CPU
 * (programmed-I/O) path — fully synchronous, no DMA, no IRQs.
 */

#include <gint/display.h>
#include <gint/drivers/r61524.h>

void cgba_panic_draw_begin(void)
{
	dclear(C_BLACK);
}

void cgba_panic_draw_line(int row, const char *text)
{
	dtext_opt(4, 4 + row * 14, C_WHITE, C_BLACK, DTEXT_LEFT, DTEXT_TOP,
		text);
}

void cgba_panic_draw_present(void)
{
	/* Full-window reset first: gameplay narrows the R61524 window for the
	 * strip blit; a panic mid-strip would otherwise present into a partial
	 * window. r61524_win_set + CPU present are both PIO — panic-safe. */
	r61524_win_set(0, 395, 0, 223);
	r61524_display(gint_vram, 0, 224, R61524_CPU);
}
