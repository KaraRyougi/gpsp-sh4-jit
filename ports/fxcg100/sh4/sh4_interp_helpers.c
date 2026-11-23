/*
 * Bring-up instruction helpers for the SH-4A dynarec.
 *
 * The dynarec emits real SH4 for Thumb data-processing, shifts, branches and
 * conditions; the heavier/rarer instruction classes route to these C helpers
 * (correct-by-reuse of gpSP's memory core) while the inline emitters grow. Each
 * helper interprets exactly one guest instruction against gpSP's `reg[]` state
 * and the execute load/store memory accessors. Helpers that can change the
 * guest PC return 1 (pure PC change: re-dispatch, no event pass) or 2 (store
 * alert: exit via update_gba) so the emitted glue re-dispatches — see
 * sh4_helper_exit in sh4_stub.S.
 *
 * These keep gpSP's little-endian guest semantics: memory goes through the
 * execute_load / execute_store helpers, never raw host pointers.
 */

#include <stdint.h>

#include "vendor/gpsp/common.h"
#include "vendor/gpsp/cpu.h"
#include "ports/fxcg100/sh4/sh4_open_bios_layout.h"
#include "ports/fxcg100/sh4/sh4_swi_overlap.h"
#ifdef CGBA_SH4_SWI_OAM_BULK
#include "ports/fxcg100/sh4/sh4_swi_oam.h"
#endif
#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
#include "ports/fxcg100/sh4/sh4_thumb_udiv.h"
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
#include "ports/fxcg100/sh4/sh4_arm_mixer.h"
#include "ports/fxcg100/sh4/sh4_arm_pcm_clamp.h"
#endif

u32 execute_arm_translate_internal(u32 cycles, void *reg_base);  /* sh4_stub.S */

#ifndef CGBA_SH4_SWI_CPUSET_FAITHFUL
#define CGBA_SH4_SWI_CPUSET_FAITHFUL 1  /* exact-model CpuSet/CpuFastSet HLE */
#endif
#ifndef CGBA_SH4_SWI_FORWARD_OVERLAP
#define CGBA_SH4_SWI_FORWARD_OVERLAP 0  /* opt-in forward pattern-copy HLE */
#endif
#ifndef CGBA_SH4_SWI_HLE_VERIFY
#define CGBA_SH4_SWI_HLE_VERIFY 0   /* predict, then interp + compare (diag) */
#endif

extern u32 cgba_diff_stop_pc;
extern int cgba_diff_stop_active;
extern int cgba_diff_stop_skip_initial;
extern s32 cgba_diff_stop_cycles_remaining;
extern int cgba_diff_stop_reslice;
extern int cgba_diff_stop_on_budget;

/* Cold-code gate: ROM blocks are only translated once dispatched this many
 * times; colder code runs on the interpreter in small budget chunks. The
 * in-world Metroid working set (~thousands of live blocks) overflowed the
 * former 896KB ROM cache, so unconditional translation wholesale-flushed
 * ~1.3x per FRAME (profiled: 95% of the slow regime was
 * translate_block_thumb + the emitters). Hotness survives flushes, so after
 * warmup only the hot set is cached and the flush cycle stops. Collisions in
 * the counter hash only
 * pre-heat a block — harmless. */
#ifndef CGBA_SH4_HOT_THRESHOLD
#define CGBA_SH4_HOT_THRESHOLD 64
#endif
#ifndef CGBA_SH4_COLD_CHUNK_CYCLES
#define CGBA_SH4_COLD_CHUNK_CYCLES 512
#endif
u8  cgba_hot_count[16384];
int cgba_cold_pending;
int cgba_cold_gate_enable;
int cgba_cold_gate_probe;   /* >0: gate checks don't heat (external-exit resolution) */
u32 cgba_sp_non_iwram_disable;
u32 cgba_sp_non_iwram_deopts;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_dynarec_cold_interp_count;
#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_interp_instr_bios, cgba_interp_instr_rom, cgba_interp_instr_ram;
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
/* Emission-mix counters (see sh4_emit_glue.h). */
unsigned long cgba_em_const_small, cgba_em_const_large, cgba_em_const_bytes;
unsigned long cgba_em_fcall_n, cgba_em_fcall_bytes;
unsigned long cgba_em_fjmp_n, cgba_em_fjmp_bytes;
unsigned long cgba_em_pj_n, cgba_em_pj_bytes;
unsigned long cgba_em_fm_n, cgba_em_fm_bytes;
unsigned long cgba_em_blk_n, cgba_em_blk_bytes;
unsigned long cgba_em_pool_ref_n, cgba_em_pool_unique_n;
unsigned long cgba_em_pool_flush_n, cgba_em_pool_bytes;
unsigned long cgba_em_help_n, cgba_em_help_bytes;
unsigned long cgba_em_tuple_routine_b, cgba_em_tuple_tramp_b;
unsigned long cgba_em_tuple_fn_b, cgba_em_tuple_opcode_b;
unsigned long cgba_em_tuple_pc_b, cgba_em_tuple_cycle_b;
unsigned long cgba_em_tuple_params_b;
unsigned long cgba_em_blk_max_bytes, cgba_em_blk_hist[6];
unsigned long cgba_em_blk_mode_n[2], cgba_em_blk_mode_bytes[2];
#endif
#endif
extern int cgba_diff_stop_on_bios_exit;
static u32 cgba_swi_dispatch_cycles(u32 sp_region, u32 lr_region, int thumb);

#if defined(CGBA_GPSP_HEADLESS_TEST) && CGBA_GPSP_HEADLESS_TRACE_PC != 0
static void sh4_headless_putc(char c)
{
  *(volatile unsigned char *)0xb7000000u = (unsigned char)c;
}

static void sh4_headless_puts(const char *s)
{
  while (*s)
    sh4_headless_putc(*s++);
}

static void sh4_headless_hex32(u32 v)
{
  static const char hex[] = "0123456789ABCDEF";
  int i;
  for (i = 7; i >= 0; i--)
    sh4_headless_putc(hex[(v >> (i * 4)) & 0x0F]);
}

static void sh4_headless_trace_op(char tag, u32 pc, u32 opcode)
{
  static u32 count;
  if ((u32)CGBA_GPSP_HEADLESS_TRACE_PC != 0xffffffffu &&
      pc != (u32)CGBA_GPSP_HEADLESS_TRACE_PC)
    return;
  count++;
  if ((count & (u32)CGBA_GPSP_HEADLESS_TRACE_MASK) != 0)
    return;

  sh4_headless_putc(tag);
  sh4_headless_hex32(count);
  sh4_headless_puts(" p");
  sh4_headless_hex32(pc);
  sh4_headless_puts(" op");
  sh4_headless_hex32(opcode);
  sh4_headless_puts(" r0");
  sh4_headless_hex32(reg[0]);
  sh4_headless_puts(" r3");
  sh4_headless_hex32(reg[3]);
  sh4_headless_puts(" r4");
  sh4_headless_hex32(reg[4]);
  sh4_headless_puts(" r5");
  sh4_headless_hex32(reg[5]);
  sh4_headless_puts(" r9");
  sh4_headless_hex32(reg[9]);
  sh4_headless_puts(" cpsr");
  sh4_headless_hex32(reg[REG_CPSR]);
  sh4_headless_puts(" pc");
  sh4_headless_hex32(reg[REG_PC]);
  sh4_headless_putc('\n');
}
#else
static void sh4_headless_trace_op(char tag, u32 pc, u32 opcode)
{
  (void)tag;
  (void)pc;
  (void)opcode;
}
#endif

/* When set, sh4_block_exit returns to the C caller after one translated block
 * (the differential harness uses this to step the dynarec one block at a time). */
int cgba_dynarec_single_block = 0;
#ifdef CGBA_SH4_DIFF_HARNESS
#define CGBA_SH4_DIFF_ACTIVE() (cgba_dynarec_single_block != 0)
#else
#define CGBA_SH4_DIFF_ACTIVE() 0
#endif

enum {
  CGBA_SH4_TLD_SRC_PC = 0,
  CGBA_SH4_TLD_SRC_SP,
  CGBA_SH4_TLD_SRC_REG,
  CGBA_SH4_TLD_SRC_IMM
};

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
#ifndef CGBA_SH4_LDST_DETAIL_COUNTERS
#define CGBA_SH4_LDST_DETAIL_COUNTERS 0
#endif
u32 cgba_sh4_helper_thumb_ldst_count;
u32 cgba_sh4_helper_thumb_block_count;
u32 cgba_sh4_helper_thumb_shift_count;
u32 cgba_sh4_helper_thumb_dp_count;
u32 cgba_sh4_helper_arm_ldst_count;
u32 cgba_sh4_helper_arm_block_count;
u32 cgba_sh4_helper_arm_dp_count;
u32 cgba_sh4_helper_arm_mul_count;
u32 cgba_sh4_helper_arm_psr_count;
u32 cgba_sh4_helper_arm_swap_count;
u32 cgba_sh4_helper_hle_div_count;
u32 cgba_sh4_helper_arm_ldst_load_count;
u32 cgba_sh4_helper_arm_ldst_store_count;
u32 cgba_sh4_helper_arm_ldst_ram_count;
u32 cgba_sh4_helper_arm_ldst_io_count;
u32 cgba_sh4_helper_arm_ldst_video_count;
u32 cgba_sh4_helper_arm_ldst_rom_count;
u32 cgba_sh4_helper_arm_ldst_other_count;
u32 cgba_sh4_helper_arm_block_load_count;
u32 cgba_sh4_helper_arm_block_store_count;
u32 cgba_sh4_helper_thumb_ldst_load_count;
u32 cgba_sh4_helper_thumb_ldst_store_count;
u32 cgba_sh4_helper_thumb_ldst_ram_count;
u32 cgba_sh4_helper_thumb_ldst_io_count;
u32 cgba_sh4_helper_thumb_ldst_video_count;
u32 cgba_sh4_helper_thumb_ldst_video_region_count[3];
u32 cgba_sh4_helper_thumb_ldst_rom_count;
u32 cgba_sh4_helper_thumb_ldst_other_count;
u32 cgba_sh4_helper_thumb_ldst_unmapped_count;
u32 cgba_sh4_helper_thumb_ldst_guest_unaligned_count;
u32 cgba_sh4_helper_thumb_ldst_host_unaligned_count;
u32 cgba_sh4_helper_thumb_ldst_unsafe_region_count;
u32 cgba_sh4_helper_thumb_ldst_smc_count;
u32 cgba_sh4_helper_thumb_ldst_native_ready_count;
u32 cgba_sh4_helper_thumb_ldst_word_count;
u32 cgba_sh4_helper_thumb_ldst_byte_count;
u32 cgba_sh4_helper_thumb_ldst_half_count;
u32 cgba_sh4_helper_thumb_ldst_pc_count;
u32 cgba_sh4_helper_thumb_ldst_sp_count;
u32 cgba_sh4_helper_thumb_ldst_reg_count;
u32 cgba_sh4_helper_thumb_ldst_imm_count;
u32 cgba_sh4_native_thumb_const_io_count;
u32 cgba_sh4_thumb_io16_store_count[512];
u32 cgba_sh4_dma_ctl_count[4];
u32 cgba_sh4_dma_ctl_enable_count[4];
u32 cgba_sh4_dma_ctl_value[4][4];
u32 cgba_sh4_dma_ctl_value_count[4][4];
u32 cgba_sh4_hle_div_zero_count;
u32 cgba_sh4_hle_div_one_count;
u32 cgba_sh4_hle_div_neg_one_count;
u32 cgba_sh4_hle_div_pow2_count;
u32 cgba_sh4_hle_div_other_count;
/* BIOS-fallback residency: calls into the interpreter fallback and the guest
 * cycles it consumed. When the ON-menu page shows all-zero JIT counters (e.g.
 * during the open-BIOS boot screen after a soft reset), these say explicitly
 * that the time went to interpreted BIOS code rather than to a JIT stall. */
u32 cgba_sh4_bios_fallback_call_count;
u32 cgba_sh4_bios_fallback_cycle_count;
#define CGBA_SH4_HELPER_HIT(name) (cgba_sh4_helper_##name##_count++)
static void cgba_sh4_helper_arm_ldst_detail(u32 is_load, u32 address)
{
  u32 region = address >> 24;
  if (is_load) cgba_sh4_helper_arm_ldst_load_count++;
  else         cgba_sh4_helper_arm_ldst_store_count++;
  if (region == 0x02 || region == 0x03) cgba_sh4_helper_arm_ldst_ram_count++;
  else if (region == 0x04)              cgba_sh4_helper_arm_ldst_io_count++;
  else if (region >= 0x05 && region <= 0x07) cgba_sh4_helper_arm_ldst_video_count++;
  else if (region >= 0x08 && region <= 0x0E) cgba_sh4_helper_arm_ldst_rom_count++;
  else                                  cgba_sh4_helper_arm_ldst_other_count++;
}
static void cgba_sh4_helper_arm_block_detail(u32 is_load)
{
  if (is_load) cgba_sh4_helper_arm_block_load_count++;
  else         cgba_sh4_helper_arm_block_store_count++;
}
static u32 cgba_sh4_helper_thumb_tag_nonzero(u8 *map, u32 region,
  u32 offset, u32 bytes)
{
  u8 *tag = (region == 0x02) ? map + 0x40000 : map - 0x8000;

  if(bytes == 4)
    return readaddress32(tag, offset) != 0;
  if(bytes == 2)
    return readaddress16(tag, offset) != 0;
  return readaddress8(tag, offset) != 0;
}

static void cgba_sh4_helper_thumb_ldst_detail(u32 is_load, u32 address,
  u32 bytes, u32 source)
{
#if CGBA_SH4_LDST_DETAIL_COUNTERS
  u32 region = address >> 24;
  u32 align_mask = bytes - 1;
  if (is_load) cgba_sh4_helper_thumb_ldst_load_count++;
  else         cgba_sh4_helper_thumb_ldst_store_count++;
  if (region == 0x02 || region == 0x03) cgba_sh4_helper_thumb_ldst_ram_count++;
  else if (region == 0x04)              cgba_sh4_helper_thumb_ldst_io_count++;
  else if (region >= 0x05 && region <= 0x07) {
    cgba_sh4_helper_thumb_ldst_video_count++;
    cgba_sh4_helper_thumb_ldst_video_region_count[region - 0x05]++;
  }
  else if (region >= 0x08 && region <= 0x0E) cgba_sh4_helper_thumb_ldst_rom_count++;
  else                                  cgba_sh4_helper_thumb_ldst_other_count++;
  if (bytes == 4)      cgba_sh4_helper_thumb_ldst_word_count++;
  else if (bytes == 2) cgba_sh4_helper_thumb_ldst_half_count++;
  else                 cgba_sh4_helper_thumb_ldst_byte_count++;
  if (!is_load && region == 0x04 && bytes == 2)
    cgba_sh4_thumb_io16_store_count[(address & 0x3FEu) >> 1]++;
  if (source == CGBA_SH4_TLD_SRC_PC)      cgba_sh4_helper_thumb_ldst_pc_count++;
  else if (source == CGBA_SH4_TLD_SRC_SP) cgba_sh4_helper_thumb_ldst_sp_count++;
  else if (source == CGBA_SH4_TLD_SRC_REG) cgba_sh4_helper_thumb_ldst_reg_count++;
  else                                    cgba_sh4_helper_thumb_ldst_imm_count++;

  if (address >= 0x10000000u) {
    cgba_sh4_helper_thumb_ldst_unsafe_region_count++;
  } else {
    u8 *map = memory_map_read[address >> 15];
    if (!map) {
      cgba_sh4_helper_thumb_ldst_unmapped_count++;
    } else if (align_mask && (address & align_mask)) {
      cgba_sh4_helper_thumb_ldst_guest_unaligned_count++;
    } else if (align_mask &&
        (((u32)(uintptr_t)(map + (address & 0x7FFFu))) & align_mask)) {
      cgba_sh4_helper_thumb_ldst_host_unaligned_count++;
    } else if (is_load ?
        (region < 0x02 || region >= 0x0D) :
        (region != 0x02 && region != 0x03)) {
      cgba_sh4_helper_thumb_ldst_unsafe_region_count++;
    } else if (!is_load && cgba_sh4_helper_thumb_tag_nonzero(map, region,
        address & 0x7FFFu, bytes)) {
      cgba_sh4_helper_thumb_ldst_smc_count++;
    } else {
      cgba_sh4_helper_thumb_ldst_native_ready_count++;
    }
  }
#else
  (void)is_load;
  (void)address;
  (void)bytes;
  (void)source;
#endif
}

static void cgba_sh4_note_dma_ctl16(u32 address, u32 source)
{
#if CGBA_SH4_LDST_DETAIL_COUNTERS
  u32 offset = address & 0x3FEu;
  int dma = -1;
  int slot;

  switch (offset) {
  case 0x0BAu: dma = 0; break;
  case 0x0C6u: dma = 1; break;
  case 0x0D2u: dma = 2; break;
  case 0x0DEu: dma = 3; break;
  default: return;
  }

  source &= 0xFFFFu;
  cgba_sh4_dma_ctl_count[dma]++;
  if (source & 0x8000u)
    cgba_sh4_dma_ctl_enable_count[dma]++;
  for (slot = 0; slot < 4; slot++) {
    if (cgba_sh4_dma_ctl_value_count[dma][slot] == 0 ||
        cgba_sh4_dma_ctl_value[dma][slot] == source) {
      cgba_sh4_dma_ctl_value[dma][slot] = source;
      cgba_sh4_dma_ctl_value_count[dma][slot]++;
      return;
    }
  }
#else
  (void)address;
  (void)source;
#endif
}
#else
#define CGBA_SH4_HELPER_HIT(name) ((void)0)
#define cgba_sh4_helper_arm_ldst_detail(is_load, address) ((void)0)
#define cgba_sh4_helper_arm_block_detail(is_load) ((void)0)
#define cgba_sh4_helper_thumb_ldst_detail(is_load, address, bytes, source) ((void)0)
#define cgba_sh4_note_dma_ctl16(address, source) ((void)0)
#endif


#include "ports/fxcg100/sh4/sh4_mem_helpers.inc.h"
#include "ports/fxcg100/sh4/sh4_arm_thumb_helpers.inc.h"
#include "ports/fxcg100/sh4/sh4_audio_fastpaths.inc.h"
#include "ports/fxcg100/sh4/sh4_bios_hle.inc.h"
#include "ports/fxcg100/sh4/sh4_cold_interp.inc.h"
