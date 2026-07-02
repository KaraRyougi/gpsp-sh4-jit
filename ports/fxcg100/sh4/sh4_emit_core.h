#ifndef CGBA_SH4_EMIT_CORE_H
#define CGBA_SH4_EMIT_CORE_H

/*
 * Core SH-4A code-generation machinery for the cgba dynarec, layered on the
 * verified instruction encoder (sh4_codegen.h):
 *
 *   - the host register model the dynarec reserves,
 *   - guest reg[] load/store addressing (the MVP keeps every guest ARM
 *     register in the reg[] state array; nothing is pinned yet),
 *   - 32-bit constant materialization via per-block PC-relative literal pools.
 *
 * The literal pool is the central SH4 codegen problem: there is no 32-bit
 * immediate move, so each constant is loaded with `MOV.L @(disp,PC),Rn` from a
 * 4-byte-aligned pool that must sit within +1020 bytes, forward of the load.
 * We emit loads with a placeholder displacement, remember the site, and
 * back-patch displacements when the pool is flushed (block end, or earlier when
 * the distance is about to exceed the encodable range).
 *
 * This header depends only on sh4_codegen.h and is exercised by
 * tests/sh4_emit_core_audit.c on the host.
 */

#include "ports/fxcg100/sh4/sh4_codegen.h"

/* ---- host (SH4) register model reserved by the dynarec ---- */
/* R15 = host SP (hardware). R0 kept free (forced index/operand). */
#define SH4_REG_BASE     14   /* reg[] base pointer (callee-saved)        */
#define SH4_REG_CYCLES   13   /* guest cycle counter (callee-saved)       */
#define SH4_REG_CPSR      8   /* cached guest CPSR (callee-saved). While
                                 generated or stub code runs, R8 is the
                                 authoritative reg[REG_CPSR]; memory is synced
                                 by the stub funnels and helper trampolines
                                 around every C call (sh4_stub.S).           */
#define SH4_REG_RET       0   /* C-call return / general scratch          */
#define SH4_REG_T0        1   /* scratch temporaries                      */
#define SH4_REG_T1        2
#define SH4_REG_T2        3
#define SH4_REG_ARG0      4   /* C-call argument registers                */
#define SH4_REG_ARG1      5
#define SH4_REG_ARG2      6
#define SH4_REG_ARG3      7

/* ---- guest reg[] indices (mirror of vendor/gpsp/cpu.h ext_reg_numbers) ---- */
#define SH4_GREG_SP        13
#define SH4_GREG_LR        14
#define SH4_GREG_PC        15
#define SH4_GREG_CPSR      16
#define SH4_GREG_CPU_MODE  17
#define SH4_GREG_HALT      18
#define SH4_GREG_N_FLAG    20
#define SH4_GREG_Z_FLAG    21
#define SH4_GREG_C_FLAG    22
#define SH4_GREG_V_FLAG    23
#define SH4_GREG_SAVE      26

/* ---- literal pool ---- */

#ifndef SH4_LITPOOL_MAX
#define SH4_LITPOOL_MAX 128
#endif

typedef struct sh4_litref {
  uint32_t value;   /* constant to materialize                    */
  size_t   site;    /* byte offset of the MOV.L @(disp,PC) opcode */
} sh4_litref;

typedef struct sh4_emitter {
  sh4_codegen *cg;
  uint8_t     *base;                     /* block start (offset origin) */
  sh4_litref   refs[SH4_LITPOOL_MAX];
  int          nrefs;
  int          error;                    /* sticky: pool full / out of range */
} sh4_emitter;

static inline void sh4_emit_init(sh4_emitter *e, sh4_codegen *cg)
{
  e->cg = cg;
  e->base = cg->ptr;
  e->nrefs = 0;
  e->error = 0;
}

static inline size_t sh4_emit_pos(const sh4_emitter *e)
{
  return (size_t)(e->cg->ptr - e->base);
}

/* Emit a 32-bit big-endian word (host endianness on this target). */
static inline void sh4_emit_u32_be(sh4_codegen *cg, uint32_t v)
{
  sh4_emit_u16(cg, (uint16_t)(v >> 16));
  sh4_emit_u16(cg, (uint16_t)(v & 0xFFFF));
}

/*
 * Load a 32-bit constant into host register rn.
 *
 * Small signed values that fit MOV #imm8 are emitted directly (no pool entry).
 * Everything else is a deferred PC-relative load patched at flush time.
 */
static inline void sh4_emit_load_imm32(sh4_emitter *e, uint32_t value, unsigned rn)
{
  if ((int32_t)value >= -128 && (int32_t)value <= 127) {
    sh4_emit_mov_imm(e->cg, (int)(int32_t)value, rn);
    return;
  }

  if (e->nrefs >= SH4_LITPOOL_MAX) {
    e->error = 1;
    return;
  }

  e->refs[e->nrefs].value = value;
  e->refs[e->nrefs].site = sh4_emit_pos(e);
  e->nrefs++;

  /* Placeholder displacement 0; patched in sh4_emit_flush_pool(). */
  sh4_emit_mov_l_load_pc(e->cg, 0, rn);
}

/*
 * Emit the literal pool and back-patch every pending load.
 *
 * Identical constants are de-duplicated into one pool slot. The pool is
 * 4-byte aligned (a NOP is emitted if needed); each load's displacement is
 *   (entry - ((site & ~3) + 4)) / 4
 * which must be 0..255. Sets e->error if a displacement is out of range.
 */
static inline void sh4_emit_flush_pool(sh4_emitter *e)
{
  if (e->nrefs == 0)
    return;

  /* Align pool to a 4-byte boundary. */
  if ((sh4_emit_pos(e) & 3) != 0)
    sh4_emit_nop(e->cg);

  /* Emit unique constants, recording each one's byte offset, then patch. */
  for (int i = 0; i < e->nrefs; i++) {
    size_t entry;
    int j;

    /* Already emitted as part of an earlier (equal-valued) ref? */
    int done = 0;
    for (j = 0; j < i; j++) {
      if (e->refs[j].value == e->refs[i].value) { done = 1; break; }
    }
    if (done)
      continue;

    entry = sh4_emit_pos(e);
    sh4_emit_u32_be(e->cg, e->refs[i].value);

    /* Patch every ref sharing this value. */
    for (j = i; j < e->nrefs; j++) {
      if (e->refs[j].value != e->refs[i].value)
        continue;

      size_t site = e->refs[j].site;
      long disp = ((long)entry - (long)((site & ~(size_t)3) + 4)) / 4;
      if (disp < 0 || disp > 255) {
        e->error = 1;
        continue;
      }
      /* Low byte of the big-endian MOV.L opcode holds the displacement. */
      e->base[site + 1] = (uint8_t)disp;
    }
  }

  e->nrefs = 0;
}

/* ---- guest reg[] access (MVP: everything spilled to reg[] off SH4_REG_BASE) */

/* Byte offset of guest reg[idx] from the base pointer. */
static inline unsigned sh4_greg_off(unsigned idx) { return idx * 4; }

/* Load guest reg[idx] -> host rn. CPSR is register-cached in R8 (a plain MOV,
 * strictly less clobbering than the old R0-indexed form). Other indices use
 * @(disp,base) for the ARM r0..r15 window (offsets 0..60), else @(R0,base)
 * with R0 set to the offset (<=127). */
static inline void sh4_emit_load_greg(sh4_codegen *cg, unsigned idx, unsigned rn)
{
  unsigned off = sh4_greg_off(idx);
  if (idx == SH4_GREG_CPSR) {
    sh4_emit_mov_reg(cg, SH4_REG_CPSR, rn);          /* rn = cached CPSR */
  } else if (off <= 60) {
    sh4_emit_mov_l_load_disp(cg, SH4_REG_BASE, rn, off >> 2);
  } else {
    sh4_emit_mov_imm(cg, (int)off, SH4_REG_RET);     /* R0 = off (<=127) */
    sh4_emit_mov_l_load_r0(cg, SH4_REG_BASE, rn);    /* rn = @(R0,base) */
  }
}

/* Store host rn -> guest reg[idx]. Same addressing rules; CPSR goes to R8. */
static inline void sh4_emit_store_greg(sh4_codegen *cg, unsigned rn, unsigned idx)
{
  unsigned off = sh4_greg_off(idx);
  if (idx == SH4_GREG_CPSR) {
    sh4_emit_mov_reg(cg, rn, SH4_REG_CPSR);          /* cached CPSR = rn */
  } else if (off <= 60) {
    sh4_emit_mov_l_store_disp(cg, rn, SH4_REG_BASE, off >> 2);
  } else {
    sh4_emit_mov_imm(cg, (int)off, SH4_REG_RET);     /* R0 = off (<=127) */
    sh4_emit_mov_l_store_r0(cg, rn, SH4_REG_BASE);   /* @(R0,base) = rn */
  }
}

#endif
