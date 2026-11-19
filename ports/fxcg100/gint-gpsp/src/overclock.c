#include "overclock.h"

#ifdef CGBA_DEFAULT_OVERCLOCK
#include <gint/clock.h>
#include <stdbool.h>
#include <stdint.h>

/* Ptune 4 F5 for fx-CG50/fx-CG100 (CalcLoverHK/Sentaro21).
 *
 *   PLL 32x, CPU 1/2: 235.93 MHz
 *   SuperHyway 1/4:   117.96 MHz
 *   SDRAM/bus 1/4:    117.96 MHz, CL2, TRC6
 *   Peripheral 1/16:   29.49 MHz
 *
 * This is intentionally explicit instead of clock_set_speed(F5): the gint
 * bundled with the current fx-CG100 toolchain aliases F5 to its older F4
 * profile and therefore leaves the SDRAM bus at 58.98 MHz.
 */
static struct cpg_overclock_setting const ptune4_f5 = {
	.FLLFRQ  = 0x00004384,
	.FRQCR   = 0x1f001103,
	.CS0BCR  = 0x44900400,
	.CS2BCR  = 0x00000000,
	.CS3BCR  = 0x04904400,
	.CS5aBCR = 0x17df0400,
	.CS0WCR  = 0x000004c0,
	.CS2WCR  = 0x00000000,
	.CS3WCR  = 0x000024d2,
	.CS5aWCR = 0x000203c1,
};

/* The SH7305 clocks gpSP's speed depends on, in Hz. */
typedef struct {
	uint64_t cpu;   /* Iphi */
	uint64_t shway; /* Sphi: SuperHyway between the CPU and the BSC */
	uint64_t bus;   /* Bphi: SDRAM and the NOR flash holding the ROM */
} cgba_cpg_rates;

/* Mirrors gint's sh7305_probe(). FLLFRQ and FRQCR fully determine the rates
 * because the CASIO OS always runs with the FLL and PLL enabled, and
 * cpg_set_overclock_setting() leaves PLLCR alone. */
static cgba_cpg_rates cgba_cpg_rates_of(struct cpg_overclock_setting const *s)
{
	uint64_t fll = s->FLLFRQ & 0x7ffu;
	if(((s->FLLFRQ >> 14) & 3u) == 1u)
		fll >>= 1;
	uint64_t pll = ((s->FRQCR >> 24) & 0x3fu) + 1u;
	uint64_t base = 32768u * fll * pll;

	cgba_cpg_rates rates;
	rates.cpu   = base >> (((s->FRQCR >> 20) & 0xfu) + 1u);
	rates.shway = base >> (((s->FRQCR >> 12) & 0xfu) + 1u);
	rates.bus   = base >> (((s->FRQCR >>  8) & 0xfu) + 1u);
	return rates;
}

/* A launcher setting beats the built-in profile when switching would slow
 * down at least one of CPU, SuperHyway or bus and speed up none of them.
 * Settings that trade one clock against another are not faster. */
static bool cgba_cpg_is_faster(struct cpg_overclock_setting const *launcher,
	struct cpg_overclock_setting const *profile)
{
	cgba_cpg_rates a = cgba_cpg_rates_of(launcher);
	cgba_cpg_rates b = cgba_cpg_rates_of(profile);

	if(a.cpu < b.cpu || a.shway < b.shway || a.bus < b.bus)
		return false;
	return a.cpu > b.cpu || a.shway > b.shway || a.bus > b.bus;
}
#endif

void cgba_overclock_init(void)
{
#ifdef CGBA_DEFAULT_OVERCLOCK
	/* Keep a faster clock the user already chose (e.g. a custom Ptune
	 * profile) instead of downclocking it to F5. */
	struct cpg_overclock_setting launcher;
	cpg_get_overclock_setting(&launcher);
	if(cgba_cpg_is_faster(&launcher, &ptune4_f5))
		return;

	/* Keep the overclock local to the add-in. gint restores the launcher's
	 * original CPG/BSC state for OS world switches and when main() returns,
	 * then reapplies F5 after each world switch back into gpSP. */
	cpg_set_overclock_setting(&ptune4_f5);
#endif
}
