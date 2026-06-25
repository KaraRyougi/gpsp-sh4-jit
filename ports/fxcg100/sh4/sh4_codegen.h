#ifndef CGBA_SH4_CODEGEN_H
#define CGBA_SH4_CODEGEN_H

/*
 * SH-4A (SH7305) instruction encoder for the cgba dynamic recompiler.
 *
 * All instructions are fixed 16-bit and emitted big-endian (high byte first),
 * matching the fx-CG100 target. Register operands are 0..15. Displacement
 * operands are the raw encoded field value (already scaled / shifted by the
 * caller) unless noted; helpers below convert byte distances to fields.
 *
 * Every register/immediate/control encoder in this header is verified
 * byte-for-byte against sh-elf-as by tests/sh4_codegen_audit.c. PC-relative
 * and branch-displacement encoders (which the assembler will not accept with
 * raw displacements) are covered by explicit-byte checks in the same test and
 * by tests/sh4_codegen_smoke.c.
 *
 * Reference: Renesas SH-4A Software Manual (REJ09B0003); shared-ptr.com/sh_insns.
 */

#include <stddef.h>
#include <stdint.h>

typedef struct sh4_codegen {
  uint8_t *ptr;
  uint8_t *limit;
  int overflow;
} sh4_codegen;

static inline size_t sh4_offset(const sh4_codegen *cg, const uint8_t *base)
{
  return (size_t)(cg->ptr - base);
}

static inline void sh4_emit_u16(sh4_codegen *cg, uint16_t opcode)
{
  if ((size_t)(cg->limit - cg->ptr) < 2) {
    cg->overflow = 1;
    return;
  }

  cg->ptr[0] = (uint8_t)(opcode >> 8);
  cg->ptr[1] = (uint8_t)opcode;
  cg->ptr += 2;
}

/* ------------------------------------------------------------------ */
/* Encoding helpers (operand field packers)                            */
/* ------------------------------------------------------------------ */

/* nnnn at bits 11..8, mmmm at bits 7..4 */
#define SH4_NM(base, n, m) \
  ((uint16_t)((base) | (((n) & 0xF) << 8) | (((m) & 0xF) << 4)))
/* nnnn at bits 11..8 only */
#define SH4_N(base, n)     ((uint16_t)((base) | (((n) & 0xF) << 8)))
/* mmmm at bits 11..8 (single-register forms that use the m slot there) */
#define SH4_M(base, m)     ((uint16_t)((base) | (((m) & 0xF) << 8)))
/* 8-bit immediate / displacement at bits 7..0 */
#define SH4_I8(base, i)    ((uint16_t)((base) | ((i) & 0xFF)))
/* nnnn + 4-bit disp */
#define SH4_ND(base, n, d) \
  ((uint16_t)((base) | (((n) & 0xF) << 8) | ((d) & 0xF)))
/* nnnn + mmmm + 4-bit disp */
#define SH4_NMD(base, n, m, d) \
  ((uint16_t)((base) | (((n) & 0xF) << 8) | (((m) & 0xF) << 4) | ((d) & 0xF)))

static inline int sh4_branch_disp8(uint32_t branch_pc, uint32_t target_pc)
{
  return ((int32_t)target_pc - (int32_t)(branch_pc + 4)) / 2;
}

static inline int sh4_branch_disp12(uint32_t branch_pc, uint32_t target_pc)
{
  return ((int32_t)target_pc - (int32_t)(branch_pc + 4)) / 2;
}

static inline int sh4_disp8_fits(int disp)
{
  return disp >= -128 && disp <= 127;
}

static inline int sh4_disp12_fits(int disp)
{
  return disp >= -2048 && disp <= 2047;
}

/* ------------------------------------------------------------------ */
/* System / no-operand                                                 */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_nop(sh4_codegen *cg)   { sh4_emit_u16(cg, 0x0009); }
static inline void sh4_emit_rts(sh4_codegen *cg)   { sh4_emit_u16(cg, 0x000B); }
static inline void sh4_emit_sett(sh4_codegen *cg)  { sh4_emit_u16(cg, 0x0018); }
static inline void sh4_emit_clrt(sh4_codegen *cg)  { sh4_emit_u16(cg, 0x0008); }
static inline void sh4_emit_clrmac(sh4_codegen *cg){ sh4_emit_u16(cg, 0x0028); }
static inline void sh4_emit_div0u(sh4_codegen *cg) { sh4_emit_u16(cg, 0x0019); }

/* ------------------------------------------------------------------ */
/* Data move                                                           */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_mov_reg(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6003, rn, rm)); }

static inline void sh4_emit_mov_imm(sh4_codegen *cg, int imm8, unsigned rn)
{ sh4_emit_u16(cg, SH4_I8(SH4_N(0xE000, rn), imm8)); }

static inline void sh4_emit_movt(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x0029, rn)); }

static inline void sh4_emit_swap_b(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6008, rn, rm)); }
static inline void sh4_emit_swap_w(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6009, rn, rm)); }
static inline void sh4_emit_xtrct(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x200D, rn, rm)); }

static inline void sh4_emit_extu_b(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600C, rn, rm)); }
static inline void sh4_emit_extu_w(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600D, rn, rm)); }
static inline void sh4_emit_exts_b(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600E, rn, rm)); }
static inline void sh4_emit_exts_w(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600F, rn, rm)); }

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_add_reg(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300C, rn, rm)); }
static inline void sh4_emit_add_imm(sh4_codegen *cg, int imm8, unsigned rn)
{ sh4_emit_u16(cg, SH4_I8(SH4_N(0x7000, rn), imm8)); }
static inline void sh4_emit_addc(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300E, rn, rm)); }
static inline void sh4_emit_addv(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300F, rn, rm)); }

static inline void sh4_emit_sub(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3008, rn, rm)); }
static inline void sh4_emit_subc(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300A, rn, rm)); }
static inline void sh4_emit_subv(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300B, rn, rm)); }

static inline void sh4_emit_neg(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600B, rn, rm)); }
static inline void sh4_emit_negc(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x600A, rn, rm)); }

static inline void sh4_emit_dt(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4010, rn)); }

/* Compares set T */
static inline void sh4_emit_cmpeq(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3000, rn, rm)); }
static inline void sh4_emit_cmphs(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3002, rn, rm)); }
static inline void sh4_emit_cmpge(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3003, rn, rm)); }
static inline void sh4_emit_cmphi(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3006, rn, rm)); }
static inline void sh4_emit_cmpgt(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3007, rn, rm)); }
static inline void sh4_emit_cmpstr(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x200C, rn, rm)); }
static inline void sh4_emit_cmppz(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4011, rn)); }
static inline void sh4_emit_cmppl(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4015, rn)); }
static inline void sh4_emit_cmpeq_imm(sh4_codegen *cg, int imm8)
{ sh4_emit_u16(cg, SH4_I8(0x8800, imm8)); }

/* Multiply / divide step */
static inline void sh4_emit_mul_l(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x0007, rn, rm)); }
static inline void sh4_emit_dmuls_l(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x300D, rn, rm)); }
static inline void sh4_emit_dmulu_l(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3005, rn, rm)); }
static inline void sh4_emit_div0s(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2007, rn, rm)); }
static inline void sh4_emit_div1(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x3004, rn, rm)); }

/* ------------------------------------------------------------------ */
/* Logic                                                               */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_and(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2009, rn, rm)); }
static inline void sh4_emit_or(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x200B, rn, rm)); }
static inline void sh4_emit_xor(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x200A, rn, rm)); }
static inline void sh4_emit_tst(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2008, rn, rm)); }
static inline void sh4_emit_not(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6007, rn, rm)); }

static inline void sh4_emit_and_imm(sh4_codegen *cg, int imm8)
{ sh4_emit_u16(cg, SH4_I8(0xC900, imm8)); }
static inline void sh4_emit_or_imm(sh4_codegen *cg, int imm8)
{ sh4_emit_u16(cg, SH4_I8(0xCB00, imm8)); }
static inline void sh4_emit_xor_imm(sh4_codegen *cg, int imm8)
{ sh4_emit_u16(cg, SH4_I8(0xCA00, imm8)); }
static inline void sh4_emit_tst_imm(sh4_codegen *cg, int imm8)
{ sh4_emit_u16(cg, SH4_I8(0xC800, imm8)); }

/* ------------------------------------------------------------------ */
/* Shift / rotate                                                      */
/* ------------------------------------------------------------------ */

/* Variable shift: count in Rm (negative = right), no T effect */
static inline void sh4_emit_shad(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x400C, rn, rm)); }
static inline void sh4_emit_shld(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x400D, rn, rm)); }

/* 1-bit shifts (T <- shifted-out bit) */
static inline void sh4_emit_shll(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4000, rn)); }
static inline void sh4_emit_shlr(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4001, rn)); }
static inline void sh4_emit_shal(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4020, rn)); }
static inline void sh4_emit_shar(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4021, rn)); }

/* Constant multi-bit shifts (no T effect) */
static inline void sh4_emit_shll2(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4008, rn)); }
static inline void sh4_emit_shlr2(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4009, rn)); }
static inline void sh4_emit_shll8(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4018, rn)); }
static inline void sh4_emit_shlr8(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4019, rn)); }
static inline void sh4_emit_shll16(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4028, rn)); }
static inline void sh4_emit_shlr16(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4029, rn)); }

/* Rotates (T <- rotated bit; ROTCL/ROTCR rotate through T) */
static inline void sh4_emit_rotl(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4004, rn)); }
static inline void sh4_emit_rotr(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4005, rn)); }
static inline void sh4_emit_rotcl(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4024, rn)); }
static inline void sh4_emit_rotcr(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x4025, rn)); }

/* ------------------------------------------------------------------ */
/* Memory: register indirect                                           */
/* ------------------------------------------------------------------ */

/* MOV.{B,W,L} @Rm, Rn  (load) */
static inline void sh4_emit_mov_b_load(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6000, rn, rm)); }
static inline void sh4_emit_mov_w_load(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6001, rn, rm)); }
static inline void sh4_emit_mov_l_load(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6002, rn, rm)); }

/* MOV.{B,W,L} Rm, @Rn  (store) */
static inline void sh4_emit_mov_b_store(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2000, rn, rm)); }
static inline void sh4_emit_mov_w_store(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2001, rn, rm)); }
static inline void sh4_emit_mov_l_store(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2002, rn, rm)); }

/* MOV.L @Rm+, Rn  (post-increment load) / MOV.L Rm, @-Rn (pre-dec store) */
static inline void sh4_emit_mov_l_load_inc(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x6006, rn, rm)); }
static inline void sh4_emit_mov_l_store_dec(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x2006, rn, rm)); }

/* ------------------------------------------------------------------ */
/* Memory: @(disp, Rn) with 4-bit disp (disp = byte offset, see field) */
/* ------------------------------------------------------------------ */

/* MOV.L @(disp4*4, Rm), Rn / MOV.L Rm, @(disp4*4, Rn) */
static inline void sh4_emit_mov_l_load_disp(sh4_codegen *cg, unsigned rm,
                                            unsigned rn, unsigned disp4)
{ sh4_emit_u16(cg, SH4_NMD(0x5000, rn, rm, disp4)); }
static inline void sh4_emit_mov_l_store_disp(sh4_codegen *cg, unsigned rm,
                                             unsigned rn, unsigned disp4)
{ sh4_emit_u16(cg, SH4_NMD(0x1000, rn, rm, disp4)); }

/* ------------------------------------------------------------------ */
/* Memory: @(R0, Rn) indexed                                           */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_mov_b_load_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x000C, rn, rm)); }
static inline void sh4_emit_mov_w_load_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x000D, rn, rm)); }
static inline void sh4_emit_mov_l_load_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x000E, rn, rm)); }
static inline void sh4_emit_mov_b_store_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x0004, rn, rm)); }
static inline void sh4_emit_mov_w_store_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x0005, rn, rm)); }
static inline void sh4_emit_mov_l_store_r0(sh4_codegen *cg, unsigned rm, unsigned rn)
{ sh4_emit_u16(cg, SH4_NM(0x0006, rn, rm)); }

/* ------------------------------------------------------------------ */
/* Memory: GBR-relative (R0 only)                                      */
/* ------------------------------------------------------------------ */

/* disp field: byte/1 (B), byte/2 (W), byte/4 (L) */
static inline void sh4_emit_mov_b_load_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC400, disp8)); }
static inline void sh4_emit_mov_w_load_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC500, disp8)); }
static inline void sh4_emit_mov_l_load_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC600, disp8)); }
static inline void sh4_emit_mov_b_store_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC000, disp8)); }
static inline void sh4_emit_mov_w_store_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC100, disp8)); }
static inline void sh4_emit_mov_l_store_gbr(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC200, disp8)); }

/* ------------------------------------------------------------------ */
/* Memory: PC-relative loads + MOVA (disp field = scaled)              */
/*   verified by explicit-byte checks, not the assembler diff          */
/* ------------------------------------------------------------------ */

/* MOV.W @(disp*2 + PC+4), Rn ; sign-extends 16-bit */
static inline void sh4_emit_mov_w_load_pc(sh4_codegen *cg, unsigned disp8, unsigned rn)
{ sh4_emit_u16(cg, SH4_I8(SH4_N(0x9000, rn), disp8)); }
/* MOV.L @(disp*4 + (PC&~3)+4), Rn */
static inline void sh4_emit_mov_l_load_pc(sh4_codegen *cg, unsigned disp8, unsigned rn)
{ sh4_emit_u16(cg, SH4_I8(SH4_N(0xD000, rn), disp8)); }
/* MOVA @(disp*4 + (PC&~3)+4), R0 */
static inline void sh4_emit_mova(sh4_codegen *cg, unsigned disp8)
{ sh4_emit_u16(cg, SH4_I8(0xC700, disp8)); }

/* ------------------------------------------------------------------ */
/* Control transfer                                                    */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_bra(sh4_codegen *cg, int disp12)
{ sh4_emit_u16(cg, (uint16_t)(0xA000 | (disp12 & 0x0FFF))); }
static inline void sh4_emit_bsr(sh4_codegen *cg, int disp12)
{ sh4_emit_u16(cg, (uint16_t)(0xB000 | (disp12 & 0x0FFF))); }
static inline void sh4_emit_bt(sh4_codegen *cg, int disp8)
{ sh4_emit_u16(cg, (uint16_t)(0x8900 | (disp8 & 0x00FF))); }
static inline void sh4_emit_bf(sh4_codegen *cg, int disp8)
{ sh4_emit_u16(cg, (uint16_t)(0x8B00 | (disp8 & 0x00FF))); }
static inline void sh4_emit_bt_s(sh4_codegen *cg, int disp8)
{ sh4_emit_u16(cg, (uint16_t)(0x8D00 | (disp8 & 0x00FF))); }
static inline void sh4_emit_bf_s(sh4_codegen *cg, int disp8)
{ sh4_emit_u16(cg, (uint16_t)(0x8F00 | (disp8 & 0x00FF))); }

static inline void sh4_emit_braf(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x0023, rm)); }
static inline void sh4_emit_bsrf(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x0003, rm)); }
static inline void sh4_emit_jmp(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x402B, rm)); }
static inline void sh4_emit_jsr(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x400B, rm)); }

/* ------------------------------------------------------------------ */
/* System registers (GBR / PR / MAC)                                   */
/* ------------------------------------------------------------------ */

static inline void sh4_emit_ldc_gbr(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x401E, rm)); }
static inline void sh4_emit_stc_gbr(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x0012, rn)); }

static inline void sh4_emit_lds_pr(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x402A, rm)); }
static inline void sh4_emit_sts_pr(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x002A, rn)); }
static inline void sh4_emit_lds_pr_inc(sh4_codegen *cg, unsigned rm)   /* LDS.L @Rm+,PR */
{ sh4_emit_u16(cg, SH4_M(0x4026, rm)); }
static inline void sh4_emit_sts_pr_dec(sh4_codegen *cg, unsigned rn)   /* STS.L PR,@-Rn */
{ sh4_emit_u16(cg, SH4_N(0x4022, rn)); }

static inline void sh4_emit_sts_macl(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x001A, rn)); }
static inline void sh4_emit_sts_mach(sh4_codegen *cg, unsigned rn)
{ sh4_emit_u16(cg, SH4_N(0x000A, rn)); }
static inline void sh4_emit_lds_macl(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x401A, rm)); }
static inline void sh4_emit_lds_mach(sh4_codegen *cg, unsigned rm)
{ sh4_emit_u16(cg, SH4_M(0x400A, rm)); }

#endif
