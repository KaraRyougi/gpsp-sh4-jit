#ifndef CGBA_SH4_ARM_BLOCK_EMIT_H
#define CGBA_SH4_ARM_BLOCK_EMIT_H

/*
 * Native SH-4A emission for ARM block transfers (LDM/STM). The register list is
 * a translate-time constant, so load transfers UNROLL into a straight-line run
 * of fast-path word reads instead of the C helper's popcount loop +
 * per-word execute_load region switch.
 *
 * LDM (load) reads through any mapped host page (EWRAM/IWRAM/IO/VRAM/paged-ROM,
 * memory_map_read non-NULL, region != 0). STM (store) stays on the C helper
 * path so write-side effects and SMC invalidation stay centralized.
 *
 * Address model (matches the oracle exactly): lowest-numbered register <-> lowest
 * address; the run is `count` contiguous ascending words from A = (base & ~3) +
 * offset, offset = IA:0 IB:+4 DB:-count*4 DA:-count*4+4. Writeback stores
 * new_base = base +/- count*4 (from the UNALIGNED base), suppressed for LDM when
 * the base is in the list.
 *
 * The whole run is guarded into ONE mapped host page (single resolve covers every
 * word); a straddle / wrong-region / unmapped run falls to the C helper. Guards
 * use a short skip over a far BRA-to-slow (sh4g_patch_cond truncates disp8, so a
 * long conditional branch over a count-sized fast path would be unsafe).
 *
 * Bails to C: r15 in the list (PC redispatch / ^ SPSR), the S bit (user-bank),
 * rn==15, empty list, base-in-list writeback for STM, any straddle/unmapped run.
 */

#include "ports/fxcg100/sh4/sh4_emit_glue.h"
#include "ports/fxcg100/sh4/sh4_fastmem.h"

extern u8 *memory_map_read[];                 /* gba_memory.c host-page table */
int  cgba_sh4_arm_block(u32 opcode, u32 pc);  /* C oracle / slow path */
void sh4_block_exit(u32 pc);                  /* redispatch entry (sh4_emit.h) */
void sh4_helper_exit(u32 pc);
void sh4_op2_pc_mem_tramp(void);   /* compact slow-path call (sh4_stub.S) */

/* Emit "branch to the slow path if (T == slow_if_t)" with no disp8 range limit:
 * a short skip over a far BRA placeholder. Returns the BRA site to patch later. */
static inline u8 *sh4g_block_guard(u8 **tp, int slow_if_t)
{
  { sh4_codegen cg = sh4g_open(tp);
    if (slow_if_t) sh4_emit_bf(&cg, 1);       /* T==0 -> skip BRA (stay fast)   */
    else           sh4_emit_bt(&cg, 1);       /* T==1 -> skip BRA               */
    sh4g_close(tp, &cg); }
  return sh4g_emit_bra_placeholder(tp);       /* BRA 0 + NOP delay -> slow      */
}

/* Native ARM LDM/STM, or 0 to fall back to C. */
static inline int sh4g_arm_block_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
  u32 rn        = (opcode >> 16) & 0xF;
  u32 rlist     = opcode & 0xFFFF;
  u32 is_load   = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre       = (opcode >> 24) & 1;
  u32 up        = (opcode >> 23) & 1;
  u32 s_bit     = (opcode >> 22) & 1;
  u32 count = 0, i;
  int offset_a, offset_nb, do_wb;
  int fm;

  if (s_bit)                 return 0;         /* user-bank / SPSR restore -> C */
  if (rlist & 0x8000)        return 0;         /* PC in list -> C (redispatch)  */
  if (rn == 15)              return 0;         /* base = PC -> C */

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) count++;
  if (count == 0)            return 0;         /* empty list -> C (rare) */
  /* STM is native for plain RAM: region-guarded to EWRAM/IWRAM, every
   * destination tag word scanned BEFORE any store (SMC falls back with no
   * partial writes), byteswapped word stores. Dense-gameplay profiling showed
   * STM-store helpers as the single hottest JIT cost (3.94M calls/session). */
  if (!is_load && writeback && (rlist & (1u << rn)))
    return 0;                                  /* store-base value is subtle */

  offset_a  = up ? (pre ? 4 : 0) : (pre ? -(int)(count * 4) : -(int)(count * 4) + 4);
  offset_nb = up ? (int)(count * 4) : -(int)(count * 4);
  do_wb = writeback && !(rlist & (1u << rn));
  fm = (is_load ? CGBA_FMB_LDM : CGBA_FMB_STM) + (do_wb ? 2 : 0);

  if (count >= CGBA_SH4_FASTMEM_BLOCK_MIN &&
      !((pc >> 24) == 0x03 && count <= 10)) {
    /* Out-of-line fast path (sh4_fastmem.h): the shared runtime-rlist block
     * routine replaces the unrolled inline body — the last inline-fat site
     * class behind the formal-gameplay translation thrash. Sites: A calc +
     * a 36-byte tuple call. Small lists stay unrolled below (faster, and
     * already small). */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      if (do_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, offset_nb, SH4_REG_ARG2);  /* R6 = wb base */
      }
      sh4_emit_shlr2(&cg, SH4_REG_T0);                   /* R1 = A */
      sh4_emit_shll2(&cg, SH4_REG_T0);
      if (offset_a != 0)
        sh4_emit_add_imm(&cg, offset_a, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, fm,
                          (const void *)cgba_sh4_arm_block, (u32)opcode,
                          (u32)pc, cycle_count,
                          sh4g_fastmem_block_params(rlist, count,
                                                    do_wb ? (int)rn : -1));
    return 1;
  }

  /* Reserve literal-pool headroom so the inline IWRAM body + fastmem call
   * cannot push an earlier MOV.L literal reference past +1020 bytes. */
  sh4g_litseg_reserve_bytes(tp, 6, 260u);

  /* R1 = A = (base & ~3) + offset_a; R6 = base + offset_nb (if do_wb).
   * Matches the exact entry contract of FMB_LDM/FMB_STM so non-IWRAM falls
   * straight into the compact out-of-line fastmem routine without duplicating
   * a 180-byte generic memory_map_read[] fallback at every site. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    if (do_wb) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      sh4_emit_add_imm(&cg, offset_nb, SH4_REG_ARG2);    /* R6 = wb base */
    }
    sh4_emit_shlr2(&cg, SH4_REG_T0);                     /* word-align base */
    sh4_emit_shll2(&cg, SH4_REG_T0);
    if (offset_a != 0)
      sh4_emit_add_imm(&cg, offset_a, SH4_REG_T0);       /* R1 = A */
    sh4g_close(tp, &cg); }

  /* ---- Direct IWRAM (region 3) inline fast path ---- */
  {
    u8 *not_iwram, *iw_miss[2], *bra_iw_done;
    int niw = 0;

    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_shlr16(&cg, SH4_REG_RET);
      sh4_emit_shlr8(&cg, SH4_REG_RET);
      sh4_emit_cmpeq_imm(&cg, 3);                        /* T = IWRAM (region 3) */
      sh4g_close(tp, &cg); }
    not_iwram = sh4g_emit_bf_placeholder(tp);

    /* R0 = A & 0x7FFF; single-page straddle check when count > 1 (a single
     * 4-byte aligned word in 0..0x7FFC cannot cross 0x8000). */
    sh4g_const(tp, 0x7FFFu, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
      sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);        /* R0 = A & 0x7FFF */
      if (count > 1) {
        sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T2);
        sh4_emit_add_imm(&cg, (int)(count * 4) - 1, SH4_REG_T2);
        sh4_emit_shll16(&cg, SH4_REG_T2);
        sh4_emit_cmppz(&cg, SH4_REG_T2);                 /* T = stays in 32 KiB */
      }
      sh4g_close(tp, &cg); }
    if (count > 1)
      iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);

    sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
    sh4g_add_reg(tp, SH4_REG_RET, SH4_REG_T2);           /* R3 = host_ptr (4-aligned) */

    if (!is_load) {
      /* IWRAM SMC tags live directly at iwram + off (use R5 to preserve R6). */
      sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG1);
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG1);
        sh4_emit_mov_l_load(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
        for (i = 1; i < count; i++) {
          sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_T1, i);
          sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
        }
        sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);   /* T = all tags clear */
        sh4g_close(tp, &cg); }
      iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);
    }

    { sh4_codegen cg = sh4g_open(tp);
      u32 idx = 0;
      for (i = 0; i < 16; i++) {
        if (!(rlist & (1u << i))) continue;
        if (is_load) {
          sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, idx);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_store_greg(&cg, SH4_REG_T1, i);
        } else {
          sh4_emit_load_greg(&cg, i, SH4_REG_T1);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, idx);
        }
        idx++;
      }
      if (do_wb)
        sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
      sh4g_close(tp, &cg); }
    sh4g_cycle_debit(tp, (int)count);                    /* IWRAM seq word = 1/word */
    bra_iw_done = sh4g_emit_bra_placeholder(tp);

    /* Non-IWRAM (EWRAM/VRAM/ROM) or IWRAM straddle/SMC -> shared ILRAM fastmem
     * routine (which handles EWRAM/VRAM/ROM natively and falls back to C). */
    while (niw)
      sh4g_patch_cond(iw_miss[--niw], *tp);
    sh4g_patch_cond(not_iwram, *tp);
    sh4g_fastmem_site_raw(tp, fm,
                          (const void *)cgba_sh4_arm_block, (u32)opcode,
                          (u32)pc, cycle_count,
                          sh4g_fastmem_block_params(rlist, count,
                                                    do_wb ? (int)rn : -1));
    sh4g_patch_bra(bra_iw_done, *tp);
  }
  return 1;
}

#ifdef CGBA_SH4_ARM_MUL_EMIT_H
/* Fuse the general 4-instruction ARMv4 IWRAM in-place block RMW idiom:
 *   op0: LDMIA rn, {rA, rB}       (2 regs, no wb, rn not in list)
 *   op1: MLA   rd1, rm1, rs1, rd1 (unconditional, non-S, rd1 in {rA, rB})
 *   op2: MLA   rd2, rm2, rs2, rd2 (unconditional, non-S, rd2 = other of {rA, rB})
 *   op3: STMIA rn{!}, {rA, rB}    (same rn and {rA, rB}, with or without wb)
 * where neither MLA reads rA, rB, or rn as a multiplicand.
 *
 * On the inline IWRAM fast path (aligned, non-straddling, SMC tags clear),
 * address decoding, region/straddle/SMC checks, and shared multiplicand loads
 * happen ONCE and the two accumulators stay in registers across Load->MLA->Store
 * (35 fewer instructions and 11 fewer data accesses than emitting 4 separate ops).
 * Any miss falls cleanly through to the 4 individual exact operations. */
static inline int sh4g_arm_rmw_block_mla_native(u8 **tp,
  u32 op0, u32 op1, u32 op2, u32 op3, u32 pc, int cc0, int cc3)
{
  u32 rn, rlist, rA = 16, rB = 16, i;
  u32 rd1, acc1, rs1, rm1, rd2, acc2, rs2, rm2;
  u32 mA, sA, mB, sB, do_wb;
  u8 *not_iwram, *iw_miss[2], *bra_done;
  int niw = 0;

  if ((pc >> 24) != 0x03)                      return 0;
  if ((op0 & 0xFFF00000u) != 0xE8900000u)      return 0; /* LDMIA rn, rlist */
  rn = (op0 >> 16) & 0xFu;
  rlist = op0 & 0xFFFFu;
  if (rn == 15u || (rlist & 0x8000u))          return 0;
  if (rlist & (1u << rn))                      return 0;
  for (i = 0; i < 15u; i++) {
    if (rlist & (1u << i)) {
      if (rA == 16u)      rA = i;
      else if (rB == 16u) rB = i;
      else                return 0;            /* > 2 regs */
    }
  }
  if (rB == 16u)                               return 0; /* < 2 regs */

  if ((op1 & 0xFFF000F0u) != 0xE0200090u)      return 0; /* MLA cond=AL, S=0 */
  if ((op2 & 0xFFF000F0u) != 0xE0200090u)      return 0; /* MLA cond=AL, S=0 */
  rd1 = (op1 >> 16) & 0xFu; acc1 = (op1 >> 12) & 0xFu;
  rs1 = (op1 >> 8)  & 0xFu; rm1  = op1 & 0xFu;
  rd2 = (op2 >> 16) & 0xFu; acc2 = (op2 >> 12) & 0xFu;
  rs2 = (op2 >> 8)  & 0xFu; rm2  = op2 & 0xFu;
  if (rd1 != acc1 || rd2 != acc2)              return 0;
  if (!((rd1 == rA && rd2 == rB) || (rd1 == rB && rd2 == rA)))
    return 0;
  if (rm1 == 15u || rs1 == 15u || rm2 == 15u || rs2 == 15u) return 0;
  if (rm1 == rA || rm1 == rB || rm1 == rn)     return 0;
  if (rs1 == rA || rs1 == rB || rs1 == rn)     return 0;
  if (rm2 == rA || rm2 == rB || rm2 == rn)     return 0;
  if (rs2 == rA || rs2 == rB || rs2 == rn)     return 0;

  if ((op3 & 0xFFD00000u) != 0xE8800000u)      return 0; /* STMIA rn{!}, rlist */
  if (((op3 >> 16) & 0xFu) != rn)              return 0;
  if ((op3 & 0xFFFFu) != rlist)                return 0;
  do_wb = (op3 >> 21) & 1u;

  if (rd1 == rA) { mA = rm1; sA = rs1; mB = rm2; sB = rs2; }
  else           { mA = rm2; sA = rs2; mB = rm1; sB = rs1; }
  /* Align any shared multiplicand into mA == mB so it loads once into R4. */
  if (mA != mB) {
    if (mA == sB)      { u32 t = mB; mB = sB; sB = t; }
    else if (sA == mB) { u32 t = mA; mA = sA; sA = t; }
    else if (sA == sB) { u32 t1 = mA; mA = sA; sA = t1;
                         u32 t2 = mB; mB = sB; sB = t2; }
  }

  sh4g_litseg_reserve_bytes(tp, 8, 280u);

  /* R1 = A = base & ~3; R6 = base + 8 (unaligned writeback, if do_wb). */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    if (do_wb) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      sh4_emit_add_imm(&cg, 8, SH4_REG_ARG2);
    }
    sh4_emit_shlr2(&cg, SH4_REG_T0);
    sh4_emit_shll2(&cg, SH4_REG_T0);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 3);                /* T = IWRAM (region 3) */
    sh4g_close(tp, &cg); }
  not_iwram = sh4g_emit_bf_placeholder(tp);

  /* R0 = A & 0x7FFF; straddle check for 2 words (8 bytes). */
  sh4g_const(tp, 0x7FFFu, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET); /* R0 = off */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T2);
    sh4_emit_add_imm(&cg, 7, SH4_REG_T2);
    sh4_emit_shll16(&cg, SH4_REG_T2);
    sh4_emit_cmppz(&cg, SH4_REG_T2);           /* T = stays in 32 KiB */
    sh4g_close(tp, &cg); }
  iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);

  /* IWRAM SMC tags check BEFORE modifying any guest state. */
  sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG1);
    sh4_emit_mov_l_load(&cg, SH4_REG_ARG1, SH4_REG_ARG0);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG1, SH4_REG_T1, 1);
    sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
    sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0); /* T = both tag words clear */
    sh4g_close(tp, &cg); }
  iw_miss[niw++] = sh4g_emit_bf_placeholder(tp);

  /* R3 = iwram_data + off; execute Load -> MLA -> Store in registers! */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2); /* R3 = host_ptr */
    sh4_emit_load_greg(&cg, mA, SH4_REG_ARG0);      /* R4 = reg[mA] */

    /* Word 0 (rA): load, mul, byteswap (hides MUL latency), add, store greg & RAM */
    sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_load_greg(&cg, sA, SH4_REG_ARG1);      /* R5 = reg[sA] */
    sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_ARG1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sts_macl(&cg, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rA);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);

    /* Word 1 (rB): reuse R4 if mB == mA! */
    if (mB != mA)
      sh4_emit_load_greg(&cg, mB, SH4_REG_ARG0);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, 1);
    sh4_emit_load_greg(&cg, sB, SH4_REG_ARG1);
    sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_ARG1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sts_macl(&cg, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rB);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, 1);

    if (do_wb)
      sh4_emit_store_greg(&cg, SH4_REG_ARG2, rn);
    sh4g_close(tp, &cg); }
  sh4g_cycle_debit(tp, 4);                     /* 2 (LDMIA) + 2 (STMIA) */
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* Miss path (non-IWRAM / straddle / SMC tag set): exact 4-op fallback.
   * R1 still holds A = base & ~3 for FMB_LDM! */
  while (niw)
    sh4g_patch_cond(iw_miss[--niw], *tp);
  sh4g_patch_cond(not_iwram, *tp);
  sh4g_fastmem_site_raw(tp, CGBA_FMB_LDM,
                        (const void *)cgba_sh4_arm_block, op0, pc, cc0,
                        sh4g_fastmem_block_params(rlist, 2, -1));
  sh4_greg_forward_reset();
  sh4g_arm_multiply_native(tp, op1, 0);
  sh4_greg_forward_reset();
  sh4g_arm_multiply_native(tp, op2, 0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
    if (do_wb) {
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      sh4_emit_add_imm(&cg, 8, SH4_REG_ARG2);
    }
    sh4_emit_shlr2(&cg, SH4_REG_T0);
    sh4_emit_shll2(&cg, SH4_REG_T0);
    sh4g_close(tp, &cg); }
  sh4g_fastmem_site_raw(tp, CGBA_FMB_STM + (do_wb ? 2 : 0),
                        (const void *)cgba_sh4_arm_block, op3, pc + 12, cc3,
                        sh4g_fastmem_block_params(rlist, 2, do_wb ? (int)rn : -1));
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

#ifdef CGBA_SH4_ARM_LDST_NATIVE
/* Fuse the general 10-instruction ARMv4 IWRAM 4-word block RMW + 2-sample
 * byte-stream MAC idiom:
 *   op[0]: LDMIA rn, {rA, rB, rC, rD}
 *   op[1]: LDRSB/LDRB rTmp, [rS, rI]
 *   op[2]: MLA rd1, rTmp, rs1, rd1
 *   op[3]: MLA rd2, rTmp, rs2, rd2
 *   op[4]: ADD rI, rI, #1
 *   op[5]: LDRSB/LDRB rTmp, [rS, rI]    (same as op[1])
 *   op[6]: MLA rd3, rTmp, rs3, rd3
 *   op[7]: MLA rd4, rTmp, rs4, rd4
 *   op[8]: ADD rI, rI, #1               (same as op[4])
 *   op[9]: STMIA rn{!}, {rA, rB, rC, rD}
 *
 * Checks ROM8 + same-page (off+1 < 0x1000) + IWRAM + 4-word straddle +
 * 4 SMC tag words BEFORE touching any guest state, then executes both byte
 * loads and all 4 word Load->MLA->Store updates in registers (~91 vs ~174 insns).
 * Any guard failure falls cleanly to the 10 individual exact operations. */
static inline int sh4g_arm_rmw_block4_ldrsb_mla_native(u8 **tp, const u32 *ops,
  const int *ccs, int reps, u32 pc)
{
  u32 rn, rlist, regs[4], nreg = 0, i;
  u32 rS, rI, rTmp, is_signed, do_wb;
  u32 mla_rd[4], mla_rs[4], word_rs[4];
  unsigned word_samp[8];
  u32 rom8_limit;
  u8 *gmiss[6], *hot_commit, *miss_tramp, *bra_done;
  int ng = 0, r;

  if ((pc >> 24) != 0x03 || reps < 1 || reps > 2) return 0;
  if (cgba_gamepak_fragment_blocks == NULL || gamepak_size <= 0x8000u) return 0;
  if (reps == 2) {
    for (i = 0; i < 10u; i++) {
      if (ops[10 + i] != ops[i]) return 0;
    }
  }
  if ((ops[0] & 0xFFF00000u) != 0xE8900000u) return 0; /* LDMIA rn, rlist */
  rn = (ops[0] >> 16) & 0xFu;
  rlist = ops[0] & 0xFFFFu;
  if (rn == 15u || (rlist & 0x8000u) || (rlist & (1u << rn))) return 0;
  for (i = 0; i < 15u; i++) {
    if (rlist & (1u << i)) {
      if (nreg >= 4u) return 0;
      regs[nreg++] = i;
    }
  }
  if (nreg != 4u) return 0;

  /* op[1] and op[5]: identical LDRSB or LDRB rTmp, [rS, rI] */
  if (ops[5] != ops[1]) return 0;
  if ((ops[1] & 0xFFF00FF0u) == 0xE19000D0u)      is_signed = 1;
  else if ((ops[1] & 0xFFF00FF0u) == 0xE7D00000u) is_signed = 0;
  else return 0;
  rS   = (ops[1] >> 16) & 0xFu;
  rTmp = (ops[1] >> 12) & 0xFu;
  rI   = ops[1] & 0xFu;
  if (rS == 15u || rI == 15u || rTmp == 15u) return 0;
  if (rTmp == rn || rTmp == rS || rTmp == rI || (rlist & (1u << rTmp))) return 0;
  if (rI == rn || (rlist & (1u << rI)) || (rlist & (1u << rS))) return 0;

  /* op[4] and op[8]: ADD rI, rI, #1 */
  if (ops[4] != ops[8]) return 0;
  if ((ops[4] & 0xFFF00FFFu) != 0xE2800001u) return 0;
  if (((ops[4] >> 16) & 0xFu) != rI || ((ops[4] >> 12) & 0xFu) != rI) return 0;

  /* op[9]: STMIA rn{!}, rlist */
  if ((ops[9] & 0xFFD00000u) != 0xE8800000u) return 0;
  if (((ops[9] >> 16) & 0xFu) != rn || (ops[9] & 0xFFFFu) != rlist) return 0;
  do_wb = (ops[9] >> 21) & 1u;
  if (reps == 2 && !do_wb) return 0;

  /* op[2], op[3], op[6], op[7]: 4 MLAs updating each of regs[0..3] once using rTmp */
  {
    static const int mla_idx[4] = { 2, 3, 6, 7 };
    u32 seen = 0;
    for (i = 0; i < 4u; i++) {
      u32 mop = ops[mla_idx[i]];
      u32 rd, acc, rs, rm;
      if ((mop & 0xFFF000F0u) != 0xE0200090u) return 0;
      rd = (mop >> 16) & 0xFu; acc = (mop >> 12) & 0xFu;
      rs = (mop >> 8)  & 0xFu; rm  = mop & 0xFu;
      if (rd != acc || !(rlist & (1u << rd)) || (seen & (1u << rd))) return 0;
      if (rm == rTmp)      { /* rs is other multiplicand */ }
      else if (rs == rTmp) { rs = rm; }
      else return 0;
      if (rs == 15u || rs == rn || rs == rTmp || rs == rI || (rlist & (1u << rs)))
        return 0;
      seen |= (1u << rd);
      mla_rd[i] = rd;
      mla_rs[i] = rs;
    }
    if (seen != rlist) return 0;
    for (i = 0; i < 4u; i++) {
      u32 j;
      for (j = 0; j < 4u; j++) {
        if (mla_rd[j] == regs[i]) {
          word_rs[i] = mla_rs[j];
          word_samp[i]     = (j < 2u) ? SH4_REG_ARG1 : SH4_REG_ARG2;      /* R5=s0, R6=s1 */
          word_samp[4 + i] = (j < 2u) ? SH4_REG_ARG3 : SH4_REG_FM_REGION; /* R7=s2, R12=s3 */
          break;
        }
      }
    }
  }

  rom8_limit = (gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u;
  sh4g_litseg_reserve_bytes(tp, 10, 320u);

  /* ---- Guard 1: ROM8 + same 4 KiB page for addr0..addr0+(reps*2-1) ---- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rS, SH4_REG_T0);
    sh4_emit_load_greg(&cg, rI, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);    /* R1 = addr0 = rS + rI */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x08008000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr0 - 0x08008000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, rom8_limit, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, -12, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)(cgba_gamepak_fragment_blocks + 8), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);        /* block == NULL */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  sh4g_const(tp, 0x0FFFu, SH4_REG_RET);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_and(&cg, SH4_REG_T0, SH4_REG_RET);       /* R0 = off0 */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);   /* R2 = off0 */
    sh4_emit_add_imm(&cg, reps * 2 - 1, SH4_REG_RET); /* R0 = off0 + (1 or 3) */
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, 0x10);                      /* T = same 4 KiB page */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bf_placeholder(tp);

  /* Load s0->R5, s1->R6 (and s2->R7, s3->R12 when reps==2); no guest state modified yet. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T2);    /* R3 = host_page + off0 */
    sh4_emit_mov_b_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG1); /* R5 = s0 */
    sh4_emit_mov_b_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG2); /* R6 = s1 */
    if (!is_signed) {
      sh4_emit_extu_b(&cg, SH4_REG_ARG1, SH4_REG_ARG1);
      sh4_emit_extu_b(&cg, SH4_REG_ARG2, SH4_REG_ARG2);
    }
    if (reps == 2) {
      sh4_emit_mov_b_load_inc(&cg, SH4_REG_T2, SH4_REG_ARG3); /* R7 = s2 */
      sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_FM_REGION); /* R12 = s3 */
      if (!is_signed) {
        sh4_emit_extu_b(&cg, SH4_REG_ARG3, SH4_REG_ARG3);
        sh4_emit_extu_b(&cg, SH4_REG_FM_REGION, SH4_REG_FM_REGION);
      }
    }
    sh4g_close(tp, &cg); }

  /* ---- Guard 2: Canonical IWRAM + straddle + (reps*4) SMC tag words ---- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rn, SH4_REG_RET);         /* R0 = base */
    sh4_emit_shlr2(&cg, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);                 /* R0 = A = base & ~3 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = A - 0x03000000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(0x8000u - reps * 16u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphi(&cg, SH4_REG_T1, SH4_REG_RET);     /* T = out of canonical IWRAM / straddle */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);
    sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_ARG0);
    for (i = 1; i < (u32)(reps * 4); i++) {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, i);
      sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
    }
    sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all tags clear */
    sh4g_close(tp, &cg); }
  hot_commit = sh4g_emit_bt_placeholder(tp);

  /* Local bridge so all guard branches stay well within disp8 reach. */
  while (ng)
    sh4g_patch_cond(gmiss[--ng], *tp);
  miss_tramp = sh4g_emit_bra_placeholder(tp);

  /* ---- Fast-path commit: R0 = iwram off, R5..R12 = samples ---- */
  sh4g_patch_cond(hot_commit, *tp);
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  {
    int hoist_rs = (word_rs[0] == word_rs[2] && word_rs[1] == word_rs[3] &&
                    word_rs[0] != regs[0] && word_rs[0] != regs[1] &&
                    word_rs[0] != regs[2] && word_rs[0] != regs[3] &&
                    word_rs[1] != regs[0] && word_rs[1] != regs[1] &&
                    word_rs[1] != regs[2] && word_rs[1] != regs[3]);
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T2);   /* R3 = host_ptr */
    if (hoist_rs) {
      sh4_emit_load_greg(&cg, word_rs[0], SH4_REG_ARG0);
      sh4_emit_load_greg(&cg, word_rs[1], SH4_REG_T0);
    }
    for (r = 0; r < reps; r++) {
      int is_last = (r == reps - 1);
      for (i = 0; i < 4u; i++) {
        u32 disp = (u32)(r * 4) + i;
        unsigned rs_host;
        if (disp == 0u) sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
        else            sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, disp);
        if (hoist_rs) {
          rs_host = (i & 1u) ? SH4_REG_T0 : SH4_REG_ARG0;
        } else {
          sh4_emit_load_greg(&cg, word_rs[i], SH4_REG_ARG0);
          rs_host = SH4_REG_ARG0;
        }
        sh4_emit_mul_l(&cg, word_samp[r * 4 + i], rs_host);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_sts_macl(&cg, SH4_REG_RET);
        sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);
        if (is_last)
          sh4_emit_store_greg(&cg, SH4_REG_T1, regs[i]);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
        sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
        if (disp == 0u) sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
        else            sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, disp);
      }
    }
    /* Commit rTmp = last sample, rI += reps*2, and (if do_wb) rn += reps*16. */
    sh4_emit_store_greg(&cg, reps == 2 ? SH4_REG_FM_REGION : SH4_REG_ARG2, rTmp);
    sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
    sh4_emit_add_imm(&cg, reps * 2, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rI);
    if (do_wb) {
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4_emit_add_imm(&cg, reps * 16, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rn);
    }
    sh4g_close(tp, &cg); }
  sh4g_cycle_debit(tp, reps * (8 + 2 * (int)ws_cyc_nseq[8][0]));
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* ---- Exact fallback on any miss ---- */
  sh4g_patch_bra(miss_tramp, *tp);
  for (r = 0; r < reps; r++) {
    int bi = r * 10;
    u32 pcr = pc + (u32)(bi * 4);
    sh4_greg_forward_reset();
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      sh4_emit_shlr2(&cg, SH4_REG_T0);
      sh4_emit_shll2(&cg, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, CGBA_FMB_LDM,
                          (const void *)cgba_sh4_arm_block, ops[bi + 0], pcr, ccs[bi + 0],
                          sh4g_fastmem_block_params(rlist, 4, -1));
    sh4_greg_forward_reset();
    sh4g_arm_ldst_native(tp, ops[bi + 1], pcr + 4, ccs[bi + 1]);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + 2], 0);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + 3], 0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
      sh4_emit_add_imm(&cg, 1, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rI);
      sh4g_close(tp, &cg); }
    sh4_greg_forward_reset();
    sh4g_arm_ldst_native(tp, ops[bi + 5], pcr + 20, ccs[bi + 5]);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + 6], 0);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + 7], 0);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
      sh4_emit_add_imm(&cg, 1, SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rI);
      sh4_emit_load_greg(&cg, rn, SH4_REG_T0);
      if (do_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, 16, SH4_REG_ARG2);
      }
      sh4_emit_shlr2(&cg, SH4_REG_T0);
      sh4_emit_shll2(&cg, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, CGBA_FMB_STM + (do_wb ? 2 : 0),
                          (const void *)cgba_sh4_arm_block, ops[bi + 9], pcr + 36, ccs[bi + 9],
                          sh4g_fastmem_block_params(rlist, 4, do_wb ? (int)rn : -1));
    sh4g_litseg_maybe_flush(tp);
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

/* Fuse 1..4 consecutive identical ARMv4 IWRAM interpolated byte-stream stereo
 * RMW-MLA units:
 *   (optional op[0]: MOV rI, rP, LSR #sh)
 *   op[b+0]: LDRSB/LDRB rS, [rI, +rB]!
 *   op[b+1]: LDRSB/LDRB rT, [rI, #+1]
 *   op[b+2]: SUB rT, rT, rS
 *   op[b+3]: AND rI, rP, rM
 *   op[b+4]: MUL rT, rI, rT
 *   op[b+5]: ADD rS, rS, rT, LSR/ASR #sh2
 *   op[b+6]: LDMIA rD, {rT, rI}
 *   op[b+7]: MLA rA, rS, rV1, rA
 *   op[b+8]: MLA rB_w, rS, rV2, rB_w
 *   op[b+9]: STMIA rD{!}, {rT, rI}
 *   (optional op[11]: ADD rP, rP, rStep)
 *
 * Hoists ROM8 + same-page (all count sample pairs) + IWRAM + straddle + SMC
 * tag guards BEFORE mutating any guest state, then runs a zero-branch dt-loop
 * in registers (R3=nor_page, R5=idx0-off0, R6=iwram_dst, R7=rP, R12=count),
 * committing final architectural rS/rA/rB_w/rD/rP and combined waitstate
 * cycles once at exit. Any guard failure jumps cleanly to exact fallback. */
static inline int sh4g_arm_interp_rmw_mla_native(u8 **tp, const u32 *ops,
  const int *ccs, int count, int has_step, u32 pc,
  const u8 *unit_has_gate, const int *unit_pre_debit,
  u8 **unit_blk_off, u8 **unit_body_off)
{
  const int unit_len = has_step ? 12 : 10;
  const int base = has_step ? 1 : 0;
  u32 rI = 0, rP = 0, rS, rT, rB, rM, rD, rA, rB_w, rV1 = 0, rV2 = 0, rStep = 0;
  u32 sh = 0, sh2, sh2_type, rlist, do_wb, mod_mask, ro_mask, rom8_limit;
  int is_signed, u, j, hot_base_debit = 0, hot_total_debit, any_mid_gate = 0;
  u8 *gmiss[12], *miss_tramp, *hot_loop, *loop_head, *bra_done;
  int ng = 0;

  if ((pc >> 24) != 0x03 || count < 1 || count > 4 || (!has_step && count != 1))
    return 0;
  if (cgba_gamepak_fragment_blocks == NULL || gamepak_size <= 0x8000u)
    return 0;

  /* All repeated units must be byte-identical instructions. */
  for (u = 1; u < count; u++) {
    for (j = 0; j < unit_len; j++) {
      if (ops[u * unit_len + j] != ops[j])
        return 0;
    }
  }

  if (has_step) {
    u32 o0 = ops[0];
    if ((o0 & 0xFFFF0070u) != 0xE1A00020u) return 0; /* MOV rI, rP, LSR #sh */
    sh = (o0 >> 7) & 0x1Fu;
    if (sh == 0u) return 0;
    rI = (o0 >> 12) & 0xFu;
    rP = o0 & 0xFu;
  }

  { u32 o1 = ops[base + 0];
    u32 rn1 = (o1 >> 16) & 0xFu;
    if ((o1 & 0xFFF00FF0u) == 0xE1B000D0u)      is_signed = 1; /* LDRSB [rn,+rm]! */
    else if ((o1 & 0xFFF00FF0u) == 0xE7F00000u) is_signed = 0; /* LDRB  [rn,+rm]! */
    else return 0;
    if (has_step) { if (rn1 != rI) return 0; }
    else          { rI = rn1; }
    rS = (o1 >> 12) & 0xFu;
    rB = o1 & 0xFu; }

  { u32 o2 = ops[base + 1];
    if (is_signed) { if ((o2 & 0xFFF00FFFu) != 0xE1D000D1u) return 0; }
    else           { if ((o2 & 0xFFF00FFFu) != 0xE5D00001u) return 0; }
    if (((o2 >> 16) & 0xFu) != rI) return 0;
    rT = (o2 >> 12) & 0xFu; }

  { u32 o3 = ops[base + 2];
    if ((o3 & 0xFFF00FF0u) != 0xE0400000u ||
        ((o3 >> 12) & 0xFu) != rT || ((o3 >> 16) & 0xFu) != rT ||
        (o3 & 0xFu) != rS)
      return 0; }

  { u32 o4 = ops[base + 3];
    u32 a4 = (o4 >> 16) & 0xFu, b4 = o4 & 0xFu;
    if ((o4 & 0xFFF00FF0u) != 0xE0000000u || ((o4 >> 12) & 0xFu) != rI)
      return 0;
    if (has_step) {
      if (a4 == rP)      rM = b4;
      else if (b4 == rP) rM = a4;
      else return 0;
    } else {
      rP = a4; rM = b4;
    } }

  { u32 o5 = ops[base + 4];
    u32 rm5 = o5 & 0xFu, rs5 = (o5 >> 8) & 0xFu;
    if ((o5 & 0xFFF0F0F0u) != 0xE0000090u || ((o5 >> 16) & 0xFu) != rT)
      return 0;
    if (!((rm5 == rI && rs5 == rT) || (rm5 == rT && rs5 == rI)))
      return 0; }

  { u32 o6 = ops[base + 5];
    sh2_type = (o6 >> 5) & 3u;
    sh2 = (o6 >> 7) & 0x1Fu;
    if ((o6 & 0xFFF00010u) != 0xE0800000u ||
        ((o6 >> 12) & 0xFu) != rS || ((o6 >> 16) & 0xFu) != rS ||
        (o6 & 0xFu) != rT ||
        (sh2_type != 1u && sh2_type != 2u) || sh2 == 0u)
      return 0; }

  { u32 o7 = ops[base + 6];
    if ((o7 & 0xFFF00000u) != 0xE8900000u) return 0;
    rD = (o7 >> 16) & 0xFu;
    rlist = o7 & 0xFFFFu;
    if (rT == rI || rlist != ((1u << rT) | (1u << rI))) return 0;
    rA = (rT < rI) ? rT : rI;
    rB_w = (rT < rI) ? rI : rT; }

  { u32 mops[2] = { ops[base + 7], ops[base + 8] };
    u32 seen = 0u;
    int m;
    for (m = 0; m < 2; m++) {
      u32 mo = mops[m];
      u32 rd = (mo >> 16) & 0xFu, acc = (mo >> 12) & 0xFu;
      u32 rs = (mo >> 8) & 0xFu, rm = mo & 0xFu, rv;
      if ((mo & 0xFFF000F0u) != 0xE0200090u || rd != acc) return 0;
      if (rm == rS)      rv = rs;
      else if (rs == rS) rv = rm;
      else return 0;
      if (rd == rA)        { if (seen & 1u) return 0; seen |= 1u; rV1 = rv; }
      else if (rd == rB_w) { if (seen & 2u) return 0; seen |= 2u; rV2 = rv; }
      else return 0;
    }
    if (seen != 3u) return 0; }

  { u32 o10 = ops[base + 9];
    if ((o10 & 0xFFD00000u) != 0xE8800000u ||
        ((o10 >> 16) & 0xFu) != rD || (o10 & 0xFFFFu) != rlist)
      return 0;
    do_wb = (o10 >> 21) & 1u;
    if (count > 1 && !do_wb) return 0; }

  if (has_step) {
    u32 o11 = ops[11];
    u32 a11 = (o11 >> 16) & 0xFu, b11 = o11 & 0xFu;
    if ((o11 & 0xFFF00FF0u) != 0xE0800000u || ((o11 >> 12) & 0xFu) != rP)
      return 0;
    if (a11 == rP)      rStep = b11;
    else if (b11 == rP) rStep = a11;
    else return 0;
  }

  /* Strict register distinctness checks (no PC, no read-only aliasing). */
  if (rS >= 15u || rT >= 15u || rI >= 15u || rB >= 15u || rP >= 15u ||
      rM >= 15u || rD >= 15u || rV1 >= 15u || rV2 >= 15u ||
      (has_step && rStep >= 15u))
    return 0;
  if (rS == rT || rS == rI || rT == rI)
    return 0;
  mod_mask = (1u << rS) | (1u << rT) | (1u << rI);
  ro_mask = (1u << rB) | (1u << rM) | (1u << rV1) | (1u << rV2) |
            (has_step ? (1u << rStep) : 0u);
  if ((ro_mask & mod_mask) != 0u) return 0;
  if (((1u << rD) & mod_mask) != 0u) return 0;
  if (((1u << rP) & mod_mask) != 0u) return 0;
  if (do_wb && ((ro_mask & (1u << rD)) != 0u)) return 0;
  if (has_step && ((ro_mask & (1u << rP)) != 0u)) return 0;
  if (has_step && do_wb && rD == rP) return 0;

  for (u = 1; u < count; u++) {
    if (unit_has_gate && unit_has_gate[u]) {
      any_mid_gate = 1;
      hot_base_debit += unit_pre_debit[u];
    }
  }
  hot_total_debit = hot_base_debit + count * (2 * (int)ws_cyc_nseq[8][0] + 4);

  rom8_limit = (gamepak_size > 0x01000000u ? 0x01000000u : gamepak_size) - 0x8000u;
  sh4g_litseg_reserve_bytes(tp, 12, 340u);

  /* ---- Guard 0: cycle gate safety across intermediate unit boundaries ---- */
  if (any_mid_gate) {
    sh4g_const(tp, (u32)hot_total_debit, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_cmpgt(&cg, SH4_REG_T1, SH4_REG_CYCLES); /* T = (R13 > total) */
      sh4g_close(tp, &cg); }
    gmiss[ng++] = sh4g_emit_bf_placeholder(tp);
  }

  /* ---- Guard 1: ROM8 + same 4 KiB page for ALL count sample pairs ---- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rP, SH4_REG_ARG3);        /* R7 = rP */
    if (has_step) {
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_T0);
      sh4_emit_mov_imm(&cg, -(int)sh, SH4_REG_ARG0);
      sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_T0);   /* R1 = idx0 = rP >> sh */
    } else {
      sh4_emit_load_greg(&cg, rI, SH4_REG_T0);        /* R1 = idx0 = reg[rI] */
    }
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG1);  /* R5 = idx0 */
    sh4_emit_load_greg(&cg, rB, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);    /* R1 = addr0 = rB + idx0 */
    sh4g_close(tp, &cg); }

  sh4g_const(tp, 0x08008000u, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = addr0 - 0x08008000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, rom8_limit, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_imm(&cg, -12, SH4_REG_T1);
    sh4_emit_shld(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_shll2(&cg, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  sh4g_const(tp, (u32)(uintptr_t)(cgba_gamepak_fragment_blocks + 8), SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T2);
    sh4_emit_tst(&cg, SH4_REG_T2, SH4_REG_T2);        /* R3 == NULL? */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  sh4g_const(tp, 0x0FFFu, SH4_REG_ARG0);              /* R4 = 0x0FFF */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_and(&cg, SH4_REG_ARG0, SH4_REG_RET);     /* R0 = off0 */
    sh4_emit_cmphs(&cg, SH4_REG_ARG0, SH4_REG_RET);   /* off0 == 0x0FFF? */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bt_placeholder(tp);

  if (count > 1) {
    /* R5 = idx0 - off0; verify off_k = ((rP + k*rStep)>>sh) - R5 < 0x0FFF. */
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_ARG1);   /* R5 = idx0 - off0 */
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_T1);/* R2 = rP */
      sh4_emit_load_greg(&cg, rStep, SH4_REG_T0);     /* R1 = rStep */
      sh4_emit_mov_imm(&cg, -(int)sh, SH4_REG_ARG2);  /* R6 = -sh */
      sh4g_close(tp, &cg); }
    for (u = 1; u < count; u++) {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T1);
        sh4_emit_mov_reg(&cg, SH4_REG_T1, SH4_REG_RET);
        sh4_emit_shld(&cg, SH4_REG_ARG2, SH4_REG_RET);
        sh4_emit_sub(&cg, SH4_REG_ARG1, SH4_REG_RET);
        sh4_emit_cmphs(&cg, SH4_REG_ARG0, SH4_REG_RET);
        sh4g_close(tp, &cg); }
      gmiss[ng++] = sh4g_emit_bt_placeholder(tp);
    }
  } else {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG1); /* R5 = off0 */
    sh4g_close(tp, &cg);
  }

  /* ---- Guard 2: IWRAM + straddle + (2*count) SMC tag words ---- */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rD, SH4_REG_T0);
    sh4_emit_shlr2(&cg, SH4_REG_T0);
    sh4_emit_shll2(&cg, SH4_REG_T0);                  /* R1 = A = reg[rD] & ~3 */
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_shlr16(&cg, SH4_REG_RET);
    sh4_emit_shlr8(&cg, SH4_REG_RET);
    sh4_emit_cmpeq_imm(&cg, 3);                       /* T = IWRAM */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bf_placeholder(tp);

  sh4g_const(tp, 0x7FFFu, SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_and(&cg, SH4_REG_T1, SH4_REG_RET);       /* R0 = iw_off */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_T1);
    sh4_emit_add_imm(&cg, count * 8 - 1, SH4_REG_T1);
    sh4_emit_shll16(&cg, SH4_REG_T1);
    sh4_emit_cmppz(&cg, SH4_REG_T1);                  /* T = no 32 KiB wrap */
    sh4g_close(tp, &cg); }
  gmiss[ng++] = sh4g_emit_bf_placeholder(tp);

  sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_ARG2);
  { sh4_codegen cg = sh4g_open(tp);
    int w;
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG2);
    sh4_emit_mov_l_load(&cg, SH4_REG_ARG2, SH4_REG_ARG0);
    for (w = 1; w < count * 2; w++) {
      sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG2, SH4_REG_T1, w);
      sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
    }
    sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all SMC tags clear */
    sh4g_close(tp, &cg); }
  hot_loop = sh4g_emit_bt_placeholder(tp);            /* 99.8%: enter hot loop */

  /* Local miss bridge: keeps all gmiss[] disp8 branches well under 160 bytes. */
  while (ng)
    sh4g_patch_cond(gmiss[--ng], *tp);
  miss_tramp = sh4g_emit_bra_placeholder(tp);

  /* ---- Hot path: zero guards, zero branches except dt-loop ---- */
  sh4g_patch_cond(hot_loop, *tp);
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_ARG2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG2); /* R6 = iwram_dst */
    if (count > 1)
      sh4_emit_mov_imm(&cg, count, SH4_REG_FM_REGION);/* R12 = count */
    sh4g_close(tp, &cg); }

  loop_head = *tp;
  { sh4_codegen cg = sh4g_open(tp);
    if (count > 1) {
      sh4_emit_mov_reg(&cg, SH4_REG_ARG3, SH4_REG_RET);
      sh4_emit_mov_imm(&cg, -(int)sh, SH4_REG_ARG0);
      sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_RET);
      sh4_emit_sub(&cg, SH4_REG_ARG1, SH4_REG_RET);   /* R0 = off_k */
    } else {
      sh4_emit_mov_reg(&cg, SH4_REG_ARG1, SH4_REG_RET);/* R0 = off0 */
    }
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T0); /* R1 = s0 */
    sh4_emit_add_imm(&cg, 1, SH4_REG_RET);
    sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1); /* R2 = s1 */
    if (!is_signed) {
      sh4_emit_extu_b(&cg, SH4_REG_T0, SH4_REG_T0);
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    }
    sh4_emit_sub(&cg, SH4_REG_T0, SH4_REG_T1);        /* R2 = s1 - s0 */
    sh4_emit_load_greg(&cg, rM, SH4_REG_ARG0);
    sh4_emit_and(&cg, SH4_REG_ARG3, SH4_REG_ARG0);    /* R4 = rP & rM */
    sh4_emit_mul_l(&cg, SH4_REG_ARG0, SH4_REG_T1);    /* MACL = frac * (s1-s0) */
    sh4_emit_mov_imm(&cg, -(int)sh2, SH4_REG_ARG0);   /* hides MUL latency */
    sh4_emit_sts_macl(&cg, SH4_REG_T1);
    if (sh2_type == 2u)
      sh4_emit_shad(&cg, SH4_REG_ARG0, SH4_REG_T1);
    else
      sh4_emit_shld(&cg, SH4_REG_ARG0, SH4_REG_T1);
    sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);    /* R1 = s */

    /* Word 0 (rA): buf[0] += s * vol1; R2 keeps final unswapped word 0. */
    sh4_emit_load_greg(&cg, rV1, SH4_REG_ARG0);
    sh4_emit_mul_l(&cg, SH4_REG_T0, SH4_REG_ARG0);
    sh4_emit_mov_l_load(&cg, SH4_REG_ARG2, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_w(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sts_macl(&cg, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);   /* R2 = new_w0 */
    sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4_emit_swap_w(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_mov_l_store(&cg, SH4_REG_RET, SH4_REG_ARG2);

    /* Word 1 (rB_w): buf[1] += s * vol2; R4 keeps final unswapped word 1. */
    sh4_emit_load_greg(&cg, rV2, SH4_REG_ARG0);
    sh4_emit_mul_l(&cg, SH4_REG_T0, SH4_REG_ARG0);
    sh4_emit_mov_l_load_disp(&cg, SH4_REG_ARG2, SH4_REG_ARG0, 1);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_w(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_ARG0);
    sh4_emit_sts_macl(&cg, SH4_REG_RET);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG0); /* R4 = new_w1 */
    sh4_emit_swap_b(&cg, SH4_REG_ARG0, SH4_REG_RET);
    sh4_emit_swap_w(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
    sh4_emit_mov_l_store_disp(&cg, SH4_REG_RET, SH4_REG_ARG2, 1);

    if (has_step) {
      sh4_emit_load_greg(&cg, rStep, SH4_REG_RET);
      sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_ARG3); /* R7 += rStep */
    }
    if (count > 1) {
      int disp8;
      sh4_emit_dt(&cg, SH4_REG_FM_REGION);
      disp8 = (int)(((long)(uintptr_t)loop_head -
                     ((long)(uintptr_t)cg.ptr + 4)) >> 1);
      sh4_emit_bf_s(&cg, disp8);
      sh4_emit_add_imm(&cg, 8, SH4_REG_ARG2);         /* delay slot: R6 += 8 */
    }

    /* Final architectural register commit (once after all count samples). */
    sh4_emit_store_greg(&cg, SH4_REG_T0, rS);
    sh4_emit_store_greg(&cg, SH4_REG_T1, rA);
    sh4_emit_store_greg(&cg, SH4_REG_ARG0, rB_w);
    if (do_wb) {
      sh4_emit_load_greg(&cg, rD, SH4_REG_RET);
      sh4_emit_add_imm(&cg, count * 8, SH4_REG_RET);
      sh4_emit_store_greg(&cg, SH4_REG_RET, rD);
    }
    if (has_step)
      sh4_emit_store_greg(&cg, SH4_REG_ARG3, rP);
    sh4g_close(tp, &cg); }

  sh4g_cycle_debit(tp, hot_total_debit);
  bra_done = sh4g_emit_bra_placeholder(tp);

  /* ---- Cold fallback: exact instruction-by-instruction emission ---- */
  sh4g_patch_bra(miss_tramp, *tp);
  for (u = 0; u < count; u++) {
    int bi = u * unit_len;
    u32 pc_u = pc + (u32)(bi * 4);
    int fm_ld = is_signed ? CGBA_FM_LOAD_SB : CGBA_FM_LOAD_B;
    sh4_greg_forward_reset();
    if (u > 0 && unit_has_gate && unit_has_gate[u]) {
      sh4g_cycle_sub(tp, unit_pre_debit[u], pc_u);
      if (unit_blk_off) unit_blk_off[u] = *tp;
      sh4g_cycle_gate(tp, pc_u, 1);
      if (unit_body_off) unit_body_off[u] = *tp;
    } else if (u > 0) {
      if (unit_blk_off) unit_blk_off[u] = *tp;
      if (unit_body_off) unit_body_off[u] = *tp;
    }
    if (has_step) {
      sh4g_arm_dp_native(tp, ops[bi + 0], pc_u, 0);
      sh4_greg_forward_reset();
    }
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
      sh4_emit_load_greg(&cg, rB, SH4_REG_T1);
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0);
      sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site(tp, fm_ld + CGBA_FM_WB, (const void *)cgba_sh4_arm_ldst,
                      ops[bi + base + 0], pc_u + (u32)(base * 4),
                      ccs[bi + base + 0], rS, (int)rI);
    sh4_greg_forward_reset();
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rI, SH4_REG_T0);
      sh4_emit_add_imm(&cg, 1, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site(tp, fm_ld, (const void *)cgba_sh4_arm_ldst,
                      ops[bi + base + 1], pc_u + (u32)((base + 1) * 4),
                      ccs[bi + base + 1], rT, -1);
    sh4_greg_forward_reset();
    sh4g_arm_dp_native(tp, ops[bi + base + 2], pc_u + (u32)((base + 2) * 4), 0);
    sh4_greg_forward_reset();
    sh4g_arm_dp_native(tp, ops[bi + base + 3], pc_u + (u32)((base + 3) * 4), 0);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + base + 4], 0);
    sh4_greg_forward_reset();
    sh4g_arm_dp_native(tp, ops[bi + base + 5], pc_u + (u32)((base + 5) * 4), 0);
    sh4_greg_forward_reset();
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rD, SH4_REG_T0);
      sh4_emit_shlr2(&cg, SH4_REG_T0);
      sh4_emit_shll2(&cg, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, CGBA_FMB_LDM, (const void *)cgba_sh4_arm_block,
                          ops[bi + base + 6], pc_u + (u32)((base + 6) * 4),
                          ccs[bi + base + 6], sh4g_fastmem_block_params(rlist, 2, -1));
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + base + 7], 0);
    sh4_greg_forward_reset();
    sh4g_arm_multiply_native(tp, ops[bi + base + 8], 0);
    sh4_greg_forward_reset();
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rD, SH4_REG_T0);
      if (do_wb) {
        sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_ARG2);
        sh4_emit_add_imm(&cg, 8, SH4_REG_ARG2);
      }
      sh4_emit_shlr2(&cg, SH4_REG_T0);
      sh4_emit_shll2(&cg, SH4_REG_T0);
      sh4g_close(tp, &cg); }
    sh4g_fastmem_site_raw(tp, CGBA_FMB_STM + (do_wb ? 2 : 0),
                          (const void *)cgba_sh4_arm_block,
                          ops[bi + base + 9], pc_u + (u32)((base + 9) * 4),
                          ccs[bi + base + 9],
                          sh4g_fastmem_block_params(rlist, 2, do_wb ? (int)rD : -1));
    sh4_greg_forward_reset();
    if (has_step) {
      sh4g_arm_dp_native(tp, ops[bi + 11], pc_u + 44, 0);
      sh4_greg_forward_reset();
    }
    sh4g_litseg_maybe_flush(tp);
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}
#endif
#endif

#endif /* CGBA_SH4_ARM_BLOCK_EMIT_H */
