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
#include <stdlib.h>

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
u32 reg_mode[7][7];
u32 spsr[6];
/* Emitted code reads these u32 arrays through the orc windows, which model
 * the SH4's big-endian byte order over host (LE) buffers — so the harness
 * copies hold byte-swapped values (see the io_registers eswap16 note in the
 * PSR section). The logical table feeds the reference model. */
static const u32 cpu_modes_logical[16] =
{
  0x00, 0x12, 0x11, 0x13, 0x16, 0x16, 0x16, 0x14,
  0x16, 0x16, 0x16, 0x15, 0x16, 0x16, 0x16, 0x10
};
#define ORC_BSWAP32(x) __builtin_bswap32(x)
const u32 cpu_modes[16] =
{
  ORC_BSWAP32(0x00), ORC_BSWAP32(0x12), ORC_BSWAP32(0x11), ORC_BSWAP32(0x13),
  ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x14),
  ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x15),
  ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x16), ORC_BSWAP32(0x10)
};
u8 *memory_map_read[8192];
u8 iwram[0x10000];                 /* gpSP global (push_iwram fast path) */
u8 ws_cyc_seq[16][2], ws_cyc_nseq[16][2];
u32 cgba_sh4_native_thumb_const_io_count, cgba_sh4_native_thumb_runtime_io_count;
int cgba_sh4_extra_cycles;
int cgba_sh4_thumb_ldst(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_arm_ldst(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_arm_psr(u32 o, u32 p){(void)o;(void)p;return 0;}
void sh4_block_exit(u32 pc){(void)pc;}
void sh4_helper_exit(u32 pc){(void)pc;}
void sh4_op2_pc_mem_tramp(void){}
void sh4_op2_pc_tramp(void){}
void sh4_op2_tramp(void){}
void sh4_indirect_branch_thumb(u32 a){(void)a;}
void sh4_pc_redispatch(u32 pc){(void)pc;}
void sh4_update_gba(u32 pc){(void)pc;}
void sh4_op2_mem_tramp(void){}
void sh4_headless_trace_op(char k, u32 pc, u32 op){(void)k;(void)pc;(void)op;}
void sh4_indirect_branch_arm(u32 a){(void)a;}
void sh4_indirect_branch_dual(u32 a){(void)a;}
void sh4_indirect_branch_dual_thumb_current(u32 a){(void)a;}
void execute_swi(u32 pc){(void)pc;}
void sh4_cheat_hook(void){}
u32 cgba_sh4_hle_div(u32 o, u32 p){(void)o;(void)p;return 0;}

/* Host analog of cgba_sh4_vec_table (sh4_stub.S): entries stored BIG-ENDIAN so
 * the mini-interpreter's byte-wise MOV.L read returns the truncated host
 * address, matching the calculator layout. R9 points here; R10 holds
 * sh4_block_exit directly. Filled in orc_vec_init(). */
static u8 orc_vec_table[12 * 4];
static void orc_vec_put(unsigned idx, const void *pf)
{
  u32 v = (u32)(uintptr_t)pf;
  orc_vec_table[idx * 4 + 0] = (u8)(v >> 24);
  orc_vec_table[idx * 4 + 1] = (u8)(v >> 16);
  orc_vec_table[idx * 4 + 2] = (u8)(v >> 8);
  orc_vec_table[idx * 4 + 3] = (u8)v;
}

#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
int cgba_sh4_arm_block(u32 o, u32 p);
int cgba_sh4_thumb_block(u32 o, u32 p);
int cgba_sh4_arm_block(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_thumb_block(u32 o, u32 p){(void)o;(void)p;return 0;}
u32 cgba_sh4_native_thumb_push_iwram_count2;
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"
#include "ports/fxcg100/sh4/sh4_thumb_block_emit.h"

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
u8 *cgba_sh4_psr_rebank_routine;
u32 cgba_idle_wait;
static u8 psr_rebank_buf[512];

static void orc_vec_init(void)
{
  orc_vec_put(SH4G_VEC_pc_redispatch, (const void *)sh4_pc_redispatch);
  orc_vec_put(SH4G_VEC_ib_arm, (const void *)sh4_indirect_branch_arm);
  orc_vec_put(SH4G_VEC_ib_thumb, (const void *)sh4_indirect_branch_thumb);
  orc_vec_put(SH4G_VEC_ib_dual, (const void *)sh4_indirect_branch_dual);
  orc_vec_put(SH4G_VEC_ib_dual_thumb_current,
              (const void *)sh4_indirect_branch_dual_thumb_current);
  orc_vec_put(SH4G_VEC_update_gba, (const void *)sh4_update_gba);
  orc_vec_put(SH4G_VEC_helper_exit, (const void *)sh4_helper_exit);
  orc_vec_put(SH4G_VEC_execute_swi, (const void *)execute_swi);
  orc_vec_put(SH4G_VEC_cheat_hook, (const void *)sh4_cheat_hook);
  orc_vec_put(SH4G_VEC_hle_div, (const void *)cgba_sh4_hle_div);
  orc_vec_put(SH4G_VEC_ws_cyc_seq, (const void *)ws_cyc_seq);
  orc_vec_put(SH4G_VEC_ws_cyc_nseq, (const void *)ws_cyc_nseq);
}

/* ---- guest state used by the interpreter ---- */
static u32 g_reg[64];

/* ---- SH4 mini-interpreter -------------------------------------------------
 * Virtual addresses are 32-bit-truncated HOST pointers, resolved through
 * registered memory windows — so emitted literals holding &io_registers,
 * &memory_map_read, page pointers and the fastmem routine buffer all work.
 * Supports calls (JSR/RTS/STS PR), delay slots, and detects the fastmem
 * guard-failure far_jmp into sh4_op2_pc_mem_tramp as the "slow path". */
static char unmodeled[64];

#define ORC_MAX_WIN 24
static struct { u32 base; u32 size; u8 *host; int is_maptab; } orc_win[ORC_MAX_WIN];
static int orc_nwin;

static void orc_reset_windows(void) { orc_nwin = 0; }
static void orc_add_window(const void *host, u32 size, int is_maptab)
{
  orc_win[orc_nwin].base = (u32)(uintptr_t)host;
  orc_win[orc_nwin].size = size;
  orc_win[orc_nwin].host = (u8 *)host;
  orc_win[orc_nwin].is_maptab = is_maptab;
  orc_nwin++;
}
static u8 *orc_resolve(u32 addr, int *is_maptab)
{
  for (int i = 0; i < orc_nwin; i++) {
    u32 off = addr - orc_win[i].base;
    if (off < orc_win[i].size) {
      if (is_maptab) *is_maptab = orc_win[i].is_maptab;
      return orc_win[i].host + off;
    }
  }
  if (getenv("ORC_DEBUG")) {
    fprintf(stderr, "resolve miss %08X; windows:", addr);
    for (int i = 0; i < orc_nwin; i++)
      fprintf(stderr, " [%08X+%X]", orc_win[i].base, orc_win[i].size);
    fprintf(stderr, "\n");
  }
  return NULL;
}
static u32 orc_read(u32 addr, int size_log2, int *ok)
{
  int maptab = 0;
  u8 *hp = orc_resolve(addr, &maptab);
  if (!hp) { *ok = 0; return 0; }
  if (maptab) {                    /* pointer table: emitted code indexes
                                      4-byte slots; host slots are 8 bytes */
    u32 idx = (u32)(hp - (u8 *)(void *)memory_map_read) / 4;
    return (u32)(uintptr_t)memory_map_read[idx];
  }
  switch (size_log2) {             /* big-endian, as the SH4 would */
  case 0: return hp[0];
  case 1: return (u32)((hp[0] << 8) | hp[1]);
  default: return ((u32)hp[0] << 24) | ((u32)hp[1] << 16) |
                  ((u32)hp[2] << 8) | hp[3];
  }
}
static void orc_write(u32 addr, u32 v, int size_log2, int *ok)
{
  u8 *hp = orc_resolve(addr, NULL);
  if (!hp) { *ok = 0; return; }
  switch (size_log2) {
  case 0: hp[0] = (u8)v; break;
  case 1: hp[0] = (u8)(v >> 8); hp[1] = (u8)v; break;
  default: hp[0] = (u8)(v >> 24); hp[1] = (u8)(v >> 16);
           hp[2] = (u8)(v >> 8);  hp[3] = (u8)v; break;
  }
}

static u32 orc_slow_target;        /* trunc(&sh4_op2_pc_mem_tramp) */
static u32 orc_slow_target2;       /* trunc(&sh4_op2_pc_tramp) */
static int orc_took_slow;

static int orc_is_slow_target(u32 a)
{ return a == orc_slow_target || a == orc_slow_target2; }

static int run_at(u32 pc, u32 pc_end)
{
  u32 R[16] = {0};
  u32 MACL = 0, PR = 0;
  int T = 0;
  int steps = 0;

  R[SH4_REG_BASE] = 0;
  R[SH4_REG_CPSR] = g_reg[SH4_GREG_CPSR];
  R[SH4_REG_VEC] = (u32)(uintptr_t)orc_vec_table;
  R[SH4_REG_BEXIT] = (u32)(uintptr_t)sh4_block_exit;
  unmodeled[0] = 0;
  orc_took_slow = 0;

  while (pc != pc_end) {
    int okf = 1;
    if (++steps > 40000) { snprintf(unmodeled, sizeof unmodeled, "step cap"); return 0; }
    u32 opw = orc_read(pc, 1, &okf);
    if (!okf) { snprintf(unmodeled, sizeof unmodeled, "fetch @%08X", pc); return 0; }
    u16 op = (u16)opw;
    unsigned nn = (op >> 8) & 0xF, mm = (op >> 4) & 0xF;
    u32 next = pc + 2;

    if      (op == 0x0009) { /* NOP */ }
    else if (op == 0x0018) T = 1;
    else if (op == 0x0008) T = 0;
    else if (op == 0x000B) {                                        /* RTS */
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf || (u16)slot != 0x0009) { snprintf(unmodeled, sizeof unmodeled, "rts slot"); return 0; }
      next = PR;
    }
    else if ((op & 0xF0FF) == 0x400B) {                             /* JSR @Rn */
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf || (u16)slot != 0x0009) { snprintf(unmodeled, sizeof unmodeled, "jsr slot"); return 0; }
      if (orc_is_slow_target(R[nn])) {          /* call into a C tramp stub */
        orc_took_slow = 1;
        snprintf(unmodeled, sizeof unmodeled, "jmp/jsr (slow path taken)");
        return 0;
      }
      PR = pc + 4;
      next = R[nn];
    }
    else if ((op & 0xF0FF) == 0x402B) {                             /* JMP @Rn */
      if (orc_is_slow_target(R[nn])) {
        orc_took_slow = 1;
        snprintf(unmodeled, sizeof unmodeled, "jmp/jsr (slow path taken)");
        return 0;
      }
      snprintf(unmodeled, sizeof unmodeled, "jmp %08X", R[nn]);
      return 0;
    }
    else if ((op & 0xF0FF) == 0x002A) R[nn] = PR;                   /* STS PR,Rn */
    else if ((op & 0xF000) == 0xE000) R[nn] = (u32)(s32)(s8)(op & 0xFF);
    else if ((op & 0xF000) == 0x7000) R[nn] += (u32)(s32)(s8)(op & 0xFF);
    else if ((op & 0xFF00) == 0x8800) T = ((s32)R[0] == (s32)(s8)(op & 0xFF));
    else if ((op & 0xFF00) == 0xC900) R[0] &= (u32)(op & 0xFF);
    else if ((op & 0xFF00) == 0xC800) T = ((R[0] & (u32)(op & 0xFF)) == 0);
    else if ((op & 0xF00F) == 0x6003) R[nn] = R[mm];
    else if ((op & 0xF00F) == 0x6007) R[nn] = ~R[mm];
    else if ((op & 0xF00F) == 0x600B) R[nn] = 0u - R[mm];
    else if ((op & 0xF00F) == 0x6008) {
      u32 v = R[mm];
      R[nn] = (v & 0xFFFF0000u) | ((v >> 8) & 0xFF) | ((v & 0xFF) << 8);
    }
    else if ((op & 0xF00F) == 0x6009) R[nn] = (R[mm] >> 16) | (R[mm] << 16); /* SWAP.W */
    else if ((op & 0xF00F) == 0x600C) R[nn] = R[mm] & 0xFF;
    else if ((op & 0xF00F) == 0x600D) R[nn] = R[mm] & 0xFFFF;
    else if ((op & 0xF00F) == 0x600E) R[nn] = (u32)(s32)(s8)R[mm];
    else if ((op & 0xF00F) == 0x600F) R[nn] = (u32)(s32)(int16_t)R[mm];
    else if ((op & 0xF00F) == 0x2008) T = ((R[mm] & R[nn]) == 0);
    else if ((op & 0xF00F) == 0x2009) R[nn] &= R[mm];
    else if ((op & 0xF00F) == 0x200A) R[nn] ^= R[mm];
    else if ((op & 0xF00F) == 0x200B) R[nn] |= R[mm];
    else if ((op & 0xF00F) == 0x3000) T = (R[nn] == R[mm]);
    else if ((op & 0xF00F) == 0x3002) T = (R[nn] >= R[mm]);
    else if ((op & 0xF00F) == 0x3006) T = (R[nn] > R[mm]);
    else if ((op & 0xF00F) == 0x3008) R[nn] -= R[mm];
    else if ((op & 0xF00F) == 0x300C) R[nn] += R[mm];
    else if ((op & 0xF00F) == 0x300E) {
      u64 sm = (u64)R[nn] + R[mm] + (u32)T;
      R[nn] = (u32)sm; T = (int)(sm >> 32);
    }
    else if ((op & 0xF00F) == 0x300F) {
      u32 a = R[nn], b = R[mm], r = a + b;
      T = (int)((~(a ^ b) & (a ^ r)) >> 31); R[nn] = r;
    }
    else if ((op & 0xF00F) == 0x300A) {
      u64 sm = (u64)R[nn] - R[mm] - (u32)T;
      R[nn] = (u32)sm; T = (int)((sm >> 32) & 1);
    }
    else if ((op & 0xF00F) == 0x300B) {
      u32 a = R[nn], b = R[mm], r = a - b;
      T = (int)(((a ^ b) & (a ^ r)) >> 31); R[nn] = r;
    }
    else if ((op & 0xF00F) == 0x0007) MACL = R[nn] * R[mm];
    else if ((op & 0xF0FF) == 0x001A) R[nn] = MACL;
    else if ((op & 0xF0FF) == 0x0029) R[nn] = (u32)T;
    else if ((op & 0xF0FF) == 0x4010) { R[nn] -= 1; T = (R[nn] == 0); }  /* DT */
    else if ((op & 0xF00F) == 0x400C) {
      s32 sh = (s32)R[mm];
      if (sh >= 0)               R[nn] <<= (sh & 0x1F);
      else if ((sh & 0x1F) == 0) R[nn] = (u32)((s32)R[nn] >> 31);
      else                       R[nn] = (u32)((s32)R[nn] >> (32 - (sh & 0x1F)));
    }
    else if ((op & 0xF00F) == 0x400D) {
      s32 sh = (s32)R[mm];
      if (sh >= 0)               R[nn] <<= (sh & 0x1F);
      else if ((sh & 0x1F) == 0) R[nn] = 0;
      else                       R[nn] >>= (32 - (sh & 0x1F));
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
    else if ((op & 0xF0FF) == 0x4025) { int ot = T; T = (int)(R[nn] & 1);
                                        R[nn] = (R[nn] >> 1) | ((u32)ot << 31); }  /* ROTCR */
    else if ((op & 0xF0FF) == 0x4011) T = ((s32)R[nn] >= 0);
    else if ((op & 0xF0FF) == 0x4015) T = ((s32)R[nn] > 0);
    else if ((op & 0xF000) == 0xD000) {                             /* MOV.L @(d,PC) */
      u32 a = ((pc & ~3u) + 4) + (u32)(op & 0xFF) * 4;
      R[nn] = orc_read(a, 2, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "lit @%08X", a); return 0; }
    }
    else if ((op & 0xF000) == 0x5000) {                             /* MOV.L @(d,Rm) */
      if (mm == SH4_REG_BASE) R[nn] = g_reg[op & 0xF];
      else {
        R[nn] = orc_read(R[mm] + (u32)(op & 0xF) * 4, 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "5xxx @%08X", R[mm]); return 0; }
      }
    }
    else if ((op & 0xF000) == 0x1000) {                             /* MOV.L Rm,@(d,Rn) */
      if (nn == SH4_REG_BASE) g_reg[op & 0xF] = R[mm];
      else {
        orc_write(R[nn] + (u32)(op & 0xF) * 4, R[mm], 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "1xxx @%08X", R[nn]); return 0; }
      }
    }
    else if ((op & 0xF00F) == 0x000E) {                             /* MOV.L @(R0,Rm) */
      if (mm == SH4_REG_BASE) R[nn] = g_reg[R[0] >> 2];
      else {
        R[nn] = orc_read(R[mm] + R[0], 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "r0-ld @%08X", R[mm] + R[0]); return 0; }
      }
    }
    else if ((op & 0xF00F) == 0x0006) {                             /* MOV.L Rm,@(R0,Rn) */
      if (nn == SH4_REG_BASE) g_reg[R[0] >> 2] = R[mm];
      else {
        orc_write(R[nn] + R[0], R[mm], 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "r0-st @%08X", R[nn] + R[0]); return 0; }
      }
    }
    else if ((op & 0xF00F) == 0x000D) {                             /* MOV.W @(R0,Rm) */
      R[nn] = (u32)(s32)(int16_t)(u16)orc_read(R[mm] + R[0], 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "r0-ldw @%08X", R[mm] + R[0]); return 0; }
    }
    else if ((op & 0xF00F) == 0x000C) {                             /* MOV.B @(R0,Rm) */
      R[nn] = (u32)(s32)(s8)orc_read(R[mm] + R[0], 0, &okf);
      if (!okf) {
        if (getenv("ORC_DEBUG"))
          fprintf(stderr, "r0-ldb state: R0=%08X R1=%08X R2=%08X R3=%08X R5=%08X mm=%u\n",
                  R[0], R[1], R[2], R[3], R[5], mm);
        snprintf(unmodeled, sizeof unmodeled, "r0-ldb @%08X", R[mm] + R[0]); return 0; }
    }
    else if ((op & 0xF00F) == 0x0005) {                             /* MOV.W Rm,@(R0,Rn) */
      orc_write(R[nn] + R[0], R[mm], 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "r0-stw"); return 0; }
    }
    else if ((op & 0xF00F) == 0x0004) {                             /* MOV.B Rm,@(R0,Rn) */
      orc_write(R[nn] + R[0], R[mm], 0, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "r0-stb"); return 0; }
    }
    else if ((op & 0xF00F) == 0x6001) {                             /* MOV.W @Rm,Rn */
      R[nn] = (u32)(s32)(int16_t)(u16)orc_read(R[mm], 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.w @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xF00F) == 0x6000) {                             /* MOV.B @Rm,Rn */
      R[nn] = (u32)(s32)(s8)orc_read(R[mm], 0, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.b @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xF00F) == 0x6002) {                             /* MOV.L @Rm,Rn */
      R[nn] = orc_read(R[mm], 2, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.l @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xF00F) == 0x2001) {                             /* MOV.W Rm,@Rn */
      orc_write(R[nn], R[mm], 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.w st @%08X", R[nn]); return 0; }
    }
    else if ((op & 0xF00F) == 0x2000) {                             /* MOV.B Rm,@Rn */
      orc_write(R[nn], R[mm], 0, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.b st @%08X", R[nn]); return 0; }
    }
    else if ((op & 0xF00F) == 0x2002) {                             /* MOV.L Rm,@Rn */
      orc_write(R[nn], R[mm], 2, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.l st @%08X", R[nn]); return 0; }
    }
    else if ((op & 0xFF00) == 0x8900) {
      if (T) next = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
    }
    else if ((op & 0xFF00) == 0x8B00) {
      if (!T) next = pc + 4 + (u32)((s32)(s8)(op & 0xFF) * 2);
    }
    else if ((op & 0xF000) == 0xA000) {                             /* BRA */
      s32 d = (s32)(op & 0x0FFF); if (d & 0x800) d -= 0x1000;
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf || (u16)slot != 0x0009) { snprintf(unmodeled, sizeof unmodeled, "bra slot"); return 0; }
      next = pc + 4 + (u32)(d * 2);
    }
    else {
      snprintf(unmodeled, sizeof unmodeled, "op %04x @%08X", op, pc);
      return 0;
    }
    pc = next;
  }
  g_reg[SH4_GREG_CPSR] = R[SH4_REG_CPSR];
  return 1;
}

/* Back-compat wrapper for the ALU/PSR sections: single code window. */
static int run_sh4x(const u8 *code, size_t n)
{
  orc_reset_windows();
  orc_add_window(code, (u32)n + 64, 0);          /* +64: literal pool slack */
  orc_add_window(io_registers, sizeof(io_registers), 0);
  orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
  orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
  orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
  orc_add_window(reg_mode, sizeof reg_mode, 0);
  orc_add_window(spsr, sizeof spsr, 0);
  orc_add_window(cpu_modes, sizeof cpu_modes, 0);
  orc_add_window(psr_rebank_buf, sizeof psr_rebank_buf, 0);
  orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
  orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
  return run_at((u32)(uintptr_t)code, (u32)(uintptr_t)code + (u32)n);
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
  orc_vec_init();
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

  /* ---- thumb immediate shifts (sh4g_shift_imm): exact N/Z/C ---- */
  {
    static const struct { int kind; unsigned imm5; } sf[] = {
      {0,19},{0,1},{0,31},{0,8},{1,1},{1,19},{1,31},{2,1},{2,19},{2,31},
      {1,0},{2,0},   /* LSR/ASR #0 = #32 specials */
    };
    static const u32 svals[] = {
      0xE0u, 0x00000000u, 0xFFFFFFFFu, 0x80000000u, 0x00002000u, 0x12345678u
    };
    static const u32 smasks[] = { 0xF, 0xE, 0xC, 0x2, 0x6, 0x8 };
    for (unsigned fi = 0; fi < sizeof sf / sizeof *sf; fi++)
      for (unsigned vi = 0; vi < sizeof svals / sizeof *svals; vi++)
        for (int cin = 0; cin <= 1; cin++)
          for (unsigned mi = 0; mi < sizeof smasks / sizeof *smasks; mi++) {
            static u8 pbuf[1024]; u8 *pp = pbuf;
            u32 v = svals[vi], want, wc = (u32)cin, wrote_c = 0;
            u32 mask = smasks[mi];
            u32 cpsr_in = ((u32)cin << 29) | 0x1000001Fu;   /* V set: must stay */

            sh4g_shift_imm(&pp, sf[fi].kind, 0, 1, sf[fi].imm5, mask);
            cases++;
            memset(g_reg, 0, sizeof g_reg);
            for (int i = 0; i < 16; i++) g_reg[i] = 0x77000000u + (u32)i;
            g_reg[1] = v;
            g_reg[SH4_GREG_CPSR] = cpsr_in;
            if (!run_sh4x(pbuf, (size_t)(pp - pbuf))) {
              printf("FAIL shimm k%d i%u: interpreter: %s\n",
                     sf[fi].kind, sf[fi].imm5, unmodeled);
              fails++; continue;
            }
            /* ARM reference */
            {
              unsigned n = sf[fi].imm5;
              switch (sf[fi].kind) {
              case 0:  /* LSL */
                if (n == 0) { want = v; }
                else { want = v << n; wc = (v >> (32 - n)) & 1; wrote_c = 1; }
                break;
              case 1:  /* LSR (0 -> 32) */
                if (n == 0) { want = 0; wc = v >> 31; }
                else { want = v >> n; wc = (v >> (n - 1)) & 1; }
                wrote_c = 1;
                break;
              default: /* ASR (0 -> 32) */
                if (n == 0) { want = (u32)((s32)v >> 31); wc = v >> 31; }
                else { want = (u32)((s32)v >> n); wc = (v >> (n - 1)) & 1; }
                wrote_c = 1;
                break;
              }
            }
            if (g_reg[0] != want) {
              printf("FAIL shimm k%d i%u v=%08X m=%X: rd=%08X want %08X\n",
                     sf[fi].kind, sf[fi].imm5, v, mask, g_reg[0], want);
              fails++; continue;
            }
            {
              u32 got = g_reg[SH4_GREG_CPSR];
              u32 vbit = got & 0x10000000u;
              u32 low  = got & 0x0FFFFFFFu & ~0x10000000u;
              u32 gotc = (got >> 29) & 1;
              u32 gotn = got >> 31, gotz = (got >> 30) & 1;
              u32 wn = want >> 31, wz = (want == 0);
              int bad = 0;
              if (vbit != 0x10000000u) bad = 1;              /* V clobbered */
              if ((low & 0xFFFFFFF) != 0x1F && (got & 0xFFFFFFF) != 0x1F) bad = 1;
              if (mask & 0x2) {                              /* C live */
                u32 expc = wrote_c ? wc : (u32)cin;
                if (gotc != expc) bad = 1;
              }
              if ((mask & 0x8) && gotn != wn) bad = 1;
              if ((mask & 0x4) && gotz != wz) bad = 1;
              if (bad) {
                printf("FAIL shimm k%d i%u v=%08X cin=%d m=%X: CPSR=%08X "
                       "(want n%u z%u c%u v1)\n",
                       sf[fi].kind, sf[fi].imm5, v, cin, mask, got,
                       wn, wz, wrote_c ? wc : (u32)cin);
                fails++;
              }
            }
          }
  }

  /* ---- native MSR/MRS: guard chain + masked merge + IO pending check ---- */
  {
    {   /* resident set_cpu_mode routine, baked into MSR sites as a literal */
      u8 *rtp = psr_rebank_buf;
      cgba_sh4_psr_rebank_routine = sh4g_psr_emit_rebank_routine(&rtp);
      if ((size_t)(rtp - psr_rebank_buf) > sizeof psr_rebank_buf) {
        printf("FAIL psr rebank routine overflows buffer (%zu)\n",
               (size_t)(rtp - psr_rebank_buf));
        fails++;
      }
    }
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
      0x0000001Fu, 0x0000009Fu, 0x60000012u, 0xF000009Fu, 0x2000001Fu,
      0x00000011u                                   /* FIQ: old-mode bail */
    };
    static const u32 rvals[] = {
      0x0000001Fu, 0x0000009Fu, 0x9000001Fu, 0xF0000012u, 0x00000010u,
      0x0000003Fu,                                  /* Thumb bit: bail    */
      0x00000011u                                   /* FIQ: new-mode bail */
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
              g_reg[SH4_GREG_CPU_MODE] =
                user ? 0x00 : cpu_modes_logical[oldc & 0xF];
              for (int r = 0; r < 7; r++)
                for (int c = 0; c < 7; c++)
                  reg_mode[r][c] =
                    ORC_BSWAP32(0xB0000000u + (u32)r * 0x100u + (u32)c);

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
              u32 old_mode = user ? 0x00u : cpu_modes_logical[oldc & 0xF];
              u32 new_mode = cpu_modes_logical[merged & 0xF];
              int want_slow = 0;
              if (pfield != 2) {
                if (user) want_slow = 1;
                else if ((oldc ^ merged) & 0x20) want_slow = 1;
                else if (!(merged & 0x80) &&
                         (ios[si].ie & ios[si].iff) && (ios[si].ime & 1))
                  want_slow = 1;
              }
              if (want_slow != slow) {
                printf("FAIL msr op=%08X old=%08X val=%08X user=%d io=%u: "
                       "slow=%d want %d\n", opcode, oldc, val, user, si, slow, want_slow);
                fails++; continue;
              }
              if (slow)
                continue;
              if (g_reg[SH4_GREG_CPSR] != merged) {
                printf("FAIL msr op=%08X old=%08X val=%08X: CPSR=%08X want %08X\n",
                       opcode, oldc, val, g_reg[SH4_GREG_CPSR], merged);
                fails++; continue;
              }
              if (pfield == 2)
                continue;                       /* flags-only: no banking */
              if (g_reg[SH4_GREG_CPU_MODE] != new_mode) {
                printf("FAIL msr op=%08X: CPU_MODE=%08X want %08X\n",
                       opcode, g_reg[SH4_GREG_CPU_MODE], new_mode);
                fails++; continue;
              }
              if (old_mode != new_mode) {
                /* Model set_cpu_mode over logical values: retire into the
                 * old row (7 registers when ENTERING FIQ, else r13/r14),
                 * then load from the new row (7 when LEAVING FIQ). Shared
                 * rows (USER<->SYSTEM, INVALID<->INVALID) retire-then-load
                 * the just-written slots, exactly like the C code. */
                u32 or_ = old_mode & 0xF, nr = new_mode & 0xF;
                u32 rm_m[7][7], regs_m[16];
                int bad = 0;
                for (int r = 0; r < 7; r++)
                  for (int c = 0; c < 7; c++)
                    rm_m[r][c] = 0xB0000000u + (u32)r * 0x100u + (u32)c;
                for (int r = 0; r < 16; r++) regs_m[r] = 0xFACE0000u + (u32)r;
                regs_m[2] = rvals[ri];
                if (new_mode == 0x12)
                  for (int r = 0; r < 7; r++) rm_m[or_][r] = regs_m[8 + r];
                else {
                  rm_m[or_][5] = regs_m[13];
                  rm_m[or_][6] = regs_m[14];
                }
                if (old_mode == 0x12)
                  for (int r = 0; r < 7; r++) regs_m[8 + r] = rm_m[nr][r];
                else {
                  regs_m[13] = rm_m[nr][5];
                  regs_m[14] = rm_m[nr][6];
                }
                for (int r = 8; r < 15 && !bad; r++)
                  if (g_reg[r] != regs_m[r]) bad = 1;
                for (int c = 0; c < 7 && !bad; c++)
                  if (ORC_BSWAP32(reg_mode[or_][c]) != rm_m[or_][c]) bad = 1;
                if (bad) {
                  printf("FAIL msr rebank op=%08X old=%08X val=%08X "
                         "modes %02X->%02X: r13=%08X r14=%08X r8=%08X\n",
                         opcode, oldc, val, old_mode, new_mode,
                         g_reg[13], g_reg[14], g_reg[8]);
                  fails++;
                }
              } else if (g_reg[13] != 0xFACE000Du || g_reg[14] != 0xFACE000Eu) {
                printf("FAIL msr op=%08X: r13/r14 moved without mode change\n",
                       opcode);
                fails++;
              }
            }
  }

  /* ---- native SPSR forms: spsr[mode & 0xF] array read / masked merge ---- */
  {
    static const u32 spsr_ops[] = {
      0xE14F3000u,            /* MRS r3, spsr */
      0xE169F002u,            /* MSR spsr_fc, r2 */
      0xE168F002u,            /* MSR spsr_f, r2 */
      0xE161F002u,            /* MSR spsr_c, r2 */
      0xE369F0A5u,            /* MSR spsr_fc, #0xA5 */
    };
    static const u32 smodes[] = { 0x00, 0x10, 0x11, 0x13, 0x15 };
    static const u32 srvals[] = {
      0x00000000u, 0xFFFFFFFFu, 0xF00000D3u, 0x0000001Fu, 0x600000B2u
    };
    for (unsigned oi = 0; oi < sizeof spsr_ops / sizeof *spsr_ops; oi++)
      for (unsigned mi = 0; mi < sizeof smodes / sizeof *smodes; mi++)
        for (unsigned ri = 0; ri < sizeof srvals / sizeof *srvals; ri++) {
          u32 opcode = spsr_ops[oi];
          u32 idx = smodes[mi] & 0xF;
          u32 sinit = 0xA5000000u + idx * 0x111u;
          static u8 pbuf[4096]; u8 *pp = pbuf;

          if (!sh4g_arm_psr_native(&pp, opcode, pc, 0)) {
            printf("FAIL spsr op=%08X: no native emission\n", opcode);
            fails++; continue;
          }
          cases++;

          memset(g_reg, 0, sizeof g_reg);
          for (int i = 0; i < 16; i++) g_reg[i] = 0xFACE0000u + (u32)i;
          g_reg[2] = srvals[ri];
          g_reg[SH4_GREG_CPSR] = 0x6000001Fu;
          g_reg[SH4_GREG_CPU_MODE] = smodes[mi];
          for (int i = 0; i < 6; i++)
            spsr[i] = ORC_BSWAP32(0xA5000000u + (u32)i * 0x111u);

          if (!run_sh4x(pbuf, (size_t)(pp - pbuf))) {
            printf("FAIL spsr op=%08X: interpreter: %s\n", opcode, unmodeled);
            fails++; continue;
          }

          if (!((opcode >> 21) & 1)) {          /* MRS r3, spsr */
            if (g_reg[3] != sinit) {
              printf("FAIL mrs-spsr mode=%02X: r3=%08X want %08X\n",
                     smodes[mi], g_reg[3], sinit);
              fails++;
            }
            continue;
          }
          u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
          u32 mask = (pfield == 3) ? 0xF00000EFu
                   : (pfield == 2) ? 0xF0000000u : 0x000000EFu;
          u32 val = (opcode & 0x02000000u) ? 0xA5u : srvals[ri];
          u32 want = (val & mask) | (sinit & ~mask);
          int bad = (ORC_BSWAP32(spsr[idx]) != want) ||
                    (g_reg[SH4_GREG_CPSR] != 0x6000001Fu) ||
                    (g_reg[SH4_GREG_CPU_MODE] != smodes[mi]);
          for (u32 i = 0; i < 6 && !bad; i++)
            if (i != idx &&
                ORC_BSWAP32(spsr[i]) != 0xA5000000u + i * 0x111u) bad = 1;
          if (bad) {
            printf("FAIL msr-spsr op=%08X mode=%02X val=%08X: "
                   "spsr[%u]=%08X want %08X\n",
                   opcode, smodes[mi], val, idx, ORC_BSWAP32(spsr[idx]), want);
            fails++;
          }
        }
  }

  /* ---- ARM data-processing native: the interrupt-dispatcher workload ---- */
  {
    /* op2 forms: imm rot0, imm rotated, plain reg, reg LSL#4, reg LSR#4,
     * reg ASR#4, reg LSR#32(field 0), reg ASR#32(field 0) */
    static const u32 op2s[] = {
      0x020000A5u,          /* imm 0xA5, rot 0 */
      0x02000FA5u,          /* imm 0xA5 ror 30 -> shifter C = bit31 */
      0x00000004u,          /* r4 */
      0x00000204u,          /* r4, lsl #4 */
      0x00000224u,          /* r4, lsr #4 */
      0x00000244u,          /* r4, asr #4 */
      0x00000024u,          /* r4, lsr #32 */
      0x00000044u,          /* r4, asr #32 */
      0x00000264u,          /* r4, ror #4 */
      0x00000E64u,          /* r4, ror #28 */
    };
    static const u32 vals2[] = {
      0, 1, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x12345678u, 0xA5A50000u
    };
    for (u32 aop = 0; aop <= 0xF; aop++)
      for (unsigned oi = 0; oi < sizeof op2s / sizeof *op2s; oi++)
        for (unsigned vi = 0; vi < sizeof vals2 / sizeof *vals2; vi++)
          for (unsigned wi = 0; wi < sizeof vals2 / sizeof *vals2; wi += 2)
            for (int S = 0; S <= 1; S++)
              for (int cin = 0; cin <= 1; cin++)
                for (unsigned mi = 0; mi < sizeof masks / sizeof *masks; mi++) {
                  int is_test = (aop >= 8 && aop <= 0xB);
                  if (is_test && !S) continue;              /* not encodable */
                  /* pass 2 of the vi loop doubles as a PC-operand pass:
                     rn = PC on odd vi (the emitter folds pc+8 to a const) */
                  u32 rn_field = (vi & 1) ? 15u : 2u;
                  u32 opcode = 0xE0000000u | (aop << 21) | ((u32)S << 20)
                             | (rn_field << 16) | (3u << 12) | op2s[oi];
                  static u8 abuf[1024]; u8 *ap = abuf;
                  u32 fm2 = S ? masks[mi] : 0;

                  memset(g_reg, 0, sizeof g_reg);
                  for (int i = 0; i < 16; i++) g_reg[i] = 0xAB000000u + (u32)i;
                  g_reg[2] = vals2[vi];                     /* rn */
                  g_reg[4] = vals2[wi];                     /* rm */
                  g_reg[3] = 0x51515151u;                   /* rd before */
                  g_reg[SH4_GREG_CPSR] = ((u32)cin << 29) | 0x1000001Fu;

                  if (!sh4g_arm_dp_native(&ap, opcode, pc, fm2))
                    continue;
                  cases++;

                  if (!run_sh4x(abuf, (size_t)(ap - abuf))) {
                    printf("FAIL adp op=%08X: interpreter: %s\n", opcode, unmodeled);
                    fails++; continue;
                  }

                  /* reference: mirror cgba_sh4_arm_dp + arm_shifter_operand */
                  u32 a = (rn_field == 15) ? pc + 8 : vals2[vi];
                  u32 carry = (u32)cin, oldc = (u32)cin;
                  u32 b;
                  if (opcode & 0x02000000u) {
                    u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
                    b = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
                    if (rot) carry = (b >> 31) & 1;
                  } else {
                    u32 val = vals2[wi];
                    u32 type = (opcode >> 5) & 3;
                    u32 amount = (opcode >> 7) & 0x1F;
                    switch (type) {
                    case 0:
                      if (amount == 0) b = val;
                      else { carry = (val >> (32 - amount)) & 1; b = val << amount; }
                      break;
                    case 1:
                      if (amount == 0) { carry = (val >> 31) & 1; b = 0; }
                      else { carry = (val >> (amount - 1)) & 1; b = val >> amount; }
                      break;
                    case 2:
                      if (amount == 0) { carry = (val >> 31) & 1; b = (u32)((s32)val >> 31); }
                      else { carry = (val >> (amount - 1)) & 1; b = (u32)((s32)val >> amount); }
                      break;
                    default:            /* ROR #n (RRX never passes the emitter) */
                      carry = (val >> (amount - 1)) & 1;
                      b = (val >> amount) | (val << (32 - amount));
                      break;
                    }
                  }
                  u32 cf = carry, vf = ((u32)0x1000001Fu >> 28) & 1, res = 0;
                  int writes = 1;
                  u64 tmp;
                  switch (aop) {
                  case 0x0: res = a & b; break;
                  case 0x1: res = a ^ b; break;
                  case 0x2: tmp = (u64)a - b; res = (u32)tmp; cf = a >= b;
                            vf = ((a ^ b) & (a ^ res)) >> 31; break;
                  case 0x3: tmp = (u64)b - a; res = (u32)tmp; cf = b >= a;
                            vf = ((b ^ a) & (b ^ res)) >> 31; break;
                  case 0x4: tmp = (u64)a + b; res = (u32)tmp; cf = (u32)(tmp >> 32);
                            vf = (~(a ^ b) & (a ^ res)) >> 31; break;
                  case 0x5: tmp = (u64)a + b + oldc; res = (u32)tmp; cf = (u32)(tmp >> 32);
                            vf = (~(a ^ b) & (a ^ res)) >> 31; break;
                  case 0x6: tmp = (u64)a - b - (1 - oldc); res = (u32)tmp;
                            cf = a >= ((u64)b + (1 - oldc));
                            vf = ((a ^ b) & (a ^ res)) >> 31; break;
                  case 0x7: tmp = (u64)b - a - (1 - oldc); res = (u32)tmp;
                            cf = b >= ((u64)a + (1 - oldc));
                            vf = ((b ^ a) & (b ^ res)) >> 31; break;
                  case 0x8: res = a & b; writes = 0; break;
                  case 0x9: res = a ^ b; writes = 0; break;
                  case 0xA: tmp = (u64)a - b; res = (u32)tmp; cf = a >= b;
                            vf = ((a ^ b) & (a ^ res)) >> 31; writes = 0; break;
                  case 0xB: tmp = (u64)a + b; res = (u32)tmp; cf = (u32)(tmp >> 32);
                            vf = (~(a ^ b) & (a ^ res)) >> 31; writes = 0; break;
                  case 0xC: res = a | b; break;
                  case 0xD: res = b; break;
                  case 0xE: res = a & ~b; break;
                  default:  res = ~b; break;
                  }

                  u32 want_rd = writes ? res : 0x51515151u;
                  if (g_reg[3] != want_rd) {
                    printf("FAIL adp op=%08X a=%08X b(rm)=%08X cin=%d: rd=%08X want %08X\n",
                           opcode, a, vals2[wi], cin, g_reg[3], want_rd);
                    fails++; continue;
                  }
                  if (g_reg[2] != vals2[vi] || g_reg[4] != vals2[wi]) {
                    printf("FAIL adp op=%08X: operand regs clobbered\n", opcode);
                    fails++; continue;
                  }
                  u32 got = g_reg[SH4_GREG_CPSR];
                  u32 old = ((u32)cin << 29) | 0x1000001Fu;
                  if (!S) {
                    if (got != old) {
                      printf("FAIL adp op=%08X: CPSR changed w/o S %08X->%08X\n",
                             opcode, old, got);
                      fails++;
                    }
                    continue;
                  }
                  /* Masked contract: flags in fm2 must equal the ARM
                   * architectural result (logical: NZ + shifter C, arith:
                   * NZCV; preserved flags equal old); flags outside fm2 may
                   * be old or the correctly-computed value (widening). */
                  int arith = (aop >= 2 && aop <= 7) || aop == 0xA || aop == 0xB;
                  u32 wN = (res >> 31) & 1, wZ = (res == 0);
                  u32 wC = arith ? (cf & 1) : (carry & 1);
                  u32 wV = arith ? (vf & 1) : ((old >> 28) & 1);
                  u32 arch = (old & 0x0FFFFFFFu) |
                             (wN << 31) | (wZ << 30) | (wC << 29) | (wV << 28);
                  int bad = 0;
                  for (int fb = 0; fb < 4; fb++) {
                    u32 bit = 1u << (28 + fb);           /* V,C,Z,N */
                    u32 m = (fm2 >> fb) & 1;             /* mask V=1,C=2,Z=4,N=8 */
                    u32 gf = got & bit, af = arch & bit, of = old & bit;
                    if (m) { if (gf != af) bad = 1; }
                    else if (gf != of && gf != af) bad = 1;
                  }
                  if ((got & 0x0FFFFFFFu) != (old & 0x0FFFFFFFu)) bad = 1;
                  if (bad) {
                    printf("FAIL adp op=%08X a=%08X rm=%08X cin=%d fm=%X: CPSR=%08X arch %08X old %08X\n",
                           opcode, a, vals2[wi], cin, fm2, got, arch, old);
                    fails++;
                  }
                }
  }

  /* ---- fastmem: real sites calling real routines over a modeled map ---- */
  {
    static u8 fm_buf[8192];
    static u8 giwram[0x10000];   /* tags 0..0x7FFF, data 0x8000.. */
    static u8 ewram[0x48000];    /* data page 0, tag mirror at +0x40000 */
    static u8 vram[0x8000];
    static u8 rom[0x8000];
    u8 *tp = fm_buf;
    const u32 pc = 0x08000100;

    for (int fm = 0; fm < CGBA_FM_COUNT; fm++)
      cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_routine(&tp, fm);
    for (int fm = CGBA_FMB_LDM; fm < CGBA_FM_TOTAL; fm++)
      cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_block_routine(&tp, fm);
    if (getenv("ORC_PRINT_FM"))
      for (int fm = 0; fm < CGBA_FM_TOTAL; fm++)
        fprintf(stderr, "fm[%d] off=0x%zx\n", fm,
                (size_t)(cgba_sh4_fastmem_routine[fm] - fm_buf));

    memset(memory_map_read, 0, sizeof memory_map_read);
    memory_map_read[0x02000000u >> 15] = ewram;
    memory_map_read[0x03000000u >> 15] = giwram + 0x8000;
    memory_map_read[0x04000000u >> 15] = (u8 *)io_registers;
    memory_map_read[0x06000000u >> 15] = vram;
    memory_map_read[0x08000000u >> 15] = rom;

    for (unsigned i = 0; i < sizeof rom; i++)   rom[i]   = (u8)(0x11 + i * 7);
    for (unsigned i = 0; i < 0x8000; i++) {
      giwram[0x8000 + i] = (u8)(0x23 + i * 3);
      ewram[i]          = (u8)(0x35 + i * 5);
      vram[i]           = (u8)(0x47 + i * 11);
    }
    for (unsigned i = 0x1230; i < 0x1260; i++)  /* SMC tag range: covers the
        pre/post/reg-offset effective addresses of the smc targets */
      giwram[i] = 1;
    for (unsigned i = 0x2330; i < 0x2360; i++)
      ewram[0x40000 + i] = 1;

    struct tgt { u32 base; int slow_ld; int slow_st; const char *nm; } tgts[] = {
      { 0x03000100u, 0, 0, "iwram" },
      { 0x02000200u, 0, 0, "ewram" },
      { 0x06000300u, 0, -1, "vram" },           /* -1: word/half ok, byte slow */
      { 0x04000000u, 0, 1, "io" },
      { 0x08000400u, 0, 1, "rom" },
      { 0x00000100u, 1, 1, "bios" },
      { 0x0C000000u, 1, 1, "unmapped" },
      { 0x03001234u, 0, 2, "iwram-smc" },       /* 2: store hits an SMC tag */
      { 0x02002340u, 0, 2, "ewram-smc" },
    };

    /* ARM single transfers: every kind x addressing form x target */
    struct form { u32 op_base; int kind; int is_load; int wb; const char *nm; } forms[] = {
      /* word/byte form: cond=E, bits fixed; imm offset 0x10, rn=2, rd=3 */
      { 0xE5923010u, LDK_W, 1, 0, "ldr  [r2,#imm]" },
      { 0xE5D23010u, LDK_B, 1, 0, "ldrb [r2,#imm]" },
      { 0xE5B23010u, LDK_W, 1, 1, "ldr  [r2,#imm]!" },
      { 0xE4923010u, LDK_W, 1, 1, "ldr  [r2],#imm" },
      { 0xE5823010u, LDK_W, 0, 0, "str  [r2,#imm]" },
      { 0xE5C23010u, LDK_B, 0, 0, "strb [r2,#imm]" },
      { 0xE4823010u, LDK_W, 0, 1, "str  [r2],#imm" },
      { 0xE7923004u, LDK_W, 1, 0, "ldr  [r2,r4]" },
      { 0xE7923104u, LDK_W, 1, 0, "ldr  [r2,r4,lsl#2]" },
      /* halfword/signed form: imm offset 0x10 (hi nibble 1, lo 0) */
      { 0xE1D231B0u, LDK_UH, 1, 0, "ldrh [r2,#imm]" },
      { 0xE1D231D0u, LDK_SB, 1, 0, "ldrsb[r2,#imm]" },
      { 0xE1D231F0u, LDK_SH, 1, 0, "ldrsh[r2,#imm]" },
      { 0xE1C231B0u, LDK_UH, 0, 0, "strh [r2,#imm]" },
      { 0xE1F231B0u, LDK_UH, 1, 1, "ldrh [r2,#imm]!" },
    };

    for (unsigned fi = 0; fi < sizeof forms / sizeof *forms; fi++)
      for (unsigned ti = 0; ti < sizeof tgts / sizeof *tgts; ti++)
        for (int mis = 0; mis <= 1; mis++) {
          static u8 sbuf[512];
          u8 *sp = sbuf;
          u32 op = forms[fi].op_base;
          int kind = forms[fi].kind, is_load = forms[fi].is_load;
          int align = (kind == LDK_W) ? 3 :
                      (kind == LDK_UH || kind == LDK_SH) ? 1 : 0;
          int pre = (op >> 24) & 1, up = (op >> 23) & 1;
          u32 imm_form = ((op & 0x0E000090u) == 0x90u)
            ? ((op >> 22) & 1) : !((op >> 25) & 1);
          u32 off = imm_form ? 0x10 : (g_reg[4] = 0x10, 0x10);
          u32 shifted = (!imm_form && (op & 0x0FF0u)) ? 1 : 0;
          u32 base = tgts[ti].base + (mis ? 1 : 0);
          u32 eff, wbv;

          if (mis && align == 0) continue;      /* byte: no misalign case */
          memset(g_reg, 0, sizeof g_reg);
          for (int i = 0; i < 16; i++) g_reg[i] = 0x51AB0000u + (u32)i;
          g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
          g_reg[4] = shifted ? 0x04 : 0x10;     /* reg offset (lsl#2 -> 0x10) */
          g_reg[2] = base;
          g_reg[3] = is_load ? 0xDEAD0001u : 0xCAFE1234u;
          eff = pre ? (up ? base + off : base - off) : base;
          wbv = up ? base + off : base - off;

          if (!sh4g_arm_ldst_native(&sp, op, pc, 0))
            { if (ti == 0 && !mis) { printf("FAIL emit reject %s\n", forms[fi].nm); fails++; } continue; }
          cases++;

          /* fresh copies of mutable memory for the reference diff */
          u8 mem_before[8]; const u8 *page = NULL; u32 poff = 0;
          u32 reg = eff >> 24;
          if (reg == 2) { page = ewram; poff = eff & 0x7FFF; }
          else if (reg == 3) { page = giwram + 0x8000; poff = eff & 0x7FFF; }
          else if (reg == 6) { page = vram; poff = eff & 0x7FFF; }
          if (page) memcpy(mem_before, page + poff, 4);

          orc_reset_windows();
          orc_add_window(sbuf, sizeof sbuf, 0);
          orc_add_window(fm_buf, sizeof fm_buf, 0);
          orc_add_window(io_registers, sizeof io_registers, 0);
          orc_add_window(memory_map_read, 8192 * 4, 1);
          orc_add_window(giwram, sizeof giwram, 0);
          orc_add_window(ewram, sizeof ewram, 0);
          orc_add_window(vram, sizeof vram, 0);
          orc_add_window(rom, sizeof rom, 0);
          orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
          orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
          orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
          orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
          orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

          int ran = run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sbuf + (u32)(sp - sbuf));
          int slow = (!ran && orc_took_slow);
          if (!ran && !slow) {
            printf("FAIL fm %s @%s%s: interpreter: %s\n",
                   forms[fi].nm, tgts[ti].nm, mis ? "+1" : "", unmodeled);
            fails++;
            if (page) memcpy((void *)(page + poff), mem_before, 4);
            continue;
          }

          /* expected route */
          int want_slow;
          if (mis) want_slow = 1;
          else if (is_load) want_slow = tgts[ti].slow_ld;
          else {
            want_slow = tgts[ti].slow_st;
            if (want_slow == -1) want_slow = (kind == LDK_B);   /* vram byte */
            else if (want_slow == 2) want_slow = 1;             /* smc tag */
          }
          if (want_slow != slow) {
            printf("FAIL fm %s @%s%s: slow=%d want %d\n",
                   forms[fi].nm, tgts[ti].nm, mis ? "+1" : "", slow, want_slow);
            fails++;
            if (page) memcpy((void *)(page + poff), mem_before, 4);
            continue;
          }
          if (slow) { if (page) memcpy((void *)(page + poff), mem_before, 4); continue; }

          /* fast path: check rd / writeback / memory / other regs */
          u32 want_rd = g_reg[3];
          if (is_load && page) {
            u32 lo = page[poff], b1 = page[poff+1], b2 = page[poff+2], b3 = page[poff+3];
            u32 lev = lo | (b1 << 8) | (b2 << 16) | (b3 << 24);
            switch (kind) {
            case LDK_W:  want_rd = lev; break;
            case LDK_B:  want_rd = lev & 0xFF; break;
            case LDK_UH: want_rd = lev & 0xFFFF; break;
            case LDK_SH: want_rd = (u32)(s32)(int16_t)(lev & 0xFFFF); break;
            default:     want_rd = (u32)(s32)(s8)(lev & 0xFF); break;
            }
          } else if (is_load && (eff >> 24) == 4) {
            const u8 *iop = (const u8 *)io_registers + (eff & 0x3FF);
            u32 lev = iop[0] | (iop[1] << 8) | ((kind == LDK_W) ?
                      ((u32)iop[2] << 16) | ((u32)iop[3] << 24) : 0);
            switch (kind) {
            case LDK_W:  want_rd = lev; break;
            case LDK_B:  want_rd = lev & 0xFF; break;
            case LDK_UH: want_rd = lev & 0xFFFF; break;
            case LDK_SH: want_rd = (u32)(s32)(int16_t)(lev & 0xFFFF); break;
            default:     want_rd = (u32)(s32)(s8)(lev & 0xFF); break;
            }
          } else if (is_load && (eff >> 24) == 8) {
            const u8 *rp = rom + (eff & 0x7FFF);
            u32 lev = rp[0] | (rp[1] << 8) | ((u32)rp[2] << 16) | ((u32)rp[3] << 24);
            switch (kind) {
            case LDK_W:  want_rd = lev; break;
            case LDK_B:  want_rd = lev & 0xFF; break;
            case LDK_UH: want_rd = lev & 0xFFFF; break;
            case LDK_SH: want_rd = (u32)(s32)(int16_t)(lev & 0xFFFF); break;
            default:     want_rd = (u32)(s32)(s8)(lev & 0xFF); break;
            }
          }
          if (g_reg[3] != want_rd) {
            printf("FAIL fm %s @%s: rd=%08X want %08X\n",
                   forms[fi].nm, tgts[ti].nm, g_reg[3], want_rd);
            fails++;
          }
          if (forms[fi].wb && g_reg[2] != wbv) {
            printf("FAIL fm %s @%s: wb rn=%08X want %08X\n",
                   forms[fi].nm, tgts[ti].nm, g_reg[2], wbv);
            fails++;
          }
          if (!forms[fi].wb && g_reg[2] != base) {
            printf("FAIL fm %s @%s: rn clobbered %08X\n",
                   forms[fi].nm, tgts[ti].nm, g_reg[2]);
            fails++;
          }
          if (!is_load && page) {
            u32 v = 0xCAFE1234u;
            u8 want[4] = { mem_before[0], mem_before[1], mem_before[2], mem_before[3] };
            switch (kind) {
            case LDK_W: want[0] = (u8)v; want[1] = (u8)(v >> 8);
                        want[2] = (u8)(v >> 16); want[3] = (u8)(v >> 24); break;
            case LDK_UH: want[0] = (u8)v; want[1] = (u8)(v >> 8); break;
            default:     want[0] = (u8)v; break;
            }
            if (memcmp(page + poff, want, 4) != 0) {
              printf("FAIL fm %s @%s: mem %02X%02X%02X%02X want %02X%02X%02X%02X\n",
                     forms[fi].nm, tgts[ti].nm,
                     page[poff], page[poff+1], page[poff+2], page[poff+3],
                     want[0], want[1], want[2], want[3]);
              fails++;
            }
            memcpy((void *)(page + poff), mem_before, 4);
          }
        }

    /* rn==15 literal-pool loads: address is a compile-time constant
     * (pc+8 +/- imm) synthesized into the site; lands in the rom page. */
    {
      static const struct { u32 op; int kind; const char *nm; } pcf[] = {
        { 0xE59F3010u, LDK_W,  "ldr  r3,[pc,#0x10]"  },
        { 0xE51F3010u, LDK_W,  "ldr  r3,[pc,-#0x10]" },
        { 0xE5DF3010u, LDK_B,  "ldrb r3,[pc,#0x10]"  },
        { 0xE1DF31B0u, LDK_UH, "ldrh r3,[pc,#0x10]"  },
        { 0xE1DF31F0u, LDK_SH, "ldrsh r3,[pc,#0x10]" },
        { 0xE15F31B0u, LDK_UH, "ldrh r3,[pc,-#0x10]" },
      };
      for (unsigned fi = 0; fi < sizeof pcf / sizeof *pcf; fi++) {
        static u8 sbuf[512];
        u8 *sp = sbuf;
        u32 op = pcf[fi].op;
        int up = (op >> 23) & 1;
        u32 eff = (pc + 8) + (up ? 0x10u : (u32)-0x10);
        const u8 *rp = rom + (eff & 0x7FFF);
        u32 lev = rp[0] | ((u32)rp[1] << 8) | ((u32)rp[2] << 16) |
                  ((u32)rp[3] << 24);
        u32 want;

        if (!sh4g_arm_ldst_native(&sp, op, pc, 0)) {
          printf("FAIL pcldr emit reject %s\n", pcf[fi].nm);
          fails++; continue;
        }
        cases++;
        memset(g_reg, 0, sizeof g_reg);
        for (int i = 0; i < 16; i++) g_reg[i] = 0x51AB0000u + (u32)i;
        g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
        g_reg[3] = 0xDEAD0001u;

        orc_reset_windows();
        orc_add_window(sbuf, sizeof sbuf, 0);
        orc_add_window(fm_buf, sizeof fm_buf, 0);
        orc_add_window(io_registers, sizeof io_registers, 0);
        orc_add_window(memory_map_read, 8192 * 4, 1);
        orc_add_window(giwram, sizeof giwram, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

        if (!run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sbuf + (u32)(sp - sbuf))) {
          printf("FAIL pcldr %s: %s%s\n", pcf[fi].nm,
                 orc_took_slow ? "took slow path: " : "interpreter: ", unmodeled);
          fails++; continue;
        }
        switch (pcf[fi].kind) {
        case LDK_W:  want = lev; break;
        case LDK_B:  want = lev & 0xFF; break;
        case LDK_UH: want = lev & 0xFFFF; break;
        case LDK_SH: want = (u32)(s32)(int16_t)(lev & 0xFFFF); break;
        default:     want = (u32)(s32)(s8)(lev & 0xFF); break;
        }
        if (g_reg[3] != want) {
          printf("FAIL pcldr %s: rd=%08X want %08X (eff=%08X)\n",
                 pcf[fi].nm, g_reg[3], want, eff);
          fails++;
        }
      }
      /* STRH to REG_IF / REG_IE: native interrupt-register fast path in
       * the halfword-store routine. IF ack is pure; IE writes natively
       * unless they would unmask a pending IRQ (IME on, CPSR I clear). */
      {
        struct iocase {
          u32 addr; u32 rd_val; u16 ie0, if0, ime0; u32 cpsr;
          int want_slow; u16 want_ie, want_if; const char *nm;
        } iocs[] = {
          { 0x04000202u, 0x0005u, 0x00FFu, 0x0007u, 1, 0x0000001Fu,
            0, 0x00FF, 0x0002, "if-ack" },
          { 0x04000202u, 0xFFFFu, 0x00FFu, 0x0007u, 1, 0x0000001Fu,
            0, 0x00FF, 0x0000, "if-ack-all" },
          { 0x04000200u, 0x0008u, 0x0001u, 0x0002u, 1, 0x0000001Fu,
            0, 0x0008, 0x0002, "ie-no-overlap" },
          { 0x04000200u, 0x0002u, 0x0001u, 0x0002u, 1, 0x0000001Fu,
            1, 0x0002, 0x0002, "ie-pending-raise" },
          { 0x04000200u, 0x0002u, 0x0001u, 0x0002u, 0, 0x0000001Fu,
            0, 0x0002, 0x0002, "ie-pending-ime-off" },
          { 0x04000200u, 0x0002u, 0x0001u, 0x0002u, 1, 0x0000009Fu,
            0, 0x0002, 0x0002, "ie-pending-i-set" },
          { 0x04000204u, 0x1234u, 0x0001u, 0x0002u, 1, 0x0000001Fu,
            1, 0x0001, 0x0002, "other-io-slow" },
        };
        for (unsigned ci = 0; ci < sizeof iocs / sizeof *iocs; ci++) {
          static u8 sbuf[512];
          u8 *sp = sbuf;
          u32 op = 0xE1C230B0u;               /* strh r3, [r2] */
          u8 *bp;

          if (!sh4g_arm_ldst_native(&sp, op, pc, 0)) {
            printf("FAIL ioreg emit reject\n"); fails++; continue;
          }
          cases++;
          memset(g_reg, 0, sizeof g_reg);
          for (int i = 0; i < 16; i++) g_reg[i] = 0x51AB0000u + (u32)i;
          g_reg[SH4_GREG_CPSR] = iocs[ci].cpsr;
          g_reg[2] = iocs[ci].addr;
          g_reg[3] = iocs[ci].rd_val;
          bp = (u8 *)&io_registers[0x200 >> 1];
          bp[0] = (u8)iocs[ci].ie0;  bp[1] = (u8)(iocs[ci].ie0 >> 8);
          bp = (u8 *)&io_registers[0x202 >> 1];
          bp[0] = (u8)iocs[ci].if0;  bp[1] = (u8)(iocs[ci].if0 >> 8);
          bp = (u8 *)&io_registers[0x208 >> 1];
          bp[0] = (u8)iocs[ci].ime0; bp[1] = (u8)(iocs[ci].ime0 >> 8);

          orc_reset_windows();
          orc_add_window(sbuf, sizeof sbuf, 0);
          orc_add_window(fm_buf, sizeof fm_buf, 0);
          orc_add_window(io_registers, sizeof io_registers, 0);
          orc_add_window(memory_map_read, 8192 * 4, 1);
          orc_add_window(giwram, sizeof giwram, 0);
          orc_add_window(ewram, sizeof ewram, 0);
          orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
          orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
          orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
          orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
          orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

          int ran = run_at((u32)(uintptr_t)sbuf,
                           (u32)(uintptr_t)sbuf + (u32)(sp - sbuf));
          int slow = (!ran && orc_took_slow);
          if (!ran && !slow) {
            printf("FAIL ioreg %s: interpreter: %s\n", iocs[ci].nm, unmodeled);
            fails++; continue;
          }
          if (slow != iocs[ci].want_slow) {
            printf("FAIL ioreg %s: slow=%d want %d\n",
                   iocs[ci].nm, slow, iocs[ci].want_slow);
            fails++; continue;
          }
          if (slow)
            continue;                          /* C helper owns the effects */
          {
            const u8 *ie = (const u8 *)&io_registers[0x200 >> 1];
            const u8 *ifp = (const u8 *)&io_registers[0x202 >> 1];
            u16 g_ie = (u16)(ie[0] | (ie[1] << 8));
            u16 g_if = (u16)(ifp[0] | (ifp[1] << 8));
            if (g_ie != iocs[ci].want_ie || g_if != iocs[ci].want_if) {
              printf("FAIL ioreg %s: IE=%04X IF=%04X want %04X/%04X\n",
                     iocs[ci].nm, g_ie, g_if, iocs[ci].want_ie, iocs[ci].want_if);
              fails++;
            }
          }
        }
      }

      /* PC-relative STORES / writeback forms must still reject */
      {
        static const u32 rej[] = { 0xE58F3010u,   /* str  r3,[pc,#0x10]  */
                                   0xE1CF31B0u,   /* strh r3,[pc,#0x10]  */
                                   0xE5BF3010u,   /* ldr  r3,[pc,#0x10]! */
                                   0xE79F3004u }; /* ldr  r3,[pc,r4]     */
        for (unsigned ri = 0; ri < sizeof rej / sizeof *rej; ri++) {
          static u8 sbuf2[256];
          u8 *sp2 = sbuf2;
          if (sh4g_arm_ldst_native(&sp2, rej[ri], pc, 0)) {
            printf("FAIL pcldr: %08X should stay on C\n", rej[ri]);
            fails++;
          } else
            cases++;
        }
      }
    }

  /* ---- fastmem: Thumb sites (same routines; checks the kind mapping) ---- */
  {
    /* rd=0, rb=1, ro=2 forms; imm5 slot 4 (word: <<2, half: <<1, byte: <<0) */
    struct tf { u32 op; int kind; int is_load; const char *nm; } tforms[] = {
      { 0x6908u, SH4_THUMB_LDK_W, 1, "ldr  [r1,#16]" },
      { 0x6108u, SH4_THUMB_LDK_W, 0, "str  [r1,#16]" },
      { 0x7C08u, SH4_THUMB_LDK_B, 1, "ldrb [r1,#16]" },
      { 0x7408u, SH4_THUMB_LDK_B, 0, "strb [r1,#16]" },
      { 0x8A08u, SH4_THUMB_LDK_UH, 1, "ldrh [r1,#16]" },
      { 0x8208u, SH4_THUMB_LDK_UH, 0, "strh [r1,#16]" },
      { 0x5888u, SH4_THUMB_LDK_W, 1, "ldr  [r1,r2]" },
      { 0x5688u, SH4_THUMB_LDK_SB, 1, "ldrsb[r1,r2]" },
      { 0x5E88u, SH4_THUMB_LDK_SH, 1, "ldrsh[r1,r2]" },
    };
    u32 tbases[] = { 0x03000400u, 0x02000500u, 0x06000600u, 0x08000700u };
    for (unsigned fi = 0; fi < sizeof tforms / sizeof *tforms; fi++)
      for (unsigned bi = 0; bi < sizeof tbases / sizeof *tbases; bi++) {
        static u8 sbuf[512];
        u8 *sp = sbuf;
        int kind = tforms[fi].kind, is_load = tforms[fi].is_load;
        u32 base = tbases[bi];
        u32 reg_off = (tforms[fi].op & 0xF000u) == 0x5000u;
        u32 eff = base + 0x10;
        u32 regn = eff >> 24;
        const u8 *page = (regn == 2) ? ewram : (regn == 3) ? giwram + 0x8000
                       : (regn == 6) ? vram : rom;
        u32 poff = eff & 0x7FFF;
        u8 mem_before[4];
        int st_slow = (regn == 6) ? (kind == SH4_THUMB_LDK_B) : (regn >= 8);

        memset(g_reg, 0, sizeof g_reg);
        for (int i = 0; i < 16; i++) g_reg[i] = 0x7EB00000u + (u32)i;
        g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
        g_reg[1] = base;
        g_reg[2] = 0x10;
        g_reg[0] = is_load ? 0xDEAD0002u : 0xBEEF5678u;
        memcpy(mem_before, page + poff, 4);

        if (!sh4g_thumb_ldst_native(&sp, tforms[fi].op, pc, 0)) {
          printf("FAIL temit reject %s\n", tforms[fi].nm);
          fails++;
          continue;
        }
        cases++;

        orc_reset_windows();
        orc_add_window(sbuf, sizeof sbuf, 0);
        orc_add_window(fm_buf, sizeof fm_buf, 0);
        orc_add_window(io_registers, sizeof io_registers, 0);
        orc_add_window(memory_map_read, 8192 * 4, 1);
        orc_add_window(giwram, sizeof giwram, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

        int ran = run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sbuf + (u32)(sp - sbuf));
        int slow = (!ran && orc_took_slow);
        int want_slow = is_load ? 0 : st_slow;
        if (!ran && !slow) {
          printf("FAIL tfm %s @%X: interpreter: %s\n", tforms[fi].nm, base, unmodeled);
          fails++;
          memcpy((void *)(page + poff), mem_before, 4);
          continue;
        }
        if (slow != want_slow) {
          printf("FAIL tfm %s @%X: slow=%d want %d\n", tforms[fi].nm, base, slow, want_slow);
          fails++;
          memcpy((void *)(page + poff), mem_before, 4);
          continue;
        }
        if (!slow) {
          if (is_load) {
            u32 lev = page[poff] | ((u32)page[poff+1] << 8) |
                      ((u32)page[poff+2] << 16) | ((u32)page[poff+3] << 24);
            u32 want;
            switch (kind) {
            case SH4_THUMB_LDK_W:  want = lev; break;
            case SH4_THUMB_LDK_B:  want = lev & 0xFF; break;
            case SH4_THUMB_LDK_UH: want = lev & 0xFFFF; break;
            case SH4_THUMB_LDK_SH: want = (u32)(s32)(int16_t)(lev & 0xFFFF); break;
            default:               want = (u32)(s32)(s8)(lev & 0xFF); break;
            }
            if (g_reg[0] != want) {
              printf("FAIL tfm %s @%X: rd=%08X want %08X\n",
                     tforms[fi].nm, base, g_reg[0], want);
              fails++;
            }
          } else {
            u32 v = 0xBEEF5678u;
            u8 want[4] = { mem_before[0], mem_before[1], mem_before[2], mem_before[3] };
            switch (kind) {
            case SH4_THUMB_LDK_W: want[0]=(u8)v; want[1]=(u8)(v>>8);
                                  want[2]=(u8)(v>>16); want[3]=(u8)(v>>24); break;
            case SH4_THUMB_LDK_UH: want[0]=(u8)v; want[1]=(u8)(v>>8); break;
            default:               want[0]=(u8)v; break;
            }
            if (memcmp(page + poff, want, 4) != 0) {
              printf("FAIL tfm %s @%X: mem mismatch\n", tforms[fi].nm, base);
              fails++;
            }
            memcpy((void *)(page + poff), mem_before, 4);
          }
        }
        (void)reg_off;
      }
  }

  /* ---- fastmem: block transfers through the runtime-rlist routines ---- */
  {
    struct bf { u32 op; int is_arm; int inline_path; const char *nm; } bforms[] = {
      { 0xE8B20039u, 1, 0, "ldmia r2!,{r0,r3-r5}" },
      { 0xE8920039u, 1, 0, "ldmia r2,{r0,r3-r5}"  },
      { 0xE8A200F0u, 1, 0, "stmia r2!,{r4-r7}"    },
      { 0xE92200F0u, 1, 0, "stmdb r2!,{r4-r7}"    },
      { 0xE9320039u, 1, 0, "ldmdb r2!,{r0,r3-r5}" },
      { 0x0000B407u, 0, 0, "push {r0-r2}"         },
      { 0x0000B507u, 0, 0, "push {r0-r2,lr}"      },
      { 0x0000BC07u, 0, 0, "pop  {r0-r2}"         },
      { 0x0000C11Cu, 0, 0, "stmia r1!,{r2-r4}"    },
      { 0x0000C91Cu, 0, 0, "ldmia r1!,{r2-r4}"    },
      { 0x0000C916u, 0, 0, "ldmia r1!,{r1,r2,r4}" },
      { 0x0000C803u, 0, 1, "ldmia r0!,{r0,r1}"    },  /* count<3: inline path,
                                                         base in rlist (wb skip) */
    };
    struct bt2 { u32 base; int slow_ld; int slow_st; const char *nm; } btgts[] = {
      { 0x03000200u, 0, 0, "iwram" },
      { 0x02000300u, 0, 0, "ewram" },
      { 0x06000400u, 0, 0, "vram" },
      { 0x08000500u, 0, 1, "rom" },
      { 0x00000100u, 1, 1, "bios" },
      { 0x03000203u, 1, 1, "unaligned" },
      { 0x03007FF8u, 1, 1, "straddle" },
      { 0x03001240u, 0, 1, "iwram-smc" },
    };
    for (unsigned fi = 0; fi < sizeof bforms / sizeof *bforms; fi++)
      for (unsigned ti = 0; ti < sizeof btgts / sizeof *btgts; ti++) {
        static u8 sbuf[1024];
        u8 *sp = sbuf;
        u32 op = bforms[fi].op;
        int is_arm = bforms[fi].is_arm;
        u32 rlist, base_reg, is_load, wb, pre, up_, count = 0, lrbit = 0;
        u32 base = btgts[ti].base;

        if (is_arm) {
          rlist = op & 0xFFFF; base_reg = (op >> 16) & 0xF;
          is_load = (op >> 20) & 1; wb = (op >> 21) & 1;
          pre = (op >> 24) & 1; up_ = (op >> 23) & 1;
        } else {
          u32 hi2 = (op >> 8) & 0xFF;
          rlist = op & 0xFF;
          if (hi2 == 0xB4 || hi2 == 0xB5) { base_reg = 13; is_load = 0; pre = 0; up_ = 0; wb = 1; lrbit = (hi2 == 0xB5); }
          else if (hi2 == 0xBC) { base_reg = 13; is_load = 1; pre = 0; up_ = 1; wb = 1; }
          else { base_reg = (op >> 8) & 7; is_load = (op >> 11) & 1; pre = 0; up_ = 1; wb = 1; }
        }
        for (int b = 0; b < 16; b++) if (rlist & (1u << b)) count++;
        if (lrbit) { count++; rlist |= 1u << 14; }

        u32 A, wbv;
        if (is_arm) {
          int offa = up_ ? (pre ? 4 : 0) : (pre ? -(int)(count*4) : -(int)(count*4) + 4);
          A = (base & ~3u) + (u32)offa;
          wbv = up_ ? base + count*4 : base - count*4;
        } else if (base_reg == 13 && !is_load) {   /* push */
          A = base - count*4; wbv = A;
        } else {                                    /* pop / (ld/st)mia */
          A = base; wbv = base + count*4;
        }
        int ldm_base_in_list = is_load && !is_arm && base_reg < 8 &&
                               (rlist & (1u << base_reg));
        int do_wb = wb && !(is_arm && wb && (rlist & (1u << base_reg)) && !is_load)
                       && !(is_arm && is_load && (rlist & (1u << base_reg)))
                       && !ldm_base_in_list;

        memset(g_reg, 0, sizeof g_reg);
        for (int i = 0; i < 16; i++) g_reg[i] = 0x33000000u + ((u32)i << 8);
        g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
        g_reg[base_reg] = base;

        int emitted = is_arm
          ? sh4g_arm_block_native(&sp, op, pc, 0)
          : sh4g_thumb_block_native(&sp, op, pc, 0);
        if (!emitted) {
          printf("FAIL bfm emit reject %s\n", bforms[fi].nm);
          fails++;
          continue;
        }
        cases++;

        u8 mem_before[64]; const u8 *page = NULL; u32 poff = 0;
        u32 regn = A >> 24;
        if (regn == 2) { page = ewram; poff = A & 0x7FFF; }
        else if (regn == 3) { page = giwram + 0x8000; poff = A & 0x7FFF; }
        else if (regn == 6) { page = vram; poff = A & 0x7FFF; }
        else if (regn == 8) { page = rom; poff = A & 0x7FFF; }
        if (page && poff + count*4 <= 0x8000) memcpy(mem_before, page + poff, count*4);

        orc_reset_windows();
        orc_add_window(sbuf, sizeof sbuf, 0);
        orc_add_window(fm_buf, sizeof fm_buf, 0);
        orc_add_window(io_registers, sizeof io_registers, 0);
        orc_add_window(memory_map_read, 8192 * 4, 1);
        orc_add_window(giwram, sizeof giwram, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

        int ran = run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sbuf + (u32)(sp - sbuf));
        int slow = (!ran && orc_took_slow);
        int want_slow = is_load ? btgts[ti].slow_ld : btgts[ti].slow_st;
        if (regn == 6 && !is_load) want_slow = 0;    /* vram word stores ok */
        if (bforms[fi].inline_path && regn != 2 && regn != 3)
          want_slow = 1;         /* count<3 inline path is RAM-only */
        if (ti == 5)                                 /* unaligned: ARM masks */
          want_slow = !is_arm;
        if (ti == 6)                                 /* straddle: form-dependent */
          want_slow = ((A >> 15) != ((A + count*4 - 1) >> 15));
        if (!ran && !slow) {
          printf("FAIL bfm %s @%s: interpreter: %s\n",
                 bforms[fi].nm, btgts[ti].nm, unmodeled);
          fails++;
          goto brestore;
        }
        if (slow != want_slow) {
          printf("FAIL bfm %s @%s: slow=%d want %d\n",
                 bforms[fi].nm, btgts[ti].nm, slow, want_slow);
          fails++;
          goto brestore;
        }
        if (!slow) {
          u32 off2 = poff, gi2 = 0;
          for (int b = 0; b < 16; b++) {
            if (!(rlist & (1u << b))) continue;
            if (is_load) {
              u32 lev = page[off2] | ((u32)page[off2+1] << 8) |
                        ((u32)page[off2+2] << 16) | ((u32)page[off2+3] << 24);
              if (g_reg[b] != lev) {
                printf("FAIL bfm %s @%s: r%d=%08X want %08X\n",
                       bforms[fi].nm, btgts[ti].nm, b, g_reg[b], lev);
                fails++; goto brestore;
              }
            } else {
              u32 v = (b == (int)base_reg) ? base : 0x33000000u | ((u32)b << 8);
              u8 want[4] = { (u8)v, (u8)(v>>8), (u8)(v>>16), (u8)(v>>24) };
              if (memcmp(page + off2, want, 4) != 0) {
                printf("FAIL bfm %s @%s: mem[%u] mismatch\n",
                       bforms[fi].nm, btgts[ti].nm, gi2);
                fails++; goto brestore;
              }
            }
            off2 += 4; gi2++;
          }
          u32 want_base = do_wb ? wbv : base;
          if (is_load && (rlist & (1u << base_reg)))
            want_base = g_reg[base_reg];             /* loaded value wins */
          if (g_reg[base_reg] != want_base) {
            printf("FAIL bfm %s @%s: base=%08X want %08X\n",
                   bforms[fi].nm, btgts[ti].nm, g_reg[base_reg], want_base);
            fails++;
          }
        }
      brestore:
        if (page && !is_load && poff + count*4 <= 0x8000)
          memcpy((void *)(page + poff), mem_before, count*4);
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
