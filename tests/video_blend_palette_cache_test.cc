#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the production renderer in this translation unit so the test calls
 * the exact opt-in cache and blend paths.  --gc-sections discards unrelated
 * video.cc code and its unresolved dependencies. */
#define CGBA_VIDEO_BLEND_PALETTE_CACHE 1
#include "../vendor/gpsp/video.cc"

/* Production owns these in cpu.cc/gba_memory.c.  The focused host test only
 * needs the converted palette, blend registers, and dirty notification. */
u16 palette_ram_converted[512];
u16 io_registers[512];
volatile u32 palette_ram_dirty;

static uint32_t rng_state = 0x7f4a7c15u;
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
    fprintf(stderr, "video blend-cache test failed: %s\n", message);
  return condition ? 1 : 0;
}

static void reset_caches(void)
{
  memset(&blend_palette_cache_a, 0, sizeof(blend_palette_cache_a));
  memset(&blend_palette_cache_b, 0, sizeof(blend_palette_cache_b));
  palette_ram_dirty = 1;
}

static void set_factors(unsigned factor_a, unsigned factor_b)
{
  write_ioreg(REG_BLDALPHA, (factor_b << 8) | factor_a);
  write_ioreg(REG_BLDY, 0);
}

static uint32_t make_blend_pair(unsigned index_a, unsigned index_b)
{
  /* Low pixel: palette index + first-target flag.  High pixel: palette index
   * + second-target flag. */
  return (index_a & 0x1ffu) | 0x200u |
    (((index_b & 0x1ffu) | 0x400u) << 16);
}

static uint16_t blend_reference(uint16_t pixel_a, uint16_t pixel_b,
  unsigned factor_a, unsigned factor_b)
{
  uint32_t expanded_a =
    (pixel_a | ((uint32_t)pixel_a << 16)) & BLND_MSK;
  uint32_t expanded_b =
    (pixel_b | ((uint32_t)pixel_b << 16)) & BLND_MSK;
  uint32_t blended =
    ((expanded_a * factor_a) + (expanded_b * factor_b)) >> 4;

  if (factor_a + factor_b > 16 &&
      (blended & (OVFR_MSK | OVFG_MSK | OVFB_MSK))) {
    if (blended & OVFG_MSK)
      blended |= SATG_MSK;
    if (blended & OVFR_MSK)
      blended |= SATR_MSK;
    if (blended & OVFB_MSK)
      blended |= SATB_MSK;
  }
  blended &= BLND_MSK;
  return (uint16_t)((blended >> 16) | blended);
}

static int check_output(const uint32_t *src, const uint16_t *got,
  unsigned count, unsigned factor_a, unsigned factor_b, const char *phase)
{
  for (unsigned i = 0; i < count; i++) {
    unsigned index_a = src[i] & 0x1ffu;
    unsigned index_b = (src[i] >> 16) & 0x1ffu;
    uint16_t want = blend_reference(palette_ram_converted[index_a],
      palette_ram_converted[index_b], factor_a, factor_b);
    pixel_cases++;
    if (got[i] != want) {
      fprintf(stderr,
        "%s mismatch case=%u ia=%u ib=%u fa=%u fb=%u got=%04x want=%04x\n",
        phase, i, index_a, index_b, factor_a, factor_b, got[i], want);
      return 0;
    }
  }
  return 1;
}

static int test_state_machine(void)
{
  uint32_t src[4] = {
    make_blend_pair(0, 511), make_blend_pair(1, 510),
    make_blend_pair(31, 63), make_blend_pair(257, 129),
  };
  uint16_t dst[4];

  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] =
      (uint16_t)(((i * 109u) ^ (i << 9) ^ 0x5aa5u) & 0xffffu);

  reset_caches();
  set_factors(6, 10);

  /* A dirty notification invalidates both tables.  The first observation of
   * each factor renders directly and only records a pending rebuild. */
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(palette_ram_dirty == 0, "dirty flag was not consumed") ||
      !check(!blend_palette_cache_a.valid && blend_palette_cache_a.pending,
        "cache A did not delay its first rebuild") ||
      !check(!blend_palette_cache_b.valid && blend_palette_cache_b.pending,
        "cache B was not evaluated after cache A missed") ||
      !check(blend_palette_cache_a.pending_factor == 6,
        "cache A pending factor is wrong") ||
      !check(blend_palette_cache_b.pending_factor == 10,
        "cache B pending factor is wrong") ||
      !check_output(src, dst, 4, 6, 10, "initial direct"))
    return 0;

  /* The second identical observation pays for both tables and uses them for
   * the same render call. */
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(blend_palette_cache_a.valid &&
          blend_palette_cache_a.factor == 6 &&
          !blend_palette_cache_a.pending,
        "cache A did not rebuild on its second observation") ||
      !check(blend_palette_cache_b.valid &&
          blend_palette_cache_b.factor == 10 &&
          !blend_palette_cache_b.pending,
        "cache B did not rebuild on its second observation") ||
      !check_output(src, dst, 4, 6, 10, "initial cached"))
    return 0;

  /* A stable-factor hit must return the existing table without rebuilding.
   * A test-only sentinel makes that distinction observable. */
  const u32 *term = NULL;
  u32 saved_a = blend_palette_cache_a.term[511];
  u32 saved_b = blend_palette_cache_b.term[511];
  blend_palette_cache_a.term[511] ^= 0x01010101u;
  blend_palette_cache_b.term[511] ^= 0x10101010u;
  u32 sentinel_a = blend_palette_cache_a.term[511];
  u32 sentinel_b = blend_palette_cache_b.term[511];
  if (!check(blend_palette_cache_get(&blend_palette_cache_a, 6, &term) &&
          term == blend_palette_cache_a.term &&
          blend_palette_cache_a.term[511] == sentinel_a,
        "stable cache A hit rebuilt its table") ||
      !check(blend_palette_cache_get(&blend_palette_cache_b, 10, &term) &&
          term == blend_palette_cache_b.term &&
          blend_palette_cache_b.term[511] == sentinel_b,
        "stable cache B hit rebuilt its table"))
    return 0;
  blend_palette_cache_a.term[511] = saved_a;
  blend_palette_cache_b.term[511] = saved_b;

  /* A factor change gets the same one-call grace period.  Both A and B must be
   * observed even though A's miss is enough to force direct rendering. */
  set_factors(7, 12);
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(blend_palette_cache_a.valid && blend_palette_cache_a.pending &&
          blend_palette_cache_a.factor == 6 &&
          blend_palette_cache_a.pending_factor == 7,
        "cache A factor change did not enter pending state") ||
      !check(blend_palette_cache_b.valid && blend_palette_cache_b.pending &&
          blend_palette_cache_b.factor == 10 &&
          blend_palette_cache_b.pending_factor == 12,
        "cache B factor change was not independently evaluated") ||
      !check_output(src, dst, 4, 7, 12, "factor-change direct"))
    return 0;

  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(blend_palette_cache_a.valid &&
          blend_palette_cache_a.factor == 7 &&
          !blend_palette_cache_a.pending,
        "cache A factor change did not rebuild") ||
      !check(blend_palette_cache_b.valid &&
          blend_palette_cache_b.factor == 12 &&
          !blend_palette_cache_b.pending,
        "cache B factor change did not rebuild") ||
      !check_output(src, dst, 4, 7, 12, "factor-change cached"))
    return 0;

  /* Model a regular palette write: the producer updates converted RAM and
   * raises the shared dirty bit.  The next render must not use stale terms. */
  palette_ram_converted[31] ^= 0xffffu;
  palette_ram_dirty = 1;
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(!blend_palette_cache_a.valid && blend_palette_cache_a.pending,
        "palette write did not invalidate cache A") ||
      !check(!blend_palette_cache_b.valid && blend_palette_cache_b.pending,
        "palette write did not invalidate cache B") ||
      !check_output(src, dst, 4, 7, 12, "palette-dirty direct"))
    return 0;
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(blend_palette_cache_a.valid && blend_palette_cache_b.valid,
        "palette-write tables did not rebuild") ||
      !check_output(src, dst, 4, 7, 12, "palette-dirty cached"))
    return 0;

  /* Model savestate restore, which regenerates every converted entry and then
   * raises the same dirty notification. */
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (uint16_t)(0xffffu - i * 73u);
  palette_ram_dirty = 1;
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  if (!check(!blend_palette_cache_a.valid && blend_palette_cache_a.pending &&
          !blend_palette_cache_b.valid && blend_palette_cache_b.pending,
        "savestate-style restore did not invalidate both caches") ||
      !check_output(src, dst, 4, 7, 12, "savestate direct"))
    return 0;
  merge_blend<BLEND_ONLY, false>(0, 4, dst, src);
  return check(blend_palette_cache_a.valid && blend_palette_cache_b.valid,
      "savestate-style tables did not rebuild") &&
    check_output(src, dst, 4, 7, 12, "savestate cached");
}

static int test_randomized_output(void)
{
  enum { kCases = 4096 };
  static uint32_t src[kCases];
  static uint16_t direct[kCases];
  static uint16_t cached[kCases];
  static const unsigned factors[][2] = {
    {0, 0}, {3, 5}, {8, 8}, {16, 0},
    {9, 8}, {13, 7}, {16, 16},
  };

  for (unsigned f = 0; f < sizeof(factors) / sizeof(factors[0]); f++) {
    unsigned factor_a = factors[f][0];
    unsigned factor_b = factors[f][1];

    /* Include exact channel limits as well as arbitrary RGB565 bit patterns. */
    palette_ram_converted[0] = 0x0000;
    palette_ram_converted[1] = 0xffff;
    palette_ram_converted[2] = 0xf800;
    palette_ram_converted[3] = 0x07e0;
    palette_ram_converted[4] = 0x001f;
    for (unsigned i = 5; i < 512; i++)
      palette_ram_converted[i] = (uint16_t)rng32();
    for (unsigned i = 0; i < kCases; i++)
      src[i] = make_blend_pair(rng32() & 0x1ffu, rng32() & 0x1ffu);
    /* Guarantee component-limit and overflow cases in addition to the
     * randomized indices.  In particular, white/white with the >16 factor
     * pairs must exercise all three saturation masks. */
    src[0] = make_blend_pair(0, 0);
    src[1] = make_blend_pair(1, 1);
    src[2] = make_blend_pair(2, 2);
    src[3] = make_blend_pair(3, 3);
    src[4] = make_blend_pair(4, 4);
    src[5] = make_blend_pair(0, 1);
    src[6] = make_blend_pair(1, 0);

    reset_caches();
    set_factors(factor_a, factor_b);

    /* First observation is the production direct path. */
    merge_blend<BLEND_ONLY, false>(0, kCases, direct, src);
    if (!check(!blend_palette_cache_a.valid &&
            !blend_palette_cache_b.valid,
          "random direct pass rebuilt too early") ||
        !check_output(src, direct, kCases, factor_a, factor_b,
          "random direct"))
      return 0;

    /* Second observation builds both tables and executes the cached path. */
    merge_blend<BLEND_ONLY, false>(0, kCases, cached, src);
    if (!check(blend_palette_cache_a.valid &&
            blend_palette_cache_b.valid,
          "random cached pass did not build both tables") ||
        !check_output(src, cached, kCases, factor_a, factor_b,
          "random cached") ||
        !check(memcmp(direct, cached, sizeof(direct)) == 0,
          "cached RGB565 output differs from production direct output"))
      return 0;
  }

  return 1;
}

int main(void)
{
  if (!test_state_machine() || !test_randomized_output())
    return 1;

  printf("video blend palette-cache: %lu pixel comparisons passed\n",
    pixel_cases);
  return 0;
}
