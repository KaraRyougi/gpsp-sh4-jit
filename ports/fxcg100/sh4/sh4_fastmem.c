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
#include "vendor/gpsp/gpsp_config.h"
#include "ports/fxcg100/sh4/sh4_fastmem_emit.h"

#if CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
extern u8 rom_translation_cache[ROM_TRANSLATION_CACHE_SIZE];
extern u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];
extern int cgba_sh4_extra_cycles;

/* Out-of-line slow-path descriptors for fastmem sites (single + WB + block):
 *   - ROM cache: grows downward from the top of rom_translation_cache (sorted
 *     in strictly descending ret_pc order; shares the existing ROM arena with
 *     zero extra .cgba.highbss cost).
 *   - RAM cache: fixed 4096-entry table (64 KiB in .cgba.highbss, sorted in
 *     strictly ascending ret_pc order; any overflow falls back to C helper).
 *   - 512-entry direct-mapped cache in .cgba.highbss probed directly in SH-4
 *     ASM inside sh4_fastmem_compact8_tramp. */
enum {
  CGBA_SH4_RAM_FM_DESC_MAX = 4096,
  CGBA_SH4_OOL_CACHE_SIZE = 512
};
static cgba_sh4_fm_desc cgba_sh4_ram_fm_desc[CGBA_SH4_RAM_FM_DESC_MAX] CGBA_HIGH_BSS;
static u32 cgba_sh4_ram_fm_desc_count CGBA_HIGH_BSS;
static cgba_sh4_fm_desc *cgba_sh4_rom_fm_desc_cur CGBA_HIGH_BSS;
const cgba_sh4_fm_desc *cgba_sh4_fm_ool_cache[CGBA_SH4_OOL_CACHE_SIZE] CGBA_HIGH_BSS;

static inline unsigned cgba_sh4_fm_ool_hash(u32 ret_pc)
{
  return ((ret_pc >> 1) ^ (ret_pc >> 10)) & (CGBA_SH4_OOL_CACHE_SIZE - 1u);
}

static inline cgba_sh4_fm_desc *cgba_sh4_rom_fm_desc_top(void)
{
  return (cgba_sh4_fm_desc *)(void *)(rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE);
}

void cgba_sh4_fm_ool_reset_rom(void)
{
  cgba_sh4_rom_fm_desc_cur = cgba_sh4_rom_fm_desc_top();
  memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
}

void cgba_sh4_fm_ool_reset_ram(void)
{
  cgba_sh4_ram_fm_desc_count = 0u;
  memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
}

void cgba_sh4_fm_ool_reset_all(void)
{
  cgba_sh4_rom_fm_desc_cur = cgba_sh4_rom_fm_desc_top();
  cgba_sh4_ram_fm_desc_count = 0u;
  memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
}

u8 *cgba_sh4_fm_ool_rom_limit(u32 threshold)
{
  cgba_sh4_fm_desc *cur = cgba_sh4_rom_fm_desc_cur;
  if (!cur) {
    cur = cgba_sh4_rom_fm_desc_top();
    cgba_sh4_rom_fm_desc_cur = cur;
  }
  u8 *top_bytes = (u8 *)(void *)cur;
  if (top_bytes <= rom_translation_cache + threshold)
    return rom_translation_cache;
  return top_bytes - threshold;
}

void cgba_sh4_fm_ool_rewind(const u8 *ptr, int ram_region)
{
  u32 lim = (u32)(uintptr_t)ptr;
  if (ram_region) {
    while (cgba_sh4_ram_fm_desc_count > 0u &&
           cgba_sh4_ram_fm_desc[cgba_sh4_ram_fm_desc_count - 1u].ret_pc >= lim)
      cgba_sh4_ram_fm_desc_count--;
  } else {
    cgba_sh4_fm_desc *top = cgba_sh4_rom_fm_desc_top();
    if (!cgba_sh4_rom_fm_desc_cur)
      cgba_sh4_rom_fm_desc_cur = top;
    while (cgba_sh4_rom_fm_desc_cur < top &&
           cgba_sh4_rom_fm_desc_cur->ret_pc >= lim)
      cgba_sh4_rom_fm_desc_cur++;
  }
  memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
}

int cgba_sh4_fm_ool_can_record(const u8 *tp)
{
  if (tp >= rom_translation_cache &&
      tp < rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE) {
    cgba_sh4_fm_desc *cur = cgba_sh4_rom_fm_desc_cur;
    if (!cur) {
      cur = cgba_sh4_rom_fm_desc_top();
      cgba_sh4_rom_fm_desc_cur = cur;
    }
    return (const u8 *)(void *)(cur - 1) >
           tp + TRANSLATION_CACHE_LIMIT_THRESHOLD + 256u;
  }
  if (tp >= ram_translation_cache &&
      tp < ram_translation_cache + RAM_TRANSLATION_CACHE_SIZE) {
    return cgba_sh4_ram_fm_desc_count < CGBA_SH4_RAM_FM_DESC_MAX;
  }
  return 0;
}

void cgba_sh4_fm_ool_push(u32 ret_pc, u32 pc_flags, u32 opcode, u32 cycle_count)
{
  const u8 *p = (const u8 *)(uintptr_t)ret_pc;
  int popped = 0;
  if (p >= rom_translation_cache &&
      p < rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE) {
    cgba_sh4_fm_desc *top = cgba_sh4_rom_fm_desc_top();
    if (!cgba_sh4_rom_fm_desc_cur)
      cgba_sh4_rom_fm_desc_cur = top;
    while (cgba_sh4_rom_fm_desc_cur < top &&
           cgba_sh4_rom_fm_desc_cur->ret_pc >= ret_pc) {
      cgba_sh4_rom_fm_desc_cur++;
      popped = 1;
    }
    if (popped)
      memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
    cgba_sh4_fm_desc *d = --cgba_sh4_rom_fm_desc_cur;
    d->ret_pc = ret_pc;
    d->pc_flags = pc_flags;
    d->opcode = opcode;
    d->cycle_count = cycle_count;
  } else {
    while (cgba_sh4_ram_fm_desc_count > 0u &&
           cgba_sh4_ram_fm_desc[cgba_sh4_ram_fm_desc_count - 1u].ret_pc >= ret_pc) {
      cgba_sh4_ram_fm_desc_count--;
      popped = 1;
    }
    if (popped)
      memset(cgba_sh4_fm_ool_cache, 0, sizeof(cgba_sh4_fm_ool_cache));
    cgba_sh4_fm_desc *d = &cgba_sh4_ram_fm_desc[cgba_sh4_ram_fm_desc_count++];
    d->ret_pc = ret_pc;
    d->pc_flags = pc_flags;
    d->opcode = opcode;
    d->cycle_count = cycle_count;
  }
}

CGBA_HOT_TEXT const cgba_sh4_fm_desc *cgba_sh4_fastmem_ool_lookup(u32 ret_pc)
{
  const u8 *p = (const u8 *)(uintptr_t)ret_pc;
  const cgba_sh4_fm_desc *found = NULL;
  if (p >= rom_translation_cache &&
      p < rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE) {
    const cgba_sh4_fm_desc *arr = cgba_sh4_rom_fm_desc_cur;
    if (arr) {
      u32 n = (u32)(cgba_sh4_rom_fm_desc_top() - arr);
      u32 lo = 0u, hi = n;
      while (lo < hi) {
        u32 mid = lo + ((hi - lo) >> 1);
        u32 k = arr[mid].ret_pc;
        if (k == ret_pc) { found = &arr[mid]; break; }
        if (k > ret_pc) lo = mid + 1u; else hi = mid;
      }
    }
  } else {
    const cgba_sh4_fm_desc *arr = cgba_sh4_ram_fm_desc;
    u32 lo = 0u, hi = cgba_sh4_ram_fm_desc_count;
    while (lo < hi) {
      u32 mid = lo + ((hi - lo) >> 1);
      u32 k = arr[mid].ret_pc;
      if (k == ret_pc) { found = &arr[mid]; break; }
      if (k < ret_pc) lo = mid + 1u; else hi = mid;
    }
  }
  if (!found) {
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
    extern void gint_panic(uint32_t code);
    gint_panic(0x0CBFu);              /* missing OOL fastmem descriptor */
#endif
    for (;;)
      ;
  }
  cgba_sh4_fm_ool_cache[cgba_sh4_fm_ool_hash(ret_pc)] = found;
  return found;
}
#endif

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

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL] CGBA_HIGH_BSS;
u8 *cgba_sh4_fastmem_io16_store CGBA_HIGH_BSS;
u8 *cgba_sh4_fastmem_block_cold[4] CGBA_HIGH_BSS;
u8 *cgba_sh4_pop1_bx_routine CGBA_HIGH_BSS;
u8 *cgba_sh4_thumb_bx_routine CGBA_HIGH_BSS;
u8 *cgba_sh4_psr_rebank_routine CGBA_HIGH_BSS;

/* Place overflow/cold resident fastmem routines (io16_store, psr_rebank, WB
 * handlers that spill past 4 KiB ILRAM) in untranslated P1 .cgba.highbss so
 * calling them never consumes a P0 ITLB/UTLB entry. Co-locating the routine
 * table in .cgba.highbss also guarantees clear_gpsp_highbss() invalidates the
 * cached pointers whenever cgba_fastmem_buf is wiped on ROM switch. */
static u8 cgba_fastmem_buf[10240] CGBA_HIGH_BSS;

#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && !defined(CGBA_CLASSPAD) && \
    CGBA_SH4_FASTMEM_ILRAM
/* gint's default world-switch policy reloads linked on-chip constants and
 * clears everything else. Generated fastmem bodies in ILRAM are NOLOAD
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
    CGBA_SH4_FASTMEM_ILRAM
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
  /* Emit the out-of-line IO16 store handler into .cgba.highbss first so
   * STORE_UH can reference it and shrink from ~1140 B to ~360 B (ILRAM). */
  tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
  cgba_sh4_fastmem_io16_store = sh4g_fastmem_emit_io16_store(&tp);
  /* Emit cold/generic block-transfer fallbacks for the two WB variants so their
   * ILRAM entries hold only the compact branchless 8-step contiguous ladder. */
  memset(cgba_sh4_fastmem_block_cold, 0, sizeof(cgba_sh4_fastmem_block_cold));
  tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
  cgba_sh4_fastmem_block_cold[CGBA_FMB_LDM_WB - CGBA_FMB_LDM] =
    sh4g_fastmem_emit_block_cold(&tp, CGBA_FMB_LDM_WB);
  tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
  cgba_sh4_fastmem_block_cold[CGBA_FMB_STM_WB - CGBA_FMB_LDM] =
    sh4g_fastmem_emit_block_cold(&tp, CGBA_FMB_STM_WB);
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
    if (fm == CGBA_FMB_STM_WB) {
      size_t bx_tail_off = 0;
      u8 *pop_entry;
      tp = (u8 *)(((uintptr_t)tp + 3u) & ~(uintptr_t)3u);
      pop_entry = sh4g_fastmem_emit_pop1_bx(&tp, &bx_tail_off);
      cgba_sh4_pop1_bx_routine = pop_entry;
      cgba_sh4_thumb_bx_routine = pop_entry + bx_tail_off;
#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && CGBA_SH4_FASTMEM_ILRAM
      {
        size_t sz = (size_t)(tp - pop_entry);
        if (ilram_tp < ilram_end && sz <= (size_t)(ilram_end - ilram_tp)) {
          memcpy(ilram_tp, pop_entry, sz);
          cgba_sh4_pop1_bx_routine = ilram_tp;
          cgba_sh4_thumb_bx_routine = ilram_tp + bx_tail_off;
          ilram_tp = (u8 *)(((uintptr_t)ilram_tp + sz + 3u) & ~(uintptr_t)3u);
          tp = pop_entry;
        }
      }
#endif
    }
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
