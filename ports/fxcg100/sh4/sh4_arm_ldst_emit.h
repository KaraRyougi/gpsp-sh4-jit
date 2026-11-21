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
#include "ports/fxcg100/sh4/sh4_fastmem.h"

extern u8 *memory_map_read[];          /* gba_memory.c: 32KB host-page table */
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */

/* Access kinds (the load width/sign we natively fast-path). */
enum { LDK_W = 0, LDK_B, LDK_UH, LDK_SH, LDK_SB };

/* Emit load from @(R0, R3) into R2 and extend/byteswap according to `kind`. */
static inline void sh4g_arm_emit_load_r0_r3(sh4_codegen *cg, int kind)
{
  switch (kind) {
  case LDK_W:
    sh4_emit_mov_l_load_r0(cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_bswap32(cg, SH4_REG_T1, SH4_REG_T1);
    break;
  case LDK_B:
    sh4_emit_mov_b_load_r0(cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(cg, SH4_REG_T1, SH4_REG_T1);
    break;
  case LDK_UH:
    sh4_emit_mov_w_load_r0(cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_swap_b(cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_extu_w(cg, SH4_REG_T1, SH4_REG_T1);
    break;
  case LDK_SH:
    sh4_emit_mov_w_load_r0(cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_swap_b(cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_exts_w(cg, SH4_REG_T1, SH4_REG_T1);
    break;
  default: /* LDK_SB */
    sh4_emit_mov_b_load_r0(cg, SH4_REG_T2, SH4_REG_T1);
    break;
  }
}

/* Forward declaration for fused pair fallback emission. */
static inline int sh4g_arm_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count);

/* Emit native SH4 for the ARM transfer `opcode`, or return 0 to fall back to C.
 * When `pair_op2 != 0` and `(pc >> 24) == 0x03`, also attempts same-page ROM8
 * fusion with a consecutive same-base immediate load `pair_op2` at `pc + 4`
 * (setting `*fused_pair = 1` when both instructions have been emitted). */
static inline int sh4g_arm_ldst_native_ex(u8 **tp, u32 opcode, u32 pc,
  int cycle_count, u32 pair_op2, int pair_cc2, int *fused_pair)
{
#ifndef CGBA_SH4_ARM_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  (void)pair_op2;
  (void)pair_cc2;
  if (fused_pair) *fused_pair = 0;
  return 0;
#else
  u32 pre = (opcode >> 24) & 1, up = (opcode >> 23) & 1;
  u32 writeback = (opcode >> 21) & 1, is_load = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF, rd = (opcode >> 12) & 0xF;
  u32 offset = 0, reg_offset = 0, rm = 0;
  u32 shift_offset = 0, shoff_type = 0, shoff_amount = 0;
  int kind, align_mask;
  int effective_wb = writeback || !pre;
  int pair_ok = 0, pair_kind = 0, pair_delta = 0;
  u32 pair_rd = 0;
  if (fused_pair) *fused_pair = 0;

  if (rd == 15) return 0;               /* PC destination -> C */
  /* rn==15 literal-pool loads (LDR rd,[pc,#imm] — 2 per IRQ in AW's ISR)
   * have a COMPILE-TIME address: pc+8 +/- imm. Immediate, no-writeback
   * loads go through the normal fastmem site with the address synthesized
   * as a constant; everything else PC-based stays on C. (A post-indexed
   * form implies writeback and is rejected below via effective_wb.) */
  if (rn == 15 && (!is_load || writeback || !pre || (opcode & 0x02000000)))
    return 0;
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
    if ((!is_load && (signed_ld || !half_w)) || (is_load && !signed_ld && !half_w))
      return 0;                                                   /* reserved / mul / swap */
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
  if (rn == 15 && (reg_offset || shift_offset)) return 0;  /* pc + reg -> C */

  /* Check if `pair_op2` is a same-base immediate pre-indexed load whose
   * address is `addr1 + delta` with `delta` in [1..15] and guaranteed aligned
   * whenever `addr1` is aligned. Requires `rd != rn` so op1 doesn't overwrite
   * the base register before op2 reads it. */
  if (pair_op2 != 0 && (pc >> 24) == 0x03 && is_load && rn != 15 && rd != rn &&
      cgba_gamepak_fragment_blocks != NULL && gamepak_size > 0x8000u &&
      (pair_op2 >> 28) == 0xEu) {
    u32 pre2 = (pair_op2 >> 24) & 1, up2 = (pair_op2 >> 23) & 1;
    u32 wb2 = (pair_op2 >> 21) & 1, is_load2 = (pair_op2 >> 20) & 1;
    u32 rn2 = (pair_op2 >> 16) & 0xF, rd2 = (pair_op2 >> 12) & 0xF;
    if (is_load2 && pre2 && !wb2 && rn2 == rn && rd2 != 15) {
      int k2 = -1, am2 = 0;
      u32 off2 = 0;
      if ((pair_op2 & 0x0E400090u) == 0x00400090u) {       /* halfword/signed imm */
        u32 s2 = (pair_op2 >> 6) & 1, h2 = (pair_op2 >> 5) & 1;
        off2 = ((pair_op2 >> 4) & 0xF0u) | (pair_op2 & 0xFu);
        if (s2 && h2)      { k2 = LDK_SH; am2 = 1; }
        else if (s2)       { k2 = LDK_SB; am2 = 0; }
        else if (h2)       { k2 = LDK_UH; am2 = 1; }
      } else if ((pair_op2 & 0x0E000000u) == 0x04000000u) { /* LDR / LDRB imm */
        off2 = pair_op2 & 0xFFFu;
        if ((pair_op2 >> 22) & 1) { k2 = LDK_B; am2 = 0; }
        else                      { k2 = LDK_W; am2 = 3; }
      }
      if (k2 >= 0 && am2 <= align_mask) {
        s32 soff2 = up2 ? (s32)off2 : -(s32)off2;
        s32 delta = 0;
        if (effective_wb && pre) {
          delta = soff2;
        } else if (!reg_offset && !shift_offset) {
          s32 soff1 = up ? (s32)offset : -(s32)offset;
          delta = effective_wb ? (soff1 + soff2) : (soff2 - soff1);
        }
        if (delta >= 1 && delta <= 15 && (delta & am2) == 0) {
          pair_ok = 1;
          pair_kind = k2;
          pair_delta = (int)delta;
          pair_rd = rd2;
        }
      }
    }
  }

  if (rn == 15) {                       /* literal pool: constant address */
    u32 caddr = (pc + 8) + (up ? offset : (u32)-(int32_t)offset);
    sh4g_const(tp, caddr, SH4_REG_T0);
    goto have_address;
  }

  /* addr = transfer address in R1; for post-indexed forms keep the old base in
   * R1 and precompute writeback into R6 before the fast path clobbers R2. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  if (shift_offset || reg_offset) {
    if (shift_offset) {                                /* offset = reg[rm] shifted (R2) */
      sh4g_arm_shift_imm_op2(tp, shoff_type, shoff_amount, rm);
    } else {                                           /* offset = reg[rm] (runtime) */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
      sh4g_close(tp, &cg);
    }
    if (pre) {
      sh4_codegen cg = sh4g_open(tp);
      if (!up)                                         /* addr = rn - offset (down) */
        sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
      else                                             /* addr = rn + offset */
        sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = addr */
      sh4g_close(tp, &cg);
    }
    if (effective_wb) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2); /* R6 = new Rn */
      if (!pre) {
        if (!up)
          sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG2);
        else
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
      }
      sh4g_close(tp, &cg);
    }
  } else {                                             /* offset = +/- immediate */
    s32 soff = up ? (s32)offset : -(s32)offset;
    if (soff == 0) {
      if (effective_wb) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4g_close(tp, &cg);
      }
    } else if (soff >= -128 && soff <= 127) {
      sh4_codegen cg = sh4g_open(tp);
      if (pre) {
        sh4_emit_add_imm(&cg, (int)soff, SH4_REG_T0);  /* R1 = addr */
        if (effective_wb)
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      } else if (effective_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, (int)soff, SH4_REG_ARG2);
      }
      sh4g_close(tp, &cg);
    } else {
      sh4g_const(tp, (u32)soff, SH4_REG_T1);
      if (pre) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = addr */
        sh4g_close(tp, &cg);
      }
      if (effective_wb) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        if (!pre)
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
        sh4g_close(tp, &cg);
      }
    }
  }

have_address:
  {
    u8 *iwram_skip = NULL, *rom8_skip = NULL, *pair_both_done = NULL;

    if ((pc >> 24) == 0x03) {
      int has_rom8 = is_load && cgba_gamepak_fragment_blocks != NULL &&
                     gamepak_size > 0x8000u;
      int rom8_first = has_rom8 && pair_ok;
      u32 rom8_limit = has_rom8
        ? ((gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u)
        : 0u;
      int pass;

      sh4g_litseg_reserve_bytes(tp, pair_ok ? 10 : 8, pair_ok ? 280u : 220u);

      for (pass = 0; pass < 2; pass++) {
        int do_rom8 = rom8_first ? (pass == 0) : (pass == 1);
        if (do_rom8) {
          u8 *rom8_miss[3];
          int nrom8 = 0;
          if (!has_rom8)
            continue;

          /* Unified ROM8 guard: (addr - 0x08008000) < rom8_limit checks
           * region == 8, block >= 8, AND offset < gamepak_size in one branch. */
          if (align_mask) {
            sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
            sh4_emit_mov_imm(&cg, align_mask, SH4_REG_T2);
            sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T1);  /* T = guest aligned */
            sh4g_close(tp, &cg);
            rom8_miss[nrom8++] = sh4g_emit_bf_placeholder(tp);
          }

          sh4g_emit_rom8_page_lookup(tp, rom8_limit,
                                     cgba_gamepak_fragment_blocks + 8,
                                     SH4_REG_T0, rom8_miss, &nrom8);

          sh4g_const(tp, 0x0FFFu, SH4_REG_RET);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);  /* R0 = addr & 0x0FFF */
            sh4g_arm_emit_load_r0_r3(&cg, kind);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
            if (effective_wb)
              sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
            sh4g_close(tp, &cg); }

          if (pair_ok) {
            /* Same-page ROM8 load-2 fast path: R0 still holds addr1 & 0x0FFF
             * and R3 still holds the resolved 4 KiB NOR block pointer. */
            u8 *same_page_miss;
            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_add_imm(&cg, pair_delta, SH4_REG_RET); /* R0 = off1 + delta */
              sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1); /* R2 = off2 */
              sh4_emit_shlr8(&cg, SH4_REG_RET);               /* bit 12 -> bit 4 */
              sh4_emit_tst_imm(&cg, 0x10);                    /* T = same 4 KiB page */
              sh4g_close(tp, &cg); }
            same_page_miss = sh4g_emit_bf_placeholder(tp);

            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET); /* R0 = off2 */
              sh4g_arm_emit_load_r0_r3(&cg, pair_kind);
              sh4_emit_store_greg(&cg, SH4_REG_T1, pair_rd);
              sh4g_close(tp, &cg); }

            /* Debit combined ROM8 waitstates for both loads once.
             * WAITCNT writes flush both ROM and RAM translation caches, so
             * ws_cyc_nseq[8][0/1] are compile-time constants inside blocks. */
            sh4g_cycle_debit(tp,
              (int)ws_cyc_nseq[8][kind == LDK_W ? 1 : 0] +
              (int)ws_cyc_nseq[8][pair_kind == LDK_W ? 1 : 0]);
            pair_both_done = sh4g_emit_bra_placeholder(tp);
            sh4g_patch_cond(same_page_miss, *tp);
          }

          /* WAITCNT writes flush both translation caches -> compile-time constant. */
          sh4g_cycle_debit(tp, (int)ws_cyc_nseq[8][kind == LDK_W ? 1 : 0]);
          rom8_skip = sh4g_emit_bra_placeholder(tp);

          while (nrom8)
            sh4g_patch_cond(rom8_miss[--nrom8], *tp);
        } else {
          u8 *iwram_miss[3];
          int niw = 0;

          /* Direct IWRAM (region 3) fast path. */
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
            sh4_emit_shlr16(&cg, SH4_REG_RET);
            sh4_emit_shlr8(&cg, SH4_REG_RET);
            sh4_emit_cmpeq_imm(&cg, 3);                  /* T = IWRAM */
            sh4g_close(tp, &cg); }
          iwram_miss[niw++] = sh4g_emit_bf_placeholder(tp);

          if (align_mask) {
            sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
            sh4_emit_tst_imm(&cg, align_mask);           /* T = guest aligned */
            sh4g_close(tp, &cg);
            iwram_miss[niw++] = sh4g_emit_bf_placeholder(tp);
          }

          sh4g_const(tp, 0x7FFFu, SH4_REG_RET);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);  /* R0 = addr & 0x7FFF */
            sh4g_close(tp, &cg); }
          sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);

          if (is_load) {
            sh4_codegen cg = sh4g_open(tp);
            sh4g_arm_emit_load_r0_r3(&cg, kind);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
            if (effective_wb)
              sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
            sh4g_close(tp, &cg);
          } else {
            /* Fast SMC bypass via cgba_iwram_code_end, else check tag at iwram + off. */
            u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                              SH4_REG_RET, SH4_REG_ARG0, SH4_REG_T1);
            sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG0);
            { sh4_codegen cg = sh4g_open(tp);
              if (kind == LDK_W)
                sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
              else if (kind == LDK_UH)
                sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
              else
                sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
              sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T1); /* T = tag clear */
              sh4g_close(tp, &cg); }
            iwram_miss[niw++] = sh4g_emit_bf_placeholder(tp);
            sh4g_patch_cond(smc_ok, *tp);

            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_load_greg(&cg, rd, SH4_REG_T1);
              if (kind == LDK_W) {
                sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
                sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
              } else if (kind == LDK_UH) {
                sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
                sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
              } else {
                sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
              }
              if (effective_wb)
                sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
              sh4g_close(tp, &cg); }
          }
          sh4g_cycle_debit(tp, 1);                       /* IWRAM nseq cost */
          iwram_skip = sh4g_emit_bra_placeholder(tp);

          while (niw)
            sh4g_patch_cond(iwram_miss[--niw], *tp);
        }
      }
    }

    /* Out-of-line fast path: handles EWRAM/IO/VRAM/ROM and falls back to C. */
    {
      int fm = is_load ? (int)kind
        : (kind == LDK_W ? CGBA_FM_STORE_W
           : kind == LDK_UH ? CGBA_FM_STORE_UH : CGBA_FM_STORE_B);
      if (effective_wb)
        fm += CGBA_FM_WB;
      sh4g_fastmem_site(tp, fm,
                        (const void *)cgba_sh4_arm_ldst, (u32)opcode, (u32)pc,
                        cycle_count, rd, effective_wb ? (int)rn : -1);
    }
    if (iwram_skip)
      sh4g_patch_bra(iwram_skip, *tp);
    if (rom8_skip)
      sh4g_patch_bra(rom8_skip, *tp);
    if (pair_both_done) {
      sh4_greg_forward_reset();
      sh4g_arm_ldst_native(tp, pair_op2, pc + 4, pair_cc2);
      sh4g_patch_bra(pair_both_done, *tp);
      if (fused_pair) *fused_pair = 1;
    }
  }
  return 1;

#endif
}

static inline int sh4g_arm_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
  return sh4g_arm_ldst_native_ex(tp, opcode, pc, cycle_count, 0u, 0, NULL);
}

#ifdef CGBA_SH4_ARM_LDST_NATIVE
/* Fuse a run of 2..8 consecutive unconditional ARM register-offset byte loads
 * in IWRAM code sharing the same base register `rn`:
 *   LDRB/LDRSB rd[k], [rn, rm[k]]  (P=1, U=1, W=0, L=1, unshifted reg offset)
 * with rd[k] != rn, rn/rd[k]/rm[k] != 15.
 *
 * When op[0] hits ROM8, caches the resolved 4 KiB NOR page pointer in R3 and
 * `(reg[rn] - guest_page_base)` in R6. Each subsequent load checks
 * `(unsigned)(reg[rm[k]] + R6) < 0x1000` in 3 insns and reads straight from
 * @(R0, R3) (7 insns/lookup vs 25 insns/lookup), debiting all waitstates once.
 * Any miss on load k debits the k completed loads and jumps to exact normal
 * emission for k..cnt-1. */
static inline int sh4g_arm_ldst_reg_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  u32 rn, rd[8], rm[8];
  u8 is_signed[8];
  u32 rom8_limit;
  int k, ws;
  u8 *rom8_miss[2], *page_miss[8], *bra_normal[8], *all_done;

  if ((pc >> 24) != 0x03 || cnt < 2 || cnt > 8) return 0;
  if (cgba_gamepak_fragment_blocks == NULL || gamepak_size <= 0x8000u) return 0;

  rn = (ops[0] >> 16) & 0xFu;
  if (rn == 15u) return 0;
  for (k = 0; k < cnt; k++) {
    u32 op = ops[k];
    if ((op & 0xFFF00FF0u) == 0xE7D00000u)      is_signed[k] = 0; /* LDRB */
    else if ((op & 0xFFF00FF0u) == 0xE19000D0u) is_signed[k] = 1; /* LDRSB */
    else return 0;
    if (((op >> 16) & 0xFu) != rn) return 0;
    rd[k] = (op >> 12) & 0xFu;
    rm[k] = op & 0xFu;
    if (rd[k] == 15u || rm[k] == 15u || rd[k] == rn) return 0;
  }

  rom8_limit = (gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u;
  ws = (int)ws_cyc_nseq[8][0];
  sh4g_litseg_reserve_bytes(tp, 8, 280u);

  /* op[0]: R6 = reg[rn], R1 = addr0 = reg[rn] + reg[rm[0]]. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_ARG2);
    sh4_emit_load_greg(&cg, rm[0], SH4_REG_T0);
    sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_T0);
    sh4g_close(tp, &cg); }

  { int nr8 = 0;
    sh4g_emit_rom8_page_lookup(tp, rom8_limit,
                               cgba_gamepak_fragment_blocks + 8,
                               SH4_REG_T0, rom8_miss, &nr8); }

  /* Perform op[0] and cache R6 = reg[rn] - guest_page_base, R5 = 0x1000. */
  sh4g_const(tp, 0x0FFFu, SH4_REG_RET);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);       /* R0 = addr0 & 0x0FFF */
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    if (!is_signed[0])
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd[0]);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG0);  /* R4 = addr0 */
    sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_ARG0);     /* R4 = addr0 & ~0xFFF */
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_ARG2);    /* R6 = reg[rn] - page_base */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x1000u, SH4_REG_ARG1);              /* R5 = 0x1000 */

  /* Remaining loads k = 1..cnt-1: 7 insns each on same-page hit! */
  for (k = 1; k < cnt; k++) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rm[k], SH4_REG_RET);
      sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_RET); /* R0 = addr_k - page_base */
      sh4_emit_cmphs(&cg, SH4_REG_ARG1, SH4_REG_RET);   /* (unsigned)R0 >= 0x1000 */
      sh4g_close(tp, &cg); }
    page_miss[k] = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      if (!is_signed[k])
        sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
      sh4g_close(tp, &cg); }
  }
  sh4g_cycle_debit(tp, cnt * ws);
  all_done = sh4g_emit_bra_placeholder(tp);

  /* Compact miss trampolines (all within disp8 reach of fast path). */
  sh4g_patch_cond(rom8_miss[0], *tp);
  sh4g_patch_cond(rom8_miss[1], *tp);
  bra_normal[0] = sh4g_emit_bra_placeholder(tp);
  for (k = 1; k < cnt; k++) {
    sh4g_patch_cond(page_miss[k], *tp);
    sh4g_cycle_debit(tp, k * ws);
    bra_normal[k] = sh4g_emit_bra_placeholder(tp);
  }

  /* Exact normal fallbacks for k = 0..cnt-1. */
  for (k = 0; k < cnt; k++) {
    sh4g_patch_bra(bra_normal[k], *tp);
    sh4_greg_forward_reset();
    sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
  }
  sh4g_patch_bra(all_done, *tp);
  return 1;
}

/* Fuse a run of 2..8 consecutive unconditional ARM IWRAM byte loads
 * (LDRB/LDRSB with [rn, +rm], [rn, #+imm], or post-inc [rn], #+imm).
 * Hoists iwram_data into R3, 0x7FFF into R5, and any shared read-only
 * offset register rm into R6, debiting all IWRAM cycles once at exit.
 * Any non-IWRAM load k debits the k completed loads and falls cleanly into
 * normal exact emission for k..cnt-1. */
static inline int sh4g_arm_iwram_byte_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  u8 form[8];      /* 0=[rn,+rm], 1=[rn],+#imm, 2=[rn,+#imm] */
  u8 is_signed[8];
  u32 rn[8], rd[8], arg[8];
  u32 written_mask = 0u, shared_rm = 16u;
  int have_shared_rm = 0, k;
  u8 *iw_miss[8], *bra_normal[8], *all_done;

  if ((pc >> 24) != 0x03 || cnt < 2 || cnt > 8) return 0;

  for (k = 0; k < cnt; k++) {
    u32 op = ops[k];
    if ((op & 0xFFF00FF0u) == 0xE19000D0u) {
      form[k] = 0; is_signed[k] = 1; arg[k] = op & 0xFu;
    } else if ((op & 0xFFF00FF0u) == 0xE7D00000u) {
      form[k] = 0; is_signed[k] = 0; arg[k] = op & 0xFu;
    } else if ((op & 0xFFF000F0u) == 0xE0D000D0u) {
      form[k] = 1; is_signed[k] = 1; arg[k] = ((op >> 4) & 0xF0u) | (op & 0xFu);
    } else if ((op & 0xFFF00000u) == 0xE4D00000u) {
      form[k] = 1; is_signed[k] = 0; arg[k] = op & 0xFFFu;
    } else if ((op & 0xFFF000F0u) == 0xE1D000D0u) {
      form[k] = 2; is_signed[k] = 1; arg[k] = ((op >> 4) & 0xF0u) | (op & 0xFu);
    } else if ((op & 0xFFF00000u) == 0xE5D00000u) {
      form[k] = 2; is_signed[k] = 0; arg[k] = op & 0xFFFu;
    } else {
      return 0;
    }
    rn[k] = (op >> 16) & 0xFu;
    rd[k] = (op >> 12) & 0xFu;
    if (rn[k] == 15u || rd[k] == 15u) return 0;
    if (form[k] == 0) {
      if (arg[k] == 15u) return 0;
    } else {
      if (arg[k] > 127u) return 0;
      if (form[k] == 1 && rd[k] == rn[k]) return 0;
    }
    written_mask |= (1u << rd[k]);
    if (form[k] == 1) written_mask |= (1u << rn[k]);
  }

  /* Cache a shared read-only offset register rm in R6 across all [rn, +rm]. */
  for (k = 0; k < cnt; k++) {
    if (form[k] == 0) {
      if (shared_rm == 16u) { shared_rm = arg[k]; have_shared_rm = 1; }
      else if (shared_rm != arg[k]) { have_shared_rm = 0; break; }
    }
  }
  if (have_shared_rm && (written_mask & (1u << shared_rm)))
    have_shared_rm = 0;

  /* Hoisted multi-base fast path (cnt >= 3, <= 2 distinct bases, bases never
   * overwritten as rd, optional single shared read-only rm, all effective
   * displacements 0..15): validates each base (and base+rm) ONCE upfront,
   * keeps host pointers in R4..R7, and emits 2 insns per LDRSB (3 per LDRB). */
  if (cnt >= 3) {
    u32 bases[2] = { 16u, 16u };
    int nbases = 0, b_idx[8], disp[8];
    int used_plain[2] = { 0, 0 }, used_rm[2] = { 0, 0 };
    int cur_delta[2] = { 0, 0 }, max_plain[2] = { 0, 0 }, max_rm[2] = { 0, 0 };
    u32 rd_mask = 0u, base_mask = 0u;
    int hoist_ok = 1;

    for (k = 0; k < cnt; k++) {
      int b = -1, j;
      if (form[k] == 0 && !have_shared_rm) { hoist_ok = 0; break; }
      for (j = 0; j < nbases; j++)
        if (bases[j] == rn[k]) { b = j; break; }
      if (b < 0) {
        if (nbases >= 2) { hoist_ok = 0; break; }
        b = nbases++;
        bases[b] = rn[k];
        base_mask |= (1u << rn[k]);
      }
      b_idx[k] = b;
      rd_mask |= (1u << rd[k]);
      if (form[k] == 0) {
        disp[k] = cur_delta[b];
        used_rm[b] = 1;
        if (disp[k] > max_rm[b]) max_rm[b] = disp[k];
      } else if (form[k] == 1) {
        disp[k] = cur_delta[b];
        used_plain[b] = 1;
        if (disp[k] > max_plain[b]) max_plain[b] = disp[k];
        cur_delta[b] += (int)arg[k];
      } else {
        disp[k] = cur_delta[b] + (int)arg[k];
        used_plain[b] = 1;
        if (disp[k] > max_plain[b]) max_plain[b] = disp[k];
      }
      if (disp[k] > 15 || cur_delta[b] > 127) { hoist_ok = 0; break; }
    }
    if (hoist_ok && (rd_mask & base_mask) == 0u) {
      static const unsigned ptr_regs[2][2] = {
        { SH4_REG_ARG0, SH4_REG_ARG2 }, /* b=0: plain->R4, rm->R6 */
        { SH4_REG_ARG1, SH4_REG_ARG3 }  /* b=1: plain->R5, rm->R7 */
      };
      u8 *gmiss[4];
      int ng = 0, b, max_all = 0;

      for (b = 0; b < nbases; b++) {
        if (used_plain[b] && max_plain[b] > max_all) max_all = max_plain[b];
        if (used_rm[b] && max_rm[b] > max_all)       max_all = max_rm[b];
      }

      sh4g_litseg_reserve_bytes(tp, 4, 200u);
      sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);    /* R3 = iwram_data */
      sh4g_const(tp, 0x8000u - (u32)max_all, SH4_REG_T1);    /* R2 = limit */

      /* Single-subtract canonical IWRAM + straddle guard per active pointer:
       * (u32)(addr - 0x03000000) < (0x8000 - max_all) validates region 3,
       * non-wrap across 32 KiB, and produces the exact IWRAM offset in R0. */
      for (b = 0; b < nbases; b++) {
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, bases[b], SH4_REG_T0);     /* R1 = reg[base] */
          sh4g_close(tp, &cg); }
        if (used_plain[b]) {
          sh4g_const(tp, 0x03000000u, ptr_regs[b][0]);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
            sh4_emit_sub(&cg, ptr_regs[b][0], SH4_REG_RET);
            sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
            sh4g_close(tp, &cg); }
          gmiss[ng++] = sh4g_emit_bt_placeholder(tp);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T2, ptr_regs[b][0]);
            sh4_emit_add_reg(&cg, SH4_REG_RET, ptr_regs[b][0]);
            sh4g_close(tp, &cg); }
        }
        if (used_rm[b]) {
          sh4g_const(tp, 0x03000000u, ptr_regs[b][1]);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_load_greg(&cg, shared_rm, SH4_REG_RET);
            sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_RET);  /* R0 = base + rm */
            sh4_emit_sub(&cg, ptr_regs[b][1], SH4_REG_RET);
            sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
            sh4g_close(tp, &cg); }
          gmiss[ng++] = sh4g_emit_bt_placeholder(tp);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T2, ptr_regs[b][1]);
            sh4_emit_add_reg(&cg, SH4_REG_RET, ptr_regs[b][1]);
            sh4g_close(tp, &cg); }
        }
      }

      /* Hot body: zero guards, 2 insns per LDRSB (3 per LDRB). */
      { sh4_codegen cg = sh4g_open(tp);
        for (k = 0; k < cnt; k++) {
          unsigned pr = ptr_regs[b_idx[k]][form[k] == 0 ? 1 : 0];
          if (disp[k] == 0)
            sh4_emit_mov_b_load(&cg, pr, SH4_REG_RET);
          else
            sh4_emit_mov_b_load_disp(&cg, pr, (unsigned)disp[k]);
          if (!is_signed[k])
            sh4_emit_extu_b(&cg, SH4_REG_RET, SH4_REG_RET);
          sh4_emit_store_greg(&cg, SH4_REG_RET, rd[k]);
        }
        for (b = 0; b < nbases; b++) {
          if (cur_delta[b] > 0) {
            int pin = sh4_pinned_reg(bases[b]);
            if (pin >= 0) {
              sh4_emit_add_imm(&cg, cur_delta[b], (unsigned)pin);
              continue;
            }
            sh4_emit_load_greg(&cg, bases[b], SH4_REG_T0);
            sh4_emit_add_imm(&cg, cur_delta[b], SH4_REG_T0);
            sh4_emit_store_greg(&cg, SH4_REG_T0, bases[b]);
          }
        }
        sh4g_close(tp, &cg); }
      all_done = sh4g_emit_bra_debit(tp, cnt);

      /* Exact fallback on any guard miss (no guest state modified yet). */
      while (ng)
        sh4g_patch_cond(gmiss[--ng], *tp);
      for (k = 0; k < cnt; k++) {
        sh4_greg_forward_reset();
        sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
      }
      sh4g_patch_bra(all_done, *tp);
      return 1;
    }
  }

  sh4g_litseg_reserve_bytes(tp, 4, (u32)(32 + cnt * 24));
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2); /* R3 = iwram_data */
  sh4g_const(tp, 0x7FFFu, SH4_REG_ARG1);              /* R5 = 0x7FFF */
  if (have_shared_rm) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, shared_rm, SH4_REG_ARG2); /* R6 = reg[shared_rm] */
    sh4g_close(tp, &cg);
  }

  for (k = 0; k < cnt; k++) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn[k], SH4_REG_T0);     /* R1 = base */
      if (form[k] == 0) {
        if (have_shared_rm) {
          sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_T0);
        } else {
          sh4_emit_load_greg(&cg, arg[k], SH4_REG_T1);
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
        }
      } else if (form[k] == 2 && arg[k] != 0u) {
        sh4_emit_add_imm(&cg, (int)arg[k], SH4_REG_T0);
      }
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 3);                     /* T = IWRAM */
      sh4g_close(tp, &cg); }
    iw_miss[k] = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG1, SH4_REG_RET);   /* R0 = addr & 0x7FFF */
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      if (!is_signed[k])
        sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
      if (form[k] == 1 && arg[k] != 0u) {
        sh4_emit_add_imm(&cg, (int)arg[k], SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, rn[k]);
      }
      sh4g_close(tp, &cg); }
  }
  sh4g_cycle_debit(tp, cnt);                          /* cnt * 1 IWRAM nseq */
  all_done = sh4g_emit_bra_placeholder(tp);

  /* Per-step miss bridges: debit k completed loads, then enter normal[k]. */
  for (k = 0; k < cnt; k++) {
    sh4g_patch_cond(iw_miss[k], *tp);
    if (k > 0) sh4g_cycle_debit(tp, k);
    bra_normal[k] = sh4g_emit_bra_placeholder(tp);
  }
  for (k = 0; k < cnt; k++) {
    sh4g_patch_bra(bra_normal[k], *tp);
    sh4_greg_forward_reset();
    sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
  }
  sh4g_patch_bra(all_done, *tp);
  return 1;
}
#endif

#endif /* CGBA_SH4_ARM_LDST_EMIT_H */
