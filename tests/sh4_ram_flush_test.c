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
  cgba_iwram_code_end = 0U;
  ewram_code_min = ~0U;
  cgba_ewram_code_end = 0U;
}

static void test_iwram_offset_zero(void)
{
  reset_ranges();
  memset(iwram, 0, sizeof(iwram));
  memset(iwram, 0xA5, 0x8000);       /* translated-code tag mirror */
  iwram[0x8000] = 0x5A;              /* guest data must not be cleared */
  iwram_code_min = 0;
  cgba_iwram_code_end = 0;           /* degenerate end <= min -> full clear */

  flush_translation_cache_ram();

  CHECK(all_zero(iwram, 0x8000),
        "IWRAM offset-zero tag survived the production RAM flush");
  CHECK(iwram[0x8000] == 0x5A,
        "IWRAM data changed while clearing its tag mirror: %02X",
        iwram[0x8000]);
  CHECK(iwram_code_min == ~0U && cgba_iwram_code_end == 0,
        "IWRAM range was not reset: %08X-%08X",
        iwram_code_min, cgba_iwram_code_end);
}

static void test_ewram_offset_zero(void)
{
  reset_ranges();
  memset(ewram, 0, sizeof(ewram));
  ewram[0] = 0x5A;                   /* guest data must not be cleared */
  memset(ewram + 0x40000, 0xA5, 0x40000); /* translated-code tag mirror */
  ewram_code_min = 0;
  cgba_ewram_code_end = 0;           /* degenerate end <= min -> full clear */

  flush_translation_cache_ram();

  CHECK(all_zero(ewram + 0x40000, 0x40000),
        "EWRAM offset-zero tag survived the production RAM flush");
  CHECK(ewram[0] == 0x5A,
        "EWRAM data changed while clearing its tag mirror: %02X", ewram[0]);
  CHECK(ewram_code_min == ~0U && cgba_ewram_code_end == 0,
        "EWRAM range was not reset: %08X-%08X",
        ewram_code_min, cgba_ewram_code_end);
}

static void test_subrange_and_dual_hot_preserved(void)
{
  reset_ranges();
  memset(iwram, 0, sizeof(iwram));
  memset(iwram + 0x100, 0xA5, 0x20);
  iwram_code_min = 0x104;
  cgba_iwram_code_end = 0x118;
  cgba_dynarec_dual_hot[0] = 0x08001235u;
  cgba_dynarec_dual_hot[1] = 0x8C100008u;

  flush_translation_cache_ram();

  CHECK(all_zero(iwram + 0x100, 0x18),
        "IWRAM subrange [0x100, 0x118) was not cleared");
  CHECK(iwram[0x118] == 0xA5,
        "IWRAM byte past cgba_iwram_code_end was unexpectedly cleared: %02X",
        iwram[0x118]);
  CHECK(cgba_dynarec_dual_hot[0] == 0x08001235u &&
        cgba_dynarec_dual_hot[1] == 0x8C100008u,
        "cgba_dynarec_dual_hot (ROM-only) was wiped by RAM flush");
  CHECK(iwram_code_min == ~0U && cgba_iwram_code_end == 0,
        "IWRAM range was not reset after subrange flush: %08X-%08X",
        iwram_code_min, cgba_iwram_code_end);
}

static void test_smc_change_predicates(void)
{
  u8 dst[16], src[16], tags[16];
  u8 pat4[4] = { 0x11, 0x22, 0x33, 0x44 };
  u8 pat2[2] = { 0xAA, 0xBB };
  unsigned i;

  for (i = 0; i < sizeof(dst); i++) {
    dst[i] = (u8)(0x10u + i);
    src[i] = dst[i];
    tags[i] = 0u;
  }

  /* Identical copy over tagged code must NOT signal SMC. */
  tags[4] = 1u; tags[5] = 1u; tags[6] = 1u; tags[7] = 1u;
  CHECK(cgba_smc_copy_changes_tagged(dst, src, tags, sizeof(dst)) == 0,
        "identical copy over tagged code falsely flagged SMC");

  /* Changing only untagged data bytes in a mixed span must NOT signal SMC. */
  src[0] = 0xFFu;
  src[12] = 0xEEu;
  CHECK(cgba_smc_copy_changes_tagged(dst, src, tags, sizeof(dst)) == 0,
        "untagged-only copy diff falsely flagged SMC");

  /* Changing any tagged code byte MUST signal SMC. */
  src[6] = 0x99u;
  CHECK(cgba_smc_copy_changes_tagged(dst, src, tags, sizeof(dst)) == 1,
        "tagged byte modification failed to flag SMC");

  /* Fill over tagged bytes with identical pattern must NOT signal SMC. */
  for (i = 0; i < sizeof(dst); i++)
    dst[i] = pat4[i & 3u];
  CHECK(cgba_smc_fill_changes_tagged(dst, pat4, 4u, tags, sizeof(dst)) == 0,
        "identical 32-bit fill over tagged code falsely flagged SMC");
  dst[1] = 0x00u; /* untagged differs, tagged matches */
  CHECK(cgba_smc_fill_changes_tagged(dst, pat4, 4u, tags, sizeof(dst)) == 0,
        "32-bit fill differing only at untagged byte falsely flagged SMC");
  dst[5] = 0x00u; /* tagged differs */
  CHECK(cgba_smc_fill_changes_tagged(dst, pat4, 4u, tags, sizeof(dst)) == 1,
        "32-bit fill changing tagged byte failed to flag SMC");

  /* 16-bit fill check. */
  for (i = 0; i < sizeof(dst); i++)
    dst[i] = pat2[i & 1u];
  CHECK(cgba_smc_fill_changes_tagged(dst, pat2, 2u, tags, sizeof(dst)) == 0,
        "identical 16-bit fill over tagged code falsely flagged SMC");
  dst[7] = 0x00u;
  CHECK(cgba_smc_fill_changes_tagged(dst, pat2, 2u, tags, sizeof(dst)) == 1,
        "16-bit fill changing tagged byte failed to flag SMC");

  /* Scalar 1/2/4-byte store checks (incl. partial-tag word store). */
  memset(tags, 0, sizeof(tags));
  dst[0] = 0x78u; dst[1] = 0x56u; dst[2] = 0x34u; dst[3] = 0x12u;
  tags[0] = 1u; tags[1] = 1u; /* lower halfword tagged, upper untagged */
  CHECK(cgba_smc_scalar_changes_tagged(dst, tags, 4u, 0x12345678u) == 0,
        "identical scalar 32-bit store falsely flagged SMC");
  CHECK(cgba_smc_scalar_changes_tagged(dst, tags, 4u, 0xABCD5678u) == 0,
        "scalar 32-bit store changing only untagged upper halfword falsely flagged SMC");
  CHECK(cgba_smc_scalar_changes_tagged(dst, tags, 4u, 0x12345679u) == 1,
        "scalar 32-bit store changing tagged byte failed to flag SMC");
  CHECK(cgba_smc_scalar_changes_tagged(dst, tags, 1u, 0x78u) == 0,
        "identical scalar 8-bit store falsely flagged SMC");
  CHECK(cgba_smc_scalar_changes_tagged(dst, tags, 1u, 0x79u) == 1,
        "changing scalar 8-bit store failed to flag SMC");
}

int main(void)
{
  test_iwram_offset_zero();
  test_ewram_offset_zero();
  test_subrange_and_dual_hot_preserved();
  test_smc_change_predicates();

  CHECK(flush_ram_count == 3,
        "production RAM flush call count=%u expected=3", flush_ram_count);
  if (failures) {
    printf("SH4 RAM offset-zero flush regression failed: %u\n", failures);
    return 1;
  }

  puts("SH4 production RAM offset-zero flush regression passed");
  return 0;
}
