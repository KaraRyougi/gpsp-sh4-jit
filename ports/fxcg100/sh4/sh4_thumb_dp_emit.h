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
 * Register-specified shifts and hi-register PC forms are emitted natively;
 * both have execution-oracle coverage for their ARM-defined corner cases.
 *
 * Correctness gate: the single-block lockstep and the frame diff both compare
 * reg[REG_CPSR], so C and V must be exact, not just N/Z.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_fastmem.h"

#ifndef CGBA_SH4_LDST_DETAIL_COUNTERS
#define CGBA_SH4_LDST_DETAIL_COUNTERS 0
#endif

extern u8 *memory_map_read[];
extern u16 io_registers[512];
extern u32 spsr[6];
int cgba_sh4_thumb_ldst(u32 opcode, u32 pc);
int cgba_sh4_arm_psr(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */
void sh4_op2_pc_tramp(void);
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
extern u32 cgba_sh4_native_thumb_const_io_count;
extern u32 cgba_sh4_native_thumb_runtime_io_count;
#endif

/* NZCV packing lives in sh4g_set_flags (sh4_emit_glue.h), masked by the
 * per-instruction flag liveness the scan pass computes. */

/* ADD/SUB of (a in R1, b in R2), result left in R1 and stored to reg[rd] when
 * write_result. fmask = live-flag mask (N=8 Z=4 C=2 V=1) — rounded up so only
 * the live flags' inputs are computed. Verified full recipe:
 *   ADD: clrt; addc => result + C in T;  addv on a copy => V in T.
 *   SUB: cmp/hs => ARM C in T (a>=b);  sub => result;  subv on a copy => V.
 * C and V are captured to R4/R5 (movt) and packed by sh4g_set_flags. */
static inline void sh4g_dp_addsub(u8 **tp, int is_sub, unsigned rd,
                                  int write_result, u32 fmask)
{
  u32 eff = sh4g_flags_round(fmask & 0xF);
  sh4_codegen cg = sh4g_open(tp);
  const unsigned a = SH4_REG_T0;     /* R1 = first / result */
  const unsigned b = SH4_REG_T1;     /* R2 = second */
  const unsigned acopy = SH4_REG_T2; /* R3 = copy of a, for the V recompute */
  const unsigned rc = SH4_REG_ARG0;  /* R4 = C (0/1) */
  const unsigned rv = SH4_REG_ARG1;  /* R5 = V (0/1) */

  if (eff & 0x1)
    sh4_emit_mov_reg(&cg, a, acopy);           /* save original a for V */
  if (is_sub) {
    if (eff & 0x2) { sh4_emit_cmphs(&cg, b, a); sh4_emit_movt(&cg, rc); }
    sh4_emit_sub(&cg, b, a);                    /* a = a - b (result) */
    if (eff & 0x1) { sh4_emit_subv(&cg, b, acopy); sh4_emit_movt(&cg, rv); }
  } else if (eff & 0x3) {
    if (eff & 0x2) {
      sh4_emit_clrt(&cg);
      sh4_emit_addc(&cg, b, a);                 /* a = a + b + 0, T = ARM C */
      sh4_emit_movt(&cg, rc);
    } else {
      sh4_emit_add_reg(&cg, b, a);
    }
    if (eff & 0x1) {
      sh4_emit_addv(&cg, b, acopy);             /* T = signed overflow = V */
      sh4_emit_movt(&cg, rv);
    }
  } else {
    sh4_emit_add_reg(&cg, b, a);                /* a = a + b (no C/V wanted) */
  }
  if (write_result)
    sh4_emit_store_greg(&cg, a, rd);
  sh4g_close(tp, &cg);
  sh4g_set_flags(tp, SH4_REG_T0, SH4_REG_ARG0, SH4_REG_ARG1, eff);
}

/* ADC/SBC/RSC: carry-in add/subtract of (a in R1, b in R2), result in R1. The
 * shifter carry is irrelevant here (C comes from the ALU). Carry-in T = old C
 * for ADC, !old C for SBC/RSC (ARM borrow = ~C). C-out = T for ADC, !T for the
 * subtract (ARM C = !borrow). V uses the bit formula from the saved operands:
 *   add:  V = (~(a^b) & (a^res)) >> 31     sub: V = ((a^b) & (a^res)) >> 31. */
static inline void sh4g_dp_carry(u8 **tp, int is_sub, unsigned rd,
                                 int write_result, u32 fmask)
{
  u32 eff = sh4g_flags_round(fmask & 0xF);
  sh4_codegen cg = sh4g_open(tp);
  const unsigned a  = SH4_REG_T0;    /* R1 = first / result */
  const unsigned b  = SH4_REG_T1;    /* R2 = second (preserved by addc/subc) */
  const unsigned a0 = SH4_REG_ARG2;  /* R6 = saved original a (for V) */
  const unsigned rc = SH4_REG_ARG0;  /* R4 = C (0/1) */
  const unsigned rv = SH4_REG_ARG1;  /* R5 = V (0/1) */
  const unsigned t  = SH4_REG_T2;    /* R3 scratch */

  if (eff & 0x1) sh4_emit_mov_reg(&cg, a, a0);
  if (!is_sub) {                                  /* ADC: T = old C, then a+b+C */
    sh4_emit_load_greg(&cg, SH4_GREG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);              /* T = CPSR bit29 = old C */
    sh4_emit_addc(&cg, b, a);                     /* a = a + b + C, T = C-out */
    if (eff & 0x2) sh4_emit_movt(&cg, rc);
  } else {                                        /* SBC/RSC: T = !old C, then a-b-!C */
    sh4_emit_load_greg(&cg, SH4_GREG_CPSR, t);
    sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
    sh4_emit_rotr(&cg, SH4_REG_RET);
    sh4_emit_shlr2(&cg, SH4_REG_RET);             /* R0 = 0x20000000 (C bit) */
    sh4_emit_tst(&cg, SH4_REG_RET, t);            /* T = (old C == 0) = !C */
    sh4_emit_subc(&cg, b, a);                     /* a = a - b - !C, T = borrow */
    if (eff & 0x2) {                              /* ARM C = !borrow */
      sh4_emit_movt(&cg, rc);
      sh4_emit_not(&cg, rc, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 1);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, rc);
    }
  }
  if (eff & 0x1) {                                /* V from saved a0, b, res */
    sh4_emit_mov_reg(&cg, a0, t); sh4_emit_xor(&cg, b, t);     /* t = a0 ^ b */
    if (!is_sub) sh4_emit_not(&cg, t, t);                       /* add: ~(a0^b) */
    sh4_emit_mov_reg(&cg, a0, SH4_REG_RET); sh4_emit_xor(&cg, a, SH4_REG_RET); /* R0 = a0^res */
    sh4_emit_and(&cg, SH4_REG_RET, t);
    sh4_emit_shll(&cg, t);                         /* T = bit31 of t */
    sh4_emit_movt(&cg, rv);
  }
  if (write_result) sh4_emit_store_greg(&cg, a, rd);
  sh4g_close(tp, &cg);
  sh4g_set_flags(tp, SH4_REG_T0, SH4_REG_ARG0, SH4_REG_ARG1, eff);
}

/* Emit native SH4 for the Thumb data-proc `opcode`, or return 0 to fall back to
 * the C helper. flag_status bits 0-3 are gpSP's per-instruction live-flag mask
 * (thumb_dead_flag_eliminate): a clear bit has no reader before the next writer
 * on any path — including the conservative all-live at block ends and before
 * every PC-changer — so skipping its materialization is unobservable except at
 * mid-block store-alert exits, the same trade the upstream x86/ARM backends
 * ship. Producing extra flags is always safe; sh4g_flags_round exploits that. */
static inline int sh4g_thumb_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status,
                                       int cycles)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 fm = flag_status & 0xF;
  if (opcode == 0x46C0u)                                /* MOV r8,r8: Thumb NOP */
    return 1;

  /* fmt3 MOV Rd,#imm8  (001 00 ddd iiiiiiii) — N/Z only, C/V preserved. */
  if (hi >= 0x20 && hi <= 0x27) {
    unsigned rd = (opcode >> 8) & 7;
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T0);
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    sh4g_set_nz_m(tp, SH4_REG_T0, fm);
    return 1;
  }

  /* fmt3 CMP/ADD/SUB Rd,#imm8  (001 op ddd iiiiiiii), op: 1 CMP, 2 ADD, 3 SUB. */
  if (hi >= 0x28 && hi <= 0x3F) {
    unsigned sub = (opcode >> 11) & 3;
    unsigned rd  = (opcode >> 8) & 7;
    if (sub == 1 && !fm)
      return 1;                                         /* CMP, flags dead: nop */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rd, SH4_REG_T0);          /* a = reg[rd] */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T1);          /* b = imm8 */
    if (sub == 2) sh4g_dp_addsub(tp, 0, rd, 1, fm);     /* ADD */
    else          sh4g_dp_addsub(tp, 1, rd, sub == 3, fm); /* CMP (no wb) / SUB */
    return 1;
  }

  /* fmt2 ADD/SUB Rd,Rn,Rm|#imm3  (00011 I op nnn mmm/iii ddd). I=bit10 selects
   * register (0x18-0x1B) vs #imm3 (0x1C-0x1F); both go native via sh4g_dp_addsub. */
  if (hi >= 0x18 && hi <= 0x1F) {
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
    sh4g_dp_addsub(tp, is_sub, rd, 1, fm);
    return 1;
  }

  /* fmt4 ALU reg  (010000 op sss ddd). Register-specified shifts stay on the
   * C path; the rest are exact native NZCV or N/Z as appropriate. */
  if (hi >= 0x40 && hi <= 0x43) {
    unsigned alu = (opcode >> 6) & 0xF;
    unsigned rs  = (opcode >> 3) & 7;
    unsigned rd  = opcode & 7;
    int op, write = 1;
    if (alu == 0x5 || alu == 0x6) {                     /* ADC / SBC */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
        sh4g_close(tp, &cg); }
      sh4g_dp_carry(tp, alu == 0x6, rd, 1, fm);
      return 1;
    }
    if (alu == 0x9 || alu == 0xA || alu == 0xB) {       /* NEG / CMP / CMN */
      if (alu != 0x9 && !fm)
        return 1;                                       /* CMP/CMN, flags dead */
      { sh4_codegen cg = sh4g_open(tp);
        if (alu == 0x9)
          sh4_emit_mov_imm(&cg, 0, SH4_REG_T0);
        else
          sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
        sh4g_close(tp, &cg); }
      sh4g_dp_addsub(tp, alu != 0xB, rd, alu == 0x9, fm);
      return 1;
    }
    switch (alu) {
    case 0x0: op = SH4DP_AND; break;                    /* AND */
    case 0x1: op = SH4DP_EOR; break;                    /* EOR */
    case 0x8: op = SH4DP_TST; write = 0; break;         /* TST (no wb) */
    case 0xC: op = SH4DP_ORR; break;                    /* ORR */
    case 0xD: op = SH4DP_MUL; break;                    /* MUL */
    case 0xE: op = SH4DP_BIC; break;                    /* BIC */
    case 0xF: op = SH4DP_MVN; break;                    /* MVN: Rd = ~Rs */
    default:  return 0;
    }
    if (!write && !fm)
      return 1;                                         /* TST, flags dead */
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
    sh4g_set_nz_m(tp, SH4_REG_T0, fm);
    return 1;
  }

  /* fmt5 hi-reg ADD/CMP/MOV. Any source or ALU operand R15 reads the
   * translate-time constant PC+4. A PC destination exits through the same
   * contract as the C helper (R4 = new PC, R1 = 1: pure PC change ->
   * sh4_pc_redispatch); this includes ADD pc,Rs and MOV pc,pc, not only the
   * common MOV pc,lr return idiom. */
  if (hi >= 0x44 && hi <= 0x46) {
    unsigned op = (opcode >> 8) & 3;                     /* 0 ADD, 1 CMP, 2 MOV */
    unsigned rd = (opcode & 7) | ((opcode >> 4) & 8);
    unsigned rs = (opcode >> 3) & 0xF;
    if (rd == 15) {
      if (op == 1) {                                    /* CMP pc,Rs */
        if (!fm)
          return 1;                                     /* flags dead: nop */
        sh4g_const(tp, pc + 4, SH4_REG_T0);
        if (rs == 15) sh4g_const(tp, pc + 4, SH4_REG_T1);
        else { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
          sh4g_close(tp, &cg); }
        sh4g_dp_addsub(tp, 1, 0, 0, fm);
        return 1;
      }
      if (op > 2)
        return 0;

      /* Debit before calculating the target: a large debit materializes an
       * immediate through R1, while the target is deliberately kept in R0. */
      sh4g_cycle_debit(tp, cycles);
      if (op == 0) {                                    /* ADD pc,Rs */
        sh4g_const(tp, pc + 4, SH4_REG_RET);
        if (rs == 15) sh4g_const(tp, pc + 4, SH4_REG_T1);
        else { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
          sh4g_close(tp, &cg); }
        if (rs == 15) { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
          sh4g_close(tp, &cg); }
      } else if (rs == 15) {                            /* MOV pc,pc */
        sh4g_const(tp, pc + 4, SH4_REG_RET);
      } else {
        sh4g_load_greg(tp, rs, SH4_REG_RET);            /* MOV pc,Rs */
      }
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, -2, SH4_REG_T1);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);      /* new PC = rs & ~1 */
        sh4_emit_store_greg(&cg, SH4_REG_RET, SH4_GREG_PC);
        sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0);/* R4 = new PC */
        sh4_emit_mov_imm(&cg, 1, SH4_REG_T0);            /* R1 = pure PC change */
        sh4g_close(tp, &cg); }
      sh4g_vec_jmp(tp, SH4G_VEC_helper_exit);
      return 1;
    }
    if (op == 1) {                                       /* CMP */
      if (!fm)
        return 1;                                        /* flags dead: nop */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (rs == 15) sh4g_const(tp, pc + 4, SH4_REG_T1);  /* Thumb R15 = PC+4 */
      else { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
        sh4g_close(tp, &cg); }
      sh4g_dp_addsub(tp, 1, rd, 0, fm);
      return 1;
    }
    if (op == 0) {                                       /* ADD */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (rs == 15) sh4g_const(tp, pc + 4, SH4_REG_T1);
      else { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T1);
        sh4g_close(tp, &cg); }
      sh4g_dp_addsub(tp, 0, rd, 1, 0);
      return 1;
    }
    if (op == 2) {                                       /* MOV */
      if (rs == 15) {
        sh4g_const(tp, pc + 4, SH4_REG_T0);
        sh4g_store_greg(tp, SH4_REG_T0, rd);
      } else { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rs, SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
        sh4g_close(tp, &cg); }
      return 1;
    }
  }

  return 0;
}

/* R0 = (reg[CPU_MODE] & 0xF) * 4 and base_rn = &spsr[0], ready for the
 * @(R0,Rn) indexed forms. REG_SPSR/REG_MODE discard the privilege bit the
 * same way. */
static inline void sh4g_psr_spsr_index(u8 **tp, unsigned base_rn)
{
  sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_RET);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_and_imm(&cg, 0xF);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)spsr, base_rn);
}

/* Native MRS/MSR. MRS cpsr is a plain register copy: reg[REG_CPSR] carries
 * the full canonical flag set in this port (set_nzcv / the helpers write
 * CPSR bits directly), so no flag collapse pass is needed. SPSR forms are
 * plain spsr[mode & 0xF] array reads/masked merges — no banking or IRQ
 * side effects. MSR cpsr merges under gpSP's privileged field mask with
 * three runtime guards to the C helper: USER mode (restricted mask), a
 * Thumb bit change, and an IRQ-enabled result with an interrupt actually
 * pending (needs the vector + redispatch). Mode changes JSR the resident
 * set_cpu_mode routine (sh4g_psr_emit_rebank_routine, sh4_fastmem.c
 * buffer) — full FIQ semantics, no block-byte or I-cache cost at the
 * site. The hot IRQ-dispatcher bracket — mrs spsr, two mode-switching
 * msr cpsr_cf, msr spsr — stays fully native. MSR with a PC operand
 * stays on C. */
static inline int sh4g_arm_psr_native(u8 **tp, u32 opcode, u32 pc, int cycles)
{
  u32 is_msr   = (opcode >> 21) & 1;
  u32 use_spsr = (opcode >> 22) & 1;

  if (!is_msr) {                                         /* MRS rd, psr */
    u32 rd = (opcode >> 12) & 0xF;
    if (rd == 15)
      return 0;
    if (use_spsr) {                                      /* rd = spsr[mode] */
      sh4g_psr_spsr_index(tp, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
    } else {
      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
    }
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    return 1;
  }

  {                                                      /* MSR cpsr_<f>, op2 */
    u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
    u32 is_imm = (opcode >> 25) & 1;
    u32 val = 0, rm = 0;
    if (is_imm) {
      u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
      val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else {
      rm = opcode & 0xF;
      if (rm == 15)
        return 0;
    }
    if (pfield == 0)
      return 0;

    if (use_spsr) {                    /* MSR spsr_<f>: masked array merge
                                        * (gpSP spsr_masks — bit4 of the
                                        * control byte is architecturally
                                        * fixed and excluded). */
      u32 mask = (pfield == 3) ? 0xF00000EFu
               : (pfield == 2) ? 0xF0000000u : 0x000000EFu;
      sh4g_psr_spsr_index(tp, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      sh4g_const(tp, ~mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (is_imm)
        sh4g_const(tp, val & mask, SH4_REG_T1);
      else {
        sh4g_load_greg(tp, rm, SH4_REG_T1);
        sh4g_const(tp, mask, SH4_REG_ARG1);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_and(&cg, SH4_REG_ARG1, SH4_REG_T1);
          sh4g_close(tp, &cg); }
      }
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T0, SH4_REG_T2);
        sh4g_close(tp, &cg); }
      return 1;
    }

    if (pfield == 2) {                 /* flags only: mask is 0xF0000000 in
                                        * BOTH privilege levels -> no guards */
      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
      sh4g_const(tp, 0x0FFFFFFFu, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      if (is_imm)
        sh4g_const(tp, val & 0xF0000000u, SH4_REG_T1);
      else {
        sh4g_load_greg(tp, rm, SH4_REG_T1);
        sh4g_const(tp, 0xF0000000u, SH4_REG_T2);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_and(&cg, SH4_REG_T2, SH4_REG_T1);
          sh4g_close(tp, &cg); }
      }
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, SH4_GREG_CPSR);
        sh4g_close(tp, &cg); }
      return 1;
    }

    {                                  /* control byte written (pfield 1 / 3) */
      u32 mask = (pfield == 3) ? 0xF00000EFu : 0x000000EFu;
      u8 *to_slow[2], *to_store[3], *done, *skip_bank;
      int ns = 0, nst = 0, i;

      if (!cgba_sh4_psr_rebank_routine)  /* dynarec init not run yet */
        return 0;

      if (is_imm) sh4g_const(tp, val, SH4_REG_ARG3);     /* R7 = value */
      else        sh4g_load_greg(tp, rm, SH4_REG_ARG3);

      sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1); /* R5 = old CPSR */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, ~mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);       /* old & ~mask */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, mask, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_T2);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T2);       /* val & mask */
        sh4_emit_or(&cg, SH4_REG_T2, SH4_REG_T0);        /* T0 = new CPSR */
        sh4g_close(tp, &cg); }

      /* USER mode -> C (restricted mask). Privileged CPU_MODE values are
       * 0x10.. (PRIVMODE = mode >> 4); MODE_USER is 0x00. */
      sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_tst_imm(&cg, 0x10);                     /* T=1: USER */
        sh4g_close(tp, &cg); }
      to_slow[ns++] = sh4g_emit_bt_placeholder(tp);

      /* Thumb bit change -> C (execution-state switch mid-block) */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0x20);
        sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T=1: unchanged */
        sh4g_close(tp, &cg); }
      to_slow[ns++] = sh4g_emit_bf_placeholder(tp);

      /* IRQs disabled in the NEW value -> nothing can fire, store directly.
       * (Checked on the new I bit, not old^new: an already-pending IRQ with
       * I clear must be taken now, exactly like check_for_interrupts.) */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, 0x80);                     /* T=1: IRQs enabled */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bf_placeholder(tp);

      /* IRQs enabled: pending? IE & IF nonzero-ness is byte-order neutral. */
      sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x200), SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);    /* IE */
        sh4_emit_add_imm(&cg, 2, SH4_REG_T2);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG2);
        sh4_emit_extu_w(&cg, SH4_REG_ARG2, SH4_REG_ARG2);/* IF */
        sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_ARG2);     /* T=1: none pending */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 6, SH4_REG_T2);
        sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
        sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);  /* IME, true order */
        sh4_emit_tst_imm(&cg, 1);                        /* T=1: IME off */
        sh4g_close(tp, &cg); }
      to_store[nst++] = sh4g_emit_bt_placeholder(tp);

      /* fall-through: an IRQ will fire -> full C helper (vector + exit).
       * with_cycles MUST be 1: sh4_op2_pc_tramp unconditionally reads the
       * fifth (cycle_count) literal on its PC-change exit — a 4-literal site
       * makes it debit its own instruction bytes from the cycle counter,
       * which detonated exactly when gameplay IRQ traffic started. */
      for (i = 0; i < ns; i++)
        sh4g_patch_cond(to_slow[i], *tp);
      sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_tramp,
                          (const void *)cgba_sh4_arm_psr, opcode, pc,
                          1, cycles);
      done = sh4g_emit_bra_placeholder(tp);

      for (i = 0; i < nst; i++)
        sh4g_patch_cond(to_store[i], *tp);

      /* Committed — no more bails. Same CPSR mode nibble (same gpSP mode by
       * construction): plain CPSR store, keeping the hot I-bit-toggle
       * bracket inline. Nibble changed: JSR the resident set_cpu_mode
       * routine (sh4_fastmem.c buffer — outside the translation caches, so
       * the ~110-byte re-bank body costs no block bytes or I-cache reach;
       * inlining it here measured +59% imiss on AW). The routine handles
       * FIQ's 7-register bank and preserves R1 = merged CPSR. */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0xF);
        sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T=1: same mode */
        sh4g_close(tp, &cg); }
      skip_bank = sh4g_emit_bt_placeholder(tp);

      sh4g_const(tp, (u32)(uintptr_t)cgba_sh4_psr_rebank_routine, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_jsr(&cg, SH4_REG_RET);
        sh4_emit_nop(&cg);
        sh4g_close(tp, &cg); }

      sh4g_patch_cond(skip_bank, *tp);
      sh4g_store_greg(tp, SH4_REG_T0, SH4_GREG_CPSR);
      sh4g_patch_bra(done, *tp);
      return 1;
    }
  }
}

/* Thumb register-amount shifts (LSL/LSR/ASR/ROR Rd,Rs), the last fmt4 forms on
 * the C path — MP2K envelope/volume scaling hits these ~33K/session. Semantics
 * mirror cgba_sh4_thumb_shift_reg exactly: amount = reg[rs] & 0xFF;
 *   amount==0             -> result and C unchanged, N/Z from the value;
 *   1..31                 -> normal shift, C = last bit out;
 *   LSL/LSR ==32          -> result 0, C = bit0 / bit31;   >32 -> 0, C=0;
 *   ASR >=32              -> sign-fill, C = bit31;
 *   ROR amount&31==0      -> result unchanged, C = bit31; else rotate,
 *                            C = bit(a-1) of val = bit31 of the result.
 * SHLD/SHAD take signed amounts (negative = right by -n for n in 1..31), which
 * gives every carry recipe in one instruction. C work is skipped when the
 * liveness mask says C is dead (fm & 2 == 0). Layout:
 *   prologue; amount==0 -> zero:
 *   <kind body: {small | big} paths merging at join>
 *   join: store rd; pack NZC;  BRA end
 *   zero: pack NZ only (C architecturally preserved)
 *   end: */
static inline int sh4g_thumb_shift_reg_native(u8 **tp, u32 opcode, u32 pc,
                                              u32 flag_status)
{
  u32 sub = (opcode >> 6) & 0xF;     /* 0x2=LSL 0x3=LSR 0x4=ASR 0x7=ROR */
  unsigned rd = opcode & 7;
  unsigned rs = (opcode >> 3) & 7;
  u32 fm = flag_status & 0xF;
  int want_c = (fm & 0x2) != 0;
  const unsigned val = SH4_REG_T0;   /* R1: value, then result */
  const unsigned amt = SH4_REG_RET;  /* R0: amount (and_imm target) */
  const unsigned am32 = SH4_REG_T2;  /* R3: amount-32 */
  const unsigned rc  = SH4_REG_ARG0; /* R4: carry out (0/1) */
  const unsigned s1  = SH4_REG_ARG1; /* R5 scratch */
  const unsigned s2  = SH4_REG_ARG2; /* R6 scratch */
  u8 *zero_ph, *end_ph, *skip_ph = NULL;
  (void)pc;

  if (sub != 0x2 && sub != 0x3 && sub != 0x4 && sub != 0x7)
    return 0;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rd, val);
    sh4_emit_load_greg(&cg, rs, amt);
    sh4_emit_and_imm(&cg, 0xFF);                 /* R0 = amount */
    sh4_emit_tst(&cg, amt, amt);                 /* T = (amount == 0) */
    sh4g_close(tp, &cg); }
  zero_ph = sh4g_emit_bt_placeholder(tp);

  if (sub == 0x7) {                              /* ROR */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_and_imm(&cg, 0x1F);               /* R0 = a = amount & 31 */
      sh4_emit_tst(&cg, amt, amt);               /* T = (a == 0): mult of 32 */
      sh4g_close(tp, &cg); }
    { u8 *m32_ph = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);         /* a in 1..31: rotate */
        sh4_emit_mov_reg(&cg, val, rc);
        sh4_emit_neg(&cg, amt, s2);
        sh4_emit_shld(&cg, s2, rc);              /* rc = val >> a */
        sh4_emit_mov_imm(&cg, 32, s1);
        sh4_emit_sub(&cg, amt, s1);              /* s1 = 32 - a */
        sh4_emit_shld(&cg, s1, val);             /* val <<= (32 - a) */
        sh4_emit_or(&cg, rc, val);               /* result */
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(m32_ph, *tp); }            /* a==0: result unchanged */
    if (want_c) {                                /* both paths: C = result b31 */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, val, rc);
      sh4_emit_shll(&cg, rc);                    /* T = bit31 */
      sh4_emit_movt(&cg, rc);
      sh4g_close(tp, &cg);
    }
  } else {                                       /* LSL / LSR / ASR */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, amt, am32);
      sh4_emit_add_imm(&cg, -32, am32);          /* R3 = amount - 32 */
      sh4_emit_cmppz(&cg, am32);                 /* T = (amount >= 32) */
      sh4g_close(tp, &cg); }
    { u8 *big_ph = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);         /* amount in 1..31 */
        if (want_c) {
          if (sub == 0x2) {                      /* LSL: C = val>>(32-amt) & 1 */
            sh4_emit_mov_reg(&cg, val, rc);
            sh4_emit_shld(&cg, am32, rc);        /* am32 = -(32-amt) */
          } else {                               /* LSR/ASR: C = val>>(amt-1) & 1 */
            sh4_emit_mov_imm(&cg, 1, s1);
            sh4_emit_sub(&cg, amt, s1);          /* s1 = 1 - amt (<= 0) */
            sh4_emit_mov_reg(&cg, val, rc);
            sh4_emit_shld(&cg, s1, rc);
          }
          sh4_emit_mov_imm(&cg, 1, s1);
          sh4_emit_and(&cg, s1, rc);             /* rc = C */
        }
        if (sub == 0x2)
          sh4_emit_shld(&cg, amt, val);          /* result = val << amt */
        else {
          sh4_emit_neg(&cg, amt, s2);
          if (sub == 0x3) sh4_emit_shld(&cg, s2, val);   /* logical right */
          else            sh4_emit_shad(&cg, s2, val);   /* arithmetic right */
        }
        sh4g_close(tp, &cg); }
      skip_ph = sh4g_emit_bra_placeholder(tp);
      sh4g_patch_cond(big_ph, *tp); }
    { sh4_codegen cg = sh4g_open(tp);           /* amount >= 32 */
      if (sub == 0x4) {                          /* ASR: sign-fill, C = b31 */
        if (want_c) {
          sh4_emit_mov_reg(&cg, val, rc);
          sh4_emit_shll(&cg, rc);
          sh4_emit_movt(&cg, rc);
        }
        sh4_emit_mov_imm(&cg, -31, s2);
        sh4_emit_shad(&cg, s2, val);
      } else {                                   /* LSL/LSR: 0; C only at ==32 */
        if (want_c) {
          sh4_emit_tst(&cg, am32, am32);         /* T = (amount == 32) */
          sh4_emit_movt(&cg, s1);
          sh4_emit_mov_reg(&cg, val, rc);
          if (sub == 0x2) {                      /* LSL#32: C = bit0 */
            sh4_emit_mov_imm(&cg, 1, s2);
            sh4_emit_and(&cg, s2, rc);
          } else {                               /* LSR#32: C = bit31 */
            sh4_emit_shll(&cg, rc);
            sh4_emit_movt(&cg, rc);
          }
          sh4_emit_and(&cg, s1, rc);             /* zero unless amount==32 */
        }
        sh4_emit_mov_imm(&cg, 0, val);
      }
      sh4g_close(tp, &cg); }
    sh4g_patch_bra(skip_ph, *tp);                /* small path joins here */
  }

  sh4g_store_greg(tp, val, rd);                  /* result -> reg[rd] */
  sh4g_set_flags(tp, val, rc, rc,
                 sh4g_flags_round(fm & (want_c ? 0xE : 0xC)));
  end_ph = sh4g_emit_bra_placeholder(tp);

  sh4g_patch_cond(zero_ph, *tp);                 /* amount==0: N/Z only */
  sh4g_set_flags(tp, val, rc, rc, sh4g_flags_round(fm & 0xC));
  sh4g_patch_bra(end_ph, *tp);
  return 1;
}

enum {
  SH4_THUMB_LDK_W = 0,
  SH4_THUMB_LDK_B,
  SH4_THUMB_LDK_UH,
  SH4_THUMB_LDK_SH,
  SH4_THUMB_LDK_SB
};

static inline void sh4g_charge_mem_cell(u8 **tp, const u8 *cell)
{
  sh4g_const(tp, (uint32_t)(uintptr_t)cell, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
}

/* Fast path for the hot Thumb shape:
 *   LDR rB, [pc, #literal]   ; rB = fixed IO address
 *   LDRH/LDRB/LDR rD, [rB, #offset]
 *
 * The generic native load is still correct, but it must rebuild the effective
 * address class, probe memory_map_read[], check alignment and read the wait-state
 * table at runtime. If translation knows the effective address is fixed IO, the
 * host pointer is stable and the read can be emitted directly. Loads only: IO
 * writes remain helper-owned. */
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
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7, rb = (opcode >> 3) & 7;
  u32 ro = 0, offset = 0, address;
  int reg_offset = 0, kind = SH4_THUMB_LDK_W, align_mask = 3, is_load = 1;

  if (hi >= 0x50 && hi <= 0x5F) {
    u32 op = (opcode >> 10) & 3;
    ro = (opcode >> 6) & 7;
    reg_offset = 1;
    if (opcode & 0x0200) {
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_UH; align_mask = 1; is_load = 0; break;
      case 1: kind = SH4_THUMB_LDK_SB; align_mask = 0; break;
      case 2: kind = SH4_THUMB_LDK_UH; align_mask = 1; break;
      case 3: kind = SH4_THUMB_LDK_SH; align_mask = 1; break;
      }
    } else {
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_W; align_mask = 3; is_load = 0; break;
      case 1: kind = SH4_THUMB_LDK_B; align_mask = 0; is_load = 0; break;
      case 2: kind = SH4_THUMB_LDK_W; align_mask = 3; break;
      case 3: kind = SH4_THUMB_LDK_B; align_mask = 0; break;
      }
    }
  } else if (hi >= 0x60 && hi <= 0x7F) {
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    is_load = (opcode >> 11) & 1;
    offset = imm5 << (is_byte ? 0 : 2);
    kind = is_byte ? SH4_THUMB_LDK_B : SH4_THUMB_LDK_W;
    align_mask = is_byte ? 0 : 3;
  } else if (hi >= 0x80 && hi <= 0x8F) {
    is_load = (opcode >> 11) & 1;
    offset = ((opcode >> 6) & 0x1F) << 1;
    kind = SH4_THUMB_LDK_UH;
    align_mask = 1;
  } else if (hi >= 0x90 && hi <= 0x9F) {
    is_load = (opcode >> 11) & 1;
    rd = (opcode >> 8) & 7;
    rb = 13;
    offset = (opcode & 0xFF) << 2;
    kind = SH4_THUMB_LDK_W;
    align_mask = 3;
  } else {
    return 0;
  }

  if (!(const_mask & (1u << rb)))
    return 0;
  if (reg_offset && !(const_mask & (1u << ro)))
    return 0;

  address = const_val[rb] + offset + (reg_offset ? const_val[ro] : 0);
  if (((address >> 24) & 0x0F) != 0x04)
    return 0;
  if (align_mask && (address & (u32)align_mask))
    return 0;
  if (!is_load)
    return 0;

#if (defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)) && \
  CGBA_SH4_LDST_DETAIL_COUNTERS
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_native_thumb_const_io_count,
             SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T0, SH4_REG_T1);
    sh4_emit_add_imm(&cg, 1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
#endif

  sh4g_const(tp, (u32)(uintptr_t)(((u8 *)io_registers) + (address & 0x3FFu)),
             SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    switch (kind) {
    case SH4_THUMB_LDK_W:
      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
      sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_B:
      sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_UH:
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_SH:
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    default:
      sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
      break;
    }
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    sh4g_close(tp, &cg); }

  sh4g_charge_mem_cell(tp, &ws_cyc_nseq[0x04][kind == SH4_THUMB_LDK_W]);
  return 1;
#endif
}

static inline int sh4g_thumb_ldst_runtime_io_candidate(u32 opcode)
{
  return opcode == 0x6820u || opcode == 0x7820u || opcode == 0x8801u;
}

static inline u8 *sh4g_thumb_ldst_runtime_io_fast(u8 **tp, u32 opcode,
  int kind, unsigned rd, int align_mask)
{
  u8 *miss[2];
  int nmiss = 0;
  u8 *done;

  if (!sh4g_thumb_ldst_runtime_io_candidate(opcode))
    return NULL;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 4);                 /* T = GBA IO region */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

  if (align_mask) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);          /* T = aligned */
    sh4g_close(tp, &cg);
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

#if (defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)) && \
  CGBA_SH4_LDST_DETAIL_COUNTERS
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_native_thumb_runtime_io_count,
             SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_add_imm(&cg, 1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
#endif

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, 22, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -22, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET); /* R0 = address & 0x3ff */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)io_registers, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    switch (kind) {
    case SH4_THUMB_LDK_W:
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
      sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_B:
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_UH:
      sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    default:
      sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    }
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    sh4g_close(tp, &cg); }

  sh4g_charge_mem_cell(tp, &ws_cyc_nseq[0x04][kind == SH4_THUMB_LDK_W]);
  done = sh4g_emit_bra_placeholder(tp);
  for (int i = 0; i < nmiss; i++)
    sh4g_patch_cond(miss[i], *tp);
  return done;
}

/* Emit "branch to the Thumb memory slow path if (T == slow_if_t)" without the
 * BT/BF disp8 range limit. The short conditional skips over a far BRA. */
static inline u8 *sh4g_thumb_ldst_guard(u8 **tp, int slow_if_t)
{
  { sh4_codegen cg = sh4g_open(tp);
    if (slow_if_t) sh4_emit_bf(&cg, 1);       /* T==0 -> stay fast */
    else           sh4_emit_bt(&cg, 1);       /* T==1 -> stay fast */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bra_placeholder(tp);
}

/* Native Thumb mapped loads and plain RAM stores. Side-effecting/open regions
 * fall back to cgba_sh4_thumb_ldst so DMA/IRQ alerts, I/O semantics, ROM paging
 * and backup stay helper-owned. RAM stores check the SMC tag mirror first and
 * fall back if they would overwrite translated guest code. */
static inline int sh4g_thumb_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
#ifndef CGBA_SH4_THUMB_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  return 0;
#else
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7, rb = (opcode >> 3) & 7;
  u32 offset = 0, ro = 0, reg_offset = 0;
  int pc_relative = 0;
  int kind = SH4_THUMB_LDK_W, align_mask = 3, is_load = 1;
  u8 *runtime_io_done = NULL;

  if (hi >= 0x48 && hi <= 0x4F) {                    /* LDR Rd,[PC,#imm8*4] */
    rd = (opcode >> 8) & 7;
    offset = (opcode & 0xFF) << 2;
    pc_relative = 1;
    kind = SH4_THUMB_LDK_W;
    align_mask = 3;
  } else if (hi >= 0x50 && hi <= 0x5F) {             /* register-offset forms */
    u32 op = (opcode >> 10) & 3;
    ro = (opcode >> 6) & 7;
    reg_offset = 1;
    if (opcode & 0x0200) {                            /* format 8: H/S */
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_UH; align_mask = 1; is_load = 0; break; /* STRH */
      case 1: kind = SH4_THUMB_LDK_SB; align_mask = 0; break;  /* LDRSB */
      case 2: kind = SH4_THUMB_LDK_UH; align_mask = 1; break;  /* LDRH */
      case 3: kind = SH4_THUMB_LDK_SH; align_mask = 1; break;  /* LDRSH */
      }
    } else {                                          /* format 7: L/B */
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_W; align_mask = 3; is_load = 0; break;  /* STR */
      case 1: kind = SH4_THUMB_LDK_B; align_mask = 0; is_load = 0; break;  /* STRB */
      case 2: kind = SH4_THUMB_LDK_W; align_mask = 3; break;   /* LDR */
      case 3: kind = SH4_THUMB_LDK_B; align_mask = 0; break;   /* LDRB */
      }
    }
  } else if (hi >= 0x60 && hi <= 0x7F) {             /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    is_load = (opcode >> 11) & 1;
    offset = imm5 << (is_byte ? 0 : 2);
    kind = is_byte ? SH4_THUMB_LDK_B : SH4_THUMB_LDK_W;
    align_mask = is_byte ? 0 : 3;
  } else if (hi >= 0x80 && hi <= 0x8F) {             /* imm5 halfword */
    is_load = (opcode >> 11) & 1;
    offset = ((opcode >> 6) & 0x1F) << 1;
    kind = SH4_THUMB_LDK_UH;
    align_mask = 1;
  } else if (hi >= 0x90 && hi <= 0x9F) {             /* SP-relative word */
    is_load = (opcode >> 11) & 1;
    rd = (opcode >> 8) & 7;
    rb = 13;                                         /* REG_SP */
    offset = (opcode & 0xFF) << 2;
    kind = SH4_THUMB_LDK_W;
    align_mask = 3;
  } else {
    return 0;
  }

  if (pc_relative) {
    sh4g_const(tp, ((pc & ~2u) + 4u + offset), SH4_REG_T0);
  } else {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rb, SH4_REG_T0);       /* R1 = base */
      sh4g_close(tp, &cg); }
    if (reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, ro, SH4_REG_T1);
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = base + reg[ro] */
      sh4g_close(tp, &cg);
    } else if (offset) {
      sh4g_const(tp, offset, SH4_REG_T1);
      sh4g_add_reg(tp, SH4_REG_T1, SH4_REG_T0);      /* R1 = base + offset */
    }
  }

  if (is_load && !pc_relative && !reg_offset)
    runtime_io_done = sh4g_thumb_ldst_runtime_io_fast(tp, opcode, kind, rd,
                                                      align_mask);

  /* Out-of-line fast path (see sh4_fastmem.h): shared routine + 36-byte
   * site; guard failures fall to cgba_sh4_thumb_ldst via the tramp. */
  {
    int fm = is_load ? (int)kind
      : (kind == SH4_THUMB_LDK_W ? CGBA_FM_STORE_W
         : kind == SH4_THUMB_LDK_UH ? CGBA_FM_STORE_UH : CGBA_FM_STORE_B);
    sh4g_fastmem_site(tp, cgba_sh4_fastmem_routine[fm],
                      (const void *)cgba_sh4_thumb_ldst, (u32)opcode, (u32)pc,
                      cycle_count, rd, -1);
  }
  if (runtime_io_done)
    sh4g_patch_bra(runtime_io_done, *tp);
  return 1;
#endif
}

/* Set CPSR bit29 (C) to the constant c (0/1), preserving the rest. Used for the
 * ARM logical-with-immediate shifter carry, which is a translate-time constant. */
static inline void sh4g_set_c_const(u8 **tp, unsigned c)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned cpsr = SH4_REG_T1;            /* R2 */
  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, cpsr);
  sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
  sh4_emit_rotr(&cg, SH4_REG_RET);             /* R0 = 0x80000000 */
  sh4_emit_shlr2(&cg, SH4_REG_RET);            /* R0 = 0x20000000 (bit29) */
  if (c) {
    sh4_emit_or(&cg, SH4_REG_RET, cpsr);
  } else {
    sh4_emit_not(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_and(&cg, SH4_REG_RET, cpsr);
  }
  sh4_emit_store_greg(&cg, cpsr, SH4_GREG_CPSR);
  sh4g_close(tp, &cg);
}

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
static inline void sh4g_arm_shift_imm_op2(u8 **tp, u32 type, u32 amount, unsigned rm)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned op2 = SH4_REG_T1;                 /* R2 */
  sh4_emit_load_greg(&cg, rm, op2);
  if (type == 0) {                                 /* LSL #amount (amount 1..31) */
    sh4_emit_mov_imm(&cg, (int)amount, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, op2);
  } else if (type == 1) {                          /* LSR */
    if (amount == 0) {                             /* LSR #32 -> 0 */
      sh4_emit_mov_imm(&cg, 0, op2);
    } else {
      sh4_emit_mov_imm(&cg, -(int)amount, SH4_REG_RET);
      sh4_emit_shld(&cg, SH4_REG_RET, op2);        /* logical right */
    }
  } else if (type == 2) {                          /* ASR */
    sh4_emit_mov_imm(&cg, amount == 0 ? -32 : -(int)amount, SH4_REG_RET);
    sh4_emit_shad(&cg, SH4_REG_RET, op2);          /* arithmetic right (#32 = sign) */
  } else {                                         /* ROR #n (n = 1..31) */
    sh4_emit_mov_reg(&cg, op2, SH4_REG_T2);        /* R3 = v */
    sh4_emit_mov_imm(&cg, -(int)amount, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, op2);          /* T1 = v >> n */
    sh4_emit_mov_imm(&cg, (int)(32 - amount), SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T2);   /* R3 = v << (32-n) */
    sh4_emit_or(&cg, SH4_REG_T2, op2);             /* T1 = ROR(v, n) */
  }
  sh4g_close(tp, &cg);
}

/* Set CPSR bit29 (C) from bit0 of creg (a 0/1 value), preserving the rest. For
 * the logical-with-S shifted-register path where the shifter carry is runtime.
 * creg must survive sh4g_set_nz (it uses R0/R2/R3 only) — pass a high ARG reg. */
static inline void sh4g_set_c_reg(u8 **tp, unsigned creg)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned cpsr = SH4_REG_T1;                /* R2 */
  const unsigned tmp  = SH4_REG_T2;                /* R3 */
  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, cpsr);
  sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
  sh4_emit_rotr(&cg, SH4_REG_RET);                 /* R0 = 0x80000000 */
  sh4_emit_shlr2(&cg, SH4_REG_RET);                /* R0 = 0x20000000 (bit29) */
  sh4_emit_not(&cg, SH4_REG_RET, tmp);             /* tmp = ~bit29 */
  sh4_emit_and(&cg, tmp, cpsr);                    /* clear C */
  sh4_emit_mov_reg(&cg, creg, tmp);
  sh4_emit_mov_imm(&cg, 29, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, tmp);            /* tmp = carry << 29 */
  sh4_emit_or(&cg, tmp, cpsr);
  sh4_emit_store_greg(&cg, cpsr, SH4_GREG_CPSR);
  sh4g_close(tp, &cg);
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
static inline int sh4g_arm_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status)
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
        if (S || (op != 0xD && op != 0xF) || shift_rs == 15)
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
    if (!write && !fm)
      return 1;                                  /* CMP/CMN, flags dead: nop */
    if (rn == 15) {
      sh4g_const(tp, pc + 8, SH4_REG_T0);        /* rn = PC: constant */
    } else {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg);
    }
    if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else         sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
    sh4g_dp_addsub(tp, is_sub, rd, write, fm);
    return 1;
  }
  case 0x0: case 0x1: case 0xC: case 0xD:        /* AND/EOR/ORR/MOV */
  case 0xE: case 0xF: case 0x8: case 0x9: {      /* BIC/MVN/TST/TEQ */
    int dpop, write;
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
    if (!(dpop == SH4DP_MOV || dpop == SH4DP_MVN)) { /* MOV/MVN: second only */
      if (rn == 15) {
        sh4g_const(tp, pc + 8, SH4_REG_T0);      /* rn = PC: constant */
      } else {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
        sh4g_close(tp, &cg);
      }
    }
    if (reg_shifted) {
      sh4g_arm_shift_reg_op2_noflags(tp, shift_type, rm, shift_rs);
    } else if (shifted) {                        /* operand2 = shifted reg[rm] */
      if (fm & 0x2) sh4g_arm_shift_imm_full(tp, shift_type, shift_amount, rm, SH4_REG_ARG2);
      else          sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    } else {
      sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4g_dp_compute(&cg, dpop);
      if (write)
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg); }
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
    if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else         sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
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

#endif /* CGBA_SH4_THUMB_DP_EMIT_H */
