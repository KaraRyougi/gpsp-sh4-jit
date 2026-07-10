#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the production templates so the test exercises the exact opt-in
 * base-layer renderer.  Enable the existing blend cache as well to verify the
 * two derived-palette consumers cannot clear each other's invalidation. */
#define CGBA_VIDEO_BLEND_PALETTE_CACHE 1
#define CGBA_VIDEO_BACKDROP_SHADOW_PALETTE 1
#define CGBA_VIDEO_OPAQUE_ROW_UNROLL 1
#include "../vendor/gpsp/video.cc"

u16 palette_ram_converted[512];
u16 io_registers[512];
volatile u32 palette_ram_dirty;

static uint32_t rng_state = 0x91e10da5u;
static unsigned long pixel_cases;

static uint32_t rng32(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static int check(bool condition, const char *message)
{
  if (!condition)
    fprintf(stderr, "video backdrop-shadow test failed: %s\n", message);
  return condition ? 1 : 0;
}

static int check_shadow_contents(const u16 *shadow)
{
  for (unsigned i = 0; i < 256; i++) {
    u16 want = (i & 15) ? palette_ram_converted[i]
                        : palette_ram_converted[0];
    if (shadow[i] != want) {
      fprintf(stderr, "shadow[%u]=%04x want=%04x\n", i, shadow[i], want);
      return 0;
    }
  }
  return 1;
}

static int test_invalidation(void)
{
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] =
      (u16)((i * 313u) ^ (i << 7) ^ 0x5a5au);

  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  blend_palette_cache_observe_writes();
  if (!check((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) == 0,
        "blend consumer did not clear its bit") ||
      !check((palette_ram_dirty & CGBA_PALETTE_DIRTY_BACKDROP) != 0,
        "blend consumer cleared backdrop invalidation") ||
      !check_shadow_contents(get_backdrop_shadow_palette()) ||
      !check(palette_ram_dirty == 0,
        "backdrop consumer did not clear its bit"))
    return 0;

  /* Without a producer notification, the cached table must remain stable. */
  u16 cached = backdrop_shadow_palette[17];
  palette_ram_converted[17] ^= 0xffffu;
  if (!check(get_backdrop_shadow_palette()[17] == cached,
        "clean lookup unexpectedly rebuilt the shadow"))
    return 0;

  /* Observe the consumers in the opposite order after changing both the
   * backdrop and a regular color. */
  palette_ram_converted[0] ^= 0x4210u;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  if (!check_shadow_contents(get_backdrop_shadow_palette()) ||
      !check((palette_ram_dirty & CGBA_PALETTE_DIRTY_BACKDROP) == 0,
        "backdrop consumer did not clear its bit after rebuild") ||
      !check((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) != 0,
        "backdrop consumer cleared blend invalidation"))
    return 0;
  blend_palette_cache_observe_writes();
  return check(palette_ram_dirty == 0,
    "blend consumer left a dirty bit after opposite-order observation");
}

static void render_reference(uint32_t row, unsigned start, unsigned end,
  bool hflip, unsigned palbank, u16 *dst)
{
  for (unsigned i = start; i < end; i++) {
    unsigned shift = (hflip ? 7 - i : i) * 4;
    unsigned pval = (row >> shift) & 15;
    dst[i - start] = pval
      ? palette_ram_converted[(palbank << 4) | pval]
      : palette_ram_converted[0];
  }
}

template<bool hflip>
static void render_full_production(uint32_t row, unsigned palbank, u16 *dst)
{
  alignas(4) u8 tile_data[32] = {0};
  uint32_t stored = eswap32(row);
  memcpy(tile_data, &stored, sizeof(stored));
  render_tile_Nbpp<u16, FULLCOLOR, false, true, hflip>(0, 0, dst,
    (u16)(palbank << 12), tile_data, 0, get_backdrop_shadow_palette());
}

template<bool hflip>
static void render_partial_production(uint32_t row, unsigned start,
  unsigned end, unsigned palbank, u16 *dst)
{
  alignas(4) u8 tile_data[32] = {0};
  uint32_t stored = eswap32(row);
  memcpy(tile_data, &stored, sizeof(stored));
  rend_part_tile_Nbpp<u16, FULLCOLOR, false, true, hflip>(0, 0, dst,
    start, end, (u16)(palbank << 12), tile_data, 0,
    get_backdrop_shadow_palette());
}

static int check_render(uint32_t row, unsigned start, unsigned end,
  bool hflip, unsigned palbank, bool partial)
{
  u16 got[8], want[8];
  memset(got, 0xa5, sizeof(got));
  memset(want, 0xa5, sizeof(want));
  render_reference(row, start, end, hflip, palbank, want);
  if (partial) {
    if (hflip)
      render_partial_production<true>(row, start, end, palbank, got);
    else
      render_partial_production<false>(row, start, end, palbank, got);
  } else {
    if (hflip)
      render_full_production<true>(row, palbank, got);
    else
      render_full_production<false>(row, palbank, got);
  }

  pixel_cases += end - start;
  if (memcmp(got, want, (end - start) * sizeof(*got)) != 0) {
    fprintf(stderr,
      "render mismatch row=%08x start=%u end=%u flip=%u pal=%u partial=%u\n",
      row, start, end, hflip, palbank, partial);
    return 0;
  }
  return 1;
}

static int test_rendering(void)
{
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (u16)rng32();
  /* Make every sub-palette color zero visibly different from the backdrop. */
  palette_ram_converted[0] = 0x1234;
  for (unsigned i = 16; i < 256; i += 16)
    palette_ram_converted[i] = (u16)(0xf000u | i);
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;

  static const uint32_t rows[] = {
    0x00000000u, 0xffffffffu, 0x12345678u, 0x87654321u,
    0xf0f0f0f0u, 0x0f0f0f0fu, 0x10101010u, 0x01020304u,
  };
  for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); r++)
    for (unsigned palbank = 0; palbank < 16; palbank++)
      for (unsigned flip = 0; flip < 2; flip++) {
        if (!check_render(rows[r], 0, 8, flip != 0, palbank, false))
          return 0;
        for (unsigned start = 0; start < 8; start++)
          for (unsigned end = start + 1; end <= 8; end++)
            if (!check_render(rows[r], start, end, flip != 0, palbank, true))
              return 0;
      }

  /* Dense zero-position coverage through the full-tile production template. */
  for (uint32_t x = 0; x <= 0xffffu; x++) {
    uint32_t row = x | (x << 16);
    if (!check_render(row, 0, 8, false, (x >> 4) & 15, false) ||
        !check_render(row, 0, 8, true, x & 15, false))
      return 0;
  }

  /* Random partial spans stress both flips and every palette bank. */
  for (unsigned i = 0; i < 100000; i++) {
    uint32_t row = rng32();
    unsigned start = rng32() & 7;
    unsigned end = start + 1 + (rng32() % (8 - start));
    if (!check_render(row, start, end, (rng32() & 1) != 0,
        rng32() & 15, true))
      return 0;
  }
  return 1;
}

int main(void)
{
  if (!test_invalidation() || !test_rendering())
    return 1;
  printf("video backdrop-shadow: %lu pixel comparisons passed\n", pixel_cases);
  return 0;
}
