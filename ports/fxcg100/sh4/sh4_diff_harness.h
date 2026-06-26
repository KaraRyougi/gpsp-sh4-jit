#ifndef CGBA_SH4_DIFF_HARNESS_H
#define CGBA_SH4_DIFF_HARNESS_H

#include <stdint.h>

/* What diverged between the interpreter and the dynarec. */
enum {
  CGBA_DIFF_NONE = 0,
  CGBA_DIFF_REG,
  CGBA_DIFF_IWRAM,
  CGBA_DIFF_EWRAM,
  CGBA_DIFF_IO,
};

typedef struct {
  int      diverged;        /* 1 if interp and dynarec disagree           */
  int      kind;            /* CGBA_DIFF_* region of the first mismatch    */
  int      index;           /* register index or byte offset within region */
  uint32_t interp_value;    /* oracle value                                */
  uint32_t dynarec_value;   /* dynarec value                               */
  uint32_t cycles;          /* cycle budget the comparison used            */
  uint32_t start_pc;        /* guest PC at the start of the compared window */
  uint32_t interp_pc;       /* oracle reg[15] after the run                 */
  uint32_t dynarec_pc;      /* dynarec reg[15] after the run                */
} cgba_diff_result;

/* Run `cycles` of guest execution under both cores from one snapshot and diff
 * the result. Returns 1 (and fills *out) on the first divergence, else 0. */
int cgba_sh4_diff_run(uint32_t cycles, cgba_diff_result *out);

const char *cgba_sh4_diff_kind_name(int kind);

/* Diagnostic: run the diff and dump start/interp/dynarec PC + each divergent
 * r0..r15 (oracle vs dynarec) into out, up to max_lines. Returns lines used. */
unsigned cgba_sh4_diff_dump(uint32_t cycles, char out[][48], unsigned max_lines);

/* Run one window under both cores and report which regions (IWRAM/EWRAM/VRAM/IO)
 * diverge plus the first diverging IWRAM word, for classifying a content diff. */
unsigned cgba_sh4_diff_regions(uint32_t cycles, char out[][48], unsigned max_lines);

/* Single-block lockstep diff from a clean reset: report the first block whose
 * dynarec translation disagrees with the interpreter (block PC + divergent
 * register or next-PC). Returns lines used. */
unsigned cgba_sh4_diff_blocks(unsigned max_blocks, char out[][48], unsigned max_lines);

#endif /* CGBA_SH4_DIFF_HARNESS_H */
