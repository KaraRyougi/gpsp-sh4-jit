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

#ifndef VIDEO_H
#define VIDEO_H

void update_scanline(void);
void video_reload_counters(void);
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
void video_invalidate_scanline_cache(u32 start_y, u32 end_y);
extern u32 cgba_video_frame_dirty_rows;
extern u32 cgba_video_frame_dirty_row_mask[5];
#else
static inline void video_invalidate_scanline_cache(u32 start_y, u32 end_y)
{ (void)start_y; (void)end_y; }
#endif

extern s32 affine_reference_x[2];
extern s32 affine_reference_y[2];

/* Commit a 4-word (16-byte) BG2PA..BG2Y affine register block (0x04000020)
 * from a 4-byte-aligned guest buffer, refreshing dma_bus_val, dirty flag,
 * and BG2 internal reference point latches. Shared by bulk DMA and HBlank
 * BG2 affine fast paths. */
static inline void cgba_commit_bg2_affine_dma_4words(const u8 *s)
{
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
  {
    u32 rx = eswap32(w2);
    u32 ry = eswap32(w3);
    dma_bus_val = ry;
#ifdef CGBA_VRAM_DIRTY_ACTIVE
    cgba_video_io_dirty = 1;
#endif
    affine_reference_x[0] = ((s32)(rx << 4)) >> 4;
    affine_reference_y[0] = ((s32)(ry << 4)) >> 4;
  }
}

extern u16* gba_screen_pixels;

#endif
