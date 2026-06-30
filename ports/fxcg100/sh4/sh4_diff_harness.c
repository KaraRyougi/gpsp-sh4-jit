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

/* gint's rtc_ticks(), declared directly: <gint/rtc.h> pulls <gint/types.h> whose
 * `u32` (uint32_t) collides with gpSP's `u32` (unsigned int). 128 Hz counter. */
uint32_t rtc_ticks(void);

void execute_arm(u32 cycles);              /* interpreter (cpu.cc) */
u32  execute_arm_translate(u32 cycles);    /* dynarec (sh4_interp_helpers.c) */
u32 update_gba(int remaining_cycles);      /* main.c hardware scheduler */
void reset_gba(void);                      /* main.c */
extern int cgba_dynarec_single_block;      /* sh4_interp_helpers.c / sh4_stub.S */
extern u32 execute_cycles;                 /* gpSP per-frame cycle budget (main.c) */
extern u32 skip_next_frame;                /* gpSP: 1 = skip the renderer */
#if defined(CGBA_GPSP_HEADLESS_TEST)
extern int cgba_sh4_trace_update_gba;      /* main.c headless scheduler trace */
extern int cgba_sh4_trace_update_tag;
extern int cgba_sh4_trace_update_count;
extern int cgba_sh4_trace_update_limit;
#endif

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
static u32 oracle_h_pal, oracle_h_oam;

/* Byte copy of the interpreter's IWRAM, so a divergence can be located to an
 * address (not just a hash mismatch). 64 KiB; in the high arena. */
static u8 oracle_iwram[1024 * 32 * 2] CGBA_HIGH_BSS_LOCAL;
static u16 oracle_io[512] CGBA_HIGH_BSS_LOCAL;

static void capture_full(void)  { gba_save_state(snap_buf); }
static void restore_full(void)  { gba_load_state(snap_buf); }

static void trace_update_begin(char tag)
{
#if defined(CGBA_GPSP_HEADLESS_TEST)
  cgba_sh4_trace_update_tag = tag;
  cgba_sh4_trace_update_count = 0;
  cgba_sh4_trace_update_limit = 96;
  cgba_sh4_trace_update_gba = 1;
#else
  (void)tag;
#endif
}

static void trace_update_end(void)
{
#if defined(CGBA_GPSP_HEADLESS_TEST)
  cgba_sh4_trace_update_gba = 0;
#endif
}

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
  trace_update_begin('I');
  execute_arm(cycles);
  trace_update_end();
  out->interp_pc = reg[REG_PC];
  memcpy(oracle_reg, reg, sizeof oracle_reg);
  oracle_h_iwram = fnv1a(iwram, 1024 * 32 * 2);
  oracle_h_ewram = fnv1a(ewram, 1024 * 256 * 2);
  oracle_h_vram  = fnv1a(vram, 1024 * 96);
  oracle_h_io    = fnv1a(io_registers, sizeof io_registers);
  oracle_h_pal   = fnv1a(palette_ram, sizeof palette_ram);
  oracle_h_oam   = fnv1a(oam_ram, sizeof oam_ram);

  /* 3. rewind and run the dynarec from the identical starting state */
  restore_full();
  flush_dynarec_caches();
  trace_update_begin('D');
  execute_arm_translate(cycles);
  trace_update_end();
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
    out->diverged = 1; out->kind = CGBA_DIFF_VRAM; return 1;
  }
  if (fnv1a(io_registers, sizeof io_registers) != oracle_h_io) {
    out->diverged = 1; out->kind = CGBA_DIFF_IO; return 1;
  }
  if (fnv1a(palette_ram, sizeof palette_ram) != oracle_h_pal) {
    out->diverged = 1; out->kind = CGBA_DIFF_PAL; return 1;
  }
  if (fnv1a(oam_ram, sizeof oam_ram) != oracle_h_oam) {
    out->diverged = 1; out->kind = CGBA_DIFF_OAM; return 1;
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
    snprintf(out[n++], 48, "p%lX i%lX d%lX %s%d %lX/%lX",
      (unsigned long)r.start_pc, (unsigned long)r.interp_pc,
      (unsigned long)r.dynarec_pc, cgba_sh4_diff_kind_name(r.kind), r.index,
      (unsigned long)r.interp_value, (unsigned long)r.dynarec_value);
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
  u32 hew, hvr, hio, hpal, hoam;
  u32 first = 0xFFFFFFFFu, cnt = 0, iv = 0, dv = 0;
  int rdiff = -1;

  capture_full();
  trace_update_begin('I');
  execute_arm(cycles);
  trace_update_end();
  ipc = reg[REG_PC];
  memcpy(oracle_reg, reg, sizeof oracle_reg);
  memcpy(oracle_iwram, iwram, sizeof oracle_iwram);
  memcpy(oracle_io, io_registers, sizeof oracle_io);
  hew = fnv1a(ewram, 1024 * 256 * 2);
  hvr = fnv1a(vram, 1024 * 96);
  hio = fnv1a(io_registers, sizeof io_registers);
  hpal = fnv1a(palette_ram, sizeof palette_ram);
  hoam = fnv1a(oam_ram, sizeof oam_ram);

  restore_full();
  flush_dynarec_caches();
  trace_update_begin('D');
  execute_arm_translate(cycles);
  trace_update_end();
  dpc = reg[REG_PC];

  for (i = 0; i < 16; i++)
    if (reg[i] != oracle_reg[i]) { rdiff = (int)i; break; }
  if (rdiff < 0 && reg[REG_CPSR] != oracle_reg[REG_CPSR])
    rdiff = REG_CPSR;                         /* r16 => flags/mode (N/Z/C/V) diverged */
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
    snprintf(out[n++], 48, "iw%lu ew%d vr%d io%d pa%d oa%d", (unsigned long)cnt,
      fnv1a(ewram, 1024 * 256 * 2) != hew, fnv1a(vram, 1024 * 96) != hvr,
      fnv1a(io_registers, sizeof io_registers) != hio,
      fnv1a(palette_ram, sizeof palette_ram) != hpal,
      fnv1a(oam_ram, sizeof oam_ram) != hoam);
  /* First diverging IWRAM word (GBA addr = 0x03000000 + off). Audio-like packed
   * sample bytes vs a silent oracle => a sound buffer (cosmetic); structured
   * data => a real store bug worth the single-block lockstep hunt. */
  if (cnt && n < max_lines)
    snprintf(out[n++], 48, "iw@%lX i%08lX d%08lX", (unsigned long)first,
      (unsigned long)iv, (unsigned long)dv);
  if (fnv1a(io_registers, sizeof io_registers) != hio && n < max_lines) {
    for (i = 0; i < sizeof oracle_io / sizeof oracle_io[0]; i++) {
      if (oracle_io[i] != io_registers[i]) {
        snprintf(out[n++], 48, "io@%lX i%04lX d%04lX", (unsigned long)(i * 2),
          (unsigned long)eswap16(oracle_io[i]),
          (unsigned long)eswap16(io_registers[i]));
        break;
      }
    }
  }
  return n;
}

unsigned cgba_sh4_diff_window(uint32_t cycles, char out[][48],
  unsigned max_lines)
{
  unsigned n = 0;

  if (n < max_lines)
    n += cgba_sh4_diff_dump(cycles, out + n, max_lines - n);
  restore_full();

  if (n < max_lines)
    n += cgba_sh4_diff_regions(cycles, out + n, max_lines - n);
  restore_full();

  return n;
}

/* A/B throughput benchmark: from one snapshot, time `frames` GBA frames of pure
 * CPU emulation (renderer skipped) under the interpreter, then the dynarec, and
 * report ticks + frames/sec for each plus the dynarec time-speedup (x100; >100 =
 * faster). Both run the identical workload from the same state, so the ratio
 * isolates the recompiler from everything else. The 128 Hz RTC is the timer, so
 * the numbers are real FPS on hardware and a comparable ratio in casio-emu. */
unsigned cgba_sh4_bench(unsigned frames, char out[][48], unsigned max_lines)
{
  unsigned i, n = 0;
  uint32_t t0, it, dt, ifps, dfps, spd;
  u32 saved_skip = skip_next_frame;

  capture_full();                            /* S = current game state */

  restore_full();                            /* interpreter from S */
  skip_next_frame = 1;
  t0 = rtc_ticks();
  for (i = 0; i < frames; i++) execute_arm(execute_cycles);
  it = rtc_ticks() - t0;

  restore_full();                            /* dynarec from S (load flushes cache) */
  skip_next_frame = 1;
  t0 = rtc_ticks();
  for (i = 0; i < frames; i++) execute_arm_translate(execute_cycles);
  dt = rtc_ticks() - t0;

  restore_full();                            /* leave a coherent state */
  skip_next_frame = saved_skip;

  ifps = it ? frames * 128u / it : 0;
  dfps = dt ? frames * 128u / dt : 0;
  spd  = dt ? it * 100u / dt : 0;

  if (n < max_lines)
    snprintf(out[n++], 48, "bench f%u it%lu dt%lu", frames,
      (unsigned long)it, (unsigned long)dt);
  if (n < max_lines)
    snprintf(out[n++], 48, "ifps%lu dfps%lu spd%lu", (unsigned long)ifps,
      (unsigned long)dfps, (unsigned long)spd);
  return n;
}

const char *cgba_sh4_diff_kind_name(int kind)
{
  switch (kind) {
  case CGBA_DIFF_REG:   return "reg";
  case CGBA_DIFF_IWRAM: return "iwram";
  case CGBA_DIFF_EWRAM: return "ewram";
  case CGBA_DIFF_VRAM:  return "vram";
  case CGBA_DIFF_PAL:   return "pal";
  case CGBA_DIFF_OAM:   return "oam";
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
static u32 input_reg[64];   /* DIAG: starting state S of each diffed block */

extern u32 cgba_diff_stop_pc;     /* cpu.cc: execute_arm "run until reg[15]==pc" hook */
extern int cgba_diff_stop_active;
extern int cgba_diff_stop_skip_initial;
extern s32 cgba_diff_stop_cycles_remaining;

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
static int interp_run_to_pc(u32 target_pc, u32 cycles, int skip_initial)
{
  cgba_diff_stop_pc = target_pc;
  cgba_diff_stop_active = 1;
  cgba_diff_stop_skip_initial = skip_initial;
  cgba_diff_stop_cycles_remaining = (s32)cycles;
  execute_arm(cycles);             /* hook returns early the instant PC == target */
  cgba_diff_stop_active = 0;
  cgba_diff_stop_skip_initial = 0;
  return reg[REG_PC] == target_pc;
}

/* True iff the dynarec's block-end registers differ from the interpreter's
 * (r0..r15 + CPSR; cycle/halt/sleep are timing residue, intentionally ignored). */
static int regs_diverge(void)
{
  int i;
  for (i = 0; i < 16; i++)
    if (dyn_reg[i] != oracle_reg[i]) return 1;
  return dyn_reg[REG_CPSR] != oracle_reg[REG_CPSR];
}

static unsigned cgba_sh4_diff_blocks_core(unsigned max_blocks, char out[][48],
  unsigned max_lines, int reset_first)
{
  unsigned b, n = 0;

  /* Step from a clean reset: the cart boots from 0x08000000 without halting, so
   * the earliest (simplest) blocks are checked first and there is no halt/wake
   * ambiguity. Chaining is disabled (cgba_dynarec_single_block) so each
   * execute_arm_translate stops after one block. */
  if (reset_first)
    reset_gba();
  dbg_tag('S', reg[REG_PC]);
  if (!reset_first && reg[CPU_HALT_STATE] != CPU_ACTIVE) {
    u32 ret;
    dbg_tag('H', reg[REG_PC]);
    ret = update_gba(-64);
    dbg_tag('W', reg[REG_PC]);
    if (ret & 0x80000000u) {
      if (n < max_lines)
        snprintf(out[n++], 48, "SLEEP frame p%lX h%lu", (unsigned long)reg[REG_PC],
          (unsigned long)reg[CPU_HALT_STATE]);
      return n;
    }
  }
  flush_dynarec_caches();
  cgba_dynarec_single_block = 1;

  for (b = 0; b < max_blocks; b++) {
    u32 pc0 = reg[REG_PC];
    u32 dpc;
    u32 dret, dused, iused;
    int i, diverged = 0;

    if (reg[CPU_HALT_STATE] != 0) {   /* process halt/wait + keep diffing */
      u32 ret;
      dbg_tag('H', reg[REG_PC]);
      ret = update_gba(-64);
      dbg_tag('W', reg[REG_PC]);
      if (ret & 0x80000000u)
        break;
      continue;
    }

    capture_full();                /* shared starting state S (= interp baseline) */
    memcpy(input_reg, reg, sizeof input_reg);   /* DIAG: starting state S */
    dbg_tag('D', pc0);

    /* Dynarec: one block from S. Its end PC is the lockstep target. */
    dret = execute_arm_translate(0x4000);
    dused = 0x4000u - dret;
    dpc = reg[REG_PC];
    memcpy(dyn_reg, reg, sizeof dyn_reg);
    dbg_tag('R', dpc);

    /* Interpreter (oracle): rewind to S, run to the dynarec's block-end PC. */
    restore_full();
    if (!interp_run_to_pc(dpc, 0x4000u, dpc == pc0)) {
      if (n < max_lines)          /* dynarec went where the interpreter does not */
        snprintf(out[n++], 48, "B%u p%lX PC i%lX d%lX h%d", b,
          (unsigned long)pc0, (unsigned long)reg[REG_PC], (unsigned long)dpc,
          (int)reg[CPU_HALT_STATE]);
      break;
    }
    iused = 0x4000u - (u32)cgba_diff_stop_cycles_remaining;
    memcpy(oracle_reg, reg, sizeof oracle_reg);
    dbg_tag('I', reg[REG_PC]);

    if (0 && iused != dused && n < max_lines) {   /* DIAG: ignore benign cycle diffs */
      diverged = 1;
      snprintf(out[n++], 48, "B%u p%lX cyc i%lu d%lu", b,
        (unsigned long)pc0, (unsigned long)iused, (unsigned long)dused);
#ifdef CGBA_SH4_DIFF_DUMP_OPS
      { u32 a;
        extern int cgba_diff_trace_cycles;
        if (n < max_lines)
          snprintf(out[n++], 48, "Bops cpsr%lX dpc%lX",
            (unsigned long)oracle_reg[REG_CPSR], (unsigned long)dpc);
        for (a = pc0; a < dpc && a < pc0 + 0x44u && n < max_lines; a += 4)
          snprintf(out[n++], 48, "op %lX %08lX", (unsigned long)a,
            (unsigned long)read_memory32(a));
        /* re-run the interp from the snapshot to dpc with per-insn cycle trace
         * (streamed to the putchar port as cPC:REMAIN lines). */
        restore_full();
        cgba_diff_stop_pc = dpc; cgba_diff_stop_active = 1;
        cgba_diff_stop_cycles_remaining = (s32)0x4000;
        cgba_diff_trace_cycles = 1;
        execute_arm(0x4000u);
        cgba_diff_trace_cycles = 0; cgba_diff_stop_active = 0;
      }
#endif
    }

    /* Compare; on a mismatch, check for a backward-branch-into-block (loop)
     * artifact. Under single-block mode the dyn exits such a block at the
     * backward branch AFTER one body pass, but interp_run_to_pc stops at the
     * FIRST time PC==dpc -- the loop-body entry, reached sequentially BEFORE the
     * branch. Retry the interp skipping one dpc occurrence (= one body pass); if
     * THAT matches, the first compare was the harness artifact, not a codegen
     * bug, and the one-body-pass state is the correct baseline for the next
     * block. Real (non-loop) divergences fail the retry and are still reported. */
    diverged = regs_diverge();
    if (diverged && dpc != pc0) {
      u32 saved_oracle[64];
      memcpy(saved_oracle, oracle_reg, sizeof saved_oracle);
      restore_full();
      if (interp_run_to_pc(dpc, 0x4000u, 1)) {       /* skip the body-start dpc */
        memcpy(oracle_reg, reg, sizeof oracle_reg);
        if (!regs_diverge())
          diverged = 0;                              /* loop artifact, not a bug */
      }
      if (diverged) {                                /* real: restore natural compare */
        restore_full();
        interp_run_to_pc(dpc, 0x4000u, dpc == pc0);
        memcpy(oracle_reg, saved_oracle, sizeof oracle_reg);
      }
    }
    if (diverged) {
      for (i = 0; i < 16; i++)
        if (dyn_reg[i] != oracle_reg[i] && n < max_lines)
          snprintf(out[n++], 48, "B%u p%lX r%d i%lX d%lX", b,
            (unsigned long)pc0, i, (unsigned long)oracle_reg[i],
            (unsigned long)dyn_reg[i]);
      if (dyn_reg[REG_CPSR] != oracle_reg[REG_CPSR] && n < max_lines)
        snprintf(out[n++], 48, "B%u p%lX cpsr i%lX d%lX", b, (unsigned long)pc0,
          (unsigned long)oracle_reg[REG_CPSR], (unsigned long)dyn_reg[REG_CPSR]);
      if (n < max_lines)
        snprintf(out[n++], 48, "IO dpc%lX du%lu iu%lu cI%lX cD%lX",
          (unsigned long)dpc, (unsigned long)dused, (unsigned long)iused,
          (unsigned long)oracle_reg[REG_CPSR], (unsigned long)dyn_reg[REG_CPSR]);
      if (n < max_lines)
        snprintf(out[n++], 48, "IN r0%lX r1%lX r4%lX ip%lX",
          (unsigned long)input_reg[0], (unsigned long)input_reg[1],
          (unsigned long)input_reg[4], (unsigned long)input_reg[12]);
      if (n < max_lines)
        snprintf(out[n++], 48, "IN r5%lX r6%lX r7%lX cP%lX",
          (unsigned long)input_reg[5], (unsigned long)input_reg[6],
          (unsigned long)input_reg[7], (unsigned long)input_reg[REG_CPSR]);
      break;
    }
    /* A self-loop that matches the interpreter (dpc == pc0) is a VCOUNT/idle
     * wait the dynarec also spins on. Single-block mode can't advance VCOUNT, so
     * step the interpreter freely until it leaves the loop, then resume the
     * lockstep from there (the spin's own wait blocks go undiffed -- they are
     * not the codegen under test). Without this the diff hangs on the per-frame
     * VBlank wait and never reaches the real divergence. */
    if (dpc == pc0) {
      int guard = 300000;
      while (reg[REG_PC] == pc0 && guard-- > 0) {
        if (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
          if (update_gba(-64) & 0x80000000u) break;   /* frame complete */
        } else {
          execute_arm(256);                            /* advance VCOUNT/timers */
        }
      }
    }
    /* reg[] is now the interpreter state at dpc -> next iteration's baseline. */
  }
  cgba_dynarec_single_block = 0;
  if (n == 0 && max_lines)
    snprintf(out[n++], 48, "MATCH %u blocks", b);
  return n;
}

unsigned cgba_sh4_diff_blocks(unsigned max_blocks, char out[][48],
  unsigned max_lines)
{
  return cgba_sh4_diff_blocks_core(max_blocks, out, max_lines, 1);
}

unsigned cgba_sh4_diff_blocks_here(unsigned max_blocks, char out[][48],
  unsigned max_lines)
{
  return cgba_sh4_diff_blocks_core(max_blocks, out, max_lines, 0);
}
