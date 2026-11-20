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

  /* Version 2 had the same file size and zeroed reserved storage in the word
   * now used by frame_limit. It must migrate to the new enabled default. */
  config_from_state(&config, &state);
  config.version = CGBA_CONFIG_VERSION_LEGACY;
  config.frame_limit = 0;
  assert(config_valid(&config));
  memset(&state, 0, sizeof(state));
  state_from_config(&state, &config);
  assert(state.frame_limit == 1);
  assert(state.game_sort == FXCG100_GAME_SORT_NAME);

  /* Library sort mode round-trips in lcd_extra bits 24..25 without changing
   * file size or version. */
  for (uint32_t s = 0; s < FXCG100_GAME_SORT_COUNT; s++) {
    memset(&state, 0, sizeof(state));
    state.frame_limit = 1;
    state.game_sort = s;
    config_from_state(&config, &state);
    assert(config_valid(&config));
    memset(&state, 0, sizeof(state));
    state_from_config(&state, &config);
    assert(state.game_sort == s);
  }
  config.lcd_extra |= (3u << 24);
  assert(!config_valid(&config));

  puts("fx-CG config frame-limit migration passed");
  return 0;
}
