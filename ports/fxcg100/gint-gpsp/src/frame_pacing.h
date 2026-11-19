#ifndef CGBA_FRAME_PACING_H
#define CGBA_FRAME_PACING_H

#include <stdint.h>

/*
 * Normal-play speed limiter.  One timer ticks once per GBA frame for as long
 * as the limiter is on, and frame n may not end before tick n.  A frame that
 * ends early sleeps until its tick; one that ends late does not, and neither
 * do the frames after it until the lost time has been made up (at most
 * CGBA_LIMITER_MAX_LATE frames of it: more is forgotten, so a long stall is
 * not followed by a burst).
 *
 * It used to be a one-shot timer per frame, so a frame that overran was
 * never made up for and the frames after it still slept their full time:
 * with any frame skipping the game ran slow, and unevenly, by exactly the
 * time the drawn frames took over one frame.
 *
 * Fast-forward bypasses the limiter at the call site.
 */
#define CGBA_LIMITER_MAX_LATE 3

typedef struct cgba_frame_limiter {
	int enabled;
	uint32_t done;       /* frames finished since the timer was started */
	int late;            /* whole frames behind after the last one; <0: n/a */
} cgba_frame_limiter;

void cgba_frame_limiter_init(cgba_frame_limiter *limiter);
void cgba_frame_limiter_begin(cgba_frame_limiter *limiter, int enabled);
void cgba_frame_limiter_wait(cgba_frame_limiter *limiter);
/* Stop the timer (leaving the game loop, or the program). */
void cgba_frame_limiter_stop(void);

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
 *
 * With the limiter on, the controller does not measure a rate (which the
 * limiter holds a hair under the target, so the old comparison came out
 * either way at random and the skip count wandered up and down every half
 * second, changing the rhythm of the picture each time). It goes by how far
 * behind the limiter says the emulator is: the skip count goes up when two
 * windows in a row fell behind, and is tried one lower only after a stretch
 * of windows that kept up, a stretch that doubles each time the try fails.
 * So the count settles, and the picture is drawn at even intervals.
 */

typedef struct cgba_pacer {
	int   skip;            /* current skip count: render 1 of (skip+1) frames */
	int   max_skip;        /* upper bound on consecutive skipped renders      */
	int   target_fps;      /* emulated frame rate we aim to sustain           */
	int   since_render;    /* emulated frames since the last rendered one     */
	uint32_t window_frames;/* measurement window length, in emulated frames   */
	uint32_t in_window;    /* emulated frames counted in the current window   */
	uint32_t start_ticks;  /* rtc_ticks() at the start of the current window  */
	/* From the limiter (cgba_pacer_feedback): */
	int   fed;             /* frames with feedback in the current window      */
	int   worst_late;      /* most frames behind seen in the current window   */
	int   bad_windows;     /* consecutive windows that fell behind            */
	int   good_windows;    /* consecutive windows that kept up                */
	int   hold;            /* windows to keep up before trying one skip less  */
	int   lowered;         /* the last change was a try at one skip less      */
} cgba_pacer;

/* Initialize / reset the controller. target_fps and max_skip are clamped to
 * sane ranges; pass target_fps<=0 to use the GBA's ~60 fps default. */
void cgba_pacer_init(cgba_pacer *p, int target_fps, int max_skip);
void cgba_pacer_reset(cgba_pacer *p);

/* Call exactly once per emulated frame. Returns nonzero when this frame should
 * be rendered + blitted, zero when its render should be skipped. */
int cgba_pacer_should_render(cgba_pacer *p);

/* Call once per emulated frame after cgba_frame_limiter_wait(), with the
 * limiter's `late` (negative when the limiter is off). */
void cgba_pacer_feedback(cgba_pacer *p, int late);

/*
 * FPS meter for on-screen performance metrics. Independent of the pacer / of the
 * frameskip mode: it counts emulated frames and drawn/blitted frames over a ~1 s
 * RTC window and exposes the two rates. draw_fps is expected to fall when
 * frameskip is enabled; emu_fps is the core throughput/speed metric.
 */
typedef struct cgba_fps_meter {
	uint32_t emu_frames;   /* emulated frames counted in the current window  */
	uint32_t draw_frames;  /* drawn/blitted frames in the current window     */
	uint32_t start_ticks;  /* rtc_ticks() at the start of the current window */
	uint32_t emu_fps;      /* last computed emulated frames/sec              */
	uint32_t draw_fps;     /* last computed drawn/blitted frames/sec         */
} cgba_fps_meter;

void cgba_fps_init(cgba_fps_meter *m);

/* Call once per emulated frame; drawn != 0 when the frame was blitted.
 * Refreshes emu_fps/draw_fps once per window; returns nonzero on a refresh. */
int cgba_fps_tick(cgba_fps_meter *m, int drawn);

#endif
