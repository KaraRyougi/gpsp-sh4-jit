/*
 * The built-in Ptune 4 F5 profile must not downclock a faster setting the
 * user applied before launching gpSP (e.g. a custom Ptune profile).
 */

#include <stdio.h>
#include <string.h>

#define CGBA_DEFAULT_OVERCLOCK 1
#include "ports/fxcg100/gint-gpsp/src/overclock.c"

static int failures;
static struct cpg_overclock_setting hardware;
static int set_calls;

void cpg_get_overclock_setting(struct cpg_overclock_setting *s)
{
	*s = hardware;
}

void cpg_set_overclock_setting(struct cpg_overclock_setting const *s)
{
	hardware = *s;
	set_calls++;
}

static void check(int condition, const char *name)
{
	if(condition)
		return;
	printf("FAIL: %s\n", name);
	failures++;
}

/* FRQCR with STC = pll-1 and the IFC/SFC/BFC/P1FC divider exponents. */
static uint32_t frqcr(unsigned pll, unsigned ifc, unsigned sfc, unsigned bfc,
	unsigned pfc)
{
	return ((uint32_t)(pll - 1) << 24) | (ifc << 20) | (sfc << 12)
		| (bfc << 8) | pfc;
}

static struct cpg_overclock_setting launcher(uint32_t fllfrq, uint32_t frq)
{
	struct cpg_overclock_setting s;
	memset(&s, 0, sizeof s);
	s.FLLFRQ = fllfrq;
	s.FRQCR = frq;
	s.CS3WCR = 0x000024d1;
	return s;
}

/* Returns 1 when cgba_overclock_init() switched the hardware to Ptune 4 F5. */
static int boots_into_f5(struct cpg_overclock_setting start)
{
	hardware = start;
	set_calls = 0;
	cgba_overclock_init();
	if(set_calls == 0)
		return memcmp(&hardware, &start, sizeof start) == 0 ? 0 : -1;
	return set_calls == 1
		&& memcmp(&hardware, &ptune4_f5, sizeof hardware) == 0;
}

int main(void)
{
	cgba_cpg_rates f5 = cgba_cpg_rates_of(&ptune4_f5);
	check(f5.cpu == 235929600u, "F5 CPU is 235.93 MHz");
	check(f5.shway == 117964800u, "F5 SuperHyway is 117.96 MHz");
	check(f5.bus == 117964800u, "F5 bus is 117.96 MHz");

	/* fx-CG50/fx-CG100 OS default (gint F1): 117.96 MHz CPU. */
	check(boots_into_f5(launcher(0x4384, 0x0f011112)) == 1,
		"stock clock is overclocked to F5");
	/* gint F4: same CPU as F5 but a 58.98 MHz bus. */
	check(boots_into_f5(launcher(0x4384, frqcr(32, 0, 1, 2, 3))) == 1,
		"slower-bus profile is replaced by F5");
	check(boots_into_f5(ptune4_f5) == 1,
		"launcher already at F5 keeps F5");
	/* Same frequencies with different memory waits are not faster. */
	check(boots_into_f5(launcher(0x4384, ptune4_f5.FRQCR)) == 1,
		"equal clocks still apply the tested F5 timings");
	/* SELXM=0 with half the FLL ratio is the same 235.93 MHz. */
	check(boots_into_f5(launcher(0x01c2, ptune4_f5.FRQCR)) == 1,
		"SELXM halving is accounted for");

	/* PLL 36x with F5's dividers: 265.42 MHz CPU, 132.71 MHz bus. */
	check(boots_into_f5(launcher(0x4384, frqcr(36, 0, 1, 1, 3))) == 0,
		"faster PLL profile is kept");
	/* SELXM=0 doubles the FLL output: 471.86 MHz CPU. */
	check(boots_into_f5(launcher(0x0384, ptune4_f5.FRQCR)) == 0,
		"faster FLL profile is kept");
	/* Same CPU, bus at 1/2: only the bus is faster. */
	check(boots_into_f5(launcher(0x4384, frqcr(32, 0, 1, 0, 3))) == 0,
		"faster-bus profile with equal CPU is kept");

	/* PLL 36x, bus 1/8: faster CPU but a 66.36 MHz bus is a trade-off. */
	check(boots_into_f5(launcher(0x4384, frqcr(36, 0, 1, 2, 3))) == 1,
		"faster CPU with slower bus is replaced by F5");

	if(failures)
		return 1;
	puts("overclock keeps faster launcher clocks");
	return 0;
}
