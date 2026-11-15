/*
 * fx-CP400 LCD transfer benchmark.
 *
 * Measures how fast pixels reach the panel by CPU writes and by DMA, in the
 * configurations gpSP could use, and how much CPU is left while a DMA
 * transfer runs. It also shows which column offset the panel needs.
 *
 * Results are shown on screen and written to \fls0\LCDBENCH.TXT.
 *
 * All panel access here is done directly (window + 0x2c + data), with the
 * column offset the OS applies: 40 unless ID register 0xda reads 0x16 or
 * 0x52. gint's own dupdate() is used only on the last comparison page.
 */

#include <gint/clock.h>
#include <gint/display.h>
#include <gint/cpu.h>
#include <gint/dma.h>
#include <gint/drivers/touch.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <gint/prof.h>
#include <gint/timer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "classpad_touch_guard.h"

HHK_NAME("LCD Bench")
HHK_DESCRIPTION("LCD transfer benchmark: CPU writes vs DMA")
HHK_AUTHOR("KaraRyougi")
HHK_VERSION("1.2")

#define PANEL_W 320
#define PANEL_H 528
#define FRAME_PIXELS (PANEL_W * PANEL_H)

#define LCD_DATA ((volatile uint16_t *)0xb4000000u)
#define LCD_PRDR ((volatile uint8_t *)0xa405013cu)

/* On-chip X and Y RAM, 8 KiB each: the strip banks gpSP uses. */
#define XRAM ((uint16_t *)0xe5007000u)
#define YRAM ((uint16_t *)0xe5017000u)
/* One strip: 12 full-width rows = 3840 pixels (a multiple of 16). */
#define STRIP_ROWS 12
#define STRIP_PIXELS (PANEL_W * STRIP_ROWS)
#define STRIPS (PANEL_H / STRIP_ROWS)

#define REPS 5

extern int BFile_Open(const char *path, int flags);
extern int BFile_Write(int fd, void const *buf, int size);
extern int BFile_Close(int fd);
extern int BFile_Remove(char const *path);

/* ---- panel ---------------------------------------------------------------- */

static unsigned panel_id_first, panel_id;
static int first_column;

static inline void synco(void)
{
	__asm__ volatile("synco");
}

static void lcd_select(unsigned reg)
{
	*LCD_PRDR &= (uint8_t)~0x10;
	synco();
	*LCD_DATA = (uint16_t)reg;
	synco();
	*LCD_PRDR |= 0x10;
	synco();
}

static inline void lcd_param(unsigned v)
{
	*LCD_DATA = (uint16_t)v;
	synco();
}

static void lcd_identify(void)
{
	lcd_select(0xda);
	panel_id_first = *LCD_DATA;
	panel_id = *LCD_DATA;
	first_column = ((panel_id & 0xff) == 0x16 || (panel_id & 0xff) == 0x52)
		? 0 : 40;
}

static void lcd_window(int c0, int c1, int r0, int r1)
{
	c0 += first_column;
	c1 += first_column;
	lcd_select(0x2a);
	lcd_param((c0 >> 8) & 3);
	lcd_param(c0 & 0xff);
	lcd_param((c1 >> 8) & 3);
	lcd_param(c1 & 0xff);
	lcd_select(0x2b);
	lcd_param((r0 >> 8) & 3);
	lcd_param(r0 & 0xff);
	lcd_param((r1 >> 8) & 3);
	lcd_param(r1 & 0xff);
	lcd_select(0x2c);
}

static void present(void)
{
	volatile uint16_t *lcd = LCD_DATA;
	const uint16_t *p = gint_vram;

	lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
	for(int i = 0; i < FRAME_PIXELS; i++)
		*lcd = p[i];
}

/* DMA reads physical memory: push the cached copy of a range out first. */
static void flush_range(const void *start, unsigned bytes)
{
	uintptr_t p = (uintptr_t)start & ~(uintptr_t)31;
	uintptr_t end = (uintptr_t)start + bytes;

	for(; p < end; p += 32)
		__asm__ volatile("ocbwb @%0" :: "r"(p) : "memory");
	synco();
}

/* ---- test pictures -------------------------------------------------------- */

static void fill_vram(unsigned seed)
{
	for(int y = 0; y < PANEL_H; y++)
		for(int x = 0; x < PANEL_W; x++) {
			unsigned r = ((unsigned)x * 31 / (PANEL_W - 1) + seed * 5) & 31;
			unsigned g = ((unsigned)y * 63 / (PANEL_H - 1) + seed * 9) & 63;
			unsigned b = ((unsigned)(x + y) / 8 + seed * 3) & 31;

			gint_vram[y * PANEL_W + x] = (uint16_t)((r << 11) | (g << 5) | b);
		}
	flush_range(gint_vram, FRAME_PIXELS * 2);
}

static void fill_strip(uint16_t *bank, unsigned seed)
{
	for(int i = 0; i < STRIP_PIXELS; i++) {
		unsigned x = (unsigned)i % PANEL_W, y = (unsigned)i / PANEL_W;

		bank[i] = (uint16_t)((((x / 10 + seed) & 31) << 11) |
			(((y * 5 + seed * 7) & 63) << 5) | ((x / 6 + seed * 3) & 31));
	}
}

/* ---- the transfers -------------------------------------------------------- */

static void cpu_frame(void)
{
	volatile uint16_t *lcd = LCD_DATA;
	const uint16_t *p = gint_vram;

	lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
	for(int i = 0; i < FRAME_PIXELS; i++)
		*lcd = p[i];
}

static void cpu_frame_unrolled(void)
{
	volatile uint16_t *lcd = LCD_DATA;
	const uint16_t *p = gint_vram;

	lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
	for(int i = 0; i < FRAME_PIXELS; i += 8, p += 8) {
		*lcd = p[0]; *lcd = p[1]; *lcd = p[2]; *lcd = p[3];
		*lcd = p[4]; *lcd = p[5]; *lcd = p[6]; *lcd = p[7];
	}
}

/* CPU writes out of on-chip RAM, strip by strip. */
static void cpu_strips_xram(void)
{
	volatile uint16_t *lcd = LCD_DATA;

	for(int s = 0; s < STRIPS; s++) {
		const uint16_t *p = XRAM;

		lcd_window(0, PANEL_W - 1, s * STRIP_ROWS, (s + 1) * STRIP_ROWS - 1);
		for(int i = 0; i < STRIP_PIXELS; i++)
			*lcd = p[i];
	}
}

/* One DMA for the whole frame, out of main RAM. */
static dma_size_t unit_size;
static unsigned unit_bytes;

static void dma_frame_ram(void)
{
	lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
	dma_transfer_sync(0, unit_size, FRAME_PIXELS * 2u / unit_bytes,
		gint_vram, DMA_INC, (void *)LCD_DATA, DMA_FIXED);
}

/* DMA strip by strip out of on-chip RAM, waiting for each. */
static void dma_strips_xram(void)
{
	for(int s = 0; s < STRIPS; s++) {
		lcd_window(0, PANEL_W - 1, s * STRIP_ROWS, (s + 1) * STRIP_ROWS - 1);
		dma_transfer_sync(0, unit_size, STRIP_PIXELS * 2u / unit_bytes,
			XRAM, DMA_INC, (void *)LCD_DATA, DMA_FIXED);
	}
}

/* The gpSP scheme: copy the next strip from main RAM into the free bank
 * while the previous strip is on the bus, then send it. */
static void dma_pipeline(void)
{
	uint16_t *const banks[2] = { XRAM, YRAM };
	int pending = 0;

	for(int s = 0; s < STRIPS; s++) {
		uint16_t *bank = banks[s & 1];

		memcpy(bank, gint_vram + s * STRIP_PIXELS, STRIP_PIXELS * 2);
		if(pending)
			dma_transfer_wait(0);
		lcd_window(0, PANEL_W - 1, s * STRIP_ROWS, (s + 1) * STRIP_ROWS - 1);
		pending = dma_transfer_async(0, DMA_32B, STRIP_PIXELS * 2u / 32u,
			bank, DMA_INC, (void *)LCD_DATA, DMA_FIXED, GINT_CALL_NULL);
		if(!pending) {
			volatile uint16_t *lcd = LCD_DATA;

			for(int i = 0; i < STRIP_PIXELS; i++)
				*lcd = bank[i];
		}
	}
	if(pending)
		dma_transfer_wait(0);
}

static void window_sets(void)
{
	for(int s = 0; s < STRIPS; s++)
		lcd_window(0, PANEL_W - 1, s * STRIP_ROWS, (s + 1) * STRIP_ROWS - 1);
}

/* ---- CPU left over during DMA --------------------------------------------- */

static volatile int flag;
static uint32_t ram_work[32768];   /* 128 KiB: larger than the data cache */

/* Work that stays in registers: counts iterations until `flag`. */
static uint32_t spin_registers(void)
{
	uint32_t n = 0;

	while(!flag)
		n++;
	return n;
}

/* Work that misses the cache on every step: one word per 32-byte line. */
static uint32_t spin_memory(void)
{
	uint32_t n = 0, i = 0, sum = 0;

	while(!flag) {
		sum += ram_work[i];
		ram_work[i] = sum;
		i = (i + 8) & 32767;
		n++;
	}
	return n;
}

/* Iterations per millisecond with nothing else going on. */
static uint32_t spin_rate_idle(uint32_t (*spin)(void))
{
	uint32_t n;
	int t;

	flag = 0;
	t = timer_configure(TIMER_ANY, 50000, GINT_CALL_SET_STOP(&flag));
	if(t < 0)
		return 0;
	timer_start(t);
	n = spin();
	return n / 50;
}

/* Iterations per millisecond while a whole-frame DMA runs; also returns how
 * long that DMA took. */
static uint32_t spin_rate_dma(uint32_t (*spin)(void), uint32_t *dma_us)
{
	uint32_t n = 0, us;

	fill_vram(11);
	lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
	flag = 0;
	us = prof_exec({
		if(dma_transfer_async(0, DMA_32B, FRAME_PIXELS * 2u / 32u, gint_vram,
				DMA_INC, (void *)LCD_DATA, DMA_FIXED, GINT_CALL_SET(&flag)))
			n = spin();
	});
	*dma_us = us;
	return us ? (uint32_t)((uint64_t)n * 1000u / us) : 0;
}

/* Same question for the way gpSP sends a frame: strips out of on-chip RAM,
 * so the DMA itself never reads main memory. */
static uint32_t spin_rate_dma_strips(uint32_t (*spin)(void), uint32_t *dma_us)
{
	uint32_t n = 0, us;

	fill_strip(XRAM, 12);
	us = prof_exec({
		for(int s = 0; s < STRIPS; s++) {
			lcd_window(0, PANEL_W - 1, s * STRIP_ROWS,
				(s + 1) * STRIP_ROWS - 1);
			flag = 0;
			if(dma_transfer_async(0, DMA_32B, STRIP_PIXELS * 2u / 32u, XRAM,
					DMA_INC, (void *)LCD_DATA, DMA_FIXED,
					GINT_CALL_SET(&flag)))
				n += spin();
		}
	});
	*dma_us = us;
	return us ? (uint32_t)((uint64_t)n * 1000u / us) : 0;
}

/* ---- results -------------------------------------------------------------- */

#define MAX_LINES 72
#define LINE_MAX 64
static char lines[MAX_LINES][LINE_MAX];
static int line_count;

static void add_line(const char *fmt, ...)
{
	va_list args;

	if(line_count >= MAX_LINES)
		return;
	va_start(args, fmt);
	vsnprintf(lines[line_count++], LINE_MAX, fmt, args);
	va_end(args);
}

static void status(const char *text)
{
	dclear(C_BLACK);
	dtext(8, 8, C_WHITE, "LCD Bench");
	dtext(8, 28, C_WHITE, text);
	present();
}

/* Time one way of drawing a frame: average and best of REPS. */
static void measure(const char *name, void (*draw)(void), int pixels)
{
	uint32_t total = 0, best = 0xffffffffu;

	for(int i = 0; i < REPS; i++) {
		uint32_t us = prof_exec({ draw(); });

		total += us;
		if(us < best)
			best = us;
	}
	total /= REPS;
	if(pixels)
		add_line("%-22s %6lu us  %3lu ns/px", name, (unsigned long)total,
			(unsigned long)((uint64_t)best * 1000u / (uint32_t)pixels));
	else
		add_line("%-22s %6lu us", name, (unsigned long)total);
}

static void measure_dma_unit(const char *name, dma_size_t size, unsigned bytes,
	void (*draw)(void))
{
	unit_size = size;
	unit_bytes = bytes;
	measure(name, draw, FRAME_PIXELS);
}

static int save_results(void)
{
	static const char path[] = "\\fls0\\LCDBENCH.TXT";
	int fd, ok = 1;

	BFile_Remove(path);
	fd = BFile_Open(path, 0x02 | 0x04);   /* write-only | create */
	if(fd < 0)
		return 0;
	for(int i = 0; i < line_count && ok; i++) {
		int len = (int)strlen(lines[i]);

		ok = BFile_Write(fd, lines[i], len) == len &&
			BFile_Write(fd, "\r\n", 2) == 2;
	}
	return BFile_Close(fd) >= 0 && ok;
}

static void show_results(const char *footer)
{
	dclear(C_BLACK);
	for(int i = 0; i < line_count && i < 40; i++)
		dtext(4, 4 + i * 12, C_WHITE, lines[i]);
	dtext(4, PANEL_H - 16, C_RGB(31, 31, 0), footer);
	present();
}

/* ---- touch probe ----------------------------------------------------------
 * Shows where gint reports a touch, including below the last LCD row where
 * the printed icons (Settings, Menu, Main, Resize, Swap, Rotate, Esc) are.
 * Every release is recorded with the last position seen, in screen pixels
 * and as the raw converter values, and goes into the results file. */
#define MAX_TAPS 28

static void touch_probe(void)
{
	struct _touch_addots dots, last = { 0 };
	struct _touch_adraw raw, last_raw = { 0 };
	char text[64];
	int taps = 0, was_down = 0, redraw = 1;

	add_line("touch probe: x y (raw x y)");
	/* Only EXE ends the probe. It used to end by itself after the last tap,
	 * which sent the program on to save its file while the screen was still
	 * being tapped. */
	for(;;) {
		clearevents();
		if(keydown(KEY_EXE))
			break;

		cpu_atomic_start();
		touch_get_last_scan(&raw, NULL, &dots);
		cpu_atomic_end();

		if(dots.touches >= 1) {
			if(dots.x1 != last.x1 || dots.y1 != last.y1)
				redraw = 1;
			last = dots;
			last_raw = raw;
			was_down = 1;
		} else if(was_down) {
			was_down = 0;
			if(taps < MAX_TAPS) {
				add_line("tap %2d: %4d %4d  (%5u %5u)", taps + 1, last.x1,
					last.y1, (unsigned)last_raw.x1, (unsigned)last_raw.y1);
				taps++;
			}
			redraw = 1;
		}

		if(redraw) {
			redraw = 0;
			dclear(C_BLACK);
			dtext(8, 8, C_WHITE, "Touch probe");
			dtext(8, 28, C_WHITE, "Tap each icon under the screen, left");
			dtext(8, 42, C_WHITE, "to right, then the four screen corners.");
			dtext(8, 62, C_RGB(31, 31, 0), taps < MAX_TAPS ? "EXE when done"
				: "List full. Press EXE");
			snprintf(text, sizeof(text), "now: %s x=%d y=%d",
				was_down ? "down" : "up", last.x1, last.y1);
			dtext(8, 90, C_WHITE, text);
			snprintf(text, sizeof(text), "taps recorded: %d of %d", taps,
				MAX_TAPS);
			dtext(8, 106, C_WHITE, text);
			snprintf(text, sizeof(text), "I2C: stale %lu err %lu stuck %lu sp %lu",
				(unsigned long)cp_touch_guard_stale,
				(unsigned long)cp_touch_guard_errors,
				(unsigned long)cp_touch_guard_stuck,
				(unsigned long)cp_touch_guard_spurious);
			dtext(8, 118, C_RGB(31, 31, 0), text);
			for(int i = 0; i < taps && i < MAX_TAPS; i++)
				dtext(8, 130 + i * 12, C_WHITE,
					lines[line_count - taps + i]);
			if(was_down && last.y1 >= 0 && last.y1 < PANEL_H &&
					last.x1 >= 0 && last.x1 < PANEL_W) {
				dline(last.x1 - 8, last.y1, last.x1 + 8, last.y1,
					C_RGB(0, 31, 0));
				dline(last.x1, last.y1 - 8, last.x1, last.y1 + 8,
					C_RGB(0, 31, 0));
			}
			present();
		}
	}
	while(keydown(KEY_EXE))
		clearevents();
	/* How often the guard had to step in (classpad_touch_guard.h): a stale
	 * arbitration flag at the start of a read, a read ended on a bus error,
	 * a read ended because it stopped making progress. */
	add_line("touch I2C: stale AL %lu, errors %lu, stuck %lu, spurious %lu",
		(unsigned long)cp_touch_guard_stale,
		(unsigned long)cp_touch_guard_errors,
		(unsigned long)cp_touch_guard_stuck,
		(unsigned long)cp_touch_guard_spurious);
}

static int guard_tick(void)
{
	cp_touch_guard_tick();
	return TIMER_CONTINUE;
}

static int touched(void)
{
	struct _touch_addots dots;

	cpu_atomic_start();
	touch_get_last_scan(NULL, NULL, &dots);
	cpu_atomic_end();
	return dots.touches >= 1;
}

/* Wait until the screen has been left alone for about a third of a second.
 * Used before every switch to the OS: the OS reads the touch controller too
 * while it runs, and gint cuts that off when it takes the hardware back. */
static void wait_untouched(void)
{
	int quiet = 0, shown = 0;

	while(quiet < 40) {
		clearevents();
		if(touched()) {
			quiet = 0;
			if(!shown) {
				status("Lift your finger off the screen");
				shown = 1;
			}
		} else {
			quiet++;
		}
		for(volatile int i = 0; i < 20000; i++)
			;
	}
}

static int os_touch_nothing(void)
{
	/* Any short call into the OS will do. */
	int fd = BFile_Open("\\fls0\\LCDBENCH.TXT", 0x01);

	if(fd >= 0)
		BFile_Close(fd);
	return 0;
}

/* Does switching to the OS and back survive a finger on the screen? gpSP
 * does this during play (it reads the game from storage a piece at a time),
 * so it has to. The progress is on screen, so if this hangs the last number
 * shown says where. */
static void switch_while_touched(void)
{
	char text[64];
	int held = 0, i;
	key_event_t ev;

	dclear(C_BLACK);
	dtext(8, 8, C_WHITE, "Touch test 2: OS switch while touched");
	dtext(8, 36, C_WHITE, "Put a finger on the screen and keep it");
	dtext(8, 50, C_WHITE, "there, then press EXE. 20 switches to");
	dtext(8, 64, C_WHITE, "the OS and back follow.");
	dtext(8, 92, C_RGB(31, 31, 0), "If it stops: note the last number,");
	dtext(8, 106, C_RGB(31, 31, 0), "then restart the calculator.");
	dtext(8, 134, C_WHITE, "Any other key skips this test.");
	present();
	ev = getkey();
	if(ev.key != KEY_EXE) {
		add_line("OS switch while touched: skipped");
		return;
	}
	for(i = 0; i < 20; i++) {
		int now = touched();

		held += now;
		snprintf(text, sizeof(text), "switch %d of 20  (%s)", i + 1,
			now ? "touched" : "NOT touched");
		status(text);
		gint_world_switch(GINT_CALL(os_touch_nothing));
	}
	add_line("OS switch while touched: 20 done, %d with a touch", held);
	snprintf(text, sizeof(text), "Passed: 20 switches, %d touched", held);
	status(text);
	for(volatile int k = 0; k < 6000000; k++)
		;
}

/* A picture that only looks right with the correct column offset: a border
 * on the outermost pixels, and marks every 40 columns. */
static void draw_alignment(const char *title)
{
	char text[48];

	dclear(C_BLACK);
	drect_border(0, 0, PANEL_W - 1, PANEL_H - 1, C_NONE, 1, C_WHITE);
	for(int x = 40; x < PANEL_W; x += 40)
		dline(x, 0, x, 12, C_RGB(31, 31, 0));
	dline(0, 0, PANEL_W - 1, PANEL_H - 1, C_RGB(0, 31, 0));
	dline(PANEL_W - 1, 0, 0, PANEL_H - 1, C_RGB(0, 31, 0));
	dtext(12, 40, C_WHITE, title);
	dtext(12, 60, C_WHITE, "A white frame must touch all four");
	dtext(12, 76, C_WHITE, "edges; the green lines must end in");
	dtext(12, 92, C_WHITE, "the corners.");
	snprintf(text, sizeof(text), "ID da = %04x %04x, first column %d",
		panel_id_first, panel_id, first_column);
	dtext(12, 120, C_WHITE, text);
	dtext(12, 150, C_RGB(31, 31, 0), "Press a key");
}

int main(void)
{
	const clock_frequency_t *freq = clock_freq();
	uint32_t idle, busy, dma_us;
	int saved;

	int guard_timer;

	prof_init();
	lcd_identify();
	/* Without this a bus error from the touch controller ends the program
	 * with gint's error 10E0. */
	cp_touch_guard_install();
	guard_timer = timer_configure(TIMER_TMU, 1000000 / 128,
		GINT_CALL(guard_tick));
	if(guard_timer >= 0)
		timer_start(guard_timer);

	add_line("fx-CP400 LCD bench");
	add_line("panel ID da=%04x %04x  first col %d", panel_id_first, panel_id,
		first_column);
	add_line("I %d B %d P %d kHz", freq->Iphi_f / 1000, freq->Bphi_f / 1000,
		freq->Pphi_f / 1000);
	add_line("frame 320x528, avg of %d, ns/px best", REPS);

	draw_alignment("1/2  this program's offset (OS rule)");
	present();
	getkey();

	status("CPU writes...");
	fill_vram(1);
	measure("CPU loop, RAM", cpu_frame, FRAME_PIXELS);
	fill_vram(2);
	measure("CPU loop x8, RAM", cpu_frame_unrolled, FRAME_PIXELS);
	fill_strip(XRAM, 3);
	measure("CPU strips, XRAM", cpu_strips_xram, FRAME_PIXELS);

	fill_vram(4);
	measure_dma_unit("DMA 32B, RAM", DMA_32B, 32, dma_frame_ram);
	fill_vram(5);
	measure_dma_unit("DMA 16B, RAM", DMA_16B, 16, dma_frame_ram);
	fill_vram(6);
	measure_dma_unit("DMA 4B, RAM", DMA_4B, 4, dma_frame_ram);
	fill_vram(7);
	measure_dma_unit("DMA 2B, RAM", DMA_2B, 2, dma_frame_ram);

	fill_strip(XRAM, 8);
	measure_dma_unit("DMA 32B strips, XRAM", DMA_32B, 32, dma_strips_xram);
	fill_strip(XRAM, 9);
	measure_dma_unit("DMA 2B strips, XRAM", DMA_2B, 2, dma_strips_xram);

	fill_vram(10);
	measure("gpSP pipeline (copy+DMA)", dma_pipeline, FRAME_PIXELS);
	measure("44 window sets only", window_sets, 0);

	idle = spin_rate_idle(spin_registers);
	busy = spin_rate_dma(spin_registers, &dma_us);
	add_line("CPU during DMA, registers: %lu%%  (%lu us)",
		idle ? (unsigned long)((uint64_t)busy * 100u / idle) : 0ul,
		(unsigned long)dma_us);
	idle = spin_rate_idle(spin_memory);
	busy = spin_rate_dma(spin_memory, &dma_us);
	add_line("CPU during DMA, RAM misses: %lu%%  (%lu us)",
		idle ? (unsigned long)((uint64_t)busy * 100u / idle) : 0ul,
		(unsigned long)dma_us);

	idle = spin_rate_idle(spin_registers);
	busy = spin_rate_dma_strips(spin_registers, &dma_us);
	add_line("CPU during XRAM strips, reg: %lu%%  (%lu us)",
		idle ? (unsigned long)((uint64_t)busy * 100u / idle) : 0ul,
		(unsigned long)dma_us);
	idle = spin_rate_idle(spin_memory);
	busy = spin_rate_dma_strips(spin_memory, &dma_us);
	add_line("CPU during XRAM strips, RAM: %lu%%  (%lu us)",
		idle ? (unsigned long)((uint64_t)busy * 100u / idle) : 0ul,
		(unsigned long)dma_us);

	touch_probe();

	/* Save what there is before the test that may hang. */
	wait_untouched();
	status("saving LCDBENCH.TXT...");
	gint_world_switch(GINT_CALL(save_results));
	switch_while_touched();

	wait_untouched();
	status("saving LCDBENCH.TXT...");
	saved = gint_world_switch(GINT_CALL(save_results));
	show_results(saved ? "Saved to LCDBENCH.TXT. Press a key"
		: "Could NOT save the file. Press a key");
	getkey();

	/* Same picture through gint, which always assumes the 40-column offset. */
	draw_alignment("2/2  gint dupdate()");
	dupdate();
	getkey();

	show_results("Press a key to exit");
	getkey();
	if(guard_timer >= 0)
		timer_stop(guard_timer);
	prof_quit();
	return 1;
}
