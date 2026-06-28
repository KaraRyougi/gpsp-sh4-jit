#include "fxcg100_platform.h"

#include <stddef.h>
#include <string.h>

typedef struct fxcg100_key_choice {
  uint16_t key;
  const char *label;
} fxcg100_key_choice;

static const fxcg100_key_choice bindable_keys[] = {
  { FXCG100_KEY_HOME, "HOME" },
  { FXCG100_KEY_SETTINGS, "SETTINGS" },
  { FXCG100_KEY_BACK, "BACK" },
  { FXCG100_KEY_SHIFT, "SHIFT" },
  { FXCG100_KEY_ALPHA, "ALPHA" },
  { FXCG100_KEY_VARS, "VAR" },
  { FXCG100_KEY_OK, "OK" },
  { FXCG100_KEY_EXE, "EXE" },
  { FXCG100_KEY_LEFT, "LEFT" },
  { FXCG100_KEY_RIGHT, "RIGHT" },
  { FXCG100_KEY_UP, "UP" },
  { FXCG100_KEY_DOWN, "DOWN" },
  { FXCG100_KEY_CATALOG, "CAT" },
  { FXCG100_KEY_TOOLS, "TOOLS" },
  { FXCG100_KEY_PREVTAB, "BEGIN" },
  { FXCG100_KEY_NEXTTAB, "END" },
  { FXCG100_KEY_PAGEUP, "PGUP" },
  { FXCG100_KEY_PAGEDOWN, "PGDN" },
  { FXCG100_KEY_SQUARE, "X^2" },
  { FXCG100_KEY_POWER, "^" },
  { FXCG100_KEY_XOT, "X,T" },
  { FXCG100_KEY_SQRT, "SQRT" },
  { FXCG100_KEY_EXP_FUN, "EXP" },
  { FXCG100_KEY_SIN, "SIN" },
  { FXCG100_KEY_COS, "COS" },
  { FXCG100_KEY_TAN, "TAN" },
  { FXCG100_KEY_FRAC, "FRAC" },
  { FXCG100_KEY_LEFTP, "(" },
  { FXCG100_KEY_RIGHTP, ")" },
  { FXCG100_KEY_COMMA, "," },
  { FXCG100_KEY_7, "7" },
  { FXCG100_KEY_8, "8" },
  { FXCG100_KEY_9, "9" },
  { FXCG100_KEY_DEL, "DEL" },
  { FXCG100_KEY_AC, "AC" },
  { FXCG100_KEY_4, "4" },
  { FXCG100_KEY_5, "5" },
  { FXCG100_KEY_6, "6" },
  { FXCG100_KEY_MUL, "x" },
  { FXCG100_KEY_DIV, "/" },
  { FXCG100_KEY_1, "1" },
  { FXCG100_KEY_2, "2" },
  { FXCG100_KEY_3, "3" },
  { FXCG100_KEY_ADD, "+" },
  { FXCG100_KEY_SUB, "-" },
  { FXCG100_KEY_0, "0" },
  { FXCG100_KEY_DOT, "." },
  { FXCG100_KEY_EXP, "x10" },
  { FXCG100_KEY_FORMAT, "FMT" },
};

static const uint16_t default_keymap[FXCG100_GBA_KEY_COUNT] = {
  FXCG100_KEY_SHIFT,
  FXCG100_KEY_ALPHA,
  FXCG100_KEY_VARS,
  FXCG100_KEY_EXE,
  FXCG100_KEY_RIGHT,
  FXCG100_KEY_LEFT,
  FXCG100_KEY_UP,
  FXCG100_KEY_DOWN,
  FXCG100_KEY_PREVTAB,
  FXCG100_KEY_NEXTTAB
};

static const uint16_t default_hotkey_map[FXCG100_HOTKEY_COUNT] = {
  0, 0, 0, 0, 0
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
  "SAVE+EXIT",
  "DISPLAY FPS",
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
    return "ON";
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
    if (fxcg100_key_down(keymap[i]))
      buttons |= gba_button_bits[i];
  }
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

  return hotkeys;
}
