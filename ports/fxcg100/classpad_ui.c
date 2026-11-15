/*
 * fx-CP400 interface: the game library, the pause menu, settings, the
 * key-mapping page and the clock page. This replaces fxcg100_menu.c on the
 * ClassPad and keeps its contract (fxcg100_menu_init / fxcg100_menu_run /
 * ..._should_blit), so main() is shared with the fx-CG models.
 *
 * It is built for a touchscreen: every control is at least 40 px tall with a
 * larger touch area where there is room, lists scroll by dragging (and keep
 * moving when flicked), and nothing needs a key. The keypad still works
 * throughout - cursor keys move a focus ring and change values, EXE
 * activates, backspace goes back - because touch can be lost (for instance
 * with an overclock the touch controller does not follow).
 *
 * The look follows Material 3: tonal surfaces from one colour scheme
 * (classpad_theme.h), cards, filled / tonal / outlined buttons, segmented
 * buttons, switches, dialogs over a scrim, a snackbar.
 *
 * Screens are drawn straight into gint's VRAM in panel order for the current
 * orientation (classpad_gfx.h), so the same code lays out 320x528 upright
 * and 528x320 turned, and presenting is a plain full-frame push.
 *
 * The interface is immediate-mode: each step reads the input once, redraws
 * the current screen, and the widget calls report what was tapped. Nothing
 * is redrawn while there is no input and nothing is moving. cp_ui_step() is
 * separate from the blocking fxcg100_menu_run() loop so the host test can
 * drive it.
 *
 * Motion is timed by a 128 Hz tick count, not by frames, so it takes the
 * same time whatever a frame costs. Most of it is free: a screen fades by being
 * drawn with a colour scheme part-way to the background, and slides by an
 * offset on the surface, so no pixel is blended that would not be drawn
 * anyway.
 */

#include "fxcg100_platform.h"
#include "classpad_layout.h"
#include "classpad_clock.h"
#include "classpad_gfx.h"
#include "classpad_theme.h"

#include <gint/display.h>

#include <stdio.h>
#include <string.h>

#define MARGIN 12              /* screen edge to a card */
#define PAD 16                 /* card edge to its content */
#define CARD_RADIUS 20
#define CONTROL_H 40
#define DRAG_SLOP 10

#define TICKS_PER_SECOND 128u
#define T_ENTER 26             /* a screen coming in: about 200 ms */
#define T_DIALOG 20
#define T_RIPPLE 28
#define T_RIPPLE_OUT 20
#define T_THEME 32
#define T_SNACK_IN 16
#define T_SNACK_OUT 12
#define T_SNACK (5u * TICKS_PER_SECOND / 2u)
#define SCRIM 150              /* of 256: how far a dialog darkens the page */

enum {
	SCREEN_LIBRARY = 0,
	SCREEN_PAUSE,
	SCREEN_SETTINGS,
	SCREEN_CONTROLS,
	SCREEN_CLOCK,
	SCREEN_COUNT
};

enum { CONFIRM_NONE = 0, CONFIRM_RESTART, CONFIRM_EXIT };

enum {
	MODAL_NONE = 0, MODAL_CONFIRM, MODAL_CAPTURE, MODAL_CLOCK, MODAL_UNLOCK
};

enum { BTN_FILLED = 0, BTN_TONAL, BTN_OUTLINED, BTN_TEXT, BTN_DANGER };

enum {
	ICON_NONE = 0, ICON_BACK, ICON_CLOSE, ICON_SETTINGS, ICON_CHEVRON,
	ICON_CHECK, ICON_PLAY
};

#define UI_ANIMS 16

typedef struct cp_ui {
	fxcg100_menu_state *state;
	cp_surface s;
	int w, h;
	int bar_h;                 /* height of the top bar */
	int wide;                  /* landscape: labels beside their controls */
	int in_game;               /* a game is loaded behind the menu */
	int screen;

	/* Colours. `t` is what is being drawn with right now: the scheme, on
	 * its way in from another scheme, faded while a screen comes in, darkened
	 * under a dialog. */
	cp_theme t, base;
	uint16_t faint;            /* disabled marks, the scroll indicator */
	uint32_t theme_shown;
	cp_theme theme_from;
	uint32_t theme_t0;
	int theme_fading;
	int tint_fade, tint_dim;   /* the same treatment for any other colour */
	uint16_t tint_to;

	/* Time. */
	uint32_t now;              /* interface time at this step, in ticks */
	uint32_t real;             /* fxcg100_ticks() at this step */
	int dt;                    /* ticks since the step before */
	int motion;                /* animations are on */
	int animating;             /* something drawn this step is still moving */

	/* Input for this step. */
	uint32_t prev_keys, key_edge, prev_strip, strip_edge;
	int touching, tx, ty;      /* finger position while down */
	int press_x, press_y;      /* where this touch began */
	int dragging;              /* it has moved too far to be a tap */
	int tap, tap_x, tap_y;     /* lifted this step without dragging */
	int touch_began;           /* the finger came down at this step */
	int drag_dy;

	/* Keypad focus: index among the widgets in drawing order. */
	int focus, focus_shown, widgets, widgets_last;
	int activate, adjust;
	cp_rect focus_rect;
	int focus_seen;

	/* Motion. */
	uint32_t enter_t0;         /* the current screen started coming in */
	int entering, enter_dir, enter_black;
	int slide;                 /* sideways offset of the content this step */
	int dim;                   /* scrim under a dialog, 0..SCRIM */
	int modal;                 /* MODAL_* shown at the last step */
	uint32_t modal_t0;
	int touch_seq, last_id;    /* touch areas, numbered in drawing order */
	int ripple_live, ripple_id, ripple_x, ripple_y, ripple_released;
	uint32_t ripple_t0, ripple_up;
	struct { const void *key; int value, seen; } anim[UI_ANIMS];
	int fling;                 /* scroll speed after a flick, px/256 a tick */
	int card_y;

	/* A stepper being adjusted from the keypad (its key), and by how much
	 * at this step. */
	const void *edit;
	int edit_delta;

	int scroll[SCREEN_COUNT];
	const char *toast;
	uint32_t toast_t0;         /* interface time it appeared */
	uint32_t toast_real;       /* fxcg100_ticks() when it appeared */
	int toast_leaving;
	uint32_t toast_out_t0;
	int settings_dirty;
	int confirm;
	int capture;               /* binding being captured, -1 if none */
	int capture_armed;         /* keys were released since capture began */

	/* Clock page. `edit` is what the controls show; `running` what the
	 * hardware has. After Apply the previous setting is kept in `backup`
	 * until the player confirms or `deadline` (RTC ticks) passes. */
	cp_clock clock_edit, clock_running, clock_backup;
	cp_chain clock_chain;      /* the chain of clock_edit, as edited */
	const char *clock_tested;  /* result of the last Test, shown while the */
	cp_clock clock_tested_on;  /* page still shows the setting it was for */
	int clock_test_failed;
	int clock_loaded, clock_pending;
	uint32_t clock_deadline, clock_at_startup;
	uint32_t clock_unlocked;   /* the limits are switched off */
	int clock_unlock_ask;      /* asking whether to switch them off */
	int redraw;
} cp_ui;

/* The host test finds controls by what they say rather than by where they
 * are. */
#ifdef CGBA_HOST_TEST
static struct { char name[40]; cp_rect r; } ui_marks[128];
static int ui_mark_count;

static void ui_mark(const char *name, cp_rect r)
{
	if(ui_mark_count < 128) {
		snprintf(ui_marks[ui_mark_count].name,
			sizeof(ui_marks[0].name), "%s", name);
		ui_marks[ui_mark_count++].r = r;
	}
}
#define ui_marks_reset() (ui_mark_count = 0)
#else
#define ui_mark(name, r) ((void)0)
#define ui_marks_reset() ((void)0)
#endif

/* ---- time and motion ------------------------------------------------------ */

/* How far along (0..256) something is that began at t0 and takes `ticks`. */
static int ui_progress(cp_ui *ui, uint32_t t0, int ticks)
{
	uint32_t elapsed = ui->now - t0;

	if(!ui->motion || elapsed >= (uint32_t)ticks)
		return 256;
	ui->animating = 1;
	return (int)(elapsed * 256u / (uint32_t)ticks);
}

/* Fast at first, settling at the end. */
static int ease_out(int p)
{
	int q = 256 - p;

	return 256 - ((((q * q) >> 8) * q) >> 8);
}

/* Move a value towards a target at a steady rate. */
static int ui_approach(cp_ui *ui, int value, int target, int per_tick)
{
	int step = per_tick * ui->dt;

	if(!ui->motion)
		return target;
	if(value == target)
		return value;
	if(value < target)
		value = value + step > target ? target : value + step;
	else
		value = value - step < target ? target : value - step;
	if(value != target)
		ui->animating = 1;
	return value;
}

/* A value that follows a target smoothly, remembered between steps under a
 * key (the control's label). The first time a key is seen it starts at the
 * target, so nothing moves when a page opens. */
static int ui_anim(cp_ui *ui, const void *key, int target)
{
	int i, slot = -1;

	for(i = 0; i < UI_ANIMS; i++) {
		if(ui->anim[i].key == key)
			break;
		if(!ui->anim[i].key && slot < 0)
			slot = i;
	}
	if(i == UI_ANIMS) {
		if(slot < 0)
			return target;
		ui->anim[slot].key = key;
		ui->anim[slot].value = target;
		i = slot;
	} else if(ui->anim[i].value != target) {
		int d = target - ui->anim[i].value;
		int step = d * ui->dt / 8, floor = 2 * ui->dt;

		if(step > -floor && step < floor)
			step = d > 0 ? floor : -floor;
		if(!ui->motion || (d > 0 ? step >= d : step <= d))
			step = d;
		ui->anim[i].value += step;
		if(ui->anim[i].value != target)
			ui->animating = 1;
	}
	ui->anim[i].seen = 1;
	return ui->anim[i].value;
}

/* Forget the values of controls that were not drawn at the last step. */
static void ui_anim_collect(cp_ui *ui)
{
	int i;

	for(i = 0; i < UI_ANIMS; i++) {
		if(!ui->anim[i].seen)
			ui->anim[i].key = NULL;
		ui->anim[i].seen = 0;
	}
}

/* ---- colours -------------------------------------------------------------- */

static void ui_use(cp_ui *ui, const cp_theme *t)
{
	ui->t = *t;
	ui->faint = cp_blend(t->container, t->outline, 110, 256);
}

/* A colour from outside the scheme, given the fade and scrim in force. */
static uint16_t ui_tint(const cp_ui *ui, uint16_t color)
{
	color = cp_blend(color, ui->tint_to, ui->tint_fade, 256);
	return cp_blend(color, 0x0000, ui->tint_dim, 256);
}

/* Work out the scheme for this step: the chosen one (cross-faded when it has
 * just been changed), faded towards the background while the screen is
 * coming in. Returns how far the content should be slid sideways. */
static int ui_resolve_theme(cp_ui *ui)
{
	const cp_theme *chosen = cp_theme_get(ui->state->theme);
	cp_theme shown;
	int p, fade = 0;

	if(ui->state->theme != ui->theme_shown) {
		ui->theme_from = ui->base;
		ui->theme_t0 = ui->now;
		ui->theme_shown = ui->state->theme;
		ui->theme_fading = 1;
	}
	if(ui->theme_fading) {
		p = ui_progress(ui, ui->theme_t0, T_THEME);
		cp_theme_mix(&ui->base, &ui->theme_from, chosen, p);
		ui->theme_fading = p < 256;
	} else {
		ui->base = *chosen;
	}

	if(ui->entering) {
		p = ease_out(ui_progress(ui, ui->enter_t0, T_ENTER));
		fade = 256 - p;
		if(p >= 256)
			ui->entering = 0;
	}
	ui->tint_fade = fade;
	ui->tint_to = ui->enter_black ? 0x0000 : ui->base.surface;
	ui->tint_dim = 0;
	cp_theme_toward(&shown, &ui->base, ui->tint_to, fade);
	ui_use(ui, &shown);
	if(!ui->entering)
		ui->enter_black = 0;
	return ui->enter_dir * fade * 40 / 256;
}

/* ---- input ---------------------------------------------------------------- */

/* gpSP starts upright, the way the calculator is held to start it, even
 * when the saved view is a landscape one: until the first game starts (or
 * the player turns the screen themselves) the interface is drawn in the
 * portrait view of the same size class, and the saved view is left alone
 * for the game to use. */
static int ui_start_upright;

void cp_ui_game_starting(void)
{
	ui_start_upright = 0;
}

static void ui_refresh_orientation(cp_ui *ui)
{
	unsigned view = ui->state->screen_scale, rotation = ui->state->rotation;

	if(ui_start_upright && cp_view_is_landscape(view))
		view = cp_view_turned(view);

	fxcg100_lcd_set_scale(view);
	fxcg100_lcd_set_rotation(rotation);
	ui->w = cp_view_is_landscape(view) ? CP_PANEL_H : CP_PANEL_W;
	ui->h = cp_view_is_landscape(view) ? CP_PANEL_W : CP_PANEL_H;
	ui->wide = ui->w >= 480;
	ui->bar_h = ui->wide ? 56 : 64;
	cp_surface_init(&ui->s, gint_vram, ui->w, ui->h,
		cp_surface_mode(view, rotation));
}

static void ui_show_focus(cp_ui *ui, int delta)
{
	int last = ui->widgets_last > 0 ? ui->widgets_last - 1 : 0;

	if(!ui->focus_shown)
		ui->focus_shown = 1;      /* first key press only reveals the ring */
	else
		ui->focus += delta;
	if(ui->focus < 0)
		ui->focus = 0;
	if(ui->focus > last)
		ui->focus = last;
}

/* Read keys and touch once. Returns nonzero if anything happened. */
static int ui_input(cp_ui *ui)
{
	uint32_t keys = fxcg100_poll_app_keys();
	uint32_t strip = fxcg100_touch_strip_icons();
	int x = 0, y = 0, touching = fxcg100_touch_point(&x, &y);
	int changed;

	ui->key_edge = keys & ~ui->prev_keys;
	ui->strip_edge = strip & ~ui->prev_strip;
	ui->prev_keys = keys;
	ui->prev_strip = strip;
	changed = ui->key_edge != 0 || ui->strip_edge != 0;

	ui->tap = 0;
	ui->drag_dy = 0;
	ui->touch_began = touching && !ui->touching;
	if(touching) {
		if(!ui->touching) {
			ui->press_x = x;
			ui->press_y = y;
			ui->dragging = 0;
			ui->focus_shown = 0;
			ui->edit = NULL;          /* a tap on its value picks it again */
			changed = 1;
		} else {
			int dx = x - ui->press_x, dy = y - ui->press_y;

			if(dx > DRAG_SLOP || dx < -DRAG_SLOP || dy > DRAG_SLOP ||
					dy < -DRAG_SLOP)
				ui->dragging = 1;
			if(ui->dragging)
				ui->drag_dy = y - ui->ty;
			if(x != ui->tx || y != ui->ty)
				changed = 1;
		}
		ui->tx = x;
		ui->ty = y;
	} else if(ui->touching) {
		ui->tap = !ui->dragging;
		ui->tap_x = ui->tx;
		ui->tap_y = ui->ty;
		changed = 1;
	}
	ui->touching = touching;

	ui->activate = 0;
	ui->adjust = 0;
	ui->edit_delta = 0;
	if(ui->edit) {
		/* A stepper was tapped: the cursor keys change its value, by one
		 * left and right and by ten up and down, until EXE or back. */
		if(ui->key_edge & FXCG100_APPKEY_LEFT)
			ui->edit_delta = -1;
		if(ui->key_edge & FXCG100_APPKEY_RIGHT)
			ui->edit_delta = 1;
		if(ui->key_edge & FXCG100_APPKEY_UP)
			ui->edit_delta = 10;
		if(ui->key_edge & FXCG100_APPKEY_DOWN)
			ui->edit_delta = -10;
		if(ui->key_edge & (FXCG100_APPKEY_EXE | FXCG100_APPKEY_BACK)) {
			ui->edit = NULL;
			ui->key_edge &= ~(uint32_t)(FXCG100_APPKEY_EXE |
				FXCG100_APPKEY_BACK);
		}
		return changed;
	}
	if(ui->key_edge & FXCG100_APPKEY_UP)
		ui_show_focus(ui, -1);
	if(ui->key_edge & FXCG100_APPKEY_DOWN)
		ui_show_focus(ui, 1);
	if(ui->focus_shown) {
		if(ui->key_edge & FXCG100_APPKEY_EXE)
			ui->activate = 1;
		if(ui->key_edge & FXCG100_APPKEY_LEFT)
			ui->adjust = -1;
		if(ui->key_edge & FXCG100_APPKEY_RIGHT)
			ui->adjust = 1;
	} else if(ui->key_edge & (FXCG100_APPKEY_EXE | FXCG100_APPKEY_LEFT |
			FXCG100_APPKEY_RIGHT)) {
		ui_show_focus(ui, 0);
	}
	return changed;
}

/* What a page drawn under a dialog must not see. */
typedef struct ui_muted {
	uint32_t key_edge, strip_edge;
	int touching, tap, drag_dy, activate, adjust, focus_shown;
} ui_muted;

static void ui_mute(cp_ui *ui, ui_muted *saved)
{
	saved->key_edge = ui->key_edge;
	saved->strip_edge = ui->strip_edge;
	saved->touching = ui->touching;
	saved->tap = ui->tap;
	saved->drag_dy = ui->drag_dy;
	saved->activate = ui->activate;
	saved->adjust = ui->adjust;
	saved->focus_shown = ui->focus_shown;
	ui->key_edge = ui->strip_edge = 0;
	ui->touching = ui->tap = ui->drag_dy = 0;
	ui->activate = ui->adjust = ui->focus_shown = 0;
}

static void ui_unmute(cp_ui *ui, const ui_muted *saved)
{
	ui->key_edge = saved->key_edge;
	ui->strip_edge = saved->strip_edge;
	ui->touching = saved->touching;
	ui->tap = saved->tap;
	ui->drag_dy = saved->drag_dy;
	ui->activate = saved->activate;
	ui->adjust = saved->adjust;
	ui->focus_shown = saved->focus_shown;
}

/* Settings is one level in from the library and the pause menu; key mapping
 * and the clock page are one further. A deeper screen comes in from the
 * right, a shallower one from the left. */
static void ui_goto(cp_ui *ui, int screen)
{
	static const int8_t depth[SCREEN_COUNT] = { 0, 0, 1, 2, 2 };
	int d = depth[screen] - depth[ui->screen];

	ui->enter_dir = d > 0 ? 1 : d < 0 ? -1 : 0;
	ui->enter_t0 = ui->now;
	ui->entering = 1;
	ui->ripple_live = 0;
	ui->fling = 0;
	memset(ui->anim, 0, sizeof(ui->anim));

	ui->screen = screen;
	ui->edit = NULL;
	ui->focus = 0;
	ui->focus_shown = 0;
	ui->confirm = CONFIRM_NONE;
	ui->capture = -1;
	ui->clock_loaded = 0;
	ui->redraw = 1;
}

static void ui_snack(cp_ui *ui, const char *text)
{
	ui->toast = text;
	ui->toast_t0 = ui->now;
	/* Now, not when this step began: what led to the message may have taken
	 * seconds (the clock test), and it must still get its full time. */
	ui->toast_real = fxcg100_ticks();
	ui->toast_leaving = 0;
	ui->redraw = 1;
}

/* ---- widgets -------------------------------------------------------------- */

static int point_in(cp_rect r, int x, int y)
{
	return cp_rect_contains(r, x, y);
}

#define HIT_HOT     1   /* finger is resting on it */
#define HIT_CLICK   2   /* tapped, or activated from the keypad */
#define HIT_FOCUS   4   /* it has the keypad focus */

/* Touch state of a rectangle that does not take part in keypad focus. */
static int ui_touch(cp_ui *ui, cp_rect r)
{
	int flags = 0;

	ui->last_id = ui->touch_seq++;
	if(ui->touching && !ui->dragging && point_in(r, ui->press_x, ui->press_y)
			&& point_in(r, ui->tx, ui->ty) &&
			cp_surface_visible(&ui->s, ui->tx, ui->ty))
		flags |= HIT_HOT;
	if(ui->tap && point_in(r, ui->press_x, ui->press_y) &&
			point_in(r, ui->tap_x, ui->tap_y) &&
			cp_surface_visible(&ui->s, ui->tap_x, ui->tap_y))
		flags |= HIT_CLICK;
	return flags;
}

/* Register a widget: touch state plus keypad focus and activation. */
static int ui_widget(cp_ui *ui, cp_rect r)
{
	int flags = ui_touch(ui, r);
	int index = ui->widgets++;

	if(ui->focus_shown && index == ui->focus) {
		flags |= HIT_FOCUS;
		ui->focus_rect = r;
		ui->focus_seen = 1;
		if(ui->activate)
			flags |= HIT_CLICK;
	}
	return flags;
}

/* Feedback for a press: a translucent layer that spreads from the finger
 * over the control, stays while it is held and fades once it is let go.
 * `id` is ui->last_id from the control's ui_touch / ui_widget. */
static void ui_layer(cp_ui *ui, int id, cp_rect r, int radius, int flags,
	uint16_t color)
{
	int alpha = 34, reach, dx, dy, p;

	if((flags & HIT_HOT) && !(ui->ripple_live && ui->ripple_id == id &&
			!ui->ripple_released)) {
		ui->ripple_live = 1;
		ui->ripple_id = id;
		ui->ripple_x = ui->press_x;
		ui->ripple_y = ui->press_y;
		ui->ripple_t0 = ui->now;
		ui->ripple_released = 0;
	}
	if(!ui->ripple_live || ui->ripple_id != id)
		return;
	if(!(flags & HIT_HOT)) {
		if(!ui->ripple_released) {
			ui->ripple_released = 1;
			ui->ripple_up = ui->now;
		}
		p = ui_progress(ui, ui->ripple_up, T_RIPPLE_OUT);
		if(p >= 256) {
			ui->ripple_live = 0;
			return;
		}
		alpha = alpha * (256 - p) / 256;
	}
	dx = ui->ripple_x - r.x > r.x + r.w - ui->ripple_x
		? ui->ripple_x - r.x : r.x + r.w - ui->ripple_x;
	dy = ui->ripple_y - r.y > r.y + r.h - ui->ripple_y
		? ui->ripple_y - r.y : r.y + r.h - ui->ripple_y;
	reach = cp_isqrt(dx * dx + dy * dy) + 1;
	p = ease_out(ui_progress(ui, ui->ripple_t0, T_RIPPLE));
	cp_ripple(&ui->s, r.x, r.y, r.w, r.h, radius, ui->ripple_x, ui->ripple_y,
		12 + (reach - 12) * p / 256, color, alpha);
}

static void ui_focus_ring(const cp_ui *ui, cp_rect r, int radius)
{
	cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 2, ui->t.primary);
}

static void ui_icon(const cp_ui *ui, int icon, int cx, int cy, uint16_t color)
{
	const cp_surface *s = &ui->s;

	switch(icon) {
	case ICON_BACK:
		cp_line(s, cx - 8, cy, cx + 8, cy, 2, color);
		cp_line(s, cx - 8, cy, cx - 2, cy - 6, 2, color);
		cp_line(s, cx - 8, cy, cx - 2, cy + 6, 2, color);
		break;
	case ICON_CLOSE:
		cp_line(s, cx - 6, cy - 6, cx + 6, cy + 6, 2, color);
		cp_line(s, cx - 6, cy + 6, cx + 6, cy - 6, 2, color);
		break;
	case ICON_SETTINGS:            /* two sliders */
		cp_line(s, cx - 9, cy - 5, cx + 9, cy - 5, 2, color);
		cp_line(s, cx - 9, cy + 5, cx + 9, cy + 5, 2, color);
		cp_disc(s, cx + 4, cy - 5, 4, color);
		cp_disc(s, cx - 4, cy + 5, 4, color);
		break;
	case ICON_CHEVRON:
		cp_line(s, cx - 3, cy - 6, cx + 3, cy, 2, color);
		cp_line(s, cx + 3, cy, cx - 3, cy + 6, 2, color);
		break;
	case ICON_CHECK:
		cp_line(s, cx - 6, cy, cx - 2, cy + 4, 2, color);
		cp_line(s, cx - 2, cy + 4, cx + 6, cy - 5, 2, color);
		break;
	case ICON_PLAY:
		cp_triangle(s, cx - 5, cy - 9, cx + 9, cy, cx - 5, cy + 9, color);
		break;
	default:
		break;
	}
}

/* A pill-shaped button, optionally with an icon before its label. */
static int ui_button_icon(cp_ui *ui, cp_rect r, const char *label, int style,
	int icon)
{
	int flags = ui_widget(ui, r), id = ui->last_id;
	int radius = r.h / 2;
	int tw = cp_text_width(&cp_font_body, label), x;
	uint16_t ink = ui->t.primary;

	switch(style) {
	case BTN_FILLED:
		cp_round_rect(&ui->s, r.x, r.y, r.w, r.h, radius, ui->t.primary);
		ink = ui->t.on_primary;
		break;
	case BTN_TONAL:
		cp_round_rect(&ui->s, r.x, r.y, r.w, r.h, radius,
			ui->t.secondary_container);
		ink = ui->t.on_secondary_container;
		break;
	case BTN_OUTLINED:
		cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 1, ui->t.outline);
		break;
	case BTN_DANGER:
		cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 1, ui->t.error);
		ink = ui->t.error;
		break;
	default:
		break;
	}
	ui_layer(ui, id, r, radius, flags, ink);
	x = r.x + (r.w - tw - (icon ? 26 : 0)) / 2;
	if(icon) {
		ui_icon(ui, icon, x + 6, r.y + r.h / 2, ink);
		x += 26;
	}
	cp_text(&ui->s, &cp_font_body, x, r.y + (r.h - cp_font_body.height) / 2,
		label, ink);
	if(flags & HIT_FOCUS)
		cp_round_ring(&ui->s, r.x - 3, r.y - 3, r.w + 6, r.h + 6, radius + 3,
			2, ui->t.on_surface);
	ui_mark(label, r);
	return (flags & HIT_CLICK) != 0;
}

static int ui_button(cp_ui *ui, cp_rect r, const char *label, int style)
{
	return ui_button_icon(ui, r, label, style, ICON_NONE);
}

/* Narrow the clip to the columns x0..x1-1 (in the coordinates being drawn),
 * keeping the old clip in *saved for the caller to put back. */
static void ui_clip_columns(cp_ui *ui, cp_rect *saved, int x0, int x1)
{
	cp_rect c = ui->s.clip;

	*saved = c;
	x0 += ui->s.ox;
	x1 += ui->s.ox;
	if(x0 < c.x)
		x0 = c.x;
	if(x1 > c.x + c.w)
		x1 = c.x + c.w;
	if(x1 < x0)
		x1 = x0;
	ui->s.clip = (cp_rect){ (int16_t)x0, c.y, (int16_t)(x1 - x0), c.h };
}

/* A row of mutually exclusive choices, as a Material segmented button: one
 * outlined pill divided into segments, the chosen one filled edge to edge
 * with a check mark before its label. The fill slides to a newly chosen
 * segment. `key` names the
 * control for ui_anim. Returns nonzero if *value changed. */
static int ui_segments(cp_ui *ui, cp_rect r, const void *key,
	const char *const *labels, int n, uint32_t *value)
{
	int index = ui->widgets++;
	int focused = ui->focus_shown && index == ui->focus;
	uint32_t old = *value;
	int i, cell = r.w / n, radius = r.h / 2, pos;
	int flags[8], ids[8];
	cp_rect saved;

	if(n > 8)
		n = 8;
	if(focused) {
		ui->focus_rect = r;
		ui->focus_seen = 1;
		if(ui->adjust < 0 && *value > 0)
			(*value)--;
		if(ui->adjust > 0 && (int)*value < n - 1)
			(*value)++;
		if(ui->activate)
			*value = (*value + 1) % (uint32_t)n;
	}
	for(i = 0; i < n; i++) {
		cp_rect c = { (int16_t)(r.x + i * cell), r.y,
			(int16_t)(i == n - 1 ? r.w - i * cell : cell), r.h };

		flags[i] = ui_touch(ui, c);
		ids[i] = ui->last_id;
		if(flags[i] & HIT_CLICK)
			*value = (uint32_t)i;
		ui_mark(labels[i], c);
	}

	/* The fill of the chosen segment: the part of the whole pill that lies
	 * over that segment, so its outer end is round and its inner edges are
	 * straight. Drawn by narrowing the clip to those columns. */
	pos = ui_anim(ui, key, (int)*value * 256);
	{
		int x0 = r.x + pos * cell / 256;
		int x1 = pos >= (n - 1) * 256 ? r.x + r.w : x0 + cell;

		ui_clip_columns(ui, &saved, x0, x1);
		cp_round_rect(&ui->s, r.x, r.y, r.w, r.h, radius,
			ui->t.secondary_container);
		ui->s.clip = saved;
	}

	for(i = 0; i < n; i++) {
		cp_rect c = { (int16_t)(r.x + i * cell), r.y,
			(int16_t)(i == n - 1 ? r.w - i * cell : cell), r.h };
		int chosen = (uint32_t)i == *value;
		/* Material's label size: the smaller, medium-weight face. */
		const cp_font *font = &cp_font_small;
		int tw = cp_text_width(font, labels[i]);
		int x = c.x + (c.w - tw) / 2;
		uint16_t ink = chosen ? ui->t.on_secondary_container
			: ui->t.on_surface;

		/* The check mark and the label are centred together. */
		if(chosen && tw + 30 <= c.w) {
			x += 10;
			ui_icon(ui, ICON_CHECK, x - 14, c.y + c.h / 2, ink);
		}
		cp_text(&ui->s, font, x, c.y + (c.h - font->height) / 2, labels[i],
			ink);
		/* The pressed layer takes the segment's own shape too. */
		ui_clip_columns(ui, &saved, c.x, c.x + c.w);
		ui_layer(ui, ids[i], r, radius, flags[i], ink);
		ui->s.clip = saved;
	}

	/* The outline and every divider go on top, in the outline colour. */
	cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 1, ui->t.outline);
	for(i = 1; i < n; i++)
		cp_fill(&ui->s, r.x + i * cell, r.y + 1, 1, r.h - 2, ui->t.outline);
	if(focused)
		cp_round_ring(&ui->s, r.x - 3, r.y - 3, r.w + 6, r.h + 6, radius + 3,
			2, ui->t.primary);
	return *value != old;
}

/* "-  value  +" over min..max in steps; `text` is what to show for the
 * value. With a `key` (the control's label), a tap on the value picks the
 * stepper for the keypad: left and right then change it by one, up and down
 * by `big`. Returns nonzero if *value changed. */
static int ui_stepper(cp_ui *ui, cp_rect r, const char *key, uint32_t big,
	uint32_t *value, uint32_t min, uint32_t max, uint32_t step,
	const char *text)
{
	int index = ui->widgets++;
	int focused = ui->focus_shown && index == ui->focus;
	cp_rect minus = { r.x, r.y, 56, r.h };
	cp_rect plus = { (int16_t)(r.x + r.w - 56), r.y, 56, r.h };
	int fm = ui_touch(ui, minus), idm = ui->last_id;
	int fp = ui_touch(ui, plus), idp = ui->last_id;
	cp_rect middle = { (int16_t)(r.x + 56), r.y, (int16_t)(r.w - 112), r.h };
	int fv = key ? ui_touch(ui, middle) : 0;
	int radius = r.h / 2, cy = r.y + r.h / 2;
	uint32_t old = *value;
	uint16_t ink;

	if(focused) {
		ui->focus_rect = r;
		ui->focus_seen = 1;
	}
	if(((fm & HIT_CLICK) || (focused && ui->adjust < 0)) && *value > min)
		*value = *value - min < step ? min : *value - step;
	if(((fp & HIT_CLICK) || (focused && ui->adjust > 0)) && *value < max)
		*value = max - *value < step ? max : *value + step;
	if(fv & HIT_CLICK) {
		ui->edit = key;
		ui->redraw = 1;
	}
	if(key && ui->edit == key && ui->edit_delta) {
		/* edit_delta is 1 for left/right and 10 for up/down. */
		uint32_t d = ui->edit_delta == 1 || ui->edit_delta == -1 ? 1u : big;

		if(ui->edit_delta < 0)
			*value = *value - min < d ? min : *value - d;
		else
			*value = max - *value < d ? max : *value + d;
	}

	cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 1, ui->t.outline);
	ui_layer(ui, idm, minus, radius, fm, ui->t.on_surface);
	ui_layer(ui, idp, plus, radius, fp, ui->t.on_surface);
	ink = *value > min ? ui->t.primary : ui->faint;
	cp_line(&ui->s, minus.x + 22, cy, minus.x + 34, cy, 2, ink);
	ink = *value < max ? ui->t.primary : ui->faint;
	cp_line(&ui->s, plus.x + 22, cy, plus.x + 34, cy, 2, ink);
	cp_line(&ui->s, plus.x + 28, cy - 6, plus.x + 28, cy + 6, 2, ink);
	if(key && ui->edit == key) {
		/* Picked: the value is on the selection tone, the outline thick. */
		cp_round_rect(&ui->s, middle.x, r.y + 4, middle.w, r.h - 8,
			(r.h - 8) / 2, ui->t.secondary_container);
		cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, radius, 2, ui->t.primary);
	}
	cp_text_centered(&ui->s, &cp_font_body, r.x, r.y, r.w, r.h, text,
		key && ui->edit == key ? ui->t.on_secondary_container
		: ui->t.on_surface);
	if(focused)
		cp_round_ring(&ui->s, r.x - 3, r.y - 3, r.w + 6, r.h + 6, radius + 3,
			2, ui->t.primary);
	ui_mark("-", minus);
	ui_mark("+", plus);
	if(key)
		ui_mark(key, middle);
	return *value != old;
}

/* A stepper that shows the number itself. */
static int ui_number(cp_ui *ui, cp_rect r, uint32_t *value, uint32_t max)
{
	char text[12];

	/* Format after stepping would lag a frame; the caller redraws on change. */
	snprintf(text, sizeof(text), "%u", (unsigned)*value);
	return ui_stepper(ui, r, NULL, 1, value, 0, max, 1, text);
}

/* The picture of a switch, `on` running from 0 (off) to 256. */
static void ui_switch_draw(const cp_ui *ui, int x, int y, int on)
{
	uint16_t track = cp_blend(ui->t.container_high, ui->t.primary, on, 256);
	uint16_t edge = cp_blend(ui->t.outline, ui->t.primary, on, 256);
	uint16_t thumb = cp_blend(ui->t.outline, ui->t.on_primary, on, 256);

	cp_round_rect(&ui->s, x, y, 52, 32, 16, edge);
	cp_round_rect(&ui->s, x + 2, y + 2, 48, 28, 14, track);
	cp_disc(&ui->s, x + 16 + on * 20 / 256, y + 16, 8 + on * 4 / 256, thumb);
}

/* The ten state slots as chips, five to a row; one keypad focus stop.
 * Returns nonzero if *value changed. */
static int ui_slot_chips(cp_ui *ui, int x, int y, int w, uint32_t *value)
{
	int index = ui->widgets++;
	int focused = ui->focus_shown && index == ui->focus;
	int cw = (w - 16) / 5, i;
	uint32_t old = *value;

	if(focused) {
		ui->focus_rect = (cp_rect){ (int16_t)x, (int16_t)y, (int16_t)w, 78 };
		ui->focus_seen = 1;
		if(ui->adjust < 0 && *value > 0)
			(*value)--;
		if(ui->adjust > 0 && *value < 9)
			(*value)++;
	}
	for(i = 0; i < 10; i++) {
		cp_rect c = { (int16_t)(x + (i % 5) * (cw + 4)),
			(int16_t)(y + (i / 5) * 42), (int16_t)cw, 36 };
		/* The touch area takes the gaps as well. */
		cp_rect hit = { (int16_t)(c.x - 2), (int16_t)(c.y - 3),
			(int16_t)(c.w + 4), 42 };
		int flags = ui_touch(ui, hit), id = ui->last_id;
		char text[2] = { (char)('0' + i), 0 };
		int chosen;

		if(flags & HIT_CLICK)
			*value = (uint32_t)i;
		chosen = (uint32_t)i == *value;
		if(chosen)
			cp_round_rect(&ui->s, c.x, c.y, c.w, c.h, 18, ui->t.primary);
		else
			cp_round_ring(&ui->s, c.x, c.y, c.w, c.h, 18, 1, ui->t.outline);
		ui_layer(ui, id, c, 18, flags, ui->t.on_surface);
		cp_text_centered(&ui->s, &cp_font_body, c.x, c.y, c.w, c.h, text,
			chosen ? ui->t.on_primary : ui->t.on_surface);
		if(chosen && focused)
			cp_round_ring(&ui->s, c.x - 3, c.y - 3, c.w + 6, c.h + 6, 21, 2,
				ui->t.on_surface);
		ui_mark(text, hit);
	}
	return *value != old;
}

/* The colour schemes as swatches in a grid; one keypad focus stop. */
static int ui_swatches(cp_ui *ui, cp_rect r, int cols, uint32_t *value)
{
	int index = ui->widgets++;
	int focused = ui->focus_shown && index == ui->focus;
	int rows = (CP_THEME_COUNT + cols - 1) / cols;
	int cw = r.w / cols, ch = r.h / rows, i;
	uint32_t old = *value;

	if(focused) {
		ui->focus_rect = r;
		ui->focus_seen = 1;
		if(ui->adjust < 0 && *value > 0)
			(*value)--;
		if(ui->adjust > 0 && *value < CP_THEME_COUNT - 1)
			(*value)++;
	}
	for(i = 0; i < CP_THEME_COUNT; i++) {
		cp_rect c = { (int16_t)(r.x + (i % cols) * cw),
			(int16_t)(r.y + (i / cols) * ch), (int16_t)cw, (int16_t)ch };
		int cx = c.x + cw / 2, cy = c.y + ch / 2;
		int flags = ui_touch(ui, c), id = ui->last_id;
		cp_rect ring = { (int16_t)(cx - 20), (int16_t)(cy - 20), 40, 40 };

		if(flags & HIT_CLICK)
			*value = (uint32_t)i;
		cp_disc(&ui->s, cx, cy, 15, ui_tint(ui, cp_themes[i].primary));
		if((uint32_t)i == *value) {
			cp_round_ring(&ui->s, ring.x, ring.y, 40, 40, 20, 2,
				focused ? ui->t.primary : ui->t.on_surface);
			ui_icon(ui, ICON_CHECK, cx, cy,
				ui_tint(ui, cp_themes[i].on_primary));
		}
		ui_layer(ui, id, ring, 20, flags, ui->t.on_surface);
		ui_mark(cp_theme_names[i], c);
	}
	return *value != old;
}

#define BAR_BACK   1
#define BAR_ACTION 2

/* Top bar: optional icon on the left (back, close), the title, optional
 * icon button on the right. It takes the card tone once the page under it
 * has been scrolled. Returns which icon was used.
 *
 * What is drawn after the bar is the screen's content, which slides in when
 * the screen is new; the bar itself stays put. */
static int ui_bar(cp_ui *ui, const char *title, int nav, int action)
{
	int scroll = ui->scroll[ui->screen];
	int lift = scroll <= 0 ? 0 : scroll >= 24 ? 256 : scroll * 256 / 24;
	int x = 16, right = ui->w - 16, used = 0, cy = ui->bar_h / 2;

	cp_fill(&ui->s, 0, 0, ui->w, ui->bar_h,
		cp_blend(ui->t.surface, ui->t.container, lift, 256));
	if(nav) {
		cp_rect r = { 4, (int16_t)(cy - 24), 48, 48 };
		int flags = ui_widget(ui, r), id = ui->last_id;

		ui_layer(ui, id, r, 24, flags, ui->t.on_surface);
		ui_icon(ui, nav, r.x + 24, cy, ui->t.on_surface);
		if(flags & HIT_FOCUS)
			ui_focus_ring(ui, r, 24);
		if(flags & HIT_CLICK)
			used = BAR_BACK;
		ui_mark(nav == ICON_BACK ? "Back" : "Close", r);
		x = 56;
	}
	if(action) {
		cp_rect r = { (int16_t)(ui->w - 56), (int16_t)(cy - 24), 48, 48 };
		int flags = ui_widget(ui, r), id = ui->last_id;

		cp_disc(&ui->s, r.x + 24, cy, 20, ui->t.container_high);
		ui_layer(ui, id, r, 24, flags, ui->t.on_surface);
		ui_icon(ui, action, r.x + 24, cy, ui->t.on_surface_variant);
		if(flags & HIT_FOCUS)
			ui_focus_ring(ui, r, 24);
		if(flags & HIT_CLICK)
			used = BAR_ACTION;
		ui_mark("Settings icon", r);
		right = r.x - 4;
	}
	cp_text_fit(&ui->s, &cp_font_title, x, (ui->bar_h - cp_font_title.height) / 2,
		right - x, title, ui->t.on_surface);
	ui->s.ox = ui->slide;
	return used;
}

/* The part of the screen under the bar scrolls. Call before drawing its
 * content (at y offsets minus the returned scroll) and ui_scroll_end() with
 * the content height after. */
static int ui_scroll_begin(cp_ui *ui)
{
	cp_rect area = { 0, (int16_t)ui->bar_h, (int16_t)ui->w,
		(int16_t)(ui->h - ui->bar_h) };
	int *scroll = &ui->scroll[ui->screen];

	if(ui->touching) {
		if(ui->dragging && ui->press_y >= ui->bar_h) {
			*scroll -= ui->drag_dy;
			/* Remember how fast, for when the finger lifts. */
			ui->fling = (ui->fling + ui->drag_dy * 256 /
				(ui->dt > 0 ? ui->dt : 1)) / 2;
		} else {
			ui->fling = 0;
		}
	} else if(ui->fling) {
		if(!ui->motion || ui->dt >= 20) {
			ui->fling = 0;
		} else {
			*scroll -= ui->fling * ui->dt / 256;
			ui->fling -= ui->fling * ui->dt / 20;
			if(ui->fling > -96 && ui->fling < 96)
				ui->fling = 0;
			else
				ui->animating = 1;
		}
	}
	cp_surface_clip(&ui->s, area);
	ui->focus_seen = 0;
	return *scroll;
}

static void ui_scroll_end(cp_ui *ui, int content_h)
{
	int *scroll = &ui->scroll[ui->screen];
	int view_h = ui->h - ui->bar_h, max = content_h - view_h, old = *scroll;

	cp_surface_unclip(&ui->s);
	/* Bring the keypad focus on screen, most of the way at each step. */
	if(ui->focus_shown && ui->focus_seen) {
		int d = 0;

		if(ui->focus_rect.y < ui->bar_h + 8)
			d = ui->focus_rect.y - (ui->bar_h + 8);
		else if(ui->focus_rect.y + ui->focus_rect.h > ui->h - 8)
			d = ui->focus_rect.y + ui->focus_rect.h - (ui->h - 8);
		*scroll += ui->motion && (d > 6 || d < -6) ? d * 2 / 3 : d;
	}
	if(*scroll > max) {
		*scroll = max;
		ui->fling = 0;
	}
	if(*scroll < 0) {
		*scroll = 0;
		ui->fling = 0;
	}
	if(*scroll != old)
		ui->redraw = 1;

	/* Scroll indicator. */
	if(max > 0) {
		int bar_h = view_h * view_h / content_h;
		int bar_y = ui->bar_h + (view_h - bar_h) * *scroll / max;
		int saved = ui->s.ox;

		ui->s.ox = 0;
		cp_round_rect(&ui->s, ui->w - 5, bar_y + 2, 3, bar_h - 4, 1, ui->faint);
		ui->s.ox = saved;
	}
}

/* ---- cards and rows -------------------------------------------------------
 * A scrolling page is section labels and cards. A card's height is not known
 * until its rows have been drawn, so each row paints its own strip of the
 * card and the corners are rounded off at the end. */

static void section_label(cp_ui *ui, int *y, const char *text)
{
	cp_text(&ui->s, &cp_font_small, PAD, *y + 16, text, ui->t.primary);
	*y += 16 + cp_font_small.height + 8;
}

static void card_begin(cp_ui *ui, int y)
{
	ui->card_y = y;
}

static cp_rect row_begin(cp_ui *ui, int *y, int h)
{
	cp_rect r = { MARGIN, (int16_t)*y, (int16_t)(ui->w - 2 * MARGIN),
		(int16_t)h };

	cp_fill(&ui->s, r.x, r.y, r.w, r.h, ui->t.container);
	*y += h;
	return r;
}

static void card_end(cp_ui *ui, int *y)
{
	int x1 = ui->w - MARGIN - CARD_RADIUS, y1 = *y - CARD_RADIUS;

	cp_corner(&ui->s, MARGIN, ui->card_y, CARD_RADIUS, 0, ui->t.surface);
	cp_corner(&ui->s, x1, ui->card_y, CARD_RADIUS, 1, ui->t.surface);
	cp_corner(&ui->s, MARGIN, y1, CARD_RADIUS, 2, ui->t.surface);
	cp_corner(&ui->s, x1, y1, CARD_RADIUS, 3, ui->t.surface);
	*y += 4;
}

/* The pressed layer and focus ring of a whole row, inset from the card. */
static void row_feedback(cp_ui *ui, cp_rect r, int flags, int id)
{
	cp_rect in = { (int16_t)(r.x + 4), (int16_t)(r.y + 2),
		(int16_t)(r.w - 8), (int16_t)(r.h - 4) };

	ui_layer(ui, id, in, 16, flags, ui->t.on_surface);
	if(flags & HIT_FOCUS)
		ui_focus_ring(ui, in, 16);
}

/* A line of text with an optional second, smaller line under it, centred
 * vertically in a row. */
static void row_text(cp_ui *ui, cp_rect r, int max_w, const char *label,
	const char *support)
{
	int y = r.y + (r.h - cp_font_body.height -
		(support ? cp_font_small.height : 0)) / 2;

	cp_text_fit(&ui->s, &cp_font_body, r.x + PAD, y, max_w, label,
		ui->t.on_surface);
	if(support)
		cp_text_fit(&ui->s, &cp_font_small, r.x + PAD,
			y + cp_font_body.height, max_w, support,
			ui->t.on_surface_variant);
}

/* An item that leads somewhere, anywhere on a card. */
static int nav_item(cp_ui *ui, cp_rect r, const char *label,
	const char *support)
{
	int flags = ui_widget(ui, r), id = ui->last_id;

	row_text(ui, r, r.w - 2 * PAD - 24, label, support);
	ui_icon(ui, ICON_CHEVRON, r.x + r.w - 26, r.y + r.h / 2,
		ui->t.on_surface_variant);
	row_feedback(ui, r, flags, id);
	ui_mark(label, r);
	return (flags & HIT_CLICK) != 0;
}

static int row_nav(cp_ui *ui, int *y, const char *label, const char *support)
{
	return nav_item(ui, row_begin(ui, y, support ? 72 : 56), label, support);
}

/* A row with a switch; a tap anywhere on it flips the switch. */
static int row_switch(cp_ui *ui, int *y, const char *label,
	const char *support, uint32_t *value)
{
	cp_rect r = row_begin(ui, y, support ? 72 : 56);
	int flags = ui_widget(ui, r), id = ui->last_id;
	int changed = 0;

	if((flags & HIT_CLICK) || ((flags & HIT_FOCUS) && ui->adjust)) {
		*value = !*value;
		changed = 1;
	}
	row_text(ui, r, r.w - 2 * PAD - 64, label, support);
	ui_switch_draw(ui, r.x + r.w - PAD - 52, r.y + (r.h - 32) / 2,
		ui_anim(ui, label, *value ? 256 : 0));
	row_feedback(ui, r, flags, id);
	ui_mark(label, r);
	return changed;
}

/* A row for a control with its name: side by side when the screen is wide,
 * the name above the control when it is not. Returns the control's place. */
static cp_rect setting_row_h(cp_ui *ui, int *y, const char *label,
	const char *value, int control_h)
{
	cp_rect r, control;

	if(ui->wide) {
		r = row_begin(ui, y, control_h + 16);
		cp_text(&ui->s, &cp_font_body, r.x + PAD,
			r.y + (56 - cp_font_body.height) / 2, label, ui->t.on_surface);
		if(value)
			cp_text(&ui->s, &cp_font_small, r.x + PAD, r.y + 40, value,
				ui->t.on_surface_variant);
		control = (cp_rect){ (int16_t)(r.x + r.w - PAD - 280),
			(int16_t)(r.y + 8), 280, (int16_t)control_h };
	} else {
		/* 10 above the name, 8 between it and the control, 12 below. */
		int top = 10 + cp_font_small.height + 8;

		r = row_begin(ui, y, top + control_h + 12);
		cp_text(&ui->s, &cp_font_small, r.x + PAD, r.y + 10, label,
			ui->t.on_surface_variant);
		if(value)
			cp_text(&ui->s, &cp_font_small, r.x + r.w - PAD -
				cp_text_width(&cp_font_small, value), r.y + 10, value,
				ui->t.on_surface);
		control = (cp_rect){ (int16_t)(r.x + PAD), (int16_t)(r.y + top),
			(int16_t)(r.w - 2 * PAD), (int16_t)control_h };
	}
	return control;
}

static cp_rect setting_row(cp_ui *ui, int *y, const char *label)
{
	return setting_row_h(ui, y, label, NULL, CONTROL_H);
}

/* Two buttons side by side across the page. */
static void button_pair(const cp_ui *ui, int y, int h, cp_rect *a, cp_rect *b)
{
	int w = (ui->w - 2 * MARGIN - 2 * 4 - 8) / 2;

	*a = (cp_rect){ MARGIN + 4, (int16_t)y, (int16_t)w, (int16_t)h };
	*b = (cp_rect){ (int16_t)(MARGIN + 4 + w + 8), (int16_t)y, (int16_t)w,
		(int16_t)h };
}

/* ---- dialogs --------------------------------------------------------------
 * A dialog is drawn over its page, which is drawn first, darkened and deaf
 * to input (see cp_ui_step). */

static cp_rect dialog_box(cp_ui *ui, int h)
{
	int w = ui->w - 48 > 312 ? 312 : ui->w - 48;
	cp_rect box = { (int16_t)((ui->w - w) / 2), (int16_t)((ui->h - h) / 2),
		(int16_t)w, (int16_t)h };

	/* It rises into place. */
	ui->s.oy = (256 - ease_out(ui_progress(ui, ui->modal_t0, T_DIALOG))) *
		28 / 256;
	cp_round_rect(&ui->s, box.x, box.y, box.w, box.h, 28,
		ui->t.container_high);
	return box;
}

static void dialog_text(cp_ui *ui, cp_rect box, const char *title,
	const char *body)
{
	cp_text_fit(&ui->s, &cp_font_title, box.x + 24, box.y + 22, box.w - 48,
		title, ui->t.on_surface);
	cp_text_fit(&ui->s, &cp_font_small, box.x + 24, box.y + 60, box.w - 48,
		body, ui->t.on_surface_variant);
}

/* Places for the buttons along the bottom, right-aligned: `right` is the
 * main one, `left` (which may be NULL) the other. */
static void dialog_buttons(cp_rect box, const char *left, const char *right,
	cp_rect *l, cp_rect *r)
{
	int rw = cp_text_width(&cp_font_body, right) + 48;
	int lw = left ? cp_text_width(&cp_font_body, left) + 36 : 0;

	*r = (cp_rect){ (int16_t)(box.x + box.w - 20 - rw),
		(int16_t)(box.y + box.h - 20 - 44), (int16_t)rw, 44 };
	*l = (cp_rect){ (int16_t)(r->x - 8 - lw), r->y, (int16_t)lw, 44 };
}

/* ---- library -------------------------------------------------------------- */

/* A ROM's name as shown: the file name without its ".gba". */
static void game_title(char *dst, size_t size, uint32_t index)
{
	size_t len;

	snprintf(dst, size, "%s", fxcg100_rom_source_label(index));
	len = strlen(dst);
	if(len > 4 && dst[len - 4] == '.' &&
			(dst[len - 3] | 0x20) == 'g' && (dst[len - 2] | 0x20) == 'b' &&
			(dst[len - 1] | 0x20) == 'a')
		dst[len - 4] = 0;
}

#define GAME_ROW_H 64

static fxcg100_menu_result screen_library(cp_ui *ui)
{
	uint32_t count = fxcg100_rom_source_count(), i;
	int bar, y, top;
	char name[64], text[32];

	bar = ui_bar(ui, "Games", ui->in_game ? ICON_BACK : ICON_CLOSE,
		ICON_SETTINGS);
	if(bar == BAR_ACTION)
		ui_goto(ui, SCREEN_SETTINGS);
	if(bar == BAR_BACK || (ui->key_edge & FXCG100_APPKEY_BACK)) {
		if(ui->in_game)
			ui_goto(ui, SCREEN_PAUSE);
		else if(bar == BAR_BACK)
			return FXCG100_MENU_QUIT;
	}

	if(count == 0) {
		cp_text_centered(&ui->s, &cp_font_title, 0, ui->h / 2 - 36, ui->w, 28,
			"No games yet", ui->t.on_surface);
		cp_text_centered(&ui->s, &cp_font_small, 0, ui->h / 2, ui->w, 20,
			"Copy .gba files to the calculator's storage.",
			ui->t.on_surface_variant);
		return FXCG100_MENU_CONTINUE;
	}

	top = ui->bar_h - ui_scroll_begin(ui);
	y = top + 4;

	if(ui->in_game) {
		/* The game behind the menu: one tap goes back to it. */
		cp_rect r = { MARGIN, (int16_t)y, (int16_t)(ui->w - 2 * MARGIN), 92 };
		int flags = ui_widget(ui, r), id = ui->last_id;
		int cx = r.x + r.w - 44, cy = r.y + 46;

		cp_round_rect(&ui->s, r.x, r.y, r.w, r.h, CARD_RADIUS,
			ui->t.primary_container);
		game_title(name, sizeof(name), ui->state->rom_source);
		snprintf(text, sizeof(text), "State slot %u",
			(unsigned)ui->state->savestate_slot);
		cp_text(&ui->s, &cp_font_small, r.x + PAD, r.y + 12, "Now playing",
			ui->t.on_primary_container);
		cp_text_fit(&ui->s, &cp_font_title, r.x + PAD, r.y + 32,
			r.w - 2 * PAD - 64, name, ui->t.on_primary_container);
		cp_text(&ui->s, &cp_font_small, r.x + PAD, r.y + 64, text,
			ui->t.on_primary_container);
		cp_disc(&ui->s, cx, cy, 24, ui->t.primary);
		ui_icon(ui, ICON_PLAY, cx, cy, ui->t.on_primary);
		ui_layer(ui, id, r, CARD_RADIUS, flags, ui->t.on_primary_container);
		if(flags & HIT_FOCUS)
			cp_round_ring(&ui->s, r.x, r.y, r.w, r.h, CARD_RADIUS, 2,
				ui->t.on_surface);
		ui_mark("Now playing", r);
		if(flags & HIT_CLICK)
			return FXCG100_MENU_RETURN;
		y += r.h;
	}

	section_label(ui, &y, "All games");
	card_begin(ui, y);
	for(i = 0; i < count; i++) {
		cp_rect r = row_begin(ui, &y, GAME_ROW_H);
		int flags = ui_widget(ui, r), id = ui->last_id;
		int current = ui->in_game && i == ui->state->rom_source;
		char initial[2] = { '?', 0 };
		const char *c;

		game_title(name, sizeof(name), i);
		for(c = name; *c; c++)
			if((*c >= '0' && *c <= '9') || ((*c | 0x20) >= 'a' &&
					(*c | 0x20) <= 'z')) {
				initial[0] = *c >= 'a' ? (char)(*c - 0x20) : *c;
				break;
			}
		cp_round_rect(&ui->s, r.x + 12, r.y + 12, 40, 40, 12, current
			? ui->t.primary : ui->t.secondary_container);
		cp_text_centered(&ui->s, &cp_font_body, r.x + 12, r.y + 12, 40, 40,
			initial, current ? ui->t.on_primary
			: ui->t.on_secondary_container);
		cp_text_fit(&ui->s, &cp_font_body, r.x + 64,
			r.y + (r.h - cp_font_body.height) / 2,
			r.w - 64 - PAD - (current ? 60 : 0), name, ui->t.on_surface);
		if(current)
			cp_text(&ui->s, &cp_font_small, r.x + r.w - PAD -
				cp_text_width(&cp_font_small, "Playing"),
				r.y + (r.h - cp_font_small.height) / 2, "Playing",
				ui->t.primary);
		row_feedback(ui, r, flags, id);
		ui_mark(name, r);
		if(flags & HIT_CLICK) {
			ui->state->rom_source = i;
			return FXCG100_MENU_LOAD_GAME;
		}
	}
	card_end(ui, &y);
	ui_scroll_end(ui, y + 8 - top);
	return FXCG100_MENU_CONTINUE;
}

/* ---- pause menu ----------------------------------------------------------- */

static fxcg100_menu_result modal_confirm(cp_ui *ui)
{
	int restart = ui->confirm == CONFIRM_RESTART;
	const char *go = restart ? "Restart" : "Exit";
	cp_rect box = dialog_box(ui, 172), cancel, ok;

	dialog_text(ui, box, restart ? "Restart this game?" : "Exit gpSP?",
		restart ? "Progress since the last save is lost."
		: "The in-game save is written first.");
	dialog_buttons(box, "Cancel", go, &cancel, &ok);
	if(ui_button(ui, cancel, "Cancel", BTN_TEXT) ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		ui->confirm = CONFIRM_NONE;
		ui->focus_shown = 0;
		ui->redraw = 1;
		return FXCG100_MENU_CONTINUE;
	}
	if(ui_button(ui, ok, go, BTN_FILLED))
		return restart ? FXCG100_MENU_RESET : FXCG100_MENU_QUIT;
	return FXCG100_MENU_CONTINUE;
}

#define STATE_CARD_H 204
#define NAV_CARD_H 168

/* Which of the ten state slots Save and Load use, and the two actions. */
static fxcg100_menu_result pause_state_card(cp_ui *ui, int x, int y, int w)
{
	int bw = (w - 24 - 8) / 2;
	cp_rect save = { (int16_t)(x + 12), (int16_t)(y + 134), (int16_t)bw, 56 };
	cp_rect load = { (int16_t)(x + 12 + bw + 8), save.y, (int16_t)bw, 56 };

	cp_round_rect(&ui->s, x, y, w, STATE_CARD_H, CARD_RADIUS, ui->t.container);
	cp_text(&ui->s, &cp_font_small, x + PAD, y + 12, "State slot",
		ui->t.on_surface_variant);
	if(ui_slot_chips(ui, x + 12, y + 42, w - 24, &ui->state->savestate_slot))
		ui->redraw = 1;
	if(ui_button(ui, save, "Save state", BTN_TONAL))
		return FXCG100_MENU_SAVE_STATE;
	if(ui_button(ui, load, "Load state", BTN_TONAL))
		return FXCG100_MENU_LOAD_STATE;
	return FXCG100_MENU_CONTINUE;
}

static void pause_nav_card(cp_ui *ui, int x, int y, int w)
{
	int bw = (w - 24 - 8) / 2;
	cp_rect settings = { (int16_t)x, (int16_t)y, (int16_t)w, 56 };
	cp_rect games = { (int16_t)x, (int16_t)(y + 56), (int16_t)w, 56 };
	cp_rect restart = { (int16_t)(x + 12), (int16_t)(y + 118), (int16_t)bw, 40 };
	cp_rect quit = { (int16_t)(x + 12 + bw + 8), restart.y, (int16_t)bw, 40 };

	cp_round_rect(&ui->s, x, y, w, NAV_CARD_H, CARD_RADIUS, ui->t.container);
	if(nav_item(ui, settings, "Settings", NULL))
		ui_goto(ui, SCREEN_SETTINGS);
	if(nav_item(ui, games, "Games", NULL))
		ui_goto(ui, SCREEN_LIBRARY);
	if(ui_button(ui, restart, "Restart", BTN_OUTLINED)) {
		ui->confirm = CONFIRM_RESTART;
		ui->focus_shown = 0;
		ui->redraw = 1;
	}
	if(ui_button(ui, quit, "Exit gpSP", BTN_DANGER)) {
		ui->confirm = CONFIRM_EXIT;
		ui->focus_shown = 0;
		ui->redraw = 1;
	}
}

static fxcg100_menu_result screen_pause(cp_ui *ui)
{
	fxcg100_menu_result result;
	int x = MARGIN, w = ui->w - 2 * MARGIN, y = ui->bar_h;
	cp_rect resume;
	char name[64];

	game_title(name, sizeof(name), ui->state->rom_source);
	ui_bar(ui, name, ICON_NONE, ICON_NONE);

	/* Clear or Esc goes straight back to the game. */
	if(ui->key_edge & (FXCG100_APPKEY_BACK | FXCG100_APPKEY_ON))
		if(!(ui->prev_strip & (FXCG100_STRIP_MENU | FXCG100_STRIP_SETTINGS)))
			return FXCG100_MENU_RETURN;

	/* Upright: Resume, the state card, the rest, in one column. Turned:
	 * Resume and the rest on the left, the state card on the right. */
	if(ui->wide) {
		w = (ui->w - 2 * MARGIN - 12) / 2;
		y += 4;
	}
	resume = (cp_rect){ (int16_t)(x + 4), (int16_t)y, (int16_t)(w - 8), 56 };
	if(ui_button_icon(ui, resume, "Resume", BTN_FILLED, ICON_PLAY))
		return FXCG100_MENU_RETURN;
	if(ui->wide) {
		result = pause_state_card(ui, x + w + 12, y, w);
		pause_nav_card(ui, x, y + 56 + 14, w);
	} else {
		result = pause_state_card(ui, x, y + 56 + 14, w);
		pause_nav_card(ui, x, y + 56 + 14 + STATE_CARD_H + 12, w);
	}
	return result;
}

/* ---- settings ------------------------------------------------------------- */

static void megahertz(char *dst, size_t size, const char *name, uint32_t hz)
{
	hz += 50000u;              /* round to a tenth of a megahertz */
	snprintf(dst, size, "%s %u.%u MHz", name, (unsigned)(hz / 1000000u),
		(unsigned)(hz / 100000u % 10u));
}

static void leave_settings(cp_ui *ui)
{
	if(ui->settings_dirty) {
		ui_snack(ui, fxcg100_config_save(ui->state) ? "Settings saved"
			: "Could not save the settings");
		ui->settings_dirty = 0;
	}
	ui_goto(ui, ui->in_game ? SCREEN_PAUSE : SCREEN_LIBRARY);
}

static fxcg100_menu_result screen_settings(cp_ui *ui)
{
	static const char *const turn[] = { "Portrait", "CW", "CCW" };
	static const char *const size_portrait[] = { "1:1", "Fit width" };
	static const char *const size_landscape[] = { "1:1", "1.5x", "2x" };
	static const char *const filter[] = { "Smooth", "Sharp", "Crisp" };
	static const char *const transfer[] = { "DMA", "CPU" };
	static const char *const frameskip[] = { "Auto", "Manual", "Off" };
	fxcg100_menu_state *st = ui->state;
	uint32_t landscape = (uint32_t)cp_view_is_landscape(st->screen_scale);
	uint32_t size = cp_view_size(st->screen_scale);
	/* 0 portrait; 1 the calculator turned clockwise, which puts the keypad
	 * under the left hand; 2 turned counter-clockwise. */
	uint32_t held = landscape ? 1u + (st->rotation == CP_ROTATION_KEYS_RIGHT)
		: 0u;
	int changed = 0, y, top;
	cp_clock clock;
	char text[40];

	if(ui_bar(ui, "Settings", ICON_BACK, ICON_NONE) == BAR_BACK ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		leave_settings(ui);
		return FXCG100_MENU_CONTINUE;
	}

	top = ui->bar_h - ui_scroll_begin(ui);
	y = top;

	section_label(ui, &y, "Display");
	card_begin(ui, y);
	if(ui_segments(ui, setting_row(ui, &y, "Device rotation"), turn, turn, 3,
			&held)) {
		ui_start_upright = 0;      /* the player has chosen */
		if((held != 0) != (landscape != 0)) {
			size = cp_view_size(cp_view_turned(st->screen_scale));
			landscape = held != 0;
		}
		if(landscape)
			st->rotation = held == 2 ? CP_ROTATION_KEYS_RIGHT
				: CP_ROTATION_KEYS_LEFT;
		changed = 1;
	}
	if(ui_segments(ui, setting_row(ui, &y, "Picture size"),
			landscape ? size_landscape : size_portrait,
			landscape ? size_landscape : size_portrait,
			(int)cp_view_size_count((int)landscape), &size))
		changed = 1;
	st->screen_scale = cp_view_make((int)landscape, size);
	changed |= ui_segments(ui, setting_row(ui, &y, "Scaling filter"), filter,
		filter, 3, &st->screen_filter);
	changed |= ui_segments(ui, setting_row(ui, &y, "Screen transfer"),
		transfer, transfer, 2, &st->lcd_cpu);
	changed |= row_switch(ui, &y, "Touch keys",
		"Show the on-screen buttons", &st->touch_keys);
	card_end(ui, &y);

	section_label(ui, &y, "Appearance");
	card_begin(ui, y);
	if(st->theme >= CP_THEME_COUNT)
		st->theme = 0;
	changed |= ui_swatches(ui, setting_row_h(ui, &y, "Colour",
		cp_theme_names[st->theme], 96), 4, &st->theme);
	changed |= row_switch(ui, &y, "Animations", NULL, &st->animations);
	card_end(ui, &y);

	section_label(ui, &y, "Speed");
	card_begin(ui, y);
	changed |= ui_segments(ui, setting_row(ui, &y, "Frame skipping"),
		frameskip, frameskip, 3, &st->frameskip_type);
	if(st->frameskip_type == 1)
		changed |= ui_number(ui, setting_row(ui, &y, "Frames to skip"),
			&st->frameskip_value, 9);
	changed |= row_switch(ui, &y, "Limit to 60 fps",
		"Never run faster than a real GBA", &st->frame_limit);
	changed |= row_switch(ui, &y, "Show frame rate", NULL, &st->show_fps);
	fxcg100_clock_read(&clock);
	megahertz(text, sizeof(text), "CPU", cp_clock_hz(&clock, CP_CLK_CPU));
	if(row_nav(ui, &y, "Clock speed", text))
		ui_goto(ui, SCREEN_CLOCK);
	card_end(ui, &y);

	section_label(ui, &y, "Keys");
	card_begin(ui, y);
	if(row_nav(ui, &y, "Key mapping", "Buttons and shortcuts"))
		ui_goto(ui, SCREEN_CONTROLS);
	card_end(ui, &y);

	/* How the touch screen is being read, when it is not the ordinary case
	 * with nothing to report: through gint's driver because its reads could
	 * not be switched off (classpad_touch_poll.h), or with reads that had
	 * to be given up. */
	if(fxcg100_touch_recoveries() || !fxcg100_touch_is_polled()) {
		snprintf(text, sizeof(text), "Touch: %s, %u bus errors",
			fxcg100_touch_is_polled() ? "polled" : "gint driver",
			(unsigned)fxcg100_touch_recoveries());
		cp_text(&ui->s, &cp_font_small, PAD, y + 8, text,
			ui->t.on_surface_variant);
		y += 8 + cp_font_small.height;
	}

	ui_scroll_end(ui, y + 8 - top);
	if(changed) {
		ui->settings_dirty = 1;
		ui->redraw = 1;       /* the layout itself may have changed */
	}
	return FXCG100_MENU_CONTINUE;
}

/* ---- key mapping ---------------------------------------------------------- */

#define BINDING_COUNT 14

static const struct {
	const char *name;
	int hotkey;                /* index into the GBA keymap or hotkey map */
	int index;
} bindings[BINDING_COUNT] = {
	{ "Up", 0, FXCG100_GBA_KEY_UP },       { "Down", 0, FXCG100_GBA_KEY_DOWN },
	{ "Left", 0, FXCG100_GBA_KEY_LEFT },   { "Right", 0, FXCG100_GBA_KEY_RIGHT },
	{ "A", 0, FXCG100_GBA_KEY_A },         { "B", 0, FXCG100_GBA_KEY_B },
	{ "L", 0, FXCG100_GBA_KEY_L },         { "R", 0, FXCG100_GBA_KEY_R },
	{ "Start", 0, FXCG100_GBA_KEY_START },
	{ "Select", 0, FXCG100_GBA_KEY_SELECT },
	{ "Fast forward", 1, FXCG100_HOTKEY_FAST_FORWARD },
	{ "Save state", 1, FXCG100_HOTKEY_SAVE_STATE },
	{ "Load state", 1, FXCG100_HOTKEY_LOAD_STATE },
	{ "Show frame rate", 1, FXCG100_HOTKEY_DISPLAY_FPS },
};

static uint16_t *binding_slot(cp_ui *ui, int i)
{
	return bindings[i].hotkey ? &ui->state->hotkey_map[bindings[i].index]
		: &ui->state->keymap[bindings[i].index];
}

/* The binding other than `except` that uses a key, or -1. */
static int key_user(cp_ui *ui, int except, uint16_t key)
{
	int i;

	for(i = 0; i < BINDING_COUNT; i++)
		if(i != except && *binding_slot(ui, i) == key)
			return i;
	return -1;
}

/* Waiting for the key that a function should use. */
static fxcg100_menu_result modal_capture(cp_ui *ui)
{
	static char moved[48];
	cp_rect box = dialog_box(ui, 196), cancel, none;
	uint16_t key = fxcg100_poll_physical_key();
	char text[64];

	snprintf(text, sizeof(text), "It will be used for %s.",
		bindings[ui->capture].name);
	dialog_text(ui, box, "Press a key", text);
	cp_text_fit(&ui->s, &cp_font_small, box.x + 24, box.y + 82, box.w - 48,
		"The Menu icon always opens the menu.", ui->t.on_surface_variant);

	if(!ui->capture_armed) {
		/* Ignore whatever was still held when the page opened. */
		ui->capture_armed = key == 0;
	} else if(key && fxcg100_key_bindable(key)) {
		/* A key that something else uses is taken from it, and the player
		 * is told what was left without one. */
		int other = key_user(ui, ui->capture, key);

		if(other >= 0) {
			*binding_slot(ui, other) = 0;
			snprintf(moved, sizeof(moved), "%s now has no key",
				bindings[other].name);
			ui_snack(ui, moved);
		}
		*binding_slot(ui, ui->capture) = key;
		ui->settings_dirty = 1;
		ui->capture = -1;
	}

	/* Anything may be left without a key: the GBA buttons are on the touch
	 * screen as well. */
	dialog_buttons(box, "No key", "Cancel", &none, &cancel);
	if(ui_button(ui, none, "No key", BTN_TEXT) && ui->capture >= 0) {
		*binding_slot(ui, ui->capture) = 0;
		ui->settings_dirty = 1;
		ui->capture = -1;
	}
	if(ui_button(ui, cancel, "Cancel", BTN_TEXT))
		ui->capture = -1;
	/* Keys are polled, not event-driven: keep stepping while this is up. */
	ui->redraw = 1;
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result screen_controls(cp_ui *ui)
{
	int y, top, i;

	if(ui_bar(ui, "Key mapping", ICON_BACK, ICON_NONE) == BAR_BACK ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		ui_goto(ui, SCREEN_SETTINGS);
		return FXCG100_MENU_CONTINUE;
	}

	top = ui->bar_h - ui_scroll_begin(ui);
	y = top;
	for(i = 0; i < BINDING_COUNT; i++) {
		uint16_t key = *binding_slot(ui, i);
		const char *label = key ? fxcg100_key_label(key) : "None";
		cp_rect r, chip;
		int flags, id;

		if(i == 0 || i == 10) {
			if(i)
				card_end(ui, &y);
			section_label(ui, &y, i ? "Shortcuts" : "Game Boy Advance buttons");
			card_begin(ui, y);
		}
		r = row_begin(ui, &y, 56);
		flags = ui_widget(ui, r);
		id = ui->last_id;
		chip = (cp_rect){ (int16_t)(r.x + r.w - PAD - 124), (int16_t)(r.y + 10),
			124, 36 };
		row_text(ui, r, r.w - 2 * PAD - 136, bindings[i].name, NULL);
		cp_round_rect(&ui->s, chip.x, chip.y, chip.w, chip.h, 10,
			ui->t.secondary_container);
		cp_text_centered(&ui->s, &cp_font_body, chip.x, chip.y, chip.w, chip.h,
			label, key ? ui->t.on_secondary_container
			: ui->t.on_surface_variant);
		row_feedback(ui, r, flags, id);
		ui_mark(bindings[i].name, r);
		if(flags & HIT_CLICK) {
			ui->capture = i;
			ui->capture_armed = 0;
			ui->redraw = 1;
		}
	}
	card_end(ui, &y);
	{
		cp_rect r = { MARGIN + 4, (int16_t)(y + 12),
			(int16_t)(ui->w - 2 * MARGIN - 8), 48 };

		if(ui_button(ui, r, "Reset to defaults", BTN_OUTLINED)) {
			fxcg100_keymap_defaults(ui->state->keymap);
			fxcg100_hotkey_defaults(ui->state->hotkey_map);
			ui->settings_dirty = 1;
			ui_snack(ui, "Default keys restored");
		}
		y += 76;
	}
	ui_scroll_end(ui, y - top);
	return FXCG100_MENU_CONTINUE;
}

/* ---- clock speed ---------------------------------------------------------- */

/* The setting the calculator had when gpSP started: what "Stock" restores
 * and what the saved setting is tied to. */
static cp_clock clock_stock;
static int clock_stock_known;
/* The processor speed measured on the stock setting (fxcg100_measure_speed),
 * zero until taken; and the speed of the running setting worked out from
 * it, in Hz, zero if not known. */
static uint32_t clock_stock_rounds, clock_measured_hz;

/* Measure the running setting against the stock one. The stock figure can
 * only be taken while the calculator is on the stock setting. */
static void clock_measure(const cp_clock *running)
{
	uint32_t rounds = fxcg100_measure_speed();

	clock_measured_hz = 0;
	if(!rounds)
		return;
	if(cp_clock_equal(running, &clock_stock))
		clock_stock_rounds = rounds;
	if(clock_stock_rounds)
		clock_measured_hz = (uint32_t)((uint64_t)cp_clock_hz(&clock_stock,
			CP_CLK_CPU) * rounds / clock_stock_rounds);
}

static const uint16_t clock_file_path[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'G', 'P', 'S', 'P', 'O', 'C', '.', 'C', 'F', 'G', 0
};

static void clock_capture_stock(void)
{
	if(!clock_stock_known) {
		fxcg100_clock_read(&clock_stock);
		clock_stock_known = 1;
		/* The speed on the stock setting, taken now while the calculator
		 * is certainly on it: everything measured later is compared with
		 * this. */
		clock_measure(&clock_stock);
	}
}

/* Move the hardware to a setting: slowest memory waits first, then the
 * clock, then the waits that go with it (see cp_clock_apply_step). */
static void clock_apply(const cp_clock *to)
{
	cp_clock from, step;
	int i;

	fxcg100_clock_read(&from);
	if(cp_clock_equal(&from, to))
		return;
	for(i = 0; i < 3; i++) {
		step = cp_clock_apply_step(&from, to, i);
		fxcg100_clock_write(&step);
	}
}

static int clock_file_load(cp_clock_file *file)
{
	return fxcg100_storage_read_blob(clock_file_path, file, sizeof(*file)) &&
		cp_clock_file_valid(file);
}

static int clock_file_save(const cp_clock *clock, uint32_t at_startup,
	uint32_t unlocked)
{
	cp_clock_file file;

	memset(&file, 0, sizeof(file));
	file.magic = CP_CLOCK_FILE_MAGIC;
	file.at_startup = at_startup;
	file.unlocked = unlocked;
	file.clock = *clock;
	file.stock_frqcr = clock_stock.FRQCR;
	file.check = cp_clock_file_sum(&file);
	return fxcg100_storage_write_blob(clock_file_path, &file, sizeof(file));
}

void cp_ui_apply_saved_clock(void)
{
	cp_clock_file file;

	clock_capture_stock();
	/* Holding the menu key while gpSP starts skips a saved setting, in case
	 * one was saved that the calculator cannot run. */
	if(fxcg100_poll_app_keys() & FXCG100_APPKEY_ON)
		return;
	/* Only on top of the state it was made on: not, for instance, when
	 * another tool has already changed the clock. */
	if(clock_file_load(&file) && file.at_startup &&
			file.stock_frqcr == clock_stock.FRQCR)
		clock_apply(&file.clock);
}

static void clock_page_open(cp_ui *ui)
{
	cp_clock_file file;

	clock_capture_stock();
	fxcg100_clock_read(&ui->clock_running);
	clock_measure(&ui->clock_running);
	ui->clock_edit = ui->clock_running;
	cp_chain_decode(&ui->clock_edit, &ui->clock_chain);
	ui->clock_at_startup = 0;
	ui->clock_unlocked = 0;
	if(clock_file_load(&file)) {
		ui->clock_at_startup = file.at_startup;
		ui->clock_unlocked = file.unlocked;
	}
	ui->clock_tested = NULL;
	ui->clock_unlock_ask = 0;
	ui->clock_pending = 0;
	ui->clock_loaded = 1;
}

/* "Keep this speed?" with a countdown: if the new setting broke touch
 * or the keys, doing nothing brings the old one back. */
static fxcg100_menu_result modal_clock(cp_ui *ui)
{
	const uint32_t total = 10u * TICKS_PER_SECOND;
	cp_rect box = dialog_box(ui, 184), revert, keep;
	uint32_t left = ui->clock_deadline - fxcg100_ticks();
	char text[48];

	if(left > 60u * TICKS_PER_SECOND)  /* deadline passed (or the day rolled) */
		left = 0;
	snprintf(text, sizeof(text), "Going back in %u s",
		(unsigned)((left + TICKS_PER_SECOND - 1u) / TICKS_PER_SECOND));
	dialog_text(ui, box, "Keep this speed?", text);
	/* The time left, as a bar that runs down. */
	cp_round_rect(&ui->s, box.x + 24, box.y + 90, box.w - 48, 4, 2,
		ui->t.secondary_container);
	cp_round_rect(&ui->s, box.x + 24, box.y + 90,
		(int)((uint32_t)(box.w - 48) * (left > total ? total : left) / total),
		4, 2, ui->t.primary);

	dialog_buttons(box, "Go back", "Keep", &revert, &keep);
	if(ui_button(ui, revert, "Go back", BTN_TEXT) || left == 0 ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		clock_apply(&ui->clock_backup);
		ui->clock_running = ui->clock_backup;
		ui->clock_pending = 0;
		clock_measure(&ui->clock_running);
		ui_snack(ui, "Previous clock speed restored");
	} else if(ui_button(ui, keep, "Keep", BTN_FILLED)) {
		ui->clock_running = ui->clock_edit;
		ui->clock_pending = 0;
		clock_measure(&ui->clock_running);
		ui_snack(ui, clock_file_save(&ui->clock_running, ui->clock_at_startup,
			ui->clock_unlocked)
			? "Clock speed kept" : "Kept, but could not be saved");
	}
	/* The countdown has to keep running without input. */
	ui->redraw = 1;
	return FXCG100_MENU_CONTINUE;
}

/* The limits in force: Xtune's defaults, or its outer ones when the player
 * has switched the limits off. */
static const uint32_t *clock_limits(const cp_ui *ui)
{
	return ui->clock_unlocked ? cp_clock_hard_limit : cp_clock_limit;
}

/* Before the limits are switched off. */
static fxcg100_menu_result modal_unlock(cp_ui *ui)
{
	cp_rect box = dialog_box(ui, 216), cancel, ok;

	dialog_text(ui, box, "Switch the limits off?",
		"Nothing past them is tested.");
	cp_text_fit(&ui->s, &cp_font_small, box.x + 24, box.y + 84, box.w - 48,
		"A setting the calculator cannot run", ui->t.on_surface_variant);
	cp_text_fit(&ui->s, &cp_font_small, box.x + 24, box.y + 106, box.w - 48,
		"freezes it until it is reset.", ui->t.on_surface_variant);
	dialog_buttons(box, "Cancel", "Switch off", &cancel, &ok);
	if(ui_button(ui, cancel, "Cancel", BTN_TEXT) ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		ui->clock_unlock_ask = 0;
		ui->focus_shown = 0;
		ui->redraw = 1;
	} else if(ui_button(ui, ok, "Switch off", BTN_FILLED)) {
		ui->clock_unlock_ask = 0;
		ui->clock_unlocked = 1;
		clock_file_save(&ui->clock_running, ui->clock_at_startup, 1);
		ui->focus_shown = 0;
		ui->redraw = 1;
	}
	return FXCG100_MENU_CONTINUE;
}

/* A row that only reports something. */
static void row_info(cp_ui *ui, int *y, const char *label, const char *value)
{
	cp_rect r = row_begin(ui, y, 48);

	cp_text(&ui->s, &cp_font_body, r.x + PAD,
		r.y + (r.h - cp_font_body.height) / 2, label, ui->t.on_surface);
	cp_text(&ui->s, &cp_font_small,
		r.x + r.w - PAD - cp_text_width(&cp_font_small, value),
		r.y + (r.h - cp_font_small.height) / 2, value,
		ui->t.on_surface_variant);
}

/* Take `steps` steps of a row of the chain in a direction, as far as they
 * go, and work out the setting again. Says so if a limit stopped it. */
static int clock_chain_move(cp_ui *ui, int row, int dir, int steps)
{
	static char text[48];
	int moved = 0, result = 0;

	while(steps-- > 0) {
		result = cp_chain_step(&ui->clock_chain, row, dir, 0,
			clock_limits(ui));
		if(result != 0)
			break;
		moved = 1;
	}
	if(result > 0) {
		snprintf(text, sizeof(text), "%s limit is %u MHz",
			cp_clock_name[result],
			(unsigned)(clock_limits(ui)[result] / 1000000u));
		ui_snack(ui, text);
	}
	if(moved)
		ui->clock_edit = cp_clock_for_chain(&clock_stock, &ui->clock_chain);
	return moved || result > 0;
}

static fxcg100_menu_result screen_clock(cp_ui *ui)
{
	static const char *const dividers[] = { "/2", "/4", "/8", "/16", "/32",
		"/64" };
	static const struct { const char *label; int row; } div_rows[4] = {
		{ "CPU divider", CP_CLK_CPU },
		{ "SuperHyway divider", CP_CLK_SH },
		{ "Bus divider", CP_CLK_BUS },
		{ "Peripheral divider", CP_CLK_PERIPH },
	};
	cp_clock *e = &ui->clock_edit;
	cp_chain *ch = &ui->clock_chain;
	const char *problem, *status;
	uint16_t status_ink;
	cp_rect a, b;
	uint32_t v, old, low, high, periph;
	int y, top, i, changed = 0, tested, card_h;
	char text[40];

	if(!ui->clock_loaded)
		clock_page_open(ui);
	if(ui_bar(ui, "Clock speed", ICON_BACK, ICON_NONE) == BAR_BACK ||
			(ui->key_edge & FXCG100_APPKEY_BACK)) {
		ui->clock_loaded = 0;
		ui_goto(ui, SCREEN_SETTINGS);
		return FXCG100_MENU_CONTINUE;
	}

	top = ui->bar_h - ui_scroll_begin(ui);
	y = top + 4;

	/* What the chain below adds up to. The stock setting is always allowed,
	 * whatever the rules say of it. */
	problem = cp_clock_equal(e, &clock_stock) ? NULL
		: cp_clock_problem(e, clock_limits(ui));
	status = "Not applied yet";
	status_ink = ui->t.on_surface_variant;
	if(problem) {
		status = problem;
		status_ink = ui->t.error;
	} else if(cp_clock_hz(e, CP_CLK_BUS) > CP_CLK_BUS_MEASURED) {
		/* Only reachable with the limits off. */
		status = "Bus above 130 MHz";
		status_ink = ui->t.error;
	} else if(cp_clock_equal(e, &ui->clock_running)) {
		/* With what the processor really does, measured against a timer
		 * that counts the 32 kHz crystal: the figures above are only what
		 * the registers say. */
		static char in_use[40];

		status = clock_stock_rounds ? "In use now"
			: "In use now (speed not measurable)";
		status_ink = ui->t.primary;
		if(clock_measured_hz) {
			uint32_t nominal = cp_clock_hz(e, CP_CLK_CPU);
			uint32_t off = clock_measured_hz > nominal
				? clock_measured_hz - nominal : nominal - clock_measured_hz;

			megahertz(in_use, sizeof(in_use), "In use, measured",
				clock_measured_hz);
			status = in_use;
			if(off > nominal / 10u)       /* more than a tenth out */
				status_ink = ui->t.error;
		}
	}
	/* The result of the last Test has a line of its own, whatever else the
	 * card says, for as long as the page shows the setting it was for. */
	tested = ui->clock_tested && cp_clock_equal(e, &ui->clock_tested_on);
	card_h = tested ? 128 : 104;
	cp_round_rect(&ui->s, MARGIN, y, ui->w - 2 * MARGIN, card_h, CARD_RADIUS,
		ui->t.container);
	megahertz(text, sizeof(text), "CPU", cp_clock_hz(e, CP_CLK_CPU));
	cp_text(&ui->s, &cp_font_title, MARGIN + PAD, y + 12, text,
		ui->t.on_surface);
	megahertz(text, sizeof(text), "Bus", cp_clock_hz(e, CP_CLK_BUS));
	i = cp_text(&ui->s, &cp_font_small, MARGIN + PAD, y + 48, text,
		ui->t.on_surface_variant);
	megahertz(text, sizeof(text), "Peripherals", cp_clock_hz(e, CP_CLK_PERIPH));
	cp_text(&ui->s, &cp_font_small, i + 14, y + 48, text,
		ui->t.on_surface_variant);
	cp_text_fit(&ui->s, &cp_font_small, MARGIN + PAD, y + 72,
		ui->w - 2 * MARGIN - 2 * PAD, status, status_ink);
	if(tested)
		cp_text_fit(&ui->s, &cp_font_small, MARGIN + PAD, y + 96,
			ui->w - 2 * MARGIN - 2 * PAD, ui->clock_tested,
			ui->clock_test_failed ? ui->t.error : ui->t.primary);
	y += card_h + 12;

	/* Try it, or use it. */
	{
		int test, apply;

		button_pair(ui, y, 48, &a, &b);
		test = ui_button(ui, a, "Test", BTN_TONAL);
		apply = ui_button(ui, b, "Apply", BTN_FILLED);

		if((test || apply) && problem) {
			ui_snack(ui, problem);
			changed = 1;
		} else if(test) {
			/* Run on the new setting for a few seconds of memory, flash and
			 * processor checks (classpad_platform.c), then return. */
			uint32_t before, after;
			int ram;

			cp_ui_status(ui->state, "Testing this clock speed",
				"A few seconds. Reset if it stops.");
			before = fxcg100_stress_signature();
			clock_apply(e);
			ram = fxcg100_stress_ram();
			after = fxcg100_stress_signature();
			clock_apply(&ui->clock_running);
			/* The result stays in the card at the top, for as long as the
			 * page shows the setting that was tested. */
			ui->clock_tested = ram ? "Test FAILED: memory"
				: after != before ? "Test FAILED: flash or processor"
				: "Test passed";
			ui->clock_test_failed = ram || after != before;
			ui->clock_tested_on = *e;
			ui_snack(ui, ui->clock_tested);
			changed = 1;
		} else if(apply && !cp_clock_equal(e, &ui->clock_running)) {
			ui->clock_backup = ui->clock_running;
			clock_apply(e);
			ui->clock_pending = 1;
			ui->clock_deadline = fxcg100_ticks() + 10u * TICKS_PER_SECOND;
			ui->focus_shown = 0;
			changed = 1;
		}
		y += 48;
	}

	/* The chain: the only part that is set by hand. A divider moves its
	 * neighbours when the chain would otherwise not be legal. */
	section_label(ui, &y, "Clock");
	card_begin(ui, y);
	old = v = ch->pll;
	snprintf(text, sizeof(text), "x%u", (unsigned)v);
	if(ui_stepper(ui, setting_row(ui, &y, "PLL multiplier"), "PLL multiplier",
			1, &v, 1, CP_CLK_PLL_MAX, 1, text))
		changed |= clock_chain_move(ui, CP_CLK_PLL, v > old ? 1 : -1,
			(int)(v > old ? v - old : old - v));
	old = v = ch->fll;
	snprintf(text, sizeof(text), "x%u", (unsigned)v);
	if(ui_stepper(ui, setting_row(ui, &y, "FLL multiplier"), "FLL multiplier",
			10, &v, CP_CLK_FLL_MIN, CP_CLK_FLL_MAX, 10, text))
		changed |= clock_chain_move(ui, CP_CLK_FLL, v > old ? 1 : -1,
			(int)(v > old ? v - old : old - v));
	for(i = 0; i < 4; i++) {
		old = v = cp_chain_div_code(ch->div[i]);
		if(ui_segments(ui, setting_row(ui, &y, div_rows[i].label),
				div_rows[i].label, dividers, 6, &v))
			/* a smaller divider is a faster clock */
			changed |= clock_chain_move(ui, div_rows[i].row,
				v < old ? 1 : -1, (int)(v < old ? old - v : v - old));
	}
	card_end(ui, &y);

	/* What follows from it. */
	section_label(ui, &y, "Set automatically");
	card_begin(ui, y);
	snprintf(text, sizeof(text), "%u cycles (stock %u)",
		cp_clock_wait_cycles(cp_clock_flash_wait(e)),
		cp_clock_wait_cycles(cp_clock_flash_wait(&clock_stock)));
	row_info(ui, &y, "Flash read wait", text);
	snprintf(text, sizeof(text), "%u cycles (stock %u)",
		cp_clock_trc_cycles(cp_clock_trc(e)),
		cp_clock_trc_cycles(cp_clock_trc(&clock_stock)));
	row_info(ui, &y, "SDRAM TRC", text);
	periph = cp_clock_hz(e, CP_CLK_PERIPH);
	cp_clock_i2c_counts(periph, &low, &high);
	snprintf(text, sizeof(text), "%u kHz", (unsigned)(periph / (low + high) /
		1000u));
	row_info(ui, &y, "Touch bus", text);
	card_end(ui, &y);

	section_label(ui, &y, "Use");
	card_begin(ui, y);
	v = ui->clock_at_startup;
	if(row_switch(ui, &y, "Apply when gpSP starts",
			"Hold Clear at start to skip", &v)) {
		ui->clock_at_startup = v;
		ui_snack(ui, clock_file_save(&ui->clock_running, v,
			ui->clock_unlocked)
			? (v ? "Will be applied at start-up" : "Start-up setting off")
			: "Could not save");
		changed = 1;
	}
	/* Shown as "Limits: on"; switching them off asks first. */
	v = !ui->clock_unlocked;
	if(row_switch(ui, &y, "Limits", "Keep to the tested range", &v)) {
		if(v) {
			ui->clock_unlocked = 0;
			clock_file_save(&ui->clock_running, ui->clock_at_startup, 0);
		} else {
			ui->clock_unlock_ask = 1;
			ui->focus_shown = 0;
		}
		changed = 1;
	}
	card_end(ui, &y);
	{
		cp_rect r = { MARGIN + 4, (int16_t)(y + 12),
			(int16_t)(ui->w - 2 * MARGIN - 8), 48 };

		if(ui_button(ui, r, "Restore stock", BTN_OUTLINED)) {
			*e = clock_stock;
			cp_chain_decode(e, ch);
			changed = 1;
		}
		y += 76;
	}

	ui_scroll_end(ui, y - top);
	if(changed)
		ui->redraw = 1;
	return FXCG100_MENU_CONTINUE;
}

/* ---- stepping ------------------------------------------------------------- */

/* A short message along the bottom: it slides up, stays a couple of seconds
 * and slides away. */
static void ui_snackbar(cp_ui *ui)
{
	int w, x, drop;

	if(!ui->toast)
		return;
	if(ui->toast_leaving) {
		int p = ui_progress(ui, ui->toast_out_t0, T_SNACK_OUT);

		if(p >= 256) {
			ui->toast = NULL;
			return;
		}
		drop = p * 72 / 256;
	} else {
		drop = (256 - ease_out(ui_progress(ui, ui->toast_t0, T_SNACK_IN))) *
			72 / 256;
	}
	w = cp_text_width(&cp_font_body, ui->toast) + 2 * PAD;
	if(w > ui->w - 2 * MARGIN)
		w = ui->w - 2 * MARGIN;
	x = (ui->w - w) / 2;
	cp_round_rect(&ui->s, x, ui->h - 60 + drop, w, 48, 12, ui->t.on_surface);
	cp_text_fit(&ui->s, &cp_font_body, x + PAD,
		ui->h - 60 + drop + (48 - cp_font_body.height) / 2, w - 2 * PAD,
		ui->toast, ui->t.surface);
}

static int ui_modal(const cp_ui *ui)
{
	if(ui->screen == SCREEN_PAUSE && ui->confirm != CONFIRM_NONE)
		return MODAL_CONFIRM;
	if(ui->screen == SCREEN_CONTROLS && ui->capture >= 0)
		return MODAL_CAPTURE;
	if(ui->screen == SCREEN_CLOCK && ui->clock_pending)
		return MODAL_CLOCK;
	if(ui->screen == SCREEN_CLOCK && ui->clock_unlock_ask)
		return MODAL_UNLOCK;
	return MODAL_NONE;
}

/* Shortcuts on the icon strip that work on every screen. */
static void ui_strip_shortcuts(cp_ui *ui)
{
	if(ui->strip_edge & FXCG100_STRIP_ROTATE)
		cp_rotate_next(&ui->state->screen_scale, &ui->state->rotation);
	if(ui->strip_edge & FXCG100_STRIP_RESIZE)
		ui->state->screen_scale = cp_view_resized(ui->state->screen_scale);
	if(ui->strip_edge & FXCG100_STRIP_SWAP)
		ui->state->rotation ^= 1u;
	if(ui->strip_edge & (FXCG100_STRIP_ROTATE | FXCG100_STRIP_RESIZE |
			FXCG100_STRIP_SWAP)) {
		ui->settings_dirty = 1;
		ui_start_upright = 0;      /* the player has chosen */
	}
	if(ui_modal(ui) != MODAL_NONE)
		return;
	if((ui->strip_edge & FXCG100_STRIP_SETTINGS) &&
			ui->screen != SCREEN_SETTINGS)
		ui_goto(ui, SCREEN_SETTINGS);
	if((ui->strip_edge & FXCG100_STRIP_MENU) && ui->screen != SCREEN_LIBRARY) {
		if(ui->screen == SCREEN_SETTINGS || ui->screen == SCREEN_CONTROLS)
			leave_settings(ui);
		ui_goto(ui, SCREEN_LIBRARY);
	}
}

void cp_ui_begin(cp_ui *ui, fxcg100_menu_state *state, int in_game)
{
	memset(ui, 0, sizeof(*ui));
	ui->state = state;
	ui->in_game = in_game;
	ui->screen = in_game ? SCREEN_PAUSE : SCREEN_LIBRARY;
	ui->capture = -1;
	ui->redraw = 1;
	if(in_game)
		ui_start_upright = 0;

	/* The menu fades in from black, which is what was behind it. */
	ui->real = fxcg100_ticks();
	ui->enter_t0 = ui->now;
	ui->animating = 1;
	ui->entering = 1;
	ui->enter_black = 1;
	ui->theme_shown = state->theme;
	ui->base = *cp_theme_get(state->theme);

	/* Whatever opened the menu is still held: do not let it act here. */
	ui->prev_keys = fxcg100_poll_app_keys();
	ui->prev_strip = fxcg100_touch_strip_icons();
	if(in_game && (ui->prev_strip & FXCG100_STRIP_SETTINGS))
		ui->screen = SCREEN_SETTINGS;
	if(fxcg100_touch_point(&ui->tx, &ui->ty)) {
		ui->touching = 1;
		ui->dragging = 1;
	}
}

/* One round of input and drawing. Returns FXCG100_MENU_CONTINUE until the
 * player chooses something main() must act on. */
fxcg100_menu_result cp_ui_step(cp_ui *ui)
{
	fxcg100_menu_result result;
	uint32_t real = fxcg100_ticks();
	int changed = ui_input(ui);
	int moving = ui->animating, expired, modal;
	ui_muted muted = { 0 };
	cp_theme shown;

	/* A message goes away by itself after a while, or at a new key press or
	 * touch once it has been up for half a second. */
	expired = ui->toast && !ui->toast_leaving &&
		(real - ui->toast_real >= T_SNACK ||
		((ui->key_edge || (ui->touching && !ui->dragging &&
		ui->tx == ui->press_x && ui->ty == ui->press_y &&
		ui->drag_dy == 0 && ui->touch_began)) &&
		real - ui->toast_real >= TICKS_PER_SECOND / 2u));
	if(!changed && !ui->redraw && !expired)
		return FXCG100_MENU_CONTINUE;

	/* Interface time. It follows the tick count, but while something is
	 * moving it advances by at least a frame's worth at every step, so an
	 * animation always ends even if the count were to stand still. */
	ui->dt = (int)(real - ui->real);
	if(ui->dt < 0 || ui->dt > 32)
		ui->dt = 32;
	if(moving && ui->dt < 3)
		ui->dt = 3;
	ui->real = real;
	ui->now += (uint32_t)ui->dt;
	ui->motion = ui->state->animations != 0;
	ui->redraw = 0;
	ui->animating = 0;
	if(expired) {
		ui->toast_leaving = 1;
		ui->toast_out_t0 = ui->now;
	}

	ui_strip_shortcuts(ui);
	ui_refresh_orientation(ui);
	ui->slide = ui_resolve_theme(ui);
	ui_anim_collect(ui);
	ui_marks_reset();
	ui->widgets = 0;
	ui->touch_seq = 0;

	/* Under a dialog the page is still drawn, darkened and deaf. */
	modal = ui_modal(ui);
	if(modal != ui->modal) {
		ui->modal = modal;
		ui->modal_t0 = ui->now;
		ui->ripple_live = 0;
	}
	ui->dim = ui_approach(ui, ui->dim, modal ? SCRIM : 0, 14);
	shown = ui->t;
	if(ui->dim) {
		cp_theme dark;

		ui->tint_dim = ui->dim;
		cp_theme_toward(&dark, &shown, 0x0000, ui->dim);
		ui_use(ui, &dark);
	}
	if(modal)
		ui_mute(ui, &muted);
	cp_fill(&ui->s, 0, 0, ui->w, ui->h, ui->t.surface);

	switch(ui->screen) {
	case SCREEN_PAUSE:    result = screen_pause(ui); break;
	case SCREEN_SETTINGS: result = screen_settings(ui); break;
	case SCREEN_CONTROLS: result = screen_controls(ui); break;
	case SCREEN_CLOCK:    result = screen_clock(ui); break;
	default:              result = screen_library(ui); break;
	}
	cp_surface_unclip(&ui->s);
	ui->s.ox = 0;
	ui->tint_dim = 0;
	ui_use(ui, &shown);

	if(modal) {
		ui_unmute(ui, &muted);
		ui_marks_reset();
		ui->widgets = 0;
		switch(modal) {
		case MODAL_CONFIRM: result = modal_confirm(ui); break;
		case MODAL_CAPTURE: result = modal_capture(ui); break;
		case MODAL_UNLOCK:  result = modal_unlock(ui); break;
		default:            result = modal_clock(ui); break;
		}
		ui->s.oy = 0;
	}
	ui->widgets_last = ui->widgets;
	if(result != FXCG100_MENU_CONTINUE)
		return result;

	ui_snackbar(ui);
	fxcg100_lcd_present_frame();
	if(ui->animating)
		ui->redraw = 1;
	return FXCG100_MENU_CONTINUE;
}

/* A message while something slow happens (loading a game, saving a state),
 * in the same look as the rest of the interface. */
void cp_ui_status(const fxcg100_menu_state *state, const char *line1,
	const char *line2)
{
	cp_ui ui;
	int w, x, y;

	memset(&ui, 0, sizeof(ui));
	ui.state = (fxcg100_menu_state *)state;
	ui_refresh_orientation(&ui);
	ui_use(&ui, cp_theme_get(state->theme));
	w = ui.w - 48 > 312 ? 312 : ui.w - 48;
	x = (ui.w - w) / 2;
	y = (ui.h - 104) / 2;
	cp_fill(&ui.s, 0, 0, ui.w, ui.h, ui.t.surface);
	cp_round_rect(&ui.s, x, y, w, 104, 28, ui.t.container_high);
	cp_text_centered(&ui.s, &cp_font_body, x, y + (line2 ? 22 : 40), w, 24,
		line1 ? line1 : "", ui.t.on_surface);
	if(line2) {
		int tw = cp_text_width(&cp_font_small, line2);

		cp_text_fit(&ui.s, &cp_font_small, tw > w - 40 ? x + 20 :
			x + (w - tw) / 2, y + 56, w - 40, line2,
			ui.t.on_surface_variant);
	}
	fxcg100_lcd_present_frame();
}

/* ---- the fxcg100_menu contract -------------------------------------------- */

void fxcg100_menu_init(fxcg100_menu_state *state)
{
	memset(state, 0, sizeof(*state));
	state->screen_scale = CP_VIEW_PORTRAIT_43;
	state->touch_keys = 1;
	state->animations = 1;
	state->frameskip_type = 1;
	state->frameskip_value = 1;
	state->frame_limit = 1;
	fxcg100_keymap_defaults(state->keymap);
	fxcg100_hotkey_defaults(state->hotkey_map);
	state->random_lfsr = 0x00c6ba5eu;
	fxcg100_config_load(state);
	if(state->screen_scale >= CP_VIEW_COUNT)
		state->screen_scale = CP_VIEW_PORTRAIT_43;
	ui_start_upright = 1;
	clock_capture_stock();
}

/* main() calls this with frame 0 before any game is loaded, and with the
 * running frame number when the menu is opened during play. */
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
	uint32_t frame, uint32_t last_hash, const fxcg100_debug_info *debug)
{
	static cp_ui ui;
	fxcg100_menu_result result;

	(void)last_hash;
	(void)debug;
	cp_ui_begin(&ui, state, frame != 0);
	for(;;) {
		result = cp_ui_step(&ui);
		if(result != FXCG100_MENU_CONTINUE)
			break;
		if(!ui.redraw)
			for(volatile unsigned i = 0; i < 12000; i++)
				;
	}
	if(ui.settings_dirty)
		fxcg100_config_save(state);
	return result;
}

int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame)
{
	uint32_t period;

	if(state->frameskip_type == 2 || state->frameskip_value == 0)
		return 1;
	period = state->frameskip_value + 1;
	if(state->frameskip_variation) {
		state->random_lfsr = state->random_lfsr * 1664525u + 1013904223u;
		return (state->random_lfsr % period) == 0;
	}
	return (frame % period) == 1;
}
