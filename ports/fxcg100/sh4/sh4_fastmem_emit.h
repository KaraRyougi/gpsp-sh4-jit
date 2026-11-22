/* Resident SH-4 fastmem / block / POP+BX / PSR-rebank routine generators.
 * Included only by sh4_fastmem.c (and execution-oracle / audit unit tests);
 * translation-time emitters only need the lightweight sh4_fastmem.h ABI. */
#ifndef CGBA_SH4_FASTMEM_EMIT_H
#define CGBA_SH4_FASTMEM_EMIT_H

#include "ports/fxcg100/sh4/sh4_fastmem.h"

static inline void sh4g_fm_lit_or_const(u8 **tp, uint32_t value, unsigned rn)
{
  if (!sh4g_litseg_add(tp, value, rn))
    sh4g_const(tp, value, rn);
}

/* Unified fast RAM SMC bypass check: loads *(const u32 *)code_end_ptr and tests
 * `off_reg >= *code_end_ptr`. Emits BT (clean -> skip tag check) when
 * !branch_on_fail, or BF (dirty/below-watermark -> cold path) when
 * branch_on_fail. */
static inline u8 *sh4g_fm_emit_smc_bypass(u8 **tp, const void *code_end_ptr,
                                          unsigned off_reg, unsigned addr_reg,
                                          unsigned tmp_reg, int use_lit,
                                          int branch_on_fail)
{
  if (use_lit)
    sh4g_fm_lit_or_const(tp, (u32)(uintptr_t)code_end_ptr, addr_reg);
  else
    sh4g_const(tp, (u32)(uintptr_t)code_end_ptr, addr_reg);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, addr_reg, tmp_reg);
    sh4_emit_cmphs(&cg, tmp_reg, off_reg);       /* T = (off_reg >= *code_end) */
    sh4g_close(tp, &cg); }
  return branch_on_fail ? sh4g_emit_bf_placeholder(tp)
                        : sh4g_emit_bt_placeholder(tp);
}

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

static inline void sh4g_fastmem_io16_dispstat_store(u8 **tp, u8 *store_tail)
{
  u8 *miss;

  sh4g_const(tp, 0x04000004u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  miss = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
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
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
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
    sh4_emit_mov_imm(&cg, SH4_GREG_OAM_UPDATED * 4, SH4_REG_RET); /* reg[OAM_UPDATED] */
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
 * directly into the delay slot (OOL descriptors make PR the exact resume PC). */
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
static inline void sh4g_fastmem_emit_load_xfer(u8 **tp, int kind, int rd_clean)
{
  sh4_codegen cg = sh4g_open(tp);
  switch (kind) {
  case CGBA_FM_LOAD_W:
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
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
  if (!rd_clean) {
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);                     /* R0 = rd byte-offset */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_BASE);
  }
  sh4g_close(tp, &cg);
}

/* Emit reg[rd] load, byte-swap, and store to @(R0, R3); preserves R0 via R7.
 * When val_in_t1 is set, reg[rd] is already loaded into R2 (SH4_REG_T1). */
static inline void sh4g_fastmem_emit_store_xfer(u8 **tp, int kind,
                                                int rd_clean, int val_in_t1)
{
  sh4_codegen cg = sh4g_open(tp);
  if (!val_in_t1) {
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG3); /* R7 = offset */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    if (!rd_clean)
      sh4_emit_and_imm(&cg, 0xFF);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET); /* R0 = offset */
  }
  switch (kind) {
  case CGBA_FM_STORE_W:
    sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
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
 * Dispatches canonical IWRAM (3) and EWRAM (2) directly for BOTH loads and
 * stores (avoiding memory_map_read[] D-cache traffic and runtime cycle-table
 * branches), computes `region = addr >> 24` into R0 lazily for non-IWRAM
 * hits (and preserves it in R7 across section 4 without touching R12), handles
 * Palette/VRAM/IO16 stores directly, and falls through to the mapped /
 * fragmented-NOR load path for ROM/video/IO reads. */
/* Out-of-line IO16 halfword store handler (region 4, non-writeback). Handles
 * DISPSTAT, IF, IE (non-IRQ-raising), DISPCNT, and all side-effect-free video
 * IO16 registers in [0x008..0x026] | [0x030..0x036] | [0x040..0x054] via one
 * compact range dispatcher, falling back to canonical write_io_register16() for
 * everything else (DMA, timers, sound, affine refs, pending deferred flushes). */
static inline u8 *sh4g_fastmem_emit_io16_store(u8 **tp)
{
  u8 *entry = *tp;
  const void *const slow_tramp = (const void *)sh4_fastmem_compact8_tramp;
  u8 *io_check, *store_tail, *video_store_tail, *not_if, *not_ie;
  u8 *fail_odd, *fail_ie, *fail_range, *hit_a, *hit_b, *slow_label;
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
  u8 *fail_pending;
#endif

  sh4g_litseg_begin();
  io_check = sh4g_emit_bra_placeholder(tp);
  store_tail = *tp;
  sh4g_fastmem_rts_debit(tp, 1);
#ifdef CGBA_VRAM_DIRTY_ACTIVE
  video_store_tail = *tp;
  sh4g_const(tp, (u32)(uintptr_t)&cgba_video_io_dirty, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail); }
#else
  video_store_tail = store_tail;
#endif

  sh4g_patch_bra(io_check, *tp);
  /* Stash incoming store value (R2) in R5 before address comparisons use R2. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_ARG1);
    sh4g_close(tp, &cg); }
  /* Unaligned halfword IO stores always defer to canonical write_memory16. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 1);                      /* T = even */
    sh4g_close(tp, &cg); }
  { u8 *ok = sh4g_emit_bt_placeholder(tp);
    fail_odd = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(ok, *tp); }

  /* Non-video IO registers first: unaffected by deferred scanline queue. */
  sh4g_fastmem_io16_dispstat_store(tp, store_tail);

  sh4g_const(tp, 0x04000202u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);   /* T = REG_IF */
    sh4g_close(tp, &cg); }
  not_if = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
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
    sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0);   /* T = REG_IE */
    sh4g_close(tp, &cg); }
  not_ie = sh4g_emit_bf_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)((u8 *)io_registers + 0x200), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_add_imm(&cg, 2, SH4_REG_T2);
    sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
    sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_ARG0);   /* T=1: none pending */
    sh4g_close(tp, &cg); }
  { u8 *sk = sh4g_emit_bf_placeholder(tp);
    u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail);
    sh4g_patch_cond(sk, *tp); }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, 6, SH4_REG_T2);
    sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_RET);
    /* Match canonical cpu_has_interrupt(): IME != 0 (endian-invariant). */
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);   /* T=1: IME == 0 */
    sh4g_close(tp, &cg); }
  { u8 *sk = sh4g_emit_bf_placeholder(tp);
    u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail);
    sh4g_patch_cond(sk, *tp); }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 0x80);                   /* T=1: IRQs enabled */
    sh4g_close(tp, &cg); }
  { u8 *sk = sh4g_emit_bt_placeholder(tp);
    u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, store_tail);
    sh4g_patch_cond(sk, *tp); }
  fail_ie = sh4g_emit_bra_placeholder(tp);

  sh4g_patch_cond(not_ie, *tp);
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
  /* Flush pending deferred scanlines via slow_tramp before any video IO write. */
  sh4g_const(tp, (u32)(uintptr_t)&cgba_pending_sl_count, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);   /* T=1: none pending */
    sh4g_close(tp, &cg); }
  { u8 *ok = sh4g_emit_bt_placeholder(tp);
    fail_pending = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_cond(ok, *tp); }
#endif

  sh4g_fastmem_io16_dispcnt_store(tp, video_store_tail);

  /* Unified direct video IO16 store for all side-effect-free registers <= BLDY:
   *   [0x008..0x026] BG0..3CNT, BG0..3HOFS/VOFS, BG2PA..PD  (off - 0x08 <= 0x1E)
   *   [0x030..0x036] BG3PA..PD                              (off - 0x30 <= 0x06)
   *   [0x040..0x054] WIN0H/V, WIN1H/V, WININ/OUT, MOSAIC, BLDCNT/ALPHA/Y
   *                                                         (off - 0x40 <= 0x14)
   * Excludes VCOUNT (0x006), BG2/3 reference points (0x028..0x02E, 0x038..0x03E),
   * and any address >= 0x04000056. */
  sh4g_const(tp, 0x04000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG0);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_ARG0);   /* R4 = off = addr - 0x04000000 */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -0x08, SH4_REG_T2);
    sh4_emit_mov_imm(&cg, 0x1E, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_T1);   /* T = (off - 0x08 <= 0x1E) */
    sh4g_close(tp, &cg); }
  hit_a = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -0x30, SH4_REG_T2);
    sh4_emit_mov_imm(&cg, 0x06, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_T1);   /* T = (off - 0x30 <= 0x06) */
    sh4g_close(tp, &cg); }
  hit_b = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_T2);
    sh4_emit_add_imm(&cg, -0x40, SH4_REG_T2);
    sh4_emit_mov_imm(&cg, 0x14, SH4_REG_T1);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_T1);   /* T = (off - 0x40 <= 0x14) */
    sh4g_close(tp, &cg); }
  fail_range = sh4g_emit_bf_placeholder(tp);

  sh4g_patch_cond(hit_a, *tp);
  sh4g_patch_cond(hit_b, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET); /* R0 = off */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)io_registers, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  { u8 *b = sh4g_emit_bra_placeholder(tp);
    sh4g_patch_bra(b, video_store_tail); }

  slow_label = *tp;
  sh4g_patch_bra(fail_odd, slow_label);
  sh4g_patch_bra(fail_ie, slow_label);
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
  sh4g_patch_bra(fail_pending, slow_label);
#endif
  sh4g_patch_cond(fail_range, slow_label);
  sh4g_far_jmp(tp, slow_tramp);
  sh4g_litseg_end_tail(tp);
  return entry;
}

/* Shared single-RAM load/store body for EWRAM and IWRAM: R0 holds in-region
 * offset; emits data base load, SMC bypass+tag check (stores), transfer,
 * writeback, and RTS with cycle debit. */
static inline void sh4g_fastmem_emit_ram_body(u8 **tp, int is_load, int kind,
  int wb, int compact8, const void *data_base, const void *code_end_ptr,
  const void *tag_base, int load_rd_after_smc, int debit_cycles,
  u8 **fail_out, int *nfail)
{
  sh4g_fm_lit_or_const(tp, (u32)(uintptr_t)data_base, SH4_REG_T2);

  if (is_load) {
    sh4g_fastmem_emit_load_xfer(tp, kind, compact8);
  } else {
    u8 *smc_ok = sh4g_fm_emit_smc_bypass(tp, code_end_ptr, SH4_REG_RET,
                                         SH4_REG_ARG3, SH4_REG_ARG0, 1, 0);
    sh4g_const(tp, (u32)(uintptr_t)tag_base, SH4_REG_ARG3);
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
    fail_out[(*nfail)++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_ok, *tp);
    if (load_rd_after_smc) {
      /* Load reg[rd] into R2 without disturbing R0 (offset) via R7 = &reg[rd]. */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_extu_b(&cg, SH4_REG_ARG1, SH4_REG_ARG3);
      sh4_emit_add_reg(&cg, SH4_REG_BASE, SH4_REG_ARG3);
      sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_T1);
      sh4g_close(tp, &cg);
    }
    sh4g_fastmem_emit_store_xfer(tp, kind, compact8, 1);
  }
  sh4g_fastmem_emit_wb(tp, wb);
  sh4g_fastmem_rts_debit(tp, debit_cycles);
}

static inline void sh4g_fastmem_emit_ewram(u8 **tp, int is_load, int kind,
  int align_mask, int wb, int compact8, const void *slow_tramp)
{
  u8 *not_ewram;
  u8 *ewram_fail[2];
  int nfail = 0, i;

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

  if (!is_load && !compact8) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 0xFF);
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
      sh4g_close(tp, &cg); }
    sh4g_const(tp, 0x3FFFFu, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* EWRAM mirror offset */
      sh4g_close(tp, &cg); }
  } else {
    sh4g_const(tp, 0x3FFFFu, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      if (!align_mask)
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* EWRAM mirror offset */
      sh4g_close(tp, &cg); }
  }

  sh4g_fastmem_emit_ram_body(tp, is_load, kind, wb, compact8,
    ewram, &cgba_ewram_code_end, ewram + 0x40000, 0,
    (kind == CGBA_FM_LOAD_W || kind == CGBA_FM_STORE_W) ? 6 : 3,
    ewram_fail, &nfail);

  if (nfail > 0) {
    for (i = 0; i < nfail; i++)
      sh4g_patch_cond(ewram_fail[i], *tp);
    sh4g_far_jmp(tp, slow_tramp);
  }
  sh4g_patch_cond(not_ewram, *tp);
  /* R0 still equals region on fallthrough. */
}

static inline u8 *sh4g_fastmem_emit_routine(u8 **tp, int fm)
{
  u8 *entry = *tp;
  int is_load = cgba_fm_is_load(fm);
  int kind = fm % CGBA_FM_BASE_COUNT;
  int align_mask = cgba_fm_align_mask(fm);
  int wb = fm >= CGBA_FM_WB;
  const int compact8 = !wb;
  const void *const slow_tramp =
    compact8 ? (is_load ? (const void *)sh4_fastmem_compact8_load_tramp
                        : (const void *)sh4_fastmem_compact8_tramp)
             : (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP;
  u8 *guards[12]; int ng = 0;
  u8 *fragment_entry = NULL, *fragment_join = NULL;
  u8 *fragment_far = NULL;
  int i;

  sh4g_litseg_begin();

  /* params -> R5 (passed in R5 for WB variants; compact8 non-WB sites pass
   * store data in R2 or receive load data in R2).
   * For all 6 hot kinds (LOAD_W/B/UH, STORE_W/UH/B), validate canonical IWRAM
   * + alignment via a single TST and compute `region = addr >> 24` lazily. */
  {
    int canonical_iwram = (kind == CGBA_FM_LOAD_W || kind == CGBA_FM_LOAD_B ||
                           kind == CGBA_FM_LOAD_UH ||
                           kind == CGBA_FM_STORE_W || kind == CGBA_FM_STORE_UH ||
                           kind == CGBA_FM_STORE_B);
    u8 *not_iwram;
    u8 *iwram_fail[2];
    int nfail = 0;

    /* ---- 1. Direct IWRAM (region 3) fast path: BOTH loads and stores ---- */
    if (canonical_iwram) {
      /* Single-TST check for BOTH (addr - 0x03000000) < 0x8000 AND guest
       * alignment: ((addr - 0x03000000) & (0xFFFF8000 | align_mask)) == 0.
       * Any non-canonical or unaligned IWRAM address takes not_iwram ->
       * region 3 -> slow_tramp. ILRAM literals avoid shift chains & D-misses. */
      sh4g_fm_lit_or_const(tp, 0x03000000u, SH4_REG_ARG0);
      sh4g_fm_lit_or_const(tp, 0xFFFF8000u | (u32)align_mask, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET); /* R0 = addr - 0x03000000 */
        sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_RET);   /* T = in IWRAM & aligned */
        sh4g_close(tp, &cg); }
      not_iwram = sh4g_emit_bf_placeholder(tp);
    } else {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_shlr16(&cg, SH4_REG_RET);
        sh4_emit_shlr8(&cg, SH4_REG_RET);            /* R0 = addr >> 24 */
        sh4_emit_cmpeq_imm(&cg, 3);                  /* T = IWRAM */
        sh4g_close(tp, &cg); }
      not_iwram = sh4g_emit_bf_placeholder(tp);

      if (align_mask) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, align_mask);           /* T = guest aligned */
        sh4g_close(tp, &cg);
        iwram_fail[nfail++] = sh4g_emit_bf_placeholder(tp);
      }

      if (!is_load && !compact8) {
        /* Preload reg[rd] into R2 while R0 is free (saves R0<->R7 shuffle). */
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
          sh4_emit_and_imm(&cg, 0xFF);
          sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
          sh4g_close(tp, &cg); }
        sh4g_const(tp, 0x7FFFu, SH4_REG_ARG0);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
          sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);
          sh4g_close(tp, &cg); }
      } else {
        sh4g_const(tp, 0x7FFFu, SH4_REG_ARG0);
        { sh4_codegen cg = sh4g_open(tp);
          if (!align_mask)
            sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
          sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);
          sh4g_close(tp, &cg); }
      }
    }

    sh4g_fastmem_emit_ram_body(tp, is_load, kind, wb, compact8,
      iwram + 0x8000, &cgba_iwram_code_end, iwram,
      canonical_iwram && !compact8, 1, iwram_fail, &nfail);

    if (nfail > 0) {
      /* On align/SMC failure, jump out to the slow tramp via a local bridge
       * (keeps all disp8 BF branches short while avoiding fallthrough). */
      for (i = 0; i < nfail; i++)
        sh4g_patch_cond(iwram_fail[i], *tp);
      sh4g_far_jmp(tp, slow_tramp);
    }
    sh4g_patch_cond(not_iwram, *tp);
    if (canonical_iwram) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);              /* R0 = addr >> 24 */
      sh4g_close(tp, &cg);
    }
    /* R0 equals region on the not_iwram path. */
  }

  /* ---- 2. Direct EWRAM (region 2) fast path: BOTH loads and stores ----
   * Deferred past VRAM for STORE_UH (29% VRAM vs 1% EWRAM) and past ROM8 for
   * LOAD_UH/LOAD_SH/LOAD_SB (35% ROM8 vs 9% EWRAM) to cut miss branches. */
  {
    int defer_ewram = (!is_load && kind == CGBA_FM_STORE_UH) ||
                      (is_load && (kind == CGBA_FM_LOAD_UH ||
                                   kind == CGBA_FM_LOAD_SH ||
                                   kind == CGBA_FM_LOAD_SB));
    if (!defer_ewram)
      sh4g_fastmem_emit_ewram(tp, is_load, kind, align_mask, wb, compact8, slow_tramp);
  }

  /* ---- 3. Store-only specialized paths (VRAM / Palette / IO16) ---- */
  if (!is_load) {
    /* 3a. Direct VRAM store (region 6) for word / halfword / byte.
     * On ARM7TDMI, unaligned STR/STRH mask off low address bits (align_mask),
     * and write_vram8 masks bit 0 and duplicates the low byte across the
     * 16-bit halfword (symmetric, so no byte-swap needed). */
    {
      u8 *not_vram, *not_mirror;
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
      u8 *vram_pending;
#endif
      u32 vram_mask = (kind == CGBA_FM_STORE_W) ? 0x1FFFCu : 0x1FFFEu;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 6);                  /* T = VRAM */
        sh4g_close(tp, &cg); }
      not_vram = sh4g_emit_bf_placeholder(tp);

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
      sh4g_const(tp, (u32)(uintptr_t)&cgba_pending_sl_count, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4g_close(tp, &cg); }
      vram_pending = sh4g_emit_bf_placeholder(tp);
#endif

      /* Preload reg[rd] into R2 while R0 is free (compact8 already passes R2). */
      if (!compact8) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0xFF);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
        sh4g_close(tp, &cg);
      }

      /* Aligned VRAM offset: addr & vram_mask; mirror 0x18000..0x1FFFF -> 0x10000..0x17FFF. */
      sh4g_const(tp, vram_mask, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET); /* R0 = addr & vram_mask */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, 0x18000u, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmphs(&cg, SH4_REG_ARG0, SH4_REG_RET); /* T = (off >= 0x18000) */
        sh4g_close(tp, &cg); }
      not_mirror = sh4g_emit_bf_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, -128, SH4_REG_ARG0);
        sh4_emit_shll8(&cg, SH4_REG_ARG0);           /* -0x8000 */
        sh4_emit_add_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(not_mirror, *tp);

#ifdef CGBA_VRAM_DIRTY_ACTIVE
      /* R0 is 0x00000..0x0FFFF (BG) or 0x10000..0x17FFF (OBJ):
       * (R0 >> 16) + 1 gives CGBA_VRAM_DIRTY_BG (1) or CGBA_VRAM_DIRTY_OBJ (2). */
      sh4g_vec_load(tp, SH4G_VEC_vram_dirty, SH4_REG_ARG3);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T2);
        sh4_emit_shlr16(&cg, SH4_REG_T2);
        sh4_emit_add_imm(&cg, 1, SH4_REG_T2);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
        sh4_emit_or(&cg, SH4_REG_T2, SH4_REG_ARG0);
        sh4_emit_mov_l_store(&cg, SH4_REG_ARG0, SH4_REG_ARG3);
        sh4g_close(tp, &cg); }
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
      { u8 *skip_sb;
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_shlr(&cg, SH4_REG_T2);             /* T = 1 if BG, 0 if OBJ */
          sh4g_close(tp, &cg); }
        skip_sb = sh4g_emit_bf_placeholder(tp);
        sh4g_const(tp, (u32)(uintptr_t)&cgba_vram_sb_dirty, SH4_REG_ARG3);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T2);
          sh4_emit_mov_imm(&cg, -11, SH4_REG_ARG0);
          sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_T2);
          sh4_emit_mov_imm(&cg, 1, SH4_REG_ARG0);
          sh4_emit_shld(&cg, SH4_REG_T2, SH4_REG_ARG0);
          sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_T2);
          sh4_emit_or(&cg, SH4_REG_ARG0, SH4_REG_T2);
          sh4_emit_mov_l_store(&cg, SH4_REG_T2, SH4_REG_ARG3);
          sh4g_close(tp, &cg); }
        sh4g_patch_cond(skip_sb, *tp); }
#endif
#endif
      sh4g_const(tp, (u32)(uintptr_t)vram, SH4_REG_T2);
      if (kind == CGBA_FM_STORE_B) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_ARG0);
        sh4_emit_shll8(&cg, SH4_REG_ARG0);
        sh4_emit_or(&cg, SH4_REG_ARG0, SH4_REG_T1);
        sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
        sh4g_close(tp, &cg);
      } else {
        sh4g_fastmem_emit_store_xfer(tp, kind, compact8, 1);
      }
      sh4g_fastmem_emit_wb(tp, wb);
      sh4g_fastmem_rts_debit(tp, kind == CGBA_FM_STORE_W ? 2 : 1);

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
      sh4g_patch_cond(vram_pending, *tp);
      sh4g_far_jmp(tp, slow_tramp);
#endif
      sh4g_patch_cond(not_vram, *tp);
      /* R0 still equals region. */
    }
    /* Byte stores only hit IWRAM/EWRAM/VRAM natively; everything else stays on C. */
    if (kind == CGBA_FM_STORE_B) {
      sh4g_far_jmp(tp, slow_tramp);
      sh4g_litseg_end_tail(tp);
      return entry;
    }
    if (kind == CGBA_FM_STORE_UH)
      sh4g_fastmem_emit_ewram(tp, is_load, kind, align_mask, wb, compact8, slow_tramp);

    /* 3b. Direct Palette RAM store (region 5) for word / halfword. */
    {
      u8 *not_pal;
      u8 *pal_fail[3];
      int npf = 0;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 5);                  /* T = palette RAM */
        sh4g_close(tp, &cg); }
      not_pal = sh4g_emit_bf_placeholder(tp);

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
      sh4g_const(tp, (u32)(uintptr_t)&cgba_pending_sl_count, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4g_close(tp, &cg); }
      pal_fail[npf++] = sh4g_emit_bf_placeholder(tp);
#endif

      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_tst_imm(&cg, align_mask);           /* T = guest aligned */
        sh4g_close(tp, &cg); }
      pal_fail[npf++] = sh4g_emit_bf_placeholder(tp);

      sh4g_const(tp, 0x03FFu, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG3);
        sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_ARG3); /* R7 = palette offset (4-aligned -> <= 0x3FC) */
        sh4g_close(tp, &cg); }

      if (!compact8) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_and_imm(&cg, 0xFF);
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_T1);
        sh4g_close(tp, &cg);
      }

      sh4g_const(tp, (u32)(uintptr_t)palette_ram, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
        if (kind == CGBA_FM_STORE_W) {
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_ARG0);
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
#if defined(CGBA_PALETTE_DIRTY_OBJ_ONLY) && \
    (CGBA_PALETTE_DIRTY_OBJ_ONLY != CGBA_PALETTE_DIRTY_ACTIVE)
      /* R7 is palette offset (0..0x3FE): BG (<0x200) sets all dirty bits,
       * OBJ (>=0x200) ORs in only OBJ-visible bits (including FOLDED_OBJ) to
       * avoid spurious BG backdrop/folded/signed palette rebuilds. */
      { u8 *pal_is_bg, *pal_dirty_done;
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
          sh4_emit_shlr8(&cg, SH4_REG_RET);
          sh4_emit_tst_imm(&cg, 2);                   /* T = (off < 0x200) */
          sh4g_close(tp, &cg); }
        pal_is_bg = sh4g_emit_bt_placeholder(tp);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_RET);
          sh4_emit_or_imm(&cg, CGBA_PALETTE_DIRTY_OBJ_ONLY);
          sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_T2);
          sh4g_close(tp, &cg); }
        pal_dirty_done = sh4g_emit_bra_placeholder(tp);
        sh4g_patch_cond(pal_is_bg, *tp);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_mov_imm(&cg, CGBA_PALETTE_DIRTY_ACTIVE, SH4_REG_RET);
          sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_T2);
          sh4g_close(tp, &cg); }
        sh4g_patch_bra(pal_dirty_done, *tp); }
#else
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, CGBA_PALETTE_DIRTY_ACTIVE, SH4_REG_RET);
        sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_T2);
        sh4g_close(tp, &cg); }
#endif
#endif
      sh4g_fastmem_emit_wb(tp, wb);
      sh4g_fastmem_rts_debit(tp, kind == CGBA_FM_STORE_W ? 2 : 1);

      for (i = 0; i < npf; i++)
        sh4g_patch_cond(pal_fail[i], *tp);
      sh4g_far_jmp(tp, slow_tramp);
      sh4g_patch_cond(not_pal, *tp);
      /* R0 still equals region. */
    }

    /* 3c. Halfword IO-register fast path (region 4, non-writeback only). */
    if (kind == CGBA_FM_STORE_UH && !wb && cgba_sh4_fastmem_io16_store) {
      u8 *not_io;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmpeq_imm(&cg, 4);                  /* T = IO */
        sh4g_close(tp, &cg); }
      not_io = sh4g_emit_bf_placeholder(tp);
      sh4g_far_jmp(tp, cgba_sh4_fastmem_io16_store);
      sh4g_patch_cond(not_io, *tp);
      sh4g_far_jmp(tp, slow_tramp);
      sh4g_litseg_end_tail(tp);
      return entry;
    }

    /* STORE_W non-RAM/VRAM/Palette -> slow C helper. */
    sh4g_far_jmp(tp, slow_tramp);
    sh4g_litseg_end_tail(tp);
    return entry;
  }

  /* ---- 3d. Direct fragmented-ROM fast path (region 8, offset >= 0x8000) ----
   * On fragmented NOR flash (fx-CG100/CG50/calcemu), >95% of 32 KiB ROM pages
   * are NULL in memory_map_read[] and live in the 4 KiB fragment table.
   * cgba_sh4_fastmem_init() re-emits when the ROM changes, so the fragment
   * table pointer and gamepak_size are embedded directly as constants (saving
   * 2 memory loads and a NULL-table check on every ROM8 read). */
  if (cgba_gamepak_fragment_blocks != NULL && gamepak_size > 0x8000u) {
    u32 rom8_limit =
      (gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u;
    u8 *not_rom8;
    u8 *not_rom8_reload[2];
    u8 *rom8_align_fail = NULL;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 8);                    /* T = Game Pak WS0 (0x08) */
      sh4g_close(tp, &cg); }
    not_rom8 = sh4g_emit_bf_placeholder(tp);

    /* Unified check: (addr - 0x08008000) < rom8_limit verifies BOTH
     * block_idx >= 8 AND offset < gamepak_size in one branch. */
    { int nr8 = 0;
      sh4g_emit_rom8_page_lookup(tp, rom8_limit,
                                 cgba_gamepak_fragment_blocks + 8,
                                 SH4_REG_T0, not_rom8_reload, &nr8); }

    if (align_mask) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_RET);
      sh4_emit_or(&cg, SH4_REG_T0, SH4_REG_RET);     /* guest | host ptr */
      sh4_emit_tst_imm(&cg, align_mask);             /* T = both aligned */
      sh4g_close(tp, &cg);
      rom8_align_fail = sh4g_emit_bf_placeholder(tp);
    }

    sh4g_const(tp, 0x0FFFu, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* R0 = addr & 0x0FFF */
      sh4g_close(tp, &cg); }
    sh4g_fastmem_emit_load_xfer(tp, kind, compact8);
    sh4g_fastmem_emit_wb(tp, wb);
    /* reload_timing_info() calls cgba_sh4_fastmem_init() whenever WAITCNT
     * changes ws_cyc_nseq[8][0/1], so the debit is a baked constant here. */
    sh4g_fastmem_rts_debit(tp,
      (int)ws_cyc_nseq[8][kind == CGBA_FM_LOAD_W ? 1 : 0]);

    if (rom8_align_fail) {
      sh4g_patch_cond(rom8_align_fail, *tp);
      sh4g_far_jmp(tp, slow_tramp);
    }

    sh4g_patch_cond(not_rom8_reload[0], *tp);
    sh4g_patch_cond(not_rom8_reload[1], *tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 8, SH4_REG_RET);
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(not_rom8, *tp);
    /* R0 == region on fallthrough. */
  }
  if (kind == CGBA_FM_LOAD_UH || kind == CGBA_FM_LOAD_SH ||
      kind == CGBA_FM_LOAD_SB)
    sh4g_fastmem_emit_ewram(tp, is_load, kind, align_mask, wb, compact8, slow_tramp);

  /* ---- 4. Remaining LOAD regions (IO / Video / Game Pak 4..12) ----
   * R0 == region (not 2, not 3). Save into R7 (untouched across section 4)
   * and restrict to 4..12 (BIOS 0..1 and backup/unmapped >= 13 stay on C). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG3);
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

  sh4g_fastmem_emit_load_xfer(tp, kind, compact8);
  sh4g_fastmem_emit_wb(tp, wb);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    if (kind == CGBA_FM_LOAD_W)
      sh4_emit_add_imm(&cg, 1, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_nseq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_ARG0);
    sh4_emit_extu_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_rts(&cg);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_CYCLES); /* delay slot */
    sh4g_close(tp, &cg); }

  /* Fragment-entry bridge + common guard failure. */
  sh4g_patch_cond(fragment_entry, *tp);
  fragment_far = sh4g_emit_bra_placeholder(tp);

  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, slow_tramp);

  /* Out-of-line 4 KiB NOR fragment lookup (region is already proven < 13
   * and lives in R7, so only region >= 8 needs testing). */
  {
    u8 *miss[4];
    u8 *fragment_ok;
    int nmiss = 0;

    sh4g_patch_bra(fragment_far, *tp);

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
      sh4_emit_mov_imm(&cg, 8, SH4_REG_T1);
      sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);  /* T = region >= 8 */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

    sh4g_const(tp, 0x01FFFFFFu, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);    /* R0 = logical offset */
      sh4g_close(tp, &cg); }
    if (cgba_gamepak_fragment_blocks != NULL && gamepak_size > 0u) {
      /* Matches section 3d: cgba_sh4_fastmem_init() re-emits on ROM change. */
      sh4g_const(tp, gamepak_size, SH4_REG_T1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET); /* T = offset >= size */
        sh4g_close(tp, &cg); }
      miss[nmiss++] = sh4g_emit_bt_placeholder(tp);
      sh4g_const(tp, (u32)(uintptr_t)cgba_gamepak_fragment_blocks, SH4_REG_T2);
    } else {
      sh4g_const(tp, (u32)(uintptr_t)&gamepak_size, SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
        sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET); /* T = offset >= size */
        sh4g_close(tp, &cg); }
      miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

      sh4g_const(tp, (u32)(uintptr_t)&cgba_gamepak_fragment_blocks,
                 SH4_REG_T2);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T2);
        sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);   /* T = table == NULL */
        sh4g_close(tp, &cg); }
      miss[nmiss++] = sh4g_emit_bt_placeholder(tp);
    }
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
    sh4g_far_jmp(tp, slow_tramp);
    sh4g_patch_bra(fragment_ok, fragment_join);
  }

  sh4g_litseg_end_tail(tp);
  return entry;
}

/* ---- shared block-transfer routine (cold/generic + ILRAM contiguous) ----
 * Contract: R1 = A (word-aligned LOW address of the ascending run), R6 = the
 * writeback base value (wb variants), PR = site tuple (or R5 = params under
 * OOL descriptors). Never clobbers R12 (pinned guest ARM r1). Guard set
 * mirrors sh4_arm_block_emit.h (the safe superset for Thumb too: region-0
 * excluded, unmapped/straddling/misaligned runs and SMC-tagged store targets
 * fall to the C helper via the trampoline, BEFORE any architectural write). */
static inline u8 *sh4g_fastmem_emit_block_cold(u8 **tp, int fm)
{
  u8 *entry = *tp;
  int is_load = (fm == CGBA_FMB_LDM || fm == CGBA_FMB_LDM_WB);
  int wb = (fm >= CGBA_FMB_LDM_WB);
  u8 *guards[10]; int ng = 0;
  u8 *to_xfer[3]; int nxfer = 0;
  u8 *not_iwram, *loop_top, *skip_ph;
  int i;

  sh4g_litseg_begin();

  /* Params already live in R5 under OOL descriptors.
   * R2 = count (params bits 16..20, 1..16). */
  { sh4_codegen cg = sh4g_open(tp);
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

  /* ---- 1. Direct canonical IWRAM fast path (stack PUSH/POP/LDM/STM) ----
   * Validate canonical IWRAM + no 32 KiB straddle in a single compare:
   *   (u32)(A - 0x03000000) <= (0x8000 - count * 4)
   * On miss (EWRAM/VRAM/ROM/mirror), region = A >> 24 is computed lazily. */
  sh4g_const(tp, 0x03000000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);    /* R0 = A - 0x03000000 */
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_shll2(&cg, SH4_REG_T2);                 /* R3 = count * 4 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x8000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sub(&cg, SH4_REG_T2, SH4_REG_ARG0);     /* R4 = 0x8000 - count*4 */
    sh4_emit_cmphi(&cg, SH4_REG_ARG0, SH4_REG_RET);  /* T = R0 > 0x8000 - c*4 */
    sh4g_close(tp, &cg); }
  not_iwram = sh4g_emit_bt_placeholder(tp);

  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);         /* R3 = host_ptr */
  if (!is_load) {
    u8 *smc_clean = sh4g_fm_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                            SH4_REG_RET, SH4_REG_ARG3,
                                            SH4_REG_ARG3, 0, 0);
    /* IWRAM SMC tag scan: OR-accumulate all count tag words (reuse R1 as loop
     * counter now that host_ptr is in R3, keeping R2 = count intact). */
    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG3);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG3); /* R7 = iwram + off */
      sh4_emit_mov_imm(&cg, 0, SH4_REG_RET);            /* R0 = acc = 0 */
      sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T0);    /* R1 = count */
      sh4g_close(tp, &cg); }
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_inc(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
      sh4_emit_or(&cg, SH4_REG_ARG0, SH4_REG_RET);      /* acc |= tag */
      sh4_emit_dt(&cg, SH4_REG_T0);                     /* R1--; T = zero */
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); }  /* BF loop_top */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);      /* T = all tags clear */
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_clean, *tp);
  }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, 3, SH4_REG_T0);            /* R1 = region 3 (IWRAM) */
    sh4g_close(tp, &cg); }
  to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);

  /* Early guard bridge (alignment / IWRAM SMC): keeps disp8 branches from
   * section 1 well under +-254 bytes regardless of section 2 size. */
  if (ng > 0) {
    for (i = 0; i < ng; i++)
      sh4g_patch_cond(guards[i], *tp);
    ng = 0;
    sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);
  }

  /* ---- 2. Non-IWRAM path (EWRAM / VRAM / ROM) ---- */
  sh4g_patch_cond(not_iwram, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  /* R0 == region, R1 == A (IWRAM mirrors with region==3 fall to slow path on store). */
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
    { sh4_codegen cg2 = sh4g_open(tp);
      sh4_emit_shlr16(&cg2, SH4_REG_T0);
      sh4_emit_shlr8(&cg2, SH4_REG_T0);              /* R1 = region */
      sh4g_close(tp, &cg2); }
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);
  } else {
    /* Non-IWRAM store is either VRAM (region 6) or EWRAM (region 2). */
    u8 *ewram_smc;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 6);                    /* T = VRAM */
      sh4g_close(tp, &cg); }
    ewram_smc = sh4g_emit_bf_placeholder(tp);

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
    sh4g_const(tp, (u32)(uintptr_t)&cgba_pending_sl_count, SH4_REG_ARG0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4g_close(tp, &cg); }
    guards[ng++] = sh4g_emit_bf_placeholder(tp);
#endif

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
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
    /* Exact BG screenblock bitmask: transfer is within one 32 KiB page and
     * count <= 16 (<= 64 B), so [start_off .. end_off] touches at most two
     * adjacent 2 KiB screenblocks: (1u << sb0) | (1u << sb1). Preserves
     * R1 (A), R2 (count), R3 (host_ptr), R5 (params), R6 (wb_val). */
    { u8 *skip_sb;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_shlr(&cg, SH4_REG_RET);              /* T = 1 if BG, 0 if OBJ */
        sh4g_close(tp, &cg); }
      skip_sb = sh4g_emit_bf_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_extu_w(&cg, SH4_REG_T0, SH4_REG_ARG3);   /* R7 = start_off */
        sh4_emit_mov_imm(&cg, -11, SH4_REG_RET);
        sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_ARG3);    /* R7 = sb0 (0..31) */
        sh4_emit_mov_imm(&cg, 1, SH4_REG_ARG0);
        sh4_emit_shld(&cg, SH4_REG_ARG3, SH4_REG_ARG0);   /* R4 = 1u << sb0 */
        sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
        sh4_emit_shll2(&cg, SH4_REG_RET);                 /* R0 = count * 4 */
        sh4_emit_add_imm(&cg, -1, SH4_REG_RET);           /* R0 = count*4 - 1 */
        sh4_emit_extu_w(&cg, SH4_REG_T0, SH4_REG_ARG3);   /* R7 = start_off */
        sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG3); /* R7 = end_off */
        sh4_emit_mov_imm(&cg, -11, SH4_REG_RET);
        sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_ARG3);    /* R7 = sb1 (0..31) */
        sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
        sh4_emit_shld(&cg, SH4_REG_ARG3, SH4_REG_RET);    /* R0 = 1u << sb1 */
        sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_ARG0);      /* R4 = mask */
        sh4g_close(tp, &cg); }
      sh4g_const(tp, (u32)(uintptr_t)&cgba_vram_sb_dirty, SH4_REG_ARG3);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG3, SH4_REG_RET);
        sh4_emit_or(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_ARG3);
        sh4g_close(tp, &cg); }
      sh4g_patch_cond(skip_sb, *tp); }
#endif
#endif
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 6, SH4_REG_T0);          /* R1 = region 6 (VRAM) */
      sh4g_close(tp, &cg); }
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);

    /* EWRAM SMC tag scan: fast-bypass via cgba_ewram_code_end, else scan tags
     * at host_ptr + 0x40000 (4 << 16). Reuse R1 as loop counter so R2 = count
     * stays intact for cycle charge. */
    sh4g_patch_cond(ewram_smc, *tp);
    { u8 *ew_smc_clean;
      sh4g_const(tp, 0x3FFFFu, SH4_REG_ARG0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET); /* R0 = A & 0x3FFFF */
        sh4g_close(tp, &cg); }
      ew_smc_clean = sh4g_fm_emit_smc_bypass(tp, &cgba_ewram_code_end,
                                             SH4_REG_RET, SH4_REG_ARG3,
                                             SH4_REG_ARG0, 0, 0);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_imm(&cg, 4, SH4_REG_ARG3);
        sh4_emit_shll16(&cg, SH4_REG_ARG3);            /* 0x40000 */
        sh4_emit_add_reg(&cg, SH4_REG_T2, SH4_REG_ARG3); /* R7 = tag_ptr */
        sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = count */
        sh4g_close(tp, &cg); }
      loop_top = *tp;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_l_load_inc(&cg, SH4_REG_ARG3, SH4_REG_ARG0);
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = tags clear */
        sh4g_close(tp, &cg); }
      guards[ng++] = sh4g_emit_bf_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_dt(&cg, SH4_REG_T0);                  /* R1--; T = zero */
        sh4g_close(tp, &cg); }
      { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
        sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */
      sh4g_patch_cond(ew_smc_clean, *tp); }
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_imm(&cg, 2, SH4_REG_T0);          /* R1 = region 2 (EWRAM) */
      sh4g_close(tp, &cg); }
    to_xfer[nxfer++] = sh4g_emit_bra_placeholder(tp);
  }

  /* Common guard-failure bridge placed mid-routine so every BT/BF above stays
   * well within +-254 bytes. */
  for (i = 0; i < ng; i++)
    sh4g_patch_cond(guards[i], *tp);
  sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);

  /* ---- 3. Shared transfer loop + writeback + cycle charge ----
   * R3 = host_ptr, R5 = params, R6 = wb_val, R1 = (region << 8) | count.
   * Handles both contiguous (bit 21 == 1) and generic bitmask (bit 21 == 0). */
  for (i = 0; i < nxfer; i++)
    sh4g_patch_bra(to_xfer[i], *tp);
  {
    u8 *not_contig, *contig_done, *no_lr_adj, *no_lr_tail;
    u8 *enter_nib_bt, *skip_nib;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_shll8(&cg, SH4_REG_T0);                 /* R1 = region << 8 */
      sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_T0);        /* R1 = (region<<8)|count */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, 0x20);                     /* T = (bit21 == 0) */
      sh4g_close(tp, &cg); }
    not_contig = sh4g_emit_bt_placeholder(tp);

    /* Contiguous tight dt loop (R2 = count, R0 = params >> 16; R1 preserved). */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst_imm(&cg, 0x40);                     /* T = (has_lr == 0) */
      sh4g_close(tp, &cg); }
    no_lr_adj = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_imm(&cg, -1, SH4_REG_T1);           /* R2 = n_contig */
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(no_lr_adj, *tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_extu_b(&cg, SH4_REG_ARG1, SH4_REG_RET); /* R0 = first * 4 */
      sh4g_close(tp, &cg); }
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      if (is_load) {
        sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG3);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG3, SH4_REG_BASE);
      } else {
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_ARG3);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store(&cg, SH4_REG_ARG3, SH4_REG_T2);
        sh4_emit_add_imm(&cg, 4, SH4_REG_T2);
      }
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      sh4_emit_dt(&cg, SH4_REG_T1);
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_tst_imm(&cg, 0x40);                     /* T = (has_lr == 0) */
      sh4g_close(tp, &cg); }
    no_lr_tail = sh4g_emit_bt_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      if (is_load) {
        sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG3);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store_disp(&cg, SH4_REG_ARG3, SH4_REG_BASE, 14);
      } else {
        sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_ARG3, 14);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store(&cg, SH4_REG_ARG3, SH4_REG_T2);
      }
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(no_lr_tail, *tp);
    contig_done = sh4g_emit_bra_placeholder(tp);

    /* Generic non-contiguous bitmask loop (bit 21 == 0). */
    sh4g_patch_cond(not_contig, *tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_extu_w(&cg, SH4_REG_ARG1, SH4_REG_T1);  /* R2 = rlist (!= 0) */
      sh4_emit_mov_imm(&cg, 0, SH4_REG_RET);           /* R0 = greg_off */
      sh4_emit_mov_imm(&cg, 0x0F, SH4_REG_ARG0);       /* R4 = 0x0F nibble mask */
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_T1);     /* T = (rlist & 0xF)==0 */
      sh4g_close(tp, &cg); }
    enter_nib_bt = sh4g_emit_bt_placeholder(tp);
    loop_top = *tp;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_shlr(&cg, SH4_REG_T1);                  /* T = bit0 */
      sh4g_close(tp, &cg); }
    skip_ph = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      if (is_load) {
        sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG3);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG3, SH4_REG_BASE);
      } else {
        sh4_emit_mov_l_load_r0(&cg, SH4_REG_BASE, SH4_REG_ARG3);
        sh4_emit_bswap32(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_mov_l_store(&cg, SH4_REG_ARG3, SH4_REG_T2);
        sh4_emit_add_imm(&cg, 4, SH4_REG_T2);
      }
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T1);       /* list empty? */
      sh4g_close(tp, &cg); }
    { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
      sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); } /* BF loop_top */

    sh4g_patch_bra(contig_done, *tp);

    /* ---- 4. Writeback (precomputed base in R6, params in R5) ---- */
    if (wb) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);                /* R0 = wb byte-off */
      sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
      sh4g_close(tp, &cg);
    }

    /* ---- 5. Cycle charge & RTS (R1 = (region << 8) | count) ---- */
    { u8 *not_iwram_cyc;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
        sh4_emit_shlr8(&cg, SH4_REG_RET);               /* R0 = region */
        sh4_emit_extu_b(&cg, SH4_REG_T0, SH4_REG_T0);   /* R1 = count */
        sh4_emit_cmpeq_imm(&cg, 3);                     /* T = IWRAM */
        sh4g_close(tp, &cg); }
      not_iwram_cyc = sh4g_emit_bf_placeholder(tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_rts(&cg);
        sh4_emit_sub(&cg, SH4_REG_T0, SH4_REG_CYCLES);  /* IWRAM: count * 1 */
        sh4g_close(tp, &cg); }

      /* Out-of-line skip_one / skip_nib (placed right after IWRAM RTS so all
       * disp8 branches from/to loop_top stay tiny). */
      sh4g_patch_cond(skip_ph, *tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      skip_nib = *tp;
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* T = (rlist & 0xF)==0 */
        sh4g_close(tp, &cg); }
      { long d = ((long)loop_top - ((long)*tp + 4)) / 2;
        sh4g_u16(tp, (uint16_t)(0x8B00 | (d & 0xFF))); }/* BF loop_top */
      sh4g_patch_cond(enter_nib_bt, *tp);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_shlr2(&cg, SH4_REG_T1);
        sh4_emit_shlr2(&cg, SH4_REG_T1);
        sh4_emit_add_imm(&cg, 16, SH4_REG_RET);         /* skip 4 regs */
        sh4g_close(tp, &cg); }
      { long d = ((long)skip_nib - ((long)*tp + 4)) / 2;
        sh4g_u16(tp, (uint16_t)(0xA000 | (d & 0x0FFF)));/* BRA skip_nib */
        sh4g_u16(tp, 0x0009); }

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
          sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_T0);
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
          sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
          sh4_emit_shll(&cg, SH4_REG_RET);              /* 2*count */
          sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_RET); /* 3*count */
          sh4_emit_shll(&cg, SH4_REG_RET);              /* 6*count */
          sh4_emit_rts(&cg);
          sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_CYCLES);
          sh4g_close(tp, &cg); }
        sh4g_patch_cond(vram_cyc, *tp);
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_shll(&cg, SH4_REG_T0);               /* 2*count */
          sh4_emit_rts(&cg);
          sh4_emit_sub(&cg, SH4_REG_T0, SH4_REG_CYCLES);
          sh4g_close(tp, &cg); }
      }
    }
  }

  sh4g_litseg_end_tail(tp);
  return entry;
}

/* Hot ILRAM block-transfer entry: branchless 8-step unrolled ladder (5 insns/reg)
 * for contiguous canonical-IWRAM PUSH/POP/LDM/STM with cgba_iwram_code_end SMC
 * bypass; non-contiguous, non-IWRAM, or low-IWRAM stores tail-jump to the cold
 * routine in .cgba.highbss. Uses R3 (then R1 after guards) for count*4 so R12
 * (pinned guest ARM r1) is never touched. */
static inline u8 *sh4g_fastmem_emit_block_routine(u8 **tp, int fm)
{
  u8 *cold = cgba_sh4_fastmem_block_cold[fm - CGBA_FMB_LDM];
  if (!cold)
    return sh4g_fastmem_emit_block_cold(tp, fm);

  u8 *entry = *tp;
  int is_load = (fm == CGBA_FMB_LDM || fm == CGBA_FMB_LDM_WB);
  int wb = (fm >= CGBA_FMB_LDM_WB);
  u8 *to_cold[4]; int ncold = 0;
  u8 *to_slow, *no_lr_adj, *skip_lr;
  int k;

  sh4g_litseg_begin();

  /* R5 = params (passed in R5 under OOL descriptors); check bit 21
   * (contiguous); R2 = count; R3 = count * 4 (R1 stays live for cold tail). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 0x20);                       /* T = (bit21 == 0) */
    sh4g_close(tp, &cg); }
  to_cold[ncold++] = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_and_imm(&cg, 0x1F);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);    /* R2 = count */
    sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_shll2(&cg, SH4_REG_T2);                   /* R3 = count * 4 */
    sh4g_close(tp, &cg); }

  /* Canonical IWRAM + no-straddle + alignment check (R1 = A untouched). */
  sh4g_fm_lit_or_const(tp, 0x03000000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);      /* R0 = A - 0x03000000 */
    sh4g_close(tp, &cg); }
  sh4g_fm_lit_or_const(tp, 0x8000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sub(&cg, SH4_REG_T2, SH4_REG_ARG0);       /* R4 = 0x8000 - c*4 */
    sh4_emit_cmphi(&cg, SH4_REG_ARG0, SH4_REG_RET);    /* T = non-IWRAM/straddle */
    sh4g_close(tp, &cg); }
  to_cold[ncold++] = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, 3);                          /* T = word aligned */
    sh4g_close(tp, &cg); }
  to_slow = sh4g_emit_bf_placeholder(tp);

  if (!is_load) {
    /* Fast SMC bypass: if off >= cgba_iwram_code_end, all tags are guaranteed 0. */
    to_cold[ncold++] = sh4g_fm_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                               SH4_REG_RET, SH4_REG_ARG3,
                                               SH4_REG_ARG3, 0, 1);
  }

  /* Past all guards: R1 (A) is dead, so move count*4 from R3 -> R1 to free R3
   * for host_ptr without touching R12 (pinned guest r1). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T2, SH4_REG_T0);     /* R1 = count * 4 */
    sh4g_close(tp, &cg); }
  /* R3 = iwram_data + off (ILRAM literal — avoids vec_table RAM load). */
  sh4g_const(tp, (u32)(uintptr_t)(iwram + 0x8000), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);    /* R3 = host_ptr */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 0x40);                       /* T = (has_lr == 0) */
    sh4g_close(tp, &cg); }
  no_lr_adj = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, -4, SH4_REG_T0);             /* R1 = n_contig * 4 */
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(no_lr_adj, *tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_extu_b(&cg, SH4_REG_ARG1, SH4_REG_ARG3);  /* R7 = first * 4 */
    sh4_emit_add_reg(&cg, SH4_REG_BASE, SH4_REG_ARG3); /* R7 = &reg[first] */
    sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_ARG3);   /* + n_contig*4 */
    if (!is_load) {
      sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);   /* R3 += n_contig*4 */
      sh4_emit_add_imm(&cg, -32, SH4_REG_ARG3);        /* R7 += (n_contig-8)*4 */
    }
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 0xFF);                       /* R0 = (8-n_contig)*10 */
    sh4_emit_braf(&cg, SH4_REG_RET);                   /* PC+4+R0 -> ladder */
    if (is_load)
      sh4_emit_add_imm(&cg, -32, SH4_REG_ARG3);        /* delay: R7 -= 32 */
    else
      sh4_emit_add_imm(&cg, -32, SH4_REG_T2);          /* delay: R3 -= 32 */
    /* 8-step unrolled ladder: EXACTLY 5 insns (10 bytes) per step! */
    for (k = 0; k < 8; k++) {
      if (is_load) {
        sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG0);
        sh4_emit_bswap32(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_mov_l_store_disp(&cg, SH4_REG_ARG0, SH4_REG_ARG3, (unsigned)k);
      } else {
        sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG3, SH4_REG_ARG0, (unsigned)k);
        sh4_emit_bswap32(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
        sh4_emit_mov_l_store_disp(&cg, SH4_REG_ARG0, SH4_REG_T2, (unsigned)k);
      }
    }
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 0x40);                       /* T = (has_lr == 0) */
    sh4g_close(tp, &cg); }
  skip_lr = sh4g_emit_bt_placeholder(tp);
  { sh4_codegen cg = sh4g_open(tp);
    if (is_load) {
      sh4_emit_mov_l_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG0);
      sh4_emit_bswap32(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_mov_l_store_disp(&cg, SH4_REG_ARG0, SH4_REG_BASE, 14);
    } else {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_ARG0, 14);
      sh4_emit_bswap32(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_mov_l_store_disp(&cg, SH4_REG_ARG0, SH4_REG_T2, 8);
    }
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(skip_lr, *tp);

  if (wb) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);                  /* R0 = wb_rn * 4 */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG2, SH4_REG_BASE);
    sh4g_close(tp, &cg);
  }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_rts(&cg);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);     /* IWRAM: count * 1 */
    sh4g_close(tp, &cg); }

  for (k = 0; k < ncold; k++)
    sh4g_patch_cond(to_cold[k], *tp);
  sh4g_far_jmp(tp, cold);

  sh4g_patch_cond(to_slow, *tp);
  sh4g_far_jmp(tp, (const void *)CGBA_SH4_FASTMEM_SLOW_TRAMP);

  sh4g_litseg_end_tail(tp);
  return entry;
}

/* ---- shared ILRAM fused `POP {rX}; BX rX` and standalone `BX rX` tail ----
 * Entry 1 (`cgba_sh4_pop1_bx_routine`):
 *   In: R5 = rX * 4 (0..28), PR -> 4-byte-aligned `.long pc` (used only by
 *   sh4_pop1_bx_slow on non-IWRAM/unaligned SP). Pops 1 word from canonical
 *   aligned IWRAM SP, writes reg[rX] (and syncs pinned R11 if rX==0, R12 if
 *   rX==1), advances SP += 4, debits 1 cycle, and falls through to `bx_tail`.
 * Entry 2 (`cgba_sh4_thumb_bx_routine` = entry + *bx_tail_off):
 *   In: R4 = target PC (bit 0 = 1 Thumb, 0 ARM), CPSR.T == 1. Caller already
 *   debited accumulated block cycles (minus BX's cancelled fetch). Charges
 *   Thumb target sequential fetch, exits via sh4_block_exit on R13 <= 0, and
 *   dispatches via sh4_indirect_branch_dual[_thumb_current]. */
static inline u8 *sh4g_fastmem_emit_pop1_bx(u8 **tp, size_t *bx_tail_off)
{
  u8 *entry = *tp;
  u8 *pop_slow;
  u8 *bx_arm, *bx_thumb_exhausted, *bx_arm_exhausted;

  sh4g_litseg_begin();

  /* Entry 1: canonical aligned IWRAM POP {rX}. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, SH4_GREG_SP, SH4_REG_T0);   /* R1 = SP */
    sh4g_close(tp, &cg); }
  sh4g_fm_lit_or_const(tp, 0x03000000u, SH4_REG_ARG0);
  sh4g_fm_lit_or_const(tp, 0xFFFF8003u, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);       /* R0 = SP - 0x03000000 */
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_RET);         /* T = in IWRAM & aligned */
    sh4g_close(tp, &cg); }
  pop_slow = sh4g_emit_bf_placeholder(tp);

  sh4g_fm_lit_or_const(tp, (u32)(uintptr_t)(iwram + 0x8000), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_ARG0);
    sh4_emit_bswap32(&cg, SH4_REG_ARG0, SH4_REG_ARG0);   /* R4 = popped word (target) */
    sh4_emit_add_imm(&cg, 4, SH4_REG_T0);               /* R1 = SP + 4 */
    sh4_emit_store_greg(&cg, SH4_REG_T0, SH4_GREG_SP);  /* reg[SP] = SP + 4 */
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);   /* R0 = rX * 4 */
    sh4_emit_mov_l_store_r0(&cg, SH4_REG_ARG0, SH4_REG_BASE);
    sh4g_close(tp, &cg); }
#if CGBA_SH4_PIN_GUEST_R0
  { u8 *skip_r11;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);      /* T = (rX == 0) */
      sh4g_close(tp, &cg); }
    skip_r11 = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_GUEST_R0); /* sync pinned r0 */
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(skip_r11, *tp); }
#endif
#if CGBA_SH4_PIN_GUEST_R1
  { u8 *skip_r12;
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpeq_imm(&cg, 4);                       /* T = (rX == 1) */
      sh4g_close(tp, &cg); }
    skip_r12 = sh4g_emit_bf_placeholder(tp);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_GUEST_R1); /* sync pinned r1 */
      sh4g_close(tp, &cg); }
    sh4g_patch_cond(skip_r12, *tp); }
#endif
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, -1, SH4_REG_CYCLES);          /* IWRAM word load cost = 1 */
    sh4g_close(tp, &cg); }

  /* Entry 2: shared Thumb BX tail (`cgba_sh4_thumb_bx_routine`). */
  if (bx_tail_off)
    *bx_tail_off = (size_t)(*tp - entry);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 1);                           /* T = ARM target (bit0 == 0) */
    sh4g_close(tp, &cg); }
  bx_arm = sh4g_emit_bt_placeholder(tp);

  /* Thumb target: charge ws_cyc_seq[(target >> 24) & 0xF][0]. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_and_imm(&cg, 15);
    sh4_emit_shll(&cg, SH4_REG_RET);                    /* R0 = region * 2 */
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_seq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);                /* T = (R13 > 0) */
    sh4g_close(tp, &cg); }
  bx_thumb_exhausted = sh4g_emit_bf_placeholder(tp);
  sh4g_vec_jmp(tp, SH4G_VEC_ib_dual_thumb_current);

  sh4g_patch_cond(bx_thumb_exhausted, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, -1, SH4_REG_ARG0);            /* clear bit0 (bit0 == 1) */
    sh4g_close(tp, &cg); }
  sh4g_block_exit_jmp(tp);

  sh4g_patch_cond(bx_arm, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);                /* T = (R13 > 0) */
    sh4g_close(tp, &cg); }
  bx_arm_exhausted = sh4g_emit_bf_placeholder(tp);
  sh4g_vec_jmp(tp, SH4G_VEC_ib_dual);

  sh4g_patch_cond(bx_arm_exhausted, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, 2, SH4_REG_ARG0);
    sh4_emit_shlr2(&cg, SH4_REG_ARG0);
    sh4_emit_shll2(&cg, SH4_REG_ARG0);                  /* (target + 2) & ~3 */
    sh4_emit_mov_imm(&cg, -33, SH4_REG_RET);            /* ~0x20 */
    sh4_emit_and(&cg, SH4_REG_RET, SH4_REG_CPSR);       /* clear CPSR.T */
    sh4g_close(tp, &cg); }
  sh4g_block_exit_jmp(tp);

  sh4g_patch_cond(pop_slow, *tp);
  sh4g_far_jmp(tp, (const void *)sh4_pop1_bx_slow);

  sh4g_litseg_end_tail(tp);
  return entry;
}

/* ---- shared PSR mode-change (set_cpu_mode) routine ----------------------
 * Called (JSR) from MSR-cpsr control-byte sites whose merged value changes
 * the CPSR mode nibble; keeping the ~110-byte re-bank OUT of translated
 * blocks matters more than its instruction count — inlining it grew the hot
 * IRQ-dispatcher block and cost +59% I-cache misses on AW. In: R1 = merged
 * CPSR (preserved; the site stores it into the cached CPSR R8 after
 * return). Mirrors gpSP's set_cpu_mode exactly: early-out on old==new;
 * retire r13/r14 into reg_mode[old & 0xF][5..6], bank r8..r12 via
 * reg_mode[MODE_USER][0..4] and r8..r14 via reg_mode[MODE_FIQ][0..6] when
 * entering or leaving FIQ, load r13/r14 from reg_mode[new & 0xF][5..6],
 * and update reg[CPU_MODE]. Leaf; only uses caller-scratch
 * (R0, R2, R3, R5, R6, R7). */
extern u32 reg_mode[7][7];
extern const u32 cpu_modes[16];

static inline u8 *sh4g_psr_emit_rebank_routine(u8 **tp)
{
  u8 *entry = *tp;
  u8 *same_mode, *from_fiq, *to_fiq;
  int i;

  sh4g_litseg_begin();

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
  /* R5 = old gpSP mode; early-out when unchanged (avoids FIQ->FIQ re-bank). */
  sh4g_load_greg(tp, SH4_GREG_CPU_MODE, SH4_REG_ARG1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmpeq(&cg, SH4_REG_ARG1, SH4_REG_ARG3);
    sh4g_close(tp, &cg); }
  same_mode = sh4g_emit_bt_placeholder(tp);

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
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 0x12);                   /* leaving FIQ? */
    sh4g_close(tp, &cg); }
  from_fiq = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 0x12);                   /* entering FIQ? */
    sh4g_close(tp, &cg); }
  to_fiq = sh4g_emit_bt_placeholder(tp);

  /* Common non-FIQ <-> non-FIQ hot path: retire/load r13/r14 only. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, SH4_GREG_SP, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 5);
    sh4_emit_load_greg(&cg, SH4_GREG_LR, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 6);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 5);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_SP);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 6);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_LR);
    sh4_emit_store_greg(&cg, SH4_REG_ARG3, SH4_GREG_CPU_MODE);
    sh4g_close(tp, &cg); }
  sh4g_patch_cond(same_mode, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_rts(&cg);
    sh4_emit_nop(&cg);
    sh4g_close(tp, &cg); }

  /* Leaving FIQ: save r8..r14 to FIQ bank, restore r8..r12 from USER bank,
   * load r13..r14 from new mode bank. */
  sh4g_patch_cond(from_fiq, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    for (i = 0; i < 7; i++) {
      sh4_emit_load_greg(&cg, 8 + (unsigned)i, SH4_REG_T1);
      sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, (unsigned)i);
    }
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)reg_mode, SH4_REG_ARG2);
  { sh4_codegen cg = sh4g_open(tp);
    for (i = 0; i < 5; i++) {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG2, SH4_REG_T1, (unsigned)i);
      sh4_emit_store_greg(&cg, SH4_REG_T1, 8 + (unsigned)i);
    }
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 5);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_SP);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 6);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_LR);
    sh4_emit_store_greg(&cg, SH4_REG_ARG3, SH4_GREG_CPU_MODE);
    sh4_emit_rts(&cg);
    sh4_emit_nop(&cg);
    sh4g_close(tp, &cg); }

  /* Entering FIQ: save r13..r14 to old mode bank, save r8..r12 to USER bank,
   * load r8..r14 from FIQ bank. */
  sh4g_patch_cond(to_fiq, *tp);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, SH4_GREG_SP, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 5);
    sh4_emit_load_greg(&cg, SH4_GREG_LR, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, 6);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)reg_mode, SH4_REG_ARG2);
  { sh4_codegen cg = sh4g_open(tp);
    for (i = 0; i < 5; i++) {
      sh4_emit_load_greg(&cg, 8 + (unsigned)i, SH4_REG_T1);
      sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_ARG2, (unsigned)i);
    }
    for (i = 0; i < 7; i++) {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, (unsigned)i);
      sh4_emit_store_greg(&cg, SH4_REG_T1, 8 + (unsigned)i);
    }
    sh4_emit_store_greg(&cg, SH4_REG_ARG3, SH4_GREG_CPU_MODE);
    sh4_emit_rts(&cg);
    sh4_emit_nop(&cg);
    sh4g_close(tp, &cg); }

  sh4g_litseg_end_tail(tp);
  return entry;
}

#endif /* CGBA_SH4_FASTMEM_EMIT_H */
