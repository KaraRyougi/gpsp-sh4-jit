#ifndef CGBA_SH4_ARM_LDST_EMIT_H
#define CGBA_SH4_ARM_LDST_EMIT_H

/*
 * Native SH-4A emission for ARM single load/store (the #2 hottest class), with an
 * inline memory fast path: for the always-mapped data regions EWRAM (0x02) and
 * IWRAM (0x03) the access goes straight through gpSP's memory_map_read host-page
 * table instead of a C call into read_memory. Every other region (I/O, ROM,
 * VRAM/palette/OAM, BIOS, backup, unaligned) is resolved at RUNTIME by a region
 * guard that branches to the C helper cgba_sh4_arm_ldst, which keeps the exact
 * side-effect / writeback / alert semantics.
 *
 * Milestone 1: immediate-offset WORD LOADS, rd!=15, rn!=15, pre-indexed with no
 * writeback. The big, lowest-risk subset. Stores, bytes/halfwords, register
 * offsets, writeback and the PC forms stay on the C path (return 0 / runtime
 * fall-through).
 *
 * Big-endian host: guest RAM is little-endian, so a word read is byte-reversed
 * (swap.b;swap.w;swap.b). SH-4A faults on an unaligned mov.l, so the fast path is
 * only taken for word-aligned addresses (else the C path, which rotates).
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

extern u8 *memory_map_read[];          /* gba_memory.c: 32KB host-page table */
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);

/* Emit native SH4 for the ARM ld/st `opcode`, or return 0 to fall back to C. */
static inline int sh4g_arm_ldst_native(u8 **tp, u32 opcode, u32 pc)
{
  u32 is_load, is_byte, pre, writeback, up, rn, rd, imm;
  u8 *bf_slow1, *bf_slow2, *bra_done;

  if ((opcode & 0x0E000090) == 0x00000090) return 0;  /* halfword/signed -> C */
  if (opcode & 0x02000000)                 return 0;  /* register offset  -> C (M3) */

  is_load   = (opcode >> 20) & 1;
  is_byte   = (opcode >> 22) & 1;
  pre       = (opcode >> 24) & 1;
  writeback = (opcode >> 21) & 1;
  up        = (opcode >> 23) & 1;
  rn        = (opcode >> 16) & 0xF;
  rd        = (opcode >> 12) & 0xF;
  imm       = opcode & 0xFFF;

  if (!is_load)             return 0;   /* M1: loads only (stores = M4) */
  if (is_byte)              return 0;   /* M1: word only  (byte  = M2) */
  if (!pre || writeback)    return 0;   /* M1: pre-indexed, no writeback */
  if (rd == 15 || rn == 15) return 0;   /* PC operands -> C */

  /* addr = reg[rn] +/- imm12, in R1 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, up ? imm : (u32)(-(int32_t)imm), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);     /* R1 = addr */
    /* region guard: T = ((addr >> 25) == 1)  i.e. region 2 (EWRAM) or 3 (IWRAM) */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -25, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr >> 25 */
    sh4_emit_cmpeq_imm(&cg, 1);
    sh4g_close(tp, &cg); }
  bf_slow1 = sh4g_emit_bf_placeholder(tp);             /* not EWRAM/IWRAM -> slow */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);    /* alignment: T = (addr&3)==0 */
    sh4_emit_tst_imm(&cg, 3);
    sh4g_close(tp, &cg); }
  bf_slow2 = sh4g_emit_bf_placeholder(tp);             /* unaligned -> slow */

  /* --- fast path: reg[rd] = byteswap(memory_map_read[addr>>15][addr & 0x7FFF]) --- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr >> 15 (page index) */
    sh4_emit_shll2(&cg, SH4_REG_RET);                  /* R0 = index * 4 (byte offset) */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);  /* R3 = memory_map_read[idx] */
    /* R0 = addr & 0x7FFF (intra-page offset) */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);  /* R2 = page[offset] (raw BE) */
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);       /* byte-reverse to guest LE */
    sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd);            /* reg[rd] = value */
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_placeholder(tp);             /* skip the slow path */

  /* --- slow path: the C helper (SH4_CALL_OP2_PC equivalent) --- */
  sh4g_patch_cond(bf_slow1, *tp);
  sh4g_patch_cond(bf_slow2, *tp);
  sh4g_const(tp, (u32)opcode, SH4_REG_ARG0);
  sh4g_const(tp, (u32)pc, SH4_REG_ARG1);
  sh4g_far_call(tp, (const void *)cgba_sh4_arm_ldst);
  sh4g_redispatch_if_r0(tp, (const void *)sh4_block_exit);

  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

#endif /* CGBA_SH4_ARM_LDST_EMIT_H */
