/* gameplaySP — SH-4A (SH7305 / fx-CG100) dynarec entry/exit trampoline.
 *
 * Mirrors mips/mips_stub.S for the SH4 host. MVP register model
 * (see sh4/sh4_emit.h): all guest ARM registers and the NZCV flags live in the
 * reg[] state array; flags are kept materialized in REG_CPSR bits 28..31, so no
 * flag collapse/extract is needed at C-call boundaries. Guest memory accesses
 * go through the execute_load_* and execute_store_* C helpers.
 *
 * SH4 ABI: integer args in R4..R7, return in R0, callee-saved R8..R15 + PR.
 *
 * Persistent dynarec registers (preserved across translated blocks and across
 * C calls, since R8..R14 are callee-saved):
 *   R14 = reg[] base pointer
 *   R13 = cycle counter (counts down; block exits when it crosses 0)
 *
 * NOTE: assembled and checked with sh-elf-as; not yet validated on hardware.
 */

#include "../gpsp_config.h"

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

.text
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
.macro REDISPATCH_IF_NULL
  tst     r0, r0
  bf      7f                      /* R0 != 0: resolved host block -> jump to it */
  bra     lookup_pc               /* R0 == 0: re-dispatch (BRA disp12 reaches) */
  nop
7:
.endm

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

  /* CPU might be sleeping; do not wake it up. */
  mov     #CPU_HALT_STATE, r0
  mov.l   @(r0,r14), r0
  tst     r0, r0
  bf      cpu_sleep_loop          /* halt_state != 0 -> sleep */

  bra     lookup_pc
  nop

/* ---- return to the main C caller; ARM state already saved ---- */
return_to_main:
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
  sts.l   pr, @-r15               /* save return into the block */

  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    update_gba

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
  mov     #REG_CPSR, r0
  mov.l   @(r0,r14), r1
  mov     #0x20, r0
  tst     r0, r1                  /* T = (thumb bit clear) */
  bt      1f                      /* ARM mode */

  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  CALL    block_lookup_address_thumb
  REDISPATCH_IF_NULL
  jmp     @r0
  nop
1:
  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  CALL    block_lookup_address_arm
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

/* ---- CPU halted: spin on update_gba until an event wakes it ---- */
cpu_sleep_loop:
  mov     r13, r4
  CALL    update_gba
  cmp/pz  r0
  bf      return_to_main          /* frame complete */
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
  /* Differential-harness single-block mode: return to the C caller after one
     block (PC already committed) so the interpreter can be diffed lockstep. */
  mov.l   sb_flag, r0
  mov.l   @r0, r0
  tst     r0, r0
  bt      1f
  bra     return_to_main
  mov     r13, r0                 /* single-block diff: return remaining cycles */
1:
  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    update_gba
  cmp/pz  r0
  bf      return_to_main          /* bit31 set -> frame complete */
  mov     r0, r13
  mov     #-1, r1
  shlr16  r1
  shlr    r1
  and     r1, r13                 /* cycles = ret & 0x7FFF */
  bra     lookup_pc               /* always re-dispatch on reg[REG_PC] */
  nop

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
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bf      iba_slow                /* single-block diff -> unchanged C path */
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
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* commit reg[REG_PC] = pc */
  add     #8, r7                  /* sizeof(hashhdr_type) + prologue size */
  jmp     @r7
  nop
iba_slow:
  INC_COUNTER cgba_dynarec_ibh_arm_slow_count
  CALL    block_lookup_address_arm
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      iba_disp                /* not single-block -> dispatch */
  bra     return_to_main          /* single-block diff: stop after this block */
  mov     r13, r0                 /* delay slot: return remaining cycles */
iba_disp:
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_thumb)
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bf      ibt_slow
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
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)
  add     #8, r7
  jmp     @r7
  nop
ibt_slow:
  INC_COUNTER cgba_dynarec_ibh_thumb_slow_count
  CALL    block_lookup_address_thumb
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      ibt_disp
  bra     return_to_main
  mov     r13, r0
ibt_disp:
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_dual)
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     ibd_slow                /* single-block diff -> unchanged C path */
  nop
1:

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
  mov     #REG_CPSR, r0
  mov.l   @(r0,r14), r1
  mov     #-33, r0
  and     r0, r1                  /* clear CPSR.T */
  mov     #REG_CPSR, r0
  mov.l   r1, @(r0,r14)
  jmp     @r7
  nop

/* Hot subcase for Thumb BX when the generated code has already checked that the
 * target bit is set. Current CPSR is already Thumb, so a hot cache hit only needs
 * to resolve the translated Thumb block and commit PC; it skips the generic dual
 * ARM/Thumb split and the redundant CPSR.T store. Misses and single-block diff
 * mode rejoin the exact generic dual resolver below. */
defsymbl(sh4_indirect_branch_dual_thumb_current)
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     ibd_slow
  nop
1:
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
  mov     #REG_CPSR, r0
  mov.l   @(r0,r14), r1
  mov     #0x20, r0
  or      r0, r1                  /* set CPSR.T */
  mov     #REG_CPSR, r0
  mov.l   r1, @(r0,r14)
  jmp     @r7
  nop

ibd_slow:
  INC_COUNTER cgba_dynarec_ibh_dual_slow_count
  CALL    block_lookup_address_dual
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     return_to_main
  mov     r13, r0
1:
  REDISPATCH_IF_NULL
  jmp     @r0
  nop

  .align 2
ibh_bios_limit:
  .long   0x00004000
ibh_mul:
  .long   0x9E3779B1              /* 2654435761 (Knuth multiplicative hash) */
ibh_hash:
  .long   _rom_branch_hash
ibh_tcache:
  .long   _rom_translation_cache
ibh_dhot_key:
  .long   _cgba_dynarec_dual_hot_key
ibh_dhot_ptr:
  .long   _cgba_dynarec_dual_hot_ptr

/* ---- BIOS fallback: interpret the open-BIOS routine, then resume JIT. ---- */
defsymbl(sh4_bios_fallback_entry)
  mov     r13, r4                 /* arg0 = remaining cycles */
  CALL    cgba_sh4_bios_fallback
  cmp/pz  r0
  bt      1f                      /* helper stopped at BIOS return PC */
  bra     return_to_main          /* helper completed the frame */
  nop
1:
  mov     r0, r13                 /* helper stopped at BIOS return PC */
  bra     lookup_pc
  nop

/* ---- RAM self-modifying-code flush; R6 = current PC ---- */
defsymbl(smc_write)
  mov     #REG_PC, r0
  mov.l   r6, @(r0,r14)
  sts.l   pr, @-r15
  CALL    flush_translation_cache_ram
  lds.l   @r15+, pr
  bra     lookup_pc
  nop

/* ---- read CPSR: flags are already materialized in CPSR ---- */
defsymbl(execute_read_cpsr)
  mov     #REG_CPSR, r0
  rts
  mov.l   @(r0,r14), r0           /* delay slot: return CPSR */

/* ---- software interrupt; R4 = return PC (next instruction) ---- */
defsymbl(execute_swi)
  sts.l   pr, @-r15
  mov     #REG_PC, r0
  mov.l   r4, @(r0,r14)           /* stash next PC for the C handler */
  CALL    sh4_swi_handler
  lds.l   @r15+, pr
  rts
  nop

/* ---- cheat processing hook (called at the master PC) ---- */
defsymbl(sh4_cheat_hook)
  sts.l   pr, @-r15
  CALL    process_cheats
  lds.l   @r15+, pr
  rts
  nop

.align 2
sb_flag:
  .long   _cgba_dynarec_single_block

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
  mov.l   sb_flag2, r0
  mov.l   @r0, r0
  tst     r0, r0
  bt      1f
  bra     return_to_main          /* single-block diff: stop after this block */
  mov     r13, r0                 /* delay slot: return remaining cycles */
1:
  bra     lookup_pc
  nop

.align 2
sb_flag2:
  .long   _cgba_dynarec_single_block

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
