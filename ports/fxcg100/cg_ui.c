/*
 * fx-CG50 / fx-CG100 interface: the game library, the pause menu, settings
 * and the key-mapping page. This replaces fxcg100_menu.c in the gint builds
 * and keeps its contract (fxcg100_menu_init / fxcg100_menu_run /
 * fxcg100_menu_should_blit), so main() is shared with the other models.
 *
 * It is made for a keypad and a small screen, so it is a calculator list
 * rather than the ClassPad II's Material 3 cards: one list a page, a
 * full-width bar in the primary colour on the selected row, values at the
 * end of their row (the cursor keys change them, with ‹ › on the selected
 * row), section titles with a hairline, and a footer that names the keys
 * the selected row takes and shows results for two seconds. Colours are the
 * ClassPad II schemes (classpad_theme.h); text is Source Sans one weight
 * heavier than there (cg_font.h). See docs/fxcg-ui.md.
 *
 * Both models have the same 396x224 panel. The layout is 384x216 at (6, 4)
 * of gint's VRAM, where fullscreen gameplay is shown, and every band
 * (header, footer, highlight bar) is painted out to the panel's edges, so
 * whatever of the border is visible matches.
 *
 * Nothing moves. Each key press draws the page into VRAM again; when only
 * the selection moved, just the rows that changed, the header and the
 * footer are sent to the panel.
 *
 * cg_ui_step() is separate from the blocking fxcg100_menu_run() loop so the
 * host test can drive it (tests/fxcg_ui_test.c).
 */

#include "fxcg100_platform.h"
#include "fxcg100_rom_sort.h"
#include "cg_font.h"
#include "classpad_gfx.h"
#include "classpad_theme.h"
#include "txt_reader.h"

#include <gint/display.h>
#include <gint/rtc.h>

#include <stdio.h>
#include <string.h>

/* Developer diagnostics (the Debug info page) are left out of releases.
 * Build with -DCGBA_DEBUG_MENU=1 to show them. */
#ifndef CGBA_DEBUG_MENU
#define CGBA_DEBUG_MENU 0
#endif

/* ---- geometry ------------------------------------------------------------- */

#define SCR_X 6                /* the 384x216 layout in gint's 396x224 VRAM */
#define SCR_Y 4
#define W 384
#define H 216
#define HEAD 22                /* header band */
#define FOOT 18                /* footer band */
#define FY (H - FOOT)
#define ROW 22                 /* list row: eight between header and footer */
#define SEC 20                 /* section title row */
#define VAL_R (W - 22)         /* right edge of every value */
#define VIEW_H (FY - HEAD)
#define LIB_ROWS (VIEW_H / ROW)
#define MAX_ROWS 80

#define TICKS_PER_SECOND 128u  /* rtc_ticks() */
#if defined(CGBA_SAVE_TIMING) || defined(CGBA_KEY_TEST)
#define T_MESSAGE (12u * TICKS_PER_SECOND)  /* time to read the readout */
#else
#define T_MESSAGE (2u * TICKS_PER_SECOND)
#endif
#define T_REPEAT_DELAY 51u     /* a held cursor key repeats after 400 ms */
#define T_REPEAT 10u           /* ... every 80 ms */
#define DIM 141                /* of 256: how far a dialog darkens the page */
#define DIM_LIGHT 100          /* softer scrim for light themes */

#define F_TITLE (&cg_font_title)
#define F_BODY (&cg_font_body)
#define F_SMALL (&cg_font_small)
#define F_KEY (&cg_font_key)

/* Key names as printed on each model. */
#ifdef CGBA_FXCG50
#define KEY_NAME_OK "EXE"
#define KEY_NAME_BACK "EXIT"
#define KEY_NAME_HOME "MENU"
#define KEY_NAME_SETTINGS "OPTN"
#define KEY_NAME_SORT "SHIFT"
#define KEY_NAME_MENU "AC/ON"
#define HINT_SETTINGS_LABEL "Settings"
#define HINT_BACK_LABEL "Back"
#else
#define KEY_NAME_OK "OK"
#define KEY_NAME_BACK "BACK"
#define KEY_NAME_HOME "HOME"
#define KEY_NAME_SETTINGS "SETTINGS"
#define KEY_NAME_SORT "CATALOG"
#define KEY_NAME_MENU "ON"
#define HINT_SETTINGS_LABEL ""
#define HINT_BACK_LABEL ""
#endif

#define CURSOR_KEYS (FXCG100_APPKEY_UP | FXCG100_APPKEY_DOWN | \
	FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT | \
	FXCG100_APPKEY_PGUP | FXCG100_APPKEY_PGDN)

#define TXT_HEAD 18            /* compact reader header band */
#define TXT_BM_VIS_ROWS 6      /* visible rows in the bookmarks dialog */

enum { PAGE_LIBRARY = 0, PAGE_PAUSE, PAGE_SETTINGS, PAGE_KEYS, PAGE_DEBUG,
	PAGE_TXT_BROWSER, PAGE_TXT_READER, PAGE_COUNT };

enum { DIALOG_NONE = 0, DIALOG_EXIT, DIALOG_RESTART, DIALOG_SWITCH,
	DIALOG_SAVE, DIALOG_CAPTURE, DIALOG_TXT_OPTIONS, DIALOG_TXT_BOOKMARKS };

enum { REDRAW_NONE = 0, REDRAW_FOOTER, REDRAW_SELECTION, REDRAW_FULL };

/* Row kinds, and what each row is. */
enum { RK_SECTION = 0, RK_PLAIN, RK_SUB, RK_VALUE, RK_KEY, RK_COLOUR };

enum {
	ID_NONE = 0,
	ID_RESUME, ID_SAVE, ID_LOAD, ID_READER, ID_SETTINGS, ID_GAMES, ID_RESTART,
	ID_EXIT,
	ID_SIZE, ID_FILTER, ID_VSYNC, ID_REFRESH, ID_BACKLIGHT, ID_COLOUR, ID_DARK_MODE,
	ID_FRAMESKIP, ID_SKIP_N, ID_SKIP_PATTERN,
	ID_LIMIT, ID_SHOW_FPS, ID_FF_MODE, ID_FF_SPEED, ID_KEYMAP, ID_DEBUG,
	ID_RESET_KEYS,
	ID_BIND0,                  /* + binding index */
	ID_LINE0 = ID_BIND0 + 20   /* + debug line */
};

typedef struct ui_row {
	uint8_t kind;
	uint8_t id;
	uint8_t error;             /* destructive: the label in the error colour */
	uint8_t accent;            /* the value is a state (Playing), not a choice */
	uint8_t lr;                /* the cursor keys change the value */
	uint8_t rule;              /* a hairline under the row */
	uint16_t key;              /* RK_KEY: the key, 0 for none */
	int16_t y, h;              /* from the top of the list */
	const char *label;
	char value[24];
} ui_row;

typedef struct cg_ui {
	fxcg100_menu_state *state;
	const fxcg100_debug_info *debug;
	int in_game;
	int page, settings_from;
	uint8_t sel[PAGE_COUNT];   /* selected row id of each list page */
	int16_t scroll[PAGE_COUNT];/* first list pixel shown */
	uint32_t game_sel, game_first;
	int txt_browser_sel, txt_browser_first, txt_browser_from;
	int txt_direct, txt_hotkey_armed;
	int txt_opt_jump;
	int txt_bm_sel, txt_bm_first;
	int dialog, dialog_sel;
	int capture, capture_armed;
	uint32_t switch_rom;
	int settings_dirty;
	int resume;                /* the next run picks up where this one was */
	char message[64];
	int message_error;
	uint32_t message_t0;
	const char *progress;
	uint32_t keys, edge;
	int repeat;                /* the cursor-key edge is a held key repeating */
	uint32_t rep_key, rep_t, rep_wait;  /* the cursor key that repeats */
	int redraw;
	cp_theme t;
	cp_surface s;
	ui_row rows[MAX_ROWS];
	int nrows, list_h;
	int sel_y, sel_h;          /* screen band of the highlighted row */
	/* what the panel shows */
	int shown, shown_page, shown_scroll, shown_dialog, shown_theme, shown_dark;
	int shown_sel_y, shown_sel_h;
} cg_ui;

static cg_ui ui_main;
static int open_reader_direct;

/* ---- time ----------------------------------------------------------------- */

/* rtc_ticks() counts from midnight: a count that went back means the day
 * changed, which is taken as long ago. */
static uint32_t ticks_since(uint32_t t0, uint32_t now)
{
	return now >= t0 ? now - t0 : 0xffffu;
}

/* ---- text ----------------------------------------------------------------- */

static int cap_of(const cp_font *f)
{
	return f->ascent - cp_font_glyph(f, 'H')->top;
}

/* Rows under the baseline that descenders reach. */
static int desc_of(const cp_font *f)
{
	static const char probe[] = "gjpqy,;()[]{}|";
	const char *c;
	int d = 0;

	for (c = probe; *c; c++) {
		const cp_glyph *g = cp_font_glyph(f, *c);
		int v = g->top + g->h - f->ascent;

		if (v > d)
			d = v;
	}
	return d;
}

/* Baseline for text in a band y..y+h-1: capitals centred, but never so low
 * that a descender reaches the band's last row, where the next band (a
 * highlight bar, the screen edge) would cut it off. */
static int row_base(const cp_font *f, int y, int h)
{
	int centred = (2 * y + h + cap_of(f) + 1) / 2;
	int lowest = y + h - 1 - desc_of(f);

	return centred < lowest ? centred : lowest;
}

static int text_w(const cp_font *f, const char *s)
{
	return cp_text_width(f, s);
}

static void text(const cg_ui *ui, const cp_font *f, int x, int base,
	const char *s, uint16_t color)
{
	cp_text(&ui->s, f, x, base - f->ascent, s, color);
}

static void text_end(const cg_ui *ui, const cp_font *f, int x, int base,
	const char *s, uint16_t color)
{
	text(ui, f, x - text_w(f, s), base, s, color);
}

/* Baseline that centres capitals on the line between rows cy-1 and cy. */
static int mid_base(const cp_font *f, int cy)
{
	return (2 * cy + cap_of(f) + 1) / 2;
}

static void text_mid(const cg_ui *ui, const cp_font *f, int x, int cy,
	const char *s, uint16_t color)
{
	text(ui, f, x, mid_base(f, cy), s, color);
}

/* Text cut to max_w with "..." when it does not fit. */
static void text_fit(const cg_ui *ui, const cp_font *f, int x, int base,
	int max_w, const char *s, uint16_t color)
{
	int dots = text_w(f, "..."), w = 0, n = 0;

	if (text_w(f, s) <= max_w) {
		text(ui, f, x, base, s, color);
		return;
	}
	while (s[n] && w + cp_font_glyph(f, s[n])->advance + dots <= max_w)
		w += cp_font_glyph(f, s[n++])->advance;
	if (n == 0)
		n = 1;
	while (n > 1 && s[n - 1] == ' ')
		n--;
	x = cp_text_n(&ui->s, f, x, base - f->ascent, s, n, color);
	text(ui, f, x, base, "...", color);
}

/* ---- colours and shapes --------------------------------------------------- */

static uint16_t mix(uint16_t a, uint16_t b, int t)
{
	return cp_blend(a, b, t, 256);
}

static uint16_t hairline(const cg_ui *ui)
{
	return mix(ui->t.outline, ui->t.surface, 141);
}

/* A band across the whole panel, border included. */
static void band(const cg_ui *ui, int y, int h, uint16_t color)
{
	cp_fill(&ui->s, -SCR_X, y, DWIDTH, h, color);
}

static void rect(const cg_ui *ui, int x, int y, int w, int h, int r,
	uint16_t color)
{
	cp_round_rect(&ui->s, x, y, w, h, r, color);
}

enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

/* A small filled triangle pointing `dir`, centred on the line between rows
 * cy-1 and cy; `big` for the ‹ › beside a value, else a keycap arrow. */
static void triangle(const cg_ui *ui, int cx, int cy, int dir, int big,
	uint16_t color)
{
	int a = big ? 3 : 2, b = big ? 4 : 3, c = big ? 2 : 1;

	cy -= 1;
	switch (dir) {
	case DIR_UP:
		cp_triangle(&ui->s, cx, cy - b + 1, cx + b, cy + a, cx - b, cy + a,
			color);
		break;
	case DIR_DOWN:
		cp_triangle(&ui->s, cx, cy + b, cx + b, cy - a + 1, cx - b,
			cy - a + 1, color);
		break;
	case DIR_LEFT:
		cp_triangle(&ui->s, cx - a, cy, cx + c, cy - b, cx + c, cy + b,
			color);
		break;
	default:
		cp_triangle(&ui->s, cx + a, cy, cx - c, cy - b, cx - c, cy + b,
			color);
		break;
	}
}

static void icon_chevron(const cg_ui *ui, int cx, int cy, uint16_t color)
{
	cp_line(&ui->s, cx - 2, cy - 5, cx + 2, cy - 1, 1, color);
	cp_line(&ui->s, cx + 2, cy - 1, cx - 2, cy + 3, 1, color);
}

static void icon_check(const cg_ui *ui, int cx, int cy, uint16_t color)
{
	cp_line(&ui->s, cx - 4, cy - 1, cx - 1, cy + 2, 1, color);
	cp_line(&ui->s, cx - 1, cy + 2, cx + 4, cy - 3, 1, color);
}

/* ---- keycaps and the footer ----------------------------------------------- */

enum { TK_UD = 0, TK_LR, TK_OK, TK_BACK, TK_HOME, TK_SETTINGS, TK_MENU,
	TK_DEL, TK_DIGITS, TK_SORT, TK_ANY };

/* Footers that did not fit even without their optional hints (checked by
 * the host test). */
static int hint_overflows;

typedef struct hint {
	uint8_t token;
	uint8_t optional;          /* left out first when the footer is full */
	const char *label;
} hint;

static const char *token_text(int token)
{
	switch (token) {
	case TK_OK:       return KEY_NAME_OK;
	case TK_BACK:     return KEY_NAME_BACK;
	case TK_HOME:     return KEY_NAME_HOME;
	case TK_SETTINGS: return KEY_NAME_SETTINGS;
	case TK_MENU:     return KEY_NAME_MENU;
	case TK_DEL:      return "DEL";
	case TK_DIGITS:   return "0-9";
	case TK_SORT:     return KEY_NAME_SORT;
	case TK_ANY:      return "ANY KEY";
	default:          return "";
	}
}

static int keycap_w(int token)
{
	if (token == TK_UD || token == TK_LR)
		return 6 + 2 * 8;
	return text_w(F_KEY, token_text(token)) + 10;
}

/* A keycap's face around text on baseline `base`: the capitals with the
 * same three rows above and below them. */
static void keycap_face(const cg_ui *ui, int x, int base, int w, uint16_t fill)
{
	int cap = cap_of(F_KEY);

	rect(ui, x, base - cap - 3, w, cap + 6, 3, fill);
}

/* The row that centres an arrow on the capitals of baseline `base`. */
static int keycap_arrow_cy(int base)
{
	return base - cap_of(F_KEY) / 2;
}

static int keycap(const cg_ui *ui, int x, int base, int token)
{
	int w = keycap_w(token), cy = keycap_arrow_cy(base);
	uint16_t ink = ui->t.on_surface;

	keycap_face(ui, x, base, w, mix(ui->t.container_high, ui->t.on_surface,
		26));
	if (token == TK_UD) {
		triangle(ui, x + 7, cy, DIR_UP, 0, ink);
		triangle(ui, x + 15, cy, DIR_DOWN, 0, ink);
	} else if (token == TK_LR) {
		triangle(ui, x + 7, cy, DIR_LEFT, 0, ink);
		triangle(ui, x + 15, cy, DIR_RIGHT, 0, ink);
	} else {
		const char *s = token_text(token);

		text(ui, F_KEY, x + (w - text_w(F_KEY, s) + 1) / 2, base, s, ink);
	}
	return w;
}

static void footer_band(const cg_ui *ui)
{
	band(ui, FY, DHEIGHT - SCR_Y - FY, ui->t.container);
}

static int hint_w(const hint *h)
{
	return keycap_w(h->token) +
		(h->label[0] ? 4 + text_w(F_SMALL, h->label) : 0);
}

/* Key hints: `left` from the left edge, `right` against the right edge.
 * When they do not all fit, optional ones are left out, last first. */
static void hint_bar(const cg_ui *ui, const hint *left, int nl,
	const hint *right, int nr)
{
	uint8_t keep[8];
	/* Keycap text shares the labels' baseline. */
	int i, total, x, gap = 12, base = row_base(F_SMALL, FY, FOOT);

	footer_band(ui);
	for (i = 0; i < nl + nr && i < 8; i++)
		keep[i] = 1;
	for (;;) {
		int n = 0, drop = -1;

		total = 0;
		for (i = 0; i < nl + nr; i++)
			if (keep[i]) {
				total += hint_w(i < nl ? &left[i] : &right[i - nl]);
				n++;
			}
		total += n > 1 ? (n - 1) * gap : 0;
		if (total <= W - 20)
			break;
		for (i = nl + nr - 1; i >= nl && drop < 0; i--)
			if (keep[i] && right[i - nl].optional)
				drop = i;
		for (i = nl - 1; i >= 0 && drop < 0; i--)
			if (keep[i] && left[i].optional)
				drop = i;
		if (drop < 0) {
			if (gap > 8) {
				gap = 8;
				continue;
			}
			hint_overflows++;
			break;
		}
		keep[drop] = 0;
	}
	x = 10;
	for (i = 0; i < nl; i++) {
		if (!keep[i])
			continue;
		x += keycap(ui, x, base, left[i].token);
		if (left[i].label[0]) {
			x += 4;
			text(ui, F_SMALL, x, base, left[i].label, ui->t.on_surface_variant);
			x += text_w(F_SMALL, left[i].label);
		}
		x += gap;
	}
	x = W - 10;
	for (i = nr - 1; i >= 0; i--) {
		int kw;

		if (!keep[nl + i])
			continue;
		x -= hint_w(&right[i]);
		kw = keycap(ui, x, base, right[i].token);
		if (right[i].label[0])
			text(ui, F_SMALL, x + kw + 4, base, right[i].label,
				ui->t.on_surface_variant);
		x -= gap;
	}
}

enum { NOTE_DONE = 0, NOTE_ERROR, NOTE_BUSY };

/* A result, or what is being done, in the footer in place of the key
 * hints. */
static void footer_message(const cg_ui *ui, const char *s, int kind)
{
	int base = row_base(F_SMALL, FY, FOOT);

	footer_band(ui);
	if (kind == NOTE_DONE) {
		icon_check(ui, 16, base - (cap_of(F_SMALL) + 1) / 2, ui->t.primary);
		text_fit(ui, F_SMALL, 27, base, W - 37, s, ui->t.on_surface);
	} else {
		text_fit(ui, F_SMALL, 10, base, W - 20, s, kind == NOTE_ERROR
			? ui->t.error : ui->t.on_surface);
	}
}

/* ---- list parts ----------------------------------------------------------- */

static void header(const cg_ui *ui, const char *title, const char *meta)
{
	int meta_w = meta && meta[0] ? text_w(F_SMALL, meta) + 16 : 0;

	band(ui, -SCR_Y, HEAD + SCR_Y, ui->t.container);
	text_fit(ui, F_TITLE, 10, row_base(F_TITLE, 0, HEAD), W - 20 - meta_w,
		title, ui->t.on_surface);
	if (meta_w)
		text_end(ui, F_SMALL, W - 10, row_base(F_SMALL, 0, HEAD), meta,
			ui->t.on_surface_variant);
}

static void section_row(const cg_ui *ui, int y, const char *label)
{
	int base = y + SEC - 2 - desc_of(F_SMALL);
	int x = 10 + text_w(F_SMALL, label) + 8;

	text(ui, F_SMALL, 10, base, label, ui->t.primary);
	cp_fill(&ui->s, x, base - (cap_of(F_SMALL) + 1) / 2, W - 10 - x, 1,
		hairline(ui));
}

static void scrollbar(const cg_ui *ui, int first, int shown, int total)
{
	int y, h;

	if (total <= shown)
		return;
	h = VIEW_H * shown / total;
	if (h < 8)
		h = 8;
	y = HEAD + (VIEW_H - h) * first / (total - shown);
	rect(ui, W - 4, y, 2, h, 1, mix(ui->t.outline, ui->t.surface, 77));
}

/* A key at the end of a row, as a keycap; the cursor keys are drawn. */
static int key_value(const cg_ui *ui, int xr, int cy, uint16_t key, int sel)
{
	const char *label = fxcg100_key_label(key);
	int dir = -1, w;
	uint16_t ink = sel ? ui->t.primary : ui->t.on_surface;

	if (!strcmp(label, "UP"))
		dir = DIR_UP;
	else if (!strcmp(label, "DOWN"))
		dir = DIR_DOWN;
	else if (!strcmp(label, "LEFT"))
		dir = DIR_LEFT;
	else if (!strcmp(label, "RIGHT"))
		dir = DIR_RIGHT;
	int base = mid_base(F_KEY, cy);

	w = dir >= 0 ? 22 : text_w(F_KEY, label) + 12;
	keycap_face(ui, xr - w, base, w,
		sel ? ui->t.on_primary : ui->t.container_high);
	if (dir >= 0)
		triangle(ui, xr - w / 2, keycap_arrow_cy(base), dir, 0, ink);
	else
		text(ui, F_KEY, xr - w + (w - text_w(F_KEY, label) + 1) / 2, base,
			label, ink);
	return w;
}

static void draw_row(cg_ui *ui, int y, const ui_row *r, int sel)
{
	const cp_theme *t = &ui->t;
	int cy = y + ROW / 2, base = row_base(F_BODY, y, ROW), vw = 0;
	uint16_t fg = sel ? t->on_primary : r->error ? t->error : t->on_surface;
	uint16_t vfg = sel ? t->on_primary
		: r->accent ? t->primary : t->on_surface_variant;

	if (sel) {
		band(ui, y, ROW, t->primary);
		ui->sel_y = y;
		ui->sel_h = ROW;
	}
	if (r->kind == RK_SUB) {
		icon_chevron(ui, W - 16, cy, sel ? t->on_primary
			: t->on_surface_variant);
	} else if (r->kind == RK_KEY && r->key) {
		vw = key_value(ui, VAL_R, cy, r->key, sel);
	} else if (r->value[0]) {
		vw = text_w(F_BODY, r->value);
		text_end(ui, F_BODY, VAL_R, base, r->value, vfg);
		if (r->kind == RK_COLOUR) {
			int sx = VAL_R - vw - 10;

			if (sel)
				cp_disc(&ui->s, sx, cy, 7, t->on_primary);
			cp_disc(&ui->s, sx, cy, 5, cp_theme_get_mode(ui->state->theme,
				ui->state->dark_mode)->primary);
			vw += 16;
		}
		if (sel && r->lr) {
			triangle(ui, VAL_R + 9, cy, DIR_RIGHT, 1, t->on_primary);
			triangle(ui, VAL_R - vw - 9, cy, DIR_LEFT, 1, t->on_primary);
		}
	}
	text_fit(ui, F_BODY, 10, base, VAL_R - vw - 34, r->label, fg);
}

/* ---- rows of each page ---------------------------------------------------- */

static ui_row *add_row(cg_ui *ui, int kind, int id, const char *label)
{
	ui_row *r = &ui->rows[ui->nrows < MAX_ROWS ? ui->nrows++ : MAX_ROWS - 1];

	memset(r, 0, sizeof(*r));
	r->kind = (uint8_t)kind;
	r->id = (uint8_t)id;
	r->label = label;
	r->y = (int16_t)ui->list_h;
	r->h = kind == RK_SECTION ? SEC : ROW;
	ui->list_h += r->h;
	return r;
}

static ui_row *add_value(cg_ui *ui, int id, const char *label,
	const char *value)
{
	ui_row *r = add_row(ui, RK_VALUE, id, label);

	snprintf(r->value, sizeof(r->value), "%s", value);
	r->lr = 1;
	return r;
}

static void game_title(char *dst, size_t size, uint32_t index)
{
	size_t len;

	snprintf(dst, size, "%s", fxcg100_rom_source_label(index));
	len = strlen(dst);
	if (len > 4 && dst[len - 4] == '.' &&
			(dst[len - 3] | 0x20) == 'g' && (dst[len - 2] | 0x20) == 'b' &&
			(dst[len - 1] | 0x20) == 'a')
		dst[len - 4] = 0;
}

static void rows_pause(cg_ui *ui)
{
	char slot[16];

	snprintf(slot, sizeof(slot), "Slot %u",
		(unsigned)ui->state->savestate_slot % 10u);
	ui->list_h = 0;
	add_row(ui, RK_PLAIN, ID_RESUME, "Resume");
	add_value(ui, ID_SAVE, "Save state", slot);
	add_value(ui, ID_LOAD, "Load state", slot)->rule = 1;
	add_row(ui, RK_SUB, ID_READER, "TXT reader");
	add_row(ui, RK_SUB, ID_SETTINGS, "Settings");
	add_row(ui, RK_SUB, ID_GAMES, "Games")->rule = 1;
	add_row(ui, RK_PLAIN, ID_RESTART, "Restart");
	add_row(ui, RK_PLAIN, ID_EXIT, "Exit gpSP")->error = 1;
}

/* 240x160, 320x212, 384x216. */
static const char *const size_names[] = { "Original", "Full Height",
	"Full Screen" };
static const char *const filter_names[] = { "Smooth", "Sharp", "Crisp" };
static const char *const refresh_names[] = { "Auto", "System",
	"59.73 Hz (GBA)", "75 Hz", "90 Hz", "120 Hz", "150 Hz" };
static const char *const frameskip_names[] = { "Auto", "Manual", "Off" };
static const char *const pattern_names[] = { "Uniform", "Random" };
static const char *const ff_mode_names[FXCG100_FF_MODE_COUNT] = {
	"Hold", "Toggle"
};
static const char *const ff_speed_names[FXCG100_FF_SPEED_COUNT] = {
	"Unlimited", "1.5x", "2x", "3x", "4x"
};

static const char *pick(const char *const *names, uint32_t count,
	uint32_t value)
{
	return names[value < count ? value : 0];
}

static void rows_settings(cg_ui *ui)
{
	fxcg100_menu_state *st = ui->state;
	char n[12];
	ui_row *r;

	add_row(ui, RK_SECTION, ID_NONE, "Display");
	add_value(ui, ID_SIZE, "Picture size", pick(size_names, 3,
		st->screen_scale));
	/* Original is not scaled, so it has nothing to filter. */
	if (st->screen_scale != 0)
		add_value(ui, ID_FILTER, "Scaling filter", pick(filter_names, 3,
			st->screen_filter));
	add_value(ui, ID_VSYNC, "VSync", st->lcd_sync ? "On" : "Off");
	add_value(ui, ID_REFRESH, "Refresh rate", pick(refresh_names, 7,
		st->lcd_refresh));
	if (st->backlight == 0) {
		add_value(ui, ID_BACKLIGHT, "Backlight", "System");
	} else {
		snprintf(n, sizeof(n), "%u", (unsigned)st->backlight);
		add_value(ui, ID_BACKLIGHT, "Backlight", n);
	}
	add_row(ui, RK_SECTION, ID_NONE, "Appearance");
	r = add_value(ui, ID_COLOUR, "Colour", cp_theme_names[st->theme <
		CP_THEME_COUNT ? st->theme : 0]);
	r->kind = RK_COLOUR;
	add_value(ui, ID_DARK_MODE, "Dark mode", st->dark_mode ? "On" : "Off");
	add_row(ui, RK_SECTION, ID_NONE, "Speed");
	add_value(ui, ID_FRAMESKIP, "Frame skipping", pick(frameskip_names, 3,
		st->frameskip_type));
	if (st->frameskip_type == 1) {
		snprintf(n, sizeof(n), "%u", (unsigned)st->frameskip_value);
		add_value(ui, ID_SKIP_N, "Frames to skip", n);
		add_value(ui, ID_SKIP_PATTERN, "Skip pattern", pick(pattern_names, 2,
			st->frameskip_variation));
	}
	add_value(ui, ID_LIMIT, "Limit to 60 fps", st->frame_limit ? "On" : "Off");
	add_value(ui, ID_SHOW_FPS, "Show frame rate", st->show_fps ? "On" : "Off");
	add_value(ui, ID_FF_MODE, "Fast forward mode", pick(ff_mode_names,
		FXCG100_FF_MODE_COUNT, st->fast_forward_mode));
	add_value(ui, ID_FF_SPEED, "Fast forward speed", pick(ff_speed_names,
		FXCG100_FF_SPEED_COUNT, st->fast_forward_speed));
	add_row(ui, RK_SECTION, ID_NONE, "Keys");
	add_row(ui, RK_SUB, ID_KEYMAP, "Key mapping");
#if CGBA_DEBUG_MENU
	add_row(ui, RK_SECTION, ID_NONE, "Debug");
	add_row(ui, RK_SUB, ID_DEBUG, "Debug info");
#endif
}

/* The functions a key can be given, in the order of the key-mapping page:
 * the GBA buttons, which must all have a key, then the shortcuts. */
#define BINDING_COUNT 16
#define GBA_BINDINGS 10

static const struct {
	const char *name;
	int hotkey;
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
	{ "TXT reader", 1, FXCG100_HOTKEY_TXT_READER },
	{ "Save & exit", 1, FXCG100_HOTKEY_SAVE_EXIT },
};

static uint16_t *binding_slot(cg_ui *ui, int i)
{
	return bindings[i].hotkey ? &ui->state->hotkey_map[bindings[i].index]
		: &ui->state->keymap[bindings[i].index];
}

static void rows_keys(cg_ui *ui)
{
	int i;

	for (i = 0; i < BINDING_COUNT; i++) {
		ui_row *r;

		if (i == 0 || i == GBA_BINDINGS)
			add_row(ui, RK_SECTION, ID_NONE,
				i ? "Shortcuts" : "Game Boy Advance");
		r = add_row(ui, RK_KEY, ID_BIND0 + i, bindings[i].name);
		r->key = *binding_slot(ui, i);
		if (!r->key)
			snprintf(r->value, sizeof(r->value), "None");
		r->rule = i == BINDING_COUNT - 1;
	}
	add_row(ui, RK_PLAIN, ID_RESET_KEYS, "Reset to defaults");
}

static void rows_debug(cg_ui *ui)
{
	uint32_t i;

	if (!ui->debug || ui->debug->count == 0) {
		add_row(ui, RK_PLAIN, ID_LINE0, "No debug snapshot");
		return;
	}
	for (i = 0; i < ui->debug->count && i < FXCG100_DEBUG_MENU_LINES; i++)
		add_row(ui, RK_PLAIN, ID_LINE0 + (int)i, ui->debug->lines[i]);
}

static void build_rows(cg_ui *ui)
{
	ui->nrows = 0;
	ui->list_h = 0;
	switch (ui->page) {
	case PAGE_PAUSE:    rows_pause(ui); break;
	case PAGE_SETTINGS: rows_settings(ui); break;
	case PAGE_KEYS:     rows_keys(ui); break;
	case PAGE_DEBUG:    rows_debug(ui); break;
	default:            break;
	}
}

static int row_index(const cg_ui *ui, int id)
{
	int i;

	for (i = 0; i < ui->nrows; i++)
		if (ui->rows[i].id == id && ui->rows[i].kind != RK_SECTION)
			return i;
	return -1;
}

/* The selected row of a list page; the first one when the id is gone. */
static const ui_row *selected_row(cg_ui *ui)
{
	int i = row_index(ui, ui->sel[ui->page]);

	if (i < 0) {
		for (i = 0; i < ui->nrows && ui->rows[i].kind == RK_SECTION; i++)
			;
		if (i >= ui->nrows)
			return NULL;
		ui->sel[ui->page] = ui->rows[i].id;
	}
	return &ui->rows[i];
}

/* The top of the first row at or below list pixel y: scrolling stops on
 * row tops, so no sliver of a row shows under the header. */
static int row_top_from(const cg_ui *ui, int y)
{
	int i;

	for (i = 0; i < ui->nrows; i++)
		if (ui->rows[i].y >= y)
			return ui->rows[i].y;
	return y;
}

/* Scroll so the selected row shows with the rows on either side of it. */
static void list_reveal(cg_ui *ui)
{
	int i = row_index(ui, ui->sel[ui->page]), top, bottom, max;
	int16_t *scroll = &ui->scroll[ui->page];

	max = ui->list_h > VIEW_H ? row_top_from(ui, ui->list_h - VIEW_H) : 0;
	if (i >= 0) {
		top = i > 0 ? ui->rows[i - 1].y : 0;
		bottom = i + 1 < ui->nrows ? ui->rows[i + 1].y + ui->rows[i + 1].h
			: ui->list_h;
		if (top < *scroll)
			*scroll = (int16_t)top;
		if (bottom > *scroll + VIEW_H)
			*scroll = (int16_t)row_top_from(ui, bottom - VIEW_H);
	}
	if (*scroll > max)
		*scroll = (int16_t)max;
	if (*scroll < 0)
		*scroll = 0;
}

static uint32_t library_order(const cg_ui *ui, uint32_t *order)
{
	return fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		ui->state->game_sort);
}

static void library_reveal(cg_ui *ui)
{
	uint32_t count = fxcg100_rom_source_count(), max;

	if (count > FXCG100_ROM_SORT_MAX)
		count = FXCG100_ROM_SORT_MAX;
	max = count > LIB_ROWS ? count - LIB_ROWS : 0;
	if (ui->game_sel >= count)
		ui->game_sel = count ? count - 1 : 0;
	if (ui->game_sel < ui->game_first + 1)
		ui->game_first = ui->game_sel ? ui->game_sel - 1 : 0;
	if (ui->game_sel + 2 > ui->game_first + LIB_ROWS)
		ui->game_first = ui->game_sel + 2 - LIB_ROWS;
	if (ui->game_first > max)
		ui->game_first = max;
}

/* ---- pages ---------------------------------------------------------------- */

static void draw_list(cg_ui *ui, const char *title, const char *meta)
{
	int scroll = ui->scroll[ui->page], i;
	const ui_row *sel = ui->dialog ? NULL : selected_row(ui);

	cp_fill(&ui->s, -SCR_X, -SCR_Y, DWIDTH, DHEIGHT, ui->t.surface);
	header(ui, title, meta);
	cp_surface_clip(&ui->s, (cp_rect){ 0, (int16_t)(SCR_Y + HEAD),
		(int16_t)DWIDTH, (int16_t)VIEW_H });
	for (i = 0; i < ui->nrows; i++) {
		const ui_row *r = &ui->rows[i];
		int y = HEAD + r->y - scroll;

		if (r->rule && y + r->h > HEAD && y + r->h < FY)
			cp_fill(&ui->s, 10, y + r->h, W - 20, 1, hairline(ui));
	}
	for (i = 0; i < ui->nrows; i++) {
		const ui_row *r = &ui->rows[i];
		int y = HEAD + r->y - scroll;

		if (y + r->h <= HEAD || y >= FY)
			continue;
		if (r->kind == RK_SECTION)
			section_row(ui, y, r->label);
		else
			draw_row(ui, y, r, r == sel);
	}
	cp_surface_unclip(&ui->s);
	scrollbar(ui, scroll, VIEW_H, ui->list_h);
}

static void draw_library(cg_ui *ui)
{
	uint32_t order[FXCG100_ROM_SORT_MAX];
	uint32_t count = library_order(ui, order), i;
	char name[64], meta[36];

	cp_fill(&ui->s, -SCR_X, -SCR_Y, DWIDTH, DHEIGHT, ui->t.surface);
	if (count == 0) {
		header(ui, "Games", NULL);
		text_mid(ui, F_TITLE, (W - text_w(F_TITLE, "No games yet")) / 2, 96,
			"No games yet", ui->t.on_surface);
		text_mid(ui, F_SMALL, (W - text_w(F_SMALL,
			"Copy .gba files to the calculator's storage.")) / 2, 120,
			"Copy .gba files to the calculator's storage.",
			ui->t.on_surface_variant);
		return;
	}
	library_reveal(ui);
	snprintf(meta, sizeof(meta), "%s  %u / %u",
		fxcg100_game_sort_label(ui->state->game_sort),
		(unsigned)ui->game_sel + 1, (unsigned)count);
	header(ui, "Games", meta);
	for (i = 0; i < LIB_ROWS && ui->game_first + i < count; i++) {
		uint32_t pos = ui->game_first + i;
		uint32_t g = order[pos];
		ui_row r;

		memset(&r, 0, sizeof(r));
		r.kind = RK_VALUE;
		game_title(name, sizeof(name), g);
		r.label = name;
		if (ui->in_game && g == ui->state->rom_source) {
			snprintf(r.value, sizeof(r.value), "Playing");
			r.accent = 1;
		}
		draw_row(ui, HEAD + (int)i * ROW, &r, !ui->dialog && pos == ui->game_sel);
	}
	scrollbar(ui, (int)ui->game_first, LIB_ROWS, (int)count);
}

static int txt_browser_total(const txt_reader *r)
{
	return (int)r->dir_count + (r->dir_depth > 0 ? 1 : 0);
}

static void txt_browser_reveal(cg_ui *ui, const txt_reader *r)
{
	int count = txt_browser_total(r), max;

	max = count > LIB_ROWS ? count - LIB_ROWS : 0;
	if (ui->txt_browser_sel >= count)
		ui->txt_browser_sel = count > 0 ? count - 1 : 0;
	if (ui->txt_browser_sel < 0)
		ui->txt_browser_sel = 0;
	if (ui->txt_browser_sel < ui->txt_browser_first + 1)
		ui->txt_browser_first = ui->txt_browser_sel > 0
			? ui->txt_browser_sel - 1 : 0;
	if (ui->txt_browser_sel + 2 > ui->txt_browser_first + LIB_ROWS)
		ui->txt_browser_first = ui->txt_browser_sel + 2 - LIB_ROWS;
	if (ui->txt_browser_first > max)
		ui->txt_browser_first = max;
	if (ui->txt_browser_first < 0)
		ui->txt_browser_first = 0;
}

static void draw_txt_browser(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	int total = txt_browser_total(r), i;
	char dir_label[48], row_name[TXT_NAME_MAX + 2];

	cp_fill(&ui->s, -SCR_X, -SCR_Y, DWIDTH, DHEIGHT, ui->t.surface);
	txt_reader_display_dir(r, dir_label, sizeof(dir_label));
	header(ui, "TXT files", dir_label);
	if (total == 0) {
		text_mid(ui, F_TITLE, (W - text_w(F_TITLE, "No .TXT files")) / 2, 96,
			"No .TXT files", ui->t.on_surface);
		text_mid(ui, F_SMALL, (W - text_w(F_SMALL,
			"Copy .txt files to the calculator's storage.")) / 2, 120,
			"Copy .txt files to the calculator's storage.",
			ui->t.on_surface_variant);
		return;
	}
	txt_browser_reveal(ui, r);
	for (i = 0; i < LIB_ROWS && ui->txt_browser_first + i < total; i++) {
		int pos = ui->txt_browser_first + i;
		ui_row row;

		memset(&row, 0, sizeof(row));
		if (r->dir_depth > 0 && pos == 0) {
			row.kind = RK_PLAIN;
			row.label = "..";
			snprintf(row.value, sizeof(row.value), "Up");
		} else {
			int idx = pos - (r->dir_depth > 0 ? 1 : 0);
			const txt_dir_entry *e = &r->dir_entries[idx];

			if (e->is_dir) {
				row.kind = RK_SUB;
				snprintf(row_name, sizeof(row_name), "%s/", e->name);
				row.label = row_name;
			} else {
				row.kind = RK_VALUE;
				row.label = e->name;
				txt_format_size(e->size, row.value, sizeof(row.value));
			}
		}
		draw_row(ui, HEAD + i * ROW, &row,
			!ui->dialog && pos == ui->txt_browser_sel);
	}
	scrollbar(ui, ui->txt_browser_first, LIB_ROWS, total);
}

static int txt_reader_footer_visible(const cg_ui *ui, const txt_reader *r)
{
	return ui->progress != NULL || ui->message[0] != 0 || r->show_hints != 0;
}

static void draw_txt_reader(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	const txt_bitmap_font *hf = &txt_font_terminus_6x12;
	const txt_bitmap_font *bf;
	int footer_vis = txt_reader_footer_visible(ui, r);
	int body_top = TXT_HEAD + 2;
	int body_bot = footer_vis ? FY : (H - 2);
	int body_h = body_bot - body_top;
	int line_h, vis_lines, cols, i;
	int has_bm, pct, right_len, max_name_cols, rx;
	char status[24];

	txt_reader_sync_storage(r);
	bf = txt_reader_font(r);
	line_h = (int)bf->h + 1;
	vis_lines = body_h / line_h;
	if (vis_lines < 1)
		vis_lines = 1;
	cols = (W - 14) / (int)bf->w;
	if (cols < 1)
		cols = 1;
	txt_reader_layout(r, cols, vis_lines);

	cp_fill(&ui->s, -SCR_X, -SCR_Y, DWIDTH, DHEIGHT, ui->t.surface);
	band(ui, -SCR_Y, TXT_HEAD + SCR_Y, ui->t.container);

	has_bm = txt_reader_has_visible_bookmark(r);
	pct = txt_reader_percent(r);
	if (!r->wrap)
		snprintf(status, sizeof(status), "NOWRAP %d%%", pct);
	else
		snprintf(status, sizeof(status), "%d%%", pct);
	right_len = (int)strlen(status) + (has_bm ? 2 : 0);
	rx = W - 6 - right_len * (int)hf->w;
	if (has_bm) {
		txt_draw_text(&ui->s, hf, rx, 3, "*", ui->t.primary);
		txt_draw_text(&ui->s, hf, rx + 2 * (int)hf->w, 3, status,
			ui->t.on_surface_variant);
	} else {
		txt_draw_text(&ui->s, hf, rx, 3, status, ui->t.on_surface_variant);
	}
	max_name_cols = rx - 12;
	if (max_name_cols > 0)
		txt_draw_text_fit(&ui->s, hf, 6, 3, max_name_cols, r->doc_name,
			ui->t.on_surface);

	if (r->doc_size == 0 || r->line_count == 0) {
		const char *empty = "(Empty file)";
		int ew = (int)strlen(empty) * (int)bf->w;

		txt_draw_text(&ui->s, bf, (W - ew) / 2, body_top + body_h / 2 - 6,
			empty, ui->t.on_surface_variant);
	} else {
		for (i = 0; i < r->line_count; i++)
			txt_draw_text(&ui->s, bf, 6, body_top + i * line_h,
				r->lines[i].text, ui->t.on_surface);
	}

	if (r->doc_size > 0 && (r->top_offset > 0 || r->end_offset < r->doc_size)) {
		int track_h = body_bot - body_top - 4;
		uint32_t vis_bytes = r->end_offset > r->top_offset
			? r->end_offset - r->top_offset : 1u;
		int thumb_h = (int)(((uint64_t)track_h * vis_bytes) / r->doc_size);
		int thumb_y;

		if (thumb_h < 10)
			thumb_h = 10;
		if (thumb_h > track_h)
			thumb_h = track_h;
		if (r->top_offset >= r->doc_size)
			thumb_y = body_top + 2 + track_h - thumb_h;
		else
			thumb_y = body_top + 2 +
				(int)(((uint64_t)(track_h - thumb_h) * r->top_offset) /
					r->doc_size);
		rect(ui, W - 4, thumb_y, 2, thumb_h, 1,
			mix(ui->t.outline, ui->t.surface, 77));
	}
}

/* The page, without its footer. */
static void draw_page(cg_ui *ui)
{
	char name[64];

	ui->sel_y = ui->sel_h = 0;
	switch (ui->page) {
	case PAGE_PAUSE:
		game_title(name, sizeof(name), ui->state->rom_source);
		draw_list(ui, name, "Paused");
		break;
	case PAGE_SETTINGS:
		draw_list(ui, "Settings", NULL);
		break;
	case PAGE_KEYS:
		draw_list(ui, "Key mapping", NULL);
		break;
	case PAGE_DEBUG:
		draw_list(ui, "Debug info", NULL);
		break;
	case PAGE_TXT_BROWSER:
		draw_txt_browser(ui);
		break;
	case PAGE_TXT_READER:
		draw_txt_reader(ui);
		break;
	default:
		draw_library(ui);
		break;
	}
}

#define HINTS(a) (a), (int)(sizeof(a) / sizeof((a)[0]))

static void page_hints(cg_ui *ui)
{
	const ui_row *r = (ui->page == PAGE_LIBRARY ||
		ui->page == PAGE_TXT_BROWSER || ui->page == PAGE_TXT_READER)
		? NULL : selected_row(ui);
	int id = r ? r->id : ID_NONE;

	switch (ui->page) {
	case PAGE_LIBRARY: {
		static const hint l[] = { { TK_UD, 1, "Choose" }, { TK_OK, 0, "Play" },
			{ TK_SORT, 0, "Sort" }, { TK_LR, 1, "Page" } };
		static const hint in[] = { { TK_BACK, 0, HINT_BACK_LABEL },
			{ TK_SETTINGS, 0, HINT_SETTINGS_LABEL } };
		static const hint start[] = { { TK_HOME, 0, "Exit" },
			{ TK_SETTINGS, 0, HINT_SETTINGS_LABEL } };

		if (fxcg100_rom_source_count() == 0)
			hint_bar(ui, NULL, 0, HINTS(start));
		else if (ui->in_game)
			hint_bar(ui, HINTS(l), HINTS(in));
		else
			hint_bar(ui, HINTS(l), HINTS(start));
		break;
	}
	case PAGE_TXT_BROWSER: {
		txt_reader *tr = txt_reader_global();
		static const hint l[] = { { TK_UD, 1, "Choose" }, { TK_OK, 0, "Open" },
			{ TK_LR, 1, "Page" } };
		static const hint up[] = { { TK_BACK, 0, "Up" } };
		static const hint back[] = { { TK_BACK, 0, HINT_BACK_LABEL } };

		if (txt_browser_total(tr) == 0)
			hint_bar(ui, NULL, 0, HINTS(back));
		else if (tr->dir_depth > 0)
			hint_bar(ui, HINTS(l), HINTS(up));
		else
			hint_bar(ui, HINTS(l), HINTS(back));
		break;
	}
	case PAGE_TXT_READER: {
		txt_reader *tr = txt_reader_global();
		static const hint wrap_l[] = { { TK_UD, 0, "Scroll" },
			{ TK_LR, 0, "Page" }, { TK_OK, 0, "Options" },
			{ TK_SORT, 1, "Mark" } };
		static const hint pan_l[] = { { TK_UD, 0, "Scroll" },
			{ TK_LR, 0, "Pan" }, { TK_OK, 0, "Options" },
			{ TK_SORT, 1, "Mark" } };
		static const hint right[] = { { TK_BACK, 0, HINT_BACK_LABEL } };

		hint_bar(ui, tr->wrap ? wrap_l : pan_l, 4, HINTS(right));
		break;
	}
	case PAGE_PAUSE: {
		static const hint save[] = { { TK_UD, 0, "Move" }, { TK_LR, 0, "Slot" },
			{ TK_OK, 0, "Save" } };
		static const hint load[] = { { TK_UD, 0, "Move" }, { TK_LR, 0, "Slot" },
			{ TK_OK, 0, "Load" } };
		static const hint other[] = { { TK_UD, 0, "Move" },
			{ TK_OK, 0, "Select" }, { TK_DIGITS, 1, "Slot" } };
		static const hint right[] = { { TK_BACK, 0, "Resume" } };

		hint_bar(ui, id == ID_SAVE ? save : id == ID_LOAD ? load : other, 3,
			HINTS(right));
		break;
	}
	case PAGE_SETTINGS: {
		static const hint value[] = { { TK_LR, 0, "Change" },
			{ TK_UD, 0, "Move" } };
		static const hint number[] = { { TK_LR, 0, "-1 / +1" },
			{ TK_UD, 0, "Move" } };
		static const hint open[] = { { TK_OK, 0, "Open" }, { TK_UD, 0, "Move" } };
		static const hint right[] = { { TK_BACK, 0, "Done" } };

		hint_bar(ui, (id == ID_SKIP_N || id == ID_BACKLIGHT) ? number
			: r && r->kind == RK_SUB ? open : value, 2, HINTS(right));
		break;
	}
	case PAGE_KEYS: {
		static const hint bind[] = { { TK_OK, 0, "Change" },
			{ TK_UD, 0, "Move" } };
		static const hint clear[] = { { TK_OK, 0, "Change" },
			{ TK_DEL, 0, "Clear" }, { TK_UD, 1, "Move" } };
		static const hint reset[] = { { TK_OK, 0, "Reset" },
			{ TK_UD, 0, "Move" } };
		static const hint right[] = { { TK_BACK, 0, HINT_BACK_LABEL } };

		if (id == ID_RESET_KEYS)
			hint_bar(ui, HINTS(reset), HINTS(right));
		else if (r && r->key && id >= ID_BIND0 + GBA_BINDINGS)
			hint_bar(ui, HINTS(clear), HINTS(right));
		else
			hint_bar(ui, HINTS(bind), HINTS(right));
		break;
	}
	default: {
		static const hint l[] = { { TK_UD, 0, "Scroll" } };
		static const hint right[] = { { TK_BACK, 0, HINT_BACK_LABEL } };

		hint_bar(ui, HINTS(l), HINTS(right));
		break;
	}
	}
}

/* ---- dialogs -------------------------------------------------------------- */

static void dialog_row(const cg_ui *ui, int x, int y, int w, const char *label,
	int sel, int error)
{
	if (sel)
		cp_fill(&ui->s, x, y, w, ROW, ui->t.primary);
	text(ui, F_BODY, x + 12, row_base(F_BODY, y, ROW), label, sel
		? ui->t.on_primary : error ? ui->t.error : ui->t.on_surface);
}

static void draw_confirm(cg_ui *ui)
{
	static const hint l[] = { { TK_UD, 0, "Choose" }, { TK_OK, 0, "Confirm" } };
	static const hint exit_r[] = { { TK_HOME, 0, "Exit" },
		{ TK_BACK, 0, "Cancel" } };
	static const hint other_r[] = { { TK_BACK, 0, "Cancel" } };
	const char *title, *line, *go;
	char name[64], text_buf[96];
	int w, x, y = 52;

	if (ui->dialog == DIALOG_EXIT) {
		title = "Exit gpSP?";
		line = ui->in_game ? "In-game save will be written to storage."
			: "Back to the calculator's menu.";
		go = "Exit";
	} else if (ui->dialog == DIALOG_RESTART) {
		title = "Restart this game?";
		line = "Progress since the last save is lost.";
		go = "Restart";
	} else if (ui->dialog == DIALOG_SAVE) {
		title = "Save state?";
		snprintf(text_buf, sizeof(text_buf), "Slot %u will be overwritten.",
			(unsigned)(ui->state->savestate_slot % 10u));
		line = text_buf;
		go = "Save";
	} else {
		game_title(name, sizeof(name), ui->switch_rom);
		snprintf(text_buf, sizeof(text_buf), "Play %s?", name);
		title = text_buf;
		line = "Progress since the last save is lost.";
		go = "Play";
	}
	w = text_w(F_TITLE, title) + 24;
	if (w < text_w(F_SMALL, line) + 24)
		w = text_w(F_SMALL, line) + 24;
	if (w < 260)
		w = 260;
	if (w > W - 24)
		w = W - 24;
	x = (W - w) / 2;
	rect(ui, x, y, w, 100, 8, ui->t.container_high);
	cp_surface_clip(&ui->s, (cp_rect){ (int16_t)(SCR_X + x), (int16_t)(SCR_Y + y),
		(int16_t)w, 100 });
	text_mid(ui, F_TITLE, x + 12, y + 14, title, ui->t.on_surface);
	text_mid(ui, F_SMALL, x + 12, y + 34, line, ui->t.on_surface_variant);
	cp_surface_unclip(&ui->s);
	dialog_row(ui, x, y + 48, w, "Cancel", ui->dialog_sel == 0, 0);
	dialog_row(ui, x, y + 70, w, go, ui->dialog_sel == 1, 1);
	if (ui->dialog == DIALOG_EXIT)
		hint_bar(ui, HINTS(l), HINTS(exit_r));
	else
		hint_bar(ui, HINTS(l), HINTS(other_r));
}

static void draw_capture(cg_ui *ui)
{
	static const hint l[] = { { TK_ANY, 0, "Bind" } };
	static const hint r[] = { { TK_MENU, 0, "Cancel" } };
	const char *name = bindings[ui->capture].name;
	char title[48], line[64];
	int w, x, y = 62, kw;

	snprintf(title, sizeof(title), "Press key for [%s]", name);
	snprintf(line, sizeof(line), "Existing binding with the same key will be cleared.");
	w = text_w(F_TITLE, title) + 24;
	if (w < text_w(F_SMALL, line) + 24)
		w = text_w(F_SMALL, line) + 24;
	if (w < 240)
		w = 240;
	x = (W - w) / 2;
	rect(ui, x, y, w, 82, 8, ui->t.container_high);
	text_mid(ui, F_TITLE, x + 12, y + 16, title, ui->t.on_surface);
	text_mid(ui, F_SMALL, x + 12, y + 38, line, ui->t.on_surface_variant);
	kw = keycap(ui, x + 12, mid_base(F_KEY, y + 62), TK_MENU);
	text(ui, F_SMALL, x + 12 + kw + 5, mid_base(F_KEY, y + 62), "cancels",
		ui->t.on_surface_variant);
	hint_bar(ui, HINTS(l), HINTS(r));
}

static void draw_txt_options(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	static const char *const labels[5] = {
		"Jump to", "Bookmarks", "Font", "Wrap", "Files"
	};
	static const hint jump_l[] = { { TK_UD, 0, "Move" },
		{ TK_LR, 0, "-5/+5%" }, { TK_OK, 0, "Jump" } };
	static const hint toggle_l[] = { { TK_UD, 0, "Move" },
		{ TK_LR, 0, "Change" } };
	static const hint open_l[] = { { TK_UD, 0, "Move" },
		{ TK_OK, 0, "Open" } };
	static const hint right[] = { { TK_BACK, 0, "Close" } };
	char vals[5][16];
	int w = 264, h = 148, x = (W - w) / 2, y = (FY - h) / 2, i;

	snprintf(vals[0], sizeof(vals[0]), "%d%%", ui->txt_opt_jump);
	snprintf(vals[1], sizeof(vals[1]), "%u", txt_reader_bookmark_count(r));
	snprintf(vals[2], sizeof(vals[2]), "%s",
		r->font_idx == TXT_FONT_8X14 ? "8x14" : "6x12");
	snprintf(vals[3], sizeof(vals[3]), "%s", r->wrap ? "On" : "Off");
	vals[4][0] = 0;

	rect(ui, x, y, w, h, 8, ui->t.container_high);
	text_mid(ui, F_TITLE, x + 12, y + 15, "Reader options", ui->t.on_surface);
	for (i = 0; i < 5; i++) {
		int ry = y + 30 + i * ROW;
		int cy = ry + ROW / 2;
		int base = row_base(F_BODY, ry, ROW);
		int sel = ui->dialog_sel == i;
		uint16_t fg = sel ? ui->t.on_primary : ui->t.on_surface;
		uint16_t vfg = sel ? ui->t.on_primary : ui->t.on_surface_variant;
		int vr = x + w - 22;

		if (sel)
			cp_fill(&ui->s, x, ry, w, ROW, ui->t.primary);
		text(ui, F_BODY, x + 12, base, labels[i], fg);
		if (i == 1 || i == 4) {
			icon_chevron(ui, x + w - 14, cy, vfg);
			if (vals[i][0])
				text_end(ui, F_BODY, x + w - 24, base, vals[i], vfg);
		} else {
			int vw = text_w(F_BODY, vals[i]);

			text_end(ui, F_BODY, vr, base, vals[i], vfg);
			if (sel) {
				triangle(ui, vr + 9, cy, DIR_RIGHT, 1, ui->t.on_primary);
				triangle(ui, vr - vw - 9, cy, DIR_LEFT, 1, ui->t.on_primary);
			}
		}
	}
	if (ui->dialog_sel == 0)
		hint_bar(ui, HINTS(jump_l), HINTS(right));
	else if (ui->dialog_sel == 2 || ui->dialog_sel == 3)
		hint_bar(ui, HINTS(toggle_l), HINTS(right));
	else
		hint_bar(ui, HINTS(open_l), HINTS(right));
}

static void txt_bm_reveal(cg_ui *ui, int total)
{
	int max = total > TXT_BM_VIS_ROWS ? total - TXT_BM_VIS_ROWS : 0;

	if (ui->txt_bm_sel >= total)
		ui->txt_bm_sel = total > 0 ? total - 1 : 0;
	if (ui->txt_bm_sel < 0)
		ui->txt_bm_sel = 0;
	if (ui->txt_bm_sel < ui->txt_bm_first)
		ui->txt_bm_first = ui->txt_bm_sel;
	if (ui->txt_bm_sel >= ui->txt_bm_first + TXT_BM_VIS_ROWS)
		ui->txt_bm_first = ui->txt_bm_sel - TXT_BM_VIS_ROWS + 1;
	if (ui->txt_bm_first > max)
		ui->txt_bm_first = max;
	if (ui->txt_bm_first < 0)
		ui->txt_bm_first = 0;
}

static void draw_txt_bookmarks(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	uint32_t marks[TXT_MAX_BOOKMARKS];
	int count = (int)txt_reader_get_bookmarks(r, marks, TXT_MAX_BOOKMARKS);
	int can_add = count < (int)TXT_MAX_BOOKMARKS;
	int total = count + (can_add ? 1 : 0);
	static const hint bm_l[] = { { TK_OK, 0, "Jump" }, { TK_DEL, 0, "Delete" },
		{ TK_UD, 1, "Move" } };
	static const hint add_l[] = { { TK_OK, 0, "Add" }, { TK_UD, 0, "Move" } };
	static const hint right[] = { { TK_BACK, 0, HINT_BACK_LABEL } };
	char meta[16], preview[48], row_text[64];
	int w = 316, h = 166, x = (W - w) / 2, y = 16, i;

	txt_bm_reveal(ui, total);
	rect(ui, x, y, w, h, 8, ui->t.container_high);
	text_mid(ui, F_TITLE, x + 12, y + 14, "Bookmarks", ui->t.on_surface);
	snprintf(meta, sizeof(meta), "%d / %u", count, (unsigned)TXT_MAX_BOOKMARKS);
	text_end(ui, F_SMALL, x + w - 12, mid_base(F_SMALL, y + 14), meta,
		ui->t.on_surface_variant);

	for (i = 0; i < TXT_BM_VIS_ROWS && ui->txt_bm_first + i < total; i++) {
		int idx = ui->txt_bm_first + i;
		int ry = y + 28 + i * ROW;
		int base = row_base(F_BODY, ry, ROW);
		int sel = ui->txt_bm_sel == idx;

		if (sel)
			cp_fill(&ui->s, x, ry, w, ROW, ui->t.primary);
		if (idx < count) {
			int pct = r->doc_size > 0
				? (int)(((uint64_t)marks[idx] * 100u) / r->doc_size) : 0;

			if (pct > 100)
				pct = 100;
			txt_reader_preview_at(r, marks[idx], preview, sizeof(preview));
			if (preview[0])
				snprintf(row_text, sizeof(row_text), "%d%%  %s", pct, preview);
			else
				snprintf(row_text, sizeof(row_text), "%d%%", pct);
			text_fit(ui, F_BODY, x + 12, base, w - 26, row_text,
				sel ? ui->t.on_primary : ui->t.on_surface);
		} else {
			text_fit(ui, F_BODY, x + 12, base, w - 26, "+ Add bookmark here",
				sel ? ui->t.on_primary : ui->t.primary);
		}
	}
	if (total > TXT_BM_VIS_ROWS) {
		int track_h = TXT_BM_VIS_ROWS * ROW - 4;
		int thumb_h = track_h * TXT_BM_VIS_ROWS / total;
		int thumb_y;

		if (thumb_h < 10)
			thumb_h = 10;
		thumb_y = y + 30 + (track_h - thumb_h) * ui->txt_bm_first /
			(total - TXT_BM_VIS_ROWS);
		rect(ui, x + w - 5, thumb_y, 2, thumb_h, 1,
			mix(ui->t.outline, ui->t.container_high, 77));
	}
	if (ui->txt_bm_sel < count)
		hint_bar(ui, HINTS(bm_l), HINTS(right));
	else
		hint_bar(ui, HINTS(add_l), HINTS(right));
}

/* ---- drawing and sending to the panel ------------------------------------- */

static void draw(cg_ui *ui)
{
	ui->t = *cp_theme_get_mode(ui->state->theme, ui->state->dark_mode);
	cp_surface_init(&ui->s, gint_vram, DWIDTH, DHEIGHT, CP_SURFACE_UPRIGHT);
	ui->s.ox = SCR_X;
	ui->s.oy = SCR_Y;
	build_rows(ui);
	if (ui->dialog) {
		cp_theme shown = ui->t;

		/* The page underneath, darkened and without its footer. */
		cp_theme_toward(&ui->t, &shown, 0x0000,
			ui->state->dark_mode ? DIM : DIM_LIGHT);
		draw_page(ui);
		ui->t = shown;
		if (ui->dialog == DIALOG_CAPTURE)
			draw_capture(ui);
		else if (ui->dialog == DIALOG_TXT_OPTIONS)
			draw_txt_options(ui);
		else if (ui->dialog == DIALOG_TXT_BOOKMARKS)
			draw_txt_bookmarks(ui);
		else
			draw_confirm(ui);
		return;
	}
	draw_page(ui);
	if (ui->progress)
		footer_message(ui, ui->progress, NOTE_BUSY);
	else if (ui->message[0])
		footer_message(ui, ui->message, ui->message_error ? NOTE_ERROR
			: NOTE_DONE);
	else if (ui->page != PAGE_TXT_READER || txt_reader_global()->show_hints)
		page_hints(ui);
}

static int page_scroll(const cg_ui *ui)
{
	if (ui->page == PAGE_LIBRARY)
		return (int)ui->game_first;
	if (ui->page == PAGE_TXT_BROWSER)
		return ui->txt_browser_first;
	if (ui->page == PAGE_TXT_READER)
		return (int)txt_reader_global()->top_offset;
	return ui->scroll[ui->page];
}

static void push_band(int y, int h)
{
	if (h > 0)
		fxcg100_lcd_update_rows((unsigned)(SCR_Y + y), (unsigned)h);
}

static void render(cg_ui *ui)
{
	int mode = ui->redraw;
	int same;

	draw(ui);
	same = ui->shown && !ui->dialog && !ui->shown_dialog &&
		ui->shown_page == ui->page && ui->shown_scroll == page_scroll(ui) &&
		ui->shown_theme == (int)ui->state->theme &&
		ui->shown_dark == (ui->state->dark_mode != 0);
	if (same && mode == REDRAW_SELECTION) {
		push_band(-SCR_Y, HEAD + SCR_Y);
		push_band(ui->shown_sel_y, ui->shown_sel_h);
		push_band(ui->sel_y, ui->sel_h);
		push_band(FY, DHEIGHT - SCR_Y - FY);
	} else if (same && mode == REDRAW_FOOTER) {
		push_band(FY, DHEIGHT - SCR_Y - FY);
	} else {
		fxcg100_lcd_update();
	}
	ui->shown = 1;
	ui->shown_page = ui->page;
	ui->shown_scroll = page_scroll(ui);
	ui->shown_dialog = ui->dialog;
	ui->shown_theme = (int)ui->state->theme;
	ui->shown_dark = ui->state->dark_mode != 0;
	ui->shown_sel_y = ui->sel_y;
	ui->shown_sel_h = ui->sel_h;
	ui->redraw = REDRAW_NONE;
}

static void want(cg_ui *ui, int mode)
{
	if (mode > ui->redraw)
		ui->redraw = mode;
}

/* ---- input ---------------------------------------------------------------- */

static void message(cg_ui *ui, int error, const char *fmt, const char *a,
	const char *b)
{
	snprintf(ui->message, sizeof(ui->message), fmt, a, b);
	ui->message_error = error;
	ui->message_t0 = rtc_ticks();
	want(ui, REDRAW_FOOTER);
}

static void read_keys(cg_ui *ui)
{
	uint32_t keys = fxcg100_poll_app_keys(), now = rtc_ticks();
	uint32_t edge = keys & ~ui->keys;

	ui->repeat = 0;
	if (edge & CURSOR_KEYS) {
		/* The lowest bit: one direction repeats at a time. */
		ui->rep_key = (edge & CURSOR_KEYS) & (0u - (edge & CURSOR_KEYS));
		ui->rep_t = now;
		ui->rep_wait = T_REPEAT_DELAY;
	} else if (ui->rep_key && (keys & ui->rep_key)) {
		if (ticks_since(ui->rep_t, now) >= ui->rep_wait) {
			edge |= ui->rep_key;
			ui->repeat = 1;
			ui->rep_t = now;
			ui->rep_wait = T_REPEAT;
		}
	} else {
		ui->rep_key = 0;
	}
	ui->keys = keys;
	ui->edge = edge;
}

static void goto_page(cg_ui *ui, int page)
{
	ui->page = page;
	ui->sel[page] = ID_NONE;
	ui->scroll[page] = 0;
	if (page == PAGE_LIBRARY && ui->in_game) {
		uint32_t order[FXCG100_ROM_SORT_MAX];
		uint32_t count = library_order(ui, order);

		ui->game_sel = fxcg100_rom_sort_find(order, count,
			ui->state->rom_source);
		library_reveal(ui);
	}
	build_rows(ui);
	want(ui, REDRAW_FULL);
}

static void open_settings(cg_ui *ui)
{
	if (ui->page == PAGE_SETTINGS || ui->page == PAGE_KEYS ||
			ui->page == PAGE_DEBUG)
		return;
	ui->settings_from = ui->page;
	goto_page(ui, PAGE_SETTINGS);
}

static void save_settings(cg_ui *ui)
{
	if (!ui->settings_dirty)
		return;
	ui->settings_dirty = 0;
	fxcg100_wait_keys_released();
	if (fxcg100_config_save(ui->state))
		message(ui, 0, "Settings saved", NULL, NULL);
	else
		message(ui, 1, "Could not save the settings", NULL, NULL);
	ui->keys = fxcg100_poll_app_keys();
	ui->edge = 0;
	ui->rep_key = 0;
}

static void leave_settings(cg_ui *ui)
{
	int dest = ui->settings_from;

	save_settings(ui);
	if (dest == PAGE_SETTINGS || dest == PAGE_KEYS || dest == PAGE_DEBUG)
		dest = ui->in_game ? PAGE_PAUSE : PAGE_LIBRARY;
	goto_page(ui, dest);
}

static void open_dialog(cg_ui *ui, int dialog)
{
	ui->dialog = dialog;
	ui->dialog_sel = 0;
	want(ui, REDRAW_FULL);
}

/* The next or previous of `count` choices, going round. */
static uint32_t cycle(uint32_t value, uint32_t count, int delta)
{
	if (value >= count)
		value = 0;
	return delta < 0 ? (value + count - 1) % count : (value + 1) % count;
}

/* Up and down on a list page: the next row that can be selected, stopping
 * at the ends. */
static void list_move(cg_ui *ui, int delta)
{
	int i = row_index(ui, ui->sel[ui->page]);

	if (i < 0)
		return;
	for (i += delta; i >= 0 && i < ui->nrows; i += delta)
		if (ui->rows[i].kind != RK_SECTION) {
			ui->sel[ui->page] = ui->rows[i].id;
			list_reveal(ui);
			want(ui, REDRAW_SELECTION);
			return;
		}
	/* Past the end: a fresh press goes round to the other end; a held key
	 * stops there, so holding it does not cycle through the list. */
	if (ui->repeat)
		return;
	for (i = delta > 0 ? 0 : ui->nrows - 1; i >= 0 && i < ui->nrows;
			i += delta)
		if (ui->rows[i].kind != RK_SECTION) {
			ui->sel[ui->page] = ui->rows[i].id;
			list_reveal(ui);
			want(ui, REDRAW_SELECTION);
			return;
		}
}

static void change_setting(cg_ui *ui, int id, int delta)
{
	fxcg100_menu_state *st = ui->state;

	switch (id) {
	case ID_SIZE:
		st->screen_scale = cycle(st->screen_scale, 3, delta);
		break;
	case ID_FILTER:
		st->screen_filter = cycle(st->screen_filter, 3, delta);
		break;
	case ID_VSYNC:
		st->lcd_sync = !st->lcd_sync;
		break;
	case ID_REFRESH:
		st->lcd_refresh = cycle(st->lcd_refresh, 7, delta);
		break;
	case ID_BACKLIGHT:
		st->backlight = cycle(st->backlight, 17, delta);
		fxcg100_lcd_set_backlight(st->backlight, st->backlight_pwm);
		break;
	case ID_COLOUR:
		st->theme = cycle(st->theme, CP_THEME_COUNT, delta);
		break;
	case ID_DARK_MODE:
		st->dark_mode = !st->dark_mode;
		break;
	case ID_FRAMESKIP:
		st->frameskip_type = cycle(st->frameskip_type, 3, delta);
		break;
	case ID_SKIP_N:
		if (delta < 0 && st->frameskip_value > 0)
			st->frameskip_value--;
		else if (delta > 0 && st->frameskip_value < 9)
			st->frameskip_value++;
		else if (st->frameskip_value > 9)
			st->frameskip_value = 9;
		else
			return;
		break;
	case ID_SKIP_PATTERN:
		st->frameskip_variation = cycle(st->frameskip_variation, 2, delta);
		break;
	case ID_LIMIT:
		st->frame_limit = !st->frame_limit;
		break;
	case ID_SHOW_FPS:
		st->show_fps = !st->show_fps;
		break;
	case ID_FF_MODE:
		st->fast_forward_mode = cycle(st->fast_forward_mode,
			FXCG100_FF_MODE_COUNT, delta);
		break;
	case ID_FF_SPEED:
		st->fast_forward_speed = cycle(st->fast_forward_speed,
			FXCG100_FF_SPEED_COUNT, delta);
		break;
	default:
		return;
	}
	ui->settings_dirty = 1;
	build_rows(ui);
	list_reveal(ui);
	want(ui, REDRAW_FULL);
}

static fxcg100_menu_result input_library(cg_ui *ui)
{
	uint32_t order[FXCG100_ROM_SORT_MAX];
	uint32_t count = library_order(ui, order), e = ui->edge;

	if (e & FXCG100_APPKEY_BACK) {
		if (ui->in_game)
			goto_page(ui, PAGE_PAUSE);
		return FXCG100_MENU_CONTINUE;
	}
	if (count == 0)
		return FXCG100_MENU_CONTINUE;
	if (e & FXCG100_APPKEY_SORT) {
		ui->state->game_sort = (ui->state->game_sort + 1u) %
			FXCG100_GAME_SORT_COUNT;
		ui->settings_dirty = 1;
		ui->game_sel = 0;
		ui->game_first = 0;
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_UP) {
		if (ui->game_sel > 0)
			ui->game_sel--;
		else if (!ui->repeat)
			ui->game_sel = count - 1;     /* round to the end */
		want(ui, REDRAW_SELECTION);
	} else if (e & FXCG100_APPKEY_DOWN) {
		if (ui->game_sel + 1 < count)
			ui->game_sel++;
		else if (!ui->repeat)
			ui->game_sel = 0;
		want(ui, REDRAW_SELECTION);
	} else if (e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT)) {
		uint32_t max = count > LIB_ROWS ? count - LIB_ROWS : 0;

		if (e & FXCG100_APPKEY_LEFT) {
			ui->game_sel = ui->game_sel > LIB_ROWS ? ui->game_sel - LIB_ROWS : 0;
			ui->game_first = ui->game_first > LIB_ROWS
				? ui->game_first - LIB_ROWS : 0;
		} else {
			ui->game_sel = ui->game_sel + LIB_ROWS < count
				? ui->game_sel + LIB_ROWS : count - 1;
			ui->game_first = ui->game_first + LIB_ROWS < max
				? ui->game_first + LIB_ROWS : max;
		}
		want(ui, REDRAW_SELECTION);
	} else if (e & FXCG100_APPKEY_EXE) {
		uint32_t rom = order[ui->game_sel < count ? ui->game_sel : 0];

		if (ui->in_game && rom == ui->state->rom_source)
			return FXCG100_MENU_RETURN;
		if (ui->in_game) {
			ui->switch_rom = rom;
			open_dialog(ui, DIALOG_SWITCH);
			return FXCG100_MENU_CONTINUE;
		}
		ui->state->rom_source = rom;
		return FXCG100_MENU_LOAD_GAME;
	}
	library_reveal(ui);
	return FXCG100_MENU_CONTINUE;
}

static void open_txt_browser(cg_ui *ui, int from_page)
{
	txt_reader *r = txt_reader_global();

	fxcg100_wait_keys_released();
	txt_reader_scan_dir(r);
	ui->txt_browser_sel = 0;
	ui->txt_browser_first = 0;
	ui->txt_browser_from = from_page;
	goto_page(ui, PAGE_TXT_BROWSER);
}

static void open_txt_reader_page(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();

	if (r->has_doc) {
		txt_reader_sync_storage(r);
		txt_reader_on_enter(r);
		goto_page(ui, PAGE_TXT_READER);
	} else {
		open_txt_browser(ui, ui->txt_direct ? -1 : PAGE_PAUSE);
	}
}

static fxcg100_menu_result input_txt_browser(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	uint32_t e = ui->edge;
	int total = txt_browser_total(r);

	if (e & FXCG100_APPKEY_BACK) {
		if (r->dir_depth > 0) {
			fxcg100_wait_keys_released();
			txt_reader_up_dir(r);
			ui->txt_browser_sel = 0;
			ui->txt_browser_first = 0;
			want(ui, REDRAW_FULL);
			return FXCG100_MENU_CONTINUE;
		}
		if (ui->txt_browser_from == PAGE_TXT_READER && r->has_doc) {
			txt_reader_on_enter(r);
			goto_page(ui, PAGE_TXT_READER);
			return FXCG100_MENU_CONTINUE;
		}
		if (ui->txt_direct)
			return FXCG100_MENU_RETURN;
		goto_page(ui, PAGE_PAUSE);
		return FXCG100_MENU_CONTINUE;
	}
	if (total == 0)
		return FXCG100_MENU_CONTINUE;
	if (e & FXCG100_APPKEY_UP) {
		if (ui->txt_browser_sel > 0)
			ui->txt_browser_sel--;
		else if (!ui->repeat)
			ui->txt_browser_sel = total - 1;
		want(ui, REDRAW_SELECTION);
	} else if (e & FXCG100_APPKEY_DOWN) {
		if (ui->txt_browser_sel + 1 < total)
			ui->txt_browser_sel++;
		else if (!ui->repeat)
			ui->txt_browser_sel = 0;
		want(ui, REDRAW_SELECTION);
	} else if (e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_PGUP)) {
		ui->txt_browser_sel = ui->txt_browser_sel > LIB_ROWS
			? ui->txt_browser_sel - LIB_ROWS : 0;
		ui->txt_browser_first = ui->txt_browser_first > LIB_ROWS
			? ui->txt_browser_first - LIB_ROWS : 0;
		want(ui, REDRAW_SELECTION);
	} else if (e & (FXCG100_APPKEY_RIGHT | FXCG100_APPKEY_PGDN)) {
		int max = total > LIB_ROWS ? total - LIB_ROWS : 0;

		ui->txt_browser_sel = ui->txt_browser_sel + LIB_ROWS < total
			? ui->txt_browser_sel + LIB_ROWS : total - 1;
		ui->txt_browser_first = ui->txt_browser_first + LIB_ROWS < max
			? ui->txt_browser_first + LIB_ROWS : max;
		want(ui, REDRAW_SELECTION);
	} else if (e & FXCG100_APPKEY_EXE) {
		if (r->dir_depth > 0 && ui->txt_browser_sel == 0) {
			fxcg100_wait_keys_released();
			txt_reader_up_dir(r);
			ui->txt_browser_sel = 0;
			ui->txt_browser_first = 0;
			want(ui, REDRAW_FULL);
			return FXCG100_MENU_CONTINUE;
		}
		int idx = ui->txt_browser_sel - (r->dir_depth > 0 ? 1 : 0);
		if (idx >= 0 && idx < (int)r->dir_count) {
			const txt_dir_entry *ent = &r->dir_entries[idx];

			if (ent->is_dir) {
				fxcg100_wait_keys_released();
				if (txt_reader_enter_dir(r, ent->name)) {
					ui->txt_browser_sel = 0;
					ui->txt_browser_first = 0;
					want(ui, REDRAW_FULL);
				} else {
					message(ui, 1, "Folder too deep", NULL, NULL);
				}
			} else {
				char rel[TXT_PATH_MAX];

				if (r->dir_path[0])
					snprintf(rel, sizeof(rel), "%s\\%s", r->dir_path, ent->name);
				else
					snprintf(rel, sizeof(rel), "%s", ent->name);
				fxcg100_wait_keys_released();
				if (txt_reader_open(r, rel)) {
					goto_page(ui, PAGE_TXT_READER);
				} else {
					message(ui, 1, "Could not open file", NULL, NULL);
				}
			}
		}
		return FXCG100_MENU_CONTINUE;
	}
	txt_browser_reveal(ui, r);
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result input_txt_reader(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	uint32_t e = ui->edge;
	int d;

	if (e & FXCG100_APPKEY_BACK) {
		txt_reader_flush(r);
		if (ui->txt_direct)
			return FXCG100_MENU_RETURN;
		goto_page(ui, PAGE_PAUSE);
		return FXCG100_MENU_CONTINUE;
	}
	for (d = 0; d < 10; d++) {
		if (e & FXCG100_APPKEY_DIGIT(d)) {
			char pct_str[8];

			txt_reader_jump_percent(r, d * 10);
			snprintf(pct_str, sizeof(pct_str), "%d%%", d * 10);
			message(ui, 0, "Jumped to %s (DEL to undo)", pct_str, NULL);
			want(ui, REDRAW_FULL);
			return FXCG100_MENU_CONTINUE;
		}
	}
	if (e & FXCG100_APPKEY_UP) {
		txt_reader_scroll_lines(r, -1);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_DOWN) {
		txt_reader_scroll_lines(r, 1);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_PGUP) {
		txt_reader_page_up(r);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_PGDN) {
		txt_reader_page_down(r);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_LEFT) {
		if (r->wrap)
			txt_reader_page_up(r);
		else
			txt_reader_pan(r, -8);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_RIGHT) {
		if (r->wrap)
			txt_reader_page_down(r);
		else
			txt_reader_pan(r, 8);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_DEL) {
		if (txt_reader_undo_jump(r)) {
			message(ui, 0, "Jump undone", NULL, NULL);
			want(ui, REDRAW_FULL);
		}
	} else if (e & FXCG100_APPKEY_SORT) {
		int rc = txt_reader_add_bookmark(r);

		if (rc == TXT_BM_ADDED)
			message(ui, 0, "Bookmark added", NULL, NULL);
		else if (rc == TXT_BM_EXISTS)
			message(ui, 0, "Already bookmarked", NULL, NULL);
		else if (rc == TXT_BM_FULL)
			message(ui, 1, "Bookmarks full (32 max)", NULL, NULL);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_EXE) {
		ui->txt_opt_jump = txt_reader_percent(r);
		open_dialog(ui, DIALOG_TXT_OPTIONS);
	}
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result input_txt_options(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	uint32_t e = ui->edge;
	int d;

	if (e & FXCG100_APPKEY_BACK) {
		ui->dialog = DIALOG_NONE;
		want(ui, REDRAW_FULL);
		return FXCG100_MENU_CONTINUE;
	}
	if (e & FXCG100_APPKEY_UP) {
		ui->dialog_sel = (ui->dialog_sel + 4) % 5;
		want(ui, REDRAW_FULL);
		return FXCG100_MENU_CONTINUE;
	}
	if (e & FXCG100_APPKEY_DOWN) {
		ui->dialog_sel = (ui->dialog_sel + 1) % 5;
		want(ui, REDRAW_FULL);
		return FXCG100_MENU_CONTINUE;
	}
	if (ui->dialog_sel == 0) {
		for (d = 0; d < 10; d++) {
			if (e & FXCG100_APPKEY_DIGIT(d)) {
				ui->txt_opt_jump = d * 10;
				want(ui, REDRAW_FULL);
			}
		}
		if (e & FXCG100_APPKEY_LEFT) {
			ui->txt_opt_jump = ui->txt_opt_jump > 5 ? ui->txt_opt_jump - 5 : 0;
			want(ui, REDRAW_FULL);
		} else if (e & FXCG100_APPKEY_RIGHT) {
			ui->txt_opt_jump = ui->txt_opt_jump < 95 ? ui->txt_opt_jump + 5 : 100;
			want(ui, REDRAW_FULL);
		} else if (e & FXCG100_APPKEY_EXE) {
			char pct_str[8];

			txt_reader_jump_percent(r, ui->txt_opt_jump);
			ui->dialog = DIALOG_NONE;
			snprintf(pct_str, sizeof(pct_str), "%d%%", ui->txt_opt_jump);
			message(ui, 0, "Jumped to %s (DEL to undo)", pct_str, NULL);
			want(ui, REDRAW_FULL);
		}
	} else if (ui->dialog_sel == 1) {
		if (e & FXCG100_APPKEY_EXE) {
			ui->txt_bm_sel = 0;
			ui->txt_bm_first = 0;
			ui->dialog = DIALOG_TXT_BOOKMARKS;
			want(ui, REDRAW_FULL);
		}
	} else if (ui->dialog_sel == 2) {
		if (e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT | FXCG100_APPKEY_EXE)) {
			txt_reader_set_font(r, r->font_idx == TXT_FONT_6X12
				? TXT_FONT_8X14 : TXT_FONT_6X12);
			want(ui, REDRAW_FULL);
		}
	} else if (ui->dialog_sel == 3) {
		if (e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT | FXCG100_APPKEY_EXE)) {
			txt_reader_set_wrap(r, !r->wrap);
			want(ui, REDRAW_FULL);
		}
	} else if (ui->dialog_sel == 4) {
		if (e & FXCG100_APPKEY_EXE) {
			ui->dialog = DIALOG_NONE;
			open_txt_browser(ui, PAGE_TXT_READER);
		}
	}
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result input_txt_bookmarks(cg_ui *ui)
{
	txt_reader *r = txt_reader_global();
	uint32_t marks[TXT_MAX_BOOKMARKS];
	int count = (int)txt_reader_get_bookmarks(r, marks, TXT_MAX_BOOKMARKS);
	int can_add = count < (int)TXT_MAX_BOOKMARKS;
	int total = count + (can_add ? 1 : 0);
	uint32_t e = ui->edge;

	if (e & FXCG100_APPKEY_BACK) {
		ui->dialog = DIALOG_TXT_OPTIONS;
		ui->dialog_sel = 1;
		want(ui, REDRAW_FULL);
		return FXCG100_MENU_CONTINUE;
	}
	if (total == 0)
		return FXCG100_MENU_CONTINUE;
	if (e & FXCG100_APPKEY_UP) {
		if (ui->txt_bm_sel > 0)
			ui->txt_bm_sel--;
		else if (!ui->repeat)
			ui->txt_bm_sel = total - 1;
		txt_bm_reveal(ui, total);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_DOWN) {
		if (ui->txt_bm_sel + 1 < total)
			ui->txt_bm_sel++;
		else if (!ui->repeat)
			ui->txt_bm_sel = 0;
		txt_bm_reveal(ui, total);
		want(ui, REDRAW_FULL);
	} else if ((e & FXCG100_APPKEY_DEL) && ui->txt_bm_sel < count) {
		txt_reader_remove_bookmark(r, (unsigned)ui->txt_bm_sel);
		count--;
		total = count + (count < (int)TXT_MAX_BOOKMARKS ? 1 : 0);
		txt_bm_reveal(ui, total);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_EXE) {
		if (ui->txt_bm_sel < count) {
			uint32_t target = marks[ui->txt_bm_sel];
			int pct = r->doc_size > 0
				? (int)(((uint64_t)target * 100u) / r->doc_size) : 0;
			char pct_str[8];

			if (pct > 100)
				pct = 100;
			txt_reader_jump_offset(r, target, 1);
			ui->dialog = DIALOG_NONE;
			snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
			message(ui, 0, "Jumped to %s (DEL to undo)", pct_str, NULL);
			want(ui, REDRAW_FULL);
		} else {
			int rc = txt_reader_add_bookmark(r);

			ui->dialog = DIALOG_NONE;
			if (rc == TXT_BM_ADDED)
				message(ui, 0, "Bookmark added", NULL, NULL);
			else if (rc == TXT_BM_EXISTS)
				message(ui, 0, "Already bookmarked", NULL, NULL);
			else if (rc == TXT_BM_FULL)
				message(ui, 1, "Bookmarks full (32 max)", NULL, NULL);
			want(ui, REDRAW_FULL);
		}
	}
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result input_pause(cg_ui *ui)
{
	uint32_t e = ui->edge;
	const ui_row *r = selected_row(ui);
	int id = r ? r->id : ID_NONE, d;

	if (e & FXCG100_APPKEY_BACK)
		return FXCG100_MENU_RETURN;
	for (d = 0; d < 10; d++)
		if (e & FXCG100_APPKEY_DIGIT(d)) {
			ui->state->savestate_slot = (uint32_t)d;
			want(ui, REDRAW_FULL);
		}
	if (e & FXCG100_APPKEY_UP)
		list_move(ui, -1);
	else if (e & FXCG100_APPKEY_DOWN)
		list_move(ui, 1);
	else if ((e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT)) &&
			(id == ID_SAVE || id == ID_LOAD)) {
		ui->state->savestate_slot = cycle(ui->state->savestate_slot, 10,
			(e & FXCG100_APPKEY_LEFT) ? -1 : 1);
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_EXE) {
		switch (id) {
		case ID_RESUME:   return FXCG100_MENU_RETURN;
		case ID_SAVE:
			fxcg100_wait_keys_released();
			if (fxcg100_savestate_exists(ui->state->savestate_slot % 10u)) {
				ui->keys = fxcg100_poll_app_keys();
				open_dialog(ui, DIALOG_SAVE);
				break;
			}
			return FXCG100_MENU_SAVE_STATE;
		case ID_LOAD:     return FXCG100_MENU_LOAD_STATE;
		case ID_READER:   open_txt_reader_page(ui); break;
		case ID_SETTINGS: open_settings(ui); break;
		case ID_GAMES:    goto_page(ui, PAGE_LIBRARY); break;
		case ID_RESTART:  open_dialog(ui, DIALOG_RESTART); break;
		case ID_EXIT:     open_dialog(ui, DIALOG_EXIT); break;
		default:          break;
		}
	}
	return FXCG100_MENU_CONTINUE;
}

static void input_settings(cg_ui *ui)
{
	uint32_t e = ui->edge;
	const ui_row *r = selected_row(ui);

	if (e & FXCG100_APPKEY_BACK) {
		leave_settings(ui);
		return;
	}
	if (!r)
		return;
	if (e & FXCG100_APPKEY_UP)
		list_move(ui, -1);
	else if (e & FXCG100_APPKEY_DOWN)
		list_move(ui, 1);
	else if (e & (FXCG100_APPKEY_LEFT | FXCG100_APPKEY_RIGHT))
		change_setting(ui, r->id, (e & FXCG100_APPKEY_LEFT) ? -1 : 1);
	else if (e & FXCG100_APPKEY_EXE) {
		if (r->id == ID_KEYMAP)
			goto_page(ui, PAGE_KEYS);
		else if (r->id == ID_DEBUG)
			goto_page(ui, PAGE_DEBUG);
		else if (r->id == ID_VSYNC || r->id == ID_DARK_MODE ||
				r->id == ID_LIMIT || r->id == ID_SHOW_FPS || r->id == ID_FF_MODE)
			change_setting(ui, r->id, 1);
	}
}

/* The binding other than `except` that uses a key, or -1. */
static int key_user(cg_ui *ui, int except, uint16_t key)
{
	int i;

	for (i = 0; i < BINDING_COUNT; i++)
		if (i != except && *binding_slot(ui, i) == key)
			return i;
	return -1;
}

/* Give binding i the key. If another binding already uses that key, clear
 * the previous binding to None (0). */
static void bind(cg_ui *ui, int i, uint16_t key)
{
	uint16_t *slot = binding_slot(ui, i);
	int other = key_user(ui, i, key);

	if (key == *slot)
		return;
	if (other >= 0) {
		*binding_slot(ui, other) = 0;
		message(ui, 0, "%s cleared (None)", bindings[other].name, NULL);
	}
	*slot = key;
	ui->settings_dirty = 1;
}

static void input_keys(cg_ui *ui)
{
	uint32_t e = ui->edge;
	const ui_row *r = selected_row(ui);

	if (e & FXCG100_APPKEY_BACK) {
		goto_page(ui, PAGE_SETTINGS);
		return;
	}
	if (!r)
		return;
	if (e & FXCG100_APPKEY_UP) {
		list_move(ui, -1);
	} else if (e & FXCG100_APPKEY_DOWN) {
		list_move(ui, 1);
	} else if (e & FXCG100_APPKEY_EXE) {
		if (r->id == ID_RESET_KEYS) {
			fxcg100_keymap_defaults(ui->state->keymap);
			fxcg100_hotkey_defaults(ui->state->hotkey_map);
			ui->settings_dirty = 1;
			message(ui, 0, "Default keys restored", NULL, NULL);
			want(ui, REDRAW_FULL);
		} else {
			ui->capture = r->id - ID_BIND0;
			ui->capture_armed = 0;
			open_dialog(ui, DIALOG_CAPTURE);
		}
	} else if ((e & FXCG100_APPKEY_DEL) && r->id >= ID_BIND0 &&
			r->id < ID_BIND0 + BINDING_COUNT && r->key) {
		*binding_slot(ui, r->id - ID_BIND0) = 0;
		ui->settings_dirty = 1;
		want(ui, REDRAW_FULL);
	}
}

static void input_capture(cg_ui *ui)
{
	uint16_t key = fxcg100_poll_physical_key();

	if (!ui->capture_armed) {
		/* Ignore whatever was still held when the box opened. */
		ui->capture_armed = key == 0;
		return;
	}
	if (!key)
		return;
	if (key != FXCG100_PHYSKEY_ON) {
		if (!fxcg100_key_bindable(key))
			return;
		bind(ui, ui->capture, key);
	}
	ui->dialog = DIALOG_NONE;
	ui->rep_key = 0;
	ui->keys = fxcg100_poll_app_keys();
	want(ui, REDRAW_FULL);
}

static fxcg100_menu_result input_dialog(cg_ui *ui)
{
	uint32_t e = ui->edge;

	if (ui->dialog == DIALOG_TXT_OPTIONS)
		return input_txt_options(ui);
	if (ui->dialog == DIALOG_TXT_BOOKMARKS)
		return input_txt_bookmarks(ui);
	if (ui->dialog == DIALOG_EXIT && (e & FXCG100_APPKEY_HOME))
		return FXCG100_MENU_QUIT;
	if (e & FXCG100_APPKEY_BACK) {
		ui->dialog = DIALOG_NONE;
		want(ui, REDRAW_FULL);
	} else if ((e & (FXCG100_APPKEY_UP | FXCG100_APPKEY_DOWN)) && !ui->repeat) {
		ui->dialog_sel = !ui->dialog_sel;
		want(ui, REDRAW_FULL);
	} else if (e & FXCG100_APPKEY_EXE) {
		int dialog = ui->dialog;

		ui->dialog = DIALOG_NONE;
		want(ui, REDRAW_FULL);
		if (!ui->dialog_sel)
			return FXCG100_MENU_CONTINUE;
		if (dialog == DIALOG_EXIT)
			return FXCG100_MENU_QUIT;
		if (dialog == DIALOG_RESTART)
			return FXCG100_MENU_RESET;
		if (dialog == DIALOG_SAVE)
			return FXCG100_MENU_SAVE_STATE;
		ui->state->rom_source = ui->switch_rom;
		return FXCG100_MENU_LOAD_GAME;
	}
	return FXCG100_MENU_CONTINUE;
}

static fxcg100_menu_result input(cg_ui *ui)
{
	uint32_t e = ui->edge;

	if (ui->dialog)
		return input_dialog(ui);
	/* Keys that work on every page. */
	if (e & FXCG100_APPKEY_HOME) {
		open_dialog(ui, DIALOG_EXIT);
		return FXCG100_MENU_CONTINUE;
	}
	if ((e & FXCG100_APPKEY_ON) && ui->in_game)
		return FXCG100_MENU_RETURN;
	if (e & FXCG100_APPKEY_SETTINGS) {
		open_settings(ui);
		return FXCG100_MENU_CONTINUE;
	}
	switch (ui->page) {
	case PAGE_LIBRARY:     return input_library(ui);
	case PAGE_PAUSE:       return input_pause(ui);
	case PAGE_SETTINGS:    input_settings(ui); break;
	case PAGE_KEYS:        input_keys(ui); break;
	case PAGE_TXT_BROWSER: return input_txt_browser(ui);
	case PAGE_TXT_READER:  return input_txt_reader(ui);
	default:
		if (e & FXCG100_APPKEY_BACK)
			goto_page(ui, PAGE_SETTINGS);
		else if (e & FXCG100_APPKEY_UP)
			list_move(ui, -1);
		else if (e & FXCG100_APPKEY_DOWN)
			list_move(ui, 1);
		break;
	}
	return FXCG100_MENU_CONTINUE;
}

/* ---- the interface's life ------------------------------------------------- */

static void cg_ui_begin(cg_ui *ui, fxcg100_menu_state *state, int in_game,
	const fxcg100_debug_info *debug)
{
	int direct = open_reader_direct;
	int resume = !direct && ui->resume && in_game && ui->state == state;

	open_reader_direct = 0;
	ui->state = state;
	ui->debug = debug;
	ui->in_game = in_game;
	ui->dialog = DIALOG_NONE;
	ui->settings_dirty = 0;
	ui->progress = NULL;
	ui->resume = 0;
	ui->txt_direct = direct && in_game;
	ui->txt_hotkey_armed = 0;
	if (state->game_sort >= FXCG100_GAME_SORT_COUNT)
		state->game_sort = FXCG100_GAME_SORT_NAME;
	if (state->backlight > 16)
		state->backlight = 0;
	if (state->fast_forward_mode >= FXCG100_FF_MODE_COUNT)
		state->fast_forward_mode = FXCG100_FF_MODE_HOLD;
	if (state->fast_forward_speed >= FXCG100_FF_SPEED_COUNT)
		state->fast_forward_speed = FXCG100_FF_SPEED_UNLIMITED;
	if (resume) {
		ui->message_t0 = rtc_ticks();
	} else {
		uint32_t order[FXCG100_ROM_SORT_MAX];
		uint32_t count = library_order(ui, order);

		ui->message[0] = 0;
		ui->page = in_game ? PAGE_PAUSE : PAGE_LIBRARY;
		ui->sel[PAGE_PAUSE] = ID_RESUME;
		ui->game_sel = fxcg100_rom_sort_find(order, count, state->rom_source);
		ui->game_first = 0;
		library_reveal(ui);
	}
	if (state->theme >= CP_THEME_COUNT)
		state->theme = 0;
	if (state->game_sort >= FXCG100_GAME_SORT_COUNT)
		state->game_sort = FXCG100_GAME_SORT_NAME;
	if (state->backlight > 16)
		state->backlight = 0;
	if (state->fast_forward_mode >= FXCG100_FF_MODE_COUNT)
		state->fast_forward_mode = FXCG100_FF_MODE_HOLD;
	if (state->fast_forward_speed >= FXCG100_FF_SPEED_COUNT)
		state->fast_forward_speed = FXCG100_FF_SPEED_UNLIMITED;
	if (ui->txt_direct)
		open_txt_reader_page(ui);
	/* Whatever opened the menu is still held: do not let it act here. */
	ui->keys = fxcg100_poll_app_keys();
	ui->edge = 0;
	ui->rep_key = 0;
	ui->shown = 0;
	ui->redraw = REDRAW_FULL;
	build_rows(ui);
}

/* One round of input and drawing. Returns FXCG100_MENU_CONTINUE until the
 * player chooses something main() must act on. */
static fxcg100_menu_result cg_ui_step(cg_ui *ui)
{
	fxcg100_menu_result result = FXCG100_MENU_CONTINUE;

	read_keys(ui);
	if (ui->dialog == DIALOG_CAPTURE) {
		input_capture(ui);
	} else {
		if (ui->in_game && ui->dialog == DIALOG_NONE &&
				(ui->page == PAGE_TXT_READER || ui->page == PAGE_TXT_BROWSER)) {
			uint16_t hk = ui->state->hotkey_map[FXCG100_HOTKEY_TXT_READER];

			if (hk) {
				uint16_t phys = fxcg100_poll_physical_key();

				if (!ui->txt_hotkey_armed) {
					if (phys != hk)
						ui->txt_hotkey_armed = 1;
				} else if (phys == hk) {
					result = FXCG100_MENU_RETURN;
				}
			}
		}
		if (result == FXCG100_MENU_CONTINUE && ui->edge) {
			/* A result in the footer goes at the next key, which then acts as
			 * usual; one this key itself produces stays. */
			if (ui->message[0] && !ui->dialog) {
				ui->message[0] = 0;
				want(ui, ui->page == PAGE_TXT_READER
					? REDRAW_FULL : REDRAW_FOOTER);
			}
			build_rows(ui);
			result = input(ui);
		}
	}
	if (result != FXCG100_MENU_CONTINUE) {
		txt_reader_flush(txt_reader_global());
		if (ui->settings_dirty) {
			fxcg100_wait_keys_released();
			fxcg100_config_save(ui->state);
			ui->settings_dirty = 0;
		}
		return result;
	}
#ifdef CGBA_KEY_TEST
	/* Key test build: the footer shows the keyboard matrix as the hardware
	 * reports it (the six KEYSC words gint scans) and gpSP's own key bits,
	 * whenever they change. */
	{
		static uint16_t last[6];
		static uint32_t last_app = 0xffffffffu;
		volatile uint16_t *keysc = (volatile uint16_t *)0xa44b0000;
		uint16_t now[6];
		int i, changed = ui->keys != last_app;

		for (i = 0; i < 6; i++) {
			now[i] = keysc[i];
			changed |= now[i] != last[i];
			last[i] = now[i];
		}
		last_app = ui->keys;
		if (changed && !ui->dialog) {
			snprintf(ui->message, sizeof(ui->message),
				"%04X %04X %04X %04X %04X %04X  %05X", now[0], now[1], now[2],
				now[3], now[4], now[5], (unsigned)(ui->keys & 0xfffffu));
			ui->message_error = 0;
			ui->message_t0 = rtc_ticks();
			want(ui, REDRAW_FOOTER);
		}
	}
#endif
	if (ui->message[0] && !ui->dialog &&
			ticks_since(ui->message_t0, rtc_ticks()) >= T_MESSAGE) {
		ui->message[0] = 0;
		want(ui, ui->page == PAGE_TXT_READER ? REDRAW_FULL : REDRAW_FOOTER);
	}
	if (ui->redraw)
		render(ui);
	return FXCG100_MENU_CONTINUE;
}

void cg_ui_show_progress(const char *text_line)
{
	cg_ui *ui = &ui_main;

	if (!ui->state)
		return;
	ui->progress = text_line;
	ui->dialog = DIALOG_NONE;
	draw(ui);
	fxcg100_lcd_update();
	ui->progress = NULL;
}

void cg_ui_set_result(const char *text_line, int error)
{
	snprintf(ui_main.message, sizeof(ui_main.message), "%s",
		text_line ? text_line : "");
	ui_main.message_error = error;
	ui_main.resume = 1;
}

/* The game frame, darkened, where the display mode puts it. */
static void draw_dim_frame(const cg_ui *ui, const uint16_t *frame,
	uint32_t scale)
{
	static const struct { int x, y, w, h; } at[3] = {
		{ (DWIDTH - 240) / 2, (DHEIGHT - 160) / 2, 240, 160 },
		{ (DWIDTH - 320) / 2, ((DHEIGHT - 212) / 2) & ~3, 320, 212 },
		{ (DWIDTH - W) / 2, ((DHEIGHT - H) / 2) & ~3, W, H },
	};
	int m = scale < 3 ? (int)scale : 0, x, y;

	(void)ui;
	memset(gint_vram, 0, (size_t)DWIDTH * DHEIGHT * sizeof(uint16_t));
	for (y = 0; y < at[m].h; y++) {
		const uint16_t *src = frame + (y * 160 / at[m].h) * 240;
		uint16_t *dst = gint_vram + (at[m].y + y) * DWIDTH + at[m].x;

		for (x = 0; x < at[m].w; x++)
			dst[x] = cp_blend(src[x * 240 / at[m].w], 0x0000, DIM, 256);
	}
}

void cg_ui_status(const fxcg100_menu_state *state, const char *line1,
	const char *line2, const uint16_t *frame)
{
	cg_ui ui;
	int w, x, y, h = line2 ? 64 : 44;

	memset(&ui, 0, sizeof(ui));
	ui.state = (fxcg100_menu_state *)state;
	ui.t = *cp_theme_get_mode(state ? state->theme : 0,
		state ? state->dark_mode : 1);
	cp_surface_init(&ui.s, gint_vram, DWIDTH, DHEIGHT, CP_SURFACE_UPRIGHT);
	ui.s.ox = SCR_X;
	ui.s.oy = SCR_Y;
	if (frame)
		draw_dim_frame(&ui, frame, state ? state->screen_scale : 0);
	else
		cp_fill(&ui.s, -SCR_X, -SCR_Y, DWIDTH, DHEIGHT, ui.t.surface);
	if (!line1)
		line1 = "";
	w = text_w(F_BODY, line1) + 24;
	if (line2 && w < text_w(F_SMALL, line2) + 24)
		w = text_w(F_SMALL, line2) + 24;
	if (w < 200)
		w = 200;
	if (w > W - 24)
		w = W - 24;
	x = (W - w) / 2;
	y = (H - h) / 2;
	rect(&ui, x, y, w, h, 8, ui.t.container_high);
	text_fit(&ui, F_BODY, x + 12, mid_base(F_BODY, line2 ? y + 16 : y + h / 2),
		w - 24, line1, ui.t.on_surface);
	if (line2)
		text_fit(&ui, F_SMALL, x + 12, mid_base(F_SMALL, y + 36), w - 24, line2,
			ui.t.on_surface_variant);
	fxcg100_lcd_update();
}

/* ---- the fxcg100_menu contract -------------------------------------------- */

void fxcg100_menu_open_reader(void)
{
	open_reader_direct = 1;
}

void fxcg100_menu_init(fxcg100_menu_state *state)
{
	memset(state, 0, sizeof(*state));
	state->frameskip_type = 1;
	state->frameskip_value = 1;
	state->frame_limit = 1;
	state->dark_mode = 1;
	fxcg100_keymap_defaults(state->keymap);
	fxcg100_hotkey_defaults(state->hotkey_map);
	state->random_lfsr = 0x00c6ba5eu;
	fxcg100_config_load(state);
	if (state->screen_scale > 2)
		state->screen_scale = 0;
	if (state->screen_filter > 2)
		state->screen_filter = 0;
	if (state->theme >= CP_THEME_COUNT)
		state->theme = 0;
	if (state->game_sort >= FXCG100_GAME_SORT_COUNT)
		state->game_sort = FXCG100_GAME_SORT_NAME;
	if (state->backlight > 16)
		state->backlight = 0;
	if (state->fast_forward_mode >= FXCG100_FF_MODE_COUNT)
		state->fast_forward_mode = FXCG100_FF_MODE_HOLD;
	if (state->fast_forward_speed >= FXCG100_FF_SPEED_COUNT)
		state->fast_forward_speed = FXCG100_FF_SPEED_UNLIMITED;
	memset(&ui_main, 0, sizeof(ui_main));
	open_reader_direct = 0;
	txt_reader_init(txt_reader_global());
}

/* main() calls this with frame 0 before any game is loaded, and with the
 * running frame number when the menu is opened during play. */
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
	uint32_t frame, uint32_t last_hash, const fxcg100_debug_info *debug)
{
	fxcg100_menu_result result;

	(void)last_hash;
	cg_ui_begin(&ui_main, state, frame != 0, debug);
	for (;;) {
		result = cg_ui_step(&ui_main);
		if (result != FXCG100_MENU_CONTINUE)
			return result;
		for (volatile unsigned i = 0; i < 12000; i++)
			;
	}
}

int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame)
{
	uint32_t period;

	if (state->frameskip_type == 2 || state->frameskip_value == 0)
		return 1;
	period = state->frameskip_value + 1;
	if (state->frameskip_variation) {
		state->random_lfsr = state->random_lfsr * 1664525u + 1013904223u;
		return (state->random_lfsr % period) == 0;
	}
	return (frame % period) == 1;
}
