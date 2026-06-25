/*
 * Differential interp-vs-dynarec harness (subtask 2).
 *
 * The interpreter (execute_arm) is the correctness oracle. To validate the SH4
 * dynarec we run the SAME starting machine state through both cores for an
 * identical cycle budget and diff the result. Any mismatch localizes a dynarec
 * bug to the run window; the harness reports the first divergent register/region.
 *
 * Bring-up scope: we snapshot the CPU register file plus the guest memory
 * regions a translated block can touch, run each core from the snapshot (the
 * dynarec caches are flushed so it re-translates against the restored memory),
 * and compare. Peripheral timing state (timers/video counters) is not snapshot,
 * so keep diff windows short (a handful of blocks) to avoid event-timing noise.
 *
 * Driven by the live `dynarec_enable` toggle: cgba_gpsp_run_frame() dispatches
 * to execute_arm or execute_arm_translate; cgba_sh4_diff_run() runs the A/B
 * comparison on demand.
 */

#include <string.h>

#include "vendor/gpsp/common.h"
#include "vendor/gpsp/cpu.h"
#include "vendor/gpsp/gba_memory.h"
#include "ports/fxcg100/sh4/sh4_diff_harness.h"

void execute_arm(u32 cycles);              /* interpreter (cpu.cc) */
u32  execute_arm_translate(u32 cycles);    /* dynarec (sh4_interp_helpers.c) */

#define CGBA_HIGH_BSS_LOCAL __attribute__((section(".cgba.highbss"), aligned(32)))

/* Full starting-state snapshot, used to rewind between the interpreter and the
 * dynarec run so both execute from identical state. Just ONE copy lives in the
 * high-RAM arena; the interpreter RESULT is compared via reg[] (full, to
 * localize the divergent register) plus per-region FNV-1a hashes (cheap, no
 * second multi-hundred-KB buffer). */
typedef struct {
  u32 reg[64];
  u32 spsr[6];
  u32 reg_mode[7][7];
  u16 palette_ram[512];
  u16 oam_ram[512];
  u16 io_registers[512];
  u8  vram[1024 * 96];
  u8  iwram[1024 * 32 * 2];
  u8  ewram[1024 * 256 * 2];
} cgba_machine_snapshot;

static cgba_machine_snapshot snap_initial CGBA_HIGH_BSS_LOCAL;

/* Interpreter result (the oracle): full reg[] + region hashes. */
static u32 oracle_reg[64];
static u32 oracle_h_iwram, oracle_h_ewram, oracle_h_vram, oracle_h_io;

static void capture_full(cgba_machine_snapshot *s)
{
  memcpy(s->reg, reg, sizeof s->reg);
  memcpy(s->spsr, spsr, sizeof s->spsr);
  memcpy(s->reg_mode, reg_mode, sizeof s->reg_mode);
  memcpy(s->palette_ram, palette_ram, sizeof s->palette_ram);
  memcpy(s->oam_ram, oam_ram, sizeof s->oam_ram);
  memcpy(s->io_registers, io_registers, sizeof s->io_registers);
  memcpy(s->vram, vram, sizeof s->vram);
  memcpy(s->iwram, iwram, sizeof s->iwram);
  memcpy(s->ewram, ewram, sizeof s->ewram);
}

static void restore_full(const cgba_machine_snapshot *s)
{
  memcpy(reg, s->reg, sizeof s->reg);
  memcpy(spsr, s->spsr, sizeof s->spsr);
  memcpy(reg_mode, s->reg_mode, sizeof s->reg_mode);
  memcpy(palette_ram, s->palette_ram, sizeof s->palette_ram);
  memcpy(oam_ram, s->oam_ram, sizeof s->oam_ram);
  memcpy(io_registers, s->io_registers, sizeof s->io_registers);
  memcpy(vram, s->vram, sizeof s->vram);
  memcpy(iwram, s->iwram, sizeof s->iwram);
  memcpy(ewram, s->ewram, sizeof s->ewram);
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
  capture_full(&snap_initial);

  /* 2. reference run: interpreter -> oracle reg[] + region hashes */
  execute_arm(cycles);
  out->interp_pc = reg[REG_PC];
  memcpy(oracle_reg, reg, sizeof oracle_reg);
  oracle_h_iwram = fnv1a(iwram, 1024 * 32 * 2);
  oracle_h_ewram = fnv1a(ewram, 1024 * 256 * 2);
  oracle_h_vram  = fnv1a(vram, 1024 * 96);
  oracle_h_io    = fnv1a(io_registers, sizeof io_registers);

  /* 3. rewind and run the dynarec from the identical starting state */
  restore_full(&snap_initial);
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
