#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_ea_mixer.h"

static void ref_advance_channel(uint32_t *ptr, uint32_t *frac,
                                uint32_t frac_inc, uint32_t int_inc,
                                uint32_t r1, uint32_t count)
{
  uint32_t lead = (r1 & 3u) ? (4u - (r1 & 3u)) : 0u;
  if (lead > count)
    lead = count;
  uint32_t rem = count - lead;
  uint32_t tail = rem & 3u;
  uint32_t body = rem - tail;

  for (uint32_t i = 0; i < lead + body; i++) {
    uint64_t f = (uint64_t)(*frac) + (uint64_t)frac_inc;
    *frac = (uint32_t)f;
    *ptr = *ptr + int_inc + (uint32_t)(f >> 32);
  }
  for (uint32_t i = 0; i < tail; i++) {
    uint64_t f = (uint64_t)(*frac) + (uint64_t)frac_inc;
    *frac = (uint32_t)f;
    *ptr = *ptr + 1u + int_inc + (uint32_t)(f >> 32);
  }
}

int main(void)
{
  /* 1. Signature prefix rejection checks */
  uint32_t buf[CGBA_SH4_EA_MIXER_WORDS];
  memset(buf, 0, sizeof(buf));
  if (cgba_sh4_ea_mixer_match_words(buf)) {
    fprintf(stderr, "FAIL: zero buffer matched EA mixer\n");
    return 1;
  }
  buf[0] = CGBA_SH4_EA_MIXER_W0;
  buf[1] = CGBA_SH4_EA_MIXER_W1;
  buf[2] = CGBA_SH4_EA_MIXER_W2;
  buf[3] = CGBA_SH4_EA_MIXER_W3;
  if (cgba_sh4_ea_mixer_match_words(buf)) {
    fprintf(stderr, "FAIL: prefix-only buffer matched EA mixer hash\n");
    return 1;
  }

  /* 2. Exact per-sample phase advance parity across counts, alignments, carries */
  static const uint32_t counts[] = { 0, 1, 2, 3, 4, 5, 7, 15, 16, 17, 19, 31, 32, 224, 304, 1023 };
  static const uint32_t r1s[] = { 0x03005F0Cu, 0x03005F0Du, 0x03005F0Eu, 0x03005F0Fu };
  static const uint32_t fracs[] = { 0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };
  static const uint32_t fincs[] = { 0u, 0x12345678u, 0x80000000u, 0xFFFFFFFFu };
  static const uint32_t iincs[] = { 0u, 1u, 2u, 5u };
  unsigned cases = 0;

  for (unsigned ci = 0; ci < sizeof(counts) / sizeof(*counts); ci++)
    for (unsigned ri = 0; ri < sizeof(r1s) / sizeof(*r1s); ri++)
      for (unsigned fi = 0; fi < sizeof(fracs) / sizeof(*fracs); fi++)
        for (unsigned fii = 0; fii < sizeof(fincs) / sizeof(*fincs); fii++)
          for (unsigned iii = 0; iii < sizeof(iincs) / sizeof(*iincs); iii++) {
            uint32_t count = counts[ci], r1 = r1s[ri];
            uint32_t lead = (r1 & 3u) ? (4u - (r1 & 3u)) : 0u;
            if (lead > count) lead = count;
            uint32_t tail = (count - lead) & 3u;

            uint32_t p1 = 0x08123456u, f1 = fracs[fi];
            uint32_t p2 = 0x08123456u, f2 = fracs[fi];
            cgba_sh4_ea_mixer_advance_channel(&p1, &f1, fincs[fii], iincs[iii],
                                              count, tail);
            ref_advance_channel(&p2, &f2, fincs[fii], iincs[iii], r1, count);
            if (p1 != p2 || f1 != f2) {
              fprintf(stderr,
                "FAIL phase count=%u r1=%08X f=%08X fi=%08X ii=%u: "
                "got (%08X,%08X) want (%08X,%08X)\n",
                count, r1, fracs[fi], fincs[fii], iincs[iii], p1, f1, p2, f2);
              return 1;
            }
            cases++;
          }

  /* 3. Cycle estimate monotonicity */
  uint32_t c0 = cgba_sh4_ea_mixer_estimate_cycles(0, 0x03005F0Cu, 4, 4);
  uint32_t c16 = cgba_sh4_ea_mixer_estimate_cycles(16, 0x03005F0Cu, 4, 4);
  uint32_t c256 = cgba_sh4_ea_mixer_estimate_cycles(256, 0x03005F0Cu, 4, 4);
  if (!(c0 < c16 && c16 < c256)) {
    fprintf(stderr, "FAIL cycle monotonicity: %u %u %u\n", c0, c16, c256);
    return 1;
  }

  printf("sh4 EA mixer fastpath: signature/phase/cycles OK (%u cases)\n", cases);
  return 0;
}
