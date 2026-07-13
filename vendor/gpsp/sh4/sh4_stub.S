/* gameplaySP — SH-4A (SH7305 / fx-CG100) dynarec entry/exit trampoline.
 *
 * Mirrors mips/mips_stub.S for the SH4 host. Guest r0 is pinned in R11 and
 * CPSR in R8 while translated code runs; both are synchronized with reg[] at
 * C/helper boundaries. Other guest registers remain in the reg[] state array.
 *
 * SH4 ABI: integer args in R4..R7, return in R0, callee-saved R8..R15 + PR.
 *
 * Persistent dynarec registers (preserved across translated blocks and across
 * C calls, since R8..R14 are callee-saved):
 *   R14 = reg[] base pointer
 *   R13 = cycle counter (counts down; block exits when it crosses 0)
 *   R11 = guest ARM r0
 *
 * NOTE: assembled and checked with sh-elf-as. The new ILRAM/XYRAM placement
 * still requires final physical-hardware boot and timing validation.
 */

#include "../gpsp_config.h"

#ifndef CGBA_SH4_PIN_GUEST_R0
#define CGBA_SH4_PIN_GUEST_R0 1
#endif

/* The Casio sh-elf toolchain (fxSDK) uses a leading-underscore C ABI: a C
 * symbol `foo` is `_foo` at link time (cf. bios_data.S). Export both spellings
 * so generated code and the C core (which reference `_foo`) resolve. */
#define defsymbl(symbol) \
  .global symbol ;       \
  .global _##symbol ;    \
  .type symbol, @function ; \
  symbol: ;              \
  _##symbol:

/* reg[] byte offsets (u32 reg[]) */
.equ REG_R0,           (0 * 4)
.equ REG_PC,           (15 * 4)
.equ REG_CPSR,         (16 * 4)
.equ CPU_MODE,         (17 * 4)
.equ CPU_HALT_STATE,   (18 * 4)
.equ REG_BUS_VALUE,    (19 * 4)
.equ REG_SLEEP_CYCLES, (24 * 4)
.equ REG_SAVE,         (26 * 4)
.equ REG_SAVE2,        (27 * 4)
.equ REG_SAVE3,        (28 * 4)

.equ CPU_ALERT_HALT,   (1 << 0)
.equ CPU_ALERT_SMC,    (1 << 1)
.equ CPU_ALERT_IRQ,    (1 << 2)

/* SPSR / reg_mode layout, identical to the MIPS stub's derivation.
 * reg[] is u32[64] (0x100 bytes); spsr[] and reg_mode[] follow in the
 * generated/extern layout the C side provides via separate symbols, so the
 * supervisor helpers below address them through their own externs. */

.section .ilram.dispatch, "ax"
.align 2

/* Call a (possibly far) C function. Address is materialized from a
 * routine-local literal; the literal is skipped at runtime. */
.macro CALL func
  mov.l   8f, r0
  jsr     @r0
  nop
  bra     9f
  nop
  .align 2
8:
  .long   _\func          /* C symbols carry a leading underscore (fxSDK ABI) */
9:
.endm

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
.macro INC_COUNTER sym
  mov.l   8f, r0
  mov.l   @r0, r1
  add     #1, r1
  mov.l   r1, @r0
  bra     9f
  nop
  .align 2
8:
  .long   _\sym
9:
.endm
#else
.macro INC_COUNTER sym
.endm
#endif

/* After a block_lookup_address_* call (host block in R0): a NULL result means the
 * translation cache overflowed mid-translate and was flushed (translate_block
 * returns false -> the resolver returns NULL, cpu_threaded.c:3167-3172 / 2604 /
 * 2642). The block must be re-translated into the now-empty cache. The resolver
 * has already committed reg[REG_PC] to the target, so "pedal out to the beginning"
 * by branching back to lookup_pc instead of jumping to NULL (which lands on PC=0/
 * garbage). One flush+retry always suffices: a single block fits the fresh cache.
 * Without this, any cache overflow is a hard crash -- latent in upstream gpSP, hit
 * here once blocks grow large enough (e.g. the native Thumb byte-LDR fast path). */
/* Persistent guest-state sync. R8 is authoritative CPSR and R11 is
 * authoritative guest r0 while generated/stub code runs. Both memory cells
 * must be current before every C call and re-fetched after it. The CPSR-named
 * macros retain their established call sites but synchronize both caches. */
.macro CPSR_FLUSH
#if CGBA_SH4_PIN_GUEST_R0
  mov.l   r11, @r14
#endif
  mov     #REG_CPSR, r0
  mov.l   r8, @(r0,r14)
.endm
.macro CPSR_RELOAD
#if CGBA_SH4_PIN_GUEST_R0
  mov.l   @r14, r11
#endif
  mov     #REG_CPSR, r0
  mov.l   @(r0,r14), r8
.endm
.macro CPSR_RELOAD_R1
#if CGBA_SH4_PIN_GUEST_R0
  mov.l   @r14, r11
#endif
  mov     r14, r1
  add     #REG_CPSR, r1
  mov.l   @r1, r8
.endm
.macro CPSR_FLUSH_R1
#if CGBA_SH4_PIN_GUEST_R0
  mov.l   r11, @r14
#endif
  mov     r14, r1
  add     #REG_CPSR, r1
  mov.l   r8, @r1
.endm

.macro REDISPATCH_IF_NULL
  tst     r0, r0
  bf      7f                      /* R0 != 0: resolved host block -> jump to it */
  mov.l   8f, r0                  /* NULL: cold-code gate or overflow retry */
  mov.l   @r0, r1
  tst     r1, r1
  bt      6f                      /* not cold: cache-overflow retry */
  mov     #0, r1
  mov.l   r1, @r0                 /* clear cgba_cold_pending */
  bra     sh4_cold_interp_entry   /* interpret a not-yet-hot chunk */
  nop
6:
  bra     lookup_pc
  nop
  .align 2
8:
  .long   _cgba_cold_pending
7:
.endm

/* ---- resident vector table (base pinned in R9 by the entry prologue) ----
 * Generated code reaches these with MOV.L @(disp4,R9),Rn (2 bytes) instead of
 * a 10-12 byte inline literal island. Entry ORDER must match the SH4G_VEC_*
 * enum in ports/fxcg100/sh4/sh4_emit_glue.h. */
  .align 2
defsymbl(cgba_sh4_vec_table)
  .long   sh4_pc_redispatch                     /*  0 SH4G_VEC_pc_redispatch */
  .long   sh4_indirect_branch_arm               /*  1 SH4G_VEC_ib_arm        */
  .long   sh4_indirect_branch_thumb             /*  2 SH4G_VEC_ib_thumb      */
  .long   sh4_indirect_branch_dual              /*  3 SH4G_VEC_ib_dual       */
  .long   sh4_indirect_branch_dual_thumb_current /* 4 SH4G_VEC_ib_dual_thumb_current */
  .long   sh4_update_gba                        /*  5 SH4G_VEC_update_gba    */
  .long   sh4_helper_exit                       /*  6 SH4G_VEC_helper_exit   */
  .long   execute_swi                           /*  7 SH4G_VEC_execute_swi   */
  .long   sh4_cheat_hook                        /*  8 SH4G_VEC_cheat_hook    */
  .long   sh4_hle_div                           /*  9 SH4G_VEC_hle_div       */
  .long   _ws_cyc_seq                           /* 10 SH4G_VEC_ws_cyc_seq    */
  .long   _ws_cyc_nseq                          /* 11 SH4G_VEC_ws_cyc_nseq   */
  .long   _iwram + 0x8000                       /* 12 SH4G_VEC_iwram_data    */
  .long   _ewram                                /* 13 SH4G_VEC_ewram_data    */
#if defined(CGBA_VIDEO_BLEND_PALETTE_CACHE) || \
    defined(CGBA_VIDEO_BACKDROP_SHADOW_PALETTE)
  .long   _palette_ram_dirty                    /* 14 SH4G_VEC_palette_dirty */
#endif

/* ---- main entry: execute_arm_translate_internal(cycles, reg_ptr) ---- */
/* R4 = cycle counter, R5 = pointer to reg[] */

defsymbl(execute_arm_translate_internal)
  mov.l   r8,  @-r15
  mov.l   r9,  @-r15
  mov.l   r10, @-r15
  mov.l   r11, @-r15
  mov.l   r12, @-r15
  mov.l   r13, @-r15
  mov.l   r14, @-r15
  sts.l   pr,  @-r15

  mov     r5, r14                 /* reg[] base */
  mov     r4, r13                 /* cycle counter */
  mov.l   9f, r9                  /* pin &cgba_sh4_vec_table (see enum in
                                     sh4_emit_glue.h; callee-saved across all
                                     C calls, so loaded exactly once here) */
  mov.l   10f, r10                /* pin &sh4_block_exit for JMP @R10 exits */
  CPSR_RELOAD                     /* prime R8 CPSR and R11 guest-r0 caches */

  /* CPU might be sleeping; do not wake it up. */
  mov     #CPU_HALT_STATE, r0
  mov.l   @(r0,r14), r0
  tst     r0, r0
  bt      .Lcpu_awake             /* halt_state == 0 -> normal dispatch */
  bra     cpu_sleep_loop
  nop
.Lcpu_awake:

  bra     lookup_pc
  nop
  .align 2
9:
  .long   cgba_sh4_vec_table
10:
  .long   sh4_block_exit

/* ---- return to the main C caller; ARM state already saved ---- */
return_to_main:
  CPSR_FLUSH_R1                   /* commit cached CPSR and guest r0. R1-based: R0 is
                                     the entry point's return value here
                                     (remaining cycles / frame-complete tag),
                                     loaded by the callers' delay slots. */
  lds.l   @r15+, pr
  mov.l   @r15+, r14
  mov.l   @r15+, r13
  mov.l   @r15+, r12
  mov.l   @r15+, r11
  mov.l   @r15+, r10
  mov.l   @r15+, r9
  rts
  mov.l   @r15+, r8               /* delay slot: final pop */

/* ---- cycle counter ran out: process the next hardware event ---- */
/* Entered from generated code with R4 = current GBA PC. */
defsymbl(sh4_update_gba)
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* reg[REG_PC] = current PC */
  CPSR_FLUSH
  sts.l   pr, @-r15               /* save return into the block */

  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    update_gba
  CPSR_RELOAD_R1                  /* update_gba may take an IRQ (CPSR write) */

  cmp/pz  r0
  bt      1f                      /* bit31 clear -> keep running */
  /* Frame complete: DISCARD the saved in-block return before unwinding, or
   * return_to_main pops it into PR and shifts every callee-saved restore by
   * one slot (R14 <- caller's PR, ...), resuming C with a corrupted register
   * file. Latent since bring-up (nothing called this entry until the idle-
   * loop eliminate path); found via the on-screen crash report. */
  lds.l   @r15+, pr
  bra     return_to_main
  nop
1:
  mov     r0, r13
  mov     #-1, r1
  shlr16  r1
  shlr    r1
  and     r1, r13                 /* new cycle count = ret & 0x7FFF */

  shll    r0                      /* test bit30 (PC changed) */
  cmp/pz  r0
  bf      lookup_pc_pop           /* bit30 set -> re-dispatch on new PC */

  lds.l   @r15+, pr               /* else resume the same block */
  rts
  nop

lookup_pc_pop:
  lds.l   @r15+, pr               /* discard saved in-block return */
lookup_pc:
  /* Hot path: probe the inline ROM branch hash (sh4_indirect_branch_*)
   * instead of calling the C resolver. Every IRQ return, BIOS exit,
   * helper PC re-dispatch and update_gba redirect funnels through here —
   * on hardware that was ~200+ C resolver calls per frame for targets that
   * are almost always already-translated ROM blocks. The hash stubs handle
   * the BIOS (<0x4000) fallback themselves and use the same C resolver on
   * a miss. Single-block diff mode keeps the legacy direct C path: the
   * stubs' sb slow path returns to main after resolving, which is not what
   * lookup_pc's mid-run callers expect. */
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   lookup_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bf      lookup_pc_c             /* single-block diff -> legacy C resolver */
#endif
  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  mov     #0x20, r0
  tst     r0, r8                  /* T = (thumb bit clear); CPSR cached in R8 */
  bt      1f                      /* ARM mode */
  bra     sh4_indirect_branch_thumb
  nop
1:
  bra     sh4_indirect_branch_arm
  nop
#ifdef CGBA_SH4_DIFF_HARNESS
  .align 2
lookup_sb:
  .long   _cgba_dynarec_single_block
lookup_pc_c:
  mov     #0x20, r0
  tst     r0, r8                  /* T = (thumb bit clear); CPSR cached in R8 */
  bt      1f                      /* ARM mode */

  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  CPSR_FLUSH
  CALL    block_lookup_address_thumb
  CPSR_RELOAD_R1
  REDISPATCH_IF_NULL
  jmp     @r0
  nop
#endif
1:
  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  CPSR_FLUSH
  CALL    block_lookup_address_arm
  CPSR_RELOAD_R1
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

/* ---- CPU halted: spin on update_gba until an event wakes it ---- */
cpu_sleep_loop:
  CPSR_FLUSH
  mov     r13, r4
  CALL    update_gba
  CPSR_RELOAD_R1
  cmp/pz  r0
  bt      .Lsleep_continue
  bra     return_to_main          /* frame complete */
  nop
.Lsleep_continue:
  mov     r0, r13
  mov     #-1, r1
  shlr16  r1
  shlr    r1
  and     r1, r13
  /* CPU active again: re-dispatch */
  bra     lookup_pc
  nop

/* ---- unified block exit: R4 = target GBA PC ----
 * Every translated block branch funnels here (jumped to, not called). It stores
 * the new PC, lets update_gba process pending hardware events, returns to the
 * main C caller when the frame is done, otherwise re-dispatches on the new PC.
 * The cheap direct-chaining fast path (when the cycle budget is not yet spent)
 * is handled inline in generated code and bypasses this entirely. */
defsymbl(sh4_block_exit)
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* reg[REG_PC] = target PC */
  CPSR_FLUSH
  /* Differential-harness single-block mode: return to the C caller after one
     block (PC already committed) so the interpreter can be diffed lockstep.
     Local literal: the shared sb_flag pool drifted past MOV.L's +1KB forward
     reach when the CPSR-cache sync sequences grew this region. */
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   bx_sb, r0
  mov.l   @r0, r0
  tst     r0, r0
  bt      1f
  bra     return_to_main
  mov     r13, r0                 /* single-block diff: return remaining cycles */
1:
#endif
  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    update_gba
  CPSR_RELOAD_R1                  /* update_gba may take an IRQ (CPSR write) */
  cmp/pz  r0
  bt      .Lblock_continue
  bra     return_to_main          /* bit31 set -> frame complete */
  nop
.Lblock_continue:
  mov     r0, r13
  mov     #-1, r1
  shlr16  r1
  shlr    r1
  and     r1, r13                 /* cycles = ret & 0x7FFF */
  bra     lookup_pc               /* always re-dispatch on reg[REG_PC] */
  nop

#ifdef CGBA_SH4_DIFF_HARNESS
  .align 2
bx_sb:
  .long   _cgba_dynarec_single_block
#endif

/* ---- indirect branches (BX, computed PC); R4 = target GBA address ----
 * block_lookup_address_* commits reg[REG_PC] (see cpu_threaded.c), so after the
 * lookup the single-block diff mode can return to the C caller with PC correct,
 * exactly like sh4_block_exit. R0 = resolved host block; R13 = remaining cycles. */
/* Inline ROM branch-hash fast path. The C resolver is a far call plus chain
 * walk on every BX/return/Thumb-BL suffix. For ROM targets that are already
 * translated, probe the ROM branch hash here and jump directly to the cached
 * host block. Keep single-block diff mode on the old C path, and keep open-BIOS
 * targets on the interpreter fallback used by the Metroid cutscene fix. */
defsymbl(sh4_indirect_branch_arm)
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bf      iba_slow                /* single-block diff -> unchanged C path */
#endif
  mov.l   ibh_bios_limit, r1
  cmp/hs  r1, r4                  /* T = target >= 0x4000 */
  bt      iba_hash
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* match resolver before BIOS fallback */
  bra     sh4_bios_fallback_entry
  nop
iba_hash:
  mov     r4, r2
  shlr2   r2
  shll2   r2                      /* key = pc & ~3 (ARM) */
#if CGBA_SH4_KNOWN_TARGET_CACHE
  mov     r2, r0
  shlr2   r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_ahot_key, r1
  mov.l   @(r0,r1), r3
  cmp/eq  r2, r3
  bf      iba_hash_miss
  mov.l   ibh_ahot_ptr, r1
  mov.l   @(r0,r1), r7
  tst     r7, r7
  bt      iba_hash_miss
  INC_COUNTER cgba_dynarec_ibh_arm_hot_count
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)
  jmp     @r7
  nop
iba_hash_miss:
#endif
  mov.l   ibh_mul, r3
  dmulu.l r2, r3
  sts     macl, r0
  mov     #-(32-ROM_BRANCH_HASH_BITS), r1
  shld    r1, r0                  /* hash index = product >> (32 - bits) */
  shll2   r0                      /* * sizeof(u32) */
  mov.l   ibh_hash, r1
  mov.l   @(r0,r1), r5            /* blk_offset = rom_branch_hash[hash] */
  mov.l   ibh_tcache, r6
iba_walk:
  tst     r5, r5
  bt      iba_slow                /* miss */
  mov     r5, r7
  add     r6, r7                  /* r7 = &rom_translation_cache[blk_offset] */
  mov.l   @r7, r0                 /* hashhdr_type.pc_value */
  cmp/eq  r2, r0
  bt      iba_hit
  mov.l   @(4,r7), r5             /* hashhdr_type.next_entry */
  bra     iba_walk
  nop
iba_hit:
  INC_COUNTER cgba_dynarec_ibh_arm_hit_count
  add     #8, r7                  /* sizeof(hashhdr_type) + prologue size */
#if CGBA_SH4_KNOWN_TARGET_CACHE
  mov     r2, r0
  shlr2   r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_ahot_key, r1
  mov.l   r2, @(r0,r1)
  mov.l   ibh_ahot_ptr, r1
  mov.l   r7, @(r0,r1)
#endif
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* commit reg[REG_PC] = pc */
  jmp     @r7
  nop
iba_slow:
  INC_COUNTER cgba_dynarec_ibh_arm_slow_count
  CPSR_FLUSH
  CALL    block_lookup_address_arm
  CPSR_RELOAD_R1
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      iba_disp                /* not single-block -> dispatch */
  bra     return_to_main          /* single-block diff: stop after this block */
  mov     r13, r0                 /* delay slot: return remaining cycles */
iba_disp:
#endif
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_thumb)
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bf      ibt_slow
#endif
  mov.l   ibh_bios_limit, r1
  cmp/hs  r1, r4                  /* T = target >= 0x4000 */
  bt      ibt_hash
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)
  bra     sh4_bios_fallback_entry
  nop
ibt_hash:
  mov     r4, r0
  or      #1, r0
  mov     r0, r2                  /* key = pc | 1 (Thumb) */
#if CGBA_SH4_KNOWN_TARGET_CACHE
  mov     r2, r0
  shlr    r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_thot_key, r1
  mov.l   @(r0,r1), r3
  cmp/eq  r2, r3
  bf      ibt_hash_miss
  mov.l   ibh_thot_ptr, r1
  mov.l   @(r0,r1), r7
  tst     r7, r7
  bt      ibt_hash_miss
  INC_COUNTER cgba_dynarec_ibh_thumb_hot_count
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)
  jmp     @r7
  nop
ibt_hash_miss:
#endif
  mov.l   ibh_mul, r3
  dmulu.l r2, r3
  sts     macl, r0
  mov     #-(32-ROM_BRANCH_HASH_BITS), r1
  shld    r1, r0
  shll2   r0
  mov.l   ibh_hash, r1
  mov.l   @(r0,r1), r5
  mov.l   ibh_tcache, r6
ibt_walk:
  tst     r5, r5
  bt      ibt_slow
  mov     r5, r7
  add     r6, r7
  mov.l   @r7, r0
  cmp/eq  r2, r0
  bt      ibt_hit
  mov.l   @(4,r7), r5
  bra     ibt_walk
  nop
ibt_hit:
  INC_COUNTER cgba_dynarec_ibh_thumb_hit_count
  add     #8, r7
#if CGBA_SH4_KNOWN_TARGET_CACHE
  mov     r2, r0
  shlr    r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_thot_key, r1
  mov.l   r2, @(r0,r1)
  mov.l   ibh_thot_ptr, r1
  mov.l   r7, @(r0,r1)
#endif
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)
  jmp     @r7
  nop
ibt_slow:
  INC_COUNTER cgba_dynarec_ibh_thumb_slow_count
  CPSR_FLUSH
  CALL    block_lookup_address_thumb
  CPSR_RELOAD_R1
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      ibt_disp
  bra     return_to_main
  mov     r13, r0
ibt_disp:
#endif
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_dual)
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     ibd_slow                /* single-block diff -> unchanged C path */
  nop
1:
#endif

  mov     r4, r0
  tst     #1, r0                  /* T = ARM target (bit0 clear) */
  bt      ibd_arm_hash

  /* Thumb target: pc = target & ~1, key = pc | 1. */
  mov     r4, r2
  mov     #-2, r0
  and     r0, r2                  /* r2 = pc */
  mov     r2, r0
  or      #1, r0
  mov     r0, r2                  /* r2 = hash key */
  mov.l   ibh_bios_limit, r1
  cmp/hs  r1, r2                  /* T = key >= 0x4000 */
  bt      1f
  bra     ibd_slow
  nop
1:
  bra     ibd_thumb_hash
  nop

ibd_arm_hash:
  /* ARM target: pc = (target + 2) & ~3, key = pc. */
  mov     r4, r2
  add     #2, r2
  shlr2   r2
  shll2   r2                      /* r2 = key = pc */
  mov.l   ibh_bios_limit, r1
  cmp/hs  r1, r2                  /* T = pc >= 0x4000 */
  bt      1f
  bra     ibd_slow
  nop
1:

  mov     r2, r0
  shlr2   r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_dhot_key, r1
  mov.l   @(r0,r1), r3
  cmp/eq  r2, r3
  bf      ibd_arm_hash_miss
  mov.l   ibh_dhot_ptr, r1
  mov.l   @(r0,r1), r7
  tst     r7, r7
  bt      ibd_arm_hash_miss
  INC_COUNTER cgba_dynarec_ibh_dual_hot_arm_count
  bra     ibd_arm_commit
  nop

ibd_arm_hash_miss:
  mov.l   ibh_mul, r3
  dmulu.l r2, r3
  sts     macl, r0
  mov     #-(32-ROM_BRANCH_HASH_BITS), r1
  shld    r1, r0
  shll2   r0
  mov.l   ibh_hash, r1
  mov.l   @(r0,r1), r5
  mov.l   ibh_tcache, r6
ibd_arm_walk:
  tst     r5, r5
  bf      1f
  bra     ibd_slow
  nop
1:
  mov     r5, r7
  add     r6, r7
  mov.l   @r7, r0
  cmp/eq  r2, r0
  bt      ibd_arm_hit
  mov.l   @(4,r7), r5
  bra     ibd_arm_walk
  nop
ibd_arm_hit:
  INC_COUNTER cgba_dynarec_ibh_dual_arm_hit_count
  add     #8, r7
  mov     r2, r0
  shlr2   r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_dhot_key, r1
  mov.l   r2, @(r0,r1)
  mov.l   ibh_dhot_ptr, r1
  mov.l   r7, @(r0,r1)
ibd_arm_commit:
  mov     #REG_PC, r0
  mov.l   r2, @(r0,r14)           /* commit reg[REG_PC] = pc */
  mov     #-33, r0
  and     r0, r8                  /* clear CPSR.T (cached) */
  jmp     @r7
  nop

/* Hot subcase for Thumb BX when the generated code has already checked that the
 * target bit is set. Current CPSR is already Thumb, so a hot cache hit only needs
 * to resolve the translated Thumb block and commit PC; it skips the generic dual
 * ARM/Thumb split and the redundant CPSR.T store. Misses and single-block diff
 * mode rejoin the exact generic dual resolver below. */
defsymbl(sh4_indirect_branch_dual_thumb_current)
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     ibd_slow
  nop
1:
#endif
  mov     r4, r2
  mov     #-2, r0
  and     r0, r2                  /* r2 = pc = target & ~1 */
  mov     r2, r0
  or      #1, r0
  mov     r0, r2                  /* r2 = hash key = pc | 1 */
  mov.l   ibh_bios_limit, r1
  cmp/hs  r1, r2                  /* T = key >= 0x4000 */
  bt      2f
  bra     ibd_slow
  nop
2:
  mov     r2, r0
  shlr    r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_dhot_key, r1
  mov.l   @(r0,r1), r3
  cmp/eq  r2, r3
  bt      3f
  bra     ibd_thumb_hash_miss
  nop
3:
  mov.l   ibh_dhot_ptr, r1
  mov.l   @(r0,r1), r7
  tst     r7, r7
  bf      4f
  bra     ibd_thumb_hash_miss
  nop
4:
  INC_COUNTER cgba_dynarec_ibh_dual_hot_thumb_count
  mov     r2, r1
  mov     #-2, r0
  and     r0, r1                  /* pc = key & ~1 */
  mov     #REG_PC, r0
  mov.l   r1, @(r0,r14)
  jmp     @r7
  nop

ibd_thumb_hash:
  mov     r2, r0
  shlr    r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_dhot_key, r1
  mov.l   @(r0,r1), r3
  cmp/eq  r2, r3
  bf      ibd_thumb_hash_miss
  mov.l   ibh_dhot_ptr, r1
  mov.l   @(r0,r1), r7
  tst     r7, r7
  bt      ibd_thumb_hash_miss
  INC_COUNTER cgba_dynarec_ibh_dual_hot_thumb_count
  bra     ibd_thumb_commit
  nop

ibd_thumb_hash_miss:
  mov.l   ibh_mul, r3
  dmulu.l r2, r3
  sts     macl, r0
  mov     #-(32-ROM_BRANCH_HASH_BITS), r1
  shld    r1, r0
  shll2   r0
  mov.l   ibh_hash, r1
  mov.l   @(r0,r1), r5
  mov.l   ibh_tcache, r6
ibd_thumb_walk:
  tst     r5, r5
  bt      ibd_slow
  mov     r5, r7
  add     r6, r7
  mov.l   @r7, r0
  cmp/eq  r2, r0
  bt      ibd_thumb_hit
  mov.l   @(4,r7), r5
  bra     ibd_thumb_walk
  nop
ibd_thumb_hit:
  INC_COUNTER cgba_dynarec_ibh_dual_thumb_hit_count
  add     #8, r7
  mov     r2, r0
  shlr    r0
  and     #0x3f, r0
  shll2   r0
  mov.l   ibh_dhot_key, r1
  mov.l   r2, @(r0,r1)
  mov.l   ibh_dhot_ptr, r1
  mov.l   r7, @(r0,r1)
ibd_thumb_commit:
  mov     r2, r1
  mov     #-2, r0
  and     r0, r1                  /* pc = key & ~1 */
  mov     #REG_PC, r0
  mov.l   r1, @(r0,r14)
  mov     #0x20, r0
  or      r0, r8                  /* set CPSR.T (cached) */
  jmp     @r7
  nop

ibd_slow:
  INC_COUNTER cgba_dynarec_ibh_dual_slow_count
  CPSR_FLUSH
  CALL    block_lookup_address_dual
  CPSR_RELOAD_R1                  /* the dual resolver writes CPSR.T */
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   ibh_sb, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     return_to_main
  mov     r13, r0
1:
#endif
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

  .align 2
#ifdef CGBA_SH4_DIFF_HARNESS
ibh_sb:
  .long   _cgba_dynarec_single_block
#endif
ibh_bios_limit:
  .long   0x00004000
ibh_mul:
  .long   0x9E3779B1              /* 2654435761 (Knuth multiplicative hash) */
ibh_hash:
  .long   _rom_branch_hash
ibh_tcache:
  .long   _rom_translation_cache
#if CGBA_SH4_KNOWN_TARGET_CACHE
ibh_ahot_key:
  .long   _cgba_dynarec_arm_hot_key
ibh_ahot_ptr:
  .long   _cgba_dynarec_arm_hot_ptr
ibh_thot_key:
  .long   _cgba_dynarec_thumb_hot_key
ibh_thot_ptr:
  .long   _cgba_dynarec_thumb_hot_ptr
#endif
ibh_dhot_key:
  .long   _cgba_dynarec_dual_hot_key
ibh_dhot_ptr:
  .long   _cgba_dynarec_dual_hot_ptr

/* ---- BIOS fallback: interpret the open-BIOS routine, then resume JIT. ---- */
defsymbl(sh4_bios_fallback_entry)
  CPSR_FLUSH
  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    cgba_sh4_bios_fallback
  CPSR_RELOAD_R1                  /* the interpreter rewrites CPSR */
  cmp/pz  r0
  bt      1f                      /* helper stopped at BIOS return PC */
  bra     return_to_main          /* helper completed the frame */
  nop
1:
  mov     r0, r13                 /* helper stopped at BIOS return PC */
  bra     lookup_pc
  nop

/* ---- cold-code gate: interpret a chunk of not-yet-hot code ---- */
defsymbl(sh4_cold_interp_entry)
  CPSR_FLUSH
  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    cgba_sh4_cold_interp
  CPSR_RELOAD_R1                  /* the interpreter rewrites CPSR */
  cmp/pz  r0
  bt      1f
  bra     return_to_main          /* frame completed inside */
  nop
1:
  mov     r0, r13
  bra     lookup_pc
  nop

/* ---- RAM self-modifying-code flush; R6 = current PC ---- */
defsymbl(smc_write)
  mov     #REG_PC, r0
  mov.l   r6, @(r0,r14)
  sts.l   pr, @-r15
  CPSR_FLUSH
  CALL    flush_translation_cache_ram
  CPSR_RELOAD
  lds.l   @r15+, pr
  bra     lookup_pc
  nop

/* ---- read CPSR: flags are already materialized in CPSR ---- */
defsymbl(execute_read_cpsr)
  rts
  mov     r8, r0                  /* delay slot: cached CPSR */

/* ---- software interrupt; R4 = return PC (next instruction) ---- */
/* Exact divide HLE is a direct generated-code call, so bracket its C body with
 * the same persistent-state synchronization used by the shared trampolines. */
defsymbl(sh4_hle_div)
  sts.l   pr, @-r15
  CPSR_FLUSH
  CALL    cgba_sh4_hle_div
  CPSR_RELOAD
  lds.l   @r15+, pr
  rts
  nop

defsymbl(execute_swi)
  sts.l   pr, @-r15
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* stash next PC for the C handler */
  CPSR_FLUSH
  CALL    sh4_swi_handler
  CPSR_RELOAD                     /* SVC banking rewrote CPSR */
  lds.l   @r15+, pr
  rts
  nop

/* ---- cheat processing hook (called at the master PC) ---- */
defsymbl(sh4_cheat_hook)
  sts.l   pr, @-r15
  CPSR_FLUSH
  CALL    process_cheats
  CPSR_RELOAD
  lds.l   @r15+, pr
  rts
  nop

/* ---- helper exit: dispatch on the C helper's return code ----
 * R4 = target PC (already committed to reg[REG_PC] by the helper),
 * R1 = helper return code: bit1 set = a store raised an alert (IRQ unmask,
 * HALT, SMC) and update_gba must process it now -> sh4_block_exit; otherwise
 * it is a pure PC change (POP {pc}, MOV pc, LDM {pc}, MSR-vectored IRQ, ...)
 * and re-dispatching without an update_gba pass is exactly the interpreter's
 * behavior -- it only services events on cycle exhaustion or alerts, and the
 * dynarec's cycle gates/branch exits still bound the budget. */
defsymbl(sh4_helper_exit)
  mov     r1, r0
  tst     #2, r0                  /* T = 1: no alert -> pure PC change */
  bt      sh4_pc_redispatch
  bra     sh4_block_exit          /* alert: process events (R4 = PC) */
  nop

/* ---- pure PC re-dispatch, no update_gba; R4 = target PC, current mode ---- */
defsymbl(sh4_pc_redispatch)
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* reg[REG_PC] = target PC */
#ifdef CGBA_SH4_DIFF_HARNESS
  mov.l   sb_flag2, r0
  mov.l   @r0, r0
  tst     r0, r0
  bt      1f
  bra     return_to_main          /* single-block diff: stop after this block */
  mov     r13, r0                 /* delay slot: return remaining cycles */
1:
#endif
  bra     lookup_pc
  nop

#ifdef CGBA_SH4_DIFF_HARNESS
.align 2
sb_flag2:
  .long   _cgba_dynarec_single_block
#endif

/* ---- compact per-instruction C-helper calls -------------------------------
 * Call sites in translated code are the FOOTPRINT problem: the classic glue
 * (two inline literal consts + far call + debit + redispatch) is ~70 bytes per
 * helper-called guest instruction, and ARM gameplay code is helper-heavy, so
 * blocks blow the ROM translation cache and it wholesale-flushes every few
 * frames. The compact form is a fixed ~28-byte site:
 *
 *   [pad NOP]              ; parity fix so the literal block is 4-aligned
 *   mov.l  L, r0           ; first literal = trampoline address
 *   jsr    @r0             ; PR -> the `bra 2f` below
 *   nop
 *   bra    2f              ; helper continue-path returns HERE
 *   nop
 * L:.long  TRAMP, fn, opcode, pc [, cycle_count]
 * 2:
 *
 * The trampolines read (fn, opcode, pc[, cycles]) through PR, run the helper,
 * apply the memory-cycle debit and the PC-change redispatch generically, and
 * `rts` back to the site's `bra 2f` on the continue path. R12 is callee-saved
 * and reserved from ordinary translated instructions, so it carries the
 * literal cursor across the C call (resident fastmem uses it only within its
 * own routines). Semantics mirror the old inline glue exactly: the extra-cycles
 * debit is unconditional (MEM forms), the accumulated cycle_count is debited
 * only on the PC-change exit, and helper return codes route through
 * sh4_helper_exit (1 = pure PC change, 2 = store alert). */

defsymbl(sh4_op2_tramp)             /* plain: fn(opcode, pc), no exits */
  sts     pr, r1
  sts.l   pr, @-r15
  CPSR_FLUSH
  add     #8, r1                    /* skip the TRAMP literal -> fn */
  mov.l   @r1+, r0                  /* fn */
  mov.l   @r1+, r4                  /* opcode */
  jsr     @r0
  mov.l   @r1+, r5                  /* pc (delay slot: not PC-relative) */
  CPSR_RELOAD_R1
  lds.l   @r15+, pr
  rts
  nop

defsymbl(sh4_op2_mem_tramp)         /* + unconditional guest-memory debit */
  sts     pr, r1
  sts.l   pr, @-r15
  CPSR_FLUSH
  add     #8, r1                    /* skip the TRAMP literal -> fn */
  mov.l   @r1+, r0
  mov.l   @r1+, r4
  jsr     @r0
  mov.l   @r1+, r5
  CPSR_RELOAD_R1
  mov.l   tram_extra, r1            /* &cgba_sh4_extra_cycles */
  mov.l   @r1, r1
  sub     r1, r13
  lds.l   @r15+, pr
  rts
  nop

defsymbl(sh4_op2_pc_tramp)          /* + redispatch when the helper moved PC */
  sts     pr, r1
  sts.l   pr, @-r15
  CPSR_FLUSH
  add     #8, r1                    /* skip the TRAMP literal -> fn */
  mov.l   @r1+, r0
  mov.l   @r1+, r4
  mov     r1, r12                   /* literal cursor survives the C call */
  jsr     @r0
  mov.l   @r12+, r5                 /* pc; r12 -> cycle_count literal */
  CPSR_RELOAD_R1
  bra     op2_pc_common
  mov     #0, r7                    /* full-width cycle descriptor */

defsymbl(sh4_op2_pc_mem_tramp)      /* + memory debit + PC redispatch */
  sts     pr, r1
  sts.l   pr, @-r15
  CPSR_FLUSH
  add     #8, r1                    /* skip the TRAMP literal -> fn */
  mov.l   @r1+, r0
  mov.l   @r1+, r4
  mov     r1, r12
  jsr     @r0
  mov.l   @r12+, r5
  CPSR_RELOAD_R1
  mov.l   tram_extra, r1
  mov.l   @r1, r1
  sub     r1, r13                   /* memory cycles: both paths (as before) */
  bra     op2_pc_common
  mov     #0, r7                    /* full-width cycle descriptor */

#if CGBA_SH4_COMPACT_FASTMEM_TUPLE
/* Compact fastmem descriptors pack the 16-bit cycle debit into the low half
 * of the word at PR+20. Single-transfer params occupy the high half; block
 * descriptors keep their full params word at PR+24. */
defsymbl(sh4_fastmem_pc_mem_tramp)
  sts     pr, r1
  sts.l   pr, @-r15
  CPSR_FLUSH
  add     #8, r1
  mov.l   @r1+, r0
  mov.l   @r1+, r4
  mov     r1, r12
  jsr     @r0
  mov.l   @r12+, r5
  CPSR_RELOAD_R1
  mov.l   tram_extra, r1
  mov.l   @r1, r1
  sub     r1, r13
  bra     op2_pc_common
  mov     #1, r7                    /* low-16 cycle descriptor */
#endif

op2_pc_common:
  tst     r0, r0
  bt      1f                        /* R0 == 0: continue the block */
  mov.l   @r12, r1
  tst     r7, r7
  bt      2f
  extu.w  r1, r1
2:
  sub     r1, r13                   /* accumulated cycles: exit path only */
  mov     r0, r1                    /* helper code for sh4_helper_exit */
  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4             /* R4 = committed reg[REG_PC] */
  lds.l   @r15+, pr                 /* balance the stack (value unused) */
  bra     sh4_helper_exit
  nop
1:
  lds.l   @r15+, pr
  rts
  nop

.align 2
tram_extra:
  .long   _cgba_sh4_extra_cycles

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
