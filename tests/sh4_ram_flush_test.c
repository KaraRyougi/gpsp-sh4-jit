/* Production-bound regression for RAM tag invalidation at guest offset zero.
 *
 * Include cpu_threaded.c directly so this test exercises the real
 * flush_translation_cache_ram() implementation, rather than only the
 * cgba_sh4_ram_code_seen() predicate shared with it. Dead-section elimination
 * drops the translator and host-emitter code that this narrow host test does
 * not call.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The generic host emitter parses cpu_threaded.c without target assembly. Its
 * ROM lookup macros reference this SH4-owned diagnostic variable before the
 * later declaration in cpu_threaded.c, so provide the declaration/definition
 * before including the production translation unit. */
int cgba_dynarec_single_block;

#define X86_64_ARCH 1
#define HAVE_DYNAREC 1
#define ROM_TRANSLATION_CACHE_SIZE (64 * 1024)
#define RAM_TRANSLATION_CACHE_SIZE (64 * 1024)
#define ROM_BRANCH_HASH_BITS 8

#include "vendor/gpsp/cpu_threaded.c"

/* State referenced by the retained flush section. */
u32 reg[64];
u8 iwram[1024 * 32 * 2];
u8 ewram[1024 * 256 * 2];
u8 ram_translation_cache[RAM_TRANSLATION_CACHE_SIZE];
u32 flush_ram_count;

static unsigned failures;

#define CHECK(condition, ...) do {                                            \
  if (!(condition)) {                                                         \
    failures++;                                                               \
    printf("FAIL %s:%d: ", __FILE__, __LINE__);                              \
    printf(__VA_ARGS__);                                                      \
    putchar('\n');                                                            \
  }                                                                           \
} while (0)

static int all_zero(const u8 *p, size_t size)
{
  size_t i;
  for (i = 0; i < size; i++)
    if (p[i] != 0)
      return 0;
  return 1;
}

static void reset_ranges(void)
{
  iwram_code_min = ~0U;
  iwram_code_max = 0;
  ewram_code_min = ~0U;
  ewram_code_max = 0;
}

static void test_iwram_offset_zero(void)
{
  reset_ranges();
  memset(iwram, 0, sizeof(iwram));
  memset(iwram, 0xA5, 0x8000);       /* translated-code tag mirror */
  iwram[0x8000] = 0x5A;              /* guest data must not be cleared */
  iwram_code_min = 0;
  iwram_code_max = 0;

  flush_translation_cache_ram();

  CHECK(all_zero(iwram, 0x8000),
        "IWRAM offset-zero tag survived the production RAM flush");
  CHECK(iwram[0x8000] == 0x5A,
        "IWRAM data changed while clearing its tag mirror: %02X",
        iwram[0x8000]);
  CHECK(iwram_code_min == ~0U && iwram_code_max == 0,
        "IWRAM range was not reset: %08X-%08X",
        iwram_code_min, iwram_code_max);
}

static void test_ewram_offset_zero(void)
{
  reset_ranges();
  memset(ewram, 0, sizeof(ewram));
  ewram[0] = 0x5A;                   /* guest data must not be cleared */
  memset(ewram + 0x40000, 0xA5, 0x40000); /* translated-code tag mirror */
  ewram_code_min = 0;
  ewram_code_max = 0;

  flush_translation_cache_ram();

  CHECK(all_zero(ewram + 0x40000, 0x40000),
        "EWRAM offset-zero tag survived the production RAM flush");
  CHECK(ewram[0] == 0x5A,
        "EWRAM data changed while clearing its tag mirror: %02X", ewram[0]);
  CHECK(ewram_code_min == ~0U && ewram_code_max == 0,
        "EWRAM range was not reset: %08X-%08X",
        ewram_code_min, ewram_code_max);
}

int main(void)
{
  test_iwram_offset_zero();
  test_ewram_offset_zero();

  CHECK(flush_ram_count == 2,
        "production RAM flush call count=%u expected=2", flush_ram_count);
  if (failures) {
    printf("SH4 RAM offset-zero flush regression failed: %u\n", failures);
    return 1;
  }

  puts("SH4 production RAM offset-zero flush regression passed");
  return 0;
}
