#include "fxcg100_platform.h"

static int cg100_keycode_to_matrix(int keycode, int *row, int *col)
{
  switch (keycode) {
  case FXCG100_KEY_ON:       *row = 9; *col = 6; return 1;
  case FXCG100_KEY_HOME:     *row = 9; *col = 5; return 1;
  case FXCG100_KEY_PREVTAB:  *row = 9; *col = 4; return 1;
  case FXCG100_KEY_UP:       *row = 9; *col = 3; return 1;
  case FXCG100_KEY_NEXTTAB:  *row = 9; *col = 2; return 1;
  case FXCG100_KEY_PAGEUP:   *row = 9; *col = 1; return 1;
  case FXCG100_KEY_SETTINGS: *row = 8; *col = 6; return 1;
  case FXCG100_KEY_BACK:     *row = 8; *col = 5; return 1;
  case FXCG100_KEY_LEFT:     *row = 8; *col = 4; return 1;
  case FXCG100_KEY_OK:       *row = 8; *col = 3; return 1;
  case FXCG100_KEY_RIGHT:    *row = 8; *col = 2; return 1;
  case FXCG100_KEY_PAGEDOWN: *row = 8; *col = 1; return 1;
  case FXCG100_KEY_SHIFT:    *row = 7; *col = 6; return 1;
  case FXCG100_KEY_ALPHA:    *row = 7; *col = 5; return 1;
  case FXCG100_KEY_VARS:     *row = 7; *col = 4; return 1;
  case FXCG100_KEY_DOWN:     *row = 7; *col = 3; return 1;
  case FXCG100_KEY_CATALOG:  *row = 7; *col = 2; return 1;
  case FXCG100_KEY_TOOLS:    *row = 7; *col = 1; return 1;
  case FXCG100_KEY_XOT:      *row = 6; *col = 6; return 1;
  case FXCG100_KEY_FRAC:     *row = 6; *col = 5; return 1;
  case FXCG100_KEY_SQRT:     *row = 6; *col = 4; return 1;
  case FXCG100_KEY_POWER:    *row = 6; *col = 3; return 1;
  case FXCG100_KEY_SQUARE:   *row = 6; *col = 2; return 1;
  case FXCG100_KEY_EXP_FUN:  *row = 6; *col = 1; return 1;
  case FXCG100_KEY_COMMA:    *row = 5; *col = 6; return 1;
  case FXCG100_KEY_SIN:      *row = 5; *col = 5; return 1;
  case FXCG100_KEY_COS:      *row = 5; *col = 4; return 1;
  case FXCG100_KEY_TAN:      *row = 5; *col = 3; return 1;
  case FXCG100_KEY_LEFTP:    *row = 5; *col = 2; return 1;
  case FXCG100_KEY_RIGHTP:   *row = 5; *col = 1; return 1;
  case FXCG100_KEY_FORMAT:   *row = 1; *col = 3; return 1;
  default:
    *row = keycode >> 4;
    *col = 7 - (keycode & 7);
    return keycode > 0;
  }
}

int fxcg100_key_down(int keycode)
{
  volatile uint16_t *kiudata = (volatile uint16_t *)0xa44b0000;
  int row;
  int col;
  int word;
  int bit;

  if (!cg100_keycode_to_matrix(keycode, &row, &col))
    return 0;
  word = row >> 1;
  bit = col + 8 * (row & 1);
  if (word < 0 || word >= 6 || bit < 0 || bit >= 16)
    return 0;

  return (kiudata[word] & (uint16_t)(1u << bit)) != 0;
}

uint32_t fxcg100_poll_app_keys(void)
{
  uint32_t keys = 0;

  if (fxcg100_key_down(FXCG100_KEY_SHIFT))
    keys |= FXCG100_APPKEY_SHIFT;
  if (fxcg100_key_down(FXCG100_KEY_HOME))
    keys |= FXCG100_APPKEY_HOME;
  if (fxcg100_key_down(FXCG100_KEY_AC))
    keys |= FXCG100_APPKEY_AC;
  if (fxcg100_key_down(FXCG100_KEY_EXE) ||
      fxcg100_key_down(FXCG100_KEY_OK))
    keys |= FXCG100_APPKEY_EXE;
  if (fxcg100_key_down(FXCG100_KEY_TOOLS))
    keys |= FXCG100_APPKEY_MENU;
  if (fxcg100_key_down(FXCG100_KEY_UP))
    keys |= FXCG100_APPKEY_UP;
  if (fxcg100_key_down(FXCG100_KEY_DOWN))
    keys |= FXCG100_APPKEY_DOWN;
  if (fxcg100_key_down(FXCG100_KEY_LEFT))
    keys |= FXCG100_APPKEY_LEFT;
  if (fxcg100_key_down(FXCG100_KEY_RIGHT))
    keys |= FXCG100_APPKEY_RIGHT;
  if (fxcg100_key_down(FXCG100_KEY_BACK))
    keys |= FXCG100_APPKEY_BACK;
  if (fxcg100_key_down(FXCG100_PHYSKEY_ON))
    keys |= FXCG100_APPKEY_ON;

  return keys;
}
