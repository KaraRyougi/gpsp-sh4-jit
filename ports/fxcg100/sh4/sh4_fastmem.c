/*
 * Resident fastmem routines (see sh4_fastmem.h): the 16 shared load/store
 * fast paths, generated once at dynarec init. The buffer has a dedicated
 * NOLOAD XYRAM allocation — not the high arena (whose end is pinned at the
 * hardware-proven 0x8c655300) — and survives translation-cache flushes.
 * Cache sync remains harmless for the P4 XYRAM window and keeps host/emulator
 * configurations using ordinary executable RAM coherent.
 */

#include "vendor/gpsp/common.h"   /* gpSP fixed-size types (u8/u32/...) */
#include "ports/fxcg100/sh4/sh4_fastmem.h"

#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
#include "ports/fxcg100/sh4/sh4_cache.h"
/* Avoid gint's legacy u32 typedef colliding with gpSP's common.h. This is the
 * public gint/gint.h API and its documented 20 KiB on-chip backup contract. */
extern void gint_set_onchip_save_mode(int mode, void *ptr);
enum { CGBA_GINT_ONCHIP_BACKUP = 1, CGBA_GINT_ONCHIP_BUFSIZE = 20 << 10 };
#endif

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
u8 *cgba_sh4_psr_rebank_routine;

#if CGBA_SH4_FASTMEM_XYRAM
static u8 cgba_fastmem_buf[8192]
  __attribute__((section(".xyram.fastmem"), aligned(32)));
#else
static u8 cgba_fastmem_buf[8192] __attribute__((aligned(32)));
#endif

#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && \
    CGBA_SH4_FASTMEM_XYRAM
/* gint's default world-switch policy reloads linked on-chip constants and
 * clears everything else. The generated fastmem body is NOLOAD state, so keep
 * all 20 KiB of ILRAM/XYRAM across BFile/OS calls instead of silently wiping
 * it while leaving the routine pointer table live. */
static u8 cgba_onchip_backup[CGBA_GINT_ONCHIP_BUFSIZE]
  __attribute__((aligned(32)));
#endif

void cgba_sh4_fastmem_init(void)
{
  u8 *tp = cgba_fastmem_buf;
  int fm;

#if (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && \
    CGBA_SH4_FASTMEM_XYRAM
  gint_set_onchip_save_mode(CGBA_GINT_ONCHIP_BACKUP, cgba_onchip_backup);
#endif
  if (cgba_sh4_fastmem_routine[0])       /* idempotent (re-init on reset) */
    return;
  for (fm = 0; fm < CGBA_FM_COUNT; fm++)
    cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_routine(&tp, fm);
  for (fm = CGBA_FMB_LDM; fm < CGBA_FM_TOTAL; fm++)
    cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_block_routine(&tp, fm);
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
#endif
}
