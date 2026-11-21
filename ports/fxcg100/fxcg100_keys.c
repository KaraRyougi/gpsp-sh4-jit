#include "fxcg100_platform.h"

#include <stddef.h>
#include <string.h>

typedef struct fxcg100_key_choice {
  uint16_t key;
  const char *label;
} fxcg100_key_choice;

#if defined(CGBA_CLASSPAD)
#include <gint/keycodes.h>

/* ClassPad II: bindable keys are gint KEY_* codes. Clear (which
 * doubles as the power key) opens the emulator menu until it is given to
 * something here; the Menu and Settings icons under the screen always do.
 * The arrows are
 * screen directions: in landscape they follow the picture, not the print on
 * the cursor pad (see cp_physical_direction). */
static const fxcg100_key_choice bindable_keys[] = {
  { KEY_KBD, "Keyboard" }, { KEY_SHIFT, "Shift" },
  { KEY_DEL, "Backspace" }, { KEY_CLEAR, "Clear" },
  { KEY_LEFT, "Left" },    { KEY_RIGHT, "Right" },
  { KEY_UP, "Up" },        { KEY_DOWN, "Down" },
  { KEY_EQUALS, "=" },     { KEY_X, "x" },
  { KEY_Y, "y" },          { KEY_Z, "z" },
  { KEY_POWER, "^" },      { KEY_DIV, "/" },
  { KEY_LEFTP, "(" },      { KEY_7, "7" },
  { KEY_8, "8" },          { KEY_9, "9" },
  { KEY_MUL, "*" },
  { KEY_RIGHTP, ")" },     { KEY_4, "4" },
  { KEY_5, "5" },          { KEY_6, "6" },
  { KEY_SUB, "-" },
  { KEY_COMMA, "," },      { KEY_1, "1" },
  { KEY_2, "2" },          { KEY_3, "3" },
  { KEY_ADD, "+" },
  { KEY_NEG, "(-)" },      { KEY_0, "0" },
  { KEY_DOT, "." },        { KEY_EXP, "EXP" },
  { KEY_EXE, "EXE" },
};

_Static_assert(sizeof(bindable_keys) / sizeof(bindable_keys[0]) == 34,
               "ClassPad II must expose every physical key");

/* GBA button -> ClassPad II key, in enum order:
 *   A=EXE B=+ SELECT== START=X
 *   RIGHT LEFT UP DOWN L=SHIFT R=BKSP  (Clear opens the emulator menu)
 * Every GBA button is also available as a touch key. */
static const uint16_t default_keymap[FXCG100_GBA_KEY_COUNT] = {
  KEY_EXE, KEY_ADD, KEY_EQUALS, KEY_X,
  KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_SHIFT, KEY_DEL
};
#elif defined(CGBA_FXCG50)
#include <gint/keycodes.h>

/* fx-CG50 / Graph 90+E: bindable keys are gint KEY_* codes on the classic
 * matrix (labels are the fx-CG50 silkscreen). AC/ON is reserved for the
 * emulator menu, so it is not offered as a GBA binding. */
static const fxcg100_key_choice bindable_keys[] = {
  { KEY_MENU, "MENU" },   { KEY_EXIT, "EXIT" },
  { KEY_SHIFT, "SHIFT" }, { KEY_ALPHA, "ALPHA" },
  { KEY_SQUARE, "X^2" },  { KEY_POWER, "^" },
  { KEY_OPTN, "OPTN" },   { KEY_VARS, "VARS" },
  { KEY_F1, "F1" },       { KEY_F2, "F2" },
  { KEY_F3, "F3" },       { KEY_F4, "F4" },
  { KEY_F5, "F5" },       { KEY_F6, "F6" },
  { KEY_LEFT, "LEFT" },   { KEY_RIGHT, "RIGHT" },
  { KEY_UP, "UP" },       { KEY_DOWN, "DOWN" },
  { KEY_EXE, "EXE" },
  { KEY_XOT, "X,T" },     { KEY_LOG, "LOG" },
  { KEY_LN, "LN" },       { KEY_SIN, "SIN" },
  { KEY_COS, "COS" },     { KEY_TAN, "TAN" },
  { KEY_FRAC, "a b/c" },  { KEY_FD, "S<=>D" },
  { KEY_LEFTP, "(" },      { KEY_RIGHTP, ")" },
  { KEY_COMMA, "," },     { KEY_ARROW, "->" },
  { KEY_DEL, "DEL" },
  { KEY_7, "7" },         { KEY_8, "8" },
  { KEY_9, "9" },         { KEY_4, "4" },
  { KEY_5, "5" },         { KEY_6, "6" },
  { KEY_MUL, "x" },       { KEY_DIV, "/" },
  { KEY_1, "1" },         { KEY_2, "2" },
  { KEY_3, "3" },         { KEY_ADD, "+" },
  { KEY_SUB, "-" },       { KEY_0, "0" },
  { KEY_DOT, "." },       { KEY_EXP, "x10" },
  { KEY_NEG, "(-)" },
};

_Static_assert(sizeof(bindable_keys) / sizeof(bindable_keys[0]) == 49,
  "fx-CG50 must expose every physical key except AC/ON");

/* GBA button -> fx-CG50 key, in enum order:
 *   A=MENU B=EXIT SELECT=^ START=VARS
 *   RIGHT LEFT UP DOWN L=F5 R=F6  (AC/ON opens the emulator menu) */
static const uint16_t default_keymap[FXCG100_GBA_KEY_COUNT] = {
  KEY_MENU, KEY_EXIT, KEY_POWER, KEY_VARS,
  KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_F5, KEY_F6
};
#else
static const fxcg100_key_choice bindable_keys[] = {
  { 69, "HOME" },     { 78, "SETTINGS" },
  { 68, "BACK" },     { 77, "SHIFT" },
  { 67, "ALPHA" },
  { 57, "VAR" },      { 48, "OK" },
  { 31, "EXE" },      { 58, "LEFT" },
  { 38, "RIGHT" },    { 49, "UP" },
  { 47, "DOWN" },     { 37, "CAT" },
  { 27, "TOOLS" },
  { 59, "BEGIN" },    { 39, "END" },
  { 29, "PGUP" },     { 28, "PGDN" },
  { 36, "X^2" },      { 46, "^" },
  { 76, "X,T" },      { 56, "SQRT" },
  { 26, "EXP" },      { 65, "SIN" },
  { 55, "COS" },      { 45, "TAN" },
  { 66, "FRAC" },     { 35, "(" },
  { 25, ")" },        { 75, "," },
  { 74, "7" },        { 64, "8" },
  { 54, "9" },        { 44, "DEL" },
  { 34, "AC" },
  { 73, "4" },        { 63, "5" },
  { 53, "6" },        { 43, "x" },
  { 33, "/" },        { 72, "1" },
  { 62, "2" },        { 52, "3" },
  { 42, "+" },        { 32, "-" },
  { 71, "0" },        { 61, "." },
  { 51, "x10" },      { 41, "FMT" },
};

/* GBA button -> fx-CG100 key, in enum order:
 *   A=OK(48) B=CAT(37) SELECT=PGUP(29) START=PGDN(28)
 *   RIGHT(38) LEFT(58) UP(49) DOWN(47) L=BEGIN(59) R=END(39) */
static const uint16_t default_keymap[FXCG100_GBA_KEY_COUNT] = {
  48, 37, 29, 28, 38, 58, 49, 47, 59, 39
};
#endif

static const uint16_t default_hotkey_map[FXCG100_HOTKEY_COUNT] = {
  0, 0, 0, 0, 0, 0
};

static const uint32_t gba_button_bits[FXCG100_GBA_KEY_COUNT] = {
  FXCG100_GBA_BUTTON_A,
  FXCG100_GBA_BUTTON_B,
  FXCG100_GBA_BUTTON_SELECT,
  FXCG100_GBA_BUTTON_START,
  FXCG100_GBA_BUTTON_RIGHT,
  FXCG100_GBA_BUTTON_LEFT,
  FXCG100_GBA_BUTTON_UP,
  FXCG100_GBA_BUTTON_DOWN,
  FXCG100_GBA_BUTTON_L,
  FXCG100_GBA_BUTTON_R,
};

static const char * const gba_key_labels[FXCG100_GBA_KEY_COUNT] = {
  "A",
  "B",
  "SELECT",
  "START",
  "RIGHT",
  "LEFT",
  "UP",
  "DOWN",
  "LEFT TRIGGER",
  "RIGHT TRIGGER",
};

static const char * const hotkey_labels[FXCG100_HOTKEY_COUNT] = {
  "FAST FORWARD",
  "LOAD STATE",
  "SAVE STATE",
  "TXT READER",
  "DISPLAY FPS",
  "SAVE & EXIT",
};

int fxcg100_key_bindable(uint16_t key)
{
  size_t i;

  for (i = 0; i < sizeof(bindable_keys) / sizeof(bindable_keys[0]); i++) {
    if (bindable_keys[i].key == key)
      return 1;
  }
  return 0;
}

const char *fxcg100_key_label(uint16_t key)
{
  size_t i;

  if (key == 0)
    return "NONE";

  for (i = 0; i < sizeof(bindable_keys) / sizeof(bindable_keys[0]); i++) {
    if (bindable_keys[i].key == key)
      return bindable_keys[i].label;
  }
  if (key == FXCG100_PHYSKEY_ON)
#ifdef CGBA_CLASSPAD
    return "Clear";
#else
    return "ON";
#endif
  return "?";
}

const char *fxcg100_gba_key_label(uint32_t index)
{
  if (index < FXCG100_GBA_KEY_COUNT)
    return gba_key_labels[index];
  return "?";
}

const char *fxcg100_hotkey_label(uint32_t index)
{
  if (index < FXCG100_HOTKEY_COUNT)
    return hotkey_labels[index];
  return "?";
}

void fxcg100_keymap_defaults(uint16_t *keymap)
{
  if (!keymap)
    return;
  memcpy(keymap, default_keymap, sizeof(default_keymap));
}

void fxcg100_hotkey_defaults(uint16_t *hotkey_map)
{
  if (!hotkey_map)
    return;
  memcpy(hotkey_map, default_hotkey_map, sizeof(default_hotkey_map));
}

int fxcg100_keymap_valid(const uint16_t *keymap)
{
  uint32_t i;
  uint32_t j;

  if (!keymap)
    return 0;
  for (i = 0; i < FXCG100_GBA_KEY_COUNT; i++) {
#ifdef CGBA_CLASSPAD
    /* On the ClassPad II a GBA button may have no key: it is still on the
     * touch screen, and giving its key to something else leaves it so. */
    if (keymap[i] == 0)
      continue;
#endif
    if (!fxcg100_key_bindable(keymap[i]))
      return 0;
    for (j = i + 1; j < FXCG100_GBA_KEY_COUNT; j++) {
      if (keymap[i] == keymap[j])
        return 0;
    }
  }
  return 1;
}

int fxcg100_hotkey_map_valid(const uint16_t *hotkey_map)
{
  uint32_t i;
  uint32_t j;

  if (!hotkey_map)
    return 0;
  for (i = 0; i < FXCG100_HOTKEY_COUNT; i++) {
    if (hotkey_map[i] != 0 && !fxcg100_key_bindable(hotkey_map[i]))
      return 0;
    if (hotkey_map[i] == 0)
      continue;
    for (j = i + 1; j < FXCG100_HOTKEY_COUNT; j++) {
      if (hotkey_map[i] == hotkey_map[j])
        return 0;
    }
  }
  return 1;
}

int fxcg100_input_maps_valid(const uint16_t *keymap,
                             const uint16_t *hotkey_map)
{
  uint32_t i;
  uint32_t j;

  if (!fxcg100_keymap_valid(keymap) ||
      !fxcg100_hotkey_map_valid(hotkey_map))
    return 0;

  for (i = 0; i < FXCG100_HOTKEY_COUNT; i++) {
    if (hotkey_map[i] == 0)
      continue;
    for (j = 0; j < FXCG100_GBA_KEY_COUNT; j++) {
      if (hotkey_map[i] == keymap[j])
        return 0;
    }
  }
  return 1;
}

uint16_t fxcg100_poll_physical_key(void)
{
  size_t i;

  (void)fxcg100_poll_app_keys();

  if (fxcg100_key_down(FXCG100_PHYSKEY_ON))
    return FXCG100_PHYSKEY_ON;

  for (i = 0; i < sizeof(bindable_keys) / sizeof(bindable_keys[0]); i++) {
    if (fxcg100_key_down(bindable_keys[i].key))
      return bindable_keys[i].key;
  }
  return 0;
}

static uint32_t sanitize_gba_buttons(uint32_t buttons)
{
  if ((buttons & FXCG100_GBA_BUTTON_LEFT) &&
      (buttons & FXCG100_GBA_BUTTON_RIGHT))
    buttons &= ~(FXCG100_GBA_BUTTON_LEFT | FXCG100_GBA_BUTTON_RIGHT);

  if ((buttons & FXCG100_GBA_BUTTON_UP) &&
      (buttons & FXCG100_GBA_BUTTON_DOWN))
    buttons &= ~(FXCG100_GBA_BUTTON_UP | FXCG100_GBA_BUTTON_DOWN);

  return buttons;
}

uint32_t fxcg100_poll_gba_buttons_mapped(const uint16_t *keymap)
{
  uint32_t buttons = FXCG100_GBA_BUTTON_NONE;
  uint32_t i;

  if (!fxcg100_keymap_valid(keymap))
    keymap = default_keymap;

  for (i = 0; i < FXCG100_GBA_KEY_COUNT; i++) {
    if (keymap[i] != 0 && fxcg100_key_down(keymap[i]))
      buttons |= gba_button_bits[i];
  }
#ifdef CGBA_CLASSPAD
  buttons |= fxcg100_touch_gba_buttons();
  /* The touch keys show what is held, from the keypad as well. */
  fxcg100_touch_show_buttons(buttons);
#endif
  return sanitize_gba_buttons(buttons);
}

uint32_t fxcg100_poll_gba_buttons(void)
{
  return fxcg100_poll_gba_buttons_mapped(default_keymap);
}

uint32_t fxcg100_poll_hotkeys_mapped(const uint16_t *hotkey_map)
{
  uint32_t hotkeys = 0;
  uint32_t i;

  if (!fxcg100_hotkey_map_valid(hotkey_map))
    hotkey_map = default_hotkey_map;

  for (i = 0; i < FXCG100_HOTKEY_COUNT; i++) {
    uint16_t key = hotkey_map[i];

    if (key != 0 && fxcg100_key_down(key))
      hotkeys |= FXCG100_HOTKEY_BIT(i);
  }
#ifdef CGBA_CLASSPAD
  /* The Main icon under the screen is fast forward while it is held. */
  if (fxcg100_touch_strip_icons() & FXCG100_STRIP_MAIN)
    hotkeys |= FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD);
#endif

  return hotkeys;
}
