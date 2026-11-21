#ifndef CGBA_SH4_ARM_PCM_CLAMP_H
#define CGBA_SH4_ARM_PCM_CLAMP_H

/*
 * Exact model for the eight-sample 16-bit-to-8-bit signed PCM downmix/clamp
 * loop copied into IWRAM by Nintendo/Flagship GBA sound drivers (e.g. Legend
 * of Zelda: A Link to the Past & Four Swords).  As with sh4_arm_mixer.h, the
 * fast path stops right before the CMP/BNE loop tail so architectural NZCV
 * flags and scheduler exits remain on the ordinary translated path.
 *
 * One batch is eight repetitions of:
 *   ldrsh r3, [r0], #2
 *   cmp   r3, #0
 *   addmi r3, r3, #0x7F
 *   mov   r3, r3, asr #7
 *   ands  r12, r3, r5
 *   cmpne r12, r5
 *   addne r3, r4, r3, lsr #31
 *   strb  r3, [r1], #1
 * followed by `cmp r0, r2; bne batch`.
 *
 * This header has no target dependencies so arithmetic, register, signature,
 * and cycle contracts can be differentially verified on the host.
 */

#include <stdint.h>

#define CGBA_SH4_ARM_PCM_CLAMP_STEP_WORDS  8u
#define CGBA_SH4_ARM_PCM_CLAMP_STEPS       8u
#define CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS \
  (CGBA_SH4_ARM_PCM_CLAMP_STEP_WORDS * CGBA_SH4_ARM_PCM_CLAMP_STEPS)
#define CGBA_SH4_ARM_PCM_CLAMP_TAIL_WORDS  2u
#define CGBA_SH4_ARM_PCM_CLAMP_WORDS \
  (CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS + CGBA_SH4_ARM_PCM_CLAMP_TAIL_WORDS)
#define CGBA_SH4_ARM_PCM_CLAMP_BODY_BYTES \
  (CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS * 4u)
#define CGBA_SH4_ARM_PCM_CLAMP_BYTES (CGBA_SH4_ARM_PCM_CLAMP_WORDS * 4u)

/* Up to 16 batches (128 samples, 256 input bytes, 128 output bytes) per
 * helper invocation, enough to absorb a full 960-cycle HBlank slice in one
 * call while keeping the SMC-tag proof bounded. */
#define CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES 16u

static const uint32_t cgba_sh4_arm_pcm_clamp_step_pattern[CGBA_SH4_ARM_PCM_CLAMP_STEP_WORDS] = {
  0xE0D030F2u, /* ldrsh r3, [r0], #2 */
  0xE3530000u, /* cmp   r3, #0 */
  0x4283307Fu, /* addmi r3, r3, #0x7F */
  0xE1A033C3u, /* mov   r3, r3, asr #7 */
  0xE013C005u, /* ands  r12, r3, r5 */
  0x115C0005u, /* cmpne r12, r5 */
  0x10843FA3u, /* addne r3, r4, r3, lsr #31 */
  0xE4C13001u  /* strb  r3, [r1], #1 */
};

static const uint32_t cgba_sh4_arm_pcm_clamp_tail_pattern[CGBA_SH4_ARM_PCM_CLAMP_TAIL_WORDS] = {
  0xE1500002u, /* cmp   r0, r2 */
  0x1AFFFFBDu  /* bne   -0x104 (batch start) */
};

static inline uint32_t cgba_sh4_arm_pcm_clamp_word(uint32_t idx)
{
  if (idx < CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS)
    return cgba_sh4_arm_pcm_clamp_step_pattern[idx & 7u];
  return cgba_sh4_arm_pcm_clamp_tail_pattern[idx - CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS];
}

static inline int cgba_sh4_arm_pcm_clamp_match(const uint32_t *ops)
{
  uint32_t i;
  for (i = 0; i < CGBA_SH4_ARM_PCM_CLAMP_WORDS; i++) {
    if (ops[i] != cgba_sh4_arm_pcm_clamp_word(i))
      return 0;
  }
  return 1;
}

typedef struct cgba_sh4_arm_pcm_clamp_state {
  uint32_t r0, r1, r2, r3, r4, r5, r12;
} cgba_sh4_arm_pcm_clamp_state;

/* Execute one 8-instruction sample step with exact ARM arithmetic for any
 * r4/r5 and any signed 16-bit input sample (portable C11, no signed-shift UB). */
static inline uint8_t cgba_sh4_arm_pcm_clamp_step(
  cgba_sh4_arm_pcm_clamp_state *s, int16_t sample)
{
  uint32_t r3 = (uint32_t)(int32_t)sample;
  uint32_t r12;

  if (sample < 0)
    r3 += 0x7Fu;
  r3 = (r3 >> 7) | ((uint32_t)-(int32_t)(r3 >> 31) << 25);
  r12 = r3 & s->r5;
  if (r12 != 0u && r12 != s->r5)
    r3 = s->r4 + (r3 >> 31);
  s->r0 += 2u;
  s->r1 += 1u;
  s->r3 = r3;
  s->r12 = r12;
  return (uint8_t)r3;
}

/* All 64 body instructions fetch sequentially; the 8 LDRSH and 8 STRB
 * accesses charge their respective halfword/byte non-sequential region costs. */
static inline uint32_t cgba_sh4_arm_pcm_clamp_body_cycles(
  uint32_t seq_word_cost, uint32_t in_nseq_cost, uint32_t out_nseq_cost)
{
  return CGBA_SH4_ARM_PCM_CLAMP_BODY_WORDS * seq_word_cost +
         CGBA_SH4_ARM_PCM_CLAMP_STEPS * in_nseq_cost +
         CGBA_SH4_ARM_PCM_CLAMP_STEPS * out_nseq_cost;
}

static inline uint32_t cgba_sh4_arm_pcm_clamp_taken_tail_cycles(
  uint32_t seq_word_cost, uint32_t branch_nseq_word_cost)
{
  return 2u * seq_word_cost + branch_nseq_word_cost;
}

/* Plan a constant-cost multi-batch prefix matching `cmp r0, r2; bne batch`
 * and sh4g_branch_exit's strictly-positive R13 gate. */
static inline uint32_t cgba_sh4_arm_pcm_clamp_plan_constant(
  uint32_t r0, uint32_t r2, int32_t budget, uint32_t body_cycles,
  uint32_t tail_cycles, uint32_t *spent_out)
{
  uint32_t batches = 1u;
  uint32_t spent = body_cycles;

  while (batches < CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES &&
         (r0 + batches * 16u) != r2) {
    if (budget <= 0 || (uint32_t)budget <= spent + tail_cycles)
      break;
    spent += tail_cycles + body_cycles;
    batches++;
  }

  *spent_out = spent;
  return batches;
}

#endif /* CGBA_SH4_ARM_PCM_CLAMP_H */
