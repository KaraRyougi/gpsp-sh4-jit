/* Native SH-4 Thumb load/store, constant-address, and same-base run emitters.
 * Included via sh4_thumb_dp_emit.h. */
#ifndef CGBA_SH4_THUMB_LDST_EMIT_H
#define CGBA_SH4_THUMB_LDST_EMIT_H

#ifdef CGBA_SH4_THUMB_LDST_NATIVE
typedef struct sh4g_thumb_ldst_info {
  u32 rd, rb, ro, offset;
  int kind, is_load, reg_offset, pc_relative;
} sh4g_thumb_ldst_info;

/* Decode Thumb single load/store (formats 6..11). Returns 1 on match, 0 else. */
static inline int sh4g_thumb_ldst_decode(u32 opcode, sh4g_thumb_ldst_info *out)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  out->rd = opcode & 7u;
  out->rb = (opcode >> 3) & 7u;
  out->ro = 0u;
  out->offset = 0u;
  out->kind = CGBA_FM_LOAD_W;
  out->is_load = 1;
  out->reg_offset = 0;
  out->pc_relative = 0;

  if (hi >= 0x48u && hi <= 0x4Fu) {                  /* LDR Rd,[PC,#imm8*4] */
    out->rd = (opcode >> 8) & 7u;
    out->offset = (opcode & 0xFFu) << 2;
    out->pc_relative = 1;
    return 1;
  }
  if (hi >= 0x50u && hi <= 0x5Fu) {                  /* register-offset forms */
    u32 op = (opcode >> 10) & 3u;
    out->ro = (opcode >> 6) & 7u;
    out->reg_offset = 1;
    if (opcode & 0x0200u) {                          /* format 8: H/S */
      switch (op) {
      case 0: out->kind = CGBA_FM_LOAD_UH; out->is_load = 0; break;
      case 1: out->kind = CGBA_FM_LOAD_SB; break;
      case 2: out->kind = CGBA_FM_LOAD_UH; break;
      case 3: out->kind = CGBA_FM_LOAD_SH; break;
      }
    } else {                                         /* format 7: L/B */
      switch (op) {
      case 0: out->kind = CGBA_FM_LOAD_W; out->is_load = 0; break;
      case 1: out->kind = CGBA_FM_LOAD_B; out->is_load = 0; break;
      case 2: out->kind = CGBA_FM_LOAD_W; break;
      case 3: out->kind = CGBA_FM_LOAD_B; break;
      }
    }
    return 1;
  }
  if (hi >= 0x60u && hi <= 0x7Fu) {                  /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1Fu;
    u32 is_byte = (opcode >> 12) & 1u;
    out->is_load = (int)((opcode >> 11) & 1u);
    out->offset = imm5 << (is_byte ? 0 : 2);
    out->kind = is_byte ? CGBA_FM_LOAD_B : CGBA_FM_LOAD_W;
    return 1;
  }
  if (hi >= 0x80u && hi <= 0x8Fu) {                  /* imm5 halfword */
    out->is_load = (int)((opcode >> 11) & 1u);
    out->offset = ((opcode >> 6) & 0x1Fu) << 1;
    out->kind = CGBA_FM_LOAD_UH;
    return 1;
  }
  if (hi >= 0x90u && hi <= 0x9Fu) {                  /* SP-relative word */
    out->is_load = (int)((opcode >> 11) & 1u);
    out->rd = (opcode >> 8) & 7u;
    out->rb = 13u;                                   /* REG_SP */
    out->offset = (opcode & 0xFFu) << 2;
    return 1;
  }
  return 0;
}
#endif

/* Fast path for the hot Thumb shape:
 *   LDR rB, [pc, #literal]   ; rB = fixed IO / IWRAM / EWRAM address
 *   LDRH/LDRB/LDR rD, [rB, #offset]
 *
 * When translation knows the effective address is aligned static IO/IWRAM/EWRAM,
 * the host pointer is permanent and the read emits directly with zero branches.
 * Returns the fixed non-sequential cycle debit (1 for IO/IWRAM, 3 or 6 for
 * EWRAM) so the caller folds it into block cycle_count. */
static inline int sh4g_thumb_ldst_const_native(u8 **tp, u32 opcode,
  u32 const_mask, const u32 const_val[16])
{
#ifndef CGBA_SH4_THUMB_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)const_mask;
  (void)const_val;
  return 0;
#else
  sh4g_thumb_ldst_info d;
  u32 address;
  int debit;

  if (!sh4g_thumb_ldst_decode(opcode, &d) || !d.is_load || d.pc_relative)
    return 0;
  if (!(const_mask & (1u << d.rb)))
    return 0;
  if (d.reg_offset && !(const_mask & (1u << d.ro)))
    return 0;

  address = const_val[d.rb] + d.offset + (d.reg_offset ? const_val[d.ro] : 0u);
  debit = sh4g_emit_const_addr_load(tp, d.kind, address, d.rd);
#if (defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)) && \
  CGBA_SH4_LDST_DETAIL_COUNTERS
  if (debit) {
    sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_native_thumb_const_io_count,
               SH4_REG_T0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_add_imm(&cg, 1, SH4_REG_T1);
      sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T0);
      sh4g_close(tp, &cg); }
  }
#endif
  return debit;
#endif
}

/* Native Thumb mapped loads and plain RAM stores. Side-effecting/open regions
 * fall back to cgba_sh4_thumb_ldst so DMA/IRQ alerts, I/O semantics, ROM paging
 * and backup stay helper-owned. RAM stores check the SMC tag mirror first and
 * fall back if they would overwrite translated guest code. */
SH4G_NOINLINE int sh4g_thumb_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
#ifndef CGBA_SH4_THUMB_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  return 0;
#else
  sh4g_thumb_ldst_info d;

  if (!sh4g_thumb_ldst_decode(opcode, &d))
    return 0;

  if (d.is_load && !d.pc_relative) {
    uint16_t load_delay_op;
    if (d.reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, d.rb, SH4_REG_T0);
      sh4_emit_load_greg(&cg, d.ro, SH4_REG_T1);
      sh4g_close(tp, &cg);
      load_delay_op = 0x312Cu;                       /* ADD R2, R1 */
    } else {
      load_delay_op = sh4g_emit_compact_load_delay_imm(tp, d.rb, (s32)d.offset);
    }
    sh4g_fastmem_site_compact(tp, d.kind,
                              (const void *)cgba_sh4_thumb_ldst, (u32)opcode,
                              (u32)pc, cycle_count, d.rd, load_delay_op);
    return 1;
  }

  if (d.pc_relative) {
    sh4g_const(tp, ((pc & ~2u) + 4u + d.offset), SH4_REG_T0);
  } else {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, d.rb, SH4_REG_T0);     /* R1 = base */
      sh4g_close(tp, &cg); }
    if (d.reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, d.ro, SH4_REG_T1);
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = base + reg[ro] */
      sh4g_close(tp, &cg);
    } else if (d.offset) {
      if (d.offset <= 127u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, (int)d.offset, SH4_REG_T0);
        sh4g_close(tp, &cg);
      } else if (d.offset <= 254u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 127, SH4_REG_T0);
        sh4_emit_add_imm(&cg, (int)(d.offset - 127u), SH4_REG_T0);
        sh4g_close(tp, &cg);
      } else {
        sh4g_const(tp, d.offset, SH4_REG_T1);
        sh4g_add_reg(tp, SH4_REG_T1, SH4_REG_T0);
      }
    }
  }

  /* Out-of-line fast path (see sh4_fastmem.h): shared routine + compact
   * site; guard failures fall to cgba_sh4_thumb_ldst via the tramp. */
  {
    int fm = d.is_load ? d.kind
      : (d.kind == CGBA_FM_LOAD_W ? CGBA_FM_STORE_W
         : d.kind == CGBA_FM_LOAD_UH ? CGBA_FM_STORE_UH : CGBA_FM_STORE_B);
    sh4g_fastmem_site(tp, fm,
                      (const void *)cgba_sh4_thumb_ldst, (u32)opcode, (u32)pc,
                      cycle_count, d.rd, -1);
  }
  return 1;
#endif
}

#ifdef CGBA_SH4_THUMB_LDST_NATIVE
/* Fuse Thumb constant-RAM-base indexed array load:
 *   op0: ADD rA, rIdx, rC  (fmt2, dead flags) OR ADD rA, rH (fmt5, no flags)
 *        where one operand is a known IWRAM/EWRAM constant `base`
 *   op1: LDRB/LDRH/LDR rD, [rA, #off]
 * Checks `(u32)reg[rIdx] < (ram_size - (base_off + off))` (guaranteed in-region
 * with zero 32-bit wrap) and alignment, then loads straight from host RAM
 * without a fastmem call, skipping `reg[rA]` store when `rD == rA`.
 * Returns 1 on fusion (writing `*out_rA` and `*out_rD`), 0 otherwise. */
static inline int sh4g_thumb_ram_idx_load_native(u8 **tp, u32 op0, u32 op1,
  u32 pc, int cc2, u32 const_mask, const u32 *const_val, int op0_flags_live,
  unsigned *out_rA, unsigned *out_rD)
{
  unsigned rA = 0, rIdx = 0, rD;
  u32 base = 0, off = 0, region, base_off, limit, host_base;
  int kind, align, debit;
  u8 *miss[2], *bra_done;
  int nmiss = 0;

  if ((op0 & 0xFE00u) == 0x1800u) {                  /* fmt2 ADD Rd, Rn, Rm */
    unsigned rn, rm;
    if (op0_flags_live) return 0;
    rA = op0 & 7u;
    rn = (op0 >> 3) & 7u;
    rm = (op0 >> 6) & 7u;
    if ((const_mask & (1u << rn)) && !(const_mask & (1u << rm))) {
      base = const_val[rn]; rIdx = rm;
    } else if ((const_mask & (1u << rm)) && !(const_mask & (1u << rn))) {
      base = const_val[rm]; rIdx = rn;
    } else {
      return 0;
    }
  } else if ((op0 & 0xFF00u) == 0x4400u) {           /* fmt5 ADD Rd, Rs */
    unsigned rs = (op0 >> 3) & 15u;
    rA = (op0 & 7u) | ((op0 >> 4) & 8u);
    if (rA >= 8u || rs == 15u) return 0;
    if ((const_mask & (1u << rA)) && !(const_mask & (1u << rs))) {
      base = const_val[rA]; rIdx = rs;
    } else if ((const_mask & (1u << rs)) && !(const_mask & (1u << rA))) {
      base = const_val[rs]; rIdx = rA;
    } else {
      return 0;
    }
  } else {
    return 0;
  }

  if ((op1 & 0xF800u) == 0x7800u) {                  /* LDRB rD, [rA, #imm5] */
    kind = CGBA_FM_LOAD_B; off = (op1 >> 6) & 31u; align = 0;
  } else if ((op1 & 0xF800u) == 0x8800u) {           /* LDRH rD, [rA, #imm5*2] */
    kind = CGBA_FM_LOAD_UH; off = ((op1 >> 6) & 31u) << 1; align = 1;
  } else if ((op1 & 0xF800u) == 0x6800u) {           /* LDR rD, [rA, #imm5*4] */
    kind = CGBA_FM_LOAD_W; off = ((op1 >> 6) & 31u) << 2; align = 3;
  } else {
    return 0;
  }
  if (((op1 >> 3) & 7u) != rA) return 0;
  rD = op1 & 7u;

  region = base >> 24;
  if (region == 3u) {
    base_off = base - 0x03000000u;
    if (base_off >= 0x8000u || base_off + off >= 0x8000u) return 0;
    limit = 0x8000u - (base_off + off);
    host_base = (u32)(uintptr_t)(iwram + 0x8000u + base_off + off);
    debit = 1;
  } else if (region == 2u) {
    base_off = base - 0x02000000u;
    if (base_off >= 0x40000u || base_off + off >= 0x40000u) return 0;
    limit = 0x40000u - (base_off + off);
    host_base = (u32)(uintptr_t)(ewram + base_off + off);
    debit = (kind == CGBA_FM_LOAD_W) ? 6 : 3;
  } else {
    return 0;
  }
  if (align && ((base + off) & (u32)align)) return 0;

  sh4g_litseg_reserve_bytes(tp, 4, 112u);

  /* Load idx -> R0, verify (u32)idx < limit (no wrap, in-region) and align. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rIdx, SH4_REG_RET);      /* R0 = reg[rIdx] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, limit, SH4_REG_T1);                 /* R2 = limit */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);    /* T = (R0 >= limit) */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  if (align) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, align);                    /* T = aligned */
    sh4g_close(tp, &cg);
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Fast path: optional reg[rA] commit (skipped when rD == rA), direct load. */
  if (rD != rA) {
    sh4g_const(tp, base, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rA);
      sh4g_close(tp, &cg); }
  }
  sh4g_const(tp, host_base, SH4_REG_T2);             /* R3 = host_base */
  { sh4_codegen cg = sh4g_open(tp);
    unsigned dst = SH4_REG_T0;
    int pin = sh4_pinned_reg(rD);
    if (pin >= 0 && kind != CGBA_FM_LOAD_W)
      dst = (unsigned)pin;
    sh4g_emit_host_load_r0(&cg, kind, SH4_REG_T2, dst);
    if (dst == SH4_REG_T0)
      sh4_emit_store_greg(&cg, SH4_REG_T0, rD);
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, debit);

  /* Cold fallback: R0 still holds original reg[rIdx]. */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  sh4g_const(tp, base, SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T0);  /* R1 = base + idx */
    sh4_emit_store_greg(&cg, SH4_REG_T0, rA);
    if (off)
      sh4_emit_add_imm(&cg, (int)off, SH4_REG_T0);   /* R1 = addr */
    sh4g_close(tp, &cg); }
  sh4g_fastmem_site(tp, (int)kind,
                    (const void *)cgba_sh4_thumb_ldst, (u32)op1, (u32)(pc + 2),
                    cc2, rD, -1);

  sh4g_patch_bra(bra_done, *tp);
  if (out_rA) *out_rA = rA;
  if (out_rD) *out_rD = rD;
  return 1;
}

/* Fuse a run of 2..6 consecutive Thumb `(STRH rD, [rB, #0]; ADD rB, #even_step)`
 * pairs (dead ADD flags, rD != rB, max store offset <= 30, total step <= 127).
 * Validates canonical aligned IWRAM rB + straddle + halfword SMC tags ONCE,
 * writes all halfwords inline (reusing swapped R0 when rD repeats), advances
 * rB once, and debits npairs IWRAM cycles once. */
static inline int sh4g_thumb_strh_stride_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int npairs, u32 pc)
{
  unsigned rB, rd[6];
  u32 step[6], off[6], total_step = 0u;
  int p;
  u8 *miss[3], *bra_done;
  int nmiss = 0;

  if (npairs < 2 || npairs > 6) return 0;
  if ((ops[0] & 0xFFC0u) != 0x8000u) return 0;
  rB = (ops[0] >> 3) & 7u;
  for (p = 0; p < npairs; p++) {
    u32 op_s = ops[p * 2];
    u32 op_a = ops[p * 2 + 1];
    if ((op_s & 0xFFC0u) != 0x8000u || ((op_s >> 3) & 7u) != rB) return 0;
    rd[p] = op_s & 7u;
    if (rd[p] == rB) return 0;
    if ((op_a & 0xFF00u) != (0x3000u | (rB << 8))) return 0;
    step[p] = op_a & 0xFFu;
    if (step[p] == 0u || (step[p] & 1u) != 0u) return 0;
    off[p] = total_step;
    if (off[p] > 30u) return 0;
    total_step += step[p];
    if (total_step > 127u) return 0;
  }

  sh4g_litseg_reserve_bytes(tp, 6, (u32)(112 + npairs * 16));

  /* Guard 1: rB in canonical aligned IWRAM with zero 32 KiB wrap. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rB, SH4_REG_T0);          /* R1 = reg[rB] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);     /* R0 = rB - 0x03000000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x8000u - (off[npairs - 1] + 2u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, 1);                         /* halfword aligned? */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG1); /* R5 = iw_off */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

  /* Guard 2: fast-bypass via cgba_iwram_code_end, else check halfword tags. */
  { u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                      SH4_REG_ARG1, SH4_REG_T2, SH4_REG_ARG0);

    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);  /* R3 = iwram + iw_off */
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0); /* off[0] == 0 */
      for (p = 1; p < npairs; p++) {
        sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, off[p] >> 1);
        sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_ARG0);
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all tags clear */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_ok, *tp); }

  /* Hot body: write all npairs halfwords inline, commit rB += total_step. */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);  /* R3 = iwram_data + off */
    for (p = 0; p < npairs; p++) {
      if (p == 0 || rd[p] != rd[p - 1]) {
        sh4_emit_load_greg(&cg, rd[p], SH4_REG_RET);
        sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
      }
      if (off[p] == 0u) sh4_emit_mov_w_store(&cg, SH4_REG_RET, SH4_REG_T2);
      else              sh4_emit_mov_w_store_disp(&cg, SH4_REG_T2, off[p] >> 1);
    }
    sh4_emit_add_imm(&cg, (int)total_step, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rB);
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, npairs);

  /* Cold fallback: exact per-pair emission (ADD flags were proven dead). */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  for (p = 0; p < npairs; p++) {
    sh4_greg_forward_reset();
    sh4g_thumb_ldst_native(tp, ops[p * 2], pc + (u32)(p * 4), ccs[p * 2]);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rB, SH4_REG_T0);
      sh4_emit_add_imm(&cg, (int)step[p], SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rB);
      sh4g_close(tp, &cg); }
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

/* Fuse a run of 2..8 consecutive Thumb same-base immediate-offset loads/stores
 * (`0x6000..0x8FFF`: LDR/STR, LDRB/STRB, LDRH/STRH [rB, #imm]).
 * Validates canonical aligned IWRAM rB + range + SMC tags ONCE (with fast
 * cgba_iwram_code_end bypass), eliminates dead IWRAM loads overwritten later in
 * the same run, runs all accesses inline (zero JSR overhead), and debits cnt
 * IWRAM cycles once. On miss, emits compact8+OOL fastmem sites so EWRAM/VRAM/IO
 * stay on native fastmem instead of C interp. */
static inline int sh4g_thumb_base_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  unsigned rB;
  u8 is_load[8], rd[8], sz[8], off[8], dead_load[8];
  u32 max_end = 0u, req_align = 0u;
  int has_store = 0, k, j;
  u8 *miss[3], *bra_done;
  int nmiss = 0;

  if (cnt < 2 || cnt > 8) return 0;
  if (ops[0] < 0x6000u || ops[0] >= 0x9000u) return 0;
  rB = (ops[0] >> 3) & 7u;

  for (k = 0; k < cnt; k++) {
    u32 op = ops[k];
    u32 hi = (op >> 8) & 0xFFu;
    u32 o, s, al;
    if (op < 0x6000u || op >= 0x9000u) return 0;
    if (((op >> 3) & 7u) != rB) return 0;
    is_load[k] = (u8)((op >> 11) & 1u);
    rd[k] = (u8)(op & 7u);
    if (k < cnt - 1 && is_load[k] && rd[k] == rB) return 0;
    if (hi < 0x70u) {
      s = 4u; al = 3u; o = ((op >> 6) & 31u) << 2;
    } else if (hi < 0x80u) {
      s = 1u; al = 0u; o = (op >> 6) & 31u;
    } else {
      s = 2u; al = 1u; o = ((op >> 6) & 31u) << 1;
    }
    sz[k] = (u8)s;
    off[k] = (u8)o;
    dead_load[k] = 0;
    if (o + s > max_end) max_end = o + s;
    req_align |= al;
    if (!is_load[k]) has_store = 1;
  }

  /* Pure IWRAM dead-load elimination: if load k writes rd[k] and a later load j
   * overwrites rd[k] before any intervening store reads rd[k], skip load k on
   * the IWRAM fast path while still debiting its cycle in `add #-cnt, r13`. */
  for (k = 0; k < cnt; k++) {
    if (!is_load[k] || rd[k] == rB) continue;
    for (j = k + 1; j < cnt; j++) {
      if (!is_load[j] && rd[j] == rd[k]) break;
      if (is_load[j] && rd[j] == rd[k]) {
        dead_load[k] = 1;
        break;
      }
    }
  }

  sh4g_litseg_reserve_bytes(tp, 6, (u32)(96 + cnt * 20));

  /* Guard 1: rB in canonical IWRAM [0x03000000, 0x03008000 - max_end] & aligned. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rB, SH4_REG_T0);            /* R1 = reg[rB] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_RET);
  sh4g_const(tp, 0x8000u - (max_end - 1u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_T0);         /* R1 = rB - 0x03000000 */
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);        /* T = out of bounds */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  if (req_align) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, (int)req_align);              /* T = aligned */
    sh4g_close(tp, &cg);
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Guard 2: if any store, fast-bypass via cgba_iwram_code_end, else check tags. */
  if (has_store) {
    u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                      SH4_REG_T0, SH4_REG_T2, SH4_REG_T1);
    int first = 1;

    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      u32 seen_wmask = 0u;
      sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);    /* R3 = iwram + off */
      for (k = 0; k < cnt; k++) {
        unsigned o = off[k], s = sz[k];
        unsigned dst_r;
        if (is_load[k]) continue;
        if (s == 4u && o <= 60u) {
          if (seen_wmask & (1u << (o >> 2))) continue;
          seen_wmask |= (1u << (o >> 2));
        }
        dst_r = first ? SH4_REG_ARG0 : SH4_REG_T1;
        if (s == 4u) {
          if (o == 0u)      sh4_emit_mov_l_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 60u) sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, dst_r, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, dst_r);
          }
        } else if (s == 2u) {
          if (o == 0u) sh4_emit_mov_w_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 30u) {
            sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, o >> 1);
            sh4_emit_mov_reg(&cg, SH4_REG_RET, dst_r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, dst_r);
          }
          sh4_emit_extu_w(&cg, dst_r, dst_r);
        } else {
          if (o == 0u) sh4_emit_mov_b_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 15u) {
            sh4_emit_mov_b_load_disp(&cg, SH4_REG_T2, o);
            sh4_emit_mov_reg(&cg, SH4_REG_RET, dst_r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, dst_r);
          }
          sh4_emit_extu_b(&cg, dst_r, dst_r);
        }
        if (first) first = 0;
        else       sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all tags clear */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_ok, *tp);
  }

  /* Hot body: R3 = iwram_data + off, zero guards, zero JSR/JMP. */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);      /* R3 = iwram_data + off */
    for (k = 0; k < cnt; k++) {
      unsigned o = off[k], s = sz[k], r = rd[k];
      if (dead_load[k]) continue;
      if (is_load[k]) {
        if (s == 4u) {
          if (o == 0u)      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
          else if (o <= 60u) sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
          }
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_store_greg(&cg, SH4_REG_T1, r);
        } else if (s == 2u) {
          if (o == 0u) {
            sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          } else if (o <= 30u) {
            sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, o >> 1);
            sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_extu_w(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_store_greg(&cg, SH4_REG_RET, r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          }
        } else {
          if (o == 0u) {
            sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          } else if (o <= 15u) {
            sh4_emit_mov_b_load_disp(&cg, SH4_REG_T2, o);
            sh4_emit_extu_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_store_greg(&cg, SH4_REG_RET, r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          }
        }
      } else {
        if (s == 4u) {
          sh4_emit_load_greg(&cg, r, SH4_REG_T1);
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          if (o == 0u)      sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
          else if (o <= 60u) sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        } else if (s == 2u) {
          if (o == 0u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
          } else if (o <= 30u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_RET);
            sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_mov_w_store_disp(&cg, SH4_REG_T2, o >> 1);
          } else {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        } else {
          if (o == 0u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_mov_b_store(&cg, SH4_REG_T1, SH4_REG_T2);
          } else if (o <= 15u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_RET);
            sh4_emit_mov_b_store_disp(&cg, SH4_REG_T2, o);
          } else {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        }
      }
    }
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, cnt);

  /* Cold fallback: emit resident fastmem sites directly (8-10B each under
   * compact8+OOL, keeping EWRAM/VRAM/IO on native fastmem instead of C interp). */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  for (k = 0; k < cnt; k++) {
    sh4_greg_forward_reset();
    sh4g_thumb_ldst_native(tp, ops[k], pc + (u32)(k * 2), ccs[k]);
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}
#endif


#endif /* CGBA_SH4_THUMB_LDST_EMIT_H */
