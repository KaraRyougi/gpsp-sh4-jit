#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CGBA_VIDEO_TILE_ROW_CACHE 1
#define CGBA_VIDEO_TILE_ROW_PROFILE 1
#define CGBA_VIDEO_BLEND_PALETTE_CACHE 1
#define CGBA_VIDEO_BACKDROP_SHADOW_PALETTE 1
#define CGBA_VIDEO_OPAQUE_ROW_UNROLL 1
#include "../vendor/gpsp/video.cc"

u16 palette_ram_converted[512], io_registers[512];
volatile u32 palette_ram_dirty;
u8 vram[96 * 1024];
static unsigned seed = 0x19475132, comparisons;
static unsigned random_word() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
#define CHECK(c) do { if (!(c)) { printf("tile row cache failed line %d\n", __LINE__); return false; } } while (0)

template<bool base, bool flip>
static bool compare_row(u32 data, unsigned bank, unsigned offset)
{
  alignas(4) u16 actual[12], expected[12];
  for (unsigned i = 0; i < 12; i++) actual[i] = expected[i] = random_word();
  render_cached_tile_row<base>(eswap32(data), (bank << 12) | (flip ? 0x400 : 0) | (data & 1023), actual + offset);
  for (unsigned i = 0; i < 8; i++) {
    unsigned color = (data >> (4 * (flip ? 7-i : i))) & 15;
    if (color || base) expected[offset+i] = palette_ram_converted[color ? bank*16+color : 0];
  }
  comparisons++;
  CHECK(!memcmp(actual, expected, sizeof(actual)));
  return true;
}

static bool run_tests()
{
  CHECK(sizeof(cgba_tile_row_entry) == 32);
  for (unsigned round = 0; round < 80; round++) {
    for (unsigned i = 0; i < 512; i++) palette_ram_converted[i] = random_word();
    palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    if (round & 1) get_backdrop_shadow_palette();
    tile_row_observe_palette();
    CHECK(!(palette_ram_dirty & CGBA_PALETTE_DIRTY_TILE_ROW));
    CHECK(palette_ram_dirty & CGBA_PALETTE_DIRTY_BLEND);
    if (!(round & 1)) CHECK(palette_ram_dirty & CGBA_PALETTE_DIRTY_BACKDROP);
    u32 epoch = tile_row_epoch;
    /* Identical writes and OBJ-only palette changes preserve cached BG rows. */
    palette_ram_converted[256 + (round & 255)] ^= 0x1234;
    palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
    tile_row_observe_palette();
    CHECK(tile_row_epoch == epoch);
    for (unsigned n = 0; n < 512; n++) {
      u32 data = n < 256 ? n * 0x01010101u : random_word();
      unsigned bank = n & 15;
      for (unsigned repeat = 0; repeat < 2; repeat++) {
        CHECK((compare_row<true,false>(data, bank, 2)));
        CHECK((compare_row<false,false>(data, bank, 1)));
        CHECK((compare_row<true,true>(data, bank, 1)));
        CHECK((compare_row<false,true>(data, bank, 2)));
      }
    }
  }
  /* Epoch wrap, including a stale entry whose tag would otherwise alias. */
  tile_row_epoch = 0xffff0000u;
  palette_ram_converted[0] ^= 0xffff;
  palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
  tile_row_observe_palette();
  CHECK(tile_row_epoch == 0x10000);
  CHECK((compare_row<true,false>(0, 0, 1)));

  /* Exercise the production span dispatcher: partial tiles, maps, flips,
   * odd destination alignment, multiple modes and both palette consumers.
   * Clear/restore only the DISPCNT mode to select the uncached reference. */
  for (unsigned n = 0; n < 1000; n++) {
    for (unsigned i = 0; i < sizeof(vram); i++) vram[i] = random_word();
    for (unsigned i = 0; i < 512; i++) palette_ram_converted[i] = random_word();
    unsigned bgcnt = random_word() & 0xff4f;
    if (n >= 800) bgcnt = (bgcnt & ~0xcu) | 0x80; // 8bpp, safe char base
    write_ioreg(REG_BG0CNT, bgcnt);
    write_ioreg(REG_MOSAIC, n % 5 ? 0 : 0x11);
    write_ioreg(REG_BG0HOFS, random_word() & 511);
    write_ioreg(REG_BG0VOFS, random_word() & 511);
    write_ioreg(REG_VCOUNT, n % 160);
    write_ioreg(REG_BLDCNT, random_word());
    unsigned start = n % 31, end = 240 - n % 37;
    alignas(4) u16 cached[256], direct[256];
    for (unsigned i = 0; i < 256; i++) cached[i] = direct[i] = random_word();
    for (unsigned base = 0; base < 2; base++) {
      palette_ram_dirty = CGBA_PALETTE_DIRTY_ACTIVE;
      write_ioreg(REG_DISPCNT, 0);
      unsigned lookups = cgba_tile_row_hits + cgba_tile_row_misses;
      if (base) render_scanline_text<u16,FULLCOLOR,true>(0,start,end,cached,palette_ram_converted);
      else render_scanline_text<u16,FULLCOLOR,false>(0,start,end,cached,palette_ram_converted);
      if ((bgcnt & 0x80) || ((bgcnt & 0x40) && !(n % 5)))
        CHECK(cgba_tile_row_hits + cgba_tile_row_misses == lookups);
      write_ioreg(REG_DISPCNT, 1);
      if (base) render_scanline_text<u16,FULLCOLOR,true>(0,start,end,direct,palette_ram_converted);
      else render_scanline_text<u16,FULLCOLOR,false>(0,start,end,direct,palette_ram_converted);
      CHECK(!memcmp(cached,direct,sizeof(cached)));
      comparisons++;
    }
  }
  CHECK(cgba_tile_row_hits && cgba_tile_row_misses);
  return true;
}
int main()
{
  if (!run_tests()) return 1;
  printf("tile row cache: %u comparisons passed (hits=%u misses=%u)\n",comparisons,cgba_tile_row_hits,cgba_tile_row_misses);
  return 0;
}
