#ifndef CGBA_FRAME_PACING_H
#define CGBA_FRAME_PACING_H

#include <stdint.h>

/*
 * Adaptive ("automatic") frameskip controller for the gint gpSP port.
 *
 * The emulator advances one emulated GBA frame per main-loop iteration and the
 * software renderer + LCD blit are the droppable cost. On a CPU-bound target we
 * cannot hit 60 fps, so this controller measures the achieved emulated frame
 * rate over a sliding window (via the 128 Hz RTC) and raises the render-skip
 * count when we are behind the target, lowering it back toward 0 when we keep
 * up. The window is several frames wide so the coarse RTC resolution is fine.
 *
 * This is the real implementation behind the menu's "AUTOMATIC" frameskip type;
 * the "MANUAL" and "OFF" types stay on fxcg100_menu_should_blit().
 */

typedef struct cgba_pacer {
	int   skip;            /* current skip count: render 1 of (skip+1) frames */
	int   max_skip;        /* upper bound on consecutive skipped renders      */
	int   target_fps;      /* emulated frame rate we aim to sustain           */
	int   since_render;    /* emulated frames since the last rendered one     */
	uint32_t window_frames;/* measurement window length, in emulated frames   */
	uint32_t in_window;    /* emulated frames counted in the current window   */
	uint32_t start_ticks;  /* rtc_ticks() at the start of the current window  */
} cgba_pacer;

/* Initialize / reset the controller. target_fps and max_skip are clamped to
 * sane ranges; pass target_fps<=0 to use the GBA's ~60 fps default. */
void cgba_pacer_init(cgba_pacer *p, int target_fps, int max_skip);
void cgba_pacer_reset(cgba_pacer *p);

/* Call exactly once per emulated frame. Returns nonzero when this frame should
 * be rendered + blitted, zero when its render should be skipped. */
int cgba_pacer_should_render(cgba_pacer *p);

#endif
