#include "fxcg100_platform.h"
#include "sh4/sh4_cache.h"
#include "sh4/sh4_codegen.h"

typedef int (*probe_fn)(void);

static uint8_t probe_code[32] __attribute__((aligned(32)));

static int emit_probe(int value)
{
  sh4_codegen cg;

  cg.ptr = probe_code;
  cg.limit = probe_code + sizeof(probe_code);
  cg.overflow = 0;

  sh4_emit_mov_imm(&cg, value, 0);
  sh4_emit_rts(&cg);
  sh4_emit_nop(&cg);

  if (cg.overflow)
    return -1;

  cgba_sh4_cache_sync(probe_code, cg.ptr);
  return 0;
}

int cgba_run_jit_probe(void)
{
  probe_fn fn = (probe_fn)probe_code;
  int first;
  int second;

  if (emit_probe(0x2a) != 0)
    return -1;
  first = fn();

  if (emit_probe(0x2b) != 0)
    return -2;
  second = fn();

  if (first != 0x2a || second != 0x2b)
    return -3;

  return 0;
}
