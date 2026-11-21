#ifndef CGBA_FXCG_BACKLIGHT_H
#define CGBA_FXCG_BACKLIGHT_H

#include <stdint.h>

/*
 * Hardware-independent backlight mapping and validation helpers for the
 * fx-CG50 and fx-CG100 (Graph Math+) displays:
 *
 *   1. Traditional Renesas R61524 (fx-CG50 and early fx-CG100):
 *      - PNDR (0xA4050138) bit 4 selects low (0) vs high (1) LED current range
 *      - R61524 register 0x5A1 sets the 8-bit PWM duty (0x00..0xFF)
 *      - R61524 register 0x5A2 sets the PWM divider (Casio OS default 0x0F)
 *      The 16 user stops span smoothly from Casio OS's dim level (low 0x14)
 *      through OS levels 1..5 into full brightness (high 0xFF).
 *
 *   2. 2025-2026 MIPI-DCS displays (RDID1 0xDA == 0x32 or 0x52, Himax
 *      HX8394-F / HX8399-C family):
 *      - Standard MIPI-DCS WRDISBV (0x51) / RDDISBV (0x52) controls brightness
 *        without altering panel-specific PWM polarity or CABC registers (0xC9).
 *      - Supports both 8-bit 1-parameter WRDISBV (HX8394-F / ClassPad 0x52:
 *        DBV[7:0], 15..255) and 12-bit 2-parameter WRDISBV (HX8399-C:
 *        DBV[11:8], DBV[7:0], 0x00FF..0x0FFF).
 *
 * Level 0 always leaves, or restores, the brightness read from the hardware
 * before gpSP first changed it.
 */

#define FXCG_BACKLIGHT_MAX_LEVEL 16u

typedef struct fxcg_r61524_bl {
  uint8_t high_bit; /* PNDR bit 4: 0 = low range, 1 = high range */
  uint8_t duty;     /* R61524 register 0x5A1 */
  uint8_t divider;  /* R61524 register 0x5A2 */
} fxcg_r61524_bl;

/* True for the 2025-2026 MIPI-DCS displays (matching gint's r61524.c). */
static inline int fxcg_panel_is_dcs(uint16_t version)
{
  return version == 50u || version == 82u;
}

/* 16 monotonic R61524 stops aligned with Casio OS's low->high range handoff:
 * Casio OS uses low 0x14..0xD2 (steps dim..3) then high 0x6E..0xC8 (4..max). */
static inline fxcg_r61524_bl fxcg_r61524_level(uint32_t level,
                                               fxcg_r61524_bl system)
{
  static const uint8_t high_bits[FXCG_BACKLIGHT_MAX_LEVEL] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1,
  };
  static const uint8_t duties[FXCG_BACKLIGHT_MAX_LEVEL] = {
    0x14u, 0x28u, 0x40u, 0x58u, 0x78u, 0x9cu, 0xc4u, 0xe8u,
    0x6eu, 0x84u, 0x9au, 0xb0u, 0xc8u, 0xdcu, 0xeeu, 0xffu,
  };
  fxcg_r61524_bl out;

  if (level == 0u || level > FXCG_BACKLIGHT_MAX_LEVEL)
    return system;
  out.high_bit = high_bits[level - 1u];
  out.duty = duties[level - 1u];
  out.divider = 0x0fu;
  return out;
}

static inline int fxcg_r61524_divider_valid(uint8_t div)
{
  static const uint8_t valid[10] = {
    0x00u, 0x01u, 0x02u, 0x03u, 0x07u, 0x0fu, 0x1fu, 0x3fu, 0x7fu, 0xffu,
  };
  unsigned i;

  for (i = 0; i < sizeof(valid); i++)
    if (div == valid[i])
      return 1;
  return 0;
}

/* Guard against unread or bogus register readback so exit always restores a
 * usable screen (Casio OS middle step: low range, duty 0xD2, divider 0x0F). */
static inline fxcg_r61524_bl fxcg_r61524_sanitize_saved(uint8_t high_bit,
                                                        uint16_t raw_duty,
                                                        uint16_t raw_div)
{
  fxcg_r61524_bl out;
  uint8_t duty = (uint8_t)(raw_duty & 0xffu);
  uint8_t div = (uint8_t)(raw_div & 0xffu);

  if (duty < 0x10u || (raw_duty & 0xff00u) != 0u) {
    out.high_bit = 0u;
    out.duty = 0xd2u;
    out.divider = 0x0fu;
    return out;
  }
  out.high_bit = high_bit ? 1u : 0u;
  out.duty = duty;
  out.divider = ((raw_div & 0xff00u) == 0u && fxcg_r61524_divider_valid(div))
    ? div : 0x0fu;
  return out;
}

/* 8-bit DCS WRDISBV (HX8394-F / ClassPad 0x52): level 1..16 -> 15..255. */
static inline uint8_t fxcg_dcs8_level(uint32_t level, uint8_t system)
{
  if (level == 0u || level > FXCG_BACKLIGHT_MAX_LEVEL)
    return system;
  return (uint8_t)(level * 16u - 1u);
}

static inline uint8_t fxcg_dcs8_sanitize_saved(uint8_t raw)
{
  if (raw < 8u)
    return 0x50u;
  return raw;
}

/* 12-bit DCS WRDISBV (HX8399-C): level 1..16 -> 0x00FF..0x0FFF. */
static inline uint16_t fxcg_dcs12_level(uint32_t level, uint16_t system)
{
  if (level == 0u || level > FXCG_BACKLIGHT_MAX_LEVEL)
    return system;
  return (uint16_t)(level * 256u - 1u);
}

static inline uint16_t fxcg_dcs12_sanitize_saved(uint8_t b0, uint8_t b1)
{
  uint16_t val = (uint16_t)(((uint16_t)(b0 & 0x0fu) << 8) | b1);

  if (val < 0x0080u)
    return 0x0500u;
  return val;
}

/* Classify whether a MIPI-DCS panel uses 8-bit (1) or 12-bit (2) WRDISBV from
 * initial RDID2 (0xDB) and RDDISBV (0x52) reads, or 0 if ambiguous (caller
 * should resolve with a 1-byte write+readback probe). */
static inline int fxcg_dcs_classify_initial(uint8_t id2, uint8_t b0, uint8_t b1)
{
  (void)b1;
  if (id2 == 0x99u && (b0 & 0xf0u) == 0u)
    return 2;
  if ((b0 & 0xf0u) != 0u)
    return 1;
  return 0;
}

/* Resolve an ambiguous DCS panel after writing 1 byte `probe` (>= 0x10) to
 * 0x51 and reading back `r0` from 0x52:
 *   - 8-bit panel echoes `probe` verbatim (upper nibble non-zero)
 *   - 12-bit panel latches only DBV[11:8] = probe & 0x0F (upper nibble zero)
 *   - Unreadable bus defaults to 8-bit (matching ClassPad 0x52 / HX8394-F). */
static inline int fxcg_dcs_classify_probe(uint8_t probe, uint8_t r0)
{
  if (r0 == probe)
    return 1;
  if ((r0 & 0xf0u) == 0u && (r0 & 0x0fu) == (probe & 0x0fu))
    return 2;
  return 1;
}

#endif
