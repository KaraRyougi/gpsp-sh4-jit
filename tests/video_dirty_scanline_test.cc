#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CGBA_VIDEO_TEXT_FAST4BPP 1
#define CGBA_VIDEO_DIRTY_SCANLINE 1
#define CGBA_VIDEO_SPRITE_DECODE_CACHE 1
#define CGBA_VIDEO_BACKDROP_SHADOW_PALETTE 1
#define CGBA_VIDEO_OPAQUE_ROW_UNROLL 1

#include "../vendor/gpsp/video.cc"

u16 palette_ram_converted[512];
u16 io_registers[512];
u16 oam_ram[512];
u8 vram[96 * 1024];
u32 reg[64];
u32 skip_next_frame = 0;
int sprite_limit = 1;
volatile u32 palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
volatile u32 cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;
volatile u32 cgba_vram_sb_dirty = 0;
volatile u32 cgba_video_io_dirty = 1;

static unsigned seed = 0x51A7BEEFu;
static unsigned rand_u32(void)
{
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

#define CHECK(cond) do { \
  if (!(cond)) { \
    printf("video_dirty_scanline_test failed at line %d\n", __LINE__); \
    return false; \
  } \
} while (0)

static u16 fb_cached[240 * 160];
static u16 fb_ref[240 * 160];

/* Render a full frame into dst WITHOUT using the scanline cache, matching
 * vanilla update_scanline() semantics exactly. */
static void render_frame_reference(u16 *dst)
{
  s32 saved_ax[2] = { affine_reference_x[0], affine_reference_x[1] };
  s32 saved_ay[2] = { affine_reference_y[0], affine_reference_y[1] };
  u16 saved_vcount = io_registers[REG_VCOUNT];
  u32 saved_oam_upd = reg[OAM_UPDATED];

  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 video_mode = dispcnt & 0x07;
  order_obj(video_mode);

  for (u32 y = 0; y < 160; y++) {
    write_ioreg(REG_VCOUNT, (u16)y);
    order_layers((dispcnt >> 8) & active_layers[video_mode], y);
    u16 *row = dst + y * 240;
    if (dispcnt & 0x80)
      memset(row, 0xff, 240 * sizeof(u16));
    else
      render_scanline_window(row);

    if (video_mode) {
      const u32 bgmosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xF) + 1;
      if (read_ioreg(REG_BG2CNT) & 0x40) {
        if ((y % bgmosv) == bgmosv - 1) {
          affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB) * bgmosv;
          affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD) * bgmosv;
        }
      } else {
        affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB);
        affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD);
      }
      if (read_ioreg(REG_BG3CNT) & 0x40) {
        if ((y % bgmosv) == bgmosv - 1) {
          affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB) * bgmosv;
          affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD) * bgmosv;
        }
      } else {
        affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB);
        affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD);
      }
    }
  }

  affine_reference_x[0] = saved_ax[0];
  affine_reference_x[1] = saved_ax[1];
  affine_reference_y[0] = saved_ay[0];
  affine_reference_y[1] = saved_ay[1];
  io_registers[REG_VCOUNT] = saved_vcount;
  reg[OAM_UPDATED] = saved_oam_upd;
}

static void run_cached_frame(void)
{
  gba_screen_pixels = fb_cached;
  for (u32 y = 0; y < 160; y++) {
    write_ioreg(REG_VCOUNT, (u16)y);
    update_scanline();
  }
}

static bool verify_frame(const char *label)
{
  video_reload_counters();
  render_frame_reference(fb_ref);
  video_reload_counters();
  run_cached_frame();
  if (memcmp(fb_cached, fb_ref, sizeof(fb_cached)) != 0) {
    for (u32 y = 0; y < 160; y++) {
      if (memcmp(fb_cached + y * 240, fb_ref + y * 240, 240 * sizeof(u16)) != 0) {
        printf("MISMATCH in '%s' at row %u (dirty_rows=%u)\n",
               label, y, cgba_video_frame_dirty_rows);
        return false;
      }
    }
  }
  return true;
}

static void init_scene_mode0(void)
{
  memset(io_registers, 0, sizeof(io_registers));
  memset(vram, 0, sizeof(vram));
  memset(oam_ram, 0, sizeof(oam_ram));
  for (unsigned i = 0; i < 128; i++)
    oam_ram[i * 4] = eswap16(0x0200); /* disable all sprites */

  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (u16)(rand_u32() & 0xFFFFu);

  /* Populate 4bpp BG tiles (leave tile 0 zero for skip_zero exercise!) */
  for (unsigned i = 32; i < 0x4000; i++)
    vram[i] = (u8)rand_u32();
  /* Populate OBJ tiles */
  for (unsigned i = 0x10000; i < 0x14000; i++)
    vram[i] = (u8)rand_u32();
  /* Populate screenblocks 8..11 (mix of empty tile 0, hflip, vflip, subpal) */
  for (unsigned sb = 8; sb < 12; sb++) {
    u16 *map = (u16 *)&vram[sb * 2048];
    for (unsigned i = 0; i < 1024; i++) {
      u16 entry = (rand_u32() % 3 == 0) ? 0u
        : (u16)((rand_u32() & 0xFC7Fu));
      map[i] = eswap16(entry);
    }
  }

  write_ioreg(REG_DISPCNT, 0x1340); /* Mode 0, 1D OBJ, BG0+BG1+OBJ */
  write_ioreg(REG_BG0CNT, 0x0801);  /* char 0, screen 8, prio 1 */
  write_ioreg(REG_BG1CNT, 0x0902);  /* char 0, screen 9, prio 2 */
  write_ioreg(REG_BG0HOFS, 3);
  write_ioreg(REG_BG0VOFS, 5);
  write_ioreg(REG_BG1HOFS, 11);
  write_ioreg(REG_BG1VOFS, 17);

  /* Place a 16x16 sprite at (x=40, y=50) -> rows 50..65 */
  oam_ram[0] = eswap16(50u | 0x0000u);      /* y=50, normal, 4bpp, square */
  oam_ram[1] = eswap16(40u | 0x4000u);      /* x=40, 16x16 */
  oam_ram[2] = eswap16(4u | (1u << 10));    /* tile 4, prio 1 */

  reg[OAM_UPDATED] = 1;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;
  video_invalidate_scanline_cache(0, 160);
}

static bool test_fast4bpp_standalone(void)
{
  /* Directly compare render_scanline_text<FULLCOLOR> with fast4bpp vs
   * scalar reference (4bpp, base/overlay, hflip/vflip, odd offsets, tile0). */
  for (unsigned t = 0; t < 200; t++) {
    memset(vram, 0, 0x10000);
    for (unsigned i = (t & 1) ? 0 : 32; i < 0x2000; i++)
      vram[i] = (u8)rand_u32();
    for (unsigned i = 0; i < 256; i++)
      palette_ram_converted[i] = (u16)(rand_u32() & 0xFFFFu);
    u16 *map = (u16 *)&vram[8 * 2048];
    for (unsigned i = 0; i < 1024; i++) {
      u16 e = (rand_u32() % 4 == 0) ? 0u : (u16)(rand_u32() & 0xFC3Fu);
      map[i] = eswap16(e);
    }
    write_ioreg(REG_DISPCNT, 0x0100);
    write_ioreg(REG_BG0CNT, 0x0800 | ((t & 3) << 14));
    write_ioreg(REG_BG0HOFS, (u16)(rand_u32() & 511));
    write_ioreg(REG_BG0VOFS, (u16)(rand_u32() & 511));
    write_ioreg(REG_VCOUNT, (u16)(t % 160));
    write_ioreg(REG_MOSAIC, 0);

    u32 start = rand_u32() % 23;
    u32 end = 240 - (rand_u32() % 23);
    u16 got_base[240], ref_base[240], got_over[240], ref_over[240];
    for (unsigned i = 0; i < 240; i++) {
      u16 bg = (u16)rand_u32();
      got_base[i] = ref_base[i] = bg;
      got_over[i] = ref_over[i] = bg;
    }

    palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    render_scanline_text<u16, FULLCOLOR, true>(0, start, end, got_base, get_backdrop_shadow_palette());
    render_scanline_text<u16, FULLCOLOR, false>(0, start, end, got_over, palette_ram_converted);

    /* Independent scalar reference matching GBA 4bpp layout */
    u32 bg_cnt = read_ioreg(REG_BG0CNT);
    u32 sc_size = bg_cnt >> 14;
    u32 hofs = read_ioreg(REG_BG0HOFS) & 511;
    u32 vofs = read_ioreg(REG_BG0VOFS) & 511;
    u32 py = (read_ioreg(REG_VCOUNT) + vofs) & ((sc_size & 2) ? 511u : 255u);
    for (u32 x = start; x < end; x++) {
      u32 px = (x + hofs) & ((sc_size & 1) ? 511u : 255u);
      u32 tx = px >> 3, ty = py >> 3;
      u32 sb = (bg_cnt >> 8) & 0x1Fu;
      if (tx >= 32) { sb += 1; tx -= 32; }
      if (ty >= 32) { sb += (sc_size == 3) ? 2 : 1; ty -= 32; }
      u16 entry = eswap16(((u16 *)&vram[sb * 2048])[ty * 32 + tx]);
      u32 tile = entry & 0x3FFu;
      u32 pal = (entry >> 12) & 0xFu;
      u32 lx = (entry & 0x400u) ? (7u - (px & 7u)) : (px & 7u);
      u32 ly = (entry & 0x800u) ? (7u - (py & 7u)) : (py & 7u);
      u8 byte = vram[tile * 32 + ly * 4 + (lx >> 1)];
      u8 pix = (lx & 1u) ? (byte >> 4) : (byte & 0xFu);
      if (pix) {
        u16 col = palette_ram_converted[pal * 16 + pix];
        ref_base[x] = col;
        ref_over[x] = col;
      } else {
        ref_base[x] = palette_ram_converted[0];
      }
    }

    CHECK(memcmp(got_base, ref_base, sizeof(got_base)) == 0);
    CHECK(memcmp(got_over, ref_over, sizeof(got_over)) == 0);
  }
  return true;
}

static bool test_affine_clip_axis(void)
{
  static const s32 special_d[] = { 0, 1, -1, 256, -256, 187, -100, 512, -512, 3, -7 };
  static const s32 limits[] = { 8 << 8, 16 << 8, 32 << 8, 64 << 8, 128 << 8, 256 << 8, 1024 << 8 };
  for (unsigned t = 0; t < 4000; t++) {
    s32 limit = limits[t % (sizeof(limits) / sizeof(limits[0]))];
    s32 d = (t < 400)
              ? special_d[t % (sizeof(special_d) / sizeof(special_d[0]))]
              : ((s32)(rand_u32() & 0x7FFu) - 1024);
    s32 s = (s32)(rand_u32() % (u32)(limit * 2)) - limit / 2;
    s32 cnt = 1 + (s32)(rand_u32() % 240u);
    s32 lo = (s32)(rand_u32() % (u32)cnt);
    s32 hi = lo + (s32)(rand_u32() % (u32)(cnt - lo));

    /* Brute-force exact valid interval [ref_lo, ref_hi] within [lo, hi]. */
    s32 ref_lo = hi + 1, ref_hi = lo - 1;
    for (s32 i = lo; i <= hi; i++) {
      s32 v = s + i * d;
      if (v >= 0 && v < limit) {
        if (i < ref_lo) ref_lo = i;
        if (i > ref_hi) ref_hi = i;
      }
    }

    s32 got_lo = lo, got_hi = hi;
    cgba_affine_clip_axis(s, d, limit, got_lo, got_hi);
    if (ref_lo > ref_hi) {
      CHECK(got_lo > got_hi);
    } else {
      CHECK(got_lo == ref_lo && got_hi == ref_hi);
    }
  }
  return true;
}

static bool test_mode1_direct_and_fused_blend(void)
{
  /* Compare try_render_direct_single_bg_blend (both fused 4bpp-over-4bpp and
   * general direct blend, bd_is_tgt2 true/false, opaque/transparent base,
   * single/mixed subpalettes, saturating/non-saturating, evb==16, palette
   * mutation, and opaque-row VRAM invalidation) against STCKCOLOR + merge_blend. */
  memset(vram, 0, sizeof(vram));
  memset(oam_ram, 0, sizeof(oam_ram));
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (u16)(rand_u32() & 0xFFFFu);
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_BG | CGBA_VRAM_DIRTY_OBJ;
  cgba_bg_vram_invalidate();

  /* Populate CBB 2 (0x8000): tile 0 empty, tiles 1..15 opaque, tiles 16..31 sparse. */
  u8 *cbb2 = &vram[2 * 16384];
  for (unsigned t = 1; t < 16; t++) {
    for (unsigned b = 0; b < 32; b++) {
      u8 p0 = (u8)(1u + ((t + b) % 15u));
      u8 p1 = (u8)(1u + ((t * 3u + b) % 15u));
      cbb2[t * 32 + b] = (u8)(p0 | (p1 << 4));
    }
  }
  for (unsigned t = 16; t < 32; t++) {
    for (unsigned b = 0; b < 32; b++)
      cbb2[t * 32 + b] = (u8)((rand_u32() % 3 == 0) ? 0u : (rand_u32() & 0xFFu));
  }

  /* Populate Mode 1 affine BG2 map (SBB 4) and 8bpp tiles (CBB 0). */
  for (unsigned i = 0; i < 64 * 64; i++)
    vram[i] = (u8)((rand_u32() % 5 == 0) ? 0u : (1u + (rand_u32() % 200u)));
  for (unsigned i = 0; i < 1024; i++)
    vram[4 * 2048 + i] = (u8)(rand_u32() & 0x3Fu);

  static const u16 alphas[] = { 0x1008u, 0x0808u, 0x0C0Au, 0x0010u, 0x1000u, 0x0509u };
  for (unsigned cfg = 0; cfg < 24; cfg++) {
    bool bd_tgt2 = (cfg & 1) != 0;
    bool base_opaque = (cfg & 2) != 0;
    bool base_mixed_pal = (cfg & 4) != 0;
    bool cand_mixed_pal = (cfg & 8) != 0;

    /* SBB 0 = BG0 (cand), SBB 2 = BG1 (base). */
    u16 *m0 = (u16 *)&vram[0 * 2048];
    u16 *m1 = (u16 *)&vram[2 * 2048];
    for (unsigned i = 0; i < 1024; i++) {
      u32 c_tid = (rand_u32() % 4 == 0) ? 0u : (16u + (rand_u32() & 15u));
      u32 c_pal = cand_mixed_pal ? (rand_u32() & 15u) : 8u;
      u32 c_flip = (rand_u32() & 3u) << 10;
      m0[i] = eswap16((u16)(c_tid | c_flip | (c_pal << 12)));

      u32 b_tid = base_opaque ? (1u + (rand_u32() % 15u))
                              : ((rand_u32() % 4 == 0) ? 0u : (16u + (rand_u32() & 15u)));
      u32 b_pal = base_mixed_pal ? (rand_u32() & 15u) : 7u;
      u32 b_flip = (rand_u32() & 3u) << 10;
      m1[i] = eswap16((u16)(b_tid | b_flip | (b_pal << 12)));
    }
    cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
    cgba_bg_vram_invalidate();

    /* Mode 1: BG0 (prio 3, 4bpp), BG1 (prio 3, 4bpp), BG2 (prio 2, 8bpp affine). */
    write_ioreg(REG_DISPCNT, 0x0701u); /* Mode 1, BG0|BG1|BG2 enabled */
    write_ioreg(REG_BG0CNT, 0x000Bu);  /* CBB=2, SBB=0, prio=3 */
    write_ioreg(REG_BG1CNT, 0x020Bu);  /* CBB=2, SBB=2, prio=3 */
    write_ioreg(REG_BG2CNT, 0x4402u);  /* CBB=0, SBB=4, 256x256, prio=2 */
    write_ioreg(REG_BG0HOFS, (u16)(123u + cfg * 7u));
    write_ioreg(REG_BG0VOFS, (u16)(cfg * 13u));
    write_ioreg(REG_BG1HOFS, (u16)(cfg * 5u));
    write_ioreg(REG_BG1VOFS, (u16)(cfg * 3u));
    write_ioreg(REG_BG2PA, 187);
    write_ioreg(REG_BG2PC, (u16)(s16)-100);
    affine_reference_x[0] = 16 << 8;
    affine_reference_y[0] = 24 << 8;
    write_ioreg(REG_MOSAIC, 0);
    write_ioreg(REG_BLDALPHA, alphas[cfg % (sizeof(alphas) / sizeof(alphas[0]))]);
    write_ioreg(REG_BLDCNT, (u16)(0x0041u | 0x0200u | (bd_tgt2 ? 0x2000u : 0u)));

    /* Mutate palette mid-loop to verify fused LUT invalidation. */
    if (cfg == 12) {
      for (unsigned i = 0; i < 256; i++)
        palette_ram_converted[i] ^= 0x14A5u;
      palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    }

    for (u32 y = 0; y < 16; y++) {
      write_ioreg(REG_VCOUNT, (u16)(y * 9u));
      u32 start = (cfg + y) % 11u;
      u32 end = 240u - ((cfg * 3u + y) % 13u);
      order_layers(0x07u, y * 9u);

      u16 got[240], ref[240];
      u32 tmp_buf[240];
      memset(got, 0x5A, sizeof(got));
      memset(ref, 0x5A, sizeof(ref));

      bool used = try_render_direct_single_bg_blend(
        start, end, got, 0x27u, 0x07u, read_ioreg(REG_BLDCNT));
      if (!base_mixed_pal || bd_tgt2 || base_opaque)
        CHECK(used);

      if (used) {
        tile_render_layers<STCKCOLOR, STCKCOLOR, u32>(start, end, tmp_buf, 0x07u);
        merge_blend<BLEND_ONLY, false>(start, end, ref, tmp_buf);
        CHECK(memcmp(got, ref, sizeof(got)) == 0);
      }
    }

    /* Verify opaque-row cache invalidation when a tile becomes transparent. */
    if (cfg == 2) {
      CHECK(cgba_sb_row_is_all_opaque(2u, 2u, 1u));
      cbb2[1 * 32] = 0u; /* tile 1 now has transparent pixels */
      cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
      cgba_bg_vram_invalidate();
      CHECK(!cgba_sb_row_is_all_opaque(2u, 2u, 1u));
      /* Restore tile 1 */
      cbb2[1 * 32] = 0x21u;
      cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
      cgba_bg_vram_invalidate();
    }
  }
  return true;
}

static bool test_mode1_batch_and_bg2_snaps(void)
{
  /* Verify Mode 1 batch renderer + deferred per-scanline BG2 HBlank snapshots
   * (cgba_snapshot_bg2_affine_pending / cgba_flush_pending_scanlines /
   * cgba_try_render_mode1_batch) against immediate per-scanline rendering across:
   *  - Mode 0 top split + Mode 1 bottom Mode-7 with per-scanline BG2 PA..Y updates
   *  - BG1 alpha-blended horizon (evb=16 and general eva/evb, uniform solid + mixed + empty rows)
   *  - OBJs below and above blend_bg, plus dy==0 rows and non-wrap BG2
   *  - Windowed / mosaic fallback with pending BG2 snapshots + live register preservation
   *  - 160-bit cgba_video_frame_dirty_row_mask coverage */
  memset(vram, 0, sizeof(vram));
  memset(oam_ram, 0, sizeof(oam_ram));
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (u16)(rand_u32() & 0xFFFFu);
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;
  cgba_vram_sb_dirty = 0xFFFFFFFFu;
  cgba_bg_vram_invalidate();

  /* CBB 0 (4bpp text tiles for BG0/BG1):
   *   tile 0: empty
   *   tiles 1..8: mixed 4bpp tiles
   *   tile 9: per-scanline uniform solid rows (like Mario Kart horizon fog!) */
  for (unsigned i = 32; i < 9 * 32; i++)
    vram[i] = (u8)(rand_u32() & 0xFFu);
  for (unsigned r = 0; r < 8; r++) {
    u8 p = (u8)((r % 6u) + 1u);
    u8 b = (u8)(p | (p << 4));
    memset(&vram[9 * 32 + r * 4], b, 4);
  }
  /* CBB 1 (8bpp affine tiles for BG2): 64 tiles with full 0..255 color indices */
  for (unsigned i = 0; i < 64 * 64; i++)
    vram[16384 + i] = (u8)(rand_u32() & 0xFFu);
  /* OBJ VRAM (0x10000): 4bpp tiles */
  for (unsigned i = 32; i < 0x1000; i++)
    vram[0x10000 + i] = (u8)(rand_u32() & 0xFFu);

  /* SB 16: BG0 HUD map (sparse) */
  u16 *map0 = (u16 *)&vram[16 * 2048];
  for (unsigned i = 0; i < 1024; i++) {
    u16 tid = (rand_u32() % 6 == 0) ? (u16)(1u + (rand_u32() & 7u)) : 0u;
    u16 fl = (u16)((rand_u32() & 3u) << 10);
    u16 pal = (u16)((rand_u32() & 3u) << 12);
    map0[i] = eswap16((u16)(tid | fl | pal));
  }
  /* SB 17: BG1 horizon fog map (rows 0..3 solid tile 9, row 4 mixed, rest empty) */
  u16 *map1 = (u16 *)&vram[17 * 2048];
  for (unsigned row = 0; row < 32; row++) {
    for (unsigned col = 0; col < 32; col++) {
      u16 e = 0u;
      if (row < 4) e = (u16)(9u | ((row & 3u) << 12));
      else if (row == 4) e = (u16)((1u + (col & 7u)) | (2u << 12));
      map1[row * 32 + col] = eswap16(e);
    }
  }
  /* SB 24..31: BG2 8bpp affine map (1024x1024 -> 128x128 bytes = 16 KiB) */
  for (unsigned i = 0; i < 16384; i++)
    vram[24 * 2048 + i] = (u8)(rand_u32() & 63u);

  /* Two 4bpp OBJs: one at prio 0 (above BG1) and one at prio 2 (below BG1) */
  oam_ram[0] = eswap16(82u);
  oam_ram[1] = eswap16(60u | 0x4000u); /* 16x16 */
  oam_ram[2] = eswap16(4u | (0u << 10));
  oam_ram[4] = eswap16(90u);
  oam_ram[5] = eswap16(120u | 0x4000u); /* 16x16 */
  oam_ram[6] = eswap16(8u | (2u << 10));
  reg[OAM_UPDATED] = 1;

  auto run_hblank_mode1_case = [&](const char *label, u16 dispcnt, u16 bg2cnt,
                                   u16 bldcnt, u16 bldalpha, u16 mosaic,
                                   bool inject_dy0) -> bool {
    static u16 ref_fb[240 * 160];
    static u16 got_fb[240 * 160];

    auto setup_regs = [&]() {
      write_ioreg(REG_DISPCNT, dispcnt);
      write_ioreg(REG_BG0CNT, 0x1000u); /* CBB0, SB16, prio 0 */
      write_ioreg(REG_BG1CNT, 0x1101u); /* CBB0, SB17, prio 1 */
      write_ioreg(REG_BG2CNT, bg2cnt);  /* CBB1, SB24, prio 3 */
      write_ioreg(REG_BG0HOFS, 0u);
      write_ioreg(REG_BG0VOFS, 0u);
      write_ioreg(REG_BG1HOFS, 0u);
      write_ioreg(REG_BG1VOFS, 0u);
      write_ioreg(REG_BLDCNT, bldcnt);
      write_ioreg(REG_BLDALPHA, bldalpha);
      write_ioreg(REG_MOSAIC, mosaic);
      write_ioreg(REG_WIN0H, 0x10E0u);
      write_ioreg(REG_WIN0V, 0x4098u);
      write_ioreg(REG_WININ, 0x003Fu);
      write_ioreg(REG_WINOUT, 0x0027u);
      write_ioreg(REG_BG2PA, 0x0100u);
      write_ioreg(REG_BG2PB, 0x0000u);
      write_ioreg(REG_BG2PC, 0x0040u);
      write_ioreg(REG_BG2PD, 0x0100u);
      affine_reference_x[0] = 0x00100000;
      affine_reference_y[0] = 0x00080000;
      affine_reference_x[1] = 0;
      affine_reference_y[1] = 0;
    };

    /* Reference: render each scanline immediately before HBlank BG2 write */
    setup_regs();
    memset(ref_fb, 0x5A, sizeof(ref_fb));
    gba_screen_pixels = ref_fb;
    video_invalidate_scanline_cache(0, 160);
    for (u32 y = 74u; y < 160u; y++) {
      write_ioreg(REG_VCOUNT, (u16)y);
      update_scanline();
      /* Simulate HBlank DMA0 updating BG2PA..BG2Y for next scanline */
      s16 pa = (s16)(96 + (int)((y - 74u) * 3u) * ((y & 16u) ? -1 : 1));
      s16 pc = (inject_dy0 && (y == 90u || y == 125u)) ? 0 : (s16)(64 + (int)(y - 74u) * 2);
      write_ioreg(REG_BG2PA, (u16)pa);
      write_ioreg(REG_BG2PC, (u16)pc);
      affine_reference_x[0] = (s32)((y * 0x1357u) & 0x0FFFFFFFu);
      affine_reference_y[0] = (s32)((y * 0x2468u) & 0x0FFFFFFFu);
      if (y == 118u)
        write_ioreg(REG_BG0HOFS, 3u);
    }
    const u16 ref_end_pa = read_ioreg(REG_BG2PA);
    const u16 ref_end_pc = read_ioreg(REG_BG2PC);
    const s32 ref_end_rx = affine_reference_x[0];
    const s32 ref_end_ry = affine_reference_y[0];

    /* Deferred: queue scanlines + snapshot BG2 affine before each HBlank write */
    setup_regs();
    memset(got_fb, 0x5A, sizeof(got_fb));
    gba_screen_pixels = got_fb;
    video_invalidate_scanline_cache(0, 160);
    memset(cgba_video_frame_dirty_row_mask, 0, sizeof(cgba_video_frame_dirty_row_mask));
    for (u32 y = 74u; y < 160u; y++) {
      write_ioreg(REG_VCOUNT, (u16)y);
      cgba_queue_scanlines(y, 1u);
      cgba_snapshot_bg2_affine_pending();
      s16 pa = (s16)(96 + (int)((y - 74u) * 3u) * ((y & 16u) ? -1 : 1));
      s16 pc = (inject_dy0 && (y == 90u || y == 125u)) ? 0 : (s16)(64 + (int)(y - 74u) * 2);
      write_ioreg(REG_BG2PA, (u16)pa);
      write_ioreg(REG_BG2PC, (u16)pc);
      affine_reference_x[0] = (s32)((y * 0x1357u) & 0x0FFFFFFFu);
      affine_reference_y[0] = (s32)((y * 0x2468u) & 0x0FFFFFFFu);
      if (y == 118u) {
        /* Mid-batch non-BG2 write forces flush + live register preservation */
        cgba_flush_pending_scanlines();
        write_ioreg(REG_BG0HOFS, 3u);
      }
    }
    cgba_flush_pending_scanlines();

    if (read_ioreg(REG_BG2PA) != ref_end_pa ||
        read_ioreg(REG_BG2PC) != ref_end_pc ||
        affine_reference_x[0] != ref_end_rx ||
        affine_reference_y[0] != ref_end_ry) {
      fprintf(stderr, "FAIL [%s]: live BG2 affine regs clobbered after flush\n", label);
      return false;
    }
    for (u32 y = 74u; y < 160u; y++) {
      CHECK((cgba_video_frame_dirty_row_mask[y >> 5] & (1u << (y & 31u))) != 0u);
      if (memcmp(got_fb + y * 240u, ref_fb + y * 240u, 240u * sizeof(u16)) != 0) {
        for (u32 x = 0; x < 240u; x++) {
          if (got_fb[y * 240u + x] != ref_fb[y * 240u + x]) {
            fprintf(stderr, "FAIL [%s] at (%u,%u): got=0x%04x ref=0x%04x\n",
                    label, x, y, got_fb[y * 240u + x], ref_fb[y * 240u + x]);
            return false;
          }
        }
      }
    }
    return true;
  };

  /* Wrapped 1024x1024 Mode 1 with evb=16 horizon blend (Mario Kart Scene 1) */
  CHECK(run_hblank_mode1_case("m1-wrap-evb16", 0x1701u, 0xF807u, 0x3442u, 0x1008u, 0u, false));
  /* Same with dy==0 scanlines injected */
  CHECK(run_hblank_mode1_case("m1-wrap-evb16-dy0", 0x1701u, 0xF807u, 0x3442u, 0x1008u, 0u, true));
  /* Non-wrapped BG2 with general eva/evb blend */
  CHECK(run_hblank_mode1_case("m1-nowrap-genblend", 0x1701u, 0xD807u, 0x3442u, 0x0A06u, 0u, false));
  /* Windowed fallback with BG2 snapshots (must fall back and restore live regs) */
  CHECK(run_hblank_mode1_case("m1-win0-fallback", 0x3701u, 0xF807u, 0x3442u, 0x1008u, 0u, false));
  /* Mosaic fallback with BG2 snapshots */
  CHECK(run_hblank_mode1_case("m1-mosaic-fallback", 0x1701u, 0xF847u, 0x3442u, 0x1008u, 0x0022u, false));

  /* Verify cgba_memeq_u32 helper */
  u32 a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  u32 b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(cgba_memeq_u32(a, b, 8u) == true);
  b[0] = 9; CHECK(cgba_memeq_u32(a, b, 8u) == false); b[0] = 1;
  b[7] = 9; CHECK(cgba_memeq_u32(a, b, 8u) == false);

  return true;
}

static bool run_tests(void)
{
  CHECK(test_fast4bpp_standalone());
  CHECK(test_affine_clip_axis());
  CHECK(test_mode1_direct_and_fused_blend());
  CHECK(test_mode1_batch_and_bg2_snaps());

  /* 1. Cold frame: all 160 rows render cleanly. */
  init_scene_mode0();
  CHECK(verify_frame("mode0-cold"));
  CHECK(cgba_video_frame_dirty_rows == 160);

  /* 2. Static frame: 0 dirty rows! */
  CHECK(verify_frame("mode0-static"));
  CHECK(cgba_video_frame_dirty_rows == 0);

  /* 3. Identical palette DMA + identical OAM DMA: STILL 0 dirty rows! */
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-identical-pal-oam-dma"));
  CHECK(cgba_video_frame_dirty_rows == 0);

  /* 4. OBJ-only VRAM write: ONLY the 16 sprite rows (50..65) re-render! */
  vram[0x10000 + 4 * 32 + 5] ^= 0x5Au;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_OBJ;
  CHECK(verify_frame("mode0-obj-vram-only"));
  CHECK(cgba_video_frame_dirty_rows == 16);

  /* 5. OBJ-only palette write: ONLY the 16 sprite rows re-render! */
  palette_ram_converted[256 + 3] ^= 0x1234u;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  CHECK(verify_frame("mode0-obj-pal-only"));
  CHECK(cgba_video_frame_dirty_rows == 16);

  /* 6. Sprite moves from y=50..65 to y=54..69: ONLY 20 rows (50..69) dirty! */
  oam_ram[0] = eswap16(54u);
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-sprite-move"));
  CHECK(cgba_video_frame_dirty_rows == 20);

  /* 7. BG VRAM write: all 160 rows re-render cleanly. */
  vram[0x00080] ^= 0xFFu;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
  CHECK(verify_frame("mode0-bg-vram"));
  CHECK(cgba_video_frame_dirty_rows == 160);

  /* 8. BG scroll change: all 160 rows re-render, then static -> 0! */
  write_ioreg(REG_BG0HOFS, 4);
  CHECK(verify_frame("mode0-scroll"));
  CHECK(cgba_video_frame_dirty_rows == 160);
  CHECK(verify_frame("mode0-scroll-static"));
  CHECK(cgba_video_frame_dirty_rows == 0);

  /* 9. FPS overlay partial invalidation: ONLY rows 0..17 re-render! */
  memset(fb_cached, 0, 18 * 240 * sizeof(u16));
  video_invalidate_scanline_cache(0, 18);
  CHECK(verify_frame("mode0-fps-overlay-heal"));
  CHECK(cgba_video_frame_dirty_rows == 18);

  /* 10. Affine sprite matrix change: invalidates affine sprite rows ONLY! */
  oam_ram[4] = eswap16(100u | 0x0100u);      /* sprite 1: y=100, affine, 16x16 */
  oam_ram[5] = eswap16(80u | 0x4000u | (2u << 9)); /* affine group 2 */
  oam_ram[6] = eswap16(8u | (1u << 10));
  oam_ram[8 * 4 + 3] = eswap16(0x0100u);     /* group 2 PA = 1.0 */
  oam_ram[9 * 4 + 3] = eswap16(0x0000u);     /* group 2 PB = 0.0 */
  oam_ram[10 * 4 + 3] = eswap16(0x0000u);    /* group 2 PC = 0.0 */
  oam_ram[11 * 4 + 3] = eswap16(0x0100u);    /* group 2 PD = 1.0 */
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-affine-sprite-add"));
  CHECK(cgba_video_frame_dirty_rows == 16);
  /* Mutate ONLY group 2 PA (attr3) -> must invalidate rows 100..115! */
  oam_ram[8 * 4 + 3] = eswap16(0x0080u);
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-affine-matrix-update"));
  CHECK(cgba_video_frame_dirty_rows == 16);

  /* 11. Frameskip preserves sticky dirty latches across skipped frame! */
  skip_next_frame = 1;
  vram[0x00090] ^= 0x33u;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
  palette_ram_converted[5] ^= 0x7777u;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  run_cached_frame(); /* skipped */
  skip_next_frame = 0;
  CHECK(verify_frame("mode0-after-frameskip"));
  CHECK(cgba_video_frame_dirty_rows == 160);

  /* 12. Forced blank transitions & steady forced blank! */
  write_ioreg(REG_DISPCNT, read_ioreg(REG_DISPCNT) | 0x0080u);
  CHECK(verify_frame("forced-blank-enter"));
  CHECK(cgba_video_frame_dirty_rows == 160);
  CHECK(verify_frame("forced-blank-steady"));
  CHECK(cgba_video_frame_dirty_rows == 0);
  write_ioreg(REG_DISPCNT, read_ioreg(REG_DISPCNT) & ~0x0080u);
  CHECK(verify_frame("forced-blank-exit"));
  CHECK(cgba_video_frame_dirty_rows == 160);

  /* 13. Mode 3 bitmap extending past 0x10000 (OBJ VRAM dirty bit must dirty BG!) */
  write_ioreg(REG_DISPCNT, 0x0403u); /* Mode 3, BG2 */
  write_ioreg(REG_BG2PA, 0x0100u);
  write_ioreg(REG_BG2PB, 0x0000u);
  write_ioreg(REG_BG2PC, 0x0000u);
  write_ioreg(REG_BG2PD, 0x0100u);
  write_ioreg(REG_BG2X_L, 0);
  write_ioreg(REG_BG2X_H, 0);
  write_ioreg(REG_BG2Y_L, 0);
  write_ioreg(REG_BG2Y_H, 0);
  for (unsigned i = 0; i < 240 * 160 * 2; i++)
    vram[i] = (u8)rand_u32();
  cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;
  CHECK(verify_frame("mode3-cold"));
  CHECK(verify_frame("mode3-static"));
  CHECK(cgba_video_frame_dirty_rows == 0);
  /* Write into bottom of Mode 3 bitmap (>= 0x10000) with only OBJ dirty bit */
  vram[0x11000] ^= 0xFFu;
  vram[0x11001] ^= 0xFFu;
  cgba_vram_dirty = CGBA_VRAM_DIRTY_OBJ;
  CHECK(verify_frame("mode3-high-vram-write"));
  CHECK(cgba_video_frame_dirty_rows == 160);

  /* 14. Active-layer-aware BLDCNT no-op + full-screen WIN1 fast path! */
  init_scene_mode0();
  write_ioreg(REG_DISPCNT, 0x5E00u); /* Mode 0, BG1/2/3+OBJ+WIN1, BG0 OFF */
  write_ioreg(REG_WIN1H, 0x00F0u);
  write_ioreg(REG_WIN1V, 0x00A0u);
  write_ioreg(REG_WININ, 0x3F00u);
  write_ioreg(REG_WINOUT, 0x0000u);
  write_ioreg(REG_BLDALPHA, 0x0A06u);
  write_ioreg(REG_BLDY, 0x0008u);
  write_ioreg(REG_BG1CNT, 0x0900u);
  write_ioreg(REG_BG2CNT, 0x0A01u);
  write_ioreg(REG_BG3CNT, 0x0B02u);
  /* BLDCNT targets disabled BG0 only -> must take fast fullcolor path & match ref. */
  write_ioreg(REG_BLDCNT, 0x3E41u);
  CHECK(verify_frame("mode0-win1-bldcnt-disabled-bg0"));
  CHECK(cgba_mode0_fast_fullframe == true);
  /* BLDCNT targets OBJ only -> fullcolor on non-OBJ rows, blend on OBJ rows. */
  write_ioreg(REG_BLDCNT, 0x3E50u);
  CHECK(verify_frame("mode0-win1-bldcnt-obj-only"));
  CHECK(cgba_mode0_fast_fullframe == false);
  CHECK(cgba_mode0_fast_no_obj == true);
  /* BLDCNT self-only BG1 blend -> no-op, fast path. */
  write_ioreg(REG_BLDCNT, 0x0242u);
  CHECK(verify_frame("mode0-win1-bldcnt-self-bg1"));
  CHECK(cgba_mode0_fast_fullframe == true);
  /* BLDCNT brighten on backdrop (BD=0x20) -> NOT a no-op! */
  write_ioreg(REG_BLDCNT, 0x00A0u);
  CHECK(verify_frame("mode0-win1-bldcnt-brighten-bd"));
  CHECK(cgba_mode0_fast_fullframe == false);
  /* BLDCNT brighten on disabled BG0 only -> no-op! */
  write_ioreg(REG_BLDCNT, 0x0081u);
  CHECK(verify_frame("mode0-win1-bldcnt-brighten-disabled-bg0"));
  CHECK(cgba_mode0_fast_fullframe == true);

  /* 15. Direct-to-RGB565 single-1st-target BG alpha-blend fast path! */
  init_scene_mode0();
  write_ioreg(REG_DISPCNT, 0x5F00u); /* Mode 0, BG0/1/2/3+OBJ+WIN1 */
  write_ioreg(REG_WIN1H, 0x00F0u);
  write_ioreg(REG_WIN1V, 0x00A0u);
  write_ioreg(REG_WININ, 0x3F00u);
  write_ioreg(REG_WINOUT, 0x0020u);
  write_ioreg(REG_BG0CNT, 0x4800u); /* prio 0, 512x256, sb 8, 4bpp */
  write_ioreg(REG_BG1CNT, 0x0901u); /* prio 1, sb 9 */
  write_ioreg(REG_BG2CNT, 0x0A02u); /* prio 2, sb 10 */
  write_ioreg(REG_BG3CNT, 0x0B03u); /* prio 3, sb 11 */
  write_ioreg(REG_BLDCNT, 0x3E41u); /* 1st=BG0, 2nd=BG1|BG2|BG3|OBJ|BD */
  /* Non-saturating across all 8 sub-tile horizontal offsets (0..7) */
  write_ioreg(REG_BLDALPHA, 0x0907u); /* eva=7, evb=9 */
  for (u32 h = 0; h < 8u; h++) {
    write_ioreg(REG_BG0HOFS, (u16)(248u + h));
    write_ioreg(REG_BG0VOFS, (u16)(h * 3u));
    CHECK(verify_frame("mode0-direct-blend-thoff"));
    CHECK(cgba_m0.blend_bg_layer == 0u);
    CHECK(cgba_m0.blend_forbid_omask == 0u);
  }
  /* Saturating blend (eva + evb > 16) */
  write_ioreg(REG_BLDALPHA, 0x0C0Cu); /* eva=12, evb=12 */
  write_ioreg(REG_BG0HOFS, 3u);
  CHECK(verify_frame("mode0-direct-blend-sat"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  /* Invisible overlay (eva=0, evb=16) and opaque overlay (eva=16, evb=0) */
  write_ioreg(REG_BLDALPHA, 0x1000u);
  CHECK(verify_frame("mode0-direct-blend-eva0-evb16"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  write_ioreg(REG_BLDALPHA, 0x0010u);
  CHECK(verify_frame("mode0-direct-blend-eva16-evb0"));
  CHECK(cgba_mode0_fast_fullframe == true);
  /* Blended BG as bottommost layer (prio 3, blending over Backdrop) */
  write_ioreg(REG_BLDALPHA, 0x0808u);
  write_ioreg(REG_BG0CNT, 0x4803u); /* prio 3 */
  write_ioreg(REG_BG1CNT, 0x0902u);
  write_ioreg(REG_BG2CNT, 0x0A01u);
  write_ioreg(REG_BG3CNT, 0x0B00u);
  write_ioreg(REG_BLDCNT, 0x2041u); /* 1st=BG0, 2nd=BD only (no lower BGs!) */
  CHECK(verify_frame("mode0-direct-blend-bottommost-over-bd"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  /* Lower BG NOT in 2nd target -> MUST disqualify direct-blend & match ref */
  write_ioreg(REG_BG0CNT, 0x4800u); /* prio 0 */
  write_ioreg(REG_BG1CNT, 0x0901u);
  write_ioreg(REG_BG2CNT, 0x0A02u);
  write_ioreg(REG_BG3CNT, 0x0B03u);
  write_ioreg(REG_BLDCNT, 0x3A41u); /* BG2 (bit 10) missing from 2nd target! */
  CHECK(verify_frame("mode0-direct-blend-disqualify-lower-bg"));
  CHECK(cgba_m0.blend_bg_layer == 0xFFu);
  /* OBJ missing from 2nd target -> forbid lower OBJ prios (1,2,3 = 0x0E) */
  write_ioreg(REG_BLDCNT, 0x2E41u); /* 2nd=BG1|BG2|BG3|BD, OBJ excluded */
  CHECK(verify_frame("mode0-direct-blend-obj-not-tgt2"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  CHECK(cgba_m0.blend_forbid_omask == 0x0Eu);
  /* OBJ also in 1st target -> forbid all OBJ prios (0x0F) */
  write_ioreg(REG_BLDCNT, 0x3E51u); /* 1st=BG0|OBJ, 2nd=BG1..3|OBJ|BD */
  CHECK(verify_frame("mode0-direct-blend-obj-also-tgt1"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  CHECK(cgba_m0.blend_forbid_omask == 0x0Fu);
  /* Semi-transparent OBJ present on some rows -> exact parity */
  write_ioreg(REG_BLDCNT, 0x3E41u);
  oam_ram[0] = eswap16(54u | 0x0400u); /* semi-transparent mode */
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-direct-blend-with-st-obj"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  /* Palette fade / change while BLDCNT + BLDALPHA stay constant: both BG and
   * OBJ palette mutations must invalidate blend/folded caches cleanly. */
  write_ioreg(REG_BLDALPHA, 0x0907u);
  oam_ram[0] = eswap16(54u);
  reg[OAM_UPDATED] = 1;
  CHECK(verify_frame("mode0-direct-blend-pre-fade"));
  for (unsigned i = 0; i < 512; i++)
    palette_ram_converted[i] = (u16)((palette_ram_converted[i] >> 1) & 0x7BEFu);
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  CHECK(verify_frame("mode0-direct-blend-post-fade"));
  /* Full cache invalidation (e.g. reset / buffer swap) must drop blend cache
   * even if palette_ram_dirty was not set explicitly. */
  for (unsigned i = 0; i < 256; i++)
    palette_ram_converted[i] ^= 0x0841u;
  video_invalidate_scanline_cache(0, 160);
  CHECK(verify_frame("mode0-direct-blend-after-invalidate"));

  /* Saturating alpha blend (eva + evb > 16, as in Zelda ALttP rain/lightning):
   * verify exact pixel parity across normal + saturating coefficients. */
  write_ioreg(REG_BLDCNT, 0x3E41u);
  write_ioreg(REG_BLDALPHA, 0x0A0Au); /* eva=10, evb=10 -> 20 > 16 */
  CHECK(verify_frame("mode0-direct-blend-saturating-10-10"));
  CHECK(cgba_m0.blend_bg_layer == 0u);
  write_ioreg(REG_BLDALPHA, 0x1010u); /* eva=16, evb=16 -> 32 > 16 */
  CHECK(verify_frame("mode0-direct-blend-saturating-16-16"));

  /* Mode 0 brightness darken/brighten plan detection & full-frame + single-window
   * effect-only cutout parity (Zelda ALttP prologue WIN0 shape and variants). */
  write_ioreg(REG_DISPCNT, 0x1F00u); /* BG0..3 + OBJ, no windows */
  write_ioreg(REG_BLDCNT, 0x00FFu);  /* COL_EFFECT_DARK, all 1st targets */
  write_ioreg(REG_BLDY, 5u);
  CHECK(verify_frame("mode0-bright-dark-fullscreen"));
  CHECK(cgba_m0.bright_effect == 3u);
  CHECK(cgba_m0.bright_evy == 5u);
  CHECK(cgba_m0.bright_tgt1_mask == 0x3Fu);
  CHECK(cgba_m0.bright_win_active == 0u);

  write_ioreg(REG_BLDCNT, 0x00BFu);  /* COL_EFFECT_BRIGHT, all 1st targets */
  write_ioreg(REG_BLDY, 7u);
  CHECK(verify_frame("mode0-bright-light-fullscreen"));
  CHECK(cgba_m0.bright_effect == 2u);
  CHECK(cgba_m0.bright_evy == 7u);

  /* WIN0 effect-only cutout: eff_in=0 (effect outside WIN0, as in LEGEND0). */
  write_ioreg(REG_DISPCNT, 0x3F00u); /* WIN0 enabled */
  write_ioreg(REG_WININ, 0x1F1Fu);   /* bit 5 = 0 inside WIN0 */
  write_ioreg(REG_WINOUT, 0x3F3Fu);  /* bit 5 = 1 outside WIN0 */
  write_ioreg(REG_WIN0H, (30u << 8) | 210u);
  write_ioreg(REG_WIN0V, (20u << 8) | 140u);
  write_ioreg(REG_BLDCNT, 0x00FFu);
  write_ioreg(REG_BLDY, 5u);
  CHECK(verify_frame("mode0-bright-dark-win0-eff-out"));
  CHECK(cgba_m0.bright_effect == 3u);
  CHECK(cgba_m0.bright_win_active == 1u);
  CHECK(cgba_m0.bright_win_eff_in == 0u);
  CHECK(cgba_m0.bright_win_x0 == 30u && cgba_m0.bright_win_x1 == 210u);
  CHECK(cgba_m0.bright_win_y0 == 20u && cgba_m0.bright_win_y1 == 140u);

  /* WIN0 effect-only cutout: eff_in=1 (effect inside WIN0) with partial target1. */
  write_ioreg(REG_WININ, 0x3F3Fu);
  write_ioreg(REG_WINOUT, 0x1F1Fu);
  write_ioreg(REG_BLDCNT, 0x00AAu);  /* BRIGHT on BG1|BG3|BD only */
  write_ioreg(REG_BLDY, 9u);
  CHECK(verify_frame("mode0-bright-light-win0-eff-in-partial"));
  CHECK(cgba_m0.bright_effect == 2u);
  CHECK(cgba_m0.bright_win_active == 1u);
  CHECK(cgba_m0.bright_win_eff_in == 1u);
  CHECK(cgba_m0.bright_tgt1_mask == 0x2Au);

  /* WIN0 with differing layer enables inside vs outside MUST disqualify bright fold. */
  write_ioreg(REG_WININ, 0x3E3Eu);   /* BG0 disabled inside WIN0 */
  write_ioreg(REG_WINOUT, 0x1F1Fu);
  CHECK(verify_frame("mode0-bright-win0-layer-mismatch-disqualify"));
  CHECK(cgba_m0.bright_effect == 0u);

  /* Clean up window registers. */
  write_ioreg(REG_DISPCNT, 0x1F00u);
  write_ioreg(REG_BLDCNT, 0u);
  write_ioreg(REG_BLDY, 0u);

  /* --- Tile4 opacity classifier, uniform non-zero empty row skip, and
   * 2-/3-layer aligned opaque merge verification --- */
  {
    /* Set up dedicated tiles in charblock 0:
     *   tile 0x07F: all zero (EMPTY, non-zero tile ID like Zelda HUD)
     *   tile 0x1EC: all zero (EMPTY, dungeon blank tile)
     *   tile 0x080: all non-zero nibbles (OPAQUE)
     *   tile 0x081: opaque except 1 zero pixel (MIXED) — test every row/nibble */
    cgba_tile4_invalidate_all();
    memset(vram + 0x07Fu * 32u, 0, 32u);
    memset(vram + 0x1ECu * 32u, 0, 32u);
    memset(vram + 0x080u * 32u, 0x37u, 32u);
    CHECK(cgba_tile4_classify(0u, 0x07Fu) == CGBA_TILE4_EMPTY);
    CHECK(cgba_tile4_classify(0u, 0x1ECu) == CGBA_TILE4_EMPTY);
    CHECK(cgba_tile4_classify(0u, 0x080u) == CGBA_TILE4_OPAQUE);
    CHECK(cgba_tile4_classify(1536u, 600u) == CGBA_TILE4_MIXED); /* >= 2048 sentinel */

    /* Every single pixel position (64 pixels) becoming 0 must flip OPAQUE -> MIXED. */
    for (u32 px = 0; px < 64u; px++) {
      memset(vram + 0x081u * 32u, 0x5Au, 32u);
      u32 byte_idx = px >> 1;
      u8 mask = (px & 1u) ? 0x0Fu : 0xF0u;
      vram[0x081u * 32u + byte_idx] &= mask;
      cgba_tile4_invalidate_sbd(1u << (0x081u >> 6));
      CHECK(cgba_tile4_classify(0u, 0x081u) == CGBA_TILE4_MIXED);
    }

    /* Non-zero uniform blank row detection + VRAM charblock edit invalidation. */
    u32 sb = 16u; /* VRAM 0x8000 */
    u16 *map = (u16 *)(void *)(vram + sb * 2048u);
    for (u32 i = 0; i < 1024u; i++)
      map[i] = eswap16((u16)((i & 3u) << 12 | 0x07Fu)); /* varied pal, same empty tid */
    /* Put one opaque tile in row 5. */
    map[5u * 32u + 7u] = eswap16(0x0080u);
    cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
    cgba_vram_sb_dirty = (1u << sb);
    cgba_bg_vram_invalidate();
    u32 cbb_mask = 0u;
    u32 em = cgba_sb_compute_empty_rows(sb, 0u, &cbb_mask);
    CHECK(em == (0xFFFFFFFFu & ~(1u << 5)));
    CHECK((cbb_mask & (1u << (0x07Fu >> 6))) != 0u);

    /* Now draw a non-zero pixel into tile 0x07F and invalidate its 2 KiB block:
     * row_all_empty must drop to 0! */
    vram[0x07Fu * 32u + 12u] = 0x04u;
    cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
    cgba_vram_sb_dirty = (1u << (0x07Fu >> 6));
    cgba_bg_vram_invalidate();
    cbb_mask = 0u;
    em = cgba_sb_compute_empty_rows(sb, 0u, &cbb_mask);
    CHECK(em == 0u);
    memset(vram + 0x07Fu * 32u, 0, 32u);
    cgba_tile4_invalidate_all();

    /* Exhaustive 27-case (cls0, cls1, cls2) truth-table test for try_merge_2/3,
     * covering both same_h and mismatched seg1, thoff=0 (30) and thoff!=0 (31),
     * with HFLIP/VFLIP/palette bits preserved. */
    u16 tids[3] = { 0x1ECu, 0x080u, 0x081u }; /* EMPTY, OPAQUE, MIXED */
    u16 m0[32], m1[32], m2[32], out[32];
    for (u32 c0 = 0; c0 < 3u; c0++) {
      for (u32 c1 = 0; c1 < 3u; c1++) {
        u16 r0 = eswap16((u16)(0x1400u | tids[c0]));
        u16 r1 = eswap16((u16)(0x2800u | tids[c1]));
        for (u32 s = 0; s < 31u; s++) { m0[s] = r0; m1[s] = r1; }
        const u16 *am1_2[2] = { m0, m1 };
        const u16 *am2_2[2] = { m0 + 17, m1 + 17 };
        u32 seg_same[2] = { 17u, 17u };
        u32 seg_diff[2] = { 17u, 11u };
        const u16 *am2_2d[2] = { m0 + 17, m1 + 11 };
        bool exp2 = !(c1 == 2u && c0 != 0u);
        u16 exp_tile2 = (c1 == 1u || (c1 == 2u && c0 == 0u)) ? r1 : r0;
        CHECK(cgba_mode0_try_merge_2(out, 30u, 0u, am1_2, am2_2, seg_same) == exp2);
        if (exp2) {
          for (u32 s = 0; s < 30u; s++) CHECK(out[s] == exp_tile2);
        }
        CHECK(cgba_mode0_try_merge_2(out, 31u, 0u, am1_2, am2_2d, seg_diff) == exp2);
        if (exp2) {
          for (u32 s = 0; s < 31u; s++) CHECK(out[s] == exp_tile2);
        }

        for (u32 c2 = 0; c2 < 3u; c2++) {
          u16 r2 = eswap16((u16)(0x3C00u | tids[c2]));
          for (u32 s = 0; s < 31u; s++) m2[s] = r2;
          const u16 *am1_3[3] = { m0, m1, m2 };
          const u16 *am2_3[3] = { m0 + 19, m1 + 19, m2 + 19 };
          u32 seg3_same[3] = { 19u, 19u, 19u };
          const u16 *am2_3d[3] = { m0 + 19, m1 + 11, m2 + 23 };
          u32 seg3_diff[3] = { 19u, 11u, 23u };
          bool exp3;
          u16 exp_tile3 = r0;
          if (c2 == 1u) {
            exp3 = true; exp_tile3 = r2;
          } else if (c2 == 0u) {
            exp3 = exp2; exp_tile3 = exp_tile2;
          } else {
            exp3 = (c1 == 0u && c0 == 0u); exp_tile3 = r2;
          }
          CHECK(cgba_mode0_try_merge_3(out, 30u, 0u, am1_3, am2_3, seg3_same) == exp3);
          if (exp3) {
            for (u32 s = 0; s < 30u; s++) CHECK(out[s] == exp_tile3);
          }
          CHECK(cgba_mode0_try_merge_3(out, 31u, 0u, am1_3, am2_3d, seg3_diff) == exp3);
          if (exp3) {
            for (u32 s = 0; s < 31u; s++) CHECK(out[s] == exp_tile3);
          }
        }
      }
    }
  }

  /* ======================================================================
   * Phase 3A/3B/3C/3D differential tests: Mode 1/2 affine plans & dirty skip,
   * direct RGB565 OBJ_MOD_SEMITRAN blend (normal/clipped/sat/overlap/tgt2),
   * and WIN0/WIN1 differing-layer-mask multi-span windowing.
   * ====================================================================== */
  {
    /* Helper to reset IO and fill randomized BG/OBJ VRAM & palettes. */
    auto init_scene = [&]() {
      memset(io_registers, 0, sizeof(io_registers));
      memset(oam_ram, 0, sizeof(oam_ram));
      for (u32 i = 0; i < 128; i++)
        oam_ram[i * 4] = eswap16(0x0200u); /* disable all sprites */
      for (u32 i = 0; i < 512; i++)
        palette_ram_converted[i] = (u16)(rand_u32() & 0xFFFFu);
      for (u32 i = 0; i < sizeof(vram); i++) {
        u32 r = rand_u32();
        vram[i] = (r & 3u) ? (u8)(r >> 8) : 0u; /* ~25% zero bytes for transparency */
      }
      /* Zero tile 0 in charblock 0 to exercise empty-tile skips too. */
      memset(vram, 0, 64u);
      palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
      cgba_vram_dirty = CGBA_VRAM_DIRTY_ALL;
      cgba_vram_sb_dirty = 0;
      cgba_video_io_dirty = 1;
      reg[OAM_UPDATED] = 1;
      video_invalidate_scanline_cache(0, 160);
    };

    /* 1. Mode 1 (BG0/BG1 4bpp text + BG2 affine) & Mode 2 (BG2/BG3 affine):
     *    scale (dy==0) & rotate (dy!=0), wrap & nowrap, negative dx/dy,
     *    and steady-frame dirty-row skip preserving affine_reference_x/y. */
    init_scene();
    for (u32 trial = 0; trial < 24u; trial++) {
      u32 mode = (trial < 14u) ? 1u : 2u;
      bool wrap = (trial & 1u) != 0u;
      bool rot = (trial & 2u) != 0u;
      bool mos = (trial % 7u) == 0u;
      u32 map_sz = trial & 3u;
      u16 dispcnt = (u16)(mode | ((mode == 1u) ? 0x1700u : 0x1C00u) | 0x0040u);
      write_ioreg(REG_DISPCNT, dispcnt);
      write_ioreg(REG_BG0CNT, (u16)(0x0800u | (trial & 3u)));
      write_ioreg(REG_BG1CNT, (u16)(0x0900u | ((trial + 1u) & 3u)));
      write_ioreg(REG_BG2CNT, (u16)((map_sz << 14) | (wrap ? 0x2000u : 0u) |
                                    0x0A00u | (mos ? 0x0040u : 0u) | ((trial + 2u) & 3u)));
      write_ioreg(REG_BG3CNT, (u16)((((map_sz + 1u) & 3u) << 14) | (!wrap ? 0x2000u : 0u) |
                                    0x0C00u | ((trial + 3u) & 3u)));
      write_ioreg(REG_BG0HOFS, (u16)(trial * 13u));
      write_ioreg(REG_BG0VOFS, (u16)(trial * 7u));
      write_ioreg(REG_BG1HOFS, (u16)(trial * 5u));
      write_ioreg(REG_BG1VOFS, (u16)(trial * 11u));
      write_ioreg(REG_MOSAIC, mos ? 0x0023u : 0x0000u);
      s16 pa = (trial & 4u) ? (s16)-320 : (s16)(192 + (trial * 29u & 0xFFu));
      s16 pb = rot ? (s16)(64 - (int)(trial * 17u & 0x7Fu)) : (s16)0;
      s16 pc = rot ? (s16)(-48 + (int)(trial * 19u & 0x7Fu)) : (s16)0;
      s16 pd = (s16)(256 + (int)(trial * 13u & 0x7Fu));
      write_ioreg(REG_BG2PA, (u16)pa); write_ioreg(REG_BG2PB, (u16)pb);
      write_ioreg(REG_BG2PC, (u16)pc); write_ioreg(REG_BG2PD, (u16)pd);
      write_ioreg(REG_BG3PA, (u16)(pa >> 1)); write_ioreg(REG_BG3PB, (u16)-pb);
      write_ioreg(REG_BG3PC, (u16)-pc); write_ioreg(REG_BG3PD, (u16)pd);
      s32 rx0 = ((s32)(trial & 1u ? -20 : 12)) << 8;
      s32 ry0 = ((s32)(trial & 2u ? -10 : 8)) << 8;
      write_ioreg(REG_BG2X_L, (u16)(rx0 & 0xFFFF)); write_ioreg(REG_BG2X_H, (u16)(rx0 >> 16));
      write_ioreg(REG_BG2Y_L, (u16)(ry0 & 0xFFFF)); write_ioreg(REG_BG2Y_H, (u16)(ry0 >> 16));
      write_ioreg(REG_BG3X_L, (u16)(rx0 & 0xFFFF)); write_ioreg(REG_BG3X_H, (u16)(rx0 >> 16));
      write_ioreg(REG_BG3Y_L, (u16)(ry0 & 0xFFFF)); write_ioreg(REG_BG3Y_H, (u16)(ry0 >> 16));
      cgba_video_io_dirty = 1;
      CHECK(verify_frame("phase3_mode12_affine"));
      /* Verify dirty-row mask recorded all 160 rows on dirty frame. */
      CHECK(cgba_video_frame_dirty_rows == 160u);
      CHECK(cgba_video_frame_dirty_row_mask[0] == 0xFFFFFFFFu &&
            cgba_video_frame_dirty_row_mask[4] == 0xFFFFFFFFu);
      /* Second identical frame must skip all 160 rows cleanly! */
      CHECK(verify_frame("phase3_mode12_affine_steady"));
      CHECK(cgba_video_frame_dirty_rows == 0u);
      CHECK((cgba_video_frame_dirty_row_mask[0] | cgba_video_frame_dirty_row_mask[1] |
             cgba_video_frame_dirty_row_mask[2] | cgba_video_frame_dirty_row_mask[3] |
             cgba_video_frame_dirty_row_mask[4]) == 0u);
    }

    /* 2. Phase 3C: Direct RGB565 OBJ_MOD_SEMITRAN blend vs STCKCOLOR reference. */
    init_scene();
    write_ioreg(REG_DISPCNT, 0x1340u); /* Mode 0, BG0 + BG1 + OBJ, 1D OBJ */
    write_ioreg(REG_BG0CNT, 0x0801u);  /* prio 1 */
    write_ioreg(REG_BG1CNT, 0x0902u);  /* prio 2 */
    /* BLDCNT: no BG effect (mode 0), 2nd targets = BG0 + BG1 + Backdrop (0x2300) */
    write_ioreg(REG_BLDCNT, 0x2300u);

    for (u32 c = 0; c < 12u; c++) {
      for (u32 i = 0; i < 128; i++)
        oam_ram[i * 4] = eswap16(0x0200u);
      u32 eva = (c & 1u) ? 12u : 7u;
      u32 evb = (c & 2u) ? 10u : 9u; /* c&3==3 tests saturating eva+evb=22 > 16 */
      write_ioreg(REG_BLDALPHA, (u16)(eva | (evb << 8)));
      /* Place non-overlapping 4bpp SEMITRAN OBJs (normal, hflip, vflip, clipped). */
      s32 xs[3] = { (c == 4u) ? -4 : 16, 100, (c == 5u) ? 232 : 180 };
      for (u32 k = 0; k < 3u; k++) {
        u16 a0 = (u16)(((20u + k * 24u) & 0xFFu) | 0x0400u); /* OBJ_MOD_SEMITRAN, 4bpp, square */
        u16 a1 = (u16)((xs[k] & 0x1FFu) | 0x4000u |           /* 16x16 */
                       ((k == 1u) ? 0x1000u : 0u) |            /* hflip */
                       ((k == 2u) ? 0x2000u : 0u));            /* vflip */
        u16 a2 = (u16)((k * 16u) | (1u << 10) | ((k + 1u) << 12)); /* prio 1 */
        oam_ram[k * 4 + 0] = eswap16(a0);
        oam_ram[k * 4 + 1] = eswap16(a1);
        oam_ram[k * 4 + 2] = eswap16(a2);
      }
      if (c == 6u) {
        /* Add an overlapping normal OBJ underneath sprite 0 -> must trigger fallback. */
        oam_ram[10 * 4 + 0] = eswap16(20u);
        oam_ram[10 * 4 + 1] = eswap16(20u | 0x4000u);
        oam_ram[10 * 4 + 2] = eswap16(0x0040u | (1u << 10));
      } else if (c == 7u) {
        /* Add an overlapping normal OBJ ABOVE (lower OAM idx) sprite 0 -> still direct-safe! */
        oam_ram[0 * 4 + 0] = eswap16(20u);
        oam_ram[0 * 4 + 1] = eswap16(20u | 0x4000u);
        oam_ram[0 * 4 + 2] = eswap16(0x0040u | (0u << 10));
        oam_ram[1 * 4 + 0] = eswap16(20u | 0x0400u);
        oam_ram[1 * 4 + 1] = eswap16(16u | 0x4000u);
        oam_ram[1 * 4 + 2] = eswap16(0x0010u | (1u << 10));
      } else if (c == 8u) {
        /* Remove BG1 from 2nd targets -> priority 1 must NOT use direct alpha. */
        write_ioreg(REG_BLDCNT, 0x2100u);
      } else {
        write_ioreg(REG_BLDCNT, 0x2300u);
      }
      reg[OAM_UPDATED] = 1;
      cgba_video_io_dirty = 1;
      CHECK(verify_frame("phase3_semitran_obj"));
      if (c < 6u) {
        CHECK(cgba_m0.obj_alpha_direct_pmask != 0u);
        CHECK(obj_alpha_complex[24] == 0u);
      } else if (c == 6u) {
        CHECK(obj_alpha_complex[24] != 0u);
      } else if (c == 8u) {
        CHECK((cgba_m0.obj_alpha_direct_pmask & (1u << 1)) == 0u);
      }
    }

    /* 3. Phase 3D: WIN0/WIN1 differing layer masks (normal, inverted, overlapping,
     *    and mixed effect/non-effect spans) in Mode 0 and Mode 1. */
    init_scene();
    for (u32 w = 0; w < 12u; w++) {
      u32 mode = (w >= 8u) ? 1u : 0u;
      u32 wctrl = 1u + (w % 3u); /* 1=WIN0, 2=WIN1, 3=WIN0+WIN1 */
      write_ioreg(REG_DISPCNT, (u16)(mode | (wctrl << 13) | 0x1740u));
      write_ioreg(REG_BG0CNT, 0x0800u);
      write_ioreg(REG_BG1CNT, 0x0901u);
      write_ioreg(REG_BG2CNT, (mode == 1u) ? 0x6A02u : 0x0A02u);
      write_ioreg(REG_BG0HOFS, (u16)(w * 9u));
      write_ioreg(REG_BG1HOFS, (u16)(w * 7u));
      write_ioreg(REG_BG2PA, 0x0120u); write_ioreg(REG_BG2PB, 0x0010u);
      write_ioreg(REG_BG2PC, 0xFFE8u); write_ioreg(REG_BG2PD, 0x0110u);
      if (w & 1u) {
        /* Normal windows */
        write_ioreg(REG_WIN0H, (u16)((20u << 8) | 200u));
        write_ioreg(REG_WIN0V, (u16)((16u << 8) | 120u));
        write_ioreg(REG_WIN1H, (u16)((80u << 8) | 232u));
        write_ioreg(REG_WIN1V, (u16)((40u << 8) | 150u));
      } else {
        /* Inverted (l > r and/or top > bot) windows */
        write_ioreg(REG_WIN0H, (u16)((180u << 8) | 40u));
        write_ioreg(REG_WIN0V, (u16)((120u << 8) | 30u));
        write_ioreg(REG_WIN1H, (u16)((210u << 8) | 70u));
        write_ioreg(REG_WIN1V, (u16)((20u << 8) | 140u));
      }
      /* Differing layer masks in WIN0, WIN1, WINOUT + mixed color effects. */
      write_ioreg(REG_WININ, (u16)(0x15u | (0x33u << 8)));
      write_ioreg(REG_WINOUT, 0x26u);
      write_ioreg(REG_BLDCNT, (w & 2u) ? 0x00EFu : 0x0000u);
      write_ioreg(REG_BLDY, 0x0008u);
      cgba_video_io_dirty = 1;
      CHECK(verify_frame("phase3_win_multispan"));
      CHECK(cgba_m0.win_multi_active != 0u);
    }

    /* 4. 8-bpp blend_bg direct RGB565 fast path (both !base_done backdrop-folded
     *    LUT via empty lower BG skip AND base_done direct blend over opaque BG0),
     *    aligned & unaligned HOFS (0..7 + 512-wide seam), hflip/vflip, sat/nonsat,
     *    and palette-write cache invalidation. */
    init_scene();
    /* Populate 8-bpp tiles in charblock 1 (vram[0x4000..0x8000]) with transparent
     * (0) and non-zero indices 1..255, plus 4-bpp tiles in charblock 0. */
    for (u32 t = 0; t < 32u; t++) {
      u8 *tp = &vram[0x4000u + t * 64u];
      for (u32 i = 0; i < 64u; i++) {
        if (t == 0u)
          tp[i] = 0u; /* all-zero transparent tile */
        else if (((t + i) & 7u) == 0u)
          tp[i] = 0u; /* mixed transparent pixels */
        else
          tp[i] = (u8)(1u + ((t * 17u + i * 5u) % 255u));
      }
    }
    /* BG0 (sb=28): 100% empty except tile rows 4 and 15 (exercises BOTH !base_done
     * and base_done strips inside the same frame!). */
    memset(&vram[28u * 2048u], 0, 2048u);
    for (u32 tx = 0; tx < 32u; tx++) {
      ((u16 *)&vram[28u * 2048u])[4u * 32u + tx] = eswap16((u16)(1u + (tx & 7u)));
      ((u16 *)&vram[28u * 2048u])[15u * 32u + tx] = eswap16((u16)(2u + (tx & 5u)));
    }
    /* BG1 (sb=29): 100% empty 4-bpp layer. */
    memset(&vram[29u * 2048u], 0, 2048u);
    /* BG2 (sb=30..31, 512x256 wide 8-bpp layer) with normal, hflip, vflip, hvflip. */
    for (u32 sb = 30u; sb <= 31u; sb++) {
      u16 *mp = (u16 *)&vram[sb * 2048u];
      for (u32 i = 0; i < 1024u; i++) {
        u16 tid = (u16)((i + sb * 7u) & 31u);
        u16 fl = (u16)(((i & 3u) == 1u ? 0x0400u : 0u) |
                       ((i & 3u) == 2u ? 0x0800u : 0u) |
                       ((i & 3u) == 3u ? 0x0C00u : 0u));
        mp[i] = eswap16((u16)(tid | fl));
      }
    }
    /* BG3 (sb=27): 100% empty 4-bpp layer above BG2. */
    memset(&vram[27u * 2048u], 0, 2048u);
    /* Ensure tile 0 in charblock 0 is all zeros so BG0/BG1/BG3 empty rows match. */
    memset(&vram[0], 0, 32u);
    cgba_vram_sb_dirty = 0xFFFFFFFFu;

    write_ioreg(REG_DISPCNT, 0x1F40u); /* Mode 0, BG0+BG1+BG2+BG3+OBJ, 1D OBJ */
    write_ioreg(REG_BG0CNT, 0x1C03u);  /* prio 3, 4bpp, cbb=0, sb=28 */
    write_ioreg(REG_BG1CNT, 0x1D02u);  /* prio 2, 4bpp, cbb=0, sb=29 */
    write_ioreg(REG_BG2CNT, 0x5E85u);  /* prio 1, 8bpp, cbb=1, sb=30, 512x256 */
    write_ioreg(REG_BG3CNT, 0x1B00u);  /* prio 0, 4bpp, cbb=0, sb=27 */
    write_ioreg(REG_BLDCNT, 0x3F44u);  /* BLEND: 1st=BG2, 2nd=ALL */

    static const u16 test_hofs[10] = { 0u, 192u, 1u, 2u, 3u, 5u, 7u, 249u, 255u, 487u };
    for (u32 t = 0; t < 10u; t++) {
      write_ioreg(REG_BG0HOFS, (u16)(t * 3u));
      write_ioreg(REG_BG0VOFS, (u16)(157u + t));
      write_ioreg(REG_BG2HOFS, test_hofs[t]);
      write_ioreg(REG_BG2VOFS, (u16)(157u + t * 5u));
      u32 eva = (t == 8u) ? 0u : ((t & 1u) ? 12u : 15u);
      u32 evb = (t == 8u) ? 16u : ((t & 1u) ? 10u : 1u);
      write_ioreg(REG_BLDALPHA, (u16)(eva | (evb << 8)));
      if (t == 5u) {
        /* Mutate palette mid-sequence (both backdrop and entry 42) to verify
         * cgba_bg_blend_bd_pal invalidation. */
        palette_ram_converted[0] = 0x7C1Fu;
        palette_ram_converted[42] = 0x03E0u;
        cgba_blend_caches_invalidate();
      }
      cgba_video_io_dirty = 1;
      CHECK(verify_frame("blend8bpp_pop_fastpath"));
      CHECK(cgba_m0.blend_bg_layer == 2u);
      CHECK((cgba_bg_plans[2].flags & 8u) != 0u);
    }
    /* Also verify non-blend 8-bpp base fast path (BLDCNT=0). */
    write_ioreg(REG_BLDCNT, 0x0000u);
    write_ioreg(REG_DISPCNT, 0x0440u); /* BG2 (8-bpp) only */
    write_ioreg(REG_BG2HOFS, 195u);
    cgba_video_io_dirty = 1;
    CHECK(verify_frame("nonblend_8bpp_base"));

    /* 5. Verify CGBA_PALETTE_DIRTY_OBJ_ONLY refreshes folded OBJ palette
     *    without stale entries when an OBJ-only palette write occurs during
     *    Mode 0 brightness/blend. */
    init_scene();
    write_ioreg(REG_DISPCNT, 0x1140u); /* Mode 0, BG0 + OBJ */
    write_ioreg(REG_BG0CNT, 0x1C01u);  /* prio 1, 4bpp */
    write_ioreg(REG_BLDCNT, 0x0090u);  /* BRIGHTEN OBJ (1st=OBJ, mode=2) */
    write_ioreg(REG_BLDY, 8u);
    u16 *oam16 = (u16 *)oam_ram;
    oam16[0] = eswap16(20u);           /* y=20, normal, 4bpp, square */
    oam16[1] = eswap16(20u);           /* x=20, 8x8 */
    oam16[2] = eswap16(1u);            /* tile 1, prio 0, pal 0 */
    memset(&vram[0x10020u], 0x11, 32u);
    reg[OAM_UPDATED] = 1u;
    cgba_vram_sb_dirty = 0xFFFFFFFFu;
    cgba_video_io_dirty = 1u;
    CHECK(verify_frame("obj_only_folded_pal_init"));

    /* Mutate ONLY OBJ palette entry 1 and mark CGBA_PALETTE_DIRTY_OBJ_ONLY
     * (exact bits written by gba_memory.c / sh4_fastmem.h for offset >= 0x200). */
    palette_ram_converted[256u + 1u] = 0x7C1Fu;
    palette_ram_dirty |= CGBA_PALETTE_DIRTY_OBJ_ONLY;
    CHECK(verify_frame("obj_only_folded_pal_bright"));

    /* Same check under Mode 0 folded blend (1st=OBJ, 2nd=BG0). */
    write_ioreg(REG_BLDCNT, 0x0150u);  /* BLEND: 1st=OBJ, 2nd=BG0 */
    write_ioreg(REG_BLDALPHA, 0x0808u);
    cgba_video_io_dirty = 1u;
    CHECK(verify_frame("obj_only_folded_pal_blend_init"));
    palette_ram_converted[256u + 1u] = 0x03E0u;
    palette_ram_dirty |= CGBA_PALETTE_DIRTY_OBJ_ONLY;
    CHECK(verify_frame("obj_only_folded_pal_blend_update"));

    /* 7. Mode 1 direct affine BG2 alpha-blend (AW2 Mission Start scene pattern):
     *    BG1 (prio 3, 4bpp text) 100% opaque covering backdrop, BG2 (prio 2,
     *    8bpp affine, rotating), BG0 (prio 0, 4bpp text), OBJ (prio 0),
     *    BLDCNT = 1st=BG2, 2nd=BG1 (backdrop NOT in 2nd target). */
    init_scene();
    write_ioreg(REG_DISPCNT, 0x1741u); /* Mode 1, BG0 + BG1 + BG2 + OBJ */
    write_ioreg(REG_BG0CNT, 0x1C00u);  /* prio 0, cbb 0, sbb 28, 4bpp */
    write_ioreg(REG_BG1CNT, 0x1E03u);  /* prio 3, cbb 0, sbb 30, 4bpp */
    write_ioreg(REG_BG2CNT, 0x4F8Au);  /* prio 2, cbb 2, sbb 15, 256x256, wrap=0 */
    write_ioreg(REG_BG1HOFS, 13u);
    write_ioreg(REG_BG1VOFS, 29u);
    /* Populate BG1 (sbb 30, cbb 0) with 100% opaque 4bpp tiles using subpals 1,2. */
    memset(&vram[0x0020u], 0x23, 32u); /* tile 1: all non-zero nibbles */
    memset(&vram[0x0040u], 0x45, 32u); /* tile 2: all non-zero nibbles */
    {
      u16 *m1 = (u16 *)&vram[30u * 2048u];
      for (u32 i = 0; i < 1024u; i++) {
        u16 sub = (u16)(((i & 1u) ? 2u : 1u) << 12);
        m1[i] = eswap16((u16)(sub | ((i & 3u) ? 1u : 2u)));
      }
      /* BG2 (cbb 2, sbb 15): mix of empty, solid, opaque textured, and mixed tiles */
      memset(&vram[2u * 16384u], 0, 16384u);
      memset(&vram[2u * 16384u + 1u * 64u], 5, 64u); /* tile 1: solid */
      for (u32 k = 0u; k < 64u; k++) {
        vram[2u * 16384u + 2u * 64u + k] = (u8)((k & 7u) ? (1u + (k % 12u)) : 0u); /* mixed */
        vram[2u * 16384u + 3u * 64u + k] = (u8)(1u + (k % 12u)); /* opaque textured */
      }
      u8 *m2 = &vram[15u * 2048u];
      memset(m2, 0, 1024u);
      for (u32 ty = 2u; ty < 18u; ty++) {
        for (u32 tx = 3u; tx < 19u; tx++)
          m2[ty * 32u + tx] = (u8)(((tx + ty) % 4u));
      }
    }
    /* Non-saturating dark BG1 subpalettes + red BG2 palette */
    for (u32 i = 1u; i < 16u; i++) {
      palette_ram_converted[16u + i] = 0x3908u; /* low RGB */
      palette_ram_converted[32u + i] = 0x4208u;
      palette_ram_converted[i] = (u16)((((i * 2u) & 31u) << 11) | (i << 5) | (i & 7u));
    }
    cgba_blend_caches_invalidate();
    cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
    cgba_vram_sb_dirty = 0xFFFFFFFFu;
    cgba_video_io_dirty = 1u;

    static const s16 m1_rot[6][4] = {
      {  256,    0,    0,  256 }, /* 0 deg */
      {  181, -181,  181,  181 }, /* 45 deg */
      {    0, -256,  256,    0 }, /* 90 deg */
      { -181, -181,  181, -181 }, /* 135 deg */
      { -256,    0,    0, -256 }, /* 180 deg */
      {  312,  140, -140,  312 }, /* scaled + rotated */
    };
    static const u16 m1_alphas[5] = { 0x1008u, 0x1010u, 0x0808u, 0x1000u, 0x0010u };
    for (u32 ai = 0; ai < 5u; ai++) {
      write_ioreg(REG_BLDCNT, 0x0244u); /* 1st=BG2, 2nd=BG1 (no backdrop) */
      write_ioreg(REG_BLDALPHA, m1_alphas[ai]);
      for (u32 ri = 0; ri < 6u; ri++) {
        write_ioreg(REG_BG2PA, (u16)m1_rot[ri][0]);
        write_ioreg(REG_BG2PB, (u16)m1_rot[ri][1]);
        write_ioreg(REG_BG2PC, (u16)m1_rot[ri][2]);
        write_ioreg(REG_BG2PD, (u16)m1_rot[ri][3]);
        write_ioreg(REG_BG2X_L, 0x0000u);
        write_ioreg(REG_BG2X_H, 0x0040u);
        write_ioreg(REG_BG2Y_L, 0x0000u);
        write_ioreg(REG_BG2Y_H, 0x0030u);
        affine_reference_x[0] = 64 << 8;
        affine_reference_y[0] = 48 << 8;
        write_ioreg(REG_BG2CNT, (u16)(0x4F8Au | ((ri & 1u) ? 0x2000u : 0u)));
        cgba_video_io_dirty = 1u;
        CHECK(verify_frame("mode1_affine_bg2_blend"));
      }
    }
    /* Force RGB565 channel saturation (bright BG1 + bright BG2, eva=12, evb=16). */
    for (u32 i = 1u; i < 16u; i++) {
      palette_ram_converted[16u + i] = 0xFFFFu;
      palette_ram_converted[i] = 0xFFFFu;
    }
    write_ioreg(REG_BLDCNT, 0x0244u);
    write_ioreg(REG_BLDALPHA, 0x100Cu);
    write_ioreg(REG_BG2CNT, 0x4F8Au);
    write_ioreg(REG_BG2PA, (u16)181);
    write_ioreg(REG_BG2PB, (u16)-181);
    write_ioreg(REG_BG2PC, (u16)181);
    write_ioreg(REG_BG2PD, (u16)181);
    affine_reference_x[0] = 64 << 8;
    affine_reference_y[0] = 48 << 8;
    cgba_blend_caches_invalidate();
    cgba_video_io_dirty = 1u;
    CHECK(verify_frame("mode1_affine_bg2_blend_saturate"));

    /* Non-opaque BG1 with backdrop NOT in 2nd target must NOT falsely blend
     * BG2 against backdrop (must match reference pixel-for-pixel). */
    vram[0x0020u] = 0x00u; /* introduce transparent pixel into BG1 tile 1 */
    cgba_vram_dirty = CGBA_VRAM_DIRTY_BG;
    cgba_vram_sb_dirty = 0xFFFFFFFFu;
    cgba_video_io_dirty = 1u;
    CHECK(verify_frame("mode1_affine_bg2_blend_nonopaque_bg1_fallback"));

    /* Mode 2 (single affine BG2 with BG3 disabled) alpha-blend over backdrop. */
    write_ioreg(REG_DISPCNT, 0x1442u); /* Mode 2, BG2 + OBJ (BG3 off) */
    write_ioreg(REG_BLDCNT, 0x2044u);  /* 1st=BG2, 2nd=backdrop */
    write_ioreg(REG_BLDALPHA, 0x1008u);
    write_ioreg(REG_BG2CNT, 0x4F8Au);
    affine_reference_x[0] = 64 << 8;
    affine_reference_y[0] = 48 << 8;
    cgba_blend_caches_invalidate();
    cgba_video_io_dirty = 1u;
    CHECK(verify_frame("mode2_affine_bg2_blend_backdrop"));
  }

  /* 6. Mode 4 direct 16-bit OBJ_BLEND fast path vs STCKCOLOR + merge_blend<OBJ_BLEND, true>:
   *    verify bit-exact parity across opaque-before-ST, ST-before-opaque,
   *    multiple overlapping ST OBJs, 8-bpp ST OBJ, odd-aligned window clips,
   *    saturating & non-saturating BLDALPHA, and zero st_buf residue across lines. */
  {
    for (u32 i = 0; i < 512u; i++)
      palette_ram_converted[i] = (u16)((i * 251u + 0x1234u) & 0xFFFFu);
    palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    for (u32 y = 0; y < 160u; y++)
      for (u32 x = 0; x < 240u; x++) {
        vram[y * 240u + x] = (u8)((x * 7u + y * 13u) & 0xFFu);
        vram[0xA000u + y * 240u + x] = (u8)((x * 11u + y * 5u + 3u) & 0xFFu);
      }
    for (u32 t = 0; t < 16u; t++) {
      for (u32 b = 0; b < 32u; b++) {
        u8 lo = (u8)((t + b) & 0x0Fu);
        u8 hi = (u8)((t * 3u + b + 1u) & 0x0Fu);
        /* Bitmap modes (3-5) only allow OBJ tile IDs >= 512 (0x14000..0x17FFF). */
        vram[0x14000u + t * 32u + b] = (u8)((hi << 4) | lo);
      }
    }
    memset(oam_ram, 0, sizeof(oam_ram));
    for (u32 i = 0; i < 128u; i++)
      oam_ram[i * 4u] = eswap16(0x0200u);

    /* OAM 0: opaque 4bpp 16x16 at (30, 14) -> drawn AFTER ST OBJs (y=14..29) */
    oam_ram[0 * 4u + 0] = eswap16(14u | 0x0000u);
    oam_ram[0 * 4u + 1] = eswap16(30u | 0x4000u);
    oam_ram[0 * 4u + 2] = eswap16(0x201u | (1u << 12));

    /* OAM 1: ST 4bpp 32x32 at (20, 8) -> overlaps OAM 2 on y=10..25 with NO prior
     * opaque OBJ on y=10..13 (directly exercises st_rem > 1 bg_save snapshot). */
    oam_ram[1 * 4u + 0] = eswap16(8u | 0x0400u);
    oam_ram[1 * 4u + 1] = eswap16(20u | 0x8000u);
    oam_ram[1 * 4u + 2] = eswap16(0x202u | (2u << 12));

    /* OAM 2: ST 8bpp 16x16 at (28, 10) -> overlapping 2nd ST OBJ */
    oam_ram[2 * 4u + 0] = eswap16(10u | 0x2400u);
    oam_ram[2 * 4u + 1] = eswap16(28u | 0x4000u);
    oam_ram[2 * 4u + 2] = eswap16(0x204u);

    /* OAM 3: opaque 4bpp 16x16 at (24, 16) -> drawn BEFORE ST OBJs (y=16..31) */
    oam_ram[3 * 4u + 0] = eswap16(16u | 0x0000u);
    oam_ram[3 * 4u + 1] = eswap16(24u | 0x4000u);
    oam_ram[3 * 4u + 2] = eswap16(0x203u | (3u << 12));

    reg[OAM_UPDATED] = 1u;
    write_ioreg(REG_BG2CNT, 0x0000u);
    write_ioreg(REG_BG2PA, 0x0100u);
    write_ioreg(REG_BG2PB, 0x0000u);
    write_ioreg(REG_BG2PC, 0x0000u);
    write_ioreg(REG_BG2PD, 0x0100u);
    write_ioreg(REG_BLDCNT, 0x3F3Fu);

    static const u16 alphas[2] = { 0x0D0Fu, 0x0709u };
    static const u16 dispcnts[2] = { 0x1F44u, 0x1F54u };
    static const u32 spans[2][2] = { { 0u, 240u }, { 21u, 197u } };

    for (u32 ai = 0; ai < 2u; ai++) {
      write_ioreg(REG_BLDALPHA, alphas[ai]);
      write_ioreg(REG_DISPCNT, dispcnts[ai]);
      reg[OAM_UPDATED] = 1u;
      order_obj(4u);
      CHECK(obj_alpha_count[12] >= 2u);
      CHECK((obj_row_prio_mask[12] & 0x0Fu) != 0u);
      for (u32 si = 0; si < 2u; si++) {
        u32 s = spans[si][0], e = spans[si][1];
        for (u32 y = 6u; y < 44u; y++) {
          write_ioreg(REG_VCOUNT, (u16)y);
          order_layers((dispcnts[ai] >> 8) & 0x1Fu, y);
          affine_reference_x[0] = 0;
          affine_reference_y[0] = (s32)(y << 8);

          u16 got[240];
          u16 ref[240];
          u32 tmp[240];
          memset(got, 0xAA, sizeof(got));
          memset(ref, 0xAA, sizeof(ref));

          bool ok = try_render_mode4_direct_obj_blend(
            s, e, got, 0x1Fu, dispcnts[ai], 0x3F3Fu);
          CHECK(ok);

          bitmap_render_layers<INDXCOLOR, STCKCOLOR, u32>(
            s, e, tmp, 0x1Fu);
          merge_blend<OBJ_BLEND, true>(s, e, ref, tmp);

          if (memcmp(got, ref, sizeof(got)) != 0) {
            for (u32 x = 0; x < 240u; x++) {
              if (got[x] != ref[x]) {
                fprintf(stderr,
                  "mode4 direct OBJ_BLEND mismatch ai=%u si=%u y=%u x=%u: "
                  "got=%04X ref=%04X\n", ai, si, y, x, got[x], ref[x]);
                return false;
              }
            }
          }
        }
      }
    }
  }

  return true;
}

int main(void)
{
  if (!run_tests())
    return 1;
  printf("video dirty scanline + fast4bpp tests passed (MSB_FIRST=%d)\n",
#ifdef MSB_FIRST
         1
#else
         0
#endif
  );
  return 0;
}
