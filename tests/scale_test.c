/* Host unit test for the fxcg100 RGB565 upscaling cores.
 * Build: cc -O2 -I../ports/fxcg100 scale_test.c -o scale_test && ./scale_test
 * (or from repo root: cc -O2 -Iports/fxcg100 tests/scale_test.c ...) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fxcg100_scale.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* Reference per-channel floor average. */
static uint16_t ref_avg(uint16_t x, uint16_t y)
{
	unsigned r = (((x >> 11) & 31) + ((y >> 11) & 31)) / 2;
	unsigned g = (((x >> 5) & 63) + ((y >> 5) & 63)) / 2;
	unsigned b = ((x & 31) + (y & 31)) / 2;

	return (uint16_t)((r << 11) | (g << 5) | b);
}

static void test_avg565(void)
{
	static const uint16_t edge[] = {
		0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x8410, 0x7BEF,
		0x0821, 0xF7DE, 0x5555, 0xAAAA, 0x1234, 0xFEDC
	};
	unsigned i, j;
	unsigned long seed = 0x2545F491u;

	for (i = 0; i < sizeof(edge) / sizeof(edge[0]); i++)
		for (j = 0; j < sizeof(edge) / sizeof(edge[0]); j++)
			CHECK(cgba_avg565(edge[i], edge[j]) ==
				ref_avg(edge[i], edge[j]),
				"avg565(%04X,%04X)=%04X want %04X",
				edge[i], edge[j],
				cgba_avg565(edge[i], edge[j]),
				ref_avg(edge[i], edge[j]));

	for (i = 0; i < 1000000; i++) {
		uint16_t x, y;

		seed = seed * 6364136223846793005ul + 1442695040888963407ul;
		x = (uint16_t)(seed >> 24);
		y = (uint16_t)(seed >> 44);
		if (cgba_avg565(x, y) != ref_avg(x, y)) {
			CHECK(0, "avg565(%04X,%04X)=%04X want %04X",
				x, y, cgba_avg565(x, y), ref_avg(x, y));
			break;
		}
	}
}

#define CANARY 0xC0DE

static void test_row_avg(void)
{
	_Alignas(4) uint16_t a[384], b[384], dst[386];
	unsigned i;
	unsigned long seed = 0x9E3779B9u;

	for (i = 0; i < 384; i++) {
		seed = seed * 6364136223846793005ul + 1442695040888963407ul;
		a[i] = (uint16_t)(seed >> 20);
		b[i] = (uint16_t)(seed >> 40);
	}
	dst[384] = CANARY; dst[385] = CANARY;
	cgba_scale_row_avg(a, b, dst, 384);
	for (i = 0; i < 384; i++)
		CHECK(dst[i] == ref_avg(a[i], b[i]),
			"row_avg[%u]=%04X want %04X", i, dst[i],
			ref_avg(a[i], b[i]));
	CHECK(dst[384] == CANARY && dst[385] == CANARY, "row_avg canary");
	cgba_scale_row_avg(a, b, dst, 320);
	for (i = 320; i < 384; i++)
		dst[i] = 0;
}

static void test_row_240_320(void)
{
	_Alignas(4) uint16_t src[240];
	_Alignas(4) uint16_t dst[322];
	unsigned i, g;

	/* constant row stays constant */
	for (i = 0; i < 240; i++)
		src[i] = 0x1234;
	dst[320] = CANARY; dst[321] = CANARY;
	cgba_scale_row_240_320(src, dst);
	for (i = 0; i < 320; i++)
		CHECK(dst[i] == 0x1234, "const 320 dst[%u]=%04X", i, dst[i]);
	CHECK(dst[320] == CANARY && dst[321] == CANARY, "320 canary tromped");

	/* distinct pixels land per the {a, ab, bc, c} pattern */
	for (i = 0; i < 240; i++)
		src[i] = (uint16_t)(i << 5);   /* distinct green ramp */
	cgba_scale_row_240_320(src, dst);
	for (g = 0; g < 80; g++) {
		uint16_t a = src[g * 3], b = src[g * 3 + 1], c = src[g * 3 + 2];

		CHECK(dst[g * 4 + 0] == a, "g%u tap0", g);
		CHECK(dst[g * 4 + 1] == cgba_avg565(a, b), "g%u tap1", g);
		CHECK(dst[g * 4 + 2] == cgba_avg565(b, c), "g%u tap2", g);
		CHECK(dst[g * 4 + 3] == c, "g%u tap3", g);
	}
}

static void test_row_240_384(void)
{
	_Alignas(4) uint16_t src[240];
	_Alignas(4) uint16_t dst[386];
	unsigned i, g;

	for (i = 0; i < 240; i++)
		src[i] = 0xBEEF;
	dst[384] = CANARY; dst[385] = CANARY;
	cgba_scale_row_240_384(src, dst);
	for (i = 0; i < 384; i++)
		CHECK(dst[i] == 0xBEEF, "const 384 dst[%u]=%04X", i, dst[i]);
	CHECK(dst[384] == CANARY && dst[385] == CANARY, "384 canary tromped");

	for (i = 0; i < 240; i++)
		src[i] = (uint16_t)(i << 5);
	cgba_scale_row_240_384(src, dst);
	for (g = 0; g < 48; g++) {
		const uint16_t *s = src + g * 5;
		uint16_t *o = dst + g * 8;

		CHECK(o[0] == s[0], "g%u t0", g);
		CHECK(o[1] == cgba_avg565(s[0], s[1]), "g%u t1", g);
		CHECK(o[2] == cgba_avg565(s[1], s[2]), "g%u t2", g);
		CHECK(o[3] == s[2], "g%u t3", g);
		CHECK(o[4] == cgba_avg565(s[2], s[3]), "g%u t4", g);
		CHECK(o[5] == s[3], "g%u t5", g);
		CHECK(o[6] == cgba_avg565(s[3], s[4]), "g%u t6", g);
		CHECK(o[7] == s[4], "g%u t7", g);
	}
}

static void test_vmap216(void)
{
	uint16_t vmap[216];
	unsigned t;
	unsigned prev = 0;

	cgba_scale_build_vmap216(vmap);
	CHECK((vmap[0] & CGBA_SCALE_VROW) == 0, "first row maps to src 0");
	CHECK((vmap[215] & CGBA_SCALE_VROW) == 159, "last row maps to src 159");
	for (t = 0; t < 216; t++) {
		unsigned s = vmap[t] & CGBA_SCALE_VROW;
		int blend = (vmap[t] & CGBA_SCALE_VBLEND) != 0;

		CHECK(s < 160, "t%u s=%u out of range", t, s);
		CHECK(!blend || s + 1 < 160, "t%u blends past the end", t);
		CHECK(s >= prev, "t%u not monotonic (%u < %u)", t, s, prev);
		CHECK(s - prev <= 1, "t%u skips a source row", t);
		prev = s;
	}
	/* every source row is consumed (27/20 > 1: no dropped rows) */
	{
		unsigned seen[160] = { 0 };

		for (t = 0; t < 216; t++)
			seen[vmap[t] & CGBA_SCALE_VROW] = 1;
		for (t = 0; t < 160; t++)
			CHECK(seen[t], "src row %u never sampled", t);
	}
}

/* The 4:3 vertical grouping used by the presenter: 53 groups of 3 source
 * rows -> 4 output rows covers 159 of 160 source rows and 212 output rows. */
static void test_43_geometry(void)
{
	CHECK(53 * 3 == 159, "4:3 consumes 159 source rows");
	CHECK(53 * 4 == 212, "4:3 produces 212 output rows");
	CHECK(17 * 12 + 8 == 212, "17 full strips + one 8-row strip");
	CHECK(12 * 320 * 2 <= 0x2000, "12-row 320px strip fits an XY bank");
	CHECK(8 * 384 * 2 <= 0x2000, "8-row 384px strip fits an XY bank");
	CHECK(27 * 8 == 216, "fullscreen: 27 strips of 8 rows");
}

int main(void)
{
	test_avg565();
	test_row_avg();
	test_row_240_320();
	test_row_240_384();
	test_vmap216();
	test_43_geometry();

	if (failures) {
		printf("scale_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("scale_test: all passed\n");
	return 0;
}
