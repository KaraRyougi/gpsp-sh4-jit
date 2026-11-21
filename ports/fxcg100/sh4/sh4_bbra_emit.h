/* 4-slot intra-segment BBRA (basic-block register allocation) emitters.
 * Included via sh4_thumb_dp_emit.h. */
#ifndef CGBA_SH4_BBRA_EMIT_H
#define CGBA_SH4_BBRA_EMIT_H

/* ---- 4-slot intra-segment BBRA for straight-line dead-flag Thumb ALU runs ---
 * Allocates the top 4 hot unpinned guest registers (r2..r14, refs >= 2) into
 * SH-4 R4..R7 across a straight-line run of 2..16 dead-flag Thumb ALU ops,
 * keeping r0 in R11 and r1 in R12. Eliminates intermediate reg[] loads/stores
 * and SH7305 2-cycle Load-Use stalls; flushes only dirty slots at run end so
 * no dirty state ever escapes across branch/memory/helper boundaries. */
/* Fast inline prefilter: returns 1 for any opcode that sh4g_thumb_bbra_classify
 * could possibly accept, rejecting loads/stores/branches/PUSH/POP in 2 ops. */
static inline int sh4g_thumb_bbra_maybe(u32 op)
{
  unsigned hi = (op >> 8) & 0xFFu;
  return hi <= 0x46u ? (hi <= 0x27u || (hi >= 0x30u && hi != 0x45u))
                     : (hi >= 0xA8u && hi <= 0xAFu);
}

static inline int sh4g_thumb_bbra_classify(u32 op, u32 *rmask, u32 *wmask)
{
  unsigned hi = (op >> 8) & 0xFFu;
  *rmask = 0u;
  *wmask = 0u;
  if (hi <= 0x17u) {                          /* LSL/LSR/ASR Rd, Rm, #imm5 */
    unsigned rd = op & 7u, rm = (op >> 3) & 7u;
    unsigned sub = (op >> 11) & 3u, imm5 = (op >> 6) & 31u;
    if (!(sub == 1u && imm5 == 0u))           /* LSR #32 overwrites without reading rm */
      *rmask = 1u << rm;
    *wmask = 1u << rd;
    return 1;
  }
  if (hi <= 0x1Fu) {                          /* ADD/SUB Rd, Rn, Rm|#imm3 */
    unsigned rd = op & 7u, rn = (op >> 3) & 7u, arg = (op >> 6) & 7u;
    unsigned is_imm = (op >> 10) & 1u;
    *rmask = (1u << rn) | (is_imm ? 0u : (1u << arg));
    *wmask = 1u << rd;
    return 1;
  }
  if (hi <= 0x27u) {                          /* MOV Rd, #imm8 */
    *wmask = 1u << ((op >> 8) & 7u);
    return 1;
  }
  if (hi >= 0x30u && hi <= 0x3Fu) {           /* ADD/SUB Rd, #imm8 */
    unsigned rd = (op >> 8) & 7u;
    *rmask = 1u << rd;
    *wmask = 1u << rd;
    return 1;
  }
  if (hi >= 0x40u && hi <= 0x43u) {           /* ALU reg-reg */
    unsigned alu = (op >> 6) & 0xFu;
    unsigned rs = (op >> 3) & 7u, rd = op & 7u;
    switch (alu) {
    case 0x0u: case 0x1u: case 0xCu: case 0xDu: case 0xEu:
      *rmask = (1u << rd) | (1u << rs);
      *wmask = 1u << rd;
      return 1;
    case 0x9u: case 0xFu:
      *rmask = 1u << rs;
      *wmask = 1u << rd;
      return 1;
    default:
      return 0;
    }
  }
  if (hi == 0x44u || hi == 0x46u) {           /* ADD/MOV Hd, Hm (non-PC) */
    unsigned rd = (op & 7u) | ((op >> 4) & 8u);
    unsigned rs = (op >> 3) & 0xFu;
    if (rd == 15u || rs == 15u)
      return 0;
    *rmask = (hi == 0x44u ? (1u << rd) : 0u) | (1u << rs);
    *wmask = 1u << rd;
    return 1;
  }
  if (hi >= 0xA8u && hi <= 0xAFu) {           /* ADD Rd, SP, #imm8*4 */
    *rmask = 1u << SH4_GREG_SP;
    *wmask = 1u << ((op >> 8) & 7u);
    return 1;
  }
  return 0;
}

/* Shared 4-slot BBRA register selector (top 4 unpinned guest regs with refs >= 2). */
static inline int sh4g_bbra_pick_slots(const u32 *rm_in, const u32 *wm_in,
                                       int len, u32 unpinned_mask,
                                       s8 gslot[16], unsigned slot_guest[4],
                                       u32 *wmask_all_out)
{
  u8 refs[16];
  u8 writes[16];
  u32 seen_once = 0u, cand_mask = 0u, wmask_all = 0u;
  int i, s, nslots = 0;

  memset(refs, 0, sizeof refs);
  memset(writes, 0, sizeof writes);
  for (i = 0; i < len; i++) {
    u32 rm = rm_in[i], wm = wm_in[i], m;
    wmask_all |= wm;
    rm &= unpinned_mask;
    wm &= unpinned_mask;
    cand_mask |= (seen_once & (rm | wm)) | (rm & wm);
    seen_once |= rm | wm;
    for (m = rm; m; m &= m - 1u)
      refs[__builtin_ctz(m)]++;
    for (m = wm; m; m &= m - 1u) {
      unsigned g = (unsigned)__builtin_ctz(m);
      refs[g]++;
      writes[g]++;
    }
  }
  if (wmask_all_out)
    *wmask_all_out = wmask_all;
  if (!cand_mask)
    return 0;

  memset(gslot, -1, 16);
  for (s = 0; s < 4 && cand_mask; s++) {
    int best = -1;
    unsigned best_score = 1u;
    u32 m;
    for (m = cand_mask; m; m &= m - 1u) {
      int g = __builtin_ctz(m);
      if (refs[g] > best_score ||
          (refs[g] == best_score && best >= 0 && writes[g] > writes[best])) {
        best = g;
        best_score = refs[g];
      }
    }
    if (best < 0)
      break;
    cand_mask &= ~(1u << best);
    gslot[best] = (s8)nslots;
    slot_guest[nslots] = (unsigned)best;
    nslots++;
  }
  return nslots;
}

static inline void sh4g_thumb_bbra_const_step(u32 op, u32 *cmask, u32 cval[16])
{
  unsigned hi = (op >> 8) & 0xFFu;
  u32 m = *cmask;
  if (hi <= 0x17u) {                          /* LSL/LSR/ASR Rd, Rm, #imm5 */
    unsigned rd = op & 7u, rm = (op >> 3) & 7u;
    unsigned imm5 = (op >> 6) & 31u, sub = (op >> 11) & 3u;
    if (sub == 1u && imm5 == 0u) {
      cval[rd] = 0u;
      *cmask = m | (1u << rd);
    } else if (m & (1u << rm)) {
      u32 v = cval[rm];
      if (sub == 0u)      v = v << imm5;
      else if (sub == 1u) v = v >> imm5;
      else                v = imm5 ? (u32)((s32)v >> imm5)
                                   : ((v & 0x80000000u) ? 0xFFFFFFFFu : 0u);
      cval[rd] = v;
      *cmask = m | (1u << rd);
    } else {
      *cmask = m & ~(1u << rd);
    }
  } else if (hi <= 0x1Fu) {                   /* ADD/SUB Rd, Rn, Rm|#imm3 */
    unsigned rd = op & 7u, rn = (op >> 3) & 7u, arg = (op >> 6) & 7u;
    int is_sub = (op >> 9) & 1u, is_imm = (op >> 10) & 1u;
    if ((m & (1u << rn)) && (is_imm || (m & (1u << arg)))) {
      u32 rhs = is_imm ? arg : cval[arg];
      cval[rd] = is_sub ? (cval[rn] - rhs) : (cval[rn] + rhs);
      *cmask = m | (1u << rd);
    } else {
      *cmask = m & ~(1u << rd);
    }
  } else if (hi <= 0x27u) {                   /* MOV Rd, #imm8 */
    unsigned rd = (op >> 8) & 7u;
    cval[rd] = op & 0xFFu;
    *cmask = m | (1u << rd);
  } else if (hi >= 0x30u && hi <= 0x3Fu) {    /* ADD/SUB Rd, #imm8 */
    unsigned rd = (op >> 8) & 7u;
    u32 imm8 = op & 0xFFu;
    if (m & (1u << rd)) {
      cval[rd] = (hi >= 0x38u) ? (cval[rd] - imm8) : (cval[rd] + imm8);
      *cmask = m | (1u << rd);
    } else {
      *cmask = m & ~(1u << rd);
    }
  } else if (hi >= 0x40u && hi <= 0x43u) {    /* ALU reg-reg */
    unsigned alu = (op >> 6) & 0xFu, rs = (op >> 3) & 7u, rd = op & 7u;
    if ((m & (1u << rs)) && (alu == 0x9u || alu == 0xFu || (m & (1u << rd)))) {
      u32 a = cval[rd], b = cval[rs];
      switch (alu) {
      case 0x0u: cval[rd] = a & b; break;
      case 0x1u: cval[rd] = a ^ b; break;
      case 0x9u: cval[rd] = 0u - b; break;
      case 0xCu: cval[rd] = a | b; break;
      case 0xDu: cval[rd] = a * b; break;
      case 0xEu: cval[rd] = a & ~b; break;
      default:   cval[rd] = ~b; break;        /* 0xF MVN */
      }
      *cmask = m | (1u << rd);
    } else {
      *cmask = m & ~(1u << rd);
    }
  } else if (hi == 0x44u || hi == 0x46u) {    /* ADD/MOV Hd, Hm */
    unsigned rd = (op & 7u) | ((op >> 4) & 8u);
    unsigned rs = (op >> 3) & 0xFu;
    if (hi == 0x46u) {
      if (m & (1u << rs)) { cval[rd] = cval[rs]; *cmask = m | (1u << rd); }
      else                { *cmask = m & ~(1u << rd); }
    } else if ((m & (1u << rd)) && (m & (1u << rs))) {
      cval[rd] = cval[rd] + cval[rs];
      *cmask = m | (1u << rd);
    } else {
      *cmask = m & ~(1u << rd);
    }
  } else if (hi >= 0xA8u && hi <= 0xAFu) {    /* ADD Rd, SP, #imm8*4 */
    *cmask = m & ~(1u << ((op >> 8) & 7u));
  }
}

SH4G_NOINLINE int sh4g_thumb_alu_run_native_const(u8 **tp, const u16 *ops,
                                                  const u32 *rm_in, const u32 *wm_in,
                                                  int len, u32 *written_mask_out,
                                                  u32 *const_mask_io, u32 const_val_io[16])
{
  static const unsigned slot_reg[4] = {
    SH4_REG_ARG0, SH4_REG_ARG1, SH4_REG_ARG2, SH4_REG_ARG3
  };
  const u32 unpinned_mask = 0x7FFFu
#if CGBA_SH4_PIN_GUEST_R0
    & ~1u
#endif
#if CGBA_SH4_PIN_GUEST_R1
    & ~2u
#endif
    ;
  u32 rm_loc[16], wm_loc[16];
  s8 gslot[16];
  unsigned slot_guest[4];
  int nslots = 0, i, s;
  u32 slot_loaded = 0u, slot_dirty = 0u, wmask_all = 0u;
  u8 *start_tp;

  if (len < 2 || len > 16)
    return 0;
  if (!rm_in) {
    for (i = 0; i < len; i++) {
      if (!sh4g_thumb_bbra_classify(ops[i], &rm_loc[i], &wm_loc[i]))
        return 0;
    }
    rm_in = rm_loc;
    wm_in = wm_loc;
  }
  nslots = sh4g_bbra_pick_slots(rm_in, wm_in, len, unpinned_mask,
                                gslot, slot_guest, &wmask_all);
  if (nslots == 0)
    return 0;

  sh4_greg_forward_reset();
  start_tp = *tp;

#define BBRA_GET_SRC(cgp, g_, scratch_) ({                                     \
  unsigned _g = (g_), _r;                                                      \
  int _pin = sh4_pinned_reg(_g);                                               \
  if (_pin >= 0) {                                                             \
    _r = (unsigned)_pin;                                                       \
  } else if (gslot[_g] >= 0) {                                                 \
    unsigned _sl = (unsigned)gslot[_g];                                        \
    _r = slot_reg[_sl];                                                        \
    if (!(slot_loaded & (1u << _sl))) {                                        \
      sh4_emit_load_greg((cgp), _g, _r);                                       \
      slot_loaded |= (1u << _sl);                                              \
    }                                                                          \
  } else {                                                                     \
    sh4_emit_load_greg((cgp), _g, (scratch_));                                 \
    _r = (scratch_);                                                           \
  }                                                                            \
  _r;                                                                          \
})

#define BBRA_DST_REG(g_, scratch_) ({                                          \
  unsigned _g = (g_), _r;                                                      \
  int _pin = sh4_pinned_reg(_g);                                               \
  if (_pin >= 0) _r = (unsigned)_pin;                                          \
  else if (gslot[_g] >= 0) _r = slot_reg[gslot[_g]];                           \
  else _r = (scratch_);                                                        \
  _r;                                                                          \
})

#define BBRA_COMMIT_DST(cgp, g_, regw_) do {                                   \
  unsigned _g = (g_);                                                          \
  int _pin = sh4_pinned_reg(_g);                                               \
  if (_pin >= 0) {                                                             \
    if ((regw_) != (unsigned)_pin) sh4_emit_mov_reg((cgp), (regw_), (unsigned)_pin); \
  } else if (gslot[_g] >= 0) {                                                 \
    unsigned _sl = (unsigned)gslot[_g];                                        \
    if ((regw_) != slot_reg[_sl]) sh4_emit_mov_reg((cgp), (regw_), slot_reg[_sl]); \
    slot_loaded |= (1u << _sl);                                                \
    slot_dirty  |= (1u << _sl);                                                \
  } else {                                                                     \
    sh4_emit_store_greg((cgp), (regw_), _g);                                   \
  }                                                                            \
} while (0)

  for (i = 0; i < len; i++) {
    u32 op = ops[i];
    unsigned hi = (op >> 8) & 0xFFu;

    /* Fused zero/sign-extend inside the BBRA run:
     * LSL Rd, Rm, #16/#24 + LSR/ASR Rd, Rd, #16/#24 -> EXTU/EXTS directly. */
    if (i + 1 < len && ((op & 0xFFC0u) == 0x0400u || (op & 0xFFC0u) == 0x0600u)) {
      unsigned e_rd = op & 7u, e_rm = (op >> 3) & 7u;
      unsigned is_b = ((op & 0xFFC0u) == 0x0600u);
      u32 want_lsr = (is_b ? 0x0E00u : 0x0C00u) | (e_rd << 3) | e_rd;
      u32 want_asr = (is_b ? 0x1600u : 0x1400u) | (e_rd << 3) | e_rd;
      if (ops[i + 1] == want_lsr || ops[i + 1] == want_asr) {
        int is_s = (ops[i + 1] == want_asr);
        sh4_codegen cg = sh4g_open(tp);
        unsigned r_src = BBRA_GET_SRC(&cg, e_rm, SH4_REG_T0);
        unsigned r_dst = BBRA_DST_REG(e_rd, SH4_REG_T0);
        if (is_b) {
          if (is_s) sh4_emit_exts_b(&cg, r_src, r_dst);
          else      sh4_emit_extu_b(&cg, r_src, r_dst);
        } else {
          if (is_s) sh4_emit_exts_w(&cg, r_src, r_dst);
          else      sh4_emit_extu_w(&cg, r_src, r_dst);
        }
        BBRA_COMMIT_DST(&cg, e_rd, r_dst);
        sh4g_close(tp, &cg);
        i++;
        continue;
      }
    }

    if (hi <= 0x17u) {                        /* LSL/LSR/ASR Rd, Rm, #imm5 */
      unsigned rd = op & 7u, rm = (op >> 3) & 7u;
      unsigned imm5 = (op >> 6) & 31u, sub = (op >> 11) & 3u;
      sh4_codegen cg = sh4g_open(tp);
      unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
      if (sub == 1u && imm5 == 0u) {
        sh4_emit_mov_imm(&cg, 0, r_dst);      /* LSR #32 -> 0 */
      } else {
        unsigned r_src = BBRA_GET_SRC(&cg, rm, SH4_REG_T0);
        if (r_src != r_dst)
          sh4_emit_mov_reg(&cg, r_src, r_dst);
        if (sub == 0u) {
          switch (imm5) {
          case 0: break;
          case 1: sh4_emit_shll(&cg, r_dst); break;
          case 2: sh4_emit_shll2(&cg, r_dst); break;
          case 8: sh4_emit_shll8(&cg, r_dst); break;
          case 16: sh4_emit_shll16(&cg, r_dst); break;
          default:
            sh4_emit_mov_imm(&cg, (int)imm5, SH4_REG_RET);
            sh4_emit_shld(&cg, SH4_REG_RET, r_dst);
            break;
          }
        } else if (sub == 1u) {
          switch (imm5) {
          case 1: sh4_emit_shlr(&cg, r_dst); break;
          case 2: sh4_emit_shlr2(&cg, r_dst); break;
          case 8: sh4_emit_shlr8(&cg, r_dst); break;
          case 16: sh4_emit_shlr16(&cg, r_dst); break;
          default:
            sh4_emit_mov_imm(&cg, -(int)imm5, SH4_REG_RET);
            sh4_emit_shld(&cg, SH4_REG_RET, r_dst);
            break;
          }
        } else {                              /* ASR */
          if (imm5 == 0u) {
            sh4_emit_mov_imm(&cg, -31, SH4_REG_RET);
            sh4_emit_shad(&cg, SH4_REG_RET, r_dst);
            sh4_emit_shar(&cg, r_dst);
          } else if (imm5 == 1u) {
            sh4_emit_shar(&cg, r_dst);
          } else {
            sh4_emit_mov_imm(&cg, -(int)imm5, SH4_REG_RET);
            sh4_emit_shad(&cg, SH4_REG_RET, r_dst);
          }
        }
      }
      BBRA_COMMIT_DST(&cg, rd, r_dst);
      sh4g_close(tp, &cg);
    } else if (hi <= 0x1Fu) {                 /* ADD/SUB Rd, Rn, Rm|#imm3 */
      unsigned rd = op & 7u, rn = (op >> 3) & 7u, arg = (op >> 6) & 7u;
      unsigned is_sub = (op >> 9) & 1u, is_imm = (op >> 10) & 1u;
      sh4_codegen cg = sh4g_open(tp);
      unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
      if (is_imm) {
        unsigned r_src = BBRA_GET_SRC(&cg, rn, SH4_REG_T0);
        int simm = is_sub ? -(int)arg : (int)arg;
        if (r_src != r_dst)
          sh4_emit_mov_reg(&cg, r_src, r_dst);
        if (simm != 0)
          sh4_emit_add_imm(&cg, simm, r_dst);
        BBRA_COMMIT_DST(&cg, rd, r_dst);
      } else {
        unsigned r_n = BBRA_GET_SRC(&cg, rn, SH4_REG_T0);
        unsigned r_m = (arg == rn) ? r_n : BBRA_GET_SRC(&cg, arg, SH4_REG_T1);
        if (is_sub && rn == arg) {
          sh4_emit_mov_imm(&cg, 0, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        } else if (r_dst == r_n) {
          if (is_sub) sh4_emit_sub(&cg, r_m, r_dst);
          else        sh4_emit_add_reg(&cg, r_m, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        } else if (!is_sub && r_dst == r_m) {
          sh4_emit_add_reg(&cg, r_n, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        } else if (r_dst != r_m) {
          sh4_emit_mov_reg(&cg, r_n, r_dst);
          if (is_sub) sh4_emit_sub(&cg, r_m, r_dst);
          else        sh4_emit_add_reg(&cg, r_m, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        } else {                              /* is_sub && r_dst == r_m */
          sh4_emit_mov_reg(&cg, r_n, SH4_REG_T2);
          sh4_emit_sub(&cg, r_m, SH4_REG_T2);
          BBRA_COMMIT_DST(&cg, rd, SH4_REG_T2);
        }
      }
      sh4g_close(tp, &cg);
    } else if (hi <= 0x27u) {                 /* MOV Rd, #imm8 */
      unsigned rd = (op >> 8) & 7u;
      unsigned imm8 = op & 0xFFu;
      unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
      sh4g_const(tp, imm8, r_dst);
      { sh4_codegen cg = sh4g_open(tp);
        BBRA_COMMIT_DST(&cg, rd, r_dst);
        sh4g_close(tp, &cg); }
    } else if (hi >= 0x30u && hi <= 0x3Fu) {  /* ADD/SUB Rd, #imm8 */
      unsigned rd = (op >> 8) & 7u;
      unsigned is_sub = (op >> 11) & 1u;
      unsigned imm8 = op & 0xFFu;
      if (imm8 != 0u) {
        sh4_codegen cg = sh4g_open(tp);
        unsigned r_d = BBRA_GET_SRC(&cg, rd, SH4_REG_T0);
        if (!is_sub) {
          if (imm8 <= 127u) {
            sh4_emit_add_imm(&cg, (int)imm8, r_d);
          } else if (imm8 <= 254u) {
            sh4_emit_add_imm(&cg, 127, r_d);
            sh4_emit_add_imm(&cg, (int)(imm8 - 127u), r_d);
          } else {
            sh4_emit_add_imm(&cg, 127, r_d);
            sh4_emit_add_imm(&cg, 127, r_d);
            sh4_emit_add_imm(&cg, 1, r_d);
          }
        } else {
          if (imm8 <= 128u) {
            sh4_emit_add_imm(&cg, -(int)imm8, r_d);
          } else {
            sh4_emit_add_imm(&cg, -128, r_d);
            sh4_emit_add_imm(&cg, -(int)(imm8 - 128u), r_d);
          }
        }
        BBRA_COMMIT_DST(&cg, rd, r_d);
        sh4g_close(tp, &cg);
      }
    } else if (hi >= 0x40u && hi <= 0x43u) {  /* ALU reg-reg */
      unsigned alu = (op >> 6) & 0xFu;
      unsigned rs = (op >> 3) & 7u, rd = op & 7u;
      sh4_codegen cg = sh4g_open(tp);
      if (alu == 0x9u || alu == 0xFu) {       /* NEG / MVN */
        unsigned r_s = BBRA_GET_SRC(&cg, rs, SH4_REG_T1);
        unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
        if (alu == 0x9u) sh4_emit_neg(&cg, r_s, r_dst);
        else             sh4_emit_not(&cg, r_s, r_dst);
        BBRA_COMMIT_DST(&cg, rd, r_dst);
      } else {
        unsigned r_d = BBRA_GET_SRC(&cg, rd, SH4_REG_T0);
        unsigned r_s = (rs == rd) ? r_d : BBRA_GET_SRC(&cg, rs, SH4_REG_T1);
        switch (alu) {
        case 0x0u: sh4_emit_and(&cg, r_s, r_d); break;
        case 0x1u: sh4_emit_xor(&cg, r_s, r_d); break;
        case 0xCu: sh4_emit_or(&cg, r_s, r_d); break;
        case 0xDu:
          sh4_emit_mul_l(&cg, r_s, r_d);
          sh4_emit_sts_macl(&cg, r_d);
          break;
        case 0xEu:
          sh4_emit_not(&cg, r_s, SH4_REG_RET);
          sh4_emit_and(&cg, SH4_REG_RET, r_d);
          break;
        }
        BBRA_COMMIT_DST(&cg, rd, r_d);
      }
      sh4g_close(tp, &cg);
    } else if (hi == 0x44u || hi == 0x46u) {  /* ADD/MOV Hd, Hm */
      unsigned rd = (op & 7u) | ((op >> 4) & 8u);
      unsigned rs = (op >> 3) & 0xFu;
      sh4_codegen cg = sh4g_open(tp);
      if (hi == 0x46u) {                      /* MOV */
        if (rd != rs) {
          unsigned r_s = BBRA_GET_SRC(&cg, rs, SH4_REG_T1);
          unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
          if (r_s != r_dst)
            sh4_emit_mov_reg(&cg, r_s, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        }
      } else {                                /* ADD */
        unsigned r_d = BBRA_GET_SRC(&cg, rd, SH4_REG_T0);
        unsigned r_s = (rs == rd) ? r_d : BBRA_GET_SRC(&cg, rs, SH4_REG_T1);
        sh4_emit_add_reg(&cg, r_s, r_d);
        BBRA_COMMIT_DST(&cg, rd, r_d);
      }
      sh4g_close(tp, &cg);
    } else {                                  /* ADD Rd, SP, #imm8*4 */
      unsigned rd = (op >> 8) & 7u;
      u32 imm = (op & 0xFFu) << 2;
      unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
      if (imm <= 127u) {
        sh4_codegen cg = sh4g_open(tp);
        unsigned r_sp = BBRA_GET_SRC(&cg, SH4_GREG_SP, SH4_REG_T0);
        if (r_sp != r_dst)
          sh4_emit_mov_reg(&cg, r_sp, r_dst);
        if (imm != 0u)
          sh4_emit_add_imm(&cg, (int)imm, r_dst);
        BBRA_COMMIT_DST(&cg, rd, r_dst);
        sh4g_close(tp, &cg);
      } else {
        sh4g_const(tp, imm, SH4_REG_T1);
        { sh4_codegen cg = sh4g_open(tp);
          unsigned r_sp = BBRA_GET_SRC(&cg, SH4_GREG_SP, SH4_REG_T0);
          if (r_sp != r_dst)
            sh4_emit_mov_reg(&cg, r_sp, r_dst);
          sh4_emit_add_reg(&cg, SH4_REG_T1, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
          sh4g_close(tp, &cg); }
      }
    }
  }

  /* Flush dirty cached slots back to reg[]. */
  if (slot_dirty) {
    sh4_codegen cg = sh4g_open(tp);
    for (s = 0; s < nslots; s++) {
      if (slot_dirty & (1u << s))
        sh4_emit_store_greg(&cg, slot_reg[s], slot_guest[s]);
    }
    sh4g_close(tp, &cg);
  }
  sh4_greg_forward_finish(start_tp, *tp, 1);

  if (const_mask_io && const_val_io) {
    for (i = 0; i < len; i++)
      sh4g_thumb_bbra_const_step(ops[i], const_mask_io, const_val_io);
  }
  if (written_mask_out)
    *written_mask_out = wmask_all;
  return 1;
}

static inline int sh4g_thumb_alu_run_native_ex(u8 **tp, const u16 *ops,
                                               const u32 *rm_in, const u32 *wm_in,
                                               int len, u32 *written_mask_out)
{
  return sh4g_thumb_alu_run_native_const(tp, ops, rm_in, wm_in, len,
                                         written_mask_out, NULL, NULL);
}

static inline int sh4g_thumb_alu_run_native(u8 **tp, const u16 *ops, int len,
                                            u32 *written_mask_out)
{
  return sh4g_thumb_alu_run_native_const(tp, ops, NULL, NULL, len,
                                         written_mask_out, NULL, NULL);
}

/* ---- 4-slot intra-segment BBRA for straight-line ARM ALU/MUL/MLA runs -----
 * Allocates the top 4 hot unpinned guest registers (r2..r14, refs >= 2) into
 * SH-4 R4..R7 across 2..16 unconditional non-S / dead-flag ARM DP & MUL/MLA
 * instructions in BOTH ROM and IWRAM. Also caches the last shift immediate in
 * R0 across consecutive shifted ops, absorbing the old same-Rn and same-Rd
 * ARM peepholes into one general register allocator. */
static inline int sh4g_arm_bbra_maybe(u32 op)
{
  if ((op >> 28) != 0xEu)
    return 0;
  if ((op & 0x0E000000u) == 0x02000000u)      /* DP imm */
    return 1;
  if ((op & 0x0E000010u) == 0x00000000u)      /* DP reg (imm shift) */
    return 1;
  if ((op & 0x0FC000F0u) == 0x00000090u)      /* MUL / MLA */
    return 1;
  return 0;
}

static inline int sh4g_arm_bbra_classify(u32 op, u32 *rmask, u32 *wmask)
{
  *rmask = 0u;
  *wmask = 0u;
  if ((op >> 28) != 0xEu)
    return 0;
  if ((op & 0x0FC000F0u) == 0x00000090u) {    /* MUL / MLA (S ignored if caller proved dead) */
    unsigned rd = (op >> 16) & 0xFu;
    unsigned rn = (op >> 12) & 0xFu;
    unsigned rs = (op >> 8)  & 0xFu;
    unsigned rm = op & 0xFu;
    int is_mla = (op >> 21) & 1;
    if (rd == 15u || rm == 15u || rs == 15u || (is_mla && rn == 15u))
      return 0;
    *rmask = (1u << rm) | (1u << rs) | (is_mla ? (1u << rn) : 0u);
    *wmask = 1u << rd;
    return 1;
  }
  if ((op & 0x0E000000u) == 0x02000000u || (op & 0x0E000010u) == 0x00000000u) {
    u32 alu = (op >> 21) & 0xFu;
    unsigned rn = (op >> 16) & 0xFu;
    unsigned rd = (op >> 12) & 0xFu;
    int is_imm = (op >> 25) & 1;
    int unary = (alu == 0xDu || alu == 0xFu);
    if (alu == 0x5u || alu == 0x6u || alu == 0x7u ||
        (alu >= 0x8u && alu <= 0xBu))
      return 0;                               /* ADC/SBC/RSC/TST/TEQ/CMP/CMN */
    if (rd == 15u || (!unary && rn == 15u))
      return 0;
    if (!is_imm) {
      unsigned rm = op & 0xFu;
      unsigned sh_type = (op >> 5) & 3u;
      unsigned sh_imm = (op >> 7) & 31u;
      if (rm == 15u || sh_type > 2u || (sh_type != 0u && sh_imm == 0u))
        return 0;
      *rmask = (unary ? 0u : (1u << rn)) | (1u << rm);
    } else {
      *rmask = unary ? 0u : (1u << rn);
    }
    *wmask = 1u << rd;
    return 1;
  }
  return 0;
}

static inline void sh4g_bbra_emit_arm_shift(sh4_codegen *cg, unsigned sh_type,
                                            unsigned sh_imm, unsigned r_in,
                                            unsigned r_out, int *cached_sh)
{
  if (r_in != r_out)
    sh4_emit_mov_reg(cg, r_in, r_out);
  if (sh_imm == 0u)
    return;
  if (sh_type == 0u) {
    switch (sh_imm) {
    case 1:  sh4_emit_shll(cg, r_out); return;
    case 2:  sh4_emit_shll2(cg, r_out); return;
    case 3:  sh4_emit_shll2(cg, r_out); sh4_emit_shll(cg, r_out); return;
    case 4:  sh4_emit_shll2(cg, r_out); sh4_emit_shll2(cg, r_out); return;
    case 8:  sh4_emit_shll8(cg, r_out); return;
    case 9:  sh4_emit_shll8(cg, r_out); sh4_emit_shll(cg, r_out); return;
    case 10: sh4_emit_shll8(cg, r_out); sh4_emit_shll2(cg, r_out); return;
    case 16: sh4_emit_shll16(cg, r_out); return;
    case 17: sh4_emit_shll16(cg, r_out); sh4_emit_shll(cg, r_out); return;
    case 18: sh4_emit_shll16(cg, r_out); sh4_emit_shll2(cg, r_out); return;
    case 24: sh4_emit_shll16(cg, r_out); sh4_emit_shll8(cg, r_out); return;
    default: break;
    }
    if (*cached_sh != (int)sh_imm) {
      sh4_emit_mov_imm(cg, (int)sh_imm, SH4_REG_RET);
      *cached_sh = (int)sh_imm;
    }
    sh4_emit_shld(cg, SH4_REG_RET, r_out);
  } else if (sh_type == 1u) {
    switch (sh_imm) {
    case 1:  sh4_emit_shlr(cg, r_out); return;
    case 2:  sh4_emit_shlr2(cg, r_out); return;
    case 3:  sh4_emit_shlr2(cg, r_out); sh4_emit_shlr(cg, r_out); return;
    case 4:  sh4_emit_shlr2(cg, r_out); sh4_emit_shlr2(cg, r_out); return;
    case 8:  sh4_emit_shlr8(cg, r_out); return;
    case 9:  sh4_emit_shlr8(cg, r_out); sh4_emit_shlr(cg, r_out); return;
    case 10: sh4_emit_shlr8(cg, r_out); sh4_emit_shlr2(cg, r_out); return;
    case 16: sh4_emit_shlr16(cg, r_out); return;
    case 17: sh4_emit_shlr16(cg, r_out); sh4_emit_shlr(cg, r_out); return;
    case 18: sh4_emit_shlr16(cg, r_out); sh4_emit_shlr2(cg, r_out); return;
    case 24: sh4_emit_shlr16(cg, r_out); sh4_emit_shlr8(cg, r_out); return;
    default: break;
    }
    if (*cached_sh != -(int)sh_imm) {
      sh4_emit_mov_imm(cg, -(int)sh_imm, SH4_REG_RET);
      *cached_sh = -(int)sh_imm;
    }
    sh4_emit_shld(cg, SH4_REG_RET, r_out);
  } else { /* ASR */
    if (sh_imm == 1u) { sh4_emit_shar(cg, r_out); return; }
    if (sh_imm == 2u) { sh4_emit_shar(cg, r_out); sh4_emit_shar(cg, r_out); return; }
    if (*cached_sh != -(int)sh_imm) {
      sh4_emit_mov_imm(cg, -(int)sh_imm, SH4_REG_RET);
      *cached_sh = -(int)sh_imm;
    }
    sh4_emit_shad(cg, SH4_REG_RET, r_out);
  }
}

SH4G_NOINLINE int sh4g_arm_alu_run_native_ex(u8 **tp, const u32 *ops,
                                             const u32 *rm_in, const u32 *wm_in,
                                             int len)
{
  static const unsigned slot_reg[4] = {
    SH4_REG_ARG0, SH4_REG_ARG1, SH4_REG_ARG2, SH4_REG_ARG3
  };
  const u32 unpinned_mask = 0x7FFFu
#if CGBA_SH4_PIN_GUEST_R0
    & ~1u
#endif
#if CGBA_SH4_PIN_GUEST_R1
    & ~2u
#endif
    ;
  u32 rm_loc[16], wm_loc[16];
  s8 gslot[16];
  unsigned slot_guest[4];
  int nslots, i, s, cached_sh = 0;
  u32 slot_loaded = 0u, slot_dirty = 0u;
  u8 *start_tp;

  if (len < 2 || len > 16)
    return 0;
  if (!rm_in) {
    for (i = 0; i < len; i++) {
      if (!sh4g_arm_bbra_classify(ops[i], &rm_loc[i], &wm_loc[i]))
        return 0;
    }
    rm_in = rm_loc;
    wm_in = wm_loc;
  }
  nslots = sh4g_bbra_pick_slots(rm_in, wm_in, len, unpinned_mask,
                                gslot, slot_guest, NULL);
  if (nslots == 0)
    return 0;

  sh4_greg_forward_reset();
  start_tp = *tp;

  for (i = 0; i < len; i++) {
    u32 op = ops[i];
    sh4g_litseg_reserve_bytes(tp, 2, 48u);

    if ((op & 0x0FC000F0u) == 0x00000090u) {  /* MUL / MLA */
      unsigned rd = (op >> 16) & 0xFu;
      unsigned rn = (op >> 12) & 0xFu;
      unsigned rs = (op >> 8)  & 0xFu;
      unsigned rm = op & 0xFu;
      int is_mla = (op >> 21) & 1;
      sh4_codegen cg = sh4g_open(tp);
      unsigned r_m = BBRA_GET_SRC(&cg, rm, SH4_REG_T0);
      unsigned r_s = (rs == rm) ? r_m : BBRA_GET_SRC(&cg, rs, SH4_REG_T1);
      unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
      sh4_emit_mul_l(&cg, r_m, r_s);
      if (!is_mla) {
        sh4_emit_sts_macl(&cg, r_dst);
        BBRA_COMMIT_DST(&cg, rd, r_dst);
      } else {
        unsigned r_n = BBRA_GET_SRC(&cg, rn, SH4_REG_T2);
        if (r_dst == r_n) {
          sh4_emit_sts_macl(&cg, SH4_REG_T0);
          sh4_emit_add_reg(&cg, SH4_REG_T0, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        } else {
          sh4_emit_sts_macl(&cg, r_dst);
          sh4_emit_add_reg(&cg, r_n, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
        }
      }
      sh4g_close(tp, &cg);
      continue;
    }

    {
      u32 alu = (op >> 21) & 0xFu;
      unsigned rn = (op >> 16) & 0xFu;
      unsigned rd = (op >> 12) & 0xFu;
      int is_imm = (op >> 25) & 1;
      u32 imm = 0u;
      unsigned rm = op & 0xFu;
      unsigned sh_type = (op >> 5) & 3u;
      unsigned sh_imm = (op >> 7) & 31u;

      if (is_imm) {
        u32 rot = ((op >> 8) & 0xFu) * 2u;
        u32 v = op & 0xFFu;
        imm = rot ? ((v >> rot) | (v << (32u - rot))) : v;
      }

      if (alu == 0xDu || alu == 0xFu) {       /* MOV / MVN */
        unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
        if (is_imm) {
          sh4g_const(tp, (alu == 0xFu) ? ~imm : imm, r_dst);
          { sh4_codegen cg = sh4g_open(tp);
            BBRA_COMMIT_DST(&cg, rd, r_dst);
            sh4g_close(tp, &cg); }
        } else {
          sh4_codegen cg = sh4g_open(tp);
          unsigned r_src = BBRA_GET_SRC(&cg, rm, SH4_REG_T0);
          sh4g_bbra_emit_arm_shift(&cg, sh_type, sh_imm, r_src, r_dst, &cached_sh);
          if (alu == 0xFu)
            sh4_emit_not(&cg, r_dst, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
          sh4g_close(tp, &cg);
        }
        continue;
      }

      if (is_imm) {
        if (alu == 0x0u && (imm == 0xFFu || imm == 0xFFFFu)) {
          sh4_codegen cg = sh4g_open(tp);
          unsigned r_n = BBRA_GET_SRC(&cg, rn, SH4_REG_T0);
          unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
          if (imm == 0xFFu) sh4_emit_extu_b(&cg, r_n, r_dst);
          else              sh4_emit_extu_w(&cg, r_n, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
          sh4g_close(tp, &cg);
          continue;
        }
        if ((alu == 0x4u && (imm <= 127u || imm >= 0xFFFFFF80u)) ||
            (alu == 0x2u && (imm <= 128u || imm >= 0xFFFFFF81u))) {
          int d = (alu == 0x4u) ? (int)(s32)imm : (int)(s32)(0u - imm);
          sh4_codegen cg = sh4g_open(tp);
          unsigned r_n = BBRA_GET_SRC(&cg, rn, SH4_REG_T0);
          unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
          if (r_n != r_dst)
            sh4_emit_mov_reg(&cg, r_n, r_dst);
          if (d != 0)
            sh4_emit_add_imm(&cg, d, r_dst);
          BBRA_COMMIT_DST(&cg, rd, r_dst);
          sh4g_close(tp, &cg);
          continue;
        }
        if (alu == 0xEu) {                    /* BIC imm -> AND ~imm */
          imm = ~imm;
          alu = 0x0u;
        }
      }

      {
        unsigned r_op2;
        if (is_imm) {
          sh4g_const(tp, imm, SH4_REG_T1);
          r_op2 = SH4_REG_T1;
        } else if (sh_imm != 0u) {
          sh4_codegen cg = sh4g_open(tp);
          unsigned r_src_m = BBRA_GET_SRC(&cg, rm, SH4_REG_T1);
          sh4g_bbra_emit_arm_shift(&cg, sh_type, sh_imm, r_src_m, SH4_REG_T1, &cached_sh);
          sh4g_close(tp, &cg);
          r_op2 = SH4_REG_T1;
        } else {
          sh4_codegen cg = sh4g_open(tp);
          r_op2 = BBRA_GET_SRC(&cg, rm, SH4_REG_T1);
          sh4g_close(tp, &cg);
        }

        { sh4_codegen cg = sh4g_open(tp);
          unsigned r_n = BBRA_GET_SRC(&cg, rn, SH4_REG_T0);
          unsigned r_dst = BBRA_DST_REG(rd, SH4_REG_T0);
          if (alu == 0x3u) {                  /* RSB: op2 - rn */
            if (r_op2 != SH4_REG_T1)
              sh4_emit_mov_reg(&cg, r_op2, SH4_REG_T1);
            sh4_emit_sub(&cg, r_n, SH4_REG_T1);
            BBRA_COMMIT_DST(&cg, rd, SH4_REG_T1);
          } else if (r_dst == SH4_REG_T0 && r_op2 == SH4_REG_T1 &&
                     (alu == 0x0u || alu == 0x1u || alu == 0x4u || alu == 0xCu)) {
            if (alu == 0x0u)      sh4_emit_and(&cg, r_n, SH4_REG_T1);
            else if (alu == 0x1u) sh4_emit_xor(&cg, r_n, SH4_REG_T1);
            else if (alu == 0x4u) sh4_emit_add_reg(&cg, r_n, SH4_REG_T1);
            else                  sh4_emit_or(&cg, r_n, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
          } else {
            unsigned r_w = (r_dst == r_op2) ? SH4_REG_T0 : r_dst;
            if (r_w != r_n)
              sh4_emit_mov_reg(&cg, r_n, r_w);
            if (alu == 0xEu) {                /* BIC reg */
              sh4_emit_not(&cg, r_op2, SH4_REG_T1);
              sh4_emit_and(&cg, SH4_REG_T1, r_w);
            } else if (alu == 0x0u) {
              sh4_emit_and(&cg, r_op2, r_w);
            } else if (alu == 0x1u) {
              sh4_emit_xor(&cg, r_op2, r_w);
            } else if (alu == 0x2u) {
              sh4_emit_sub(&cg, r_op2, r_w);
            } else if (alu == 0x4u) {
              sh4_emit_add_reg(&cg, r_op2, r_w);
            } else {
              sh4_emit_or(&cg, r_op2, r_w);
            }
            BBRA_COMMIT_DST(&cg, rd, r_w);
          }
          sh4g_close(tp, &cg); }
      }
    }
  }

  /* Flush dirty cached slots back to reg[]. */
  if (slot_dirty) {
    sh4_codegen cg = sh4g_open(tp);
    for (s = 0; s < nslots; s++) {
      if (slot_dirty & (1u << s))
        sh4_emit_store_greg(&cg, slot_reg[s], slot_guest[s]);
    }
    sh4g_close(tp, &cg);
  }
  sh4_greg_forward_finish(start_tp, *tp,
                          *tp > start_tp && (*tp)[-2] == 0x1Eu);
  return 1;
}

static inline int sh4g_arm_alu_run_native(u8 **tp, const u32 *ops, int len)
{
  return sh4g_arm_alu_run_native_ex(tp, ops, NULL, NULL, len);
}

#undef BBRA_GET_SRC
#undef BBRA_DST_REG
#undef BBRA_COMMIT_DST

#endif /* CGBA_SH4_BBRA_EMIT_H */
