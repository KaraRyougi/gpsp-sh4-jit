#ifndef CGBA_FXCG100_LCD_CLEANUP_H
#define CGBA_FXCG100_LCD_CLEANUP_H

#define FXCG100_LCD_OS_OVERLAY_ROWS 32u

/* First column outside the gameplay viewport on its right. Fullscreen returns
 * display_width because the next frame overwrites the OS indicator itself. */
static inline unsigned fxcg100_lcd_cleanup_right_start(unsigned scale_mode,
	unsigned display_width)
{
	unsigned viewport_width;

	if(scale_mode == 2)
		return display_width;
	viewport_width = scale_mode == 1 ? 320u : 240u;
	if(viewport_width >= display_width)
		return display_width;
	return (display_width - viewport_width) / 2u + viewport_width;
}

static inline unsigned fxcg100_lcd_cleanup_rows(unsigned display_height)
{
	return display_height < FXCG100_LCD_OS_OVERLAY_ROWS ? display_height :
		FXCG100_LCD_OS_OVERLAY_ROWS;
}

#endif
