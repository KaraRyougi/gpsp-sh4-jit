#ifndef CGBA_CLASSPAD_LAYOUT_H
#define CGBA_CLASSPAD_LAYOUT_H

/* fx-CP400 (ClassPad II) screen layout: where the GBA picture goes, where the
 * touch keys go, and how a touch point becomes GBA buttons.
 *
 * Everything here is pure integer geometry with no gint dependency, so the
 * presenter (gint-gpsp/src/classpad_platform.c) and the host unit test
 * (tests/classpad_layout_test.c) share one definition.
 *
 * Two coordinate systems:
 *   panel  the R61523 as wired: 320 columns x 528 rows, origin top-left when
 *          the calculator is held upright (keypad below the screen).
 *   user   what the player sees. Portrait is the panel itself (320x528).
 *          Landscape is 528x320 with the calculator turned a quarter turn:
 *          "keys right" turns it counter-clockwise (keypad under the right
 *          hand), "keys left" turns it clockwise.
 *
 * Scale choices (docs/classpad-port.md has the reasoning):
 *   portrait   1:1  240x160      exact pixels, wide borders
 *   portrait   4:3  320x212      fills the 320 px width, aspect-true 3->4
 *   landscape  1:1  240x160      exact pixels, in the 1.5x layout
 *   landscape  1.5x 360x240      exact 2->3, leaves room for a full touch pad
 *   landscape  2x   480x320      exact integer, fills the 320 px height
 */

#include <stdint.h>

#define CP_PANEL_W 320
#define CP_PANEL_H 528

enum {
	CP_VIEW_PORTRAIT_1X = 0,
	CP_VIEW_PORTRAIT_43,
	CP_VIEW_LANDSCAPE_15X,
	CP_VIEW_LANDSCAPE_2X,
	CP_VIEW_LANDSCAPE_1X,      /* added later; the values are saved in the config */
	CP_VIEW_COUNT
};

enum {
	CP_ROTATION_KEYS_RIGHT = 0,
	CP_ROTATION_KEYS_LEFT,
	CP_ROTATION_COUNT
};

/* GBA KEYINPUT bit positions (same values as FXCG100_GBA_BUTTON_*). */
#define CP_GBA_A      0x001u
#define CP_GBA_B      0x002u
#define CP_GBA_SELECT 0x004u
#define CP_GBA_START  0x008u
#define CP_GBA_RIGHT  0x010u
#define CP_GBA_LEFT   0x020u
#define CP_GBA_UP     0x040u
#define CP_GBA_DOWN   0x080u
#define CP_GBA_R      0x100u
#define CP_GBA_L      0x200u
#define CP_GBA_DPAD   (CP_GBA_UP | CP_GBA_DOWN | CP_GBA_LEFT | CP_GBA_RIGHT)

typedef struct cp_rect {
	int16_t x, y, w, h;
} cp_rect;

/* What a touch key looks like. The shape is only appearance: what registers
 * a touch is the key's `hit` rectangle. */
enum {
	CP_KEY_DPAD = 0,       /* one-piece cross; frame is its bounding square */
	CP_KEY_ROUND,          /* A, B */
	CP_KEY_SHOULDER_L,     /* tab flush with the left screen edge */
	CP_KEY_SHOULDER_R,     /* tab flush with the right screen edge */
	CP_KEY_PILL,           /* Select, Start; caption drawn below the pill */
	CP_KEY_TAB             /* narrow-strip key, caption inside */
};

/* A touch key, in the coordinates the player sees.
 *
 *   frame  what is drawn
 *   hit    what registers a touch; contains the frame and extends past it
 *          where there is room (not for the shoulder keys, which register
 *          exactly where they are drawn). Hit rectangles never overlap each other or
 *          the picture, and the space between them belongs to no key - in
 *          particular the gap between the D-pad and B in portrait, so a
 *          thumb landing between them presses neither.
 *
 * Sizes follow what works on phones for the same job: at this panel's
 * ~128 ppi the 144 px D-pad is about 29 mm and a 64 px button about 13 mm. */
typedef struct cp_key {
	cp_rect frame;
	cp_rect hit;
	uint16_t mask;         /* CP_GBA_* bits; all four directions for a D-pad */
	uint8_t shape;
	char label[7];
} cp_key;

typedef struct cp_layout {
	uint16_t user_w, user_h;
	cp_rect image;         /* user coordinates of the scaled GBA picture */
	const cp_key *keys;
	unsigned key_count;
} cp_layout;

static inline int cp_view_is_landscape(unsigned view)
{
	return view == CP_VIEW_LANDSCAPE_15X || view == CP_VIEW_LANDSCAPE_2X ||
		view == CP_VIEW_LANDSCAPE_1X;
}

/* The picture sizes of an orientation, smallest first: two in portrait
 * (1:1, fit width), three in landscape (1:1, 1.5x, 2x). */
static inline unsigned cp_view_size_count(int landscape)
{
	return landscape ? 3u : 2u;
}

static inline unsigned cp_view_size(unsigned view)
{
	switch (view) {
	case CP_VIEW_PORTRAIT_43:
	case CP_VIEW_LANDSCAPE_15X:
		return 1;
	case CP_VIEW_LANDSCAPE_2X:
		return 2;
	default:
		return 0;
	}
}

static inline unsigned cp_view_make(int landscape, unsigned size)
{
	static const uint8_t views[2][3] = {
		{ CP_VIEW_PORTRAIT_1X, CP_VIEW_PORTRAIT_43, CP_VIEW_PORTRAIT_43 },
		{ CP_VIEW_LANDSCAPE_1X, CP_VIEW_LANDSCAPE_15X, CP_VIEW_LANDSCAPE_2X },
	};

	return views[landscape ? 1 : 0][size > 2 ? 2 : size];
}

/* The same picture the other way up: 1:1 stays 1:1, and the larger sizes
 * go to the largest of the other orientation. */
static inline unsigned cp_view_turned(unsigned view)
{
	int landscape = cp_view_is_landscape(view);

	if (cp_view_size(view) == 0)
		return cp_view_make(!landscape, 0);
	return cp_view_make(!landscape, landscape ? 1 : 2);
}

/* The Resize icon: the next size of this orientation, round and round. */
static inline unsigned cp_view_resized(unsigned view)
{
	int landscape = cp_view_is_landscape(view);

	return cp_view_make(landscape,
		(cp_view_size(view) + 1) % cp_view_size_count(landscape));
}

/* Portrait. Both scales use the same pad, in the same place, so switching
 * the picture size never moves a key: the picture ends at row 212 either way
 * (1:1 sits against the bottom of that space), the shoulder keys come 24 px
 * under it and the D-pad and A 24 px under them. A shoulder key registers
 * exactly where it is drawn and nowhere else, which leaves 12 px between it
 * and the ranges of the D-pad and A below. */
static const cp_key cp_keys_portrait[] = {
	{ {   0, 236, 112,  40 }, {   0, 236, 112,  40 },
	  CP_GBA_L, CP_KEY_SHOULDER_L, "L" },
	{ { 208, 236, 112,  40 }, { 208, 236, 112,  40 },
	  CP_GBA_R, CP_KEY_SHOULDER_R, "R" },
	{ {   2, 300, 144, 144 }, {   0, 288, 152, 166 },
	  CP_GBA_DPAD, CP_KEY_DPAD, "" },
	{ { 180, 366,  64,  64 }, { 168, 354,  80,  88 },
	  CP_GBA_B, CP_KEY_ROUND, "B" },
	{ { 252, 302,  64,  64 }, { 248, 288,  72,  88 },
	  CP_GBA_A, CP_KEY_ROUND, "A" },
	{ {  88, 468,  64,  32 }, {  80, 458,  76,  64 },
	  CP_GBA_SELECT, CP_KEY_PILL, "Select" },
	{ { 168, 468,  64,  32 }, { 164, 458,  76,  64 },
	  CP_GBA_START, CP_KEY_PILL, "Start" },
};

/* Landscape 1.5x: picture flush top-right (360x240). L/R and the D-pad sit
 * in the 168 px column on the left, the D-pad high in it, where the left
 * thumb rests when the calculator is held by its ends, Select/Start and B/A in the 80 px strip
 * under the picture. */
static const cp_key cp_keys_landscape_15x[] = {
	{ {   4,   6,  76,  40 }, {   4,   6,  76,  40 },
	  CP_GBA_L, CP_KEY_TAB, "L" },
	{ {  88,   6,  76,  40 }, {  88,   6,  76,  40 },
	  CP_GBA_R, CP_KEY_TAB, "R" },
	{ {  12,  72, 144, 144 }, {   0,  56, 168, 176 },
	  CP_GBA_DPAD, CP_KEY_DPAD, "" },
	{ { 184, 262,  64,  32 }, { 176, 250,  76,  66 },
	  CP_GBA_SELECT, CP_KEY_PILL, "Select" },
	{ { 264, 262,  64,  32 }, { 256, 250,  76,  66 },
	  CP_GBA_START, CP_KEY_PILL, "Start" },
	{ { 368, 250,  64,  64 }, { 354, 242,  82,  78 },
	  CP_GBA_B, CP_KEY_ROUND, "B" },
	{ { 452, 250,  64,  64 }, { 438, 242,  90,  78 },
	  CP_GBA_A, CP_KEY_ROUND, "A" },
};

/* Landscape 2x: the picture takes 480 of the 528 px, so only a 48 px strip is
 * left, for four keys; the D-pad and the shoulder keys are on the keypad.
 * The strip is on the side away from the keypad, under the free thumb: on
 * the left with the keypad on the right, on the right with it on the left. */
#define CP_STRIP_KEYS(X) \
	{ { (X) + 2,   4, 44, 72 }, { (X),   0, 48, 80 }, \
	  CP_GBA_A,      CP_KEY_TAB, "A" }, \
	{ { (X) + 2,  84, 44, 72 }, { (X),  80, 48, 80 }, \
	  CP_GBA_B,      CP_KEY_TAB, "B" }, \
	{ { (X) + 2, 164, 44, 72 }, { (X), 160, 48, 80 }, \
	  CP_GBA_SELECT, CP_KEY_TAB, "Select" }, \
	{ { (X) + 2, 244, 44, 72 }, { (X), 240, 48, 80 }, \
	  CP_GBA_START,  CP_KEY_TAB, "Start" }

static const cp_key cp_keys_landscape_2x_left[] = { CP_STRIP_KEYS(0) };
static const cp_key cp_keys_landscape_2x_right[] = { CP_STRIP_KEYS(480) };

#define CP_COUNT_OF(a) ((unsigned)(sizeof(a) / sizeof((a)[0])))

/* Largest number of touch keys in any layout, and the largest frame (the
 * D-pad) in pixels: the presenter sizes its buffers from these. */
/* Height of the caption strip painted 2 px under a pill key. */
#define CP_KEY_CAPTION_H 17
#define CP_MAX_KEYS 8
#define CP_MAX_KEY_PIXELS (144 * 144)

static inline cp_layout cp_layout_for(unsigned view, unsigned rotation)
{
	cp_layout l;

	switch (view) {
	case CP_VIEW_PORTRAIT_43:
		l.user_w = CP_PANEL_W; l.user_h = CP_PANEL_H;
		l.image = (cp_rect){ 0, 0, 320, 212 };
		l.keys = cp_keys_portrait;
		l.key_count = CP_COUNT_OF(cp_keys_portrait);
		break;
	case CP_VIEW_LANDSCAPE_15X:
		l.user_w = CP_PANEL_H; l.user_h = CP_PANEL_W;
		l.image = (cp_rect){ 168, 0, 360, 240 };
		l.keys = cp_keys_landscape_15x;
		l.key_count = CP_COUNT_OF(cp_keys_landscape_15x);
		break;
	case CP_VIEW_LANDSCAPE_2X:
		l.user_w = CP_PANEL_H; l.user_h = CP_PANEL_W;
		if (rotation == CP_ROTATION_KEYS_LEFT) {
			l.image = (cp_rect){ 0, 0, 480, 320 };
			l.keys = cp_keys_landscape_2x_right;
		} else {
			l.image = (cp_rect){ 48, 0, 480, 320 };
			l.keys = cp_keys_landscape_2x_left;
		}
		l.key_count = CP_COUNT_OF(cp_keys_landscape_2x_left);
		break;
	case CP_VIEW_LANDSCAPE_1X:
		l.user_w = CP_PANEL_H; l.user_h = CP_PANEL_W;
		/* The 1.5x layout with the picture at its own size, in the middle
		 * of the space the 1.5x picture fills. */
		l.image = (cp_rect){ 228, 40, 240, 160 };
		l.keys = cp_keys_landscape_15x;
		l.key_count = CP_COUNT_OF(cp_keys_landscape_15x);
		break;
	case CP_VIEW_PORTRAIT_1X:
	default:
		l.user_w = CP_PANEL_W; l.user_h = CP_PANEL_H;
		l.image = (cp_rect){ 40, 52, 240, 160 };
		l.keys = cp_keys_portrait;
		l.key_count = CP_COUNT_OF(cp_keys_portrait);
		break;
	}
	return l;
}

/* The Rotate icon steps clockwise through all four orientations:
 * portrait -> CW (keys left) -> 180 (upside down) -> CCW (keys right) -> portrait. */
static inline void cp_rotate_next(uint32_t *view, uint32_t *rotation)
{
	if (!cp_view_is_landscape(*view)) {
		*view = cp_view_turned(*view);
		*rotation = (*rotation == CP_ROTATION_KEYS_RIGHT)
			? CP_ROTATION_KEYS_LEFT : CP_ROTATION_KEYS_RIGHT;
	} else {
		*view = cp_view_turned(*view);
	}
}

/* Portrait with the calculator held upside down (keypad above the screen):
 * the rotation setting has no other meaning in portrait, and Swap, which
 * changes hands in landscape, turns the picture over here. */
static inline int cp_view_is_flipped(unsigned view, unsigned rotation)
{
	return !cp_view_is_landscape(view) && rotation == CP_ROTATION_KEYS_LEFT;
}

/* ---- user <-> panel --------------------------------------------------------
 * Portrait is the identity, or a half turn when flipped:
 *       panel col = 319 - user x,  panel row = 527 - user y
 * In landscape a user row is a panel column:
 *   keys right (calculator turned counter-clockwise):
 *       panel col = 319 - user y,  panel row = user x
 *   keys left (turned clockwise):
 *       panel col = user y,        panel row = 527 - user x            */

static inline cp_rect cp_user_to_panel_rect(unsigned view, unsigned rotation,
	cp_rect u)
{
	cp_rect p;

	if (cp_view_is_flipped(view, rotation)) {
		p = u;
		p.x = (int16_t)(CP_PANEL_W - u.x - u.w);
		p.y = (int16_t)(CP_PANEL_H - u.y - u.h);
		return p;
	}
	if (!cp_view_is_landscape(view))
		return u;
	p.w = u.h;
	p.h = u.w;
	if (rotation == CP_ROTATION_KEYS_LEFT) {
		p.x = u.y;
		p.y = (int16_t)(CP_PANEL_H - u.x - u.w);
	} else {
		p.x = (int16_t)(CP_PANEL_W - u.y - u.h);
		p.y = u.x;
	}
	return p;
}

static inline void cp_panel_to_user_point(unsigned view, unsigned rotation,
	int px, int py, int *ux, int *uy)
{
	if (cp_view_is_flipped(view, rotation)) {
		*ux = CP_PANEL_W - 1 - px;
		*uy = CP_PANEL_H - 1 - py;
	} else if (!cp_view_is_landscape(view)) {
		*ux = px;
		*uy = py;
	} else if (rotation == CP_ROTATION_KEYS_LEFT) {
		*ux = CP_PANEL_H - 1 - py;
		*uy = px;
	} else {
		*ux = py;
		*uy = CP_PANEL_W - 1 - px;
	}
}

static inline void cp_user_to_panel_point(unsigned view, unsigned rotation,
	int ux, int uy, int *px, int *py)
{
	if (cp_view_is_flipped(view, rotation)) {
		*px = CP_PANEL_W - 1 - ux;
		*py = CP_PANEL_H - 1 - uy;
	} else if (!cp_view_is_landscape(view)) {
		*px = ux;
		*py = uy;
	} else if (rotation == CP_ROTATION_KEYS_LEFT) {
		*px = uy;
		*py = CP_PANEL_H - 1 - ux;
	} else {
		*px = CP_PANEL_W - 1 - uy;
		*py = ux;
	}
}

/* ---- hit testing ---------------------------------------------------------- */

static inline int cp_rect_contains(cp_rect r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Buttons a key gives for a touch at a user-space point: nothing outside its
 * hit rectangle. A D-pad is split into thirds of its frame; the outer thirds
 * are directions (extended out to the hit rectangle), so the four corners
 * give two directions and the centre gives none. */
static inline uint32_t cp_key_buttons(const cp_key *k, int ux, int uy)
{
	uint32_t mask = 0;
	int third;

	if (!cp_rect_contains(k->hit, ux, uy))
		return 0;
	if (k->shape != CP_KEY_DPAD)
		return k->mask;

	third = k->frame.w / 3;
	if (ux < k->frame.x + third)
		mask |= CP_GBA_LEFT;
	else if (ux >= k->frame.x + k->frame.w - third)
		mask |= CP_GBA_RIGHT;
	third = k->frame.h / 3;
	if (uy < k->frame.y + third)
		mask |= CP_GBA_UP;
	else if (uy >= k->frame.y + k->frame.h - third)
		mask |= CP_GBA_DOWN;
	return mask;
}

/* GBA buttons for one panel-space touch point. */
static inline uint32_t cp_layout_touch(const cp_layout *l, unsigned view,
	unsigned rotation, int px, int py)
{
	int ux, uy;
	unsigned i;

	if (px < 0 || px >= CP_PANEL_W || py < 0 || py >= CP_PANEL_H)
		return 0;
	cp_panel_to_user_point(view, rotation, px, py, &ux, &uy);
	for (i = 0; i < l->key_count; i++) {
		uint32_t mask = cp_key_buttons(&l->keys[i], ux, uy);

		if (mask)
			return mask;
	}
	return 0;
}

/* GBA buttons for up to two simultaneous touch points. */
static inline uint32_t cp_layout_buttons(const cp_layout *l, unsigned view,
	unsigned rotation, int touches, int x1, int y1, int x2, int y2)
{
	uint32_t mask = 0;

	if (touches >= 1)
		mask |= cp_layout_touch(l, view, rotation, x1, y1);
	if (touches >= 2)
		mask |= cp_layout_touch(l, view, rotation, x2, y2);
	return mask;
}

/* How a key should be painted for a set of held buttons: 0 is idle. A D-pad
 * reports which arms are lit (CP_GBA_* direction bits); any other key is lit
 * when its button is held. */
static inline unsigned cp_key_state(const cp_key *k, uint32_t buttons)
{
	if (k->shape == CP_KEY_DPAD)
		return buttons & CP_GBA_DPAD;
	return (buttons & k->mask) == k->mask;
}

/* ---- icon strip ------------------------------------------------------------
 * The row of printed icons under the LCD is part of the touch panel, not of
 * the key matrix: a touch there arrives as a point below the last LCD row.
 * The icon centres (x = 23, 69, 114, 160, 206, 251, 297) are the ones the OS
 * accepts; cells are split halfway between neighbours. (Measured through
 * gint on a calculator they come out 3-9 px further right, at y = 557..560,
 * well inside the same cells.) Rows 528..CP_STRIP_Y-1
 * are left unassigned, so only a touch on the lower part of the printed
 * icons counts and one just under the bottom of the picture does not. (The
 * presenter also requires the touch to have begun on the strip.) */
enum {
	CP_ICON_SETTINGS = 0,
	CP_ICON_MENU,
	CP_ICON_MAIN,
	CP_ICON_RESIZE,
	CP_ICON_SWAP,
	CP_ICON_ROTATE,
	CP_ICON_ESC,
	CP_ICON_COUNT
};

#define CP_STRIP_Y 546

/* Icon under a panel-space touch point, or -1. */
static inline int cp_strip_icon(int px, int py)
{
	static const int16_t right_edge[CP_ICON_COUNT] = {
		46, 92, 137, 183, 229, 274, CP_PANEL_W
	};
	int i;

	if (py < CP_STRIP_Y || px < 0 || px >= CP_PANEL_W)
		return -1;
	for (i = 0; i < CP_ICON_COUNT; i++)
		if (px < right_edge[i])
			return i;
	return -1;
}

static inline uint32_t cp_strip_icons(int touches, int x1, int y1, int x2,
	int y2)
{
	uint32_t icons = 0;
	int icon;

	if (touches >= 1 && (icon = cp_strip_icon(x1, y1)) >= 0)
		icons |= 1u << icon;
	if (touches >= 2 && (icon = cp_strip_icon(x2, y2)) >= 0)
		icons |= 1u << icon;
	return icons;
}

/* ---- arrow keys in landscape ----------------------------------------------
 * The cursor pad is fixed to the case, so a quarter turn moves "up". Given the
 * direction the player means (0 up, 1 right, 2 down, 3 left), return the
 * direction printed on the key that now points that way. */
static inline unsigned cp_physical_direction(unsigned view, unsigned rotation,
	unsigned user_direction)
{
	if (cp_view_is_flipped(view, rotation))
		return (user_direction + 2u) & 3u;   /* up is the DOWN key */
	if (!cp_view_is_landscape(view))
		return user_direction & 3u;
	if (rotation == CP_ROTATION_KEYS_LEFT)
		return (user_direction + 3u) & 3u;   /* up is the LEFT key */
	return (user_direction + 1u) & 3u;       /* up is the RIGHT key */
}

#endif /* CGBA_CLASSPAD_LAYOUT_H */
