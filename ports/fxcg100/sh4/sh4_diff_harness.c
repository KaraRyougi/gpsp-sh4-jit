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

/* Byte copy of the interpreter's IWRAM, so a divergence can be located to an
 * address (not just a hash mismatch). 64 KiB; in the high arena. */
static u8 oracle_iwram[1024 * 32 * 2] CGBA_HIGH_BSS_LOCAL;

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

/* Run one window under both cores and report WHICH regions diverge (so VRAM —
 * the display — is checked, not short-circuited at IWRAM) plus the first
 * diverging IWRAM word + total diverging word count. For classifying the
 * post-halt content diff: benign (sound/scratch, VRAM matches) vs real. */
unsigned cgba_sh4_diff_regions(uint32_t cycles, char out[][48], unsigned max_lines)
{
  unsigned n = 0, i;
  u32 start_pc = reg[REG_PC], ipc, dpc;
  u32 hew, hvr, hio;
  u32 first = 0xFFFFFFFFu, cnt = 0, iv = 0, dv = 0;
  int rdiff = -1;

  capture_full();
  execute_arm(cycles);
  ipc = reg[REG_PC];
  memcpy(oracle_reg, reg, sizeof oracle_reg);
  memcpy(oracle_iwram, iwram, sizeof oracle_iwram);
  hew = fnv1a(ewram, 1024 * 256 * 2);
  hvr = fnv1a(vram, 1024 * 96);
  hio = fnv1a(io_registers, sizeof io_registers);

  restore_full();
  flush_dynarec_caches();
  execute_arm_translate(cycles);
  dpc = reg[REG_PC];

  for (i = 0; i < 16; i++)
    if (reg[i] != oracle_reg[i]) { rdiff = (int)i; break; }
  for (i = 0; i < sizeof oracle_iwram; i += 4) {
    u32 a, b;
    memcpy(&a, oracle_iwram + i, 4);
    memcpy(&b, iwram + i, 4);
    if (a != b) { if (first == 0xFFFFFFFFu) { first = i; iv = a; dv = b; } cnt++; }
  }

  if (n < max_lines)
    snprintf(out[n++], 48, "p%lX i%lX d%lX r%d", (unsigned long)start_pc,
      (unsigned long)ipc, (unsigned long)dpc, rdiff);
  if (n < max_lines)        /* iw = diverging IWRAM words; the rest are 0/1 flags */
    snprintf(out[n++], 48, "iw%lu ew%d vr%d io%d", (unsigned long)cnt,
      fnv1a(ewram, 1024 * 256 * 2) != hew, fnv1a(vram, 1024 * 96) != hvr,
      fnv1a(io_registers, sizeof io_registers) != hio);
  /* First diverging IWRAM word (GBA addr = 0x03000000 + off). Audio-like packed
   * sample bytes vs a silent oracle => a sound buffer (cosmetic); structured
   * data => a real store bug worth the single-block lockstep hunt. */
  if (cnt && n < max_lines)
    snprintf(out[n++], 48, "iw@%lX i%08lX d%08lX", (unsigned long)first,
      (unsigned long)iv, (unsigned long)dv);
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
 * Dynarec-driven lockstep: step the dynarec exactly one block from a shared
 * state, then run the interpreter from the same state TO the dynarec's block-end
 * PC, and compare the register files there. Reports the first block that
 * disagrees, isolating a dynarec bug to one block.
 *
 * Why dynarec-driven: the dynarec can only stop at block boundaries, while the
 * interpreter's reg[15] advances at arbitrary cycle points (a taken branch does
 * `goto arm_loop`, bypassing the budget check, so execute_arm(1) runs a coarse
 * multi-instruction slice). Driving with the interpreter and making the dynarec
 * chase its settle-PC overshoots — the dynarec's block runs straight past a
 * mid-block settle point. Driving with the dynarec and making the interpreter
 * run to the dynarec's (real) block-end PC always aligns, because that PC is a
 * branch boundary the interpreter passes through. The catch-up uses execute_arm's
 * cgba_diff_stop_pc hook (cpu.cc), which returns the instant reg[15] == target.
 *
 * Cycle accounting differs between the two cores, so after enough blocks the
 * IRQ/halt timing skews and registers legitimately diverge there — that's the
 * signal that the residual is timing, not per-block logic, not a bug to chase. */

static u32 dyn_reg[64];

extern u32 cgba_diff_stop_pc;     /* cpu.cc: execute_arm "run until reg[15]==pc" hook */
extern int cgba_diff_stop_active;

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

/* Run the interpreter from the current state until it is about to execute
 * target_pc (via execute_arm's cgba_diff_stop_pc hook), or until it halts / a
 * generous instruction budget is spent. Returns nonzero iff it reached
 * target_pc. Failing to reach it means the dynarec branched somewhere the
 * interpreter does not — a real control-flow divergence. */
static int interp_run_to_pc(u32 target_pc)
{
  unsigned k;
  cgba_diff_stop_pc = target_pc;
  cgba_diff_stop_active = 1;
  for (k = 0; k < 4096 && reg[REG_PC] != target_pc &&
              reg[CPU_HALT_STATE] == 0; k++)
    execute_arm(0x400);            /* hook returns early the instant PC == target */
  cgba_diff_stop_active = 0;
  return reg[REG_PC] == target_pc;
}

unsigned cgba_sh4_diff_blocks(unsigned max_blocks, char out[][48], unsigned max_lines)
{
  unsigned b, n = 0;

  /* Step from a clean reset: the cart boots from 0x08000000 without halting, so
   * the earliest (simplest) blocks are checked first and there is no halt/wake
   * ambiguity. Chaining is disabled (cgba_dynarec_single_block) so each
   * execute_arm_translate stops after one block. */
  reset_gba();
  dbg_tag('S', reg[REG_PC]);
  flush_dynarec_caches();
  cgba_dynarec_single_block = 1;

  for (b = 0; b < max_blocks; b++) {
    u32 pc0 = reg[REG_PC];
    u32 dpc;
    int i, diverged = 0;

    if (reg[CPU_HALT_STATE] != 0)
      break;                       /* halt: frame-level diff handles those */

    capture_full();                /* shared starting state S (= interp baseline) */
    dbg_tag('D', pc0);

    /* Dynarec: one block from S. Its end PC is the lockstep target. */
    execute_arm_translate(0x4000);
    dpc = reg[REG_PC];
    memcpy(dyn_reg, reg, sizeof dyn_reg);
    dbg_tag('R', dpc);

    /* Interpreter (oracle): rewind to S, run to the dynarec's block-end PC. */
    restore_full();
    if (!interp_run_to_pc(dpc)) {
      if (n < max_lines)          /* dynarec went where the interpreter does not */
        snprintf(out[n++], 48, "B%u p%lX PC i%lX d%lX h%d", b,
          (unsigned long)pc0, (unsigned long)reg[REG_PC], (unsigned long)dpc,
          (int)reg[CPU_HALT_STATE]);
      break;
    }
    memcpy(oracle_reg, reg, sizeof oracle_reg);
    dbg_tag('I', reg[REG_PC]);

    for (i = 0; i < 16; i++) {     /* r0..r15 (r15 = PC, already aligned by both) */
      if (dyn_reg[i] != oracle_reg[i] && n < max_lines) {
        diverged = 1;
        snprintf(out[n++], 48, "B%u p%lX r%d i%lX d%lX", b,
          (unsigned long)pc0, i, (unsigned long)oracle_reg[i],
          (unsigned long)dyn_reg[i]);
      }
    }
    if (!diverged && dyn_reg[REG_CPSR] != oracle_reg[REG_CPSR] && n < max_lines) {
      diverged = 1;                /* mode/flags divergence */
      snprintf(out[n++], 48, "B%u p%lX cpsr i%lX d%lX", b, (unsigned long)pc0,
        (unsigned long)oracle_reg[REG_CPSR], (unsigned long)dyn_reg[REG_CPSR]);
    }
    if (diverged)
      break;
    /* reg[] is now the interpreter state at dpc -> next iteration's baseline. */
  }
  cgba_dynarec_single_block = 0;
  if (n == 0 && max_lines)
    snprintf(out[n++], 48, "MATCH %u blocks", b);
  return n;
}
