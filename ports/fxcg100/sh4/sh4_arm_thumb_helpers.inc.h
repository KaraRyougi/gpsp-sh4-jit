/* Internal module included by sh4_interp_helpers.c: ARM/Thumb instruction helpers. */
/* ===================== Thumb single load/store ===================== */

static void cgba_sh4_thumb_ldst_do(u32 opcode, u32 pc)
{
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rd = opcode & 7;
  u32 rb = (opcode >> 3) & 7;

  if (hi >= 0x48 && hi <= 0x4F) {                  /* LDR Rd,[PC,#imm8*4] */
    u32 rdpc = (opcode >> 8) & 7;
    u32 addr = ((pc & ~2u) + 4) + ((opcode & 0xFF) * 4);
    cgba_sh4_helper_thumb_ldst_detail(1, addr, 4, CGBA_SH4_TLD_SRC_PC);
    reg[rdpc] = execute_load_u32(addr);
    return;
  }
  if (hi >= 0x50 && hi <= 0x5F) {                  /* reg-offset forms */
    u32 ro = (opcode >> 6) & 7;
    u32 addr = reg[rb] + reg[ro];
    if (opcode & 0x0200) {                          /* format 8: H/S */
      switch ((opcode >> 10) & 3) {
      case 0:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 2, CGBA_SH4_TLD_SRC_REG);
        execute_store_u16(addr, reg[rd]);
        break;                                             /* STRH */
      case 1:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 1, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_s8(addr);
        break;                                             /* LDRSB */
      case 2:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 2, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u16(addr);
        break;                                             /* LDRH */
      case 3:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 2, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_s16(addr);
        break;                                             /* LDRSH */
      }
    } else {                                        /* format 7: L/B */
      switch ((opcode >> 10) & 3) {
      case 0:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 4, CGBA_SH4_TLD_SRC_REG);
        execute_store_u32(addr, reg[rd]);
        break;                                             /* STR */
      case 1:
        cgba_sh4_helper_thumb_ldst_detail(0, addr, 1, CGBA_SH4_TLD_SRC_REG);
        execute_store_u8(addr, reg[rd]);
        break;                                             /* STRB */
      case 2:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 4, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u32(addr);
        break;                                             /* LDR */
      case 3:
        cgba_sh4_helper_thumb_ldst_detail(1, addr, 1, CGBA_SH4_TLD_SRC_REG);
        reg[rd] = execute_load_u8(addr);
        break;                                             /* LDRB */
      }
    }
    return;
  }
  if (hi >= 0x60 && hi <= 0x7F) {                  /* imm5 word/byte */
    u32 imm5 = (opcode >> 6) & 0x1F;
    u32 is_byte = (opcode >> 12) & 1;
    u32 is_load = (opcode >> 11) & 1;
    u32 addr = reg[rb] + (is_byte ? imm5 : imm5 * 4);
    cgba_sh4_helper_thumb_ldst_detail(is_load, addr, is_byte ? 1 : 4,
      CGBA_SH4_TLD_SRC_IMM);
    if (is_byte) {
      if (is_load) reg[rd] = execute_load_u8(addr);
      else         execute_store_u8(addr, reg[rd]);
    } else {
      if (is_load) reg[rd] = execute_load_u32(addr);
      else         execute_store_u32(addr, reg[rd]);
    }
    return;
  }
  if (hi >= 0x80 && hi <= 0x8F) {                  /* LDRH/STRH imm5*2 */
    u32 addr = reg[rb] + ((opcode >> 6) & 0x1F) * 2;
    cgba_sh4_helper_thumb_ldst_detail((opcode & 0x0800) != 0, addr, 2,
      CGBA_SH4_TLD_SRC_IMM);
    if (opcode & 0x0800) reg[rd] = execute_load_u16(addr);
    else                 execute_store_u16(addr, reg[rd]);
    return;
  }
  if (hi >= 0x90 && hi <= 0x9F) {                  /* LDR/STR [SP,#imm8*4] */
    u32 rdsp = (opcode >> 8) & 7;
    u32 addr = reg[REG_SP] + (opcode & 0xFF) * 4;
    cgba_sh4_helper_thumb_ldst_detail((opcode & 0x0800) != 0, addr, 4,
      CGBA_SH4_TLD_SRC_SP);
    if (opcode & 0x0800) reg[rdsp] = execute_load_u32(addr);
    else                 execute_store_u32(addr, reg[rdsp]);
    return;
  }
}

/* Thumb single load/store never targets PC; run it, then break out of the block
 * if a store raised an alert (DMA/HALT idle, IRQ, SMC). */
int cgba_sh4_thumb_ldst(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_ldst);
  cgba_sh4_begin_mem_helper(pc + 2);
  cgba_sh4_reset_mem_cycles(0);
  cgba_sh4_thumb_ldst_do(opcode, pc);
  sh4_headless_trace_op('L', pc, opcode);
  return cgba_store_alert_break(pc + 2);
}

/* ===================== Thumb block (PUSH/POP/LDMIA/STMIA) =========== */

int cgba_sh4_thumb_block(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(thumb_block);
  u32 hi = (opcode >> 8) & 0xFF;
  u32 rlist = opcode & 0xFF;
  int wrote_pc = 0;
  int i;

  cgba_sh4_begin_mem_helper(pc + 2);
  cgba_sh4_reset_mem_cycles(1);

  if (hi == 0xB4 || hi == 0xB5) {                  /* PUSH {rlist[, lr]} */
    u32 sp = reg[REG_SP];
    u32 count = 0;
    for (i = 0; i < 8; i++) if (rlist & (1 << i)) count++;
    if (hi == 0xB5) count++;                         /* lr */
    sp -= count * 4;
    reg[REG_SP] = sp;
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { execute_store_u32(sp, reg[i]); sp += 4; }
    if (hi == 0xB5) execute_store_u32(sp, reg[REG_LR]);
    return cgba_store_alert_break(pc + 2);
  }
  if (hi == 0xBC || hi == 0xBD) {                  /* POP {rlist[, pc]} */
    u32 sp = reg[REG_SP];
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { reg[i] = execute_load_u32(sp); sp += 4; }
    if (hi == 0xBD) { reg[REG_PC] = execute_load_u32(sp) & ~1u; sp += 4; wrote_pc = 1; }
    reg[REG_SP] = sp;
    return wrote_pc;
  }
  if (hi >= 0xC0 && hi <= 0xCF) {                  /* STMIA/LDMIA Rb!,{rlist} */
    u32 rb = (opcode >> 8) & 7;
    u32 addr = reg[rb];
    u32 is_load = (opcode >> 11) & 1;
    if (is_load) {
      /* Writeback FIRST (exec_thumb_block_mem parity): with the base in the
         rlist the LOADED value must win, not the incremented base. The
         fastmem block routine already matches this; this slow path serves
         the same emitted sites on guard failure and must agree. */
      u32 end = addr;
      for (i = 0; i < 8; i++)
        if (rlist & (1 << i))
          end += 4;
      reg[rb] = end;
      for (i = 0; i < 8; i++)
        if (rlist & (1 << i)) { reg[i] = execute_load_u32(addr); addr += 4; }
      return cgba_store_alert_break(pc + 2);
    }
    for (i = 0; i < 8; i++)
      if (rlist & (1 << i)) { execute_store_u32(addr, reg[i]); addr += 4; }
    reg[rb] = addr;
    return cgba_store_alert_break(pc + 2);
  }
  return cgba_store_alert_break(pc + 2);
}

/* ===================== ARM single data transfer ==================== */

static u32 arm_shifter_operand(u32 opcode, u32 pc, u32 *carry_out)
{
  /* Register-or-immediate shifter operand for ARM data-processing / LDR. */
  u32 cpsr_c = (reg[REG_CPSR] >> 29) & 1;
  if (opcode & 0x02000000) {                       /* immediate (rotated) */
    u32 imm = opcode & 0xFF;
    u32 rot = ((opcode >> 8) & 0xF) * 2;
    u32 v = (rot == 0) ? imm : ((imm >> rot) | (imm << (32 - rot)));
    *carry_out = rot ? ((v >> 31) & 1) : cpsr_c;
    return v;
  } else {                                         /* register shift */
    u32 rm = opcode & 0xF;
    u32 by_reg = opcode & 0x10;                     /* shift amount in a reg */
    /* R15 reads PC+8, or PC+12 when the shift amount is register-specified
       (the extra pipeline cycle advances PC one instruction further). */
    u32 val = (rm == 15) ? (pc + (by_reg ? 12 : 8)) : reg[rm];
    u32 type = (opcode >> 5) & 3;
    u32 amount;
    if (by_reg) {                                   /* shift by register */
      u32 rs = (opcode >> 8) & 0xF;
      amount = reg[rs] & 0xFF;
      if (amount == 0) { *carry_out = cpsr_c; return val; } /* reg-#0: no-op */
    } else {
      amount = (opcode >> 7) & 0x1F;
    }
    switch (type) {
    case 0: /* LSL */
      if (amount == 0) { *carry_out = cpsr_c; return val; }
      if (amount < 32) { *carry_out = (val >> (32 - amount)) & 1; return val << amount; }
      *carry_out = (amount == 32) ? (val & 1) : 0; return 0;
    case 1: /* LSR */
      if (amount == 0) amount = 32;
      if (amount < 32) { *carry_out = (val >> (amount - 1)) & 1; return val >> amount; }
      *carry_out = (amount == 32) ? ((val >> 31) & 1) : 0; return 0;
    case 2: /* ASR */
      if (amount == 0 || amount >= 32) { *carry_out = (val >> 31) & 1; return (u32)((s32)val >> 31); }
      *carry_out = (val >> (amount - 1)) & 1; return (u32)((s32)val >> amount);
    default: /* ROR / RRX */
      if (amount == 0) { u32 r = (cpsr_c << 31) | (val >> 1); *carry_out = val & 1; return r; }
      amount &= 31;
      if (amount == 0) { *carry_out = (val >> 31) & 1; return val; }
      *carry_out = (val >> (amount - 1)) & 1;
      return (val >> amount) | (val << (32 - amount));
    }
  }
}

#ifdef CGBA_GPSP_HEADLESS_TEST
/* Which guest instructions still reach the C single-transfer helper (emit
 * bail or fastmem guard failure)? Small direct-mapped pc-keyed table. */
u32 cgba_armldst_fb_pc[16], cgba_armldst_fb_op[16], cgba_armldst_fb_n[16];
#endif

int cgba_sh4_arm_ldst(u32 opcode, u32 pc)
{
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  {
    u32 slot = (pc * 2654435761u) >> 28;
    if (cgba_armldst_fb_n[slot] == 0 || cgba_armldst_fb_pc[slot] == pc) {
      cgba_armldst_fb_pc[slot] = pc;
      cgba_armldst_fb_op[slot] = opcode;
      cgba_armldst_fb_n[slot]++;
    }
  }
#endif
  CGBA_SH4_HELPER_HIT(arm_ldst);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 base = (rn == 15) ? (pc + 8) : reg[rn];
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 offset, addr;
  int is_half = 0, is_byte = 0, signed_ld = 0, half_w = 0;

  cgba_sh4_begin_mem_helper(pc + 4);
  cgba_sh4_reset_mem_cycles(0);

  if ((opcode & 0x0E000090) == 0x00000090) {
    /* halfword / signed-byte transfer (bits 27..25 = 000, bits 7,4 = 1) */
    is_half = 1;
    signed_ld = (opcode >> 6) & 1;
    half_w = (opcode >> 5) & 1;
    if (opcode & 0x00400000) offset = ((opcode >> 4) & 0xF0) | (opcode & 0x0F); /* imm */
    else                     offset = reg[opcode & 0xF];                        /* reg */
  } else {
    is_byte = (opcode >> 22) & 1;
    if (opcode & 0x02000000) {                       /* register, shifted by imm */
      u32 rm = reg[opcode & 0xF];
      u32 type = (opcode >> 5) & 3, sh = (opcode >> 7) & 0x1F;
      switch (type) {
      case 0: offset = sh ? (rm << sh) : rm; break;
      case 1: offset = sh ? (rm >> sh) : 0; break;
      case 2: offset = (u32)((s32)rm >> (sh ? sh : 31)); break;
      default: offset = sh ? ((rm >> sh) | (rm << (32 - sh)))
                           : (((reg[REG_CPSR] >> 29) & 1) << 31) | (rm >> 1); break;
      }
    } else {
      offset = opcode & 0xFFF;                        /* 12-bit immediate */
    }
  }

  addr = pre ? (up ? base + offset : base - offset) : base;
  cgba_sh4_helper_arm_ldst_detail(is_load, addr);

  if (is_load) {
    u32 v;
    if (is_half) {
      if (signed_ld && half_w)  v = execute_load_s16(addr);
      else if (signed_ld)       v = execute_load_s8(addr);
      else                      v = execute_load_u16(addr);
    } else {
      v = is_byte ? execute_load_u8(addr) : execute_load_u32(addr);
    }
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
    reg[rd] = v;
    if (rd == 15) { reg[REG_PC] = v & ~1u; return 1; }
  } else {
    u32 v = (rd == 15) ? (pc + 12) : reg[rd];
    if (is_half) execute_store_u16(addr, v);
    else if (is_byte) execute_store_u8(addr, v);
    else execute_store_u32(addr, v);
    if (!pre) addr = up ? base + offset : base - offset;
    if (writeback || !pre) reg[rn] = addr;
  }
  return cgba_store_alert_break(pc + 4);
}

/* ===================== CPSR / SPSR write (gpSP canonical) ==========
 * Every mode-changing write goes through these so the register banking stays in
 * one place — the same set_cpu_mode + check_for_interrupts path the interpreter
 * uses (cpu.cc). Doing the re-bank ad hoc per call site is what desynced
 * reg[CPU_MODE] before. The IRQ gate reads IE/IF/IME through read_ioreg (the
 * guest is little-endian, the SH4 host is big-endian), unlike the x86 backend
 * which can index io_registers[] directly. */

/* set_cpu_mode + check_for_interrupts: re-bank to the mode now in reg[REG_CPSR]
 * and, if that just unmasked a pending IRQ, enter it (LR_irq = next_pc + 4,
 * SPSR_irq = old CPSR, CPSR = 0xD2, switch to IRQ mode). Returns the IRQ vector
 * to redispatch to, or 0 if no IRQ was taken. */
static u32 sh4_rebank_and_irq(u32 next_pc)
{
  set_cpu_mode(cpu_modes[reg[REG_CPSR] & 0xF]);
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && read_ioreg(REG_IME) &&
      ((reg[REG_CPSR] & 0x80) == 0)) {
    reg[REG_BUS_VALUE] = 0xe25ef004u;
    REG_MODE(MODE_IRQ)[6] = next_pc + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    set_cpu_mode(MODE_IRQ);
    return 0x00000018;
  }
  return 0;
}

/* gpSP execute_spsr_restore: exception return (SUBS pc,... / LDM{pc}^). In a
 * privileged non-system mode, restore CPSR from the mode's SPSR, re-bank, take a
 * now-pending IRQ, and fold the restored Thumb bit (bit0) into the address. In
 * USER/SYSTEM there is no SPSR, so the address passes through unchanged. */
u32 execute_spsr_restore(u32 address)
{
  if (reg[CPU_MODE] != MODE_USER && reg[CPU_MODE] != MODE_SYSTEM) {
    if (reg[CPU_MODE] == MODE_IRQ)
      reg[REG_BUS_VALUE] = 0xe55ec002u;
    reg[REG_CPSR] = REG_SPSR(reg[CPU_MODE]);
    if (sh4_rebank_and_irq(address))
      address = 0x00000018;
    else if (reg[REG_CPSR] & 0x20)
      address |= 0x01;
  }
  return address;
}

/* ===================== ARM block (LDM/STM) ========================= */

int cgba_sh4_arm_block(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_block);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rlist = opcode & 0xFFFF;
  u32 is_load = (opcode >> 20) & 1;
  u32 writeback = (opcode >> 21) & 1;
  u32 pre = (opcode >> 24) & 1;
  u32 up = (opcode >> 23) & 1;
  u32 s_bit = (opcode >> 22) & 1;
  u32 base = reg[rn];
  u32 count = 0, addr, new_base, i, lowest = 16;
  int wrote_pc = 0;
  /* S-bit LDM/STM transfers the USER-mode banked registers (r8-r14). Match the
     gpSP interpreter (cpu.cc exec_arm_block_mem ~1010/1041): bracket the transfer
     in USER mode whenever the S bit is set AND it is a store OR rn != r15 -- so
     an LDM{pc}^ exception return (S=1, pc in list, rn != 15) lands the popped user
     r13/r14 in the USER bank and the SPSR re-bank below (execute_spsr_restore ->
     set_cpu_mode(USER)) reloads THEM instead of stale USER sp/lr. (The old code
     transferred the current/privileged bank, diverging from the interpreter for
     this exception-return case; not the trigger of the frame-700 freeze, which is
     a control-flow divergence, but a genuine latent bug.) The native fast path
     bails on the S bit, so this C helper handles every S-bit block transfer. */
  int user_bank = s_bit && (!is_load || rn != 15);
  u32 old_cpsr = reg[REG_CPSR];

  cgba_sh4_begin_mem_helper(pc + 4);
  cgba_sh4_reset_mem_cycles(1);
  cgba_sh4_helper_arm_block_detail(is_load);

  for (i = 0; i < 16; i++)
    if (rlist & (1u << i)) { count++; if (lowest == 16) lowest = i; }

  new_base = up ? base + count * 4 : base - count * 4;
  addr = (up ? base : new_base) & ~3u;   /* word-align; traverse ascending */
  if (up == 0) pre = !pre;

  if (user_bank) set_cpu_mode(MODE_USER);   /* transfer the USER bank (r13/r14) */

  for (i = 0; i < 16; i++) {
    if (!(rlist & (1u << i))) continue;
    if (pre) addr += 4;
    if (is_load) {
      reg[i] = execute_load_u32(addr);
      if (i == 15) {
        /* ARM LDM {..., pc} without S-bit stays in ARM mode (4-byte aligned);
         * with S-bit, execute_spsr_restore() below applies the restored T bit. */
        reg[REG_PC] = reg[15] & (s_bit ? ~1u : ~3u);
        wrote_pc = 1;
      }
    } else {
      /* STM stores PC+12 for r15; for the base reg, the OLD base is stored only
         when it is the lowest in the list, otherwise the written-back value. */
      u32 v = (i == 15) ? (pc + 12)
            : (i == rn && writeback && i != lowest) ? new_base
            : reg[i];
      execute_store_u32(addr, v);
    }
    if (!pre) addr += 4;
  }

  /* LDM with the base in the list keeps the loaded value (no writeback). */
  if (writeback && !(is_load && (rlist & (1u << rn))))
    reg[rn] = new_base;

  if (user_bank) set_cpu_mode(cpu_modes[old_cpsr & 0xF]);   /* back to entry mode */

  /* LDM{pc}^ (exception return): restore CPSR from SPSR, re-bank, take any now-
   * pending IRQ — the canonical execute_spsr_restore path. */
  if (wrote_pc && s_bit) {
    u32 next = execute_spsr_restore(reg[15]);
    reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
  }
  /* `|` not `||`: always consume the alert. LDM is loads (no alert) so the break
   * is a no-op for wrote_pc; STM stores, where the break may exit the block. */
  return wrote_pc | cgba_store_alert_break(pc + 4);
}

/* ===================== ARM data-processing ========================= */

#ifdef CGBA_GPSP_HEADLESS_TEST
/* dp fallback mix: [op nibble] and reason flags */
u32 cgba_dp_fb_op[16];
u32 cgba_dp_fb_pc, cgba_dp_fb_regshift, cgba_dp_fb_ror, cgba_dp_fb_s;
#endif

int cgba_sh4_arm_dp(u32 opcode, u32 pc)
{
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_dp_fb_op[(opcode >> 21) & 0xF]++;
  if (((opcode >> 12) & 0xF) == 15 || ((opcode >> 16) & 0xF) == 15 ||
      (!(opcode & 0x02000000) && (opcode & 0xF) == 15))
    cgba_dp_fb_pc++;
  else if (!(opcode & 0x02000000) && (opcode & 0x10))
    cgba_dp_fb_regshift++;
  else if (!(opcode & 0x02000000) && ((opcode >> 5) & 3) == 3 && (opcode & 0xFF0))
    cgba_dp_fb_ror++;
  if ((opcode >> 20) & 1) cgba_dp_fb_s++;
#endif
  CGBA_SH4_HELPER_HIT(arm_dp);
  u32 op = (opcode >> 21) & 0xF;
  u32 set_flags = (opcode >> 20) & 1;
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  /* With a register-controlled shift (bit25 clear, bit4 set), R15 reads PC+12;
     otherwise PC+8. arm_shifter_operand applies the same rule to Rm. */
  u32 reg_shift = ((opcode & 0x02000010) == 0x00000010);
  u32 a = (rn == 15) ? (pc + (reg_shift ? 12 : 8)) : reg[rn];
  u32 carry = (reg[REG_CPSR] >> 29) & 1;
  u32 b = arm_shifter_operand(opcode, pc, &carry);
  u32 cf = carry, vf = (reg[REG_CPSR] >> 28) & 1, res = 0;
  u32 oldc = (reg[REG_CPSR] >> 29) & 1;
  int writes = 1;
  u64 tmp;

  switch (op) {
  case 0x0: res = a & b; break;                    /* AND */
  case 0x1: res = a ^ b; break;                    /* EOR */
  case 0x2: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SUB */
  case 0x3: tmp = (u64)b - (u64)a; res = (u32)tmp; cf = b >= a;
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSB */
  case 0x4: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADD */
  case 0x5: tmp = (u64)a + (u64)b + oldc; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; break;  /* ADC */
  case 0x6: tmp = (u64)a - (u64)b - (1 - oldc); res = (u32)tmp; cf = a >= ((u64)b + (1 - oldc));
            vf = ((a ^ b) & (a ^ res)) >> 31; break;   /* SBC */
  case 0x7: tmp = (u64)b - (u64)a - (1 - oldc); res = (u32)tmp; cf = b >= ((u64)a + (1 - oldc));
            vf = ((b ^ a) & (b ^ res)) >> 31; break;   /* RSC */
  case 0x8: res = a & b; writes = 0; break;        /* TST */
  case 0x9: res = a ^ b; writes = 0; break;        /* TEQ */
  case 0xA: tmp = (u64)a - (u64)b; res = (u32)tmp; cf = a >= b;
            vf = ((a ^ b) & (a ^ res)) >> 31; writes = 0; break;  /* CMP */
  case 0xB: tmp = (u64)a + (u64)b; res = (u32)tmp; cf = tmp >> 32;
            vf = (~(a ^ b) & (a ^ res)) >> 31; writes = 0; break; /* CMN */
  case 0xC: res = a | b; break;                    /* ORR */
  case 0xD: res = b; break;                        /* MOV */
  case 0xE: res = a & ~b; break;                   /* BIC */
  case 0xF: res = ~b; break;                       /* MVN */
  }

  if (writes) {
    reg[rd] = res;
    if (rd == 15) {
      /* Writing PC with the S bit (e.g. SUBS pc,lr,#4 — the IRQ/exception
       * return) restores CPSR from SPSR and re-banks via execute_spsr_restore,
       * which also folds the restored Thumb bit into bit0. Mask afterwards so
       * the new Thumb state picks the alignment. */
      u32 next = set_flags ? execute_spsr_restore(res) : res;
      reg[REG_PC] = next & ((reg[REG_CPSR] & 0x20) ? ~1u : ~3u);
      return 1;
    }
    if (set_flags) set_nzcv(res, cf, vf & 1);
  } else if (set_flags) {
    set_nzcv(res, cf, vf & 1);
  }
  return 0;
}

/* ===================== ARM multiply / psr / swap (TODO inline) ====== */

void cgba_sh4_arm_multiply(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_mul);
  u32 rd = (opcode >> 16) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 rn = (opcode >> 12) & 0xF;
  u32 res = reg[rm] * reg[rs];
  (void)pc;
  if (opcode & 0x00200000) res += reg[rn];          /* MLA */
  reg[rd] = res;
  if (opcode & 0x00100000) set_nz(res);             /* S bit */
}

void cgba_sh4_arm_multiply_long(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_mul);
  u32 rdhi = (opcode >> 16) & 0xF;
  u32 rdlo = (opcode >> 12) & 0xF;
  u32 rs = (opcode >> 8) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_signed = (opcode >> 22) & 1;
  u32 accumulate = (opcode >> 21) & 1;
  u64 res;
  (void)pc;
  if (is_signed) res = (u64)((s64)(s32)reg[rm] * (s64)(s32)reg[rs]);
  else           res = (u64)reg[rm] * (u64)reg[rs];
  if (accumulate) res += (((u64)reg[rdhi]) << 32) | reg[rdlo];
  reg[rdlo] = (u32)res;
  reg[rdhi] = (u32)(res >> 32);
  if (opcode & 0x00100000) {                         /* S bit */
    u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
    if (res & 0x8000000000000000ull) cpsr |= CF_N;
    if (res == 0) cpsr |= CF_Z;
    reg[REG_CPSR] = cpsr;
  }
}

int cgba_sh4_arm_psr(u32 opcode, u32 pc)
{
#if defined(CGBA_GPSP_HEADLESS_TEST) && defined(CGBA_SH4_DIAG_COUNTERS)
  {
    extern u32 cgba_psr_fb[8];
    /* [0]=mrs [1]=msr-imm [2]=msr-reg-c [3]=msr-reg-f [4]=msr-reg-cf
       [5]=msr-spsr [6]=other */
    u32 is_msr = (opcode >> 21) & 1;
    u32 spsr_sel = (opcode >> 22) & 1;
    u32 fields = (opcode >> 16) & 0xF;
    if (!is_msr) cgba_psr_fb[0]++;
    else if (spsr_sel) cgba_psr_fb[5]++;
    else if (opcode & 0x02000000) cgba_psr_fb[1]++;
    else if (fields == 1) cgba_psr_fb[2]++;
    else if (fields == 8) cgba_psr_fb[3]++;
    else if (fields == 9) cgba_psr_fb[4]++;
    else cgba_psr_fb[6]++;
  }
#endif

  CGBA_SH4_HELPER_HIT(arm_psr);
  u32 is_msr   = (opcode >> 21) & 1;   /* 0 = MRS (read), 1 = MSR (write) */
  u32 use_spsr = (opcode >> 22) & 1;   /* 0 = CPSR, 1 = SPSR of cur mode  */

  if (!is_msr) {                                     /* MRS Rd, <psr> */
    u32 rd = (opcode >> 12) & 0xF;
    reg[rd] = use_spsr ? REG_SPSR(reg[CPU_MODE]) : reg[REG_CPSR];
    return 0;
  }

  {                                                  /* MSR <psr>, val */
    /* Field mask is privilege-aware (gpSP's cpsr_masks): in USER mode the
       control byte is restricted to the Thumb bit, so USER code cannot change
       the mode bits. pfield bit0 = control field, bit1 = flags field. */
    u32 pfield = ((opcode >> 16) & 1) | ((opcode >> 18) & 2);
    u32 val, mask;
    if (opcode & 0x02000000) {
      u32 imm = opcode & 0xFF, rot = ((opcode >> 8) & 0xF) * 2;
      val = rot ? ((imm >> rot) | (imm << (32 - rot))) : imm;
    } else {
      val = reg[opcode & 0xF];
    }

    if (use_spsr) {
      mask = spsr_masks[pfield];
      REG_SPSR(reg[CPU_MODE]) = (val & mask) | (REG_SPSR(reg[CPU_MODE]) & ~mask);
    } else {
      /* CPSR write through the canonical bank path. When the control byte is
       * written (mode/IRQ-disable change) re-bank and, if an IRQ just unmasked,
       * take it and redispatch — gpSP's execute_store_cpsr. */
      mask = cpsr_masks[pfield][PRIVMODE(reg[CPU_MODE])];
      reg[REG_CPSR] = (val & mask) | (reg[REG_CPSR] & ~mask);
      if (mask & 0xFF) {
        u32 vec = sh4_rebank_and_irq(pc + 4);
        if (vec) { reg[REG_PC] = vec; return 1; }
      }
    }
  }
  return 0;
}

int cgba_sh4_arm_swap(u32 opcode, u32 pc)
{
  CGBA_SH4_HELPER_HIT(arm_swap);
  u32 rn = (opcode >> 16) & 0xF;
  u32 rd = (opcode >> 12) & 0xF;
  u32 rm = opcode & 0xF;
  u32 is_byte = (opcode >> 22) & 1;
  u32 addr = reg[rn];
  /* SWP's interpreter advances PC after the read/write pair. */
  cgba_sh4_begin_mem_helper(pc);
  cgba_sh4_reset_mem_cycles(0);
  if (is_byte) {
    u32 tmp = execute_load_u8(addr);
    execute_store_u8(addr, reg[rm]);
    reg[rd] = tmp;
  } else {
    u32 tmp = execute_load_u32(addr);
    execute_store_u32(addr, reg[rm]);
    reg[rd] = tmp;
  }
  /* The store may have hit I/O (IRQ/HALT) or code (SMC); drain it so the block
   * exits with PC at the next instruction, like the normal store helpers. */
  return cgba_store_alert_break(pc + 4);
}

/* SWI 0x06/0x07 divide HLE.
 * The patched open BIOS wraps swi_Div/swi_DivARM (0x3E00/0x3E0C) to publish
 * r3 = ABS(quotient) matching the official GBA BIOS contract (required by
 * Minish Cap's direction helper), while preserving r2. */
static u32 cgba_sh4_abs_s32_bits(s32 v)
{
  return (v < 0) ? (0u - (u32)v) : (u32)v;
}

static inline u32 cgba_sh4_bitlen32(u32 v)
{
  static const u8 blen4[16] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4
  };
  u32 n = 0u;
  if (v >= 0x10000u) { v >>= 16; n += 16u; }
  if (v >= 0x100u)   { v >>= 8;  n += 8u;  }
  if (v >= 0x10u)    { v >>= 4;  n += 4u;  }
  return n + blen4[v];
}

/* Shared synchronous R13 cycle-budget snapshot for all entry-only speculative
 * loop fast paths (ARM mixer, PCM clamp, EA mixer, MP2K loop fold, Thumb udiv).
 * Never nested because each helper executes synchronously inside one C call. */
volatile s32 cgba_sh4_fastpath_budget;

