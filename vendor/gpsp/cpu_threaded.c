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
u8 *ram_translation_ptr = ram_translation_cache;
#endif
/* Note, see stub files for more cache definitions */

u32 iwram_code_min = ~0U;
u32 iwram_code_max =  0U;
u32 ewram_code_min = ~0U;
u32 ewram_code_max =  0U;

#define INITIAL_ROM_WATERMARK   16   // To avoid NULL aliasing
u32 rom_cache_watermark = INITIAL_ROM_WATERMARK;

u8 *bios_swi_entrypoint = NULL;

#ifdef SH4_ARCH
u32 cgba_hle_bios_irq_entry(void);   /* sh4_interp_helpers.c */
u32 cgba_hle_bios_irq_exit(void);
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
u32 cgba_dynarec_dual_hot_key[64] CGBA_HIGH_BSS;
u32 cgba_dynarec_dual_hot_ptr[64] CGBA_HIGH_BSS;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
#define CGBA_SH4_PROF_BITS 11
#define CGBA_SH4_PROF_SLOTS (1u << CGBA_SH4_PROF_BITS)
struct cgba_sh4_prof_row {
  u32 key;
  u32 count;
};

u32 cgba_dynarec_rom_flush_count;
u32 cgba_dynarec_ram_flush_count;
u32 cgba_dynarec_arm_translate_count;
u32 cgba_dynarec_thumb_translate_count;
u32 cgba_dynarec_lookup_arm_count;
u32 cgba_dynarec_lookup_thumb_count;
u32 cgba_dynarec_lookup_dual_count;
u32 cgba_dynarec_icache_sync_count;
u32 cgba_dynarec_icache_sync_bytes;
u32 cgba_dynarec_ibh_arm_hit_count;
u32 cgba_dynarec_ibh_arm_slow_count;
u32 cgba_dynarec_ibh_thumb_hit_count;
u32 cgba_dynarec_ibh_thumb_slow_count;
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
  u16 flag_data;
  u8 condition;
  u8 update_cycles;
} block_data_type;

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
  #include "sh4/sh4_emit.h"
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


#define check_pc_region(pc)                                                   \
  new_pc_region = (pc >> 15);                                                 \
  if(new_pc_region != pc_region)                                              \
  {                                                                           \
    pc_region = new_pc_region;                                                \
    pc_address_block = memory_map_read[new_pc_region];                        \
                                                                              \
    if(!pc_address_block)                                                     \
      pc_address_block = load_gamepak_page(pc_region & 0x3FF);                \
  }                                                                           \

#define translate_arm_instruction()                                           \
  arm_load_flag_status()                                                      \
  check_pc_region(pc);                                                        \
  opcode = readaddress32(pc_address_block, (pc & 0x7FFF));                    \
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
  flag_status = block_data[block_data_position].flag_data;                    \
  check_pc_region(pc);                                                        \
  last_opcode = opcode;                                                       \
  opcode = readaddress16(pc_address_block, (pc & 0x7FFF));                    \
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
        /* ROM + same page -> optimize as const load */                       \
        if (!ram_region && (((aoff + 4) >> 15) == (pc >> 15))) {              \
          u32 value = readaddress32(pc_address_block, (aoff & 0x7FFF));       \
          thumb_load_pc_pool_const(rdreg, value);                             \
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

#define thumb_flag_status()                                                   \
{                                                                             \
  u16 flag_status = 0;                                                        \
  switch((opcode >> 8) & 0xFF)                                                \
  {                                                                           \
    /* left shift by imm */                                                   \
    case 0x00 ... 0x07:                                                       \
      thumb_flag_modifies_zn();                                               \
      if(((opcode >> 6) & 0x1F) != 0)                                         \
      {                                                                       \
        thumb_flag_modifies_c();                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    /* right shift by imm */                                                  \
    case 0x08 ... 0x17:                                                       \
      thumb_flag_modifies_znc();                                              \
      break;                                                                  \
                                                                              \
    /* add, subtract */                                                       \
    case 0x18 ... 0x1F:                                                       \
      thumb_flag_modifies_all();                                              \
      break;                                                                  \
                                                                              \
    /* mov reg, imm */                                                        \
    case 0x20 ... 0x27:                                                       \
      thumb_flag_modifies_zn();                                               \
      break;                                                                  \
                                                                              \
    /* cmp reg, imm; add, subtract */                                         \
    case 0x28 ... 0x3F:                                                       \
      thumb_flag_modifies_all();                                              \
      break;                                                                  \
                                                                              \
    case 0x40:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* AND rd, rs */                                                    \
          thumb_flag_modifies_zn();                                           \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* EOR rd, rs */                                                    \
          thumb_flag_modifies_zn();                                           \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* LSL rd, rs */                                                    \
          thumb_flag_modifies_zn_maybe_c();                                   \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* LSR rd, rs */                                                    \
          thumb_flag_modifies_zn_maybe_c();                                   \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    case 0x41:                                                                \
      switch((opcode >> 6) & 0x03)                                            \
      {                                                                       \
        case 0x00:                                                            \
          /* ASR rd, rs */                                                    \
          thumb_flag_modifies_zn_maybe_c();                                   \
          break;                                                              \
                                                                              \
        case 0x01:                                                            \
          /* ADC rd, rs */                                                    \
          thumb_flag_modifies_all();                                          \
          thumb_flag_requires_c();                                            \
          break;                                                              \
                                                                              \
        case 0x02:                                                            \
          /* SBC rd, rs */                                                    \
          thumb_flag_modifies_all();                                          \
          thumb_flag_requires_c();                                            \
          break;                                                              \
                                                                              \
        case 0x03:                                                            \
          /* ROR rd, rs */                                                    \
          thumb_flag_modifies_zn_maybe_c();                                   \
          break;                                                              \
      }                                                                       \
      break;                                                                  \
                                                                              \
    /* TST, NEG, CMP, CMN */                                                  \
    case 0x42:                                                                \
      if(((opcode >> 6) & 0x03) == 0)                                         \
        /* TST — N/Z only; C and V are architecturally PRESERVED. The         \
           historical modifies_all was an upstream scan bug: its bogus        \
           must-C/V killed live C/V producers above a TST once a backend      \
           actually honored flag_data (ADDS; TST; BCS broke). */              \
        thumb_flag_modifies_zn();                                             \
      else                                                                    \
        thumb_flag_modifies_all();                                            \
      break;                                                                  \
                                                                              \
    /* ORR, MUL, BIC, MVN */                                                  \
    case 0x43:                                                                \
      thumb_flag_modifies_zn();                                               \
      break;                                                                  \
                                                                              \
    /* add hi: flag-transparent, EXCEPT ADD pc, rs — a runtime PC-changer     \
       (jump-table idiom) that leaves the block, so all flags must be live    \
       across it (upstream scan missed this; fall through if rd==pc). */      \
    case 0x44:                                                                \
      if((opcode & 0x87) != 0x87)                                             \
        break;                                                                \
      thumb_flag_requires_all();                                              \
      break;                                                                  \
                                                                              \
    case 0x45:                                                                \
      /* CMP rd, rs */                                                        \
      thumb_flag_modifies_all();                                              \
      break;                                                                  \
                                                                              \
    /* mov might change PC (fall through if so) */                            \
    case 0x46:                                                                \
      if((opcode & 0xFF87) != 0x4687)                                         \
        break;                                                                \
                                                                              \
    /* branches (can change PC) */                                            \
    case 0x47:                                                                \
    case 0xBD:                                                                \
    case 0xD0 ... 0xE7:                                                       \
    case 0xF0 ... 0xFF:                                                       \
      thumb_flag_requires_all();                                              \
      break;                                                                  \
  }                                                                           \
  block_data[block_data_position].flag_data = flag_status;                    \
}                                                                             \

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

#define allocate_tag_thumb(location) { \
  location[0] = ram_block_tag;         \
  ram_block_tag -= 2;                  \
}

typedef struct
{
  u32 offset_arm;     // Cache offset to the ARM-mode compiled block
  u32 offset_thumb;   // Cache offset to the Thumb-mode compiled block
} ramtag_type;

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
#define CGBA_EM_BLK_STAT(nbytes) \
  (cgba_em_blk_n++, cgba_em_blk_bytes += (unsigned long)(nbytes))
#else
#define CGBA_EM_BLK_STAT(nbytes) ((void)0)
#endif

#ifndef CGBA_SH4_HOT_THRESHOLD
#define CGBA_SH4_HOT_THRESHOLD 64
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

#define block_lookup_translate_builder(type)                                  \
u8 function_cc *block_lookup_translate_##type(u32 pc)                         \
{                                                                             \
  u8 pcregion = (pc >> 24);                                                   \
  u16 *location;                                                              \
  u32 block_tag;                                                              \
                                                                              \
  block_lookup_address_pc_##type();                                           \
                                                                              \
  switch(pcregion)                                                            \
  {                                                                           \
    case 0x2:                                                                 \
    case 0x3:                                                                 \
    {                                                                         \
      u16* tagp = (pcregion == 2) ? (u16 *)(ewram + (pc & 0x3FFFF) + 0x40000) \
                                  : (u16 *)(iwram + (pc & 0x7FFF));           \
      ramtag_type* trentry;                                                   \
      /* Allocate a tag if not a valid one, and initialize header */          \
      if (!VALID_TAG(*tagp)) {                                                \
        allocate_tag_##type(tagp);                                            \
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
        if(bhdr->pc_value == key)                                             \
          return &rom_translation_cache[                                      \
                  blk_offset + sizeof(hashhdr_type) + block_prologue_size];   \
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
         !cgba_dynarec_single_block) {                                        \
        u32 hot_idx = (key * 2654435761U) >> 18;                              \
        if(cgba_hot_count[hot_idx] < CGBA_SH4_HOT_THRESHOLD) {                \
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
     !cgba_dynarec_single_block) {
    u32 handler = cgba_hle_bios_irq_entry();
    if(handler != 0)
      pc = handler;                /* resolve the game handler directly */
  }
  else if(pc == 0x00000030u && reg[CPU_MODE] == MODE_IRQ &&
          !cgba_dynarec_single_block) {
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
  opcode = readaddress32(pc_address_block, (block_end_pc & 0x7FFF));          \
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
  (((opcode >= 0xD000) && (opcode < 0xDF00)) ||                               \
   (((opcode & 0xFF00) == 0xDF00) &&                                          \
    (!is_div_swi(opcode & 0xFF))) ||                                          \
   ((opcode >= 0xE000) && (opcode < 0xE800)) ||                               \
   ((opcode & 0xFF00) == 0x4700) ||                                           \
   ((opcode & 0xFF00) == 0xBD00) ||                                           \
   ((opcode & 0xFF87) == 0x4687) ||                                           \
   ((opcode >= 0xF800)))                                                      \

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
  opcode = readaddress16(pc_address_block, (block_end_pc & 0x7FFF));          \
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

/* Adaptive scan cap: a single block whose EMITTED code exceeds the whole
 * (freshly flushed) translation cache would flush+retranslate forever — the
 * "went too far, pedal out to the beginning" retry assumes a block always
 * fits an empty cache. That assumption fails on this port: SimCity 2000
 * copies a 724-instruction LDM/STM-heavy renderer into IWRAM whose SH4
 * emission (~480 bytes/insn worst case) can never fit the RAM cache, so the
 * JIT livelocked (emulator) or spun until the guest went wild (hardware,
 * EXC=040 crash report). When an oversized attempt overflows, halve the cap
 * so the retry ends the block early through the fall-through translation
 * gate (a legal "size-limit" block end, same as the MAX_BLOCK_SIZE stop).
 * Sticky for the session; reset on init_dynarec_caches (ROM load/reset).
 *
 * The shrink keys off the bytes THIS attempt emitted, not off starting at
 * the cache base: the oversized block is usually reached recursively while
 * resolving another block's external exits, so its attempts never begin in
 * an empty cache. Any single block emitting more than a quarter of its
 * cache is capped, which both guarantees it can fit and leaves room for
 * the rest of the working set. */
static u32 cgba_block_scan_cap = MAX_BLOCK_SIZE;

/* DIAG bisect knob: -DCGBA_BLOCK_SCAN_CAP_DISABLE reverts scan_block to the
 * fixed MAX_BLOCK_SIZE stop (cap machinery inert) to isolate the cap. */
#ifdef CGBA_BLOCK_SCAN_CAP_DISABLE
#define CGBA_BLOCK_SCAN_CAP_EXPR ((u32)MAX_BLOCK_SIZE)
#else
#define CGBA_BLOCK_SCAN_CAP_EXPR cgba_block_scan_cap
#endif

static void cgba_block_overflow_shrink(u8 *attempt_base, u8 *attempt_end,
  bool ram_region)
{
  u32 cache_size = ram_region ? RAM_TRANSLATION_CACHE_SIZE
                              : ROM_TRANSLATION_CACHE_SIZE;
  if((u32)(attempt_end - attempt_base) > cache_size / 4 &&
     cgba_block_scan_cap > 32)
    cgba_block_scan_cap >>= 1;
}


block_data_type block_data[MAX_BLOCK_SIZE];
block_exit_type block_exits[MAX_EXITS];

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
  (cgba_dynarec_single_block && (start) >= 0xB5Cu && (start) < 0xC10u)
#else
#define CGBA_DIAG_ONE_INSN_BLOCK(start) ((void)(start), 0)
#endif

#define smc_write_arm_yes() {                                                 \
  intptr_t offset = (pc < 0x03000000) ? 0x40000 : -0x8000;                    \
  if(address32(pc_address_block, (block_end_pc & 0x7FFF) + offset) == 0)      \
  {                                                                           \
    address32(pc_address_block, (block_end_pc & 0x7FFF) + offset) =           \
      CODE_TAG_BLOCK32;                                                       \
  }                                                                           \
}

#define smc_write_thumb_yes() {                                               \
  intptr_t offset = (pc < 0x03000000) ? 0x40000 : -0x8000;                    \
  if(address16(pc_address_block, (block_end_pc & 0x7FFF) + offset) == 0)      \
  {                                                                           \
    address16(pc_address_block, (block_end_pc & 0x7FFF) + offset) =           \
      CODE_TAG_BLOCK16;                                                       \
  }                                                                           \
}

#define smc_write_arm_no()                                                    \

#define smc_write_thumb_no()                                                  \

#define scan_block(type, smc_write_op)                                        \
{                                                                             \
  __label__ block_end;                                                        \
  u32 cgba_blk_start = block_end_pc;  /* DIAG: block start PC */              \
  /* Find the end of the block */                                             \
  do                                                                          \
  {                                                                           \
    check_pc_region(block_end_pc);                                            \
    smc_write_##type##_##smc_write_op();                                      \
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
    for(i = 0; i < translation_gate_targets; i++)                             \
    {                                                                         \
      if(block_end_pc == translation_gate_target_pc[i])                       \
        goto block_end;                                                       \
    }                                                                         \
                                                                              \
    block_data_position++;                                                    \
    if(((u32)block_data_position >= CGBA_BLOCK_SCAN_CAP_EXPR) ||               \
     CGBA_DIAG_ONE_INSN_BLOCK(cgba_blk_start) ||                             \
     (block_end_pc == 0x3007FF0) || (block_end_pc == 0x203FFFF0))             \
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

#define update_pc_limits()                                                    \
if (ram_region) {                                                             \
  if (pc >= 0x3000000) {                                                      \
    iwram_code_min = MIN(pc & 0x7FFF, iwram_code_min);                        \
    iwram_code_max = MAX(pc & 0x7FFF, iwram_code_max);                        \
  } else {                                                                    \
    ewram_code_min = MIN(pc & 0x3FFFF, ewram_code_min);                       \
    ewram_code_max = MAX(pc & 0x3FFFF, ewram_code_max);                       \
  }                                                                           \
}                                                                             \

bool translate_block_arm(u32 pc, bool ram_region)
{
  u32 ended_uncond = 0;
  u32 opcode = 0;
  u32 last_opcode;
  u32 condition;
  u32 last_condition;
  u32 pc_region = (pc >> 15);
  u32 new_pc_region;
  u8 *pc_address_block = memory_map_read[pc_region];
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
  s32 i;
  u32 flag_status;
  block_exit_type external_block_exits[MAX_EXITS];
  generate_block_extra_vars_arm();

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_arm_translate_count++;
#endif

  arm_fix_pc();

  if(!pc_address_block)
    pc_address_block = load_gamepak_page(pc_region & 0x3FF);

  if (ram_region) {
    translation_ptr = ram_translation_ptr;
    translation_cache_limit = &ram_translation_cache[
       RAM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD
       - (0x10000 - ram_block_tag) / 2 * sizeof(ramtag_type)];
  } else {
    translation_ptr = rom_translation_ptr;
    translation_cache_limit =
     rom_translation_cache + ROM_TRANSLATION_CACHE_SIZE -
     TRANSLATION_CACHE_LIMIT_THRESHOLD;
  }
  attempt_base = translation_ptr;

  generate_block_prologue();

  /* This is a function because it's used a lot more than it might seem (all
     of the data processing functions can access it), and its expansion was
     massacreing the compiler. */

  if(ram_region)
  {
    scan_block(arm, yes);
  }
  else
  {
    scan_block(arm, no);
  }

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
      block_data[(branch_target - block_start_pc) /
       arm_instruction_width].update_cycles = 1;
#ifdef SH4_ARCH
      /* The SH4 port emits an update_gba-capable accounting flush + cycle
         gate AT this position; an IRQ raised there latches CPSR into
         SPSR_irq, so all flags must be live entering the seam — the linear
         eliminate pass cannot see this exit on its own. */
      block_data[(branch_target - block_start_pc) /
       arm_instruction_width].flag_data |= 0xF00;
#endif
    }
  }

  arm_dead_flag_eliminate();

  block_exit_position = 0;
  block_data_position = 0;

  last_condition = 0x0E;

  while(pc != block_end_pc)
  {
#ifdef SH4_ARCH
    /* Block-entry (branch target). Force-close any open ARM conditional block so
     * the flush is UNCONDITIONAL (fall-through must always charge+zero the
     * pre-entry cycles, even when the surrounding condition is false). The flush
     * sits before block_offset so a loop-back jump bypasses it (no re-charge);
     * a gate-only check sits AT block_offset so loop-back can still break a spin. */
    if(block_data[block_data_position].update_cycles)
    {
      if((last_condition & 0x0F) != 0x0E)
      {
        generate_branch_patch_conditional(backpatch_address, translation_ptr);
        last_condition = 0x0E;
      }
      generate_cycle_update();
      block_data[block_data_position].block_offset = translation_ptr;
      generate_prof_block_entry(0);
      generate_cycle_gate(1);
    }
    else
    {
      block_data[block_data_position].block_offset = translation_ptr;
      if(block_data_position == 0)
        generate_prof_block_entry(0);
    }
#else
    block_data[block_data_position].block_offset = translation_ptr;
#endif
    arm_base_cycles();

    if (pc == cheat_master_hook)
    {
      arm_process_cheats();
    }

    update_pc_limits();
    translate_arm_instruction();
#ifdef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
    generate_cycle_update();
#endif
    block_data_position++;

    /* If it went too far the cache needs to be flushed and the process
       restarted. Because we might already be nested several stages in
       a simple recursive call here won't work, it has to pedal out to
       the beginning. */

    if(translation_ptr > translation_cache_limit) {
      CGBA_DIAG_LOG("@@CGBA_XFLUSH arm ram=%d start=%08lx pc=%08lx end=%08lx pos=%ld cap=%lu",
        (int)ram_region, (unsigned long)block_start_pc, (unsigned long)pc,
        (unsigned long)block_end_pc, (long)block_data_position,
        (unsigned long)cgba_block_scan_cap);
      cgba_block_overflow_shrink(attempt_base, translation_ptr, ram_region);
      if (ram_region)
        flush_translation_cache_ram();
      else
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
    branch_target = block_exits[i].branch_target;

    if((branch_target >= block_start_pc) && (branch_target < block_end_pc))
    {
      /* Internal branch, patch to recorded address */
      translation_target =
       block_data[(branch_target - block_start_pc) /
        arm_instruction_width].block_offset;

#ifdef SH4_ARCH
      generate_branch_patch_internal(block_exits[i].branch_source,
       translation_target);
#else
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
                     sizeof(hashhdr_type));
    rom_translation_ptr = translation_ptr;
  }

#ifdef SH4_ARCH
  /* Single-block diff mode: skip external-branch resolution to avoid gpSP's
     recursive translate-the-whole-reachable-graph cascade; unresolved exits
     simply redispatch through sh4_block_exit. */
  if (!cgba_dynarec_single_block)
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

bool translate_block_thumb(u32 pc, bool ram_region)
{
  u32 ended_uncond = 0;
  u32 opcode = 0;
  u32 last_opcode;
  u32 condition;
  u32 pc_region = (pc >> 15);
  u32 new_pc_region;
  u8 *pc_address_block = memory_map_read[pc_region];
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
  s32 i;
  u32 flag_status;
  block_exit_type external_block_exits[MAX_EXITS];
  generate_block_extra_vars_thumb();

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_thumb_translate_count++;
#endif

  thumb_fix_pc();

  if(!pc_address_block)
    pc_address_block = load_gamepak_page(pc_region & 0x3FF);

  if (ram_region) {
    translation_ptr = ram_translation_ptr;
    translation_cache_limit = &ram_translation_cache[
       RAM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD
       - (0x10000 - ram_block_tag) / 2 * sizeof(ramtag_type)];
  } else {
    translation_ptr = rom_translation_ptr;
    translation_cache_limit = &rom_translation_cache[
       ROM_TRANSLATION_CACHE_SIZE - TRANSLATION_CACHE_LIMIT_THRESHOLD];
  }
  attempt_base = translation_ptr;

  generate_block_prologue();

  /* This is a function because it's used a lot more than it might seem (all
     of the data processing functions can access it), and its expansion was
     massacreing the compiler. */

  if(ram_region)
  {
    scan_block(thumb, yes);
  }
  else
  {
    scan_block(thumb, no);
  }

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
      block_data[(branch_target - block_start_pc) /
       thumb_instruction_width].update_cycles = 1;
#ifdef SH4_ARCH
      /* Same as the ARM loop: the flush+gate at this seam can take an IRQ
         with CPSR architecturally observable — keep flags live across it. */
      block_data[(branch_target - block_start_pc) /
       thumb_instruction_width].flag_data |= 0xF00;
#endif
    }
  }

  thumb_dead_flag_eliminate();

  block_exit_position = 0;
  block_data_position = 0;

  while(pc != block_end_pc)
  {
#ifdef SH4_ARCH
    /* Block-entry: flush before block_offset (loop-back bypasses it), gate-only
     * AT block_offset (loop-back hits it). Thumb has no conditional runs, so no
     * force-close is needed -- the flush is already unconditional here. */
    if(block_data[block_data_position].update_cycles)
    {
      generate_cycle_update();
      block_data[block_data_position].block_offset = translation_ptr;
      sh4_thumb_const_clear_all();
      generate_prof_block_entry(1);
      generate_cycle_gate(0);
    }
    else
    {
      block_data[block_data_position].block_offset = translation_ptr;
      if(block_data_position == 0)
        generate_prof_block_entry(1);
    }
#else
    block_data[block_data_position].block_offset = translation_ptr;
#endif
    thumb_base_cycles();

    if (pc == cheat_master_hook)
    {
      thumb_process_cheats();
    }

    update_pc_limits();
    translate_thumb_instruction();
#ifdef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
    generate_cycle_update();
#endif
    block_data_position++;

    /* If it went too far the cache needs to be flushed and the process
       restarted. Because we might already be nested several stages in
       a simple recursive call here won't work, it has to pedal out to
       the beginning. */

    if(translation_ptr > translation_cache_limit)
    {
      CGBA_DIAG_LOG("@@CGBA_XFLUSH thumb ram=%d start=%08lx pc=%08lx end=%08lx pos=%ld cap=%lu",
        (int)ram_region, (unsigned long)block_start_pc, (unsigned long)pc,
        (unsigned long)block_end_pc, (long)block_data_position,
        (unsigned long)cgba_block_scan_cap);
      cgba_block_overflow_shrink(attempt_base, translation_ptr, ram_region);
      if (ram_region)
        flush_translation_cache_ram();
      else
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

  /* See translate_block_arm: the gate is dead code after a clean
     unconditional-branch block end. */
  if (!ended_uncond)
    generate_translation_gate(thumb);

  for(i = 0; i < block_exit_position; i++)
  {
    branch_target = block_exits[i].branch_target;

    if((branch_target >= block_start_pc) && (branch_target < block_end_pc))
    {
      /* Internal branch, patch to recorded address */
      translation_target =
       block_data[(branch_target - block_start_pc) /
        thumb_instruction_width].block_offset;

#ifdef SH4_ARCH
      generate_branch_patch_internal(block_exits[i].branch_source,
       translation_target);
#else
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
                     sizeof(hashhdr_type));
    rom_translation_ptr = translation_ptr;
  }

#ifdef SH4_ARCH
  /* See translate_block_arm: skip external-branch resolution in single-block
     diff mode so we translate only the entered block. */
  if (!cgba_dynarec_single_block)
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
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_ram_flush_count++;
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST)
  CGBA_DIAG_LOG("@@CGBA_RAMFLUSH n=%lu pc=%08lx used=%lu iw=%04x-%04x",
    (unsigned long)cgba_dynarec_ram_flush_count, (unsigned long)reg[REG_PC],
    (unsigned long)(ram_translation_ptr - ram_translation_cache),
    (unsigned)(iwram_code_min & 0xFFFF), (unsigned)(iwram_code_max & 0xFFFF));
#endif
  /*printf("ram flush %d (pc %x), %x to %x, %x to %x\n",
   flush_ram_count, reg[REG_PC], iwram_code_min, iwram_code_max,
   ewram_code_min, ewram_code_max);*/

  last_ram_translation_ptr = ram_translation_cache;
  ram_translation_ptr = ram_translation_cache;

  // Proceed to clean the SMC area if needed
  // (also try to memset as little as possible for performance)
  if (iwram_code_max) {
    if(iwram_code_max > iwram_code_min) {
      iwram_code_min &= ~15U;
      iwram_code_max = MIN(iwram_code_max + 8, 0x8000);
      memset(&iwram[iwram_code_min], 0, iwram_code_max - iwram_code_min);
    } else
      memset(iwram, 0, 0x8000);
  }

  if (ewram_code_max) {
    if(ewram_code_max > ewram_code_min) {
      ewram_code_min &= ~15U;
      ewram_code_max = MIN(ewram_code_max + 8, 0x40000);
      memset(&ewram[0x40000 + ewram_code_min], 0, ewram_code_max - ewram_code_min);
    } else
      memset(&ewram[0x40000], 0, 0x40000);
  }

  iwram_code_min = ~0U;
  iwram_code_max =  0U;
  ewram_code_min = ~0U;
  ewram_code_max =  0U;
  ram_block_tag = INITIAL_TOP_TAG;

  /* The dual-hot dispatch table (sh4_stub.S) caches RESOLVED HOST POINTERS,
   * including RAM-cache blocks (Thumb returns into IWRAM/EWRAM code). Leaving
   * them across a RAM flush hands the stub a stale pointer into rewritten
   * cache memory — a wild jump long after the flush. ROM flush and init
   * already clear it; this path was the gap. */
  memset(cgba_dynarec_dual_hot_key, 0, sizeof(cgba_dynarec_dual_hot_key));
  memset(cgba_dynarec_dual_hot_ptr, 0, sizeof(cgba_dynarec_dual_hot_ptr));
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
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_dynarec_rom_flush_count++;
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST)
  CGBA_DIAG_LOG("@@CGBA_ROMFLUSH n=%lu pc=%08lx used=%lu",
    (unsigned long)cgba_dynarec_rom_flush_count, (unsigned long)reg[REG_PC],
    (unsigned long)(rom_translation_ptr - rom_translation_cache));
#endif

  last_rom_translation_ptr = &rom_translation_cache[rom_cache_watermark];
  rom_translation_ptr      = &rom_translation_cache[rom_cache_watermark];

  memset(rom_branch_hash, 0, sizeof(rom_branch_hash));
  memset(cgba_dynarec_dual_hot_key, 0, sizeof(cgba_dynarec_dual_hot_key));
  memset(cgba_dynarec_dual_hot_ptr, 0, sizeof(cgba_dynarec_dual_hot_ptr));
}

void init_dynarec_caches(void)
{
#ifdef SH4_ARCH
  cgba_sh4_fastmem_init();     /* resident ldst fast paths (sh4_fastmem.c) */
  memset(cgba_hot_count, 0, sizeof(cgba_hot_count));
  cgba_cold_pending = 0;
#endif
  cgba_block_scan_cap = MAX_BLOCK_SIZE;
  /* Initialize caches so that we can start initalizing the emitter. */
  rom_translation_ptr = last_rom_translation_ptr = &rom_translation_cache[0];
  memset(rom_branch_hash, 0, sizeof(rom_branch_hash));
  memset(cgba_dynarec_dual_hot_key, 0, sizeof(cgba_dynarec_dual_hot_key));
  memset(cgba_dynarec_dual_hot_ptr, 0, sizeof(cgba_dynarec_dual_hot_ptr));

  ram_translation_ptr = last_ram_translation_ptr = &ram_translation_cache[0];
  memset(iwram, 0, 0x8000);
  memset(&ewram[0x40000], 0, 0x40000);

  ewram_code_min = 0;
  ewram_code_max = 0x40000;
  iwram_code_min = 0;
  iwram_code_max = 0x8000;
}

void flush_dynarec_caches(void)
{
  /* Flush ROM and RAM caches. */
  flush_translation_cache_rom();
  ewram_code_min = 0;
  ewram_code_max = 0x40000;
  iwram_code_min = 0;
  iwram_code_max = 0x8000;
  flush_translation_cache_ram();
}
