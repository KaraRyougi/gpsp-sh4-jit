#ifndef CGBA_SH4_FASTMEM_H
#define CGBA_SH4_FASTMEM_H

/*
 * Out-of-line native memory fast paths — the translation-cache DENSITY fix.
 *
 * Metroid's in-world working set overflowed the former 896KB ROM cache
 * when every native load/store site inlines its ~90-130 byte fast path
 * (guards + page probe + transfer + SMC tags + wait-state charge): the JIT
 * wholesale-flushes ~3x per FRAME and spends the frame retranslating (~3fps,
 * measured: FL R15 / TX T7025 / IC 12.5MB per 5-frame window).
 *
 * Fix: emit each fast-path SHAPE exactly once at init into a small resident
 * buffer, and call it from a fixed ~36-byte site — the same literal-tuple
 * mechanism as the compact C-helper trampolines, with one extra twist: the
 * tuple is laid out so the routine's guard-failure path can jump STRAIGHT
 * into the existing sh4_op2_pc_mem_tramp, which then reads the helper fn /
 * opcode / pc / cycles from the very same tuple. One site shape serves both
 * the native fast path and the C fallback.
 *
 *   site:  [addr calc, site-specific]           ; R1 = address, R6 = wb value
 *          [pad NOP so the literals align]
 *          MOV.L  L, r0
 *          JSR    @r0                            ; PR -> the BRA below
 *          NOP
 *          BRA    2f
 *          NOP
 *   L:     .long  ROUTINE                        ; PR+4
 *          .long  helper_fn                      ; PR+8   (tramp: fn)
 *          .long  opcode                         ; PR+12  (tramp: opcode)
 *          .long  pc                             ; PR+16  (tramp: pc)
 *          .long  cycle_count                    ; PR+20  (tramp: cycles)
 *          .long  params                         ; PR+24  (routine only)
 *   2:
 *
 * Routine contract:
 *   in:  R1 = guest transfer address, R6 = writeback value (wb variants),
 *        PR = site return (also the tuple cursor), R8 = cached CPSR (do not
 *        touch), R13/R14 cycle counter / reg base as everywhere.
 *   out: fast path performs transfer + reg[rd] + writeback + cycle charge,
 *        returns with RTS. Guard failure jumps to sh4_op2_pc_mem_tramp with
 *        PR intact — the C helper re-executes the instruction from original
 *        state (writeback is NOT committed before any guard).
 *   params: bits 0-7 = rd byte-offset in reg[] (rd*4);
 *           bits 8-15 = writeback rn byte-offset (wb variants only).
 *
 * The generator reuses the exact inline emitter recipes (guards, byte-order
 * swaps, SMC tag scan, charge) — this file reorganizes WHERE the code lives,
 * not what it does. Routines live in a normal .bss buffer (NOT the high
 * arena) and survive translation-cache flushes.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

/* Block lists with at least this many registers use the shared runtime-rlist
 * routine (40-byte site); smaller lists keep the faster unrolled inline path.
 * Tunable for capacity-vs-speed A/B on hardware. */
#ifndef CGBA_SH4_FASTMEM_BLOCK_MIN
#define CGBA_SH4_FASTMEM_BLOCK_MIN 3
#endif

/* Routine variants: kind x direction, plus ARM writeback twins. */
enum {
  CGBA_FM_LOAD_W = 0, CGBA_FM_LOAD_B, CGBA_FM_LOAD_UH, CGBA_FM_LOAD_SH,
  CGBA_FM_LOAD_SB, CGBA_FM_STORE_W, CGBA_FM_STORE_UH, CGBA_FM_STORE_B,
  CGBA_FM_BASE_COUNT,
  CGBA_FM_WB = CGBA_FM_BASE_COUNT,   /* +CGBA_FM_WB = writeback variant */
  CGBA_FM_COUNT = CGBA_FM_BASE_COUNT * 2,
  /* Block transfers (LDM/STM/PUSH/POP, runtime register-list loop). The
   * +2 variants commit the precomputed writeback base from R6. */
  CGBA_FMB_LDM = CGBA_FM_COUNT,
  CGBA_FMB_STM,
  CGBA_FMB_LDM_WB,
  CGBA_FMB_STM_WB,
  CGBA_FM_TOTAL
};

extern u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];

extern u8 *memory_map_read[];
extern u32 gamepak_size;
extern const u8 * const *cgba_gamepak_fragment_blocks;
extern u16 io_registers[512];          /* eswap16'd (LE-layout) halfwords */
extern u16 palette_ram[512];
extern u16 palette_ram_converted[512];
#ifdef CGBA_PALETTE_DIRTY_ACTIVE
extern volatile u32 palette_ram_dirty;
#endif
#ifdef CGBA_VRAM_DIRTY_ACTIVE
extern volatile u32 cgba_vram_dirty;
#endif
extern u8 iwram[];
extern u8 vram[1024 * 96];
void sh4_op2_pc_mem_tramp(void);
#ifndef CGBA_SH4_COMPACT_FASTMEM_TUPLE
#define CGBA_SH4_COMPACT_FASTMEM_TUPLE 0
#endif
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
void sh4_fastmem_pc_mem_tramp(void);
#define CGBA_SH4_FASTMEM_SLOW_TRAMP sh4_fastmem_pc_mem_tramp
#else
#define CGBA_SH4_FASTMEM_SLOW_TRAMP sh4_op2_pc_mem_tramp
#endif

static inline void sh4g_fastmem_convert_palette(u8 **tp, unsigned src,
                                                unsigned dst, unsigned tmp1,
                                                unsigned tmp2)
{
#ifdef USE_XBGR1555_FORMAT
  sh4g_const(tp, 0x7FFFu, tmp1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, src, dst);
    sh4_emit_and(&cg, tmp1, dst);
    sh4g_close(tp, &cg); }
#else
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, src, dst);
    sh4_emit_mov_imm(&cg, 0x1F, tmp1);
    sh4_emit_and(&cg, tmp1, dst);
    sh4_emit_mov_imm(&cg, 11, tmp1);
    sh4_emit_shld(&cg, tmp1, dst);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03E0u, tmp2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, src, tmp1);
    sh4_emit_and(&cg, tmp2, tmp1);
    sh4_emit_shll(&cg, tmp1);
    sh4_emit_or(&cg, tmp1, dst);
    sh4_emit_mov_reg(&cg, src, tmp1);
    sh4_emit_mov_imm(&cg, -10, tmp2);
    sh4_emit_shld(&cg, tmp2, tmp1);
    sh4_emit_mov_imm(&cg, 0x1F, tmp2);
    sh4_emit_and(&cg, tmp2, tmp1);
    sh4_emit_or(&cg, tmp1, dst);
    sh4g_close(tp, &cg); }
#endif
}

static inline void sh4g_fastmem_io16_direct_store(u8 **tp, u32 guest_address,
                                                  u32 io_offset,
                                                  u8 *store_tail)
{
  u8 *miss;

  sh4g_const(tp, guest_address, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  miss = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + io_offset),
             SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail); }

  sh4g_patch_cond(miss, *tp);
}

static inline void sh4g_fastmem_io16_dispstat_store(u8 **tp, u8 *store_tail)
{
  u8 *miss;

  sh4g_const(tp, 0x04000004u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  miss = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
    sh4_emit_mov_imm(&cg, -8, SH4_REG_ARG0);
    sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_T1);   /* value & ~0x07 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x004), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x07);                   /* keep LCD status bits */
    sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail); }

  sh4g_patch_cond(miss, *tp);
}

static inline void sh4g_fastmem_io16_dispcnt_store(u8 **tp, u8 *store_tail)
{
  u8 *miss, *same_mode;

  sh4g_const(tp, 0x04000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  miss = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x07);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x000), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x07);
    sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  same_mode = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 25 * 4, SH4_REG_RET);       /* reg[OAM_UPDATED] */
    sh4_emit_mov_imm(&cg, 1, SH4_REG_ARG0);
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG0, SH4_REG_BASE);
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(same_mode, *tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail); }

  sh4g_patch_cond(miss, *tp);
}

static inline void sh4g_fastmem_io16_pair_direct_store(u8 **tp,
                                                       u32 guest_address,
                                                       u32 io_offset,
                                                       u8 *store_tail)
{
  u8 *miss_odd, *miss_hi;

  sh4g_const(tp, guest_address, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T2);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_T2);       /* delta = addr - base */
    sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 1);                        /* T = even */
    sh4g_close(tp, &cg); }
  miss_odd = sh4g_emit_bf_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 2, SH4_REG_T1);
    sh4_emit_cmphi(&cg, SH4_REG_T1, SH4_REG_T2);     /* T = delta > 2 */
    sh4g_close(tp, &cg); }
  miss_hi = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG3);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + io_offset),
             SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail); }

  sh4g_patch_cond(miss_odd, *tp);
  sh4g_patch_cond(miss_hi, *tp);
}

static inline int cgba_fm_is_load(int fm)
{ return (fm % CGBA_FM_BASE_COUNT) <= CGBA_FM_LOAD_SB; }

static inline int cgba_fm_align_mask(int fm)
{
  switch (fm % CGBA_FM_BASE_COUNT) {
  case CGBA_FM_LOAD_W: case CGBA_FM_STORE_W:   return 3;
  case CGBA_FM_LOAD_UH: case CGBA_FM_LOAD_SH:
  case CGBA_FM_STORE_UH:                       return 1;
  default:                                     return 0;
  }
}

/* Emit RTS with a small immediate cycle debit (`add #-cycles, R13`) packed
 * directly into the delay slot. */
static inline void sh4g_fastmem_rts_debit(u8 **tp, int cycles)
{
  sh4g_u16(tp, 0x000B);                              /* RTS */
  if (cycles >= -128 && cycles <= 127 && cycles != 0)
    sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-cycles) & 0xFF)));
  else
    sh4g_u16(tp, 0x0009);                            /* NOP */
}

/* Emit writeback commit (`reg[rn] = R6`) for wb variants. */
static inline void sh4g_fastmem_emit_wb(u8 **tp, int wb)
{
  if (wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);                     /* R0 = rn byte-offset */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
    sh4g_close(tp, &cg);
  }
}

/* Emit load/byte-swap/store-to-reg[rd] from @(R0, R3). */
static inline void sh4g_fastmem_emit_load_xfer(u8 **tp, int kind)
{
  sh4_codegen cg = sh4g_open(tp);
  switch (kind) {
  case CGBA_FM_LOAD_W:
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
    sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
    break;
  case CGBA_FM_LOAD_B:
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    break;
  case CGBA_FM_LOAD_UH:
    sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
    break;
  case CGBA_FM_LOAD_SH:
    sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
    break;
  default: /* LOAD_SB: mov.b sign-extends */
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    break;
  }
  sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
  sh4_emit_and_imm(&cg, 0xFF);                       /* R0 = rd byte-offset */
  sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_BASE);
  sh4g_close(tp, &cg);
}

/* Emit reg[rd] load, byte-swap, and store to @(R0, R3); preserves R0 via R7. */
static inline void sh4g_fastmem_emit_store_xfer(u8 **tp, int kind)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG3);  /* R7 = offset */
  sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
  sh4_emit_and_imm(&cg, 0xFF);
  sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
  sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);  /* R0 = offset */
  switch (kind) {
  case CGBA_FM_STORE_W:
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
    sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    break;
  case CGBA_FM_STORE_UH:
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    break;
  default:
    sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    break;
  }
  sh4g_close(tp, &cg);
}

/* ---- the shared routine body ------------------------------------------- */
/* Emits one variant at *tp; returns its entry point.
 *
 * Computes `region = addr >> 24` ONCE into R0 and R12, dispatches IWRAM (3)
 * and EWRAM (2) directly for BOTH loads and stores via the R9 vector table
 * (avoiding memory_map_read[] D-cache traffic and runtime cycle-table
 * branches), handles Palette/VRAM/IO16 stores directly, and falls through to
 * the mapped/fragmented-NOR load path for ROM/video/IO reads. */
static inline u8 *sh4g_fastmem_emit_routine(u8 **tp, int fm)
{
  u8 *entry = *tp;
  int is_load = cgba_fm_is_load(fm);
  int kind = fm % CGBA_FM_BASE_COUNT;
  int align_mask = cgba_fm_align_mask(fm);
  int wb = fm >= CGBA_FM_WB;
  u8 *guards[12]; int ng = 0;
  u8 *io_check = NULL, *store_tail = NULL;
  u8 *fragment_entry = NULL, *fragment_join = NULL;
  u8 *fragment_far = NULL;
  int i;

  /* params -> R5 early (untouched throughout), region = addr >> 24 -> R0, R12. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_ARG1);
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_ARG1, 20 >> 2);
    sh4_emit_shlr16(&cg, SH4_REG_ARG1);             /* packed high half */
#else
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_ARG1, 24 >> 2);
#endif
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);               /* R0 = addr >> 24 */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_FM_REGION);
    sh4g_close(tp, &cg); }

  /* ---- 1. Direct IWRAM (region 3) fast path: BOTH loads and stores ---- */
  {
    u8 *not_iwram;
    u8 *iwram_fail[2];
    int nfail = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 3);                    /* T = IWRAM */
      sh4g_close(tp, &cg); }
    not_iwram = sh4g_emit_bf_placeholder(tp);

    if (align_mask) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, align_mask);             /* T = guest aligned */
      sh4g_close(tp, &cg);
      iwram_fail[nfail++] = sh4g_emit_bf_placeholder(tp);
    }

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
      sh4g_close(tp, &cg); }
    sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);

    if (is_load) {
      sh4g_fastmem_emit_load_xfer(tp, kind);
    } else {
      /* IWRAM SMC tags live at iwram_data - 0x8000 (0x8000 = 128 << 8). */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG3);
        sh4_emit_mov_imm(&cg, -128, SH4_REG_ARG0);
        sh4_emit_shll8(&cg, SH4_REG_ARG0);           /* R4 = -0x8000 */
        sh4_emit_add_reg(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
        switch (kind) {
        case CGBA_FM_STORE_W:
          sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        case CGBA_FM_STORE_UH:
          sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        default:
          sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        }
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = tag clear */
        sh4g_close(tp, &cg); }
      iwram_fail[nfail++] = sh4g_emit_bf_placeholder(tp);
      sh4g_fastmem_emit_store_xfer(tp, kind);
    }
    sh4g_fastmem_emit_wb(tp, wb);
    sh4g_fastmem_rts_debit(tp, 1);                   /* IWRAM nseq cost = 1 */

    if (nfail > 0) {
      /* On align/SMC failure, jump out to the slow tramp via a local bridge
       * (keeps all disp8 BF branches short while avoiding fallthrough). */
      for (i = 0; i < nfail; i++)
        sh4g_patch_cond(iwram_fail[i], *tp);
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
    }
    sh4g_patch_cond(not_iwram, *tp);
    /* R0 still equals region (R12) on the not_iwram path! */
  }

  /* ---- 2. Direct EWRAM (region 2) fast path: BOTH loads and stores ---- */
  {
    u8 *not_ewram;
    u8 *ewram_fail[2];
    int nfail = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 2);                    /* T = EWRAM */
      sh4g_close(tp, &cg); }
    not_ewram = sh4g_emit_bf_placeholder(tp);

    if (align_mask) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, align_mask);
      sh4g_close(tp, &cg);
      ewram_fail[nfail++] = sh4g_emit_bf_placeholder(tp);
    }

    /* Synthesize 0x3FFFF without a literal load: (4 << 16) - 1. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 4, SH4_REG_ARG0);
      sh4_emit_shll16(&cg, SH4_REG_ARG0);            /* R4 = 0x40000 */
      sh4_emit_add_imm(&cg, -1, SH4_REG_ARG0);       /* R4 = 0x3FFFF */
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* EWRAM mirror offset */
      sh4g_close(tp, &cg); }
    sh4g_vec_load(tp, SH4G_VEC_ewram_data, SH4_REG_T2);

    if (is_load) {
      sh4g_fastmem_emit_load_xfer(tp, kind);
    } else {
      /* EWRAM SMC tags live at ewram + 0x40000; R4 is already 0x3FFFF. */
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 1, SH4_REG_ARG0);      /* R4 = 0x40000 */
        sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG3);
        sh4_emit_add_reg(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
        switch (kind) {
        case CGBA_FM_STORE_W:
          sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        case CGBA_FM_STORE_UH:
          sh4_emit_mov_w_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        default:
          sh4_emit_mov_b_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG0); break;
        }
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = tag clear */
        sh4g_close(tp, &cg); }
      ewram_fail[nfail++] = sh4g_emit_bf_placeholder(tp);
      sh4g_fastmem_emit_store_xfer(tp, kind);
    }
    sh4g_fastmem_emit_wb(tp, wb);
    sh4g_fastmem_rts_debit(tp,
      (kind == CGBA_FM_LOAD_W || kind == CGBA_FM_STORE_W) ? 6 : 3);

    if (nfail > 0) {
      for (i = 0; i < nfail; i++)
        sh4g_patch_cond(ewram_fail[i], *tp);
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
    }
    sh4g_patch_cond(not_ewram, *tp);
    /* R0 still equals region (R12) on the not_ewram path! */
  }

  /* ---- 3. Store-only specialized paths (VRAM / Palette / IO16) ---- */
  if (!is_load) {
    /* Byte stores only hit IWRAM/EWRAM natively; everything else stays on C. */
    if (kind == CGBA_FM_STORE_B) {
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
      return entry;
    }

    /* 3a. Direct VRAM store (region 6) for word / halfword. */
    {
      u8 *not_vram, *not_mirror, *vram_unaligned;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 6);                  /* T = VRAM */
        sh4g_close(tp, &cg); }
      not_vram = sh4g_emit_bf_placeholder(tp);

      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, align_mask);
        sh4g_close(tp, &cg); }
      vram_unaligned = sh4g_emit_bf_placeholder(tp);

      sh4g_const(tp, (u32)(uintptr_t)vram, SH4_REG_T2);
      sh4g_const(tp, 0x18000u, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      not_mirror = sh4g_emit_bf_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, -128, SH4_REG_ARG0);
        sh4_emit_shll8(&cg, SH4_REG_ARG0);           /* -0x8000 */
        sh4_emit_add_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(not_mirror, *tp);
      sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);     /* R3 = vram 32K page */

#ifdef CGBA_VRAM_DIRTY_ACTIVE
      /* R0 is 0x00000/0x08000 (BG) or 0x10000 (OBJ): (R0 >> 16) + 1 gives
       * CGBA_VRAM_DIRTY_BG (1) or CGBA_VRAM_DIRTY_OBJ (2) branchlessly. */
      sh4g_vec_load(tp, SH4G_VEC_vram_dirty, SH4_REG_ARG3);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_shlr16(&cg, SH4_REG_RET);
        sh4_emit_add_imm(&cg, 1, SH4_REG_RET);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
        sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_ARG0);
        sh4_emit_mov_l_store(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
        sh4g_close(tp, &cg); }
#endif
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
        sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      sh4g_fastmem_emit_store_xfer(tp, kind);
      sh4g_fastmem_emit_wb(tp, wb);
      sh4g_fastmem_rts_debit(tp, kind == CGBA_FM_STORE_W ? 2 : 1);

      sh4g_patch_cond(vram_unaligned, *tp);
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
      sh4g_patch_cond(not_vram, *tp);
      /* R0 still equals region (R12). */
    }

    /* 3b. Direct Palette RAM store (region 5) for word / halfword. */
    {
      u8 *not_pal;
      u8 *pal_fail[2];
      int npf = 0;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 5);                  /* T = palette RAM */
        sh4g_close(tp, &cg); }
      not_pal = sh4g_emit_bf_placeholder(tp);

      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, align_mask);           /* T = guest aligned */
        sh4g_close(tp, &cg); }
      pal_fail[npf++] = sh4g_emit_bf_placeholder(tp);

      sh4g_const(tp, 0x03FFu, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG3);
        sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_ARG3); /* R7 = palette offset */
        sh4g_close(tp, &cg); }
      if (kind == CGBA_FM_STORE_W) {
        sh4g_const(tp, 0x03FCu, SH4_REG_ARG0);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_cmphi(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
          sh4g_close(tp, &cg); }
        pal_fail[npf++] = sh4g_emit_bt_placeholder(tp);
      }

      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0xFF);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
        sh4g_close(tp, &cg); }

      sh4g_const(tp, (u32)(uintptr_t)palette_ram, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
        if (kind == CGBA_FM_STORE_W) {
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
          sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
          sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
          sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG0, SH4_REG_T2);
        } else {
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
          sh4_emit_mov_w_store_r0(&cg, SH4_REG_ARG0, SH4_REG_T2);
        }
        sh4g_close(tp, &cg); }

      sh4g_fastmem_convert_palette(tp, SH4_REG_T1, SH4_REG_ARG0,
                                   SH4_REG_RET, SH4_REG_T2);
      sh4g_const(tp, (u32)(uintptr_t)palette_ram_converted, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
        sh4_emit_mov_w_store_r0(&cg, SH4_REG_ARG0, SH4_REG_T2);
        sh4g_close(tp, &cg); }

      if (kind == CGBA_FM_STORE_W) {
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_ARG0);
          sh4_emit_shlr16(&cg, SH4_REG_ARG0);
          sh4g_close(tp, &cg); }
        sh4g_fastmem_convert_palette(tp, SH4_REG_ARG0, SH4_REG_T1,
                                     SH4_REG_RET, SH4_REG_T2);
        sh4g_const(tp, (u32)(uintptr_t)palette_ram_converted, SH4_REG_T2);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
          sh4_emit_add_imm(&cg, 2, SH4_REG_RET);
          sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          sh4g_close(tp, &cg); }
      }

#ifdef CGBA_PALETTE_DIRTY_ACTIVE
      sh4g_vec_load(tp, SH4G_VEC_palette_dirty, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, CGBA_PALETTE_DIRTY_ACTIVE, SH4_REG_RET);
        sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_T2);
        sh4g_close(tp, &cg); }
#endif
      sh4g_fastmem_emit_wb(tp, wb);
      sh4g_fastmem_rts_debit(tp, kind == CGBA_FM_STORE_W ? 2 : 1);

      for (i = 0; i < npf; i++)
        sh4g_patch_cond(pal_fail[i], *tp);
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
      sh4g_patch_cond(not_pal, *tp);
      /* R0 still equals region (R12). */
    }

    /* 3c. Halfword IO-register fast path (region 4, non-writeback only). */
    if (kind == CGBA_FM_STORE_UH && !wb) {
      u8 *not_io;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 4);                  /* T = IO */
        sh4g_close(tp, &cg); }
      not_io = sh4g_emit_bf_placeholder(tp);

      /* Shared IO16 return tail (wb + fixed 1-cycle IO debit). */
      io_check = sh4g_emit_bra_placeholder(tp);
      store_tail = *tp;
      sh4g_fastmem_emit_wb(tp, wb);
      sh4g_fastmem_rts_debit(tp, 1);

      sh4g_patch_cond(not_io, *tp);
      sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);

      sh4g_patch_bra(io_check, *tp);
      {
        u8 *not_if, *fail_ie;
        sh4g_fastmem_io16_dispcnt_store(tp, store_tail);
        sh4g_fastmem_io16_dispstat_store(tp, store_tail);
        sh4g_fastmem_io16_pair_direct_store(tp, 0x04000010u, 0x010u, store_tail);
        sh4g_fastmem_io16_pair_direct_store(tp, 0x04000014u, 0x014u, store_tail);
        sh4g_fastmem_io16_pair_direct_store(tp, 0x04000018u, 0x018u, store_tail);
        sh4g_fastmem_io16_pair_direct_store(tp, 0x0400001Cu, 0x01Cu, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000040u, 0x040u, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000044u, 0x044u, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000048u, 0x048u, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x0400004Au, 0x04Au, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x0400004Cu, 0x04Cu, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000050u, 0x050u, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000052u, 0x052u, store_tail);
        sh4g_fastmem_io16_direct_store(tp, 0x04000054u, 0x054u, store_tail);

        sh4g_const(tp, 0x04000202u, SH4_REG_T1);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0); /* T = REG_IF */
          sh4g_close(tp, &cg); }
        not_if = sh4g_emit_bf_placeholder(tp);

        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
          sh4_emit_and_imm(&cg, 0xFF);
          sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_not(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4g_close(tp, &cg); }
        sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x202), SH4_REG_T2);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
          sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_ARG0);
          sh4_emit_mov_w_store(&cg, SH4_REG_ARG0, SH4_REG_T2);
          sh4g_close(tp, &cg); }
        { u8 *b = sh4g_emit_bra_placeholder(tp);
          sh4g_patch_bra(b, store_tail); }

        sh4g_patch_cond(not_if, *tp);
        sh4g_const(tp, 0x04000200u, SH4_REG_T1);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0); /* T = REG_IE */
          sh4g_close(tp, &cg); }
        guards[ng++] = sh4g_emit_bf_placeholder(tp);

        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
          sh4_emit_and_imm(&cg, 0xFF);
          sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4g_close(tp, &cg); }
        sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x200), SH4_REG_T2);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
          sh4_emit_add_imm(&cg, 2, SH4_REG_T2);
          sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
          sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_ARG0); /* T=1: none pending */
          sh4g_close(tp, &cg); }
        { u8 *sk = sh4g_emit_bf_placeholder(tp);
          u8 *b = sh4g_emit_bra_placeholder(tp);
          sh4g_patch_bra(b, store_tail);
          sh4g_patch_cond(sk, *tp); }
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_add_imm(&cg, 6, SH4_REG_T2);
          sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
          sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
          sh4_emit_tst_imm(&cg, 1);                    /* T=1: IME off */
          sh4g_close(tp, &cg); }
        { u8 *sk = sh4g_emit_bf_placeholder(tp);
          u8 *b = sh4g_emit_bra_placeholder(tp);
          sh4g_patch_bra(b, store_tail);
          sh4g_patch_cond(sk, *tp); }
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
          sh4_emit_tst_imm(&cg, 0x80);                 /* T=1: IRQs enabled */
          sh4g_close(tp, &cg); }
        fail_ie = sh4g_emit_bt_placeholder(tp);
        { u8 *b = sh4g_emit_bra_placeholder(tp);
          sh4g_patch_bra(b, store_tail); }
        sh4g_patch_cond(fail_ie, *tp);

        for (i = 0; i < ng; i++)
          sh4g_patch_cond(guards[i], *tp);
        sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
      }
      return entry;
    }

    /* STORE_W non-RAM/VRAM/Palette -> slow C helper. */
    sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
    return entry;
  }

  /* ---- 4. Remaining LOAD regions (IO / Video / Game Pak 4..12) ----
   * R0 == R12 == region (not 2, not 3). Restrict to 4..12 (BIOS 0..1 and
   * backup/unmapped >= 13 stay on C). This also replaces the 0x10000000
   * literal guard since region < 13 implies addr < 0x0D000000 < 0x10000000. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 4, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);    /* T = region >= 4 */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 13, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);    /* T = region >= 13 */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);     /* R0 = addr >> 15 */
    sh4_emit_shll2(&cg, SH4_REG_RET);                /* page index * 4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);       /* T = (page == NULL) */
    sh4g_close(tp, &cg); }
  fragment_entry = sh4g_emit_bt_placeholder(tp);
  fragment_join = *tp;

  if (align_mask) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);               /* T = guest aligned */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  /* R0 = addr & 0x7FFF (in-page offset) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  if (align_mask) {                                  /* unaligned host (NOR) */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, align_mask);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  sh4g_fastmem_emit_load_xfer(tp, kind);
  sh4g_fastmem_emit_wb(tp, wb);
  sh4g_charge_mem_region_specialized(
      tp, SH4_REG_FM_REGION, /*seq=*/0,
      /*is_word=*/(kind == CGBA_FM_LOAD_W), 1);
  sh4g_u16(tp, 0x000B);                              /* RTS */
  sh4g_u16(tp, 0x0009);                              /* delay NOP */

  /* Fragment-entry bridge + common guard failure. */
  sh4g_patch_cond(fragment_entry, *tp);
  fragment_far = sh4g_emit_bra_placeholder(tp);

  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);

  /* Out-of-line 4 KiB NOR fragment lookup (region is already proven < 13
   * and lives in R12, so only region >= 8 needs testing). */
  {
    u8 *miss[4];
    u8 *fragment_ok;
    int nmiss = 0;

    sh4g_patch_bra(fragment_far, *tp);

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_FM_REGION, SH4_REG_RET);
      sh4_emit_mov_imm(&cg, 8, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = region >= 8 */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

    sh4g_const(tp, 0x01FFFFFFu, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);    /* R0 = logical offset */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, (u32)(uintptr_t)&gamepak_size, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = offset >= size */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

    sh4g_const(tp, (u32)(uintptr_t)&cgba_gamepak_fragment_blocks,
               SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T2);
      sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* T = table == NULL */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, -12, SH4_REG_T1);
      sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);   /* R0 = block index */
      sh4_emit_shll2(&cg, SH4_REG_RET);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
      sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* T = block == NULL */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

    sh4g_const(tp, 0x7000u, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);
      sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_T2);
      sh4g_close(tp, &cg); }
    fragment_ok = sh4g_emit_bra_placeholder(tp);

    for (i = 0; i < nmiss; i++)
      sh4g_patch_cond(miss[i], *tp);
    sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
    sh4g_patch_bra(fragment_ok, fragment_join);
  }

  return entry;
}

/* ---- per-site call ------------------------------------------------------ */
/* Same geometry as sh4g_op2_tramp_call, with the 6-literal fastmem tuple. */
#ifdef CGBA_GPSP_HEADLESS_TEST
extern unsigned long cgba_em_fm_n, cgba_em_fm_bytes;
#endif

static inline void sh4g_fastmem_site_raw(u8 **tp, int fm,
                                         const void *helper_fn, u32 opcode,
                                         u32 pc, int cycle_count, u32 params)
{
  const u8 *routine = cgba_sh4_fastmem_routine[fm];
#if CGBA_SH4_PIN_GUEST_R0
  int publish_r0 = 1, reload_r0 = 1;
#if !defined(CGBA_SH4_SELECTIVE_R0_SYNC) || CGBA_SH4_SELECTIVE_R0_SYNC
  int block = fm >= CGBA_FM_COUNT;
  int load = block ? !(fm & 1) : (fm % CGBA_FM_BASE_COUNT < CGBA_FM_STORE_W);
  int wb = block ? fm >= CGBA_FMB_LDM_WB : fm >= CGBA_FM_WB;
  int transfers_r0 = block ? (params & 1) != 0 : (params & 255) == 0;
  int writes_r0 = wb && (block ? (params >> 24) == 0 : (params & 0xFF00) == 0);
  /* Address/writeback operands were already read through the pinned-register
   * accessors. Only store data needs publishing; only a load or base writeback
   * needs reloading. Slow trampolines synchronize r0 themselves, including on
   * exits that never return to this continuation. */
  publish_r0 = !load && transfers_r0;
  reload_r0 = (load && transfers_r0) || writes_r0;
#endif
#endif
  u8 *site;
  u8 *lit;
  unsigned nlit = 6;
  int block_params = 1;
  long bra_disp;
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
  block_params = (params & 0xFFFF0000u) != 0;
  nlit = block_params ? 6u : 5u;
  if ((unsigned)cycle_count > 0xFFFFu) {
    /* Correctness escape for a pathological very long accounting run. */
    sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                        helper_fn, opcode, pc, 1, cycle_count);
    return;
  }
#endif
  (void)block_params;
#ifdef CGBA_GPSP_HEADLESS_TEST
  u8 *stat0 = *tp;
  cgba_em_fm_n++;
#endif

#if CGBA_SH4_PIN_GUEST_R0
  if (publish_r0) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_GUEST_R0, SH4_REG_BASE, 0);
    sh4g_close(tp, &cg); }
#endif
  if (((uintptr_t)*tp + 10) & 3)               /* literals must be 4-aligned */
    sh4g_u16(tp, 0x0009);
  site = *tp;
  lit = site + 10;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | 2));   /* MOV.L L,R0 */
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));       /* JSR @R0    */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  bra_disp = ((long)(lit + nlit * 4) - ((long)(*tp) + 4)) / 2;
  sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));      /* BRA 2f     */
  sh4g_u16(tp, 0x0009);                                        /* delay NOP  */
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)routine);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)helper_fn);
    sh4_emit_u32_be(&cg, opcode);
    sh4_emit_u32_be(&cg, pc);
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
    if (block_params) {
      sh4_emit_u32_be(&cg, (u32)cycle_count & 0xFFFFu);
      sh4_emit_u32_be(&cg, params);
    } else {
      sh4_emit_u32_be(&cg, (params << 16) | ((u32)cycle_count & 0xFFFFu));
    }
#else
    sh4_emit_u32_be(&cg, (u32)cycle_count);
    sh4_emit_u32_be(&cg, params);
#endif
    sh4g_close(tp, &cg);
  }
#if CGBA_SH4_PIN_GUEST_R0
  if (reload_r0) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_GUEST_R0, 0);
    sh4g_close(tp, &cg); }                                     /* 2: R11 */
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
  cgba_em_tuple_routine_b += 4;
  cgba_em_tuple_fn_b += 4;
  cgba_em_tuple_opcode_b += 4;
  cgba_em_tuple_pc_b += 4;
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
  cgba_em_tuple_cycle_b += 2;
  cgba_em_tuple_params_b += block_params ? 4 : 2;
#else
  cgba_em_tuple_cycle_b += 4;
  cgba_em_tuple_params_b += 4;
#endif
#endif
}

static inline void sh4g_fastmem_site(u8 **tp, int fm,
                                     const void *helper_fn, u32 opcode, u32 pc,
                                     int cycle_count, unsigned rd, int wb_rn)
{
  u32 params = (rd * 4u) | (wb_rn >= 0 ? ((u32)wb_rn * 4u) << 8 : 0);
  sh4g_fastmem_site_raw(tp, fm, helper_fn, opcode, pc, cycle_count, params);
}

/* Block-site params: rlist (16-bit ARM layout, LR = bit14) | count << 16 |
 * writeback rn byte-offset << 24 (wb variants only). */
static inline u32 sh4g_fastmem_block_params(u32 rlist, u32 count, int wb_rn)
{
  return (rlist & 0xFFFFu) | (count << 16) |
         (wb_rn >= 0 ? ((u32)wb_rn * 4u) << 24 : 0);
}

/* ---- shared block-transfer routine (runtime register-list loop) ---------
 * Contract: R1 = A (word-aligned LOW address of the ascending run), R6 = the
 * writeback base value (wb variants), PR = site tuple. Guard set mirrors
 * sh4_arm_block_emit.h (the safe superset for Thumb too: region-0 excluded,
 * unmapped/straddling/misaligned runs and SMC-tagged store targets fall to
 * the C helper via the tuple trampoline, BEFORE any architectural write). */
static inline u8 *sh4g_fastmem_emit_block_routine(u8 **tp, int fm)
{
  u8 *entry = *tp;
  int is_load = (fm == CGBA_FMB_LDM || fm == CGBA_FMB_LDM_WB);
  int wb = (fm >= CGBA_FMB_LDM_WB);
  u8 *guards[10]; int ng = 0;
  u8 *to_xfer[3]; int nxfer = 0;
  u8 *not_iwram, *loop_top, *skip_ph;
  int i;

  /* Load params ONCE into R5 (kept live across setup, transfer, wb, charge).
   * R2 = count (params bits 16..20, 1..16). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_ARG1);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_ARG1, 24 >> 2);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x1F);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);  /* R2 = count */
    sh4g_close(tp, &cg); }

  /* Guest alignment (Thumb bases can be unaligned; ARM pre-masks). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);                        /* T = word aligned */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);

  /* Compute region = A >> 24 ONCE into R0 and R12. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_FM_REGION);
    sh4_emit_cmpeq_imm(&cg, 3);                      /* T = IWRAM (region 3) */
    sh4g_close(tp, &cg); }
  not_iwram = sh4g_emit_bf_placeholder(tp);

  /* ---- 1. Direct IWRAM fast path (stack PUSH/POP/LDM/STM) ----
   * Zero memory_map_read lookup, zero literals, single-page straddle check
   * via ((A & 0x7FFF) + count*4 - 1) < 0x8000 (bit 15 == 0 -> shll16; cmp/pz). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_shll2(&cg, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -1, SH4_REG_T2);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);
    sh4_emit_shll16(&cg, SH4_REG_T2);
    sh4_emit_cmppz(&cg, SH4_REG_T2);                 /* T = stays in 32 KiB */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);

  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);         /* R3 = host_ptr */
  if (!is_load) {
    /* IWRAM SMC tag scan: tags live at host_ptr - 0x8000. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, -128, SH4_REG_ARG3);
      sh4_emit_shll8(&cg, SH4_REG_ARG3);             /* -0x8000 */
      sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_ARG3); /* R7 = tag_ptr */
      sh4g_close(tp, &cg); }
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_inc(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = tags clear */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_dt(&cg, SH4_REG_T1);                  /* count--; T = zero */
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */
  }
  to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);

  /* ---- 2. Non-IWRAM path (EWRAM / VRAM / ROM) ---- */
  sh4g_patch_cond(not_iwram, *tp);
  /* R0 == R12 == region (guaranteed != 3). */
  if (is_load) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);   /* T = BIOS/region 0 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 16, SH4_REG_ARG0);
      sh4_emit_cmphs(&cg, SH4_REG_ARG0, SH4_REG_RET); /* T = addr >= 0x10000000 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bt_placeholder(tp);
  } else {
    u8 *store_ok;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 2);                    /* T = EWRAM */
      sh4g_close(tp, &cg); }
    store_ok = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 6);                    /* T = VRAM */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(store_ok, *tp);
  }

  /* off = A & 0x7FFF -> R4; straddle check: (off + count*4 - 1) < 0x8000 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0); /* R4 = off */
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_shll2(&cg, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -1, SH4_REG_T2);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);
    sh4_emit_shll16(&cg, SH4_REG_T2);
    sh4_emit_cmppz(&cg, SH4_REG_T2);                 /* T = one 32 KiB page */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);

  /* page = memory_map_read[A >> 15]; R3 = page + off */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_ARG3);
    sh4_emit_shld(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);
  sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_T2);        /* R3 = host_ptr */

  if (is_load) {                                     /* host-align (NOR) */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);
  } else {
    /* Non-IWRAM store is either VRAM (region 6) or EWRAM (region 2). */
    u8 *ewram_smc;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_FM_REGION, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 6);                    /* T = VRAM */
      sh4g_close(tp, &cg); }
    ewram_smc = sh4g_emit_bf_placeholder(tp);

#ifdef CGBA_VRAM_DIRTY_ACTIVE
    /* Entire transfer is inside one 32 KiB page: bit 16 of A cleanly selects
     * BG (0 -> bit 0 = 1) vs OBJ (1 -> bit 1 = 2), including 0x18000 mirror. */
    sh4g_vec_load(tp, SH4G_VEC_vram_dirty, SH4_REG_ARG3);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 1);
      sh4_emit_add_imm(&cg, 1, SH4_REG_RET);
      sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
      sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_ARG0);
      sh4_emit_mov_l_store(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
      sh4g_close(tp, &cg); }
#endif
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);

    /* EWRAM SMC tag scan: tags live at host_ptr + 0x40000 (4 << 16). */
    sh4g_patch_cond(ewram_smc, *tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 4, SH4_REG_ARG3);
      sh4_emit_shll16(&cg, SH4_REG_ARG3);            /* 0x40000 */
      sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_ARG3); /* R7 = tag_ptr */
      sh4g_close(tp, &cg); }
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_inc(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = tags clear */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_dt(&cg, SH4_REG_T1);                  /* count--; T = zero */
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);
  }

  /* Common guard-failure bridge placed mid-routine so every BT/BF above stays
   * well within +-254 bytes. */
  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);

  /* ---- 3. Shared transfer loop ----
   * R3 = host_ptr, R5 = params, R6 = wb_val, R12 = region.
   * R0 = greg_off (0,4,...), R1 = swap scratch, R2 = rlist, R7 = word. */
  for (i = 0; i < nxfer; i++)
    sh4g_patch_bra(to_xfer[i], *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_extu_w(&cg, SH4_REG_ARG1, SH4_REG_T1);  /* R2 = rlist */
    sh4_emit_mov_imm(&cg, 0, SH4_REG_RET);           /* R0 = greg_off */
    sh4g_close(tp, &cg); }
  loop_top = *tp;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_shlr(&cg, SH4_REG_T1);                  /* T = bit0 */
    sh4g_close(tp, &cg); }
  skip_ph = sh4g_emit_bf_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    if (is_load) {
      sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG3);
      sh4_emit_swap_b(&cg, SH4_REG_ARG3, SH4_REG_T0);
      sh4_emit_swap_w(&cg, SH4_REG_T0, SH4_REG_T0);
      sh4_emit_swap_b(&cg, SH4_REG_T0, SH4_REG_ARG3);
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG3, SH4_REG_BASE);
    } else {
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_ARG3);
      sh4_emit_swap_b(&cg, SH4_REG_ARG3, SH4_REG_T0);
      sh4_emit_swap_w(&cg, SH4_REG_T0, SH4_REG_T0);
      sh4_emit_swap_b(&cg, SH4_REG_T0, SH4_REG_ARG3);
      sh4_emit_mov_l_store(&cg, SH4_REG_ARG3, SH4_REG_T2);
      sh4_emit_add_imm(&cg, 4, SH4_REG_T2);
    }
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(skip_ph, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
    sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T1);       /* list empty? */
    sh4g_close(tp, &cg); }
  { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
    sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */

  /* ---- 4. Writeback (precomputed base in R6, params in R5) ---- */
  if (wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);                /* R0 = wb byte-off */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
    sh4g_close(tp, &cg);
  }

  /* ---- 5. Cycle charge & RTS (delay-slot sub) ---- */
  { u8 *not_iwram_cyc;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0x1F);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1); /* R2 = count */
      sh4_emit_mov_reg(&cg, SH4_REG_FM_REGION, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 3);                     /* T = IWRAM */
      sh4g_close(tp, &cg); }
    not_iwram_cyc = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_rts(&cg);
      sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);  /* IWRAM: count * 1 */
      sh4g_close(tp, &cg); }

    sh4g_patch_cond(not_iwram_cyc, *tp);
    if (is_load) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_shll(&cg, SH4_REG_RET);
        sh4_emit_add_imm(&cg, 1, SH4_REG_RET);        /* word column */
        sh4g_close(tp, &cg); }
      sh4g_vec_load(tp, SH4G_VEC_ws_cyc_seq, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_ARG0);
        sh4_emit_extu_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_sts_macl(&cg, SH4_REG_ARG0);
        sh4_emit_rts(&cg);
        sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_CYCLES);
        sh4g_close(tp, &cg); }
    } else {
      /* Remaining store regions: EWRAM (6*count) or VRAM (2*count). */
      u8 *vram_cyc;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 6);
        sh4g_close(tp, &cg); }
      vram_cyc = sh4g_emit_bt_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
        sh4_emit_shll(&cg, SH4_REG_RET);              /* 2*count */
        sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET); /* 3*count */
        sh4_emit_shll(&cg, SH4_REG_RET);              /* 6*count */
        sh4_emit_rts(&cg);
        sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_CYCLES);
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(vram_cyc, *tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_shll(&cg, SH4_REG_T1);               /* 2*count */
        sh4_emit_rts(&cg);
        sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
        sh4g_close(tp, &cg); }
    }
  }

  return entry;
}

/* ---- shared PSR mode-change (set_cpu_mode) routine ----------------------
 * Called (JSR) from MSR-cpsr control-byte sites whose merged value changes
 * the CPSR mode nibble; keeping the ~110-byte re-bank OUT of translated
 * blocks matters more than its instruction count — inlining it grew the hot
 * IRQ-dispatcher block and cost +59% I-cache misses on AW. In: R1 = merged
 * CPSR (preserved; the site stores it into the cached CPSR R8 after
 * return). Mirrors gpSP's set_cpu_mode exactly: retire r13/r14 into
 * reg_mode[old & 0xF][5..6] (r8..r14 into [0..6] when entering FIQ), load
 * them back from the new row (7 registers when leaving FIQ), update
 * reg[CPU_MODE]. Same-row transitions (USER<->SYSTEM, INVALID<->INVALID)
 * retire-then-reload the same slots, exactly like the C code. Leaf; only
 * uses caller-scratch (R0, R2, R3, R5, R6, R7). */
extern u32 reg_mode[7][7];
extern const u32 cpu_modes[16];

static inline u8 *sh4g_psr_emit_rebank_routine(u8 **tp)
{
  u8 *entry = *tp;
  u8 *to_save7, *save_done, *to_load7, *load_done;
  int i;

  /* R7 = new gpSP mode = cpu_modes[R1 & 0xF] */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xF);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)cpu_modes, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T1, SH4_REG_ARG3);
    sh4g_close(tp, &cg); }
  /* R5 = old gpSP mode */
  sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_ARG1);

  /* R6 = &reg_mode[old & 0xF], R3 = &reg_mode[new & 0xF] (28-byte rows) */
  sh4g_const(tp, (u32)(uintptr_t)reg_mode, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xF);
    sh4_emit_shll2(&cg, SH4_REG_RET);                /* 4*old  */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_shll2(&cg, SH4_REG_RET);                /* 16*old */
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);  /* 28*old */
    sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG2);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG2);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xF);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_RET);  /* 28*new */
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);  /* R3 = new row */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 0x12);                   /* entering FIQ? */
    sh4g_close(tp, &cg); }
  to_save7 = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);                  /* retire r13/r14 */
    sh4_emit_load_greg(&cg, SH4_GREG_SP, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 5);
    sh4_emit_load_greg(&cg, SH4_GREG_LR, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 6);
    sh4g_close(tp, &cg); }
  save_done = sh4g_emit_bra_placeholder(tp);

  sh4g_patch_cond(to_save7, *tp);
  { sh4_codegen cg = sh4g_open(tp);                  /* retire r8..r14 */
    for (i = 0; i < 7; i++) {
      sh4_emit_load_greg(&cg, 8 + (unsigned)i, SH4_REG_T1);
      sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, (unsigned)i);
    }
    sh4g_close(tp, &cg); }

  sh4g_patch_bra(save_done, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 0x12);                   /* leaving FIQ? */
    sh4g_close(tp, &cg); }
  to_load7 = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);                  /* load r13/r14 */
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 5);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_SP);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 6);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_LR);
    sh4g_close(tp, &cg); }
  load_done = sh4g_emit_bra_placeholder(tp);

  sh4g_patch_cond(to_load7, *tp);
  { sh4_codegen cg = sh4g_open(tp);                  /* load r8..r14 */
    for (i = 0; i < 7; i++) {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, (unsigned)i);
      sh4_emit_store_greg(&cg, SH4_REG_T1, 8 + (unsigned)i);
    }
    sh4g_close(tp, &cg); }

  sh4g_patch_bra(load_done, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_store_greg(&cg, SH4_REG_ARG3, SH4_GREG_CPU_MODE);
    sh4_emit_rts(&cg);
    sh4_emit_nop(&cg);
    sh4g_close(tp, &cg); }

  return entry;
}

/* Routine table, filled by cgba_sh4_fastmem_init() (sh4_fastmem.c). */
extern u8 *cgba_sh4_psr_rebank_routine;
void cgba_sh4_fastmem_init(void);

#endif /* CGBA_SH4_FASTMEM_H */
