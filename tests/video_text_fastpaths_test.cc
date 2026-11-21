#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Whole-span differential for the two text-background fast paths:
 *   CGBA_VIDEO_EMPTY_RUN_SKIP  runs of repeated empty overlay tiles
 *   CGBA_VIDEO_PAIR_PALETTE    two-pixel palette table (base rows and opaque
 *                              overlay rows, FULLCOLOR u16)
 * The reference below derives every pixel from the tile map independently of
 * the production walker. */
#define CGBA_VIDEO_BACKDROP_SHADOW_PALETTE 1
#define CGBA_VIDEO_OPAQUE_ROW_UNROLL 1
#define CGBA_VIDEO_EMPTY_RUN_SKIP 1
#define CGBA_VIDEO_PAIR_PALETTE 1
#include "../vendor/gpsp/video.cc"

u16 palette_ram_converted[512], io_registers[512];
volatile u32 palette_ram_dirty;
u8 vram[96 * 1024];

static uint32_t seed = 0x2f6e2b1u;
static unsigned long spans, pixels;
static uint32_t rnd(void)
{
  seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
  return seed;
}

#define CHECK(c) do { if (!(c)) { \
  printf("text fastpaths failed line %d (span %lu)\n", __LINE__, spans); \
  return false; } } while (0)

/* Returns the 4-bpp color index and palette bank of BG0 pixel x. */
static unsigned ref_pixel(unsigned x, unsigned *bank)
{
  unsigned bgcnt = read_ioreg(REG_BG0CNT);
  unsigned size = bgcnt >> 14;
  unsigned bgx = (x + read_ioreg(REG_BG0HOFS)) & ((size & 1) ? 511 : 255);
  unsigned bgy = (read_ioreg(REG_VCOUNT) + read_ioreg(REG_BG0VOFS)) &
                 ((size & 2) ? 511 : 255);
  unsigned block = (bgx >> 8) + (bgy >> 8) * ((size & 1) ? 2 : 1);
  const u8 *map = vram + ((bgcnt >> 8) & 31) * 2048 + block * 2048;
  unsigned at = (((bgy & 255) >> 3) * 32 + ((bgx & 255) >> 3)) * 2;
  unsigned entry = map[at] | (map[at + 1] << 8);
  unsigned px = bgx & 7, py = bgy & 7;
  if (entry & 0x400) px = 7 - px;
  if (entry & 0x800) py = 7 - py;
  unsigned byte = vram[((bgcnt >> 2) & 3) * 16384 + (entry & 0x3ff) * 32 +
                       py * 4 + (px >> 1)];
  *bank = entry >> 12;
  return (px & 1) ? byte >> 4 : byte & 15;
}

static void randomize_vram(unsigned round)
{
  /* Tiles: a mix of fully empty, fully opaque, and arbitrary rows. */
  for (unsigned t = 0; t < sizeof(vram) / 32; t++) {
    unsigned kind = rnd() % 4;
    for (unsigned i = 0; i < 32; i++) {
      u8 v = (u8)rnd();
      if (kind == 0) v = 0;
      else if (kind == 1) v |= 0x11;
      else if (kind == 2 && (rnd() & 3) == 0) v = 0;
      vram[t * 32 + i] = v;
    }
  }
  /* Maps (any 2 KiB block may be selected): long runs of one entry, so the
   * run skip sees repeated empty tiles, repeated drawn tiles and run ends
   * landing on every position. Only char base 0 below 0x4000 is left as
   * tile data when the screen base is high enough; either way the reference
   * reads the same bytes the renderer does. */
  for (unsigned at = (round & 1) ? 0x4000 : 0; at + 1 < sizeof(vram); ) {
    unsigned run = 1 + rnd() % ((round % 3) ? 40 : 3);
    unsigned entry = rnd() & 0xffff;
    while (run-- && at + 1 < sizeof(vram)) {
      vram[at] = (u8)entry; vram[at + 1] = (u8)(entry >> 8);
      at += 2;
    }
  }
}

static bool run_tests()
{
  for (unsigned round = 0; round < 400; round++) {
    if (round % 8 == 0)
      randomize_vram(round / 8);
    if (round == 0) {
      for (unsigned i = 0; i < 512; i++) palette_ram_converted[i] = (u16)rnd();
      palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    } else if (round % 3 == 0) {
      /* Palette writers always raise the dirty mask; change one color, the
       * backdrop, or a whole bank. Other rounds reuse built tables. */
      unsigned what = rnd() % 3;
      if (what == 0) palette_ram_converted[rnd() & 255] ^= 0x8421;
      else if (what == 1) palette_ram_converted[0] = (u16)rnd();
      else for (unsigned i = 0; i < 16; i++)
        palette_ram_converted[(rnd() & 0xf0) + i] = (u16)rnd();
      palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    }

    for (unsigned line = 0; line < 12; line++) {
      unsigned bgcnt = rnd() & 0xff0c;          /* 4 bpp, no mosaic */
      write_ioreg(REG_DISPCNT, 0);
      write_ioreg(REG_MOSAIC, 0);
      write_ioreg(REG_BG0CNT, bgcnt);
      write_ioreg(REG_BG0HOFS, rnd() & 511);
      write_ioreg(REG_BG0VOFS, rnd() & 511);
      write_ioreg(REG_VCOUNT, rnd() % 160);
      write_ioreg(REG_BLDCNT, rnd());
      unsigned start = rnd() % 240, end = start + 1 + rnd() % (240 - start);
      if (line < 4) { start = line & 1; end = 240; }
      u32 bg_comb = color_flags(5), px_comb = color_flags(0);

      for (unsigned base = 0; base < 2; base++) {
        alignas(4) u16 got16[244], want16[244];
        alignas(4) u32 got32[244], want32[244], stk[244], wstk[244];
        for (unsigned i = 0; i < 244; i++) {
          got16[i] = want16[i] = (u16)rnd();
          got32[i] = want32[i] = stk[i] = wstk[i] = rnd() & 0xffff;
        }
        for (unsigned x = start; x < end; x++) {
          unsigned bank, c = ref_pixel(x, &bank);
          u32 pxflg = px_comb | (bank << 4);
          if (c) {
            want16[x] = palette_ram_converted[bank * 16 + c];
            want32[x] = pxflg | c;
            wstk[x] = pxflg | c | ((base ? bg_comb : wstk[x]) << 16);
          } else if (base) {
            want16[x] = palette_ram_converted[0];
            want32[x] = bg_comb;
            wstk[x] = bg_comb;
          }
          pixels++;
        }
        if (base) {
          render_scanline_text<u16, FULLCOLOR, true>(0, start, end, got16, palette_ram_converted);
          render_scanline_text<u32, INDXCOLOR, true>(0, start, end, got32, palette_ram_converted);
          render_scanline_text<u32, STCKCOLOR, true>(0, start, end, stk, palette_ram_converted);
        } else {
          render_scanline_text<u16, FULLCOLOR, false>(0, start, end, got16, palette_ram_converted);
          render_scanline_text<u32, INDXCOLOR, false>(0, start, end, got32, palette_ram_converted);
          render_scanline_text<u32, STCKCOLOR, false>(0, start, end, stk, palette_ram_converted);
        }
        spans++;
        CHECK(!memcmp(got16, want16, sizeof(got16)));
        CHECK(!memcmp(got32, want32, sizeof(got32)));
        CHECK(!memcmp(stk, wstk, sizeof(stk)));
      }
    }
  }

  /* Pair entries are exactly the shadow palette's two colors. */
  for (unsigned sub = 0; sub < 16; sub++) {
    if (!pair_palette_valid[sub]) continue;
    for (unsigned b = 0; b < 256; b++) {
      CHECK(pair_palette[sub][b][0] == backdrop_shadow_palette[sub * 16 + (b & 15)]);
      CHECK(pair_palette[sub][b][1] == backdrop_shadow_palette[sub * 16 + (b >> 4)]);
    }
  }
  /* An OBJ-only palette write must not invalidate BG pair tables. */
  for (unsigned sub = 0; sub < 16; sub++) pair_palette_build(sub);
  palette_ram_converted[256 + 7] ^= 0xffff;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  get_backdrop_shadow_palette();
  for (unsigned sub = 0; sub < 16; sub++) CHECK(pair_palette_valid[sub]);
  /* One BG color invalidates exactly its bank; the backdrop, every bank. */
  palette_ram_converted[5 * 16 + 3] ^= 0x0101;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  get_backdrop_shadow_palette();
  for (unsigned sub = 0; sub < 16; sub++)
    CHECK(pair_palette_valid[sub] == (sub != 5));
  palette_ram_converted[0] ^= 0x0101;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  get_backdrop_shadow_palette();
  for (unsigned sub = 0; sub < 16; sub++) CHECK(!pair_palette_valid[sub]);

  /* 8-bpp strip base & overlay differential against reference render_scanline_text. */
  for (unsigned round = 0; round < 40; round++) {
    randomize_vram(round);
    /* Inject sparse 0 bytes into 8-bpp tile data so empty-tile & partial transparency trigger. */
    for (unsigned i = 0; i < 0x4000u; i++) {
      if ((rnd() & 3u) == 0u) vram[i] = 0u;
    }
    unsigned size = rnd() & 3u;
    unsigned bgcnt = (size << 14) | ((16u + (rnd() & 7u)) << 8) | 0x0080u; /* 8-bpp, cbb=0 */
    unsigned hofs = (round < 8) ? (round & 7u) : (rnd() & 511u);
    unsigned vofs = rnd() & 511u;
    unsigned sy0 = rnd() % 140u;
    unsigned sy1 = sy0 + 1u + (rnd() % 20u);
    if (sy1 > 160u) sy1 = 160u;

    write_ioreg(REG_DISPCNT, 0x0100u);
    write_ioreg(REG_MOSAIC, 0);
    write_ioreg(REG_BG0CNT, bgcnt);
    write_ioreg(REG_BG0HOFS, hofs);
    write_ioreg(REG_BG0VOFS, vofs);

    cgba_mode0_bg_plan p;
    memset(&p, 0, sizeof(p));
    u32 base_block = (bgcnt >> 8) & 0x1Fu;
    u32 sb_bot = base_block + ((size & 2u) ? ((size & 1u) ? 2u : 1u) : 0u);
    cgba_bg_hspan_plan sp;
    cgba_build_bg_hspan_plan(hofs, size & 1u, 0u, 240u, &sp);
    p.tile_charbase = vram;
    p.sb_ptr[0] = (u16 *)&vram[base_block * 2048u];
    p.sb_ptr[1] = (u16 *)&vram[sb_bot * 2048u];
    p.counts_packed = sp.cp;
    p.vofs = (u16)vofs;
    p.first_byte_off = sp.first_byte_off;
    p.map2_byte_off = sp.map2_byte_off;
    p.tile_hoff = sp.thoff;
    p.flags = 8u;

    static u16 got_fb[20 * 240], want_fb[20 * 240];
    for (unsigned is_over = 0; is_over < 2; is_over++) {
      for (unsigned i = 0; i < (sy1 - sy0) * 240u; i++)
        got_fb[i] = want_fb[i] = (u16)rnd();
      if (is_over)
        cgba_render_8bpp_bg_over_rows(&p, sy0, sy1, got_fb, palette_ram_converted);
      else
        cgba_render_8bpp_bg_base_rows(&p, sy0, sy1, got_fb, palette_ram_converted);
      for (unsigned y = sy0; y < sy1; y++) {
        write_ioreg(REG_VCOUNT, y);
        if (is_over)
          render_scanline_text<u16, FULLCOLOR, false>(
            0, 0, 240, want_fb + (y - sy0) * 240u, palette_ram_converted);
        else
          render_scanline_text<u16, FULLCOLOR, true>(
            0, 0, 240, want_fb + (y - sy0) * 240u, palette_ram_converted);
      }
      spans += (sy1 - sy0);
      pixels += (sy1 - sy0) * 240u;
      CHECK(!memcmp(got_fb, want_fb, (sy1 - sy0) * 240u * sizeof(u16)));
    }
  }
  return true;
}

int main()
{
  if (!run_tests()) return 1;
  printf("text fastpaths: %lu spans, %lu reference pixels passed\n",
    spans * 3, pixels * 3);
  return 0;
}
