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

#ifndef MEMORY_H
#define MEMORY_H

#include "libretro.h"

#define FEAT_AUTODETECT  -1
#define FEAT_DISABLE      0
#define FEAT_ENABLE       1

#define DMA_CHAN_CNT   4

#define DMA_START_IMMEDIATELY         0
#define DMA_START_VBLANK              1
#define DMA_START_HBLANK              2
#define DMA_START_SPECIAL             3
#define DMA_INACTIVE                  4

#define DMA_16BIT                     0
#define DMA_32BIT                     1

#define DMA_NO_REPEAT                 0
#define DMA_REPEAT                    1

#define DMA_INCREMENT                 0
#define DMA_DECREMENT                 1
#define DMA_FIXED                     2
#define DMA_RELOAD                    3

#define DMA_NO_IRQ                    0
#define DMA_TRIGGER_IRQ               1

#define DMA_DIRECT_SOUND_A            0
#define DMA_DIRECT_SOUND_B            1
#define DMA_NO_DIRECT_SOUND           2

// Access to timer registers
#define REG_TMXD(n)   (REG_TM0D   + (2 * (n)))
#define REG_TMXCNT(n) (REG_TM0CNT + (2 * (n)))

typedef struct
{
  u32 source_address;
  u32 dest_address;
  u32 length;
  u32 repeat_type;
  u32 direct_sound_channel;
  u32 source_direction;
  u32 dest_direction;
  u32 length_type;
  u32 start_type;
  u32 irq;
} dma_transfer_type;

typedef enum
{
  REG_DISPCNT = 0x00,
  REG_DISPSTAT = 0x02,
  REG_VCOUNT = 0x03,
  REG_BG0CNT = 0x04,
  REG_BG1CNT = 0x05,
  REG_BG2CNT = 0x06,
  REG_BG3CNT = 0x07,
  REG_BG0HOFS = 0x08,
  REG_BG0VOFS = 0x09,
  REG_BG1HOFS = 0x0A,
  REG_BG1VOFS = 0x0B,
  REG_BG2HOFS = 0x0C,
  REG_BG2VOFS = 0x0D,
  REG_BG3HOFS = 0x0E,
  REG_BG3VOFS = 0x0F,
  REG_BG2PA = 0x10,
  REG_BG2PB = 0x11,
  REG_BG2PC = 0x12,
  REG_BG2PD = 0x13,
  REG_BG2X_L = 0x14,
  REG_BG2X_H = 0x15,
  REG_BG2Y_L = 0x16,
  REG_BG2Y_H = 0x17,
  REG_BG3PA = 0x18,
  REG_BG3PB = 0x19,
  REG_BG3PC = 0x1A,
  REG_BG3PD = 0x1B,
  REG_BG3X_L = 0x1C,
  REG_BG3X_H = 0x1D,
  REG_BG3Y_L = 0x1E,
  REG_BG3Y_H = 0x1F,
  REG_WIN0H = 0x20,
  REG_WIN1H = 0x21,
  REG_WIN0V = 0x22,
  REG_WIN1V = 0x23,
  REG_WININ = 0x24,
  REG_WINOUT = 0x25,
  REG_MOSAIC = 0x26,
  REG_BLDCNT = 0x28,
  REG_BLDALPHA = 0x29,
  REG_BLDY = 0x2A,
  // Sound control registers
  REG_SOUND1CNT_L = 0x30,
  REG_SOUND1CNT_H = 0x31,
  REG_SOUND1CNT_X = 0x32,
  REG_SOUND2CNT_L = 0x34,
  REG_SOUND2CNT_H = 0x36,
  REG_SOUND3CNT_L = 0x38,
  REG_SOUND3CNT_H = 0x39,
  REG_SOUND3CNT_X = 0x3A,
  REG_SOUND4CNT_L = 0x3C,
  REG_SOUND4CNT_H = 0x3E,
  REG_SOUNDCNT_L = 0x40,
  REG_SOUNDCNT_H = 0x41,
  REG_SOUNDCNT_X = 0x42,
  REG_SOUNDWAVE_0 = 0x48,
  REG_SOUNDWAVE_1 = 0x49,
  REG_SOUNDWAVE_2 = 0x4A,
  REG_SOUNDWAVE_3 = 0x4B,
  REG_SOUNDWAVE_4 = 0x4C,
  REG_SOUNDWAVE_5 = 0x4D,
  REG_SOUNDWAVE_6 = 0x4E,
  REG_SOUNDWAVE_7 = 0x4F,
  REG_SOUND_FIFOA_L = 0x50,
  REG_SOUND_FIFOA_H = 0x51,
  REG_SOUND_FIFOB_L = 0x52,
  REG_SOUND_FIFOB_H = 0x53,
  // DMA control
  REG_DMA0SAD = 0x58,
  REG_DMA0DAD = 0x5A,
  REG_DMA0CNT_L = 0x5C,
  REG_DMA0CNT_H = 0x5D,
  REG_DMA1SAD = 0x5E,
  REG_DMA1DAD = 0x60,
  REG_DMA1CNT_L = 0x62,
  REG_DMA1CNT_H = 0x63,
  REG_DMA2SAD = 0x64,
  REG_DMA2DAD = 0x66,
  REG_DMA2CNT_L = 0x68,
  REG_DMA2CNT_H = 0x69,
  REG_DMA3SAD = 0x6A,
  REG_DMA3DAD = 0x6C,
  REG_DMA3CNT_L = 0x6E,
  REG_DMA3CNT_H = 0x6F,
  // Timers
  REG_TM0D = 0x80,
  REG_TM0CNT = 0x81,
  REG_TM1D = 0x82,
  REG_TM1CNT = 0x83,
  REG_TM2D = 0x84,
  REG_TM2CNT = 0x85,
  REG_TM3D = 0x86,
  REG_TM3CNT = 0x87,
  // Serial
  REG_SIODATA32_L = 0x90,
  REG_SIODATA32_H = 0x91,
  REG_SIOMULTI0 = 0x90,
  REG_SIOMULTI1 = 0x91,
  REG_SIOMULTI2 = 0x92,
  REG_SIOMULTI3 = 0x93,
  REG_SIOCNT = 0x94,
  REG_SIOMLT_SEND = 0x95,
  REG_SIODATA8 = 0x96,
  // Key input
  REG_P1 = 0x098,
  REG_P1CNT = 0x099,
  // More serial
  REG_RCNT = 0x9A,
  // Interrupt, waitstate, power.
  REG_IE = 0x100,
  REG_IF = 0x101,
  REG_WAITCNT = 0x102,
  REG_IME = 0x104,
  REG_HALTCNT = 0x180
} hardware_register;

// Some useful macros to avoid reg math
#define REG_BGxCNT(n)  (REG_BG0CNT + (n))
#define REG_WINxH(n)   (REG_WIN0H  + (n))
#define REG_WINxV(n)   (REG_WIN0V  + (n))
#define REG_BGxHOFS(n) (REG_BG0HOFS + ((n) * 2))
#define REG_BGxVOFS(n) (REG_BG0VOFS + ((n) * 2))
#define REG_BGxPA(n)   (REG_BG2PA + ((n)-2)*8)
#define REG_BGxPB(n)   (REG_BG2PB + ((n)-2)*8)
#define REG_BGxPC(n)   (REG_BG2PC + ((n)-2)*8)
#define REG_BGxPD(n)   (REG_BG2PD + ((n)-2)*8)

#define FLASH_DEVICE_UNDEFINED       0x00
#define FLASH_DEVICE_MACRONIX_64KB   0x1C
#define FLASH_DEVICE_AMTEL_64KB      0x3D
#define FLASH_DEVICE_SST_64K         0xD4
#define FLASH_DEVICE_PANASONIC_64KB  0x1B
#define FLASH_DEVICE_MACRONIX_128KB  0x09
#define FLASH_DEVICE_SANYO_128KB     0x13


#define FLASH_MANUFACTURER_MACRONIX  0xC2
#define FLASH_MANUFACTURER_AMTEL     0x1F
#define FLASH_MANUFACTURER_PANASONIC 0x32
#define FLASH_MANUFACTURER_SST       0xBF
#define FLASH_MANUFACTURER_SANYO     0x62

u32 function_cc read_memory8(u32 address);
u32 function_cc read_memory8s(u32 address);
u32 function_cc read_memory16(u32 address);
u16 function_cc read_memory16_signed(u32 address);
u32 function_cc read_memory16s(u32 address);
u32 function_cc read_memory32(u32 address);
cpu_alert_type function_cc write_memory8(u32 address, u8 value);
cpu_alert_type function_cc write_memory16(u32 address, u16 value);
cpu_alert_type function_cc write_memory32(u32 address, u32 value);
u32 function_cc read_eeprom(void);
void function_cc write_eeprom(u32 address, u32 value);
u8 read_backup(u32 address);
void function_cc write_backup(u32 address, u32 value);
void function_cc write_gpio(u32 address, u32 value);

void write_rumble(bool oldv, bool newv);
void rumble_frame_reset();
float rumble_active_pct();

/* EDIT: Shouldn't this be extern ?! */
extern const u32 def_seq_cycles[16][2];
/* Cycles can change depending on WAITCNT */
extern u8 ws_cyc_seq[16][2];
extern u8 ws_cyc_nseq[16][2];

extern u32 gamepak_size;
extern bool gamepak_mini_materialized;
extern bool gamepak_header_nonstandard;
extern char gamepak_code[5];
extern char gamepak_filename[512];

cpu_alert_type dma_transfer(unsigned dma_chan, int *cycles);
u8 *memory_region(u32 address, u32 *memory_limit);
u32 load_gamepak(const struct retro_game_info* info, const char *name,
                 int force_rtc, int force_rumble, int force_serial);
u32 load_gamepak_from_memory(const u8 *rom, u32 rom_size,
                             int force_rtc, int force_rumble,
                             int force_serial);
u32 load_gamepak_from_pages(const u8 * const *pages, u32 rom_size,
                            int force_rtc, int force_rumble,
                            int force_serial);
s32 load_bios(char *name);
void init_memory(void);
void init_gamepak_buffer(void);
bool gamepak_must_swap(void);
void memory_term(void);
u8 *load_gamepak_page(u32 physical_index);
/* Bind the calculator storage layer's sanitized 8192-entry table of direct
 * 4 KiB NOR blocks. NULL unpublishes it before storage mutation/ROM teardown. */
void cgba_gamepak_bind_fragment_table(const u8 * const *blocks);
/* Resolve the 4 KiB host block containing a guest address. Contiguous 32 KiB
 * mappings stay first and free; fragmented NOR uses the table; unsafe blocks
 * fall back to the aligned 32 KiB gather LRU. */
u8 *cgba_gamepak_resolve_4k(u32 address);
/* Generic instruction-source resolver. Returns a 4 KiB block base for any
 * mapped GBA-bus address and uses the cartridge resolver for fragmented ROM. */
u8 *cgba_memory_map_read_4k(u32 address);

extern const u8 * const *cgba_gamepak_fragment_blocks;

/* Calculator-only transient storage backed by the 1 MiB fallback cache. The
 * embedded mini ROM has separate storage. Acquiring/releasing invalidates all
 * fallback mappings so callers may safely overwrite the arena while guest
 * execution is stopped. */
u8 *cgba_gamepak_scratch_acquire(u32 min_size);
void cgba_gamepak_scratch_release(void);
/* Drop every direct and cached GamePak alias before Fugue block addresses are
 * refreshed. A failed refresh must never leave stale P1 pointers reachable. */
void cgba_gamepak_unmap_pages(void);
/* Replace cartridge aliases after a NOR block-address refresh without
 * resetting any guest cartridge/RTC/backup state. */
bool cgba_gamepak_remap_pages(const u8 * const *pages, u32 rom_size);

extern u32 oam_update;
extern u32 gbc_sound_wave_update;
extern dma_transfer_type dma[DMA_CHAN_CNT];
extern u32 dma_bus_val;
extern u8 cgba_hblank_dma_mask;
extern u8 cgba_dma_bg2_fast_mask;

extern u8 open_gba_bios_rom[1024*16];
extern bool cgba_using_external_bios;
extern u16 palette_ram[512];
extern u16 oam_ram[512];
extern u16 palette_ram_converted[512];
#define CGBA_PALETTE_DIRTY_BLEND      0x01u
#define CGBA_PALETTE_DIRTY_BACKDROP   0x02u
#define CGBA_PALETTE_DIRTY_SCANLINE   0x08u
#define CGBA_PALETTE_DIRTY_FOLDED     0x10u
#define CGBA_PALETTE_DIRTY_SIGNED     0x20u
#define CGBA_PALETTE_DIRTY_FOLDED_OBJ 0x40u
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
#define CGBA_PALETTE_BLEND_BIT CGBA_PALETTE_DIRTY_BLEND
#else
#define CGBA_PALETTE_BLEND_BIT 0
#endif
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
#define CGBA_PALETTE_BACKDROP_BIT CGBA_PALETTE_DIRTY_BACKDROP
#else
#define CGBA_PALETTE_BACKDROP_BIT 0
#endif
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
#define CGBA_PALETTE_SCANLINE_BIT CGBA_PALETTE_DIRTY_SCANLINE
#else
#define CGBA_PALETTE_SCANLINE_BIT 0
#endif
#if defined(CGBA_VIDEO_TEXT_FAST4BPP) && CGBA_VIDEO_TEXT_FAST4BPP && \
    (defined(__SH4__) || defined(__SH4_NOFPU__) || defined(__SH4A__) || \
     defined(__sh__)) && defined(MSB_FIRST)
#define CGBA_SH4_ASM_4BPP 1
#endif
#if defined(CGBA_SH4_ASM_4BPP) && \
    defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
#define CGBA_PALETTE_FOLDED_BIT     CGBA_PALETTE_DIRTY_FOLDED
#define CGBA_PALETTE_FOLDED_OBJ_BIT CGBA_PALETTE_DIRTY_FOLDED_OBJ
#else
#define CGBA_PALETTE_FOLDED_BIT     0
#define CGBA_PALETTE_FOLDED_OBJ_BIT 0
#endif
#ifdef CGBA_SH4_ASM_4BPP
#define CGBA_PALETTE_SIGNED_BIT CGBA_PALETTE_DIRTY_SIGNED
#else
#define CGBA_PALETTE_SIGNED_BIT 0
#endif
#if CGBA_PALETTE_BLEND_BIT || CGBA_PALETTE_BACKDROP_BIT || \
    CGBA_PALETTE_SCANLINE_BIT || \
    CGBA_PALETTE_FOLDED_BIT || CGBA_PALETTE_SIGNED_BIT || \
    CGBA_PALETTE_FOLDED_OBJ_BIT
#define CGBA_PALETTE_DIRTY_ACTIVE \
  (CGBA_PALETTE_BLEND_BIT | CGBA_PALETTE_BACKDROP_BIT | \
   CGBA_PALETTE_SCANLINE_BIT | \
   CGBA_PALETTE_FOLDED_BIT | CGBA_PALETTE_SIGNED_BIT | \
   CGBA_PALETTE_FOLDED_OBJ_BIT)
/* OBJ palette (0x05000200..0x050003FF) cannot affect BG backdrop shadow,
 * BG folded palettes, or Mode 1/2 BG signed palettes; it does
 * affect folded OBJ palettes (cgba_folded_pal[256..511]), blend caches, and
 * scanline change detection. */
#define CGBA_PALETTE_DIRTY_OBJ_ONLY \
  (CGBA_PALETTE_BLEND_BIT | CGBA_PALETTE_SCANLINE_BIT | \
   CGBA_PALETTE_FOLDED_OBJ_BIT)
#endif
#ifdef CGBA_PALETTE_DIRTY_ACTIVE
/* Bitmask set by every path which mutates palette_ram_converted, including
 * generated SH4 fastmem code.  Each renderer cache clears only its own bit
 * after rebuilding, so independent consumers cannot hide invalidation from
 * one another. */
extern volatile u32 palette_ram_dirty;
#define mark_palette_ram_dirty() \
  (palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE)
#define mark_palette_ram_addr_dirty(off) do {                                 \
  if ((u32)(off) < 0x200u) palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;     \
  else                     palette_ram_dirty |= CGBA_PALETTE_DIRTY_OBJ_ONLY;  \
} while (0)
#else
#define mark_palette_ram_dirty() ((void)0)
#define mark_palette_ram_addr_dirty(off) ((void)(off))
#endif
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
#define CGBA_VRAM_DIRTY_BG   0x01u
#define CGBA_VRAM_DIRTY_OBJ  0x02u
#define CGBA_VRAM_DIRTY_ALL  (CGBA_VRAM_DIRTY_BG | CGBA_VRAM_DIRTY_OBJ)
#define CGBA_VRAM_DIRTY_ACTIVE CGBA_VRAM_DIRTY_ALL
#define CGBA_VRAM_SB_DIRTY_ACTIVE 1
/* Bitmask set by every path that writes VRAM (CPU, DMA, SWI HLE, native SH4
 * stores, reset and savestate load). Consumed by the dirty-scanline tracker. */
extern volatile u32 cgba_vram_dirty;
extern volatile u32 cgba_vram_sb_dirty;
extern volatile u32 cgba_video_io_dirty;
#define mark_vram_dirty_off(off)                                              \
  do {                                                                        \
    u32 _vo = (u32)(off);                                                     \
    if (_vo < 0x10000u) {                                                     \
      cgba_vram_dirty |= CGBA_VRAM_DIRTY_BG;                                  \
      cgba_vram_sb_dirty |= (1u << (_vo >> 11));                              \
    } else {                                                                  \
      cgba_vram_dirty |= CGBA_VRAM_DIRTY_OBJ;                                 \
    }                                                                         \
  } while (0)
#define mark_vram_dirty_span(off, bytes)                                      \
  do {                                                                        \
    u32 _vo = (u32)(off), _vb = (u32)(bytes);                                 \
    if (_vo < 0x10000u && _vb > 0u) {                                         \
      u32 _hi = _vo + _vb - 1u;                                               \
      if (_hi >= 0x10000u) _hi = 0xFFFFu;                                     \
      u32 _s0 = _vo >> 11, _s1 = _hi >> 11;                                   \
      u32 _m = (_s1 - _s0 >= 31u) ? 0xFFFFFFFFu                               \
                                  : (((2u << (_s1 - _s0)) - 1u) << _s0);      \
      cgba_vram_dirty |= CGBA_VRAM_DIRTY_BG;                                  \
      cgba_vram_sb_dirty |= _m;                                               \
    }                                                                         \
    if (_vb > 0u && _vo + _vb > 0x10000u)                                     \
      cgba_vram_dirty |= CGBA_VRAM_DIRTY_OBJ;                                 \
  } while (0)
#define mark_vram_dirty_all()                                                 \
  do {                                                                        \
    cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;                                    \
    cgba_vram_sb_dirty = 0xFFFFFFFFu;                                         \
  } while (0)
#define CGBA_VIDEO_DEFERRED_ACTIVE 1
extern volatile u32 cgba_pending_sl_count;
extern u32 cgba_pending_sl_start;
void cgba_flush_pending_scanlines(void);
void cgba_snapshot_bg2_affine_pending(void);
int cgba_try_hblank_bg2_dma_fast(u32 ch);
int cgba_try_batch_hblank_bg2_dma_fast(u32 ch, u32 start_y, u32 lines);
void cgba_discard_pending_scanlines(void);
void cgba_queue_scanlines(u32 start_y, u32 count);
static inline void cgba_video_flush_before_write(void) {
  if (__builtin_expect(cgba_pending_sl_count != 0u, 0))
    cgba_flush_pending_scanlines();
}
static inline void cgba_video_snapshot_bg2_before_write(void) {
  if (__builtin_expect(cgba_pending_sl_count != 0u, 0))
    cgba_snapshot_bg2_affine_pending();
}
#else
#define mark_vram_dirty_off(off) ((void)(off))
#define mark_vram_dirty_span(off, bytes) do { (void)(off); (void)(bytes); } while (0)
#define mark_vram_dirty_all() ((void)0)
static inline void cgba_video_flush_before_write(void) {}
static inline void cgba_video_snapshot_bg2_before_write(void) {}
static inline void cgba_discard_pending_scanlines(void) {}
#endif
/* Alignment-safe SMC tag-mirror scan shared by DMA, CPU stores, and SWI HLE.
 * SH-4 faults on unaligned u32 loads, so peel unaligned head/tail bytes. */
static inline int cgba_smc_tags_nonzero(const u8 *tags, u32 len)
{
  u32 i = 0;
  if (!tags)
    return 0;
  for (; i < len && (((uintptr_t)tags + i) & 3u); i++)
    if (tags[i]) return 1;
  for (; i + 4u <= len; i += 4u)
    if (*(const u32 *)(const void *)(tags + i)) return 1;
  for (; i < len; i++)
    if (tags[i]) return 1;
  return 0;
}

/* Return 1 only if a non-overlapping copy of [src, src+len) into [dst, dst+len)
 * modifies at least one byte covered by a non-zero SMC tag.  Must be called
 * BEFORE overwriting dst.  Zero overhead when no tags are set in the span. */
static inline int cgba_smc_copy_changes_tagged(
  const u8 *dst, const u8 *src, const u8 *tags, u32 len)
{
  u32 i;
  if (!cgba_smc_tags_nonzero(tags, len))
    return 0;
  if (memcmp(dst, src, len) == 0)
    return 0;
  for (i = 0; i < len; i++)
    if (tags[i] && dst[i] != src[i])
      return 1;
  return 0;
}

/* Return 1 only if filling [dst, dst+len) with repeating pat[0..pat_len-1]
 * (pat_len is a power of two: 1, 2, or 4) modifies a tagged byte.  Must be
 * called BEFORE overwriting dst. */
static inline int cgba_smc_fill_changes_tagged(
  const u8 *dst, const u8 *pat, u32 pat_len, const u8 *tags, u32 len)
{
  u32 i, mask = pat_len - 1u;
  if (!cgba_smc_tags_nonzero(tags, len))
    return 0;
  for (i = 0; i < len; i++)
    if (tags[i] && dst[i] != pat[i & mask])
      return 1;
  return 0;
}

/* Return 1 only if a 1/2/4-byte little-endian store of new_val into dst
 * modifies at least one byte covered by a non-zero SMC tag.  Must be called
 * BEFORE overwriting dst. */
static inline int cgba_smc_scalar_changes_tagged(
  const u8 *dst, const u8 *tags, u32 bytes, u32 new_val)
{
  u32 i;
  if (!cgba_smc_tags_nonzero(tags, bytes))
    return 0;
  for (i = 0; i < bytes; i++) {
    if (tags[i] && dst[i] != (u8)(new_val >> (i * 8u)))
      return 1;
  }
  return 0;
}
extern u16 io_registers[512];
#if defined(CGBA_SILENT_TIMERS) && CGBA_SILENT_TIMERS
/* GBA IRQ bitmask (subset of IRQ_TIMER0..IRQ_TIMER3 = 0x0078) of timers whose
 * slice cap was skipped in the current slice because they were masked in IE
 * and already latched in IF. Clearing IF or enabling IE for any set bit must
 * reslice via CPU_ALERT_RESLICE so subsequent overflows are not delayed. */
extern volatile u32 cgba_timer_uncapped_irq_mask;
#endif
extern u8 vram[1024 * 96];
extern u8 bios_rom[1024 * 16];
// Double buffer used for SMC detection
extern u8 ewram[1024 * 256 * 2];
extern u8 iwram[1024 * 32 * 2];

extern u8 *memory_map_read[8 * 1024];

extern u32 reg[64];

#define BACKUP_SRAM       0
#define BACKUP_FLASH      1
#define BACKUP_EEPROM     2
#define BACKUP_UNKN       3

#define SRAM_SIZE_32KB    1
#define SRAM_SIZE_64KB    2

#define FLASH_SIZE_64KB   1
#define FLASH_SIZE_128KB  2

#define EEPROM_512_BYTE   1
#define EEPROM_8_KBYTE   16

#define EEPROM_BASE_MODE              0
#define EEPROM_READ_MODE              1
#define EEPROM_READ_HEADER_MODE       2
#define EEPROM_ADDRESS_MODE           3
#define EEPROM_WRITE_MODE             4
#define EEPROM_WRITE_ADDRESS_MODE     5
#define EEPROM_ADDRESS_FOOTER_MODE    6
#define EEPROM_WRITE_FOOTER_MODE      7

#define FLASH_BASE_MODE               0
#define FLASH_ERASE_MODE              1
#define FLASH_ID_MODE                 2
#define FLASH_WRITE_MODE              3
#define FLASH_BANKSWITCH_MODE         4

extern u32 backup_type;
extern u32 sram_bankcount;
extern u32 flash_bank_cnt;
extern u32 eeprom_size;

extern u8 gamepak_backup[1024 * 128];

/* cgba: set whenever the game writes its backup (SRAM/Flash/EEPROM) so the
 * fx-CG100 port only flushes the .SAV to NOR flash when the save changed. */
extern u32 cgba_backup_dirty;

// Page sticky bit routines
extern u32 gamepak_sticky_bit[1024/32];
static inline void touch_gamepak_page(u32 physical_index)
{
  u32 idx = (physical_index >> 5) & 31;
  u32 bof = physical_index & 31;

  gamepak_sticky_bit[idx] |= (1u << bof);
}

static inline void clear_gamepak_stickybits(void)
{
  memset(gamepak_sticky_bit, 0, sizeof(gamepak_sticky_bit));
}

bool memory_check_savestate(const u8*src);
bool memory_read_savestate(const u8*src);
unsigned memory_write_savestate(u8 *dst);

#endif
