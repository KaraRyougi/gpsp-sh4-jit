/* Host test for fx-CG50 / fx-CG100 backlight mapping (fxcg_backlight.h) and
 * the GBA framebuffer fast-forward / FPS overlays (frame_overlay.c). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/fxcg100_platform.h"
#include "ports/fxcg100/fxcg_backlight.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		failures++; \
	} \
} while (0)

static void test_backlight_tables(void)
{
	fxcg_r61524_bl sys = { 0u, 0x78u, 0x0fu };
	uint32_t lv;

	/* Panel version detection (matches gint's r61524.c). */
	CHECK(fxcg_panel_is_dcs(50u) && fxcg_panel_is_dcs(82u),
		"0x32 and 0x52 are MIPI-DCS panels");
	CHECK(!fxcg_panel_is_dcs(0u) && !fxcg_panel_is_dcs(0x1524u) &&
		!fxcg_panel_is_dcs(0xffffu), "other IDs are R61524");

	/* R61524: level 0 and out-of-range restore system; 1..8 monotonic in low
	 * range; 9..16 monotonic in high range. */
	{
		fxcg_r61524_bl r0 = fxcg_r61524_level(0u, sys);
		fxcg_r61524_bl r_oob = fxcg_r61524_level(17u, sys);

		CHECK(r0.high_bit == sys.high_bit && r0.duty == sys.duty &&
			r0.divider == sys.divider, "R61524 level 0 restores system");
		CHECK(r_oob.duty == sys.duty, "R61524 out-of-range restores system");
	}
	for (lv = 1u; lv <= 16u; lv++) {
		fxcg_r61524_bl cur = fxcg_r61524_level(lv, sys);

		CHECK(cur.high_bit == (lv >= 9u ? 1u : 0u),
			"R61524 range bit at level %u", (unsigned)lv);
		CHECK(cur.divider == 0x0fu, "R61524 divider at level %u", (unsigned)lv);
		if (lv > 1u && lv != 9u) {
			fxcg_r61524_bl prev = fxcg_r61524_level(lv - 1u, sys);
			CHECK(cur.duty > prev.duty,
				"R61524 duty not strictly increasing at %u (%u <= %u)",
				(unsigned)lv, cur.duty, prev.duty);
		}
	}
	/* R61524 saved-register sanitization. */
	{
		fxcg_r61524_bl ok = fxcg_r61524_sanitize_saved(1u, 0x00a0u, 0x00ffu);
		fxcg_r61524_bl bad_duty = fxcg_r61524_sanitize_saved(1u, 0x0000u, 0x000fu);
		fxcg_r61524_bl bad_div = fxcg_r61524_sanitize_saved(0u, 0x0078u, 0x0055u);

		CHECK(ok.high_bit == 1u && ok.duty == 0xa0u && ok.divider == 0xffu,
			"R61524 valid saved readback preserved");
		CHECK(bad_duty.high_bit == 0u && bad_duty.duty == 0xd2u &&
			bad_duty.divider == 0x0fu, "R61524 zero duty falls back to OS mid");
		CHECK(bad_div.duty == 0x78u && bad_div.divider == 0x0fu,
			"R61524 invalid divider falls back to 0x0F");
	}

	/* 8-bit DCS (HX8394-F / ClassPad 0x52) & 12-bit DCS (HX8399-C). */
	CHECK(fxcg_dcs8_level(0u, 0x42u) == 0x42u, "DCS8 level 0 restores system");
	CHECK(fxcg_dcs8_level(1u, 0x42u) == 15u &&
		fxcg_dcs8_level(16u, 0x42u) == 255u, "DCS8 1..16 span 15..255");
	CHECK(fxcg_dcs8_sanitize_saved(0u) == 0x50u &&
		fxcg_dcs8_sanitize_saved(0x64u) == 0x64u, "DCS8 sanitize");

	CHECK(fxcg_dcs12_level(0u, 0x0654u) == 0x0654u,
		"DCS12 level 0 restores system");
	CHECK(fxcg_dcs12_level(1u, 0x0654u) == 0x00ffu &&
		fxcg_dcs12_level(16u, 0x0654u) == 0x0fffu,
		"DCS12 1..16 span 0x00FF..0x0FFF");
	CHECK(fxcg_dcs12_sanitize_saved(0u, 0u) == 0x0500u &&
		fxcg_dcs12_sanitize_saved(0x06u, 0x54u) == 0x0654u, "DCS12 sanitize");

	/* 8-bit vs 12-bit runtime classification. */
	CHECK(fxcg_dcs_classify_initial(0x94u, 0x60u, 0x00u) == 1,
		"HX8394-F upper bits classify as 8-bit");
	CHECK(fxcg_dcs_classify_initial(0x99u, 0x06u, 0x40u) == 2,
		"HX8399-C ID2=0x99 classifies as 12-bit");
	CHECK(fxcg_dcs_classify_initial(0x00u, 0x08u, 0x40u) == 0,
		"ambiguous custom OTP with b0<=0x0F requests probe");
	CHECK(fxcg_dcs_classify_probe(0x7fu, 0x7fu) == 1,
		"probe echoing full byte classifies as 8-bit");
	CHECK(fxcg_dcs_classify_probe(0x7fu, 0x0fu) == 2,
		"probe latching low nibble only classifies as 12-bit");
	CHECK(fxcg_dcs_classify_probe(0x7fu, 0x00u) == 1,
		"unreadable probe safely defaults to 8-bit");
}

static void test_overlays(void)
{
	static uint16_t fb[240 * 160];
	const uint16_t sentinel = 0x1234u;
	uint32_t sp;
	int x, y;

	for (sp = 0u; sp < FXCG100_FF_SPEED_COUNT; sp++) {
		int white_px = 0, black_px = 0;

		for (x = 0; x < 240 * 160; x++)
			fb[x] = sentinel;

		fxcg100_lcd_overlay_fps(fb, 120u, 60u);
		fxcg100_lcd_overlay_fast_forward(fb, sp);

		/* Top-right region (x >= 188, y < 11) must contain badge ink and box. */
		for (y = 0; y < 11; y++) {
			for (x = 188; x < 240; x++) {
				uint16_t px = fb[y * 240 + x];
				if (px == 0xFFFFu)
					white_px++;
				else if (px == 0x0000u)
					black_px++;
			}
		}
		CHECK(white_px > 20 && black_px > 20,
			"FF badge for speed %u missing text/box (w=%d b=%d)",
			(unsigned)sp, white_px, black_px);

		/* Gap between top-left FPS badge (x < 128) and top-right FF badge
		 * (x >= 188) must stay untouched, proving zero collision. */
		for (y = 0; y < 17; y++) {
			for (x = 128; x < 188; x++) {
				CHECK(fb[y * 240 + x] == sentinel,
					"overlap/spill at (%d,%d) for speed %u",
					x, y, (unsigned)sp);
			}
		}

		/* Rows below both badges must stay untouched. */
		for (y = 17; y < 160; y++)
			for (x = 0; x < 240; x++)
				CHECK(fb[y * 240 + x] == sentinel,
					"spill below overlays at (%d,%d)", x, y);
	}
}

int main(void)
{
	test_backlight_tables();
	test_overlays();

	if (failures)
		return 1;
	printf("fxcg_backlight_overlay_test: all checks passed\n");
	return 0;
}
