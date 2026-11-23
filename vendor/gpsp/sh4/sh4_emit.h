/* gameplaySP — SH-4A (SH7305 / fx-CG100) dynarec host emitter.
 *
 * This header is the seam cpu_threaded.c includes when SH4_ARCH is defined. It
 * implements gpSP's host-emitter macro contract for SH-4A, building on:
 *   - instruction encoder           ports/fxcg100/sh4/sh4_codegen.h
 *   - literal pool + reg[] access   ports/fxcg100/sh4/sh4_emit_core.h
 *   - emission glue + shifts/flags  ports/fxcg100/sh4/sh4_emit_glue.h
 *   - entry/exit trampoline         vendor/gpsp/sh4/sh4_stub.S
 *
 * Native SH-4A code is emitted for ARM/Thumb DP, shifts, multiply(-long),
 * branches, conditions, PSR, single/block fastmem transfers, and block-entry
 * fast paths; uncommon/side-effectful forms (SWP, SWI, RRX, PC-dest DP, SMC
 * hits, open-bus/backup I/O) fall back via compact trampolines to C helpers in
 * sh4_interp_helpers.c.
 */

#ifndef SH4_EMIT_H
#define SH4_EMIT_H

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_thumb_block_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_mul_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"
#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
#include "ports/fxcg100/sh4/sh4_thumb_udiv.h"
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
#include "ports/fxcg100/sh4/sh4_arm_mixer.h"
#include "ports/fxcg100/sh4/sh4_arm_pcm_clamp.h"
#endif
#ifdef CGBA_GBA_OVER_AZLE_IDLE
#include "ports/fxcg100/sh4/sh4_idle_signature.h"
#endif
#ifdef CGBA_SH4_M4A_MIX_SKIP
#include "ports/fxcg100/sh4/sh4_m4a_skip.h"
#include "ports/fxcg100/sh4/sh4_ea_mixer.h"
#include "ports/fxcg100/sh4/sh4_gax_mixer.h"
#endif

/* ------------------------------------------------------------------ */
/* Runtime symbols (sh4/sh4_stub.S, cpu.cc, cpu_threaded.c, helpers).  */
/* ------------------------------------------------------------------ */

u32  execute_arm_translate_internal(u32 cycles, void *reg_base);

/* All guest branches/redispatch funnel through this stub entry (R4 = PC). */
void sh4_block_exit(u32 pc);
/* Helper-return dispatcher (R4 = PC, R1 = helper code: 1 pure PC change,
 * 2 store alert) and the pure PC re-dispatch that skips update_gba. */
void sh4_helper_exit(u32 pc);
void sh4_pc_redispatch(u32 pc);
/* Compact helper-call trampolines (literal tuple read via PR; sh4_stub.S). */
void sh4_op2_tramp(void);
void sh4_op2_pc_tramp(void);
void sh4_op2_pc_mem_tramp(void);
u32  sh4_update_gba(u32 pc);
void sh4_indirect_branch_arm(u32 address);
void sh4_indirect_branch_thumb(u32 address);
void sh4_indirect_branch_dual(u32 address);
void sh4_indirect_branch_dual_thumb_current(u32 address);
#if CGBA_SH4_LAZY_CHAIN
void sh4_lazy_link_rom_arm(u32 address);
void sh4_lazy_link_rom_thumb(u32 address);
#endif
void sh4_bios_fallback_entry(void);
void sh4_cold_interp_entry(void);
/* Port-provided trap for an executed wild guest branch (untranslatable PC):
 * panics with the guest target on-screen instead of jumping to garbage. */
void cgba_sh4_wild_jump(u32 pc) __attribute__((noreturn));
void execute_swi(u32 pc);
void sh4_cheat_hook(void);

/* When set, the differential harness steps one block at a time: suppress direct
 * block chaining so every branch funnels through sh4_block_exit. */
extern int cgba_dynarec_single_block;

/* Per-game idle-loop PC from the gba_over database (0xFFFFFFFF when absent);
 * also declared in cpu.h — repeated here so this header stays standalone. */
extern u32 idle_loop_target_pc;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 *cgba_sh4_prof_counter_for_key(u32 key);
#endif

u32  execute_spsr_restore(u32 address);

/* Bring-up instruction helpers (sh4_interp_helpers.c) — interpret one guest
 * instruction against reg[]/memory using the shared C core. Handlers that can
 * change the guest PC return 1 (caller re-dispatches), else 0. */
int  cgba_sh4_thumb_ldst(u32 opcode, u32 pc);   /* returns 1 if a store alerted */
int  cgba_sh4_thumb_block(u32 opcode, u32 pc);
int  cgba_sh4_arm_dp(u32 opcode, u32 pc);
int  cgba_sh4_arm_ldst(u32 opcode, u32 pc);
int  cgba_sh4_arm_block(u32 opcode, u32 pc);
void cgba_sh4_arm_multiply(u32 opcode, u32 pc);
void cgba_sh4_arm_multiply_long(u32 opcode, u32 pc);
int  cgba_sh4_arm_psr(u32 opcode, u32 pc);   /* returns 1 if an IRQ changed PC */
int cgba_sh4_arm_swap(u32 opcode, u32 pc);
void cgba_sh4_hle_div(u32 cpu_mode, u32 pc);

extern volatile s32 cgba_sh4_fastpath_budget;
#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
int cgba_sh4_thumb_udiv_loop_try(u32 unused, u32 pc);
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
int cgba_sh4_arm_mixer_loop_try(u32 unused, u32 pc);
int cgba_sh4_arm_pcm_clamp_loop_try(u32 unused, u32 pc);
#endif
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
int cgba_sh4_ea_mixer_try(u32 opcode, u32 pc);
int cgba_sh4_audio_mixer_try(u32 kind, u32 pc);
#endif

#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
#include "ports/fxcg100/sh4/sh4_cache.h"
#endif

/* Instruction tracing is compiled out (no TRACE_INSTRUCTIONS support yet). */
#define emit_trace_thumb_instruction(pc)
#define emit_trace_arm_instruction(pc)

/* ================================================================== */
/* Block driver glue                                                   */
/* ================================================================== */

/* No prologue and no per-block trampolines: every far reference is a
 * self-contained inline literal (sh4_emit_glue.h), so block entry is the first
 * real instruction. */
#define block_prologue_size 0

#if defined(CGBA_SH4_THUMB_UDIV_FASTPATH) || \
    defined(CGBA_SH4_ARM_MIXER_FASTPATH) || \
    defined(CGBA_SH4_M4A_LOOP_FOLD) || \
    (defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP)
static inline int sh4g_range_has_observer(u32 start, u32 end)
{
  u32 i;
  if ((cheat_master_hook >= start && cheat_master_hook < end) ||
      (idle_loop_target_pc >= start && idle_loop_target_pc < end))
    return 1;
  for (i = 0; i < translation_gate_targets; i++) {
    if (translation_gate_target_pc[i] >= start &&
        translation_gate_target_pc[i] < end)
      return 1;
  }
  return 0;
}
#endif

#if defined(CGBA_SH4_THUMB_UDIV_FASTPATH) || defined(CGBA_SH4_ARM_MIXER_FASTPATH)
/* Entry-only speculative loop prefix shared by udiv / ARM mixer / PCM clamp.
 * On decline the helper committed nothing and BT falls through into the
 * normal block body; on hit R13 remains positive by construction, we debit
 * cgba_sh4_extra_cycles, and redispatch at `resume_pc`. */
static inline void sh4g_emit_speculative_loop_entry(u8 **tp,
                                                    volatile s32 *budget_var,
                                                    const void *try_fn,
                                                    u32 first_word, u32 pc,
                                                    u32 resume_pc)
{
  u8 *fallback;
  sh4g_const(tp, (u32)(uintptr_t)budget_var, SH4_REG_T0);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store(&cg, SH4_REG_CYCLES, SH4_REG_T0);
    sh4g_close(tp, &cg);
  }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_tramp,
                      try_fn, first_word, pc, 0, 0);
  sh4g_litseg_reserve(tp, 8);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst(&cg, SH4_REG_RET, SH4_REG_RET); /* T = helper declined */
    sh4g_close(tp, &cg);
  }
  fallback = sh4g_emit_bt_placeholder(tp);
  sh4g_cycle_debit_from_global(tp, &cgba_sh4_extra_cycles);
  sh4g_const(tp, resume_pc, SH4_REG_ARG0);
  sh4g_vec_jmp(tp, SH4G_VEC_pc_redispatch);
  sh4g_patch_cond_skip(fallback, *tp);
}
#endif

#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
/* Match guest opcodes through readaddress16(): the ROM mapping is host-endian
 * and may be NOR-backed, so raw memcmp would be wrong on big-endian SH4. */
static inline int sh4g_thumb_udiv_loop_match_map(const u8 *map, u32 pc)
{
  u32 off = pc & 0x0FFFu;
  u32 i;
  if (!map || off + CGBA_SH4_THUMB_UDIV_LOOP_BYTES > 0x1000u)
    return 0;
  for (i = 0; i < CGBA_SH4_THUMB_UDIV_LOOP_HALFWORDS; i++) {
    if (readaddress16(map, off + i * 2u) !=
        cgba_sh4_thumb_udiv_loop_pattern[i])
      return 0;
  }
  return 1;
}

static inline void sh4g_thumb_udiv_loop_entry(u8 **tp, const u8 *map, u32 pc)
{
  if (!sh4g_thumb_udiv_loop_match_map(map, pc) ||
      sh4g_range_has_observer(pc, pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES))
    return;
  sh4g_emit_speculative_loop_entry(tp, &cgba_sh4_fastpath_budget,
                                   (const void *)cgba_sh4_thumb_udiv_loop_try,
                                   (u32)cgba_sh4_thumb_udiv_loop_pattern[0],
                                   pc, pc + CGBA_SH4_THUMB_UDIV_LOOP_BYTES);
}

#endif /* CGBA_SH4_THUMB_UDIV_FASTPATH */

#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
/* Match through readaddress32(): RAM/gamepak mappings use guest byte order and
 * cannot be compared with a host-endian raw memcmp on SH4. */
static inline int sh4g_arm_mixer_match_map(const u8 *map, u32 pc)
{
  u32 off = pc & 0x0FFFu;
  u32 i;
  if (!map || off + CGBA_SH4_ARM_MIXER_BYTES > 0x1000u)
    return 0;
  if (readaddress32(map, off) != cgba_sh4_arm_mixer_step_pattern[0])
    return 0;
  for (i = 1; i < CGBA_SH4_ARM_MIXER_WORDS; i++) {
    if (readaddress32(map, off + i * 4u) != cgba_sh4_arm_mixer_word(i))
      return 0;
  }
  return 1;
}

static inline void sh4g_arm_mixer_entry(u8 **tp, const u8 *map, u32 pc)
{
  if (!sh4g_arm_mixer_match_map(map, pc) ||
      sh4g_range_has_observer(pc, pc + CGBA_SH4_ARM_MIXER_BYTES))
    return;
  sh4g_emit_speculative_loop_entry(tp, &cgba_sh4_fastpath_budget,
                                   (const void *)cgba_sh4_arm_mixer_loop_try,
                                   cgba_sh4_arm_mixer_step_pattern[0],
                                   pc, pc + CGBA_SH4_ARM_MIXER_BODY_BYTES);
}

static inline int sh4g_arm_pcm_clamp_match_map(const u8 *map, u32 pc)
{
  u32 off = pc & 0x0FFFu;
  u32 i;
  if (!map || off + CGBA_SH4_ARM_PCM_CLAMP_BYTES > 0x1000u)
    return 0;
  if (readaddress32(map, off) != cgba_sh4_arm_pcm_clamp_step_pattern[0])
    return 0;
  for (i = 1; i < CGBA_SH4_ARM_PCM_CLAMP_WORDS; i++) {
    if (readaddress32(map, off + i * 4u) != cgba_sh4_arm_pcm_clamp_word(i))
      return 0;
  }
  return 1;
}

static inline void sh4g_arm_pcm_clamp_entry(u8 **tp, const u8 *map, u32 pc)
{
  if (!sh4g_arm_pcm_clamp_match_map(map, pc) ||
      sh4g_range_has_observer(pc, pc + CGBA_SH4_ARM_PCM_CLAMP_BYTES))
    return;
  sh4g_emit_speculative_loop_entry(tp, &cgba_sh4_fastpath_budget,
                                   (const void *)cgba_sh4_arm_pcm_clamp_loop_try,
                                   cgba_sh4_arm_pcm_clamp_step_pattern[0],
                                   pc, pc + CGBA_SH4_ARM_PCM_CLAMP_BODY_BYTES);
}
#endif /* CGBA_SH4_ARM_MIXER_FASTPATH */

#ifdef CGBA_SH4_M4A_MIX_SKIP
/* See sh4_m4a_skip.h.  Structural loop-level matcher: every self-contained
 * MP2K/m4a ARM inner loop (reverb, fixed-rate main/tail, interpolating Flavor A
 * and Flavor B, with any stereo buffer stride) is re-verified against live RAM
 * words when translating ARM RAM blocks.  Later guest writes trigger ordinary
 * SMC invalidation and re-verification. */
static cgba_sh4_m4a_match sh4g_m4a_cache[4];
static unsigned sh4g_m4a_cache_next;
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 cgba_sh4_m4a_skip_drop_count;
u32 cgba_sh4_m4a_skip_mem_count;
#define SH4G_M4A_SKIP_STAT(expr) ((void)(expr))
#else
#define SH4G_M4A_SKIP_STAT(expr) ((void)0)
#endif

static inline int sh4g_m4a_read_word(u32 addr, uint32_t *out)
{
  const u8 *map = cgba_memory_map_read_4k(addr);
  if (!map)
    return 0;
  *out = (uint32_t)readaddress32(map, addr & 0x0FFFu);
  return 1;
}

static inline int sh4g_m4a_in_match(const cgba_sh4_m4a_match *m, u32 pc)
{
  s32 rel;
  if (!m || m->kind == CGBA_SH4_M4A_KIND_NONE)
    return 0;
  rel = (s32)(pc - m->head_pc);
  return rel >= -(s32)m->pre_bytes && rel < (s32)m->body_bytes;
}

static inline void sh4g_m4a_cache_put(const cgba_sh4_m4a_match *m)
{
  unsigned i;
  for (i = 0; i < 4u; i++) {
    if (sh4g_m4a_cache[i].kind != CGBA_SH4_M4A_KIND_NONE &&
        sh4g_m4a_cache[i].head_pc == m->head_pc) {
      sh4g_m4a_cache[i] = *m;
      return;
    }
  }
  sh4g_m4a_cache[sh4g_m4a_cache_next & 3u] = *m;
  sh4g_m4a_cache_next++;
}

static inline int sh4g_m4a_skip_match_head(u32 head_pc, cgba_sh4_m4a_match *out)
{
  uint32_t w[CGBA_SH4_M4A_MAX_LOOP_WORDS];
  u32 page = 0xFFFFFFFFu;
  const u8 *map = NULL;
  u32 i, n, max_w, min_w, reg;
  int has_pre = 0;

  if ((head_pc & 3u) ||
      ((reg = head_pc >> 24) != 0x02u && reg != 0x03u))
    return 0;
  if (!sh4g_m4a_read_word(head_pc, &w[0]))
    return 0;
  if (w[0] == 0xE19500D6u) {
    max_w = 17u;
    min_w = 17u;
  } else if (w[0] == 0xE1D500D0u) {
    max_w = 12u;
    min_w = 12u;
  } else if (w[0] == 0xE5956000u) {
    max_w = CGBA_SH4_M4A_MAX_LOOP_WORDS;
    min_w = 10u;
  } else {
    return 0;
  }

  n = (0x01000000u - (head_pc & 0x00FFFFFFu)) >> 2;
  if (n > max_w)
    n = max_w;
  if (n < min_w)
    return 0;

  for (i = 1u; i < n; i++) {
    u32 addr = head_pc + (i << 2);
    if ((addr >> 12) != page) {
      page = addr >> 12;
      map = cgba_memory_map_read_4k(addr);
      if (!map) {
        n = i;
        break;
      }
    }
    w[i] = (uint32_t)readaddress32(map, addr & 0x0FFFu);
  }
  if (n < min_w)
    return 0;

  if (w[0] == 0xE5956000u && (head_pc & 0x00FFFFFFu) >= 12u) {
    uint32_t p0, p1, p2;
    if (sh4g_m4a_read_word(head_pc - 12u, &p0) && p0 == 0xE1D300D0u &&
        sh4g_m4a_read_word(head_pc - 8u, &p1)  && p1 == 0xE1F310D1u &&
        sh4g_m4a_read_word(head_pc - 4u, &p2)  && p2 == 0xE0411000u)
      has_pre = 1;
  }

  return cgba_sh4_m4a_match_loop_words(w, n, head_pc, has_pre, out);
}

static inline cgba_sh4_m4a_match sh4g_m4a_skip_locate(u32 pc)
{
  cgba_sh4_m4a_match out;
  cgba_sh4_m4a_match tmp;
  u32 reg, hi, lo, cand;
  unsigned i;

  out.kind = CGBA_SH4_M4A_KIND_NONE;
  out.head_pc = 0;
  out.pre_bytes = 0;
  out.body_bytes = 0;
  out.stride = 0;

  pc &= ~3u;
  reg = pc >> 24;
  if (cgba_dynarec_single_block || (reg != 0x02u && reg != 0x03u))
    return out;

  for (i = 0; i < 4u; i++) {
    if (sh4g_m4a_in_match(&sh4g_m4a_cache[i], pc)) {
      if (sh4g_m4a_skip_match_head(sh4g_m4a_cache[i].head_pc, &tmp) &&
          sh4g_m4a_in_match(&tmp, pc)) {
        sh4g_m4a_cache[i] = tmp;
        return tmp;
      }
      sh4g_m4a_cache[i].kind = CGBA_SH4_M4A_KIND_NONE;
    }
  }

  hi = pc + 12u;
  if ((hi >> 24) != reg)
    hi = (pc & 0xFF000000u) | 0x00FFFFFCu;
  lo = ((pc & 0x00FFFFFFu) >= 26u * 4u) ? (pc - 26u * 4u) : (pc & 0xFF000000u);
  for (cand = hi; ; cand -= 4u) {
    if (sh4g_m4a_skip_match_head(cand, &tmp) && sh4g_m4a_in_match(&tmp, pc)) {
      sh4g_m4a_cache_put(&tmp);
      return tmp;
    }
    if (cand == lo)
      break;
  }
  return out;
}

static inline void sh4g_m4a_skip_step(cgba_sh4_m4a_match *m, const u8 *map,
                                      u32 pc, u32 pos)
{
  u32 op;
  cgba_sh4_m4a_match tmp;

  if (pos == 0u || cgba_dynarec_single_block)
    return;
  if (sh4g_m4a_in_match(m, pc))
    return;
  m->kind = CGBA_SH4_M4A_KIND_NONE;
  if (((pc >> 24) != 0x02u && (pc >> 24) != 0x03u) || !map)
    return;
  op = readaddress32(map, pc & 0x0FFFu);
  if (op == 0xE19500D6u || op == 0xE1D500D0u || op == 0xE5956000u) {
    if (sh4g_m4a_skip_match_head(pc, &tmp)) {
      sh4g_m4a_cache_put(&tmp);
      *m = tmp;
    }
  } else if (op == 0xE1D300D0u) {
    u32 head = pc + 12u;
    if ((head >> 24) == (pc >> 24) &&
        sh4g_m4a_skip_match_head(head, &tmp) && tmp.pre_bytes == 12u) {
      sh4g_m4a_cache_put(&tmp);
      *m = tmp;
    }
  }
}

/* Returns 1 when the instruction at `pc` was handled here (nothing, or
 * writeback + memory cycle charge, emitted); 0 to translate it normally. */
static inline int sh4g_m4a_skip_emit(u8 **tp, const cgba_sh4_m4a_match *match,
                                     u32 pc, u32 opcode)
{
  cgba_sh4_m4a_skip_mem m;
  unsigned cls;

  if (!match || match->kind == CGBA_SH4_M4A_KIND_NONE)
    return 0;
  cls = cgba_sh4_m4a_match_class(match, pc, opcode);
  if (cls == CGBA_SH4_M4A_SKIP_DROP) {
    SH4G_M4A_SKIP_STAT(cgba_sh4_m4a_skip_drop_count++);
    return 1;
  }
  if (cls != CGBA_SH4_M4A_SKIP_MEM ||
      !cgba_sh4_m4a_skip_mem_decode(opcode, &m))
    return 0;

  /* T0 = transfer address.  Post-indexed forms transfer at the old base. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, m.rn, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  if (m.reg_offset || m.offset) {
    if (m.reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, m.rm, SH4_REG_T1);
      sh4g_close(tp, &cg);
    } else {
      sh4g_const(tp, m.offset, SH4_REG_T1);
    }
    { sh4_codegen cg = sh4g_open(tp);
      unsigned dst = m.pre ? SH4_REG_T0 : SH4_REG_ARG2;
      if (!m.pre)
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      if (m.up)
        sh4_emit_add_reg(&cg, SH4_REG_T1, dst);
      else
        sh4_emit_sub(&cg, SH4_REG_T1, dst);
      if (m.writeback)
        sh4_emit_store_greg(&cg, dst, m.rn);
      sh4g_close(tp, &cg); }
  }
  /* Same table and column as cgba_sh4_charge_mem(): one nonsequential access
   * priced by the transfer address's region. */
  sh4g_charge_mem_run(tp, SH4_REG_T0, /*seq=*/0, (int)m.is_word, 1);
  SH4G_M4A_SKIP_STAT(cgba_sh4_m4a_skip_mem_count++);
  return 1;
}

#ifdef CGBA_SH4_M4A_LOOP_FOLD
int cgba_sh4_m4a_loop_try(u32 packed, u32 pc);

/* Prefix at a verified loop head.  `at_seam` is the translator's guarantee
 * that every earlier instruction's cycles have been debited (block start or a
 * branch-target flush), so R13 is the true budget.  The helper leaves guest
 * state at the same loop head; the ordinary translation follows and runs
 * whatever was not folded. */
static inline void sh4g_m4a_loop_entry(u8 **tp, const cgba_sh4_m4a_match *m,
                                       u32 pc, int at_seam)
{
  u32 packed;
  if (!m || m->kind == CGBA_SH4_M4A_KIND_NONE || !at_seam || pc != m->head_pc)
    return;
  if (m->kind != CGBA_SH4_M4A_KIND_REVERB &&
      m->kind != CGBA_SH4_M4A_KIND_FIXED &&
      m->kind != CGBA_SH4_M4A_KIND_INTERP_A &&
      m->kind != CGBA_SH4_M4A_KIND_INTERP_B &&
      m->kind != CGBA_SH4_M4A_KIND_MONO_REVERB &&
      m->kind != CGBA_SH4_M4A_KIND_MONO_FIXED &&
      m->kind != CGBA_SH4_M4A_KIND_MONO_INTERP_A &&
      m->kind != CGBA_SH4_M4A_KIND_MONO_INTERP_B)
    return;
  if (sh4g_range_has_observer(m->head_pc, m->head_pc + m->body_bytes))
    return;
  packed = (u32)m->kind | ((u32)m->stride << 8);
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_fastpath_budget, SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store(&cg, SH4_REG_CYCLES, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_tramp,
                      (const void *)cgba_sh4_m4a_loop_try, packed, pc, 0, 0);
  sh4g_cycle_debit_from_global(tp, &cgba_sh4_extra_cycles);
}
#endif

#define sh4_m4a_skip_active() (sh4_m4a_cur_match.kind != CGBA_SH4_M4A_KIND_NONE)
#define arm_translate_override()                                             \
  sh4g_m4a_skip_emit(&translation_ptr, &sh4_m4a_cur_match, (u32)pc,          \
                     (u32)opcode)
#define SH4_M4A_SKIP_BLOCK_VARS                                              \
  ; cgba_sh4_m4a_match sh4_m4a_cur_match = sh4g_m4a_skip_locate((u32)pc)
#else
#define SH4_M4A_SKIP_BLOCK_VARS
#endif /* CGBA_SH4_M4A_MIX_SKIP */

#if defined(CGBA_SH4_M4A_MIX_SKIP) || defined(CGBA_SH4_M4A_LOOP_FOLD) || \
    defined(CGBA_SH4_ARM_MIXER_FASTPATH)
static inline void sh4g_arm_seam_fastpaths(u8 **tp, const u8 *map, u32 pc,
#ifdef CGBA_SH4_M4A_MIX_SKIP
                                           cgba_sh4_m4a_match *m4a_match,
#else
                                           void *m4a_match,
#endif
                                           u32 pos, int update_cycles)
{
#ifdef CGBA_SH4_M4A_MIX_SKIP
  sh4g_m4a_skip_step(m4a_match, map, pc, pos);
#ifdef CGBA_SH4_M4A_LOOP_FOLD
  sh4g_m4a_loop_entry(tp, m4a_match, pc, pos == 0u || update_cycles);
#endif
#else
  (void)m4a_match;
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
  /* Block start (pos == 0) already runs entry fastpaths in
   * sh4g_block_entry_fastpaths(); catch mid-block loop heads (e.g. a mixer
   * or PCM clamp prologue falling straight into the loop body) at their
   * cycle seam too. */
  if (pos > 0u && update_cycles) {
    sh4g_arm_mixer_entry(tp, map, pc);
    sh4g_arm_pcm_clamp_entry(tp, map, pc);
  }
#else
  (void)tp; (void)map; (void)pc; (void)pos; (void)update_cycles;
#endif
}
#ifdef CGBA_SH4_M4A_MIX_SKIP
#define SH4_M4A_SKIP_MATCH_ARG (&sh4_m4a_cur_match)
#else
#define SH4_M4A_SKIP_MATCH_ARG ((void *)0)
#endif
#define arm_instruction_prefix()                                             \
  sh4g_arm_seam_fastpaths(&translation_ptr, pc_address_block, (u32)pc,       \
    SH4_M4A_SKIP_MATCH_ARG, (u32)block_data_position,                        \
    (int)block_data[block_data_position].update_cycles)
#endif

#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
static inline int sh4g_ea_mixer_match_iwram(u32 pc)
{
  u32 off, h, i;
  if ((pc >> 24) != 0x03u || (pc & 3u) != 0u)
    return 0;
  off = pc & 0x7FFFu;
  if (off + CGBA_SH4_EA_MIXER_BYTES > 0x8000u)
    return 0;
  if (readaddress32(iwram + 0x8000u, off + 0u) != CGBA_SH4_EA_MIXER_W0 ||
      readaddress32(iwram + 0x8000u, off + 4u) != CGBA_SH4_EA_MIXER_W1 ||
      readaddress32(iwram + 0x8000u, off + 8u) != CGBA_SH4_EA_MIXER_W2 ||
      readaddress32(iwram + 0x8000u, off + 12u) != CGBA_SH4_EA_MIXER_W3)
    return 0;
  h = 0x811C9DC5u;
  for (i = 0u; i < CGBA_SH4_EA_MIXER_WORDS; i++)
    h = cgba_sh4_ea_mixer_fnv1a_step(
      h, readaddress32(iwram + 0x8000u, off + i * 4u));
  if (h != CGBA_SH4_EA_MIXER_FNV1A)
    return 0;
  return !sh4g_range_has_observer(pc, pc + CGBA_SH4_EA_MIXER_BYTES);
}

static inline void sh4g_ea_mixer_entry(u8 **tp, u32 pc)
{
  if (!sh4g_ea_mixer_match_iwram(pc))
    return;
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_fastpath_budget, SH4_REG_T0);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store(&cg, SH4_REG_CYCLES, SH4_REG_T0);
    sh4g_close(tp, &cg);
  }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                      (const void *)cgba_sh4_ea_mixer_try,
                      CGBA_SH4_EA_MIXER_W0, pc, 1, 0);
}

int cgba_sh4_4ch_resample_match_iwram(u32 off);

static inline void sh4g_audio_mixer_entry(u8 **tp, u32 pc)
{
  u32 off, avail, i;
  uint32_t nwords = 0u;
  uint32_t wbuf[CGBA_SH4_AUDIO_MIXER_MAX_WORDS];
  u32 w0;
  int kind = CGBA_SH4_AUDIO_MIXER_NONE;

  if ((pc >> 24) != 0x03u || (pc & 3u) != 0u)
    return;
  off = pc & 0x7FFFu;
  if (off + 8u * 4u > 0x8000u)
    return;
  w0 = readaddress32(iwram + 0x8000u, off);
  if (w0 != 0xE92D0FF0u && w0 != 0xE92D4FF0u &&
      w0 != 0xE92D0FF1u && w0 != 0xE92D5FFEu &&
      w0 != 0xE92D4000u)
    return;
  if (w0 == 0xE92D4000u && off + CGBA_SH4_4CH_RESAMPLE_WORDS * 4u <= 0x8000u &&
      readaddress32(iwram + 0x8000u, off + 4u) == 0xE92D1FFFu) {
    if (cgba_sh4_4ch_resample_match_iwram(off)) {
      kind = CGBA_SH4_AUDIO_MIXER_4CH_RESAMPLE;
      nwords = CGBA_SH4_4CH_RESAMPLE_WORDS;
    }
  }
  if (kind == CGBA_SH4_AUDIO_MIXER_NONE) {
    avail = (0x8000u - off) >> 2;
    if (avail > CGBA_SH4_AUDIO_MIXER_MAX_WORDS)
      avail = CGBA_SH4_AUDIO_MIXER_MAX_WORDS;
    for (i = 0u; i < avail; i++)
      wbuf[i] = readaddress32(iwram + 0x8000u, off + i * 4u);
    kind = cgba_sh4_audio_mixer_match_words(wbuf, avail, &nwords);
  }
  if (kind == CGBA_SH4_AUDIO_MIXER_NONE)
    return;
  if (sh4g_range_has_observer(pc, pc + nwords * 4u))
    return;
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_fastpath_budget, SH4_REG_T0);
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store(&cg, SH4_REG_CYCLES, SH4_REG_T0);
    sh4g_close(tp, &cg);
  }
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                      (const void *)cgba_sh4_audio_mixer_try,
                      (u32)kind, pc, 1, 0);
}
#endif

static inline void sh4g_block_entry_fastpaths(u8 **tp, const u8 *map,
                                               u32 pc, int thumb)
{
#ifdef CGBA_SH4_THUMB_UDIV_FASTPATH
  if (thumb)
    sh4g_thumb_udiv_loop_entry(tp, map, pc);
#endif
#ifdef CGBA_SH4_ARM_MIXER_FASTPATH
  if (!thumb) {
    sh4g_arm_mixer_entry(tp, map, pc);
    sh4g_arm_pcm_clamp_entry(tp, map, pc);
  }
#endif
#if defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP
  if (!thumb) {
    sh4g_ea_mixer_entry(tp, pc);
    sh4g_audio_mixer_entry(tp, pc);
  }
#endif
#if !defined(CGBA_SH4_THUMB_UDIV_FASTPATH) && \
    !defined(CGBA_SH4_ARM_MIXER_FASTPATH) && \
    !(defined(CGBA_SH4_M4A_MIX_SKIP) && CGBA_SH4_M4A_MIX_SKIP)
  (void)tp; (void)map; (void)pc; (void)thumb;
#endif
}

#define generate_block_extra_vars_arm()                                      \
  int sh4_block_is_thumb = 0;                                                \
  u32 sh4_arm_const_mask = 0;                                                \
  u32 sh4_arm_const_val[16] = {0} SH4_M4A_SKIP_BLOCK_VARS
#define generate_block_extra_vars_thumb()                                    \
  int sh4_block_is_thumb = 1;                                                \
  u32 sh4_thumb_const_mask = 0;                                              \
  u32 sh4_thumb_const_val[16] = {0}
#define generate_block_prologue()                                            \
  sh4g_block_entry_fastpaths(&translation_ptr, pc_address_block, pc,         \
                              sh4_block_is_thumb)

#define sh4_thumb_const_clear_all()                                            \
  do { sh4_thumb_const_mask = 0; } while(0)
#define sh4_thumb_const_kill(reg_index)                                        \
  do { sh4_thumb_const_mask &= ~(1u << ((reg_index) & 15)); } while(0)
#define sh4_thumb_const_set(reg_index, value)                                  \
  do { unsigned _ct_r = ((unsigned)(reg_index)) & 15u;                         \
       sh4_thumb_const_val[_ct_r] = (u32)(value);                              \
       sh4_thumb_const_mask |= (1u << _ct_r); } while(0)

#define sh4_arm_const_clear_all()                                              \
  do { sh4_arm_const_mask = 0; } while(0)
#define sh4_arm_const_kill(reg_index)                                          \
  do { sh4_arm_const_mask &= ~(1u << ((reg_index) & 15)); } while(0)
#define sh4_arm_const_set(reg_index, value)                                    \
  do { unsigned _ca_r = ((unsigned)(reg_index)) & 15u;                         \
       sh4_arm_const_val[_ca_r] = (u32)(value);                                \
       sh4_arm_const_mask |= (1u << _ca_r); } while(0)

/* Lightweight unconditional ARM DP constant propagation (killing Rd on any
 * conditional write or non-constant op) so constant IWRAM/EWRAM/IO bases
 * synthesized via rotated-immediate MOV/ADD/SUB/ORR/BIC or literal-pool LDR
 * hit sh4g_arm_ldst_const_native(). */
static inline void sh4g_arm_const_update_dp(u32 opcode, u32 condition, u32 pc,
                                            u32 *mask, u32 val[16])
{
  u32 alu = (opcode >> 21) & 0xFu;
  u32 rd = (opcode >> 12) & 0xFu;
  u32 rn = (opcode >> 16) & 0xFu;
  u32 m = *mask;
  int unary = (alu == 0xDu || alu == 0xFu);

  if (alu >= 0x8u && alu <= 0xBu)
    return;                                    /* TST/TEQ/CMP/CMN */
  if (rd == 15u) {
    *mask = 0u;
    return;
  }
  if (condition != 0x0Eu) {
    *mask = m & ~(1u << rd);
    return;
  }
  if (opcode & 0x02000000u) {                  /* immediate operand2 */
    u32 imm = opcode & 0xFFu;
    u32 rot = ((opcode >> 8) & 0xFu) * 2u;
    u32 op2 = rot ? ((imm >> rot) | (imm << (32u - rot))) : imm;
    if (unary) {
      val[rd] = (alu == 0xDu) ? op2 : ~op2;
      *mask = m | (1u << rd);
      return;
    }
    if (rn == 15u || (m & (1u << rn))) {
      u32 a = (rn == 15u) ? (pc + 8u) : val[rn];
      switch (alu) {
      case 0x0u: val[rd] = a & op2;  break;
      case 0x1u: val[rd] = a ^ op2;  break;
      case 0x2u: val[rd] = a - op2;  break;
      case 0x3u: val[rd] = op2 - a;  break;
      case 0x4u: val[rd] = a + op2;  break;
      case 0xCu: val[rd] = a | op2;  break;
      case 0xEu: val[rd] = a & ~op2; break;
      default:   *mask = m & ~(1u << rd); return;
      }
      *mask = m | (1u << rd);
      return;
    }
  } else if ((opcode & 0x00000FF0u) == 0u) {   /* unshifted register operand2 */
    u32 rm = opcode & 0xFu;
    if (rm != 15u && (m & (1u << rm))) {
      u32 b = val[rm];
      if (unary) {
        val[rd] = (alu == 0xDu) ? b : ~b;
        *mask = m | (1u << rd);
        return;
      }
      if (rn != 15u && (m & (1u << rn))) {
        u32 a = val[rn];
        switch (alu) {
        case 0x0u: val[rd] = a & b;  break;
        case 0x1u: val[rd] = a ^ b;  break;
        case 0x2u: val[rd] = a - b;  break;
        case 0x3u: val[rd] = b - a;  break;
        case 0x4u: val[rd] = a + b;  break;
        case 0xCu: val[rd] = a | b;  break;
        case 0xEu: val[rd] = a & ~b; break;
        default:   *mask = m & ~(1u << rd); return;
        }
        *mask = m | (1u << rd);
        return;
      }
    }
  }
  *mask = m & ~(1u << rd);
}

static inline unsigned sh4g_thumb_ldst_reg_index(u32 opcode)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  if ((hi >= 0x48u && hi <= 0x4Fu) || (hi >= 0x90u && hi <= 0x9Fu))
    return (opcode >> 8) & 7u;
  return opcode & 7u;
}

static inline int sh4g_thumb_ldst_is_load(u32 opcode)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  if (hi <= 0x4Fu)
    return 1;
  if (hi <= 0x5Fu)
    return hi >= 0x56u;
  return ((opcode >> 11) & 1u) != 0;
}

#if (defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)) && \
    (!defined(CGBA_SH4_BLOCK_PROFILE) || CGBA_SH4_BLOCK_PROFILE)
static inline void sh4g_prof_block_entry(u8 **tp, u32 pc, int thumb)
{
  u32 *counter = cgba_sh4_prof_counter_for_key((pc & ~1u) | (thumb ? 1u : 0u));
  if(!counter)
    return;
  sh4g_const(tp, (u32)(uintptr_t)counter, SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T0, SH4_REG_T1);
    sh4_emit_add_imm(&cg, 1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
}
#define generate_prof_block_entry(is_thumb) \
  sh4g_prof_block_entry(&translation_ptr, (u32)pc, (is_thumb))
#else
#define generate_prof_block_entry(is_thumb) do {} while(0)
#endif

#define generate_cycle_update()                                               \
  do { if(cycle_count != 0) {                                                 \
         sh4g_cycle_sub(&translation_ptr, (int)cycle_count, (u32)pc);        \
         cycle_count = 0;                                                     \
       } } while(0)

/* Loop-break gate only (no flush) emitted AT a block-entry / loop-back target. */
#define generate_cycle_gate(is_word)                                          \
  sh4g_cycle_gate(&translation_ptr, (u32)pc, (is_word))

#define generate_load_reg_pc(hostreg, reg_index, pc_offset)                   \
  do { if((reg_index) == REG_PC)                                              \
         sh4g_const(&translation_ptr, (u32)(pc + (pc_offset)), (hostreg));    \
       else                                                                   \
         sh4g_load_greg(&translation_ptr, (reg_index), (hostreg));            \
  } while(0)

/* Patch sites: unconditional jump chain (near BRA / far literal), conditional
 * BT/BF disp8. Direct chaining is suppressed while the single-block harness is
 * stepping, so each branch redispatches through sh4_block_exit and can be
 * diffed lockstep. */
#define generate_branch_patch_unconditional(dest, offset)                     \
  do { if(!cgba_dynarec_single_block)                                         \
         sh4g_chain_patch((u8 *)(dest), (const void *)(offset)); } while(0)
#define generate_branch_patch_internal(dest, offset)                          \
  do { if((dest) != NULL)                                                     \
         sh4g_chain_patch((u8 *)(dest), (const void *)(offset)); } while(0)
#define generate_branch_patch_conditional(dest, offset)                       \
  sh4g_patch_cond_skip((u8 *)(dest), (const void *)(offset))

/* Re-dispatch the block at `pc` (used when a block runs off its end or hits a
 * translation gate). Flush accumulated block cycles first (the flush exits to
 * sh4_block_exit when the budget is spent, so run-off/gate loops still give
 * update_gba() a chance to retire events); the dispatch itself is a pure PC
 * change and skips the update_gba pass. */
#define generate_translation_gate(type)                                       \
  do { generate_cycle_update();                                               \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG0);                   \
       sh4g_vec_jmp(&translation_ptr, SH4G_VEC_pc_redispatch); } while(0)

/* Indirect branches (BX / computed PC) must honour the ARM/Thumb mode of the
 * target: the `dual` trampoline switches mode from the target's bit 0, and the
 * arm/thumb ones force the mode. Routing these through sh4_block_exit (which
 * dispatches on the *current* CPSR Thumb bit) would miss the BX mode switch.
 * The target PC is already in R4 (SH4_REG_ARG0). */
#define generate_indirect_branch_no_cycle_update(type)                        \
  sh4g_vec_jmp(&translation_ptr, SH4G_VEC_ib_##type)
#define generate_indirect_branch_cycle_update(type)                           \
  do { generate_cycle_update();                                               \
       sh4g_vec_jmp(&translation_ptr, SH4G_VEC_ib_##type); } while(0)
/* Unconditional indirect branches must flush the block's accumulated cycles
 * before exiting (matching the MIPS backend); a conditional one already had its
 * cycles handled by the surrounding conditional/branch path. */
#define generate_indirect_branch_dual()                                       \
  do { if(condition == 0x0E) generate_indirect_branch_cycle_update(dual);     \
       else generate_indirect_branch_no_cycle_update(dual); } while(0)

#if CGBA_SH4_LAZY_CHAIN
#define SH4_DEFAULT_BRANCH_EXIT(new_pc)                                       \
  (cgba_dynarec_single_block                                                  \
    ? (const void *)sh4_block_exit                                            \
    : ((!ram_region && ((u32)(new_pc) >= 0x08000000u))                        \
        ? (sh4_block_is_thumb ? (const void *)sh4_lazy_link_rom_thumb         \
                              : (const void *)sh4_lazy_link_rom_arm)          \
        : (sh4_block_is_thumb ? (const void *)sh4_indirect_branch_thumb       \
                              : (const void *)sh4_indirect_branch_arm)))
#else
#define SH4_DEFAULT_BRANCH_EXIT(new_pc) ((void)(new_pc), (const void *)sh4_block_exit)
#endif

#ifndef SH4_INTERNAL_KNOWN_TARGET
#define SH4_INTERNAL_KNOWN_TARGET(tgt_pc) ((u8 *)NULL)
#endif

#define generate_branch_no_cycle_update(writeback_location, new_pc)           \
  do { u32 _gbncu_pc = (u32)(new_pc);                                         \
       if (!cgba_dynarec_single_block &&                                      \
           _gbncu_pc >= block_start_pc && _gbncu_pc < block_end_pc) {         \
         u8 *_gbncu_known = SH4_INTERNAL_KNOWN_TARGET(_gbncu_pc);             \
         (writeback_location) = sh4g_branch_exit_internal_known(              \
           &translation_ptr, _gbncu_pc, _gbncu_known);                        \
       } else {                                                               \
         (writeback_location) =                                               \
           sh4g_branch_exit(&translation_ptr, _gbncu_pc,                      \
                            SH4_DEFAULT_BRANCH_EXIT(_gbncu_pc));              \
       } } while(0)

/* ---- idle-loop elimination (dynarec mirror of the interpreter's) ---------
 * The interpreter zeroes the remaining cycle budget when it (a) arrives at the
 * per-game gba_over idle_loop_target_pc or (b) takes a branch-to-self (the
 * port's runtime detector, cpu.cc) — update_gba then fast-forwards to the next
 * scheduled event instead of spinning the loop for real. Without this the JIT
 * executes every idle iteration and loses to the interpreter on any
 * idle-heavy scene (e.g. title screens waiting on VBlank). Detected at
 * translate time on direct branches: self-branch, a branch AT the DB address
 * (arm_emit.h semantics), or a backward branch INTO the DB address (loop-head
 * style entries). `pc` is the branch instruction's own address here. */
#ifdef CGBA_GBA_OVER_AZLE_IDLE
static inline int sh4g_registered_idle_signature_ok(const u8 *map,
                                                     u32 map_region,
                                                     u32 target)
{
  u32 off;

  if (target != CGBA_SH4_AZLE_IDLE_PC)
    return 1;
  if (!map || map_region != (target >> 12))
    return 0;                              /* cross-page/unmapped: stay normal */
  off = target & 0x0FFFu;
  if (off + CGBA_SH4_AZLE_IDLE_BYTES > 0x1000u)
    return 0;
  return cgba_sh4_azle_idle_signature_match(
    readaddress16(map, off + 0u), readaddress16(map, off + 2u),
    readaddress16(map, off + 4u), readaddress16(map, off + 6u),
    readaddress16(map, off + 8u));
}
#define SH4_REGISTERED_IDLE_SIGNATURE_OK(target)                              \
  sh4g_registered_idle_signature_ok(pc_address_block, pc_region, (u32)(target))
#else
#define SH4_REGISTERED_IDLE_SIGNATURE_OK(target) ((void)(target), 1)
#endif

#define SH4_REGISTERED_IDLE_BRANCH(new_pc)                                    \
  (((u32)pc == idle_loop_target_pc || (u32)(new_pc) == idle_loop_target_pc) &&\
   (u32)(new_pc) < (u32)pc)

#define SH4_IDLE_BRANCH(new_pc)                                               \
  ((u32)(new_pc) == (u32)pc ||                                                \
   (SH4_REGISTERED_IDLE_BRANCH(new_pc) &&                                     \
    SH4_REGISTERED_IDLE_SIGNATURE_OK(idle_loop_target_pc)))

/* Only invoke cgba_idle_wait when the branch is a self-loop or the entire poll
 * body [new_pc .. pc] executed in this block (block_start_pc <= new_pc). If an
 * IRQ or savestate resumes mid-loop or directly at the branch
 * (block_start_pc > new_pc), either the loaded value or condition flags may be
 * stale from before the IRQ set the wake flag, so take a normal branch back to
 * the loop head to re-poll memory before sleeping. */
#define SH4_IDLE_BRANCH_CAN_ELIMINATE(new_pc)                                 \
  (SH4_IDLE_BRANCH(new_pc) &&                                                 \
   ((u32)(new_pc) == (u32)pc || (u32)block_start_pc <= (u32)(new_pc)))

/* Forward registered idle branch (e.g. Advance Wars 1/2 soft-reset check at
 * 0x0803880a / 0x08036e2a, called inside the main VBlank-wait loop). Emits
 * idle_mode=3 so cgba_update_gba_slow() only promotes to idle_pass=2 sleep
 * after two consecutive hits within 256 GBA cycles (zero intervening work). */
#define SH4_FWD_IDLE_BRANCH_CAN_ELIMINATE(new_pc)                             \
  ((u32)pc == idle_loop_target_pc &&                                          \
   (u32)(new_pc) > (u32)pc &&                                                 \
   (u32)block_start_pc < (u32)pc &&                                           \
   SH4_REGISTERED_IDLE_SIGNATURE_OK(idle_loop_target_pc))

#define SH4_IDLE_BRANCH_MODE(new_pc)                                          \
  (SH4_IDLE_BRANCH_CAN_ELIMINATE(new_pc)                                      \
     ? (((u32)(new_pc) == (u32)pc) ? 1 : 2)                                   \
     : (SH4_FWD_IDLE_BRANCH_CAN_ELIMINATE(new_pc) ? 3 : 0))

#define generate_branch_current_update(writeback_location, new_pc)            \
  do { sh4g_cycle_debit(&translation_ptr, (int)cycle_count);                  \
       cycle_count = 0;                                                       \
       generate_branch_no_cycle_update(writeback_location, new_pc); } while(0)

/* A CONDITIONAL branch's exit sequence is emitted INSIDE the condition guard
 * (the not-taken path skips it). So it must NOT reset the translate-time
 * cycle_count: the not-taken fall-through keeps accumulating those fetch cycles
 * and a later unconditional gate flushes them. The taken path debits the cycles
 * up to the branch (a constant) plus the target-fetch refill, then exits. (An
 * unconditional branch uses generate_branch_cycle_update, which DOES zero, since
 * it always exits.) */
SH4G_NOINLINE u8 *sh4g_generate_branch_taken(
  u8 **tp, int cycle_type, u32 tgt, u32 pc, u32 block_start_pc,
  u32 block_end_pc, u32 cycle_count, u8 *known_target,
  const void *default_exit)
{
  u32 short_lim = cycle_type ? 48u : 56u;
  int deb = (int)cycle_count + (int)ws_cyc_nseq[(tgt >> 24) & 0x0Fu][cycle_type];
  if (!cgba_dynarec_single_block && tgt > pc && tgt < block_end_pc) {
    if ((tgt - pc) <= short_lim && deb > 0 && deb <= 128)
      return sh4g_emit_bra_debit(tp, deb);
    sh4g_cycle_debit(tp, deb);
    if ((tgt - pc) <= short_lim)
      return sh4g_emit_bra_placeholder(tp);
    return sh4g_emit_patch_jump(tp);
  }
  sh4g_cycle_debit(tp, deb);
  if (!cgba_dynarec_single_block && tgt >= block_start_pc && tgt < block_end_pc)
    return sh4g_branch_exit_internal_known(tp, tgt, known_target);
  return sh4g_branch_exit(tp, tgt, default_exit);
}

#define generate_branch_taken(cycle_type, writeback_location, new_pc)         \
  do { u32 _gbt_tgt = (u32)(new_pc);                                          \
       u8 *_gbt_known = (!cgba_dynarec_single_block &&                        \
           _gbt_tgt >= block_start_pc && _gbt_tgt < block_end_pc)             \
         ? SH4_INTERNAL_KNOWN_TARGET(_gbt_tgt) : NULL;                        \
       (writeback_location) = sh4g_generate_branch_taken(                     \
         &translation_ptr, (cycle_type), _gbt_tgt, (u32)pc,                   \
         block_start_pc, block_end_pc, (u32)cycle_count,                      \
         _gbt_known, SH4_DEFAULT_BRANCH_EXIT(_gbt_tgt)); } while(0)

#define generate_branch_cycle_update(cycle_type, writeback_location, new_pc)  \
  do { generate_branch_taken(cycle_type, writeback_location, new_pc);         \
       cycle_count = 0; } while(0)

#define generate_arm_branch()                                                 \
  do {                                                                        \
    u32 _b_target = block_exits[block_exit_position].branch_target;           \
    int _b_idle = SH4_IDLE_BRANCH_MODE(_b_target);                            \
    if(_b_idle) {                                                             \
      /* the interpreter detects ARM B-to-self for any EXECUTED condition     \
       * (cpu.cc case 0xA0..0xAF runs post condition check), so both legs     \
       * idle-eliminate; cycle handling mirrors the non-idle legs below. */   \
      sh4g_cycle_debit(&translation_ptr, (int)cycle_count +                   \
        (int)ws_cyc_nseq[(_b_target >> 24) & 0x0F][1]);                       \
      if(condition == 0x0E) cycle_count = 0;                                  \
      block_exits[block_exit_position].branch_source =                        \
        sh4g_branch_exit_idle(&translation_ptr, _b_target,                    \
          SH4_DEFAULT_BRANCH_EXIT(_b_target), _b_idle);                       \
    }                                                                         \
    else if(condition == 0x0E)                                                \
      generate_branch_cycle_update(1,                                         \
        block_exits[block_exit_position].branch_source, _b_target);           \
    else                                                                      \
      generate_branch_taken(1,                                                \
        block_exits[block_exit_position].branch_source, _b_target);           \
    block_exit_position++;                                                    \
  } while(0)

/* ================================================================== */
/* Conditions                                                          */
/* ================================================================== */

#define CGBA_CC_eq 0x0
#define CGBA_CC_ne 0x1
#define CGBA_CC_cs 0x2
#define CGBA_CC_cc 0x3
#define CGBA_CC_mi 0x4
#define CGBA_CC_pl 0x5
#define CGBA_CC_vs 0x6
#define CGBA_CC_vc 0x7
#define CGBA_CC_hi 0x8
#define CGBA_CC_ls 0x9
#define CGBA_CC_ge 0xA
#define CGBA_CC_lt 0xB
#define CGBA_CC_gt 0xC
#define CGBA_CC_le 0xD

/* T = condition satisfied, then the skip jumps over the predicated body when
 * false. generate_cond_emit uses a disp8 BF — fine for the bounded Thumb-branch
 * body. The ARM same-condition run is unbounded, so generate_condition() uses
 * generate_cond_emit_far (a far literal jump) which reaches any distance and
 * cannot wrap the skip target; both close via generate_branch_patch_conditional
 * (sh4g_patch_cond_skip dispatches on the placeholder). */
#define generate_cond_emit(cc_value)                                          \
  do { sh4g_cond_to_T(&translation_ptr, (cc_value));                          \
       backpatch_address = sh4g_emit_bf_placeholder(&translation_ptr); } while(0)

#define generate_cond_emit_far(cc_value)                                      \
  do { sh4g_cond_to_T(&translation_ptr, (cc_value));                          \
       backpatch_address = sh4g_emit_cond_skip_far(&translation_ptr); } while(0)

#define generate_condition()        generate_cond_emit_far(condition)
#define generate_condition_eq()     generate_cond_emit(CGBA_CC_eq)
#define generate_condition_ne()     generate_cond_emit(CGBA_CC_ne)
#define generate_condition_cs()     generate_cond_emit(CGBA_CC_cs)
#define generate_condition_cc()     generate_cond_emit(CGBA_CC_cc)
#define generate_condition_mi()     generate_cond_emit(CGBA_CC_mi)
#define generate_condition_pl()     generate_cond_emit(CGBA_CC_pl)
#define generate_condition_vs()     generate_cond_emit(CGBA_CC_vs)
#define generate_condition_vc()     generate_cond_emit(CGBA_CC_vc)
#define generate_condition_hi()     generate_cond_emit(CGBA_CC_hi)
#define generate_condition_ls()     generate_cond_emit(CGBA_CC_ls)
#define generate_condition_ge()     generate_cond_emit(CGBA_CC_ge)
#define generate_condition_lt()     generate_cond_emit(CGBA_CC_lt)
#define generate_condition_gt()     generate_cond_emit(CGBA_CC_gt)
#define generate_condition_le()     generate_cond_emit(CGBA_CC_le)

#define arm_conditional_block_header()                                        \
  do {                                                                        \
    int _acbh_single =                                                        \
      (block_data_position + 1 >= (s32)num_insns) ||                          \
      block_data[block_data_position + 1].update_cycles ||                    \
      (((pc + 4) >> 12) == pc_region &&                                       \
       (readaddress32(pc_address_block, (pc + 4) & 0x0FFFu) >> 28) !=         \
         condition);                                                          \
    int _acbh_bounded = _acbh_single && (                                     \
      ((opcode & 0x0E000000u) == 0x0A000000u) ||                              \
      ((opcode & 0x0FC000F0u) == 0x00000090u &&                               \
       ((opcode >> 16) & 15u) != 15u) ||                                      \
      ((opcode & 0x0C000000u) == 0u &&                                        \
       ((opcode & 0x02000000u) || !(opcode & 0x10u)) &&                       \
       ((opcode >> 12) & 15u) != 15u &&                                       \
       (opcode & 0x01900000u) != 0x01000000u));                               \
    if (_acbh_bounded) {                                                      \
      sh4g_litseg_reserve(&translation_ptr, 6);                               \
      generate_cond_emit(condition);                                          \
    } else {                                                                  \
      generate_condition();                                                   \
    }                                                                         \
  } while (0)

/* ================================================================== */
/* Thumb data-processing (100% native SH-4A emission).                 */
/* ================================================================== */
#define thumb_data_proc(type, name, rn_type, _rd, _rs, _rn)                   \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count);              \
       sh4g_thumb_bbra_const_step((u32)opcode, &sh4_thumb_const_mask,         \
                                  sh4_thumb_const_val); } while(0)
#define thumb_data_proc_test(type, name, rn_type, _rs, _rn)                   \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count); } while(0)
#define thumb_data_proc_unary(type, name, rn_type, _rd, _rn)                  \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count);              \
       sh4g_thumb_bbra_const_step((u32)opcode, &sh4_thumb_const_mask,         \
                                  sh4_thumb_const_val); } while(0)

#define thumb_data_proc_hi(name)                                              \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count);              \
       sh4g_thumb_bbra_const_step((u32)opcode, &sh4_thumb_const_mask,         \
                                  sh4_thumb_const_val); } while(0)
#define thumb_data_proc_test_hi(name)                                         \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count); } while(0)
#define thumb_data_proc_mov_hi()                                              \
  do { sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc,           \
                            (u32)flag_status, (int)cycle_count);              \
       sh4g_thumb_bbra_const_step((u32)opcode, &sh4_thumb_const_mask,         \
                                  sh4_thumb_const_val); } while(0)

/* ================================================================== */
/* Thumb shifts (immediate and register-specified, 100% native).       */
/* ================================================================== */

#define SH4_THUMB_SHIFT_imm(decode_type, op_type)                             \
  do { u32 _sh_op = ((u32)opcode >> 11) & 3;                                  \
       sh4g_shift_imm(&translation_ptr, (int)_sh_op,                          \
                      (unsigned)((u32)opcode & 7),                            \
                      (unsigned)(((u32)opcode >> 3) & 7),                     \
                      (unsigned)(((u32)opcode >> 6) & 0x1F),                  \
                      (u32)flag_status & 0xFu);                               \
       sh4g_thumb_bbra_const_step((u32)opcode, &sh4_thumb_const_mask,         \
                                  sh4_thumb_const_val);                       \
  } while(0)

#define SH4_THUMB_SHIFT_reg(decode_type, op_type)                             \
  do { sh4g_thumb_shift_reg_native(&translation_ptr, (u32)opcode,             \
                                   (u32)pc, (u32)flag_status);                \
       sh4_thumb_const_kill((u32)opcode & 7u); } while(0)

#define thumb_shift(decode_type, op_type, value_type)                         \
  SH4_THUMB_SHIFT_##value_type(decode_type, op_type)

/* ================================================================== */
/* Thumb loads of PC/SP-relative addresses, SP adjust, pool const      */
/* ================================================================== */

#define thumb_load_pc(_rd)                                                    \
  do { thumb_decode_imm();                                                    \
       u32 _ct_value = (u32)(((pc & ~2) + 4) + (imm * 4));                    \
       sh4g_const(&translation_ptr, _ct_value,                                \
                  SH4_REG_T0);                                                \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, (_rd));                  \
       sh4_thumb_const_set((_rd), _ct_value); } while(0)

#define thumb_load_sp(_rd)                                                    \
  do { thumb_decode_imm();                                                    \
       u32 _sp_off = (u32)(imm * 4);                                          \
       sh4g_load_greg(&translation_ptr, REG_SP, SH4_REG_T0);                  \
       if (_sp_off) {                                                         \
         if (_sp_off <= 127u) {                                               \
           sh4_codegen _cg = sh4g_open(&translation_ptr);                     \
           sh4_emit_add_imm(&_cg, (int)_sp_off, SH4_REG_T0);                  \
           sh4g_close(&translation_ptr, &_cg);                                \
         } else {                                                             \
           sh4g_const(&translation_ptr, _sp_off, SH4_REG_T1);                 \
           sh4g_add_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0);            \
         }                                                                    \
       }                                                                      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, (_rd));                  \
       sh4_thumb_const_kill(_rd); } while(0)

#define thumb_adjust_sp_up(_off)                                              \
  do { if ((_off) <= 127u) {                                                  \
         sh4_codegen _cg = sh4g_open(&translation_ptr);                       \
         sh4_emit_add_imm(&_cg, (int)(_off), SH4_REG_T0);                     \
         sh4g_close(&translation_ptr, &_cg);                                  \
       } else {                                                               \
         sh4g_const(&translation_ptr, (_off), SH4_REG_T1);                    \
         sh4g_add_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0);              \
       } } while(0)
#define thumb_adjust_sp_down(_off)                                            \
  do { if ((_off) <= 128u) {                                                  \
         sh4_codegen _cg = sh4g_open(&translation_ptr);                       \
         sh4_emit_add_imm(&_cg, -(int)(_off), SH4_REG_T0);                    \
         sh4g_close(&translation_ptr, &_cg);                                  \
       } else {                                                               \
         sh4g_const(&translation_ptr, (_off), SH4_REG_T1);                    \
         sh4g_sub_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0);              \
       } } while(0)

#define thumb_adjust_sp(direction)                                            \
  do { thumb_decode_add_sp();                                                 \
       u32 _sp_off = (u32)(imm * 4);                                          \
       if (_sp_off) {                                                         \
         sh4g_load_greg(&translation_ptr, REG_SP, SH4_REG_T0);                \
         thumb_adjust_sp_##direction(_sp_off);                                \
         sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_SP);               \
       }                                                                      \
       sh4_thumb_const_kill(REG_SP); } while(0)

#define thumb_load_pc_pool_const(rd, value)                                   \
  do { u32 _pool_addr = ((pc & ~2u) + 4u) + ((opcode & 0xFFu) * 4u);          \
       cycle_count += ws_cyc_nseq[(_pool_addr >> 24) & 0x0F][1];             \
       sh4g_const(&translation_ptr, (u32)(value), SH4_REG_T0);                \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, (rd));                   \
       sh4_thumb_const_set((rd), (value)); } while(0)

/* ================================================================== */
/* Branches                                                            */
/* ================================================================== */

#define arm_b()    generate_arm_branch()

#define arm_bl()                                                              \
  do { sh4g_const(&translation_ptr, (u32)(pc + 4), SH4_REG_T0);               \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       if (condition == 0x0E) sh4_arm_const_set(REG_LR, (u32)(pc + 4));       \
       else sh4_arm_const_kill(REG_LR);                                       \
       generate_arm_branch(); } while(0)

#define arm_bx()                                                              \
  do { u32 rn = opcode & 0x0F;                                                \
       generate_load_reg_pc(SH4_REG_ARG0, rn, 8);                            \
       /* The interpreter attributes the post-BX sequential fetch to the      \
        * TARGET region (charged by the target block itself), so cancel this   \
        * instruction's own arm_base_cycles fetch; then add the runtime        \
        * pipeline refill, which depends on the target's mode/region: an ARM   \
        * target costs ws_cyc_nseq[target][1], an ARM->Thumb BX costs none     \
        * (it falls into thumb_loop with no refill). */                        \
       cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][1];                       \
       sh4g_charge_indirect_refill(&translation_ptr, SH4_REG_ARG0);           \
       if (condition == 0x0E) sh4_arm_const_clear_all();                      \
       generate_indirect_branch_dual(); } while(0)

#define arm_swi()                                                             \
  do { sh4g_const(&translation_ptr, (u32)(pc + 4), SH4_REG_ARG0);             \
       sh4g_vec_call(&translation_ptr, SH4G_VEC_execute_swi);                 \
       generate_branch_current_update(                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       sh4_arm_const_clear_all();                                             \
       block_exit_position++; } while(0)

/* Unconditional Thumb B: idle-eliminates self-loops, registered backward loops,
 * and forward registered idle branches (idle_mode 1/2/3). */
#define thumb_b()                                                             \
  do { u32 _b_target = block_exits[block_exit_position].branch_target;        \
       int _b_idle = SH4_IDLE_BRANCH_MODE(_b_target);                         \
       if(_b_idle) {                                                          \
         sh4g_cycle_debit(&translation_ptr, (int)cycle_count +                \
           (int)ws_cyc_nseq[(_b_target >> 24) & 0x0F][0]);                    \
         cycle_count = 0;                                                     \
         block_exits[block_exit_position].branch_source =                     \
           sh4g_branch_exit_idle(&translation_ptr, _b_target,                 \
             SH4_DEFAULT_BRANCH_EXIT(_b_target), _b_idle);                    \
       } else                                                                 \
         generate_branch_cycle_update(0,                                      \
           block_exits[block_exit_position].branch_source, _b_target);        \
       block_exit_position++; } while(0)

#define thumb_bl()                                                            \
  do { sh4g_const(&translation_ptr, (u32)((pc + 2) | 0x01), SH4_REG_T0);      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, (u32)((pc + 2) | 0x01));                   \
       generate_branch_cycle_update(0,                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       block_exit_position++; } while(0)

#define thumb_blh()                                                           \
  do { thumb_decode_branch();                                                 \
       sh4g_load_greg(&translation_ptr, REG_LR, SH4_REG_ARG0);                \
       sh4g_const(&translation_ptr, (u32)(offset * 2), SH4_REG_T1);           \
       sh4g_add_reg(&translation_ptr, SH4_REG_T1, SH4_REG_ARG0);              \
       sh4g_const(&translation_ptr, (u32)((pc + 2) | 0x01), SH4_REG_T0);      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, (u32)((pc + 2) | 0x01));                   \
       generate_indirect_branch_cycle_update(thumb); } while(0)

/* BL prefix (high word, 0xF000-0xF7FF): materialize the temporary LR =
 * (PC+4) + sign_extend(offset11) << 12 -- the intermediate value the suffix's
 * thumb_blh() adds the low offset to. When a combined BL has no mid-BL cycle
 * gate or cheat hook in non-exact builds, the prefix is elided because
 * thumb_bl() immediately overwrites LR; otherwise LR is materialized so a
 * mid-BL exit/redispatch at the suffix sees the right intermediate LR. */
#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
#define thumb_bl_prefix()                                                     \
  do { if (!cgba_dynarec_single_block &&                                      \
           (u32)(pc + 2) < block_end_pc &&                                    \
           !block_data[block_data_position + 1].update_cycles &&              \
           (u32)(pc + 2) != cheat_hook &&                                     \
           ((u32)(pc + 2) >> 12) == pc_region &&                              \
           readaddress16(pc_address_block, (pc + 2) & 0x0FFFu) >= 0xF800u) {  \
         /* Combined BL with no mid-BL gate/cheat: thumb_bl() at pc+2         \
            immediately overwrites LR without reading the prefix temp. */     \
       } else {                                                               \
         thumb_decode_branch();                                               \
         u32 _ct_lr = (u32)(pc + 4 + ((s32)((offset & 0x07FF) << 21) >> 9));  \
         sh4g_const(&translation_ptr, _ct_lr, SH4_REG_T0);                    \
         sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);               \
         sh4_thumb_const_set(REG_LR, _ct_lr);                                 \
       } } while(0)
#else
#define thumb_bl_prefix()                                                     \
  do { thumb_decode_branch();                                                 \
       u32 _ct_lr = (u32)(pc + 4 + ((s32)((offset & 0x07FF) << 21) >> 9));    \
       sh4g_const(&translation_ptr, _ct_lr, SH4_REG_T0);                      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, _ct_lr); } while(0)
#endif

#ifndef CGBA_SH4_EXACT_CYCLE_BOUNDARIES
#define thumb_bx()                                                            \
  do { thumb_decode_hireg_op();                                               \
       if (!cgba_dynarec_single_block && cgba_sh4_thumb_bx_routine != NULL) { \
         int _tbx_deb;                                                        \
         sh4g_litseg_reserve(&translation_ptr, 2);                            \
         generate_load_reg_pc(SH4_REG_ARG0, rs, 4);                           \
         cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][0];                     \
         _tbx_deb = (int)cycle_count;                                         \
         cycle_count = 0;                                                     \
         if (_tbx_deb != 0 && (_tbx_deb < -127 || _tbx_deb > 128)) {          \
           sh4g_cycle_debit(&translation_ptr, _tbx_deb);                      \
           _tbx_deb = 0;                                                      \
         }                                                                    \
         sh4_thumb_const_clear_all();                                         \
         sh4g_const(&translation_ptr,                                         \
                    (u32)(uintptr_t)cgba_sh4_thumb_bx_routine, SH4_REG_RET);  \
         { sh4_codegen _cg = sh4g_open(&translation_ptr);                     \
           sh4_emit_jmp(&_cg, SH4_REG_RET);                                   \
           if (_tbx_deb != 0)                                                 \
             sh4_emit_add_imm(&_cg, -_tbx_deb, SH4_REG_CYCLES);               \
           else                                                               \
             sh4_emit_nop(&_cg);                                              \
           sh4g_close(&translation_ptr, &_cg); }                              \
       } else {                                                               \
         generate_load_reg_pc(SH4_REG_ARG0, rs, 4);                           \
         cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][0];                     \
         sh4g_charge_thumb_bx_target_fetch(&translation_ptr, SH4_REG_ARG0);   \
         generate_cycle_update();                                             \
         sh4_thumb_const_clear_all();                                         \
         sh4g_thumb_bx_dispatch(&translation_ptr);                            \
       } } while(0)
#else
#define thumb_bx()                                                            \
  do { thumb_decode_hireg_op();                                               \
       generate_load_reg_pc(SH4_REG_ARG0, rs, 4);                             \
       cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][0];                       \
       sh4g_charge_thumb_bx_target_fetch(&translation_ptr, SH4_REG_ARG0);     \
       generate_cycle_update();                                               \
       sh4_thumb_const_clear_all();                                           \
       sh4g_thumb_bx_dispatch(&translation_ptr); } while(0)
#endif

/* Conditional Thumb branch. A registered idle loop is usually exactly this
 * shape — `ldrh/and/cmp; beq back` with idle_loop_target_pc AT the branch
 * (SMA2: 0x8000534 d0fb, Prince of Persia: 0x808ff3a d0fc) — so evaluate the
 * condition first and only idle-eliminate the TAKEN back-edge to _cb_target.
 * Unconditionally parking at pc with cgba_idle_wait=1 before testing the
 * condition would sleep until the next IRQ even when the poll already
 * succeeded (dropping a full frame and starving VBlank HBlank-DMA reloads). */
#define thumb_conditional_branch(condition)                                   \
  do { u32 _cb_target = block_exits[block_exit_position].branch_target;       \
       int _cb_idle = SH4_IDLE_BRANCH_MODE(_cb_target);                       \
       sh4g_litseg_reserve(&translation_ptr, 6);                              \
       generate_condition_##condition();                                      \
       if(_cb_idle) {                                                         \
         sh4g_cycle_debit(&translation_ptr, (int)cycle_count +                \
           (int)ws_cyc_nseq[(_cb_target >> 24) & 0x0F][0]);                   \
         block_exits[block_exit_position].branch_source =                     \
           sh4g_branch_exit_idle(&translation_ptr, _cb_target,                \
             SH4_DEFAULT_BRANCH_EXIT(_cb_target), _cb_idle);                  \
       } else {                                                               \
         generate_branch_taken(0,                                             \
           block_exits[block_exit_position].branch_source, _cb_target);       \
       }                                                                      \
       generate_branch_patch_conditional(backpatch_address, translation_ptr); \
       cycle_count += ws_cyc_nseq[((u32)(pc + 2) >> 24) & 0x0F][0];           \
       block_exit_position++; } while(0)

#define thumb_swi()                                                           \
  do { sh4g_const(&translation_ptr, (u32)(pc + 2), SH4_REG_ARG0);             \
       sh4g_vec_call(&translation_ptr, SH4G_VEC_execute_swi);            \
       cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][0];                      \
       generate_branch_current_update(                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       sh4_thumb_const_clear_all();                                           \
       block_exit_position++; } while(0)

/* ================================================================== */
/* Cheats                                                              */
/* ================================================================== */
#define thumb_process_cheats()                                                \
  do { sh4g_vec_call(&translation_ptr, SH4G_VEC_cheat_hook);         \
       sh4_thumb_const_clear_all(); } while(0)
#define arm_process_cheats()                                                  \
  do { sh4g_vec_call(&translation_ptr, SH4G_VEC_cheat_hook);                  \
       sh4_arm_const_clear_all(); } while(0)

/* ================================================================== */
/* C-helper trampoline calls: pass opcode in R4, pc in R5, JSR.       */
/* ================================================================== */

/* All three forms emit the compact fixed-shape trampoline site (~28-32 bytes,
 * see sh4g_op2_tramp_call): the shared stub reads (fn, opcode, pc[, cycles])
 * through PR, runs the helper, debits guest-memory cycles (MEM forms, both
 * paths — as the old inline glue did), and on a nonzero helper return (1 =
 * pure PC change, 2 = store alert) debits the accumulated cycle_count and
 * leaves via sh4_helper_exit. */
#define SH4_CALL_OP2(fn)                                                      \
  sh4g_op2_tramp_call(&translation_ptr, (const void *)sh4_op2_tramp,          \
                      (const void *)(fn), (u32)opcode, (u32)pc, 0, 0)

#define SH4_CALL_OP2_PC(fn)                                                   \
  sh4g_op2_tramp_call(&translation_ptr, (const void *)sh4_op2_pc_tramp,       \
                      (const void *)(fn), (u32)opcode, (u32)pc, 1,            \
                      (int)cycle_count)

#define SH4_CALL_OP2_PC_MEM(fn)                                               \
  sh4g_op2_tramp_call(&translation_ptr, (const void *)sh4_op2_pc_mem_tramp,   \
                      (const void *)(fn), (u32)opcode, (u32)pc, 1,            \
                      (int)cycle_count)

/* Memory (single + block transfers). */
#define thumb_access_memory(access_type, op_type, reg_rd, reg_rb, reg_ro,     \
                            address_type, offset, mem_type)                   \
  do { int _const_cyc = sh4g_thumb_ldst_const_native(&translation_ptr,        \
                                                     (u32)opcode,             \
                                                     sh4_thumb_const_mask,    \
                                                     sh4_thumb_const_val);    \
       if (_const_cyc) {                                                      \
         cycle_count += (u32)_const_cyc;                                      \
       } else if (!sh4g_thumb_ldst_native(&translation_ptr, (u32)opcode,      \
                                          (u32)pc, (int)cycle_count)) {       \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_thumb_ldst);                            \
       }                                                                      \
       if (sh4g_thumb_ldst_is_load((u32)opcode))                              \
         sh4_thumb_const_kill(sh4g_thumb_ldst_reg_index((u32)opcode)); } while(0)
#define thumb_block_memory(access_type, pre_op, post_op, base_reg)            \
  do { u32 _tb_op = (u32)opcode;                                              \
       u32 _tb_hi = (_tb_op >> 8) & 0xFFu;                                    \
       if(!sh4g_thumb_block_native(&translation_ptr, _tb_op, (u32)pc,         \
                                   (int)cycle_count))                         \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_thumb_block);                           \
       if (_tb_hi == 0xB4u || _tb_hi == 0xB5u) {                              \
         sh4_thumb_const_kill(13);          /* PUSH only writes SP */         \
       } else if (_tb_hi == 0xBCu) {                                          \
         sh4_thumb_const_mask &= ~((_tb_op & 0xFFu) | (1u << 13));            \
       } else if (_tb_hi >= 0xC0u && _tb_hi <= 0xC7u) {                       \
         sh4_thumb_const_kill((_tb_op >> 8) & 7u); /* STMIA only writes Rb */ \
       } else if (_tb_hi >= 0xC8u && _tb_hi <= 0xCFu) {                       \
         sh4_thumb_const_mask &= ~((_tb_op & 0xFFu) |                         \
                                   (1u << ((_tb_op >> 8) & 7u)));             \
       } else {                                                               \
         sh4_thumb_const_clear_all();                                         \
       } } while(0)
#define arm_access_memory(access_type, direction, adjust_op, mem_type,        \
                          offset_type)                                        \
  sh4g_arm_access_memory_emit(&translation_ptr, (u32)opcode, (u32)pc,         \
                              (u32)condition, &cycle_count, ram_region,       \
                              pc_address_block,                               \
                              &sh4_arm_const_mask, sh4_arm_const_val)
#define arm_block_memory(access_type, offset_type, writeback_type, s_bit)     \
  do { u32 _ab_op = (u32)opcode;                                              \
       if(!sh4g_arm_block_native(&translation_ptr, _ab_op, (u32)pc,           \
                                 (int)cycle_count))                           \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_arm_block);                             \
       if ((_ab_op & 0x00408000u) != 0u) {                                    \
         sh4_arm_const_clear_all();                                           \
       } else {                                                               \
         if (_ab_op & 0x00100000u)                                            \
           sh4_arm_const_mask &= ~(_ab_op & 0x7FFFu);                         \
         if (_ab_op & 0x00200000u)                                            \
           sh4_arm_const_kill((_ab_op >> 16) & 0xFu);                         \
       } } while(0)

/* ARM data-processing: native SH-4 for imm, unshifted reg, imm-shifted reg
 * (except RRX), and reg-shifted reg (S=0 and S=1); Rd==15/PC+12/RRX -> C. */
#define arm_data_proc(name, type, flags_op)                                   \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc,          \
                              (u32)flag_status))                               \
         SH4_CALL_OP2_PC(cgba_sh4_arm_dp);                                    \
       sh4g_arm_const_update_dp((u32)opcode, (u32)condition, (u32)pc,         \
                                &sh4_arm_const_mask, sh4_arm_const_val); } while(0)
#define arm_data_proc_test(name, type)                                        \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc,          \
                              (u32)flag_status))                               \
         SH4_CALL_OP2(cgba_sh4_arm_dp); } while(0)
#define arm_data_proc_unary(name, type, flags_op)                             \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc,          \
                              (u32)flag_status))                               \
         SH4_CALL_OP2_PC(cgba_sh4_arm_dp);                                    \
       sh4g_arm_const_update_dp((u32)opcode, (u32)condition, (u32)pc,         \
                                &sh4_arm_const_mask, sh4_arm_const_val); } while(0)
#define arm_multiply(add_op, flags)                                          \
  do { if(!sh4g_arm_multiply_native(&translation_ptr, (u32)opcode,            \
                                    (u32)flag_status))                         \
         SH4_CALL_OP2(cgba_sh4_arm_multiply);                                 \
       sh4_arm_const_kill(((u32)opcode >> 16) & 0xFu); } while(0)
#define arm_multiply_long(name, add_op, flags)                                \
  do { if(!sh4g_arm_multiply_long_native(&translation_ptr, (u32)opcode,       \
                                         (u32)flag_status))                    \
         SH4_CALL_OP2(cgba_sh4_arm_multiply_long);                            \
       sh4_arm_const_kill(((u32)opcode >> 16) & 0xFu);                        \
       sh4_arm_const_kill(((u32)opcode >> 12) & 0xFu); } while(0)
#define arm_psr(op_type, transfer_type, psr_reg)                              \
  do { u32 _ap_op = (u32)opcode;                                              \
       if (!sh4g_arm_psr_native(&translation_ptr, _ap_op, (u32)pc,            \
                                (int)cycle_count))                            \
         SH4_CALL_OP2_PC(cgba_sh4_arm_psr);                                   \
       if (!(_ap_op & (1u << 21)))                                            \
         sh4_arm_const_kill((_ap_op >> 12) & 0xFu);                           \
       else if (!(_ap_op & (1u << 22)) && (_ap_op & (1u << 16)))              \
         sh4_arm_const_clear_all(); } while(0)
#define arm_swap(type)                                                        \
  do { SH4_CALL_OP2_PC_MEM(cgba_sh4_arm_swap);                                \
       sh4_arm_const_kill(((u32)opcode >> 12) & 0xFu); } while(0)

/* Native HLE SWI Div call via SH4G_VEC_hle_div (sh4_stub.S syncs pinned r0/r1
 * and R8 CPSR around cgba_sh4_hle_div, which writes r0/r1/r3). */
#define arm_hle_div(cpu_mode)                                                 \
  do { sh4g_const(&translation_ptr, 0u, SH4_REG_ARG0);                        \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG1);                   \
       sh4g_vec_call(&translation_ptr, SH4G_VEC_hle_div);                     \
       sh4g_cycle_debit_from_global(&translation_ptr, &cgba_sh4_extra_cycles);\
       sh4_##cpu_mode##_const_clear_all(); } while(0)
#define arm_hle_div_arm(cpu_mode)                                             \
  do { sh4g_const(&translation_ptr, 1u, SH4_REG_ARG0);                        \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG1);                   \
       sh4g_vec_call(&translation_ptr, SH4G_VEC_hle_div);                     \
       sh4g_cycle_debit_from_global(&translation_ptr, &cgba_sh4_extra_cycles);\
       sh4_##cpu_mode##_const_clear_all(); } while(0)

#endif /* SH4_EMIT_H */
