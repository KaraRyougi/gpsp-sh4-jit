#include <stdint.h>
#include <stdio.h>

#include "ports/fxcg100/fxcg100_platform.h"

int fxcg100_key_down(int basic_keycode)
{
  (void)basic_keycode;
  return 0;
}

uint32_t fxcg100_poll_app_keys(void)
{
  return 0;
}

#include "ports/fxcg100/fxcg100_keys.c"

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

  fxcg100_keymap_defaults(keymap);
  fxcg100_hotkey_defaults(hotkeys);
  check(fxcg100_keymap_valid(keymap), "default GBA keymap is valid");
  check(fxcg100_hotkey_map_valid(hotkeys), "default hotkeys are valid");
  check(fxcg100_input_maps_valid(keymap, hotkeys),
        "default input maps are valid together");

  keymap[FXCG100_GBA_KEY_B] = keymap[FXCG100_GBA_KEY_A];
  check(!fxcg100_keymap_valid(keymap), "duplicate GBA key is invalid");
  fxcg100_keymap_defaults(keymap);

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = 48;
  hotkeys[FXCG100_HOTKEY_LOAD_STATE] = 48;
  check(!fxcg100_hotkey_map_valid(hotkeys),
        "duplicate nonzero hotkey is invalid");
  fxcg100_hotkey_defaults(hotkeys);

  hotkeys[FXCG100_HOTKEY_FAST_FORWARD] = 69;
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

  if (failures)
    return 1;

  puts("fx-CG100 input map validation passed");
  return 0;
}
