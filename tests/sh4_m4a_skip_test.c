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

/* Runs `groups` whole four-sample groups from the group head.  Returns 0 and
 * sets *stopped if a group would leave the fold's domain (wave end). */
static uint32_t ref_interp(uint32_t groups, cgba_sh4_m4a_interp_state *s,
                           int *stopped)
{
  uint32_t cyc = 0;
  *stopped = 0;
  while (groups--) {
    I; M(s->r5, 1);               /* ldr r6,[r5]            */
    I; M(s->r5 + 0x630, 1);       /* ldr r7,[r5,#0x630]     */
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
    I; M(s->r5 + 0x630, 1);       /* str r7,[r5,#0x630]     */
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
  cgba_sh4_m4a_loop_costs costs;
  unsigned long folded = 0, declined = 0;
  unsigned n;

  for (n = 0; n < 200000; n++) {
    unsigned i;
    int32_t budget = (int32_t)(rnd() % 3000) - 100;
    for (i = 0; i < 16; i++) {
      mem_cost[i][0] = (uint8_t)(1 + rnd() % 9);
      mem_cost[i][1] = (uint8_t)(1 + rnd() % 9);
    }
    SEQ = 1 + rnd() % 3; BR = 1 + rnd() % 5;
    costs.seq = SEQ; costs.branch = BR;
    costs.mem = (const uint8_t (*)[2])mem_cost;

    {                                                   /* reverb */
      uint32_t r4 = (n % 7 == 0) ? rnd() % 3 : rnd() % 300;
      uint32_t r5 = 0x03000000u + (rnd() & 0x7FFF), r6 = 0x630;
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
      spent = cgba_sh4_m4a_fold_interp(&a, budget, &costs);
      if (!spent) {
        declined++;
        if (a.r2 != s.r2 || a.r3 != s.r3 || a.r5 != s.r5 || a.r8 != s.r8 ||
            a.r9 != s.r9 || a.lr != s.lr || a.cpsr != s.cpsr)
          FAIL("interp: declined fold changed state");
      } else {
        groups = (s.r8 - a.r8) / 4;
        folded++;
        want = ref_interp(groups, &r, &stopped);
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
  }
  if (!folded || !declined)
    FAIL("fold test did not cover both outcomes");
  printf("m4a loop fold: %lu folds matched the instruction-order reference "
         "(%lu declined)\n", folded, declined);
  return 0;
}

int main(void)
{
  if (test_folds())
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
