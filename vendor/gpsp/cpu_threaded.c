/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// Not-so-important todo:
// - stm reglist writeback when base is in the list needs adjustment
// - block memory needs psr swapping and user mode reg swapping

#include "common.h"
#include "sh4/sh4_jit_safety.h"
#ifdef CGBA_SH4_DIFF_HARNESS
#define CGBA_SH4_DIFF_ACTIVE() (cgba_dynarec_single_block != 0)
#else
#define CGBA_SH4_DIFF_ACTIVE() 0
#endif
#ifdef SH4_ARCH
#if defined(CGBA_SH4_COMPACT_FASTMEM_TUPLE) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && \
    (!defined(CGBA_SH4_FASTMEM_OOL_DESC) || CGBA_SH4_FASTMEM_OOL_DESC)
#define CGBA_SH4_RAM_CODE_START(cache) ((cache) + CGBA_SH4_RAM_FM_DESC_BYTES)
#define CGBA_SH4_RAM_CODE_WATERMARK CGBA_SH4_RAM_FM_DESC_BYTES
#else
#define CGBA_SH4_RAM_CODE_START(cache) CGBA_SH4_RAM_CACHE_START(cache)
#define CGBA_SH4_RAM_CODE_WATERMARK CGBA_SH4_RAM_CACHE_WATERMARK
#endif
#if RAM_TRANSLATION_CACHE_SIZE <= \
    CGBA_SH4_RAM_CODE_WATERMARK + TRANSLATION_CACHE_LIMIT_THRESHOLD + \
    CGBA_SH4_RAM_TAG_BYTES
#error "SH4 RAM translation cache is too small for its descriptor/sentinel reserve"
#endif
#endif
#if defined(VITA)
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#elif defined(PS2)
#include <kernel.h>
#endif

u8 *last_rom_translation_ptr = NULL;
u8 *last_ram_translation_ptr = NULL;

#if defined(MMAP_JIT_CACHE)
u8* rom_translation_cache;
u8* ram_translation_cache;
u8 *rom_translation_ptr;
u8 *ram_translation_ptr;
#elif defined(VITA)
u8* rom_translation_cache;
u8* ram_translation_cache;
u8 *rom_translation_ptr;
u8 *ram_translation_ptr;
int sceBlock;
#elif defined(_3DS) 
u8* rom_translation_cache_ptr;
u8* ram_translation_cache_ptr;
u8 *rom_translation_ptr = rom_translation_cache;
u8 *ram_translation_ptr = ram_translation_cache;
#else
u8 *rom_translation_ptr = rom_translation_cache;
#ifdef SH4_ARCH
u8 *ram_translation_ptr = CGBA_SH4_RAM_CODE_START(ram_translation_cache);
#else
u8 *ram_translation_ptr = ram_translation_cache;
#endif
#endif
/* Note, see stub files for more cache definitions */

/* `min == ~0U` is the unseen sentinel; guest I/EWRAM offset zero is valid
 * translated code.  Host RAM-cache byte zero is a separate sentinel.
 * `cgba_*_code_end` is the exclusive upper byte bound of all written tags. */
u32 iwram_code_min = ~0U;
u32 cgba_iwram_code_end = 0U;
u32 ewram_code_min = ~0U;
u32 cgba_ewram_code_end = 0U;

#define INITIAL_ROM_WATERMARK   16   // To avoid NULL aliasing
u32 rom_cache_watermark = INITIAL_ROM_WATERMARK;

u8 *bios_swi_entrypoint = NULL;

#ifdef SH4_ARCH
u32 cgba_hle_bios_irq_entry(void);   /* sh4_interp_helpers.c */
u32 cgba_hle_bios_irq_exit(void);

/* Per-256-byte IWRAM SMC quarantine.  Games with a small self-modifying stub
 * in IWRAM (e.g., an audio mixer patching 1-2 loop instructions each frame)
 * otherwise trigger flush_translation_cache_ram() multiple times per frame and
 * wipe hundreds of KiB of unrelated IWRAM code.  After CGBA_SH4_SMC_HEAT_THRESH
 * CPU-store SMC hits on the same 256 B IWRAM page, quarantine that page: never
 * translate blocks starting in or spanning into it, and dispatch entries in it
 * to sh4_cold_interp_entry.  Because no tag bits are ever set in a quarantined
 * page after the triggering flush, subsequent self-modifications to that page
 * take the zero-tag fast path without flushing the rest of the RAM cache. */
#define CGBA_SH4_SMC_PAGE_SHIFT   8u
#define CGBA_SH4_SMC_PAGE_COUNT   (0x8000u >> CGBA_SH4_SMC_PAGE_SHIFT)
#define CGBA_SH4_SMC_HEAT_THRESH  2u
u8  cgba_iwram_smc_heat[CGBA_SH4_SMC_PAGE_COUNT];
u8  cgba_iwram_smc_quarantine[CGBA_SH4_SMC_PAGE_COUNT];
u32 cgba_iwram_smc_quarantine_any = 0u;

void cgba_sh4_note_iwram_smc(u32 iwram_off)
{
  u32 page = (iwram_off & 0x7FFFu) >> CGBA_SH4_SMC_PAGE_SHIFT;
  if (cgba_iwram_smc_heat[page] < 255u)
    cgba_iwram_smc_heat[page]++;
  if (cgba_iwram_smc_heat[page] >= CGBA_SH4_SMC_HEAT_THRESH) {
    cgba_iwram_smc_quarantine[page] = 1u;
    cgba_iwram_smc_quarantine_any = 1u;
  }
}

static inline void cgba_sh4_reset_iwram_smc_quarantine(void)
{
  memset(cgba_iwram_smc_heat, 0, sizeof(cgba_iwram_smc_heat));
  memset(cgba_iwram_smc_quarantine, 0, sizeof(cgba_iwram_smc_quarantine));
  cgba_iwram_smc_quarantine_any = 0u;
}

static inline int cgba_sh4_iwram_page_quarantined(u32 pc)
{
  return __builtin_expect(cgba_iwram_smc_quarantine_any != 0u, 0) &&
         (pc >> 24) == 0x03u &&
         cgba_iwram_smc_quarantine[(pc & 0x7FFFu) >> CGBA_SH4_SMC_PAGE_SHIFT];
}

static inline u32 cgba_sh4_iwram_quarantine_cap(u32 start, u32 width, u32 cap)
{
  if (__builtin_expect(cgba_iwram_smc_quarantine_any != 0u, 0) &&
      (start >> 24) == 0x03u) {
    u32 p0 = (start & 0x7FFFu) >> CGBA_SH4_SMC_PAGE_SHIFT;
    u32 p;
    for (p = p0 + 1u; p < CGBA_SH4_SMC_PAGE_COUNT; p++) {
      if (cgba_iwram_smc_quarantine[p]) {
        u32 lim = (start & ~0x7FFFu) + (p << CGBA_SH4_SMC_PAGE_SHIFT);
        u32 n = (lim - start) / width;
        if (n < cap)
          cap = n;
        break;
      }
    }
  }
  return cap;
}
#define cgba_sh4_set_cold_pending() (cgba_cold_pending = 1)
#else
static inline int cgba_sh4_iwram_page_quarantined(u32 pc)
{
  (void)pc;
  return 0;
}
static inline u32 cgba_sh4_iwram_quarantine_cap(u32 start, u32 width, u32 cap)
{
  (void)start;
  (void)width;
  return cap;
}
static inline void cgba_sh4_reset_iwram_smc_quarantine(void) {}
#define cgba_sh4_set_cold_pending() ((void)0)
#endif

// Contains an offset table to rom_translation cache area
// It features a chaining linked list for collisions
// The rom area has a small header section that contains:
//  - PC value for the entry
//  - Offset to the next entry (if any)
typedef struct
{
  u32 pc_value;
  u32 next_entry;
} hashhdr_type;

u32 rom_branch_hash[ROM_BRANCH_HASH_SIZE] CGBA_HIGH_BSS;
u32 cgba_dynarec_arm_hot[128] CGBA_HIGH_BSS;   /* 64 interleaved {key, ptr} */
u32 cgba_dynarec_thumb_hot[512] CGBA_HIGH_BSS; /* 256 interleaved {key, ptr} */
u32 cgba_dynarec_dual_hot[512] CGBA_HIGH_BSS;  /* 256 interleaved {key, ptr} */

/* Always kept: the ClassPad settings page shows them. */
u32 cgba_dynarec_rom_flush_count;
u32 cgba_dynarec_ram_flush_count;
u32 cgba_dynarec_ram_full_count;   /* of those, because the cache was full */
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
#define CGBA_SH4_PROF_BITS 11
#define CGBA_SH4_PROF_SLOTS (1u << CGBA_SH4_PROF_BITS)
struct cgba_sh4_prof_row {
  u32 key;
  u32 count;
};

u32 cgba_dynarec_arm_translate_count;
u32 cgba_dynarec_thumb_translate_count;
u32 cgba_dynarec_lookup_arm_count;
u32 cgba_dynarec_lookup_thumb_count;
u32 cgba_dynarec_lookup_dual_count;
u32 cgba_dynarec_icache_sync_count;
u32 cgba_dynarec_icache_sync_bytes;
u32 cgba_dynarec_patch_sync_count;
u32 cgba_dynarec_patch_sync_bytes;
u32 cgba_dynarec_ibh_arm_hit_count;
u32 cgba_dynarec_ibh_arm_slow_count;
u32 cgba_dynarec_ibh_arm_hot_count;
u32 cgba_dynarec_ibh_thumb_hit_count;
u32 cgba_dynarec_ibh_thumb_slow_count;
u32 cgba_dynarec_ibh_thumb_hot_count;
u32 cgba_dynarec_ibh_dual_arm_hit_count;
u32 cgba_dynarec_ibh_dual_thumb_hit_count;
u32 cgba_dynarec_ibh_dual_slow_count;
u32 cgba_dynarec_ibh_dual_hot_arm_count;
u32 cgba_dynarec_ibh_dual_hot_thumb_count;
u32 cgba_sh4_prof_key[CGBA_SH4_PROF_SLOTS] CGBA_HIGH_BSS;
u32 cgba_sh4_prof_count[CGBA_SH4_PROF_SLOTS] CGBA_HIGH_BSS;
u32 cgba_sh4_prof_overflow_count;
u32 cgba_sh4_prof_entry_count;

u32 *cgba_sh4_prof_counter_for_key(u32 key)
{
  key = (key & 0x0fffffffu) | 0x80000000u;
  u32 slot = (key * 2654435761u) >> (32 - CGBA_SH4_PROF_BITS);

  for(u32 i = 0; i < CGBA_SH4_PROF_SLOTS; i++) {
    u32 at = (slot + i) & (CGBA_SH4_PROF_SLOTS - 1);
    if(cgba_sh4_prof_key[at] == key)
      return &cgba_sh4_prof_count[at];
    if(cgba_sh4_prof_key[at] == 0) {
      cgba_sh4_prof_key[at] = key;
      cgba_sh4_prof_count[at] = 0;
      cgba_sh4_prof_entry_count++;
      return &cgba_sh4_prof_count[at];
    }
  }

  return &cgba_sh4_prof_overflow_count;
}

void cgba_sh4_prof_reset(void)
{
  memset(cgba_sh4_prof_key, 0, sizeof(cgba_sh4_prof_key));
  memset(cgba_sh4_prof_count, 0, sizeof(cgba_sh4_prof_count));
  cgba_sh4_prof_overflow_count = 0;
  cgba_sh4_prof_entry_count = 0;
}

unsigned cgba_sh4_prof_top(struct cgba_sh4_prof_row *out, unsigned max)
{
  unsigned n = 0;

  for(unsigned want = 0; want < max; want++) {
    u32 best_count = 0;
    u32 best_key = 0;
    unsigned best_slot = 0;

    for(unsigned i = 0; i < CGBA_SH4_PROF_SLOTS; i++) {
      u32 count = cgba_sh4_prof_count[i];
      if(count <= best_count)
        continue;
      int already = 0;
      for(unsigned j = 0; j < n; j++) {
        if(out[j].key == cgba_sh4_prof_key[i]) {
          already = 1;
          break;
        }
      }
      if(already)
        continue;
      best_count = count;
      best_key = cgba_sh4_prof_key[i];
      best_slot = i;
    }

    if(cgba_sh4_prof_overflow_count > best_count) {
      int already = 0;
      for(unsigned j = 0; j < n; j++) {
        if(out[j].key == 0xffffffffu) {
          already = 1;
          break;
        }
      }
      if(!already) {
        best_count = cgba_sh4_prof_overflow_count;
        best_key = 0xffffffffu;
      }
    }

    (void)best_slot;
    if(best_count == 0)
      break;
    out[n].key = best_key;
    out[n].count = best_count;
    n++;
  }

  return n;
}
#endif

typedef struct
{
  u8 *block_offset;
#ifdef SH4_ARCH
  u8 *body_offset;
  u16 raw_flag_data;
#endif
  u16 flag_data;
  u8 condition;
  u8 update_cycles;
} block_data_type;

#ifdef SH4_ARCH
static inline int sh4_flags_dead_from(const block_data_type *bd,
                                      s32 start, s32 end, u32 need_kill)
{
  u32 killed = 0;
  s32 lim = start + 16;
  s32 p;
  if (start < 0 || start >= end)
    return 0;
  if (lim > end)
    lim = end;
  for (p = start; p < lim; p++) {
    u16 fd;
    u32 req;
    if (p > start && (bd[p].update_cycles & 1u))
      return 0;
    fd = bd[p].raw_flag_data;
    req = (fd >> 8) & 0x0Fu;
    if (req & ~killed)
      return 0;
    killed |= (fd >> 4) & 0x0Fu;
    if ((killed & need_kill) == need_kill)
      return 1;
  }
  return 0;
}
#endif

typedef struct
{
  u32 branch_target;
  u8 *branch_source;
} block_exit_type;

// Div (6) and DivArm (7)
#define is_div_swi(swinum) (((swinum) & 0xFE) == 0x06)

#ifdef SH4_ARCH
/* The SH4 backend is validated against cpu.cc's interpreter cycle model. That
 * interpreter does not charge the old threaded "approximation" cycles for ARM
 * or Thumb multiply instructions; keeping them here makes hot IWRAM math loops
 * over-debit cycles and drift across frame boundaries. */
#define ARM_THREAD_MUL_EXTRA_CYCLES    0
#define ARM_THREAD_MLA_EXTRA_CYCLES    0
#define ARM_THREAD_UMULL_EXTRA_CYCLES  0
#define ARM_THREAD_UMLAL_EXTRA_CYCLES  0
#define ARM_THREAD_SMULL_EXTRA_CYCLES  0
#define ARM_THREAD_SMLAL_EXTRA_CYCLES  0
#define ARM_THREAD_HLE_DIV_CYCLES      0
#define SH4_THUMB_MUL_EXTRA_CYCLES     0
#else
#define ARM_THREAD_MUL_EXTRA_CYCLES    2
#define ARM_THREAD_MLA_EXTRA_CYCLES    3
#define ARM_THREAD_UMULL_EXTRA_CYCLES  3
#define ARM_THREAD_UMLAL_EXTRA_CYCLES  3
#define ARM_THREAD_SMULL_EXTRA_CYCLES  2
#define ARM_THREAD_SMLAL_EXTRA_CYCLES  3
#define ARM_THREAD_HLE_DIV_CYCLES      64
#define SH4_THUMB_MUL_EXTRA_CYCLES     2
#endif

#define arm_decode_data_proc_reg(opcode)                                      \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_data_proc_imm(opcode)                                      \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 imm = opcode & 0xFF;                                                    \
  u32 imm_ror = ((opcode >> 8) & 0x0F) * 2                                    \

#define arm_decode_psr_reg(opcode)                                            \
  u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);               \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_psr_imm(opcode)                                            \
  u32 psr_pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);               \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 imm = opcode & 0xFF;                                                    \
  u32 imm_ror = ((opcode >> 8) & 0x0F) * 2                                    \

#define arm_decode_branchx(opcode)                                            \
  u32 rn = opcode & 0x0F                                                      \

#define arm_decode_multiply()                                                 \
  u32 rd = (opcode >> 16) & 0x0F;                                             \
  u32 rn = (opcode >> 12) & 0x0F;                                             \
  u32 rs = (opcode >> 8) & 0x0F;                                              \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_multiply_long()                                            \
  u32 rdhi = (opcode >> 16) & 0x0F;                                           \
  u32 rdlo = (opcode >> 12) & 0x0F;                                           \
  u32 rs = (opcode >> 8) & 0x0F;                                              \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_swap()                                                     \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_half_trans_r()                                             \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_half_trans_of()                                            \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 offset = ((opcode >> 4) & 0xF0) | (opcode & 0x0F)                       \

#define arm_decode_data_trans_imm()                                           \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 offset = opcode & 0x0FFF                                                \

#define arm_decode_data_trans_reg()                                           \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 rd = (opcode >> 12) & 0x0F;                                             \
  u32 rm = opcode & 0x0F                                                      \

#define arm_decode_block_trans()                                              \
  u32 rn = (opcode >> 16) & 0x0F;                                             \
  u32 reg_list = opcode & 0xFFFF                                              \

#define arm_decode_branch()                                                   \
  s32 offset = ((s32)(opcode & 0xFFFFFF) << 8) >> 6                           \

#define thumb_decode_shift()                                                  \
  u32 imm = (opcode >> 6) & 0x1F;                                             \
  u32 rs = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_add_sub()                                                \
  u32 rn = (opcode >> 6) & 0x07;                                              \
  u32 rs = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_add_sub_imm()                                            \
  u32 imm = (opcode >> 6) & 0x07;                                             \
  u32 rs = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_imm()                                                    \
  u32 imm = opcode & 0xFF                                                     \

#define thumb_decode_alu_op()                                                 \
  u32 rs = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_hireg_op()                                               \
  u32 rs = (opcode >> 3) & 0x0F;                                              \
  u32 rd = ((opcode >> 4) & 0x08) | (opcode & 0x07)                           \

#define thumb_decode_mem_reg()                                                \
  u32 ro = (opcode >> 6) & 0x07;                                              \
  u32 rb = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_mem_imm()                                                \
  u32 imm = (opcode >> 6) & 0x1F;                                             \
  u32 rb = (opcode >> 3) & 0x07;                                              \
  u32 rd = opcode & 0x07                                                      \

#define thumb_decode_add_sp()                                                 \
  u32 imm = opcode & 0x7F                                                     \

#define thumb_decode_rlist()                                                  \
  u32 reg_list = opcode & 0xFF                                                \

#define thumb_decode_branch_cond()                                            \
  s32 offset = (s8)(opcode & 0xFF)                                            \

#define thumb_decode_branch()                                                 \
  u32 offset = opcode & 0x07FF                                                \

/* Include the right emitter headers */
#if defined(MIPS_ARCH)
  #include "mips/mips_emit.h"
#elif defined(ARM_ARCH)
  #include "arm/arm_emit.h"
#elif defined(ARM64_ARCH)
  #include "arm/arm64_emit.h"
#elif defined(SH4_ARCH)
  #define SH4_INTERNAL_KNOWN_TARGET(tgt_pc)                                   \
    (((tgt_pc) <= (u32)pc)                                                    \
      ? block_data[((tgt_pc) - block_start_pc) >>                             \
                   (sh4_block_is_thumb ? 1 : 2)].body_offset                  \
      : NULL)
  #include "sh4/sh4_emit.h"

SH4G_NOINLINE void sh4g_arm_access_memory_emit(
  u8 **tp, u32 opcode, u32 pc, u32 condition, u32 *cycle_count_p,
  bool ram_region, u8 *pc_address_block)
{
  if (!ram_region && ((opcode & 0x0F7F0000u) == 0x051F0000u)) {
    u32 rd = (opcode >> 12) & 0xFu;
    if (rd != 15u) {
      u32 off = opcode & 0xFFFu;
      u32 addr = (pc + 8u) + ((opcode & 0x00800000u) ? off : (u32)-(s32)off);
      u32 reg_hi = addr >> 24;
      if ((addr & 3u) == 0u && reg_hi >= 0x08u && reg_hi <= 0x0Du &&
          (addr & 0x01FFFFFFu) >= 0x200u) {
        u8 *blk = ((addr >> 12) == (pc >> 12))
          ? pc_address_block : cgba_memory_map_read_4k(addr);
        if (blk) {
          u32 val = readaddress32(blk, addr & 0x0FFFu);
          int ws = (int)ws_cyc_nseq[reg_hi][1];
          if (condition == 0x0Eu) *cycle_count_p += (u32)ws;
          else if (ws) sh4g_cycle_debit(tp, ws);
          sh4g_const(tp, val, SH4_REG_T0);
          sh4g_store_greg(tp, SH4_REG_T0, rd);
          return;
        }
      }
    }
  }
  if (!sh4g_arm_ldst_native(tp, opcode, pc, (int)*cycle_count_p))
    sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                        (const void *)cgba_sh4_arm_ldst, opcode, pc, 1,
                        (int)*cycle_count_p);
}
#else
  #include "x86/x86_emit.h"
#endif

/* Backends that fold the whole Thumb BL into the suffix need no prefix code. */
#ifndef thumb_bl_prefix
#define thumb_bl_prefix() do {} while(0)
#endif

/* Cache invalidation */

#if defined(PSP)
  void platform_cache_sync(void *baseaddr, void *endptr) {
    sceKernelDcacheWritebackRange(baseaddr, ((char*)endptr) - ((char*)baseaddr));
    sceKernelIcacheInvalidateRange(baseaddr, ((char*)endptr) - ((char*)baseaddr));
  }
#elif defined(PS2)
  void platform_cache_sync(void *baseaddr, void *endptr) {
    FlushCache(0);   // Dcache flush
    FlushCache(2);   // Icache invalidate
  }
#elif defined(VITA)
  void platform_cache_sync(void *baseaddr, void *endptr) {
    sceKernelSyncVMDomain(sceBlock, baseaddr, ((char*)endptr) - ((char*)baseaddr) + 64);
  }
#elif defined(_3DS)
  #include "3ds/3ds_utils.h"
  void platform_cache_sync(void *baseaddr, void *endptr) {
    ctr_flush_invalidate_cache();
  }
#elif defined(ARM_ARCH) || defined(ARM64_ARCH)
  void platform_cache_sync(void *baseaddr, void *endptr) {
    __clear_cache(baseaddr, endptr);
  }
#elif defined(MIPS_ARCH)
  void platform_cache_sync(void *baseaddr, void *endptr) {
    __builtin___clear_cache(baseaddr, endptr);
  }
#elif defined(SH4_ARCH)
  /* SH7305 (SH-4A): make freshly emitted host code executable by writing back
     the operand cache then invalidating the instruction cache for the range.
     Per-32-byte-line OCBWB -> SYNCO -> ICBI -> SYNCO; see ports/.../sh4_cache.h.
     Without this the CPU may execute stale I-cache lines and fault on the first
     JMP into a translated block. */
  #include "ports/fxcg100/sh4/sh4_cache.h"
  void platform_cache_sync(void *baseaddr, void *endptr) {
    cgba_sh4_cache_sync(baseaddr, endptr);
  }
#else
  /* x86 CPUs have icache consistency checks */
  void platform_cache_sync(void *baseaddr, void *endptr) {}
#endif

void translate_icache_sync() {
    u32 synced = 0;
    // Cache emitted code can only grow
    if (last_rom_translation_ptr < rom_translation_ptr) {
        synced += (u32)(rom_translation_ptr - last_rom_translation_ptr);
        platform_cache_sync(last_rom_translation_ptr, rom_translation_ptr);
        last_rom_translation_ptr = rom_translation_ptr;
    }
    if (last_ram_translation_ptr < ram_translation_ptr) {
        synced += (u32)(ram_translation_ptr - last_ram_translation_ptr);
        platform_cache_sync(last_ram_translation_ptr, ram_translation_ptr);
        last_ram_translation_ptr = ram_translation_ptr;
    }
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    if (synced) {
        cgba_dynarec_icache_sync_count++;
        cgba_dynarec_icache_sync_bytes += synced;
    }
#endif
}

/* End of Cache invalidation */


#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
#define CGBA_SH4_FM_OOL_REWIND(ptr, ram) cgba_sh4_fm_ool_rewind((ptr), (ram))
#else
#define CGBA_SH4_FM_OOL_REWIND(ptr, ram) ((void)0)
#endif

#ifdef SH4_ARCH
#define check_pc_region(pc)                                                   \
  new_pc_region = (pc >> 12);                                                 \
  if(new_pc_region != pc_region)                                              \
  {                                                                           \
    pc_region = new_pc_region;                                                \
    pc_address_block = cgba_memory_map_read_4k(pc);                           \
    if(!pc_address_block)                                                     \
    {                                                                         \
      sh4_greg_forward_reset();                                               \
      sh4g_litseg_abort();                                                    \
      CGBA_SH4_FM_OOL_REWIND(attempt_base, ram_region);                       \
      return false;                                                           \
    }                                                                         \
  }
#else
#define check_pc_region(pc)                                                   \
  new_pc_region = (pc >> 12);                                                 \
  if(new_pc_region != pc_region)                                              \
  {                                                                           \
    pc_region = new_pc_region;                                                \
    pc_address_block = cgba_memory_map_read_4k(pc);                           \
    if(!pc_address_block)                                                     \
      return false;                                                           \
  }
#endif                                                                           \

#ifndef arm_instruction_prefix
#define arm_instruction_prefix() do {} while(0)
#endif
#ifndef arm_translate_override
#define arm_translate_override() 0
#endif

#define translate_arm_instruction()                                           \
  arm_load_flag_status()                                                      \
  check_pc_region(pc);                                                        \
  opcode = readaddress32(pc_address_block, (pc & 0x0FFF));                    \
  condition = block_data[block_data_position].condition;                      \
                                                                              \
  if((condition != last_condition) || (condition >= 0x20))                    \
  {                                                                           \
    if((last_condition & 0x0F) != 0x0E)                                       \
    {                                                                         \
      generate_branch_patch_conditional(backpatch_address, translation_ptr);  \
    }                                                                         \
                                                                              \
    last_condition = condition;                                               \
                                                                              \
    condition &= 0x0F;                                                        \
                                                                              \
    if(condition != 0x0E)                                                     \
    {                                                                         \
      arm_conditional_block_header();                                         \
    }                                                                         \
  }                                                                           \
  emit_trace_arm_instruction(pc);                                             \
                                                                              \
  /* A backend may fully handle (or elide) an instruction here; the condition \
   * guard above and the cycle accounting around this macro still apply. */   \
  if(arm_translate_override())                                                \
  {                                                                           \
  }                                                                           \
  else                                                                        \
  switch((opcode >> 20) & 0xFF)                                               \
  {                                                                           \
    case 0x00:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], -rm */                                            \
          arm_access_memory(store, down, post, u16, half_reg);                \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* MUL rd, rm, rs */                                                \
          arm_multiply(no, no);                                               \
          cycle_count += ARM_THREAD_MUL_EXTRA_CYCLES;                         \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* AND rd, rn, reg_op */                                              \
        arm_data_proc(and, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x01:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* MULS rd, rm, rs */                                             \
            arm_multiply(no, yes);                                            \
            cycle_count += ARM_THREAD_MUL_EXTRA_CYCLES;                       \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], -rm */                                          \
            arm_access_memory(load, down, post, u16, half_reg);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], -rm */                                         \
            arm_access_memory(load, down, post, s8, half_reg);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], -rm */                                         \
            arm_access_memory(load, down, post, s16, half_reg);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ANDS rd, rn, reg_op */                                             \
        arm_data_proc(ands, reg_flags, flags);                                \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x02:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], -rm */                                            \
          arm_access_memory(store, down, post, u16, half_reg);                \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* MLA rd, rm, rs, rn */                                            \
          arm_multiply(yes, no);                                              \
          cycle_count += ARM_THREAD_MLA_EXTRA_CYCLES;                         \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* EOR rd, rn, reg_op */                                              \
        arm_data_proc(eor, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x03:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* MLAS rd, rm, rs, rn */                                         \
            arm_multiply(yes, yes);                                           \
            cycle_count += ARM_THREAD_MLA_EXTRA_CYCLES;                       \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], -rm */                                          \
            arm_access_memory(load, down, post, u16, half_reg);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], -rm */                                         \
            arm_access_memory(load, down, post, s8, half_reg);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], -rm */                                         \
            arm_access_memory(load, down, post, s16, half_reg);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* EORS rd, rn, reg_op */                                             \
        arm_data_proc(eors, reg_flags, flags);                                \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x04:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn], -imm */                                             \
        arm_access_memory(store, down, post, u16, half_imm);                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* SUB rd, rn, reg_op */                                              \
        arm_data_proc(sub, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x05:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn], -imm */                                         \
            arm_access_memory(load, down, post, u16, half_imm);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], -imm */                                        \
            arm_access_memory(load, down, post, s8, half_imm);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], -imm */                                        \
            arm_access_memory(load, down, post, s16, half_imm);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* SUBS rd, rn, reg_op */                                             \
        arm_data_proc(subs, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x06:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn], -imm */                                             \
        arm_access_memory(store, down, post, u16, half_imm);                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* RSB rd, rn, reg_op */                                              \
        arm_data_proc(rsb, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x07:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn], -imm */                                         \
            arm_access_memory(load, down, post, u16, half_imm);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], -imm */                                        \
            arm_access_memory(load, down, post, s8, half_imm);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], -imm */                                        \
            arm_access_memory(load, down, post, s16, half_imm);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* RSBS rd, rn, reg_op */                                             \
        arm_data_proc(rsbs, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x08:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], +rm */                                            \
          arm_access_memory(store, up, post, u16, half_reg);                  \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* UMULL rd, rm, rs */                                              \
          arm_multiply_long(u64, no, no);                                     \
          cycle_count += ARM_THREAD_UMULL_EXTRA_CYCLES;                       \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ADD rd, rn, reg_op */                                              \
        arm_data_proc(add, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x09:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* UMULLS rdlo, rdhi, rm, rs */                                   \
            arm_multiply_long(u64, no, yes);                                  \
            cycle_count += ARM_THREAD_UMULL_EXTRA_CYCLES;                     \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], +rm */                                          \
            arm_access_memory(load, up, post, u16, half_reg);                 \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], +rm */                                         \
            arm_access_memory(load, up, post, s8, half_reg);                  \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], +rm */                                         \
            arm_access_memory(load, up, post, s16, half_reg);                 \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ADDS rd, rn, reg_op */                                             \
        arm_data_proc(adds, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0A:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], +rm */                                            \
          arm_access_memory(store, up, post, u16, half_reg);                  \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* UMLAL rd, rm, rs */                                              \
          arm_multiply_long(u64_add, yes, no);                                \
          cycle_count += ARM_THREAD_UMLAL_EXTRA_CYCLES;                       \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ADC rd, rn, reg_op */                                              \
        arm_data_proc(adc, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0B:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* UMLALS rdlo, rdhi, rm, rs */                                   \
            arm_multiply_long(u64_add, yes, yes);                             \
            cycle_count += ARM_THREAD_UMLAL_EXTRA_CYCLES;                     \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], +rm */                                          \
            arm_access_memory(load, up, post, u16, half_reg);                 \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], +rm */                                         \
            arm_access_memory(load, up, post, s8, half_reg);                  \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], +rm */                                         \
            arm_access_memory(load, up, post, s16, half_reg);                 \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ADCS rd, rn, reg_op */                                             \
        arm_data_proc(adcs, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0C:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], +imm */                                           \
          arm_access_memory(store, up, post, u16, half_imm);                  \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* SMULL rd, rm, rs */                                              \
          arm_multiply_long(s64, no, no);                                     \
          cycle_count += ARM_THREAD_SMULL_EXTRA_CYCLES;                       \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* SBC rd, rn, reg_op */                                              \
        arm_data_proc(sbc, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0D:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* SMULLS rdlo, rdhi, rm, rs */                                   \
            arm_multiply_long(s64, no, yes);                                  \
            cycle_count += ARM_THREAD_SMULL_EXTRA_CYCLES;                     \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], +imm */                                         \
            arm_access_memory(load, up, post, u16, half_imm);                 \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], +imm */                                        \
            arm_access_memory(load, up, post, s8, half_imm);                  \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], +imm */                                        \
            arm_access_memory(load, up, post, s16, half_imm);                 \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* SBCS rd, rn, reg_op */                                             \
        arm_data_proc(sbcs, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0E:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn], +imm */                                           \
          arm_access_memory(store, up, post, u16, half_imm);                  \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* SMLAL rd, rm, rs */                                              \
          arm_multiply_long(s64_add, yes, no);                                \
          cycle_count += ARM_THREAD_SMLAL_EXTRA_CYCLES;                       \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* RSC rd, rn, reg_op */                                              \
        arm_data_proc(rsc, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x0F:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 0:                                                             \
            /* SMLALS rdlo, rdhi, rm, rs */                                   \
            arm_multiply_long(s64_add, yes, yes);                             \
            cycle_count += ARM_THREAD_SMLAL_EXTRA_CYCLES;                     \
            break;                                                            \
                                                                              \
          case 1:                                                             \
            /* LDRH rd, [rn], +imm */                                         \
            arm_access_memory(load, up, post, u16, half_imm);                 \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn], +imm */                                        \
            arm_access_memory(load, up, post, s8, half_imm);                  \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn], +imm */                                        \
            arm_access_memory(load, up, post, s16, half_imm);                 \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* RSCS rd, rn, reg_op */                                             \
        arm_data_proc(rscs, reg, flags);                                      \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x10:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn - rm] */                                            \
          arm_access_memory(store, down, pre, u16, half_reg);                 \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* SWP rd, rm, [rn] */                                              \
          arm_swap(u32);                                                      \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MRS rd, cpsr */                                                    \
        arm_psr(reg, read, cpsr);                                             \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x11:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn - rm] */                                          \
            arm_access_memory(load, down, pre, u16, half_reg);                \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn - rm] */                                         \
            arm_access_memory(load, down, pre, s8, half_reg);                 \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn - rm] */                                         \
            arm_access_memory(load, down, pre, s16, half_reg);                \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* TST rd, rn, reg_op */                                              \
        arm_data_proc_test(tst, reg_flags);                                   \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x12:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn - rm]! */                                             \
        arm_access_memory(store, down, pre_wb, u16, half_reg);                \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        if(opcode & 0x10)                                                     \
        {                                                                     \
          /* BX rn */                                                         \
          arm_bx();                                                           \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* MSR cpsr, rm */                                                  \
          arm_psr(reg, store, cpsr);                                          \
        }                                                                     \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x13:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn - rm]! */                                         \
            arm_access_memory(load, down, pre_wb, u16, half_reg);             \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn - rm]! */                                        \
            arm_access_memory(load, down, pre_wb, s8, half_reg);              \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn - rm]! */                                        \
            arm_access_memory(load, down, pre_wb, s16, half_reg);             \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* TEQ rd, rn, reg_op */                                              \
        arm_data_proc_test(teq, reg_flags);                                   \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x14:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        if(opcode & 0x20)                                                     \
        {                                                                     \
          /* STRH rd, [rn - imm] */                                           \
          arm_access_memory(store, down, pre, u16, half_imm);                 \
        }                                                                     \
        else                                                                  \
        {                                                                     \
          /* SWPB rd, rm, [rn] */                                             \
          arm_swap(u8);                                                       \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MRS rd, spsr */                                                    \
        arm_psr(reg, read, spsr);                                             \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x15:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn - imm] */                                         \
            arm_access_memory(load, down, pre, u16, half_imm);                \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn - imm] */                                        \
            arm_access_memory(load, down, pre, s8, half_imm);                 \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn - imm] */                                        \
            arm_access_memory(load, down, pre, s16, half_imm);                \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* CMP rn, reg_op */                                                  \
        arm_data_proc_test(cmp, reg);                                         \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x16:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn - imm]! */                                            \
        arm_access_memory(store, down, pre_wb, u16, half_imm);                \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MSR spsr, rm */                                                    \
        arm_psr(reg, store, spsr);                                            \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x17:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn - imm]! */                                        \
            arm_access_memory(load, down, pre_wb, u16, half_imm);             \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn - imm]! */                                       \
            arm_access_memory(load, down, pre_wb, s8, half_imm);              \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn - imm]! */                                       \
            arm_access_memory(load, down, pre_wb, s16, half_imm);             \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* CMN rd, rn, reg_op */                                              \
        arm_data_proc_test(cmn, reg);                                         \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x18:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn + rm] */                                              \
        arm_access_memory(store, up, pre, u16, half_reg);                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ORR rd, rn, reg_op */                                              \
        arm_data_proc(orr, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x19:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn + rm] */                                          \
            arm_access_memory(load, up, pre, u16, half_reg);                  \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn + rm] */                                         \
            arm_access_memory(load, up, pre, s8, half_reg);                   \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn + rm] */                                         \
            arm_access_memory(load, up, pre, s16, half_reg);                  \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ORRS rd, rn, reg_op */                                             \
        arm_data_proc(orrs, reg_flags, flags);                                \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1A:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn + rm]! */                                             \
        arm_access_memory(store, up, pre_wb, u16, half_reg);                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MOV rd, reg_op */                                                  \
        arm_data_proc_unary(mov, reg, no_flags);                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1B:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn + rm]! */                                         \
            arm_access_memory(load, up, pre_wb, u16, half_reg);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn + rm]! */                                        \
            arm_access_memory(load, up, pre_wb, s8, half_reg);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn + rm]! */                                        \
            arm_access_memory(load, up, pre_wb, s16, half_reg);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MOVS rd, reg_op */                                                 \
        arm_data_proc_unary(movs, reg_flags, flags);                          \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1C:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn + imm] */                                             \
        arm_access_memory(store, up, pre, u16, half_imm);                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* BIC rd, rn, reg_op */                                              \
        arm_data_proc(bic, reg, no_flags);                                    \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1D:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn + imm] */                                         \
            arm_access_memory(load, up, pre, u16, half_imm);                  \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn + imm] */                                        \
            arm_access_memory(load, up, pre, s8, half_imm);                   \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn + imm] */                                        \
            arm_access_memory(load, up, pre, s16, half_imm);                  \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* BICS rd, rn, reg_op */                                             \
        arm_data_proc(bics, reg_flags, flags);                                \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1E:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        /* STRH rd, [rn + imm]! */                                            \
        arm_access_memory(store, up, pre_wb, u16, half_imm);                  \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MVN rd, reg_op */                                                  \
        arm_data_proc_unary(mvn, reg, no_flags);                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x1F:                                                                \
      if((opcode & 0x90) == 0x90)                                             \
      {                                                                       \
        switch((opcode >> 5) & 0x03)                                          \
        {                                                                     \
          case 1:                                                             \
            /* LDRH rd, [rn + imm]! */                                        \
            arm_access_memory(load, up, pre_wb, u16, half_imm);               \
            break;                                                            \
                                                                              \
          case 2:                                                             \
            /* LDRSB rd, [rn + imm]! */                                       \
            arm_access_memory(load, up, pre_wb, s8, half_imm);                \
            break;                                                            \
                                                                              \
          case 3:                                                             \
            /* LDRSH rd, [rn + imm]! */                                       \
            arm_access_memory(load, up, pre_wb, s16, half_imm);               \
            break;                                                            \
        }                                                                     \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* MVNS rd, rn, reg_op */                                             \
        arm_data_proc_unary(mvns, reg_flags, flags);                          \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x20:                                                                \
      /* AND rd, rn, imm */                                                   \
      arm_data_proc(and, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x21:                                                                \
      /* ANDS rd, rn, imm */                                                  \
      arm_data_proc(ands, imm_flags, flags);                                  \
      break;                                                                  \
                                                                              \
    case 0x22:                                                                \
      /* EOR rd, rn, imm */                                                   \
      arm_data_proc(eor, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x23:                                                                \
      /* EORS rd, rn, imm */                                                  \
      arm_data_proc(eors, imm_flags, flags);                                  \
      break;                                                                  \
                                                                              \
    case 0x24:                                                                \
      /* SUB rd, rn, imm */                                                   \
      arm_data_proc(sub, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x25:                                                                \
      /* SUBS rd, rn, imm */                                                  \
      arm_data_proc(subs, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x26:                                                                \
      /* RSB rd, rn, imm */                                                   \
      arm_data_proc(rsb, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x27:                                                                \
      /* RSBS rd, rn, imm */                                                  \
      arm_data_proc(rsbs, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x28:                                                                \
      /* ADD rd, rn, imm */                                                   \
      arm_data_proc(add, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x29:                                                                \
      /* ADDS rd, rn, imm */                                                  \
      arm_data_proc(adds, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x2A:                                                                \
      /* ADC rd, rn, imm */                                                   \
      arm_data_proc(adc, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x2B:                                                                \
      /* ADCS rd, rn, imm */                                                  \
      arm_data_proc(adcs, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x2C:                                                                \
      /* SBC rd, rn, imm */                                                   \
      arm_data_proc(sbc, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x2D:                                                                \
      /* SBCS rd, rn, imm */                                                  \
      arm_data_proc(sbcs, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x2E:                                                                \
      /* RSC rd, rn, imm */                                                   \
      arm_data_proc(rsc, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x2F:                                                                \
      /* RSCS rd, rn, imm */                                                  \
      arm_data_proc(rscs, imm, flags);                                        \
      break;                                                                  \
                                                                              \
    case 0x30 ... 0x31:                                                       \
      /* TST rn, imm */                                                       \
      arm_data_proc_test(tst, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x32:                                                                \
      /* MSR cpsr, imm */                                                     \
      arm_psr(imm, store, cpsr);                                              \
      break;                                                                  \
                                                                              \
    case 0x33:                                                                \
      /* TEQ rn, imm */                                                       \
      arm_data_proc_test(teq, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x34 ... 0x35:                                                       \
      /* CMP rn, imm */                                                       \
      arm_data_proc_test(cmp, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x36:                                                                \
      /* MSR spsr, imm */                                                     \
      arm_psr(imm, store, spsr);                                              \
      break;                                                                  \
                                                                              \
    case 0x37:                                                                \
      /* CMN rn, imm */                                                       \
      arm_data_proc_test(cmn, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x38:                                                                \
      /* ORR rd, rn, imm */                                                   \
      arm_data_proc(orr, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x39:                                                                \
      /* ORRS rd, rn, imm */                                                  \
      arm_data_proc(orrs, imm_flags, flags);                                  \
      break;                                                                  \
                                                                              \
    case 0x3A:                                                                \
      /* MOV rd, imm */                                                       \
      arm_data_proc_unary(mov, imm, no_flags);                                \
      break;                                                                  \
                                                                              \
    case 0x3B:                                                                \
      /* MOVS rd, imm */                                                      \
      arm_data_proc_unary(movs, imm_flags, flags);                            \
      break;                                                                  \
                                                                              \
    case 0x3C:                                                                \
      /* BIC rd, rn, imm */                                                   \
      arm_data_proc(bic, imm, no_flags);                                      \
      break;                                                                  \
                                                                              \
    case 0x3D:                                                                \
      /* BICS rd, rn, imm */                                                  \
      arm_data_proc(bics, imm_flags, flags);                                  \
      break;                                                                  \
                                                                              \
    case 0x3E:                                                                \
      /* MVN rd, imm */                                                       \
      arm_data_proc_unary(mvn, imm, no_flags);                                \
      break;                                                                  \
                                                                              \
    case 0x3F:                                                                \
      /* MVNS rd, imm */                                                      \
      arm_data_proc_unary(mvns, imm_flags, flags);                            \
      break;                                                                  \
                                                                              \
    case 0x40:                                                                \
      /* STR rd, [rn], -imm */                                                \
      arm_access_memory(store, down, post, u32, imm);                         \
      break;                                                                  \
                                                                              \
    case 0x41:                                                                \
      /* LDR rd, [rn], -imm */                                                \
      arm_access_memory(load, down, post, u32, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x42:                                                                \
      /* STRT rd, [rn], -imm */                                               \
      arm_access_memory(store, down, post, u32, imm);                         \
      break;                                                                  \
                                                                              \
    case 0x43:                                                                \
      /* LDRT rd, [rn], -imm */                                               \
      arm_access_memory(load, down, post, u32, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x44:                                                                \
      /* STRB rd, [rn], -imm */                                               \
      arm_access_memory(store, down, post, u8, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x45:                                                                \
      /* LDRB rd, [rn], -imm */                                               \
      arm_access_memory(load, down, post, u8, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x46:                                                                \
      /* STRBT rd, [rn], -imm */                                              \
      arm_access_memory(store, down, post, u8, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x47:                                                                \
      /* LDRBT rd, [rn], -imm */                                              \
      arm_access_memory(load, down, post, u8, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x48:                                                                \
      /* STR rd, [rn], +imm */                                                \
      arm_access_memory(store, up, post, u32, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x49:                                                                \
      /* LDR rd, [rn], +imm */                                                \
      arm_access_memory(load, up, post, u32, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x4A:                                                                \
      /* STRT rd, [rn], +imm */                                               \
      arm_access_memory(store, up, post, u32, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x4B:                                                                \
      /* LDRT rd, [rn], +imm */                                               \
      arm_access_memory(load, up, post, u32, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x4C:                                                                \
      /* STRB rd, [rn], +imm */                                               \
      arm_access_memory(store, up, post, u8, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x4D:                                                                \
      /* LDRB rd, [rn], +imm */                                               \
      arm_access_memory(load, up, post, u8, imm);                             \
      break;                                                                  \
                                                                              \
    case 0x4E:                                                                \
      /* STRBT rd, [rn], +imm */                                              \
      arm_access_memory(store, up, post, u8, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x4F:                                                                \
      /* LDRBT rd, [rn], +imm */                                              \
      arm_access_memory(load, up, post, u8, imm);                             \
      break;                                                                  \
                                                                              \
    case 0x50:                                                                \
      /* STR rd, [rn - imm] */                                                \
      arm_access_memory(store, down, pre, u32, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x51:                                                                \
      /* LDR rd, [rn - imm] */                                                \
      arm_access_memory(load, down, pre, u32, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x52:                                                                \
      /* STR rd, [rn - imm]! */                                               \
      arm_access_memory(store, down, pre_wb, u32, imm);                       \
      break;                                                                  \
                                                                              \
    case 0x53:                                                                \
      /* LDR rd, [rn - imm]! */                                               \
      arm_access_memory(load, down, pre_wb, u32, imm);                        \
      break;                                                                  \
                                                                              \
    case 0x54:                                                                \
      /* STRB rd, [rn - imm] */                                               \
      arm_access_memory(store, down, pre, u8, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x55:                                                                \
      /* LDRB rd, [rn - imm] */                                               \
      arm_access_memory(load, down, pre, u8, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x56:                                                                \
      /* STRB rd, [rn - imm]! */                                              \
      arm_access_memory(store, down, pre_wb, u8, imm);                        \
      break;                                                                  \
                                                                              \
    case 0x57:                                                                \
      /* LDRB rd, [rn - imm]! */                                              \
      arm_access_memory(load, down, pre_wb, u8, imm);                         \
      break;                                                                  \
                                                                              \
    case 0x58:                                                                \
      /* STR rd, [rn + imm] */                                                \
      arm_access_memory(store, up, pre, u32, imm);                            \
      break;                                                                  \
                                                                              \
    case 0x59:                                                                \
      /* LDR rd, [rn + imm] */                                                \
      arm_access_memory(load, up, pre, u32, imm);                             \
      break;                                                                  \
                                                                              \
    case 0x5A:                                                                \
      /* STR rd, [rn + imm]! */                                               \
      arm_access_memory(store, up, pre_wb, u32, imm);                         \
      break;                                                                  \
                                                                              \
    case 0x5B:                                                                \
      /* LDR rd, [rn + imm]! */                                               \
      arm_access_memory(load, up, pre_wb, u32, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x5C:                                                                \
      /* STRB rd, [rn + imm] */                                               \
      arm_access_memory(store, up, pre, u8, imm);                             \
      break;                                                                  \
                                                                              \
    case 0x5D:                                                                \
      /* LDRB rd, [rn + imm] */                                               \
      arm_access_memory(load, up, pre, u8, imm);                              \
      break;                                                                  \
                                                                              \
    case 0x5E:                                                                \
      /* STRB rd, [rn + imm]! */                                              \
      arm_access_memory(store, up, pre_wb, u8, imm);                          \
      break;                                                                  \
                                                                              \
    case 0x5F:                                                                \
      /* LDRBT rd, [rn + imm]! */                                             \
      arm_access_memory(load, up, pre_wb, u8, imm);                           \
      break;                                                                  \
                                                                              \
    case 0x60:                                                                \
      /* STR rd, [rn], -rm */                                                 \
      arm_access_memory(store, down, post, u32, reg);                         \
      break;                                                                  \
                                                                              \
    case 0x61:                                                                \
      /* LDR rd, [rn], -rm */                                                 \
      arm_access_memory(load, down, post, u32, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x62:                                                                \
      /* STRT rd, [rn], -rm */                                                \
      arm_access_memory(store, down, post, u32, reg);                         \
      break;                                                                  \
                                                                              \
    case 0x63:                                                                \
      /* LDRT rd, [rn], -rm */                                                \
      arm_access_memory(load, down, post, u32, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x64:                                                                \
      /* STRB rd, [rn], -rm */                                                \
      arm_access_memory(store, down, post, u8, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x65:                                                                \
      /* LDRB rd, [rn], -rm */                                                \
      arm_access_memory(load, down, post, u8, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x66:                                                                \
      /* STRBT rd, [rn], -rm */                                               \
      arm_access_memory(store, down, post, u8, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x67:                                                                \
      /* LDRBT rd, [rn], -rm */                                               \
      arm_access_memory(load, down, post, u8, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x68:                                                                \
      /* STR rd, [rn], +rm */                                                 \
      arm_access_memory(store, up, post, u32, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x69:                                                                \
      /* LDR rd, [rn], +rm */                                                 \
      arm_access_memory(load, up, post, u32, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x6A:                                                                \
      /* STRT rd, [rn], +rm */                                                \
      arm_access_memory(store, up, post, u32, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x6B:                                                                \
      /* LDRT rd, [rn], +rm */                                                \
      arm_access_memory(load, up, post, u32, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x6C:                                                                \
      /* STRB rd, [rn], +rm */                                                \
      arm_access_memory(store, up, post, u8, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x6D:                                                                \
      /* LDRB rd, [rn], +rm */                                                \
      arm_access_memory(load, up, post, u8, reg);                             \
      break;                                                                  \
                                                                              \
    case 0x6E:                                                                \
      /* STRBT rd, [rn], +rm */                                               \
      arm_access_memory(store, up, post, u8, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x6F:                                                                \
      /* LDRBT rd, [rn], +rm */                                               \
      arm_access_memory(load, up, post, u8, reg);                             \
      break;                                                                  \
                                                                              \
    case 0x70:                                                                \
      /* STR rd, [rn - rm] */                                                 \
      arm_access_memory(store, down, pre, u32, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x71:                                                                \
      /* LDR rd, [rn - rm] */                                                 \
      arm_access_memory(load, down, pre, u32, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x72:                                                                \
      /* STR rd, [rn - rm]! */                                                \
      arm_access_memory(store, down, pre_wb, u32, reg);                       \
      break;                                                                  \
                                                                              \
    case 0x73:                                                                \
      /* LDR rd, [rn - rm]! */                                                \
      arm_access_memory(load, down, pre_wb, u32, reg);                        \
      break;                                                                  \
                                                                              \
    case 0x74:                                                                \
      /* STRB rd, [rn - rm] */                                                \
      arm_access_memory(store, down, pre, u8, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x75:                                                                \
      /* LDRB rd, [rn - rm] */                                                \
      arm_access_memory(load, down, pre, u8, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x76:                                                                \
      /* STRB rd, [rn - rm]! */                                               \
      arm_access_memory(store, down, pre_wb, u8, reg);                        \
      break;                                                                  \
                                                                              \
    case 0x77:                                                                \
      /* LDRB rd, [rn - rm]! */                                               \
      arm_access_memory(load, down, pre_wb, u8, reg);                         \
      break;                                                                  \
                                                                              \
    case 0x78:                                                                \
      /* STR rd, [rn + rm] */                                                 \
      arm_access_memory(store, up, pre, u32, reg);                            \
      break;                                                                  \
                                                                              \
    case 0x79:                                                                \
      /* LDR rd, [rn + rm] */                                                 \
      arm_access_memory(load, up, pre, u32, reg);                             \
      break;                                                                  \
                                                                              \
    case 0x7A:                                                                \
      /* STR rd, [rn + rm]! */                                                \
      arm_access_memory(store, up, pre_wb, u32, reg);                         \
      break;                                                                  \
                                                                              \
    case 0x7B:                                                                \
      /* LDR rd, [rn + rm]! */                                                \
      arm_access_memory(load, up, pre_wb, u32, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x7C:                                                                \
      /* STRB rd, [rn + rm] */                                                \
      arm_access_memory(store, up, pre, u8, reg);                             \
      break;                                                                  \
                                                                              \
    case 0x7D:                                                                \
      /* LDRB rd, [rn + rm] */                                                \
      arm_access_memory(load, up, pre, u8, reg);                              \
      break;                                                                  \
                                                                              \
    case 0x7E:                                                                \
      /* STRB rd, [rn + rm]! */                                               \
      arm_access_memory(store, up, pre_wb, u8, reg);                          \
      break;                                                                  \
                                                                              \
    case 0x7F:                                                                \
      /* LDRBT rd, [rn + rm]! */                                              \
      arm_access_memory(load, up, pre_wb, u8, reg);                           \
      break;                                                                  \
                                                                              \
    case 0x80:                                                                \
      /* STMDA rn, rlist */                                                   \
      arm_block_memory(store, down_a, no, no);                                \
      break;                                                                  \
                                                                              \
    case 0x81:                                                                \
      /* LDMDA rn, rlist */                                                   \
      arm_block_memory(load, down_a, no, no);                                 \
      break;                                                                  \
                                                                              \
    case 0x82:                                                                \
      /* STMDA rn!, rlist */                                                  \
      arm_block_memory(store, down_a, down, no);                              \
      break;                                                                  \
                                                                              \
    case 0x83:                                                                \
      /* LDMDA rn!, rlist */                                                  \
      arm_block_memory(load, down_a, down, no);                               \
      break;                                                                  \
                                                                              \
    case 0x84:                                                                \
      /* STMDA rn, rlist^ */                                                  \
      arm_block_memory(store, down_a, no, yes);                               \
      break;                                                                  \
                                                                              \
    case 0x85:                                                                \
      /* LDMDA rn, rlist^ */                                                  \
      arm_block_memory(load, down_a, no, yes);                                \
      break;                                                                  \
                                                                              \
    case 0x86:                                                                \
      /* STMDA rn!, rlist^ */                                                 \
      arm_block_memory(store, down_a, down, yes);                             \
      break;                                                                  \
                                                                              \
    case 0x87:                                                                \
      /* LDMDA rn!, rlist^ */                                                 \
      arm_block_memory(load, down_a, down, yes);                              \
      break;                                                                  \
                                                                              \
    case 0x88:                                                                \
      /* STMIA rn, rlist */                                                   \
      arm_block_memory(store, no, no, no);                                    \
      break;                                                                  \
                                                                              \
    case 0x89:                                                                \
      /* LDMIA rn, rlist */                                                   \
      arm_block_memory(load, no, no, no);                                     \
      break;                                                                  \
                                                                              \
    case 0x8A:                                                                \
      /* STMIA rn!, rlist */                                                  \
      arm_block_memory(store, no, up, no);                                    \
      break;                                                                  \
                                                                              \
    case 0x8B:                                                                \
      /* LDMIA rn!, rlist */                                                  \
      arm_block_memory(load, no, up, no);                                     \
      break;                                                                  \
                                                                              \
    case 0x8C:                                                                \
      /* STMIA rn, rlist^ */                                                  \
      arm_block_memory(store, no, no, yes);                                   \
      break;                                                                  \
                                                                              \
    case 0x8D:                                                                \
      /* LDMIA rn, rlist^ */                                                  \
      arm_block_memory(load, no, no, yes);                                    \
      break;                                                                  \
                                                                              \
    case 0x8E:                                                                \
      /* STMIA rn!, rlist^ */                                                 \
      arm_block_memory(store, no, up, yes);                                   \
      break;                                                                  \
                                                                              \
    case 0x8F:                                                                \
      /* LDMIA rn!, rlist^ */                                                 \
      arm_block_memory(load, no, up, yes);                                    \
      break;                                                                  \
                                                                              \
    case 0x90:                                                                \
      /* STMDB rn, rlist */                                                   \
      arm_block_memory(store, down_b, no, no);                                \
      break;                                                                  \
                                                                              \
    case 0x91:                                                                \
      /* LDMDB rn, rlist */                                                   \
      arm_block_memory(load, down_b, no, no);                                 \
      break;                                                                  \
                                                                              \
    case 0x92:                                                                \
      /* STMDB rn!, rlist */                                                  \
      arm_block_memory(store, down_b, down, no);                              \
      break;                                                                  \
                                                                              \
    case 0x93:                                                                \
      /* LDMDB rn!, rlist */                                                  \
      arm_block_memory(load, down_b, down, no);                               \
      break;                                                                  \
                                                                              \
    case 0x94:                                                                \
      /* STMDB rn, rlist^ */                                                  \
      arm_block_memory(store, down_b, no, yes);                               \
      break;                                                                  \
                                                                              \
    case 0x95:                                                                \
      /* LDMDB rn, rlist^ */                                                  \
      arm_block_memory(load, down_b, no, yes);                                \
      break;                                                                  \
                                                                              \
    case 0x96:                                                                \
      /* STMDB rn!, rlist^ */                                                 \
      arm_block_memory(store, down_b, down, yes);                             \
      break;                                                                  \
                                                                              \
    case 0x97:                                                                \
      /* LDMDB rn!, rlist^ */                                                 \
      arm_block_memory(load, down_b, down, yes);                              \
      break;                                                                  \
                                                                              \
    case 0x98:                                                                \
      /* STMIB rn, rlist */                                                   \
      arm_block_memory(store, up, no, no);                                    \
      break;                                                                  \
                                                                              \
    case 0x99:                                                                \
      /* LDMIB rn, rlist */                                                   \
      arm_block_memory(load, up, no, no);                                     \
      break;                                                                  \
                                                                              \
    case 0x9A:                                                                \
      /* STMIB rn!, rlist */                                                  \
      arm_block_memory(store, up, up, no);                                    \
      break;                                                                  \
                                                                              \
    case 0x9B:                                                                \
      /* LDMIB rn!, rlist */                                                  \
      arm_block_memory(load, up, up, no);                                     \
      break;                                                                  \
                                                                              \
    case 0x9C:                                                                \
      /* STMIB rn, rlist^ */                                                  \
      arm_block_memory(store, up, no, yes);                                   \
      break;                                                                  \
                                                                              \
    case 0x9D:                                                                \
      /* LDMIB rn, rlist^ */                                                  \
      arm_block_memory(load, up, no, yes);                                    \
      break;                                                                  \
                                                                              \
    case 0x9E:                                                                \
      /* STMIB rn!, rlist^ */                                                 \
      arm_block_memory(store, up, up, yes);                                   \
      break;                                                                  \
                                                                              \
    case 0x9F:                                                                \
      /* LDMIB rn!, rlist^ */                                                 \
      arm_block_memory(load, up, up, yes);                                    \
      break;                                                                  \
                                                                              \
    case 0xA0 ... 0xAF:                                                       \
    {                                                                         \
      /* B offset */                                                          \
      arm_b();                                                                \
      break;                                                                  \
    }                                                                         \
                                                                              \
    case 0xB0 ... 0xBF:                                                       \
    {                                                                         \
      /* BL offset */                                                         \
      arm_bl();                                                               \
      break;                                                                  \
    }                                                                         \
                                                                              \
    case 0xF0 ... 0xFF:                                                       \
    {                                                                         \
      u32 swinum = (opcode >> 16) & 0xFF;                                     \
      if (swinum == 6) {                                                      \
        cycle_count += ARM_THREAD_HLE_DIV_CYCLES;                             \
        arm_hle_div(arm);                                                     \
      }                                                                       \
      else if (swinum == 7) {                                                 \
        cycle_count += ARM_THREAD_HLE_DIV_CYCLES;                             \
        arm_hle_div_arm(arm);                                                 \
      }                                                                       \
      else {                                                                  \
        arm_swi();                                                            \
      }                                                                       \
      break;                                                                  \
    }                                                                         \
  }                                                                           \
                                                                              \
  pc += 4                                                                     \

#if defined(SH4_ARCH) && !defined(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)

/* ARM per-instruction flag classification for the dead-flag pass — the ARM
 * analogue of thumb_flag_status(), same block_data[].flag_data encoding:
 *   bits 0-3  = flags the instruction MAY modify (rewritten by
 *               arm_dead_flag_eliminate to the set it SHOULD generate),
 *   bits 4-7  = flags it MUST modify,
 *   bits 8-11 = flags it REQUIRES (condition source flags, C carry-in, or
 *               all-live for anything that leaves the block with the CPSR
 *               architecturally observable: branches, PC writes, SWI, MSR/MRS).
 * Conditional instructions contribute their condition's source flags to
 * requires and demote must to may (a failed condition leaves flags untouched).
 * Stores pass liveness through (flag_data 0) exactly like the upstream Thumb
 * scan: a mid-block store-alert exit may observe a skipped dead flag, the same
 * documented trade the x86/ARM backends ship. Investigated 2026-07-05: the
 * skipped flags are regenerated before any in-block use and block ends are
 * all-live, so the guest can only observe them if an ISR inspects SPSR NZCV
 * mid-block — accepted (making every store a liveness barrier measured -10%
 * on the Metroid movement soak via block growth -> cache pressure). The
 * single-block lockstep can flag this as a false CPSR mismatch at a store
 * alert exit; verify against a memory/window diff before chasing it. */
#define arm_flag_status()                                                     \
{                                                                             \
  static const u16 arm_cond_requires[16] = {                                  \
    0x400, 0x400,   /* EQ/NE: Z */                                            \
    0x200, 0x200,   /* CS/CC: C */                                            \
    0x800, 0x800,   /* MI/PL: N */                                            \
    0x100, 0x100,   /* VS/VC: V */                                            \
    0x600, 0x600,   /* HI/LS: C,Z */                                          \
    0x900, 0x900,   /* GE/LT: N,V */                                          \
    0xD00, 0xD00,   /* GT/LE: N,Z,V */                                        \
    0x000, 0xF00    /* AL: none; NV/extension space: conservative */          \
  };                                                                          \
  /* arm_load_opcode has already stripped the condition nibble from `opcode`
   * (opcode &= 0xFFFFFFF) and stashed it in `condition` — read it from there,
   * NOT from opcode>>28 (which would classify everything as EQ-conditional
   * and silently neuter the whole pass). */                                  \
  u32 fcond = condition & 0x0F;                                               \
  u32 fclass = (opcode >> 25) & 7;                                            \
  u16 fstat = arm_cond_requires[fcond];                                       \
  if(fclass <= 1)                                                             \
  {                                                                           \
    if((opcode & 0x0FFFFFF0) == 0x012FFF10)                                   \
      fstat |= 0xF00;                           /* BX: leaves the block */    \
    else if((opcode & 0x0FC000F0) == 0x00000090 ||                            \
            (opcode & 0x0F8000F0) == 0x00800090)                              \
    {                                                                         \
      if(opcode & 0x00100000)                                                 \
        fstat |= (fcond != 0x0E) ? 0x0C : 0xCC; /* MULS(L): N,Z */            \
    }                                                                         \
    else if((opcode & 0x0FB00FF0) == 0x01000090)                              \
    { /* SWP: memory op, no flag effects (alert exits pass through) */ }      \
    else if(fclass == 0 && (opcode & 0x90) == 0x90)                           \
    {                                                                         \
      if((opcode & 0x00100000) && (((opcode >> 12) & 0xF) == 15))             \
        fstat |= 0xF00;                         /* LDRH/LDRSB pc */           \
    }                                                                         \
    else if((opcode & 0x0D900000) == 0x01000000)                              \
      fstat |= 0xF0F;                           /* MRS reads all as data;     \
                                                   MSR may write all + vector */ \
    else                                                                      \
    {                                                                         \
      u32 fop = (opcode >> 21) & 0xF;                                         \
      if(((opcode >> 12) & 0xF) == 15)                                        \
        fstat |= 0xF00;                         /* Rd==PC (incl. SUBS pc) */  \
      if(fop >= 0x5 && fop <= 0x7)                                            \
        fstat |= 0x200;                         /* ADC/SBC/RSC carry-in */    \
      if(!(opcode & 0x02000000) && ((opcode >> 5) & 3) == 3)                  \
        fstat |= 0x200;                         /* ROR/RRX operand2 */        \
      if(opcode & 0x00100000)                                                 \
      {                                                                       \
        u16 fmod;                                                             \
        if(fop <= 0x1 || fop == 0x8 || fop == 0x9 ||                          \
           (fop >= 0xC && fop <= 0xF))                                        \
          fmod = 0xCE;                          /* logical: must NZ, may C */ \
        else                                                                  \
          fmod = 0xFF;                          /* arithmetic: NZCV */        \
        if(fcond != 0x0E)                                                     \
          fmod &= 0x0F;                         /* conditional: may only */   \
        fstat |= fmod;                                                        \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  else if(fclass == 5)                                                        \
    fstat |= 0xF00;                             /* B/BL end the block */      \
  else if(fclass == 2 || fclass == 3)                                         \
  {                                                                           \
    if((opcode & 0x00100000) && (((opcode >> 12) & 0xF) == 15))               \
      fstat |= 0xF00;                           /* LDR pc */                  \
    if((opcode & 0x02000000) && ((opcode >> 4) & 0xFF) == 0x06)               \
      fstat |= 0x200;                           /* [Rn, Rm, RRX] reads C */   \
  }                                                                           \
  else if(fclass == 4)                                                        \
  {                                                                           \
    if((opcode & 0x00100000) &&                                               \
       ((opcode & 0x8000) || (opcode & 0x00400000)))                          \
      fstat |= 0xF00;                           /* LDM {..pc} / LDM^ */       \
  }                                                                           \
  else                                                                        \
    fstat |= 0xF00;                             /* SWI / coprocessor space */ \
  block_data[block_data_position].flag_data = fstat;                          \
}

/* A/B diagnostic: CGBA_SH4_ARM_DEAD_FLAGS=0 forces every ARM instruction to
 * see all-flags-live (the pre-liveness behavior) while keeping the scan
 * running — isolates the ARM dead-flag ANALYSIS from every other layer. */
#if defined(CGBA_SH4_ARM_DEAD_FLAGS) && (CGBA_SH4_ARM_DEAD_FLAGS == 0)
#define arm_load_flag_status()                                                \
  flag_status = 0xF;                                                          \

#else
#define arm_load_flag_status()                                                \
  flag_status = block_data[block_data_position].flag_data;                    \

#endif
#else

#define arm_flag_status()                                                     \

#define arm_load_flag_status()                                                \

#endif

#define translate_thumb_instruction()                                         \
  check_pc_region(pc);                                                        \
  last_opcode = opcode;                                                       \
  opcode = readaddress16(pc_address_block, (pc & 0x0FFF));                    \
  translate_thumb_instruction_loaded()

#define translate_thumb_instruction_loaded()                                  \
  flag_status = block_data[block_data_position].flag_data;                    \
  emit_trace_thumb_instruction(pc);                                           \
  u8 hiop = opcode >> 8;                                                      \
                                                                              \
  switch(hiop)                                                                \
  {                                                                           \
    case 0x00 ... 0x07:                                                       \
      /* LSL rd, rs, imm */                                                   \
      thumb_shift(shift, lsl, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x08 ... 0x0F:                                                       \
      /* LSR rd, rs, imm */                                                   \
      thumb_shift(shift, lsr, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x10 ... 0x17:                                                       \
      /* ASR rd, rs, imm */                                                   \
      thumb_shift(shift, asr, imm);                                           \
      break;                                                                  \
                                                                              \
    case 0x18 ... 0x19:                                                       \
      /* ADD rd, rs, rn */                                                    \
      thumb_data_proc(add_sub, adds, reg, rd, rs, rn);                        \
      break;                                                                  \
                                                                              \
    case 0x1A ... 0x1B:                                                       \
      /* SUB rd, rs, rn */                                                    \
      thumb_data_proc(add_sub, subs, reg, rd, rs, rn);                        \
      break;                                                                  \
                                                                              \
    case 0x1C ... 0x1D:                                                       \
      /* ADD rd, rs, imm */                                                   \
      thumb_data_proc(add_sub_imm, adds, imm, rd, rs, imm);                   \
      break;                                                                  \
                                                                              \
    case 0x1E ... 0x1F:                                                       \
      /* SUB rd, rs, imm */                                                   \
      thumb_data_proc(add_sub_imm, subs, imm, rd, rs, imm);                   \
      break;                                                                  \
                                                                              \
    /* MOV r0..7, imm */                                                      \
    case 0x20: thumb_data_proc_unary(imm, movs, imm, 0, imm); break;          \
    case 0x21: thumb_data_proc_unary(imm, movs, imm, 1, imm); break;          \
    case 0x22: thumb_data_proc_unary(imm, movs, imm, 2, imm); break;          \
    case 0x23: thumb_data_proc_unary(imm, movs, imm, 3, imm); break;          \
    case 0x24: thumb_data_proc_unary(imm, movs, imm, 4, imm); break;          \
    case 0x25: thumb_data_proc_unary(imm, movs, imm, 5, imm); break;          \
    case 0x26: thumb_data_proc_unary(imm, movs, imm, 6, imm); break;          \
    case 0x27: thumb_data_proc_unary(imm, movs, imm, 7, imm); break;          \
                                                                              \
    /* CMP r0, imm */                                                         \
    case 0x28: thumb_data_proc_test(imm, cmp, imm, 0, imm); break;            \
    case 0x29: thumb_data_proc_test(imm, cmp, imm, 1, imm); break;            \
    case 0x2A: thumb_data_proc_test(imm, cmp, imm, 2, imm); break;            \
    case 0x2B: thumb_data_proc_test(imm, cmp, imm, 3, imm); break;            \
    case 0x2C: thumb_data_proc_test(imm, cmp, imm, 4, imm); break;            \
    case 0x2D: thumb_data_proc_test(imm, cmp, imm, 5, imm); break;            \
    case 0x2E: thumb_data_proc_test(imm, cmp, imm, 6, imm); break;            \
    case 0x2F: thumb_data_proc_test(imm, cmp, imm, 7, imm); break;            \
                                                                              \
    /* ADD r0..7, imm */                                                      \
    case 0x30: thumb_data_proc(imm, adds, imm, 0, 0, imm); break;             \
    case 0x31: thumb_data_proc(imm, adds, imm, 1, 1, imm); break;             \
    case 0x32: thumb_data_proc(imm, adds, imm, 2, 2, imm); break;             \
    case 0x33: thumb_data_proc(imm, adds, imm, 3, 3, imm); break;             \
    case 0x34: thumb_data_proc(imm, adds, imm, 4, 4, imm); break;             \
    case 0x35: thumb_data_proc(imm, adds, imm, 5, 5, imm); break;             \
    case 0x36: thumb_data_proc(imm, adds, imm, 6, 6, imm); break;             \
    case 0x37: thumb_data_proc(imm, adds, imm, 7, 7, imm); break;             \
                                                                              \
    /* SUB r0..7, imm */                                                      \
    case 0x38: thumb_data_proc(imm, subs, imm, 0, 0, imm); break;             \
    case 0x39: thumb_data_proc(imm, subs, imm, 1, 1, imm); break;             \
    case 0x3A: thumb_data_proc(imm, subs, imm, 2, 2, imm); break;             \
    case 0x3B: thumb_data_proc(imm, subs, imm, 3, 3, imm); break;             \
    case 0x3C: thumb_data_proc(imm, subs, imm, 4, 4, imm); break;             \
    case 0x3D: thumb_data_proc(imm, subs, imm, 5, 5, imm); break;             \
    case 0x3E: thumb_data_proc(imm, subs, imm, 6, 6, imm); break;             \
    case 0x3F: thumb_data_proc(imm, subs, imm, 7, 7, imm); break;             \
                                                                              \
    case 0x40:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* AND rd, rs */                                                    \
          thumb_data_proc(alu_op, ands, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* EOR rd, rs */                                                    \
          thumb_data_proc(alu_op, eors, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* LSL rd, rs */                                                    \
          thumb_shift(alu_op, lsl, reg);                                      \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* LSR rd, rs */                                                    \
          thumb_shift(alu_op, lsr, reg);                                      \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x41:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* ASR rd, rs */                                                    \
          thumb_shift(alu_op, asr, reg);                                      \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* ADC rd, rs */                                                    \
          thumb_data_proc(alu_op, adcs, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* SBC rd, rs */                                                    \
          thumb_data_proc(alu_op, sbcs, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* ROR rd, rs */                                                    \
          thumb_shift(alu_op, ror, reg);                                      \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x42:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* TST rd, rs */                                                    \
          thumb_data_proc_test(alu_op, tst, reg, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* NEG rd, rs */                                                    \
          thumb_data_proc_unary(alu_op, neg, reg, rd, rs);                    \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* CMP rd, rs */                                                    \
          thumb_data_proc_test(alu_op, cmp, reg, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* CMN rd, rs */                                                    \
          thumb_data_proc_test(alu_op, cmn, reg, rd, rs);                     \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x43:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* ORR rd, rs */                                                    \
          thumb_data_proc(alu_op, orrs, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* MUL rd, rs */                                                    \
          thumb_data_proc(alu_op, muls, reg, rd, rs, rd);                     \
          cycle_count += SH4_THUMB_MUL_EXTRA_CYCLES;                          \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* BIC rd, rs */                                                    \
          thumb_data_proc(alu_op, bics, reg, rd, rd, rs);                     \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* MVN rd, rs */                                                    \
          thumb_data_proc_unary(alu_op, mvns, reg, rd, rs);                   \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x44:                                                                \
      /* ADD rd, rs */                                                        \
      thumb_data_proc_hi(add);                                                \
      break;                                                                  \
                                                                              \
    case 0x45:                                                                \
      /* CMP rd, rs */                                                        \
      thumb_data_proc_test_hi(cmp);                                           \
      break;                                                                  \
                                                                              \
    case 0x46:                                                                \
      /* MOV rd, rs */                                                        \
      thumb_data_proc_mov_hi();                                               \
      break;                                                                  \
                                                                              \
    case 0x47:                                                                \
      /* BX rs */                                                             \
      thumb_bx();                                                             \
      break;                                                                  \
                                                                              \
    case 0x48 ... 0x4F:                                                       \
      /* LDR r0..7, [pc + imm] */                                             \
      {                                                                       \
        thumb_decode_imm();                                                   \
        u32 rdreg = (hiop & 7);                                               \
        u32 aoff = (pc & ~2) + (imm*4) + 4;                                   \
        /* ROM literal pool -> optimize as const load (same/adj 4 KiB page). */\
        if (!ram_region && ((aoff >> 24) >= 0x08u) && ((aoff >> 24) <= 0x0Du)){\
          u8 *_pool_blk = ((aoff >> 12) == (pc >> 12))                        \
            ? pc_address_block : cgba_memory_map_read_4k(aoff);               \
          if (_pool_blk) {                                                    \
            u32 value = readaddress32(_pool_blk, (aoff & 0x0FFF));            \
            thumb_load_pc_pool_const(rdreg, value);                           \
          } else {                                                            \
            thumb_access_memory(load, imm, rdreg, 0, 0, pc_relative, aoff, u32);\
          }                                                                   \
        } else {                                                              \
          thumb_access_memory(load, imm, rdreg, 0, 0, pc_relative, aoff, u32);\
        }                                                                     \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x50 ... 0x51:                                                       \
      /* STR rd, [rb + ro] */                                                 \
      thumb_access_memory(store, mem_reg, rd, rb, ro, reg_reg, 0, u32);       \
      break;                                                                  \
                                                                              \
    case 0x52 ... 0x53:                                                       \
      /* STRH rd, [rb + ro] */                                                \
      thumb_access_memory(store, mem_reg, rd, rb, ro, reg_reg, 0, u16);       \
      break;                                                                  \
                                                                              \
    case 0x54 ... 0x55:                                                       \
      /* STRB rd, [rb + ro] */                                                \
      thumb_access_memory(store, mem_reg, rd, rb, ro, reg_reg, 0, u8);        \
      break;                                                                  \
                                                                              \
    case 0x56 ... 0x57:                                                       \
      /* LDSB rd, [rb + ro] */                                                \
      thumb_access_memory(load, mem_reg, rd, rb, ro, reg_reg, 0, s8);         \
      break;                                                                  \
                                                                              \
    case 0x58 ... 0x59:                                                       \
      /* LDR rd, [rb + ro] */                                                 \
      thumb_access_memory(load, mem_reg, rd, rb, ro, reg_reg, 0, u32);        \
      break;                                                                  \
                                                                              \
    case 0x5A ... 0x5B:                                                       \
      /* LDRH rd, [rb + ro] */                                                \
      thumb_access_memory(load, mem_reg, rd, rb, ro, reg_reg, 0, u16);        \
      break;                                                                  \
                                                                              \
    case 0x5C ... 0x5D:                                                       \
      /* LDRB rd, [rb + ro] */                                                \
      thumb_access_memory(load, mem_reg, rd, rb, ro, reg_reg, 0, u8);         \
      break;                                                                  \
                                                                              \
    case 0x5E ... 0x5F:                                                       \
      /* LDSH rd, [rb + ro] */                                                \
      thumb_access_memory(load, mem_reg, rd, rb, ro, reg_reg, 0, s16);        \
      break;                                                                  \
                                                                              \
    case 0x60 ... 0x67:                                                       \
      /* STR rd, [rb + imm] */                                                \
      thumb_access_memory(store, mem_imm, rd, rb, 0, reg_imm, (imm * 4),      \
       u32);                                                                  \
      break;                                                                  \
                                                                              \
    case 0x68 ... 0x6F:                                                       \
      /* LDR rd, [rb + imm] */                                                \
      thumb_access_memory(load, mem_imm, rd, rb, 0, reg_imm, (imm * 4), u32); \
      break;                                                                  \
                                                                              \
    case 0x70 ... 0x77:                                                       \
      /* STRB rd, [rb + imm] */                                               \
      thumb_access_memory(store, mem_imm, rd, rb, 0, reg_imm, imm, u8);       \
      break;                                                                  \
                                                                              \
    case 0x78 ... 0x7F:                                                       \
      /* LDRB rd, [rb + imm] */                                               \
      thumb_access_memory(load, mem_imm, rd, rb, 0, reg_imm, imm, u8);        \
      break;                                                                  \
                                                                              \
    case 0x80 ... 0x87:                                                       \
      /* STRH rd, [rb + imm] */                                               \
      thumb_access_memory(store, mem_imm, rd, rb, 0, reg_imm,                 \
       (imm * 2), u16);                                                       \
      break;                                                                  \
                                                                              \
    case 0x88 ... 0x8F:                                                       \
      /* LDRH rd, [rb + imm] */                                               \
      thumb_access_memory(load, mem_imm, rd, rb, 0, reg_imm, (imm * 2), u16); \
      break;                                                                  \
                                                                              \
    /* STR r0..7, [sp + imm] */                                               \
    case 0x90:                                                                \
      thumb_access_memory(store, imm, 0, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x91:                                                                \
      thumb_access_memory(store, imm, 1, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x92:                                                                \
      thumb_access_memory(store, imm, 2, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x93:                                                                \
      thumb_access_memory(store, imm, 3, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x94:                                                                \
      thumb_access_memory(store, imm, 4, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x95:                                                                \
      thumb_access_memory(store, imm, 5, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x96:                                                                \
      thumb_access_memory(store, imm, 6, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
    case 0x97:                                                                \
      thumb_access_memory(store, imm, 7, 13, 0, reg_imm_sp, imm, u32);        \
      break;                                                                  \
                                                                              \
    /* LDR r0..7, [sp + imm] */                                               \
    case 0x98:                                                                \
      thumb_access_memory(load, imm, 0, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x99:                                                                \
      thumb_access_memory(load, imm, 1, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9A:                                                                \
      thumb_access_memory(load, imm, 2, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9B:                                                                \
      thumb_access_memory(load, imm, 3, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9C:                                                                \
      thumb_access_memory(load, imm, 4, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9D:                                                                \
      thumb_access_memory(load, imm, 5, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9E:                                                                \
      thumb_access_memory(load, imm, 6, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
    case 0x9F:                                                                \
      thumb_access_memory(load, imm, 7, 13, 0, reg_imm_sp, imm, u32);         \
      break;                                                                  \
                                                                              \
    /* ADD r0..7, pc, +imm */                                                 \
    case 0xA0: thumb_load_pc(0); break;                                       \
    case 0xA1: thumb_load_pc(1); break;                                       \
    case 0xA2: thumb_load_pc(2); break;                                       \
    case 0xA3: thumb_load_pc(3); break;                                       \
    case 0xA4: thumb_load_pc(4); break;                                       \
    case 0xA5: thumb_load_pc(5); break;                                       \
    case 0xA6: thumb_load_pc(6); break;                                       \
    case 0xA7: thumb_load_pc(7); break;                                       \
                                                                              \
    /* ADD r0..7, sp, +imm */                                                 \
    case 0xA8: thumb_load_sp(0); break;                                       \
    case 0xA9: thumb_load_sp(1); break;                                       \
    case 0xAA: thumb_load_sp(2); break;                                       \
    case 0xAB: thumb_load_sp(3); break;                                       \
    case 0xAC: thumb_load_sp(4); break;                                       \
    case 0xAD: thumb_load_sp(5); break;                                       \
    case 0xAE: thumb_load_sp(6); break;                                       \
    case 0xAF: thumb_load_sp(7); break;                                       \
                                                                              \
    case 0xB0 ... 0xB3:                                                       \
      if((opcode >> 7) & 0x01)                                                \
      {                                                                       \
        /* ADD sp, -imm */                                                    \
        thumb_adjust_sp(down);                                                \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        /* ADD sp, +imm */                                                    \
        thumb_adjust_sp(up);                                                  \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0xB4:                                                                \
      /* PUSH rlist */                                                        \
      thumb_block_memory(store, down, no, 13);                                \
      break;                                                                  \
                                                                              \
    case 0xB5:                                                                \
      /* PUSH rlist, lr */                                                    \
      thumb_block_memory(store, push_lr, push_lr, 13);                        \
      break;                                                                  \
                                                                              \
    case 0xBC:                                                                \
      /* POP rlist */                                                         \
      thumb_block_memory(load, no, up, 13);                                   \
      break;                                                                  \
                                                                              \
    case 0xBD:                                                                \
      /* POP rlist, pc */                                                     \
      thumb_block_memory(load, no, pop_pc, 13);                               \
      break;                                                                  \
                                                                              \
    case 0xC0:                                                                \
      /* STMIA r0!, rlist */                                                  \
      thumb_block_memory(store, no, up, 0);                                   \
      break;                                                                  \
                                                                              \
    case 0xC1:                                                                \
      /* STMIA r1!, rlist */                                                  \
      thumb_block_memory(store, no, up, 1);                                   \
      break;                                                                  \
                                                                              \
    case 0xC2:                                                                \
      /* STMIA r2!, rlist */                                                  \
      thumb_block_memory(store, no, up, 2);                                   \
      break;                                                                  \
                                                                              \
    case 0xC3:                                                                \
      /* STMIA r3!, rlist */                                                  \
      thumb_block_memory(store, no, up, 3);                                   \
      break;                                                                  \
                                                                              \
    case 0xC4:                                                                \
      /* STMIA r4!, rlist */                                                  \
      thumb_block_memory(store, no, up, 4);                                   \
      break;                                                                  \
                                                                              \
    case 0xC5:                                                                \
      /* STMIA r5!, rlist */                                                  \
      thumb_block_memory(store, no, up, 5);                                   \
      break;                                                                  \
                                                                              \
    case 0xC6:                                                                \
      /* STMIA r6!, rlist */                                                  \
      thumb_block_memory(store, no, up, 6);                                   \
      break;                                                                  \
                                                                              \
    case 0xC7:                                                                \
      /* STMIA r7!, rlist */                                                  \
      thumb_block_memory(store, no, up, 7);                                   \
      break;                                                                  \
                                                                              \
    case 0xC8:                                                                \
      /* LDMIA r0!, rlist */                                                  \
      thumb_block_memory(load, no, up, 0);                                    \
      break;                                                                  \
                                                                              \
    case 0xC9:                                                                \
      /* LDMIA r1!, rlist */                                                  \
      thumb_block_memory(load, no, up, 1);                                    \
      break;                                                                  \
                                                                              \
    case 0xCA:                                                                \
      /* LDMIA r2!, rlist */                                                  \
      thumb_block_memory(load, no, up, 2);                                    \
      break;                                                                  \
                                                                              \
    case 0xCB:                                                                \
      /* LDMIA r3!, rlist */                                                  \
      thumb_block_memory(load, no, up, 3);                                    \
      break;                                                                  \
                                                                              \
    case 0xCC:                                                                \
      /* LDMIA r4!, rlist */                                                  \
      thumb_block_memory(load, no, up, 4);                                    \
      break;                                                                  \
                                                                              \
    case 0xCD:                                                                \
      /* LDMIA r5!, rlist */                                                  \
      thumb_block_memory(load, no, up, 5);                                    \
      break;                                                                  \
                                                                              \
    case 0xCE:                                                                \
      /* LDMIA r6!, rlist */                                                  \
      thumb_block_memory(load, no, up, 6);                                    \
      break;                                                                  \
                                                                              \
    case 0xCF:                                                                \
      /* LDMIA r7!, rlist */                                                  \
      thumb_block_memory(load, no, up, 7);                                    \
      break;                                                                  \
                                                                              \
    case 0xD0:                                                                \
      /* BEQ label */                                                         \
      thumb_conditional_branch(eq);                                           \
      break;                                                                  \
                                                                              \
    case 0xD1:                                                                \
      /* BNE label */                                                         \
      thumb_conditional_branch(ne);                                           \
      break;                                                                  \
                                                                              \
    case 0xD2:                                                                \
      /* BCS label */                                                         \
      thumb_conditional_branch(cs);                                           \
      break;                                                                  \
                                                                              \
    case 0xD3:                                                                \
      /* BCC label */                                                         \
      thumb_conditional_branch(cc);                                           \
      break;                                                                  \
                                                                              \
    case 0xD4:                                                                \
      /* BMI label */                                                         \
      thumb_conditional_branch(mi);                                           \
      break;                                                                  \
                                                                              \
    case 0xD5:                                                                \
      /* BPL label */                                                         \
      thumb_conditional_branch(pl);                                           \
      break;                                                                  \
                                                                              \
    case 0xD6:                                                                \
      /* BVS label */                                                         \
      thumb_conditional_branch(vs);                                           \
      break;                                                                  \
                                                                              \
    case 0xD7:                                                                \
      /* BVC label */                                                         \
      thumb_conditional_branch(vc);                                           \
      break;                                                                  \
                                                                              \
    case 0xD8:                                                                \
      /* BHI label */                                                         \
      thumb_conditional_branch(hi);                                           \
      break;                                                                  \
                                                                              \
    case 0xD9:                                                                \
      /* BLS label */                                                         \
      thumb_conditional_branch(ls);                                           \
      break;                                                                  \
                                                                              \
    case 0xDA:                                                                \
      /* BGE label */                                                         \
      thumb_conditional_branch(ge);                                           \
      break;                                                                  \
                                                                              \
    case 0xDB:                                                                \
      /* BLT label */                                                         \
      thumb_conditional_branch(lt);                                           \
      break;                                                                  \
                                                                              \
    case 0xDC:                                                                \
      /* BGT label */                                                         \
      thumb_conditional_branch(gt);                                           \
      break;                                                                  \
                                                                              \
    case 0xDD:                                                                \
      /* BLE label */                                                         \
      thumb_conditional_branch(le);                                           \
      break;                                                                  \
                                                                              \
    case 0xDF:                                                                \
    {                                                                         \
      u32 swinum = opcode & 0xFF;                                             \
      if (swinum == 6) {                                                      \
        cycle_count += ARM_THREAD_HLE_DIV_CYCLES;                             \
        arm_hle_div(thumb);                                                   \
      }                                                                       \
      else if (swinum == 7) {                                                 \
        cycle_count += ARM_THREAD_HLE_DIV_CYCLES;                             \
        arm_hle_div_arm(thumb);                                               \
      }                                                                       \
      else {                                                                  \
        thumb_swi();                                                          \
      }                                                                       \
      break;                                                                  \
    }                                                                         \
                                                                              \
    case 0xE0 ... 0xE7:                                                       \
    {                                                                         \
      /* B label */                                                           \
      thumb_b();                                                              \
      break;                                                                  \
    }                                                                         \
                                                                              \
    case 0xF0 ... 0xF7:                                                       \
    {                                                                         \
      /* BL prefix (high word). Emitters materialize the temporary LR here so  \
         a cycle-budget exit between the BL halves can resume at the suffix     \
         (thumb_blh) with the correct LR instead of a stale one. Combined BLs   \
         overwrite this LR in thumb_bl(). */                                   \
      thumb_bl_prefix();                                                       \
      break;                                                                  \
    }                                                                         \
                                                                              \
    case 0xF8 ... 0xFF:                                                       \
    {                                                                         \
      /* (high word) BL label */                                              \
      /* This might not be preceeding a BL low word (Golden Sun 2), if so     \
         it must be handled like an indirect branch. */                       \
      if((last_opcode >= 0xF000) && (last_opcode < 0xF800))                   \
      {                                                                       \
        thumb_bl();                                                           \
      }                                                                       \
      else                                                                    \
      {                                                                       \
        thumb_blh();                                                          \
      }                                                                       \
      break;                                                                  \
    }                                                                         \
  }                                                                           \
                                                                              \
  pc += 2                                                                     \

#define thumb_flag_modifies_all()                                             \
  flag_status |= 0xFF                                                         \

#define thumb_flag_modifies_zn()                                              \
  flag_status |= 0xCC                                                         \

#define thumb_flag_modifies_znc()                                             \
  flag_status |= 0xEE                                                         \

#define thumb_flag_modifies_zn_maybe_c()                                      \
  flag_status |= 0xCE                                                         \

#define thumb_flag_modifies_c()                                               \
  flag_status |= 0x22                                                         \

#define thumb_flag_requires_c()                                               \
  flag_status |= 0x200                                                        \

#define thumb_flag_requires_all()                                             \
  flag_status |= 0xF00                                                        \

/* Combined Thumb high-byte scan/peephole LUT:
 *   bits  0..11: flag_data (1 = special-case sub-decode: 0x00..0x07,
 *                0x40..0x42, 0x44, 0x46, 0xDF)
 *   bit  14 (0x4000): cur_hi peephole candidate (0x04,0x06,0x18,0x19,0x44,0x80)
 *   bit  15 (0x8000): thumb_exit_point (unconditional/conditional exit) */
static const u16 cgba_thumb_hi_lut[256] = {
  /* 0x00..0x07: LSL imm (0x04/0x06 = LSL #16/#24 extend candidate) */
  1, 1, 1, 1, 0x4001u, 1, 0x4001u, 1,
  /* 0x08..0x17: LSR/ASR imm (modifies Z,N,C) */
  0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
  0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
  /* 0x18..0x1F: ADD/SUB fmt2 (0x18/0x19 = ADD reg, ram-idx candidate) */
  0x40FFu, 0x40FFu, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  /* 0x20..0x27: MOV imm (modifies Z,N) */
  0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
  /* 0x28..0x3F: CMP/ADD/SUB imm8 (modifies all) */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  /* 0x40..0x47: ALU, hi-reg (0x44 ram-idx cand, 0x46 maybe exit), BX (exit) */
  1, 1, 1, 0xCC, 0x4001u, 0xFF, 1, 0x8F00u,
  /* 0x48..0x7F: loads/stores */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x80..0x8F: STRH/LDRH (0x80 = STRH #0 stride candidate) */
  0x4000u, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0x90..0x9F: SP-relative STR/LDR (handled by unified sp_span pass) */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xA0..0xAF: ADD PC/SP imm */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xB0..0xBF: misc (0xBD = POP {..., pc} exit + requires all) */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0x8F00u, 0, 0,
  /* 0xC0..0xCF: LDMIA/STMIA */
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xD0..0xDF: Bcc (exit) / 0xDF SWI (special: non-div SWI is exit) */
  0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u,
  0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 1,
  /* 0xE0..0xEF: B (0xE0..0xE7 exit + requires all), undefined (0xE8..0xEF) */
  0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u,
  0, 0, 0, 0, 0, 0, 0, 0,
  /* 0xF0..0xFF: BL (0xF0..0xF7 prefix requires all; 0xF8..0xFF exit + requires all) */
  0x0F00u, 0x0F00u, 0x0F00u, 0x0F00u, 0x0F00u, 0x0F00u, 0x0F00u, 0x0F00u,
  0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u, 0x8F00u
};

static inline u16 cgba_thumb_scan_info(u32 opcode)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  u16 f = (u16)(cgba_thumb_hi_lut[hi] & 0x8FFFu);
  if (__builtin_expect((f & 0x0FFFu) == 1u, 0)) {
    if (hi < 0x08u)
      return (opcode & 0x07C0u) ? 0xEEu : 0xCCu;
    if (hi <= 0x42u) {
      static const u16 alu_lut[12] = {
        0xCCu, 0xCCu, 0xCEu, 0xCEu,    /* 0x40: AND, EOR, LSL, LSR */
        0xCEu, 0x2FFu, 0x2FFu, 0xCEu,  /* 0x41: ASR, ADC, SBC, ROR */
        0xCCu, 0xFFu, 0xFFu, 0xFFu     /* 0x42: TST, NEG, CMP, CMN */
      };
      return alu_lut[(opcode >> 6) & 0x0Fu];
    }
    if (hi == 0xDFu)
      return is_div_swi(opcode & 0xFFu) ? 0x0F00u : 0x8F00u;
    return ((opcode & 0x87u) == 0x87u)
             ? (hi == 0x46u ? 0x8F00u : 0x0F00u)
             : 0u;
  }
  return f;
}

#ifdef SH4_ARCH
#define thumb_flag_status()                                                   \
  do {                                                                        \
    cgba_thumb_scan_fl = cgba_thumb_scan_info(opcode);                        \
    u16 _tfd = (u16)(cgba_thumb_scan_fl & 0x0FFFu);                           \
    block_data[block_data_position].flag_data = _tfd;                         \
    block_data[block_data_position].raw_flag_data = _tfd;                     \
  } while(0)
#else
#define thumb_flag_status()                                                   \
  do {                                                                        \
    cgba_thumb_scan_fl = cgba_thumb_scan_info(opcode);                        \
    block_data[block_data_position].flag_data =                               \
      (u16)(cgba_thumb_scan_fl & 0x0FFFu);                                    \
  } while(0)
#endif

// I/EWRAM memory tagging
// Code emitted in the RAM cache has tags (16 bit values) in the mirror tag ram
// that indicate that the address contains code. The following values are used:
// 0x0000 : this is just data (never translated)
// 0x00XX : not used (since first byte is zero)
// 0x0101 : this is code that is not the start of a translated block
// 0xXXXX : this is the start of a translated block, starting from 0xFFFF downwards
//          LSB is always set (we decrement by two) to ensure both bytes != 0
//
// The tag value is an index to a `ramtag_type` structure that sits at the end
// of the RAM CACHE (grows like a stack). For simplicity we start tags at 0xFFFF
// and grow like a stack.

#define LAST_TAG_NUM       0x0101
#define INITIAL_TOP_TAG    0xFFFF
#define CODE_TAG_BLOCK16   0x0101
#define CODE_TAG_BLOCK32   0x01010101

#define VALID_TAG(tagn) (tagn > LAST_TAG_NUM)

#define allocate_tag_arm(location) {   \
  location[0] = ram_block_tag;         \
  /* Could be another thumb inst */    \
  if (!location[1])                    \
    location[1] = CODE_TAG_BLOCK16;    \
  ram_block_tag -= 2;                  \
}
#define allocate_tag_width_arm 4u

#define allocate_tag_thumb(location) { \
  location[0] = ram_block_tag;         \
  ram_block_tag -= 2;                  \
}
#define allocate_tag_width_thumb 2u

typedef struct
{
  u32 offset_arm;     // Cache offset to the ARM-mode compiled block
  u32 offset_thumb;   // Cache offset to the Thumb-mode compiled block
} ramtag_type;

#ifdef SH4_ARCH
typedef char cgba_sh4_ram_tag_size_must_match_guard[
  sizeof(ramtag_type) == CGBA_SH4_RAM_TAG_BYTES ? 1 : -1];
#endif

static u32 ram_block_tag = INITIAL_TOP_TAG;

#if defined(CGBA_GPSP_HEADLESS_TEST)
/* DIAG: translation-storm tracing (headless only). Prints go to the
 * emulator debug port; capped so a livelock cannot flood the log. */
static void cgba_diag_puts(const char *s)
{
  while(*s)
    *(volatile unsigned char *)0xb7000000u = (unsigned char)*s++;
  *(volatile unsigned char *)0xb7000000u = '\n';
}
static unsigned cgba_diag_budget = 4000;
#define CGBA_DIAG_LOG(...) do {                                               \
    if(cgba_diag_budget) {                                                    \
      char _dbg[120];                                                         \
      cgba_diag_budget--;                                                     \
      snprintf(_dbg, sizeof _dbg, __VA_ARGS__);                               \
      cgba_diag_puts(_dbg);                                                   \
    }                                                                         \
  } while(0)
#else
#define CGBA_DIAG_LOG(...) do { } while(0)
#endif


inline static ramtag_type* get_ram_tag(u16 tagval) {
  ramtag_type *tbl = (ramtag_type*)&ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];
  s16 tgidx = (s16)(tagval);
  return &tbl[tgidx >> 1];  /* Since LSB is always 1 and thus unused */
}

// This function will return a pointer to a translated block of code. If it
// doesn't exist it will translate it, if it does it will pass it back.

// type should be "arm", "thumb", or "dual." For arm or thumb the PC should
// be a real PC, for dual the least significant bit will determine if it's
// ARM or Thumb mode.

#define block_lookup_address_pc_arm()                                         \
  u32 thumb = 0;                                                              \
  pc &= ~0x03

#define block_lookup_address_pc_thumb()                                       \
  u32 thumb = 1;                                                              \
  pc &= ~0x01                                                                 \


#if defined(SH4_ARCH) && defined(CGBA_GPSP_HEADLESS_TEST)
extern unsigned long cgba_em_blk_n, cgba_em_blk_bytes;
extern unsigned long cgba_em_blk_max_bytes, cgba_em_blk_hist[6];
extern unsigned long cgba_em_blk_mode_n[2], cgba_em_blk_mode_bytes[2];
#define CGBA_EM_BLK_STAT(nbytes, is_thumb) do {                              \
  unsigned long _b = (unsigned long)(nbytes);                                \
  unsigned _m = (is_thumb) ? 1u : 0u;                                        \
  unsigned _h = (_b <= 64) ? 0u : (_b <= 128) ? 1u :                        \
                (_b <= 256) ? 2u : (_b <= 512) ? 3u :                       \
                (_b <= 1024) ? 4u : 5u;                                     \
  cgba_em_blk_n++; cgba_em_blk_bytes += _b;                                  \
  if (_b > cgba_em_blk_max_bytes) cgba_em_blk_max_bytes = _b;                \
  cgba_em_blk_hist[_h]++;                                                     \
  cgba_em_blk_mode_n[_m]++; cgba_em_blk_mode_bytes[_m] += _b;                \
} while (0)
#else
#define CGBA_EM_BLK_STAT(nbytes, is_thumb) ((void)0)
#endif

#ifndef CGBA_SH4_HOT_THRESHOLD
#define CGBA_SH4_HOT_THRESHOLD 64
#endif
#ifndef CGBA_SH4_HOT_TIER1
#define CGBA_SH4_HOT_TIER1 1u
#endif
#ifndef CGBA_SH4_HOT_TIER2
#define CGBA_SH4_HOT_TIER2 4u
#endif
#if CGBA_SH4_HOT_THRESHOLD > 255
#error "CGBA_SH4_HOT_THRESHOLD must fit the u8 hot counters (0..255; 0 = gate off)"
#endif
/* Declared unconditionally so the gate block parses on every arch; non-SH4
   arms fold it away via cgba_cold_gate_enable == 0. */
extern u8  cgba_hot_count[16384];

#ifndef CGBA_SH4_HEAT_FLUSH_LEAK
#define CGBA_SH4_HEAT_FLUSH_LEAK 16   /* per-flush hot-table decay */
#endif
extern int cgba_cold_pending;
#ifdef SH4_ARCH
extern int cgba_cold_gate_enable;
extern int cgba_cold_gate_probe;
#else
#define cgba_cold_gate_enable 0
#define cgba_cold_gate_probe 0
#endif

#ifdef SH4_ARCH
#ifndef CGBA_SH4_THUMB_CHAIN_EXISTING
#define CGBA_SH4_THUMB_CHAIN_EXISTING 1
#endif
#ifndef CGBA_SH4_THUMB_SEAM_ALIAS
#define CGBA_SH4_THUMB_SEAM_ALIAS 1
#endif
static inline u8 *cgba_sh4_chase_bra(u8 *target)
{
  if (target >= rom_translation_cache &&
      target + 4 <= rom_translation_ptr) {
    u16 ins0 = ((u16)target[0] << 8) | (u16)target[1];
    u16 ins1 = ((u16)target[2] << 8) | (u16)target[3];
    if ((ins0 & 0xF000u) == 0xA000u && ins1 == 0x0009u) {
      s32 disp = (s32)(((u32)(ins0 & 0x0FFFu) ^ 0x0800u) - 0x0800u) * 2;
      u8 *dst = target + 4 + disp;
      if (dst >= rom_translation_cache && dst < rom_translation_ptr)
        return dst;
    }
  }
  return target;
}

static void cgba_sh4_emit_chain_existing(u8 **tp, u32 *cycle_count,
                                         u8 *chain_existing_target)
{
  u8 *chain_dst;
  long chain_disp;
  sh4g_litseg_reserve(tp, 2);
  if (*cycle_count != 0) {
    sh4g_cycle_debit(tp, (int)*cycle_count);
    *cycle_count = 0;
  }
  chain_dst = cgba_sh4_chase_bra(chain_existing_target);
  chain_disp = ((long)(uintptr_t)chain_dst -
                ((long)(uintptr_t)(*tp) + 4)) / 2;
  if (chain_disp >= -2048 && chain_disp <= 2047) {
    sh4g_u16(tp, (u16)(0xA000u | (chain_disp & 0x0FFFu)));
    sh4g_u16(tp, 0x0009u);
  } else {
    sh4g_far_jmp(tp, chain_dst);
  }
}

static u8 *cgba_rom_hash_lookup_thumb(u32 pc)
{
  u32 key = pc | 1u;
  u32 hash_target = ((key * 2654435761U) >> (32 - ROM_BRANCH_HASH_BITS))
                    & (ROM_BRANCH_HASH_SIZE - 1);
  u32 blk_offset = rom_branch_hash[hash_target];
  while (blk_offset) {
    hashhdr_type *bhdr = (hashhdr_type *)&rom_translation_cache[blk_offset];
    if (bhdr->pc_value == key)
      return cgba_sh4_chase_bra(&rom_translation_cache[
        blk_offset + sizeof(hashhdr_type) + block_prologue_size]);
    blk_offset = bhdr->next_entry;
  }
  return NULL;
}
#define CGBA_SH4_CHASE_ROM_HIT(ptr) cgba_sh4_chase_bra(ptr)
#else
#define CGBA_SH4_CHASE_ROM_HIT(ptr) (ptr)
#endif

#define block_lookup_translate_builder(type)                                  \
u8 function_cc *block_lookup_translate_##type(u32 pc)                         \
{                                                                             \
  u8 pcregion = (pc >> 24);                                                   \
                                                                              \
  block_lookup_address_pc_##type();                                           \
  if(!cgba_sh4_jit_exec_domain(pc))                                           \
    return (u8 *)(~(uintptr_t)0);                                             \
                                                                              \
  switch(pcregion)                                                            \
  {                                                                           \
    case 0x2:                                                                 \
    case 0x3:                                                                 \
    {                                                                         \
      u16* tagp;                                                              \
      ramtag_type* trentry;                                                   \
      if (pcregion == 3 && cgba_sh4_iwram_page_quarantined(pc)) {             \
        cgba_sh4_set_cold_pending();                                          \
        return NULL;                                                          \
      }                                                                       \
      tagp = (pcregion == 2) ? (u16 *)(ewram + (pc & 0x3FFFF) + 0x40000)      \
                             : (u16 *)(iwram + (pc & 0x7FFF));                \
      /* Allocate a tag if not a valid one, and initialize header */          \
      if (!VALID_TAG(*tagp)) {                                                \
        allocate_tag_##type(tagp);                                            \
        if (pcregion == 3) {                                                  \
          u32 _iw_off = pc & 0x7FFFu;                                         \
          u32 _iw_end = _iw_off + allocate_tag_width_##type;                  \
          if (_iw_off < iwram_code_min) iwram_code_min = _iw_off;             \
          if (_iw_end > cgba_iwram_code_end) cgba_iwram_code_end = _iw_end;   \
        } else {                                                              \
          u32 _ew_off = pc & 0x3FFFFu;                                        \
          u32 _ew_end = _ew_off + allocate_tag_width_##type;                  \
          if (_ew_off < ewram_code_min) ewram_code_min = _ew_off;             \
          if (_ew_end > cgba_ewram_code_end) cgba_ewram_code_end = _ew_end;   \
        }                                                                     \
        trentry = get_ram_tag(*tagp);                                         \
        trentry->offset_arm = 0;                                              \
        trentry->offset_thumb = 0;                                            \
      } else {                                                                \
        trentry = get_ram_tag(*tagp);                                         \
      }                                                                       \
                                                                              \
      if (!trentry->offset_##type) {                                          \
        bool result;                                                          \
        u8 *blkptr = ram_translation_ptr + block_prologue_size;               \
        trentry->offset_##type = blkptr - ram_translation_cache;              \
        result = translate_block_##type(pc, true);                            \
                                                                              \
        if (result)                                                           \
          return blkptr;                                                      \
      } else {                                                                \
        return &ram_translation_cache[trentry->offset_##type];                \
      }                                                                       \
      return NULL;                                                            \
    }                                                                         \
                                                                              \
    case 0x0:                                                                 \
    case 0x8 ... 0xD:                                                         \
    {                                                                         \
      u32 key = pc | thumb;                                                   \
      u32 hash_target = ((key * 2654435761U) >> (32 - ROM_BRANCH_HASH_BITS))  \
                                              & (ROM_BRANCH_HASH_SIZE - 1);   \
                                                                              \
      hashhdr_type *bhdr;                                                     \
      u32 blk_offset = rom_branch_hash[hash_target];                          \
      u32 *blk_offset_addr = &rom_branch_hash[hash_target];                   \
      while(blk_offset)                                                       \
      {                                                                       \
        bhdr = (hashhdr_type*)&rom_translation_cache[blk_offset];             \
        if(bhdr->pc_value == key) {                                           \
          return CGBA_SH4_CHASE_ROM_HIT(&rom_translation_cache[               \
                  blk_offset + sizeof(hashhdr_type) + block_prologue_size]);  \
        }                                                                     \
                                                                              \
        blk_offset = bhdr->next_entry;                                        \
        blk_offset_addr = &bhdr->next_entry;                                  \
      }                                                                       \
                                                                              \
      /* Cold-code gate (SH4): only translate ROM blocks that have proven   \
         hot; colder dispatches interpret a chunk instead (the stub routes   \
         NULL + cgba_cold_pending to sh4_cold_interp_entry). Enabled only    \
         for the stub resolvers; translation gates and the single-block     \
         diff harness translate unconditionally. */                          \
      if(CGBA_SH4_HOT_THRESHOLD > 0 &&                                        \
         cgba_cold_gate_enable && pcregion >= 0x8 &&                          \
         !CGBA_SH4_DIFF_ACTIVE()) {                                           \
        u32 hot_idx = (key * 2654435761U) >> 18;                              \
        size_t rom_used = (size_t)(rom_translation_ptr - rom_translation_cache); \
        u32 eff_thresh =                                                      \
          (rom_used < (ROM_TRANSLATION_CACHE_SIZE * 5u / 8u)) ? (u32)CGBA_SH4_HOT_TIER1 : \
          (rom_used < (ROM_TRANSLATION_CACHE_SIZE * 7u / 8u)) ? (u32)CGBA_SH4_HOT_TIER2 : \
          (u32)CGBA_SH4_HOT_THRESHOLD;                                        \
        if (eff_thresh > (u32)CGBA_SH4_HOT_THRESHOLD)                         \
          eff_thresh = (u32)CGBA_SH4_HOT_THRESHOLD;                           \
        if(cgba_hot_count[hot_idx] < eff_thresh) {                            \
          /* Heat only on real dispatch (resolver path). External-exit        \
             resolution PROBES without heating: in flush-thrash scenes each   \
             retranslation would otherwise bump every cold branch target,     \
             so never-executed code crosses the threshold and fills the       \
             cache — a compounding feedback loop. */                          \
          if (!cgba_cold_gate_probe)                                          \
            cgba_hot_count[hot_idx]++;                                        \
          cgba_cold_pending = 1;                                              \
          return NULL;                                                        \
        }                                                                     \
      }                                                                       \
                                                                              \
      { /* Not found, go ahead and translate, and backfill the hash table */  \
        u8 *blkptr;                                                           \
        bool result;                                                          \
        bhdr = (hashhdr_type*)rom_translation_ptr;                            \
        bhdr->pc_value = key;                                                 \
        bhdr->next_entry = 0;                                                 \
        *blk_offset_addr = (u32)(rom_translation_ptr - rom_translation_cache);\
        rom_translation_ptr += sizeof(hashhdr_type);                          \
        blkptr = rom_translation_ptr + block_prologue_size;                   \
        result = translate_block_##type(pc, false);                           \
                                                                              \
        if (result)                                                           \
          return blkptr;                                                      \
      }                                                                       \
      return NULL;                                                            \
    }                                                                         \
  }                                                                           \
                                                                              \
  /* Do not return NULL since it could indeed happen that some branch         \
     points to some random place (perhaps due to being garbage). This can     \
     happen when especulatively compiling code in RAM. Perhaps the game       \
     patches these instructions later, which would trigger a flush */         \
  return (u8*)(~0);                                                           \
}                                                                             \

block_lookup_translate_builder(arm);
block_lookup_translate_builder(thumb);

#ifndef CGBA_GPSP_HEADLESS_WJ_START
#define CGBA_GPSP_HEADLESS_WJ_START 0u
#endif
#ifndef CGBA_GPSP_HEADLESS_WJ_END
#define CGBA_GPSP_HEADLESS_WJ_END 0xffffffffu
#endif

/* Ring of recent C-resolver dispatch targets (block sequence). Compiled in
 * ALL dynarec builds — the crash reporter prints its tail so a wild-jump
 * photo shows the guest's path into the bad branch (resolver dispatches are
 * already the slow path, one ring store is noise). The boot/vector-region
 * logging below additionally streams to the emulator debug port and remains
 * headless-only. */
u32 cgba_wj_ring[24];
unsigned cgba_wj_pos;

#ifdef CGBA_GPSP_HEADLESS_TEST
/* DIAG: trace guest jumps into the BIOS boot/vector region (a wild jump =
 * corrupted control flow). Logs the target + a ring of recent resolver targets
 * (block sequence) + LR to the casio-emu putchar port. */
static void cgba_wj_putc(char c) { *(volatile unsigned char *)0xb7000000u = (unsigned char)c; }
static void cgba_wj_hex(u32 v) { static const char h[] = "0123456789ABCDEF"; int i;
  for (i = 7; i >= 0; i--) cgba_wj_putc(h[(v >> (i * 4)) & 0xF]); }
static void cgba_wj_note_boot_region(u32 pc)
{
  unsigned i;
  if (pc >= 0x260u) return;                 /* not the boot/vector region */
  if (frame_counter > (u32)CGBA_GPSP_HEADLESS_WJ_END)
    return;
#if CGBA_GPSP_HEADLESS_WJ_START != 0
  if (frame_counter < (u32)CGBA_GPSP_HEADLESS_WJ_START)
    return;
#endif
  cgba_wj_putc('@'); cgba_wj_putc('@'); cgba_wj_putc('W'); cgba_wj_putc('J');
  cgba_wj_putc(' '); cgba_wj_hex(pc);
  cgba_wj_putc(' '); cgba_wj_putc('l'); cgba_wj_putc('r'); cgba_wj_hex(reg[14]);
  cgba_wj_putc(' '); cgba_wj_putc('s'); cgba_wj_putc('p'); cgba_wj_hex(reg[13]);
  cgba_wj_putc(' '); cgba_wj_putc('['); cgba_wj_hex(read_memory32(reg[13] - 4));
  cgba_wj_putc(','); cgba_wj_hex(read_memory32(reg[13])); cgba_wj_putc(']');
  cgba_wj_putc(':');
  for (i = 0; i < 24; i++) { cgba_wj_hex(cgba_wj_ring[(cgba_wj_pos + i) % 24]); cgba_wj_putc(' '); }
  cgba_wj_putc('\n');
}
#endif

static void cgba_wj_note(u32 pc)
{
  cgba_wj_ring[cgba_wj_pos] = pc; cgba_wj_pos = (cgba_wj_pos + 1) % 24;
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_wj_note_boot_region(pc);
#endif
}

u8 function_cc *block_lookup_address_dual(u32 pc)
{
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_lookup_dual_count++;
#endif
  u32 thumb = pc & 0x01;
  if(thumb) {
    pc &= ~1;
    reg[REG_CPSR] |= 0x20;
    return block_lookup_address_thumb(pc);
  } else {
    pc = (pc + 2) & ~0x03;
    reg[REG_CPSR] &= ~0x20;
    return block_lookup_address_arm(pc);
  }
}

u8 function_cc *block_lookup_address_arm(u32 pc)
{
  unsigned i;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_lookup_arm_count++;
#endif
#ifdef SH4_ARCH
  /* Commit the resolved PC. lookup_pc already has reg[REG_PC] == pc (no-op), but
   * the BX / computed-PC trampolines jump straight here without storing it, so
   * an indirect branch would otherwise leave reg[REG_PC] stale — which defeats
   * cgba_dynarec_single_block (the diff harness reads reg[REG_PC] as the block
   * end) and would mis-bank an IRQ taken right after the branch. */
  reg[REG_PC] = pc;
  /* BIOS IRQ wrapper HLE (sh4_interp_helpers.c): dispatch the vector and the
   * epilogue natively instead of two interpreter round-trips per IRQ. */
  if(pc == 0x00000018u && reg[CPU_MODE] == MODE_IRQ &&
     !CGBA_SH4_DIFF_ACTIVE()) {
    u32 handler = cgba_hle_bios_irq_entry();
    if(handler != 0)
      pc = handler;                /* resolve the game handler directly */
  }
  else if(pc == 0x00000030u && reg[CPU_MODE] == MODE_IRQ &&
          !CGBA_SH4_DIFF_ACTIVE()) {
    u32 np = cgba_hle_bios_irq_exit();
    if(np == 0x00000018u)          /* pending IRQ re-entry */
      np = cgba_hle_bios_irq_entry();
    if(np >= 0x00004000u) {
      if(reg[REG_CPSR] & 0x20)     /* interrupted code was Thumb */
        return block_lookup_address_thumb(np & ~1u);
      pc = np;
    } else {
      pc = reg[REG_PC];            /* odd handler: interpreter path */
    }
  }
  if(pc < 0x00004000u)
    return (u8 *)sh4_bios_fallback_entry;
  /* Classic NES / Famicom Mini titles copy executable stubs into VRAM
   * (e.g. 0x06000080) and branch there. Interpret VRAM execution instead of
   * trapping as a wild jump; avoids allocating a VRAM tag table or SMC hooks. */
  if((pc >> 24) == 0x06u)
    return (u8 *)sh4_cold_interp_entry;
  if(cgba_sh4_iwram_page_quarantined(pc))
    return (u8 *)sh4_cold_interp_entry;
#endif
  cgba_wj_note(pc);
#ifdef SH4_ARCH
  cgba_cold_gate_enable = 1;
#endif
  for (i = 0; i < 4; i++) {
    u8 *ret = block_lookup_translate_arm(pc);
#ifdef SH4_ARCH
    if (cgba_cold_pending) {
      cgba_cold_gate_enable = 0;
      return NULL;                /* stub: interpret a cold chunk */
    }
#endif
    if (ret) {
#ifdef SH4_ARCH
      /* An EXECUTED branch resolved to the untranslatable-address sentinel:
       * jumping to it would fault at a random host address. Fail loudly with
       * the guest target instead (a wild guest branch is a codegen bug). */
      if (ret == (u8 *)(~(uintptr_t)0))
        cgba_sh4_wild_jump(pc);
#endif
#ifdef SH4_ARCH
      cgba_cold_gate_enable = 0;
#endif
      translate_icache_sync();
      return ret;
    }
  }

#ifdef SH4_ARCH
  cgba_cold_gate_enable = 0;
#endif
  printf("bad jump %x (%x)\n", pc, reg[REG_PC]);
  fflush(stdout);
  return NULL;
}

u8 function_cc *block_lookup_address_thumb(u32 pc)
{
  unsigned i;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_lookup_thumb_count++;
#endif
#ifdef SH4_ARCH
  reg[REG_PC] = pc;   /* see block_lookup_address_arm: commit PC for indirect/BX */
  if(pc < 0x00004000u)
    return (u8 *)sh4_bios_fallback_entry;
  /* Classic NES / Famicom Mini titles copy executable stubs into VRAM
   * (e.g. 0x06000080) and branch there. Interpret VRAM execution instead of
   * trapping as a wild jump; avoids allocating a VRAM tag table or SMC hooks. */
  if((pc >> 24) == 0x06u)
    return (u8 *)sh4_cold_interp_entry;
  if(cgba_sh4_iwram_page_quarantined(pc))
    return (u8 *)sh4_cold_interp_entry;
#endif
  cgba_wj_note(pc);
#ifdef SH4_ARCH
  cgba_cold_gate_enable = 1;
#endif
  for (i = 0; i < 4; i++) {
    u8 *ret = block_lookup_translate_thumb(pc);
#ifdef SH4_ARCH
    if (cgba_cold_pending) {
      cgba_cold_gate_enable = 0;
      return NULL;                /* stub: interpret a cold chunk */
    }
#endif
    if (ret) {
#ifdef SH4_ARCH
      if (ret == (u8 *)(~(uintptr_t)0))   /* see block_lookup_address_arm */
        cgba_sh4_wild_jump(pc);
#endif
#ifdef SH4_ARCH
      cgba_cold_gate_enable = 0;
#endif
      translate_icache_sync();
      return ret;
    }
  }
#ifdef SH4_ARCH
  cgba_cold_gate_enable = 0;
#endif
  printf("bad jump %x (%x)\n", pc, reg[REG_PC]);
  fflush(stdout);
  return NULL;
}


// Potential exit point: If the rd field is pc for instructions is 0x0F,
// the instruction is b/bl/bx, or the instruction is ldm with PC in the
// register list.
// All instructions with upper 3 bits less than 100b have an rd field
// except bx, where the bits must be 0xF there anyway, multiplies,
// which cannot have 0xF in the corresponding fields, and msr, which
// has 0x0F there but doesn't end things (therefore must be special
// checked against). Because MSR and BX overlap both are checked for.

#define arm_exit_point                                                        \
 (((opcode < 0x8000000) && ((opcode & 0x000F000) == 0x000F000) &&             \
  ((opcode & 0xDB0F000) != 0x120F000)) ||                                     \
  ((opcode & 0x12FFF10) == 0x12FFF10) ||                                      \
  ((opcode & 0x8108000) == 0x8108000) ||                                      \
  ((opcode >= 0xA000000) && (opcode < 0xF000000)) ||                          \
  ((opcode >= 0xF000000) && (!is_div_swi((opcode >> 16) & 0xFF))))            \

#define arm_opcode_branch                                                     \
  ((opcode & 0xE000000) == 0xA000000)                                         \

#define arm_opcode_swi                                                        \
  ((opcode & 0xF000000) == 0xF000000)                                         \

#define arm_opcode_unconditional_branch                                       \
  (condition == 0x0E)                                                         \

/* SH4 gate elision: 1 only when the just-scanned exit point's EMITTED code
 * provably ends with a terminal jump (both branch_exit legs jump, dispatch
 * far-jumps, SWI ends in branch_exit). The arm_exit_point 0xA-0xE range also
 * matches coprocessor space, and class-1 pc-writers are heterogeneous — those
 * keep their end-of-block gate. Thumb exit points all emit terminals. */
#define arm_scan_terminal_emitted                                             \
  (arm_opcode_branch || arm_opcode_swi ||                                     \
   ((opcode & 0x12FFF10) == 0x12FFF10) ||       /* BX */                      \
   ((opcode & 0x8108000) == 0x8108000))         /* LDM {...,pc} */            \

#define thumb_scan_terminal_emitted 1

#define arm_load_opcode()                                                     \
  opcode = readaddress32(pc_address_block, (block_end_pc & 0x0FFF));          \
  condition = opcode >> 28;                                                   \
                                                                              \
  opcode &= 0xFFFFFFF;                                                        \
                                                                              \
  block_end_pc += 4                                                           \

#define arm_branch_target()                                                   \
  branch_target = (block_end_pc + 4 + (((s32)(opcode & 0xFFFFFF) << 8) >> 6)) \

// Contiguous conditional block flags modification - it will set 0x20 in the
// condition's bits if this instruction modifies flags. Taken from the CPU
// switch so it'd better be right this time.

#define arm_set_condition(_condition)                                         \
  block_data[block_data_position].condition = _condition;                     \
  switch((opcode >> 20) & 0xFF)                                               \
  {                                                                           \
    case 0x01:                                                                \
    case 0x03:                                                                \
    case 0x09:                                                                \
    case 0x0B:                                                                \
    case 0x0D:                                                                \
    case 0x0F:                                                                \
      if((((opcode >> 5) & 0x03) == 0) || ((opcode & 0x90) != 0x90))          \
        block_data[block_data_position].condition |= 0x20;                    \
      break;                                                                  \
                                                                              \
    case 0x05:                                                                \
    case 0x07:                                                                \
    case 0x11:                                                                \
    case 0x13:                                                                \
    case 0x15 ... 0x17:                                                       \
    case 0x19:                                                                \
    case 0x1B:                                                                \
    case 0x1D:                                                                \
    case 0x1F:                                                                \
      if((opcode & 0x90) != 0x90)                                             \
        block_data[block_data_position].condition |= 0x20;                    \
      break;                                                                  \
                                                                              \
    case 0x12:                                                                \
      if(((opcode & 0x90) != 0x90) && !(opcode & 0x10))                       \
        block_data[block_data_position].condition |= 0x20;                    \
      break;                                                                  \
                                                                              \
    case 0x21:                                                                \
    case 0x23:                                                                \
    case 0x25:                                                                \
    case 0x27:                                                                \
    case 0x29:                                                                \
    case 0x2B:                                                                \
    case 0x2D:                                                                \
    case 0x2F ... 0x37:                                                       \
    case 0x39:                                                                \
    case 0x3B:                                                                \
    case 0x3D:                                                                \
    case 0x3F:                                                                \
      block_data[block_data_position].condition |= 0x20;                      \
    break;                                                                    \
  }                                                                           \

#define arm_instruction_width 4

#ifdef SH4_ARCH
#define arm_base_cycles()                                                     \
  cycle_count += ws_cyc_seq[(pc >> 24) & 0x0F][1]
#else
#define arm_base_cycles()                                                     \
  cycle_count += def_seq_cycles[pc >> 24][1]
#endif

#if defined(SH4_ARCH) && !defined(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)

/* Bottom-up ARM flag liveness, identical to thumb_dead_flag_eliminate: start
 * all-live at the block end, mask each instruction's may-modify set by the
 * flags still needed below it, kill what it must define, add what it uses.
 * The translate loop then reloads flag_status per instruction
 * (arm_load_flag_status) instead of the historical constant 0xF. */
#define arm_dead_flag_eliminate()                                             \
{                                                                             \
  u32 needed_mask = 0xFF;                                                     \
  while(--block_data_position >= 0)                                           \
  {                                                                           \
    flag_status = block_data[block_data_position].flag_data;                  \
    block_data[block_data_position].flag_data =                               \
     (flag_status & needed_mask);                                             \
    needed_mask &= ~((flag_status >> 4) & 0x0F);                              \
    needed_mask |= flag_status >> 8;                                          \
  }                                                                           \
}

#else

// For now this just sets a variable that says flags should always be
// computed.

#define arm_dead_flag_eliminate()                                             \
  flag_status = 0xF                                                           \

#endif

// The following Thumb instructions can exit:
// b, bl, bx, swi, pop {... pc}, and mov pc, ..., the latter being a hireg
// op only. Rather simpler to identify than the ARM set.

#define thumb_exit_point                                                      \
  (__builtin_expect((cgba_thumb_scan_fl & 0x8000u) != 0u, 0))

#define thumb_opcode_branch                                                   \
  (((opcode >= 0xD000) && (opcode < 0xDF00)) ||                               \
   ((opcode >= 0xE000) && (opcode < 0xE800)) ||                               \
   (opcode >= 0xF800))                                                        \

#define thumb_opcode_swi                                                      \
  ((opcode & 0xFF00) == 0xDF00)                                               \

#define thumb_opcode_unconditional_branch                                     \
  ((opcode < 0xD000) || (opcode >= 0xDF00))                                   \

#define thumb_load_opcode()                                                   \
  last_opcode = opcode;                                                       \
  opcode = readaddress16(pc_address_block, (block_end_pc & 0x0FFF));          \
                                                                              \
  block_end_pc += 2                                                           \

#define thumb_branch_target()                                                 \
  if(opcode < 0xE000)                                                         \
  {                                                                           \
    branch_target = block_end_pc + 2 + ((s8)(opcode & 0xFF) * 2);             \
  }                                                                           \
  else                                                                        \
                                                                              \
  if(opcode < 0xF800)                                                         \
  {                                                                           \
    branch_target = block_end_pc + 2 + ((s32)((opcode & 0x7FF) << 21) >> 20); \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    if((last_opcode >= 0xF000) && (last_opcode < 0xF800))                     \
    {                                                                         \
      branch_target =                                                         \
       (block_end_pc + ((s32)((last_opcode & 0x07FF) << 21) >> 9) +           \
       ((opcode & 0x07FF) * 2));                                              \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      goto no_direct_branch;                                                  \
    }                                                                         \
  }                                                                           \

#define thumb_set_condition(_condition)                                       \

#define thumb_instruction_width 2

#ifdef SH4_ARCH
#define thumb_base_cycles()                                                   \
  cycle_count += ws_cyc_seq[(pc >> 24) & 0x0F][0]
#else
#define thumb_base_cycles()                                                   \
  cycle_count += def_seq_cycles[pc >> 24][0]
#endif

// Here's how this works: each instruction has three different sets of flag
// attributes, each consisiting of a 4bit mask describing how that instruction
// interacts with the 4 main flags (N/Z/C/V).
// The first set, in bits 0:3, is the set of flags the instruction may
// modify. After this pass this is changed to the set of flags the instruction
// should modify - if the bit for the corresponding flag is not set then code
// does not have to be generated to calculate the flag for that instruction.

// The second set, in bits 7:4, is the set of flags that the instruction must
// modify (ie, for shifts by the register values the instruction may not
// always modify the C flag, and thus the C bit won't be set here).

// The third set, in bits 11:8, is the set of flags that the instruction uses
// in its computation, or the set of flags that will be needed after the
// instruction is done. For any instructions that change the PC all of the
// bits should be set because it is (for now) unknown what flags will be
// needed after it arrives at its destination. Instructions that use the
// carry flag as input will have it set as well.

// The algorithm is a simple liveness analysis procedure: It starts at the
// bottom of the instruction stream and sets a "currently needed" mask to
// the flags needed mask of the current instruction. Then it moves down
// an instruction, ANDs that instructions "should generate" mask by the
// "currently needed" mask, then ANDs the "currently needed" mask by
// the 1's complement of the instruction's "must generate" mask, and ORs
// the "currently needed" mask by the instruction's "flags needed" mask.

#if defined(SH4_ARCH) && defined(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)

/* Exact-cycle diff builds compare reg[REG_CPSR] at every instruction
   boundary, where a legitimately dead flag skip reads as a false mismatch:
   keep every may-flag generated (emitters self-mask to what they write). */
#define thumb_dead_flag_eliminate()                                           \
{                                                                             \
  while(--block_data_position >= 0)                                           \
    block_data[block_data_position].flag_data |= 0x0F;                        \
}

#else

#define thumb_dead_flag_eliminate()                                           \
{                                                                             \
  u32 needed_mask = 0xff;                                                     \
                                                                              \
  while(--block_data_position >= 0)                                           \
  {                                                                           \
    flag_status = block_data[block_data_position].flag_data;                  \
    block_data[block_data_position].flag_data =                               \
     (flag_status & needed_mask);                                             \
    needed_mask &= ~((flag_status >> 4) & 0x0F);                              \
    needed_mask |= flag_status >> 8;                                          \
  }                                                                           \
}                                                                             \

#endif

#define MAX_BLOCK_SIZE   1024   // 2/4KiB blocks max
#define MAX_EXITS          32   // This covers 99% blocks

#ifdef SH4_ARCH
#ifndef CGBA_SH4_RAM_SCAN_CAP_INITIAL
#define CGBA_SH4_RAM_SCAN_CAP_INITIAL MAX_BLOCK_SIZE
#endif
#ifndef CGBA_SH4_ROM_SCAN_CAP_INITIAL
#define CGBA_SH4_ROM_SCAN_CAP_INITIAL MAX_BLOCK_SIZE
#endif
#if CGBA_SH4_RAM_SCAN_CAP_INITIAL < 32 || \
    CGBA_SH4_RAM_SCAN_CAP_INITIAL > MAX_BLOCK_SIZE
#error "CGBA_SH4_RAM_SCAN_CAP_INITIAL must be in [32, MAX_BLOCK_SIZE]"
#endif
#if CGBA_SH4_ROM_SCAN_CAP_INITIAL < 32 || \
    CGBA_SH4_ROM_SCAN_CAP_INITIAL > MAX_BLOCK_SIZE
#error "CGBA_SH4_ROM_SCAN_CAP_INITIAL must be in [32, MAX_BLOCK_SIZE]"
#endif

/* Adaptive per-cache scan caps: a single block whose EMITTED code exceeds the
 * whole freshly-flushed translation cache would flush+retranslate forever — the
 * "went too far, pedal out to the beginning" retry assumes a block always
 * fits an empty cache. That assumption fails on this port: SimCity 2000
 * copies a 724-instruction LDM/STM-heavy renderer into IWRAM whose SH4
 * emission (~480 bytes/insn worst case) can never fit the RAM cache, so the
 * JIT livelocked (emulator) or spun until the guest went wild (hardware,
 * EXC=040 crash report). When an oversized attempt overflows, halve the cap
 * so the retry ends the block early through the fall-through translation
 * gate (a legal "size-limit" block end, same as the MAX_BLOCK_SIZE stop).
 * Sticky for the session within its RAM/ROM cache; reset on
 * init_dynarec_caches (ROM load/reset).  Keeping the domains separate avoids
 * shortening ordinary ROM blocks after one pathological copied-RAM routine.
 *
 * The shrink keys off the bytes THIS attempt emitted, not off starting at
 * the cache base: the oversized block is usually reached recursively while
 * resolving another block's external exits, so its attempts never begin in
 * an empty cache. An overflowing attempt that emitted more than a quarter of
 * its cache halves that domain's cap; repeated retries eventually reach the
 * 32-instruction floor while leaving room for the rest of the working set. */
static u32 cgba_ram_block_scan_cap = CGBA_SH4_RAM_SCAN_CAP_INITIAL;
static u32 cgba_rom_block_scan_cap = CGBA_SH4_ROM_SCAN_CAP_INITIAL;
#else
/* Preserve the upstream cross-backend behavior outside the SH4 port. */
static u32 cgba_block_scan_cap = MAX_BLOCK_SIZE;
#endif

/* DIAG bisect knob: -DCGBA_BLOCK_SCAN_CAP_DISABLE reverts scan_block to the
 * fixed MAX_BLOCK_SIZE stop (cap machinery inert) to isolate the cap. */
#ifdef CGBA_BLOCK_SCAN_CAP_DISABLE
#define CGBA_BLOCK_SCAN_CAP_EXPR ((u32)MAX_BLOCK_SIZE)
#elif defined(SH4_ARCH)
#define CGBA_BLOCK_SCAN_CAP_EXPR \
  (ram_region ? cgba_ram_block_scan_cap : cgba_rom_block_scan_cap)
#else
#define CGBA_BLOCK_SCAN_CAP_EXPR cgba_block_scan_cap
#endif

static void cgba_block_overflow_shrink(u8 *attempt_base, u8 *attempt_end,
  bool ram_region)
{
#ifdef SH4_ARCH
  u32 cache_size = ram_region ? RAM_TRANSLATION_CACHE_SIZE
                              : ROM_TRANSLATION_CACHE_SIZE;
  if((u32)(attempt_end - attempt_base) > cache_size / 4)
    cgba_sh4_scan_caps_shrink_domain(&cgba_ram_block_scan_cap,
      &cgba_rom_block_scan_cap, ram_region);
#else
  u32 cache_size = ram_region ? RAM_TRANSLATION_CACHE_SIZE
                              : ROM_TRANSLATION_CACHE_SIZE;
  if((u32)(attempt_end - attempt_base) >
       cache_size / 4 && cgba_block_scan_cap > 32)
    cgba_block_scan_cap >>= 1;
#endif
}


block_data_type block_data[MAX_BLOCK_SIZE];
block_exit_type block_exits[MAX_EXITS];
#ifdef SH4_ARCH
static u32 cgba_block_exit_pc[MAX_EXITS];
#define cgba_record_exit_pc(idx, epc) (cgba_block_exit_pc[(idx)] = (epc))
#else
#define cgba_record_exit_pc(idx, epc) ((void)0)
#endif

/* DIAG: with -DCGBA_DIAG_SINGLE_INSN, force one-instruction blocks ONLY for the
 * divergent BIOS function-entry block (start PC == CGBA_DIAG_BLK_PC, default
 * 0xB5C) while the block-diff harness is active (cgba_dynarec_single_block), so
 * the diff resolves to the single mistranslated opcode -- without splitting the
 * Thumb game code (BL artifact) or per-instruction-ing the whole BIOS LZ77 loop
 * (which runs thousands of times). 0 (normal blocks) in non-diag builds. */
#if defined(CGBA_DIAG_SINGLE_INSN)
extern int cgba_dynarec_single_block;
#ifndef CGBA_DIAG_BLK_PC
#define CGBA_DIAG_BLK_PC 0xB5Cu
#endif
#define CGBA_DIAG_ONE_INSN_BLOCK(start) \
  (CGBA_SH4_DIFF_ACTIVE() && (start) >= 0xB5Cu && (start) < 0xC10u)
#else
#define CGBA_DIAG_ONE_INSN_BLOCK(start) ((void)(start), 0)
#endif

#define smc_write_arm_yes() {                                                 \
  u8 *tag_base = (block_end_pc < 0x03000000u) ? ewram + 0x40000 : iwram;      \
  u32 tag_off = (block_end_pc < 0x03000000u) ?                               \
    (block_end_pc & 0x3FFFFu) : (block_end_pc & 0x7FFFu);                     \
  if(address32(tag_base, tag_off) == 0)                                       \
  {                                                                           \
    address32(tag_base, tag_off) = CODE_TAG_BLOCK32;                          \
  }                                                                           \
  if (block_end_pc >= 0x03000000u) {                                          \
    if (tag_off < iwram_code_min) iwram_code_min = tag_off;                   \
    if ((tag_off + 4u) > cgba_iwram_code_end)                                 \
      cgba_iwram_code_end = tag_off + 4u;                                     \
  } else {                                                                    \
    if (tag_off < ewram_code_min) ewram_code_min = tag_off;                   \
    if ((tag_off + 4u) > cgba_ewram_code_end)                                 \
      cgba_ewram_code_end = tag_off + 4u;                                     \
  }                                                                           \
}

#define smc_write_thumb_yes() {                                               \
  u8 *tag_base = (block_end_pc < 0x03000000u) ? ewram + 0x40000 : iwram;      \
  u32 tag_off = (block_end_pc < 0x03000000u) ?                               \
    (block_end_pc & 0x3FFFFu) : (block_end_pc & 0x7FFFu);                     \
  if(address16(tag_base, tag_off) == 0)                                       \
  {                                                                           \
    address16(tag_base, tag_off) = CODE_TAG_BLOCK16;                          \
  }                                                                           \
  if (block_end_pc >= 0x03000000u) {                                          \
    if (tag_off < iwram_code_min) iwram_code_min = tag_off;                   \
    if ((tag_off + 2u) > cgba_iwram_code_end)                                 \
      cgba_iwram_code_end = tag_off + 2u;                                     \
  } else {                                                                    \
    if (tag_off < ewram_code_min) ewram_code_min = tag_off;                   \
    if ((tag_off + 2u) > cgba_ewram_code_end)                                 \
      cgba_ewram_code_end = tag_off + 2u;                                     \
  }                                                                           \
}

#if defined(SH4_ARCH) && CGBA_SH4_THUMB_CHAIN_EXISTING
/* In RAM (especially IWRAM 3D engines with many branch/call-return entries into
 * large ARM functions), chaining a scan onto an already-translated RAM block
 * prevents O(N^2) tail duplication from overflowing the 512 KiB RAM cache. */
#define cgba_scan_existing_arm()                                              \
  if (ram_region && !CGBA_SH4_DIFF_ACTIVE() &&                                \
      (block_end_pc >> 24) == (cgba_blk_start >> 24) &&                       \
      (idle_loop_target_pc == 0xFFFFFFFFu ||                                  \
       (u32)(block_end_pc - (idle_loop_target_pc - 64u)) > 128u)) {           \
    u16 *_tp = (block_end_pc < 0x03000000u)                                   \
      ? (u16 *)(ewram + (block_end_pc & 0x3FFFFu) + 0x40000u)                 \
      : (u16 *)(iwram + (block_end_pc & 0x7FFFu));                            \
    if (VALID_TAG(*_tp)) {                                                    \
      ramtag_type *_te = get_ram_tag(*_tp);                                   \
      if (_te->offset_arm) {                                                  \
        u8 *_existing = ram_translation_cache + _te->offset_arm;              \
        if (_existing != attempt_base) {                                      \
          chain_existing_target = _existing;                                  \
          break;                                                              \
        }                                                                     \
      }                                                                       \
    }                                                                         \
  }
#else
#define cgba_scan_existing_arm()
#endif

#if defined(SH4_ARCH) && CGBA_SH4_THUMB_CHAIN_EXISTING
#define cgba_scan_existing_thumb()                                            \
  if (!ram_region && !CGBA_SH4_DIFF_ACTIVE() &&                               \
      cgba_blk_start >= 0x08000000u &&                                        \
      (opcode & 0xF800u) != 0xF000u &&                                        \
      (idle_loop_target_pc == 0xFFFFFFFFu ||                                  \
       (u32)(block_end_pc - (idle_loop_target_pc - 64u)) > 128u)) {           \
    u8 *_existing = cgba_rom_hash_lookup_thumb(block_end_pc);                 \
    if (_existing != NULL && _existing != attempt_base) {                     \
      chain_existing_target = _existing;                                      \
      break;                                                                  \
    }                                                                         \
  }
#else
#define cgba_scan_existing_thumb()
#endif

#define scan_block(type)                                                      \
{                                                                             \
  __label__ block_end;                                                        \
  u32 cgba_blk_start = block_end_pc;  /* DIAG: block start PC */              \
  u32 cgba_eff_cap = CGBA_BLOCK_SCAN_CAP_EXPR;                                \
  const u32 cgba_n_gates = translation_gate_targets;                          \
  if (CGBA_DIAG_ONE_INSN_BLOCK(cgba_blk_start)) {                             \
    cgba_eff_cap = 1u;                                                        \
  } else {                                                                    \
    unsigned _dom = cgba_sh4_jit_exec_domain(cgba_blk_start);                 \
    u32 _dom_lim = (_dom == 4u) ? 0x0E000000u :                               \
                   (_dom == 3u) ? 0x04000000u :                               \
                   (_dom == 2u) ? 0x03000000u :                               \
                   (_dom == 1u) ? 0x00004000u : 0u;                           \
    if (_dom_lim <= cgba_blk_start) {                                         \
      cgba_eff_cap = 1u;                                                      \
    } else {                                                                  \
      u32 _dom_n = (_dom_lim - cgba_blk_start) / type##_instruction_width;    \
      if (_dom_n < cgba_eff_cap) cgba_eff_cap = _dom_n;                       \
    }                                                                         \
    if (__builtin_expect(ram_region, 0)) {                                    \
      cgba_eff_cap = cgba_sh4_iwram_quarantine_cap(                           \
        cgba_blk_start, type##_instruction_width, cgba_eff_cap);              \
      /* Cap inside the active IWRAM (32 KiB) or EWRAM (256 KiB) mirror so a  \
       * scan stops before the 16-byte tail (e.g. IWRAM 0x7FF0..0x7FFF BIOS   \
       * IRQ vector/flags) and never wraps past the mirror boundary. */       \
      u32 _mask = (_dom == 3u) ? 0x7FFFu : 0x3FFFFu;                          \
      u32 _off  = cgba_blk_start & _mask;                                     \
      u32 _stop = (_off < (_mask - 15u)) ? (_mask - 15u) : (_mask + 1u);      \
      u32 _rn   = (_stop - _off) / type##_instruction_width;                  \
      if (_rn < cgba_eff_cap) cgba_eff_cap = _rn;                             \
    }                                                                         \
  }                                                                           \
  /* Find the end of the block */                                             \
  do                                                                          \
  {                                                                           \
    check_pc_region(block_end_pc);                                            \
    if (__builtin_expect(ram_region, 0)) {                                    \
      smc_write_##type##_yes();                                               \
    }                                                                         \
    type##_load_opcode();                                                     \
    type##_flag_status();                                                     \
    /* Initialize here, NOT at the loop bottom: exit-point breaks skip the   \
       bottom, and a stale 1 from a previous translation at this index       \
       emits a spurious (history-dependent) cycle gate at the final          \
       instruction. The branch-target seam pass sets real 1s after the scan. */ \
    block_data[block_data_position].update_cycles = 0;                        \
                                                                              \
    if(type##_exit_point)                                                     \
    {                                                                         \
      /* Branch/branch with link */                                           \
      if(type##_opcode_branch)                                                \
      {                                                                       \
        __label__ no_direct_branch;                                           \
        type##_branch_target();                                               \
        block_exits[block_exit_position].branch_target = branch_target;       \
        cgba_record_exit_pc(block_exit_position,                              \
                            block_end_pc - type##_instruction_width);         \
        block_exit_position++;                                                \
                                                                              \
        /* Give the branch target macro somewhere to bail if it turns out to  \
           be an indirect branch (ala malformed Thumb bl) */                  \
        no_direct_branch:;                                                    \
      }                                                                       \
                                                                              \
      /* SWI branches to the BIOS, unless it's an HLE call, then it is        \
         not parsed as an exit_point but rather an "instruction" of sorts. */ \
      if(type##_opcode_swi)                                                   \
      {                                                                       \
        block_exits[block_exit_position].branch_target = 0x00000008;          \
        cgba_record_exit_pc(block_exit_position,                              \
                            block_end_pc - type##_instruction_width);         \
        block_exit_position++;                                                \
      }                                                                       \
                                                                              \
      type##_set_condition(condition | 0x10);                                 \
                                                                              \
      /* Only unconditional branches can end the block. */                    \
      if(type##_opcode_unconditional_branch)                                  \
      {                                                                       \
        /* Check to see if any prior block exits branch after here,           \
           if so don't end the block. Starts from the top and works           \
           down because the most recent branch is most likely to              \
           join after the end (if/then form) */                               \
        for(i = block_exit_position - 2; i >= 0; i--)                         \
        {                                                                     \
          if(block_exits[i].branch_target == block_end_pc)                    \
            break;                                                            \
        }                                                                     \
                                                                              \
        if(i < 0)                                                             \
        {                                                                     \
          /* Nothing branches to block_end_pc and the last instruction is an  \
             unconditional branch: the end-of-block translation gate would be \
             unreachable — IF this opcode class emits a terminal jump. */     \
          ended_uncond = type##_scan_terminal_emitted;                        \
          break;                                                              \
        }                                                                     \
      }                                                                       \
      if(block_exit_position == MAX_EXITS)                                    \
        break;                                                                \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      type##_set_condition(condition);                                        \
    }                                                                         \
                                                                              \
    if (__builtin_expect(cgba_n_gates != 0, 0))                               \
    {                                                                         \
      for(i = 0; (u32)i < cgba_n_gates; i++)                                  \
      {                                                                       \
        if(block_end_pc == translation_gate_target_pc[i])                     \
          goto block_end;                                                     \
      }                                                                       \
    }                                                                         \
                                                                              \
    block_data_position++;                                                    \
    cgba_scan_existing_##type();                                              \
    if((u32)block_data_position >= cgba_eff_cap)                              \
    {                                                                         \
      break;                                                                  \
    }                                                                         \
  } while(1);                                                                 \
                                                                              \
  block_end:;                                                                 \
}                                                                             \

#define arm_fix_pc()                                                          \
  pc &= ~0x03                                                                 \

#define thumb_fix_pc()                                                        \
  pc &= ~0x01                                                                 \

bool translate_block_arm(u32 pc, bool ram_region)
{
  u32 ended_uncond = 0;
  u32 opcode = 0;
  u32 last_opcode;
  u32 condition;
  u32 last_condition;
  u32 pc_region = (pc >> 12);
  u32 new_pc_region;
  u8 *pc_address_block = cgba_memory_map_read_4k(pc);
  u32 block_start_pc = pc;
  u32 block_end_pc = pc;
  u32 block_exit_position = 0;
  s32 block_data_position = 0;
  u32 external_block_exit_position = 0;
  u32 branch_target;
  u32 cycle_count = 0;
  u8 *translation_target;
  u8 *backpatch_address = NULL;
  u8 *translation_ptr = NULL;
  u8 *translation_cache_limit = NULL;
  u8 *attempt_base = NULL;
#ifdef SH4_ARCH
  u8 *chain_existing_target = NULL;
#endif
  s32 i;
  u32 flag_status;
  block_exit_type external_block_exits[MAX_EXITS];
  generate_block_extra_vars_arm();

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_arm_translate_count++;
#endif

  arm_fix_pc();

  if(!pc_address_block)
    return false;

  if (ram_region) {
    translation_ptr = ram_translation_ptr;
    translation_cache_limit = &ram_translation_cache[
       RAM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD
       - (0x10000 - ram_block_tag) / 2 * sizeof(ramtag_type)];
  } else {
    translation_ptr = rom_translation_ptr;
#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
    translation_cache_limit = cgba_sh4_fm_ool_rom_limit(TRANSLATION_CACHE_LIMIT_THRESHOLD);
#else
    translation_cache_limit =
     rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE -
     TRANSLATION_CACHE_LIMIT_THRESHOLD;
#endif
  }
  attempt_base = translation_ptr;

#ifdef SH4_ARCH
  sh4g_litseg_begin();
#endif
  generate_block_prologue();

  /* This is a function because it's used a lot more than it might seem (all
     of the data processing functions can access it), and its expansion was
     massacreing the compiler. */

  scan_block(arm);

#ifdef SH4_ARCH
  s32 num_insns = block_data_position;
  for(i = 0; i < num_insns; i++)
    block_data[i].raw_flag_data = block_data[i].flag_data;
#endif

  for(i = 0; i < block_exit_position; i++)
  {
    branch_target = block_exits[i].branch_target;

#ifdef SH4_ARCH
    if((branch_target >= block_start_pc) &&
     (branch_target < block_end_pc))
#else
    if((branch_target > block_start_pc) &&
     (branch_target < block_end_pc))
#endif
    {
      u32 seam_idx = (branch_target - block_start_pc) /
                     arm_instruction_width;
#ifdef SH4_ARCH
      block_data[seam_idx].body_offset = NULL;
      if (branch_target <= cgba_block_exit_pc[i] || cgba_dynarec_single_block) {
        block_data[seam_idx].update_cycles |= 1u;
        /* Backward/loop seam: the flush+gate at this seam can take an IRQ
           with CPSR architecturally observable — keep flags live across it. */
        block_data[seam_idx].flag_data |= 0xF00;
      } else {
        /* Forward-only join: syncs R13 without a loop-break IRQ gate. */
        block_data[seam_idx].update_cycles |= 2u;
      }
#else
      block_data[seam_idx].update_cycles = 1;
#endif
    }
  }

  arm_dead_flag_eliminate();

  block_exit_position = 0;
  block_data_position = 0;

  last_condition = 0x0E;
#ifdef SH4_ARCH
  sh4_greg_forward_reset();
#endif

  while(pc != block_end_pc)
  {
#ifdef SH4_ARCH
    /* Block-entry (branch target). Force-close any open ARM conditional block so
     * the flush/debit is UNCONDITIONAL (fall-through must always charge+zero the
     * pre-entry cycles, even when the surrounding condition is false). */
    if(block_data[block_data_position].update_cycles)
    {
      if((last_condition & 0x0F) != 0x0E)
      {
        generate_branch_patch_conditional(backpatch_address, translation_ptr);
        last_condition = 0x0E;
      }
      if (block_data[block_data_position].update_cycles & 1u) {
        generate_cycle_update();
        block_data[block_data_position].block_offset = translation_ptr;
        generate_prof_block_entry(0);
        generate_cycle_gate(1);
#if defined(CGBA_SH4_BLOCK_PROFILE) && CGBA_SH4_BLOCK_PROFILE
        block_data[block_data_position].body_offset =
          block_data[block_data_position].block_offset;
#else
        block_data[block_data_position].body_offset = translation_ptr;
#endif
      } else {
        /* Forward-only join: debit accumulated cycles without an IRQ gate. */
        if (cycle_count != 0) {
          sh4g_cycle_debit(&translation_ptr, (int)cycle_count);
          cycle_count = 0;
        }
        block_data[block_data_position].block_offset = translation_ptr;
        block_data[block_data_position].body_offset = translation_ptr;
      }
    }
    else
    {
      block_data[block_data_position].block_offset = translation_ptr;
      block_data[block_data_position].body_offset = translation_ptr;
      if(block_data_position == 0)
        generate_prof_block_entry(0);
    }
#else
    block_data[block_data_position].block_offset = translation_ptr;
#endif
    /* Backend hook at an instruction boundary, before this instruction's
     * own fetch cycle joins the translate-time count. */
    arm_instruction_prefix();
    arm_base_cycles();

    if (pc == cheat_master_hook)
    {
      arm_process_cheats();
    }

#ifdef SH4_ARCH
    if (block_data_position == 0 || block_data[block_data_position].update_cycles)
      sh4_greg_forward_reset();
    {
      int arm_fused = 0;
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
      if (!cgba_dynarec_single_block &&
          (last_condition & 0x0F) == 0x0E &&
          block_data_position + 1 < num_insns &&
          !block_data[block_data_position + 1].update_cycles &&
          (pc + 4) != cheat_master_hook &&
          ((pc + 4) >> 12) == pc_region
#ifdef CGBA_SH4_M4A_MIX_SKIP
          && !sh4_m4a_skip_active()
#endif
          ) {
        u32 cur_op = readaddress32(pc_address_block, pc & 0x0FFF);
        u32 next_op = readaddress32(pc_address_block, (pc + 4) & 0x0FFF);
        u32 next_cond = next_op >> 28;
        if ((cur_op >> 28) == 0xEu &&
            (next_op & 0x0F000000u) == 0x0A000000u &&
            next_cond < 14u) {
          u32 b_target = block_exits[block_exit_position].branch_target;
          u32 save_pc = pc;
          pc += 4;
          int is_idle = SH4_IDLE_BRANCH(b_target);
          pc = save_pc;
          if (!is_idle) {
            int fkind = -1;
            unsigned f_rn = (cur_op >> 16) & 0xFu;
            unsigned f_rd = (cur_op >> 12) & 0xFu;
            int f_is_imm = (cur_op >> 25) & 1;
            u32 f_imm = 0;
            unsigned f_rm = cur_op & 0xFu;
            u32 f_op = (cur_op >> 21) & 0xFu;
            int f_s = (cur_op >> 20) & 1;
            int op2_ok = 0;
            if ((cur_op & 0x0C000000u) == 0u && f_s && f_rn != 15u) {
              if (f_is_imm) {
                u32 rot = ((cur_op >> 8) & 0xFu) * 2u;
                u32 v = cur_op & 0xFFu;
                f_imm = rot ? ((v >> rot) | (v << (32u - rot))) : v;
                op2_ok = 1;
              } else if ((cur_op & 0x00000FF0u) == 0u && f_rm != 15u) {
                op2_ok = 1;
              }
            }
            if (op2_ok) {
              if (f_op == 0xAu && (next_cond <= 3u || next_cond >= 8u))
                fkind = 0;                                 /* CMP */
              else if (f_op == 0x8u && next_cond <= 1u &&
                       (!f_is_imm || ((cur_op & 0xF00u) == 0u)))
                fkind = 1;                                 /* TST (NZ only) */
              else if (f_op == 0x2u && f_rd != 15u &&
                       (next_cond <= 1u || next_cond == 4u || next_cond == 5u))
                fkind = 2;                                 /* SUBS */
              else if (f_op == 0x4u && f_rd != 15u &&
                       (next_cond <= 1u || next_cond == 4u || next_cond == 5u))
                fkind = 3;                                 /* ADDS */
            }
            if (fkind >= 0) {
              u32 need_kill = (fkind == 1) ? 0xCu : 0xFu;
              int is_internal = (b_target >= block_start_pc &&
                                 b_target < block_end_pc);
              s32 tgt_idx = is_internal
                ? (s32)((b_target - block_start_pc) / arm_instruction_width)
                : -1;
              int taken_dead = is_internal
                ? sh4_flags_dead_from(block_data, tgt_idx, num_insns, need_kill)
                : 0;
              s32 fall_idx = block_data_position + 2;
              int fall_dead = (fall_idx < num_insns &&
                               !(block_data[fall_idx].update_cycles & 1u))
                ? sh4_flags_dead_from(block_data, fall_idx, num_insns, need_kill)
                : 0;
              if (taken_dead || fall_dead) {
                int taken_debit;
                int internal_mode = 0;
                u8 *known_target = NULL;
                if (fkind == 0 && f_is_imm && f_imm == 0u &&
                    (next_cond <= 1u || next_cond >= 0xAu))
                  fkind = 4;                               /* CMP a, #0 -> TST/PZ/PL */
                sh4g_litseg_reserve(&translation_ptr, 6);
                { sh4_codegen cg = sh4g_open(&translation_ptr);
                  sh4_emit_load_greg(&cg, f_rn, SH4_REG_T0);
                  sh4g_close(&translation_ptr, &cg); }
                if (fkind != 4) {
                  if (f_is_imm)
                    sh4g_const(&translation_ptr, f_imm, SH4_REG_T1);
                  else {
                    sh4_codegen cg = sh4g_open(&translation_ptr);
                    sh4_emit_load_greg(&cg, f_rm, SH4_REG_T1);
                    sh4g_close(&translation_ptr, &cg);
                  }
                }
                pc += 4;
                arm_base_cycles();                         /* Bcc fetch cycle */
                taken_debit = (int)cycle_count +
                  (int)ws_cyc_nseq[(b_target >> 24) & 0x0Fu][1];
                if (is_internal) {
                  if (b_target > pc) {
                    internal_mode = ((b_target - pc) <= 48u) ? 3 : 2;
                  } else {
                    internal_mode = 1;
                    if (tgt_idx <= (s32)block_data_position)
                      known_target = block_data[tgt_idx].body_offset;
                  }
                }
                block_exits[block_exit_position].branch_source =
                  sh4g_emit_fused_cmp_bcc_known(
                    &translation_ptr, fkind, f_rd, next_cond,
                    taken_debit, b_target, internal_mode,
                    SH4_DEFAULT_BRANCH_EXIT(b_target),
                    taken_dead, fall_dead, 0, known_target);
                block_exit_position++;
                block_data_position++;
                pc += 4;
                sh4_greg_forward_reset();
                sh4g_litseg_maybe_flush(&translation_ptr);
                arm_fused = 1;
              }
            }
          }
        }
        /* General ARM peephole: MOVS Rd, Rm{, shift} + single conditional
         * ADD/SUB{EQ/NE/MI/PL} Rd, Rd, #imm when flags are dead after op1. */
        if (!arm_fused && (cur_op & 0xFFFF0010u) == 0xE1B00000u &&
            (next_cond == 0u || next_cond == 1u || next_cond == 4u || next_cond == 5u) &&
            (next_op & 0x0E100000u) == 0x02000000u) {
          s32 fall_idx = block_data_position + 2;
          u32 need_kill = (((cur_op >> 7) & 0x1Fu) == 0u && ((cur_op >> 5) & 3u) == 0u)
                          ? 0xCu : 0xEu;
          int fall_dead = (fall_idx < num_insns &&
                           !block_data[fall_idx].update_cycles)
            ? sh4_flags_dead_from(block_data, fall_idx, num_insns, need_kill)
            : 0;
          if (fall_dead) {
            u8 *forward_start = translation_ptr;
            if (sh4g_arm_movs_cond_addsub_native(&translation_ptr, cur_op, next_op)) {
              pc += 4;
              arm_base_cycles();
              block_data_position++;
              pc += 4;
              sh4_greg_forward_finish(forward_start, translation_ptr,
                translation_ptr > forward_start && translation_ptr[-2] == 0x1Eu);
              sh4g_litseg_maybe_flush(&translation_ptr);
              arm_fused = 1;
            }
          }
        }
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
        /* Soundless skip for 1..4 identical stepped interpolated stereo IWRAM
         * PCM mixer units (e.g. Metroid Fusion / IS / Camelot sound drivers). */
        if (!arm_fused && (pc >> 24) == 0x03 &&
            (cur_op & 0xFFFF0070u) == 0xE1A00020u &&
            (next_op & 0xFFF00FF0u) == 0xE1B000D0u) {
          int nunits = 0;
          u32 ops48[48];
          int ccs48[48];
          u8 unit_has_gate[4] = { 0, 0, 0, 0 };
          int unit_pre_debit[4] = { 0, 0, 0, 0 };
          u8 *unit_blk_off[4] = { NULL, NULL, NULL, NULL };
          u8 *unit_body_off[4] = { NULL, NULL, NULL, NULL };
          u32 save_pc = pc;
          u32 save_cc = cycle_count;
          while (nunits < 4) {
            int bi = nunits * 12;
            int k, ok_u = 1;
            if (block_data_position + bi + 11 >= num_insns ||
                ((save_pc + (u32)((bi + 11) * 4)) >> 12) != pc_region)
              break;
            for (k = 0; k < 12; k++) {
              int idx = bi + k;
              if ((save_pc + (u32)(idx * 4)) == cheat_master_hook) {
                ok_u = 0; break;
              }
              if (k > 0 && block_data[block_data_position + idx].update_cycles) {
                ok_u = 0; break;
              }
              u32 rop = readaddress32(pc_address_block, (save_pc + (u32)(idx * 4)) & 0x0FFF);
              if (nunits > 0 && rop != ops48[k]) {
                ok_u = 0; break;
              }
              ops48[idx] = rop;
            }
            if (!ok_u) break;
            if (nunits > 0 && block_data[block_data_position + bi].update_cycles) {
              unit_has_gate[nunits] = 1;
              unit_pre_debit[nunits] = (int)cycle_count;
              cycle_count = 0;
            }
            for (k = 0; k < 12; k++) {
              int idx = bi + k;
              if (idx == 0) {
                ccs48[0] = (int)save_cc;
              } else {
                pc = save_pc + (u32)(idx * 4);
                arm_base_cycles();
                ccs48[idx] = (int)cycle_count;
              }
            }
            nunits++;
          }
          pc = save_pc;
          if (nunits >= 1 &&
              sh4g_arm_interp_rmw_mla_skip(&translation_ptr, ops48, ccs48,
                                           nunits, pc,
                                           unit_has_gate, unit_pre_debit,
                                           unit_blk_off, unit_body_off)) {
            int u;
            int consumed_ops = nunits * 12;
            for (u = 1; u < nunits; u++) {
              int idx = block_data_position + u * 12;
              block_data[idx].block_offset = unit_blk_off[u];
              block_data[idx].body_offset = unit_body_off[u];
            }
            block_data_position += consumed_ops - 1;
            pc += (u32)(consumed_ops * 4);
            sh4_greg_forward_reset();
            sh4g_litseg_maybe_flush(&translation_ptr);
            arm_fused = 1;
          } else {
            cycle_count = save_cc;
          }
        }
#endif
        /* General 4-slot R4..R7 BBRA for straight-line non-S / dead-flag ARM
         * ALU & MUL/MLA runs (2..16 ops, BOTH ROM and IWRAM). Replaces the old
         * same-Rn and same-Rd peepholes with one general register allocator. */
        if (!arm_fused &&
            !(block_data[block_data_position].flag_data & 0xFu) &&
            !(block_data[block_data_position + 1].flag_data & 0xFu) &&
            sh4g_arm_bbra_maybe(cur_op) && sh4g_arm_bbra_maybe(next_op)) {
          u32 rm_arr[16], wm_arr[16];
          if (sh4g_arm_bbra_classify(cur_op, &rm_arr[0], &wm_arr[0]) &&
              sh4g_arm_bbra_classify(next_op, &rm_arr[1], &wm_arr[1])) {
            const u32 unpin = 0x7FFFu
#if CGBA_SH4_PIN_GUEST_R0
              & ~1u
#endif
#if CGBA_SH4_PIN_GUEST_R1
              & ~2u
#endif
              ;
            u32 run_ops[16];
            int rlen = 2;
            u32 t0 = (rm_arr[0] | wm_arr[0]) & unpin;
            u32 t1 = (rm_arr[1] | wm_arr[1]) & unpin;
            u32 seen = t0 | t1;
            u32 dup  = (t0 & t1) | (rm_arr[0] & wm_arr[0] & unpin) |
                       (rm_arr[1] & wm_arr[1] & unpin);
            u32 save_pc = pc;
            u32 save_cc = cycle_count;
            run_ops[0] = cur_op;
            run_ops[1] = next_op;
            while (rlen < 16 &&
                   block_data_position + rlen < num_insns &&
                   !block_data[block_data_position + rlen].update_cycles &&
                   !(block_data[block_data_position + rlen].flag_data & 0xFu) &&
                   (save_pc + (u32)(rlen * 4)) != cheat_master_hook &&
                   ((save_pc + (u32)(rlen * 4)) >> 12) == pc_region) {
              u32 rop = readaddress32(pc_address_block,
                                      (save_pc + (u32)(rlen * 4)) & 0x0FFF);
              u32 tk;
              if (!sh4g_arm_bbra_maybe(rop) ||
                  !sh4g_arm_bbra_classify(rop, &rm_arr[rlen], &wm_arr[rlen]))
                break;
              tk = (rm_arr[rlen] | wm_arr[rlen]) & unpin;
              dup |= (seen & tk) | (rm_arr[rlen] & wm_arr[rlen] & unpin);
              seen |= tk;
              run_ops[rlen++] = rop;
            }
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
            /* Preserve trailing IWRAM stereo PCM mixer step lead-in if present. */
            if (rlen >= 2 && (save_pc >> 24) == 0x03 &&
                (run_ops[rlen - 1] & 0xFFFF0070u) == 0xE1A00020u &&
                block_data_position + rlen < num_insns &&
                ((save_pc + (u32)(rlen * 4)) >> 12) == pc_region) {
              u32 after_op = readaddress32(pc_address_block,
                                           (save_pc + (u32)(rlen * 4)) & 0x0FFF);
              if ((after_op & 0xFFF00FF0u) == 0xE1B000D0u)
                rlen--;
            }
#endif
            if (rlen >= 2 && dup != 0u &&
                sh4g_arm_alu_run_native_ex(&translation_ptr, run_ops,
                                           rm_arr, wm_arr, rlen)) {
              int k;
              for (k = 1; k < rlen; k++) {
                pc = save_pc + (u32)(k * 4);
                arm_base_cycles();
              }
              pc = save_pc + (u32)(rlen * 4);
              block_data_position += rlen - 1;
              sh4g_litseg_maybe_flush(&translation_ptr);
              arm_fused = 1;
            } else {
              cycle_count = save_cc;
              pc = save_pc;
            }
          }
        }
        /* Unified dual-region (IWRAM + ROM8) ARM load-run pass: replaces the
         * old conflicting Peepholes 1, 4, and 7 with one pass handling both
         * byte-load runs (2..8 ops) and same-base mixed-width bursts (2..4 ops). */
        if (!arm_fused && (cur_op >> 28) == 0xEu && (next_op >> 28) == 0xEu &&
            (((cur_op & 0x0C100000u) == 0x04100000u) ||
             ((cur_op & 0x0E100090u) == 0x00100090u && (cur_op & 0x00000060u) != 0u)) &&
            (((next_op & 0x0C100000u) == 0x04100000u) ||
             ((next_op & 0x0E100090u) == 0x00100090u && (next_op & 0x00000060u) != 0u))) {
          u32 run_ops[8];
          int run_ccs[8];
          int rcnt = 1, consumed;
          u32 save_pc = pc;
          u32 save_cc = cycle_count;
          run_ops[0] = cur_op;
          run_ccs[0] = (int)cycle_count;
          while (rcnt < 8 &&
                 block_data_position + rcnt < num_insns &&
                 !block_data[block_data_position + rcnt].update_cycles &&
                 (save_pc + (u32)(rcnt * 4)) != cheat_master_hook &&
                 ((save_pc + (u32)(rcnt * 4)) >> 12) == pc_region) {
            u32 rop = readaddress32(pc_address_block, (save_pc + (u32)(rcnt * 4)) & 0x0FFF);
            if ((rop >> 28) != 0xEu ||
                !(((rop & 0x0C100000u) == 0x04100000u) ||
                  ((rop & 0x0E100090u) == 0x00100090u && (rop & 0x00000060u) != 0u)))
              break;
            pc = save_pc + (u32)(rcnt * 4);
            arm_base_cycles();
            run_ops[rcnt] = rop;
            run_ccs[rcnt] = (int)cycle_count;
            rcnt++;
          }
          pc = save_pc;
          consumed = (rcnt >= 2)
            ? sh4g_arm_load_run_native(&translation_ptr, run_ops, run_ccs, rcnt, pc)
            : 0;
          if (consumed >= 2) {
            cycle_count = (u32)run_ccs[consumed - 1];
            block_data_position += consumed - 1;
            pc = save_pc + (u32)(consumed * 4);
            sh4_greg_forward_reset();
            sh4g_litseg_maybe_flush(&translation_ptr);
            arm_fused = 1;
          } else {
            cycle_count = save_cc;
          }
        }
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
        /* Soundless skip for 1..2x fixed-rate IWRAM stereo PCM mix units:
         * LDMIA {4} + 2*(LDRSB + MLA + MLA + ADD #1) + STMIA!. */
        if (!arm_fused && (pc >> 24) == 0x03 &&
            (cur_op & 0xFFF00000u) == 0xE8900000u &&
            (next_op & 0xFFF00FF0u) == 0xE19000D0u &&
            block_data_position + 9 < num_insns &&
            ((pc + 36) >> 12) == pc_region) {
          int ok10 = 1, k10;
          for (k10 = 2; k10 < 10; k10++) {
            if (block_data[block_data_position + k10].update_cycles ||
                (pc + (u32)(k10 * 4)) == cheat_master_hook) {
              ok10 = 0; break;
            }
          }
          if (ok10) {
            u32 ops20[20];
            int ccs20[20];
            u32 save_pc = pc;
            u32 save_cc = cycle_count;
            int reps = 1;
            ops20[0] = cur_op; ccs20[0] = (int)cycle_count;
            for (k10 = 1; k10 < 10; k10++) {
              pc = save_pc + (u32)(k10 * 4);
              arm_base_cycles();
              ops20[k10] = readaddress32(pc_address_block, pc & 0x0FFF);
              ccs20[k10] = (int)cycle_count;
            }
            if (block_data_position + 19 < num_insns &&
                ((save_pc + 76) >> 12) == pc_region) {
              int ok20 = 1;
              u32 cc_after10 = cycle_count;
              for (k10 = 10; k10 < 20; k10++) {
                u32 rop;
                if (block_data[block_data_position + k10].update_cycles ||
                    (save_pc + (u32)(k10 * 4)) == cheat_master_hook) {
                  ok20 = 0; break;
                }
                rop = readaddress32(pc_address_block, (save_pc + (u32)(k10 * 4)) & 0x0FFF);
                if (rop != ops20[k10 - 10]) {
                  ok20 = 0; break;
                }
                ops20[k10] = rop;
              }
              if (ok20) {
                for (k10 = 10; k10 < 20; k10++) {
                  pc = save_pc + (u32)(k10 * 4);
                  arm_base_cycles();
                  ccs20[k10] = (int)cycle_count;
                }
                reps = 2;
              } else {
                cycle_count = cc_after10;
              }
            }
            pc = save_pc;
            if (sh4g_arm_rmw_block4_ldrsb_mla_skip(&translation_ptr,
                                                   ops20, ccs20, reps, pc)) {
              block_data_position += reps * 10 - 1;
              pc += (u32)(reps * 40);
              sh4_greg_forward_reset();
              sh4g_litseg_maybe_flush(&translation_ptr);
              arm_fused = 1;
            } else {
              cycle_count = save_cc;
            }
          }
        }
#endif
      }
#endif
      if (!arm_fused) {
        u8 *forward_start = translation_ptr;
        translate_arm_instruction();
        /* Straight-line unconditional ALU (non-S or dead-flag S, non-reg-shift,
         * Rd!=PC) or MUL/MLA (non-S, Rd!=PC), ending at MOV.L Rm,@(disp,R14). */
        if (__builtin_expect(translation_ptr > forward_start &&
                             translation_ptr[-2] == 0x1Eu, 0)) {
          sh4_greg_forward_finish(forward_start, translation_ptr,
            (opcode >> 28) == 14 &&
            (((opcode & 0x0C000000) == 0 &&
              (!(opcode & 0x00100000) || !(flag_status & 15)) &&
              ((opcode & 0x02000000) || !(opcode & 0x00000010)) &&
              ((opcode >> 12) & 15) != 15 &&
              (((opcode >> 21) & 15) < 8 || ((opcode >> 21) & 15) >= 12)) ||
             ((opcode & 0xFFD000F0) == 0xE0000090 && ((opcode >> 16) & 15) != 15) ||
             ((pc >> 24) != 0x03 && CGBA_SH4_COMPACT_FASTMEM_TUPLE &&
              cycle_count <= 0xFFFFu && ((opcode >> 12) & 15u) != 15u &&
              (((opcode & 0x0D300000u) == 0x05100000u) ||
               (((opcode & 0x0F300090u) == 0x01100090u) && (opcode & 0x60u) != 0u)))));
        } else {
          sh4_greg_forward_reset();
        }
        sh4g_litseg_maybe_flush(&translation_ptr);
      }
    }
#else
    translate_arm_instruction();
#endif
#ifdef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
    generate_cycle_update();
#endif
    block_data_position++;

    /* If it went too far the cache needs to be flushed and the process
       restarted. Because we might already be nested several stages in
       a simple recursive call here won't work, it has to pedal out to
       the beginning. */

#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
    if (!ram_region)
      translation_cache_limit = cgba_sh4_fm_ool_rom_limit(TRANSLATION_CACHE_LIMIT_THRESHOLD);
#endif
    if(translation_ptr > translation_cache_limit) {
      CGBA_DIAG_LOG("@@CGBA_XFLUSH arm ram=%d start=%08lx pc=%08lx end=%08lx pos=%ld cap=%lu",
        (int)ram_region, (unsigned long)block_start_pc, (unsigned long)pc,
        (unsigned long)block_end_pc, (long)block_data_position,
        (unsigned long)CGBA_BLOCK_SCAN_CAP_EXPR);
      cgba_block_overflow_shrink(attempt_base, translation_ptr, ram_region);
#ifdef SH4_ARCH
      sh4_greg_forward_reset();
      sh4g_litseg_abort();
#endif
      if (ram_region) {
        cgba_dynarec_ram_full_count++;
        flush_translation_cache_ram();
      } else
        flush_translation_cache_rom();
      return false;
    }

    /* If the next instruction is a block entry point update the
       cycle counter and update */
#ifndef SH4_ARCH
    if (pc != block_end_pc &&
        block_data[block_data_position].update_cycles)
    {
      generate_cycle_update();
    }
#endif
  }

#ifdef SH4_ARCH
  sh4_greg_forward_reset();
  if ((last_condition & 0x0F) != 0x0E) {
    sh4g_litseg_end(&translation_ptr);
    generate_branch_patch_conditional(backpatch_address, translation_ptr);
    last_condition = 0x0E;
    ended_uncond = 0;   /* the open conditional's skip target IS the gate */
  } else if (chain_existing_target == NULL) {
    if (ended_uncond)
      sh4g_litseg_end_tail(&translation_ptr);
    else
      sh4g_litseg_end(&translation_ptr);
  }
  if (chain_existing_target != NULL) {
    cgba_sh4_emit_chain_existing(&translation_ptr, &cycle_count,
                                 chain_existing_target);
    sh4g_litseg_end_tail(&translation_ptr);
    ended_uncond = 1;
  }
#endif

  /* This can happen if the last instruction is *not* inconditional */
  if ((last_condition & 0x0F) != 0x0E) {
    generate_branch_patch_conditional(backpatch_address, translation_ptr);
    ended_uncond = 0;   /* the open conditional's skip target IS the gate */
  }

  /* Generate the fall-through translation gate unless the scan provably ended
     at an unconditional branch nothing falls past (then it is dead code —
     ~20-24 bytes on most blocks). Kept for MAX_EXITS / gate-target /
     size-limit ends, where fall-through is real. */
  if (!ended_uncond)
    generate_translation_gate(arm);

  for(i = 0; i < block_exit_position; i++)
  {
#ifdef SH4_ARCH
    if (!block_exits[i].branch_source)
      continue;
#endif
    branch_target = block_exits[i].branch_target;

    if((branch_target >= block_start_pc) && (branch_target < block_end_pc))
    {
      /* Internal branch, patch to recorded address */
#ifdef SH4_ARCH
      translation_target =
       block_data[(branch_target - block_start_pc) /
        arm_instruction_width].body_offset;
      generate_branch_patch_internal(block_exits[i].branch_source,
       translation_target);
#else
      translation_target =
       block_data[(branch_target - block_start_pc) /
        arm_instruction_width].block_offset;
      generate_branch_patch_unconditional(block_exits[i].branch_source,
       translation_target);
#endif
    }
    else
    {
      /* External branch, save for later */
      external_block_exits[external_block_exit_position].branch_target =
       branch_target;
      external_block_exits[external_block_exit_position].branch_source =
       block_exits[i].branch_source;
      external_block_exit_position++;
    }
  }

#ifdef SH4_ARCH
  /* Keep the cache cursor 4-aligned: the next block's hashhdr/ramtag and any
     leading literal tuples are written with 32-bit stores, and vec-jmp exits
     (JMP @R9-table / @R10) end on 2-byte granularity unlike the old
     literal-island exits which always ended at literal+4. */
  if ((uintptr_t)translation_ptr & 3) {
    *(u16 *)translation_ptr = 0x0009;             /* NOP pad (never executed) */
    translation_ptr += 2;
  }
#endif
  if (ram_region)
    ram_translation_ptr = translation_ptr;
  else {
    /* THIS block's own emission (cursor delta incl. its hashhdr) — measured
       here, before external-exit resolution recursively translates other
       blocks, so nested emission isn't double-counted into the stat. */
    CGBA_EM_BLK_STAT((translation_ptr - rom_translation_ptr) +
                     sizeof(hashhdr_type), 0);
    rom_translation_ptr = translation_ptr;
  }

#ifdef SH4_ARCH
  /* Single-block diff mode: skip external-branch resolution to avoid gpSP's
     recursive translate-the-whole-reachable-graph cascade; unresolved exits
     simply redispatch through sh4_block_exit. */
  if (!CGBA_SH4_DIFF_ACTIVE())
#endif
  for(i = 0; i < external_block_exit_position; i++)
  {
    branch_target = external_block_exits[i].branch_target;
#ifdef SH4_ARCH
    /* All BIOS targets INCLUDING the SWI vector (0x8) keep dispatching via
       block_lookup_address -> sh4_bios_fallback_entry. bios_swi_entrypoint
       is NULL on SH4 (init_bios_hooks is only wired into the mips/x86/arm64
       backends), so the old ==0x8 arm aborted resolution and left every
       later exit of an SWI-bearing block unchained. */
    if (branch_target < 0x00004000u)
      continue;
#endif
    if(branch_target == 0x00000008)
      translation_target = bios_swi_entrypoint;
    else {
#ifdef SH4_ARCH
      cgba_cold_gate_probe++;          /* probe, don't heat (see the gate) */
      translation_target = block_lookup_translate_arm(branch_target);
      cgba_cold_gate_probe--;
    }
#else
      translation_target = block_lookup_translate_arm(branch_target);
    }
#endif
#ifdef SH4_ARCH
    if (translation_target == (u8 *)(~(uintptr_t)0)) {
      /* Untranslatable-address sentinel: dispatch traps it loudly via
         cgba_sh4_wild_jump if the branch is ever taken; hard-patching it
         would jump to host 0xFFFFFFFF with no diagnostics. */
      continue;
    }
    if (!translation_target && cgba_cold_pending) {
      /* Cold-gate target: leave the exit unpatched — it keeps dispatching
         through sh4_block_exit and the gate decides later. Treating this
         as translate-failure wholesale-flushed the cache in a loop. */
      cgba_cold_pending = 0;
      continue;
    }
    if (translation_target && branch_target != 0x00000008) {
      /* Never chain across caches: flush_translation_cache_ram leaves the
         ROM cache in place (and vice versa), so a cross-cache direct chain
         dangles into freed memory once the other side flushes — EXC=180 at
         a host PC inside the arena, long after the flush (field crash with
         RAM=102k churn). Cross-cache branches keep dispatching through
         sh4_block_exit. */
      int tgt_ram = ((u8 *)translation_target >= ram_translation_cache &&
                     (u8 *)translation_target <
                       ram_translation_cache + RAM_TRANSLATION_CACHE_SIZE);
      if ((ram_region != 0) != tgt_ram)
        continue;
    }
#endif
    if (!translation_target)
      return false;
    generate_branch_patch_unconditional(
      external_block_exits[i].branch_source, translation_target);
  }
  return true;
}

#if defined(SH4_ARCH) && !defined(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
/* External ROM flag-liveness lookahead: checks if up to 6 straight-line,
 * non-store, non-LDM, non-branch Thumb instructions starting at `target_pc`
 * (immutable Game Pak ROM) overwrite all flags in `need_kill` before any
 * flag read. Returns 1 if killed at step 0 (cycles > 0 suffices), 2 if killed
 * at step 1..5 (cycles > 96 guarantees no gate or cold-interp stop before the
 * killer), or 0 otherwise. */
static inline int sh4_thumb_flags_dead_at_rom_pc(u32 target_pc, u32 need_kill,
                                                 u32 cheat_hook)
{
  u32 region;
  const u8 *page;
  u32 killed = 0u;
  int step;

  if (target_pc < 0x08000000u || target_pc >= 0x0E000000u || (target_pc & 1u))
    return 0;
  page = cgba_memory_map_read_4k(target_pc);
  if (!page)
    return 0;
  region = target_pc >> 12;

  for (step = 0; step < 6; step++) {
    u32 cur_pc = target_pc + (u32)(step * 2);
    u32 op, hi;
    u16 info;
    u32 req;
    if ((cur_pc >> 12) != region || cur_pc == cheat_hook)
      return 0;
    op = readaddress16(page, cur_pc & 0x0FFFu);
    info = cgba_thumb_scan_info(op);
    if (info & 0x8000u)                            /* any branch / exit / SWI */
      return 0;
    hi = (op >> 8) & 0xFFu;
    /* Reject SWI (incl. HLE div), BL prefix, ADD pc, stores, LDM/STM/PUSH/POP,
     * and undefined opcodes so no I/O side effect or multi-word cycle burst
     * can precede the flag-killer. */
    if (hi == 0xDFu || hi >= 0xE8u || (op & 0xFF87u) == 0x4487u)
      return 0;
    if ((hi >= 0x50u && hi <= 0x55u) ||
        (hi >= 0x60u && hi <= 0x67u) ||
        (hi >= 0x70u && hi <= 0x77u) ||
        (hi >= 0x80u && hi <= 0x87u) ||
        (hi >= 0x90u && hi <= 0x97u) ||
        (hi >= 0xB1u && hi <= 0xCFu))
      return 0;
    req = (info >> 8) & 0x0Fu;
    if (req & ~killed)
      return 0;
    killed |= (info >> 4) & 0x0Fu;
    if ((killed & need_kill) == need_kill)
      return (step == 0) ? 1 : 2;
  }
  return 0;
}

static __attribute__((noinline)) int
cgba_thumb_try_peephole(
    u8 **tp_p, u32 cur_op, u32 next_op,
    u32 *pc_p, u32 *cycle_count_p, s32 *bd_pos_p, u32 *be_pos_p,
    u32 *last_op_p, u32 *op_p,
    u32 *const_mask_p, const u32 *sh4_thumb_const_val,
    const u8 *pc_address_block, u32 pc_region,
    u32 block_start_pc, u32 block_end_pc,
    s32 num_insns, u32 total_exits, u32 cheat_hook, bool ram_region)
{
  u8 *translation_ptr = *tp_p;
  u32 pc = *pc_p;
  u32 cycle_count = *cycle_count_p;
  s32 block_data_position = *bd_pos_p;
  u32 block_exit_position = *be_pos_p;
  u32 sh4_thumb_const_mask = *const_mask_p;
  const int sh4_block_is_thumb = 1;
  u32 next_hi = (next_op >> 8) & 0xFFu;
  int thumb_fused = 0;

  /* Fuse GCC Thumb zero/sign-extend idiom:
   *   LSL Rd, Rm, #16/#24 (dead flags) + LSR/ASR Rd, Rd, #16/#24 (dead C)
   * into a single SH-4 EXTU.W / EXTS.W / EXTU.B / EXTS.B. */
  if (((cur_op & 0xFFC0u) == 0x0400u || (cur_op & 0xFFC0u) == 0x0600u) &&
      !(block_data[block_data_position].flag_data & 0xFu) &&
      !(block_data[block_data_position + 1].flag_data & 0x2u)) {
    unsigned e_rd = cur_op & 7u;
    unsigned e_rm = (cur_op >> 3) & 7u;
    unsigned e_is_byte = ((cur_op & 0xFFC0u) == 0x0600u);
    u32 want_lsr = (e_is_byte ? 0x0E00u : 0x0C00u) | (e_rd << 3) | e_rd;
    u32 want_asr = (e_is_byte ? 0x1600u : 0x1400u) | (e_rd << 3) | e_rd;
    if (next_op == want_lsr || next_op == want_asr) {
      u32 next_fm = block_data[block_data_position + 1].flag_data & 0xCu;
      u8 *forward_start = translation_ptr;
      int is_signed = (next_op == want_asr);
      unsigned dst_reg = SH4_REG_T0;
      { sh4_codegen cg = sh4g_open(&translation_ptr);
        int pin_rd = sh4_pinned_reg(e_rd);
        if (pin_rd >= 0 && e_rd == e_rm) {
          dst_reg = (unsigned)pin_rd;
        } else if (pin_rd >= 0) {
          sh4_emit_load_greg(&cg, e_rm, (unsigned)pin_rd);
          dst_reg = (unsigned)pin_rd;
        } else {
          sh4_emit_load_greg(&cg, e_rm, SH4_REG_T0);
        }
        if (e_is_byte) {
          if (is_signed) sh4_emit_exts_b(&cg, dst_reg, dst_reg);
          else           sh4_emit_extu_b(&cg, dst_reg, dst_reg);
        } else {
          if (is_signed) sh4_emit_exts_w(&cg, dst_reg, dst_reg);
          else           sh4_emit_extu_w(&cg, dst_reg, dst_reg);
        }
        if (dst_reg == SH4_REG_T0)
          sh4_emit_store_greg(&cg, SH4_REG_T0, e_rd);
        sh4g_close(&translation_ptr, &cg); }
      if (next_fm)
        sh4g_set_nz_m(&translation_ptr, dst_reg, next_fm);
      sh4_thumb_const_kill(e_rd);
      pc += 2;
      thumb_base_cycles();
      pc += 2;
      block_data_position++;
      *last_op_p = next_op;
      *op_p = next_op;
      sh4_greg_forward_finish(forward_start, translation_ptr,
                              !next_fm && dst_reg == SH4_REG_T0);
      sh4g_litseg_maybe_flush(&translation_ptr);
      thumb_fused = 1;
    }
  }
  /* General Thumb peephole: constant-RAM-base ADD + LDRB/LDRH/LDR
   * indexed array load (e.g. LDR r0, =iwram; ADD r0, rIdx; LDRB r0, [r0]). */
  if (!thumb_fused && sh4_thumb_const_mask != 0u &&
      (((cur_op & 0xFE00u) == 0x1800u) || ((cur_op & 0xFF00u) == 0x4400u)) &&
      (((next_op & 0xF800u) == 0x7800u) ||
       ((next_op & 0xF800u) == 0x8800u) ||
       ((next_op & 0xF800u) == 0x6800u))) {
    u32 save_pc = pc;
    u32 save_cc = cycle_count;
    unsigned f_rA = 0, f_rD = 0;
    pc += 2;
    thumb_base_cycles();
    { int cc2 = (int)cycle_count;
      pc = save_pc;
      if (sh4g_thumb_ram_idx_load_native(
            &translation_ptr, cur_op, next_op, pc, cc2,
            sh4_thumb_const_mask, sh4_thumb_const_val,
            (block_data[block_data_position].flag_data & 0xFu) != 0u,
            &f_rA, &f_rD)) {
        sh4_thumb_const_kill(f_rA);
        sh4_thumb_const_kill(f_rD);
        pc += 4;
        block_data_position++;
        *last_op_p = next_op;
        *op_p = next_op;
        sh4_greg_forward_reset();
        sh4g_litseg_maybe_flush(&translation_ptr);
        thumb_fused = 1;
      } else {
        cycle_count = save_cc;
      } }
  }
  /* General Thumb peephole: consecutive `(STRH rD, [rB, #0]; ADD rB, #even)`
   * strided halfword store run (2..6 pairs, dead ADD flags). */
  if (!thumb_fused &&
      (cur_op & 0xFFC0u) == 0x8000u &&
      (next_op & 0xFF00u) == (0x3000u | (((cur_op >> 3) & 7u) << 8)) &&
      !(block_data[block_data_position + 1].flag_data & 0xFu)) {
    u32 rB = (cur_op >> 3) & 7u;
    u32 run_ops[12];
    int run_ccs[12];
    int npairs = 0;
    u32 save_pc = pc;
    u32 save_cc = cycle_count;
    while (npairs < 6 &&
           block_data_position + npairs * 2 + 1 < num_insns &&
           ((save_pc + (u32)((npairs * 2 + 1) * 2)) >> 12) == pc_region) {
      int i0 = npairs * 2;
      int i1 = i0 + 1;
      u32 p0 = save_pc + (u32)(i0 * 2);
      u32 p1 = save_pc + (u32)(i1 * 2);
      u32 o0, o1;
      if ((i0 > 0 && block_data[block_data_position + i0].update_cycles) ||
          block_data[block_data_position + i1].update_cycles ||
          p0 == cheat_hook || p1 == cheat_hook)
        break;
      if (block_data[block_data_position + i1].flag_data & 0xFu)
        break;
      o0 = readaddress16(pc_address_block, p0 & 0x0FFF);
      o1 = readaddress16(pc_address_block, p1 & 0x0FFF);
      if ((o0 & 0xFFC0u) != 0x8000u || ((o0 >> 3) & 7u) != rB ||
          (o0 & 7u) == rB ||
          (o1 & 0xFF00u) != (0x3000u | (rB << 8)) ||
          (o1 & 0xFFu) == 0u || ((o1 & 1u) != 0u))
        break;
      if (i0 == 0) {
        run_ccs[0] = (int)save_cc;
      } else {
        pc = p0; thumb_base_cycles();
        run_ccs[i0] = (int)cycle_count;
      }
      pc = p1; thumb_base_cycles();
      run_ccs[i1] = (int)cycle_count;
      run_ops[i0] = o0;
      run_ops[i1] = o1;
      npairs++;
    }
    pc = save_pc;
    if (npairs >= 2 &&
        sh4g_thumb_strh_stride_run_native(&translation_ptr, run_ops,
                                          run_ccs, npairs, pc)) {
      sh4_thumb_const_kill(rB);
      block_data_position += npairs * 2 - 1;
      *last_op_p = run_ops[npairs * 2 - 1];
      *op_p = run_ops[npairs * 2 - 1];
      pc += (u32)(npairs * 4);
      sh4_greg_forward_reset();
      sh4g_litseg_maybe_flush(&translation_ptr);
      thumb_fused = 1;
    } else {
      cycle_count = save_cc;
    }
  }
  /* General Thumb peephole: consecutive same-base immediate-offset load/store
   * run (2..8 ops of 0x6000..0x8FFF sharing rB, no mid-run write to rB).
   * Yield to zero-branch sh4g_thumb_ldst_const_native when rB is a known
   * non-IWRAM constant (e.g. IO/EWRAM, which would miss the IWRAM-only guard
   * 100% of the time) or a known IWRAM constant with loads only. */
  if (!thumb_fused &&
      cur_op >= 0x6000u && cur_op < 0x9000u &&
      next_op >= 0x6000u && next_op < 0x9000u &&
      ((cur_op >> 3) & 7u) == ((next_op >> 3) & 7u) &&
      !(((cur_op >> 11) & 1u) && (cur_op & 7u) == ((cur_op >> 3) & 7u))) {
    u32 run_ops[8];
    int run_ccs[8];
    unsigned run_rb = (cur_op >> 3) & 7u;
    int rb_is_const = (int)((sh4_thumb_const_mask >> run_rb) & 1u);
    if (!rb_is_const || ((sh4_thumb_const_val[run_rb] >> 24) == 0x03u)) {
      int rcnt = 0, has_store = !((cur_op >> 11) & 1u);
      u32 save_pc = pc;
      u32 save_cc = cycle_count;
      run_ops[0] = cur_op;
      run_ccs[0] = (int)cycle_count;
      rcnt = 1;
      while (rcnt < 8 &&
             block_data_position + rcnt < num_insns &&
             !block_data[block_data_position + rcnt].update_cycles &&
             (save_pc + (u32)(rcnt * 2)) != cheat_hook &&
             ((save_pc + (u32)(rcnt * 2)) >> 12) == pc_region) {
        u32 rop = readaddress16(pc_address_block,
                                (save_pc + (u32)(rcnt * 2)) & 0x0FFFu);
        if (rop < 0x6000u || rop >= 0x9000u || ((rop >> 3) & 7u) != run_rb)
          break;
        /* Stop if previous op loaded into run_rb (only allowed as final op). */
        if (rcnt > 1 && ((run_ops[rcnt - 1] >> 11) & 1u) &&
            (run_ops[rcnt - 1] & 7u) == run_rb)
          break;
        pc = save_pc + (u32)(rcnt * 2);
        thumb_base_cycles();
        run_ops[rcnt] = rop;
        run_ccs[rcnt] = (int)cycle_count;
        if (!((rop >> 11) & 1u))
          has_store = 1;
        rcnt++;
      }
      pc = save_pc;
      if (rcnt >= 2 && (!rb_is_const || has_store) &&
          sh4g_thumb_base_run_native(&translation_ptr, run_ops, run_ccs,
                                     rcnt, pc)) {
        int k;
        for (k = 0; k < rcnt; k++) {
          if ((run_ops[k] >> 11) & 1u)
            sh4_thumb_const_kill(run_ops[k] & 7u);
        }
        block_data_position += rcnt - 1;
        *last_op_p = run_ops[rcnt - 1];
        *op_p = run_ops[rcnt - 1];
        pc += (u32)(rcnt * 2);
        sh4_greg_forward_reset();
        sh4g_litseg_maybe_flush(&translation_ptr);
        thumb_fused = 1;
      } else {
        cycle_count = save_cc;
      }
    }
  }
  /* General 1-insn Thumb forward-skip if-conversion:
   *   (CMP rN,#imm | CMP rN,rM | CMP hN,hM | TST rN,rM); B<cc> pc+6; <1 ALU>
   * when flags are dead at pc+6. Eliminates the forward branch seam and
   * executes with exact GBA cycle accounting on both arms (handles min/max/
   * abs/clamp/signed-div-pow2/cond-select across all games). */
  if (!thumb_fused &&
      next_hi >= 0xD0u && next_hi <= 0xDDu &&
      (next_op & 0xFFu) == 0u &&
      block_data_position + 3 < num_insns &&
      !block_data[block_data_position + 2].update_cycles &&
      (u32)(pc + 4) != cheat_hook &&
      ((u32)(pc + 4) >> 12) == pc_region &&
      block_exits[block_exit_position].branch_target == (u32)(pc + 6)) {
    u32 next_cond = next_hi & 0x0Fu;
    u32 op2 = readaddress16(pc_address_block, (pc + 4) & 0x0FFFu);
    u32 op2_hi = (op2 >> 8) & 0xFFu;
    u32 op2_rm = 0u, op2_wm = 0u;
    int cmp_kind = -1; /* 0=CMP(a,b), 1=TST(a,b), 2=CMP(a,#0) */
    int cmp_is_imm = 0;
    unsigned cmp_rn = 0, cmp_rm = 0;
    u32 cmp_imm = 0;
    u32 cur_hi = (cur_op >> 8) & 0xFFu;

    if (op2_hi <= 0x46u && sh4g_thumb_bbra_maybe(op2) &&
        sh4g_thumb_bbra_classify(op2, &op2_rm, &op2_wm) &&
        !(op2_wm & (1u << SH4_GREG_SP))) {
      if ((cur_op & 0xF8FFu) == 0x2800u &&
          (next_cond <= 1u || next_cond >= 0xAu)) {
        cmp_kind = 2; cmp_rn = (cur_op >> 8) & 7u;
      } else if (cur_hi >= 0x28u && cur_hi <= 0x2Fu &&
                 (next_cond <= 3u || next_cond >= 8u)) {
        cmp_kind = 0; cmp_rn = (cur_op >> 8) & 7u;
        cmp_is_imm = 1; cmp_imm = cur_op & 0xFFu;
      } else if ((cur_op & 0xFFC0u) == 0x4280u &&
                 (next_cond <= 3u || next_cond >= 8u)) {
        cmp_kind = 0; cmp_rn = cur_op & 7u; cmp_rm = (cur_op >> 3) & 7u;
      } else if (cur_hi == 0x45u &&
                 (next_cond <= 3u || next_cond >= 8u)) {
        unsigned hrn = (cur_op & 7u) | ((cur_op >> 4) & 8u);
        unsigned hrm = (cur_op >> 3) & 0xFu;
        if (hrn != 15u && hrm != 15u) {
          cmp_kind = 0; cmp_rn = hrn; cmp_rm = hrm;
        }
      } else if ((cur_op & 0xFFC0u) == 0x4200u && next_cond <= 1u) {
        cmp_kind = 1; cmp_rn = cur_op & 7u; cmp_rm = (cur_op >> 3) & 7u;
      }
    }

    if (cmp_kind >= 0 &&
        sh4_flags_dead_from(block_data, block_data_position + 3, num_insns, 0xFu)) {
      u32 region = (pc >> 24) & 0x0Fu;
      int seq_c = (int)ws_cyc_seq[region][0];
      int nseq_c = (int)ws_cyc_nseq[region][0];
      u8 *skip_br;

      sh4g_litseg_reserve_bytes(&translation_ptr, 4, 56u);
      { sh4_codegen cg = sh4g_open(&translation_ptr);
        sh4_emit_load_greg(&cg, cmp_rn, SH4_REG_T0);
        sh4g_close(&translation_ptr, &cg); }
      if (cmp_kind != 2) {
        if (cmp_is_imm) {
          sh4g_const(&translation_ptr, cmp_imm, SH4_REG_T1);
        } else {
          sh4_codegen cg = sh4g_open(&translation_ptr);
          sh4_emit_load_greg(&cg, cmp_rm, SH4_REG_T1);
          sh4g_close(&translation_ptr, &cg);
        }
      }
      { sh4_codegen cg = sh4g_open(&translation_ptr);
        if (cmp_kind == 2) {
          if (next_cond <= 1u)       sh4_emit_tst(&cg, SH4_REG_T0, SH4_REG_T0);
          else if (next_cond <= 0xBu) sh4_emit_cmppz(&cg, SH4_REG_T0);
          else                       sh4_emit_cmppl(&cg, SH4_REG_T0);
        } else if (cmp_kind == 1) {
          sh4_emit_tst(&cg, SH4_REG_T1, SH4_REG_T0);
        } else {
          switch (next_cond & 0xEu) {
          case 0x0u: sh4_emit_cmpeq(&cg, SH4_REG_T1, SH4_REG_T0); break;
          case 0x2u: sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0); break;
          case 0x8u: sh4_emit_cmphi(&cg, SH4_REG_T1, SH4_REG_T0); break;
          case 0xAu: sh4_emit_cmpge(&cg, SH4_REG_T1, SH4_REG_T0); break;
          default:   sh4_emit_cmpgt(&cg, SH4_REG_T1, SH4_REG_T0); break;
          }
        }
        sh4g_close(&translation_ptr, &cg); }
      /* Skip op2 when B<cc> IS taken: even cond -> BT, odd cond -> BF. */
      skip_br = ((next_cond & 1u) == 0u)
        ? sh4g_emit_bt_placeholder(&translation_ptr)
        : sh4g_emit_bf_placeholder(&translation_ptr);
      sh4_greg_forward_reset();
      if (op2_hi <= 0x17u) {
        sh4g_shift_imm(&translation_ptr, (int)((op2 >> 11) & 3u),
                       (unsigned)(op2 & 7u), (unsigned)((op2 >> 3) & 7u),
                       (unsigned)((op2 >> 6) & 31u), 0u);
      } else {
        sh4g_thumb_dp_native(&translation_ptr, op2, (u32)(pc + 4), 0u, 0);
      }
      if (seq_c > 0 && seq_c <= 128) {
        sh4_codegen cg = sh4g_open(&translation_ptr);
        sh4_emit_add_imm(&cg, -seq_c, SH4_REG_CYCLES);
        sh4g_close(&translation_ptr, &cg);
      }
      sh4g_patch_cond(skip_br, translation_ptr);
      cycle_count += (u32)(seq_c + nseq_c);
      block_exits[block_exit_position].branch_source = NULL;
      /* Clear the now-dead forward seam at pc+6 if no other exit targets it. */
      { u32 e, other = 0;
        for (e = 0; e < total_exits; e++) {
          if (e != block_exit_position &&
              block_exits[e].branch_target == (u32)(pc + 6)) {
            other = 1;
            break;
          }
        }
        if (!other)
          block_data[block_data_position + 3].update_cycles = 0;
      }
      sh4_thumb_const_mask &= ~op2_wm;
      block_exit_position++;
      block_data_position += 2;
      *last_op_p = op2;
      *op_p = op2;
      pc += 6;
      sh4_greg_forward_reset();
      sh4g_litseg_maybe_flush(&translation_ptr);
      thumb_fused = 1;
    }
  }
  if (!thumb_fused && next_hi >= 0xD0u && next_hi <= 0xDDu) {
    u32 next_cond = next_hi & 0x0Fu;
    u32 b_target = block_exits[block_exit_position].branch_target;
    u32 save_pc = pc;
    pc += 2;
    int is_idle = SH4_IDLE_BRANCH_CAN_ELIMINATE(b_target) ||
                  SH4_FWD_IDLE_BRANCH_CAN_ELIMINATE(b_target);
    pc = save_pc;
    if (!is_idle) {
      int fkind = -1;
      int f_alu = -1;
      int f_extra = 0;
      unsigned f_rn = 0, f_rd = 0, f_rm = 0;
      unsigned f_sh_kind = 0, f_sh_imm = 0;
      int f_is_imm = 0;
      u32 f_imm = 0;
      u32 cur_hi = (cur_op >> 8) & 0xFFu;
      if (cur_hi >= 0x28u && cur_hi <= 0x2Fu &&
          (next_cond <= 3u || next_cond >= 8u)) {
        fkind = 0; f_rn = (cur_op >> 8) & 7u;
        f_is_imm = 1; f_imm = cur_op & 0xFFu;
      } else if ((cur_op & 0xFFC0u) == 0x4280u &&
                 (next_cond <= 3u || next_cond >= 8u)) {
        fkind = 0; f_rn = cur_op & 7u; f_rm = (cur_op >> 3) & 7u;
      } else if (cur_hi == 0x45u &&
                 (next_cond <= 3u || next_cond >= 8u)) {
        unsigned rn5 = (cur_op & 7u) | ((cur_op >> 4) & 8u);
        unsigned rm5 = (cur_op >> 3) & 0xFu;
        if (rn5 != 15u && rm5 != 15u) {
          fkind = 0; f_rn = rn5; f_rm = rm5;
        }
      } else if ((cur_op & 0xFFC0u) == 0x4200u && next_cond <= 1u) {
        fkind = 1; f_rn = cur_op & 7u; f_rm = (cur_op >> 3) & 7u;
      } else if (cur_hi >= 0x30u && cur_hi <= 0x3Fu &&
                 (next_cond <= 1u || next_cond == 4u || next_cond == 5u)) {
        fkind = (cur_hi >= 0x38u) ? 2 : 3;
        f_rd = f_rn = (cur_op >> 8) & 7u;
        f_is_imm = 1; f_imm = cur_op & 0xFFu;
      } else if (cur_hi >= 0x18u && cur_hi <= 0x1Fu &&
                 (next_cond <= 1u || next_cond == 4u || next_cond == 5u)) {
        fkind = ((cur_op >> 9) & 1u) ? 2 : 3;
        f_rd = cur_op & 7u;
        f_rn = (cur_op >> 3) & 7u;
        f_is_imm = (cur_op >> 10) & 1;
        f_rm = f_imm = (cur_op >> 6) & 7u;
      } else if ((cur_op & 0xFFC0u) == 0x0000u &&
                 (next_cond <= 1u || next_cond == 4u || next_cond == 5u)) {
        fkind = 5; f_alu = SH4DP_MOV;
        f_rd = cur_op & 7u;
        f_rn = (cur_op >> 3) & 7u;
      } else if (cur_hi < 0x18u && ((cur_op >> 6) & 0x1Fu) != 0u &&
                 next_cond <= 5u) {
        fkind = 6;
        f_sh_kind = (cur_op >> 11) & 3u;
        f_sh_imm = (cur_op >> 6) & 0x1Fu;
        f_rd = cur_op & 7u;
        f_rn = (cur_op >> 3) & 7u;
        f_extra = (f_sh_kind == 0u) ? -(int)(32u - f_sh_imm)
                                    : -(int)(f_sh_imm - 1u);
      } else if (cur_hi >= 0x40u && cur_hi <= 0x43u &&
                 (next_cond <= 1u || next_cond == 4u || next_cond == 5u)) {
        unsigned alu = (cur_op >> 6) & 0xFu;
        switch (alu) {
        case 0x0u: f_alu = SH4DP_AND; break;
        case 0x1u: f_alu = SH4DP_EOR; break;
        case 0xCu: f_alu = SH4DP_ORR; break;
        case 0xDu: f_alu = SH4DP_MUL; break;
        case 0xEu: f_alu = SH4DP_BIC; break;
        case 0xFu: f_alu = SH4DP_MVN; break;
        default:   f_alu = -1;        break;
        }
        if (f_alu >= 0) {
          fkind = 5;
          f_rd = f_rn = cur_op & 7u;
          f_rm = (cur_op >> 3) & 7u;
        }
      }
      if (fkind >= 0) {
        u32 need_kill = (fkind == 1 || fkind == 5) ? 0xCu
                      : (fkind == 6) ? 0xEu : 0xFu;
        int long_br = 0;
        u32 eff_cond = next_cond;
        u32 eff_target = b_target;
        u32 op2_long = 0;
        /* General GCC Thumb long-branch inversion:
         *   op0 + B<cc> (pc+6) + B target2  ->  op0 + B<!cc> target2 */
        if (b_target == (u32)(pc + 6) &&
            block_exit_position + 1 < MAX_EXITS &&
            block_data_position + 3 < num_insns &&
            !block_data[block_data_position + 2].update_cycles &&
            (u32)(pc + 4) != cheat_hook &&
            ((u32)(pc + 4) >> 12) == pc_region) {
          op2_long = readaddress16(pc_address_block, (pc + 4) & 0x0FFF);
          if ((op2_long & 0xF800u) == 0xE000u) {
            u32 lt = block_exits[block_exit_position + 1].branch_target;
            u32 sv = pc;
            pc += 4;
            int is_idle2 = SH4_IDLE_BRANCH(lt);
            pc = sv;
            if (!is_idle2) {
              long_br = 1;
              eff_cond = next_cond ^ 1u;
              eff_target = lt;
            }
          }
        }
        int is_internal = (eff_target >= block_start_pc &&
                           eff_target < block_end_pc);
        s32 tgt_idx = is_internal
          ? (s32)((eff_target - block_start_pc) / thumb_instruction_width)
          : -1;
        int taken_dead = is_internal
          ? sh4_flags_dead_from(block_data, tgt_idx, num_insns, need_kill)
          : sh4_thumb_flags_dead_at_rom_pc(eff_target, need_kill, cheat_hook);
        s32 fall_idx = block_data_position + (long_br ? 3 : 2);
        int fall_dead = (fall_idx < num_insns &&
                         !(block_data[fall_idx].update_cycles & 1u))
          ? sh4_flags_dead_from(block_data, fall_idx, num_insns, need_kill)
          : 0;
        if (taken_dead || fall_dead || long_br) {
          int taken_debit;
          int internal_mode = 0;
          u8 *known_target = NULL;
          u8 *site;
          if (is_internal) {
            u32 br_pc = pc + (long_br ? 4u : 2u);
            if (eff_target > br_pc) {
              internal_mode = ((tgt_idx - fall_idx) <= 28) ? 3 : 2;
            } else {
              internal_mode = 1;
              if (tgt_idx <= (s32)block_data_position)
                known_target = block_data[tgt_idx].body_offset;
            }
          }
          if (fkind == 0 && f_is_imm && f_imm == 0u &&
              (eff_cond <= 1u || eff_cond >= 0xAu))
            fkind = 4;                               /* CMP a, #0 -> TST/PZ/PL */
          sh4g_litseg_reserve(&translation_ptr, 6);
          if (fkind == 5) {
            sh4_codegen cg = sh4g_open(&translation_ptr);
            if (f_alu == SH4DP_MOV) {
              sh4_emit_load_greg(&cg, f_rn, SH4_REG_T0);
            } else if (f_alu == SH4DP_MVN) {
              sh4_emit_load_greg(&cg, f_rm, SH4_REG_T1);
              sh4g_dp_compute(&cg, SH4DP_MVN);
            } else {
              sh4_emit_load_greg(&cg, f_rn, SH4_REG_T0);
              sh4_emit_load_greg(&cg, f_rm, SH4_REG_T1);
              sh4g_dp_compute(&cg, f_alu);
            }
            sh4_emit_store_greg(&cg, SH4_REG_T0, f_rd);
            sh4g_close(&translation_ptr, &cg);
          } else if (fkind == 6) {
            sh4_codegen cg = sh4g_open(&translation_ptr);
            sh4_emit_load_greg(&cg, f_rn, SH4_REG_T1);   /* R2 = orig Rs */
            sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_T0);
            sh4_emit_mov_imm(&cg,
              (f_sh_kind == 0u) ? (int)f_sh_imm : -(int)f_sh_imm,
              SH4_REG_RET);
            if (f_sh_kind == 2u)
              sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
            else
              sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T0);
            sh4_emit_store_greg(&cg, SH4_REG_T0, f_rd);
            sh4g_close(&translation_ptr, &cg);
          } else {
            { sh4_codegen cg = sh4g_open(&translation_ptr);
              sh4_emit_load_greg(&cg, f_rn, SH4_REG_T0);
              sh4g_close(&translation_ptr, &cg); }
            if (fkind != 4) {
              if (f_is_imm)
                sh4g_const(&translation_ptr, f_imm, SH4_REG_T1);
              else {
                sh4_codegen cg = sh4g_open(&translation_ptr);
                sh4_emit_load_greg(&cg, f_rm, SH4_REG_T1);
                sh4g_close(&translation_ptr, &cg);
              }
            }
          }
          if (fkind == 2 || fkind == 3 || fkind == 5 || fkind == 6)
            sh4_thumb_const_kill(f_rd);
          pc += 2;
          thumb_base_cycles();                       /* Bcc fetch cycle */
          if (long_br) {
            taken_debit = (int)cycle_count +
              (int)ws_cyc_nseq[((u32)(pc + 2) >> 24) & 0x0Fu][0] +
              (int)ws_cyc_seq[((u32)(pc + 2) >> 24) & 0x0Fu][0] +
              (int)ws_cyc_nseq[(eff_target >> 24) & 0x0Fu][0];
            site = sh4g_emit_fused_cmp_bcc_known(
              &translation_ptr, fkind, f_rd, eff_cond,
              taken_debit, eff_target, internal_mode,
              SH4_DEFAULT_BRANCH_EXIT(eff_target),
              taken_dead, fall_dead, f_extra, known_target);
            block_exits[block_exit_position].branch_source = NULL;
            block_exits[block_exit_position + 1].branch_source = site;
            cycle_count += ws_cyc_nseq[((u32)(pc + 4) >> 24) & 0x0Fu][0];
            block_exit_position += 2;
            block_data_position += 2;
            *last_op_p = op2_long;
            *op_p = op2_long;
            pc += 4;
          } else {
            taken_debit = (int)cycle_count +
              (int)ws_cyc_nseq[(eff_target >> 24) & 0x0Fu][0];
            block_exits[block_exit_position].branch_source =
              sh4g_emit_fused_cmp_bcc_known(
                &translation_ptr, fkind, f_rd, eff_cond,
                taken_debit, eff_target, internal_mode,
                SH4_DEFAULT_BRANCH_EXIT(eff_target),
                taken_dead, fall_dead, f_extra, known_target);
            cycle_count += ws_cyc_nseq[((u32)(pc + 2) >> 24) & 0x0Fu][0];
            block_exit_position++;
            block_data_position++;
            *last_op_p = next_op;
            *op_p = next_op;
            pc += 2;
          }
          sh4_greg_forward_reset();
          sh4g_litseg_maybe_flush(&translation_ptr);
          thumb_fused = 1;
        }
      }
    }
  }
  if (thumb_fused) {
    *tp_p = translation_ptr;
    *pc_p = pc;
    *cycle_count_p = cycle_count;
    *bd_pos_p = block_data_position;
    *be_pos_p = block_exit_position;
    *const_mask_p = sh4_thumb_const_mask;
  }
  return thumb_fused;
}
#endif

bool translate_block_thumb(u32 pc, bool ram_region)
{
  u32 ended_uncond = 0;
  u32 opcode = 0;
  u32 last_opcode;
  u32 condition;
  u16 cgba_thumb_scan_fl = 0;
  u32 pc_region = (pc >> 12);
  u32 new_pc_region;
  u8 *pc_address_block = cgba_memory_map_read_4k(pc);
  u32 block_start_pc = pc;
  u32 block_end_pc = pc;
  u32 block_exit_position = 0;
  s32 block_data_position = 0;
  u32 external_block_exit_position = 0;
  u32 branch_target;
  u32 cycle_count = 0;
  u8 *translation_target;
  u8 *backpatch_address = NULL;
  u8 *translation_ptr = NULL;
  u8 *translation_cache_limit = NULL;
  u8 *attempt_base = NULL;
#ifdef SH4_ARCH
  u8 *chain_existing_target = NULL;
#endif
  s32 i;
  u32 flag_status;
  block_exit_type external_block_exits[MAX_EXITS];
  generate_block_extra_vars_thumb();

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_thumb_translate_count++;
#endif

  thumb_fix_pc();

  if(!pc_address_block)
    return false;

  if (ram_region) {
    translation_ptr = ram_translation_ptr;
    translation_cache_limit = &ram_translation_cache[
       RAM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD
       - (0x10000 - ram_block_tag) / 2 * sizeof(ramtag_type)];
  } else {
    translation_ptr = rom_translation_ptr;
#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
    translation_cache_limit = cgba_sh4_fm_ool_rom_limit(TRANSLATION_CACHE_LIMIT_THRESHOLD);
#else
    translation_cache_limit = &rom_translation_cache[
       ROM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD];
#endif
  }
  attempt_base = translation_ptr;

#ifdef SH4_ARCH
  sh4g_litseg_begin();
#endif
  generate_block_prologue();

  /* This is a function because it's used a lot more than it might seem (all
     of the data processing functions can access it), and its expansion was
     massacreing the compiler. */

  scan_block(thumb);

#ifdef SH4_ARCH
  s32 num_insns = block_data_position;
  s32 total_insns = (s32)((block_end_pc - block_start_pc) /
                          thumb_instruction_width);
#endif

  for(i = 0; i < block_exit_position; i++)
  {
    branch_target = block_exits[i].branch_target;

#ifdef SH4_ARCH
    if((branch_target >= block_start_pc) &&
     (branch_target < block_end_pc))
#else
    if((branch_target > block_start_pc) &&
     (branch_target < block_end_pc))
#endif
    {
      u32 seam_idx = (branch_target - block_start_pc) /
                     thumb_instruction_width;
#ifdef SH4_ARCH
      block_data[seam_idx].body_offset = NULL;
      if (branch_target <= cgba_block_exit_pc[i] || cgba_dynarec_single_block) {
        block_data[seam_idx].update_cycles |= 1u;
        /* Backward/loop seam: the flush+gate at this seam can take an IRQ
           with CPSR architecturally observable — keep flags live across it. */
        block_data[seam_idx].flag_data |= 0xF00;
      } else {
        /* Forward-only join: syncs R13 without a loop-break IRQ gate. */
        block_data[seam_idx].update_cycles |= 2u;
      }
#else
      block_data[seam_idx].update_cycles = 1;
#endif
    }
  }

  thumb_dead_flag_eliminate();

#ifdef SH4_ARCH
  u32 total_exits = block_exit_position;
#endif
  block_exit_position = 0;
  block_data_position = 0;
#ifdef SH4_ARCH
  sh4_greg_forward_reset();
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
  const int allow_fuse = !cgba_dynarec_single_block;
  int sp_span_valid = 0;       /* 0=none, 1=loads-only, 2=loads+stores */
  u32 sp_span_max_off = 0u;
  u32 sp_span_store_wmask = 0u;
#endif
  u32 pc_seq_cycles = ws_cyc_seq[(pc >> 24) & 0x0Fu][0];
  if ((pc >> 12) != pc_region) {
    pc_region = pc >> 12;
    pc_address_block = cgba_memory_map_read_4k(pc);
    if (!pc_address_block) {
      sh4_greg_forward_reset();
      sh4g_litseg_abort();
      CGBA_SH4_FM_OOL_REWIND(attempt_base, ram_region);
      return false;
    }
  }
#endif
  const u32 cheat_hook = cheat_master_hook;

  while(pc != block_end_pc)
  {
#ifdef SH4_ARCH
    /* Block-entry: flush before block_offset (loop-back bypasses it), gate-only
     * AT block_offset (loop-back hits it). Thumb has no conditional runs, so no
     * force-close is needed -- the flush is already unconditional here. */
    if(block_data[block_data_position].update_cycles)
    {
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
      sp_span_valid = 0;
#endif
      if (block_data[block_data_position].update_cycles & 1u) {
        generate_cycle_update();
        block_data[block_data_position].block_offset = translation_ptr;
        sh4_thumb_const_clear_all();
        sh4_greg_forward_reset();
        generate_prof_block_entry(1);
        generate_cycle_gate(0);
#if defined(CGBA_SH4_BLOCK_PROFILE) && CGBA_SH4_BLOCK_PROFILE
        block_data[block_data_position].body_offset =
          block_data[block_data_position].block_offset;
#else
        block_data[block_data_position].body_offset = translation_ptr;
#endif
      } else {
        /* Forward-only join: debit accumulated cycles without an IRQ gate. */
        if (cycle_count != 0) {
          sh4g_cycle_debit(&translation_ptr, (int)cycle_count);
          cycle_count = 0;
        }
        block_data[block_data_position].block_offset = translation_ptr;
        block_data[block_data_position].body_offset = translation_ptr;
        sh4_thumb_const_clear_all();
        sh4_greg_forward_reset();
      }
    }
    else
    {
      block_data[block_data_position].block_offset = translation_ptr;
      block_data[block_data_position].body_offset = translation_ptr;
      if(block_data_position == 0)
        generate_prof_block_entry(1);
    }
    new_pc_region = (pc >> 12);
    if (__builtin_expect(new_pc_region != pc_region, 0)) {
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
      sp_span_valid = 0;
#endif
      pc_region = new_pc_region;
      pc_address_block = cgba_memory_map_read_4k(pc);
      if (!pc_address_block) {
        sh4_greg_forward_reset();
        sh4g_litseg_abort();
        CGBA_SH4_FM_OOL_REWIND(attempt_base, ram_region);
        return false;
      }
      pc_seq_cycles = ws_cyc_seq[(pc >> 24) & 0x0Fu][0];
    }
    cycle_count += pc_seq_cycles;
#else
    block_data[block_data_position].block_offset = translation_ptr;
    thumb_base_cycles();
#endif

    if (__builtin_expect(pc == cheat_hook, 0))
    {
#if defined(SH4_ARCH) && !defined(CGBA_SH4_EXACT_CYCLE_BOUNDARIES)
      sp_span_valid = 0;
#endif
      thumb_process_cheats();
    }

#ifdef SH4_ARCH
    {
      u32 cur_op = readaddress16(pc_address_block, (pc & 0x0FFF));
      int thumb_fused = 0;
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
      if (allow_fuse &&
          cgba_sh4_pop1_bx_routine != NULL &&
          (cur_op >> 8) == 0xBCu &&
          block_data_position + 1 < total_insns &&
          !block_data[block_data_position + 1].update_cycles &&
          (pc + 2) != cheat_hook &&
          ((pc + 2) & 0x0FFFu) != 0u) {
        u32 rlist = cur_op & 0xFFu;
        if (rlist != 0u && (rlist & (rlist - 1u)) == 0u) {
          u32 next_op = readaddress16(pc_address_block, (pc + 2) & 0x0FFF);
          unsigned rx = (unsigned)__builtin_ctz(rlist);
          if (next_op == (0x4700u | (rx << 3))) {
            /* POP {rX}; BX rX -> compact 12-byte ILRAM call. POP's fetch cycle
             * is already in cycle_count; BX cancels its own fetch cycle. */
            sh4g_litseg_reserve(&translation_ptr, 1);
            if (cycle_count != 0) {
              sh4g_cycle_debit(&translation_ptr, (int)cycle_count);
              cycle_count = 0;
            }
            sh4g_const(&translation_ptr,
                       (u32)(uintptr_t)cgba_sh4_pop1_bx_routine, SH4_REG_RET);
            if (((uintptr_t)translation_ptr + 4u) & 3u)
              sh4g_u16(&translation_ptr, 0x0009u);
            { sh4_codegen _cg = sh4g_open(&translation_ptr);
              sh4_emit_jsr(&_cg, SH4_REG_RET);
              sh4_emit_mov_imm(&_cg, (int)(rx * 4u), SH4_REG_ARG1);
              sh4_emit_u32_be(&_cg, (u32)pc);
              sh4g_close(&translation_ptr, &_cg); }
            sh4_thumb_const_clear_all();
            sh4_greg_forward_reset();
            last_opcode = next_op;
            opcode = next_op;
            pc += 4;
            block_data_position++;
            sh4g_litseg_maybe_flush(&translation_ptr);
            thumb_fused = 1;
          }
        }
      }
      if (!thumb_fused &&
          allow_fuse &&
          block_data_position + 1 < num_insns &&
          !block_data[block_data_position + 1].update_cycles &&
          (pc + 2) != cheat_hook &&
          ((pc + 2) & 0x0FFFu) != 0u) {
        u32 next_op = readaddress16(pc_address_block, (pc + 2) & 0x0FFF);
        /* 4-slot R4..R7 BBRA runs BEFORE standalone LSL+LSR/ASR extend so
         * dead-flag zero/sign-extends stay fused inside longer ALU runs, and
         * propagates compile-time constants across the entire run. */
        if (!(block_data[block_data_position].flag_data & 0xFu) &&
            !(block_data[block_data_position + 1].flag_data & 0xFu) &&
            sh4g_thumb_bbra_maybe(cur_op)) {
          u32 rm_arr[16], wm_arr[16];
          u16 next_rop = (u16)next_op;
          if (sh4g_thumb_bbra_maybe(next_rop) &&
              sh4g_thumb_bbra_classify(cur_op, &rm_arr[0], &wm_arr[0]) &&
              sh4g_thumb_bbra_classify(next_rop, &rm_arr[1], &wm_arr[1])) {
            const u32 unpin = 0x7FFFu
#if CGBA_SH4_PIN_GUEST_R0
              & ~1u
#endif
#if CGBA_SH4_PIN_GUEST_R1
              & ~2u
#endif
              ;
            u16 run_ops[16];
            int rlen = 2;
            u32 t0 = (rm_arr[0] | wm_arr[0]) & unpin;
            u32 t1 = (rm_arr[1] | wm_arr[1]) & unpin;
            u32 seen = t0 | t1;
            u32 dup  = (t0 & t1) | (rm_arr[0] & wm_arr[0] & unpin) |
                       (rm_arr[1] & wm_arr[1] & unpin);
            run_ops[0] = (u16)cur_op;
            run_ops[1] = next_rop;
            while (rlen < 16 &&
                   block_data_position + rlen < num_insns &&
                   !block_data[block_data_position + rlen].update_cycles &&
                   !(block_data[block_data_position + rlen].flag_data & 0xFu) &&
                   (pc + (u32)(rlen * 2)) != cheat_hook &&
                   ((pc + (u32)(rlen * 2)) & 0x0FFFu) != 0u) {
              u16 rop = readaddress16(pc_address_block,
                                      (pc + (u32)(rlen * 2)) & 0x0FFF);
              u32 tk;
              if (!sh4g_thumb_bbra_maybe(rop) ||
                  !sh4g_thumb_bbra_classify(rop, &rm_arr[rlen], &wm_arr[rlen]))
                break;
              tk = (rm_arr[rlen] | wm_arr[rlen]) & unpin;
              dup |= (seen & tk) | (rm_arr[rlen] & wm_arr[rlen] & unpin);
              seen |= tk;
              run_ops[rlen++] = rop;
            }
            /* Preserve trailing runtime-indexed const-RAM ADD+LDR peephole. */
            if (rlen >= 2 && sh4_thumb_const_mask != 0u &&
                block_data_position + rlen < num_insns &&
                ((run_ops[rlen - 1] & 0xFE00u) == 0x1800u ||
                 (run_ops[rlen - 1] & 0xFF00u) == 0x4400u) &&
                ((pc + (u32)(rlen * 2)) & 0x0FFFu) != 0u) {
              u16 after_op = readaddress16(pc_address_block,
                                           (pc + (u32)(rlen * 2)) & 0x0FFF);
              if ((after_op & 0xF800u) == 0x7800u ||
                  (after_op & 0xF800u) == 0x8800u ||
                  (after_op & 0xF800u) == 0x6800u)
                rlen--;
            }
            if (rlen >= 2 && dup != 0u) {
              u32 wmask = 0u;
              if (sh4g_thumb_alu_run_native_const(&translation_ptr, run_ops,
                                                  rm_arr, wm_arr, rlen, &wmask,
                                                  &sh4_thumb_const_mask,
                                                  sh4_thumb_const_val)) {
                if (wmask & (1u << SH4_GREG_SP))
                  sp_span_valid = 0;
                pc += (u32)(rlen * 2);
                cycle_count += pc_seq_cycles * (u32)(rlen - 1);
                block_data_position += rlen - 1;
                last_opcode = run_ops[rlen - 2];
                opcode = run_ops[rlen - 1];
                sh4g_litseg_maybe_flush(&translation_ptr);
                thumb_fused = 1;
              }
            }
          }
        }
        if (!thumb_fused &&
            ((cgba_thumb_hi_lut[cur_op >> 8] & 0x4000u) ||
             (u32)(next_op - 0xD000u) < 0x0E00u ||
             ((u32)(cur_op - 0x6000u) < 0x3000u &&
              (u32)(next_op - 0x6000u) < 0x3000u &&
              ((cur_op >> 3) & 7u) == ((next_op >> 3) & 7u)))) {
          thumb_fused = cgba_thumb_try_peephole(
            &translation_ptr, cur_op, next_op,
            &pc, &cycle_count, &block_data_position, &block_exit_position,
            &last_opcode, &opcode,
            &sh4_thumb_const_mask, sh4_thumb_const_val,
            pc_address_block, pc_region,
            block_start_pc, block_end_pc,
            num_insns, total_exits, cheat_hook, ram_region);
        }
      }
      /* Unified proven-IWRAM-SP span for both isolated and consecutive
       * `LDR/STR rd, [sp, #imm]` (off <= 60). Validates canonical aligned
       * IWRAM SP + store SMC tags ONCE at the first SP access in a straight-
       * line span of >=2 SP accesses (deopting to cold interp on miss), then
       * batches any consecutive covered SP ops under a single `iwram_bias + SP`
       * pointer (5 insns/op, compile-time cycle fold, full reg forwarding). */
      if (!thumb_fused && allow_fuse &&
          (cur_op & 0xF000u) == 0x9000u &&
          ((cur_op & 0xFFu) <= 15u)) {
        u32 sp_off = (cur_op & 0xFFu) << 2;
        int sp_is_load = (int)((cur_op >> 11) & 1u);
        if (!sp_span_valid) {
          /* Forward scan across straight-line instructions up to any join
           * seam, branch/SWI, SP write, cheat hook, or 4 KiB page boundary. */
          s32 scan = block_data_position;
          u32 scan_pc = pc;
          int sp_cnt = 0, sp_has_st = 0;
          u32 max_o = 0u, st_wmask = 0u;
          while (scan < num_insns && (scan_pc >> 12) == pc_region &&
                 scan_pc != cheat_hook) {
            u32 sop = readaddress16(pc_address_block, scan_pc & 0x0FFFu);
            u32 shi = sop >> 8;
            if (scan > block_data_position && block_data[scan].update_cycles)
              break;
            if ((sop & 0xF000u) == 0x9000u) {
              u32 so = (sop & 0xFFu) << 2;
              if (so <= 60u) {
                sp_cnt++;
                if (so > max_o) max_o = so;
                if (!((sop >> 11) & 1u)) {
                  sp_has_st = 1;
                  st_wmask |= (1u << (so >> 2));
                }
              }
            } else if (shi == 0xB0u || shi == 0xB4u || shi == 0xB5u ||
                       shi == 0xBCu || shi == 0xBDu ||
                       (sop & 0xFF80u) == 0x4700u ||
                       ((shi == 0x44u || shi == 0x46u) &&
                        (((sop & 7u) | ((sop >> 4) & 8u)) == 13u)) ||
                       shi >= 0xD0u) {
              break; /* SP write, BX, branch, or SWI */
            }
            scan++;
            scan_pc += 2u;
          }
          if (sp_cnt >= 2) {
            u8 *deopt[3], *sp_ok;
            int nd = 0, pre_deb = (int)(cycle_count - pc_seq_cycles);
            sh4g_litseg_reserve_bytes(&translation_ptr, 7, 160u);
            sh4_greg_forward_reset();
            { sh4_codegen cg = sh4g_open(&translation_ptr);
              sh4_emit_load_greg(&cg, 13, SH4_REG_T0);  /* R1 = SP */
              sh4g_close(&translation_ptr, &cg); }
            sh4g_const(&translation_ptr, 0x03000000u, SH4_REG_RET);
            sh4g_const(&translation_ptr, 0x8000u - (max_o + 3u), SH4_REG_T1);
            { sh4_codegen cg = sh4g_open(&translation_ptr);
              sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_T0);
              sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);
              sh4g_close(&translation_ptr, &cg); }
            deopt[nd++] = sh4g_emit_bt_placeholder(&translation_ptr);
            { sh4_codegen cg = sh4g_open(&translation_ptr);
              sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
              sh4_emit_tst_imm(&cg, 3);
              sh4g_close(&translation_ptr, &cg); }
            deopt[nd++] = sh4g_emit_bf_placeholder(&translation_ptr);
            if (sp_has_st) {
              u8 *smc_ok;
              int first = 1, w;
              sh4g_const(&translation_ptr, (u32)(uintptr_t)&cgba_iwram_code_end,
                         SH4_REG_T2);
              { sh4_codegen cg = sh4g_open(&translation_ptr);
                sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
                sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);
                sh4g_close(&translation_ptr, &cg); }
              smc_ok = sh4g_emit_bt_placeholder(&translation_ptr);
              /* Fast bypass missed (e.g. high-IWRAM code above SP): check the
               * actual SMC tags for the stored stack words before deopting. */
              sh4g_const(&translation_ptr, (u32)(uintptr_t)iwram, SH4_REG_T2);
              { sh4_codegen cg = sh4g_open(&translation_ptr);
                sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);
                for (w = 0; w < 16; w++) {
                  if (!(st_wmask & (1u << w))) continue;
                  if (first) {
                    if (w == 0) sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_RET);
                    else        sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_RET, (unsigned)w);
                    first = 0;
                  } else {
                    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, (unsigned)w);
                    sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_RET);
                  }
                }
                sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);
                sh4g_close(&translation_ptr, &cg); }
              deopt[nd++] = sh4g_emit_bf_placeholder(&translation_ptr);
              sh4g_patch_cond(smc_ok, translation_ptr);
            }
            sp_ok = sh4g_emit_bra_placeholder(&translation_ptr);
            while (nd)
              sh4g_patch_cond(deopt[--nd], translation_ptr);
            if (pre_deb != 0)
              sh4g_cycle_debit(&translation_ptr, pre_deb);
            sh4g_const(&translation_ptr, (u32)pc, SH4_REG_RET);
            sh4g_store_greg(&translation_ptr, SH4_REG_RET, SH4_GREG_PC);
            sh4g_far_jmp(&translation_ptr, (const void *)sh4_cold_interp_entry);
            sh4g_patch_bra(sp_ok, translation_ptr);
            sp_span_valid = sp_has_st ? 2 : 1;
            sp_span_max_off = max_o;
            sp_span_store_wmask = st_wmask;
          }
        }
        if (sp_span_valid && sp_off <= sp_span_max_off &&
            (sp_is_load ||
             (sp_span_valid == 2 && (sp_span_store_wmask & (1u << (sp_off >> 2)))))) {
          u32 batch_ops[8];
          int rcnt = 1, k;
          u32 iwram_bias = (u32)(uintptr_t)(iwram + 0x8000) - 0x03000000u;
          u8 *forward_start;
          batch_ops[0] = cur_op;
          while (rcnt < 8 &&
                 block_data_position + rcnt < num_insns &&
                 !block_data[block_data_position + rcnt].update_cycles &&
                 (pc + (u32)(rcnt * 2)) != cheat_hook &&
                 ((pc + (u32)(rcnt * 2)) >> 12) == pc_region) {
            u32 rop = readaddress16(pc_address_block, (pc + (u32)(rcnt * 2)) & 0x0FFFu);
            u32 roff;
            int ris_load;
            if ((rop & 0xF000u) != 0x9000u)
              break;
            roff = (rop & 0xFFu) << 2;
            ris_load = (int)((rop >> 11) & 1u);
            if (roff > sp_span_max_off ||
                (!ris_load && !(sp_span_valid == 2 &&
                                (sp_span_store_wmask & (1u << (roff >> 2))))))
              break;
            batch_ops[rcnt++] = rop;
          }
          sh4g_litseg_reserve_bytes(&translation_ptr, 1, (u32)(16 + rcnt * 12));
          forward_start = translation_ptr;
          if (!sp_is_load) {
            /* Load first store value BEFORE iwram_bias so it can consume
             * forwarding from the immediately preceding instruction. */
            sh4_codegen cg = sh4g_open(&translation_ptr);
            sh4_emit_load_greg(&cg, (cur_op >> 8) & 7u, SH4_REG_T1);
            sh4g_close(&translation_ptr, &cg);
          }
          sh4g_const(&translation_ptr, iwram_bias, SH4_REG_RET);
          { sh4_codegen cg = sh4g_open(&translation_ptr);
            sh4_emit_load_greg(&cg, 13, SH4_REG_T2);    /* R3 = SP */
            sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);
            for (k = 0; k < rcnt; k++) {
              u32 opk = batch_ops[k];
              u32 w = opk & 0xFFu;
              unsigned rdk = (opk >> 8) & 7u;
              if ((opk >> 11) & 1u) {
                if (w == 0u) sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
                else         sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, w);
                sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
                sh4_emit_store_greg(&cg, SH4_REG_T1, rdk);
                sh4_thumb_const_kill(rdk);
              } else {
                if (k > 0)
                  sh4_emit_load_greg(&cg, rdk, SH4_REG_T1);
                sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
                if (w == 0u) sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
                else         sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, w);
              }
            }
            sh4g_close(&translation_ptr, &cg); }
          /* Fold fetch cycles for ops 1..rcnt-1 and rcnt IWRAM nseq cycles. */
          cycle_count += pc_seq_cycles * (u32)(rcnt - 1) + (u32)rcnt;
          if (((batch_ops[rcnt - 1] >> 11) & 1u) &&
              translation_ptr > forward_start && translation_ptr[-2] == 0x1Eu)
            sh4_greg_forward_finish(forward_start, translation_ptr, 1);
          else
            sh4_greg_forward_reset();
          last_opcode = (rcnt >= 2) ? batch_ops[rcnt - 2] : opcode;
          opcode = batch_ops[rcnt - 1];
          block_data_position += rcnt - 1;
          pc += (u32)(rcnt * 2);
          sh4g_litseg_maybe_flush(&translation_ptr);
          thumb_fused = 1;
        }
      }
      /* Invalidate proven-IWRAM-SP span on any SP write or branch/call/SWI. */
      if (sp_span_valid) {
        u32 cur_hi = cur_op >> 8;
        if (cur_hi == 0xB0u || cur_hi == 0xB4u || cur_hi == 0xB5u ||
            cur_hi == 0xBCu || cur_hi == 0xBDu ||
            (cur_op & 0xFF80u) == 0x4700u ||
            ((cur_hi == 0x44u || cur_hi == 0x46u) &&
             (((cur_op & 7u) | ((cur_op >> 4) & 8u)) == 13u)) ||
            (!thumb_fused && cur_hi >= 0xD0u) ||
            (thumb_fused && (opcode >> 8) >= 0xD0u)) {
          sp_span_valid = 0;
        }
      }
#endif
      if (!thumb_fused) {
        u8 *forward_start = translation_ptr;
        last_opcode = opcode;
        opcode = cur_op;
        translate_thumb_instruction_loaded();
        /* Fast reject: forwarding only activates when the last emitted SH-4
         * insn is MOV.L Rm,@(disp,R14) (high byte 0x1E). */
        if (__builtin_expect(translation_ptr > forward_start &&
                             translation_ptr[-2] == 0x1Eu, 0)) {
          sh4_greg_forward_finish(forward_start, translation_ptr,
            (!(flag_status & 15) &&
             ((opcode & 0xE000) == 0x0000 ||
              (opcode & 0xF800) == 0x1800 ||
              (opcode & 0xF800) == 0x2000 ||
              (opcode & 0xF000) == 0x3000 ||
              ((opcode & 0xFC00) == 0x4000 &&
               ((opcode >> 6) & 15) != 2 && ((opcode >> 6) & 15) != 3 &&
               ((opcode >> 6) & 15) != 4 && ((opcode >> 6) & 15) != 7))) ||
            (((opcode & 0xFF00) == 0x4400 || (opcode & 0xFF00) == 0x4600) &&
             (((opcode & 7) | ((opcode >> 4) & 8)) != 15)) ||
            (opcode & 0xF000) == 0xA000 ||
            (CGBA_SH4_COMPACT_FASTMEM_TUPLE &&
             cycle_count <= 0xFFFFu &&
             sh4g_thumb_ldst_is_load(opcode) &&
             ((opcode & 0xF800u) == 0x4800u ||
              (opcode & 0xF000u) == 0x5000u ||
              (opcode & 0xF000u) == 0x6000u ||
              (opcode & 0xF000u) == 0x7000u ||
              (opcode & 0xF000u) == 0x8000u ||
              (opcode & 0xF000u) == 0x9000u)));
        } else {
          sh4_greg_forward_reset();
        }
        sh4g_litseg_maybe_flush(&translation_ptr);
      }
    }
#else
    translate_thumb_instruction();
#endif
#ifdef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
    generate_cycle_update();
#endif
    block_data_position++;

    /* If it went too far the cache needs to be flushed and the process
       restarted. Because we might already be nested several stages in
       a simple recursive call here won't work, it has to pedal out to
       the beginning. */

#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
    if (!ram_region)
      translation_cache_limit = cgba_sh4_fm_ool_rom_limit(TRANSLATION_CACHE_LIMIT_THRESHOLD);
#endif
    if(translation_ptr > translation_cache_limit)
    {
      CGBA_DIAG_LOG("@@CGBA_XFLUSH thumb ram=%d start=%08lx pc=%08lx end=%08lx pos=%ld cap=%lu",
        (int)ram_region, (unsigned long)block_start_pc, (unsigned long)pc,
        (unsigned long)block_end_pc, (long)block_data_position,
        (unsigned long)CGBA_BLOCK_SCAN_CAP_EXPR);
      cgba_block_overflow_shrink(attempt_base, translation_ptr, ram_region);
#ifdef SH4_ARCH
      sh4_greg_forward_reset();
      sh4g_litseg_abort();
#endif
      if (ram_region) {
        cgba_dynarec_ram_full_count++;
        flush_translation_cache_ram();
      } else
        flush_translation_cache_rom();
      return false;
    }

    /* If the next instruction is a block entry point update the
       cycle counter and update */
#ifndef SH4_ARCH
    if (pc != block_end_pc &&
        block_data[block_data_position].update_cycles)
    {
      generate_cycle_update();
    }
#endif
  }

#ifdef SH4_ARCH
  sh4_greg_forward_reset();
  if (chain_existing_target != NULL) {
    cgba_sh4_emit_chain_existing(&translation_ptr, &cycle_count,
                                 chain_existing_target);
    ended_uncond = 1;
  }
  if (ended_uncond)
    sh4g_litseg_end_tail(&translation_ptr);
  else
    sh4g_litseg_end(&translation_ptr);
#endif

  /* See translate_block_arm: the gate is dead code after a clean
     unconditional-branch block end. */
  if (!ended_uncond)
    generate_translation_gate(thumb);

  for(i = 0; i < block_exit_position; i++)
  {
#ifdef SH4_ARCH
    if (!block_exits[i].branch_source)
      continue;
#endif
    branch_target = block_exits[i].branch_target;

    if((branch_target >= block_start_pc) && (branch_target < block_end_pc))
    {
      /* Internal branch, patch to recorded address */
#ifdef SH4_ARCH
      translation_target =
       block_data[(branch_target - block_start_pc) /
        thumb_instruction_width].body_offset;
      generate_branch_patch_internal(block_exits[i].branch_source,
       translation_target);
#else
      translation_target =
       block_data[(branch_target - block_start_pc) /
        thumb_instruction_width].block_offset;
      generate_branch_patch_unconditional(block_exits[i].branch_source,
       translation_target);
#endif
    }
    else
    {
      /* External branch, save for later */
      external_block_exits[external_block_exit_position].branch_target =
       branch_target;
      external_block_exits[external_block_exit_position].branch_source =
       block_exits[i].branch_source;
      external_block_exit_position++;
    }
  }

#ifdef SH4_ARCH
  /* Keep the cache cursor 4-aligned: the next block's hashhdr/ramtag and any
     leading literal tuples are written with 32-bit stores, and vec-jmp exits
     (JMP @R9-table / @R10) end on 2-byte granularity unlike the old
     literal-island exits which always ended at literal+4. */
  if ((uintptr_t)translation_ptr & 3) {
    *(u16 *)translation_ptr = 0x0009;             /* NOP pad (never executed) */
    translation_ptr += 2;
  }

  /* Register internal update_cycles seams in rom_branch_hash as 12-byte
     BRA-to-body_offset aliases so IRQ returns / external branches / later
     overlapping prefixes reuse this compiled tail instead of duplicating it. */
#if CGBA_SH4_THUMB_SEAM_ALIAS
  if (!ram_region && !CGBA_SH4_DIFF_ACTIVE() && block_start_pc >= 0x08000000u) {
    for (i = 1; i < total_insns; i++) {
      u8 *target_ptr;
      u32 seam_pc, key, hash_target, alias_off;
      u8 *seam_blk, *bra_site;
      u16 seam_op;
      long disp;
      if (!block_data[i].update_cycles || block_data[i].body_offset == NULL)
        continue;
      seam_pc = block_start_pc + (u32)i * thumb_instruction_width;
      if (idle_loop_target_pc != 0xFFFFFFFFu &&
          (u32)(seam_pc - (idle_loop_target_pc - 64u)) <= 128u)
        continue;
      seam_blk = cgba_memory_map_read_4k(seam_pc);
      seam_op = seam_blk ? readaddress16(seam_blk, seam_pc & 0x0FFFu) : 0xFFFFu;
      if (seam_op >= 0xF800u)
        continue;
      if (cgba_rom_hash_lookup_thumb(seam_pc) != NULL)
        continue;
      if (translation_ptr + sizeof(hashhdr_type) + 4 > translation_cache_limit)
        break;
      target_ptr = block_data[i].body_offset;
      bra_site = translation_ptr + sizeof(hashhdr_type);
      disp = ((long)(uintptr_t)target_ptr - ((long)(uintptr_t)bra_site + 4)) / 2;
      if (disp < -2048 || disp > 2047)
        continue;
      key = seam_pc | 1u;
      hash_target = ((key * 2654435761U) >> (32 - ROM_BRANCH_HASH_BITS))
                    & (ROM_BRANCH_HASH_SIZE - 1);
      alias_off = (u32)(translation_ptr - rom_translation_cache);
      {
        hashhdr_type *ahdr = (hashhdr_type *)translation_ptr;
        ahdr->pc_value = key;
        ahdr->next_entry = rom_branch_hash[hash_target];
        rom_branch_hash[hash_target] = alias_off;
      }
      translation_ptr += sizeof(hashhdr_type);
      sh4g_u16(&translation_ptr, (u16)(0xA000u | (disp & 0x0FFFu)));
      sh4g_u16(&translation_ptr, 0x0009u);
    }
  }
#endif
#endif
  if (ram_region)
    ram_translation_ptr = translation_ptr;
  else {
    /* THIS block's own emission (cursor delta incl. its hashhdr) — measured
       here, before external-exit resolution recursively translates other
       blocks, so nested emission isn't double-counted into the stat. */
    CGBA_EM_BLK_STAT((translation_ptr - rom_translation_ptr) +
                     sizeof(hashhdr_type), 1);
    rom_translation_ptr = translation_ptr;
  }

#ifdef SH4_ARCH
  /* See translate_block_arm: skip external-branch resolution in single-block
     diff mode so we translate only the entered block. */
  if (!CGBA_SH4_DIFF_ACTIVE())
#endif
  for(i = 0; i < external_block_exit_position; i++)
  {
    branch_target = external_block_exits[i].branch_target;
#ifdef SH4_ARCH
    /* See the ARM loop: BIOS targets incl. the SWI vector stay unpatched. */
    if (branch_target < 0x00004000u)
      continue;
#endif
    if(branch_target == 0x00000008)
      translation_target = bios_swi_entrypoint;
    else {
#ifdef SH4_ARCH
      cgba_cold_gate_probe++;          /* probe, don't heat (see the gate) */
      translation_target = block_lookup_translate_thumb(branch_target);
      cgba_cold_gate_probe--;
#else
      translation_target = block_lookup_translate_thumb(branch_target);
#endif
    }
#ifdef SH4_ARCH
    if (translation_target == (u8 *)(~(uintptr_t)0)) {
      /* Untranslatable-address sentinel: dispatch traps it loudly via
         cgba_sh4_wild_jump if the branch is ever taken; hard-patching it
         would jump to host 0xFFFFFFFF with no diagnostics. */
      continue;
    }
    if (!translation_target && cgba_cold_pending) {
      /* Cold-gate target: leave the exit unpatched — it keeps dispatching
         through sh4_block_exit and the gate decides later. Treating this
         as translate-failure wholesale-flushed the cache in a loop. */
      cgba_cold_pending = 0;
      continue;
    }
    if (translation_target && branch_target != 0x00000008) {
      /* Never chain across caches: flush_translation_cache_ram leaves the
         ROM cache in place (and vice versa), so a cross-cache direct chain
         dangles into freed memory once the other side flushes — EXC=180 at
         a host PC inside the arena, long after the flush (field crash with
         RAM=102k churn). Cross-cache branches keep dispatching through
         sh4_block_exit. */
      int tgt_ram = ((u8 *)translation_target >= ram_translation_cache &&
                     (u8 *)translation_target <
                       ram_translation_cache + RAM_TRANSLATION_CACHE_SIZE);
      if ((ram_region != 0) != tgt_ram)
        continue;
    }
#endif
    if (!translation_target)
      return false;
    generate_branch_patch_unconditional(
      external_block_exits[i].branch_source, translation_target);
  }
  return true;
}

void init_bios_hooks(void)
{
  // Pre-generate this entry point so that we can safely invoke fast
  // SWI calls from ROM and RAM regardless of cache flushes.
  rom_translation_ptr = &rom_translation_cache[rom_cache_watermark];
  last_rom_translation_ptr = rom_translation_ptr;
  bios_swi_entrypoint = block_lookup_address_arm(0x8);
  rom_cache_watermark = (u32)(rom_translation_ptr - rom_translation_cache);
}

void flush_translation_cache_ram(void)
{
  /* Flushes RAM caches avoiding doing too much work (ie. wiping unused memory) */
  flush_ram_count++;
  cgba_dynarec_ram_flush_count++;
#if defined(CGBA_GPSP_HEADLESS_TEST)
  CGBA_DIAG_LOG("@@CGBA_RAMFLUSH n=%lu pc=%08lx used=%lu iw=%04x-%04x",
    (unsigned long)cgba_dynarec_ram_flush_count, (unsigned long)reg[REG_PC],
    (unsigned long)(ram_translation_ptr - ram_translation_cache),
    (unsigned)(iwram_code_min & 0xFFFF), (unsigned)(cgba_iwram_code_end & 0xFFFF));
#endif

#ifdef SH4_ARCH
  last_ram_translation_ptr =
    CGBA_SH4_RAM_CODE_START(ram_translation_cache);
  ram_translation_ptr =
    CGBA_SH4_RAM_CODE_START(ram_translation_cache);
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
  cgba_sh4_fm_ool_reset_ram();
#endif
#else
  last_ram_translation_ptr = ram_translation_cache;
  ram_translation_ptr = ram_translation_cache;
#endif

  // Proceed to clean the SMC area if needed
  // (also try to memset as little as possible for performance)
  /* min == ~0U is the explicit unseen state; offset zero is valid code. */
  if (cgba_sh4_ram_code_seen(iwram_code_min) || cgba_iwram_code_end > 0U) {
    if (cgba_sh4_ram_code_seen(iwram_code_min) &&
        cgba_iwram_code_end > iwram_code_min) {
      u32 clr_min = iwram_code_min & ~15U;
      u32 clr_max = MIN(cgba_iwram_code_end, 0x8000U);
      memset(&iwram[clr_min], 0, clr_max - clr_min);
    } else
      memset(iwram, 0, 0x8000);
  }

  if (cgba_sh4_ram_code_seen(ewram_code_min) || cgba_ewram_code_end > 0U) {
    if (cgba_sh4_ram_code_seen(ewram_code_min) &&
        cgba_ewram_code_end > ewram_code_min) {
      u32 clr_min = ewram_code_min & ~15U;
      u32 clr_max = MIN(cgba_ewram_code_end, 0x40000U);
      memset(&ewram[0x40000 + clr_min], 0, clr_max - clr_min);
    } else
      memset(&ewram[0x40000], 0, 0x40000);
  }

  iwram_code_min = ~0U;
  cgba_iwram_code_end = 0U;
  ewram_code_min = ~0U;
  cgba_ewram_code_end = 0U;
  ram_block_tag = INITIAL_TOP_TAG;

  /* cgba_dynarec_dual_hot is populated strictly on rom_branch_hash hits
   * (sh4_stub.S), so it only ever holds ROM-cache pointers and is cleared
   * by flush_translation_cache_rom() — never invalidated by RAM flushes. */
}

#ifdef SH4_ARCH
u32 cgba_last_rom_flush_frame;   /* CGBA_COLD_HEAT's adaptive-increment clock */
#endif

#ifdef CGBA_GPSP_HEADLESS_TEST
/* Dump the ROM-cache block map (guest key -> arena offset of the hashhdr)
 * over the debug port, so a JIT-arena PC histogram (casio-emu HLE_PROFILE)
 * can be joined back to guest blocks offline. Emission is sequential, so a
 * block's extent ends at the next-higher header offset. Only the final
 * cache generation is described; runs with many ROM flushes will mix
 * generations (AW steady state has ~0 after warmup). */
void cgba_sh4_dump_rom_blockmap(void)
{
  volatile unsigned char *dbg = (volatile unsigned char *)0xb7000000u;
  static const char hexd[] = "0123456789ABCDEF";
  u32 i;
#define CGBA_DBG_PUTS(str)   do { const char *p_ = (str); while (*p_) *dbg = (unsigned char)*p_++; } while (0)
#define CGBA_DBG_HEX8(val)   do { u32 v_ = (val); int k_;        for (k_ = 7; k_ >= 0; k_--) *dbg = (unsigned char)hexd[(v_ >> (k_ * 4)) & 0xF]; } while (0)
  CGBA_DBG_PUTS("jit blkmap base=");
  CGBA_DBG_HEX8((u32)(uintptr_t)rom_translation_cache);
  CGBA_DBG_PUTS(" end=");
  CGBA_DBG_HEX8((u32)(uintptr_t)rom_translation_ptr);
  *dbg = '\n';
  for (i = 0; i < ROM_BRANCH_HASH_SIZE; i++) {
    u32 off = rom_branch_hash[i];
    while (off) {
      hashhdr_type *bhdr = (hashhdr_type *)&rom_translation_cache[off];
      CGBA_DBG_PUTS("jit blk ");
      CGBA_DBG_HEX8(bhdr->pc_value);
      *dbg = ' ';
      CGBA_DBG_HEX8(off);
      *dbg = '\n';
      off = bhdr->next_entry;
    }
  }
  CGBA_DBG_PUTS("jit blkmap done\n");
#undef CGBA_DBG_PUTS
#undef CGBA_DBG_HEX8
}
#endif

void flush_translation_cache_rom(void)
{
#ifdef SH4_ARCH
  /* Decay the cold-gate hot table. Leaky bucket (subtract a constant),
     NOT halving: with capacity flushes every ~57 frames, halving gives the
     fixed point h* = N (executions per epoch), so any block executed fewer
     than THRESHOLD times per epoch stays interpreted FOREVER — Metroid
     movement measured 54% of ALL time inside execute_arm on exactly that
     mid-warm population. Subtracting D instead translates every block
     executed more than D times per epoch eventually, while never-executed
     heat still decays to zero across a few flushes (same anti-feedback
     property that stopped the flush-thrash spiral). No-flush games never
     pay this. */
  {
    unsigned hi;
    for (hi = 0; hi < sizeof(cgba_hot_count); hi++) {
      u8 h = cgba_hot_count[hi];
      cgba_hot_count[hi] = (h > CGBA_SH4_HEAT_FLUSH_LEAK)
        ? (u8)(h - CGBA_SH4_HEAT_FLUSH_LEAK) : 0;
    }
  }
  cgba_last_rom_flush_frame = frame_counter;
#endif
  /* We flush the generated code except for everything below the watermark. */
  cgba_dynarec_rom_flush_count++;
#if defined(CGBA_GPSP_HEADLESS_TEST)
  CGBA_DIAG_LOG("@@CGBA_ROMFLUSH n=%lu pc=%08lx used=%lu",
    (unsigned long)cgba_dynarec_rom_flush_count, (unsigned long)reg[REG_PC],
    (unsigned long)(rom_translation_ptr - rom_translation_cache));
#endif

  last_rom_translation_ptr = &rom_translation_cache[rom_cache_watermark];
  rom_translation_ptr      = &rom_translation_cache[rom_cache_watermark];
#if defined(SH4_ARCH) && CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
  cgba_sh4_fm_ool_reset_rom();
#endif
  memset(rom_branch_hash, 0, sizeof(rom_branch_hash));
  memset(cgba_dynarec_arm_hot, 0, sizeof(cgba_dynarec_arm_hot));
  memset(cgba_dynarec_thumb_hot, 0, sizeof(cgba_dynarec_thumb_hot));
  memset(cgba_dynarec_dual_hot, 0, sizeof(cgba_dynarec_dual_hot));
}

void init_dynarec_caches(void)
{
#ifdef SH4_ARCH
  cgba_sh4_fastmem_init();     /* resident ldst fast paths (sh4_fastmem.c) */
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
  cgba_sh4_fm_ool_reset_all();
#endif
  memset(cgba_hot_count, 0, sizeof(cgba_hot_count));
  cgba_cold_pending = 0;
#endif
#ifdef SH4_ARCH
  cgba_sh4_scan_caps_reset(&cgba_ram_block_scan_cap,
    &cgba_rom_block_scan_cap, CGBA_SH4_RAM_SCAN_CAP_INITIAL,
    CGBA_SH4_ROM_SCAN_CAP_INITIAL);
#else
  cgba_block_scan_cap = MAX_BLOCK_SIZE;
#endif
  /* Initialize caches so that we can start initalizing the emitter. */
  rom_translation_ptr = last_rom_translation_ptr = &rom_translation_cache[0];
  memset(rom_branch_hash, 0, sizeof(rom_branch_hash));
  memset(cgba_dynarec_arm_hot, 0, sizeof(cgba_dynarec_arm_hot));
  memset(cgba_dynarec_thumb_hot, 0, sizeof(cgba_dynarec_thumb_hot));
  memset(cgba_dynarec_dual_hot, 0, sizeof(cgba_dynarec_dual_hot));

#ifdef SH4_ARCH
  ram_translation_ptr = last_ram_translation_ptr =
    CGBA_SH4_RAM_CODE_START(ram_translation_cache);
#else
  ram_translation_ptr = last_ram_translation_ptr = &ram_translation_cache[0];
#endif
  memset(iwram, 0, 0x8000);
  memset(&ewram[0x40000], 0, 0x40000);

  ewram_code_min = ~0U;
  cgba_ewram_code_end = 0U;
  iwram_code_min = ~0U;
  cgba_iwram_code_end = 0U;
  ram_block_tag = INITIAL_TOP_TAG;
  cgba_sh4_reset_iwram_smc_quarantine();
}

void flush_dynarec_caches(void)
{
  /* Flush ROM and RAM caches (force a full wipe of both RAM tag tables). */
  cgba_sh4_reset_iwram_smc_quarantine();
  flush_translation_cache_rom();
  ewram_code_min = 0;
  cgba_ewram_code_end = 0x40000U;
  iwram_code_min = 0;
  cgba_iwram_code_end = 0x8000U;
  flush_translation_cache_ram();
}
