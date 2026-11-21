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

/* Emit native SH4 for the ARM transfer `opcode`, or return 0 to fall back to C. */
SH4G_NOINLINE int sh4g_arm_ldst_native(u8 **tp, u32 opcode, u32 pc,
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

  if (rn == 15) {                       /* literal pool: constant address */
    u32 caddr = (pc + 8) + (up ? offset : (u32)-(int32_t)offset);
    sh4g_const(tp, caddr, SH4_REG_T0);
    goto have_address;
  }

#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
  /* Compact non-IWRAM ARM load (pre-indexed, no writeback): pack the final
   * address instruction into the JSR @R0 delay slot (mirrors Thumb loads). */
  if (is_load && !effective_wb && (pc >> 24) != 0x03 &&
      (unsigned)cycle_count <= 0xFFFFu) {
    uint16_t load_delay_op = 0x0009u;
    if (shift_offset || reg_offset) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (shift_offset) {
        sh4g_arm_shift_imm_op2(tp, shoff_type, shoff_amount, rm);
      } else {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
        sh4g_close(tp, &cg);
      }
      load_delay_op = up ? 0x312Cu : 0x3128u;        /* ADD/SUB R2, R1 */
    } else {
      s32 soff = up ? (s32)offset : -(s32)offset;
      if (soff == 0) {
#if CGBA_SH4_REG_FORWARD
        if (*tp == sh4_forward_at && rn == sh4_forward_guest) {
          if (sh4_forward_host == SH4_REG_RET) {
            sh4_codegen cg = sh4g_open(tp);
            sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
            sh4g_close(tp, &cg);
          } else if (sh4_forward_host == SH4_REG_T0) {
            sh4_forward_at = NULL;
          } else {
            load_delay_op = (uint16_t)(0x6103u | (sh4_forward_host << 4));
            sh4_forward_at = NULL;
          }
        } else
#endif
        {
#if CGBA_SH4_PIN_GUEST_R0
          if (rn == 0u)
            load_delay_op = 0x61B3u;                 /* MOV R11, R1 */
          else
#endif
#if CGBA_SH4_PIN_GUEST_R1
          if (rn == 1u)
            load_delay_op = 0x61C3u;                 /* MOV R12, R1 */
          else
#endif
            load_delay_op = (uint16_t)(0x51E0u | (rn & 0xFu));
        }
      } else if (soff >= -128 && soff <= 127) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4g_close(tp, &cg);
        load_delay_op = (uint16_t)(0x7100u | ((u32)soff & 0xFFu));
      } else if (soff > 127 && soff <= 254) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4_emit_add_imm(&cg, 127, SH4_REG_T0);
        sh4g_close(tp, &cg);
        load_delay_op = (uint16_t)(0x7100u | ((u32)(soff - 127) & 0xFFu));
      } else if (soff < -128 && soff >= -256) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4_emit_add_imm(&cg, -128, SH4_REG_T0);
        sh4g_close(tp, &cg);
        load_delay_op = (uint16_t)(0x7100u | ((u32)(soff + 128) & 0xFFu));
      } else {
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
          sh4g_close(tp, &cg); }
        sh4g_const(tp, (u32)soff, SH4_REG_T1);
        load_delay_op = 0x312Cu;                     /* ADD R2, R1 */
      }
    }
    sh4g_fastmem_site_compact(tp, kind,
                              (const void *)cgba_sh4_arm_ldst, (u32)opcode,
                              (u32)pc, cycle_count, rd, load_delay_op);
    return 1;
  }
#endif

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
    u8 *iwram_skip = NULL, *rom8_skip = NULL;

    if ((pc >> 24) == 0x03) {
      int has_rom8 = is_load && cgba_gamepak_fragment_blocks != NULL &&
                     gamepak_size > 0x8000u;
      u32 rom8_limit = has_rom8
        ? ((gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u)
        : 0u;
      u8 *iwram_miss[3];
      int niw = 0;

      sh4g_litseg_reserve_bytes(tp, 8, 220u);

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

      if (has_rom8) {
        u8 *rom8_miss[3];
        int nrom8 = 0;

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

        sh4g_cycle_debit(tp, (int)ws_cyc_nseq[8][kind == LDK_W ? 1 : 0]);
        rom8_skip = sh4g_emit_bra_placeholder(tp);

        while (nrom8)
          sh4g_patch_cond(rom8_miss[--nrom8], *tp);
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
  }
  return 1;

#endif
}

#ifdef CGBA_SH4_ARM_LDST_NATIVE
/* Decode an unconditional ARM load opcode for sh4g_arm_load_run_native.
 * Returns 1 if it is a supported non-PC-dest load, filling kind, align_mask,
 * rn, rd, pre, up, wb, is_reg, off_or_rm. */
static inline int sh4g_arm_load_classify(u32 op, int *kind, int *align_mask,
  u32 *rn, u32 *rd, u32 *pre, u32 *up, u32 *wb, int *is_reg, u32 *arg)
{
  if ((op >> 28) != 0xEu) return 0;
  *pre = (op >> 24) & 1u;
  *up  = (op >> 23) & 1u;
  *wb  = (op >> 21) & 1u;
  *rn  = (op >> 16) & 0xFu;
  *rd  = (op >> 12) & 0xFu;
  if (*rn == 15u || *rd == 15u) return 0;
  if (((op >> 20) & 1u) == 0u) return 0;               /* must be load */
  if ((*wb || !*pre) && *rd == *rn) return 0;

  if ((op & 0x0E000090u) == 0x00000090u) {             /* halfword / signed */
    u32 s = (op >> 6) & 1u, h = (op >> 5) & 1u;
    if (!s && !h) return 0;
    if (s && h)      { *kind = LDK_SH; *align_mask = 1; }
    else if (s)      { *kind = LDK_SB; *align_mask = 0; }
    else             { *kind = LDK_UH; *align_mask = 1; }
    if (op & 0x00400000u) {
      *is_reg = 0;
      *arg = ((op >> 4) & 0xF0u) | (op & 0xFu);
    } else {
      *is_reg = 1;
      *arg = op & 0xFu;
      if (*arg == 15u) return 0;
    }
    return 1;
  }
  if ((op & 0x0C000000u) == 0x04000000u) {             /* LDR / LDRB */
    if ((op >> 22) & 1u) { *kind = LDK_B; *align_mask = 0; }
    else                 { *kind = LDK_W; *align_mask = 3; }
    if (op & 0x02000000u) {
      if (op & 0xFF0u) return 0;                       /* unshifted reg only */
      *is_reg = 1;
      *arg = op & 0xFu;
      if (*arg == 15u) return 0;
    } else {
      *is_reg = 0;
      *arg = op & 0xFFFu;
    }
    return 1;
  }
  return 0;
}

/* Unified dual-region (IWRAM + ROM8) ARM load-run emitter.
 * Replaces the old conflicting Peepholes 1, 4, and 7 with one pass that
 * handles BOTH IWRAM and ROM8 targets cleanly:
 *   Path 1: 2..8 consecutive byte loads (LDRB/LDRSB [rn,+rm], [rn],#+imm,
 *           [rn,#+imm]) -> hoisted multi-base IWRAM fast path and/or same-page
 *           ROM8 reg-run + IWRAM dual-region loop.
 *   Path 2: 2..4 consecutive same-base mixed-width loads (LDR/LDRH/LDRSH/LDRB/
 *           LDRSB) within [0..15] bytes of addr0 -> single-guard ROM8 + IWRAM
 *           dual-region burst (works in both IWRAM and ROM code).
 * Returns the number of consumed ops (2..cnt), or 0 if not fused. */
SH4G_NOINLINE int sh4g_arm_load_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  int in_iwram = ((pc >> 24) == 0x03);
  int has_rom8 = (cgba_gamepak_fragment_blocks != NULL && gamepak_size > 0x8000u);
  u32 rom8_limit = has_rom8
    ? ((gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u) : 0u;
  int bcnt = 0;

  if (cnt < 2 || cnt > 8) return 0;

  /* ---- Path 1: Byte-load runs (2..8 ops of LDRB/LDRSB) ---- */
  {
    u8 form[8], is_signed[8];
    u32 rn[8], rd[8], arg[8];
    u32 written_mask = 0u, shared_rm = 16u;
    int have_shared_rm = 0, same_rn_reg = 1, k;

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
        break;
      }
      rn[k] = (op >> 16) & 0xFu;
      rd[k] = (op >> 12) & 0xFu;
      if (rn[k] == 15u || rd[k] == 15u) break;
      if (form[k] == 0) {
        if (arg[k] == 15u) break;
      } else {
        if (arg[k] > 127u) break;
        if (form[k] == 1 && rd[k] == rn[k]) break;
      }
      if (form[k] != 0 || rn[k] != rn[0] || rd[k] == rn[0])
        same_rn_reg = 0;
      written_mask |= (1u << rd[k]);
      if (form[k] == 1) written_mask |= (1u << rn[k]);
      bcnt++;
    }

    if (bcnt >= 2 && (in_iwram || bcnt >= 3)) {
      int can_rom8_reg = has_rom8 && same_rn_reg;

      for (k = 0; k < bcnt; k++) {
        if (form[k] == 0) {
          if (shared_rm == 16u) { shared_rm = arg[k]; have_shared_rm = 1; }
          else if (shared_rm != arg[k]) { have_shared_rm = 0; break; }
        }
      }
      if (have_shared_rm && (written_mask & (1u << shared_rm)))
        have_shared_rm = 0;

      /* Hoisted multi-base IWRAM fast path (IWRAM code, bcnt >= 3, <= 2 bases,
       * bases never overwritten as rd, optional single shared read-only rm,
       * all effective displacements 0..15).  In ROM code, single-base imm
       * byte runs fall through to Path 2 which has dual ROM8 + IWRAM bursts. */
      if (in_iwram && bcnt >= 3) {
        u32 bases[2] = { 16u, 16u };
        int nbases = 0, b_idx[8], disp[8];
        int used_plain[2] = { 0, 0 }, used_rm[2] = { 0, 0 };
        int cur_delta[2] = { 0, 0 }, max_plain[2] = { 0, 0 }, max_rm[2] = { 0, 0 };
        u32 rd_mask = 0u, base_mask = 0u;
        int hoist_ok = 1;

        for (k = 0; k < bcnt; k++) {
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
            { SH4_REG_ARG0, SH4_REG_ARG2 },
            { SH4_REG_ARG1, SH4_REG_ARG3 }
          };
          u8 *gmiss[4], *all_done;
          int ng = 0, b, max_all = 0;

          for (b = 0; b < nbases; b++) {
            if (used_plain[b] && max_plain[b] > max_all) max_all = max_plain[b];
            if (used_rm[b] && max_rm[b] > max_all)       max_all = max_rm[b];
          }

          sh4g_litseg_reserve_bytes(tp, 4, 200u);
          sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
          sh4g_const(tp, 0x8000u - (u32)max_all, SH4_REG_T1);

          for (b = 0; b < nbases; b++) {
            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_load_greg(&cg, bases[b], SH4_REG_T0);
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
                sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_RET);
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

          { sh4_codegen cg = sh4g_open(tp);
            for (k = 0; k < bcnt; k++) {
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
          all_done = sh4g_emit_bra_debit(tp, bcnt);

          while (ng)
            sh4g_patch_cond(gmiss[--ng], *tp);
          for (k = 0; k < bcnt; k++) {
            sh4_greg_forward_reset();
            sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
          }
          sh4g_patch_bra(all_done, *tp);
          return bcnt;
        }
      }

      /* Dual-region byte run:
       *  - If all ops are same-base [rn, +rm] with ROM8 available: try same-page
       *    ROM8 first (7 insns/load), and on op[0] miss fall cleanly into the
       *    IWRAM per-op loop (resolving the old Peephole 4 vs 7 conflict!).
       *  - Otherwise (in IWRAM code): run the IWRAM per-op loop directly. */
      if (can_rom8_reg || in_iwram) {
        u8 *rom8_miss[2] = { NULL, NULL };
        u8 *page_miss[8], *bra_rom8_done = NULL, *bra_to_iwram = NULL;
        u8 *bra_rom8_norm[8], *iw_miss[8], *bra_iw_norm[8], *bra_iw_done;
        int ws = (int)ws_cyc_nseq[8][0];

        if (can_rom8_reg) {
          sh4g_litseg_reserve_bytes(tp, 8, 280u);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_load_greg(&cg, rn[0], SH4_REG_ARG2);
            sh4_emit_load_greg(&cg, arg[0], SH4_REG_T0);
            sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_T0);
            sh4g_close(tp, &cg); }
          { int nr8 = 0;
            sh4g_emit_rom8_page_lookup(tp, rom8_limit,
                                       cgba_gamepak_fragment_blocks + 8,
                                       SH4_REG_T0, rom8_miss, &nr8); }
          sh4g_const(tp, 0x0FFFu, SH4_REG_RET);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
            if (!is_signed[0])
              sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd[0]);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG0);
            sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_ARG0);
            sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_ARG2);
            sh4g_close(tp, &cg); }
          sh4g_const(tp, 0x1000u, SH4_REG_ARG1);

          for (k = 1; k < bcnt; k++) {
            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_load_greg(&cg, arg[k], SH4_REG_RET);
              sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_RET);
              sh4_emit_cmphs(&cg, SH4_REG_ARG1, SH4_REG_RET);
              sh4g_close(tp, &cg); }
            page_miss[k] = sh4g_emit_bt_placeholder(tp);
            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
              if (!is_signed[k])
                sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
              sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
              sh4g_close(tp, &cg); }
          }
          sh4g_cycle_debit(tp, bcnt * ws);
          bra_rom8_done = sh4g_emit_bra_placeholder(tp);

          sh4g_patch_cond(rom8_miss[0], *tp);
          sh4g_patch_cond(rom8_miss[1], *tp);
          bra_to_iwram = sh4g_emit_bra_placeholder(tp);
          for (k = 1; k < bcnt; k++) {
            sh4g_patch_cond(page_miss[k], *tp);
            sh4g_cycle_debit(tp, k * ws);
            bra_rom8_norm[k] = sh4g_emit_bra_placeholder(tp);
          }
          sh4g_patch_bra(bra_to_iwram, *tp);
        }

        sh4g_litseg_reserve_bytes(tp, 4, (u32)(32 + bcnt * 28));
        sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
        sh4g_const(tp, 0x7FFFu, SH4_REG_ARG1);
        if (have_shared_rm) {
          sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, shared_rm, SH4_REG_ARG2);
          sh4g_close(tp, &cg);
        }
        for (k = 0; k < bcnt; k++) {
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_load_greg(&cg, rn[k], SH4_REG_T0);
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
            sh4_emit_cmpeq_imm(&cg, 3);
            sh4g_close(tp, &cg); }
          iw_miss[k] = sh4g_emit_bf_placeholder(tp);
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
            sh4_emit_and(&cg, SH4_REG_ARG1, SH4_REG_RET);
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
        bra_iw_done = sh4g_emit_bra_debit(tp, bcnt);

        for (k = 0; k < bcnt; k++) {
          sh4g_patch_cond(iw_miss[k], *tp);
          if (k > 0) sh4g_cycle_debit(tp, k);
          bra_iw_norm[k] = sh4g_emit_bra_placeholder(tp);
        }
        for (k = 0; k < bcnt; k++) {
          sh4g_patch_bra(bra_iw_norm[k], *tp);
          if (can_rom8_reg && k > 0)
            sh4g_patch_bra(bra_rom8_norm[k], *tp);
          sh4_greg_forward_reset();
          sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
        }
        if (bra_rom8_done)
          sh4g_patch_bra(bra_rom8_done, *tp);
        sh4g_patch_bra(bra_iw_done, *tp);
        return bcnt;
      }
    }
  }

  /* ---- Path 2: Same-base adjacent mixed-width load run (2..4 ops) ----
   * Handles LDR/LDRH/LDRSH/LDRB/LDRSB sharing `rn` with offsets in [0..15]
   * relative to addr0. Single upfront guard for ROM8 and IWRAM before any
   * guest register is written; misses fall cleanly to per-op normal emission. */
  {
    int kind[4], am[4], is_reg0 = 0, pcnt = 1, k;
    u32 rn0, rd[4], pre0, up0, wb0, arg0;
    int eff_wb0, max_delta = 0, delta[4];
    int lim = cnt < 4 ? cnt : 4;

    if (!sh4g_arm_load_classify(ops[0], &kind[0], &am[0], &rn0, &rd[0],
                                &pre0, &up0, &wb0, &is_reg0, &arg0))
      return 0;
    eff_wb0 = (int)(wb0 || !pre0);
    if (rd[0] == rn0) return 0;                        /* op0 clobbers base */
    delta[0] = 0;

    for (k = 1; k < lim; k++) {
      int kk, kam, kreg;
      u32 krn, krd, kpre, kup, kwb, karg;
      s32 soff_k, d;
      if (!sh4g_arm_load_classify(ops[k], &kk, &kam, &krn, &krd,
                                  &kpre, &kup, &kwb, &kreg, &karg))
        break;
      if (!kpre || kwb || kreg || krn != rn0 || kam > am[0])
        break;
      soff_k = kup ? (s32)karg : -(s32)karg;
      if (eff_wb0 && pre0) {
        d = soff_k;
      } else if (!is_reg0) {
        s32 soff0 = up0 ? (s32)arg0 : -(s32)arg0;
        d = eff_wb0 ? (soff0 + soff_k) : (soff_k - soff0);
      } else {
        break;
      }
      if (d < 0 || d > 15 || (d & kam) != 0)
        break;
      kind[k] = kk;
      am[k] = kam;
      rd[k] = krd;
      delta[k] = (int)d;
      if ((int)d > max_delta) max_delta = (int)d;
      pcnt++;
      if (krd == rn0)                                  /* allowed only as last op */
        break;
    }

    if (pcnt >= 2 && (in_iwram || pcnt >= 3)) {
      u8 *rom8_miss[4], *iw_miss[2], *rom8_done = NULL, *iw_done;
      int nrom8 = 0, niw = 0;

      sh4g_litseg_reserve_bytes(tp, 10, 260u);

      /* Compute addr0 -> R1, optional wb -> R6 (no guest state touched yet). */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn0, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (is_reg0) {
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, arg0, SH4_REG_T1);
          if (pre0) {
            if (up0) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
            else     sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
          }
          if (eff_wb0) {
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
            if (!pre0) {
              if (up0) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
              else     sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG2);
            }
          }
          sh4g_close(tp, &cg); }
      } else {
        s32 soff0 = up0 ? (s32)arg0 : -(s32)arg0;
        if (soff0 == 0) {
          if (eff_wb0) {
            sh4_codegen cg = sh4g_open(tp);
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
            sh4g_close(tp, &cg);
          }
        } else if (soff0 >= -128 && soff0 <= 127) {
          sh4_codegen cg = sh4g_open(tp);
          if (pre0) {
            sh4_emit_add_imm(&cg, (int)soff0, SH4_REG_T0);
            if (eff_wb0) sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
          } else if (eff_wb0) {
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
            sh4_emit_add_imm(&cg, (int)soff0, SH4_REG_ARG2);
          }
          sh4g_close(tp, &cg);
        } else {
          sh4g_const(tp, (u32)soff0, SH4_REG_T1);
          { sh4_codegen cg = sh4g_open(tp);
            if (pre0) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
            if (eff_wb0) {
              sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
              if (!pre0) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
            }
            sh4g_close(tp, &cg); }
        }
      }

      /* Fast path A: ROM8 same-4KiB-page burst. */
      if (has_rom8) {
        int tot_ws = 0;
        if (am[0]) {
          sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
          sh4_emit_tst_imm(&cg, am[0]);                /* T = aligned */
          sh4g_close(tp, &cg);
          rom8_miss[nrom8++] = sh4g_emit_bf_placeholder(tp);
        }
        sh4g_emit_rom8_page_lookup(tp, rom8_limit,
                                   cgba_gamepak_fragment_blocks + 8,
                                   SH4_REG_T0, rom8_miss, &nrom8);
        sh4g_const(tp, 0x0FFFu, SH4_REG_ARG0);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_ARG0); /* R4 = off0 */
          sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
          if (max_delta)
            sh4_emit_add_imm(&cg, max_delta, SH4_REG_RET);
          sh4_emit_shlr8(&cg, SH4_REG_RET);
          sh4_emit_tst_imm(&cg, 0x10);                 /* T = fits in page */
          sh4g_close(tp, &cg); }
        rom8_miss[nrom8++] = sh4g_emit_bf_placeholder(tp);

        { sh4_codegen cg = sh4g_open(tp);
          if (eff_wb0)
            sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn0);
          for (k = 0; k < pcnt; k++) {
            sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
            if (delta[k]) sh4_emit_add_imm(&cg, delta[k], SH4_REG_RET);
            sh4g_arm_emit_load_r0_r3(&cg, kind[k]);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
            tot_ws += (int)ws_cyc_nseq[8][kind[k] == LDK_W ? 1 : 0];
          }
          sh4g_close(tp, &cg); }
        rom8_done = sh4g_emit_bra_debit(tp, tot_ws);
        while (nrom8)
          sh4g_patch_cond(rom8_miss[--nrom8], *tp);
      }

      /* Fast path B: IWRAM burst (single canonical range + alignment guard). */
      sh4g_const(tp, 0x03000000u, SH4_REG_T1);
      sh4g_const(tp, 0x8000u - (u32)max_delta, SH4_REG_ARG1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG0);
        sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG0);   /* R4 = addr0 - 0x03000000 */
        sh4_emit_cmphs(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
        sh4g_close(tp, &cg); }
      iw_miss[niw++] = sh4g_emit_bt_placeholder(tp);
      if (am[0]) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, am[0]);                  /* T = aligned */
        sh4g_close(tp, &cg);
        iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);
      }
      sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        if (eff_wb0)
          sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn0);
        for (k = 0; k < pcnt; k++) {
          sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
          if (delta[k]) sh4_emit_add_imm(&cg, delta[k], SH4_REG_RET);
          sh4g_arm_emit_load_r0_r3(&cg, kind[k]);
          sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
        }
        sh4g_close(tp, &cg); }
      iw_done = sh4g_emit_bra_debit(tp, pcnt);

      while (niw)
        sh4g_patch_cond(iw_miss[--niw], *tp);
      for (k = 0; k < pcnt; k++) {
        sh4_greg_forward_reset();
        sh4g_arm_ldst_native(tp, ops[k], pc + (u32)(k * 4), ccs[k]);
      }
      if (rom8_done)
        sh4g_patch_bra(rom8_done, *tp);
      sh4g_patch_bra(iw_done, *tp);
      return pcnt;
    }
  }

  return 0;
}
#endif

#endif /* CGBA_SH4_ARM_LDST_EMIT_H */
