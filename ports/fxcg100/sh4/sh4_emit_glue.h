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
 * CURRENT MODEL:
 *   - Guest ARM r0 is resident in SH-4 R11, r1 in R12, and CPSR in R8 while
 *     translated code runs; the assembly funnels synchronize all three around
 *     C boundaries. Other guest registers live in reg[].
 *   - NZCV are materialized in the cached CPSR, with per-instruction liveness
 *     allowing dead flag work to be omitted.
 *   - Large 32-bit constants use bounded, forward segmented literal pools while
 *     a block is being translated; equal constants share one word per segment.
 *     Resident emitters and far targets retain self-contained literal islands.
 *   - Common ARM/Thumb memory operations, block transfers, ALU/shift/multiply,
 *     PSR and exact divide paths emit native SH-4; uncommon or side-effectful
 *     cases retain compact C-helper fallbacks.
 *
 * Host register model (mirrors sh4_emit_core.h): R14 = reg[] base, R13 = cycle
 * counter, R12 = guest r1, R11 = guest r0, R10 = block exit, R9 = vector table,
 * R8 = CPSR; R0 = scratch/C return, R1..R3 = scratch, R4..R7 = C args.
 */

#include "ports/fxcg100/sh4/sh4_codegen.h"
#include "ports/fxcg100/sh4/sh4_emit_core.h"

/* Large translation-time-only emitters: keep out-of-line so GCC does not clone
 * them across fallback loops and bloat translate_block_arm/thumb (.text). */
#if defined(__GNUC__) || defined(__clang__)
#define SH4G_NOINLINE static __attribute__((noinline, unused))
#else
#define SH4G_NOINLINE static
#endif

extern int cgba_sh4_extra_cycles;
extern u8 ws_cyc_seq[16][2];   /* gba_memory.c: GBA wait-state cycle tables, */
extern u8 ws_cyc_nseq[16][2];  /* [region][word?1:0], live under WAITCNT.    */

/* ---- back-patch I-cache sync --------------------------------------------- *
 * All branch/jump patches happen before final block publication (which syncs
 * the emitted range), and sh4g_patch_jump updates a data literal read via the
 * operand cache (D-cache coherent on a single SH-4 core). */
#define SH4G_RESYNC(p, n) ((void)0)

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
#if defined(__sh__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  u8 *p = *tp;
  *(uint16_t *)p = op;
  *tp = p + 2;
#else
  sh4_codegen cg = sh4g_open(tp);
  sh4_emit_u16(&cg, op);
  sh4g_close(tp, &cg);
#endif
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
  SH4G_VEC_iwram_data,
  SH4G_VEC_ewram_data,
#if defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE) || \
    (defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE)
  SH4G_VEC_palette_dirty,
#endif
#if defined(CGBA_VIDEO_DIRTY_SCANLINE) && CGBA_VIDEO_DIRTY_SCANLINE
  SH4G_VEC_vram_dirty,
#endif
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
extern unsigned long cgba_em_help_n, cgba_em_help_bytes;
extern unsigned long cgba_em_tuple_routine_b, cgba_em_tuple_tramp_b;
extern unsigned long cgba_em_tuple_fn_b, cgba_em_tuple_opcode_b;
extern unsigned long cgba_em_tuple_pc_b, cgba_em_tuple_cycle_b;
extern unsigned long cgba_em_tuple_params_b;
#define SH4G_EMSTAT(expr) (expr)
#else
#define SH4G_EMSTAT(expr) ((void)0)
#endif

/* ---- segmented production literal pool ---------------------------------
 * Large constants normally carry a private BRA-skipped word. While translating
 * a block, collect their MOV.L references into bounded forward segments. A
 * segment closes at a guest-instruction boundary after 704 bytes, leaving room
 * for its skip branch, alignment, and up to 48 unique words inside MOV.L's
 * +1020-byte reach. Equal values share one word within the segment. Resident
 * fastmem generation leaves this inactive and retains self-contained islands. */
#ifndef CGBA_SH4_SEGMENTED_LITERAL_POOL
#define CGBA_SH4_SEGMENTED_LITERAL_POOL 0
#endif

#if CGBA_SH4_SEGMENTED_LITERAL_POOL
#define SH4G_LITSEG_MAX_REFS 48
#define SH4G_LITSEG_FLUSH_DISTANCE 704
typedef struct sh4g_litseg_ref {
  u8 *site;
  uint32_t value;
} sh4g_litseg_ref;
typedef struct sh4g_litseg_state {
  int active;
  int nrefs;
  sh4g_litseg_ref refs[SH4G_LITSEG_MAX_REFS];
} sh4g_litseg_state;
static sh4g_litseg_state sh4g_litseg;

#if defined(CGBA_GPSP_HEADLESS_TEST)
extern unsigned long cgba_em_pool_ref_n, cgba_em_pool_unique_n;
extern unsigned long cgba_em_pool_flush_n, cgba_em_pool_bytes;
#define SH4G_POOLSTAT(expr) (expr)
#else
#define SH4G_POOLSTAT(expr) ((void)0)
#endif

static inline void sh4g_litseg_flush_mode(u8 **tp, int emit_bra)
{
  u8 *bra = NULL;
  int i;
  unsigned unique = 0;
  unsigned long bytes0;

  if (!sh4g_litseg.active || sh4g_litseg.nrefs == 0)
    return;

  bytes0 = (unsigned long)(uintptr_t)*tp;
  if (emit_bra) {
    bra = *tp;
    sh4g_u16(tp, 0xA000);                              /* BRA over pool */
    sh4g_u16(tp, 0x0009);                              /* delay NOP */
  }
  if ((uintptr_t)*tp & 3u)
    sh4g_u16(tp, 0x0009);

  {
    /* Generation-stamped 64-entry open-addressed table for O(1) exact dedup
     * (SH4G_LITSEG_MAX_REFS=48 < 64 guarantees >=16 free slots). */
    static uint16_t hgen = 0;
    static uint16_t htag[64];
    static uint32_t hval[64];
    static u8      *hent[64];
    int nrefs = sh4g_litseg.nrefs;
    u8 *cur = *tp;
    uint16_t gen = (uint16_t)(hgen + 1u);
    if (__builtin_expect(gen == 0u, 0)) {
      int z;
      for (z = 0; z < 64; z++) htag[z] = 0u;
      gen = 1u;
    }
    hgen = gen;
    for (i = 0; i < nrefs; i++) {
      uint32_t v = sh4g_litseg.refs[i].value;
      u8 *site = sh4g_litseg.refs[i].site;
      u8 *entry;
      unsigned slot = ((v >> 2) ^ (v >> 10) ^ (v >> 18)) & 63u;
      uintptr_t site_base;
      while (htag[slot] == gen) {
        if (hval[slot] == v) {
          entry = hent[slot];
          goto have_entry;
        }
        slot = (slot + 1u) & 63u;
      }
      entry = cur;
      htag[slot] = gen;
      hval[slot] = v;
      hent[slot] = entry;
      unique++;
#if defined(__sh__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      *(uint32_t *)cur = v;
      cur += 4;
#else
      { sh4_codegen cg;
        cg.ptr = cur;
        cg.limit = cur + 4;
        cg.overflow = 0;
        sh4_emit_u32_be(&cg, v);
        cur = cg.ptr; }
#endif
    have_entry:
      site_base = ((uintptr_t)site & ~(uintptr_t)3u) + 4u;
      if ((uintptr_t)entry >= site_base) {
        uintptr_t disp = ((uintptr_t)entry - site_base) >> 2;
        if (disp <= 255u) {
          site[1] = (u8)disp;
          SH4G_RESYNC(site, 2);
        }
      }
    }
    *tp = cur;
  }

  if (emit_bra) {
    uintptr_t d = ((uintptr_t)*tp - ((uintptr_t)bra + 4u)) >> 1;
    bra[0] = (u8)(0xA0u | ((d >> 8) & 0x0Fu));
    bra[1] = (u8)d;
    SH4G_RESYNC(bra, 2);
  }

  SH4G_POOLSTAT((cgba_em_pool_flush_n++,
                 cgba_em_pool_unique_n += unique,
                 cgba_em_pool_bytes +=
                   (unsigned long)(uintptr_t)*tp - bytes0));
  (void)unique;
  (void)bytes0;
  sh4g_litseg.nrefs = 0;
}

static inline void sh4g_litseg_flush(u8 **tp)
{
  sh4g_litseg_flush_mode(tp, 1);
}

static inline void sh4g_litseg_begin(void)
{
  sh4g_litseg.active = 1;
  sh4g_litseg.nrefs = 0;
}

static inline void sh4g_litseg_maybe_flush(u8 **tp)
{
  /* Invariant: nrefs > 0 implies active != 0. */
  if (sh4g_litseg.nrefs &&
      (size_t)(*tp - sh4g_litseg.refs[0].site) >=
        SH4G_LITSEG_FLUSH_DISTANCE)
    sh4g_litseg_flush(tp);
}

/* Flush early if fewer than `need` literal slots remain or the next `bytes`
 * sequence could push the oldest ref past flush distance. */
static inline void sh4g_litseg_reserve_bytes(u8 **tp, int need, size_t bytes)
{
  if (sh4g_litseg.nrefs &&
      (sh4g_litseg.nrefs + need >= SH4G_LITSEG_MAX_REFS ||
       (size_t)(*tp - sh4g_litseg.refs[0].site) + bytes >=
         SH4G_LITSEG_FLUSH_DISTANCE))
    sh4g_litseg_flush(tp);
}

static inline void sh4g_litseg_reserve(u8 **tp, int need)
{
  sh4g_litseg_reserve_bytes(tp, need, 128u);
}

static inline void sh4g_litseg_end(u8 **tp)
{
  sh4g_litseg_flush(tp);
  sh4g_litseg.active = 0;
}

/* Flush at an unreachable routine tail (after RTS/JMP): omits the skip BRA. */
static inline void sh4g_litseg_end_tail(u8 **tp)
{
  sh4g_litseg_flush_mode(tp, 0);
  sh4g_litseg.active = 0;
}

static inline void sh4g_litseg_abort(void)
{
  sh4g_litseg.active = 0;
  sh4g_litseg.nrefs = 0;
}

static inline int sh4g_litseg_add(u8 **tp, uint32_t value, unsigned rn)
{
  if (!sh4g_litseg.active)
    return 0;
  if (sh4g_litseg.nrefs == SH4G_LITSEG_MAX_REFS)
    sh4g_litseg_flush(tp);
  sh4g_litseg.refs[sh4g_litseg.nrefs].site = *tp;
  sh4g_litseg.refs[sh4g_litseg.nrefs].value = value;
  sh4g_litseg.nrefs++;
  sh4g_u16(tp, (uint16_t)(0xD000 | (rn << 8)));       /* patched at flush */
  SH4G_POOLSTAT((cgba_em_pool_ref_n++, cgba_em_pool_bytes += 2));
  return 1;
}
#else
static inline void sh4g_litseg_begin(void) {}
static inline void sh4g_litseg_maybe_flush(u8 **tp) { (void)tp; }
static inline void sh4g_litseg_reserve(u8 **tp, int need) { (void)tp; (void)need; }
static inline void sh4g_litseg_reserve_bytes(u8 **tp, int need, size_t bytes)
{ (void)tp; (void)need; (void)bytes; }
static inline void sh4g_litseg_end(u8 **tp) { (void)tp; }
static inline void sh4g_litseg_end_tail(u8 **tp) { (void)tp; }
static inline void sh4g_litseg_abort(void) {}
static inline int sh4g_litseg_add(u8 **tp, uint32_t value, unsigned rn)
{ (void)tp; (void)value; (void)rn; return 0; }
#endif

static __attribute__((noinline)) int
sh4g_const_try_shift(u8 **tp, uint32_t value, unsigned rn)
{
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
      return 1;
    }
  }
  return 0;
}

static __attribute__((noinline)) void sh4g_const_slow(u8 **tp, uint32_t value, unsigned rn)
{
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
   * (1<<18), SP/PC*4 offsets, etc. O(1) bit-width pre-filter rejects 99%+ of
   * 32-bit addresses without calling software __ctzsi2 on SH-4. */
  if ((value & 3u) == 0u && value != 0u) {
    uint32_t low = value & (0u - value);
    uint32_t x = value ^ (uint32_t)((int32_t)value >> 31);
    if (__builtin_expect(low >= 0x02000000u || x < (low << 7), 0)) {
      if (sh4g_const_try_shift(tp, value, rn))
        return;
    }
  }

  if (sh4g_litseg_add(tp, value, rn)) {
    SH4G_EMSTAT(cgba_em_const_large++);
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
    SH4G_EMSTAT((cgba_em_const_large++, cgba_em_const_bytes += (unsigned long)(*tp - load)));
  }
}

static inline void sh4g_const(u8 **tp, uint32_t value, unsigned rn)
{
  if ((int32_t)value >= -128 && (int32_t)value <= 127) {
    SH4G_EMSTAT(cgba_em_const_small++);
    sh4g_u16(tp, (uint16_t)(0xE000 | (rn << 8) | (value & 0xFF)));   /* MOV #imm8 */
    return;
  }
  sh4g_const_slow(tp, value, rn);
}

/* small two-operand emitters used by a few handlers */
static inline void sh4g_add_reg(u8 **tp, unsigned rm, unsigned rn)
{ sh4_codegen cg = sh4g_open(tp); sh4_emit_add_reg(&cg, rm, rn); sh4g_close(tp, &cg); }
static inline void sh4g_sub_reg(u8 **tp, unsigned rm, unsigned rn)
{ sh4_codegen cg = sh4g_open(tp); sh4_emit_sub(&cg, rm, rn); sh4g_close(tp, &cg); }

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

/* ---- far jump via inline literal ----------------------------------------- */

/* JMP @literal(fn), no return; literal sits after the (taken) JMP. */
static inline void sh4g_far_jmp(u8 **tp, const void *fn)
{
  u8 *load = *tp, *lit;
  SH4G_EMSTAT(cgba_em_fjmp_n++);
  long ld_disp;

  if (sh4g_litseg_add(tp, (uint32_t)(uintptr_t)fn, SH4_REG_RET)) {
    sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_RET << 8)));                    /* JMP @R0 */
    sh4g_u16(tp, 0x0009);                                                     /* NOP (delay) */
    SH4G_EMSTAT(cgba_em_fjmp_bytes += (unsigned long)(*tp - load));
    return;
  }

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
  u8 *stat0 = *tp;
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
  SH4G_EMSTAT((cgba_em_help_n++,
               cgba_em_help_bytes += (unsigned long)(*tp - stat0),
               cgba_em_tuple_tramp_b += 4,
               cgba_em_tuple_fn_b += 4,
               cgba_em_tuple_opcode_b += 4,
               cgba_em_tuple_pc_b += 4,
               cgba_em_tuple_cycle_b += with_cycles ? 4 : 0));
  (void)stat0;
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
  unsigned r_tmp  = SH4_REG_T2;               /* R3 */

  if (!eff)
    return;
  cg = sh4g_open(tp);
  switch (eff) {                              /* drop the live top bits in R8 */
  case 0x8:
    sh4_emit_shll(&cg, SH4_REG_CPSR);  break;
  case 0xC:
    sh4_emit_shll2(&cg, SH4_REG_CPSR); break;
  case 0xE:
    sh4_emit_shll2(&cg, SH4_REG_CPSR); sh4_emit_shll(&cg, SH4_REG_CPSR); break;
  default:                                    /* 0xF */
    sh4_emit_shll2(&cg, SH4_REG_CPSR); sh4_emit_shll2(&cg, SH4_REG_CPSR); break;
  }

  if (eff & 0x1) {                            /* V in first (ends at bit28) */
    sh4_emit_shlr(&cg, rv);                   /* T = rv (0/1); rv is dead temp */
    sh4_emit_rotcr(&cg, SH4_REG_CPSR);
  }
  if (eff & 0x2) {                            /* C */
    sh4_emit_shlr(&cg, rc);                   /* T = rc (0/1); rc is dead temp */
    sh4_emit_rotcr(&cg, SH4_REG_CPSR);
  }
  if (eff & 0x4) {                            /* Z */
    sh4_emit_tst(&cg, result, result);        /* T = (result == 0) */
    sh4_emit_rotcr(&cg, SH4_REG_CPSR);
  }
  /* N last: lands at bit31 */
  sh4_emit_mov_reg(&cg, result, r_tmp);
  sh4_emit_shll(&cg, r_tmp);                  /* T = result bit31 */
  sh4_emit_rotcr(&cg, SH4_REG_CPSR);
  sh4g_close(tp, &cg);
}

static inline void sh4g_set_nz_m(u8 **tp, unsigned result, u32 mask)
{
  sh4g_set_flags(tp, result, SH4_REG_ARG0, SH4_REG_ARG0,
                 sh4g_flags_round(mask & 0xC));
}

/* ---- data-processing core ------------------------------------------------ */

enum {
  SH4DP_AND, SH4DP_ORR, SH4DP_EOR, SH4DP_BIC,
  SH4DP_MOV, SH4DP_MVN, SH4DP_TST, SH4DP_MUL
};

/* Compute logical/unary/mul op of (first in R1, second in R2) into R1. */
static inline void sh4g_dp_compute(sh4_codegen *cg, int op)
{
  const unsigned a = SH4_REG_T0; /* R1 = first / result */
  const unsigned b = SH4_REG_T1; /* R2 = second */

  switch (op) {
  case SH4DP_AND: case SH4DP_TST: sh4_emit_and(cg, b, a); break;
  case SH4DP_ORR:                 sh4_emit_or(cg, b, a);  break;
  case SH4DP_EOR:                 sh4_emit_xor(cg, b, a); break;
  case SH4DP_BIC: sh4_emit_not(cg, b, b); sh4_emit_and(cg, b, a); break;
  case SH4DP_MUL: sh4_emit_mul_l(cg, b, a); sh4_emit_sts_macl(cg, a); break;
  case SH4DP_MOV: sh4_emit_mov_reg(cg, b, a); break;   /* result = second */
  case SH4DP_MVN: sh4_emit_not(cg, b, a); break;       /* result = ~second */
  default: break;
  }
}

/* ---- shifts (LSL/LSR/ASR/ROR by imm or reg) ------------------------------ */
enum { SH4SH_LSL, SH4SH_LSR, SH4SH_ASR, SH4SH_ROR };

/* Unified immediate shift in place on `dst`. `type`: 0=LSL 1=LSR 2=ASR 3=ROR
 * (ROR requires amount 1..31; RRX #0 excluded). For LSR/ASR, amount == 0 means
 * shift by 32. Uses 1-2 cycle SH-4 fixed shifts for common amounts; otherwise
 * loads the shift count into R0 (reusing *cached_sh when non-NULL). For ROR
 * with amount != 16, SH4_REG_T2 (R3) is used as a scratch register. */
static inline void sh4g_emit_shift_imm(sh4_codegen *cg, unsigned type,
                                       unsigned amount, unsigned dst,
                                       int *cached_sh)
{
  if (type == SH4SH_LSL) {
    switch (amount) {
    case 0:  return;
    case 1:  sh4_emit_shll(cg, dst); return;
    case 2:  sh4_emit_shll2(cg, dst); return;
    case 3:  sh4_emit_shll2(cg, dst); sh4_emit_shll(cg, dst); return;
    case 4:  sh4_emit_shll2(cg, dst); sh4_emit_shll2(cg, dst); return;
    case 8:  sh4_emit_shll8(cg, dst); return;
    case 9:  sh4_emit_shll8(cg, dst); sh4_emit_shll(cg, dst); return;
    case 10: sh4_emit_shll8(cg, dst); sh4_emit_shll2(cg, dst); return;
    case 16: sh4_emit_shll16(cg, dst); return;
    case 17: sh4_emit_shll16(cg, dst); sh4_emit_shll(cg, dst); return;
    case 18: sh4_emit_shll16(cg, dst); sh4_emit_shll2(cg, dst); return;
    case 24: sh4_emit_shll16(cg, dst); sh4_emit_shll8(cg, dst); return;
    default: break;
    }
    if (!cached_sh || *cached_sh != (int)amount) {
      sh4_emit_mov_imm(cg, (int)amount, SH4_REG_RET);
      if (cached_sh) *cached_sh = (int)amount;
    }
    sh4_emit_shld(cg, SH4_REG_RET, dst);
  } else if (type == SH4SH_LSR) {
    switch (amount) {
    case 0:  sh4_emit_mov_imm(cg, 0, dst); return;
    case 1:  sh4_emit_shlr(cg, dst); return;
    case 2:  sh4_emit_shlr2(cg, dst); return;
    case 3:  sh4_emit_shlr2(cg, dst); sh4_emit_shlr(cg, dst); return;
    case 4:  sh4_emit_shlr2(cg, dst); sh4_emit_shlr2(cg, dst); return;
    case 8:  sh4_emit_shlr8(cg, dst); return;
    case 9:  sh4_emit_shlr8(cg, dst); sh4_emit_shlr(cg, dst); return;
    case 10: sh4_emit_shlr8(cg, dst); sh4_emit_shlr2(cg, dst); return;
    case 16: sh4_emit_shlr16(cg, dst); return;
    case 17: sh4_emit_shlr16(cg, dst); sh4_emit_shlr(cg, dst); return;
    case 18: sh4_emit_shlr16(cg, dst); sh4_emit_shlr2(cg, dst); return;
    case 24: sh4_emit_shlr16(cg, dst); sh4_emit_shlr8(cg, dst); return;
    default: break;
    }
    if (!cached_sh || *cached_sh != -(int)amount) {
      sh4_emit_mov_imm(cg, -(int)amount, SH4_REG_RET);
      if (cached_sh) *cached_sh = -(int)amount;
    }
    sh4_emit_shld(cg, SH4_REG_RET, dst);
  } else if (type == SH4SH_ASR) {
    int sh;
    if (amount == 1u) { sh4_emit_shar(cg, dst); return; }
    if (amount == 2u) { sh4_emit_shar(cg, dst); sh4_emit_shar(cg, dst); return; }
    sh = (amount == 0u) ? -32 : -(int)amount;
    if (!cached_sh || *cached_sh != sh) {
      sh4_emit_mov_imm(cg, sh, SH4_REG_RET);
      if (cached_sh) *cached_sh = sh;
    }
    sh4_emit_shad(cg, SH4_REG_RET, dst);
  } else if (amount == 16u) {
    sh4_emit_swap_w(cg, dst, dst);
  } else if (amount == 8u) {
    sh4_emit_mov_reg(cg, dst, SH4_REG_T2);
    sh4_emit_shlr8(cg, dst);
    sh4_emit_shll16(cg, SH4_REG_T2);
    sh4_emit_shll8(cg, SH4_REG_T2);
    sh4_emit_or(cg, SH4_REG_T2, dst);
  } else if (amount == 24u) {
    sh4_emit_mov_reg(cg, dst, SH4_REG_T2);
    sh4_emit_shll8(cg, dst);
    sh4_emit_shlr16(cg, SH4_REG_T2);
    sh4_emit_shlr8(cg, SH4_REG_T2);
    sh4_emit_or(cg, SH4_REG_T2, dst);
  } else {
    sh4_emit_mov_reg(cg, dst, SH4_REG_T2);
    sh4_emit_mov_imm(cg, -(int)amount, SH4_REG_RET);
    sh4_emit_shld(cg, SH4_REG_RET, dst);
    sh4_emit_mov_imm(cg, (int)(32u - amount), SH4_REG_RET);
    sh4_emit_shld(cg, SH4_REG_RET, SH4_REG_T2);
    sh4_emit_or(cg, SH4_REG_T2, dst);
    if (cached_sh) *cached_sh = (int)(32u - amount);
  }
}

/* Extract the shifter carry-out (0 or 1) for immediate shift (type, amount)
 * from the unshifted `src` value (or from `shifted` for ROR #1..31) into `dst`.
 * Caller ensures (type != LSL || amount != 0). Clobbers R0 and `dst`. */
static inline void sh4g_emit_shift_imm_carry(sh4_codegen *cg, unsigned type,
                                             unsigned amount, unsigned src,
                                             unsigned shifted, unsigned dst)
{
  if (type == SH4SH_ROR) {
    /* ROR #n (n=1..31): carry = bit(n-1) of src == bit31 of shifted result. */
    sh4_emit_mov_reg(cg, shifted, dst);
    sh4_emit_shll(cg, dst);
    sh4_emit_movt(cg, dst);
  } else if (amount == 0u || (type == SH4SH_LSL && amount == 1u)) {
    /* LSR#32, ASR#32, LSL#1: carry = bit31 of src. */
    if (src != dst) sh4_emit_mov_reg(cg, src, dst);
    sh4_emit_shll(cg, dst);
    sh4_emit_movt(cg, dst);
  } else if (type != SH4SH_LSL && amount == 1u) {
    /* LSR#1, ASR#1: carry = bit0 of src. */
    if (src != dst) sh4_emit_mov_reg(cg, src, dst);
    sh4_emit_shlr(cg, dst);
    sh4_emit_movt(cg, dst);
  } else {
    int cshift = (type == SH4SH_LSL) ? -(int)(32u - amount) : -(int)(amount - 1u);
    if (src != dst) sh4_emit_mov_reg(cg, src, dst);
    sh4_emit_mov_imm(cg, cshift, SH4_REG_RET);
    sh4_emit_shld(cg, SH4_REG_RET, dst);
    sh4_emit_shlr(cg, dst);
    sh4_emit_movt(cg, dst);
  }
}

/* fmask = live-flag mask (N=8 Z=4 C=2 V=1); the shifter carry is computed only
 * when C is live, which drops the carry instructions from dead-flag shifts. */
static inline void sh4g_shift_imm(u8 **tp, int kind, unsigned rd, unsigned rs,
                                  unsigned imm5, u32 fmask)
{
  sh4_codegen cg = sh4g_open(tp);
  const unsigned carry = SH4_REG_ARG0;
  int preserves_c = (kind == SH4SH_LSL && imm5 == 0);
  int want_c = (fmask & 0x2) && !preserves_c;
  sh4_emit_load_greg(&cg, rs, SH4_REG_T0);
  if (want_c)
    sh4g_emit_shift_imm_carry(&cg, (unsigned)kind, imm5, SH4_REG_T0,
                              SH4_REG_T0, carry);
  sh4g_emit_shift_imm(&cg, (unsigned)kind, imm5, SH4_REG_T0, NULL);
  sh4_emit_store_greg(&cg, SH4_REG_T0, rd);
  sh4g_close(tp, &cg);
  sh4g_set_flags(tp, SH4_REG_T0, carry, carry,
                 sh4g_flags_round(fmask & (want_c ? 0xE : 0xC)));
}

/* ---- cycle counter ------------------------------------------------------- */

static inline void sh4g_cycle_debit_reg(u8 **tp, int n, unsigned tmp_reg)
{
  if (n == 0)
    return;
  if (n >= -128 && n <= 127) {
    sh4g_u16(tp, (uint16_t)(0x7000 | (SH4_REG_CYCLES << 8) | ((-n) & 0xFF))); /* ADD #-n */
  } else {
    sh4g_const(tp, (uint32_t)(-n), tmp_reg);
    sh4g_u16(tp, (uint16_t)(0x300C | (SH4_REG_CYCLES << 8) | (tmp_reg << 4))); /* ADD tmp_reg */
  }
}

static inline void sh4g_cycle_debit(u8 **tp, int n)
{
  sh4g_cycle_debit_reg(tp, n, SH4_REG_T0);
}

/* Same as sh4g_cycle_debit but uses R0 (SH4_REG_RET) for large debits so R1/R2
 * operands survive intact across fused CMP/TST/SUBS + Bcc taken paths. */
static inline void sh4g_cycle_debit_r0(u8 **tp, int n)
{
  sh4g_cycle_debit_reg(tp, n, SH4_REG_RET);
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
 * `region_reg` holds the already-classified guest region (address >> 24) and
 * must not be T1/T2. `seq` selects the seq vs nonseq table; `is_word`
 * selects column 1 (32-bit) vs 0 (8/16-bit). Clobbers R0/T1/T2 (and T0 when
 * count > 1); only the cycle counter R13 changes. */
static inline void sh4g_charge_mem_region(u8 **tp, unsigned region_reg, int seq,
                                          int is_word, unsigned count)
{
  if (count == 0)
    return;
  { sh4_codegen cg = sh4g_open(tp);
    if (region_reg != SH4_REG_RET)
      sh4_emit_mov_reg(&cg, region_reg, SH4_REG_RET);
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

static inline void sh4g_charge_mem_run(u8 **tp, unsigned addr_reg, int seq,
                                       int is_word, unsigned count)
{
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, addr_reg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);             /* R0 = addr >> 24 */
    sh4g_close(tp, &cg); }
  sh4g_charge_mem_region(tp, SH4_REG_RET, seq, is_word, count);
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
extern void sh4_cycle_gate_exit(void);

static inline void sh4g_cycle_gate(u8 **tp, uint32_t pc, int is_word)
{
  u8 *bt;
  sh4g_litseg_reserve_bytes(tp, 2, 24u);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);       /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bt = *tp;
  sh4g_u16(tp, 0x8900);                        /* BT skip (budget remains) */
  sh4g_const(tp, pc, SH4_REG_ARG0);            /* R4 = pc */
  sh4g_const(tp, (uint32_t)(uintptr_t)sh4_cycle_gate_exit, SH4_REG_RET);
  sh4g_u16(tp, (uint16_t)(0x402B | (SH4_REG_RET << 8))); /* JMP @R0 */
  sh4g_u16(tp, (uint16_t)(0xE000 | (SH4_REG_T1 << 8) |
                          (is_word ? 1u : 0u)));         /* delay: MOV #is_word, R2 */
  { long d = ((long)(*tp) - ((long)bt + 4)) / 2; bt[1] = (uint8_t)(d & 0xFF); }
}

#ifndef CGBA_SH4_LAZY_CHAIN
#define CGBA_SH4_LAZY_CHAIN 1
#endif

/* ---- branch exit (patchable far jump / call) ----------------------------- *
 * Emits a fixed self-contained jump/call whose target literal is back-patched
 * by sh4g_patch_jump(). Returns the patch site (the MOV.L address).          */
static inline u8 *sh4g_emit_patch_branch(u8 **tp, uint16_t branch_op)
{
  u8 *site = *tp, *lit;
  long ld_disp;

  lit = (u8 *)(((uintptr_t)(site + 6) + 3u) & ~(uintptr_t)3u);
  ld_disp = ((long)lit - (((long)site & ~3L) + 4)) / 4;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | (ld_disp & 0xFF)));   /* MOV.L @(d,PC),R0 */
  sh4g_u16(tp, (uint16_t)(branch_op | (SH4_REG_RET << 8)));                   /* JMP/JSR @R0 */
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

static inline u8 *sh4g_emit_patch_jump(u8 **tp)
{
  return sh4g_emit_patch_branch(tp, 0x402B);                                   /* JMP @R0 */
}

#if CGBA_SH4_LAZY_CHAIN
/* Same geometry as sh4g_emit_patch_jump, but uses JSR @R0 so an unpatched
 * exit passes PR = site + 6 to sh4_lazy_link_rom_{arm,thumb}, which computes
 * lit = (PR + 3) & ~3 and overwrites the 4-byte data literal once the target
 * appears in rom_branch_hash. sh4g_chain_patch still rewrites near sites to
 * BRA + NOP identically. */
static inline u8 *sh4g_emit_patch_call(u8 **tp)
{
  return sh4g_emit_patch_branch(tp, 0x400B);                                   /* JSR @R0 */
}
#endif

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
    if (form == 0xD0) {
      site[2] = 0x00;                  /* NOP in the delay slot (replaces JMP) */
      site[3] = 0x09;
    }
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
  u8 *bf, *site;
  sh4g_litseg_reserve_bytes(tp, 1, 32u);
  sh4g_const(tp, new_pc, SH4_REG_ARG0);         /* R4 = new_pc */

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);        /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bf = *tp;
  sh4g_u16(tp, 0x8B00);                          /* BF cold_exit (hot fallthrough: 1 cyc) */

#if CGBA_SH4_LAZY_CHAIN
  site = sh4g_emit_patch_call(tp);               /* chain to target block (patched or lazy-linked) */
#else
  site = sh4g_emit_patch_jump(tp);               /* chain to target block (patched) */
#endif
  sh4g_patch_jump(site, block_exit_fn);          /* default: redispatch / lazy link */
  { long d = ((long)(*tp) - ((long)bf + 4)) / 2; bf[1] = (uint8_t)(d & 0xFF); }
  sh4g_block_exit_jmp(tp);                       /* cold: exhausted -> events + redispatch */
  return site;
}

/* Internal intra-block branch exit: when `known_target` is already emitted
 * (backward loop edges), bind directly with a single `BT target` (+-254 B) or
 * `BF cold; BRA target; NOP` (+-4094 B), eliminating the 10-12 B patch_jump
 * placeholder and double-taken branch. Returns NULL when already bound. */
static inline u8 *sh4g_branch_exit_internal_known(u8 **tp, uint32_t new_pc,
                                                  u8 *known_target)
{
  u8 *bf, *site;
  sh4g_litseg_reserve_bytes(tp, 1, 32u);
  if (known_target != NULL) {
    long d_bt = ((long)(uintptr_t)known_target -
                 ((long)(uintptr_t)(*tp + 2) + 4)) / 2;
    if (d_bt >= -128 && d_bt <= 127) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_cmppl(&cg, SH4_REG_CYCLES);    /* T = (cycles > 0) */
        sh4g_close(tp, &cg); }
      sh4g_u16(tp, (uint16_t)(0x8900 | (d_bt & 0xFF))); /* BT known_target */
      sh4g_const(tp, new_pc, SH4_REG_ARG0);     /* cold: R4 = new_pc */
      sh4g_block_exit_jmp(tp);                  /* exhausted: events + redispatch */
      return NULL;
    }
    {
      long d_bra = ((long)(uintptr_t)known_target -
                    ((long)(uintptr_t)(*tp + 4) + 4)) / 2;
      if (d_bra >= -2048 && d_bra <= 2047) {
        { sh4_codegen cg = sh4g_open(tp);
          sh4_emit_cmppl(&cg, SH4_REG_CYCLES);  /* T = (cycles > 0) */
          sh4g_close(tp, &cg); }
        bf = *tp;
        sh4g_u16(tp, 0x8B00);                   /* BF exhausted */
        sh4g_u16(tp, (uint16_t)(0xA000 | (d_bra & 0x0FFF))); /* BRA target */
        sh4g_u16(tp, 0x0009);                   /* delay NOP */
        { long d = ((long)(*tp) - ((long)bf + 4)) / 2; bf[1] = (uint8_t)(d & 0xFF); }
        sh4g_const(tp, new_pc, SH4_REG_ARG0);   /* cold: R4 = new_pc */
        sh4g_block_exit_jmp(tp);                /* exhausted: events + redispatch */
        return NULL;
      }
    }
  }
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmppl(&cg, SH4_REG_CYCLES);        /* T = (cycles > 0) */
    sh4g_close(tp, &cg); }
  bf = *tp;
  sh4g_u16(tp, 0x8B00);                         /* BF cold_exit (hot fallthrough: 1 cyc) */
  site = sh4g_emit_patch_jump(tp);              /* patched to BRA body_offset */
  { long d = ((long)(*tp) - ((long)bf + 4)) / 2; bf[1] = (uint8_t)(d & 0xFF); }
  sh4g_const(tp, new_pc, SH4_REG_ARG0);         /* cold: R4 = new_pc */
  sh4g_block_exit_jmp(tp);                      /* exhausted: events + redispatch */
  return site;
}

/* Idle-loop branch exit — the dynarec mirror of the interpreter's idle
 * handling (gba_over.h idle_loop_target_pc + the port's b-to-self detector,
 * cpu.cc) and of arm_emit.h's generate_branch_idle_eliminate: the branch burns
 * the REST of the cycle budget (R13 = 0, only when it is still positive, like
 * the interpreter's `cycles_remaining > 0` guard) and CALLS sh4_update_gba,
 * which commits R4 to reg[REG_PC] and fast-forwards hardware to the next event.
 * `idle_mode` selects how update_gba batches slices:
 *   1 = branch-to-self (only an IRQ can exit; always safe to batch until IRQ
 *       or frame end),
 *   2 = registered gba_over.h backward poll loop (batch only when IRQs can
 *       wake it; when IME/IE/CPSR.I mask IRQs, run a single event slice so
 *       VCOUNT/DISPSTAT polls never skip past their target scanline),
 *   3 = registered gba_over.h forward idle branch (preserve remaining cycles
 *       and let cgba_update_gba_slow() promote to mode 2 only on a second
 *       consecutive hit within 256 GBA cycles).
 * Returns the patch site. */
extern u32 cgba_idle_wait;   /* main.c: one-shot idle event batching */

SH4G_NOINLINE u8 *sh4g_branch_exit_idle(u8 **tp, uint32_t new_pc,
                                        const void *block_exit_fn,
                                        int idle_mode)
{
  u8 *site;
  sh4g_const(tp, new_pc, SH4_REG_ARG0);              /* R4 = target PC */
  sh4g_const(tp, (u32)(uintptr_t)&cgba_idle_wait, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, idle_mode, SH4_REG_T2);
    sh4_emit_mov_l_store(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4g_close(tp, &cg); }
  if (idle_mode != 3) {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmppl(&cg, SH4_REG_CYCLES);             /* T = (budget > 0) */
      sh4g_close(tp, &cg); }
    sh4g_u16(tp, 0x8B00);                              /* BF +0: keep <=0 budget */
    sh4g_u16(tp, (uint16_t)(0xE000 | (SH4_REG_CYCLES << 8))); /* MOV #0,R13 */
  }
  sh4g_vec_call(tp, SH4G_VEC_update_gba);            /* fast-forward events */
  sh4g_const(tp, new_pc, SH4_REG_ARG0);              /* R4 clobbered by C call */
#if CGBA_SH4_LAZY_CHAIN
  site = sh4g_emit_patch_call(tp);                   /* chain to target block */
#else
  site = sh4g_emit_patch_jump(tp);                   /* chain to target block */
#endif
  sh4g_patch_jump(site, block_exit_fn);              /* default: full exit / lazy link */
  return site;
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
  sh4_codegen cg = sh4g_open(tp);
  unsigned cc4 = cond & 0x0F;

  if (cc4 == 0x0E) { sh4_emit_sett(&cg); sh4g_close(tp, &cg); return; }   /* AL */
  if (cc4 == 0x0F) { sh4_emit_clrt(&cg); sh4g_close(tp, &cg); return; }   /* NV */

  switch (cc4) {
  case 0x0: /* EQ: Z (bit30) == 1 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    break;
  case 0x1: /* NE: Z (bit30) == 0 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  case 0x2: /* CS: C (bit29) == 1 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    break;
  case 0x3: /* CC: C (bit29) == 0 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  case 0x4: /* MI: N (bit31) == 1 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    break;
  case 0x5: /* PL: N (bit31) == 0 */
    sh4_emit_cmppz(&cg, SH4_REG_CPSR);
    break;
  case 0x6: /* VS: V (bit28) == 1 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    break;
  case 0x7: /* VC: V (bit28) == 0 */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  case 0x8: /* HI */ case 0x9: /* LS */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);                           /* R0 sign = C  */
    sh4_emit_not(&cg, SH4_REG_CPSR, SH4_REG_T1);                /* R2 = ~CPSR   */
    sh4_emit_shll(&cg, SH4_REG_T1);                             /* R2 sign = !Z */
    sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);                 /* R0 sign = HI */
    if (cc4 == 0x8) sh4_emit_shll(&cg, SH4_REG_RET);
    else            sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  case 0xA: /* GE */ case 0xB: /* LT */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);                            /* R0 sign = V  */
    sh4_emit_xor(&cg, SH4_REG_CPSR, SH4_REG_RET);               /* R0 sign = LT */
    if (cc4 == 0xB) sh4_emit_shll(&cg, SH4_REG_RET);
    else            sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  default:  /* 0xC GT / 0xD LE */
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);                            /* R0 sign = V  */
    sh4_emit_xor(&cg, SH4_REG_CPSR, SH4_REG_RET);               /* R0 sign = N^V*/
    sh4_emit_mov_reg(&cg, SH4_REG_CPSR, SH4_REG_T1);
    sh4_emit_shll(&cg, SH4_REG_T1);                             /* R2 sign = Z  */
    sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_RET);                  /* R0 sign = LE */
    if (cc4 == 0xD) sh4_emit_shll(&cg, SH4_REG_RET);
    else            sh4_emit_cmppz(&cg, SH4_REG_RET);
    break;
  }
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

/* Emit a forward BRA placeholder with `ADD #-debit, R13` packed into its
 * delay slot when debit is in 1..128 (otherwise delay NOP). */
static inline u8 *sh4g_emit_bra_debit(u8 **tp, int debit)
{
  u8 *site = *tp;
  sh4g_u16(tp, 0xA000);            /* BRA disp12 (patched) */
  if (debit > 0 && debit <= 128)
    sh4g_u16(tp, (uint16_t)(0x7000u | (SH4_REG_CYCLES << 8) |
                            ((-debit) & 0xFF)));
  else
    sh4g_u16(tp, 0x0009);
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

/* Fast RAM SMC bypass: branch (BT placeholder returned) when
 * `off_reg >= *(const u32 *)code_end_ptr`, proving all SMC tags at/above
 * `off_reg` are zero without touching the tag mirror. Clobbers tmp_ptr & tmp_val. */
static inline u8 *sh4g_emit_smc_bypass(u8 **tp, const void *code_end_ptr,
                                       unsigned off_reg, unsigned tmp_ptr,
                                       unsigned tmp_val)
{
  sh4g_const(tp, (u32)(uintptr_t)code_end_ptr, tmp_ptr);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, tmp_ptr, tmp_val);
    sh4_emit_cmphs(&cg, tmp_val, off_reg); /* T = (off_reg >= *code_end_ptr) */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bt_placeholder(tp);
}

/* Unrolled OR-reduction of `count` (1..16) 32-bit RAM SMC tag words at
 * `tag_base + off_reg`, guarded by `code_end_ptr` bypass. Returns a BF
 * placeholder taken when any tag word is non-zero; fast-bypass jumps cleanly
 * over the tag check. Clobbers tmp_ptr, tmp_acc, tmp_val (tmp_val may alias
 * tmp_ptr only when count == 1). */
static inline u8 *sh4g_emit_smc_words_guard(u8 **tp, const void *code_end_ptr,
                                            const void *tag_base,
                                            unsigned off_reg, unsigned count,
                                            unsigned tmp_ptr, unsigned tmp_acc,
                                            unsigned tmp_val)
{
  u8 *smc_ok = sh4g_emit_smc_bypass(tp, code_end_ptr, off_reg, tmp_ptr, tmp_acc);
  u8 *fail;
  unsigned w;
  sh4g_const(tp, (u32)(uintptr_t)tag_base, tmp_ptr);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, off_reg, tmp_ptr);
    sh4_emit_mov_l_load(&cg, tmp_ptr, tmp_acc);
    for (w = 1; w < count; w++) {
      sh4_emit_mov_l_load_disp(&cg, tmp_ptr, tmp_val, w);
      sh4_emit_or(&cg, tmp_val, tmp_acc);
    }
    sh4_emit_tst(&cg, tmp_acc, tmp_acc); /* T = all tag words clear */
    sh4g_close(tp, &cg); }
  fail = sh4g_emit_bf_placeholder(tp);
  sh4g_patch_cond(smc_ok, *tp);
  return fail;
}

/* Fragmented ROM8 (0x08008000..0x08008000+rom8_limit) 4 KiB page lookup.
 * In:  addr_reg (typically SH4_REG_T0) holds guest address.
 * Out: SH4_REG_T2 (R3) = non-NULL host 4 KiB page pointer; addr_reg preserved.
 * Clobbers SH4_REG_RET (R0) and SH4_REG_T1 (R2).
 * Appends 2 BT miss placeholders (out-of-range, NULL page) to miss_out[*nmiss]. */
static inline void sh4g_emit_rom8_page_lookup(u8 **tp, u32 rom8_limit,
                                              const void *frag_blocks_p8,
                                              unsigned addr_reg,
                                              u8 **miss_out, int *nmiss)
{
  sh4g_const(tp, 0x08008000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, addr_reg, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr - 0x08008000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, rom8_limit, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);     /* T = block<8 or >=size */
    sh4g_close(tp, &cg); }
  miss_out[(*nmiss)++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, -12, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);      /* R0 = block_idx - 8 */
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)frag_blocks_p8, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);        /* T = block == NULL */
    sh4g_close(tp, &cg); }
  miss_out[(*nmiss)++] = sh4g_emit_bt_placeholder(tp);
}

static inline u8 *sh4g_block_far_guard(u8 **tp, int slow_if_t)
{
  { sh4_codegen cg = sh4g_open(tp);
    if (slow_if_t) sh4_emit_bf(&cg, 1);               /* T==0 -> stay fast */
    else           sh4_emit_bt(&cg, 1);               /* T==1 -> stay fast */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bra_placeholder(tp);
}

/* Shared inline RAM fast path for Thumb POP {rlist, pc} and ARM LDM {..., pc}
 * (non-S, rn != 15). Guards alignment, single 32 KiB page, and EWRAM/IWRAM
 * (0x02/0x03), loads all registers in ascending order, applies optional base
 * writeback, charges sequential word cycles, and dispatches via ib_thumb/ib_arm. */
SH4G_NOINLINE void sh4g_emit_ram_ldm_pc(u8 **tp, unsigned base_reg,
                                        u32 rlist16, u32 count,
                                        int offset_a, int offset_nb,
                                        int do_wb, int is_thumb,
                                        const void *mem_map_read,
                                        const void *tramp_fn,
                                        const void *helper_fn,
                                        u32 opcode, u32 pc, int cycle_count)
{
  u8 *guards[5];
  int ng = 0;
  u32 i;

  sh4g_litseg_reserve_bytes(tp, 4, (u32)(140u + count * 16u));

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, base_reg, SH4_REG_T0);     /* R1 = orig base */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1);   /* R5 = A */
    if (offset_a != 0)
      sh4_emit_add_imm(&cg, offset_a, SH4_REG_ARG1);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);                          /* T = word aligned */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_far_guard(tp, 0);

  /* All words must live in one 32 KiB host page. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);
    sh4_emit_add_imm(&cg, (int)(count * 4) - 1, SH4_REG_T2);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_T2);
    sh4_emit_cmpeq(&cg, SH4_REG_RET, SH4_REG_T2);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_far_guard(tp, 0);

  sh4g_const(tp, 0x10000000u, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_ARG1);     /* T = out of GBA map */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_far_guard(tp, 1);

  /* RAM (0x02/0x03) only. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_shlr(&cg, SH4_REG_RET);                   /* R0 = A >> 25 */
    sh4_emit_cmpeq_imm(&cg, 1);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_far_guard(tp, 0);

  /* R3 = memory_map_read[A >> 15]. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)mem_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);         /* T = unmapped */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_block_far_guard(tp, 1);

  /* R0 = A & 0x7FFF. Load registers 0..14 then PC (15). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr(&cg, SH4_REG_RET);
    for (i = 0; i < 15u; i++) {
      if (!(rlist16 & (1u << i))) continue;
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, i);
    }
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_imm(&cg, 1, SH4_REG_ARG0);
    sh4_emit_not(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_PC);
    if (do_wb) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
      if (offset_nb != 0)
        sh4_emit_add_imm(&cg, offset_nb, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, base_reg);
    }
    sh4g_close(tp, &cg); }

  sh4g_charge_mem_run(tp, SH4_REG_ARG1, /*seq=*/1, /*is_word=*/1, count);
  if (cycle_count)
    sh4g_cycle_debit(tp, cycle_count);
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);
  sh4g_vec_jmp(tp, is_thumb ? SH4G_VEC_ib_thumb : SH4G_VEC_ib_arm);

  for (i = 0; i < (u32)ng; i++)
    sh4g_patch_bra(guards[i], *tp);
  sh4g_op2_tramp_call(tp, tramp_fn, helper_fn, opcode, pc, 1, cycle_count);
}

#endif /* CGBA_SH4_EMIT_GLUE_H */
