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

/* Split cohesive emitter headers (included here so callers keep one entry). */
#include "ports/fxcg100/sh4/sh4_thumb_ldst_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_bbra_emit.h"

#endif /* CGBA_SH4_THUMB_DP_EMIT_H */
