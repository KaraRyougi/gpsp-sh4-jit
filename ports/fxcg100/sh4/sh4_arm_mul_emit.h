#ifndef CGBA_SH4_ARM_MUL_EMIT_H
#define CGBA_SH4_ARM_MUL_EMIT_H

/*
 * Native SH-4A emission for ARM multiply, replacing the C helpers
 * cgba_sh4_arm_multiply (MUL/MLA, 32x32->32) and cgba_sh4_arm_multiply_long
 * (UMULL/UMLAL/SMULL/SMLAL, 32x32->64). This class is large on math-heavy games
 * (profiled ~282k C-dispatches/window) and maps almost 1:1 onto the SH4
 * multiplier:
 *   MUL/MLA       -> mul.l Rm,Rs  (MACL = low 32);          sts MACL
 *   UMULL/UMLAL   -> dmulu.l Rm,Rs (MACH:MACL = 64);        sts MACL/MACH
 *   SMULL/SMLAL   -> dmuls.l Rm,Rs (MACH:MACL = 64 signed); sts MACL/MACH
 * Accumulate (MLA/UMLAL/SMLAL) is a 32- or 64-bit add (clrt;addc chain).
 *
 * Flags (S bit, opcode bit20): ARMv4 MUL/MLA set N/Z only and leave C/V (gpSP's
 * set_nz) — so sh4g_set_nz matches. The long forms set N from bit63 and Z over
 * the full 64-bit result, C/V left — sh4g_set_nz64 below. PC operands (rd/rm/rs/
 * rn == 15) are UNPREDICTABLE on ARM; we fall back to C for those.
 *
 * Decode note: for the multiply class rd is at bits 19..16 (NOT 15..12 like
 * data-proc); the long form has rdhi=19..16, rdlo=15..12.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

/* CPSR N/Z from a 64-bit result in (rlo, rhi): N = rhi bit31, Z = (rlo|rhi)==0;
 * C and V preserved. Mirrors sh4g_set_nz but over the full 64-bit value. rlo and
 * rhi must not be R0/R5/R6 (the scratch used here). */
static inline void sh4g_set_nz64(u8 **tp, unsigned rlo, unsigned rhi)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned cpsr = SH4_REG_ARG1;          /* R5 (rlo/rhi live in R1/R2) */
  const unsigned tmp  = SH4_REG_ARG2;          /* R6 */

  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, cpsr);
  sh4_emit_shll2(&cg, cpsr);                   /* clear bits 31..30 (N,Z) */
  sh4_emit_shlr2(&cg, cpsr);

  /* N = rhi & 0x80000000 (already in bit position 31) */
  sh4_emit_mov_reg(&cg, rhi, tmp);
  sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
  sh4_emit_rotr(&cg, SH4_REG_RET);             /* R0 = 0x80000000 */
  sh4_emit_and(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  /* Z = ((rlo | rhi) == 0) << 30 */
  sh4_emit_mov_reg(&cg, rlo, tmp);
  sh4_emit_or(&cg, rhi, tmp);
  sh4_emit_tst(&cg, tmp, tmp);
  sh4_emit_movt(&cg, tmp);
  sh4_emit_mov_imm(&cg, 30, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, tmp);
  sh4_emit_or(&cg, tmp, cpsr);

  sh4_emit_store_greg(&cg, cpsr, SH4_GREG_CPSR);
  sh4g_close(tp, &cg);
}

/* MUL (rd = rm*rs) / MLA (rd = rm*rs + rn). Returns 0 to fall back to C.
 * flag_status = live-flag mask; MUL S writes N/Z only, so dead N+Z skip the
 * whole materialization. */
static inline int sh4g_arm_multiply_native(u8 **tp, u32 opcode, u32 flag_status)
{
  u32 rd = (opcode >> 16) & 0xF;
  u32 rn = (opcode >> 12) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 accumulate = (opcode >> 21) & 1;            /* A bit -> MLA */
  u32 set_flags  = (opcode >> 20) & 1;            /* S bit */

  if (rd == 15 || rm == 15 || rs == 15) return 0; /* PC operand UNPREDICTABLE -> C */
  if (accumulate && rn == 15)           return 0;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T0);
    sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
    sh4_emit_mul_l(&cg, SH4_REG_T0, SH4_REG_T1);    /* MACL = rm * rs (low 32) */
    sh4_emit_sts_macl(&cg, SH4_REG_T0);             /* R1 = product */
    if (accumulate) {
      sh4_emit_load_greg(&cg, rn, SH4_REG_T1);      /* R2 = reg[rn] */
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);/* R1 += rn */
    }
    sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
    sh4g_close(tp, &cg); }
  if (set_flags)
    sh4g_set_nz_m(tp, SH4_REG_T0, flag_status);     /* N/Z only, C/V preserved */
  return 1;
}

/* UMULL/SMULL (rdhi:rdlo = rm*rs) + UMLAL/SMLAL (accumulate into rdhi:rdlo). */
static inline int sh4g_arm_multiply_long_native(u8 **tp, u32 opcode, u32 flag_status)
{
  u32 rdhi = (opcode >> 16) & 0xF;
  u32 rdlo = (opcode >> 12) & 0xF;
  u32 rs   = (opcode >> 8) & 0xF;
  u32 rm   = opcode & 0xF;
  u32 is_signed  = (opcode >> 22) & 1;
  u32 accumulate = (opcode >> 21) & 1;
  u32 set_flags  = (opcode >> 20) & 1;

  if (rdhi == 15 || rdlo == 15 || rm == 15 || rs == 15) return 0;
  if (rdhi == rdlo) return 0;                         /* UNPREDICTABLE -> C */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T0);
    sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
    if (is_signed) sh4_emit_dmuls_l(&cg, SH4_REG_T0, SH4_REG_T1);  /* MACH:MACL signed */
    else           sh4_emit_dmulu_l(&cg, SH4_REG_T0, SH4_REG_T1);  /* MACH:MACL unsigned */
    sh4_emit_sts_macl(&cg, SH4_REG_T0);              /* R1 = low 32  */
    sh4_emit_sts_mach(&cg, SH4_REG_T1);              /* R2 = high 32 */
    if (accumulate) {                                /* 64-bit add of reg[rdhi]:reg[rdlo] */
      sh4_emit_load_greg(&cg, rdlo, SH4_REG_T2);     /* R3 = reg[rdlo] */
      sh4_emit_load_greg(&cg, rdhi, SH4_REG_ARG0);   /* R4 = reg[rdhi] */
      sh4_emit_clrt(&cg);
      sh4_emit_addc(&cg, SH4_REG_T2, SH4_REG_T0);    /* lo += rdlo, T = carry */
      sh4_emit_addc(&cg, SH4_REG_ARG0, SH4_REG_T1);  /* hi += rdhi + carry */
    }
    sh4_emit_store_greg(&cg, SH4_REG_T0, rdlo);      /* reg[rdlo] = low  */
    sh4_emit_store_greg(&cg, SH4_REG_T1, rdhi);      /* reg[rdhi] = high */
    sh4g_close(tp, &cg); }
  if (set_flags && (flag_status & 0xC))
    sh4g_set_nz64(tp, SH4_REG_T0, SH4_REG_T1);       /* N=bit63, Z=64-bit==0 */
  return 1;
}

#endif /* CGBA_SH4_ARM_MUL_EMIT_H */
