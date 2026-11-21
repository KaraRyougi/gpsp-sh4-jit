#include "frame_pacing.h"

#include <gint/cpu.h>
#include <gint/rtc.h>
#include <gint/timer.h>

/* The 128 Hz count the pacing is measured against. On the ClassPad II it comes
 * from the platform, which does not depend on the RTC running. */
#ifdef CGBA_CLASSPAD
uint32_t fxcg100_ticks(void);
#define pace_ticks() fxcg100_ticks()
#else
#define pace_ticks() rtc_ticks()
#endif

#define RTC_HZ          128u
#define DEFAULT_TARGET   60
#define DEFAULT_MAX_SKIP  9
/* Windows (half a second each) that must keep up before one skip less is
 * tried: from 2 s, doubling to 64 s while the tries fail. */
#define PACER_HOLD_MIN    4
#define PACER_HOLD_MAX  128
/* One native GBA frame is about 16743 us (59.7275 Hz).  TIMER_ANY normally
 * selects a 32768 Hz ETMU for this delay; timer quantization plus loop overhead
 * therefore keeps normal play below, rather than just above, 60 fps. */
#define FRAME_LIMIT_US CGBA_FRAME_LIMIT_US

/* The frame clock: ticks since the timer was started. */
static int limiter_timer = -1;
static uint32_t limiter_period_us;
static volatile uint32_t limiter_due;

static int limiter_tick(void)
{
	limiter_due++;
	return TIMER_CONTINUE;
}

void cgba_frame_limiter_stop(void)
{
	if(limiter_timer >= 0)
		timer_stop(limiter_timer);
	limiter_timer = -1;
	limiter_period_us = 0;
}

void cgba_frame_limiter_init(cgba_frame_limiter *limiter)
{
	limiter->enabled = 0;
	limiter->period_us = 0;
	limiter->done = 0;
	limiter->late = -1;
}

void cgba_frame_limiter_begin_period(cgba_frame_limiter *limiter,
                                     uint32_t period_us)
{
	limiter->enabled = 0;
	if(period_us == 0u) {
		cgba_frame_limiter_stop();
		limiter->period_us = 0;
		return;
	}
	if(limiter_timer >= 0 && limiter_period_us != period_us)
		cgba_frame_limiter_stop();
	if(limiter_timer < 0) {
		limiter_due = 0;
		limiter->done = 0;
		limiter_timer = timer_configure(TIMER_ANY, period_us,
			GINT_CALL(limiter_tick));
		/* No timer is available.  Running uncapped is safer than stalling
		 * the emulator; the next frame will try to acquire one again. */
		if(limiter_timer < 0) {
			limiter->period_us = 0;
			return;
		}
		limiter_period_us = period_us;
		timer_start(limiter_timer);
	}
	limiter->period_us = period_us;
	limiter->enabled = 1;
}

void cgba_frame_limiter_begin(cgba_frame_limiter *limiter, int enabled)
{
	cgba_frame_limiter_begin_period(limiter, enabled ? FRAME_LIMIT_US : 0u);
}

uint32_t cgba_fast_forward_period_us(uint32_t speed_mode)
{
	switch(speed_mode) {
	case 1u: return 11162u; /* 1.5x (~89.6 fps) */
	case 2u: return 8372u;  /* 2x   (~119.5 fps) */
	case 3u: return 5581u;  /* 3x   (~179.2 fps) */
	case 4u: return 4186u;  /* 4x   (~238.9 fps) */
	default: return 0u;     /* Unlimited */
	}
}

uint32_t cgba_fast_forward_render_period(uint32_t speed_mode)
{
	switch(speed_mode) {
	case 1u: return 2u; /* 1.5x -> ~45 drawn fps */
	case 2u: return 2u; /* 2x   -> ~60 drawn fps */
	case 3u: return 3u; /* 3x   -> ~60 drawn fps */
	case 4u: return 4u; /* 4x   -> ~60 drawn fps */
	default: return 8u; /* Unlimited: maximize throughput */
	}
}

void cgba_frame_limiter_wait(cgba_frame_limiter *limiter)
{
	int32_t late;

	if(!limiter->enabled) {
		limiter->late = -1;
		return;
	}
	limiter->done++;
	while((int32_t)(limiter_due - limiter->done) < 0)
		sleep();
	late = (int32_t)(limiter_due - limiter->done);
	if(late > CGBA_LIMITER_MAX_LATE) {
		limiter->done = limiter_due - CGBA_LIMITER_MAX_LATE;
		late = CGBA_LIMITER_MAX_LATE + 1;    /* time was given up */
	}
	limiter->late = late;
}

static int clampi(int v, int lo, int hi)
{
	if(v < lo)
		return lo;
	if(v > hi)
		return hi;
	return v;
}

void cgba_pacer_init(cgba_pacer *p, int target_fps, int max_skip)
{
	p->target_fps = target_fps > 0 ? target_fps : DEFAULT_TARGET;
	p->max_skip = max_skip > 0 ? max_skip : DEFAULT_MAX_SKIP;
	/* Re-measure roughly twice per target second. */
	p->window_frames = (uint32_t)(p->target_fps / 2);
	if(p->window_frames < 4)
		p->window_frames = 4;
	cgba_pacer_reset(p);
}

void cgba_pacer_reset(cgba_pacer *p)
{
	p->skip = 0;
	p->since_render = 0;
	p->in_window = 0;
	p->start_ticks = pace_ticks();
	p->fed = 0;
	p->worst_late = 0;
	p->bad_windows = 0;
	p->good_windows = 0;
	p->hold = PACER_HOLD_MIN;
	p->lowered = 0;
}

void cgba_pacer_feedback(cgba_pacer *p, int late)
{
	if(late < 0)
		return;
	p->fed++;
	if(late > p->worst_late)
		p->worst_late = late;
}

/* With the limiter on: see frame_pacing.h. A window fell behind if at some
 * point in it the emulator was two frames or more late. */
static void update_from_limiter(cgba_pacer *p)
{
	if(p->worst_late >= 2) {
		p->good_windows = 0;
		if(++p->bad_windows >= 2) {
			p->bad_windows = 0;
			if(p->lowered && p->hold < PACER_HOLD_MAX)
				p->hold *= 2;             /* the try did not hold */
			p->lowered = 0;
			p->skip = clampi(p->skip + 1, 0, p->max_skip);
		}
	} else {
		p->bad_windows = 0;
		if(++p->good_windows >= p->hold) {
			p->good_windows = 0;
			if(p->skip > 0) {
				p->skip--;
				p->lowered = 1;
			}
		}
	}
}

/* RTC ticks since midnight wrap once per day; use modular subtraction and
 * treat an out-of-range delta (e.g. the midnight rollover) as "restart". */
static uint32_t ticks_elapsed(uint32_t start, uint32_t now)
{
	uint32_t day = RTC_HZ * 86400u;
	uint32_t d = now - start;            /* wraps cleanly in unsigned */
	if(d >= day)
		return 0;                        /* rollover / bogus: skip update */
	return d;
}

static void update_window(cgba_pacer *p)
{
	uint32_t now = pace_ticks();
	uint32_t elapsed = ticks_elapsed(p->start_ticks, now);

	if(p->fed * 2 >= (int)p->in_window) {
		update_from_limiter(p);
	} else if(elapsed > 0) {
		/* achieved_fps = in_window * RTC_HZ / elapsed; compare to target
		 * without floating point: behind  <=>  in_window*RTC_HZ < target*elapsed */
		uint32_t lhs = p->in_window * RTC_HZ;
		uint32_t rhs = (uint32_t)p->target_fps * elapsed;

		if(lhs < rhs)
			p->skip = clampi(p->skip + 1, 0, p->max_skip);
		else
			p->skip = clampi(p->skip - 1, 0, p->max_skip);
	}

	p->start_ticks = now;
	p->in_window = 0;
	p->fed = 0;
	p->worst_late = 0;
}

int cgba_pacer_should_render(cgba_pacer *p)
{
	int render;

	if(++p->in_window >= p->window_frames)
		update_window(p);

	if(p->since_render >= p->skip) {
		p->since_render = 0;
		render = 1;
	} else {
		p->since_render++;
		render = 0;
	}

	return render;
}

void cgba_fps_init(cgba_fps_meter *m)
{
	m->emu_frames = 0;
	m->draw_frames = 0;
	m->emu_fps = 0;
	m->draw_fps = 0;
	m->start_ticks = pace_ticks();
}

int cgba_fps_tick(cgba_fps_meter *m, int drawn)
{
	uint32_t now = pace_ticks();
	uint32_t elapsed;

	m->emu_frames++;
	if(drawn)
		m->draw_frames++;

	elapsed = ticks_elapsed(m->start_ticks, now);
	/* A gap far larger than the window (e.g. returning from a paused menu)
	 * would skew one reading; treat it as a fresh window without reporting. */
	if(elapsed >= 4u * RTC_HZ) {
		m->emu_frames = 0;
		m->draw_frames = 0;
		m->start_ticks = now;
		return 0;
	}
	if(elapsed >= RTC_HZ) {            /* ~1 s measurement window */
		m->emu_fps = m->emu_frames * RTC_HZ / elapsed;
		m->draw_fps = m->draw_frames * RTC_HZ / elapsed;
		m->emu_frames = 0;
		m->draw_frames = 0;
		m->start_ticks = now;
		return 1;
	}
	return 0;
}
