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

static inline int sh4g_thumb_alu_run_native_ex(u8 **tp, const u16 *ops,
                                               const u32 *rm_in, const u32 *wm_in,
                                               int len, u32 *written_mask_out)
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
  u8 refs[16];
  u8 writes[16];
  s8 gslot[16];
  unsigned slot_guest[4];
  int nslots = 0, i, s;
  u32 slot_loaded = 0u, slot_dirty = 0u, wmask_all = 0u;
  u32 seen_once = 0u, cand_mask = 0u;
  u8 *start_tp;

  if (len < 2 || len > 16)
    return 0;
  memset(refs, 0, sizeof refs);
  memset(writes, 0, sizeof writes);

  for (i = 0; i < len; i++) {
    u32 rm, wm, m;
    if (rm_in) {
      rm = rm_in[i];
      wm = wm_in[i];
    } else if (!sh4g_thumb_bbra_classify(ops[i], &rm, &wm)) {
      return 0;
    }
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
  if (!cand_mask)
    return 0;                                 /* no unpinned reuse: nothing to save */

  memset(gslot, -1, sizeof gslot);
  /* Pick up to 4 hottest unpinned guest regs with refs >= 2. */
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

#undef BBRA_GET_SRC
#undef BBRA_DST_REG
#undef BBRA_COMMIT_DST

  if (written_mask_out)
    *written_mask_out = wmask_all;
  return 1;
}

static inline int sh4g_thumb_alu_run_native(u8 **tp, const u16 *ops, int len,
                                            u32 *written_mask_out)
{
  return sh4g_thumb_alu_run_native_ex(tp, ops, NULL, NULL, len,
                                      written_mask_out);
}


#endif /* CGBA_SH4_BBRA_EMIT_H */
