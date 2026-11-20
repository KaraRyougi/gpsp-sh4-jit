/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2023 David Guillen Fandos <david@davidgf.net>
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

extern "C" {
  #include "common.h"
}

#ifdef CGBA_LCD_SCANLINE_STREAM
extern "C" void fxcg100_lcd_stream_scanline(const u16 *row, unsigned y, int dirty);
#endif
#if defined(CGBA_VIDEO_TEXT_FAST4BPP) && CGBA_VIDEO_TEXT_FAST4BPP && \
    (defined(__SH4__) || defined(__SH4_NOFPU__) || defined(__SH4A__) || \
     defined(__sh__)) && defined(MSB_FIRST)
#define CGBA_SH4_ASM_4BPP 1
extern "C" {
void cgba_render_4bpp_span_over_sh4(
  const u16 *map_ptr, u32 todraw, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u16 *paltbl, u32 skip_zero);
void cgba_render_4bpp_span_base_sh4(
  const u16 *map_ptr, u32 todraw, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u16 *paltbl);
void cgba_render_4bpp_span_base_pair_sh4(
  const u16 *map_ptr, u32 todraw, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *pair_lut);
void cgba_render_4bpp_span_over_pair_sh4(
  const u16 *map_ptr, u32 todraw, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *pair_lut, const u16 *paltbl);
void cgba_render_4bpp_span2_base_pair_sh4(
  const u16 *map_ptr, u32 counts, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *pair_lut, const u16 *map_ptr2);
void cgba_render_4bpp_span2_over_pair_sh4(
  const u16 *map_ptr, u32 counts, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *pair_lut, const u16 *paltbl,
  const u16 *map_ptr2);
void cgba_render_4bpp_plan_base_sh4(
  const void *plan, u32 vcount, u16 *dest_ptr, const u32 *pair_lut);
void cgba_render_4bpp_plan_over_sh4(
  const void *plan, u32 vcount, u16 *dest_ptr, const u32 *pair_lut);
void cgba_render_4bpp_strip_base_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u32 *pair_lut, const u16 *map2);
void cgba_render_4bpp_strip_over_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u32 *pair_lut, const u16 *map2);
void cgba_render_4bpp_strip_blend_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_k_evb, const u32 *term_a, const u16 *map2);
void cgba_blend_solid_rect_sh4(
  u16 *dst_top, u32 wh_packed, u32 term_a_val, u32 evb);
void cgba_render_obj_4bpp_tiles_sh4(
  const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr, const u16 *subpal);
void cgba_render_obj_4bpp_tiles_hf_sh4(
  const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr, const u16 *subpal);
struct cgba_aff_rot_params {
  u16 *dst_ptr;
  const u8 *base_vram;
  const u16 *subpal;
  s32 source_x;
  s32 source_y;
  s32 dx;
  s32 dy;
  u32 cnt;
  u32 dimw;
  u32 dimh;
  u32 row_shift;
};
void cgba_render_obj_4bpp_affine_scale_sh4(
  u16 *dst_ptr, u32 cnt_and_dimw, s32 source_x, s32 dx,
  const u8 *row_ptr, const u16 *subpal);
void cgba_render_obj_4bpp_affine_rot_sh4(const cgba_aff_rot_params *p);
}
#endif

u16* gba_screen_pixels = NULL;

#define get_screen_pixels()   gba_screen_pixels
#define get_screen_pitch()    GBA_SCREEN_PITCH

typedef struct {
  u16 attr0, attr1, attr2, attr3;
} t_oam;

typedef struct {
  u16 pad0[3];
  u16 dx;
  u16 pad1[3];
  u16 dmx;
  u16 pad2[3];
  u16 dy;
  u16 pad3[3];
  u16 dmy;
} t_affp;

typedef void (* bitmap_render_function)(
  u32 start, u32 end, void *dest_ptr, const u16 *pal);
typedef void (* tile_render_function)(
  u32 layer, u32 start, u32 end, void *dest_ptr, const u16 *pal);

typedef void (*render_function_u16)(
  u32 start, u32 end, u16 *scanline, u32 enable_flags);
typedef void (*render_function_u32)(
  u32 start, u32 end, u32 *scanline, u32 enable_flags);

typedef void (*window_render_function)(u16 *scanline, u32 start, u32 end);

static void render_scanline_conditional(
  u32 start, u32 end, u16 *scanline, u32 enable_flags = 0x3F);

typedef struct
{
  bitmap_render_function blit_render;
  bitmap_render_function scale_render;
  bitmap_render_function affine_render;
} bitmap_layer_render_struct;

typedef struct
{
  render_function_u16 fullcolor;
  render_function_u16 indexed_u16;
  render_function_u32 indexed_u32;
  render_function_u32 stacked;
} layer_render_struct;

// Object blending modes
#define OBJ_MOD_NORMAL     0
#define OBJ_MOD_SEMITRAN   1
#define OBJ_MOD_WINDOW     2
#define OBJ_MOD_INVALID    3

// BLDCNT color effect modes
#define COL_EFFECT_NONE   0x0
#define COL_EFFECT_BLEND  0x1
#define COL_EFFECT_BRIGHT 0x2
#define COL_EFFECT_DARK   0x3

// Background render modes
#define RENDER_NORMAL   0
#define RENDER_COL16    1
#define RENDER_COL32    2
#define RENDER_ALPHA    3


// Byte lengths of complete tiles and tile rows in 4bpp and 8bpp.

#define tile_width_4bpp   4
#define tile_size_4bpp   32
#define tile_width_8bpp   8
#define tile_size_8bpp   64

// Sprite rendering cycles
#define REND_CYC_MAX          32768   /* Theoretical max is 17920 */
#define REND_CYC_SCANLINE      1210
#define REND_CYC_REDUCED        954

// Generate bit mask (bits 9th and 10th) with information about the pixel
// status (1st and/or 2nd target) for later blending.
static inline u16 color_flags(u32 layer) {
  u32 bldcnt = read_ioreg(REG_BLDCNT);
  return (
    ((bldcnt >> layer) & 0x01) |            // 1st target
    ((bldcnt >> (layer + 7)) & 0x02)        // 2nd target
  ) << 9;
}

static const u32 map_widths[] = { 256, 512, 256, 512 };

typedef enum
{
  FULLCOLOR,  // Regular rendering, output a 16 bit color
  INDXCOLOR,  // Rendering to indexed color, so we can later apply dark/bright
  STCKCOLOR,  // Stacks two indexed pixels (+flags) to apply blending
  PIXCOPY     // Special mode used for sprites, to allow for obj-window drawing
} rendtype;

s32 affine_reference_x[2];
s32 affine_reference_y[2];

static inline s32 signext28(u32 value)
{
  s32 ret = (s32)(value << 4);
  return ret >> 4;
}

void video_reload_counters()
{
  /* This happens every Vblank */
  affine_reference_x[0] = signext28(read_ioreg32(REG_BG2X_L));
  affine_reference_y[0] = signext28(read_ioreg32(REG_BG2Y_L));
  affine_reference_x[1] = signext28(read_ioreg32(REG_BG3X_L));
  affine_reference_y[1] = signext28(read_ioreg32(REG_BG3Y_L));
}

#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
/* Base-layer 4-bpp FULLCOLOR text backgrounds normally branch on every
 * transparent color-zero nibble to select the global backdrop color.  Mirror
 * the BG palette and replace color zero in each 16-color sub-palette with the
 * backdrop, making that selection an unconditional indexed load instead.
 *
 * palette_ram_dirty is a bitmask shared with the independent blend cache.  We
 * consume only our bit so either cache may observe a palette write first. */
static u16 backdrop_shadow_palette[256] __attribute__((aligned(4)));

#ifdef CGBA_VIDEO_PAIR_PALETTE
/* Two-pixel palette: for each 16-color sub-palette, one entry per packed tile
 * byte holding both of its pixels in framebuffer order (low nibble first).
 * A 4-bpp row is then four table loads instead of eight nibble extractions
 * and eight palette loads.  Entries are derived from the backdrop-shadow
 * palette, so they are exact for base-layer rows (color zero = backdrop) and
 * for fully opaque rows of any layer (no nibble is zero).
 *
 * Sub-tables are rebuilt lazily; the shadow refresh below invalidates only
 * the sub-palettes whose sixteen colors actually changed. */
static u16 pair_palette[16][256][2] __attribute__((aligned(32)));
static u8 pair_palette_valid[16];
static u8 pair_palette_any_invalid = 1;

static __attribute__((noinline)) void pair_palette_build(unsigned sub)
{
  const u16 *pal = backdrop_shadow_palette + sub * 16;
  u16 (*out)[2] = pair_palette[sub];
  for (unsigned b = 0; b < 256; b++) {
    out[b][0] = pal[b & 15];
    out[b][1] = pal[b >> 4];
  }
  pair_palette_valid[sub] = 1;
}

#ifdef CGBA_SH4_ASM_4BPP
static __attribute__((noinline)) void pair_palette_build_all_invalid(void)
{
  for (unsigned sub = 0; sub < 16; sub++) {
    if (!pair_palette_valid[sub])
      pair_palette_build(sub);
  }
  pair_palette_any_invalid = 0;
}

static inline const u32 *pair_palette_get_all_u32(void)
{
  if (__builtin_expect(pair_palette_any_invalid != 0, 0))
    pair_palette_build_all_invalid();
  return (const u32 *)&pair_palette[0][0][0];
}
#endif

template<bool hflip>
static inline __attribute__((always_inline)) void cgba_render_4bpp_row_pairs(
  u32 tilepix, unsigned sub, u16 *dest_ptr)
{
  typedef u32 alias_u32 __attribute__((may_alias));
  if (__builtin_expect(!pair_palette_valid[sub], 0))
    pair_palette_build(sub);
  const u16 (*tbl)[2] = pair_palette[sub];
  /* tilepix is in guest order: byte 0 (bits 7..0) holds pixels 0 and 1. */
  const u16 *p0 = tbl[hflip ? (tilepix >> 24)         : (tilepix & 0xFF)];
  const u16 *p1 = tbl[hflip ? ((tilepix >> 16) & 0xFF) : ((tilepix >> 8) & 0xFF)];
  const u16 *p2 = tbl[hflip ? ((tilepix >> 8) & 0xFF)  : ((tilepix >> 16) & 0xFF)];
  const u16 *p3 = tbl[hflip ? (tilepix & 0xFF)         : (tilepix >> 24)];
  if (!((uintptr_t)dest_ptr & 3)) {
    /* SH4 faults on unaligned longword stores; spans may start on an odd
     * pixel, which takes the halfword path below. */
    alias_u32 *out = (alias_u32 *)dest_ptr;
    u32 v0 = *(const alias_u32 *)p0, v1 = *(const alias_u32 *)p1;
    u32 v2 = *(const alias_u32 *)p2, v3 = *(const alias_u32 *)p3;
    if (hflip) {
      v0 = (v0 << 16) | (v0 >> 16); v1 = (v1 << 16) | (v1 >> 16);
      v2 = (v2 << 16) | (v2 >> 16); v3 = (v3 << 16) | (v3 >> 16);
    }
    out[0] = v0; out[1] = v1; out[2] = v2; out[3] = v3;
  } else {
    dest_ptr[0] = p0[hflip]; dest_ptr[1] = p0[!hflip];
    dest_ptr[2] = p1[hflip]; dest_ptr[3] = p1[!hflip];
    dest_ptr[4] = p2[hflip]; dest_ptr[5] = p2[!hflip];
    dest_ptr[6] = p3[hflip]; dest_ptr[7] = p3[!hflip];
  }
}
#endif

static const u16 *get_backdrop_shadow_palette(void)
{
  if (palette_ram_dirty & CGBA_PALETTE_DIRTY_BACKDROP) {
#ifdef CGBA_VIDEO_PAIR_PALETTE
    u16 backdrop = palette_ram_converted[0];
    for (u32 sub = 0; sub < 16; sub++) {
      u16 *shadow = backdrop_shadow_palette + sub * 16;
      const u16 *live = palette_ram_converted + sub * 16;
      if (shadow[0] != backdrop ||
          memcmp(shadow + 1, live + 1, 15 * sizeof(u16))) {
        shadow[0] = backdrop;
        memcpy(shadow + 1, live + 1, 15 * sizeof(u16));
        pair_palette_valid[sub] = 0;
        pair_palette_any_invalid = 1;
      }
    }
#else
    memcpy(backdrop_shadow_palette, palette_ram_converted,
      sizeof(backdrop_shadow_palette));
    for (u32 i = 0; i < 256; i += 16)
      backdrop_shadow_palette[i] = palette_ram_converted[0];
#endif
    palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_BACKDROP;
  }
  return backdrop_shadow_palette;
}
#endif

#ifdef CGBA_VIDEO_TILE_ROW_CACHE
#ifndef CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES
#define CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES 128
#endif
static_assert(CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES > 0 &&
  !(CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES & (CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES - 1)),
  "tile row cache size must be a power of two");

/* Content keys observe all VRAM writers without instrumenting their hot paths.
 * One entry fits one SH4 cache line. Base and overlay layers share entries;
 * zero pixels contain the backdrop, and overlay writers honor the mask. */
struct alignas(32) cgba_tile_row_entry {
  u32 packed, tag;
  u16 pixels[8];
  u32 mask;
  u32 padding;
};
static cgba_tile_row_entry tile_rows[CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES];
static u16 tile_row_palette[256] __attribute__((aligned(4)));
static u32 tile_row_epoch = 0x10000;
#ifdef CGBA_VIDEO_TILE_ROW_PROFILE
extern "C" { u32 cgba_tile_row_hits, cgba_tile_row_misses; }
#endif

static void tile_row_observe_palette(void)
{
  if (!(palette_ram_dirty & CGBA_PALETTE_DIRTY_TILE_ROW)) return;
  /* Repeated identical palette DMA and OBJ-only writes must not evict BG rows. */
  if (memcmp(tile_row_palette, palette_ram_converted, sizeof(tile_row_palette))) {
    memcpy(tile_row_palette, palette_ram_converted, sizeof(tile_row_palette));
    tile_row_epoch += 0x10000;
    if (!tile_row_epoch) {
      memset(tile_rows, 0, sizeof(tile_rows));
      tile_row_epoch = 0x10000;
    }
  }
  palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_TILE_ROW;
}

static __attribute__((noinline)) void tile_row_fill(cgba_tile_row_entry *row,
                                                   u32 packed, u32 tag)
{
  u32 data = eswap32(packed), mask = 0;
  const u16 *pal = tile_row_palette + ((tag >> 8) & 0xf0);
  for (unsigned i = 0; i < 8; i++) {
    unsigned color = tag & 0x400 ? data >> 28 : data & 15;
    row->pixels[i] = color ? pal[color] : tile_row_palette[0];
    if (color) mask |= 1u << i;
    if (tag & 0x400) data <<= 4; else data >>= 4;
  }
  row->mask = mask == 255 ? ~0u : mask;
  row->packed = packed;
  row->tag = tag;
}

template<bool isbase>
static inline __attribute__((always_inline)) void render_cached_tile_row(
  u32 packed, unsigned tile, u16 *__restrict dst)
{
  /* Tile number is already decoded by the map walker. Use it only as a
   * cheap index; raw VRAM content and palette/flip tags remain the key. */
  unsigned index = tile & (CGBA_VIDEO_TILE_ROW_CACHE_ENTRIES - 1);
  cgba_tile_row_entry &row = tile_rows[index];
  u32 tag = tile_row_epoch | (tile & 0xf400);
  if (row.tag != tag || row.packed != packed) {
#ifdef CGBA_VIDEO_TILE_ROW_PROFILE
    cgba_tile_row_misses++;
#endif
    tile_row_fill(&row, packed, tag);
  } else {
#ifdef CGBA_VIDEO_TILE_ROW_PROFILE
    cgba_tile_row_hits++;
#endif
  }
  const u32 mask = row.mask;
  if (isbase || mask == ~0u) {
    /* Scanline spans can start on an odd pixel. Never issue an unaligned
     * SH4 longword store; the halfword fallback has the same pixel order. */
    if (!((uintptr_t)dst & 3)) {
      typedef u32 alias_u32 __attribute__((may_alias));
      alias_u32 *out = (alias_u32 *)dst;
      const alias_u32 *in = (const alias_u32 *)row.pixels;
      out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = in[3];
    } else {
      dst[0] = row.pixels[0]; dst[1] = row.pixels[1];
      dst[2] = row.pixels[2]; dst[3] = row.pixels[3];
      dst[4] = row.pixels[4]; dst[5] = row.pixels[5];
      dst[6] = row.pixels[6]; dst[7] = row.pixels[7];
    }
  } else {
#define CGBA_TILE_ROW_PIXEL(i) if (mask & (1u << (i))) dst[i] = row.pixels[i]
    CGBA_TILE_ROW_PIXEL(0); CGBA_TILE_ROW_PIXEL(1);
    CGBA_TILE_ROW_PIXEL(2); CGBA_TILE_ROW_PIXEL(3);
    CGBA_TILE_ROW_PIXEL(4); CGBA_TILE_ROW_PIXEL(5);
    CGBA_TILE_ROW_PIXEL(6); CGBA_TILE_ROW_PIXEL(7);
#undef CGBA_TILE_ROW_PIXEL
  }
}
#endif

// Renders non-affine tiled background layer.
// Will process a full or partial tile (start and end within 0..8) and draw
// it in either 8 or 4 bpp mode. Honors vertical and horizontal flip.

// tile contains the tile info (contains tile index, flip bits, pal info)
// hflip causes the tile pixels lookup to be reversed (from MSB to LSB
// If isbase is not set, color 0 is interpreted as transparent, otherwise
// we are drawing the base layer, so palette[0] is used (backdrop).

template<typename dtype, rendtype rdtype, bool is8bpp, bool isbase, bool hflip>
static inline void rend_part_tile_Nbpp(u32 bg_comb, u32 px_comb,
  dtype *dest_ptr, u32 start, u32 end, u16 tile,
  const u8 *tile_base, int vertical_pixel_flip, const u16 *paltbl
) {
  // Seek to the specified tile, using the tile number and size.
  // tile_base already points to the right tile-line vertical offset
  const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];
  u16 bgcolor = paltbl[0];

  // On vertical flip, apply the mirror offset
  if (tile & 0x800)
    tile_ptr += vertical_pixel_flip;

  if (is8bpp) {
    // Each byte is a color, mapped to a palete. 8 bytes can be read as 64bit
    for (u32 i = start; i < end; i++, dest_ptr++) {
      // Honor hflip by selecting bytes in the correct order
      u32 sel = hflip ? (7-i) : i;
      u8 pval = tile_ptr[sel];
      // Alhpa mode stacks previous value (unless rendering the first layer)
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = paltbl[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_comb;  // Add combine flags
        else if (rdtype == STCKCOLOR)
          // Stack pixels on top of the pixel value and combine flags
          *dest_ptr = pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16);
      }
      else if (isbase) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = bgcolor;
        else
          *dest_ptr = 0 | bg_comb;  // Add combine flags
      }
    }
  } else {
    u32 raw = *(const u32 *)tile_ptr;
    if (!raw) {
      if (isbase) {
        for (u32 i = start; i < end; i++, dest_ptr++)
          *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : (0 | bg_comb);
      }
      return;
    }
    // In 4bpp mode, the tile[15..12] bits contain the sub-palette number.
    u16 tilepal = (tile >> 12) << 4;
    u16 pxflg = px_comb | tilepal;
    const u16 *subpal = &paltbl[tilepal];
    // Read packed pixel data, skip start pixels
    u32 tilepix = eswap32(raw);
    if (hflip) tilepix <<= (start * 4);
    else       tilepix >>= (start * 4);
    // Only 32 bits (8 pixels * 4 bits)
    for (u32 i = start; i < end; i++, dest_ptr++) {
      u8 pval = hflip ? tilepix >> 28 : tilepix & 0xF;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
      if (rdtype == FULLCOLOR && isbase) {
        /* render_scanline_text_fast substitutes the shadow palette for this
         * exact instantiation, so subpal[0] is already the backdrop. */
        *dest_ptr = subpal[pval];
      }
      else
#endif
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = subpal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pxflg | pval;
        else if (rdtype == STCKCOLOR)    // Stack pixels
          *dest_ptr = pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16);
      }
      else if (isbase) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = bgcolor;
        else
          *dest_ptr = 0 | bg_comb;
      }
      // Advance to next packed data
      if (hflip) tilepix <<= 4;
      else       tilepix >>= 4;
    }
  }
}

// Same as above, but optimized for full tiles. Skip comments here.
#if defined(CGBA_VIDEO_OPAQUE_TILE_FASTPATH) || \
    defined(CGBA_VIDEO_OPAQUE_ROW_UNROLL)
static inline bool cgba_4bpp_row_is_opaque(u32 tilepix)
{
  return ((tilepix - 0x11111111u) & ~tilepix & 0x88888888u) == 0;
}
#endif

#ifdef CGBA_VIDEO_OPAQUE_ROW_UNROLL
/* The SH4 -O2 loop for a stacked opaque row executes a pointer increment,
 * loop counter, and conditional branch for every pixel (GCC partly duplicates
 * the loop body, but does not remove that bookkeeping).  Full text tiles are
 * always eight pixels wide, so spell out the common u32 indexed/stacked case.
 * Keeping tilepix as a shift register is cheaper on SH4 than eight unrelated
 * variable shifts, while constant destination indices select MOV.L @(disp,Rn)
 * loads/stores and avoid pointer updates. */
template<rendtype rdtype, bool isbase, bool hflip>
static inline void cgba_render_opaque_4bpp_row_u32(
  u32 tilepix, u32 bg_comb, u32 pxflg, u32 *dest_ptr)
{
#define CGBA_OPAQUE_ROW_PIXEL(index_)                                      \
  do {                                                                     \
    u32 pval = hflip ? (tilepix >> 28) : (tilepix & 0x0Fu);                \
    if (rdtype == INDXCOLOR)                                               \
      dest_ptr[index_] = pxflg | pval;                                     \
    else if (isbase)                                                       \
      dest_ptr[index_] = (bg_comb << 16) | pxflg | pval;                  \
    else                                                                   \
      dest_ptr[index_] = (dest_ptr[index_] << 16) | pxflg | pval;         \
    if ((index_) != 7) {                                                   \
      if (hflip) tilepix <<= 4; else tilepix >>= 4;                        \
    }                                                                      \
  } while (0)

  CGBA_OPAQUE_ROW_PIXEL(0);
  CGBA_OPAQUE_ROW_PIXEL(1);
  CGBA_OPAQUE_ROW_PIXEL(2);
  CGBA_OPAQUE_ROW_PIXEL(3);
  CGBA_OPAQUE_ROW_PIXEL(4);
  CGBA_OPAQUE_ROW_PIXEL(5);
  CGBA_OPAQUE_ROW_PIXEL(6);
  CGBA_OPAQUE_ROW_PIXEL(7);

#undef CGBA_OPAQUE_ROW_PIXEL
}
#endif

#ifdef CGBA_VIDEO_OPAQUE_ROW_UNROLL
/* Emerald's measured Mode 0 path renders almost every base text row directly
 * into the u16 FULLCOLOR scanline.  Spell out those eight palette loads and
 * stores as well. Callers use this when a row is known opaque, or for any row
 * when the backdrop-shadow palette has already encoded color-zero semantics. */
template<bool hflip>
static inline void cgba_render_4bpp_row_fullcolor_u16(
  u32 tilepix, const u16 *subpal, u16 *dest_ptr)
{
#define CGBA_OPAQUE_FULLCOLOR_PIXEL(index_)                                \
  do {                                                                     \
    u32 pval = hflip ? (tilepix >> 28) : (tilepix & 0x0Fu);                \
    dest_ptr[index_] = subpal[pval];                                       \
    if ((index_) != 7) {                                                   \
      if (hflip) tilepix <<= 4; else tilepix >>= 4;                        \
    }                                                                      \
  } while (0)

  CGBA_OPAQUE_FULLCOLOR_PIXEL(0);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(1);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(2);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(3);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(4);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(5);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(6);
  CGBA_OPAQUE_FULLCOLOR_PIXEL(7);

#undef CGBA_OPAQUE_FULLCOLOR_PIXEL
}
#endif

template<typename dtype, rendtype rdtype, bool is8bpp, bool isbase, bool hflip>
static inline void render_tile_Nbpp(
  u32 bg_comb, u32 px_comb, dtype *dest_ptr, u16 tile,
  const u8 *tile_base, int vertical_pixel_flip, const u16 *paltbl
) {
  const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];
  u16 bgcolor = paltbl[0];

  if (tile & 0x800)
    tile_ptr += vertical_pixel_flip;

  if (is8bpp) {
    for (u32 j = 0; j < 2; j++) {
      u32 tilepix = eswap32(((u32*)tile_ptr)[hflip ? 1-j : j]);
      if (tilepix) {
        for (u32 i = 0; i < 4; i++, dest_ptr++) {
          u8 pval = hflip ? (tilepix >> 24) : tilepix;
          if (hflip) tilepix <<= 8; else tilepix >>= 8;
          if (pval) {
            if (rdtype == FULLCOLOR)
              *dest_ptr = paltbl[pval];
            else if (rdtype == INDXCOLOR)
              *dest_ptr = pval | px_comb;  // Add combine flags
            else if (rdtype == STCKCOLOR)
              *dest_ptr = pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16);
          }
          else if (isbase) {
            *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
          }
        }
      } else {
        for (u32 i = 0; i < 4; i++, dest_ptr++)
          if (isbase)
            *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
      }
    }
  } else {
    u32 tilepix = eswap32(*(u32*)tile_ptr);
#if defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && \
    defined(CGBA_VIDEO_OPAQUE_ROW_UNROLL)
    /* The base FULLCOLOR instantiation receives the backdrop-shadow palette,
     * so every nibble -- including color zero -- is an unconditional palette
     * lookup.  Dispatch before both the all-zero split and opacity classifier:
     * Emerald's measured Mode 0 base rows are all opaque, and classifying each
     * row would otherwise erase much of the unrolled writer's gain. */
    if (sizeof(dtype) == sizeof(u16) && rdtype == FULLCOLOR && isbase) {
#ifdef CGBA_VIDEO_PAIR_PALETTE
      cgba_render_4bpp_row_pairs<hflip>(tilepix, tile >> 12, (u16 *)dest_ptr);
#else
      u16 tilepal = (tile >> 12) << 4;
      cgba_render_4bpp_row_fullcolor_u16<hflip>(
        tilepix, &paltbl[tilepal], (u16 *)dest_ptr);
#endif
      return;
    }
#endif
    if (tilepix) {  // We can skip it all if the row is transparent
      u16 tilepal = (tile >> 12) << 4;
      u16 pxflg = px_comb | tilepal;
      const u16 *subpal = &paltbl[tilepal];
#if defined(CGBA_VIDEO_OPAQUE_TILE_FASTPATH) || \
    defined(CGBA_VIDEO_OPAQUE_ROW_UNROLL)
      /* A 4-bpp color index of zero is the only transparent value.  Hoist the
       * eight per-pixel transparency branches when every nibble is nonzero.
       * The subtraction is lane-wise in base 16 whenever all nibbles are at
       * least one; if any lane is zero, its high bit (and possibly later
       * borrow-affected lanes) is caught by the mask. */
      bool opaque = cgba_4bpp_row_is_opaque(tilepix);
#ifdef CGBA_VIDEO_OPAQUE_ROW_UNROLL
      if (opaque) {
        /* Keep specialization to the two measured buffer shapes so other
         * renderer instantiations do not inflate the SH4 I-cache footprint. */
        if (sizeof(dtype) == sizeof(u32) &&
            (rdtype == INDXCOLOR || rdtype == STCKCOLOR)) {
          cgba_render_opaque_4bpp_row_u32<rdtype, isbase, hflip>(
            tilepix, bg_comb, pxflg, (u32 *)dest_ptr);
          return;
        }
        if (sizeof(dtype) == sizeof(u16) && rdtype == FULLCOLOR) {
#ifdef CGBA_VIDEO_PAIR_PALETTE
          /* No nibble is zero, so the shadow-derived pairs equal the live
           * palette's. render_scanline_text_fast refreshed the shadow. */
          cgba_render_4bpp_row_pairs<hflip>(
            tilepix, tile >> 12, (u16 *)dest_ptr);
#else
          cgba_render_4bpp_row_fullcolor_u16<hflip>(
            tilepix, subpal, (u16 *)dest_ptr);
#endif
          return;
        }
      }
#endif
#ifdef CGBA_VIDEO_OPAQUE_TILE_FASTPATH
      if (opaque) {
        for (u32 i = 0; i < 8; i++, dest_ptr++) {
          u8 pval = (hflip ? (tilepix >> 28) : tilepix) & 0xF;
          if (hflip) tilepix <<= 4; else tilepix >>= 4;
          if (rdtype == FULLCOLOR)
            *dest_ptr = subpal[pval];
          else if (rdtype == INDXCOLOR)
            *dest_ptr = pxflg | pval;
          else if (rdtype == STCKCOLOR)
            *dest_ptr = pxflg | pval |
              ((isbase ? bg_comb : *dest_ptr) << 16);
        }
        return;
      }
#endif
#endif
      for (u32 i = 0; i < 8; i++, dest_ptr++) {
        u8 pval = (hflip ? (tilepix >> 28) : tilepix) & 0xF;
        if (hflip) tilepix <<= 4; else tilepix >>= 4;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
        if (rdtype == FULLCOLOR && isbase) {
          /* paltbl is the backdrop-shadow palette for this instantiation. */
          *dest_ptr = subpal[pval];
        }
        else
#endif
        if (pval) {
          if (rdtype == FULLCOLOR)
            *dest_ptr = subpal[pval];
          else if (rdtype == INDXCOLOR)
            *dest_ptr = pxflg | pval;
          else if (rdtype == STCKCOLOR)
            *dest_ptr = pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16);
        }
        else if (isbase) {
          *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
        }
      }
    } else if (isbase) {
      // In this case we simply fill the pixels with background pixels
      for (u32 i = 0; i < 8; i++, dest_ptr++)
        *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
    }
  }
}


/* Keep the cached and generic full-row loops separate at compile time. */
template<typename dtype, rendtype rdtype, bool is8bpp, bool isbase, bool hflip, bool cached>
static inline __attribute__((always_inline)) void render_text_tile(
  u32 bg_comb, u32 px_comb, dtype *dest_ptr, u16 tile,
  const u8 *tile_base, int vertical_pixel_flip, const u16 *paltbl)
{
#ifdef CGBA_VIDEO_TILE_ROW_CACHE
  if (cached) {
    const u8 *tile_ptr = tile_base + (tile & 0x3ff) * 32;
    if (tile & 0x800) tile_ptr += vertical_pixel_flip;
    u32 raw = *(const u32 *)tile_ptr;
    if (!raw) {
      if (isbase) {
        u16 backdrop = paltbl[0];
        for (unsigned i = 0; i < 8; i++) dest_ptr[i] = backdrop;
      }
      return;
    }
    render_cached_tile_row<isbase>(raw, tile, (u16 *)dest_ptr);
    return;
  }
#endif
  render_tile_Nbpp<dtype, rdtype, is8bpp, isbase, hflip>(
    bg_comb, px_comb, dest_ptr, tile, tile_base, vertical_pixel_flip, paltbl);
}

/* Full-tile run of one text-background scanline span.
 *
 * CGBA_VIDEO_EMPTY_RUN_SKIP: an overlay (non-base) 4-bpp tile row whose packed
 * word is zero draws nothing, and any following map entry with the same raw
 * value names the same tile, flips and row, so it draws nothing either.
 * Compare raw map entries to step over such runs instead of decoding each
 * tile again.  VRAM is read on every scanline, so there is no state to
 * invalidate. */
template<typename stype, rendtype rdtype, bool is8bpp, bool isbase, bool cached>
static inline __attribute__((always_inline)) void render_text_tile_run(
  u32 todraw, u16 *&map_ptr, stype *&dest_ptr, u32 bg_comb, u32 px_comb,
  const u8 *tile_base, s32 vflip_off, const u16 *paltbl)
{
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  if (sizeof(stype) == sizeof(u16) && rdtype == FULLCOLOR &&
      !is8bpp && !cached) {
    if (todraw) {
#ifdef CGBA_VIDEO_PAIR_PALETTE
      const u32 *pair_lut = pair_palette_get_all_u32();
      if (isbase) {
        cgba_render_4bpp_span_base_pair_sh4(
          map_ptr, todraw, (u16 *)dest_ptr, tile_base, vflip_off, pair_lut);
      } else {
        cgba_render_4bpp_span_over_pair_sh4(
          map_ptr, todraw, (u16 *)dest_ptr, tile_base, vflip_off,
          pair_lut, paltbl);
      }
#else
      if (isbase) {
        cgba_render_4bpp_span_base_sh4(
          map_ptr, todraw, (u16 *)dest_ptr, tile_base, vflip_off, paltbl);
      } else {
        u32 skip_zero = (*(const u32 *)tile_base == 0u) ? 0u : 1u;
        cgba_render_4bpp_span_over_sh4(
          map_ptr, todraw, (u16 *)dest_ptr, tile_base, vflip_off,
          paltbl, skip_zero);
      }
#endif
      map_ptr += todraw;
      dest_ptr += todraw * 8;
    }
    return;
  }
#endif
#ifdef CGBA_VIDEO_EMPTY_RUN_SKIP
  if (!isbase && !is8bpp && !cached) {
    u16 *map = map_ptr;
    u16 *map_end = map + todraw;
    stype *dst = dest_ptr;
    while (map < map_end) {
      u16 raw = *map;
      u16 tile = eswap16(raw);
      const u8 *row = tile_base + (tile & 0x3FF) * 32;
      if (tile & 0x800)
        row += vflip_off;
      if (!*(const u32 *)row) {     /* zero in either byte order */
        do {
          map++;
          dst += 8;
        } while (map < map_end && *map == raw);
        continue;
      }
      if (tile & 0x400)
        render_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
          bg_comb, px_comb, dst, tile, tile_base, vflip_off, paltbl);
      else
        render_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
          bg_comb, px_comb, dst, tile, tile_base, vflip_off, paltbl);
      map++;
      dst += 8;
    }
    map_ptr = map;
    dest_ptr = dst;
    return;
  }
#endif
  for (u32 i = 0; i < todraw; i++, dest_ptr += 8) {
    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      render_text_tile<stype, rdtype, is8bpp, isbase, true, cached>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
    else
      render_text_tile<stype, rdtype, is8bpp, isbase, false, cached>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
  }
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
/* Per-screenblock 32-bit row-occupancy cache (128 B in .bss), invalidated on
 * any BG VRAM write via cgba_vram_dirty.  Lets 4-bpp FULLCOLOR overlay layers
 * (such as Pokemon Emerald's empty BG0 UI layer while walking) skip an entire
 * scanline in ~18 instructions without scanning tiles or touching palettes. */
static struct {
  u32 valid_mask;
  u32 row_nonzero[32];
} cgba_sb;
#define cgba_sb_valid_mask (cgba_sb.valid_mask)
#define cgba_sb_row_nonzero (cgba_sb.row_nonzero)

static __attribute__((noinline)) u32 cgba_sb_scan_rows(u32 sb)
{
#ifdef MSB_FIRST
  const u32 id_vflip_mask = 0xFF0BFF0Bu;
#else
  const u32 id_vflip_mask = 0x0BFF0BFFu;
#endif
  const u32 *p = (const u32 *)&vram[sb * 2048u];
  u32 mask = 0;
  for (u32 row = 0; row < 32; row++) {
    /* Early-out on common non-empty rows after checking left + middle words. */
    if (((p[0] | p[7]) & id_vflip_mask) != 0u ||
        ((p[1] | p[2] | p[3] | p[4] | p[5] | p[6] |
          p[8] | p[9] | p[10] | p[11] | p[12] | p[13] | p[14] | p[15]) &
         id_vflip_mask) != 0u)
      mask |= (1u << row);
    p += 16;
  }
  cgba_sb_row_nonzero[sb] = mask;
  cgba_sb_valid_mask |= (1u << sb);
  return mask;
}

static inline bool cgba_sb_row_is_all_zero(u32 sb, u32 row_bit)
{
  if (sb >= 32u)
    return false;
  u32 mask = (cgba_sb_valid_mask & (1u << sb))
               ? cgba_sb_row_nonzero[sb]
               : cgba_sb_scan_rows(sb);
  return (mask & row_bit) == 0;
}
#endif

template<typename stype, rendtype rdtype, bool isbase, bool is8bpp, bool cached = false>
static void render_scanline_text_fast(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
#ifdef CGBA_VIDEO_TILE_ROW_CACHE
  if (cached) tile_row_observe_palette();
#endif
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  u16 vcount = read_ioreg(REG_VCOUNT);
  u32 map_size = (bg_control >> 14) & 0x03;
  u32 voffset = (vcount + read_ioreg(REG_BGxVOFS(layer))) % 512;
  u32 base_block = (bg_control >> 8) & 0x1F;
  u32 tilewidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
  u32 vert_pix_offset = (voffset % 8) * tilewidth;
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  u8 *tile_base = &vram[tilecntrl * 16*1024 + vert_pix_offset];

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  /* Fast exit for empty 4-bpp FULLCOLOR overlay rows: if every map entry on
   * this tile row is 0x0000 and tile 0's row is all-zero, nothing can be drawn. */
  if (!isbase && !is8bpp && rdtype == FULLCOLOR && sizeof(stype) == sizeof(u16)) {
    u32 sb0 = base_block +
              (((map_size & 0x02) && (voffset >= 256))
                 ? ((map_size & 0x01) ? 2u : 1u) : 0u);
    u32 row_bit = 1u << ((voffset & 255u) >> 3);
    if (cgba_sb_row_is_all_zero(sb0, row_bit) &&
        (!(map_size & 0x01) || cgba_sb_row_is_all_zero(sb0 + 1u, row_bit)) &&
        *(const u32 *)tile_base == 0u)
      return;
  }
#endif

  u32 map_width = map_widths[map_size];
  u32 hoffset = (start + read_ioreg(REG_BGxHOFS(layer))) % 512;
  stype *dest_ptr = ((stype*)scanline) + start;

  // Calculate combine masks. These store 2 bits of info: 1st and 2nd target.
  // If set, the current pixel belongs to a layer that is 1st or 2nd target.
  u32 bg_comb = (rdtype == FULLCOLOR) ? 0u : color_flags(5);
  u32 px_comb = (rdtype == FULLCOLOR) ? 0u : color_flags(layer);

#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
  /* tile_render_layers calls this with palette_ram_converted.  The shadow
   * differs only at each 4-bpp sub-palette's color-zero slot, where it holds
   * the backdrop selected by the old transparent-pixel branch. */
  if (rdtype == FULLCOLOR && isbase && !is8bpp)
    paltbl = get_backdrop_shadow_palette();
#ifdef CGBA_VIDEO_PAIR_PALETTE
  /* Opaque overlay rows also draw through the pair table, which must track
   * the live BG palette (text layers always receive palette_ram_converted). */
  else if (rdtype == FULLCOLOR && !is8bpp && sizeof(stype) == sizeof(u16))
    (void)get_backdrop_shadow_palette();
#endif
#endif

  // Background map data is in vram, at an offset specified in 2K blocks.
  // (each map data block is 32x32 tiles, at 16bpp, so 2KB)
  u16 *map_base = (u16 *)&vram[base_block * 2048];
  u16 *map_ptr, *second_ptr;

  end -= start;

  // Skip the top one/two block(s) if using the bottom half
  if ((map_size & 0x02) && (voffset >= 256))
    map_base += ((map_width / 8) * 32);

  // Skip the top tiles within the block
  map_base += (((voffset % 256) / 8) * 32);

  // we might need to render from two charblocks, store a second pointer.
  second_ptr = map_ptr = map_base;

  if(map_size & 0x01) {    // If background is 512 pixels wide
    if(hoffset >= 256) {
      // If we are rendering the right block, skip a whole charblock
      hoffset -= 256;
      map_ptr += (32 * 32);
    } else {
      // If we are rendering the left block, we might overrun into the right
      second_ptr += (32 * 32);
    }
  } else {
    hoffset %= 256;     // Background is 256 pixels wide
  }

  // Skip the left blocks within the block
  map_ptr += hoffset / 8;

  // Calculate the pixel offset between a line and its "flipped" mirror.
  // The values can be {56, 40, 24, 8, -8, -24, -40, -56}
  s32 vflip_off = is8bpp ?
       tile_size_8bpp - 2*vert_pix_offset - tile_width_8bpp :
       tile_size_4bpp - 2*vert_pix_offset - tile_width_4bpp;
  // Number of pixels available until the end of the tile block
  u32 pixel_run = 256 - hoffset;

  u32 tile_hoff = hoffset % 8;
  u32 partial_hcnt = 8 - tile_hoff;

  if (tile_hoff) {
    // First partial tile, only right side is visible.
    u32 todraw = MIN(end, partial_hcnt); // [1..7]
    u32 stop = tile_hoff + todraw;   // Usually 8, unless short run.

    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, tile_hoff, stop, tile, tile_base, vflip_off, paltbl);
    else
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, tile_hoff, stop, tile, tile_base, vflip_off, paltbl);

    dest_ptr += todraw;
    end -= todraw;
    pixel_run -= todraw;
  }

  if (!end)
    return;

  // Now render full tiles
  u32 todraw = MIN(end, pixel_run) / 8;

  render_text_tile_run<stype, rdtype, is8bpp, isbase, cached>(
    todraw, map_ptr, dest_ptr, bg_comb, px_comb, tile_base, vflip_off, paltbl);

  end -= todraw * 8;
  pixel_run -= todraw * 8;

  if (!end)
    return;

  // Switch to the next char block if we ran out of tiles
  if (!pixel_run)
    map_ptr = second_ptr;

  todraw = end / 8;
  render_text_tile_run<stype, rdtype, is8bpp, isbase, cached>(
    todraw, map_ptr, dest_ptr, bg_comb, px_comb, tile_base, vflip_off, paltbl);

  end -= todraw * 8;

  // Finalize the tile rendering the left side of it (from 0 up to "end").
  if (end) {
    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, 0, end, tile, tile_base, vflip_off, paltbl);
    else
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, 0, end, tile, tile_base, vflip_off, paltbl);
  }
}

// A slow version of the above function that allows for mosaic effects
template<typename stype, rendtype rdtype, bool isbase, bool is8bpp>
static __attribute__((noinline)) void render_scanline_text_mosaic(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  const u32 mosh = (read_ioreg(REG_MOSAIC) & 0xF) + 1;
  const u32 mosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xF) + 1;
  u16 vcount = read_ioreg(REG_VCOUNT);
  u32 map_size = (bg_control >> 14) & 0x03;
  u32 map_width = map_widths[map_size];
  u32 hoffset = (start + read_ioreg(REG_BGxHOFS(layer))) % 512;
  u16 vmosoff = vcount - vcount % mosv;
  u32 voffset = (vmosoff + read_ioreg(REG_BGxVOFS(layer))) % 512;
  stype *dest_ptr = ((stype*)scanline) + start;

  u32 bg_comb = color_flags(5), px_comb = color_flags(layer);

  u32 base_block = (bg_control >> 8) & 0x1F;
  u16 *map_base = (u16 *)&vram[base_block * 2048];
  u16 *map_ptr, *second_ptr;

  if ((map_size & 0x02) && (voffset >= 256))
    map_base += ((map_width / 8) * 32);

  map_base += (((voffset % 256) / 8) * 32);

  second_ptr = map_ptr = map_base;

  if(map_size & 0x01) {    // If background is 512 pixels wide
    if(hoffset >= 256) {
      // If we are rendering the right block, skip a whole charblock
      hoffset -= 256;
      map_ptr += (32 * 32);
    } else {
      // If we are rendering the left block, we might overrun into the right
      second_ptr += (32 * 32);
    }
  } else {
    hoffset %= 256;     // Background is 256 pixels wide
  }

  // Skip the left blocks within the block
  map_ptr += hoffset / 8;

  // Render a single scanline of text tiles
  u32 tilewidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
  u32 vert_pix_offset = (voffset % 8) * tilewidth;
  // Calculate the pixel offset between a line and its "flipped" mirror.
  // The values can be {56, 40, 24, 8, -8, -24, -40, -56}
  s32 vflip_off = is8bpp ?
       tile_size_8bpp - 2*vert_pix_offset - tile_width_8bpp :
       tile_size_4bpp - 2*vert_pix_offset - tile_width_4bpp;

  // The tilemap base is selected via bgcnt (16KiB chunks)
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  // Account for the base offset plus the tile vertical offset
  u8 *tile_base = &vram[tilecntrl * 16*1024 + vert_pix_offset];

  u16 bgcolor = paltbl[0];

  // Iterate pixel by pixel, loading data every N pixels to honor mosaic effect
  u8 pval = 0;
  u32 mctr = 0;
  for (u32 i = 0; start < end; start++, i++, dest_ptr++) {
    u16 tile = eswap16(*map_ptr);

    // Counter-based mosaic (see render_affine_background for rationale).
    if (!mctr) {
      const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];

      bool hflip = (tile & 0x400);
      if (tile & 0x800)
        tile_ptr += vflip_off;

      // Load byte or nibble with pixel data.
      if (is8bpp) {
        if (hflip)
          pval = tile_ptr[7 - hoffset % 8];
        else
          pval = tile_ptr[hoffset % 8];
      } else {
        if (hflip)
          pval = (tile_ptr[(7 - hoffset % 8) >> 1] >> (((hoffset & 1) ^ 1) * 4)) & 0xF;
        else
          pval = (tile_ptr[(hoffset % 8) >> 1] >> ((hoffset & 1) * 4)) & 0xF;
      }
      mctr = mosh;
    }
    mctr--;

    if (is8bpp) {
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = paltbl[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_comb;  // Add combine flags
        else if (rdtype == STCKCOLOR)
          *dest_ptr = pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16);
      }
      else if (isbase) {
        *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
      }
    } else {
      u16 tilepal = (tile >> 12) << 4;
      u16 pxflg = px_comb | tilepal;
      const u16 *subpal = &paltbl[tilepal];
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = subpal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pxflg | pval;
        else if (rdtype == STCKCOLOR)
          *dest_ptr = pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16);
      }
      else if (isbase) {
        *dest_ptr = (rdtype == FULLCOLOR) ? bgcolor : 0 | bg_comb;
      }
    }

    // Need to continue from the next charblock
    hoffset++;
    if (hoffset % 8 == 0)
      map_ptr++;
    if (hoffset >= 256) {
      hoffset = 0;
      map_ptr = second_ptr;
    }
  }
}

template<typename stype, rendtype rdtype, bool isbase>
static __attribute__((noinline)) void render_scanline_text_fast_8bpp(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  render_scanline_text_fast<stype, rdtype, isbase, true>(
    layer, start, end, scanline, paltbl);
}

template<typename stype, rendtype rdtype, bool isbase>
static void render_scanline_text(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  // Tile mode has 4 and 8 bpp modes.
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  bool is8bpp = (bg_control & 0x80);
  bool has_mosaic = (bg_control & 0x40) && ((read_ioreg(REG_MOSAIC) & 0xFF) != 0);

#ifdef CGBA_VIDEO_TILE_ROW_CACHE
  /* Dispatch once per span. A separate instantiation keeps the cache hit
   * path inlined without pulling the generic renderer into every tile. */
  if (sizeof(stype) == sizeof(u16) && rdtype == FULLCOLOR && !is8bpp &&
      !has_mosaic && !(read_ioreg(REG_DISPCNT) & 7) && paltbl == palette_ram_converted) {
    render_scanline_text_fast<stype, rdtype, isbase, false, true>(
      layer, start, end, scanline, paltbl);
    return;
  }
#endif

  if (__builtin_expect(has_mosaic, 0)) {
    if (is8bpp)
      render_scanline_text_mosaic<stype, rdtype, isbase, true>(
        layer, start, end, scanline, paltbl);
    else
      render_scanline_text_mosaic<stype, rdtype, isbase, false>(
        layer, start, end, scanline, paltbl);
  } else {
    if (__builtin_expect(is8bpp, 0))
      render_scanline_text_fast_8bpp<stype, rdtype, isbase>(
        layer, start, end, scanline, paltbl);
    else
      render_scanline_text_fast<stype, rdtype, isbase, false>(
        layer, start, end, scanline, paltbl);
  }
}

static inline u8 lookup_pix_8bpp(
  u32 px, u32 py, const u8 *tile_base, const u8 *map_base, u32 map_size
) {
  // Pitch represents the log2(number of tiles per row) (from 16 to 128)
  u32 map_pitch = map_size + 4;
  // Given coords (px,py) in the background space, find the tile.
  u32 mapoff = (px / 8) + ((py / 8) << map_pitch);
  // Each tile is 8x8, so 64 bytes each.
  const u8 *tile_ptr = &tile_base[map_base[mapoff] * tile_size_8bpp];
  // Read the 8bit color within the tile.
  return tile_ptr[(px % 8) + ((py % 8) * 8)];
}


template<typename dsttype, rendtype rdtype, bool isbase>
static inline void rend_pix_8bpp(
  dsttype *dest_ptr, u8 pval, u32 bg_comb, u32 px_comb, const u16 *pal
) {
  // Alhpa mode stacks previous value (unless rendering the first layer)
  if (pval) {
    if (rdtype == FULLCOLOR)
      *dest_ptr = pal[pval];
    else if (rdtype == INDXCOLOR)
      *dest_ptr = pval | px_comb;  // Add combine flags
    else if (rdtype == STCKCOLOR)
      // Stack pixels. If base, stack the base pixel.
      *dest_ptr = pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16);
  }
  else if (isbase) {
    // Transparent pixel, but we are base layer, so render background.
    if (rdtype == FULLCOLOR)
      *dest_ptr = pal[0];
    else
      *dest_ptr = 0 | bg_comb;  // Just backdrop color and combine flags
  }
}

template<typename dsttype, rendtype rdtype>
static inline void render_bdrop_pixel_8bpp(
  dsttype *dest_ptr, u32 bg_comb, u16 bgcol) {
  // Alhpa mode stacks previous value (unless rendering the first layer)
  if (rdtype == FULLCOLOR)
    *dest_ptr = bgcol;
  else
    *dest_ptr = 0 | bg_comb;
}

typedef void (*affine_render_function) (
  u32 layer, u32 start, u32 cnt, const u8 *map_base,
  u32 map_size, const u8 *tile_base, void *dst_ptr,
  const u16* pal);

// Affine background rendering logic.
// wrap extends the background infinitely, otherwise transparent/backdrop fill
// rotate indicates if there's any rotation (optimized version for no-rotation)
// mosaic applies to horizontal mosaic (vertical is adjusted via affine ref)
template <typename dtype, rendtype rdtype,
          bool isbase, bool wrap, bool rotate, bool mosaic>
static inline void render_affine_background(
  u32 layer, u32 start, u32 cnt, const u8 *map_base,
  u32 map_size, const u8 *tile_base, void *dst_ptr_raw,
  const u16* pal) {

  dtype *dst_ptr = (dtype*)dst_ptr_raw;
  // Backdrop and current layer combine bits.
  u32 bg_comb = color_flags(5);
  u32 px_comb = color_flags(layer);

  s32 dx = (s16)read_ioreg(REG_BGxPA(layer));
  s32 dy = (s16)read_ioreg(REG_BGxPC(layer));

  s32 source_x = affine_reference_x[layer - 2] + (start * dx);
  s32 source_y = affine_reference_y[layer - 2] + (start * dy);

  // Maps are squared, four sizes available (128x128 to 1024x1024)
  u32 width_height = 128 << map_size;

  // Horizontal mosaic effect.
  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC)) & 0xF : 0) + 1;

  if (wrap) {
    // In wrap mode the entire space is covered, since it "wraps" at the edges
    u8 pval = 0;
    u32 mctr = 0;
    if (rotate) {
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8) & (width_height-1);
        u32 pix_y = (u32)(source_y >> 8) & (width_height-1);

        // Lookup pixel and draw it (only every Nth if mosaic is on).  The
        // counter form avoids a divl per pixel: mosh is loop-invariant but
        // runtime-valued, so gcc/clang do not strength-reduce 'i % mosh'
        // into a multiply-by-magic-constant sequence.  When mosaic == false
        // the counter machinery is dead-code-eliminated at compile time.
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;

        source_x += dx; source_y += dy;  // Move to the next pixel
      }
    } else {
      // Y coordinate stays contant across the walk.
      const u32 pix_y = (u32)(source_y >> 8) & (width_height-1);
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8) & (width_height-1);
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;
        source_x += dx;  // Only moving in the X direction.
      }
    }
  } else {
    u16 bgcol = pal[0];
    if (rotate) {
      // Draw backdrop pixels if necessary until we reach the background edge.
      while (cnt) {
        // Draw backdrop pixels if they lie outside of the background.
        u32 pix_x = (u32)(source_x >> 8), pix_y = (u32)(source_y >> 8);

        // Stop once we find a pixel that is actually *inside* the map.
        if (pix_x < width_height && pix_y < width_height)
          break;

        // Draw a backdrop pixel if we are the base layer.
        if (isbase)
          render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr, bg_comb, bgcol);

        dst_ptr++;
        source_x += dx; source_y += dy;
        cnt--;
      }

      // Draw background pixels by looking them up in the map
      u8 pval = 0;
      u32 mctr = 0;
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8), pix_y = (u32)(source_y >> 8);

        // Check if we run out of background pixels, stop drawing.
        if (pix_x >= width_height || pix_y >= width_height)
          break;

        // Lookup pixel and draw it.
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;

        // Move to the next pixel, update coords accordingly
        source_x += dx; source_y += dy;
      }
    } else {
      // Specialized version for scaled-only backgrounds
      u8 pval = 0;
      const u32 pix_y = (u32)(source_y >> 8);
      if (pix_y < width_height) {  // Check if within Y-coord range
        // Draw/find till left edge
        while (cnt) {
          u32 pix_x = (u32)(source_x >> 8);
          if (pix_x < width_height)
            break;

          if (isbase)
            render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr, bg_comb, bgcol);

          dst_ptr++;
          source_x += dx;
          cnt--;
        }
        // Draw actual background
        u32 mctr = 0;
        for (u32 i = 0; cnt; i++, cnt--) {
          u32 pix_x = (u32)(source_x >> 8);
          if (pix_x >= width_height)
            break;

          if (!mosaic || !mctr) {
            pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
            mctr = mosh;
          }
          rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
          if (mosaic) mctr--;

          source_x += dx;
        }
      }
    }

    // Complete the line on the right, if we ran out over the bg edge.
    // Only necessary for the base layer, otherwise we can safely finish.
    if (isbase)
      while (cnt--)
        render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr++, bg_comb, bgcol);
  }
}


// Renders affine backgrounds. These differ substantially from non-affine
// ones. Tile maps are byte arrays (instead of 16 bit), limiting the map to
// 256 different tiles (with no flip bits and just one single 256 color pal).
// Optimize for common cases: wrap/non-wrap, scaling/rotation.
template<typename dsttype, rendtype rdtype, bool isbase>
static void render_scanline_affine(u32 layer,
 u32 start, u32 end, void *scanline, const u16 *pal)
{
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  u32 map_size = (bg_control >> 14) & 0x03;

  // Char block base pointer
  u32 base_block = (bg_control >> 8) & 0x1F;
  u8 *map_base = &vram[base_block * 2048];
  // The tilemap base is selected via bgcnt (16KiB chunks)
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  u8 *tile_base = &vram[tilecntrl * 16*1024];

  dsttype *dest_ptr = ((dsttype*)scanline) + start;
  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;

  bool has_mosaic = (bg_control & 0x40) && (mosamount != 0);
  bool has_rotation = read_ioreg(REG_BGxPC(layer)) != 0;
  bool has_wrap = (bg_control >> 13) & 1;

  // Number of pixels to render
  u32 cnt = end - start;

  // Four specialized versions for faster rendering on specific cases like
  // scaling only or non-wrapped backgrounds.
  u32 fidx = (has_wrap     ? 0x4 : 0) |
             (has_rotation ? 0x2 : 0) |
             (has_mosaic   ? 0x1 : 0);

  static const affine_render_function rdfns[8] = {
    render_affine_background<dsttype, rdtype, isbase, false, false, false>,
    render_affine_background<dsttype, rdtype, isbase, false, false, true>,
    render_affine_background<dsttype, rdtype, isbase, false, true,  false>,
    render_affine_background<dsttype, rdtype, isbase, false, true,  true>,
    render_affine_background<dsttype, rdtype, isbase, true,  false, false>,
    render_affine_background<dsttype, rdtype, isbase, true,  false, true>,
    render_affine_background<dsttype, rdtype, isbase, true,  true,  false>,
    render_affine_background<dsttype, rdtype, isbase, true,  true,  true>,
  };

  rdfns[fidx](layer, start, cnt, map_base, map_size, tile_base, dest_ptr, pal);
}

template<rendtype rdmode, typename buftype, unsigned mode, typename pixfmt>
static inline void bitmap_pixel_write(
  buftype *dst_ptr, pixfmt val, const u16 * palptr, u16 px_attr
) {
  if (mode != 4)
    *dst_ptr = convert_palette(val); // Direct color, u16 bitmap
  else if (val) {
    if (rdmode == FULLCOLOR)
      *dst_ptr = palptr[val];
    else if (rdmode == INDXCOLOR)
      *dst_ptr = val | px_attr;  // Add combine flags
    else if (rdmode == STCKCOLOR)
      *dst_ptr = val | px_attr | ((*dst_ptr) << 16);  // Stack pixels
  }
}


typedef enum
{
  BLIT,     // The bitmap has no scaling nor rotation on the X axis
  SCALED,   // The bitmap features some scaling (on the X axis) but no rotation
  ROTATED   // Bitmap has rotation (and perhaps scaling too)
} bm_rendmode;

// Renders a bitmap honoring the pixel mode and any affine transformations.
// There's optimized versions for bitmaps without scaling / rotation.

template<rendtype rdtype, typename dsttype, // Rendering target type and format
         unsigned mode,
         typename pixfmt,                   // Bitmap source pixel format (8/16)
         unsigned width, unsigned height,   // Bitmap size (not screen!)
         bm_rendmode rdmode,                // Rendering mode optimization.
         bool mosaic>                       // Whether mosaic effect is used.
static inline void render_scanline_bitmap(
  u32 start, u32 end, void *scanline, const u16 * palptr
) {
  s32 dx = (s16)read_ioreg(REG_BG2PA);
  s32 dy = (s16)read_ioreg(REG_BG2PC);
  s32 source_x = affine_reference_x[0] + (start * dx); // Always BG2
  s32 source_y = affine_reference_y[0] + (start * dy);

  // Premature abort render optimization if bitmap out of Y coordinate.
  if ((rdmode != ROTATED) && ((u32)(source_y >> 8)) >= height)
    return;

  // Modes 4 and 5 feature double buffering.
  bool second_frame = (mode >= 4) && (read_ioreg(REG_DISPCNT) & 0x10);
  pixfmt *src_ptr = (pixfmt*)&vram[second_frame ? 0xA000 : 0x0000];
  dsttype *dst_ptr = ((dsttype*)scanline) + start;
  u16 px_attr = color_flags(2);   // Always BG2

  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC)) & 0xF : 0) + 1;

  if (rdmode == BLIT) {
    // We just blit pixels (copy) from buffer to buffer.
    const u32 pixel_y = (u32)(source_y >> 8);
    if (source_x < 0) {
      // The bitmap starts somewhere after "start", skip those pixels.
      u32 delta = (-source_x + 255) >> 8;
      dst_ptr += delta;
      start += delta;
      source_x = 0;
    }

    u32 pixel_x = (u32)(source_x >> 8);
    u32 pixcnt = MIN(end - start, width - pixel_x);
    pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; pixcnt; i++, pixcnt--, valptr++) {
      // Pretty much pixel copier (counter-based mosaic: see render_affine_background)
      if (!mosaic || !mctr) {
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }
      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr);
      if (mosaic) mctr--;
    }
  }
  else if (rdmode == SCALED) {
    // Similarly to above, but now we need to sample pixels instead.
    const u32 pixel_y = (u32)(source_y >> 8);

    // Find the "inside" of the bitmap
    while (start < end) {
      u32 pixel_x = (u32)(source_x >> 8);
      if (pixel_x < width)
        break;
      source_x += dx;
      start++;
      dst_ptr++;
    }

    u32 cnt = end - start;
    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; cnt; i++, cnt--) {
      u32 pixel_x = (u32)(source_x >> 8);
      if (pixel_x >= width)
        break;  // We reached the end of the bitmap

      if (!mosaic || !mctr) {
        pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }

      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr);
      if (mosaic) mctr--;
      source_x += dx;
    }
  } else {
    // Look for the first pixel to be drawn.
    while (start < end) {
      u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);
      if (pixel_x < width && pixel_y < height)
        break;
      start++;
      dst_ptr++;
      source_x += dx;  source_y += dy;
    }

    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; start < end; start++, i++) {
      u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

      // Check if we run out of background pixels, stop drawing.
      if (pixel_x >= width || pixel_y >= height)
        break;

      // Lookup pixel and draw it.  Counter-based mosaic (see
      // render_affine_background).  Before this fix the loop counter
      // 'i' was declared but never incremented; gcc constant-folded
      // 'i % mosh' to !0 and the mosaic skip never triggered, so the
      // rotated-bitmap mode-3/4/5 paths sampled every pixel instead of
      // every Nth.  Matches the SCALED-bitmap and affine-object paths,
      // which both honor BG2 horizontal mosaic correctly.
      if (!mosaic || !mctr) {
        pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }

      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr);
      if (mosaic) mctr--;

      // Move to the next pixel, update coords accordingly
      source_x += dx;
      source_y += dy;
    }
  }
}

// Object/Sprite rendering logic

static const u8 obj_dim_table[3][4][2] = {
  { {8, 8}, {16, 16}, {32, 32}, {64, 64} },
  { {16, 8}, {32, 8}, {32, 16}, {64, 32} },
  { {8, 16}, {8, 32}, {16, 32}, {32, 64} }
};

static u8 obj_priority_list[5][160][128] CGBA_HIGH_BSS;
static u8 obj_priority_count[5][160] CGBA_HIGH_BSS;
static u8 obj_alpha_count[160];
static u8 obj_row_prio_mask[160];

typedef struct {
  s32 obj_x, obj_y;
  s32 obj_w, obj_h;
  u32 attr1, attr2;
  bool is_double;
} t_sprite;

#ifndef CGBA_VIDEO_SPRITE_DECODE_CACHE
#define CGBA_VIDEO_SPRITE_DECODE_CACHE 0
#endif

static t_sprite decode_sprite(const t_oam *entry)
{
  u16 a0 = eswap16(entry->attr0), a1 = eswap16(entry->attr1);
  u32 shape = a0 >> 14, size = a1 >> 14;
  t_sprite obj = {
    .obj_x = (s32)(a1 << 23) >> 23,
    .obj_y = (a0 & 255) > 160 ? (a0 & 255) - 256 : (a0 & 255),
    .obj_w = obj_dim_table[shape][size][0],
    .obj_h = obj_dim_table[shape][size][1],
    .attr1 = a1, .attr2 = eswap16(entry->attr2),
    .is_double = (a0 & 0x200) != 0,
  };
  return obj;
}

#if CGBA_VIDEO_SPRITE_DECODE_CACHE
/* Rebuilt with the visibility lists on every OAM mutation, including native
 * stores, DMA, reset and savestate load. Affine matrices remain live OAM reads. */
struct cgba_decoded_obj {
  t_sprite sprite;
  u16 attr0;
  u16 fast_tile_base;  /* ((attr2 & 0x3FF) << 5) + (hflip ? (row_bytes - 32) : 0) */
  u16 fast_span;       /* (num_tiles - 1) << 5 */
  u8  fast_flags;      /* bit0=!aff&!8bpp, bit1=hflip, bit2=vflip, bit3=emosaic */
  u8  fast_num_tiles;  /* obj_w >> 3 */
  u8  fast_pal_off;    /* (attr2 >> 8) & 0xF0 */
  u8  fast_row_shift;  /* log2(obj_w << 2): 5/6/7/8 for 1D OBJ mapping */
};
static cgba_decoded_obj decoded_objs[128];
#endif

// Renders a tile row (8 pixels) for a regular (non-affine) object/sprite.
// tile_offset points to the VRAM offset where the data lives.
template<typename dsttype, rendtype rdtype, bool is8bpp, bool hflip>
static inline void render_obj_part_tile_Nbpp(
  u32 px_comb, dsttype *dest_ptr, u32 start, u32 end,
  u32 tile_offset, u16 palette, const u16 *pal
) {
  // Note that the last VRAM bank wrap around, hence the offset aliasing
  const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];
  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  if (is8bpp) {
    // Each byte is a color, mapped to a palete.
    for (u32 i = start; i < end; i++, dest_ptr++) {
      // Honor hflip by selecting bytes in the correct order
      u32 sel = hflip ? (7-i) : i;
      u8 pval = tile_ptr[sel];
      // Alhpa mode stacks previous value
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = pal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_attr;  // Add combine flags
        else if (rdtype == STCKCOLOR) {
          // Stack pixels on top of the pixel value and combine flags
          // We do not stack OBJ on OBJ, rather overwrite the previous object
          if (*dest_ptr & 0x100)
            *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
          else
            *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
        }
        else if (rdtype == PIXCOPY)
          *dest_ptr = dest_ptr[240];
      }
    }
  } else {
    // Only 32 bits (8 pixels * 4 bits)
    for (u32 i = start; i < end; i++, dest_ptr++) {
      u32 selb = hflip ? (3-i/2) : i/2;
      u32 seln = hflip ? ((i & 1) ^ 1) : (i & 1);
      u8 pval = (tile_ptr[selb] >> (seln * 4)) & 0xF;
      const u16 *subpal = &pal[palette];
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = subpal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_attr;
        else if (rdtype == STCKCOLOR) {
          if (*dest_ptr & 0x100)
            *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
          else
            *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);  // Stack pixels
        }
        else if (rdtype == PIXCOPY)
          *dest_ptr = dest_ptr[240];
      }
    }
  }
}

// Same as above but optimized for full tiles
template<typename dsttype, rendtype rdtype, bool is8bpp, bool hflip>
static inline void render_obj_tile_Nbpp(u32 px_comb,
  dsttype *dest_ptr, u32 tile_offset, u16 palette, const u16 *pal
) {
  const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];
  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  if (is8bpp) {
    for (u32 j = 0; j < 2; j++) {
      u32 tilepix = eswap32(((u32*)tile_ptr)[hflip ? 1-j : j]);
      if (tilepix) {
        for (u32 i = 0; i < 4; i++, dest_ptr++) {
          u8 pval = hflip ? (tilepix >> 24) : tilepix;
          if (hflip) tilepix <<= 8; else tilepix >>= 8;
          if (pval) {
            if (rdtype == FULLCOLOR)
              *dest_ptr = pal[pval];
            else if (rdtype == INDXCOLOR)
              *dest_ptr = pval | px_attr;  // Add combine flags
            else if (rdtype == STCKCOLOR) {
              if (*dest_ptr & 0x100)
                *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
              else
                *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
            }
            else if (rdtype == PIXCOPY)
              *dest_ptr = dest_ptr[240];
          }
        }
      } else
        dest_ptr += 4;
    }
  } else {
    u32 raw = *(const u32 *)tile_ptr;
    if (raw) {   // Can skip all pixels if the row is just transparent
      u32 tilepix = eswap32(raw);
      const u16 *subpal = &pal[palette];
      if (rdtype == FULLCOLOR && sizeof(dsttype) == sizeof(u16)) {
        u32 pval;
        u32 lo = tilepix & 0xFFFFu;
        u32 hi = tilepix >> 16;
        if (hflip) {
          if (hi) {
            if ((pval = hi >> 12))          dest_ptr[0] = subpal[pval];
            if ((pval = (hi >> 8) & 0xFu))  dest_ptr[1] = subpal[pval];
            if ((pval = (hi >> 4) & 0xFu))  dest_ptr[2] = subpal[pval];
            if ((pval = hi & 0xFu))         dest_ptr[3] = subpal[pval];
          }
          if (lo) {
            if ((pval = lo >> 12))          dest_ptr[4] = subpal[pval];
            if ((pval = (lo >> 8) & 0xFu))  dest_ptr[5] = subpal[pval];
            if ((pval = (lo >> 4) & 0xFu))  dest_ptr[6] = subpal[pval];
            if ((pval = lo & 0xFu))         dest_ptr[7] = subpal[pval];
          }
        } else {
          if (lo) {
            if ((pval = lo & 0xFu))         dest_ptr[0] = subpal[pval];
            if ((pval = (lo >> 4) & 0xFu))  dest_ptr[1] = subpal[pval];
            if ((pval = (lo >> 8) & 0xFu))  dest_ptr[2] = subpal[pval];
            if ((pval = lo >> 12))          dest_ptr[3] = subpal[pval];
          }
          if (hi) {
            if ((pval = hi & 0xFu))         dest_ptr[4] = subpal[pval];
            if ((pval = (hi >> 4) & 0xFu))  dest_ptr[5] = subpal[pval];
            if ((pval = (hi >> 8) & 0xFu))  dest_ptr[6] = subpal[pval];
            if ((pval = hi >> 12))          dest_ptr[7] = subpal[pval];
          }
        }
        return;
      }
      for (u32 i = 0; i < 8; i++, dest_ptr++) {
        u8 pval = (hflip ? (tilepix >> 28) : tilepix) & 0xF;
        if (hflip) tilepix <<= 4; else tilepix >>= 4;
        if (pval) {
          if (rdtype == FULLCOLOR)
            *dest_ptr = subpal[pval];
          else if (rdtype == INDXCOLOR)
            *dest_ptr = pval | px_attr;
          else if (rdtype == STCKCOLOR) {  // Stack background, replace sprite
            if (*dest_ptr & 0x100)
              *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
            else
              *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
          }
          else if (rdtype == PIXCOPY)
            *dest_ptr = dest_ptr[240];
        }
      }
    }
  }
}

// Renders a regular sprite (non-affine) row to screen.
// delta_x is the object X coordinate referenced from the window start.
// cnt is the maximum number of pixels to draw, honoring window, obj width, etc.
template <typename stype, rendtype rdtype, bool is8bpp, bool hflip>
static void render_object(
  s32 delta_x, u32 cnt, stype *dst_ptr, u32 tile_offset, u32 px_comb,
  u16 palette, const u16* palptr
) {
  // Tile size in bytes for each mode
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  // Number of bytes to advance (or rewind) on the tile map
  const s32 tile_size_off = hflip ? -tile_bsize : tile_bsize;

  if (delta_x < 0) {      // Left part is outside of the screen/window.
    u32 offx = -delta_x;  // How many pixels did we skip from the object?
    s32 block_off = offx / 8;
    u32 tile_off = offx % 8;

    // Skip the first object tiles (skips in the flip direction)
    tile_offset += block_off * tile_size_off;

    // Render a partial tile to the left
    if (tile_off) {
      u32 residual = 8 - tile_off;   // Pixel count to complete the first tile
      u32 maxpix = MIN(residual, cnt);
      render_obj_part_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
        px_comb, dst_ptr, tile_off, tile_off + maxpix, tile_offset, palette, palptr);

      // Move to the next tile
      tile_offset += tile_size_off;
      // Account for drawn pixels
      cnt -= maxpix;
      dst_ptr += maxpix;
    }
  } else {
    // Render object completely from the left. Skip the empty space to the left
    dst_ptr += delta_x;
  }

  // Render full tiles to the scan line.
  s32 num_tiles = cnt / 8;
#ifdef CGBA_SH4_ASM_4BPP
  if (rdtype == FULLCOLOR && !is8bpp && sizeof(stype) == sizeof(u16) && num_tiles > 0) {
    u32 off = tile_offset & 0x7FFFu;
    u32 span = (u32)(num_tiles - 1) << 5;
    if ((!hflip && off + span < 0x8000u) || (hflip && off >= span)) {
      if (hflip)
        cgba_render_obj_4bpp_tiles_hf_sh4(
          &vram[0x10000u + off], (u32)num_tiles, (u16 *)dst_ptr, &palptr[palette]);
      else
        cgba_render_obj_4bpp_tiles_sh4(
          &vram[0x10000u + off], (u32)num_tiles, (u16 *)dst_ptr, &palptr[palette]);
      tile_offset += (u32)num_tiles * (u32)tile_size_off;
      dst_ptr += num_tiles * 8;
      num_tiles = 0;
    }
  }
#endif
  while (num_tiles--) {
    // Render full tiles
    render_obj_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
      px_comb, dst_ptr, tile_offset, palette, palptr);
    tile_offset += tile_size_off;
    dst_ptr += 8;
  }

  // Render any partial tile on the end
  cnt = cnt % 8;
  if (cnt)
    render_obj_part_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
      px_comb, dst_ptr, 0, cnt, tile_offset, palette, palptr);
}

// A slower version of the version above, that renders objects pixel by pixel.
// This allows proper mosaic effects whenever necessary.
template <typename stype, rendtype rdtype, bool is8bpp, bool hflip>
static void render_object_mosaic(
  s32 delta_x, u32 cnt, stype *dst_ptr, u32 base_tile_offset,
  u32 mosh, u32 px_comb, u16 palette, const u16* pal
) {
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  const s32 tile_size_off = hflip ? -tile_bsize : tile_bsize;

  u32 offx = 0;
  if (delta_x < 0) {      // Left part is outside of the screen/window.
    offx = -delta_x;  // Number of skipped pixels
  } else {
    dst_ptr += delta_x;
  }

  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  u8 pval = 0;
  u32 mctr = 0;
  for (u32 i = 0; i < cnt; i++, offx++, dst_ptr++) {
    // Counter-based mosaic (see render_affine_background for rationale).
    if (!mctr) {
      // Load tile pixel color.
      u32 tile_offset = base_tile_offset + (offx / 8) * tile_size_off;
      const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];

      // Lookup for each mode and flip value.
      if (is8bpp) {
        if (hflip)
          pval = tile_ptr[7 - offx % 8];
        else
          pval = tile_ptr[offx % 8];
      } else {
        if (hflip)
          pval = (tile_ptr[(7 - offx % 8) >> 1] >> (((offx & 1) ^ 1) * 4)) & 0xF;
        else
          pval = (tile_ptr[(offx % 8) >> 1] >> ((offx & 1) * 4)) & 0xF;
      }
      mctr = mosh;
    }
    mctr--;

    // Write the pixel value as required
    const u16 *subpal = &pal[palette];
    if (pval) {
      if (rdtype == FULLCOLOR)
        *dst_ptr = is8bpp ? pal[pval] : subpal[pval];
      else if (rdtype == INDXCOLOR)
        *dst_ptr = pval | px_attr;  // Add combine flags
      else if (rdtype == STCKCOLOR) {
        if (*dst_ptr & 0x100)
          *dst_ptr = pval | px_attr | ((*dst_ptr) & 0xFFFF0000);
        else
          *dst_ptr = pval | px_attr | ((*dst_ptr) << 16);
      }
      else if (rdtype == PIXCOPY)
        *dst_ptr = dst_ptr[240];
    }
  }
}


// Renders an affine sprite row to screen.
// They support 4bpp and 8bpp modes. 1D and 2D tile mapping modes.
// Their render area is limited to their size (and optionally double size)
template <typename stype, rendtype rdtype, bool mosaic, bool is8bpp, bool rotate>
static void render_affine_object(
  const t_sprite *obji, const t_affp *affp, bool is_double,
  u32 start, u32 end, stype *dst_ptr, u32 mosv, u32 mosh,
  u32 base_tile, u32 pxcomb, u16 palette, const u16 *palptr
) {
  // Tile size in bytes for each mode
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  const u32 tile_bwidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;

  // Affine params
  s32 dx = (s16)eswap16(affp->dx);
  s32 dy = (s16)eswap16(affp->dy);
  s32 dmx = (s16)eswap16(affp->dmx);
  s32 dmy = (s16)eswap16(affp->dmy);

  // Object dimensions and boundaries
  u32 obj_dimw = obji->obj_w;
  u32 obj_dimh = obji->obj_h;
  s32 middle_x = is_double ? obji->obj_w : (obji->obj_w / 2);
  s32 middle_y = is_double ? obji->obj_h : (obji->obj_h / 2);
  s32 obj_width  = is_double ? obji->obj_w * 2 : obji->obj_w;
  s32 obj_height = is_double ? obji->obj_h * 2 : obji->obj_h;

  s32 vcount = read_ioreg(REG_VCOUNT);
  if (mosaic)
    vcount -= vcount % mosv;
  s32 y_delta = vcount - (obji->obj_y + middle_y);

  if (obji->obj_x < (signed)start)
    middle_x -= (start - obji->obj_x);
  s32 source_x = (obj_dimw << 7) + (y_delta * dmx) - (middle_x * dx);
  s32 source_y = (obj_dimh << 7) + (y_delta * dmy) - (middle_x * dy);

  // Early optimization if Y-coord is out completely for this line.
  // (if there's no rotation Y coord remains identical throughout the line).
  if (!rotate && ((u32)(source_y >> 8)) >= (u32)obj_height)
    return;

  u32 d_start = MAX((signed)start, obji->obj_x);
  u32 d_end   = MIN((signed)end,   obji->obj_x + obj_width);
  if (d_end <= d_start)
    return;
  u32 cnt = d_end - d_start;
  dst_ptr += d_start;

  if (!rotate && (u32)(source_y >> 8) >= obj_dimh)
    return;

  bool obj1dmap = read_ioreg(REG_DISPCNT) & 0x40;
  const u32 tile_pitch = obj1dmap ? (obj_dimw / 8) * tile_bsize : 1024;
  u32 px_attr = pxcomb | palette | 0x100;  // Combine flags + high palette bit

#ifdef CGBA_SH4_ASM_4BPP
  if (rdtype == FULLCOLOR && !is8bpp && !mosaic && sizeof(stype) == sizeof(u16)) {
    if (!rotate) {
      u32 py = (u32)(source_y >> 8);
      u32 row_base = (base_tile + (py >> 3) * tile_pitch + ((py & 7u) << 2)) & 0x7FFFu;
      u32 max_byte = row_base + (((obj_dimw >> 3) - 1u) << 5) + 4u;
      if (__builtin_expect(max_byte <= 0x8000u, 1)) {
        cgba_render_obj_4bpp_affine_scale_sh4(
          (u16 *)(void *)dst_ptr, (cnt << 16) | obj_dimw, source_x, dx,
          &vram[0x10000u + row_base], palptr + palette);
        return;
      }
    } else {
      u32 bt = base_tile & 0x7FFFu;
      u32 max_byte = bt + ((obj_dimh >> 3) - 1u) * tile_pitch +
                     (((obj_dimw >> 3) - 1u) << 5) + 32u;
      if (__builtin_expect(max_byte <= 0x8000u, 1)) {
        u32 row_shift = 7u;
        if (obj1dmap) {
          /* dimw in {8, 16, 32, 64} -> row_shift in {2, 3, 4, 5}. */
          row_shift = (obj_dimw == 8u) ? 2u : (obj_dimw == 16u) ? 3u
                    : (obj_dimw == 32u) ? 4u : 5u;
        }
        cgba_aff_rot_params p = {
          (u16 *)(void *)dst_ptr,
          &vram[0x10000u + bt],
          palptr + palette,
          source_x, source_y, dx, dy, cnt, obj_dimw, obj_dimh, row_shift
        };
        cgba_render_obj_4bpp_affine_rot_sh4(&p);
        return;
      }
    }
  }
#endif

  // Skip pixels outside of the sprite area, until we reach the sprite "inside"
  while (cnt) {
    u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

    // Stop once we find a pixel that is actually *inside* the map.
    if (pixel_x < obj_dimw && pixel_y < obj_dimh)
      break;

    dst_ptr++;
    source_x += dx;
    if (rotate)
      source_y += dy;
    cnt--;
  }

  // Draw sprite pixels by looking them up first. Lookup address is tricky!
  u8 pixval = 0;
  u32 mctr = 0;
  for (u32 i = 0; i < cnt; i++) {
    u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

    // Check if we run out of the sprite, then we can safely abort.
    if (pixel_x >= obj_dimw || pixel_y >= obj_dimh)
      return;

    // For mosaic, we "remember" the last looked up pixel.  Counter form
    // avoids a per-pixel divl (see render_affine_background).
    if (!mosaic || !mctr) {
      // Lookup pixel and draw it.
      if (is8bpp) {
        // We lookup the byte directly and render it.
        const u32 tile_off =
          base_tile +                        // Character base
          ((pixel_y >> 3) * tile_pitch) +    // Skip vertical blocks
          ((pixel_x >> 3) * tile_bsize) +    // Skip horizontal blocks
          ((pixel_y & 0x7) * tile_bwidth) +  // Skip vertical rows to the pixel
          (pixel_x & 0x7);                   // Skip the horizontal offset

        pixval = vram[0x10000 + (tile_off & 0x7FFF)];   // Read pixel value!
      } else {
        const u32 tile_off =
          base_tile +                        // Character base
          ((pixel_y >> 3) * tile_pitch) +    // Skip vertical blocks
          ((pixel_x >> 3) * tile_bsize) +    // Skip horizontal blocks
          ((pixel_y & 0x7) * tile_bwidth) +  // Skip vertical rows to the pixel
          ((pixel_x >> 1) & 0x3);            // Skip the horizontal offset

        u8 pixpair = vram[0x10000 + (tile_off & 0x7FFF)]; // Read 2 pixels @4bpp
        pixval = ((pixel_x & 1) ? pixpair >> 4 : pixpair & 0xF);
      }
      mctr = mosh;
    }
    if (mosaic) mctr--;

    // Render the pixel value
    if (pixval) {
      if (rdtype == FULLCOLOR)
        *dst_ptr = palptr[pixval | palette];
      else if (rdtype == INDXCOLOR)
        *dst_ptr = pixval | px_attr;  // Add combine flags
      else if (rdtype == STCKCOLOR) {
        // Stack pixels on top of the pixel value and combine flags
        if (*dst_ptr & 0x100)
          *dst_ptr = pixval | px_attr | ((*dst_ptr) & 0xFFFF0000);
        else
          *dst_ptr = pixval | px_attr | ((*dst_ptr) << 16);  // Stack pixels
      }
      else if (rdtype == PIXCOPY)
        *dst_ptr = dst_ptr[240];
    }

    // Move to the next pixel, update coords accordingly
    dst_ptr++;
    source_x += dx;
    if (rotate)
      source_y += dy;
  }
}

// Renders a single sprite on the current scanline.
// This function calls the affine or regular renderer depending on the sprite.
// Will calculate whether sprite has certain effects (flip, rotation ...) to
// use an optimized renderer function.
template <typename stype, rendtype rdtype, bool is8bpp, bool mosaic>
inline static void render_sprite(
  const t_sprite *obji, bool is_affine, u32 start, u32 end, stype *scanline,
  u32 pxcomb, const u16* palptr, s32 vcount, bool obj1dmap
) {
  const u32 msk = is8bpp && !obj1dmap ? 0x3FE : 0x3FF;
  const u32 base_tile = (obji->attr2 & msk) * 32;

  const u32 mosv = (mosaic ? (read_ioreg(REG_MOSAIC) >> 12) & 0xF : 0) + 1;
  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC) >>  8) & 0xF : 0) + 1;

  // Render the object scanline using the correct mode.
  // (in 4bpp mode calculate the palette number)
  // Objects use the higher palette part
  u16 pal = (is8bpp ? 0 : ((obji->attr2 >> 8) & 0xF0));

  if (is_affine) {
    u32 pnum = (obji->attr1 >> 9) & 0x1f;
    const t_affp *affp_base = (t_affp*)oam_ram;
    const t_affp *affp = &affp_base[pnum];

    if (affp->dy == 0)     // No rotation happening (just scale)
      render_affine_object<stype, rdtype, mosaic, is8bpp, false>(
        obji, affp, obji->is_double, start, end, scanline, mosv, mosh,
        base_tile, pxcomb, pal, palptr);
    else                   // Full rotation and scaling
      render_affine_object<stype, rdtype, mosaic, is8bpp, true>(
        obji, affp, obji->is_double, start, end, scanline, mosv, mosh,
        base_tile, pxcomb, pal, palptr);
  } else {
    // Non-affine objects can be flipped on both edges.
    bool hflip = (obji->attr1 & 0x1000) != 0;
    bool vflip = (obji->attr1 & 0x2000) != 0;

    // Calulate the vertical offset (row) to be displayed. Account for vflip.
    u32 voffset = vflip ? (u32)(obji->obj_y + obji->obj_h - vcount - 1)
                        : (u32)(vcount - obji->obj_y);
    if (mosaic)
      voffset -= voffset % mosv;

    // Calculate base tile for the object (points to the row to be drawn).
    u32 tile_bsize  = is8bpp ? tile_size_8bpp : tile_size_4bpp;
    u32 tile_bwidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
    u32 row_bytes   = is8bpp ? ((u32)obji->obj_w << 3) : ((u32)obji->obj_w << 2);
    u32 obj_pitch   = obj1dmap ? row_bytes : 1024u;
    u32 hflip_off   = hflip ? (row_bytes - tile_bsize) : 0u;

    // Calculate the pointer to the tile.
    const u32 tile_offset =
      base_tile +                    // Char offset
      (voffset >> 3) * obj_pitch +   // Select tile row offset
      (voffset & 7u) * tile_bwidth + // Skip tile rows
      hflip_off;                     // Account for horizontal flip

    // Make everything relative to start
    s32 obj_x_offset  = obji->obj_x - (s32)start;
    u32 clipped_width = (u32)(obj_x_offset >= 0 ? obji->obj_w : obji->obj_w + obj_x_offset);
    u32 max_range = (u32)(obj_x_offset >= 0 ? (s32)end - obji->obj_x : (s32)(end - start));
    u32 max_draw = MIN(max_range, clipped_width);

    if (mosaic && mosh > 1) {
      if (hflip)
        render_object_mosaic<stype, rdtype, is8bpp, true>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, mosh, pxcomb, pal, palptr);
      else
        render_object_mosaic<stype, rdtype, is8bpp, false>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, mosh, pxcomb, pal, palptr);
    } else {
      if (hflip)
        render_object<stype, rdtype, is8bpp, true>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, pxcomb, pal, palptr);
      else
        render_object<stype, rdtype, is8bpp, false>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, pxcomb, pal, palptr);
    }
  }
}

// Renders objects on a scanline for a given priority.
template <typename stype, rendtype rdtype>
static __attribute__((noinline)) void render_scanline_obj_slow(
  const t_sprite *obji, u16 obj_attr0, u32 start, u32 end,
  stype *scanline, u32 obj_cflags, const u16 *palptr,
  s32 vcount, bool obj1dmap, bool mosaic_active
) {
  bool is_affine = (obj_attr0 & 0x100) != 0;
  s32 obj_maxw = (is_affine && obji->is_double) ? obji->obj_w * 2 : obji->obj_w;

  if (obji->obj_x >= (signed)end || obji->obj_x + obj_maxw <= (signed)start)
    return;

  if (rdtype == PIXCOPY) {
    u32 sec_start = MAX((signed)start, obji->obj_x);
    u32 sec_end   = MIN((signed)end, obji->obj_x + obj_maxw);
    u32 obj_enable = read_ioreg(REG_WINOUT) >> 8;

    u16 *tmp_ptr = (u16*)&scanline[GBA_SCREEN_PITCH];
    render_scanline_conditional(sec_start, sec_end, tmp_ptr, obj_enable);
  }

  u32 pxcomb = 0u;
  if (rdtype != FULLCOLOR) {
    bool is_trans = ((obj_attr0 >> 10) & 0x3) == OBJ_MOD_SEMITRAN;
    pxcomb = (is_trans ? 0x800u : 0u) | obj_cflags;
  }

  bool emosaic = (obj_attr0 & 0x1000) != 0;
  bool is_8bpp = (obj_attr0 & 0x2000) != 0;

  if (__builtin_expect(emosaic && mosaic_active, 0)) {
    if (is_8bpp)
      render_sprite<stype, rdtype, true, true>(
        obji, is_affine, start, end, scanline, pxcomb, palptr, vcount, obj1dmap);
    else
      render_sprite<stype, rdtype, false, true>(
        obji, is_affine, start, end, scanline, pxcomb, palptr, vcount, obj1dmap);
  } else {
    if (is_8bpp)
      render_sprite<stype, rdtype, true, false>(
        obji, is_affine, start, end, scanline, pxcomb, palptr, vcount, obj1dmap);
    else
      render_sprite<stype, rdtype, false, false>(
        obji, is_affine, start, end, scanline, pxcomb, palptr, vcount, obj1dmap);
  }
}

// This function assumes that order_obj has been called to prepare the objects.
template <typename stype, rendtype rdtype, bool cached = CGBA_VIDEO_SPRITE_DECODE_CACHE>
void render_scanline_objs(
  u32 priority, u32 start, u32 end, void *raw_ptr, const u16* palptr
) {
  stype *scanline = (stype*)raw_ptr;
  const s32 vcount = read_ioreg(REG_VCOUNT);
  u32 objcnt = obj_priority_count[priority][vcount];
  if (__builtin_expect(objcnt == 0u, 0))
    return;
  const bool obj1dmap = (read_ioreg(REG_DISPCNT) & 0x40) != 0;
  const bool mosaic_active = (read_ioreg(REG_MOSAIC) & 0xFF00) != 0;
  const u32 obj_cflags = (rdtype == FULLCOLOR) ? 0u : color_flags(4);
  const u8 *objlist = obj_priority_list[priority][vcount];

#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
  if (cached && rdtype == FULLCOLOR && sizeof(stype) == sizeof(u16)) {
    const u8 forbid_mask = mosaic_active ? 0x08u : 0x00u;
    for (s32 objn = (s32)objcnt - 1; objn >= 0; objn--) {
      const cgba_decoded_obj *d = &decoded_objs[objlist[objn]];
      s32 ox = d->sprite.obj_x;
      u8 ff = d->fast_flags;
      s32 ox_end = ox + ((!(ff & 1u) && (d->attr0 & 0x100) && d->sprite.is_double)
                          ? (d->sprite.obj_w << 1) : d->sprite.obj_w);
      if (ox_end <= (s32)start || ox >= (s32)end)
        continue;
      if (__builtin_expect((ff & 1u) != 0u && (ff & forbid_mask) == 0u &&
                           ox >= (s32)start && ox_end <= (s32)end, 1)) {
        u32 voffset = (ff & 4u) ? (u32)(d->sprite.obj_y + d->sprite.obj_h - vcount - 1)
                                : (u32)(vcount - d->sprite.obj_y);
        u32 pshift = obj1dmap ? (u32)d->fast_row_shift : 10u;
        u32 off = (d->fast_tile_base + ((voffset >> 3) << pshift) +
                   ((voffset & 7u) << 2)) & 0x7FFFu;
        u32 span = d->fast_span;
        if (ff & 2u) {
          if (__builtin_expect(off >= span, 1)) {
            cgba_render_obj_4bpp_tiles_hf_sh4(
              &vram[0x10000u + off], d->fast_num_tiles,
              (u16 *)scanline + ox, palptr + d->fast_pal_off);
            continue;
          }
        } else {
          if (__builtin_expect(off + span < 0x8000u, 1)) {
            cgba_render_obj_4bpp_tiles_sh4(
              &vram[0x10000u + off], d->fast_num_tiles,
              (u16 *)scanline + ox, palptr + d->fast_pal_off);
            continue;
          }
        }
      }
      render_scanline_obj_slow<stype, rdtype>(
        &d->sprite, d->attr0, start, end, scanline, obj_cflags, palptr,
        vcount, obj1dmap, mosaic_active);
    }
    return;
  }
#endif

  // Render all the visible objects for this priority (back to front)
  for (s32 objn = (s32)objcnt - 1; objn >= 0; objn--) {
    u32 objoff = objlist[objn];
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
    u16 obj_attr0;
    t_sprite direct;
    if (!cached) {
      const t_oam *oamentry = &((t_oam*)oam_ram)[objoff];
      obj_attr0 = eswap16(oamentry->attr0);
      direct = decode_sprite(oamentry);
    } else {
      obj_attr0 = decoded_objs[objoff].attr0;
    }
    const t_sprite &obji = cached ? decoded_objs[objoff].sprite : direct;
#else
    const t_oam *oamentry = &((t_oam*)oam_ram)[objoff];
    u16 obj_attr0 = eswap16(oamentry->attr0);
    const t_sprite obji = decode_sprite(oamentry);
#endif
    render_scanline_obj_slow<stype, rdtype>(
      &obji, obj_attr0, start, end, scanline, obj_cflags, palptr,
      vcount, obj1dmap, mosaic_active);
  }
}


// Goes through the object list in the OAM (from #127 to #0) and adds objects
// into a sorted list by priority for the current row.
// Invisible objects are discarded. ST-objects are flagged. Cycle counting is
// performed to discard excessive objects (to match HW capabilities).
static void order_obj(u32 video_mode)
{
  u32 obj_num;
  t_oam *oam_base = (t_oam*)oam_ram;
  u16 rend_cycles[160];

  bool hblank_free = read_ioreg(REG_DISPCNT) & 0x20;
  u16 max_rend_cycles = !sprite_limit ? REND_CYC_MAX :
                         hblank_free  ? REND_CYC_REDUCED :
                                        REND_CYC_SCANLINE;

  memset(obj_priority_count, 0, sizeof(obj_priority_count));
  memset(obj_alpha_count, 0, sizeof(obj_alpha_count));
  memset(obj_row_prio_mask, 0, sizeof(obj_row_prio_mask));
  memset(rend_cycles, 0, sizeof(rend_cycles));

  for(obj_num = 0; obj_num < 128; obj_num++)
  {
    t_oam *oam_ptr = &oam_base[obj_num];
    u16 obj_attr0 = eswap16(oam_ptr->attr0);

    // Bit 9 disables regular sprites (that is, non-affine ones).
    if ((obj_attr0 & 0x0300) == 0x0200)
      continue;

    u16 obj_shape = obj_attr0 >> 14;
    u32 obj_mode = (obj_attr0 >> 10) & 0x03;

    // Prohibited shape and mode
    if ((obj_shape == 0x3) || (obj_mode == OBJ_MOD_INVALID))
      continue;

    u16 obj_attr2 = eswap16(oam_ptr->attr2);

    // On bitmap modes, objs 0-511 are not usable, ingore them.
    if ((video_mode >= 3) && (!(obj_attr2 & 0x200)))
      continue;

    u16 obj_attr1 = eswap16(oam_ptr->attr1);
    // Calculate object size (from size and shape attr bits)
    u16 obj_size = (obj_attr1 >> 14);
    s32 base_width  = obj_dim_table[obj_shape][obj_size][0];
    s32 base_height = obj_dim_table[obj_shape][obj_size][1];
    s32 obj_height = base_height;
    s32 obj_width  = base_width;
    s32 obj_y = obj_attr0 & 0xFF;

    if(obj_y > 160)
      obj_y -= 256;

    // Double size for affine sprites with double bit set
    bool is_double = (obj_attr0 & 0x200) != 0;
    if(is_double)
    {
      obj_height *= 2;
      obj_width *= 2;
    }

    if(((obj_y + obj_height) > 0) && (obj_y < 160))
    {
      s32 obj_x = (s32)(obj_attr1 << 23) >> 23;

      if(((obj_x + obj_width) > 0) && (obj_x < 240))
      {
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
        cgba_decoded_obj *dobj = &decoded_objs[obj_num];
        t_sprite *ds = &dobj->sprite;
        ds->obj_x = obj_x;
        ds->obj_y = obj_y;
        ds->obj_w = base_width;
        ds->obj_h = base_height;
        ds->attr1 = obj_attr1;
        ds->attr2 = obj_attr2;
        ds->is_double = is_double;
        dobj->attr0 = obj_attr0;
        {
          u32 ntiles = (u32)base_width >> 3;
          u32 hflip = (obj_attr1 & 0x1000u) ? 2u : 0u;
          u32 vflip = (obj_attr1 & 0x2000u) ? 4u : 0u;
          u32 emos  = (obj_attr0 & 0x1000u) ? 8u : 0u;
          u32 f4    = ((obj_attr0 & 0x2100u) == 0u) ? 1u : 0u;
          dobj->fast_flags = (u8)(f4 | hflip | vflip | emos);
          dobj->fast_num_tiles = (u8)ntiles;
          dobj->fast_pal_off = (u8)((obj_attr2 >> 8) & 0xF0u);
          dobj->fast_span = (u16)((ntiles - 1u) << 5);
          dobj->fast_tile_base = (u16)(((obj_attr2 & 0x3FFu) << 5) +
                                       (hflip ? (((u32)base_width << 2) - 32u) : 0u));
          dobj->fast_row_shift = (u8)((base_width == 8) ? 5u :
                                      (base_width == 16) ? 6u :
                                      (base_width == 32) ? 7u : 8u);
        }
#endif
        u32 obj_priority = (obj_attr2 >> 10) & 0x03;
        bool is_affine = obj_attr0 & 0x100;
        // Clip Y coord and height to the 0..159 interval
        u32 starty = MAX(obj_y, 0);
        u32 endy   = MIN(obj_y + obj_height, 160);

        // Calculate needed cycles to render the sprite
        u16 cyccnt = is_affine ? (10 + obj_width * 2) : obj_width;

        switch (obj_mode) {
        case OBJ_MOD_SEMITRAN:
          {
            u8 pbit = (u8)(1u << obj_priority);
            u8 *cnt_p = &obj_priority_count[obj_priority][starty];
            u8 *list_p = obj_priority_list[obj_priority][starty];
            u8 *mask_p = &obj_row_prio_mask[starty];
            u8 *alpha_p = &obj_alpha_count[starty];
            u16 *rc_p = &rend_cycles[starty];
            u32 n = endy - starty;
            do {
              if (*rc_p < max_rend_cycles) {
                u32 cur_cnt = *cnt_p;
                list_p[cur_cnt] = (u8)obj_num;
                *cnt_p = (u8)(cur_cnt + 1u);
                *mask_p |= pbit;
                *rc_p += cyccnt;
                *alpha_p = 1;
              }
              cnt_p++; list_p += 128; mask_p++; alpha_p++; rc_p++;
            } while (--n);
          }
          break;
        case OBJ_MOD_WINDOW:
          obj_priority = 4;
          /* fallthrough */
        case OBJ_MOD_NORMAL:
          // Add the object to the list.
          {
            u8 pbit = (u8)(1u << obj_priority);
            u8 *cnt_p = &obj_priority_count[obj_priority][starty];
            u8 *list_p = obj_priority_list[obj_priority][starty];
            u8 *mask_p = &obj_row_prio_mask[starty];
            u16 *rc_p = &rend_cycles[starty];
            u32 n = endy - starty;
            do {
              if (*rc_p < max_rend_cycles) {
                u32 cur_cnt = *cnt_p;
                list_p[cur_cnt] = (u8)obj_num;
                *cnt_p = (u8)(cur_cnt + 1u);
                *mask_p |= pbit;
                *rc_p += cyccnt;
              }
              cnt_p++; list_p += 128; mask_p++; rc_p++;
            } while (--n);
          }
          break;
        };
      }
    }
  }
}

u32 layer_order[16];
u32 layer_count;

// Sorts active BG/OBJ layers and generates an ordered list of layers.
// Things are drawn back to front, so lowest priority goes first.
static void order_layers(u32 layer_flags, u32 vcnt)
{
  static u32 cached_bg_key = 0xFFFFFFFFu;
  static u8 bg_only_order[4];
  static u8 bg_only_count;
  static u8 bg_prio_list[4][4];
  static u8 bg_prio_cnt[4];

  const u32 p0 = (u32)(read_ioreg(REG_BGxCNT(0)) & 0x03);
  const u32 p1 = (u32)(read_ioreg(REG_BGxCNT(1)) & 0x03);
  const u32 p2 = (u32)(read_ioreg(REG_BGxCNT(2)) & 0x03);
  const u32 p3 = (u32)(read_ioreg(REG_BGxCNT(3)) & 0x03);
  const u32 bg_key = (layer_flags & 0x0Fu) |
                     (p0 << 4) | (p1 << 6) | (p2 << 8) | (p3 << 10);

  if (__builtin_expect(bg_key != cached_bg_key, 0)) {
    const u32 layer_prio[4] = { p0, p1, p2, p3 };
    bg_only_count = 0;
    for (s32 priority = 3; priority >= 0; priority--) {
      u8 cnt = 0;
      for (s32 lnum = 3; lnum >= 0; lnum--) {
        if (((layer_flags >> lnum) & 1) && (layer_prio[lnum] == (u32)priority)) {
          bg_prio_list[priority][cnt++] = (u8)lnum;
          bg_only_order[bg_only_count++] = (u8)lnum;
        }
      }
      bg_prio_cnt[priority] = cnt;
    }
    cached_bg_key = bg_key;
  }

  const u32 omask = (layer_flags & 0x10) ? (u32)(obj_row_prio_mask[vcnt] & 0x0Fu) : 0u;
  if (!omask) {
    u32 cnt = bg_only_count;
    layer_count = cnt;
    for (u32 i = 0; i < cnt; i++)
      layer_order[i] = bg_only_order[i];
    return;
  }

  u32 out = 0;
  for (s32 priority = 3; priority >= 0; priority--) {
    u32 bcnt = bg_prio_cnt[priority];
    for (u32 i = 0; i < bcnt; i++)
      layer_order[out++] = bg_prio_list[priority][i];
    if (omask & (1u << priority))
      layer_order[out++] = (u32)priority | 0x04u;
  }
  layer_count = out;
}


// Blending is performed by separating an RGB value into 0G0R0B (32 bit)
// Since blending factors are at most 16, mult/add operations do not overflow
// to the neighbouring color and can be performed much faster than separatedly

// Here follow the mask value to separate/expand the color to 32 bit,
// the mask to detect overflows in the blend operation and

#define BLND_MSK (SATR_MSK | SATG_MSK | SATB_MSK)

#ifdef USE_XBGR1555_FORMAT
  #define OVFG_MSK 0x04000000
  #define OVFR_MSK 0x00008000
  #define OVFB_MSK 0x00000020
  #define SATG_MSK 0x03E00000
  #define SATR_MSK 0x00007C00
  #define SATB_MSK 0x0000001F
#else
  #define OVFG_MSK 0x08000000
  #define OVFR_MSK 0x00010000
  #define OVFB_MSK 0x00000020
  #define SATG_MSK 0x07E00000
  #define SATR_MSK 0x0000F800
  #define SATB_MSK 0x0000001F
#endif

typedef enum
{
  OBJ_BLEND,    // No effects, just blend forced-blend pixels (ie. ST objects)
  BLEND_ONLY,   // Just alpha blending (if the pixels are 1st and 2nd target)
  BLEND_BRIGHT, // Perform alpha blending if appropiate, and brighten otherwise
  BLEND_DARK,   // Same but with darken effecg
} blendtype;

#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
/* Cache the full, unshifted expanded-color product.  The two products must be
 * added before the >> 4 so rounding and the overflow bits used by saturation
 * remain identical to the original expression. */
typedef struct {
  u32 term[512];
  u32 factor;
  u32 pending_factor;
  bool valid;
  bool pending;
} blend_palette_term_cache;

static blend_palette_term_cache blend_palette_cache_a
  __attribute__((aligned(32)));
static blend_palette_term_cache blend_palette_cache_b
  __attribute__((aligned(32)));

static inline void blend_palette_cache_observe_writes(void)
{
  if (palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) {
    /* A changed palette gets one direct-render grace call.  If it remains
     * stable, the following call builds the tables.  Raster effects which
     * write the palette every scanline therefore stay on the direct path. */
    blend_palette_cache_a.valid = false;
    blend_palette_cache_a.pending = false;
    blend_palette_cache_b.valid = false;
    blend_palette_cache_b.pending = false;
    palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_BLEND;
  }
}

static bool blend_palette_cache_get(blend_palette_term_cache *cache,
                                    u32 factor, const u32 **term)
{
  if (cache->valid && cache->factor == factor) {
    *term = cache->term;
    return true;
  }

  /* Likewise, require a new factor to be observed twice before paying for a
   * 512-entry rebuild.  This prevents scanline-varying BLDALPHA from turning
   * the optimization into a regression. */
  if (!cache->pending || cache->pending_factor != factor) {
    cache->pending = true;
    cache->pending_factor = factor;
    return false;
  }

  for (u32 i = 0; i < 512; i++) {
    u32 pixel = palette_ram_converted[i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    cache->term[i] = expanded * factor;
  }
  cache->factor = factor;
  cache->valid = true;
  cache->pending = false;
  *term = cache->term;
  return true;
}

#ifndef CGBA_VIDEO_TILE_ROW_CACHE
static const u32 *cgba_bg_blend_term_a_get(u32 eva)
{
  blend_palette_cache_observe_writes();
  if (__builtin_expect(blend_palette_cache_a.valid &&
                       blend_palette_cache_a.factor == eva, 1))
    return blend_palette_cache_a.term;
  for (u32 i = 0; i < 512u; i++) {
    u32 pixel = palette_ram_converted[i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    blend_palette_cache_a.term[i] = expanded * eva;
  }
  blend_palette_cache_a.factor = eva;
  blend_palette_cache_a.valid = true;
  blend_palette_cache_a.pending = false;
  return blend_palette_cache_a.term;
}
#endif
#else
#if CGBA_PALETTE_BLEND_BIT
static u32 cgba_direct_term_a[256] __attribute__((aligned(32)));
static u32 cgba_direct_term_a_eva = 0xFFFFFFFFu;
static const u32 *cgba_bg_blend_term_a_get(u32 eva)
{
  if (__builtin_expect((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) != 0u, 0)) {
    cgba_direct_term_a_eva = 0xFFFFFFFFu;
    palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_BLEND;
  }
  if (__builtin_expect(cgba_direct_term_a_eva == eva, 1))
    return cgba_direct_term_a;
  for (u32 i = 0; i < 256u; i++) {
    u32 pixel = palette_ram_converted[i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    cgba_direct_term_a[i] = expanded * eva;
  }
  cgba_direct_term_a_eva = eva;
  return cgba_direct_term_a;
}
#else
static const u32 *cgba_bg_blend_term_a_get(u32 eva)
{
  static u32 direct_term_a[256];
  for (u32 i = 0; i < 256u; i++) {
    u32 pixel = palette_ram_converted[i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    direct_term_a[i] = expanded * eva;
  }
  return direct_term_a;
}
#endif
#endif

// Applies blending (and optional brighten/darken) effect to a bunch of
// color-indexed pixel pairs. Depending on the mode and the pixel target
// number, blending, darken/brighten or no effect will be applied.
// Bits 0-8 encode the color index (paletted colors)
// Bit 9 is set if the pixel belongs to a 1st target layer
// Bit 10 is set if the pixel belongs to a 2nd target layer
// Bit 11 is set if the pixel belongs to a ST-object
template <blendtype bldtype, bool st_objs>
static void merge_blend(u32 start, u32 end, u16 *dst, u32 *src) {
  u32 bldalpha = read_ioreg(REG_BLDALPHA);
  u32 brightf = MIN(16, read_ioreg(REG_BLDY) & 0x1F);
  u32 blend_a = MIN(16, (bldalpha >> 0) & 0x1F);
  u32 blend_b = MIN(16, (bldalpha >> 8) & 0x1F);

  bool can_saturate = blend_a + blend_b > 16;

#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
  const u32 *blend_term_a = NULL;
  const u32 *blend_term_b = NULL;
  blend_palette_cache_observe_writes();
  bool cached_a =
    blend_palette_cache_get(&blend_palette_cache_a, blend_a, &blend_term_a);
  bool cached_b =
    blend_palette_cache_get(&blend_palette_cache_b, blend_b, &blend_term_b);
  bool cached_terms = cached_a && cached_b;
#endif

  if (can_saturate) {
    // If blending can result in saturation, we need to clamp output values.
    while (start < end) {
      u32 pixpair = src[start];
      // If ST-OBJ, force blending mode (has priority over other effects).
      // If regular blending mode, blend if 1st/2nd bits are set respectively.
      // Otherwise, apply other color effects if 1st bit is set.
      bool force_blend = (pixpair & 0x04000800) == 0x04000800;
      bool do_blend    = (pixpair & 0x04000200) == 0x04000200;
      if ((st_objs && force_blend) || (do_blend && bldtype == BLEND_ONLY)) {
        // Top pixel is 1st target, pixel below is 2nd target. Blend!
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        u32 pfe;
        if (cached_terms) {
          pfe = (blend_term_a[(pixpair >> 0) & 0x1FF] +
                 blend_term_b[(pixpair >> 16) & 0x1FF]) >> 4;
        } else {
#endif
        u16 p1 = palette_ram_converted[(pixpair >>  0) & 0x1FF];
        u16 p2 = palette_ram_converted[(pixpair >> 16) & 0x1FF];
        u32 p1e = (p1 | (p1 << 16)) & BLND_MSK;
        u32 p2e = (p2 | (p2 << 16)) & BLND_MSK;
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4);
#else
        u32 pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4);
#endif
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        }
#endif

        // If the overflow bit is set, saturate (set) all bits to one.
        if (pfe & (OVFR_MSK | OVFG_MSK | OVFB_MSK)) {
          if (pfe & OVFG_MSK)
            pfe |= SATG_MSK;
          if (pfe & OVFR_MSK)
            pfe |= SATR_MSK;
          if (pfe & OVFB_MSK)
            pfe |= SATB_MSK;
        }
        pfe &= BLND_MSK;
        dst[start++] = (pfe >> 16) | pfe;
      }
      else if ((bldtype == BLEND_DARK || bldtype == BLEND_BRIGHT) &&
               (pixpair & 0x200) == 0x200) {
        // Top pixel is 1st-target, can still apply bright/dark effect.
        u16 pidx = palette_ram_converted[pixpair & 0x1FF];
        u32 epixel = (pidx | (pidx << 16)) & BLND_MSK;
        u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightf) >> 4) & BLND_MSK;
        u32 pb = ((epixel * (16 - brightf)) >> 4) & BLND_MSK;
        epixel = (pa + pb) & BLND_MSK;
        dst[start++] = (epixel >> 16) | epixel;
      }
      else {
        dst[start++] = palette_ram_converted[pixpair & 0x1FF];   // No effects
      }
    }
  } else {
    while (start < end) {
      u32 pixpair = src[start];
      bool do_blend    = (pixpair & 0x04000200) == 0x04000200;
      bool force_blend = (pixpair & 0x04000800) == 0x04000800;
      if ((st_objs && force_blend) || (do_blend && bldtype == BLEND_ONLY)) {
        // Top pixel is 1st target, pixel below is 2nd target. Blend!
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        u32 pfe;
        if (cached_terms) {
          pfe = (blend_term_a[(pixpair >> 0) & 0x1FF] +
                 blend_term_b[(pixpair >> 16) & 0x1FF]) >> 4;
        } else {
#endif
        u16 p1 = palette_ram_converted[(pixpair >>  0) & 0x1FF];
        u16 p2 = palette_ram_converted[(pixpair >> 16) & 0x1FF];
        u32 p1e = (p1 | (p1 << 16)) & BLND_MSK;
        u32 p2e = (p2 | (p2 << 16)) & BLND_MSK;
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4) & BLND_MSK;
#else
        u32 pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4) & BLND_MSK;
#endif
#ifdef CGBA_VIDEO_BLEND_PALETTE_CACHE
        }
        pfe &= BLND_MSK;
#endif
        dst[start++] = (pfe >> 16) | pfe;
      }
      else if ((bldtype == BLEND_DARK || bldtype == BLEND_BRIGHT) &&
               (pixpair & 0x200) == 0x200) {
        // Top pixel is 1st-target, can still apply bright/dark effect.
        u16 pidx = palette_ram_converted[pixpair & 0x1FF];
        u32 epixel = (pidx | (pidx << 16)) & BLND_MSK;
        u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightf) >> 4) & BLND_MSK;
        u32 pb = ((epixel * (16 - brightf)) >> 4) & BLND_MSK;
        epixel = (pa + pb) & BLND_MSK;
        dst[start++] = (epixel >> 16) | epixel;
      }
      else {
        dst[start++] = palette_ram_converted[pixpair & 0x1FF];   // No effects
      }
    }
  }
}

// Applies brighten/darken effect to a bunch of color-indexed pixels.
template <blendtype bldtype>
static void merge_brightness(u32 start, u32 end, u16 *srcdst) {
  u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);

  while (start < end) {
    u16 spix = srcdst[start];
    u16 pixcol = palette_ram_converted[spix & 0x1FF];

    if ((spix & 0x200) == 0x200) {
      // Pixel is 1st target, can apply color effect.
      u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
      u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightness) >> 4) & BLND_MSK; // B/W
      u32 pb = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
      epixel = (pa + pb) & BLND_MSK;
      pixcol = (epixel >> 16) | epixel;
    }

    srcdst[start++] = pixcol;
  }
}

static inline void cgba_fill_u16_fast(u16 *dst, u32 count, u16 val)
{
  if (count == 0u)
    return;
  if (((uintptr_t)dst & 2u) != 0u) {
    *dst++ = val;
    count--;
  }
  u32 pair = ((u32)val << 16) | (u32)val;
  u32 *d32 = (u32 *)dst;
  u32 pairs = count >> 1;
  while (pairs >= 4u) {
    d32[0] = pair;
    d32[1] = pair;
    d32[2] = pair;
    d32[3] = pair;
    d32 += 4;
    pairs -= 4u;
  }
  while (pairs--)
    *d32++ = pair;
  if (count & 1u)
    *((u16 *)d32) = val;
}

// Fills a segment using the backdrop color (in the right mode).
template<rendtype rdmode, typename dsttype>
void fill_line_background(u32 start, u32 end, dsttype *scanline) {
  if (start >= end)
    return;
  dsttype fillval = (rdmode == FULLCOLOR)
                      ? (dsttype)palette_ram_converted[0]
                      : (dsttype)color_flags(5);
  if (sizeof(dsttype) == sizeof(u16)) {
    cgba_fill_u16_fast((u16 *)(void *)(scanline + start), end - start, (u16)fillval);
    return;
  }
  while (start < end)
    scanline[start++] = fillval;
}

// Renders the backdrop color (ie. whenever no layer is active) applying
// any effects that might still apply (usually darken/brighten).
static void render_backdrop(u32 start, u32 end, u16 *scanline) {
  if (start >= end)
    return;
  u16 bldcnt = read_ioreg(REG_BLDCNT);
  u16 pixcol = palette_ram_converted[0];
  u32 effect = (bldcnt >> 6) & 0x03;
  u32 bd_1st_target = ((bldcnt >> 0x5) & 0x01);

  if (bd_1st_target && effect == COL_EFFECT_BRIGHT) {
    u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);

    // Unpack 16 bit pixel for fast blending operation
    u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
    u32 pa = ((BLND_MSK * brightness)      >> 4) & BLND_MSK;  // White color
    u32 pb = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
    epixel = (pa + pb) & BLND_MSK;
    pixcol = (epixel >> 16) | epixel;
  }
  else if (bd_1st_target && effect == COL_EFFECT_DARK) {
    u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);
    u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
    epixel = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
    pixcol = (epixel >> 16) | epixel;
  }

  // Fill the line with that color
  cgba_fill_u16_fast(scanline + start, end - start, pixcol);
}

// Renders all the available and enabled layers (in tiled mode).
// Walks the list of layers in visibility order and renders them in the
// specified mode (taking into consideration the first layer, etc).
template<rendtype bgmode, rendtype objmode, typename dsttype>
void tile_render_layers(u32 start, u32 end, dsttype *dst_ptr, u32 enabled_layers) {
  u32 lnum;
  u32 base_done = 0;
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u16 video_mode = dispcnt & 0x07;
  bool obj_enabled = (enabled_layers & 0x10);   // Objects are visible

  bool objlayer_is_1st_tgt =
    (bgmode == FULLCOLOR && objmode == FULLCOLOR) ? false
      : (((read_ioreg(REG_BLDCNT) >> 4) & 1) != 0);
  bool has_trans_obj =
    (bgmode == FULLCOLOR && objmode == FULLCOLOR) ? false
      : (obj_alpha_count[read_ioreg(REG_VCOUNT)] != 0);

  for (lnum = 0; lnum < layer_count; lnum++) {
    u32 layer = layer_order[lnum];
    bool is_obj = layer & 0x4;
    if (is_obj && obj_enabled) {
      bool can_skip_blend = !has_trans_obj && !objlayer_is_1st_tgt;

      // If it's the first layer, make sure to fill with backdrop color.
      if (!base_done)
        fill_line_background<bgmode, dsttype>(start, end, dst_ptr);

      // Optimization: skip blending mode if no blending can happen to this layer
      if (objmode == STCKCOLOR && can_skip_blend)
        render_scanline_objs<dsttype, INDXCOLOR>(
          layer & 0x3, start, end, dst_ptr, &palette_ram_converted[0x100]);
      else
        render_scanline_objs<dsttype, objmode>(
          layer & 0x3, start, end, dst_ptr, &palette_ram_converted[0x100]);

      base_done = 1;
    }
    else if (!is_obj && ((1 << layer) & enabled_layers)) {
      if (bgmode == FULLCOLOR && video_mode == 0) {
        if (base_done)
          render_scanline_text<dsttype, bgmode, false>(
            layer, start, end, dst_ptr, palette_ram_converted);
        else
          render_scanline_text<dsttype, bgmode, true>(
            layer, start, end, dst_ptr, palette_ram_converted);
        base_done = 1;
        continue;
      }
      bool layer_is_1st_tgt = ((read_ioreg(REG_BLDCNT) >> layer) & 1) != 0;
      bool can_skip_blend = !has_trans_obj && !layer_is_1st_tgt;

      bool is_affine = (video_mode >= 1) && (layer >= 2);
      u32 fnidx = (base_done) | (is_affine ? 2 : 0);

      // Can optimize rendering if no blending can really happen.
      // If stack mode, no blending and not base layer, we might speed up a bit
      if (bgmode == STCKCOLOR && can_skip_blend) {
        static const tile_render_function rdfns[4] = {
          render_scanline_text<dsttype, INDXCOLOR, true>,
          render_scanline_text<dsttype, INDXCOLOR, false>,
          render_scanline_affine<dsttype, INDXCOLOR, true>,
          render_scanline_affine<dsttype, INDXCOLOR, false>,
        };
        rdfns[fnidx](layer, start, end, dst_ptr, palette_ram_converted);
      } else {
        static const tile_render_function rdfns[4] = {
          render_scanline_text<dsttype, bgmode, true>,
          render_scanline_text<dsttype, bgmode, false>,
          render_scanline_affine<dsttype, bgmode, true>,
          render_scanline_affine<dsttype, bgmode, false>,
        };
        rdfns[fnidx](layer, start, end, dst_ptr, palette_ram_converted);
      }

      base_done = 1;
    }
  }

  // Render background if we did not render any active layer.
  if (!base_done)
    fill_line_background<bgmode, dsttype>(start, end, dst_ptr);
}


// Renders all layers honoring color effects (blending, brighten/darken).
// It uses different rendering routines depending on the coloring effect
// requirements, speeding up common cases where no effects are used.

// No effects use NORMAL mode (RBB565 color is written on the buffer).
// For blending, we use BLEND mode to record the two top-most pixels.
// For other effects we use COLOR16, which records an indexed color in the
// buffer (used for darken/brighten effects at later passes) or COLOR32,
// which similarly uses an indexed color for rendering but recording one
// color for the background and another one for the object layer.

static const u8 active_layers[] = {
  0x1F,   // Mode 0, Tile BG0-3 and OBJ
  0x17,   // Mode 1, Tile BG0-2 and OBJ
  0x1C,   // Mode 2, Tile BG2-3 and OBJ
  0x14,   // Mode 3, BMP  BG2 and OBJ
  0x14,   // Mode 4, BMP  BG2 and OBJ
  0x14,   // Mode 5, BMP  BG2 and OBJ
  0,      // Unused
  0,
};

static void render_w_effects(
  u32 start, u32 end, u16* scanline, u32 enable_flags,
  const layer_render_struct *renderers
) {
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 vcount = read_ioreg(REG_VCOUNT);
  bool effects_enabled = enable_flags & 0x20;   // Window bit for effects.
  u32 vis_layers = ((dispcnt >> 8) & active_layers[dispcnt & 0x07]) &
                   (enable_flags & 0x1Fu);
  if (vcount < 160u && (obj_row_prio_mask[vcount] & 0x0Fu) == 0u)
    vis_layers &= ~0x10u;
  u16 bldcnt = read_ioreg(REG_BLDCNT);
  u32 tgt2_active = (bldcnt >> 8) & (vis_layers | 0x20u);
  bool obj_blend = (vis_layers & 0x10u) != 0u &&
                   obj_alpha_count[vcount] > 0 &&
                   tgt2_active != 0u;

  // If the window bits disable effects, default to NONE
  u32 effect_type = effects_enabled ? ((bldcnt >> 6) & 0x03)
                                    : COL_EFFECT_NONE;

  switch (effect_type) {
  case COL_EFFECT_BRIGHT:
    {
      // If no visible layers (or backdrop) are 1st target, no effect will happen.
      bool some_1st_tgt = (bldcnt & (vis_layers | 0x20u)) != 0u;
      // If the factor is zero, it's the same as "regular" rendering.
      bool non_zero_blend = (read_ioreg(REG_BLDY) & 0x1F) != 0;
      if (some_1st_tgt && non_zero_blend) {
        if (obj_blend) {
          u32 tmp_buf[240];
          renderers->indexed_u32(start, end, tmp_buf, enable_flags);
          merge_blend<BLEND_BRIGHT, true>(start, end, scanline, tmp_buf);
        } else {
          renderers->indexed_u16(start, end, scanline, enable_flags);
          merge_brightness<BLEND_BRIGHT>(start, end, scanline);
        }
        return;
      }
    }
    break;

  case COL_EFFECT_DARK:
    {
      // If no visible layers (or backdrop) are 1st target, no effect will happen.
      bool some_1st_tgt = (bldcnt & (vis_layers | 0x20u)) != 0u;
      // If the factor is zero, it's the same as "regular" rendering.
      bool non_zero_blend = (read_ioreg(REG_BLDY) & 0x1F) != 0;
      if (some_1st_tgt && non_zero_blend) {
        if (obj_blend) {
          u32 tmp_buf[240];
          renderers->indexed_u32(start, end, tmp_buf, enable_flags);
          merge_blend<BLEND_DARK, true>(start, end, scanline, tmp_buf);
        } else {
          renderers->indexed_u16(start, end, scanline, enable_flags);
          merge_brightness<BLEND_DARK>(start, end, scanline);
        }
        return;
      }
    }
    break;

  case COL_EFFECT_BLEND:
    {
      // Backdrop has no layer beneath it so it can never trigger 1st->2nd blend.
      u32 tgt1_active = bldcnt & vis_layers;
      bool self_only_bg = (tgt1_active == tgt2_active) &&
                          ((tgt1_active & (tgt1_active - 1u)) == 0u) &&
                          !(tgt1_active & 0x10u);
      // If 1st target is 100% opacity and 2nd is 0%, just render regularly.
      bool non_trns_tgt = (read_ioreg(REG_BLDALPHA) & 0x1F1F) != 0x001F;
      if (tgt1_active && tgt2_active && !self_only_bg && non_trns_tgt) {
        u32 tmp_buf[240];
        renderers->stacked(start, end, tmp_buf, enable_flags);
        if (obj_blend)
          merge_blend<BLEND_ONLY, true>(start, end, scanline, tmp_buf);
        else
          merge_blend<BLEND_ONLY, false>(start, end, scanline, tmp_buf);
        return;
      }
    }
    break;

  case COL_EFFECT_NONE:
    // Default case, see below.
    break;
  };

  // Default rendering mode, without layer effects (except perhaps sprites).
  if (obj_blend) {
    u32 tmp_buf[240];
    renderers->stacked(start, end, tmp_buf, enable_flags);
    merge_blend<OBJ_BLEND, true>(start, end, scanline, tmp_buf);
  } else {
    renderers->fullcolor(start, end, scanline, enable_flags);
  }
}

#define bitmap_layer_render_functions(rdmode, dsttype, mode, ttype, w, h) {   \
 {                                                                            \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, BLIT, false>,   \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, SCALED, false>, \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, ROTATED, false>,\
 },                                                                           \
 {                                                                            \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, BLIT, true>,    \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, SCALED, true>,  \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, ROTATED, true>, \
 }                                                                            \
}

static const bitmap_layer_render_struct idx32_bmrend[3][2] =
{
  bitmap_layer_render_functions(INDXCOLOR, u32, 3, u16, 240, 160),
  bitmap_layer_render_functions(INDXCOLOR, u32, 4, u8,  240, 160),
  bitmap_layer_render_functions(INDXCOLOR, u32, 5, u16, 160, 128)
};

// Render the BG and OBJ in a bitmap scanline from start to end ONLY if
// enable_flag allows that layer/OBJ.

template<rendtype bgmode, rendtype objmode, typename dsttype>
static void bitmap_render_layers(
  u32 start, u32 end, dsttype *scanline, u32 enable_flags)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  bool has_trans_obj = obj_alpha_count[read_ioreg(REG_VCOUNT)];
  bool objlayer_is_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x10) != 0;
  bool bg2_is_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x4) != 0;

  // Fill in the renderers for a layer based on the mode type,
  static const bitmap_layer_render_struct renderers[3][2] =
  {
    bitmap_layer_render_functions(bgmode, dsttype, 3, u16, 240, 160),
    bitmap_layer_render_functions(bgmode, dsttype, 4, u8,  240, 160),
    bitmap_layer_render_functions(bgmode, dsttype, 5, u16, 160, 128)
  };

  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;
  u32 bg_control = read_ioreg(REG_BG2CNT);
  u32 mmode = ((bg_control & 0x40) && (mosamount != 0)) ? 1 : 0;

  unsigned modeidx = (dispcnt & 0x07) - 3;
  const bitmap_layer_render_struct *mode_rend = &renderers[modeidx][mmode];
  const bitmap_layer_render_struct *idxm_rend = &idx32_bmrend[modeidx][mmode];

  u32 current_layer;
  u32 layer_order_pos;

  fill_line_background<bgmode, dsttype>(start, end, scanline);

  for(layer_order_pos = 0; layer_order_pos < layer_count; layer_order_pos++)
  {
    current_layer = layer_order[layer_order_pos];
    if(current_layer & 0x04)
    {
      if (enable_flags & 0x10) {
        bool can_skip_blend = !has_trans_obj && !objlayer_is_1st_tgt;

        // Optimization: skip blending mode if no blending can happen to this layer
        if (objmode == STCKCOLOR && can_skip_blend)
          render_scanline_objs<dsttype, INDXCOLOR>(
            current_layer & 3, start, end, scanline, &palette_ram_converted[0x100]);
        else
          render_scanline_objs<dsttype, objmode>(
            current_layer & 3, start, end, scanline, &palette_ram_converted[0x100]);
      }
    }
    else
    {
      if(enable_flags & 0x04) {
        s32 dx = (s16)read_ioreg(REG_BG2PA);
        s32 dy = (s16)read_ioreg(REG_BG2PC);

        // Optimization: Skip stack mode if there's no blending happening.
        bool can_skip_blend = !has_trans_obj && !bg2_is_1st_tgt;
        const bitmap_layer_render_struct *rd =
          (bgmode == STCKCOLOR && can_skip_blend) ? idxm_rend : mode_rend;

        if (dy)
          rd->affine_render(start, end, scanline, palette_ram_converted);
        else if (dx == 256)
          rd->blit_render(start, end, scanline, palette_ram_converted);
        else
          rd->scale_render(start, end, scanline, palette_ram_converted);
      }
    }
  }
}


static const layer_render_struct tile_mode_renderers = {
  .fullcolor   = tile_render_layers<FULLCOLOR, FULLCOLOR, u16>,
  .indexed_u16 = tile_render_layers<INDXCOLOR, INDXCOLOR, u16>,
  .indexed_u32 = tile_render_layers<INDXCOLOR, STCKCOLOR, u32>,
  .stacked     = tile_render_layers<STCKCOLOR, STCKCOLOR, u32>,
};

static const layer_render_struct bitmap_mode_renderers = {
  .fullcolor   = bitmap_render_layers<FULLCOLOR, FULLCOLOR, u16>,
  .indexed_u16 = bitmap_render_layers<INDXCOLOR, INDXCOLOR, u16>,
  .indexed_u32 = bitmap_render_layers<INDXCOLOR, STCKCOLOR, u32>,
  .stacked     = bitmap_render_layers<STCKCOLOR, STCKCOLOR, u32>,
};

// Renders a full scanline, given an enable_flags mask (for which layers and
// effects are enabled).
static void render_scanline_conditional(
  u32 start, u32 end, u16 *scanline, u32 enable_flags)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 video_mode = dispcnt & 0x07;

  // Check if any layer is actually active.
  if (layer_count && (enable_flags & 0x1F)) {
    // Color effects currently only supported in indexed-color modes (tiled and mode 4)
    if(video_mode < 3)
      render_w_effects(start, end, scanline, enable_flags, &tile_mode_renderers);
    else if (video_mode == 4)
      render_w_effects(start, end, scanline, enable_flags, &bitmap_mode_renderers);
    else
      // TODO: Implement mode 3 & 5 color effects (at least partially, ie. ST objs)
      bitmap_mode_renderers.fullcolor(start, end, scanline, enable_flags);
  }
  else
    // Render the backdrop color, since no layers are enabled/visible.
    render_backdrop(start, end, scanline);
}

// Renders the are outside of all active windows
static void render_windowout_pass(u16 *scanline, u32 start, u32 end)
{
  u32 winout = read_ioreg(REG_WINOUT);
  u32 wndout_enable = winout & 0x3F;

  render_scanline_conditional(start, end, scanline, wndout_enable);
}

// Renders window-obj. This is a pixel-level windowing effect, based on sprites
// (objects) with a special rendering mode (the sprites are not themselves
// visible but rather "enable" other pixels to be rendered conditionally).
static void render_windowobj_pass(u16 *scanline, u32 start, u32 end)
{
  u32 winout = read_ioreg(REG_WINOUT);
  u32 wndout_enable = winout & 0x3F;

  // First we render the "window-out" segment.
  render_scanline_conditional(start, end, scanline, wndout_enable);

  // Now we render the objects in "copy" mode. This renders the scanline in
  // WinObj-mode to a temporary buffer and performs a "copy-mode" render.
  // In this mode, we copy pixels from the temp buffer to the final buffer
  // whenever an object pixel is rendered.
  render_scanline_objs<u16, PIXCOPY>(4, start, end, scanline, NULL);

  // TODO: Evaluate whether it's better to render the whole line and copy,
  // or render subsegments and copy as we go (depends on the pixel/obj count)
}

// If the window Y coordinates are out of the window range we can skip
// rendering the inside of the window.
inline bool in_window_y(u32 vcount, u32 top, u32 bottom) {
  // TODO: check if these are reversed when top-bottom are also reversed.
  if (top > 227)     // This causes the window to be invisible
    return false;
  if (bottom > 227)  // This makes it all visible
    return true;

  if (top > bottom)  /* Reversed: if not in the "band" */
    return vcount > top || vcount <= bottom;

  return vcount >= top && vcount < bottom;
}

// Renders window 0/1. Checks boundaries and divides the segment into
// subsegments (if necessary) rendering each one in their right mode.
// outfn is called for "out-of-window" rendering.
template<window_render_function outfn, unsigned winnum>
static void render_window_n_pass(u16 *scanline, u32 start, u32 end)
{
  u32 vcount = read_ioreg(REG_VCOUNT);
  // Check the Y coordinates to check if they fall in the right row
  u32 win_top = read_ioreg(REG_WINxV(winnum)) >> 8;
  u32 win_bot = read_ioreg(REG_WINxV(winnum)) & 0xFF;
  // Check the X coordinates and generate up to three segments
  // Clip the coordinates to the [start, end) range.
  u32 win_lraw = read_ioreg(REG_WINxH(winnum)) >> 8;
  u32 win_rraw = read_ioreg(REG_WINxH(winnum)) & 0xFF;
  u32 win_l = MAX(start, MIN(end, win_lraw));
  u32 win_r = MAX(start, MIN(end, win_rraw));
  bool goodwin = win_lraw < win_rraw;

  if (!in_window_y(vcount, win_top, win_bot) || (win_lraw == win_rraw))
    // WindowN is completely out, just render all out.
    outfn(scanline, start, end);
  else {
    // Render window withtin the clipped range
    // Enable bits for stuff inside the window (and outside)
    u32 winin = read_ioreg(REG_WININ);
    u32 wndn_enable = (winin >> (8 * winnum)) & 0x3F;

    // If the window is defined upside down, the areas are inverted.
    if (goodwin) {
      // Render [start, win_l) range (which is outside the window)
      if (win_l != start)
        outfn(scanline, start, win_l);
      // Render the actual window0 pixels
      render_scanline_conditional(win_l, win_r, scanline, wndn_enable);
      // Render the [win_l, end] range (outside)
      if (win_r != end)
        outfn(scanline, win_r, end);
    } else {
      // Render [0, win_r) range (which is "inside" window0)
      if (win_r != start)
        render_scanline_conditional(start, win_r, scanline, wndn_enable);
      // The actual window is now outside, render recursively
      outfn(scanline, win_r, win_l);
      // Render the [win_l, 240] range ("inside")
      if (win_l != end)
        render_scanline_conditional(win_l, end, scanline, wndn_enable);
    }
  }
}

// Renders a full scaleline, taking into consideration windowing effects.
// Breaks the rendering step into N steps, for each windowed region.
static void render_scanline_window(u16 *scanline)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 win_ctrl = (dispcnt >> 13);

  // Priority decoding for windows
  switch (win_ctrl) {
  case 0x0: // No windows are active.
    render_scanline_conditional(0, 240, scanline);
    break;

  case 0x1: // Window 0
    render_window_n_pass<render_windowout_pass, 0>(scanline, 0, 240);
    break;

  case 0x2: // Window 1
    render_window_n_pass<render_windowout_pass, 1>(scanline, 0, 240);
    break;

  case 0x3: // Window 0 & 1
    render_window_n_pass<render_window_n_pass<render_windowout_pass, 1>, 0>(scanline, 0, 240);
    break;

  case 0x4: // Window Obj
    render_windowobj_pass(scanline, 0, 240);
    break;

  case 0x5: // Window 0 & Obj
    render_window_n_pass<render_windowobj_pass, 0>(scanline, 0, 240);
    break;

  case 0x6: // Window 1 & Obj
    render_window_n_pass<render_windowobj_pass, 1>(scanline, 0, 240);
    break;

  case 0x7: // Window 0, 1 & Obj
    render_window_n_pass<render_window_n_pass<render_windowobj_pass, 1>, 0>(scanline, 0, 240);
    break;
  }
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
/* Ultra-lightweight frame+row dirty scanline tracker (~2.4 KiB in .bss).
 *
 * Keeps ONE frame-level IO snapshot (checked once at vcount == 0) plus a
 * 160-byte per-row validity mask and a 160-byte per-row sprite mask.
 * On scrolling/walking frames where vcount == 0 detects an IO or BG change,
 * rows 1..159 execute zero memcmp/memcpy and zero D-cache churn; on static,
 * dialog, or sprite-only frames, unchanged rows skip in ~8 instructions.
 * Any mid-frame video IO write (HBlank HDMA) sets cgba_video_io_dirty and
 * cleanly disables row reuse until the next uniform frame. */
struct cgba_frame_io_snap {
  u16 dispcnt;
  u16 bg_io[12];       /* raw io_registers[0x04..0x0F]: BG0..3 CNT/HOFS/VOFS */
  u16 win_mos_io[7];   /* raw io_registers[0x20..0x26]: WIN0H..MOSAIC */
  u16 bld_io[3];       /* raw io_registers[0x28..0x2A]: BLDCNT/ALPHA/BLDY */
  u16 aff_mat[8];      /* raw BG2PA..PD (0x10..0x13), BG3PA..PD (0x18..0x1B) */
  s32 aff_ref[4];      /* affine_reference_x/y [0..1] at vcount == 0 */
};

u32 cgba_video_frame_dirty_rows = 160;
static u8  cgba_sl_row_valid[160];
static u8  cgba_sl_row_has_obj[160];
static cgba_frame_io_snap cgba_sl_frame_io;
static u16 cgba_sl_pal_snap[512];
static u16 cgba_sl_oam_snap[512];
static u32 cgba_sl_oam_key = 0xFFFFFFFFu;
static u16 *cgba_sl_last_pixels = NULL;
static u32 cgba_sl_last_pitch = 0;
static bool cgba_sl_io_snap_valid = false;
static bool cgba_sl_pal_snap_valid = false;
static bool cgba_sl_oam_snap_valid = false;
static bool cgba_sl_prev_uniform = false;
static bool cgba_sl_cur_uniform = false;
static bool cgba_sl_frame_bg_dirty = true;
static bool cgba_sl_frame_obj_dirty = true;
static bool cgba_bg_plan_valid = false;

static inline void cgba_bg_vram_invalidate(void);
static inline void cgba_bg_plan_invalidate_all(void);

extern "C" void video_invalidate_scanline_cache(u32 start_y, u32 end_y)
{
  if (start_y >= 160)
    return;
  if (end_y > 160)
    end_y = 160;
  memset(cgba_sl_row_valid + start_y, 0, end_y - start_y);
  if (start_y == 0 && end_y == 160) {
    cgba_sl_io_snap_valid = false;
    cgba_sl_pal_snap_valid = false;
    cgba_sl_oam_snap_valid = false;
    cgba_sl_oam_key = 0xFFFFFFFFu;
    cgba_sl_prev_uniform = false;
    cgba_sl_cur_uniform = false;
    cgba_sl_frame_bg_dirty = true;
    cgba_sl_frame_obj_dirty = true;
    cgba_bg_plan_valid = false;
    cgba_bg_vram_invalidate();
    cgba_bg_plan_invalidate_all();
  }
  cgba_video_frame_dirty_rows = 160;
}

static inline bool cgba_sl_oam_yrange(u16 raw_attr0, u16 raw_attr1,
                                      u32 *out_start, u32 *out_end)
{
  u16 attr0 = eswap16(raw_attr0);
  if ((attr0 & 0x0300) == 0x0200)
    return false;
  u16 shape = attr0 >> 14;
  u32 mode = (attr0 >> 10) & 0x03;
  if (shape == 0x3 || mode == OBJ_MOD_INVALID)
    return false;
  u16 attr1 = eswap16(raw_attr1);
  u16 size = attr1 >> 14;
  s32 h = obj_dim_table[shape][size][1];
  s32 y = attr0 & 0xFF;
  if (y > 160)
    y -= 256;
  if (attr0 & 0x200)
    h *= 2;
  if ((y + h) <= 0 || y >= 160)
    return false;
  *out_start = (u32)(y < 0 ? 0 : y);
  *out_end = (u32)((y + h) > 160 ? 160 : (y + h));
  return true;
}

static void cgba_sl_sync_oam(u32 video_mode, u16 dispcnt, bool all_rows_dirty)
{
  u32 key = ((video_mode >= 3) ? 4u : 0u) |
            ((dispcnt & 0x20) ? 2u : 0u) |
            (sprite_limit ? 1u : 0u);
  reg[OAM_UPDATED] = 0;

  if (cgba_sl_oam_snap_valid && key == cgba_sl_oam_key &&
      memcmp(cgba_sl_oam_snap, oam_ram, sizeof(cgba_sl_oam_snap)) == 0) {
    /* Byte-identical OAM update (e.g. every-VBlank OAM DMA): skip order_obj()
     * and keep all cached scanlines valid. */
    return;
  }

  if (!all_rows_dirty) {
    if (!cgba_sl_oam_snap_valid || key != cgba_sl_oam_key) {
      memset(cgba_sl_row_valid, 0, sizeof(cgba_sl_row_valid));
    } else {
      u32 dirty_aff = 0;
      for (u32 i = 0; i < 128; i++) {
        if (cgba_sl_oam_snap[i * 4 + 3] != oam_ram[i * 4 + 3])
          dirty_aff |= 1u << (i >> 2);
      }
      for (u32 i = 0; i < 128; i++) {
        u16 o0 = cgba_sl_oam_snap[i * 4 + 0];
        u16 o1 = cgba_sl_oam_snap[i * 4 + 1];
        u16 o2 = cgba_sl_oam_snap[i * 4 + 2];
        u16 n0 = oam_ram[i * 4 + 0];
        u16 n1 = oam_ram[i * 4 + 1];
        u16 n2 = oam_ram[i * 4 + 2];
        bool changed = (o0 != n0) || (o1 != n1) || (o2 != n2);
        if (!changed && dirty_aff) {
          u16 a0 = eswap16(n0);
          if (a0 & 0x100) {
            u32 aff_idx = (eswap16(n1) >> 9) & 0x1Fu;
            if (dirty_aff & (1u << aff_idx))
              changed = true;
          }
        }
        if (changed) {
          u32 sy, ey;
          if (cgba_sl_oam_yrange(o0, o1, &sy, &ey))
            memset(cgba_sl_row_valid + sy, 0, ey - sy);
          if (cgba_sl_oam_yrange(n0, n1, &sy, &ey))
            memset(cgba_sl_row_valid + sy, 0, ey - sy);
        }
      }
    }
  }

  order_obj(video_mode);
  memcpy(cgba_sl_oam_snap, oam_ram, sizeof(cgba_sl_oam_snap));
  cgba_sl_oam_key = key;
  cgba_sl_oam_snap_valid = true;
}

static inline void cgba_sl_sync_palette(bool *bg_changed, bool *obj_changed)
{
  if (!(palette_ram_dirty & CGBA_PALETTE_DIRTY_SCANLINE) &&
      cgba_sl_pal_snap_valid)
    return;
  palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_SCANLINE;
  if (!cgba_sl_pal_snap_valid) {
    memcpy(cgba_sl_pal_snap, palette_ram_converted, sizeof(cgba_sl_pal_snap));
    cgba_sl_pal_snap_valid = true;
    *bg_changed = true;
    *obj_changed = true;
    return;
  }
  if (memcmp(cgba_sl_pal_snap, palette_ram_converted, 256 * sizeof(u16)) != 0) {
    memcpy(cgba_sl_pal_snap, palette_ram_converted, 256 * sizeof(u16));
    *bg_changed = true;
  }
  if (memcmp(cgba_sl_pal_snap + 256, palette_ram_converted + 256,
             256 * sizeof(u16)) != 0) {
    memcpy(cgba_sl_pal_snap + 256, palette_ram_converted + 256,
           256 * sizeof(u16));
    *obj_changed = true;
  }
}

static inline bool cgba_sl_check_and_capture_frame_io(u32 video_mode, u16 dispcnt)
{
  u16 raw_dispcnt = io_registers[REG_DISPCNT];
  bool same = cgba_sl_io_snap_valid && (cgba_sl_frame_io.dispcnt == raw_dispcnt);
  if (same && !(dispcnt & 0x80)) {
    if (memcmp(cgba_sl_frame_io.bg_io, &io_registers[REG_BG0CNT],
               sizeof(cgba_sl_frame_io.bg_io)) != 0 ||
        memcmp(cgba_sl_frame_io.win_mos_io, &io_registers[REG_WIN0H],
               sizeof(cgba_sl_frame_io.win_mos_io)) != 0 ||
        memcmp(cgba_sl_frame_io.bld_io, &io_registers[REG_BLDCNT],
               sizeof(cgba_sl_frame_io.bld_io)) != 0) {
      same = false;
    } else if (video_mode != 0) {
      if (memcmp(cgba_sl_frame_io.aff_mat, &io_registers[REG_BG2PA],
                 4 * sizeof(u16)) != 0 ||
          memcmp(cgba_sl_frame_io.aff_mat + 4, &io_registers[REG_BG3PA],
                 4 * sizeof(u16)) != 0 ||
          cgba_sl_frame_io.aff_ref[0] != affine_reference_x[0] ||
          cgba_sl_frame_io.aff_ref[1] != affine_reference_y[0] ||
          cgba_sl_frame_io.aff_ref[2] != affine_reference_x[1] ||
          cgba_sl_frame_io.aff_ref[3] != affine_reference_y[1]) {
        same = false;
      }
    }
  }
  if (!same) {
    cgba_sl_frame_io.dispcnt = raw_dispcnt;
    if (!(dispcnt & 0x80)) {
      memcpy(cgba_sl_frame_io.bg_io, &io_registers[REG_BG0CNT],
             sizeof(cgba_sl_frame_io.bg_io));
      memcpy(cgba_sl_frame_io.win_mos_io, &io_registers[REG_WIN0H],
             sizeof(cgba_sl_frame_io.win_mos_io));
      memcpy(cgba_sl_frame_io.bld_io, &io_registers[REG_BLDCNT],
             sizeof(cgba_sl_frame_io.bld_io));
      if (video_mode != 0) {
        memcpy(cgba_sl_frame_io.aff_mat, &io_registers[REG_BG2PA],
               4 * sizeof(u16));
        memcpy(cgba_sl_frame_io.aff_mat + 4, &io_registers[REG_BG3PA],
               4 * sizeof(u16));
        cgba_sl_frame_io.aff_ref[0] = affine_reference_x[0];
        cgba_sl_frame_io.aff_ref[1] = affine_reference_y[0];
        cgba_sl_frame_io.aff_ref[2] = affine_reference_x[1];
        cgba_sl_frame_io.aff_ref[3] = affine_reference_y[1];
      }
    }
    cgba_sl_io_snap_valid = true;
  }
  return same;
}
#endif /* CGBA_VIDEO_DIRTY_SCANLINE */

#ifndef CGBA_VIDEO_TILE_ROW_CACHE
/* Per-layer Mode-0 full-scanline (0..240) plan cache.  Recomputed once per
 * uniform frame (or after any mid-frame video IO write) instead of 160x/frame
 * per BG layer. */
struct alignas(32) cgba_mode0_bg_plan {
  const u8 *tile_charbase;
  u16 *sb_ptr[2];
  u32 counts_packed;      /* todraw1 | (todraw2 << 16) */
  u32 row_nonzero[2];     /* combined sb|sb+1 nonzero-row bitmask per vhalf */
  u16 vofs;
  u16 first_byte_off;     /* first_off * 2 (bytes from map_row) */
  u16 map2_byte_off;      /* map2_off * 2 (bytes from map_row) */
  u8  flags;              /* bit0 = is_fast_4bpp, bit1 = has_zero_row */
  u8  tile_hoff;          /* 0 => aligned (todraw1>=1, no partial); 1..7 => partial */
};
#ifdef CGBA_SH4_ASM_4BPP
static_assert(offsetof(cgba_mode0_bg_plan, tile_charbase) == 0, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, sb_ptr) == 4, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, counts_packed) == 12, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, row_nonzero) == 16, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, vofs) == 24, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, first_byte_off) == 26, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, map2_byte_off) == 28, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, flags) == 30, "plan layout");
static_assert(offsetof(cgba_mode0_bg_plan, tile_hoff) == 31, "plan layout");
#endif

struct alignas(32) cgba_mode0_state {
  cgba_mode0_bg_plan plans[4];
  u8   lorder[16][8];
  u8   lcount[16];
  u32  lorder_key;
  u32  used_sb_mask;
  u32  bg_static_key[4];
  u32  bg_sb_mask[4];
  u16  bg_hofs[4];
  u8   bg_wide[4];
  u8   fast_fullframe;
  u8   fast_no_obj;
  u8   obj_alpha_can_blend;
  u8   obj_enabled;
  u8   blend_bg_layer;       /* 0..3 when direct RGB565 alpha-blend is active, else 0xFF */
  u8   blend_forbid_omask;   /* OBJ priority bits that disqualify direct blend */
  u8   blend_eva;            /* clamped 0..16 */
  u8   blend_evb;            /* clamped 0..16 */
};
static cgba_mode0_state cgba_m0;
#define cgba_bg_plans cgba_m0.plans
#define cgba_mode0_lorder cgba_m0.lorder
#define cgba_mode0_lcount cgba_m0.lcount
#define cgba_mode0_fast_fullframe cgba_m0.fast_fullframe
#define cgba_mode0_fast_no_obj cgba_m0.fast_no_obj
#define cgba_mode0_obj_alpha_can_blend cgba_m0.obj_alpha_can_blend
#define cgba_mode0_obj_enabled cgba_m0.obj_enabled

static inline bool cgba_mode0_can_fast(u32 omask)
{
  if (cgba_mode0_fast_fullframe)
    return true;
  if (cgba_mode0_fast_no_obj && omask == 0u)
    return true;
  if (cgba_m0.blend_bg_layer < 4u &&
      (omask & cgba_m0.blend_forbid_omask) == 0u)
    return true;
  return false;
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static inline u32 cgba_sb_get_row_nonzero(u32 sb)
{
  if (sb >= 32u)
    return 0xFFFFFFFFu;
  return (cgba_sb_valid_mask & (1u << sb))
           ? cgba_sb_row_nonzero[sb]
           : cgba_sb_scan_rows(sb);
}
#endif

static inline bool cgba_window_covers_all_y(u32 top, u32 bot)
{
  if (top > 227u)
    return false;
  if (bot > 227u)
    return true;
  if (top > bot)
    return false;
  return top == 0u && bot >= 160u;
}

static inline bool cgba_window_is_empty(u16 wh, u16 wv)
{
  return ((wh >> 8) == (wh & 0xFFu)) || ((wv >> 8) > 227u);
}

static inline bool cgba_bldcnt_is_noop(u16 bldcnt, u32 vis_layers)
{
  u32 eff = (bldcnt >> 6) & 3u;
  if (eff == COL_EFFECT_NONE)
    return true;
  if (eff == COL_EFFECT_BLEND) {
    u32 tgt1 = bldcnt & vis_layers;
    u32 tgt2 = (bldcnt >> 8) & (vis_layers | 0x20u);
    bool self_only_bg = (tgt1 == tgt2) &&
                        ((tgt1 & (tgt1 - 1u)) == 0u) &&
                        !(tgt1 & 0x10u);
    u32 bldalpha = read_ioreg(REG_BLDALPHA);
    u32 eva = MIN(16u, (u32)(bldalpha & 0x1Fu));
    u32 evb = MIN(16u, (u32)((bldalpha >> 8) & 0x1Fu));
    return (tgt1 == 0u) || (tgt2 == 0u) || self_only_bg ||
           (eva == 16u && evb == 0u);
  }
  return ((bldcnt & (vis_layers | 0x20u)) == 0u) ||
         ((read_ioreg(REG_BLDY) & 0x1Fu) == 0u);
}

static __attribute__((noinline)) void cgba_rebuild_mode0_bg_plans(u16 dispcnt)
{
  u32 win_ctrl = dispcnt >> 13;
  u32 enables = 0;
  bool full_win = false;

  if (win_ctrl == 0u) {
    full_win = true;
    enables = 0x3Fu;
  } else if (win_ctrl & 1u) {
    /* WIN0 has highest priority over WIN1, OBJWIN, and WINOUT. */
    u16 w0h = read_ioreg(REG_WIN0H);
    u16 w0v = read_ioreg(REG_WIN0V);
    if ((w0h >> 8) == 0u && (w0h & 0xFFu) >= 240u &&
        cgba_window_covers_all_y(w0v >> 8, w0v & 0xFFu)) {
      full_win = true;
      enables = read_ioreg(REG_WININ) & 0x3Fu;
    } else if (cgba_window_is_empty(w0h, w0v)) {
      if (win_ctrl == 1u) {
        full_win = true;
        enables = read_ioreg(REG_WINOUT) & 0x3Fu;
      } else if (win_ctrl == 3u) {
        u16 w1h = read_ioreg(REG_WIN1H);
        u16 w1v = read_ioreg(REG_WIN1V);
        if ((w1h >> 8) == 0u && (w1h & 0xFFu) >= 240u &&
            cgba_window_covers_all_y(w1v >> 8, w1v & 0xFFu)) {
          full_win = true;
          enables = (read_ioreg(REG_WININ) >> 8) & 0x3Fu;
        } else if (cgba_window_is_empty(w1h, w1v)) {
          full_win = true;
          enables = read_ioreg(REG_WINOUT) & 0x3Fu;
        }
      }
    }
  } else if (win_ctrl == 2u) {
    u16 w1h = read_ioreg(REG_WIN1H);
    u16 w1v = read_ioreg(REG_WIN1V);
    if ((w1h >> 8) == 0u && (w1h & 0xFFu) >= 240u &&
        cgba_window_covers_all_y(w1v >> 8, w1v & 0xFFu)) {
      full_win = true;
      enables = (read_ioreg(REG_WININ) >> 8) & 0x3Fu;
    } else if (cgba_window_is_empty(w1h, w1v)) {
      full_win = true;
      enables = read_ioreg(REG_WINOUT) & 0x3Fu;
    }
  }

  u32 vis_layers = 0u;
  bool no_effect = false;
  bool no_effect_no_obj = false;
  bool obj_alpha_can_blend = false;
  u8 blend_bg = 0xFFu;
  u8 blend_forbid = 0x0Fu;
  u8 blend_eva = 0u;
  u8 blend_evb = 0u;
  u32 bg_prio[4] = { 0, 0, 0, 0 };
  bool have_bg_prio = false;

  if (full_win && (enables & 0x1Fu) != 0u) {
    vis_layers = ((dispcnt >> 8) & 0x1Fu) & (enables & 0x1Fu);
    u16 bldcnt = read_ioreg(REG_BLDCNT);
    obj_alpha_can_blend = (vis_layers & 0x10u) != 0u &&
                          (((bldcnt >> 8) & (vis_layers | 0x20u)) != 0u);
    if (!(enables & 0x20u)) {
      no_effect = true;
      no_effect_no_obj = true;
    } else {
      no_effect = cgba_bldcnt_is_noop(bldcnt, vis_layers);
      no_effect_no_obj = no_effect ||
                         cgba_bldcnt_is_noop(bldcnt, vis_layers & ~0x10u);
      /* Direct-to-RGB565 single-1st-target BG alpha-blend fast path:
       * When exactly one visible BG layer is a 1st target under COL_EFFECT_BLEND,
       * and every layer strictly below it (including Backdrop) is a 2nd target,
       * all lower layers can render directly in 16-bit FULLCOLOR and the 1st-target
       * BG can alpha-blend directly onto the RGB565 destination buffer (with no
       * 32-bit STCKCOLOR buffer and no merge_blend pass). */
      if (!no_effect_no_obj && (((bldcnt >> 6) & 3u) == COL_EFFECT_BLEND)) {
        u32 tgt1_all = bldcnt & vis_layers;
        u32 tgt1_bg = tgt1_all & 0x0Fu;
        u32 tgt2_raw = (bldcnt >> 8) & 0x3Fu;
        if (tgt1_bg != 0u && (tgt1_bg & (tgt1_bg - 1u)) == 0u &&
            (tgt2_raw & 0x20u) != 0u) {
          u32 cand = (u32)__builtin_ctz(tgt1_bg);
          for (u32 layer = 0; layer < 4u; layer++)
            bg_prio[layer] = (u32)(read_ioreg(REG_BGxCNT(layer)) & 0x03u);
          have_bg_prio = true;
          u32 cprio = bg_prio[cand];
          bool all_lower_tgt2 = true;
          for (u32 b = 0; b < 4u; b++) {
            if (b == cand || !((vis_layers >> b) & 1u))
              continue;
            if (bg_prio[b] > cprio || (bg_prio[b] == cprio && b > cand)) {
              if (!((tgt2_raw >> b) & 1u)) {
                all_lower_tgt2 = false;
                break;
              }
            }
          }
          if (all_lower_tgt2) {
            u32 bldalpha = read_ioreg(REG_BLDALPHA);
            blend_bg = (u8)cand;
            if (tgt1_all & 0x10u)
              blend_forbid = 0x0Fu;
            else if (!(tgt2_raw & 0x10u))
              blend_forbid = (u8)((0x0Fu << (cprio + 1u)) & 0x0Fu);
            else
              blend_forbid = 0x00u;
            blend_eva = (u8)MIN(16u, (u32)(bldalpha & 0x1Fu));
            blend_evb = (u8)MIN(16u, (u32)((bldalpha >> 8) & 0x1Fu));
          }
        }
      }
    }
  }

  cgba_mode0_fast_fullframe = no_effect;
  cgba_mode0_fast_no_obj = no_effect_no_obj;
  cgba_mode0_obj_alpha_can_blend = obj_alpha_can_blend;
  cgba_mode0_obj_enabled = (vis_layers & 0x10u) != 0u;
  cgba_m0.blend_bg_layer = blend_bg;
  cgba_m0.blend_forbid_omask = blend_forbid;
  cgba_m0.blend_eva = blend_eva;
  cgba_m0.blend_evb = blend_evb;
  if (!no_effect_no_obj && blend_bg >= 4u)
    return;

  /* Precompute layer draw order for all 16 possible OBJ-priority row masks so
   * Mode-0 fast-path scanlines skip order_layers() and BGxCNT IO reads.
   * Cached by (vis_layers, bg_prio[0..3]) so steady frames skip the 16x16 loop. */
  {
    u32 lkey = 0x80000000u | vis_layers;
    for (u32 layer = 0; layer < 4u; layer++) {
      u32 pr = have_bg_prio ? bg_prio[layer]
                            : (u32)(read_ioreg(REG_BGxCNT(layer)) & 0x03u);
      bg_prio[layer] = pr;
      lkey |= pr << (8u + (layer << 1));
    }
    if (__builtin_expect(lkey != cgba_m0.lorder_key, 0)) {
      cgba_m0.lorder_key = lkey;
      for (u32 omask = 0; omask < 16u; omask++) {
        u32 out = 0u;
        for (s32 prio = 3; prio >= 0; prio--) {
          for (s32 lnum = 3; lnum >= 0; lnum--) {
            if (((vis_layers >> lnum) & 1u) && (bg_prio[lnum] == (u32)prio))
              cgba_mode0_lorder[omask][out++] = (u8)lnum;
          }
          if (omask & (1u << prio))
            cgba_mode0_lorder[omask][out++] = (u8)((u32)prio | 0x04u);
        }
        cgba_mode0_lcount[omask] = (u8)out;
      }
    }
  }

  const u32 mos_bg = read_ioreg(REG_MOSAIC) & 0xFFu;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  const u32 always_base_layer =
    (cgba_mode0_lcount[15] > 0u) ? (u32)cgba_mode0_lorder[15][0] : 0xFFu;
#endif
  u32 used_sbs = 0u;
  for (u32 layer = 0; layer < 4u; layer++) {
    if (!((vis_layers >> layer) & 1u))
      continue;
    cgba_mode0_bg_plan *p = &cgba_bg_plans[layer];
    u32 bg_control = read_ioreg(REG_BGxCNT(layer));
    u32 h_raw = read_ioreg(REG_BGxHOFS(layer)) & 511u;
    u16 vofs = read_ioreg(REG_BGxVOFS(layer));
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    bool base_no_blend = (layer == always_base_layer && layer != blend_bg);
#else
    bool base_no_blend = false;
#endif
    u32 skey = 0x80000000u | bg_control |
               ((mos_bg != 0u) ? 0x10000u : 0u) |
               (base_no_blend ? 0x20000u : 0u);
    if (__builtin_expect(skey == cgba_m0.bg_static_key[layer], 1)) {
      used_sbs |= cgba_m0.bg_sb_mask[layer];
      if (!(p->flags & 1u))
        continue;
      p->vofs = vofs;
      if (__builtin_expect(h_raw == cgba_m0.bg_hofs[layer], 1))
        continue;
      cgba_m0.bg_hofs[layer] = (u16)h_raw;
      u32 wide = cgba_m0.bg_wide[layer];
      u32 h = h_raw;
      u16 map_right_off = 0, second_off = 0;
      if (wide) {
        if (h >= 256u) {
          h -= 256u;
          map_right_off = 1024u;
        } else {
          second_off = 1024u;
        }
      } else {
        h &= 255u;
      }
      u32 pixel_run = 256u - h;
      u32 tile_hoff = h & 7u;
      u32 partial_left = tile_hoff ? (8u - tile_hoff) : 0u;
      u32 rem = 240u - partial_left;
      pixel_run -= partial_left;
      u32 todraw1 = MIN(rem, pixel_run) >> 3;
      rem -= todraw1 << 3;
      pixel_run -= todraw1 << 3;
      bool need_switch = (rem > 0u) && (pixel_run == 0u);
      u32 todraw2 = rem >> 3;
      u16 first_off = (u16)(map_right_off + (h >> 3));
      u16 map2_off = need_switch ? second_off
                                 : (u16)(first_off + (tile_hoff ? 1u : 0u) + todraw1);
      p->counts_packed = (u32)todraw1 | ((u32)todraw2 << 16);
      p->first_byte_off = (u16)(first_off << 1);
      p->map2_byte_off = (u16)(map2_off << 1);
      p->tile_hoff = (u8)tile_hoff;
      continue;
    }

    bool is8bpp = (bg_control & 0x80u) != 0;
    bool has_mosaic = ((bg_control & 0x40u) != 0) && (mos_bg != 0);
    if (is8bpp || has_mosaic) {
      p->flags = 0u;
      cgba_m0.bg_static_key[layer] = skey;
      cgba_m0.bg_sb_mask[layer] = 0u;
      continue;
    }
    u32 map_size = (bg_control >> 14) & 3u;
    u32 base_block = (bg_control >> 8) & 0x1Fu;
    u32 sb_bot = base_block + ((map_size & 2u) ? ((map_size & 1u) ? 2u : 1u) : 0u);
    u32 tilecntrl = (bg_control >> 2) & 3u;
    u32 h = h_raw;
    cgba_m0.bg_hofs[layer] = (u16)h;
    cgba_m0.bg_wide[layer] = (u8)(map_size & 1u);
    u16 map_right_off = 0, second_off = 0;
    if (map_size & 1u) {
      if (h >= 256u) {
        h -= 256u;
        map_right_off = 1024u;
      } else {
        second_off = 1024u;
      }
    } else {
      h &= 255u;
    }
    u32 pixel_run = 256u - h;
    u32 tile_hoff = h & 7u;
    u32 partial_left = tile_hoff ? (8u - tile_hoff) : 0u;
    u32 rem = 240u - partial_left;
    pixel_run -= partial_left;
    u32 todraw1 = MIN(rem, pixel_run) >> 3;
    rem -= todraw1 << 3;
    pixel_run -= todraw1 << 3;
    bool need_switch = (rem > 0u) && (pixel_run == 0u);
    u32 todraw2 = rem >> 3;

    u16 first_off = (u16)(map_right_off + (h >> 3));
    u16 map2_off = need_switch ? second_off
                               : (u16)(first_off + (tile_hoff ? 1u : 0u) + todraw1);
    p->tile_charbase = &vram[tilecntrl * 16384u];
    p->sb_ptr[0] = (u16 *)&vram[base_block * 2048u];
    p->sb_ptr[1] = (u16 *)&vram[sb_bot * 2048u];
    p->counts_packed = (u32)todraw1 | ((u32)todraw2 << 16);
    p->vofs = vofs;
    p->first_byte_off = (u16)(first_off << 1);
    p->map2_byte_off = (u16)(map2_off << 1);
    p->tile_hoff = (u8)tile_hoff;
    u32 layer_sbs = 0u;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    if (base_no_blend) {
      /* Base layer always renders with base_done == 0; never skips zero rows. */
      p->row_nonzero[0] = 0xFFFFFFFFu;
      p->row_nonzero[1] = 0xFFFFFFFFu;
      p->flags = 1u;
    } else {
      u32 nz0 = 0xFFFFFFFFu, nz1 = 0xFFFFFFFFu;
      if (sb_bot == base_block) {
        nz0 = cgba_sb_get_row_nonzero(base_block);
        layer_sbs |= (1u << base_block);
        if (map_size & 1u) {
          nz0 |= cgba_sb_get_row_nonzero(base_block + 1u);
          if (base_block + 1u < 32u)
            layer_sbs |= (1u << (base_block + 1u));
        }
        nz1 = nz0;
      } else {
        nz0 = cgba_sb_get_row_nonzero(base_block);
        layer_sbs |= (1u << base_block);
        if (map_size & 1u) {
          nz0 |= cgba_sb_get_row_nonzero(base_block + 1u);
          if (base_block + 1u < 32u)
            layer_sbs |= (1u << (base_block + 1u));
        }
        nz1 = cgba_sb_get_row_nonzero(sb_bot);
        if (sb_bot < 32u)
          layer_sbs |= (1u << sb_bot);
        if (map_size & 1u) {
          nz1 |= cgba_sb_get_row_nonzero(sb_bot + 1u);
          if (sb_bot + 1u < 32u)
            layer_sbs |= (1u << (sb_bot + 1u));
        }
      }
      p->row_nonzero[0] = nz0;
      p->row_nonzero[1] = nz1;
      p->flags = (u8)(1u | (((nz0 & nz1) != 0xFFFFFFFFu) ? 2u : 0u));
    }
#else
    p->row_nonzero[0] = 0xFFFFFFFFu;
    p->row_nonzero[1] = 0xFFFFFFFFu;
    p->flags = 1u;
#endif
    cgba_m0.bg_static_key[layer] = skey;
    cgba_m0.bg_sb_mask[layer] = layer_sbs;
    used_sbs |= layer_sbs;
  }
  cgba_m0.used_sb_mask = used_sbs;
  if (blend_bg < 4u && !(cgba_bg_plans[blend_bg].flags & 1u))
    cgba_m0.blend_bg_layer = 0xFFu;
}

template<bool saturate>
static inline u16 cgba_blend_rgb565_direct(u32 term_a_val, u16 dst_pix, u32 evb)
{
  u32 p2e = ((u32)dst_pix | ((u32)dst_pix << 16)) & BLND_MSK;
  u32 pfe = (term_a_val + p2e * evb) >> 4;
  if (saturate) {
    if (__builtin_expect((pfe & (OVFR_MSK | OVFG_MSK | OVFB_MSK)) != 0u, 0)) {
      if (pfe & OVFG_MSK)
        pfe |= SATG_MSK;
      if (pfe & OVFR_MSK)
        pfe |= SATR_MSK;
      if (pfe & OVFB_MSK)
        pfe |= SATB_MSK;
    }
  }
  pfe &= BLND_MSK;
  return (u16)((pfe >> 16) | pfe);
}

template<bool saturate>
static __attribute__((noinline)) void cgba_render_4bpp_part_blend_strip(
  u16 *dst_top, u32 k, u32 col_start, u32 col_end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r,
  const u32 *term_a_base, u32 evb)
{
  u32 map_entry = eswap16(raw_map);
  const u8 *row = tile_charbase + ((map_entry & 0x3FFu) << 5) + (start_r << 2);
  s32 row_step = 4;
  if (map_entry & 0x800u) {
    row += 28 - (s32)(start_r << 3);
    row_step = -4;
  }
  const u32 *ta = term_a_base + ((map_entry >> 8) & 0xF0u);
  const bool hflip = (map_entry & 0x400u) != 0u;
  const u32 width = col_end - col_start;
  u16 *dst_row = dst_top;

  u32 last_tav = 0xFFFFFFFFu;
  u16 last_in = 0, last_out = 0;
  for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
    u32 w = *(const u32 *)row;
    if (w == 0u)
      continue;
    if (__builtin_expect((((w << 4) ^ w) >> 4) == 0u, 1)) {
      u32 tav = ta[w & 0xFu];
      if (tav != last_tav) {
        last_tav = tav;
        last_in = 0;
        last_out = cgba_blend_rgb565_direct<saturate>(tav, 0, evb);
      }
      for (u32 i = 0; i < width; i++) {
        u16 d = dst_row[i];
        if (d != last_in) {
          last_in = d;
          last_out = cgba_blend_rgb565_direct<saturate>(tav, d, evb);
        }
        dst_row[i] = last_out;
      }
    } else if (__builtin_expect(hflip, 0)) {
      for (u32 i = 0; i < width; i++) {
        u32 sc = 7u - (col_start + i);
        u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
        if (pix)
          dst_row[i] = cgba_blend_rgb565_direct<saturate>(ta[pix], dst_row[i], evb);
      }
    } else {
      for (u32 i = 0; i < width; i++) {
        u32 c = col_start + i;
        u32 pix = (row[c >> 1] >> ((c & 1u) << 2)) & 0xFu;
        if (pix)
          dst_row[i] = cgba_blend_rgb565_direct<saturate>(ta[pix], dst_row[i], evb);
      }
    }
  }
}

template<bool saturate>
static __attribute__((noinline)) void cgba_render_4bpp_strip_blend_c(
  const u16 *map1, u32 counts_packed, u16 *dst_top,
  const u8 *tile_charbase, u32 start_r_k_evb,
  const u32 *term_a_base, const u16 *map2)
{
  u32 todraw = counts_packed & 0xFFFFu;
  u32 todraw2 = counts_packed >> 16;
  u32 k = start_r_k_evb & 0xFFu;
  u32 start_r = (start_r_k_evb >> 8) & 0xFFu;
  u32 evb = (start_r_k_evb >> 16) & 0xFFu;
  const u8 *tb_base = tile_charbase + (start_r << 2);
  s32 vflip_off = 28 - (s32)(start_r << 3);
  const u16 *map = map1;
  u16 *dst_tile = dst_top;
  u32 last_empty = 0xFFFFFFFFu;

  for (;;) {
    while (todraw--) {
      u16 raw_map = *map++;
      if ((u32)raw_map == last_empty) {
        dst_tile += 8;
        continue;
      }
      u32 map_entry = eswap16(raw_map);
      const u8 *row = tb_base + ((map_entry & 0x3FFu) << 5);
      s32 row_step = 4;
      if (map_entry & 0x800u) {
        row += vflip_off;
        row_step = -4;
      }
      const u32 *ta = term_a_base + ((map_entry >> 8) & 0xF0u);
      bool hflip = (map_entry & 0x400u) != 0u;
      u16 *d = dst_tile;
      bool any_nz = false;
      for (u32 r = 0; r < k; r++, row += row_step, d += 240u) {
        if (*(const u32 *)row == 0u)
          continue;
        any_nz = true;
        if (!hflip) {
          for (u32 bi = 0; bi < 4u; bi++) {
            u32 b = row[bi];
            if (!b)
              continue;
            u32 p0 = b & 0xFu;
            u32 p1 = b >> 4;
            if (p0)
              d[bi * 2u] = cgba_blend_rgb565_direct<saturate>(ta[p0], d[bi * 2u], evb);
            if (p1)
              d[bi * 2u + 1u] = cgba_blend_rgb565_direct<saturate>(ta[p1], d[bi * 2u + 1u], evb);
          }
        } else {
          for (u32 bi = 0; bi < 4u; bi++) {
            u32 b = row[bi];
            if (!b)
              continue;
            u32 p0 = b & 0xFu;
            u32 p1 = b >> 4;
            u32 x0 = 7u - bi * 2u;
            if (p0)
              d[x0] = cgba_blend_rgb565_direct<saturate>(ta[p0], d[x0], evb);
            if (p1)
              d[x0 - 1u] = cgba_blend_rgb565_direct<saturate>(ta[p1], d[x0 - 1u], evb);
          }
        }
      }
      if (!any_nz)
        last_empty = (u32)raw_map;
      dst_tile += 8;
    }
    if (!todraw2)
      break;
    todraw = todraw2;
    todraw2 = 0;
    map = map2;
  }
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_blend_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  u32 eva, u32 evb)
{
  if (eva == 0u && evb == 16u)
    return;
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
  const bool saturate = (eva + evb) > 16u;
  const u32 thoff = p->tile_hoff;
  const u8 pflags = p->flags;
  u32 y = sy0;
  u16 *dst_top = dst_first_row;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (pflags & 2u) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
        const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
        u32 any = 0;
        for (u32 r = 0; r < k; r++)
          any |= t0[r];
        if (any == 0u) {
          y += k;
          dst_top += k * 240u;
          continue;
        }
      }
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map1 = (u16 *)(map_row_b + p->first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
    u32 rke = (evb << 16) | (start_r << 8) | k;

    if (__builtin_expect(thoff == 0u, 1)) {
      if (!saturate) {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
        cgba_render_4bpp_strip_blend_sh4(
          map1, p->counts_packed, dst_top, p->tile_charbase, rke, term_a, map2);
#else
        cgba_render_4bpp_strip_blend_c<false>(
          map1, p->counts_packed, dst_top, p->tile_charbase, rke, term_a, map2);
#endif
      } else {
        cgba_render_4bpp_strip_blend_c<true>(
          map1, p->counts_packed, dst_top, p->tile_charbase, rke, term_a, map2);
      }
    } else {
      u32 left_w = 8u - thoff;
      u32 cp = p->counts_packed;
      u16 left_raw = *map1++;
      u16 *mid_map1 = (cp & 0xFFFFu) ? map1 : map2;
      u32 mid_cp = (cp & 0xFFFFu) ? cp : (cp >> 16);
      u16 right_raw = *(map2 + (cp >> 16));
      if (!saturate) {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
        cgba_render_4bpp_strip_blend_sh4(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke, term_a, map2);
#else
        cgba_render_4bpp_strip_blend_c<false>(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke, term_a, map2);
#endif
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, term_a, evb);
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, term_a, evb);
      } else {
        cgba_render_4bpp_strip_blend_c<true>(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke, term_a, map2);
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, term_a, evb);
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, term_a, evb);
      }
    }
    y += k;
    dst_top += k * 240u;
  }
}

template<bool isbase>
static __attribute__((noinline)) void cgba_render_4bpp_part_fullcolor(
  u16 *dst, u32 start, u32 end, u16 raw_map,
  const u8 *tile_base, s32 vflip_off, const u16 *paltbl)
{
  u16 tile = eswap16(raw_map);
  const u8 *tile_ptr = tile_base + ((tile & 0x3FFu) << 5);
  if (tile & 0x800u)
    tile_ptr += vflip_off;
  u32 raw = *(const u32 *)tile_ptr;
  if (!raw) {
    if (isbase) {
      u16 bg = paltbl[0];
      for (u32 i = start; i < end; i++)
        *dst++ = bg;
    }
    return;
  }
  const u16 *subpal = paltbl + ((tile >> 8) & 0xF0u);
  u32 tilepix = eswap32(raw);
  if (__builtin_expect((tile & 0x400u) != 0, 0)) {
    tilepix <<= (start << 2);
    for (u32 i = start; i < end; i++, dst++) {
      u32 pval = tilepix >> 28;
      tilepix <<= 4;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
      if (isbase || pval)
        *dst = subpal[pval];
#else
      if (pval)
        *dst = subpal[pval];
      else if (isbase)
        *dst = paltbl[0];
#endif
    }
  } else {
    tilepix >>= (start << 2);
    for (u32 i = start; i < end; i++, dst++) {
      u32 pval = tilepix & 0xFu;
      tilepix >>= 4;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
      if (isbase || pval)
        *dst = subpal[pval];
#else
      if (pval)
        *dst = subpal[pval];
      else if (isbase)
        *dst = paltbl[0];
#endif
    }
  }
}

static inline void cgba_bg_plan_invalidate_all(void)
{
  for (u32 l = 0; l < 4u; l++)
    cgba_m0.bg_static_key[l] = 0u;
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static inline void cgba_bg_vram_invalidate(void)
{
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
  u32 sbd = cgba_vram_sb_dirty;
  cgba_vram_sb_dirty = 0;
  cgba_sb_valid_mask &= sbd ? ~sbd : 0u;
  if (!sbd) {
    cgba_bg_plan_valid = false;
    for (u32 l = 0; l < 4u; l++)
      cgba_m0.bg_static_key[l] = 0u;
  } else {
    if (sbd & cgba_m0.used_sb_mask)
      cgba_bg_plan_valid = false;
    for (u32 l = 0; l < 4u; l++) {
      if (sbd & cgba_m0.bg_sb_mask[l])
        cgba_m0.bg_static_key[l] = 0u;
    }
  }
#else
  cgba_sb_valid_mask = 0;
  cgba_bg_plan_valid = false;
  for (u32 l = 0; l < 4u; l++)
    cgba_m0.bg_static_key[l] = 0u;
#endif
}
#endif

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
static __attribute__((noinline)) void render_mode0_bg_unaligned_sh4_pal(
  const cgba_mode0_bg_plan *p, u32 vcount, u16 *screen_offset, u32 base_done,
  const u16 *pal_conv, const u16 *base_pal, const u32 *pair_lut)
{
  u32 voffset = (vcount + p->vofs) & 511u;
  u32 vhalf = voffset >> 8;
  u32 vert_pix = (voffset & 7u) << 2;
  const u8 *tile_base = p->tile_charbase + vert_pix;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  if (__builtin_expect((p->flags & 2u) != 0u && base_done != 0u, 0)) {
    u32 vtile_row = (voffset & 255u) >> 3;
    if (!(p->row_nonzero[vhalf] & (1u << vtile_row)) &&
        *(const u32 *)tile_base == 0u)
      return;
  }
#endif
  s32 vflip_off = 28 - (s32)(vert_pix << 1);
  u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
  u16 *map_ptr = (u16 *)(map_row_b + p->first_byte_off);
  u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
  u32 thoff = p->tile_hoff;
  u16 *dst = screen_offset;
  u32 cp = p->counts_packed;

  if (base_done) {
    cgba_render_4bpp_part_fullcolor<false>(
      dst, thoff, 8u, *map_ptr++, tile_base, vflip_off, pal_conv);
    dst += 8u - thoff;
    if (cp & 0xFFFFu) {
      cgba_render_4bpp_span2_over_pair_sh4(
        map_ptr, cp, dst, tile_base, vflip_off, pair_lut, pal_conv, map2);
      map_ptr = map2 + (cp >> 16);
    } else {
      u32 td2 = cp >> 16;
      cgba_render_4bpp_span_over_pair_sh4(
        map2, td2, dst, tile_base, vflip_off, pair_lut, pal_conv);
      map_ptr = map2 + td2;
    }
    cgba_render_4bpp_part_fullcolor<false>(
      dst + 232u, 0u, thoff, *map_ptr, tile_base, vflip_off, pal_conv);
  } else {
    cgba_render_4bpp_part_fullcolor<true>(
      dst, thoff, 8u, *map_ptr++, tile_base, vflip_off, base_pal);
    dst += 8u - thoff;
    if (cp & 0xFFFFu) {
      cgba_render_4bpp_span2_base_pair_sh4(
        map_ptr, cp, dst, tile_base, vflip_off, pair_lut, map2);
      map_ptr = map2 + (cp >> 16);
    } else {
      u32 td2 = cp >> 16;
      cgba_render_4bpp_span_base_pair_sh4(
        map2, td2, dst, tile_base, vflip_off, pair_lut);
      map_ptr = map2 + td2;
    }
    cgba_render_4bpp_part_fullcolor<true>(
      dst + 232u, 0u, thoff, *map_ptr, tile_base, vflip_off, base_pal);
  }
}

static inline void render_mode0_bg_unaligned_sh4(
  const cgba_mode0_bg_plan *p, u32 vcount, u16 *screen_offset, u32 base_done)
{
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  render_mode0_bg_unaligned_sh4_pal(
    p, vcount, screen_offset, base_done,
    palette_ram_converted, backdrop_shadow_palette,
    (const u32 *)&pair_palette[0][0][0]);
#else
  render_mode0_bg_unaligned_sh4_pal(
    p, vcount, screen_offset, base_done,
    palette_ram_converted, get_backdrop_shadow_palette(),
    pair_palette_get_all_u32());
#endif
}
#endif

static __attribute__((noinline)) void render_mode0_fullcolor_scanline(
  u32 vcount, u16 *screen_offset, const u8 *lorder, u32 lcount)
{
  const cgba_mode0_bg_plan *const plans = cgba_m0.plans;
  const u16 *const pal_conv = palette_ram_converted;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_PAIR_PALETTE)
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];
#else
  const u16 *const base_pal = backdrop_shadow_palette;
#endif
#else
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_PAIR_PALETTE)
  const u32 *const pair_lut = pair_palette_get_all_u32();
#else
  const u16 *const base_pal = get_backdrop_shadow_palette();
#endif
#endif
#else
  const u16 *const base_pal = pal_conv;
#endif

  u32 base_done = 0;
  for (u32 lnum = 0; lnum < lcount; lnum++) {
    u32 layer = lorder[lnum];
    if (layer & 0x4u) {
      if (!base_done)
        fill_line_background<FULLCOLOR, u16>(0, 240, screen_offset);
      write_ioreg(REG_VCOUNT, vcount);
      render_scanline_objs<u16, FULLCOLOR>(
        layer & 0x3u, 0, 240, screen_offset, pal_conv + 0x100);
      base_done = 1;
      continue;
    }

    const cgba_mode0_bg_plan *p = &plans[layer];
    if (__builtin_expect(layer == cgba_m0.blend_bg_layer, 0)) {
      if (!base_done) {
        fill_line_background<FULLCOLOR, u16>(0, 240, screen_offset);
        base_done = 1;
      }
      cgba_render_4bpp_bg_blend_rows(
        p, vcount, vcount + 1u, screen_offset,
        cgba_m0.blend_eva, cgba_m0.blend_evb);
      continue;
    }
    u8 pflags = p->flags;
    if (__builtin_expect(!(pflags & 1u), 0)) {
      write_ioreg(REG_VCOUNT, vcount);
      if (base_done)
        render_scanline_text<u16, FULLCOLOR, false>(
          layer, 0, 240, screen_offset, pal_conv);
      else
        render_scanline_text<u16, FULLCOLOR, true>(
          layer, 0, 240, screen_offset, pal_conv);
      base_done = 1;
      continue;
    }

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
    if (__builtin_expect(p->tile_hoff == 0u, 1)) {
      if (base_done)
        cgba_render_4bpp_plan_over_sh4(p, vcount, screen_offset, pair_lut);
      else {
        base_done = 1;
        cgba_render_4bpp_plan_base_sh4(p, vcount, screen_offset, pair_lut);
      }
      continue;
    }
    render_mode0_bg_unaligned_sh4(p, vcount, screen_offset, base_done);
    base_done = 1;
    continue;
#else
    u32 voffset = (vcount + p->vofs) & 511u;
    u32 vhalf = voffset >> 8;
    u32 vert_pix = (voffset & 7u) << 2;
    const u8 *tile_base = p->tile_charbase + vert_pix;

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    if (__builtin_expect((pflags & 2u) != 0u && base_done != 0u, 0)) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if (!(p->row_nonzero[vhalf] & (1u << vtile_row)) &&
          *(const u32 *)tile_base == 0u)
        continue;
    }
#endif

    s32 vflip_off = 28 - (s32)(vert_pix << 1);
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map_ptr = (u16 *)(map_row_b + p->first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
    u32 thoff = p->tile_hoff;

    if (base_done) {
      u16 *dst = screen_offset;
      u32 td1 = p->counts_packed & 0xFFFFu;
      u32 td2 = p->counts_packed >> 16;
      if (thoff) {
        cgba_render_4bpp_part_fullcolor<false>(
          dst, thoff, 8u, *map_ptr++, tile_base, vflip_off, pal_conv);
        dst += 8u - thoff;
      }
      if (td1) {
        render_text_tile_run<u16, FULLCOLOR, false, false, false>(
          td1, map_ptr, dst, 0u, 0u, tile_base, vflip_off, pal_conv);
      }
      map_ptr = map2;
      if (td2) {
        render_text_tile_run<u16, FULLCOLOR, false, false, false>(
          td2, map_ptr, dst, 0u, 0u, tile_base, vflip_off, pal_conv);
      }
      if (thoff) {
        cgba_render_4bpp_part_fullcolor<false>(
          dst, 0u, thoff, *map_ptr, tile_base, vflip_off, pal_conv);
      }
    } else {
      base_done = 1;
      u16 *dst = screen_offset;
      u32 td1 = p->counts_packed & 0xFFFFu;
      u32 td2 = p->counts_packed >> 16;
      if (thoff) {
        cgba_render_4bpp_part_fullcolor<true>(
          dst, thoff, 8u, *map_ptr++, tile_base, vflip_off, base_pal);
        dst += 8u - thoff;
      }
      if (td1) {
        render_text_tile_run<u16, FULLCOLOR, false, true, false>(
          td1, map_ptr, dst, 0u, 0u, tile_base, vflip_off, base_pal);
      }
      map_ptr = map2;
      if (td2) {
        render_text_tile_run<u16, FULLCOLOR, false, true, false>(
          td2, map_ptr, dst, 0u, 0u, tile_base, vflip_off, base_pal);
      }
      if (thoff) {
        cgba_render_4bpp_part_fullcolor<true>(
          dst, 0u, thoff, *map_ptr, tile_base, vflip_off, base_pal);
      }
    }
#endif
  }

  if (!base_done)
    fill_line_background<FULLCOLOR, u16>(0, 240, screen_offset);
}
#else
static inline void cgba_bg_plan_invalidate_all(void) {}
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static inline void cgba_bg_vram_invalidate(void)
{
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
  u32 sbd = cgba_vram_sb_dirty;
  cgba_vram_sb_dirty = 0;
  cgba_sb_valid_mask &= sbd ? ~sbd : 0u;
#else
  cgba_sb_valid_mask = 0;
#endif
}
#endif
#endif /* !CGBA_VIDEO_TILE_ROW_CACHE */

static inline void cgba_sync_mode0_palettes(void)
{
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
  (void)get_backdrop_shadow_palette();
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_PAIR_PALETTE)
  (void)pair_palette_get_all_u32();
#endif
#endif
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static __attribute__((noinline)) bool cgba_sl_begin_frame(
  u32 video_mode, u16 dispcnt)
{
  bool bg_dirty = !cgba_sl_prev_uniform;
  bool obj_dirty = false;
  cgba_video_frame_dirty_rows = 0;
  cgba_sl_cur_uniform = true;

  cgba_video_io_dirty = 0;
  bool io_same = cgba_sl_check_and_capture_frame_io(video_mode, dispcnt);
  if (!io_same || !cgba_sl_prev_uniform) {
    bg_dirty = true;
    cgba_bg_plan_valid = false;
  }

  {
    u32 vd = cgba_vram_dirty;
    if (vd) {
      cgba_vram_dirty = 0;
      if (vd & CGBA_VRAM_DIRTY_BG) {
        bg_dirty = true;
        cgba_bg_vram_invalidate();
      }
      if (vd & CGBA_VRAM_DIRTY_OBJ) {
        obj_dirty = true;
        if (video_mode >= 3)
          bg_dirty = true;
      }
    }
  }
  cgba_sl_sync_palette(&bg_dirty, &obj_dirty);
  cgba_sync_mode0_palettes();
  if (dispcnt & 0x80) {
    /* Steady forced blank: only depends on dispcnt (checked via io_same). */
    obj_dirty = false;
    bg_dirty = !cgba_sl_prev_uniform || !io_same;
  }
  if (reg[OAM_UPDATED])
    cgba_sl_sync_oam(video_mode, dispcnt, bg_dirty);

  cgba_sl_frame_bg_dirty = bg_dirty;
  cgba_sl_frame_obj_dirty = obj_dirty;
  return bg_dirty || !cgba_sl_row_valid[0] ||
         (obj_dirty && cgba_sl_row_has_obj[0]);
}

static __attribute__((noinline)) void cgba_sl_midframe_dirty(
  u32 video_mode, u16 dispcnt)
{
  if (cgba_video_io_dirty) {
    cgba_video_io_dirty = 0;
    cgba_sl_cur_uniform = false;
    cgba_sl_frame_bg_dirty = true;
    cgba_bg_plan_valid = false;
  }
  if (cgba_vram_dirty) {
    u32 vd = cgba_vram_dirty;
    cgba_vram_dirty = 0;
    cgba_sl_cur_uniform = false;
    if (vd & CGBA_VRAM_DIRTY_BG) {
      cgba_sl_frame_bg_dirty = true;
      cgba_bg_vram_invalidate();
    }
    if (vd & CGBA_VRAM_DIRTY_OBJ) {
      cgba_sl_frame_obj_dirty = true;
      if (video_mode >= 3)
        cgba_sl_frame_bg_dirty = true;
    }
  }
  if (palette_ram_dirty & CGBA_PALETTE_DIRTY_SCANLINE) {
    bool pbg = false, pobj = false;
    cgba_sl_sync_palette(&pbg, &pobj);
    cgba_sync_mode0_palettes();
    if (pbg) {
      cgba_sl_cur_uniform = false;
      cgba_sl_frame_bg_dirty = true;
    }
    if (pobj) {
      cgba_sl_cur_uniform = false;
      cgba_sl_frame_obj_dirty = true;
    }
  }
  if (reg[OAM_UPDATED]) {
    cgba_sl_cur_uniform = false;
    cgba_sl_sync_oam(video_mode, dispcnt, cgba_sl_frame_bg_dirty);
  }
}
#endif

static __attribute__((noinline)) void cgba_update_affine_refs(u32 vcount)
{
  // Account for vertical mosaic effect, by correcting affine references.
  const u32 bgmosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xF) + 1;

  if (read_ioreg(REG_BG2CNT) & 0x40) {   // Mosaic enabled for this BG
    if ((vcount % bgmosv) == bgmosv-1) { // Correct after the last line
      affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB) * bgmosv;
      affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD) * bgmosv;
    }
  } else {
    affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB);
    affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD);
  }

  if (read_ioreg(REG_BG3CNT) & 0x40) {
    if ((vcount % bgmosv) == bgmosv-1) {
      affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB) * bgmosv;
      affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD) * bgmosv;
    }
  } else {
    affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB);
    affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD);
  }
}

#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
volatile u32 cgba_pending_sl_count = 0;
u32 cgba_pending_sl_start = 0;

void cgba_discard_pending_scanlines(void)
{
  cgba_pending_sl_count = 0;
  cgba_pending_sl_start = 0;
}

#if !defined(CGBA_VIDEO_TILE_ROW_CACHE) && defined(CGBA_SH4_ASM_4BPP) && \
    defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
static u16 cgba_folded_pal[512] __attribute__((aligned(32)));
static u16 cgba_folded_bg_shadow[256] __attribute__((aligned(32)));
static u16 cgba_folded_pair_lut[16][256][2] CGBA_HIGH_BSS;
static u16 cgba_folded_pal_snap[512] __attribute__((aligned(32)));
static u32 cgba_folded_key = 0xFFFFFFFFu;
static bool cgba_folded_valid = false;

static inline void cgba_fill_row_u16(u16 *dst, u16 col)
{
  u32 pair = ((u32)col << 16) | (u32)col;
  u32 *d32 = (u32 *)dst;
  for (u32 i = 0; i < 120u; i++)
    d32[i] = pair;
}

static inline void cgba_fill_span_u16(u16 *dst, u32 count, u16 col)
{
  if (count == 0u)
    return;
  if (((uintptr_t)dst & 2u) != 0u) {
    *dst++ = col;
    count--;
  }
  u32 pair = ((u32)col << 16) | (u32)col;
  u32 *d32 = (u32 *)dst;
  u32 pairs = count >> 1;
  for (u32 i = 0; i < pairs; i++)
    d32[i] = pair;
  if (count & 1u)
    dst[count - 1u] = col;
}

static bool cgba_blend_tile_is_solid_cidx(
  u16 raw_map, const u8 *tile_charbase, u32 start_r, u32 k, u32 *out_cidx)
{
  u32 me = eswap16(raw_map);
  const u8 *row = tile_charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
  s32 step = 4;
  if (me & 0x800u) {
    row += 28 - (s32)(start_r << 3);
    step = -4;
  }
  u32 w0 = *(const u32 *)row;
  if (w0 == 0u || (((w0 << 4) ^ w0) >> 4) != 0u)
    return false;
  for (u32 r = 1u; r < k; r++) {
    row += step;
    if (*(const u32 *)row != w0)
      return false;
  }
  *out_cidx = ((me >> 8) & 0xF0u) | (w0 & 0x0Fu);
  return true;
}

/* Analyze a strip [sy0, sy1) of blend_layer for uniform solid margins.
 * Returns true if either the entire strip is uniform solid cidx (out_xlo=240,
 * out_xhi=0) OR the left [0, out_xlo) + right [out_xhi, 240) solid-cidx margins
 * cover >= 32 pixels so horizontal windowed palette folding pays off. */
static bool cgba_blend_strip_analyze(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1,
  u32 *out_cidx, u32 *out_xlo, u32 *out_xhi)
{
  const u32 thoff = p->tile_hoff;
  const u32 cp = p->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  const u32 nslots = thoff ? (td1 + td2 + 2u) : (td1 + td2);

  bool have_cidx = false;
  u32 expected_cidx = 0u;
  u32 min_left = nslots;
  u32 min_right = nslots;
  u32 y = sy0;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    const u16 *map1 = (const u16 *)(map_row_b + p->first_byte_off);
    const u16 *map2 = (const u16 *)(map_row_b + p->map2_byte_off);

    auto get_slot = [&](u32 i) -> u16 {
      if (thoff == 0u)
        return (i < td1) ? map1[i] : map2[i - td1];
      if (i <= td1)
        return map1[i];
      return map2[i - 1u - td1];
    };

    bool have_cached = false;
    u16 cached_raw = 0u;
    auto is_solid_match = [&](u16 rm) -> bool {
      if (have_cached && rm == cached_raw)
        return true;
      u32 cidx = 0u;
      if (!cgba_blend_tile_is_solid_cidx(rm, p->tile_charbase, start_r, k, &cidx))
        return false;
      if (!have_cidx) {
        have_cidx = true;
        expected_cidx = cidx;
      } else if (cidx != expected_cidx) {
        return false;
      }
      have_cached = true;
      cached_raw = rm;
      return true;
    };

    u32 left_cnt = 0u;
    while (left_cnt < min_left && is_solid_match(get_slot(left_cnt)))
      left_cnt++;
    min_left = left_cnt;

    if (min_left < nslots) {
      u32 right_limit = nslots - min_left;
      if (min_right > right_limit)
        min_right = right_limit;
      u32 right_cnt = 0u;
      while (right_cnt < min_right &&
             is_solid_match(get_slot(nslots - 1u - right_cnt)))
        right_cnt++;
      min_right = right_cnt;
      if (min_left + min_right < 4u)
        return false;
    }

    y += k;
  }

  if (!have_cidx)
    return false;
  if (min_left == nslots) {
    *out_cidx = expected_cidx;
    *out_xlo = 240u;
    *out_xhi = 0u;
    return true;
  }

  u32 x_lo = 0u;
  if (min_left > 0u)
    x_lo = thoff ? ((8u - thoff) + (min_left - 1u) * 8u) : (min_left * 8u);
  u32 x_hi = 240u;
  if (min_right > 0u)
    x_hi = thoff ? (240u - thoff - (min_right - 1u) * 8u) : (240u - min_right * 8u);

  if (x_lo >= x_hi) {
    *out_cidx = expected_cidx;
    *out_xlo = 240u;
    *out_xhi = 0u;
    return true;
  }

  if (x_lo + (240u - x_hi) < 32u)
    return false;

  *out_cidx = expected_cidx;
  *out_xlo = x_lo;
  *out_xhi = x_hi;
  return true;
}

template<bool isbase>
static __attribute__((noinline)) void cgba_render_4bpp_part_strip_fullcolor(
  u16 *dst_top, u32 k, u32 start, u32 end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r, const u16 *paltbl)
{
  u32 tile = eswap16(raw_map);
  const u8 *row = tile_charbase + ((tile & 0x3FFu) << 5) + (start_r << 2);
  s32 row_step = 4;
  if (tile & 0x800u) {
    row += 28 - (s32)(start_r << 3);
    row_step = -4;
  }
  const u16 *subpal = paltbl + ((tile >> 8) & 0xF0u);
  const u16 bg = paltbl[0];
  const u32 width = end - start;
  const u32 shift = start << 2;
  u16 *dst_row = dst_top;

  if (__builtin_expect((tile & 0x400u) != 0u, 0)) {
    for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
      u32 raw = *(const u32 *)row;
      if (!raw) {
        if (isbase) {
          for (u32 i = 0; i < width; i++)
            dst_row[i] = bg;
        }
        continue;
      }
      u32 tilepix = eswap32(raw) << shift;
      for (u32 i = 0; i < width; i++) {
        u32 pval = tilepix >> 28;
        tilepix <<= 4;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
        if (isbase || pval)
          dst_row[i] = subpal[pval];
#else
        if (pval)
          dst_row[i] = subpal[pval];
        else if (isbase)
          dst_row[i] = bg;
#endif
      }
    }
  } else {
    for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
      u32 raw = *(const u32 *)row;
      if (!raw) {
        if (isbase) {
          for (u32 i = 0; i < width; i++)
            dst_row[i] = bg;
        }
        continue;
      }
      u32 tilepix = eswap32(raw) >> shift;
      for (u32 i = 0; i < width; i++) {
        u32 pval = tilepix & 0xFu;
        tilepix >>= 4;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
        if (isbase || pval)
          dst_row[i] = subpal[pval];
#else
        if (pval)
          dst_row[i] = subpal[pval];
        else if (isbase)
          dst_row[i] = bg;
#endif
      }
    }
  }
}

struct cgba_bg_hspan_plan {
  u16 first_byte_off;
  u16 map2_byte_off;
  u32 cp;
  u16 x0;
  u16 mid_x;
  u16 right_x;
  u8  thoff;
  u8  left_w;
  u8  right_w;
  u8  mid_aligned;
};

static inline void cgba_build_bg_hspan_plan(
  u32 hofs, u32 wide, u32 x0, u32 x1, cgba_bg_hspan_plan *out)
{
  u32 total_w = x1 - x0;
  u32 h = (hofs + x0) & (wide ? 511u : 255u);
  u16 map_right_off = 0u, second_off = 0u;
  if (wide) {
    if (h >= 256u) {
      h -= 256u;
      map_right_off = 1024u;
    } else {
      second_off = 1024u;
    }
  }
  u32 pixel_run = 256u - h;
  u32 thoff = h & 7u;
  u32 left_w = thoff ? MIN(total_w, 8u - thoff) : 0u;
  u32 rem = total_w - left_w;
  pixel_run -= left_w;
  u32 todraw1 = MIN(rem, pixel_run) >> 3;
  rem -= todraw1 << 3;
  pixel_run -= todraw1 << 3;
  bool need_switch = (rem > 0u) && (pixel_run == 0u);
  u32 todraw2 = rem >> 3;
  u32 right_w = rem & 7u;
  out->first_byte_off = (u16)((map_right_off + (h >> 3)) << 1);
  out->map2_byte_off = (u16)((need_switch
                                ? second_off
                                : (map_right_off + (h >> 3) + (left_w ? 1u : 0u) + todraw1)) << 1);
  out->cp = todraw1 | (todraw2 << 16);
  out->x0 = (u16)x0;
  out->mid_x = (u16)(x0 + left_w);
  out->right_x = (u16)(x1 - right_w);
  out->thoff = (u8)thoff;
  out->left_w = (u8)left_w;
  out->right_w = (u8)right_w;
  out->mid_aligned = ((x0 + left_w) & 1u) == 0u ? 1u : 0u;
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_split_sh4(
  u32 layer, u32 sy0, u32 sy1, u32 xlo, u32 xhi,
  u16 *screen_base, u32 base_done)
{
  const cgba_mode0_bg_plan *const p = &cgba_m0.plans[layer];
  const u32 hofs = cgba_m0.bg_hofs[layer];
  const u32 wide = cgba_m0.bg_wide[layer];
  const u16 *const pal_conv = palette_ram_converted;
  const u16 *const base_pal = backdrop_shadow_palette;
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];
  const u32 *const fpair_lut = (const u32 *)&cgba_folded_pair_lut[0][0][0];

  cgba_bg_hspan_plan segs[3];
  const u16 *seg_pal[3];
  const u16 *seg_bpal[3];
  const u32 *seg_plut[3];
  u32 nseg = 0u;

  if (xlo > 0u) {
    cgba_build_bg_hspan_plan(hofs, wide, 0u, xlo, &segs[nseg]);
    seg_pal[nseg] = cgba_folded_pal;
    seg_bpal[nseg] = cgba_folded_bg_shadow;
    seg_plut[nseg] = fpair_lut;
    nseg++;
  }
  cgba_build_bg_hspan_plan(hofs, wide, xlo, xhi, &segs[nseg]);
  seg_pal[nseg] = pal_conv;
  seg_bpal[nseg] = base_pal;
  seg_plut[nseg] = pair_lut;
  nseg++;
  if (xhi < 240u) {
    cgba_build_bg_hspan_plan(hofs, wide, xhi, 240u, &segs[nseg]);
    seg_pal[nseg] = cgba_folded_pal;
    seg_bpal[nseg] = cgba_folded_bg_shadow;
    seg_plut[nseg] = fpair_lut;
    nseg++;
  }

  const u8 *const charbase = p->tile_charbase;
  const u8 pflags = p->flags;
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (base_done && (pflags & 2u)) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
        const u32 *t0 = (const u32 *)(charbase + (start_r << 2));
        u32 any = 0;
        for (u32 r = 0; r < k; r++)
          any |= t0[r];
        if (any == 0u) {
          y += k;
          continue;
        }
      }
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *dst_top = screen_base + y * 240u;
    u32 rk = (start_r << 8) | k;

    for (u32 s = 0; s < nseg; s++) {
      const cgba_bg_hspan_plan *sp = &segs[s];
      u16 *map1 = (u16 *)(map_row_b + sp->first_byte_off);
      u16 *map2 = (u16 *)(map_row_b + sp->map2_byte_off);
      u32 left_w = sp->left_w;
      u32 right_w = sp->right_w;
      u32 cp = sp->cp;
      u32 todraw1 = cp & 0xFFFFu;
      u32 todraw2 = cp >> 16;
      u16 left_raw = left_w ? *map1++ : 0u;
      u16 right_raw = right_w ? *(map2 + todraw2) : 0u;

      if (cp != 0u) {
        u16 *mid_map1 = todraw1 ? map1 : map2;
        u32 mid_cp = todraw1 ? cp : todraw2;
        if (__builtin_expect(sp->mid_aligned != 0u, 1)) {
          if (base_done)
            cgba_render_4bpp_strip_over_sh4(
              mid_map1, mid_cp, dst_top + sp->mid_x, charbase, rk, seg_plut[s], map2);
          else
            cgba_render_4bpp_strip_base_sh4(
              mid_map1, mid_cp, dst_top + sp->mid_x, charbase, rk, seg_plut[s], map2);
        } else {
          for (u32 r = 0; r < k; r++) {
            u32 vp = (start_r + r) << 2;
            const u8 *tb = charbase + vp;
            s32 vfo = 28 - (s32)(vp << 1);
            u16 *row_dst = dst_top + r * 240u + sp->mid_x;
            if (base_done) {
              if (todraw1)
                cgba_render_4bpp_span2_over_pair_sh4(
                  map1, cp, row_dst, tb, vfo, seg_plut[s], seg_pal[s], map2);
              else
                cgba_render_4bpp_span_over_pair_sh4(
                  map2, todraw2, row_dst, tb, vfo, seg_plut[s], seg_pal[s]);
            } else {
              if (todraw1)
                cgba_render_4bpp_span2_base_pair_sh4(
                  map1, cp, row_dst, tb, vfo, seg_plut[s], map2);
              else
                cgba_render_4bpp_span_base_pair_sh4(
                  map2, todraw2, row_dst, tb, vfo, seg_plut[s]);
            }
          }
        }
      }
      if (left_w) {
        if (base_done)
          cgba_render_4bpp_part_strip_fullcolor<false>(
            dst_top + sp->x0, k, sp->thoff, sp->thoff + left_w, left_raw,
            charbase, start_r, seg_pal[s]);
        else
          cgba_render_4bpp_part_strip_fullcolor<true>(
            dst_top + sp->x0, k, sp->thoff, sp->thoff + left_w, left_raw,
            charbase, start_r, seg_bpal[s]);
      }
      if (right_w) {
        if (base_done)
          cgba_render_4bpp_part_strip_fullcolor<false>(
            dst_top + sp->right_x, k, 0u, right_w, right_raw,
            charbase, start_r, seg_pal[s]);
        else
          cgba_render_4bpp_part_strip_fullcolor<true>(
            dst_top + sp->right_x, k, 0u, right_w, right_raw,
            charbase, start_r, seg_bpal[s]);
      }
    }
    y += k;
  }
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_blend_span(
  const cgba_mode0_bg_plan *p, u32 hofs, u32 wide,
  u32 sy0, u32 sy1, u32 x0, u32 x1,
  u16 *screen_base, u32 eva, u32 evb)
{
  u32 total_w = x1 - x0;
  if (total_w == 0u || (eva == 0u && evb == 16u))
    return;
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
  const bool saturate = (eva + evb) > 16u;
  u32 h = (hofs + x0) & (wide ? 511u : 255u);
  u16 map_right_off = 0u, second_off = 0u;
  if (wide) {
    if (h >= 256u) {
      h -= 256u;
      map_right_off = 1024u;
    } else {
      second_off = 1024u;
    }
  }
  u32 pixel_run = 256u - h;
  u32 thoff = h & 7u;
  u32 left_w = thoff ? MIN(total_w, 8u - thoff) : 0u;
  u32 rem = total_w - left_w;
  pixel_run -= left_w;
  u32 todraw1 = MIN(rem, pixel_run) >> 3;
  rem -= todraw1 << 3;
  pixel_run -= todraw1 << 3;
  bool need_switch = (rem > 0u) && (pixel_run == 0u);
  u32 todraw2 = rem >> 3;
  u32 right_w = rem & 7u;
  u32 first_byte_off = (map_right_off + (h >> 3)) << 1;
  u32 map2_byte_off = (need_switch
                         ? second_off
                         : (map_right_off + (h >> 3) + (left_w ? 1u : 0u) + todraw1)) << 1;
  u32 cp = todraw1 | (todraw2 << 16);
  u32 mid_x = x0 + left_w;
  u32 right_x = x1 - right_w;
  u8 pflags = p->flags;
  u32 y = sy0;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (pflags & 2u) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
        const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
        u32 any = 0;
        for (u32 r = 0; r < k; r++)
          any |= t0[r];
        if (any == 0u) {
          y += k;
          continue;
        }
      }
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map1 = (u16 *)(map_row_b + first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + map2_byte_off);
    u16 *dst_top = screen_base + y * 240u;
    u16 left_raw = left_w ? *map1++ : 0u;
    u16 right_raw = right_w ? *(map2 + todraw2) : 0u;
    u32 rke = (evb << 16) | (start_r << 8) | k;

    if (cp != 0u) {
      u16 *mid_map1 = todraw1 ? map1 : map2;
      u32 mid_cp = todraw1 ? cp : todraw2;
      if (!saturate) {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
        cgba_render_4bpp_strip_blend_sh4(
          mid_map1, mid_cp, dst_top + mid_x, p->tile_charbase, rke, term_a, map2);
#else
        cgba_render_4bpp_strip_blend_c<false>(
          mid_map1, mid_cp, dst_top + mid_x, p->tile_charbase, rke, term_a, map2);
#endif
      } else {
        cgba_render_4bpp_strip_blend_c<true>(
          mid_map1, mid_cp, dst_top + mid_x, p->tile_charbase, rke, term_a, map2);
      }
    }
    if (left_w) {
      if (!saturate)
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top + x0, k, thoff, thoff + left_w, left_raw, p->tile_charbase, start_r, term_a, evb);
      else
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + x0, k, thoff, thoff + left_w, left_raw, p->tile_charbase, start_r, term_a, evb);
    }
    if (right_w) {
      if (!saturate)
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top + right_x, k, 0u, right_w, right_raw, p->tile_charbase, start_r, term_a, evb);
      else
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + right_x, k, 0u, right_w, right_raw, p->tile_charbase, start_r, term_a, evb);
    }
    y += k;
  }
}

static __attribute__((noinline)) void cgba_folded_palette_prepare(
  u16 p1, u32 eva, u32 evb)
{
  u32 key = ((u32)p1 << 16) | (eva << 8) | evb;
  bool key_changed = !cgba_folded_valid || (cgba_folded_key != key);
#if CGBA_PALETTE_FOLDED_BIT
  if (__builtin_expect(!key_changed &&
                       !(palette_ram_dirty & CGBA_PALETTE_DIRTY_FOLDED), 1))
    return;
  palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_FOLDED;
#else
  if (__builtin_expect(!key_changed, 1))
    return;
#endif
  u32 ta = (((u32)p1 | ((u32)p1 << 16)) & BLND_MSK) * eva;
  bool sat = (eva + evb) > 16u;
  bool bd_changed = key_changed ||
                    (cgba_folded_pal_snap[0] != palette_ram_converted[0]);
  u16 bd_folded = sat ? cgba_blend_rgb565_direct<true>(ta, palette_ram_converted[0], evb)
                      : cgba_blend_rgb565_direct<false>(ta, palette_ram_converted[0], evb);
  cgba_folded_pal[0] = bd_folded;

  for (u32 sub = 0; sub < 16u; sub++) {
    u32 base = sub << 4;
    if (!key_changed && !bd_changed &&
        memcmp(&cgba_folded_pal_snap[base], &palette_ram_converted[base],
               16u * sizeof(u16)) == 0)
      continue;
    memcpy(&cgba_folded_pal_snap[base], &palette_ram_converted[base],
           16u * sizeof(u16));
    u16 *fpal = &cgba_folded_pal[base];
    u16 *fshd = &cgba_folded_bg_shadow[base];
    const u16 *src = &palette_ram_converted[base];
    if (sat) {
      for (u32 c = 0; c < 16u; c++)
        fpal[c] = cgba_blend_rgb565_direct<true>(ta, src[c], evb);
    } else {
      for (u32 c = 0; c < 16u; c++)
        fpal[c] = cgba_blend_rgb565_direct<false>(ta, src[c], evb);
    }
    fshd[0] = bd_folded;
    memcpy(fshd + 1, fpal + 1, 15u * sizeof(u16));
    u16 (*out)[2] = cgba_folded_pair_lut[sub];
    for (u32 b = 0; b < 256u; b++) {
      out[b][0] = fshd[b & 15u];
      out[b][1] = fshd[b >> 4];
    }
  }

  if (key_changed ||
      memcmp(&cgba_folded_pal_snap[256], &palette_ram_converted[256],
             256u * sizeof(u16)) != 0) {
    memcpy(&cgba_folded_pal_snap[256], &palette_ram_converted[256],
           256u * sizeof(u16));
    if (sat) {
      for (u32 c = 256u; c < 512u; c++)
        cgba_folded_pal[c] = cgba_blend_rgb565_direct<true>(
          ta, palette_ram_converted[c], evb);
    } else {
      for (u32 c = 256u; c < 512u; c++)
        cgba_folded_pal[c] = cgba_blend_rgb565_direct<false>(
          ta, palette_ram_converted[c], evb);
    }
  }
  cgba_folded_key = key;
  cgba_folded_valid = true;
}

static __attribute__((noinline)) void render_mode0_fullcolor_strip_sh4(
  u32 sy0, u32 sy1, u16 *screen_base, const u8 *lorder, u32 lcount)
{
  const cgba_mode0_bg_plan *const plans = cgba_m0.plans;
  const u16 *const pal_conv = palette_ram_converted;
  const u16 *const base_pal = backdrop_shadow_palette;
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];

  const u32 blend_layer = cgba_m0.blend_bg_layer;
  bool folded = false;
  bool split_active = false;
  u32 split_xlo = 0u, split_xhi = 240u;
  u32 raw_xlo = 0u, raw_xhi = 240u;
  u32 split_cidx = 0u;
  if (__builtin_expect(blend_layer < 4u &&
                       !(cgba_m0.blend_eva == 0u && cgba_m0.blend_evb == 16u), 0)) {
    u32 cidx = 0u, xlo = 0u, xhi = 240u;
    if (cgba_blend_strip_analyze(&plans[blend_layer], sy0, sy1, &cidx, &xlo, &xhi)) {
      cgba_folded_palette_prepare(
        pal_conv[cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
      if (xlo >= xhi) {
        folded = true;
      } else {
        raw_xlo = xlo;
        raw_xhi = xhi;
        split_cidx = cidx;
        /* Align [xlo, xhi) outward to the dominant lower-BG tile grid so
         * matching lower BGs incur 0 internal partial tiles. */
        u32 best_h = 0u, best_cnt = 0u;
        for (u32 i = 0; i < lcount; i++) {
          u32 l = lorder[i];
          if (l == blend_layer)
            break;
          if ((l & 4u) || !(plans[l].flags & 1u))
            continue;
          u32 h7 = cgba_m0.bg_hofs[l] & 7u;
          u32 cnt = 0u;
          for (u32 j = i; j < lcount && lorder[j] != blend_layer; j++) {
            u32 l2 = lorder[j];
            if (!(l2 & 4u) && (plans[l2].flags & 1u) &&
                (cgba_m0.bg_hofs[l2] & 7u) == h7)
              cnt++;
          }
          if (cnt > best_cnt) {
            best_cnt = cnt;
            best_h = h7;
          }
        }
        if (best_cnt > 0u) {
          u32 d_lo = (xlo + best_h) & 7u;
          u32 xlo_a = (xlo >= d_lo) ? (xlo - d_lo) : 0u;
          u32 d_hi = (8u - ((xhi + best_h) & 7u)) & 7u;
          u32 xhi_a = (xhi + d_hi <= 240u) ? (xhi + d_hi) : 240u;
          if (xlo_a + (240u - xhi_a) >= 32u) {
            xlo = xlo_a;
            xhi = xhi_a;
          }
        }
        split_active = true;
        split_xlo = xlo;
        split_xhi = xhi;
      }
    }
  }

  const u16 *cur_pal = folded ? cgba_folded_pal : pal_conv;
  const u16 *cur_base_pal = folded ? cgba_folded_bg_shadow : base_pal;
  const u32 *cur_pair_lut = folded ? (const u32 *)&cgba_folded_pair_lut[0][0][0]
                                   : pair_lut;

  auto fill_strip_backdrop = [&]() {
    if (split_active) {
      u16 bd_f = cgba_folded_pal[0];
      u16 bd_n = pal_conv[0];
      for (u32 y = sy0; y < sy1; y++) {
        u16 *row = screen_base + y * 240u;
        if (split_xlo > 0u)
          cgba_fill_span_u16(row, split_xlo, bd_f);
        cgba_fill_span_u16(row + split_xlo, split_xhi - split_xlo, bd_n);
        if (split_xhi < 240u)
          cgba_fill_span_u16(row + split_xhi, 240u - split_xhi, bd_f);
      }
    } else {
      u16 bd = cur_pal[0];
      for (u32 y = sy0; y < sy1; y++)
        cgba_fill_row_u16(screen_base + y * 240u, bd);
    }
  };

  u32 base_done = 0;
  for (u32 lnum = 0; lnum < lcount; lnum++) {
    u32 layer = lorder[lnum];
    if (layer & 0x4u) {
      if (!base_done) {
        fill_strip_backdrop();
        base_done = 1;
      }
      u32 prio = layer & 0x3u;
      u32 pbit = 1u << prio;
      if (__builtin_expect(split_active, 0)) {
        for (u32 y = sy0; y < sy1; y++) {
          if (obj_row_prio_mask[y] & pbit) {
            write_ioreg(REG_VCOUNT, y);
            u16 *row = screen_base + y * 240u;
            if (split_xlo > 0u)
              render_scanline_objs<u16, FULLCOLOR>(
                prio, 0u, split_xlo, row, cgba_folded_pal + 0x100);
            render_scanline_objs<u16, FULLCOLOR>(
              prio, split_xlo, split_xhi, row, pal_conv + 0x100);
            if (split_xhi < 240u)
              render_scanline_objs<u16, FULLCOLOR>(
                prio, split_xhi, 240u, row, cgba_folded_pal + 0x100);
          }
        }
      } else {
        for (u32 y = sy0; y < sy1; y++) {
          if (obj_row_prio_mask[y] & pbit) {
            write_ioreg(REG_VCOUNT, y);
            render_scanline_objs<u16, FULLCOLOR>(
              prio, 0, 240, screen_base + y * 240u, cur_pal + 0x100);
          }
        }
      }
      continue;
    }

    const cgba_mode0_bg_plan *p = &plans[layer];
    if (__builtin_expect(layer == blend_layer, 0)) {
      if (!base_done) {
        fill_strip_backdrop();
        base_done = 1;
      }
      if (folded) {
        /* Any layer drawn above blend_layer (e.g. OBJ0) is NOT blended. */
        folded = false;
        cur_pal = pal_conv;
        cur_base_pal = base_pal;
        cur_pair_lut = pair_lut;
        continue;
      }
      if (split_active) {
        u32 eva = cgba_m0.blend_eva;
        u32 evb = cgba_m0.blend_evb;
#if !defined(USE_XBGR1555_FORMAT)
        if ((eva + evb) <= 16u && (split_xlo < raw_xlo || raw_xhi < split_xhi)) {
          u32 p1 = pal_conv[split_cidx];
          u32 tav = ((p1 | (p1 << 16)) & BLND_MSK) * eva;
          u32 nrows = sy1 - sy0;
          u16 *top_row = screen_base + sy0 * 240u;
          if (split_xlo < raw_xlo)
            cgba_blend_solid_rect_sh4(
              top_row + split_xlo, (nrows << 16) | (raw_xlo - split_xlo), tav, evb);
          if (raw_xhi < split_xhi)
            cgba_blend_solid_rect_sh4(
              top_row + raw_xhi, (nrows << 16) | (split_xhi - raw_xhi), tav, evb);
          cgba_render_4bpp_bg_blend_span(
            p, cgba_m0.bg_hofs[layer], cgba_m0.bg_wide[layer],
            sy0, sy1, raw_xlo, raw_xhi, screen_base, eva, evb);
        } else
#endif
        {
          cgba_render_4bpp_bg_blend_span(
            p, cgba_m0.bg_hofs[layer], cgba_m0.bg_wide[layer],
            sy0, sy1, split_xlo, split_xhi, screen_base, eva, evb);
        }
        split_active = false;
        continue;
      }
      cgba_render_4bpp_bg_blend_rows(
        p, sy0, sy1, screen_base + sy0 * 240u,
        cgba_m0.blend_eva, cgba_m0.blend_evb);
      continue;
    }
    u8 pflags = p->flags;
    if (__builtin_expect(!(pflags & 1u), 0)) {
      if (__builtin_expect(split_active, 0)) {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          u16 *row = screen_base + y * 240u;
          if (base_done) {
            if (split_xlo > 0u)
              render_scanline_text<u16, FULLCOLOR, false>(
                layer, 0u, split_xlo, row, cgba_folded_pal);
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, split_xlo, split_xhi, row, pal_conv);
            if (split_xhi < 240u)
              render_scanline_text<u16, FULLCOLOR, false>(
                layer, split_xhi, 240u, row, cgba_folded_pal);
          } else {
            if (split_xlo > 0u)
              render_scanline_text<u16, FULLCOLOR, true>(
                layer, 0u, split_xlo, row, cgba_folded_pal);
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, split_xlo, split_xhi, row, pal_conv);
            if (split_xhi < 240u)
              render_scanline_text<u16, FULLCOLOR, true>(
                layer, split_xhi, 240u, row, cgba_folded_pal);
          }
        }
      } else {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          if (base_done)
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, 0, 240, screen_base + y * 240u, cur_pal);
          else
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, 0, 240, screen_base + y * 240u, cur_pal);
        }
      }
      base_done = 1;
      continue;
    }

    if (__builtin_expect(split_active, 0)) {
      cgba_render_4bpp_bg_split_sh4(
        layer, sy0, sy1, split_xlo, split_xhi, screen_base, base_done);
      base_done = 1;
      continue;
    }

    u32 thoff = p->tile_hoff;
    if (__builtin_expect(thoff == 0u, 1)) {
      u32 y = sy0;
      while (y < sy1) {
        u32 voffset = (y + p->vofs) & 511u;
        u32 start_r = voffset & 7u;
        u32 k = 8u - start_r;
        if (y + k > sy1)
          k = sy1 - y;
        u32 vhalf = voffset >> 8;
        if (base_done && (pflags & 2u)) {
          u32 vtile_row = (voffset & 255u) >> 3;
          if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
            const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
            u32 any = 0;
            for (u32 r = 0; r < k; r++)
              any |= t0[r];
            if (any == 0u) {
              y += k;
              continue;
            }
          }
        }
        u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
        u16 *map1 = (u16 *)(map_row_b + p->first_byte_off);
        u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
        u16 *dst_top = screen_base + y * 240u;
        u32 rk = (start_r << 8) | k;
        if (base_done)
          cgba_render_4bpp_strip_over_sh4(
            map1, p->counts_packed, dst_top, p->tile_charbase, rk, cur_pair_lut, map2);
        else
          cgba_render_4bpp_strip_base_sh4(
            map1, p->counts_packed, dst_top, p->tile_charbase, rk, cur_pair_lut, map2);
        y += k;
      }
      base_done = 1;
      continue;
    }

    if (!(thoff & 1u)) {
      /* Even tile_hoff (2, 4, 6): middle full tiles start at 4-byte aligned dst! */
      u32 y = sy0;
      u32 left_w = 8u - thoff;
      u32 cp = p->counts_packed;
      while (y < sy1) {
        u32 voffset = (y + p->vofs) & 511u;
        u32 start_r = voffset & 7u;
        u32 k = 8u - start_r;
        if (y + k > sy1)
          k = sy1 - y;
        u32 vhalf = voffset >> 8;
        if (base_done && (pflags & 2u)) {
          u32 vtile_row = (voffset & 255u) >> 3;
          if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
            const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
            u32 any = 0;
            for (u32 r = 0; r < k; r++)
              any |= t0[r];
            if (any == 0u) {
              y += k;
              continue;
            }
          }
        }
        u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
        u16 *map1 = (u16 *)(map_row_b + p->first_byte_off);
        u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
        u16 *dst_top = screen_base + y * 240u;
        u16 left_raw = *map1++;
        u16 *mid_map1 = (cp & 0xFFFFu) ? map1 : map2;
        u32 mid_cp = (cp & 0xFFFFu) ? cp : (cp >> 16);
        u16 right_raw = *(map2 + (cp >> 16));
        u32 rk = (start_r << 8) | k;
        if (base_done) {
          cgba_render_4bpp_strip_over_sh4(
            mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rk, cur_pair_lut, map2);
          cgba_render_4bpp_part_strip_fullcolor<false>(
            dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, cur_pal);
          cgba_render_4bpp_part_strip_fullcolor<false>(
            dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, cur_pal);
        } else {
          cgba_render_4bpp_strip_base_sh4(
            mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rk, cur_pair_lut, map2);
          cgba_render_4bpp_part_strip_fullcolor<true>(
            dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, cur_base_pal);
          cgba_render_4bpp_part_strip_fullcolor<true>(
            dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, cur_base_pal);
        }
        y += k;
      }
      base_done = 1;
      continue;
    }

    for (u32 y = sy0; y < sy1; y++)
      render_mode0_bg_unaligned_sh4_pal(
        p, y, screen_base + y * 240u, base_done, cur_pal, cur_base_pal, cur_pair_lut);
    base_done = 1;
  }

  if (!base_done)
    fill_strip_backdrop();
}
#endif

void cgba_flush_pending_scanlines(void)
{
  u32 count = cgba_pending_sl_count;
  if (!count)
    return;
  u32 y0 = cgba_pending_sl_start;
  u32 y_end = y0 + count;
  cgba_pending_sl_count = 0;

  if (__builtin_expect(skip_next_frame != 0, 0))
    return;

  u32 pitch = get_screen_pitch();
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u16 *screen_base = get_screen_pixels();
  u32 video_mode = dispcnt & 0x07;

  if (__builtin_expect(screen_base != cgba_sl_last_pixels ||
                       pitch != cgba_sl_last_pitch, 0)) {
    video_invalidate_scanline_cache(0, 160);
    cgba_sl_last_pixels = screen_base;
    cgba_sl_last_pitch = pitch;
  }

  if (y0 == 0u) {
    (void)cgba_sl_begin_frame(video_mode, dispcnt);
  } else if ((cgba_video_io_dirty | cgba_vram_dirty |
              (palette_ram_dirty & CGBA_PALETTE_DIRTY_SCANLINE) |
              reg[OAM_UPDATED]) != 0) {
    cgba_sl_midframe_dirty(video_mode, dispcnt);
  }

  const bool bg_dirty = cgba_sl_frame_bg_dirty;
  const bool obj_dirty = cgba_sl_frame_obj_dirty;
  const u8 cur_uniform = cgba_sl_cur_uniform ? 1u : 0u;
  const bool obj_en = !(dispcnt & 0x80) &&
                      (((dispcnt & 0x1000) != 0) || ((dispcnt & 0x8000) != 0));
  const u16 saved_vc = read_ioreg(REG_VCOUNT);

#ifndef CGBA_VIDEO_TILE_ROW_CACHE
  if (__builtin_expect(video_mode == 0u && !(dispcnt & 0x80), 1)) {
    if (__builtin_expect(!cgba_bg_plan_valid, 0)) {
      cgba_rebuild_mode0_bg_plans(dispcnt);
      cgba_bg_plan_valid = true;
    }
    const bool obj_alpha_can = cgba_mode0_obj_alpha_can_blend != 0;
    const bool m0_obj_en = cgba_mode0_obj_enabled != 0;

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
    const bool has_fast = cgba_mode0_fast_fullframe != 0 ||
                          cgba_mode0_fast_no_obj != 0 ||
                          cgba_m0.blend_bg_layer < 4u;
    if (__builtin_expect(bg_dirty && pitch == 240u && (y_end - y0) >= 2u &&
                         has_fast && y_end <= 160u, 1)) {
      u32 sy0 = y0;
      while (sy0 < y_end) {
        u32 sy1 = (sy0 + 16u) & ~15u;
        if (sy1 > y_end)
          sy1 = y_end;
        u32 nrows = sy1 - sy0;
        u32 strip_omask = 0u;
        u32 alpha_or = 0u;
        if (m0_obj_en) {
          const u8 *pm = obj_row_prio_mask + sy0;
          if (obj_alpha_can) {
            const u8 *ac = obj_alpha_count + sy0;
            for (u32 r = 0; r < nrows; r++) {
              strip_omask |= pm[r];
              alpha_or |= ac[r];
            }
          } else {
            for (u32 r = 0; r < nrows; r++)
              strip_omask |= pm[r];
          }
          strip_omask &= 0x0Fu;
        }
        if (__builtin_expect(nrows >= 2u &&
                             cgba_mode0_can_fast(strip_omask) &&
                             alpha_or == 0u, 1)) {
          render_mode0_fullcolor_strip_sh4(
            sy0, sy1, screen_base,
            cgba_mode0_lorder[strip_omask], cgba_mode0_lcount[strip_omask]);
          cgba_video_frame_dirty_rows += nrows;
          if (!obj_en || strip_omask == 0u) {
            for (u32 y = sy0; y < sy1; y++) {
              cgba_sl_row_has_obj[y] = 0u;
              cgba_sl_row_valid[y] = cur_uniform;
            }
          } else {
            for (u32 y = sy0; y < sy1; y++) {
              cgba_sl_row_has_obj[y] = (obj_row_prio_mask[y] != 0) ? 1u : 0u;
              cgba_sl_row_valid[y] = cur_uniform;
            }
          }
          sy0 = sy1;
          continue;
        }
        u16 *screen_offset = screen_base + sy0 * 240u;
        for (u32 y = sy0; y < sy1; y++, screen_offset += 240u) {
          cgba_video_frame_dirty_rows++;
          u32 omask = m0_obj_en ? (u32)(obj_row_prio_mask[y] & 0x0Fu) : 0u;
          if (cgba_mode0_can_fast(omask) &&
              !(obj_alpha_can && obj_alpha_count[y])) {
            render_mode0_fullcolor_scanline(
              y, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
          } else {
            write_ioreg(REG_VCOUNT, y);
            order_layers((dispcnt >> 8) & active_layers[0], y);
            render_scanline_window(screen_offset);
          }
          cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
          cgba_sl_row_valid[y] = cur_uniform;
        }
        sy0 = sy1;
      }
      write_ioreg(REG_VCOUNT, saved_vc);
      if (y_end >= 160u)
        cgba_sl_prev_uniform = cgba_sl_cur_uniform;
      return;
    }
#endif

    u16 *screen_offset = screen_base + y0 * pitch;

    for (u32 y = y0; y < y_end; y++, screen_offset += pitch) {
      bool row_dirty = bg_dirty ||
                       (y >= 160u) ||
                       !cgba_sl_row_valid[y] ||
                       (obj_dirty && cgba_sl_row_has_obj[y]);
      if (!row_dirty)
        continue;
      cgba_video_frame_dirty_rows++;
      u32 omask = m0_obj_en ? (u32)(obj_row_prio_mask[y] & 0x0Fu) : 0u;
      if (__builtin_expect(
            cgba_mode0_can_fast(omask) &&
            !(obj_alpha_can && obj_alpha_count[y]), 1)) {
        render_mode0_fullcolor_scanline(
          y, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      } else {
        write_ioreg(REG_VCOUNT, y);
        order_layers((dispcnt >> 8) & active_layers[0], y);
        render_scanline_window(screen_offset);
      }
      if (__builtin_expect(y < 160u, 1)) {
        cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
        cgba_sl_row_valid[y] = cur_uniform;
      }
    }
    write_ioreg(REG_VCOUNT, saved_vc);
    if (y_end >= 160u)
      cgba_sl_prev_uniform = cgba_sl_cur_uniform;
    return;
  }
#endif

  {
    u16 *screen_offset = screen_base + y0 * pitch;
    for (u32 y = y0; y < y_end; y++, screen_offset += pitch) {
      bool row_dirty = bg_dirty ||
                       (y >= 160u) ||
                       !cgba_sl_row_valid[y] ||
                       (obj_dirty && cgba_sl_row_has_obj[y]);
      if (row_dirty) {
        cgba_video_frame_dirty_rows++;
        if (dispcnt & 0x80) {
          memset(screen_offset, 0xff, 240 * sizeof(u16));
        } else {
          write_ioreg(REG_VCOUNT, y);
          order_layers((dispcnt >> 8) & active_layers[video_mode], y);
          render_scanline_window(screen_offset);
        }
        if (y < 160u) {
          cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
          cgba_sl_row_valid[y] = cur_uniform;
        }
      }
      if (video_mode != 0u)
        cgba_update_affine_refs(y);
    }
    write_ioreg(REG_VCOUNT, saved_vc);
    if (y_end >= 160u)
      cgba_sl_prev_uniform = cgba_sl_cur_uniform;
  }
}

void cgba_queue_scanlines(u32 start_y, u32 count)
{
  if (__builtin_expect(skip_next_frame != 0 || count == 0u, 0))
    return;
  u32 cur = cgba_pending_sl_count;
  if (cur != 0u && start_y == cgba_pending_sl_start + cur) {
    cgba_pending_sl_count = cur + count;
    return;
  }
  if (cur != 0u)
    cgba_flush_pending_scanlines();
  cgba_pending_sl_start = start_y;
  cgba_pending_sl_count = count;
}
#endif

void update_scanline(void)
{
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
  if (__builtin_expect(cgba_pending_sl_count != 0u, 0))
    cgba_flush_pending_scanlines();
#endif
  if (__builtin_expect(skip_next_frame != 0, 0))
    return;

  u32 pitch = get_screen_pitch();
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 vcount = read_ioreg(REG_VCOUNT);
  u16 *screen_base = get_screen_pixels();
  u16 *screen_offset = screen_base + (vcount * pitch);
  u32 video_mode = dispcnt & 0x07;

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  if (__builtin_expect(screen_base != cgba_sl_last_pixels ||
                       pitch != cgba_sl_last_pitch, 0)) {
    video_invalidate_scanline_cache(0, 160);
    cgba_sl_last_pixels = screen_base;
    cgba_sl_last_pitch = pitch;
  }

  bool row_dirty;
  if (__builtin_expect(vcount == 0u, 0)) {
    row_dirty = cgba_sl_begin_frame(video_mode, dispcnt);
  } else {
    /* Fast path for rows 1..159: single branch when no mid-frame write happened. */
    if (__builtin_expect((cgba_video_io_dirty | cgba_vram_dirty |
                          (palette_ram_dirty & CGBA_PALETTE_DIRTY_SCANLINE) |
                          reg[OAM_UPDATED]) != 0, 0)) {
      cgba_sl_midframe_dirty(video_mode, dispcnt);
    }
    if (__builtin_expect(cgba_sl_frame_bg_dirty, 1))
      row_dirty = true;
    else if (vcount < 160)
      row_dirty = !cgba_sl_row_valid[vcount] ||
                  (cgba_sl_frame_obj_dirty && cgba_sl_row_has_obj[vcount]);
    else
      row_dirty = true;
  }

  if (row_dirty) {
    cgba_video_frame_dirty_rows++;

    // If the screen is in forced blank draw pure white.
    if (__builtin_expect((dispcnt & 0x80) != 0, 0))
      memset(screen_offset, 0xff, 240*sizeof(u16));
#ifndef CGBA_VIDEO_TILE_ROW_CACHE
    else if (__builtin_expect(video_mode == 0u, 1)) {
      if (__builtin_expect(!cgba_bg_plan_valid, 0)) {
        cgba_rebuild_mode0_bg_plans(dispcnt);
        cgba_bg_plan_valid = true;
      }
      u32 omask = cgba_mode0_obj_enabled ? (u32)(obj_row_prio_mask[vcount] & 0x0Fu) : 0u;
      if (__builtin_expect(
            cgba_mode0_can_fast(omask) &&
            !(cgba_mode0_obj_alpha_can_blend && obj_alpha_count[vcount]), 1)) {
        render_mode0_fullcolor_scanline(
          vcount, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      } else {
        order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
        render_scanline_window(screen_offset);
      }
    }
#endif
    else {
      order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
      render_scanline_window(screen_offset);
    }

    if (__builtin_expect(vcount < 160u, 1)) {
      bool obj_en = !(dispcnt & 0x80) &&
                    (((dispcnt & 0x1000) != 0) || ((dispcnt & 0x8000) != 0));
      cgba_sl_row_has_obj[vcount] =
        (obj_en && obj_row_prio_mask[vcount] != 0) ? 1 : 0;
      cgba_sl_row_valid[vcount] = cgba_sl_cur_uniform ? 1 : 0;
    }
  }
  if (__builtin_expect(vcount == 159u, 0))
    cgba_sl_prev_uniform = cgba_sl_cur_uniform;
#else
  // If OAM has been modified since the last scanline has been updated then
  // reorder and reprofile the OBJ lists.
  if(reg[OAM_UPDATED])
  {
    order_obj(video_mode);
    reg[OAM_UPDATED] = 0;
  }

  // If the screen is in in forced blank draw pure white.
  if(dispcnt & 0x80)
    memset(screen_offset, 0xff, 240*sizeof(u16));
#ifndef CGBA_VIDEO_TILE_ROW_CACHE
  else if (video_mode == 0u) {
    cgba_rebuild_mode0_bg_plans(dispcnt);
    u32 omask = cgba_mode0_obj_enabled ? (u32)(obj_row_prio_mask[vcount] & 0x0Fu) : 0u;
    if (cgba_mode0_can_fast(omask) &&
        !(cgba_mode0_obj_alpha_can_blend && obj_alpha_count[vcount])) {
      render_mode0_fullcolor_scanline(
        vcount, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
    } else {
      order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
      render_scanline_window(screen_offset);
    }
  }
#endif
  else {
    order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
    render_scanline_window(screen_offset);
  }
#endif

#ifdef CGBA_LCD_SCANLINE_STREAM
  /* The renderer has finished every write to this row. In unscaled streaming
   * mode the platform batches it into a DMA-safe 16-row YRAM strip. */
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  fxcg100_lcd_stream_scanline(screen_offset, vcount, row_dirty ? 1 : 0);
#else
  fxcg100_lcd_stream_scanline(screen_offset, vcount, 1);
#endif
#endif

  // Mode 0 does not use any affine params at all.
  if (__builtin_expect(video_mode != 0u, 0))
    cgba_update_affine_refs(vcount);
}
