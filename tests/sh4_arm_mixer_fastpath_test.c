#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_arm_mixer.h"

#define MEM_SIZE 512u
#define MEM_MASK (MEM_SIZE - 1u)

typedef struct model {
  cgba_sh4_arm_mixer_state s;
  uint8_t mem[MEM_SIZE];
  uint32_t cycles;
} model;

static uint8_t rd8(const model *m, uint32_t address)
{
  return m->mem[address & MEM_MASK];
}

static uint16_t rd16(const model *m, uint32_t address)
{
  uint32_t a = address & MEM_MASK;
  return (uint16_t)(m->mem[a] | ((uint16_t)m->mem[(a + 1u) & MEM_MASK] << 8));
}

static void wr16(model *m, uint32_t address, uint16_t value)
{
  uint32_t a = address & MEM_MASK;
  m->mem[a] = (uint8_t)value;
  m->mem[(a + 1u) & MEM_MASK] = (uint8_t)(value >> 8);
}

/* Straight instruction-order oracle for the 36-word body. */
static void run_reference(model *m, uint32_t seq_cost,
                          const uint8_t source_cost[4], uint32_t output_cost)
{
  uint32_t i;
  uint32_t sample_index = 0;

  for (i = 0; i < CGBA_SH4_ARM_MIXER_BODY_WORDS; i++) {
    uint32_t op = cgba_sh4_arm_mixer_pattern[i];
    m->cycles += seq_cost;
    switch (i % 9u) {
    case 0:
      if (op != 0xE080C424u) goto bad_opcode;
      m->s.r12 = m->s.r0 + (m->s.r4 >> 8);
      break;
    case 1:
      if (op != 0xE1DC80D0u) goto bad_opcode;
      m->s.r8 = (uint32_t)(int32_t)(int8_t)rd8(m, m->s.r12);
      m->cycles += source_cost[sample_index];
      break;
    case 2:
      if (op != 0xE1D1C0F0u) goto bad_opcode;
      m->s.r12 = (uint32_t)(int32_t)(int16_t)rd16(m, m->s.r1);
      m->cycles += output_cost;
      break;
    case 3:
      if (op != 0xE02CC896u) goto bad_opcode;
      m->s.r12 = m->s.r6 * m->s.r8 + m->s.r12;
      break;
    case 4:
      if (op != 0xE0C1C0B2u) goto bad_opcode;
      wr16(m, m->s.r1, (uint16_t)m->s.r12);
      m->s.r1 += 2u;
      m->cycles += output_cost;
      break;
    case 5:
      if (op != 0xE1D2C0F0u) goto bad_opcode;
      m->s.r12 = (uint32_t)(int32_t)(int16_t)rd16(m, m->s.r2);
      m->cycles += output_cost;
      break;
    case 6:
      if (op != 0xE02CC897u) goto bad_opcode;
      m->s.r12 = m->s.r7 * m->s.r8 + m->s.r12;
      break;
    case 7:
      if (op != 0xE0C2C0B2u) goto bad_opcode;
      wr16(m, m->s.r2, (uint16_t)m->s.r12);
      m->s.r2 += 2u;
      m->cycles += output_cost;
      break;
    default:
      if (op != 0xE0844005u) goto bad_opcode;
      m->s.r4 += m->s.r5;
      sample_index++;
      break;
    }
  }
  return;

bad_opcode:
  fprintf(stderr, "oracle saw unexpected opcode %08x at %u\n",
          cgba_sh4_arm_mixer_pattern[i], i);
  m->cycles = UINT32_MAX;
}

/* Portable counterpart of the optimized helper's exact memory ordering. */
static void run_fast_model(model *m, uint32_t seq_cost,
                           const uint8_t source_cost[4], uint32_t output_cost)
{
  uint32_t i;
  for (i = 0; i < 4; i++) {
    uint32_t source = cgba_sh4_arm_mixer_source_address(&m->s);
    int8_t sample = (int8_t)rd8(m, source);
    int16_t left;
    int16_t right;
    uint16_t store;

    cgba_sh4_arm_mixer_step_begin(&m->s, sample);
    left = (int16_t)rd16(m, m->s.r1);
    store = cgba_sh4_arm_mixer_step_left(&m->s, left);
    wr16(m, m->s.r1 - 2u, store);
    right = (int16_t)rd16(m, m->s.r2);
    store = cgba_sh4_arm_mixer_step_right(&m->s, right);
    wr16(m, m->s.r2 - 2u, store);
  }
  m->cycles += cgba_sh4_arm_mixer_body_cycles(
    seq_cost, source_cost, output_cost);
}

static void run_reference_batches(model *m, uint32_t batches,
                                  uint32_t seq_cost, uint8_t source_cost,
                                  uint32_t output_cost, uint32_t tail_cost)
{
  uint8_t costs[4] = {
    source_cost, source_cost, source_cost, source_cost
  };
  uint32_t i;

  for (i = 0; i < batches; i++) {
    run_reference(m, seq_cost, costs, output_cost);
    if (i + 1u < batches)
      m->cycles += tail_cost;
  }
}

/* Portable counterpart of the whole-call specialization.  It intentionally
 * performs each source load at its original position so source/output and
 * left/right aliases feed back exactly across batch boundaries. */
static void run_whole_model(model *m, uint32_t batches,
                            uint32_t seq_cost, uint8_t source_cost,
                            uint32_t output_cost, uint32_t tail_cost)
{
  uint32_t samples = batches * 4u;
  uint32_t i;

  for (i = 0; i < samples; i++) {
    uint32_t source = m->s.r0 + (m->s.r4 >> 8);
    uint32_t sample = (uint32_t)(int32_t)(int8_t)rd8(m, source);
    int16_t left = (int16_t)rd16(m, m->s.r1);
    int16_t right;

    m->s.r8 = sample;
    m->s.r12 = m->s.r6 * sample + (uint32_t)(int32_t)left;
    wr16(m, m->s.r1, (uint16_t)m->s.r12);
    m->s.r1 += 2u;

    right = (int16_t)rd16(m, m->s.r2);
    m->s.r12 = m->s.r7 * sample + (uint32_t)(int32_t)right;
    wr16(m, m->s.r2, (uint16_t)m->s.r12);
    m->s.r2 += 2u;
    m->s.r4 += m->s.r5;
  }

  m->cycles += batches *
    (CGBA_SH4_ARM_MIXER_BODY_WORDS * seq_cost +
     4u * source_cost + 16u * output_cost);
  m->cycles += (batches - 1u) * tail_cost;
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

static int compare_case(uint32_t case_no)
{
  model ref, got;
  uint8_t source_cost[4];
  uint32_t seq_cost = 1u + (rng32() & 7u);
  uint32_t output_cost = 1u + (rng32() & 15u);
  uint32_t i;

  for (i = 0; i < sizeof(ref.mem); i++)
    ref.mem[i] = (uint8_t)rng32();
  ref.s.r0 = 0x03000000u | (rng32() & MEM_MASK);
  ref.s.r1 = 0x03000000u | (rng32() & (MEM_MASK & ~1u));
  /* Frequent exact/partial aliasing catches reordered right loads. */
  ref.s.r2 = (case_no & 3u) ?
    (0x03000000u | (rng32() & (MEM_MASK & ~1u))) : ref.s.r1;
  ref.s.r3 = rng32(); ref.s.r4 = rng32(); ref.s.r5 = rng32();
  ref.s.r6 = rng32(); ref.s.r7 = rng32(); ref.s.r8 = rng32();
  ref.s.r12 = rng32(); ref.s.cpsr = rng32(); ref.cycles = 0;
  for (i = 0; i < 4; i++)
    source_cost[i] = (uint8_t)(1u + (rng32() & 15u));
  got = ref;

  run_reference(&ref, seq_cost, source_cost, output_cost);
  run_fast_model(&got, seq_cost, source_cost, output_cost);

  if (memcmp(&ref.s, &got.s, sizeof(ref.s)) ||
      memcmp(ref.mem, got.mem, sizeof(ref.mem)) || ref.cycles != got.cycles) {
    fprintf(stderr, "mixer mismatch case=%u cycles=%u/%u "
                    "r1=%08x/%08x r2=%08x/%08x r4=%08x/%08x "
                    "r8=%08x/%08x r12=%08x/%08x cpsr=%08x/%08x\n",
            case_no, ref.cycles, got.cycles,
            ref.s.r1, got.s.r1, ref.s.r2, got.s.r2,
            ref.s.r4, got.s.r4, ref.s.r8, got.s.r8,
            ref.s.r12, got.s.r12, ref.s.cpsr, got.s.cpsr);
    return 0;
  }
  return 1;
}

static int compare_whole_case(uint32_t case_no)
{
  model ref, got;
  uint32_t batches = 1u + (rng32() & 7u);
  uint32_t seq_cost = 1u + (rng32() & 7u);
  uint32_t output_cost = 1u + (rng32() & 15u);
  uint32_t tail_cost = 1u + (rng32() & 15u);
  uint8_t source_cost = (uint8_t)(1u + (rng32() & 15u));
  uint32_t i;

  for (i = 0; i < sizeof(ref.mem); i++)
    ref.mem[i] = (uint8_t)rng32();
  ref.s.r1 = 0x03000000u | (rng32() & (MEM_MASK & ~1u));
  ref.s.r2 = (case_no & 3u) ?
    (0x03000000u | (rng32() & (MEM_MASK & ~1u))) : ref.s.r1;
  ref.s.r4 = rng32();
  ref.s.r5 = rng32();
  /* Make source/output feedback common, including across batches. */
  if ((case_no & 7u) == 0u)
    ref.s.r0 = ref.s.r1 - (ref.s.r4 >> 8);
  else if ((case_no & 7u) == 1u)
    ref.s.r0 = ref.s.r2 - (ref.s.r4 >> 8);
  else
    ref.s.r0 = 0x03000000u | (rng32() & MEM_MASK);
  ref.s.r3 = ref.s.r1 + batches * 8u;
  ref.s.r6 = rng32(); ref.s.r7 = rng32(); ref.s.r8 = rng32();
  ref.s.r12 = rng32(); ref.s.cpsr = rng32(); ref.cycles = 0;
  got = ref;

  run_reference_batches(&ref, batches, seq_cost, source_cost,
                        output_cost, tail_cost);
  run_whole_model(&got, batches, seq_cost, source_cost,
                  output_cost, tail_cost);

  if (memcmp(&ref.s, &got.s, sizeof(ref.s)) ||
      memcmp(ref.mem, got.mem, sizeof(ref.mem)) || ref.cycles != got.cycles) {
    fprintf(stderr, "whole mixer mismatch case=%u batches=%u cycles=%u/%u "
                    "r1=%08x/%08x r2=%08x/%08x r4=%08x/%08x "
                    "r8=%08x/%08x r12=%08x/%08x cpsr=%08x/%08x\n",
            case_no, batches, ref.cycles, got.cycles,
            ref.s.r1, got.s.r1, ref.s.r2, got.s.r2,
            ref.s.r4, got.s.r4, ref.s.r8, got.s.r8,
            ref.s.r12, got.s.r12, ref.s.cpsr, got.s.cpsr);
    return 0;
  }
  return 1;
}

static int compare_plan_case(uint32_t case_no)
{
  uint32_t r1 = 0x03000000u | ((rng32() & 0x7FFFu) & ~1u);
  uint32_t r3 = r1 + (rng32() & 0xFFu);
  int32_t budget = 1 + (int32_t)(rng32() & 0x7FFu);
  uint32_t body = 1u + (rng32() & 0x3FFu);
  uint32_t tail = 1u + (rng32() & 0x3Fu);
  uint32_t ref_batches = 1u;
  uint32_t ref_spent = body;
  uint32_t got_spent;
  int ref_stop;
  int got_stop;
  uint32_t got_batches;

  while (ref_batches < CGBA_SH4_ARM_MIXER_WHOLE_MAX_BATCHES &&
         r1 + ref_batches * 8u < r3 &&
         cgba_sh4_arm_mixer_can_fold_taken(
           budget, ref_spent, tail)) {
    ref_spent += tail + body;
    ref_batches++;
  }
  ref_stop = r1 + ref_batches * 8u < r3 &&
    !cgba_sh4_arm_mixer_can_fold_taken(budget, ref_spent, tail);

  got_batches = cgba_sh4_arm_mixer_plan_constant(
    r1, r3, budget, body, tail, &got_spent, &got_stop);
  if (ref_batches != got_batches || ref_spent != got_spent ||
      ref_stop != got_stop) {
    fprintf(stderr, "mixer plan mismatch case=%u n=%u/%u spent=%u/%u "
                    "stop=%d/%d budget=%d body=%u tail=%u\n",
            case_no, ref_batches, got_batches, ref_spent, got_spent,
            ref_stop, got_stop, budget, body, tail);
    return 0;
  }
  return 1;
}

int main(void)
{
  uint32_t pattern[CGBA_SH4_ARM_MIXER_WORDS];
  uint32_t i;

  memcpy(pattern, cgba_sh4_arm_mixer_pattern, sizeof(pattern));
  if (!cgba_sh4_arm_mixer_match(pattern)) {
    fprintf(stderr, "canonical mixer signature rejected\n");
    return 1;
  }
  pattern[17] ^= 1u;
  if (cgba_sh4_arm_mixer_match(pattern)) {
    fprintf(stderr, "mutated mixer signature accepted\n");
    return 1;
  }

  if (!cgba_sh4_arm_mixer_source_fast_ok(0x02000000u) ||
      !cgba_sh4_arm_mixer_source_fast_ok(0x03000000u) ||
      !cgba_sh4_arm_mixer_source_fast_ok(0x08000000u) ||
      cgba_sh4_arm_mixer_source_fast_ok(0x04000000u) ||
      cgba_sh4_arm_mixer_source_fast_ok(0x0D000000u) ||
      !cgba_sh4_arm_mixer_output_fast_ok(0x03000000u) ||
      cgba_sh4_arm_mixer_output_fast_ok(0x03000001u) ||
      cgba_sh4_arm_mixer_output_fast_ok(0x02000000u)) {
    fprintf(stderr, "mixer memory guard boundary mismatch\n");
    return 1;
  }

  if (cgba_sh4_arm_mixer_can_fold_taken(100, 90, 10) ||
      !cgba_sh4_arm_mixer_can_fold_taken(101, 90, 10) ||
      cgba_sh4_arm_mixer_can_fold_taken(-1, 0, 1)) {
    fprintf(stderr, "mixer budget boundary mismatch\n");
    return 1;
  }

  for (i = 0; i < 100000; i++) {
    if (!compare_case(i) || !compare_whole_case(i) ||
        !compare_plan_case(i))
      return 1;
  }

  puts("sh4 ARM mixer fastpath: signature/state/memory/cycles/whole-plan OK "
       "(300000 cases)");
  return 0;
}
