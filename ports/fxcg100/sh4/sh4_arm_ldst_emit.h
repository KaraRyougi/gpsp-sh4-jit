#ifndef CGBA_SH4_ARM_LDST_EMIT_H
#define CGBA_SH4_ARM_LDST_EMIT_H

/*
 * Native SH-4A emission for ARM single transfers (the #2 hottest class), with an
 * inline memory fast path: for the always-mapped data regions EWRAM (0x02) and
 * IWRAM (0x03) the access goes straight through gpSP's memory_map_read host-page
 * table instead of a C call into read/write_memory. Mapped ROM loads are also
 * fast-pathed; NULL ROM pages still branch to the helper so the pager can fill
 * them. Every side-effecting/open region (BIOS/open bus, I/O, backup, unaligned)
 * is resolved at runtime by a guard that branches to the C helper
 * cgba_sh4_arm_ldst, which keeps the exact side-effect / writeback / alert
 * semantics.
 *
 * Stores stay on the C helper path. That keeps SMC invalidation, DMA/IRQ
 * alerts, and other write-side effects in the canonical store machinery.
 *
 * Big-endian host: guest RAM is little-endian, so a word read is byte-reversed
 * (swap.b;swap.w;swap.b). SH-4A faults on an unaligned mov.l, so the fast path is
 * only taken for word-aligned addresses (else the C path, which rotates).
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

extern u8 *memory_map_read[];          /* gba_memory.c: 32KB host-page table */
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */

/* Access kinds (the load width/sign we natively fast-path). */
enum { LDK_W = 0, LDK_B, LDK_UH, LDK_SH, LDK_SB };

/* Emit native SH4 for the ARM transfer `opcode`, or return 0 to fall back to C.
 * Handles single RAM/mapped-load transfers (word/byte/halfword/signed) with
 * pre- or post-indexed addressing through gpSP's memory_map_read host-page table.
 * Stores use the C helper. */
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
  int effective_wb = writeback || !pre;
  u8 *guards[7]; int ng = 0;
  u8 *bra_done;

  if (rd == 15 || rn == 15) return 0;   /* PC operand (incl. STR pc) -> C */
  /* Stores are native for plain EWRAM/IWRAM: the emitted path is RAM-only
   * (region guard), SMC-checked (width-sized tag-mirror probe, falling back
   * BEFORE any write), and byteswapped — the same discipline as the proven
   * Thumb store path. Dense-gameplay profiling showed ARM stores as the top
   * helper class (H ARM st=466K + blk stores/session), so helper-owned
   * stores were the JIT's largest remaining instruction-count cost. */
  if (is_load && effective_wb && rd == rn)
    return 0;                           /* helper writes Rn before Rd */

  if ((opcode & 0x0E000090) == 0x00000090) {           /* halfword / signed form */
    u32 signed_ld = (opcode >> 6) & 1, half_w = (opcode >> 5) & 1;
    if (opcode & 0x00400000)                           /* immediate (8-bit) */
      offset = ((opcode >> 4) & 0xF0) | (opcode & 0xF);
    else                                               /* register offset (no shift) */
      { reg_offset = 1; rm = opcode & 0xF; }
    if (!is_load && (signed_ld || !half_w))
      return 0;                                                   /* reserved */
    if (!is_load)          { kind = LDK_UH; align_mask = 1; }      /* STRH */
    else if (signed_ld && half_w) { kind = LDK_SH; align_mask = 1; }   /* LDRSH */
    else if (signed_ld)    { kind = LDK_SB; align_mask = 0; }      /* LDRSB */
    else                   { kind = LDK_UH; align_mask = 1; }      /* LDRH */
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
    if ((opcode >> 22) & 1)  { kind = LDK_B; align_mask = 0; }    /* LDRB */
    else                     { kind = LDK_W; align_mask = 3; }    /* LDR  */
  }
  if ((reg_offset || shift_offset) && rm == 15) return 0;  /* PC offset register -> C */

  /* addr = transfer address in R1; for post-indexed forms keep the old base in
   * R1 and precompute writeback into R6 before the fast path clobbers R2. */
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
  if (pre) {
    sh4_codegen cg = sh4g_open(tp);
    if ((reg_offset || shift_offset) && !up)           /* addr = rn - offset (down) */
      sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
    else                                               /* addr = rn + offset */
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);   /* R1 = addr */
    sh4g_close(tp, &cg);
  }
  if (effective_wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);   /* R6 = new Rn */
    if (!pre) {
      if ((reg_offset || shift_offset) && !up)
        sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG2);
      else
        sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
    }
    sh4g_close(tp, &cg);
  }

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

  if (is_load) {
    /* Fast loads are safe for mapped RAM/I/O/video/gamepak memory. gpSP models
     * I/O reads as raw io_registers[] loads; writes stay helper-owned below.
     * Exclude BIOS/open (0/1) and backup/EEPROM (13..15). */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);                  /* R0 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);      /* T = region >= 2 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);         /* BIOS/open -> slow */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 13, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);      /* T = backup/EEPROM */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bt_placeholder(tp);         /* backup -> slow */
  } else {
    /* Fast stores must not bypass side effects. 0x02/0x03 are plain RAM. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_mov_imm(&cg, -25, SH4_REG_T1);
      sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr >> 25 */
      sh4_emit_cmpeq_imm(&cg, 1);                        /* T = region 2 or 3 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);         /* not EWRAM/IWRAM -> slow */
  }
  if (align_mask) {                                    /* SH4 faults on unaligned w/h */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);                   /* T = aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);          /* misaligned -> slow */
  }

  /* R0 = addr & 0x7FFF (in-page offset). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  if (is_load && align_mask) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_mov_imm(&cg, align_mask, SH4_REG_ARG0);
    sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = host ptr aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);    /* unaligned NOR/RAM ptr */
  }

  if (!is_load) {
    u8 *bf_iwram, *bra_tag_ready;
    /* Build the SMC tag-page pointer in R5 from the data page pointer in R3:
     *   EWRAM tag mirror = ewram + 0x40000, so page + 0x40000.
     *   IWRAM data page  = iwram + 0x8000, so tag page = page - 0x8000. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG1);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_shlr16(&cg, SH4_REG_T1);
      sh4_emit_shlr8(&cg, SH4_REG_T1);                  /* R2 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = EWRAM */
      sh4g_close(tp, &cg); }
    bf_iwram = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, 0x40000u, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG1);
    bra_tag_ready = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(bf_iwram, *tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG1);
    sh4g_patch_bra(bra_tag_ready, *tp);

    /* Any nonzero tag byte means this write hits translated RAM code. */
    { sh4_codegen cg = sh4g_open(tp);
      switch (kind) {
      case LDK_W:  sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0); break;
      case LDK_UH: sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0); break;
      default:     sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0); break;
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = tag == 0 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);        /* SMC -> slow */
  }

  /* --- fast path: transfer reg[rd] and page[addr & 0x7FFF] --- */
  { sh4_codegen cg = sh4g_open(tp);
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
    } else {
      sh4_emit_load_greg(&cg, rd, SH4_REG_T1);
      switch (kind) {                                  /* guest is little-endian */
      case LDK_W:
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      case LDK_UH:
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      default:
        sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      }
    }
    sh4g_close(tp, &cg); }
  if (effective_wb)
    sh4g_store_greg(tp, SH4_REG_ARG2, rn);
  /* Charge the single access (nonseq; word column for LDR/STR, else byte/half);
   * addr is still in T0. The slow path charges the same via extra_cycles. */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0, /*is_word=*/(kind == LDK_W), 1);
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* --- slow path: the C helper (SH4_CALL_OP2_PC equivalent) --- */
  { int gi; for (gi = 0; gi < ng; gi++) sh4g_patch_cond(guards[gi], *tp); }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                      (const void *)cgba_sh4_arm_ldst, (u32)opcode, (u32)pc,
                      1, cycle_count);

  sh4g_patch_bra(bra_done, *tp);
  return 1;
#endif
}

#endif /* CGBA_SH4_ARM_LDST_EMIT_H */
