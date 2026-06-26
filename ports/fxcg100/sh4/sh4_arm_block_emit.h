#ifndef CGBA_SH4_ARM_BLOCK_EMIT_H
#define CGBA_SH4_ARM_BLOCK_EMIT_H

/*
 * Native SH-4A emission for ARM block transfer LDM (cgba_sh4_arm_block), the
 * #3 remaining lever (~194k C-dispatches/window). The register list is a
 * translate-time constant, so the transfer UNROLLS into a straight-line run of
 * fast-path word loads.
 *
 * Stage 1 = LDM only (loads raise no cpu_alert, so no store/SMC handling needed).
 * STM stays on C until a store fast path exists. We also bail to C for: r15 in
 * the list (PC load needs redispatch / the ^ SPSR path), the S bit (user-bank
 * transfer), rn==15, and an empty list.
 *
 * Address model (matches the oracle exactly): lowest-numbered register goes to
 * the lowest address; the run is `count` contiguous ascending words starting at
 * A = (base & ~3) + offset, with offset = IA:0 IB:+4 DB:-count*4 DA:-count*4+4.
 * Writeback stores new_base = base +/- count*4 (from the UNALIGNED base), and is
 * suppressed when the base register is itself in the list.
 *
 * The whole run is guarded to lie in ONE mapped host page (memory_map_read), so
 * a single resolve covers every word and a page-straddle / unmapped / BIOS run
 * falls back to the C helper. The guards use a short skip over a far BRA-to-slow
 * (sh4g_patch_cond truncates disp8, so a long conditional branch is unsafe).
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

extern u8 *memory_map_read[];                 /* gba_memory.c host-page table */
int  cgba_sh4_arm_block(u32 opcode, u32 pc);  /* C oracle / slow path */
void sh4_block_exit(u32 pc);                  /* redispatch entry (sh4_emit.h) */

/* Emit "branch to the slow path if (T == slow_if_t)" with no disp8 range limit:
 * a short skip over a far BRA placeholder. Returns the BRA site to patch later. */
static inline u8 *sh4g_block_guard(u8 **tp, int slow_if_t)
{
  { sh4_codegen cg = sh4g_open(tp);
    if (slow_if_t) sh4_emit_bf(&cg, 1);       /* T==0 -> skip BRA (stay fast)   */
    else           sh4_emit_bt(&cg, 1);       /* T==1 -> skip BRA               */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bra_placeholder(tp);       /* BRA 0 + NOP delay -> slow      */
}

/* Native ARM LDM, or 0 to fall back to C. */
static inline int sh4g_arm_block_native(u8 **tp, u32 opcode, u32 pc)
{
  u32 rn        = (opcode >> 16) & 0xF;
  u32 rlist     = opcode & 0xFFFF;
  u32 is_load   = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre       = (opcode >> 24) & 1;
  u32 up        = (opcode >> 23) & 1;
  u32 s_bit     = (opcode >> 22) & 1;
  u32 count = 0, i;
  int offset_a, offset_nb, do_wb;
  u8 *g_strad, *g_region, *g_page, *bra_done;

  if (!is_load)              return 0;         /* STM -> C (no store fast path yet) */
  if (s_bit)                 return 0;         /* user-bank / SPSR restore -> C */
  if (rlist & 0x8000)        return 0;         /* PC in list -> C (redispatch)  */
  if (rn == 15)              return 0;         /* base = PC -> C */

  for (i = 0; i < 16; i++) if (rlist & (1u << i)) count++;
  if (count == 0)            return 0;         /* empty list -> C (rare) */

  offset_a  = up ? (pre ? 4 : 0) : (pre ? -(int)(count * 4) : -(int)(count * 4) + 4);
  offset_nb = up ? (int)(count * 4) : -(int)(count * 4);
  /* LDM with the base in the list keeps the loaded value (no writeback). */
  do_wb = writeback && !(rlist & (1u << rn));

  /* base = reg[rn] in R1; A = (base & ~3) + offset_a in R5 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1);
    sh4_emit_shlr2(&cg, SH4_REG_ARG1);          /* clear low 2 bits (word-align) */
    sh4_emit_shll2(&cg, SH4_REG_ARG1);
    sh4_emit_add_imm(&cg, offset_a, SH4_REG_ARG1);   /* R5 = A */
    sh4g_close(tp, &cg); }

  /* straddle guard: (A >> 15) == ((A + count*4 - 1) >> 15) i.e. one host page */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);     /* R0 = A >> 15 (page idx) */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);
    sh4_emit_add_imm(&cg, (int)(count * 4) - 1, SH4_REG_T2);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_T2);      /* R3 = (A+count*4-1) >> 15 */
    sh4_emit_cmpeq(&cg, SH4_REG_RET, SH4_REG_T2);    /* T = same page */
    sh4g_close(tp, &cg); }
  g_strad = sh4g_block_guard(tp, 0);                 /* straddle (T==0) -> slow */

  /* region guard: A >> 24 != 0 (exclude BIOS / region 0) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);
    sh4_emit_shlr16(&cg, SH4_REG_T2); sh4_emit_shlr8(&cg, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);       /* T = (region == 0) */
    sh4g_close(tp, &cg); }
  g_region = sh4g_block_guard(tp, 1);

  /* page = memory_map_read[A >> 15]; guard non-NULL */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);                /* R0 = (A>>15)*4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);  /* R3 = page base */
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);            /* T = (page == NULL) */
    sh4g_close(tp, &cg); }
  g_page = sh4g_block_guard(tp, 1);

  /* R0 = A & 0x7FFF (in-page offset of the first word) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }

  /* fast path: load each listed register (ascending = lowest addr first) */
  { sh4_codegen cg = sh4g_open(tp);
    for (i = 0; i < 16; i++) {
      if (!(rlist & (1u << i))) continue;
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);  /* R2 = page[R0] (raw BE) */
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);                /* next word offset */
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);       /* byte-reverse to LE */
      sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, i);              /* reg[i] = value */
    }
    if (do_wb) {                                            /* reg[rn] = base +/- count*4 */
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_add_imm(&cg, offset_nb, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rn);
    }
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* slow path: the C helper (SH4_CALL_OP2_PC equivalent) */
  sh4g_patch_bra(g_strad,  *tp);
  sh4g_patch_bra(g_region, *tp);
  sh4g_patch_bra(g_page,   *tp);
  sh4g_const(tp, (u32)opcode, SH4_REG_ARG0);
  sh4g_const(tp, (u32)pc, SH4_REG_ARG1);
  sh4g_far_call(tp, (const void *)cgba_sh4_arm_block);
  sh4g_redispatch_if_r0(tp, (const void *)sh4_block_exit);

  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

#endif /* CGBA_SH4_ARM_BLOCK_EMIT_H */
