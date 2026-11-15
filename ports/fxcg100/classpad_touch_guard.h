#ifndef CGBA_CLASSPAD_TOUCH_GUARD_H
#define CGBA_CLASSPAD_TOUCH_GUARD_H

/* Keeps gint's fx-CP400 touch driver from stopping the program.
 *
 * gint (src/touch, as of 463f180) reads the touch controller over I2C from
 * its key-scan timer interrupt, 128 times a second while the screen is
 * touched. Compared with what the calculator's OS does for the same job
 * (02.01.7003, routines at 0x80089f28..0x8008a5b6), three things are missing,
 * and each of them can take the whole program down:
 *
 * 1. Bus errors are fatal. gint's handlers for "arbitration lost" and "no
 *    acknowledge" call gint_panic(0x10e0). The OS's handlers (0x8008a050,
 *    0x8008a066) clear the flag, note the error and carry on.
 *
 * 2. It does not wait for the bus to be idle. Before a transfer the OS
 *    resets the module and waits, with a timeout, until the status register
 *    shows both lines high and the bus not busy (0x8008a54e: ICSR & 0xd0 ==
 *    0xc0). gint only waits for "not busy". Starting with a line still low
 *    is what loses arbitration: a crash report from a calculator (EXC=10E0
 *    while a touch key was pressed) has the interrupted PC on the instruction
 *    after the arbitration interrupt is enabled, before the start condition
 *    is even written.
 *
 * 3. Its waits have no way out. It sleeps inside the interrupt until the
 *    transfer is marked finished; a wake-up missed between the check and the
 *    sleep, or an interrupt that never comes, leaves the key-scan interrupt
 *    asleep for good, and the main program with it.
 *
 * A driver for the same controller that polls instead (the Windows CE port's
 * classpad_touch_bus.h) has run without trouble; before every read it
 * resets the module, clears the status register, and from then on treats
 * either error flag as "drop this sample", with every wait bounded. That is
 * the behaviour aimed at here.
 *
 * What this does about them, from outside gint:
 *
 * - cp_touch_guard_install() replaces the two error handlers. Arbitration
 *   lost before the transfer has begun: wait briefly for the lines to go
 *   idle, as the OS does, clear the flag and let the transfer start. Lost
 *   later, or no acknowledge: end the transfer and mark its sample bad.
 * - cp_touch_guard_tick(), called 128 times a second from a TMU timer, deals
 *   with the waits. A TMU interrupt outranks the key scan (an ETMU), so it
 *   still runs while that one is asleep, and returning from it wakes the
 *   sleep, which then looks at the transfer again. A transfer during which
 *   three ticks pass without a single I2C interrupt (a whole one takes well
 *   under a millisecond) is ended. Interrupts are what is counted, not the
 *   state of the request: the key scan also runs 128 times a second, so a
 *   tick can land at the same point of one healthy transfer after another,
 *   and they all look alike.
 * - The two ordinary I2C interrupts are passed on to gint's handlers, after
 *   being counted for that purpose and after setting the clock dividers
 *   (cp_touch_guard_set_clock): gint writes the dividers for the stock
 *   peripheral clock at the start of every transfer, which makes the bus
 *   run too fast once the calculator is overclocked.
 * - cp_touch_guard_take_bad_sample() tells the caller to ignore the sample
 *   of a transfer that was ended early.
 *
 * Not covered: gint also waits for "not busy" with interrupts blocked when
 * it starts a transfer; if that never came, nothing here could run.
 *
 * The struct below mirrors gint's private struct i2c_request_info. */

#include <gint/defs/call.h>
#include <gint/intc.h>
#include <gint/mpu/i2c.h>

#include <stddef.h>
#include <stdint.h>

#include "classpad_clock.h"

struct cp_gint_i2c_request {
	int mode, target_register;
	uint8_t *buffer;
	size_t buffer_size, buffer_size_remaining, buffer_cursor;
	int status;                       /* 1 when finished */
	int state;                        /* 0 before the first interrupt */
};
extern volatile struct cp_gint_i2c_request __i2c_request;

#define CP_I2C_STATUS_FINISHED 1
#define CP_I2C_STATE_START 0
#define CP_I2C_STATE_DEAD 2617

/* Both lines high and the bus not busy: SCLM | SDAM set, BUSY clear. */
#define CP_I2C_BUS_IDLE(icsr) (((icsr) & 0xd0) == 0xc0)

/* These are written from interrupt handlers for as long as gint runs, so
 * they go in .data: in gpSP that is in the part of the program that is
 * always there, unlike .bss, most of which lives in memory that is handed
 * back before the program ends. */
#define CP_TOUCH_GUARD_VAR __attribute__((section(".data.cp_touch_guard")))

/* What had to be done, for the diagnostics. */
static volatile uint32_t cp_touch_guard_stale CP_TOUCH_GUARD_VAR = 0;
static volatile uint32_t cp_touch_guard_errors CP_TOUCH_GUARD_VAR = 0;
static volatile uint32_t cp_touch_guard_stuck CP_TOUCH_GUARD_VAR = 0;
static volatile int cp_touch_guard_bad_sample CP_TOUCH_GUARD_VAR = 0;
static volatile uint32_t cp_touch_guard_interrupts CP_TOUCH_GUARD_VAR = 0;
/* Clock dividers to use instead of gint's; zero leaves gint's alone. */
static volatile uint8_t cp_touch_guard_iccl CP_TOUCH_GUARD_VAR = 0;
static volatile uint8_t cp_touch_guard_icch CP_TOUCH_GUARD_VAR = 0;

/* gint's own handlers for the two ordinary I2C interrupts. */
extern void i2c_inth_wait(void);
extern void i2c_inth_trans(void);

static inline uint32_t cp_touch_guard_events(void)
{
	return cp_touch_guard_stale + cp_touch_guard_errors + cp_touch_guard_stuck;
}

/* End the current transfer where it stands: no more I2C interrupts, the
 * request marked finished so gint's wait returns. */
static void cp_touch_guard_end_transfer(void)
{
	SH7305_I2C.ICIC.byte = 0x00;
	SH7305_I2C.ICSR.byte = 0x00;
	SH7305_I2C.ICCR.byte = 0x00;
	__i2c_request.state = CP_I2C_STATE_DEAD;
	__i2c_request.status = CP_I2C_STATUS_FINISHED;
	cp_touch_guard_bad_sample = 1;
}

static void cp_touch_guard_arbitration_lost(void)
{
	int i;

	if(__i2c_request.state != CP_I2C_STATE_START) {
		cp_touch_guard_end_transfer();
		cp_touch_guard_errors++;
		return;
	}
	/* The start condition has not been sent yet. Give the lines a moment to
	 * go idle (a few hundred microseconds at most), then let it go ahead.
	 * If they never do, the transfer stalls and the tick ends it. */
	for(i = 0; i < 4000 && !CP_I2C_BUS_IDLE(SH7305_I2C.ICSR.byte); i++)
		;
	SH7305_I2C.ICSR.AL = 0;
	cp_touch_guard_stale++;
}

static void cp_touch_guard_no_acknowledge(void)
{
	SH7305_I2C.ICCR.byte = 0x90;      /* stop condition */
	cp_touch_guard_end_transfer();
	cp_touch_guard_errors++;
}

static inline void cp_touch_guard_apply_clock(void)
{
	if(cp_touch_guard_iccl) {
		SH7305_I2C.ICCL = cp_touch_guard_iccl;
		SH7305_I2C.ICCH = cp_touch_guard_icch;
	}
}

static void cp_touch_guard_wait(void)
{
	cp_touch_guard_interrupts++;
	cp_touch_guard_apply_clock();
	i2c_inth_wait();
}

static void cp_touch_guard_trans(void)
{
	cp_touch_guard_interrupts++;
	cp_touch_guard_apply_clock();
	i2c_inth_trans();
}

static inline void cp_touch_guard_install(void)
{
	intc_handler_function(0xe00, GINT_CALL(cp_touch_guard_arbitration_lost));
	intc_handler_function(0xe20, GINT_CALL(cp_touch_guard_no_acknowledge));
	intc_handler_function(0xe40, GINT_CALL(cp_touch_guard_wait));
	intc_handler_function(0xe60, GINT_CALL(cp_touch_guard_trans));
}

/* Tell the guard the peripheral clock, after any change to it. */
static inline void cp_touch_guard_set_clock(uint32_t pphi_hz)
{
	uint32_t l, h;

	cp_clock_i2c_counts(pphi_hz, &l, &h);
	cp_touch_guard_icch = (uint8_t)h;
	cp_touch_guard_iccl = (uint8_t)l;
}

/* Call from a TMU timer callback, 128 times a second. */
static inline void cp_touch_guard_tick(void)
{
	static int stuck_ticks CP_TOUCH_GUARD_VAR = 0;
	static uint32_t last_interrupts CP_TOUCH_GUARD_VAR = 0;

	if(__i2c_request.status == CP_I2C_STATUS_FINISHED ||
			cp_touch_guard_interrupts != last_interrupts) {
		stuck_ticks = 0;
		last_interrupts = cp_touch_guard_interrupts;
	} else if(++stuck_ticks >= 3) {
		cp_touch_guard_end_transfer();
		cp_touch_guard_stuck++;
		stuck_ticks = 0;
	}
}

/* Nonzero once after a transfer was ended early: the sample gint holds now
 * is incomplete and should be ignored. */
static inline int cp_touch_guard_take_bad_sample(void)
{
	int bad = cp_touch_guard_bad_sample;

	cp_touch_guard_bad_sample = 0;
	return bad;
}

#endif /* CGBA_CLASSPAD_TOUCH_GUARD_H */
