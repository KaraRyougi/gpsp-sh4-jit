#include <stdint.h>
#include <stdio.h>

#include "ports/fxcg100/sh4/sh4_idle_signature.h"

static int failures;

static void check(int ok, const char *what)
{
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

static int match(const uint16_t words[CGBA_SH4_AZLE_IDLE_HALFWORDS])
{
  return cgba_sh4_azle_idle_signature_match(words[0], words[1], words[2],
                                             words[3], words[4]);
}

int main(void)
{
  static const uint16_t expected[CGBA_SH4_AZLE_IDLE_HALFWORDS] = {
    0x8811u, 0x1C18u, 0x4008u, 0x2800u, 0xD0FAu
  };
  uint16_t words[CGBA_SH4_AZLE_IDLE_HALFWORDS];
  unsigned i;

  check(CGBA_SH4_AZLE_IDLE_PC == 0x080683A8u, "verified loop-head PC");
  check(match(expected), "exact AZLE revision-0 loop is accepted");

  for (i = 0; i < CGBA_SH4_AZLE_IDLE_HALFWORDS; i++) {
    unsigned j;
    for (j = 0; j < CGBA_SH4_AZLE_IDLE_HALFWORDS; j++)
      words[j] = expected[j];
    words[i] ^= 1u;
    check(!match(words), "a changed halfword is declined");
  }

  if (failures)
    return 1;
  puts("SH4 AZLE idle signature tests passed");
  return 0;
}
