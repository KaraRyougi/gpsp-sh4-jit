#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_arm_pcm_clamp.h"

/* Instruction-by-instruction ARM oracle for one 8-word sample step:
 *   ldrsh r3, [r0], #2
 *   cmp   r3, #0
 *   addmi r3, r3, #0x7F
 *   mov   r3, r3, asr #7
 *   ands  r12, r3, r5
 *   cmpne r12, r5
 *   addne r3, r4, r3, lsr #31
 *   strb  r3, [r1], #1
 */
static uint8_t oracle_step(cgba_sh4_arm_pcm_clamp_state *s, int16_t sample)
{
  uint32_t r3 = (uint32_t)(int32_t)sample;
  int n_flag = (r3 >> 31) & 1;
  int z_flag;
  uint32_t r12;

  s->r0 += 2u;
  if (n_flag)
    r3 += 0x7Fu;
  /* ARM ASR #7 on 32-bit value */
  if (r3 & 0x80000000u)
    r3 = (r3 >> 7) | 0xFE000000u;
  else
    r3 = (r3 >> 7);
  r12 = r3 & s->r5;
  z_flag = (r12 == 0u);
  if (!z_flag)
    z_flag = (r12 == s->r5);
  if (!z_flag)
    r3 = s->r4 + (r3 >> 31);
  s->r1 += 1u;
  s->r3 = r3;
  s->r12 = r12;
  return (uint8_t)r3;
}

static uint32_t rng_state = 0x12345678u;
static uint32_t next_u32(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

int main(void)
{
  uint32_t ops[CGBA_SH4_ARM_PCM_CLAMP_WORDS];
  uint32_t i;

  /* 1. Signature match + single-bit mutation rejection across all 66 words. */
  for (i = 0; i < CGBA_SH4_ARM_PCM_CLAMP_WORDS; i++)
    ops[i] = cgba_sh4_arm_pcm_clamp_word(i);
  if (!cgba_sh4_arm_pcm_clamp_match(ops)) {
    fprintf(stderr, "canonical PCM clamp pattern failed to match\n");
    return 1;
  }
  for (i = 0; i < CGBA_SH4_ARM_PCM_CLAMP_WORDS; i++) {
    ops[i] ^= 1u << (i & 31u);
    if (cgba_sh4_arm_pcm_clamp_match(ops)) {
      fprintf(stderr, "mutated word %u unexpectedly matched\n", i);
      return 1;
    }
    ops[i] ^= 1u << (i & 31u);
  }

  /* 2. Exhaustive all 65,536 int16_t inputs for Zelda's r4=0x7F, r5=0xFF80. */
  for (i = 0; i < 65536u; i++) {
    int16_t sample = (int16_t)(uint16_t)i;
    cgba_sh4_arm_pcm_clamp_state a = { 0x03000600u, 0x03000500u, 0, 0, 0x7Fu, 0xFF80u, 0 };
    cgba_sh4_arm_pcm_clamp_state b = a;
    uint8_t oa = oracle_step(&a, sample);
    uint8_t ob = cgba_sh4_arm_pcm_clamp_step(&b, sample);
    if (oa != ob || memcmp(&a, &b, sizeof(a)) != 0) {
      fprintf(stderr, "mismatch at sample=%d: oracle=%02x fast=%02x\n",
              (int)sample, oa, ob);
      return 1;
    }
  }

  /* 3. Random (r4, r5, sample) stress test. */
  for (i = 0; i < 50000u; i++) {
    int16_t sample = (int16_t)(uint16_t)next_u32();
    uint32_t r4 = next_u32();
    uint32_t r5 = (i & 3u) ? (next_u32() & 0xFFFFu) : next_u32();
    cgba_sh4_arm_pcm_clamp_state a = { 0x03001000u, 0x02001000u, 0, 0, r4, r5, 0 };
    cgba_sh4_arm_pcm_clamp_state b = a;
    uint8_t oa = oracle_step(&a, sample);
    uint8_t ob = cgba_sh4_arm_pcm_clamp_step(&b, sample);
    if (oa != ob || memcmp(&a, &b, sizeof(a)) != 0) {
      fprintf(stderr, "random mismatch i=%u sample=%d r4=%08x r5=%08x\n",
              i, (int)sample, r4, r5);
      return 1;
    }
  }

  /* 4. Cycle accounting and constant multi-batch planner checks. */
  {
    uint32_t body = cgba_sh4_arm_pcm_clamp_body_cycles(1u, 1u, 3u);
    uint32_t tail = cgba_sh4_arm_pcm_clamp_taken_tail_cycles(1u, 1u);
    uint32_t spent = 0;
    uint32_t batches;

    if (body != 64u + 8u + 24u || tail != 3u) {
      fprintf(stderr, "unexpected cycle formula body=%u tail=%u\n", body, tail);
      return 1;
    }
    /* Exact 4 batches available, ample budget. */
    batches = cgba_sh4_arm_pcm_clamp_plan_constant(
      0x03000600u, 0x03000640u, 1000, body, tail, &spent);
    if (batches != 4u || spent != 4u * body + 3u * tail) {
      fprintf(stderr, "4-batch plan mismatch: b=%u spent=%u\n", batches, spent);
      return 1;
    }
    /* Budget boundary: cannot fold 2nd batch if budget <= body + tail. */
    batches = cgba_sh4_arm_pcm_clamp_plan_constant(
      0x03000600u, 0x03000800u, (int32_t)(body + tail), body, tail, &spent);
    if (batches != 1u || spent != body) {
      fprintf(stderr, "budget-eq stop mismatch: b=%u spent=%u\n", batches, spent);
      return 1;
    }
    /* Budget boundary: folds 2nd batch when budget == body + tail + 1. */
    batches = cgba_sh4_arm_pcm_clamp_plan_constant(
      0x03000600u, 0x03000800u, (int32_t)(body + tail + 1u), body, tail, &spent);
    if (batches != 2u || spent != 2u * body + tail) {
      fprintf(stderr, "budget+1 fold mismatch: b=%u spent=%u\n", batches, spent);
      return 1;
    }
    /* Cap at CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES. */
    batches = cgba_sh4_arm_pcm_clamp_plan_constant(
      0x03000600u, 0x03001600u, 100000, body, tail, &spent);
    if (batches != CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES ||
        spent != CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES * body +
                 (CGBA_SH4_ARM_PCM_CLAMP_MAX_BATCHES - 1u) * tail) {
      fprintf(stderr, "max-batch cap mismatch: b=%u spent=%u\n", batches, spent);
      return 1;
    }
  }

  printf("sh4 ARM PCM clamp fastpath: signature/state/cycles OK (115536 cases)\n");
  return 0;
}
