#ifndef CGBA_SH4_ARM_LDST_EMIT_H
#define CGBA_SH4_ARM_LDST_EMIT_H

/*
 * Native SH-4A emission for ARM single transfers (the #2 hottest class):
 * mapped loads (IWRAM/EWRAM/IO/VRAM/Palette/OAM/ROM) and plain RAM/VRAM/IO/
 * Palette stores route through the shared compact8+OOL fastmem routines (or
 * inline IWRAM/ROM8 fast paths for IWRAM code). Side-effecting/open regions
 * (BIOS/open bus, backup, unaligned, SMC-hit RAM stores) fall back to the C
 * helper cgba_sh4_arm_ldst so writeback, SMC, and DMA/IRQ alerts stay exact.
 *
 * Big-endian host: guest RAM is little-endian, so a word read is byte-reversed
 * (swap.b;swap.w;swap.b). SH-4A faults on an unaligned mov.l, so the fast path is
 * only taken for aligned addresses (else the C path, which rotates).
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_fastmem.h"

extern u8 *memory_map_read[];          /* gba_memory.c: 32KB host-page table */
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */

#ifdef CGBA_SH4_ARM_LDST_NATIVE
typedef struct sh4g_arm_ldst_info {
  u32 pre, up, writeback, is_load, rn, rd;
  u32 offset, rm, shoff_type, shoff_amount;
  int kind, align_mask, effective_wb, reg_offset, shift_offset;
} sh4g_arm_ldst_info;

/* Decode an ARM single transfer (LDR/STR/B/H/SH/SB). Returns 1 on supported
 * native forms (rejecting PC dest, shifted PC, ROR/RRX/reg-shift, and
 * writeback-with-rd==rn loads), 0 to fall back to C. */
static inline int sh4g_arm_ldst_decode(u32 opcode, sh4g_arm_ldst_info *d)
{
  d->pre          = (opcode >> 24) & 1u;
  d->up           = (opcode >> 23) & 1u;
  d->writeback    = (opcode >> 21) & 1u;
  d->is_load      = (opcode >> 20) & 1u;
  d->rn           = (opcode >> 16) & 0xFu;
  d->rd           = (opcode >> 12) & 0xFu;
  d->offset       = 0u;
  d->rm           = 0u;
  d->shoff_type   = 0u;
  d->shoff_amount = 0u;
  d->reg_offset   = 0;
  d->shift_offset = 0;
  d->effective_wb = (int)(d->writeback || !d->pre);

  if (d->rd == 15u) return 0;                        /* PC destination -> C */
  if (d->rn == 15u && (!d->is_load || d->writeback || !d->pre ||
                       (opcode & 0x02000000u)))
    return 0;
  if (d->is_load && d->effective_wb && d->rd == d->rn)
    return 0;                                        /* helper writes Rn first */

  if ((opcode & 0x0E000090u) == 0x00000090u) {       /* halfword / signed */
    u32 signed_ld = (opcode >> 6) & 1u, half_w = (opcode >> 5) & 1u;
    if (opcode & 0x00400000u)                        /* immediate (8-bit) */
      d->offset = ((opcode >> 4) & 0xF0u) | (opcode & 0xFu);
    else                                             /* register offset */
      { d->reg_offset = 1; d->rm = opcode & 0xFu; }
    if ((!d->is_load && (signed_ld || !half_w)) ||
        (d->is_load && !signed_ld && !half_w))
      return 0;                                      /* reserved / mul / swap */
    if (!d->is_load)              { d->kind = CGBA_FM_LOAD_UH; d->align_mask = 1; }
    else if (signed_ld && half_w) { d->kind = CGBA_FM_LOAD_SH; d->align_mask = 1; }
    else if (signed_ld)           { d->kind = CGBA_FM_LOAD_SB; d->align_mask = 0; }
    else                          { d->kind = CGBA_FM_LOAD_UH; d->align_mask = 1; }
  } else if ((opcode & 0x0C000000u) == 0x04000000u) { /* normal word / byte */
    if (opcode & 0x02000000u) {                      /* register offset */
      u32 st = (opcode >> 5) & 3u, sa = (opcode >> 7) & 0x1Fu;
      if (opcode & 0x10u) return 0;                  /* reg-specified shift -> C */
      d->rm = opcode & 0xFu;
      if (sa == 0u && st == 0u) d->reg_offset = 1;   /* LSL #0 = plain reg */
      else if (st == 3u)        return 0;            /* ROR/RRX -> C */
      else { d->shift_offset = 1; d->shoff_type = st; d->shoff_amount = sa; }
    } else {
      d->offset = opcode & 0xFFFu;                   /* 12-bit immediate */
    }
    if ((opcode >> 22) & 1u) { d->kind = CGBA_FM_LOAD_B; d->align_mask = 0; }
    else                     { d->kind = CGBA_FM_LOAD_W; d->align_mask = 3; }
  } else {
    return 0;
  }
  if ((d->reg_offset || d->shift_offset) && (d->rm == 15u || d->rn == 15u))
    return 0;
  return 1;
}

/* Emit transfer address -> R1 (SH4_REG_T0) and optional writeback base -> R6
 * (SH4_REG_ARG2) without modifying any guest register. Shared by single ARM
 * fastmem/IWRAM transfers and Path 2 load bursts. */
static inline void sh4g_arm_ldst_emit_addr_wb(u8 **tp,
  const sh4g_arm_ldst_info *d)
{
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, d->rn, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  if (d->shift_offset || d->reg_offset) {
    if (d->shift_offset) {
      sh4g_arm_shift_imm_op2(tp, d->shoff_type, d->shoff_amount, d->rm);
    } else {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, d->rm, SH4_REG_T1);
      sh4g_close(tp, &cg);
    }
    { sh4_codegen cg = sh4g_open(tp);
      if (d->pre) {
        if (d->up) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
        else       sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
      }
      if (d->effective_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        if (!d->pre) {
          if (d->up) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
          else       sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG2);
        }
      }
      sh4g_close(tp, &cg); }
  } else {
    s32 soff = d->up ? (s32)d->offset : -(s32)d->offset;
    if (soff == 0) {
      if (d->effective_wb) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4g_close(tp, &cg);
      }
    } else if (soff >= -128 && soff <= 127) {
      sh4_codegen cg = sh4g_open(tp);
      if (d->pre) {
        sh4_emit_add_imm(&cg, (int)soff, SH4_REG_T0);
        if (d->effective_wb)
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      } else if (d->effective_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, (int)soff, SH4_REG_ARG2);
      }
      sh4g_close(tp, &cg);
    } else {
      sh4g_const(tp, (u32)soff, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        if (d->pre)
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
        if (d->effective_wb) {
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
          if (!d->pre)
            sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_ARG2);
        }
        sh4g_close(tp, &cg); }
    }
  }
}
#endif

/* Direct zero-branch load when ARM constant propagation knows the effective
 * address is aligned static IWRAM/EWRAM/IO (no writeback). Mirrors
 * sh4g_thumb_ldst_const_native and returns fixed non-seq cycle debit (or 0). */
static inline int sh4g_arm_ldst_const_native(u8 **tp, u32 opcode,
  u32 const_mask, const u32 const_val[16])
{
#ifndef CGBA_SH4_ARM_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)const_mask;
  (void)const_val;
  return 0;
#else
  sh4g_arm_ldst_info d;
  u32 off, address;

  if (!sh4g_arm_ldst_decode(opcode, &d))
    return 0;
  if (!d.is_load || d.effective_wb || d.rn == 15u || d.shift_offset)
    return 0;
  if (!(const_mask & (1u << d.rn)))
    return 0;
  if (d.reg_offset) {
    if (!(const_mask & (1u << d.rm)))
      return 0;
    off = const_val[d.rm];
  } else {
    off = d.offset;
  }
  address = d.up ? (const_val[d.rn] + off) : (const_val[d.rn] - off);
  return sh4g_emit_const_addr_load(tp, d.kind, address, d.rd);
#endif
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
  sh4g_arm_ldst_info d;

  if (!sh4g_arm_ldst_decode(opcode, &d))
    return 0;

  if (d.rn == 15u) {                    /* literal pool: constant address */
    u32 caddr = (pc + 8u) + (d.up ? d.offset : (u32)-(int32_t)d.offset);
    sh4g_const(tp, caddr, SH4_REG_T0);
    goto have_address;
  }

  /* Compact non-IWRAM ARM load (pre-indexed, no writeback): pack the final
   * address instruction into the JSR @R0 delay slot (mirrors Thumb loads). */
  if (d.is_load && !d.effective_wb && (pc >> 24) != 0x03) {
    uint16_t load_delay_op;
    if (d.shift_offset || d.reg_offset) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, d.rn, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (d.shift_offset) {
        sh4g_arm_shift_imm_op2(tp, d.shoff_type, d.shoff_amount, d.rm);
      } else {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, d.rm, SH4_REG_T1);
        sh4g_close(tp, &cg);
      }
      load_delay_op = d.up ? 0x312Cu : 0x3128u;      /* ADD/SUB R2, R1 */
    } else {
      s32 soff = d.up ? (s32)d.offset : -(s32)d.offset;
      load_delay_op = sh4g_emit_compact_load_delay_imm(tp, d.rn, soff);
    }
    sh4g_fastmem_site_compact(tp, d.kind,
                              (const void *)cgba_sh4_arm_ldst, (u32)opcode,
                              (u32)pc, cycle_count, d.rd, load_delay_op);
    return 1;
  }

  sh4g_arm_ldst_emit_addr_wb(tp, &d);

have_address:
  {
    u8 *iwram_skip = NULL, *rom8_skip = NULL;

    if ((pc >> 24) == 0x03) {
      int has_rom8 = d.is_load && cgba_gamepak_fragment_blocks != NULL &&
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

      if (d.align_mask) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, d.align_mask);         /* T = guest aligned */
        sh4g_close(tp, &cg);
        iwram_miss[niw++] = sh4g_emit_bf_placeholder(tp);
      }

      sh4g_const(tp, 0x7FFFu, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);  /* R0 = addr & 0x7FFF */
        sh4g_close(tp, &cg); }
      sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);

      if (d.is_load) {
        sh4_codegen cg = sh4g_open(tp);
        sh4g_emit_host_load_r0(&cg, d.kind, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_store_greg(&cg, SH4_REG_T1, d.rd);
        if (d.effective_wb)
          sh4_emit_store_greg(&cg, SH4_REG_ARG2, d.rn);
        sh4g_close(tp, &cg);
      } else {
        /* Fast SMC bypass via cgba_iwram_code_end, else check tag at iwram + off. */
        u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                          SH4_REG_RET, SH4_REG_ARG0, SH4_REG_T1);
        sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG0);
        { sh4_codegen cg = sh4g_open(tp);
          if (d.kind == CGBA_FM_LOAD_W)
            sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
          else if (d.kind == CGBA_FM_LOAD_UH)
            sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
          else
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG0, SH4_REG_T1);
          sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T1); /* T = tag clear */
          sh4g_close(tp, &cg); }
        iwram_miss[niw++] = sh4g_emit_bf_placeholder(tp);
        sh4g_patch_cond(smc_ok, *tp);

        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, d.rd, SH4_REG_T1);
          if (d.kind == CGBA_FM_LOAD_W) {
            sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          } else if (d.kind == CGBA_FM_LOAD_UH) {
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          } else {
            sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
          if (d.effective_wb)
            sh4_emit_store_greg(&cg, SH4_REG_ARG2, d.rn);
          sh4g_close(tp, &cg); }
      }
      sh4g_cycle_debit(tp, 1);                       /* IWRAM nseq cost */
      iwram_skip = sh4g_emit_bra_placeholder(tp);

      while (niw)
        sh4g_patch_cond(iwram_miss[--niw], *tp);

      if (has_rom8) {
        u8 *rom8_miss[3];
        int nrom8 = 0;

        if (d.align_mask) {
          sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
          sh4_emit_mov_imm(&cg, d.align_mask, SH4_REG_T2);
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
          sh4g_emit_host_load_r0(&cg, d.kind, SH4_REG_T2, SH4_REG_T1);
          sh4_emit_store_greg(&cg, SH4_REG_T1, d.rd);
          if (d.effective_wb)
            sh4_emit_store_greg(&cg, SH4_REG_ARG2, d.rn);
          sh4g_close(tp, &cg); }

        sh4g_cycle_debit(tp, (int)ws_cyc_nseq[8][d.kind == CGBA_FM_LOAD_W ? 1 : 0]);
        rom8_skip = sh4g_emit_bra_placeholder(tp);

        while (nrom8)
          sh4g_patch_cond(rom8_miss[--nrom8], *tp);
      }
    }

    /* Out-of-line fast path: handles EWRAM/IO/VRAM/ROM and falls back to C. */
    {
      int fm = d.is_load ? d.kind
        : (d.kind == CGBA_FM_LOAD_W ? CGBA_FM_STORE_W
           : d.kind == CGBA_FM_LOAD_UH ? CGBA_FM_STORE_UH : CGBA_FM_STORE_B);
      if (d.effective_wb)
        fm += CGBA_FM_WB;
      sh4g_fastmem_site(tp, fm,
                        (const void *)cgba_sh4_arm_ldst, (u32)opcode, (u32)pc,
                        cycle_count, d.rd, d.effective_wb ? (int)d.rn : -1);
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
/* Decode an unconditional ARM load opcode for sh4g_arm_load_run_native. */
static inline int sh4g_arm_load_classify(u32 op, int *kind, int *align_mask,
  u32 *rn, u32 *rd, u32 *pre, u32 *up, u32 *wb, int *is_reg, u32 *arg)
{
  sh4g_arm_ldst_info d;
  if ((op >> 28) != 0xEu || !sh4g_arm_ldst_decode(op, &d)) return 0;
  if (!d.is_load || d.rn == 15u || d.shift_offset) return 0;
  *kind = d.kind; *align_mask = d.align_mask;
  *rn = d.rn; *rd = d.rd; *pre = d.pre; *up = d.up; *wb = d.writeback;
  *is_reg = d.reg_offset; *arg = d.reg_offset ? d.rm : d.offset;
  return 1;
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
      { sh4g_arm_ldst_info d0;
        memset(&d0, 0, sizeof(d0));
        d0.rn = rn0; d0.pre = pre0; d0.up = up0;
        d0.effective_wb = eff_wb0;
        d0.reg_offset = is_reg0;
        d0.rm = is_reg0 ? arg0 : 0u;
        d0.offset = is_reg0 ? 0u : arg0;
        sh4g_arm_ldst_emit_addr_wb(tp, &d0); }

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
            sh4g_emit_host_load_r0(&cg, kind[k], SH4_REG_T2, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd[k]);
            tot_ws += (int)ws_cyc_nseq[8][kind[k] == CGBA_FM_LOAD_W ? 1 : 0];
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
          sh4g_emit_host_load_r0(&cg, kind[k], SH4_REG_T2, SH4_REG_T1);
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
