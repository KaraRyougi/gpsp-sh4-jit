#include "frame_pacing.h"

#include <gint/rtc.h>

#define RTC_HZ          128u
#define DEFAULT_TARGET   60
#define DEFAULT_MAX_SKIP  9

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
	p->start_ticks = rtc_ticks();
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
	uint32_t now = rtc_ticks();
	uint32_t elapsed = ticks_elapsed(p->start_ticks, now);

	if(elapsed > 0) {
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
	m->start_ticks = rtc_ticks();
}

int cgba_fps_tick(cgba_fps_meter *m, int drawn)
{
	uint32_t now = rtc_ticks();
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
