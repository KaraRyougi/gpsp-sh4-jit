#include "fxcg100_platform.h"
#include "../../fxcg100_scale.h"
#include "../../fxcg100_lcd_cleanup.h"
#include "../../fxcg_sync.h"

#include <gint/clock.h>
#include <gint/dma.h>
#include <gint/display.h>
#include <gint/drivers/r61524.h>
#include <gint/keyboard.h>
#include <gint/keycodes.h>
#include <gint/mpu/tmu.h>
#include <gint/rtc.h>
#include <gint/timer.h>

#include <stdio.h>
#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define GBA_W 240
#define GBA_H 160
#define LCD_DATA_REGISTER ((uintptr_t)0xb4000000u)
#define LCD_PRDR          ((volatile uint8_t *)0xa405013cu)
#define LCD_FMARK_MASK    0x20u /* Port R bit 5: R61524 FMARK / VSync */
#define GBA_FRAME_BYTES (GBA_W * GBA_H * (int)sizeof(uint16_t))
#define GBA_FRAME_DMA_BLOCKS (GBA_FRAME_BYTES / 32)

/*
 * The hardware-proven presenter gives each DMA buffer a full 8 KiB on-chip
 * bank. If the experimental resident fastmem image claims XRAM, fall back to
 * two 4 KiB halves of YRAM; that layout costs substantially more DMA launches
 * in the scaled modes and is therefore not the release default.
 */
#if defined(CGBA_SH4_FASTMEM_XYRAM) && CGBA_SH4_FASTMEM_XYRAM
# define STRIP_LINES 8
# define STRIP_43_LINES 4
# define STRIP_FULL_LINES 4
# define STRIP_BANK_BYTES 0x1000
# define STRIP_BUF0 ((uintptr_t)0xe5010000u)
# define STRIP_BUF1 ((uintptr_t)0xe5011000u)
#else
# define STRIP_LINES 16
# define STRIP_43_LINES 12
# define STRIP_FULL_LINES 8
# define STRIP_BANK_BYTES 0x2000
# define STRIP_BUF0 ((uintptr_t)0xe5007000u)
# define STRIP_BUF1 ((uintptr_t)0xe5017000u)
#endif
_Static_assert(STRIP_LINES * GBA_W * 2 <= STRIP_BANK_BYTES,
	"GBA strip must fit its on-chip DMA bank");
_Static_assert(STRIP_LINES % 4 == 0,
	"strip height must stay 4-row aligned for the R61524 DMA window");
_Static_assert(STRIP_43_LINES * 320 * 2 <= STRIP_BANK_BYTES,
	"4:3 strip must fit its on-chip DMA bank");
_Static_assert(STRIP_FULL_LINES * 384 * 2 <= STRIP_BANK_BYTES,
	"fullscreen strip must fit its on-chip DMA bank");

static int lcd_dma_pending;
/* The gameplay blit narrows the R61524 GRAM window to the GBA rectangle via
 * r61524_start_frame(). gint's dclear/dtext/dupdate menu rendering assumes the
 * full 396x224 window, so we must restore it before any gint push or the menu
 * shows a black screen with a stray line. Tracked here, healed at push time. */
static int lcd_window_partial;
/* The OS paints its BFile busy indicator directly into LCD GRAM during a
 * world switch. Centered gameplay does not cover that top-right border, so
 * remember the activity until a full update or gameplay blit removes it. */
static int lcd_os_overlay_dirty;
/* True once a complete gameplay frame has been pushed to the LCD without an
 * intervening menu/status update, scale/filter change, or OS overlay draw. */
static int lcd_frame_synced;

/* ---- PR.5 (FMARK/TE) hardware sync for R61524 & OS 2.10 0x32/0x52 -------- */

enum {
	CG_SYNC_HW_UNKNOWN = 0,
	CG_SYNC_HW_READY,
	CG_SYNC_HW_UNAVAILABLE,
};

enum {
	CG_PANEL_NONE = 0,
	CG_PANEL_R61524,    /* Renesas R61524 (0x1524): R008 + R010 */
	CG_PANEL_HIMAX,     /* OS 2.10 MIPI DCS (0x32/0x52): 0xB2 + 0xDF */
};

static cg_sync_state cg_sync;
static int cg_sync_hw_state;
static int cg_sync_panel;
static int cg_sync_timer = -1;
static volatile uint32_t *cg_sync_tcnt;
static uint32_t cg_sync_pphi_hz;
static uint32_t cg_sync_refresh_mode;
static uint16_t cg_sync_orig_r008;
static uint16_t cg_sync_orig_r010;
static uint32_t cg_sync_orig_bp;
static uint32_t cg_sync_orig_total;
static uint32_t cg_sync_orig_period;
static int cg_sync_regs_saved;
static int cg_sync_regs_dirty;
static uint32_t cg_sync_applied_mhz;

static inline void cg_sync_poll_fmark(void)
{
#if !defined(CGBA_GPSP_HEADLESS_TEST)
	int lvl;

	if(cg_sync_hw_state != CG_SYNC_HW_READY || !cg_sync_tcnt)
		return;
	lvl = (*LCD_PRDR & LCD_FMARK_MASK) != 0u;
	if(lvl && !cg_sync.prev_fmark)
		cg_sync_note_edge(&cg_sync, *cg_sync_tcnt);
	cg_sync.prev_fmark = lvl;
#endif
}

static void wait_lcd_dma(void)
{
	if(!lcd_dma_pending)
		return;

	cg_sync_poll_fmark();
	dma_transfer_wait(0);
	lcd_dma_pending = 0;
	cg_sync_poll_fmark();
}

#if !defined(CGBA_GPSP_HEADLESS_TEST)
static int cg_sync_tmu_cb(void)
{
	return TIMER_CONTINUE;
}

static void cg_sync_release_timer(void)
{
	if(cg_sync_timer >= 0)
		timer_stop(cg_sync_timer);
	cg_sync_timer = -1;
	cg_sync_tcnt = NULL;
}

/* Wait for a rising edge on PR.5 (low -> high) using the active TMU.
 * Returns 1 and writes the edge timestamp on success, 0 on timeout. */
static int cg_sync_wait_rising(uint32_t timeout_ticks, uint32_t *edge_tcnt)
{
	uint32_t start = *cg_sync_tcnt;

	while((*LCD_PRDR & LCD_FMARK_MASK) != 0u) {
		if((uint32_t)(start - *cg_sync_tcnt) >= timeout_ticks)
			return 0;
	}
	while((*LCD_PRDR & LCD_FMARK_MASK) == 0u) {
		if((uint32_t)(start - *cg_sync_tcnt) >= timeout_ticks)
			return 0;
	}
	if(edge_tcnt)
		*edge_tcnt = *cg_sync_tcnt;
	cg_sync.prev_fmark = 1;
	return 1;
}

static inline void cg_dcs_synco(void)
{
	__asm__ volatile("synco");
}

static void cg_dcs_select(uint16_t cmd)
{
	*LCD_PRDR &= (uint8_t)~0x10u;
	cg_dcs_synco();
	*(volatile uint16_t *)LCD_DATA_REGISTER = cmd;
	cg_dcs_synco();
	*LCD_PRDR |= 0x10u;
	cg_dcs_synco();
}

static inline void cg_dcs_param(uint16_t val)
{
	*(volatile uint16_t *)LCD_DATA_REGISTER = val;
	cg_dcs_synco();
}

static uint8_t cg_dcs_read_id(void)
{
	volatile uint16_t *data = (volatile uint16_t *)LCD_DATA_REGISTER;
	uint8_t prev = 0xffu, id = 0u;
	int tries;

	cg_dcs_select(0xdau);
	(void)*data;
	(void)*data;
	for(tries = 0; tries < 6; tries++) {
		cg_dcs_select(0xdau);
		(void)*data;
		id = (uint8_t)(*data & 0xffu);
		if(id == prev)
			break;
		prev = id;
	}
	return id;
}

static void cg_himax_write_timing(uint8_t vfp, uint8_t rtn)
{
	uint8_t b2[6], df[12];
	int i;

	cgba_himax_build_b2(cgba_himax_fxcg100(), vfp, rtn, b2);
	cgba_himax_build_df(cgba_himax_fxcg100(), vfp, rtn, df);
	cg_dcs_select(0xb2u);
	for(i = 0; i < 6; i++)
		cg_dcs_param(b2[i]);
	cg_dcs_select(0xdfu);
	for(i = 0; i < 12; i++)
		cg_dcs_param(df[i]);
}

static void cg_sync_restore_timing(void)
{
	if(!cg_sync_regs_saved)
		return;
	if(cg_sync_applied_mhz != 0u || cg_sync_regs_dirty) {
		wait_lcd_dma();
		if(cg_sync_panel == CG_PANEL_R61524) {
			r61524_set(0x008, cg_sync_orig_r008);
			r61524_set(0x010, cg_sync_orig_r010);
		} else if(cg_sync_panel == CG_PANEL_HIMAX) {
			cg_himax_write_timing(CG_HIMAX_STOCK_VFP, 0xffu);
		}
	}
	cg_sync_applied_mhz = 0u;
	cg_sync_regs_dirty = 0;
	cg_sync.bp_rows = cg_sync_orig_bp;
	cg_sync.total_rows = cg_sync_orig_total;
	if(cg_sync_orig_period)
		cg_sync.period_ticks = cg_sync_orig_period;
	cg_sync.anchor_valid = 0;
}

/* Measure 6 consecutive PR.5 periods into *period_out and return the last
 * edge timestamp in *last_edge. Returns 1 on success, 0 on timeout/invalid. */
static int cg_sync_measure_period(uint32_t timeout_ticks, uint32_t min_per,
	uint32_t max_per, uint32_t *period_out, uint32_t *last_edge)
{
	uint32_t edges[7];
	uint64_t sum = 0;
	int i;

	for(i = 0; i < 7; i++) {
		if(!cg_sync_wait_rising(timeout_ticks, &edges[i]))
			return 0;
	}
	for(i = 0; i < 6; i++) {
		uint32_t dt = (uint32_t)(edges[i] - edges[i + 1]);
		if(dt < min_per || dt > max_per)
			return 0;
		sum += dt;
	}
	*period_out = (uint32_t)((sum + 3u) / 6u);
	*last_edge = edges[6];
	return 1;
}

/* Apply a target refresh rate in millihertz (0 = restore stock timing) by
 * solving the closest safe (VFP, RTN) pair via lcd_sync_common.h and
 * re-measuring PR.5 to confirm the new panel period. */
static void cg_sync_apply_rate(uint32_t want_mhz)
{
	uint32_t new_bp = cg_sync_orig_bp, new_total = cg_sync_orig_total;
	uint32_t target_ticks;
	uint32_t timeout_ticks, min_per, max_per, dummy = 0;
	uint32_t measured_per = 0, measured_edge = 0;

	if(cg_sync_hw_state != CG_SYNC_HW_READY || !cg_sync_regs_saved)
		return;
	if(want_mhz == 0u) {
		cg_sync_restore_timing();
		return;
	}

	/* TMU runs at Pphi/4; 1 frame at want_mhz (mHz) = Pphi * 250 / want_mhz. */
	target_ticks = (uint32_t)(((uint64_t)cg_sync_pphi_hz * 250ull) / want_mhz);
	wait_lcd_dma();
	if(cg_sync_panel == CG_PANEL_R61524) {
		uint16_t new8, new10;

		cg_sync_solve_timing(cg_sync_orig_r008, cg_sync_orig_r010,
			cg_sync_orig_period, target_ticks,
			&new8, &new10, &new_bp, &new_total);
		r61524_set(0x008, new8);
		r61524_set(0x010, new10);
		if(r61524_get(0x008) != new8 || r61524_get(0x010) != new10) {
			cg_sync_restore_timing();
			return;
		}
	} else if(cg_sync_panel == CG_PANEL_HIMAX) {
		uint8_t vfp, rtn;

		cg_sync_solve_himax(cg_sync_orig_period, target_ticks,
			&vfp, &rtn, &new_total);
		new_bp = CG_HIMAX_VBP;
		cg_himax_write_timing(vfp, rtn);
	} else {
		return;
	}

	/* Accept 40..220 Hz on the tuned PR.5 check (covers 59.7..150 Hz). */
	timeout_ticks = cg_sync_pphi_hz / 100u;
	min_per = cg_sync_pphi_hz / 880u;
	max_per = cg_sync_pphi_hz / 160u;

	if(cg_sync_tcnt &&
	   cg_sync_wait_rising(timeout_ticks, &dummy) &&
	   cg_sync_measure_period(timeout_ticks, min_per, max_per,
			&measured_per, &measured_edge)) {
		cg_sync.bp_rows = new_bp;
		cg_sync.total_rows = new_total;
		cg_sync.period_ticks = measured_per;
		cg_sync.anchor_tcnt = measured_edge;
		cg_sync.anchor_valid = 1;
		cg_sync_applied_mhz = want_mhz;
		cg_sync_regs_dirty = 0;
	} else {
		cg_sync_restore_timing();
	}
}

static void cg_sync_probe_hw(void)
{
	uint16_t id1, id2;
	uint32_t pphi, timeout_ticks, min_per, max_per;
	uint32_t orig_period = 0, last_edge = 0;
	int t;

	wait_lcd_dma();
	cg_sync_panel = CG_PANEL_NONE;
	/* Read R61524 device code (0x000) twice; on 2025-2026 MIPI DCS panels
	 * 0x00 is a side-effect-free NOP that does not return 0x1524. */
	id1 = r61524_get(0x000);
	id2 = r61524_get(0x000);
	if(id1 == 0x1524u && id2 == 0x1524u) {
		uint16_t r008 = r61524_get(0x008);
		uint16_t r010 = r61524_get(0x010);

		cg_sync_panel = CG_PANEL_R61524;
		cg_sync_orig_r008 = r008;
		cg_sync_orig_r010 = r010;
		cg_sync_parse_r008(r008, &cg_sync_orig_bp, &cg_sync_orig_total);
	} else {
		/* fx-CG100 OS 2.10 Himax-family panel (ID 0x32 or 0x52 via DCS 0xDA;
		 * OS enables TE on PR.5 via 0x35=0x00 and 0x44=0x0000). */
		uint8_t dcs_id = cg_dcs_read_id();

		if(dcs_id != 0x32u && dcs_id != 0x52u) {
			cg_sync_hw_state = CG_SYNC_HW_UNAVAILABLE;
			return;
		}
		cg_sync_panel = CG_PANEL_HIMAX;
		cg_sync_orig_bp = CG_HIMAX_VBP;
		cg_sync_orig_total = CG_HIMAX_STOCK_TOTAL;
	}
	cg_sync_regs_saved = 1;
	cg_sync_regs_dirty = 0;
	cg_sync_applied_mhz = 0u;
	cg_sync.bp_rows = cg_sync_orig_bp;
	cg_sync.total_rows = cg_sync_orig_total;

	/* Acquire a free-running TMU at Pphi/4 (wraps every ~580 s at 29.5 MHz). */
	cg_sync_release_timer();
	for(t = 2; t >= 0 && cg_sync_timer < 0; t--) {
		cg_sync_timer = timer_configure(t | TIMER_Pphi_4, 0xffffffffu,
			GINT_CALL(cg_sync_tmu_cb));
	}
	if(cg_sync_timer < 0) {
		cg_sync_hw_state = CG_SYNC_HW_UNAVAILABLE;
		return;
	}
	cg_sync_tcnt = &SH7305_TMU.TMU[cg_sync_timer].TCNT;
	timer_start(cg_sync_timer);

	pphi = (uint32_t)clock_freq()->Pphi_f;
	if(!pphi)
		pphi = 29491200u;
	cg_sync_pphi_hz = pphi;
	/* 40 ms per edge timeout; valid stock refresh period: 40..90 Hz. */
	timeout_ticks = pphi / 100u;
	min_per = pphi / 360u;
	max_per = pphi / 160u;

	if(!cg_sync_measure_period(timeout_ticks, min_per, max_per,
			&orig_period, &last_edge)) {
		cg_sync_release_timer();
		cg_sync_hw_state = CG_SYNC_HW_UNAVAILABLE;
		return;
	}

	cg_sync_orig_period = orig_period;
	cg_sync.period_ticks = orig_period;
	cg_sync.anchor_tcnt = last_edge;
	cg_sync.anchor_valid = 1;
	memset(cg_sync.write_ticks, 0, sizeof(cg_sync.write_ticks));
	memset(cg_sync.write_min_ticks, 0, sizeof(cg_sync.write_min_ticks));
	cg_sync_hw_state = CG_SYNC_HW_READY;
}

static uint32_t cg_sync_before_frame(uint32_t mode, uint32_t y0, uint32_t h)
{
	uint32_t pphi, want_mhz, write_min, write_max, safe_lo, safe_len;
	uint32_t wait_start, max_wait;

	want_mhz = cg_sync_target_mhz(cg_sync_refresh_mode, cg_sync.enabled);
	if(!cg_sync.enabled && want_mhz == 0u) {
		if(cg_sync_applied_mhz != 0u)
			cg_sync_restore_timing();
		return 0u;
	}

	pphi = (uint32_t)clock_freq()->Pphi_f;
	if(cg_sync_hw_state == CG_SYNC_HW_READY && pphi && pphi != cg_sync_pphi_hz) {
		cg_sync_restore_timing();
		cg_sync_release_timer();
		cg_sync_hw_state = CG_SYNC_HW_UNKNOWN;
	}
	if(cg_sync_hw_state == CG_SYNC_HW_UNKNOWN)
		cg_sync_probe_hw();
	if(cg_sync_hw_state != CG_SYNC_HW_READY || !cg_sync_tcnt)
		return 0u;

	if(cg_sync_applied_mhz != want_mhz || cg_sync_regs_dirty)
		cg_sync_apply_rate(want_mhz);

	/* If VSync is off (user only selected a custom refresh rate), we are done. */
	if(!cg_sync.enabled)
		return 0u;

	cg_sync_poll_fmark();
	if(cg_sync_needs_resync(&cg_sync, *cg_sync_tcnt)) {
		uint32_t edge = 0;
		if(cg_sync_wait_rising(cg_sync.period_ticks * 2u, &edge))
			cg_sync_note_edge(&cg_sync, edge);
		else
			return *cg_sync_tcnt;
	}

	write_max = (mode < CG_SYNC_MODE_COUNT && cg_sync.write_ticks[mode])
		? cg_sync.write_ticks[mode]
		: cg_sync_default_write_ticks(cg_sync.period_ticks, mode);
	write_min = (mode < CG_SYNC_MODE_COUNT && cg_sync.write_min_ticks[mode])
		? cg_sync.write_min_ticks[mode]
		: write_max;

	if(cg_sync_safe_window_range(cg_sync.period_ticks, cg_sync.total_rows,
			cg_sync.bp_rows, y0, h, write_min, write_max,
			&safe_lo, &safe_len)) {
		wait_start = *cg_sync_tcnt;
		max_wait = cg_sync.period_ticks + cg_sync.period_ticks / 2u;
		for(;;) {
			uint32_t now, phase;

			cg_sync_poll_fmark();
			now = *cg_sync_tcnt;
			phase = cg_sync_phase_from_tcnt(cg_sync.anchor_tcnt,
				now, cg_sync.period_ticks);
			if(cg_sync_wait_needed(phase, cg_sync.period_ticks,
					safe_lo, safe_len) == 0u)
				break;
			if((uint32_t)(wait_start - now) >= max_wait)
				break;
		}
	}
	return *cg_sync_tcnt;
}

static void cg_sync_after_frame(uint32_t mode, uint32_t start_tcnt,
	uint32_t total_strips)
{
	uint32_t span, est;

	if(!cg_sync.enabled || cg_sync_hw_state != CG_SYNC_HW_READY ||
	   !cg_sync_tcnt || total_strips < 2u)
		return;
	cg_sync_poll_fmark();
	/* Called right before launching the final strip's async DMA, so
	 * (total_strips - 1) strips have finished; scale up by the last strip. */
	span = (uint32_t)(start_tcnt - *cg_sync_tcnt);
	est = cgba_sync_scale_strips(span, total_strips);
	cg_sync_update_write_ticks(&cg_sync, mode, est);
}
#else
static void cg_sync_restore_timing(void) {}
static void cg_sync_release_timer(void) {}
static uint32_t cg_sync_before_frame(uint32_t mode, uint32_t y0, uint32_t h)
{ (void)mode; (void)y0; (void)h; return 0u; }
static void cg_sync_after_frame(uint32_t mode, uint32_t start_tcnt,
	uint32_t total_strips)
{ (void)mode; (void)start_tcnt; (void)total_strips; }
#endif

void fxcg100_lcd_set_sync(uint32_t wait, uint32_t offset)
{
	(void)offset;
	cg_sync.enabled = wait ? 1u : 0u;
	if(!cg_sync.enabled) {
		if(cg_sync_target_mhz(cg_sync_refresh_mode, 0u) == 0u)
			cg_sync_restore_timing();
		cg_sync.anchor_valid = 0;
	}
}

void fxcg100_lcd_set_refresh(uint32_t rate)
{
	cg_sync_refresh_mode = (rate <= 6u) ? rate : 0u;
	if(cg_sync_target_mhz(cg_sync_refresh_mode, cg_sync.enabled) == 0u)
		cg_sync_restore_timing();
}

/* Hand the panel back to gint: finish any direct blit DMA and restore the full
 * display window. No-op unless a gameplay blit narrowed the window. */
static void restore_full_window(void)
{
	if(!lcd_window_partial)
		return;

	wait_lcd_dma();
	r61524_win_set(0, DWIDTH - 1, 0, DHEIGHT - 1);
	lcd_window_partial = 0;
}

#ifndef CGBA_FXCG50
static int keycode_from_cg100_skin_code(int basic_keycode)
{
	static const int cg100_rows_9_to_5[][6] = {
		{ KEY_PAGEUP, KEY_NEXTTAB, KEY_UP, KEY_PREVTAB, KEY_HOME, KEY_ON },
		{ KEY_PAGEDOWN, KEY_RIGHT, KEY_OK, KEY_LEFT, KEY_BACK, KEY_SETTINGS },
		{ KEY_TOOLS, KEY_CATALOG, KEY_DOWN, KEY_VARS, KEY_ALPHA, KEY_SHIFT },
		{ KEY_EXPFUN, KEY_SQUARE, KEY_POWER, KEY_SQRT, KEY_FRAC, KEY_XOT },
		{ KEY_RIGHTP, KEY_LEFTP, KEY_TAN, KEY_COS, KEY_SIN, KEY_COMMA },
	};
	int row = basic_keycode % 10;
	int col = basic_keycode / 10 - 1;

	if(row < 0 || row > 9 || col < 1 || col > 6)
		return 0;

	/*
	 * Keymaps are stored as CG100 skin positions (eg. 49 = Up,
	 * 48 = OK, 79 = ON). gint's keyboard API expects logical keycodes on
	 * Graph Math+, not raw fx-CG20-style matrix positions.
	 */
	if(row >= 5)
		return cg100_rows_9_to_5[9 - row][col - 1];
	if(basic_keycode == 34)
		return KEY_ACON;
	if(basic_keycode == 41)
		return KEY_FORMAT;

	return (row << 4) + (7 - col);
}

#endif /* !CGBA_FXCG50 */

int fxcg100_key_down(int basic_keycode)
{
#ifdef CGBA_FXCG50
	/* fx-CG50: keymaps hold gint KEY_* codes directly (the classic matrix). */
	return basic_keycode ? (keydown(basic_keycode) ? 1 : 0) : 0;
#else
	int keycode = keycode_from_cg100_skin_code(basic_keycode);

	return keycode ? keydown(keycode) : 0;
#endif
}

uint32_t fxcg100_poll_app_keys(void)
{
	uint32_t keys = 0;

	clearevents();

#ifdef CGBA_FXCG50
	/* fx-CG50 shell navigation. Only APPKEY_ON is read during gameplay (to
	 * open the emulator menu), so it must be a non-GBA key -> AC/ON. The rest
	 * are polled only inside the menu, where GBA input is paused, so they can
	 * reuse the arrows / EXE / EXIT. */
	if(keydown(KEY_SHIFT))
		keys |= FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_SORT;
	if(keydown(KEY_MENU))
		keys |= FXCG100_APPKEY_HOME;
	if(keydown(KEY_ACON))
		keys |= FXCG100_APPKEY_AC | FXCG100_APPKEY_ON;
	if(keydown(KEY_EXE))
		keys |= FXCG100_APPKEY_EXE;
	if(keydown(KEY_OPTN))
		keys |= FXCG100_APPKEY_MENU;
	if(keydown(KEY_UP))
		keys |= FXCG100_APPKEY_UP;
	if(keydown(KEY_DOWN))
		keys |= FXCG100_APPKEY_DOWN;
	if(keydown(KEY_LEFT))
		keys |= FXCG100_APPKEY_LEFT;
	if(keydown(KEY_RIGHT))
		keys |= FXCG100_APPKEY_RIGHT;
	if(keydown(KEY_EXIT))
		keys |= FXCG100_APPKEY_BACK;
	if(keydown(KEY_OPTN))
		keys |= FXCG100_APPKEY_SETTINGS;
	if(keydown(KEY_DEL))
		keys |= FXCG100_APPKEY_DEL;
	{
		static const int digits[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
			KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };

		for(int i = 0; i < 10; i++)
			if(keydown(digits[i]))
				keys |= FXCG100_APPKEY_DIGIT(i);
	}
#else
	if(fxcg100_key_down(77))
		keys |= FXCG100_APPKEY_SHIFT;
	if(fxcg100_key_down(69))
		keys |= FXCG100_APPKEY_HOME;
	if(fxcg100_key_down(34))
		keys |= FXCG100_APPKEY_AC;
	if(fxcg100_key_down(31) || fxcg100_key_down(48))
		keys |= FXCG100_APPKEY_EXE;
	if(fxcg100_key_down(27))
		keys |= FXCG100_APPKEY_MENU;
	if(fxcg100_key_down(49))
		keys |= FXCG100_APPKEY_UP;
	if(fxcg100_key_down(47))
		keys |= FXCG100_APPKEY_DOWN;
	if(fxcg100_key_down(58))
		keys |= FXCG100_APPKEY_LEFT;
	if(fxcg100_key_down(38))
		keys |= FXCG100_APPKEY_RIGHT;
	if(fxcg100_key_down(68))
		keys |= FXCG100_APPKEY_BACK;
	if(fxcg100_key_down(FXCG100_PHYSKEY_ON))
		keys |= FXCG100_APPKEY_ON;
	if(fxcg100_key_down(78))
		keys |= FXCG100_APPKEY_SETTINGS;
	if(fxcg100_key_down(44))
		keys |= FXCG100_APPKEY_DEL;
	if(fxcg100_key_down(37))
		keys |= FXCG100_APPKEY_SORT;
	if(fxcg100_key_down(29))
		keys |= FXCG100_APPKEY_PGUP;
	if(fxcg100_key_down(28))
		keys |= FXCG100_APPKEY_PGDN;
	{
		/* Skin positions of 0..9 (see fxcg100_keys.c). */
		static const uint8_t digits[10] = { 71, 72, 62, 52, 73, 63, 53, 74,
			64, 54 };

		for(int i = 0; i < 10; i++)
			if(fxcg100_key_down(digits[i]))
				keys |= FXCG100_APPKEY_DIGIT(i);
	}
#endif

	return keys;
}

/* 128 Hz ticks for timing (savestate timings): the RTC. */
uint32_t fxcg100_ticks(void)
{
	return rtc_ticks();
}

void fxcg100_wait_keys_released(void)
{
#if !defined(CGBA_GPSP_HEADLESS_TEST) && !defined(CGBA_HOST_TEST)
	volatile uint16_t *keysc = (volatile uint16_t *)0xa44b0000u;
	uint32_t start = rtc_ticks();
	uint32_t last_tick = start;
	unsigned clear_ticks = 0;
	unsigned clear_polls = 0;

	/* Wait until both gint's key state and the raw SH7305 KEYSC matrix
	 * report no key held across two 128 Hz RTC ticks (~16 ms debounce),
	 * bounded so a stuck matrix or stopped RTC can never deadlock. On
	 * fx-CG100, entering a Fugue world switch while a physical key is
	 * still down can leave the OS call or return stuck. */
	for(unsigned poll = 0; poll < 60000u; poll++) {
		uint32_t now = rtc_ticks();
		uint32_t app = fxcg100_poll_app_keys();
		uint16_t raw = (uint16_t)(keysc[0] | keysc[1] | keysc[2] |
			keysc[3] | keysc[4] | keysc[5]);

		if(app == 0 && raw == 0) {
			clear_polls++;
			if(now != last_tick) {
				last_tick = now;
				if(++clear_ticks >= 2u)
					break;
			} else if(clear_polls >= 64u) {
				break;
			}
		} else {
			clear_ticks = 0;
			clear_polls = 0;
			last_tick = now;
		}
		if((uint32_t)(now - start) >= 384u)
			break;
		for(volatile unsigned i = 0; i < 4000; i++)
			;
	}
	clearevents();
#endif
}

void fxcg100_lcd_init(void)
{
	lcd_os_overlay_dirty = 0;
	lcd_frame_synced = 0;
	cg_sync_hw_state = CG_SYNC_HW_UNKNOWN;
	cg_sync_panel = CG_PANEL_NONE;
	cg_sync_regs_saved = 0;
	cg_sync_regs_dirty = 0;
	cg_sync_applied_mhz = 0u;
	cg_sync.anchor_valid = 0;
}

void fxcg100_lcd_shutdown(void)
{
	restore_full_window();
	wait_lcd_dma();
	cg_sync_restore_timing();
	cg_sync_release_timer();
	cg_sync_hw_state = CG_SYNC_HW_UNKNOWN;
	cg_sync_panel = CG_PANEL_NONE;
	cg_sync_regs_saved = 0;
	cg_sync.anchor_valid = 0;
	lcd_frame_synced = 0;
}

void fxcg100_lcd_clear(uint16_t color)
{
	wait_lcd_dma();
	dclear((int)color);
	lcd_frame_synced = 0;
}

void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
	uint16_t color)
{
	if(x >= LCD_W || y >= LCD_H || w == 0 || h == 0)
		return;

	wait_lcd_dma();
	if(x + w > LCD_W)
		w = LCD_W - x;
	if(y + h > LCD_H)
		h = LCD_H - y;

	drect((int)x, (int)y, (int)(x + w - 1), (int)(y + h - 1),
		(int)color);
}

void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
	uint16_t fg, uint16_t bg)
{
	wait_lcd_dma();
	dtext_opt((int)x, (int)y, (int)fg, (int)bg,
		DTEXT_LEFT, DTEXT_TOP, text ? text : "");
}

void fxcg100_lcd_update(void)
{
	restore_full_window();
	wait_lcd_dma();
	dupdate();
	lcd_os_overlay_dirty = 0;
	lcd_frame_synced = 0;
}

void fxcg100_lcd_update_rows(unsigned y, unsigned h)
{
	unsigned y1 = y + h;

	if(y >= DHEIGHT || h == 0)
		return;
	if(y1 > DHEIGHT)
		y1 = DHEIGHT;
	/* r61524_display() sends whole rows by DMA in groups of four. */
	y &= ~3u;
	y1 = (y1 + 3u) & ~3u;
	if(y1 > DHEIGHT)
		y1 = DHEIGHT;
	restore_full_window();
	wait_lcd_dma();
	r61524_display(gint_vram, (int)y, (int)(y1 - y), R61524_DMA_WAIT);
	lcd_frame_synced = 0;
}

void fxcg100_lcd_status(const char *text)
{
	uint16_t bg = C_RGB(0, 10, 22);
	uint16_t fg = C_WHITE;

	restore_full_window();
	wait_lcd_dma();
	drect(0, 0, DWIDTH - 1, 13, bg);
	dtext_opt(4, 2, fg, bg, DTEXT_LEFT, DTEXT_TOP, text ? text : "");
	dupdate();
	lcd_os_overlay_dirty = 0;
	lcd_frame_synced = 0;
}

void fxcg100_lcd_note_os_activity(void)
{
	lcd_os_overlay_dirty = 1;
	lcd_frame_synced = 0;
	cg_sync_regs_dirty = 1;
	cg_sync.anchor_valid = 0;
}

/* Start the DMA of one prepared YRAM strip to the LCD window. Falls back to CPU
 * writes if the DMA channel is unavailable. */
static void start_strip_dma(const uint16_t *strip, int x, int y,
	int width, int rows)
{
	unsigned blocks = (unsigned)(width * rows) * sizeof(uint16_t) / 32u;

	r61524_start_frame(x, x + width - 1, y, y + rows - 1);
	if(dma_transfer_async(0, DMA_32B, blocks, (void *)strip, DMA_INC,
			(void *)LCD_DATA_REGISTER, DMA_FIXED, GINT_CALL_NULL)) {
		lcd_dma_pending = 1;
		return;
	}

	volatile uint16_t *display = (volatile uint16_t *)LCD_DATA_REGISTER;
	for(int i = 0; i < width * rows; i++)
		display[0] = strip[i];
}

/* ---- Scaled presentation --------------------------------------------------
 * Geometry (menu DISPLAY SCALING, fxcg100_lcd_set_scale):
 *   0  1:1 centered 240x160 (below)
 *   1  4:3: 320x212, exact 3->4 groups both axes
 *   2  fullscreen: 384x216, 5->8 horizontal taps, 27/20 vertical map
 * Filter (menu SCALE FILTER, fxcg100_lcd_set_filter): SMOOTH / SHARP / CRISP
 * selects the horizontal row scaler and the vertical tap composition (see
 * fxcg100_scale.h). Scaled rows are composed directly into the XY-RAM strip
 * banks while the previous strip's DMA is in flight, so the filter cost
 * hides under the LCD transfer. Strip heights stay multiples of 4 (R61524
 * DMA window). */

static uint32_t lcd_scale_mode;
static uint32_t lcd_scale_filter;   /* CGBA_SCALE_FILTER_{SMOOTH,SHARP,CRISP} */

/* Erase only the top-right border that can retain the OS BFile icon. In
 * fullscreen the next frame covers the icon itself, so that mode has no extra
 * LCD traffic. The 32-row bound comfortably covers the indicator while keeping
 * the one-shot cleanup far smaller than a full-screen push. */
static void clear_os_overlay_before_blit(void)
{
	int viewport_right;
	int rows = (int)fxcg100_lcd_cleanup_rows(DHEIGHT);
	volatile uint16_t *display = (volatile uint16_t *)LCD_DATA_REGISTER;

	if(!lcd_os_overlay_dirty)
		return;
	lcd_os_overlay_dirty = 0;
	viewport_right = (int)fxcg100_lcd_cleanup_right_start(lcd_scale_mode,
		DWIDTH);
	if(viewport_right >= DWIDTH)
		return;

	wait_lcd_dma();
	r61524_start_frame(viewport_right, DWIDTH - 1, 0, rows - 1);
	for(int i = 0; i < (DWIDTH - viewport_right) * rows; i++)
		display[0] = 0;
	lcd_window_partial = 1;
}

void fxcg100_lcd_set_scale(uint32_t mode)
{
	lcd_scale_mode = (mode <= 2) ? mode : 0;
	lcd_frame_synced = 0;
}

void fxcg100_lcd_set_filter(uint32_t filter)
{
	lcd_scale_filter = (filter <= CGBA_SCALE_FILTER_CRISP) ? filter : 0;
	lcd_frame_synced = 0;
}

int fxcg100_lcd_is_synced(void)
{
	return lcd_frame_synced && !lcd_os_overlay_dirty;
}

typedef void (*cgba_hscale_fn)(const uint16_t *, uint16_t *);

static cgba_hscale_fn hscale_320_for(uint32_t f)
{
	if (f == CGBA_SCALE_FILTER_CRISP) return cgba_scale_row_240_320_crisp;
	if (f == CGBA_SCALE_FILTER_SHARP) return cgba_scale_row_240_320_sharp;
	return cgba_scale_row_240_320;
}

static cgba_hscale_fn hscale_384_for(uint32_t f)
{
	if (f == CGBA_SCALE_FILTER_CRISP) return cgba_scale_row_240_384_crisp;
	if (f == CGBA_SCALE_FILTER_SHARP) return cgba_scale_row_240_384_sharp;
	return cgba_scale_row_240_384;
}

/* One 320px scratch row (4:3 middle row) + two cached hscaled rows with
 * their source indices (fullscreen). Regular RAM: reads/writes stay cached. */
static uint16_t scale_scratch[LCD_W] __attribute__((aligned(4)));
static uint16_t scale_hrow[2][LCD_W] __attribute__((aligned(4)));
static int scale_hrow_idx[2];
static cgba_hscale_fn scale_hrow_fn;   /* selected fullscreen hscaler */

/* 4:3: 53 groups of 3 source rows -> 4 output rows. Horizontal filter via
 * the selected 240->320 scaler; vertical taps {A, .75, 1.5, 2.25} composed
 * per filter (SMOOTH blends r1+r2, SHARP only r2, CRISP neither). 320px
 * wide, centered (ox=32), top-aligned; the window stays 4-row aligned. */
static void blit_gba_scaled_43(const uint16_t *pixels)
{
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	cgba_hscale_fn hs = hscale_320_for(lcd_scale_filter);
	uint32_t filter = lcd_scale_filter;
	int ox = (DWIDTH - 320) / 2;
	int oy = ((DHEIGHT - 212) / 2) & ~3;    /* 4-aligned DMA window */
	int group = 0, y = 0, bi = 0;
	uint32_t strips_sent = 0, sync_t0;

	wait_lcd_dma();
	sync_t0 = cg_sync_before_frame(1u, (uint32_t)oy, 212u);
	while (y < 212) {
		int rows = (212 - y < STRIP_43_LINES) ? 212 - y : STRIP_43_LINES;
		uint16_t *sp = strips[bi];
		int g;

		for (g = 0; g < rows / 4; g++, group++) {
			const uint16_t *A = pixels + (group * 3 + 0) * GBA_W;
			const uint16_t *B = pixels + (group * 3 + 1) * GBA_W;
			const uint16_t *C = pixels + (group * 3 + 2) * GBA_W;
			uint16_t *r0 = sp + (g * 4 + 0) * 320;
			uint16_t *r1 = sp + (g * 4 + 1) * 320;
			uint16_t *r2 = sp + (g * 4 + 2) * 320;
			uint16_t *r3 = sp + (g * 4 + 3) * 320;

			hs(A, r0);
			hs(C, r3);
			if (filter == CGBA_SCALE_FILTER_SMOOTH) {
				hs(B, scale_scratch);
				cgba_scale_row_avg(r0, scale_scratch, r1, 320);
				cgba_scale_row_avg(scale_scratch, r3, r2, 320);
			} else if (filter == CGBA_SCALE_FILTER_SHARP) {
				hs(B, r1);
				cgba_scale_row_avg(r1, r3, r2, 320);
			} else {                               /* CRISP: A,B,C,C */
				hs(B, r1);
				memcpy(r2, r3, 320 * sizeof(uint16_t));
			}
		}
		if (y > 0)
			wait_lcd_dma();
		strips_sent++;
		if (y + rows >= 212)
			cg_sync_after_frame(1u, sync_t0, strips_sent);
		start_strip_dma(strips[bi], ox, oy + y, 320, rows);
		bi ^= 1;
		y += rows;
	}
	lcd_window_partial = 1;
	lcd_frame_synced = 1;
}

/* Fullscreen: hscaled source rows are cached in two slots keyed by
 * (source row & 1), so a blend pair (s, s+1) never collides. */
static const uint16_t *fs_hrow(const uint16_t *pixels, unsigned s)
{
	unsigned slot = s & 1;

	if (scale_hrow_idx[slot] != (int)s) {
		scale_hrow_fn(pixels + s * GBA_W, scale_hrow[slot]);
		scale_hrow_idx[slot] = (int)s;
	}
	return scale_hrow[slot];
}

static void blit_gba_scaled_full(const uint16_t *pixels)
{
	static uint16_t vmap[216];
	static int vmap_filter = -1;
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	int ox = (DWIDTH - LCD_W) / 2;
	int oy = ((DHEIGHT - LCD_H) / 2) & ~3;  /* 4-aligned DMA window */
	int y = 0, bi = 0;
	uint32_t strips_sent = 0, sync_t0;

	if (vmap_filter != (int)lcd_scale_filter) {
		cgba_scale_build_vmap216_f(vmap, (int)lcd_scale_filter);
		vmap_filter = (int)lcd_scale_filter;
	}
	scale_hrow_fn = hscale_384_for(lcd_scale_filter);
	scale_hrow_idx[0] = scale_hrow_idx[1] = -1;   /* new frame contents */

	wait_lcd_dma();
	sync_t0 = cg_sync_before_frame(2u, (uint32_t)oy, LCD_H);
	while (y < LCD_H) {
		uint16_t *sp = strips[bi];
		int rows = (LCD_H - y < STRIP_FULL_LINES)
			? LCD_H - y : STRIP_FULL_LINES;
		int r;

		for (r = 0; r < rows; r++) {
			uint16_t v = vmap[y + r];
			unsigned src = v & CGBA_SCALE_VROW;
			uint16_t *out = sp + r * LCD_W;

			if (v & CGBA_SCALE_VBLEND)
				cgba_scale_row_avg(fs_hrow(pixels, src),
					fs_hrow(pixels, src + 1), out, LCD_W);
			else
				memcpy(out, fs_hrow(pixels, src),
					LCD_W * sizeof(uint16_t));
		}
		if (y > 0)
			wait_lcd_dma();
		strips_sent++;
		if (y + rows >= LCD_H)
			cg_sync_after_frame(2u, sync_t0, strips_sent);
		start_strip_dma(strips[bi], ox, oy + y, LCD_W, rows);
		bi ^= 1;
		y += rows;
	}
	lcd_window_partial = 1;
	lcd_frame_synced = 1;
}

extern void cgba_copy_32b_sh4(void *dst, const void *src, uint32_t nbytes);

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
	int ox = (DWIDTH - GBA_W) / 2;   /* 78 */
	int oy = (DHEIGHT - GBA_H) / 2;  /* 32 (4-aligned) */
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	int y, bi;
	int rows;

	if(!pixels)
		return;
	clear_os_overlay_before_blit();
	if(lcd_scale_mode == 1) {
		blit_gba_scaled_43(pixels);
		return;
	}
	if(lcd_scale_mode == 2) {
		blit_gba_scaled_full(pixels);
		return;
	}

	/*
	 * Present the frame as strips DMA'd out of the on-chip banks selected above.
	 * Double-buffered: prepare the next strip while the current DMA runs.
	 */
	wait_lcd_dma();
	{
		uint32_t sync_t0 = cg_sync_before_frame(0u, (uint32_t)oy, GBA_H);
		uint32_t strips_sent = 1u;

		y = 0;
		rows = STRIP_LINES;
		cgba_copy_32b_sh4(strips[0], &pixels[y * GBA_W],
			(uint32_t)(rows * GBA_W * sizeof(uint16_t)));
		start_strip_dma(strips[0], ox, oy + y, GBA_W, rows);

		bi = 1;
		y += rows;
		while(y < GBA_H) {
			rows = (y + STRIP_LINES > GBA_H) ? (GBA_H - y) : STRIP_LINES;
			cgba_copy_32b_sh4(strips[bi], &pixels[y * GBA_W],
				(uint32_t)(rows * GBA_W * sizeof(uint16_t)));
			wait_lcd_dma();   /* wait for the previous strip's DMA */
			strips_sent++;
			if(y + rows >= GBA_H)
				cg_sync_after_frame(0u, sync_t0, strips_sent);
			start_strip_dma(strips[bi], ox, oy + y, GBA_W, rows);
			bi ^= 1;
			y += rows;
		}
	}

	/*
	 * Do NOT wait for the final strip's DMA here. The whole frame is already
	 * copied into the on-chip strips, so we leave the last DMA in flight and
	 * return -- the next frame's run_frame() then overlaps it (the prizoop
	 * trick). Every consumer that could collide already waits first: the next
	 * blit's leading wait_lcd_dma() before it reuses a strip bank, and the
	 * menu/status path's restore_full_window() before any gint push. The frame
	 * buffer itself is free to be overwritten (the DMA reads on-chip RAM).
	 */
	lcd_window_partial = 1;   /* window narrowed; menu restores it */
	lcd_frame_synced = 1;
}

/* ---- Scanline-streamed unscaled presentation ---------------------------
 * update_scanline() calls us only after a row is architecturally complete.
 * A 16-row group is therefore byte-for-byte the same data that the normal
 * end-of-frame blit copies, but its LCD DMA overlaps emulation of the next
 * group. The final DMA remains in flight under the same ownership contract as
 * fxcg100_lcd_blit_gba(). */
#ifdef CGBA_LCD_SCANLINE_STREAM
static int lcd_stream_active;
static unsigned lcd_stream_next_y;
static unsigned lcd_stream_bank;
static int lcd_stream_strip_dirty;

int fxcg100_lcd_stream_begin(void)
{
	if(lcd_scale_mode != 0 || cg_sync.enabled)
		return 0;
	wait_lcd_dma();
	clear_os_overlay_before_blit();
	lcd_stream_next_y = 0;
	lcd_stream_bank = 0;
	lcd_stream_strip_dirty = !lcd_frame_synced;
	lcd_stream_active = 1;
	return 1;
}

void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y, int dirty)
{
	uint16_t *const strips[2] = {
		(uint16_t *)STRIP_BUF0,
		(uint16_t *)STRIP_BUF1,
	};
	unsigned rows;
	int ox = (DWIDTH - GBA_W) / 2;
	int oy = (DHEIGHT - GBA_H) / 2;

	if(!lcd_stream_active || !row || y >= GBA_H)
		return;
	if(dirty)
		lcd_stream_strip_dirty = 1;
	if(y + 1 < lcd_stream_next_y + STRIP_LINES && y + 1 != GBA_H)
		return;
	if(y + 1 > lcd_stream_next_y + STRIP_LINES && y + 1 != GBA_H) {
		lcd_stream_active = 0;       /* out-of-order/missing row: fall back */
		return;
	}

	rows = y + 1 - lcd_stream_next_y;
	if(rows == 0 || rows > STRIP_LINES) {
		lcd_stream_active = 0;
		return;
	}
	if(lcd_stream_strip_dirty) {
		memcpy(strips[lcd_stream_bank], row - (rows - 1) * GBA_W,
			(size_t)rows * GBA_W * sizeof(uint16_t));
		wait_lcd_dma();
		start_strip_dma(strips[lcd_stream_bank], ox,
			oy + (int)lcd_stream_next_y, GBA_W, (int)rows);
		lcd_stream_bank ^= 1;
		lcd_window_partial = 1;
	}
	lcd_stream_next_y += rows;
	lcd_stream_strip_dirty = !lcd_frame_synced;
}

int fxcg100_lcd_stream_end(void)
{
	int complete = lcd_stream_active && lcd_stream_next_y == GBA_H;
	lcd_stream_active = 0;
	if(complete)
		lcd_frame_synced = 1;
	return complete;
}
#else
int fxcg100_lcd_stream_begin(void) { return 0; }
void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y, int dirty)
{ (void)row; (void)y; (void)dirty; }
int fxcg100_lcd_stream_end(void) { return 0; }
#endif
