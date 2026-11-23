#ifndef CGBA_UI_COMMON_H
#define CGBA_UI_COMMON_H

/* Shared UI state clamping, frame-blit cadence, ROM title formatting,
 * key-binding descriptors, and TXT bookmark status messages used by
 * cg_ui.c (fx-CG50/100), classpad_ui.c (ClassPad II), and legacy bare-metal
 * fxcg100_menu.c. Keep freestanding-safe (no <stdio.h> dependency). */

#include <stddef.h>
#include <stdint.h>

#include "fxcg100_platform.h"

/* Clamp persisted or mutated menu state to the active platform's valid ranges.
 * Platform bounds:
 *   fx-CG50/100: max_scale=3 (0..2), default_scale=0, max_refresh=6 (0..6)
 *   ClassPad II: max_scale=5 (0..4), default_scale=0, max_refresh=9 (0..9) */
static inline void fxcg100_state_clamp_platform(fxcg100_menu_state *state,
	uint32_t max_scale, uint32_t default_scale, uint32_t max_refresh,
	uint32_t theme_count)
{
	if (!state)
		return;
	if (state->screen_scale >= max_scale)
		state->screen_scale = default_scale;
	if (state->screen_filter > 2u)
		state->screen_filter = 0u;
	if (state->lcd_refresh > max_refresh)
		state->lcd_refresh = 0u;
	if (theme_count > 0u && state->theme >= theme_count)
		state->theme = 0u;
	if (state->game_sort >= FXCG100_GAME_SORT_COUNT)
		state->game_sort = FXCG100_GAME_SORT_NAME;
	if (state->backlight > 16u)
		state->backlight = 0u;
	if (state->fast_forward_mode >= FXCG100_FF_MODE_COUNT)
		state->fast_forward_mode = FXCG100_FF_MODE_HOLD;
	if (state->fast_forward_speed >= FXCG100_FF_SPEED_COUNT)
		state->fast_forward_speed = FXCG100_FF_SPEED_UNLIMITED;
	if (state->bios_mode >= FXCG100_BIOS_MODE_COUNT)
		state->bios_mode = FXCG100_BIOS_AUTO;
}

static inline int cgba_ui_menu_should_blit(fxcg100_menu_state *state,
	uint32_t frame)
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

/* Copy a ROM source label into dst, stripping a trailing ".gba" (any case). */
static inline void cgba_ui_game_title(char *dst, size_t size, uint32_t index)
{
	const char *src = fxcg100_rom_source_label(index);
	size_t len = 0;

	if (!dst || size == 0)
		return;
	if (src) {
		while (len + 1 < size && src[len]) {
			dst[len] = src[len];
			len++;
		}
	}
	dst[len] = 0;
	if (len > 4 && dst[len - 4] == '.' &&
			(dst[len - 3] | 0x20) == 'g' && (dst[len - 2] | 0x20) == 'b' &&
			(dst[len - 1] | 0x20) == 'a')
		dst[len - 4] = 0;
}

#define CGBA_UI_BINDING_COUNT 16
#define CGBA_UI_GBA_BINDINGS  10

typedef struct {
	const char *name;
	int hotkey;
	int index;
} cgba_ui_binding;

#ifdef CGBA_CLASSPAD
#define CGBA_UI_TXT_READER_BIND_LABEL "TXT Reader"
#else
#define CGBA_UI_TXT_READER_BIND_LABEL "TXT reader"
#endif

static const cgba_ui_binding cgba_ui_bindings[CGBA_UI_BINDING_COUNT] = {
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
	{ CGBA_UI_TXT_READER_BIND_LABEL, 1, FXCG100_HOTKEY_TXT_READER },
	{ "Save & exit", 1, FXCG100_HOTKEY_SAVE_EXIT },
};

static inline uint16_t *cgba_ui_binding_slot(fxcg100_menu_state *state, int i)
{
	return cgba_ui_bindings[i].hotkey
		? &state->hotkey_map[cgba_ui_bindings[i].index]
		: &state->keymap[cgba_ui_bindings[i].index];
}

#ifdef CGBA_TXT_READER_H
static inline const char *cgba_ui_txt_bookmark_status(int rc, int *is_error)
{
	if (is_error)
		*is_error = 0;
	if (rc == TXT_BM_ADDED)
		return "Bookmark added";
	if (rc == TXT_BM_EXISTS)
		return "Already bookmarked";
	if (rc == TXT_BM_FULL) {
		if (is_error)
			*is_error = 1;
		return "Bookmarks full (32 max)";
	}
	return NULL;
}
#endif

#endif /* CGBA_UI_COMMON_H */
