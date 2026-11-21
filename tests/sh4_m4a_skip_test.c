#include <stdint.h>
#include <stdio.h>

#include "ports/fxcg100/sh4/sh4_m4a_skip.h"

/* Audit of the m4a elision table.  The liveness argument in sh4_m4a_skip.h
 * holds only if each row is the kind of instruction it claims to be; this
 * decodes every row and enforces that. */

#define FAIL(...) do { printf("m4a skip table: " __VA_ARGS__); printf("\n"); \
                       return 1; } while (0)

/* Registers a dropped instruction may write / a skipped load may target.
 * The reverb loop (before the mix entry) uses r5/r7 as buffer pointers and r6
 * as a constant index, so only its r0/r1 temporaries are scratch there; in
 * the mix paths r6/r7 are the PCM accumulators and ip/lr interpolation
 * temporaries. */
static unsigned row_offset;
static int dead_reg(unsigned r)
{
  if (r == 0 || r == 1)
    return 1;
  if (row_offset < CGBA_SH4_M4A_SKIP_ENTRY_MIX)
    return 0;
  return r == 6 || r == 7 || r == 12 || r == 14;
}

/* ---- loop-fold differential -------------------------------------------
 * Instruction-order references for the two folded loops, written from the
 * disassembly one instruction per line: `I` is a fetched instruction
 * (elided, condition-failed and executed ones all cost one sequential
 * fetch), `B` a taken branch, `M` a transfer at an address.  They run an
 * exact number of iterations and know nothing about budgets. */
static uint8_t mem_cost[16][2];
static uint32_t SEQ, BR;
static uint32_t rng_state = 0x1234abcdu;
static uint32_t rnd(void)
{
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
#define I (cyc += SEQ)
#define B (cyc += BR)
#define M(addr, col) (cyc += mem_cost[((addr) >> 24) & 15][col])

static uint32_t ref_reverb(uint32_t n, uint32_t *r4, uint32_t *r5, uint32_t r6,
                           uint32_t *r7, uint32_t *cpsr)
{
  uint32_t cyc = 0;
  while (n--) {
    I; M(*r5 + r6, 0);            /* ldrsb r0,[r5,r6]   */
    I; M(*r5, 0);                 /* ldrsb r1,[r5]      */
    I;                            /* add                */
    I; M(*r7 + r6, 0);            /* ldrsb r1,[r7,r6]   */
    I;                            /* add                */
    I; M(*r7, 0); *r7 += 1;       /* ldrsb r1,[r7],#1   */
    I;                            /* add                */
    I;                            /* mul                */
    I;                            /* asr                */
    I;                            /* tst                */
    I;                            /* addne              */
    I; M(*r5 + r6, 0);            /* strb r0,[r5,r6]    */
    I; M(*r5, 0); *r5 += 1;       /* strb r0,[r5],#1    */
    I; *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(*r4, 1);
       *r4 -= 1;                  /* subs r4,r4,#1      */
    I; B;                         /* bgt (taken)        */
  }
  return cyc;
}

/* Runs `groups` whole four-sample fixed-rate groups from the group head. */
static uint32_t ref_fixed(uint32_t groups, uint32_t *r3, uint32_t *r5,
                          uint32_t *r8, uint32_t *cpsr, uint32_t stride)
{
  uint32_t cyc = 0;
  while (groups--) {
    I; M(*r5, 1);                 /* ldr r6,[r5]            */
    I; M(*r5 + stride, 1);        /* ldr r7,[r5,#stride]    */
    for (;;) {
      uint32_t before;
      I; M(*r3, 0); *r3 += 1;     /* ldrsb r0,[r3],#1       */
      I; I; I; I; I; I;           /* mul/bic/add/mul/bic/add */
      before = *r5;
      I; *r5 += 0x40000000u;      /* adds r5,r5,#0x40000000 */
      I;                          /* bcc                    */
      if (*r5 >= before) { B; continue; }
      break;
    }
    I; M(*r5 + stride, 1);        /* str r7,[r5,#stride]    */
    I; M(*r5, 1); *r5 += 4;       /* str r6,[r5],#4         */
    I; *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(*r8, 4);
       *r8 -= 4;                  /* subs r8,r8,#4          */
    I; B;                         /* bgt (taken)            */
  }
  return cyc;
}

/* Runs `groups` whole four-sample groups from the group head.  Returns 0 and
 * sets *stopped if a group would leave the fold's domain (wave end). */
static uint32_t ref_interp(uint32_t groups, cgba_sh4_m4a_interp_state *s,
                           uint32_t stride, int *stopped)
{
  uint32_t cyc = 0;
  *stopped = 0;
  while (groups--) {
    I; M(s->r5, 1);               /* ldr r6,[r5]            */
    I; M(s->r5 + stride, 1);      /* ldr r7,[r5,#stride]    */
    for (;;) {
      uint32_t before;
      I; I; I; I; I; I; I; I;     /* mul/add/mul/bic/add/mul/bic/add */
      I; s->r9 += s->r4;          /* add r9,r9,r4           */
      I; s->lr = s->r9 >> 23;     /* lsrs lr,r9,#23         */
      I;                          /* beq                    */
      if (s->lr == 0) {
        B;
      } else {
        I; s->r9 &= ~0x3F800000u; /* bic r9                 */
        I; if ((int32_t)s->r2 <= (int32_t)s->lr) { *stopped = 1; return 0; }
           s->r2 -= s->lr;        /* subs r2,r2,lr          */
        I;                        /* ble (not taken)        */
        I; s->lr -= 1;            /* subs lr,lr,#1          */
        I;                        /* addeq                  */
        I; if (s->lr) { s->r3 += s->lr; M(s->r3, 0); }  /* ldrsbne r0,[r3,lr]! */
        I; s->r3 += 1; M(s->r3, 0);                     /* ldrsb r1,[r3,#1]!   */
        I;                        /* sub                    */
      }
      before = s->r5;
      I; s->r5 += 0x40000000u;    /* adds r5,r5,#0x40000000 */
      I;                          /* bcc                    */
      if (s->r5 >= before) { B; continue; }   /* carry clear: next sample */
      break;
    }
    I; M(s->r5 + stride, 1);      /* str r7,[r5,#stride]    */
    I; M(s->r5, 1); s->r5 += 4;   /* str r6,[r5],#4         */
    I; s->cpsr = (s->cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(s->r8, 4);
       s->r8 -= 4;                /* subs r8,r8,#4          */
    I; B;                         /* bgt (taken)            */
  }
  return cyc;
}

static uint32_t ref_reverb_mono(uint32_t n, uint32_t *r4, uint32_t *r5,
                                uint32_t *r7, uint32_t *cpsr)
{
  uint32_t cyc = 0;
  while (n--) {
    I; M(*r5, 0);                 /* ldrsb r0,[r5]      */
    I; M(*r7, 0); *r7 += 1;       /* ldrsb r1,[r7],#1   */
    I;                            /* add                */
    I;                            /* mul                */
    I;                            /* asr                */
    I;                            /* tst                */
    I;                            /* addne              */
    I; M(*r5, 0); *r5 += 1;       /* strb r0,[r5],#1    */
    I; *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(*r4, 1);
       *r4 -= 1;                  /* subs r4,r4,#1      */
    I; B;                         /* bgt (taken)        */
  }
  return cyc;
}

static uint32_t ref_fixed_mono(uint32_t groups, uint32_t *r3, uint32_t *r5,
                               uint32_t *r8, uint32_t *cpsr)
{
  uint32_t cyc = 0;
  while (groups--) {
    I; M(*r5, 1);                 /* ldr r6,[r5]        */
    for (;;) {
      uint32_t before;
      I; M(*r3, 0); *r3 += 1;     /* ldrsb r0,[r3],#1   */
      I; I; I;                    /* mul/bic/add        */
      before = *r5;
      I; *r5 += 0x40000000u;      /* adds r5,r5,#0x40000000 */
      I;                          /* bcc                */
      if (*r5 >= before) { B; continue; }
      break;
    }
    I; M(*r5, 1); *r5 += 4;       /* str r6,[r5],#4     */
    I; *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(*r8, 4);
       *r8 -= 4;                  /* subs r8,r8,#4      */
    I; B;                         /* bgt (taken)        */
  }
  return cyc;
}

static uint32_t ref_interp_mono(uint32_t groups, cgba_sh4_m4a_interp_state *s,
                                int *stopped)
{
  uint32_t cyc = 0;
  *stopped = 0;
  while (groups--) {
    I; M(s->r5, 1);               /* ldr r6,[r5]            */
    for (;;) {
      uint32_t before;
      I; I; I; I; I;              /* mul/add/mul/bic/add    */
      I; s->r9 += s->r4;          /* add lr,lr,r4           */
      I; s->lr = s->r9 >> 23;     /* lsrs r9,lr,#23         */
      I;                          /* beq                    */
      if (s->lr == 0) {
        B;
      } else {
        I; s->r9 &= ~0x3F800000u; /* bic lr                 */
        I; if ((int32_t)s->r2 <= (int32_t)s->lr) { *stopped = 1; return 0; }
           s->r2 -= s->lr;        /* subs r2,r2,r9          */
        I;                        /* ble (not taken)        */
        I; s->lr -= 1;            /* subs r9,r9,#1          */
        I;                        /* addeq                  */
        I; if (s->lr) { s->r3 += s->lr; M(s->r3, 0); }  /* ldrsbne r0,[r3,r9]! */
        I; s->r3 += 1; M(s->r3, 0);                     /* ldrsb r1,[r3,#1]!   */
        I;                        /* sub                    */
      }
      before = s->r5;
      I; s->r5 += 0x40000000u;    /* adds r5,r5,#0x40000000 */
      I;                          /* bcc                    */
      if (s->r5 >= before) { B; continue; }
      break;
    }
    I; M(s->r5, 1); s->r5 += 4;   /* str r6,[r5],#4         */
    I; s->cpsr = (s->cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(s->r8, 4);
       s->r8 -= 4;                /* subs r8,r8,#4          */
    I; B;                         /* bgt (taken)            */
  }
  return cyc;
}
#undef I
#undef B
#undef M

static int test_folds(void)
{
  static const uint32_t strides[4] = { 0x630u, 0x310u, 0x440u, 0x800u };
  cgba_sh4_m4a_loop_costs costs;
  unsigned long folded = 0, declined = 0;
  unsigned n;

  for (n = 0; n < 200000; n++) {
    unsigned i;
    int32_t budget = (int32_t)(rnd() % 3000) - 100;
    uint32_t stride = strides[n & 3u];
    for (i = 0; i < 16; i++) {
      mem_cost[i][0] = (uint8_t)(1 + rnd() % 9);
      mem_cost[i][1] = (uint8_t)(1 + rnd() % 9);
    }
    SEQ = 1 + rnd() % 3; BR = 1 + rnd() % 5;
    costs.seq = SEQ; costs.branch = BR;
    costs.mem = (const uint8_t (*)[2])mem_cost;

    {                                                   /* reverb */
      uint32_t r4 = (n % 7 == 0) ? rnd() % 3 : rnd() % 300;
      uint32_t r5 = 0x03000000u + (rnd() & 0x7FFF), r6 = stride;
      uint32_t r7 = (rnd() & 1) ? r5 : 0x03000000u + (rnd() & 0x7FFF);
      uint32_t cpsr = rnd();
      uint32_t a4 = r4, a5 = r5, a7 = r7, ac = cpsr;
      uint32_t spent, want;
      if (n % 11 == 0) { r5 = a5 = 0x00FFFFF0u + rnd() % 32; }  /* region seam */
      spent = cgba_sh4_m4a_fold_reverb(&a4, &a5, r6, &a7, &ac, budget, &costs);
      if (!spent) {
        declined++;
        if (a4 != r4 || a5 != r5 || a7 != r7 || ac != cpsr)
          FAIL("reverb: declined fold changed state");
      } else {
        uint32_t k = r4 - a4;
        folded++;
        want = ref_reverb(k, &r4, &r5, r6, &r7, &cpsr);
        if (want != spent || a4 != r4 || a5 != r5 || a7 != r7 || ac != cpsr)
          FAIL("reverb: fold of %u iterations differs (cycles %u vs %u)",
               k, spent, want);
        if ((int32_t)a4 < 1)
          FAIL("reverb: folded the exiting iteration");
        if (budget - (int32_t)spent <= 0)
          FAIL("reverb: fold exhausted the budget");
      }
    }
    {                                                   /* fixed-rate mix */
      uint32_t r3 = 0x08000000u + (rnd() & 0x00FFFFFF);
      uint32_t r5 = 0x03000000u + (rnd() & 0x7FFC);
      uint32_t r8 = (n % 9 == 0) ? rnd() % 9 : rnd() % 300;
      uint32_t cpsr = rnd();
      uint32_t a3, a5, a8, ac;
      uint32_t spent, want, groups;
      if (n % 13 == 0) r3 = 0x08FFFFF0u + rnd() % 32;   /* r3 region seam */
      if (n % 17 == 0) r5 = 0x03FFFFE0u + (rnd() % 64); /* r5 region seam */
      a3 = r3; a5 = r5; a8 = r8; ac = cpsr;
      spent = cgba_sh4_m4a_fold_fixed(&a3, &a5, &a8, &ac, stride, budget, &costs);
      if (!spent) {
        declined++;
        if (a3 != r3 || a5 != r5 || a8 != r8 || ac != cpsr)
          FAIL("fixed: declined fold changed state");
      } else {
        groups = (r8 - a8) / 4;
        folded++;
        want = ref_fixed(groups, &r3, &r5, &r8, &cpsr, stride);
        if (want != spent || a3 != r3 || a5 != r5 || a8 != r8 || ac != cpsr)
          FAIL("fixed: fold of %u groups differs (cycles %u vs %u)",
               groups, spent, want);
        if ((int32_t)a8 <= 0)
          FAIL("fixed: folded the last group");
        if (budget - (int32_t)spent <= 0)
          FAIL("fixed: fold exhausted the budget");
      }
    }
    {                                                   /* interpolating mix */
      cgba_sh4_m4a_interp_state s, a, r;
      uint32_t spent, want, groups;
      int stopped;
      s.r2 = (n % 5 == 0) ? rnd() % 40 : rnd() % 20000;
      s.r3 = 0x08000000u + (rnd() & 0x00FFFFFF);
      s.r4 = (n % 3 == 0) ? rnd() & 0x00FFFFFF : rnd() & 0x03FFFFFF;
      s.r5 = 0x03000000u + (rnd() & 0x7FFC);
      s.r8 = (n % 9 == 0) ? rnd() % 9 : rnd() % 300;
      s.r9 = rnd() & 0x007FFFFF;
      s.lr = rnd(); s.cpsr = rnd();
      if (n % 13 == 0) s.r3 = 0x08FFFFF0u + rnd() % 64;   /* region seam */
      a = s; r = s;
      spent = (stride == 0x630u)
        ? cgba_sh4_m4a_fold_interp(&a, budget, &costs)
        : cgba_sh4_m4a_fold_interp_stride(&a, stride, budget, &costs);
      if (!spent) {
        declined++;
        if (a.r2 != s.r2 || a.r3 != s.r3 || a.r5 != s.r5 || a.r8 != s.r8 ||
            a.r9 != s.r9 || a.lr != s.lr || a.cpsr != s.cpsr)
          FAIL("interp: declined fold changed state");
      } else {
        groups = (s.r8 - a.r8) / 4;
        folded++;
        want = ref_interp(groups, &r, stride, &stopped);
        if (stopped)
          FAIL("interp: folded across a wave end");
        if (want != spent || a.r2 != r.r2 || a.r3 != r.r3 || a.r5 != r.r5 ||
            a.r8 != r.r8 || a.r9 != r.r9 || a.lr != r.lr || a.cpsr != r.cpsr ||
            a.r4 != r.r4)
          FAIL("interp: fold of %u groups differs (cycles %u vs %u)",
               groups, spent, want);
        if ((int32_t)a.r8 <= 0)
          FAIL("interp: folded the last group");
        if (budget - (int32_t)spent <= 0)
          FAIL("interp: fold exhausted the budget");
      }
    }
    {                                                   /* mono reverb */
      uint32_t r4 = (n % 7 == 0) ? rnd() % 3 : rnd() % 300;
      uint32_t r5 = 0x03000000u + (rnd() & 0x7FFF);
      uint32_t r7 = (rnd() & 1) ? r5 : 0x03000000u + (rnd() & 0x7FFF);
      uint32_t cpsr = rnd();
      uint32_t a4 = r4, a5 = r5, a7 = r7, ac = cpsr;
      uint32_t spent, want;
      if (n % 11 == 0) { r5 = a5 = 0x00FFFFF0u + rnd() % 32; }
      spent = cgba_sh4_m4a_fold_reverb_mono(&a4, &a5, &a7, &ac, budget, &costs);
      if (!spent) {
        declined++;
        if (a4 != r4 || a5 != r5 || a7 != r7 || ac != cpsr)
          FAIL("mono reverb: declined fold changed state");
      } else {
        uint32_t k = r4 - a4;
        folded++;
        want = ref_reverb_mono(k, &r4, &r5, &r7, &cpsr);
        if (want != spent || a4 != r4 || a5 != r5 || a7 != r7 || ac != cpsr)
          FAIL("mono reverb: fold of %u iterations differs (cycles %u vs %u)",
               k, spent, want);
        if ((int32_t)a4 < 1)
          FAIL("mono reverb: folded the exiting iteration");
        if (budget - (int32_t)spent <= 0)
          FAIL("mono reverb: fold exhausted the budget");
      }
    }
    {                                                   /* mono fixed-rate */
      uint32_t r3 = 0x08000000u + (rnd() & 0x00FFFFFF);
      uint32_t r5 = 0x03000000u + (rnd() & 0x7FFC);
      uint32_t r8 = (n % 9 == 0) ? rnd() % 9 : rnd() % 300;
      uint32_t cpsr = rnd();
      uint32_t a3, a5, a8, ac;
      uint32_t spent, want, groups;
      if (n % 13 == 0) r3 = 0x08FFFFF0u + rnd() % 32;
      if (n % 17 == 0) r5 = 0x03FFFFE0u + (rnd() % 64);
      a3 = r3; a5 = r5; a8 = r8; ac = cpsr;
      spent = cgba_sh4_m4a_fold_fixed_mono(&a3, &a5, &a8, &ac, budget, &costs);
      if (!spent) {
        declined++;
        if (a3 != r3 || a5 != r5 || a8 != r8 || ac != cpsr)
          FAIL("mono fixed: declined fold changed state");
      } else {
        groups = (r8 - a8) / 4;
        folded++;
        want = ref_fixed_mono(groups, &r3, &r5, &r8, &cpsr);
        if (want != spent || a3 != r3 || a5 != r5 || a8 != r8 || ac != cpsr)
          FAIL("mono fixed: fold of %u groups differs (cycles %u vs %u)",
               groups, spent, want);
        if ((int32_t)a8 <= 0)
          FAIL("mono fixed: folded the last group");
        if (budget - (int32_t)spent <= 0)
          FAIL("mono fixed: fold exhausted the budget");
      }
    }
    {                                                   /* mono interpolating */
      cgba_sh4_m4a_interp_state s, a, r;
      uint32_t spent, want, groups;
      int stopped;
      s.r2 = (n % 5 == 0) ? rnd() % 40 : rnd() % 20000;
      s.r3 = 0x08000000u + (rnd() & 0x00FFFFFF);
      s.r4 = (n % 3 == 0) ? rnd() & 0x00FFFFFF : rnd() & 0x03FFFFFF;
      s.r5 = 0x03000000u + (rnd() & 0x7FFC);
      s.r8 = (n % 9 == 0) ? rnd() % 9 : rnd() % 300;
      s.r9 = rnd() & 0x007FFFFF;
      s.lr = rnd(); s.cpsr = rnd();
      if (n % 13 == 0) s.r3 = 0x08FFFFF0u + rnd() % 64;
      a = s; r = s;
      spent = cgba_sh4_m4a_fold_interp_mono(&a, budget, &costs);
      if (!spent) {
        declined++;
        if (a.r2 != s.r2 || a.r3 != s.r3 || a.r5 != s.r5 || a.r8 != s.r8 ||
            a.r9 != s.r9 || a.lr != s.lr || a.cpsr != s.cpsr)
          FAIL("mono interp: declined fold changed state");
      } else {
        groups = (s.r8 - a.r8) / 4;
        folded++;
        want = ref_interp_mono(groups, &r, &stopped);
        if (stopped)
          FAIL("mono interp: folded across a wave end");
        if (want != spent || a.r2 != r.r2 || a.r3 != r.r3 || a.r5 != r.r5 ||
            a.r8 != r.r8 || a.r9 != r.r9 || a.lr != r.lr || a.cpsr != r.cpsr ||
            a.r4 != r.r4)
          FAIL("mono interp: fold of %u groups differs (cycles %u vs %u)",
               groups, spent, want);
        if ((int32_t)a.r8 <= 0)
          FAIL("mono interp: folded the last group");
        if (budget - (int32_t)spent <= 0)
          FAIL("mono interp: fold exhausted the budget");
      }
    }
  }
  if (!folded || !declined)
    FAIL("fold test did not cover both outcomes");
  printf("m4a loop fold: %lu folds matched the instruction-order reference "
         "(%lu declined)\n", folded, declined);
  return 0;
}

static int dead_reg_for_kind(uint8_t kind, unsigned r)
{
  if (r == 0 || r == 1)
    return 1;
  if (kind == CGBA_SH4_M4A_KIND_REVERB ||
      kind == CGBA_SH4_M4A_KIND_MONO_REVERB)
    return 0;
  if (kind == CGBA_SH4_M4A_KIND_MONO_FIXED ||
      kind == CGBA_SH4_M4A_KIND_MONO_FIXED_TAIL)
    return r == 6;
  if (kind == CGBA_SH4_M4A_KIND_MONO_INTERP_B)
    return r == 6 || r == 9 || r == 12;
  if (kind == CGBA_SH4_M4A_KIND_MONO_INTERP_A)
    return r == 6 || r == 12 || r == 14;
  if (kind == CGBA_SH4_M4A_KIND_INTERP_B)
    return r == 6 || r == 7 || r == 9 || r == 12;
  return r == 6 || r == 7 || r == 12 || r == 14;
}

static int audit_structural_loop(const char *name, const uint32_t *w,
                                 unsigned len, int has_pre,
                                 uint8_t want_kind, uint16_t want_stride)
{
  cgba_sh4_m4a_match m;
  uint32_t head = 0x03000B04u;
  unsigned i;

  if (!cgba_sh4_m4a_match_loop_words(w, len, head, has_pre, &m))
    FAIL("%s: expected match", name);
  if (m.kind != want_kind || m.stride != want_stride || m.head_pc != head ||
      m.pre_bytes != (has_pre ? 12u : 0u))
    FAIL("%s: wrong match metadata (kind=%u stride=0x%x pre=%u)",
         name, m.kind, m.stride, m.pre_bytes);

  /* Corrupting any matched word (other than branch displacement on conditional
   * exits or ADR offset in reverb epilogue) must reject the match. */
  for (i = 0; i < len; i++) {
    uint32_t bad[CGBA_SH4_M4A_MAX_LOOP_WORDS];
    cgba_sh4_m4a_match bm;
    unsigned j;
    for (j = 0; j < len; j++) bad[j] = w[j];
    bad[i] ^= 0x0F000000u; /* flip top opcode/cond nibble */
    if (cgba_sh4_m4a_match_loop_words(bad, len, head, has_pre, &bm))
      FAIL("%s: corruption at word %u still matched", name, i);
  }

  /* Audit every instruction classified as DROP or MEM by cgba_sh4_m4a_match_class. */
  for (i = 0; i < m.body_bytes / 4u; i++) {
    uint32_t op = w[i];
    unsigned cls = cgba_sh4_m4a_match_class(&m, head + i * 4u, op);
    unsigned rd = (op >> 12) & 15u;
    if (cls == CGBA_SH4_M4A_SKIP_DROP) {
      if ((op & 0x0FC000F0u) == 0x00000090u) {
        unsigned mrd = (op >> 16) & 15u;
        if ((op & 0x00100000u) || !dead_reg_for_kind(m.kind, mrd))
          FAIL("%s word %u: illegal DROP multiply", name, i);
      } else if ((op & 0x0C000000u) == 0u) {
        unsigned opc = (op >> 21) & 15u;
        if (opc < 8u || opc > 11u) {
          if ((op & 0x00100000u) || !dead_reg_for_kind(m.kind, rd))
            FAIL("%s word %u: illegal DROP ALU writing r%u", name, i, rd);
        }
      } else {
        FAIL("%s word %u: DROP is not ALU/MUL", name, i);
      }
    } else if (cls == CGBA_SH4_M4A_SKIP_MEM) {
      cgba_sh4_m4a_skip_mem mm;
      if (!cgba_sh4_m4a_skip_mem_decode(op, &mm))
        FAIL("%s word %u: MEM does not decode", name, i);
      if ((op & 0x00100000u) && !dead_reg_for_kind(m.kind, rd))
        FAIL("%s word %u: MEM load writes live r%u", name, i, rd);
      if (dead_reg_for_kind(m.kind, mm.rn))
        FAIL("%s word %u: MEM base r%u is scratch", name, i, mm.rn);
    }
  }
  return 0;
}

static int test_structural_matcher(void)
{
  static const uint32_t rev[17] = {
    0xE19500D6u, 0xE1D510D0u, 0xE0800001u, 0xE19710D6u, 0xE0800001u,
    0xE0D710D1u, 0xE0800001u, 0xE0010390u, 0xE1A004C1u, 0xE3100080u,
    0x12800001u, 0xE7C50006u, 0xE4C50001u, 0xE2544001u, 0xCAFFFFF0u,
    0xE28F0010u, 0xE12FFF10u
  };
  static const uint32_t fix[15] = {
    0xE5956000u, 0xE5957630u, 0xE0D300D1u, 0xE001009Au, 0xE3C118FFu,
    0xE0816466u, 0xE001009Bu, 0xE3C118FFu, 0xE0817467u, 0xE2955101u,
    0x3AFFFFF6u, 0xE5857630u, 0xE4856004u, 0xE2588004u, 0xCAFFFFF0u
  };
  static const uint32_t fix_tail[17] = {
    0xE5956000u, 0xE5957310u, 0xE0D300D1u, 0xE001009Au, 0xE3C118FFu,
    0xE0816466u, 0xE001009Bu, 0xE3C118FFu, 0xE0817467u, 0xE2522001u,
    0x0A000012u, 0xE2955101u, 0x3AFFFFF4u, 0xE5857310u, 0xE4856004u,
    0xE2588004u, 0xCAFFFFEEu
  };
  static const uint32_t interp_a[29] = {
    0xE5956000u, 0xE5957630u, 0xE00E0199u, 0xE080EBCEu, 0xE00C0E9Au,
    0xE3CCC8FFu, 0xE08C6466u, 0xE00C0E9Bu, 0xE3CCC8FFu, 0xE08C7467u,
    0xE0899004u, 0xE1B0EBA9u, 0x0A000007u, 0xE3C995FEu, 0xE052200Eu,
    0xDAFFFFD2u, 0xE25EE001u, 0x00800001u, 0x11B300DEu, 0xE1F310D1u,
    0xE0411000u, 0xE2955101u, 0x3AFFFFEAu, 0xE5857630u, 0xE4856004u,
    0xE2588004u, 0xCAFFFFE4u, 0xE2433001u, 0xE8BD1010u
  };
  /* Exact Prince of Persia: The Sands of Time (early-SDK MP2K Flavor B). */
  static const uint32_t interp_b[29] = {
    0xE5956000u, 0xE5957630u, 0xE009019Eu, 0xE0809BC9u, 0xE00C099Au,
    0xE3CCC8FFu, 0xE08C6466u, 0xE00C099Bu, 0xE3CCC8FFu, 0xE08C7467u,
    0xE08EE004u, 0xE1B09BAEu, 0x0A000007u, 0xE3CEE5FEu, 0xE0522009u,
    0xDAFFFFD7u, 0xE2599001u, 0x00800001u, 0x11B300D9u, 0xE1F310D1u,
    0xE0411000u, 0xE2955101u, 0x3AFFFFEAu, 0xE5857630u, 0xE4856004u,
    0xE2588004u, 0xCAFFFFE4u, 0xE2433001u, 0xE8BD1010u
  };
  /* Exact Mario Kart: Super Circuit mono MP2K loops (Flavor B) + Flavor A dual. */
  static const uint32_t mono_rev[12] = {
    0xE1D500D0u, 0xE0D710D1u, 0xE0800001u, 0xE0010390u, 0xE1A00441u,
    0xE3100080u, 0x12800001u, 0xE4C50001u, 0xE2544001u, 0xCAFFFFF5u,
    0xE28F001Fu, 0xE12FFF10u
  };
  static const uint32_t mono_fix[10] = {
    0xE5956000u, 0xE0D300D1u, 0xE001009Au, 0xE3C118FFu, 0xE0816466u,
    0xE2955101u, 0x3AFFFFF9u, 0xE4856004u, 0xE2588004u, 0xCAFFFFF5u
  };
  static const uint32_t mono_fix_tail[12] = {
    0xE5956000u, 0xE0D300D1u, 0xE001009Au, 0xE3C118FFu, 0xE0816466u,
    0xE2522001u, 0x0A000011u, 0xE2955101u, 0x3AFFFFF7u, 0xE4856004u,
    0xE2588004u, 0xCAFFFFDCu
  };
  static const uint32_t mono_interp_b[24] = {
    0xE5956000u, 0xE009019Eu, 0xE0809BC9u, 0xE00C099Au, 0xE3CCC8FFu,
    0xE08C6466u, 0xE08EE004u, 0xE1B09BAEu, 0x0A000007u, 0xE3CEE5FEu,
    0xE0522009u, 0xDAFFFFD4u, 0xE2599001u, 0x00800001u, 0x11B300D9u,
    0xE1F310D1u, 0xE0411000u, 0xE2955101u, 0x3AFFFFEDu, 0xE4856004u,
    0xE2588004u, 0xCAFFFFE9u, 0xE2433001u, 0xE8BD1010u
  };
  static const uint32_t mono_interp_a[24] = {
    0xE5956000u, 0xE00E0199u, 0xE080EBCEu, 0xE00C0E9Au, 0xE3CCC8FFu,
    0xE08C6466u, 0xE0899004u, 0xE1B0EBA9u, 0x0A000007u, 0xE3C995FEu,
    0xE052200Eu, 0xDAFFFFD2u, 0xE25EE001u, 0x00800001u, 0x11B300DEu,
    0xE1F310D1u, 0xE0411000u, 0xE2955101u, 0x3AFFFFEDu, 0xE4856004u,
    0xE2588004u, 0xCAFFFFE9u, 0xE2433001u, 0xE8BD1010u
  };
  uint32_t custom_b[29];
  uint32_t bad_mrev[12];
  cgba_sh4_m4a_match m;
  unsigned i;

  if (audit_structural_loop("reverb", rev, 17u, 0, CGBA_SH4_M4A_KIND_REVERB, 0u))
    return 1;
  if (audit_structural_loop("fixed", fix, 15u, 0, CGBA_SH4_M4A_KIND_FIXED, 0x630u))
    return 1;
  if (audit_structural_loop("fixed_tail", fix_tail, 17u, 0, CGBA_SH4_M4A_KIND_FIXED_TAIL, 0x310u))
    return 1;
  if (audit_structural_loop("interp_a", interp_a, 29u, 1, CGBA_SH4_M4A_KIND_INTERP_A, 0x630u))
    return 1;
  if (audit_structural_loop("interp_b", interp_b, 29u, 1, CGBA_SH4_M4A_KIND_INTERP_B, 0x630u))
    return 1;
  if (audit_structural_loop("mono_reverb", mono_rev, 12u, 0, CGBA_SH4_M4A_KIND_MONO_REVERB, 0u))
    return 1;
  if (audit_structural_loop("mono_fixed", mono_fix, 10u, 0, CGBA_SH4_M4A_KIND_MONO_FIXED, 0u))
    return 1;
  if (audit_structural_loop("mono_fixed_tail", mono_fix_tail, 12u, 0, CGBA_SH4_M4A_KIND_MONO_FIXED_TAIL, 0u))
    return 1;
  if (audit_structural_loop("mono_interp_b", mono_interp_b, 24u, 1, CGBA_SH4_M4A_KIND_MONO_INTERP_B, 0u))
    return 1;
  if (audit_structural_loop("mono_interp_a", mono_interp_a, 24u, 1, CGBA_SH4_M4A_KIND_MONO_INTERP_A, 0u))
    return 1;

  /* Mono negative checks: truncated buffers and wrong shift count. */
  if (cgba_sh4_m4a_match_loop_words(mono_rev, 11u, 0x03000B04u, 0, &m) ||
      cgba_sh4_m4a_match_loop_words(mono_fix, 9u, 0x03000B04u, 0, &m) ||
      cgba_sh4_m4a_match_loop_words(mono_fix_tail, 11u, 0x03000B04u, 0, &m) ||
      cgba_sh4_m4a_match_loop_words(mono_interp_b, 23u, 0x03000B04u, 1, &m))
    FAIL("truncated mono loop buffer must be rejected");
  for (i = 0; i < 12u; i++) bad_mrev[i] = mono_rev[i];
  bad_mrev[4] = 0xE1A004C1u; /* stereo asr #9 instead of mono asr #8 */
  if (cgba_sh4_m4a_match_loop_words(bad_mrev, 12u, 0x03000B04u, 0, &m))
    FAIL("mono reverb with asr #9 must be rejected");

  /* Custom stride (0x310) on Flavor B + mismatched load/store stride rejection. */
  for (i = 0; i < 29u; i++) custom_b[i] = interp_b[i];
  custom_b[1]  = 0xE5957310u;
  custom_b[23] = 0xE5857310u;
  if (audit_structural_loop("interp_b_0x310", custom_b, 29u, 0,
                            CGBA_SH4_M4A_KIND_INTERP_B, 0x310u))
    return 1;
  custom_b[23] = 0xE5857630u;
  if (cgba_sh4_m4a_match_loop_words(custom_b, 29u, 0x03000DD0u, 1, &m))
    FAIL("mismatched load/store stride must be rejected");
  custom_b[1] = 0xE5957631u; custom_b[23] = 0xE5857631u;
  if (cgba_sh4_m4a_match_loop_words(custom_b, 29u, 0x03000DD0u, 1, &m))
    FAIL("unaligned stride must be rejected");

  /* Pre-header classification checks (stereo + mono). */
  if (!cgba_sh4_m4a_match_loop_words(interp_b, 29u, 0x03000DDCu, 1, &m))
    FAIL("interp_b pre-header match failed");
  if (cgba_sh4_m4a_match_class(&m, 0x03000DD0u, 0xE1D300D0u) != CGBA_SH4_M4A_SKIP_MEM ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD4u, 0xE1F310D1u) != CGBA_SH4_M4A_SKIP_MEM ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD8u, 0xE0411000u) != CGBA_SH4_M4A_SKIP_DROP ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD0u, 0xE1D300D1u) != CGBA_SH4_M4A_SKIP_KEEP)
    FAIL("pre-header classification mismatch");
  if (!cgba_sh4_m4a_match_loop_words(mono_interp_b, 24u, 0x03000DDCu, 1, &m))
    FAIL("mono_interp_b pre-header match failed");
  if (cgba_sh4_m4a_match_class(&m, 0x03000DD0u, 0xE1D300D0u) != CGBA_SH4_M4A_SKIP_MEM ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD4u, 0xE1F310D1u) != CGBA_SH4_M4A_SKIP_MEM ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD8u, 0xE0411000u) != CGBA_SH4_M4A_SKIP_DROP ||
      cgba_sh4_m4a_match_class(&m, 0x03000DD0u, 0xE1D300D1u) != CGBA_SH4_M4A_SKIP_KEEP)
    FAIL("mono pre-header classification mismatch");

  printf("m4a structural matcher: 10 loop forms (5 stereo + 5 mono) + corruption checks passed\n");
  return 0;
}

int main(void)
{
  if (test_folds())
    return 1;
  if (test_structural_matcher())
    return 1;

  unsigned i, drops = 0, mems = 0, flag_setters = 0;
  uint32_t previous = 0;

  for (i = 0; i < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    const cgba_sh4_m4a_skip_row *row = &cgba_sh4_m4a_skip_rows[i];
    uint32_t op = row->opcode;
    unsigned cond = op >> 28, rd = (op >> 12) & 15;

    row_offset = row->offset;
    if ((row->offset & 3) || row->offset >= CGBA_SH4_M4A_SKIP_BYTES)
      FAIL("row %u: bad offset %x", i, row->offset);
    if (i && row->offset <= previous)
      FAIL("row %u: offsets not strictly increasing", i);
    previous = row->offset;
    if (cond == 15)
      FAIL("row %u: unconditional-space opcode", i);
    if (cgba_sh4_m4a_skip_class(row->offset, op) != row->cls)
      FAIL("row %u: class lookup mismatch", i);
    if (cgba_sh4_m4a_skip_class(row->offset, op ^ 0x00010000u) !=
        CGBA_SH4_M4A_SKIP_KEEP)
      FAIL("row %u: a different opcode at this offset was not kept", i);

    if (row->cls == CGBA_SH4_M4A_SKIP_DROP) {
      drops++;
      if ((op & 0x0FC000F0u) == 0x00000090u) {            /* MUL / MLA */
        unsigned mrd = (op >> 16) & 15;
        if (op & 0x00100000u)
          FAIL("row %u: flag-setting multiply", i);
        if (!dead_reg(mrd))
          FAIL("row %u: multiply writes live r%u", i, mrd);
      } else if ((op & 0x0C000000u) == 0) {               /* data processing */
        unsigned opc = (op >> 21) & 15;
        if ((op & 0x02000000u) == 0 && (op & 0x90u) == 0x90u)
          FAIL("row %u: not a data-processing opcode", i);
        if (opc >= 8 && opc <= 11) {                      /* TST/TEQ/CMP/CMN */
          flag_setters++;
        } else {
          if (op & 0x00100000u)
            FAIL("row %u: flag-setting ALU op", i);
          if (!dead_reg(rd))
            FAIL("row %u: ALU op writes live r%u", i, rd);
        }
      } else {
        FAIL("row %u: DROP row is not ALU/MUL", i);
      }
    } else if (row->cls == CGBA_SH4_M4A_SKIP_MEM) {
      cgba_sh4_m4a_skip_mem m;
      mems++;
      if (!cgba_sh4_m4a_skip_mem_decode(op, &m))
        FAIL("row %u: MEM row does not decode", i);
      if (rd == 15)
        FAIL("row %u: PC transfer", i);
      if ((op & 0x00100000u) && !dead_reg(rd))
        FAIL("row %u: load targets live r%u", i, rd);
      /* Writeback bases must stay exact, so they may not also be elided
       * destinations of the same instruction. */
      if ((op & 0x00100000u) && m.writeback && rd == m.rn)
        FAIL("row %u: load writes back into its own destination", i);
      if (dead_reg(m.rn))
        FAIL("row %u: base r%u is a scratch register", i, m.rn);
    } else {
      FAIL("row %u: unknown class", i);
    }
  }

  /* Exactly one dropped flag setter (reverb TST), immediately followed by the
   * dropped conditional that is its only reader. */
  if (flag_setters != 1)
    FAIL("%u dropped flag setters, expected 1", flag_setters);
  for (i = 0; i + 1 < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    const cgba_sh4_m4a_skip_row *row = &cgba_sh4_m4a_skip_rows[i];
    if (row->opcode == 0xE3100080u) {
      if (row[1].offset != row->offset + 4u ||
          row[1].cls != CGBA_SH4_M4A_SKIP_DROP || (row[1].opcode >> 28) != 1)
        FAIL("TST is not followed by its dropped ADDNE");
    }
  }

  /* Addressing decode spot checks. */
  {
    cgba_sh4_m4a_skip_mem m;
    if (!cgba_sh4_m4a_skip_mem_decode(0xE5957630u, &m) || m.rn != 5 ||
        !m.pre || !m.up || m.writeback || m.reg_offset || !m.is_word ||
        m.offset != 0x630)
      FAIL("decode ldr r7,[r5,#0x630]");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE4856004u, &m) || m.rn != 5 ||
        m.pre || !m.up || !m.writeback || !m.is_word || m.offset != 4)
      FAIL("decode str r6,[r5],#4");
    if (!cgba_sh4_m4a_skip_mem_decode(0x11B300DEu, &m) || m.rn != 3 ||
        !m.pre || !m.up || !m.writeback || !m.reg_offset || m.rm != 14 ||
        m.is_word)
      FAIL("decode ldrsbne r0,[r3,lr]!");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE0D300D1u, &m) || m.rn != 3 ||
        m.pre || !m.writeback || m.reg_offset || m.offset != 1 || m.is_word)
      FAIL("decode ldrsb r0,[r3],#1");
    if (!cgba_sh4_m4a_skip_mem_decode(0xE7C50006u, &m) || m.rn != 5 ||
        !m.pre || m.writeback || !m.reg_offset || m.rm != 6 || m.is_word)
      FAIL("decode strb r0,[r5,r6]");
    if (cgba_sh4_m4a_skip_mem_decode(0xE7950106u, &m))
      FAIL("shifted register offset must be rejected");
    if (cgba_sh4_m4a_skip_mem_decode(0xE59F0004u, &m))
      FAIL("PC-relative load must be rejected");
    if (cgba_sh4_m4a_skip_mem_decode(0xE0010390u, &m))
      FAIL("MUL must not decode as a transfer");
  }

  if (cgba_sh4_m4a_skip_class(CGBA_SH4_M4A_SKIP_BYTES, 0xE0800001u) ||
      cgba_sh4_m4a_skip_class(0x01Au, 0xE0800001u) ||
      cgba_sh4_m4a_skip_class(0x000u, CGBA_SH4_M4A_SKIP_FIRST_WORD))
    FAIL("out-of-table offsets must be kept");

  printf("m4a skip table: %u rows audited (%u drop, %u mem)\n",
         CGBA_SH4_M4A_SKIP_ROWS, drops, mems);
  return 0;
}
