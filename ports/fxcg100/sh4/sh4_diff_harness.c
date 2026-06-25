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

/* Snapshot of all state the diff compares (kept in the high-RAM arena). */
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
static cgba_machine_snapshot snap_interp  CGBA_HIGH_BSS_LOCAL;

static void capture(cgba_machine_snapshot *s)
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

static void restore(const cgba_machine_snapshot *s)
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

static int first_block_diff(const u8 *a, const u8 *b, u32 n, u32 *off)
{
  u32 i;
  for (i = 0; i < n; i++)
    if (a[i] != b[i]) { *off = i; return 1; }
  return 0;
}

int cgba_sh4_diff_run(uint32_t cycles, cgba_diff_result *out)
{
  int i;
  u32 off;

  memset(out, 0, sizeof *out);
  out->cycles = cycles;

  /* 1. snapshot the starting state */
  capture(&snap_initial);

  /* 2. reference run: interpreter */
  execute_arm(cycles);
  capture(&snap_interp);

  /* 3. restore and run the dynarec from the identical starting state */
  restore(&snap_initial);
  flush_dynarec_caches();
  execute_arm_translate(cycles);

  /* 4. compare register file first (most actionable) */
  for (i = 0; i < 64; i++) {
    if (reg[i] != snap_interp.reg[i]) {
      out->diverged = 1;
      out->kind = CGBA_DIFF_REG;
      out->index = i;
      out->interp_value = snap_interp.reg[i];
      out->dynarec_value = reg[i];
      return 1;
    }
  }

  /* 5. compare the touched memory regions */
  if (first_block_diff(iwram, snap_interp.iwram, sizeof snap_interp.iwram, &off)) {
    out->diverged = 1; out->kind = CGBA_DIFF_IWRAM; out->index = (int)off;
    out->interp_value = snap_interp.iwram[off]; out->dynarec_value = iwram[off];
    return 1;
  }
  if (first_block_diff(ewram, snap_interp.ewram, sizeof snap_interp.ewram, &off)) {
    out->diverged = 1; out->kind = CGBA_DIFF_EWRAM; out->index = (int)off;
    out->interp_value = snap_interp.ewram[off]; out->dynarec_value = ewram[off];
    return 1;
  }
  if (first_block_diff((u8 *)io_registers, (u8 *)snap_interp.io_registers,
                       sizeof snap_interp.io_registers, &off)) {
    out->diverged = 1; out->kind = CGBA_DIFF_IO; out->index = (int)off;
    return 1;
  }

  out->diverged = 0;
  return 0;
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
