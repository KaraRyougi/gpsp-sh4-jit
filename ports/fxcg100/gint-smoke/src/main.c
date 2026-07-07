#include <gint/display.h>

static void draw_smoke_screen(void)
{
	dclear(C_WHITE);

	drect(0, 0, DWIDTH - 1, 21, C_RGB(0, 10, 22));
	dtext(8, 5, C_WHITE, "gpSP GINT SMOKE");

	drect_border(8, 34, DWIDTH - 9, 120,
		C_RGB(30, 31, 30), 1, C_RGB(0, 13, 24));
	dtext(20, 48, C_BLACK, "gint display path is active");
	dtext(20, 66, C_BLACK, "No raw LCD init, no debug port");
	dtext(20, 84, C_BLACK, "Auto-return after display hold");

	drect(0, 136, DWIDTH - 1, 157, C_RGB(31, 0, 0));
	drect(0, 158, DWIDTH - 1, 179, C_RGB(0, 28, 0));
	drect(0, 180, DWIDTH - 1, 201, C_RGB(0, 0, 31));
	drect(0, 202, DWIDTH - 1, DHEIGHT - 1, C_RGB(31, 27, 0));

	dtext(20, 126, C_RGB(20, 20, 20), "keyboard disabled in this isolate");

	dupdate();
}

int main(void)
{
	draw_smoke_screen();

	for(volatile unsigned int i = 0; i < 120000000; i++)
		;

	return 1;
}
