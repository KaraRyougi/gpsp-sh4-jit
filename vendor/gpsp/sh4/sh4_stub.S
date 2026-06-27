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
  bf      return_to_main          /* bit31 set (negative) -> frame complete */

  mov     r0, r13
  mov.l   c_mask15, r1
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
  jmp     @r0
  nop
1:
  mov     #REG_PC, r0
  mov.l   @(r0,r14), r4
  CALL    block_lookup_address_arm
  jmp     @r0
  nop

/* ---- CPU halted: spin on update_gba until an event wakes it ---- */
cpu_sleep_loop:
  mov     r13, r4
  CALL    update_gba
  cmp/pz  r0
  bf      return_to_main          /* frame complete */
  mov     r0, r13
  mov.l   c_mask15, r1
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
  mov.l   c_mask15, r1
  and     r1, r13                 /* cycles = ret & 0x7FFF */
  bra     lookup_pc               /* always re-dispatch on reg[REG_PC] */
  nop

/* ---- indirect branches (BX, computed PC); R4 = target GBA address ----
 * block_lookup_address_* commits reg[REG_PC] (see cpu_threaded.c), so after the
 * lookup the single-block diff mode can return to the C caller with PC correct,
 * exactly like sh4_block_exit. R0 = resolved host block; R13 = remaining cycles. */
defsymbl(sh4_indirect_branch_arm)
  CALL    block_lookup_address_arm
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f                      /* not single-block -> dispatch */
  bra     return_to_main          /* single-block diff: stop after this block */
  mov     r13, r0                 /* delay slot: return remaining cycles */
1:
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_thumb)
  CALL    block_lookup_address_thumb
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     return_to_main
  mov     r13, r0
1:
  jmp     @r0
  nop

defsymbl(sh4_indirect_branch_dual)
  CALL    block_lookup_address_dual
  mov.l   sb_flag, r1
  mov.l   @r1, r1
  tst     r1, r1
  bt      1f
  bra     return_to_main
  mov     r13, r0
1:
  jmp     @r0
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
c_mask15:
  .long   0x00007FFF
sb_flag:
  .long   _cgba_dynarec_single_block

#if defined(__linux__) && defined(__ELF__)
  .section .note.GNU-stack,"",%progbits
#endif
