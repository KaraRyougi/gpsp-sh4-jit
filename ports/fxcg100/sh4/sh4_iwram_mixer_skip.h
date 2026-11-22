/* Soundless audio-mixer skipping for non-MP2K IWRAM stereo PCM mixers
 * (e.g. Metroid Fusion / Intelligent Systems / Camelot fixed-rate and
 * linearly-interpolated stereo mix loops). Included via sh4_arm_block_emit.h. */
#ifndef CGBA_SH4_IWRAM_MIXER_SKIP_H
#define CGBA_SH4_IWRAM_MIXER_SKIP_H

#if defined(CGBA_SH4_ARM_MUL_EMIT_H) && defined(CGBA_SH4_ARM_LDST_NATIVE) && \
    defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
/* Since the calculator port has no sound backend, the IWRAM mix-buffer
 * RMW-MLA work and ROM sample fetches are discarded by audio DMA.  When sample
 * addresses stay in ROM WS0 (0x08..0x09, constant waitstate ws_cyc_nseq[8][0])
 * and the mix buffer stays inside non-wrapping IWRAM (0x03), we advance only
 * the live loop pointers/phase and debit the exact cycle count — zero ROM
 * reads, zero IWRAM writes, zero SMC tag checks.  Any guard miss falls cleanly
 * to exact instruction-by-instruction native emission. */

static inline void sh4g_emit_ws0_rom_check(u8 **tp, unsigned addr_reg,
                                           u8 **miss_out, int *nmiss)
{
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, addr_reg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_add_imm(&cg, -8, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, 2, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET); /* T = region not 8 or 9 */
    sh4g_close(tp, &cg); }
  miss_out[(*nmiss)++] = sh4g_emit_bt_placeholder(tp);
}

static inline void sh4g_emit_iwram_span_check(u8 **tp, unsigned base_greg,
                                              int bytes_minus_1,
                                              u8 **miss_out, int *nmiss)
{
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, base_greg, SH4_REG_T0);
    sh4_emit_shlr2(&cg, SH4_REG_T0);
    sh4_emit_shll2(&cg, SH4_REG_T0);                  /* R1 = base & ~3 */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 3);                       /* T = IWRAM */
    sh4g_close(tp, &cg); }
  miss_out[(*nmiss)++] = sh4g_emit_bf_placeholder(tp);

  sh4g_const(tp, 0x7FFFu, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = aligned off */
    sh4_emit_add_imm(&cg, bytes_minus_1, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET);
    sh4_emit_cmppz(&cg, SH4_REG_RET);                 /* T = no 32 KiB wrap */
    sh4g_close(tp, &cg); }
  miss_out[(*nmiss)++] = sh4g_emit_bf_placeholder(tp);
}

SH4G_NOINLINE void sh4g_emit_mixer_fallback_ops(u8 **tp, const u32 *ops,
                                                const int *ccs, int count,
                                                u32 pc)
{
  int k;
  for (k = 0; k < count; k++) {
    u32 op = ops[k];
    u32 p = pc + (u32)(k * 4);
    sh4_greg_forward_reset();
    if ((op & 0x0E000000u) == 0x08000000u) {
      /* Compact OOL fastmem LDMIA/STMIA{!} (keeps cold fallback well within BRA range). */
      u32 rn = (op >> 16) & 0xFu, rlist = op & 0xFFFFu;
      u32 is_load = (op >> 20) & 1u, wb = (op >> 21) & 1u, nreg = 0u, i;
      for (i = 0; i < 16u; i++)
        if (rlist & (1u << i)) nreg++;
      sh4g_arm_block_emit_addr(tp, rn, (int)wb, 0, (int)(nreg * 4u));
      sh4g_fastmem_site_raw(tp, (is_load ? CGBA_FMB_LDM : CGBA_FMB_STM) +
                            (wb ? 2 : 0),
                            (const void *)cgba_sh4_arm_block, op, p, ccs[k],
                            sh4g_fastmem_block_params(rlist, nreg,
                                                      wb ? (int)rn : -1));
    } else if ((op & 0x0FC000F0u) == 0x00000090u) {
      sh4g_arm_multiply_native(tp, op, 0);
    } else if ((op & 0x0E000090u) == 0x00000090u) {
      /* Compact OOL fastmem pre-indexed +up LDRSB (imm or reg, optional wb). */
      u32 rn = (op >> 16) & 0xFu, rd = (op >> 12) & 0xFu;
      u32 wb = (op >> 21) & 1u;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        if (op & 0x00400000u) {
          u32 imm = ((op >> 4) & 0xF0u) | (op & 0xFu);
          sh4_emit_add_imm(&cg, (int)imm, SH4_REG_T0);
        } else {
          sh4_emit_load_greg(&cg, op & 0xFu, SH4_REG_T1);
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
        }
        if (wb)
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4g_close(tp, &cg); }
      sh4g_fastmem_site(tp, CGBA_FM_LOAD_SB + (wb ? CGBA_FM_WB : 0),
                        (const void *)cgba_sh4_arm_ldst, op, p, ccs[k],
                        rd, wb ? (int)rn : -1);
    } else {
      sh4g_arm_dp_native(tp, op, p, 0);
    }
    sh4g_litseg_maybe_flush(tp);
  }
}

/* Soundless skip for 1..2x fixed-rate IWRAM stereo PCM mix units:
 *   LDMIA rn, {rA,rB,rC,rD}; LDRSB rTmp,[rS,rI]; MLA; MLA; ADD rI,rI,#1;
 *   LDRSB rTmp,[rS,rI]; MLA; MLA; ADD rI,rI,#1; STMIA rn!, {rA,rB,rC,rD}. */
SH4G_NOINLINE int sh4g_arm_rmw_block4_ldrsb_mla_skip(u8 **tp, const u32 *ops,
  const int *ccs, int reps, u32 pc)
{
  u32 rn, rlist, nreg = 0, i;
  u32 rS, rI, rTmp;
  u8 *gmiss[4], *bra_done;
  int ng = 0;

  if ((pc >> 24) != 0x03 || reps < 1 || reps > 2) return 0;
  if (reps == 2) {
    for (i = 0; i < 10u; i++) {
      if (ops[10 + i] != ops[i]) return 0;
    }
  }
  if ((ops[0] & 0xFFF00000u) != 0xE8900000u) return 0; /* LDMIA rn, rlist */
  rn = (ops[0] >> 16) & 0xFu;
  rlist = ops[0] & 0xFFFFu;
  if (rn == 15u || (rlist & 0x8000u) || (rlist & (1u << rn))) return 0;
  for (i = 0; i < 15u; i++) {
    if (rlist & (1u << i)) nreg++;
  }
  if (nreg != 4u) return 0;

  /* op[1] and op[5]: identical signed LDRSB rTmp, [rS, rI] */
  if (ops[5] != ops[1] || (ops[1] & 0xFFF00FF0u) != 0xE19000D0u) return 0;
  rS   = (ops[1] >> 16) & 0xFu;
  rTmp = (ops[1] >> 12) & 0xFu;
  rI   = ops[1] & 0xFu;
  if (rS == 15u || rI == 15u || rTmp == 15u) return 0;
  if (rTmp == rn || rTmp == rS || rTmp == rI || (rlist & (1u << rTmp))) return 0;
  if (rI == rn || (rlist & (1u << rI)) || (rlist & (1u << rS))) return 0;

  /* op[4] and op[8]: ADD rI, rI, #1 */
  if (ops[4] != ops[8] || (ops[4] & 0xFFF00FFFu) != 0xE2800001u) return 0;
  if (((ops[4] >> 16) & 0xFu) != rI || ((ops[4] >> 12) & 0xFu) != rI) return 0;

  /* op[9]: STMIA rn!, rlist (must writeback mix pointer) */
  if ((ops[9] & 0xFFF00000u) != 0xE8A00000u) return 0;
  if (((ops[9] >> 16) & 0xFu) != rn || (ops[9] & 0xFFFFu) != rlist) return 0;

  /* op[2], op[3], op[6], op[7]: 4 MLAs updating each of rlist once using rTmp */
  {
    static const int mla_idx[4] = { 2, 3, 6, 7 };
    u32 seen = 0u;
    for (i = 0; i < 4u; i++) {
      u32 mop = ops[mla_idx[i]];
      u32 rd, acc, rs, rm;
      if ((mop & 0xFFF000F0u) != 0xE0200090u) return 0;
      rd = (mop >> 16) & 0xFu; acc = (mop >> 12) & 0xFu;
      rs = (mop >> 8)  & 0xFu; rm  = mop & 0xFu;
      if (rd != acc || !(rlist & (1u << rd)) || (seen & (1u << rd))) return 0;
      if (rm == rTmp)      { /* rs is volume */ }
      else if (rs == rTmp) { rs = rm; }
      else return 0;
      if (rs == 15u || rs == rn || rs == rTmp || rs == rI || (rlist & (1u << rs)))
        return 0;
      seen |= (1u << rd);
    }
    if (seen != rlist) return 0;
  }

  sh4g_litseg_reserve_bytes(tp, 6, 160u);

  /* Guard 1: sample range [rS+rI .. rS+rI+reps*2-1] stays in ROM WS0 (0x08..0x09). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rS, SH4_REG_T0);
    sh4_emit_load_greg(&cg, rI, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);    /* R1 = addr0 */
    sh4g_close(tp, &cg); }
  sh4g_emit_ws0_rom_check(tp, SH4_REG_T0, gmiss, &ng);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, reps * 2 - 1, SH4_REG_T0);  /* R1 = addr_last */
    sh4g_close(tp, &cg); }
  sh4g_emit_ws0_rom_check(tp, SH4_REG_T0, gmiss, &ng);

  /* Guard 2: mix destination stays in non-wrapping IWRAM (no SMC check needed). */
  sh4g_emit_iwram_span_check(tp, rn, reps * 16 - 1, gmiss, &ng);

  /* Soundless fast commit: advance sample index rI, mix pointer rn, and cycles. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
    sh4_emit_add_imm(&cg, reps * 2, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rI);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4_emit_add_imm(&cg, reps * 16, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rn);
    sh4g_close(tp, &cg); }
  sh4g_cycle_debit(tp, reps * (8 + 2 * (int)ws_cyc_nseq[8][0]));
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* Exact fallback on any guard miss. */
  while (ng)
    sh4g_patch_cond(gmiss[--ng], *tp);
  sh4g_emit_mixer_fallback_ops(tp, ops, ccs, reps * 10, pc);
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

/* Soundless skip for 1..4x stepped linearly-interpolated IWRAM stereo PCM mix:
 *   MOV rI, rP, LSR #sh; LDRSB rS, [rI, +rB]!; LDRSB rT, [rI, #+1];
 *   SUB rT, rT, rS; AND rI, rP, rM; MUL rT, rI, rT; ADD rS, rS, rT, LSR/ASR #sh2;
 *   LDMIA rD, {rT, rI}; MLA rA, rS, rV1, rA; MLA rB_w, rS, rV2, rB_w;
 *   STMIA rD!, {rT, rI}; ADD rP, rP, rStep. */
SH4G_NOINLINE int sh4g_arm_interp_rmw_mla_skip(u8 **tp, const u32 *ops,
  const int *ccs, int count, u32 pc,
  const u8 *unit_has_gate, const int *unit_pre_debit,
  u8 **unit_blk_off, u8 **unit_body_off)
{
  u32 rI, rP, rS, rT, rB, rM, rD, rA, rB_w, rV1 = 0, rV2 = 0, rStep = 0;
  u32 sh, sh2, sh2_type, rlist, mod_mask, ro_mask;
  int u, j, hot_base_debit = 0, hot_total_debit, any_mid_gate = 0;
  u8 *gmiss[11], *bra_done;
  int ng = 0;

  if ((pc >> 24) != 0x03 || count < 1 || count > 4)
    return 0;

  for (u = 1; u < count; u++) {
    for (j = 0; j < 12; j++) {
      if (ops[u * 12 + j] != ops[j])
        return 0;
    }
  }

  /* op[0]: MOV rI, rP, LSR #sh */
  if ((ops[0] & 0xFFFF0070u) != 0xE1A00020u) return 0;
  sh = (ops[0] >> 7) & 0x1Fu;
  if (sh == 0u) return 0;
  rI = (ops[0] >> 12) & 0xFu;
  rP = ops[0] & 0xFu;

  /* op[1]: LDRSB rS, [rI, +rB]! */
  if ((ops[1] & 0xFFF00FF0u) != 0xE1B000D0u || ((ops[1] >> 16) & 0xFu) != rI)
    return 0;
  rS = (ops[1] >> 12) & 0xFu;
  rB = ops[1] & 0xFu;

  /* op[2]: LDRSB rT, [rI, #+1] */
  if ((ops[2] & 0xFFF00FFFu) != 0xE1D000D1u || ((ops[2] >> 16) & 0xFu) != rI)
    return 0;
  rT = (ops[2] >> 12) & 0xFu;

  /* op[3]: SUB rT, rT, rS */
  if ((ops[3] & 0xFFF00FF0u) != 0xE0400000u ||
      ((ops[3] >> 12) & 0xFu) != rT || ((ops[3] >> 16) & 0xFu) != rT ||
      (ops[3] & 0xFu) != rS)
    return 0;

  /* op[4]: AND rI, rP, rM (either operand order) */
  { u32 a4 = (ops[4] >> 16) & 0xFu, b4 = ops[4] & 0xFu;
    if ((ops[4] & 0xFFF00FF0u) != 0xE0000000u || ((ops[4] >> 12) & 0xFu) != rI)
      return 0;
    if (a4 == rP)      rM = b4;
    else if (b4 == rP) rM = a4;
    else return 0; }

  /* op[5]: MUL rT, rI, rT (either operand order) */
  { u32 rm5 = ops[5] & 0xFu, rs5 = (ops[5] >> 8) & 0xFu;
    if ((ops[5] & 0xFFF0F0F0u) != 0xE0000090u || ((ops[5] >> 16) & 0xFu) != rT)
      return 0;
    if (!((rm5 == rI && rs5 == rT) || (rm5 == rT && rs5 == rI)))
      return 0; }

  /* op[6]: ADD rS, rS, rT, LSR/ASR #sh2 */
  sh2_type = (ops[6] >> 5) & 3u;
  sh2 = (ops[6] >> 7) & 0x1Fu;
  if ((ops[6] & 0xFFF00010u) != 0xE0800000u ||
      ((ops[6] >> 12) & 0xFu) != rS || ((ops[6] >> 16) & 0xFu) != rS ||
      (ops[6] & 0xFu) != rT || (sh2_type != 1u && sh2_type != 2u) || sh2 == 0u)
    return 0;

  /* op[7]: LDMIA rD, {rT, rI} */
  if ((ops[7] & 0xFFF00000u) != 0xE8900000u) return 0;
  rD = (ops[7] >> 16) & 0xFu;
  rlist = ops[7] & 0xFFFFu;
  if (rT == rI || rlist != ((1u << rT) | (1u << rI))) return 0;
  rA = (rT < rI) ? rT : rI;
  rB_w = (rT < rI) ? rI : rT;

  /* op[8..9]: MLA rA, rS, rV1, rA and MLA rB_w, rS, rV2, rB_w */
  { u32 seen = 0u;
    int m;
    for (m = 0; m < 2; m++) {
      u32 mo = ops[8 + m];
      u32 rd = (mo >> 16) & 0xFu, acc = (mo >> 12) & 0xFu;
      u32 rs = (mo >> 8) & 0xFu, rm = mo & 0xFu, rv;
      if ((mo & 0xFFF000F0u) != 0xE0200090u || rd != acc) return 0;
      if (rm == rS)      rv = rs;
      else if (rs == rS) rv = rm;
      else return 0;
      if (rd == rA)        { if (seen & 1u) return 0; seen |= 1u; rV1 = rv; }
      else if (rd == rB_w) { if (seen & 2u) return 0; seen |= 2u; rV2 = rv; }
      else return 0;
    }
    if (seen != 3u) return 0; }

  /* op[10]: STMIA rD!, {rT, rI} (must writeback mix pointer) */
  if ((ops[10] & 0xFFF00000u) != 0xE8A00000u ||
      ((ops[10] >> 16) & 0xFu) != rD || (ops[10] & 0xFFFFu) != rlist)
    return 0;

  /* op[11]: ADD rP, rP, rStep */
  { u32 a11 = (ops[11] >> 16) & 0xFu, b11 = ops[11] & 0xFu;
    if ((ops[11] & 0xFFF00FF0u) != 0xE0800000u || ((ops[11] >> 12) & 0xFu) != rP)
      return 0;
    if (a11 == rP)      rStep = b11;
    else if (b11 == rP) rStep = a11;
    else return 0; }

  if (rS >= 15u || rT >= 15u || rI >= 15u || rB >= 15u || rP >= 15u ||
      rM >= 15u || rD >= 15u || rV1 >= 15u || rV2 >= 15u || rStep >= 15u)
    return 0;
  if (rS == rT || rS == rI || rT == rI || rD == rP)
    return 0;
  mod_mask = (1u << rS) | (1u << rT) | (1u << rI);
  ro_mask = (1u << rB) | (1u << rM) | (1u << rV1) | (1u << rV2) | (1u << rStep);
  if ((ro_mask & mod_mask) != 0u || ((1u << rD) & mod_mask) != 0u ||
      ((1u << rP) & mod_mask) != 0u || ((ro_mask & (1u << rD)) != 0u) ||
      ((ro_mask & (1u << rP)) != 0u))
    return 0;

  for (u = 1; u < count; u++) {
    if (unit_has_gate && unit_has_gate[u]) {
      any_mid_gate = 1;
      hot_base_debit += unit_pre_debit[u];
    }
  }
  hot_total_debit = hot_base_debit + count * (2 * (int)ws_cyc_nseq[8][0] + 4);

  sh4g_litseg_reserve_bytes(tp, 6, 260u);

  /* Guard 0: cycle gate safety across intermediate unit boundaries. */
  if (any_mid_gate) {
    sh4g_const(tp, (u32)hot_total_debit, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpgt(&cg, SH4_REG_T1, SH4_REG_CYCLES); /* T = (R13 > total) */
      sh4g_close(tp, &cg); }
    gmiss[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Guard 1: verify every sample pair [rB + (rP_u >> sh) .. +1] lies in ROM WS0
   * while computing final R7 = rP + count * rStep as a side effect. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, -(int)sh, SH4_REG_ARG0);    /* R4 = -sh */
    sh4_emit_load_greg(&cg, rStep, SH4_REG_ARG1);     /* R5 = rStep */
    sh4_emit_load_greg(&cg, rB, SH4_REG_ARG2);        /* R6 = rB */
    sh4_emit_load_greg(&cg, rP, SH4_REG_ARG3);        /* R7 = rP */
    sh4g_close(tp, &cg); }
  for (u = 0; u < count; u++) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_T0);
      sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_T0);   /* R1 = rP_u >> sh */
      sh4_emit_add_reg(&cg, SH4_REG_ARG2, SH4_REG_T0);/* R1 = addr_u */
      sh4_emit_add_reg(&cg, SH4_REG_ARG1, SH4_REG_ARG3); /* R7 += rStep */
      sh4g_close(tp, &cg); }
    sh4g_emit_ws0_rom_check(tp, SH4_REG_T0, gmiss, &ng);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_imm(&cg, 1, SH4_REG_T0);           /* R1 = addr_u + 1 */
      sh4g_close(tp, &cg); }
    sh4g_emit_ws0_rom_check(tp, SH4_REG_T0, gmiss, &ng);
  }

  /* Guard 2: mix destination stays in non-wrapping IWRAM (no SMC check needed). */
  sh4g_emit_iwram_span_check(tp, rD, count * 8 - 1, gmiss, &ng);

  /* Soundless fast commit: advance phase rP (already in R7), mix pointer rD, cycles. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_store_greg(&cg, SH4_REG_ARG3, rP);
    sh4_emit_load_greg(&cg, rD, SH4_REG_T0);
    sh4_emit_add_imm(&cg, count * 8, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rD);
    sh4g_close(tp, &cg); }
  sh4g_cycle_debit(tp, hot_total_debit);
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* Cold fallback: exact instruction-by-instruction emission (preserves mid-gates). */
  while (ng)
    sh4g_patch_cond(gmiss[--ng], *tp);
  for (u = 0; u < count; u++) {
    int bi = u * 12;
    u32 pc_u = pc + (u32)(bi * 4);
    if (u > 0 && unit_has_gate && unit_has_gate[u]) {
      sh4g_cycle_sub(tp, unit_pre_debit[u], pc_u);
      if (unit_blk_off) unit_blk_off[u] = *tp;
      sh4g_cycle_gate(tp, pc_u, 1);
      if (unit_body_off) unit_body_off[u] = *tp;
    } else if (u > 0) {
      if (unit_blk_off) unit_blk_off[u] = *tp;
      if (unit_body_off) unit_body_off[u] = *tp;
    }
    sh4g_emit_mixer_fallback_ops(tp, ops + bi, ccs + bi, 12, pc_u);
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}
#endif

#endif /* CGBA_SH4_IWRAM_MIXER_SKIP_H */
