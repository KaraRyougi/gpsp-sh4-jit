#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../ports/fxcg100/fxcg100_platform.h"

int fxcg100_input_maps_valid(const uint16_t *keymap,
                             const uint16_t *hotkey_map)
{
  (void)keymap;
  (void)hotkey_map;
  return 1;
}

/* Include the implementation so this host test can exercise the private
 * on-disk migration helpers without exposing them in the production API. */
#include "../ports/fxcg100/fxcg100_config.c"

int main(void)
{
  fxcg100_menu_state state;
  fxcg100_config_file config;

  memset(&state, 0, sizeof(state));
  state.frame_limit = 1;
  config_from_state(&config, &state);
  assert(config.version == CGBA_CONFIG_VERSION);
  assert(config.size == sizeof(config));
  assert(config.frame_limit == 1);
  assert(config_valid(&config));

  state.frame_limit = 0;
  config_from_state(&config, &state);
  assert(config.frame_limit == 0);
  assert(config_valid(&config));

  config.frame_limit = 2;
  assert(!config_valid(&config));

  /* Version 2 had the same file size and zeroed reserved storage in the words
   * now used by frame_limit and lcd_extra, and could hold legacy backup policy
   * values in hotkey_extra. It must migrate cleanly to enabled defaults and an
   * unbound Save & exit shortcut. */
  config_from_state(&config, &state);
  config.version = CGBA_CONFIG_VERSION_LEGACY;
  config.frame_limit = 0;
  config.hotkey_extra = 1;
  config.lcd_extra = 0;
  assert(config_valid(&config));
  memset(&state, 0, sizeof(state));
  state_from_config(&state, &config);
  assert(state.frame_limit == 1);
  assert(state.game_sort == FXCG100_GAME_SORT_NAME);
  assert(state.dark_mode == 1);
  assert(state.fast_forward_mode == FXCG100_FF_MODE_HOLD);
  assert(state.fast_forward_speed == FXCG100_FF_SPEED_UNLIMITED);
  assert(state.bios_mode == FXCG100_BIOS_AUTO);
  assert(state.hotkey_map[FXCG100_HOTKEY_SAVE_EXIT] == 0);

  /* Version 3 preserved frame_limit=0 and did not populate Save & exit. */
  config_from_state(&config, &state);
  config.version = CGBA_CONFIG_VERSION_V3;
  config.frame_limit = 0;
  config.hotkey_extra = 0;
  assert(config_valid(&config));
  memset(&state, 0, sizeof(state));
  state_from_config(&state, &config);
  assert(state.frame_limit == 0);
  assert(state.hotkey_map[FXCG100_HOTKEY_SAVE_EXIT] == 0);

  /* Library sort mode (bits 24..25), dark mode (bit 26 inverted), fast-forward
   * mode (bit 27), fast-forward speed (bits 28..30), backlight (bits 8..12),
   * and the Save & exit hotkey (hotkey_extra) round-trip in version 4. */
  for (uint32_t s = 0; s < FXCG100_GAME_SORT_COUNT; s++) {
    for (uint32_t dark = 0; dark < 2; dark++) {
      for (uint32_t ffm = 0; ffm < FXCG100_FF_MODE_COUNT; ffm++) {
        for (uint32_t ffs = 0; ffs < FXCG100_FF_SPEED_COUNT; ffs++) {
          memset(&state, 0, sizeof(state));
          state.frame_limit = 1;
          state.game_sort = s;
          state.dark_mode = dark;
          state.backlight = 12;
          state.fast_forward_mode = ffm;
          state.fast_forward_speed = ffs;
          state.hotkey_map[FXCG100_HOTKEY_SAVE_EXIT] = 65;
          config_from_state(&config, &state);
          assert(config.version == CGBA_CONFIG_VERSION);
          assert(config_valid(&config));
          assert(((config.lcd_extra >> 26) & 1u) == (dark ? 0u : 1u));
          assert(((config.lcd_extra >> 27) & 1u) == ffm);
          assert(((config.lcd_extra >> 28) & 7u) == ffs);
          assert(config.hotkey_extra == 65u);
          memset(&state, 0, sizeof(state));
          state_from_config(&state, &config);
          assert(state.game_sort == s);
          assert(state.dark_mode == dark);
          assert(state.backlight == 12);
          assert(state.fast_forward_mode == ffm);
          assert(state.fast_forward_speed == ffs);
          assert(state.hotkey_map[FXCG100_HOTKEY_SAVE_EXIT] == 65);
        }
      }
    }
  }
  config_from_state(&config, &state);
  config.lcd_extra |= (3u << 24);
  assert(!config_valid(&config));
  config_from_state(&config, &state);
  config.lcd_extra = (config.lcd_extra & ~(7u << 28)) |
    ((uint32_t)FXCG100_FF_SPEED_COUNT << 28);
  assert(!config_valid(&config));
  config_from_state(&config, &state);
  config.lcd_extra = (1u << 31);
  assert(!config_valid(&config));
  config_from_state(&config, &state);
  config.hotkey_extra |= (1u << 16);
  assert(!config_valid(&config));

  /* Unified panel refresh rate preset (bits 20..23, 0..9) round-trips in
   * lcd_extra without changing file size or version. */
  for (uint32_t r = 0; r <= 9; r++) {
    memset(&state, 0, sizeof(state));
    state.frame_limit = 1;
    state.dark_mode = 1;
    state.lcd_refresh = r;
    config_from_state(&config, &state);
    assert(config_valid(&config));
    assert(((config.lcd_extra >> 20) & 15u) == r);
    memset(&state, 0, sizeof(state));
    state_from_config(&state, &config);
    assert(state.lcd_refresh == r);
  }
  config.lcd_extra = (10u << 20);
  assert(!config_valid(&config));

  /* GBA BIOS mode (bits 4..5, 0..2) round-trips in lcd_extra without changing
   * file size or version, and rejects out-of-range value 3. */
  for (uint32_t b = 0; b < FXCG100_BIOS_MODE_COUNT; b++) {
    memset(&state, 0, sizeof(state));
    state.frame_limit = 1;
    state.dark_mode = 1;
    state.bios_mode = b;
    config_from_state(&config, &state);
    assert(config_valid(&config));
    assert(((config.lcd_extra >> 4) & 3u) == b);
    memset(&state, 0, sizeof(state));
    state_from_config(&state, &config);
    assert(state.bios_mode == b);
  }
  config_from_state(&config, &state);
  config.lcd_extra = (config.lcd_extra & ~(3u << 4)) |
    ((uint32_t)FXCG100_BIOS_MODE_COUNT << 4);
  assert(!config_valid(&config));

  /* Only 3 scaling filters exist (SMOOTH=0, SHARP=1, CRISP=2); filter 3 is invalid. */
  config_from_state(&config, &state);
  config.screen_filter = 2;
  assert(config_valid(&config));
  config.screen_filter = 3;
  assert(!config_valid(&config));

  puts("fx-CG config migration and v4 fields passed");
  return 0;
}
