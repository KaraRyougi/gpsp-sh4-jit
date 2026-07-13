#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the production template in this translation unit so the test calls
 * the exact row path plus the broader opt-in tile classifier. --gc-sections
 * discards the rest of video.cc. */
#define CGBA_VIDEO_OPAQUE_TILE_FASTPATH 1
#define CGBA_VIDEO_OPAQUE_ROW_UNROLL 1
#include "../vendor/gpsp/video.cc"

static uint32_t rng_state = 0x6d2b79f5u;
static unsigned long test_cases;

static uint32_t rng32(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static bool opaque_reference(uint32_t row)
{
  for (unsigned i = 0; i < 8; i++)
    if (((row >> (i * 4)) & 0xfu) == 0)
      return false;
  return true;
}

static void render_reference(uint32_t row, rendtype mode, bool isbase,
  bool hflip, uint32_t bg_comb, uint32_t px_comb, unsigned palbank,
  uint32_t *dst, const uint16_t *pal)
{
  uint32_t pxflg = px_comb | (palbank << 4);

  for (unsigned i = 0; i < 8; i++) {
    unsigned shift = (hflip ? 7 - i : i) * 4;
    uint32_t pval = (row >> shift) & 0xfu;

    if (pval) {
      if (mode == FULLCOLOR)
        dst[i] = pal[(palbank << 4) | pval];
      else if (mode == INDXCOLOR)
        dst[i] = pxflg | pval;
      else if (mode == STCKCOLOR)
        dst[i] = pxflg | pval |
          ((isbase ? bg_comb : dst[i]) << 16);
    } else if (isbase) {
      dst[i] = mode == FULLCOLOR ? pal[0] : bg_comb;
    }
  }
}

template<rendtype mode, bool isbase, bool hflip>
static void render_production_t(uint32_t row, uint32_t bg_comb,
  uint32_t px_comb, unsigned palbank, uint32_t *dst, const uint16_t *pal)
{
  alignas(4) uint8_t tile_data[32] = {0};
  uint32_t stored = eswap32(row);
  memcpy(tile_data, &stored, sizeof(stored));
  render_tile_Nbpp<u32, mode, false, isbase, hflip>(bg_comb, px_comb,
    dst, (uint16_t)(palbank << 12), tile_data, 0, pal);
}

#define DISPATCH_BASE_FLIP(mode_)                                             \
  do {                                                                        \
    if (isbase) {                                                             \
      if (hflip) render_production_t<mode_, true, true>(row, bg_comb,         \
        px_comb, palbank, dst, pal);                                          \
      else render_production_t<mode_, true, false>(row, bg_comb,              \
        px_comb, palbank, dst, pal);                                          \
    } else {                                                                  \
      if (hflip) render_production_t<mode_, false, true>(row, bg_comb,        \
        px_comb, palbank, dst, pal);                                          \
      else render_production_t<mode_, false, false>(row, bg_comb,             \
        px_comb, palbank, dst, pal);                                          \
    }                                                                         \
  } while (0)

static void render_production(uint32_t row, rendtype mode, bool isbase,
  bool hflip, uint32_t bg_comb, uint32_t px_comb, unsigned palbank,
  uint32_t *dst, const uint16_t *pal)
{
  switch (mode) {
    case FULLCOLOR: DISPATCH_BASE_FLIP(FULLCOLOR); break;
    case INDXCOLOR: DISPATCH_BASE_FLIP(INDXCOLOR); break;
    case STCKCOLOR: DISPATCH_BASE_FLIP(STCKCOLOR); break;
    default: break;
  }
}

#undef DISPATCH_BASE_FLIP

template<bool isbase, bool hflip>
static void render_production_fullcolor_u16(uint32_t row, unsigned palbank,
  uint16_t *dst, const uint16_t *pal)
{
  alignas(4) uint8_t tile_data[32] = {0};
  uint32_t stored = eswap32(row);
  memcpy(tile_data, &stored, sizeof(stored));
  render_tile_Nbpp<u16, FULLCOLOR, false, isbase, hflip>(0, 0, dst,
    (uint16_t)(palbank << 12), tile_data, 0, pal);
}

static int check_fullcolor_u16(uint32_t row, bool isbase, bool hflip,
  unsigned palbank, const uint16_t *pal)
{
  uint16_t got[8], want[8];

  for (unsigned i = 0; i < 8; i++)
    got[i] = want[i] = (uint16_t)rng32();
  for (unsigned i = 0; i < 8; i++) {
    unsigned shift = (hflip ? 7 - i : i) * 4;
    unsigned pval = (row >> shift) & 0xfu;
    if (pval)
      want[i] = pal[(palbank << 4) | pval];
    else if (isbase)
      want[i] = pal[0];
  }

  if (isbase) {
    if (hflip)
      render_production_fullcolor_u16<true, true>(row, palbank, got, pal);
    else
      render_production_fullcolor_u16<true, false>(row, palbank, got, pal);
  } else {
    if (hflip)
      render_production_fullcolor_u16<false, true>(row, palbank, got, pal);
    else
      render_production_fullcolor_u16<false, false>(row, palbank, got, pal);
  }
  test_cases++;

  if (memcmp(got, want, sizeof(got)) != 0) {
    fprintf(stderr,
      "u16 fullcolor mismatch row=%08x base=%u flip=%u pal=%u\n",
      row, isbase, hflip, palbank);
    return 0;
  }
  return 1;
}

static int check_case(uint32_t row, rendtype mode, bool isbase, bool hflip,
  uint32_t bg_comb, uint32_t px_comb, unsigned palbank, const uint16_t *pal)
{
  uint32_t got[8], want[8];

  for (unsigned i = 0; i < 8; i++)
    got[i] = want[i] = rng32();

  render_reference(row, mode, isbase, hflip, bg_comb, px_comb, palbank,
    want, pal);
  render_production(row, mode, isbase, hflip, bg_comb, px_comb, palbank,
    got, pal);
  test_cases++;

  if (memcmp(got, want, sizeof(got)) != 0) {
    fprintf(stderr,
      "mismatch row=%08x mode=%u base=%u flip=%u bg=%x px=%x pal=%u\n",
      row, (unsigned)mode, isbase, hflip, bg_comb, px_comb, palbank);
    for (unsigned i = 0; i < 8; i++)
      if (got[i] != want[i])
        fprintf(stderr, "  pixel %u got=%08x want=%08x\n", i, got[i],
          want[i]);
    return 0;
  }
  return 1;
}

int main(void)
{
  uint16_t pal[256];
  static const uint32_t rows[] = {
    0x00000000u, 0x11111111u, 0xffffffffu, 0x12345678u,
    0x87654321u, 0xf0f0f0f0u, 0x0f0f0f0fu, 0x10101010u,
  };
  static const uint32_t flags[] = {0x000u, 0x200u, 0x400u, 0x600u};

  for (unsigned i = 0; i < 256; i++)
    pal[i] = (uint16_t)((i * 257u) ^ (i << 7) ^ 0x5a5au);

  /* Exhaust the predicate over every 16-bit nibble pattern, repeated into
   * both halves, then stress unrelated high/low halves deterministically. */
  for (uint32_t x = 0; x <= 0xffffu; x++) {
    uint32_t row = x | (x << 16);
    if (cgba_4bpp_row_is_opaque(row) != opaque_reference(row)) {
      fprintf(stderr, "opaque predicate mismatch row=%08x\n", row);
      return 1;
    }
  }
  for (unsigned i = 0; i < 200000; i++) {
    uint32_t row = rng32();
    if (cgba_4bpp_row_is_opaque(row) != opaque_reference(row)) {
      fprintf(stderr, "opaque predicate mismatch row=%08x\n", row);
      return 1;
    }
  }

  /* Exhaust all four-nibble patterns, repeated into a full row, through the
   * production u32 renderer.  This covers the new unrolled path densely while
   * retaining zeros in every position for fallback/base-layer semantics. */
  for (uint32_t x = 0; x <= 0xffffu; x++) {
    uint32_t row = x | (x << 16);
    for (unsigned mode = INDXCOLOR; mode <= STCKCOLOR; mode++)
      for (unsigned isbase = 0; isbase < 2; isbase++)
        for (unsigned hflip = 0; hflip < 2; hflip++)
          if (!check_case(row, (rendtype)mode, isbase != 0, hflip != 0,
              flags[(x >> 2) & 3], flags[x & 3], (x >> 4) & 15, pal))
            return 1;
    for (unsigned isbase = 0; isbase < 2; isbase++)
      for (unsigned hflip = 0; hflip < 2; hflip++)
        if (!check_fullcolor_u16(row, isbase != 0, hflip != 0,
            (x >> 4) & 15, pal))
          return 1;
  }

  /* Structured coverage of transparency position, metadata, palette bank,
   * output mode, base-layer behavior, and horizontal order. */
  for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); r++)
    for (unsigned zero_pos = 0; zero_pos <= 8; zero_pos++) {
      uint32_t row = rows[r];
      if (zero_pos < 8)
        row &= ~(0xfu << (zero_pos * 4));
      for (unsigned mode = FULLCOLOR; mode <= STCKCOLOR; mode++)
        for (unsigned isbase = 0; isbase < 2; isbase++)
          for (unsigned hflip = 0; hflip < 2; hflip++)
            for (unsigned palbank = 0; palbank < 16; palbank++)
              for (unsigned bg = 0; bg < 4; bg++)
                for (unsigned px = 0; px < 4; px++)
                  if (!check_case(row, (rendtype)mode, isbase != 0,
                      hflip != 0, flags[bg], flags[px], palbank, pal))
                    return 1;
    }

  /* Random differential coverage exercises both the opaque fast path and the
   * unchanged transparent fallback with arbitrary old destination words. */
  for (unsigned i = 0; i < 200000; i++) {
    uint32_t row = rng32();
    rendtype mode = (rendtype)(rng32() % 3);
    bool isbase = (rng32() & 1) != 0;
    bool hflip = (rng32() & 1) != 0;
    unsigned palbank = rng32() & 15;
    uint32_t bg_comb = flags[rng32() & 3];
    uint32_t px_comb = flags[rng32() & 3];
    if (!check_case(row, mode, isbase, hflip, bg_comb, px_comb, palbank,
        pal))
      return 1;
    if (!check_fullcolor_u16(row, isbase, hflip, palbank, pal))
      return 1;
  }

  printf("video opaque-tile differential: %lu cases passed\n", test_cases);
  return 0;
}
