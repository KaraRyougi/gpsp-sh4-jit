#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_thumb_udiv.h"

typedef struct ref_state {
  uint32_t r[5];
  uint32_t cpsr;
  uint32_t seq;
  uint32_t branches;
} ref_state;

enum {
  FN = 1u << 31,
  FZ = 1u << 30,
  FC = 1u << 29,
  FV = 1u << 28
};

static void ref_set_nz(ref_state *s, uint32_t v)
{
  s->cpsr &= ~(FN | FZ);
  if (v >> 31) s->cpsr |= FN;
  if (v == 0) s->cpsr |= FZ;
}

static void ref_cmp(ref_state *s, uint32_t a, uint32_t b)
{
  uint32_t v = a - b;
  s->cpsr &= ~(FN | FZ | FC | FV);
  if (v >> 31) s->cpsr |= FN;
  if (v == 0) s->cpsr |= FZ;
  if (a >= b) s->cpsr |= FC;
  if (((a ^ b) & (a ^ v)) >> 31) s->cpsr |= FV;
}

static void ref_lsr(ref_state *s, unsigned rd, unsigned rs, unsigned amount)
{
  uint32_t v = s->r[rs];
  uint32_t c = (v >> (amount - 1)) & 1u;
  s->r[rd] = v >> amount;
  ref_set_nz(s, s->r[rd]);
  s->cpsr = (s->cpsr & ~FC) | (c ? FC : 0u);
}

static ref_state run_reference(uint32_t r0, uint32_t r1, uint32_t r2,
                               uint32_t r3, uint32_t r4, uint32_t cpsr)
{
  ref_state s = {{r0, r1, r2, r3, r4}, cpsr, 0, 0};
  int pc = 0;
  unsigned guard = 0;

  while (pc != (int)CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS) {
    uint16_t op;
    int next = pc + 1;
    if (pc < 0 || pc >= (int)CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS ||
        ++guard > 512) {
      fprintf(stderr, "reference escaped: pc=%d guard=%u\n", pc, guard);
      s.seq = UINT32_MAX;
      return s;
    }
    op = cgba_sh4_thumb_udiv_loop_pattern[pc];
    s.seq++;

    switch (op) {
    case 0x4288: ref_cmp(&s, s.r[0], s.r[1]); break;
    case 0x42A0: ref_cmp(&s, s.r[0], s.r[4]); break;
    case 0x1A40: {
      uint32_t a = s.r[0], b = s.r[1];
      s.r[0] = a - b; ref_cmp(&s, a, b); break;
    }
    case 0x1B00: {
      uint32_t a = s.r[0], b = s.r[4];
      s.r[0] = a - b; ref_cmp(&s, a, b); break;
    }
    case 0x431A: s.r[2] |= s.r[3]; ref_set_nz(&s, s.r[2]); break;
    case 0x4322: s.r[2] |= s.r[4]; ref_set_nz(&s, s.r[2]); break;
    case 0x084C: ref_lsr(&s, 4, 1, 1); break;
    case 0x085C: ref_lsr(&s, 4, 3, 1); break;
    case 0x088C: ref_lsr(&s, 4, 1, 2); break;
    case 0x089C: ref_lsr(&s, 4, 3, 2); break;
    case 0x08CC: ref_lsr(&s, 4, 1, 3); break;
    case 0x08DC: ref_lsr(&s, 4, 3, 3); break;
    case 0x091B: ref_lsr(&s, 3, 3, 4); break;
    case 0x0909: ref_lsr(&s, 1, 1, 4); break;
    case 0x2800: ref_cmp(&s, s.r[0], 0); break;
    default:
      if ((op & 0xF000u) == 0xD000u) {
        int take;
        unsigned cc = (op >> 8) & 0xFu;
        int8_t off = (int8_t)(op & 0xFFu);
        if (cc == 0) take = (s.cpsr & FZ) != 0;       /* EQ */
        else if (cc == 3) take = (s.cpsr & FC) == 0;  /* CC */
        else {
          fprintf(stderr, "unexpected condition %u\n", cc);
          s.seq = UINT32_MAX;
          return s;
        }
        s.branches++;
        if (take) next = pc + 2 + off;
      } else if ((op & 0xF800u) == 0xE000u) {
        int32_t off = (int32_t)(op & 0x7FFu);
        if (off & 0x400) off -= 0x800;
        s.branches++;
        next = pc + 2 + off;
      } else {
        fprintf(stderr, "unexpected opcode %04x at %d\n", op, pc);
        s.seq = UINT32_MAX;
        return s;
      }
      break;
    }
    pc = next;
  }
  return s;
}

static uint32_t rng_state = 0x6d2b79f5u;
static uint32_t rng32(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

static int compare_case(uint32_t r0, uint32_t r1, uint32_t r2,
                        uint32_t r3, uint32_t r4, uint32_t cpsr,
                        uint32_t seq_cost, uint32_t nseq_cost)
{
  ref_state ref = run_reference(r0, r1, r2, r3, r4, cpsr);
  cgba_sh4_thumb_udiv_loop_result got =
    cgba_sh4_thumb_udiv_loop_run(r0, r1, r2, r3, r4, cpsr,
                                 seq_cost, nseq_cost);
  uint32_t ref_cycles = ref.seq * seq_cost + ref.branches * nseq_cost;

  if (got.r0 != ref.r[0] || got.r1 != ref.r[1] ||
      got.r2 != ref.r[2] || got.r3 != ref.r[3] || got.r4 != ref.r[4] ||
      got.cpsr != ref.cpsr || got.seq_insns != ref.seq ||
      got.branch_refills != ref.branches || got.cycles != ref_cycles) {
    fprintf(stderr,
      "mismatch in=%08x/%08x/%08x/%08x/%08x cpsr=%08x "
      "got=%08x/%08x/%08x/%08x/%08x c=%08x s=%u b=%u cy=%u "
      "ref=%08x/%08x/%08x/%08x/%08x c=%08x s=%u b=%u cy=%u\n",
      r0, r1, r2, r3, r4, cpsr,
      got.r0, got.r1, got.r2, got.r3, got.r4, got.cpsr,
      got.seq_insns, got.branch_refills, got.cycles,
      ref.r[0], ref.r[1], ref.r[2], ref.r[3], ref.r[4], ref.cpsr,
      ref.seq, ref.branches, ref_cycles);
    return 0;
  }
  return 1;
}

int main(void)
{
  uint16_t pattern[CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS];
  unsigned i;

  memcpy(pattern, cgba_sh4_thumb_udiv_loop_pattern, sizeof(pattern));
  if (!cgba_sh4_thumb_udiv_loop_match(pattern)) {
    fprintf(stderr, "canonical signature rejected\n");
    return 1;
  }
  pattern[13] ^= 1;
  if (cgba_sh4_thumb_udiv_loop_match(pattern)) {
    fprintf(stderr, "mutated signature accepted\n");
    return 1;
  }

  /* Edge cases plus arbitrary internal-loop states (including zero r1/r3). */
  if (!compare_case(0, 1, 0, 1, 0xdeadbeef, 0xA000001Fu, 3, 5) ||
      !compare_case(UINT32_MAX, 1, 0, 1, 7, 0x5000003Fu, 1, 2) ||
      !compare_case(17, 0, 9, 0, 11, 0xF123451Fu, 5, 9))
    return 1;

  for (i = 0; i < 200000; i++) {
    uint32_t seq_cost = 1u + (rng32() & 7u);
    uint32_t nseq_cost = 1u + (rng32() & 15u);
    if (!compare_case(rng32(), rng32(), rng32(), rng32(), rng32(),
                      rng32(), seq_cost, nseq_cost))
      return 1;
  }

  {
    cgba_sh4_thumb_udiv_loop_result r =
      cgba_sh4_thumb_udiv_loop_run(0xFEDCBA98u, 0x10000000u, 0, 0x10000000u,
                                   0, 0x20u, 3, 5);
    if (cgba_sh4_thumb_udiv_loop_budget_ok((int32_t)r.cycles, r.cycles) ||
        !cgba_sh4_thumb_udiv_loop_budget_ok((int32_t)r.cycles + 1, r.cycles) ||
        cgba_sh4_thumb_udiv_loop_budget_ok(-1, r.cycles)) {
      fprintf(stderr, "budget boundary mismatch\n");
      return 1;
    }
  }

  puts("sh4 udiv fastpath: signature/state/flags/cycles OK (200003 cases)");
  return 0;
}
