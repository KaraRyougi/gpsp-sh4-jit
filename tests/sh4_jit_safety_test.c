#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vendor/gpsp/sh4/sh4_jit_safety.h"

static unsigned checks;

static void expect(const char *what, uint32_t opcode, int got, int want)
{
  checks++;
  if (!!got == !!want)
    return;
  fprintf(stderr, "%s: opcode=%08X got=%d want=%d\n",
          what, (unsigned)opcode, !!got, !!want);
  exit(1);
}

static void test_ram_seen_sentinel(void)
{
  expect("RAM unseen sentinel", UINT32_MAX,
         cgba_sh4_ram_code_seen(UINT32_MAX), 0);
  expect("RAM offset zero", 0, cgba_sh4_ram_code_seen(0), 1);
  expect("IWRAM final offset", 0x7FFF,
         cgba_sh4_ram_code_seen(0x7FFF), 1);
  expect("EWRAM final offset", 0x3FFFF,
         cgba_sh4_ram_code_seen(0x3FFFF), 1);
}

static void test_exec_domains(void)
{
  expect("BIOS executable", 0x00003FFF,
         cgba_sh4_jit_exec_domain(0x00003FFF), 1);
  expect("past BIOS rejected", 0x00004000,
         cgba_sh4_jit_exec_domain(0x00004000), 0);
  expect("I/O rejected", 0x04000000,
         cgba_sh4_jit_exec_domain(0x04000000), 0);
  expect("outside GBA bus rejected", 0x10000000,
         cgba_sh4_jit_exec_domain(0x10000000), 0);
}

static void test_scan_domain_boundaries(void)
{
  expect("IWRAM mirror stays executable", 0x03FFFFFC,
         cgba_sh4_jit_scan_may_continue(0x03000000, 0x03FFFFFC), 1);
  expect("EWRAM 32K page seam", 0x02008000,
         cgba_sh4_jit_scan_may_continue(0x02007FFC, 0x02008000), 1);
  expect("ROM 32K page seam", 0x08008000,
         cgba_sh4_jit_scan_may_continue(0x08007FFC, 0x08008000), 1);
  expect("ROM window seam", 0x0A000000,
         cgba_sh4_jit_scan_may_continue(0x09FFFFFC, 0x0A000000), 1);

  expect("IWRAM to I/O stops", 0x04000000,
         cgba_sh4_jit_scan_may_continue(0x03FFFFFC, 0x04000000), 0);
  expect("EWRAM to IWRAM stops", 0x03000000,
         cgba_sh4_jit_scan_may_continue(0x02FFFFFC, 0x03000000), 0);
  expect("BIOS end stops", 0x00004000,
         cgba_sh4_jit_scan_may_continue(0x00003FFC, 0x00004000), 0);
  expect("ROM to save memory stops", 0x0E000000,
         cgba_sh4_jit_scan_may_continue(0x0DFFFFFC, 0x0E000000), 0);
  expect("unsupported start cannot scan", 0x04000004,
         cgba_sh4_jit_scan_may_continue(0x04000000, 0x04000004), 0);
}

int main(void)
{
  test_ram_seen_sentinel();
  test_exec_domains();
  test_scan_domain_boundaries();
  printf("sh4 JIT safety: %u boundary checks passed\n", checks);
  return 0;
}
