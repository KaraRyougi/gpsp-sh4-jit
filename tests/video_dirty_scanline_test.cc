#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CGBA_VIDEO_TEXT_FAST4BPP 1
#define CGBA_VIDEO_DIRTY_SCANLINE 1
#define CGBA_VIDEO_SPRITE_DECODE_CACHE 1
#define CGBA_VIDEO_BLEND_PALETTE_CACHE 1
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

static bool run_tests(void)
{
  CHECK(test_fast4bpp_standalone());

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
