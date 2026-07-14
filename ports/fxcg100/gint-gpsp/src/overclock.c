#include "overclock.h"

#ifdef CGBA_DEFAULT_OVERCLOCK
#include <gint/clock.h>

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
#endif

void cgba_overclock_init(void)
{
#ifdef CGBA_DEFAULT_OVERCLOCK
	/* Keep the overclock local to the add-in. gint restores the launcher's
	 * original CPG/BSC state for OS world switches and when main() returns,
	 * then reapplies F5 after each world switch back into gpSP. */
	cpg_set_overclock_setting(&ptune4_f5);
#endif
}
