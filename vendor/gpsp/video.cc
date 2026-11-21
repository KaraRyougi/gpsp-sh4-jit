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

#ifdef CGBA_SH4_ASM_4BPP
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
void cgba_render_4bpp_strip_base_u16_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u32 *pair_lut, const u16 *map2);
void cgba_render_4bpp_strip_over_u16_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u32 *pair_lut, const u16 *map2);
void cgba_render_4bpp_part_strip_sh4(
  u16 *dst_top, u32 k_start_w, u32 raw_map_isbase,
  const u8 *tile_charbase, u32 start_r, const u16 *paltbl);
void cgba_render_4bpp_strip_blend_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_k_evb, const u32 *term_a, const u16 *map2);
void cgba_render_4bpp_strip_blend_sat_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_k_evb, const u32 *term_a, const u16 *map2);
void cgba_sparse_override_band_sh4(const void *ctx);
void cgba_blend_solid_rect_sh4(
  u16 *dst_top, u32 wh_packed, u32 term_a_val, u32 evb);
void cgba_render_obj_4bpp_tiles_sh4(
  const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr, const u16 *subpal);
void cgba_render_obj_4bpp_tiles_hf_sh4(
  const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr, const u16 *subpal);
void cgba_render_obj_4bpp_tiles_strip_sh4(
  const u8 *tile_ptr, u32 k_and_ntiles, u16 *dst_top, const u16 *subpal,
  s32 row_step);
void cgba_render_obj_4bpp_tiles_strip_hf_sh4(
  const u8 *tile_ptr, u32 k_and_ntiles, u16 *dst_top, const u16 *subpal,
  s32 row_step);
struct cgba_obj_aff_scale_strip_params {
  u16 *dst_ptr;
  const u8 *vram_tx;
  const u16 *subpal;
  s32 sx0;
  s32 dx;
  s32 sy0;
  s32 dmy;
  u32 span_cnt;
  u32 nrows;
  u32 ntiles;
  u32 tile_pitch_sh;
};
void cgba_render_obj_4bpp_affine_scale_strip_sh4(
  const cgba_obj_aff_scale_strip_params *p);
int cgba_mode0_merge2_seg_sh4(
  u16 *out, const u16 *s0, const u16 *s1, u32 n, const u8 *cls_tbl);
int cgba_mode0_merge3_seg_sh4(
  u16 *out, const u16 *s0, const u16 *s1, const u16 *s2, u32 n, const u8 *cls_tbl);
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
struct cgba_aff_bg_params {
  void *dst_ptr;
  const u8 *map_base;
  const u8 *tile_base;
  const u16 *pal;
  s32 source_x;
  s32 source_y;
  s32 dx;
  s32 dy;
  u32 cnt;
  u32 width_height;
  u32 map_pitch;
  u32 flags;          /* bit0 = isbase, bit1 = wrap, bits16..31 = bg_comb */
};
void cgba_render_affine_bg_scale_sh4(const cgba_aff_bg_params *p);
void cgba_render_affine_bg_rot_u32_sh4(const cgba_aff_bg_params *p);
void cgba_render_obj_4bpp_tiles_blend_sh4(
  const u8 *tile_ptr, u32 num_tiles, u16 *dst_ptr,
  const u32 *sub_term_a, u32 evb_and_flags);
void cgba_merge_brightness_sh4(
  u16 *srcdst, u32 count, const u16 *pal_conv, u32 pa, u32 inv_evy);
struct cgba_merge_blend_params {
  u32 blend_a;
  u32 blend_b;
  u32 top_blend_bits;
  u32 sat_mask;
  u32 bright_mask;
  u32 bright_pa;
  u32 bright_inv;
};
void cgba_merge_blend_sh4(
  u16 *dst, const u32 *src, u32 count, const u16 *pal_conv,
  const cgba_merge_blend_params *p);
void cgba_render_8bpp_strip_base_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u16 *pal256, const u16 *map2);
void cgba_render_8bpp_strip_over_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 start_r_and_k, const u16 *pal256, const u16 *map2);
void cgba_render_8bpp_strip_blend_sh4(
  const u16 *map1, u32 counts, u16 *dst_top, const u8 *tile_charbase,
  u32 rke_sat, const u32 *term_a256, const u16 *map2, const u16 *bd_pal256);
struct cgba_aff_rot_bg_params {
  u16 *dst_end;
  const u8 *map_base;
  const u8 *tile_base;
  const u16 *pal;
  u32 ux;
  u32 uy;
  u32 step_x;
  u32 step_y;
  u32 cnt;
  s32 pix_shift;
  s32 row_shift;
  u32 use_same_texel;
  const u8 *tile_kind;
};
struct cgba_aff_addc_params {
  u16 *dst_end;
  const u8 *map_base;
  const u8 *tile_base;
  const u16 *pal;
  u32 f_x;
  u32 f_y;
  u32 step_fx;
  u32 step_fy;
  u32 cnt;
  s32 pix_x;
  s32 pix_y_sh;
  s32 step_x;
  s32 step_y_sh;
  u32 mask_x;
  u32 mask_y_sh;
  s32 row_shift;
};
void cgba_render_affine_rot_base_sh4(const cgba_aff_rot_bg_params *p);
void cgba_render_affine_rot_base_addc_sh4(const cgba_aff_addc_params *p, u32 mode);
void cgba_render_affine_rot_base_p2_sh4(const cgba_aff_addc_params *p);
void cgba_render_affine_rot_over_sh4(const cgba_aff_rot_bg_params *p);
void cgba_render_4bpp_span_blend_evb16_sh4(
  const u16 *map_ptr, u32 todraw, u16 *dest_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *term_a_s4);
s32 cgba_unpack_4bpp_nibs_sh4(
  const u16 *map_ptr, u32 todraw, u8 *dst_nibs,
  const u8 *tile_base, s32 vflip_off, const u16 *unpack_lut, s32 expected_subpal);
struct cgba_fused_blend_params {
  const u16 *map_ptr;
  const u8 *base_nibs;
  u16 *dest_ptr;
  const u16 *lut;
  const u8 *tile_base;
  s32 vflip_off;
  u32 todraw;
  u32 expected_pal;
};
u32 cgba_render_4bpp_span_fused_blend_sh4(const cgba_fused_blend_params *p);
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

static inline bool cgba_memeq_u32(const void *a, const void *b, u32 nwords)
{
  const u32 *pa = (const u32 *)a;
  const u32 *pb = (const u32 *)b;
  for (u32 i = 0u; i < nwords; i++) {
    if (pa[i] != pb[i])
      return false;
  }
  return true;
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
      u32 *s32 = (u32 *)shadow;
      const u32 *l32 = (const u32 *)live;
      bool diff = (shadow[0] != backdrop) || (shadow[1] != live[1]);
      if (!diff) {
        u32 d = (s32[1] ^ l32[1]) | (s32[2] ^ l32[2]) | (s32[3] ^ l32[3]) |
                (s32[4] ^ l32[4]) | (s32[5] ^ l32[5]) | (s32[6] ^ l32[6]) |
                (s32[7] ^ l32[7]);
        diff = (d != 0u);
      }
      if (diff) {
        shadow[0] = backdrop;
        shadow[1] = live[1];
        s32[1] = l32[1]; s32[2] = l32[2]; s32[3] = l32[3];
        s32[4] = l32[4]; s32[5] = l32[5]; s32[6] = l32[6];
        s32[7] = l32[7];
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
#ifdef CGBA_VIDEO_OPAQUE_ROW_UNROLL
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
#ifdef CGBA_VIDEO_OPAQUE_ROW_UNROLL
      /* A 4-bpp color index of zero is the only transparent value.  Hoist the
       * eight per-pixel transparency branches when every nibble is nonzero.
       * The subtraction is lane-wise in base 16 whenever all nibbles are at
       * least one; if any lane is zero, its high bit (and possibly later
       * borrow-affected lanes) is caught by the mask. */
      if (cgba_4bpp_row_is_opaque(tilepix)) {
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


/* Full-tile run of one text-background scanline span.
 *
 * CGBA_VIDEO_EMPTY_RUN_SKIP: an overlay (non-base) 4-bpp tile row whose packed
 * word is zero draws nothing, and any following map entry with the same raw
 * value names the same tile, flips and row, so it draws nothing either.
 * Compare raw map entries to step over such runs instead of decoding each
 * tile again.  VRAM is read on every scanline, so there is no state to
 * invalidate. */
template<typename stype, rendtype rdtype, bool is8bpp, bool isbase>
static inline __attribute__((always_inline)) void render_text_tile_run(
  u32 todraw, u16 *&map_ptr, stype *&dest_ptr, u32 bg_comb, u32 px_comb,
  const u8 *tile_base, s32 vflip_off, const u16 *paltbl)
{
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  if (sizeof(stype) == sizeof(u16) && rdtype == FULLCOLOR && !is8bpp) {
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
  if (!isbase && !is8bpp) {
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
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
    else
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
  }
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
/* Lazy 1-byte per-tile opacity classification for all 2,048 4-bpp tiles in
 * BG VRAM [0x0000..0x10000), plus a 512-entry sentinel tail [2048..2559] for
 * charblock-3 tile IDs >= 512 (2,560 B in high BSS, zero bounds check on hot
 * lookups).  Tile opacity/emptiness is invariant under HFLIP, VFLIP, and
 * palette bank, so each entry depends only on the physical VRAM tile index.
 * Invalidated at 2 KiB granularity (64 tiles = 64 bytes per dirty bit) via
 * cgba_vram_sb_dirty. */
enum {
  CGBA_TILE4_UNKNOWN = 0u,
  CGBA_TILE4_EMPTY   = 1u,
  CGBA_TILE4_OPAQUE  = 2u,
  CGBA_TILE4_MIXED   = 3u
};
static u8 cgba_vram_tile4_class[2560] CGBA_HIGH_BSS;

static __attribute__((noinline)) u32 cgba_tile4_classify_slow(u32 vtile)
{
  if (__builtin_expect(vtile >= 2048u, 0)) {
    if (vtile < 2560u)
      cgba_vram_tile4_class[vtile] = CGBA_TILE4_MIXED;
    return CGBA_TILE4_MIXED;
  }
  const u32 *tw = (const u32 *)(const void *)(vram + (vtile << 5));
  u32 any_nz = 0u;
  u32 any_zero_nib = 0u;
  for (u32 r = 0u; r < 8u; r++) {
    u32 w = tw[r];
    any_nz |= w;
    any_zero_nib |= ((w + 0xEEEEEEEFu) & ~w) & 0x88888888u;
  }
  u32 cls = (any_nz == 0u) ? CGBA_TILE4_EMPTY
                           : ((any_zero_nib == 0u) ? CGBA_TILE4_OPAQUE
                                                   : CGBA_TILE4_MIXED);
  cgba_vram_tile4_class[vtile] = (u8)cls;
  return cls;
}

static inline u32 cgba_tile4_classify(u32 cbb_tile_base, u32 tid)
{
  u32 vtile = cbb_tile_base + tid;
  u32 cls = cgba_vram_tile4_class[vtile];
  return __builtin_expect(cls != 0u, 1) ? cls : cgba_tile4_classify_slow(vtile);
}

static inline void cgba_tile4_invalidate_all(void)
{
  memset(cgba_vram_tile4_class, 0, 2048u);
}

static inline void cgba_tile4_invalidate_sbd(u32 sbd)
{
  if (sbd == 0u) {
    cgba_tile4_invalidate_all();
    return;
  }
  while (sbd) {
    u32 b = (u32)__builtin_ctz(sbd);
    sbd &= sbd - 1u;
    memset(cgba_vram_tile4_class + (b << 6), 0, 64u);
  }
}

/* Per-screenblock 32-bit row-occupancy and uniform-tile cache, invalidated on
 * BG VRAM writes via cgba_vram_dirty / cgba_vram_sb_dirty.  Detects both
 * tile-0 empty rows and rows filled uniformly with any non-zero blank tile
 * (e.g. Zelda ALttP HUD tile 0x007F or dungeon blank tile 0x01EC). */
static struct {
  u32 valid_mask;
  u32 row_nonzero[32];
  u32 row_uniform_mask[32];
  u32 opaque_valid[4];
  u32 row_opaque[4][32];
} cgba_sb;
static u16 cgba_sb_row_uniform_tid[32][32] CGBA_HIGH_BSS;
#define cgba_sb_valid_mask (cgba_sb.valid_mask)
#define cgba_sb_row_nonzero (cgba_sb.row_nonzero)

static __attribute__((noinline)) u32 cgba_sb_scan_opaque_rows(u32 cbb, u32 sb)
{
  const u16 *m = (const u16 *)&vram[sb * 2048u];
  const u32 cbb_base = cbb << 9;
  u32 mask = 0u;
  for (u32 row = 0u; row < 32u; row++) {
    bool all_op = true;
    for (u32 col = 0u; col < 32u; col++) {
      u32 tid = eswap16(m[col]) & 0x3FFu;
      if (cgba_tile4_classify(cbb_base, tid) != CGBA_TILE4_OPAQUE) {
        all_op = false;
        break;
      }
    }
    if (all_op)
      mask |= (1u << row);
    m += 32;
  }
  cgba_sb.row_opaque[cbb][sb] = mask;
  cgba_sb.opaque_valid[cbb] |= (1u << sb);
  return mask;
}

static inline bool cgba_sb_row_is_all_opaque(u32 cbb, u32 sb, u32 row_bit)
{
  if (sb >= 32u || cbb >= 4u)
    return false;
  u32 mask = (cgba_sb.opaque_valid[cbb] & (1u << sb))
               ? cgba_sb.row_opaque[cbb][sb]
               : cgba_sb_scan_opaque_rows(cbb, sb);
  return (mask & row_bit) != 0u;
}

static __attribute__((noinline)) u32 cgba_sb_scan_rows(u32 sb)
{
#ifdef MSB_FIRST
  const u32 id_vflip_mask = 0xFF0BFF0Bu;
  const u32 id_mask       = 0xFF03FF03u;
#else
  const u32 id_vflip_mask = 0x0BFF0BFFu;
  const u32 id_mask       = 0x03FF03FFu;
#endif
  const u32 *p = (const u32 *)&vram[sb * 2048u];
  u16 *utid = cgba_sb_row_uniform_tid[sb];
  u32 mask = 0u;
  u32 umask = 0u;
  for (u32 row = 0u; row < 32u; row++) {
    u32 w0 = p[0];
    u32 w7 = p[7];
    u32 lo = (w0 & id_mask) & 0xFFFFu;
    u32 pair = lo | (lo << 16);
    u32 udiff = ((w0 ^ pair) | (w7 ^ pair)) & id_mask;
    if (udiff == 0u) {
      udiff = (((p[1] ^ pair) | (p[2] ^ pair) | (p[3] ^ pair) | (p[4] ^ pair) |
                (p[5] ^ pair) | (p[6] ^ pair) | (p[8] ^ pair) | (p[9] ^ pair) |
                (p[10] ^ pair) | (p[11] ^ pair) | (p[12] ^ pair) | (p[13] ^ pair) |
                (p[14] ^ pair) | (p[15] ^ pair)) & id_mask);
      if (udiff == 0u) {
        umask |= (1u << row);
        utid[row] = (u16)(eswap16((u16)lo) & 0x3FFu);
      }
    }
    if (((w0 | w7) & id_vflip_mask) != 0u ||
        ((p[1] | p[2] | p[3] | p[4] | p[5] | p[6] |
          p[8] | p[9] | p[10] | p[11] | p[12] | p[13] | p[14] | p[15]) &
         id_vflip_mask) != 0u)
      mask |= (1u << row);
    p += 16;
  }
  cgba_sb_row_nonzero[sb] = mask;
  cgba_sb.row_uniform_mask[sb] = umask;
  cgba_sb_valid_mask |= (1u << sb);
  return mask;
}

static inline u32 cgba_sb_compute_empty_rows(
  u32 sb, u32 cbb_tile_base, u32 *out_cbb_dirty_mask)
{
  if (sb >= 32u)
    return 0u;
  if (!(cgba_sb_valid_mask & (1u << sb)))
    (void)cgba_sb_scan_rows(sb);
  u32 um = cgba_sb.row_uniform_mask[sb];
  if (um == 0u)
    return 0u;
  const u16 *utid = cgba_sb_row_uniform_tid[sb];
  u32 empty_mask = 0u;
  u32 last_tid = 0xFFFFu;
  bool last_empty = false;
  while (um) {
    u32 r = (u32)__builtin_ctz(um);
    um &= um - 1u;
    u32 tid = utid[r];
    if (tid != last_tid) {
      last_tid = tid;
      last_empty = (cgba_tile4_classify(cbb_tile_base, tid) == CGBA_TILE4_EMPTY);
    }
    if (last_empty) {
      empty_mask |= (1u << r);
      *out_cbb_dirty_mask |= (1u << ((cbb_tile_base + tid) >> 6));
    }
  }
  return empty_mask;
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

static inline u32 cgba_tile4_classify_raw_tbl(const u8 *cls_tbl, u16 raw)
{
  u32 tid = (u32)(eswap16(raw) & 0x3FFu);
  u32 cls = cls_tbl[tid];
  return __builtin_expect(cls != 0u, 1)
           ? cls
           : cgba_tile4_classify_slow((u32)(cls_tbl - cgba_vram_tile4_class) + tid);
}

/* Merge 2 aligned 4bpp BG map rows (bottom am*[0], top am*[1]) for a single
 * tile band into out[0..nslots-1].  Both layers must share tile_charbase,
 * tile_hoff, and (vofs & 7).  Returns true when every slot reduces to a single
 * tile; false if any slot has a mixed top tile over a non-empty bottom tile. */
static __attribute__((noinline, unused)) bool cgba_mode0_try_merge_2(
  u16 *out, u32 nslots, u32 cbb_tile_base,
  const u16 *const *am1, const u16 *const *am2, const u32 *aseg1)
{
  const u8 *const cls_tbl = cgba_vram_tile4_class + cbb_tile_base;
  u32 seg1_0 = aseg1[0];
  u32 seg1_1 = aseg1[1];
  const u16 *m0_1 = am1[0], *m0_2 = am2[0];
  const u16 *m1_1 = am1[1], *m1_2 = am2[1];
  if (__builtin_expect(seg1_0 == seg1_1, 1)) {
#ifdef CGBA_SH4_ASM_4BPP
    if (__builtin_expect(
          cgba_mode0_merge2_seg_sh4(out, m0_1, m1_1, seg1_0, cls_tbl) &&
          cgba_mode0_merge2_seg_sh4(out + seg1_0, m0_2, m1_2,
                                    nslots - seg1_0, cls_tbl), 1))
      return true;
#endif
    const u16 *s0 = m0_1;
    const u16 *s1 = m1_1;
    u32 n = seg1_0;
    for (u32 pass = 0u; pass < 2u; pass++) {
      const u16 *end = s1 + n;
      while (s1 < end) {
        u16 r1 = *s1++;
        u16 r0 = *s0++;
        u32 c1 = cgba_tile4_classify_raw_tbl(cls_tbl, r1);
        if (c1 == CGBA_TILE4_OPAQUE) {
          *out++ = r1;
        } else if (c1 == CGBA_TILE4_EMPTY) {
          *out++ = r0;
        } else {
          if (cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
            return false;
          *out++ = r1;
        }
      }
      s0 = m0_2;
      s1 = m1_2;
      n = nslots - seg1_0;
    }
    return true;
  }

  for (u32 i = 0u; i < nslots; i++) {
    u16 r1 = (i < seg1_1) ? m1_1[i] : m1_2[i - seg1_1];
    u32 c1 = cgba_tile4_classify_raw_tbl(cls_tbl, r1);
    if (c1 == CGBA_TILE4_OPAQUE) {
      out[i] = r1;
    } else if (c1 == CGBA_TILE4_EMPTY) {
      out[i] = (i < seg1_0) ? m0_1[i] : m0_2[i - seg1_0];
    } else {
      u16 r0 = (i < seg1_0) ? m0_1[i] : m0_2[i - seg1_0];
      if (cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
        return false;
      out[i] = r1;
    }
  }
  return true;
}

/* Merge 3 aligned 4bpp BG map rows (bottom am*[0], mid am*[1], top am*[2]) for
 * a single tile band into out[0..nslots-1].  Returns true when every slot
 * reduces to a single tile across all 3 layers. */
static __attribute__((noinline, unused)) bool cgba_mode0_try_merge_3(
  u16 *out, u32 nslots, u32 cbb_tile_base,
  const u16 *const *am1, const u16 *const *am2, const u32 *aseg1)
{
  const u8 *const cls_tbl = cgba_vram_tile4_class + cbb_tile_base;
  u32 seg1_0 = aseg1[0];
  u32 seg1_1 = aseg1[1];
  u32 seg1_2 = aseg1[2];
  const u16 *m0_1 = am1[0], *m0_2 = am2[0];
  const u16 *m1_1 = am1[1], *m1_2 = am2[1];
  const u16 *m2_1 = am1[2], *m2_2 = am2[2];
  if (__builtin_expect(seg1_0 == seg1_1 && seg1_0 == seg1_2, 1)) {
#ifdef CGBA_SH4_ASM_4BPP
    if (__builtin_expect(
          cgba_mode0_merge3_seg_sh4(out, m0_1, m1_1, m2_1, seg1_0, cls_tbl) &&
          cgba_mode0_merge3_seg_sh4(out + seg1_0, m0_2, m1_2, m2_2,
                                    nslots - seg1_0, cls_tbl), 1))
      return true;
#endif
    const u16 *s0 = m0_1;
    const u16 *s1 = m1_1;
    const u16 *s2 = m2_1;
    u32 n = seg1_0;
    for (u32 pass = 0u; pass < 2u; pass++) {
      const u16 *end = s2 + n;
      while (s2 < end) {
        u16 r2 = *s2++;
        u16 r1 = *s1++;
        u16 r0 = *s0++;
        u32 c2 = cgba_tile4_classify_raw_tbl(cls_tbl, r2);
        if (c2 == CGBA_TILE4_OPAQUE) {
          *out++ = r2;
          continue;
        }
        u32 c1 = cgba_tile4_classify_raw_tbl(cls_tbl, r1);
        if (c2 == CGBA_TILE4_EMPTY) {
          if (c1 == CGBA_TILE4_OPAQUE) {
            *out++ = r1;
          } else if (c1 == CGBA_TILE4_EMPTY) {
            *out++ = r0;
          } else {
            if (cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
              return false;
            *out++ = r1;
          }
        } else {
          if (c1 != CGBA_TILE4_EMPTY ||
              cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
            return false;
          *out++ = r2;
        }
      }
      s0 = m0_2;
      s1 = m1_2;
      s2 = m2_2;
      n = nslots - seg1_0;
    }
    return true;
  }

  for (u32 i = 0u; i < nslots; i++) {
    u16 r2 = (i < seg1_2) ? m2_1[i] : m2_2[i - seg1_2];
    u32 c2 = cgba_tile4_classify_raw_tbl(cls_tbl, r2);
    if (c2 == CGBA_TILE4_OPAQUE) {
      out[i] = r2;
      continue;
    }
    u16 r1 = (i < seg1_1) ? m1_1[i] : m1_2[i - seg1_1];
    u32 c1 = cgba_tile4_classify_raw_tbl(cls_tbl, r1);
    if (c2 == CGBA_TILE4_EMPTY) {
      if (c1 == CGBA_TILE4_OPAQUE) {
        out[i] = r1;
      } else if (c1 == CGBA_TILE4_EMPTY) {
        out[i] = (i < seg1_0) ? m0_1[i] : m0_2[i - seg1_0];
      } else {
        u16 r0 = (i < seg1_0) ? m0_1[i] : m0_2[i - seg1_0];
        if (cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
          return false;
        out[i] = r1;
      }
    } else {
      u16 r0 = (i < seg1_0) ? m0_1[i] : m0_2[i - seg1_0];
      if (c1 != CGBA_TILE4_EMPTY ||
          cgba_tile4_classify_raw_tbl(cls_tbl, r0) != CGBA_TILE4_EMPTY)
        return false;
      out[i] = r2;
    }
  }
  return true;
}
#endif

template<typename stype, rendtype rdtype, bool isbase, bool is8bpp>
static void render_scanline_text_fast(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
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

  render_text_tile_run<stype, rdtype, is8bpp, isbase>(
    todraw, map_ptr, dest_ptr, bg_comb, px_comb, tile_base, vflip_off, paltbl);

  end -= todraw * 8;
  pixel_run -= todraw * 8;

  if (!end)
    return;

  // Switch to the next char block if we ran out of tiles
  if (!pixel_run)
    map_ptr = second_ptr;

  todraw = end / 8;
  render_text_tile_run<stype, rdtype, is8bpp, isbase>(
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

/* Precomputed 32-bit unsigned reciprocals: ceil(2^32 / d) for d=2..255,
 * 0xFFFFFFFF for d=1 (conservative floor-1 for rem>=1, exact 0 for rem==0),
 * 0 for d=0.  Exact ((u64)u * recip32[d]) >> 32 == u / d for d=2..255, u<65536. */
static const u32 cgba_recip32[256] = {
  0x00000000u, 0xFFFFFFFFu, 0x80000000u, 0x55555556u,
  0x40000000u, 0x33333334u, 0x2AAAAAABu, 0x24924925u,
  0x20000000u, 0x1C71C71Du, 0x1999999Au, 0x1745D175u,
  0x15555556u, 0x13B13B14u, 0x12492493u, 0x11111112u,
  0x10000000u, 0x0F0F0F10u, 0x0E38E38Fu, 0x0D79435Fu,
  0x0CCCCCCDu, 0x0C30C30Du, 0x0BA2E8BBu, 0x0B21642Du,
  0x0AAAAAABu, 0x0A3D70A4u, 0x09D89D8Au, 0x097B425Fu,
  0x0924924Au, 0x08D3DCB1u, 0x08888889u, 0x08421085u,
  0x08000000u, 0x07C1F07Du, 0x07878788u, 0x07507508u,
  0x071C71C8u, 0x06EB3E46u, 0x06BCA1B0u, 0x06906907u,
  0x06666667u, 0x063E7064u, 0x06186187u, 0x05F417D1u,
  0x05D1745Eu, 0x05B05B06u, 0x0590B217u, 0x0572620Bu,
  0x05555556u, 0x0539782Au, 0x051EB852u, 0x05050506u,
  0x04EC4EC5u, 0x04D4873Fu, 0x04BDA130u, 0x04A7904Bu,
  0x04924925u, 0x047DC120u, 0x0469EE59u, 0x0456C798u,
  0x04444445u, 0x04325C54u, 0x04210843u, 0x04104105u,
  0x04000000u, 0x03F03F04u, 0x03E0F83Fu, 0x03D22636u,
  0x03C3C3C4u, 0x03B5CC0Fu, 0x03A83A84u, 0x039B0AD2u,
  0x038E38E4u, 0x0381C0E1u, 0x03759F23u, 0x0369D037u,
  0x035E50D8u, 0x03531DEDu, 0x03483484u, 0x033D91D3u,
  0x03333334u, 0x03291620u, 0x031F3832u, 0x03159722u,
  0x030C30C4u, 0x03030304u, 0x02FA0BE9u, 0x02F14991u,
  0x02E8BA2Fu, 0x02E05C0Cu, 0x02D82D83u, 0x02D02D03u,
  0x02C8590Cu, 0x02C0B02Du, 0x02B93106u, 0x02B1DA47u,
  0x02AAAAABu, 0x02A3A0FEu, 0x029CBC15u, 0x0295FAD5u,
  0x028F5C29u, 0x0288DF0Du, 0x02828283u, 0x027C4598u,
  0x02762763u, 0x02702703u, 0x026A43A0u, 0x02647C6Au,
  0x025ED098u, 0x02593F6Au, 0x0253C826u, 0x024E6A18u,
  0x02492493u, 0x0243F6F1u, 0x023EE090u, 0x0239E0D6u,
  0x0234F72Du, 0x02302303u, 0x022B63CCu, 0x0226B903u,
  0x02222223u, 0x021D9EAEu, 0x02192E2Au, 0x0214D022u,
  0x02108422u, 0x020C49BBu, 0x02082083u, 0x02040811u,
  0x02000000u, 0x01FC07F1u, 0x01F81F82u, 0x01F4465Au,
  0x01F07C20u, 0x01ECC07Cu, 0x01E9131Bu, 0x01E573ADu,
  0x01E1E1E2u, 0x01DE5D6Fu, 0x01DAE608u, 0x01D77B66u,
  0x01D41D42u, 0x01D0CB59u, 0x01CD8569u, 0x01CA4B31u,
  0x01C71C72u, 0x01C3F8F1u, 0x01C0E071u, 0x01BDD2B9u,
  0x01BACF92u, 0x01B7D6C4u, 0x01B4E81Cu, 0x01B20365u,
  0x01AF286Cu, 0x01AC5702u, 0x01A98EF7u, 0x01A6D01Bu,
  0x01A41A42u, 0x01A16D40u, 0x019EC8EAu, 0x019C2D15u,
  0x0199999Au, 0x01970E50u, 0x01948B10u, 0x01920FB5u,
  0x018F9C19u, 0x018D3019u, 0x018ACB91u, 0x01886E60u,
  0x01861862u, 0x0183C978u, 0x01818182u, 0x017F4060u,
  0x017D05F5u, 0x017AD221u, 0x0178A4C9u, 0x01767DCFu,
  0x01745D18u, 0x01724288u, 0x01702E06u, 0x016E1F77u,
  0x016C16C2u, 0x016A13CEu, 0x01681682u, 0x01661EC7u,
  0x01642C86u, 0x01623FA8u, 0x01605817u, 0x015E75BCu,
  0x015C9883u, 0x015AC057u, 0x0158ED24u, 0x01571ED4u,
  0x01555556u, 0x01539095u, 0x0151D07Fu, 0x01501502u,
  0x014E5E0Bu, 0x014CAB89u, 0x014AFD6Bu, 0x0149539Fu,
  0x0147AE15u, 0x01460CBDu, 0x01446F87u, 0x0142D663u,
  0x01414142u, 0x013FB014u, 0x013E22CCu, 0x013C995Bu,
  0x013B13B2u, 0x013991C3u, 0x01381382u, 0x013698E0u,
  0x013521D0u, 0x0133AE46u, 0x01323E35u, 0x0130D191u,
  0x012F684Cu, 0x012E025Du, 0x012C9FB5u, 0x012B404Bu,
  0x0129E413u, 0x01288B02u, 0x0127350Cu, 0x0125E228u,
  0x0124924Au, 0x01234568u, 0x0121FB79u, 0x0120B471u,
  0x011F7048u, 0x011E2EF4u, 0x011CF06Bu, 0x011BB4A5u,
  0x011A7B97u, 0x01194539u, 0x01181182u, 0x0116E069u,
  0x0115B1E6u, 0x011485F1u, 0x01135C82u, 0x0112358Fu,
  0x01111112u, 0x010FEF02u, 0x010ECF57u, 0x010DB20Bu,
  0x010C9715u, 0x010B7E6Fu, 0x010A6811u, 0x010953F4u,
  0x01084211u, 0x01073261u, 0x010624DEu, 0x01051980u,
  0x01041042u, 0x0103091Cu, 0x01020409u, 0x01010102u,
};

static inline s32 cgba_fast_div_pos(s32 num, s32 d)
{
  u32 u = (u32)num;
  u32 ud = (u32)d;
  if (__builtin_expect((ud & (ud - 1u)) == 0u, 0))
    return (s32)(u >> __builtin_ctz(ud));
  if (__builtin_expect(u < 65536u && ud < 1024u, 1)) {
    if (ud < 256u)
      return (s32)((u32)(((u64)u * cgba_recip32[ud]) >> 32));
    if (ud < 512u) {
      u32 q = (u32)(((u64)u * cgba_recip32[ud >> 1]) >> 33);
      if (q * ud > u)
        q--;
      return (s32)q;
    }
    u32 q = (u32)(((u64)u * cgba_recip32[ud >> 2]) >> 34);
    if (q * ud > u)
      q--;
    return (s32)q;
  }
  return num / d;
}

// Affine background rendering logic.
// wrap extends the background infinitely, otherwise transparent/backdrop fill
// rotate indicates if there's any rotation (optimized version for no-rotation)
// mosaic applies to horizontal mosaic (vertical is adjusted via affine ref)
static inline void cgba_affine_clip_axis(s32 s, s32 d, s32 limit, s32 &lo, s32 &hi)
{
  if (d == 0) {
    if (s < 0 || s >= limit)
      hi = -1;
    return;
  }
  if (d > 0) {
    s32 s_lo = s + lo * d;
    if (s_lo >= limit) {
      hi = -1;
      return;
    }
    s32 s_hi = s + hi * d;
    if (s_hi < 0) {
      hi = -1;
      return;
    }
    if (s_lo < 0) {
      s32 i0 = cgba_fast_div_pos(-s + d - 1, d);
      if (i0 > lo)
        lo = i0;
    }
    if (s_hi >= limit) {
      s32 i1 = cgba_fast_div_pos(limit - 1 - s, d);
      if (i1 < hi)
        hi = i1;
    }
  } else {
    s32 ad = -d;
    s32 s_lo = s + lo * d;
    if (s_lo < 0) {
      hi = -1;
      return;
    }
    s32 s_hi = s + hi * d;
    if (s_hi >= limit) {
      hi = -1;
      return;
    }
    if (s_lo >= limit) {
      s32 i0 = cgba_fast_div_pos(s - limit + ad, ad);
      if (i0 > lo)
        lo = i0;
    }
    if (s_hi < 0) {
      s32 i1 = cgba_fast_div_pos(s, ad);
      if (i1 < hi)
        hi = i1;
    }
  }
}

#ifdef CGBA_SH4_ASM_4BPP
/* Per-charblock 8-bpp affine tile classification (256 B per CBB, invalidated
 * on BG VRAM writes to that charblock): 0 = all-zero, 1 = solid non-zero,
 * 2 = mixed.  Lets rotated affine kernels take a 12-insn fast loop on any
 * solid or empty 8x8 tile (not just tile 0). */
static u8 cgba_aff_tile_kind[4][256] __attribute__((aligned(32)));
static u8 cgba_aff_tile_valid = 0u;

static __attribute__((noinline)) const u8 *cgba_aff_tile_kind_scan(u32 cbb)
{
  u8 *out = cgba_aff_tile_kind[cbb];
  const u32 *t = (const u32 *)&vram[cbb * 16384u];
  for (u32 id = 0; id < 256u; id++, t += 16) {
    u32 w0 = t[0];
    u32 b0 = w0 & 0xFFu;
    u32 rep = b0 * 0x01010101u;
    u32 diff = (w0 ^ rep) | (t[1] ^ rep) | (t[2] ^ rep) | (t[3] ^ rep) |
               (t[4] ^ rep) | (t[5] ^ rep) | (t[6] ^ rep) | (t[7] ^ rep) |
               (t[8] ^ rep) | (t[9] ^ rep) | (t[10] ^ rep) | (t[11] ^ rep) |
               (t[12] ^ rep) | (t[13] ^ rep) | (t[14] ^ rep) | (t[15] ^ rep);
    out[id] = (diff != 0u) ? 2u : ((b0 == 0u) ? 0u : 1u);
  }
  cgba_aff_tile_valid |= (u8)(1u << cbb);
  return out;
}

static inline const u8 *cgba_aff_tile_kind_get(u32 cbb)
{
  if (__builtin_expect((cgba_aff_tile_valid & (1u << cbb)) != 0u, 1))
    return cgba_aff_tile_kind[cbb];
  return cgba_aff_tile_kind_scan(cbb);
}

/* Signed-indexed BG palette for SH-4 8-bpp affine kernels: entry [128 + b]
 * maps signed byte b in [-128..127] directly to palette_ram_converted[(u8)b],
 * eliminating extu.b on every texel load.  cgba_signed_bg_pal_dup mirrors the
 * same entries duplicated as (c<<16)|c for 32-bit mov.l pair stores. */
static u16 cgba_signed_bg_pal[256] __attribute__((aligned(32)));
static u32 cgba_signed_bg_pal_dup[256] __attribute__((aligned(32)));
static bool cgba_signed_bg_pal_valid = false;

static inline const u16 *cgba_signed_bg_pal_get(void)
{
#ifdef CGBA_PALETTE_DIRTY_ACTIVE
  if (__builtin_expect(cgba_signed_bg_pal_valid &&
                       !(palette_ram_dirty & CGBA_PALETTE_DIRTY_SIGNED), 1))
    return cgba_signed_bg_pal + 128;
  palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_SIGNED;
#endif
  for (u32 i = 0u; i < 128u; i++) {
    u32 hi = palette_ram_converted[i + 128u];
    u32 lo = palette_ram_converted[i];
    cgba_signed_bg_pal[i] = (u16)hi;
    cgba_signed_bg_pal[i + 128u] = (u16)lo;
    cgba_signed_bg_pal_dup[i] = (hi << 16) | hi;
    cgba_signed_bg_pal_dup[i + 128u] = (lo << 16) | lo;
  }
  cgba_signed_bg_pal_valid = true;
  return cgba_signed_bg_pal + 128;
}

static u32 cgba_fpal_dup_data[4][256] __attribute__((aligned(32)));
static const u16 *cgba_fpal_dup_src[4];
static u32 cgba_fpal_dup_ver[4];
static u32 cgba_fpal_dup_rr = 0u;

static __attribute__((noinline)) const u32 *cgba_fpal_dup_get(
  const u16 *spal, u32 ver)
{
  for (u32 i = 0u; i < 4u; i++) {
    if (cgba_fpal_dup_src[i] == spal && cgba_fpal_dup_ver[i] == ver)
      return cgba_fpal_dup_data[i] + 128;
  }
  u32 slot = cgba_fpal_dup_rr++ & 3u;
  u32 *dst = cgba_fpal_dup_data[slot];
  const u16 *src = spal - 128;
  for (u32 i = 0u; i < 256u; i++) {
    u32 c = src[i];
    dst[i] = (c << 16) | c;
  }
  cgba_fpal_dup_src[slot] = spal;
  cgba_fpal_dup_ver[slot] = ver;
  return dst + 128;
}

static __attribute__((noinline)) void cgba_dispatch_affine_rot_base_sh4(
  u16 *dst_end, const u8 *map_base, const u8 *tile_base, const u16 *pal,
  s32 x0, s32 y0, s32 dx, s32 dy, u32 cnt, u32 map_size,
  bool pal_has_signed_prefix = false,
  const u16 *cached_spal = NULL,
  const u32 *cached_dpal = NULL,
  u32 fpal_dup_ver = 0u)
{
  const s32 sdx = -dx;
  const s32 sdy = -dy;
  const u32 adx = (u32)(sdx < 0 ? -sdx : sdx);
  const u32 ady = (u32)(sdy < 0 ? -sdy : sdy);
  if (__builtin_expect(adx < 2048u && ady < 2048u, 1)) {
    const u32 t_mask = (16u << map_size) - 1u;
    cgba_aff_addc_params ap;
    ap.dst_end = dst_end;
    ap.map_base = map_base;
    ap.tile_base = tile_base;
    ap.cnt = cnt;
    ap.pix_x = (s32)(((u32)(x0 >> 11)) & t_mask);
    ap.pix_y_sh = (s32)(((u32)(y0 >> 11)) & t_mask);
    ap.mask_x = t_mask;
    ap.row_shift = (s32)map_size + 4;
    if (__builtin_expect(adx < 128u && ady < 128u &&
                         (adx < 48u || ady < 48u || (adx + ady) < 120u), 1)) {
      u32 tmp_dpal[256];
      const u32 *dpal;
      if (cached_dpal != NULL) {
        dpal = cached_dpal;
      } else if (pal == palette_ram_converted) {
        (void)cgba_signed_bg_pal_get();
        dpal = cgba_signed_bg_pal_dup + 128;
      } else if (pal_has_signed_prefix && fpal_dup_ver != 0u) {
        dpal = cgba_fpal_dup_get(pal, fpal_dup_ver);
      } else if (pal_has_signed_prefix) {
        const u16 *src = pal - 128;
        for (u32 i = 0u; i < 256u; i++) {
          u32 c = src[i];
          tmp_dpal[i] = (c << 16) | c;
        }
        dpal = tmp_dpal + 128;
      } else {
        for (u32 i = 0u; i < 128u; i++) {
          u32 hi = pal[i + 128u];
          u32 lo = pal[i];
          tmp_dpal[i] = (hi << 16) | hi;
          tmp_dpal[i + 128u] = (lo << 16) | lo;
        }
        dpal = tmp_dpal + 128;
      }
      ap.pal = (const u16 *)dpal;
      if (sdx >= 0) {
        ap.f_x = ((u32)(x0 & 0xFF)) << 24;
        ap.step_x = 1;
      } else {
        ap.f_x = ((u32)(255 - (x0 & 0xFF))) << 24;
        ap.step_x = -1;
      }
      ap.step_fx = adx << 25;
      if (sdy >= 0) {
        ap.f_y = ((u32)(y0 & 0xFF)) << 24;
        ap.step_y_sh = 8;
      } else {
        ap.f_y = ((u32)(255 - (y0 & 0xFF))) << 24;
        ap.step_y_sh = -8;
      }
      ap.step_fy = ady << 25;
      ap.mask_y_sh = ((((u32)(y0 >> 8)) & 7u) << 3) | (((u32)(x0 >> 8)) & 7u);
      cgba_render_affine_rot_base_p2_sh4(&ap);
      return;
    }
    u16 tmp_spal[256];
    const u16 *spal;
    if (cached_spal != NULL) {
      spal = cached_spal;
    } else if (pal == palette_ram_converted) {
      spal = cgba_signed_bg_pal_get();
    } else if (pal_has_signed_prefix) {
      spal = pal;
    } else {
      memcpy(tmp_spal, pal + 128, 128u * sizeof(u16));
      memcpy(tmp_spal + 128, pal, 128u * sizeof(u16));
      spal = tmp_spal + 128;
    }
    ap.pal = spal;
    if (adx < 256u && ady < 256u && (adx + ady) < 420u) {
      if (sdx >= 0) {
        ap.f_x = ((u32)(x0 & 0xFF)) << 24;
        ap.step_x = 1;
      } else {
        ap.f_x = ((u32)(255 - (x0 & 0xFF))) << 24;
        ap.step_x = -1;
      }
      ap.step_fx = adx << 24;
      if (sdy >= 0) {
        ap.f_y = ((u32)(y0 & 0xFF)) << 24;
        ap.step_y_sh = 8;
      } else {
        ap.f_y = ((u32)(255 - (y0 & 0xFF))) << 24;
        ap.step_y_sh = -8;
      }
      ap.step_fy = ady << 24;
      ap.mask_y_sh = ((((u32)(y0 >> 8)) & 7u) << 3) | (((u32)(x0 >> 8)) & 7u);
      cgba_render_affine_rot_base_addc_sh4(&ap, 0u);
      return;
    }
    u32 qflip = 0u;
    if (sdx >= 0) {
      ap.f_x = ((u32)(x0 & 0x7FF)) << 21;
      ap.step_fx = adx << 21;
      ap.step_x = 1;
    } else {
      ap.f_x = ((u32)(2047 - (x0 & 0x7FF))) << 21;
      ap.step_fx = adx << 21;
      ap.step_x = -1;
      qflip |= 7u;
    }
    if (sdy >= 0) {
      ap.f_y = ((u32)(y0 & 0x7FF)) << 21;
      ap.step_fy = ady << 21;
      ap.step_y_sh = 1;
    } else {
      ap.f_y = ((u32)(2047 - (y0 & 0x7FF))) << 21;
      ap.step_fy = ady << 21;
      ap.step_y_sh = -1;
      qflip |= 0x38u;
    }
    ap.mask_y_sh = qflip;
    cgba_render_affine_rot_base_addc_sh4(&ap, 1u);
    return;
  }
  const u32 shift = 17u - map_size;
  cgba_aff_rot_bg_params p;
  p.dst_end = dst_end;
  p.map_base = map_base;
  p.tile_base = tile_base;
  p.pal = pal;
  p.ux = (u32)x0 << shift;
  p.uy = (u32)y0 << shift;
  p.step_x = (u32)sdx << shift;
  p.step_y = (u32)sdy << shift;
  p.cnt = cnt;
  p.pix_shift = -(s32)(25u - map_size);
  p.row_shift = (s32)(map_size + 1u);
  p.use_same_texel = 0u;
  p.tile_kind = NULL;
  cgba_render_affine_rot_base_sh4(&p);
}
#endif
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

  if (cnt == 0u)
    return;

#ifdef CGBA_SH4_ASM_4BPP
  if (!mosaic) {
    if (rdtype == FULLCOLOR && sizeof(dtype) == sizeof(u16)) {
      if (rotate) {
        u32 shift = 17u - map_size;
        u32 abs_step = (u32)(dx < 0 ? -dx : dx) + (u32)(dy < 0 ? -dy : dy);
        u32 flags = (abs_step < (isbase ? 260u : 180u)) ? 1u : 0u;
        const u8 *tkind = !isbase ? cgba_aff_tile_kind_get((u32)(tile_base - vram) >> 14) : NULL;
        if (wrap) {
          if (isbase) {
            cgba_dispatch_affine_rot_base_sh4(
              (u16 *)dst_ptr + cnt, map_base, tile_base, pal,
              source_x + (s32)(cnt - 1u) * dx, source_y + (s32)(cnt - 1u) * dy,
              dx, dy, cnt, map_size);
            return;
          }
          cgba_aff_rot_bg_params p;
          p.dst_end = (u16 *)dst_ptr + cnt;
          p.map_base = map_base;
          p.tile_base = tile_base;
          p.pal = pal;
          p.ux = (u32)(source_x + (s32)(cnt - 1u) * dx) << shift;
          p.uy = (u32)(source_y + (s32)(cnt - 1u) * dy) << shift;
          p.step_x = (u32)(-dx) << shift;
          p.step_y = (u32)(-dy) << shift;
          p.cnt = cnt;
          p.pix_shift = -(s32)(25u - map_size);
          p.row_shift = (s32)(map_size + 1u);
          p.use_same_texel = flags;
          p.tile_kind = tkind;
          cgba_render_affine_rot_over_sh4(&p);
          return;
        } else {
          s32 lo = 0;
          s32 hi = (s32)cnt - 1;
          s32 limit = (s32)(width_height << 8);
          cgba_affine_clip_axis(source_x, dx, limit, lo, hi);
          if (lo <= hi)
            cgba_affine_clip_axis(source_y, dy, limit, lo, hi);
          if (lo > hi) {
            if (isbase) {
              u16 *d16 = (u16 *)dst_ptr;
              u16 bgcol = pal[0];
              for (u32 i = 0; i < cnt; i++)
                d16[i] = bgcol;
            }
            return;
          }
          u32 u_lo = (u32)lo;
          u32 run_cnt = (u32)(hi - lo + 1);
          if (isbase && u_lo > 0u) {
            u16 *d16 = (u16 *)dst_ptr;
            u16 bgcol = pal[0];
            for (u32 i = 0; i < u_lo; i++)
              d16[i] = bgcol;
          }
          if (isbase) {
            cgba_dispatch_affine_rot_base_sh4(
              (u16 *)dst_ptr + (u_lo + run_cnt), map_base, tile_base, pal,
              source_x + hi * dx, source_y + hi * dy,
              dx, dy, run_cnt, map_size);
          } else {
            cgba_aff_rot_bg_params p;
            p.dst_end = (u16 *)dst_ptr + (u_lo + run_cnt);
            p.map_base = map_base;
            p.tile_base = tile_base;
            p.pal = pal;
            p.ux = (u32)(source_x + hi * dx) << shift;
            p.uy = (u32)(source_y + hi * dy) << shift;
            p.step_x = (u32)(-dx) << shift;
            p.step_y = (u32)(-dy) << shift;
            p.cnt = run_cnt;
            p.pix_shift = -(s32)(25u - map_size);
            p.row_shift = (s32)(map_size + 1u);
            p.use_same_texel = flags;
            p.tile_kind = tkind;
            cgba_render_affine_rot_over_sh4(&p);
          }
          if (isbase && (u_lo + run_cnt) < cnt) {
            u16 *d16 = (u16 *)dst_ptr + (u_lo + run_cnt);
            u32 tail = cnt - (u_lo + run_cnt);
            u16 bgcol = pal[0];
            for (u32 i = 0; i < tail; i++)
              d16[i] = bgcol;
          }
          return;
        }
      }
      cgba_aff_bg_params p = {
        (void *)dst_ptr, map_base, tile_base, pal,
        source_x, source_y, dx, dy, cnt, width_height, map_size + 4u,
        (isbase ? 1u : 0u) | (wrap ? 2u : 0u)
      };
      cgba_render_affine_bg_scale_sh4(&p);
      return;
    }
    if (rotate && sizeof(dtype) == sizeof(u32) &&
        (rdtype == INDXCOLOR || (rdtype == STCKCOLOR && isbase))) {
      u32 px_comb_eff = (rdtype == STCKCOLOR && isbase)
        ? (px_comb | (bg_comb << 16))
        : px_comb;
      cgba_aff_bg_params p = {
        (void *)dst_ptr, map_base, tile_base,
        (const u16 *)(uintptr_t)px_comb_eff,
        source_x, source_y, dx, dy, cnt, width_height, map_size + 4u,
        (isbase ? 1u : 0u) | (wrap ? 2u : 0u) | (bg_comb << 16)
      };
      cgba_render_affine_bg_rot_u32_sh4(&p);
      return;
    }
  }
#endif

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
      // Y coordinate stays constant across the walk: hoist map/tile row pointers.
      const u32 pix_y = (u32)(source_y >> 8) & (width_height-1);
      const u8 *map_row = map_base + ((pix_y >> 3) << (map_size + 4));
      const u8 *tile_row_base = tile_base + ((pix_y & 7u) << 3);
      u32 cur_tx = 0xFFFFFFFFu;
      const u8 *cur_tile_row = tile_row_base;
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8) & (width_height-1);
        if (!mosaic || !mctr) {
          u32 tx = pix_x >> 3;
          if (tx != cur_tx) {
            cur_tx = tx;
            cur_tile_row = tile_row_base + ((u32)map_row[tx] << 6);
          }
          pval = cur_tile_row[pix_x & 7u];
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
      // Specialized version for scaled-only backgrounds: hoist Y row offsets.
      u8 pval = 0;
      const u32 pix_y = (u32)(source_y >> 8);
      if (pix_y < width_height) {  // Check if within Y-coord range
        const u8 *map_row = map_base + ((pix_y >> 3) << (map_size + 4));
        const u8 *tile_row_base = tile_base + ((pix_y & 7u) << 3);
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
        u32 cur_tx = 0xFFFFFFFFu;
        const u8 *cur_tile_row = tile_row_base;
        for (u32 i = 0; cnt; i++, cnt--) {
          u32 pix_x = (u32)(source_x >> 8);
          if (pix_x >= width_height)
            break;

          if (!mosaic || !mctr) {
            u32 tx = pix_x >> 3;
            if (tx != cur_tx) {
              cur_tx = tx;
              cur_tile_row = tile_row_base + ((u32)map_row[tx] << 6);
            }
            pval = cur_tile_row[pix_x & 7u];
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
static u8 obj_alpha_prio_mask[160];
static u8 obj_alpha_complex[160];
static u8 obj_row_prio_mask[160];
static u8 obj_prio_sprites[5][128];
static u8 obj_prio_num[5];
static u8 obj_vis_list[128];
static u8 obj_vis_num;
static u8 obj_y_start[128];
static u8 obj_y_end[128];
static u16 obj_cyccnt[128];
static u8 obj_mode_arr[128];
static u8 obj_prio_arr[128];
static bool obj_list_built = false;
static bool obj_cyccap_hit = false;
static u16 obj_last_max_rc = 0;

static __attribute__((noinline)) void cgba_order_obj_materialize_lists(void)
{
  u16 rend_cycles[160];
  u16 max_rc = obj_last_max_rc;
  memset(obj_priority_count, 0, sizeof(obj_priority_count));
  memset(rend_cycles, 0, sizeof(rend_cycles));
  for (u32 vi = 0; vi < obj_vis_num; vi++) {
    u32 obj_num = obj_vis_list[vi];
    u32 prio = obj_prio_arr[obj_num];
    u32 starty = obj_y_start[obj_num];
    u32 endy = obj_y_end[obj_num];
    u16 cyc = obj_cyccnt[obj_num];
    u8 *cnt_p = &obj_priority_count[prio][starty];
    u8 *list_p = obj_priority_list[prio][starty];
    u16 *rc_p = &rend_cycles[starty];
    u32 n = endy - starty;
    do {
      if (*rc_p < max_rc) {
        u32 cur_cnt = *cnt_p;
        list_p[cur_cnt] = (u8)obj_num;
        *cnt_p = (u8)(cur_cnt + 1u);
        *rc_p += cyc;
      }
      cnt_p++; list_p += 128; rc_p++;
    } while (--n);
  }
  obj_list_built = true;
}

typedef struct {
  s16 obj_x, obj_y;
  s16 obj_w, obj_h;
  u16 attr1, attr2;
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
    .obj_x = (s16)((s32)(a1 << 23) >> 23),
    .obj_y = (s16)((a0 & 255) > 160 ? (a0 & 255) - 256 : (a0 & 255)),
    .obj_w = (s16)obj_dim_table[shape][size][0],
    .obj_h = (s16)obj_dim_table[shape][size][1],
    .attr1 = a1, .attr2 = eswap16(entry->attr2),
    .is_double = (a0 & 0x200) != 0,
  };
  return obj;
}

#if CGBA_VIDEO_SPRITE_DECODE_CACHE
/* Rebuilt with the visibility lists on every OAM mutation, including native
 * stores, DMA, reset and savestate load. Affine matrices remain live OAM reads.
 * Sized to 32 bytes so indexing is a single shift and each entry sits in one
 * SH-4 D-cache line. */
struct alignas(32) cgba_decoded_obj {
  t_sprite sprite;
  u16 attr0;
  u16 fast_tile_base;  /* ((attr2 & 0x3FF) << 5) + (hflip ? (row_bytes - 32) : 0) */
  u16 fast_span;       /* (num_tiles - 1) << 5 */
  u8  fast_flags;      /* bit0=!aff&!8bpp, bit1=hflip, bit2=vflip, bit3=emosaic */
  u8  fast_num_tiles;  /* obj_w >> 3 */
  u8  fast_pal_off;    /* (attr2 >> 8) & 0xF0 */
  u8  fast_row_shift;  /* log2(obj_w << 2): 5/6/7/8 for 1D OBJ mapping */
};
static_assert(sizeof(cgba_decoded_obj) == 32,
              "cgba_decoded_obj must be 32 bytes");
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
        s32 lo = 0, hi = (s32)cnt - 1;
        cgba_affine_clip_axis(source_x, dx, (s32)(obj_dimw << 8), lo, hi);
        if (lo > hi)
          return;
        cgba_render_obj_4bpp_affine_scale_sh4(
          (u16 *)(void *)dst_ptr + lo, ((u32)(hi - lo + 1) << 16) | obj_dimw,
          source_x + lo * dx, dx,
          &vram[0x10000u + row_base], palptr + palette);
        return;
      }
    } else {
      u32 bt = base_tile & 0x7FFFu;
      u32 max_byte = bt + ((obj_dimh >> 3) - 1u) * tile_pitch +
                     (((obj_dimw >> 3) - 1u) << 5) + 32u;
      if (__builtin_expect(max_byte <= 0x8000u, 1)) {
        s32 lo = 0, hi = (s32)cnt - 1;
        cgba_affine_clip_axis(source_x, dx, (s32)(obj_dimw << 8), lo, hi);
        if (lo <= hi)
          cgba_affine_clip_axis(source_y, dy, (s32)(obj_dimh << 8), lo, hi);
        if (lo > hi)
          return;
        u32 row_shift = 7u;
        if (obj1dmap) {
          /* dimw in {8, 16, 32, 64} -> row_shift in {2, 3, 4, 5}. */
          row_shift = (obj_dimw == 8u) ? 2u : (obj_dimw == 16u) ? 3u
                    : (obj_dimw == 32u) ? 4u : 5u;
        }
        cgba_aff_rot_params p = {
          (u16 *)(void *)dst_ptr + lo,
          &vram[0x10000u + bt],
          palptr + palette,
          source_x + lo * dx, source_y + lo * dy, dx, dy,
          (u32)(hi - lo + 1), obj_dimw, obj_dimh, row_shift
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

#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
static __attribute__((noinline)) bool cgba_render_obj_4bpp_affine_fast(
  const cgba_decoded_obj *d, u32 start, u32 end, u16 *scanline,
  const u16 *palptr, s32 vcount, bool obj1dmap)
{
  const t_sprite *obji = &d->sprite;
  const t_affp *affp = &((const t_affp *)oam_ram)[(obji->attr1 >> 9) & 0x1Fu];
  s32 dx  = (s16)eswap16(affp->dx);
  s32 dy  = (s16)eswap16(affp->dy);
  s32 dmx = (s16)eswap16(affp->dmx);
  s32 dmy = (s16)eswap16(affp->dmy);

  u32 obj_dimw = (u32)obji->obj_w;
  u32 obj_dimh = (u32)obji->obj_h;
  s32 middle_x = obji->is_double ? obji->obj_w : (obji->obj_w >> 1);
  s32 middle_y = obji->is_double ? obji->obj_h : (obji->obj_h >> 1);
  s32 y_delta = vcount - (obji->obj_y + middle_y);
  if (obji->obj_x < (s32)start)
    middle_x -= ((s32)start - obji->obj_x);
  s32 source_x = ((s32)obj_dimw << 7) + (y_delta * dmx) - (middle_x * dx);
  s32 source_y = ((s32)obj_dimh << 7) + (y_delta * dmy) - (middle_x * dy);

  u32 d_start = (obji->obj_x > (s32)start) ? (u32)obji->obj_x : start;
  s32 ox_end = obji->obj_x + (obji->is_double ? (obji->obj_w << 1) : obji->obj_w);
  u32 d_end = (ox_end < (s32)end) ? (u32)ox_end : end;
  u32 cnt = d_end - d_start;

  u32 base_tile = (obji->attr2 & 0x3FFu) << 5;
  u32 tile_pitch = obj1dmap ? (obj_dimw << 2) : 1024u;

  if (dy == 0) {
    u32 py = (u32)(source_y >> 8);
    if (py >= obj_dimh)
      return true;
    u32 row_base = (base_tile + (py >> 3) * tile_pitch + ((py & 7u) << 2)) & 0x7FFFu;
    if (__builtin_expect(row_base + d->fast_span + 4u > 0x8000u, 0))
      return false;
    if (dx == 256 && source_x == 0 && cnt == obj_dimw) {
      cgba_render_obj_4bpp_tiles_sh4(
        &vram[0x10000u + row_base], d->fast_num_tiles,
        scanline + d_start, palptr + d->fast_pal_off);
    } else {
      cgba_render_obj_4bpp_affine_scale_sh4(
        scanline + d_start, (cnt << 16) | obj_dimw,
        source_x, dx,
        &vram[0x10000u + row_base], palptr + d->fast_pal_off);
    }
    return true;
  }

  u32 bt = base_tile & 0x7FFFu;
  u32 max_byte = bt + ((obj_dimh >> 3) - 1u) * tile_pitch + d->fast_span + 32u;
  if (__builtin_expect(max_byte > 0x8000u, 0))
    return false;
  s32 lo = 0, hi = (s32)cnt - 1;
  cgba_affine_clip_axis(source_x, dx, (s32)(obj_dimw << 8), lo, hi);
  if (lo <= hi)
    cgba_affine_clip_axis(source_y, dy, (s32)(obj_dimh << 8), lo, hi);
  if (lo <= hi) {
    u32 row_shift = obj1dmap ? ((u32)d->fast_row_shift - 3u) : 7u;
    cgba_aff_rot_params p = {
      scanline + d_start + lo,
      &vram[0x10000u + bt],
      palptr + d->fast_pal_off,
      source_x + lo * dx, source_y + lo * dy, dx, dy,
      (u32)(hi - lo + 1), obj_dimw, obj_dimh, row_shift
    };
    cgba_render_obj_4bpp_affine_rot_sh4(&p);
  }
  return true;
}
#endif

// This function assumes that order_obj has been called to prepare the objects.
template <typename stype, rendtype rdtype, bool cached = CGBA_VIDEO_SPRITE_DECODE_CACHE>
void render_scanline_objs(
  u32 priority, u32 start, u32 end, void *raw_ptr, const u16* palptr
) {
  if (__builtin_expect(!obj_list_built, 0))
    cgba_order_obj_materialize_lists();
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
      } else if ((d->attr0 & 0x2100u) == 0x0100u && (ff & forbid_mask) == 0u) {
        if (cgba_render_obj_4bpp_affine_fast(
              d, start, end, (u16 *)scanline, palptr, vcount, obj1dmap))
          continue;
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

  memset(obj_prio_num, 0, sizeof(obj_prio_num));
  memset(obj_alpha_count, 0, sizeof(obj_alpha_count));
  memset(obj_alpha_prio_mask, 0, sizeof(obj_alpha_prio_mask));
  memset(obj_alpha_complex, 0, sizeof(obj_alpha_complex));
  memset(obj_row_prio_mask, 0, sizeof(obj_row_prio_mask));
  memset(rend_cycles, 0, sizeof(rend_cycles));
  obj_vis_num = 0;
  obj_list_built = false;
  obj_cyccap_hit = false;
  obj_last_max_rc = max_rend_cycles;
  bool any_alpha_obj = false;

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
        u32 obj_priority = (obj_mode == OBJ_MOD_WINDOW)
                             ? 4u
                             : ((obj_attr2 >> 10) & 0x03u);
        bool is_affine = (obj_attr0 & 0x100) != 0;
        // Clip Y coord and height to the 0..159 interval
        u32 starty = MAX(obj_y, 0);
        u32 endy   = MIN(obj_y + obj_height, 160);

        // Calculate needed cycles to render the sprite
        u16 cyccnt = is_affine ? (10 + obj_width * 2) : obj_width;

        obj_vis_list[obj_vis_num++] = (u8)obj_num;
        obj_prio_sprites[obj_priority][obj_prio_num[obj_priority]++] = (u8)obj_num;
        obj_y_start[obj_num] = (u8)starty;
        obj_y_end[obj_num] = (u8)endy;
        obj_cyccnt[obj_num] = cyccnt;
        obj_mode_arr[obj_num] = (u8)obj_mode;
        obj_prio_arr[obj_num] = (u8)obj_priority;

        u8 pbit = (u8)(1u << obj_priority);
        u8 *mask_p = &obj_row_prio_mask[starty];
        u16 *rc_p = &rend_cycles[starty];
        u32 n = endy - starty;
        if (obj_mode == OBJ_MOD_SEMITRAN) {
          any_alpha_obj = true;
          u8 *alpha_p = &obj_alpha_count[starty];
          do {
            if (*rc_p < max_rend_cycles) {
              *mask_p |= pbit;
              *rc_p += cyccnt;
              *alpha_p = 1;
            } else {
              obj_cyccap_hit = true;
            }
            mask_p++; alpha_p++; rc_p++;
          } while (--n);
        } else {
          do {
            if (*rc_p < max_rend_cycles) {
              *mask_p |= pbit;
              *rc_p += cyccnt;
            } else {
              obj_cyccap_hit = true;
            }
            mask_p++; rc_p++;
          } while (--n);
        }
      }
    }
  }

  if (any_alpha_obj) {
    cgba_order_obj_materialize_lists();
    for (u32 y = 0; y < 160u; y++) {
      if (!obj_alpha_count[y])
        continue;
      s16 dx0[64], dx1[64];
      u32 ndrawn = 0u;
      u8 apmask = 0u;
      u8 complex = 0u;
      for (s32 p = 3; p >= 0 && !complex; p--) {
        u32 cnt = obj_priority_count[p][y];
        if (!cnt)
          continue;
        const u8 *lst = obj_priority_list[p][y];
        for (s32 i = (s32)cnt - 1; i >= 0; i--) {
          u32 oidx = lst[i];
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
          const cgba_decoded_obj *d = &decoded_objs[oidx];
          u16 a0 = d->attr0;
          s32 ox = d->sprite.obj_x;
          s32 ow = ((a0 & 0x100u) && d->sprite.is_double)
                     ? (d->sprite.obj_w << 1) : d->sprite.obj_w;
#else
          const t_oam *op = &oam_base[oidx];
          u16 a0 = eswap16(op->attr0);
          u16 a1 = eswap16(op->attr1);
          s32 ox = (s32)(a1 << 23) >> 23;
          s32 ow = obj_dim_table[a0 >> 14][a1 >> 14][0];
          if (a0 & 0x200u)
            ow <<= 1;
#endif
          s32 ox_end = ox + ow;
          if (((a0 >> 10) & 3u) == OBJ_MOD_SEMITRAN) {
            apmask |= (u8)(1u << p);
            if ((a0 & 0x3100u) != 0u) {
              complex = 1u;
              break;
            }
            for (u32 k = 0; k < ndrawn; k++) {
              if (ox < (s32)dx1[k] && ox_end > (s32)dx0[k]) {
                complex = 1u;
                break;
              }
            }
            if (complex)
              break;
          }
          if (ndrawn < 64u) {
            dx0[ndrawn] = (s16)ox;
            dx1[ndrawn] = (s16)ox_end;
            ndrawn++;
          } else {
            complex = 1u;
            break;
          }
        }
      }
      obj_alpha_prio_mask[y] = apmask;
      obj_alpha_complex[y] = complex;
    }
  }
}

#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
/* Sprite-major FULLCOLOR OBJ strip renderer for [sy0, sy1) (width 0..240).
 * Draws all sprites of `prio` back-to-front across their clipped vertical
 * range, avoiding per-scanline OAM decoding, out-of-bounds affine rows,
 * redundant 4bpp row unpacking on zoomed rows, and the 80 KiB obj_priority_list. */
static __attribute__((noinline)) void cgba_render_obj_batch_fullcolor_strip(
  u32 prio, u32 sy0, u32 sy1, u16 *screen_base,
  const u16 *const *row_pal, const u16 *base_pal)
{
  u32 pbit = 1u << prio;
  if (__builtin_expect(obj_cyccap_hit, 0)) {
    for (u32 y = sy0; y < sy1; y++) {
      if (obj_row_prio_mask[y] & pbit) {
        const u16 *rpal = row_pal ? row_pal[y - sy0] : base_pal;
        write_ioreg(REG_VCOUNT, y);
        render_scanline_objs<u16, FULLCOLOR>(
          prio, 0, 240, screen_base + y * 240u, rpal + 0x100);
      }
    }
    return;
  }

  u32 scnt = obj_prio_num[prio];
  if (scnt == 0u)
    return;
  const u8 *pm = obj_row_prio_mask + sy0;
  u32 any_row = 0u;
  for (u32 r = 0u; r < sy1 - sy0; r++)
    any_row |= pm[r];
  if (!(any_row & pbit))
    return;

  const bool obj1dmap = (read_ioreg(REG_DISPCNT) & 0x40) != 0;
  const bool mosaic_active = (read_ioreg(REG_MOSAIC) & 0xFF00) != 0;
  const u8 forbid_mask = mosaic_active ? 0x08u : 0x00u;
  const u8 *slist = obj_prio_sprites[prio];

  for (s32 si = (s32)scnt - 1; si >= 0; si--) {
    u32 oid = slist[si];
    u32 y0 = obj_y_start[oid];
    u32 y1 = obj_y_end[oid];
    if (y0 < sy0) y0 = sy0;
    if (y1 > sy1) y1 = sy1;
    if (y0 >= y1)
      continue;

    const cgba_decoded_obj *d = &decoded_objs[oid];
    const t_sprite *obji = &d->sprite;
    s32 ox = obji->obj_x;
    u8 ff = d->fast_flags;

    u32 sub_y0 = y0;
    while (sub_y0 < y1) {
      const u16 *rpal = row_pal ? row_pal[sub_y0 - sy0] : base_pal;
      u32 sub_y1 = y1;
      if (row_pal) {
        sub_y1 = sub_y0 + 1u;
        while (sub_y1 < y1 && row_pal[sub_y1 - sy0] == rpal)
          sub_y1++;
      }
      const u16 *palptr = rpal + 0x100;

      if (__builtin_expect((ff & 1u) != 0u && (ff & forbid_mask) == 0u &&
                           ox >= 0 && (ox + obji->obj_w) <= 240, 1)) {
        /* Regular 4bpp full-width sprite: render in up to 8-row tile-row strips. */
        u32 pshift = obj1dmap ? (u32)d->fast_row_shift : 10u;
        u32 span = d->fast_span;
        u32 ntiles = d->fast_num_tiles;
        const u16 *subpal = palptr + d->fast_pal_off;
        bool vflip = (ff & 4u) != 0u;
        bool hflip = (ff & 2u) != 0u;
        s32 row_step = vflip ? -4 : 4;
        u32 y = sub_y0;
        while (y < sub_y1) {
          u32 voff = vflip ? (u32)(obji->obj_y + obji->obj_h - (s32)y - 1)
                           : (u32)((s32)y - obji->obj_y);
          u32 in_tile = voff & 7u;
          u32 k = vflip ? (in_tile + 1u) : (8u - in_tile);
          if (y + k > sub_y1)
            k = sub_y1 - y;
          u32 off = (d->fast_tile_base + ((voff >> 3) << pshift) +
                     (in_tile << 2)) & 0x7FFFu;
          bool nowrap = hflip ? (off >= span) : (off + span < 0x8000u);
          if (__builtin_expect(nowrap, 1)) {
            u16 *dst = screen_base + y * 240u + ox;
            if (hflip)
              cgba_render_obj_4bpp_tiles_strip_hf_sh4(
                &vram[0x10000u + off], (k << 16) | ntiles, dst, subpal, row_step);
            else
              cgba_render_obj_4bpp_tiles_strip_sh4(
                &vram[0x10000u + off], (k << 16) | ntiles, dst, subpal, row_step);
          } else {
            for (u32 ry = y; ry < y + k; ry++) {
              write_ioreg(REG_VCOUNT, ry);
              render_scanline_obj_slow<u16, FULLCOLOR>(
                obji, d->attr0, 0, 240, screen_base + ry * 240u, 0u,
                palptr, (s32)ry, obj1dmap, mosaic_active);
            }
          }
          y += k;
        }
      } else if ((d->attr0 & 0x2100u) == 0x0100u && (ff & forbid_mask) == 0u) {
        /* Affine 4bpp non-mosaic sprite: decode matrix ONCE per strip. */
        const t_affp *affp = &((const t_affp *)oam_ram)[(obji->attr1 >> 9) & 0x1Fu];
        s32 dx  = (s16)eswap16(affp->dx);
        s32 dy  = (s16)eswap16(affp->dy);
        s32 dmx = (s16)eswap16(affp->dmx);
        s32 dmy = (s16)eswap16(affp->dmy);
        if (__builtin_expect(dy == 0 && dmx == 0, 1)) {
          u32 obj_dimw = (u32)obji->obj_w;
          u32 obj_dimh = (u32)obji->obj_h;
          s32 middle_y = obji->is_double ? obji->obj_h : (obji->obj_h >> 1);
          s32 source_y0 = ((s32)obj_dimh << 7) +
                          ((s32)sub_y0 - (obji->obj_y + middle_y)) * dmy;
          s32 ylo = 0, yhi = (s32)(sub_y1 - sub_y0) - 1;
          cgba_affine_clip_axis(source_y0, dmy, (s32)(obj_dimh << 8), ylo, yhi);
          if (ylo <= yhi) {
            s32 middle_x = obji->is_double ? obji->obj_w : (obji->obj_w >> 1);
            u32 d_start = (ox > 0) ? (u32)ox : 0u;
            s32 ox_end = ox + (obji->is_double ? (obji->obj_w << 1) : obji->obj_w);
            u32 d_end = (ox_end < 240) ? (u32)ox_end : 240u;
            if (ox < 0)
              middle_x -= (-ox);
            s32 source_x = ((s32)obj_dimw << 7) - (middle_x * dx);
            s32 lo = 0, hi = (s32)(d_end - d_start) - 1;
            cgba_affine_clip_axis(source_x, dx, (s32)(obj_dimw << 8), lo, hi);
            if (lo <= hi) {
              s32 sx_first = source_x + lo * dx;
              s32 sx_last  = source_x + hi * dx;
              u32 px_a = (u32)(sx_first >> 8);
              u32 px_b = (u32)(sx_last >> 8);
              u32 min_tx = (px_a < px_b ? px_a : px_b) >> 3;
              u32 max_tx = (px_a > px_b ? px_a : px_b) >> 3;
              s32 sy_first = source_y0 + ylo * dmy;
              s32 sy_last  = source_y0 + yhi * dmy;
              u32 py_a = (u32)(sy_first >> 8);
              u32 py_b = (u32)(sy_last >> 8);
              u32 max_py = (py_a > py_b) ? py_a : py_b;
              u32 bt = (obji->attr2 & 0x3FFu) << 5;
              u32 pshift = obj1dmap ? (u32)d->fast_row_shift : 10u;
              u32 max_byte = bt + ((max_py >> 3) << pshift) + (max_tx << 5) + 32u;
              if (__builtin_expect(max_byte <= 0x8000u, 1)) {
                cgba_obj_aff_scale_strip_params p;
                p.dst_ptr = screen_base + (sub_y0 + (u32)ylo) * 240u + d_start + (u32)lo;
                p.vram_tx = &vram[0x10000u + bt + (min_tx << 5)];
                p.subpal  = palptr + d->fast_pal_off;
                p.sx0     = sx_first - (s32)(min_tx << 11);
                p.dx      = dx;
                p.sy0     = sy_first;
                p.dmy     = dmy;
                p.span_cnt = (u32)(hi - lo + 1);
                p.nrows    = (u32)(yhi - ylo + 1);
                p.ntiles   = max_tx - min_tx + 1u;
                p.tile_pitch_sh = pshift;
                cgba_render_obj_4bpp_affine_scale_strip_sh4(&p);
              } else {
                for (u32 ry = sub_y0 + (u32)ylo; ry <= sub_y0 + (u32)yhi; ry++) {
                  if (!cgba_render_obj_4bpp_affine_fast(
                        d, 0, 240, screen_base + ry * 240u, palptr, (s32)ry, obj1dmap)) {
                    write_ioreg(REG_VCOUNT, ry);
                    render_scanline_obj_slow<u16, FULLCOLOR>(
                      obji, d->attr0, 0, 240, screen_base + ry * 240u, 0u,
                      palptr, (s32)ry, obj1dmap, mosaic_active);
                  }
                }
              }
            }
          }
        } else {
          for (u32 ry = sub_y0; ry < sub_y1; ry++) {
            if (!cgba_render_obj_4bpp_affine_fast(
                  d, 0, 240, screen_base + ry * 240u, palptr, (s32)ry, obj1dmap)) {
              write_ioreg(REG_VCOUNT, ry);
              render_scanline_obj_slow<u16, FULLCOLOR>(
                obji, d->attr0, 0, 240, screen_base + ry * 240u, 0u,
                palptr, (s32)ry, obj1dmap, mosaic_active);
            }
          }
        }
      } else {
        for (u32 ry = sub_y0; ry < sub_y1; ry++) {
          write_ioreg(REG_VCOUNT, ry);
          render_scanline_obj_slow<u16, FULLCOLOR>(
            obji, d->attr0, 0, 240, screen_base + ry * 240u, 0u,
            palptr, (s32)ry, obj1dmap, mosaic_active);
        }
      }

      sub_y0 = sub_y1;
    }
  }
}
#endif

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

static u32 cgba_term_a_epoch = 1u;

#if CGBA_PALETTE_BLEND_BIT
static u32 cgba_direct_term_a[256] __attribute__((aligned(32)));
static u32 cgba_direct_term_a_eva = 0xFFFFFFFFu;
static u32 cgba_direct_obj_term_a[256] __attribute__((aligned(32)));
static u32 cgba_direct_obj_term_a_eva = 0xFFFFFFFFu;
static const u32 *cgba_bg_blend_term_a_get(u32 eva)
{
  if (__builtin_expect((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) != 0u, 0)) {
    cgba_direct_term_a_eva = 0xFFFFFFFFu;
    cgba_direct_obj_term_a_eva = 0xFFFFFFFFu;
    cgba_term_a_epoch++;
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
  cgba_term_a_epoch++;
  return cgba_direct_term_a;
}
static const u32 *cgba_obj_blend_term_a_get(u32 eva)
{
  if (__builtin_expect((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) != 0u, 0)) {
    cgba_direct_term_a_eva = 0xFFFFFFFFu;
    cgba_direct_obj_term_a_eva = 0xFFFFFFFFu;
    cgba_term_a_epoch++;
    palette_ram_dirty &= ~CGBA_PALETTE_DIRTY_BLEND;
  }
  if (__builtin_expect(cgba_direct_obj_term_a_eva == eva, 1))
    return cgba_direct_obj_term_a;
  for (u32 i = 0; i < 256u; i++) {
    u32 pixel = palette_ram_converted[256u + i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    cgba_direct_obj_term_a[i] = expanded * eva;
  }
  cgba_direct_obj_term_a_eva = eva;
  return cgba_direct_obj_term_a;
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
  cgba_term_a_epoch++;
  return direct_term_a;
}
static const u32 *cgba_obj_blend_term_a_get(u32 eva)
{
  static u32 direct_obj_term_a[256];
  for (u32 i = 0; i < 256u; i++) {
    u32 pixel = palette_ram_converted[256u + i];
    u32 expanded = (pixel | (pixel << 16)) & BLND_MSK;
    direct_obj_term_a[i] = expanded * eva;
  }
  return direct_obj_term_a;
}
#endif

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
static u32 cgba_direct_term_a_s4[256] __attribute__((aligned(32)));
static u32 cgba_direct_term_a_s4_epoch = 0u;
static const u32 *cgba_bg_blend_term_a_s4_get(u32 eva)
{
  const u32 *ta = cgba_bg_blend_term_a_get(eva);
  if (__builtin_expect(cgba_direct_term_a_s4_epoch == cgba_term_a_epoch, 1))
    return cgba_direct_term_a_s4;
  for (u32 i = 0; i < 256u; i++)
    cgba_direct_term_a_s4[i] = (ta[i] >> 4) & BLND_MSK;
  cgba_direct_term_a_s4_epoch = cgba_term_a_epoch;
  return cgba_direct_term_a_s4;
}
#endif

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

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  if (start < end) {
    cgba_merge_blend_params p;
    p.blend_a = blend_a;
    p.blend_b = blend_b;
    p.top_blend_bits = (st_objs ? 0x800u : 0u) |
                       ((bldtype == BLEND_ONLY) ? 0x200u : 0u);
    p.sat_mask = can_saturate ? (OVFR_MSK | OVFG_MSK | OVFB_MSK) : 0u;
    p.bright_mask = (bldtype == BLEND_DARK || bldtype == BLEND_BRIGHT) ? 0x200u : 0u;
    p.bright_pa = (bldtype == BLEND_BRIGHT)
                    ? (((BLND_MSK * brightf) >> 4) & BLND_MSK) : 0u;
    p.bright_inv = 16u - brightf;
    cgba_merge_blend_sh4(dst + start, src + start, end - start,
                         palette_ram_converted, &p);
  }
  return;
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
        u16 p1 = palette_ram_converted[(pixpair >>  0) & 0x1FF];
        u16 p2 = palette_ram_converted[(pixpair >> 16) & 0x1FF];
        u32 p1e = (p1 | (p1 << 16)) & BLND_MSK;
        u32 p2e = (p2 | (p2 << 16)) & BLND_MSK;
        u32 pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4);

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
        u16 p1 = palette_ram_converted[(pixpair >>  0) & 0x1FF];
        u16 p2 = palette_ram_converted[(pixpair >> 16) & 0x1FF];
        u32 p1e = (p1 | (p1 << 16)) & BLND_MSK;
        u32 p2e = (p2 | (p2 << 16)) & BLND_MSK;
        u32 pfe = (((p1e * blend_a) + (p2e * blend_b)) >> 4) & BLND_MSK;
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

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  if (start < end) {
    u32 pa = (bldtype == BLEND_DARK) ? 0u
                                     : (((BLND_MSK * brightness) >> 4) & BLND_MSK);
    cgba_merge_brightness_sh4(srcdst + start, end - start,
                              palette_ram_converted, pa, 16u - brightness);
  }
  return;
#endif

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

template<bool saturate>
static inline void cgba_blend_4bpp_part_pixels(
  u16 *dst, u32 col_start, u32 col_end, u16 raw_map,
  const u8 *tile_base, s32 vflip_off, const u32 *term_a, u32 evb)
{
  u32 tile = eswap16(raw_map);
  const u8 *row = tile_base + ((tile & 0x3FFu) << 5);
  if (tile & 0x800u)
    row += vflip_off;
  if (*(const u32 *)row == 0u)
    return;
  const u32 *ta = term_a + ((tile >> 8) & 0xF0u);
  const u32 width = col_end - col_start;
  u32 last_key = 0u;
  u16 last_out = 0u;
  if (tile & 0x400u) {
    for (u32 i = 0; i < width; i++) {
      u32 sc = 7u - (col_start + i);
      u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
      if (pix) {
        u16 d = dst[i];
        u32 key = (pix << 16) | d;
        if (key != last_key) {
          last_key = key;
          last_out = cgba_blend_rgb565_direct<saturate>(ta[pix], d, evb);
        }
        dst[i] = last_out;
      }
    }
  } else {
    for (u32 i = 0; i < width; i++) {
      u32 c = col_start + i;
      u32 pix = (row[c >> 1] >> ((c & 1u) << 2)) & 0xFu;
      if (pix) {
        u16 d = dst[i];
        u32 key = (pix << 16) | d;
        if (key != last_key) {
          last_key = key;
          last_out = cgba_blend_rgb565_direct<saturate>(ta[pix], d, evb);
        }
        dst[i] = last_out;
      }
    }
  }
}

#if !defined(CGBA_SH4_ASM_4BPP) || defined(USE_XBGR1555_FORMAT)
template<bool saturate>
static inline void cgba_blend_4bpp_tile_run_c(
  u32 todraw, u16 *&map_ptr, u16 *&dst_ptr,
  const u8 *tile_base, s32 vflip_off, const u32 *term_a, u32 evb)
{
  u16 *map = map_ptr;
  u16 *map_end = map + todraw;
  u16 *dst = dst_ptr;
  while (map < map_end) {
    u16 raw = *map;
    u32 tile = eswap16(raw);
    const u8 *row = tile_base + ((tile & 0x3FFu) << 5);
    if (tile & 0x800u)
      row += vflip_off;
    if (*(const u32 *)row == 0u) {
      do {
        map++;
        dst += 8;
      } while (map < map_end && *map == raw);
      continue;
    }
    const u32 *ta = term_a + ((tile >> 8) & 0xF0u);
    if (tile & 0x400u) {
      for (u32 i = 0; i < 8u; i++) {
        u32 sc = 7u - i;
        u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
        if (pix)
          dst[i] = cgba_blend_rgb565_direct<saturate>(ta[pix], dst[i], evb);
      }
    } else {
      for (u32 i = 0; i < 4u; i++) {
        u32 b = row[i];
        u32 p0 = b & 0xFu;
        u32 p1 = b >> 4;
        if (p0)
          dst[i * 2u] = cgba_blend_rgb565_direct<saturate>(ta[p0], dst[i * 2u], evb);
        if (p1)
          dst[i * 2u + 1u] = cgba_blend_rgb565_direct<saturate>(ta[p1], dst[i * 2u + 1u], evb);
      }
    }
    map++;
    dst += 8;
  }
  map_ptr = map;
  dst_ptr = dst;
}
#endif

static __attribute__((noinline)) void cgba_render_scanline_text_4bpp_blend(
  u32 layer, u32 start, u32 end, u16 *scanline, u32 eva, u32 evb)
{
  if (start >= end)
    return;
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  u16 vcount = read_ioreg(REG_VCOUNT);
  u32 map_size = (bg_control >> 14) & 0x03u;
  u32 voffset = (vcount + read_ioreg(REG_BGxVOFS(layer))) & 511u;
  u32 base_block = (bg_control >> 8) & 0x1Fu;
  u32 vert_pix_offset = (voffset & 7u) * 4u;
  u32 tilecntrl = (bg_control >> 2) & 0x03u;
  const u8 *tile_base = &vram[tilecntrl * 16384u + vert_pix_offset];

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  {
    u32 sb0 = base_block +
              (((map_size & 0x02u) && (voffset >= 256u))
                 ? ((map_size & 0x01u) ? 2u : 1u) : 0u);
    u32 row_bit = 1u << ((voffset & 255u) >> 3);
    if (cgba_sb_row_is_all_zero(sb0, row_bit) &&
        (!(map_size & 0x01u) || cgba_sb_row_is_all_zero(sb0 + 1u, row_bit)) &&
        *(const u32 *)tile_base == 0u)
      return;
  }
#endif

  u32 map_width = map_widths[map_size];
  u32 hoffset = (start + read_ioreg(REG_BGxHOFS(layer))) & 511u;
  u16 *dest_ptr = scanline + start;
  u16 *map_base = (u16 *)&vram[base_block * 2048u];
  if ((map_size & 0x02u) && (voffset >= 256u))
    map_base += (map_width >> 3) * 32u;
  map_base += ((voffset & 255u) >> 3) * 32u;

  u16 *map_ptr, *second_ptr;
  if (map_size & 0x01u) {
    if (hoffset >= 256u) {
      map_ptr = map_base + 1024u;
      second_ptr = map_base;
      hoffset -= 256u;
    } else {
      map_ptr = map_base;
      second_ptr = map_base + 1024u;
    }
  } else {
    map_ptr = second_ptr = map_base;
    hoffset &= 255u;
  }
  map_ptr += hoffset >> 3;

  s32 vflip_off = (s32)((7u - (voffset & 7u)) * 4u) - (s32)vert_pix_offset;
  u32 rem = end - start;
  u32 pixel_run = 256u - hoffset;
  u32 tile_hoff = hoffset & 7u;
  const bool saturate = (eva + evb) > 16u;
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  const u32 *term_a_s4 = (evb == 16u) ? cgba_bg_blend_term_a_s4_get(eva) : NULL;
#endif

  if (tile_hoff) {
    u32 todraw = MIN(8u - tile_hoff, rem);
    if (saturate)
      cgba_blend_4bpp_part_pixels<true>(
        dest_ptr, tile_hoff, tile_hoff + todraw, *map_ptr++,
        tile_base, vflip_off, term_a, evb);
    else
      cgba_blend_4bpp_part_pixels<false>(
        dest_ptr, tile_hoff, tile_hoff + todraw, *map_ptr++,
        tile_base, vflip_off, term_a, evb);
    dest_ptr += todraw;
    rem -= todraw;
    pixel_run -= todraw;
    if (pixel_run == 0u) {
      map_ptr = second_ptr;
      pixel_run = 256u;
    }
  }

  while (rem >= 8u) {
    u32 todraw = MIN(rem, pixel_run) >> 3;
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
    if (evb == 16u) {
      cgba_render_4bpp_span_blend_evb16_sh4(
        map_ptr, todraw, dest_ptr, tile_base, vflip_off, term_a_s4);
    } else {
      u32 rke = (evb << 16) | ((voffset & 7u) << 8) | 1u;
      const u8 *tile_charbase = &vram[tilecntrl * 16384u];
      if (saturate)
        cgba_render_4bpp_strip_blend_sat_sh4(
          map_ptr, todraw, dest_ptr, tile_charbase, rke, term_a, second_ptr);
      else
        cgba_render_4bpp_strip_blend_sh4(
          map_ptr, todraw, dest_ptr, tile_charbase, rke, term_a, second_ptr);
    }
    map_ptr += todraw;
    dest_ptr += todraw * 8u;
#else
    if (saturate) {
      cgba_blend_4bpp_tile_run_c<true>(
        todraw, map_ptr, dest_ptr, tile_base, vflip_off, term_a, evb);
    } else {
      cgba_blend_4bpp_tile_run_c<false>(
        todraw, map_ptr, dest_ptr, tile_base, vflip_off, term_a, evb);
    }
#endif
    rem -= todraw * 8u;
    pixel_run -= todraw * 8u;
    if (pixel_run == 0u) {
      map_ptr = second_ptr;
      pixel_run = 256u;
    }
  }

  if (rem) {
    if (saturate)
      cgba_blend_4bpp_part_pixels<true>(
        dest_ptr, 0u, rem, *map_ptr, tile_base, vflip_off, term_a, evb);
    else
      cgba_blend_4bpp_part_pixels<false>(
        dest_ptr, 0u, rem, *map_ptr, tile_base, vflip_off, term_a, evb);
  }
}

static union {
  u16 w[256];
  u8 b[512];
} cgba_nib_unpack_sh1 __attribute__((aligned(32)));
static bool cgba_nib_unpack_sh1_ready = false;

static inline const u16 *cgba_nib_unpack_sh1_get(void)
{
  if (__builtin_expect(!cgba_nib_unpack_sh1_ready, 0)) {
    for (u32 i = 0; i < 256u; i++) {
      cgba_nib_unpack_sh1.b[i * 2u + 0u] = (u8)((i & 0xFu) << 1);
      cgba_nib_unpack_sh1.b[i * 2u + 1u] = (u8)((i >> 4) << 1);
    }
    cgba_nib_unpack_sh1_ready = true;
  }
  return cgba_nib_unpack_sh1.w;
}

struct cgba_fused_blend_lut_slot {
  u16 lut[256];
  u32 key;
  u32 pal_epoch;
};
static cgba_fused_blend_lut_slot cgba_fused_blend_luts[4]
  __attribute__((aligned(32)));
static u32 cgba_fused_blend_lut_rr = 0u;
static cgba_fused_blend_lut_slot *cgba_fused_blend_last = &cgba_fused_blend_luts[0];

static __attribute__((noinline)) const u16 *cgba_fused_blend_lut_miss(
  u32 sub_cand, u32 sub_base, u32 eva, u32 evb, bool bd_is_tgt2)
{
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
  const u32 pal_epoch = cgba_term_a_epoch;
  const u32 key = (sub_cand & 0xFu) | ((sub_base & 0xFu) << 4) |
                  (eva << 8) | (evb << 13) | (bd_is_tgt2 ? (1u << 18) : 0u);
  for (u32 s = 0; s < 4u; s++) {
    if (cgba_fused_blend_luts[s].pal_epoch == pal_epoch &&
        cgba_fused_blend_luts[s].key == key) {
      cgba_fused_blend_last = &cgba_fused_blend_luts[s];
      return cgba_fused_blend_luts[s].lut;
    }
  }

  u32 idx = cgba_fused_blend_lut_rr++ & 3u;
  cgba_fused_blend_lut_slot *slot = &cgba_fused_blend_luts[idx];
  u16 *lut = slot->lut;
  const u16 bd_col = palette_ram_converted[0];
  const u16 *base_pal = &palette_ram_converted[(sub_base & 0xFu) << 4];
  const u16 *cand_pal = &palette_ram_converted[(sub_cand & 0xFu) << 4];
  const u32 *cand_ta = &term_a[(sub_cand & 0xFu) << 4];
  const bool saturate = (eva + evb) > 16u;

  lut[0] = bd_col;
  for (u32 bp = 1u; bp < 16u; bp++)
    lut[bp] = base_pal[bp];

  if (saturate) {
    for (u32 cp = 1u; cp < 16u; cp++) {
      u16 *row = &lut[cp << 4];
      u32 ta = cand_ta[cp];
      row[0] = bd_is_tgt2 ? cgba_blend_rgb565_direct<true>(ta, bd_col, evb)
                          : cand_pal[cp];
      for (u32 bp = 1u; bp < 16u; bp++)
        row[bp] = cgba_blend_rgb565_direct<true>(ta, base_pal[bp], evb);
    }
  } else {
    for (u32 cp = 1u; cp < 16u; cp++) {
      u16 *row = &lut[cp << 4];
      u32 ta = cand_ta[cp];
      row[0] = bd_is_tgt2 ? cgba_blend_rgb565_direct<false>(ta, bd_col, evb)
                          : cand_pal[cp];
      for (u32 bp = 1u; bp < 16u; bp++)
        row[bp] = cgba_blend_rgb565_direct<false>(ta, base_pal[bp], evb);
    }
  }
  slot->key = key;
  slot->pal_epoch = pal_epoch;
  cgba_fused_blend_last = slot;
  return lut;
}

static inline const u16 *cgba_fused_blend_lut_get(
  u32 sub_cand, u32 sub_base, u32 eva, u32 evb, bool bd_is_tgt2)
{
#if CGBA_PALETTE_BLEND_BIT
  const u32 key = (sub_cand & 0xFu) | ((sub_base & 0xFu) << 4) |
                  (eva << 8) | (evb << 13) | (bd_is_tgt2 ? (1u << 18) : 0u);
  cgba_fused_blend_lut_slot *last = cgba_fused_blend_last;
  if (__builtin_expect((palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND) == 0u &&
                       last->pal_epoch == cgba_term_a_epoch &&
                       last->key == key, 1))
    return last->lut;
#endif
  return cgba_fused_blend_lut_miss(sub_cand, sub_base, eva, evb, bd_is_tgt2);
}

#ifndef CGBA_SH4_ASM_4BPP
static s32 cgba_unpack_4bpp_nibs_c(
  const u16 *map_ptr, u32 todraw, u8 *dst_nibs,
  const u8 *tile_base, s32 vflip_off, s32 expected_subpal)
{
  s32 sub = expected_subpal;
  for (u32 t = 0; t < todraw; t++, dst_nibs += 8) {
    u32 tile = eswap16(map_ptr[t]);
    const u8 *row = tile_base + ((tile & 0x3FFu) << 5);
    if (tile & 0x800u)
      row += vflip_off;
    if (*(const u32 *)row == 0u) {
      for (u32 i = 0; i < 8u; i++)
        dst_nibs[i] = 0u;
      continue;
    }
    s32 tsub = (s32)((tile >> 12) & 0xFu);
    if (sub < 0)
      sub = tsub;
    else if (sub != tsub)
      return -1;
    if (tile & 0x400u) {
      for (u32 i = 0; i < 4u; i++) {
        u32 b = row[3u - i];
        dst_nibs[i * 2u + 0u] = (u8)((b >> 4) << 1);
        dst_nibs[i * 2u + 1u] = (u8)((b & 0xFu) << 1);
      }
    } else {
      for (u32 i = 0; i < 4u; i++) {
        u32 b = row[i];
        dst_nibs[i * 2u + 0u] = (u8)((b & 0xFu) << 1);
        dst_nibs[i * 2u + 1u] = (u8)((b >> 4) << 1);
      }
    }
  }
  return (sub < 0) ? 16 : sub;
}
#endif

static inline void cgba_fused_blend_part_pixels(
  u16 *dst, const u8 *base_nibs, u32 col_start, u32 col_end,
  u16 raw_map, const u8 *tile_base, s32 vflip_off, const u16 *lut)
{
  u32 tile = eswap16(raw_map);
  const u8 *row = tile_base + ((tile & 0x3FFu) << 5);
  if (tile & 0x800u)
    row += vflip_off;
  const u32 width = col_end - col_start;
  if (*(const u32 *)row == 0u) {
    for (u32 i = 0; i < width; i++)
      dst[i] = lut[base_nibs[i] >> 1];
    return;
  }
  if (tile & 0x400u) {
    for (u32 i = 0; i < width; i++) {
      u32 sc = 7u - (col_start + i);
      u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
      dst[i] = lut[(pix << 4) | (base_nibs[i] >> 1)];
    }
  } else {
    for (u32 i = 0; i < width; i++) {
      u32 c = col_start + i;
      u32 pix = (row[c >> 1] >> ((c & 1u) << 2)) & 0xFu;
      dst[i] = lut[(pix << 4) | (base_nibs[i] >> 1)];
    }
  }
}

static __attribute__((noinline)) bool cgba_try_render_fused_4bpp_over_4bpp(
  u32 base_bg, u32 cand_bg, u32 start, u32 end, u16 *scanline,
  u32 eva, u32 evb, bool bd_is_tgt2)
{
  if (start >= end)
    return true;
  const u32 vcount = read_ioreg(REG_VCOUNT);

  /* Unpack base_bg into tile-aligned 4-byte-aligned buffer (base_pix << 1). */
  u32 b_cnt = read_ioreg(REG_BGxCNT(base_bg));
  u32 b_msize = (b_cnt >> 14) & 0x03u;
  u32 b_voff = (vcount + read_ioreg(REG_BGxVOFS(base_bg))) & 511u;
  u32 b_hoff = (start + read_ioreg(REG_BGxHOFS(base_bg))) & 511u;
  u32 b_sbb = (b_cnt >> 8) & 0x1Fu;
  u32 b_vp = (b_voff & 7u) * 4u;
  const u8 *b_tbase = &vram[((b_cnt >> 2) & 0x03u) * 16384u + b_vp];
  u16 *b_mbase = (u16 *)&vram[b_sbb * 2048u];
  if ((b_msize & 0x02u) && (b_voff >= 256u))
    b_mbase += (map_widths[b_msize] >> 3) * 32u;
  b_mbase += ((b_voff & 255u) >> 3) * 32u;

  u16 *b_map1, *b_map2;
  if (b_msize & 0x01u) {
    if (b_hoff >= 256u) {
      b_map1 = b_mbase + 1024u;
      b_map2 = b_mbase;
      b_hoff -= 256u;
    } else {
      b_map1 = b_mbase;
      b_map2 = b_mbase + 1024u;
    }
  } else {
    b_map1 = b_map2 = b_mbase;
    b_hoff &= 255u;
  }
  b_map1 += b_hoff >> 3;
  s32 b_vflip = (s32)((7u - (b_voff & 7u)) * 4u) - (s32)b_vp;
  u32 b_thoff = b_hoff & 7u;
  u32 b_total_tiles = (b_thoff + (end - start) + 7u) >> 3;
  u32 b_span1 = MIN(b_total_tiles, (256u - (b_hoff & ~7u)) >> 3);
  u32 b_span2 = b_total_tiles - b_span1;

  u8 base_nibs_buf[256] __attribute__((aligned(4)));
#ifdef CGBA_SH4_ASM_4BPP
  const u16 *unpack_lut = cgba_nib_unpack_sh1_get();
  s32 sub_base = cgba_unpack_4bpp_nibs_sh4(
    b_map1, b_span1, base_nibs_buf, b_tbase, b_vflip, unpack_lut, -1);
  if (sub_base < 0)
    return false;
  if (b_span2 > 0u) {
    sub_base = cgba_unpack_4bpp_nibs_sh4(
      b_map2, b_span2, base_nibs_buf + b_span1 * 8u, b_tbase, b_vflip,
      unpack_lut, (sub_base == 16) ? -1 : sub_base);
    if (sub_base < 0)
      return false;
  }
#else
  s32 sub_base = cgba_unpack_4bpp_nibs_c(
    b_map1, b_span1, base_nibs_buf, b_tbase, b_vflip, -1);
  if (sub_base < 0)
    return false;
  if (b_span2 > 0u) {
    sub_base = cgba_unpack_4bpp_nibs_c(
      b_map2, b_span2, base_nibs_buf + b_span1 * 8u, b_tbase, b_vflip,
      (sub_base == 16) ? -1 : sub_base);
    if (sub_base < 0)
      return false;
  }
#endif
  if (sub_base == 16)
    sub_base = 0;

  /* Walk cand_bg and blend directly with base_nibs via cached 16x16 LUTs. */
  u32 c_cnt = read_ioreg(REG_BGxCNT(cand_bg));
  u32 c_msize = (c_cnt >> 14) & 0x03u;
  u32 c_voff = (vcount + read_ioreg(REG_BGxVOFS(cand_bg))) & 511u;
  u32 c_hoff = (start + read_ioreg(REG_BGxHOFS(cand_bg))) & 511u;
  u32 c_sbb = (c_cnt >> 8) & 0x1Fu;
  u32 c_vp = (c_voff & 7u) * 4u;
  const u8 *c_tbase = &vram[((c_cnt >> 2) & 0x03u) * 16384u + c_vp];
  u16 *c_mbase = (u16 *)&vram[c_sbb * 2048u];
  if ((c_msize & 0x02u) && (c_voff >= 256u))
    c_mbase += (map_widths[c_msize] >> 3) * 32u;
  c_mbase += ((c_voff & 255u) >> 3) * 32u;

  u16 *c_map, *c_map2;
  if (c_msize & 0x01u) {
    if (c_hoff >= 256u) {
      c_map = c_mbase + 1024u;
      c_map2 = c_mbase;
      c_hoff -= 256u;
    } else {
      c_map = c_mbase;
      c_map2 = c_mbase + 1024u;
    }
  } else {
    c_map = c_map2 = c_mbase;
    c_hoff &= 255u;
  }
  c_map += c_hoff >> 3;
  s32 c_vflip = (s32)((7u - (c_voff & 7u)) * 4u) - (s32)c_vp;

  const u8 *nibs = base_nibs_buf + b_thoff;
  u16 *dest_ptr = scanline + start;
  u32 rem = end - start;
  u32 pixel_run = 256u - c_hoff;
  u32 c_thoff = c_hoff & 7u;
  u32 cur_sub_cand = 0xFFFFFFFFu;
  const u16 *cur_lut = NULL;

  if (c_thoff) {
    u32 todraw = MIN(8u - c_thoff, rem);
    u16 raw = *c_map++;
    u32 tsub = (eswap16(raw) >> 12) & 0xFu;
    cur_lut = cgba_fused_blend_lut_get(tsub, (u32)sub_base, eva, evb, bd_is_tgt2);
    cur_sub_cand = tsub;
    cgba_fused_blend_part_pixels(
      dest_ptr, nibs, c_thoff, c_thoff + todraw, raw, c_tbase, c_vflip, cur_lut);
    dest_ptr += todraw;
    nibs += todraw;
    rem -= todraw;
    pixel_run -= todraw;
    if (pixel_run == 0u) {
      c_map = c_map2;
      pixel_run = 256u;
    }
  }

  while (rem >= 8u) {
    u32 todraw = MIN(rem, pixel_run) >> 3;
    u32 span_tiles = todraw;
    while (todraw > 0u) {
      u32 tsub = (eswap16(*c_map) >> 12) & 0xFu;
      if (tsub != cur_sub_cand) {
        cur_lut = cgba_fused_blend_lut_get(tsub, (u32)sub_base, eva, evb, bd_is_tgt2);
        cur_sub_cand = tsub;
      }
#ifdef CGBA_SH4_ASM_4BPP
      cgba_fused_blend_params fp;
      fp.map_ptr = c_map;
      fp.base_nibs = nibs;
      fp.dest_ptr = dest_ptr;
      fp.lut = cur_lut;
      fp.tile_base = c_tbase;
      fp.vflip_off = c_vflip;
      fp.todraw = todraw;
      fp.expected_pal = tsub << 4;
      u32 done = cgba_render_4bpp_span_fused_blend_sh4(&fp);
      c_map += done;
      nibs += done * 8u;
      dest_ptr += done * 8u;
      todraw -= done;
#else
      cgba_fused_blend_part_pixels(
        dest_ptr, nibs, 0u, 8u, *c_map++, c_tbase, c_vflip, cur_lut);
      nibs += 8u;
      dest_ptr += 8u;
      todraw--;
#endif
    }
    rem -= span_tiles * 8u;
    pixel_run -= span_tiles * 8u;
    if (pixel_run == 0u) {
      c_map = c_map2;
      pixel_run = 256u;
    }
  }

  if (rem) {
    u16 raw = *c_map;
    u32 tsub = (eswap16(raw) >> 12) & 0xFu;
    if (tsub != cur_sub_cand)
      cur_lut = cgba_fused_blend_lut_get(tsub, (u32)sub_base, eva, evb, bd_is_tgt2);
    cgba_fused_blend_part_pixels(
      dest_ptr, nibs, 0u, rem, raw, c_tbase, c_vflip, cur_lut);
  }
  return true;
}

static inline bool cgba_text_bg_row_is_opaque(u32 layer, u32 vcount)
{
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  if (bg_control & 0x80u)
    return false;
  if ((bg_control & 0x40u) && ((read_ioreg(REG_MOSAIC) & 0xFFu) != 0u))
    return false;
  u32 map_size = (bg_control >> 14) & 0x03u;
  u32 voffset = (vcount + read_ioreg(REG_BGxVOFS(layer))) & 511u;
  u32 base_block = (bg_control >> 8) & 0x1Fu;
  u32 cbb = (bg_control >> 2) & 0x03u;
  u32 sb0 = base_block +
            (((map_size & 0x02u) && (voffset >= 256u))
               ? ((map_size & 0x01u) ? 2u : 1u) : 0u);
  u32 row_bit = 1u << ((voffset & 255u) >> 3);
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  return cgba_sb_row_is_all_opaque(cbb, sb0, row_bit) &&
         (!(map_size & 0x01u) || cgba_sb_row_is_all_opaque(cbb, sb0 + 1u, row_bit));
#else
  (void)cbb; (void)sb0; (void)row_bit;
  return false;
#endif
}

static bool try_render_direct_single_bg_blend(
  u32 start, u32 end, u16 *scanline, u32 enable_flags, u32 vis_layers, u16 bldcnt)
{
  u32 tgt1_active = bldcnt & vis_layers;
  if ((tgt1_active & 0x10u) != 0u || tgt1_active == 0u ||
      (tgt1_active & (tgt1_active - 1u)) != 0u)
    return false;

  u32 cand = (u32)__builtin_ctz(tgt1_active);
  u32 video_mode = read_ioreg(REG_DISPCNT) & 0x07u;
  if (video_mode >= 3u)
    return false;
  if (video_mode >= 1u && cand >= 2u)
    return false;
  u32 cand_cnt = read_ioreg(REG_BGxCNT(cand));
  if ((cand_cnt & 0x80u) != 0u)
    return false;
  if ((cand_cnt & 0x40u) != 0u && (read_ioreg(REG_MOSAIC) & 0xFFu) != 0u)
    return false;

  u32 tgt2_raw = (bldcnt >> 8) & 0x3Fu;
  bool obj_enabled = (enable_flags & 0x10u) != 0u;
  u32 vcount = read_ioreg(REG_VCOUNT);
  bool bd_is_tgt2 = (tgt2_raw & 0x20u) != 0u;
  bool bd_covered = bd_is_tgt2;
  s32 cand_pos = -1;
  u32 below_active_cnt = 0u;
  s32 sole_base_bg = -1;

  for (u32 i = 0; i < layer_count; i++) {
    u32 layer = layer_order[i];
    bool is_obj = (layer & 0x04u) != 0u;
    if (!is_obj && layer == cand) {
      cand_pos = (s32)i;
      break;
    }
    if (is_obj) {
      if (obj_enabled && (obj_row_prio_mask[vcount] & (1u << (layer & 3u))) != 0u) {
        if (!(tgt2_raw & 0x10u))
          return false;
        below_active_cnt++;
      }
    } else if (((1u << layer) & enable_flags) != 0u) {
      if (!((tgt2_raw >> layer) & 1u))
        return false;
      below_active_cnt++;
      sole_base_bg = (s32)layer;
    }
  }
  if (cand_pos < 0)
    return false;

  u32 bldalpha = read_ioreg(REG_BLDALPHA);
  u32 eva = MIN(16u, (bldalpha >> 0) & 0x1Fu);
  u32 evb = MIN(16u, (bldalpha >> 8) & 0x1Fu);

  /* Fused 4bpp-over-4bpp direct 16x16 LUT blend: handles both bd_is_tgt2
   * true and false without needing an opaque-row proof on base_bg. */
  bool fused_done = false;
  if (below_active_cnt == 1u && sole_base_bg >= 0 &&
      !(video_mode >= 1u && sole_base_bg >= 2)) {
    u32 base_cnt = read_ioreg(REG_BGxCNT((u32)sole_base_bg));
    if ((base_cnt & 0x80u) == 0u &&
        !((base_cnt & 0x40u) != 0u && (read_ioreg(REG_MOSAIC) & 0xFFu) != 0u)) {
      fused_done = cgba_try_render_fused_4bpp_over_4bpp(
        (u32)sole_base_bg, cand, start, end, scanline, eva, evb, bd_is_tgt2);
    }
  }

  if (!fused_done) {
    if (!bd_covered) {
      for (u32 i = 0; i < (u32)cand_pos; i++) {
        u32 layer = layer_order[i];
        if ((layer & 0x04u) == 0u && ((1u << layer) & enable_flags) != 0u &&
            !(video_mode >= 1u && layer >= 2u) &&
            cgba_text_bg_row_is_opaque(layer, vcount)) {
          bd_covered = true;
          break;
        }
      }
      if (!bd_covered)
        return false;
    }

    u32 base_done = 0u;
    for (u32 i = 0; i < (u32)cand_pos; i++) {
      u32 layer = layer_order[i];
      bool is_obj = (layer & 0x04u) != 0u;
      if (is_obj) {
        if (obj_enabled) {
          if (!base_done)
            fill_line_background<FULLCOLOR, u16>(start, end, scanline);
          render_scanline_objs<u16, FULLCOLOR>(
            layer & 3u, start, end, scanline, &palette_ram_converted[0x100]);
          base_done = 1u;
        }
      } else if (((1u << layer) & enable_flags) != 0u) {
        bool is_affine = (video_mode >= 1u) && (layer >= 2u);
        if (is_affine) {
          if (base_done)
            render_scanline_affine<u16, FULLCOLOR, false>(
              layer, start, end, scanline, palette_ram_converted);
          else
            render_scanline_affine<u16, FULLCOLOR, true>(
              layer, start, end, scanline, palette_ram_converted);
        } else {
          if (base_done)
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, start, end, scanline, palette_ram_converted);
          else
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, start, end, scanline, palette_ram_converted);
        }
        base_done = 1u;
      }
    }
    if (!base_done)
      fill_line_background<FULLCOLOR, u16>(start, end, scanline);

    cgba_render_scanline_text_4bpp_blend(cand, start, end, scanline, eva, evb);
  }

  for (u32 i = (u32)cand_pos + 1u; i < layer_count; i++) {
    u32 layer = layer_order[i];
    bool is_obj = (layer & 0x04u) != 0u;
    if (is_obj) {
      if (obj_enabled)
        render_scanline_objs<u16, FULLCOLOR>(
          layer & 3u, start, end, scanline, &palette_ram_converted[0x100]);
    } else if (((1u << layer) & enable_flags) != 0u) {
      bool is_affine = (video_mode >= 1u) && (layer >= 2u);
      if (is_affine)
        render_scanline_affine<u16, FULLCOLOR, false>(
          layer, start, end, scanline, palette_ram_converted);
      else
        render_scanline_text<u16, FULLCOLOR, false>(
          layer, start, end, scanline, palette_ram_converted);
    }
  }
  return true;
}

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
        if (!obj_blend && (dispcnt & 0x07u) < 3u &&
            try_render_direct_single_bg_blend(
              start, end, scanline, enable_flags, vis_layers, bldcnt))
          return;
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

static const bitmap_layer_render_struct idx32_mode4_bmrend[2] =
  bitmap_layer_render_functions(INDXCOLOR, u32, 4, u8, 240, 160);

/* Modes 3 and 5 are direct-color and only ever dispatched through FULLCOLOR;
 * specializing avoids instantiating 48 unreachable Mode 3/5 indexed/stacked
 * template variants in .text. */
template<rendtype bgmode, typename dsttype>
struct bitmap_mode_lut {
  static inline const bitmap_layer_render_struct *get(unsigned modeidx, u32 mmode) {
    (void)modeidx;
    static const bitmap_layer_render_struct mode4_renderers[2] =
      bitmap_layer_render_functions(bgmode, dsttype, 4, u8, 240, 160);
    return &mode4_renderers[mmode];
  }
};

template<>
struct bitmap_mode_lut<INDXCOLOR, u32> {
  static inline const bitmap_layer_render_struct *get(unsigned modeidx, u32 mmode) {
    (void)modeidx;
    return &idx32_mode4_bmrend[mmode];
  }
};

template<>
struct bitmap_mode_lut<FULLCOLOR, u16> {
  static inline const bitmap_layer_render_struct *get(unsigned modeidx, u32 mmode) {
    static const bitmap_layer_render_struct renderers[3][2] =
    {
      bitmap_layer_render_functions(FULLCOLOR, u16, 3, u16, 240, 160),
      bitmap_layer_render_functions(FULLCOLOR, u16, 4, u8,  240, 160),
      bitmap_layer_render_functions(FULLCOLOR, u16, 5, u16, 160, 128)
    };
    return &renderers[modeidx][mmode];
  }
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

  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;
  u32 bg_control = read_ioreg(REG_BG2CNT);
  u32 mmode = ((bg_control & 0x40) && (mosamount != 0)) ? 1 : 0;

  unsigned modeidx = (dispcnt & 0x07) - 3;
  const bitmap_layer_render_struct *mode_rend =
    bitmap_mode_lut<bgmode, dsttype>::get(modeidx, mmode);
  const bitmap_layer_render_struct *idxm_rend = &idx32_mode4_bmrend[mmode];

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
u32 cgba_video_frame_dirty_row_mask[5] = {
  0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
};

static inline void cgba_video_mark_row_dirty(u32 y)
{
  if (__builtin_expect(y < 160u, 1))
    cgba_video_frame_dirty_row_mask[y >> 5] |= 1u << (y & 31u);
}

static inline void cgba_video_mark_rows_dirty(u32 y0, u32 y1)
{
  if (y1 > 160u)
    y1 = 160u;
  while (y0 < y1) {
    u32 w = y0 >> 5;
    u32 lo = y0 & 31u;
    u32 hi = (w + 1u) << 5;
    if (hi > y1)
      hi = y1;
    u32 n = hi - y0;
    u32 bits = (n == 32u) ? 0xFFFFFFFFu : (((1u << n) - 1u) << lo);
    cgba_video_frame_dirty_row_mask[w] |= bits;
    y0 = hi;
  }
}
static u8  cgba_sl_row_valid[160];
static u8  cgba_sl_row_has_obj[160];
static cgba_frame_io_snap cgba_sl_frame_io;
static u16 cgba_sl_pal_snap[512] __attribute__((aligned(32)));
static u16 cgba_sl_oam_snap[512] __attribute__((aligned(32)));
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
static inline void cgba_blend_caches_invalidate(void);

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
    cgba_blend_caches_invalidate();
  }
  cgba_video_frame_dirty_rows = 160;
  cgba_video_frame_dirty_row_mask[0] = 0xFFFFFFFFu;
  cgba_video_frame_dirty_row_mask[1] = 0xFFFFFFFFu;
  cgba_video_frame_dirty_row_mask[2] = 0xFFFFFFFFu;
  cgba_video_frame_dirty_row_mask[3] = 0xFFFFFFFFu;
  cgba_video_frame_dirty_row_mask[4] = 0xFFFFFFFFu;
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
      cgba_memeq_u32(cgba_sl_oam_snap, oam_ram, 256u)) {
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
  if (!cgba_memeq_u32(cgba_sl_pal_snap, palette_ram_converted, 128u)) {
    memcpy(cgba_sl_pal_snap, palette_ram_converted, 256 * sizeof(u16));
    *bg_changed = true;
  }
  if (!cgba_memeq_u32(cgba_sl_pal_snap + 256, palette_ram_converted + 256, 128u)) {
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

/* Unified horizontal tile-span plan for arbitrary [x0, x1) slices (including
 * full-width [0, 240)) across 256px / 512px text BG screenblocks. */
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

static inline void cgba_bg_hspan_from_plan(
  const cgba_mode0_bg_plan *p, cgba_bg_hspan_plan *out)
{
  u32 thoff = p->tile_hoff;
  u32 left_w = thoff ? (8u - thoff) : 0u;
  out->first_byte_off = p->first_byte_off;
  out->map2_byte_off = p->map2_byte_off;
  out->cp = p->counts_packed;
  out->x0 = 0u;
  out->mid_x = (u16)left_w;
  out->right_x = (u16)(240u - thoff);
  out->thoff = (u8)thoff;
  out->left_w = (u8)left_w;
  out->right_w = (u8)thoff;
  out->mid_aligned = (left_w & 1u) == 0u ? 1u : 0u;
}

/* True when every tile in this strip's tile row references tile 0 and tile 0's
 * rows [start_r, start_r + k) are completely transparent. */
static inline bool cgba_4bpp_strip_row_zero_skip(
  const cgba_mode0_bg_plan *p, u32 vhalf, u32 voffset, u32 start_r, u32 k)
{
  if (!(p->flags & 2u))
    return false;
  u32 vtile_row = (voffset & 255u) >> 3;
  if (p->row_nonzero[vhalf] & (1u << vtile_row))
    return false;
  const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
  u32 any = 0u;
  for (u32 r = 0u; r < k; r++)
    any |= t0[r];
  return any == 0u;
}

struct alignas(32) cgba_mode0_state {
  cgba_mode0_bg_plan plans[4];
  u8   lorder[16][8];
  u8   lcount[16];
  u32  lorder_key;
  u32  used_sb_mask;
  u32  bg_static_key[4];
  u32  bg_sb_mask[4];
  u32  row_all_empty[4][2];  /* bit r = 1 iff every tile on row r of vhalf 0/1 is all-empty */
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
  u8   bright_effect;        /* 0 = none, COL_EFFECT_BRIGHT(2) or COL_EFFECT_DARK(3) */
  u8   bright_evy;           /* clamped 1..16 */
  u8   bright_tgt1_mask;     /* BLDCNT[5:0] */
  u8   bright_win_active;    /* 1 when single-window effect-only cutout is active */
  u8   bright_win_eff_in;    /* 0 = outside folded / inside normal, 1 = inside folded */
  u8   bright_win_x0;        /* 0..240 */
  u8   bright_win_x1;        /* bright_win_x0..240 */
  u8   bright_win_y0;        /* 0..160 */
  u8   bright_win_y1;        /* bright_win_y0..160 */
  u8   video_mode;           /* 0, 1, or 2 */
  u8   obj_alpha_direct_pmask; /* 4-bit mask of OBJ prios safe for direct RGB565 blend */
  u8   obj_alpha_eva;        /* clamped 0..16 for OBJ_MOD_SEMITRAN direct blend */
  u8   obj_alpha_evb;        /* clamped 0..16 for OBJ_MOD_SEMITRAN direct blend */
  u8   win_multi_active;     /* 1 when WIN0/WIN1 (no OBJWIN) multi-span fast path is active */
  u8   bg_prio[4];           /* cached BG0..BG3 priorities for multi-span windowing */
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

static inline bool cgba_row_alpha_ok(u32 y)
{
  if (!cgba_mode0_obj_alpha_can_blend || !obj_alpha_count[y])
    return true;
  if (!cgba_m0.obj_alpha_direct_pmask || obj_alpha_complex[y])
    return false;
  return (obj_alpha_prio_mask[y] & ~cgba_m0.obj_alpha_direct_pmask) == 0u;
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

static __attribute__((noinline)) void cgba_rebuild_bg_plans(u32 video_mode, u16 dispcnt)
{
  const u32 mode_layers = active_layers[video_mode];
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
  u8 bright_effect = 0u;
  u8 bright_evy = 0u;
  u8 bright_tgt1_mask = 0u;
  u8 bright_win_active = 0u;
  u8 bright_win_eff_in = 0u;
  u8 bright_win_x0 = 0u;
  u8 bright_win_x1 = 0u;
  u8 bright_win_y0 = 0u;
  u8 bright_win_y1 = 0u;
  u8 obj_alpha_direct_pmask = 0u;
  u8 obj_alpha_eva = 0u;
  u8 obj_alpha_evb = 0u;
  u8 win_multi_active = 0u;
  u32 bg_prio[4] = { 0, 0, 0, 0 };
  bool have_bg_prio = false;
  u16 bldcnt = read_ioreg(REG_BLDCNT);

  if (full_win && (enables & 0x1Fu) != 0u) {
    vis_layers = ((dispcnt >> 8) & mode_layers) & (enables & 0x1Fu);
    obj_alpha_can_blend = (vis_layers & 0x10u) != 0u &&
                          (((bldcnt >> 8) & (vis_layers | 0x20u)) != 0u);
    if (!(enables & 0x20u)) {
      no_effect = true;
      no_effect_no_obj = true;
    } else {
      no_effect = cgba_bldcnt_is_noop(bldcnt, vis_layers);
      no_effect_no_obj = no_effect ||
                         cgba_bldcnt_is_noop(bldcnt, vis_layers & ~0x10u);
      /* Direct-to-RGB565 single-1st-target BG alpha-blend fast path (Mode 0):
       * When exactly one visible BG layer is a 1st target under COL_EFFECT_BLEND,
       * and every layer strictly below it (including Backdrop) is a 2nd target,
       * all lower layers can render directly in 16-bit FULLCOLOR and the 1st-target
       * BG can alpha-blend directly onto the RGB565 destination buffer. */
      if (video_mode == 0u && !no_effect_no_obj &&
          (((bldcnt >> 6) & 3u) == COL_EFFECT_BLEND)) {
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
      else if (!no_effect) {
        u32 eff = (bldcnt >> 6) & 3u;
        if (eff == COL_EFFECT_BRIGHT || eff == COL_EFFECT_DARK) {
          u32 evy = MIN(16u, (u32)(read_ioreg(REG_BLDY) & 0x1Fu));
          if (evy > 0u && (bldcnt & (vis_layers | 0x20u)) != 0u) {
            bright_effect = (u8)eff;
            bright_evy = (u8)evy;
            bright_tgt1_mask = (u8)(bldcnt & 0x3Fu);
          }
        }
      }
    }
  }
  else if (!full_win && (win_ctrl == 1u || win_ctrl == 2u ||
                         (win_ctrl == 3u &&
                          cgba_window_is_empty(read_ioreg(REG_WIN1H),
                                               read_ioreg(REG_WIN1V))))) {
    /* Single-window effect-only cutout: same layer mask inside & outside,
     * differing only in color-effect enable bit (e.g. dialogue/prologue box
     * darkened outside WIN0 with COL_EFFECT_DARK/BRIGHT). */
    bool is_win1 = (win_ctrl == 2u);
    u16 wh = is_win1 ? read_ioreg(REG_WIN1H) : read_ioreg(REG_WIN0H);
    u16 wv = is_win1 ? read_ioreg(REG_WIN1V) : read_ioreg(REG_WIN0V);
    u32 en_in = is_win1 ? ((read_ioreg(REG_WININ) >> 8) & 0x3Fu)
                        : (read_ioreg(REG_WININ) & 0x3Fu);
    u32 en_out = read_ioreg(REG_WINOUT) & 0x3Fu;
    u32 w_lraw = wh >> 8;
    u32 w_rraw = wh & 0xFFu;
    u32 w_top = wv >> 8;
    u32 w_bot = wv & 0xFFu;
    if ((en_in & 0x1Fu) == (en_out & 0x1Fu) &&
        (en_in & 0x1Fu) != 0u &&
        w_lraw < w_rraw &&
        w_top <= 227u && (w_bot > 227u || w_top <= w_bot)) {
      vis_layers = ((dispcnt >> 8) & mode_layers) & (en_in & 0x1Fu);
      obj_alpha_can_blend = (vis_layers & 0x10u) != 0u &&
                            (((bldcnt >> 8) & (vis_layers | 0x20u)) != 0u);
      u32 eff = (bldcnt >> 6) & 3u;
      if (((en_in ^ en_out) & 0x20u) == 0u) {
        /* Identical enable bits inside and outside -> effectively full_win. */
        if (!(en_out & 0x20u)) {
          no_effect = true;
          no_effect_no_obj = true;
        } else if (cgba_bldcnt_is_noop(bldcnt, vis_layers)) {
          no_effect = true;
          no_effect_no_obj = true;
        } else if (eff == COL_EFFECT_BRIGHT || eff == COL_EFFECT_DARK) {
          u32 evy = MIN(16u, (u32)(read_ioreg(REG_BLDY) & 0x1Fu));
          if (evy > 0u && (bldcnt & (vis_layers | 0x20u)) != 0u) {
            bright_effect = (u8)eff;
            bright_evy = (u8)evy;
            bright_tgt1_mask = (u8)(bldcnt & 0x3Fu);
          }
        }
      } else if (eff == COL_EFFECT_NONE ||
                 cgba_bldcnt_is_noop(bldcnt, vis_layers)) {
        no_effect = true;
        no_effect_no_obj = true;
      } else if (eff == COL_EFFECT_BRIGHT || eff == COL_EFFECT_DARK) {
        u32 evy = MIN(16u, (u32)(read_ioreg(REG_BLDY) & 0x1Fu));
        if (evy > 0u && (bldcnt & (vis_layers | 0x20u)) != 0u) {
          bright_effect = (u8)eff;
          bright_evy = (u8)evy;
          bright_tgt1_mask = (u8)(bldcnt & 0x3Fu);
          bright_win_active = 1u;
          bright_win_eff_in = (en_in & 0x20u) ? 1u : 0u;
          bright_win_x0 = (u8)MIN(240u, w_lraw);
          bright_win_x1 = (u8)MIN(240u, w_rraw);
          bright_win_y0 = (w_bot > 227u) ? 0u : (u8)MIN(160u, w_top);
          bright_win_y1 = (w_bot > 227u) ? 160u : (u8)MIN(160u, w_bot);
        }
      }
    }
  }

  /* Phase 3D: WIN0/WIN1 (without OBJWIN) differing layer masks -> multi-span strip path. */
  if (!no_effect_no_obj && blend_bg >= 4u && bright_effect == 0u &&
      !full_win && (win_ctrl >= 1u && win_ctrl <= 3u)) {
    win_multi_active = 1u;
    vis_layers = (dispcnt >> 8) & mode_layers;
  }

  cgba_m0.video_mode = (u8)video_mode;
  cgba_mode0_fast_fullframe = no_effect;
  cgba_mode0_fast_no_obj = no_effect_no_obj;
  cgba_mode0_obj_alpha_can_blend = obj_alpha_can_blend;
  cgba_mode0_obj_enabled = (vis_layers & 0x10u) != 0u;
  cgba_m0.blend_bg_layer = blend_bg;
  cgba_m0.blend_forbid_omask = blend_forbid;
  cgba_m0.blend_eva = blend_eva;
  cgba_m0.blend_evb = blend_evb;
  cgba_m0.bright_effect = bright_effect;
  cgba_m0.bright_evy = bright_evy;
  cgba_m0.bright_tgt1_mask = bright_tgt1_mask;
  cgba_m0.bright_win_active = bright_win_active;
  cgba_m0.bright_win_eff_in = bright_win_eff_in;
  cgba_m0.bright_win_x0 = bright_win_x0;
  cgba_m0.bright_win_x1 = bright_win_x1;
  cgba_m0.bright_win_y0 = bright_win_y0;
  cgba_m0.bright_win_y1 = bright_win_y1;
  cgba_m0.win_multi_active = win_multi_active;
  cgba_m0.obj_alpha_direct_pmask = 0u;
  cgba_m0.obj_alpha_eva = 0u;
  cgba_m0.obj_alpha_evb = 0u;
  if (!no_effect_no_obj && blend_bg >= 4u && bright_effect == 0u && !win_multi_active)
    return;

  /* Precompute layer draw order for all 16 possible OBJ-priority row masks so
   * Mode 0/1/2 fast-path scanlines skip order_layers() and BGxCNT IO reads. */
  {
    u32 lkey = 0x80000000u | vis_layers | (video_mode << 24);
    for (u32 layer = 0; layer < 4u; layer++) {
      u32 pr = have_bg_prio ? bg_prio[layer]
                            : (u32)(read_ioreg(REG_BGxCNT(layer)) & 0x03u);
      bg_prio[layer] = pr;
      cgba_m0.bg_prio[layer] = (u8)pr;
      lkey |= pr << (8u + (layer << 1));
    }
    if (!win_multi_active && __builtin_expect(lkey != cgba_m0.lorder_key, 0)) {
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

  /* Phase 3C: Direct RGB565 OBJ_MOD_SEMITRAN fast path.
   * Safe for OBJ priority p whenever no BG-level color effect is active
   * (no_effect), and every visible BG below/equal to p PLUS Backdrop is a
   * 2nd target (and no SEMITRAN OBJ on the row overlaps an earlier OBJ). */
  if (no_effect && obj_alpha_can_blend) {
    u32 tgt2_raw = (bldcnt >> 8) & 0x3Fu;
    if (tgt2_raw & 0x20u) {
      for (u32 p = 0; p < 4u; p++) {
        u32 below = 0x20u;
        for (u32 b = 0; b < 4u; b++) {
          if (((vis_layers >> b) & 1u) && bg_prio[b] >= p)
            below |= (1u << b);
        }
        if ((tgt2_raw & below) == below)
          obj_alpha_direct_pmask |= (u8)(1u << p);
      }
      if (obj_alpha_direct_pmask) {
        u32 bldalpha = read_ioreg(REG_BLDALPHA);
        obj_alpha_eva = (u8)MIN(16u, (u32)(bldalpha & 0x1Fu));
        obj_alpha_evb = (u8)MIN(16u, (u32)((bldalpha >> 8) & 0x1Fu));
      }
    }
    cgba_m0.obj_alpha_direct_pmask = obj_alpha_direct_pmask;
    cgba_m0.obj_alpha_eva = obj_alpha_eva;
    cgba_m0.obj_alpha_evb = obj_alpha_evb;
  }

  const u32 mos_bg = read_ioreg(REG_MOSAIC) & 0xFFu;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  const u32 always_base_layer =
    (!win_multi_active && cgba_mode0_lcount[15] > 0u)
      ? (u32)cgba_mode0_lorder[15][0] : 0xFFu;
#endif
  u32 used_sbs = 0u;
  for (u32 layer = 0; layer < 4u; layer++) {
    if (!((vis_layers >> layer) & 1u))
      continue;
    cgba_mode0_bg_plan *p = &cgba_bg_plans[layer];
    if (video_mode >= 1u && layer >= 2u) {
      /* Affine BG (BG2 in Mode 1, BG2/BG3 in Mode 2): flag bit 2 = is_affine. */
      p->flags = 4u;
      cgba_m0.bg_static_key[layer] = 0u;
      cgba_m0.bg_sb_mask[layer] = 0u;
      continue;
    }
    u32 bg_control = read_ioreg(REG_BGxCNT(layer));
    u32 h_raw = read_ioreg(REG_BGxHOFS(layer)) & 511u;
    u16 vofs = read_ioreg(REG_BGxVOFS(layer));
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    bool base_no_blend = (layer == always_base_layer && blend_bg >= 4u &&
                          (bright_effect == 0u ||
                           (((bright_tgt1_mask >> 5) & 1u) ==
                            ((bright_tgt1_mask >> layer) & 1u))));
#else
    bool base_no_blend = false;
#endif
    u32 skey = 0x80000000u | bg_control |
               ((mos_bg != 0u) ? 0x10000u : 0u) |
               (base_no_blend ? 0x20000u : 0u);
    if (__builtin_expect(skey == cgba_m0.bg_static_key[layer], 1)) {
      used_sbs |= cgba_m0.bg_sb_mask[layer];
      if (!(p->flags & (1u | 8u)))
        continue;
      p->vofs = vofs;
      if (__builtin_expect(h_raw == cgba_m0.bg_hofs[layer], 1))
        continue;
      cgba_m0.bg_hofs[layer] = (u16)h_raw;
      cgba_bg_hspan_plan sp;
      cgba_build_bg_hspan_plan(h_raw, cgba_m0.bg_wide[layer], 0u, 240u, &sp);
      p->counts_packed = sp.cp;
      p->first_byte_off = sp.first_byte_off;
      p->map2_byte_off = sp.map2_byte_off;
      p->tile_hoff = sp.thoff;
      continue;
    }

    bool is8bpp = (bg_control & 0x80u) != 0;
    bool has_mosaic = ((bg_control & 0x40u) != 0) && (mos_bg != 0);
    if (has_mosaic) {
      p->flags = 0u;
      cgba_m0.bg_static_key[layer] = skey;
      cgba_m0.bg_sb_mask[layer] = 0u;
      continue;
    }
    u32 map_size = (bg_control >> 14) & 3u;
    u32 base_block = (bg_control >> 8) & 0x1Fu;
    u32 sb_bot = base_block + ((map_size & 2u) ? ((map_size & 1u) ? 2u : 1u) : 0u);
    u32 tilecntrl = (bg_control >> 2) & 3u;
    cgba_m0.bg_hofs[layer] = (u16)h_raw;
    cgba_m0.bg_wide[layer] = (u8)(map_size & 1u);
    cgba_bg_hspan_plan sp;
    cgba_build_bg_hspan_plan(h_raw, map_size & 1u, 0u, 240u, &sp);
    p->tile_charbase = &vram[tilecntrl * 16384u];
    p->sb_ptr[0] = (u16 *)&vram[base_block * 2048u];
    p->sb_ptr[1] = (u16 *)&vram[sb_bot * 2048u];
    p->counts_packed = sp.cp;
    p->vofs = vofs;
    p->first_byte_off = sp.first_byte_off;
    p->map2_byte_off = sp.map2_byte_off;
    p->tile_hoff = sp.thoff;
    if (is8bpp) {
      p->row_nonzero[0] = 0xFFFFFFFFu;
      p->row_nonzero[1] = 0xFFFFFFFFu;
      cgba_m0.row_all_empty[layer][0] = 0u;
      cgba_m0.row_all_empty[layer][1] = 0u;
      p->flags = 8u;
      cgba_m0.bg_static_key[layer] = skey;
      cgba_m0.bg_sb_mask[layer] = 0u;
      continue;
    }
    u32 layer_sbs = 0u;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    if (base_no_blend) {
      /* Base layer always renders with base_done == 0; never skips zero rows. */
      p->row_nonzero[0] = 0xFFFFFFFFu;
      p->row_nonzero[1] = 0xFFFFFFFFu;
      cgba_m0.row_all_empty[layer][0] = 0u;
      cgba_m0.row_all_empty[layer][1] = 0u;
      p->flags = 1u;
    } else {
      u32 nz0 = 0xFFFFFFFFu, nz1 = 0xFFFFFFFFu;
      u32 em0 = 0u, em1 = 0u;
      u32 empty_cbb_mask = 0u;
      u32 cbb_tile_base = tilecntrl << 9;
      if (sb_bot == base_block) {
        nz0 = cgba_sb_get_row_nonzero(base_block);
        em0 = cgba_sb_compute_empty_rows(base_block, cbb_tile_base, &empty_cbb_mask);
        layer_sbs |= (1u << base_block);
        if (map_size & 1u) {
          nz0 |= cgba_sb_get_row_nonzero(base_block + 1u);
          em0 &= cgba_sb_compute_empty_rows(base_block + 1u, cbb_tile_base, &empty_cbb_mask);
          if (base_block + 1u < 32u)
            layer_sbs |= (1u << (base_block + 1u));
        }
        nz1 = nz0;
        em1 = em0;
      } else {
        nz0 = cgba_sb_get_row_nonzero(base_block);
        em0 = cgba_sb_compute_empty_rows(base_block, cbb_tile_base, &empty_cbb_mask);
        layer_sbs |= (1u << base_block);
        if (map_size & 1u) {
          nz0 |= cgba_sb_get_row_nonzero(base_block + 1u);
          em0 &= cgba_sb_compute_empty_rows(base_block + 1u, cbb_tile_base, &empty_cbb_mask);
          if (base_block + 1u < 32u)
            layer_sbs |= (1u << (base_block + 1u));
        }
        nz1 = cgba_sb_get_row_nonzero(sb_bot);
        em1 = cgba_sb_compute_empty_rows(sb_bot, cbb_tile_base, &empty_cbb_mask);
        if (sb_bot < 32u)
          layer_sbs |= (1u << sb_bot);
        if (map_size & 1u) {
          nz1 |= cgba_sb_get_row_nonzero(sb_bot + 1u);
          em1 &= cgba_sb_compute_empty_rows(sb_bot + 1u, cbb_tile_base, &empty_cbb_mask);
          if (sb_bot + 1u < 32u)
            layer_sbs |= (1u << (sb_bot + 1u));
        }
      }
      layer_sbs |= empty_cbb_mask;
      p->row_nonzero[0] = nz0;
      p->row_nonzero[1] = nz1;
      cgba_m0.row_all_empty[layer][0] = em0;
      cgba_m0.row_all_empty[layer][1] = em1;
      p->flags = (u8)(1u | (((nz0 & nz1) != 0xFFFFFFFFu) ? 2u : 0u));
    }
#else
    p->row_nonzero[0] = 0xFFFFFFFFu;
    p->row_nonzero[1] = 0xFFFFFFFFu;
    cgba_m0.row_all_empty[layer][0] = 0u;
    cgba_m0.row_all_empty[layer][1] = 0u;
    p->flags = 1u;
#endif
    cgba_m0.bg_static_key[layer] = skey;
    cgba_m0.bg_sb_mask[layer] = layer_sbs;
    used_sbs |= layer_sbs;
  }
  cgba_m0.used_sb_mask = used_sbs;
  if (blend_bg < 4u && !(cgba_bg_plans[blend_bg].flags & (1u | 8u)))
    cgba_m0.blend_bg_layer = 0xFFu;
}

/* Cached 256-entry RGB565 LUT folding a 1st-target 8-bpp BG alpha-blended
 * over uniform backdrop (pal_conv[0]): entry 0 stays unblended backdrop,
 * entries 1..255 hold blend(pal_conv[i], pal_conv[0], eva, evb). */
static u16 cgba_bg_blend_bd_pal[256] __attribute__((aligned(32)));
static u32 cgba_bg_blend_bd_key = 0xFFFFFFFFu;
static u32 cgba_bg_blend_bd_epoch = 0u;

static const u16 *cgba_bg_blend_bd_pal_get(u32 eva, u32 evb)
{
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
  u16 bd = palette_ram_converted[0];
  u32 key = (eva << 24) | (evb << 16) | (u32)bd;
  if (__builtin_expect(cgba_bg_blend_bd_key == key &&
                       cgba_bg_blend_bd_epoch == cgba_term_a_epoch, 1))
    return cgba_bg_blend_bd_pal;
  cgba_bg_blend_bd_pal[0] = bd;
  if ((eva + evb) <= 16u) {
    for (u32 i = 1u; i < 256u; i++)
      cgba_bg_blend_bd_pal[i] = cgba_blend_rgb565_direct<false>(term_a[i], bd, evb);
  } else {
    for (u32 i = 1u; i < 256u; i++)
      cgba_bg_blend_bd_pal[i] = cgba_blend_rgb565_direct<true>(term_a[i], bd, evb);
  }
  cgba_bg_blend_bd_key = key;
  cgba_bg_blend_bd_epoch = cgba_term_a_epoch;
  return cgba_bg_blend_bd_pal;
}

/* Direct RGB565 scanline OBJ renderer when OBJ_MOD_SEMITRAN sprites on this row
 * are non-overlapping 4bpp non-affine non-mosaic sprites over 2nd-target layers. */
static __attribute__((noinline)) void cgba_render_scanline_objs_direct_alpha(
  u32 priority, u32 start, u32 end, u16 *scanline, const u16 *palptr,
  u32 vcount, u32 eva, u32 evb)
{
  if (__builtin_expect(!obj_list_built, 0))
    cgba_order_obj_materialize_lists();
  u32 objcnt = obj_priority_count[priority][vcount];
  if (__builtin_expect(objcnt == 0u, 0))
    return;
  const bool obj1dmap = (read_ioreg(REG_DISPCNT) & 0x40) != 0;
  const bool mosaic_active = (read_ioreg(REG_MOSAIC) & 0xFF00) != 0;
  const u8 *objlist = obj_priority_list[priority][vcount];
  const u32 *obj_term_a = cgba_obj_blend_term_a_get(eva);
  const bool sat = (eva + evb) > 16u;

  for (s32 objn = (s32)objcnt - 1; objn >= 0; objn--) {
    u32 objoff = objlist[objn];
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
    const cgba_decoded_obj *d = &decoded_objs[objoff];
    u16 a0 = d->attr0;
    const t_sprite &obji = d->sprite;
#else
    const t_oam *oamentry = &((t_oam *)oam_ram)[objoff];
    u16 a0 = eswap16(oamentry->attr0);
    t_sprite obji = decode_sprite(oamentry);
#endif
    if (((a0 >> 10) & 3u) != OBJ_MOD_SEMITRAN) {
#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
      s32 ox = obji.obj_x;
      u8 ff = d->fast_flags;
      s32 ox_end = ox + ((!(ff & 1u) && (a0 & 0x100) && obji.is_double)
                          ? (obji.obj_w << 1) : obji.obj_w);
      if (ox_end <= (s32)start || ox >= (s32)end)
        continue;
      const u8 forbid_mask = mosaic_active ? 0x08u : 0x00u;
      if (__builtin_expect((ff & 1u) != 0u && (ff & forbid_mask) == 0u &&
                           ox >= (s32)start && ox_end <= (s32)end, 1)) {
        u32 voffset = (ff & 4u) ? (u32)(obji.obj_y + obji.obj_h - (s32)vcount - 1)
                                : (u32)((s32)vcount - obji.obj_y);
        u32 pshift = obj1dmap ? (u32)d->fast_row_shift : 10u;
        u32 off = (d->fast_tile_base + ((voffset >> 3) << pshift) +
                   ((voffset & 7u) << 2)) & 0x7FFFu;
        u32 span = d->fast_span;
        if (ff & 2u) {
          if (__builtin_expect(off >= span, 1)) {
            cgba_render_obj_4bpp_tiles_hf_sh4(
              &vram[0x10000u + off], d->fast_num_tiles,
              scanline + ox, palptr + d->fast_pal_off);
            continue;
          }
        } else {
          if (__builtin_expect(off + span < 0x8000u, 1)) {
            cgba_render_obj_4bpp_tiles_sh4(
              &vram[0x10000u + off], d->fast_num_tiles,
              scanline + ox, palptr + d->fast_pal_off);
            continue;
          }
        }
      }
#endif
      render_scanline_obj_slow<u16, FULLCOLOR>(
        &obji, a0, start, end, scanline, 0u, palptr,
        (s32)vcount, obj1dmap, mosaic_active);
      continue;
    }

    /* Semi-transparent 4bpp non-affine non-mosaic OBJ: blend directly onto RGB565. */
    s32 ox = obji.obj_x;
    s32 ox_end = ox + obji.obj_w;
    if (ox_end <= (s32)start || ox >= (s32)end)
      continue;
    bool hflip = (obji.attr1 & 0x1000u) != 0u;
    bool vflip = (obji.attr1 & 0x2000u) != 0u;
    u32 voffset = vflip ? (u32)(obji.obj_y + obji.obj_h - (s32)vcount - 1)
                        : (u32)((s32)vcount - obji.obj_y);
    u32 ntiles = (u32)obji.obj_w >> 3;
    u32 pal_off = (obji.attr2 >> 8) & 0xF0u;
    const u32 *sub_ta = obj_term_a + pal_off;
    u32 tile_pitch = obj1dmap ? (ntiles << 5) : 1024u;
    u32 base_tile = (obji.attr2 & 0x3FFu) << 5;
    u32 row_off = (base_tile + (voffset >> 3) * tile_pitch + ((voffset & 7u) << 2)) & 0x7FFFu;

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
    if (ox >= (s32)start && ox_end <= (s32)end) {
      u32 span = (ntiles - 1u) << 5;
      u32 asm_flags = evb | (hflip ? 0x100u : 0u) | (sat ? 0x200u : 0u);
      if (hflip) {
        u32 start_off = (row_off + span) & 0x7FFFu;
        if (__builtin_expect(start_off >= span, 1)) {
          cgba_render_obj_4bpp_tiles_blend_sh4(
            &vram[0x10000u + start_off], ntiles, scanline + ox, sub_ta, asm_flags);
          continue;
        }
      } else {
        if (__builtin_expect(row_off + span < 0x8000u, 1)) {
          cgba_render_obj_4bpp_tiles_blend_sh4(
            &vram[0x10000u + row_off], ntiles, scanline + ox, sub_ta, asm_flags);
          continue;
        }
      }
    }
#endif

    /* Clipped, VRAM-wrapping, or host C++ fallback for 4bpp SEMITRAN OBJ. */
    u32 sx0 = (u32)MAX((s32)start, ox);
    u32 sx1 = (u32)MIN((s32)end, ox_end);
    for (u32 sx = sx0; sx < sx1; sx++) {
      u32 local_x = sx - (u32)ox;
      u32 px = hflip ? ((u32)obji.obj_w - 1u - local_x) : local_x;
      u32 t_off = (row_off + ((px >> 3) << 5)) & 0x7FFFu;
      u8 byte = vram[0x10000u + t_off + ((px & 7u) >> 1)];
      u32 pval = (px & 1u) ? (byte >> 4) : (byte & 0xFu);
      if (pval) {
        scanline[sx] = sat ? cgba_blend_rgb565_direct<true>(sub_ta[pval], scanline[sx], evb)
                           : cgba_blend_rgb565_direct<false>(sub_ta[pval], scanline[sx], evb);
      }
    }
  }
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

static inline void cgba_dispatch_4bpp_strip_blend(
  bool saturate, const u16 *map1, u32 counts_packed, u16 *dst_top,
  const u8 *tile_charbase, u32 rke, const u32 *term_a, const u16 *map2)
{
  if (!saturate) {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
    cgba_render_4bpp_strip_blend_sh4(
      map1, counts_packed, dst_top, tile_charbase, rke, term_a, map2);
#else
    cgba_render_4bpp_strip_blend_c<false>(
      map1, counts_packed, dst_top, tile_charbase, rke, term_a, map2);
#endif
  } else {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
    cgba_render_4bpp_strip_blend_sat_sh4(
      map1, counts_packed, dst_top, tile_charbase, rke, term_a, map2);
#else
    cgba_render_4bpp_strip_blend_c<true>(
      map1, counts_packed, dst_top, tile_charbase, rke, term_a, map2);
#endif
  }
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_blend_hspan_rows(
  const cgba_mode0_bg_plan *p, const cgba_bg_hspan_plan *sp,
  u32 sy0, u32 sy1, u16 *dst_first_row, u32 eva, u32 evb)
{
  if (eva == 0u && evb == 16u)
    return;
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
  const bool saturate = (eva + evb) > 16u;
  const u32 left_w = sp->left_w;
  const u32 right_w = sp->right_w;
  const u32 cp = sp->cp;
  const u32 todraw1 = cp & 0xFFFFu;
  const u32 todraw2 = cp >> 16;
  u32 y = sy0;
  u16 *dst_top = dst_first_row;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (cgba_4bpp_strip_row_zero_skip(p, vhalf, voffset, start_r, k)) {
      y += k;
      dst_top += k * 240u;
      continue;
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map1 = (u16 *)(map_row_b + sp->first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + sp->map2_byte_off);
    u16 left_raw = left_w ? *map1++ : 0u;
    u16 right_raw = right_w ? *(map2 + todraw2) : 0u;
    u32 rke = (evb << 16) | (start_r << 8) | k;

    if (cp != 0u) {
      u16 *mid_map1 = todraw1 ? map1 : map2;
      u32 mid_cp = todraw1 ? cp : todraw2;
      cgba_dispatch_4bpp_strip_blend(
        saturate, mid_map1, mid_cp, dst_top + sp->mid_x, p->tile_charbase,
        rke, term_a, map2);
    }
    if (left_w) {
      if (!saturate)
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top + sp->x0, k, sp->thoff, sp->thoff + left_w, left_raw,
          p->tile_charbase, start_r, term_a, evb);
      else
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + sp->x0, k, sp->thoff, sp->thoff + left_w, left_raw,
          p->tile_charbase, start_r, term_a, evb);
    }
    if (right_w) {
      if (!saturate)
        cgba_render_4bpp_part_blend_strip<false>(
          dst_top + sp->right_x, k, 0u, right_w, right_raw,
          p->tile_charbase, start_r, term_a, evb);
      else
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + sp->right_x, k, 0u, right_w, right_raw,
          p->tile_charbase, start_r, term_a, evb);
    }
    y += k;
    dst_top += k * 240u;
  }
}

static inline void cgba_render_4bpp_bg_blend_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  u32 eva, u32 evb)
{
  cgba_bg_hspan_plan sp;
  cgba_bg_hspan_from_plan(p, &sp);
  cgba_render_4bpp_bg_blend_hspan_rows(p, &sp, sy0, sy1, dst_first_row, eva, evb);
}

template<bool is_over>
static __attribute__((noinline)) void cgba_render_8bpp_part_strip(
  u16 *dst_top, u32 k, u32 col_start, u32 col_end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r, const u16 *pal256)
{
  u32 map_entry = eswap16(raw_map);
  const u8 *row = tile_charbase + ((map_entry & 0x3FFu) << 6) + (start_r << 3);
  s32 row_step = 8;
  if (map_entry & 0x800u) {
    row += 56 - (s32)(start_r << 4);
    row_step = -8;
  }
  const bool hflip = (map_entry & 0x400u) != 0u;
  const u32 width = col_end - col_start;
  u16 *dst_row = dst_top;
  for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
    if (__builtin_expect(hflip, 0)) {
      for (u32 i = 0; i < width; i++) {
        u32 pix = row[7u - (col_start + i)];
        if (!is_over || pix)
          dst_row[i] = pal256[pix];
      }
    } else {
      for (u32 i = 0; i < width; i++) {
        u32 pix = row[col_start + i];
        if (!is_over || pix)
          dst_row[i] = pal256[pix];
      }
    }
  }
}

template<bool saturate>
static __attribute__((noinline)) void cgba_render_8bpp_part_blend_strip(
  u16 *dst_top, u32 k, u32 col_start, u32 col_end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r,
  const u32 *term_a256, u32 evb)
{
  u32 map_entry = eswap16(raw_map);
  const u8 *row = tile_charbase + ((map_entry & 0x3FFu) << 6) + (start_r << 3);
  s32 row_step = 8;
  if (map_entry & 0x800u) {
    row += 56 - (s32)(start_r << 4);
    row_step = -8;
  }
  const bool hflip = (map_entry & 0x400u) != 0u;
  const u32 width = col_end - col_start;
  u16 *dst_row = dst_top;
  for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
    if (__builtin_expect(hflip, 0)) {
      for (u32 i = 0; i < width; i++) {
        u32 pix = row[7u - (col_start + i)];
        if (pix)
          dst_row[i] = cgba_blend_rgb565_direct<saturate>(term_a256[pix], dst_row[i], evb);
      }
    } else {
      for (u32 i = 0; i < width; i++) {
        u32 pix = row[col_start + i];
        if (pix)
          dst_row[i] = cgba_blend_rgb565_direct<saturate>(term_a256[pix], dst_row[i], evb);
      }
    }
  }
}

#ifndef CGBA_SH4_ASM_4BPP
template<bool is_over>
static __attribute__((noinline)) void cgba_render_8bpp_strip_c(
  const u16 *map1, u32 counts_packed, u16 *dst_top,
  const u8 *tile_charbase, u32 start_r_and_k,
  const u16 *pal256, const u16 *map2)
{
  u32 todraw = counts_packed & 0xFFFFu;
  u32 todraw2 = counts_packed >> 16;
  u32 k = start_r_and_k & 0xFFu;
  u32 start_r = (start_r_and_k >> 8) & 0xFFu;
  const u8 *tb_norm = tile_charbase + (start_r << 3);
  s32 vflip_off = 56 - (s32)(start_r << 4);
  const u16 *map = map1;
  u16 *dst_tile = dst_top;

  for (;;) {
    while (todraw--) {
      u32 map_entry = eswap16(*map++);
      const u8 *row = tb_norm + ((map_entry & 0x3FFu) << 6);
      s32 row_step = 8;
      if (map_entry & 0x800u) {
        row += vflip_off;
        row_step = -8;
      }
      bool hflip = (map_entry & 0x400u) != 0u;
      u16 *d = dst_tile;
      for (u32 r = 0; r < k; r++, row += row_step, d += 240u) {
        if (!hflip) {
          for (u32 c = 0; c < 8u; c++) {
            u32 pix = row[c];
            if (!is_over || pix)
              d[c] = pal256[pix];
          }
        } else {
          for (u32 c = 0; c < 8u; c++) {
            u32 pix = row[7u - c];
            if (!is_over || pix)
              d[c] = pal256[pix];
          }
        }
      }
      dst_tile += 8;
    }
    if (!todraw2)
      break;
    todraw = todraw2;
    todraw2 = 0;
    map = map2;
  }
}
#endif

#if !defined(CGBA_SH4_ASM_4BPP) || defined(USE_XBGR1555_FORMAT)
template<bool saturate>
static __attribute__((noinline)) void cgba_render_8bpp_strip_blend_c(
  const u16 *map1, u32 counts_packed, u16 *dst_top,
  const u8 *tile_charbase, u32 rke_sat,
  const u32 *term_a256, const u16 *map2)
{
  u32 todraw = counts_packed & 0xFFFFu;
  u32 todraw2 = counts_packed >> 16;
  u32 k = rke_sat & 0xFFu;
  u32 start_r = (rke_sat >> 8) & 0xFFu;
  u32 evb = (rke_sat >> 16) & 0xFFu;
  const u8 *tb_norm = tile_charbase + (start_r << 3);
  s32 vflip_off = 56 - (s32)(start_r << 4);
  const u16 *map = map1;
  u16 *dst_tile = dst_top;

  for (;;) {
    while (todraw--) {
      u32 map_entry = eswap16(*map++);
      const u8 *row = tb_norm + ((map_entry & 0x3FFu) << 6);
      s32 row_step = 8;
      if (map_entry & 0x800u) {
        row += vflip_off;
        row_step = -8;
      }
      bool hflip = (map_entry & 0x400u) != 0u;
      u16 *d = dst_tile;
      for (u32 r = 0; r < k; r++, row += row_step, d += 240u) {
        if (!hflip) {
          for (u32 c = 0; c < 8u; c++) {
            u32 pix = row[c];
            if (pix)
              d[c] = cgba_blend_rgb565_direct<saturate>(term_a256[pix], d[c], evb);
          }
        } else {
          for (u32 c = 0; c < 8u; c++) {
            u32 pix = row[7u - c];
            if (pix)
              d[c] = cgba_blend_rgb565_direct<saturate>(term_a256[pix], d[c], evb);
          }
        }
      }
      dst_tile += 8;
    }
    if (!todraw2)
      break;
    todraw = todraw2;
    todraw2 = 0;
    map = map2;
  }
}
#endif

template<bool is_over>
static __attribute__((noinline)) void cgba_render_8bpp_bg_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  const u16 *pal256)
{
  const u32 thoff = p->tile_hoff;
  u32 y = sy0;
  u16 *dst_top = dst_first_row;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map1 = (u16 *)(map_row_b + p->first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
    u32 rk = (start_r << 8) | k;

    if (__builtin_expect(thoff == 0u, 1)) {
#ifdef CGBA_SH4_ASM_4BPP
      if (is_over)
        cgba_render_8bpp_strip_over_sh4(
          map1, p->counts_packed, dst_top, p->tile_charbase, rk, pal256, map2);
      else
        cgba_render_8bpp_strip_base_sh4(
          map1, p->counts_packed, dst_top, p->tile_charbase, rk, pal256, map2);
#else
      cgba_render_8bpp_strip_c<is_over>(
        map1, p->counts_packed, dst_top, p->tile_charbase, rk, pal256, map2);
#endif
    } else {
      u32 left_w = 8u - thoff;
      u32 cp = p->counts_packed;
      u16 left_raw = *map1++;
      u16 *mid_map1 = (cp & 0xFFFFu) ? map1 : map2;
      u32 mid_cp = (cp & 0xFFFFu) ? cp : (cp >> 16);
      u16 right_raw = *(map2 + (cp >> 16));
#ifdef CGBA_SH4_ASM_4BPP
      if (is_over)
        cgba_render_8bpp_strip_over_sh4(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rk, pal256, map2);
      else
        cgba_render_8bpp_strip_base_sh4(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rk, pal256, map2);
#else
      cgba_render_8bpp_strip_c<is_over>(
        mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rk, pal256, map2);
#endif
      cgba_render_8bpp_part_strip<is_over>(
        dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, pal256);
      cgba_render_8bpp_part_strip<is_over>(
        dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, pal256);
    }
    y += k;
    dst_top += k * 240u;
  }
}

static inline void cgba_render_8bpp_bg_base_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  const u16 *pal256)
{
  cgba_render_8bpp_bg_rows<false>(p, sy0, sy1, dst_first_row, pal256);
}

static inline void cgba_render_8bpp_bg_over_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  const u16 *pal256)
{
  cgba_render_8bpp_bg_rows<true>(p, sy0, sy1, dst_first_row, pal256);
}

static __attribute__((noinline)) void cgba_render_8bpp_bg_blend_rows(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *dst_first_row,
  u32 eva, u32 evb)
{
  if (eva == 0u && evb == 16u)
    return;
  const u32 *term_a = cgba_bg_blend_term_a_get(eva);
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  const u16 *bd_pal = cgba_bg_blend_bd_pal_get(eva, evb);
#endif
  const bool saturate = (eva + evb) > 16u;
  const u32 thoff = p->tile_hoff;
  u32 y = sy0;
  u16 *dst_top = dst_first_row;

  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *map1 = (u16 *)(map_row_b + p->first_byte_off);
    u16 *map2 = (u16 *)(map_row_b + p->map2_byte_off);
    u32 rke_sat = ((u32)saturate << 24) | (evb << 16) | (start_r << 8) | k;

    if (__builtin_expect(thoff == 0u, 1)) {
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
      cgba_render_8bpp_strip_blend_sh4(
        map1, p->counts_packed, dst_top, p->tile_charbase, rke_sat, term_a, map2, bd_pal);
#else
      if (!saturate)
        cgba_render_8bpp_strip_blend_c<false>(
          map1, p->counts_packed, dst_top, p->tile_charbase, rke_sat, term_a, map2);
      else
        cgba_render_8bpp_strip_blend_c<true>(
          map1, p->counts_packed, dst_top, p->tile_charbase, rke_sat, term_a, map2);
#endif
    } else {
      u32 left_w = 8u - thoff;
      u32 cp = p->counts_packed;
      u16 left_raw = *map1++;
      u16 *mid_map1 = (cp & 0xFFFFu) ? map1 : map2;
      u32 mid_cp = (cp & 0xFFFFu) ? cp : (cp >> 16);
      u16 right_raw = *(map2 + (cp >> 16));
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
      cgba_render_8bpp_strip_blend_sh4(
        mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke_sat, term_a, map2, bd_pal);
#else
      if (!saturate)
        cgba_render_8bpp_strip_blend_c<false>(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke_sat, term_a, map2);
      else
        cgba_render_8bpp_strip_blend_c<true>(
          mid_map1, mid_cp, dst_top + left_w, p->tile_charbase, rke_sat, term_a, map2);
#endif
      if (!saturate) {
        cgba_render_8bpp_part_blend_strip<false>(
          dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, term_a, evb);
        cgba_render_8bpp_part_blend_strip<false>(
          dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, term_a, evb);
      } else {
        cgba_render_8bpp_part_blend_strip<true>(
          dst_top, k, thoff, 8u, left_raw, p->tile_charbase, start_r, term_a, evb);
        cgba_render_8bpp_part_blend_strip<true>(
          dst_top + left_w + 232u, k, 0u, thoff, right_raw, p->tile_charbase, start_r, term_a, evb);
      }
    }
    y += k;
    dst_top += k * 240u;
  }
}

static inline bool cgba_bg_strip_all_empty(
  const cgba_mode0_bg_plan *p, const u32 row_empty[2], u32 sy0, u32 sy1)
{
  if ((row_empty[0] | row_empty[1]) == 0u)
    return false;
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 k = 8u - (voffset & 7u);
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    u32 rbit = 1u << ((voffset & 255u) >> 3);
    if (!(row_empty[vhalf] & rbit))
      return false;
    y += k;
  }
  return true;
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
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  cgba_tile4_invalidate_all();
#endif
}

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static inline void cgba_sb_opaque_invalidate(u32 sbd)
{
  static const u32 cbb_sb_mask[4] = {
    0x0000FFFFu, 0x00FFFF00u, 0xFFFF0000u, 0xFF000000u
  };
  if (!sbd) {
    for (u32 c = 0; c < 4u; c++)
      cgba_sb.opaque_valid[c] = 0u;
#ifdef CGBA_SH4_ASM_4BPP
    cgba_aff_tile_valid = 0u;
#endif
    return;
  }
  for (u32 c = 0; c < 4u; c++) {
    if (sbd & cbb_sb_mask[c])
      cgba_sb.opaque_valid[c] = 0u;
    else
      cgba_sb.opaque_valid[c] &= ~sbd;
#ifdef CGBA_SH4_ASM_4BPP
    if (sbd & (0xFFu << (c * 8u)))
      cgba_aff_tile_valid &= (u8)~(1u << c);
#endif
  }
}

static inline void cgba_bg_vram_invalidate(void)
{
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
  u32 sbd = cgba_vram_sb_dirty;
  cgba_vram_sb_dirty = 0;
  cgba_sb_valid_mask &= sbd ? ~sbd : 0u;
  cgba_tile4_invalidate_sbd(sbd);
  cgba_sb_opaque_invalidate(sbd);
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
  cgba_tile4_invalidate_all();
  cgba_sb_opaque_invalidate(0u);
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
      u32 prio = layer & 0x3u;
      write_ioreg(REG_VCOUNT, vcount);
      if (__builtin_expect(cgba_mode0_obj_alpha_can_blend &&
                           (obj_alpha_prio_mask[vcount] & (1u << prio)), 0)) {
        cgba_render_scanline_objs_direct_alpha(
          prio, 0, 240, screen_offset, pal_conv + 0x100, vcount,
          cgba_m0.obj_alpha_eva, cgba_m0.obj_alpha_evb);
      } else {
        render_scanline_objs<u16, FULLCOLOR>(
          prio, 0, 240, screen_offset, pal_conv + 0x100);
      }
      base_done = 1;
      continue;
    }

    const cgba_mode0_bg_plan *p = &plans[layer];
    if (__builtin_expect(layer == cgba_m0.blend_bg_layer, 0)) {
      if (p->flags & 8u) {
        if (!base_done) {
          const u16 *bd_pal = cgba_bg_blend_bd_pal_get(cgba_m0.blend_eva, cgba_m0.blend_evb);
          cgba_render_8bpp_bg_base_rows(p, vcount, vcount + 1u, screen_offset, bd_pal);
          base_done = 1;
        } else {
          cgba_render_8bpp_bg_blend_rows(
            p, vcount, vcount + 1u, screen_offset,
            cgba_m0.blend_eva, cgba_m0.blend_evb);
        }
        continue;
      }
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
      if (pflags & 8u) {
        if (base_done)
          cgba_render_8bpp_bg_over_rows(
            p, vcount, vcount + 1u, screen_offset, pal_conv);
        else
          cgba_render_8bpp_bg_base_rows(
            p, vcount, vcount + 1u, screen_offset, pal_conv);
        base_done = 1;
        continue;
      }
      write_ioreg(REG_VCOUNT, vcount);
      if (pflags & 4u) {
        if (base_done)
          render_scanline_affine<u16, FULLCOLOR, false>(
            layer, 0, 240, screen_offset, pal_conv);
        else
          render_scanline_affine<u16, FULLCOLOR, true>(
            layer, 0, 240, screen_offset, pal_conv);
      } else if (base_done) {
        render_scanline_text<u16, FULLCOLOR, false>(
          layer, 0, 240, screen_offset, pal_conv);
      } else {
        render_scanline_text<u16, FULLCOLOR, true>(
          layer, 0, 240, screen_offset, pal_conv);
      }
      base_done = 1;
      continue;
    }

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
    if ((base_done || cgba_m0.blend_bg_layer < 4u) &&
        (cgba_m0.row_all_empty[layer][0] | cgba_m0.row_all_empty[layer][1]) != 0u) {
      u32 voff_e = (vcount + p->vofs) & 511u;
      if (cgba_m0.row_all_empty[layer][voff_e >> 8] & (1u << ((voff_e & 255u) >> 3)))
        continue;
    }
#endif

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
        render_text_tile_run<u16, FULLCOLOR, false, false>(
          td1, map_ptr, dst, 0u, 0u, tile_base, vflip_off, pal_conv);
      }
      map_ptr = map2;
      if (td2) {
        render_text_tile_run<u16, FULLCOLOR, false, false>(
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
        render_text_tile_run<u16, FULLCOLOR, false, true>(
          td1, map_ptr, dst, 0u, 0u, tile_base, vflip_off, base_pal);
      }
      map_ptr = map2;
      if (td2) {
        render_text_tile_run<u16, FULLCOLOR, false, true>(
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
  cgba_video_frame_dirty_row_mask[0] = 0u;
  cgba_video_frame_dirty_row_mask[1] = 0u;
  cgba_video_frame_dirty_row_mask[2] = 0u;
  cgba_video_frame_dirty_row_mask[3] = 0u;
  cgba_video_frame_dirty_row_mask[4] = 0u;
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

static inline void cgba_update_affine_ref_single(u32 aidx, u32 vcount)
{
  u16 bgcnt = read_ioreg(aidx ? REG_BG3CNT : REG_BG2CNT);
  if (bgcnt & 0x40u) {
    u32 bgmosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xFu) + 1u;
    if ((vcount % bgmosv) == bgmosv - 1u) {
      affine_reference_x[aidx] += (s16)read_ioreg(aidx ? REG_BG3PB : REG_BG2PB) * (s32)bgmosv;
      affine_reference_y[aidx] += (s16)read_ioreg(aidx ? REG_BG3PD : REG_BG2PD) * (s32)bgmosv;
    }
  } else {
    affine_reference_x[aidx] += (s16)read_ioreg(aidx ? REG_BG3PB : REG_BG2PB);
    affine_reference_y[aidx] += (s16)read_ioreg(aidx ? REG_BG3PD : REG_BG2PD);
  }
}

static __attribute__((noinline, unused)) void cgba_advance_affine_refs(u32 y0, u32 y1)
{
  u32 n = y1 - y0;
  if (!n)
    return;
  if ((read_ioreg(REG_BG2CNT) & 0x40u) == 0u) {
    affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB) * (s32)n;
    affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD) * (s32)n;
  } else {
    for (u32 y = y0; y < y1; y++)
      cgba_update_affine_ref_single(0u, y);
  }
  if ((read_ioreg(REG_BG3CNT) & 0x40u) == 0u) {
    affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB) * (s32)n;
    affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD) * (s32)n;
  } else {
    for (u32 y = y0; y < y1; y++)
      cgba_update_affine_ref_single(1u, y);
  }
}

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

struct cgba_bg2_aff_snap {
  s32 ref_x;
  s32 ref_y;
  s16 pa;
  s16 pc;
};
static cgba_bg2_aff_snap cgba_bg2_snaps[160];
static u32 cgba_bg2_snap_count = 0u;

void cgba_discard_pending_scanlines(void)
{
  cgba_pending_sl_count = 0;
  cgba_pending_sl_start = 0;
  cgba_bg2_snap_count = 0u;
}

static __attribute__((noinline)) void cgba_snapshot_bg2_affine_mosaic(
  u32 y0, u32 i, u32 count, s16 pa, s16 pc)
{
  for (; i < count; i++) {
    u32 y = y0 + i;
    cgba_bg2_snaps[y].ref_x = affine_reference_x[0];
    cgba_bg2_snaps[y].ref_y = affine_reference_y[0];
    cgba_bg2_snaps[y].pa = pa;
    cgba_bg2_snaps[y].pc = pc;
    cgba_update_affine_refs(y);
  }
  cgba_bg2_snap_count = count;
}

/* Mode 1, or Mode 2 with BG3 disabled (only BG2 + OBJ active — identical
 * single-affine-BG surface to Mode 1 so BG2 snapshots/batching are safe). */
static inline bool cgba_bg2_affine_mode_ok(u16 dispcnt)
{
  u32 m = dispcnt & 0x87u;
  return m == 0x01u || (m == 0x02u && (dispcnt & 0x0800u) == 0u);
}

void cgba_snapshot_bg2_affine_pending(void)
{
  u32 count = cgba_pending_sl_count;
  if (count == 0u)
    return;
  u32 y0 = cgba_pending_sl_start;
  if (__builtin_expect(!cgba_bg2_affine_mode_ok(read_ioreg(REG_DISPCNT)) ||
                       y0 + count > 160u || skip_next_frame != 0, 0)) {
    cgba_flush_pending_scanlines();
    return;
  }
  u32 i = cgba_bg2_snap_count;
  if (i >= count)
    return;
  s16 pa = (s16)read_ioreg(REG_BG2PA);
  s16 pc = (s16)read_ioreg(REG_BG2PC);
  if (__builtin_expect(((read_ioreg(REG_BG2CNT) | read_ioreg(REG_BG3CNT)) & 0x40u) != 0u, 0)) {
    cgba_snapshot_bg2_affine_mosaic(y0, i, count, pa, pc);
    return;
  }
  s32 pb2 = (s16)read_ioreg(REG_BG2PB);
  s32 pd2 = (s16)read_ioreg(REG_BG2PD);
  s32 pb3 = (s16)read_ioreg(REG_BG3PB);
  s32 pd3 = (s16)read_ioreg(REG_BG3PD);
  s32 rx0 = affine_reference_x[0];
  s32 ry0 = affine_reference_y[0];
  s32 rx1 = affine_reference_x[1];
  s32 ry1 = affine_reference_y[1];
  cgba_bg2_aff_snap *dst = &cgba_bg2_snaps[y0 + i];
  u32 n = count - i;
  do {
    dst->ref_x = rx0;
    dst->ref_y = ry0;
    dst->pa = pa;
    dst->pc = pc;
    dst++;
    rx0 += pb2;
    ry0 += pd2;
    rx1 += pb3;
    ry1 += pd3;
  } while (--n);
  affine_reference_x[0] = rx0;
  affine_reference_y[0] = ry0;
  affine_reference_x[1] = rx1;
  affine_reference_y[1] = ry1;
  cgba_bg2_snap_count = count;
}

#if defined(CGBA_BULK_DMA) && CGBA_BULK_DMA
int cgba_try_hblank_bg2_dma0_fast(void)
{
  if (__builtin_expect(skip_next_frame != 0, 0))
    return 0;
  dma_transfer_type *dmach = &dma[0];
  u32 src_ptr = dmach->source_address & 0x0FFFFFFCu;
  u32 sreg = src_ptr >> 24;
  const u8 *s;
  int cyc;
  if (__builtin_expect(sreg == 3u, 1)) {
    u32 soff = src_ptr & 0x7FFFu;
    if (__builtin_expect(soff + 16u > 0x8000u, 0))
      return 0;
    s = iwram + 0x8000u + soff;
    cyc = 8;
  } else if (sreg == 2u) {
    u32 soff = src_ptr & 0x3FFFFu;
    if (__builtin_expect(soff + 16u > 0x40000u, 0))
      return 0;
    s = ewram + soff;
    cyc = 28;
  } else {
    return 0;
  }
  u32 count = cgba_pending_sl_count;
  if (__builtin_expect(count != 0u, 1)) {
    u32 y0 = cgba_pending_sl_start;
    u32 i = cgba_bg2_snap_count;
    if (__builtin_expect(
          !cgba_bg2_affine_mode_ok(read_ioreg(REG_DISPCNT)) ||
          y0 + count > 160u ||
          ((io_registers[REG_BG2CNT] | io_registers[REG_BG3CNT]) & eswap16(0x40u)) != 0u ||
          (count - i) != 1u, 0))
      return 0;
    cgba_bg2_aff_snap *sn = &cgba_bg2_snaps[y0 + i];
    sn->ref_x = affine_reference_x[0];
    sn->ref_y = affine_reference_y[0];
    sn->pa = (s16)read_ioreg(REG_BG2PA);
    sn->pc = (s16)read_ioreg(REG_BG2PC);
    if (__builtin_expect((io_registers[REG_BG3PB] | io_registers[REG_BG3PD]) != 0u, 0)) {
      affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB);
      affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD);
    }
    cgba_bg2_snap_count = count;
  }
  const u32 *src32 = (const u32 *)s;
  u32 *d32 = (u32 *)((u8 *)io_registers + 0x20u);
  u32 w0 = src32[0];
  u32 w1 = src32[1];
  u32 w2 = src32[2];
  u32 w3 = src32[3];
  d32[0] = w0;
  d32[1] = w1;
  d32[2] = w2;
  d32[3] = w3;
  u32 rx = eswap32(w2);
  u32 ry = eswap32(w3);
  dma_bus_val = ry;
  cgba_video_io_dirty = 1;
  affine_reference_x[0] = ((s32)(rx << 4)) >> 4;
  affine_reference_y[0] = ((s32)(ry << 4)) >> 4;
  dmach->source_address = src_ptr + 16u;
  return cyc;
}

int cgba_try_batch_hblank_bg2_dma0_fast(u32 start_y, u32 lines)
{
  if (__builtin_expect(skip_next_frame != 0 || lines == 0u ||
                       start_y + lines > 160u, 0))
    return 0;
  if (__builtin_expect(
        !cgba_bg2_affine_mode_ok(read_ioreg(REG_DISPCNT)) ||
        ((io_registers[REG_BG2CNT] | io_registers[REG_BG3CNT]) &
         eswap16(0x40u)) != 0u, 0))
    return 0;
  u32 cur = cgba_pending_sl_count;
  if (cur != 0u &&
      __builtin_expect(start_y != cgba_pending_sl_start + cur ||
                       cgba_bg2_snap_count != cur, 0))
    return 0;
  dma_transfer_type *dmach = &dma[0];
  u32 src_ptr = dmach->source_address & 0x0FFFFFFCu;
  u32 sreg = src_ptr >> 24;
  u32 total_bytes = lines * 16u;
  const u8 *s;
  if (__builtin_expect(sreg == 3u, 1)) {
    u32 soff = src_ptr & 0x7FFFu;
    if (__builtin_expect(soff + total_bytes > 0x8000u, 0))
      return 0;
    s = iwram + 0x8000u + soff;
  } else if (sreg == 2u) {
    u32 soff = src_ptr & 0x3FFFFu;
    if (__builtin_expect(soff + total_bytes > 0x40000u, 0))
      return 0;
    s = ewram + soff;
  } else {
    return 0;
  }

  cgba_bg2_aff_snap *sn = &cgba_bg2_snaps[start_y];
  sn->ref_x = affine_reference_x[0];
  sn->ref_y = affine_reference_y[0];
  sn->pa = (s16)read_ioreg(REG_BG2PA);
  sn->pc = (s16)read_ioreg(REG_BG2PC);
  sn++;

  const u8 *p = s;
  for (u32 k = 1u; k < lines; k++) {
    u32 rx = readaddress32(p, 8u);
    u32 ry = readaddress32(p, 12u);
    sn->ref_x = ((s32)(rx << 4)) >> 4;
    sn->ref_y = ((s32)(ry << 4)) >> 4;
    sn->pa = (s16)readaddress16(p, 0u);
    sn->pc = (s16)readaddress16(p, 4u);
    sn++;
    p += 16u;
  }

  const u32 *last32 = (const u32 *)p;
  u32 *d32 = (u32 *)((u8 *)io_registers + 0x20u);
  u32 w0 = last32[0];
  u32 w1 = last32[1];
  u32 w2 = last32[2];
  u32 w3 = last32[3];
  d32[0] = w0;
  d32[1] = w1;
  d32[2] = w2;
  d32[3] = w3;
  u32 rx = eswap32(w2);
  u32 ry = eswap32(w3);
  dma_bus_val = ry;
  cgba_video_io_dirty = 1;
  affine_reference_x[0] = ((s32)(rx << 4)) >> 4;
  affine_reference_y[0] = ((s32)(ry << 4)) >> 4;
  if (__builtin_expect((io_registers[REG_BG3PB] | io_registers[REG_BG3PD]) != 0u, 0)) {
    affine_reference_x[1] += (s32)(s16)read_ioreg(REG_BG3PB) * (s32)lines;
    affine_reference_y[1] += (s32)(s16)read_ioreg(REG_BG3PD) * (s32)lines;
  }
  dmach->source_address = src_ptr + total_bytes;

  if (cur == 0u) {
    cgba_pending_sl_start = start_y;
    cgba_pending_sl_count = lines;
    cgba_bg2_snap_count = lines;
  } else {
    cgba_pending_sl_count = cur + lines;
    cgba_bg2_snap_count = cur + lines;
  }
  return 1;
}
#endif

#if defined(CGBA_SH4_ASM_4BPP) && \
    defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
static u16 cgba_folded_pal[512] __attribute__((aligned(32)));
static u16 cgba_folded_bg_shadow[256] __attribute__((aligned(32)));
/* Mode 0 folded 4bpp pair LUT (16 KiB) and Mode 1 folded affine/BG/OBJ
 * palettes (20 KiB) are never live simultaneously; sharing storage saves
 * 16 KiB of .cgba.highbss. Ownership transitions invalidate the incoming side. */
static union {
  u16 pair_lut[16][256][2];
  u16 m1_fpal[16][640];
} cgba_folded_highbss CGBA_HIGH_BSS;
#define cgba_folded_pair_lut (cgba_folded_highbss.pair_lut)
#define cgba_m1_fpal_data    (cgba_folded_highbss.m1_fpal)
static u8 cgba_folded_union_owner = 0u; /* 0 = none, 1 = m0 pair_lut, 2 = m1_fpal */
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

static u16 cgba_blend_dom_hint = 0u;
static bool cgba_blend_dom_hint_valid = false;

/* Fast single-pass classifier using the cached dominant pure-solid tile hint.
 * Returns:
 *   0 = hint not applicable (fall back to full analyze + find_dominant)
 *   1 = entire strip is pure-solid (*out_cidx)
 *   2 = sparse override (*out_dom_raw, *out_cidx)
 *   3 = contiguous split cutout (*out_cidx, *out_xlo, *out_xhi) */
static u32 cgba_blend_strip_classify_hint(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, bool lower_4bpp_ok,
  u16 *out_dom_raw, u32 *out_cidx, u32 *out_xlo, u32 *out_xhi)
{
  if (!cgba_blend_dom_hint_valid)
    return 0u;
  const u16 cand = cgba_blend_dom_hint;
  u32 cidx = 0u;
  if (!cgba_blend_tile_is_solid_cidx(cand, p->tile_charbase, 0u, 8u, &cidx)) {
    cgba_blend_dom_hint_valid = false;
    return 0u;
  }

  const u32 thoff = p->tile_hoff;
  const u32 cp = p->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  const u32 nslots = thoff ? (td1 + td2 + 2u) : (td1 + td2);
  const u32 seg1_cnt = thoff ? (td1 + 1u) : td1;
  const u32 seg2_cnt = nslots - seg1_cnt;

  u32 total_slots = 0u;
  u32 match_cnt = 0u;
  u32 nrows_t = 0u;
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

    u32 first_miss = nslots;
    u32 last_miss = 0u;
    const u32 cand_pair = ((u32)cand << 16) | (u32)cand;

    auto scan_seg = [&](const u16 *mp, u32 cnt, u32 base_idx) {
      u32 s = 0u;
      if (cnt != 0u && (((uintptr_t)mp & 2u) != 0u)) {
        if (__builtin_expect(mp[0] == cand, 1)) {
          match_cnt++;
        } else {
          if (first_miss == nslots)
            first_miss = base_idx;
          last_miss = base_idx;
        }
        s = 1u;
      }
      const u32 *mp32 = (const u32 *)(mp + s);
      u32 pairs = (cnt - s) >> 1;
      for (u32 p_i = 0u; p_i < pairs; p_i++, s += 2u) {
        if (__builtin_expect(mp32[p_i] == cand_pair, 1)) {
          match_cnt += 2u;
        } else {
          if (mp[s] == cand) {
            match_cnt++;
          } else {
            u32 idx = base_idx + s;
            if (first_miss == nslots)
              first_miss = idx;
            last_miss = idx;
          }
          if (mp[s + 1u] == cand) {
            match_cnt++;
          } else {
            u32 idx = base_idx + s + 1u;
            if (first_miss == nslots)
              first_miss = idx;
            last_miss = idx;
          }
        }
      }
      if (s < cnt) {
        if (__builtin_expect(mp[s] == cand, 1)) {
          match_cnt++;
        } else {
          u32 idx = base_idx + s;
          if (first_miss == nslots)
            first_miss = idx;
          last_miss = idx;
        }
      }
    };

    scan_seg(map1, seg1_cnt, 0u);
    if (seg2_cnt != 0u)
      scan_seg(map2, seg2_cnt, seg1_cnt);
    if (first_miss < min_left)
      min_left = first_miss;
    if (first_miss < nslots) {
      u32 rcnt = nslots - 1u - last_miss;
      if (rcnt < min_right)
        min_right = rcnt;
    }
    total_slots += nslots;
    nrows_t++;
    y += k;
  }

  if (total_slots == 0u)
    return 0u;
  if (match_cnt == total_slots) {
    *out_cidx = cidx;
    return 1u;
  }

  u32 x_lo = 0u;
  if (min_left > 0u)
    x_lo = thoff ? ((8u - thoff) + (min_left - 1u) * 8u) : (min_left * 8u);
  u32 x_hi = 240u;
  if (min_right > 0u)
    x_hi = thoff ? (240u - thoff - (min_right - 1u) * 8u) : (240u - min_right * 8u);
  if (x_lo >= x_hi) {
    *out_cidx = cidx;
    return 1u;
  }

  u32 edge_slots = nrows_t * (min_left + min_right);
  if (lower_4bpp_ok && (x_hi - x_lo) >= 48u &&
      edge_slots < total_slots &&
      match_cnt > edge_slots &&
      (match_cnt - edge_slots) * 2u >= (total_slots - edge_slots)) {
    *out_dom_raw = cand;
    *out_cidx = cidx;
    return 2u;
  }
  if (min_left + min_right >= 4u && (x_lo + (240u - x_hi)) >= 32u) {
    *out_cidx = cidx;
    *out_xlo = x_lo;
    *out_xhi = x_hi;
    return 3u;
  }
  return 0u;
}

/* Detect a dominant pure-solid tile covering >= 50% of the non-edge interior
 * of a blend_layer strip when non-dominant pixels (e.g. rain streaks, mist,
 * sparkles) are scattered across the strip. */
static bool cgba_blend_strip_find_dominant(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1,
  u32 edge_px, u16 *out_dom_raw, u32 *out_cidx)
{
  const u32 thoff = p->tile_hoff;
  const u32 cp = p->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  const u32 nslots = thoff ? (td1 + td2 + 2u) : (td1 + td2);
  const u32 seg1_cnt = thoff ? (td1 + 1u) : td1;

  u16 cand = 0u;
  s32 vote = 0;
  u32 total_slots = 0u;
  u32 nrows_t = 0u;

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

    for (u32 s = 0u; s < seg1_cnt; s++) {
      u16 rm = map1[s];
      if (vote == 0) {
        cand = rm;
        vote = 1;
      } else if (rm == cand) {
        vote++;
      } else {
        vote--;
      }
    }
    u32 seg2_cnt = nslots - seg1_cnt;
    for (u32 s = 0u; s < seg2_cnt; s++) {
      u16 rm = map2[s];
      if (vote == 0) {
        cand = rm;
        vote = 1;
      } else if (rm == cand) {
        vote++;
      } else {
        vote--;
      }
    }
    total_slots += nslots;
    nrows_t++;
    y += k;
  }

  if (total_slots == 0u || vote <= 0)
    return false;

  u32 cidx = 0u;
  if (!cgba_blend_tile_is_solid_cidx(cand, p->tile_charbase, 0u, 8u, &cidx))
    return false;

  u32 match_cnt = 0u;
  y = sy0;
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
    for (u32 s = 0u; s < seg1_cnt; s++)
      match_cnt += (map1[s] == cand);
    u32 seg2_cnt = nslots - seg1_cnt;
    for (u32 s = 0u; s < seg2_cnt; s++)
      match_cnt += (map2[s] == cand);
    y += k;
  }

  u32 edge_slots = nrows_t * (edge_px >> 3);
  if (edge_slots >= total_slots)
    return false;
  if (match_cnt <= edge_slots ||
      (match_cnt - edge_slots) * 2u < (total_slots - edge_slots))
    return false;

  cgba_blend_dom_hint = cand;
  cgba_blend_dom_hint_valid = true;
  *out_dom_raw = cand;
  *out_cidx = cidx;
  return true;
}

struct cgba_low_bg_info {
  const u8 *sb_row0[8];
  u32 hofs;
  u32 hmask;
  const u8 *charbase;
  u8 vy7[8];
  u8 pad[12];
};

struct cgba_low_layer_ref {
  u8 is_obj;
  u8 idx; /* index into low_bg[] if !is_obj, else OBJ prio 0..3 */
};

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
struct cgba_sparse_band_ctx {
  const u16 *map1;
  const u16 *map2;
  u32 td1;
  u32 td2;
  u32 sx0;
  u32 start_r;
  u32 k;
  u16 *dst_top;
  const u8 *charbase;
  const u32 *term_a;
  const u16 *pal_conv;
  const cgba_low_bg_info *low_bg;
  u32 n_low_bg;
  u32 dom_raw;
  u32 dom_sub;
  u32 dom_word;
  u32 evb;
  u32 saturate;
  u8 row_has_obj[8];
  u32 (*sample_obj_fn)(const void *obj_ctx, u32 px, u32 r);
  const void *obj_ctx;
};

struct cgba_sparse_obj_ctx {
  u32 y;
  const cgba_low_bg_info *low_bg;
  u32 n_low_bg;
  const cgba_low_layer_ref *low_all;
  u32 n_low_all;
  const u8 *row_has_obj;
  u16 *tmp_obj_row;
};

static_assert(sizeof(cgba_low_bg_info) == 64u, "cgba_low_bg_info stride mismatch");
static_assert(__builtin_offsetof(cgba_low_bg_info, sb_row0) == 0u, "sb_row0");
static_assert(__builtin_offsetof(cgba_low_bg_info, hofs) == 32u, "hofs");
static_assert(__builtin_offsetof(cgba_low_bg_info, hmask) == 36u, "hmask");
static_assert(__builtin_offsetof(cgba_low_bg_info, charbase) == 40u, "charbase");
static_assert(__builtin_offsetof(cgba_low_bg_info, vy7) == 44u, "vy7");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, map1) == 0u, "map1");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, map2) == 4u, "map2");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, td1) == 8u, "td1");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, td2) == 12u, "td2");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, sx0) == 16u, "sx0");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, start_r) == 20u, "start_r");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, k) == 24u, "k");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, dst_top) == 28u, "dst_top");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, charbase) == 32u, "charbase");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, term_a) == 36u, "term_a");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, pal_conv) == 40u, "pal_conv");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, low_bg) == 44u, "low_bg");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, n_low_bg) == 48u, "n_low_bg");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, dom_raw) == 52u, "dom_raw");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, dom_sub) == 56u, "dom_sub");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, dom_word) == 60u, "dom_word");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, evb) == 64u, "evb");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, saturate) == 68u, "saturate");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, row_has_obj) == 72u, "row_has_obj");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, sample_obj_fn) == 80u, "sample_obj_fn");
static_assert(__builtin_offsetof(cgba_sparse_band_ctx, obj_ctx) == 84u, "obj_ctx");
#endif

static inline __attribute__((always_inline)) u16 cgba_sample_bg_pixel_4bpp(
  u32 x, u32 r, const cgba_low_bg_info *e, u32 *out_hit)
{
  u32 vx = (x + e->hofs) & e->hmask;
  const u8 *mptr = e->sb_row0[r] + (((vx & 256u) << 3) | ((vx & 0xF8u) >> 2));
  u32 lo = mptr[0];
  u32 hi = mptr[1];
  u32 vy7 = e->vy7[r];
  u32 tr = (hi & 0x08u) ? (7u - vy7) : vy7;
  u32 tc = (hi & 0x04u) ? (7u - (vx & 7u)) : (vx & 7u);
  u32 tid = ((hi & 3u) << 8) | lo;
  u8 byte = e->charbase[(tid << 5) + (tr << 2) + (tc >> 1)];
  u32 pix = (tc & 1u) ? (byte >> 4) : (byte & 0xFu);
  *out_hit = pix;
  if (pix != 0u)
    return palette_ram_converted[(hi & 0xF0u) + pix];
  return 0u;
}

/* Sample the exact un-folded lower-layer RGB565 pixel at (x, py) using
 * strip-row-precomputed BG row pointers and fast OBJ rejection. */
static inline __attribute__((always_inline)) u16 cgba_sample_lower_pixel_fast(
  u32 x, u32 py, u32 r,
  const cgba_low_bg_info *low_bg, u32 n_low_bg,
  const cgba_low_layer_ref *low_all, u32 n_low_all,
  u32 row_obj_mask, u16 *tmp_obj_row)
{
  if (__builtin_expect(row_obj_mask == 0u, 1)) {
    for (u32 i = 0u; i < n_low_bg; i++) {
      u32 hit = 0u;
      u16 col = cgba_sample_bg_pixel_4bpp(x, r, &low_bg[i], &hit);
      if (hit != 0u)
        return col;
    }
    return palette_ram_converted[0];
  }

  for (u32 i = 0u; i < n_low_all; i++) {
    if (low_all[i].is_obj) {
      u32 prio = low_all[i].idx;
      if ((row_obj_mask & (1u << prio)) == 0u)
        continue;
      u32 objcnt = obj_priority_count[prio][py];
      const u8 *objlist = obj_priority_list[prio][py];
      bool hit = false;
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
      for (u32 o = 0u; o < objcnt; o++) {
        const cgba_decoded_obj *d = &decoded_objs[objlist[o]];
        s32 ox = d->sprite.obj_x;
        u8 ff = d->fast_flags;
        s32 ox_end = ox + ((!(ff & 1u) && (d->attr0 & 0x100) && d->sprite.is_double)
                            ? (d->sprite.obj_w << 1) : d->sprite.obj_w);
        if (ox <= (s32)x && (s32)x < ox_end) {
          hit = true;
          break;
        }
      }
#else
      hit = (objcnt != 0u);
#endif
      if (!hit)
        continue;
      write_ioreg(REG_VCOUNT, py);
      tmp_obj_row[x] = 0u;
      render_scanline_objs<u16, INDXCOLOR>(prio, x, x + 1u, tmp_obj_row, NULL);
      if (tmp_obj_row[x] != 0u)
        return palette_ram_converted[tmp_obj_row[x] & 0x1FFu];
    } else {
      u32 hit = 0u;
      u16 col = cgba_sample_bg_pixel_4bpp(x, r, &low_bg[low_all[i].idx], &hit);
      if (hit != 0u)
        return col;
    }
  }
  return palette_ram_converted[0];
}

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
static u32 cgba_sparse_sample_obj_cb(const void *raw_ctx, u32 px, u32 r)
{
  const cgba_sparse_obj_ctx *c = (const cgba_sparse_obj_ctx *)raw_ctx;
  u32 py = c->y + r;
  u32 mask = (u32)c->row_has_obj[r];
#if CGBA_VIDEO_SPRITE_DECODE_CACHE
  bool any_bbox = false;
  for (u32 prio = 0u; mask != 0u; prio++, mask >>= 1) {
    if (!(mask & 1u))
      continue;
    u32 objcnt = obj_priority_count[prio][py];
    const u8 *objlist = obj_priority_list[prio][py];
    for (u32 o = 0u; o < objcnt; o++) {
      const cgba_decoded_obj *d = &decoded_objs[objlist[o]];
      s32 ox = d->sprite.obj_x;
      u8 ff = d->fast_flags;
      s32 ox_end = ox + ((!(ff & 1u) && (d->attr0 & 0x100) && d->sprite.is_double)
                          ? (d->sprite.obj_w << 1) : d->sprite.obj_w);
      if (ox <= (s32)px && (s32)px < ox_end) {
        any_bbox = true;
        break;
      }
    }
    if (any_bbox)
      break;
  }
  if (!any_bbox)
    return 0xFFFFFFFFu;
#endif
  return (u32)cgba_sample_lower_pixel_fast(
    px, py, r, c->low_bg, c->n_low_bg,
    c->low_all, c->n_low_all, (u32)c->row_has_obj[r], c->tmp_obj_row);
}
#endif

/* Re-blend only non-dominant pixels of blend_layer after lower layers were
 * rendered with cgba_folded_pal for dom_cidx. */
template<bool saturate>
static __attribute__((noinline)) void cgba_render_4bpp_bg_sparse_override_t(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *screen_base,
  const u8 *lorder, u32 blend_idx, u16 dom_raw, u32 dom_cidx,
  u32 eva, u32 evb)
{
  const u32 *const term_a = cgba_bg_blend_term_a_get(eva);
  const u32 dom_sub = dom_cidx & 0xF0u;
  const u32 dom_nib = dom_cidx & 0x0Fu;
  const u32 dom_word = dom_nib * 0x11111111u;
  const u32 thoff = p->tile_hoff;
  const u32 cp = p->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  const u8 *const charbase = p->tile_charbase;
  u16 tmp_obj_row[240];

  /* Precompute top-to-bottom lower-layer tables once per strip. */
  cgba_low_bg_info low_bg[4];
  u8 low_bg_id[4];
  cgba_low_layer_ref low_all[8];
  u32 n_low_bg = 0u;
  u32 n_low_all = 0u;
  u32 low_obj_mask = 0u;
  for (u32 i = blend_idx; i > 0u; i--) {
    u32 layer = lorder[i - 1u];
    if (layer & 0x4u) {
      u8 prio = (u8)(layer & 3u);
      low_all[n_low_all].is_obj = 1u;
      low_all[n_low_all].idx = prio;
      n_low_all++;
      low_obj_mask |= 1u << prio;
    } else {
      u8 bi = (u8)n_low_bg++;
      low_bg_id[bi] = (u8)layer;
      low_bg[bi].hofs = cgba_m0.bg_hofs[layer];
      low_bg[bi].hmask = cgba_m0.bg_wide[layer] ? 511u : 255u;
      low_bg[bi].charbase = cgba_m0.plans[layer].tile_charbase;
      low_all[n_low_all].is_obj = 0u;
      low_all[n_low_all].idx = bi;
      n_low_all++;
    }
  }

  auto emit_pix = [&](u32 px, u32 py, u32 r, u32 pix, const u32 *ta,
                      u16 *dst_row, u32 row_obj_mask) {
    u16 raw_dst = cgba_sample_lower_pixel_fast(
      px, py, r, low_bg, n_low_bg, low_all, n_low_all, row_obj_mask, tmp_obj_row);
    if (pix == 0u)
      dst_row[px] = raw_dst;
    else
      dst_row[px] = cgba_blend_rgb565_direct<saturate>(ta[pix], raw_dst, evb);
  };

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  cgba_sparse_obj_ctx obj_ctx;
  obj_ctx.low_bg = low_bg;
  obj_ctx.n_low_bg = n_low_bg;
  obj_ctx.low_all = low_all;
  obj_ctx.n_low_all = n_low_all;
  obj_ctx.tmp_obj_row = tmp_obj_row;

  cgba_sparse_band_ctx band_ctx;
  band_ctx.td1 = td1;
  band_ctx.td2 = td2;
  band_ctx.sx0 = thoff ? (8u - thoff) : 0u;
  band_ctx.charbase = charbase;
  band_ctx.term_a = term_a;
  band_ctx.pal_conv = palette_ram_converted;
  band_ctx.low_bg = low_bg;
  band_ctx.n_low_bg = n_low_bg;
  band_ctx.dom_raw = (u32)dom_raw;
  band_ctx.dom_sub = dom_sub;
  band_ctx.dom_word = dom_word;
  band_ctx.evb = evb;
  band_ctx.saturate = saturate ? 1u : 0u;
  band_ctx.sample_obj_fn = cgba_sparse_sample_obj_cb;
  band_ctx.obj_ctx = &obj_ctx;
  obj_ctx.row_has_obj = band_ctx.row_has_obj;

  auto handle_part_tile = [&](u16 raw_map, u32 sx, u32 tc0, u32 w_pix,
                              u32 y, u32 start_r, u32 k, u16 *dst_top) {
    if (raw_map == dom_raw)
      return;
    u32 me = eswap16(raw_map);
    u32 sub = (me >> 8) & 0xF0u;
    bool same_sub = (sub == dom_sub);
    bool hflip = (me & 0x400u) != 0u;
    const u8 *row = charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
    s32 row_step = 4;
    if (me & 0x800u) {
      row += 28 - (s32)(start_r << 3);
      row_step = -4;
    }
    const u32 *ta = term_a + sub;
    u16 *dst_row = dst_top;
    for (u32 r = 0u; r < k; r++, row += row_step, dst_row += 240u) {
      u32 rw = *(const u32 *)row;
      if (same_sub && rw == dom_word)
        continue;
      u32 py = y + r;
      u32 row_obj_mask = (u32)band_ctx.row_has_obj[r];
      for (u32 i = 0u; i < w_pix; i++) {
        u32 c = tc0 + i;
        u32 sc = hflip ? (7u - c) : c;
        u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
        if (same_sub && pix == dom_nib)
          continue;
        emit_pix(sx + i, py, r, pix, ta, dst_row, row_obj_mask);
      }
    }
  };
#else
  const u32 nslots = thoff ? (td1 + td2 + 2u) : (td1 + td2);
  const u32 seg1_cnt = thoff ? (td1 + 1u) : td1;
#endif

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
    u16 *dst_top = screen_base + y * 240u;

    /* Precompute lower-BG map-row pointers for all k rows ONCE per tile-row. */
    for (u32 i = 0u; i < n_low_bg; i++) {
      const cgba_mode0_bg_plan *bp = &cgba_m0.plans[low_bg_id[i]];
      u32 vy0 = (y + bp->vofs) & 511u;
      u32 sr = vy0 & 7u;
      u32 split = 8u - sr;
      const u8 *sb_a = (const u8 *)bp->sb_ptr[vy0 >> 8] + ((vy0 & 0xF8u) << 3);
      const u8 *sb_b = sb_a;
      if (k > split) {
        u32 vy1 = (vy0 + split) & 511u;
        sb_b = (const u8 *)bp->sb_ptr[vy1 >> 8] + ((vy1 & 0xF8u) << 3);
      }
      for (u32 r = 0u; r < k; r++) {
        if (r < split) {
          low_bg[i].sb_row0[r] = sb_a;
          low_bg[i].vy7[r] = (u8)(sr + r);
        } else {
          low_bg[i].sb_row0[r] = sb_b;
          low_bg[i].vy7[r] = (u8)(r - split);
        }
      }
    }

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
    obj_ctx.y = y;
    for (u32 r = 0u; r < k; r++)
      band_ctx.row_has_obj[r] = (u8)(low_obj_mask & (u32)obj_row_prio_mask[y + r]);

    if (thoff) {
      handle_part_tile(map1[0], 0u, thoff, 8u - thoff, y, start_r, k, dst_top);
      band_ctx.map1 = map1 + 1;
      band_ctx.map2 = map2;
      band_ctx.start_r = start_r;
      band_ctx.k = k;
      band_ctx.dst_top = dst_top;
      cgba_sparse_override_band_sh4(&band_ctx);
      handle_part_tile(map2[td2], 240u - thoff, 0u, thoff, y, start_r, k, dst_top);
    } else {
      band_ctx.map1 = map1;
      band_ctx.map2 = map2;
      band_ctx.start_r = start_r;
      band_ctx.k = k;
      band_ctx.dst_top = dst_top;
      cgba_sparse_override_band_sh4(&band_ctx);
    }
#else
    u32 sx = 0u;
    for (u32 s = 0u; s < nslots; s++) {
      u32 tc0 = 0u, w_pix = 8u;
      if (thoff) {
        if (s == 0u) {
          tc0 = thoff;
          w_pix = 8u - thoff;
        } else if (s == nslots - 1u) {
          w_pix = thoff;
        }
      }
      u16 raw_map = (s < seg1_cnt) ? map1[s] : map2[s - seg1_cnt];
      if (__builtin_expect(raw_map == dom_raw, 1)) {
        sx += w_pix;
        continue;
      }
      u32 me = eswap16(raw_map);
      u32 sub = (me >> 8) & 0xF0u;
      bool same_sub = (sub == dom_sub);
      bool hflip = (me & 0x400u) != 0u;
      const u8 *row = charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
      s32 row_step = 4;
      if (me & 0x800u) {
        row += 28 - (s32)(start_r << 3);
        row_step = -4;
      }
      const u32 *ta = term_a + sub;
      u16 *dst_row = dst_top;
      if (__builtin_expect(w_pix == 8u && same_sub, 1)) {
        for (u32 r = 0u; r < k; r++, row += row_step, dst_row += 240u) {
          u32 rw = *(const u32 *)row;
          u32 diff = rw ^ dom_word;
          if (__builtin_expect(diff == 0u, 1))
            continue;
          u32 py = y + r;
          u32 row_obj_mask = low_obj_mask & (u32)obj_row_prio_mask[py];
          u32 d = eswap32(diff);
          u32 v = eswap32(rw);
          if (!hflip) {
            u32 px = sx;
            if (!(d & 0xFFFFu)) {
              d >>= 16;
              v >>= 16;
              px += 4u;
            }
            do {
              if (d & 0xFu)
                emit_pix(px, py, r, v & 0xFu, ta, dst_row, row_obj_mask);
              d >>= 4;
              v >>= 4;
              px++;
            } while (d);
          } else {
            u32 px = sx + 7u;
            if (!(d & 0xFFFFu)) {
              d >>= 16;
              v >>= 16;
              px -= 4u;
            }
            do {
              if (d & 0xFu)
                emit_pix(px, py, r, v & 0xFu, ta, dst_row, row_obj_mask);
              d >>= 4;
              v >>= 4;
              px--;
            } while (d);
          }
        }
      } else {
        for (u32 r = 0u; r < k; r++, row += row_step, dst_row += 240u) {
          u32 rw = *(const u32 *)row;
          if (same_sub && rw == dom_word)
            continue;
          u32 py = y + r;
          u32 row_obj_mask = low_obj_mask & (u32)obj_row_prio_mask[py];
          for (u32 i = 0u; i < w_pix; i++) {
            u32 c = tc0 + i;
            u32 sc = hflip ? (7u - c) : c;
            u32 pix = (row[sc >> 1] >> ((sc & 1u) << 2)) & 0xFu;
            if (same_sub && pix == dom_nib)
              continue;
            emit_pix(sx + i, py, r, pix, ta, dst_row, row_obj_mask);
          }
        }
      }
      sx += w_pix;
    }
#endif
    y += k;
  }
}

static inline void cgba_render_4bpp_bg_sparse_override(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *screen_base,
  const u8 *lorder, u32 blend_idx, u16 dom_raw, u32 dom_cidx,
  u32 eva, u32 evb)
{
  if ((eva + evb) > 16u)
    cgba_render_4bpp_bg_sparse_override_t<true>(
      p, sy0, sy1, screen_base, lorder, blend_idx, dom_raw, dom_cidx, eva, evb);
  else
    cgba_render_4bpp_bg_sparse_override_t<false>(
      p, sy0, sy1, screen_base, lorder, blend_idx, dom_raw, dom_cidx, eva, evb);
}

static __attribute__((noinline)) void cgba_render_4bpp_part_strip_even_base(
  u16 *dst_top, u32 k, u32 start, u32 end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r, const u32 *pair_lut)
{
  u32 tile = eswap16(raw_map);
  const u8 *row = tile_charbase + ((tile & 0x3FFu) << 5) + (start_r << 2);
  s32 row_step = 4;
  if (tile & 0x800u) {
    row += 28 - (s32)(start_r << 3);
    row_step = -4;
  }
  const u32 *plut = pair_lut + (((tile >> 12) & 0xFu) << 8);
  const u32 b0 = start >> 1;
  const u32 b1 = end >> 1;
  u16 *dst_row = dst_top;
  if (__builtin_expect((tile & 0x400u) != 0u, 0)) {
    for (u32 r = 0u; r < k; r++, row += row_step, dst_row += 240u) {
      u32 *d32 = (u32 *)dst_row;
      for (u32 b = b0; b < b1; b++) {
        u32 v = plut[row[3u - b]];
        *d32++ = (v >> 16) | (v << 16);
      }
    }
  } else {
    for (u32 r = 0u; r < k; r++, row += row_step, dst_row += 240u) {
      u32 *d32 = (u32 *)dst_row;
      for (u32 b = b0; b < b1; b++)
        *d32++ = plut[row[b]];
    }
  }
}

template<bool isbase>
static __attribute__((noinline)) void cgba_render_4bpp_part_strip_fullcolor(
  u16 *dst_top, u32 k, u32 start, u32 end, u16 raw_map,
  const u8 *tile_charbase, u32 start_r, const u16 *paltbl)
{
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  u32 w = end - start;
  if (w && k) {
    cgba_render_4bpp_part_strip_sh4(
      dst_top, (start << 16) | (w << 8) | k,
      ((u32)isbase << 16) | (u32)raw_map,
      tile_charbase, start_r, paltbl);
  }
#else
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

  auto fill_const = [](u16 *d, u32 w, u16 c) {
    switch (w) {
      case 7u: d[6] = c; /* fallthrough */
      case 6u: d[5] = c; /* fallthrough */
      case 5u: d[4] = c; /* fallthrough */
      case 4u: d[3] = c; /* fallthrough */
      case 3u: d[2] = c; /* fallthrough */
      case 2u: d[1] = c; /* fallthrough */
      case 1u: d[0] = c; break;
      default:
        for (u32 i = 0; i < w; i++) d[i] = c;
        break;
    }
  };

  if (__builtin_expect((tile & 0x400u) != 0u, 0)) {
    for (u32 r = 0; r < k; r++, row += row_step, dst_row += 240u) {
      u32 raw = *(const u32 *)row;
      if (!raw) {
        if (isbase)
          fill_const(dst_row, width, bg);
        continue;
      }
      if (isbase && (((raw << 4) ^ raw) >> 4) == 0u) {
        fill_const(dst_row, width, subpal[raw & 0xFu]);
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
        if (isbase)
          fill_const(dst_row, width, bg);
        continue;
      }
      if (isbase && (((raw << 4) ^ raw) >> 4) == 0u) {
        fill_const(dst_row, width, subpal[raw & 0xFu]);
        continue;
      }
      u32 tilepix = eswap32(raw) >> shift;
#ifdef CGBA_VIDEO_BACKDROP_SHADOW_PALETTE
      if (isbase) {
        switch (width) {
          case 7u: dst_row[6] = subpal[(tilepix >> 24) & 0xFu]; /* fallthrough */
          case 6u: dst_row[5] = subpal[(tilepix >> 20) & 0xFu]; /* fallthrough */
          case 5u: dst_row[4] = subpal[(tilepix >> 16) & 0xFu]; /* fallthrough */
          case 4u: dst_row[3] = subpal[(tilepix >> 12) & 0xFu]; /* fallthrough */
          case 3u: dst_row[2] = subpal[(tilepix >>  8) & 0xFu]; /* fallthrough */
          case 2u: dst_row[1] = subpal[(tilepix >>  4) & 0xFu]; /* fallthrough */
          case 1u: dst_row[0] = subpal[tilepix & 0xFu]; break;
          default:
            for (u32 i = 0; i < width; i++) {
              dst_row[i] = subpal[tilepix & 0xFu];
              tilepix >>= 4;
            }
            break;
        }
        continue;
      }
#endif
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
#endif
}

static inline void cgba_dispatch_4bpp_strip_sh4(
  bool base_done, bool aligned, const u16 *map1, u32 cp, u16 *dst_top,
  const u8 *charbase, u32 rk, const u32 *pair_lut, const u16 *map2)
{
  if (__builtin_expect(aligned, 1)) {
    if (base_done)
      cgba_render_4bpp_strip_over_sh4(
        map1, cp, dst_top, charbase, rk, pair_lut, map2);
    else
      cgba_render_4bpp_strip_base_sh4(
        map1, cp, dst_top, charbase, rk, pair_lut, map2);
  } else {
    if (base_done)
      cgba_render_4bpp_strip_over_u16_sh4(
        map1, cp, dst_top, charbase, rk, pair_lut, map2);
    else
      cgba_render_4bpp_strip_base_u16_sh4(
        map1, cp, dst_top, charbase, rk, pair_lut, map2);
  }
}

static inline void cgba_render_4bpp_part_edge_sh4(
  u16 *dst, u32 k, u32 col0, u32 col1, u16 raw_map,
  const u8 *charbase, u32 start_r, bool base_done,
  const u16 *pal, const u16 *bpal, const u32 *pair_lut)
{
  if (base_done) {
    cgba_render_4bpp_part_strip_fullcolor<false>(
      dst, k, col0, col1, raw_map, charbase, start_r, pal);
  } else if ((((uintptr_t)dst & 3u) == 0u) && (((col0 | col1) & 1u) == 0u)) {
    cgba_render_4bpp_part_strip_even_base(
      dst, k, col0, col1, raw_map, charbase, start_r, pair_lut);
  } else {
    cgba_render_4bpp_part_strip_fullcolor<true>(
      dst, k, col0, col1, raw_map, charbase, start_r, bpal);
  }
}

static inline void cgba_render_4bpp_hspan_seg_sh4(
  const cgba_bg_hspan_plan *sp, u8 *map_row_b, u16 *dst_top,
  const u8 *charbase, u32 start_r, u32 k, u32 rk, bool base_done,
  const u16 *pal, const u16 *bpal, const u32 *pair_lut)
{
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
    cgba_dispatch_4bpp_strip_sh4(
      base_done, sp->mid_aligned != 0u, mid_map1, mid_cp, dst_top + sp->mid_x,
      charbase, rk, pair_lut, map2);
  }
  if (left_w)
    cgba_render_4bpp_part_edge_sh4(
      dst_top + sp->x0, k, sp->thoff, sp->thoff + left_w, left_raw,
      charbase, start_r, base_done, pal, bpal, pair_lut);
  if (right_w)
    cgba_render_4bpp_part_edge_sh4(
      dst_top + sp->right_x, k, 0u, right_w, right_raw,
      charbase, start_r, base_done, pal, bpal, pair_lut);
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_split_sh4(
  u32 layer, u32 sy0, u32 sy1, u32 xlo, u32 xhi,
  u16 *screen_base, u32 base_done, bool invert_fold = false)
{
  const cgba_mode0_bg_plan *const p = &cgba_m0.plans[layer];
  const u32 hofs = cgba_m0.bg_hofs[layer];
  const u32 wide = cgba_m0.bg_wide[layer];
  const u16 *const pal_conv = palette_ram_converted;
  const u16 *const base_pal = backdrop_shadow_palette;
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];
  const u32 *const fpair_lut = (const u32 *)&cgba_folded_pair_lut[0][0][0];
  const u16 *const outer_pal = invert_fold ? pal_conv : cgba_folded_pal;
  const u16 *const outer_bpal = invert_fold ? base_pal : cgba_folded_bg_shadow;
  const u32 *const outer_plut = invert_fold ? pair_lut : fpair_lut;
  const u16 *const inner_pal = invert_fold ? cgba_folded_pal : pal_conv;
  const u16 *const inner_bpal = invert_fold ? cgba_folded_bg_shadow : base_pal;
  const u32 *const inner_plut = invert_fold ? fpair_lut : pair_lut;

  cgba_bg_hspan_plan segs[3];
  const u16 *seg_pal[3];
  const u16 *seg_bpal[3];
  const u32 *seg_plut[3];
  u32 nseg = 0u;

  if (xlo > 0u) {
    cgba_build_bg_hspan_plan(hofs, wide, 0u, xlo, &segs[nseg]);
    seg_pal[nseg] = outer_pal;
    seg_bpal[nseg] = outer_bpal;
    seg_plut[nseg] = outer_plut;
    nseg++;
  }
  cgba_build_bg_hspan_plan(hofs, wide, xlo, xhi, &segs[nseg]);
  seg_pal[nseg] = inner_pal;
  seg_bpal[nseg] = inner_bpal;
  seg_plut[nseg] = inner_plut;
  nseg++;
  if (xhi < 240u) {
    cgba_build_bg_hspan_plan(hofs, wide, xhi, 240u, &segs[nseg]);
    seg_pal[nseg] = outer_pal;
    seg_bpal[nseg] = outer_bpal;
    seg_plut[nseg] = outer_plut;
    nseg++;
  }

  const u8 *const charbase = p->tile_charbase;
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (base_done && cgba_4bpp_strip_row_zero_skip(p, vhalf, voffset, start_r, k)) {
      y += k;
      continue;
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *dst_top = screen_base + y * 240u;
    u32 rk = (start_r << 8) | k;

    for (u32 s = 0; s < nseg; s++) {
      cgba_render_4bpp_hspan_seg_sh4(
        &segs[s], map_row_b, dst_top, charbase, start_r, k, rk,
        base_done != 0u, seg_pal[s], seg_bpal[s], seg_plut[s]);
    }
    y += k;
  }
}

static inline void cgba_render_4bpp_bg_blend_span(
  const cgba_mode0_bg_plan *p, u32 hofs, u32 wide,
  u32 sy0, u32 sy1, u32 x0, u32 x1,
  u16 *screen_base, u32 eva, u32 evb)
{
  if (x1 <= x0 || (eva == 0u && evb == 16u))
    return;
  cgba_bg_hspan_plan sp;
  cgba_build_bg_hspan_plan(hofs, wide, x0, x1, &sp);
  cgba_render_4bpp_bg_blend_hspan_rows(
    p, &sp, sy0, sy1, screen_base + sy0 * 240u, eva, evb);
}

template<typename Fn>
static inline void cgba_folded_prepare_impl(u32 key, Fn transform)
{
#if !CGBA_PALETTE_FOLDED_BIT
#error "CGBA_PALETTE_FOLDED_BIT must be non-zero when folded palette prepare is enabled"
#endif
  static_assert((CGBA_PALETTE_DIRTY_ACTIVE & CGBA_PALETTE_DIRTY_FOLDED) != 0u,
                "CGBA_PALETTE_DIRTY_ACTIVE must include CGBA_PALETTE_DIRTY_FOLDED");
  static_assert((CGBA_PALETTE_DIRTY_OBJ_ONLY & CGBA_PALETTE_DIRTY_FOLDED_OBJ) != 0u,
                "CGBA_PALETTE_DIRTY_OBJ_ONLY must include CGBA_PALETTE_DIRTY_FOLDED_OBJ");
  if (cgba_folded_union_owner != 1u) {
    cgba_folded_valid = false;
    cgba_folded_union_owner = 1u;
  }
  bool key_changed = !cgba_folded_valid || (cgba_folded_key != key);
  u32 dirty = palette_ram_dirty & (CGBA_PALETTE_DIRTY_FOLDED | CGBA_PALETTE_DIRTY_FOLDED_OBJ);
  if (__builtin_expect(!key_changed && dirty == 0u, 1))
    return;
  palette_ram_dirty &= ~(CGBA_PALETTE_DIRTY_FOLDED | CGBA_PALETTE_DIRTY_FOLDED_OBJ);

  if (key_changed || (dirty & CGBA_PALETTE_DIRTY_FOLDED)) {
    bool bd_changed = key_changed ||
                      (cgba_folded_pal_snap[0] != palette_ram_converted[0]);
    u16 bd_folded = transform(palette_ram_converted[0]);
    cgba_folded_pal[0] = bd_folded;

    for (u32 sub = 0; sub < 16u; sub++) {
      u32 base = sub << 4;
      if (!key_changed && !bd_changed &&
          cgba_memeq_u32(&cgba_folded_pal_snap[base], &palette_ram_converted[base], 8u))
        continue;
      memcpy(&cgba_folded_pal_snap[base], &palette_ram_converted[base],
             16u * sizeof(u16));
      u16 *fpal = &cgba_folded_pal[base];
      u16 *fshd = &cgba_folded_bg_shadow[base];
      const u16 *src = &palette_ram_converted[base];
      for (u32 c = 0; c < 16u; c++)
        fpal[c] = transform(src[c]);
      fshd[0] = bd_folded;
      memcpy(fshd + 1, fpal + 1, 15u * sizeof(u16));
      u16 (*out)[2] = cgba_folded_pair_lut[sub];
      for (u32 b = 0; b < 256u; b++) {
        out[b][0] = fshd[b & 15u];
        out[b][1] = fshd[b >> 4];
      }
    }
  }

  if (key_changed ||
      !cgba_memeq_u32(&cgba_folded_pal_snap[256], &palette_ram_converted[256], 128u)) {
    memcpy(&cgba_folded_pal_snap[256], &palette_ram_converted[256],
           256u * sizeof(u16));
    for (u32 c = 256u; c < 512u; c++)
      cgba_folded_pal[c] = transform(palette_ram_converted[c]);
  }
  cgba_folded_key = key;
  cgba_folded_valid = true;
}

static __attribute__((noinline)) void cgba_folded_palette_prepare(
  u16 p1, u32 eva, u32 evb)
{
  u32 key = ((u32)p1 << 16) | (eva << 8) | evb;
  u32 ta = (((u32)p1 | ((u32)p1 << 16)) & BLND_MSK) * eva;
  if ((eva + evb) > 16u) {
    cgba_folded_prepare_impl(key, [=](u16 c) {
      return cgba_blend_rgb565_direct<true>(ta, c, evb);
    });
  } else {
    cgba_folded_prepare_impl(key, [=](u16 c) {
      return cgba_blend_rgb565_direct<false>(ta, c, evb);
    });
  }
}

static inline u16 cgba_apply_brightness_rgb565(u16 pixcol, u32 effect, u32 evy)
{
  u32 epixel = ((u32)pixcol | ((u32)pixcol << 16)) & BLND_MSK;
  u32 pa = (effect == COL_EFFECT_DARK) ? 0u
                                       : (((BLND_MSK * evy) >> 4) & BLND_MSK);
  u32 pb = ((epixel * (16u - evy)) >> 4) & BLND_MSK;
  epixel = (pa + pb) & BLND_MSK;
  return (u16)((epixel >> 16) | epixel);
}

static __attribute__((noinline)) void cgba_folded_brightness_prepare(
  u32 effect, u32 evy)
{
  /* Bit 15 is always 0 in alpha-blend keys (eva <= 16), keeping brightness
   * keys disjoint from alpha-blend keys in the shared folded-palette cache. */
  u32 key = 0x00008000u | (effect << 8) | evy;
  cgba_folded_prepare_impl(key, [=](u16 c) {
    return cgba_apply_brightness_rgb565(c, effect, evy);
  });
}

static __attribute__((noinline)) void cgba_render_4bpp_text_bg_strip_sh4(
  const cgba_mode0_bg_plan *p, u32 sy0, u32 sy1, u16 *screen_base,
  bool base_done, const u16 *cur_pal, const u16 *cur_base_pal,
  const u32 *cur_pair_lut, const u32 *row_empty = NULL)
{
  cgba_bg_hspan_plan sp;
  cgba_bg_hspan_from_plan(p, &sp);
  const u8 *const charbase = p->tile_charbase;
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (base_done) {
      if (row_empty != NULL &&
          (row_empty[vhalf] & (1u << ((voffset & 255u) >> 3)))) {
        y += k;
        continue;
      }
      if (cgba_4bpp_strip_row_zero_skip(p, vhalf, voffset, start_r, k)) {
        y += k;
        continue;
      }
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *dst_top = screen_base + y * 240u;
    u32 rk = (start_r << 8) | k;
    cgba_render_4bpp_hspan_seg_sh4(
      &sp, map_row_b, dst_top, charbase, start_r, k, rk,
      base_done, cur_pal, cur_base_pal, cur_pair_lut);
    y += k;
  }
}

static __attribute__((noinline)) void cgba_render_4bpp_merged_strip_sh4(
  const u8 *ml, u32 mcnt, u32 sy0, u32 sy1, u16 *screen_base, u32 base_done,
  const u16 *l_pal, const u16 *l_bpal, const u32 *l_plut, u16 bd_color)
{
  const cgba_mode0_bg_plan *const plans = cgba_m0.plans;
  const cgba_mode0_bg_plan *const p0_ref = &plans[ml[0]];
  const u8 *const charbase = p0_ref->tile_charbase;
  const u32 cbb_tile_base = (u32)(charbase - vram) >> 5;
  const u32 thoff = p0_ref->tile_hoff;
  const u32 left_w = thoff ? (8u - thoff) : 0u;
  const u32 nslots = thoff ? 31u : 30u;
  const bool mid_aligned = !(thoff & 1u);
  alignas(4) u16 merged[32];

  auto draw_merged = [&](u16 *dst_top, u32 start_r, u32 k, u32 rk, u32 is_over) {
    bool ov = is_over != 0u;
    if (thoff == 0u) {
      cgba_dispatch_4bpp_strip_sh4(
        ov, true, merged, 30u, dst_top, charbase, rk, l_plut, merged + 30);
    } else {
      cgba_dispatch_4bpp_strip_sh4(
        ov, mid_aligned, merged + 1, 29u, dst_top + left_w, charbase, rk,
        l_plut, merged + 30);
      cgba_render_4bpp_part_edge_sh4(
        dst_top, k, thoff, 8u, merged[0], charbase, start_r, ov,
        l_pal, l_bpal, l_plut);
      cgba_render_4bpp_part_edge_sh4(
        dst_top + left_w + 232u, k, 0u, thoff, merged[30], charbase, start_r,
        ov, l_pal, l_bpal, l_plut);
    }
  };

  auto draw_single = [&](const cgba_mode0_bg_plan *q, u16 *map1, u16 *map2,
                         u16 *dst_top, u32 start_r, u32 k, u32 rk, u32 is_over) {
    bool ov = is_over != 0u;
    u32 cp = q->counts_packed;
    if (thoff == 0u) {
      cgba_dispatch_4bpp_strip_sh4(
        ov, true, map1, cp, dst_top, charbase, rk, l_plut, map2);
    } else {
      u16 left_raw = *map1++;
      u16 *mid_map1 = (cp & 0xFFFFu) ? map1 : map2;
      u32 mid_cp = (cp & 0xFFFFu) ? cp : (cp >> 16);
      u16 right_raw = *(map2 + (cp >> 16));
      cgba_dispatch_4bpp_strip_sh4(
        ov, mid_aligned, mid_map1, mid_cp, dst_top + left_w, charbase, rk,
        l_plut, map2);
      cgba_render_4bpp_part_edge_sh4(
        dst_top, k, thoff, 8u, left_raw, charbase, start_r, ov,
        l_pal, l_bpal, l_plut);
      cgba_render_4bpp_part_edge_sh4(
        dst_top + left_w + 232u, k, 0u, thoff, right_raw, charbase, start_r,
        ov, l_pal, l_bpal, l_plut);
    }
  };

  u32 y = sy0;
  while (y < sy1) {
    u32 voff0 = (y + p0_ref->vofs) & 511u;
    u32 start_r = voff0 & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u16 *dst_top = screen_base + y * 240u;
    u32 rk = (start_r << 8) | k;

    const cgba_mode0_bg_plan *ap[3];
    u16 *am1[3], *am2[3];
    u32 aseg1[3];
    u32 acnt = 0u;
    for (u32 i = 0u; i < mcnt; i++) {
      u32 li = ml[i];
      const cgba_mode0_bg_plan *q = &plans[li];
      u32 voff = (y + q->vofs) & 511u;
      u32 vhalf = voff >> 8;
      u32 rbit = 1u << ((voff & 255u) >> 3);
      if (cgba_m0.row_all_empty[li][vhalf] & rbit)
        continue;
      u8 *mrb = (u8 *)q->sb_ptr[vhalf] + ((voff & 0xF8u) << 3);
      ap[acnt] = q;
      am1[acnt] = (u16 *)(mrb + q->first_byte_off);
      am2[acnt] = (u16 *)(mrb + q->map2_byte_off);
      aseg1[acnt] = (q->counts_packed & 0xFFFFu) + (thoff ? 1u : 0u);
      acnt++;
    }

    if (acnt == 0u) {
      if (!base_done) {
        for (u32 r = 0u; r < k; r++)
          cgba_fill_row_u16(dst_top + r * 240u, bd_color);
      }
    } else if (acnt == 1u) {
      draw_single(ap[0], am1[0], am2[0], dst_top, start_r, k, rk, base_done);
    } else if (acnt == 2u) {
      if (cgba_mode0_try_merge_2(
            merged, nslots, cbb_tile_base, am1, am2, aseg1)) {
        draw_merged(dst_top, start_r, k, rk, base_done);
      } else {
        draw_single(ap[0], am1[0], am2[0], dst_top, start_r, k, rk, base_done);
        draw_single(ap[1], am1[1], am2[1], dst_top, start_r, k, rk, 1u);
      }
    } else {
      if (cgba_mode0_try_merge_3(
            merged, nslots, cbb_tile_base, am1, am2, aseg1)) {
        draw_merged(dst_top, start_r, k, rk, base_done);
      } else if (cgba_mode0_try_merge_2(
                   merged, nslots, cbb_tile_base, am1, am2, aseg1)) {
        draw_merged(dst_top, start_r, k, rk, base_done);
        draw_single(ap[2], am1[2], am2[2], dst_top, start_r, k, rk, 1u);
      } else {
        draw_single(ap[0], am1[0], am2[0], dst_top, start_r, k, rk, base_done);
        draw_single(ap[1], am1[1], am2[1], dst_top, start_r, k, rk, 1u);
        draw_single(ap[2], am1[2], am2[2], dst_top, start_r, k, rk, 1u);
      }
    }
    y += k;
  }
}

static __attribute__((noinline)) void render_mode0_fullcolor_strip_sh4(
  u32 sy0, u32 sy1, u16 *screen_base, const u8 *lorder, u32 lcount)
{
  const cgba_mode0_bg_plan *const plans = cgba_m0.plans;
  const u16 *const pal_conv = palette_ram_converted;
  const u16 *const base_pal = backdrop_shadow_palette;
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];
  const u32 *const fpair_lut = (const u32 *)&cgba_folded_pair_lut[0][0][0];

  const u32 blend_layer = cgba_m0.blend_bg_layer;
  const u32 bright_eff = cgba_m0.bright_effect;
  const u32 bright_t1 = bright_eff ? (u32)cgba_m0.bright_tgt1_mask : 0x3Fu;
  bool folded = false;
  bool sparse_override = false;
  u16 sparse_dom_raw = 0u;
  u32 sparse_dom_cidx = 0u;
  bool split_active = false;
  bool invert_fold = false;
  u32 split_xlo = 0u, split_xhi = 240u;
  u32 raw_xlo = 0u, raw_xhi = 240u;
  u32 split_cidx = 0u;
  if (__builtin_expect(bright_eff != 0u, 0)) {
    cgba_folded_brightness_prepare(bright_eff, cgba_m0.bright_evy);
    if (!cgba_m0.bright_win_active) {
      folded = true;
    } else {
      bool in_y = (sy0 >= cgba_m0.bright_win_y0 && sy0 < cgba_m0.bright_win_y1);
      bool eff_in = (cgba_m0.bright_win_eff_in != 0u);
      if (!in_y) {
        folded = !eff_in;
      } else if (cgba_m0.bright_win_x0 == 0u && cgba_m0.bright_win_x1 >= 240u) {
        folded = eff_in;
      } else if (cgba_m0.bright_win_x0 >= cgba_m0.bright_win_x1) {
        folded = !eff_in;
      } else {
        split_active = true;
        invert_fold = eff_in;
        split_xlo = cgba_m0.bright_win_x0;
        split_xhi = cgba_m0.bright_win_x1;
      }
    }
  } else if (__builtin_expect(blend_layer < 4u &&
                              (plans[blend_layer].flags & 1u) &&
                              !(cgba_m0.blend_eva == 0u && cgba_m0.blend_evb == 16u), 0)) {
    bool lower_4bpp_ok = true;
    for (u32 i = 0; i < lcount; i++) {
      u32 l = lorder[i];
      if (l == blend_layer)
        break;
      if (!(l & 4u) && !(plans[l].flags & 1u)) {
        lower_4bpp_ok = false;
        break;
      }
    }
    u32 cidx = 0u, xlo = 0u, xhi = 240u;
    u32 hcls = cgba_blend_strip_classify_hint(
      &plans[blend_layer], sy0, sy1, lower_4bpp_ok,
      &sparse_dom_raw, &cidx, &xlo, &xhi);
    bool analyzed = false;
    bool use_sparse = false;
    if (hcls == 1u) {
      cgba_folded_palette_prepare(
        pal_conv[cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
      folded = true;
    } else if (hcls == 2u) {
      sparse_dom_cidx = cidx;
      cgba_folded_palette_prepare(
        pal_conv[sparse_dom_cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
      folded = true;
      sparse_override = true;
      use_sparse = true;
    } else if (hcls == 3u) {
      analyzed = true;
    } else {
      analyzed = cgba_blend_strip_analyze(&plans[blend_layer], sy0, sy1, &cidx, &xlo, &xhi);
      if (analyzed && xlo >= xhi) {
        cgba_folded_palette_prepare(
          pal_conv[cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
        folded = true;
      } else {
        u32 span_w = analyzed ? (xhi - xlo) : 240u;
        u32 edge_px = analyzed ? (xlo + (240u - xhi)) : 0u;
        if (lower_4bpp_ok && span_w >= 48u &&
            cgba_blend_strip_find_dominant(
              &plans[blend_layer], sy0, sy1, edge_px,
              &sparse_dom_raw, &sparse_dom_cidx)) {
          cgba_folded_palette_prepare(
            pal_conv[sparse_dom_cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
          folded = true;
          sparse_override = true;
          use_sparse = true;
        }
      }
    }
    if (!folded && !use_sparse && analyzed) {
      cgba_folded_palette_prepare(
        pal_conv[cidx], cgba_m0.blend_eva, cgba_m0.blend_evb);
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

  const u16 *cur_pal = folded ? cgba_folded_pal : pal_conv;
  const u16 *cur_base_pal = folded ? cgba_folded_bg_shadow : base_pal;
  const u32 *cur_pair_lut = folded ? fpair_lut : pair_lut;
  const u16 *outer_pal = invert_fold ? pal_conv : cgba_folded_pal;
  const u16 *inner_pal = invert_fold ? cgba_folded_pal : pal_conv;
  const bool bd_t1 = (bright_t1 & 0x20u) != 0u;

  auto fill_strip_backdrop = [&]() {
    if (split_active && bd_t1) {
      u16 bd_o = outer_pal[0];
      u16 bd_i = inner_pal[0];
      for (u32 y = sy0; y < sy1; y++) {
        u16 *row = screen_base + y * 240u;
        if (split_xlo > 0u)
          cgba_fill_span_u16(row, split_xlo, bd_o);
        cgba_fill_span_u16(row + split_xlo, split_xhi - split_xlo, bd_i);
        if (split_xhi < 240u)
          cgba_fill_span_u16(row + split_xhi, 240u - split_xhi, bd_o);
      }
    } else {
      u16 bd = (folded && bd_t1) ? cgba_folded_pal[0] : pal_conv[0];
      for (u32 y = sy0; y < sy1; y++)
        cgba_fill_row_u16(screen_base + y * 240u, bd);
    }
  };

  const u32 vmode = cgba_m0.video_mode;
  s32 strip_ax[2], strip_ay[2];
  if (__builtin_expect(vmode != 0u, 0)) {
    strip_ax[0] = affine_reference_x[0];
    strip_ay[0] = affine_reference_y[0];
    strip_ax[1] = affine_reference_x[1];
    strip_ay[1] = affine_reference_y[1];
  }

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
      const bool obj_t1 = (bright_t1 & 0x10u) != 0u;
      if (__builtin_expect(split_active && obj_t1, 0)) {
        for (u32 y = sy0; y < sy1; y++) {
          if (obj_row_prio_mask[y] & pbit) {
            write_ioreg(REG_VCOUNT, y);
            u16 *row = screen_base + y * 240u;
            if (split_xlo > 0u)
              render_scanline_objs<u16, FULLCOLOR>(
                prio, 0u, split_xlo, row, outer_pal + 0x100);
            render_scanline_objs<u16, FULLCOLOR>(
              prio, split_xlo, split_xhi, row, inner_pal + 0x100);
            if (split_xhi < 240u)
              render_scanline_objs<u16, FULLCOLOR>(
                prio, split_xhi, 240u, row, outer_pal + 0x100);
          }
        }
      } else {
        const u16 *opal_base = (folded && obj_t1) ? cgba_folded_pal : pal_conv;
        const bool check_st = cgba_mode0_obj_alpha_can_blend != 0u;
        bool strip_has_st = false;
        if (check_st) {
          for (u32 y = sy0; y < sy1; y++) {
            if ((obj_row_prio_mask[y] & pbit) && (obj_alpha_prio_mask[y] & pbit)) {
              strip_has_st = true;
              break;
            }
          }
        }
#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
        if (!strip_has_st) {
          cgba_render_obj_batch_fullcolor_strip(
            prio, sy0, sy1, screen_base, NULL, opal_base);
        } else
#endif
        {
          const u16 *opal = opal_base + 0x100;
          for (u32 y = sy0; y < sy1; y++) {
            if (obj_row_prio_mask[y] & pbit) {
              write_ioreg(REG_VCOUNT, y);
              if (__builtin_expect(check_st && (obj_alpha_prio_mask[y] & pbit), 0))
                cgba_render_scanline_objs_direct_alpha(
                  prio, 0, 240, screen_base + y * 240u, opal, y,
                  cgba_m0.obj_alpha_eva, cgba_m0.obj_alpha_evb);
              else
                render_scanline_objs<u16, FULLCOLOR>(
                  prio, 0, 240, screen_base + y * 240u, opal);
            }
          }
        }
      }
      continue;
    }

    const cgba_mode0_bg_plan *p = &plans[layer];
    if (__builtin_expect(layer == blend_layer, 0)) {
      if (p->flags & 8u) {
        if (!base_done) {
          const u16 *bd_pal = cgba_bg_blend_bd_pal_get(cgba_m0.blend_eva, cgba_m0.blend_evb);
          cgba_render_8bpp_bg_base_rows(
            p, sy0, sy1, screen_base + sy0 * 240u, bd_pal);
          base_done = 1;
        } else {
          cgba_render_8bpp_bg_blend_rows(
            p, sy0, sy1, screen_base + sy0 * 240u,
            cgba_m0.blend_eva, cgba_m0.blend_evb);
        }
        continue;
      }
      if (!base_done) {
        fill_strip_backdrop();
        base_done = 1;
      }
      if (folded) {
        if (sparse_override) {
          cgba_render_4bpp_bg_sparse_override(
            p, sy0, sy1, screen_base, lorder, lnum,
            sparse_dom_raw, sparse_dom_cidx,
            cgba_m0.blend_eva, cgba_m0.blend_evb);
        }
        /* Any layer drawn above blend_layer (e.g. OBJ0/BG0 HUD) is NOT blended. */
        folded = false;
        sparse_override = false;
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

    const bool bg_t1 = ((bright_t1 >> layer) & 1u) != 0u;
    if (__builtin_expect(!base_done && bright_eff != 0u && bg_t1 != bd_t1, 0)) {
      fill_strip_backdrop();
      base_done = 1;
    }
    const bool layer_split = split_active && bg_t1;
    const u16 *l_pal = (folded && bg_t1) ? cur_pal : pal_conv;
    const u16 *l_bpal = (folded && bg_t1) ? cur_base_pal : base_pal;
    const u32 *l_plut = (folded && bg_t1) ? cur_pair_lut : pair_lut;

    u8 pflags = p->flags;
    if (__builtin_expect(!(pflags & 1u), 0)) {
      if ((pflags & 8u) && !layer_split) {
        if (base_done)
          cgba_render_8bpp_bg_over_rows(
            p, sy0, sy1, screen_base + sy0 * 240u, l_pal);
        else
          cgba_render_8bpp_bg_base_rows(
            p, sy0, sy1, screen_base + sy0 * 240u, l_pal);
        base_done = 1;
        continue;
      }
      if (pflags & 4u) {
        u32 aidx = layer - 2u;
        affine_reference_x[aidx] = strip_ax[aidx];
        affine_reference_y[aidx] = strip_ay[aidx];
        if (__builtin_expect(layer_split, 0)) {
          for (u32 y = sy0; y < sy1; y++) {
            write_ioreg(REG_VCOUNT, y);
            u16 *row = screen_base + y * 240u;
            if (base_done) {
              if (split_xlo > 0u)
                render_scanline_affine<u16, FULLCOLOR, false>(
                  layer, 0u, split_xlo, row, outer_pal);
              render_scanline_affine<u16, FULLCOLOR, false>(
                layer, split_xlo, split_xhi, row, inner_pal);
              if (split_xhi < 240u)
                render_scanline_affine<u16, FULLCOLOR, false>(
                  layer, split_xhi, 240u, row, outer_pal);
            } else {
              if (split_xlo > 0u)
                render_scanline_affine<u16, FULLCOLOR, true>(
                  layer, 0u, split_xlo, row, outer_pal);
              render_scanline_affine<u16, FULLCOLOR, true>(
                layer, split_xlo, split_xhi, row, inner_pal);
              if (split_xhi < 240u)
                render_scanline_affine<u16, FULLCOLOR, true>(
                  layer, split_xhi, 240u, row, outer_pal);
            }
            cgba_update_affine_ref_single(aidx, y);
          }
        } else {
          for (u32 y = sy0; y < sy1; y++) {
            write_ioreg(REG_VCOUNT, y);
            if (base_done)
              render_scanline_affine<u16, FULLCOLOR, false>(
                layer, 0, 240, screen_base + y * 240u, l_pal);
            else
              render_scanline_affine<u16, FULLCOLOR, true>(
                layer, 0, 240, screen_base + y * 240u, l_pal);
            cgba_update_affine_ref_single(aidx, y);
          }
        }
        base_done = 1;
        continue;
      }
      if (__builtin_expect(layer_split, 0)) {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          u16 *row = screen_base + y * 240u;
          if (base_done) {
            if (split_xlo > 0u)
              render_scanline_text<u16, FULLCOLOR, false>(
                layer, 0u, split_xlo, row, outer_pal);
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, split_xlo, split_xhi, row, inner_pal);
            if (split_xhi < 240u)
              render_scanline_text<u16, FULLCOLOR, false>(
                layer, split_xhi, 240u, row, outer_pal);
          } else {
            if (split_xlo > 0u)
              render_scanline_text<u16, FULLCOLOR, true>(
                layer, 0u, split_xlo, row, outer_pal);
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, split_xlo, split_xhi, row, inner_pal);
            if (split_xhi < 240u)
              render_scanline_text<u16, FULLCOLOR, true>(
                layer, split_xhi, 240u, row, outer_pal);
          }
        }
      } else {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          if (base_done)
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, 0, 240, screen_base + y * 240u, l_pal);
          else
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, 0, 240, screen_base + y * 240u, l_pal);
        }
      }
      base_done = 1;
      continue;
    }

    if (__builtin_expect(layer_split, 0)) {
      cgba_render_4bpp_bg_split_sh4(
        layer, sy0, sy1, split_xlo, split_xhi, screen_base, base_done, invert_fold);
      base_done = 1;
      continue;
    }

    if (!base_done && blend_layer < 4u &&
        cgba_bg_strip_all_empty(p, cgba_m0.row_all_empty[layer], sy0, sy1))
      continue;

    /* Merge 2 or 3 consecutive aligned 4bpp BGs sharing charbase, sub-tile
     * scroll phase, and brightness target-1 membership into a single strip
     * pass per 8-row tile band whenever upper layers are opaque or empty. */
    {
      u8 ml[3];
      ml[0] = (u8)layer;
      u32 mcnt = 1u;
      while (mcnt < 3u && (lnum + mcnt) < lcount) {
        u32 nl = lorder[lnum + mcnt];
        if ((nl & 0x4u) || nl == blend_layer)
          break;
        const cgba_mode0_bg_plan *np = &plans[nl];
        if (!(np->flags & 1u))
          break;
        if ((((bright_t1 >> nl) & 1u) != 0u) != bg_t1)
          break;
        if (np->tile_charbase != p->tile_charbase ||
            np->tile_hoff != p->tile_hoff ||
            ((np->vofs ^ p->vofs) & 7u) != 0u)
          break;
        ml[mcnt++] = (u8)nl;
      }
      if (mcnt >= 2u) {
        cgba_render_4bpp_merged_strip_sh4(
          ml, mcnt, sy0, sy1, screen_base, base_done,
          l_pal, l_bpal, l_plut, l_bpal[0]);
        base_done = 1;
        lnum += mcnt - 1u;
        continue;
      }
    }

    cgba_render_4bpp_text_bg_strip_sh4(
      p, sy0, sy1, screen_base, base_done != 0u,
      l_pal, l_bpal, l_plut, cgba_m0.row_all_empty[layer]);
    base_done = 1;
  }

  if (!base_done)
    fill_strip_backdrop();
  if (__builtin_expect(vmode != 0u, 0)) {
    affine_reference_x[0] = strip_ax[0];
    affine_reference_y[0] = strip_ay[0];
    affine_reference_x[1] = strip_ax[1];
    affine_reference_y[1] = strip_ay[1];
  }
}

/* Phase 3D: Multi-span windowing (WIN0/WIN1 without OBJWIN) tile-major 4bpp
 * strip renderer for arbitrary horizontal [sx0, sx1) window spans. */
struct cgba_win_hspan {
  u16 x0;
  u16 x1;
  u8  en;
};

static u32 cgba_build_win_hspans(u32 win_ctrl, u32 y, cgba_win_hspan out[5])
{
  u16 w0h = read_ioreg(REG_WIN0H), w0v = read_ioreg(REG_WIN0V);
  u16 w1h = read_ioreg(REG_WIN1H), w1v = read_ioreg(REG_WIN1V);
  u16 winin = read_ioreg(REG_WININ);
  u8 en_out = (u8)(read_ioreg(REG_WINOUT) & 0x3Fu);
  u8 en0 = (u8)(winin & 0x3Fu);
  u8 en1 = (u8)((winin >> 8) & 0x3Fu);

  u32 w0_lr = w0h >> 8, w0_rr = w0h & 0xFFu;
  u32 w1_lr = w1h >> 8, w1_rr = w1h & 0xFFu;
  bool in0_y = ((win_ctrl & 1u) != 0u) && (w0_lr != w0_rr) &&
               in_window_y(y, w0v >> 8, w0v & 0xFFu);
  bool in1_y = ((win_ctrl & 2u) != 0u) && (w1_lr != w1_rr) &&
               in_window_y(y, w1v >> 8, w1v & 0xFFu);
  u32 w0l = MIN(240u, w0_lr), w0r = MIN(240u, w0_rr);
  u32 w1l = MIN(240u, w1_lr), w1r = MIN(240u, w1_rr);
  bool good0 = (w0_lr < w0_rr);
  bool good1 = (w1_lr < w1_rr);

  u16 bp[6];
  u32 nbp = 2u;
  bp[0] = 0u;
  bp[1] = 240u;
  if (in0_y) { bp[nbp++] = (u16)w0l; bp[nbp++] = (u16)w0r; }
  if (in1_y) { bp[nbp++] = (u16)w1l; bp[nbp++] = (u16)w1r; }
  for (u32 i = 1u; i < nbp; i++) {
    u16 v = bp[i];
    u32 j = i;
    while (j > 0u && bp[j - 1u] > v) {
      bp[j] = bp[j - 1u];
      j--;
    }
    bp[j] = v;
  }

  u32 nspan = 0u;
  for (u32 i = 0u; i + 1u < nbp; i++) {
    u32 a = bp[i], b = bp[i + 1u];
    if (a >= b)
      continue;
    u8 en = en_out;
    if (in0_y && (good0 ? (a >= w0l && a < w0r) : (a < w0r || a >= w0l)))
      en = en0;
    else if (in1_y && (good1 ? (a >= w1l && a < w1r) : (a < w1r || a >= w1l)))
      en = en1;
    if (nspan > 0u && out[nspan - 1u].en == en) {
      out[nspan - 1u].x1 = (u16)b;
    } else {
      out[nspan].x0 = (u16)a;
      out[nspan].x1 = (u16)b;
      out[nspan].en = en;
      nspan++;
    }
  }
  return nspan;
}

static __attribute__((noinline)) void cgba_render_4bpp_bg_hspan_strip_sh4(
  u32 layer, u32 sy0, u32 sy1, u32 x0, u32 x1, u16 *screen_base, u32 base_done,
  const u16 *pal_conv, const u16 *base_pal, const u32 *pair_lut)
{
  const cgba_mode0_bg_plan *const p = &cgba_m0.plans[layer];
  cgba_bg_hspan_plan sp;
  cgba_build_bg_hspan_plan(cgba_m0.bg_hofs[layer], cgba_m0.bg_wide[layer],
                           x0, x1, &sp);

  const u8 *const charbase = p->tile_charbase;
  const u32 *const row_empty = cgba_m0.row_all_empty[layer];
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + p->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    if (base_done) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if ((row_empty[vhalf] & (1u << vtile_row)) ||
          cgba_4bpp_strip_row_zero_skip(p, vhalf, voffset, start_r, k)) {
        y += k;
        continue;
      }
    }
    u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    u16 *dst_top = screen_base + y * 240u;
    u32 rk = (start_r << 8) | k;
    cgba_render_4bpp_hspan_seg_sh4(
      &sp, map_row_b, dst_top, charbase, start_r, k, rk,
      base_done != 0u, pal_conv, base_pal, pair_lut);
    y += k;
  }
}

static __attribute__((noinline)) void cgba_render_win_multi_strip_sh4(
  u32 video_mode, u16 dispcnt, u32 sy0, u32 sy1, u16 *screen_base)
{
  cgba_win_hspan spans[5];
  u32 nspans = cgba_build_win_hspans(dispcnt >> 13, sy0, spans);
  const u32 mode_layers = active_layers[video_mode];
  const u32 disp_layers = (dispcnt >> 8) & mode_layers;
  const u16 bldcnt = read_ioreg(REG_BLDCNT);
  const u32 bld_eff = (bldcnt >> 6) & 3u;
  const u32 bld_evy = MIN(16u, (u32)(read_ioreg(REG_BLDY) & 0x1Fu));
  const bool bld_is_bright =
    (bld_eff == COL_EFFECT_BRIGHT || bld_eff == COL_EFFECT_DARK) &&
    bld_evy > 0u;
  const u32 bright_t1 = bldcnt & 0x3Fu;
  bool bright_prepared = false;
  const u16 *const pal_conv = palette_ram_converted;
  const u16 *const base_pal = backdrop_shadow_palette;
  const u32 *const pair_lut = (const u32 *)&pair_palette[0][0][0];
  const u32 *const fpair_lut = (const u32 *)&cgba_folded_pair_lut[0][0][0];
  const cgba_mode0_bg_plan *const plans = cgba_m0.plans;
  s32 strip_ax[2], strip_ay[2];
  if (__builtin_expect(video_mode != 0u, 0)) {
    strip_ax[0] = affine_reference_x[0];
    strip_ay[0] = affine_reference_y[0];
    strip_ax[1] = affine_reference_x[1];
    strip_ay[1] = affine_reference_y[1];
  }

  for (u32 s = 0u; s < nspans; s++) {
    u32 sx0 = spans[s].x0;
    u32 sx1 = spans[s].x1;
    u32 en = spans[s].en;
    u32 span_vis = disp_layers & (en & 0x1Fu);
    bool span_no_eff = (span_vis != 0u) &&
                       (!(en & 0x20u) || cgba_bldcnt_is_noop(bldcnt, span_vis));
    bool span_bright = !span_no_eff && (span_vis != 0u) &&
                       ((en & 0x20u) != 0u) && bld_is_bright;
    u32 span_omask = 0u;
    bool span_alpha_ok = true;
    u8 span_alpha_pmask = 0u;
    u32 span_eva = 0u, span_evb = 0u;
    bool span_alpha_can = (span_vis & 0x10u) != 0u &&
                          (((bldcnt >> 8) & (span_vis | 0x20u)) != 0u);
    if ((span_no_eff || span_bright) && (span_vis & 0x10u)) {
      if (span_no_eff && span_alpha_can) {
        u32 tgt2_raw = (bldcnt >> 8) & 0x3Fu;
        if (tgt2_raw & 0x20u) {
          for (u32 p = 0; p < 4u; p++) {
            u32 below = 0x20u;
            for (u32 b = 0; b < 4u; b++) {
              if (((span_vis >> b) & 1u) && cgba_m0.bg_prio[b] >= p)
                below |= (1u << b);
            }
            if ((tgt2_raw & below) == below)
              span_alpha_pmask |= (u8)(1u << p);
          }
          if (span_alpha_pmask) {
            u32 bldalpha = read_ioreg(REG_BLDALPHA);
            span_eva = MIN(16u, (u32)(bldalpha & 0x1Fu));
            span_evb = MIN(16u, (u32)((bldalpha >> 8) & 0x1Fu));
          }
        }
      }
      for (u32 y = sy0; y < sy1; y++) {
        span_omask |= (u32)(obj_row_prio_mask[y] & 0x0Fu);
        if (span_alpha_can && obj_alpha_count[y]) {
          if (span_bright || !span_alpha_pmask || obj_alpha_complex[y] ||
              (obj_alpha_prio_mask[y] & ~span_alpha_pmask) != 0u) {
            span_alpha_ok = false;
            break;
          }
        }
      }
    }

    if ((!span_no_eff && !span_bright) || !span_alpha_ok) {
      if (video_mode != 0u) {
        affine_reference_x[0] = strip_ax[0];
        affine_reference_y[0] = strip_ay[0];
        affine_reference_x[1] = strip_ax[1];
        affine_reference_y[1] = strip_ay[1];
      }
      for (u32 y = sy0; y < sy1; y++) {
        write_ioreg(REG_VCOUNT, y);
        order_layers(disp_layers, y);
        render_scanline_conditional(sx0, sx1, screen_base + y * 240u, en);
        if (video_mode != 0u)
          cgba_update_affine_refs(y);
      }
      continue;
    }

    if (span_bright && !bright_prepared) {
      cgba_folded_brightness_prepare(bld_eff, bld_evy);
      bright_prepared = true;
    }
    const bool bd_t1 = span_bright && ((bright_t1 & 0x20u) != 0u);
    const u16 bd_col = bd_t1 ? cgba_folded_pal[0] : pal_conv[0];

    u8 slorder[8];
    u32 slcount = 0u;
    for (s32 prio = 3; prio >= 0; prio--) {
      for (s32 lnum = 3; lnum >= 0; lnum--) {
        if (((span_vis >> lnum) & 1u) && (cgba_m0.bg_prio[lnum] == (u32)prio))
          slorder[slcount++] = (u8)lnum;
      }
      if (span_omask & (1u << prio))
        slorder[slcount++] = (u8)((u32)prio | 0x04u);
    }

    u32 base_done = 0u;
    for (u32 i = 0u; i < slcount; i++) {
      u32 layer = slorder[i];
      if (layer & 0x4u) {
        if (!base_done) {
          for (u32 y = sy0; y < sy1; y++)
            cgba_fill_span_u16(screen_base + y * 240u + sx0, sx1 - sx0, bd_col);
          base_done = 1u;
        }
        const u16 *opal = (span_bright && (bright_t1 & 0x10u))
                            ? (cgba_folded_pal + 0x100)
                            : (pal_conv + 0x100);
        u32 prio = layer & 3u;
        u32 pbit = 1u << prio;
        for (u32 y = sy0; y < sy1; y++) {
          if (obj_row_prio_mask[y] & pbit) {
            write_ioreg(REG_VCOUNT, y);
            if (__builtin_expect(span_alpha_can && (obj_alpha_prio_mask[y] & pbit), 0))
              cgba_render_scanline_objs_direct_alpha(
                prio, sx0, sx1, screen_base + y * 240u, opal,
                y, span_eva, span_evb);
            else
              render_scanline_objs<u16, FULLCOLOR>(
                prio, sx0, sx1, screen_base + y * 240u, opal);
          }
        }
        continue;
      }
      const bool bg_t1 = span_bright && (((bright_t1 >> layer) & 1u) != 0u);
      if (__builtin_expect(!base_done && span_bright && bg_t1 != bd_t1, 0)) {
        for (u32 y = sy0; y < sy1; y++)
          cgba_fill_span_u16(screen_base + y * 240u + sx0, sx1 - sx0, bd_col);
        base_done = 1u;
      }
      const u16 *l_pal = bg_t1 ? cgba_folded_pal : pal_conv;
      const u16 *l_bpal = bg_t1 ? cgba_folded_bg_shadow : base_pal;
      const u32 *l_plut = bg_t1 ? fpair_lut : pair_lut;
      const cgba_mode0_bg_plan *p = &plans[layer];
      if (p->flags & 1u) {
        cgba_render_4bpp_bg_hspan_strip_sh4(
          layer, sy0, sy1, sx0, sx1, screen_base, base_done,
          l_pal, l_bpal, l_plut);
        base_done = 1u;
      } else if (p->flags & 4u) {
        u32 aidx = layer - 2u;
        affine_reference_x[aidx] = strip_ax[aidx];
        affine_reference_y[aidx] = strip_ay[aidx];
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          if (base_done)
            render_scanline_affine<u16, FULLCOLOR, false>(
              layer, sx0, sx1, screen_base + y * 240u, l_pal);
          else
            render_scanline_affine<u16, FULLCOLOR, true>(
              layer, sx0, sx1, screen_base + y * 240u, l_pal);
          cgba_update_affine_ref_single(aidx, y);
        }
        base_done = 1u;
      } else {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          if (base_done)
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, sx0, sx1, screen_base + y * 240u, l_pal);
          else
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, sx0, sx1, screen_base + y * 240u, l_pal);
        }
        base_done = 1u;
      }
    }
    if (!base_done) {
      for (u32 y = sy0; y < sy1; y++)
        cgba_fill_span_u16(screen_base + y * 240u + sx0, sx1 - sx0, bd_col);
    }
  }

  if (__builtin_expect(video_mode != 0u, 0)) {
    affine_reference_x[0] = strip_ax[0];
    affine_reference_y[0] = strip_ay[0];
    affine_reference_x[1] = strip_ax[1];
    affine_reference_y[1] = strip_ay[1];
  }
}
#endif

static inline void cgba_build_mode1_text_plan(u32 layer, cgba_mode0_bg_plan *p)
{
  const u32 mos_bg = read_ioreg(REG_MOSAIC) & 0xFFu;
  const u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  const bool is8bpp = (bg_control & 0x80u) != 0u;
  const bool has_mosaic = ((bg_control & 0x40u) != 0u) && (mos_bg != 0u);
  if (is8bpp || has_mosaic) {
    p->flags = 0u;
    return;
  }
  const u32 map_size = (bg_control >> 14) & 3u;
  const u32 base_block = (bg_control >> 8) & 0x1Fu;
  const u32 sb_bot = base_block + ((map_size & 2u) ? ((map_size & 1u) ? 2u : 1u) : 0u);
  const u32 tilecntrl = (bg_control >> 2) & 3u;
  cgba_bg_hspan_plan sp;
  cgba_build_bg_hspan_plan(read_ioreg(REG_BGxHOFS(layer)) & 511u,
                           map_size & 1u, 0u, 240u, &sp);
  p->tile_charbase = &vram[tilecntrl * 16384u];
  p->sb_ptr[0] = (u16 *)&vram[base_block * 2048u];
  p->sb_ptr[1] = (u16 *)&vram[sb_bot * 2048u];
  p->counts_packed = sp.cp;
  p->vofs = read_ioreg(REG_BGxVOFS(layer));
  p->first_byte_off = sp.first_byte_off;
  p->map2_byte_off = sp.map2_byte_off;
  p->tile_hoff = sp.thoff;
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  u32 nz0 = cgba_sb_get_row_nonzero(base_block);
  if (map_size & 1u)
    nz0 |= cgba_sb_get_row_nonzero(base_block + 1u);
  u32 nz1 = nz0;
  if (sb_bot != base_block) {
    nz1 = cgba_sb_get_row_nonzero(sb_bot);
    if (map_size & 1u)
      nz1 |= cgba_sb_get_row_nonzero(sb_bot + 1u);
  }
  p->row_nonzero[0] = nz0;
  p->row_nonzero[1] = nz1;
  p->flags = (u8)(1u | (((nz0 & nz1) != 0xFFFFFFFFu) ? 2u : 0u));
#else
  p->row_nonzero[0] = 0xFFFFFFFFu;
  p->row_nonzero[1] = 0xFFFFFFFFu;
  p->flags = 1u;
#endif
}

static inline bool cgba_tile_rows_all_zero(
  u16 raw_map, const u8 *tile_charbase, u32 start_r, u32 k)
{
  u32 me = eswap16(raw_map);
  const u8 *row = tile_charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
  s32 step = 4;
  if (me & 0x800u) {
    row += 28 - (s32)(start_r << 3);
    step = -4;
  }
  u32 any = 0u;
  for (u32 r = 0u; r < k; r++, row += step)
    any |= *(const u32 *)row;
  return any == 0u;
}

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
/* Layout per slot (640 u16) in cgba_m1_fpal_data (shared with Mode 0
 * cgba_folded_pair_lut in cgba_folded_highbss): [0..127] = mirror of
 * bg[128..255] for signed-byte SH-4 affine indexing, [128..383] = bg[0..255],
 * [384..639] = obj[0..255]. Returned pointers point to &cgba_m1_fpal_data[s][128]. */
static u16 cgba_m1_pal_snap[512] __attribute__((aligned(32)));
static u32 cgba_m1_fpal_key[16];
static u32 cgba_m1_fpal_dirty[16]; /* bit s (0..31): 16-entry subpalette s needs rebuild */
static u8  cgba_m1_fpal_has_obj[16];
static u32 cgba_m1_fpal_epoch = 0u;
static u32 cgba_m1_fpal_count = 0u;
static u32 cgba_m1_fpal_rr = 0u;
static bool cgba_m1_pal_snap_valid = false;

static inline void cgba_m1_fpal_rebuild_sub(
  u16 *out, u32 sub, u32 ta, u32 evb, bool sat)
{
  const u16 *src = &palette_ram_converted[sub << 4];
  u16 *dst = &out[sub << 4];
  if (evb == 16u) {
    const u32 ta_s4 = (ta >> 4) & BLND_MSK;
    for (u32 c = 0u; c < 16u; c++) {
      u32 p2 = src[c];
      u32 pfe = ta_s4 + (((p2 | (p2 << 16)) & BLND_MSK));
      if (__builtin_expect((pfe & (OVFR_MSK | OVFG_MSK | OVFB_MSK)) != 0u, 0)) {
        if (pfe & OVFG_MSK) pfe |= SATG_MSK;
        if (pfe & OVFR_MSK) pfe |= SATR_MSK;
        if (pfe & OVFB_MSK) pfe |= SATB_MSK;
      }
      pfe &= BLND_MSK;
      dst[c] = (u16)(pfe | (pfe >> 16));
    }
  } else if (sat) {
    for (u32 c = 0u; c < 16u; c++)
      dst[c] = cgba_blend_rgb565_direct<true>(ta, src[c], evb);
  } else {
    for (u32 c = 0u; c < 16u; c++)
      dst[c] = cgba_blend_rgb565_direct<false>(ta, src[c], evb);
  }
  if (sub >= 8u && sub < 16u)
    memcpy(dst - 256, dst, 16u * sizeof(u16));
}

static u32 cgba_m1_fpal_ver = 1u;

static __attribute__((noinline)) void cgba_m1_fpal_sync_pal(u32 epoch)
{
  cgba_m1_fpal_epoch = epoch;
  if (!cgba_m1_pal_snap_valid) {
    memcpy(cgba_m1_pal_snap, palette_ram_converted, sizeof(cgba_m1_pal_snap));
    cgba_m1_pal_snap_valid = true;
    cgba_m1_fpal_count = 0u;
    cgba_m1_fpal_rr = 0u;
    return;
  }
  u32 dirty_mask = 0u;
  for (u32 sub = 0u; sub < 32u; sub++) {
    u32 base = sub << 4;
    if (!cgba_memeq_u32(&cgba_m1_pal_snap[base], &palette_ram_converted[base], 8u)) {
      memcpy(&cgba_m1_pal_snap[base], &palette_ram_converted[base],
             16u * sizeof(u16));
      dirty_mask |= (1u << sub);
    }
  }
  if (dirty_mask != 0u) {
    const u32 cnt = cgba_m1_fpal_count;
    for (u32 s = 0u; s < cnt; s++)
      cgba_m1_fpal_dirty[s] |= dirty_mask;
  }
}

static __attribute__((noinline)) const u16 *cgba_m1_folded_pal_get(
  u16 p1, u32 eva, u32 evb, bool need_obj)
{
  if (cgba_folded_union_owner != 2u) {
    cgba_m1_fpal_count = 0u;
    cgba_m1_fpal_rr = 0u;
    cgba_m1_fpal_ver++;
    cgba_folded_union_owner = 2u;
  }
  (void)cgba_bg_blend_term_a_get(eva);
  const u32 epoch = cgba_term_a_epoch;
  if (__builtin_expect(cgba_m1_fpal_epoch != epoch, 0))
    cgba_m1_fpal_sync_pal(epoch);
  const u32 key = ((u32)p1 << 16) | (eva << 8) | evb;
  const u32 cnt = cgba_m1_fpal_count;
  const u32 want_mask = need_obj ? 0xFFFFFFFFu : 0x0000FFFFu;
  for (u32 s = 0u; s < cnt; s++) {
    if (cgba_m1_fpal_key[s] == key) {
      u32 todo = cgba_m1_fpal_dirty[s] & want_mask;
      if (need_obj && !cgba_m1_fpal_has_obj[s]) {
        todo |= 0xFFFF0000u;
        cgba_m1_fpal_has_obj[s] = 1u;
      }
      u16 *out = cgba_m1_fpal_data[s] + 128;
      if (__builtin_expect(todo != 0u, 0)) {
        u32 ta = (((u32)p1 | ((u32)p1 << 16)) & BLND_MSK) * eva;
        bool sat = (eva + evb) > 16u;
        u32 m = todo;
        while (m) {
          u32 sub = (u32)__builtin_ctz(m);
          cgba_m1_fpal_rebuild_sub(out, sub, ta, evb, sat);
          m &= m - 1u;
        }
        cgba_m1_fpal_dirty[s] &= ~todo;
        if (todo & 0x0000FFFFu)
          cgba_m1_fpal_ver++;
      }
      return out;
    }
  }
  u32 idx = (cnt < 16u) ? cgba_m1_fpal_count++ : (cgba_m1_fpal_rr++ & 15u);
  u16 *out = cgba_m1_fpal_data[idx] + 128;
  u32 ta = (((u32)p1 | ((u32)p1 << 16)) & BLND_MSK) * eva;
  bool sat = (eva + evb) > 16u;
  u32 nsub = need_obj ? 32u : 16u;
  for (u32 sub = 0u; sub < nsub; sub++)
    cgba_m1_fpal_rebuild_sub(out, sub, ta, evb, sat);
  cgba_m1_fpal_key[idx] = key;
  cgba_m1_fpal_has_obj[idx] = need_obj ? 1u : 0u;
  cgba_m1_fpal_dirty[idx] = need_obj ? 0u : 0xFFFF0000u;
  cgba_m1_fpal_ver++;
  return out;
}

/* Classify k scanlines [y0, y0 + k) within a single 8-line tile row of a 4bpp
 * text BG.  When all 30..31 map entries across the tile row are identical
 * (common for uniform overlays / horizon gradients), checks the map ONCE per
 * 8-line tile row and then inspects only 1 u32 word per scanline.
 * Outputs per-row: 0 = empty, 1 = uniform solid (out_cidx[r]), 2 = mixed. */
static __attribute__((noinline)) void cgba_blend_tilerow_classify(
  const cgba_mode0_bg_plan *p, u32 y0, u32 k, u8 *out_kind, u8 *out_cidx)
{
  const u32 voffset0 = (y0 + p->vofs) & 511u;
  const u32 start_r = voffset0 & 7u;
  const u32 vhalf = voffset0 >> 8;
  if ((p->flags & 2u) != 0u) {
    u32 vtile_row = (voffset0 & 255u) >> 3;
    if (!(p->row_nonzero[vhalf] & (1u << vtile_row))) {
      const u32 *t0 = (const u32 *)(p->tile_charbase + (start_r << 2));
      u32 any = 0u;
      for (u32 r = 0u; r < k; r++)
        any |= t0[r];
      if (any == 0u) {
        for (u32 r = 0u; r < k; r++)
          out_kind[r] = 0u;
        return;
      }
    }
  }
  const u32 thoff = p->tile_hoff;
  const u32 cp = p->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  u8 *map_row_b = (u8 *)p->sb_ptr[vhalf] + ((voffset0 & 0xF8u) << 3);
  const u16 *map1 = (const u16 *)(map_row_b + p->first_byte_off);
  const u16 *map2 = (const u16 *)(map_row_b + p->map2_byte_off);
  const u32 n1 = thoff ? (td1 + 1u) : td1;
  const u32 n2 = thoff ? (td2 + 1u) : td2;

  /* Fast check: are all visible map entries in this tile row identical? */
  const u16 rm0 = map1[0];
  bool all_same_rm = true;
  for (u32 i = 1u; i < n1; i++) {
    if (map1[i] != rm0) {
      all_same_rm = false;
      break;
    }
  }
  if (all_same_rm) {
    for (u32 i = 0u; i < n2; i++) {
      if (map2[i] != rm0) {
        all_same_rm = false;
        break;
      }
    }
  }

  if (all_same_rm) {
    u32 me = eswap16(rm0);
    const u8 *trow = p->tile_charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
    s32 step = 4;
    if (me & 0x800u) {
      trow += 28 - (s32)(start_r << 3);
      step = -4;
    }
    const u32 pal_hi = (me >> 8) & 0xF0u;
    for (u32 r = 0u; r < k; r++, trow += step) {
      u32 w = *(const u32 *)trow;
      if (w == 0u) {
        out_kind[r] = 0u;
      } else if ((((w << 4) ^ w) >> 4) == 0u) {
        out_kind[r] = 1u;
        out_cidx[r] = (u8)(pal_hi | (w & 0x0Fu));
      } else {
        out_kind[r] = 2u;
      }
    }
    return;
  }

  /* General case: collect distinct raw_map entries once for this tile row (up to 4). */
  u16 uniq[4];
  u32 nuniq = 0u;
  auto add_uniq = [&](u16 rm) -> bool {
    for (u32 u = 0u; u < nuniq; u++) {
      if (uniq[u] == rm)
        return true;
    }
    if (nuniq >= 4u)
      return false;
    uniq[nuniq++] = rm;
    return true;
  };
  bool few_uniq = true;
  for (u32 i = 0u; i < n1; i++) {
    if (!add_uniq(map1[i])) {
      few_uniq = false;
      break;
    }
  }
  if (few_uniq) {
    for (u32 i = 0u; i < n2; i++) {
      if (!add_uniq(map2[i])) {
        few_uniq = false;
        break;
      }
    }
  }

  if (few_uniq) {
    const u8 *trows[4];
    s32 steps[4];
    u32 pals[4];
    for (u32 u = 0u; u < nuniq; u++) {
      u32 me = eswap16(uniq[u]);
      const u8 *tr = p->tile_charbase + ((me & 0x3FFu) << 5) + (start_r << 2);
      s32 st = 4;
      if (me & 0x800u) {
        tr += 28 - (s32)(start_r << 3);
        st = -4;
      }
      trows[u] = tr;
      steps[u] = st;
      pals[u] = (me >> 8) & 0xF0u;
    }
    for (u32 r = 0u; r < k; r++) {
      u32 w0 = *(const u32 *)trows[0];
      trows[0] += steps[0];
      bool want_empty = (w0 == 0u);
      bool ok = true;
      u32 exp_cidx = 0u;
      if (!want_empty) {
        if ((((w0 << 4) ^ w0) >> 4) != 0u)
          ok = false;
        else
          exp_cidx = pals[0] | (w0 & 0x0Fu);
      }
      for (u32 u = 1u; u < nuniq; u++) {
        u32 w = *(const u32 *)trows[u];
        trows[u] += steps[u];
        if (!ok)
          continue;
        if (want_empty) {
          if (w != 0u)
            ok = false;
        } else {
          if (w == 0u || (((w << 4) ^ w) >> 4) != 0u ||
              (pals[u] | (w & 0x0Fu)) != exp_cidx)
            ok = false;
        }
      }
      if (!ok) {
        out_kind[r] = 2u;
      } else if (want_empty) {
        out_kind[r] = 0u;
      } else {
        out_kind[r] = 1u;
        out_cidx[r] = (u8)exp_cidx;
      }
    }
    return;
  }

  for (u32 r = 0u; r < k; r++)
    out_kind[r] = 2u;
}
#endif

static __attribute__((noinline)) void cgba_mode1_bg2_fallback(
  bool isbase, bool bg2_wrap, s32 dy, u32 bg2_map_size,
  const u8 *bg2_map_base, const u8 *bg2_tile_base,
  u16 *dst_row, const u16 *rpal)
{
  if (isbase) {
    if (bg2_wrap) {
      if (dy)
        render_affine_background<u16, FULLCOLOR, true, true, true, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
      else
        render_affine_background<u16, FULLCOLOR, true, true, false, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
    } else {
      if (dy)
        render_affine_background<u16, FULLCOLOR, true, false, true, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
      else
        render_affine_background<u16, FULLCOLOR, true, false, false, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
    }
  } else {
    if (bg2_wrap) {
      if (dy)
        render_affine_background<u16, FULLCOLOR, false, true, true, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
      else
        render_affine_background<u16, FULLCOLOR, false, true, false, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
    } else {
      if (dy)
        render_affine_background<u16, FULLCOLOR, false, false, true, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
      else
        render_affine_background<u16, FULLCOLOR, false, false, false, false>(
          2, 0, 240, bg2_map_base, bg2_map_size, bg2_tile_base, dst_row, rpal);
    }
  }
}

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
static __attribute__((noinline)) void cgba_mode1_blend_evb16_strip(
  const cgba_mode0_bg_plan *plan, u32 sy0, u32 sy1, u16 *screen_base,
  const u32 *blend_term_a_s4, const u32 *blend_term_a)
{
  const u32 thoff = plan->tile_hoff;
  const u32 cp = plan->counts_packed;
  const u32 td1 = cp & 0xFFFFu;
  const u32 td2 = cp >> 16;
  const u8 pflags = plan->flags;
  u32 y = sy0;
  while (y < sy1) {
    u32 voffset = (y + plan->vofs) & 511u;
    u32 start_r = voffset & 7u;
    u32 k = 8u - start_r;
    if (y + k > sy1)
      k = sy1 - y;
    u32 vhalf = voffset >> 8;
    const u32 *t0 = (const u32 *)(plan->tile_charbase + (start_r << 2));
    u32 t0_any = 0u;
    for (u32 r = 0u; r < k; r++)
      t0_any |= t0[r];
    if ((pflags & 2u) != 0u && t0_any == 0u) {
      u32 vtile_row = (voffset & 255u) >> 3;
      if (!(plan->row_nonzero[vhalf] & (1u << vtile_row))) {
        y += k;
        continue;
      }
    }
    u8 *map_row_b = (u8 *)plan->sb_ptr[vhalf] + ((voffset & 0xF8u) << 3);
    const u16 *map1 = (const u16 *)(map_row_b + plan->first_byte_off);
    const u16 *map2 = (const u16 *)(map_row_b + plan->map2_byte_off);
    u16 *dst_top = screen_base + y * 240u;
    u32 empty_rm = (t0_any == 0u) ? 0u : 0xFFFFFFFFu;

    auto render_evb16_span = [&](const u16 *mptr, u32 cnt, u16 *dst_x) {
      if (cnt == 0u)
        return;
      u32 lo = 0u;
      while (lo < cnt) {
        u16 rm = mptr[lo];
        if ((u32)rm == empty_rm) {
          lo++;
          continue;
        }
        if (cgba_tile_rows_all_zero(rm, plan->tile_charbase, start_r, k)) {
          empty_rm = (u32)rm;
          lo++;
          continue;
        }
        break;
      }
      if (lo >= cnt)
        return;
      u32 hi = cnt - 1u;
      while (hi > lo) {
        u16 rm = mptr[hi];
        if ((u32)rm == empty_rm) {
          hi--;
          continue;
        }
        if (cgba_tile_rows_all_zero(rm, plan->tile_charbase, start_r, k)) {
          empty_rm = (u32)rm;
          hi--;
          continue;
        }
        break;
      }
      const u32 nz_cnt = hi - lo + 1u;
      const u16 *nz_map = mptr + lo;
      u16 *dst_row = dst_x + lo * 8u;
      for (u32 r = 0u; r < k; r++, dst_row += 240u) {
        u32 vp = (start_r + r) << 2;
        const u8 *tbase = plan->tile_charbase + vp;
        s32 vflip_off = 28 - (s32)(vp << 1);
        cgba_render_4bpp_span_blend_evb16_sh4(
          nz_map, nz_cnt, dst_row, tbase, vflip_off, blend_term_a_s4);
      }
    };

    if (thoff == 0u) {
      render_evb16_span(map1, td1, dst_top);
      render_evb16_span(map2, td2, dst_top + td1 * 8u);
    } else {
      u32 left_w = 8u - thoff;
      u16 left_raw = *map1++;
      u16 right_raw = *(map2 + td2);
      if ((u32)left_raw != empty_rm &&
          !cgba_tile_rows_all_zero(left_raw, plan->tile_charbase, start_r, k)) {
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top, k, thoff, 8u, left_raw, plan->tile_charbase,
          start_r, blend_term_a, 16u);
      }
      render_evb16_span(map1, td1, dst_top + left_w);
      render_evb16_span(map2, td2, dst_top + left_w + td1 * 8u);
      if ((u32)right_raw != empty_rm &&
          !cgba_tile_rows_all_zero(right_raw, plan->tile_charbase, start_r, k)) {
        cgba_render_4bpp_part_blend_strip<true>(
          dst_top + left_w + 232u, k, 0u, thoff, right_raw, plan->tile_charbase,
          start_r, blend_term_a, 16u);
      }
    }
    y += k;
  }
}
#endif

static __attribute__((noinline)) bool cgba_try_render_mode1_batch(
  u32 y0, u32 y_end, u16 *screen_base, u16 dispcnt, bool has_bg2_snaps)
{
  if ((dispcnt & 0xE000u) != 0u || !cgba_bg2_affine_mode_ok(dispcnt) ||
      y_end > 160u || (y_end - y0) < 2u)
    return false;

  const u32 vis_layers = (dispcnt >> 8) & active_layers[dispcnt & 0x07u];
  const bool obj_en = (vis_layers & 0x10u) != 0u;
  u32 strip_omask = 0u;
  u32 alpha_or = 0u;
  if (obj_en) {
    const u8 *pm = obj_row_prio_mask + y0;
    const u8 *ac = obj_alpha_count + y0;
    u32 n = y_end - y0;
    for (u32 r = 0u; r < n; r++) {
      strip_omask |= pm[r];
      alpha_or |= ac[r];
    }
    strip_omask &= 0x0Fu;
  }

  const u16 bldcnt = read_ioreg(REG_BLDCNT);
  if (obj_en && alpha_or != 0u && (((bldcnt >> 8) & (vis_layers | 0x20u)) != 0u))
    return false;

  const u32 vis_eff = vis_layers & (strip_omask ? 0x1Fu : 0x0Fu);
  const u32 mos_bg = read_ioreg(REG_MOSAIC) & 0xFFu;
  if ((vis_eff & 0x04u) != 0u &&
      ((read_ioreg(REG_BG2CNT) & 0x40u) != 0u) && (mos_bg != 0u))
    return false;

  u32 bg_prio[3];
  bg_prio[0] = read_ioreg(REG_BG0CNT) & 3u;
  bg_prio[1] = read_ioreg(REG_BG1CNT) & 3u;
  bg_prio[2] = read_ioreg(REG_BG2CNT) & 3u;

  u32 blend_bg = 0xFFu;
  u32 blend_eva = 0u;
  u32 blend_evb = 0u;
  if (!cgba_bldcnt_is_noop(bldcnt, vis_eff)) {
    if (((bldcnt >> 6) & 3u) != COL_EFFECT_BLEND)
      return false;
    u32 tgt1 = bldcnt & vis_eff;
    if (tgt1 == 0u || (tgt1 & (tgt1 - 1u)) != 0u || (tgt1 & 0x03u) == 0u)
      return false;
    u32 cand = (u32)__builtin_ctz(tgt1);
    u32 cand_cnt = read_ioreg(REG_BGxCNT(cand));
    if ((cand_cnt & 0x80u) != 0u || ((cand_cnt & 0x40u) != 0u && mos_bg != 0u))
      return false;
    u32 tgt2_raw = (bldcnt >> 8) & 0x3Fu;
    if (!(tgt2_raw & 0x20u))
      return false;
    u32 cprio = bg_prio[cand];
    for (u32 b = 0u; b < 3u; b++) {
      if (b == cand || !((vis_eff >> b) & 1u))
        continue;
      if (bg_prio[b] > cprio || (bg_prio[b] == cprio && b > cand)) {
        if (!((tgt2_raw >> b) & 1u))
          return false;
      }
    }
    u32 lower_omask = (strip_omask >> (cprio + 1u));
    if (lower_omask != 0u && !(tgt2_raw & 0x10u))
      return false;
    u32 bldalpha = read_ioreg(REG_BLDALPHA);
    blend_bg = cand;
    blend_eva = MIN(16u, (u32)(bldalpha & 0x1Fu));
    blend_evb = MIN(16u, (u32)((bldalpha >> 8) & 0x1Fu));
  }

  u8 lorder[8];
  u32 lcount = 0u;
  for (s32 prio = 3; prio >= 0; prio--) {
    for (s32 b = 2; b >= 0; b--) {
      if (((vis_eff >> b) & 1u) && bg_prio[b] == (u32)prio)
        lorder[lcount++] = (u8)b;
    }
    if (strip_omask & (1u << prio))
      lorder[lcount++] = (u8)((u32)prio | 0x04u);
  }

  const u16 *const pal_conv = palette_ram_converted;
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
  const u16 *const base_pal = get_backdrop_shadow_palette();
  const u32 *const pair_lut = pair_palette_get_all_u32();
#endif

  auto fill_rows_backdrop = [&](u32 sy0, u32 sy1, u16 bgcol) {
    u32 pair = ((u32)bgcol << 16) | (u32)bgcol;
    u32 *d32 = (u32 *)(screen_base + sy0 * 240u);
    u32 n32 = (sy1 - sy0) * 120u;
    for (u32 i = 0u; i < n32; i++)
      d32[i] = pair;
  };

  if (lcount == 0u) {
    fill_rows_backdrop(y0, y_end, pal_conv[0]);
    if (!has_bg2_snaps) {
      for (u32 y = y0; y < y_end; y++)
        cgba_update_affine_refs(y);
    }
    return true;
  }

  cgba_mode0_bg_plan text_plans[2];
  if (vis_eff & 0x01u)
    cgba_build_mode1_text_plan(0u, &text_plans[0]);
  if (vis_eff & 0x02u)
    cgba_build_mode1_text_plan(1u, &text_plans[1]);

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE) && !defined(USE_XBGR1555_FORMAT)
  bool can_fold_blend = false;
  u32 fold_lower_obj_mask = 0u;
  if (blend_bg < 2u && !(blend_eva == 0u && blend_evb == 16u) &&
      (text_plans[blend_bg].flags & 1u) != 0u) {
    bool below_text_bg = false;
    for (u32 i = 0u; i < lcount; i++) {
      u32 l = lorder[i];
      if (l == blend_bg)
        break;
      if (l & 0x04u)
        fold_lower_obj_mask |= (1u << (l & 3u));
      else if (l < 2u)
        below_text_bg = true;
    }
    can_fold_blend = !below_text_bg;
  }
#endif

#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
  const u32 *const blend_term_a_s4 =
    (blend_bg < 2u && blend_evb == 16u && blend_eva != 0u)
      ? cgba_bg_blend_term_a_s4_get(blend_eva)
      : NULL;
  const u32 *const blend_term_a =
    (blend_bg < 2u && blend_evb == 16u && blend_eva != 0u)
      ? cgba_bg_blend_term_a_get(blend_eva)
      : NULL;
#endif

  const u32 bg2_cnt = (vis_eff & 0x04u) ? read_ioreg(REG_BG2CNT) : 0u;
  const u32 bg2_map_size = (bg2_cnt >> 14) & 3u;
  const bool bg2_wrap = (bg2_cnt & 0x2000u) != 0u;
  const u32 bg2_cbb = (bg2_cnt >> 2) & 3u;
  const u8 *const bg2_tile_base = &vram[bg2_cbb * 16384u];
  const u8 *const bg2_map_base = &vram[((bg2_cnt >> 8) & 0x1Fu) * 2048u];
#ifdef CGBA_SH4_ASM_4BPP
  const u32 bg2_shift = 17u - bg2_map_size;
  const s32 bg2_limit = (s32)((128u << bg2_map_size) << 8);
  const u8 *bg2_tkind = NULL;
  bool bg2_tkind_ready = false;
#endif

  /* Process Mode 1 in 24-scanline (11.25 KiB) strips aligned to 8-line tile
   * boundaries so the framebuffer strip stays hot in the 32 KiB D-cache across
   * BG2, BG1, BG0, and OBJ passes while preserving full 8-line tile batching. */
  u32 sy0 = y0;
  while (sy0 < y_end) {
    u32 sy1 = ((sy0 + 24u) & ~7u);
    if (sy1 <= sy0)
      sy1 = sy0 + 24u;
    if (sy1 > y_end)
      sy1 = y_end;

    bool any_folded = false;
    const u16 *row_pal[32];
    u32 strip_n = sy1 - sy0;
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE) && !defined(USE_XBGR1555_FORMAT)
    bool any_mixed = false;
    u8 row_mixed[32];
    if (can_fold_blend) {
      const cgba_mode0_bg_plan *bp = &text_plans[blend_bg];
      u32 last_key = 0xFFFFFFFFu;
      const u16 *last_fpal = pal_conv;
      u32 r = 0u;
      while (r < strip_n) {
        u32 y = sy0 + r;
        u32 start_r = (y + bp->vofs) & 7u;
        u32 k = 8u - start_r;
        if (r + k > strip_n)
          k = strip_n - r;
        u8 kinds[8];
        u8 cidxs[8];
        cgba_blend_tilerow_classify(bp, y, k, kinds, cidxs);
        for (u32 j = 0u; j < k; j++) {
          u32 rr = r + j;
          u32 kind = kinds[j];
          if (kind == 1u) {
            u32 cidx = cidxs[j];
            bool need_obj = (fold_lower_obj_mask != 0u) &&
                            ((obj_row_prio_mask[sy0 + rr] & fold_lower_obj_mask) != 0u);
            u32 ckey = cidx | (need_obj ? 0x100u : 0u);
            if (ckey != last_key) {
              last_key = ckey;
              last_fpal = cgba_m1_folded_pal_get(
                pal_conv[cidx], blend_eva, blend_evb, need_obj);
            }
            row_pal[rr] = last_fpal;
            row_mixed[rr] = 0u;
            any_folded = true;
          } else {
            row_pal[rr] = pal_conv;
            row_mixed[rr] = (kind == 2u) ? 1u : 0u;
            if (kind == 2u)
              any_mixed = true;
          }
        }
        r += k;
      }
    }
#endif

    auto fill_strip_bg = [&]() {
      if (any_folded) {
        for (u32 r = 0u; r < strip_n; r++)
          fill_rows_backdrop(sy0 + r, sy0 + r + 1u, row_pal[r][0]);
      } else {
        fill_rows_backdrop(sy0, sy1, pal_conv[0]);
      }
    };

    u32 base_done = 0u;
    for (u32 li = 0u; li < lcount; li++) {
      u32 layer = lorder[li];

      if (layer & 0x04u) {
        if (!base_done) {
          fill_strip_bg();
          base_done = 1u;
        }
        u32 prio = layer & 3u;
#if defined(CGBA_SH4_ASM_4BPP) && CGBA_VIDEO_SPRITE_DECODE_CACHE
        cgba_render_obj_batch_fullcolor_strip(
          prio, sy0, sy1, screen_base, any_folded ? row_pal : NULL, pal_conv);
#else
        u32 pbit = 1u << prio;
        for (u32 y = sy0; y < sy1; y++) {
          if (obj_row_prio_mask[y] & pbit) {
            const u16 *rpal = any_folded ? row_pal[y - sy0] : pal_conv;
            write_ioreg(REG_VCOUNT, y);
            render_scanline_objs<u16, FULLCOLOR>(
              prio, 0, 240, screen_base + y * 240u, rpal + 0x100);
          }
        }
#endif
        continue;
      }

      if (layer == 2u) {
        const bool isbase = (base_done == 0u);
#ifdef CGBA_SH4_ASM_4BPP
        if (!isbase && !bg2_tkind_ready) {
          bg2_tkind = cgba_aff_tile_kind_get(bg2_cbb);
          bg2_tkind_ready = true;
        }
        cgba_aff_rot_bg_params ap;
        ap.map_base = bg2_map_base;
        ap.tile_base = bg2_tile_base;
        ap.pal = pal_conv;
        ap.pix_shift = -(s32)(25u - bg2_map_size);
        ap.row_shift = (s32)(bg2_map_size + 1u);
        ap.tile_kind = isbase ? NULL : bg2_tkind;
        const u16 *const signed_bg_pal = isbase ? cgba_signed_bg_pal_get() : NULL;
        const u32 *const signed_bg_pal_dup = isbase ? (cgba_signed_bg_pal_dup + 128) : NULL;
#endif
        const u16 saved_pa = read_ioreg(REG_BG2PA);
        const u16 saved_pc = read_ioreg(REG_BG2PC);
        const s32 saved_rx = affine_reference_x[0];
        const s32 saved_ry = affine_reference_y[0];
        bool regs_clobbered = false;

        u16 *dst_row = screen_base + sy0 * 240u;
        for (u32 y = sy0; y < sy1; y++, dst_row += 240u) {
          const u16 *rpal = any_folded ? row_pal[y - sy0] : pal_conv;
          s32 ref_x, ref_y, dx, dy;
          if (has_bg2_snaps) {
            ref_x = cgba_bg2_snaps[y].ref_x;
            ref_y = cgba_bg2_snaps[y].ref_y;
            dx = cgba_bg2_snaps[y].pa;
            dy = cgba_bg2_snaps[y].pc;
          } else {
            ref_x = affine_reference_x[0];
            ref_y = affine_reference_y[0];
            dx = (s16)read_ioreg(REG_BG2PA);
            dy = (s16)read_ioreg(REG_BG2PC);
          }
#ifdef CGBA_SH4_ASM_4BPP
          if (__builtin_expect(dy != 0, 1)) {
            if (bg2_wrap) {
              if (isbase) {
                const bool is_folded = (rpal != pal_conv);
                cgba_dispatch_affine_rot_base_sh4(
                  dst_row + 240u, bg2_map_base, bg2_tile_base, rpal,
                  ref_x + 239 * dx, ref_y + 239 * dy, dx, dy, 240u, bg2_map_size,
                  is_folded,
                  is_folded ? rpal : signed_bg_pal,
                  is_folded ? NULL : signed_bg_pal_dup,
                  is_folded ? cgba_m1_fpal_ver : 0u);
              } else {
                u32 abs_step = (u32)(dx < 0 ? -dx : dx) + (u32)(dy < 0 ? -dy : dy);
                ap.pal = rpal;
                ap.use_same_texel = (abs_step < 180u) ? 1u : 0u;
                ap.dst_end = dst_row + 240u;
                ap.ux = (u32)(ref_x + 239 * dx) << bg2_shift;
                ap.uy = (u32)(ref_y + 239 * dy) << bg2_shift;
                ap.step_x = (u32)(-dx) << bg2_shift;
                ap.step_y = (u32)(-dy) << bg2_shift;
                ap.cnt = 240u;
                cgba_render_affine_rot_over_sh4(&ap);
              }
            } else {
              s32 lo = 0, hi = 239;
              cgba_affine_clip_axis(ref_x, dx, bg2_limit, lo, hi);
              if (lo <= hi)
                cgba_affine_clip_axis(ref_y, dy, bg2_limit, lo, hi);
              if (lo > hi) {
                if (isbase) {
                  u16 bgcol = rpal[0];
                  for (u32 i = 0u; i < 240u; i++)
                    dst_row[i] = bgcol;
                }
              } else {
                u32 u_lo = (u32)lo;
                u32 run_cnt = (u32)(hi - lo + 1);
                if (isbase && u_lo > 0u) {
                  u16 bgcol = rpal[0];
                  for (u32 i = 0u; i < u_lo; i++)
                    dst_row[i] = bgcol;
                }
                if (isbase) {
                  const bool is_folded = (rpal != pal_conv);
                  cgba_dispatch_affine_rot_base_sh4(
                    dst_row + (u_lo + run_cnt), bg2_map_base, bg2_tile_base, rpal,
                    ref_x + hi * dx, ref_y + hi * dy, dx, dy, run_cnt, bg2_map_size,
                    is_folded,
                    is_folded ? rpal : signed_bg_pal,
                    is_folded ? NULL : signed_bg_pal_dup,
                    is_folded ? cgba_m1_fpal_ver : 0u);
                } else {
                  u32 abs_step = (u32)(dx < 0 ? -dx : dx) + (u32)(dy < 0 ? -dy : dy);
                  ap.pal = rpal;
                  ap.use_same_texel = (abs_step < 180u) ? 1u : 0u;
                  ap.dst_end = dst_row + (u_lo + run_cnt);
                  ap.ux = (u32)(ref_x + hi * dx) << bg2_shift;
                  ap.uy = (u32)(ref_y + hi * dy) << bg2_shift;
                  ap.step_x = (u32)(-dx) << bg2_shift;
                  ap.step_y = (u32)(-dy) << bg2_shift;
                  ap.cnt = run_cnt;
                  cgba_render_affine_rot_over_sh4(&ap);
                }
                if (isbase && (u_lo + run_cnt) < 240u) {
                  u16 bgcol = rpal[0];
                  for (u32 i = u_lo + run_cnt; i < 240u; i++)
                    dst_row[i] = bgcol;
                }
              }
            }
          } else
#endif
          {
            if (has_bg2_snaps) {
              write_ioreg(REG_BG2PA, (u16)dx);
              write_ioreg(REG_BG2PC, (u16)dy);
              affine_reference_x[0] = ref_x;
              affine_reference_y[0] = ref_y;
              regs_clobbered = true;
            }
            cgba_mode1_bg2_fallback(
              isbase, bg2_wrap, dy, bg2_map_size,
              bg2_map_base, bg2_tile_base, dst_row, rpal);
          }
          if (!has_bg2_snaps)
            cgba_update_affine_refs(y);
        }
        if (regs_clobbered) {
          write_ioreg(REG_BG2PA, saved_pa);
          write_ioreg(REG_BG2PC, saved_pc);
          affine_reference_x[0] = saved_rx;
          affine_reference_y[0] = saved_ry;
        }
        base_done = 1u;
        continue;
      }

      const cgba_mode0_bg_plan &plan = text_plans[layer];

      if (layer == blend_bg) {
        if (!base_done) {
          fill_strip_bg();
          base_done = 1u;
        }
        any_folded = false;
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE) && !defined(USE_XBGR1555_FORMAT)
        if (can_fold_blend) {
          if (!any_mixed)
            continue;
          for (u32 r = 0u; r < strip_n; r++) {
            if (!row_mixed[r])
              continue;
            u32 y = sy0 + r;
            write_ioreg(REG_VCOUNT, y);
            cgba_render_scanline_text_4bpp_blend(
              blend_bg, 0u, 240u, screen_base + y * 240u, blend_eva, blend_evb);
          }
          continue;
        }
#endif
        if (blend_eva == 0u && blend_evb == 16u)
          continue;
#if defined(CGBA_SH4_ASM_4BPP) && !defined(USE_XBGR1555_FORMAT)
        if (blend_evb == 16u) {
          cgba_mode1_blend_evb16_strip(
            &plan, sy0, sy1, screen_base, blend_term_a_s4, blend_term_a);
          continue;
        }
#endif
        cgba_render_4bpp_bg_blend_rows(
          &plan, sy0, sy1, screen_base + sy0 * 240u, blend_eva, blend_evb);
        continue;
      }

      if (!(plan.flags & 1u)) {
        for (u32 y = sy0; y < sy1; y++) {
          write_ioreg(REG_VCOUNT, y);
          if (base_done)
            render_scanline_text<u16, FULLCOLOR, false>(
              layer, 0, 240, screen_base + y * 240u, pal_conv);
          else
            render_scanline_text<u16, FULLCOLOR, true>(
              layer, 0, 240, screen_base + y * 240u, pal_conv);
        }
        base_done = 1u;
        continue;
      }

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
      cgba_render_4bpp_text_bg_strip_sh4(
        &plan, sy0, sy1, screen_base, base_done != 0u, pal_conv, base_pal, pair_lut);
      base_done = 1u;
#else
      for (u32 y = sy0; y < sy1; y++) {
        write_ioreg(REG_VCOUNT, y);
        if (base_done)
          render_scanline_text<u16, FULLCOLOR, false>(
            layer, 0, 240, screen_base + y * 240u, pal_conv);
        else
          render_scanline_text<u16, FULLCOLOR, true>(
            layer, 0, 240, screen_base + y * 240u, pal_conv);
      }
      base_done = 1u;
#endif
    }

    if (!base_done)
      fill_strip_bg();
    sy0 = sy1;
  }

  if (!has_bg2_snaps && !(vis_eff & 0x04u)) {
    for (u32 y = y0; y < y_end; y++)
      cgba_update_affine_refs(y);
  }
  return true;
}

CGBA_HOT_TEXT void cgba_flush_pending_scanlines(void)
{
  u32 count = cgba_pending_sl_count;
  if (!count)
    return;
  u32 y0 = cgba_pending_sl_start;
  u32 y_end = y0 + count;
  u32 snap_n = cgba_bg2_snap_count;
  cgba_pending_sl_count = 0;
  cgba_bg2_snap_count = 0u;

  bool has_bg2_snaps = false;
  if (snap_n > 0u) {
    if (snap_n < count && y_end <= 160u) {
      s16 pa = (s16)read_ioreg(REG_BG2PA);
      s16 pc = (s16)read_ioreg(REG_BG2PC);
      for (u32 i = snap_n; i < count; i++) {
        u32 y = y0 + i;
        cgba_bg2_snaps[y].ref_x = affine_reference_x[0];
        cgba_bg2_snaps[y].ref_y = affine_reference_y[0];
        cgba_bg2_snaps[y].pa = pa;
        cgba_bg2_snaps[y].pc = pc;
        cgba_update_affine_refs(y);
      }
    }
    has_bg2_snaps = true;
  }

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
    if (has_bg2_snaps) {
      cgba_sl_io_snap_valid = false;
      cgba_sl_cur_uniform = false;
      cgba_sl_frame_bg_dirty = true;
    }
  } else if ((cgba_video_io_dirty | cgba_vram_dirty |
              (palette_ram_dirty & CGBA_PALETTE_DIRTY_SCANLINE) |
              reg[OAM_UPDATED]) != 0) {
    cgba_sl_midframe_dirty(video_mode, dispcnt);
  }
  if (has_bg2_snaps) {
    cgba_sl_cur_uniform = false;
    cgba_sl_frame_bg_dirty = true;
  }

  const bool bg_dirty = cgba_sl_frame_bg_dirty;
  const bool obj_dirty = cgba_sl_frame_obj_dirty;
  const u8 cur_uniform = cgba_sl_cur_uniform ? 1u : 0u;
  const bool obj_en = !(dispcnt & 0x80) &&
                      (((dispcnt & 0x1000) != 0) || ((dispcnt & 0x8000) != 0));
  const u16 saved_vc = read_ioreg(REG_VCOUNT);

  if (__builtin_expect(cgba_bg2_affine_mode_ok(dispcnt) &&
                       bg_dirty && pitch == 240u && (y_end - y0) >= 2u &&
                       y_end <= 160u, 0)) {
    if (cgba_try_render_mode1_batch(y0, y_end, screen_base, dispcnt, has_bg2_snaps)) {
      cgba_video_frame_dirty_rows += (y_end - y0);
      cgba_video_mark_rows_dirty(y0, y_end);
      for (u32 y = y0; y < y_end; y++) {
        cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
        cgba_sl_row_valid[y] = cur_uniform;
      }
      write_ioreg(REG_VCOUNT, saved_vc);
      if (y_end >= 160u)
        cgba_sl_prev_uniform = cgba_sl_cur_uniform;
      return;
    }
  }

  if (__builtin_expect(!has_bg2_snaps && video_mode <= 2u && !(dispcnt & 0x80), 1)) {
    if (__builtin_expect(!cgba_bg_plan_valid || cgba_m0.video_mode != video_mode, 0)) {
      cgba_rebuild_bg_plans(video_mode, dispcnt);
      cgba_bg_plan_valid = true;
    }
    const bool obj_alpha_can = cgba_mode0_obj_alpha_can_blend != 0;
    const bool m0_obj_en = cgba_mode0_obj_enabled != 0;
    (void)obj_alpha_can;

#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
    if (__builtin_expect(bg_dirty && pitch == 240u &&
                         cgba_m0.win_multi_active != 0u &&
                         y_end <= 160u, 0)) {
      u32 wctrl = dispcnt >> 13;
      u16 w0v = read_ioreg(REG_WIN0V), w1v = read_ioreg(REG_WIN1V);
      u32 w0t = w0v >> 8, w0b = w0v & 0xFFu;
      u32 w1t = w1v >> 8, w1b = w1v & 0xFFu;
      u32 sy0 = y0;
      while (sy0 < y_end) {
        bool in0 = ((wctrl & 1u) != 0u) && in_window_y(sy0, w0t, w0b);
        bool in1 = ((wctrl & 2u) != 0u) && in_window_y(sy0, w1t, w1b);
        u32 sy1 = sy0 + 16u;
        if (sy1 > y_end)
          sy1 = y_end;
        for (u32 y = sy0 + 1u; y < sy1; y++) {
          bool n0 = ((wctrl & 1u) != 0u) && in_window_y(y, w0t, w0b);
          bool n1 = ((wctrl & 2u) != 0u) && in_window_y(y, w1t, w1b);
          if (n0 != in0 || n1 != in1) {
            sy1 = y;
            break;
          }
        }
        cgba_render_win_multi_strip_sh4(video_mode, dispcnt, sy0, sy1, screen_base);
        if (video_mode != 0u)
          cgba_advance_affine_refs(sy0, sy1);
        u32 nrows = sy1 - sy0;
        cgba_video_frame_dirty_rows += nrows;
        cgba_video_mark_rows_dirty(sy0, sy1);
        for (u32 y = sy0; y < sy1; y++) {
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

    const bool has_bright = cgba_m0.bright_effect != 0u;
    const bool has_fast = cgba_mode0_fast_fullframe != 0 ||
                          cgba_mode0_fast_no_obj != 0 ||
                          cgba_m0.blend_bg_layer < 4u ||
                          has_bright;
    if (__builtin_expect(bg_dirty && pitch == 240u &&
                         ((y_end - y0) >= 2u || has_bright) &&
                         has_fast && y_end <= 160u, 1)) {
      const u32 base_l =
        (cgba_mode0_lcount[0] > 0u) ? (u32)cgba_mode0_lorder[0][0] : 0xFFu;
      const u32 strip_phase =
        (base_l < 4u && (cgba_bg_plans[base_l].flags & (1u | 8u)))
          ? (cgba_bg_plans[base_l].vofs & 7u)
          : 0u;
      u32 sy0 = y0;
      while (sy0 < y_end) {
        u32 sy1 = sy0 + 16u - ((sy0 + strip_phase) & 7u);
        if (sy1 > y_end)
          sy1 = y_end;
        if (__builtin_expect(cgba_m0.bright_win_active != 0u, 0)) {
          u32 wy0 = cgba_m0.bright_win_y0;
          u32 wy1 = cgba_m0.bright_win_y1;
          if (sy0 < wy0 && sy1 > wy0)
            sy1 = wy0;
          else if (sy0 < wy1 && sy1 > wy1)
            sy1 = wy1;
        }
        u32 nrows = sy1 - sy0;
        u32 strip_omask = 0u;
        bool alpha_all_ok = true;
        if (m0_obj_en) {
          const u8 *pm = obj_row_prio_mask + sy0;
          u32 first_nz = (pm[0] & 0x0Fu) != 0u ? 1u : 0u;
          if (obj_alpha_can) {
            bool first_ok = cgba_row_alpha_ok(sy0);
            for (u32 r = 0; r < nrows; r++) {
              u32 p_r = pm[r] & 0x0Fu;
              bool ok_r = cgba_row_alpha_ok(sy0 + r);
              if (r > 0u &&
                  ((p_r != 0u ? 1u : 0u) != first_nz ||
                   ok_r != first_ok)) {
                sy1 = sy0 + r;
                nrows = r;
                break;
              }
              strip_omask |= p_r;
              if (!ok_r)
                alpha_all_ok = false;
            }
          } else {
            for (u32 r = 0; r < nrows; r++) {
              u32 p_r = pm[r] & 0x0Fu;
              if (r > 0u && (p_r != 0u ? 1u : 0u) != first_nz) {
                sy1 = sy0 + r;
                nrows = r;
                break;
              }
              strip_omask |= p_r;
            }
          }
        }
        if (__builtin_expect((nrows >= 2u || has_bright) &&
                             (cgba_mode0_can_fast(strip_omask) || has_bright) &&
                             alpha_all_ok, 1)) {
          render_mode0_fullcolor_strip_sh4(
            sy0, sy1, screen_base,
            cgba_mode0_lorder[strip_omask], cgba_mode0_lcount[strip_omask]);
          if (video_mode != 0u)
            cgba_advance_affine_refs(sy0, sy1);
          cgba_video_frame_dirty_rows += nrows;
          cgba_video_mark_rows_dirty(sy0, sy1);
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
          cgba_video_mark_row_dirty(y);
          u32 omask = m0_obj_en ? (u32)(obj_row_prio_mask[y] & 0x0Fu) : 0u;
          bool row_aok = cgba_row_alpha_ok(y);
          if (cgba_mode0_can_fast(omask) && row_aok) {
            render_mode0_fullcolor_scanline(
              y, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
          } else if (has_bright && row_aok) {
            render_mode0_fullcolor_strip_sh4(
              y, y + 1u, screen_base,
              cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
          } else {
            write_ioreg(REG_VCOUNT, y);
            order_layers((dispcnt >> 8) & active_layers[video_mode], y);
            render_scanline_window(screen_offset);
          }
          cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
          cgba_sl_row_valid[y] = cur_uniform;
          if (video_mode != 0u)
            cgba_update_affine_refs(y);
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
      if (!row_dirty) {
        if (video_mode != 0u)
          cgba_update_affine_refs(y);
        continue;
      }
      cgba_video_frame_dirty_rows++;
      cgba_video_mark_row_dirty(y);
      u32 omask = m0_obj_en ? (u32)(obj_row_prio_mask[y] & 0x0Fu) : 0u;
      bool row_aok = cgba_row_alpha_ok(y);
      if (__builtin_expect(cgba_mode0_can_fast(omask) && row_aok, 1)) {
        render_mode0_fullcolor_scanline(
          y, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      }
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
      else if (pitch == 240u && y < 160u && cgba_m0.bright_effect != 0u && row_aok) {
        render_mode0_fullcolor_strip_sh4(
          y, y + 1u, screen_base,
          cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      }
#endif
      else {
        write_ioreg(REG_VCOUNT, y);
        order_layers((dispcnt >> 8) & active_layers[video_mode], y);
        render_scanline_window(screen_offset);
      }
      if (__builtin_expect(y < 160u, 1)) {
        cgba_sl_row_has_obj[y] = (obj_en && obj_row_prio_mask[y] != 0) ? 1u : 0u;
        cgba_sl_row_valid[y] = cur_uniform;
      }
      if (video_mode != 0u)
        cgba_update_affine_refs(y);
    }
    write_ioreg(REG_VCOUNT, saved_vc);
    if (y_end >= 160u)
      cgba_sl_prev_uniform = cgba_sl_cur_uniform;
    return;
  }

  {
    const u16 saved_pa = read_ioreg(REG_BG2PA);
    const u16 saved_pc = read_ioreg(REG_BG2PC);
    const s32 saved_rx = affine_reference_x[0];
    const s32 saved_ry = affine_reference_y[0];
    u16 *screen_offset = screen_base + y0 * pitch;
    for (u32 y = y0; y < y_end; y++, screen_offset += pitch) {
      if (has_bg2_snaps && y < 160u) {
        write_ioreg(REG_BG2PA, (u16)cgba_bg2_snaps[y].pa);
        write_ioreg(REG_BG2PC, (u16)cgba_bg2_snaps[y].pc);
        affine_reference_x[0] = cgba_bg2_snaps[y].ref_x;
        affine_reference_y[0] = cgba_bg2_snaps[y].ref_y;
      }
      bool row_dirty = bg_dirty ||
                       (y >= 160u) ||
                       !cgba_sl_row_valid[y] ||
                       (obj_dirty && cgba_sl_row_has_obj[y]);
      if (row_dirty) {
        cgba_video_frame_dirty_rows++;
        cgba_video_mark_row_dirty(y);
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
      if (video_mode != 0u && !has_bg2_snaps)
        cgba_update_affine_refs(y);
    }
    if (has_bg2_snaps) {
      write_ioreg(REG_BG2PA, saved_pa);
      write_ioreg(REG_BG2PC, saved_pc);
      affine_reference_x[0] = saved_rx;
      affine_reference_y[0] = saved_ry;
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

#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
static inline void cgba_blend_caches_invalidate(void)
{
#ifdef CGBA_PALETTE_DIRTY_ACTIVE
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
#endif
  cgba_term_a_epoch++;
#ifdef CGBA_SH4_ASM_4BPP
  cgba_signed_bg_pal_valid = false;
#endif
  cgba_bg_blend_bd_key = 0xFFFFFFFFu;
#if defined(CGBA_VIDEO_DEFERRED_ACTIVE) && \
    defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && \
    defined(CGBA_VIDEO_PAIR_PALETTE)
  cgba_folded_valid = false;
  cgba_m1_pal_snap_valid = false;
  cgba_folded_union_owner = 0u;
#endif
#if CGBA_PALETTE_BLEND_BIT
  cgba_direct_term_a_eva = 0xFFFFFFFFu;
  cgba_direct_obj_term_a_eva = 0xFFFFFFFFu;
#endif
}
#endif

CGBA_HOT_TEXT void update_scanline(void)
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
    cgba_video_mark_row_dirty(vcount);

    // If the screen is in forced blank draw pure white.
    if (__builtin_expect((dispcnt & 0x80) != 0, 0))
      memset(screen_offset, 0xff, 240*sizeof(u16));
    else if (__builtin_expect(video_mode <= 2u, 1)) {
      if (__builtin_expect(!cgba_bg_plan_valid || cgba_m0.video_mode != video_mode, 0)) {
        cgba_rebuild_bg_plans(video_mode, dispcnt);
        cgba_bg_plan_valid = true;
      }
      u32 omask = cgba_mode0_obj_enabled ? (u32)(obj_row_prio_mask[vcount] & 0x0Fu) : 0u;
      bool row_aok = cgba_row_alpha_ok(vcount);
      if (__builtin_expect(cgba_mode0_can_fast(omask) && row_aok, 1)) {
        render_mode0_fullcolor_scanline(
          vcount, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      }
#if defined(CGBA_SH4_ASM_4BPP) && defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) && defined(CGBA_VIDEO_PAIR_PALETTE)
      else if (pitch == 240u && vcount < 160u && cgba_m0.bright_effect != 0u && row_aok) {
        render_mode0_fullcolor_strip_sh4(
          vcount, vcount + 1u, screen_base,
          cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
      }
#endif
      else {
        order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
        render_scanline_window(screen_offset);
      }
    }
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
  else if (video_mode <= 2u) {
    cgba_rebuild_bg_plans(video_mode, dispcnt);
    u32 omask = cgba_mode0_obj_enabled ? (u32)(obj_row_prio_mask[vcount] & 0x0Fu) : 0u;
    if (cgba_mode0_can_fast(omask) && cgba_row_alpha_ok(vcount)) {
      render_mode0_fullcolor_scanline(
        vcount, screen_offset, cgba_mode0_lorder[omask], cgba_mode0_lcount[omask]);
    } else {
      order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
      render_scanline_window(screen_offset);
    }
  }
  else {
    order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);
    render_scanline_window(screen_offset);
  }
#endif

  // Mode 0 does not use any affine params at all.
  if (__builtin_expect(video_mode != 0u, 0))
    cgba_update_affine_refs(vcount);
}
