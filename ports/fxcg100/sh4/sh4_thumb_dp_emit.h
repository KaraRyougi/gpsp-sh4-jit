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
    int pin = sh4_pinned_reg(rd);
    if (pin >= 0 && !fm) {
      sh4g_const(tp, opcode & 0xFF, (unsigned)pin);
      return 1;
    }
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T0);
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    sh4g_set_nz_m(tp, SH4_REG_T0, fm);
    return 1;
  }

  /* fmt3 CMP/ADD/SUB Rd,#imm8  (001 op ddd iiiiiiii), op: 1 CMP, 2 ADD, 3 SUB. */
  if (hi >= 0x28 && hi <= 0x3F) {
    unsigned sub = (opcode >> 11) & 3;
    unsigned rd  = (opcode >> 8) & 7;
    u32 imm8     = opcode & 0xFF;
    if (sub == 1 && !fm)
      return 1;                                         /* CMP, flags dead: nop */
    if (!fm && sub >= 2 && imm8 <= 127u) {
      int simm = (sub == 2) ? (int)imm8 : -(int)imm8;
      int pin = sh4_pinned_reg(rd);
      sh4_codegen cg = sh4g_open(tp);
      if (pin >= 0) {
        if (simm != 0)
          sh4_emit_add_imm(&cg, simm, (unsigned)pin);
        sh4g_close(tp, &cg);
        return 1;
      }
      sh4_emit_load_greg(&cg, rd, SH4_REG_T0);
      if (simm != 0)
        sh4_emit_add_imm(&cg, simm, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg);
      return 1;
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rd, SH4_REG_T0);          /* a = reg[rd] */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, imm8, SH4_REG_T1);                   /* b = imm8 */
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
    if (!fm && is_imm) {
      int simm = is_sub ? -(int)arg : (int)arg;
      int pin_rd = sh4_pinned_reg(rd);
      int pin_rn = sh4_pinned_reg(rn);
      sh4_codegen cg = sh4g_open(tp);
      if (pin_rd >= 0) {
        if (rn != rd)
          sh4_emit_load_greg(&cg, rn, (unsigned)pin_rd);
        if (simm != 0)
          sh4_emit_add_imm(&cg, simm, (unsigned)pin_rd);
        sh4g_close(tp, &cg);
        return 1;
      }
      if (pin_rn >= 0 && simm == 0) {
        sh4_emit_store_greg(&cg, (unsigned)pin_rn, rd);
        sh4g_close(tp, &cg);
        return 1;
      }
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      if (simm != 0)
        sh4_emit_add_imm(&cg, simm, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg);
      return 1;
    }
#if CGBA_SH4_REG_FORWARD
    if (!is_imm && !is_sub && *tp == sh4_forward_at && arg == sh4_forward_guest && rn != arg) {
      unsigned tmp = rn; rn = arg; arg = tmp;
    }
#endif
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
 *   LDR rB, [pc, #literal]   ; rB = fixed IO / IWRAM / EWRAM address
 *   LDRH/LDRB/LDR rD, [rB, #offset]
 *
 * The generic native load is still correct, but it must call into the ILRAM
 * fastmem routine, probe region/alignment, and return via JMP. When translation
 * knows the effective address is aligned static IO/IWRAM/EWRAM, the host
 * pointer is permanent and the read emits directly with zero branches. Returns
 * the fixed non-sequential cycle debit (1 for IO/IWRAM, 3 or 6 for EWRAM) so
 * the caller folds it into block cycle_count without emitting an extra ADD. */
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
  u32 ro = 0, offset = 0, address, region;
  int reg_offset = 0, kind = SH4_THUMB_LDK_W, align_mask = 3, is_load = 1;
  uintptr_t host_addr;
  int debit;

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

  if (!is_load)
    return 0;
  if (!(const_mask & (1u << rb)))
    return 0;
  if (reg_offset && !(const_mask & (1u << ro)))
    return 0;

  address = const_val[rb] + offset + (reg_offset ? const_val[ro] : 0);
  if (align_mask && (address & (u32)align_mask))
    return 0;

  region = address >> 24;
  if (region == 0x03u) {
    host_addr = (uintptr_t)(iwram + 0x8000u + (address & 0x7FFFu));
    debit = 1;
  } else if (region == 0x02u) {
    host_addr = (uintptr_t)(ewram + (address & 0x3FFFFu));
    debit = (kind == SH4_THUMB_LDK_W) ? 6 : 3;
  } else if (region == 0x04u) {
    host_addr = (uintptr_t)(((u8 *)io_registers) + (address & 0x3FFu));
    debit = 1;
  } else {
    return 0;
  }

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

  sh4g_const(tp, (u32)host_addr, SH4_REG_T2);
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

  return debit;
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
      if (offset <= 127u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, (int)offset, SH4_REG_T0); /* R1 = base + offset */
        sh4g_close(tp, &cg);
      } else if (offset <= 254u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 127, SH4_REG_T0);
        sh4_emit_add_imm(&cg, (int)(offset - 127u), SH4_REG_T0);
        sh4g_close(tp, &cg);
      } else {
        sh4g_const(tp, offset, SH4_REG_T1);
        sh4g_add_reg(tp, SH4_REG_T1, SH4_REG_T0);      /* R1 = base + offset */
      }
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
    sh4g_fastmem_site(tp, fm,
                      (const void *)cgba_sh4_thumb_ldst, (u32)opcode, (u32)pc,
                      cycle_count, rd, -1);
  }
  if (runtime_io_done)
    sh4g_patch_bra(runtime_io_done, *tp);
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
    kind = SH4_THUMB_LDK_B; off = (op1 >> 6) & 31u; align = 0;
  } else if ((op1 & 0xF800u) == 0x8800u) {           /* LDRH rD, [rA, #imm5*2] */
    kind = SH4_THUMB_LDK_UH; off = ((op1 >> 6) & 31u) << 1; align = 1;
  } else if ((op1 & 0xF800u) == 0x6800u) {           /* LDR rD, [rA, #imm5*4] */
    kind = SH4_THUMB_LDK_W; off = ((op1 >> 6) & 31u) << 2; align = 3;
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
    debit = (kind == SH4_THUMB_LDK_W) ? 6 : 3;
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
    if (pin >= 0 && kind != SH4_THUMB_LDK_W)
      dst = (unsigned)pin;
    switch (kind) {
    case SH4_THUMB_LDK_B:
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, dst);
      sh4_emit_extu_b(&cg, dst, dst);
      break;
    case SH4_THUMB_LDK_UH:
      sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, dst);
      sh4_emit_swap_b(&cg, dst, dst);
      sh4_emit_extu_w(&cg, dst, dst);
      break;
    default: /* SH4_THUMB_LDK_W */
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T0);
      sh4_emit_swap_b(&cg, SH4_REG_T0, SH4_REG_ARG0);
      sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T0);
      break;
    }
    if (dst == SH4_REG_T0)
      sh4_emit_store_greg(&cg, SH4_REG_T0, rD);
    sh4g_close(tp, &cg); }
  bra_done = *tp;
  sh4g_u16(tp, 0xA000);                              /* BRA done */
  sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-debit) & 0xFF)));

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

/* Fuse a run of 2..8 consecutive Thumb SP-relative word loads/stores
 * (`LDR/STR Rd, [SP, #imm8*4]`, 0x9000..0x9FFF) with max offset <= 60 bytes.
 * Validates canonical aligned IWRAM SP + straddle + store SMC tags ONCE,
 * then runs all loads/stores inline (5 insns each vs ~28 insns + JSR/JMP). */
static inline int sh4g_thumb_sp_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  u8 is_load[8], rd[8], woff[8];
  u32 max_off = 0u, store_wmask = 0u;
  int has_store = 0, k;
  u8 *miss[3], *bra_done;
  int nmiss = 0;

  if (cnt < 2 || cnt > 8) return 0;
  for (k = 0; k < cnt; k++) {
    u32 op = ops[k];
    u32 off;
    if ((op & 0xF000u) != 0x9000u) return 0;
    off = (op & 0xFFu) << 2;
    if (off > 60u) return 0;
    is_load[k] = (u8)((op >> 11) & 1u);
    rd[k] = (u8)((op >> 8) & 7u);
    woff[k] = (u8)(off >> 2);
    if (off > max_off) max_off = off;
    if (!is_load[k]) {
      has_store = 1;
      store_wmask |= (1u << woff[k]);
    }
  }

  sh4g_litseg_reserve_bytes(tp, 5, (u32)(72 + cnt * 16));

  /* Guard 1: SP in canonical IWRAM [0x03000000, 0x03007FFF - max_off - 3] & aligned. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, 13, SH4_REG_T0);          /* R1 = reg[SP] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);     /* R0 = SP - 0x03000000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x8000u - (max_off + 3u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);     /* out of bounds? */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, 3);                         /* word aligned? */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

  /* Guard 2: if any store, verify all destination IWRAM SMC tags are 0. */
  if (has_store) {
    int first = 1, w;
    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2); /* R3 = iwram + sp_off */
      for (w = 0; w < 16; w++) {
        if (!(store_wmask & (1u << w))) continue;
        if (first) {
          if (w == 0) sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
          else        sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_ARG0, (unsigned)w);
          first = 0;
        } else {
          sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, (unsigned)w);
          sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
        }
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);  /* T = all tags clear */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Hot body: zero guards, zero JSR/RTS, 5 insns per word load/store. */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);   /* R3 = iwram_data + sp_off */
    for (k = 0; k < cnt; k++) {
      unsigned w = woff[k];
      if (is_load[k]) {
        if (w == 0u) sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T0);
        else         sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T0, w);
        sh4_emit_swap_b(&cg, SH4_REG_T0, SH4_REG_T1);
        sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T0);
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd[k]);
      } else {
        sh4_emit_load_greg(&cg, rd[k], SH4_REG_T0);
        sh4_emit_swap_b(&cg, SH4_REG_T0, SH4_REG_T1);
        sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        if (w == 0u) sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
        else         sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, w);
      }
    }
    sh4g_close(tp, &cg); }
  bra_done = *tp;
  sh4g_u16(tp, 0xA000);                               /* BRA done */
  sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-cnt) & 0xFF)));

  /* Cold fallback: exact normal emission (no guest state mutated on miss). */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  for (k = 0; k < cnt; k++) {
    sh4_greg_forward_reset();
    sh4g_thumb_ldst_native(tp, ops[k], pc + (u32)(k * 2), ccs[k]);
  }
  sh4g_patch_bra(bra_done, *tp);
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

  sh4g_litseg_reserve_bytes(tp, 5, (u32)(96 + npairs * 16));

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

  /* Guard 2: verify every destination IWRAM halfword SMC tag is 0. */
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
  bra_done = *tp;
  sh4g_u16(tp, 0xA000);                               /* BRA done */
  sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-npairs) & 0xFF)));

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
#endif

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
      if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
      else         sh4g_arm_load_op2(tp, is_imm, op2, rm, pc);
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

/* General ARM IWRAM same-Rn same-shift DP run (2..8 ops, non-S, rd!=rn):
 * caches reg[rn] in R3 and shift amount in R0 across the run (-2 insns and
 * -1 greg load per op, e.g. audio packing `8x AND rX, r12, rX, LSR #7`). */
static inline int sh4g_arm_dp_same_rn_run_native(u8 **tp, const u32 *ops, int cnt)
{
  u32 op0 = ops[0];
  u32 alu = (op0 >> 21) & 0xFu;          /* 0x0=AND, 0x1=EOR, 0x4=ADD, 0xC=ORR */
  unsigned rn = (op0 >> 16) & 0xFu;
  unsigned sh_type = (op0 >> 5) & 3u;    /* 0=LSL, 1=LSR, 2=ASR */
  unsigned sh_imm = (op0 >> 7) & 0x1Fu;
  int i;

  if (cnt < 2 || cnt > 8 || rn == 15u)
    return 0;
  if ((op0 & 0xFE100010u) != 0xE0000000u)
    return 0;
  if (alu != 0x0u && alu != 0x1u && alu != 0x4u && alu != 0xCu)
    return 0;
  if (sh_type > 2u || (sh_type != 0u && sh_imm == 0u))
    return 0;

  for (i = 0; i < cnt; i++) {
    u32 op = ops[i];
    unsigned rd = (op >> 12) & 0xFu;
    unsigned rm = op & 0xFu;
    if ((op & 0xFFFF0FF0u) != (op0 & 0xFFFF0FF0u) ||
        rd == 15u || rm == 15u || rd == rn)
      return 0;
  }

  sh4g_litseg_reserve_bytes(tp, 0, (u32)(8 + cnt * 8));
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T2);         /* R3 = reg[rn] */
    if (sh_imm != 0u)
      sh4_emit_mov_imm(&cg, (sh_type == 0u) ? (int)sh_imm : -(int)sh_imm, SH4_REG_RET);
    for (i = 0; i < cnt; i++) {
      unsigned rd = (ops[i] >> 12) & 0xFu;
      unsigned rm = ops[i] & 0xFu;
      sh4_emit_load_greg(&cg, rm, SH4_REG_T0);       /* R1 = reg[rm] */
      if (sh_imm != 0u) {
        if (sh_type == 2u)
          sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
        else
          sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T0);
      }
      if (alu == 0x0u)      sh4_emit_and(&cg, SH4_REG_T2, SH4_REG_T0);
      else if (alu == 0x1u) sh4_emit_xor(&cg, SH4_REG_T2, SH4_REG_T0);
      else if (alu == 0x4u) sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_T0);
      else                  sh4_emit_or(&cg, SH4_REG_T2, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
    }
    sh4g_close(tp, &cg); }
  return 1;
}

/* General ARM same-Rd accumulator DP run (2..8 ops, non-S, rd==rn==accum,
 * rm!=accum, register op2 with optional immediate LSL/LSR/ASR): keeps reg[rd]
 * live in R1 across the entire run, saving 2 greg memops per op (e.g.
 * `3x ADD r6, r6, rX` or `3x ORR r4, r4, rX, LSL #8/#16/#24`). */
static inline int sh4g_arm_dp_same_rd_run_native(u8 **tp, const u32 *ops, int cnt)
{
  unsigned rd = (ops[0] >> 12) & 0xFu;
  int i;

  if (cnt < 2 || cnt > 8 || rd == 15u)
    return 0;

  for (i = 0; i < cnt; i++) {
    u32 op = ops[i];
    u32 alu = (op >> 21) & 0xFu;
    unsigned rn = (op >> 16) & 0xFu;
    unsigned rm = op & 0xFu;
    unsigned sh_type = (op >> 5) & 3u;
    unsigned sh_imm = (op >> 7) & 0x1Fu;
    if ((op & 0xFE100010u) != 0xE0000000u) return 0;
    if (alu != 0x0u && alu != 0x1u && alu != 0x2u && alu != 0x4u && alu != 0xCu)
      return 0;
    if (((op >> 12) & 0xFu) != rd || rn != rd || rm == 15u || rm == rd)
      return 0;
    if (sh_type > 2u || (sh_type != 0u && sh_imm == 0u))
      return 0;
  }

  sh4g_litseg_reserve_bytes(tp, 0, (u32)(8 + cnt * 12));
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rd, SH4_REG_T0);          /* R1 = accum */
    sh4g_close(tp, &cg); }
  for (i = 0; i < cnt; i++) {
    u32 op = ops[i];
    u32 alu = (op >> 21) & 0xFu;
    unsigned rm = op & 0xFu;
    unsigned sh_type = (op >> 5) & 3u;
    unsigned sh_imm = (op >> 7) & 0x1Fu;
    if (sh_imm != 0u) {
      sh4g_arm_shift_imm_op2(tp, sh_type, sh_imm, rm); /* R2 = op2, R1 intact */
    } else {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
      sh4g_close(tp, &cg);
    }
    { sh4_codegen cg = sh4g_open(tp);
      if (alu == 0x0u)      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_T0);
      else if (alu == 0x1u) sh4_emit_xor(&cg, SH4_REG_T1, SH4_REG_T0);
      else if (alu == 0x2u) sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T0);
      else if (alu == 0x4u) sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
      else                  sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);
      sh4g_close(tp, &cg); }
  }
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

static inline u8 *sh4g_emit_fused_cmp_bcc_ex(
  u8 **tp, int kind, unsigned rd, unsigned cond,
  int taken_debit, uint32_t target_pc, int is_internal,
  const void *block_exit_fn, int taken_dead, int fall_dead, int extra)
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
  sh4g_cycle_debit_r0(tp, taken_debit);
  if (taken_dead) {
    u8 *bt, *bt2 = NULL;
    { sh4_codegen cg = sh4g_open(tp);
      if (taken_dead == 1) {
        sh4_emit_cmppl(&cg, SH4_REG_CYCLES);             /* T = (cycles > 0) */
      } else {
        /* External ROM lookahead (step 1..5): require cycles > 96 so neither a
         * mid-block gate nor cold-interp chunk can stop before the killer. */
        sh4_emit_mov_imm(&cg, 96, SH4_REG_RET);
        sh4_emit_cmpgt(&cg, SH4_REG_RET, SH4_REG_CYCLES); /* T = (cycles > 96) */
      }
      sh4g_close(tp, &cg); }
    bt = sh4g_emit_bt_placeholder(tp);                   /* 98%+: hot jump, skip CPSR */
    sh4g_fused_mat_flags(tp, kind, extra);               /* cold: exact CPSR */
    if (taken_dead != 1) {
      /* 0 < cycles <= 96: CPSR is now exact; still jump to target without
       * calling update_gba prematurely. */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmppl(&cg, SH4_REG_CYCLES);               /* T = (cycles > 0) */
      sh4g_close(tp, &cg);
      bt2 = sh4g_emit_bt_placeholder(tp);
    }
    sh4g_const(tp, target_pc, SH4_REG_ARG0);
    sh4g_block_exit_jmp(tp);
    sh4g_patch_cond(bt, *tp);
    if (bt2)
      sh4g_patch_cond(bt2, *tp);
    if (is_internal) {
      site = sh4g_emit_patch_jump(tp);                   /* patched -> BRA body_offset */
    } else {
      sh4g_const(tp, target_pc, SH4_REG_ARG0);           /* R4 = target_pc (lazy link) */
#if CGBA_SH4_LAZY_CHAIN
      site = sh4g_emit_patch_call(tp);
#else
      site = sh4g_emit_patch_jump(tp);
#endif
      sh4g_patch_jump(site, block_exit_fn);
    }
  } else {
    sh4g_fused_mat_flags(tp, kind, extra);
    site = is_internal ? sh4g_branch_exit_internal(tp, target_pc)
                       : sh4g_branch_exit(tp, target_pc, block_exit_fn);
  }

  /* --- NOT-TAKEN fallthrough (R1, R2 still intact!) --- */
  sh4g_patch_cond(skip, *tp);
  if (!fall_dead)
    sh4g_fused_mat_flags(tp, kind, extra);

  return site;
}

static inline u8 *sh4g_emit_fused_cmp_bcc(
  u8 **tp, int kind, unsigned rd, unsigned cond,
  int taken_debit, uint32_t target_pc, int is_internal,
  const void *block_exit_fn, int taken_dead, int fall_dead)
{
  return sh4g_emit_fused_cmp_bcc_ex(
    tp, kind, rd, cond, taken_debit, target_pc, is_internal,
    block_exit_fn, taken_dead, fall_dead, 0);
}

/* ---- 4-slot intra-segment BBRA for straight-line dead-flag Thumb ALU runs ---
 * Allocates the top 4 hot unpinned guest registers (r2..r14, refs >= 2) into
 * SH-4 R4..R7 across a straight-line run of 2..16 dead-flag Thumb ALU ops,
 * keeping r0 in R11 and r1 in R12. Eliminates intermediate reg[] loads/stores
 * and SH7305 2-cycle Load-Use stalls; flushes only dirty slots at run end so
 * no dirty state ever escapes across branch/memory/helper boundaries. */
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

static inline int sh4g_thumb_alu_run_native(u8 **tp, const u16 *ops, int len,
                                            u32 *written_mask_out)
{
  static const unsigned slot_reg[4] = {
    SH4_REG_ARG0, SH4_REG_ARG1, SH4_REG_ARG2, SH4_REG_ARG3
  };
  u8 refs[16];
  u8 writes[16];
  s8 gslot[16];
  unsigned slot_guest[4];
  int nslots = 0, i, s;
  u32 slot_loaded = 0u, slot_dirty = 0u, wmask_all = 0u;
  u8 *start_tp;

  if (len < 2 || len > 16)
    return 0;
  memset(refs, 0, sizeof refs);
  memset(writes, 0, sizeof writes);
  for (i = 0; i < 16; i++)
    gslot[i] = -1;

  for (i = 0; i < len; i++) {
    u32 rm, wm, g;
    if (!sh4g_thumb_bbra_classify(ops[i], &rm, &wm))
      return 0;
    wmask_all |= wm;
    for (g = 0; g < 15u; g++) {
      if (rm & (1u << g)) refs[g]++;
      if (wm & (1u << g)) { refs[g]++; writes[g]++; }
    }
  }

  /* Pick up to 4 hottest unpinned guest regs with refs >= 2. */
  for (s = 0; s < 4; s++) {
    int best = -1;
    unsigned best_score = 1u;                 /* require refs >= 2 */
    for (i = 0; i < 15; i++) {
      if (sh4_pinned_reg((unsigned)i) >= 0 || gslot[i] >= 0)
        continue;
      if (refs[i] > best_score ||
          (refs[i] == best_score && best >= 0 && writes[i] > writes[best])) {
        best = i;
        best_score = refs[i];
      }
    }
    if (best < 0)
      break;
    gslot[best] = (s8)nslots;
    slot_guest[nslots] = (unsigned)best;
    nslots++;
  }
  if (nslots == 0)
    return 0;                                 /* no unpinned reuse: nothing to save */

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

#endif /* CGBA_SH4_THUMB_DP_EMIT_H */
