/* gameplaySP — SH-4A (SH7305 / fx-CG100) dynarec host emitter.
 *
 * STATUS: in progress. The verified foundation is in place and host-tested:
 *   - instruction encoder           ports/fxcg100/sh4/sh4_codegen.h   (vs sh-elf-as)
 *   - literal pool + reg[] access    ports/fxcg100/sh4/sh4_emit_core.h (host audit)
 *   - Thumb data-proc translation    ports/fxcg100/sh4/sh4_thumb_mvp.h (host audit)
 *   - entry/exit trampoline          vendor/gpsp/sh4/sh4_stub.S        (assembles)
 *
 * This header is the seam included by cpu_threaded.c when SH4_ARCH is defined
 * (see cpu_threaded.c). It does NOT yet implement gpSP's full host-emitter macro
 * contract, so an SH4_ARCH build of cpu_threaded.c will not compile to
 * completion until the macros in the "REMAINING CONTRACT" checklist below are
 * filled in. The register model, primitives, and runtime symbols are settled.
 *
 * MVP model: every guest ARM register lives in reg[]; N/Z flags are materialized
 * directly in REG_CPSR (no flag caching yet); memory goes through the C
 * execute_load_* / execute_store_* helpers. Speed optimizations (resident hot
 * registers, lazy/dead-flag elimination, inline memory fast paths) layer on
 * after correctness — see docs/sh4-jit-optimization-plan.md.
 */

#ifndef SH4_EMIT_H
#define SH4_EMIT_H

#include "ports/fxcg100/sh4/sh4_emit_core.h"
#include "ports/fxcg100/sh4/sh4_thumb_mvp.h"

/* ------------------------------------------------------------------ */
/* Runtime symbols provided by sh4/sh4_stub.S and cpu.cc / cpu_threaded.c */
/* ------------------------------------------------------------------ */

/* Entry point: set up reg base + cycle counter, dispatch first block. */
u32  execute_arm_translate_internal(u32 cycles, void *reg_base);

/* Branch-into-the-VM trampolines (jumped to, not called). */
void sh4_indirect_branch_arm(u32 address);
void sh4_indirect_branch_thumb(u32 address);
void sh4_indirect_branch_dual(u32 address);

/* Cycle-exhaustion + event hook; RAM SMC flush; SWI; cheat hook. */
u32  sh4_update_gba(u32 pc);
void smc_write(void);
void execute_swi(u32 pc);
void sh4_cheat_hook(void);

/* CPSR/SPSR helpers (flags are kept live in CPSR in the MVP). */
u32  execute_read_cpsr(void);
u32  execute_read_spsr(void);
u32  execute_spsr_restore(u32 address);
void execute_store_cpsr(u32 new_cpsr, u32 store_mask);
void execute_store_spsr(u32 new_spsr, u32 store_mask);

/* Default guest-memory access handler tables (region-indexed), shared with the
 * C core; the MVP routes every access through execute_{load,store}_* directly. */
extern void *tmemld[11][16];
extern void *tmemst[4][16];

/* ------------------------------------------------------------------ */
/* Host instruction-cache sync after emitting code (SH7305).           */
/* OCBWB -> SYNCO -> ICBI per 32-byte line; see ports/.../sh4_cache.h.  */
/* ------------------------------------------------------------------ */
#ifdef CGBA_FXCG100
#include "ports/fxcg100/sh4/sh4_cache.h"
#endif

/* ================================================================== */
/* REMAINING CONTRACT — macros cpu_threaded.c requires from a host     */
/* emitter. Each maps onto the verified primitives above. (Names taken */
/* from the MIPS/x86 backends; signatures must match those.)           */
/* ================================================================== */
/*
 * Codegen cursor / cache:
 *   translation_ptr, translation_cache_limit            (file-scope, like MIPS)
 *   generate_block_prologue()                           -> prologue: cache reg_base
 *                                                          (GBR or R14), load PC
 *   block_prologue_size                                 -> exact prologue bytes
 *   generate_block_extra_vars_arm/_thumb()              -> per-block locals
 *
 * Register move / immediate / PC  (use sh4_emit_load_greg/store_greg,
 *                                  sh4_emit_load_imm32, the literal pool):
 *   generate_load_reg(ireg, reg_index)
 *   generate_store_reg(ireg, reg_index)
 *   generate_load_imm(ireg, imm)
 *   generate_load_pc(ireg, new_pc)
 *   generate_store_reg_pc_no_flags / _flags(...)
 *
 * ALU / shift / multiply  (sh4_emit_add_reg/sub/and/or/xor/not, shad/shld,
 *                          dmulu_l/dmuls_l + sts macl/mach):
 *   generate_op_<name>_reg(rd, rn, rm)  and _imm(rd, rn)
 *     names: and orr eor bic sub rsb add adc sbc rsc mov mvn
 *   arm_data_proc / arm_data_proc_test / arm_data_proc_unary
 *   arm_multiply / arm_multiply_long
 *
 * Flags  (sh4_emit_set_nz pattern; extend to C/V; honour block_data flag_data):
 *   condition codes EQ..LE  -> CMP/TST setting T, then BT/BF
 *   generate_condition(), arm_conditional_block_header()
 *   arm_dead_flag_eliminate / thumb_*_flag_* are in cpu_threaded.c (shared)
 *
 * Cycles / branch / block link:
 *   generate_cycle_update()                  -> SUB #n from SH4_REG_CYCLES (DT/ADD)
 *   generate_branch_cycle_update(s, pc)
 *   generate_branch_no_cycle_update(s, pc)
 *   generate_branch_patch_conditional/_unconditional(dest, offset)  -> BT/BF, BRA
 *   generate_indirect_branch_arm/thumb/dual(...)  -> jmp sh4_indirect_branch_*
 *   generate_translation_gate(type)
 *
 * Memory  (MVP: jsr execute_load_* and execute_store_*; later inline fast):
 *   arm_access_memory_load/_store(mem_type)
 *   arm_access_memory(...)/thumb_block_memory(...)/arm_block_memory(...)
 *
 * Instruction handlers (decode lives in cpu_threaded.c; these EMIT):
 *   thumb_*: data_proc, shift, add_sub, imm, alu_op, hireg_op, mem_*, rlist,
 *            b, bl, blh, bx, swi, process_cheats     (reuse sh4_thumb_mvp.h)
 *   arm_*:   b, bl, bx, swi, data/half/block transfers, multiply, psr, swap
 *
 * C-call ABI helper:
 *   generate_function_call(fn)  -> materialize fn via literal pool, JSR @Rn, NOP
 */

#endif /* SH4_EMIT_H */
