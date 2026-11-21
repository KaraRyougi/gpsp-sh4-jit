/* Native SH-4 ARM data-processing, fused CMP+Bcc, and ARM DP run emitters.
 * Included via sh4_thumb_dp_emit.h. */
#ifndef CGBA_SH4_ARM_DP_EMIT_H
#define CGBA_SH4_ARM_DP_EMIT_H

/* Load ARM operand2 into R2: an immediate constant, or a plain register.
 * rm == 15 (unshifted) reads pc+8 — a translate-time constant. */
static inline void sh4g_arm_load_op2(u8 **tp, int is_imm, u32 op2, unsigned rm,
                                     u32 pc)
{
  if (is_imm) {
    sh4g_const(tp, op2, SH4_REG_T1);
  } else if (rm == 15) {
    sh4g_const(tp, pc + 8, SH4_REG_T1);
  } else {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
    sh4g_close(tp, &cg);
  }
}

/* ARM operand2 = reg[rm] shifted by an IMMEDIATE amount, into R2. type: 0=LSL
 * 1=LSR 2=ASR 3=ROR(#n>=1; RRX excluded). `amount` is the 5-bit field; LSL is
 * always >=1, while an LSR/ASR field of 0 means shift by 32. Verified bit-exact
 * vs arm_shifter_operand. Does NOT compute the shifter carry (arith ops take C/V
 * from the ALU; logical+S handles the carry separately). SH4 shld/shad take a
 * signed amount: +n left, -n right, with mov #imm sign-extending. */
static inline void sh4g_arm_shift_imm_reg(u8 **tp, u32 type, u32 amount,
                                          unsigned rm, unsigned dst)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_load_greg(&cg, rm, dst);
  if (type == 0) {                                 /* LSL #amount (amount 1..31) */
    switch (amount) {
    case 1:  sh4_emit_shll(&cg, dst); break;
    case 2:  sh4_emit_shll2(&cg, dst); break;
    case 8:  sh4_emit_shll8(&cg, dst); break;
    case 16: sh4_emit_shll16(&cg, dst); break;
    case 24: sh4_emit_shll16(&cg, dst); sh4_emit_shll8(&cg, dst); break;
    default:
      sh4_emit_mov_imm(&cg, (int)amount, SH4_REG_RET);
      sh4_emit_shld(&cg, SH4_REG_RET, dst);
      break;
    }
  } else if (type == 1) {                          /* LSR */
    switch (amount) {
    case 0:  sh4_emit_mov_imm(&cg, 0, dst); break; /* LSR #32 -> 0 */
    case 1:  sh4_emit_shlr(&cg, dst); break;
    case 2:  sh4_emit_shlr2(&cg, dst); break;
    case 8:  sh4_emit_shlr8(&cg, dst); break;
    case 16: sh4_emit_shlr16(&cg, dst); break;
    case 24: sh4_emit_shlr16(&cg, dst); sh4_emit_shlr8(&cg, dst); break;
    default:
      sh4_emit_mov_imm(&cg, -(int)amount, SH4_REG_RET);
      sh4_emit_shld(&cg, SH4_REG_RET, dst);        /* logical right */
      break;
    }
  } else if (type == 2) {                          /* ASR */
    if (amount == 1) {
      sh4_emit_shar(&cg, dst);
    } else {
      sh4_emit_mov_imm(&cg, amount == 0 ? -32 : -(int)amount, SH4_REG_RET);
      sh4_emit_shad(&cg, SH4_REG_RET, dst);        /* arithmetic right (#32 = sign) */
    }
  } else if (amount == 16) {                       /* ROR #16 */
    sh4_emit_swap_w(&cg, dst, dst);
  } else if (amount == 8) {                        /* ROR #8: (v>>8)|(v<<24) */
    sh4_emit_mov_reg(&cg, dst, SH4_REG_T2);
    sh4_emit_shlr8(&cg, dst);
    sh4_emit_shll16(&cg, SH4_REG_T2);
    sh4_emit_shll8(&cg, SH4_REG_T2);
    sh4_emit_or(&cg, SH4_REG_T2, dst);
  } else if (amount == 24) {                       /* ROR #24: (v>>24)|(v<<8) */
    sh4_emit_mov_reg(&cg, dst, SH4_REG_T2);
    sh4_emit_shll8(&cg, dst);
    sh4_emit_shlr16(&cg, SH4_REG_T2);
    sh4_emit_shlr8(&cg, SH4_REG_T2);
    sh4_emit_or(&cg, SH4_REG_T2, dst);
  } else {                                         /* ROR #n (n = 1..31) */
    sh4_emit_mov_reg(&cg, dst, SH4_REG_T2);        /* R3 = v */
    sh4_emit_mov_imm(&cg, -(int)amount, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, dst);          /* dst = v >> n */
    sh4_emit_mov_imm(&cg, (int)(32 - amount), SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T2);   /* R3 = v << (32-n) */
    sh4_emit_or(&cg, SH4_REG_T2, dst);             /* dst = ROR(v, n) */
  }
  sh4g_close(tp, &cg);
}

static inline void sh4g_arm_shift_imm_op2(u8 **tp, u32 type, u32 amount, unsigned rm)
{
  sh4g_arm_shift_imm_reg(tp, type, amount, rm, SH4_REG_T1);
}

/* operand2 = reg[rm] shifted by an immediate (LSL/LSR/ASR), into R2, AND the
 * shifter carry (0/1) into carry_reg. For logical+S. carry = LSL:(val>>(32-a))&1,
 * LSR/ASR:(val>>(a-1))&1 [a>=1], and bit31 for the #32 (field-0) forms. carry_reg
 * doubles as the val scratch; pass a high ARG reg so it survives the logical op. */
static inline void sh4g_arm_shift_imm_full(u8 **tp, u32 type, u32 amount,
                                           unsigned rm, unsigned carry_reg)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned op2 = SH4_REG_T1;                 /* R2 */
  const unsigned val = carry_reg;                  /* holds reg[rm], then carry */
  const unsigned tmp = SH4_REG_T2;                 /* R3 carry scratch */
  sh4_emit_load_greg(&cg, rm, val);
  /* operand2 (same recipe as sh4g_arm_shift_imm_op2) */
  sh4_emit_mov_reg(&cg, val, op2);
  if (type == 0) {
    sh4_emit_mov_imm(&cg, (int)amount, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, op2);
  } else if (type == 1) {
    if (amount == 0) sh4_emit_mov_imm(&cg, 0, op2);
    else { sh4_emit_mov_imm(&cg, -(int)amount, SH4_REG_RET); sh4_emit_shld(&cg, SH4_REG_RET, op2); }
  } else {
    sh4_emit_mov_imm(&cg, amount == 0 ? -32 : -(int)amount, SH4_REG_RET);
    sh4_emit_shad(&cg, SH4_REG_RET, op2);
  }
  /* shifter carry into val (R6) */
  if (amount == 0) {                               /* LSR#32 / ASR#32: carry = bit31 */
    sh4_emit_mov_reg(&cg, val, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);               /* T <- bit31 */
    sh4_emit_movt(&cg, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, val);
  } else {
    int cshift = (type == 0) ? -(int)(32 - amount) : -(int)(amount - 1);
    sh4_emit_mov_reg(&cg, val, tmp);
    sh4_emit_mov_imm(&cg, cshift, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, tmp);          /* tmp = val >> |cshift| */
    sh4_emit_mov_reg(&cg, tmp, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 1);                       /* R0 = carry bit */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, val);
  }
  sh4g_close(tp, &cg);
}

/* operand2 = reg[rm] shifted by (reg[rs] & 0xFF), into R2. This is the ARM
 * register-specified shifter form used here only by logical ops with S=0, so
 * carry-out is intentionally ignored. */
static inline void sh4g_arm_shift_reg_op2_noflags(u8 **tp, u32 type,
                                                  unsigned rm, unsigned rs)
{
  const unsigned op2  = SH4_REG_T1;               /* R2 = value/result */
  const unsigned amt  = SH4_REG_RET;              /* R0 = amount */
  const unsigned am32 = SH4_REG_T2;               /* R3 = amount - 32 */
  const unsigned tmp  = SH4_REG_ARG0;             /* R4 scratch */
  const unsigned tmp2 = SH4_REG_ARG1;             /* R5 scratch */
  u8 *zero_ph;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, op2);
    sh4_emit_load_greg(&cg, rs, amt);
    sh4_emit_and_imm(&cg, 0xFF);                  /* amount = Rs & 0xFF */
    sh4_emit_tst(&cg, amt, amt);                  /* amount == 0: no shift */
    sh4g_close(tp, &cg); }
  zero_ph = sh4g_emit_bt_placeholder(tp);

  if (type == 3) {                                /* ROR by register */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_and_imm(&cg, 0x1F);                /* rotate count */
      sh4_emit_tst(&cg, amt, amt);                /* multiple of 32: no shift */
      sh4g_close(tp, &cg); }
    { u8 *m32_ph = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, op2, tmp);
        sh4_emit_neg(&cg, amt, tmp2);
        sh4_emit_shld(&cg, tmp2, tmp);            /* tmp = value >> amount */
        sh4_emit_mov_imm(&cg, 32, tmp2);
        sh4_emit_sub(&cg, amt, tmp2);             /* tmp2 = 32 - amount */
        sh4_emit_shld(&cg, tmp2, op2);            /* op2 = value << tmp2 */
        sh4_emit_or(&cg, tmp, op2);
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(m32_ph, *tp); }
  } else {                                        /* LSL / LSR / ASR */
    u8 *skip_big, *big_ph;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, amt, am32);
      sh4_emit_add_imm(&cg, -32, am32);           /* am32 = amount - 32 */
      sh4_emit_cmppz(&cg, am32);                  /* amount >= 32 */
      sh4g_close(tp, &cg); }
    big_ph = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);             /* amount in 1..31 */
      if (type == 0) {
        sh4_emit_shld(&cg, amt, op2);
      } else {
        sh4_emit_neg(&cg, amt, tmp);
        if (type == 1) sh4_emit_shld(&cg, tmp, op2);
        else           sh4_emit_shad(&cg, tmp, op2);
      }
      sh4g_close(tp, &cg); }
    skip_big = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(big_ph, *tp);
    { sh4_codegen cg = sh4g_open(tp);             /* amount >= 32 */
      if (type == 2) {
        sh4_emit_mov_imm(&cg, -31, tmp);
        sh4_emit_shad(&cg, tmp, op2);             /* all sign bits */
      } else {
        sh4_emit_mov_imm(&cg, 0, op2);
      }
      sh4g_close(tp, &cg); }
    sh4g_patch_bra(skip_big, *tp);
  }

  sh4g_patch_cond(zero_ph, *tp);
}

/* Native SH4 for ARM data-processing (the dominant class on ARM-heavy games).
 * Handles the two operand forms where the shifter carry is trivial: IMMEDIATE
 * (operand2 + carry are translate-time constants) and plain REGISTER with no
 * shift (operand2 = Rm, shifter C = old C, preserved). Arithmetic reuses
 * sh4g_dp_addsub (C/V from the ALU); logical sets N/Z, leaves V, and sets C only
 * for the rotated-immediate case. Register-specified shifts are accepted only
 * for dead-flag MOV/MVN without PC operands. Flags gated on the S bit AND the
 * per-instruction live mask from arm_flag_status/arm_dead_flag_eliminate. */
SH4G_NOINLINE int sh4g_arm_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status)
{
  u32 op = (opcode >> 21) & 0xF;
  u32 S  = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  int is_imm = (opcode & 0x02000000) != 0;
  u32 op2 = 0, rm = 0;
  u32 shifted = 0, shift_type = 0, shift_amount = 0;
  u32 reg_shifted = 0, shift_rs = 0;
  u32 fm;
  int c_const = -1;                              /* -1 = preserve C; 0/1 = set const */
  fm = S ? (flag_status & 0xF) : 0;

  if (rd == 15) return 0;                        /* PC dest -> exit paths */
  if (is_imm) {
    u32 imm = opcode & 0xFF;
    u32 rot = ((opcode >> 8) & 0xF) * 2;
    op2 = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    if (rot) c_const = (int)((op2 >> 31) & 1);   /* else shifter C = old C */
  } else {
    rm = opcode & 0xF;
    if (rm == 15 && (opcode & 0x0FF0)) return 0;  /* shifted PC -> C path */
    if (opcode & 0x0FF0) {                        /* shifted register */
      shift_type = (opcode >> 5) & 3;
      if ((opcode >> 4) & 1) {
        shift_rs = (opcode >> 8) & 0xF;
        if (S || shift_rs == 15 || (op != 0xD && op != 0xF && rn == 15))
          return 0;                               /* needs shifter C or PC+12 */
        reg_shifted = 1;                          /* register-specified shift */
      } else {
        shift_amount = (opcode >> 7) & 0x1F;
        if (shift_type == 3) {
          /* ROR #n natively (AW: 1400+ `add rd,rn,rm,ror #n`/frame fell back
           * here); RRX (#0) needs C-in, and a LOGICAL op with live C needs the
           * shifter carry-out — both stay on the C path. Arithmetic ops take
           * C/V from the ALU, so live flags are fine there. */
          if (shift_amount == 0) return 0;        /* RRX -> C */
          if ((fm & 0x2) && (op < 0x2 || op == 0x8 || op == 0x9 ||
                             op >= 0xC))
            return 0;                             /* logical + live C -> C */
        }
      }
      shifted = 1;                                /* immediate/register shift */
    }                                             /* else operand2 = reg[Rm], LSL #0 */
  }

  switch (op) {
  case 0x2: case 0x4: case 0xA: case 0xB: {      /* SUB / ADD / CMP / CMN */
    int is_sub = (op == 0x2 || op == 0xA);
    int write  = (op == 0x2 || op == 0x4);
    int op2_first = 0;
    if (!write && !fm)
      return 1;                                  /* CMP/CMN, flags dead: nop */
#if CGBA_SH4_REG_FORWARD
    if (op == 0x4 && !is_imm && !shifted && rn != 15 && rm != 15 &&
        *tp == sh4_forward_at && rm == sh4_forward_guest && rn != rm) {
      u32 tmp = rn; rn = rm; rm = tmp;
    } else if (shifted && !reg_shifted && rm != 15 &&
               *tp == sh4_forward_at && rm == sh4_forward_guest && rn != rm) {
      op2_first = 1;
    }
#endif
    if (op2_first)
      sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    if (rn == 15) {
      sh4g_const(tp, pc + 8, SH4_REG_T0);        /* rn = PC: constant */
    } else {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg);
    }
    if (is_imm && !fm && write) {
      int32_t simm = is_sub ? -(int32_t)op2 : (int32_t)op2;
      if (simm >= -128 && simm <= 127) {
        sh4_codegen cg = sh4g_open(tp);
        if (simm != 0)
          sh4_emit_add_imm(&cg, (int)simm, SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
        sh4g_close(tp, &cg);
        return 1;
      }
    }
    if (!op2_first) {
      if (reg_shifted) sh4g_arm_shift_reg_op2_noflags(tp, shift_type, rm, shift_rs);
      else if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
      else             sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
    }
    sh4g_dp_addsub(tp, is_sub, rd, write, fm);
    return 1;
  }
  case 0x0: case 0x1: case 0xC: case 0xD:        /* AND/EOR/ORR/MOV */
  case 0xE: case 0xF: case 0x8: case 0x9: {      /* BIC/MVN/TST/TEQ */
    int dpop, write;
    int op2_first = 0;
    unsigned mov_reg = SH4_REG_T1;
    switch (op) {
    case 0x0: dpop = SH4DP_AND; write = 1; break;
    case 0x1: dpop = SH4DP_EOR; write = 1; break;
    case 0xC: dpop = SH4DP_ORR; write = 1; break;
    case 0xD: dpop = SH4DP_MOV; write = 1; break;
    case 0xE: dpop = SH4DP_BIC; write = 1; break;
    case 0xF: dpop = SH4DP_MVN; write = 1; break;
    case 0x8: dpop = SH4DP_AND; write = 0; break; /* TST */
    default:  dpop = SH4DP_EOR; write = 0; break; /* TEQ */
    }
    if (!write && !fm)
      return 1;                                  /* TST/TEQ, flags dead: nop */
#if CGBA_SH4_REG_FORWARD
    if (!is_imm && !shifted && rn != 15 && rm != 15 &&
        (dpop == SH4DP_AND || dpop == SH4DP_EOR || dpop == SH4DP_ORR) &&
        *tp == sh4_forward_at && rm == sh4_forward_guest && rn != rm) {
      u32 tmp = rn; rn = rm; rm = tmp;
    } else if (shifted && !reg_shifted && !(fm & 0x2) && rm != 15 &&
               !(dpop == SH4DP_MOV || dpop == SH4DP_MVN) &&
               *tp == sh4_forward_at && rm == sh4_forward_guest && rn != rm) {
      op2_first = 1;
    }
#endif
    if (op2_first)
      sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    if (!(dpop == SH4DP_MOV || dpop == SH4DP_MVN)) { /* MOV/MVN: second only */
      if (rn == 15) {
        sh4g_const(tp, pc + 8, SH4_REG_T0);      /* rn = PC: constant */
      } else {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4g_close(tp, &cg);
      }
    }
    if (op2_first) {
      /* op2 already shifted into R2 */
    } else if (reg_shifted) {
      sh4g_arm_shift_reg_op2_noflags(tp, shift_type, rm, shift_rs);
    } else if (shifted) {                        /* operand2 = shifted reg[rm] */
      if (fm & 0x2) {
        sh4g_arm_shift_imm_full(tp, shift_type, shift_amount, rm, SH4_REG_ARG2);
      } else {
#if CGBA_SH4_REG_FORWARD
        if (dpop == SH4DP_MOV && !fm &&
            *tp == sh4_forward_at && rm == sh4_forward_guest &&
            sh4_forward_host == SH4_REG_T0)
          mov_reg = SH4_REG_T0;                  /* shift R1 in place, skip MOV */
#endif
        sh4g_arm_shift_imm_reg(tp, shift_type, shift_amount, rm, mov_reg);
      }
    } else if (dpop == SH4DP_AND && is_imm && write &&
               (op2 == 0xFFu || op2 == 0xFFFFu)) {
      sh4_codegen cg = sh4g_open(tp);
      if (op2 == 0xFFu) sh4_emit_extu_b(&cg, SH4_REG_T0, SH4_REG_T0);
      else              sh4_emit_extu_w(&cg, SH4_REG_T0, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg);
      goto arm_logic_flags;
    } else if (dpop == SH4DP_BIC && is_imm) {
      /* Pre-invert immediate at translate time: AND with ~op2 skips NOT. */
      sh4g_const(tp, ~op2, SH4_REG_T1);
      dpop = SH4DP_AND;
    } else {
      int pin = sh4_pinned_reg(rd);
      if (dpop == SH4DP_MOV && is_imm && !fm && pin >= 0) {
        sh4g_const(tp, op2, (unsigned)pin);
        return 1;
      }
      sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
    }
    { sh4_codegen cg = sh4g_open(tp);
      if (dpop == SH4DP_MOV && !fm) {
        sh4_emit_store_greg(&cg, mov_reg, rd);
      } else {
        sh4g_dp_compute(&cg, dpop);
        if (write)
          sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      }
      sh4g_close(tp, &cg); }
arm_logic_flags:
    if (fm) {                                    /* N/Z; V kept; C from shifter */
      int have_c = (fm & 0x2) && (shifted || c_const >= 0);
      if (have_c) {
        /* One merged NZC pack (V preserved) instead of an NZ pack plus a
           second CPSR read-modify-write for C: 24-26B vs 32-40B. Plain-reg
           op2 keeps the old C (shifter carry = C) and stays on the NZ path. */
        if (!shifted) {
          sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_imm(&cg, c_const, SH4_REG_ARG2);
          sh4g_close(tp, &cg);
        }
        sh4g_set_flags(tp, SH4_REG_T0, SH4_REG_ARG2, SH4_REG_ARG2, 0xE);
      } else {
        sh4g_set_nz_m(tp, SH4_REG_T0, fm);
      }
    }
    return 1;
  }
  case 0x3: case 0x5: case 0x6: case 0x7: {      /* RSB / ADC / SBC / RSC */
    int reverse = (op == 0x3 || op == 0x7);       /* result = op2 - rn */
    if (rn == 15) {
      sh4g_const(tp, pc + 8, SH4_REG_T0);        /* rn = PC: constant */
    } else {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg);
    }
    if (reg_shifted) sh4g_arm_shift_reg_op2_noflags(tp, shift_type, rm, shift_rs);
    else if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else             sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
    if (reverse) {                                /* swap to T0=op2, T1=rn */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T0);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
      sh4g_close(tp, &cg);
    }
    if (op == 0x3) sh4g_dp_addsub(tp, 1, rd, 1, fm);         /* RSB = op2 - rn */
    else           sh4g_dp_carry(tp, op != 0x5, rd, 1, fm);  /* ADC add / SBC,RSC sub */
    return 1;
  }
  default: return 0;
  }
}

/* Fuse ARM `MOVS Rd, Rm{, LSL/LSR/ASR #imm5}` + single conditional
 * `ADD/SUB{EQ/NE/MI/PL} Rd, Rd, #imm` (non-S) when flags are provably dead
 * after op1 (e.g., signed division rounding `movs r6, r6, asr #9; addmi r6, r6, #1`).
 * Emits 7 SH-4 instructions instead of ~34 (no CPSR pack/unpack or carry). */
static inline int sh4g_arm_movs_cond_addsub_native(u8 **tp, u32 op0, u32 op1)
{
  u32 rd = (op0 >> 12) & 0xFu;
  u32 rm = op0 & 0xFu;
  u32 sh_type = (op0 >> 5) & 3u;
  u32 sh_imm = (op0 >> 7) & 0x1Fu;
  u32 cond = op1 >> 28;
  u32 op1_alu = (op1 >> 21) & 0xFu;
  u32 imm = op1 & 0xFFu;
  u32 rot = ((op1 >> 8) & 0xFu) * 2u;
  u32 val = rot ? ((imm >> rot) | (imm << (32u - rot))) : imm;
  int delta;
  u8 *skip;

  if ((op0 & 0xFFFF0010u) != 0xE1B00000u) return 0;
  if (rd == 15u || rm == 15u || sh_type > 2u) return 0;
  if (sh_type != 0u && sh_imm == 0u) return 0;
  if (cond != 0u && cond != 1u && cond != 4u && cond != 5u) return 0;
  if ((op1 & 0x0E100000u) != 0x02000000u) return 0;   /* imm, non-S */
  if (op1_alu != 0x4u && op1_alu != 0x2u) return 0;   /* ADD or SUB */
  if (((op1 >> 12) & 0xFu) != rd || ((op1 >> 16) & 0xFu) != rd) return 0;
  if (val > 127u) return 0;
  delta = (op1_alu == 0x4u) ? (int)val : -(int)val;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T0);          /* R1 = reg[rm] */
    if (sh_imm != 0u) {
      sh4_emit_mov_imm(&cg, (sh_type == 0u) ? (int)sh_imm : -(int)sh_imm, SH4_REG_RET);
      if (sh_type == 2u) sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
      else               sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T0);
    }
    if (cond <= 1u) sh4_emit_tst(&cg, SH4_REG_T0, SH4_REG_T0);
    else            sh4_emit_cmppz(&cg, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  skip = (cond == 1u || cond == 4u) ? sh4g_emit_bt_placeholder(tp)
                                    : sh4g_emit_bf_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    if (delta != 0)
      sh4_emit_add_imm(&cg, delta, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(skip, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
    sh4g_close(tp, &cg); }
  return 1;
}

/* ---- Fused CMP / TST / SUBS / ADDS / NZ-ALU / SHIFT + Bcc on SH-4 T bit - *
 * Evaluates the compare/test/add/sub/shift/logical condition directly into
 * SH-4 T without packing/unpacking CPSR NZCV on any edge where the flags are
 * provably dead. On any live edge AND on the cold R13 <= 0 slow path before
 * sh4_block_exit, the exact architectural flags are materialized into R8 so
 * CPSR is 100% bit-exact at every cycle gate, IRQ, and frame boundary.
 *
 * Caller has loaded operand `a` in R1 (SH4_REG_T0) and `b` in R2 (SH4_REG_T1),
 * verified (taken_dead || fall_dead), and reserved literal pool space.
 * kind: 0 = CMP(a-b), 1 = TST(a&b), 2 = SUBS(rd=a-b), 3 = ADDS(rd=a+b),
 *       4 = CMP(a,#0), 5 = precomputed NZ-only result in R1 (reg[rd] written),
 *       6 = precomputed LSL/LSR/ASR #imm5 result in R1 (reg[rd] written),
 *           original Rs in R2, extra = carry_shift (-31..0).                 */
static inline void sh4g_fused_mat_flags(u8 **tp, int kind, int extra)
{
  if (kind == 0) {
    sh4g_dp_addsub(tp, 1, 0, 0, 0xF);
  } else if (kind == 4) {                                /* CMP a, #0 (b uninit) */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 0, SH4_REG_T1);
      sh4g_close(tp, &cg); }
    sh4g_dp_addsub(tp, 1, 0, 0, 0xF);
  } else if (kind == 1) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_set_nz_m(tp, SH4_REG_T0, 0xC);
  } else if (kind == 5) {                                /* NZ-only result in R1 */
    sh4g_set_nz_m(tp, SH4_REG_T0, 0xC);
  } else if (kind == 6) {                                /* shift imm5: R1=res, R2=orig */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_ARG0);
      if (extra != 0) {
        sh4_emit_mov_imm(&cg, extra, SH4_REG_RET);
        sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_ARG0);
      }
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 1);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0);
      sh4g_close(tp, &cg); }
    sh4g_set_flags(tp, SH4_REG_T0, SH4_REG_ARG0, SH4_REG_ARG0, 0xE);
  } else if (kind == 2) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);     /* restore a = res + b */
      sh4g_close(tp, &cg); }
    sh4g_dp_addsub(tp, 1, 0, 0, 0xF);
  } else {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);         /* restore a = res - b */
      sh4g_close(tp, &cg); }
    sh4g_dp_addsub(tp, 0, 0, 0, 0xF);
  }
}

static inline u8 *sh4g_emit_fused_cmp_bcc_known(
  u8 **tp, int kind, unsigned rd, unsigned cond,
  int taken_debit, uint32_t target_pc, int is_internal,
  const void *block_exit_fn, int taken_dead, int fall_dead, int extra,
  u8 *known_target)
{
  const unsigned a = SH4_REG_T0;                         /* R1 */
  const unsigned b = SH4_REG_T1;                         /* R2 */
  int skip_if_true = 0;
  u8 *skip, *site;

  { sh4_codegen cg = sh4g_open(tp);
    if (kind == 4) {                                     /* CMP a, #0 (no R2!) */
      switch (cond) {
      case 0x0: sh4_emit_tst(&cg, a, a);   skip_if_true = 0; break; /* EQ */
      case 0x1: sh4_emit_tst(&cg, a, a);   skip_if_true = 1; break; /* NE */
      case 0xA: sh4_emit_cmppz(&cg, a);    skip_if_true = 0; break; /* GE */
      case 0xB: sh4_emit_cmppz(&cg, a);    skip_if_true = 1; break; /* LT */
      case 0xC: sh4_emit_cmppl(&cg, a);    skip_if_true = 0; break; /* GT */
      default:  sh4_emit_cmppl(&cg, a);    skip_if_true = 1; break; /* 0xD LE */
      }
    } else if (kind == 0) {                              /* CMP a, b */
      switch (cond) {
      case 0x0: sh4_emit_cmpeq(&cg, b, a); skip_if_true = 0; break;
      case 0x1: sh4_emit_cmpeq(&cg, b, a); skip_if_true = 1; break;
      case 0x2: sh4_emit_cmphs(&cg, b, a); skip_if_true = 0; break;
      case 0x3: sh4_emit_cmphs(&cg, b, a); skip_if_true = 1; break;
      case 0x8: sh4_emit_cmphi(&cg, b, a); skip_if_true = 0; break;
      case 0x9: sh4_emit_cmphi(&cg, b, a); skip_if_true = 1; break;
      case 0xA: sh4_emit_cmpge(&cg, b, a); skip_if_true = 0; break;
      case 0xB: sh4_emit_cmpge(&cg, b, a); skip_if_true = 1; break;
      case 0xC: sh4_emit_cmpgt(&cg, b, a); skip_if_true = 0; break;
      default:  sh4_emit_cmpgt(&cg, b, a); skip_if_true = 1; break; /* 0xD LE */
      }
    } else if (kind == 1) {                              /* TST a, b */
      sh4_emit_tst(&cg, b, a);
      skip_if_true = (cond == 0x1);
    } else if (kind == 5 || kind == 6) {                 /* precomputed in R1 */
      if (cond == 0x0 || cond == 0x1) {
        sh4_emit_tst(&cg, a, a);
        skip_if_true = (cond == 0x1);
      } else if (cond == 0x4 || cond == 0x5) {           /* MI (0x4) / PL (0x5) */
        sh4_emit_cmppz(&cg, a);
        skip_if_true = (cond == 0x4);
      } else {                                           /* CS (0x2) / CC (0x3) for kind==6 */
        sh4_emit_mov_reg(&cg, b, SH4_REG_RET);
        if (extra != 0) {
          sh4_emit_mov_imm(&cg, extra, SH4_REG_T2);
          sh4_emit_shld(&cg, SH4_REG_T2, SH4_REG_RET);
        }
        sh4_emit_tst_imm(&cg, 1);                        /* T = (C == 0) */
        skip_if_true = (cond == 0x2);
      }
    } else {                                             /* SUBS / ADDS */
      if (kind == 2) sh4_emit_sub(&cg, b, a);
      else           sh4_emit_add_reg(&cg, b, a);
      sh4_emit_store_greg(&cg, a, rd);
      if (cond == 0x0 || cond == 0x1) {
        sh4_emit_tst(&cg, a, a);
        skip_if_true = (cond == 0x1);
      } else {                                           /* MI (0x4) / PL (0x5) */
        sh4_emit_cmppz(&cg, a);
        skip_if_true = (cond == 0x4);
      }
    }
    sh4g_close(tp, &cg); }

  skip = skip_if_true ? sh4g_emit_bt_placeholder(tp)
                      : sh4g_emit_bf_placeholder(tp);

  /* --- TAKEN path (R1, R2 still intact!) --- */
  if (is_internal >= 2) {
    /* Forward internal branch: cannot form a loop, so skip CMP/PL R13 and
     * cold block_exit stub. For short forward skips (is_internal == 3), pack
     * small cycle debits directly into the BRA delay slot (4 bytes total). */
    if (!taken_dead)
      sh4g_fused_mat_flags(tp, kind, extra);
    if (is_internal == 3 && taken_debit > 0 && taken_debit <= 128) {
      site = sh4g_emit_bra_debit(tp, taken_debit);
    } else {
      sh4g_cycle_debit_r0(tp, taken_debit);
      if (is_internal == 3)
        site = sh4g_emit_bra_placeholder(tp);
      else
        site = sh4g_emit_patch_jump(tp);
    }
  } else {
    sh4g_cycle_debit_r0(tp, taken_debit);
    if (taken_dead) {
      sh4g_litseg_reserve_bytes(tp, 1, 32u);
      if (is_internal && known_target != NULL && taken_dead == 1) {
        long d_bt = ((long)(uintptr_t)known_target -
                     ((long)(uintptr_t)(*tp + 2) + 4)) / 2;
        if (d_bt >= -128 && d_bt <= 127) {
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_cmppl(&cg, SH4_REG_CYCLES);         /* T = (cycles > 0) */
            sh4g_close(tp, &cg); }
          sh4g_u16(tp, (uint16_t)(0x8900 | (d_bt & 0xFF))); /* BT known_target */
          sh4g_fused_mat_flags(tp, kind, extra);         /* cold: exact CPSR */
          sh4g_const(tp, target_pc, SH4_REG_ARG0);
          sh4g_block_exit_jmp(tp);
          site = NULL;
          goto fused_taken_done;
        }
        {
          long d_bra = ((long)(uintptr_t)known_target -
                        ((long)(uintptr_t)(*tp + 4) + 4)) / 2;
          if (d_bra >= -2048 && d_bra <= 2047) {
            u8 *cold;
            { sh4_codegen cg = sh4g_open(tp);
              sh4_emit_cmppl(&cg, SH4_REG_CYCLES);       /* T = (cycles > 0) */
              sh4g_close(tp, &cg); }
            cold = sh4g_emit_bf_placeholder(tp);         /* cold: cycles <= 0 */
            sh4g_u16(tp, (uint16_t)(0xA000 | (d_bra & 0x0FFF))); /* BRA target */
            sh4g_u16(tp, 0x0009);                        /* delay NOP */
            sh4g_patch_cond(cold, *tp);
            sh4g_fused_mat_flags(tp, kind, extra);       /* cold: exact CPSR */
            sh4g_const(tp, target_pc, SH4_REG_ARG0);
            sh4g_block_exit_jmp(tp);
            site = NULL;
            goto fused_taken_done;
          }
        }
      }
      {
        u8 *cold, *go_target;
        { sh4_codegen cg = sh4g_open(tp);
          if (taken_dead == 1) {
            sh4_emit_cmppl(&cg, SH4_REG_CYCLES);         /* T = (cycles > 0) */
          } else {
            /* External ROM lookahead (step 1..5): require cycles > 96 so neither a
             * mid-block gate nor cold-interp chunk can stop before the killer. */
            sh4_emit_mov_imm(&cg, 96, SH4_REG_RET);
            sh4_emit_cmpgt(&cg, SH4_REG_RET, SH4_REG_CYCLES); /* T = (cycles > 96) */
          }
          sh4g_close(tp, &cg); }
        cold = sh4g_emit_bf_placeholder(tp);             /* cold: not-taken 98%+ */
        go_target = *tp;
        if (is_internal) {
          site = sh4g_emit_patch_jump(tp);               /* patched -> BRA body_offset */
        } else {
          sh4g_const(tp, target_pc, SH4_REG_ARG0);       /* R4 = target_pc (lazy link) */
#if CGBA_SH4_LAZY_CHAIN
          site = sh4g_emit_patch_call(tp);
#else
          site = sh4g_emit_patch_jump(tp);
#endif
          sh4g_patch_jump(site, block_exit_fn);
        }
        sh4g_patch_cond(cold, *tp);
        sh4g_fused_mat_flags(tp, kind, extra);           /* cold: exact CPSR */
        if (taken_dead != 1) {
          /* 0 < cycles <= 96: CPSR is now exact; jump back to go_target without
           * calling update_gba prematurely. */
          long d_go;
          { sh4_codegen cg = sh4g_open(tp);
            sh4_emit_cmppl(&cg, SH4_REG_CYCLES);         /* T = (cycles > 0) */
            sh4g_close(tp, &cg); }
          d_go = ((long)(uintptr_t)go_target - ((long)(uintptr_t)*tp + 4)) / 2;
          sh4g_u16(tp, (uint16_t)(0x8900 | (d_go & 0xFF))); /* BT go_target */
        }
        sh4g_const(tp, target_pc, SH4_REG_ARG0);
        sh4g_block_exit_jmp(tp);
      }
    } else {
      sh4g_fused_mat_flags(tp, kind, extra);
      site = is_internal ? sh4g_branch_exit_internal_known(tp, target_pc, known_target)
                         : sh4g_branch_exit(tp, target_pc, block_exit_fn);
    }
  }
fused_taken_done:

  /* --- NOT-TAKEN fallthrough (R1, R2 still intact!) --- */
  sh4g_patch_cond(skip, *tp);
  if (!fall_dead)
    sh4g_fused_mat_flags(tp, kind, extra);

  return site;
}


#endif /* CGBA_SH4_ARM_DP_EMIT_H */
