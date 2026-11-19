#ifndef CGBA_TEST_CGUI_RTC_H
#define CGBA_TEST_CGUI_RTC_H

/* Host-test stand-in for gint's 128 Hz RTC tick count; the test sets the
 * time (tests/fxcg_ui_test.c). */

#include <stdint.h>

uint32_t rtc_ticks(void);

#endif
