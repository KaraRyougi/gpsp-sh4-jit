/* The speed limiter and the automatic frame skipping of frame_pacing.c on a
 * virtual clock: an emulator whose frames cost a fixed time, more when they
 * are drawn. What must hold is what a player sees: the game runs at full
 * speed whenever that is possible at all, and the picture is drawn at even
 * intervals, i.e. the skip count stops changing. */
#include <stdint.h>
#include <stdio.h>

#include <gint/timer.h>
#include <gint/cpu.h>
#include "ports/fxcg100/gint-gpsp/src/frame_pacing.h"

static int failures;
#define CHECK(cond, ...) do { if(!(cond)) { \
	printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
	printf("\n"); failures++; } } while(0)

/* ---- virtual clock and the one timer -------------------------------------- */
static uint64_t now_us;
static uint64_t timer_period, timer_next;
static gint_call_t timer_call;
static int timer_running;

static void advance(uint64_t to)
{
	while(timer_running && timer_next <= to) {
		now_us = timer_next;
		timer_next += timer_period;
		if(timer_call() == TIMER_STOP)
			timer_running = 0;
	}
	if(to > now_us)
		now_us = to;
}

int timer_configure(int timer, uint64_t delay_us, gint_call_t callback)
{
	(void)timer;
	timer_period = delay_us;
	timer_call = callback;
	return 0;
}
void timer_start(int timer) { (void)timer; timer_running = 1; timer_next = now_us + timer_period; }
void timer_stop(int timer) { (void)timer; timer_running = 0; }
void sleep(void) { advance(timer_running ? timer_next : now_us + 1000); }
uint32_t fxcg100_ticks(void) { return (uint32_t)(now_us * 128u / 1000000u); }
uint32_t rtc_ticks(void) { return fxcg100_ticks(); }

/* ---- one run --------------------------------------------------------------- */
typedef struct result {
	double speed;          /* emulated frames per second over the last part */
	int skip, changes;     /* final skip count; changes of it in that part */
	int hitch_recovered;
} result;

/* `plain` and `drawn` are the cost of a frame in microseconds. A stall of
 * `hitch` microseconds happens once, half-way. */
static result run(uint32_t plain, uint32_t drawn, uint32_t hitch, int seconds)
{
	cgba_frame_limiter limiter;
	cgba_pacer pacer;
	result r = { 0, 0, 0, 0 };
	int frames = seconds * 60, f, last_skip = -1;
	int tail_from = frames * 2 / 3, tail_frames = 0;
	uint64_t tail_start = 0;

	now_us = 0;
	timer_running = 0;
	cgba_frame_limiter_init(&limiter);
	cgba_pacer_init(&pacer, 60, 9);
	for(f = 0; f < frames; f++) {
		int render;

		cgba_frame_limiter_begin(&limiter, 1);
		render = cgba_pacer_should_render(&pacer);
		advance(now_us + (render ? drawn : plain));
		if(hitch && f == frames / 2)
			advance(now_us + hitch);
		cgba_frame_limiter_wait(&limiter);
		cgba_pacer_feedback(&pacer, limiter.late);

		if(f == tail_from) {
			tail_start = now_us;
			last_skip = pacer.skip;
		} else if(f > tail_from) {
			tail_frames++;
			if(pacer.skip != last_skip) {
				r.changes++;
				last_skip = pacer.skip;
			}
		}
	}
	cgba_frame_limiter_stop();
	r.speed = tail_frames * 1e6 / (double)(now_us - tail_start);
	r.skip = pacer.skip;
	return r;
}

int main(void)
{
	/* frame costs: not drawn, drawn; the skip count that keeps full speed */
	static const struct { uint32_t plain, drawn; int skip; } cases[] = {
		{  6000, 12000, 0 },   /* everything fits in one frame */
		{  9000, 24000, 1 },   /* one drawn in two: 33.0 of 33.5 ms */
		{  9000, 30000, 2 },
		{ 12000, 40000, 5 },
		{  8000, 21000, 1 },
	};
	unsigned c;
	result r;

	for(c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		r = run(cases[c].plain, cases[c].drawn, 0, 600);
		CHECK(r.speed > 59.0 && r.speed < 60.2,
			"case %u: %.1f frames a second", c, r.speed);
		CHECK(r.skip == cases[c].skip || r.skip == cases[c].skip + 1,
			"case %u: settled on skip %d, %d keeps full speed", c, r.skip,
			cases[c].skip);
		/* One skip less is tried now and then, and taken back: at most a
		 * couple of times in the last 200 seconds. */
		CHECK(r.changes <= 8, "case %u: the skip count changed %d times",
			c, r.changes);
	}

	/* A stall of a third of a second (the JIT translating a new area) is
	 * not made up for by a burst, and does not leave the skip count up. */
	r = run(9000, 24000, 330000, 600);
	CHECK(r.skip <= 2 && r.speed > 59.0, "after a stall: skip %d, %.1f fps",
		r.skip, r.speed);

	/* An emulator that cannot reach full speed even drawing nothing: the
	 * skip count goes to its limit and stays. */
	r = run(18000, 30000, 0, 300);
	CHECK(r.skip == 9 && r.changes == 0, "too slow: skip %d, %d changes",
		r.skip, r.changes);

	/* Fast-forward speed presets: capped multipliers hold their target fps
	 * and switching periods dynamically reconfigures the timer cleanly. */
	{
		static const struct {
			uint32_t speed_mode;
			double min_fps, max_fps;
			uint32_t render_period;
		} ff_cases[] = {
			{ 1u,  88.0,  91.0, 2u }, /* 1.5x (~89.6 fps) */
			{ 2u, 118.0, 121.0, 2u }, /* 2x   (~119.5 fps) */
			{ 3u, 177.0, 181.0, 3u }, /* 3x   (~179.2 fps) */
			{ 4u, 236.0, 241.0, 4u }, /* 4x   (~238.9 fps) */
		};
		cgba_frame_limiter lim;

		CHECK(cgba_fast_forward_period_us(0u) == 0u &&
			cgba_fast_forward_render_period(0u) == 8u,
			"unlimited FF preset");
		now_us = 0;
		timer_running = 0;
		cgba_frame_limiter_init(&lim);
		for(c = 0; c < sizeof(ff_cases) / sizeof(ff_cases[0]); c++) {
			uint32_t period = cgba_fast_forward_period_us(
				ff_cases[c].speed_mode);
			uint64_t t0 = now_us;
			int f;

			CHECK(cgba_fast_forward_render_period(ff_cases[c].speed_mode) ==
				ff_cases[c].render_period, "FF render period %u", c);
			for(f = 0; f < 120; f++) {
				cgba_frame_limiter_begin_period(&lim, period);
				advance(now_us + 2000u);
				cgba_frame_limiter_wait(&lim);
			}
			double fps = 120.0 * 1e6 / (double)(now_us - t0);
			CHECK(fps >= ff_cases[c].min_fps && fps <= ff_cases[c].max_fps,
				"FF mode %u: %.1f fps", ff_cases[c].speed_mode, fps);
		}
		/* Unlimited disables the timer; returning to 1x re-enables it. */
		cgba_frame_limiter_begin_period(&lim, 0u);
		CHECK(!lim.enabled && !timer_running, "unlimited stops timer");
		cgba_frame_limiter_begin(&lim, 1);
		CHECK(lim.enabled && timer_running &&
			lim.period_us == CGBA_FRAME_LIMIT_US, "back to 1x limiter");
		cgba_frame_limiter_stop();
	}

	/* Midnight RTC rollover (now < start_ticks): cgba_fps_tick() must reset
	 * its window immediately instead of stalling for 24 hours. */
	{
		cgba_fps_meter fps;
		int reported = 0;

		/* Set clock 0.5 s before midnight (86400 * 128 - 64). */
		now_us = ((uint64_t)(86400u * 128u - 64u) * 1000000ull) / 128ull;
		cgba_fps_init(&fps);
		for(int i = 0; i < 30; i++) {
			now_us += 16667ull;
			(void)cgba_fps_tick(&fps, 1);
		}
		/* Roll over past midnight back to ~0.1 s. */
		now_us = 100000ull;
		CHECK(cgba_fps_tick(&fps, 1) == 0,
			"midnight rollover tick should reset window without reporting");
		/* Run 1.1 s after midnight; FPS meter must recover and report ~60 fps. */
		for(int i = 0; i < 70; i++) {
			now_us += 16667ull;
			if(cgba_fps_tick(&fps, 1))
				reported++;
		}
		CHECK(reported >= 1 && fps.emu_fps >= 55u && fps.emu_fps <= 65u,
			"FPS meter stalled after midnight rollover (reported=%d fps=%u)",
			reported, (unsigned)fps.emu_fps);
	}

	if(failures)
		return 1;
	printf("frame_pacing_test: all checks passed\n");
	return 0;
}
