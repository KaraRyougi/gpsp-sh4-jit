#ifndef CGBA_SH4_THUMB_DP_EMIT_H
#define CGBA_SH4_THUMB_DP_EMIT_H

/*
 * Native SH-4A emission for Thumb data-processing, replacing the per-instruction
 * C-helper call (cgba_sh4_thumb_dp) one op at a time.
 *
 * sh4g_thumb_dp_native() decodes the opcode exactly as the C oracle does and
 * emits native SH4 for the ops on its allow-list, returning 1. For every other
 * op it returns 0 WITHOUT advancing *tp, so the macro falls back to
 * SH4_CALL_OP2(cgba_sh4_thumb_dp). Rolling out an op = adding a case here; the
 * build stays shippable and lockstep-correct for the converted subset at all
 * times.
 *
 * Flags follow the verified NZCV recipe:
 *   ADD = a+b      : result+C via `clrt; addc`,   V via `addv` on a copy.
 *   SUB/CMP = a-b  : result via `sub`, C via `cmp/hs` (ARM C directly),
 *                    V via `subv` on a copy.
 *   logical        : N/Z only; C and V are left untouched (the op doesn't move
 *                    them — the shifter does, handled elsewhere).
 * ADC/SBC (carry-in) and the MUL/hi-reg-PC forms stay on the C path for now.
 *
 * Correctness gate: the single-block lockstep and the frame diff both compare
 * reg[REG_CPSR], so C and V must be exact, not just N/Z.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

/* Write N/Z/C/V into reg[REG_CPSR] from a result register plus C and V already
 * reduced to 0/1 in rc/rv. N = result bit31, Z = (result==0). rc and rv must NOT
 * be R1/R2/R3 (the working set used here). Mirrors sh4g_set_nz's N/Z path. */
static inline void sh4g_set_nzcv(u8 **tp, unsigned result, unsigned rc, unsigned rv)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned cpsr = SH4_REG_T1; /* R2 */
  const unsigned tmp  = SH4_REG_T2; /* R3 */

  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, cpsr);
  sh4_emit_shll2(&cg, cpsr);                 /* clear bits 31..28 (N,Z,C,V) */
  sh4_emit_shll2(&cg, cpsr);
  sh4_emit_shlr2(&cg, cpsr);
  sh4_emit_shlr2(&cg, cpsr);

  /* N = result & 0x80000000 (already in position 31) */
  sh4_emit_mov_reg(&cg, result, tmp);
  sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
  sh4_emit_rotr(&cg, SH4_REG_RET);           /* R0 = 0x80000000 */
  sh4_emit_and(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  /* Z = (result == 0) << 30 */
  sh4_emit_tst(&cg, result, result);
  sh4_emit_movt(&cg, tmp);
  sh4_emit_mov_imm(&cg, 30, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  /* C = rc << 29 */
  sh4_emit_mov_reg(&cg, rc, tmp);
  sh4_emit_mov_imm(&cg, 29, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  /* V = rv << 28 */
  sh4_emit_mov_reg(&cg, rv, tmp);
  sh4_emit_mov_imm(&cg, 28, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  sh4_emit_store_greg(&cg, cpsr, SH4_GREG_CPSR);
  sh4g_close(tp, &cg);
}

/* ADD/SUB of (a in R1, b in R2) with full NZCV, result left in R1 and stored to
 * reg[rd] when write_result. Verified recipe:
 *   ADD: clrt; addc => result + C in T;  addv on a copy => V in T.
 *   SUB: cmp/hs => ARM C in T (a>=b);  sub => result;  subv on a copy => V.
 * C and V are captured to R4/R5 (movt) and packed by sh4g_set_nzcv. */
static inline void sh4g_dp_addsub(u8 **tp, int is_sub, unsigned rd,
                                  int write_result, int set_flags)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned a = SH4_REG_T0;     /* R1 = first / result */
  const unsigned b = SH4_REG_T1;     /* R2 = second */
  const unsigned acopy = SH4_REG_T2; /* R3 = copy of a, for the V recompute */
  const unsigned rc = SH4_REG_ARG0;  /* R4 = C (0/1) */
  const unsigned rv = SH4_REG_ARG1;  /* R5 = V (0/1) */

  if (set_flags)
    sh4_emit_mov_reg(&cg, a, acopy);           /* save original a for V */
  if (is_sub) {
    if (set_flags) { sh4_emit_cmphs(&cg, b, a); sh4_emit_movt(&cg, rc); }
    sh4_emit_sub(&cg, b, a);                    /* a = a - b (result) */
    if (set_flags) { sh4_emit_subv(&cg, b, acopy); sh4_emit_movt(&cg, rv); }
  } else if (set_flags) {
    sh4_emit_clrt(&cg);
    sh4_emit_addc(&cg, b, a);                   /* a = a + b + 0, T = carry = ARM C */
    sh4_emit_movt(&cg, rc);
    sh4_emit_addv(&cg, b, acopy);               /* T = signed overflow = V */
    sh4_emit_movt(&cg, rv);
  } else {
    sh4_emit_add_reg(&cg, b, a);                /* a = a + b (no flags) */
  }
  if (write_result)
    sh4_emit_store_greg(&cg, a, rd);
  sh4g_close(tp, &cg);
  if (set_flags)
    sh4g_set_nzcv(tp, SH4_REG_T0, SH4_REG_ARG0, SH4_REG_ARG1);
}

/* Emit native SH4 for the Thumb data-proc `opcode`, or return 0 to fall back to
 * the C helper. flag_status is gpSP's per-instruction dead-flag mask (bits N=8
 * Z=4 C=2 V=1): when the flags this op sets are all dead, the flag emission is
 * skipped — the big win, since the eager NZCV pack dominates the op's cost. */
static inline int sh4g_thumb_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status)
{
  u32 hi = (opcode >> 8) & 0xFF;
  (void)pc;

  /* fmt3 MOV Rd,#imm8  (001 00 ddd iiiiiiii) — N/Z only, C/V preserved. */
  if (hi >= 0x20 && hi <= 0x27) {
    unsigned rd = (opcode >> 8) & 7;
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T0);
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    if (flag_status & 0xC)
      sh4g_set_nz(tp, SH4_REG_T0);
    return 1;
  }

  /* fmt3 CMP/ADD/SUB Rd,#imm8  (001 op ddd iiiiiiii), op: 1 CMP, 2 ADD, 3 SUB. */
  if (hi >= 0x28 && hi <= 0x3F) {
    unsigned sub = (opcode >> 11) & 3;
    unsigned rd  = (opcode >> 8) & 7;
    int sf = (flag_status & 0xF) != 0;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rd, SH4_REG_T0);          /* a = reg[rd] */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T1);          /* b = imm8 */
    if (sub == 2) sh4g_dp_addsub(tp, 0, rd, 1, sf);     /* ADD */
    else          sh4g_dp_addsub(tp, 1, rd, sub == 3, sf); /* CMP (no wb) / SUB */
    return 1;
  }

  /* fmt2 ADD/SUB Rd,Rn,Rm|#imm3  (00011 I op nnn mmm/iii ddd). */
  if (hi >= 0x18 && hi <= 0x1B) {
    unsigned rd  = opcode & 7;
    unsigned rn  = (opcode >> 3) & 7;
    unsigned arg = (opcode >> 6) & 7;
    unsigned is_sub = (opcode >> 9) & 1;
    unsigned is_imm = (opcode >> 10) & 1;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);          /* a = reg[rn] */
      sh4g_close(tp, &cg); }
    if (is_imm) sh4g_const(tp, arg, SH4_REG_T1);        /* b = imm3 */
    else { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, arg, SH4_REG_T1);         /* b = reg[Rm] */
      sh4g_close(tp, &cg); }
    sh4g_dp_addsub(tp, is_sub, rd, 1, (flag_status & 0xF) != 0);
    return 1;
  }

  /* fmt4 logical ALU reg  (010000 op sss ddd) — N/Z only, C/V preserved.
   * Arithmetic (NEG/CMP/CMN/ADC/SBC), shifts and MUL stay on the C path. */
  if (hi >= 0x40 && hi <= 0x43) {
    unsigned alu = (opcode >> 6) & 0xF;
    unsigned rs  = (opcode >> 3) & 7;
    unsigned rd  = opcode & 7;
    int op, write = 1;
    switch (alu) {
    case 0x0: op = SH4DP_AND; break;                    /* AND */
    case 0x1: op = SH4DP_EOR; break;                    /* EOR */
    case 0x8: op = SH4DP_TST; write = 0; break;         /* TST (no wb) */
    case 0xC: op = SH4DP_ORR; break;                    /* ORR */
    case 0xE: op = SH4DP_BIC; break;                    /* BIC */
    case 0xF: op = SH4DP_MVN; break;                    /* MVN: Rd = ~Rs */
    default:  return 0;
    }
    { sh4_codegen cg = sh4g_open(tp);
      if (op == SH4DP_MVN)                              /* second-operand only */
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
      else {
        sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
      }
      sh4g_dp_compute(&cg, op);
      if (write)
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg); }
    if (flag_status & 0xC)
      sh4g_set_nz(tp, SH4_REG_T0);
    return 1;
  }

  return 0;
}

#endif /* CGBA_SH4_THUMB_DP_EMIT_H */
