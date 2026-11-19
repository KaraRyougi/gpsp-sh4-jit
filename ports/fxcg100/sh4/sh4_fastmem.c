/*
 * Resident fastmem routines (see sh4_fastmem.h): the 16 shared load/store
 * fast paths, generated once at dynarec init. The buffer has a dedicated
 * NOLOAD XYRAM allocation — not the high arena (whose end is pinned at the
 * hardware-proven 0x8c655300) — and survives translation-cache flushes.
 * Cache sync remains harmless for the P4 XYRAM window and keeps host/emulator
 * configurations using ordinary executable RAM coherent.
 */

#include <string.h>
#include "vendor/gpsp/common.h"   /* gpSP fixed-size types (u8/u32/...) */
#include "ports/fxcg100/sh4/sh4_fastmem.h"

#ifndef CGBA_SH4_FASTMEM_ILRAM
#define CGBA_SH4_FASTMEM_ILRAM 1
#endif

#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
#include "ports/fxcg100/sh4/sh4_cache.h"
/* Avoid gint's legacy u32 typedef colliding with gpSP's common.h. This is the
 * public gint/gint.h API and its documented 20 KiB on-chip backup contract. */
extern void gint_set_onchip_save_mode(int mode, void *ptr);
enum { CGBA_GINT_ONCHIP_BACKUP = 1, CGBA_GINT_ONCHIP_BUFSIZE = 20 << 10 };
#if CGBA_SH4_FASTMEM_ILRAM
extern u8 cgba_ilram_fastmem_start[];
extern u8 cgba_ilram_fastmem_end[];
#endif
#endif

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
u8 *cgba_sh4_fastmem_io16_store;
u8 *cgba_sh4_psr_rebank_routine;

#if CGBA_SH4_FASTMEM_XYRAM
static u8 cgba_fastmem_buf[8192]
  __attribute__((section(".xyram.fastmem"), aligned(32)));
#else
static u8 cgba_fastmem_buf[8192] __attribute__((aligned(32)));
#endif

#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && !defined(CGBA_CLASSPAD) && \
    (CGBA_SH4_FASTMEM_XYRAM || CGBA_SH4_FASTMEM_ILRAM)
/* gint's default world-switch policy reloads linked on-chip constants and
 * clears everything else. Generated fastmem bodies in ILRAM/XYRAM are NOLOAD
 * runtime state, so keep all 20 KiB of ILRAM/XYRAM across BFile/OS calls
 * instead of silently wiping them while leaving the routine pointer table live. */
static u8 cgba_onchip_backup[CGBA_GINT_ONCHIP_BUFSIZE]
  __attribute__((aligned(32)));
#endif

void cgba_sh4_fastmem_init(void)
{
  /* Hottest routines first so the zero-wait-state ILRAM budget packs the
   * highest-frequency load/store and block-transfer routines first. */
  static const u8 emit_order[CGBA_FM_TOTAL] = {
    CGBA_FM_LOAD_W,
    CGBA_FM_STORE_W,
    CGBA_FM_LOAD_UH,
    CGBA_FM_LOAD_B,
    CGBA_FM_STORE_UH,
    CGBA_FMB_LDM_WB,
    CGBA_FMB_STM_WB,
    CGBA_FM_STORE_B,
    CGBA_FM_LOAD_SB,
    CGBA_FM_LOAD_SH,
    CGBA_FMB_LDM,
    CGBA_FMB_STM,
    CGBA_FM_LOAD_W + CGBA_FM_WB,
    CGBA_FM_STORE_W + CGBA_FM_WB,
    CGBA_FM_LOAD_UH + CGBA_FM_WB,
    CGBA_FM_LOAD_B + CGBA_FM_WB,
    CGBA_FM_STORE_UH + CGBA_FM_WB,
    CGBA_FM_STORE_B + CGBA_FM_WB,
    CGBA_FM_LOAD_SH + CGBA_FM_WB,
    CGBA_FM_LOAD_SB + CGBA_FM_WB
  };
  u8 *tp = cgba_fastmem_buf;
#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && CGBA_SH4_FASTMEM_ILRAM
  u8 *ilram_tp = cgba_ilram_fastmem_start;
  u8 *ilram_end = cgba_ilram_fastmem_end;
#endif
  int i;

#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && !defined(CGBA_CLASSPAD) && \
    (CGBA_SH4_FASTMEM_XYRAM || CGBA_SH4_FASTMEM_ILRAM)
  gint_set_onchip_save_mode(CGBA_GINT_ONCHIP_BACKUP, cgba_onchip_backup);
#endif
  static const u8 * const *last_frag_blocks;
  static u32 last_gamepak_size;
  static u8 last_ws8_0, last_ws8_1;
  if (cgba_sh4_fastmem_routine[0] &&
      last_frag_blocks == cgba_gamepak_fragment_blocks &&
      last_gamepak_size == gamepak_size &&
      last_ws8_0 == ws_cyc_nseq[8][0] &&
      last_ws8_1 == ws_cyc_nseq[8][1])
    return;
  last_frag_blocks = cgba_gamepak_fragment_blocks;
  last_gamepak_size = gamepak_size;
  last_ws8_0 = ws_cyc_nseq[8][0];
  last_ws8_1 = ws_cyc_nseq[8][1];
  /* Emit the out-of-line IO16 store handler into .bss first so STORE_UH can
   * reference it and shrink from ~1140 B to ~360 B (fitting in ILRAM). */
  tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
  cgba_sh4_fastmem_io16_store = sh4g_fastmem_emit_io16_store(&tp);
  for (i = 0; i < CGBA_FM_TOTAL; i++) {
    int fm = emit_order[i];
    u8 *entry;
    /* Keep every routine entry 4-byte aligned so PC-relative literal pools
     * remain identical when relocated into ILRAM. */
    tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
    if (fm < CGBA_FM_COUNT)
      entry = sh4g_fastmem_emit_routine(&tp, fm);
    else
      entry = sh4g_fastmem_emit_block_routine(&tp, fm);
    cgba_sh4_fastmem_routine[fm] = entry;
#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && CGBA_SH4_FASTMEM_ILRAM
    {
      size_t sz = (size_t)(tp - entry);
      if (ilram_tp < ilram_end && sz <= (size_t)(ilram_end - ilram_tp)) {
        memcpy(ilram_tp, entry, sz);
        cgba_sh4_fastmem_routine[fm] = ilram_tp;
        ilram_tp = (u8 *)(((uintptr_t)ilram_tp + sz + 3u) & ~(uintptr_t)3u);
        tp = entry;
      }
    }
#endif
  }
  tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
  cgba_sh4_psr_rebank_routine = sh4g_psr_emit_rebank_routine(&tp);
  if ((size_t)(tp - cgba_fastmem_buf) > sizeof cgba_fastmem_buf) {
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
    extern void gint_panic(uint32_t code);
    gint_panic(0x0CBBu);              /* resident routine buffer overflow */
#endif
    for (;;)
      ;
  }
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
  cgba_sh4_cache_sync(cgba_fastmem_buf, tp);
#if CGBA_SH4_FASTMEM_ILRAM
  if (ilram_tp > cgba_ilram_fastmem_start)
    cgba_sh4_cache_sync(cgba_ilram_fastmem_start, ilram_tp);
#endif
#endif
}
