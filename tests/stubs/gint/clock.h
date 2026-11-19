#ifndef CGBA_TEST_GINT_CLOCK_H
#define CGBA_TEST_GINT_CLOCK_H

#include <stdint.h>

struct cpg_overclock_setting
{
	uint32_t FLLFRQ, FRQCR;
	uint32_t CS0BCR, CS2BCR, CS3BCR, CS5aBCR;
	uint32_t CS0WCR, CS2WCR, CS3WCR, CS5aWCR;
};

void cpg_get_overclock_setting(struct cpg_overclock_setting *s);
void cpg_set_overclock_setting(struct cpg_overclock_setting const *s);

#endif
