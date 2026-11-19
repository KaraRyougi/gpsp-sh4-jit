#ifndef CGBA_TEST_FXCG_GINT_DISPLAY_H
#define CGBA_TEST_FXCG_GINT_DISPLAY_H

/* Host-test stand-in for gint's fx-CG rendering API (396x224 VRAM), for
 * tests/fxcg_present_test.c. The functions are implemented by the test. */

#include <stdint.h>

#define DWIDTH 396
#define DHEIGHT 224

#define C_RGB(r, g, b) \
	((uint16_t)((((r) & 31) << 11) | (((g) & 31) << 6) | ((b) & 31)))
#define C_WHITE 0xffff
#define C_BLACK 0x0000

enum { DTEXT_LEFT = 0, DTEXT_TOP = 0 };

extern uint16_t *gint_vram;
void dclear(int color);
void drect(int x1, int y1, int x2, int y2, int color);
void dtext_opt(int x, int y, int fg, int bg, int halign, int valign,
	const char *text);
void dupdate(void);

#endif
