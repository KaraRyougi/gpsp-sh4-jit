#ifndef CGBA_SH4_M4A_SKIP_H
#define CGBA_SH4_M4A_SKIP_H

/*
 * Translation-time elision of discarded PCM work in the m4a/MP2K mixer.
 *
 * The calculator port has no sound backend: the direct-sound FIFOs go nowhere,
 * so the only consumer of the driver's PCM mix buffer is the driver itself
 * (reverb feedback).  The game still runs `SoundMainRAM` from IWRAM every
 * frame, and with reverb plus four interpolated channels that routine alone
 * executes ~17k guest instructions per frame.
 *
 * This is NOT a helper that replaces the routine.  The routine keeps running
 * instruction by instruction through the ordinary translator; selected
 * instructions simply emit less host code:
 *
 *   DROP  pure arithmetic whose only purpose is to produce a PCM sample
 *         (MUL/BIC/ADD-ROR accumulation, reverb averaging).  Nothing is
 *         emitted.  The translate-time cycle count still includes it.
 *   MEM   a sample/PCM-buffer load or store.  The transfer is not performed;
 *         base-register writeback and the runtime memory cycle charge (by the
 *         address's region, same table as the real access) are still emitted.
 *
 * Every branch, flag-setting loop counter, budget gate, and every store to
 * the SoundChannel structures (status, count, fw, current) is left alone, so
 * guest control flow, guest cycle accounting, event-slice boundaries, envelope
 * progress and sample-end detection are unchanged by construction.  What
 * changes: the PCM buffer is no longer written by translated code, and the
 * scratch registers below hold stale values while inside the routine.
 *
 * Liveness (audited against the disassembly; `tests/sh4_m4a_skip_test.c`
 * checks every table row decodes to the form this argument relies on):
 *   r0, r1   sample / delta temporaries.  Every live read is preceded by a
 *            kept write (loop-length reload, envelope/ROR setup, ADR + BX).
 *   r6, r7   PCM accumulators.  Only read by dropped arithmetic and by PCM
 *            buffer stores.
 *   ip, lr   interpolation temporaries.  ip is saved/restored around the
 *            interpolating path by the routine's own PUSH/POP {r4, ip}; the
 *            elided writes leave it holding the value POP restores anyway.
 *            lr is rewritten by the kept `LSRS lr, r9, #23` before each use.
 *   flags    the single dropped flag setter (`TST r0, #0x80`, reverb) feeds
 *            only the dropped `ADDNE`; `SUBS r4, r4, #1` rewrites NZCV before
 *            the loop branch reads them.
 *
 * The signature is the exact 227-word image [reverb entry, mix exit) of the
 * Pokemon Emerald (BPEE) build of the driver.  The compressed/reversed sample
 * paths after it are untouched.  Other MP2K builds do not match and translate
 * normally.
 *
 * This header has no target dependencies so the table can be audited on the
 * host.
 */

#include <stdint.h>

#define CGBA_SH4_M4A_SKIP_WORDS      227u
#define CGBA_SH4_M4A_SKIP_BYTES      (CGBA_SH4_M4A_SKIP_WORDS * 4u)
#define CGBA_SH4_M4A_SKIP_HASH       0x5583F4BAu
#define CGBA_SH4_M4A_SKIP_FIRST_WORD 0xE3540002u  /* cmp r4, #2 (reverb entry) */
/* Dispatcher-visible ARM entries (reached by BX from the Thumb half). */
#define CGBA_SH4_M4A_SKIP_ENTRY_REVERB 0x000u
#define CGBA_SH4_M4A_SKIP_ENTRY_MIX    0x190u

enum {
  CGBA_SH4_M4A_SKIP_KEEP = 0,
  CGBA_SH4_M4A_SKIP_DROP = 1,
  CGBA_SH4_M4A_SKIP_MEM  = 2
};

typedef struct cgba_sh4_m4a_skip_row {
  uint16_t offset;      /* byte offset from the reverb entry */
  uint32_t opcode;
  uint8_t  cls;
} cgba_sh4_m4a_skip_row;

static const cgba_sh4_m4a_skip_row cgba_sh4_m4a_skip_rows[] = {
  /* reverb: 4-tap average of both channels, written back to both */
  { 0x010u, 0xE19500D6u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r5,r6]     */
  { 0x014u, 0xE1D510D0u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r5]        */
  { 0x018u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP }, /* add r0,r0,r1         */
  { 0x01Cu, 0xE19710D6u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r7,r6]     */
  { 0x020u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x024u, 0xE0D710D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r7],#1     */
  { 0x028u, 0xE0800001u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x02Cu, 0xE0010390u, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,r0,r3         */
  { 0x030u, 0xE1A004C1u, CGBA_SH4_M4A_SKIP_DROP }, /* asr r0,r1,#9         */
  { 0x034u, 0xE3100080u, CGBA_SH4_M4A_SKIP_DROP }, /* tst r0,#0x80         */
  { 0x038u, 0x12800001u, CGBA_SH4_M4A_SKIP_DROP }, /* addne r0,r0,#1       */
  { 0x03Cu, 0xE7C50006u, CGBA_SH4_M4A_SKIP_MEM  }, /* strb r0,[r5,r6]      */
  { 0x040u, 0xE4C50001u, CGBA_SH4_M4A_SKIP_MEM  }, /* strb r0,[r5],#1      */
  /* fixed-rate mix, four-sample groups */
  { 0x1F4u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldr r6,[r5]          */
  { 0x1F8u, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldr r7,[r5,#0x630]   */
  { 0x1FCu, 0xE0D300D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r3],#1     */
  { 0x200u, 0xE001009Au, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,sl,r0         */
  { 0x204u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP }, /* bic r1,r1,#0xff0000  */
  { 0x208u, 0xE0816466u, CGBA_SH4_M4A_SKIP_DROP }, /* add r6,r1,r6,ror #8  */
  { 0x20Cu, 0xE001009Bu, CGBA_SH4_M4A_SKIP_DROP }, /* mul r1,fp,r0         */
  { 0x210u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x214u, 0xE0817467u, CGBA_SH4_M4A_SKIP_DROP }, /* add r7,r1,r7,ror #8  */
  { 0x220u, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  }, /* str r7,[r5,#0x630]   */
  { 0x224u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  }, /* str r6,[r5],#4       */
  /* fixed-rate mix, tail / loop-point groups */
  { 0x238u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x23Cu, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x240u, 0xE0D300D1u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x244u, 0xE001009Au, CGBA_SH4_M4A_SKIP_DROP },
  { 0x248u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x24Cu, 0xE0816466u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x250u, 0xE001009Bu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x254u, 0xE3C118FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x258u, 0xE0817467u, CGBA_SH4_M4A_SKIP_DROP },
  { 0x26Cu, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x270u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  },
  /* interpolating mix */
  { 0x2F4u, 0xE1D300D0u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r0,[r3]        */
  { 0x2F8u, 0xE1F310D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r3,#1]!    */
  { 0x2FCu, 0xE0411000u, CGBA_SH4_M4A_SKIP_DROP }, /* sub r1,r1,r0         */
  { 0x300u, 0xE5956000u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x304u, 0xE5957630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x308u, 0xE00E0199u, CGBA_SH4_M4A_SKIP_DROP }, /* mul lr,r9,r1         */
  { 0x30Cu, 0xE080EBCEu, CGBA_SH4_M4A_SKIP_DROP }, /* add lr,r0,lr,asr #23 */
  { 0x310u, 0xE00C0E9Au, CGBA_SH4_M4A_SKIP_DROP }, /* mul ip,sl,lr         */
  { 0x314u, 0xE3CCC8FFu, CGBA_SH4_M4A_SKIP_DROP }, /* bic ip,ip,#0xff0000  */
  { 0x318u, 0xE08C6466u, CGBA_SH4_M4A_SKIP_DROP }, /* add r6,ip,r6,ror #8  */
  { 0x31Cu, 0xE00C0E9Bu, CGBA_SH4_M4A_SKIP_DROP }, /* mul ip,fp,lr         */
  { 0x320u, 0xE3CCC8FFu, CGBA_SH4_M4A_SKIP_DROP },
  { 0x324u, 0xE08C7467u, CGBA_SH4_M4A_SKIP_DROP }, /* add r7,ip,r7,ror #8  */
  { 0x344u, 0x00800001u, CGBA_SH4_M4A_SKIP_DROP }, /* addeq r0,r0,r1       */
  { 0x348u, 0x11B300DEu, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsbne r0,[r3,lr]!  */
  { 0x34Cu, 0xE1F310D1u, CGBA_SH4_M4A_SKIP_MEM  }, /* ldrsb r1,[r3,#1]!    */
  { 0x350u, 0xE0411000u, CGBA_SH4_M4A_SKIP_DROP }, /* sub r1,r1,r0         */
  { 0x35Cu, 0xE5857630u, CGBA_SH4_M4A_SKIP_MEM  },
  { 0x360u, 0xE4856004u, CGBA_SH4_M4A_SKIP_MEM  },
};

#define CGBA_SH4_M4A_SKIP_ROWS \
  ((unsigned)(sizeof(cgba_sh4_m4a_skip_rows) / sizeof(cgba_sh4_m4a_skip_rows[0])))

/* FNV-1a over the little-endian bytes of each word. */
#define CGBA_SH4_M4A_SKIP_HASH_INIT 0x811C9DC5u
static inline uint32_t cgba_sh4_m4a_skip_hash_word(uint32_t hash, uint32_t word)
{
  unsigned k;
  for (k = 0; k < 4u; k++) {
    hash ^= (word >> (8u * k)) & 0xFFu;
    hash *= 0x01000193u;
  }
  return hash;
}

/* Class of the instruction at `offset` inside a verified image.  The opcode
 * is re-checked so a row can never apply to a different instruction. */
static inline unsigned cgba_sh4_m4a_skip_class(uint32_t offset, uint32_t opcode)
{
  unsigned i;
  if (offset >= CGBA_SH4_M4A_SKIP_BYTES || (offset & 3u))
    return CGBA_SH4_M4A_SKIP_KEEP;
  for (i = 0; i < CGBA_SH4_M4A_SKIP_ROWS; i++) {
    if (cgba_sh4_m4a_skip_rows[i].offset == offset)
      return cgba_sh4_m4a_skip_rows[i].opcode == opcode
        ? cgba_sh4_m4a_skip_rows[i].cls : CGBA_SH4_M4A_SKIP_KEEP;
  }
  return CGBA_SH4_M4A_SKIP_KEEP;
}

/* Decoded addressing of a MEM row: single transfer, immediate or plain
 * register offset.  Returns 0 for any form the elision emitter does not
 * implement (the caller then translates the instruction normally). */
typedef struct cgba_sh4_m4a_skip_mem {
  uint8_t rn, rm;
  uint8_t pre, up, writeback, reg_offset, is_word;
  uint32_t offset;
} cgba_sh4_m4a_skip_mem;

static inline int cgba_sh4_m4a_skip_mem_decode(uint32_t opcode,
                                               cgba_sh4_m4a_skip_mem *m)
{
  m->pre = (opcode >> 24) & 1u;
  m->up = (opcode >> 23) & 1u;
  m->writeback = ((opcode >> 21) & 1u) || !m->pre;
  m->rn = (opcode >> 16) & 0xFu;
  m->rm = 0;
  m->reg_offset = 0;
  m->offset = 0;
  if ((opcode & 0x0E000090u) == 0x00000090u) {      /* halfword / signed form */
    if (!(opcode & 0x60u))
      return 0;                                      /* MUL / SWP space */
    if (opcode & 0x00400000u)
      m->offset = ((opcode >> 4) & 0xF0u) | (opcode & 0xFu);
    else {
      m->reg_offset = 1;
      m->rm = opcode & 0xFu;
    }
    m->is_word = 0;
  } else if ((opcode & 0x0C000000u) == 0x04000000u) { /* word / byte form */
    if (opcode & 0x02000000u) {
      if (opcode & 0xFF0u)
        return 0;                                    /* shifted offset */
      m->reg_offset = 1;
      m->rm = opcode & 0xFu;
    } else {
      m->offset = opcode & 0xFFFu;
    }
    m->is_word = !((opcode >> 22) & 1u);
  } else {
    return 0;
  }
  /* r0 is pinned in a host register and r15 needs pipeline arithmetic;
   * neither occurs as a base or index in the mixer. */
  if (m->rn == 0 || m->rn == 15 || (m->reg_offset && (m->rm == 0 || m->rm == 15)))
    return 0;
  return 1;
}

/* ------------------------------------------------------------------------
 * Loop models (CGBA_SH4_M4A_LOOP_FOLD)
 *
 * With the PCM work elided, the two hot loops are counters and pointer
 * bumps.  These models advance them by whole iterations in C.  They are
 * called from a prefix emitted at the loop head, and the translated loop
 * still follows the prefix, so:
 *   - an iteration is folded only if the cycle budget stays positive after
 *     it.  The budget only decreases inside an iteration and every gate in
 *     the translated code fires on budget <= 0, so a folded iteration is one
 *     in which no gate would have fired.  The iteration that exhausts the
 *     budget is always left to the translated code, which exits normally;
 *   - loop exits, sample ends and loop points are never folded either.
 * The cycles charged are the interpreter's for the same path: one sequential
 * fetch per instruction on the path (elided ones included), one
 * nonsequential refill per taken branch, and one nonsequential access per
 * transfer priced by its address region.  All flags the loop head expects
 * come from the last SUBS and are recomputed exactly.
 * ------------------------------------------------------------------------ */

#define CGBA_SH4_M4A_LOOP_REVERB 0x010u   /* reverb loop head            */
#define CGBA_SH4_M4A_LOOP_INTERP 0x300u   /* interpolating group head    */

typedef struct cgba_sh4_m4a_loop_costs {
  uint32_t seq;                 /* ws_cyc_seq[code region][word]  */
  uint32_t branch;              /* ws_cyc_nseq[code region][word] */
  const uint8_t (*mem)[2];      /* ws_cyc_nseq[16][2]             */
} cgba_sh4_m4a_loop_costs;

static inline uint32_t cgba_sh4_m4a_subs_flags(uint32_t a, uint32_t b)
{
  uint32_t r = a - b;
  return (r & 0x80000000u) | (r == 0 ? 0x40000000u : 0u) |
         (a >= b ? 0x20000000u : 0u) |
         ((((a ^ b) & (a ^ r)) >> 31) << 28);
}

/* Reverb loop, one sample per iteration (15 instructions, 6 byte transfers):
 *   r4 = samples left, r5/r7 = buffer cursors, r6 = constant channel offset.
 * Returns the cycles spent; 0 means nothing was folded and nothing changed. */
static inline uint32_t cgba_sh4_m4a_fold_reverb(
  uint32_t *r4, uint32_t *r5, uint32_t r6, uint32_t *r7, uint32_t *cpsr,
  int32_t budget, const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t n4 = *r4, n5 = *r5, n7 = *r7, spent = 0, last = 0;
  uint32_t rem, base_cost;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)n4 <= 1)
    return 0;
  rem = (uint32_t)(budget - 1);       /* strictly positive remaining budget */
  base_cost = 15u * c->seq + c->branch;

  while ((int32_t)n4 > 1) {           /* keep the final (exiting) iteration */
    uint32_t a0 = n5 + r6, a2 = n7 + r6, cost;
    uint32_t max_k, by_budget, room, r1, r2, r3, step_cost;
    if ((a0 >> 28) | (n5 >> 28) | (a2 >> 28) | (n7 >> 28))
      break;                          /* outside the charge table */
    cost = base_cost +
           2u * mem[a0 >> 24][0] + 2u * mem[n5 >> 24][0] +
           mem[a2 >> 24][0] + mem[n7 >> 24][0];
    if (cost > rem)
      break;
    /* All four cursors increment by 1/iter, so cost stays constant until one
     * crosses a 16 MiB region boundary. Fold the whole uniform run in O(1). */
    max_k = n4 - 1u;
    by_budget = rem / cost;
    if (by_budget < max_k)
      max_k = by_budget;
    room = 0x01000000u - (a0 & 0x00FFFFFFu);
    r1 = 0x01000000u - (n5 & 0x00FFFFFFu);
    r2 = 0x01000000u - (a2 & 0x00FFFFFFu);
    r3 = 0x01000000u - (n7 & 0x00FFFFFFu);
    if (r1 < room) room = r1;
    if (r2 < room) room = r2;
    if (r3 < room) room = r3;
    if (room < max_k)
      max_k = room;
    step_cost = max_k * cost;
    spent += step_cost;
    rem -= step_cost;
    last = n4 - max_k + 1u;
    n4 -= max_k;
    n5 += max_k;
    n7 += max_k;
  }
  if (!spent)
    return 0;
  *r4 = n4; *r5 = n5; *r7 = n7;
  *cpsr = (*cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(last, 1u);
  return spent;
}

/* Interpolating mix, four samples per group:
 *   r2 = samples left in the wave, r3 = wave cursor, r4 = phase step,
 *   r5 = buffer cursor (sample-in-group counter in its top two bits),
 *   r8 = output samples left, r9 = phase, lr = last integer step. */
typedef struct cgba_sh4_m4a_interp_state {
  uint32_t r2, r3, r4, r5, r8, r9, lr, cpsr;
} cgba_sh4_m4a_interp_state;

static inline uint32_t cgba_sh4_m4a_fold_interp(
  cgba_sh4_m4a_interp_state *s, int32_t budget,
  const cgba_sh4_m4a_loop_costs *c)
{
  uint32_t spent = 0, rem;
  uint32_t r2 = s->r2, r3 = s->r3, r4 = s->r4, r5 = s->r5;
  uint32_t r8 = s->r8, r9 = s->r9, lr = s->lr;
  uint32_t seq = c->seq, br = c->branch;
  uint32_t fixed_cost = 58u * seq + 4u * br;
  uint32_t step_seq = 8u * seq;
  const uint8_t (*mem)[2] = c->mem;

  if (budget <= 1 || (int32_t)r8 <= 4)
    return 0;
  rem = (uint32_t)(budget - 1);       /* strictly positive remaining budget */

  /* Cache region-derived wait-state costs across groups: in practice r5 stays
   * in IWRAM (0x03) and r3 stays in ROM/RAM for the entire mix call. */
  uint32_t r5_reg = 0xFFFFFFFFu, r5_630_reg = 0xFFFFFFFFu;
  uint32_t r5_cost = 0;
  uint32_t r3_reg = r3 >> 24;
  uint32_t r3_mem = (r3_reg < 16u) ? mem[r3_reg][0] : 0u;

  /* O(1) uniform-region batch fast path: fold up to 63 groups (252 samples)
   * at a time using pure 32-bit arithmetic (4*63*f < 2^31).  The exact 1-group
   * tail loop below finishes the final 0..2 groups and any edge cases. */
  if ((int32_t)r2 > 0 && r9 < 0x00800000u && r4 < 0x3F800000u &&
      (r3 >> 28) == 0u) {
    uint32_t q = r4 >> 23;
    uint32_t f = r4 & 0x007FFFFFu;
    uint32_t step1 = step_seq + r3_mem;
    uint32_t step2 = step1 + r3_mem;
    for (;;) {
      uint32_t r5_b = r5 + 0x630u;
      uint32_t k, k_bud, k_r5, k_r5b, max_gcost, n_samp, phase_sum, carry;
      uint32_t total_steps, max_steps, r3_room, batch_cost;
      if ((r5 >> 28) | (r5_b >> 28) || (int32_t)r8 <= 4)
        break;
      if ((r5 >> 24) != r5_reg || (r5_b >> 24) != r5_630_reg) {
        r5_reg = r5 >> 24;
        r5_630_reg = r5_b >> 24;
        r5_cost = fixed_cost + 2u * mem[r5_reg][1] + 2u * mem[r5_630_reg][1];
      }
      if (q == 0u)
        max_gcost = r5_cost + 4u * (step1 > br ? step1 : br);
      else
        max_gcost = r5_cost + 4u * step2;
      k_bud = rem / max_gcost;
      if (k_bud == 0u)
        break;
      k = (r8 - 1u) >> 2;
      if (k > 63u) k = 63u;
      if (k_bud < k) k = k_bud;
      k_r5 = (0x01000000u - (r5 & 0x00FFFFFFu)) >> 2;
      k_r5b = (0x01000000u - (r5_b & 0x00FFFFFFu)) >> 2;
      if (k_r5 < k) k = k_r5;
      if (k_r5b < k) k = k_r5b;
      if (k == 0u)
        break;
      r3_room = 0x00FFFFFFu - (r3 & 0x00FFFFFFu);
      max_steps = r2 - 1u;
      if (r3_room < max_steps) max_steps = r3_room;
      n_samp = k << 2;
      phase_sum = r9 + n_samp * f;
      carry = phase_sum >> 23;
      total_steps = n_samp * q + carry;
      if (total_steps > max_steps) {
        k = max_steps / ((q + 1u) << 2);
        if (k == 0u)
          break;
        n_samp = k << 2;
        phase_sum = r9 + n_samp * f;
        carry = phase_sum >> 23;
        total_steps = n_samp * q + carry;
      }
      r9 = phase_sum & 0x007FFFFFu;
      if (q == 0u) {
        batch_cost = k * r5_cost + (n_samp - carry) * br + carry * step1;
        lr = 0u;
      } else if (q == 1u) {
        batch_cost = k * (r5_cost + 4u * step1) + carry * r3_mem;
        lr = (r9 < f) ? 1u : 0u;
      } else {
        batch_cost = k * (r5_cost + 4u * step2);
        lr = (q - 1u) + ((r9 < f) ? 1u : 0u);
      }
      spent += batch_cost;
      rem -= batch_cost;
      r2 -= total_steps;
      r3 += total_steps;
      r5 += n_samp;
      r8 -= n_samp;
    }
  }

  for (;;) {
    uint32_t n2 = r2, n3 = r3, n9 = r9, nlr = lr;
    uint32_t r5_b, cost, i;

    /* Group head: top bits clear, and the group must not be the last one
     * (SUBS r8, r8, #4 ; BGT must be taken). */
    r5_b = r5 + 0x630u;
    if ((r5 >> 28) | (r5_b >> 28) || (int32_t)r8 <= 4)
      break;
    if ((r5 >> 24) != r5_reg || (r5_b >> 24) != r5_630_reg) {
      r5_reg = r5 >> 24;
      r5_630_reg = r5_b >> 24;
      r5_cost = fixed_cost + 2u * mem[r5_reg][1] + 2u * mem[r5_630_reg][1];
    }
    cost = r5_cost;
    for (i = 0; i < 4u; i++) {
      n9 += r4;
      nlr = n9 >> 23;
      if (nlr == 0u) {
        cost += br;                              /* BEQ over the step code */
      } else {
        n9 &= ~0x3F800000u;
        if ((int32_t)n2 <= (int32_t)nlr)
          goto done;                             /* wave end / loop point */
        n2 -= nlr;
        cost += step_seq;
        nlr -= 1u;
        if (nlr != 0u) {
          n3 += nlr;
          if (n3 >> 28)
            goto done;
          if (__builtin_expect((n3 >> 24) != r3_reg, 0)) {
            r3_reg = n3 >> 24;
            r3_mem = mem[r3_reg][0];
          }
          cost += r3_mem;
        }
        n3 += 1u;
        if (n3 >> 28)
          goto done;
        if (__builtin_expect((n3 >> 24) != r3_reg, 0)) {
          r3_reg = n3 >> 24;
          r3_mem = mem[r3_reg][0];
        }
        cost += r3_mem;
      }
    }
    if (cost > rem)
      break;
    spent += cost;
    rem -= cost;
    r2 = n2; r3 = n3; r5 += 4u; r9 = n9; lr = nlr;
    r8 -= 4u;
  }
done:
  if (spent) {
    s->cpsr = (s->cpsr & 0x0FFFFFFFu) | cgba_sh4_m4a_subs_flags(r8 + 4u, 4u);
    s->r2 = r2; s->r3 = r3; s->r5 = r5; s->r8 = r8; s->r9 = r9; s->lr = lr;
  }
  return spent;
}

#endif /* CGBA_SH4_M4A_SKIP_H */
