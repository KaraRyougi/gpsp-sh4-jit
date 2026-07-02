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
 * Register-specified shifts and hi-reg-PC forms stay on the C path for now.
 *
 * Correctness gate: the single-block lockstep and the frame diff both compare
 * reg[REG_CPSR], so C and V must be exact, not just N/Z.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

extern u8 *memory_map_read[];
extern u16 io_registers[512];
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
static inline int sh4g_thumb_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 fm = flag_status & 0xF;
  (void)pc;

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

  /* fmt5 hi-reg ADD/CMP/MOV. rs==15 reads the translate-time constant PC+4.
   * rd==15 MOV — the `mov pc, lr` return idiom, hot in BX-less BIOS-era code
   * such as the MP2K sound engine — exits through the same contract the C
   * helper uses (R4 = new PC, R1 = 1: pure PC change -> sh4_pc_redispatch).
   * ADD pc and MOV pc,pc stay on the C path. */
  if (hi >= 0x44 && hi <= 0x46) {
    unsigned op = (opcode >> 8) & 3;                     /* 0 ADD, 1 CMP, 2 MOV */
    unsigned rd = (opcode & 7) | ((opcode >> 4) & 8);
    unsigned rs = (opcode >> 3) & 0xF;
    if (rd == 15) {
      if (op != 2 || rs == 15)
        return 0;
      sh4g_load_greg(tp, rs, SH4_REG_RET);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, -2, SH4_REG_T1);
        sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);      /* new PC = rs & ~1 */
        sh4_emit_store_greg(&cg, SH4_REG_RET, SH4_GREG_PC);
        sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0);/* R4 = new PC */
        sh4_emit_mov_imm(&cg, 1, SH4_REG_T0);            /* R1 = pure PC change */
        sh4g_close(tp, &cg); }
      sh4g_far_jmp(tp, (const void *)sh4_helper_exit);
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

/* Native MRS/MSR (CPSR forms). MRS is a plain register copy: reg[REG_CPSR]
 * carries the full canonical flag set in this port (set_nzcv / the helpers
 * write CPSR bits directly), so no flag collapse pass is needed. MSR merges
 * under gpSP's privileged field mask with three runtime guards to the C
 * helper: USER mode (different mask + restricted control byte), a mode or
 * Thumb bit change (needs set_cpu_mode re-banking), and an IRQ-enabled
 * result with an interrupt actually pending (needs the vector + redispatch).
 * The hot MP2K bracket — MSR cpsr_c toggling only the I bit with no IRQ due —
 * stays fully native. SPSR forms and MSR with a PC operand stay on C. */
static inline int sh4g_arm_psr_native(u8 **tp, u32 opcode, u32 pc)
{
  u32 is_msr   = (opcode >> 21) & 1;
  u32 use_spsr = (opcode >> 22) & 1;
  if (use_spsr)
    return 0;

  if (!is_msr) {                                         /* MRS rd, cpsr */
    u32 rd = (opcode >> 12) & 0xF;
    if (rd == 15)
      return 0;
    sh4g_load_greg(tp, SH4_GREG_CPSR, SH4_REG_T0);
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
      u8 *to_slow[2], *to_store[3], *done;
      int ns = 0, nst = 0, i;

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

      /* mode or Thumb bit changes -> C (set_cpu_mode re-banking) */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0x3F);
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

      /* fall-through: an IRQ will fire -> full C helper (vector + exit) */
      for (i = 0; i < ns; i++)
        sh4g_patch_cond(to_slow[i], *tp);
      sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_tramp,
                          (const void *)cgba_sh4_arm_psr, opcode, pc, 0, 0);
      done = sh4g_emit_bra_placeholder(tp);

      for (i = 0; i < nst; i++)
        sh4g_patch_cond(to_store[i], *tp);
      sh4g_store_greg(tp, SH4_REG_T0, SH4_GREG_CPSR);
      sh4g_patch_bra(done, *tp);
      return 1;
    }
  }
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

  if (!is_load)
    return 0;
  if (!(const_mask & (1u << rb)))
    return 0;
  if (reg_offset && !(const_mask & (1u << ro)))
    return 0;

  address = const_val[rb] + offset + (reg_offset ? const_val[ro] : 0);
  if (((address >> 24) & 0x0F) != 0x04)
    return 0;
  if (align_mask && (address & (u32)align_mask))
    return 0;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
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

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
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
  u8 *guards[8]; int ng = 0;
  u8 *bra_done, *runtime_io_done = NULL;

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

  /* memory_map_read[] only covers the GBA 0x00000000..0x0fffffff space. */
  sh4g_const(tp, 0x10000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);   /* T = addr >= 0x10000000 */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_ldst_guard(tp, 1);     /* out of map -> slow */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);   /* R0 = addr >> 15 */
    sh4_emit_shll2(&cg, SH4_REG_RET);              /* R0 = index * 4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* T = (page == NULL) */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_ldst_guard(tp, 1);     /* unmapped -> slow */
  if (is_load) {
    /* Fast loads are safe for mapped RAM/I/O/video/gamepak memory. gpSP models
     * I/O reads as raw io_registers[] loads; writes stay helper-owned below.
     * Exclude BIOS/open (0/1) and backup/EEPROM (13..15). */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);              /* R0 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = region >= 2 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 0);     /* BIOS/open -> slow */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 13, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = backup/EEPROM */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 1);     /* backup -> slow */
  } else {
    /* Fast stores: plain RAM (0x02/0x03, SMC tag-checked below) or VRAM
     * word/half (plain; mirroring in the read map; no region-6 side effects).
     * Byte stores to VRAM duplicate to the halfword -> C helper. */
    u8 *vram_ok = NULL;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);              /* R0 = addr >> 24 */
      sh4g_close(tp, &cg); }
    if (kind != SH4_THUMB_LDK_B) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 6);                  /* T = VRAM */
        sh4g_close(tp, &cg); }
      vram_ok = sh4g_emit_bt_placeholder(tp);
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_shlr(&cg, SH4_REG_RET);               /* R0 = addr >> 25 */
      sh4_emit_cmpeq_imm(&cg, 1);                    /* regions 2 or 3 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 0);
    if (vram_ok)
      sh4g_patch_cond(vram_ok, *tp);
  }
  if (align_mask) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);             /* T = aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 0);   /* misaligned -> slow */
  }

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET); /* R0 = addr & 0x7FFF */
    sh4_emit_shll16(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  if (align_mask) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_mov_imm(&cg, align_mask, SH4_REG_ARG0);
    sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_T1);   /* T = host ptr aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 0);   /* unaligned NOR/RAM ptr */
  }

  if (!is_load) {
    u8 *bf_iwram, *bra_tag_ready, *vram_skip;
    /* VRAM has no SMC tag mirror (region 6 is never translated code). */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_shlr16(&cg, SH4_REG_T1);
      sh4_emit_shlr8(&cg, SH4_REG_T1);                  /* R2 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 6, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = VRAM */
      sh4g_close(tp, &cg); }
    vram_skip = sh4g_emit_bt_placeholder(tp);
    /* Build SMC tag page in R5:
     *   EWRAM: data page + 0x40000, IWRAM: data page - 0x8000. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG1);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_shlr16(&cg, SH4_REG_T1);
      sh4_emit_shlr8(&cg, SH4_REG_T1);                  /* R2 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = EWRAM */
      sh4g_close(tp, &cg); }
    bf_iwram = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, 0x40000u, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG1);
    bra_tag_ready = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(bf_iwram, *tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG1);
    sh4g_patch_bra(bra_tag_ready, *tp);

    { sh4_codegen cg = sh4g_open(tp);
      switch (kind) {
      case SH4_THUMB_LDK_W:
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
        break;
      case SH4_THUMB_LDK_UH:
        sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
        break;
      default:
        sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
        break;
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = tag == 0 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_thumb_ldst_guard(tp, 0);         /* SMC -> slow */
    sh4g_patch_cond(vram_skip, *tp);                     /* VRAM: no tags */
  }

  { sh4_codegen cg = sh4g_open(tp);
    if (is_load) {
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
    case SH4_THUMB_LDK_SH:
      sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    default:                                        /* LDRSB: mov.b sign-extends */
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      break;
      }
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    } else {
      sh4_emit_load_greg(&cg, rd, SH4_REG_T1);
      switch (kind) {
      case SH4_THUMB_LDK_W:
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
        sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      case SH4_THUMB_LDK_UH:
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      default:
        sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        break;
      }
    }
    sh4g_close(tp, &cg); }
  /* Charge the single access (nonseq; word column only for word transfer). */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0,
    /*is_word=*/(kind == SH4_THUMB_LDK_W), 1);
  bra_done = sh4g_emit_bra_placeholder(tp);

  { int gi; for (gi = 0; gi < ng; gi++) sh4g_patch_bra(guards[gi], *tp); }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                      (const void *)cgba_sh4_thumb_ldst, (u32)opcode, (u32)pc,
                      1, cycle_count);

  sh4g_patch_bra(bra_done, *tp);
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

/* Load ARM operand2 into R2: an immediate constant, or a plain register. */
static inline void sh4g_arm_load_op2(u8 **tp, int is_imm, u32 op2, unsigned rm)
{
  if (is_imm) {
    sh4g_const(tp, op2, SH4_REG_T1);
  } else {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
    sh4g_close(tp, &cg);
  }
}

/* ARM operand2 = reg[rm] shifted by an IMMEDIATE amount, into R2. type: 0=LSL
 * 1=LSR 2=ASR (NOT ROR). `amount` is the 5-bit field; in the shifted path LSL is
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
  } else {                                         /* ASR (type 2) */
    sh4_emit_mov_imm(&cg, amount == 0 ? -32 : -(int)amount, SH4_REG_RET);
    sh4_emit_shad(&cg, SH4_REG_RET, op2);          /* arithmetic right (#32 = sign) */
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

/* Native SH4 for ARM data-processing (the dominant class on ARM-heavy games).
 * Handles the two operand forms where the shifter carry is trivial: IMMEDIATE
 * (operand2 + carry are translate-time constants) and plain REGISTER with no
 * shift (operand2 = Rm, shifter C = old C, preserved). Arithmetic reuses
 * sh4g_dp_addsub (C/V from the ALU); logical sets N/Z, leaves V, and sets C only
 * for the rotated-immediate case. Shifted-register forms, register-specified
 * shifts and PC operands stay on the C path. Flags gated on the S bit AND the
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
  u32 fm;
  int c_const = -1;                              /* -1 = preserve C; 0/1 = set const */
  (void)pc;
  fm = S ? (flag_status & 0xF) : 0;

  if (rd == 15 || rn == 15) return 0;            /* PC operands -> C path */
  if (is_imm) {
    u32 imm = opcode & 0xFF;
    u32 rot = ((opcode >> 8) & 0xF) * 2;
    op2 = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    if (rot) c_const = (int)((op2 >> 31) & 1);   /* else shifter C = old C */
  } else {
    rm = opcode & 0xF;
    if (rm == 15) return 0;                       /* Rm = PC -> C path */
    if (opcode & 0x0FF0) {                        /* shifted register */
      shift_type = (opcode >> 5) & 3;
      if ((opcode >> 4) & 1)     return 0;        /* register-specified shift -> C */
      if (shift_type == 3)       return 0;        /* ROR/RRX -> C (later) */
      shift_amount = (opcode >> 7) & 0x1F;
      shifted = 1;                                /* immediate LSL/LSR/ASR */
    }                                             /* else operand2 = reg[Rm], LSL #0 */
  }

  switch (op) {
  case 0x2: case 0x4: case 0xA: case 0xB: {      /* SUB / ADD / CMP / CMN */
    int is_sub = (op == 0x2 || op == 0xA);
    int write  = (op == 0x2 || op == 0x4);
    if (!write && !fm)
      return 1;                                  /* CMP/CMN, flags dead: nop */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else         sh4g_arm_load_op2(tp, is_imm, op2, rm);
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
    { sh4_codegen cg = sh4g_open(tp);
      if (!(dpop == SH4DP_MOV || dpop == SH4DP_MVN))  /* MOV/MVN: second only */
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    if (shifted) {                               /* operand2 = shifted reg[rm] */
      if (fm & 0x2) sh4g_arm_shift_imm_full(tp, shift_type, shift_amount, rm, SH4_REG_ARG2);
      else          sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    } else {
      sh4g_arm_load_op2(tp, is_imm, op2, rm);
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4g_dp_compute(&cg, dpop);
      if (write)
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg); }
    if (fm) {                                    /* N/Z; V kept; C from shifter */
      sh4g_set_nz_m(tp, SH4_REG_T0, fm);
      if (fm & 0x2) {
        if (shifted)           sh4g_set_c_reg(tp, SH4_REG_ARG2); /* runtime C */
        else if (c_const >= 0) sh4g_set_c_const(tp, (unsigned)c_const);
      }
    }
    return 1;
  }
  case 0x3: case 0x5: case 0x6: case 0x7: {      /* RSB / ADC / SBC / RSC */
    int reverse = (op == 0x3 || op == 0x7);       /* result = op2 - rn */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else         sh4g_arm_load_op2(tp, is_imm, op2, rm);
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
