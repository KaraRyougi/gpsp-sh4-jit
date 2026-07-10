#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_swi_overlap.h"

static int failures;

static void check(int ok, const char *what)
{
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static void test_zelda_cpuset(void)
{
  uint8_t ram[64];
  static const uint8_t seed[8] = { 0x10, 0x21, 0x32, 0x43,
                                   0x54, 0x65, 0x76, 0x87 };
  unsigned i;

  memset(ram, 0xa5, sizeof(ram));
  memcpy(ram, seed, sizeof(seed));
  check(cgba_sh4_swi_is_forward_overlap(ram + 8, ram, 24),
        "Zelda CpuSet shape is forward overlap");
  cgba_sh4_swi_copy_forward(ram + 8, ram, 24, 4);
  for (i = 0; i < 32; i++)
    check(ram[i] == seed[i & 7], "CpuSet expands the two-word seed");
  check(ram[32] == 0xa5, "CpuSet does not overrun its destination");
}

static void test_zelda_fastset(void)
{
  uint8_t ram[1056];
  uint8_t chunked[1056];
  uint8_t seed[32];
  unsigned i;

  for (i = 0; i < sizeof(seed); i++)
    seed[i] = (uint8_t)(i * 7u + 3u);
  memset(ram, 0x5a, sizeof(ram));
  memcpy(ram, seed, sizeof(seed));
  check(cgba_sh4_swi_is_forward_overlap(ram + 32, ram, 992),
        "Zelda FastSet shape is forward overlap");
  memcpy(chunked, ram, sizeof(ram));
  cgba_sh4_swi_copy_forward(ram + 32, ram, 992, 4);
  /* The parked engine processes one BIOS eight-word iteration per chunk.
   * Re-resolving the updated source between chunks must produce the same
   * pattern expansion as one uninterrupted call. */
  for (i = 0; i < 992; i += 32)
    cgba_sh4_swi_copy_forward(chunked + 32 + i, chunked + i, 32, 4);
  check(!memcmp(ram, chunked, sizeof(ram)),
        "parked 32-byte chunks equal uninterrupted FastSet");
  for (i = 0; i < 1024; i++)
    check(ram[i] == seed[i & 31], "FastSet expands the eight-word seed");
  check(ram[1024] == 0x5a, "FastSet does not overrun its destination");
}

static void test_direction_gate(void)
{
  uint8_t ram[32];
  check(!cgba_sh4_swi_is_forward_overlap(ram, ram, 16),
        "equal ranges are declined");
  check(!cgba_sh4_swi_is_forward_overlap(ram, ram + 4, 16),
        "backward overlap is declined");
  check(!cgba_sh4_swi_is_forward_overlap(ram + 16, ram, 16),
        "touching ranges are not overlap");
}

int main(void)
{
  test_zelda_cpuset();
  test_zelda_fastset();
  test_direction_gate();
  if (failures)
    return 1;
  puts("SH4 SWI forward-overlap tests passed");
  return 0;
}
