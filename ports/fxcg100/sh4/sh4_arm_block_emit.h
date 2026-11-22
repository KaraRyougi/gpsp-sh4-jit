#ifndef CGBA_SH4_ARM_BLOCK_EMIT_H
#define CGBA_SH4_ARM_BLOCK_EMIT_H

/*
 * Native SH-4A emission for ARM block transfers (LDM/STM). Both LDM and STM
 * route through the shared ILRAM block fastmem routines (or inline unrolled
 * IWRAM fast paths for IWRAM code with count <= 10). STM checks every
 * destination SMC tag word (or cgba_iwram_code_end fast bypass) before any
 * store and falls back cleanly on SMC or side-effecting regions.
 *
 * Address model (matches the oracle exactly): lowest-numbered register <-> lowest
 * address; the run is `count` contiguous ascending words from A = (base & ~3) +
 * offset, offset = IA:0 IB:+4 DB:-count*4 DA:-count*4+4. Writeback stores
 * new_base = base +/- count*4 (from the UNALIGNED base), suppressed for LDM when
 * the base is in the list.
 *
 * Bails to C: r15 in the list (STM) or S bit (user-bank / ^ SPSR), rn==15,
 * empty list, base-in-list writeback for STM, or straddle/unmapped runs.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_fastmem.h"

extern u8 *memory_map_read[];                 /* gba_memory.c host-page table */
int  cgba_sh4_arm_block(u32 opcode, u32 pc);  /* C oracle / slow path */
void sh4_block_exit(u32 pc);                  /* redispatch entry (sh4_emit.h) */
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */

/* Compute R1 = A = (reg[rn] & ~3) + offset_a and optional R6 = reg[rn] + offset_nb. */
static inline void sh4g_arm_block_emit_addr(u8 **tp, u32 rn, int do_wb,
                                            int offset_a, int offset_nb)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
  if (do_wb) {
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
    sh4_emit_add_imm(&cg, offset_nb, SH4_REG_ARG2);      /* R6 = wb base */
  }
  sh4_emit_shlr2(&cg, SH4_REG_T0);                       /* word-align base */
  sh4_emit_shll2(&cg, SH4_REG_T0);
  if (offset_a != 0)
    sh4_emit_add_imm(&cg, offset_a, SH4_REG_T0);         /* R1 = A */
  sh4g_close(tp, &cg);
}

/* Native ARM LDM/STM, or 0 to fall back to C. */
SH4G_NOINLINE int sh4g_arm_block_native(u8 **tp, u32 opcode, u32 pc,
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
  int fm;

  if (s_bit)                 return 0;         /* user-bank / SPSR restore -> C */
  if (rn == 15)              return 0;         /* base = PC -> C */
  if (!is_load && (rlist & 0x8000)) return 0;  /* STM {...,pc} -> C */

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) count++;
  if (count == 0)            return 0;         /* empty list -> C (rare) */
  if (!is_load && writeback && (rlist & (1u << rn)))
    return 0;                                  /* store-base value is subtle */

  offset_a  = up ? (pre ? 4 : 0) : (pre ? -(int)(count * 4) : -(int)(count * 4) + 4);
  offset_nb = up ? (int)(count * 4) : -(int)(count * 4);
  do_wb = writeback && !(rlist & (1u << rn));

  if (rlist & 0x8000) {
    /* Native ARM LDM {..., pc} (non-S, non-PC base): RAM fast path + ib_arm. */
    sh4g_emit_ram_ldm_pc(tp, rn, rlist, count,
                         offset_a, offset_nb, do_wb, /*is_thumb=*/0,
                         (const void *)memory_map_read,
                         (const void *)sh4_op2_pc_mem_tramp,
                         (const void *)cgba_sh4_arm_block,
                         opcode, pc, cycle_count);
    return 1;
  }

  fm = (is_load ? CGBA_FMB_LDM : CGBA_FMB_STM) + (do_wb ? 2 : 0);

  if (!((pc >> 24) == 0x03 && count <= 10)) {
    sh4g_arm_block_emit_addr(tp, rn, do_wb, offset_a, offset_nb);
    sh4g_fastmem_site_raw(tp, fm,
                          (const void *)cgba_sh4_arm_block, (u32)opcode,
                          (u32)pc, cycle_count,
                          sh4g_fastmem_block_params(rlist, count,
                                                    do_wb ? (int)rn : -1));
    return 1;
  }

  /* Reserve literal-pool headroom so the inline IWRAM body + fastmem call
   * cannot push an earlier MOV.L literal reference past +1020 bytes. */
  sh4g_litseg_reserve_bytes(tp, 6, 260u);
  sh4g_arm_block_emit_addr(tp, rn, do_wb, offset_a, offset_nb);

  /* ---- Direct IWRAM (region 3) inline fast path ---- */
  {
    u8 *not_iwram, *iw_miss[2], *bra_iw_done;
    int niw = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 3);                        /* T = IWRAM (region 3) */
      sh4g_close(tp, &cg); }
    not_iwram = sh4g_emit_bf_placeholder(tp);

    /* R0 = A & 0x7FFF; single-page straddle check when count > 1 (a single
     * 4-byte aligned word in 0..0x7FFC cannot cross 0x8000). */
    sh4g_const(tp, 0x7FFFu, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);        /* R0 = A & 0x7FFF */
      if (count > 1) {
        sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T2);
        sh4_emit_add_imm(&cg, (int)(count * 4) - 1, SH4_REG_T2);
        sh4_emit_shll16(&cg, SH4_REG_T2);
        sh4_emit_cmppz(&cg, SH4_REG_T2);                 /* T = stays in 32 KiB */
      }
      sh4g_close(tp, &cg); }
    if (count > 1)
      iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);

    sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
    sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);           /* R3 = host_ptr (4-aligned) */

    if (!is_load) {
      /* IWRAM SMC tags live directly at iwram + off (use R5 to preserve R6). */
      iw_miss[niw++] = sh4g_emit_smc_words_guard(tp, &cgba_iwram_code_end, iwram,
                                                 SH4_REG_RET, count,
                                                 SH4_REG_ARG1, SH4_REG_ARG0,
                                                 SH4_REG_T1);
    }

    { sh4_codegen cg = sh4g_open(tp);
      u32 idx = 0;
      for (i = 0; i < 16; i++) {
        if (!(rlist & (1u << i))) continue;
        if (is_load) {
          sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, idx);
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_store_greg(&cg, SH4_REG_T1, i);
        } else {
          sh4_emit_load_greg(&cg, i, SH4_REG_T1);
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, idx);
        }
        idx++;
      }
      if (do_wb)
        sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
      sh4g_close(tp, &cg); }
    sh4g_cycle_debit(tp, (int)count);                    /* IWRAM seq word = 1/word */
    bra_iw_done = sh4g_emit_bra_placeholder(tp);

    /* Non-IWRAM (EWRAM/VRAM/ROM) or IWRAM straddle/SMC -> shared ILRAM fastmem
     * routine (which handles EWRAM/VRAM/ROM natively and falls back to C). */
    while (niw)
      sh4g_patch_cond(iw_miss[--niw], *tp);
    sh4g_patch_cond(not_iwram, *tp);
    sh4g_fastmem_site_raw(tp, fm,
                          (const void *)cgba_sh4_arm_block, (u32)opcode,
                          (u32)pc, cycle_count,
                          sh4g_fastmem_block_params(rlist, count,
                                                    do_wb ? (int)rn : -1));
    sh4g_patch_bra(bra_iw_done, *tp);
  }
  return 1;
}

#include "ports/fxcg100/sh4/sh4_iwram_mixer_skip.h"

#endif /* CGBA_SH4_ARM_BLOCK_EMIT_H */
