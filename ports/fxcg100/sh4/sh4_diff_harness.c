/*
 * Differential interp-vs-dynarec harness (subtask 2).
 *
 * The interpreter (execute_arm) is the correctness oracle. To validate the SH4
 * dynarec we run the SAME starting machine state through both cores for an
 * identical cycle budget and diff the result. Any mismatch localizes a dynarec
 * bug to the run window; the harness reports the first divergent register/region.
 *
 * We snapshot the FULL machine (gba_save_state: CPU + scheduler + memory +
 * sound) so both cores run from byte-identical state INCLUDING hardware event
 * timing — a partial reg[]+RAM snapshot leaves the timer/video scheduler
 * inconsistent and idle/halt loops spin. Run a frame-sized window so the halt /
 * VBlank-IRQ path is processed symmetrically by both cores; a short cycle window
 * stops mid-block at a halt and produces noise, not real divergences.
 *
 * Driven by the live `dynarec_enable` toggle: cgba_gpsp_run_frame() dispatches
 * to execute_arm or execute_arm_translate; cgba_sh4_diff_run() runs the A/B
 * comparison on demand. cgba_sh4_diff_blocks() single-steps blocks for finer
 * isolation (it disables chaining via cgba_dynarec_single_block), but the
 * interpreter must process halts symmetrically for that to be meaningful.
 */

#include <string.h>

#include "vendor/gpsp/common.h"
#include "vendor/gpsp/cpu.h"
#include "vendor/gpsp/gba_memory.h"
#include "vendor/gpsp/savestate.h"
#include "ports/fxcg100/sh4/sh4_diff_harness.h"

void execute_arm(u32 cycles);              /* interpreter (cpu.cc) */
u32  execute_arm_translate(u32 cycles);    /* dynarec (sh4_interp_helpers.c) */
void reset_gba(void);                      /* main.c */
extern int cgba_dynarec_single_block;      /* sh4_interp_helpers.c / sh4_stub.S */

#define CGBA_HIGH_BSS_LOCAL __attribute__((section(".cgba.highbss"), aligned(32)))

/* Full machine snapshot (CPU + scheduler + memory + sound), so the interpreter
 * and dynarec runs execute from byte-identical state INCLUDING hardware event
 * timing — a partial reg[]+RAM snapshot leaves the timer/video scheduler
 * inconsistent and idle/halt loops then spin. gba_load_state also flushes the
 * dynarec cache, which is exactly what we want between runs. */
static u8 snap_buf[GBA_STATE_MEM_SIZE] CGBA_HIGH_BSS_LOCAL;

/* Interpreter result (the oracle): full reg[] + region hashes. */
static u32 oracle_reg[64];
static u32 oracle_h_iwram, oracle_h_ewram, oracle_h_vram, oracle_h_io;

static void capture_full(void)  { gba_save_state(snap_buf); }
static void restore_full(void)  { gba_load_state(snap_buf); }

static u32 fnv1a(const void *p, u32 n)
{
  const u8 *b = (const u8 *)p;
  u32 h = 2166136261u, i;
  for (i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
  return h;
}

int cgba_sh4_diff_run(uint32_t cycles, cgba_diff_result *out)
{
  int i;

  memset(out, 0, sizeof *out);
  out->cycles = cycles;
  out->start_pc = reg[REG_PC];

  /* 1. snapshot the starting state */
  capture_full();

  /* 2. reference run: interpreter -> oracle reg[] + region hashes */
  execute_arm(cycles);
  out->interp_pc = reg[REG_PC];
  memcpy(oracle_reg, reg, sizeof oracle_reg);
  oracle_h_iwram = fnv1a(iwram, 1024 * 32 * 2);
  oracle_h_ewram = fnv1a(ewram, 1024 * 256 * 2);
  oracle_h_vram  = fnv1a(vram, 1024 * 96);
  oracle_h_io    = fnv1a(io_registers, sizeof io_registers);

  /* 3. rewind and run the dynarec from the identical starting state */
  restore_full();
  flush_dynarec_caches();
  execute_arm_translate(cycles);
  out->dynarec_pc = reg[REG_PC];

  /* 4. compare the register file first (most actionable) */
  for (i = 0; i < 64; i++) {
    if (reg[i] != oracle_reg[i]) {
      out->diverged = 1;
      out->kind = CGBA_DIFF_REG;
      out->index = i;
      out->interp_value = oracle_reg[i];
      out->dynarec_value = reg[i];
      return 1;
    }
  }

  /* 5. compare touched memory regions by hash */
  if (fnv1a(iwram, 1024 * 32 * 2) != oracle_h_iwram) {
    out->diverged = 1; out->kind = CGBA_DIFF_IWRAM; return 1;
  }
  if (fnv1a(ewram, 1024 * 256 * 2) != oracle_h_ewram) {
    out->diverged = 1; out->kind = CGBA_DIFF_EWRAM; return 1;
  }
  if (fnv1a(vram, 1024 * 96) != oracle_h_vram) {
    out->diverged = 1; out->kind = CGBA_DIFF_EWRAM; out->index = -1; return 1;
  }
  if (fnv1a(io_registers, sizeof io_registers) != oracle_h_io) {
    out->diverged = 1; out->kind = CGBA_DIFF_IO; return 1;
  }

  out->diverged = 0;
  return 0;
}

/* Run the diff and dump start PC + every divergent r0..r15 (oracle vs dynarec)
 * across up to max_lines. For root-causing a specific block. */
unsigned cgba_sh4_diff_dump(uint32_t cycles, char out[][48], unsigned max_lines)
{
  cgba_diff_result r;
  unsigned n = 0;
  int i;

  cgba_sh4_diff_run(cycles, &r);
  if (n < max_lines)
    snprintf(out[n++], 48, "p%lX i%lX d%lX %s%d",
      (unsigned long)r.start_pc, (unsigned long)r.interp_pc,
      (unsigned long)r.dynarec_pc, cgba_sh4_diff_kind_name(r.kind), r.index);
  for (i = 0; i < 16 && n < max_lines; i++)
    if (reg[i] != oracle_reg[i])
      snprintf(out[n++], 48, "r%d i%08lX d%08lX", i,
        (unsigned long)oracle_reg[i], (unsigned long)reg[i]);
  return n;
}

const char *cgba_sh4_diff_kind_name(int kind)
{
  switch (kind) {
  case CGBA_DIFF_REG:   return "reg";
  case CGBA_DIFF_IWRAM: return "iwram";
  case CGBA_DIFF_EWRAM: return "ewram";
  case CGBA_DIFF_IO:    return "io";
  default:              return "none";
  }
}

/* ---------------- single-block lockstep differential ---------------------- *
 * The cycle-window diff is too coarse to localize one instruction. This steps
 * the dynarec exactly one block (via cgba_dynarec_single_block), then runs the
 * interpreter from the same state to the dynarec's next PC, and compares. It
 * reports the FIRST block whose register file or next-PC disagrees — i.e. the
 * exact block whose translation is wrong. Starts from a clean reset so the
 * earliest (simplest) blocks are checked first. */

static u32 dyn_reg[64];

/* Incremental trace to the casio-emu debug-putchar port (0xb7000000), so if a
 * translated block hangs/faults the last printed PC pinpoints the bad block.
 * No-op-ish on hardware (debug-only path). */
static void dbg_tag(char c, u32 v)
{
#ifdef CGBA_GPSP_HEADLESS_TEST   /* casio-emu debug port only; not on hardware */
  volatile unsigned char *port = (volatile unsigned char *)0xb7000000u;
  const char *hx = "0123456789ABCDEF";
  int i;
  *port = c;
  for (i = 7; i >= 0; i--)
    *port = (unsigned char)hx[(v >> (i * 4)) & 0xF];
  *port = '\n';
#else
  (void)c; (void)v;
#endif
}

static void interp_run_to_pc(u32 target, unsigned max_steps)
{
  unsigned i;
  for (i = 0; i < max_steps; i++) {
    if (reg[REG_PC] == target)
      return;
    if (reg[CPU_HALT_STATE] != 0)   /* halted: stepping won't reach a code PC */
      return;
    execute_arm(2);
  }
}

unsigned cgba_sh4_diff_blocks(unsigned max_blocks, char out[][48], unsigned max_lines)
{
  unsigned b, n = 0;

  /* Step from the current (live) state — not a reset — to avoid the full-ROM
   * translation cascade the cart entry point triggers. */
  flush_dynarec_caches();
  cgba_dynarec_single_block = 1;     /* one block per entry, no chaining */

  for (b = 0; b < max_blocks; b++) {
    u32 pc0 = reg[REG_PC];
    u32 dyn_pc;
    int i, diverged = 0;

    dbg_tag('D', pc0);             /* before running the dynarec block */
    capture_full();

    /* dynarec: exactly one block, then back to us */
    execute_arm_translate(0x4000);
    memcpy(dyn_reg, reg, sizeof dyn_reg);
    dyn_pc = reg[REG_PC];
    dbg_tag('R', dyn_pc);          /* dynarec block returned, next PC */

    /* interpreter oracle: from the same start, advance to the dynarec's next PC */
    restore_full();
    interp_run_to_pc(dyn_pc, 16);
    dbg_tag('I', reg[REG_PC]);     /* interpreter reached */

    if (reg[REG_PC] != dyn_pc) {
      cgba_dynarec_single_block = 0;
      if (n < max_lines)
        snprintf(out[n++], 48, "B%u p%lX PC d%lX i%lX", b,
          (unsigned long)pc0, (unsigned long)dyn_pc, (unsigned long)reg[REG_PC]);
      return n;
    }
    for (i = 0; i < 16; i++) {
      if (reg[i] != dyn_reg[i] && n < max_lines) {
        diverged = 1;
        snprintf(out[n++], 48, "B%u p%lX r%d i%lX d%lX", b,
          (unsigned long)pc0, i, (unsigned long)reg[i], (unsigned long)dyn_reg[i]);
      }
    }
    if (diverged) {
      cgba_dynarec_single_block = 0;
      return n;
    }
    /* matched: the live (interpreter) state is the baseline for the next block */
  }
  cgba_dynarec_single_block = 0;
  if (n < max_lines)
    snprintf(out[n++], 48, "MATCH %u blocks", max_blocks);
  return n;
}
