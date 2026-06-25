#include "fxcg100_platform.h"

#include <stddef.h>
#include <string.h>

typedef struct fxcg100_key_choice {
  uint16_t key;
  const char *label;
} fxcg100_key_choice;

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

static const uint16_t default_keymap[FXCG100_GBA_KEY_COUNT] = {
  77, 67, 57, 31, 38, 58, 49, 47, 59, 39
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
