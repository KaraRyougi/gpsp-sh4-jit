/* Host stand-in for gint's timers: one repeating timer on a virtual clock
 * (tests/frame_pacing_test.c). */
#ifndef CGBA_TEST_PACING_TIMER_H
#define CGBA_TEST_PACING_TIMER_H
#include <stdint.h>
typedef int (*gint_call_t)(void);
#define GINT_CALL(function_) (function_)
#define TIMER_ANY (-1)
#define TIMER_CONTINUE 0
#define TIMER_STOP 1
int timer_configure(int timer, uint64_t delay_us, gint_call_t callback);
void timer_start(int timer);
void timer_stop(int timer);
#endif
