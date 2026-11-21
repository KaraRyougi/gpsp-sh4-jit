#ifndef CGBA_SH4_FASTMEM_H
#define CGBA_SH4_FASTMEM_H

/*
 * Out-of-line native memory fast paths — the translation-cache DENSITY fix.
 *
 * Metroid's in-world working set overflowed the former 896KB ROM cache
 * when every native load/store site inlines its ~90-130 byte fast path
 * (guards + page probe + transfer + SMC tags + wait-state charge): the JIT
 * wholesale-flushes ~3x per FRAME and spends the frame retranslating (~3fps,
 * measured: FL R15 / TX T7025 / IC 12.5MB per 5-frame window).
 *
 * Fix: emit each fast-path SHAPE exactly once at init into a small resident
 * buffer, and call it from a fixed ~36-byte site — the same literal-tuple
 * mechanism as the compact C-helper trampolines, with one extra twist: the
 * tuple is laid out so the routine's guard-failure path can jump STRAIGHT
 * into the existing sh4_op2_pc_mem_tramp, which then reads the helper fn /
 * opcode / pc / cycles from the very same tuple. One site shape serves both
 * the native fast path and the C fallback.
 *
 *   site:  [addr calc, site-specific]           ; R1 = address, R6 = wb value
 *          [pad NOP so the literals align]
 *          MOV.L  L, r0
 *          JSR    @r0                            ; PR -> the BRA below
 *          NOP
 *          BRA    2f
 *          NOP
 *   L:     .long  ROUTINE                        ; PR+4
 *          .long  helper_fn                      ; PR+8   (tramp: fn)
 *          .long  opcode                         ; PR+12  (tramp: opcode)
 *          .long  pc                             ; PR+16  (tramp: pc)
 *          .long  cycle_count                    ; PR+20  (tramp: cycles)
 *          .long  params                         ; PR+24  (routine only)
 *   2:
 *
 * Routine contract:
 *   in:  R1 = guest transfer address, R6 = writeback value (wb variants),
 *        PR = site return (also the tuple cursor), R8 = cached CPSR (do not
 *        touch), R13/R14 cycle counter / reg base as everywhere.
 *   out: fast path performs transfer + reg[rd] + writeback + cycle charge,
 *        returns with RTS. Guard failure jumps to sh4_op2_pc_mem_tramp with
 *        PR intact — the C helper re-executes the instruction from original
 *        state (writeback is NOT committed before any guard).
 *   params: bits 0-7 = rd byte-offset in reg[] (rd*4);
 *           bits 8-15 = writeback rn byte-offset (wb variants only).
 *
 * The generator reuses the exact inline emitter recipes (guards, byte-order
 * swaps, SMC tag scan, charge) — this file reorganizes WHERE the code lives,
 * not what it does. Routines live in a normal .bss buffer (NOT the high
 * arena) and survive translation-cache flushes.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"

/* Block lists with at least this many registers use the shared runtime-rlist
 * routine (40-byte site); smaller lists keep the faster unrolled inline path.
 * Tunable for capacity-vs-speed A/B on hardware. */
#ifndef CGBA_SH4_FASTMEM_BLOCK_MIN
#define CGBA_SH4_FASTMEM_BLOCK_MIN 1
#endif

/* Routine variants: kind x direction, plus ARM writeback twins. */
enum {
  CGBA_FM_LOAD_W = 0, CGBA_FM_LOAD_B, CGBA_FM_LOAD_UH, CGBA_FM_LOAD_SH,
  CGBA_FM_LOAD_SB, CGBA_FM_STORE_W, CGBA_FM_STORE_UH, CGBA_FM_STORE_B,
  CGBA_FM_BASE_COUNT,
  CGBA_FM_WB = CGBA_FM_BASE_COUNT,   /* +CGBA_FM_WB = writeback variant */
  CGBA_FM_COUNT = CGBA_FM_BASE_COUNT * 2,
  /* Block transfers (LDM/STM/PUSH/POP, runtime register-list loop). The
   * +2 variants commit the precomputed writeback base from R6. */
  CGBA_FMB_LDM = CGBA_FM_COUNT,
  CGBA_FMB_STM,
  CGBA_FMB_LDM_WB,
  CGBA_FMB_STM_WB,
  CGBA_FM_TOTAL
};

extern u8 *cgba_sh4_fastmem_routine[CGBA_FM_TOTAL];
extern u8 *cgba_sh4_fastmem_io16_store;
extern u8 *cgba_sh4_fastmem_block_cold[4];
extern u8 *cgba_sh4_pop1_bx_routine;
extern u8 *cgba_sh4_thumb_bx_routine;
extern u8 *cgba_sh4_psr_rebank_routine;
extern u32 cgba_iwram_code_end;
extern u32 cgba_ewram_code_end;
void cgba_sh4_fastmem_init(void);
void sh4_pop1_bx_slow(void);

extern u8 *memory_map_read[];
extern u32 gamepak_size;
extern const u8 * const *cgba_gamepak_fragment_blocks;
extern u16 io_registers[512];          /* eswap16'd (LE-layout) halfwords */
extern u16 palette_ram[512];
extern u16 palette_ram_converted[512];
#ifdef CGBA_PALETTE_DIRTY_ACTIVE
extern volatile u32 palette_ram_dirty;
#endif
#ifdef CGBA_VRAM_DIRTY_ACTIVE
extern volatile u32 cgba_vram_dirty;
#ifdef CGBA_VRAM_SB_DIRTY_ACTIVE
extern volatile u32 cgba_vram_sb_dirty;
#endif
extern volatile u32 cgba_video_io_dirty;
#endif
#ifdef CGBA_VIDEO_DEFERRED_ACTIVE
extern volatile u32 cgba_pending_sl_count;
#endif
extern u8 ewram[];
extern u8 iwram[];
extern u8 vram[1024 * 96];
void sh4_op2_pc_mem_tramp(void);
int cgba_sh4_thumb_ldst(u32 opcode, u32 pc);
int cgba_sh4_thumb_ldst_run(u32 count, u32 pc);
int cgba_sh4_arm_ldst(u32 opcode, u32 pc);
int cgba_sh4_thumb_block(u32 opcode, u32 pc);
int cgba_sh4_arm_block(u32 opcode, u32 pc);
#ifndef CGBA_SH4_COMPACT_FASTMEM_TUPLE
#define CGBA_SH4_COMPACT_FASTMEM_TUPLE 0
#endif
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
void sh4_fastmem_pc_mem_tramp(void);
void sh4_fastmem_compact8_tramp(void);
void sh4_fastmem_compact8_load_tramp(void);
#ifndef CGBA_SH4_FASTMEM_OOL_DESC
#define CGBA_SH4_FASTMEM_OOL_DESC 1
#endif
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE && CGBA_SH4_FASTMEM_OOL_DESC
enum {
  CGBA_FM_OOL_THUMB_LDST  = 0,
  CGBA_FM_OOL_ARM_LDST    = 1,
  CGBA_FM_OOL_THUMB_BLOCK = 2,
  CGBA_FM_OOL_ARM_BLOCK   = 3
};

typedef struct cgba_sh4_fm_desc {
  u32 ret_pc;
  u32 pc_flags;      /* bits 31:30 = helper_tag, bits 0..29 = guest PC */
  u32 opcode;
  u32 cycle_count;
} cgba_sh4_fm_desc;

extern const cgba_sh4_fm_desc *cgba_sh4_fm_ool_cache[512];
int  cgba_sh4_fm_ool_can_record(const u8 *tp);
void cgba_sh4_fm_ool_push(u32 ret_pc, u32 pc_flags, u32 opcode, u32 cycle_count);
void cgba_sh4_fm_ool_reset_rom(void);
void cgba_sh4_fm_ool_reset_ram(void);
void cgba_sh4_fm_ool_reset_all(void);
void cgba_sh4_fm_ool_rewind(const u8 *ptr, int ram_region);
u8  *cgba_sh4_fm_ool_rom_limit(u32 threshold);
const cgba_sh4_fm_desc *cgba_sh4_fastmem_ool_lookup(u32 ret_pc);
#define CGBA_SH4_FASTMEM_SLOW_TRAMP sh4_fastmem_compact8_tramp
#else
#define CGBA_SH4_FASTMEM_SLOW_TRAMP sh4_fastmem_pc_mem_tramp
#endif
#else
#ifndef CGBA_SH4_FASTMEM_OOL_DESC
#define CGBA_SH4_FASTMEM_OOL_DESC 0
#endif
#define CGBA_SH4_FASTMEM_SLOW_TRAMP sh4_op2_pc_mem_tramp
#endif

/* ---- per-site call ------------------------------------------------------ */
/* Same geometry as sh4g_op2_tramp_call, with the 6-literal fastmem tuple. */
#ifdef CGBA_GPSP_HEADLESS_TEST
extern unsigned long cgba_em_fm_n, cgba_em_fm_bytes;
#endif

#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
/* Compact8 non-WB single fastmem site:
 *   Loads:  routine returns guest value in R2 (SH4_REG_T1); caller stores R2
 *           to reg[rd] (or R11/R12 for r0/r1) right after the JSR/descriptor,
 *           freeing the JSR delay slot for caller-supplied `load_delay_op`
 *           (address calculation) and enabling post-load register forwarding.
 *   Stores: caller loads reg[rd] (or R11/R12 for r0/r1) directly into R2 in
 *           the JSR delay slot; routine writes R2 directly without reloading. */
static inline void sh4g_fastmem_site_compact(u8 **tp, int fm,
                                             const void *helper_fn, u32 opcode,
                                             u32 pc, int cycle_count,
                                             unsigned rd,
                                             uint16_t load_delay_op)
{
  const u8 *routine = cgba_sh4_fastmem_routine[fm];
  int is_load = (fm < CGBA_FM_STORE_W);
  int is_thumb = (helper_fn == (const void *)cgba_sh4_thumb_ldst);
#if CGBA_SH4_FASTMEM_OOL_DESC
  if ((!is_thumb && helper_fn != (const void *)cgba_sh4_arm_ldst) ||
      (pc & 0xC0000000u) ||
      !cgba_sh4_fm_ool_can_record(*tp)) {
    sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                        helper_fn, opcode, pc, 1, cycle_count);
    return;
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  u8 *stat0 = *tp;
  cgba_em_fm_n++;
#endif
  /* Load resident routine via segmented literal pool (shared across sites). */
  sh4g_const(tp, (u32)(uintptr_t)routine, SH4_REG_RET);
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8))); /* JSR @R0        */
  if (is_load) {
    sh4g_u16(tp, load_delay_op);                         /* addr op / NOP  */
  } else {
#if CGBA_SH4_PIN_GUEST_R0
    if (rd == 0u)
      sh4g_u16(tp, 0x62B3u);                             /* MOV R11, R2    */
    else
#endif
#if CGBA_SH4_PIN_GUEST_R1
    if (rd == 1u)
      sh4g_u16(tp, 0x62C3u);                             /* MOV R12, R2    */
    else
#endif
      sh4g_u16(tp, (uint16_t)(0x52E0u | (rd & 0xFu)));   /* MOV.L @(rd,R14),R2 */
  }
  /* PR points right here: register OOL slow-path descriptor keyed by PR. */
  cgba_sh4_fm_ool_push((u32)(uintptr_t)(*tp),
                       (pc & 0x3FFFFFFFu) |
                         ((u32)(is_thumb ? CGBA_FM_OOL_THUMB_LDST
                                         : CGBA_FM_OOL_ARM_LDST) << 30),
                       opcode, (u32)cycle_count);
  if (is_load) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd);            /* 2B: R2 -> reg[rd] */
    sh4g_close(tp, &cg);
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
#endif
#else
  if ((!is_thumb && helper_fn != (const void *)cgba_sh4_arm_ldst) ||
      (is_thumb && ((unsigned)cycle_count > 0xFFFFu || opcode > 0xFFFFu)) ||
      (pc & 0x80000000u)) {
    sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                        helper_fn, opcode, pc, 1, cycle_count);
    return;
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  u8 *stat0 = *tp;
  cgba_em_fm_n++;
#endif
  /* Load resident routine via segmented literal pool (shared across sites). */
  sh4g_const(tp, (u32)(uintptr_t)routine, SH4_REG_RET);
  if (((uintptr_t)*tp + 4) & 3)                 /* PR (+4) must be 4-aligned */
    sh4g_u16(tp, 0x0009);
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8))); /* JSR @R0        */
  if (is_load) {
    sh4g_u16(tp, load_delay_op);                         /* addr op / NOP  */
  } else {
#if CGBA_SH4_PIN_GUEST_R0
    if (rd == 0u)
      sh4g_u16(tp, 0x62B3u);                             /* MOV R11, R2    */
    else
#endif
#if CGBA_SH4_PIN_GUEST_R1
    if (rd == 1u)
      sh4g_u16(tp, 0x62C3u);                             /* MOV R12, R2    */
    else
#endif
      sh4g_u16(tp, (uint16_t)(0x52E0u | (rd & 0xFu)));   /* MOV.L @(rd,R14),R2 */
  }
  {
    sh4_codegen cg = sh4g_open(tp);
    if (is_thumb) {
      sh4_emit_u32_be(&cg, pc);
      sh4_emit_u32_be(&cg, ((opcode & 0xFFFFu) << 16) |
                           ((u32)cycle_count & 0xFFFFu));
    } else {
      sh4_emit_u32_be(&cg, pc | 0x80000000u);
      sh4_emit_u32_be(&cg, opcode);
      sh4_emit_u16(&cg, 0xA002);                         /* BRA +2 (PR+16) */
      sh4_emit_u16(&cg, 0x0009);                         /* delay NOP      */
      sh4_emit_u32_be(&cg, (u32)cycle_count);
    }
    if (is_load)
      sh4_emit_store_greg(&cg, SH4_REG_T1, rd);          /* 2B: R2 -> reg[rd] */
    sh4g_close(tp, &cg);
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
  cgba_em_tuple_pc_b += 4;
  cgba_em_tuple_opcode_b += is_thumb ? 2 : 4;
  cgba_em_tuple_cycle_b += is_thumb ? 2 : 4;
#endif
#endif
}
#endif

static inline void sh4g_fastmem_site_raw(u8 **tp, int fm,
                                         const void *helper_fn, u32 opcode,
                                         u32 pc, int cycle_count, u32 params)
{
  const u8 *routine = cgba_sh4_fastmem_routine[fm];
#if CGBA_SH4_PIN_GUEST_R0 || CGBA_SH4_PIN_GUEST_R1
#if !defined(CGBA_SH4_SELECTIVE_R0_SYNC) || CGBA_SH4_SELECTIVE_R0_SYNC
  int block = fm >= CGBA_FM_COUNT;
  int load = block ? !(fm & 1) : (fm % CGBA_FM_BASE_COUNT < CGBA_FM_STORE_W);
  int wb = block ? fm >= CGBA_FMB_LDM_WB : fm >= CGBA_FM_WB;
#endif
#endif
#if CGBA_SH4_PIN_GUEST_R0
  int publish_r0 = 1, reload_r0 = 1;
#if !defined(CGBA_SH4_SELECTIVE_R0_SYNC) || CGBA_SH4_SELECTIVE_R0_SYNC
  int transfers_r0 = block
    ? ((params & 0x00200000u) ? ((params & 0xFFu) == 0u) : ((params & 1u) != 0u))
    : ((params & 255u) == 0u);
  int writes_r0 = wb && (block ? (params >> 24) == 0 : (params & 0xFF00) == 0);
  /* Address/writeback operands were already read through the pinned-register
   * accessors. Only store data needs publishing; only a load or base writeback
   * needs reloading. Slow trampolines synchronize r0/r1 themselves, including
   * on exits that never return to this continuation. */
  publish_r0 = !load && transfers_r0;
  reload_r0 = (load && transfers_r0) || writes_r0;
#endif
#endif
#if CGBA_SH4_PIN_GUEST_R1
  int publish_r1 = 1, reload_r1 = 1;
#if !defined(CGBA_SH4_SELECTIVE_R0_SYNC) || CGBA_SH4_SELECTIVE_R0_SYNC
  int transfers_r1 = block
    ? ((params & 0x00200000u)
         ? (((params & 0xFFu) >> 2) <= 1u &&
            (((params & 0xFFu) >> 2) +
             (((params >> 16) & 0x1Fu) - ((params >> 22) & 1u))) > 1u)
         : ((params & 2u) != 0u))
    : ((params & 255u) == 4u);
  int writes_r1 = wb && (block ? (params >> 24) == 4 : (params & 0xFF00) == 0x0400);
  publish_r1 = !load && transfers_r1;
  reload_r1 = (load && transfers_r1) || writes_r1;
#endif
#endif
  u8 *site;
  u8 *lit;
  unsigned nlit = 6;
  int block_params = 1;
  long bra_disp;
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
  if (fm < CGBA_FM_BASE_COUNT) {
    sh4g_fastmem_site_compact(tp, fm, helper_fn, opcode, pc, cycle_count,
                              (params & 0xFFu) >> 2, 0x0009u);
    return;
  }
#if CGBA_SH4_FASTMEM_OOL_DESC
  {
    int helper_tag = -1;
    if (helper_fn == (const void *)cgba_sh4_thumb_ldst)
      helper_tag = CGBA_FM_OOL_THUMB_LDST;
    else if (helper_fn == (const void *)cgba_sh4_arm_ldst)
      helper_tag = CGBA_FM_OOL_ARM_LDST;
    else if (helper_fn == (const void *)cgba_sh4_thumb_block)
      helper_tag = CGBA_FM_OOL_THUMB_BLOCK;
    else if (helper_fn == (const void *)cgba_sh4_arm_block)
      helper_tag = CGBA_FM_OOL_ARM_BLOCK;
    if (helper_tag < 0 || (pc & 0xC0000000u) ||
        !cgba_sh4_fm_ool_can_record(*tp)) {
      sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                          helper_fn, opcode, pc, 1, cycle_count);
      return;
    }
#ifdef CGBA_GPSP_HEADLESS_TEST
    u8 *stat0 = *tp;
    cgba_em_fm_n++;
#endif
    /* Pass params in R5 (shared pool literal across identical PUSH/POP masks),
     * load routine into R0, pack optional guest-r0/r1 flush into JSR delay
     * slot, and store slow-path (pc, opcode, cycles, helper_tag) out of line. */
    sh4g_const(tp, params, SH4_REG_ARG1);
    sh4g_const(tp, (u32)(uintptr_t)routine, SH4_REG_RET);
    {
      uint16_t jsr_delay = 0x0009u;
#if CGBA_SH4_PIN_GUEST_R0
      if (publish_r0)
        jsr_delay = SH4_NMD(0x1000, SH4_REG_BASE, SH4_REG_GUEST_R0, 0);
#endif
#if CGBA_SH4_PIN_GUEST_R1
      if (publish_r1) {
        if (jsr_delay != 0x0009u)
          sh4g_u16(tp, jsr_delay);
        jsr_delay = SH4_NMD(0x1000, SH4_REG_BASE, SH4_REG_GUEST_R1, 1);
      }
#endif
      sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8))); /* JSR @R0 */
      sh4g_u16(tp, jsr_delay);
    }
    cgba_sh4_fm_ool_push((u32)(uintptr_t)(*tp),
                         (pc & 0x3FFFFFFFu) | ((u32)helper_tag << 30),
                         opcode, (u32)cycle_count);
#if CGBA_SH4_PIN_GUEST_R0
    if (reload_r0) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_GUEST_R0, 0);
      sh4g_close(tp, &cg);
    }
#endif
#if CGBA_SH4_PIN_GUEST_R1
    if (reload_r1) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_GUEST_R1, 1);
      sh4g_close(tp, &cg);
    }
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
    cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
#endif
    return;
  }
#endif
  block_params = (params & 0xFFFF0000u) != 0;
  nlit = block_params ? 6u : 5u;
  if ((unsigned)cycle_count > 0xFFFFu) {
    /* Correctness escape for a pathological very long accounting run. */
    sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                        helper_fn, opcode, pc, 1, cycle_count);
    return;
  }
#endif
  (void)block_params;
#ifdef CGBA_GPSP_HEADLESS_TEST
  u8 *stat0 = *tp;
  cgba_em_fm_n++;
#endif

#if CGBA_SH4_PIN_GUEST_R0
  if (publish_r0) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_GUEST_R0, SH4_REG_BASE, 0);
    sh4g_close(tp, &cg); }
#endif
#if CGBA_SH4_PIN_GUEST_R1
  if (publish_r1) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_GUEST_R1, SH4_REG_BASE, 1);
    sh4g_close(tp, &cg); }
#endif
  if (((uintptr_t)*tp + 10) & 3)               /* literals must be 4-aligned */
    sh4g_u16(tp, 0x0009);
  site = *tp;
  lit = site + 10;
  sh4g_u16(tp, (uint16_t)(0xD000 | (SH4_REG_RET << 8) | 2));   /* MOV.L L,R0 */
  sh4g_u16(tp, (uint16_t)(0x400B | (SH4_REG_RET << 8)));       /* JSR @R0    */
  sh4g_u16(tp, 0x0009);                                        /* delay      */
  bra_disp = ((long)(lit + nlit * 4) - ((long)(*tp) + 4)) / 2;
  sh4g_u16(tp, (uint16_t)(0xA000 | (bra_disp & 0x0FFF)));      /* BRA 2f     */
  sh4g_u16(tp, 0x0009);                                        /* delay NOP  */
  {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)routine);
    sh4_emit_u32_be(&cg, (u32)(uintptr_t)helper_fn);
    sh4_emit_u32_be(&cg, opcode);
    sh4_emit_u32_be(&cg, pc);
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
    if (block_params) {
      sh4_emit_u32_be(&cg, (u32)cycle_count & 0xFFFFu);
      sh4_emit_u32_be(&cg, params);
    } else {
      sh4_emit_u32_be(&cg, (params << 16) | ((u32)cycle_count & 0xFFFFu));
    }
#else
    sh4_emit_u32_be(&cg, (u32)cycle_count);
    sh4_emit_u32_be(&cg, params);
#endif
    sh4g_close(tp, &cg);
  }
#if CGBA_SH4_PIN_GUEST_R0
  if (reload_r0) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_GUEST_R0, 0);
    sh4g_close(tp, &cg); }                                     /* 2: R11 */
#endif
#if CGBA_SH4_PIN_GUEST_R1
  if (reload_r1) { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_BASE, SH4_REG_GUEST_R1, 1);
    sh4g_close(tp, &cg); }                                     /* 2: R12 */
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_em_fm_bytes += (unsigned long)(*tp - stat0);
  cgba_em_tuple_routine_b += 4;
  cgba_em_tuple_fn_b += 4;
  cgba_em_tuple_opcode_b += 4;
  cgba_em_tuple_pc_b += 4;
#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
  cgba_em_tuple_cycle_b += 2;
  cgba_em_tuple_params_b += block_params ? 4 : 2;
#else
  cgba_em_tuple_cycle_b += 4;
  cgba_em_tuple_params_b += 4;
#endif
#endif
}

static inline void sh4g_fastmem_site(u8 **tp, int fm,
                                     const void *helper_fn, u32 opcode, u32 pc,
                                     int cycle_count, unsigned rd, int wb_rn)
{
  u32 params = (rd * 4u) | (wb_rn >= 0 ? ((u32)wb_rn * 4u) << 8 : 0);
  sh4g_fastmem_site_raw(tp, fm, helper_fn, opcode, pc, cycle_count, params);
}

/* Block-site params:
 *   Generic (bit 21 == 0): rlist (16-bit ARM layout) | count << 16 |
 *     writeback rn byte-offset << 24 (wb variants only).
 *   Contiguous fast (bit 21 == 1): 1..8 contiguous regs (optionally + trailing
 *     LR bit 14, encoded in bit 22). bits 0..7 = first_reg*4, bits 8..15 =
 *     (8 - n_contig)*10 (exact BRAF byte offset into the 8-step unrolled ILRAM
 *     ladder), bits 16..20 = count, bits 24..31 = wb_rn*4. */
static inline u32 sh4g_fastmem_block_params(u32 rlist, u32 count, int wb_rn)
{
  u32 m = rlist & 0xFFFFu;
  u32 wb_field = (wb_rn >= 0 ? ((u32)wb_rn * 4u) << 24 : 0u);
  if (m != 0u && (m & 0x8000u) == 0u) {
    u32 has_lr = ((m & 0x4000u) != 0u && (m & 0x3FFFu) != 0u) ? 1u : 0u;
    u32 base = has_lr ? (m & ~0x4000u) : m;
    u32 lsb = base & (0u - base);
    u32 sum = base + lsb;
    u32 n_contig = count - has_lr;
    if (base != 0u && (sum & (sum - 1u)) == 0u &&
        n_contig >= 1u && n_contig <= 8u) {
      u32 first = (u32)__builtin_ctz(base);
      u32 jump_off = (8u - n_contig) * 10u;
      return (first * 4u) | (jump_off << 8) | (count << 16) |
             0x00200000u | (has_lr << 22) | wb_field;
    }
  }
  return m | (count << 16) | wb_field;
}

#endif /* CGBA_SH4_FASTMEM_H */
