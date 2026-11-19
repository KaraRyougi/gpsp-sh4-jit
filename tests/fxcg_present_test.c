/*
 * The fx-CG presenter (ports/fxcg100/gint-gpsp/src/gint_platform.c) against a
 * simulated panel whose refresh runs on a clock of its own:
 *
 *   - every display scaling and filter paints the same picture whichever way
 *     its strips go, and with VSync on they go the way the refresh goes;
 *   - with VSync on, a panel with a row counter and the refresh position
 *     that matches it, frames are not torn, whichever way the refresh runs
 *     (the DCS address mode's MY bit, or the calibration page's flip);
 *   - without VSync, or with the refresh taken the wrong way round, they are
 *     (so the tear check below can see a tear);
 *   - on the calibration page a position one step out tears;
 *   - a panel without a row counter is left alone: nothing waits.
 *
 * Time is counted in pixel writes. A frame is torn if the refresh shows the
 * new picture on some of its rows in one sweep and on others only in the
 * next.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { \
	if(!(cond)) { \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		failures++; \
	} \
} while(0)

/* ---- the simulated panel -------------------------------------------------- */

#define GRAM_W 396
#define GRAM_H 224

static uint16_t gram[GRAM_H][GRAM_W];
static uint64_t row_written_at[GRAM_H];
static unsigned row_serial[GRAM_H];
static unsigned frame_serial;
static uint64_t now;

static int win_x0, win_x1, win_y1, win_x, win_y;
static long win_left;
/* The first row of each window opened during a frame, in order. */
static int strip_rows[64];
static int strip_count;

/* The refresh: `tot` rows a sweep, `line` time units a row. The picture's
 * first row of the count is `blank` rows in; the count shows the panel's
 * rows top down unless `refresh_up`. */
static int panel_counter = 1;
static uint32_t panel_id_value = 82, panel_mode_value;
static uint32_t tot = 256, line = 520, blank = 16;
static int refresh_up;
static uint64_t phase;

static uint32_t counter_at(uint64_t t)
{
	return (uint32_t)((t / line + phase) % tot);
}

uint32_t cg_host_scanline(void)
{
	now += 4;
	/* A register that is not a counter reads the same every time. */
	return panel_counter ? counter_at(now) : 0x0123u;
}

uint32_t cg_host_panel_id(void) { return panel_id_value; }
uint32_t cg_host_panel_mode(void) { return panel_mode_value; }

/* 128 Hz from a 60 Hz refresh. */
uint32_t fxcg100_ticks(void)
{
	uint64_t tick = (uint64_t)tot * line * 60u / 128u;

	return (uint32_t)(now / tick);
}

void r61524_start_frame(int xmin, int xmax, int ymin, int ymax)
{
	CHECK(xmin >= 0 && xmax < GRAM_W && xmin <= xmax, "window x %d..%d",
		xmin, xmax);
	CHECK(ymin >= 0 && ymax < GRAM_H && ymin <= ymax, "window y %d..%d",
		ymin, ymax);
	CHECK(win_left == 0, "window opened with %ld pixels still due",
		win_left);
	win_x0 = win_x = xmin;
	win_x1 = xmax;
	win_y = ymin;
	win_y1 = ymax;
	win_left = (long)(xmax - xmin + 1) * (ymax - ymin + 1);
	if(strip_count < 64)
		strip_rows[strip_count++] = ymin;
	now += 20;
}

/* The menu's row pushes (fxcg100_lcd_update_rows) are not part of what is
 * tested here. */
static uint16_t host_vram[396 * 224];
uint16_t *gint_vram = host_vram;

void r61524_display(uint16_t *vram, int start, int height, int method)
{
	(void)vram; (void)start; (void)height; (void)method;
}

void r61524_win_set(uint16_t HSA, uint16_t HEA, uint16_t VSA, uint16_t VEA)
{
	(void)HSA; (void)HEA; (void)VSA; (void)VEA;
}

static void panel_write(uint16_t pixel)
{
	CHECK(win_left > 0, "pixel written past the window");
	if(win_left <= 0)
		return;
	gram[win_y][win_x] = pixel;
	row_written_at[win_y] = now;
	row_serial[win_y] = frame_serial;
	now++;
	win_left--;
	if(++win_x > win_x1) {
		win_x = win_x0;
		win_y++;
	}
}

#include <gint/dma.h>

bool dma_transfer_async(int channel, dma_size_t size, unsigned blocks,
	void const *src, dma_address_t src_mode, void *dst,
	dma_address_t dst_mode, gint_call_t callback)
{
	const uint16_t *pixels = src;

	(void)dst; (void)callback;
	CHECK(channel == 0 && size == DMA_32B && src_mode == DMA_INC &&
		dst_mode == DMA_FIXED, "unexpected DMA configuration");
	CHECK(((uintptr_t)src & 31) == 0, "DMA source not 32-byte aligned");
	CHECK((long)blocks * 16 == win_left,
		"DMA of %u pixels into a window with %ld left", blocks * 16u,
		win_left);
	for(unsigned i = 0; i < blocks * 16u; i++)
		panel_write(pixels[i]);
	return true;
}

void dma_transfer_wait(int channel) { (void)channel; }

void dclear(int color) { (void)color; }
void drect(int x1, int y1, int x2, int y2, int color)
{ (void)x1; (void)y1; (void)x2; (void)y2; (void)color; }
void dtext_opt(int x, int y, int fg, int bg, int halign, int valign,
	const char *text)
{ (void)x; (void)y; (void)fg; (void)bg; (void)halign; (void)valign;
  (void)text; }
void dupdate(void) {}
int keydown(int key) { (void)key; return 0; }
void clearevents(void) {}

#include "ports/fxcg100/gint-gpsp/src/gint_platform.c"

/* ---- helpers -------------------------------------------------------------- */

static uint16_t frame_pixels[240 * 160] __attribute__((aligned(4)));
static uint16_t reference[GRAM_H][GRAM_W];

static void random_frame(uint32_t seed)
{
	for(unsigned i = 0; i < 240u * 160u; i++) {
		seed = seed * 1664525u + 1013904223u;
		frame_pixels[i] = (uint16_t)(seed >> 16);
	}
}

/* The panel rows each scaling paints (gint_platform.c). */
static void picture_rows(uint32_t scale, int *y0, int *y1)
{
	static const int top[3] = { 32, 4, 4 }, rows[3] = { 160, 212, 216 };

	*y0 = top[scale];
	*y1 = top[scale] + rows[scale];
}

/* The sweep in which the refresh first shows row y after it was written. */
static int64_t first_sweep_after(int y)
{
	uint32_t c = blank + (uint32_t)(refresh_up ? GRAM_H - 1 - y : y);
	int64_t u = (int64_t)(row_written_at[y] / line + phase);
	/* the first u' > u with u' = c (mod tot) */
	int64_t n = (u + 1 - (int64_t)c + (int64_t)tot - 1) / (int64_t)tot;

	if(u + 1 - (int64_t)c < 0)
		n = 0;
	return n;
}

/* Blit the frame in `frame_pixels`; returns 1 if it was torn, and checks it
 * reached every row of the picture. */
static int blit_frame(uint32_t scale, int *upward)
{
	int y0, y1, y, torn;
	int64_t lo = INT64_MAX, hi = INT64_MIN;

	frame_serial++;
	strip_count = 0;
	fxcg100_lcd_blit_gba(frame_pixels);
	picture_rows(scale, &y0, &y1);
	for(y = y0; y < y1; y++) {
		int64_t n;

		CHECK(row_serial[y] == frame_serial, "scale %u: row %d not written",
			(unsigned)scale, y);
		n = first_sweep_after(y);
		if(n < lo)
			lo = n;
		if(n > hi)
			hi = n;
	}
	torn = hi != lo;
	if(upward) {
		int i, up = 1, down = 1;

		for(i = 1; i < strip_count; i++) {
			if(strip_rows[i] >= strip_rows[i - 1])
				up = 0;
			if(strip_rows[i] <= strip_rows[i - 1])
				down = 0;
		}
		*upward = up ? 1 : down ? 0 : -1;
	}
	return torn;
}

/* A fresh start of gpSP on a given panel. */
static void panel_reset(int counter, uint32_t id, uint32_t mode, int up)
{
	panel_counter = counter;
	panel_id_value = id;
	panel_mode_value = mode;
	refresh_up = up;
	panel_known = 0;
	scan_state = SCAN_UNKNOWN;
	memset(sync_lines, 0, sizeof(sync_lines));
	memset(sync_strips, 0, sizeof(sync_strips));
	fxcg100_lcd_sync_calibrate(0);
	fxcg100_lcd_set_sync(0, 0);
	fxcg100_lcd_set_sync_flip(0);
}

/* Frames with the emulation of the next one in between (up to one and a
 * half refreshes). Returns how many were torn, the first few of each start
 * left out: the first frame of a kind has nothing measured to go by. */
static int run_frames(uint32_t scale, int frames, uint32_t seed, int *upward)
{
	int torn = 0, i;

	for(i = 0; i < frames; i++) {
		seed = seed * 1103515245u + 12345u;
		now += (seed >> 8) % ((uint64_t)tot * line * 3u / 2u);
		random_frame(seed);
		if(blit_frame(scale, upward) && i >= 3)
			torn++;
	}
	return torn;
}

/* ---- tests ---------------------------------------------------------------- */

/* Any strip order paints the same picture. */
static void test_pictures(void)
{
	uint32_t scale, filter;

	for(scale = 0; scale < 3; scale++) {
		for(filter = 0; filter < 3; filter++) {
			int y0, y1, y, dir;

			fxcg100_lcd_set_scale(scale);
			fxcg100_lcd_set_filter(filter);
			picture_rows(scale, &y0, &y1);
			random_frame(scale * 7u + filter);

			panel_reset(1, 82, 0x00, 0);
			memset(gram, 0, sizeof(gram));
			blit_frame(scale, &dir);
			CHECK(dir == 0, "scale %u: without VSync strips go down",
				(unsigned)scale);
			memcpy(reference, gram, sizeof(gram));

			/* MY set: the refresh starts at the last row. */
			panel_reset(1, 82, 0x80, 1);
			fxcg100_lcd_set_sync(1, 1);
			memset(gram, 0, sizeof(gram));
			blit_frame(scale, &dir);   /* counts the rows first */
			blit_frame(scale, &dir);
			CHECK(dir == 1, "scale %u: with MY set strips go up (%d)",
				(unsigned)scale, dir);
			for(y = y0; y < y1; y++)
				CHECK(!memcmp(gram[y], reference[y], sizeof(gram[y])),
					"scale %u filter %u: row %d differs going up",
					(unsigned)scale, (unsigned)filter, y);

			panel_reset(1, 82, 0x00, 0);
			fxcg100_lcd_set_sync(1, 1);
			memset(gram, 0, sizeof(gram));
			blit_frame(scale, &dir);
			blit_frame(scale, &dir);
			CHECK(dir == 0, "scale %u: with MY clear strips go down (%d)",
				(unsigned)scale, dir);
			for(y = y0; y < y1; y++)
				CHECK(!memcmp(gram[y], reference[y], sizeof(gram[y])),
					"scale %u filter %u: row %d differs going down",
					(unsigned)scale, (unsigned)filter, y);
		}
	}
	fxcg100_lcd_set_filter(0);
}

static void test_counter_found(void)
{
	uint32_t rows, hz, id, mode;
	int upward;

	panel_reset(1, 82, 0x80, 1);
	CHECK(fxcg100_lcd_sync_info(&rows, &hz) == 0,
		"nothing is read before VSync is wanted");
	fxcg100_lcd_set_scale(0);
	random_frame(1);
	blit_frame(0, NULL);
	CHECK(fxcg100_lcd_sync_info(&rows, &hz) == 0,
		"VSync off: the panel is not read");
	fxcg100_lcd_set_sync(1, 1);
	blit_frame(0, NULL);
	CHECK(fxcg100_lcd_sync_info(&rows, &hz) == 1 && rows == tot &&
		hz >= 58 && hz <= 62, "counter: %u rows at %u Hz",
		(unsigned)rows, (unsigned)hz);
	CHECK(fxcg100_lcd_sync_panel(&id, &mode, &upward) && id == 82 &&
		mode == 0x80 && upward == 1, "panel %u mode %x up %d",
		(unsigned)id, (unsigned)mode, upward);
}

/* VSync with the right position: no tears, either way round, any scaling.
 * Without it: tears. */
static void test_tearing(void)
{
	static const struct { uint32_t mode; int up; uint32_t flip; } cases[] = {
		{ 0x00, 0, 0 },   /* refresh down, address mode says so */
		{ 0x80, 1, 0 },   /* refresh up, MY set */
		{ 0x00, 1, 1 },   /* refresh up, MY clear: flipped by hand */
		{ 0x80, 0, 1 },   /* refresh down, MY set: flipped by hand */
	};
	uint32_t scale;
	unsigned c;

	for(c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		for(scale = 0; scale < 3; scale++) {
			int torn, off, dir;

			fxcg100_lcd_set_scale(scale);
			panel_reset(1, 82, cases[c].mode, cases[c].up);
			off = run_frames(scale, 120, 77u + scale + c * 3u, NULL);
			CHECK(off > 10, "case %u scale %u: without VSync only %d of "
				"120 frames tore (the check is blind?)", c,
				(unsigned)scale, off);

			fxcg100_lcd_set_sync(1, 1);
			fxcg100_lcd_set_sync_flip(cases[c].flip);
			torn = run_frames(scale, 200, 991u + scale + c * 5u, &dir);
			CHECK(torn == 0, "case %u scale %u: %d of 200 frames tore "
				"with VSync", c, (unsigned)scale, torn);
			CHECK(dir == cases[c].up, "case %u scale %u: strips go %d",
				c, (unsigned)scale, dir);

			/* Taken the wrong way round. */
			fxcg100_lcd_set_sync_flip(!cases[c].flip);
			torn = run_frames(scale, 120, 5u + scale + c * 11u, NULL);
			CHECK(torn > 10, "case %u scale %u: the wrong direction "
				"tore only %d of 120 frames", c, (unsigned)scale, torn);
		}
	}
}

/* The calibration page. Every position that gives a clean page must also
 * be clean in game, the nearest one to where the picture starts must be
 * clean, and most must tear (a page that is clean everywhere tells nothing).
 *
 * On the fx-CG a step is only about 16 rows (a sixteenth of ~250), less than
 * what the page keeps clear at the early end of its window (half a step and
 * a strip), so the position one step below the right one can look clean as
 * well. Both are inside the game's clearance (a sixth of a refresh). */
static int calibrate_frames(uint32_t scale, uint32_t position, int calibrating,
	uint32_t seed)
{
	int torn;

	fxcg100_lcd_set_scale(scale);
	panel_reset(1, 82, 0x80, 1);
	fxcg100_lcd_set_sync(!calibrating, position);
	fxcg100_lcd_sync_calibrate(calibrating ? 1u : 0u);
	torn = run_frames(scale, 160, seed, NULL);
	fxcg100_lcd_sync_calibrate(0);
	return torn;
}

static void test_calibration(void)
{
	static const struct { uint32_t tot, blank, nearest; } panels[] = {
		{ 256, 16, 1 },   /* on a step */
		{ 250, 20, 1 },   /* 1.28 steps of 15.6 rows */
		{ 240, 40, 3 },   /* 2.67 steps of 15 rows */
	};
	uint32_t old_tot = tot, old_blank = blank;
	unsigned p;

	for(p = 0; p < sizeof(panels) / sizeof(panels[0]); p++) {
		uint32_t scale;

		tot = panels[p].tot;
		blank = panels[p].blank;
		for(scale = 0; scale < 3; scale++) {
			uint32_t pos;
			int clean = 0;

			for(pos = 0; pos < 16; pos++) {
				uint32_t seed = 4242u + p * 100u + scale * 17u + pos;
				int page = calibrate_frames(scale, pos, 1, seed);

				if(pos == panels[p].nearest)
					CHECK(page == 0, "panel %u scale %u: the nearest "
						"position %u tore %d of 160 on the page", p,
						(unsigned)scale, (unsigned)pos, page);
				if(page)
					continue;
				clean++;
				page = calibrate_frames(scale, pos, 0, seed + 7u);
				CHECK(page == 0, "panel %u scale %u: position %u is "
					"clean on the page but %d of 160 game frames tore",
					p, (unsigned)scale, (unsigned)pos, page);
			}
			CHECK(clean >= 1 && clean <= 3, "panel %u scale %u: %d of "
				"16 positions look clean", p, (unsigned)scale, clean);
		}
	}
	tot = old_tot;
	blank = old_blank;
}

/* No row counter: one look, then nothing more is read or waited for. */
static void test_no_counter(void)
{
	uint32_t rows, hz;
	uint64_t start, spent;
	int dir;

	fxcg100_lcd_set_scale(0);
	panel_reset(0, 0x1524, 0x00, 0);
	fxcg100_lcd_set_sync(1, 0);
	random_frame(3);
	blit_frame(0, &dir);
	CHECK(fxcg100_lcd_sync_info(&rows, &hz) == -1 && rows == 0,
		"no counter: %u rows", (unsigned)rows);
	start = now;
	blit_frame(0, &dir);
	spent = now - start;
	CHECK(spent < 240u * 160u + 1000u,
		"no counter: a frame took %llu units, more than its pixels",
		(unsigned long long)spent);
	CHECK(dir == 0, "no counter: strips go down");
}

int main(void)
{
	test_pictures();
	test_counter_found();
	test_tearing();
	test_calibration();
	test_no_counter();
	if(failures) {
		printf("fxcg_present_test: %d failures\n", failures);
		return 1;
	}
	printf("fxcg_present_test: strip orders, VSync and calibration passed\n");
	return 0;
}
