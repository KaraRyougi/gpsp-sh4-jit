#ifndef CGBA_TEST_CGUI_DISPLAY_H
#define CGBA_TEST_CGUI_DISPLAY_H

/* Host-test stand-in for gint's fx-CG VRAM: 396x224 RGB565. The VRAM is
 * provided by the test (tests/fxcg_ui_test.c). */

#include <stdint.h>

#define DWIDTH 396
#define DHEIGHT 224

extern uint16_t *gint_vram;

#endif
