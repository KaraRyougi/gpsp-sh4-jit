#ifndef CGBA_SH4_FASTMEM_H
#define CGBA_SH4_FASTMEM_H

/*
 * Out-of-line native memory fast paths — the translation-cache DENSITY fix.
 *
 * Metroid's in-world working set overflows the 896KB ROM translation cache
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

extern u8 *memory_map_read[];
extern u16 io_registers[512];          /* eswap16'd (LE-layout) halfwords */
extern u16 palette_ram[512];
extern u16 palette_ram_converted[512];
extern u8 iwram[];
extern u8 vram[1024 * 96];
void sh4_op2_pc_mem_tramp(void);

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

/* ---- the shared routine body ------------------------------------------- */
/* Emits one variant at *tp; returns its entry point. Mirrors the inline
 * bodies of sh4_arm_ldst_emit.h / sh4g_thumb_ldst_native instruction for
 * instruction, with reg[rd]/writeback offsets read from the site tuple. */
static inline u8 *sh4g_fastmem_emit_routine(u8 **tp, int fm)
{
  u8 *entry = *tp;
  int is_load = cgba_fm_is_load(fm);
  int kind = fm % CGBA_FM_BASE_COUNT;
  int align_mask = cgba_fm_align_mask(fm);
  int wb = fm >= CGBA_FM_WB;
  u8 *guards[8]; int ng = 0;
  u8 *io_check = NULL, *store_tail = NULL;
  u8 *store_vram_page_done = NULL;
  int i;

  /* params -> R5 early (R5 is untouched by everything below). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_ARG1);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_ARG1, 24 >> 2);
    sh4g_close(tp, &cg); }

  if (!is_load && (kind == CGBA_FM_STORE_UH || kind == CGBA_FM_STORE_W)) {
    u8 *miss[5];
    int nmiss = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 5);                  /* T = palette RAM */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

    if (align_mask) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, align_mask);           /* T = guest aligned */
      sh4g_close(tp, &cg);
      miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
    }

    sh4g_const(tp, 0x03FFu, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG3);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_ARG3); /* R7 = palette offset */
      sh4g_close(tp, &cg); }
    if (kind == CGBA_FM_STORE_W) {
      sh4g_const(tp, 0x03FCu, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmphi(&cg, SH4_REG_ARG0, SH4_REG_ARG3); /* T = offset > 0x3fc */
        sh4g_close(tp, &cg); }
      miss[nmiss++] = sh4g_emit_bt_placeholder(tp);
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

    if (wb) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
      sh4g_close(tp, &cg);
    }
    sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0,
                        /*is_word=*/(kind == CGBA_FM_STORE_W), 1);
    sh4g_u16(tp, 0x000B);                          /* RTS */
    sh4g_u16(tp, 0x0009);                          /* delay NOP */

    for (i = 0; i < nmiss; i++)
      sh4g_patch_cond(miss[i], *tp);
  }

  if (is_load) {
    u8 *iwram_miss[2];
    int niwram = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 3);                    /* T = IWRAM */
      sh4g_close(tp, &cg); }
    iwram_miss[niwram++] = sh4g_emit_bf_placeholder(tp);

    if (align_mask) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, align_mask);             /* T = guest aligned */
      sh4g_close(tp, &cg);
      iwram_miss[niwram++] = sh4g_emit_bf_placeholder(tp);
    }

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
      sh4g_close(tp, &cg); }
    sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
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
      sh4_emit_and_imm(&cg, 0xFF);                   /* R0 = rd byte-offset */
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_BASE);
      sh4g_close(tp, &cg); }
    if (wb) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);                   /* R0 = rn byte-offset */
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
      sh4g_close(tp, &cg);
    }
    sh4g_cycle_debit(tp, 1);                         /* IWRAM nseq cost */
    sh4g_u16(tp, 0x000B);                            /* RTS */
    sh4g_u16(tp, 0x0009);                            /* delay NOP */

    for (i = 0; i < niwram; i++)
      sh4g_patch_cond(iwram_miss[i], *tp);
  }

  /* map guard: memory_map_read[] covers 0x00000000..0x0fffffff only */
  sh4g_const(tp, 0x10000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);     /* T = addr >= 0x10000000 */
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
  if (!is_load && kind != CGBA_FM_STORE_B) {
    /* VRAM is not always present in memory_map_read[], but halfword/word VRAM
     * stores are side-effect-free plain writes in gpSP. Synthesize the 32 KiB
     * page base directly and keep R0 as the later addr&0x7fff page offset. */
    u8 *not_vram, *not_mirror;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 6);                    /* T = VRAM */
      sh4g_close(tp, &cg); }
    not_vram = sh4g_emit_bf_placeholder(tp);

    sh4g_const(tp, (u32)(uintptr_t)vram, SH4_REG_T2);
    sh4g_const(tp, 0x18000u, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* page base: 0/80k/100k/180k */
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_RET);
      sh4g_close(tp, &cg); }
    not_mirror = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_RET);     /* 0x18000 mirrors to 0x10000 */
    sh4g_patch_cond(not_mirror, *tp);
    sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);
    store_vram_page_done = sh4g_emit_bra_placeholder(tp);

    sh4g_patch_cond(not_vram, *tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* restore NULL-page test */
      sh4g_close(tp, &cg); }
  }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);
  if (store_vram_page_done)
    sh4g_patch_bra(store_vram_page_done, *tp);

  if (is_load) {
    /* regions 2..12 (RAM/IO/video/gamepak); BIOS + backup stay on C */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);              /* R0 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = region >= 2 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 13, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = backup/EEPROM */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bt_placeholder(tp);
  } else {
    /* stores: RAM (SMC-tagged) or VRAM word/half (byte stores stay on C).
       Halfword non-RAM stores detour through the interrupt-register fast
       path below (AW's per-scanline ISR acks REG_IF / rewrites REG_IE
       ~550K times per 2000 frames) before giving up to C. */
    u8 *vram_ok = NULL;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);              /* R0 = addr >> 24 */
      sh4g_close(tp, &cg); }
    if (kind != CGBA_FM_STORE_B) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 6);                  /* T = VRAM */
        sh4g_close(tp, &cg); }
      vram_ok = sh4g_emit_bt_placeholder(tp);
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_shlr(&cg, SH4_REG_RET);               /* R0 = addr >> 25 */
      sh4_emit_cmpeq_imm(&cg, 1);                    /* regions 2 or 3 */
      sh4g_close(tp, &cg); }
    if (kind == CGBA_FM_STORE_UH)
      io_check = sh4g_emit_bf_placeholder(tp);
    else
      guards[ng++] = sh4g_emit_bf_placeholder(tp);
    if (vram_ok)
      sh4g_patch_cond(vram_ok, *tp);
  }
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
  if (is_load && align_mask) {                       /* unaligned host (NOR) */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);  /* T1 = page offset */
    sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_RET);  /* R0 = host pointer */
    sh4_emit_tst_imm(&cg, align_mask);               /* T = host ptr aligned */
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);  /* restore offset */
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  if (!is_load) {
    /* SMC tag scan (RAM only; VRAM has no tag mirror). */
    u8 *bf_iwram, *bra_tag_ready, *vram_skip;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      sh4_emit_shlr16(&cg, SH4_REG_T1);
      sh4_emit_shlr8(&cg, SH4_REG_T1);               /* R2 = addr >> 24 */
      sh4_emit_mov_imm(&cg, 6, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1); /* T = VRAM */
      sh4g_close(tp, &cg); }
    vram_skip = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG3);
      sh4_emit_mov_imm(&cg, 2, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_T1); /* T = EWRAM */
      sh4g_close(tp, &cg); }
    bf_iwram = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, 0x40000u, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG3);
    bra_tag_ready = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(bf_iwram, *tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG3);
    sh4g_patch_bra(bra_tag_ready, *tp);
    { sh4_codegen cg = sh4g_open(tp);
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
    guards[ng++] = sh4g_emit_bf_placeholder(tp);      /* SMC -> slow */
    sh4g_patch_cond(vram_skip, *tp);
  }

  /* transfer */
  { sh4_codegen cg = sh4g_open(tp);
    if (is_load) {
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
      /* reg[rd] = value; rd offset from params */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);                   /* R0 = rd byte-offset */
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_BASE);
    } else {
      /* R0 already holds addr & 0x7FFF (the in-page offset computed above): the
       * SMC tag scan reads it via @(R0,tag) but never writes R0, and the VRAM
       * sub-path bypasses the scan entirely, so on every store sub-path R0 is
       * still the page offset here. Park it in R7 (ARG3 — the tag-page pointer,
       * dead after the scan; the writeback value lives in R6/ARG2, untouched)
       * across the R0-indexed reg[rd] load, then restore it for the store,
       * instead of rebuilding it with a 5-instruction shift dance. */
      sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG3);   /* R7 = page offset */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);   /* R0 = page offset */
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
    }
    sh4g_close(tp, &cg); }

  /* wb variants: commit reg[rn] = R6 AFTER the transfer (rd==rn loads are
   * translate-time bailed) — and after every guard, so the C fallback always
   * re-executes from the original base. */
  store_tail = *tp;
  if (wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);                     /* R0 = rn byte-offset */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
    sh4g_close(tp, &cg);
  }

  /* charge the access (nonseq; word column for 32-bit) and return */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0,
                      /*is_word=*/(kind == CGBA_FM_LOAD_W || kind == CGBA_FM_STORE_W), 1);
  sh4g_u16(tp, 0x000B);                              /* RTS */
  sh4g_u16(tp, 0x0009);                              /* delay NOP */

  /* Common guard failure. Keep this close to the early map/alignment/SMC
   * guards: they are disp8 BT/BF placeholders, and the optional IO-specialized
   * tail below can grow independently. */
  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, (const void *)sh4_op2_pc_mem_tramp);
  ng = 0;

  /* Halfword-store interrupt-register fast path. Mirrors gpSP's
   * write_io_register16 exactly for the two hot ISR registers:
   *   WIN0H/WIN0V/WININ/WINOUT/BLDCNT/BLDALPHA/BLDY: plain
   *     write_ioreg, no alert.
   *   REG_IF (0x202): IF &= ~value — pure, no alert possible.
   *   REG_IE (0x200): IE = value, then check_interrupt() — the write is
   *     committed natively and, when it would unmask a pending IRQ
   *     (IE & IF, IME on, CPSR I clear), falls through to the C tramp,
   *     which redoes the (idempotent) write and raises the IRQ.
   * io_registers holds eswap16'd (LE-layout) halfwords; the AND-NOT is a
   * per-byte operation, so it is done directly on the swapped forms. */
  if (io_check) {
    u8 *not_if, *fail_ie;
    sh4g_patch_cond(io_check, *tp);
    sh4g_fastmem_io16_dispcnt_store(tp, store_tail);
    sh4g_fastmem_io16_dispstat_store(tp, store_tail);
    /* BG scroll registers are plain io_registers writes; grouping them here
     * keeps Yoshi's per-frame scroll traffic out of the C helper without
     * growing every translated Thumb STRH site. */
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
      sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);   /* T = REG_IF */
      sh4g_close(tp, &cg); }
    not_if = sh4g_emit_bf_placeholder(tp);

    { sh4_codegen cg = sh4g_open(tp);                /* value = reg[rd] */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);  /* LE layout */
      sh4_emit_not(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4g_close(tp, &cg); }
    sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x202), SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_ARG0);   /* IF &= ~value */
      sh4_emit_mov_w_store(&cg, SH4_REG_ARG0, SH4_REG_T2);
      sh4g_close(tp, &cg); }
    { u8 *b = sh4g_emit_bra_placeholder(tp);
      sh4g_patch_bra(b, store_tail); }

    sh4g_patch_cond(not_if, *tp);
    sh4g_const(tp, 0x04000200u, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);   /* T = REG_IE */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);     /* other IO -> C */

    { sh4_codegen cg = sh4g_open(tp);                /* IE = value */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);  /* LE layout */
      sh4g_close(tp, &cg); }
    sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x200), SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
      sh4_emit_add_imm(&cg, 2, SH4_REG_T2);
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0); /* IF (LE) */
      sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_ARG0);   /* T=1: none pending */
      sh4g_close(tp, &cg); }
    /* T=1 -> tail. BT disp8 reach (+-256B) cannot span back over the charge
       block, so each early-out is BF-skip over a BRA (disp12). */
    { u8 *sk = sh4g_emit_bf_placeholder(tp);
      u8 *b = sh4g_emit_bra_placeholder(tp);
      sh4g_patch_bra(b, store_tail);
      sh4g_patch_cond(sk, *tp); }
    { sh4_codegen cg = sh4g_open(tp);                /* IME (swap: true order) */
      sh4_emit_add_imm(&cg, 6, SH4_REG_T2);
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
      sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, 1);                      /* T=1: IME off */
      sh4g_close(tp, &cg); }
    { u8 *sk = sh4g_emit_bf_placeholder(tp);
      u8 *b = sh4g_emit_bra_placeholder(tp);
      sh4g_patch_bra(b, store_tail);
      sh4g_patch_cond(sk, *tp); }
    { sh4_codegen cg = sh4g_open(tp);                /* CPSR I (cached in R8) */
      sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, 0x80);                   /* T=1: IRQs enabled */
      sh4g_close(tp, &cg); }
    fail_ie = sh4g_emit_bt_placeholder(tp);          /* would raise -> C */
    { u8 *b = sh4g_emit_bra_placeholder(tp);
      sh4g_patch_bra(b, store_tail); }
    sh4g_patch_cond(fail_ie, *tp);
    /* falls into the guard-failure tramp below */

    /* IO-tail guard failure: straight into the compact C-helper trampoline. PR
     * still points at the site's BRA, so the tramp reads fn/opcode/pc/cycles
     * from the same tuple this routine was called with. */
    for (i = 0; i < ng; i++)
      sh4g_patch_cond(guards[i], *tp);
    sh4g_far_jmp(tp, (const void *)sh4_op2_pc_mem_tramp);
  }

  return entry;
}

/* ---- per-site call ------------------------------------------------------ */
/* Same geometry as sh4g_op2_tramp_call, with the 6-literal fastmem tuple. */
#ifdef CGBA_GPSP_HEADLESS_TEST
extern unsigned long cgba_em_fm_n, cgba_em_fm_bytes;
#endif

static inline void sh4g_fastmem_site_raw(u8 **tp, const u8 *routine,
                                         const void *helper_fn, u32 opcode,
                                         u32 pc, int cycle_count, u32 params)
{
  u8 *site;
  u8 *lit;
  long bra_disp;
#ifdef CGBA_GPSP_HEADLESS_TEST
  u8 *stat0 = *tp;
  cgba_em_fm_n++;
#endif

  if (((uintptr_t)*tp + 10) & 3)               /* literals must be 4-aligned */
    sh4g_u16(tp, 0x0009);
  site = *tp;
  lit = site + 10;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | 2));   /* MOV.L L,R0 */
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));       /* JSR @R0    */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  bra_disp = ((long)(lit + 6 * 4) - ((long)(*tp) + 4)) / 2;
  sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));      /* BRA 2f     */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)routine);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)helper_fn);
    sh4_emit_u32_be(&cg, opcode);
    sh4_emit_u32_be(&cg, pc);
    sh4_emit_u32_be(&cg, (u32)cycle_count);
    sh4_emit_u32_be(&cg, params);
    sh4g_close(tp, &cg);
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
#endif
}

static inline void sh4g_fastmem_site(u8 **tp, const u8 *routine,
                                     const void *helper_fn, u32 opcode, u32 pc,
                                     int cycle_count, unsigned rd, int wb_rn)
{
  u32 params = (rd * 4u) | (wb_rn >= 0 ? ((u32)wb_rn * 4u) << 8 : 0);
  sh4g_fastmem_site_raw(tp, routine, helper_fn, opcode, pc, cycle_count, params);
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
  u8 *guards[8]; int ng = 0;
  u8 *loop_top, *skip_ph, *vram_ok = NULL, *vram_skip = NULL;
  int i;

  /* count -> R2 (params bits 16-20) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_T1);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T1, SH4_REG_T1, 24 >> 2);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x1F);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);  /* R2 = count */
    sh4g_close(tp, &cg); }

  /* guest alignment (Thumb bases can be unaligned; ARM pre-masks) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);                        /* T = word aligned */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);

  /* straddle: (A >> 15) == ((A + count*4 - 1) >> 15) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_shll2(&cg, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -1, SH4_REG_T2);
    sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);   /* R3 = A + count*4 - 1 */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_ARG0);
    sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_T2);
    sh4_emit_cmpeq(&cg, SH4_REG_RET, SH4_REG_T2);    /* T = one page */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bf_placeholder(tp);

  /* map bound */
  sh4g_const(tp, 0x10000000u, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);

  /* region */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  if (is_load) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);   /* T = BIOS/region 0 */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bt_placeholder(tp);
  } else {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 6);                    /* T = VRAM */
      sh4g_close(tp, &cg); }
    vram_ok = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_shlr(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 1);                    /* T = EWRAM/IWRAM */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(vram_ok, *tp);
  }

  /* page -> R3 */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_ARG0);
    sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_emit_bt_placeholder(tp);

  /* R0 = A & 0x7FFF */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET); sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET); sh4_emit_shlr(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  if (is_load) {                                     /* host-align (NOR) */
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0);
    sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4g_close(tp, &cg);
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  if (!is_load) {
    /* SMC tag pre-scan over the whole run; VRAM has no tag mirror. */
    u8 *bf_iwram, *bra_ready;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG3);
      sh4_emit_shlr16(&cg, SH4_REG_ARG3);
      sh4_emit_shlr8(&cg, SH4_REG_ARG3);
      sh4_emit_mov_imm(&cg, 6, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
      sh4g_close(tp, &cg); }
    vram_skip = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_ARG3); /* R7 = tag page */
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1);
      sh4_emit_shlr16(&cg, SH4_REG_ARG1);
      sh4_emit_shlr8(&cg, SH4_REG_ARG1);               /* R5 = region */
      sh4_emit_mov_imm(&cg, 2, SH4_REG_ARG0);
      sh4_emit_cmpeq(&cg, SH4_REG_ARG0, SH4_REG_ARG1); /* T = EWRAM */
      sh4g_close(tp, &cg); }
    bf_iwram = sh4g_emit_bf_placeholder(tp);
    sh4g_const(tp, 0x40000u, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG3);
    bra_ready = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(bf_iwram, *tp);
    sh4g_const(tp, (u32)-0x8000, SH4_REG_ARG0);
    sh4g_add_reg(tp, SH4_REG_ARG0, SH4_REG_ARG3);
    sh4g_patch_bra(bra_ready, *tp);
    /* scan loop: R5 free (params reloadable via PR); R4 saves the start off */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG0); /* R4 = start off */
      sh4g_close(tp, &cg); }
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_ARG3, SH4_REG_ARG1);
      sh4_emit_tst(&cg, SH4_REG_ARG1, SH4_REG_ARG1);   /* T = tags clear */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      sh4_emit_dt(&cg, SH4_REG_T1);                    /* count--; T = zero */
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET); /* restore off */
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(vram_skip, *tp);
  }

  /* transfer loop: R2 = rlist (params & 0xFFFF via PR), R4 = greg offset,
   * R5 = page offset, R7 = value, R0 = juggled index */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_T1);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T1, SH4_REG_T1, 24 >> 2);
    sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);      /* R2 = rlist */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG1);  /* R5 = page off */
    sh4_emit_mov_imm(&cg, 0, SH4_REG_ARG0);            /* R4 = greg off */
    sh4g_close(tp, &cg); }
  loop_top = *tp;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_shlr(&cg, SH4_REG_T1);                    /* T = bit0 */
    sh4g_close(tp, &cg); }
  skip_ph = sh4g_emit_bf_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    if (is_load) {
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_ARG3);
      sh4_emit_swap_b(&cg, SH4_REG_ARG3, SH4_REG_RET);
      sh4_emit_swap_w(&cg, SH4_REG_RET, SH4_REG_RET);
      sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_ARG3);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG3, SH4_REG_BASE);
    } else {
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_ARG3);
      sh4_emit_swap_b(&cg, SH4_REG_ARG3, SH4_REG_RET);
      sh4_emit_swap_w(&cg, SH4_REG_RET, SH4_REG_RET);
      sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_ARG3);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG3, SH4_REG_T2);
    }
    sh4_emit_add_imm(&cg, 4, SH4_REG_ARG1);
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(skip_ph, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, 4, SH4_REG_ARG0);
    sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T1);         /* list empty? */
    sh4g_close(tp, &cg); }
  { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
    sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); }   /* BF loop_top */

  /* writeback (precomputed base in R6) */
  if (wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_RET);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_RET, SH4_REG_RET, 24 >> 2);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);                  /* R0 = wb byte-off */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
    sh4g_close(tp, &cg);
  }

  /* charge: seq word cost x count (recomputed from the tuple) */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sts_pr(&cg, SH4_REG_T1);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T1, SH4_REG_T1, 24 >> 2);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0x1F);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);    /* R2 = count */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_add_imm(&cg, 1, SH4_REG_RET);             /* word column */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)ws_cyc_seq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_ARG0);
    sh4_emit_extu_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_sts_macl(&cg, SH4_REG_ARG0);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
  sh4g_u16(tp, 0x000B);                                /* RTS */
  sh4g_u16(tp, 0x0009);

  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, (const void *)sh4_op2_pc_mem_tramp);

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
extern u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
extern u8 *cgba_sh4_psr_rebank_routine;
void cgba_sh4_fastmem_init(void);

#endif /* CGBA_SH4_FASTMEM_H */
