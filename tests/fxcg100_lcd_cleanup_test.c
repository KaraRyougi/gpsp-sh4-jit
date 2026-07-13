#include <stdio.h>

#include "ports/fxcg100/fxcg100_lcd_cleanup.h"

static int failures;

static void check(int condition, const char *name)
{
	if(condition)
		return;
	printf("FAIL: %s\n", name);
	failures++;
}

int main(void)
{
	check(fxcg100_lcd_cleanup_right_start(0, 396) == 318,
		"1:1 cleanup starts after centered 240px viewport");
	check(fxcg100_lcd_cleanup_right_start(1, 396) == 358,
		"4:3 cleanup starts after centered 320px viewport");
	check(fxcg100_lcd_cleanup_right_start(2, 396) == 396,
		"fullscreen has no border cleanup");
	check(fxcg100_lcd_cleanup_right_start(0, 200) == 200,
		"undersized display produces no cleanup");
	check(fxcg100_lcd_cleanup_rows(224) == 32,
		"cleanup is limited to OS indicator rows");
	check(fxcg100_lcd_cleanup_rows(20) == 20,
		"cleanup rows clamp to display height");

	if(failures)
		return 1;
	puts("fx-CG BFile LCD cleanup geometry passed");
	return 0;
}
