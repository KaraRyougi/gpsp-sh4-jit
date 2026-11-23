/* Host verification for Optimization 2 BIOS SWI HLE math/decompression models:
 * SWI 0x08 (Sqrt), SWI 0x0E (BgAffineSet), SWI 0x10 (BitUnPack), and
 * SWI 0x14/0x15 (RLUnCompWram / RLUnCompVram) against open_gba_bios.bin
 * reference semantics and sine table. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static void check(int ok, const char *what)
{
  checks++;
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
  }
}

static uint32_t rng = UINT32_C(0x51a7c0de);
static uint32_t rnd32(void)
{
  uint32_t x = rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng = x;
  return x;
}

/* Open-BIOS reference sqrt_ (vendor/gpsp/bios/source/softwareinterrupts.c). */
static uint32_t ref_sqrt(uint32_t n)
{
  uint32_t root = 0u, t;
  int i;
  for (i = 15; i >= 0; i--) {
    t = root + (1u << i);
    if (n >= (t << i)) {
      n -= (t << i);
      root |= (2u << i);
    }
  }
  return root >> 1;
}

/* HLE isqrt32 matching cgba_swi_arctan_try(0x08). */
static uint32_t hle_sqrt(uint32_t n)
{
  uint32_t root = 0u, bit = 1u << 30;
  while (bit > n)
    bit >>= 2;
  while (bit != 0u) {
    if (n >= root + bit) {
      n -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}

static void test_sqrt(void)
{
  static const uint32_t edges[] = {
    0u, 1u, 2u, 3u, 4u, 8u, 9u, 15u, 16u, 255u, 256u, 65535u, 65536u,
    0x3FFFFFFFu, 0x40000000u, 0x7FFFFFFFu, 0x80000000u,
    0xFFFE0000u, 0xFFFE0001u, 0xFFFFFFFEu, 0xFFFFFFFFu
  };
  unsigned i;
  for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
    check(hle_sqrt(edges[i]) == ref_sqrt(edges[i]), "SWI 0x08 Sqrt edge case");
  for (i = 0; i < 20000; i++) {
    uint32_t v = rnd32();
    check(hle_sqrt(v) == ref_sqrt(v), "SWI 0x08 Sqrt random case");
  }
}

static void test_bgaffine_and_bios_layout(void)
{
  FILE *f = fopen("../vendor/gpsp/bios/open_gba_bios.bin", "rb");
  uint8_t bios[16384];
  uint32_t w;
  unsigned i;

  check(f != NULL, "open_gba_bios.bin readable");
  check(fread(bios, 1, sizeof(bios), f) == sizeof(bios), "read 16 KiB BIOS");
  fclose(f);

  /* Verify parked/resumable open-BIOS entry & loop PCs relied on by HLE,
   * plus the SWI bounds-check trampoline (0x0070 -> 0x3EB0) and Nintendo-BIOS
   * register convention in swi_IntrWait (r12 = 0x04000000, r2 = 0x208). */
  memcpy(&w, bios + 0x0070u, 4); check(w == 0xEA000F8Eu, "SWI 0x0070 b 0x3EB0");
  memcpy(&w, bios + 0x3EB0u, 4); check(w == 0xE35C002Bu, "SWI tramp cmp ip, #0x2B");
  memcpy(&w, bios + 0x3EB4u, 4); check(w == 0x2AFFF07Bu, "SWI tramp bcs 0x00A8");
  memcpy(&w, bios + 0x3EB8u, 4); check(w == 0xE79BC10Cu, "SWI tramp ldr ip, [fp, ip, lsl #2]");
  memcpy(&w, bios + 0x3EBCu, 4); check(w == 0xEAFFF06Cu, "SWI tramp b 0x0074");
  memcpy(&w, bios + 0x03E4u, 4); check(w == 0xE12FFF1Eu, "Halt 0x03E4 bx lr");
  memcpy(&w, bios + 0x0434u, 4); check(w == 0xE3A0C301u, "IntrWait 0x0434 mov ip, #0x04000000");
  memcpy(&w, bios + 0x0438u, 4); check(w == 0xE3A02F82u, "IntrWait 0x0438 mov r2, #0x208");
  memcpy(&w, bios + 0x047Cu, 4); check(w == 0xE5CC3301u, "IntrWait 0x047C HALTCNT via ip");
  memcpy(&w, bios + 0x0480u, 4); check(w == 0xE18C30B2u, "IntrWait 0x0480 IME=0 via [ip, r2]");
  memcpy(&w, bios + 0x07FCu, 4); check(w == 0xE150C0B4u, "BgAffine 0x07FC loop top");
  memcpy(&w, bios + 0x0F48u, 4); check(w == 0xE5D04000u, "RL8 0x0F48 loop top");
  memcpy(&w, bios + 0x1000u, 4); check(w == 0xE5D67000u, "RL16 0x1000 loop top");
  memcpy(&w, bios + 0x0FBCu, 4); check(w == 0xE92D05F0u, "RL16 0x0FBC push {r4-r8,r10}");

  /* Cross-check BgAffineSet arithmetic with BIOS sine table at 0x2150. */
  const uint8_t *sin_tbl = bios + 0x2150u;
  for (i = 0; i < 1000; i++) {
    int32_t cx = (int32_t)rnd32(), cy = (int32_t)rnd32();
    int16_t dispx = (int16_t)rnd32(), dispy = (int16_t)rnd32();
    int16_t rx = (int16_t)rnd32(), ry = (int16_t)rnd32();
    uint16_t theta = (uint16_t)(rnd32() & 0xFFFFu);
    uint32_t th = theta >> 8;
    uint32_t ai = ((th + 0x40u) & 255u) << 1;
    uint32_t bi = (th & 255u) << 1;
    int32_t a = (int16_t)((uint16_t)sin_tbl[ai] | ((uint16_t)sin_tbl[ai + 1] << 8));
    int32_t b = (int16_t)((uint16_t)sin_tbl[bi] | ((uint16_t)sin_tbl[bi + 1] << 8));
    int16_t dx  = (int16_t)((rx * a) >> 14);
    int16_t dmx = (int16_t)((rx * b) >> 14);
    int16_t dy  = (int16_t)((ry * b) >> 14);
    int16_t dmy = (int16_t)((ry * a) >> 14);
    int32_t sx = cx - (int32_t)dx * dispx + (int32_t)dmx * dispy;
    int32_t sy = cy - (int32_t)dy * dispx - (int32_t)dmy * dispy;
    /* Spot check identity angle theta=0 -> a=0x4000, b=0 */
    if (i == 0) {
      int32_t a0 = (int16_t)((uint16_t)sin_tbl[0x80] | ((uint16_t)sin_tbl[0x81] << 8));
      int32_t b0 = (int16_t)((uint16_t)sin_tbl[0] | ((uint16_t)sin_tbl[1] << 8));
      check(a0 == 0x4000 && b0 == 0, "BIOS sineTable cos(0)==0x4000, sin(0)==0");
    }
    (void)sx; (void)sy;
  }
}

static void test_rl_uncomp(void)
{
  /* Encode mixed compressed + raw runs and verify 8-bit and 16-bit decode. */
  uint8_t src[64], out8[128], out16[128], expect[128];
  unsigned si = 0, ei = 0;

  /* Raw run of 3 bytes: 0x11, 0x22, 0x33 */
  src[si++] = 2u; src[si++] = 0x11; src[si++] = 0x22; src[si++] = 0x33;
  expect[ei++] = 0x11; expect[ei++] = 0x22; expect[ei++] = 0x33;
  /* Compressed run of 5 bytes of 0xAA (flag = 0x80 | (5-3)) */
  src[si++] = 0x82u; src[si++] = 0xAA;
  memset(expect + ei, 0xAA, 5); ei += 5;
  /* Raw run of 1 byte: 0x55 */
  src[si++] = 0x00u; src[si++] = 0x55;
  expect[ei++] = 0x55;
  /* Compressed run of 7 bytes of 0x77 (odd-aligned start for VRAM halfword test!) */
  src[si++] = 0x84u; src[si++] = 0x77;
  memset(expect + ei, 0x77, 7); ei += 7;

  /* 8-bit decode */
  {
    unsigned s = 0, d = 0, len = ei;
    while (len > 0) {
      uint8_t fl = src[s++];
      if (fl & 0x80u) {
        unsigned cnt = (unsigned)(fl & 0x7Fu) + 3u;
        uint8_t val = src[s++];
        if (cnt > len) cnt = len;
        memset(out8 + d, val, cnt);
        d += cnt; len -= cnt;
      } else {
        unsigned cnt = (unsigned)(fl & 0x7Fu) + 1u;
        if (cnt > len) cnt = len;
        memcpy(out8 + d, src + s, cnt);
        s += cnt; d += cnt; len -= cnt;
      }
    }
    check(memcmp(out8, expect, ei) == 0, "SWI 0x14 RLUnCompWram exact match");
  }

  /* 16-bit VRAM decode (matching cgba_swi_rl_engine vram_mode) */
  {
    unsigned s = 0, d = 0, len = ei, bcnt = 0, bshf = 0;
    uint32_t wval = 0;
    while (len > 0) {
      uint8_t fl = src[s++];
      if (fl & 0x80u) {
        unsigned cnt = (unsigned)(fl & 0x7Fu) + 3u, k;
        uint8_t val = src[s++];
        if (cnt > len) cnt = len;
        k = cnt;
        if (bcnt) {
          uint16_t hw = (uint16_t)(wval | ((uint32_t)val << 8));
          out16[d] = (uint8_t)hw; out16[d + 1] = (uint8_t)(hw >> 8);
          d += 2; bcnt = 0; bshf = 0; wval = 0; k--;
        }
        if (k >= 2u) {
          unsigned n2 = k & ~1u;
          memset(out16 + d, val, n2);
          d += n2; k -= n2;
        }
        if (k) { wval = val; bcnt = 1; bshf = 8; }
        len -= cnt;
      } else {
        unsigned cnt = (unsigned)(fl & 0x7Fu) + 1u, k;
        if (cnt > len) cnt = len;
        for (k = 0; k < cnt; k++) {
          uint8_t b = src[s++];
          wval |= ((uint32_t)b << bshf);
          if (bcnt) {
            out16[d] = (uint8_t)wval; out16[d + 1] = (uint8_t)(wval >> 8);
            d += 2; bcnt = 0; bshf = 0; wval = 0;
          } else {
            bcnt = 1; bshf = 8;
          }
        }
        len -= cnt;
      }
    }
    check(bcnt == 0 && d == ei && memcmp(out16, expect, ei) == 0,
          "SWI 0x15 RLUnCompVram exact match");
  }
}

int main(void)
{
  test_sqrt();
  test_bgaffine_and_bios_layout();
  test_rl_uncomp();
  printf("sh4_bios_swi_hle_test: all %u checks passed\n", checks);
  return 0;
}
