/* Native SH-4 Thumb load/store, constant-address, and same-base run emitters.
 * Included via sh4_thumb_dp_emit.h. */
#ifndef CGBA_SH4_THUMB_LDST_EMIT_H
#define CGBA_SH4_THUMB_LDST_EMIT_H

enum {
  SH4_THUMB_LDK_W = 0,
  SH4_THUMB_LDK_B,
  SH4_THUMB_LDK_UH,
  SH4_THUMB_LDK_SH,
  SH4_THUMB_LDK_SB
};

static inline void sh4g_charge_mem_cell(u8 **tp, const u8 *cell)
{
  sh4g_const(tp, (uint32_t)(uintptr_t)cell, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
    sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
    sh4_emit_sub(&cg, SH4_REG_T1, SH4_REG_CYCLES);
    sh4g_close(tp, &cg); }
}

/* Fast path for the hot Thumb shape:
 *   LDR rB, [pc, #literal]   ; rB = fixed IO / IWRAM / EWRAM address
 *   LDRH/LDRB/LDR rD, [rB, #offset]
 *
 * The generic native load is still correct, but it must call into the ILRAM
 * fastmem routine, probe region/alignment, and return via JMP. When translation
 * knows the effective address is aligned static IO/IWRAM/EWRAM, the host
 * pointer is permanent and the read emits directly with zero branches. Returns
 * the fixed non-sequential cycle debit (1 for IO/IWRAM, 3 or 6 for EWRAM) so
 * the caller folds it into block cycle_count without emitting an extra ADD. */
static inline int sh4g_thumb_ldst_const_native(u8 **tp, u32 opcode,
  u32 const_mask, const u32 const_val[16])
{
#ifndef CGBA_SH4_THUMB_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)const_mask;
  (void)const_val;
  return 0;
#else
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7, rb = (opcode >> 3) & 7;
  u32 ro = 0, offset = 0, address, region;
  int reg_offset = 0, kind = SH4_THUMB_LDK_W, align_mask = 3, is_load = 1;
  uintptr_t host_addr;
  int debit;

  if (hi >= 0x50 && hi <= 0x5F) {
    u32 op = (opcode >> 10) & 3;
    ro = (opcode >> 6) & 7;
    reg_offset = 1;
    if (opcode & 0x0200) {
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_UH; align_mask = 1; is_load = 0; break;
      case 1: kind = SH4_THUMB_LDK_SB; align_mask = 0; break;
      case 2: kind = SH4_THUMB_LDK_UH; align_mask = 1; break;
      case 3: kind = SH4_THUMB_LDK_SH; align_mask = 1; break;
      }
    } else {
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_W; align_mask = 3; is_load = 0; break;
      case 1: kind = SH4_THUMB_LDK_B; align_mask = 0; is_load = 0; break;
      case 2: kind = SH4_THUMB_LDK_W; align_mask = 3; break;
      case 3: kind = SH4_THUMB_LDK_B; align_mask = 0; break;
      }
    }
  } else if (hi >= 0x60 && hi <= 0x7F) {
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    is_load = (opcode >> 11) & 1;
    offset = imm5 << (is_byte ? 0 : 2);
    kind = is_byte ? SH4_THUMB_LDK_B : SH4_THUMB_LDK_W;
    align_mask = is_byte ? 0 : 3;
  } else if (hi >= 0x80 && hi <= 0x8F) {
    is_load = (opcode >> 11) & 1;
    offset = ((opcode >> 6) & 0x1F) << 1;
    kind = SH4_THUMB_LDK_UH;
    align_mask = 1;
  } else if (hi >= 0x90 && hi <= 0x9F) {
    is_load = (opcode >> 11) & 1;
    rd = (opcode >> 8) & 7;
    rb = 13;
    offset = (opcode & 0xFF) << 2;
    kind = SH4_THUMB_LDK_W;
    align_mask = 3;
  } else {
    return 0;
  }

  if (!is_load)
    return 0;
  if (!(const_mask & (1u << rb)))
    return 0;
  if (reg_offset && !(const_mask & (1u << ro)))
    return 0;

  address = const_val[rb] + offset + (reg_offset ? const_val[ro] : 0);
  if (align_mask && (address & (u32)align_mask))
    return 0;

  region = address >> 24;
  if (region == 0x03u) {
    host_addr = (uintptr_t)(iwram + 0x8000u + (address & 0x7FFFu));
    debit = 1;
  } else if (region == 0x02u) {
    host_addr = (uintptr_t)(ewram + (address & 0x3FFFFu));
    debit = (kind == SH4_THUMB_LDK_W) ? 6 : 3;
  } else if (region == 0x04u) {
    host_addr = (uintptr_t)(((u8 *)io_registers) + (address & 0x3FFu));
    debit = 1;
  } else {
    return 0;
  }

#if (defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)) && \
  CGBA_SH4_LDST_DETAIL_COUNTERS
  sh4g_const(tp, (u32)(uintptr_t)&cgba_sh4_native_thumb_const_io_count,
             SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_l_load(&cg, SH4_REG_T0, SH4_REG_T1);
    sh4_emit_add_imm(&cg, 1, SH4_REG_T1);
    sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T0);
    sh4g_close(tp, &cg); }
#endif

  sh4g_const(tp, (u32)host_addr, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    switch (kind) {
    case SH4_THUMB_LDK_W:
      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_B:
      sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_UH:
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    case SH4_THUMB_LDK_SH:
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
      sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
      sh4_emit_exts_w(&cg, SH4_REG_T1, SH4_REG_T1);
      break;
    default:
      sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
      break;
    }
    sh4_emit_store_greg(&cg, SH4_REG_T1, rd);
    sh4g_close(tp, &cg); }

  return debit;
#endif
}

/* Native Thumb mapped loads and plain RAM stores. Side-effecting/open regions
 * fall back to cgba_sh4_thumb_ldst so DMA/IRQ alerts, I/O semantics, ROM paging
 * and backup stay helper-owned. RAM stores check the SMC tag mirror first and
 * fall back if they would overwrite translated guest code. */
SH4G_NOINLINE int sh4g_thumb_ldst_native(u8 **tp, u32 opcode, u32 pc,
  int cycle_count)
{
#ifndef CGBA_SH4_THUMB_LDST_NATIVE
  (void)tp;
  (void)opcode;
  (void)pc;
  (void)cycle_count;
  return 0;
#else
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7, rb = (opcode >> 3) & 7;
  u32 offset = 0, ro = 0, reg_offset = 0;
  int pc_relative = 0;
  int kind = SH4_THUMB_LDK_W, is_load = 1;

  if (hi >= 0x48 && hi <= 0x4F) {                    /* LDR Rd,[PC,#imm8*4] */
    rd = (opcode >> 8) & 7;
    offset = (opcode & 0xFF) << 2;
    pc_relative = 1;
    kind = SH4_THUMB_LDK_W;
  } else if (hi >= 0x50 && hi <= 0x5F) {             /* register-offset forms */
    u32 op = (opcode >> 10) & 3;
    ro = (opcode >> 6) & 7;
    reg_offset = 1;
    if (opcode & 0x0200) {                            /* format 8: H/S */
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_UH; is_load = 0; break; /* STRH */
      case 1: kind = SH4_THUMB_LDK_SB; break;              /* LDRSB */
      case 2: kind = SH4_THUMB_LDK_UH; break;              /* LDRH */
      case 3: kind = SH4_THUMB_LDK_SH; break;              /* LDRSH */
      }
    } else {                                          /* format 7: L/B */
      switch (op) {
      case 0: kind = SH4_THUMB_LDK_W; is_load = 0; break;  /* STR */
      case 1: kind = SH4_THUMB_LDK_B; is_load = 0; break;  /* STRB */
      case 2: kind = SH4_THUMB_LDK_W; break;               /* LDR */
      case 3: kind = SH4_THUMB_LDK_B; break;               /* LDRB */
      }
    }
  } else if (hi >= 0x60 && hi <= 0x7F) {             /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    is_load = (opcode >> 11) & 1;
    offset = imm5 << (is_byte ? 0 : 2);
    kind = is_byte ? SH4_THUMB_LDK_B : SH4_THUMB_LDK_W;
  } else if (hi >= 0x80 && hi <= 0x8F) {             /* imm5 halfword */
    is_load = (opcode >> 11) & 1;
    offset = ((opcode >> 6) & 0x1F) << 1;
    kind = SH4_THUMB_LDK_UH;
  } else if (hi >= 0x90 && hi <= 0x9F) {             /* SP-relative word */
    is_load = (opcode >> 11) & 1;
    rd = (opcode >> 8) & 7;
    rb = 13;                                         /* REG_SP */
    offset = (opcode & 0xFF) << 2;
    kind = SH4_THUMB_LDK_W;
  } else {
    return 0;
  }

  if (is_load && !pc_relative) {
    uint16_t load_delay_op = 0x0009u;
    if (reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rb, SH4_REG_T0);
      sh4_emit_load_greg(&cg, ro, SH4_REG_T1);
      sh4g_close(tp, &cg);
      load_delay_op = 0x312Cu;                       /* ADD R2, R1 */
    } else if (offset == 0u) {
#if CGBA_SH4_REG_FORWARD
      if (*tp == sh4_forward_at && rb == sh4_forward_guest) {
        if (sh4_forward_host == SH4_REG_RET) {
          sh4_codegen cg = sh4g_open(tp);
          sh4_emit_load_greg(&cg, rb, SH4_REG_T0);
          sh4g_close(tp, &cg);
        } else if (sh4_forward_host == SH4_REG_T0) {
          sh4_forward_at = NULL;
        } else {
          load_delay_op = (uint16_t)(0x6103u | (sh4_forward_host << 4));
          sh4_forward_at = NULL;
        }
      } else
#endif
      {
#if CGBA_SH4_PIN_GUEST_R0
        if (rb == 0u)
          load_delay_op = 0x61B3u;                   /* MOV R11, R1 */
        else
#endif
#if CGBA_SH4_PIN_GUEST_R1
        if (rb == 1u)
          load_delay_op = 0x61C3u;                   /* MOV R12, R1 */
        else
#endif
          load_delay_op = (uint16_t)(0x51E0u | (rb & 0xFu));
      }
    } else if (offset <= 127u) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rb, SH4_REG_T0);
      sh4g_close(tp, &cg);
      load_delay_op = (uint16_t)(0x7100u | (offset & 0xFFu));
    } else if (offset <= 254u) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rb, SH4_REG_T0);
      sh4_emit_add_imm(&cg, 127, SH4_REG_T0);
      sh4g_close(tp, &cg);
      load_delay_op = (uint16_t)(0x7100u | ((offset - 127u) & 0xFFu));
    } else {
      { sh4_codegen cg = sh4g_open(tp);
        sh4_emit_load_greg(&cg, rb, SH4_REG_T0);
        sh4g_close(tp, &cg); }
      sh4g_const(tp, offset, SH4_REG_T1);
      load_delay_op = 0x312Cu;                       /* ADD R2, R1 */
    }
    sh4g_fastmem_site_compact(tp, (int)kind,
                              (const void *)cgba_sh4_thumb_ldst, (u32)opcode,
                              (u32)pc, cycle_count, rd, load_delay_op);
    return 1;
  }

  if (pc_relative) {
    sh4g_const(tp, ((pc & ~2u) + 4u + offset), SH4_REG_T0);
  } else {
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rb, SH4_REG_T0);       /* R1 = base */
      sh4g_close(tp, &cg); }
    if (reg_offset) {
      sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, ro, SH4_REG_T1);
      sh4_emit_add_reg(&cg, SH4_REG_T1, SH4_REG_T0); /* R1 = base + reg[ro] */
      sh4g_close(tp, &cg);
    } else if (offset) {
      if (offset <= 127u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, (int)offset, SH4_REG_T0); /* R1 = base + offset */
        sh4g_close(tp, &cg);
      } else if (offset <= 254u) {
        sh4_codegen cg = sh4g_open(tp);
        sh4_emit_add_imm(&cg, 127, SH4_REG_T0);
        sh4_emit_add_imm(&cg, (int)(offset - 127u), SH4_REG_T0);
        sh4g_close(tp, &cg);
      } else {
        sh4g_const(tp, offset, SH4_REG_T1);
        sh4g_add_reg(tp, SH4_REG_T1, SH4_REG_T0);      /* R1 = base + offset */
      }
    }
  }

  /* Out-of-line fast path (see sh4_fastmem.h): shared routine + compact
   * site; guard failures fall to cgba_sh4_thumb_ldst via the tramp. */
  {
    int fm = is_load ? (int)kind
      : (kind == SH4_THUMB_LDK_W ? CGBA_FM_STORE_W
         : kind == SH4_THUMB_LDK_UH ? CGBA_FM_STORE_UH : CGBA_FM_STORE_B);
    sh4g_fastmem_site(tp, fm,
                      (const void *)cgba_sh4_thumb_ldst, (u32)opcode, (u32)pc,
                      cycle_count, rd, -1);
  }
  return 1;
#endif
}

#ifdef CGBA_SH4_THUMB_LDST_NATIVE
/* Fuse Thumb constant-RAM-base indexed array load:
 *   op0: ADD rA, rIdx, rC  (fmt2, dead flags) OR ADD rA, rH (fmt5, no flags)
 *        where one operand is a known IWRAM/EWRAM constant `base`
 *   op1: LDRB/LDRH/LDR rD, [rA, #off]
 * Checks `(u32)reg[rIdx] < (ram_size - (base_off + off))` (guaranteed in-region
 * with zero 32-bit wrap) and alignment, then loads straight from host RAM
 * without a fastmem call, skipping `reg[rA]` store when `rD == rA`.
 * Returns 1 on fusion (writing `*out_rA` and `*out_rD`), 0 otherwise. */
static inline int sh4g_thumb_ram_idx_load_native(u8 **tp, u32 op0, u32 op1,
  u32 pc, int cc2, u32 const_mask, const u32 *const_val, int op0_flags_live,
  unsigned *out_rA, unsigned *out_rD)
{
  unsigned rA = 0, rIdx = 0, rD;
  u32 base = 0, off = 0, region, base_off, limit, host_base;
  int kind, align, debit;
  u8 *miss[2], *bra_done;
  int nmiss = 0;

  if ((op0 & 0xFE00u) == 0x1800u) {                  /* fmt2 ADD Rd, Rn, Rm */
    unsigned rn, rm;
    if (op0_flags_live) return 0;
    rA = op0 & 7u;
    rn = (op0 >> 3) & 7u;
    rm = (op0 >> 6) & 7u;
    if ((const_mask & (1u << rn)) && !(const_mask & (1u << rm))) {
      base = const_val[rn]; rIdx = rm;
    } else if ((const_mask & (1u << rm)) && !(const_mask & (1u << rn))) {
      base = const_val[rm]; rIdx = rn;
    } else {
      return 0;
    }
  } else if ((op0 & 0xFF00u) == 0x4400u) {           /* fmt5 ADD Rd, Rs */
    unsigned rs = (op0 >> 3) & 15u;
    rA = (op0 & 7u) | ((op0 >> 4) & 8u);
    if (rA >= 8u || rs == 15u) return 0;
    if ((const_mask & (1u << rA)) && !(const_mask & (1u << rs))) {
      base = const_val[rA]; rIdx = rs;
    } else if ((const_mask & (1u << rs)) && !(const_mask & (1u << rA))) {
      base = const_val[rs]; rIdx = rA;
    } else {
      return 0;
    }
  } else {
    return 0;
  }

  if ((op1 & 0xF800u) == 0x7800u) {                  /* LDRB rD, [rA, #imm5] */
    kind = SH4_THUMB_LDK_B; off = (op1 >> 6) & 31u; align = 0;
  } else if ((op1 & 0xF800u) == 0x8800u) {           /* LDRH rD, [rA, #imm5*2] */
    kind = SH4_THUMB_LDK_UH; off = ((op1 >> 6) & 31u) << 1; align = 1;
  } else if ((op1 & 0xF800u) == 0x6800u) {           /* LDR rD, [rA, #imm5*4] */
    kind = SH4_THUMB_LDK_W; off = ((op1 >> 6) & 31u) << 2; align = 3;
  } else {
    return 0;
  }
  if (((op1 >> 3) & 7u) != rA) return 0;
  rD = op1 & 7u;

  region = base >> 24;
  if (region == 3u) {
    base_off = base - 0x03000000u;
    if (base_off >= 0x8000u || base_off + off >= 0x8000u) return 0;
    limit = 0x8000u - (base_off + off);
    host_base = (u32)(uintptr_t)(iwram + 0x8000u + base_off + off);
    debit = 1;
  } else if (region == 2u) {
    base_off = base - 0x02000000u;
    if (base_off >= 0x40000u || base_off + off >= 0x40000u) return 0;
    limit = 0x40000u - (base_off + off);
    host_base = (u32)(uintptr_t)(ewram + base_off + off);
    debit = (kind == SH4_THUMB_LDK_W) ? 6 : 3;
  } else {
    return 0;
  }
  if (align && ((base + off) & (u32)align)) return 0;

  sh4g_litseg_reserve_bytes(tp, 4, 112u);

  /* Load idx -> R0, verify (u32)idx < limit (no wrap, in-region) and align. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rIdx, SH4_REG_RET);      /* R0 = reg[rIdx] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, limit, SH4_REG_T1);                 /* R2 = limit */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);    /* T = (R0 >= limit) */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  if (align) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, align);                    /* T = aligned */
    sh4g_close(tp, &cg);
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Fast path: optional reg[rA] commit (skipped when rD == rA), direct load. */
  if (rD != rA) {
    sh4g_const(tp, base, SH4_REG_T1);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T1);
      sh4_emit_store_greg(&cg, SH4_REG_T1, rA);
      sh4g_close(tp, &cg); }
  }
  sh4g_const(tp, host_base, SH4_REG_T2);             /* R3 = host_base */
  { sh4_codegen cg = sh4g_open(tp);
    unsigned dst = SH4_REG_T0;
    int pin = sh4_pinned_reg(rD);
    if (pin >= 0 && kind != SH4_THUMB_LDK_W)
      dst = (unsigned)pin;
    switch (kind) {
    case SH4_THUMB_LDK_B:
      sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, dst);
      sh4_emit_extu_b(&cg, dst, dst);
      break;
    case SH4_THUMB_LDK_UH:
      sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, dst);
      sh4_emit_swap_b(&cg, dst, dst);
      sh4_emit_extu_w(&cg, dst, dst);
      break;
    default: /* SH4_THUMB_LDK_W */
      sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T0);
      sh4_emit_bswap32(&cg, SH4_REG_T0, SH4_REG_T0);
      break;
    }
    if (dst == SH4_REG_T0)
      sh4_emit_store_greg(&cg, SH4_REG_T0, rD);
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, debit);

  /* Cold fallback: R0 still holds original reg[rIdx]. */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  sh4g_const(tp, base, SH4_REG_T0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_RET, SH4_REG_T0);  /* R1 = base + idx */
    sh4_emit_store_greg(&cg, SH4_REG_T0, rA);
    if (off)
      sh4_emit_add_imm(&cg, (int)off, SH4_REG_T0);   /* R1 = addr */
    sh4g_close(tp, &cg); }
  sh4g_fastmem_site(tp, (int)kind,
                    (const void *)cgba_sh4_thumb_ldst, (u32)op1, (u32)(pc + 2),
                    cc2, rD, -1);

  sh4g_patch_bra(bra_done, *tp);
  if (out_rA) *out_rA = rA;
  if (out_rD) *out_rD = rD;
  return 1;
}

/* Fuse a run of 2..6 consecutive Thumb `(STRH rD, [rB, #0]; ADD rB, #even_step)`
 * pairs (dead ADD flags, rD != rB, max store offset <= 30, total step <= 127).
 * Validates canonical aligned IWRAM rB + straddle + halfword SMC tags ONCE,
 * writes all halfwords inline (reusing swapped R0 when rD repeats), advances
 * rB once, and debits npairs IWRAM cycles once. */
static inline int sh4g_thumb_strh_stride_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int npairs, u32 pc)
{
  unsigned rB, rd[6];
  u32 step[6], off[6], total_step = 0u;
  int p;
  u8 *miss[3], *bra_done;
  int nmiss = 0;

  if (npairs < 2 || npairs > 6) return 0;
  if ((ops[0] & 0xFFC0u) != 0x8000u) return 0;
  rB = (ops[0] >> 3) & 7u;
  for (p = 0; p < npairs; p++) {
    u32 op_s = ops[p * 2];
    u32 op_a = ops[p * 2 + 1];
    if ((op_s & 0xFFC0u) != 0x8000u || ((op_s >> 3) & 7u) != rB) return 0;
    rd[p] = op_s & 7u;
    if (rd[p] == rB) return 0;
    if ((op_a & 0xFF00u) != (0x3000u | (rB << 8))) return 0;
    step[p] = op_a & 0xFFu;
    if (step[p] == 0u || (step[p] & 1u) != 0u) return 0;
    off[p] = total_step;
    if (off[p] > 30u) return 0;
    total_step += step[p];
    if (total_step > 127u) return 0;
  }

  sh4g_litseg_reserve_bytes(tp, 6, (u32)(112 + npairs * 16));

  /* Guard 1: rB in canonical aligned IWRAM with zero 32 KiB wrap. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rB, SH4_REG_T0);          /* R1 = reg[rB] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_ARG0);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_sub(&cg, SH4_REG_ARG0, SH4_REG_RET);     /* R0 = rB - 0x03000000 */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x8000u - (off[npairs - 1] + 2u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_RET);
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_tst_imm(&cg, 1);                         /* halfword aligned? */
    sh4_emit_mov_reg(&cg, SH4_REG_RET, SH4_REG_ARG1); /* R5 = iw_off */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bf_placeholder(tp);

  /* Guard 2: fast-bypass via cgba_iwram_code_end, else check halfword tags. */
  { u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                      SH4_REG_ARG1, SH4_REG_T2, SH4_REG_ARG0);

    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_add_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);  /* R3 = iwram + iw_off */
      sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_ARG0); /* off[0] == 0 */
      for (p = 1; p < npairs; p++) {
        sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, off[p] >> 1);
        sh4_emit_or(&cg, SH4_REG_RET, SH4_REG_ARG0);
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all tags clear */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_ok, *tp); }

  /* Hot body: write all npairs halfwords inline, commit rB += total_step. */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_ARG1, SH4_REG_T2);  /* R3 = iwram_data + off */
    for (p = 0; p < npairs; p++) {
      if (p == 0 || rd[p] != rd[p - 1]) {
        sh4_emit_load_greg(&cg, rd[p], SH4_REG_RET);
        sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
      }
      if (off[p] == 0u) sh4_emit_mov_w_store(&cg, SH4_REG_RET, SH4_REG_T2);
      else              sh4_emit_mov_w_store_disp(&cg, SH4_REG_T2, off[p] >> 1);
    }
    sh4_emit_add_imm(&cg, (int)total_step, SH4_REG_T0);
    sh4_emit_store_greg(&cg, SH4_REG_T0, rB);
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, npairs);

  /* Cold fallback: exact per-pair emission (ADD flags were proven dead). */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  for (p = 0; p < npairs; p++) {
    sh4_greg_forward_reset();
    sh4g_thumb_ldst_native(tp, ops[p * 2], pc + (u32)(p * 4), ccs[p * 2]);
    { sh4_codegen cg = sh4g_open(tp);
      sh4_emit_load_greg(&cg, rB, SH4_REG_T0);
      sh4_emit_add_imm(&cg, (int)step[p], SH4_REG_T0);
      sh4_emit_store_greg(&cg, SH4_REG_T0, rB);
      sh4g_close(tp, &cg); }
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}

/* Fuse a run of 2..8 consecutive Thumb same-base immediate-offset loads/stores
 * (`0x6000..0x8FFF`: LDR/STR, LDRB/STRB, LDRH/STRH [rB, #imm]).
 * Validates canonical aligned IWRAM rB + range + SMC tags ONCE (with fast
 * cgba_iwram_code_end bypass), eliminates dead IWRAM loads overwritten later in
 * the same run, runs all accesses inline (2..6 insns each), and debits cnt
 * IWRAM cycles once. For cnt<=3 fallback emits fastmem sites (keeping ROM/EWRAM
 * Fast path guards canonical IWRAM + alignment + SMC once, then executes direct
 * host transfers (zero JSR overhead). On miss, emits compact8+OOL fastmem sites
 * so EWRAM/VRAM/IO stay on native fastmem instead of C interp. */
static inline int sh4g_thumb_base_run_native(u8 **tp, const u32 *ops,
  const int *ccs, int cnt, u32 pc)
{
  unsigned rB;
  u8 is_load[8], rd[8], sz[8], off[8], dead_load[8];
  u32 max_end = 0u, req_align = 0u;
  int has_store = 0, k, j;
  u8 *miss[3], *bra_done;
  int nmiss = 0;

  if (cnt < 2 || cnt > 8) return 0;
  if (ops[0] < 0x6000u || ops[0] >= 0x9000u) return 0;
  rB = (ops[0] >> 3) & 7u;

  for (k = 0; k < cnt; k++) {
    u32 op = ops[k];
    u32 hi = (op >> 8) & 0xFFu;
    u32 o, s, al;
    if (op < 0x6000u || op >= 0x9000u) return 0;
    if (((op >> 3) & 7u) != rB) return 0;
    is_load[k] = (u8)((op >> 11) & 1u);
    rd[k] = (u8)(op & 7u);
    if (k < cnt - 1 && is_load[k] && rd[k] == rB) return 0;
    if (hi < 0x70u) {
      s = 4u; al = 3u; o = ((op >> 6) & 31u) << 2;
    } else if (hi < 0x80u) {
      s = 1u; al = 0u; o = (op >> 6) & 31u;
    } else {
      s = 2u; al = 1u; o = ((op >> 6) & 31u) << 1;
    }
    sz[k] = (u8)s;
    off[k] = (u8)o;
    dead_load[k] = 0;
    if (o + s > max_end) max_end = o + s;
    req_align |= al;
    if (!is_load[k]) has_store = 1;
  }

  /* Pure IWRAM dead-load elimination: if load k writes rd[k] and a later load j
   * overwrites rd[k] before any intervening store reads rd[k], skip load k on
   * the IWRAM fast path while still debiting its cycle in `add #-cnt, r13`. */
  for (k = 0; k < cnt; k++) {
    if (!is_load[k] || rd[k] == rB) continue;
    for (j = k + 1; j < cnt; j++) {
      if (!is_load[j] && rd[j] == rd[k]) break;
      if (is_load[j] && rd[j] == rd[k]) {
        dead_load[k] = 1;
        break;
      }
    }
  }

  sh4g_litseg_reserve_bytes(tp, 6, (u32)(96 + cnt * 20));

  /* Guard 1: rB in canonical IWRAM [0x03000000, 0x03008000 - max_end] & aligned. */
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_load_greg(&cg, rB, SH4_REG_T0);            /* R1 = reg[rB] */
    sh4g_close(tp, &cg); }
  sh4g_const(tp, 0x03000000u, SH4_REG_RET);
  sh4g_const(tp, 0x8000u - (max_end - 1u), SH4_REG_T1);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_sub(&cg, SH4_REG_RET, SH4_REG_T0);         /* R1 = rB - 0x03000000 */
    sh4_emit_cmphs(&cg, SH4_REG_T1, SH4_REG_T0);        /* T = out of bounds */
    sh4g_close(tp, &cg); }
  miss[nmiss++] = sh4g_emit_bt_placeholder(tp);

  if (req_align) {
    sh4_codegen cg = sh4g_open(tp);
    sh4_emit_mov_reg(&cg, SH4_REG_T0, SH4_REG_RET);
    sh4_emit_tst_imm(&cg, (int)req_align);              /* T = aligned */
    sh4g_close(tp, &cg);
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
  }

  /* Guard 2: if any store, fast-bypass via cgba_iwram_code_end, else check tags. */
  if (has_store) {
    u8 *smc_ok = sh4g_emit_smc_bypass(tp, &cgba_iwram_code_end,
                                      SH4_REG_T0, SH4_REG_T2, SH4_REG_T1);
    int first = 1;

    sh4g_const(tp, (u32)(uintptr_t)iwram, SH4_REG_T2);
    { sh4_codegen cg = sh4g_open(tp);
      u32 seen_wmask = 0u;
      sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);    /* R3 = iwram + off */
      for (k = 0; k < cnt; k++) {
        unsigned o = off[k], s = sz[k];
        unsigned dst_r;
        if (is_load[k]) continue;
        if (s == 4u && o <= 60u) {
          if (seen_wmask & (1u << (o >> 2))) continue;
          seen_wmask |= (1u << (o >> 2));
        }
        dst_r = first ? SH4_REG_ARG0 : SH4_REG_T1;
        if (s == 4u) {
          if (o == 0u)      sh4_emit_mov_l_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 60u) sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, dst_r, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, dst_r);
          }
        } else if (s == 2u) {
          if (o == 0u) sh4_emit_mov_w_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 30u) {
            sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, o >> 1);
            sh4_emit_mov_reg(&cg, SH4_REG_RET, dst_r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, dst_r);
          }
          sh4_emit_extu_w(&cg, dst_r, dst_r);
        } else {
          if (o == 0u) sh4_emit_mov_b_load(&cg, SH4_REG_T2, dst_r);
          else if (o <= 15u) {
            sh4_emit_mov_b_load_disp(&cg, SH4_REG_T2, o);
            sh4_emit_mov_reg(&cg, SH4_REG_RET, dst_r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, dst_r);
          }
          sh4_emit_extu_b(&cg, dst_r, dst_r);
        }
        if (first) first = 0;
        else       sh4_emit_or(&cg, SH4_REG_T1, SH4_REG_ARG0);
      }
      sh4_emit_tst(&cg, SH4_REG_ARG0, SH4_REG_ARG0);    /* T = all tags clear */
      sh4g_close(tp, &cg); }
    miss[nmiss++] = sh4g_emit_bf_placeholder(tp);
    sh4g_patch_cond(smc_ok, *tp);
  }

  /* Hot body: R3 = iwram_data + off, zero guards, zero JSR/JMP. */
  sh4g_vec_load(tp, SH4G_VEC_iwram_data, SH4_REG_T2);
  { sh4_codegen cg = sh4g_open(tp);
    sh4_emit_add_reg(&cg, SH4_REG_T0, SH4_REG_T2);      /* R3 = iwram_data + off */
    for (k = 0; k < cnt; k++) {
      unsigned o = off[k], s = sz[k], r = rd[k];
      if (dead_load[k]) continue;
      if (is_load[k]) {
        if (s == 4u) {
          if (o == 0u)      sh4_emit_mov_l_load(&cg, SH4_REG_T2, SH4_REG_T1);
          else if (o <= 60u) sh4_emit_mov_l_load_disp(&cg, SH4_REG_T2, SH4_REG_T1, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
          }
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          sh4_emit_store_greg(&cg, SH4_REG_T1, r);
        } else if (s == 2u) {
          if (o == 0u) {
            sh4_emit_mov_w_load(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          } else if (o <= 30u) {
            sh4_emit_mov_w_load_disp(&cg, SH4_REG_T2, o >> 1);
            sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_extu_w(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_store_greg(&cg, SH4_REG_RET, r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_extu_w(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          }
        } else {
          if (o == 0u) {
            sh4_emit_mov_b_load(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          } else if (o <= 15u) {
            sh4_emit_mov_b_load_disp(&cg, SH4_REG_T2, o);
            sh4_emit_extu_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_store_greg(&cg, SH4_REG_RET, r);
          } else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_load_r0(&cg, SH4_REG_T2, SH4_REG_T1);
            sh4_emit_extu_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_store_greg(&cg, SH4_REG_T1, r);
          }
        }
      } else {
        if (s == 4u) {
          sh4_emit_load_greg(&cg, r, SH4_REG_T1);
          sh4_emit_bswap32(&cg, SH4_REG_T1, SH4_REG_T1);
          if (o == 0u)      sh4_emit_mov_l_store(&cg, SH4_REG_T1, SH4_REG_T2);
          else if (o <= 60u) sh4_emit_mov_l_store_disp(&cg, SH4_REG_T1, SH4_REG_T2, o >> 2);
          else {
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_l_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        } else if (s == 2u) {
          if (o == 0u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_w_store(&cg, SH4_REG_T1, SH4_REG_T2);
          } else if (o <= 30u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_RET);
            sh4_emit_swap_b(&cg, SH4_REG_RET, SH4_REG_RET);
            sh4_emit_mov_w_store_disp(&cg, SH4_REG_T2, o >> 1);
          } else {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_swap_b(&cg, SH4_REG_T1, SH4_REG_T1);
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_w_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        } else {
          if (o == 0u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_mov_b_store(&cg, SH4_REG_T1, SH4_REG_T2);
          } else if (o <= 15u) {
            sh4_emit_load_greg(&cg, r, SH4_REG_RET);
            sh4_emit_mov_b_store_disp(&cg, SH4_REG_T2, o);
          } else {
            sh4_emit_load_greg(&cg, r, SH4_REG_T1);
            sh4_emit_mov_imm(&cg, (int)o, SH4_REG_RET);
            sh4_emit_mov_b_store_r0(&cg, SH4_REG_T1, SH4_REG_T2);
          }
        }
      }
    }
    sh4g_close(tp, &cg); }
  bra_done = sh4g_emit_bra_debit(tp, cnt);

  /* Cold fallback: emit resident fastmem sites directly (8-10B each under
   * compact8+OOL, keeping EWRAM/VRAM/IO on native fastmem instead of C interp). */
  while (nmiss)
    sh4g_patch_cond(miss[--nmiss], *tp);
  for (k = 0; k < cnt; k++) {
    sh4_greg_forward_reset();
    sh4g_thumb_ldst_native(tp, ops[k], pc + (u32)(k * 2), ccs[k]);
  }
  sh4g_patch_bra(bra_done, *tp);
  return 1;
}
#endif


#endif /* CGBA_SH4_THUMB_LDST_EMIT_H */
