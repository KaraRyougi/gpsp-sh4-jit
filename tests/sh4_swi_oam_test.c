#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_swi_oam.h"

static uint32_t rng_state = UINT32_C(0xc001d00d);
static unsigned checks;

static uint32_t rnd32(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static void check(int ok, const char *what)
{
  checks++;
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
  }
}

/* Byte-level oracle for repeated write_memory16(address, value).  The source
 * is already in gpSP's little-endian guest byte layout. */
static void reference_copy(uint8_t oam[0x400], const uint8_t *src,
                           uint32_t dest, uint32_t count,
                           uint32_t *updated)
{
  uint32_t i;
  for (i = 0; i < count; i++) {
    size_t off = (size_t)((dest + i * 2u) & 0x3ffu);
    *updated = 1;
    oam[off] = src[i * 2u];
    oam[off + 1u] = src[i * 2u + 1u];
  }
}

static void fill_random(uint8_t *p, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
    p[i] = (uint8_t)rnd32();
}

static void test_zelda_shape(void)
{
  uint8_t src[0x400], got[0x400], want[0x400];
  uint32_t got_updated = UINT32_C(0xfeedbeef);
  uint32_t want_updated = got_updated;

  fill_random(src, sizeof(src));
  fill_random(got, sizeof(got));
  memcpy(want, got, sizeof(want));
  reference_copy(want, src, UINT32_C(0x07000000), 512, &want_updated);
  check(cgba_sh4_swi_copy_u16_to_oam(got, src, UINT32_C(0x07000000),
                                            512, &got_updated),
        "Zelda 512-halfword IWRAM-to-OAM shape is accepted");
  check(!memcmp(got, want, sizeof(got)), "Zelda shape bytes match");
  check(got_updated == want_updated, "Zelda shape OAM_UPDATED matches");
}

static void test_random_and_parked_chunks(void)
{
  enum { SOURCE_BYTES = 8192 };
  uint8_t src[SOURCE_BYTES];
  uint8_t initial[0x400], want[0x400], one[0x400], chunked[0x400];
  unsigned trial;

  for (trial = 0; trial < 50000; trial++) {
    uint32_t count = 1u + rnd32() % (SOURCE_BYTES / 2u);
    uint32_t bytes = count * 2u;
    uint32_t room = UINT32_C(0x01000000) - bytes;
    uint32_t dest = UINT32_C(0x07000000) + ((rnd32() % room) & ~1u);
    uint32_t want_updated = rnd32();
    uint32_t one_updated = want_updated;
    uint32_t chunked_updated = want_updated;
    uint32_t done = 0;

    fill_random(src, bytes);
    fill_random(initial, sizeof(initial));
    memcpy(want, initial, sizeof(want));
    memcpy(one, initial, sizeof(one));
    memcpy(chunked, initial, sizeof(chunked));

    reference_copy(want, src, dest, count, &want_updated);
    check(cgba_sh4_swi_copy_u16_to_oam(one, src, dest, count,
                                             &one_updated),
          "random valid copy accepted");
    check(!memcmp(one, want, sizeof(one)), "random one-shot bytes match");
    check(one_updated == want_updated, "random one-shot update matches");

    /* Model arbitrary event-slice parking: every call begins at the current
     * source/destination cursor and can end before the whole CpuSet does. */
    while (done < count) {
      uint32_t left = count - done;
      uint32_t k = 1u + rnd32() % left;
      check(cgba_sh4_swi_copy_u16_to_oam(chunked, src + done * 2u,
                                               dest + done * 2u, k,
                                               &chunked_updated),
            "partial parked chunk accepted");
      done += k;
    }
    check(!memcmp(chunked, want, sizeof(chunked)),
          "partial parked chunks match uninterrupted writes");
    check(chunked_updated == want_updated,
          "partial parked chunks preserve OAM_UPDATED");
  }
}

static void test_declines(void)
{
  uint8_t src[32], oam[0x400], before[0x400];
  uint32_t updated;

  fill_random(src, sizeof(src));
  fill_random(oam, sizeof(oam));
  memcpy(before, oam, sizeof(before));

#define DECLINES(call_, label_) do {                                         \
    updated = UINT32_C(0xa5a5a5a5);                                         \
    check(!(call_), label_);                                                 \
    check(updated == UINT32_C(0xa5a5a5a5), label_ " leaves update alone"); \
    check(!memcmp(oam, before, sizeof(oam)), label_ " leaves OAM alone");   \
  } while (0)

  DECLINES(cgba_sh4_swi_copy_u16_to_oam(oam, src, UINT32_C(0x07000001),
                                               1, &updated),
           "odd destination declines");
  DECLINES(cgba_sh4_swi_copy_u16_to_oam(oam, src, UINT32_C(0x06000000),
                                               1, &updated),
           "non-OAM region declines");
  DECLINES(cgba_sh4_swi_copy_u16_to_oam(oam, src, UINT32_C(0x07fffffe),
                                               2, &updated),
           "region-crossing sequence declines");
  DECLINES(cgba_sh4_swi_copy_u16_to_oam(oam, oam + 8,
                                               UINT32_C(0x07000020), 8,
                                               &updated),
           "OAM-aliasing source declines");
  DECLINES(cgba_sh4_swi_copy_u16_to_oam(oam, src, UINT32_C(0x07000000),
                                               0, &updated),
           "empty sequence declines");

#undef DECLINES
}

static void test_region_end_accept(void)
{
  uint8_t src[2] = { 0x5au, 0xc3u };
  uint8_t got[0x400], want[0x400];
  uint32_t got_updated = 0;
  uint32_t want_updated = 0;

  fill_random(got, sizeof(got));
  memcpy(want, got, sizeof(want));
  reference_copy(want, src, UINT32_C(0x07fffffe), 1, &want_updated);
  check(cgba_sh4_swi_copy_u16_to_oam(
          got, src, UINT32_C(0x07fffffe), 1, &got_updated),
        "last aligned OAM-region halfword is accepted");
  check(!memcmp(got, want, sizeof(got)),
        "last aligned OAM-region halfword mirrors correctly");
  check(got_updated == want_updated,
        "last aligned OAM-region halfword updates OAM flag");
}

int main(void)
{
  test_zelda_shape();
  test_random_and_parked_chunks();
  test_region_end_accept();
  test_declines();
  printf("sh4 CpuSet OAM: %u checks passed\n", checks);
  return 0;
}
