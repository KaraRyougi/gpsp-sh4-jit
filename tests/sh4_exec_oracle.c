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
u32 gamepak_size;
const u8 * const *cgba_gamepak_fragment_blocks;
u8 iwram[0x10000];                 /* gpSP global (push_iwram fast path) */
u8 ewram[0x80000];
u8 vram[1024 * 96];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u8 ws_cyc_seq[16][2], ws_cyc_nseq[16][2];
u32 cgba_sh4_native_thumb_const_io_count;
int cgba_sh4_extra_cycles;
int cgba_sh4_thumb_ldst(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_thumb_ldst_run(u32 c, u32 p){(void)c;(void)p;return 0;}
int cgba_sh4_arm_ldst(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_arm_psr(u32 o, u32 p){(void)o;(void)p;return 0;}
void sh4_block_exit(u32 pc){(void)pc;}
void sh4_helper_exit(u32 pc){(void)pc;}
void sh4_cycle_gate_exit(void){}
void sh4_op2_pc_mem_tramp(void){}
void sh4_fastmem_pc_mem_tramp(void){}
void sh4_fastmem_compact8_tramp(void){}
void sh4_fastmem_compact8_load_tramp(void){}
static struct { u32 ret_pc, pc_flags, opcode, cycle_count; } orc_fm_ool[64];
static unsigned orc_fm_ool_n;
int cgba_sh4_fm_ool_can_record(const u8 *tp){(void)tp;return 1;}
void cgba_sh4_fm_ool_push(u32 r, u32 p, u32 o, u32 c){
  unsigned idx = orc_fm_ool_n++ & 63u;
  orc_fm_ool[idx].ret_pc = r; orc_fm_ool[idx].pc_flags = p;
  orc_fm_ool[idx].opcode = o; orc_fm_ool[idx].cycle_count = c;
}
void cgba_sh4_fm_ool_reset_rom(void){ orc_fm_ool_n = 0; }
void cgba_sh4_fm_ool_reset_ram(void){ orc_fm_ool_n = 0; }
void cgba_sh4_fm_ool_reset_all(void){ orc_fm_ool_n = 0; }
void cgba_sh4_fm_ool_rewind(const u8 *p, int r){ (void)p; (void)r; }
u8 *cgba_sh4_fm_ool_rom_limit(u32 t){(void)t;return NULL;}
struct cgba_sh4_fm_desc;
const struct cgba_sh4_fm_desc *cgba_sh4_fm_ool_cache[512];
const struct cgba_sh4_fm_desc *cgba_sh4_fastmem_ool_lookup(u32 r){(void)r;return 0;}
void sh4_op2_pc_tramp(void){}
void sh4_op2_tramp(void){}
void sh4_indirect_branch_thumb(u32 a){(void)a;}
void sh4_pc_redispatch(u32 pc){(void)pc;}
void sh4_update_gba(u32 pc){(void)pc;}
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
static u8 orc_vec_table[16 * 4];    /* MOV.L @(disp4,R9) architectural cap */
static void orc_vec_put(unsigned idx, const void *pf)
{
  u32 v = (u32)(uintptr_t)pf;
  orc_vec_table[idx * 4 + 0] = (u8)(v >> 24);
  orc_vec_table[idx * 4 + 1] = (u8)(v >> 16);
  orc_vec_table[idx * 4 + 2] = (u8)(v >> 8);
  orc_vec_table[idx * 4 + 3] = (u8)v;
}

/* Exercise the renderer-cache invalidation ABI of native palette/VRAM stores.
 * This standalone emitter harness does not include the full memory header. */
#define CGBA_VIDEO_BACKDROP_SHADOW_PALETTE 1
#define CGBA_VIDEO_DIRTY_SCANLINE 1
#define CGBA_PALETTE_DIRTY_ACTIVE 11u
volatile u32 palette_ram_dirty;
#define CGBA_VRAM_DIRTY_BG 1u
#define CGBA_VRAM_DIRTY_OBJ 2u
#define CGBA_VRAM_DIRTY_ALL 3u
#define CGBA_VRAM_DIRTY_ACTIVE 3u
volatile u32 cgba_vram_dirty;
volatile u32 cgba_video_io_dirty;
#define CGBA_SH4_REG_FORWARD 1
#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_mul_emit.h"
#include "ports/fxcg100/sh4/sh4_fastmem_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
int cgba_sh4_arm_block(u32 o, u32 p);
int cgba_sh4_thumb_block(u32 o, u32 p);
int cgba_sh4_arm_block(u32 o, u32 p){(void)o;(void)p;return 0;}
int cgba_sh4_thumb_block(u32 o, u32 p){(void)o;(void)p;return 0;}
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"
#include "ports/fxcg100/sh4/sh4_thumb_block_emit.h"

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
u8 *cgba_sh4_fastmem_io16_store;
u8 *cgba_sh4_fastmem_block_cold[4];
u8 *cgba_sh4_pop1_bx_routine;
u8 *cgba_sh4_thumb_bx_routine;
u32 cgba_iwram_code_end = 0x8000u;
u32 cgba_ewram_code_end = 0x40000u;
void sh4_pop1_bx_slow(void){}
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
  orc_vec_put(SH4G_VEC_iwram_data, (const void *)(iwram + 0x8000));
  orc_vec_put(SH4G_VEC_ewram_data, (const void *)ewram);
  orc_vec_put(SH4G_VEC_palette_dirty, (const void *)&palette_ram_dirty);
  orc_vec_put(SH4G_VEC_vram_dirty, (const void *)&cgba_vram_dirty);
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

enum {
  ORC_WIN_BYTES = 0,
  ORC_WIN_READ_MAP = 1,
  ORC_WIN_HOST_U32 = 2,
  ORC_WIN_HOST_PTR = 3,
  ORC_WIN_FRAGMENT_MAP = 4
};

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
  /* These published target globals are part of every generated fastmem
   * routine.  Model them implicitly so the older unmapped-page cases do not
   * each need boilerplate windows merely to observe their zero defaults. */
  if (size_log2 == 2 && addr == (u32)(uintptr_t)&gamepak_size)
    return gamepak_size;
  if (size_log2 == 2 &&
      addr == (u32)(uintptr_t)&cgba_gamepak_fragment_blocks)
    return (u32)(uintptr_t)cgba_gamepak_fragment_blocks;
  if (size_log2 == 2 && addr == (u32)(uintptr_t)&cgba_iwram_code_end)
    return cgba_iwram_code_end;
  if (size_log2 == 2 && addr == (u32)(uintptr_t)&cgba_ewram_code_end)
    return cgba_ewram_code_end;
  if (size_log2 == 2 && (addr - 0x50000000u) < sizeof(g_reg) && ((addr & 3u) == 0u))
    return g_reg[(addr - 0x50000000u) >> 2];

  int maptab = 0;
  u8 *hp = orc_resolve(addr, &maptab);
  if (!hp) { *ok = 0; return 0; }
  if (maptab == ORC_WIN_READ_MAP) {/* pointer table: emitted code indexes
                                      4-byte slots; host slots are 8 bytes */
    u32 idx = (u32)(hp - (u8 *)(void *)memory_map_read) / 4;
    return (u32)(uintptr_t)memory_map_read[idx];
  }
  if (maptab == ORC_WIN_HOST_U32)
    return gamepak_size;           /* target-native u32 on a BE SH4 */
  if (maptab == ORC_WIN_HOST_PTR)
    return (u32)(uintptr_t)cgba_gamepak_fragment_blocks;
  if (maptab == ORC_WIN_FRAGMENT_MAP) {
    u32 idx = (u32)(hp - (u8 *)(void *)cgba_gamepak_fragment_blocks) / 4;
    return (u32)(uintptr_t)cgba_gamepak_fragment_blocks[idx];
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
  if (size_log2 == 2 && (addr - 0x50000000u) < sizeof(g_reg) && ((addr & 3u) == 0u)) {
    g_reg[(addr - 0x50000000u) >> 2] = v;
    return;
  }
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
static u32 orc_slow_target3;       /* trunc(&sh4_fastmem_pc_mem_tramp) */
static u32 orc_slow_target4;       /* trunc(&sh4_fastmem_compact8_tramp) */
static u32 orc_slow_target5;       /* trunc(&sh4_fastmem_compact8_load_tramp) */
static int orc_took_slow;
static u32 orc_final_cycles;

static int orc_is_slow_target(u32 a)
{
  return a == orc_slow_target || a == orc_slow_target2 ||
         a == orc_slow_target3 || a == orc_slow_target4 ||
         a == orc_slow_target5;
}

static int orc_fastmem_io16_direct_store(u32 addr)
{
  u32 off;
  if ((addr & 0xFFFFFC00u) != 0x04000000u || (addr & 1u))
    return 0;

  off = addr & 0x3FEu;
  if (off == 0x000u || off == 0x004u)
    return 1;
  return (off >= 0x008u && off <= 0x026u) ||
         (off >= 0x030u && off <= 0x036u) ||
         (off >= 0x040u && off <= 0x054u);
}

static int run_at(u32 pc, u32 pc_end)
{
  u32 R[16] = {0};
  u32 MACL = 0, PR = 0;
  int T = 0;
  int steps = 0;

  R[SH4_REG_BASE] = 0x50000000u;
  R[SH4_REG_CPSR] = g_reg[SH4_GREG_CPSR];
#if CGBA_SH4_PIN_GUEST_R0
  R[SH4_REG_GUEST_R0] = g_reg[0];
#endif
#if CGBA_SH4_PIN_GUEST_R1
  R[SH4_REG_GUEST_R1] = g_reg[1];
#endif
  R[SH4_REG_VEC] = (u32)(uintptr_t)orc_vec_table;
  R[SH4_REG_BEXIT] = (u32)(uintptr_t)sh4_block_exit;
  unmodeled[0] = 0;
  orc_took_slow = 0;
  orc_final_cycles = 0;

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
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "rts slot"); return 0; }
      if ((u16)slot == 0x0009) { /* NOP */ }
      else if ((slot & 0xF000u) == 0x7000u)
        R[(slot >> 8) & 0xFu] += (u32)(s32)(s8)(slot & 0xFFu);
      else if ((slot & 0xF00Fu) == 0x3008u)
        R[(slot >> 8) & 0xFu] -= R[(slot >> 4) & 0xFu];
      else {
        snprintf(unmodeled, sizeof unmodeled, "rts slot %04x", (unsigned)slot);
        return 0;
      }
      next = PR;
    }
    else if ((op & 0xF0FF) == 0x400B) {                             /* JSR @Rn */
      u32 target = R[nn];
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "jsr slot"); return 0; }
      if ((u16)slot == 0x0009) {}
      else if ((slot & 0xF000u) == 0xE000u)
        R[(slot >> 8) & 0xFu] = (u32)(s32)(s8)(slot & 0xFFu);
      else if ((slot & 0xF000u) == 0x7000u)
        R[(slot >> 8) & 0xFu] += (u32)(s32)(s8)(slot & 0xFFu);
      else if ((slot & 0xF00Fu) == 0x300Cu)
        R[(slot >> 8) & 0xFu] += R[(slot >> 4) & 0xFu];
      else if ((slot & 0xF00Fu) == 0x6003u)
        R[(slot >> 8) & 0xFu] = R[(slot >> 4) & 0xFu];
      else if ((slot & 0xF000u) == 0x5000u) {
        u32 v = orc_read(R[(slot >> 4) & 0xFu] + ((slot & 0xFu) << 2), 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "jsr slot load"); return 0; }
        R[(slot >> 8) & 0xFu] = v;
      }
      else if ((slot & 0xF000u) == 0x1000u) {
        orc_write(R[(slot >> 8) & 0xFu] + ((slot & 0xFu) << 2),
                  R[(slot >> 4) & 0xFu], 2, &okf);
        if (!okf) { snprintf(unmodeled, sizeof unmodeled, "jsr slot store"); return 0; }
      }
      else { snprintf(unmodeled, sizeof unmodeled, "jsr slot %04x", (unsigned)slot); return 0; }
      if (orc_is_slow_target(target)) {         /* call into a C tramp stub */
        orc_took_slow = 1;
        snprintf(unmodeled, sizeof unmodeled, "jmp/jsr (slow path taken)");
        return 0;
      }
      PR = pc + 4;
      next = target;
    }
    else if ((op & 0xF0FF) == 0x402B) {                             /* JMP @Rn */
      u32 target = R[nn];
      if (orc_is_slow_target(target)) {
        orc_took_slow = 1;
        snprintf(unmodeled, sizeof unmodeled, "jmp/jsr (slow path taken)");
        return 0;
      }
      if (target == (u32)(uintptr_t)sh4_helper_exit) {
        g_reg[SH4_GREG_CPSR] = R[SH4_REG_CPSR];
#if CGBA_SH4_PIN_GUEST_R0
        g_reg[0] = R[SH4_REG_GUEST_R0];
#endif
#if CGBA_SH4_PIN_GUEST_R1
        g_reg[1] = R[SH4_REG_GUEST_R1];
#endif
        orc_final_cycles = R[SH4_REG_CYCLES];
        return 1;                                      /* expected PC redispatch */
      }
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "jmp slot"); return 0; }
      if ((u16)slot == 0x0009) {}
      else if ((slot & 0xF000u) == 0x7000u)
        R[(slot >> 8) & 0xFu] += (u32)(s32)(s8)(slot & 0xFFu);
      else if ((slot & 0xF00Fu) == 0x3008u)
        R[(slot >> 8) & 0xFu] -= R[(slot >> 4) & 0xFu];
      else {
        snprintf(unmodeled, sizeof unmodeled, "jmp slot %04x", (unsigned)slot);
        return 0;
      }
      next = target;
    }
    else if ((op & 0xF0FF) == 0x0023) {                             /* BRAF Rm */
      u32 target = pc + 4 + R[nn];
      u32 slot = orc_read(pc + 2, 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "braf slot"); return 0; }
      if ((u16)slot == 0x0009) {}
      else if ((slot & 0xF000u) == 0x7000u)
        R[(slot >> 8) & 0xFu] += (u32)(s32)(s8)(slot & 0xFFu);
      else { snprintf(unmodeled, sizeof unmodeled, "braf slot %04x", (unsigned)slot); return 0; }
      next = target;
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
    else if ((op & 0xF0FF) == 0x4021) { T = (int)(R[nn] & 1); R[nn] = (u32)((s32)R[nn] >> 1); }
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
    else if ((op & 0xFF00) == 0x8000) {                             /* MOV.B R0,@(d,Rn) */
      orc_write(R[mm] + (u32)(op & 0xF), R[0], 0, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "80xx @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xFF00) == 0x8100) {                             /* MOV.W R0,@(d*2,Rn) */
      orc_write(R[mm] + (u32)(op & 0xF) * 2, R[0], 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "81xx @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xFF00) == 0x8400) {                             /* MOV.B @(d,Rm),R0 */
      R[0] = (u32)(s32)(s8)orc_read(R[mm] + (u32)(op & 0xF), 0, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "84xx @%08X", R[mm]); return 0; }
    }
    else if ((op & 0xFF00) == 0x8500) {                             /* MOV.W @(d*2,Rm),R0 */
      R[0] = (u32)(s32)(int16_t)(u16)orc_read(R[mm] + (u32)(op & 0xF) * 2, 1, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "85xx @%08X", R[mm]); return 0; }
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
    else if ((op & 0xF00F) == 0x6006) {                             /* MOV.L @Rm+,Rn */
      u32 addr = R[mm];
      R[mm] = addr + 4;
      R[nn] = orc_read(addr, 2, &okf);
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "mov.l @%08X+", addr); return 0; }
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
      if (!okf) { snprintf(unmodeled, sizeof unmodeled, "bra slot"); return 0; }
      if ((u16)slot == 0x0009) {}
      else if ((slot & 0xF000u) == 0x7000u)
        R[(slot >> 8) & 0xFu] += (u32)(s32)(s8)(slot & 0xFFu);
      else { snprintf(unmodeled, sizeof unmodeled, "bra slot %04x", (unsigned)slot); return 0; }
      next = pc + 4 + (u32)(d * 2);
    }
    else {
      snprintf(unmodeled, sizeof unmodeled, "op %04x @%08X", op, pc);
      return 0;
    }
    pc = next;
  }
  g_reg[SH4_GREG_CPSR] = R[SH4_REG_CPSR];
#if CGBA_SH4_PIN_GUEST_R0
  g_reg[0] = R[SH4_REG_GUEST_R0];
#endif
#if CGBA_SH4_PIN_GUEST_R1
  g_reg[1] = R[SH4_REG_GUEST_R1];
#endif
  orc_final_cycles = R[SH4_REG_CYCLES];
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
  orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
  orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
  orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
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
    st->reg[rd] = ((op == 0) ? (a + b) : b) & ((rd == 15) ? ~1u : ~0u);
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


/* Execute adjacent guest ALU instructions with and without the driver opening
 * a forwarding window. Include explicit boundary resets and pinned r0. */
static void check_register_forwarding(void)
{
  static u8 buf[4096];
  const u32 values[] = {0, 1, 0xffffffff, 0x7fffffff, 0x80000000, 0xa5a5a5a5};
  for (unsigned arm = 0; arm < 2; arm++)
  for (unsigned rd = 0; rd < (arm ? 15u : 8u); rd++)
    for (unsigned v = 0; v < sizeof(values)/sizeof(values[0]); v++)
      for (unsigned boundary = 0; boundary < 2; boundary++) {
        size_t sizes[2];
        for (unsigned enabled = 0; enabled < 2; enabled++) {
          u8 *p = buf;
          sh4_greg_forward_reset();
          /* ADD rd,#7; ADD r7,rd,#3; both flags dead. */
          if (arm)
            sh4g_arm_dp_native(&p, 0xe2800007 | (rd << 16) | (rd << 12), 0x8000100, 0);
          else
            sh4g_thumb_dp_native(&p, 0x3007 | (rd << 8), 0x8000100, 0, 0);
          if (enabled) sh4_greg_forward_finish(buf, p, 1);
          if (boundary) sh4_greg_forward_reset();
          if (arm)
            sh4g_arm_dp_native(&p, 0xe2807003 | (rd << 16), 0x8000104, 0);
          else
            sh4g_thumb_dp_native(&p, 0x1c00 | (3 << 6) | (rd << 3) | 7,
                                 0x8000102, 0, 0);
          sh4_greg_forward_reset();
          sizes[enabled] = p - buf;
          for (unsigned i = 0; i < 16; i++) g_reg[i] = values[v] + i;
          g_reg[SH4_GREG_CPSR] = 0xa000003f;
          u32 expected[16]; memcpy(expected, g_reg, sizeof(expected));
          expected[rd] += 7; expected[7] = expected[rd] + 3;
          cases++;
          if (!run_sh4x(buf, p-buf) || memcmp(expected, g_reg, sizeof(expected)) ||
              g_reg[SH4_GREG_CPSR] != 0xa000003f) {
            printf("FAIL register forwarding rd=%u v=%u boundary=%u enabled=%u\n",
                   rd, v, boundary, enabled); fails++;
          }
        }
        if (sizes[0] - sizes[1] != (sh4_pinned_reg(rd) < 0 && !boundary ? 2u : 0u)) {
          printf("FAIL register forwarding size rd=%u boundary=%u\n", rd, boundary); fails++;
        }
      }
}

int main(void)
{
  static const u8 init_seq[16][2] = {
    {1,1}, {1,1}, {3,6}, {1,1}, {1,1}, {1,2}, {1,2}, {1,2},
    {3,6}, {3,6}, {5,10}, {5,10}, {9,18}, {9,18}, {1,1}, {1,1}
  };
  static const u8 init_nseq[16][2] = {
    {1,1}, {1,1}, {3,6}, {1,1}, {1,1}, {1,2}, {1,2}, {1,2},
    {5,9}, {5,9}, {5,11}, {5,11}, {5,15}, {5,15}, {1,1}, {1,1}
  };
  memcpy(ws_cyc_seq, init_seq, sizeof init_seq);
  memcpy(ws_cyc_nseq, init_nseq, sizeof init_nseq);
  orc_vec_init();
  check_register_forwarding();
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
    /* fmt5 hi-reg: regular, source-PC, and destination-PC forms */
    0x44C8, 0x45C8, 0x46C8, 0x447D, 0x457D, 0x467D,
    0x448F, 0x458F, 0x468F, 0x44FF, 0x45FF, 0x46FF, 0x46F7
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
                /* Model fixed set_cpu_mode() over logical values:
                 * - Leaving FIQ: save r8..r14 to MODE_FIQ (row 2), restore
                 *   r8..r12 from MODE_USER (row 0), load r13..r14 from new row.
                 * - Entering FIQ: save r13..r14 to old row, save r8..r12 to
                 *   MODE_USER (row 0), load r8..r14 from MODE_FIQ (row 2).
                 * - Non-FIQ <-> Non-FIQ: retire/load r13..r14 only. */
                u32 or_ = old_mode & 0xF, nr = new_mode & 0xF;
                u32 rm_m[7][7], regs_m[16];
                int bad = 0;
                for (int r = 0; r < 7; r++)
                  for (int c = 0; c < 7; c++)
                    rm_m[r][c] = 0xB0000000u + (u32)r * 0x100u + (u32)c;
                for (int r = 0; r < 16; r++) regs_m[r] = 0xFACE0000u + (u32)r;
                regs_m[2] = rvals[ri];
                if (old_mode == 0x12) {
                  for (int r = 0; r < 7; r++) rm_m[2][r] = regs_m[8 + r];
                  for (int r = 0; r < 5; r++) regs_m[8 + r] = rm_m[0][r];
                  regs_m[13] = rm_m[nr][5];
                  regs_m[14] = rm_m[nr][6];
                } else if (new_mode == 0x12) {
                  for (int r = 0; r < 5; r++) rm_m[0][r] = regs_m[8 + r];
                  rm_m[or_][5] = regs_m[13];
                  rm_m[or_][6] = regs_m[14];
                  for (int r = 0; r < 7; r++) regs_m[8 + r] = rm_m[2][r];
                } else {
                  rm_m[or_][5] = regs_m[13];
                  rm_m[or_][6] = regs_m[14];
                  regs_m[13] = rm_m[nr][5];
                  regs_m[14] = rm_m[nr][6];
                }
                for (int r = 8; r < 15 && !bad; r++)
                  if (g_reg[r] != regs_m[r]) bad = 1;
                for (int row = 0; row < 7 && !bad; row++)
                  for (int c = 0; c < 7 && !bad; c++)
                    if (ORC_BSWAP32(reg_mode[row][c]) != rm_m[row][c]) bad = 1;
                if (bad) {
                  printf("FAIL msr rebank op=%08X old=%08X val=%08X "
                         "modes %02X->%02X: r13=%08X r14=%08X r8=%08X\n",
                         opcode, oldc, val, old_mode, new_mode,
                         g_reg[13], g_reg[14], g_reg[8]);
                  fails++;
                }
              } else {
                for (int r = 8; r < 15; r++) {
                  if (g_reg[r] != 0xFACE0000u + (u32)r) {
                    printf("FAIL msr op=%08X: r%d moved without mode change\n",
                           opcode, r);
                    fails++;
                    break;
                  }
                }
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

  /* ---- ARM MOV/MVN with register-specified shifts: Mario hot helper miss ---- */
  {
    static const u32 vals3[] = {
      0, 1, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x12345678u, 0xA5A50000u
    };
    static const u32 amounts[] = { 0, 1, 4, 31, 32, 33, 64, 255, 256 };
    for (u32 aop = 0xD; aop <= 0xF; aop += 2)
      for (u32 type = 0; type < 4; type++)
        for (unsigned vi = 0; vi < sizeof vals3 / sizeof *vals3; vi++)
          for (unsigned ai = 0; ai < sizeof amounts / sizeof *amounts; ai++) {
            u32 opcode = 0xE0000000u | (aop << 21) | (3u << 12)
                       | (5u << 8) | (type << 5) | 0x10u | 4u;
            static u8 abuf[1024]; u8 *ap = abuf;
            u32 val = vals3[vi], amount = amounts[ai] & 0xFF;
            u32 shifted;

            if (!sh4g_arm_dp_native(&ap, opcode, pc, 0)) {
              printf("FAIL adp-regshift op=%08X: native rejected\n", opcode);
              fails++; continue;
            }
            cases++;

            memset(g_reg, 0, sizeof g_reg);
            for (int i = 0; i < 16; i++) g_reg[i] = 0xCD000000u + (u32)i;
            g_reg[3] = 0x51515151u;
            g_reg[4] = val;
            g_reg[5] = amounts[ai];
            g_reg[SH4_GREG_CPSR] = 0xA000001Fu;

            if (!run_sh4x(abuf, (size_t)(ap - abuf))) {
              printf("FAIL adp-regshift op=%08X: interpreter: %s\n",
                     opcode, unmodeled);
              fails++; continue;
            }

            if (amount == 0) {
              shifted = val;
            } else {
              switch (type) {
              case 0: shifted = (amount < 32) ? (val << amount) : 0; break;
              case 1: shifted = (amount < 32) ? (val >> amount) : 0; break;
              case 2: shifted = (amount < 32) ? (u32)((s32)val >> amount)
                                              : (u32)((s32)val >> 31); break;
              default: {
                u32 rot = amount & 31;
                shifted = rot ? ((val >> rot) | (val << (32 - rot))) : val;
                break;
              }
              }
            }

            u32 want = (aop == 0xD) ? shifted : ~shifted;
            if (g_reg[3] != want || g_reg[4] != val || g_reg[5] != amounts[ai] ||
                g_reg[SH4_GREG_CPSR] != 0xA000001Fu) {
              printf("FAIL adp-regshift op=%08X val=%08X amt=%08X: "
                     "rd=%08X want %08X r4=%08X r5=%08X cpsr=%08X\n",
                     opcode, val, amounts[ai], g_reg[3], want, g_reg[4],
                     g_reg[5], g_reg[SH4_GREG_CPSR]);
              fails++;
            }
          }
  }

  /* ---- fastmem: real sites calling real routines over a modeled map ---- */
  {
    static u8 fm_buf[10240];
    u8 *giwram = iwram;          /* tags 0..0x7FFF, data 0x8000.. */
    const u32 giwram_size = 0x10000;
    static u8 rom[0x8000];
    u8 *tp = fm_buf;
    const u32 pc = 0x08000100;

    cgba_sh4_fastmem_io16_store = sh4g_fastmem_emit_io16_store(&tp);
    memset(cgba_sh4_fastmem_block_cold, 0, sizeof(cgba_sh4_fastmem_block_cold));
    tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
    cgba_sh4_fastmem_block_cold[CGBA_FMB_LDM_WB - CGBA_FMB_LDM] =
      sh4g_fastmem_emit_block_cold(&tp, CGBA_FMB_LDM_WB);
    tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
    cgba_sh4_fastmem_block_cold[CGBA_FMB_STM_WB - CGBA_FMB_LDM] =
      sh4g_fastmem_emit_block_cold(&tp, CGBA_FMB_STM_WB);
    for (int fm = 0; fm < CGBA_FM_COUNT; fm++) {
      tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
      cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_routine(&tp, fm);
    }
    for (int fm = CGBA_FMB_LDM; fm < CGBA_FM_TOTAL; fm++) {
      tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
      cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_block_routine(&tp, fm);
    }
    {
      size_t bx_off = 0;
      tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
      cgba_sh4_pop1_bx_routine = sh4g_fastmem_emit_pop1_bx(&tp, &bx_off);
      cgba_sh4_thumb_bx_routine = cgba_sh4_pop1_bx_routine + bx_off;
    }
    cgba_sh4_psr_rebank_routine = sh4g_psr_emit_rebank_routine(&tp);
    if (tp > fm_buf + sizeof fm_buf) {
      printf("FAIL fastmem resident image uses %zu bytes (budget %zu)\n",
             (size_t)(tp - fm_buf), sizeof fm_buf);
      fails++;
    }
    if (getenv("ORC_PRINT_FM"))
      for (int fm = 0; fm < CGBA_FM_TOTAL; fm++)
        fprintf(stderr, "fm[%d] off=0x%zx\n", fm,
                (size_t)(cgba_sh4_fastmem_routine[fm] - fm_buf));
    if (getenv("ORC_PRINT_FM"))
      fprintf(stderr, "fm resident used=0x%zx / 0x%zx\n",
              (size_t)(tp - fm_buf), sizeof fm_buf);

    /* Native STR/STRH notify every palette consumer, including cached BG
     * rows; test both ordinary and mirrored palette addresses. */
    for (unsigned word = 0; word < 2; word++)
      for (unsigned mirror = 0; mirror < 2; mirror++) {
        u8 sbuf[512], *sp = sbuf;
        u32 op = word ? 0xe5823000u : 0xe1c230b0u;
        memset(g_reg, 0, sizeof(g_reg));
        g_reg[2] = 0x05000020u + mirror * 0x400;
        g_reg[3] = 0x12345678;
        palette_ram_dirty = 0;
        int emitted = sh4g_arm_ldst_native(&sp, op, pc, 0);
        orc_reset_windows();
        orc_add_window(sbuf, sizeof(sbuf), ORC_WIN_BYTES);
        orc_add_window(fm_buf, sizeof(fm_buf), ORC_WIN_BYTES);
        orc_add_window(palette_ram, sizeof(palette_ram), ORC_WIN_BYTES);
        orc_add_window(palette_ram_converted, sizeof(palette_ram_converted), ORC_WIN_BYTES);
        orc_add_window((const void *)&palette_ram_dirty, sizeof(palette_ram_dirty), ORC_WIN_BYTES);
        orc_add_window(orc_vec_table, sizeof(orc_vec_table), ORC_WIN_BYTES);
        orc_add_window(ws_cyc_seq, sizeof(ws_cyc_seq), ORC_WIN_BYTES);
        orc_add_window(ws_cyc_nseq, sizeof(ws_cyc_nseq), ORC_WIN_BYTES);
        int ok = 1;
        cases++;
        if (!emitted || !run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sp) ||
            orc_read((u32)(uintptr_t)&palette_ram_dirty, 2, &ok) != CGBA_PALETTE_DIRTY_ACTIVE || !ok) {
          printf("FAIL native palette cache notification word=%u mirror=%u (%s)\n", word, mirror, unmodeled);
          fails++;
        }
      }

    /* Native STR/STRH/STM to VRAM notify cgba_vram_dirty with exact BG/OBJ
     * bit (including the 0x06018000 OBJ mirror). */
    {
      static const struct { u32 addr; u32 want; const char *nm; } vcases[] = {
        { 0x06000020u, CGBA_VRAM_DIRTY_BG,  "bg0" },
        { 0x06008020u, CGBA_VRAM_DIRTY_BG,  "bg1" },
        { 0x06010020u, CGBA_VRAM_DIRTY_OBJ, "obj" },
        { 0x06018020u, CGBA_VRAM_DIRTY_OBJ, "obj_mirror" }
      };
      memory_map_read[0x06000000u >> 15] = vram;
      memory_map_read[0x06008000u >> 15] = vram + 0x8000;
      memory_map_read[0x06010000u >> 15] = vram + 0x10000;
      memory_map_read[0x06018000u >> 15] = vram + 0x10000;
      for (unsigned ci = 0; ci < sizeof(vcases) / sizeof(vcases[0]); ci++) {
        for (unsigned mode = 0; mode < 3; mode++) {
          u8 sbuf[512], *sp = sbuf;
          u32 op = (mode == 0) ? 0xe5823000u /* STR r3,[r2] */
                 : (mode == 1) ? 0xe1c230b0u /* STRH r3,[r2] */
                               : 0xe8820038u;/* STMIA r2,{r3-r5} */
          memset(g_reg, 0, sizeof(g_reg));
          g_reg[2] = vcases[ci].addr;
          g_reg[3] = 0x11223344u;
          g_reg[4] = 0x55667788u;
          g_reg[5] = 0x99aabbccu;
          cgba_vram_dirty = 0;
          int emitted = (mode < 2) ? sh4g_arm_ldst_native(&sp, op, pc, 0)
                                   : sh4g_arm_block_native(&sp, op, pc, 0);
          orc_reset_windows();
          orc_add_window(sbuf, sizeof(sbuf), ORC_WIN_BYTES);
          orc_add_window(fm_buf, sizeof(fm_buf), ORC_WIN_BYTES);
          orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
          orc_add_window(vram, sizeof(vram), ORC_WIN_BYTES);
          orc_add_window((const void *)&cgba_vram_dirty, sizeof(cgba_vram_dirty), ORC_WIN_BYTES);
          orc_add_window(orc_vec_table, sizeof(orc_vec_table), ORC_WIN_BYTES);
          orc_add_window(ws_cyc_seq, sizeof(ws_cyc_seq), ORC_WIN_BYTES);
          orc_add_window(ws_cyc_nseq, sizeof(ws_cyc_nseq), ORC_WIN_BYTES);
          int ok = 1;
          cases++;
          if (!emitted || !run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sp) ||
              orc_read((u32)(uintptr_t)&cgba_vram_dirty, 2, &ok) != vcases[ci].want || !ok) {
            printf("FAIL native VRAM dirty %s mode=%u got=%u want=%u (%s)\n",
                   vcases[ci].nm, mode,
                   orc_read((u32)(uintptr_t)&cgba_vram_dirty, 2, &ok),
                   vcases[ci].want, unmodeled);
            fails++;
          }
        }
      }
    }

    memset(memory_map_read, 0, sizeof memory_map_read);
    memset(giwram, 0, 0x10000);
    memory_map_read[0x02000000u >> 15] = ewram;
    memory_map_read[0x03000000u >> 15] = giwram + 0x8000;
    memory_map_read[0x04000000u >> 15] = (u8 *)io_registers;
    memory_map_read[0x06000000u >> 15] = vram;
    memory_map_read[0x08000000u >> 15] = rom;

    for (unsigned i = 0; i < sizeof rom; i++)   rom[i]   = (u8)(0x11 + i * 7);
    for (unsigned i = 0; i < 0x8000; i++) {
      giwram[0x8000 + i] = (u8)(0x23 + i * 3);
      vram[i]           = (u8)(0x47 + i * 11);
    }
    for (unsigned i = 0; i < 0x40000; i++)
      ewram[i] = (u8)(0x35 + i * 5 + (i >> 14));
    for (unsigned i = 0x1230; i < 0x1260; i++)  /* SMC tag range: covers the
        pre/post/reg-offset effective addresses of the smc targets */
      giwram[i] = 1;
    for (unsigned i = 0x2330; i < 0x2360; i++)
      ewram[0x40000 + i] = 1;

    struct tgt { u32 base; int slow_ld; int slow_st; const char *nm; } tgts[] = {
      { 0x03000100u, 0, 0, "iwram" },
      { 0x02000200u, 0, 0, "ewram" },
      { 0x02052340u, 0, 0, "ewram-mirror-high" },
      { 0x02042340u, 0, 2, "ewram-mirror-smc" },
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
          if (reg == 2) { page = ewram; poff = eff & 0x3FFFF; }
          else if (reg == 3) { page = giwram + 0x8000; poff = eff & 0x7FFF; }
          else if (reg == 6) { page = vram; poff = eff & 0x7FFF; }
          if (page) memcpy(mem_before, page + poff, 4);

          orc_reset_windows();
          orc_add_window(sbuf, sizeof sbuf, 0);
          orc_add_window(fm_buf, sizeof fm_buf, 0);
          orc_add_window(io_registers, sizeof io_registers, 0);
          orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
          orc_add_window(memory_map_read, 8192 * 4, 1);
          orc_add_window(giwram, giwram_size, 0);
          orc_add_window(ewram, sizeof ewram, 0);
          orc_add_window(vram, sizeof vram, 0);
          orc_add_window((const void *)&cgba_vram_dirty, sizeof(cgba_vram_dirty), 0);
        orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
          orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
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
            else if (kind == LDK_UH && orc_fastmem_io16_direct_store(eff))
              want_slow = 0;
          }
          if (want_slow != slow) {
            printf("FAIL fm %s @%s%s: slow=%d want %d\n",
                   forms[fi].nm, tgts[ti].nm, mis ? "+1" : "", slow, want_slow);
            fails++;
            if (page) memcpy((void *)(page + poff), mem_before, 4);
            continue;
          }
          if (slow) { if (page) memcpy((void *)(page + poff), mem_before, 4); continue; }

          {
            u32 cost = ws_cyc_nseq[(eff >> 24) & 0xFu]
                                      [kind == LDK_W ? 1 : 0];
            if (orc_final_cycles != 0u - cost) {
              printf("FAIL fm %s @%s: cycles=%u want %u\n",
                     forms[fi].nm, tgts[ti].nm, orc_final_cycles, 0u - cost);
              fails++;
            }
          }

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

    /* A NULL 32 KiB ROM page can still be composed of direct 4 KiB NOR
     * fragments. Exercise the emitted second-level lookup itself (not its C
     * fallback), every scalar load kind, writeback, and each fail-closed guard.
     * The fragment lives in logical block 1 so a stale low-15 offset cannot
     * accidentally address the right byte. */
    {
      static u8 fragment[0x1000];
      static const u8 *fragment_map[8192];
      const u32 faddr = 0x08001420u;
      const u32 fpage = faddr >> 15;
      const u32 fblock = (faddr & 0x01FFFFFFu) >> 12;
      struct fload { u32 op; int kind; int wb; const char *nm; } floads[] = {
        { 0xE5923000u, LDK_W,  0, "fragment-ldr"   },
        { 0xE5D23000u, LDK_B,  0, "fragment-ldrb"  },
        { 0xE1D230B0u, LDK_UH, 0, "fragment-ldrh"  },
        { 0xE1D230D0u, LDK_SB, 0, "fragment-ldrsb" },
        { 0xE1D230F0u, LDK_SH, 0, "fragment-ldrsh" },
        { 0xE5B23010u, LDK_W,  1, "fragment-ldr-wb" },
      };

      for (unsigned i = 0; i < sizeof fragment; i++)
        fragment[i] = (u8)(0x83u + i * 29u);
      memset(fragment_map, 0, sizeof fragment_map);
      fragment_map[fblock] = fragment;
      memory_map_read[fpage] = NULL;
      gamepak_size = 0x8000u;
      cgba_gamepak_fragment_blocks = fragment_map;

      for (unsigned fi = 0; fi < sizeof floads / sizeof *floads; fi++) {
        static u8 sbuf[512];
        u8 *sp = sbuf;
        u32 base = faddr - (floads[fi].wb ? 0x10u : 0u);
        u32 off = faddr & 0x0FFFu;
        u32 lev = (u32)fragment[off] | ((u32)fragment[off + 1] << 8) |
                  ((u32)fragment[off + 2] << 16) |
                  ((u32)fragment[off + 3] << 24);
        u32 want;

        if (!sh4g_arm_ldst_native(&sp, floads[fi].op, pc, 0)) {
          printf("FAIL %s: emitter rejected\n", floads[fi].nm);
          fails++;
          continue;
        }
        cases++;
        memset(g_reg, 0, sizeof g_reg);
        for (int i = 0; i < 16; i++) g_reg[i] = 0xF1000000u + (u32)i;
        g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
        g_reg[2] = base;
        g_reg[3] = 0xDEAD0001u;

        orc_reset_windows();
        orc_add_window(sbuf, sizeof sbuf, ORC_WIN_BYTES);
        orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
        orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
        orc_add_window(fragment, sizeof fragment, ORC_WIN_BYTES);
        orc_add_window(fragment_map, 8192 * 4, ORC_WIN_FRAGMENT_MAP);
        orc_add_window(&gamepak_size, 4, ORC_WIN_HOST_U32);
        orc_add_window(&cgba_gamepak_fragment_blocks, 4, ORC_WIN_HOST_PTR);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
        orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
        orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
        orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;

        if (!run_at((u32)(uintptr_t)sbuf,
                    (u32)(uintptr_t)sbuf + (u32)(sp - sbuf))) {
          printf("FAIL %s: %s%s\n", floads[fi].nm,
                 orc_took_slow ? "took slow path: " : "interpreter: ",
                 unmodeled);
          fails++;
          continue;
        }
        switch (floads[fi].kind) {
        case LDK_W:  want = lev; break;
        case LDK_B:  want = lev & 0xFFu; break;
        case LDK_UH: want = lev & 0xFFFFu; break;
        case LDK_SH: want = (u32)(s32)(int16_t)(lev & 0xFFFFu); break;
        default:     want = (u32)(s32)(s8)(lev & 0xFFu); break;
        }
        if (g_reg[3] != want || g_reg[2] != (floads[fi].wb ? faddr : base)) {
          printf("FAIL %s: rd/rn=%08X/%08X want %08X/%08X\n",
                 floads[fi].nm, g_reg[3], g_reg[2], want,
                 floads[fi].wb ? faddr : base);
          fails++;
        }
      }

      {
        struct fguard {
          u32 address, size;
          int table_active, entry_active;
          const char *nm;
        } fguards[] = {
          { faddr,        0x8000u, 0, 1, "fragment-null-table" },
          { faddr,        0x8000u, 1, 0, "fragment-null-entry" },
          { faddr,        0x1000u, 1, 1, "fragment-past-size" },
          { 0x07001420u, 0x8000u, 1, 1, "fragment-region7" },
          { 0x0D001420u, 0x2000000u, 1, 1, "fragment-regionD" },
        };

        for (unsigned gi = 0; gi < sizeof fguards / sizeof *fguards; gi++) {
          static u8 sbuf[512];
          u8 *sp = sbuf;
          u32 block = (fguards[gi].address & 0x01FFFFFFu) >> 12;

          memset(fragment_map, 0, sizeof fragment_map);
          if (fguards[gi].entry_active)
            fragment_map[block] = fragment;
          cgba_gamepak_fragment_blocks = fguards[gi].table_active
            ? fragment_map : NULL;
          gamepak_size = fguards[gi].size;
          memory_map_read[fguards[gi].address >> 15] = NULL;
          memset(g_reg, 0, sizeof g_reg);
          g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
          g_reg[2] = fguards[gi].address;
          g_reg[3] = 0xDEAD0001u;
          if (!sh4g_arm_ldst_native(&sp, 0xE5923000u, pc, 0)) {
            printf("FAIL %s: emitter rejected\n", fguards[gi].nm);
            fails++;
            continue;
          }
          cases++;

          orc_reset_windows();
          orc_add_window(sbuf, sizeof sbuf, ORC_WIN_BYTES);
          orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
          orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
          orc_add_window(fragment, sizeof fragment, ORC_WIN_BYTES);
          orc_add_window(fragment_map, 8192 * 4, ORC_WIN_FRAGMENT_MAP);
          orc_add_window(&gamepak_size, 4, ORC_WIN_HOST_U32);
          orc_add_window(&cgba_gamepak_fragment_blocks, 4, ORC_WIN_HOST_PTR);
          orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
          orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
          orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
          orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
          orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

          if (run_at((u32)(uintptr_t)sbuf,
                     (u32)(uintptr_t)sbuf + (u32)(sp - sbuf)) ||
              !orc_took_slow) {
            printf("FAIL %s: guard did not take slow path (%s)\n",
                   fguards[gi].nm, unmodeled);
            fails++;
          }
        }
      }

      memset(fragment_map, 0, sizeof fragment_map);
      cgba_gamepak_fragment_blocks = NULL;
      gamepak_size = 0;
      memory_map_read[fpage] = rom;
    }

    /* Unified dual-region ARM load-run oracle (IWRAM + ROM8 hits/misses) */
    {
      static u8 rom_p8[4096], rom_p9[4096];
      static const u8 *frag_map[8192];
      const u32 old_gp_size = gamepak_size;
      const u8 * const *old_frags = cgba_gamepak_fragment_blocks;
      u8 *old_m8 = memory_map_read[0x08008000u >> 15];
      for (unsigned i = 0; i < 4096u; i++) {
        rom_p8[i] = (u8)(0x5Au ^ (i * 13u + (i >> 4)));
        rom_p9[i] = (u8)(0xC3u ^ (i * 17u + (i >> 3)));
      }
      memset(frag_map, 0, sizeof frag_map);
      frag_map[8] = rom_p8;
      frag_map[9] = rom_p9;
      gamepak_size = 0x10000u;
      cgba_gamepak_fragment_blocks = frag_map;
      memory_map_read[0x08008000u >> 15] = NULL;
      ws_cyc_nseq[8][0] = 3;
      ws_cyc_nseq[8][1] = 6;

      /* Path 1b: dual-region reg-offset byte run (ROM8 hit, ROM8 cross-page,
       * IWRAM hit, EWRAM fallback). */
      {
        const u32 ops[3] = {
          0xE7D01005u, /* LDRB  r1, [r0, r5] */
          0xE19020D6u, /* LDRSB r2, [r0, r6] */
          0xE7D03007u, /* LDRB  r3, [r0, r7] */
        };
        const int ccs[3] = { 1, 1, 1 };
        const struct { u32 base, o5, o6, o7; const char *nm; } sub[] = {
          { 0x08008100u, 0x04u, 0x15u, 0x22u, "rom8-hit" },
          { 0x08008FE0u, 0x10u, 0x30u, 0x38u, "rom8-cross-page" },
          { 0x03000200u, 0x03u, 0x0Au, 0x11u, "iwram-hit" },
          { 0x02000400u, 0x01u, 0x07u, 0x0Eu, "ewram-miss" },
        };
        for (int in_iw = 0; in_iw <= 1; in_iw++) {
          static u8 lbuf[2048];
          u8 *sp = lbuf;
          u32 lpc = in_iw ? 0x03000100u : 0x08000100u;
          int got_n = sh4g_arm_load_run_native(&sp, ops, ccs, 3, lpc);
          if (got_n != 3) {
            printf("FAIL arm-load-run Path1b in_iw=%d got_n=%d\n", in_iw, got_n);
            fails++;
            continue;
          }
          for (unsigned si = 0; si < sizeof sub / sizeof *sub; si++) {
            memset(g_reg, 0, sizeof g_reg);
            g_reg[0] = sub[si].base;
            g_reg[5] = sub[si].o5;
            g_reg[6] = sub[si].o6;
            g_reg[7] = sub[si].o7;
            orc_reset_windows();
            orc_add_window(lbuf, sizeof lbuf, ORC_WIN_BYTES);
            orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
            orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
            orc_add_window(giwram, giwram_size, ORC_WIN_BYTES);
            orc_add_window(ewram, sizeof ewram, ORC_WIN_BYTES);
            orc_add_window(rom_p8, sizeof rom_p8, ORC_WIN_BYTES);
            orc_add_window(rom_p9, sizeof rom_p9, ORC_WIN_BYTES);
            orc_add_window(frag_map, 8192 * 4, ORC_WIN_FRAGMENT_MAP);
            orc_add_window(&gamepak_size, 4, ORC_WIN_HOST_U32);
            orc_add_window(&cgba_gamepak_fragment_blocks, 4, ORC_WIN_HOST_PTR);
            orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
            orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
            orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
            orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
            orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
            orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
            orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
            cases++;
            if (!run_at((u32)(uintptr_t)lbuf, (u32)(uintptr_t)sp)) {
              printf("FAIL arm-load-run Path1b %s in_iw=%d: %s\n",
                     sub[si].nm, in_iw, unmodeled);
              fails++;
              continue;
            }
            u32 a1 = sub[si].base + sub[si].o5;
            u32 a2 = sub[si].base + sub[si].o6;
            u32 a3 = sub[si].base + sub[si].o7;
            u8 b1 = (a1 >> 24) == 8 ? frag_map[(a1 - 0x08000000u) >> 12][a1 & 0xFFFu]
                  : (a1 >> 24) == 3 ? giwram[0x8000u + (a1 & 0x7FFFu)]
                  : ewram[a1 & 0x3FFFFu];
            s8 b2 = (s8)((a2 >> 24) == 8 ? frag_map[(a2 - 0x08000000u) >> 12][a2 & 0xFFFu]
                  : (a2 >> 24) == 3 ? giwram[0x8000u + (a2 & 0x7FFFu)]
                  : ewram[a2 & 0x3FFFFu]);
            u8 b3 = (a3 >> 24) == 8 ? frag_map[(a3 - 0x08000000u) >> 12][a3 & 0xFFFu]
                  : (a3 >> 24) == 3 ? giwram[0x8000u + (a3 & 0x7FFFu)]
                  : ewram[a3 & 0x3FFFFu];
            if (g_reg[1] != (u32)b1 || g_reg[2] != (u32)(s32)b2 || g_reg[3] != (u32)b3) {
              printf("FAIL arm-load-run Path1b %s in_iw=%d: got (%08X,%08X,%08X) want (%08X,%08X,%08X)\n",
                     sub[si].nm, in_iw, g_reg[1], g_reg[2], g_reg[3],
                     (u32)b1, (u32)(s32)b2, (u32)b3);
              fails++;
            }
          }
        }
      }

      /* Path 2: same-base adjacent mixed-width burst with writeback AND
       * rd[last] == rn0 (verifies writeback ordering + ROM8/IWRAM/boundary/EWRAM). */
      {
        const u32 ops[4] = {
          0xE5B01004u, /* LDR   r1, [r0, #4]! */
          0xE1D020B4u, /* LDRH  r2, [r0, #4]  */
          0xE1D030D7u, /* LDRSB r3, [r0, #7]  */
          0xE5900008u, /* LDR   r0, [r0, #8]  (clobbers base r0 last!) */
        };
        const int ccs[4] = { 1, 1, 1, 1 };
        const struct { u32 base; const char *nm; } sub[] = {
          { 0x08008100u, "rom8-hit" },
          { 0x08008FF0u, "rom8-cross-page" },
          { 0x03000240u, "iwram-hit" },
          { 0x03007FF0u, "iwram-wrap-miss" },
          { 0x02000300u, "ewram-miss" },
        };
        for (int in_iw = 0; in_iw <= 1; in_iw++) {
          static u8 lbuf[2048];
          u8 *sp = lbuf;
          u32 lpc = in_iw ? 0x03000100u : 0x08000100u;
          int got_n = sh4g_arm_load_run_native(&sp, ops, ccs, 4, lpc);
          if (got_n != 4) {
            printf("FAIL arm-load-run Path2 in_iw=%d got_n=%d\n", in_iw, got_n);
            fails++;
            continue;
          }
          for (unsigned si = 0; si < sizeof sub / sizeof *sub; si++) {
            memset(g_reg, 0, sizeof g_reg);
            g_reg[0] = sub[si].base;
            orc_reset_windows();
            orc_add_window(lbuf, sizeof lbuf, ORC_WIN_BYTES);
            orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
            orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
            orc_add_window(giwram, giwram_size, ORC_WIN_BYTES);
            orc_add_window(ewram, sizeof ewram, ORC_WIN_BYTES);
            orc_add_window(rom_p8, sizeof rom_p8, ORC_WIN_BYTES);
            orc_add_window(rom_p9, sizeof rom_p9, ORC_WIN_BYTES);
            orc_add_window(frag_map, 8192 * 4, ORC_WIN_FRAGMENT_MAP);
            orc_add_window(&gamepak_size, 4, ORC_WIN_HOST_U32);
            orc_add_window(&cgba_gamepak_fragment_blocks, 4, ORC_WIN_HOST_PTR);
            orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
            orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
            orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
            orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
            orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
            orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
            orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
            cases++;
            if (!run_at((u32)(uintptr_t)lbuf, (u32)(uintptr_t)sp)) {
              printf("FAIL arm-load-run Path2 %s in_iw=%d: %s\n",
                     sub[si].nm, in_iw, unmodeled);
              fails++;
              continue;
            }
            u32 a0 = sub[si].base + 4u;
            u32 want1 = 0, want2 = 0, want3 = 0, want0 = 0;
            for (int k = 0; k < 4; k++) {
              u32 a = a0 + (u32)k;
              u8 bv = (a >> 24) == 8 ? frag_map[(a - 0x08000000u) >> 12][a & 0xFFFu]
                    : (a >> 24) == 3 ? giwram[0x8000u + (a & 0x7FFFu)]
                    : ewram[a & 0x3FFFFu];
              want1 |= (u32)bv << (k * 8);
            }
            for (int k = 0; k < 2; k++) {
              u32 a = a0 + 4u + (u32)k;
              u8 bv = (a >> 24) == 8 ? frag_map[(a - 0x08000000u) >> 12][a & 0xFFFu]
                    : (a >> 24) == 3 ? giwram[0x8000u + (a & 0x7FFFu)]
                    : ewram[a & 0x3FFFFu];
              want2 |= (u32)bv << (k * 8);
            }
            {
              u32 a = a0 + 7u;
              s8 bv = (s8)((a >> 24) == 8 ? frag_map[(a - 0x08000000u) >> 12][a & 0xFFFu]
                    : (a >> 24) == 3 ? giwram[0x8000u + (a & 0x7FFFu)]
                    : ewram[a & 0x3FFFFu]);
              want3 = (u32)(s32)bv;
            }
            for (int k = 0; k < 4; k++) {
              u32 a = a0 + 8u + (u32)k;
              u8 bv = (a >> 24) == 8 ? frag_map[(a - 0x08000000u) >> 12][a & 0xFFFu]
                    : (a >> 24) == 3 ? giwram[0x8000u + (a & 0x7FFFu)]
                    : ewram[a & 0x3FFFFu];
              want0 |= (u32)bv << (k * 8);
            }
            if (g_reg[1] != want1 || g_reg[2] != want2 ||
                g_reg[3] != want3 || g_reg[0] != want0) {
              printf("FAIL arm-load-run Path2 %s in_iw=%d: got (%08X,%08X,%08X,%08X) want (%08X,%08X,%08X,%08X)\n",
                     sub[si].nm, in_iw, g_reg[0], g_reg[1], g_reg[2], g_reg[3],
                     want0, want1, want2, want3);
              fails++;
            }
          }
        }
      }

      memory_map_read[0x08008000u >> 15] = old_m8;
      gamepak_size = old_gp_size;
      cgba_gamepak_fragment_blocks = old_frags;
    }

#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
    /* Soundless IWRAM stereo PCM mixer skippers (Peepholes 3 & 6):
     * verify both WS0+IWRAM soundless hit (advances live pointers/phase + exact
     * cycle debit, leaves IWRAM untouched) and non-WS0 EWRAM exact fallback. */
    {
      static u8 mbuf[4096];
      u32 iw_pc = 0x03000100u;
      u32 old_code_end = cgba_iwram_code_end;
      cgba_iwram_code_end = 0u; /* IWRAM mix region has zero SMC tags */
      ws_cyc_nseq[8][0] = 3u;
      ws_cyc_nseq[2][0] = 2u;

      /* 1. sh4g_arm_rmw_block4_ldrsb_mla_skip (reps = 1 and 2). */
      {
        static const u32 unit10[10] = {
          0xE890001Eu, /* LDMIA r0, {r1..r4} */
          0xE19570D6u, /* LDRSB r7, [r5, r6] */
          0xE0211897u, /* MLA r1, r7, r8, r1 */
          0xE0222997u, /* MLA r2, r7, r9, r2 */
          0xE2866001u, /* ADD r6, r6, #1 */
          0xE19570D6u, /* LDRSB r7, [r5, r6] */
          0xE0233897u, /* MLA r3, r7, r8, r3 */
          0xE0244997u, /* MLA r4, r7, r9, r4 */
          0xE2866001u, /* ADD r6, r6, #1 */
          0xE8A0001Eu, /* STMIA r0!, {r1..r4} */
        };
        u32 ops20[20];
        int ccs20[20];
        for (int i = 0; i < 20; i++) { ops20[i] = unit10[i % 10]; ccs20[i] = 0; }
        for (int reps = 1; reps <= 2; reps++) {
          u8 *mp = mbuf;
          sh4_greg_forward_reset();
          if (!sh4g_arm_rmw_block4_ldrsb_mla_skip(&mp, ops20, ccs20, reps, iw_pc)) {
            printf("FAIL mixer-skip block4 emit reject reps=%d\n", reps);
            fails++; continue;
          }
          if ((size_t)(mp - mbuf) >= 3800u) {
            printf("FAIL mixer-skip block4 size=%zu reps=%d\n", (size_t)(mp - mbuf), reps);
            fails++; continue;
          }
          for (int mode = 0; mode < 2; mode++) {
            u32 dst = 0x03000400u;
            u32 src = mode == 0 ? 0x08000200u : 0x02000400u;
            u32 idx0 = 4u;
            u32 init_w[8];
            for (int w = 0; w < 8; w++) {
              u32 v = 0x00100000u + (u32)(w * 0x12345);
              u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)(w * 4);
              init_w[w] = v;
              p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
            }
            memset(g_reg, 0, sizeof g_reg);
            g_reg[0] = dst; g_reg[5] = src; g_reg[6] = idx0;
            g_reg[8] = 0x00000025u; g_reg[9] = 0xFFFFFFE3u;
            orc_reset_windows();
            orc_add_window(mbuf, sizeof mbuf, ORC_WIN_BYTES);
            orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
            orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
            orc_add_window(giwram, giwram_size, ORC_WIN_BYTES);
            orc_add_window(ewram, sizeof ewram, ORC_WIN_BYTES);
            orc_add_window(rom, sizeof rom, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
            orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
            orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
            orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
            orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
            orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
            orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
            cases++;
            if (!run_at((u32)(uintptr_t)mbuf, (u32)(uintptr_t)mp)) {
              printf("FAIL mixer-skip block4 reps=%d mode=%d: %s\n", reps, mode, unmodeled);
              fails++; continue;
            }
            if (g_reg[0] != dst + (u32)(reps * 16) || g_reg[6] != idx0 + (u32)(reps * 2)) {
              printf("FAIL mixer-skip block4 ptrs reps=%d mode=%d: r0=%08X r6=%08X\n",
                     reps, mode, g_reg[0], g_reg[6]);
              fails++; continue;
            }
            if (mode == 0) {
              /* WS0 hit: exact cycle debit, IWRAM untouched. */
              u32 want_cyc = (u32)-(reps * (8 + 2 * (int)ws_cyc_nseq[8][0]));
              if (orc_final_cycles != want_cyc) {
                printf("FAIL mixer-skip block4 cyc reps=%d: got=%d want=%d\n",
                       reps, (int)orc_final_cycles, (int)want_cyc);
                fails++;
              }
              for (int w = 0; w < reps * 4; w++) {
                const u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)(w * 4);
                u32 got = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
                if (got != init_w[w]) {
                  printf("FAIL mixer-skip block4 modified IWRAM on hit w=%d\n", w);
                  fails++; break;
                }
              }
            } else {
              /* EWRAM fallback: exact RMW-MLA into IWRAM. */
              for (int r = 0; r < reps; r++) {
                s32 s0 = (s8)ewram[(src + idx0 + (u32)(r * 2)) & 0x3FFFFu];
                s32 s1 = (s8)ewram[(src + idx0 + (u32)(r * 2 + 1)) & 0x3FFFFu];
                u32 want[4];
                want[0] = init_w[r * 4 + 0] + (u32)s0 * 0x00000025u;
                want[1] = init_w[r * 4 + 1] + (u32)s0 * 0xFFFFFFE3u;
                want[2] = init_w[r * 4 + 2] + (u32)s1 * 0x00000025u;
                want[3] = init_w[r * 4 + 3] + (u32)s1 * 0xFFFFFFE3u;
                for (int w = 0; w < 4; w++) {
                  const u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)((r * 4 + w) * 4);
                  u32 got = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
                  if (got != want[w]) {
                    printf("FAIL mixer-skip block4 fallback r=%d w=%d got=%08X want=%08X\n",
                           r, w, got, want[w]);
                    fails++; break;
                  }
                }
              }
            }
          }
        }
      }

      /* 2. sh4g_arm_interp_rmw_mla_skip (count = 1 and 4). */
      {
        static const u32 unit12[12] = {
          0xE1A00421u, /* MOV r0, r1, LSR #8 */
          0xE1B020D4u, /* LDRSB r2, [r0, +r4]! */
          0xE1D030D1u, /* LDRSB r3, [r0, #+1] */
          0xE0433002u, /* SUB r3, r3, r2 */
          0xE0010005u, /* AND r0, r1, r5 */
          0xE0030390u, /* MUL r3, r0, r3 */
          0xE0822443u, /* ADD r2, r2, r3, ASR #8 */
          0xE8960009u, /* LDMIA r6, {r0, r3} */
          0xE0200792u, /* MLA r0, r2, r7, r0 */
          0xE0233892u, /* MLA r3, r2, r8, r3 */
          0xE8A60009u, /* STMIA r6!, {r0, r3} */
          0xE0811009u, /* ADD r1, r1, r9 */
        };
        u32 ops48[48];
        int ccs48[48];
        for (int i = 0; i < 48; i++) { ops48[i] = unit12[i % 12]; ccs48[i] = 0; }
        static const int cnts[2] = { 1, 4 };
        for (int ci = 0; ci < 2; ci++) {
          int cnt = cnts[ci];
          u8 *mp = mbuf;
          sh4_greg_forward_reset();
          if (!sh4g_arm_interp_rmw_mla_skip(&mp, ops48, ccs48, cnt, iw_pc,
                                            NULL, NULL, NULL, NULL)) {
            printf("FAIL mixer-skip interp emit reject cnt=%d\n", cnt);
            fails++; continue;
          }
          if ((size_t)(mp - mbuf) >= 3800u) {
            printf("FAIL mixer-skip interp size=%zu cnt=%d\n", (size_t)(mp - mbuf), cnt);
            fails++; continue;
          }
          for (int mode = 0; mode < 2; mode++) {
            u32 dst = 0x03000500u;
            u32 src = mode == 0 ? 0x08000200u : 0x02000400u;
            u32 p0 = 0x00000460u, step = 0x00000150u, mask = 0xFFu;
            u32 v1 = 0x00000031u, v2 = 0xFFFFFFD9u;
            u32 init_w[8];
            for (int w = 0; w < 8; w++) {
              u32 v = 0x00200000u + (u32)(w * 0x54321);
              u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)(w * 4);
              init_w[w] = v;
              p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
            }
            memset(g_reg, 0, sizeof g_reg);
            g_reg[1] = p0; g_reg[4] = src; g_reg[5] = mask; g_reg[6] = dst;
            g_reg[7] = v1; g_reg[8] = v2; g_reg[9] = step;
            orc_reset_windows();
            orc_add_window(mbuf, sizeof mbuf, ORC_WIN_BYTES);
            orc_add_window(fm_buf, sizeof fm_buf, ORC_WIN_BYTES);
            orc_add_window(memory_map_read, 8192 * 4, ORC_WIN_READ_MAP);
            orc_add_window(giwram, giwram_size, ORC_WIN_BYTES);
            orc_add_window(ewram, sizeof ewram, ORC_WIN_BYTES);
            orc_add_window(rom, sizeof rom, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, ORC_WIN_BYTES);
            orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, ORC_WIN_BYTES);
            orc_add_window(orc_vec_table, sizeof orc_vec_table, ORC_WIN_BYTES);
            orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
            orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
            orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
            orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
            orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
            cases++;
            if (!run_at((u32)(uintptr_t)mbuf, (u32)(uintptr_t)mp)) {
              printf("FAIL mixer-skip interp cnt=%d mode=%d: %s\n", cnt, mode, unmodeled);
              fails++; continue;
            }
            if (g_reg[1] != p0 + (u32)cnt * step || g_reg[6] != dst + (u32)(cnt * 8)) {
              printf("FAIL mixer-skip interp ptrs cnt=%d mode=%d: r1=%08X r6=%08X\n",
                     cnt, mode, g_reg[1], g_reg[6]);
              fails++; continue;
            }
            if (mode == 0) {
              u32 want_cyc = (u32)-(cnt * (2 * (int)ws_cyc_nseq[8][0] + 4));
              if (orc_final_cycles != want_cyc) {
                printf("FAIL mixer-skip interp cyc cnt=%d: got=%d want=%d\n",
                       cnt, (int)orc_final_cycles, (int)want_cyc);
                fails++;
              }
              for (int w = 0; w < cnt * 2; w++) {
                const u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)(w * 4);
                u32 got = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
                if (got != init_w[w]) {
                  printf("FAIL mixer-skip interp modified IWRAM on hit w=%d\n", w);
                  fails++; break;
                }
              }
            } else {
              u32 cur_p = p0;
              for (int u = 0; u < cnt; u++) {
                u32 a = src + (cur_p >> 8);
                s32 s0 = (s8)ewram[a & 0x3FFFFu];
                s32 s1 = (s8)ewram[(a + 1u) & 0x3FFFFu];
                s32 diff = (s32)((u32)(s1 - s0) * (cur_p & mask));
                s32 samp = s0 + (diff >> 8);
                u32 want0 = init_w[u * 2 + 0] + (u32)samp * v1;
                u32 want1 = init_w[u * 2 + 1] + (u32)samp * v2;
                const u8 *p = giwram + 0x8000u + (dst & 0x7FFFu) + (u32)(u * 8);
                u32 got0 = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
                u32 got1 = p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);
                if (got0 != want0 || got1 != want1) {
                  printf("FAIL mixer-skip interp fallback cnt=%d u=%d got=(%08X,%08X) want=(%08X,%08X)\n",
                         cnt, u, got0, got1, want0, want1);
                  fails++; break;
                }
                cur_p += step;
              }
            }
          }
        }
      }
      cgba_iwram_code_end = old_code_end;
    }
#endif

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
        orc_add_window(giwram, giwram_size, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window((const void *)&cgba_vram_dirty, sizeof(cgba_vram_dirty), 0);
        orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
        orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
        orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
        orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;

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
          orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
          orc_add_window(memory_map_read, 8192 * 4, 1);
          orc_add_window(giwram, giwram_size, 0);
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
      /* Plain BG scroll/display-window/blend registers are direct write_ioreg
       * stores in the shared halfword-store routine. Cover both ARM and Thumb
       * sites. */
      {
        struct diod {
          u32 addr; u32 value; int thumb; const char *nm;
        } dios[] = {
          { 0x04000010u, 0x1122u, 0, "arm-bg0hofs" },
          { 0x04000016u, 0x3344u, 1, "thumb-bg1vofs" },
          { 0x04000040u, 0x1357u, 0, "arm-win0h" },
          { 0x04000054u, 0x2468u, 1, "thumb-bldy" },
        };
        for (unsigned di = 0; di < sizeof dios / sizeof *dios; di++) {
          static u8 sbuf[512];
          u8 *sp = sbuf;
          u8 *iop = (u8 *)io_registers + (dios[di].addr & 0x3FEu);

          memset(io_registers, 0, sizeof io_registers);
          memset(g_reg, 0, sizeof g_reg);
          for (int i = 0; i < 16; i++) g_reg[i] = 0x61CD0000u + (u32)i;
          g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
          if (dios[di].thumb) {
            if (!sh4g_thumb_ldst_native(&sp, 0x8208u, pc, 0)) {
              printf("FAIL ioreg-direct %s: thumb emit reject\n", dios[di].nm);
              fails++; continue;
            }
            g_reg[0] = dios[di].value;
            g_reg[1] = dios[di].addr - 16u;       /* STRH r0,[r1,#16] */
          } else {
            if (!sh4g_arm_ldst_native(&sp, 0xE1C230B0u, pc, 0)) {
              printf("FAIL ioreg-direct %s: arm emit reject\n", dios[di].nm);
              fails++; continue;
            }
            g_reg[2] = dios[di].addr;
            g_reg[3] = dios[di].value;
          }
          cases++;

          orc_reset_windows();
          orc_add_window(sbuf, sizeof sbuf, 0);
          orc_add_window(fm_buf, sizeof fm_buf, 0);
          orc_add_window(io_registers, sizeof io_registers, 0);
          orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
          orc_add_window(memory_map_read, 8192 * 4, 1);
          orc_add_window(giwram, giwram_size, 0);
          orc_add_window(ewram, sizeof ewram, 0);
          orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
          orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
          orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
          orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
          orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;

          int ran = run_at((u32)(uintptr_t)sbuf,
                           (u32)(uintptr_t)sbuf + (u32)(sp - sbuf));
          int slow = (!ran && orc_took_slow);
          u16 got = (u16)(iop[0] | (iop[1] << 8));
          if (!ran || slow || got != (u16)dios[di].value) {
            printf("FAIL ioreg-direct %s: ran=%d slow=%d got=%04X want=%04X %s\n",
                   dios[di].nm, ran, slow, got, (u16)dios[di].value,
                   (!ran && !slow) ? unmodeled : "");
            fails++;
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
        orc_add_window(giwram, giwram_size, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window((const void *)&cgba_vram_dirty, sizeof(cgba_vram_dirty), 0);
        orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
        orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
        orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
        orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;

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
      { 0x0000C803u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "ldmia r0!,{r0,r1}" },
      { 0x0000B500u, 0, (1 < CGBA_SH4_FASTMEM_BLOCK_MIN), "push {lr}"            },
      { 0x0000B510u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "push {r4,lr}"         },
      { 0x0000BC01u, 0, (1 < CGBA_SH4_FASTMEM_BLOCK_MIN), "pop  {r0}"            },
      { 0x0000BC02u, 0, (1 < CGBA_SH4_FASTMEM_BLOCK_MIN), "pop  {r1}"            },
      { 0x0000B403u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "push {r0,r1}"         },
      { 0x0000B502u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "push {r1,lr}"         },
      { 0x0000C203u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "stmia r2!,{r0,r1}"    },
      { 0x0000CA03u, 0, (2 < CGBA_SH4_FASTMEM_BLOCK_MIN), "ldmia r2!,{r0,r1}"    },
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
        orc_add_window(giwram, giwram_size, 0);
        orc_add_window(ewram, sizeof ewram, 0);
        orc_add_window(vram, sizeof vram, 0);
        orc_add_window((const void *)&cgba_vram_dirty, sizeof(cgba_vram_dirty), 0);
        orc_add_window((const void *)&cgba_video_io_dirty, sizeof(cgba_video_io_dirty), 0);
        orc_add_window(rom, sizeof rom, 0);
        orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
        orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
        orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
        orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
        orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
        orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
        orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
        orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;

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
          u32 cost = count * (u32)ws_cyc_seq[regn & 0xFu][1];
          if (orc_final_cycles != 0u - cost) {
            printf("FAIL bfm %s @%s: cycles=%u want %u\n",
                   bforms[fi].nm, btgts[ti].nm,
                   orc_final_cycles, 0u - cost);
            fails++;
          }
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

    /* Re-run all IWRAM block forms with cgba_iwram_code_end = 0x0100u so BOTH
     * FMB_LDM_WB and FMB_STM_WB execute the hot ILRAM 8-step BRAF ladder
     * (verifying R3/R1 ladder math + pinned R11/R12 sync + zero R12 clobber). */
    cgba_iwram_code_end = 0x0100u;
    for (unsigned fi = 0; fi < sizeof bforms / sizeof *bforms; fi++) {
      static u8 sbuf[1024] __attribute__((aligned(4)));
      u8 *sp = sbuf;
      u32 op = bforms[fi].op;
      int is_arm = bforms[fi].is_arm;
      u32 rlist, base_reg, is_load, wb, pre, up_, count = 0, lrbit = 0;
      u32 base = 0x03000200u;
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
      u32 A = is_arm ? ((base & ~3u) + (u32)(up_ ? (pre ? 4 : 0) : (pre ? -(int)(count*4) : -(int)(count*4) + 4)))
                     : ((base_reg == 13 && !is_load) ? (base - count*4) : base);
      u32 wbv = is_arm ? (up_ ? base + count*4 : base - count*4)
                       : ((base_reg == 13 && !is_load) ? A : base + count*4);
      int ldm_base_in_list = is_load && !is_arm && base_reg < 8 && (rlist & (1u << base_reg));
      int do_wb = wb && !(is_arm && wb && (rlist & (1u << base_reg)) && !is_load)
                     && !(is_arm && is_load && (rlist & (1u << base_reg)))
                     && !ldm_base_in_list;
      memset(g_reg, 0, sizeof g_reg);
      for (int i = 0; i < 16; i++) g_reg[i] = 0x33000000u + ((u32)i << 8);
      g_reg[SH4_GREG_CPSR] = 0x2000001Fu;
      g_reg[base_reg] = base;
      u8 mem_before[64];
      u32 poff = A & 0x7FFFu;
      const u8 *page = giwram + 0x8000;
      memcpy(mem_before, page + poff, count * 4);
      int emitted = is_arm ? sh4g_arm_block_native(&sp, op, pc, 0)
                           : sh4g_thumb_block_native(&sp, op, pc, 0);
      cases++;
      orc_reset_windows();
      orc_add_window(sbuf, sizeof sbuf, 0);
      orc_add_window(fm_buf, sizeof fm_buf, 0);
      orc_add_window(giwram, giwram_size, 0);
      orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
      orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
      orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
      orc_slow_target = (u32)(uintptr_t)sh4_op2_pc_mem_tramp;
      orc_slow_target2 = (u32)(uintptr_t)sh4_op2_pc_tramp;
      orc_slow_target3 = (u32)(uintptr_t)sh4_fastmem_pc_mem_tramp;
      orc_slow_target4 = (u32)(uintptr_t)sh4_fastmem_compact8_tramp;
      orc_slow_target5 = (u32)(uintptr_t)sh4_fastmem_compact8_load_tramp;
      int ran = emitted && run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sp);
      if (!ran) {
        printf("FAIL bfm-hot %s: %s\n", bforms[fi].nm, unmodeled);
        fails++;
      } else {
        if (orc_final_cycles != 0u - count) {
          printf("FAIL bfm-hot %s: cycles=%u want %u\n",
                 bforms[fi].nm, orc_final_cycles, 0u - count);
          fails++;
        }
        u32 off2 = poff;
        for (int b = 0; b < 16; b++) {
          if (!(rlist & (1u << b))) continue;
          if (is_load) {
            u32 lev = page[off2] | ((u32)page[off2+1] << 8) |
                      ((u32)page[off2+2] << 16) | ((u32)page[off2+3] << 24);
            if (g_reg[b] != lev) {
              printf("FAIL bfm-hot %s: r%d=%08X want %08X\n",
                     bforms[fi].nm, b, g_reg[b], lev);
              fails++; break;
            }
          } else {
            u32 v = (b == (int)base_reg) ? base : 0x33000000u | ((u32)b << 8);
            u8 want[4] = { (u8)v, (u8)(v>>8), (u8)(v>>16), (u8)(v>>24) };
            if (memcmp(page + off2, want, 4) != 0) {
              printf("FAIL bfm-hot %s: mem mismatch at r%d\n", bforms[fi].nm, b);
              fails++; break;
            }
          }
          off2 += 4;
        }
        /* Verify non-transferred pinned r0/r1 were never clobbered. */
        for (int p = 0; p < 2; p++) {
          if (!(rlist & (1u << p)) && (u32)p != base_reg &&
              g_reg[p] != (0x33000000u + ((u32)p << 8))) {
            printf("FAIL bfm-hot %s: clobbered pinned r%d=%08X\n",
                   bforms[fi].nm, p, g_reg[p]);
            fails++;
          }
        }
        u32 want_base = do_wb ? wbv : base;
        if (is_load && (rlist & (1u << base_reg))) want_base = g_reg[base_reg];
        if (g_reg[base_reg] != want_base) {
          printf("FAIL bfm-hot %s: base=%08X want %08X\n",
                 bforms[fi].nm, g_reg[base_reg], want_base);
          fails++;
        }
      }
      if (!is_load) memcpy((void *)(page + poff), mem_before, count * 4);
    }

    /* Verify cgba_sh4_pop1_bx_routine pops target into reg[rX] AND syncs
     * pinned R11 (r0) and R12 (r1) before entering cgba_sh4_thumb_bx_routine. */
    for (unsigned rx = 0; rx < 4; rx++) {
      static u8 sbuf[128] __attribute__((aligned(4)));
      u8 *sp = sbuf;
      u32 target_val = 0x08001235u + rx * 4u;
      u32 sp_addr = 0x03000240u;
      u8 *slot = giwram + 0x8000u + (sp_addr & 0x7FFFu);
      slot[0] = (u8)target_val; slot[1] = (u8)(target_val >> 8);
      slot[2] = (u8)(target_val >> 16); slot[3] = (u8)(target_val >> 24);
      memset(g_reg, 0, sizeof g_reg);
      g_reg[0] = 0x11110000u; g_reg[1] = 0x22220001u;
      g_reg[2] = 0x33330002u; g_reg[3] = 0x44440003u;
      g_reg[13] = sp_addr;
      g_reg[SH4_GREG_CPSR] = 0x2000003Fu;
      sh4g_litseg_begin();
      sh4g_const(&sp, (u32)(uintptr_t)cgba_sh4_pop1_bx_routine, SH4_REG_RET);
      if (((uintptr_t)sp + 4u) & 3u) sh4g_u16(&sp, 0x0009u);
      { sh4_codegen cg = sh4g_open(&sp);
        sh4_emit_jsr(&cg, SH4_REG_RET);
        sh4_emit_mov_imm(&cg, (int)(rx * 4u), SH4_REG_ARG1);
        sh4_emit_u32_be(&cg, pc);
        sh4g_close(&sp, &cg); }
      sh4g_litseg_end(&sp);
      orc_reset_windows();
      orc_add_window(sbuf, sizeof sbuf, 0);
      orc_add_window(fm_buf, sizeof fm_buf, 0);
      orc_add_window(giwram, giwram_size, 0);
      orc_add_window(ws_cyc_seq, sizeof ws_cyc_seq, 0);
      orc_add_window(ws_cyc_nseq, sizeof ws_cyc_nseq, 0);
      orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
      /* Run up to cgba_sh4_thumb_bx_routine entry (verifies POP + R11/R12 sync). */
      int ran = run_at((u32)(uintptr_t)sbuf,
                       (u32)(uintptr_t)cgba_sh4_thumb_bx_routine);
      cases++;
      if (!ran || g_reg[rx] != target_val || g_reg[13] != sp_addr + 4u ||
          orc_final_cycles != (u32)-1) {
        printf("FAIL pop1_bx r%u: ran=%d reg=%08X want %08X sp=%08X cyc=%d (%s)\n",
               rx, ran, g_reg[rx], target_val, g_reg[13], (int)orc_final_cycles, unmodeled);
        fails++;
      }
      if (rx != 0 && g_reg[0] != 0x11110000u) {
        printf("FAIL pop1_bx r%u clobbered r0=%08X\n", rx, g_reg[0]); fails++;
      }
      if (rx != 1 && g_reg[1] != 0x22220001u) {
        printf("FAIL pop1_bx r%u clobbered r1=%08X\n", rx, g_reg[1]); fails++;
      }
    }

    /* Verify sh4g_thumb_base_run_native (mixed LDR/STR/LDRH/STRH/LDRB/STRB +
     * dead-load elimination + pinned r0/r1). */
    {
      static u8 sbuf[512] __attribute__((aligned(4)));
      u8 *sp = sbuf;
      const u32 ops[6] = {
        0x6810u, /* LDR  r0, [r2, #0]  (dead: overwritten by ops[2]) */
        0x8051u, /* STRH r1, [r2, #2] */
        0x6850u, /* LDR  r0, [r2, #4] */
        0x70D0u, /* STRB r0, [r2, #3] */
        0x8853u, /* LDRH r3, [r2, #2] */
        0x78D4u, /* LDRB r4, [r2, #3] */
      };
      const int ccs[6] = { 1, 2, 3, 4, 5, 6 };
      u32 base = 0x03000280u;
      u8 *p = giwram + 0x8000u + (base & 0x7FFFu);
      p[0]=0xAA; p[1]=0xBB; p[2]=0xCC; p[3]=0xDD;
      p[4]=0x78; p[5]=0x56; p[6]=0x34; p[7]=0x12;
      memset(g_reg, 0, sizeof g_reg);
      g_reg[0] = 0xDEADBEEFu;
      g_reg[1] = 0x0000BEEFu;
      g_reg[2] = base;
      sh4g_litseg_begin();
      int emitted = sh4g_thumb_base_run_native(&sp, ops, ccs, 6, pc);
      sh4g_litseg_end(&sp);
      orc_reset_windows();
      orc_add_window(sbuf, sizeof sbuf, 0);
      orc_add_window(fm_buf, sizeof fm_buf, 0);
      orc_add_window(giwram, giwram_size, 0);
      orc_add_window(orc_vec_table, sizeof orc_vec_table, 0);
      int ran = emitted && run_at((u32)(uintptr_t)sbuf, (u32)(uintptr_t)sp);
      cases++;
      /* After ops: p[2]=0xEF, p[3]=0x78 (low byte of 0x12345678), so LDRH r3
       * at #2 reads 0x78EF, LDRB r4 at #3 reads 0x78, r0 = 0x12345678. */
      if (!ran || g_reg[0] != 0x12345678u || g_reg[1] != 0x0000BEEFu ||
          g_reg[3] != 0x000078EFu || g_reg[4] != 0x00000078u ||
          orc_final_cycles != (u32)-6) {
        printf("FAIL base-run: ran=%d r0=%08X r1=%08X r3=%08X r4=%08X cyc=%d (%s)\n",
               ran, g_reg[0], g_reg[1], g_reg[3], g_reg[4], (int)orc_final_cycles, unmodeled);
        fails++;
      }
    }
    cgba_iwram_code_end = 0x8000u;
  }
  }

  /* ---- 4-slot R4..R7 BBRA straight-line ARM ALU/MUL/MLA oracle (2000 runs) --- */
  {
    static const u32 arm_alus[9] = { 0u, 1u, 2u, 3u, 4u, 0xCu, 0xDu, 0xEu, 0xFu };
    static const u32 dir_cases[][4] = {
      /* Minish Cap diff-Rn ADD regression (r3 reused, distinct r2/r4 bases) */
      { 0xE0823003u, 0xE0845003u, 0u, 0u },
      /* same-Rn LSR #7 packing (old Peephole 5) */
      { 0xE00C13A1u, 0xE00C43A4u, 0xE00C73A7u, 0u },
      /* same-Rd accumulator ORR with shifts (old Peephole 8) */
      { 0xE1844405u, 0xE1844806u, 0xE1844C07u, 0u },
      /* aliasing SUB r4, r5, r4 + RSB r5, r4, r5 + MLA r4, r6, r7, r4 + BIC */
      { 0xE0454004u, 0xE0645005u, 0xE0244796u, 0xE1C44005u },
    };
    static const int dir_lens[4] = { 2, 3, 3, 4 };
    u32 rng = 0xA1B2C3D4u;
    for (int trial = 0; trial < 2000 + 4; trial++) {
      u32 rops[16];
      int rlen;
      if (trial < 4) {
        rlen = dir_lens[trial];
        for (int k = 0; k < rlen; k++) rops[k] = dir_cases[trial][k];
      } else {
        rlen = 2 + (int)((rng = rng * 1664525u + 1013904223u) % 15u);
        for (int k = 0; k < rlen; k++) {
          u32 r = (rng = rng * 1664525u + 1013904223u);
          unsigned kind = r & 7u;
          unsigned rd = (r >> 3) % 10u; /* bias to r0..r9 for heavy reuse */
          unsigned rn = (r >> 7) % 10u;
          unsigned rm = (r >> 11) % 10u;
          unsigned rs = (r >> 15) % 10u;
          if (kind == 0u) {             /* MUL / MLA */
            u32 is_mla = (r >> 19) & 1u;
            rops[k] = 0xE0000090u | (is_mla << 21) | (rd << 16) |
                      (rn << 12) | (rs << 8) | rm;
          } else if (kind <= 3u) {      /* DP imm */
            u32 alu = arm_alus[(r >> 19) % 9u];
            u32 imm8 = (r >> 23) & 0xFFu;
            u32 rot = ((r >> 15) & 0xFu);
            if ((r & 0x10000000u) && alu == 0u) { imm8 = 0xFFu; rot = 0u; }
            if ((r & 0x20000000u) && alu == 4u) { imm8 = (r >> 23) & 0x7Fu; rot = 0u; }
            rops[k] = 0xE2000000u | (alu << 21) | (rn << 16) | (rd << 12) |
                      (rot << 8) | imm8;
          } else {                      /* DP reg (imm shift) */
            u32 alu = arm_alus[(r >> 19) % 9u];
            u32 st = (r >> 23) % 3u;
            u32 si = (r >> 25) & 31u;
            if (st != 0u && si == 0u) si = 1u + ((r >> 15) % 31u);
            rops[k] = 0xE0000000u | (alu << 21) | (rn << 16) | (rd << 12) |
                      (si << 7) | (st << 5) | rm;
          }
        }
      }
      static u8 abuf[2048] __attribute__((aligned(4)));
      u8 *sp = abuf;
      sh4g_litseg_begin();
      int emitted = sh4g_arm_alu_run_native(&sp, rops, rlen);
      sh4g_litseg_end(&sp);
      if (!emitted) continue;
      cases++;
      u32 want_reg[16];
      memset(g_reg, 0, sizeof g_reg);
      for (int i = 0; i < 16; i++) {
        u32 v = (rng = rng * 1664525u + 1013904223u);
        g_reg[i] = want_reg[i] = v;
      }
      for (int k = 0; k < rlen; k++) {
        u32 op = rops[k];
        if ((op & 0x0FC000F0u) == 0x00000090u) {
          unsigned rd = (op >> 16) & 0xFu, rn = (op >> 12) & 0xFu;
          unsigned rs = (op >> 8) & 0xFu, rm = op & 0xFu;
          u32 p = want_reg[rm] * want_reg[rs];
          want_reg[rd] = ((op >> 21) & 1u) ? (p + want_reg[rn]) : p;
        } else {
          u32 alu = (op >> 21) & 0xFu;
          unsigned rn = (op >> 16) & 0xFu, rd = (op >> 12) & 0xFu;
          u32 op2;
          if ((op >> 25) & 1u) {
            u32 rot = ((op >> 8) & 0xFu) * 2u, v = op & 0xFFu;
            op2 = rot ? ((v >> rot) | (v << (32u - rot))) : v;
          } else {
            unsigned rm = op & 0xFu, st = (op >> 5) & 3u, si = (op >> 7) & 31u;
            op2 = want_reg[rm];
            if (si != 0u) {
              if (st == 0u)      op2 <<= si;
              else if (st == 1u) op2 >>= si;
              else               op2 = (u32)((s32)op2 >> si);
            }
          }
          switch (alu) {
          case 0x0u: want_reg[rd] = want_reg[rn] & op2; break;
          case 0x1u: want_reg[rd] = want_reg[rn] ^ op2; break;
          case 0x2u: want_reg[rd] = want_reg[rn] - op2; break;
          case 0x3u: want_reg[rd] = op2 - want_reg[rn]; break;
          case 0x4u: want_reg[rd] = want_reg[rn] + op2; break;
          case 0xCu: want_reg[rd] = want_reg[rn] | op2; break;
          case 0xDu: want_reg[rd] = op2; break;
          case 0xEu: want_reg[rd] = want_reg[rn] & ~op2; break;
          default:   want_reg[rd] = ~op2; break;
          }
        }
      }
      orc_reset_windows();
      orc_add_window(abuf, sizeof abuf, 0);
      if (!run_at((u32)(uintptr_t)abuf, (u32)(uintptr_t)sp)) {
        printf("FAIL arm-bbra trial=%d: interpreter: %s\n", trial, unmodeled);
        fails++;
        break;
      }
      for (int i = 0; i < 15; i++) {
        if (g_reg[i] != want_reg[i]) {
          printf("FAIL arm-bbra trial=%d rlen=%d: r%d=%08X want %08X\n",
                 trial, rlen, i, g_reg[i], want_reg[i]);
          fails++;
          break;
        }
      }
      if (fails) break;
    }
  }

  /* ---- 4-slot R4..R7 BBRA straight-line Thumb ALU run oracle (2000 runs) --- */
  {
    u32 rng = 0xC0FFEE42u;
    for (int trial = 0; trial < 2000; trial++) {
      u16 rops[16];
      int rlen = 2 + (int)((rng = rng * 1664525u + 1013904223u) % 15u);
      for (int k = 0; k < rlen; k++) {
        rng = rng * 1664525u + 1013904223u;
        unsigned kind = rng % 10u;
        unsigned rd = (rng >> 4) & 7u, rn = (rng >> 7) & 7u, rm = (rng >> 10) & 7u;
        unsigned imm8 = (rng >> 16) & 0xFFu;
        if (kind == 0) {                      /* LSL/LSR/ASR #imm5 */
          unsigned sub = (rng >> 13) % 3u;
          unsigned imm5 = (rng >> 15) & 31u;
          rops[k] = (u16)((sub << 11) | (imm5 << 6) | (rm << 3) | rd);
        } else if (kind == 1 && k + 1 < rlen) { /* LSL+LSR/ASR extend pair */
          unsigned is_b = (rng >> 13) & 1u;
          unsigned is_s = (rng >> 14) & 1u;
          rops[k] = (u16)((is_b ? 0x0600u : 0x0400u) | (rm << 3) | rd);
          rops[k + 1] = (u16)((is_s ? (is_b ? 0x1600u : 0x1400u)
                                    : (is_b ? 0x0E00u : 0x0C00u)) | (rd << 3) | rd);
          k++;
        } else if (kind == 2) {               /* ADD/SUB reg */
          unsigned is_sub = (rng >> 13) & 1u;
          rops[k] = (u16)(0x1800u | (is_sub << 9) | (rm << 6) | (rn << 3) | rd);
        } else if (kind == 3) {               /* ADD/SUB imm3 */
          unsigned is_sub = (rng >> 13) & 1u;
          unsigned imm3 = (rng >> 14) & 7u;
          rops[k] = (u16)(0x1C00u | (is_sub << 9) | (imm3 << 6) | (rn << 3) | rd);
        } else if (kind == 4) {               /* MOV imm8 */
          rops[k] = (u16)(0x2000u | (rd << 8) | imm8);
        } else if (kind == 5) {               /* ADD/SUB imm8 */
          unsigned is_sub = (rng >> 13) & 1u;
          if ((rng & 0x300000u) == 0u) imm8 = 255u;
          else if ((rng & 0x300000u) == 0x100000u) imm8 = 128u;
          rops[k] = (u16)(0x3000u | (is_sub << 11) | (rd << 8) | imm8);
        } else if (kind == 6) {               /* ALU reg-reg */
          static const u8 alus[7] = { 0x0, 0x1, 0x9, 0xC, 0xD, 0xE, 0xF };
          unsigned alu = alus[(rng >> 13) % 7u];
          rops[k] = (u16)(0x4000u | (alu << 6) | (rm << 3) | rd);
        } else if (kind == 7) {               /* ADD/MOV Hd, Hm */
          unsigned hd = (rng >> 4) % 15u, hs = (rng >> 8) % 15u;
          unsigned is_mov = (rng >> 13) & 1u;
          rops[k] = (u16)((is_mov ? 0x4600u : 0x4400u) |
                          ((hd & 8u) << 4) | (hs << 3) | (hd & 7u));
        } else {                              /* ADD Rd, SP, #imm8*4 */
          rops[k] = (u16)(0xA800u | (rd << 8) | imm8);
        }
      }
      static u8 bbuf[4096];
      u8 *bp = bbuf;
      u32 wmask = 0u;
      if (!sh4g_thumb_alu_run_native(&bp, rops, rlen, &wmask))
        continue;
      cases++;
      u32 want[16];
      memset(g_reg, 0, sizeof g_reg);
      for (int i = 0; i < 16; i++) {
        rng = rng * 1664525u + 1013904223u;
        g_reg[i] = want[i] = rng;
      }
      g_reg[SH4_GREG_CPSR] = 0xA000003Fu;
      for (int k = 0; k < rlen; k++) {
        u32 op = rops[k];
        unsigned hi = (op >> 8) & 0xFFu;
        if (hi <= 0x17u) {
          unsigned rd = op & 7u, rm = (op >> 3) & 7u;
          unsigned imm5 = (op >> 6) & 31u, sub = (op >> 11) & 3u;
          if (sub == 0u)      want[rd] = want[rm] << imm5;
          else if (sub == 1u) want[rd] = imm5 ? (want[rm] >> imm5) : 0u;
          else                want[rd] = imm5 ? (u32)((s32)want[rm] >> imm5)
                                              : (u32)((s32)want[rm] >> 31);
        } else if (hi <= 0x1Fu) {
          unsigned rd = op & 7u, rn = (op >> 3) & 7u, arg = (op >> 6) & 7u;
          unsigned is_sub = (op >> 9) & 1u, is_imm = (op >> 10) & 1u;
          u32 rhs = is_imm ? arg : want[arg];
          want[rd] = is_sub ? (want[rn] - rhs) : (want[rn] + rhs);
        } else if (hi <= 0x27u) {
          want[(op >> 8) & 7u] = op & 0xFFu;
        } else if (hi >= 0x30u && hi <= 0x3Fu) {
          unsigned rd = (op >> 8) & 7u, is_sub = (op >> 11) & 1u;
          want[rd] = is_sub ? (want[rd] - (op & 0xFFu)) : (want[rd] + (op & 0xFFu));
        } else if (hi >= 0x40u && hi <= 0x43u) {
          unsigned alu = (op >> 6) & 0xFu, rs = (op >> 3) & 7u, rd = op & 7u;
          switch (alu) {
          case 0x0u: want[rd] &= want[rs]; break;
          case 0x1u: want[rd] ^= want[rs]; break;
          case 0x9u: want[rd] = 0u - want[rs]; break;
          case 0xCu: want[rd] |= want[rs]; break;
          case 0xDu: want[rd] *= want[rs]; break;
          case 0xEu: want[rd] &= ~want[rs]; break;
          case 0xFu: want[rd] = ~want[rs]; break;
          }
        } else if (hi == 0x44u || hi == 0x46u) {
          unsigned rd = (op & 7u) | ((op >> 4) & 8u), rs = (op >> 3) & 0xFu;
          want[rd] = (hi == 0x46u) ? want[rs] : (want[rd] + want[rs]);
        } else {
          want[(op >> 8) & 7u] = want[13] + ((op & 0xFFu) << 2);
        }
      }
      if (!run_sh4x(bbuf, (size_t)(bp - bbuf)) ||
          g_reg[SH4_GREG_CPSR] != 0xA000003Fu) {
        printf("FAIL bbra trial=%d: interpreter=%s\n", trial, unmodeled);
        fails++;
        continue;
      }
      for (int i = 0; i < 15; i++) {
        if (g_reg[i] != want[i]) {
          printf("FAIL bbra trial=%d len=%d r%d=%08X want %08X\n",
                 trial, rlen, i, g_reg[i], want[i]);
          fails++;
          break;
        }
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
