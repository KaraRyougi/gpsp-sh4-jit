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
 * Re-sync a patched line on the SH-4A's split I/D caches. Used only by
 * sh4g_patch_cond: it rewrites the disp8 of a BT/BF, which is fetched and
 * executed as an INSTRUCTION, so a stale I-cache copy would branch wrong. (Today
 * every conditional patch is intra-block, inside the forward range the post-block
 * translate_icache_sync already covers, so this is belt-and-suspenders; it stays
 * because the moment a conditional patch ever targets an already-synced block it
 * becomes load-bearing, and an instruction patch is the one case where it must.)
 *
 * It is deliberately NOT used by sh4g_patch_jump. That patches the .long target
 * of `MOV.L @(d,PC),R0; JMP @R0` — a LITERAL read as DATA via the operand cache,
 * never fetched as an instruction (the JMP leaves before reaching it). Patch
 * store and MOV.L load are D-cache-coherent on one core, so the read sees the
 * new value with no flush; an OCBWB+ICBI there is pure waste on the hottest
 * (every-chain) path. The host build of the encoder has no caches: no-op. */
#if defined(CGBA_FXCG100)
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
static inline void sh4g_const(u8 **tp, uint32_t value, unsigned rn)
{
  if ((int32_t)value >= -128 && (int32_t)value <= 127) {
    sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | (value & 0xFF)));   /* MOV #imm8 */
    return;
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
}

/* JMP @literal(fn), no return; literal sits after the (taken) JMP. */
static inline void sh4g_far_jmp(u8 **tp, const void *fn)
{
  u8 *load = *tp, *lit;
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
}

/* ---- N/Z materialization (literal-free) into REG_CPSR -------------------- */

/* result must be in R1 (SH4_REG_T0); clobbers R0/R2/R3. */
static inline void sh4g_set_nz(u8 **tp, unsigned result)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned r_cpsr = SH4_REG_T1; /* R2 */
  const unsigned r_tmp  = SH4_REG_T2; /* R3 */

  sh4_emit_load_greg(&cg, SH4_GREG_CPSR, r_cpsr);
  sh4_emit_shll2(&cg, r_cpsr);                 /* clear bits 31..30 */
  sh4_emit_shlr2(&cg, r_cpsr);

  /* N = result & 0x80000000 */
  sh4_emit_mov_reg(&cg, result, r_tmp);
  sh4_emit_mov_imm(&cg, 1, SH4_REG_RET);
  sh4_emit_rotr(&cg, SH4_REG_RET);             /* R0 = 0x80000000 */
  sh4_emit_and(&cg, SH4_REG_RET, r_tmp);
  sh4_emit_or(&cg, r_tmp, r_cpsr);

  /* Z = (result == 0) << 30 */
  sh4_emit_tst(&cg, result, result);
  sh4_emit_movt(&cg, r_tmp);
  sh4_emit_mov_imm(&cg, 30, SH4_REG_RET);
  sh4_emit_shld(&cg, SH4_REG_RET, r_tmp);
  sh4_emit_or(&cg, r_tmp, r_cpsr);

  sh4_emit_store_greg(&cg, r_cpsr, SH4_GREG_CPSR);
  sh4g_close(tp, &cg);
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

/* rd/rn are guest reg indices; rm guest reg index. */
static inline void sh4g_dp_reg(u8 **tp, int op, unsigned rd, unsigned rn,
                               unsigned rm, int set_flags)
{
  sh4_codegen cg = sh4g_open(tp);
  if (sh4g_dp_second_only(op))
    sh4_emit_load_greg(&cg, rm, SH4_REG_T1);          /* second only */
  else {
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    sh4_emit_load_greg(&cg, rm, SH4_REG_T1);
  }
  sh4g_dp_compute(&cg, op);
  if (!sh4g_dp_is_test(op))
    sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
  sh4g_close(tp, &cg);
  if (set_flags)
    sh4g_set_nz(tp, SH4_REG_T0);
}

/* rn guest reg index; imm immediate operand. */
static inline void sh4g_dp_imm(u8 **tp, int op, unsigned rd, unsigned rn,
                               uint32_t imm, int set_flags)
{
  sh4_codegen cg = sh4g_open(tp);
  if (!sh4g_dp_second_only(op))
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
  sh4g_close(tp, &cg);
  sh4g_const(tp, imm, SH4_REG_T1);                    /* second = imm */
  cg = sh4g_open(tp);
  sh4g_dp_compute(&cg, op);
  if (!sh4g_dp_is_test(op))
    sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
  sh4g_close(tp, &cg);
  if (set_flags)
    sh4g_set_nz(tp, SH4_REG_T0);
}

/* ---- shifts (LSL/LSR/ASR/ROR by imm or reg) ------------------------------ */
enum { SH4SH_LSL, SH4SH_LSR, SH4SH_ASR, SH4SH_ROR };

static inline void sh4g_shift_imm(u8 **tp, int kind, unsigned rd, unsigned rs,
                                  unsigned imm5, int set_flags)
{
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_load_greg(&cg, rs, SH4_REG_T0);
  if (imm5 != 0) {
    int amt = (kind == SH4SH_LSL) ? (int)imm5 : -(int)imm5;
    sh4_emit_mov_imm(&cg, amt, SH4_REG_RET);
    if (kind == SH4SH_ASR)
      sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
    else                                              /* LSL / LSR */
      sh4_emit_shld(&cg, SH4_REG_RET, SH4_REG_T0);
  } else if (kind == SH4SH_LSR) {
    /* Thumb LSR #0 means LSR #32 -> result is 0. */
    sh4_emit_mov_imm(&cg, 0, SH4_REG_T0);
  } else if (kind == SH4SH_ASR) {
    /* Thumb ASR #0 means ASR #32 -> sign extension (0 or -1). */
    sh4_emit_mov_imm(&cg, -31, SH4_REG_RET);
    sh4_emit_shad(&cg, SH4_REG_RET, SH4_REG_T0);
  }                                                   /* LSL #0 is a no-op */
  sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
  sh4g_close(tp, &cg);
  if (set_flags)
    sh4g_set_nz(tp, SH4_REG_T0);
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
  sh4g_const(tp, (uint32_t)(uintptr_t)(seq ? ws_cyc_seq : ws_cyc_nseq), SH4_REG_T2);
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

/* Charge the indirect-branch (BX / computed PC) pipeline refill at RUNTIME to
 * match the interpreter: an ARM target costs ws_cyc_nseq[target>>24][1]; a Thumb
 * target (bit 0 set) costs nothing -- the interp's ARM->Thumb BX falls straight
 * into thumb_loop and takes no refill (cpu.cc). `target_reg` holds the runtime
 * target and is preserved; clobbers R0/T1/T2. */
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
  sh4g_const(tp, (uint32_t)(uintptr_t)ws_cyc_nseq, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1); /* T1 = ws_cyc_nseq[reg][1] */
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);       /* R13 -= refill            */
    sh4g_close(tp, &cg); }
  { long d = ((long)(*tp) - ((long)bf + 4)) / 2; bf[1] = (uint8_t)(d & 0xFF); }
}

static inline void sh4g_cycle_sub(u8 **tp, int n, uint32_t pc,
                                  const void *block_exit_fn)
{
  u8 *bt;
  if (n == 0)
    return;
  sh4g_cycle_debit(tp, n);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppz(&cg, SH4_REG_CYCLES);      /* T = (cycles >= 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                        /* BT skip update */
  sh4g_const(tp, pc, SH4_REG_ARG0);
  sh4g_far_jmp(tp, block_exit_fn);
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
  const u8 *cell = &ws_cyc_seq[(pc >> 24) & 0x0F][is_word ? 1 : 0];
  sh4g_const(tp, (uint32_t)(uintptr_t)cell, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
}

static inline void sh4g_cycle_gate(u8 **tp, uint32_t pc, int is_word,
                                   const void *block_exit_fn)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppz(&cg, SH4_REG_CYCLES);       /* T = (cycles >= 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                        /* BT skip (budget remains) */
  sh4g_charge_fetch_cell(tp, pc, is_word);
  sh4g_const(tp, pc, SH4_REG_ARG0);
  sh4g_far_jmp(tp, block_exit_fn);
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

/* Materialize a guest PC value into R4 (ARG0) and store it to reg[REG_PC]. */
static inline void sh4g_store_pc_imm(u8 **tp, uint32_t new_pc)
{
  sh4g_const(tp, new_pc, SH4_REG_ARG0);
  sh4g_store_greg(tp, SH4_REG_ARG0, SH4_GREG_PC);
}

/* Block exit to `new_pc`. reg[REG_PC] and R4 are set to new_pc; if the cycle
 * counter is exhausted (R13 < 0) control leaves to `block_exit_fn` (which
 * processes events and re-dispatches), otherwise it takes a patchable jump that
 * the driver back-patches to the resolved target block for direct chaining
 * (initialized to `block_exit_fn` so the unpatched path is still correct).
 * Returns the patch site. */
static inline u8 *sh4g_branch_exit(u8 **tp, uint32_t new_pc,
                                   const void *block_exit_fn)
{
  u8 *bt, *site;
  sh4g_store_pc_imm(tp, new_pc);                /* R4 = reg[REG_PC] = new_pc */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppz(&cg, SH4_REG_CYCLES);        /* T = (cycles >= 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                          /* BT over (skip exit if budget left) */
  sh4g_far_jmp(tp, block_exit_fn);               /* exhausted: events + redispatch */
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }

  site = sh4g_emit_patch_jump(tp);               /* chain to target block (patched) */
  sh4g_patch_jump(site, block_exit_fn);          /* default: redispatch */
  return site;
}

/* After a C handler that returns nonzero in R0 when it changed the guest PC,
 * re-dispatch: TST R0,R0 ; BT skip ; (R4 = reg[REG_PC]) ; jmp block_exit ; skip:. */
static inline void sh4g_redispatch_if_r0(u8 **tp, const void *block_exit_fn)
{
  u8 *bt;
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T = (R0 == 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                               /* BT skip (no PC change) */
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);      /* R4 = reg[REG_PC] */
  sh4g_far_jmp(tp, block_exit_fn);
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

/* Same as sh4g_redispatch_if_r0(), but when the helper returns nonzero it first
 * debits the translated instructions accumulated since the previous cycle gate.
 * The no-PC-change path deliberately keeps accumulating: it skips this debit and
 * the normal block-end/branch gate will flush the full run later. */
static inline void sh4g_redispatch_if_r0_debit(u8 **tp, int cycle_count,
                                               const void *block_exit_fn)
{
  u8 *bt;
  if (cycle_count == 0) {
    sh4g_redispatch_if_r0(tp, block_exit_fn);
    return;
  }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET);     /* T = (R0 == 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                              /* BT skip (no PC change) */
  sh4g_cycle_debit(tp, cycle_count);
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);     /* R4 = reg[REG_PC] */
  sh4g_far_jmp(tp, block_exit_fn);
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
 * unbounded ARM run from sh4g_emit_cond_skip_far). */
static inline void sh4g_patch_cond_skip(u8 *site, const void *target)
{
  if ((site[0] & 0xF0) == 0x80)
    sh4g_patch_cond(site, target);               /* disp8 BT/BF */
  else
    sh4g_patch_jump(site, target);               /* far literal jump */
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
