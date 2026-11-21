#ifndef CGBA_SH4_THUMB_BLOCK_EMIT_H
#define CGBA_SH4_THUMB_BLOCK_EMIT_H

/*
 * Native SH-4A emission for Thumb block transfers (PUSH/POP/STMIA/LDMIA).
 *
 * The hardware profile showed Thumb block helpers dominating runtime. The hot
 * safe subset is stack/register-list transfers against plain RAM. Stores are
 * guarded by the same SMC tag mirror the helper uses and fall back before doing
 * any write if they would overwrite translated RAM code.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_fastmem.h"

extern u8 *memory_map_read[];
int  cgba_sh4_thumb_block(u32 opcode, u32 pc);
void sh4_block_exit(u32 pc);
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */
void sh4_indirect_branch_thumb(u32 address);

static inline u8 *sh4g_thumb_block_guard(u8 **tp, int slow_if_t)
{
  { sh4_codegen cg = sh4g_open(tp);
    if (slow_if_t) sh4_emit_bf(&cg, 1);       /* T==0 -> stay fast */
    else           sh4_emit_bt(&cg, 1);       /* T==1 -> stay fast */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bra_placeholder(tp);
}

static inline int sh4g_thumb_block_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
#ifndef CGBA_SH4_THUMB_BLOCK_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  return 0;
#else
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rlist = opcode & 0xFF;
  u32 base_reg, load_pc = 0, push_lr = 0, is_push = 0, is_load = 1;
  u32 count = 0, i;
  u8 *guards[8]; int ng = 0;

  if (hi == 0xB4 || hi == 0xB5) {             /* PUSH {rlist[,lr]} */
    base_reg = 13;                            /* SP */
    is_push = 1;
    is_load = 0;
    push_lr = (hi == 0xB5);
  } else if (hi == 0xBC || hi == 0xBD) {      /* POP {rlist[,pc]} */
    base_reg = 13;                            /* SP */
    load_pc = (hi == 0xBD);
  } else if (hi >= 0xC0 && hi <= 0xCF) {      /* STMIA/LDMIA Rb!,{rlist} */
    base_reg = (opcode >> 8) & 7;
    is_load = (opcode >> 11) & 1;
  } else {
    return 0;
  }

  for (i = 0; i < 8; i++)
    if (rlist & (1u << i)) count++;
  if (load_pc || push_lr) count++;
  if (count == 0) return 0;

  if (!load_pc) {
    /* All non-PC block transfers route through the resident fastmem routines
     * (sh4_fastmem.h). PUSH{..,lr} maps LR to rlist bit 14; LDMIA with the
     * base in the list keeps the loaded value (no writeback). */
    u32 rlist16 = rlist | (push_lr ? (1u << 14) : 0);
    int do_wb = !(is_load && !is_push && (rlist & (1u << base_reg)) &&
                  hi >= 0xC0);
    int off_nb = is_push ? -(int)(count * 4) : (int)(count * 4);
    int fm = (is_load ? CGBA_FMB_LDM : CGBA_FMB_STM) + (do_wb ? 2 : 0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, base_reg, SH4_REG_T0);
      if (do_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, off_nb, SH4_REG_ARG2);   /* R6 = wb base */
      }
      if (is_push)
        sh4_emit_add_imm(&cg, -(int)(count * 4), SH4_REG_T0);  /* R1 = A */
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, fm,
                          (const void *)cgba_sh4_thumb_block, (u32)opcode,
                          (u32)pc, cycle_count,
                          sh4g_fastmem_block_params(rlist16, count,
                                                    do_wb ? (int)base_reg : -1));
    return 1;
  }

  /* Inline path: POP {rlist, pc} (base_reg == SP, load from EWRAM/IWRAM). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, 13, SH4_REG_T0);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1);
    sh4g_close(tp, &cg); }

  /* Native SH4 word transfers require alignment. The C helper handles odd cases. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 3);                 /* T = word aligned */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_block_guard(tp, 0);

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
  guards[ng++] = sh4g_thumb_block_guard(tp, 0);

  sh4g_const(tp, 0x10000000u, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T2, SH4_REG_ARG1); /* T = out of GBA map */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_block_guard(tp, 1);

  /* RAM (0x02/0x03) only. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);            /* R0 = A >> 24 */
    sh4_emit_shlr(&cg, SH4_REG_RET);             /* R0 = A >> 25 */
    sh4_emit_cmpeq_imm(&cg, 1);
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_block_guard(tp, 0);

  /* R3 = memory_map_read[A >> 15]. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_mov_imm(&cg, -15, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)memory_map_read, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);     /* T = unmapped */
    sh4g_close(tp, &cg); }
  guards[ng++] = sh4g_thumb_block_guard(tp, 1);

  /* R0 = A & 0x7FFF. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);
    sh4_emit_shll16(&cg, SH4_REG_RET);
    sh4_emit_shll(&cg, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr(&cg, SH4_REG_RET);
    for (i = 0; i < 8; i++) {
      if (!(rlist & (1u << i))) continue;
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
      sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
      sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, i);
    }
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_add_imm(&cg, 4, SH4_REG_RET);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_ARG0);
    sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_mov_imm(&cg, 1, SH4_REG_ARG0);
    sh4_emit_not(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, SH4_GREG_PC);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_T1);
    sh4_emit_add_imm(&cg, (int)(count * 4), SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, 13);
    sh4g_close(tp, &cg); }

  sh4g_charge_mem_run(tp, SH4_REG_ARG1, /*seq=*/1, /*is_word=*/1, count);
  /* POP {..,pc}: a pure Thumb-mode return (ARMv4T LDM to PC does not switch
   * mode). Dispatch through the inline ROM branch-hash trampoline — a hot,
   * already-translated return target is one hash probe + JMP, with no
   * update_gba pass and no C-resolver call. The stub handles the BIOS
   * fallback, single-block diff mode, and hash misses; the cycle budget is
   * re-checked at the target block's gates/branch exits. */
  if (cycle_count)
    sh4g_cycle_debit(tp, cycle_count);
  sh4g_load_greg(tp, SH4_GREG_PC, SH4_REG_ARG0);
  sh4g_vec_jmp(tp, SH4G_VEC_ib_thumb);

  for (i = 0; i < (u32)ng; i++)
    sh4g_patch_bra(guards[i], *tp);
  sh4g_op2_tramp_call(tp, (const void *)sh4_op2_pc_mem_tramp,
                      (const void *)cgba_sh4_thumb_block, (u32)opcode, (u32)pc,
                      1, cycle_count);
  return 1;
#endif
}

#endif /* CGBA_SH4_THUMB_BLOCK_EMIT_H */
