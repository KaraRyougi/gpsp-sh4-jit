#ifndef CGBA_SH4_THUMB_UDIV_H
#define CGBA_SH4_THUMB_UDIV_H

/*
 * Pattern and exact state/cycle model for the Thumb-1 `__udivsi3` tail emitted
 * by the old GCC/libgcc routine used by a number of GBA games.
 *
 * The optimized JIT path starts at the four-bit quotient loop, after the
 * routine has already saved r4 and aligned r1/r3.  It stops immediately before
 * `movs r0,r2; pop {r4}; mov pc,lr`, so stack memory and return behavior remain
 * on the ordinary translated path.  This small model is target-independent on
 * purpose: host tests compare it with an instruction-by-instruction reference.
 */

#include <stdint.h>

#define CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS 28u
#define CGBA_SH4_THUMB_UDIV_LOOP_BYTES \
  (CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS * 2u)

static const uint16_t cgba_sh4_thumb_udiv_loop_pattern[
  CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS] = {
  0x4288, 0xD301, 0x1A40, 0x431A,
  0x084C, 0x42A0, 0xD302, 0x1B00, 0x085C, 0x4322,
  0x088C, 0x42A0, 0xD302, 0x1B00, 0x089C, 0x4322,
  0x08CC, 0x42A0, 0xD302, 0x1B00, 0x08DC, 0x4322,
  0x2800, 0xD003, 0x091B, 0xD001, 0x0909, 0xE7E3
};

static inline int cgba_sh4_thumb_udiv_loop_match(const uint16_t *ops)
{
  uint32_t i;
  for (i = 0; i < CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS; i++) {
    if (ops[i] != cgba_sh4_thumb_udiv_loop_pattern[i])
      return 0;
  }
  return 1;
}

typedef struct cgba_sh4_thumb_udiv_loop_result {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r4;
  uint32_t cpsr;
  uint32_t seq_insns;
  uint32_t branch_refills;
  uint32_t cycles;
} cgba_sh4_thumb_udiv_loop_result;

/* Run from the instruction at pattern[0] through the taken branch to the first
 * instruction after the pattern.  Every Thumb instruction has one sequential
 * fetch; every conditional branch (taken or not) and the loop-back B have one
 * nonsequential refill in gpSP's interpreter model. */
static inline cgba_sh4_thumb_udiv_loop_result
cgba_sh4_thumb_udiv_loop_run(uint32_t r0, uint32_t r1, uint32_t r2,
                             uint32_t r3, uint32_t r4, uint32_t cpsr,
                             uint32_t seq_cost, uint32_t nseq_cost)
{
  cgba_sh4_thumb_udiv_loop_result out;
  uint32_t seq = 0;
  uint32_t branches = 0;
  uint32_t final_c = 1;

  for (;;) {
    /* CMP r0,r1; BCC 1f */
    seq += 2;
    branches++;
    if (r0 >= r1) {
      r0 -= r1;                         /* SUBS r0,r0,r1 */
      r2 |= r3;                         /* ORRS r2,r3 */
      seq += 2;
    }

    /* Three more quotient bits, at r1/r3 >> {1,2,3}. */
    r4 = r1 >> 1;
    seq += 3;                           /* LSRS; CMP; BCC */
    branches++;
    if (r0 >= r4) {
      r0 -= r4;
      r4 = r3 >> 1;
      r2 |= r4;
      seq += 3;
    }

    r4 = r1 >> 2;
    seq += 3;
    branches++;
    if (r0 >= r4) {
      r0 -= r4;
      r4 = r3 >> 2;
      r2 |= r4;
      seq += 3;
    }

    r4 = r1 >> 3;
    seq += 3;
    branches++;
    if (r0 >= r4) {
      r0 -= r4;
      r4 = r3 >> 3;
      r2 |= r4;
      seq += 3;
    }

    /* CMP r0,#0; BEQ done.  CMP establishes C=1,V=0 for either exit. */
    seq += 2;
    branches++;
    if (r0 == 0) {
      final_c = 1;
      break;
    }

    /* LSRS r3,r3,#4; BEQ done.  LSR replaces N/Z/C and preserves V=0. */
    {
      uint32_t old_r3 = r3;
      r3 >>= 4;
      final_c = (old_r3 >> 3) & 1u;
    }
    seq += 2;
    branches++;
    if (r3 == 0)
      break;

    /* LSRS r1,r1,#4; B loop. */
    r1 >>= 4;
    seq += 2;
    branches++;
  }

  out.r0 = r0;
  out.r1 = r1;
  out.r2 = r2;
  out.r3 = r3;
  out.r4 = r4;
  out.cpsr = (cpsr & 0x0FFFFFFFu) | (1u << 30) |
             (final_c ? (1u << 29) : 0u);   /* N=0,Z=1,C=final_c,V=0 */
  out.seq_insns = seq;
  out.branch_refills = branches;
  out.cycles = seq * seq_cost + branches * nseq_cost;
  return out;
}

static inline int cgba_sh4_thumb_udiv_loop_budget_ok(int32_t budget,
                                                      uint32_t cycles)
{
  /* Strictly positive remainder matches the JIT branch/gate convention. */
  return budget > 0 && (uint32_t)budget > cycles;
}

#endif /* CGBA_SH4_THUMB_UDIV_H */
