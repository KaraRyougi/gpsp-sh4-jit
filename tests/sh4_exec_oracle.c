/*
 * Execution oracle for the native Thumb ALU emitters.
 *
 * The pixel-hash soaks are blind to the MP2K audio path (sound output is
 * stubbed), which is exactly where the register-amount shifts and fmt4
 * arithmetic fire hardest — a wrong RESULT there corrupts player structs for
 * thousands of frames with identical video. This test closes that hole: it
 * emits the real native code for each form, executes it in a small SH4
 * interpreter (with branches, delay slots and PC-relative literals), and
 * compares the full guest register file + CPSR against the C helper
 * semantics (sh4_interp_helpers.c), under every liveness mask the scan can
 * produce.
 *
 * Contract checked per case:
 *   - result register and every OTHER guest register: exact;
 *   - flags inside the requested mask: exactly the helper's value;
 *   - flags outside the mask that the instruction writes: helper's value OR
 *     the old value (sh4g_flags_round may widen a mask with correctly
 *     computed extras);
 *   - flags the instruction never writes (e.g. V for shifts): old value.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int32_t s32;
typedef int8_t s8;
typedef int64_t s64;
typedef uint64_t u64;

/* ---- stubs the emit headers reference ---- */
u32 reg[64];
u16 io_registers[512];
u8 *memory_map_read[8192];
u8 ws_cyc_seq[16][2], ws_cyc_nseq[16][2];
u32 cgba_sh4_native_thumb_const_io_count, cgba_sh4_native_thumb_runtime_io_count;
int cgba_sh4_extra_cycles;
int cgba_sh4_thumb_ldst(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_arm_psr(u32 o, u32 p){(void)o;(void)p;return 0;}
void sh4_block_exit(u32 pc){(void)pc;}
void sh4_helper_exit(u32 pc){(void)pc;}
void sh4_op2_pc_mem_tramp(void){}
void sh4_op2_pc_tramp(void){}
void sh4_op2_tramp(void){}
void sh4_op2_mem_tramp(void){}
void sh4_headless_trace_op(char k, u32 pc, u32 op){(void)k;(void)pc;(void)op;}

#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"

/* ---- guest state used by the interpreter ---- */
static u32 g_reg[64];

/* ---- SH4 mini-interpreter: branches, delay slots, PC-relative literals ---- */
static char unmodeled[64];

static int run_sh4x(const u8 *code, size_t n)
{
  u32 R[16] = {0};
  u32 MACL = 0;
  int T = 0;
  size_t pc = 0;
  int steps = 0;

  R[SH4_REG_BASE] = 0;                       /* virtual base: intercepted */
  R[SH4_REG_CPSR] = g_reg[SH4_GREG_CPSR];    /* runtime contract: R8 = CPSR */
  unmodeled[0] = 0;

  while (pc + 1 < n) {
    if (++steps > 20000) { snprintf(unmodeled, sizeof unmodeled, "step cap"); return 0; }
    u16 op = (u16)((code[pc] << 8) | code[pc + 1]);
    unsigned nn = (op >> 8) & 0xF, mm = (op >> 4) & 0xF;
    size_t next = pc + 2;

    if      (op == 0x0009) { /* NOP */ }
    else if (op == 0x0018) T = 1;                                   /* SETT */
    else if (op == 0x0008) T = 0;                                   /* CLRT */
    else if ((op & 0xF000) == 0xE000) R[nn] = (u32)(s32)(s8)(op & 0xFF);
    else if ((op & 0xF000) == 0x7000) R[nn] += (u32)(s32)(s8)(op & 0xFF);
    else if ((op & 0xFF00) == 0x8800) T = ((s32)R[0] == (s32)(s8)(op & 0xFF));
    else if ((op & 0xFF00) == 0xC900) R[0] &= (u32)(op & 0xFF);     /* AND #imm */
    else if ((op & 0xFF00) == 0xC800) T = ((R[0] & (u32)(op & 0xFF)) == 0);
    else if ((op & 0xF00F) == 0x6003) R[nn] = R[mm];                /* MOV */
    else if ((op & 0xF00F) == 0x6007) R[nn] = ~R[mm];               /* NOT */
    else if ((op & 0xF00F) == 0x600B) R[nn] = 0u - R[mm];           /* NEG */
    else if ((op & 0xF00F) == 0x6008) {                             /* SWAP.B */
      u32 v = R[mm];
      R[nn] = (v & 0xFFFF0000u) | ((v >> 8) & 0xFF) | ((v & 0xFF) << 8);
    }
    else if ((op & 0xF00F) == 0x600C) R[nn] = R[mm] & 0xFF;         /* EXTU.B */
    else if ((op & 0xF00F) == 0x600D) R[nn] = R[mm] & 0xFFFF;       /* EXTU.W */
    else if ((op & 0xF00F) == 0x600E) R[nn] = (u32)(s32)(s8)R[mm];  /* EXTS.B */
    else if ((op & 0xF00F) == 0x600F) R[nn] = (u32)(s32)(int16_t)R[mm];
    else if ((op & 0xF00F) == 0x2008) T = ((R[mm] & R[nn]) == 0);   /* TST */
    else if ((op & 0xF00F) == 0x2009) R[nn] &= R[mm];
    else if ((op & 0xF00F) == 0x200A) R[nn] ^= R[mm];
    else if ((op & 0xF00F) == 0x200B) R[nn] |= R[mm];
    else if ((op & 0xF00F) == 0x3000) T = (R[nn] == R[mm]);         /* CMP/EQ */
    else if ((op & 0xF00F) == 0x3002) T = (R[nn] >= R[mm]);         /* CMP/HS */
    else if ((op & 0xF00F) == 0x3006) T = (R[nn] > R[mm]);          /* CMP/HI */
    else if ((op & 0xF00F) == 0x3008) R[nn] -= R[mm];               /* SUB */
    else if ((op & 0xF00F) == 0x300C) R[nn] += R[mm];               /* ADD */
    else if ((op & 0xF00F) == 0x300E) {                             /* ADDC */
      u64 s = (u64)R[nn] + R[mm] + (u32)T;
      R[nn] = (u32)s; T = (int)(s >> 32);
    }
    else if ((op & 0xF00F) == 0x300F) {                             /* ADDV */
      u32 a = R[nn], b = R[mm], r = a + b;
      T = (int)((~(a ^ b) & (a ^ r)) >> 31); R[nn] = r;
    }
    else if ((op & 0xF00F) == 0x300A) {                             /* SUBC */
      u64 s = (u64)R[nn] - R[mm] - (u32)T;
      R[nn] = (u32)s; T = (int)((s >> 32) & 1);
    }
    else if ((op & 0xF00F) == 0x300B) {                             /* SUBV */
      u32 a = R[nn], b = R[mm], r = a - b;
      T = (int)(((a ^ b) & (a ^ r)) >> 31); R[nn] = r;
    }
    else if ((op & 0xF00F) == 0x0007) MACL = R[nn] * R[mm];         /* MUL.L */
    else if ((op & 0xF0FF) == 0x001A) R[nn] = MACL;                 /* STS MACL */
    else if ((op & 0xF0FF) == 0x0029) R[nn] = (u32)T;               /* MOVT */
    else if ((op & 0xF00F) == 0x400C) {                             /* SHAD */
      s32 s = (s32)R[mm];
      if (s >= 0)               R[nn] <<= (s & 0x1F);
      else if ((s & 0x1F) == 0) R[nn] = (u32)((s32)R[nn] >> 31);
      else                      R[nn] = (u32)((s32)R[nn] >> (32 - (s & 0x1F)));
    }
    else if ((op & 0xF00F) == 0x400D) {                             /* SHLD */
      s32 s = (s32)R[mm];
      if (s >= 0)               R[nn] <<= (s & 0x1F);
      else if ((s & 0x1F) == 0) R[nn] = 0;
      else                      R[nn] >>= (32 - (s & 0x1F));
    }
    else if ((op & 0xF0FF) == 0x4000) { T = (int)(R[nn] >> 31); R[nn] <<= 1; }
    else if ((op & 0xF0FF) == 0x4001) { T = (int)(R[nn] & 1); R[nn] >>= 1; }
    else if ((op & 0xF0FF) == 0x4008) R[nn] <<= 2;
    else if ((op & 0xF0FF) == 0x4009) R[nn] >>= 2;
    else if ((op & 0xF0FF) == 0x4018) R[nn] <<= 8;
    else if ((op & 0xF0FF) == 0x4019) R[nn] >>= 8;
    else if ((op & 0xF0FF) == 0x4028) R[nn] <<= 16;
    else if ((op & 0xF0FF) == 0x4029) R[nn] >>= 16;
    else if ((op & 0xF0FF) == 0x4004) { T = (int)(R[nn] >> 31); R[nn] = (R[nn] << 1) | (u32)T; }
    else if ((op & 0xF0FF) == 0x4005) { T = (int)(R[nn] & 1); R[nn] = (R[nn] >> 1) | ((u32)T << 31); }
    else if ((op & 0xF0FF) == 0x4011) T = ((s32)R[nn] >= 0);        /* CMP/PZ */
    else if ((op & 0xF0FF) == 0x4015) T = ((s32)R[nn] > 0);         /* CMP/PL */
    else if ((op & 0xF000) == 0xD000) {                             /* MOV.L @(d,PC) */
      size_t a = ((pc & ~(size_t)3) + 4) + (size_t)(op & 0xFF) * 4;
      if (a + 3 >= n) { snprintf(unmodeled, sizeof unmodeled, "lit OOB"); return 0; }
      R[nn] = ((u32)code[a] << 24) | ((u32)code[a+1] << 16) |
              ((u32)code[a+2] << 8) | code[a+3];
    }
    else if ((op & 0xF000) == 0x5000) {                             /* MOV.L @(d,Rm) */
      if (mm != SH4_REG_BASE) { snprintf(unmodeled, sizeof unmodeled, "5xxx rm=%u", mm); return 0; }
      R[nn] = g_reg[op & 0xF];
    }
    else if ((op & 0xF000) == 0x1000) {                             /* MOV.L Rm,@(d,Rn) */
      if (nn != SH4_REG_BASE) { snprintf(unmodeled, sizeof unmodeled, "1xxx rn=%u", nn); return 0; }
      g_reg[op & 0xF] = R[mm];
    }
    else if ((op & 0xF00F) == 0x6001) {                             /* MOV.W @Rm,Rn */
      u32 base = (u32)(uintptr_t)io_registers;
      u32 off = R[mm] - base;
      if (off >= sizeof(io_registers) - 1) {
        snprintf(unmodeled, sizeof unmodeled, "mov.w @%08X", R[mm]); return 0;
      }
      const u8 *bp = (const u8 *)io_registers + off;
      R[nn] = (u32)(s32)(int16_t)(u16)(((u16)bp[0] << 8) | bp[1]);  /* BE read */
    }
    else if ((op & 0xF00F) == 0x000E) {                             /* MOV.L @(R0,Rm) */
      if (mm != SH4_REG_BASE) { snprintf(unmodeled, sizeof unmodeled, "r0-ld rm=%u", mm); return 0; }
      R[nn] = g_reg[R[0] >> 2];
    }
    else if ((op & 0xF00F) == 0x0006) {                             /* MOV.L Rm,@(R0,Rn) */
      if (nn != SH4_REG_BASE) { snprintf(unmodeled, sizeof unmodeled, "r0-st rn=%u", nn); return 0; }
      g_reg[R[0] >> 2] = R[mm];
    }
    else if ((op & 0xFF00) == 0x8900) {                             /* BT */
      if (T) next = pc + 4 + (size_t)((ptrdiff_t)(s8)(op & 0xFF) * 2);
    }
    else if ((op & 0xFF00) == 0x8B00) {                             /* BF */
      if (!T) next = pc + 4 + (size_t)((ptrdiff_t)(s8)(op & 0xFF) * 2);
    }
    else if ((op & 0xF000) == 0xA000) {                             /* BRA (delay) */
      s32 d = (s32)(op & 0x0FFF); if (d & 0x800) d -= 0x1000;
      size_t tgt = pc + 4 + (size_t)((ptrdiff_t)d * 2);
      u16 slot = (u16)((code[pc+2] << 8) | code[pc+3]);
      if (slot != 0x0009) { snprintf(unmodeled, sizeof unmodeled, "bra slot %04x", slot); return 0; }
      next = tgt;
    }
    else if ((op & 0xF0FF) == 0x402B || (op & 0xF0FF) == 0x400B) {  /* JMP/JSR */
      snprintf(unmodeled, sizeof unmodeled, "jmp/jsr (slow path taken)");
      return 0;
    }
    else {
      snprintf(unmodeled, sizeof unmodeled, "op %04x @%zu", op, pc);
      return 0;
    }
    pc = next;
  }
  g_reg[SH4_GREG_CPSR] = R[SH4_REG_CPSR];    /* commit the R8 cache */
  return 1;
}

/* ---- C-helper reference semantics (mirrors sh4_interp_helpers.c) ---- */
#define RCF_N 0x80000000u
#define RCF_Z 0x40000000u
#define RCF_C 0x20000000u
#define RCF_V 0x10000000u

typedef struct { u32 reg[16]; u32 cpsr; u32 wrote; /* flag bits written */ } ref_state;

static u32 ref_addc(ref_state *st, u32 a, u32 b, u32 cin)
{
  u64 s = (u64)a + b + cin;
  u32 r = (u32)s;
  u32 c = (u32)(s >> 32) & 1;
  u32 v = (~(a ^ b) & (a ^ r)) >> 31;
  st->cpsr &= ~(RCF_N | RCF_Z | RCF_C | RCF_V);
  if (r & 0x80000000u) st->cpsr |= RCF_N;
  if (r == 0)          st->cpsr |= RCF_Z;
  if (c)               st->cpsr |= RCF_C;
  if (v)               st->cpsr |= RCF_V;
  st->wrote = RCF_N | RCF_Z | RCF_C | RCF_V;
  return r;
}

static void ref_set_nz(ref_state *st, u32 r)
{
  st->cpsr &= ~(RCF_N | RCF_Z);
  if (r & 0x80000000u) st->cpsr |= RCF_N;
  if (r == 0)          st->cpsr |= RCF_Z;
  st->wrote = RCF_N | RCF_Z;
}

/* cgba_sh4_thumb_shift_reg semantics, verbatim */
static void ref_shift_reg(ref_state *st, u32 opcode)
{
  u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
  u32 sub = (opcode >> 6) & 0xF;
  u32 val = st->reg[rd];
  u32 amount = st->reg[rs] & 0xFF;
  u32 result = val, carry = (st->cpsr >> 29) & 1;

  switch (sub) {
  case 0x2:
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (32 - amount)) & 1; result = val << amount; }
    else if (amount == 32) { carry = val & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x3:
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = val >> amount; }
    else if (amount == 32) { carry = (val >> 31) & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x4:
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = (u32)((s32)val >> amount); }
    else                   { carry = (val >> 31) & 1; result = (u32)((s32)val >> 31); }
    break;
  default: {
    u32 a = amount & 31;
    if (amount == 0) {}
    else if (a == 0) { carry = (val >> 31) & 1; }
    else { carry = (val >> (a - 1)) & 1; result = (val >> a) | (val << (32 - a)); }
    break;
  }
  }
  st->reg[rd] = result;
  st->cpsr &= ~(RCF_N | RCF_Z | RCF_C);
  if (result & 0x80000000u) st->cpsr |= RCF_N;
  if (result == 0)          st->cpsr |= RCF_Z;
  if (carry)                st->cpsr |= RCF_C;
  st->wrote = RCF_N | RCF_Z | RCF_C;
}

/* cgba_sh4_thumb_dp semantics, verbatim (helper always computes full flags) */
static int ref_thumb_dp(ref_state *st, u32 opcode, u32 pc)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 cin = (st->cpsr >> 29) & 1;

  if (hi >= 0x18 && hi <= 0x1F) {
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 b = (opcode & 0x0400) ? ((opcode >> 6) & 7) : st->reg[(opcode >> 6) & 7];
    u32 a = st->reg[rs];
    st->reg[rd] = (opcode & 0x0200) ? ref_addc(st, a, ~b, 1) : ref_addc(st, a, b, 0);
    return 1;
  }
  if (hi >= 0x20 && hi <= 0x3F) {
    u32 rd = (opcode >> 8) & 7, imm = opcode & 0xFF, a = st->reg[rd];
    switch ((opcode >> 11) & 3) {
    case 0: st->reg[rd] = imm; ref_set_nz(st, imm); break;
    case 1: ref_addc(st, a, ~imm, 1); break;
    case 2: st->reg[rd] = ref_addc(st, a, imm, 0); break;
    default: st->reg[rd] = ref_addc(st, a, ~imm, 1); break;
    }
    return 1;
  }
  if (hi >= 0x40 && hi <= 0x43) {
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 a = st->reg[rd], b = st->reg[rs];
    switch ((opcode >> 6) & 0xF) {
    case 0x0: st->reg[rd] = a & b;  ref_set_nz(st, st->reg[rd]); break;
    case 0x1: st->reg[rd] = a ^ b;  ref_set_nz(st, st->reg[rd]); break;
    case 0x5: st->reg[rd] = ref_addc(st, a, b, cin); break;
    case 0x6: st->reg[rd] = ref_addc(st, a, ~b, cin); break;
    case 0x8: ref_set_nz(st, a & b); break;
    case 0x9: st->reg[rd] = ref_addc(st, ~b, 0, 1); break;
    case 0xA: ref_addc(st, a, ~b, 1); break;
    case 0xB: ref_addc(st, a, b, 0); break;
    case 0xC: st->reg[rd] = a | b;  ref_set_nz(st, st->reg[rd]); break;
    case 0xD: st->reg[rd] = a * b;  ref_set_nz(st, st->reg[rd]); break;
    case 0xE: st->reg[rd] = a & ~b; ref_set_nz(st, st->reg[rd]); break;
    case 0xF: st->reg[rd] = ~b;     ref_set_nz(st, st->reg[rd]); break;
    default: return 0;
    }
    return 1;
  }
  if (hi >= 0x44 && hi <= 0x46) {
    u32 op = (opcode >> 8) & 3;
    u32 rd = (opcode & 7) | ((opcode >> 4) & 8);
    u32 rs = (opcode >> 3) & 0xF;
    u32 a = (rd == 15) ? (pc + 4) : st->reg[rd];
    u32 b = (rs == 15) ? (pc + 4) : st->reg[rs];
    if (op == 1) { ref_addc(st, a, ~b, 1); return 1; }
    if (rd == 15) return 0;                 /* PC-dest: not exercised here */
    st->reg[rd] = (op == 0) ? (a + b) : b;  /* no flags */
    return 1;
  }
  return 0;
}

/* ---- the oracle driver ---- */
static int fails, cases;

static int mask_has(u32 mask, u32 flagbit)   /* liveness bit for a CPSR flag */
{
  switch (flagbit) {
  case RCF_N: return (mask >> 3) & 1;
  case RCF_Z: return (mask >> 2) & 1;
  case RCF_C: return (mask >> 1) & 1;
  default:    return (int)(mask & 1);
  }
}

static void check(const char *what, u32 opcode, u32 mask, const u32 init[16],
                  u32 init_cpsr, int is_shift, u32 pc)
{
  static u8 buf[4096];
  u8 *p = buf;
  int emitted;

  memset(g_reg, 0, sizeof g_reg);
  for (int i = 0; i < 16; i++) g_reg[i] = init[i];
  g_reg[SH4_GREG_CPSR] = init_cpsr;

  if (is_shift)
    emitted = sh4g_thumb_shift_reg_native(&p, opcode, pc, mask);
  else
    emitted = sh4g_thumb_dp_native(&p, opcode, pc, mask, 0);
  if (!emitted)
    return;                                  /* C path: out of scope */
  cases++;

  if (!run_sh4x(buf, (size_t)(p - buf))) {
    printf("FAIL %s op=%04X mask=%X: interpreter: %s\n", what, opcode, mask, unmodeled);
    fails++;
    return;
  }

  ref_state st;
  for (int i = 0; i < 16; i++) st.reg[i] = init[i];
  st.cpsr = init_cpsr; st.wrote = 0;
  if (is_shift) ref_shift_reg(&st, opcode);
  else if (!ref_thumb_dp(&st, opcode, pc)) return;

  for (int i = 0; i < 16; i++) {
    if (g_reg[i] != st.reg[i]) {
      printf("FAIL %s op=%04X mask=%X cin=%u: r%d=%08X want %08X (v=%08X amt-word=%08X)\n",
             what, opcode, mask, (init_cpsr >> 29) & 1, i, g_reg[i], st.reg[i],
             init[opcode & 7], init[(opcode >> 3) & 7]);
      fails++;
      return;
    }
  }
  u32 got = g_reg[SH4_GREG_CPSR], old = init_cpsr, want = st.cpsr;
  for (u32 f = RCF_V; f; f <<= 1) {          /* V,C,Z,N */
    u32 gf = got & f, wf = want & f, of = old & f;
    if (mask_has(mask, f)) {
      if (gf != wf) goto flagfail;
    } else if (st.wrote & f) {
      if (gf != of && gf != wf) goto flagfail;   /* widened-but-correct ok */
    } else {
      if (gf != of) goto flagfail;
    }
    continue;
  flagfail:
    printf("FAIL %s op=%04X mask=%X cin=%u: CPSR=%08X want(masked)=%08X old=%08X wrote=%08X\n",
           what, opcode, mask, (init_cpsr >> 29) & 1, got, want, old, st.wrote);
    fails++;
    return;
  }
  if ((got & 0x0FFFFFFFu) != (old & 0x0FFFFFFFu)) {
    printf("FAIL %s op=%04X: CPSR low bits changed %08X -> %08X\n", what, opcode, old, got);
    fails++;
  }
}

int main(void)
{
  static const u32 vals[] = {
    0, 1, 2, 0x80000000u, 0x80000001u, 0x7FFFFFFFu, 0xFFFFFFFFu,
    0x12345678u, 0xA5A5A5A5u, 0x00000100u, 0xFFFFFF00u
  };
  static const u32 amts[] = {
    0, 1, 2, 7, 15, 31, 32, 33, 40, 63, 64, 65, 96, 128, 159, 160, 255,
    0x100, 0x120, 0xABCD20, 0xFFFFFF00u, 0x80000001u
  };
  static const u32 masks[] = { 0x0, 0x4, 0x8, 0xC, 0xE, 0xF };
  const u32 pc = 0x08000100;
  u32 init[16];

  /* register-amount shifts: LSL(2) LSR(3) ASR(4) ROR(7), incl. rd==rs */
  for (unsigned k = 0; k < 4; k++) {
    static const u32 alu[4] = { 0x2, 0x3, 0x4, 0x7 };
    for (unsigned vi = 0; vi < sizeof vals / sizeof *vals; vi++)
      for (unsigned ai = 0; ai < sizeof amts / sizeof *amts; ai++)
        for (int cin = 0; cin <= 1; cin++)
          for (unsigned mi = 0; mi < sizeof masks / sizeof *masks; mi++) {
            u32 cpsr = ((u32)cin << 29) | 0x9000001Fu;   /* N,V set; SYSTEM */
            for (int i = 0; i < 16; i++) init[i] = 0xC0DE0000u + (u32)i;
            init[1] = vals[vi]; init[2] = amts[ai];
            check("shift", 0x4000u | (alu[k] << 6) | (2u << 3) | 1u,
                  masks[mi], init, cpsr, 1, pc);
            /* rd == rs: value and amount from the same register */
            for (int i = 0; i < 16; i++) init[i] = 0xC0DE0000u + (u32)i;
            init[3] = vals[vi];
            check("shift-same", 0x4000u | (alu[k] << 6) | (3u << 3) | 3u,
                  masks[mi], init, cpsr, 1, pc);
          }
  }

  /* fmt4 ALU (non-shift), fmt2, fmt3, fmt5 through the dp native */
  static const u32 dp_ops[] = {
    /* fmt4: AND EOR ADC SBC TST NEG CMP CMN ORR MUL BIC MVN (rd=1, rs=2) */
    0x4011, 0x4051, 0x4151, 0x4191, 0x4211, 0x4251, 0x4291, 0x42D1,
    0x4311, 0x4351, 0x4391, 0x43D1,
    /* fmt2 ADD/SUB reg + imm3 (rd=1, rs=2, rn/imm=3) */
    0x18D1, 0x1AD1, 0x1CD1, 0x1ED1,
    /* fmt3 MOV/CMP/ADD/SUB r3,#imm */
    0x2300, 0x23FF, 0x2B01, 0x2BFF, 0x3380, 0x3BFF, 0x3B01,
    /* fmt5 hi-reg: ADD r8,r9 / CMP r8,r9 / MOV r8,r9 / vs pc (rs=15) */
    0x44C8, 0x45C8, 0x46C8, 0x447D, 0x457D, 0x467D
  };
  for (unsigned oi = 0; oi < sizeof dp_ops / sizeof *dp_ops; oi++)
    for (unsigned vi = 0; vi < sizeof vals / sizeof *vals; vi++)
      for (unsigned wi = 0; wi < sizeof vals / sizeof *vals; wi += 2)
        for (int cin = 0; cin <= 1; cin++)
          for (unsigned mi = 0; mi < sizeof masks / sizeof *masks; mi++) {
            u32 cpsr = ((u32)cin << 29) | 0x4000001Fu;   /* Z set; SYSTEM */
            for (int i = 0; i < 16; i++) init[i] = 0xBEEF0000u + (u32)i;
            init[1] = vals[vi]; init[2] = vals[wi]; init[3] = vals[vi];
            init[8] = vals[vi]; init[9] = vals[wi]; init[13] = 0x03007F00u;
            check("dp", dp_ops[oi], masks[mi], init, cpsr, 0, pc);
          }

  /* ---- native MSR/MRS: guard chain + masked merge + IO pending check ---- */
  {
    static const u32 psr_ops[] = {
      0xE10F3000u,            /* MRS r3, cpsr */
      0xE328F20Fu,            /* MSR cpsr_f, #0xF0000000 */
      0xE328F000u,            /* MSR cpsr_f, #0 */
      0xE128F002u,            /* MSR cpsr_f, r2 */
      0xE321F093u,            /* MSR cpsr_c, #0x93  (SVC, I set) */
      0xE321F01Fu,            /* MSR cpsr_c, #0x1F  (SYSTEM, I clear) */
      0xE321F09Fu,            /* MSR cpsr_c, #0x9F  (SYSTEM, I set) */
      0xE121F002u,            /* MSR cpsr_c, r2 */
      0xE329F002u,            /* MSR cpsr_fc, r2 */
    };
    static const u32 old_cpsrs[] = {
      0x0000001Fu, 0x0000009Fu, 0x60000012u, 0xF000009Fu, 0x2000001Fu
    };
    static const u32 rvals[] = {
      0x0000001Fu, 0x0000009Fu, 0x9000001Fu, 0xF0000012u, 0x00000010u
    };
    struct iostate { u16 ie, iff, ime; } ios[] = {
      { 0x0001, 0x0000, 1 },  /* nothing pending */
      { 0x0001, 0x0001, 1 },  /* pending + IME on */
      { 0x0001, 0x0001, 0 },  /* pending, IME off */
      { 0x0004, 0x0002, 1 },  /* pending bits disjoint */
    };
    for (unsigned oi = 0; oi < sizeof psr_ops / sizeof *psr_ops; oi++)
      for (unsigned ci = 0; ci < sizeof old_cpsrs / sizeof *old_cpsrs; ci++)
        for (unsigned ri = 0; ri < sizeof rvals / sizeof *rvals; ri++)
          for (unsigned si = 0; si < sizeof ios / sizeof *ios; si++)
            for (int user = 0; user <= 1; user++) {
              u8 *bp;
              u32 opcode = psr_ops[oi];
              u32 oldc = old_cpsrs[ci];
              static u8 pbuf[4096]; u8 *pp = pbuf;

              /* io_registers stores eswap16'd (LE-layout) values */
              bp = (u8 *)&io_registers[0x200 >> 1];
              bp[0] = (u8)(ios[si].ie & 0xFF);  bp[1] = (u8)(ios[si].ie >> 8);
              bp = (u8 *)&io_registers[0x202 >> 1];
              bp[0] = (u8)(ios[si].iff & 0xFF); bp[1] = (u8)(ios[si].iff >> 8);
              bp = (u8 *)&io_registers[0x208 >> 1];
              bp[0] = (u8)(ios[si].ime & 0xFF); bp[1] = (u8)(ios[si].ime >> 8);

              if (!sh4g_arm_psr_native(&pp, opcode, pc, 0))
                continue;                       /* C-path form: out of scope */
              cases++;

              memset(g_reg, 0, sizeof g_reg);
              for (int i = 0; i < 16; i++) g_reg[i] = 0xFACE0000u + (u32)i;
              g_reg[2] = rvals[ri];
              g_reg[SH4_GREG_CPSR] = oldc;
              g_reg[SH4_GREG_CPU_MODE] = user ? 0x00 : 0x10;

              int ran = run_sh4x(pbuf, (size_t)(pp - pbuf));
              int slow = (!ran && strstr(unmodeled, "slow path"));
              if (!ran && !slow) {
                printf("FAIL psr op=%08X: interpreter: %s\n", opcode, unmodeled);
                fails++; continue;
              }

              /* reference: which route must be taken, and the merge result */
              u32 is_msr = (opcode >> 21) & 1;
              if (!is_msr) {                    /* MRS r3 */
                if (slow || g_reg[3] != oldc) {
                  printf("FAIL mrs: slow=%d r3=%08X want %08X\n", slow, g_reg[3], oldc);
                  fails++;
                }
                continue;
              }
              u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
              u32 val = (opcode & 0x02000000u)
                ? ({ u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
                     rot ? ((imm >> rot) | (imm << (32 - rot))) : imm; })
                : rvals[ri];
              u32 mask = (pfield == 2) ? 0xF0000000u
                       : (pfield == 3) ? 0xF00000EFu : 0x000000EFu;
              u32 merged = (val & mask) | (oldc & ~mask);
              int want_slow = 0;
              if (pfield != 2) {
                if (user) want_slow = 1;
                else if ((oldc ^ merged) & 0x3F) want_slow = 1;
                else if (!(merged & 0x80) &&
                         (ios[si].ie & ios[si].iff) && (ios[si].ime & 1))
                  want_slow = 1;
              }
              if (want_slow != slow) {
                printf("FAIL msr op=%08X old=%08X val=%08X user=%d io=%u: "
                       "slow=%d want %d\n", opcode, oldc, val, user, si, slow, want_slow);
                fails++; continue;
              }
              if (!slow && g_reg[SH4_GREG_CPSR] != merged) {
                printf("FAIL msr op=%08X old=%08X val=%08X: CPSR=%08X want %08X\n",
                       opcode, oldc, val, g_reg[SH4_GREG_CPSR], merged);
                fails++;
              }
            }
  }

  if (fails) {
    printf("SH4 EXEC ORACLE FAILED: %d of %d cases\n", fails, cases);
    return 1;
  }
  printf("SH4 exec oracle passed (%d native cases)\n", cases);
  return 0;
}
