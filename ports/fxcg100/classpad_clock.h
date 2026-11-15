#ifndef CGBA_CLASSPAD_CLOCK_H
#define CGBA_CLASSPAD_CLOCK_H

/* The SH7305 clock chain as the fx-CP400 clock page works on it.
 *
 * The method and the tables are Ptune4's (Xtune/CPtune4 by CalcLoverHK and
 * Sentaro21), by way of the Windows CE port's cetune: the player sets the
 * chain - FLL and PLL multipliers and the four dividers - and everything
 * that has to follow the resulting clocks is worked out from them:
 *
 *   - a divider step moves its neighbours so the chain stays legal;
 *   - each clock has an upper limit, and a step past one is refused;
 *   - the flash read wait and the SDRAM TRC follow the bus clock;
 *   - the touch controller's I2C dividers follow the peripheral clock
 *     (classpad_touch_guard.h).
 *
 * Clock chain:
 *   FLL output = 32768 Hz x FLL          (halved when SELXM = 1)
 *   PLL output = FLL output x PLL
 *   CPU (I), SuperHyway (S), bus (B), peripheral (P) = PLL output / divider
 *
 *   FRQCR   STC 29:24, IFC 23:20, SFC 15:12, BFC 11:8, PFC 3:0
 *   FLLFRQ  SELXM 15:14, FLF 10:0
 *   CS0WCR  WR 10:7   (flash read waits)
 *   CS3WCR  TRC 1:0   (SDRAM)
 *
 * This header is arithmetic only; it touches no hardware and is shared with
 * the host tests. The registers are held in the same ten words, in the same
 * order, as gint's struct cpg_overclock_setting. */

#include <stddef.h>
#include <stdint.h>

typedef struct cp_clock {
	uint32_t FLLFRQ, FRQCR;
	uint32_t CS0BCR, CS2BCR, CS3BCR, CS5aBCR;
	uint32_t CS0WCR, CS2WCR, CS3WCR, CS5aWCR;
} cp_clock;

/* Rows of the chain, and indices into the frequency and limit tables. */
enum {
	CP_CLK_FLL = 0,
	CP_CLK_PLL,
	CP_CLK_CPU,
	CP_CLK_SH,
	CP_CLK_BUS,
	CP_CLK_PERIPH,
	CP_CLK_COUNT
};

#define CP_CLK_FLL_MIN 450u
#define CP_CLK_FLL_MAX 2047u
#define CP_CLK_PLL_MAX 64u
#define CP_CLK_PERIPH_MIN 1000000u

#define CP_CLK_FRQCR_FIELDS  0x3ff0ff0fu     /* STC, IFC, SFC, BFC, PFC */
#define CP_CLK_FLLFRQ_FIELDS 0x0000c7ffu     /* SELXM, FLF */
#define CP_CLK_WR_MASK       0x00000780u
#define CP_CLK_WR_SHIFT      7
#define CP_CLK_TRC_MASK      0x00000003u
#define CP_CLK_WR_SAFE       11u             /* 18 waits while the clock moves */
#define CP_CLK_TRC_SAFE      3u              /* 9 cycles */

/* The chain, decoded. div[] holds the divisors themselves (2..64) for the
 * CPU, SuperHyway, bus and peripheral clocks. */
typedef struct cp_chain {
	uint32_t fll, selxm, pll, div[4];
} cp_chain;

/* Xtune's limits for the fx-CP400, by CP_CLK_* index (the FLL has none).
 *
 * cp_clock_limit is what Xtune starts with: its default settings, the same
 * for every model except for the peripheral clock, and meant as a cautious
 * range rather than as what the hardware can do. cp_clock_hard_limit is
 * what it never goes past, and what applies here in "unlocked" mode. Between
 * the two, nothing has been measured: the wait tables below end at a
 * 130 MHz bus. */
static const uint32_t cp_clock_limit[CP_CLK_COUNT] = {
	0, 800000000u, 275000000u, 180000000u, 134000000u, 50000000u
};
static const uint32_t cp_clock_hard_limit[CP_CLK_COUNT] = {
	0, 1000000000u, 350000000u, 200000000u, 200000000u, 60000000u
};
/* The highest bus clock the wait tables have a measured setting for. */
#define CP_CLK_BUS_MEASURED 130000000u
static const char *const cp_clock_name[CP_CLK_COUNT] = {
	"FLL", "PLL", "CPU", "SuperHyway", "Bus", "Peripheral"
};

static inline void cp_chain_decode(const cp_clock *c, cp_chain *ch)
{
	ch->fll = c->FLLFRQ & 0x7ffu;
	ch->selxm = ((c->FLLFRQ >> 14) & 3u) == 1u;
	ch->pll = ((c->FRQCR >> 24) & 63u) + 1u;
	ch->div[0] = 2u << ((c->FRQCR >> 20) & 15u);
	ch->div[1] = 2u << ((c->FRQCR >> 12) & 15u);
	ch->div[2] = 2u << ((c->FRQCR >> 8) & 15u);
	ch->div[3] = 2u << (c->FRQCR & 15u);
}

static inline uint32_t cp_chain_div_code(uint32_t div)
{
	uint32_t code = 0;

	while (div > 2u) {
		div >>= 1;
		code++;
	}
	return code;
}

/* Rewrites only the chain's fields of the two registers. */
static inline void cp_chain_encode(const cp_chain *ch, cp_clock *c)
{
	c->FLLFRQ = (c->FLLFRQ & ~CP_CLK_FLLFRQ_FIELDS) |
		(ch->selxm ? 0x4000u : 0u) | (ch->fll & 0x7ffu);
	c->FRQCR = (c->FRQCR & ~(CP_CLK_FRQCR_FIELDS | 0x80000000u)) |
		((ch->pll - 1u) << 24) | (cp_chain_div_code(ch->div[0]) << 20) |
		(cp_chain_div_code(ch->div[1]) << 12) |
		(cp_chain_div_code(ch->div[2]) << 8) | cp_chain_div_code(ch->div[3]);
}

/* hz[CP_CLK_*]: the FLL and PLL outputs and the four clocks.
 * 32768 x 2047 x 64 still fits 32 bits. */
static inline void cp_chain_hz(const cp_chain *ch, uint32_t hz[CP_CLK_COUNT])
{
	unsigned i;

	hz[CP_CLK_FLL] = (32768u * ch->fll) >> (ch->selxm ? 1 : 0);
	hz[CP_CLK_PLL] = hz[CP_CLK_FLL] * ch->pll;
	for (i = 0; i < 4; i++)
		hz[CP_CLK_CPU + i] = hz[CP_CLK_PLL] / ch->div[i];
}

/* One of the frequencies of a setting, by CP_CLK_* index. */
static inline uint32_t cp_clock_hz(const cp_clock *c, unsigned which)
{
	cp_chain ch;
	uint32_t hz[CP_CLK_COUNT];

	cp_chain_decode(c, &ch);
	cp_chain_hz(&ch, hz);
	return hz[which];
}

/* Largest allowed ratio div[later] / div[earlier] (Xtune's divs_ratio). */
static inline uint32_t cp_chain_max_ratio(unsigned later, unsigned earlier)
{
	static const uint8_t ratio[3][3] = {
		{ 2, 0, 0 }, { 8, 4, 0 }, { 16, 8, 8 }
	};

	return ratio[later - 1][earlier];
}

/* Index (1..5) of the first clock above its limit, or 0. `limit` is one of
 * the two tables above. */
static inline int cp_chain_over_limit(const cp_chain *ch,
	const uint32_t *limit)
{
	uint32_t hz[CP_CLK_COUNT];
	int i;

	cp_chain_hz(ch, hz);
	for (i = CP_CLK_PLL; i < CP_CLK_COUNT; i++)
		if (hz[i] > limit[i])
			return i;
	return 0;
}

/* Why a setting must not be applied, or NULL if it may be: the shape of the
 * chain (each clock no faster than the one before it and within the allowed
 * ratio of it), the FLL range, and the limits. */
static inline const char *cp_clock_problem(const cp_clock *c,
	const uint32_t *limit)
{
	static const char *const over[CP_CLK_COUNT] = {
		NULL, "PLL output is over its limit", "CPU clock is over its limit",
		"SuperHyway clock is over its limit", "Bus clock is over its limit",
		"Peripheral clock is over its limit"
	};
	cp_chain ch;
	uint32_t hz[CP_CLK_COUNT];
	unsigned i, j;

	if (((c->FRQCR >> 20) & 15u) > 5u || ((c->FRQCR >> 12) & 15u) > 5u ||
			((c->FRQCR >> 8) & 15u) > 5u || (c->FRQCR & 15u) > 5u)
		return "A divider is out of range";
	cp_chain_decode(c, &ch);
	if (ch.fll < CP_CLK_FLL_MIN)
		return "FLL is out of range";
	for (i = 1; i < 4; i++)
		for (j = 0; j < i; j++)
			if (ch.div[i] < ch.div[j] ||
					ch.div[i] / ch.div[j] > cp_chain_max_ratio(i, j))
				return "The dividers do not fit each other";
	cp_chain_hz(&ch, hz);
	if (hz[CP_CLK_PERIPH] < CP_CLK_PERIPH_MIN)
		return "Peripheral clock below 1 MHz";
	i = (unsigned)cp_chain_over_limit(&ch, limit);
	return i ? over[i] : NULL;
}

/* One step of a row (CP_CLK_*) in a direction (+1 = faster). `big` moves the
 * FLL by 50 instead of 1. Returns 0 when the chain changed, -1 at the end
 * of the range, or the index of the clock whose limit refused the step.
 *
 * Dividers move their neighbours as in Xtune: equal ones follow, the ratio
 * table drags the rest. The peripheral divider steps down by itself when a
 * multiplier pushes that clock over its limit, and back up when there is
 * room. */
static inline int cp_chain_step(cp_chain *chain, int row, int dir, int big,
	const uint32_t *limit)
{
	cp_chain c = *chain;
	uint32_t hz[CP_CLK_COUNT];
	int i, over;

	if (row == CP_CLK_FLL) {
		uint32_t step = big ? 50u : 1u;

		if (dir > 0) {
			if (c.fll >= CP_CLK_FLL_MAX)
				return -1;
			c.fll = c.fll + step > CP_CLK_FLL_MAX ? CP_CLK_FLL_MAX
				: c.fll + step;
		} else {
			if (c.fll <= CP_CLK_FLL_MIN)
				return -1;
			c.fll = c.fll < CP_CLK_FLL_MIN + step ? CP_CLK_FLL_MIN
				: c.fll - step;
		}
	} else if (row == CP_CLK_PLL) {
		if (dir > 0) {
			if (c.pll >= CP_CLK_PLL_MAX)
				return -1;
			c.pll++;
		} else {
			if (c.pll <= 1u)
				return -1;
			c.pll--;
		}
	} else {
		int k = row - CP_CLK_CPU;

		if (dir > 0) {
			if (c.div[k] <= 2u)
				return -1;
			for (i = k - 1; i >= 0; i--)
				if (c.div[i] == c.div[k])
					c.div[i] >>= 1;
			c.div[k] >>= 1;
			for (i = k + 1; i < 4; i++)
				if (c.div[i] / c.div[k] >
						cp_chain_max_ratio((unsigned)i, (unsigned)k))
					c.div[i] >>= 1;
		} else {
			if (c.div[k] >= 64u)
				return -1;
			for (i = k + 1; i < 4; i++)
				if (c.div[i] == c.div[k])
					c.div[i] <<= 1;
			c.div[k] <<= 1;
			for (i = k - 1; i >= 0; i--)
				if (c.div[k] / c.div[i] >
						cp_chain_max_ratio((unsigned)k, (unsigned)i))
					c.div[i] <<= 1;
		}
	}

	if (row <= CP_CLK_PLL) {
		cp_chain_hz(&c, hz);
		if (dir > 0) {
			/* only while the ratio table still allows a slower one */
			while (hz[CP_CLK_PERIPH] > limit[CP_CLK_PERIPH] &&
					c.div[3] < 64u &&
					(c.div[3] << 1) / c.div[0] <= cp_chain_max_ratio(3, 0) &&
					(c.div[3] << 1) / c.div[1] <= cp_chain_max_ratio(3, 1) &&
					(c.div[3] << 1) / c.div[2] <= cp_chain_max_ratio(3, 2)) {
				c.div[3] <<= 1;
				hz[CP_CLK_PERIPH] >>= 1;
			}
		} else if (hz[CP_CLK_PERIPH] * 2u <= limit[CP_CLK_PERIPH] &&
				c.div[3] > c.div[2]) {
			c.div[3] >>= 1;
		}
	}
	if (dir > 0 && (over = cp_chain_over_limit(&c, limit)) != 0)
		return over;
	*chain = c;
	return 0;
}

/* ---- what follows the clocks ---------------------------------------------- */

/* Wait fields for a bus clock: the lowest setting whose measured ceiling,
 * less a 5 % margin, still covers it (Xtune's fx-CP400 tables). */
static inline uint32_t cp_clock_best_flash_wait(uint32_t bus_hz)
{
	static const uint8_t mhz[10] = {
		13, 23, 33, 43, 53, 62, 72, 92, 110, 130
	};
	uint32_t i;

	for (i = 0; i < 10; i++)
		if (bus_hz <= mhz[i] * 950000u)
			return i;
	return 10;
}

static inline uint32_t cp_clock_best_trc(uint32_t bus_hz)
{
	static const uint8_t mhz[3] = { 80, 105, 127 };
	uint32_t i;

	for (i = 0; i < 3; i++)
		if (bus_hz <= mhz[i] * 950000u)
			return i;
	return 3;
}

static inline unsigned cp_clock_wait_cycles(uint32_t code)
{
	static const uint8_t wait[16] = {
		0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 18, 24, 24, 24, 24
	};

	return wait[code & 15u];
}

static inline unsigned cp_clock_trc_cycles(uint32_t code)
{
	static const uint8_t cycles[4] = { 3, 4, 6, 9 };

	return cycles[code & 3u];
}

static inline uint32_t cp_clock_flash_wait(const cp_clock *c)
{
	return (c->CS0WCR & CP_CLK_WR_MASK) >> CP_CLK_WR_SHIFT;
}

static inline uint32_t cp_clock_trc(const cp_clock *c)
{
	return c->CS3WCR & CP_CLK_TRC_MASK;
}

/* The whole setting for a chain: `stock` with the chain put in and the two
 * wait fields set for the bus clock that results.
 *
 * The tables give the least each field can be at a bus clock, as measured
 * for Xtune; the calculator's own setting is not always that low. So the
 * stock setting is taken as a second reference: a faster bus than stock's
 * never gets less than stock has, and a slower one never more. A chain that
 * leaves the bus clock where stock has it keeps stock's waits, so choosing
 * the stock chain gives back the stock setting exactly. */
static inline cp_clock cp_clock_for_chain(const cp_clock *stock,
	const cp_chain *ch)
{
	cp_clock c = *stock;
	uint32_t bus;

	cp_chain_encode(ch, &c);
	bus = cp_clock_hz(&c, CP_CLK_BUS);
	if (bus != cp_clock_hz(stock, CP_CLK_BUS)) {
		int faster = bus > cp_clock_hz(stock, CP_CLK_BUS);
		uint32_t wait = cp_clock_best_flash_wait(bus);
		uint32_t trc = cp_clock_best_trc(bus);
		uint32_t stock_wait = cp_clock_flash_wait(stock);
		uint32_t stock_trc = cp_clock_trc(stock);

		if (faster ? wait < stock_wait : wait > stock_wait)
			wait = stock_wait;
		if (faster ? trc < stock_trc : trc > stock_trc)
			trc = stock_trc;
		c.CS0WCR = (c.CS0WCR & ~CP_CLK_WR_MASK) | (wait << CP_CLK_WR_SHIFT);
		c.CS3WCR = (c.CS3WCR & ~CP_CLK_TRC_MASK) | trc;
	}
	return c;
}

/* Dividers for the touch controller's I2C clock. SCL is the peripheral
 * clock / (low + high); the OS and gint use 0x29 and 0x22, about 393 kHz at
 * the stock 29.4912 MHz. For another peripheral clock, keep SCL at or below
 * that in the same low:high proportion, and never use fewer counts than
 * stock (the Windows CE port's ClasspadIicCounts). */
#define CP_CLK_I2C_LOW  0x29u
#define CP_CLK_I2C_HIGH 0x22u
#define CP_CLK_I2C_HZ   393216u

static inline void cp_clock_i2c_counts(uint32_t periph_hz, uint32_t *low,
	uint32_t *high)
{
	uint32_t l = CP_CLK_I2C_LOW, h = CP_CLK_I2C_HIGH, total;
	uint32_t stock = CP_CLK_I2C_LOW + CP_CLK_I2C_HIGH;

	if (periph_hz) {
		total = (periph_hz + CP_CLK_I2C_HZ - 1u) / CP_CLK_I2C_HZ;
		if (total > stock) {
			l = (total * CP_CLK_I2C_LOW + stock - 1u) / stock;
			h = total - l;
		}
		if (l > 255u) l = 255u;
		if (h > 255u) h = 255u;
	}
	*low = l;
	*high = h;
}

static inline int cp_clock_equal(const cp_clock *a, const cp_clock *b)
{
	unsigned i;

	for (i = 0; i < 10; i++)
		if ((&a->FLLFRQ)[i] != (&b->FLLFRQ)[i])
			return 0;
	return 1;
}

/* A change is made in three writes, so that the bus is never faster than
 * its wait setting allows (the code making the change runs from SDRAM):
 *   step 0  the old clock with the slowest waits
 *   step 1  the new clock with the slowest waits
 *   step 2  the new clock with its own waits */
static inline cp_clock cp_clock_apply_step(const cp_clock *from,
	const cp_clock *to, int step)
{
	cp_clock c = step == 0 ? *from : *to;

	if (step < 2) {
		c.CS0WCR = (c.CS0WCR & ~CP_CLK_WR_MASK) |
			(CP_CLK_WR_SAFE << CP_CLK_WR_SHIFT);
		c.CS3WCR |= CP_CLK_TRC_SAFE;
	}
	return c;
}

/* "CPU x2" from a stock setting: one step faster on the CPU divider. The
 * SuperHyway clock is dragged along if the ratio table requires it; the bus
 * and peripheral clocks, and so the memory timings and the touch bus, stay
 * as they are. Returns zero if that is not possible within the limits. */
static inline int cp_clock_cpu_x2(const cp_clock *stock, cp_clock *out)
{
	cp_chain ch;

	cp_chain_decode(stock, &ch);
	if (cp_chain_step(&ch, CP_CLK_CPU, 1, 0, cp_clock_limit) != 0)
		return 0;
	*out = cp_clock_for_chain(stock, &ch);
	return cp_clock_problem(out, cp_clock_limit) == NULL;
}

/* ---- saved setting -------------------------------------------------------- */

#define CP_CLOCK_FILE_MAGIC 0x47504f45u   /* "GPOE" */

typedef struct cp_clock_file {
	uint32_t magic;
	uint32_t at_startup;   /* apply `clock` when gpSP starts */
	uint32_t unlocked;     /* the limits are switched off */
	cp_clock clock;
	uint32_t stock_frqcr;  /* the stock FRQCR it was made on */
	uint32_t check;        /* sum of all the words above */
} cp_clock_file;

static inline uint32_t cp_clock_file_sum(const cp_clock_file *f)
{
	const uint32_t *w = (const uint32_t *)(const void *)f;
	uint32_t sum = 0;
	unsigned i;

	for (i = 0; i < sizeof(*f) / 4u - 1u; i++)
		sum += w[i] * 31u + i;
	return sum;
}

static inline int cp_clock_file_valid(const cp_clock_file *f)
{
	return f->magic == CP_CLOCK_FILE_MAGIC && f->at_startup < 2u &&
		f->unlocked < 2u && f->check == cp_clock_file_sum(f) &&
		cp_clock_problem(&f->clock, f->unlocked ? cp_clock_hard_limit
			: cp_clock_limit) == NULL;
}

#endif /* CGBA_CLASSPAD_CLOCK_H */
