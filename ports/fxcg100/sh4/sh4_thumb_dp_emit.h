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

extern u8 *memory_map_read[];
int cgba_sh4_thumb_ldst(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);

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

/* ADC/SBC/RSC: carry-in add/subtract of (a in R1, b in R2), result in R1. The
 * shifter carry is irrelevant here (C comes from the ALU). Carry-in T = old C
 * for ADC, !old C for SBC/RSC (ARM borrow = ~C). C-out = T for ADC, !T for the
 * subtract (ARM C = !borrow). V uses the bit formula from the saved operands:
 *   add:  V = (~(a^b) & (a^res)) >> 31     sub: V = ((a^b) & (a^res)) >> 31. */
static inline void sh4g_dp_carry(u8 **tp, int is_sub, unsigned rd,
                                 int write_result, int set_flags)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned a  = SH4_REG_T0;    /* R1 = first / result */
  const unsigned b  = SH4_REG_T1;    /* R2 = second (preserved by addc/subc) */
  const unsigned a0 = SH4_REG_ARG2;  /* R6 = saved original a (for V) */
  const unsigned rc = SH4_REG_ARG0;  /* R4 = C (0/1) */
  const unsigned rv = SH4_REG_ARG1;  /* R5 = V (0/1) */
  const unsigned t  = SH4_REG_T2;    /* R3 scratch */

  if (set_flags) sh4_emit_mov_reg(&cg, a, a0);
  if (!is_sub) {                                  /* ADC: T = old C, then a+b+C */
    sh4_emit_load_greg(&cg, SH4_GREG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);              /* T = CPSR bit29 = old C */
    sh4_emit_addc(&cg, b, a);                     /* a = a + b + C, T = C-out */
    if (set_flags) sh4_emit_movt(&cg, rc);
  } else {                                        /* SBC/RSC: T = !old C, then a-b-!C */
    sh4_emit_load_greg(&cg, SH4_GREG_CPSR, t);
    sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
    sh4_emit_rotr(&cg, SH4_REG_RET);
    sh4_emit_shlr2(&cg, SH4_REG_RET);             /* R0 = 0x20000000 (C bit) */
    sh4_emit_tst(&cg, SH4_REG_RET, t);            /* T = (old C == 0) = !C */
    sh4_emit_subc(&cg, b, a);                     /* a = a - b - !C, T = borrow */
    if (set_flags) {                              /* ARM C = !borrow */
      sh4_emit_movt(&cg, rc);
      sh4_emit_not(&cg, rc, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 1);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, rc);
    }
  }
  if (set_flags) {                                /* V from saved a0, b, res */
    sh4_emit_mov_reg(&cg, a0, t); sh4_emit_xor(&cg, b, t);     /* t = a0 ^ b */
    if (!is_sub) sh4_emit_not(&cg, t, t);                       /* add: ~(a0^b) */
    sh4_emit_mov_reg(&cg, a0, SH4_REG_RET); sh4_emit_xor(&cg, a, SH4_REG_RET); /* R0 = a0^res */
    sh4_emit_and(&cg, SH4_REG_RET, t);
    sh4_emit_shll(&cg, t);                         /* T = bit31 of t */
    sh4_emit_movt(&cg, rv);
  }
  if (write_result) sh4_emit_store_greg(&cg, a, rd);
  sh4g_close(tp, &cg);
  if (set_flags)
    sh4g_set_nzcv(tp, SH4_REG_T0, SH4_REG_ARG0, SH4_REG_ARG1);
}

/* Emit native SH4 for the Thumb data-proc `opcode`, or return 0 to fall back to
 * the C helper. flag_status is gpSP's per-instruction dead-flag mask, but the
 * SH4 path deliberately ignores it for now: memory/DMA/IRQ exits can expose CPSR
 * between a "dead" flag write and the next in-block consumer. Exact flags are
 * the correctness baseline; safe exit-aware flag liveness can come later. */
static inline int sh4g_thumb_dp_native(u8 **tp, u32 opcode, u32 pc, u32 flag_status)
{
  u32 hi = (opcode >> 8) & 0xFF;
  (void)pc;
  (void)flag_status;

  /* fmt3 MOV Rd,#imm8  (001 00 ddd iiiiiiii) — N/Z only, C/V preserved. */
  if (hi >= 0x20 && hi <= 0x27) {
    unsigned rd = (opcode >> 8) & 7;
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T0);
    sh4g_store_greg(tp, SH4_REG_T0, rd);
    sh4g_set_nz(tp, SH4_REG_T0);
    return 1;
  }

  /* fmt3 CMP/ADD/SUB Rd,#imm8  (001 op ddd iiiiiiii), op: 1 CMP, 2 ADD, 3 SUB. */
  if (hi >= 0x28 && hi <= 0x3F) {
    unsigned sub = (opcode >> 11) & 3;
    unsigned rd  = (opcode >> 8) & 7;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rd, SH4_REG_T0);          /* a = reg[rd] */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, opcode & 0xFF, SH4_REG_T1);          /* b = imm8 */
    if (sub == 2) sh4g_dp_addsub(tp, 0, rd, 1, 1);      /* ADD */
    else          sh4g_dp_addsub(tp, 1, rd, sub == 3, 1); /* CMP (no wb) / SUB */
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
    sh4g_dp_addsub(tp, is_sub, rd, 1, 1);
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
    sh4g_set_nz(tp, SH4_REG_T0);
    return 1;
  }

  return 0;
}

/* Native Thumb byte LDR/STR with imm5 offset:
 *   0111 0 imm5 rb rd  STRB Rd,[Rb,#imm5]
 *   0111 1 imm5 rb rd  LDRB Rd,[Rb,#imm5]
 * Only EWRAM/IWRAM are fast-pathed; all other regions fall back to the helper
 * so I/O side effects, ROM paging, backup, and alerts stay exact. */
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
  u32 rd, rb, imm5, is_load;
  u8 *guard, *bra_done;

  if (hi < 0x70 || hi > 0x7F)
    return 0;

  rd = opcode & 7;
  rb = (opcode >> 3) & 7;
  imm5 = (opcode >> 6) & 0x1F;
  is_load = (opcode >> 11) & 1;

  /* STRB writes EWRAM/IWRAM directly, which would bypass the SMC tag check that
   * only runs inside execute_store_*; route byte STORES through the C helper so a
   * self-modifying write still flushes the RAM translation cache. LDRB (a read)
   * has no such hazard and stays native. */
  if (!is_load)
    return 0;

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rb, SH4_REG_T0);       /* R1 = base */
    sh4g_close(tp, &cg); }
  if (imm5) {
    sh4g_const(tp, imm5, SH4_REG_T1);
    sh4g_add_reg(tp, SH4_REG_T1, SH4_REG_T0);      /* R1 = addr */
  }

  /* EWRAM/IWRAM only: addr>>25 is 1 for 0x02xxxxxx and 0x03xxxxxx. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -25, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 1);
    sh4g_close(tp, &cg); }
  guard = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);   /* R0 = addr >> 15 */
    sh4_emit_shll2(&cg, SH4_REG_RET);              /* R0 = index * 4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);

    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET); /* R0 = addr & 0x7FFF */
    sh4_emit_shll16(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr(&cg, SH4_REG_RET);

    if (is_load) {
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    } else {
      sh4_emit_load_greg(&cg, rd, SH4_REG_T1);
      sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    }
    sh4g_close(tp, &cg); }
  /* Charge the single byte access (nonseq, byte column); addr is still in T0.
   * The slow path charges the same via cgba_sh4_extra_cycles. */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0, /*is_word=*/0, 1);
  bra_done = sh4g_emit_bra_placeholder(tp);

  sh4g_patch_cond(guard, *tp);
  sh4g_const(tp, (u32)opcode, SH4_REG_ARG0);
  sh4g_const(tp, (u32)pc, SH4_REG_ARG1);
  sh4g_far_call(tp, (const void *)cgba_sh4_thumb_ldst);
  sh4g_cycle_debit_from_global(tp, &cgba_sh4_extra_cycles);
  sh4g_redispatch_if_r0_debit(tp, cycle_count, (const void *)sh4_block_exit);

  sh4g_patch_bra(bra_done, *tp);
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
 * for the rotated-immediate case. Shifted-register forms, RSB/ADC/SBC/RSC and
 * the PC operands stay on the C path. Flags gated on the S bit (ARM has no
 * dead-flag elimination — flag_status is always 0xF). */
static inline int sh4g_arm_dp_native(u8 **tp, u32 opcode, u32 pc)
{
  u32 op = (opcode >> 21) & 0xF;
  u32 S  = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  int is_imm = (opcode & 0x02000000) != 0;
  u32 op2 = 0, rm = 0;
  u32 shifted = 0, shift_type = 0, shift_amount = 0;
  int c_const = -1;                              /* -1 = preserve C; 0/1 = set const */
  (void)pc;

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
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    if (shifted) sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    else         sh4g_arm_load_op2(tp, is_imm, op2, rm);
    sh4g_dp_addsub(tp, is_sub, rd, write, S);
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
    { sh4_codegen cg = sh4g_open(tp);
      if (!(dpop == SH4DP_MOV || dpop == SH4DP_MVN))  /* MOV/MVN: second only */
        sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    if (shifted) {                               /* operand2 = shifted reg[rm] */
      if (S) sh4g_arm_shift_imm_full(tp, shift_type, shift_amount, rm, SH4_REG_ARG2);
      else   sh4g_arm_shift_imm_op2(tp, shift_type, shift_amount, rm);
    } else {
      sh4g_arm_load_op2(tp, is_imm, op2, rm);
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4g_dp_compute(&cg, dpop);
      if (write)
        sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
      sh4g_close(tp, &cg); }
    if (S) {                                     /* N/Z; V kept; C from shifter */
      sh4g_set_nz(tp, SH4_REG_T0);
      if (shifted)           sh4g_set_c_reg(tp, SH4_REG_ARG2);  /* runtime shifter C */
      else if (c_const >= 0) sh4g_set_c_const(tp, (unsigned)c_const);
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
    if (op == 0x3) sh4g_dp_addsub(tp, 1, rd, 1, S);          /* RSB = op2 - rn */
    else           sh4g_dp_carry(tp, op != 0x5, rd, 1, S);   /* ADC add / SBC,RSC sub */
    return 1;
  }
  default: return 0;
  }
}

#endif /* CGBA_SH4_THUMB_DP_EMIT_H */
