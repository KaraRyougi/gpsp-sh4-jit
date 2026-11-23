/* Internal module included by sh4_interp_helpers.c: cold interp, reset & entry. */
/* Interpret a small chunk of not-yet-hot code (cold-code gate). Entered from
 * sh4_cold_interp_entry with R4 = remaining JIT cycles; same return contract
 * as the BIOS fallback: new cycle budget, or 0x80000000 when the frame
 * completed inside the interpreter. */
u32 cgba_sh4_cold_interp(u32 cycles)
{
  s32 budget = (s32)cycles;
  s32 limit = (s32)CGBA_SH4_COLD_CHUNK_CYCLES;
  s32 chunk = (limit > 0 && budget > limit) ? limit : budget;
  s32 sentinel = (s32)0x7FFFFFFF;
  s32 used;

  if (chunk <= 0)
    chunk = 1;                           /* always make forward progress */
#ifdef SH4_ARCH
  /* If a Thumb block compiled with the proven-IWRAM SP-span guard is entered
   * with a non-IWRAM stack (e.g., EWRAM coroutine stack at 0x02xxxxxx in
   * Shining Soul II), every invocation would otherwise bounce through this
   * 512-cycle interpreter fallback. After 16 such deopts, disable the IWRAM
   * SP-span specialization and flush once so shared Thumb blocks retranslate
   * with general fastmem (which natively handles both IWRAM and EWRAM). */
  if (!cgba_sp_non_iwram_disable &&
      (reg[REG_CPSR] & 0x20u) != 0u &&
      ((reg[13] - 0x03000000u) >= 0x8000u || (reg[13] & 3u) != 0u)) {
    u32 entry_pc = reg[REG_PC];
    u32 region = entry_pc >> 24;
    if (region == 0x02u || region == 0x03u ||
        (region >= 0x08u && region <= 0x0Du)) {
      u16 op = (u16)read_memory16(entry_pc);
      if ((op & 0xF000u) == 0x9000u && (op & 0xFFu) <= 15u) {
        if (++cgba_sp_non_iwram_deopts >= 16u) {
          cgba_sp_non_iwram_disable = 1u;
          flush_dynarec_caches();
        }
      }
    }
  }
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_cold_interp_count++;
#endif
  cgba_diff_stop_on_budget = 1;
  cgba_diff_stop_active = 1;
  cgba_diff_stop_skip_initial = 0;
  cgba_diff_stop_reslice = 0;
  cgba_diff_stop_cycles_remaining = sentinel;
  execute_arm((u32)chunk);
  cgba_diff_stop_active = 0;
  cgba_diff_stop_on_budget = 0;

  if (cgba_diff_stop_cycles_remaining == sentinel)
    return 0x80000000u;                  /* frame completed inside (cannot
                                            happen with the hard stops, but
                                            keep the safe interpretation) */
  used = chunk - cgba_diff_stop_cycles_remaining;
  budget -= used;

  /* A halt raised inside the chunk is handed back undigested: run the sleep
   * loop here (the stub funnels expect an ACTIVE cpu after this returns). */
  while (reg[CPU_HALT_STATE] != 0 /* CPU_ACTIVE */) {
    u32 ret = update_gba(budget);
    if (completed_frame(ret))
      return ret;
    budget = (s32)cycles_to_run(ret);
  }
  if (cgba_diff_stop_reslice || budget <= 0) {
    cgba_diff_stop_reslice = 0;
    u32 ret = update_gba(budget);
    return completed_frame(ret) ? ret : cycles_to_run(ret);
  }
  return (u32)budget;
}

void cgba_sh4_reset_hle_state(void)
{
  cgba_store_alert = CPU_ALERT_NONE;
  cgba_sh4_extra_cycles = 0;
  cgba_sh4_mem_cycle_seq = 0;
  cgba_fs_entry_cost = 0u;
  cgba_cs_entry_cost = 0u;
  cgba_oa_entry_cost = 0u;
  cgba_lz_entry_cost = 0u;
  cgba_ba_entry_cost = 0u;
  cgba_rl_entry_cost = 0u;
  cgba_swi_arctan_rem = 0;
#if CGBA_SH4_SWI_HLE_VERIFY
  cgba_swi_verify.pending = 0;
#endif
}

/* Host-emitter init hook (main.c calls this under HAVE_DYNAREC). The MIPS/x86
 * backends populate inline memory-handler dispatch tables here; the bring-up
 * SH4 backend routes memory through C helpers, so there is nothing to set up. */
void init_emitter(bool must_swap)
{
  (void)must_swap;
}

/* Public dynarec entry (emitter-provided in the other backends): run translated
 * code for up to `cycles`, with R5 pointing at the guest register file. */
u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}
