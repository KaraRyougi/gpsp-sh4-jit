#ifndef CGBA_SH4_EMIT_GLUE_H
#define CGBA_SH4_EMIT_GLUE_H

/*
 * Bring-up emission glue for the gpSP SH-4A dynarec backend.
 *
 * cpu_threaded.c drives translation through a raw `u8 *translation_ptr` cursor.
 * The verified instruction encoder (sh4_codegen.h) works on a `sh4_codegen`
 * struct, so every helper here wraps the current cursor in a transient
 * `sh4_codegen`, emits a short fixed sequence, and writes the cursor back via
 * the `u8 **tp` it is handed. Composition is fine: a higher helper calls lower
 * ones, each re-reading *tp.
 *
 * BRING-UP MODEL (correctness deferred to the differential harness):
 *   - All guest ARM registers live in reg[] (load -> op -> store per insn).
 *   - N and Z are materialized in REG_CPSR bits 31/30 (literal-free); C and V
 *     are NOT computed yet (a known follow-up; conditions that read C/V are
 *     wrong until flag synthesis lands).
 *   - 32-bit constants and all far targets use a SELF-CONTAINED inline literal
 *     (MOV.L @(disp,PC) + a BRA/JMP that leaves the literal out of the
 *     execution path). There is no deferred per-block literal pool here, so
 *     nothing is range-limited and there is no cross-macro emitter state.
 *   - Memory, block transfer, multiply-long, PSR, SWAP, SWI and HLE divide
 *     route to C helpers (generate_function_call); data-proc / shifts / simple
 *     branches emit real SH4.
 *
 * Host register model (mirrors sh4_emit_core.h):
 *   R14 = reg[] base, R13 = cycle counter (both callee-saved, persistent),
 *   R0  = forced scratch / C return, R1..R3 = scratch, R4..R7 = C args.
 */

#include "ports/fxcg100/sh4/sh4_codegen.h"
#include "ports/fxcg100/sh4/sh4_emit_core.h"

extern int cgba_sh4_extra_cycles;
extern u8 ws_cyc_seq[16][2];   /* gba_memory.c: GBA wait-state cycle tables, */
extern u8 ws_cyc_nseq[16][2];  /* [region][word?1:0], live under WAITCNT.    */

/* ---- back-patch I-cache sync --------------------------------------------- *
 * All current SH4 patch sites are written while a block is still being emitted,
 * before block_lookup_address_* calls translate_icache_sync() on the full newly
 * emitted range. Re-syncing each tiny BT/BF/BRA patch separately is therefore
 * redundant and very expensive on real SH-4A hardware (OCBWB+SYNCO+ICBI+SYNCO
 * per line).
 *
 * Keep an opt-in safety switch for future code that might patch an already
 * synchronized instruction. Leave it off for hardware performance builds.
 *
 * It is deliberately NOT used by sh4g_patch_jump. That patches the .long target
 * of `MOV.L @(d,PC),R0; JMP @R0` — a LITERAL read as DATA via the operand cache,
 * never fetched as an instruction (the JMP leaves before reaching it). Patch
 * store and MOV.L load are D-cache-coherent on one core, so the read sees the
 * new value with no flush; an OCBWB+ICBI there is pure waste on the hottest
 * (every-chain) path. The host build of the encoder has no caches: no-op. */
#if defined(CGBA_FXCG100) && defined(CGBA_SH4_PATCH_RESYNC)
#include "ports/fxcg100/sh4/sh4_cache.h"
#define SH4G_RESYNC(p, n) cgba_sh4_cache_sync((void *)(p), (void *)((u8 *)(p) + (n)))
#else
#define SH4G_RESYNC(p, n) ((void)0)
#endif

/* ---- transient-cursor wrapper -------------------------------------------- */

/* A single emitted instruction/sequence is tiny; the driver enforces the real
 * cache bound (TRANSLATION_CACHE_LIMIT_THRESHOLD) after every guest insn, so a
 * generous local limit here only guards against a pathological single sequence. */
static inline sh4_codegen sh4g_open(u8 **tp)
{
  sh4_codegen cg;
  cg.ptr = *tp;
  cg.limit = *tp + 1024;
  cg.overflow = 0;
  return cg;
}

static inline void sh4g_close(u8 **tp, sh4_codegen *cg) { *tp = cg->ptr; }

static inline void sh4g_u16(u8 **tp, uint16_t op)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_u16(&cg, op);
  sh4g_close(tp, &cg);
}

/* ---- constant materialization (self-contained) --------------------------- */

/* Load a 32-bit constant into rn. Small signed values use MOV #imm8; otherwise
 * a PC-relative load of an inline literal that is jumped over so it is never
 * executed:
 *     MOV.L @(d,PC), rn
 *     BRA   9f
 *     NOP            (delay slot)
 *     [.long value]  (4-byte aligned)
 *   9:
 */
/* ---- resident vector table (R9) + pinned block exit (R10) ---------------- *
 * cgba_sh4_vec_table (sh4_stub.S) holds the fixed stub/helper/table addresses
 * generated code jumps to or reads constantly. Entry ORDER here must match the
 * .long list in sh4_stub.S exactly. Lowercase members let sh4_emit.h's
 * token-pasting macros map sh4_indirect_branch_##type directly. */
enum {
  SH4G_VEC_pc_redispatch = 0,
  SH4G_VEC_ib_arm,
  SH4G_VEC_ib_thumb,
  SH4G_VEC_ib_dual,
  SH4G_VEC_ib_dual_thumb_current,
  SH4G_VEC_update_gba,
  SH4G_VEC_helper_exit,
  SH4G_VEC_execute_swi,
  SH4G_VEC_cheat_hook,
  SH4G_VEC_hle_div,
  SH4G_VEC_ws_cyc_seq,
  SH4G_VEC_ws_cyc_nseq,
  SH4G_VEC_COUNT                       /* MOV.L disp4 reach caps this at 16 */
};

/* rn = vec_table[idx]; 2 bytes. */
static inline void sh4g_vec_load(u8 **tp, unsigned idx, unsigned rn)
{
  sh4g_u16(tp, (uint16_t)(0x5000 | (rn << 8) | (SH4_REG_VEC << 4) | idx));
}

/* JMP @vec_table[idx]; clobbers R0. 6 bytes (vs 10-12 for a literal far_jmp). */
static inline void sh4g_vec_jmp(u8 **tp, unsigned idx)
{
  sh4g_vec_load(tp, idx, SH4_REG_RET);
  sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_RET << 8)));   /* JMP @R0 */
  sh4g_u16(tp, 0x0009);                                    /* NOP (delay) */
}

/* JSR @vec_table[idx]; clobbers R0. 6 bytes (vs 14-16 for a literal far_call). */
static inline void sh4g_vec_call(u8 **tp, unsigned idx)
{
  sh4g_vec_load(tp, idx, SH4_REG_RET);
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));   /* JSR @R0 */
  sh4g_u16(tp, 0x0009);                                    /* NOP (delay) */
}

/* JMP @R10 (= sh4_block_exit); 4 bytes, no literal, no clobber. */
static inline void sh4g_block_exit_jmp(u8 **tp)
{
  sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_BEXIT << 8))); /* JMP @R10 */
  sh4g_u16(tp, 0x0009);                                    /* NOP (delay) */
}

#ifdef CGBA_GPSP_HEADLESS_TEST
/* Emission-mix counters (headless builds only): where do translated bytes go?
 * Categories overlap deliberately (a fastmem site's inner far_jmp also counts
 * as fjmp) — read them as per-category totals, not a partition. */
extern unsigned long cgba_em_const_small, cgba_em_const_large, cgba_em_const_bytes;
extern unsigned long cgba_em_fcall_n, cgba_em_fcall_bytes;
extern unsigned long cgba_em_fjmp_n, cgba_em_fjmp_bytes;
extern unsigned long cgba_em_pj_n, cgba_em_pj_bytes;
#define SH4G_EMSTAT(expr) (expr)
#else
#define SH4G_EMSTAT(expr) ((void)0)
#endif

static inline void sh4g_const(u8 **tp, uint32_t value, unsigned rn)
{
  if ((int32_t)value >= -128 && (int32_t)value <= 127) {
    SH4G_EMSTAT(cgba_em_const_small++);
    sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | (value & 0xFF)));   /* MOV #imm8 */
    return;
  }

  /* Cheap synthesis tiers before paying for a 10-12B literal island. Every op
   * used here (EXTU.B, NOT, SHLL2/SHLL8/SHLL16) leaves the T bit alone, so
   * these are drop-in safe anywhere a literal was. */
  if (value >= 128 && value <= 255) {                 /* MOV #s8; EXTU.B = 4B */
    SH4G_EMSTAT(cgba_em_const_small++);
    sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | (value & 0xFF)));
    sh4g_u16(tp, (uint16_t)(0x600C | (rn << 8) | (rn << 4)));  /* EXTU.B rn,rn */
    return;
  }
  if ((int32_t)(~value) >= -128 && (int32_t)(~value) <= 127) { /* MOV; NOT = 4B */
    SH4G_EMSTAT(cgba_em_const_small++);
    sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | (~value & 0xFF)));
    sh4g_u16(tp, (uint16_t)(0x6007 | (rn << 8) | (rn << 4)));  /* NOT rn,rn */
    return;
  }
  /* value == s8 << n with n composed from {16,8,2} (T-safe shifts only):
   * MOV #s8 + 1..3 shifts = 4-8B. Covers 0x80000000 (-128<<24), 0x40000
   * (1<<18), SP/PC*4 offsets, etc. The ctz pre-filter keeps this O(1) for
   * the dominant address-literal case — sh4g_const runs tens of millions of
   * times in flush-thrash scenes, so the search itself must stay cheap. */
  if ((value & 3) == 0 && value != 0) {
    static const u8 sh4g_shift_cand[8] = {16, 8, 2, 24, 18, 10, 4, 26};
    unsigned tz = (unsigned)__builtin_ctz(value);
    unsigned k;
    for (k = 0; k < 8; k++) {
      unsigned n = sh4g_shift_cand[k];     /* ordered by shift-op count */
      int32_t sv;
      if (n > tz)
        continue;                          /* low bits would be lost */
      sv = (int32_t)value >> n;            /* n <= tz => (sv << n) == value */
      if (sv >= -128 && sv <= 127) {
        unsigned r;
        SH4G_EMSTAT(cgba_em_const_small++);
        sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | ((uint32_t)sv & 0xFF)));
        for (r = n; r >= 16; r -= 16)
          sh4g_u16(tp, (uint16_t)(0x4028 | (rn << 8)));        /* SHLL16 */
        for (; r >= 8; r -= 8)
          sh4g_u16(tp, (uint16_t)(0x4018 | (rn << 8)));        /* SHLL8 */
        for (; r >= 2; r -= 2)
          sh4g_u16(tp, (uint16_t)(0x4008 | (rn << 8)));        /* SHLL2 */
        return;
      }
    }
  }

  {
    u8 *load = *tp;                 /* MOV.L site */
    u8 *lit;
    long bra_disp, ld_disp;

    /* Reserve: MOV.L(2) BRA(2) NOP(2) then literal 4-aligned. */
    lit = (u8 *)(((uintptr_t)(load + 6) + 3u) & ~(uintptr_t)3u);

    ld_disp = ((long)lit - (((long)load & ~3L) + 4)) / 4;     /* MOV.L disp */
    sh4g_u16(tp, (uint16_t)(0xD000 | (rn << 8) | (ld_disp & 0xFF)));

    /* BRA to just past the literal; delay-slot NOP follows. */
    bra_disp = ((long)(lit + 4) - ((long)(*tp) + 4)) / 2;
    sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));
    sh4g_u16(tp, 0x0009);                                     /* NOP (delay) */

    while (*tp < lit)                                         /* align pad */
      sh4g_u16(tp, 0x0009);

    {                                                         /* .long value */
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_u32_be(&cg, value);
      sh4g_close(tp, &cg);
    }
    SH4G_EMSTAT((cgba_em_const_large++, cgba_em_const_bytes += (unsigned long)(*tp - load)));
  }
}

/* small two-operand emitters used by a few handlers */
static inline void sh4g_add_reg(u8 **tp, unsigned rm, unsigned rn)
{ sh4_codegen cg = sh4g_open(tp); sh4_emit_add_reg(&cg, rm, rn); sh4g_close(tp, &cg); }
static inline void sh4g_sub_reg(u8 **tp, unsigned rm, unsigned rn)
{ sh4_codegen cg = sh4g_open(tp); sh4_emit_sub(&cg, rm, rn); sh4g_close(tp, &cg); }
static inline void sh4g_mov_reg(u8 **tp, unsigned rm, unsigned rn)
{ sh4_codegen cg = sh4g_open(tp); sh4_emit_mov_reg(&cg, rm, rn); sh4g_close(tp, &cg); }

/* ---- guest reg[] access -------------------------------------------------- */

static inline void sh4g_load_greg(u8 **tp, unsigned idx, unsigned rn)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_load_greg(&cg, idx, rn);
  sh4g_close(tp, &cg);
}

static inline void sh4g_store_greg(u8 **tp, unsigned rn, unsigned idx)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_store_greg(&cg, rn, idx);
  sh4g_close(tp, &cg);
}

/* ---- far call / jump via inline literal ---------------------------------- */

/* JSR @literal(fn) then return: literal is skipped on return via BRA. */
static inline void sh4g_far_call(u8 **tp, const void *fn)
{
  u8 *load = *tp, *lit;
  SH4G_EMSTAT(cgba_em_fcall_n++);
  long ld_disp, bra_disp;

  /* MOV.L(2) JSR(2) NOP(2) BRA(2) NOP(2) [pad] .long ; 9: */
  lit = (u8 *)(((uintptr_t)(load + 10) + 3u) & ~(uintptr_t)3u);

  ld_disp = ((long)lit - (((long)load & ~3L) + 4)) / 4;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | (ld_disp & 0xFF))); /* MOV.L @(d,PC),R0 */
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));                    /* JSR @R0 */
  sh4g_u16(tp, 0x0009);                                                     /* NOP (delay) */
  bra_disp = ((long)(lit + 4) - ((long)(*tp) + 4)) / 2;
  sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));                   /* BRA 9f */
  sh4g_u16(tp, 0x0009);                                                     /* NOP (delay) */
  while (*tp < lit)
    sh4g_u16(tp, 0x0009);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (uint32_t)(uintptr_t)fn);
    sh4g_close(tp, &cg);
  }
  SH4G_EMSTAT(cgba_em_fcall_bytes += (unsigned long)(*tp - load));
}

/* JMP @literal(fn), no return; literal sits after the (taken) JMP. */
static inline void sh4g_far_jmp(u8 **tp, const void *fn)
{
  u8 *load = *tp, *lit;
  SH4G_EMSTAT(cgba_em_fjmp_n++);
  long ld_disp;

  lit = (u8 *)(((uintptr_t)(load + 6) + 3u) & ~(uintptr_t)3u);
  ld_disp = ((long)lit - (((long)load & ~3L) + 4)) / 4;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | (ld_disp & 0xFF))); /* MOV.L @(d,PC),R0 */
  sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_RET << 8)));                    /* JMP @R0 */
  sh4g_u16(tp, 0x0009);                                                     /* NOP (delay) */
  while (*tp < lit)
    sh4g_u16(tp, 0x0009);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (uint32_t)(uintptr_t)fn);
    sh4g_close(tp, &cg);
  }
  SH4G_EMSTAT(cgba_em_fjmp_bytes += (unsigned long)(*tp - load));
}

/* Thumb BX almost always returns to another Thumb block in GBA games. Split that
 * hot subcase before the generic dual resolver so the runtime hit path can avoid
 * the ARM/Thumb mode split and redundant CPSR.T store. R4/ARG0 remains the target.
 * TST #1,R0 sets T when the target is ARM (bit clear), so BT skips to generic. */
static inline void sh4g_thumb_bx_dispatch(u8 **tp)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_u16(tp, (uint16_t)(0xC800 | 0x01));                 /* TST #1,R0 */
  bt = *tp;
  sh4g_u16(tp, 0x8900);                                    /* BT generic */
  sh4g_vec_jmp(tp, SH4G_VEC_ib_dual_thumb_current);        /* Thumb target */
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
  sh4g_vec_jmp(tp, SH4G_VEC_ib_dual);                      /* ARM target */
}

/* ---- compact C-helper call site ------------------------------------------ *
 * Fixed-shape call through a shared stub trampoline (sh4_stub.S): the
 * (trampoline, fn, opcode, pc[, cycle_count]) tuple lives in ONE literal block
 * after the call and the trampoline reads it via PR, then handles the memory
 * debit / PC redispatch generically. ~28-32 bytes per site vs ~70 for the old
 * inline glue — helper-heavy ARM gameplay blocks shrink ~2x, which is ROM
 * translation cache CAPACITY (the flush-thrash fix), not just speed.
 *
 * Layout (the pad pins site%4 == 2 so the literal block is 4-aligned and the
 * MOV.L displacement is a constant 2):
 *   [pad NOP]  mov.l L,r0 ; jsr @r0 ; nop ; bra 2f ; nop
 *   L: .long TRAMP, fn, opcode, pc [, cycle_count]   2:
 */
static inline void sh4g_op2_tramp_call(u8 **tp, const void *tramp,
                                       const void *fn, uint32_t opcode,
                                       uint32_t pc, int with_cycles,
                                       int cycle_count)
{
  u8 *site;
  u8 *lit;
  unsigned nlit = with_cycles ? 5 : 4;
  long bra_disp;

  if (((uintptr_t)*tp + 10) & 3)               /* literals must be 4-aligned */
    sh4g_u16(tp, 0x0009);
  site = *tp;
  lit = site + 10;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | 2));   /* MOV.L L,R0 */
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));       /* JSR @R0    */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  bra_disp = ((long)(lit + nlit * 4) - ((long)(*tp) + 4)) / 2;
  sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));      /* BRA 2f     */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (uint32_t)(uintptr_t)tramp);
    sh4_emit_u32_be(&cg, (uint32_t)(uintptr_t)fn);
    sh4_emit_u32_be(&cg, opcode);
    sh4_emit_u32_be(&cg, pc);
    if (with_cycles)
      sh4_emit_u32_be(&cg, (uint32_t)cycle_count);
    sh4g_close(tp, &cg);
  }
}

/* ---- N/Z/C/V materialization (masked, literal-free) into REG_CPSR -------- */

/* Flag-liveness masks use gpSP's flag_status low-nibble convention:
 *   N = 0x8, Z = 0x4, C = 0x2, V = 0x1  (x86_emit.h check_generate_*).
 * A cleared bit means the flag is DEAD at this instruction — no later reader
 * before the next writer on any path (thumb_dead_flag_eliminate /
 * arm_flag_status + arm_dead_flag_eliminate) — so codegen may skip producing
 * it. Producing a dead flag with its correct value is always safe, which lets
 * the writer round a sparse mask UP to the nearest top-contiguous set
 * {0x8, 0xC, 0xE, 0xF} whose bits it can clear with the literal-free shift
 * trick. Callers must compute inputs (carry/overflow regs) for the ROUNDED
 * mask, so round first, then compute, then write. */
static inline u32 sh4g_flags_round(u32 mask)
{
  if (!mask)       return 0;
  if (mask & 0x1)  return 0xF;
  if (mask & 0x2)  return 0xE;
  if (mask & 0x4)  return 0xC;
  return 0x8;
}

/* Write the flags selected by `eff` (a ROUNDED mask: 0/0x8/0xC/0xE/0xF) into
 * reg[REG_CPSR], preserving the rest. N/Z come from `result`; C/V come from
 * 0/1 values in rc/rv, read (not clobbered) only when their bit is set — pass
 * anything for unused ones. rc/rv must not be R0/R2/R3. Clobbers R0/R2/R3 and
 * leaves T = N (callers treat T as scratch; sh4g_cond_to_T recomputes).
 *
 * ROTCR insertion: pre-shift CPSR left by the live-flag count (dropping the
 * old bits), then rotate each new flag in through T from the LOWEST live bit
 * up, so the last insert lands N at bit31 and the preserved bits return to
 * place. 12/16/24/30 bytes for N/NZ/NZC/NZCV vs 18/28/40/48 for the old
 * mask-build + OR-merge chains. */
static inline void sh4g_set_flags(u8 **tp, unsigned result, unsigned rc,
                                  unsigned rv, u32 eff)
{
  sh4_codegen cg;
  unsigned r_cpsr = SH4_REG_T1;               /* R2 */
  unsigned r_tmp  = SH4_REG_T2;               /* R3 */

  if (!eff)
    return;
  cg = sh4g_open(tp);
  sh4_emit_mov_reg(&cg, SH4_REG_CPSR, r_cpsr);   /* cached CPSR (R8) */
  switch (eff) {                              /* drop the live top bits */
  case 0x8:
    sh4_emit_shll(&cg, r_cpsr);  break;
  case 0xC:
    sh4_emit_shll2(&cg, r_cpsr); break;
  case 0xE:
    sh4_emit_shll2(&cg, r_cpsr); sh4_emit_shll(&cg, r_cpsr); break;
  default:                                    /* 0xF */
    sh4_emit_shll2(&cg, r_cpsr); sh4_emit_shll2(&cg, r_cpsr); break;
  }

  if (eff & 0x1) {                            /* V in first (ends at bit28) */
    sh4_emit_mov_reg(&cg, rv, r_tmp);
    sh4_emit_shlr(&cg, r_tmp);                /* T = rv (0/1) */
    sh4_emit_rotcr(&cg, r_cpsr);
  }
  if (eff & 0x2) {                            /* C */
    sh4_emit_mov_reg(&cg, rc, r_tmp);
    sh4_emit_shlr(&cg, r_tmp);                /* T = rc (0/1) */
    sh4_emit_rotcr(&cg, r_cpsr);
  }
  if (eff & 0x4) {                            /* Z */
    sh4_emit_tst(&cg, result, result);        /* T = (result == 0) */
    sh4_emit_rotcr(&cg, r_cpsr);
  }
  /* N last: lands at bit31 */
  sh4_emit_mov_reg(&cg, result, r_tmp);
  sh4_emit_shll(&cg, r_tmp);                  /* T = result bit31 */
  sh4_emit_rotcr(&cg, r_cpsr);
  sh4_emit_mov_reg(&cg, r_cpsr, SH4_REG_CPSR);   /* commit cached CPSR */
  sh4g_close(tp, &cg);
}

/* Historical entry points, kept for the paths that always want the flags. */
static inline void sh4g_set_nz(u8 **tp, unsigned result)
{
  sh4g_set_flags(tp, result, SH4_REG_ARG0, SH4_REG_ARG0, 0xC);
}

static inline void sh4g_set_nz_m(u8 **tp, unsigned result, u32 mask)
{
  sh4g_set_flags(tp, result, SH4_REG_ARG0, SH4_REG_ARG0,
                 sh4g_flags_round(mask & 0xC));
}

static inline void sh4g_set_nzc(u8 **tp, unsigned result, unsigned carry)
{
  sh4g_set_flags(tp, result, carry, carry, 0xE);
}

/* ---- data-processing core ------------------------------------------------ */

enum {
  SH4DP_AND, SH4DP_ORR, SH4DP_EOR, SH4DP_BIC, SH4DP_ADD, SH4DP_SUB,
  SH4DP_RSB, SH4DP_MOV, SH4DP_MVN, SH4DP_NEG, SH4DP_ADC, SH4DP_SBC,
  SH4DP_RSC, SH4DP_CMP, SH4DP_CMN, SH4DP_TST, SH4DP_TEQ, SH4DP_MUL
};

/* ops that consume only the second operand (no Rn load) */
static inline int sh4g_dp_second_only(int op)
{ return op == SH4DP_MOV || op == SH4DP_MVN || op == SH4DP_NEG; }

/* Compute (op of Rn-value, second) into R1 (result). first in R1, second in R2.
 * C/V are not produced (bring-up). */
static inline void sh4g_dp_compute(sh4_codegen *cg, int op)
{
  const unsigned a = SH4_REG_T0; /* R1 = first / result */
  const unsigned b = SH4_REG_T1; /* R2 = second */

  switch (op) {
  case SH4DP_AND: case SH4DP_TST: sh4_emit_and(cg, b, a); break;
  case SH4DP_ORR:                 sh4_emit_or(cg, b, a);  break;
  case SH4DP_EOR: case SH4DP_TEQ: sh4_emit_xor(cg, b, a); break;
  case SH4DP_BIC: sh4_emit_not(cg, b, b); sh4_emit_and(cg, b, a); break;
  case SH4DP_ADD: case SH4DP_CMN: case SH4DP_ADC:
                  sh4_emit_add_reg(cg, b, a); break;
  case SH4DP_SUB: case SH4DP_CMP: case SH4DP_SBC:
                  sh4_emit_sub(cg, b, a); break;       /* a = a - b */
  case SH4DP_RSB: case SH4DP_RSC:
                  sh4_emit_sub(cg, a, b);              /* b = b - a */
                  sh4_emit_mov_reg(cg, b, a); break;   /* result in a */
  case SH4DP_MUL: sh4_emit_mul_l(cg, b, a); sh4_emit_sts_macl(cg, a); break;
  case SH4DP_MOV: sh4_emit_mov_reg(cg, b, a); break;   /* result = second */
  case SH4DP_MVN: sh4_emit_not(cg, b, a); break;       /* result = ~second */
  case SH4DP_NEG: sh4_emit_neg(cg, b, a); break;       /* result = -second */
  default: break;
  }
}

static inline int sh4g_dp_is_test(int op)
{
  return op == SH4DP_CMP || op == SH4DP_CMN || op == SH4DP_TST || op == SH4DP_TEQ;
}

/* ---- shifts (LSL/LSR/ASR/ROR by imm or reg) ------------------------------ */
enum { SH4SH_LSL, SH4SH_LSR, SH4SH_ASR, SH4SH_ROR };

/* fmask = live-flag mask (N=8 Z=4 C=2 V=1); the shifter carry is computed only
 * when C is live, which drops 6 instructions from the common dead-flag case. */
static inline void sh4g_shift_imm(u8 **tp, int kind, unsigned rd, unsigned rs,
                                  unsigned imm5, u32 fmask)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned carry = SH4_REG_ARG0;
  int preserves_c = (kind == SH4SH_LSL && imm5 == 0);
  int want_c = (fmask & 0x2) && !preserves_c;
  sh4_emit_load_greg(&cg, rs, SH4_REG_T0);
  if (imm5 != 0) {
    if (want_c) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, carry);
      sh4_emit_mov_imm(&cg,
        (kind == SH4SH_LSL) ? -(int)(32 - imm5) : -(int)(imm5 - 1),
        SH4_REG_RET);
      sh4_emit_shld(&cg, SH4_REG_RET, carry);
      sh4_emit_mov_reg(&cg, carry, SH4_REG_RET);
      sh4_emit_and_imm(&cg, 1);
      sh4_emit_mov_reg(&cg, SH4_REG_RET, carry);
    }
    sh4_emit_mov_imm(&cg, (kind == SH4SH_LSL) ? (int)imm5 : -(int)imm5,
      SH4_REG_RET);
    if (kind == SH4SH_ASR)
      sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
    else                                              /* LSL / LSR */
      sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T0);
  } else if (kind == SH4SH_LSR) {
    /* Thumb LSR #0 means LSR #32 -> result is 0. */
    if (want_c) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, carry);
      sh4_emit_shll(&cg, carry);
      sh4_emit_movt(&cg, carry);
    }
    sh4_emit_mov_imm(&cg, 0, SH4_REG_T0);
  } else if (kind == SH4SH_ASR) {
    /* Thumb ASR #0 means ASR #32 -> sign extension (0 or -1). */
    if (want_c) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, carry);
      sh4_emit_shll(&cg, carry);
      sh4_emit_movt(&cg, carry);
    }
    sh4_emit_mov_imm(&cg, -31, SH4_REG_RET);
    sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
  }                                                   /* LSL #0 is a no-op */
  sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
  sh4g_close(tp, &cg);
  sh4g_set_flags(tp, SH4_REG_T0, carry, carry,
                 sh4g_flags_round(fmask & (want_c ? 0xE : 0xC)));
}

/* Register-amount shifts (LSL/LSR/ASR/ROR Rd,Rs) are routed to a C helper
 * (cgba_sh4_thumb_shift_reg): SH4 SHLD/SHAD only use the low 5 bits + sign of
 * the count, and ROR is not a logical shift, so the full ARM semantics + carry
 * cannot be expressed by a single SH4 op. */

/* ---- cycle counter ------------------------------------------------------- */

static inline void sh4g_cycle_debit(u8 **tp, int n)
{
  if (n == 0)
    return;
  if (n >= -128 && n <= 127) {
    sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-n) & 0xFF))); /* ADD #-n */
  } else {
    sh4g_const(tp, (uint32_t)(-n), SH4_REG_T0);
    sh4g_u16(tp, (uint16_t)(0x300C | (SH4_REG_CYCLES << 8) | (SH4_REG_T0 << 4))); /* ADD R1 */
  }
}

static inline void sh4g_cycle_debit_from_global(u8 **tp, const int *extra_cycles)
{
  sh4g_const(tp, (uint32_t)(uintptr_t)extra_cycles, SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T0, SH4_REG_T0);
    sh4_emit_sub(&cg, SH4_REG_T0, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
}

/* Charge `count` guest memory accesses to the cycle counter at RUNTIME, reading
 * the SAME ws_cyc_{seq,nseq}[region][word] table the C helpers use
 * (cgba_sh4_charge_mem in sh4_interp_helpers.c) so a native INLINE fast path
 * debits exactly what its slow (C-helper) path would. The interpreter charges
 * block (LDM/STM) transfers sequentially and single transfers nonsequentially,
 * word vs byte/halfword by column; this mirrors that.
 *
 * `addr_reg` holds a guest address in the accessed region (region = addr >> 24)
 * and must not be R0/T1/T2. `seq` selects the seq vs nonseq table; `is_word`
 * selects column 1 (32-bit) vs 0 (8/16-bit). Clobbers R0/T1/T2 (and T0 when
 * count > 1); only the cycle counter R13 changes. */
static inline void sh4g_charge_mem_run(u8 **tp, unsigned addr_reg, int seq,
                                       int is_word, unsigned count)
{
  if (count == 0)
    return;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, addr_reg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);            /* R0 = region = addr >> 24      */
    sh4_emit_shll(&cg, SH4_REG_RET);             /* R0 = region * 2 (u8[16][2] row)*/
    if (is_word)
      sh4_emit_add_imm(&cg, 1, SH4_REG_RET);     /* + word column                 */
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, seq ? SH4G_VEC_ws_cyc_seq : SH4G_VEC_ws_cyc_nseq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);  /* T1 = table[region][col]*/
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);         /* zero-extend cost (>=0) */
    if (count > 1) {
      sh4_emit_mov_imm(&cg, (int)count, SH4_REG_T0);
      sh4_emit_mul_l(&cg, SH4_REG_T1, SH4_REG_T0);        /* MACL = cost * count    */
      sh4_emit_sts_macl(&cg, SH4_REG_T1);                 /* T1 = cost * count      */
    }
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);        /* R13 -= cost * count    */
    sh4g_close(tp, &cg); }
}

/* Thumb BX-to-Thumb reaches the normal end-of-Thumb-instruction accounting in
 * the interpreter, so its sequential fetch is charged from the target region.
 * Thumb BX-to-ARM jumps straight to arm_loop and leaves the target fetch to the
 * ARM stream. `target_reg` is preserved; clobbers R0/T1/T2. */
static inline void sh4g_charge_thumb_bx_target_fetch(u8 **tp, unsigned target_reg)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, target_reg, SH4_REG_RET);      /* R0 = target            */
    sh4g_close(tp, &cg); }
  sh4g_u16(tp, (uint16_t)(0xC800 | 0x01));               /* TST #1,R0 -> T=ARM target*/
  bt = *tp;
  sh4g_u16(tp, 0x8900);                                  /* BT skip (ARM target)   */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);                    /* R0 = target >> 24      */
    sh4_emit_shll(&cg, SH4_REG_RET);                     /* R0 = region * 2        */
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_seq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1); /* seq[region][halfword]  */
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* Charge ARM BX / computed-PC timing at RUNTIME to match the interpreter. An
 * ARM target costs the nonsequential refill plus the target sequential fetch:
 * cpu.cc falls through skip_instruction after the refill. A Thumb target (bit 0
 * set) costs nothing -- cpu.cc jumps straight into thumb_loop before either
 * charge. `target_reg` is preserved; clobbers R0/T1/T2. */
static inline void sh4g_charge_indirect_refill(u8 **tp, unsigned target_reg)
{
  u8 *bf;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, target_reg, SH4_REG_RET);      /* R0 = target            */
    sh4g_close(tp, &cg); }
  sh4g_u16(tp, (uint16_t)(0xC800 | 0x01));               /* TST #1,R0 -> T=ARM target*/
  bf = *tp;
  sh4g_u16(tp, 0x8B00);                                  /* BF skip (Thumb: no refill)*/
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);                    /* R0 = target >> 24 (region)*/
    sh4_emit_shll(&cg, SH4_REG_RET);                     /* R0 = region * 2          */
    sh4_emit_add_imm(&cg, 1, SH4_REG_RET);               /* + word column            */
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_nseq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1); /* T1 = ws_cyc_nseq[reg][1] */
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);       /* R13 -= refill            */
    sh4g_close(tp, &cg); }
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_seq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1); /* T1 = ws_cyc_seq[reg][1]  */
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);       /* R13 -= target fetch      */
    sh4g_close(tp, &cg); }
  { long d = ((long)(*tp) - ((long)bf + 4)) / 2; bf[1] = (uint8_t)(d & 0xFF); }
}

static inline void sh4g_cycle_sub(u8 **tp, int n, uint32_t pc)
{
  u8 *bt;
  if (n == 0)
    return;
  sh4g_cycle_debit(tp, n);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);      /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                        /* BT skip update */
  sh4g_const(tp, pc, SH4_REG_ARG0);
  sh4g_block_exit_jmp(tp);
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* Loop-break GATE: exit to update_gba() if the cycle counter has gone negative,
 * so a wait/idle loop can't spin past its budget.
 * Placed AT a branch-target block entry (loop-back lands here). The accounting
 * flush is a separate, earlier step that loop-back deliberately bypasses. On
 * the exhausted path we still charge the target fetch before update_gba(),
 * matching the interpreter's taken-branch refill + sequential target fetch
 * before it checks the event boundary. */
static inline void sh4g_charge_fetch_cell(u8 **tp, uint32_t pc, int is_word)
{
  int cell_off = (int)(((pc >> 24) & 0x0F) * 2 + (is_word ? 1 : 0));
  sh4g_vec_load(tp, SH4G_VEC_ws_cyc_seq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_imm(&cg, cell_off, SH4_REG_T2);   /* &seq[region][col] */
    sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
}

static inline void sh4g_cycle_gate(u8 **tp, uint32_t pc, int is_word)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);       /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                        /* BT skip (budget remains) */
  sh4g_charge_fetch_cell(tp, pc, is_word);
  sh4g_const(tp, pc, SH4_REG_ARG0);
  sh4g_block_exit_jmp(tp);
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* ---- branch exit (patchable far jump) ------------------------------------ *
 * Emits a fixed self-contained jump whose target literal is back-patched by
 * sh4g_patch_jump(). Returns the patch site (the MOV.L address).             */
static inline u8 *sh4g_emit_patch_jump(u8 **tp)
{
  u8 *site = *tp, *lit;
  long ld_disp;

  lit = (u8 *)(((uintptr_t)(site + 6) + 3u) & ~(uintptr_t)3u);
  ld_disp = ((long)lit - (((long)site & ~3L) + 4)) / 4;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | (ld_disp & 0xFF)));   /* MOV.L @(d,PC),R0 */
  sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_RET << 8)));                      /* JMP @R0 */
  sh4g_u16(tp, 0x0009);                                                       /* NOP */
  while (*tp < lit)
    sh4g_u16(tp, 0x0009);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, 0);                                                  /* target placeholder */
    sh4g_close(tp, &cg);
  }
  SH4G_EMSTAT((cgba_em_pj_n++, cgba_em_pj_bytes += (unsigned long)(*tp - site)));
  return site;
}

/* Patch the literal of a jump emitted by sh4g_emit_patch_jump at `site`. */
static inline void sh4g_patch_jump(u8 *site, const void *target)
{
  u8 *lit = (u8 *)(((uintptr_t)(site + 6) + 3u) & ~(uintptr_t)3u);
  lit[0] = (uint8_t)((uintptr_t)target >> 24);
  lit[1] = (uint8_t)((uintptr_t)target >> 16);
  lit[2] = (uint8_t)((uintptr_t)target >> 8);
  lit[3] = (uint8_t)((uintptr_t)target);
  /* No I-cache re-sync: the literal is read as data (D-cache-coherent), never
   * executed as an instruction. See the SH4G_RESYNC note above. */
}

/* Chain-patch a sh4g_emit_patch_jump site. A near target (BRA disp12 reach,
 * +-4 KiB) overwrites the MOV.L/JMP pair with a direct BRA + delay NOP: the hot
 * loop back-edge and most intra-cache chains become one predicted branch
 * instead of a literal D-load feeding an indirect JMP @R0. Far targets keep the
 * literal form. Chain sites are patched at most twice (the emit-time default to
 * sh4_block_exit stays a far literal; the driver then aims it once at the real
 * target), always before the enclosing block's I-cache sync, so rewriting
 * instruction bytes here needs no extra sync. */
static inline void sh4g_chain_patch(u8 *site, const void *target)
{
  long d = ((long)(uintptr_t)target - ((long)(uintptr_t)site + 4)) / 2;
  unsigned form = site[0] & 0xF0;
  if (d >= -2048 && d <= 2047 && (form == 0xD0 || form == 0xA0)) {
    site[0] = (uint8_t)(0xA0 | ((d >> 8) & 0x0F));
    site[1] = (uint8_t)(d & 0xFF);
    site[2] = 0x00;                  /* NOP in the delay slot (replaces JMP) */
    site[3] = 0x09;
    SH4G_RESYNC(site, 4);
    return;
  }
  sh4g_patch_jump(site, target);
}

/* Conditional skip over a predicated body of UNBOUNDED length (the ARM
 * same-condition run, which can be many instructions). sh4g_cond_to_T has left
 * T = (ARM condition satisfied): emit BT over a far (literal) jump, so when the
 * condition is TRUE the BT branches into the body, and when FALSE the far jump
 * is taken to the post-body skip target (back-patched by sh4g_patch_jump at run
 * close). Unlike a disp8/disp12 branch this reaches any distance, so a long run
 * can never wrap the skip target. The BT itself only hops the fixed-size jump,
 * so its disp8 is always tiny. Returns the literal patch site. */
static inline u8 *sh4g_emit_cond_skip_far(u8 **tp)
{
  u8 *bt = *tp, *site;
  sh4g_u16(tp, 0x8900);                          /* BT body (cond true)         */
  site = sh4g_emit_patch_jump(tp);               /* cond false -> jump to target */
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
  return site;
}

/* Block exit to `new_pc`. R4 is set to new_pc; if the cycle counter is
 * exhausted (R13 <= 0) control leaves to `block_exit_fn` (which commits R4 to
 * reg[REG_PC], processes events and re-dispatches), otherwise it takes a
 * patchable jump that the driver back-patches to the resolved target block for
 * direct chaining (initialized to `block_exit_fn` so the unpatched path is
 * still correct). reg[REG_PC] is deliberately NOT stored here: every consumer
 * of the exhausted/unpatched path (sh4_block_exit, sh4_update_gba) commits R4
 * itself, and the chained path runs straight into the next block, which never
 * reads reg[REG_PC] before the next commit point — dropping the store saves a
 * memory write on every taken branch. Returns the patch site. */
static inline u8 *sh4g_branch_exit(u8 **tp, uint32_t new_pc,
                                   const void *block_exit_fn)
{
  u8 *bt, *site;
  sh4g_const(tp, new_pc, SH4_REG_ARG0);         /* R4 = new_pc */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);        /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                          /* BT over (skip exit if budget left) */
  sh4g_block_exit_jmp(tp);                       /* exhausted: events + redispatch */
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }

  site = sh4g_emit_patch_jump(tp);               /* chain to target block (patched) */
  sh4g_patch_jump(site, block_exit_fn);          /* default: redispatch */
  return site;
}

/* Idle-loop branch exit — the dynarec mirror of the interpreter's idle
 * handling (gba_over.h idle_loop_target_pc + the port's b-to-self detector,
 * cpu.cc) and of arm_emit.h's generate_branch_idle_eliminate: the branch burns
 * the REST of the cycle budget (R13 = 0, only when it is still positive, like
 * the interpreter's `cycles_remaining > 0` guard) and CALLS sh4_update_gba,
 * which commits R4 to reg[REG_PC] and fast-forwards hardware to the next event.
 * When the frame completes or an IRQ redirects the PC the call never returns;
 * otherwise it comes back with a fresh budget and falls through to the normal
 * patchable chain jump, so exactly one loop iteration runs per event slice
 * instead of thousands of spins. Returns the patch site. */
static inline u8 *sh4g_branch_exit_idle(u8 **tp, uint32_t new_pc,
                                        const void *block_exit_fn)
{
  u8 *site;
  sh4g_const(tp, new_pc, SH4_REG_ARG0);              /* R4 = target PC */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);             /* T = (budget > 0) */
    sh4g_close(tp, &cg); }
  sh4g_u16(tp, 0x8B00);                              /* BF +0: keep <=0 budget */
  sh4g_u16(tp, (uint16_t)(0xE000 | (SH4_REG_CYCLES << 8))); /* MOV #0,R13 */
  sh4g_vec_call(tp, SH4G_VEC_update_gba);            /* fast-forward events */
  sh4g_const(tp, new_pc, SH4_REG_ARG0);              /* R4 clobbered by C call */
  site = sh4g_emit_patch_jump(tp);                   /* chain to target block */
  sh4g_patch_jump(site, block_exit_fn);              /* default: full exit */
  return site;
}

/* After a C handler that returns nonzero in R0 when it changed the guest PC
 * (1 = pure PC change, 2 = store alert; see sh4_interp_helpers.c), leave the
 * block: TST R0,R0 ; BT skip ; R4 = reg[REG_PC] ; R1 = code ; jmp helper_exit.
 * sh4_helper_exit (sh4_stub.S) reads the code from R1 and re-dispatches pure PC
 * changes without an update_gba pass; alerts exit through sh4_block_exit. */
static inline void sh4g_redispatch_if_r0(u8 **tp)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T = (R0 == 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                               /* BT skip (no PC change) */
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);      /* R4 = reg[REG_PC] */
  sh4g_mov_reg(tp, SH4_REG_RET, SH4_REG_T0);          /* R1 = helper code */
  sh4g_vec_jmp(tp, SH4G_VEC_helper_exit);
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* Same as sh4g_redispatch_if_r0(), but when the helper returns nonzero it first
 * debits the translated instructions accumulated since the previous cycle gate.
 * The no-PC-change path deliberately keeps accumulating: it skips this debit and
 * the normal block-end/branch gate will flush the full run later. */
static inline void sh4g_redispatch_if_r0_debit(u8 **tp, int cycle_count)
{
  u8 *bt;
  if (cycle_count == 0) {
    sh4g_redispatch_if_r0(tp);
    return;
  }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T = (R0 == 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                              /* BT skip (no PC change) */
  sh4g_cycle_debit(tp, cycle_count);
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);     /* R4 = reg[REG_PC] */
  sh4g_mov_reg(tp, SH4_REG_RET, SH4_REG_T0);         /* R1 = helper code */
  sh4g_vec_jmp(tp, SH4G_VEC_helper_exit);
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* ---- conditional skip (BT/BF over predicated body) ----------------------- *
 * Emit a test that leaves T = (ARM condition satisfied); the header then emits
 * BF to skip the predicated body when the condition is false. CPSR carries the
 * full flag set in its canonical bits (N=31, Z=30, C=29, V=28; written by
 * set_nzcv / the shifter carry-out in sh4_interp_helpers.c), so every ARM
 * condition is evaluated exactly:
 *
 *   single-flag (EQ..VC): T = (CPSR[pos] == want);
 *   compound: build a value whose sign bit (bit31) is the "positive" member of
 *   the pair, then read it with SHLL (T = sign) or CMP/PZ (T = !sign):
 *     HI = C & !Z     (LS = !HI)
 *     LT = N ^ V      (GE = !LT)
 *     LE = Z | (N^V)  (GT = !LE)
 *
 * Scratch: R0 (RET) accumulator, R1 (T0) holds CPSR, R2 (T1) temp.            */
static inline void sh4g_cond_to_T(u8 **tp, unsigned cond)
{
  static const struct { unsigned char pos, want; } cc[8] = {
    {30, 1}, /* EQ: Z set */ {30, 0}, /* NE: Z clear */
    {29, 1}, /* CS: C set */ {29, 0}, /* CC: C clear */
    {31, 1}, /* MI: N set */ {31, 0}, /* PL: N clear */
    {28, 1}, /* VS: V set */ {28, 0}, /* VC: V clear */
  };
  sh4_codegen cg = sh4g_open(tp);
  unsigned cc4 = cond & 0x0F;

  if (cc4 == 0x0E) { sh4_emit_sett(&cg); sh4g_close(tp, &cg); return; }   /* AL */
  if (cc4 == 0x0F) { sh4_emit_clrt(&cg); sh4g_close(tp, &cg); return; }   /* NV */

  if (cc4 < 8) {                                          /* single-flag */
    sh4_emit_load_greg(&cg, SH4_GREG_CPSR, SH4_REG_RET);         /* R0 = CPSR */
    sh4_emit_mov_imm(&cg, -(int)cc[cc4].pos, SH4_REG_T2);        /* R3 = -pos */
    sh4_emit_shld(&cg, SH4_REG_T2, SH4_REG_RET);                 /* R0 >>= pos */
    sh4_emit_and_imm(&cg, 1);                                    /* R0 &= 1 */
    sh4_emit_cmpeq_imm(&cg, cc[cc4].want);                       /* T = flag==want */
    sh4g_close(tp, &cg);
    return;
  }

  /* Compound: R1 = CPSR, build the pair's "positive form" into R0's sign bit. */
  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, SH4_REG_T0);            /* R1 = CPSR */
  switch (cc4) {
  case 0x8: /* HI */ case 0x9: /* LS */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);             /* R0 = CPSR    */
    sh4_emit_shll2(&cg, SH4_REG_RET);                           /* R0 = C<<31.. */
    sh4_emit_not(&cg, SH4_REG_T0, SH4_REG_T1);                  /* R2 = ~CPSR   */
    sh4_emit_shll(&cg, SH4_REG_T1);                             /* R2 sign = !Z */
    sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);                 /* R0 sign = HI */
    break;
  case 0xA: /* GE */ case 0xB: /* LT */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);             /* R0 = CPSR    */
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);                            /* R0 sign = V  */
    sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);                 /* R0 sign = LT */
    break;
  default:  /* 0xC GT / 0xD LE */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);             /* R0 = CPSR    */
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);                            /* R0 sign = V  */
    sh4_emit_xor(&cg, SH4_REG_T0, SH4_REG_RET);                 /* R0 sign = N^V*/
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);              /* R2 = CPSR    */
    sh4_emit_shll(&cg, SH4_REG_T1);                             /* R2 sign = Z  */
    sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_RET);                  /* R0 sign = LE */
    break;
  }
  /* HI/LT/LE read the sign directly; LS/GE/GT take its complement. */
  if (cc4 == 0x8 || cc4 == 0xB || cc4 == 0xD)
    sh4_emit_shll(&cg, SH4_REG_RET);                            /* T = sign  */
  else
    sh4_emit_cmppz(&cg, SH4_REG_RET);                           /* T = !sign */
  sh4g_close(tp, &cg);
}

/* Emit BF placeholder (skip body when T==0); returns the BF site to patch. */
static inline u8 *sh4g_emit_bf_placeholder(u8 **tp)
{
  u8 *site = *tp;
  sh4g_u16(tp, 0x8B00);                                         /* BF 0 (disp patched) */
  return site;
}

/* Emit BT placeholder (branch when T==1); returns the BT site to patch. */
static inline u8 *sh4g_emit_bt_placeholder(u8 **tp)
{
  u8 *site = *tp;
  sh4g_u16(tp, 0x8900);                                         /* BT 0 (disp patched) */
  return site;
}

/* Patch a BT/BF disp8 at `site` to branch to `target`. Range +-256 bytes. */
static inline void sh4g_patch_cond(u8 *site, const void *target)
{
  long d = ((long)(uintptr_t)target - ((long)(uintptr_t)site + 4)) / 2;
  site[1] = (uint8_t)(d & 0xFF);
  SH4G_RESYNC(site, 2);            /* BT/BF is an instruction: re-fetch it */
}

/* Patch a conditional skip back to `target`. One close path serves both skip
 * forms, dispatched on the placeholder opcode: a BT/BF disp8 (0x8?00 — the
 * bounded Thumb-branch body) vs a far jump's MOV.L @(d,PC),R0 (0xD?00 — the
 * unbounded ARM run from sh4g_emit_cond_skip_far). The far form chain-patches:
 * a skip target inside BRA reach (the common case — the predicated body is
 * rarely 4 KiB long) becomes a direct branch. */
static inline void sh4g_patch_cond_skip(u8 *site, const void *target)
{
  if ((site[0] & 0xF0) == 0x80)
    sh4g_patch_cond(site, target);               /* disp8 BT/BF */
  else
    sh4g_chain_patch(site, target);              /* near BRA / far literal */
}

/* Emit an unconditional local forward branch placeholder (BRA 0 + delay NOP);
 * returns the BRA site to patch. Disp12 range +-4 KB — fits a single guest
 * instruction's two arms. */
static inline u8 *sh4g_emit_bra_placeholder(u8 **tp)
{
  u8 *site = *tp;
  sh4g_u16(tp, 0xA000);            /* BRA disp12 (patched) */
  sh4g_u16(tp, 0x0009);            /* NOP in the delay slot */
  return site;
}

/* Patch a BRA disp12 at `site` to branch to `target`. */
static inline void sh4g_patch_bra(u8 *site, const void *target)
{
  long d = ((long)(uintptr_t)target - ((long)(uintptr_t)site + 4)) / 2;
  site[0] = (uint8_t)(0xA0 | ((d >> 8) & 0x0F));
  site[1] = (uint8_t)(d & 0xFF);
  SH4G_RESYNC(site, 2);
}

#endif /* CGBA_SH4_EMIT_GLUE_H */
