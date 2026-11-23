/* Internal module included by sh4_interp_helpers.c: audio mixer & loop fastpaths. */
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
/* Runtime half of the signature-gated m4a/Sappy IWRAM mixer fast path.  Every
 * access and SMC tag is validated before the first write.  The helper folds a
 * taken CMP/BCC only while it leaves the snapshotted JIT budget positive; the
 * final CMP/BCC always remains on the ordinary translated path. */
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_arm_mixer_try_count;
u32 cgba_sh4_arm_mixer_hit_count;
u32 cgba_sh4_arm_mixer_batch_count;
u32 cgba_sh4_arm_mixer_guard_fallback_count;
u32 cgba_sh4_arm_mixer_budget_stop_count;
u32 cgba_sh4_arm_mixer_first_pc;
u32 cgba_sh4_arm_mixer_first_reg[10];
#endif

typedef struct cgba_sh4_arm_mixer_prepared {
  u8 *source_page[4];
  u16 source_offset[4];
  u8 source_cost[4];
  u32 body_cycles;
} cgba_sh4_arm_mixer_prepared;

static int cgba_sh4_arm_mixer_prepare(
  const cgba_sh4_arm_mixer_state *s, u32 seq_word_cost,
  cgba_sh4_arm_mixer_prepared *p)
{
  u32 phase = s->r4;
  u32 i;

  for (i = 0; i < 4; i++) {
    u32 source = s->r0 + (phase >> 8);
    u32 left = s->r1 + i * 2u;
    u32 right = s->r2 + i * 2u;
    u32 left_off, right_off;
    u8 *page;

    if (!cgba_sh4_arm_mixer_source_fast_ok(source) ||
        (((source >> 24) >= 8u) &&
         ((source & 0x01FFFFFFu) >= gamepak_size)) ||
        !cgba_sh4_arm_mixer_output_fast_ok(left) ||
        !cgba_sh4_arm_mixer_output_fast_ok(right))
      return 0;
    page = memory_map_read[source >> 15];
    if (page) {
      p->source_page[i] = page + (source & 0x7000u);
    } else if ((source >> 24) >= 8u && (source >> 24) <= 12u) {
      page = cgba_gamepak_resolve_4k(source);
      if (!page)
        return 0;
      p->source_page[i] = page;
    } else {
      return 0;
    }

    left_off = left & 0x7FFFu;
    right_off = right & 0x7FFFu;
    if (iwram[left_off] || iwram[left_off + 1u] ||
        iwram[right_off] || iwram[right_off + 1u])
      return 0;

    p->source_offset[i] = (u16)(source & 0x0FFFu);
    p->source_cost[i] = ws_cyc_nseq[(source >> 24) & 0x0Fu][0];
    phase += s->r5;
  }
  p->body_cycles = cgba_sh4_arm_mixer_body_cycles(
    seq_word_cost, p->source_cost, ws_cyc_nseq[3][0]);
  return 1;
}

static void cgba_sh4_arm_mixer_run_prepared(
  cgba_sh4_arm_mixer_state *s, const cgba_sh4_arm_mixer_prepared *p)
{
  u8 *data = iwram + 0x8000;
  u32 i;

  for (i = 0; i < 4; i++) {
    int8_t sample = (int8_t)address8(p->source_page[i], p->source_offset[i]);
    u32 left_off = s->r1 & 0x7FFFu;
    int16_t left = (int16_t)readaddress16(data, left_off);
    uint16_t left_store;
    u32 right_off;
    int16_t right;
    uint16_t right_store;

    cgba_sh4_arm_mixer_step_begin(s, sample);
    left_store = cgba_sh4_arm_mixer_step_left(s, left);
    address16(data, left_off) = eswap16(left_store);

    /* Preserve load-left/store-left/load-right/store-right when buffers alias. */
    right_off = s->r2 & 0x7FFFu;
    right = (int16_t)readaddress16(data, right_off);
    right_store = cgba_sh4_arm_mixer_step_right(s, right);
    address16(data, right_off) = eswap16(right_store);
  }
}

/* Zelda's m4a invocation fits eight batches in one scheduler slice.  Prove
 * that complete prefix up front: both outputs are contiguous/tag-free IWRAM
 * and every monotonically increasing sample address stays in one mapped ROM
 * page.  Samples are still loaded at their exact instruction-order position,
 * so even a source mapping that aliases an output buffer observes feedback.
 * Any unusual wrap, mirror, page, ROM-size or tag boundary declines before
 * the first write and uses the conservative per-batch path below. */
static int __attribute__((noinline)) cgba_sh4_arm_mixer_run_whole(
  cgba_sh4_arm_mixer_state *s, u32 seq_word_cost, u32 tail_cycles,
  s32 budget, u32 *spent_out, u32 *batches_out, int *budget_stop_out)
{
  u32 source = cgba_sh4_arm_mixer_source_address(s);
  u32 source_region = source >> 24;
  u32 source_cost;
  u32 body_cycles;
  uint32_t spent;
  u32 batches;
  u32 samples;
  u32 bytes;
  u32 last_phase;
  u32 last_source;
  u32 left_last, right_last;
  u32 left_off, right_off;
  u8 *source_page;
  u16 *left_ptr, *right_ptr;
  u32 phase;
  u32 sample32 = 0;
  u32 mixed = 0;
  u32 i;

  if (!cgba_sh4_arm_mixer_output_fast_ok(s->r1) ||
      !cgba_sh4_arm_mixer_output_fast_ok(s->r2) ||
      source < s->r0 ||
      !cgba_sh4_arm_mixer_source_fast_ok(source) ||
      (source_region >= 8u &&
       (source & 0x01FFFFFFu) >= gamepak_size))
    return 0;

  source_cost = ws_cyc_nseq[source_region & 0x0Fu][0];
  body_cycles = CGBA_SH4_ARM_MIXER_BODY_WORDS * seq_word_cost +
                4u * source_cost + 16u * (u32)ws_cyc_nseq[3][0];
  batches = cgba_sh4_arm_mixer_plan_constant(
    s->r1, s->r3, budget, body_cycles, tail_cycles,
    &spent, budget_stop_out);
  samples = batches * 4u;
  bytes = samples * 2u;

  /* samples <= 32.  This deliberately conservative shift proves that even
   * the maximum 31 increments cannot wrap without a runtime divide. */
  if (s->r5 > ((~s->r4) >> 5))
    return 0;
  last_phase = s->r4 + (samples - 1u) * s->r5;
  last_source = s->r0 + (last_phase >> 8);
  if (last_source < s->r0 ||
      (last_source >> 24) != source_region ||
      (source_region >= 8u &&
       (last_source & 0x01FFFFFFu) >= gamepak_size))
    return 0;

  left_last = s->r1 + bytes - 2u;
  right_last = s->r2 + bytes - 2u;
  left_off = s->r1 & 0x7FFFu;
  right_off = s->r2 & 0x7FFFu;
  if (!cgba_sh4_arm_mixer_output_fast_ok(left_last) ||
      !cgba_sh4_arm_mixer_output_fast_ok(right_last) ||
      left_off + bytes > 0x8000u || right_off + bytes > 0x8000u ||
      cgba_smc_tags_nonzero(iwram + left_off, bytes) ||
      cgba_smc_tags_nonzero(iwram + right_off, bytes))
    return 0;

  left_ptr = (u16 *)(void *)(iwram + 0x8000u + left_off);
  right_ptr = (u16 *)(void *)(iwram + 0x8000u + right_off);
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
  /* Soundless O(1) skip: when the sound backend is a no-op and the PCM source
   * is outside IWRAM (ROM or EWRAM, cannot alias the IWRAM stereo output
   * buffer), the mixed samples written to left_ptr/right_ptr are discarded by
   * audio DMA. Advance registers and cycle accounting in O(1), evaluating only
   * the final sample to keep r8/r12 exact. Caller already rejected diff mode. */
  if (source_region != 3u) {
    u8 *last_page = memory_map_read[last_source >> 15];
    u32 last_off;
    if (last_page) {
      last_off = last_source & 0x7FFFu;
    } else if (source_region >= 8u) {
      last_page = cgba_gamepak_resolve_4k(last_source);
      if (!last_page)
        return 0;
      last_off = last_source & 0x0FFFu;
    } else {
      return 0;
    }
    s16 last_right = (s16)eswap16(right_ptr[samples - 1u]);
    sample32 = (u32)(s32)(s8)address8(last_page, last_off);
    mixed = s->r7 * sample32 + (u32)(s32)last_right;
    phase = last_phase + s->r5;
  } else
#endif
  {
    u32 src_mask;
    if ((source >> 15) == (last_source >> 15) &&
        (source_page = memory_map_read[source >> 15]) != NULL) {
      src_mask = 0x7FFFu;
    } else if (source_region >= 8u &&
               (source >> 12) == (last_source >> 12) &&
               (source_page = cgba_gamepak_resolve_4k(source)) != NULL) {
      src_mask = 0x0FFFu;
    } else {
      return 0;
    }
    phase = s->r4;
    for (i = 0; i < samples; i++) {
      u32 sample_address = s->r0 + (phase >> 8);
      s16 left;
      s16 right;

      sample32 = (u32)(s32)(s8)address8(source_page,
                                        sample_address & src_mask);
      left = (s16)eswap16(*left_ptr);
      mixed = s->r6 * sample32 + (u32)(s32)left;
      *left_ptr++ = eswap16((u16)mixed);

      /* Preserve load-left/store-left/load-right/store-right for aliasing
       * stereo buffers and for an IWRAM source page feeding back from them. */
      right = (s16)eswap16(*right_ptr);
      mixed = s->r7 * sample32 + (u32)(s32)right;
      *right_ptr++ = eswap16((u16)mixed);
      phase += s->r5;
    }
  }

  s->r1 += bytes;
  s->r2 += bytes;
  s->r4 = phase;
  s->r8 = sample32;
  s->r12 = mixed;
  *spent_out = spent;
  *batches_out = batches;
  return 1;
}

int cgba_sh4_arm_mixer_loop_try(u32 unused, u32 pc)
{
  cgba_sh4_arm_mixer_state s;
  cgba_sh4_arm_mixer_prepared cur;
  u32 region = (pc >> 24) & 0x0Fu;
  u32 seq_word_cost = ws_cyc_seq[region][1];
  u32 tail_cycles = cgba_sh4_arm_mixer_taken_tail_cycles(
    seq_word_cost, ws_cyc_nseq[region][1]);
  u32 spent = 0;
  u32 batches = 0;
  int whole_budget_stop = 0;
  int whole_hit;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_state initial_s;
#endif
  (void)unused;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_try_count++;
#endif
  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u) ||
      cgba_sh4_fastpath_budget <= 0)
    return 0;

  s.r0 = reg[0]; s.r1 = reg[1]; s.r2 = reg[2]; s.r3 = reg[3];
  s.r4 = reg[4]; s.r5 = reg[5]; s.r6 = reg[6]; s.r7 = reg[7];
  s.r8 = reg[8]; s.r12 = reg[12]; s.cpsr = reg[REG_CPSR];

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  initial_s = s;
#endif

  whole_hit = cgba_sh4_arm_mixer_run_whole(
    &s, seq_word_cost, tail_cycles, cgba_sh4_fastpath_budget,
    &spent, &batches, &whole_budget_stop);

  if (!whole_hit && !cgba_sh4_arm_mixer_prepare(&s, seq_word_cost, &cur)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    cgba_sh4_arm_mixer_guard_fallback_count++;
#endif
    return 0;
  }

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  if (!cgba_sh4_arm_mixer_hit_count) {
    cgba_sh4_arm_mixer_first_pc = pc;
    cgba_sh4_arm_mixer_first_reg[0] = initial_s.r0;
    cgba_sh4_arm_mixer_first_reg[1] = initial_s.r1;
    cgba_sh4_arm_mixer_first_reg[2] = initial_s.r2;
    cgba_sh4_arm_mixer_first_reg[3] = initial_s.r3;
    cgba_sh4_arm_mixer_first_reg[4] = initial_s.r4;
    cgba_sh4_arm_mixer_first_reg[5] = initial_s.r5;
    cgba_sh4_arm_mixer_first_reg[6] = initial_s.r6;
    cgba_sh4_arm_mixer_first_reg[7] = initial_s.r7;
    cgba_sh4_arm_mixer_first_reg[8] = initial_s.r8;
    cgba_sh4_arm_mixer_first_reg[9] = initial_s.r12;
  }
  if (whole_hit && whole_budget_stop)
    cgba_sh4_arm_mixer_budget_stop_count++;
#endif

  while (!whole_hit) {
    cgba_sh4_arm_mixer_run_prepared(&s, &cur);
    spent += cur.body_cycles;
    batches++;

    if (s.r1 >= s.r3 ||
        !cgba_sh4_arm_mixer_can_fold_taken(
          cgba_sh4_fastpath_budget, spent, tail_cycles)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
      if (s.r1 < s.r3)
        cgba_sh4_arm_mixer_budget_stop_count++;
#endif
      break;
    }

    /* Prove the next batch safe before consuming this taken tail.  A failed
     * proof returns at the current CMP with no partial next-batch state. */
    if (!cgba_sh4_arm_mixer_prepare(&s, seq_word_cost, &cur))
      break;
    spent += tail_cycles;
  }

  reg[1] = s.r1;
  reg[2] = s.r2;
  reg[4] = s.r4;
  reg[8] = s.r8;
  reg[12] = s.r12;
  cgba_sh4_extra_cycles = (int)spent;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_hit_count++;
  cgba_sh4_arm_mixer_batch_count += batches;
#endif
  return 1;
}

/* Fast path for the 8-sample 16-bit -> 8-bit signed PCM clamp loop
 * (see sh4_arm_pcm_clamp.h).  In soundless production builds
 * (CGBA_SH4_M4A_MIX_SKIP && !CGBA_SH4_DIFF_ACTIVE()) with non-overlapping
 * input/output buffers, skips writing the dead Direct Sound FIFO buffer in
 * O(1) while computing exact final r3/r12, advancing r0/r1, and charging
 * exact cycles.  In diff mode or overlapping buffers, executes every sample
 * faithfully.  Returns to the untouched CMP/BNE loop tail. */
int cgba_sh4_arm_pcm_clamp_loop_try(u32 unused, u32 pc)
{
  u32 r0 = reg[0], r1 = reg[1], r2 = reg[2], r4 = reg[4], r5 = reg[5];
  u32 in_region = r0 >> 24;
  u32 out_region = r1 >> 24;
  u32 pc_region = (pc >> 24) & 0x0Fu;
  u32 seq_word_cost = ws_cyc_seq[pc_region][1];
  u32 body_cycles, tail_cycles, batches, samples, in_bytes, out_bytes;
  uint32_t spent;
  u32 in_off, out_off;
  const u16 *src_ptr;
  u8 *dst_ptr;
  const u8 *out_tags;
  cgba_sh4_arm_pcm_clamp_state st;
  u32 i;
  (void)unused;

  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u) ||
      cgba_sh4_fastpath_budget <= 0)
    return 0;
  if ((r0 & 1u) != 0u ||
      (in_region != 2u && in_region != 3u) ||
      (out_region != 2u && out_region != 3u))
    return 0;

  body_cycles = cgba_sh4_arm_pcm_clamp_body_cycles(
    seq_word_cost, ws_cyc_nseq[in_region][0], ws_cyc_nseq[out_region][0]);
  tail_cycles = cgba_sh4_arm_pcm_clamp_taken_tail_cycles(
    seq_word_cost, ws_cyc_nseq[pc_region][1]);
  batches = cgba_sh4_arm_pcm_clamp_plan_constant(
    r0, r2, cgba_sh4_fastpath_budget, body_cycles, tail_cycles, &spent);
  samples = batches * CGBA_SH4_ARM_PCM_CLAMP_STEPS;
  in_bytes = samples * 2u;
  out_bytes = samples;

  if (r0 + in_bytes - 2u < r0 || ((r0 + in_bytes - 2u) >> 24) != in_region ||
      r1 + out_bytes - 1u < r1 || ((r1 + out_bytes - 1u) >> 24) != out_region)
    return 0;

  if (in_region == 3u) {
    in_off = r0 & 0x7FFFu;
    if (in_off + in_bytes > 0x8000u)
      return 0;
    src_ptr = (const u16 *)(const void *)(iwram + 0x8000u + in_off);
  } else {
    in_off = r0 & 0x3FFFFu;
    if (in_off + in_bytes > 0x40000u)
      return 0;
    src_ptr = (const u16 *)(const void *)(ewram + in_off);
  }

  if (out_region == 3u) {
    out_off = r1 & 0x7FFFu;
    if (out_off + out_bytes > 0x8000u)
      return 0;
    dst_ptr = iwram + 0x8000u + out_off;
    out_tags = iwram + out_off;
  } else {
    out_off = r1 & 0x3FFFFu;
    if (out_off + out_bytes > 0x40000u)
      return 0;
    dst_ptr = ewram + out_off;
    out_tags = ewram + 0x40000u + out_off;
  }

  if (cgba_smc_tags_nonzero(out_tags, out_bytes))
    return 0;

  st.r0 = r0; st.r1 = r1; st.r2 = r2;
  st.r3 = 0u; st.r4 = r4; st.r5 = r5; st.r12 = 0u;
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
  if (in_region != out_region ||
      in_off + in_bytes <= out_off || out_off + out_bytes <= in_off) {
    /* O(1) soundless skip: evaluate only the final sample for exact r3/r12. */
    u16 raw = src_ptr[samples - 1u];
    if (raw != 0u)
      (void)cgba_sh4_arm_pcm_clamp_step(&st, (int16_t)eswap16(raw));
  } else
#endif
  {
    for (i = 0; i < samples; i++) {
      u16 raw = src_ptr[i];
      if (raw == 0u) {
        st.r3 = 0u;
        st.r12 = 0u;
        dst_ptr[i] = 0u;
      } else {
        dst_ptr[i] = cgba_sh4_arm_pcm_clamp_step(&st, (int16_t)eswap16(raw));
      }
    }
  }

  reg[0] = r0 + in_bytes;
  reg[1] = r1 + out_bytes;
  reg[3] = st.r3;
  reg[12] = st.r12;
  cgba_sh4_extra_cycles = (int)spent;
  return 1;
}
#endif

#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
#include "ports/fxcg100/sh4/sh4_ea_mixer.h"
#include "ports/fxcg100/sh4/sh4_gax_mixer.h"

/* Return from a skipped soundless audio mixer via lr.
 *
 * Intentionally caps the cycle debit to the current event slice (budget) and
 * returns 2 (block exit -> update_gba) when est_cycles >= budget: multi-slice
 * stepping inside a soundless mixer skip would fire HBlank/timer IRQs with
 * post-mixer PC/registers and shift frame timing/hashes tuned on CACHESIM. */
static inline int cgba_sh4_return_via_lr(u32 lr, u32 est_cycles, s32 budget)
{
  if (lr & 1u) {
    reg[REG_CPSR] |= 0x20u;
    reg[REG_PC] = lr & ~1u;
  } else {
    reg[REG_CPSR] &= ~0x20u;
    reg[REG_PC] = lr & ~3u;
  }
  if (budget > 0 && est_cycles < (u32)budget) {
    cgba_sh4_extra_cycles = (int)est_cycles;
    return 1;
  }
  cgba_sh4_extra_cycles = (budget > 0) ? (int)budget : 1;
  return 2;
}

int cgba_sh4_ea_mixer_try(u32 opcode, u32 pc)
{
  u32 r0 = reg[0];
  u32 r1 = reg[1];
  u32 count = reg[2];
  u32 lr = reg[REG_LR];
  u32 r0_reg, r0_off, r0_mask, r1_reg, r1_off, r1_mask;
  u8 *r0_data;
  const u8 *r0_tags;
  u32 ch_off, nch, active_ch = 0u, skipped_ch = 0u;
  uint32_t lead, body, tail;
  u32 est_cycles;
  s32 budget = cgba_sh4_fastpath_budget;
  (void)opcode;
  (void)pc;

  cgba_sh4_extra_cycles = 0;
  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u))
    return 0;

  if (count == 0u) {
    est_cycles = cgba_sh4_ea_mixer_estimate_cycles(0u, r1, 0u, 0u);
    reg[0] = r1;
    return cgba_sh4_return_via_lr(lr, est_cycles, budget);
  }

  if (count > CGBA_SH4_EA_MIXER_MAX_SAMPLES)
    return 0;

  r0_reg = r0 >> 24;
  if ((r0 & 3u) != 0u)
    return 0;
  if (r0_reg == 0x03u) {
    r0_mask = 0x7FFFu;
    r0_data = iwram + 0x8000u;
    r0_tags = iwram;
  } else if (r0_reg == 0x02u) {
    r0_mask = 0x3FFFFu;
    r0_data = ewram;
    r0_tags = ewram + 0x40000u;
  } else {
    return 0;
  }
  r0_off = r0 & r0_mask;
  if (r0_off + 28u + 20u > r0_mask + 1u)
    return 0;

  r1_reg = r1 >> 24;
  if (r1_reg == 0x03u) {
    r1_mask = 0x7FFFu;
  } else if (r1_reg == 0x02u) {
    r1_mask = 0x3FFFFu;
  } else {
    return 0;
  }
  r1_off = r1 & r1_mask;
  if (r1_off + count > r1_mask + 1u)
    return 0;

  /* Pass 1: validate every channel entry, sentinel rewind, SMC tags, and
   * non-overlap before mutating any state. */
  ch_off = r0_off + 28u;
  for (nch = 0u; nch < CGBA_SH4_EA_MIXER_MAX_CHANNELS; nch++) {
    u32 ptr, vol;
    if (ch_off + 20u > r0_mask + 1u)
      return 0;
    ptr = readaddress32(r0_data, ch_off + 0u);
    vol = readaddress32(r0_data, ch_off + 4u);
    if (vol == 255u) {
      if (ptr != (nch + 1u) * 20u)
        return 0;
      break;
    }
    if (vol != 0u) {
      u32 src_reg = ptr >> 24;
      if (src_reg != 0x02u && src_reg != 0x03u &&
          (src_reg < 0x08u || src_reg > 0x0Du))
        return 0;
      if (address32(r0_tags, ch_off + 0u) != 0u ||
          address32(r0_tags, ch_off + 8u) != 0u)
        return 0;
      active_ch++;
    } else {
      skipped_ch++;
    }
    ch_off += 20u;
  }
  if (nch >= CGBA_SH4_EA_MIXER_MAX_CHANNELS)
    return 0;

  {
    u32 r0_end = ch_off + 20u;
    u32 r1_end = r1_off + count;
    if (r0_reg == r1_reg && !(r1_end <= r0_off || r0_end <= r1_off))
      return 0;
  }

  /* Pass 2: advance each active channel's (ptr, frac) in O(channels). */
  cgba_sh4_ea_mixer_split_counts(count, r1, &lead, &body, &tail);
  (void)lead; (void)body;

  ch_off = r0_off + 28u;
  for (u32 i = 0u; i < nch; i++, ch_off += 20u) {
    u32 vol = readaddress32(r0_data, ch_off + 4u);
    if (vol == 0u)
      continue;
    uint32_t ptr = readaddress32(r0_data, ch_off + 0u);
    uint32_t frac = readaddress32(r0_data, ch_off + 8u);
    uint32_t frac_inc = readaddress32(r0_data, ch_off + 12u);
    uint32_t int_inc = readaddress32(r0_data, ch_off + 16u);
    cgba_sh4_ea_mixer_advance_channel(&ptr, &frac, frac_inc, int_inc,
                                      count, tail);
    address32(r0_data, ch_off + 0u) = eswap32((u32)ptr);
    address32(r0_data, ch_off + 8u) = eswap32((u32)frac);
  }

  reg[0] = r1 + count;
  est_cycles = cgba_sh4_ea_mixer_estimate_cycles(count, r1, active_ch, skipped_ch);
  return cgba_sh4_return_via_lr(lr, est_cycles, budget);
}

static inline int cgba_sh4_mixer_ram_struct(u32 addr, u32 size,
                                            u8 **data_out, const u8 **tags_out,
                                            u32 *off_out)
{
  u32 r = addr >> 24;
  u32 off, mask;
  if ((addr & 3u) != 0u)
    return 0;
  if (r == 0x03u) {
    mask = 0x7FFFu;
    *data_out = iwram + 0x8000u;
    *tags_out = iwram;
  } else if (r == 0x02u) {
    mask = 0x3FFFFu;
    *data_out = ewram;
    *tags_out = ewram + 0x40000u;
  } else {
    return 0;
  }
  off = addr & mask;
  if (off + size > mask + 1u)
    return 0;
  *off_out = off;
  return 1;
}

static inline int cgba_sh4_mixer_src_region_ok(u32 addr)
{
  u32 r = addr >> 24;
  /* Exclude 0x0D when EEPROM backup is active so speculative metadata reads
   * never trigger read_eeprom() state-machine side effects. */
  if (r == 0x0Du && backup_type == BACKUP_EEPROM)
    return 0;
  return r == 0x02u || r == 0x03u || (r >= 0x08u && r <= 0x0Du);
}

static inline int cgba_sh4_mixer_ram_overlaps(u32 a, u32 alen, u32 b, u32 blen)
{
  u32 ra = a >> 24, rb = b >> 24;
  u32 mask, oa, ob;
  if (ra != rb || (ra != 0x02u && ra != 0x03u))
    return 0;
  mask = (ra == 0x03u) ? 0x7FFFu : 0x3FFFFu;
  oa = a & mask;
  ob = b & mask;
  if (oa + alen > mask + 1u || ob + blen > mask + 1u)
    return 1;
  return !(oa + alen <= ob || ob + blen <= oa);
}

int __attribute__((noinline)) cgba_sh4_4ch_resample_match_iwram(u32 off)
{
  uint32_t wbuf[CGBA_SH4_4CH_RESAMPLE_WORDS];
  unsigned i;
  if (off + CGBA_SH4_4CH_RESAMPLE_WORDS * 4u > 0x8000u)
    return 0;
  for (i = 0u; i < CGBA_SH4_4CH_RESAMPLE_WORDS; i++)
    wbuf[i] = readaddress32(iwram + 0x8000u, off + i * 4u);
  return cgba_sh4_4ch_resample_match_words(wbuf, CGBA_SH4_4CH_RESAMPLE_WORDS);
}

static inline int cgba_sh4_iwram_pc_lit32(u32 iw_off, u32 wi, u32 *out)
{
  u32 insn_off = iw_off + wi * 4u;
  u32 insn, lit_off;
  if (insn_off + 4u > 0x8000u)
    return 0;
  insn = readaddress32(iwram + 0x8000u, insn_off);
  lit_off = insn_off + 8u + (insn & 0xFFFu);
  if ((lit_off & 3u) != 0u || lit_off + 4u > 0x8000u)
    return 0;
  *out = readaddress32(iwram + 0x8000u, lit_off);
  return 1;
}

/* Soundless fast skips for Shin'en GAX, Vicarious Visions / Krawall, and
 * Eurocom / Factor 5 IWRAM stereo PCM mixers (see sh4_gax_mixer.h). */
int cgba_sh4_audio_mixer_try(u32 kind, u32 pc)
{
  u32 r0 = reg[0];
  u32 lr = reg[REG_LR];
  s32 budget = cgba_sh4_fastpath_budget;
  u8 *sdata = NULL;
  const u8 *stags = NULL;
  u32 soff = 0u;

  cgba_sh4_extra_cycles = 0;
  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u))
    return 0;

  if (kind == CGBA_SH4_AUDIO_MIXER_GAX_STD ||
      kind == CGBA_SH4_AUDIO_MIXER_GAX_PINGPONG) {
    uint32_t r1, r2, r3, r4, r5, r7, n_iters, est_c;
    u32 r1_reg, r3_before, r3_after, nzcv;
    if (!cgba_sh4_mixer_ram_struct(r0, 28u, &sdata, &stags, &soff))
      return 0;
    r1 = readaddress32(sdata, soff + 0u);
    r2 = readaddress32(sdata, soff + 4u);
    r3 = readaddress32(sdata, soff + 8u);
    r4 = readaddress32(sdata, soff + 12u);
    r5 = readaddress32(sdata, soff + 16u);
    r7 = readaddress32(sdata, soff + 24u);
    if ((s32)r3 <= 0 || r3 > CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES)
      return 0;
    if (!cgba_sh4_mixer_src_region_ok(r1) ||
        ((r2 >> 24) != 0x03u && (r2 >> 24) != 0x02u))
      return 0;
    if (cgba_sh4_mixer_ram_overlaps(r0, 28u, r2, ((r3 + 3u) >> 2) << 3))
      return 0;
    if (cgba_smc_tags_nonzero(stags + soff, 28u))
      return 0;
    r1_reg = r1 >> 24;
    if (kind == CGBA_SH4_AUDIO_MIXER_GAX_STD) {
      n_iters = cgba_sh4_gax_std_advance(&r4, r5, r7, r3);
      address32(sdata, soff + 12u) = eswap32((u32)r4);
      est_c = 20u + n_iters * (26u + 4u * (u32)ws_cyc_nseq[r1_reg][0] +
                               4u * (u32)ws_cyc_seq[r2 >> 24][1]);
    } else {
      uint32_t bounces = 0u;
      n_iters = cgba_sh4_gax_pingpong_advance(&r1, &r4, r5, &r7, r3, &bounces);
      address32(sdata, soff + 0u)  = eswap32((u32)r1);
      address32(sdata, soff + 12u) = eswap32((u32)r4);
      address32(sdata, soff + 24u) = eswap32((u32)r7);
      est_c = 36u + bounces * 16u +
              n_iters * (27u + 4u * (u32)ws_cyc_nseq[r1_reg][0] +
                         4u * (u32)ws_cyc_seq[r2 >> 24][1]);
    }
    r3_before = (u32)(r3 - ((n_iters - 1u) << 2));
    r3_after = r3_before - 4u;
    nzcv = ((r3_after & 0x80000000u) ? 0x80000000u : 0u) |
           ((r3_after == 0u)         ? 0x40000000u : 0u) |
           ((r3_before >= 4u)        ? 0x20000000u : 0u) |
           ((((r3_before ^ 4u) & (r3_before ^ r3_after)) & 0x80000000u)
              ? 0x10000000u : 0u);
    reg[REG_CPSR] = (reg[REG_CPSR] & ~0xF0000000u) | nzcv;
    reg[1] = (u32)r1;
    reg[2] = (u32)(r2 + (n_iters << 3));
    reg[3] = r3_after;
    return cgba_sh4_return_via_lr(lr, (u32)est_c, budget);
  }

  if (kind == CGBA_SH4_AUDIO_MIXER_VV_A ||
      kind == CGBA_SH4_AUDIO_MIXER_VV_B) {
    uint32_t src, dst, pos, end_pos, count, written, step, mode, loop_len;
    uint32_t iters = 0u, est_c;
    u32 op_step, op_br, iw_off = pc & 0x7FFFu;
    int rev = 0;

    if (!cgba_sh4_mixer_ram_struct(r0, 40u, &sdata, &stags, &soff))
      return 0;
    src      = readaddress32(sdata, soff + 0x00u);
    dst      = readaddress32(sdata, soff + 0x04u);
    pos      = readaddress32(sdata, soff + 0x08u);
    end_pos  = readaddress32(sdata, soff + 0x0Cu);
    count    = readaddress32(sdata, soff + 0x10u);
    written  = readaddress32(sdata, soff + 0x14u);
    step     = readaddress32(sdata, soff + 0x1Cu);
    mode     = readaddress32(sdata, soff + 0x20u);
    loop_len = readaddress32(sdata, soff + 0x24u);

    if (mode > (kind == CGBA_SH4_AUDIO_MIXER_VV_A ? 2u : 1u))
      return 0;
    if (count > CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES || written > count)
      return 0;
    if (!cgba_sh4_mixer_src_region_ok(src) ||
        ((dst >> 24) != 0x03u && (dst >> 24) != 0x02u))
      return 0;
    if (cgba_sh4_mixer_ram_overlaps(
          r0, 40u, dst + (written << 1),
          ((written < count ? (count - written) : 1u) + (mode == 2u ? 1u : 0u)) << 1))
      return 0;
    if (cgba_smc_tags_nonzero(stags + soff, 40u))
      return 0;
    if (mode <= 1u) {
      if (kind == CGBA_SH4_AUDIO_MIXER_VV_A) {
        op_step = readaddress32(iwram + 0x8000u, iw_off + (mode ? 41u : 24u) * 4u);
        op_br   = readaddress32(iwram + 0x8000u, iw_off + (mode ? 45u : 28u) * 4u);
      } else {
        op_step = readaddress32(iwram + 0x8000u, iw_off + (mode ? 39u : 23u) * 4u);
        op_br   = readaddress32(iwram + 0x8000u, iw_off + (mode ? 43u : 27u) * 4u);
      }
      if (op_step == 0xE0822008u && (op_br >> 24) == 0xBAu)
        rev = 0;
      else if (op_step == 0xE0422008u && (op_br >> 24) == 0xCAu)
        rev = 1;
      else
        return 0;
    }
    if (!cgba_sh4_vv_advance(mode, rev, &pos, end_pos, count, &written,
                             step, loop_len, &iters))
      return 0;
    address32(sdata, soff + 0x08u) = eswap32((u32)pos);
    address32(sdata, soff + 0x14u) = eswap32((u32)written);
    reg[0] = lr;
    reg[1] = (kind == CGBA_SH4_AUDIO_MIXER_VV_A) ? (u32)dst
                                                 : (u32)(dst + (written << 1));
    reg[2] = (u32)pos;
    reg[3] = (u32)written;
    est_c = 28u + iters * ((mode == 0u ? 13u : (mode == 1u ? 16u : 22u)) +
                           (u32)ws_cyc_nseq[src >> 24][0] +
                           (mode == 0u ? 1u : 2u) * (u32)ws_cyc_nseq[dst >> 24][0]);
    return cgba_sh4_return_via_lr(lr, (u32)est_c, budget);
  }

  if (kind == CGBA_SH4_AUDIO_MIXER_EUROCOM_INTERP ||
      kind == CGBA_SH4_AUDIO_MIXER_EUROCOM_RAW) {
    u32 r1 = reg[1];
    uint32_t cur, end_p, loop_start, loop_len, samples_done = 0u, est_c;
    u32 cur_reg;
    uint16_t frac, step;
    int ended;

    if (!cgba_sh4_mixer_ram_struct(r0, 28u, &sdata, &stags, &soff))
      return 0;
    if ((r1 & 1u) != 0u || ((r1 >> 24) != 0x03u && (r1 >> 24) != 0x02u))
      return 0;
    if (cgba_sh4_mixer_ram_overlaps(r0, 28u, r1, 512u))
      return 0;
    if (cgba_smc_tags_nonzero(stags + soff, 28u))
      return 0;
    cur = readaddress32(sdata, soff + 4u);
    end_p = readaddress32(sdata, soff + 8u);
    if ((s32)(end_p - cur) < 0)
      return 0; /* reverse playback: fall back to native ARM code */
    if (!cgba_sh4_mixer_src_region_ok(cur))
      return 0;
    cur_reg = cur >> 24;
    loop_start = readaddress32(sdata, soff + 12u);
    loop_len   = readaddress32(sdata, soff + 16u);
    step       = readaddress16(sdata, soff + 20u);
    frac       = readaddress16(sdata, soff + 22u);
    if (step >= 0xF000u)
      return 0; /* ARM mvn r8, #0xF000 mask edge case: fall back to native */

    ended = cgba_sh4_eurocom_advance_fwd(&cur, &end_p, &frac, step,
                                         loop_start, loop_len, &samples_done);
    address32(sdata, soff + 8u) = eswap32((u32)end_p);
    if (!ended) {
      address32(sdata, soff + 4u) = eswap32((u32)cur);
      address16(sdata, soff + 22u) = eswap16((u16)frac);
    }
    reg[0] = (u32)ended;
    {
      u32 per_s = (kind == CGBA_SH4_AUDIO_MIXER_EUROCOM_INTERP)
                    ? (28u + 2u * (u32)ws_cyc_nseq[cur_reg][0] +
                       4u * (u32)ws_cyc_nseq[r1 >> 24][0])
                    : (19u + (u32)ws_cyc_nseq[cur_reg][0] +
                       4u * (u32)ws_cyc_nseq[r1 >> 24][0]);
      est_c = 28u + samples_done * per_s;
    }
    return cgba_sh4_return_via_lr(lr, (u32)est_c, budget);
  }

  if (kind >= CGBA_SH4_AUDIO_MIXER_4CH_SUM1 &&
      kind <= CGBA_SH4_AUDIO_MIXER_4CH_SUM4) {
    u32 nch = (kind - CGBA_SH4_AUDIO_MIXER_4CH_SUM1) + 1u;
    u32 n = reg[1];
    u32 dst = reg[2];
    u32 s0 = reg[3], s1 = reg[5], s2 = reg[7], s3 = reg[9], lut = reg[12];
    u32 per_s, est_c;
    if ((s32)n <= 0 || n > CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES)
      return 0;
    if (((dst >> 24) != 0x03u && (dst >> 24) != 0x02u) ||
        !cgba_sh4_mixer_src_region_ok(s0) ||
        !cgba_sh4_mixer_src_region_ok(lut))
      return 0;
    if ((nch >= 2u && !cgba_sh4_mixer_src_region_ok(s1)) ||
        (nch >= 3u && !cgba_sh4_mixer_src_region_ok(s2)) ||
        (nch >= 4u && !cgba_sh4_mixer_src_region_ok(s3)))
      return 0;
    /* Reject dst overlapping the pushed lr slot [sp-4, sp) or input streams. */
    if (cgba_sh4_mixer_ram_overlaps(dst, n, reg[13] - 4u, 4u) ||
        cgba_sh4_mixer_ram_overlaps(dst, n, s0, n) ||
        (nch >= 2u && cgba_sh4_mixer_ram_overlaps(dst, n, s1, n)) ||
        (nch >= 3u && cgba_sh4_mixer_ram_overlaps(dst, n, s2, n)) ||
        (nch >= 4u && cgba_sh4_mixer_ram_overlaps(dst, n, s3, n)))
      return 0;
    {
      u32 last_off = n - 1u;
      s32 sum;
      cgba_sh4_begin_mem_helper(pc + 4u);
      sum = (s32)(s8)read_memory8(s0 + last_off);
      if (nch >= 2u) sum += (s32)(s8)read_memory8(s1 + last_off);
      if (nch >= 3u) sum += (s32)(s8)read_memory8(s2 + last_off);
      if (nch >= 4u) sum += (s32)(s8)read_memory8(s3 + last_off);
      reg[0] = (u32)(u8)read_memory8(lut + (u32)sum);
    }
    per_s = 6u + 2u * nch + (u32)ws_cyc_nseq[s0 >> 24][0] +
            (u32)ws_cyc_nseq[lut >> 24][0] + (u32)ws_cyc_nseq[dst >> 24][0];
    if (nch >= 2u) per_s += (u32)ws_cyc_nseq[s1 >> 24][0];
    if (nch >= 3u) per_s += (u32)ws_cyc_nseq[s2 >> 24][0];
    if (nch >= 4u) per_s += (u32)ws_cyc_nseq[s3 >> 24][0];
    reg[1] = 0u;
    reg[2] = dst + n;
    reg[3] = s0 + n;
    if (nch >= 2u) reg[5] = s1 + n;
    if (nch >= 3u) reg[7] = s2 + n;
    if (nch >= 4u) reg[9] = s3 + n;
    reg[11] = 0xFFu;
    reg[REG_CPSR] = (reg[REG_CPSR] & ~0xF0000000u) | 0x60000000u;
    est_c = 12u + n * per_s;
    /* Epilogue is `ldmfd sp!, {pc}` (ARMv4T never switches to Thumb on odd lr). */
    return cgba_sh4_return_via_lr(lr & ~3u, est_c, budget);
  }

  if (kind == CGBA_SH4_AUDIO_MIXER_4CH_RESAMPLE) {
    u32 iw_off = pc & 0x7FFFu;
    u32 flag_addr, silence_ptr, ch_ptrs[4];
    u8 *fdata = NULL, *mdata = NULL, *cdata[4];
    const u8 *ftags = NULL, *mtags = NULL, *ctags[4];
    u32 foff = 0u, moff = 0u, coff[4];
    u32 n_samples, mix_buf, pcm_buf, est_c, ch_ws = 0u;
    unsigned ci;

    if (!cgba_sh4_iwram_pc_lit32(iw_off, 3u, &flag_addr) ||
        !cgba_sh4_iwram_pc_lit32(iw_off, 6u, &ch_ptrs[0]) ||
        !cgba_sh4_iwram_pc_lit32(iw_off, 15u, &ch_ptrs[1]) ||
        !cgba_sh4_iwram_pc_lit32(iw_off, 46u, &silence_ptr) ||
        !cgba_sh4_iwram_pc_lit32(iw_off, 90u, &ch_ptrs[2]) ||
        !cgba_sh4_iwram_pc_lit32(iw_off, 99u, &ch_ptrs[3]))
      return 0;
    if (!cgba_sh4_mixer_ram_struct(flag_addr, 4u, &fdata, &ftags, &foff) ||
        cgba_smc_tags_nonzero(ftags + foff, 4u))
      return 0;
    if (!cgba_sh4_mixer_ram_struct(reg[11], 0x4Cu, &mdata, &mtags, &moff))
      return 0;
    if (fdata == mdata && foff + 4u > moff && foff < moff + 0x4Cu)
      return 0;
    n_samples = readaddress16(mdata, moff + 0x40u);
    if (n_samples == 0u || n_samples > CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES)
      return 0;
    pcm_buf = readaddress32(mdata, moff + 0x44u);
    mix_buf = readaddress32(mdata, moff + 0x48u);
    if (((pcm_buf >> 24) != 0x03u && (pcm_buf >> 24) != 0x02u) ||
        ((mix_buf >> 24) != 0x03u && (mix_buf >> 24) != 0x02u))
      return 0;
    if (cgba_sh4_mixer_ram_overlaps(mix_buf, n_samples * 4u, flag_addr, 4u) ||
        cgba_sh4_mixer_ram_overlaps(mix_buf, n_samples * 4u, reg[11], 0x4Cu) ||
        cgba_sh4_mixer_ram_overlaps(pcm_buf, n_samples, flag_addr, 4u) ||
        cgba_sh4_mixer_ram_overlaps(pcm_buf, n_samples, reg[11], 0x4Cu))
      return 0;
    for (ci = 0u; ci < 4u; ci++) {
      if (!cgba_sh4_mixer_ram_struct(ch_ptrs[ci], 0x34u,
                                     &cdata[ci], &ctags[ci], &coff[ci]) ||
          cgba_smc_tags_nonzero(ctags[ci] + coff[ci], 0x34u) ||
          (fdata == cdata[ci] && foff + 4u > coff[ci] && foff < coff[ci] + 0x34u) ||
          cgba_sh4_mixer_ram_overlaps(mix_buf, n_samples * 4u, ch_ptrs[ci], 0x34u) ||
          cgba_sh4_mixer_ram_overlaps(pcm_buf, n_samples, ch_ptrs[ci], 0x34u))
        return 0;
    }

    /* Validate inst_ptr before mutating any channel state so decline is atomic. */
    {
      uint32_t src[4], end[4], step[4], pos[4];
      uint16_t active[4];
      uint8_t loop_flag[4] = {0, 0, 0, 0};
      uint32_t loop_start[4] = {0, 0, 0, 0};
      uint32_t loop_end[4] = {0, 0, 0, 0};
      for (ci = 0u; ci < 4u; ci++) {
        u32 inst_ptr = readaddress32(cdata[ci], coff[ci] + 0x00u);
        src[ci]    = readaddress32(cdata[ci], coff[ci] + 0x04u);
        end[ci]    = readaddress32(cdata[ci], coff[ci] + 0x08u);
        pos[ci]    = readaddress32(cdata[ci], coff[ci] + 0x0Cu);
        step[ci]   = readaddress32(cdata[ci], coff[ci] + 0x10u);
        active[ci] = readaddress16(cdata[ci], coff[ci] + 0x16u);
        if (!cgba_sh4_mixer_src_region_ok(src[ci]))
          return 0;
        {
          uint32_t eff_step = (active[ci] == 0u) ? 0u : step[ci];
          int may_hit_end = (pos[ci] >= end[ci]) ||
            (eff_step != 0u &&
             ((uint64_t)pos[ci] + (uint64_t)(n_samples - 1u) * (uint64_t)eff_step >=
              (uint64_t)end[ci]));
          if (may_hit_end) {
            if ((inst_ptr & 3u) != 0u || !cgba_sh4_mixer_src_region_ok(inst_ptr))
              return 0;
            cgba_sh4_begin_mem_helper(pc + 4u);
            loop_flag[ci] = (uint8_t)read_memory8(inst_ptr + 0x16u);
            if (loop_flag[ci] != 0u) {
              loop_start[ci] = (uint32_t)read_memory32(inst_ptr + 0x0Cu);
              loop_end[ci]   = (uint32_t)read_memory32(inst_ptr + 0x10u);
              if (loop_end[ci] <= loop_start[ci])
                return 0;
            }
          }
        }
        ch_ws += (u32)ws_cyc_nseq[src[ci] >> 24][0];
      }

      /* Commit: zero the busy flag and advance all 4 channels in O(1). */
      address32(fdata, foff) = 0u;
      for (ci = 0u; ci < 4u; ci++) {
        uint16_t old_active = active[ci];
        cgba_sh4_4ch_chan_advance(&src[ci], &end[ci], &step[ci], &pos[ci],
                                  &active[ci], silence_ptr, loop_flag[ci],
                                  loop_start[ci], loop_end[ci], n_samples);
        address32(cdata[ci], coff[ci] + 0x04u) = eswap32((u32)src[ci]);
        address32(cdata[ci], coff[ci] + 0x08u) = eswap32((u32)end[ci]);
        address32(cdata[ci], coff[ci] + 0x0Cu) = eswap32((u32)pos[ci]);
        if (active[ci] != old_active) {
          if (ci >= 2u)
            address32(cdata[ci], coff[ci] + 0x10u) = eswap32((u32)step[ci]);
          address16(cdata[ci], coff[ci] + 0x16u) = eswap16((u16)active[ci]);
        }
      }
    }
    reg[REG_CPSR] = (reg[REG_CPSR] & ~0xF0000000u) | 0x60000000u;
    est_c = 120u + n_samples * (55u + ch_ws +
                                4u * (u32)ws_cyc_nseq[mix_buf >> 24][1] +
                                (u32)ws_cyc_nseq[pcm_buf >> 24][0]);
    return cgba_sh4_return_via_lr(lr, est_c, budget);
  }

  return 0;
}
#endif

#ifdef CGBA_SH4_M4A_LOOP_FOLD
#include "ports/fxcg100/sh4/sh4_m4a_skip.h"
/* Runtime half of the m4a loop fold (see sh4_m4a_skip.h).  The emitted prefix
 * snapshots the live R13 budget, calls this through the ordinary op2
 * trampoline (which makes reg[REG_CPSR] and guest r0 authoritative around the
 * call), then debits cgba_sh4_extra_cycles and falls into the translated loop
 * head.  `kind` is the loop's offset inside the verified image. */
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_m4a_loop_try_count;
u32 cgba_sh4_m4a_loop_cycles;
#endif

int cgba_sh4_m4a_loop_try(u32 packed, u32 pc)
{
  cgba_sh4_m4a_loop_costs costs;
  u32 kind = packed & 0xFFu;
  u32 stride = (packed >> 8) & 0xFFFu;
  u32 region = (pc >> 24) & 0x0Fu;
  s32 budget = cgba_sh4_fastpath_budget;
  u32 spent = 0;

  cgba_sh4_extra_cycles = 0;
  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u) || budget <= 0)
    return 0;
  costs.seq = ws_cyc_seq[region][1];
  costs.branch = ws_cyc_nseq[region][1];
  costs.mem = (const uint8_t (*)[2])ws_cyc_nseq;

  if (kind == CGBA_SH4_M4A_KIND_REVERB) {
    uint32_t r4 = reg[4], r5 = reg[5], r7 = reg[7], cpsr = reg[REG_CPSR];
    spent = cgba_sh4_m4a_fold_reverb(&r4, &r5, reg[6], &r7, &cpsr, budget,
                                     &costs);
    if (spent) {
      reg[4] = r4; reg[5] = r5; reg[7] = r7; reg[REG_CPSR] = cpsr;
    }
  } else if (kind == CGBA_SH4_M4A_KIND_MONO_REVERB) {
    uint32_t r4 = reg[4], r5 = reg[5], r7 = reg[7], cpsr = reg[REG_CPSR];
    spent = cgba_sh4_m4a_fold_reverb_mono(&r4, &r5, &r7, &cpsr, budget, &costs);
    if (spent) {
      reg[4] = r4; reg[5] = r5; reg[7] = r7; reg[REG_CPSR] = cpsr;
    }
  } else if (kind == CGBA_SH4_M4A_KIND_FIXED ||
             kind == CGBA_SH4_M4A_KIND_MONO_FIXED) {
    uint32_t r3 = reg[3], r5 = reg[5], r8 = reg[8], cpsr = reg[REG_CPSR];
    spent = cgba_sh4_m4a_fold_fixed(&r3, &r5, &r8, &cpsr, stride, budget,
                                    &costs);
    if (spent) {
      reg[3] = r3; reg[5] = r5; reg[8] = r8; reg[REG_CPSR] = cpsr;
    }
  } else if (kind == CGBA_SH4_M4A_KIND_INTERP_A ||
             kind == CGBA_SH4_M4A_KIND_INTERP_B ||
             kind == CGBA_SH4_M4A_KIND_MONO_INTERP_A ||
             kind == CGBA_SH4_M4A_KIND_MONO_INTERP_B) {
    cgba_sh4_m4a_interp_state s;
    int swap_r9_lr = (kind == CGBA_SH4_M4A_KIND_INTERP_B ||
                      kind == CGBA_SH4_M4A_KIND_MONO_INTERP_B);
    s.r2 = reg[2]; s.r3 = reg[3]; s.r4 = reg[4]; s.r5 = reg[5];
    s.r8 = reg[8];
    s.r9 = swap_r9_lr ? reg[REG_LR] : reg[9];
    s.lr = swap_r9_lr ? reg[9] : reg[REG_LR];
    s.cpsr = reg[REG_CPSR];
    spent = cgba_sh4_m4a_fold_interp_stride(&s, stride, budget, &costs);
    if (spent) {
      reg[2] = s.r2; reg[3] = s.r3; reg[5] = s.r5; reg[8] = s.r8;
      if (swap_r9_lr) {
        reg[REG_LR] = s.r9; reg[9] = s.lr;
      } else {
        reg[9] = s.r9; reg[REG_LR] = s.lr;
      }
      reg[REG_CPSR] = s.cpsr;
    }
  }
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_m4a_loop_try_count++;
  cgba_sh4_m4a_loop_cycles += spent;
#endif
  cgba_sh4_extra_cycles = (int)spent;
  return spent != 0;
}
#endif

#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
/*
 * Runtime half of the signature-gated Thumb libgcc divide-loop fast path.
 * Generated code snapshots the live R13 budget into cgba_sh4_fastpath_budget
 * before using the ordinary op2 trampoline, which also makes reg[REG_CPSR]
 * authoritative around the C call.  The model commits nothing unless the whole
 * pure-register loop fits before the next scheduler boundary.  A failed try
 * therefore falls through to the byte-for-byte ordinary translation with
 * unchanged guest state.
 */
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_thumb_udiv_try_count;
u32 cgba_sh4_thumb_udiv_hit_count;
u32 cgba_sh4_thumb_udiv_budget_fallback_count;
#endif

int cgba_sh4_thumb_udiv_loop_try(u32 unused, u32 pc)
{
  cgba_sh4_thumb_udiv_loop_result out;
  u32 region = (pc >> 24) & 0x0Fu;
  (void)unused;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_thumb_udiv_try_count++;
#endif
  if (CGBA_SH4_DIFF_ACTIVE() || !(reg[REG_CPSR] & 0x20u))
    return 0;

  out = cgba_sh4_thumb_udiv_loop_run(
    reg[0], reg[1], reg[2], reg[3], reg[4], reg[REG_CPSR],
    (u32)ws_cyc_seq[region][0], (u32)ws_cyc_nseq[region][0]);
  if (!cgba_sh4_thumb_udiv_loop_budget_ok(cgba_sh4_fastpath_budget,
                                           out.cycles)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    cgba_sh4_thumb_udiv_budget_fallback_count++;
#endif
    return 0;
  }

  reg[0] = out.r0;
  reg[1] = out.r1;
  reg[2] = out.r2;
  reg[3] = out.r3;
  reg[4] = out.r4;
  reg[REG_CPSR] = out.cpsr;
  cgba_sh4_extra_cycles = (int)out.cycles;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_thumb_udiv_hit_count++;
#endif
  return 1;
}
#endif

