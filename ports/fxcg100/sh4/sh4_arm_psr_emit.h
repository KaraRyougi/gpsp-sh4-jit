/* Native SH-4A ARM MRS/MSR emitter. Included via sh4_thumb_dp_emit.h. */
#ifndef CGBA_SH4_ARM_PSR_EMIT_H
#define CGBA_SH4_ARM_PSR_EMIT_H

/* R0 = (reg[CPU_MODE] & 0xF) * 4 and base_rn = &spsr[0], ready for the
 * @(R0,Rn) indexed forms. REG_SPSR/REG_MODE discard the privilege bit the
 * same way. */
static inline void sh4g_psr_spsr_index(u8 **tp, unsigned base_rn)
{
  sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_RET);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_and_imm(&cg, 0xF);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)spsr, base_rn);
}

/* Native MRS/MSR. MRS cpsr is a plain register copy: reg[REG_CPSR] carries
 * the full canonical flag set in this port (set_nzcv / the helpers write
 * CPSR bits directly), so no flag collapse pass is needed. SPSR forms are
 * plain spsr[mode & 0xF] array reads/masked merges — no banking or IRQ
 * side effects. MSR cpsr merges under gpSP's privileged field mask with
 * three runtime guards to the C helper: USER mode (restricted mask), a
 * Thumb bit change, and an IRQ-enabled result with an interrupt actually
 * pending (needs the vector + redispatch). Mode changes JSR the resident
 * set_cpu_mode routine (sh4g_psr_emit_rebank_routine, sh4_fastmem.c
 * buffer) — full FIQ semantics, no block-byte or I-cache cost at the
 * site. The hot IRQ-dispatcher bracket — mrs spsr, two mode-switching
 * msr cpsr_cf, msr spsr — stays fully native. MSR with a PC operand
 * stays on C. */
static inline int sh4g_arm_psr_native(u8 **tp, u32 opcode, u32 pc, int cycles)
{
  u32 is_msr   = (opcode >> 21) & 1;
  u32 use_spsr = (opcode >> 22) & 1;

  if (!is_msr) {                                         /* MRS rd, psr */
    u32 rd = (opcode >> 12) & 0xF;
    if (rd == 15)
      return 0;
    if (use_spsr) {                                      /* rd = spsr[mode] */
      sh4g_psr_spsr_index(tp, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
    } else {
      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
    }
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    return 1;
  }

  {                                                      /* MSR cpsr_<f>, op2 */
    u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
    u32 is_imm = (opcode >> 25) & 1;
    u32 val = 0, rm = 0;
    if (is_imm) {
      u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
      val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else {
      rm = opcode & 0xF;
      if (rm == 15)
        return 0;
    }
    if (pfield == 0)
      return 0;

    if (use_spsr) {                    /* MSR spsr_<f>: masked array merge
                                        * (gpSP spsr_masks — bit4 of the
                                        * control byte is architecturally
                                        * fixed and excluded). */
      u32 mask = (pfield == 3) ? 0xF00000EFu
               : (pfield == 2) ? 0xF0000000u : 0x000000EFu;
      sh4g_psr_spsr_index(tp, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      sh4g_const(tp, ~mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (is_imm)
        sh4g_const(tp, val & mask, SH4_REG_T1);
      else {
        sh4g_load_greg(tp, rm, SH4_REG_T1);
        sh4g_const(tp, mask, SH4_REG_ARG1);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_and(&cg, SH4_REG_ARG1, SH4_REG_T1);
          sh4g_close(tp, &cg); }
      }
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T0, SH4_REG_T2);
        sh4g_close(tp, &cg); }
      return 1;
    }

    if (pfield == 2) {                 /* flags only: mask is 0xF0000000 in
                                        * BOTH privilege levels -> no guards */
      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
      sh4g_const(tp, 0x0FFFFFFFu, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (is_imm)
        sh4g_const(tp, val & 0xF0000000u, SH4_REG_T1);
      else {
        sh4g_load_greg(tp, rm, SH4_REG_T1);
        sh4g_const(tp, 0xF0000000u, SH4_REG_T2);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_and(&cg, SH4_REG_T2, SH4_REG_T1);
          sh4g_close(tp, &cg); }
      }
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, SH4_GREG_CPSR);
        sh4g_close(tp, &cg); }
      return 1;
    }

    {                                  /* control byte written (pfield 1 / 3) */
      u32 mask = (pfield == 3) ? 0xF00000EFu : 0x000000EFu;
      u8 *to_slow[2], *to_store[3], *done, *skip_bank;
      int ns = 0, nst = 0, i;

      if (!cgba_sh4_psr_rebank_routine)  /* dynarec init not run yet */
        return 0;

      /* Keep the open disp8 BT/BF branches (to_slow[], to_store[]) free of
       * inline literal-pool flushes. */
      sh4g_litseg_reserve_bytes(tp, 6, 160u);

      if (is_imm) sh4g_const(tp, val, SH4_REG_ARG3);     /* R7 = value */
      else        sh4g_load_greg(tp, rm, SH4_REG_ARG3);

      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1); /* R5 = old CPSR */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, ~mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);       /* old & ~mask */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_T2);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T2);       /* val & mask */
        sh4_emit_or(&cg, SH4_REG_T2, SH4_REG_T0);        /* T0 = new CPSR */
        sh4g_close(tp, &cg); }

      /* USER mode -> C (restricted mask). Privileged CPU_MODE values are
       * 0x10.. (PRIVMODE = mode >> 4); MODE_USER is 0x00. */
      sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_tst_imm(&cg, 0x10);                     /* T=1: USER */
        sh4g_close(tp, &cg); }
      to_slow[ns++] = sh4g_emit_bt_placeholder(tp);

      /* Thumb bit change -> C (execution-state switch mid-block) */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0x20);
        sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T=1: unchanged */
        sh4g_close(tp, &cg); }
      to_slow[ns++] = sh4g_emit_bf_placeholder(tp);

      /* IRQs disabled in the NEW value -> nothing can fire, store directly.
       * (Checked on the new I bit, not old^new: an already-pending IRQ with
       * I clear must be taken now, exactly like check_for_interrupts.) */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, 0x80);                     /* T=1: IRQs enabled */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bf_placeholder(tp);

      /* IRQs enabled: pending? IE & IF nonzero-ness is byte-order neutral. */
      sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x200), SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);    /* IE */
        sh4_emit_add_imm(&cg, 2, SH4_REG_T2);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG2);
        sh4_emit_extu_w(&cg, SH4_REG_ARG2, SH4_REG_ARG2);/* IF */
        sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_ARG2);     /* T=1: none pending */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 6, SH4_REG_T2);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
        sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T=1: IME == 0 */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bt_placeholder(tp);

      /* fall-through: an IRQ will fire -> full C helper (vector + exit).
       * with_cycles MUST be 1: sh4_op2_pc_tramp unconditionally reads the
       * fifth (cycle_count) literal on its PC-change exit — a 4-literal site
       * makes it debit its own instruction bytes from the cycle counter,
       * which detonated exactly when gameplay IRQ traffic started. */
      for (i = 0; i < ns; i++)
        sh4g_patch_cond(to_slow[i], *tp);
      sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_tramp,
                          (const void *)cgba_sh4_arm_psr, opcode, pc,
                          1, cycles);
      done = sh4g_emit_bra_placeholder(tp);

      for (i = 0; i < nst; i++)
        sh4g_patch_cond(to_store[i], *tp);

      /* Committed — no more bails. Same CPSR mode nibble (same gpSP mode by
       * construction): plain CPSR store, keeping the hot I-bit-toggle
       * bracket inline. Nibble changed: JSR the resident set_cpu_mode
       * routine (sh4_fastmem.c buffer — outside the translation caches, so
       * the ~110-byte re-bank body costs no block bytes or I-cache reach;
       * inlining it here measured +59% imiss on AW). The routine handles
       * FIQ's 7-register bank and preserves R1 = merged CPSR. */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0xF);
        sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T=1: same mode */
        sh4g_close(tp, &cg); }
      skip_bank = sh4g_emit_bt_placeholder(tp);

      sh4g_const(tp, (u32)(uintptr_t)cgba_sh4_psr_rebank_routine, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_jsr(&cg, SH4_REG_RET);
        sh4_emit_nop(&cg);
        sh4g_close(tp, &cg); }

      sh4g_patch_cond(skip_bank, *tp);
      sh4g_store_greg(tp, SH4_REG_T0, SH4_GREG_CPSR);
      sh4g_patch_bra(done, *tp);
      return 1;
    }
  }
}

#endif /* CGBA_SH4_ARM_PSR_EMIT_H */
