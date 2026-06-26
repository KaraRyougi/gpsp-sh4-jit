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

/* Access kinds (the load width/sign we natively fast-path). */
enum { LDK_W = 0, LDK_B, LDK_UH, LDK_SH, LDK_SB };

/* Emit native SH4 for the ARM ld/st `opcode`, or return 0 to fall back to C.
 * Handles pre-indexed, no-writeback LOADS/STORES (word/byte/halfword/signed),
 * immediate or register (no-shift) offset, through gpSP's memory_map_read
 * host-page table for EWRAM/IWRAM only; else the C helper. */
static inline int sh4g_arm_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
#ifndef CGBA_SH4_ARM_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  return 0;
#else
  u32 pre = (opcode >> 24) & 1, up = (opcode >> 23) & 1;
  u32 writeback = (opcode >> 21) & 1, is_load = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF, rd = (opcode >> 12) & 0xF;
  u32 offset = 0, reg_offset = 0, rm = 0;
  u32 shift_offset = 0, shoff_type = 0, shoff_amount = 0;
  int kind, align_mask;
  u8 *guards[4]; int ng = 0;
  u8 *bra_done;

  if (!pre || writeback)    return 0;   /* pre-indexed, no writeback */
  if (rd == 15 || rn == 15) return 0;   /* PC operand (incl. STR pc) -> C */

  if ((opcode & 0x0E000090) == 0x00000090) {           /* halfword / signed form */
    u32 signed_ld = (opcode >> 6) & 1, half_w = (opcode >> 5) & 1;
    if (opcode & 0x00400000)                           /* immediate (8-bit) */
      offset = ((opcode >> 4) & 0xF0) | (opcode & 0xF);
    else                                               /* register offset (no shift) */
      { reg_offset = 1; rm = opcode & 0xF; }
    if (signed_ld && half_w) { kind = LDK_SH; align_mask = 1; }   /* LDRSH */
    else if (signed_ld)      { kind = LDK_SB; align_mask = 0; }   /* LDRSB */
    else                     { kind = LDK_UH; align_mask = 1; }   /* LDRH/STRH */
  } else {                                             /* normal word / byte form */
    if (opcode & 0x02000000) {                         /* register offset */
      u32 st = (opcode >> 5) & 3, sa = (opcode >> 7) & 0x1F;
      if (opcode & 0x10)        return 0;              /* register-specified shift -> C */
      rm = opcode & 0xF;
      if (sa == 0 && st == 0)   reg_offset = 1;        /* LSL #0 = plain register */
      else if (st == 3)         return 0;              /* ROR/RRX -> C */
      else { shift_offset = 1; shoff_type = st; shoff_amount = sa; } /* LSL/LSR/ASR #k */
    } else {
      offset = opcode & 0xFFF;                          /* 12-bit immediate */
    }
    if ((opcode >> 22) & 1)  { kind = LDK_B; align_mask = 0; }    /* LDRB/STRB */
    else                     { kind = LDK_W; align_mask = 3; }    /* LDR/STR  */
  }
  if ((reg_offset || shift_offset) && rm == 15) return 0;  /* PC offset register -> C */
  if (!is_load && (kind == LDK_SH || kind == LDK_SB)) return 0;   /* LDRD/STRD -> C */

  /* addr = reg[rn] +/- offset, in R1; then page = memory_map_read[addr>>15] in R3 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  if (shift_offset) {                                  /* offset = reg[rm] shifted (R2) */
    sh4g_arm_shift_imm_op2(tp, shoff_type, shoff_amount, rm);
  } else if (reg_offset) {                             /* offset = reg[rm] (runtime) */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
    sh4g_close(tp, &cg);
  } else {                                             /* offset = +/- immediate */
    sh4g_const(tp, up ? offset : (u32)(-(int32_t)offset), SH4_REG_T1);
  }
  { sh4_codegen cg = sh4g_open(tp);
    if ((reg_offset || shift_offset) && !up)           /* addr = rn - offset (down) */
      sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
    else                                               /* addr = rn + offset */
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);   /* R1 = addr */
    sh4g_close(tp, &cg); }

  /* memory_map_read[] only covers the GBA 0x00000000..0x0fffffff space. */
  sh4g_const(tp, 0x10000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);       /* T = (addr >= 0x10000000) */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);         /* out of map -> slow */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr >> 15 (page index) */
    sh4_emit_shll2(&cg, SH4_REG_RET);                  /* R0 = index * 4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);  /* R3 = memory_map_read[idx] */
    sh4g_close(tp, &cg); }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);          /* T = (page == NULL) */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);          /* unmapped -> slow */

  /* Fast memory must not bypass gpSP side effects. 0x02/0x03 are plain RAM. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -25, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);        /* R0 = addr >> 25 */
    sh4_emit_cmpeq_imm(&cg, 1);                         /* T = region 2 or 3 */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);          /* not EWRAM/IWRAM -> slow */
  if (align_mask) {                                    /* SH4 faults on unaligned w/h */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);                   /* T = aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);          /* misaligned -> slow */
  }

  /* --- fast path: load reg[rd] from / store reg[rd] to page[addr & 0x7FFF] --- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);       /* R0 = addr & 0x7FFF */
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    if (is_load) {
      switch (kind) {                                  /* guest is little-endian */
      case LDK_W:                                       /* word: read + byte-reverse */
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        break;
      case LDK_B:                                       /* LDRB: zero-extend byte */
        sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
        break;
      case LDK_UH:                                      /* LDRH: swap + zero-extend */
        sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
        break;
      case LDK_SH:                                      /* LDRSH: swap + sign-extend */
        sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
        break;
      default:                                          /* LDRSB: mov.b sign-extends */
        sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
        break;
      }
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    } else {                                            /* store: reg[rd] -> guest order */
      sh4_emit_load_greg(&cg, rd, SH4_REG_T1);
      switch (kind) {
      case LDK_W:                                       /* STR: byte-reverse + word store */
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      case LDK_UH:                                      /* STRH: swap low16 + halfword store */
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      default:                                          /* STRB: low byte (no reverse) */
        sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      }
    }
    sh4g_close(tp, &cg); }
  /* Charge the single access (nonseq; word column for LDR/STR, else byte/half);
   * addr is still in T0. The slow path charges the same via extra_cycles. */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0, /*is_word=*/(kind == LDK_W), 1);
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* --- slow path: the C helper (SH4_CALL_OP2_PC equivalent) --- */
  { int gi; for (gi = 0; gi < ng; gi++) sh4g_patch_cond(guards[gi], *tp); }
  sh4g_const(tp, (u32)opcode, SH4_REG_ARG0);
  sh4g_const(tp, (u32)pc, SH4_REG_ARG1);
  sh4g_far_call(tp, (const void *)cgba_sh4_arm_ldst);
  sh4g_cycle_debit_from_global(tp, &cgba_sh4_extra_cycles);
  sh4g_redispatch_if_r0_debit(tp, cycle_count, (const void *)sh4_block_exit);

  sh4g_patch_bra(bra_done, *tp);
  return 1;
#endif
}

#endif /* CGBA_SH4_ARM_LDST_EMIT_H */
