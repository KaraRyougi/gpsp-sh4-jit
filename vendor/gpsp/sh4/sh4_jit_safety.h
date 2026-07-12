#ifndef CGBA_SH4_JIT_SAFETY_H
#define CGBA_SH4_JIT_SAFETY_H

#include <stdint.h>

/* True once a RAM-code range has observed any guest offset. UINT32_MAX is the
 * sole unseen sentinel; offset zero is valid translated ARM/Thumb code. */
static inline int cgba_sh4_ram_code_seen(uint32_t code_min)
{
  return code_min != UINT32_MAX;
}

/* The threaded translator only owns instruction fetches from these GBA bus
 * domains.  In particular, region 0 is not wholly executable: the 16 KiB BIOS
 * backing is smaller than a memory-map page, so scanning past 0x00003fff would
 * read beyond it. */
static inline unsigned cgba_sh4_jit_exec_domain(uint32_t pc)
{
  unsigned region = pc >> 24;

  if(region == 0 && pc < 0x00004000u)
    return 1;                       /* BIOS */
  if(region == 2)
    return 2;                       /* EWRAM and its mirrors */
  if(region == 3)
    return 3;                       /* IWRAM and its mirrors */
  if(region >= 8 && region <= 0x0d)
    return 4;                       /* Game Pak ROM windows */
  return 0;
}

/* A block may span 32 KiB map pages and Game Pak waitstate windows, but it may
 * not escape the execution domain in which it started.  Besides decoding data
 * as opcodes, a RAM scan writes SMC tags relative to the start domain; letting
 * an IWRAM scan enter I/O therefore corrupts host state before emission. */
static inline int cgba_sh4_jit_scan_may_continue(uint32_t start, uint32_t next)
{
  unsigned domain = cgba_sh4_jit_exec_domain(start);

  return domain != 0 && domain == cgba_sh4_jit_exec_domain(next);
}

#endif /* CGBA_SH4_JIT_SAFETY_H */
