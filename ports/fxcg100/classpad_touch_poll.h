#ifndef CGBA_CLASSPAD_TOUCH_POLL_H
#define CGBA_CLASSPAD_TOUCH_POLL_H

/* Reads the ClassPad II touch controller by polling, in the main program,
 * instead of through gint's interrupt-driven I2C driver.
 *
 * Why: gint's driver runs each read inside its key-scan timer interrupt, on
 * a state machine advanced by nested I2C interrupts, with no reset before a
 * read, bus errors treated as fatal and waits without limit
 * (classpad_touch_guard.h lists what that led to). The Windows CE port reads
 * the same controller with one plain function that resets the bus, polls the
 * status register with a bound on every wait and gives up on the sample at
 * the first error - and has had no trouble, overclocked or not. This is that
 * function (bsp/classpad/SRC/INC/classpad_touch_bus.h there), with the same
 * order of operations.
 *
 * Taking the bus from gint. gint's key scan reads the controller whenever the
 * pen-detect pin (port R data register, bit 5) is low, and has no switch for
 * it. But it finds that register through one constant in its literal pool,
 * which is in RAM like the rest of the program. cp_touch_poll_takeover()
 * points that constant at a byte of ours that always reads "pen up", so gint
 * never starts a read again, and masks the I2C interrupts. The constant is
 * recognised by its neighbours: the same pool also holds the address of
 * gint's I2C request block and of the I2C clock register. If exactly one
 * such place is not found, nothing is changed and the caller stays with
 * gint's driver and the guard.
 *
 * A read takes about half a millisecond while the screen is touched and
 * nothing when it is not. */

#include <gint/clock.h>
#include <gint/cpu.h>
#include <gint/intc.h>
#include <gint/mpu/i2c.h>
#include <gint/touch.h>
#include <gint/drivers/touch.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "classpad_clock.h"
#include "classpad_touch_guard.h"     /* gint's I2C request block */

#define CP_TOUCH_PEN_PORT   0xa405013cu   /* PRDR; bit 5 low = pen down */
#define CP_TOUCH_ICCL_ADDR  0xa4470010u
#define CP_TOUCH_ADDRESS    0x40u         /* 7-bit I2C address */
#define CP_TOUCH_SNAPSHOT   0x84u         /* command: 16 bytes of sample */

#define CP_ICCR_ICE   0x80u
#define CP_ICCR_RACK  0x40u
#define CP_ICCR_TRS   0x10u
#define CP_ICCR_BBSY  0x04u
#define CP_ICCR_SCP   0x01u
#define CP_ICSR_BUSY  0x10u
#define CP_ICSR_AL    0x08u
#define CP_ICSR_TACK  0x04u
#define CP_ICSR_WAIT  0x02u
#define CP_ICSR_DTE   0x01u

/* What gint is given to read in place of the pen-detect port. In .data so
 * that it exists for as long as gint runs (see classpad_touch_guard.h). */
static volatile uint8_t cp_touch_poll_pen_up
	__attribute__((section(".data.cp_touch_poll"))) = 0xff;

/* For the diagnostics: reads given up, and the stage and status of the
 * last one that was. */
static uint32_t cp_touch_poll_failures;
static unsigned cp_touch_poll_stage, cp_touch_poll_status;

/* The three values the search looks for, kept inverted and in .data: were
 * they written out in this function, its own literal pool would be a second
 * place that matches. */
static volatile uint32_t cp_touch_poll_keys[3]
	__attribute__((section(".data.cp_touch_poll"))) = {
	~CP_TOUCH_PEN_PORT, ~CP_TOUCH_ICCL_ADDR, 0
};

/* Stop gint reading the touch controller. `text` .. `text_end` is the
 * program's code. Returns nonzero if gint's reads are now off. */
static __attribute__((noinline)) int cp_touch_poll_takeover(uint32_t *text,
	uint32_t *text_end)
{
	uint32_t pen_port = ~cp_touch_poll_keys[0];
	uint32_t clock_reg = ~cp_touch_poll_keys[1];
	uint32_t request = ~cp_touch_poll_keys[2];   /* set by the caller */
	uint32_t *found = NULL, *p, *q;
	int matches = 0, i;

	for(p = text; p < text_end; p++) {
		int has_request = 0, has_clock = 0;

		if(*p != pen_port)
			continue;
		for(q = p - 8; q <= p + 8; q++) {
			if(q < text || q >= text_end)
				continue;
			has_request |= *q == request;
			has_clock |= *q == clock_reg;
		}
		if(has_request && has_clock) {
			found = p;
			matches++;
		}
	}
	if(matches != 1)
		return 0;

	/* Let a read gint has in hand finish (it runs inside an interrupt, so
	 * from here it can only be seen as finished or not yet started). */
	for(i = 0; i < 100000 && __i2c_request.status != 1; i++)
		;
	cpu_atomic_start();
	*found = (uint32_t)(uintptr_t)&cp_touch_poll_pen_up;
	intc_priority(INTC_I2C_AL, 0);
	intc_priority(INTC_I2C_TACK, 0);
	intc_priority(INTC_I2C_WAIT, 0);
	intc_priority(INTC_I2C_DTE, 0);
	SH7305_I2C.ICIC.byte = 0;
	SH7305_I2C.ICCR.byte = 0;
	cpu_atomic_end();
	return 1;
}

static inline int cp_touch_poll_pen_down(void)
{
	return (*(volatile uint8_t *)CP_TOUCH_PEN_PORT & 0x20) == 0;
}

/* Wait for (status & mask) == expected. Zero on either error flag or when
 * the bound runs out; the bound is far beyond a byte time at any clock the
 * calculator can run. */
static inline int cp_touch_poll_wait(unsigned mask, unsigned expected,
	unsigned stage)
{
	unsigned i;

	cp_touch_poll_stage = stage;
	for(i = 0; i < 60000; i++) {
		unsigned status = SH7305_I2C.ICSR.byte;

		cp_touch_poll_status = status;
		if(status & (CP_ICSR_AL | CP_ICSR_TACK))
			return 0;
		if((status & mask) == expected)
			return 1;
	}
	return 0;
}

static inline void cp_touch_poll_release_wait(void)
{
	SH7305_I2C.ICSR.byte = SH7305_I2C.ICSR.byte & (uint8_t)~CP_ICSR_WAIT;
}

/* One read of `n` bytes of the snapshot. Nonzero if it completed. */
static inline int cp_touch_poll_bus_read(uint8_t *packet, int n)
{
	uint32_t low, high;
	int i;

	/* Start from a known state, at the clock of the moment. */
	cp_clock_i2c_counts((uint32_t)clock_freq()->Pphi_f, &low, &high);
	SH7305_I2C.ICCR.byte = 0;
	SH7305_I2C.ICCR.byte = CP_ICCR_ICE;
	SH7305_I2C.ICCL = (uint8_t)low;
	SH7305_I2C.ICCH = (uint8_t)high;
	SH7305_I2C.ICSR.byte = 0;
	SH7305_I2C.ICIC.byte = 0x0e;      /* AL, TACK, WAIT status; masked at INTC */

	if(!cp_touch_poll_wait(CP_ICSR_BUSY, 0, 1)) goto fail;
	SH7305_I2C.ICCR.byte = CP_ICCR_ICE | CP_ICCR_TRS | CP_ICCR_BBSY;
	if(!cp_touch_poll_wait(CP_ICSR_DTE, CP_ICSR_DTE, 2)) goto fail;
	SH7305_I2C.ICDR = CP_TOUCH_ADDRESS << 1;
	if(!cp_touch_poll_wait(CP_ICSR_WAIT, CP_ICSR_WAIT, 3)) goto fail;
	SH7305_I2C.ICDR = CP_TOUCH_SNAPSHOT;
	cp_touch_poll_release_wait();
	if(!cp_touch_poll_wait(CP_ICSR_WAIT, CP_ICSR_WAIT, 4)) goto fail;
	SH7305_I2C.ICCR.byte = CP_ICCR_ICE | CP_ICCR_TRS | CP_ICCR_BBSY;
	cp_touch_poll_release_wait();
	if(!cp_touch_poll_wait(CP_ICSR_DTE, CP_ICSR_DTE, 5)) goto fail;
	SH7305_I2C.ICDR = (CP_TOUCH_ADDRESS << 1) | 1;
	if(!cp_touch_poll_wait(CP_ICSR_WAIT, CP_ICSR_WAIT, 6)) goto fail;
	SH7305_I2C.ICCR.byte = CP_ICCR_ICE | CP_ICCR_SCP;
	cp_touch_poll_release_wait();
	for(i = 0; i < n; i++) {
		if(!cp_touch_poll_wait(CP_ICSR_WAIT, CP_ICSR_WAIT, 7)) goto fail;
		if(i == n - 1)                /* no acknowledge, then stop */
			SH7305_I2C.ICCR.byte = CP_ICCR_ICE | CP_ICCR_RACK;
		cp_touch_poll_release_wait();
		if(!cp_touch_poll_wait(CP_ICSR_DTE, CP_ICSR_DTE, 8)) goto fail;
		packet[i] = SH7305_I2C.ICDR;
	}
	if(!cp_touch_poll_wait(CP_ICSR_BUSY, 0, 9)) goto fail;
	SH7305_I2C.ICIC.byte = 0;
	SH7305_I2C.ICCR.byte = 0;
	return 1;
fail:
	SH7305_I2C.ICIC.byte = 0;         /* the next read starts from scratch */
	SH7305_I2C.ICCR.byte = 0;
	cp_touch_poll_failures++;
	return 0;
}

/* From the 16 bytes to dots on the screen: the same arithmetic as gint's
 * touch_adconv_get_conv() and touch_adconv_get_dots(), with the calibration
 * gint holds (taken from the OS where it is known). */
static inline void cp_touch_poll_convert(const uint8_t *packet,
	struct _touch_addots *dots)
{
	static int was_dual;
	touch_calibration_t cal;
	int x1, y1, z1, x2, y2, z2, threshold, big;
	unsigned raw[8], i;

	for(i = 0; i < 8; i++)
		raw[i] = (unsigned)(packet[2 * i] << 8) | packet[2 * i + 1];
	/* x1, y1, z1, gh, x2, y2, z2, dm */
	x1 = (int)(raw[0] >> 4);
	y1 = (int)(raw[1] >> 4);
	z1 = (int)(raw[2] >> 4);
	x2 = (int)(raw[4] >> 6) + (int)(raw[4] & 1u) * -0x400;
	y2 = (int)(raw[5] >> 6) + (int)(raw[5] & 1u) * -0x400;
	z2 = (int)(raw[6] >> 4) + (int)(raw[6] & 1u) * -0x1000;

	touch_calib_get(&cal);
	threshold = was_dual ? cal.dual_sensi_leave : cal.dual_sensi_entry;
	big = abs(x2) > abs(y2) ? abs(x2) : abs(y2);
	was_dual = abs(z2) >= threshold || big >= threshold;

	memset(dots, 0, sizeof(*dots));
	dots->touches = was_dual ? 2 : 1;
	dots->x1 = ((x1 - cal.x_base) * 0x100) / cal.x_div;
	dots->y1 = ((y1 - cal.y_base) * 0x100) / cal.y_div;
	dots->z1 = z1;
	if(was_dual) {
		int xmax = dots->x1 + x2 * 0x1ba / 0x100;
		int ymax = dots->y1 + y2 * 0x11f / 0x100;

		if(z2 >= 0) {
			dots->x2 = xmax;
			dots->y2 = ymax;
		} else {
			dots->x2 = xmax;
			dots->y2 = dots->y1;
			dots->y1 = ymax;
		}
	}
}

/* Find gint's read and switch it off; see the top of the file. The address
 * of gint's request block is handed to the search inverted, through .data,
 * for the same reason as its other two keys. */
static inline int cp_touch_poll_begin(uint32_t *text, uint32_t *text_end)
{
	cp_touch_poll_keys[2] = ~(uint32_t)(uintptr_t)&__i2c_request;
	return cp_touch_poll_takeover(text, text_end);
}

/* Sample the touch screen now. Nonzero if *dots was filled in (with zero
 * touches when the pen is up); zero if the read failed, in which case the
 * caller keeps what it had. */
static inline int cp_touch_poll_read(struct _touch_addots *dots)
{
	uint8_t packet[16];

	if(!cp_touch_poll_pen_down()) {
		memset(dots, 0, sizeof(*dots));
		return 1;
	}
	if(!cp_touch_poll_bus_read(packet, 16))
		return 0;
	cp_touch_poll_convert(packet, dots);
	return 1;
}

#endif /* CGBA_CLASSPAD_TOUCH_POLL_H */
