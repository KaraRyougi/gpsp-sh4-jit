#ifndef CGBA_CLASSPAD_THEME_H
#define CGBA_CLASSPAD_THEME_H

/* Colour schemes for the fx-CP400 interface and touch keys.
 *
 * Each scheme is a set of Material 3 colour roles for a dark theme, built
 * around one primary colour: the surfaces and the text are tinted slightly
 * towards it, so a scheme reads as one tone rather than a colour laid on
 * black. The interface only ever asks for a role, never for a colour.
 *
 * The first scheme is the default (a zeroed configuration selects it). */

#include <stdint.h>

#include "classpad_gfx.h"

#define CP_HEX(rgb) \
	CP_RGB(((rgb) >> 16) & 255, ((rgb) >> 8) & 255, (rgb) & 255)

typedef struct cp_theme {
	uint16_t primary;                 /* filled buttons, selection, accents */
	uint16_t on_primary;              /* text and icons on primary */
	uint16_t primary_container;       /* the "now playing" card */
	uint16_t on_primary_container;
	uint16_t secondary_container;     /* tonal buttons, selected segments */
	uint16_t on_secondary_container;
	uint16_t surface;                 /* page background */
	uint16_t container;               /* cards */
	uint16_t container_high;          /* dialogs, icon buttons, key faces */
	uint16_t on_surface;              /* text */
	uint16_t on_surface_variant;      /* secondary text, icons */
	uint16_t outline;                 /* outlined controls */
	uint16_t error;                   /* destructive actions, warnings */
} cp_theme;

#define CP_THEME_ROLES ((int)(sizeof(cp_theme) / sizeof(uint16_t)))

enum {
	CP_THEME_GREEN = 0,
	CP_THEME_TEAL,
	CP_THEME_BLUE,
	CP_THEME_INDIGO,
	CP_THEME_VIOLET,
	CP_THEME_MAGENTA,
	CP_THEME_AMBER,
	CP_THEME_MONO,
	CP_THEME_COUNT
};

#define CP_THEME(p, op, pc, opc, sc, osc, s, c, ch, os, ov, ol) \
	{ CP_HEX(p), CP_HEX(op), CP_HEX(pc), CP_HEX(opc), CP_HEX(sc), \
	  CP_HEX(osc), CP_HEX(s), CP_HEX(c), CP_HEX(ch), CP_HEX(os), CP_HEX(ov), \
	  CP_HEX(ol), CP_HEX(0xFFB4AB) }

static const cp_theme cp_themes[CP_THEME_COUNT] = {
	[CP_THEME_GREEN] = CP_THEME(0x7EDB7B, 0x00390A, 0x00530F, 0x99F895,
		0x3A4B38, 0xD5E8CF, 0x10140F, 0x1C211B, 0x272B25, 0xE0E4DB,
		0xC2C9BD, 0x8C9388),
	[CP_THEME_TEAL] = CP_THEME(0x53DBC9, 0x003731, 0x005048, 0x72F8E5,
		0x324B47, 0xCCE8E2, 0x0E1513, 0x1A211F, 0x252B2A, 0xDDE4E1,
		0xBEC9C6, 0x899390),
	[CP_THEME_BLUE] = CP_THEME(0xA8C7FA, 0x062E6F, 0x0842A0, 0xD3E3FD,
		0x3E4759, 0xDAE2F9, 0x111318, 0x1D2024, 0x282A2F, 0xE2E2E9,
		0xC4C6D0, 0x8E9099),
	[CP_THEME_INDIGO] = CP_THEME(0xBEC2FF, 0x1F2478, 0x373C90, 0xE0E0FF,
		0x444559, 0xE1E0F9, 0x121318, 0x1F1F25, 0x29292F, 0xE4E1E9,
		0xC7C5D0, 0x91909A),
	[CP_THEME_VIOLET] = CP_THEME(0xD0BCFF, 0x381E72, 0x4F378B, 0xEADDFF,
		0x4A4458, 0xE8DEF8, 0x141218, 0x211F26, 0x2B2930, 0xE6E0E9,
		0xCAC4D0, 0x938F99),
	[CP_THEME_MAGENTA] = CP_THEME(0xFBAAFF, 0x560A66, 0x70277E, 0xFFD6FE,
		0x534457, 0xF1DEF2, 0x171218, 0x231E24, 0x2E282F, 0xEADFE9,
		0xD0C3CC, 0x998D96),
	[CP_THEME_AMBER] = CP_THEME(0xFFB95C, 0x452B00, 0x633F00, 0xFFDDB3,
		0x4F4539, 0xEDE1CF, 0x17130E, 0x231F1A, 0x2E2924, 0xEBE1D9,
		0xD2C4B4, 0x9A8F80),
	[CP_THEME_MONO] = CP_THEME(0xE2E2E2, 0x1B1B1B, 0x3B3B3B, 0xF1F1F1,
		0x444444, 0xE2E2E2, 0x121212, 0x1E1E1E, 0x292929, 0xE3E3E3,
		0xC6C6C6, 0x8F8F8F),
};

static const char *const cp_theme_names[CP_THEME_COUNT] = {
	"Green", "Teal", "Blue", "Indigo", "Violet", "Magenta", "Amber", "Mono"
};

static inline const cp_theme *cp_theme_get(uint32_t index)
{
	return &cp_themes[index < CP_THEME_COUNT ? index : 0];
}

/* out = a + (b - a) * t / 256 for every role: a scheme part-way between two
 * others. Fading a whole screen is done by drawing it with such a scheme, so
 * it costs nothing per pixel. */
static inline void cp_theme_mix(cp_theme *out, const cp_theme *a,
	const cp_theme *b, int t)
{
	const uint16_t *ca = (const uint16_t *)(const void *)a;
	const uint16_t *cb = (const uint16_t *)(const void *)b;
	uint16_t *co = (uint16_t *)(void *)out;
	int i;

	for (i = 0; i < CP_THEME_ROLES; i++)
		co[i] = cp_blend(ca[i], cb[i], t, 256);
}

/* Every role moved towards one colour by t / 256. */
static inline void cp_theme_toward(cp_theme *out, const cp_theme *a,
	uint16_t color, int t)
{
	const uint16_t *ca = (const uint16_t *)(const void *)a;
	uint16_t *co = (uint16_t *)(void *)out;
	int i;

	for (i = 0; i < CP_THEME_ROLES; i++)
		co[i] = cp_blend(ca[i], color, t, 256);
}

/* ---- touch keys ------------------------------------------------------------
 * The keys sit next to the game picture on a black ground, so they are kept
 * dim: the faces are the scheme's surface tones taken part of the way to
 * black, the labels a muted version of the text colour. A pressed key is a
 * tonal step up from the idle one, not a patch of the primary colour:
 * enough to confirm the press, not enough to pull the eye off the game. */
#define CP_KEY_DIM      90            /* of 256: faces towards black */
#define CP_KEY_LIT_MIX  24            /* of 256: pressed face towards primary */
#define CP_KEY_INK_MIX  150           /* of 256: labels towards their face */

typedef struct cp_key_colors {
	uint16_t face, face_lit;
	uint16_t ink, ink_lit;
	uint16_t well;                    /* the disc behind the D-pad */
	uint16_t caption;
} cp_key_colors;

static inline cp_key_colors cp_theme_key_colors(const cp_theme *t)
{
	cp_key_colors k;

	k.face = cp_blend(t->container_high, 0x0000, CP_KEY_DIM, 256);
	k.face_lit = cp_blend(k.face, t->primary, CP_KEY_LIT_MIX, 256);
	k.ink = cp_blend(t->on_surface, k.face, CP_KEY_INK_MIX, 256);
	k.ink_lit = cp_blend(t->on_primary_container, k.face_lit,
		CP_KEY_INK_MIX, 256);
	k.well = cp_blend(t->container, 0x0000, CP_KEY_DIM, 256);
	k.caption = cp_blend(t->outline, 0x0000, CP_KEY_DIM, 256);
	return k;
}

#endif /* CGBA_CLASSPAD_THEME_H */
