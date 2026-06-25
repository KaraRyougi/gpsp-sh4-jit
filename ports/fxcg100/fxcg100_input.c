#include "fxcg100_platform.h"

int fxcg100_key_down(int basic_keycode)
{
  volatile uint16_t *kiudata = (volatile uint16_t *)0xa44b0000;
  int row = basic_keycode % 10;
  int col = basic_keycode / 10 - 1;
  int word = row >> 1;
  int bit = col + 8 * (row & 1);

  if (word < 0 || word >= 6 || bit < 0 || bit >= 16)
    return 0;

  return (kiudata[word] & (uint16_t)(1u << bit)) != 0;
}

uint32_t fxcg100_poll_app_keys(void)
{
  uint32_t keys = 0;

  if (fxcg100_key_down(77))
    keys |= FXCG100_APPKEY_SHIFT;
  if (fxcg100_key_down(69))
    keys |= FXCG100_APPKEY_HOME;
  if (fxcg100_key_down(34))
    keys |= FXCG100_APPKEY_AC;
  if (fxcg100_key_down(31) || fxcg100_key_down(48))
    keys |= FXCG100_APPKEY_EXE;
  if (fxcg100_key_down(27))
    keys |= FXCG100_APPKEY_MENU;
  if (fxcg100_key_down(49))
    keys |= FXCG100_APPKEY_UP;
  if (fxcg100_key_down(47))
    keys |= FXCG100_APPKEY_DOWN;
  if (fxcg100_key_down(58))
    keys |= FXCG100_APPKEY_LEFT;
  if (fxcg100_key_down(38))
    keys |= FXCG100_APPKEY_RIGHT;
  if (fxcg100_key_down(68))
    keys |= FXCG100_APPKEY_BACK;
  if (fxcg100_key_down(FXCG100_PHYSKEY_ON))
    keys |= FXCG100_APPKEY_ON;

  return keys;
}
