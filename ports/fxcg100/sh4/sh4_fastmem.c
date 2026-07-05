/*
 * Resident fastmem routines (see sh4_fastmem.h): the 16 shared load/store
 * fast paths, generated once at dynarec init. The buffer is ordinary .bss —
 * NOT the high arena (its end is pinned at the hardware-proven 0x8c655300)
 * — and survives translation-cache flushes; SH7305 P1 RAM is executable
 * after an explicit cache sync.
 */

#include "vendor/gpsp/common.h"   /* gpSP fixed-size types (u8/u32/...) */
#include "ports/fxcg100/sh4/sh4_fastmem.h"

#ifdef CGBA_FXCG100
#include "ports/fxcg100/sh4/sh4_cache.h"
#endif

u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
u8 *cgba_sh4_psr_rebank_routine;

static u8 cgba_fastmem_buf[8192] __attribute__((aligned(32)));

void cgba_sh4_fastmem_init(void)
{
  u8 *tp = cgba_fastmem_buf;
  int fm;

  if (cgba_sh4_fastmem_routine[0])       /* idempotent (re-init on reset) */
    return;
  for (fm = 0; fm < CGBA_FM_COUNT; fm++)
    cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_routine(&tp, fm);
  for (fm = CGBA_FMB_LDM; fm < CGBA_FM_TOTAL; fm++)
    cgba_sh4_fastmem_routine[fm] = sh4g_fastmem_emit_block_routine(&tp, fm);
  cgba_sh4_psr_rebank_routine = sh4g_psr_emit_rebank_routine(&tp);
  if ((size_t)(tp - cgba_fastmem_buf) > sizeof cgba_fastmem_buf) {
#ifdef CGBA_FXCG100
    extern void gint_panic(uint32_t code);
    gint_panic(0x0CBBu);              /* resident routine buffer overflow */
#endif
    for (;;)
      ;
  }
#ifdef CGBA_FXCG100
  cgba_sh4_cache_sync(cgba_fastmem_buf, tp);
#endif
}
