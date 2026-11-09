#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CGBA_VIDEO_SPRITE_DECODE_CACHE 1
#include "../vendor/gpsp/video.cc"

u32 reg[64], skip_next_frame;
int sprite_limit = 1;
u16 io_registers[512], oam_ram[512], palette_ram[512], palette_ram_converted[512];
u8 vram[96 * 1024];
static u32 seed = 0x98765432;
static u32 random_word() { seed = seed * 1664525u + 1013904223u; return seed; }

template<typename T, rendtype mode>
static bool compare_row(unsigned priority, unsigned start, unsigned end)
{
  T cached[480], direct[480];
  for (unsigned i = 0; i < 480; i++) cached[i] = direct[i] = (T)random_word();
  render_scanline_objs<T, mode, true>(priority, start, end, cached, palette_ram_converted + 256);
  render_scanline_objs<T, mode, false>(priority, start, end, direct, palette_ram_converted + 256);
  return !memcmp(cached, direct, sizeof(cached));
}

int main()
{
  unsigned comparisons = 0;
  for (unsigned scene = 0; scene < 64; scene++) {
    for (unsigned i = 0; i < sizeof(vram); i++) vram[i] = random_word() >> 24;
    for (unsigned i = 0; i < 512; i++) palette_ram_converted[i] = random_word() >> 16;
    for (unsigned i = 0; i < 128; i++) {
      /* Shape 3 and prohibited mode 3 also exercise visibility filtering. */
      oam_ram[i*4] = eswap16(random_word() >> 16);
      oam_ram[i*4+1] = eswap16(random_word() >> 16);
      oam_ram[i*4+2] = eswap16(random_word() >> 16);
      oam_ram[i*4+3] = eswap16(random_word() >> 16);
    }
    write_ioreg(REG_DISPCNT, (scene % 6) | ((scene & 1) ? 0x40 : 0) | ((scene & 2) ? 0x20 : 0));
    write_ioreg(REG_MOSAIC, scene & 4 ? 0x1200 : 0);
    write_ioreg(REG_BLDCNT, random_word());
    sprite_limit = scene & 8;
    order_obj(scene % 6);
    for (unsigned y = 0; y < 160; y++) {
      write_ioreg(REG_VCOUNT, y);
      for (unsigned priority = 0; priority < 5; priority++) {
        unsigned start = y % 3 ? 0 : 31, end = y % 5 ? 240 : 191;
        if (!compare_row<u16, FULLCOLOR>(priority, start, end) ||
            !compare_row<u16, INDXCOLOR>(priority, start, end) ||
            !compare_row<u32, STCKCOLOR>(priority, start, end)) {
          printf("sprite decode mismatch scene=%u y=%u priority=%u\n", scene, y, priority);
          return 1;
        }
        comparisons += 3;
      }
    }
  }
  printf("sprite decode cache: %u scanline comparisons passed\n", comparisons);
  return 0;
}
