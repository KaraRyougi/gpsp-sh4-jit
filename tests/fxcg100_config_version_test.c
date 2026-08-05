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

  puts("fx-CG config frame-limit migration passed");
  return 0;
}
