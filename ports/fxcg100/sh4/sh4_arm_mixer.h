#ifndef CGBA_SH4_ARM_MIXER_H
#define CGBA_SH4_ARM_MIXER_H

/*
 * Exact model for the four-sample ARM mixer batch copied into IWRAM by the
 * common m4a/Sappy sound driver.  The batch is deliberately stopped before
 * its CMP/BCC tail: those two instructions remain on the ordinary translated
 * path and therefore materialize the architectural flags and scheduler exit
 * exactly as before.
 *
 * One batch is four repetitions of:
 *   add r12,r0,r4,lsr #8; ldrsb r8,[r12]
 *   ldrsh r12,[r1]; mla r12,r6,r8,r12; strh r12,[r1],#2
 *   ldrsh r12,[r2]; mla r12,r7,r8,r12; strh r12,[r2],#2
 *   add r4,r4,r5
 * followed by `cmp r1,r3; bcc batch`.
 *
 * This header contains no target dependencies so the arithmetic, register,
 * signature and cycle contract can be differentially tested on the host.
 */

#include <stdint.h>

#define CGBA_SH4_ARM_MIXER_BODY_WORDS 36u
#define CGBA_SH4_ARM_MIXER_TAIL_WORDS 2u
#define CGBA_SH4_ARM_MIXER_WORDS \
  (CGBA_SH4_ARM_MIXER_BODY_WORDS + CGBA_SH4_ARM_MIXER_TAIL_WORDS)
#define CGBA_SH4_ARM_MIXER_BODY_BYTES \
  (CGBA_SH4_ARM_MIXER_BODY_WORDS * 4u)
#define CGBA_SH4_ARM_MIXER_BYTES (CGBA_SH4_ARM_MIXER_WORDS * 4u)

/* Bound the whole-call specialization so its output/tag proof stays tiny.
 * The normal 512-cycle scheduler slice admits at most eight of these batches;
 * returning at the untouched CMP after the cap is still architecturally exact
 * for a larger slice. */
#define CGBA_SH4_ARM_MIXER_WHOLE_MAX_BATCHES 8u

static const uint32_t cgba_sh4_arm_mixer_pattern[CGBA_SH4_ARM_MIXER_WORDS] = {
  0xE080C424u, 0xE1DC80D0u, 0xE1D1C0F0u,
  0xE02CC896u, 0xE0C1C0B2u, 0xE1D2C0F0u,
  0xE02CC897u, 0xE0C2C0B2u, 0xE0844005u,

  0xE080C424u, 0xE1DC80D0u, 0xE1D1C0F0u,
  0xE02CC896u, 0xE0C1C0B2u, 0xE1D2C0F0u,
  0xE02CC897u, 0xE0C2C0B2u, 0xE0844005u,

  0xE080C424u, 0xE1DC80D0u, 0xE1D1C0F0u,
  0xE02CC896u, 0xE0C1C0B2u, 0xE1D2C0F0u,
  0xE02CC897u, 0xE0C2C0B2u, 0xE0844005u,

  0xE080C424u, 0xE1DC80D0u, 0xE1D1C0F0u,
  0xE02CC896u, 0xE0C1C0B2u, 0xE1D2C0F0u,
  0xE02CC897u, 0xE0C2C0B2u, 0xE0844005u,

  0xE1510003u, 0x3AFFFFD9u
};

static inline int cgba_sh4_arm_mixer_match(const uint32_t *ops)
{
  uint32_t i;
  for (i = 0; i < CGBA_SH4_ARM_MIXER_WORDS; i++) {
    if (ops[i] != cgba_sh4_arm_mixer_pattern[i])
      return 0;
  }
  return 1;
}

typedef struct cgba_sh4_arm_mixer_state {
  uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r12;
  uint32_t cpsr;
} cgba_sh4_arm_mixer_state;

static inline uint32_t cgba_sh4_arm_mixer_source_address(
  const cgba_sh4_arm_mixer_state *s)
{
  return s->r0 + (s->r4 >> 8);
}

/* Execute one of the four identical nine-instruction sample steps in three
 * pieces so callers can preserve the exact load-left/store-left/load-right/
 * store-right ordering even when the two output ranges alias.  Unsigned
 * arithmetic gives the exact ARM low-word MLA result without signed-overflow
 * undefined behavior. */
static inline void cgba_sh4_arm_mixer_step_begin(
  cgba_sh4_arm_mixer_state *s, int8_t sample)
{
  uint32_t sample32 = (uint32_t)(int32_t)sample;

  s->r12 = cgba_sh4_arm_mixer_source_address(s);
  s->r8 = sample32;
}

static inline uint16_t cgba_sh4_arm_mixer_step_left(
  cgba_sh4_arm_mixer_state *s, int16_t left)
{
  s->r12 = s->r6 * s->r8 + (uint32_t)(int32_t)left;
  s->r1 += 2u;
  return (uint16_t)s->r12;
}

static inline uint16_t cgba_sh4_arm_mixer_step_right(
  cgba_sh4_arm_mixer_state *s, int16_t right)
{
  s->r12 = s->r7 * s->r8 + (uint32_t)(int32_t)right;
  s->r2 += 2u;
  s->r4 += s->r5;
  return (uint16_t)s->r12;
}

/* The SH4 backend has no extra MLA charge.  Each body instruction fetches
 * sequentially; the four LDRSB accesses use their runtime source regions and
 * the sixteen LDRSH/STRH accesses use the IWRAM halfword timing. */
static inline uint32_t cgba_sh4_arm_mixer_body_cycles(
  uint32_t seq_word_cost, const uint8_t source_nseq_cost[4],
  uint32_t output_nseq_cost)
{
  uint32_t cycles = CGBA_SH4_ARM_MIXER_BODY_WORDS * seq_word_cost;
  uint32_t i;
  for (i = 0; i < 4; i++)
    cycles += source_nseq_cost[i];
  return cycles + 16u * output_nseq_cost;
}

static inline uint32_t cgba_sh4_arm_mixer_taken_tail_cycles(
  uint32_t seq_word_cost, uint32_t branch_nseq_word_cost)
{
  return 2u * seq_word_cost + branch_nseq_word_cost;
}

/* A taken tail may be folded only when its debit leaves a strictly positive
 * R13, matching sh4g_branch_exit's CMP/PL gate. */
static inline int cgba_sh4_arm_mixer_can_fold_taken(int32_t budget,
                                                     uint32_t spent,
                                                     uint32_t tail_cycles)
{
  uint64_t need = (uint64_t)spent + tail_cycles;
  return budget > 0 && (uint64_t)(uint32_t)budget > need;
}

/* Plan a constant-cost, whole-call prefix.  The caller must subsequently
 * prove that every source stays in one mapping page, which is what makes
 * body_cycles constant.  Runtime waitstate costs are bytes, so the bounded
 * arithmetic below cannot overflow uint32_t.  A cap stop is allowed: the
 * ordinary CMP/BCC tail remains translated and can redispatch the loop. */
static inline uint32_t cgba_sh4_arm_mixer_plan_constant(
  uint32_t r1, uint32_t r3, int32_t budget, uint32_t body_cycles,
  uint32_t tail_cycles, uint32_t *spent_out, int *budget_stop_out)
{
  uint32_t batches = 1u;
  uint32_t spent = body_cycles;

  while (batches < CGBA_SH4_ARM_MIXER_WHOLE_MAX_BATCHES &&
         r1 + batches * 8u < r3) {
    if (budget <= 0 || (uint32_t)budget <= spent + tail_cycles)
      break;
    spent += tail_cycles + body_cycles;
    batches++;
  }

  *spent_out = spent;
  *budget_stop_out =
    r1 + batches * 8u < r3 && budget > 0 &&
    (uint32_t)budget <= spent + tail_cycles;
  return batches;
}

/* Conservative pure-memory guards used by the optimized helper.  Source
 * reads are accepted only from side-effect-free RAM/gamepak regions; output
 * reads/stores are accepted only as aligned IWRAM halfwords. */
static inline int cgba_sh4_arm_mixer_source_fast_ok(uint32_t address)
{
  uint32_t region = address >> 24;
  return region == 2u || region == 3u || (region >= 8u && region <= 12u);
}

static inline int cgba_sh4_arm_mixer_output_fast_ok(uint32_t address)
{
  return (address >> 24) == 3u && (address & 1u) == 0;
}

#endif /* CGBA_SH4_ARM_MIXER_H */
