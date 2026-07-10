#ifndef TEST_GINT_KEYCODES_H
#define TEST_GINT_KEYCODES_H

/* Host-test copy of gint's classic CG keyboard matrix codes. Production
 * calculator builds include gint's own <gint/keycodes.h>; this shim keeps the
 * CG50 input-map regression test independent of an installed fxSDK. */
enum {
  KEY_F1 = 0x91, KEY_F2 = 0x92, KEY_F3 = 0x93,
  KEY_F4 = 0x94, KEY_F5 = 0x95, KEY_F6 = 0x96,

  KEY_SHIFT = 0x81, KEY_OPTN = 0x82, KEY_VARS = 0x83,
  KEY_MENU = 0x84, KEY_LEFT = 0x85, KEY_UP = 0x86,

  KEY_ALPHA = 0x71, KEY_SQUARE = 0x72, KEY_POWER = 0x73,
  KEY_EXIT = 0x74, KEY_DOWN = 0x75, KEY_RIGHT = 0x76,

  KEY_XOT = 0x61, KEY_LOG = 0x62, KEY_LN = 0x63,
  KEY_SIN = 0x64, KEY_COS = 0x65, KEY_TAN = 0x66,

  KEY_FRAC = 0x51, KEY_FD = 0x52, KEY_LEFTP = 0x53,
  KEY_RIGHTP = 0x54, KEY_COMMA = 0x55, KEY_ARROW = 0x56,

  KEY_7 = 0x41, KEY_8 = 0x42, KEY_9 = 0x43, KEY_DEL = 0x44,
  KEY_4 = 0x31, KEY_5 = 0x32, KEY_6 = 0x33,
  KEY_MUL = 0x34, KEY_DIV = 0x35,
  KEY_1 = 0x21, KEY_2 = 0x22, KEY_3 = 0x23,
  KEY_ADD = 0x24, KEY_SUB = 0x25,
  KEY_0 = 0x11, KEY_DOT = 0x12, KEY_EXP = 0x13,
  KEY_NEG = 0x14, KEY_EXE = 0x15,

  KEY_ACON = 0x07,
};

#endif
