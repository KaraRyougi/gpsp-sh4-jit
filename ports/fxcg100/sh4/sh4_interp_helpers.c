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
#include "ports/fxcg100/sh4/sh4_swi_overlap.h"
#ifdef CGBA_SH4_SWI_OAM_BULK
#include "ports/fxcg100/sh4/sh4_swi_oam.h"
#endif
#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
#include "ports/fxcg100/sh4/sh4_thumb_udiv.h"
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
#include "ports/fxcg100/sh4/sh4_arm_mixer.h"
#endif

u32 execute_arm_translate_internal(u32 cycles, void *reg_base);  /* sh4_stub.S */

#ifndef CGBA_SH4_INTERP_SWI_HLE
#define CGBA_SH4_INTERP_SWI_HLE 0   /* see cgba_sh4_interp_swi_hle */
#endif
#ifndef CGBA_SH4_SWI_MEM_HLE
#define CGBA_SH4_SWI_MEM_HLE 0      /* see the demoted cases in cgba_hle_bios_swi */
#endif
#ifndef CGBA_SH4_SWI_CPUSET_FAITHFUL
#ifndef CGBA_SH4_SWI_CPUSET_FAITHFUL
#define CGBA_SH4_SWI_CPUSET_FAITHFUL 1  /* exact-model CpuSet/CpuFastSet HLE */
#endif
#endif
#ifndef CGBA_SH4_SWI_FORWARD_OVERLAP
#define CGBA_SH4_SWI_FORWARD_OVERLAP 0  /* opt-in forward pattern-copy HLE */
#endif
#ifndef CGBA_SH4_SWI_HLE_VERIFY
#define CGBA_SH4_SWI_HLE_VERIFY 0   /* predict, then interp + compare (diag) */
#endif
#ifndef CGBA_SH4_SWI_OBJAFFINE_HLE
#define CGBA_SH4_SWI_OBJAFFINE_HLE 0 /* experimental ObjAffineSet (SWI 0x0F) */
#endif

extern u32 cgba_diff_stop_pc;
extern int cgba_diff_stop_active;
extern int cgba_diff_stop_skip_initial;
extern s32 cgba_diff_stop_cycles_remaining;
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
u32 cgba_sh4_native_thumb_runtime_io_count;
u32 cgba_sh4_native_thumb_push_iwram_count;
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

/* ---- guest memory accessors (backend-provided wrappers over gba_memory.c) --
 * The MIPS/ARM/x86 backends supply the execute_load / execute_store family that
 * translated code calls. For SH4, the C wrappers also accumulate the GBA memory
 * access cycles so emitted code can debit R13 after a helper returns. */
/* A store can raise a CPU alert: DMA/HALT idle (CPU_HALT_STATE set), a newly
 * pending IRQ, or SMC (the guest overwrote code). write_memory* return it; we
 * accumulate it here and the ldst helpers consume it after their store(s) via
 * cgba_store_alert_break(). Dropping it (the old behavior) meant the dynarec
 * never idled, never took a store-raised IRQ, and never flushed self-modified
 * code — it just ran past, diverging from the interpreter. */
static cpu_alert_type cgba_store_alert;
int cgba_sh4_extra_cycles;
static int cgba_sh4_mem_cycle_seq;

static void cgba_sh4_reset_mem_cycles(int seq)
{
  cgba_sh4_extra_cycles = 0;
  cgba_sh4_mem_cycle_seq = seq;
}

/* Generated blocks keep their live PC in host state and commit reg[REG_PC]
 * only at dispatch/event boundaries. Generic memory fallbacks are older than
 * that ABI: open-bus reads and BIOS protection inspect reg[REG_PC] directly.
 * Publish the interpreter-visible PC before the fallback. Generated control
 * flow remains authoritative and overwrites it at the normal commit point. */
static inline void cgba_sh4_begin_mem_helper(u32 access_pc)
{
  reg[REG_PC] = access_pc;
}

static void cgba_sh4_charge_mem(u32 address, unsigned size_index)
{
  if (address < 0x10000000u) {
    u32 region = address >> 24;
    cgba_sh4_extra_cycles += cgba_sh4_mem_cycle_seq ?
      ws_cyc_seq[region][size_index] : ws_cyc_nseq[region][size_index];
  }
}

u32 function_cc execute_load_u8(u32 address)  { cgba_sh4_charge_mem(address, 0); return read_memory8(address); }
u32 function_cc execute_load_u16(u32 address) { cgba_sh4_charge_mem(address, 0); return read_memory16(address); }
u32 function_cc execute_load_u32(u32 address) { cgba_sh4_charge_mem(address, 1); return read_memory32(address); }
u32 function_cc execute_load_s8(u32 address)  { cgba_sh4_charge_mem(address, 0); return read_memory8s(address); }
u32 function_cc execute_load_s16(u32 address) { cgba_sh4_charge_mem(address, 0); return read_memory16s(address); }

static u8 *cgba_bulk_ram_host(u32 addr, u32 len, u8 **tags);
static int cgba_bulk_tags_smc(const u8 *tags, u32 len);

static inline u32 cgba_le32_read(const u8 *p)
{
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline void cgba_le32_write(u8 *p, u32 v)
{
  p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 cgba_sh4_align_store_address(u32 address, unsigned bytes)
{
  return address & ~(u32)(bytes - 1);
}

static int cgba_sh4_store_u16_io_fast(u32 address, u32 source)
{
  if ((address >> 24) != 0x04)
    return 0;

  switch (address & 0x3FEu) {
  case 0x000u: { /* DISPCNT */
    u32 value = source & 0xFFFFu;
    reg[OAM_UPDATED] |= ((value & 0x07u) != (read_ioreg(REG_DISPCNT) & 0x07u));
    address16(io_registers, 0x000u) = eswap16((u16)value);
    return 1;
  }
  case 0x004u: { /* DISPSTAT */
    u32 value = (read_ioreg(REG_DISPSTAT) & 0x07u) | (source & ~0x07u);
    address16(io_registers, 0x004u) = eswap16((u16)value);
    return 1;
  }
  case 0x016u:  /* BG1VOFS */
  case 0x01Cu:  /* BG3HOFS */
  case 0x01Eu:  /* BG3VOFS */
  case 0x048u:  /* WININ */
  case 0x04Au:  /* WINOUT */
  case 0x04Cu:  /* MOSAIC */
  case 0x052u:  /* BLDALPHA */
    address16(io_registers, address & 0x3FEu) = eswap16((u16)source);
    return 1;
  case 0x208u:  /* IME */
    address16(io_registers, 0x208u) = eswap16((u16)source);
    if ((source & 0xFFFFu) && (read_ioreg(REG_IE) & read_ioreg(REG_IF)) &&
        ((reg[REG_CPSR] & 0x80u) == 0))
      cgba_store_alert |= CPU_ALERT_IRQ;
    return 1;
  case 0x0BAu:  /* DMA0CNT_H */
  case 0x0C6u:  /* DMA1CNT_H */
  case 0x0D2u:  /* DMA2CNT_H */
  case 0x0DEu: { /* DMA3CNT_H */
    u32 dma_number;
    if (source & 0x8000u)
      return 0;
    dma_number = ((address & 0x3FEu) - 0x0BAu) / 12u;
    dma[dma_number].start_type = DMA_INACTIVE;
    dma[dma_number].direct_sound_channel = DMA_NO_DIRECT_SOUND;
    address16(io_registers, address & 0x3FEu) = eswap16((u16)source);
    return 1;
  }
  default:
    return 0;
  }
}

/* Self-modifying-code detection for RAM stores. gpSP marks every byte that
 * belongs to a translated block in a parallel "tag" mirror (IWRAM: iwram[off];
 * EWRAM: ewram[off + 0x40000]); a nonzero tag over the written range means the
 * guest just overwrote code, so the RAM translation cache must be dropped (done
 * by cgba_store_alert_break on CPU_ALERT_SMC). write_memory*() returns
 * CPU_ALERT_NONE for RAM — the native gpSP backends do this check inline, but the
 * SH4 port stores through these C helpers, so it lives here. The tag is read
 * byte-wise (the target faults on unaligned wide loads). Mirrors
 * dma_write_iwram / dma_write_ewram in gba_memory.c. */
static cpu_alert_type cgba_sh4_smc_check(u32 address, unsigned bytes)
{
  const u8 *tag;
  /* Width-align like the ARM bus (and the gpSP interpreter's fast_write_memory,
   * cpu.cc) so the tag read lands on the same code bytes the access really hits
   * and never walks past the end of the mirror for an unaligned half/word. */
  address = cgba_sh4_align_store_address(address, bytes);
  switch (address >> 24) {
  case 0x03: tag = iwram + (address & 0x7FFF); break;              /* 32 KB  */
  case 0x02: tag = ewram + (address & 0x3FFFF) + 0x40000; break;   /* 256 KB */
  default:   return CPU_ALERT_NONE;
  }
  u32 hit = 0;
  for (unsigned k = 0; k < bytes; k++)
    hit |= tag[k];
  return hit ? CPU_ALERT_SMC : CPU_ALERT_NONE;
}

void function_cc execute_store_u8(u32 address, u32 source)
{
  cgba_sh4_charge_mem(address, 0);
  cgba_store_alert |= write_memory8(address, (u8)source);
  cgba_store_alert |= cgba_sh4_smc_check(address, 1);
}

void function_cc execute_store_u16(u32 address, u32 source)
{
  u32 aligned = cgba_sh4_align_store_address(address, 2);
  cgba_sh4_charge_mem(aligned, 0);
  cgba_sh4_note_dma_ctl16(aligned, source);
  if (cgba_sh4_store_u16_io_fast(aligned, source))
    return;
  cgba_store_alert |= write_memory16(aligned, (u16)source);
  cgba_store_alert |= cgba_sh4_smc_check(aligned, 2);
}

void function_cc execute_store_u32(u32 address, u32 source)
{
  u32 aligned = cgba_sh4_align_store_address(address, 4);
  cgba_sh4_charge_mem(aligned, 1);
  cgba_store_alert |= write_memory32(aligned, source);
  cgba_store_alert |= cgba_sh4_smc_check(aligned, 4);
}

void function_cc execute_store_aligned_u32(u32 address, u32 source)
{
  u32 aligned = cgba_sh4_align_store_address(address, 4);
  cgba_sh4_charge_mem(aligned, 1);
  cgba_store_alert |= write_memory32(aligned, source);
  cgba_store_alert |= cgba_sh4_smc_check(aligned, 4);
}

/* Consume any pending store alert. If set, point PC at the next instruction,
 * flush the RAM code cache on SMC, and return 2 so the emitter glue exits the
 * block through sh4_block_exit -> update_gba (which idles on HALT/DMA and
 * vectors a pending IRQ). Returns 0 — continue the block — when there is no
 * alert, which is always the case after a pure load.
 *
 * Helper return contract (consumed by sh4_helper_exit in sh4_stub.S):
 *   0 = fall through to the next translated instruction;
 *   1 = pure PC change -> re-dispatch WITHOUT an update_gba pass;
 *   2 = store alert    -> exit via sh4_block_exit so update_gba runs now. */
#define CGBA_SH4_HELPER_ALERT 2
static int cgba_store_alert_break(u32 next_pc)
{
  cpu_alert_type a = cgba_store_alert;
  cgba_store_alert = CPU_ALERT_NONE;
  if (!a)
    return 0;
  if (a & CPU_ALERT_SMC)
    flush_translation_cache_ram();
  /* A bare IRQ alert (for example an IME/IE store that unmasks an already
   * pending IRQ) must be serviced at the same scheduler boundary as the
   * interpreter. Vectoring immediately here changes LR_irq and can return into
   * the wrong game routine after the handler's SUBS PC,LR,#4. SMC still exits
   * immediately because stale translated RAM must not continue running. */
  if (a & ~(cpu_alert_type)CPU_ALERT_IRQ) {
    reg[REG_PC] = next_pc;
    /* WAITCNT already invalidated both caches. It needs a pure redispatch,
     * while HALT/SMC/DMA must still enter the scheduler immediately. */
    if ((a & ~(cpu_alert_type)(CPU_ALERT_IRQ | CPU_ALERT_TIMING)) == 0)
      return 1;
    return CGBA_SH4_HELPER_ALERT;
  }
  return 0;
}

/* CPSR flag bits (canonical packed form, matching the interpreter). */
#define CF_N (1u << 31)
#define CF_Z (1u << 30)
#define CF_C (1u << 29)
#define CF_V (1u << 28)

static inline void set_nz(u32 result)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  reg[REG_CPSR] = cpsr;
}

static inline void set_nzcv(u32 result, u32 c, u32 v)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C | CF_V);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (c)                    cpsr |= CF_C;
  if (v)                    cpsr |= CF_V;
  reg[REG_CPSR] = cpsr;
}

/* ARM add-with-carry: a + b + cin, with full NZCV. All ARM/Thumb arithmetic is
 * expressed as add-with-carry of possibly-inverted operands:
 *   ADD = addc(a,  b, 0)   ADC = addc(a,  b, C)
 *   SUB = addc(a, ~b, 1)   SBC = addc(a, ~b, C)   RSB = addc(~a, b, 1)
 *   CMP = SUB (no write)   CMN = ADD (no write)   NEG = addc(~a, 0, 1) */
static inline u32 do_addc(u32 a, u32 b, u32 cin, int set_flags)
{
  u64 tmp = (u64)a + (u64)b + cin;
  u32 res = (u32)tmp;
  if (set_flags)
    set_nzcv(res, (u32)(tmp >> 32), ((~(a ^ b)) & (a ^ res)) >> 31);
  return res;
}

/* ===================== Thumb single load/store ===================== */

static void cgba_sh4_thumb_ldst_do(u32 opcode, u32 pc)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7;
  u32 rb = (opcode >> 3) & 7;

  if (hi >= 0x48 && hi <= 0x4F) {                  /* LDR Rd,[PC,#imm8*4] */
    u32 addr = ((pc & ~2u) + 4) + ((opcode & 0xFF) * 4);
    cgba_sh4_helper_thumb_ldst_detail(1, addr, 4, CGBA_SH4_TLD_SRC_PC);
    reg[rd] = execute_load_u32(addr);
    return;
  }
  if (hi >= 0x50 && hi <= 0x5F) {                  /* reg-offset forms */
    u32 ro = (opcode >> 6) & 7;
    u32 addr = reg[rb] + reg[ro];
    if (opcode & 0x0200) {                          /* format 8: H/S */
      switch ((opcode >> 10) & 3) {
      case 0:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 2, CGBA_SH4_TLD_SRC_REG);
        execute_store_u16(addr, reg[rd]);
        break;                                             /* STRH */
      case 1:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 1, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_s8(addr);
        break;                                             /* LDRSB */
      case 2:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 2, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u16(addr);
        break;                                             /* LDRH */
      case 3:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 2, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_s16(addr);
        break;                                             /* LDRSH */
      }
    } else {                                        /* format 7: L/B */
      switch ((opcode >> 10) & 3) {
      case 0:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 4, CGBA_SH4_TLD_SRC_REG);
        execute_store_u32(addr, reg[rd]);
        break;                                             /* STR */
      case 1:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 1, CGBA_SH4_TLD_SRC_REG);
        execute_store_u8(addr, reg[rd]);
        break;                                             /* STRB */
      case 2:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 4, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u32(addr);
        break;                                             /* LDR */
      case 3:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 1, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u8(addr);
        break;                                             /* LDRB */
      }
    }
    return;
  }
  if (hi >= 0x60 && hi <= 0x7F) {                  /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    u32 is_load = (opcode >> 11) & 1;
    u32 addr = reg[rb] + (is_byte ? imm5 : imm5 * 4);
    cgba_sh4_helper_thumb_ldst_detail(is_load, addr, is_byte ? 1 : 4,
      CGBA_SH4_TLD_SRC_IMM);
    if (is_byte) {
      if (is_load) reg[rd] = execute_load_u8(addr);
      else         execute_store_u8(addr, reg[rd]);
    } else {
      if (is_load) reg[rd] = execute_load_u32(addr);
      else         execute_store_u32(addr, reg[rd]);
    }
    return;
  }
  if (hi >= 0x80 && hi <= 0x8F) {                  /* LDRH/STRH imm5*2 */
    u32 addr = reg[rb] + ((opcode >> 6) & 0x1F) * 2;
    cgba_sh4_helper_thumb_ldst_detail((opcode & 0x0800) != 0, addr, 2,
      CGBA_SH4_TLD_SRC_IMM);
    if (opcode & 0x0800) reg[rd] = execute_load_u16(addr);
    else                 execute_store_u16(addr, reg[rd]);
    return;
  }
  if (hi >= 0x90 && hi <= 0x9F) {                  /* LDR/STR [SP,#imm8*4] */
    u32 rdsp = (opcode >> 8) & 7;
    u32 addr = reg[REG_SP] + (opcode & 0xFF) * 4;
    cgba_sh4_helper_thumb_ldst_detail((opcode & 0x0800) != 0, addr, 4,
      CGBA_SH4_TLD_SRC_SP);
    if (opcode & 0x0800) reg[rdsp] = execute_load_u32(addr);
    else                 execute_store_u32(addr, reg[rdsp]);
    return;
  }
}

/* Thumb single load/store never targets PC; run it, then break out of the block
 * if a store raised an alert (DMA/HALT idle, IRQ, SMC). */
int cgba_sh4_thumb_ldst(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_ldst);
  cgba_sh4_begin_mem_helper(pc + 2);
  cgba_sh4_reset_mem_cycles(0);
  cgba_sh4_thumb_ldst_do(opcode, pc);
  sh4_headless_trace_op('L', pc, opcode);
  return cgba_store_alert_break(pc + 2);
}

/* ===================== Thumb block (PUSH/POP/LDMIA/STMIA) =========== */

int cgba_sh4_thumb_block(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_block);
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rlist = opcode & 0xFF;
  int wrote_pc = 0;
  int i;

  cgba_sh4_begin_mem_helper(pc + 2);
  cgba_sh4_reset_mem_cycles(1);

  if (hi == 0xB4 || hi == 0xB5) {                  /* PUSH {rlist[, lr]} */
    u32 sp = reg[REG_SP];
    u32 count = 0;
    for (i = 0; i < 8; i++) if (rlist & (1 << i)) count++;
    if (hi == 0xB5) count++;                         /* lr */
    sp -= count * 4;
    reg[REG_SP] = sp;
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { execute_store_u32(sp, reg[i]); sp += 4; }
    if (hi == 0xB5) execute_store_u32(sp, reg[REG_LR]);
    return cgba_store_alert_break(pc + 2);
  }
  if (hi == 0xBC || hi == 0xBD) {                  /* POP {rlist[, pc]} */
    u32 sp = reg[REG_SP];
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { reg[i] = execute_load_u32(sp); sp += 4; }
    if (hi == 0xBD) { reg[REG_PC] = execute_load_u32(sp) & ~1u; sp += 4; wrote_pc = 1; }
    reg[REG_SP] = sp;
    return wrote_pc;
  }
  if (hi >= 0xC0 && hi <= 0xCF) {                  /* STMIA/LDMIA Rb!,{rlist} */
    u32 rb = (opcode >> 8) & 7;
    u32 addr = reg[rb];
    u32 is_load = (opcode >> 11) & 1;
    if (is_load) {
      /* Writeback FIRST (exec_thumb_block_mem parity): with the base in the
         rlist the LOADED value must win, not the incremented base. The
         fastmem block routine already matches this; this slow path serves
         the same emitted sites on guard failure and must agree. */
      u32 end = addr;
      for (i = 0; i < 8; i++)
        if (rlist & (1 << i))
          end += 4;
      reg[rb] = end;
      for (i = 0; i < 8; i++)
        if (rlist & (1 << i)) { reg[i] = execute_load_u32(addr); addr += 4; }
      return cgba_store_alert_break(pc + 2);
    }
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { execute_store_u32(addr, reg[i]); addr += 4; }
    reg[rb] = addr;
    return cgba_store_alert_break(pc + 2);
  }
  return cgba_store_alert_break(pc + 2);
}

/* ===================== Thumb register shift (LSL/LSR/ASR/ROR Rd,Rs) =
 * Real SH4 SHLD/SHAD only use the low 5 bits + sign, so ARM amounts >= 32 and
 * ROR-as-rotate must be done in C. Sets N/Z/C exactly. */
void cgba_sh4_thumb_shift_reg(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_shift);
  u32 rd = opcode & 7;
  u32 rs = (opcode >> 3) & 7;
  u32 sub = (opcode >> 6) & 0xF;     /* 0x2=LSL 0x3=LSR 0x4=ASR 0x7=ROR */
  u32 val = reg[rd];
  u32 amount = reg[rs] & 0xFF;
  u32 result = val, carry = (reg[REG_CPSR] >> 29) & 1, cpsr;
  (void)pc;

  switch (sub) {
  case 0x2: /* LSL */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (32 - amount)) & 1; result = val << amount; }
    else if (amount == 32) { carry = val & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x3: /* LSR */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = val >> amount; }
    else if (amount == 32) { carry = (val >> 31) & 1; result = 0; }
    else                   { carry = 0; result = 0; }
    break;
  case 0x4: /* ASR */
    if (amount == 0) {}
    else if (amount < 32)  { carry = (val >> (amount - 1)) & 1; result = (u32)((s32)val >> amount); }
    else                   { carry = (val >> 31) & 1; result = (u32)((s32)val >> 31); }
    break;
  default: { /* ROR */
    u32 a = amount & 31;
    if (amount == 0) {}
    else if (a == 0) { carry = (val >> 31) & 1; }
    else { carry = (val >> (a - 1)) & 1; result = (val >> a) | (val << (32 - a)); }
    break;
  }
  }

  reg[rd] = result;
  cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (carry)                cpsr |= CF_C;
  reg[REG_CPSR] = cpsr;
}

/* Thumb format 1 immediate shift (LSL/LSR/ASR Rd,Rs,#imm5), N/Z/C. */
void cgba_sh4_thumb_shift_imm(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_shift);
  u32 op = (opcode >> 11) & 3;       /* 0=LSL 1=LSR 2=ASR */
  u32 imm5 = (opcode >> 6) & 0x1F;
  u32 rs = (opcode >> 3) & 7, rd = opcode & 7;
  u32 val = reg[rs];
  u32 result = val, carry = (reg[REG_CPSR] >> 29) & 1, cpsr;
  (void)pc;

  switch (op) {
  case 0:                                            /* LSL #imm (0 = no-op) */
    if (imm5) { carry = (val >> (32 - imm5)) & 1; result = val << imm5; }
    break;
  case 1:                                            /* LSR (#0 means #32) */
    if (imm5 == 0) { carry = (val >> 31) & 1; result = 0; }
    else { carry = (val >> (imm5 - 1)) & 1; result = val >> imm5; }
    break;
  default:                                           /* ASR (#0 means #32) */
    if (imm5 == 0) { carry = (val >> 31) & 1; result = (u32)((s32)val >> 31); }
    else { carry = (val >> (imm5 - 1)) & 1; result = (u32)((s32)val >> imm5); }
    break;
  }

  reg[rd] = result;
  cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (carry)                cpsr |= CF_C;
  reg[REG_CPSR] = cpsr;
}

/* ===================== Thumb data-processing (full NZCV) =============
 * The native inline emitters set only N/Z (and mishandle ADC/SBC carry-in),
 * so the Thumb data-proc family routes here for correct N/Z/C/V and carry-in
 * until inline flag synthesis (the status doc's lazy/dead-flag path) lands.
 * Returns 1 if it wrote PC (hi-register ADD/MOV) so the caller re-dispatches. */
int cgba_sh4_thumb_dp(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_dp);
  u32 hi  = (opcode >> 8) & 0xFF;
  u32 cin = (reg[REG_CPSR] >> 29) & 1;

  if (hi >= 0x18 && hi <= 0x1F) {                    /* fmt2 ADD/SUB rd,rs,rn|imm3 */
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 b = (opcode & 0x0400) ? ((opcode >> 6) & 7) : reg[(opcode >> 6) & 7];
    u32 a = reg[rs];
    reg[rd] = (opcode & 0x0200) ? do_addc(a, ~b, 1, 1) : do_addc(a, b, 0, 1);
    return 0;
  }
  if (hi >= 0x20 && hi <= 0x3F) {                    /* fmt3 MOV/CMP/ADD/SUB rd,#imm8 */
    u32 rd = (opcode >> 8) & 7, imm = opcode & 0xFF, a = reg[rd];
    switch ((opcode >> 11) & 3) {
    case 0: reg[rd] = imm; set_nz(imm); break;            /* MOV */
    case 1: do_addc(a, ~imm, 1, 1); break;                /* CMP */
    case 2: reg[rd] = do_addc(a, imm, 0, 1); break;       /* ADD */
    default: reg[rd] = do_addc(a, ~imm, 1, 1); break;     /* SUB */
    }
    return 0;
  }
  if (hi >= 0x40 && hi <= 0x43) {                    /* fmt4 ALU (non-shift) */
    u32 rd = opcode & 7, rs = (opcode >> 3) & 7;
    u32 a = reg[rd], b = reg[rs];
    switch ((opcode >> 6) & 0xF) {
    case 0x0: reg[rd] = a & b;  set_nz(reg[rd]); break;   /* AND */
    case 0x1: reg[rd] = a ^ b;  set_nz(reg[rd]); break;   /* EOR */
    case 0x5: reg[rd] = do_addc(a, b, cin, 1); break;     /* ADC */
    case 0x6: reg[rd] = do_addc(a, ~b, cin, 1); break;    /* SBC */
    case 0x8: set_nz(a & b); break;                       /* TST */
    case 0x9: reg[rd] = do_addc(~b, 0, 1, 1); break;      /* NEG = 0 - rs */
    case 0xA: do_addc(a, ~b, 1, 1); break;                /* CMP */
    case 0xB: do_addc(a, b, 0, 1); break;                 /* CMN */
    case 0xC: reg[rd] = a | b;  set_nz(reg[rd]); break;   /* ORR */
    case 0xD: reg[rd] = a * b;  set_nz(reg[rd]); break;   /* MUL */
    case 0xE: reg[rd] = a & ~b; set_nz(reg[rd]); break;   /* BIC */
    case 0xF: reg[rd] = ~b;     set_nz(reg[rd]); break;   /* MVN */
    default: break;  /* 0x2/0x3/0x4/0x7 shifts routed to shift helpers */
    }
    return 0;
  }
  if (hi >= 0x44 && hi <= 0x46) {                    /* fmt5 hi-reg ADD/CMP/MOV */
    u32 op = (opcode >> 8) & 3;                       /* 0 ADD, 1 CMP, 2 MOV */
    u32 rd = (opcode & 7) | ((opcode >> 4) & 8);
    u32 rs = (opcode >> 3) & 0xF;
    u32 a = (rd == 15) ? (pc + 4) : reg[rd];          /* Thumb R15 reads PC+4 */
    u32 b = (rs == 15) ? (pc + 4) : reg[rs];
    u32 res;
    if (op == 1) {                                    /* CMP sets flags only */
      do_addc(a, ~b, 1, 1);
      sh4_headless_trace_op('D', pc, opcode);
      return 0;
    }
    res = (op == 0) ? (a + b) : b;                     /* ADD / MOV: no flags */
    if (rd == 15) {
      reg[REG_PC] = res & ~1u;
      sh4_headless_trace_op('D', pc, opcode);
      return 1;
    }
    reg[rd] = res;
    sh4_headless_trace_op('D', pc, opcode);
    return 0;
  }
  return 0;
}

/* ===================== ARM single data transfer ==================== */

static u32 arm_shifter_operand(u32 opcode, u32 pc, u32 *carry_out)
{
  /* Register-or-immediate shifter operand for ARM data-processing / LDR. */
  u32 cpsr_c = (reg[REG_CPSR] >> 29) & 1;
  if (opcode & 0x02000000) {                       /* immediate (rotated) */
    u32 imm = opcode & 0xFF;
    u32 rot = ((opcode >> 8) & 0xF) * 2;
    u32 v = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
    *carry_out = rot ? ((v >> 31) & 1) : cpsr_c;
    return v;
  } else {                                         /* register shift */
    u32 rm = opcode & 0xF;
    u32 by_reg = opcode & 0x10;                     /* shift amount in a reg */
    /* R15 reads PC+8, or PC+12 when the shift amount is register-specified
       (the extra pipeline cycle advances PC one instruction further). */
    u32 val = (rm == 15) ? (pc + (by_reg ? 12 : 8)) : reg[rm];
    u32 type = (opcode >> 5) & 3;
    u32 amount;
    if (by_reg) {                                   /* shift by register */
      u32 rs = (opcode >> 8) & 0xF;
      amount = reg[rs] & 0xFF;
      if (amount == 0) { *carry_out = cpsr_c; return val; } /* reg-#0: no-op */
    } else {
      amount = (opcode >> 7) & 0x1F;
    }
    switch (type) {
    case 0: /* LSL */
      if (amount == 0) { *carry_out = cpsr_c; return val; }
      if (amount < 32) { *carry_out = (val >> (32 - amount)) & 1; return val << amount; }
      *carry_out = (amount == 32) ? (val & 1) : 0; return 0;
    case 1: /* LSR */
      if (amount == 0) amount = 32;
      if (amount < 32) { *carry_out = (val >> (amount - 1)) & 1; return val >> amount; }
      *carry_out = (amount == 32) ? ((val >> 31) & 1) : 0; return 0;
    case 2: /* ASR */
      if (amount == 0 || amount >= 32) { *carry_out = (val >> 31) & 1; return (u32)((s32)val >> 31); }
      *carry_out = (val >> (amount - 1)) & 1; return (u32)((s32)val >> amount);
    default: /* ROR / RRX */
      if (amount == 0) { u32 r = (cpsr_c << 31) | (val >> 1); *carry_out = val & 1; return r; }
      amount &= 31;
      if (amount == 0) { *carry_out = (val >> 31) & 1; return val; }
      *carry_out = (val >> (amount - 1)) & 1;
      return (val >> amount) | (val << (32 - amount));
    }
  }
}

#ifdef CGBA_GPSP_HEADLESS_TEST
/* Which guest instructions still reach the C single-transfer helper (emit
 * bail or fastmem guard failure)? Small direct-mapped pc-keyed table. */
u32 cgba_armldst_fb_pc[16], cgba_armldst_fb_op[16], cgba_armldst_fb_n[16];
#endif

int cgba_sh4_arm_ldst(u32 opcode, u32 pc)
{
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  {
    u32 slot = (pc * 2654435761u) >> 28;
    if (cgba_armldst_fb_n[slot] == 0 || cgba_armldst_fb_pc[slot] == pc) {
      cgba_armldst_fb_pc[slot] = pc;
      cgba_armldst_fb_op[slot] = opcode;
      cgba_armldst_fb_n[slot]++;
    }
  }
#endif
  CGBA_SH4_HELPER_HIT(arm_ldst);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 base = (rn == 15) ? (pc + 8) : reg[rn];
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 offset, addr;
  int is_half = 0, is_byte = 0, signed_ld = 0, half_w = 0;

  cgba_sh4_begin_mem_helper(pc + 4);
  cgba_sh4_reset_mem_cycles(0);

  if ((opcode & 0x0E000090) == 0x00000090) {
    /* halfword / signed-byte transfer (bits 27..25 = 000, bits 7,4 = 1) */
    is_half = 1;
    signed_ld = (opcode >> 6) & 1;
    half_w = (opcode >> 5) & 1;
    if (opcode & 0x00400000) offset = ((opcode >> 4) & 0xF0) | (opcode & 0x0F); /* imm */
    else                     offset = reg[opcode & 0xF];                        /* reg */
  } else {
    is_byte = (opcode >> 22) & 1;
    if (opcode & 0x02000000) {                       /* register, shifted by imm */
      u32 rm = reg[opcode & 0xF];
      u32 type = (opcode >> 5) & 3, sh = (opcode >> 7) & 0x1F;
      switch (type) {
      case 0: offset = sh ? (rm << sh) : rm; break;
      case 1: offset = sh ? (rm >> sh) : 0; break;
      case 2: offset = (u32)((s32)rm >> (sh ? sh : 31)); break;
      default: offset = sh ? ((rm >> sh) | (rm << (32 - sh)))
                           : (((reg[REG_CPSR] >> 29) & 1) << 31) | (rm >> 1); break;
      }
    } else {
      offset = opcode & 0xFFF;                        /* 12-bit immediate */
    }
  }

  addr = pre ? (up ? base + offset : base - offset) : base;
  cgba_sh4_helper_arm_ldst_detail(is_load, addr);

  if (is_load) {
    u32 v;
    if (is_half) {
      if (signed_ld && half_w)  v = execute_load_s16(addr);
      else if (signed_ld)       v = execute_load_s8(addr);
      else                      v = execute_load_u16(addr);
    } else {
      v = is_byte ? execute_load_u8(addr) : execute_load_u32(addr);
    }
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
    reg[rd] = v;
    if (rd == 15) { reg[REG_PC] = v & ~1u; return 1; }
  } else {
    u32 v = (rd == 15) ? (pc + 12) : reg[rd];
    if (is_half) execute_store_u16(addr, v);
    else if (is_byte) execute_store_u8(addr, v);
    else execute_store_u32(addr, v);
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
  }
  return cgba_store_alert_break(pc + 4);
}

/* ===================== CPSR / SPSR write (gpSP canonical) ==========
 * Every mode-changing write goes through these so the register banking stays in
 * one place — the same set_cpu_mode + check_for_interrupts path the interpreter
 * uses (cpu.cc). Doing the re-bank ad hoc per call site is what desynced
 * reg[CPU_MODE] before. The IRQ gate reads IE/IF/IME through read_ioreg (the
 * guest is little-endian, the SH4 host is big-endian), unlike the x86 backend
 * which can index io_registers[] directly. */

/* set_cpu_mode + check_for_interrupts: re-bank to the mode now in reg[REG_CPSR]
 * and, if that just unmasked a pending IRQ, enter it (LR_irq = next_pc + 4,
 * SPSR_irq = old CPSR, CPSR = 0xD2, switch to IRQ mode). Returns the IRQ vector
 * to redispatch to, or 0 if no IRQ was taken. */
static u32 sh4_rebank_and_irq(u32 next_pc)
{
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && read_ioreg(REG_IME) &&
      ((reg[REG_CPSR] & 0x80) == 0)) {
    REG_MODE(MODE_IRQ)[6] = next_pc + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    set_cpu_mode(MODE_IRQ);
    return 0x00000018;
  }
  return 0;
}

/* gpSP execute_spsr_restore: exception return (SUBS pc,... / LDM{pc}^). In a
 * privileged non-system mode, restore CPSR from the mode's SPSR, re-bank, take a
 * now-pending IRQ, and fold the restored Thumb bit (bit0) into the address. In
 * USER/SYSTEM there is no SPSR, so the address passes through unchanged. */
u32 execute_spsr_restore(u32 address)
{
  if (reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM) {
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    if (sh4_rebank_and_irq(address))
      address = 0x00000018;
    else if (reg[REG_CPSR] & 0x20)
      address |= 0x01;
  }
  return address;
}

/* ===================== ARM block (LDM/STM) ========================= */

int cgba_sh4_arm_block(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_block);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rlist = opcode & 0xFFFF;
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 s_bit = (opcode >> 22) & 1;
  u32 base = reg[rn];
  u32 count = 0, addr, new_base, i, lowest = 16;
  int wrote_pc = 0;
  /* S-bit LDM/STM transfers the USER-mode banked registers (r8-r14). Match the
     gpSP interpreter (cpu.cc exec_arm_block_mem ~1010/1041): bracket the transfer
     in USER mode whenever the S bit is set AND it is a store OR rn != r15 -- so
     an LDM{pc}^ exception return (S=1, pc in list, rn != 15) lands the popped user
     r13/r14 in the USER bank and the SPSR re-bank below (execute_spsr_restore ->
     set_cpu_mode(USER)) reloads THEM instead of stale USER sp/lr. (The old code
     transferred the current/privileged bank, diverging from the interpreter for
     this exception-return case; not the trigger of the frame-700 freeze, which is
     a control-flow divergence, but a genuine latent bug.) The native fast path
     bails on the S bit, so this C helper handles every S-bit block transfer. */
  int user_bank = s_bit && (!is_load || rn != 15);
  u32 old_cpsr = reg[REG_CPSR];

  cgba_sh4_begin_mem_helper(pc + 4);
  cgba_sh4_reset_mem_cycles(1);
  cgba_sh4_helper_arm_block_detail(is_load);

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) { count++; if (lowest == 16) lowest = i; }

  new_base = up ? base + count * 4 : base - count * 4;
  addr = (up ? base : new_base) & ~3u;   /* word-align; traverse ascending */
  if (up == 0) pre = !pre;

  if (user_bank) set_cpu_mode(MODE_USER);   /* transfer the USER bank (r13/r14) */

  for (i = 0; i < 16; i++) {
    if (!(rlist & (1u << i))) continue;
    if (pre) addr += 4;
    if (is_load) {
      reg[i] = execute_load_u32(addr);
      if (i == 15) { reg[REG_PC] = reg[15] & ~1u; wrote_pc = 1; }
    } else {
      /* STM stores PC+12 for r15; for the base reg, the OLD base is stored only
         when it is the lowest in the list, otherwise the written-back value. */
      u32 v = (i == 15) ? (pc + 12)
            : (i == rn && writeback && i != lowest) ? new_base
            : reg[i];
      execute_store_u32(addr, v);
    }
    if (!pre) addr += 4;
  }

  /* LDM with the base in the list keeps the loaded value (no writeback). */
  if (writeback && !(is_load && (rlist & (1u << rn))))
    reg[rn] = new_base;

  if (user_bank) set_cpu_mode(cpu_modes[old_cpsr & 0xF]);   /* back to entry mode */

  /* LDM{pc}^ (exception return): restore CPSR from SPSR, re-bank, take any now-
   * pending IRQ — the canonical execute_spsr_restore path. */
  if (wrote_pc && s_bit) {
    u32 next = execute_spsr_restore(reg[15]);
    reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
  }
  /* `|` not `||`: always consume the alert. LDM is loads (no alert) so the break
   * is a no-op for wrote_pc; STM stores, where the break may exit the block. */
  return wrote_pc | cgba_store_alert_break(pc + 4);
}

/* ===================== ARM data-processing ========================= */

#ifdef CGBA_GPSP_HEADLESS_TEST
/* dp fallback mix: [op nibble] and reason flags */
u32 cgba_dp_fb_op[16];
u32 cgba_dp_fb_pc, cgba_dp_fb_regshift, cgba_dp_fb_ror, cgba_dp_fb_s;
#endif

int cgba_sh4_arm_dp(u32 opcode, u32 pc)
{
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_dp_fb_op[(opcode >> 21) & 0xF]++;
  if (((opcode >> 12) & 0xF) == 15 || ((opcode >> 16) & 0xF) == 15 ||
      (!(opcode & 0x02000000) && (opcode & 0xF) == 15))
    cgba_dp_fb_pc++;
  else if (!(opcode & 0x02000000) && (opcode & 0x10))
    cgba_dp_fb_regshift++;
  else if (!(opcode & 0x02000000) && ((opcode >> 5) & 3) == 3 && (opcode & 0xFF0))
    cgba_dp_fb_ror++;
  if ((opcode >> 20) & 1) cgba_dp_fb_s++;
#endif
  CGBA_SH4_HELPER_HIT(arm_dp);
  u32 op = (opcode >> 21) & 0xF;
  u32 set_flags = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  /* With a register-controlled shift (bit25 clear, bit4 set), R15 reads PC+12;
     otherwise PC+8. arm_shifter_operand applies the same rule to Rm. */
  u32 reg_shift = ((opcode & 0x02000010) == 0x00000010);
  u32 a = (rn == 15) ? (pc + (reg_shift ? 12 : 8)) : reg[rn];
  u32 carry = (reg[REG_CPSR] >> 29) & 1;
  u32 b = arm_shifter_operand(opcode, pc, &carry);
  u32 cf = carry, vf = (reg[REG_CPSR] >> 28) & 1, res = 0;
  u32 oldc = (reg[REG_CPSR] >> 29) & 1;
  int writes = 1;
  u64 tmp;

  switch (op) {
  case 0x0: res = a & b; break;                    /* AND */
  case 0x1: res = a ^ b; break;                    /* EOR */
  case 0x2: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SUB */
  case 0x3: tmp = (u64)b - (u64)a; res = (u32)tmp; cf = b >= a;
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSB */
  case 0x4: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADD */
  case 0x5: tmp = (u64)a + (u64)b + oldc; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADC */
  case 0x6: tmp = (u64)a - (u64)b - (1 - oldc); res = (u32)tmp; cf = a >= ((u64)b + (1 - oldc));
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SBC */
  case 0x7: tmp = (u64)b - (u64)a - (1 - oldc); res = (u32)tmp; cf = b >= ((u64)a + (1 - oldc));
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSC */
  case 0x8: res = a & b; writes = 0; break;        /* TST */
  case 0x9: res = a ^ b; writes = 0; break;        /* TEQ */
  case 0xA: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; writes = 0; break;  /* CMP */
  case 0xB: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; writes = 0; break; /* CMN */
  case 0xC: res = a | b; break;                    /* ORR */
  case 0xD: res = b; break;                        /* MOV */
  case 0xE: res = a & ~b; break;                   /* BIC */
  case 0xF: res = ~b; break;                       /* MVN */
  }

  if (writes) {
    reg[rd] = res;
    if (rd == 15) {
      /* Writing PC with the S bit (e.g. SUBS pc,lr,#4 — the IRQ/exception
       * return) restores CPSR from SPSR and re-banks via execute_spsr_restore,
       * which also folds the restored Thumb bit into bit0. Mask afterwards so
       * the new Thumb state picks the alignment. */
      u32 next = set_flags ? execute_spsr_restore(res) : res;
      reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
      return 1;
    }
    if (set_flags) set_nzcv(res, cf, vf & 1);
  } else if (set_flags) {
    set_nzcv(res, cf, vf & 1);
  }
  return 0;
}

/* ===================== ARM multiply / psr / swap (TODO inline) ====== */

void cgba_sh4_arm_multiply(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_mul);
  u32 rd = (opcode >> 16) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 rn = (opcode >> 12) & 0xF;
  u32 res = reg[rm] * reg[rs];
  (void)pc;
  if (opcode & 0x00200000) res += reg[rn];          /* MLA */
  reg[rd] = res;
  if (opcode & 0x00100000) set_nz(res);             /* S bit */
}

void cgba_sh4_arm_multiply_long(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_mul);
  u32 rdhi = (opcode >> 16) & 0xF;
  u32 rdlo = (opcode >> 12) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_signed = (opcode >> 22) & 1;
  u32 accumulate = (opcode >> 21) & 1;
  u64 res;
  (void)pc;
  if (is_signed) res = (u64)((s64)(s32)reg[rm] * (s64)(s32)reg[rs]);
  else           res = (u64)reg[rm] * (u64)reg[rs];
  if (accumulate) res += (((u64)reg[rdhi]) << 32) | reg[rdlo];
  reg[rdlo] = (u32)res;
  reg[rdhi] = (u32)(res >> 32);
  if (opcode & 0x00100000) {                         /* S bit */
    u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
    if (res & 0x8000000000000000ull) cpsr |= CF_N;
    if (res == 0) cpsr |= CF_Z;
    reg[REG_CPSR] = cpsr;
  }
}

int cgba_sh4_arm_psr(u32 opcode, u32 pc)
{
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  {
    extern u32 cgba_psr_fb[8];
    /* [0]=mrs [1]=msr-imm [2]=msr-reg-c [3]=msr-reg-f [4]=msr-reg-cf
       [5]=msr-spsr [6]=other */
    u32 is_msr = (opcode >> 21) & 1;
    u32 spsr_sel = (opcode >> 22) & 1;
    u32 fields = (opcode >> 16) & 0xF;
    if (!is_msr) cgba_psr_fb[0]++;
    else if (spsr_sel) cgba_psr_fb[5]++;
    else if (opcode & 0x02000000) cgba_psr_fb[1]++;
    else if (fields == 1) cgba_psr_fb[2]++;
    else if (fields == 8) cgba_psr_fb[3]++;
    else if (fields == 9) cgba_psr_fb[4]++;
    else cgba_psr_fb[6]++;
  }
#endif

  CGBA_SH4_HELPER_HIT(arm_psr);
  u32 is_msr   = (opcode >> 21) & 1;   /* 0 = MRS (read), 1 = MSR (write) */
  u32 use_spsr = (opcode >> 22) & 1;   /* 0 = CPSR, 1 = SPSR of cur mode  */

  if (!is_msr) {                                     /* MRS Rd, <psr> */
    u32 rd = (opcode >> 12) & 0xF;
    reg[rd] = use_spsr ? REG_SPSR(reg[CPU_MODE]) : reg[REG_CPSR];
    return 0;
  }

  {                                                  /* MSR <psr>, val */
    /* Field mask is privilege-aware (gpSP's cpsr_masks): in USER mode the
       control byte is restricted to the Thumb bit, so USER code cannot change
       the mode bits. pfield bit0 = control field, bit1 = flags field. */
    u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
    u32 val, mask;
    if (opcode & 0x02000000) {
      u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
      val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else {
      val = reg[opcode & 0xF];
    }

    if (use_spsr) {
      mask = spsr_masks[pfield];
      REG_SPSR(reg[CPU_MODE]) = (val & mask) | (REG_SPSR(reg[CPU_MODE]) & ~mask);
    } else {
      /* CPSR write through the canonical bank path. When the control byte is
       * written (mode/IRQ-disable change) re-bank and, if an IRQ just unmasked,
       * take it and redispatch — gpSP's execute_store_cpsr. */
      mask = cpsr_masks[pfield][PRIVMODE(reg[CPU_MODE])];
      reg[REG_CPSR] = (val & mask) | (reg[REG_CPSR] & ~mask);
      if (mask & 0xFF) {
        u32 vec = sh4_rebank_and_irq(pc + 4);
        if (vec) { reg[REG_PC] = vec; return 1; }
      }
    }
  }
  return 0;
}

int cgba_sh4_arm_swap(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_swap);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_byte = (opcode >> 22) & 1;
  u32 addr = reg[rn];
  /* SWP's interpreter advances PC after the read/write pair. */
  cgba_sh4_begin_mem_helper(pc);
  cgba_sh4_reset_mem_cycles(0);
  if (is_byte) {
    u32 tmp = execute_load_u8(addr);
    execute_store_u8(addr, reg[rm]);
    reg[rd] = tmp;
  } else {
    u32 tmp = execute_load_u32(addr);
    execute_store_u32(addr, reg[rm]);
    reg[rd] = tmp;
  }
  /* The store may have hit I/O (IRQ/HALT) or code (SMC); drain it so the block
   * exits with PC at the next instruction, like the normal store helpers. */
  return cgba_store_alert_break(pc + 4);
}

/* SWI 0x06/0x07 divide HLE.
 * The bundled open BIOS dispatcher saves/restores r2/r3 around every SWI, so
 * the observable Div/DivArm result is only r0/r1. Preserve r3 even though the
 * official BIOS contract exposes ABS(quotient) there; the interpreter runs the
 * same bundled BIOS and Yoshi depends on that exact scratch-register state. */
static u32 cgba_sh4_abs_s32_bits(s32 v)
{
  return (v < 0) ? (0u - (u32)v) : (u32)v;
}

static u32 cgba_sh4_hle_div_cycles(u32 divarm, u32 pc, s32 num, s32 den)
{
  u32 s0 = (u32)ws_cyc_seq[0][1];
  u32 n0 = (u32)ws_cyc_nseq[0][1];
  int thumb = (reg[REG_CPSR] & 0x20u) != 0;
  u32 lr = pc + (thumb ? 2u : 4u);
  u32 cycles = cgba_swi_dispatch_cycles(reg[REG_SP] >> 24, lr >> 24, thumb);
  u32 align = 0;
  u32 n, d;

  if (divarm)
    cycles += 4u * s0 + n0;   /* 1798..17A4 operand swap + B Div */

  if (den == 0)
    return 64u;               /* legacy undefined-input charge */

  n = cgba_sh4_abs_s32_bits(num);
  d = cgba_sh4_abs_s32_bits(den);
  while (d <= n && align < 31u) {
    d <<= 1;
    align++;
  }

  /* Open-BIOS Div body 17A8..17FC. Excludes the body's final BX LR because
   * cgba_swi_dispatch_cycles() already accounts for the dispatcher return path
   * around the routine call. The translated HLE SWI keeps its own instruction
   * fetch charge, and the interpreter's dispatcher/body boundary is one cycle
   * shorter than the straight instruction sum below. Conditional ALU ops still
   * fetch; taken BLS/BCC add nseq BIOS refill cycles. */
  cycles += (22u + 10u * align) * s0 + (2u * align) * n0;
  return cycles - 1u;
}

#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
/* Runtime half of the signature-gated m4a/Sappy IWRAM mixer fast path.  Every
 * access and SMC tag is validated before the first write.  The helper folds a
 * taken CMP/BCC only while it leaves the snapshotted JIT budget positive; the
 * final CMP/BCC always remains on the ordinary translated path. */
volatile s32 cgba_sh4_arm_mixer_budget;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_arm_mixer_try_count;
u32 cgba_sh4_arm_mixer_hit_count;
u32 cgba_sh4_arm_mixer_batch_count;
u32 cgba_sh4_arm_mixer_guard_fallback_count;
u32 cgba_sh4_arm_mixer_budget_stop_count;
u32 cgba_sh4_arm_mixer_first_pc;
u32 cgba_sh4_arm_mixer_first_reg[10];
#endif

typedef struct cgba_sh4_arm_mixer_prepared {
  u8 *source_page[4];
  u16 source_offset[4];
  u8 source_cost[4];
  u32 body_cycles;
} cgba_sh4_arm_mixer_prepared;

static int cgba_sh4_arm_mixer_prepare(
  const cgba_sh4_arm_mixer_state *s, u32 seq_word_cost,
  cgba_sh4_arm_mixer_prepared *p)
{
  u32 phase = s->r4;
  u32 i;

  for (i = 0; i < 4; i++) {
    u32 source = s->r0 + (phase >> 8);
    u32 left = s->r1 + i * 2u;
    u32 right = s->r2 + i * 2u;
    u32 left_off, right_off;
    u8 *page;

    if (!cgba_sh4_arm_mixer_source_fast_ok(source) ||
        (((source >> 24) >= 8u) &&
         ((source & 0x01FFFFFFu) >= gamepak_size)) ||
        !cgba_sh4_arm_mixer_output_fast_ok(left) ||
        !cgba_sh4_arm_mixer_output_fast_ok(right))
      return 0;
    page = memory_map_read[source >> 15];
    if (!page)
      return 0;

    left_off = left & 0x7FFFu;
    right_off = right & 0x7FFFu;
    if (iwram[left_off] || iwram[left_off + 1u] ||
        iwram[right_off] || iwram[right_off + 1u])
      return 0;

    p->source_page[i] = page;
    p->source_offset[i] = (u16)(source & 0x7FFFu);
    p->source_cost[i] = ws_cyc_nseq[(source >> 24) & 0x0Fu][0];
    phase += s->r5;
  }
  p->body_cycles = cgba_sh4_arm_mixer_body_cycles(
    seq_word_cost, p->source_cost, ws_cyc_nseq[3][0]);
  return 1;
}

static void cgba_sh4_arm_mixer_run_prepared(
  cgba_sh4_arm_mixer_state *s, const cgba_sh4_arm_mixer_prepared *p)
{
  u8 *data = iwram + 0x8000;
  u32 i;

  for (i = 0; i < 4; i++) {
    int8_t sample = (int8_t)address8(p->source_page[i], p->source_offset[i]);
    u32 left_off = s->r1 & 0x7FFFu;
    int16_t left = (int16_t)readaddress16(data, left_off);
    uint16_t left_store;
    u32 right_off;
    int16_t right;
    uint16_t right_store;

    cgba_sh4_arm_mixer_step_begin(s, sample);
    left_store = cgba_sh4_arm_mixer_step_left(s, left);
    address16(data, left_off) = eswap16(left_store);

    /* Preserve load-left/store-left/load-right/store-right when buffers alias. */
    right_off = s->r2 & 0x7FFFu;
    right = (int16_t)readaddress16(data, right_off);
    right_store = cgba_sh4_arm_mixer_step_right(s, right);
    address16(data, right_off) = eswap16(right_store);
  }
}

/* All output tags are metadata bytes, so their byte order is immaterial.  The
 * array is 32-byte aligned and output offsets are halfword aligned; peel one
 * halfword when needed, then check four words per iteration. */
static int cgba_sh4_arm_mixer_tags_zero(u32 offset, u32 bytes)
{
  const u32 *words;

  if (offset & 2u) {
    if (address16(iwram, offset))
      return 0;
    offset += 2u;
    bytes -= 2u;
  }

  words = (const u32 *)(const void *)(iwram + offset);
  while (bytes >= 16u) {
    if (words[0] | words[1] | words[2] | words[3])
      return 0;
    words += 4;
    bytes -= 16u;
  }
  while (bytes >= 4u) {
    if (*words++)
      return 0;
    bytes -= 4u;
  }
  if (bytes && *(const u16 *)(const void *)words)
    return 0;
  return 1;
}

/* Zelda's m4a invocation fits eight batches in one scheduler slice.  Prove
 * that complete prefix up front: both outputs are contiguous/tag-free IWRAM
 * and every monotonically increasing sample address stays in one mapped ROM
 * page.  Samples are still loaded at their exact instruction-order position,
 * so even a source mapping that aliases an output buffer observes feedback.
 * Any unusual wrap, mirror, page, ROM-size or tag boundary declines before
 * the first write and uses the conservative per-batch path below. */
static int __attribute__((noinline)) cgba_sh4_arm_mixer_run_whole(
  cgba_sh4_arm_mixer_state *s, u32 seq_word_cost, u32 tail_cycles,
  s32 budget, u32 *spent_out, u32 *batches_out, int *budget_stop_out)
{
  u32 source = cgba_sh4_arm_mixer_source_address(s);
  u32 source_region = source >> 24;
  u32 source_cost;
  u32 body_cycles;
  uint32_t spent;
  u32 batches;
  u32 samples;
  u32 bytes;
  u32 last_phase;
  u32 last_source;
  u32 left_last, right_last;
  u32 left_off, right_off;
  u8 *source_page;
  u16 *left_ptr, *right_ptr;
  u32 phase;
  u32 sample32 = 0;
  u32 mixed = 0;
  u32 i;

  if (!cgba_sh4_arm_mixer_output_fast_ok(s->r1) ||
      !cgba_sh4_arm_mixer_output_fast_ok(s->r2) ||
      source < s->r0 ||
      !cgba_sh4_arm_mixer_source_fast_ok(source) ||
      (source_region >= 8u &&
       (source & 0x01FFFFFFu) >= gamepak_size))
    return 0;

  source_cost = ws_cyc_nseq[source_region & 0x0Fu][0];
  body_cycles = CGBA_SH4_ARM_MIXER_BODY_WORDS * seq_word_cost +
                4u * source_cost + 16u * (u32)ws_cyc_nseq[3][0];
  batches = cgba_sh4_arm_mixer_plan_constant(
    s->r1, s->r3, budget, body_cycles, tail_cycles,
    &spent, budget_stop_out);
  samples = batches * 4u;
  bytes = samples * 2u;

  /* samples <= 32.  This deliberately conservative shift proves that even
   * the maximum 31 increments cannot wrap without a runtime divide. */
  if (s->r5 > ((~s->r4) >> 5))
    return 0;
  last_phase = s->r4 + (samples - 1u) * s->r5;
  last_source = s->r0 + (last_phase >> 8);
  if (last_source < s->r0 ||
      (source >> 15) != (last_source >> 15) ||
      (source_region >= 8u &&
       (last_source & 0x01FFFFFFu) >= gamepak_size))
    return 0;

  source_page = memory_map_read[source >> 15];
  if (!source_page)
    return 0;

  left_last = s->r1 + bytes - 2u;
  right_last = s->r2 + bytes - 2u;
  left_off = s->r1 & 0x7FFFu;
  right_off = s->r2 & 0x7FFFu;
  if (!cgba_sh4_arm_mixer_output_fast_ok(left_last) ||
      !cgba_sh4_arm_mixer_output_fast_ok(right_last) ||
      left_off + bytes > 0x8000u || right_off + bytes > 0x8000u ||
      !cgba_sh4_arm_mixer_tags_zero(left_off, bytes) ||
      !cgba_sh4_arm_mixer_tags_zero(right_off, bytes))
    return 0;

  left_ptr = (u16 *)(void *)(iwram + 0x8000u + left_off);
  right_ptr = (u16 *)(void *)(iwram + 0x8000u + right_off);
  phase = s->r4;
  for (i = 0; i < samples; i++) {
    u32 sample_address = s->r0 + (phase >> 8);
    s16 left;
    s16 right;

    sample32 = (u32)(s32)(s8)address8(source_page,
                                      sample_address & 0x7FFFu);
    left = (s16)eswap16(*left_ptr);
    mixed = s->r6 * sample32 + (u32)(s32)left;
    *left_ptr++ = eswap16((u16)mixed);

    /* Preserve load-left/store-left/load-right/store-right for aliasing
     * stereo buffers and for an IWRAM source page feeding back from them. */
    right = (s16)eswap16(*right_ptr);
    mixed = s->r7 * sample32 + (u32)(s32)right;
    *right_ptr++ = eswap16((u16)mixed);
    phase += s->r5;
  }

  s->r1 += bytes;
  s->r2 += bytes;
  s->r4 = phase;
  s->r8 = sample32;
  s->r12 = mixed;
  *spent_out = spent;
  *batches_out = batches;
  return 1;
}

int cgba_sh4_arm_mixer_loop_try(u32 unused, u32 pc)
{
  cgba_sh4_arm_mixer_state s;
  cgba_sh4_arm_mixer_prepared cur;
  u32 region = (pc >> 24) & 0x0Fu;
  u32 seq_word_cost = ws_cyc_seq[region][1];
  u32 tail_cycles = cgba_sh4_arm_mixer_taken_tail_cycles(
    seq_word_cost, ws_cyc_nseq[region][1]);
  u32 spent = 0;
  u32 batches = 0;
  int whole_budget_stop = 0;
  int whole_hit;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_state initial_s;
#endif
  (void)unused;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_try_count++;
#endif
  if (CGBA_SH4_DIFF_ACTIVE() || (reg[REG_CPSR] & 0x20u) ||
      cgba_sh4_arm_mixer_budget <= 0)
    return 0;

  s.r0 = reg[0]; s.r1 = reg[1]; s.r2 = reg[2]; s.r3 = reg[3];
  s.r4 = reg[4]; s.r5 = reg[5]; s.r6 = reg[6]; s.r7 = reg[7];
  s.r8 = reg[8]; s.r12 = reg[12]; s.cpsr = reg[REG_CPSR];

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  initial_s = s;
#endif

  whole_hit = cgba_sh4_arm_mixer_run_whole(
    &s, seq_word_cost, tail_cycles, cgba_sh4_arm_mixer_budget,
    &spent, &batches, &whole_budget_stop);

  if (!whole_hit && !cgba_sh4_arm_mixer_prepare(&s, seq_word_cost, &cur)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    cgba_sh4_arm_mixer_guard_fallback_count++;
#endif
    return 0;
  }

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  if (!cgba_sh4_arm_mixer_hit_count) {
    cgba_sh4_arm_mixer_first_pc = pc;
    cgba_sh4_arm_mixer_first_reg[0] = initial_s.r0;
    cgba_sh4_arm_mixer_first_reg[1] = initial_s.r1;
    cgba_sh4_arm_mixer_first_reg[2] = initial_s.r2;
    cgba_sh4_arm_mixer_first_reg[3] = initial_s.r3;
    cgba_sh4_arm_mixer_first_reg[4] = initial_s.r4;
    cgba_sh4_arm_mixer_first_reg[5] = initial_s.r5;
    cgba_sh4_arm_mixer_first_reg[6] = initial_s.r6;
    cgba_sh4_arm_mixer_first_reg[7] = initial_s.r7;
    cgba_sh4_arm_mixer_first_reg[8] = initial_s.r8;
    cgba_sh4_arm_mixer_first_reg[9] = initial_s.r12;
  }
  if (whole_hit && whole_budget_stop)
    cgba_sh4_arm_mixer_budget_stop_count++;
#endif

  while (!whole_hit) {
    cgba_sh4_arm_mixer_run_prepared(&s, &cur);
    spent += cur.body_cycles;
    batches++;

    if (s.r1 >= s.r3 ||
        !cgba_sh4_arm_mixer_can_fold_taken(
          cgba_sh4_arm_mixer_budget, spent, tail_cycles)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
      if (s.r1 < s.r3)
        cgba_sh4_arm_mixer_budget_stop_count++;
#endif
      break;
    }

    /* Prove the next batch safe before consuming this taken tail.  A failed
     * proof returns at the current CMP with no partial next-batch state. */
    if (!cgba_sh4_arm_mixer_prepare(&s, seq_word_cost, &cur))
      break;
    spent += tail_cycles;
  }

  reg[1] = s.r1;
  reg[2] = s.r2;
  reg[4] = s.r4;
  reg[8] = s.r8;
  reg[12] = s.r12;
  cgba_sh4_extra_cycles = (int)spent;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_arm_mixer_hit_count++;
  cgba_sh4_arm_mixer_batch_count += batches;
#endif
  return 1;
}
#endif

#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
/*
 * Runtime half of the signature-gated Thumb libgcc divide-loop fast path.
 * Generated code snapshots the live R13 budget here before using the ordinary
 * op2 trampoline, which also makes reg[REG_CPSR] authoritative around the C
 * call.  The model commits nothing unless the whole pure-register loop fits
 * before the next scheduler boundary.  A failed try therefore falls through
 * to the byte-for-byte ordinary translation with unchanged guest state.
 */
volatile s32 cgba_sh4_thumb_udiv_budget;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_thumb_udiv_try_count;
u32 cgba_sh4_thumb_udiv_hit_count;
u32 cgba_sh4_thumb_udiv_budget_fallback_count;
#endif

int cgba_sh4_thumb_udiv_loop_try(u32 unused, u32 pc)
{
  cgba_sh4_thumb_udiv_loop_result out;
  u32 region = (pc >> 24) & 0x0Fu;
  (void)unused;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_thumb_udiv_try_count++;
#endif
  if (CGBA_SH4_DIFF_ACTIVE() || !(reg[REG_CPSR] & 0x20u))
    return 0;

  out = cgba_sh4_thumb_udiv_loop_run(
    reg[0], reg[1], reg[2], reg[3], reg[4], reg[REG_CPSR],
    (u32)ws_cyc_seq[region][0], (u32)ws_cyc_nseq[region][0]);
  if (!cgba_sh4_thumb_udiv_loop_budget_ok(cgba_sh4_thumb_udiv_budget,
                                           out.cycles)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    cgba_sh4_thumb_udiv_budget_fallback_count++;
#endif
    return 0;
  }

  reg[0] = out.r0;
  reg[1] = out.r1;
  reg[2] = out.r2;
  reg[3] = out.r3;
  reg[4] = out.r4;
  reg[REG_CPSR] = out.cpsr;
  cgba_sh4_extra_cycles = (int)out.cycles;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_thumb_udiv_hit_count++;
#endif
  return 1;
}
#endif

void cgba_sh4_hle_div(u32 divarm, u32 pc)
{
  CGBA_SH4_HELPER_HIT(hle_div);
  s32 num = divarm ? (s32)reg[1] : (s32)reg[0];
  s32 den = divarm ? (s32)reg[0] : (s32)reg[1];
  cgba_sh4_extra_cycles = (int)cgba_sh4_hle_div_cycles(divarm, pc, num, den);
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  if (den == 0) {
    cgba_sh4_hle_div_zero_count++;
  } else if (den == 1) {
    cgba_sh4_hle_div_one_count++;
  } else if (den == -1) {
    cgba_sh4_hle_div_neg_one_count++;
  } else {
    u32 abs_den = (den < 0) ? (u32)(-den) : (u32)den;
    if ((abs_den & (abs_den - 1u)) == 0)
      cgba_sh4_hle_div_pow2_count++;
    else
      cgba_sh4_hle_div_other_count++;
  }
#endif
  if (den != 0) {
    s32 q = num / den, r = num % den;
    reg[0] = (u32)q;
    reg[1] = (u32)r;
  }
}

/* SWI trampoline target (sh4_stub.S execute_swi -> here). The stub has already
 * stashed the return address (next-instruction PC) into reg[REG_PC]. Vector to
 * the BIOS SWI handler exactly like the interpreter (cpu.cc:3516): bank into
 * Supervisor mode, save LR_svc / SPSR_svc, switch to ARM with IRQs disabled, set
 * the post-SWI open-bus value, and point PC at the 0x08 vector.
 *
 * Only the mode/bank/vector setup happens here — the emitted block then
 * redispatches to 0x08, because the block scan already registered this exit's
 * target as the BIOS vector (cpu_threaded.c:2957) and arm_swi/thumb_swi branch
 * there. gpSP loads the real open BIOS, so no SWI HLE dispatch is needed (the
 * divide HLE is handled separately by cgba_sh4_hle_div). */
void sh4_swi_handler(void)
{
  u32 return_pc = reg[REG_PC];                              /* stashed by execute_swi */
  REG_MODE(MODE_SUPERVISOR)[6] = return_pc;                 /* LR_svc = return address */
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];                /* SPSR_svc = CPSR */
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3Fu) | 0x13u | 0x80u; /* ARM + SVC + IRQ off */
  set_cpu_mode(MODE_SUPERVISOR);
  reg[REG_BUS_VALUE] = 0xe3a02004u;                         /* post-SWI bios[0xE4] open-bus */
  reg[REG_PC] = 0x00000008u;                                /* SWI vector */
}

#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_bios_entry_swi, cgba_bios_entry_irq, cgba_bios_entry_other;
u32 cgba_bios_hle_irq_in, cgba_bios_hle_irq_out;
#endif

/* ---- BIOS IRQ wrapper HLE --------------------------------------------------
 * Every raised IRQ used to make TWO interpreter round-trips: vector 0x18 runs
 * the 5-instruction BIOS dispatch stub, and the game handler's return lands on
 * the 2-instruction epilogue at 0x30 (AW fires ~119 IRQs/frame -> ~490k
 * fallback entries in the first 2000 frames, ~10% of all cycles in setup/exit
 * overhead alone). These two stubs of the shipped open BIOS are emulated here
 * state-identically (same pushes, same registers, same SPSR restore) and the
 * dispatcher never leaves native code. Anything unusual (non-IRQ mode, odd
 * handler address) falls back to the interpreter path unchanged.
 *
 * open_gba_bios.bin wrapper (disassembled):
 *   0x18: b 0x20
 *   0x20: stmdb sp!,{r0-r3,r12,lr}
 *   0x24: mov r0,#0x04000000
 *   0x28: mov lr,pc              ; lr = 0x30
 *   0x2c: ldr pc,[r0,#-4]        ; pc = [0x03007FFC]
 *   0x30: ldmia sp!,{r0-r3,r12,lr}
 *   0x34: subs pc,lr,#4          ; CPSR = SPSR_irq
 *
 * Timing: the handful of BIOS fetch cycles per stub are NOT debited (the
 * budget register is not reachable from the resolver); the data accesses
 * charge normally via execute_load/store. ~10 uncharged cycles per IRQ is
 * within the port's accepted approximate-timing envelope. */
u32 cgba_hle_bios_irq_entry(void)
{
  u32 handler = execute_load_u32(0x03007FFCu);
  u32 sp;

  if ((handler & 3) || handler < 0x02000000u || handler >= 0x0E000000u)
    return 0;                      /* odd/garbage handler: interpreter path */

  sp = reg[REG_SP] - 24;
  {
    /* The IRQ stack is linear RAM (0x03007Fxx by BIOS convention): push the
     * six registers with direct host stores and one bulk charge instead of
     * six charged accessor calls — 138 IRQs/frame in AW. Semantics match
     * the per-word path exactly (same total charge, same SMC contract). */
    u8 *tags = NULL;
    u8 *h = ((sp & 3) == 0) ? cgba_bulk_ram_host(sp, 24, &tags) : NULL;
    if (h) {
      cgba_le32_write(h +  0, reg[0]);
      cgba_le32_write(h +  4, reg[1]);
      cgba_le32_write(h +  8, reg[2]);
      cgba_le32_write(h + 12, reg[3]);
      cgba_le32_write(h + 16, reg[12]);
      cgba_le32_write(h + 20, reg[REG_LR]);
      cgba_sh4_extra_cycles += 6 *
        (cgba_sh4_mem_cycle_seq ? ws_cyc_seq[sp >> 24][1]
                                : ws_cyc_nseq[sp >> 24][1]);
      if (cgba_bulk_tags_smc(tags, 24))
        flush_translation_cache_ram();
    } else {
      execute_store_u32(sp +  0, reg[0]);
      execute_store_u32(sp +  4, reg[1]);
      execute_store_u32(sp +  8, reg[2]);
      execute_store_u32(sp + 12, reg[3]);
      execute_store_u32(sp + 16, reg[12]);
      execute_store_u32(sp + 20, reg[REG_LR]);
      if (cgba_store_alert) {      /* IRQ stack over tagged RAM code (rare) */
        cpu_alert_type a = cgba_store_alert;
        cgba_store_alert = CPU_ALERT_NONE;
        if (a & CPU_ALERT_SMC)
          flush_translation_cache_ram();
      }
    }
  }
  reg[REG_SP] = sp;
  reg[REG_LR] = 0x00000030u;
  reg[0] = 0x04000000u;
  reg[REG_PC] = handler;
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_irq_in++;
#endif
  return handler;
}

u32 cgba_hle_bios_irq_exit(void)
{
  u32 sp = reg[REG_SP];
  u32 spsr_v, ret_pc;

  {
    const u8 *h = ((sp & 3) == 0) ? cgba_bulk_ram_host(sp, 24, NULL) : NULL;
    if (h) {
      reg[0]      = cgba_le32_read(h +  0);
      reg[1]      = cgba_le32_read(h +  4);
      reg[2]      = cgba_le32_read(h +  8);
      reg[3]      = cgba_le32_read(h + 12);
      reg[12]     = cgba_le32_read(h + 16);
      reg[REG_LR] = cgba_le32_read(h + 20);
      cgba_sh4_extra_cycles += 6 *
        (cgba_sh4_mem_cycle_seq ? ws_cyc_seq[sp >> 24][1]
                                : ws_cyc_nseq[sp >> 24][1]);
    } else {
      reg[0]       = execute_load_u32(sp +  0);
      reg[1]       = execute_load_u32(sp +  4);
      reg[2]       = execute_load_u32(sp +  8);
      reg[3]       = execute_load_u32(sp + 12);
      reg[12]      = execute_load_u32(sp + 16);
      reg[REG_LR]  = execute_load_u32(sp + 20);
    }
  }
  reg[REG_SP]  = sp + 24;

  ret_pc = reg[REG_LR] - 4;
  spsr_v = REG_SPSR(MODE_IRQ);
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
  reg[REG_PC] = ret_pc;

  /* Pending-IRQ recheck, mirroring the interpreter's check_for_interrupts
   * after an SPSR restore: re-enter the IRQ vector immediately. */
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && read_ioreg(REG_IME) &&
      (reg[REG_CPSR] & 0x80) == 0) {
    REG_MODE(MODE_IRQ)[6] = ret_pc + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    reg[REG_PC] = 0x00000018u;
    set_cpu_mode(MODE_IRQ);
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_irq_out++;
#endif
  return reg[REG_PC];
}

extern int cgba_dynarec_single_block;

/* ---- BIOS SWI HLE ----------------------------------------------------------
 * The copy/decompress SWI family dominates interpreter residency in games
 * that stream graphics (SMA2: 20.6M interpreted BIOS instructions in the
 * first 2000 frames — 10k/frame — nearly all LZ77/CpuSet loops). These are
 * faithful ports of the vendored open-BIOS C sources
 * (vendor/gpsp/bios/source/softwareinterrupts.c), run through the charging
 * execute_load/store accessors so data-access cycles and SMC alerts behave
 * exactly like the interpreted loops; only the BIOS's own instruction
 * fetches are uncharged (same accepted envelope as the IRQ-wrapper HLE).
 * Registers: the real routines are compiler-generated C behind a dispatcher
 * — games cannot rely on scratch regs; r0-r3 are left as the inputs. */
/* ---- bulk copy fast path for CpuSet / CpuFastSet --------------------------
 * The SWI HLEs copied per-word through the charging execute_load/store
 * accessors (~40-80 host insns per guest word); AW stages tilemaps with
 * these every frame (execute_store_u32 + read_memory32 ~6.5% of steady
 * state). When source and destination ranges each sit linearly inside one
 * always-mapped region, do a host memmove instead: guest RAM is stored as
 * LE bytes in every host array, so bytewise copies need no swapping.
 * Cycle charging replicates the per-word loop EXACTLY (count * per-region
 * wait states under the same seq flag), so guest-visible timing is
 * unchanged. SMC follows the store path's contract: any nonzero tag byte
 * under the destination raises CPU_ALERT_SMC for the HLE tail to consume.
 * Palette/OAM/IO destinations keep the per-word path (converted-palette
 * and OAM_UPDATED side effects); ROM sources must sit in one mapped 32KB
 * page. */
static u8 *cgba_bulk_ram_host(u32 addr, u32 len, u8 **tags)
{
  u32 off;
  switch (addr >> 24) {
  case 2:
    off = addr & 0x3FFFF;
    if (off + len > 0x40000) return NULL;
    if (tags) *tags = ewram + 0x40000 + off;
    return ewram + off;
  case 3:
    off = addr & 0x7FFF;
    if (off + len > 0x8000) return NULL;
    if (tags) *tags = iwram + off;
    return iwram + 0x8000 + off;
  case 6:
    off = addr & 0x1FFFF;
    if (off + len > 0x18000) return NULL;
    if (tags) *tags = NULL;                 /* VRAM: no tag mirror */
    return vram + off;
  default:
    return NULL;
  }
}

static const u8 *cgba_bulk_src_host(u32 addr, u32 len)
{
  if ((addr >> 24) >= 8 && (addr >> 24) <= 0xD) {   /* gamepak: one page */
    u8 *page = memory_map_read[(addr & 0x0FFFFFFF) >> 15];
    u32 off = addr & 0x7FFF;
    if (!page || off + len > 0x8000) return NULL;
    return page + off;
  }
  return cgba_bulk_ram_host(addr, len, NULL);
}

static int cgba_bulk_tags_smc(const u8 *tags, u32 len)
{
  u32 i = 0;
  if (!tags) return 0;
  /* SH-4 raises an address error (EXC 0x0E0) on misaligned longword loads,
   * and the tag pointer inherits the guest destination's alignment — a
   * halfword-aligned CpuSet dest (AW unit-select copies to 0x03001FF2)
   * made the u32 scan read at +2 and hard-crashed on hardware. calcemu
   * never caught it: host CPUs tolerate misaligned loads. Align the head
   * byte-wise before the longword scan. */
  for (; i < len && (((uintptr_t)tags + i) & 3); i++)
    if (tags[i]) return 1;
  for (; i + 4 <= len; i += 4)
    if (*(const u32 *)(const void *)(tags + i)) return 1;
  for (; i < len; i++)
    if (tags[i]) return 1;
  return 0;
}

static int cgba_bulk_ranges_overlap(const u8 *a, const u8 *b, u32 len)
{
  uintptr_t aa = (uintptr_t)a;
  uintptr_t bb = (uintptr_t)b;
  return aa <= bb ? bb - aa < (uintptr_t)len
                  : aa - bb < (uintptr_t)len;
}

/* The ordinary faithful HLE intentionally declines all overlap.  The opt-in
 * extension accepts only dst>src overlap, which can be reproduced exactly by
 * re-reading each source element after earlier destination writes. */
static int cgba_swi_copy_overlap_ok(u32 src, u32 dst, u32 len)
{
  if (dst - src >= len && src - dst >= len)
    return 1;                                  /* disjoint guest ranges */
#if CGBA_SH4_SWI_FORWARD_OVERLAP
  return dst > src;                            /* forward overlap only */
#else
  return 0;
#endif
}

/* Word/halfword copy or fill, bulk when both sides resolve. Returns 1 when
 * handled (cycles charged, SMC alert accumulated), 0 to run the per-word
 * loop. size_index/width: 1/4 for 32-bit ops, 0/2 for 16-bit. */
static int cgba_bulk_cpuset(u32 source, u32 dest, u32 count, int fill,
                            unsigned size_index, u32 openbus)
{
  u32 width = size_index ? 4 : 2;
  u32 len = count * width;
  u8 *tags = NULL;
  u8 *d;
  const u8 *sp = NULL;

  if (!count || len > 0x200000u)
    return 0;
  if ((dest & (width - 1)) || (!fill && (source & (width - 1))))
    return 0;                        /* misaligned: keep per-word semantics */
  d = cgba_bulk_ram_host(dest, len, &tags);
  if (!d)
    return 0;
  if (!fill) {
    sp = cgba_bulk_src_host(source, len);
    if (!sp)
      return 0;
    /* Overlapping ranges: the BIOS copies FORWARD word-by-word, so a
       destination inside the source re-reads freshly written words (the
       classic GBA pattern-fill idiom); memmove would preserve the original
       source bytes instead. Compare HOST pointers (mirrors alias) and keep
       any overlap on the exact per-word path. */
    if (cgba_bulk_ranges_overlap(sp, d, len))
      return 0;
  }

  if (fill) {
    u32 value = (source > 0x0EFFFFFF)
      ? openbus
      : (size_index ? execute_load_u32(source) : execute_load_u16(source));
    u32 i;
    if (size_index) {
      u8 b[4] = { (u8)value, (u8)(value >> 8),
                  (u8)(value >> 16), (u8)(value >> 24) };
      for (i = 0; i < len; i += 4) memcpy(d + i, b, 4);
    } else {
      u8 b[2] = { (u8)value, (u8)(value >> 8) };
      for (i = 0; i < len; i += 2) memcpy(d + i, b, 2);
    }
  } else {
    memmove(d, sp, len);
    /* charge the source side like count sequential loads */
    if (source < 0x10000000u) {
      u32 sr = source >> 24;
      cgba_sh4_extra_cycles += (int)count *
        (cgba_sh4_mem_cycle_seq ? ws_cyc_seq[sr][size_index]
                                : ws_cyc_nseq[sr][size_index]);
    }
  }
  /* charge the destination side like count stores */
  {
    u32 dr = dest >> 24;
    cgba_sh4_extra_cycles += (int)count *
      (cgba_sh4_mem_cycle_seq ? ws_cyc_seq[dr][size_index]
                              : ws_cyc_nseq[dr][size_index]);
  }
  if (cgba_bulk_tags_smc(tags, len))
    cgba_store_alert |= CPU_ALERT_SMC;
  return 1;
}


/* ---- register- and cycle-faithful CpuSet / CpuFastSet ---------------------
 * Reproduces the open BIOS routines EXACTLY as the interpreter would run
 * them — post-SWI registers and cycle charge included — while doing the
 * memory work at host speed. Derived from open_gba_bios.bin: the SWI
 * dispatcher (0x64) stacks r2/r3/r11/r12/lr around the routine, so ONLY
 * r0/r1 carry routine clobbers back to the caller:
 *
 *   CpuSet 0x614   half-copy: r1 = dst - src
 *                  half-fill: r1 = dst + 2*count
 *                  word-copy: r1 = (dst&~3) - (src&~3), r0 = last word
 *                  word-fill: r1 = (dst&~3) + 4*count
 *   CpuFastSet 0x720 fill:    r1 = (dst&~3) + 4*ceil8(count)  (writes ceil8!)
 *                  copy:      r1 = (dst&~3) - (src&~3),
 *                             r0 = (src&~3) + 4*ceil8(count)
 *   early-outs (source region bits 25-27 zero, end region zero, count 0):
 *                  r0/r1 unchanged.
 *
 * The cycle model mirrors the interpreter's charging (per-insn seq fetch of
 * the region the pc lands in, extra nseq for taken B/BX targets, nseq per
 * single data access, seq per LDM/STM word; MOVS pc,lr to a Thumb caller
 * charges nothing — the interpreter's spsr-restore path jumps straight to
 * thumb_loop). Instruction traces are counted from the disassembly; the
 * CGBA_SH4_SWI_HLE_VERIFY build predicts (r0, r1, cycles) and then lets the
 * interpreter run the real BIOS, reporting any mismatch on the debug port.
 *
 * Anything the formulas do not cover EXACTLY falls back to the interpreted
 * BIOS (return 0): unaligned halfword operands (the BIOS does not mask
 * them; the interpreter's rotated-read semantics apply), source/dest
 * ranges that cross a 0x01000000 region boundary (per-access charges would
 * change region mid-run), open-bus sources, unresolvable/overlapping host
 * ranges, and ARM-state callers (rare; keeps the MOVS-pc tail term out of
 * the model). */

struct cgba_swi_pred {
  u32 r0, r1;
  u32 cycles;
  int r0_from_last_word;   /* word-copy: r0 = last source word */
};

/* Charge units. */
#define CGBA_WS_S(reg_, w_) ((u32)ws_cyc_seq[(reg_) & 0xF][(w_)])
#define CGBA_WS_N(reg_, w_) ((u32)ws_cyc_nseq[(reg_) & 0xF][(w_)])

/* Dispatcher entry + return, common to every SWI the open BIOS serves.
 * Entry: 13 BIOS fetches, B+BX nseq, {r11,r12,lr}+{r11} pushes on the SVC
 * stack (region 3), {r2,r3,lr} push on the caller stack, the SWI-number
 * LDRB from the caller, the jump-table LDR.
 * Return: 6 BIOS fetches, {r2,r3,lr} pop (caller stack), {r11}+{r11,r12,lr}
 * pops (SVC stack); MOVS pc,lr adds nothing for a Thumb caller. */
static u32 cgba_swi_dispatch_cycles(u32 sp_region, u32 lr_region, int thumb)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 c = 19 * S0 + 3 * N0;                    /* 13+6 fetches, B + BX + n0 table */
  c += 8 * CGBA_WS_S(3, 1);                    /* SVC-stack LDM/STM words */
  c += 6 * CGBA_WS_S(sp_region, 1);            /* caller-stack LDM/STM words */
  c += CGBA_WS_N(lr_region, 0);                /* LDRB [lr, #-2] */
  if (!thumb)
    c += CGBA_WS_S(lr_region, 1);              /* MOVS pc tail (ARM caller) */
  return c;
}

/* Resolve a same-region, in-bounds, non-open-bus guest range to host, or
 * NULL. width_mask: alignment the FORMULA requires (the BIOS itself masks
 * word ops; halfword ops must already be aligned). */
static u8 *cgba_swi_exact_host(u32 addr, u32 len, u8 **tags)
{
  if (((addr + len - 1) >> 24) != (addr >> 24))
    return NULL;                               /* crosses a region boundary */
  if (addr + len > 0x0F000000u)
    return NULL;                               /* open-bus territory */
  return cgba_bulk_ram_host(addr, len, tags);
}

static const u8 *cgba_swi_exact_src_host(u32 addr, u32 len)
{
  if (((addr + len - 1) >> 24) != (addr >> 24))
    return NULL;
  if (addr + len > 0x0F000000u)
    return NULL;
  return cgba_bulk_src_host(addr, len);
}

/* Predict CpuSet: returns 1 with *pred filled, 0 = not modeled (interp).
 * Accounting convention: every executed BIOS instruction adds S0 (the
 * interpreter's per-insn tail charges the region the pc lands in — BIOS
 * throughout); every TAKEN branch adds N0 on top; data accesses add the
 * listed nseq/seq terms. EXIT = 0x684 LDM {r4} + 0x688 BX lr. */
static int cgba_swi_cpuset_predict(u32 src, u32 dst, u32 cnt,
                                   struct cgba_swi_pred *pred)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 sp_r = reg[REG_SP] >> 24, lr_r = (reg[REG_LR] & ~1u) >> 24;
  int thumb = (REG_SPSR(MODE_SUPERVISOR) & 0x20) != 0;
  u32 count = cnt & 0x1FFFFFu;
  u32 word = (cnt >> 26) & 1, fill = (cnt >> 24) & 1;
  u32 end = src + (((cnt << 11) >> 9) & ~0xE00000u);
  u32 exit_c, c;

  if (!thumb)
    return 0;
  c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb);
  c += CGBA_WS_N(sp_r, 1);                     /* 618 STR r4 (single store) */
  exit_c = 2 * S0 + CGBA_WS_S(sp_r, 1) + N0;   /* 684 LDM + 688 BX lr */
  pred->r0 = src; pred->r1 = dst; pred->r0_from_last_word = 0;

  if (!(src & 0x0E000000u)) {                  /* eo1: 614,618,61C(taken) */
    pred->cycles = c + 3 * S0 + N0 + exit_c;
    return 1;
  }
  if (!(end & 0x0E000000u)) {                  /* eo2: ..634(taken) */
    pred->cycles = c + 9 * S0 + N0 + exit_c;
    return 1;
  }
  /* through 644: 12*S0 accumulated by each path below */

  if (!word) {
    if (!fill) {                               /* ---- half copy ---- */
      if (count == 0) {                        /* ..654(taken): 17 insns */
        pred->cycles = c + 17 * S0 + N0 + exit_c;
        return 1;
      }
      if ((src | dst) & 1)
        return 0;
      if (!cgba_swi_exact_src_host(src, count * 2))
        return 0;
      if (((dst + count * 2 - 1) >> 24) != (dst >> 24) ||
          dst + count * 2 > 0x0F000000u)
        return 0;
      if (!cgba_swi_copy_overlap_ok(src, dst, count * 2))
        return 0;
      c += 21 * S0;                            /* prologue through 664 */
      c += count * (7 * S0 + CGBA_WS_N(src >> 24, 0) + CGBA_WS_N(dst >> 24, 0));
      c += (count - 1) * N0;
      pred->r1 = dst - src;
      pred->cycles = c + exit_c;
      return 1;
    }
    /* ---- half fill: 64C taken ---- */
    if (src & 1)
      return 0;
    if (!cgba_swi_exact_src_host(src, 2))
      return 0;
    if (count == 0) {                          /* ..6DC(taken): 20 insns */
      pred->cycles = c + 20 * S0 + CGBA_WS_N(src >> 24, 0) + 2 * N0 + exit_c;
      return 1;
    }
    if (dst & 1)
      return 0;
    if (((dst + count * 2 - 1) >> 24) != (dst >> 24) ||
        dst + count * 2 > 0x0F000000u)
      return 0;
    c += 22 * S0 + CGBA_WS_N(src >> 24, 0) + N0;   /* through 6E0 + 64C-taken */
    c += count * (3 * S0 + CGBA_WS_N(dst >> 24, 0));
    c += (count - 1) * N0;
    c += N0;                                   /* 6F0 B 684 (fetch in the 22) */
    pred->r1 = dst + count * 2;
    pred->cycles = c + exit_c;
    return 1;
  }

  /* ---- word paths: 644 taken ---- */
  {
    u32 wsrc = src & ~3u, wdst = dst & ~3u;
    if (!fill) {                               /* ---- word copy ---- */
      if (count == 0) {                        /* ..6A8(taken): 21 insns */
        pred->cycles = c + 21 * S0 + 2 * N0 + exit_c;
        return 1;
      }
      if (!cgba_swi_exact_src_host(wsrc, count * 4))
        return 0;
      if (((wdst + count * 4 - 1) >> 24) != (wdst >> 24) ||
          wdst + count * 4 > 0x0F000000u)
        return 0;
      if (!cgba_swi_copy_overlap_ok(wsrc, wdst, count * 4))
        return 0;
      c += 22 * S0 + N0;                       /* through 6A8(nt) + 6C8 B + 644 */
      c += count * (7 * S0 + CGBA_WS_N(wsrc >> 24, 1) + CGBA_WS_N(wdst >> 24, 1));
      c += (count - 1) * N0;
      c += N0;                                 /* 6C8 B 684 */
      pred->r1 = wdst - wsrc;
      pred->r0_from_last_word = 1;
      pred->cycles = c + exit_c;
      return 1;
    }
    /* ---- word fill: 644 + 698 taken ---- */
    if (!cgba_swi_exact_src_host(wsrc, 4))
      return 0;
    if (count == 0) {                          /* ..704(taken): 22 insns */
      pred->cycles = c + 22 * S0 + CGBA_WS_N(wsrc >> 24, 1) + 3 * N0 + exit_c;
      return 1;
    }
    if (((wdst + count * 4 - 1) >> 24) != (wdst >> 24) ||
        wdst + count * 4 > 0x0F000000u)
      return 0;
    c += 23 * S0 + CGBA_WS_N(wsrc >> 24, 1) + 2 * N0;  /* 644,698 taken */
    c += count * (3 * S0 + CGBA_WS_N(wdst >> 24, 1));
    c += (count - 1) * N0;
    c += N0;                                   /* 714 B 684 */
    pred->r1 = wdst + count * 4;
    pred->cycles = c + exit_c;
    return 1;
  }
}

/* Predict CpuFastSet; EXIT = 0x78C LDM + 0x790 BX. */
static int cgba_swi_cpufastset_predict(u32 src, u32 dst, u32 cnt,
                                       struct cgba_swi_pred *pred)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 sp_r = reg[REG_SP] >> 24, lr_r = (reg[REG_LR] & ~1u) >> 24;
  int thumb = (REG_SPSR(MODE_SUPERVISOR) & 0x20) != 0;
  u32 count = cnt & 0x1FFFFFu;
  u32 fill = (cnt >> 24) & 1;
  u32 end = src + (((cnt << 11) >> 9) & ~0xE00000u);
  u32 wsrc = src & ~3u, wdst = dst & ~3u;
  u32 K = (count + 7) / 8;
  u32 exit_c, c;

  if (!thumb)
    return 0;
  c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb);
  c += CGBA_WS_N(sp_r, 1);                     /* 724 STR r4 */
  exit_c = 2 * S0 + CGBA_WS_S(sp_r, 1) + N0;   /* 78C LDM + 790 BX */
  pred->r0 = src; pred->r1 = dst; pred->r0_from_last_word = 0;

  if (!(src & 0x0E000000u)) {                  /* eo1: 720,724,728(taken) */
    pred->cycles = c + 3 * S0 + N0 + exit_c;
    return 1;
  }
  if (!(end & 0x0E000000u)) {                  /* eo2: ..740(taken) */
    pred->cycles = c + 9 * S0 + N0 + exit_c;
    return 1;
  }

  if (fill) {                                  /* 758 NOT taken (bit set) */
    if (!cgba_swi_exact_src_host(wsrc, 4))
      return 0;
    if (count == 0) {                          /* ..76C(taken): 20 insns */
      pred->cycles = c + 20 * S0 + CGBA_WS_N(wsrc >> 24, 1) + N0 + exit_c;
      return 1;
    }
    if (((wdst + K * 32 - 1) >> 24) != (wdst >> 24) ||
        wdst + K * 32 > 0x0F000000u)
      return 0;
    c += 20 * S0 + CGBA_WS_N(wsrc >> 24, 1);   /* through 76C(nt) */
    c += K * (28 * S0 + 8 * CGBA_WS_N(wdst >> 24, 1) + 7 * N0);
    c += (K - 1) * N0;
    pred->r1 = wdst + K * 32;
    pred->cycles = c + exit_c;
    return 1;
  }
  /* copy: 758 taken */
  if (count == 0) {                            /* ..7A0(taken): 19 insns */
    pred->cycles = c + 19 * S0 + 2 * N0 + exit_c;
    return 1;
  }
  if (!cgba_swi_exact_src_host(wsrc, K * 32))
    return 0;
  if (((wdst + K * 32 - 1) >> 24) != (wdst >> 24) ||
      wdst + K * 32 > 0x0F000000u)
    return 0;
  if (!cgba_swi_copy_overlap_ok(wsrc, wdst, K * 32))
    return 0;
  c += 20 * S0 + N0;                           /* through 7A0(nt) + 7D0 B fetch, 758 taken */
  /* chunk = 0x7A4 ADD + 8 x (CMP,LDR,cond-LDR,STR,ADD,CMP,BNE) + SUB,CMP,BGT
     = 1 + 56 + 3 fetches (trace-verified; the inner BNE's own fetch counts). */
  c += K * (60 * S0 + 8 * (CGBA_WS_N(wsrc >> 24, 1) + CGBA_WS_N(wdst >> 24, 1)) + 7 * N0);
  c += (K - 1) * N0;
  c += N0;                                     /* 7D0 B 78C */
  pred->r1 = wdst - wsrc;
  pred->r0 = wsrc + K * 32;
  pred->cycles = c + exit_c;
  return 1;
}


/* ---- Parked/resumable CpuFastSet ------------------------------------------
 * Oversized FastSets (predicted cost exceeding the current event slice) can't
 * be HLE'd atomically: the interpreter interleaves update_gba mid-copy, and
 * IRQs vector between iterations. Instead we execute the routine in budgeted
 * 8-word chunks from a CANONICAL machine state: registers, stacks, mode and
 * PC always equal what the real BIOS interpreter would show at the chunk-top
 * PC. Parking = leaving that state in place; if anything unexpected happens
 * (savestate load, odd resume, validation failure) the interpreter simply
 * continues the real BIOS from it, bit-exact by construction.
 *
 * Open-BIOS geometry (vendor/gpsp/bios/open_gba_bios.bin, trace-verified):
 *   dispatcher 0x64: STMDB sp_svc!,{r11,r12,lr}; ... STMDB sp_svc!,{spsr};
 *     switch to SYSTEM ((spsr&0x80)|0x1F, NZCV cleared);
 *     STMDB sp_usr!,{r2,r3,lr}; lr_usr = 0x94; jump table -> 0x720.
 *   FastSet 0x720: STR r4,[sp_usr,-4]!; entry checks;
 *     copy loop top 0x7A4: r3=src cursor, r1=dst-src, r4=words left (raw),
 *       r12=0x0EFFFFFF, r0=prev chunk end, r2=last loaded word.
 *     fill loop top 0x770: r1=dst cursor, r2=fill word, r4=words left,
 *       r3=prev chunk end, r12=0x720 (jump-table leftover), r0=caller r0.
 *   exit: LDM sp_usr!,{r4}; return to 0x94: LDMIA sp_usr!,{r2,r3,lr};
 *     back to SVC (0xD3, NZCV cleared, r12=0xD3); pop spsr;
 *     LDMIA sp_svc!,{r11,r12,lr}; MOVS pc,lr. Only r0/r1 escape. */
#ifdef CGBA_GPSP_HEADLESS_TEST
extern u32 cgba_bios_hle_swi_count;
#endif

#define CGBA_FS_COPY_LOOP 0x000007A4u
#define CGBA_FS_FILL_LOOP 0x00000770u
#define CGBA_FS_DECLINE   0xFFFFFFFFu

static u32 cgba_fs_entry_cost;    /* handoff: dispatch+prologue cycles */

/* Materialize the canonical loop-top state for a dispatched oversized
 * FastSet (pc==8, SVC, predict-ok). Mirrors dispatcher + routine prologue. */
static void cgba_swi_fastset_materialize(u32 fill)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 sp_svc = reg[REG_SP];
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 wsrc = reg[0] & ~3u;
  u32 sp_r = sp_svc >> 24, lr_r = (reg[REG_LR] & ~1u) >> 24;

  /* dispatcher pushes on the SVC stack */
  execute_store_u32(sp_svc - 4, reg[REG_LR]);            /* return address */
  execute_store_u32(sp_svc - 8, reg[12]);
  execute_store_u32(sp_svc - 12, reg[11]);
  execute_store_u32(sp_svc - 16, spsr_v);
  reg[REG_SP] = sp_svc - 16;

  /* switch to SYSTEM mode exactly like MSR CPSR_9 at 0x84 (NZCV cleared,
   * caller's I bit kept, F cleared) */
  reg[REG_CPSR] = (spsr_v & 0x80u) | 0x1Fu;
  set_cpu_mode(MODE_SYSTEM);

  /* dispatcher + routine pushes on the caller's stack */
  {
    u32 sp_usr = reg[REG_SP];
    execute_store_u32(sp_usr - 4, reg[REG_LR]);          /* caller lr_usr */
    execute_store_u32(sp_usr - 8, reg[3]);
    execute_store_u32(sp_usr - 12, reg[2]);
    execute_store_u32(sp_usr - 16, reg[4]);              /* 0x724 STR r4 */
    reg[REG_SP] = sp_usr - 16;
  }
  reg[REG_LR] = 0x00000094u;
  reg[11] = (spsr_v & 0x80u) | 0x1Fu;                    /* dispatcher scratch */

  /* routine entry effects + loop registers; charge dispatch + prologue */
  cgba_fs_entry_cost = cgba_swi_dispatch_cycles(sp_r, lr_r, 1)
                     + CGBA_WS_N(sp_r, 1);               /* 724 STR r4 */
  reg[4] = reg[2] & 0x1FFFFFu;                           /* words left (raw) */
  if (fill) {
    reg[3] = wsrc;
    reg[2] = execute_load_u32(wsrc);                     /* 760 LDR value */
    reg[1] = reg[1] & ~3u;                               /* dst cursor */
    reg[12] = 0x00000720u;                               /* table leftover */
    cgba_fs_entry_cost += 20 * S0 + CGBA_WS_N(wsrc >> 24, 1);
    reg[REG_PC] = CGBA_FS_FILL_LOOP;
  } else {
    reg[3] = wsrc;                                       /* src cursor */
    reg[1] = (reg[1] & ~3u) - wsrc;                      /* delta */
    reg[12] = 0x0EFFFFFFu;
    cgba_fs_entry_cost += 20 * S0 + N0;
    reg[REG_PC] = CGBA_FS_COPY_LOOP;
  }
}

/* Budgeted chunk executor from a canonical loop-top state. Returns a
 * fallback-contract value, or CGBA_FS_DECLINE to let the interpreter run
 * the (still canonical) state natively. */
static u32 cgba_swi_fastset_engine(s32 remaining)
{
  int fill = (reg[REG_PC] == CGBA_FS_FILL_LOOP);
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src, dst, chunk_c;
  s32 words;

  remaining -= (s32)cgba_fs_entry_cost;
  cgba_fs_entry_cost = 0;

  words = (s32)reg[4];
  if (words <= 0)
    return CGBA_FS_DECLINE;                    /* not a mid-copy state */
  if (fill) {
    if (reg[12] != 0x00000720u)
      return CGBA_FS_DECLINE;
    src = reg[3];                              /* unused; kept canonical */
    dst = reg[1];
    chunk_c = 28 * S0 + 8 * CGBA_WS_N(dst >> 24, 1) + 7 * N0 + N0;
  } else {
    if (reg[12] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[3];
    dst = reg[3] + reg[1];
    chunk_c = 60 * S0
            + 8 * (CGBA_WS_N(src >> 24, 1) + CGBA_WS_N(dst >> 24, 1))
            + 7 * N0 + N0;
  }
  if ((dst >> 24) >= 0x0F || (dst & 3))
    return CGBA_FS_DECLINE;

  while (1) {
    if (words <= 0) {
      /* routine + dispatcher epilogue, analytically (pops read back the
       * words we stored, so caller values reappear even if an ISR ran) */
      u32 sp_usr = reg[REG_SP];
      u32 sp_svc, spsr_pop;
      u32 sp_r = sp_usr >> 24;
      reg[4] = execute_load_u32(sp_usr);
      reg[2] = execute_load_u32(sp_usr + 4);
      reg[3] = execute_load_u32(sp_usr + 8);
      reg[REG_LR] = execute_load_u32(sp_usr + 12);
      reg[REG_SP] = sp_usr + 16;
      remaining -= (s32)(2 * S0 + CGBA_WS_S(sp_r, 1) + N0);  /* 78C+790 */
      if (fill)
        remaining += (s32)N0;             /* last fill chunk has no BGT */
      reg[12] = 0x000000D3u;
      reg[REG_CPSR] = 0x000000D3u;
      set_cpu_mode(MODE_SUPERVISOR);
      sp_svc = reg[REG_SP];
      spsr_pop = execute_load_u32(sp_svc);
      reg[11] = execute_load_u32(sp_svc + 4);
      reg[12] = execute_load_u32(sp_svc + 8);
      reg[REG_LR] = execute_load_u32(sp_svc + 12);
      reg[REG_SP] = sp_svc + 16;
      REG_SPSR(MODE_SUPERVISOR) = spsr_pop;
      reg[REG_PC] = reg[REG_LR] & ~1u;
      reg[REG_CPSR] = spsr_pop;
      set_cpu_mode(cpu_modes[spsr_pop & 0xF]);
#ifdef CGBA_GPSP_HEADLESS_TEST
      cgba_bios_hle_swi_count++;
#endif
      break;                                   /* leave via common return */
    }
    if (remaining <= 0) {
      /* canonical park at the chunk top: interleave events like the
       * interpreter's internal update_gba would */
      u32 ret = update_gba(remaining);
      if (completed_frame(ret))
        return 0x80000000u;
      remaining = (s32)cycles_to_run(ret);
      if (reg[REG_PC] != (fill ? CGBA_FS_FILL_LOOP : CGBA_FS_COPY_LOOP)) {
        /* IRQ vectored (pc=0x18, IRQ mode): run the wrapper HLE the same
         * way the fallback entry path would, then hand back to the stub.
         * The ISR's return lands on the loop-top PC and resumes here. */
        if (reg[REG_PC] == 0x00000018u && reg[CPU_MODE] == MODE_IRQ)
          (void)cgba_hle_bios_irq_entry();
        return (u32)remaining;
      }
      continue;
    }
    {
      s32 fit = remaining / (s32)chunk_c;
      u32 chunks_left = ((u32)words + 7) / 8;
      u32 k = (fit < 1) ? 1u : ((u32)fit > chunks_left ? chunks_left
                                                       : (u32)fit);
      u32 bytes = k * 32u;
      u8 *tags = NULL;
      u8 *dh = cgba_bulk_ram_host(dst, bytes, &tags);
      if (fill) {
        if (dh) {
          u32 i;
          for (i = 0; i < bytes; i += 4)
            cgba_le32_write(dh + i, reg[2]);
          if (cgba_bulk_tags_smc(tags, bytes))
            flush_translation_cache_ram();     /* SMC: see note at copy site */
        } else {
          u32 i;
          for (i = 0; i < bytes; i += 4)
            execute_store_u32(dst + i, reg[2]);
        }
        dst += bytes;
        reg[1] = dst;
        reg[3] = dst;                          /* chunk-top canonical */
      } else {
        const u8 *sh = cgba_bulk_src_host(src, bytes);
        if (!sh)
          return CGBA_FS_DECLINE;              /* state stays canonical */
        if (dh) {
#if CGBA_SH4_SWI_FORWARD_OVERLAP
          if (cgba_sh4_swi_is_forward_overlap(dh, sh, bytes))
            cgba_sh4_swi_copy_forward(dh, sh, bytes, 4);
          else
#endif
            memmove(dh, sh, bytes);
          /* The copy just overwrote tagged RAM code. The engine's return and
             park points do NOT run the common HLE tail, so flush the RAM
             translation cache HERE — otherwise a stale block could execute
             at the next park (ISR) or on completion. Safe mid-copy: the
             engine's state lives in guest memory + registers, not the cache,
             and dh/tags point into the GBA RAM arrays, not the cache. */
          if (cgba_bulk_tags_smc(tags, bytes))
            flush_translation_cache_ram();
        } else {
          u32 i;
          for (i = 0; i < bytes; i += 4)
            execute_store_u32(dst + i, cgba_le32_read(sh + i));
        }
        src += bytes;
        dst += bytes;
        reg[3] = src;
        reg[0] = src;                          /* chunk-top canonical */
        reg[2] = cgba_le32_read(sh + bytes - 4);
      }
      words -= (s32)(k * 8u);
      reg[4] = (u32)words;
      remaining -= (s32)(k * chunk_c);
    }
  }

  /* common fallback return contract (mirrors the interp-exit tail) */
  if (remaining <= 0) {
    u32 ret = update_gba(remaining);
    return completed_frame(ret) ? ret : cycles_to_run(ret);
  }
  return (u32)remaining;
}

/* ---- Parked/resumable CpuSet -----------------------------------------------
 * Same canonical-state approach as the FastSet engine, for the two CpuSet
 * COPY loops (fills stay interpreted — rare). Shares the dispatcher prologue
 * (STR r4) and epilogue (LDM {r4}; BX lr -> 0x94 -> dispatcher tail) with
 * FastSet; only the copy loop-top register layout and per-element chunking
 * differ. Open-BIOS geometry (capstone-verified, 0x614):
 *   word copy top 0x6AC: r2=src cursor, r1=dst-src, r3=count left,
 *     r12=0x0EFFFFFF, r0=last loaded word (escapes: r0=last, r1=dst-src).
 *   half copy top 0x668: r2=src cursor, r3=src END, r1=dst-src,
 *     r4=0x0EFFFFFF, r12=last half (escapes: r0=src unchanged, r1=dst-src).
 * Zelda's rain intro rebuilds OAM every frame via ~256-word CpuSets that
 * exceed the event slice (~2700 > ~1200 cyc) — previously interpreted. */
#define CGBA_CS_WCOPY_LOOP 0x000006ACu
#define CGBA_CS_HCOPY_LOOP 0x00000668u

static u32 cgba_cs_entry_cost;

static void cgba_swi_cpuset_materialize(int half)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 sp_svc = reg[REG_SP];
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 sp_r = sp_svc >> 24, lr_r = (reg[REG_LR] & ~1u) >> 24;
  u32 src0 = reg[0], dst0 = reg[1], count = reg[2] & 0x1FFFFFu;

  /* dispatcher SVC-stack pushes (identical to FastSet) */
  execute_store_u32(sp_svc - 4, reg[REG_LR]);
  execute_store_u32(sp_svc - 8, reg[12]);
  execute_store_u32(sp_svc - 12, reg[11]);
  execute_store_u32(sp_svc - 16, spsr_v);
  reg[REG_SP] = sp_svc - 16;
  reg[REG_CPSR] = (spsr_v & 0x80u) | 0x1Fu;
  set_cpu_mode(MODE_SYSTEM);
  {
    u32 sp_usr = reg[REG_SP];
    execute_store_u32(sp_usr - 4, reg[REG_LR]);
    execute_store_u32(sp_usr - 8, reg[3]);
    execute_store_u32(sp_usr - 12, reg[2]);
    execute_store_u32(sp_usr - 16, reg[4]);         /* 0x618 STR r4 */
    reg[REG_SP] = sp_usr - 16;
  }
  reg[REG_LR] = 0x00000094u;
  reg[11] = (spsr_v & 0x80u) | 0x1Fu;

  cgba_cs_entry_cost = cgba_swi_dispatch_cycles(sp_r, lr_r, 1)
                     + CGBA_WS_N(sp_r, 1);          /* 0x618 STR r4 */
  reg[0] = src0;                                    /* r0 pre-first-load */
  if (half) {
    reg[2] = src0;                                  /* src cursor */
    reg[3] = src0 + count * 2u;                     /* src END */
    reg[1] = dst0 - src0;                           /* delta */
    reg[4] = 0x0EFFFFFFu;
    cgba_cs_entry_cost += 21 * S0;
    reg[REG_PC] = CGBA_CS_HCOPY_LOOP;
  } else {
    u32 ws = src0 & ~3u, wd = dst0 & ~3u;
    reg[2] = ws;                                    /* src cursor */
    reg[1] = wd - ws;                               /* delta */
    reg[3] = count;                                 /* count remaining */
    reg[12] = 0x0EFFFFFFu;
    cgba_cs_entry_cost += 22 * S0 + N0;
    reg[REG_PC] = CGBA_CS_WCOPY_LOOP;
  }
}

static u32 cgba_swi_cpuset_engine(s32 remaining)
{
  int half = (reg[REG_PC] == CGBA_CS_HCOPY_LOOP);
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src, dst, per_elem, width;
  s32 count;

  remaining -= (s32)cgba_cs_entry_cost;
  cgba_cs_entry_cost = 0;

  if (half) {
    if (reg[4] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[2];
    count = (s32)((reg[3] - reg[2]) >> 1);           /* (src_end - src)/2 */
    width = 2;
    dst = reg[1] + reg[2];
    per_elem = 7 * S0 + CGBA_WS_N(src >> 24, 0) + CGBA_WS_N(dst >> 24, 0) + N0;
  } else {
    if (reg[12] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[2];
    count = (s32)reg[3];
    width = 4;
    dst = reg[1] + reg[2];
    per_elem = 7 * S0 + CGBA_WS_N(src >> 24, 1) + CGBA_WS_N(dst >> 24, 1) + N0;
  }
  if (count <= 0)
    return CGBA_FS_DECLINE;
  if ((dst >> 24) >= 0x0F ||
      (half ? (((dst | src) & 1) != 0) : (((dst | src) & 3) != 0)))
    return CGBA_FS_DECLINE;

  while (1) {
    if (count <= 0) {
      /* routine + dispatcher epilogue (shared shape with FastSet) */
      u32 sp_usr = reg[REG_SP];
      u32 sp_svc, spsr_pop, sp_r = sp_usr >> 24;
      reg[4] = execute_load_u32(sp_usr);
      reg[2] = execute_load_u32(sp_usr + 4);
      reg[3] = execute_load_u32(sp_usr + 8);
      reg[REG_LR] = execute_load_u32(sp_usr + 12);
      reg[REG_SP] = sp_usr + 16;
      remaining -= (s32)(2 * S0 + CGBA_WS_S(sp_r, 1) + N0);   /* 684 LDM+688 BX */
      if (half)
        remaining += (s32)N0;         /* half exits via fall-through: -1 N0 */
      reg[12] = 0x000000D3u;
      reg[REG_CPSR] = 0x000000D3u;
      set_cpu_mode(MODE_SUPERVISOR);
      sp_svc = reg[REG_SP];
      spsr_pop = execute_load_u32(sp_svc);
      reg[11] = execute_load_u32(sp_svc + 4);
      reg[12] = execute_load_u32(sp_svc + 8);
      reg[REG_LR] = execute_load_u32(sp_svc + 12);
      reg[REG_SP] = sp_svc + 16;
      REG_SPSR(MODE_SUPERVISOR) = spsr_pop;
      reg[REG_PC] = reg[REG_LR] & ~1u;
      reg[REG_CPSR] = spsr_pop;
      set_cpu_mode(cpu_modes[spsr_pop & 0xF]);
#ifdef CGBA_GPSP_HEADLESS_TEST
      cgba_bios_hle_swi_count++;
#endif
      break;
    }
    if (remaining <= 0) {
      u32 ret = update_gba(remaining);
      if (completed_frame(ret))
        return 0x80000000u;
      remaining = (s32)cycles_to_run(ret);
      if (reg[REG_PC] != (half ? CGBA_CS_HCOPY_LOOP : CGBA_CS_WCOPY_LOOP)) {
        if (reg[REG_PC] == 0x00000018u && reg[CPU_MODE] == MODE_IRQ)
          (void)cgba_hle_bios_irq_entry();
        return (u32)remaining;
      }
      continue;
    }
    {
      s32 fit = remaining / (s32)per_elem;
      u32 k = (fit < 1) ? 1u
            : ((u32)fit > (u32)count ? (u32)count : (u32)fit);
      u32 bytes = k * width;
      const u8 *sh = cgba_bulk_src_host(src, bytes);
      u8 *tags = NULL;
      u8 *dh;
      if (!sh)
        return CGBA_FS_DECLINE;
      dh = cgba_bulk_ram_host(dst, bytes, &tags);
      if (dh) {
#if CGBA_SH4_SWI_FORWARD_OVERLAP
        if (cgba_sh4_swi_is_forward_overlap(dh, sh, bytes))
          cgba_sh4_swi_copy_forward(dh, sh, bytes, width);
        else
#endif
          memmove(dh, sh, bytes);
        if (cgba_bulk_tags_smc(tags, bytes))
          flush_translation_cache_ram();       /* SMC: flush now, not via the
                                                  skipped common tail (see the
                                                  FastSet copy-site note) */
      }
#ifdef CGBA_SH4_SWI_OAM_BULK
      else if (!(half &&
                 cgba_sh4_swi_copy_u16_to_oam(
                   (u8 *)(void *)oam_ram, sh, dst, k,
                   &reg[OAM_UPDATED]))) {
#else
      else {
#endif
        int saved = cgba_sh4_extra_cycles;
        u32 i;
        for (i = 0; i < bytes; i += width) {
          if (half)
            execute_store_u16(dst + i, (u32)sh[i] | ((u32)sh[i + 1] << 8));
          else
            execute_store_u32(dst + i, cgba_le32_read(sh + i));
        }
        cgba_sh4_extra_cycles = saved;    /* per_elem is the exact charge */
      }
      src += bytes;
      dst += bytes;
      count -= (s32)k;
      reg[2] = src;                       /* src cursor (canonical) */
      if (half) {
        reg[12] = (u32)sh[bytes - 2] | ((u32)sh[bytes - 1] << 8);
      } else {
        reg[3] = (u32)count;              /* count remaining */
        reg[0] = cgba_le32_read(sh + bytes - 4);   /* last word (escapes) */
      }
      remaining -= (s32)(k * per_elem);
    }
  }

  if (remaining <= 0) {
    u32 ret = update_gba(remaining);
    return completed_frame(ret) ? ret : cycles_to_run(ret);
  }
  return (u32)remaining;
}

#if CGBA_SH4_SWI_HLE_VERIFY
static struct {
  int pending;
  u32 num, ret_pc, budget;
  u32 irq_in0;
  u32 a0, a1, a2;
  struct cgba_swi_pred pred;
} cgba_swi_verify;
u32 cgba_swi_verify_checked, cgba_swi_verify_bad;

static void cgba_swi_verify_report(const char *what, u32 got, u32 want)
{
  volatile unsigned char *d = (volatile unsigned char *)0xb7000000u;
  static const char h[] = "0123456789ABCDEF";
  const char *pfx = "SWIV ";
  int i;
  while (*pfx) *d = (unsigned char)*pfx++;
  while (*what) *d = (unsigned char)*what++;
  *d = ' ';
  for (i = 7; i >= 0; i--) *d = (unsigned char)h[(got >> (i * 4)) & 0xF];
  *d = '/';
  for (i = 7; i >= 0; i--) *d = (unsigned char)h[(want >> (i * 4)) & 0xF];
  *d = '\n';
}
#endif

/* Apply a successful prediction: memory effects at host speed, exact
 * post-registers, exact interp-equivalent cycle charge. The predictors
 * only accept ranges that resolve to linear host memory. */
static int cgba_swi_apply_faithful(u32 src, u32 dst, u32 cnt, int fastset,
                                   const struct cgba_swi_pred *pred)
{
  u32 count = cnt & 0x1FFFFFu;
  u32 fill = (cnt >> 24) & 1;
  u32 word = fastset ? 1 : ((cnt >> 26) & 1);
  u32 width = word ? 4 : 2;
  u32 eff_src = word ? (src & ~3u) : src;
  u32 eff_dst = word ? (dst & ~3u) : dst;
  u32 n = fastset ? ((count + 7) & ~7u) : count;
  u32 len = n * width;
  u8 *tags = NULL;

  if (n) {
    u8 *d = cgba_bulk_ram_host(eff_dst, len, &tags);
    const u8 *sp = NULL;
    if (!d)
      return 0;
    if (!fill) {
      sp = cgba_bulk_src_host(eff_src, len);
      if (!sp)
        return 0;
      if (cgba_bulk_ranges_overlap(sp, d, len)) {
#if CGBA_SH4_SWI_FORWARD_OVERLAP
        if (!cgba_sh4_swi_is_forward_overlap(d, sp, len))
          return 0;                            /* equal/backward: interpreter */
        cgba_sh4_swi_copy_forward(d, sp, len, width);
#else
        return 0;                              /* overlap: interpreter semantics */
#endif
      } else {
        memmove(d, sp, len);
      }
    } else {
      const u8 *vp = cgba_bulk_src_host(eff_src, width);
      u32 i;
      u8 b[4];
      if (!vp)
        return 0;
      b[0] = vp[0]; b[1] = vp[1];
      if (word) { b[2] = vp[2]; b[3] = vp[3]; }
      for (i = 0; i < len; i += width)
        memcpy(d + i, b, width);
    }
    if (cgba_bulk_tags_smc(tags, len))
      cgba_store_alert |= CPU_ALERT_SMC;
    if (pred->r0_from_last_word)
      reg[0] = cgba_le32_read(d + len - 4);
    else
      reg[0] = pred->r0;
  } else {
    reg[0] = pred->r0;
  }
  reg[1] = pred->r1;
  cgba_sh4_extra_cycles += (int)pred->cycles;
  return 1;
}

#if CGBA_SH4_SWI_OBJAFFINE_HLE
static int cgba_swi_objaffineset(u32 src, u32 dst, u32 count, u32 offset,
                                 u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 sp_r = reg[REG_SP] >> 24;
  u32 lr_r = (reg[REG_LR] & ~1u) >> 24;
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 routine, data_est, cycles;
  u32 final_dst = dst;

  if ((spsr_v & 0x20) == 0)
    return 0;                         /* ARM callers need MOVS-pc modeling */
  if (count > 1024u || ((src | dst | offset) & 1u) || offset > 256u)
    return 0;
  if (count) {
    u32 src_len = count * 8u;
    u32 max_delta = offset * (4u * count - 1u);
    final_dst = dst + max_delta;
    if (final_dst < dst || ((final_dst >> 24) != (dst >> 24)) ||
        !cgba_swi_exact_src_host(src, src_len))
      return 0;
  }

  /* Routine 0x8E0..0x97C from open_gba_bios.bin. Data loads/stores inside
   * the loop are performed below via execute_* helpers, so only dispatcher,
   * instruction fetches, literal-table load, and the private push/pop are
   * added to cgba_sh4_extra_cycles here. */
  routine = 7u * CGBA_WS_S(sp_r, 1);   /* PUSH {r4-r10} */
  if (count == 0) {
    routine += 4u * S0 + N0;           /* CMP, PUSH, SUB, BEQ taken */
  } else {
    routine += 9u * S0 + CGBA_WS_N(0, 1); /* setup + sine-table literal */
    routine += count * 29u * S0;
    routine += (count - 1u) * N0;
  }
  routine += 2u * S0 + 7u * CGBA_WS_S(sp_r, 1) + N0; /* POP + BX */

  data_est = count *
    (3u * CGBA_WS_N(src >> 24, 0) + 2u * CGBA_WS_N(0, 0) +
     4u * CGBA_WS_N(dst >> 24, 0));
  cycles = cgba_swi_dispatch_cycles(sp_r, lr_r, 1) + routine + data_est;
  if (cycles + 64u > budget)
    return 0;

  for (u32 i = 0; i < count; i++) {
    u32 theta = execute_load_u16(src + 4u) >> 8;
    s32 rx = (s16)execute_load_u16(src + 0u);
    s32 ry = (s16)execute_load_u16(src + 2u);
    s32 a = (s16)execute_load_u16(0x00002150u + (((theta + 0x40u) & 255u) << 1));
    s32 b = (s16)execute_load_u16(0x00002150u + ((theta & 255u) << 1));
    s32 dmx = (rx * b) >> 14;
    s32 dx  = (rx * a) >> 14;
    s32 dy  = (ry * b) >> 14;
    s32 dmy = (ry * a) >> 14;

    execute_store_u16(dst, (u32)(u16)dx);
    execute_store_u16(dst + offset, (u32)(u16)(-dmx));
    execute_store_u16(dst + offset * 2u, (u32)(u16)dy);
    execute_store_u16(dst + offset * 3u, (u32)(u16)dmy);
    src += 8u;
    dst += offset * 4u;
  }

  reg[0] = src;
  reg[1] = dst;
  cgba_sh4_extra_cycles += (int)(cycles - data_est);
  return 1;
}
#endif

static void cgba_swi_cpuset(u32 source, u32 dest, u32 cnt)
{
  u32 count = cnt & 0x1FFFFF;
  if (((source & 0xe000000) == 0) ||
      ((source + (((cnt << 11) >> 9) & 0x1fffff)) & 0xe000000) == 0)
    return;
  if ((cnt >> 26) & 1) {                          /* 32-bit */
    source &= 0xFFFFFFFC; dest &= 0xFFFFFFFC;
    if (cgba_bulk_cpuset(source, dest, count, (cnt >> 24) & 1, 1, 0x1CAD1CADu))
      return;
    if ((cnt >> 24) & 1) {
      u32 value = (source > 0x0EFFFFFF) ? 0x1CAD1CAD : execute_load_u32(source);
      while (count--) { execute_store_u32(dest, value); dest += 4; }
    } else {
      while (count--) {
        execute_store_u32(dest, (source > 0x0EFFFFFF) ? 0x1CAD1CAD
                                                      : execute_load_u32(source));
        dest += 4; source += 4;
      }
    }
  } else {                                        /* 16-bit */
    if (cgba_bulk_cpuset(source, dest, count, (cnt >> 24) & 1, 0, 0x1CADu))
      return;
    if ((cnt >> 24) & 1) {
      u32 value = (source > 0x0EFFFFFF) ? 0x1CAD : execute_load_u16(source);
      while (count--) { execute_store_u16(dest, value); dest += 2; }
    } else {
      while (count--) {
        execute_store_u16(dest, (source > 0x0EFFFFFF) ? 0x1CAD
                                                      : execute_load_u16(source));
        dest += 2; source += 2;
      }
    }
  }
}

static void cgba_swi_cpufastset(u32 source, u32 dest, u32 cnt)
{
  s32 count;
  if (((source & 0xe000000) == 0) ||
      ((source + (((cnt << 11) >> 9) & 0x1fffff)) & 0xe000000) == 0)
    return;
  source &= 0xFFFFFFFC; dest &= 0xFFFFFFFC;
  count = (s32)(cnt & 0x1FFFFF);
  if (cgba_bulk_cpuset(source, dest, ((u32)count + 7u) & ~7u,
                       (cnt >> 24) & 1, 1, 0xBAFFFFFBu))
    return;
  if ((cnt >> 24) & 1) {
    u32 value = (source > 0x0EFFFFFF) ? 0xBAFFFFFB : execute_load_u32(source);
    while (count > 0) {
      int i;
      for (i = 0; i < 8; i++) { execute_store_u32(dest, value); dest += 4; }
      count -= 8;
    }
  } else {
    while (count > 0) {
      int i;
      for (i = 0; i < 8; i++) {
        execute_store_u32(dest, (source > 0x0EFFFFFF) ? 0xBAFFFFFB
                                                      : execute_load_u32(source));
        source += 4; dest += 4;
      }
      count -= 8;
    }
  }
}

static void cgba_swi_lz77_wram(u32 source, u32 dest)
{
  u32 header = execute_load_u32(source);
  s32 len = (s32)(header >> 8);
  source += 4;
  if (((source & 0xe000000) == 0) ||
      ((source + ((header >> 8) & 0x1fffff)) & 0xe000000) == 0)
    return;
  while (len > 0) {
    u8 d = (u8)execute_load_u8(source++);
    int i;
    if (d) {
      for (i = 0; i < 8; i++) {
        if (d & 0x80) {
          u16 data = (u16)(execute_load_u8(source++) << 8);
          int length, i2; u32 windowOffset;
          data |= (u16)execute_load_u8(source++);
          length = (data >> 12) + 3;
          windowOffset = dest - (data & 0x0FFF) - 1;
          for (i2 = 0; i2 < length; i2++) {
            execute_store_u8(dest++, execute_load_u8(windowOffset++));
            if (--len == 0) return;
          }
        } else {
          execute_store_u8(dest++, execute_load_u8(source++));
          if (--len == 0) return;
        }
        d <<= 1;
      }
    } else {
      for (i = 0; i < 8; i++) {
        execute_store_u8(dest++, execute_load_u8(source++));
        if (--len == 0) return;
      }
    }
  }
}

static void cgba_swi_lz77_vram(u32 source, u32 dest)
{
  u32 header = execute_load_u32(source);
  s32 len = (s32)(header >> 8);
  int byteCount = 0, byteShift = 0;
  u32 writeValue = 0;
  source += 4;
  if (((source & 0xe000000) == 0) ||
      ((source + ((header >> 8) & 0x1fffff)) & 0xe000000) == 0)
    return;
#define CGBA_LZV_PUSH(b)                                                      \
  do { writeValue |= ((u32)(b) << byteShift); byteShift += 8;                 \
       if (++byteCount == 2) {                                                \
         execute_store_u16(dest, writeValue); dest += 2;                      \
         byteCount = 0; byteShift = 0; writeValue = 0;                        \
       }                                                                      \
       if (--len == 0) return;                                                \
  } while (0)
  while (len > 0) {
    u8 d = (u8)execute_load_u8(source++);
    int i;
    if (d) {
      for (i = 0; i < 8; i++) {
        if (d & 0x80) {
          u16 data = (u16)(execute_load_u8(source++) << 8);
          int length, i2; u32 windowOffset;
          data |= (u16)execute_load_u8(source++);
          length = (data >> 12) + 3;
          windowOffset = dest + (u32)byteCount - (data & 0x0FFF) - 1;
          for (i2 = 0; i2 < length; i2++)
            CGBA_LZV_PUSH(execute_load_u8(windowOffset++));
        } else {
          CGBA_LZV_PUSH(execute_load_u8(source++));
        }
        d <<= 1;
      }
    } else {
      for (i = 0; i < 8; i++)
        CGBA_LZV_PUSH(execute_load_u8(source++));
    }
  }
#undef CGBA_LZV_PUSH
}

static void cgba_swi_rl_wram(u32 source, u32 dest)
{
  u32 header = execute_load_u32(source);
  s32 len = (s32)(header >> 8);
  source += 4;
  if (((source & 0xe000000) == 0) ||
      ((source + ((header >> 8) & 0x1fffff)) & 0xe000000) == 0)
    return;
  while (len > 0) {
    u8 d = (u8)execute_load_u8(source++);
    int l = d & 0x7F, i;
    if (d & 0x80) {
      u8 data = (u8)execute_load_u8(source++);
      l += 3;
      for (i = 0; i < l; i++) {
        execute_store_u8(dest++, data);
        if (--len == 0) return;
      }
    } else {
      l++;
      for (i = 0; i < l; i++) {
        execute_store_u8(dest++, execute_load_u8(source++));
        if (--len == 0) return;
      }
    }
  }
}

static void cgba_swi_rl_vram(u32 source, u32 dest)
{
  u32 header = execute_load_u32(source & 0xFFFFFFFC);
  s32 len = (s32)(header >> 8);
  int byteCount = 0, byteShift = 0;
  u32 writeValue = 0;
  source += 4;
  if (((source & 0xe000000) == 0) ||
      ((source + ((header >> 8) & 0x1fffff)) & 0xe000000) == 0)
    return;
#define CGBA_RLV_PUSH(b)                                                      \
  do { writeValue |= ((u32)(b) << byteShift); byteShift += 8;                 \
       if (++byteCount == 2) {                                                \
         execute_store_u16(dest, writeValue); dest += 2;                      \
         byteCount = 0; byteShift = 0; writeValue = 0;                        \
       }                                                                      \
       if (--len == 0) return;                                                \
  } while (0)
  while (len > 0) {
    u8 d = (u8)execute_load_u8(source++);
    int l = d & 0x7F, i;
    if (d & 0x80) {
      u8 data = (u8)execute_load_u8(source++);
      l += 3;
      for (i = 0; i < l; i++)
        CGBA_RLV_PUSH(data);
    } else {
      l++;
      for (i = 0; i < l; i++)
        CGBA_RLV_PUSH(execute_load_u8(source++));
    }
  }
#undef CGBA_RLV_PUSH
}

#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_bios_hle_swi_count;
u32 cgba_swi_miss[48];
u32 cgba_psr_fb[8];
u32 cgba_cap_src[8];
u32 cgba_bios_other_pc[8];
#endif

/* ---- IntrWait / VBlankIntrWait HLE ----------------------------------------
 * The BIOS wait loop `do { halt; CheckInterrupts } while (!flags)` re-enters
 * the interpreter on EVERY delivered IRQ (Zelda: HBlank IRQs -> ~160
 * wake/check round-trips per waited frame, 8-16%% of the whole frame). The
 * loop is a state machine here instead: the guest parks at magic BIOS pc 4
 * in SVC mode between wakes, checks run in C through the IO-side-effecting
 * accessors, and halts hand control to update_gba exactly like the
 * interpreter's halt branch. State 2 = must halt before the first check
 * (matches the do-while shape); state 1 = check-then-halt. */
/* DISABLED pending debug. Progress so far: (1) park must run with IRQs
 * enabled (CPSR.I clear) or nothing can vector; (2) park must run in SYSTEM
 * mode with the return state saved OUTSIDE the banked SVC regs — Zelda's
 * VBlank ISR calls further SWIs (LZ77 VRAM streaming) which re-bank SVC and
 * clobber lr/SPSR; this fixed boot up to the title screen (f300 parity).
 * REMAINING wedge: the title screen's first VBlankIntrWait parks with
 * DISPSTAT.3=1, IE.0=1, IME=1, I=0 — yet REG_IF never flags VBLANK across
 * dozens of frames while frames complete inside the halt loop (single E,
 * zero R in a 600-frame trace). Next step: instrument update_gba's VBlank
 * edge (irq_raised / flag_interrupt) during the park to see why the event
 * doesn't flag. Traces: cgba_iw_trace E/R/N lines via 0xb7000000. */
#ifndef CGBA_SH4_INTRWAIT_HLE
#define CGBA_SH4_INTRWAIT_HLE 0
#endif
#if !CGBA_SH4_INTRWAIT_HLE
int cgba_intrwait_state;              /* referenced by diag traces */
#endif
#if CGBA_SH4_INTRWAIT_HLE
static u32 cgba_intrwait_mask;
int cgba_intrwait_state;          /* 0 off / 1 check / 2 halt-first */
static u32 cgba_intrwait_ret_pc;         /* saved lr_svc: nested SWIs from the
                                            VBlank ISR (Zelda streams VRAM via
                                            LZ77 in its handler) re-bank SVC
                                            and clobber lr/SPSR — the real
                                            BIOS runs this body in SYSTEM
                                            mode for exactly this reason */
static u32 cgba_intrwait_ret_cpsr;

#ifdef CGBA_GPSP_HEADLESS_TEST
static void cgba_iw_trace(char tag, u32 a, u32 b)
{
  static u32 n;
  static const char h[] = "0123456789ABCDEF";
  volatile unsigned char *port = (volatile unsigned char *)0xb7000000u;
  u32 vals[8]; int vi, bi;
  if (n >= 2500) return;
  n++;
  vals[0] = a; vals[1] = b;
  {
    extern u8 iwram[];
    vals[2] = ((u32)iwram[0x7FF8] | ((u32)iwram[0x7FF9] << 8)) |
              (((u32)iwram[0x8000+0x7FF8] | ((u32)iwram[0x8000+0x7FF9] << 8)) << 16);
  }                                                /* low half | high half */
  vals[3] = read_ioreg(REG_IE); vals[4] = read_ioreg(REG_IF);
  vals[5] = read_ioreg(REG_IME); vals[6] = reg[REG_CPSR];
  vals[7] = read_ioreg(REG_DISPSTAT);
  *port='I';*port='W';*port=tag;*port=':';
  for (vi = 0; vi < 8; vi++) {
    for (bi = 7; bi >= 0; bi--) *port = h[(vals[vi]>>(bi*4))&0xF];
    *port=' ';
  }
  *port='\n';
}
#else
#define cgba_iw_trace(t,a,b) ((void)0)
#endif

static u32 cgba_bios_if_check(u32 mask)  /* CheckInterrupts(waitFlags) */
{
  u32 intflags, flags;
  execute_store_u16(0x04000208u, 0);     /* REG_IME = 0 */
  intflags = execute_load_u16(0x03FFFFF8u);
  flags = intflags & mask;
  if (flags)
    execute_store_u16(0x03FFFFF8u, flags ^ intflags);
  execute_store_u16(0x04000208u, 1);     /* REG_IME = 1 */
  return flags;
}

static u32 cgba_intrwait_halt_wait(u32 cycles)
{
  s32 remaining = (s32)cycles;
  reg[CPU_HALT_STATE] = CPU_HALT;
  while (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
    u32 ret;
#ifdef CGBA_GPSP_HEADLESS_TEST
    {
      static u32 dbg_n;
      if ((++dbg_n & 0x3FFFF) == 0) {
        static const char h[] = "0123456789ABCDEF";
        volatile unsigned char *port = (volatile unsigned char *)0xb7000000u;
        u32 vals[5] = { read_ioreg(REG_IE), read_ioreg(REG_IF),
                        read_ioreg(REG_IME), reg[REG_CPSR], reg[REG_PC] };
        int vi, bi;
        *port='I';*port='W';*port=':';
        for (vi = 0; vi < 5; vi++) {
          for (bi = 7; bi >= 0; bi--) *port = h[(vals[vi]>>(bi*4))&0xF];
          *port=' ';
        }
        *port='\n';
      }
    }
#endif
    ret = update_gba((u32)remaining);
    if (completed_frame(ret))
      return ret;                        /* tagged: frame done while parked */
    remaining = (s32)cycles_to_run(ret);
  }
  (void)check_and_raise_interrupts();    /* vector a wake immediately */
  return (u32)(remaining > 0 ? remaining : 1);
}

#endif /* CGBA_SH4_INTRWAIT_HLE */

/* Return-from-SWI shared by the SWI HLEs (movs pc,lr from SVC). */
#if CGBA_SH4_INTRWAIT_HLE
static void cgba_swi_return(void)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  reg[REG_PC] = reg[REG_LR];
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
}
#endif

/* At the SWI vector in SVC mode: run the handled routines natively and
 * return-from-SWI (movs pc,lr: pc = lr_svc, CPSR = SPSR_svc). Returns 0 for
 * anything else — the interpreter path is unchanged for those. */
/* Per-SWI interp fallback census: [num] counts SWIs the HLE declined (so the
 * real BIOS interprets them). Defined unconditionally so diag prints link
 * everywhere; incremented only under CGBA_SH4_DIAG_COUNTERS. */
unsigned long cgba_swi_interp_n[32];
unsigned long cgba_swi_interp_words[32];   /* CpuSet/FastSet/UnComp payload */

static int cgba_hle_bios_swi_num(u32 num, u32 budget);

static int cgba_hle_bios_swi(u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];
  u32 num;
  int handled;

  if (spsr_v & 0x20)
    num = execute_load_u16(lr - 2) & 0xFF;
  else
    num = (execute_load_u32(lr - 4) >> 16) & 0xFF;

  handled = cgba_hle_bios_swi_num(num, budget);
#if CGBA_SH4_DIAG_COUNTERS
  if (!handled && num < 32) {
    cgba_swi_interp_n[num]++;
    if (num == 0x0B || num == 0x0C)
      cgba_swi_interp_words[num] += reg[2] & 0x1FFFFFu;
    else if (num >= 0x11 && num <= 0x18 && reg[0] < 0x10000000u) {
      /* UnComp family: header word at [r0] has size<<8 */
      const u8 *hp = cgba_bulk_src_host(reg[0] & ~3u, 4);
      if (hp)
        cgba_swi_interp_words[num] += cgba_le32_read(hp) >> 8;
    }
  }
#endif
  return handled;
}

static int cgba_hle_bios_swi_num(u32 num, u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];

  switch (num) {
#if CGBA_SH4_INTRWAIT_HLE
  case 0x04: case 0x05: {                /* IntrWait / VBlankIntrWait */
    u32 mask = (num == 5) ? 1u : reg[1];
    u32 discard = (num == 5) ? 1u : reg[0];
    if (cgba_intrwait_state) {
      cgba_iw_trace('N', num, reg[REG_LR]);
      return 0;                          /* nested IntrWait: interpreter */
    }
    cgba_iw_trace('E', num, reg[REG_LR]);
    cgba_intrwait_mask = mask;
    cgba_intrwait_state = 2;             /* halt before the first check */
    cgba_intrwait_ret_pc = reg[REG_LR];  /* save OUTSIDE banked SVC regs */
    cgba_intrwait_ret_cpsr = REG_SPSR(MODE_SUPERVISOR);
    /* Park in SYSTEM mode with IRQs enabled, like the real BIOS body: the
       ISR may invoke further SWIs, which re-enter SVC. */
    reg[REG_PC] = 0x00000004u;
    reg[REG_CPSR] = (reg[REG_CPSR] & 0xF0000000u) | 0x1Fu;
    set_cpu_mode(MODE_SYSTEM);
    if (discard)
      (void)cgba_bios_if_check(mask);
    (void)check_and_raise_interrupts();  /* IME=1 with pending -> vector now */
#ifdef CGBA_GPSP_HEADLESS_TEST
    cgba_bios_hle_swi_count++;
#endif
    return 2;                            /* handled, but NOT a swi-return */
  }
#endif
#if CGBA_SH4_SWI_OBJAFFINE_HLE
  case 0x0F:
    if (!cgba_swi_objaffineset(reg[0], reg[1], reg[2], reg[3], budget))
      return 0;
    break;
#endif
#if CGBA_SH4_SWI_CPUSET_FAITHFUL
  case 0x0B:
  case 0x0C: {
    /* Register- and cycle-faithful fast path; anything unmodeled falls back
       to the interpreted BIOS below (return 0). */
    struct cgba_swi_pred pred;
    int ok = (num == 0x0B)
      ? cgba_swi_cpuset_predict(reg[0], reg[1], reg[2], &pred)
      : cgba_swi_cpufastset_predict(reg[0], reg[1], reg[2], &pred);
    if (!ok)
      return 0;
    /* Only take the fast path when the whole SWI fits the CURRENT event
       slice: larger ones span update_gba windows, where the interpreter
       interleaves events (and can vector IRQs) MID-COPY — atomically
       debiting the total would move those guest-visible events after the
       copy. Big copies stay on the interpreter until the parked/resumable
       variant exists (chunked per-word debit with IRQ exits, IntrWait-
       style park state). */
    if (pred.cycles + 64u > budget) {
#if !CGBA_SH4_SWI_HLE_VERIFY
      /* Oversized copies: parked/resumable chunked execution, canonical at
       * every pause point so IRQs vector exactly as the interpreter would.
       * Fills stay interpreted (rare). */
      u32 cnt = reg[2] & 0x1FFFFFu, fill = (reg[2] >> 24) & 1;
      if (num == 0x0C && budget > 0 && cnt != 0) {
        cgba_swi_fastset_materialize(fill);
        return 3;                        /* caller runs the FastSet engine */
      }
      if (num == 0x0B && budget > 0 && cnt != 0 && !fill) {
        cgba_swi_cpuset_materialize(((reg[2] >> 26) & 1) ? 0 : 1);
        return 4;                        /* caller runs the CpuSet engine */
      }
#endif
      return 0;
    }
#if CGBA_SH4_SWI_HLE_VERIFY
    if (pred.r0_from_last_word) {
      u32 wsrc = reg[0] & ~3u, cn = reg[2] & 0x1FFFFFu;
      u32 wi = cn - 1u;
      const u8 *sp2 = cgba_bulk_src_host(wsrc, cn * 4);
      if (!sp2)
        return 0;
#if CGBA_SH4_SWI_FORWARD_OVERLAP
      {
        u32 wdst = reg[1] & ~3u;
        if (wdst > wsrc && wdst - wsrc < cn * 4u) {
          u32 delta_words = (wdst - wsrc) >> 2;
          if (!delta_words)
            return 0;
          wi %= delta_words;                   /* earlier stores feed this load */
        }
      }
#endif
      pred.r0 = cgba_le32_read(sp2 + wi * 4u);
      pred.r0_from_last_word = 0;
    }
    cgba_swi_verify.pending = 1;
    cgba_swi_verify.num = num;
    cgba_swi_verify.ret_pc = reg[REG_LR] & ~1u;
    cgba_swi_verify.pred = pred;
    cgba_swi_verify.irq_in0 = cgba_bios_hle_irq_in;
    cgba_swi_verify.a0 = reg[0]; cgba_swi_verify.a1 = reg[1];
    cgba_swi_verify.a2 = reg[2];
    {   /* one-shot per-insn cycle trace of the first traced CpuFastSet */
      static int traced;
      extern int cgba_diff_trace_cycles;
      if (!traced && num == 0x0C && reg[2] == 0x000000E0u &&
          reg[1] == 0x07000080u) {
        traced = 1;
        cgba_swi_verify_report("TRC0", reg[0], reg[1]);
        cgba_swi_verify_report("TRC1", reg[2],
          ((u32)ws_cyc_seq[0][1] << 24) | ((u32)ws_cyc_nseq[0][1] << 16) |
          ((u32)ws_cyc_seq[3][1] << 8) | (u32)ws_cyc_nseq[3][1]);
        cgba_swi_verify_report("TRC2", pred.cycles,
          ((u32)ws_cyc_seq[reg[REG_SP] >> 24][1] << 8) |
          (u32)ws_cyc_nseq[reg[REG_SP] >> 24][1]);
        cgba_diff_trace_cycles = 1;
      }
    }
    return 0;                          /* run the real BIOS; compare after */
#else
    if (!cgba_swi_apply_faithful(reg[0], reg[1], reg[2], num == 0x0C, &pred))
      return 0;
    break;                             /* handled -> common HLE tail */
#endif
  }
#endif
#if CGBA_SH4_SWI_MEM_HLE
  /* DEMOTED (default off): these HLEs reproduce the copy/decompress MEMORY
   * effects but not the open BIOS routines' post-SWI scratch registers
   * (CpuSet leaves r1 = dst-src, r2/r3 = cursor ends, r12 = last datum...)
   * nor their instruction-fetch cycle cost. Once the cold gate started
   * promoting SWI call sites (stop-on-hot), the JIT run serviced these via
   * the HLE while the interpreter ran the real BIOS — Metroid's dense
   * bit-exact soak diverged from frame ~420 on exactly that asymmetry.
   * Interpreting the real BIOS is symmetric for both cores by construction.
   * Re-enable only with register- AND cycle-faithful implementations
   * (post-state formulas derivable from the blob: see the session notes /
   * open_gba_bios.bin disassembly around 0x614/0x720). */
  case 0x0B: cgba_swi_cpuset(reg[0], reg[1], reg[2]); break;
  case 0x0C: cgba_swi_cpufastset(reg[0], reg[1], reg[2]); break;
  case 0x11: cgba_swi_lz77_wram(reg[0], reg[1]); break;
  case 0x12: cgba_swi_lz77_vram(reg[0], reg[1]); break;
  case 0x14: cgba_swi_rl_wram(reg[0], reg[1]); break;
  case 0x15: cgba_swi_rl_vram(reg[0], reg[1]); break;
#endif
  default:
#ifdef CGBA_GPSP_HEADLESS_TEST
    if (num < 48) cgba_swi_miss[num]++;
#endif
    return 0;
  }

  if (cgba_store_alert) {
    cpu_alert_type a = cgba_store_alert;
    cgba_store_alert = CPU_ALERT_NONE;
    if (a & CPU_ALERT_SMC)
      flush_translation_cache_ram();
  }
  reg[REG_PC] = lr;
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_swi_count++;
#endif
  return 1;
}

/* Parked IntrWait re-entry (pc 4, SVC): check, complete, or halt again.
 * Returns the fallback's budget/tagged contract value. */
#if CGBA_SH4_INTRWAIT_HLE
static u32 cgba_hle_intrwait_step(u32 cycles)
{
  u32 flags;

  /* S-trace disabled: too chatty for the long trace */
  if (cgba_intrwait_state == 2) {
    cgba_intrwait_state = 1;
    return cgba_intrwait_halt_wait(cycles);   /* do { HALT; ... } shape */
  }
  flags = cgba_bios_if_check(cgba_intrwait_mask);
  (void)check_and_raise_interrupts();
  cgba_iw_trace('C', flags, reg[REG_PC]);
  if (reg[REG_PC] != 0x00000004u)
    return cycles;                       /* IRQ preempted at IME=1: vector */
  if (flags) {
    cgba_intrwait_state = 0;
    reg[REG_PC] = cgba_intrwait_ret_pc;
    reg[REG_CPSR] = cgba_intrwait_ret_cpsr;
    set_cpu_mode(cpu_modes[cgba_intrwait_ret_cpsr & 0xF]);
    cgba_iw_trace('R', reg[REG_PC], reg[REG_CPSR]);
    return cycles;
  }
  return cgba_intrwait_halt_wait(cycles);
}
#endif /* CGBA_SH4_INTRWAIT_HLE */

/* Interpreter SWI hook: cpu.cc calls this right after vectoring a guest SWI
 * to 0x08 (LR_svc/SPSR_svc/SVC mode already established — the same state the
 * JIT stub presents). Cold-gated code executing SWIs otherwise interprets
 * the REAL BIOS bodies (AW: 359 entries / 2.36M interpreted instructions per
 * 2000 frames, resumed mid-copy at 0x6B4/0x7A8 after IRQs). Returns nonzero
 * when serviced, with reg[REG_PC]/CPSR/mode restored to the caller and the
 * HLE's data-access wait states in *spent for the interpreter to debit (the
 * JIT path debits them from R13 via the stub instead). Only compiled-in HLE
 * cases return 1; the gated-off IntrWait park (return 2) cannot occur while
 * CGBA_SH4_INTRWAIT_HLE is 0, and must stay on the interpreted path if it is
 * ever enabled here. */
int cgba_sh4_interp_swi_hle(s32 *spent)
{
  int saved = cgba_sh4_extra_cycles;
  int handled;

#if !CGBA_SH4_INTERP_SWI_HLE
  /* DISABLED: servicing interpreter SWIs via the HLE measured +2.85 fps on
   * AW but broke the Metroid dense JIT-vs-interp bit-exactness (boot-phase
   * IRQ count shifted 1203 -> 1252; first pixel divergence by frame 200;
   * bisect-confirmed). The HLE is cheaper than interpreting the real BIOS
   * body (only data accesses are charged) and does not reproduce the real
   * routines' post-SWI scratch-register state, so cold-context SWIs must
   * keep interpreting the real BIOS until the HLE is made cycle- and
   * register-faithful. Re-enable with CGBA_SH4_INTERP_SWI_HLE=1. */
  (void)saved; (void)handled; (void)spent;
  return 0;
#else
  if (CGBA_SH4_DIFF_ACTIVE())
    return 0;
  cgba_sh4_extra_cycles = 0;
  handled = (cgba_hle_bios_swi(0) == 1);
  *spent = handled ? cgba_sh4_extra_cycles : 0;
  cgba_sh4_extra_cycles = saved;
  return handled;
#endif
}

u32 cgba_sh4_bios_fallback(u32 cycles)
{
#ifdef CGBA_GPSP_HEADLESS_TEST
  u32 cgba_entry_pc_bios = reg[REG_PC];
#endif
  /* IRQ wrapper HLE: the asm dispatch stubs route every pc < 0x4000 here
   * before the C resolver can see it, so the vector/epilogue fast paths hook
   * the fallback itself. On success reg[REG_PC] is the game handler (or the
   * interrupted code) and the stub's lookup_pc redispatches natively. */
#if CGBA_SH4_INTRWAIT_HLE
  if (!CGBA_SH4_DIFF_ACTIVE() && cgba_intrwait_state &&
      reg[REG_PC] == 0x00000004u && reg[CPU_MODE] == MODE_SYSTEM)
    return cgba_hle_intrwait_step(cycles);
#endif
  if (!CGBA_SH4_DIFF_ACTIVE() && reg[REG_PC] == 0x00000008u &&
      reg[CPU_MODE] == MODE_SUPERVISOR) {
    int h = cgba_hle_bios_swi(cycles);
    if (h == 3)
      return cgba_swi_fastset_engine((s32)cycles);
    if (h == 4)
      return cgba_swi_cpuset_engine((s32)cycles);
    if (h)
      return cycles;                 /* PC/mode restored; stub redispatches */
  }
  if (!CGBA_SH4_DIFF_ACTIVE() && reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_FS_COPY_LOOP || reg[REG_PC] == CGBA_FS_FILL_LOOP)) {
    /* Re-entry at a canonical FastSet chunk top (ISR returned into a parked
     * copy, or the interpreter stopped exactly there). Declined -> the
     * interpreter continues the real BIOS from the same state. */
    u32 r = cgba_swi_fastset_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (!CGBA_SH4_DIFF_ACTIVE() && reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_CS_WCOPY_LOOP || reg[REG_PC] == CGBA_CS_HCOPY_LOOP)) {
    /* Re-entry at a canonical CpuSet chunk top (ISR returned mid-copy). */
    u32 r = cgba_swi_cpuset_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (!CGBA_SH4_DIFF_ACTIVE() && reg[CPU_MODE] == MODE_IRQ) {
    if (reg[REG_PC] == 0x00000018u) {
      if (cgba_hle_bios_irq_entry() != 0)
        return cycles;
    } else if (reg[REG_PC] == 0x00000030u) {
      u32 np = cgba_hle_bios_irq_exit();
      if (np != 0x00000018u)
        return cycles;
      if (cgba_hle_bios_irq_entry() != 0)   /* immediate pending re-entry */
        return cycles;
      /* odd handler: fall through and interpret from the vector */
    }
  }
  /* Interpret ONLY while the PC is inside the BIOS (region-exit stop mode,
   * cpu.cc CGBA_DIFF_STOP_CHECK): the instant execution reaches game code
   * (PC >= 0x4000) control returns and the stub re-dispatches into the JIT.
   * This holds for every entry — SWI (BIOS body, incl. the IntrWait halt
   * loop, which the interpreter fast-forwards), and IRQ vectors (0x18 -> the
   * ~10-instruction BIOS wrapper -> the GAME's handler, which now runs
   * translated; the handler's return to the BIOS epilogue comes back here
   * for the final ldm/subs). The previous version only stopped at a known
   * SWI return LR, so every VBlank/HBlank IRQ interpreted the whole rest of
   * the frame — the profile showed 68%% of a JIT run inside execute_arm. */
  cgba_diff_stop_on_bios_exit = 1;
  cgba_diff_stop_active = 1;
  cgba_diff_stop_skip_initial = 0;
  cgba_diff_stop_cycles_remaining = (s32)cycles;
  execute_arm(cycles);
  cgba_diff_stop_active = 0;
  cgba_diff_stop_on_bios_exit = 0;
#if CGBA_SH4_SWI_HLE_VERIFY
  if (cgba_swi_verify.pending) {
    cgba_swi_verify.pending = 0;
    /* Only comparable when the interpreter ran the whole SWI and returned
       to the caller (an IRQ vector or budget exhaustion mid-BIOS lands
       elsewhere — discard those records). */
    {
      extern int cgba_diff_trace_cycles;
      cgba_diff_trace_cycles = 0;
    }
    if (reg[REG_PC] == cgba_swi_verify.ret_pc &&
        cgba_bios_hle_irq_in == cgba_swi_verify.irq_in0 &&
        cgba_diff_stop_cycles_remaining >= 0 &&
        (s32)cycles > cgba_diff_stop_cycles_remaining) {
      u32 used = (u32)((s32)cycles - cgba_diff_stop_cycles_remaining);
      cgba_swi_verify_checked++;
      if (reg[0] != cgba_swi_verify.pred.r0) {
        cgba_swi_verify_bad++;
        cgba_swi_verify_report("r0", reg[0], cgba_swi_verify.pred.r0);
      }
      if (reg[1] != cgba_swi_verify.pred.r1) {
        cgba_swi_verify_bad++;
        cgba_swi_verify_report("r1", reg[1], cgba_swi_verify.pred.r1);
      }
      if (used != cgba_swi_verify.pred.cycles) {
        static unsigned dumped;
        cgba_swi_verify_bad++;
        cgba_swi_verify_report(cgba_swi_verify.num == 0x0B ? "cycB" : "cycC",
                               used, cgba_swi_verify.pred.cycles);
        if (dumped < 8) {
          dumped++;
          cgba_swi_verify_report("args01", cgba_swi_verify.a0,
                                 cgba_swi_verify.a1);
          cgba_swi_verify_report("args2L", cgba_swi_verify.a2,
                                 cgba_swi_verify.ret_pc);
          cgba_swi_verify_report("budrem", cycles,
                                 (u32)cgba_diff_stop_cycles_remaining);
          cgba_swi_verify_report("pcr0r1",
                                 (reg[0] << 16) | (reg[1] & 0xFFFFu),
                                 reg[REG_PC]);
        }
      }
    }
  }
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_bios_fallback_call_count++;
  {
    s32 used = (s32)cycles - cgba_diff_stop_cycles_remaining;
    if (used > 0)
      cgba_sh4_bios_fallback_cycle_count += (u32)used;
  }
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
  /* classify entries: SWI vector / IRQ vector / mid-BIOS resume */
  if (cgba_entry_pc_bios == 0x08) cgba_bios_entry_swi++;
  else if (cgba_entry_pc_bios == 0x18) cgba_bios_entry_irq++;
  else {
    unsigned oi;
    cgba_bios_entry_other++;
    for (oi = 0; oi < 8; oi++) {
      if (cgba_bios_other_pc[oi] == cgba_entry_pc_bios) break;
      if (cgba_bios_other_pc[oi] == 0) { cgba_bios_other_pc[oi] = cgba_entry_pc_bios; break; }
    }
  }
#endif

  if (reg[REG_PC] >= 0x00004000u ||
      (reg[REG_PC] == 0x00000004u && cgba_intrwait_state)) {
    /* Left the BIOS — or returned to the IntrWait park, which must
       re-dispatch so the park hook runs instead of the real vector code. */
    s32 remaining = cgba_diff_stop_cycles_remaining;
    if (remaining <= 0) {
      u32 ret = update_gba(remaining);
      return completed_frame(ret) ? ret : cycles_to_run(ret);
    }
    return (u32)remaining;
  }
  return 0x80000000u;                    /* frame completed inside the BIOS */
}

/* Interpret a small chunk of not-yet-hot code (cold-code gate). Entered from
 * sh4_cold_interp_entry with R4 = remaining JIT cycles; same return contract
 * as the BIOS fallback: new cycle budget, or 0x80000000 when the frame
 * completed inside the interpreter. */
u32 cgba_sh4_cold_interp(u32 cycles)
{
  s32 budget = (s32)cycles;
  s32 limit = (s32)CGBA_SH4_COLD_CHUNK_CYCLES;
  s32 chunk = (limit > 0 && budget > limit) ? limit : budget;
  s32 sentinel = (s32)0x7FFFFFFF;
  s32 used;

  if (chunk <= 0)
    chunk = 1;                           /* always make forward progress */
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_cold_interp_count++;
#endif
  cgba_diff_stop_on_budget = 1;
  cgba_diff_stop_active = 1;
  cgba_diff_stop_skip_initial = 0;
  cgba_diff_stop_cycles_remaining = sentinel;
  execute_arm((u32)chunk);
  cgba_diff_stop_active = 0;
  cgba_diff_stop_on_budget = 0;

  if (cgba_diff_stop_cycles_remaining == sentinel)
    return 0x80000000u;                  /* frame completed inside (cannot
                                            happen with the hard stops, but
                                            keep the safe interpretation) */
  used = chunk - cgba_diff_stop_cycles_remaining;
  budget -= used;

  /* A halt raised inside the chunk is handed back undigested: run the sleep
   * loop here (the stub funnels expect an ACTIVE cpu after this returns). */
  while (reg[CPU_HALT_STATE] != 0 /* CPU_ACTIVE */) {
    u32 ret = update_gba(budget);
    if (completed_frame(ret))
      return ret;
    budget = (s32)cycles_to_run(ret);
  }
  if (budget <= 0) {
    u32 ret = update_gba(budget);
    return completed_frame(ret) ? ret : cycles_to_run(ret);
  }
  return (u32)budget;
}

/* Host-emitter init hook (main.c calls this under HAVE_DYNAREC). The MIPS/x86
 * backends populate inline memory-handler dispatch tables here; the bring-up
 * SH4 backend routes memory through C helpers, so there is nothing to set up. */
void init_emitter(bool must_swap)
{
  (void)must_swap;
}

/* Public dynarec entry (emitter-provided in the other backends): run translated
 * code for up to `cycles`, with R5 pointing at the guest register file. */
u32 execute_arm_translate(u32 cycles)
{
  return execute_arm_translate_internal(cycles, &reg[0]);
}
