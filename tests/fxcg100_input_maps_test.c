#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/fxcg100_platform.h"

static int pressed_key;

int fxcg100_key_down(int basic_keycode)
{
  return basic_keycode == pressed_key;
}

uint32_t fxcg100_poll_app_keys(void)
{
  return 0;
}

#include "ports/fxcg100/fxcg100_keys.c"

static void check(int condition, const char *name);

#ifdef CGBA_FXCG50
static const uint16_t cg50_physical_keys[] = {
  KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
  KEY_SHIFT, KEY_OPTN, KEY_VARS, KEY_MENU, KEY_LEFT, KEY_UP,
  KEY_ALPHA, KEY_SQUARE, KEY_POWER, KEY_EXIT, KEY_DOWN, KEY_RIGHT,
  KEY_XOT, KEY_LOG, KEY_LN, KEY_SIN, KEY_COS, KEY_TAN,
  KEY_FRAC, KEY_FD, KEY_LEFTP, KEY_RIGHTP, KEY_COMMA, KEY_ARROW,
  KEY_7, KEY_8, KEY_9, KEY_DEL,
  KEY_4, KEY_5, KEY_6, KEY_MUL, KEY_DIV,
  KEY_1, KEY_2, KEY_3, KEY_ADD, KEY_SUB,
  KEY_0, KEY_DOT, KEY_EXP, KEY_NEG, KEY_EXE,
};

static const uint16_t cg50_expected_default_keymap[FXCG100_GBA_KEY_COUNT] = {
  KEY_MENU, KEY_EXIT, KEY_POWER, KEY_VARS,
  KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_F5, KEY_F6,
};

static const uint32_t cg50_expected_default_bits[FXCG100_GBA_KEY_COUNT] = {
  FXCG100_GBA_BUTTON_A, FXCG100_GBA_BUTTON_B,
  FXCG100_GBA_BUTTON_SELECT, FXCG100_GBA_BUTTON_START,
  FXCG100_GBA_BUTTON_RIGHT, FXCG100_GBA_BUTTON_LEFT,
  FXCG100_GBA_BUTTON_UP, FXCG100_GBA_BUTTON_DOWN,
  FXCG100_GBA_BUTTON_L, FXCG100_GBA_BUTTON_R,
};

static void check_cg50_matrix(void)
{
  char message[80];
  size_t i;

  check(sizeof(cg50_physical_keys) / sizeof(cg50_physical_keys[0]) == 49,
        "CG50 oracle contains 49 non-AC physical keys");

  for (i = 0; i < sizeof(cg50_physical_keys) /
                  sizeof(cg50_physical_keys[0]); i++) {
    uint16_t key = cg50_physical_keys[i];

    snprintf(message, sizeof(message), "CG50 key 0x%02X is bindable", key);
    check(fxcg100_key_bindable(key), message);
    snprintf(message, sizeof(message), "CG50 key 0x%02X has a label", key);
    check(strcmp(fxcg100_key_label(key), "?") != 0, message);

    pressed_key = key;
    snprintf(message, sizeof(message), "CG50 key 0x%02X is captured", key);
    check(fxcg100_poll_physical_key() == key, message);
  }
  pressed_key = 0;

  check(!fxcg100_key_bindable(KEY_ACON), "CG50 AC/ON remains reserved");
  check(strcmp(fxcg100_key_label(KEY_ADD), "+") == 0,
        "CG50 plus label");
  check(strcmp(fxcg100_key_label(KEY_SUB), "-") == 0,
        "CG50 minus label");
  check(strcmp(fxcg100_key_label(KEY_MUL), "x") == 0,
        "CG50 multiply label");
  check(strcmp(fxcg100_key_label(KEY_DIV), "/") == 0,
        "CG50 divide label");
}

static void check_cg50_defaults(const uint16_t *keymap)
{
  char message[80];
  size_t i;

  check(memcmp(keymap, cg50_expected_default_keymap,
               sizeof(cg50_expected_default_keymap)) == 0,
        "CG50 default keymap matches legacy calculator layout");

  for (i = 0; i < FXCG100_GBA_KEY_COUNT; i++) {
    pressed_key = cg50_expected_default_keymap[i];
    snprintf(message, sizeof(message),
             "CG50 default key 0x%02X drives button %u",
             cg50_expected_default_keymap[i], (unsigned)i);
    check(fxcg100_poll_gba_buttons_mapped(keymap) ==
            cg50_expected_default_bits[i], message);
  }
  pressed_key = 0;
}
#endif

static int failures;

static void check(int condition, const char *name)
{
  if (condition)
    return;

  printf("FAIL: %s\n", name);
  failures++;
}

int main(void)
{
  uint16_t keymap[FXCG100_GBA_KEY_COUNT];
  uint16_t hotkeys[FXCG100_HOTKEY_COUNT];
#ifdef CGBA_FXCG50
  const uint16_t duplicate_hotkey = KEY_F1;
  const uint16_t standalone_hotkey = KEY_F2;
#else
  const uint16_t duplicate_hotkey = 48;
  const uint16_t standalone_hotkey = 69;
#endif

  fxcg100_keymap_defaults(keymap);
  fxcg100_hotkey_defaults(hotkeys);
#ifdef CGBA_FXCG50
  check_cg50_defaults(keymap);
#endif
  check(fxcg100_keymap_valid(keymap), "default GBA keymap is valid");
  check(fxcg100_hotkey_map_valid(hotkeys), "default hotkeys are valid");
  check(fxcg100_input_maps_valid(keymap, hotkeys),
        "default input maps are valid together");

  keymap[FXCG100_GBA_KEY_B] = keymap[FXCG100_GBA_KEY_A];
  check(!fxcg100_keymap_valid(keymap), "duplicate GBA key is invalid");
  fxcg100_keymap_defaults(keymap);

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = duplicate_hotkey;
  hotkeys[FXCG100_HOTKEY_LOAD_STATE] = duplicate_hotkey;
  check(!fxcg100_hotkey_map_valid(hotkeys),
        "duplicate nonzero hotkey is invalid");
  fxcg100_hotkey_defaults(hotkeys);

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = standalone_hotkey;
  check(fxcg100_hotkey_map_valid(hotkeys), "single hotkey is valid");
  check(fxcg100_input_maps_valid(keymap, hotkeys),
        "non-overlapping hotkey is valid with GBA map");

  hotkeys[FXCG100_HOTKEY_LOAD_STATE] = 0;
  hotkeys[FXCG100_HOTKEY_SAVE_STATE] = 0;
  check(fxcg100_hotkey_map_valid(hotkeys),
        "repeated NONE hotkeys are valid");

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = keymap[FXCG100_GBA_KEY_A];
  check(!fxcg100_input_maps_valid(keymap, hotkeys),
        "hotkey overlapping GBA key is invalid");
  fxcg100_hotkey_defaults(hotkeys);

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = FXCG100_PHYSKEY_ON;
  check(!fxcg100_hotkey_map_valid(hotkeys),
        "ON cannot be bound as a hotkey");

#ifdef CGBA_FXCG50
  check_cg50_matrix();

  fxcg100_keymap_defaults(keymap);
  fxcg100_hotkey_defaults(hotkeys);
  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = KEY_F2;
  check(fxcg100_input_maps_valid(keymap, hotkeys),
        "CG50 legacy defaults validate with a hotkey");

  pressed_key = KEY_F2;
  check(fxcg100_poll_hotkeys_mapped(hotkeys) ==
          FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD),
        "CG50 non-overlapping hotkey still works");
  pressed_key = 0;
#endif

  if (failures)
    return 1;

#ifdef CGBA_FXCG50
  puts("fx-CG50 input map validation passed (49 bindable keys)");
#else
  puts("fx-CG100 input map validation passed");
#endif
  return 0;
}
