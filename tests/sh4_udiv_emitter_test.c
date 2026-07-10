/*
 * Integration checks for the signature-gated Thumb __udivsi3 entry prefix.
 *
 * Unlike sh4_udiv_fastpath_test.c (the target-independent arithmetic model),
 * this test emits the real SH4 prefix from sh4_emit.h and executes it in a
 * small SH4 oracle. The shared op2 trampoline is simulated at its ABI seam,
 * then the real cgba_sh4_thumb_udiv_loop_try() from sh4_interp_helpers.c runs.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGBA_SH4_THUMB_UDIV_FASTPATH 1
#include "vendor/gpsp/common.h"

/* sh4_emit.h is normally included after cheats.h by cpu_threaded.c. */
extern u32 cheat_master_hook;
#include "vendor/gpsp/sh4/sh4_emit.h"

u32 reg[64];
u8 ws_cyc_seq[16][2];
u8 ws_cyc_nseq[16][2];
u32 cheat_master_hook;
u32 idle_loop_target_pc;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];

/* Defined by the production helper translation unit linked into this test. */
extern int cgba_dynarec_single_block;
extern int cgba_sh4_extra_cycles;

/* Their host addresses are embedded in emitted code but never called as host
 * functions: the oracle recognizes both ABI boundaries explicitly. */
void sh4_op2_tramp(void) {}
void sh4_pc_redispatch(u32 pc) { (void)pc; }

typedef enum oracle_window_kind {
  ORACLE_BYTES_BE,
  ORACLE_NATIVE_S32
} oracle_window_kind;

typedef struct oracle_window {
  u32 base;
  u32 size;
  void *host;
  oracle_window_kind kind;
} oracle_window;

typedef struct oracle_result {
  u32 cycles;
  u32 cpsr;
  u32 redispatch_pc;
  u32 helper_opcode;
  u32 helper_pc;
  unsigned helper_calls;
  unsigned redispatches;
  int reached_fallback;
  char error[96];
} oracle_result;

#define ORACLE_MAX_WINDOWS 8
static oracle_window oracle_windows[ORACLE_MAX_WINDOWS];
static unsigned oracle_window_count;

static void oracle_reset_windows(void)
{
  oracle_window_count = 0;
}

static void oracle_add_window(void *host, u32 size, oracle_window_kind kind)
{
  oracle_window *w;
  if (oracle_window_count >= ORACLE_MAX_WINDOWS) {
    fprintf(stderr, "too many oracle windows\n");
    exit(2);
  }
  w = &oracle_windows[oracle_window_count++];
  w->base = (u32)(uintptr_t)host;
  w->size = size;
  w->host = host;
  w->kind = kind;
}

static oracle_window *oracle_resolve(u32 address, u32 size, u32 *offset)
{
  unsigned i;
  for (i = 0; i < oracle_window_count; i++) {
    oracle_window *w = &oracle_windows[i];
    u32 off = address - w->base;
    if (off <= w->size && size <= w->size - off) {
      *offset = off;
      return w;
    }
  }
  return NULL;
}

static u32 oracle_read(u32 address, unsigned size, int *ok)
{
  u32 off;
  oracle_window *w = oracle_resolve(address, size, &off);
  const u8 *p;
  if (!w) {
    *ok = 0;
    return 0;
  }
  if (w->kind == ORACLE_NATIVE_S32) {
    if (size != 4 || off != 0) {
      *ok = 0;
      return 0;
    }
    return (u32)*(volatile s32 *)w->host;
  }
  p = (const u8 *)w->host + off;
  if (size == 1)
    return p[0];
  if (size == 2)
    return ((u32)p[0] << 8) | p[1];
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
         ((u32)p[2] << 8) | p[3];
}

static void oracle_write(u32 address, u32 value, unsigned size, int *ok)
{
  u32 off;
  oracle_window *w = oracle_resolve(address, size, &off);
  u8 *p;
  if (!w) {
    *ok = 0;
    return;
  }
  if (w->kind == ORACLE_NATIVE_S32) {
    if (size != 4 || off != 0) {
      *ok = 0;
      return;
    }
    *(volatile s32 *)w->host = (s32)value;
    return;
  }
  p = (u8 *)w->host + off;
  if (size == 1) {
    p[0] = (u8)value;
  } else if (size == 2) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
  } else {
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
  }
}

static void oracle_error(oracle_result *out, const char *what, u32 pc, u16 op)
{
  snprintf(out->error, sizeof(out->error), "%s at %08X (op %04X)",
           what, pc, op);
}

/* Execute exactly the opcodes emitted by sh4g_thumb_udiv_loop_entry(). This
 * is the same windowed big-endian model used by tests/sh4_exec_oracle.c,
 * trimmed to the entry prefix's instruction set. */
static oracle_result oracle_run(const u8 *code, size_t code_size,
                                const u8 *vec, u32 initial_cycles)
{
  oracle_result out;
  u32 r[16] = {0};
  u32 pc = (u32)(uintptr_t)code;
  u32 end = pc + (u32)code_size;
  int t = 0;
  unsigned steps = 0;

  memset(&out, 0, sizeof(out));
  r[SH4_REG_CYCLES] = initial_cycles;
  r[SH4_REG_CPSR] = reg[REG_CPSR];
  r[SH4_REG_VEC] = (u32)(uintptr_t)vec;

  while (pc != end) {
    int ok = 1;
    u16 op;
    unsigned n, m;
    u32 next;
    if (++steps > 256) {
      oracle_error(&out, "step limit", pc, 0);
      break;
    }
    op = (u16)oracle_read(pc, 2, &ok);
    if (!ok) {
      oracle_error(&out, "fetch miss", pc, 0);
      break;
    }
    n = (op >> 8) & 0xFu;
    m = (op >> 4) & 0xFu;
    next = pc + 2;

    if (op == 0x0009) {
      /* NOP */
    } else if ((op & 0xF000u) == 0xE000u) {
      r[n] = (u32)(s32)(s8)(op & 0xFFu);             /* MOV #imm,Rn */
    } else if ((op & 0xF00Fu) == 0x600Cu) {
      r[n] = r[m] & 0xFFu;                           /* EXTU.B Rm,Rn */
    } else if ((op & 0xF00Fu) == 0x6007u) {
      r[n] = ~r[m];                                  /* NOT Rm,Rn */
    } else if ((op & 0xF0FFu) == 0x4008u) {
      r[n] <<= 2;                                    /* SHLL2 Rn */
    } else if ((op & 0xF0FFu) == 0x4018u) {
      r[n] <<= 8;                                    /* SHLL8 Rn */
    } else if ((op & 0xF0FFu) == 0x4028u) {
      r[n] <<= 16;                                   /* SHLL16 Rn */
    } else if ((op & 0xF000u) == 0xD000u) {
      u32 address = ((pc & ~3u) + 4u) + (u32)(op & 0xFFu) * 4u;
      r[n] = oracle_read(address, 4, &ok);            /* MOV.L @(d,PC),Rn */
      if (!ok) {
        oracle_error(&out, "literal miss", pc, op);
        break;
      }
    } else if ((op & 0xF000u) == 0x5000u) {
      r[n] = oracle_read(r[m] + (u32)(op & 0xFu) * 4u, 4, &ok);
      if (!ok) {
        oracle_error(&out, "vector load miss", pc, op);
        break;
      }
    } else if ((op & 0xF00Fu) == 0x2002u) {
      oracle_write(r[n], r[m], 4, &ok);               /* MOV.L Rm,@Rn */
      if (!ok) {
        oracle_error(&out, "store miss", pc, op);
        break;
      }
    } else if ((op & 0xF00Fu) == 0x6002u) {
      r[n] = oracle_read(r[m], 4, &ok);               /* MOV.L @Rm,Rn */
      if (!ok) {
        oracle_error(&out, "load miss", pc, op);
        break;
      }
    } else if ((op & 0xF00Fu) == 0x3008u) {
      r[n] -= r[m];                                   /* SUB Rm,Rn */
    } else if ((op & 0xF00Fu) == 0x2008u) {
      t = (r[n] & r[m]) == 0;                         /* TST Rm,Rn */
    } else if ((op & 0xFF00u) == 0x8900u) {
      if (t)
        next = pc + 4u + (u32)((s32)(s8)(op & 0xFFu) * 2);
    } else if ((op & 0xF000u) == 0xA000u) {
      s32 disp = (s32)(op & 0x0FFFu);
      u16 slot = (u16)oracle_read(pc + 2, 2, &ok);
      if (disp & 0x800)
        disp -= 0x1000;
      if (!ok || slot != 0x0009u) {
        oracle_error(&out, "bad BRA delay slot", pc, op);
        break;
      }
      next = pc + 4u + (u32)(disp * 2);               /* delay NOP folded */
    } else if ((op & 0xF0FFu) == 0x400Bu) {
      u16 slot = (u16)oracle_read(pc + 2, 2, &ok);
      u32 pr = pc + 4u;
      if (!ok || slot != 0x0009u ||
          r[n] != (u32)(uintptr_t)sh4_op2_tramp) {
        oracle_error(&out, "unexpected JSR", pc, op);
        break;
      }

      /* sh4_op2_tramp uses PR as its literal cursor: +8 is fn, then opcode
       * and guest PC. Flush/reload cached CPSR around the real C helper. */
      {
        u32 fn = oracle_read(pr + 8u, 4, &ok);
        u32 opcode = oracle_read(pr + 12u, 4, &ok);
        u32 guest_pc = oracle_read(pr + 16u, 4, &ok);
        if (!ok || fn != (u32)(uintptr_t)cgba_sh4_thumb_udiv_loop_try) {
          oracle_error(&out, "bad trampoline tuple", pc, op);
          break;
        }
        out.helper_calls++;
        out.helper_opcode = opcode;
        out.helper_pc = guest_pc;
        reg[REG_CPSR] = r[SH4_REG_CPSR];
        r[SH4_REG_RET] = (u32)cgba_sh4_thumb_udiv_loop_try(opcode, guest_pc);
        r[SH4_REG_CPSR] = reg[REG_CPSR];
      }
      next = pr;                                      /* simulated RTS */
    } else if ((op & 0xF0FFu) == 0x402Bu) {
      u16 slot = (u16)oracle_read(pc + 2, 2, &ok);
      if (!ok || slot != 0x0009u ||
          r[n] != (u32)(uintptr_t)sh4_pc_redispatch) {
        oracle_error(&out, "unexpected JMP", pc, op);
        break;
      }
      out.redispatches++;
      out.redispatch_pc = r[SH4_REG_ARG0];
      pc = end;                                       /* terminal ABI seam */
      continue;
    } else {
      oracle_error(&out, "unmodeled instruction", pc, op);
      break;
    }
    pc = next;
  }

  if (!out.error[0] && out.redispatches == 0)
    out.reached_fallback = 1;
  out.cycles = r[SH4_REG_CYCLES];
  out.cpsr = r[SH4_REG_CPSR];
  return out;
}

static _Alignas(32) u8 code[1024];
static _Alignas(4) u8 rom_map[0x8000];
static _Alignas(4) u8 vector_table[SH4G_VEC_COUNT * 4];
static int failures;

static void put_be32(u8 *p, u32 value)
{
  p[0] = (u8)(value >> 24);
  p[1] = (u8)(value >> 16);
  p[2] = (u8)(value >> 8);
  p[3] = (u8)value;
}

static void reset_observers(void)
{
  cheat_master_hook = ~0u;
  idle_loop_target_pc = ~0u;
  translation_gate_targets = 0;
  memset(translation_gate_target_pc, 0, sizeof(translation_gate_target_pc));
}

static void install_pattern(u32 pc)
{
  u32 off = pc & 0x7FFFu;
  unsigned i;
  memset(rom_map, 0xA5, sizeof(rom_map));
  for (i = 0; i < CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS; i++)
    address16(rom_map, off + i * 2u) =
      eswap16(cgba_sh4_thumb_udiv_loop_pattern[i]);
}

static size_t emit_prefix(u32 pc)
{
  u8 *p = code;
  memset(code, 0xCC, sizeof(code));
  sh4g_thumb_udiv_loop_entry(&p, rom_map, pc);
  return (size_t)(p - code);
}

static void failf(const char *what)
{
  fprintf(stderr, "FAIL: %s\n", what);
  failures++;
}

static void check_signature_and_observers(void)
{
  const u32 pc = 0x081361F0u;
  unsigned i;
  size_t emitted;

  install_pattern(pc);
  reset_observers();
  if (!sh4g_thumb_udiv_loop_match_map(rom_map, pc))
    failf("canonical opcode-map signature rejected");
  emitted = emit_prefix(pc);
  if (emitted == 0)
    failf("canonical signature emitted no entry prefix");

  for (i = 0; i < CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS; i++) {
    u32 off = (pc & 0x7FFFu) + i * 2u;
    u16 saved = address16(rom_map, off);
    address16(rom_map, off) = (u16)(saved ^ eswap16(1u));
    if (sh4g_thumb_udiv_loop_match_map(rom_map, pc) || emit_prefix(pc) != 0) {
      fprintf(stderr, "FAIL: mutated signature word %u accepted\n", i);
      failures++;
    }
    address16(rom_map, off) = saved;
  }

  if (sh4g_thumb_udiv_loop_match_map(rom_map, 0x08007FF0u))
    failf("cross-map signature accepted");

  cheat_master_hook = pc;
  if (!sh4g_thumb_udiv_loop_has_observer(pc) || emit_prefix(pc) != 0)
    failf("cheat observer did not suppress prefix");
  reset_observers();
  idle_loop_target_pc = pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES - 2u;
  if (!sh4g_thumb_udiv_loop_has_observer(pc) || emit_prefix(pc) != 0)
    failf("idle observer did not suppress prefix");
  reset_observers();
  translation_gate_targets = 1;
  translation_gate_target_pc[0] = pc + 20u;
  if (!sh4g_thumb_udiv_loop_has_observer(pc) || emit_prefix(pc) != 0)
    failf("translation-gate observer did not suppress prefix");

  reset_observers();
  cheat_master_hook = pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES;
  idle_loop_target_pc = pc - 2u;
  translation_gate_targets = 1;
  translation_gate_target_pc[0] = pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES;
  if (sh4g_thumb_udiv_loop_has_observer(pc) || emit_prefix(pc) == 0)
    failf("out-of-range observer suppressed prefix");
  reset_observers();
}

static void check_budget_path(u32 initial_budget, int expect_hit)
{
  const u32 pc = 0x081361F0u;
  const u32 tail_pc = pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES;
  const u32 initial_cpsr = 0xA000003Fu; /* Thumb state + preserved low bits. */
  const u32 initial_regs[5] = {
    0xFEDCBA98u, 0x10000000u, 0x01200000u, 0x10000000u, 0xDEADBEEFu
  };
  u32 before[64];
  cgba_sh4_thumb_udiv_loop_result model;
  oracle_result got;
  size_t code_size;
  unsigned i;

  install_pattern(pc);
  reset_observers();
  memset(reg, 0x5A, sizeof(reg));
  for (i = 0; i < 5; i++)
    reg[i] = initial_regs[i];
  reg[REG_CPSR] = initial_cpsr;
  memcpy(before, reg, sizeof(before));
  memset(ws_cyc_seq, 0, sizeof(ws_cyc_seq));
  memset(ws_cyc_nseq, 0, sizeof(ws_cyc_nseq));
  ws_cyc_seq[(pc >> 24) & 0xFu][0] = 3;
  ws_cyc_nseq[(pc >> 24) & 0xFu][0] = 5;
  cgba_dynarec_single_block = 0;
  cgba_sh4_extra_cycles = 0x13579BDF;
  cgba_sh4_thumb_udiv_budget = -123;

  model = cgba_sh4_thumb_udiv_loop_run(
    initial_regs[0], initial_regs[1], initial_regs[2], initial_regs[3],
    initial_regs[4], initial_cpsr, 3, 5);
  code_size = emit_prefix(pc);
  if (code_size == 0) {
    failf("budget test emitted no prefix");
    return;
  }

  memset(vector_table, 0, sizeof(vector_table));
  put_be32(vector_table + SH4G_VEC_pc_redispatch * 4u,
           (u32)(uintptr_t)sh4_pc_redispatch);
  oracle_reset_windows();
  oracle_add_window(code, (u32)code_size, ORACLE_BYTES_BE);
  oracle_add_window(vector_table, sizeof(vector_table), ORACLE_BYTES_BE);
  oracle_add_window((void *)&cgba_sh4_thumb_udiv_budget, 4,
                    ORACLE_NATIVE_S32);
  oracle_add_window((void *)&cgba_sh4_extra_cycles, 4, ORACLE_NATIVE_S32);
  got = oracle_run(code, code_size, vector_table, initial_budget);

  if (got.error[0]) {
    fprintf(stderr, "FAIL: oracle: %s\n", got.error);
    failures++;
    return;
  }
  if (got.helper_calls != 1 ||
      got.helper_opcode != cgba_sh4_thumb_udiv_loop_pattern[0] ||
      got.helper_pc != pc)
    failf("trampoline tuple/call count mismatch");
  if ((u32)cgba_sh4_thumb_udiv_budget != initial_budget)
    failf("entry prefix did not snapshot R13 budget");

  if (!expect_hit) {
    if (!got.reached_fallback || got.redispatches != 0)
      failf("budget decline did not fall through ordinary block");
    if (got.cycles != initial_budget || got.cpsr != initial_cpsr)
      failf("budget decline changed cached host state");
    if (memcmp(reg, before, sizeof(before)) != 0)
      failf("budget decline changed guest register state");
    if (cgba_sh4_extra_cycles != 0x13579BDF)
      failf("budget decline changed extra-cycle state");
    return;
  }

  if (got.reached_fallback || got.redispatches != 1 ||
      got.redispatch_pc != tail_pc)
    failf("successful prefix did not redispatch at mov/pop/return tail");
  if (got.cycles != initial_budget - model.cycles)
    failf("successful prefix did not debit exact dynamic cycles");
  if (reg[0] != model.r0 || reg[1] != model.r1 ||
      reg[2] != model.r2 || reg[3] != model.r3 || reg[4] != model.r4 ||
      reg[REG_CPSR] != model.cpsr || got.cpsr != model.cpsr)
    failf("successful helper commit differs from exact model");
  for (i = 5; i < 64; i++) {
    if (i != REG_CPSR && reg[i] != before[i]) {
      failf("successful helper changed an unrelated guest register");
      break;
    }
  }
  if ((u32)cgba_sh4_extra_cycles != model.cycles)
    failf("successful helper published wrong dynamic debit");
}

int main(void)
{
  cgba_sh4_thumb_udiv_loop_result model;

  check_signature_and_observers();
  model = cgba_sh4_thumb_udiv_loop_run(
    0xFEDCBA98u, 0x10000000u, 0x01200000u, 0x10000000u,
    0xDEADBEEFu, 0xA000003Fu, 3, 5);
  check_budget_path(model.cycles, 0);       /* strict boundary: decline */
  check_budget_path(model.cycles + 1u, 1);  /* one cycle remains: commit */

  if (failures)
    return 1;
  puts("sh4 udiv emitter: signature/observer/trampoline/budget/redispatch OK");
  return 0;
}
