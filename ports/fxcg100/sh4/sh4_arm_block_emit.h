#ifndef CGBA_SH4_ARM_BLOCK_EMIT_H
#define CGBA_SH4_ARM_BLOCK_EMIT_H

/*
 * Native SH-4A emission for ARM block transfers (LDM/STM). The register list is
 * a translate-time constant, so the transfer UNROLLS into a straight-line run of
 * fast-path word reads/writes instead of the C helper's popcount loop +
 * per-word execute_load/execute_store region switch.
 *
 * LDM (load) reads through any mapped host page (EWRAM/IWRAM/IO/VRAM/paged-ROM,
 * memory_map_read non-NULL, region != 0). STM (store) fast-paths only RAM
 * pages (EWRAM/IWRAM) after checking the SMC tag mirror for the whole range.
 *
 * Address model (matches the oracle exactly): lowest-numbered register <-> lowest
 * address; the run is `count` contiguous ascending words from A = (base & ~3) +
 * offset, offset = IA:0 IB:+4 DB:-count*4 DA:-count*4+4. Writeback stores
 * new_base = base +/- count*4 (from the UNALIGNED base), suppressed for LDM when
 * the base is in the list.
 *
 * The whole run is guarded into ONE mapped host page (single resolve covers every
 * word); a straddle / wrong-region / unmapped run falls to the C helper. Guards
 * use a short skip over a far BRA-to-slow (sh4g_patch_cond truncates disp8, so a
 * long conditional branch over a count-sized fast path would be unsafe).
 *
 * Bails to C: r15 in the list (PC redispatch / ^ SPSR), the S bit (user-bank),
 * rn==15, empty list, base-in-list writeback for STM, any straddle/unmapped run.
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

/* Native ARM LDM/STM, or 0 to fall back to C. */
static inline int sh4g_arm_block_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
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
  u8 *guards[24]; int ng = 0;
  u8 *bra_done;

  if (s_bit)                 return 0;         /* user-bank / SPSR restore -> C */
  if (rlist & 0x8000)        return 0;         /* PC in list -> C (redispatch)  */
  if (rn == 15)              return 0;         /* base = PC -> C */

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) count++;
  if (count == 0)            return 0;         /* empty list -> C (rare) */
  if (!is_load && writeback && (rlist & (1u << rn)))
    return 0;                                  /* store-base value is subtle */

  offset_a  = up ? (pre ? 4 : 0) : (pre ? -(int)(count * 4) : -(int)(count * 4) + 4);
  offset_nb = up ? (int)(count * 4) : -(int)(count * 4);
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
  guards[ng++] = sh4g_block_guard(tp, 0);            /* straddle (T==0) -> slow */

  /* upper-bound guard: A < 0x10000000. memory_map_read[] only covers the GBA
   * 0x00000000..0x0FFFFFFF space (8192 32 KB pages), so a high / open-bus base
   * would index past the table into host memory. */
  sh4g_const(tp, 0x10000000u, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_ARG1); /* T = (A >= 0x10000000) */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_guard(tp, 1);          /* out of map -> slow */

  if (is_load) {
    /* region guard: A >> 24 != 0 (exclude BIOS / region 0) */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);
      sh4_emit_shlr16(&cg, SH4_REG_T2); sh4_emit_shlr8(&cg, SH4_REG_T2);
      sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* T = (region == 0) */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_block_guard(tp, 1);
  } else {
    /* STM fast path is RAM-only: region 2 or 3 => (A >> 25) == 1. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_mov_imm(&cg, -25, SH4_REG_T1);
      sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 1);
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_block_guard(tp, 0);
  }

  /* page = memory_map_read[A >> 15] in R3 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);                /* R0 = (A>>15)*4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);  /* R3 = page base */
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);             /* T = (page==NULL) */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_guard(tp, 1);

  /* R0 = A & 0x7FFF (in-page offset of the first word) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }

  if (!is_load) {
    u8 *bf_iwram, *bra_tag_ready;
    /* Build SMC tag-page pointer in R6 from data page R3. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG2);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
      sh4_emit_shlr16(&cg, SH4_REG_T1);
      sh4_emit_shlr8(&cg, SH4_REG_T1);                  /* R2 = A >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = EWRAM */
      sh4g_close(tp, &cg); }
    bf_iwram = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, 0x40000u, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG2);
    bra_tag_ready = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(bf_iwram, *tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG2);
    sh4g_patch_bra(bra_tag_ready, *tp);

    /* Scan all destination tag words before doing any store. */
    { sh4_codegen cg = sh4g_open(tp);
      for (i = 0; i < count; i++) {
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG2, SH4_REG_ARG0);
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);  /* T = tag word == 0 */
        sh4g_close(tp, &cg);
        guards[ng++] = sh4g_block_guard(tp, 0);          /* SMC -> slow */
        cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      }
      sh4_emit_add_imm(&cg, -(int)(count * 4), SH4_REG_RET);
      sh4g_close(tp, &cg); }
  }

  /* fast path: transfer each listed register (ascending = lowest addr first) */
  { sh4_codegen cg = sh4g_open(tp);
    for (i = 0; i < 16; i++) {
      if (!(rlist & (1u << i))) continue;
      if (is_load) {
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);  /* R2 = page[R0] (raw BE) */
        sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);       /* byte-reverse to LE */
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_store_greg(&cg, SH4_REG_T1, i);              /* reg[i] = value */
      } else {
        sh4_emit_load_greg(&cg, i, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);       /* byte-reverse to BE */
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      }
    }
    if (do_wb) {                                              /* reg[rn] = base +/- count*4 */
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_add_imm(&cg, offset_nb, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rn);
    }
    sh4g_close(tp, &cg); }
  /* Charge the transfer cycles the inline run just performed (the slow C path
   * charges these via cgba_sh4_extra_cycles; the fast path must match). All
   * `count` words are in one page/region (straddle-guarded): seq, word. */
  sh4g_charge_mem_run(tp, SH4_REG_ARG1, /*seq=*/1, /*is_word=*/1, count);
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* slow path: the C helper (SH4_CALL_OP2_PC equivalent) */
  for (i = 0; i < (u32)ng; i++) sh4g_patch_bra(guards[i], *tp);
  sh4g_const(tp, (u32)opcode, SH4_REG_ARG0);
  sh4g_const(tp, (u32)pc, SH4_REG_ARG1);
  sh4g_far_call(tp, (const void *)cgba_sh4_arm_block);
  sh4g_cycle_debit_from_global(tp, &cgba_sh4_extra_cycles);
  sh4g_redispatch_if_r0_debit(tp, cycle_count, (const void *)sh4_block_exit);

  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

#endif /* CGBA_SH4_ARM_BLOCK_EMIT_H */
