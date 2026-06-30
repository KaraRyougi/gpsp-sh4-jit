/* gameplaySP — SH-4A (SH7305 / fx-CG100) dynarec host emitter.
 *
 * This header is the seam cpu_threaded.c includes when SH4_ARCH is defined. It
 * implements gpSP's host-emitter macro contract for SH-4A, building on the
 * verified primitives:
 *   - instruction encoder           ports/fxcg100/sh4/sh4_codegen.h
 *   - literal pool + reg[] access   ports/fxcg100/sh4/sh4_emit_core.h
 *   - bring-up emission glue         ports/fxcg100/sh4/sh4_emit_glue.h
 *   - entry/exit trampoline          vendor/gpsp/sh4/sh4_stub.S
 *
 * BRING-UP SCOPE (correctness deferred to the interp-vs-dynarec differential
 * harness; see docs/sh4-jit-status.md): the goal is a build that compiles and
 * links and runs the right control-flow skeleton. Thumb data-processing,
 * shifts, branches, conditions, the cycle counter and block exits emit real
 * SH4. The heavier/rarer handlers — all memory and block transfers, the ARM
 * barrel-shifter data-proc, multiply(-long), PSR, SWAP, SWI and HLE divide —
 * route to C helpers (sh4_interp_helpers.c) so they are correct-by-reuse while
 * the inline emitters grow. N/Z flags are materialized in REG_CPSR; C/V are not
 * computed yet, so C/V-dependent conditions are known-wrong until flag
 * synthesis lands.
 */

#ifndef SH4_EMIT_H
#define SH4_EMIT_H

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_thumb_dp_emit.h"
#include "ports/fxcg100/sh4/sh4_thumb_block_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_ldst_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_mul_emit.h"
#include "ports/fxcg100/sh4/sh4_arm_block_emit.h"

/* ------------------------------------------------------------------ */
/* Runtime symbols (sh4/sh4_stub.S, cpu.cc, cpu_threaded.c, helpers).  */
/* ------------------------------------------------------------------ */

u32  execute_arm_translate_internal(u32 cycles, void *reg_base);

/* All guest branches/redispatch funnel through this stub entry (R4 = PC). */
void sh4_block_exit(u32 pc);
u32  sh4_update_gba(u32 pc);
void sh4_indirect_branch_arm(u32 address);
void sh4_indirect_branch_thumb(u32 address);
void sh4_indirect_branch_dual(u32 address);
void sh4_indirect_branch_dual_thumb_current(u32 address);
void sh4_bios_fallback_entry(void);
void smc_write(void);
void execute_swi(u32 pc);
void sh4_cheat_hook(void);

/* When set, the differential harness steps one block at a time: suppress direct
 * block chaining so every branch funnels through sh4_block_exit. */
extern int cgba_dynarec_single_block;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
u32 *cgba_sh4_prof_counter_for_key(u32 key);
#endif

u32  execute_read_cpsr(void);
u32  execute_read_spsr(void);
u32  execute_spsr_restore(u32 address);
void execute_store_cpsr(u32 new_cpsr, u32 store_mask);
void execute_store_spsr(u32 new_spsr, u32 store_mask);

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
int  cgba_sh4_thumb_dp(u32 opcode, u32 pc);
void cgba_sh4_thumb_shift_reg(u32 opcode, u32 pc);
void cgba_sh4_thumb_shift_imm(u32 opcode, u32 pc);

extern void *tmemld[11][16];
extern void *tmemst[4][16];

#ifdef CGBA_FXCG100
#include "ports/fxcg100/sh4/sh4_cache.h"
#endif

/* ================================================================== */
/* Host register aliases used by the macros (host SH4 register #s).    */
/* ================================================================== */
#define reg_a0   SH4_REG_ARG0   /* R4 */
#define reg_a1   SH4_REG_ARG1   /* R5 */
#define reg_a2   SH4_REG_ARG2   /* R6 */

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
#define generate_block_prologue()         do {} while(0)
#define generate_block_extra_vars_arm()
#define generate_block_extra_vars_thumb()                                      \
  u32 sh4_thumb_const_mask = 0;                                                \
  u32 sh4_thumb_const_val[16] = {0}

#define sh4_thumb_const_clear_all()                                            \
  do { sh4_thumb_const_mask = 0; } while(0)
#define sh4_thumb_const_kill(reg_index)                                        \
  do { sh4_thumb_const_mask &= ~(1u << ((reg_index) & 15)); } while(0)
#define sh4_thumb_const_set(reg_index, value)                                  \
  do { unsigned _ct_r = ((unsigned)(reg_index)) & 15u;                         \
       sh4_thumb_const_val[_ct_r] = (u32)(value);                              \
       sh4_thumb_const_mask |= (1u << _ct_r); } while(0)
#define sh4_thumb_const_copy(dst, src)                                         \
  do { unsigned _ct_d = ((unsigned)(dst)) & 15u;                               \
       unsigned _ct_s = ((unsigned)(src)) & 15u;                               \
       if(sh4_thumb_const_mask & (1u << _ct_s))                                \
         sh4_thumb_const_set(_ct_d, sh4_thumb_const_val[_ct_s]);               \
       else                                                                    \
         sh4_thumb_const_kill(_ct_d); } while(0)

static inline unsigned sh4g_thumb_dp_write_reg_index(u32 opcode)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  if (hi >= 0x20u && hi <= 0x3Fu)
    return (opcode >> 8) & 7u;
  return opcode & 7u;
}

static inline unsigned sh4g_thumb_ldst_reg_index(u32 opcode)
{
  u32 hi = (opcode >> 8) & 0xFFu;
  if ((hi >= 0x48u && hi <= 0x4Fu) || (hi >= 0x90u && hi <= 0x9Fu))
    return (opcode >> 8) & 7u;
  return opcode & 7u;
}

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
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
         sh4g_cycle_sub(&translation_ptr, (int)cycle_count, (u32)pc,          \
                        (const void *)sh4_block_exit); cycle_count = 0;       \
       } } while(0)

#define generate_cycle_update_force()                                         \
  do { sh4g_cycle_sub(&translation_ptr, (int)cycle_count, (u32)pc,            \
                      (const void *)sh4_block_exit); cycle_count = 0; } while(0)

/* Loop-break gate only (no flush) emitted AT a block-entry / loop-back target. */
#define generate_cycle_gate(is_word)                                          \
  sh4g_cycle_gate(&translation_ptr, (u32)pc, (is_word),                       \
                  (const void *)sh4_block_exit)

/* materialize an immediate / PC value into a host register */
#define generate_load_pc(hostreg, value)                                      \
  sh4g_const(&translation_ptr, (u32)(value), (hostreg))

#define generate_load_reg_pc(hostreg, reg_index, pc_offset)                   \
  do { if((reg_index) == REG_PC)                                              \
         sh4g_const(&translation_ptr, (u32)(pc + (pc_offset)), (hostreg));    \
       else                                                                   \
         sh4g_load_greg(&translation_ptr, (reg_index), (hostreg));            \
  } while(0)

/* Patch sites: unconditional jump literal, conditional BT/BF disp8. Direct
 * chaining is suppressed while the single-block harness is stepping, so each
 * branch redispatches through sh4_block_exit and can be diffed lockstep. */
#define generate_branch_patch_unconditional(dest, offset)                     \
  do { if(!cgba_dynarec_single_block)                                         \
         sh4g_patch_jump((u8 *)(dest), (const void *)(offset)); } while(0)
#define generate_branch_patch_internal(dest, offset)                          \
  sh4g_patch_jump((u8 *)(dest), (const void *)(offset))
#define generate_branch_patch_conditional(dest, offset)                       \
  sh4g_patch_cond_skip((u8 *)(dest), (const void *)(offset))

/* Re-dispatch the block at `pc` (used when a block runs off its end or hits a
 * translation gate). Flush accumulated block cycles first so run-off/gate loops
 * cannot spin without giving update_gba() a chance to retire events. */
#define generate_translation_gate(type)                                       \
  do { generate_cycle_update();                                               \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG0);                   \
       sh4g_far_jmp(&translation_ptr, (const void *)sh4_block_exit); } while(0)

/* Indirect branches (BX / computed PC) must honour the ARM/Thumb mode of the
 * target: the `dual` trampoline switches mode from the target's bit 0, and the
 * arm/thumb ones force the mode. Routing these through sh4_block_exit (which
 * dispatches on the *current* CPSR Thumb bit) would miss the BX mode switch.
 * The target PC is already in R4 (SH4_REG_ARG0). */
#define generate_indirect_branch_no_cycle_update(type)                        \
  sh4g_far_jmp(&translation_ptr, (const void *)sh4_indirect_branch_##type)
#define generate_indirect_branch_cycle_update(type)                           \
  do { generate_cycle_update();                                               \
       sh4g_far_jmp(&translation_ptr,                                         \
                    (const void *)sh4_indirect_branch_##type); } while(0)
/* Unconditional indirect branches must flush the block's accumulated cycles
 * before exiting (matching the MIPS backend); a conditional one already had its
 * cycles handled by the surrounding conditional/branch path. */
#define generate_indirect_branch_arm()                                        \
  do { if(condition == 0x0E) generate_indirect_branch_cycle_update(arm);      \
       else generate_indirect_branch_no_cycle_update(arm); } while(0)
#define generate_indirect_branch_dual()                                       \
  do { if(condition == 0x0E) generate_indirect_branch_cycle_update(dual);     \
       else generate_indirect_branch_no_cycle_update(dual); } while(0)

#define generate_branch_no_cycle_update(writeback_location, new_pc)           \
  (writeback_location) =                                                      \
    sh4g_branch_exit(&translation_ptr, (u32)(new_pc), (const void *)sh4_block_exit)

#define generate_branch_current_update(writeback_location, new_pc)            \
  do { sh4g_cycle_debit(&translation_ptr, (int)cycle_count);                  \
       cycle_count = 0;                                                       \
       generate_branch_no_cycle_update(writeback_location, new_pc); } while(0)

#define generate_branch_cycle_update(cycle_type, writeback_location, new_pc)  \
  do { cycle_count += ws_cyc_nseq[((u32)(new_pc) >> 24) & 0x0F][cycle_type];  \
       generate_branch_current_update(writeback_location, new_pc); } while(0)

/* A CONDITIONAL branch's exit sequence is emitted INSIDE the condition guard
 * (the not-taken path skips it). So it must NOT reset the translate-time
 * cycle_count: the not-taken fall-through keeps accumulating those fetch cycles
 * and a later unconditional gate flushes them. The taken path debits the cycles
 * up to the branch (a constant) plus the target-fetch refill, then exits. (An
 * unconditional branch uses generate_branch_cycle_update, which DOES zero, since
 * it always exits.) */
#define generate_branch_taken(cycle_type, writeback_location, new_pc)         \
  do { sh4g_cycle_debit(&translation_ptr, (int)cycle_count +                  \
         (int)ws_cyc_nseq[((u32)(new_pc) >> 24) & 0x0F][cycle_type]);         \
       generate_branch_no_cycle_update(writeback_location, new_pc); } while(0)

#define generate_arm_branch()                                                 \
  do {                                                                        \
    if(condition == 0x0E)                                                     \
      generate_branch_cycle_update(1,                                         \
        block_exits[block_exit_position].branch_source,                       \
        block_exits[block_exit_position].branch_target);                      \
    else                                                                      \
      generate_branch_taken(1,                                                \
        block_exits[block_exit_position].branch_source,                       \
        block_exits[block_exit_position].branch_target);                      \
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

#define arm_conditional_block_header()   generate_condition()

/* ================================================================== */
/* Data-processing op-id maps                                          */
/* ================================================================== */

#define SH4OP_and  SH4DP_AND
#define SH4OP_ands SH4DP_AND
#define SH4OP_eor  SH4DP_EOR
#define SH4OP_eors SH4DP_EOR
#define SH4OP_orr  SH4DP_ORR
#define SH4OP_orrs SH4DP_ORR
#define SH4OP_bic  SH4DP_BIC
#define SH4OP_bics SH4DP_BIC
#define SH4OP_add  SH4DP_ADD
#define SH4OP_adds SH4DP_ADD
#define SH4OP_adc  SH4DP_ADC
#define SH4OP_adcs SH4DP_ADC
#define SH4OP_sub  SH4DP_SUB
#define SH4OP_subs SH4DP_SUB
#define SH4OP_sbc  SH4DP_SBC
#define SH4OP_sbcs SH4DP_SBC
#define SH4OP_rsb  SH4DP_RSB
#define SH4OP_rsbs SH4DP_RSB
#define SH4OP_rsc  SH4DP_RSC
#define SH4OP_rscs SH4DP_RSC
#define SH4OP_mov  SH4DP_MOV
#define SH4OP_movs SH4DP_MOV
#define SH4OP_mvn  SH4DP_MVN
#define SH4OP_mvns SH4DP_MVN
#define SH4OP_cmp  SH4DP_CMP
#define SH4OP_cmn  SH4DP_CMN
#define SH4OP_tst  SH4DP_TST
#define SH4OP_teq  SH4DP_TEQ
#define SH4OP_muls SH4DP_MUL
#define SH4OP_neg  SH4DP_NEG
#define SH4OP_negs SH4DP_NEG

/* ================================================================== */
/* Thumb data-processing.                                              */
/* ================================================================== */
/*
 * Routed to the C core (cgba_sh4_thumb_dp) for full, correct N/Z/C/V and
 * carry-in. The earlier inline emitters set only N/Z and mapped ADC/SBC to
 * plain add/sub (dropping carry-in -> wrong result), so CS/CC/VS/VC and the
 * compound conditions were wrong and ADC/SBC were doubly wrong. The inline path
 * (sh4g_dp_*) remains in the glue for the eventual lazy/dead-flag synthesis the
 * optimization plan describes; for bring-up, correctness wins. The opcode fully
 * determines the operation, so the helper re-decodes and the macro tokens are
 * unused. rd is r0..r7 here (never PC); only the hi-reg forms can write PC.
 */
/* Each form first tries native SH4 emission (sh4g_thumb_dp_native, op-by-op
 * allow-list); on a 0 it falls back to the C helper, untouched. */
#define thumb_data_proc(type, name, rn_type, _rd, _rs, _rn)                   \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2(cgba_sh4_thumb_dp);                                     \
       sh4_thumb_const_kill(sh4g_thumb_dp_write_reg_index((u32)opcode)); } while(0)
#define thumb_data_proc_test(type, name, rn_type, _rs, _rn)                   \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2(cgba_sh4_thumb_dp); } while(0)
#define thumb_data_proc_unary(type, name, rn_type, _rd, _rn)                  \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2(cgba_sh4_thumb_dp);                                     \
       if(((u32)opcode & 0xF800u) == 0x2000u)                                 \
         sh4_thumb_const_set(sh4g_thumb_dp_write_reg_index((u32)opcode),      \
                             ((u32)opcode & 0xFFu));                          \
       else                                                                   \
         sh4_thumb_const_kill(sh4g_thumb_dp_write_reg_index((u32)opcode)); } while(0)

/* Hi-register ADD/MOV can target r15 -> re-dispatch when the helper returns 1.
 * The native path returns 0 for the rd==15 cases so they stay on the C path. */
#define thumb_data_proc_hi(name)                                              \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2_PC(cgba_sh4_thumb_dp);                                  \
       { unsigned _ct_rd = (((u32)opcode >> 4) & 8u) | ((u32)opcode & 7u);     \
         if(_ct_rd == REG_PC) sh4_thumb_const_clear_all();                    \
         else sh4_thumb_const_kill(_ct_rd); } } while(0)
#define thumb_data_proc_test_hi(name)                                         \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2(cgba_sh4_thumb_dp); } while(0)
#define thumb_data_proc_mov_hi()                                              \
  do { if(!sh4g_thumb_dp_native(&translation_ptr, (u32)opcode, (u32)pc, (u32)flag_status))      \
         SH4_CALL_OP2_PC(cgba_sh4_thumb_dp);                                  \
       { unsigned _ct_rs = ((u32)opcode >> 3) & 0x0Fu;                        \
         unsigned _ct_rd = (((u32)opcode >> 4) & 8u) | ((u32)opcode & 7u);     \
         if(_ct_rd == REG_PC) sh4_thumb_const_clear_all();                    \
         else sh4_thumb_const_copy(_ct_rd, _ct_rs); } } while(0)

/* ================================================================== */
/* Thumb shifts. Immediate shifts are exact native N/Z/C; register shifts keep */
/* the C helper because full ARM amount>=32/ROR carry semantics are branchy.   */
/* ================================================================== */

#define SH4_THUMB_SHIFT_imm(decode_type, op_type)                             \
  do { u32 _sh_op = ((u32)opcode >> 11) & 3;                                  \
       if (_sh_op <= 2)                                                        \
         sh4g_shift_imm(&translation_ptr, (int)_sh_op,                         \
                        (unsigned)((u32)opcode & 7),                           \
                        (unsigned)(((u32)opcode >> 3) & 7),                    \
                        (unsigned)(((u32)opcode >> 6) & 0x1F), 1);             \
       else                                                                    \
         SH4_CALL_OP2(cgba_sh4_thumb_shift_imm);                               \
       sh4_thumb_const_kill((u32)opcode & 7u);                                  \
  } while(0)

#define SH4_THUMB_SHIFT_reg(decode_type, op_type)                             \
  do { SH4_CALL_OP2(cgba_sh4_thumb_shift_reg);                                \
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
       sh4g_load_greg(&translation_ptr, REG_SP, SH4_REG_T0);                  \
       sh4g_const(&translation_ptr, (u32)(imm * 4), SH4_REG_T1);              \
       sh4g_add_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0);                \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, (_rd));                  \
       sh4_thumb_const_kill(_rd); } while(0)

#define thumb_adjust_sp_up()                                                  \
  sh4g_add_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0)
#define thumb_adjust_sp_down()                                                \
  sh4g_sub_reg(&translation_ptr, SH4_REG_T1, SH4_REG_T0)

#define thumb_adjust_sp(direction)                                            \
  do { thumb_decode_add_sp();                                                 \
       sh4g_load_greg(&translation_ptr, REG_SP, SH4_REG_T0);                  \
       sh4g_const(&translation_ptr, (u32)(imm * 4), SH4_REG_T1);              \
       thumb_adjust_sp_##direction();                                         \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_SP);                 \
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
       generate_indirect_branch_dual(); } while(0)

#define arm_swi()                                                             \
  do { sh4g_const(&translation_ptr, (u32)(pc + 4), SH4_REG_ARG0);             \
       sh4g_far_call(&translation_ptr, (const void *)execute_swi);            \
       generate_branch_current_update(                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       block_exit_position++; } while(0)

#define thumb_b()                                                             \
  do { generate_branch_cycle_update(0,                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       block_exit_position++; } while(0)

#define thumb_bl()                                                            \
  do { sh4g_const(&translation_ptr, (u32)((pc + 2) | 0x01), SH4_REG_T0);      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, (u32)((pc + 2) | 0x01));                   \
       generate_branch_cycle_update(0,                                        \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       block_exit_position++; } while(0)

#define thumb_bl_prefix()                                                     \
  do { thumb_decode_branch();                                                 \
       u32 _ct_lr = (u32)(pc + 4 + ((s32)((offset & 0x07FF) << 21) >> 9));    \
       sh4g_const(&translation_ptr, _ct_lr, SH4_REG_T0);                      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, _ct_lr); } while(0)

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
 * thumb_blh() adds the low offset to. Normally prefix+suffix fold into one
 * thumb_bl() in the same block and the prefix could emit nothing, BUT the dynarec
 * can take a cycle-budget exit BETWEEN the two halves and re-dispatch at the
 * suffix; that suffix is then a separate thumb_blh() block which would read a
 * STALE LR and branch wild. So always set LR here (combined BLs overwrite it in
 * thumb_bl()). Emit unconditionally — gating on "block ends at the prefix" misses
 * the far more common mid-BL cycle exit. */
#define thumb_bl_prefix()                                                     \
  do { thumb_decode_branch();                                                 \
       u32 _ct_lr = (u32)(pc + 4 + ((s32)((offset & 0x07FF) << 21) >> 9));    \
       sh4g_const(&translation_ptr, _ct_lr, SH4_REG_T0);                      \
       sh4g_store_greg(&translation_ptr, SH4_REG_T0, REG_LR);                 \
       sh4_thumb_const_set(REG_LR, _ct_lr); } while(0)

#define thumb_bx()                                                            \
  do { thumb_decode_hireg_op();                                               \
       generate_load_reg_pc(SH4_REG_ARG0, rs, 4);                            \
       cycle_count -= ws_cyc_seq[(pc >> 24) & 0x0F][0];                      \
       sh4g_charge_thumb_bx_target_fetch(&translation_ptr, SH4_REG_ARG0);     \
       generate_cycle_update();                                               \
       sh4_thumb_const_clear_all();                                           \
       sh4g_thumb_bx_dispatch(&translation_ptr,                               \
         (const void *)sh4_indirect_branch_dual_thumb_current,                \
         (const void *)sh4_indirect_branch_dual); } while(0)

#define thumb_conditional_branch(condition)                                   \
  do { generate_condition_##condition();                                      \
       generate_branch_taken(0,                                               \
         block_exits[block_exit_position].branch_source,                      \
         block_exits[block_exit_position].branch_target);                     \
       generate_branch_patch_conditional(backpatch_address, translation_ptr); \
       cycle_count += ws_cyc_nseq[((u32)(pc + 2) >> 24) & 0x0F][0];          \
       block_exit_position++; } while(0)

#define thumb_swi()                                                           \
  do { sh4g_const(&translation_ptr, (u32)(pc + 2), SH4_REG_ARG0);             \
       sh4g_far_call(&translation_ptr, (const void *)execute_swi);            \
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
  do { sh4g_far_call(&translation_ptr, (const void *)sh4_cheat_hook);         \
       sh4_thumb_const_clear_all(); } while(0)
#define arm_process_cheats()                                                  \
  sh4g_far_call(&translation_ptr, (const void *)sh4_cheat_hook)

/* ================================================================== */
/* C-helper handlers (bring-up): pass opcode in R4, pc in R5, JSR.     */
/* ================================================================== */

#define SH4_CALL_OP2(fn)                                                      \
  do { sh4g_const(&translation_ptr, (u32)opcode, SH4_REG_ARG0);               \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG1);                   \
       sh4g_far_call(&translation_ptr, (const void *)(fn)); } while(0)

#define SH4_CALL_OP2_MEM(fn)                                                  \
  do { SH4_CALL_OP2(fn);                                                      \
       sh4g_cycle_debit_from_global(&translation_ptr,                         \
                                    &cgba_sh4_extra_cycles); } while(0)

/* Same, but the handler returns 1 in R0 when it changed the PC -> redispatch. */
#define SH4_CALL_OP2_PC(fn)                                                   \
  do { SH4_CALL_OP2(fn);                                                      \
       sh4g_redispatch_if_r0_debit(&translation_ptr, (int)cycle_count,        \
                                   (const void *)sh4_block_exit);             \
  } while(0)

#define SH4_CALL_OP2_PC_MEM(fn)                                               \
  do { SH4_CALL_OP2(fn);                                                      \
       sh4g_cycle_debit_from_global(&translation_ptr,                         \
                                    &cgba_sh4_extra_cycles);                  \
       sh4g_redispatch_if_r0_debit(&translation_ptr, (int)cycle_count,        \
                                   (const void *)sh4_block_exit);             \
  } while(0)

/* Memory (single + block transfers). */
#define thumb_access_memory(access_type, op_type, reg_rd, reg_rb, reg_ro,     \
                            address_type, offset, mem_type)                   \
  do { if(!sh4g_thumb_ldst_const_native(&translation_ptr, (u32)opcode,        \
                                        sh4_thumb_const_mask,                 \
                                        sh4_thumb_const_val) &&               \
         !sh4g_thumb_ldst_native(&translation_ptr, (u32)opcode, (u32)pc,    \
                                  (int)cycle_count))                          \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_thumb_ldst);                            \
       sh4_thumb_const_kill(sh4g_thumb_ldst_reg_index((u32)opcode)); } while(0)
#define thumb_block_memory(access_type, pre_op, post_op, base_reg)            \
  do { if(!sh4g_thumb_block_native(&translation_ptr, (u32)opcode, (u32)pc,    \
                                   (int)cycle_count))                         \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_thumb_block);                           \
       sh4_thumb_const_clear_all(); } while(0)
#define arm_access_memory(access_type, direction, adjust_op, mem_type,        \
                          offset_type)                                        \
  do { if(!sh4g_arm_ldst_native(&translation_ptr, (u32)opcode, (u32)pc,       \
                                (int)cycle_count))                            \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_arm_ldst); } while(0)
#define arm_block_memory(access_type, offset_type, writeback_type, s_bit)     \
  do { if(!sh4g_arm_block_native(&translation_ptr, (u32)opcode, (u32)pc,      \
                                 (int)cycle_count))                           \
         SH4_CALL_OP2_PC_MEM(cgba_sh4_arm_block); } while(0)

/* ARM data-processing: try native (immediate-operand forms), else the C core. */
#define arm_data_proc(name, type, flags_op)                                   \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc))         \
         SH4_CALL_OP2_PC(cgba_sh4_arm_dp); } while(0)
#define arm_data_proc_test(name, type)                                        \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc))         \
         SH4_CALL_OP2(cgba_sh4_arm_dp); } while(0)
#define arm_data_proc_unary(name, type, flags_op)                             \
  do { if(!sh4g_arm_dp_native(&translation_ptr, (u32)opcode, (u32)pc))         \
         SH4_CALL_OP2_PC(cgba_sh4_arm_dp); } while(0)
#define arm_multiply(add_op, flags)                                          \
  do { if(!sh4g_arm_multiply_native(&translation_ptr, (u32)opcode))           \
         SH4_CALL_OP2(cgba_sh4_arm_multiply); } while(0)
#define arm_multiply_long(name, add_op, flags)                                \
  do { if(!sh4g_arm_multiply_long_native(&translation_ptr, (u32)opcode))      \
         SH4_CALL_OP2(cgba_sh4_arm_multiply_long); } while(0)
#define arm_psr(op_type, transfer_type, psr_reg)   SH4_CALL_OP2_PC(cgba_sh4_arm_psr)
#define arm_swap(type)                             SH4_CALL_OP2_PC_MEM(cgba_sh4_arm_swap)

#define arm_hle_div(cpu_mode)                                                 \
  do { sh4g_const(&translation_ptr, 0u, SH4_REG_ARG0);                        \
       sh4g_const(&translation_ptr, (u32)pc, SH4_REG_ARG1);                   \
       sh4g_far_call(&translation_ptr, (const void *)cgba_sh4_hle_div); } while(0)
#define arm_hle_div_arm(cpu_mode)                  arm_hle_div(cpu_mode)

#endif /* SH4_EMIT_H */
