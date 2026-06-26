/*
 * Bring-up instruction helpers for the SH-4A dynarec.
 *
 * The dynarec emits real SH4 for Thumb data-processing, shifts, branches and
 * conditions; the heavier/rarer instruction classes route to these C helpers
 * (correct-by-reuse of gpSP's memory core) while the inline emitters grow. Each
 * helper interprets exactly one guest instruction against gpSP's `reg[]` state
 * and the execute load/store memory accessors. Helpers that can change the
 * guest PC return 1 so the emitted glue re-dispatches.
 *
 * These keep gpSP's little-endian guest semantics: memory goes through the
 * execute_load / execute_store helpers, never raw host pointers.
 */

#include "vendor/gpsp/common.h"
#include "vendor/gpsp/cpu.h"

u32 execute_arm_translate_internal(u32 cycles, void *reg_base);  /* sh4_stub.S */

/* When set, sh4_block_exit returns to the C caller after one translated block
 * (the differential harness uses this to step the dynarec one block at a time). */
int cgba_dynarec_single_block = 0;

/* ---- guest memory accessors (backend-provided wrappers over gba_memory.c) --
 * The MIPS/ARM/x86 backends supply the execute_load / execute_store family that
 * translated code calls; for SH4 they are thin C wrappers over gpSP's
 * region-dispatching read_memory / write_memory core. SMC write-alerts are
 * ignored in bring-up (RAM-code invalidation is a follow-up). */
u32 function_cc execute_load_u8(u32 address)  { return read_memory8(address); }
u32 function_cc execute_load_u16(u32 address) { return read_memory16(address); }
u32 function_cc execute_load_u32(u32 address) { return read_memory32(address); }
u32 function_cc execute_load_s8(u32 address)  { return read_memory8s(address); }
u32 function_cc execute_load_s16(u32 address) { return read_memory16s(address); }
void function_cc execute_store_u8(u32 address, u32 source)  { write_memory8(address, (u8)source); }
void function_cc execute_store_u16(u32 address, u32 source) { write_memory16(address, (u16)source); }
void function_cc execute_store_u32(u32 address, u32 source) { write_memory32(address, source); }
void function_cc execute_store_aligned_u32(u32 address, u32 source) { write_memory32(address, source); }

/* CPSR flag bits (canonical packed form, matching the interpreter). */
#define CF_N (1u << 31)
#define CF_Z (1u << 30)
#define CF_C (1u << 29)
#define CF_V (1u << 28)

static inline void set_nz(u32 result)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  reg[REG_CPSR] = cpsr;
}

static inline void set_nzcv(u32 result, u32 c, u32 v)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C | CF_V);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (c)                    cpsr |= CF_C;
  if (v)                    cpsr |= CF_V;
  reg[REG_CPSR] = cpsr;
}

/* ARM add-with-carry: a + b + cin, with full NZCV. All ARM/Thumb arithmetic is
 * expressed as add-with-carry of possibly-inverted operands:
 *   ADD = addc(a,  b, 0)   ADC = addc(a,  b, C)
 *   SUB = addc(a, ~b, 1)   SBC = addc(a, ~b, C)   RSB = addc(~a, b, 1)
 *   CMP = SUB (no write)   CMN = ADD (no write)   NEG = addc(~a, 0, 1) */
static inline u32 do_addc(u32 a, u32 b, u32 cin, int set_flags)
{
  u64 tmp = (u64)a + (u64)b + cin;
  u32 res = (u32)tmp;
  if (set_flags)
    set_nzcv(res, (u32)(tmp >> 32), ((~(a ^ b)) & (a ^ res)) >> 31);
  return res;
}

/* ===================== Thumb single load/store ===================== */

void cgba_sh4_thumb_ldst(u32 opcode, u32 pc)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7;
  u32 rb = (opcode >> 3) & 7;

  if (hi >= 0x48 && hi <= 0x4F) {                  /* LDR Rd,[PC,#imm8*4] */
    u32 addr = ((pc & ~2u) + 4) + ((opcode & 0xFF) * 4);
    reg[rd] = execute_load_u32(addr);
    return;
  }
  if (hi >= 0x50 && hi <= 0x5F) {                  /* reg-offset forms */
    u32 ro = (opcode >> 6) & 7;
    u32 addr = reg[rb] + reg[ro];
    if (opcode & 0x0200) {                          /* format 8: H/S */
      switch ((opcode >> 10) & 3) {
      case 0: execute_store_u16(addr, reg[rd]); break;     /* STRH */
      case 1: reg[rd] = execute_load_s8(addr);  break;     /* LDRSB */
      case 2: reg[rd] = execute_load_u16(addr); break;     /* LDRH */
      case 3: reg[rd] = execute_load_s16(addr); break;     /* LDRSH */
      }
    } else {                                        /* format 7: L/B */
      switch ((opcode >> 10) & 3) {
      case 0: execute_store_u32(addr, reg[rd]); break;     /* STR */
      case 1: execute_store_u8(addr, reg[rd]);  break;     /* STRB */
      case 2: reg[rd] = execute_load_u32(addr); break;     /* LDR */
      case 3: reg[rd] = execute_load_u8(addr);  break;     /* LDRB */
      }
    }
    return;
  }
  if (hi >= 0x60 && hi <= 0x7F) {                  /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    u32 is_load = (opcode >> 11) & 1;
    u32 addr = reg[rb] + (is_byte ? imm5 : imm5 * 4);
    if (is_byte) {
      if (is_load) reg[rd] = execute_load_u8(addr);
      else         execute_store_u8(addr, reg[rd]);
    } else {
      if (is_load) reg[rd] = execute_load_u32(addr);
      else         execute_store_u32(addr, reg[rd]);
    }
    return;
  }
  if (hi >= 0x80 && hi <= 0x8F) {                  /* LDRH/STRH imm5*2 */
    u32 addr = reg[rb] + ((opcode >> 6) & 0x1F) * 2;
    if (opcode & 0x0800) reg[rd] = execute_load_u16(addr);
    else                 execute_store_u16(addr, reg[rd]);
    return;
  }
  if (hi >= 0x90 && hi <= 0x9F) {                  /* LDR/STR [SP,#imm8*4] */
    u32 rdsp = (opcode >> 8) & 7;
    u32 addr = reg[REG_SP] + (opcode & 0xFF) * 4;
    if (opcode & 0x0800) reg[rdsp] = execute_load_u32(addr);
    else                 execute_store_u32(addr, reg[rdsp]);
    return;
  }
}

/* ===================== Thumb block (PUSH/POP/LDMIA/STMIA) =========== */

int cgba_sh4_thumb_block(u32 opcode, u32 pc)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rlist = opcode & 0xFF;
  int wrote_pc = 0;
  int i;

  (void)pc;
  if (hi == 0xB4 || hi == 0xB5) {                  /* PUSH {rlist[, lr]} */
    u32 sp = reg[REG_SP];
    u32 count = 0;
    for (i = 0; i < 8; i++) if (rlist & (1 << i)) count++;
    if (hi == 0xB5) count++;                         /* lr */
    sp -= count * 4;
    reg[REG_SP] = sp;
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { execute_store_u32(sp, reg[i]); sp += 4; }
    if (hi == 0xB5) execute_store_u32(sp, reg[REG_LR]);
    return 0;
  }
  if (hi == 0xBC || hi == 0xBD) {                  /* POP {rlist[, pc]} */
    u32 sp = reg[REG_SP];
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { reg[i] = execute_load_u32(sp); sp += 4; }
    if (hi == 0xBD) { reg[REG_PC] = execute_load_u32(sp) & ~1u; sp += 4; wrote_pc = 1; }
    reg[REG_SP] = sp;
    return wrote_pc;
  }
  if (hi >= 0xC0 && hi <= 0xCF) {                  /* STMIA/LDMIA Rb!,{rlist} */
    u32 rb = (opcode >> 8) & 7;
    u32 addr = reg[rb];
    u32 is_load = (opcode >> 11) & 1;
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) {
        if (is_load) reg[i] = execute_load_u32(addr);
        else         execute_store_u32(addr, reg[i]);
        addr += 4;
      }
    reg[rb] = addr;
    return 0;
  }
  return 0;
}

/* ===================== Thumb register shift (LSL/LSR/ASR/ROR Rd,Rs) =
 * Real SH4 SHLD/SHAD only use the low 5 bits + sign, so ARM amounts >= 32 and
 * ROR-as-rotate must be done in C. Sets N/Z/C exactly. */
void cgba_sh4_thumb_shift_reg(u32 opcode, u32 pc)
{
  u32 rd = opcode & 7;
  u32 rs = (opcode >> 3) & 7;
  u32 sub = (opcode >> 6) & 0xF;     /* 0x2=LSL 0x3=LSR 0x4=ASR 0x7=ROR */
  u32 val = reg[rd];
  u32 amount = reg[rs] & 0xFF;
  u32 result = val, carry = (reg[REG_CPSR] >> 29) & 1, cpsr;
  (void)pc;

  switch (sub) {
  case 0x2: /* LSL */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (32 - amount)) & 1; result = val << amount; }
    else if (amount == 32) { carry = val & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x3: /* LSR */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = val >> amount; }
    else if (amount == 32) { carry = (val >> 31) & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x4: /* ASR */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = (u32)((s32)val >> amount); }
    else                   { carry = (val >> 31) & 1; result = (u32)((s32)val >> 31); }
    break;
  default: { /* ROR */
    u32 a = amount & 31;
    if (amount == 0) {}
    else if (a == 0) { carry = (val >> 31) & 1; }
    else { carry = (val >> (a - 1)) & 1; result = (val >> a) | (val << (32 - a)); }
    break;
  }
  }

  reg[rd] = result;
  cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (carry)                cpsr |= CF_C;
  reg[REG_CPSR] = cpsr;
}

/* Thumb format 1 immediate shift (LSL/LSR/ASR Rd,Rs,#imm5), N/Z/C. */
void cgba_sh4_thumb_shift_imm(u32 opcode, u32 pc)
{
  u32 op = (opcode >> 11) & 3;       /* 0=LSL 1=LSR 2=ASR */
  u32 imm5 = (opcode >> 6) & 0x1F;
  u32 rs = (opcode >> 3) & 7, rd = opcode & 7;
  u32 val = reg[rs];
  u32 result = val, carry = (reg[REG_CPSR] >> 29) & 1, cpsr;
  (void)pc;

  switch (op) {
  case 0:                                            /* LSL #imm (0 = no-op) */
    if (imm5) { carry = (val >> (32 - imm5)) & 1; result = val << imm5; }
    break;
  case 1:                                            /* LSR (#0 means #32) */
    if (imm5 == 0) { carry = (val >> 31) & 1; result = 0; }
    else { carry = (val >> (imm5 - 1)) & 1; result = val >> imm5; }
    break;
  default:                                           /* ASR (#0 means #32) */
    if (imm5 == 0) { carry = (val >> 31) & 1; result = (u32)((s32)val >> 31); }
    else { carry = (val >> (imm5 - 1)) & 1; result = (u32)((s32)val >> imm5); }
    break;
  }

  reg[rd] = result;
  cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (carry)                cpsr |= CF_C;
  reg[REG_CPSR] = cpsr;
}

/* ===================== Thumb data-processing (full NZCV) =============
 * The native inline emitters set only N/Z (and mishandle ADC/SBC carry-in),
 * so the Thumb data-proc family routes here for correct N/Z/C/V and carry-in
 * until inline flag synthesis (the status doc's lazy/dead-flag path) lands.
 * Returns 1 if it wrote PC (hi-register ADD/MOV) so the caller re-dispatches. */
int cgba_sh4_thumb_dp(u32 opcode, u32 pc)
{
  u32 hi  = (opcode >> 8) & 0xFF;
  u32 cin = (reg[REG_CPSR] >> 29) & 1;

  if (hi >= 0x18 && hi <= 0x1F) {                    /* fmt2 ADD/SUB rd,rs,rn|imm3 */
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 b = (opcode & 0x0400) ? ((opcode >> 6) & 7) : reg[(opcode >> 6) & 7];
    u32 a = reg[rs];
    reg[rd] = (opcode & 0x0200) ? do_addc(a, ~b, 1, 1) : do_addc(a, b, 0, 1);
    return 0;
  }
  if (hi >= 0x20 && hi <= 0x3F) {                    /* fmt3 MOV/CMP/ADD/SUB rd,#imm8 */
    u32 rd = (opcode >> 8) & 7, imm = opcode & 0xFF, a = reg[rd];
    switch ((opcode >> 11) & 3) {
    case 0: reg[rd] = imm; set_nz(imm); break;            /* MOV */
    case 1: do_addc(a, ~imm, 1, 1); break;                /* CMP */
    case 2: reg[rd] = do_addc(a, imm, 0, 1); break;       /* ADD */
    default: reg[rd] = do_addc(a, ~imm, 1, 1); break;     /* SUB */
    }
    return 0;
  }
  if (hi >= 0x40 && hi <= 0x43) {                    /* fmt4 ALU (non-shift) */
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 a = reg[rd], b = reg[rs];
    switch ((opcode >> 6) & 0xF) {
    case 0x0: reg[rd] = a & b;  set_nz(reg[rd]); break;   /* AND */
    case 0x1: reg[rd] = a ^ b;  set_nz(reg[rd]); break;   /* EOR */
    case 0x5: reg[rd] = do_addc(a, b, cin, 1); break;     /* ADC */
    case 0x6: reg[rd] = do_addc(a, ~b, cin, 1); break;    /* SBC */
    case 0x8: set_nz(a & b); break;                       /* TST */
    case 0x9: reg[rd] = do_addc(~b, 0, 1, 1); break;      /* NEG = 0 - rs */
    case 0xA: do_addc(a, ~b, 1, 1); break;                /* CMP */
    case 0xB: do_addc(a, b, 0, 1); break;                 /* CMN */
    case 0xC: reg[rd] = a | b;  set_nz(reg[rd]); break;   /* ORR */
    case 0xD: reg[rd] = a * b;  set_nz(reg[rd]); break;   /* MUL */
    case 0xE: reg[rd] = a & ~b; set_nz(reg[rd]); break;   /* BIC */
    case 0xF: reg[rd] = ~b;     set_nz(reg[rd]); break;   /* MVN */
    default: break;  /* 0x2/0x3/0x4/0x7 shifts routed to shift helpers */
    }
    return 0;
  }
  if (hi >= 0x44 && hi <= 0x46) {                    /* fmt5 hi-reg ADD/CMP/MOV */
    u32 op = (opcode >> 8) & 3;                       /* 0 ADD, 1 CMP, 2 MOV */
    u32 rd = (opcode & 7) | ((opcode >> 4) & 8);
    u32 rs = (opcode >> 3) & 0xF;
    u32 a = (rd == 15) ? (pc + 4) : reg[rd];          /* Thumb R15 reads PC+4 */
    u32 b = (rs == 15) ? (pc + 4) : reg[rs];
    u32 res;
    if (op == 1) { do_addc(a, ~b, 1, 1); return 0; }  /* CMP sets flags only */
    res = (op == 0) ? (a + b) : b;                     /* ADD / MOV: no flags */
    if (rd == 15) { reg[REG_PC] = res & ~1u; return 1; }
    reg[rd] = res;
    return 0;
  }
  return 0;
}

/* ===================== ARM single data transfer ==================== */

static u32 arm_shifter_operand(u32 opcode, u32 pc, u32 *carry_out)
{
  /* Register-or-immediate shifter operand for ARM data-processing / LDR. */
  u32 cpsr_c = (reg[REG_CPSR] >> 29) & 1;
  if (opcode & 0x02000000) {                       /* immediate (rotated) */
    u32 imm = opcode & 0xFF;
    u32 rot = ((opcode >> 8) & 0xF) * 2;
    u32 v = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
    *carry_out = rot ? ((v >> 31) & 1) : cpsr_c;
    return v;
  } else {                                         /* register shift */
    u32 rm = opcode & 0xF;
    u32 by_reg = opcode & 0x10;                     /* shift amount in a reg */
    /* R15 reads PC+8, or PC+12 when the shift amount is register-specified
       (the extra pipeline cycle advances PC one instruction further). */
    u32 val = (rm == 15) ? (pc + (by_reg ? 12 : 8)) : reg[rm];
    u32 type = (opcode >> 5) & 3;
    u32 amount;
    if (by_reg) {                                   /* shift by register */
      u32 rs = (opcode >> 8) & 0xF;
      amount = reg[rs] & 0xFF;
      if (amount == 0) { *carry_out = cpsr_c; return val; } /* reg-#0: no-op */
    } else {
      amount = (opcode >> 7) & 0x1F;
    }
    switch (type) {
    case 0: /* LSL */
      if (amount == 0) { *carry_out = cpsr_c; return val; }
      if (amount < 32) { *carry_out = (val >> (32 - amount)) & 1; return val << amount; }
      *carry_out = (amount == 32) ? (val & 1) : 0; return 0;
    case 1: /* LSR */
      if (amount == 0) amount = 32;
      if (amount < 32) { *carry_out = (val >> (amount - 1)) & 1; return val >> amount; }
      *carry_out = (amount == 32) ? ((val >> 31) & 1) : 0; return 0;
    case 2: /* ASR */
      if (amount == 0 || amount >= 32) { *carry_out = (val >> 31) & 1; return (u32)((s32)val >> 31); }
      *carry_out = (val >> (amount - 1)) & 1; return (u32)((s32)val >> amount);
    default: /* ROR / RRX */
      if (amount == 0) { u32 r = (cpsr_c << 31) | (val >> 1); *carry_out = val & 1; return r; }
      amount &= 31;
      if (amount == 0) { *carry_out = (val >> 31) & 1; return val; }
      *carry_out = (val >> (amount - 1)) & 1;
      return (val >> amount) | (val << (32 - amount));
    }
  }
}

int cgba_sh4_arm_ldst(u32 opcode, u32 pc)
{
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 base = (rn == 15) ? (pc + 8) : reg[rn];
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 offset, addr;
  int is_half = 0, is_byte = 0, signed_ld = 0, half_w = 0;

  if ((opcode & 0x0E000090) == 0x00000090) {
    /* halfword / signed-byte transfer (bits 27..25 = 000, bits 7,4 = 1) */
    is_half = 1;
    signed_ld = (opcode >> 6) & 1;
    half_w = (opcode >> 5) & 1;
    if (opcode & 0x00400000) offset = ((opcode >> 4) & 0xF0) | (opcode & 0x0F); /* imm */
    else                     offset = reg[opcode & 0xF];                        /* reg */
  } else {
    is_byte = (opcode >> 22) & 1;
    if (opcode & 0x02000000) {                       /* register, shifted by imm */
      u32 rm = reg[opcode & 0xF];
      u32 type = (opcode >> 5) & 3, sh = (opcode >> 7) & 0x1F;
      switch (type) {
      case 0: offset = sh ? (rm << sh) : rm; break;
      case 1: offset = sh ? (rm >> sh) : 0; break;
      case 2: offset = (u32)((s32)rm >> (sh ? sh : 31)); break;
      default: offset = sh ? ((rm >> sh) | (rm << (32 - sh)))
                           : (((reg[REG_CPSR] >> 29) & 1) << 31) | (rm >> 1); break;
      }
    } else {
      offset = opcode & 0xFFF;                        /* 12-bit immediate */
    }
  }

  addr = pre ? (up ? base + offset : base - offset) : base;

  if (is_load) {
    u32 v;
    if (is_half) {
      if (signed_ld && half_w)  v = execute_load_s16(addr);
      else if (signed_ld)       v = execute_load_s8(addr);
      else                      v = execute_load_u16(addr);
    } else {
      v = is_byte ? execute_load_u8(addr) : execute_load_u32(addr);
    }
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
    reg[rd] = v;
    if (rd == 15) { reg[REG_PC] = v & ~1u; return 1; }
  } else {
    u32 v = (rd == 15) ? (pc + 12) : reg[rd];
    if (is_half) execute_store_u16(addr, v);
    else if (is_byte) execute_store_u8(addr, v);
    else execute_store_u32(addr, v);
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
  }
  return 0;
}

/* ===================== CPSR / SPSR write (gpSP canonical) ==========
 * Every mode-changing write goes through these so the register banking stays in
 * one place — the same set_cpu_mode + check_for_interrupts path the interpreter
 * uses (cpu.cc). Doing the re-bank ad hoc per call site is what desynced
 * reg[CPU_MODE] before. The IRQ gate reads IE/IF/IME through read_ioreg (the
 * guest is little-endian, the SH4 host is big-endian), unlike the x86 backend
 * which can index io_registers[] directly. */

/* set_cpu_mode + check_for_interrupts: re-bank to the mode now in reg[REG_CPSR]
 * and, if that just unmasked a pending IRQ, enter it (LR_irq = next_pc + 4,
 * SPSR_irq = old CPSR, CPSR = 0xD2, switch to IRQ mode). Returns the IRQ vector
 * to redispatch to, or 0 if no IRQ was taken. */
static u32 sh4_rebank_and_irq(u32 next_pc)
{
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && read_ioreg(REG_IME) &&
      ((reg[REG_CPSR] & 0x80) == 0)) {
    REG_MODE(MODE_IRQ)[6] = next_pc + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    set_cpu_mode(MODE_IRQ);
    return 0x00000018;
  }
  return 0;
}

/* gpSP execute_spsr_restore: exception return (SUBS pc,... / LDM{pc}^). In a
 * privileged non-system mode, restore CPSR from the mode's SPSR, re-bank, take a
 * now-pending IRQ, and fold the restored Thumb bit (bit0) into the address. In
 * USER/SYSTEM there is no SPSR, so the address passes through unchanged. */
u32 execute_spsr_restore(u32 address)
{
  if (reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM) {
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    if (sh4_rebank_and_irq(address))
      address = 0x00000018;
    else if (reg[REG_CPSR] & 0x20)
      address |= 0x01;
  }
  return address;
}

/* ===================== ARM block (LDM/STM) ========================= */

int cgba_sh4_arm_block(u32 opcode, u32 pc)
{
  u32 rn = (opcode >> 16) & 0xF;
  u32 rlist = opcode & 0xFFFF;
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 s_bit = (opcode >> 22) & 1;
  u32 base = reg[rn];
  u32 count = 0, addr, new_base, i, lowest = 16;
  int wrote_pc = 0;

  /* TODO(S-bit): with the S bit set and r15 NOT in the list, LDM/STM transfer
     the USER-mode banked registers — not handled here (rare). */

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) { count++; if (lowest == 16) lowest = i; }

  new_base = up ? base + count * 4 : base - count * 4;
  addr = (up ? base : new_base) & ~3u;   /* word-align; traverse ascending */
  if (up == 0) pre = !pre;

  for (i = 0; i < 16; i++) {
    if (!(rlist & (1u << i))) continue;
    if (pre) addr += 4;
    if (is_load) {
      reg[i] = execute_load_u32(addr);
      if (i == 15) { reg[REG_PC] = reg[15] & ~1u; wrote_pc = 1; }
    } else {
      /* STM stores PC+12 for r15; for the base reg, the OLD base is stored only
         when it is the lowest in the list, otherwise the written-back value. */
      u32 v = (i == 15) ? (pc + 12)
            : (i == rn && writeback && i != lowest) ? new_base
            : reg[i];
      execute_store_u32(addr, v);
    }
    if (!pre) addr += 4;
  }

  /* LDM with the base in the list keeps the loaded value (no writeback). */
  if (writeback && !(is_load && (rlist & (1u << rn))))
    reg[rn] = new_base;

  /* LDM{pc}^ (exception return): restore CPSR from SPSR, re-bank, take any now-
   * pending IRQ — the canonical execute_spsr_restore path. */
  if (wrote_pc && s_bit) {
    u32 next = execute_spsr_restore(reg[15]);
    reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
  }
  return wrote_pc;
}

/* ===================== ARM data-processing ========================= */

int cgba_sh4_arm_dp(u32 opcode, u32 pc)
{
  u32 op = (opcode >> 21) & 0xF;
  u32 set_flags = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  /* With a register-controlled shift (bit25 clear, bit4 set), R15 reads PC+12;
     otherwise PC+8. arm_shifter_operand applies the same rule to Rm. */
  u32 reg_shift = ((opcode & 0x02000010) == 0x00000010);
  u32 a = (rn == 15) ? (pc + (reg_shift ? 12 : 8)) : reg[rn];
  u32 carry = (reg[REG_CPSR] >> 29) & 1;
  u32 b = arm_shifter_operand(opcode, pc, &carry);
  u32 cf = carry, vf = (reg[REG_CPSR] >> 28) & 1, res = 0;
  u32 oldc = (reg[REG_CPSR] >> 29) & 1;
  int writes = 1;
  u64 tmp;

  switch (op) {
  case 0x0: res = a & b; break;                    /* AND */
  case 0x1: res = a ^ b; break;                    /* EOR */
  case 0x2: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SUB */
  case 0x3: tmp = (u64)b - (u64)a; res = (u32)tmp; cf = b >= a;
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSB */
  case 0x4: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADD */
  case 0x5: tmp = (u64)a + (u64)b + oldc; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADC */
  case 0x6: tmp = (u64)a - (u64)b - (1 - oldc); res = (u32)tmp; cf = a >= ((u64)b + (1 - oldc));
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SBC */
  case 0x7: tmp = (u64)b - (u64)a - (1 - oldc); res = (u32)tmp; cf = b >= ((u64)a + (1 - oldc));
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSC */
  case 0x8: res = a & b; writes = 0; break;        /* TST */
  case 0x9: res = a ^ b; writes = 0; break;        /* TEQ */
  case 0xA: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; writes = 0; break;  /* CMP */
  case 0xB: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; writes = 0; break; /* CMN */
  case 0xC: res = a | b; break;                    /* ORR */
  case 0xD: res = b; break;                        /* MOV */
  case 0xE: res = a & ~b; break;                   /* BIC */
  case 0xF: res = ~b; break;                       /* MVN */
  }

  if (writes) {
    reg[rd] = res;
    if (rd == 15) {
      /* Writing PC with the S bit (e.g. SUBS pc,lr,#4 — the IRQ/exception
       * return) restores CPSR from SPSR and re-banks via execute_spsr_restore,
       * which also folds the restored Thumb bit into bit0. Mask afterwards so
       * the new Thumb state picks the alignment. */
      u32 next = set_flags ? execute_spsr_restore(res) : res;
      reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
      return 1;
    }
    if (set_flags) set_nzcv(res, cf, vf & 1);
  } else if (set_flags) {
    set_nzcv(res, cf, vf & 1);
  }
  return 0;
}

/* ===================== ARM multiply / psr / swap (TODO inline) ====== */

void cgba_sh4_arm_multiply(u32 opcode, u32 pc)
{
  u32 rd = (opcode >> 16) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 rn = (opcode >> 12) & 0xF;
  u32 res = reg[rm] * reg[rs];
  (void)pc;
  if (opcode & 0x00200000) res += reg[rn];          /* MLA */
  reg[rd] = res;
  if (opcode & 0x00100000) set_nz(res);             /* S bit */
}

void cgba_sh4_arm_multiply_long(u32 opcode, u32 pc)
{
  u32 rdhi = (opcode >> 16) & 0xF;
  u32 rdlo = (opcode >> 12) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_signed = (opcode >> 22) & 1;
  u32 accumulate = (opcode >> 21) & 1;
  u64 res;
  (void)pc;
  if (is_signed) res = (u64)((s64)(s32)reg[rm] * (s64)(s32)reg[rs]);
  else           res = (u64)reg[rm] * (u64)reg[rs];
  if (accumulate) res += (((u64)reg[rdhi]) << 32) | reg[rdlo];
  reg[rdlo] = (u32)res;
  reg[rdhi] = (u32)(res >> 32);
  if (opcode & 0x00100000) {                         /* S bit */
    u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
    if (res & 0x8000000000000000ull) cpsr |= CF_N;
    if (res == 0) cpsr |= CF_Z;
    reg[REG_CPSR] = cpsr;
  }
}

int cgba_sh4_arm_psr(u32 opcode, u32 pc)
{
  u32 is_msr   = (opcode >> 21) & 1;   /* 0 = MRS (read), 1 = MSR (write) */
  u32 use_spsr = (opcode >> 22) & 1;   /* 0 = CPSR, 1 = SPSR of cur mode  */

  if (!is_msr) {                                     /* MRS Rd, <psr> */
    u32 rd = (opcode >> 12) & 0xF;
    reg[rd] = use_spsr ? REG_SPSR(reg[CPU_MODE]) : reg[REG_CPSR];
    return 0;
  }

  {                                                  /* MSR <psr>, val */
    /* Field mask is privilege-aware (gpSP's cpsr_masks): in USER mode the
       control byte is restricted to the Thumb bit, so USER code cannot change
       the mode bits. pfield bit0 = control field, bit1 = flags field. */
    u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
    u32 val, mask;
    if (opcode & 0x02000000) {
      u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
      val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else {
      val = reg[opcode & 0xF];
    }

    if (use_spsr) {
      mask = spsr_masks[pfield];
      REG_SPSR(reg[CPU_MODE]) = (val & mask) | (REG_SPSR(reg[CPU_MODE]) & ~mask);
    } else {
      /* CPSR write through the canonical bank path. When the control byte is
       * written (mode/IRQ-disable change) re-bank and, if an IRQ just unmasked,
       * take it and redispatch — gpSP's execute_store_cpsr. */
      mask = cpsr_masks[pfield][PRIVMODE(reg[CPU_MODE])];
      reg[REG_CPSR] = (val & mask) | (reg[REG_CPSR] & ~mask);
      if (mask & 0xFF) {
        u32 vec = sh4_rebank_and_irq(pc + 4);
        if (vec) { reg[REG_PC] = vec; return 1; }
      }
    }
  }
  return 0;
}

void cgba_sh4_arm_swap(u32 opcode, u32 pc)
{
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_byte = (opcode >> 22) & 1;
  u32 addr = reg[rn];
  (void)pc;
  if (is_byte) {
    u32 tmp = execute_load_u8(addr);
    execute_store_u8(addr, reg[rm]);
    reg[rd] = tmp;
  } else {
    u32 tmp = execute_load_u32(addr);
    execute_store_u32(addr, reg[rm]);
    reg[rd] = tmp;
  }
}

/* SWI 0x06/0x07 divide HLE (operands in r0/r1). */
void cgba_sh4_hle_div(u32 cpu_mode, u32 pc)
{
  s32 num = (s32)reg[0];
  s32 den = (s32)reg[1];
  (void)cpu_mode; (void)pc;
  if (den != 0) {
    s32 q = num / den, r = num % den;
    reg[0] = (u32)q;
    reg[1] = (u32)r;
    reg[3] = (u32)(q < 0 ? -q : q);
  }
}

/* SWI trampoline target (sh4_stub.S execute_swi -> here). The stub has already
 * stashed the return address (next-instruction PC) into reg[REG_PC]. Vector to
 * the BIOS SWI handler exactly like the interpreter (cpu.cc:3516): bank into
 * Supervisor mode, save LR_svc / SPSR_svc, switch to ARM with IRQs disabled, set
 * the post-SWI open-bus value, and point PC at the 0x08 vector.
 *
 * Only the mode/bank/vector setup happens here — the emitted block then
 * redispatches to 0x08, because the block scan already registered this exit's
 * target as the BIOS vector (cpu_threaded.c:2957) and arm_swi/thumb_swi branch
 * there. gpSP loads the real open BIOS, so no SWI HLE dispatch is needed (the
 * divide HLE is handled separately by cgba_sh4_hle_div). */
void sh4_swi_handler(void)
{
  u32 return_pc = reg[REG_PC];                              /* stashed by execute_swi */
  REG_MODE(MODE_SUPERVISOR)[6] = return_pc;                 /* LR_svc = return address */
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];                /* SPSR_svc = CPSR */
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3Fu) | 0x13u | 0x80u; /* ARM + SVC + IRQ off */
  set_cpu_mode(MODE_SUPERVISOR);
  reg[REG_BUS_VALUE] = 0xe3a02004u;                         /* post-SWI bios[0xE4] open-bus */
  reg[REG_PC] = 0x00000008u;                                /* SWI vector */
}

/* Host-emitter init hook (main.c calls this under HAVE_DYNAREC). The MIPS/x86
 * backends populate inline memory-handler dispatch tables here; the bring-up
 * SH4 backend routes memory through C helpers, so there is nothing to set up. */
void init_emitter(bool must_swap)
{
  (void)must_swap;
}

/* Public dynarec entry (emitter-provided in the other backends): run translated
 * code for up to `cycles`, with R5 pointing at the guest register file. */
u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}
