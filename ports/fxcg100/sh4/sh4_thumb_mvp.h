#ifndef CGBA_SH4_THUMB_MVP_H
#define CGBA_SH4_THUMB_MVP_H

/*
 * Reference Thumb -> SH-4A translation for the data-processing core of the
 * cgba dynarec MVP. Built directly on the verified encoder + literal-pool /
 * reg[] core (sh4_emit_core.h), decoupled from gpSP's block framework so the
 * emitted SH4 can be host-verified (tests/sh4_thumb_mvp_audit.c). These are the
 * translation patterns the gpSP sh4_emit.h handler macros adapt.
 *
 * MVP model (see docs/sh4-jit-optimization-plan.md):
 *   - every guest ARM register lives in reg[]; load -> op -> store per insn,
 *   - the N and Z condition flags are materialized directly into REG_CPSR
 *     bits 31/30 (lazy flag caching and the C/V flags are deliberately left as
 *     follow-up work; see the TODO notes per format),
 *   - control transfer and memory access are NOT handled here: those are block
 *     exits / C-helper calls owned by the block translator.
 *
 * Scratch register convention inside a translated instruction:
 *   R1 = primary operand / result (Rd)      R2 = secondary operand / scratch
 *   R3 = scratch                            R0 = index / scratch (forced uses)
 * SH4_REG_BASE (R14) and SH4_REG_CYCLES (R13) are preserved.
 *
 * sh4_translate_thumb() returns 1 if it emitted a translation, 0 if the opcode
 * is outside this MVP subset (the real translator would fall back to the
 * interpreter for those).
 */

#include "ports/fxcg100/sh4/sh4_emit_core.h"

/* CPSR flag bit positions (ARM). */
#define SH4_CPSR_N (1u << 31)
#define SH4_CPSR_Z (1u << 30)

/*
 * Materialize N and Z from a result register into REG_CPSR, preserving the
 * other CPSR bits. result must be in R1; uses R0/R2/R3 as scratch.
 *
 * TODO(flags): C and V are not computed here, and there is no lazy-flag /
 * dead-flag elimination yet — every ALU op eagerly rewrites N/Z. The gpSP
 * integration drives flag generation from block_data[].flag_data so unused
 * flags are skipped entirely; that is where this becomes cheap.
 */
static inline void sh4_emit_set_nz(sh4_emitter *e, unsigned result)
{
  sh4_codegen *cg = e->cg;
  const unsigned r_cpsr = SH4_REG_T1; /* R2 */
  const unsigned r_tmp  = SH4_REG_T2; /* R3 */

  sh4_emit_load_greg(cg, SH4_GREG_CPSR, r_cpsr);     /* r_cpsr = CPSR */
  sh4_emit_load_imm32(e, 0x3FFFFFFFu, r_tmp);        /* clear mask (~N~Z) */
  sh4_emit_and(cg, r_tmp, r_cpsr);                   /* drop old N,Z */

  /* N = result & 0x80000000 */
  sh4_emit_mov_reg(cg, result, r_tmp);               /* r_tmp = result */
  sh4_emit_load_imm32(e, SH4_CPSR_N, SH4_REG_RET);   /* R0 = 0x80000000 */
  sh4_emit_and(cg, SH4_REG_RET, r_tmp);              /* r_tmp = result & N */
  sh4_emit_or(cg, r_tmp, r_cpsr);                    /* CPSR |= N */

  /* Z = (result == 0) << 30 */
  sh4_emit_tst(cg, result, result);                  /* T = (result == 0) */
  sh4_emit_movt(cg, r_tmp);                          /* r_tmp = 0/1 */
  sh4_emit_mov_imm(cg, 30, SH4_REG_RET);             /* R0 = 30 */
  sh4_emit_shld(cg, SH4_REG_RET, r_tmp);             /* r_tmp <<= 30 */
  sh4_emit_or(cg, r_tmp, r_cpsr);                    /* CPSR |= Z */

  sh4_emit_store_greg(cg, r_cpsr, SH4_GREG_CPSR);
}

/* Thumb format 3: MOV/CMP/ADD/SUB Rd, #imm8  (001 op Rd imm8) */
static inline int sh4_thumb_imm8(sh4_emitter *e, uint16_t op)
{
  sh4_codegen *cg = e->cg;
  unsigned sub = (op >> 11) & 3;
  unsigned rd  = (op >> 8) & 7;
  unsigned imm = op & 0xFF;

  switch (sub) {
  case 0: /* MOV */
    sh4_emit_load_imm32(e, imm, SH4_REG_T0);
    sh4_emit_store_greg(cg, SH4_REG_T0, rd);
    sh4_emit_set_nz(e, SH4_REG_T0);
    return 1;
  case 1: /* CMP: flags only (Rd - imm) */
    sh4_emit_load_greg(cg, rd, SH4_REG_T0);
    sh4_emit_load_imm32(e, imm, SH4_REG_T1);
    sh4_emit_mov_reg(cg, SH4_REG_T0, SH4_REG_T2);
    sh4_emit_sub(cg, SH4_REG_T1, SH4_REG_T2);        /* T2 = Rd - imm */
    sh4_emit_set_nz(e, SH4_REG_T2);                  /* TODO: C,V */
    return 1;
  case 2: /* ADD */
    sh4_emit_load_greg(cg, rd, SH4_REG_T0);
    sh4_emit_load_imm32(e, imm, SH4_REG_T1);
    sh4_emit_add_reg(cg, SH4_REG_T1, SH4_REG_T0);
    sh4_emit_store_greg(cg, SH4_REG_T0, rd);
    sh4_emit_set_nz(e, SH4_REG_T0);                  /* TODO: C,V */
    return 1;
  default: /* 3: SUB */
    sh4_emit_load_greg(cg, rd, SH4_REG_T0);
    sh4_emit_load_imm32(e, imm, SH4_REG_T1);
    sh4_emit_sub(cg, SH4_REG_T1, SH4_REG_T0);
    sh4_emit_store_greg(cg, SH4_REG_T0, rd);
    sh4_emit_set_nz(e, SH4_REG_T0);                  /* TODO: C,V */
    return 1;
  }
}

/* Thumb format 2: ADD/SUB Rd, Rn, Rm | #imm3  (00011 I op ... ) */
static inline int sh4_thumb_addsub(sh4_emitter *e, uint16_t op)
{
  sh4_codegen *cg = e->cg;
  unsigned rd = op & 7;
  unsigned rn = (op >> 3) & 7;
  unsigned arg = (op >> 6) & 7;
  unsigned is_sub = (op >> 9) & 1;
  unsigned is_imm = (op >> 10) & 1;

  sh4_emit_load_greg(cg, rn, SH4_REG_T0);
  if (is_imm)
    sh4_emit_mov_imm(cg, (int)arg, SH4_REG_T1);      /* imm3 fits MOV #imm8 */
  else
    sh4_emit_load_greg(cg, arg, SH4_REG_T1);

  if (is_sub)
    sh4_emit_sub(cg, SH4_REG_T1, SH4_REG_T0);
  else
    sh4_emit_add_reg(cg, SH4_REG_T1, SH4_REG_T0);

  sh4_emit_store_greg(cg, SH4_REG_T0, rd);
  sh4_emit_set_nz(e, SH4_REG_T0);                    /* TODO: C,V */
  return 1;
}

/* Thumb format 1: LSL/LSR/ASR Rd, Rs, #imm5  (000 op imm5 Rs Rd) */
static inline int sh4_thumb_shift_imm(sh4_emitter *e, uint16_t op)
{
  sh4_codegen *cg = e->cg;
  unsigned sop = (op >> 11) & 3;
  unsigned imm5 = (op >> 6) & 0x1F;
  unsigned rs = (op >> 3) & 7;
  unsigned rd = op & 7;

  if (sop == 3)
    return 0;                                        /* not a shift form */

  sh4_emit_load_greg(cg, rs, SH4_REG_T0);

  if (imm5 == 0) {
    /* LSL #0 == MOV; LSR/ASR #0 mean #32 (special) -> defer */
    if (sop != 0)
      return 0;                                      /* TODO: shift-by-32 case */
  } else {
    /* SHLD/SHAD take the count in a register; negative = shift right. */
    int amount = (sop == 0) ? (int)imm5 : -(int)imm5;
    sh4_emit_mov_imm(cg, amount, SH4_REG_RET);       /* R0 = +/-imm5 */
    if (sop == 2)
      sh4_emit_shad(cg, SH4_REG_RET, SH4_REG_T0);    /* ASR (arithmetic) */
    else
      sh4_emit_shld(cg, SH4_REG_RET, SH4_REG_T0);    /* LSL/LSR (logical) */
  }

  sh4_emit_store_greg(cg, SH4_REG_T0, rd);
  sh4_emit_set_nz(e, SH4_REG_T0);                    /* TODO: C = last bit out */
  return 1;
}

/* Thumb format 4: ALU register ops  (010000 op Rs Rd) — subset */
static inline int sh4_thumb_alu_reg(sh4_emitter *e, uint16_t op)
{
  sh4_codegen *cg = e->cg;
  unsigned alu = (op >> 6) & 0xF;
  unsigned rs = (op >> 3) & 7;
  unsigned rd = op & 7;

  sh4_emit_load_greg(cg, rd, SH4_REG_T0);
  sh4_emit_load_greg(cg, rs, SH4_REG_T1);

  switch (alu) {
  case 0x0: sh4_emit_and(cg, SH4_REG_T1, SH4_REG_T0); break;        /* AND */
  case 0x1: sh4_emit_xor(cg, SH4_REG_T1, SH4_REG_T0); break;        /* EOR */
  case 0xC: sh4_emit_or(cg,  SH4_REG_T1, SH4_REG_T0); break;        /* ORR */
  case 0xF: sh4_emit_not(cg, SH4_REG_T1, SH4_REG_T0); break;        /* MVN: Rd=~Rs */
  default:
    return 0;  /* TODO: ADC/SBC/ROR/MUL/NEG/CMP/CMN/TST/BIC via this path */
  }

  sh4_emit_store_greg(cg, SH4_REG_T0, rd);
  sh4_emit_set_nz(e, SH4_REG_T0);
  return 1;
}

/* Dispatch one Thumb opcode through the MVP data-processing subset. */
static inline int sh4_translate_thumb(sh4_emitter *e, uint16_t op)
{
  if ((op & 0xE000) == 0x2000)         /* 001xx: MOV/CMP/ADD/SUB imm8 */
    return sh4_thumb_imm8(e, op);
  if ((op & 0xF800) == 0x1800)         /* 00011: ADD/SUB reg/imm3 */
    return sh4_thumb_addsub(e, op);
  if ((op & 0xE000) == 0x0000)         /* 000xx (not 00011): shift imm */
    return sh4_thumb_shift_imm(e, op);
  if ((op & 0xFC00) == 0x4000)         /* 010000: ALU reg */
    return sh4_thumb_alu_reg(e, op);
  return 0;                            /* outside MVP: interpreter fallback */
}

#endif
