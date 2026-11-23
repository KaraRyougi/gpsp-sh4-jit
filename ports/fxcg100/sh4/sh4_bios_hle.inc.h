/* Internal module included by sh4_interp_helpers.c: BIOS IRQ/SWI HLE engines. */
#define CGBA_WS_S(reg_, w_) ((u32)ws_cyc_seq[(reg_) & 0xF][(w_)])
#define CGBA_WS_N(reg_, w_) ((u32)ws_cyc_nseq[(reg_) & 0xF][(w_)])

/* Dispatcher entry + return halves, common to every SWI the open BIOS serves.
 * Entry: 17 BIOS fetches (13 in core.s plus the 4-insn bounds-check trampoline
 * at 0x3EB0 branched to from 0x0070), B+B+table+B+BX nseq (5*N0),
 * {r11,r12,lr}+{spsr} pushes on the SVC stack (region 3), {r2,r3,lr} push on
 * the caller stack, and LDRB [lr,#-2] (works identically for ARM and Thumb
 * callers in LE memory).
 * Return: 6 BIOS fetches, {r2,r3,lr} pop (caller stack), {spsr}+{r11,r12,lr}
 * pops (SVC stack); MOVS pc,lr adds 0 for Thumb or S(lr,1) for ARM callers. */
static inline u32 cgba_swi_dispatch_entry_cycles(u32 sp_region, u32 lr_region)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  return 17u * S0 + 5u * N0 + 4u * CGBA_WS_S(3, 1) +
         3u * CGBA_WS_S(sp_region, 1) + CGBA_WS_N(lr_region, 0);
}

static inline u32 cgba_swi_dispatch_return_cycles(u32 sp_region, u32 lr_region,
                                                  int thumb)
{
  u32 S0 = CGBA_WS_S(0, 1);
  u32 c = 6u * S0 + 4u * CGBA_WS_S(3, 1) + 3u * CGBA_WS_S(sp_region, 1);
  if (!thumb)
    c += CGBA_WS_S(lr_region, 1);              /* MOVS pc tail (ARM caller) */
  return c;
}

static inline u32 cgba_swi_dispatch_cycles(u32 sp_region, u32 lr_region,
                                           int thumb)
{
  return cgba_swi_dispatch_entry_cycles(sp_region, lr_region) +
         cgba_swi_dispatch_return_cycles(sp_region, lr_region, thumb);
}

void cgba_sh4_hle_div(u32 divarm, u32 pc)
{
  CGBA_SH4_HELPER_HIT(hle_div);
  s32 num = divarm ? (s32)reg[1] : (s32)reg[0];
  s32 den = divarm ? (s32)reg[0] : (s32)reg[1];
  int thumb = (reg[REG_CPSR] & 0x20u) != 0;
  /* Open BIOS switches to SYSTEM mode before pushing {r2,r3,lr} and calling
   * the Div wrapper, so sp_r is always the SYSTEM/USER bank's SP region. */
  u32 sp_r = (((reg[CPU_MODE] == MODE_USER || reg[CPU_MODE] == MODE_SYSTEM)
               ? reg[REG_SP] : REG_MODE(MODE_SYSTEM)[5]) >> 24) & 0xFu;
  u32 lr_r = ((pc + (thumb ? 2u : 4u)) >> 24) & 0xFu;
  u32 disp_c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb) +
               CGBA_WS_N(sp_r, 1) + (divarm ? 5u : 0u);

  /* Match canonical SWI post-return BIOS open-bus value (cpu.cc / sh4_swi_handler). */
  reg[REG_BUS_VALUE] = 0xe3a02004u;

  if (__builtin_expect(den == 0, 0)) {
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
    cgba_sh4_hle_div_zero_count++;
#endif
    /* Match patched open BIOS (divide.s): q = (num < 0) ? -1 : 1, r = num, |q| = 1. */
    reg[0] = (num < 0) ? (u32)-1 : 1u;
    reg[1] = (u32)num;
    reg[3] = 1u;
    cgba_sh4_extra_cycles = (int)(disp_c + 24u);
    return;
  }

  u32 n = cgba_sh4_abs_s32_bits(num);
  u32 d = cgba_sh4_abs_s32_bits(den);
  u32 aq, ar, align;

#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  if (den == 1) {
    cgba_sh4_hle_div_one_count++;
  } else if (den == -1) {
    cgba_sh4_hle_div_neg_one_count++;
  } else if ((d & (d - 1u)) == 0u) {
    cgba_sh4_hle_div_pow2_count++;
  } else {
    cgba_sh4_hle_div_other_count++;
  }
#endif

  if (n < d) {
    aq = 0u;
    ar = n;
    align = 0u;
  } else {
    if ((d & (d - 1u)) == 0u) {
      if (d == 1u) {
        aq = n;
        ar = 0u;
      } else {
        u32 sh = cgba_sh4_bitlen32(d) - 1u;
        aq = n >> sh;
        ar = n & (d - 1u);
      }
    } else {
      aq = n / d;
      ar = n - aq * d;
    }
    if (__builtin_expect(n == 0x80000000u, 0)) {
      u32 dt = d;
      align = 0u;
      while (dt <= n && align < 31u) {
        dt <<= 1;
        align++;
      }
    } else {
      align = cgba_sh4_bitlen32(aq);
    }
  }

  /* Open-BIOS SWI dispatcher + r3 wrapper (+5 + N32(sp)) + Div body
   * 17A8..17FC (21 + 12*align) + optional DivArm swap (+5). */
  cgba_sh4_extra_cycles = (int)(disp_c + 26u + 12u * align);

  {
    u32 q_neg = (u32)(num ^ den) >> 31;
    u32 r_neg = (u32)num >> 31;
    reg[0] = q_neg ? (0u - aq) : aq;
    reg[1] = r_neg ? (0u - ar) : ar;
    /* Like the official BIOS (and the patched open BIOS), r3 = ABS(quotient).
     * Minish Cap's direction helper reads r3 after SWI 6. */
    reg[3] = aq;
  }
}

/* SWI trampoline target (sh4_stub.S execute_swi -> here). The stub has already
 * stashed the return address (next-instruction PC) into reg[REG_PC]. Vector to
 * the BIOS SWI handler exactly like the interpreter (cpu.cc:3516): bank into
 * Supervisor mode, save LR_svc / SPSR_svc, switch to ARM with IRQs disabled, set
 * the post-SWI open-bus value, and point PC at the 0x08 vector.
 *
 * Only the mode/bank/vector setup happens here — the emitted block then
 * redispatches to 0x08, because the block scan already registered this exit's
 * target as the BIOS vector (cpu_threaded.c:2957) and arm_swi/thumb_swi branch
 * there. gpSP loads the real open BIOS, so no SWI HLE dispatch is needed (the
 * divide HLE is handled separately by cgba_sh4_hle_div). */
void sh4_swi_handler(void)
{
  u32 return_pc = reg[REG_PC];                              /* stashed by execute_swi */
  REG_MODE(MODE_SUPERVISOR)[6] = return_pc;                 /* LR_svc = return address */
  REG_SPSR(MODE_SUPERVISOR) = reg[REG_CPSR];                /* SPSR_svc = CPSR */
  reg[REG_CPSR] = (reg[REG_CPSR] & ~0x3Fu) | 0x13u | 0x80u; /* ARM + SVC + IRQ off */
  set_cpu_mode(MODE_SUPERVISOR);
  reg[REG_BUS_VALUE] = 0xe3a02004u;                         /* post-SWI bios[0xE4] open-bus */
  reg[REG_PC] = 0x00000008u;                                /* SWI vector */
}

#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_bios_entry_swi, cgba_bios_entry_irq, cgba_bios_entry_other;
u32 cgba_bios_hle_irq_out;
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST) || CGBA_SH4_SWI_HLE_VERIFY
u32 cgba_bios_hle_irq_in;
#endif

/* ---- BIOS IRQ wrapper HLE --------------------------------------------------
 * Every raised IRQ used to make TWO interpreter round-trips: vector 0x18 runs
 * the 5-instruction BIOS dispatch stub, and the game handler's return lands on
 * the 2-instruction epilogue at 0x30 (AW fires ~119 IRQs/frame -> ~490k
 * fallback entries in the first 2000 frames, ~10% of all cycles in setup/exit
 * overhead alone). These two stubs of the shipped open BIOS are emulated here
 * state-identically (same pushes, same registers, same SPSR restore) and the
 * dispatcher never leaves native code. Anything unusual (non-IRQ mode, odd
 * handler address) falls back to the interpreter path unchanged.
 *
 * open_gba_bios.bin wrapper (disassembled):
 *   0x18: b 0x20
 *   0x20: stmdb sp!,{r0-r3,r12,lr}
 *   0x24: mov r0,#0x04000000
 *   0x28: mov lr,pc              ; lr = 0x30
 *   0x2c: ldr pc,[r0,#-4]        ; pc = [0x03007FFC]
 *   0x30: ldmia sp!,{r0-r3,r12,lr}
 *   0x34: subs pc,lr,#4          ; CPSR = SPSR_irq
 *
 * Timing: neither block_lookup_address_arm nor cgba_sh4_bios_fallback debits
 * cgba_sh4_extra_cycles (the budget register is not reachable from the lookup
 * resolver, while parked SWIs debit the exact 0x18..0x2C cost explicitly in
 * cgba_parked_swi_irq_exit). Save/restore cgba_sh4_extra_cycles so these stubs
 * never pollute a subsequent helper's extra-cycle counter. */
u32 cgba_hle_bios_irq_entry(void)
{
  int saved_extra = cgba_sh4_extra_cycles;
  u32 handler = execute_load_u32(0x03007FFCu);
  u32 sp;

  if ((handler & 3) || handler < 0x02000000u || handler >= 0x0E000000u) {
    cgba_sh4_extra_cycles = saved_extra;
    return 0;                      /* odd/garbage handler: interpreter path */
  }

  sp = reg[REG_SP] - 24;
  {
    /* The IRQ stack is linear RAM (0x03007Fxx by BIOS convention): push the
     * six registers with direct host stores instead of six accessor calls —
     * 138 IRQs/frame in AW. Same SMC contract as the per-word path. */
    u8 *tags = NULL;
    u8 *h = ((sp & 3) == 0) ? cgba_bulk_ram_host(sp, 24, &tags) : NULL;
    if (h) {
      cgba_le32_write(h +  0, reg[0]);
      cgba_le32_write(h +  4, reg[1]);
      cgba_le32_write(h +  8, reg[2]);
      cgba_le32_write(h + 12, reg[3]);
      cgba_le32_write(h + 16, reg[12]);
      cgba_le32_write(h + 20, reg[REG_LR]);
      if (cgba_smc_tags_nonzero(tags, 24))
        flush_translation_cache_ram();
    } else {
      execute_store_u32(sp +  0, reg[0]);
      execute_store_u32(sp +  4, reg[1]);
      execute_store_u32(sp +  8, reg[2]);
      execute_store_u32(sp + 12, reg[3]);
      execute_store_u32(sp + 16, reg[12]);
      execute_store_u32(sp + 20, reg[REG_LR]);
      if (cgba_store_alert) {      /* IRQ stack over tagged RAM code (rare) */
        cpu_alert_type a = cgba_store_alert;
        cgba_store_alert = CPU_ALERT_NONE;
        if (a & CPU_ALERT_SMC)
          flush_translation_cache_ram();
      }
    }
  }
  cgba_sh4_extra_cycles = saved_extra;
  reg[REG_SP] = sp;
  reg[REG_LR] = CGBA_BIOS_IRQ_RET;
  reg[0] = 0x04000000u;
  reg[REG_PC] = handler;
#if defined(CGBA_GPSP_HEADLESS_TEST) || CGBA_SH4_SWI_HLE_VERIFY
  cgba_bios_hle_irq_in++;
#endif
  return handler;
}

u32 cgba_hle_bios_irq_exit(void)
{
  int saved_extra = cgba_sh4_extra_cycles;
  u32 sp = reg[REG_SP];
  u32 spsr_v, ret_pc;

  {
    const u8 *h = ((sp & 3) == 0) ? cgba_bulk_ram_host(sp, 24, NULL) : NULL;
    if (h) {
      reg[0]      = cgba_le32_read(h +  0);
      reg[1]      = cgba_le32_read(h +  4);
      reg[2]      = cgba_le32_read(h +  8);
      reg[3]      = cgba_le32_read(h + 12);
      reg[12]     = cgba_le32_read(h + 16);
      reg[REG_LR] = cgba_le32_read(h + 20);
    } else {
      reg[0]       = execute_load_u32(sp +  0);
      reg[1]       = execute_load_u32(sp +  4);
      reg[2]       = execute_load_u32(sp +  8);
      reg[3]       = execute_load_u32(sp + 12);
      reg[12]      = execute_load_u32(sp + 16);
      reg[REG_LR]  = execute_load_u32(sp + 20);
    }
  }
  cgba_sh4_extra_cycles = saved_extra;
  reg[REG_SP]  = sp + 24;

  spsr_v = REG_SPSR(MODE_IRQ);
  ret_pc = (reg[REG_LR] - 4u) & ((spsr_v & 0x20u) ? ~1u : ~3u);
  reg[REG_BUS_VALUE] = 0xe55ec002u;
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
  reg[REG_PC] = ret_pc;

  /* Pending-IRQ recheck, mirroring the interpreter's check_for_interrupts
   * after an SPSR restore: re-enter the IRQ vector immediately. */
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && read_ioreg(REG_IME) &&
      (reg[REG_CPSR] & 0x80) == 0) {
    reg[REG_BUS_VALUE] = 0xe25ef004u;
    REG_MODE(MODE_IRQ)[6] = ret_pc + 4;
    REG_SPSR(MODE_IRQ) = reg[REG_CPSR];
    reg[REG_CPSR] = 0xD2;
    reg[REG_PC] = CGBA_BIOS_IRQ_VEC;
    set_cpu_mode(MODE_IRQ);
  }
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_irq_out++;
#endif
  return reg[REG_PC];
}

extern int cgba_dynarec_single_block;

/* ---- BIOS SWI HLE ----------------------------------------------------------
 * The copy/decompress SWI family dominates interpreter residency in games
 * that stream graphics (SMA2: 20.6M interpreted BIOS instructions in the
 * first 2000 frames — 10k/frame — nearly all LZ77/CpuSet loops). These are
 * faithful ports of the vendored open-BIOS C sources
 * (vendor/gpsp/bios/source/softwareinterrupts.c), run through the charging
 * execute_load/store accessors so data-access cycles and SMC alerts behave
 * exactly like the interpreted loops; only the BIOS's own instruction
 * fetches are uncharged (same accepted envelope as the IRQ-wrapper HLE).
 * Registers: the real routines are compiler-generated C behind a dispatcher
 * — games cannot rely on scratch regs; r0-r3 are left as the inputs. */
/* ---- bulk copy fast path for CpuSet / CpuFastSet --------------------------
 * The SWI HLEs copied per-word through the charging execute_load/store
 * accessors (~40-80 host insns per guest word); AW stages tilemaps with
 * these every frame (execute_store_u32 + read_memory32 ~6.5% of steady
 * state). When source and destination ranges each sit linearly inside one
 * always-mapped region, do a host memmove instead: guest RAM is stored as
 * LE bytes in every host array, so bytewise copies need no swapping.
 * Cycle charging replicates the per-word loop EXACTLY (count * per-region
 * wait states under the same seq flag), so guest-visible timing is
 * unchanged. SMC follows the store path's contract: any nonzero tag byte
 * under the destination raises CPU_ALERT_SMC for the HLE tail to consume.
 * Palette/OAM/IO destinations keep the per-word path (converted-palette
 * and OAM_UPDATED side effects); ROM sources must sit in one mapped 32KB
 * page. */
static u8 *cgba_bulk_ram_host(u32 addr, u32 len, u8 **tags)
{
  u32 off;
  switch (addr >> 24) {
  case 2:
    off = addr & 0x3FFFFu;
    if (off + len > 0x40000u) return NULL;
    if (tags) *tags = ewram + 0x40000u + off;
    return ewram + off;
  case 3:
    off = addr & 0x7FFFu;
    if (off + len > 0x8000u) return NULL;
    if (tags) *tags = iwram + off;
    return iwram + 0x8000u + off;
  case 6:
    off = addr & 0x1FFFFu;
    if (off >= 0x18000u)
      off -= 0x8000u;
    if (off + len > 0x18000u) return NULL;
    if (tags) {
      cgba_video_flush_before_write();
      *tags = NULL;                 /* VRAM: no tag mirror */
      mark_vram_dirty_span(off, len);
    }
    return vram + off;
  default:
    return NULL;
  }
}

/* Unified contiguous guest read-span resolver across mapped 32 KiB ROM pages,
 * 4 KiB fragmented NOR blocks (excluding region 0x0D EEPROM), EWRAM, IWRAM,
 * and VRAM (including its 0x18000..0x1FFFF 32 KiB mirror). */
static inline const u8 *cgba_resolve_guest_read_span(u32 addr, u32 *avail_out)
{
  u32 region = addr >> 24;
  if (region >= 8u && region <= 0xDu) {
    u8 *page = memory_map_read[(addr & 0x0FFFFFFFu) >> 15];
    if (page) {
      u32 off = addr & 0x7FFFu;
      *avail_out = 0x8000u - off;
      return page + off;
    }
    if (region <= 0xCu) {
      u32 rom_off = addr & 0x01FFFFFFu;
      if (rom_off < gamepak_size) {
        u8 *blk = cgba_gamepak_resolve_4k(addr);
        if (blk) {
          u32 off4 = addr & 0x0FFFu;
          u32 rem_blk = 0x1000u - off4;
          u32 rem_rom = gamepak_size - rom_off;
          *avail_out = rem_blk < rem_rom ? rem_blk : rem_rom;
          return blk + off4;
        }
      }
    }
    return NULL;
  }
  if (region == 2u) {
    u32 off = addr & 0x3FFFFu;
    *avail_out = 0x40000u - off;
    return ewram + off;
  }
  if (region == 3u) {
    u32 off = addr & 0x7FFFu;
    *avail_out = 0x8000u - off;
    return iwram + 0x8000u + off;
  }
  if (region == 6u) {
    u32 off = addr & 0x1FFFFu;
    if (off >= 0x18000u)
      off -= 0x8000u;
    *avail_out = 0x18000u - off;
    return vram + off;
  }
  return NULL;
}

static const u8 *cgba_bulk_src_host(u32 addr, u32 len)
{
  u32 avail = 0u;
  const u8 *p = cgba_resolve_guest_read_span(addr, &avail);
  if (p && len <= avail)
    return p;
  return NULL;
}

static int cgba_bulk_ranges_overlap(const u8 *a, const u8 *b, u32 len)
{
  uintptr_t aa = (uintptr_t)a;
  uintptr_t bb = (uintptr_t)b;
  return aa <= bb ? bb - aa < (uintptr_t)len
                  : aa - bb < (uintptr_t)len;
}

/* The ordinary faithful HLE intentionally declines all overlap.  The opt-in
 * extension accepts only dst>src overlap, which can be reproduced exactly by
 * re-reading each source element after earlier destination writes. */
static int cgba_swi_copy_overlap_ok(u32 src, u32 dst, u32 len)
{
  if (dst - src >= len && src - dst >= len)
    return 1;                                  /* disjoint guest ranges */
#if CGBA_SH4_SWI_FORWARD_OVERLAP
  return dst > src;                            /* forward overlap only */
#else
  return 0;
#endif
}

/* ---- register- and cycle-faithful CpuSet / CpuFastSet ---------------------
 * Reproduces the open BIOS routines EXACTLY as the interpreter would run
 * them — post-SWI registers and cycle charge included — while doing the
 * memory work at host speed. Derived from open_gba_bios.bin: the SWI
 * dispatcher (0x64) stacks r2/r3/r11/r12/lr around the routine, so ONLY
 * r0/r1 carry routine clobbers back to the caller:
 *
 *   CpuSet 0x614   half-copy: r1 = dst - src
 *                  half-fill: r1 = dst + 2*count
 *                  word-copy: r1 = (dst&~3) - (src&~3), r0 = last word
 *                  word-fill: r1 = (dst&~3) + 4*count
 *   CpuFastSet 0x720 fill:    r1 = (dst&~3) + 4*ceil8(count)  (writes ceil8!)
 *                  copy:      r1 = (dst&~3) - (src&~3),
 *                             r0 = (src&~3) + 4*ceil8(count)
 *   early-outs (source region bits 25-27 zero, end region zero, count 0):
 *                  r0/r1 unchanged.
 *
 * The cycle model mirrors the interpreter's charging (per-insn seq fetch of
 * the region the pc lands in, extra nseq for taken B/BX targets, nseq per
 * single data access, seq per LDM/STM word; MOVS pc,lr to a Thumb caller
 * charges nothing — the interpreter's spsr-restore path jumps straight to
 * thumb_loop). Instruction traces are counted from the disassembly; the
 * CGBA_SH4_SWI_HLE_VERIFY build predicts (r0, r1, cycles) and then lets the
 * interpreter run the real BIOS, reporting any mismatch on the debug port.
 *
 * Anything the formulas do not cover EXACTLY falls back to the interpreted
 * BIOS (return 0): unaligned halfword operands (the BIOS does not mask
 * them; the interpreter's rotated-read semantics apply), source/dest
 * ranges that cross a 0x01000000 region boundary (per-access charges would
 * change region mid-run), open-bus sources, unresolvable/overlapping host
 * ranges, and ARM-state callers (rare; keeps the MOVS-pc tail term out of
 * the model). */

struct cgba_swi_pred {
  u32 r0, r1;
  u32 cycles;
  int r0_from_last_word;   /* word-copy: r0 = last source word */
};

/* Waitstate & SWI dispatcher helpers (CGBA_WS_S/N, cgba_swi_dispatch_*) live
 * above cgba_sh4_hle_div so Div/DivArm and SWI HLE share one exact model. */

static inline int cgba_swi_range_in_region(u32 addr, u32 len)
{
  return len > 0u &&
         addr + len >= addr &&
         ((addr + len - 1u) >> 24) == (addr >> 24) &&
         addr + len <= 0x0F000000u;
}

/* Resolve a same-region, in-bounds, non-open-bus guest range to host, or
 * NULL. width_mask: alignment the FORMULA requires (the BIOS itself masks
 * word ops; halfword ops must already be aligned). */
static u8 *cgba_swi_exact_host(u32 addr, u32 len, u8 **tags)
{
  if (!cgba_swi_range_in_region(addr, len))
    return NULL;
  return cgba_bulk_ram_host(addr, len, tags);
}

static const u8 *cgba_swi_exact_src_host(u32 addr, u32 len)
{
  if (!cgba_swi_range_in_region(addr, len))
    return NULL;
  return cgba_bulk_src_host(addr, len);
}

/* Predict CpuSet: returns 1 with *pred filled, 0 = not modeled (interp).
 * Supports both ARM and Thumb callers.
 * Accounting convention: every executed BIOS instruction adds S0 (the
 * interpreter's per-insn tail charges the region the pc lands in — BIOS
 * throughout); every TAKEN branch adds N0 on top; data accesses add the
 * listed nseq/seq terms. EXIT = 0x684 LDM {r4} + 0x688 BX lr. */
static int cgba_swi_cpuset_predict(u32 src, u32 dst, u32 cnt,
                                   struct cgba_swi_pred *pred)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  int thumb = (REG_SPSR(MODE_SUPERVISOR) & 0x20u) != 0u;
  u32 sp_r = REG_MODE(MODE_SYSTEM)[5] >> 24;
  u32 lr_r = (reg[REG_LR] & (thumb ? ~1u : ~3u)) >> 24;
  u32 count = cnt & 0x1FFFFFu;
  u32 word = (cnt >> 26) & 1, fill = (cnt >> 24) & 1;
  u32 end = src + (((cnt << 11) >> 9) & ~0xE00000u);
  u32 exit_c, c;

  c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb);
  c += CGBA_WS_N(sp_r, 1);                     /* 618 STR r4 (single store) */
  exit_c = 2 * S0 + CGBA_WS_S(sp_r, 1) + N0;   /* 684 LDM + 688 BX lr */
  pred->r0 = src; pred->r1 = dst; pred->r0_from_last_word = 0;

  if (!(src & 0x0E000000u)) {                  /* eo1: 614,618,61C(taken) */
    pred->cycles = c + 3 * S0 + N0 + exit_c;
    return 1;
  }
  if (!(end & 0x0E000000u)) {                  /* eo2: ..634(taken) */
    pred->cycles = c + 9 * S0 + N0 + exit_c;
    return 1;
  }
  /* through 644: 12*S0 accumulated by each path below */

  if (!word) {
    if (!fill) {                               /* ---- half copy ---- */
      if (count == 0) {                        /* ..654(taken): 17 insns */
        pred->cycles = c + 17 * S0 + N0 + exit_c;
        return 1;
      }
      if ((src | dst) & 1)
        return 0;
      if (!cgba_swi_exact_src_host(src, count * 2) ||
          !cgba_swi_range_in_region(dst, count * 2))
        return 0;
      if (!cgba_swi_copy_overlap_ok(src, dst, count * 2))
        return 0;
      c += 21 * S0;                            /* prologue through 664 */
      c += count * (7 * S0 + CGBA_WS_N(src >> 24, 0) + CGBA_WS_N(dst >> 24, 0));
      c += (count - 1) * N0;
      pred->r1 = dst - src;
      pred->cycles = c + exit_c;
      return 1;
    }
    /* ---- half fill: 64C taken ---- */
    if (src & 1)
      return 0;
    if (!cgba_swi_exact_src_host(src, 2))
      return 0;
    if (count == 0) {                          /* ..6DC(taken): 20 insns */
      pred->cycles = c + 20 * S0 + CGBA_WS_N(src >> 24, 0) + 2 * N0 + exit_c;
      return 1;
    }
    if (dst & 1)
      return 0;
    if (!cgba_swi_range_in_region(dst, count * 2))
      return 0;
    c += 22 * S0 + CGBA_WS_N(src >> 24, 0) + N0;   /* through 6E0 + 64C-taken */
    c += count * (3 * S0 + CGBA_WS_N(dst >> 24, 0));
    c += (count - 1) * N0;
    c += N0;                                   /* 6F0 B 684 (fetch in the 22) */
    pred->r1 = dst + count * 2;
    pred->cycles = c + exit_c;
    return 1;
  }

  /* ---- word paths: 644 taken ---- */
  {
    u32 wsrc = src & ~3u, wdst = dst & ~3u;
    if (!fill) {                               /* ---- word copy ---- */
      if (count == 0) {                        /* ..6A8(taken): 21 insns */
        pred->cycles = c + 21 * S0 + 2 * N0 + exit_c;
        return 1;
      }
      if (!cgba_swi_exact_src_host(wsrc, count * 4) ||
          !cgba_swi_range_in_region(wdst, count * 4))
        return 0;
      if (!cgba_swi_copy_overlap_ok(wsrc, wdst, count * 4))
        return 0;
      c += 22 * S0 + N0;                       /* through 6A8(nt) + 6C8 B + 644 */
      c += count * (7 * S0 + CGBA_WS_N(wsrc >> 24, 1) + CGBA_WS_N(wdst >> 24, 1));
      c += (count - 1) * N0;
      c += N0;                                 /* 6C8 B 684 */
      pred->r1 = wdst - wsrc;
      pred->r0_from_last_word = 1;
      pred->cycles = c + exit_c;
      return 1;
    }
    /* ---- word fill: 644 + 698 taken ---- */
    if (!cgba_swi_exact_src_host(wsrc, 4))
      return 0;
    if (count == 0) {                          /* ..704(taken): 22 insns */
      pred->cycles = c + 22 * S0 + CGBA_WS_N(wsrc >> 24, 1) + 3 * N0 + exit_c;
      return 1;
    }
    if (!cgba_swi_range_in_region(wdst, count * 4))
      return 0;
    c += 23 * S0 + CGBA_WS_N(wsrc >> 24, 1) + 2 * N0;  /* 644,698 taken */
    c += count * (3 * S0 + CGBA_WS_N(wdst >> 24, 1));
    c += (count - 1) * N0;
    c += N0;                                   /* 714 B 684 */
    pred->r1 = wdst + count * 4;
    pred->cycles = c + exit_c;
    return 1;
  }
}

/* Predict CpuFastSet (ARM & Thumb callers); EXIT = 0x78C LDM + 0x790 BX. */
static int cgba_swi_cpufastset_predict(u32 src, u32 dst, u32 cnt,
                                       struct cgba_swi_pred *pred)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  int thumb = (REG_SPSR(MODE_SUPERVISOR) & 0x20u) != 0u;
  u32 sp_r = REG_MODE(MODE_SYSTEM)[5] >> 24;
  u32 lr_r = (reg[REG_LR] & (thumb ? ~1u : ~3u)) >> 24;
  u32 count = cnt & 0x1FFFFFu;
  u32 fill = (cnt >> 24) & 1;
  u32 end = src + (((cnt << 11) >> 9) & ~0xE00000u);
  u32 wsrc = src & ~3u, wdst = dst & ~3u;
  u32 K = (count + 7) / 8;
  u32 exit_c, c;

  c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb);
  c += CGBA_WS_N(sp_r, 1);                     /* 724 STR r4 */
  exit_c = 2 * S0 + CGBA_WS_S(sp_r, 1) + N0;   /* 78C LDM + 790 BX */
  pred->r0 = src; pred->r1 = dst; pred->r0_from_last_word = 0;

  if (!(src & 0x0E000000u)) {                  /* eo1: 720,724,728(taken) */
    pred->cycles = c + 3 * S0 + N0 + exit_c;
    return 1;
  }
  if (!(end & 0x0E000000u)) {                  /* eo2: ..740(taken) */
    pred->cycles = c + 9 * S0 + N0 + exit_c;
    return 1;
  }

  if (fill) {                                  /* 758 NOT taken (bit set) */
    if (!cgba_swi_exact_src_host(wsrc, 4))
      return 0;
    if (count == 0) {                          /* ..76C(taken): 20 insns */
      pred->cycles = c + 20 * S0 + CGBA_WS_N(wsrc >> 24, 1) + N0 + exit_c;
      return 1;
    }
    if (!cgba_swi_range_in_region(wdst, K * 32))
      return 0;
    c += 20 * S0 + CGBA_WS_N(wsrc >> 24, 1);   /* through 76C(nt) */
    c += K * (28 * S0 + 8 * CGBA_WS_N(wdst >> 24, 1) + 7 * N0);
    c += (K - 1) * N0;
    pred->r1 = wdst + K * 32;
    pred->cycles = c + exit_c;
    return 1;
  }
  /* copy: 758 taken */
  if (count == 0) {                            /* ..7A0(taken): 19 insns */
    pred->cycles = c + 19 * S0 + 2 * N0 + exit_c;
    return 1;
  }
  if (!cgba_swi_exact_src_host(wsrc, K * 32) ||
      !cgba_swi_range_in_region(wdst, K * 32))
    return 0;
  if (!cgba_swi_copy_overlap_ok(wsrc, wdst, K * 32))
    return 0;
  c += 20 * S0 + N0;                           /* through 7A0(nt) + 7D0 B fetch, 758 taken */
  /* chunk = 0x7A4 ADD + 8 x (CMP,LDR,cond-LDR,STR,ADD,CMP,BNE) + SUB,CMP,BGT
     = 1 + 56 + 3 fetches (trace-verified; the inner BNE's own fetch counts). */
  c += K * (60 * S0 + 8 * (CGBA_WS_N(wsrc >> 24, 1) + CGBA_WS_N(wdst >> 24, 1)) + 7 * N0);
  c += (K - 1) * N0;
  c += N0;                                     /* 7D0 B 78C */
  pred->r1 = wdst - wsrc;
  pred->r0 = wsrc + K * 32;
  pred->cycles = c + exit_c;
  return 1;
}


/* ---- Parked/resumable CpuFastSet ------------------------------------------
 * Oversized FastSets (predicted cost exceeding the current event slice) can't
 * be HLE'd atomically: the interpreter interleaves update_gba mid-copy, and
 * IRQs vector between iterations. Instead we execute the routine in budgeted
 * 8-word chunks from a CANONICAL machine state: registers, stacks, mode and
 * PC always equal what the real BIOS interpreter would show at the chunk-top
 * PC. Parking = leaving that state in place; if anything unexpected happens
 * (savestate load, odd resume, validation failure) the interpreter simply
 * continues the real BIOS from it, bit-exact by construction.
 *
 * Open-BIOS geometry (vendor/gpsp/bios/open_gba_bios.bin, trace-verified):
 *   dispatcher 0x64: STMDB sp_svc!,{r11,r12,lr}; ... STMDB sp_svc!,{spsr};
 *     switch to SYSTEM ((spsr&0x80)|0x1F, NZCV cleared);
 *     STMDB sp_usr!,{r2,r3,lr}; lr_usr = 0x94; jump table -> 0x720.
 *   FastSet 0x720: STR r4,[sp_usr,-4]!; entry checks;
 *     copy loop top 0x7A4: r3=src cursor, r1=dst-src, r4=words left (raw),
 *       r12=0x0EFFFFFF, r0=prev chunk end, r2=last loaded word.
 *     fill loop top 0x770: r1=dst cursor, r2=fill word, r4=words left,
 *       r3=prev chunk end, r12=0x720 (jump-table leftover), r0=caller r0.
 *   exit: LDM sp_usr!,{r4}; return to 0x94: LDMIA sp_usr!,{r2,r3,lr};
 *     back to SVC (0xD3, NZCV cleared, r12=0xD3); pop spsr;
 *     LDMIA sp_svc!,{r11,r12,lr}; MOVS pc,lr. Only r0/r1 escape. */
#ifdef CGBA_GPSP_HEADLESS_TEST
extern u32 cgba_bios_hle_swi_count;
#endif

#define CGBA_FS_DECLINE   0xFFFFFFFFu

static u32 cgba_parked_swi_finish(s32 remaining);

static inline void cgba_parked_swi_drain_store_alert(void)
{
  if (__builtin_expect(cgba_store_alert != CPU_ALERT_NONE, 0)) {
    cpu_alert_type a = cgba_store_alert;
    cgba_store_alert = CPU_ALERT_NONE;
    if (a & CPU_ALERT_SMC)
      flush_translation_cache_ram();
  }
}

/* Shared prologue for all parked SWI engines (FastSet/CpuSet/ObjAffine/LZ77):
 * mirrors open-BIOS dispatcher 0x64..0x90 (SVC push {spsr,r11,r12,lr}, switch
 * to SYSTEM mode, user push {r2,r3,lr_usr}, then routine push {r4..r(3+n_extra)}).
 * Preserves cgba_sh4_extra_cycles and returns sp_usr >> 24. */
static u32 cgba_parked_swi_enter(u32 n_extra, u32 *lr_r_out)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 sp_svc = reg[REG_SP];
  u32 sp_usr, i;
  int saved_extra = cgba_sh4_extra_cycles;

  if (lr_r_out)
    *lr_r_out = (reg[REG_LR] & ~1u) >> 24;

  /* Dispatcher pushes on the SVC stack */
  execute_store_u32(sp_svc - 4, reg[REG_LR]);
  execute_store_u32(sp_svc - 8, reg[12]);
  execute_store_u32(sp_svc - 12, reg[11]);
  execute_store_u32(sp_svc - 16, spsr_v);
  reg[REG_SP] = sp_svc - 16;

  /* Switch to SYSTEM mode exactly like MSR CPSR_9 at 0x84 (NZCV cleared,
   * caller's I bit kept, F cleared). Set dispatcher's r11 before routine
   * pushes so routines pushing r11 (e.g. LZ77UnCompVram) save the exact value. */
  reg[REG_CPSR] = (spsr_v & 0x80u) | 0x1Fu;
  set_cpu_mode(MODE_SYSTEM);
  reg[11] = (spsr_v & 0x80u) | 0x1Fu;

  sp_usr = reg[REG_SP];
  execute_store_u32(sp_usr - 4, reg[REG_LR]);
  execute_store_u32(sp_usr - 8, reg[3]);
  execute_store_u32(sp_usr - 12, reg[2]);
  for (i = 0; i < n_extra; i++) {
    u32 r = 4u + (n_extra - 1u - i);
    execute_store_u32(sp_usr - 16u - i * 4u, reg[r]);
  }
  reg[REG_SP] = sp_usr - (12u + n_extra * 4u);
  cgba_sh4_extra_cycles = saved_extra;
  cgba_parked_swi_drain_store_alert();
  reg[REG_LR] = 0x00000094u;
  return sp_usr >> 24;
}

/* Shared epilogue for all parked SWI engines: pops routine {r4..r(3+n_extra)}
 * and dispatcher {r2,r3,lr_usr} from sp_usr, switches back to SVC, pops
 * {spsr,r11,r12,lr} from sp_svc, restores caller CPSR/mode/PC (ARM or Thumb),
 * and outputs the dispatcher return-half cycles via *disp_ret_cycles_out.
 * Returns sp_usr >> 24. */
static u32 cgba_parked_swi_leave(u32 n_extra, u32 *disp_ret_cycles_out)
{
  u32 sp_usr = reg[REG_SP];
  u32 sp_r = sp_usr >> 24;
  u32 sp_svc, spsr_pop, i;
  int thumb;
  int saved_extra = cgba_sh4_extra_cycles;

  for (i = 0; i < n_extra; i++)
    reg[4u + i] = execute_load_u32(sp_usr + i * 4u);
  reg[2]      = execute_load_u32(sp_usr + n_extra * 4u);
  reg[3]      = execute_load_u32(sp_usr + n_extra * 4u + 4u);
  reg[REG_LR] = execute_load_u32(sp_usr + n_extra * 4u + 8u);
  reg[REG_SP] = sp_usr + 12u + n_extra * 4u;

  reg[12] = 0x000000D3u;
  reg[REG_CPSR] = 0x000000D3u;
  set_cpu_mode(MODE_SUPERVISOR);
  sp_svc = reg[REG_SP];
  spsr_pop = execute_load_u32(sp_svc);
  reg[11] = execute_load_u32(sp_svc + 4);
  reg[12] = execute_load_u32(sp_svc + 8);
  reg[REG_LR] = execute_load_u32(sp_svc + 12);
  reg[REG_SP] = sp_svc + 16;
  cgba_sh4_extra_cycles = saved_extra;

  thumb = (spsr_pop & 0x20u) != 0u;
  REG_SPSR(MODE_SUPERVISOR) = spsr_pop;
  reg[REG_PC] = reg[REG_LR] & (thumb ? ~1u : ~3u);
  reg[REG_CPSR] = spsr_pop;
  set_cpu_mode(cpu_modes[spsr_pop & 0xF]);
  if (disp_ret_cycles_out)
    *disp_ret_cycles_out = cgba_swi_dispatch_return_cycles(
      sp_r, reg[REG_PC] >> 24, thumb);
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_swi_count++;
#endif
  return sp_r;
}

/* Shared IRQ-entry exit for parked SWI engines: mirrors execute_arm's
 * 0x18..0x2C cycle cost so IRQs firing inside a parked SWI stay cycle-locked
 * to the interpreter. Always exits via cgba_parked_swi_finish() so negative
 * remaining budgets can never leak bit-31 to sh4_stub.S (frame-complete). */
static u32 cgba_parked_swi_irq_exit(s32 remaining, u32 S0, u32 N0)
{
  if (reg[REG_PC] == CGBA_BIOS_IRQ_VEC && reg[CPU_MODE] == MODE_IRQ) {
    int saved_extra = cgba_sh4_extra_cycles;
    u32 irq_sp_r = reg[REG_SP] >> 24;
    u32 handler = cgba_hle_bios_irq_entry();
    cgba_sh4_extra_cycles = saved_extra;
    if (handler != 0u) {
      remaining -= (s32)(4 * S0 + N0 + 6 * CGBA_WS_S(irq_sp_r, 1)
                         + CGBA_WS_N(3, 1)
                         + CGBA_WS_S(handler >> 24, 1));
    }
  }
  return cgba_parked_swi_finish(remaining);
}

/* Shared mid-loop park / event-interleave step for parked SWI engines.
 * Returns 1 with *exit_out filled if the engine must yield (frame complete or
 * IRQ vectored); returns 0 with updated *remaining to keep executing. */
static int cgba_parked_swi_park(s32 *remaining, u32 loop_pc, u32 S0, u32 N0,
                                u32 *exit_out)
{
  u32 ret = update_gba(*remaining);
  if (completed_frame(ret)) {
    *exit_out = 0x80000000u;
    return 1;
  }
  *remaining = (s32)cycles_to_run(ret);
  if (reg[REG_PC] != loop_pc) {
    *exit_out = cgba_parked_swi_irq_exit(*remaining, S0, N0);
    return 1;
  }
  return 0;
}

/* Shared post-epilogue fallback return contract for parked SWI engines.
 * Carries overshoot past update_gba()'s -64 clamp so heavy final runs (e.g.
 * LZ77/RL tokens) never drop guest cycles, and guarantees return is either
 * 0x80000000 (frame complete) or a strictly non-negative remaining budget. */
static u32 cgba_parked_swi_finish(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 ret_pc = reg[REG_PC];
  while (remaining <= 0) {
    s32 carry = 0;
    s32 pass = remaining;
    u32 ret;
    if (pass < -64) {
      carry = -pass;
      pass = 0;
    }
    ret = update_gba(pass);
    if (completed_frame(ret))
      return 0x80000000u;
    if (reg[REG_PC] != ret_pc &&
        reg[REG_PC] == CGBA_BIOS_IRQ_VEC && reg[CPU_MODE] == MODE_IRQ)
      return cgba_parked_swi_irq_exit((s32)cycles_to_run(ret), S0, N0);
    remaining = (s32)cycles_to_run(ret) - carry;
  }
  return (u32)remaining;
}

static u32 cgba_fs_entry_cost;    /* handoff: dispatch+prologue cycles */

/* Materialize the canonical loop-top state for a dispatched oversized
 * FastSet (pc==8, SVC, predict-ok). Mirrors dispatcher + routine prologue. */
static void cgba_swi_fastset_materialize(u32 fill)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 wsrc = reg[0] & ~3u;
  u32 sp_r, lr_r;

  sp_r = cgba_parked_swi_enter(1u, &lr_r);

  /* routine entry effects + loop registers; charge entry-half + prologue */
  cgba_fs_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                     + CGBA_WS_N(sp_r, 1);               /* 724 STR r4 */
  reg[4] = reg[2] & 0x1FFFFFu;                           /* words left (raw) */
  if (fill) {
    int saved_extra = cgba_sh4_extra_cycles;
    reg[3] = wsrc;
    reg[2] = execute_load_u32(wsrc);                     /* 760 LDR value */
    cgba_sh4_extra_cycles = saved_extra;
    reg[1] = reg[1] & ~3u;                               /* dst cursor */
    reg[12] = 0x00000720u;                               /* table leftover */
    cgba_fs_entry_cost += 20 * S0 + CGBA_WS_N(wsrc >> 24, 1);
    reg[REG_PC] = CGBA_FS_FILL_LOOP;
  } else {
    reg[3] = wsrc;                                       /* src cursor */
    reg[1] = (reg[1] & ~3u) - wsrc;                      /* delta */
    reg[12] = 0x0EFFFFFFu;
    cgba_fs_entry_cost += 20 * S0 + N0;
    reg[REG_PC] = CGBA_FS_COPY_LOOP;
  }
}

/* Budgeted chunk executor from a canonical loop-top state. Returns a
 * fallback-contract value, or CGBA_FS_DECLINE to let the interpreter run
 * the (still canonical) state natively. */
static u32 cgba_swi_fastset_engine(s32 remaining)
{
  int fill = (reg[REG_PC] == CGBA_FS_FILL_LOOP);
  u32 loop_pc = fill ? CGBA_FS_FILL_LOOP : CGBA_FS_COPY_LOOP;
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src, dst, chunk_c, total_bytes;
  const u8 *src_full = NULL;
  s32 words;

  words = (s32)reg[4];
  if (words <= 0 || words > 0x200000)
    return CGBA_FS_DECLINE;                    /* not a mid-copy state */
  total_bytes = (((u32)words + 7u) / 8u) * 32u;
  if (fill) {
    if (reg[12] != CGBA_BIOS_FASTSET_ENTRY)
      return CGBA_FS_DECLINE;
    src = reg[3];                              /* unused; kept canonical */
    dst = reg[1];
    if ((dst & 3u) != 0u || !cgba_swi_range_in_region(dst, total_bytes))
      return CGBA_FS_DECLINE;
    chunk_c = 28 * S0 + 8 * CGBA_WS_N(dst >> 24, 1) + 7 * N0 + N0;
  } else {
    if (reg[12] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[3];
    dst = reg[3] + reg[1];
    if (((src | dst) & 3u) != 0u ||
        !cgba_swi_range_in_region(dst, total_bytes) ||
        !cgba_swi_copy_overlap_ok(src, dst, total_bytes))
      return CGBA_FS_DECLINE;
    /* Validate the entire source span BEFORE debiting entry_cost or running
     * any chunk so the engine never declines mid-copy on fragmented NOR. */
    src_full = cgba_swi_exact_src_host(src, total_bytes);
    if (!src_full)
      return CGBA_FS_DECLINE;
    chunk_c = 60 * S0
            + 8 * (CGBA_WS_N(src >> 24, 1) + CGBA_WS_N(dst >> 24, 1))
            + 7 * N0 + N0;
  }

  remaining -= (s32)cgba_fs_entry_cost;
  cgba_fs_entry_cost = 0;

  while (1) {
    if (words <= 0) {
      u32 disp_ret_c = 0u;
      u32 sp_r = cgba_parked_swi_leave(1u, &disp_ret_c);
      remaining -= (s32)(disp_ret_c + 2 * S0 + CGBA_WS_S(sp_r, 1) +
                         (fill ? 0u : N0));    /* 78C+790 (+0 on fill: last BGT nt) */
      break;                                   /* leave via common return */
    }
    if (remaining <= 0) {
      u32 park_exit;
      if (cgba_parked_swi_park(&remaining, loop_pc, S0, N0, &park_exit))
        return park_exit;
      continue;
    }
    {
      s32 fit = remaining / (s32)chunk_c;
      u32 chunks_left = ((u32)words + 7) / 8;
      u32 k = (fit < 1) ? 1u : ((u32)fit > chunks_left ? chunks_left
                                                       : (u32)fit);
      u32 bytes = k * 32u;
      u8 *tags = NULL;
      u8 *dh = cgba_bulk_ram_host(dst, bytes, &tags);
      if (fill) {
        u8 fbytes[4];
        cgba_le32_write(fbytes, reg[2]);
        if (dh) {
          int smc_changed = cgba_smc_fill_changes_tagged(dh, fbytes, 4u, tags, bytes);
          if (reg[2] == 0u) {
            memset(dh, 0, bytes);
          } else {
            u32 i;
            for (i = 0; i < bytes; i += 4)
              cgba_le32_write(dh + i, reg[2]);
          }
          if (smc_changed)
            flush_translation_cache_ram();     /* SMC: see note at copy site */
        } else if (!cgba_swi_bulk_video_fill(dst, fbytes, 4u, bytes)) {
          int saved = cgba_sh4_extra_cycles;
          u32 i;
          for (i = 0; i < bytes; i += 4)
            execute_store_u32(dst + i, reg[2]);
          cgba_sh4_extra_cycles = saved;
          cgba_parked_swi_drain_store_alert();
        }
        dst += bytes;
        reg[1] = dst;
        reg[3] = dst;                          /* chunk-top canonical */
      } else {
        const u8 *sh = src_full;
        u32 last_word = cgba_le32_read(sh + bytes - 4);
        if (dh) {
          int smc_changed;
#if CGBA_SH4_SWI_FORWARD_OVERLAP
          if (cgba_sh4_swi_is_forward_overlap(dh, sh, bytes)) {
            smc_changed = cgba_smc_tags_nonzero(tags, bytes);
            cgba_sh4_swi_copy_forward(dh, sh, bytes, 4);
            last_word = cgba_le32_read(dh + bytes - 4);
          } else
#endif
          {
            smc_changed = cgba_smc_copy_changes_tagged(dh, sh, tags, bytes);
#ifdef __sh__
            if (bytes >= 64u && ((((uintptr_t)dh | (uintptr_t)sh) & 3u) == 0u) &&
                ((uintptr_t)sh >= (uintptr_t)dh + bytes ||
                 (uintptr_t)dh >= (uintptr_t)sh + bytes)) {
              extern void cgba_dma_copy_movca_sh4(void *dst, const void *src, u32 nbytes);
              cgba_dma_copy_movca_sh4(dh, sh, bytes);
            } else
#endif
              memmove(dh, sh, bytes);
          }
          /* The copy just overwrote tagged RAM code. The engine's return and
             park points do NOT run the common HLE tail, so flush the RAM
             translation cache HERE — otherwise a stale block could execute
             at the next park (ISR) or on completion. Safe mid-copy: the
             engine's state lives in guest memory + registers, not the cache,
             and dh/tags point into the GBA RAM arrays, not the cache. */
          if (smc_changed)
            flush_translation_cache_ram();
        } else if (!cgba_swi_bulk_video_copy(dst, sh, bytes)) {
          int saved = cgba_sh4_extra_cycles;
          u32 i;
          for (i = 0; i < bytes; i += 4)
            execute_store_u32(dst + i, cgba_le32_read(sh + i));
          cgba_sh4_extra_cycles = saved;
          cgba_parked_swi_drain_store_alert();
        }
        src_full += bytes;
        src += bytes;
        dst += bytes;
        reg[3] = src;
        reg[0] = src;                          /* chunk-top canonical */
        reg[2] = last_word;
      }
      words -= (s32)(k * 8u);
      reg[4] = (u32)words;
      remaining -= (s32)(k * chunk_c);
    }
  }

  return cgba_parked_swi_finish(remaining);
}

/* ---- Parked/resumable CpuSet -----------------------------------------------
 * Same canonical-state approach as the FastSet engine, for all four CpuSet
 * loops (word/half copy + word/half fill). Shares the dispatcher prologue
 * (STR r4) and epilogue (LDM {r4}; BX lr -> 0x94 -> dispatcher tail) with
 * FastSet; only the loop-top register layout and per-element chunking differ.
 * Open-BIOS geometry (0x614..0x714):
 *   word copy top 0x6AC: r2=src cursor, r1=dst-src, r3=count left,
 *     r12=0x0EFFFFFF, r0=last loaded word (escapes: r0=last, r1=dst-src).
 *   half copy top 0x668: r2=src cursor, r3=src END, r1=dst-src,
 *     r4=0x0EFFFFFF, r12=last half (escapes: r0=src unchanged, r1=dst-src).
 *   half fill top 0x6E4: r0=src unchanged, r1=dst cursor, r2=fill16,
 *     r3=dst END (escapes: r0=src unchanged, r1=dst END).
 *   word fill top 0x708: r0=src unchanged, r1=dst cursor, r2=fill32,
 *     r3=count left (escapes: r0=src unchanged, r1=dst END). */
static u32 cgba_cs_entry_cost;

static void cgba_swi_cpuset_materialize(int half, int fill)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 sp_r, lr_r;
  u32 src0 = reg[0], dst0 = reg[1], count = reg[2] & 0x1FFFFFu;
  u32 fval = 0;

  if (fill) {
    u32 esrc = half ? src0 : (src0 & ~3u);
    const u8 *vp = cgba_bulk_src_host(esrc, half ? 2u : 4u);
    fval = half ? ((u32)vp[0] | ((u32)vp[1] << 8)) : cgba_le32_read(vp);
  }

  sp_r = cgba_parked_swi_enter(1u, &lr_r);

  /* Entry-only dispatcher cycles; return-half is charged in the epilogue. */
  cgba_cs_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                     + CGBA_WS_N(sp_r, 1);          /* 0x618 STR r4 */
  reg[0] = src0;
  if (fill) {
    reg[12] = 0x00000614u;                          /* jump-table leftover */
    if (half) {
      reg[1] = dst0;
      reg[2] = fval;
      reg[3] = dst0 + count * 2u;
      cgba_cs_entry_cost += 21 * S0 + CGBA_WS_N(src0 >> 24, 0) + N0;
      reg[REG_PC] = CGBA_CS_HFILL_LOOP;
    } else {
      u32 wsrc = src0 & ~3u, wd = dst0 & ~3u;
      reg[1] = wd;
      reg[2] = fval;
      reg[3] = count;
      cgba_cs_entry_cost += 22 * S0 + CGBA_WS_N(wsrc >> 24, 1) + 2 * N0;
      reg[REG_PC] = CGBA_CS_WFILL_LOOP;
    }
  } else if (half) {
    reg[2] = src0;                                  /* src cursor */
    reg[3] = src0 + count * 2u;                     /* src END */
    reg[1] = dst0 - src0;                           /* delta */
    reg[4] = 0x0EFFFFFFu;
    cgba_cs_entry_cost += 21 * S0;
    reg[REG_PC] = CGBA_CS_HCOPY_LOOP;
  } else {
    u32 ws = src0 & ~3u, wd = dst0 & ~3u;
    reg[2] = ws;                                    /* src cursor */
    reg[1] = wd - ws;                               /* delta */
    reg[3] = count;                                 /* count remaining */
    reg[12] = 0x0EFFFFFFu;
    cgba_cs_entry_cost += 22 * S0 + N0;
    reg[REG_PC] = CGBA_CS_WCOPY_LOOP;
  }
}

static int cgba_swi_bulk_video_copy(u32 dst, const u8 *sh, u32 bytes)
{
  u32 region = dst >> 24;
  if (region == 7u) {
    u32 off = dst & 0x3FFu;
    if (off + bytes > 0x400u)
      return 0;
    {
      u8 *od = (u8 *)(void *)oam_ram + off;
      if (memcmp(od, sh, bytes) != 0) {
        cgba_video_flush_before_write();
        memcpy(od, sh, bytes);
        reg[OAM_UPDATED] = 1;
      }
    }
    return 1;
  }
  if (region == 5u) {
    u32 off = dst & 0x3FFu;
    if (off + bytes > 0x400u)
      return 0;
    {
      u8 *pd = (u8 *)(void *)palette_ram + off;
      if (memcmp(pd, sh, bytes) != 0) {
        u16 *pr = (u16 *)(void *)pd;
        u16 *pc = (u16 *)(void *)((u8 *)(void *)palette_ram_converted + off);
        u32 n16 = bytes >> 1, i;
        cgba_video_flush_before_write();
        memcpy(pd, sh, bytes);
        for (i = 0; i < n16; i++) {
          u16 v = eswap16(pr[i]);
          pc[i] = convert_palette(v);
        }
        mark_palette_ram_addr_dirty(off);
      }
    }
    return 1;
  }
  return 0;
}

static int cgba_swi_bulk_video_fill(u32 dst, const u8 *vp, u32 width, u32 bytes)
{
  u32 region = dst >> 24;
  u8 b[4];
  if (region != 7u && region != 5u)
    return 0;
  {
    u32 off = dst & 0x3FFu;
    if (off + bytes > 0x400u)
      return 0;
    b[0] = vp[0]; b[1] = vp[1];
    if (width == 4u) { b[2] = vp[2]; b[3] = vp[3]; }
    if (region == 7u) {
      u8 *od = (u8 *)(void *)oam_ram + off;
      u32 i;
      int changed = 0;
      for (i = 0; i < bytes; i += width) {
        if (memcmp(od + i, b, width) != 0) {
          changed = 1;
          break;
        }
      }
      if (changed) {
        cgba_video_flush_before_write();
        for (i = 0; i < bytes; i += width)
          memcpy(od + i, b, width);
        reg[OAM_UPDATED] = 1;
      }
      return 1;
    } else {
      u8 *pd = (u8 *)(void *)palette_ram + off;
      u32 i;
      int changed = 0;
      for (i = 0; i < bytes; i += width) {
        if (memcmp(pd + i, b, width) != 0) {
          changed = 1;
          break;
        }
      }
      if (changed) {
        u16 *pc = (u16 *)(void *)((u8 *)(void *)palette_ram_converted + off);
        u16 c0 = convert_palette((u16)((u32)b[0] | ((u32)b[1] << 8)));
        u32 n16 = bytes >> 1;
        cgba_video_flush_before_write();
        for (i = 0; i < bytes; i += width)
          memcpy(pd + i, b, width);
        if (width == 2u) {
          for (i = 0; i < n16; i++)
            pc[i] = c0;
        } else {
          u16 c1 = convert_palette((u16)((u32)b[2] | ((u32)b[3] << 8)));
          for (i = 0; i < n16; i += 2u) {
            pc[i] = c0;
            pc[i + 1u] = c1;
          }
        }
        mark_palette_ram_addr_dirty(off);
      }
      return 1;
    }
  }
}

static u32 cgba_swi_cpuset_engine(s32 remaining)
{
  u32 loop_pc = reg[REG_PC];
  int fill = (loop_pc == CGBA_CS_WFILL_LOOP || loop_pc == CGBA_CS_HFILL_LOOP);
  int half = (loop_pc == CGBA_CS_HCOPY_LOOP || loop_pc == CGBA_CS_HFILL_LOOP);
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src = 0, dst, per_elem, width, total_bytes;
  const u8 *src_full = NULL;
  s32 count;

  if (fill) {
    if (reg[12] != CGBA_BIOS_CPUSET_ENTRY)
      return CGBA_FS_DECLINE;
    dst = reg[1];
    if (half) {
      if (reg[3] <= dst || ((reg[3] - dst) & 1u) != 0u ||
          ((reg[3] - 1u) >> 24) != (dst >> 24))
        return CGBA_FS_DECLINE;
      count = (s32)((reg[3] - dst) >> 1);
      width = 2;
      per_elem = 3 * S0 + CGBA_WS_N(dst >> 24, 0) + N0;
    } else {
      count = (s32)reg[3];
      width = 4;
      per_elem = 3 * S0 + CGBA_WS_N(dst >> 24, 1) + N0;
    }
    if (count <= 0 || count > 0x200000)
      return CGBA_FS_DECLINE;
    total_bytes = (u32)count * width;
    if ((half ? ((dst & 1u) != 0u) : ((dst & 3u) != 0u)) ||
        !cgba_swi_range_in_region(dst, total_bytes))
      return CGBA_FS_DECLINE;
  } else if (half) {
    if (reg[4] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[2];
    if (reg[3] <= src || ((reg[3] - src) & 1u) != 0u)
      return CGBA_FS_DECLINE;
    count = (s32)((reg[3] - src) >> 1);              /* (src_end - src)/2 */
    width = 2;
    dst = reg[1] + src;
    per_elem = 7 * S0 + CGBA_WS_N(src >> 24, 0) + CGBA_WS_N(dst >> 24, 0) + N0;
    if (count <= 0 || count > 0x200000 || ((dst | src) & 1u) != 0u)
      return CGBA_FS_DECLINE;
    total_bytes = (u32)count * 2u;
    if (!cgba_swi_range_in_region(dst, total_bytes) ||
        !cgba_swi_copy_overlap_ok(src, dst, total_bytes))
      return CGBA_FS_DECLINE;
    src_full = cgba_swi_exact_src_host(src, total_bytes);
    if (!src_full)
      return CGBA_FS_DECLINE;
  } else {
    if (reg[12] != 0x0EFFFFFFu)
      return CGBA_FS_DECLINE;
    src = reg[2];
    count = (s32)reg[3];
    width = 4;
    dst = reg[1] + src;
    per_elem = 7 * S0 + CGBA_WS_N(src >> 24, 1) + CGBA_WS_N(dst >> 24, 1) + N0;
    if (count <= 0 || count > 0x200000 || ((dst | src) & 3u) != 0u)
      return CGBA_FS_DECLINE;
    total_bytes = (u32)count * 4u;
    if (!cgba_swi_range_in_region(dst, total_bytes) ||
        !cgba_swi_copy_overlap_ok(src, dst, total_bytes))
      return CGBA_FS_DECLINE;
    src_full = cgba_swi_exact_src_host(src, total_bytes);
    if (!src_full)
      return CGBA_FS_DECLINE;
  }

  remaining -= (s32)cgba_cs_entry_cost;
  cgba_cs_entry_cost = 0;

  while (1) {
    if (count <= 0) {
      u32 disp_ret_c = 0u;
      u32 sp_r = cgba_parked_swi_leave(1u, &disp_ret_c);
      if (fill) {
        remaining -= (s32)(disp_ret_c + 3 * S0 + N0 + CGBA_WS_S(sp_r, 1));
      } else {
        remaining -= (s32)(disp_ret_c + 2 * S0 + CGBA_WS_S(sp_r, 1) +
                           (half ? 0u : N0)); /* 684+688 (+0 half: fallthrough) */
      }
      break;
    }
    if (remaining <= 0) {
      u32 park_exit;
      if (cgba_parked_swi_park(&remaining, loop_pc, S0, N0, &park_exit))
        return park_exit;
      continue;
    }
    {
      s32 fit = remaining / (s32)per_elem;
      u32 k = (fit < 1) ? 1u
            : ((u32)fit > (u32)count ? (u32)count : (u32)fit);
      u32 bytes = k * width;
      u8 *tags = NULL;
      u8 *dh;
      if (fill) {
        u32 fv = reg[2];
        u8 fbytes[4];
        cgba_le32_write(fbytes, fv);
        dh = cgba_bulk_ram_host(dst, bytes, &tags);
        if (dh) {
          int smc_changed = cgba_smc_fill_changes_tagged(dh, fbytes, width, tags, bytes);
          if ((half ? (fv & 0xFFFFu) : fv) == 0u) {
            memset(dh, 0, bytes);
          } else if (!half) {
            u32 i;
            for (i = 0; i < bytes; i += 4)
              cgba_le32_write(dh + i, fv);
          } else {
            u8 b0 = fbytes[0], b1 = fbytes[1];
            u32 i;
            for (i = 0; i < bytes; i += 2) {
              dh[i] = b0;
              dh[i + 1] = b1;
            }
          }
          if (smc_changed)
            flush_translation_cache_ram();
        } else if (!cgba_swi_bulk_video_fill(dst, fbytes, width, bytes)) {
          int saved = cgba_sh4_extra_cycles;
          u32 i;
          for (i = 0; i < bytes; i += width) {
            if (half)
              execute_store_u16(dst + i, fv & 0xFFFFu);
            else
              execute_store_u32(dst + i, fv);
          }
          cgba_sh4_extra_cycles = saved;
          cgba_parked_swi_drain_store_alert();
        }
        dst += bytes;
        count -= (s32)k;
        reg[1] = dst;
        if (!half)
          reg[3] = (u32)count;
        remaining -= (s32)(k * per_elem);
        continue;
      }
      {
        const u8 *sh = src_full;
        u32 last_val = half
          ? ((u32)sh[bytes - 2] | ((u32)sh[bytes - 1] << 8))
          : cgba_le32_read(sh + bytes - 4);
        dh = cgba_bulk_ram_host(dst, bytes, &tags);
        if (dh) {
          int smc_changed;
#if CGBA_SH4_SWI_FORWARD_OVERLAP
          if (cgba_sh4_swi_is_forward_overlap(dh, sh, bytes)) {
            smc_changed = cgba_smc_tags_nonzero(tags, bytes);
            cgba_sh4_swi_copy_forward(dh, sh, bytes, width);
            last_val = half
              ? ((u32)dh[bytes - 2] | ((u32)dh[bytes - 1] << 8))
              : cgba_le32_read(dh + bytes - 4);
          } else
#endif
          {
            smc_changed = cgba_smc_copy_changes_tagged(dh, sh, tags, bytes);
#ifdef __sh__
            if (bytes >= 64u && ((((uintptr_t)dh | (uintptr_t)sh) & 3u) == 0u) &&
                ((uintptr_t)sh >= (uintptr_t)dh + bytes ||
                 (uintptr_t)dh >= (uintptr_t)sh + bytes)) {
              extern void cgba_dma_copy_movca_sh4(void *dst, const void *src, u32 nbytes);
              cgba_dma_copy_movca_sh4(dh, sh, bytes);
            } else
#endif
              memmove(dh, sh, bytes);
          }
          if (smc_changed)
            flush_translation_cache_ram();
        }
#ifdef CGBA_SH4_SWI_OAM_BULK
        /* Try cgba_swi_bulk_video_copy first so non-wrapping OAM copies keep
         * the memcmp no-op skip; fall back to cgba_sh4_swi_copy_u16_to_oam
         * only for wrapping OAM copies (off + bytes > 0x400). */
        else if (!cgba_swi_bulk_video_copy(dst, sh, bytes) &&
                 !((dst >> 24) == 7u && half &&
                   (cgba_video_flush_before_write(),
                    cgba_sh4_swi_copy_u16_to_oam(
                      (u8 *)(void *)oam_ram, sh, dst, k,
                      &reg[OAM_UPDATED])))) {
#else
        else if (!cgba_swi_bulk_video_copy(dst, sh, bytes)) {
#endif
          int saved = cgba_sh4_extra_cycles;
          u32 i;
          for (i = 0; i < bytes; i += width) {
            if (half)
              execute_store_u16(dst + i, (u32)sh[i] | ((u32)sh[i + 1] << 8));
            else
              execute_store_u32(dst + i, cgba_le32_read(sh + i));
          }
          cgba_sh4_extra_cycles = saved;    /* per_elem is the exact charge */
          cgba_parked_swi_drain_store_alert();
        }
        src_full += bytes;
        src += bytes;
        dst += bytes;
        count -= (s32)k;
        reg[2] = src;                       /* src cursor (canonical) */
        if (half) {
          reg[12] = last_val;
        } else {
          reg[3] = (u32)count;              /* count remaining */
          reg[0] = last_val;                /* last word (escapes) */
        }
        remaining -= (s32)(k * per_elem);
      }
    }
  }

  return cgba_parked_swi_finish(remaining);
}

/* ---- Parked/resumable ObjAffineSet (SWI 0x0F) -----------------------------
 * Open-BIOS geometry (0x08E0..0x097C):
 *   prologue 0x08E0..0x0900: push {r4-r10}; r2 = count-1; r7 = offset*2;
 *     r10 = 0x2150 (sine table); r8 = offset*4; r0 = src+8; r3 = dst+offset.
 *   loop top 0x0904: 29 ARM insns + 3*N(src,0) + 2*N(0,0) + 4*N(dst,0) + N0.
 *   epilogue 0x0978..0x097C: pop {r4-r10}; bx lr (-> 0x94 dispatcher return).
 *   Escapes: r0 = src_orig + (count+1)*8, r1 = dst_orig + count*offset*4. */
static u32 cgba_oa_entry_cost;

static inline void cgba_swi_objaffine_step(const u8 *sh, const u8 *sin_tbl,
                                           s32 *dx, s32 *dmx_neg,
                                           s32 *dy, s32 *dmy)
{
  s32 rx = (s16)((u16)sh[0] | ((u16)sh[1] << 8));
  s32 ry = (s16)((u16)sh[2] | ((u16)sh[3] << 8));
  u32 th = (u32)sh[5];                          /* (u16[src+4]) >> 8 */
  u32 ai = ((th + 0x40u) & 255u) << 1;
  u32 bi = (th & 255u) << 1;
  s32 a = (s16)((u16)sin_tbl[ai] | ((u16)sin_tbl[ai + 1] << 8));
  s32 b = (s16)((u16)sin_tbl[bi] | ((u16)sin_tbl[bi + 1] << 8));
  *dx = (rx * a) >> 14;
  *dmx_neg = -((rx * b) >> 14);
  *dy = (ry * b) >> 14;
  *dmy = (ry * a) >> 14;
}

static int cgba_swi_objaffine_can_enter(u32 src, u32 dst, u32 count, u32 offset)
{
  if (count == 0u || count > 1024u || ((src | dst | offset) & 1u) != 0u ||
      offset > 256u)
    return 0;
  if (!cgba_swi_range_in_region(dst, offset * (4u * count - 1u) + 2u))
    return 0;
  if (!cgba_swi_exact_src_host(src, count * 8u))
    return 0;
  return 1;
}

static void cgba_swi_objaffine_materialize(void)
{
  u32 S0 = CGBA_WS_S(0, 1);
  u32 src0 = reg[0], dst0 = reg[1], count = reg[2], offset = reg[3];
  u32 lr_r, sp_r;

  /* 0x08E4: push {r4..r10} (7 words) */
  sp_r = cgba_parked_swi_enter(7u, &lr_r);

  reg[0] = src0 + 8u;
  reg[1] = dst0;
  reg[2] = count - 1u;
  reg[3] = dst0 + offset;
  reg[7] = offset << 1;
  reg[8] = offset << 2;
  reg[10] = 0x00002150u;
  reg[12] = 0x000008E0u;
  reg[REG_PC] = CGBA_OA_LOOP;

  /* Dispatcher entry + 0x08E0..0x0900 (9 insns, PUSH 7 words, LDR literal). */
  cgba_oa_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                     + 9 * S0 + 7 * CGBA_WS_S(sp_r, 1) + CGBA_WS_N(0, 1);
}

static u32 cgba_swi_objaffine_engine(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 rem_count = reg[2] + 1u;
  u32 offset = reg[7] >> 1;
  u32 src, dst, per_elem;
  const u8 *sh;
  const u8 *sin_tbl = bios_rom + 0x2150u;

  if (reg[10] != CGBA_BIOS_SIN_TABLE || reg[REG_LR] != CGBA_BIOS_SWI_RET ||
      rem_count == 0u || rem_count > 1024u ||
      (reg[7] & 1u) != 0u || reg[8] != (reg[7] << 1) || offset > 256u ||
      reg[3] != reg[1] + offset)
    return CGBA_FS_DECLINE;

  src = reg[0] - 8u;
  dst = reg[1];
  if (((src | dst | offset) & 1u) != 0u ||
      !cgba_swi_range_in_region(dst, offset * (4u * rem_count - 1u) + 2u))
    return CGBA_FS_DECLINE;
  sh = cgba_swi_exact_src_host(src, rem_count * 8u);
  if (!sh)
    return CGBA_FS_DECLINE;

  remaining -= (s32)cgba_oa_entry_cost;
  cgba_oa_entry_cost = 0;

  per_elem = 29 * S0
           + 3 * CGBA_WS_N(src >> 24, 0)
           + 2 * CGBA_WS_N(0, 0)
           + 4 * CGBA_WS_N(dst >> 24, 0)
           + N0;

  while (1) {
    if (rem_count == 0u) {
      u32 disp_ret_c = 0u;
      u32 sp_r = cgba_parked_swi_leave(7u, &disp_ret_c);
      /* 0x0978 POP {r4-r10} + 0x097C BX lr (+N0 cancels last loop BNE nt). */
      remaining -= (s32)(disp_ret_c + 2 * S0 + 7 * CGBA_WS_S(sp_r, 1));
      break;
    }
    if (remaining <= 0) {
      u32 park_exit;
      if (cgba_parked_swi_park(&remaining, CGBA_OA_LOOP, S0, N0, &park_exit))
        return park_exit;
      continue;
    }
    {
      s32 fit = remaining / (s32)per_elem;
      u32 k = (fit < 1) ? 1u : ((u32)fit > rem_count ? rem_count : (u32)fit);
      u32 span = offset * (4u * k - 1u) + 2u;
      u8 *tags = NULL;
      u8 *dh = cgba_bulk_ram_host(dst, span, &tags);
      u8 *oam_d = NULL;
      u32 i;
      s32 last_dx = 0, last_dmx_neg = 0, last_dy = 0, last_dmy = 0;

      if (!dh && (dst >> 24) == 7u) {
        u32 ooff = dst & 0x3FFu;
        if (ooff + span <= 0x400u) {
          cgba_video_flush_before_write();
          oam_d = (u8 *)(void *)oam_ram + ooff;
          reg[OAM_UPDATED] = 1;
        }
      }
      if (dh || oam_d) {
        u8 *out = dh ? dh : oam_d;
        for (i = 0; i < k; i++) {
          cgba_swi_objaffine_step(sh, sin_tbl,
                                  &last_dx, &last_dmx_neg, &last_dy, &last_dmy);
          u16 pa = (u16)last_dx, pb = (u16)last_dmx_neg;
          u16 pc = (u16)last_dy, pd = (u16)last_dmy;
          u8 *p0 = out;
          u8 *p1 = p0 + offset;
          u8 *p2 = p1 + offset;
          u8 *p3 = p2 + offset;
          p0[0] = (u8)pa; p0[1] = (u8)(pa >> 8);
          p1[0] = (u8)pb; p1[1] = (u8)(pb >> 8);
          p2[0] = (u8)pc; p2[1] = (u8)(pc >> 8);
          p3[0] = (u8)pd; p3[1] = (u8)(pd >> 8);
          sh += 8;
          out += offset * 4u;
        }
        if (dh && cgba_smc_tags_nonzero(tags, span))
          flush_translation_cache_ram();
      } else {
        int saved_extra = cgba_sh4_extra_cycles;
        for (i = 0; i < k; i++) {
          u32 d = dst + i * offset * 4u;
          cgba_swi_objaffine_step(sh, sin_tbl,
                                  &last_dx, &last_dmx_neg, &last_dy, &last_dmy);
          execute_store_u16(d, (u32)(u16)last_dx);
          execute_store_u16(d + offset, (u32)(u16)last_dmx_neg);
          execute_store_u16(d + offset * 2u, (u32)(u16)last_dy);
          execute_store_u16(d + offset * 3u, (u32)(u16)last_dmy);
          sh += 8;
        }
        cgba_sh4_extra_cycles = saved_extra;
        cgba_parked_swi_drain_store_alert();
      }
      src += k * 8u;
      dst += k * offset * 4u;
      rem_count -= k;
      reg[0] = src + 8u;
      reg[1] = dst;
      reg[2] = rem_count - 1u;
      reg[3] = dst + offset;
      reg[5] = (u32)last_dy;
      reg[6] = (u32)last_dx;
      reg[9] = (u32)last_dmx_neg;
      reg[12] = (u32)last_dmy;
      remaining -= (s32)(k * per_elem);
    }
  }

  return cgba_parked_swi_finish(remaining);
}

/* ---- Parked/resumable LZ77UnCompWram (0x11) & LZ77UnCompVram (0x12) -------
 * Open-BIOS geometry:
 *   SWI 0x11 (0x0A44..0x0B58): outer flag loop top CGBA_LZ8_LOOP = 0x0A74.
 *     push {r4-r8} (5 words, sp_usr -= 32); r0=src, r1=dst, r2=len>0.
 *   SWI 0x12 (0x0CDC -> 0x0B5C..0x0CD8): outer flag loop top CGBA_LZ16_LOOP = 0x0BA4.
 *     push {r4-r11} (8 words, sp_usr -= 44); r0=src, r1=dst(even), r3=writeVal,
 *     r4=byteCount(0/1), r5=len>0, r12=byteShift(0/8).
 * Parks cleanly at outer flag-byte boundaries (<= ~600 cycles per flag byte),
 * charging exact instruction/branch/waitstate cycles and yielding to IRQs. */
static u32 cgba_lz_entry_cost;

static inline const u8 *cgba_lz_src_page(u32 addr, u32 *avail_out)
{
  return cgba_resolve_guest_read_span(addr, avail_out);
}

static inline u8 cgba_lz_read_u8(u32 addr)
{
  u32 avail;
  const u8 *p = cgba_lz_src_page(addr, &avail);
  if (p)
    return *p;
  return (u8)read_memory8(addr);
}

static inline u8 *cgba_lz_dst_region(u32 dst, u32 *off_out, u32 *size_out,
                                     u8 **tags_base_out)
{
  switch (dst >> 24) {
  case 2u:
    *off_out = dst & 0x3FFFFu;
    *size_out = 0x40000u;
    *tags_base_out = ewram + 0x40000u;
    return ewram;
  case 3u:
    *off_out = dst & 0x7FFFu;
    *size_out = 0x8000u;
    *tags_base_out = iwram;
    return iwram + 0x8000u;
  case 6u:
    *off_out = dst & 0x1FFFFu;
    *size_out = 0x18000u;
    *tags_base_out = NULL;
    return vram;
  default:
    return NULL;
  }
}

static int cgba_swi_lz77_can_enter(int vram_mode, u32 *len_out)
{
  u32 src0 = reg[0], dst0 = reg[1];
  u32 avail, header, len, dst_off, dst_sz;
  const u8 *hp;
  u8 *tags_base;

  if ((src0 & 3u) != 0u)
    return 0;
  if (vram_mode && (dst0 & 1u) != 0u)
    return 0;
  hp = cgba_lz_src_page(src0, &avail);
  if (hp && avail >= 4u) {
    header = cgba_le32_read(hp);
  } else if ((src0 >> 24) >= 8u && (src0 >> 24) <= 0xDu) {
    header = read_memory32(src0);
  } else {
    return 0;
  }
  len = header >> 8;
  if (len == 0u)
    return 0;
  if (((src0 + 4u) & 0x0E000000u) == 0u ||
      (((src0 + 4u) + (len & 0x1FFFFFu)) & 0x0E000000u) == 0u)
    return 0;
  if (!cgba_lz_dst_region(dst0, &dst_off, &dst_sz, &tags_base))
    return 0;
  if (len > dst_sz || dst_off + len > dst_sz)
    return 0;
  *len_out = len;
  return 1;
}

static void cgba_swi_lz77_materialize(int vram_mode, u32 len)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src0 = reg[0], dst0 = reg[1];
  u32 lr_r, sp_r;

  /* 0x0B5C pushes {r4..r11} (8 words); 0x0A4C pushes {r4..r8} (5 words). */
  sp_r = cgba_parked_swi_enter(vram_mode ? 8u : 5u, &lr_r);

  if (vram_mode) {
    reg[0] = src0 + 4u;
    reg[1] = dst0;
    reg[2] = 1u;
    reg[3] = 0u;
    reg[4] = 0u;
    reg[5] = len;
    reg[12] = 0u;
    reg[REG_PC] = CGBA_LZ16_LOOP;
    /* Dispatcher entry + 0x0CDC..0x0CE0 (2*S0+N0) + 0x0B5C..0x0BA0 (18*S0 + 8*S(sp) + N(src,1)). */
    cgba_lz_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                       + 20 * S0 + N0 + 8 * CGBA_WS_S(sp_r, 1)
                       + CGBA_WS_N(src0 >> 24, 1);
  } else {
    reg[0] = src0 + 4u;
    reg[1] = dst0;
    reg[2] = len;
    reg[3] = (src0 + 4u) + (len & 0x1FFFFFu);
    reg[12] = 0x00000A44u;
    reg[REG_PC] = CGBA_LZ8_LOOP;
    /* Dispatcher entry + 0x0A44..0x0A70 (12*S0 + 5*S(sp) + N(src,1)). */
    cgba_lz_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                       + 12 * S0 + 5 * CGBA_WS_S(sp_r, 1)
                       + CGBA_WS_N(src0 >> 24, 1);
  }
}

static u32 cgba_swi_lz77_engine(s32 remaining)
{
  int vram_mode = (reg[REG_PC] == CGBA_LZ16_LOOP);
  u32 loop_pc = vram_mode ? CGBA_LZ16_LOOP : CGBA_LZ8_LOOP;
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src = reg[0], dst = reg[1];
  u32 len = vram_mode ? reg[5] : reg[2];
  u32 wval = vram_mode ? reg[3] : 0u;
  u32 bcnt = vram_mode ? reg[4] : 0u;
  u32 bshf = vram_mode ? reg[12] : 0u;
  u32 dst_off, dst_sz;
  u8 *tags_base = NULL;
  u8 *dh_base;
  u32 src_r = src >> 24, dst_r = dst >> 24;
  u32 Nsrc, Ndst;
  u32 slice_dst_start;
  u32 exit_r0 = src, exit_r1 = dst;
  const u8 *sp = NULL;
  u32 s_avail = 0;

  if (reg[REG_LR] != CGBA_BIOS_SWI_RET || len == 0u)
    return CGBA_FS_DECLINE;
  if (vram_mode && ((dst & 1u) != 0u || bcnt > 1u || bshf != (bcnt << 3)))
    return CGBA_FS_DECLINE;
  dh_base = cgba_lz_dst_region(dst, &dst_off, &dst_sz, &tags_base);
  if (!dh_base || len > dst_sz || dst_off + len > dst_sz)
    return CGBA_FS_DECLINE;
  if (vram_mode && dst_off + ((len + bcnt) & ~1u) > dst_sz)
    return CGBA_FS_DECLINE;

  remaining -= (s32)cgba_lz_entry_cost;
  cgba_lz_entry_cost = 0;

  sp = cgba_lz_src_page(src, &s_avail);
  if (!sp)
    s_avail = 0u;

  Nsrc = CGBA_WS_N(src_r, 0);
  Ndst = CGBA_WS_N(dst_r, 0);
  slice_dst_start = dst_off;

  /* Flush any deferred scanlines BEFORE decompressing into VRAM. */
  if (dst_r == 6u)
    cgba_video_flush_before_write();

  while (1) {
    if (len == 0u) {
      /* Flush dirty/SMC for the final slice before epilogue. */
      u32 written = dst_off - slice_dst_start;
      if (written > 0u) {
        if (dst_r == 6u)
          mark_vram_dirty_span(slice_dst_start, written);
        else if (tags_base && cgba_smc_tags_nonzero(tags_base + slice_dst_start, written))
          flush_translation_cache_ram();
      }
      {
        u32 disp_ret_c = 0u;
        u32 sp_r;
        reg[0] = exit_r0;
        reg[1] = exit_r1;
        sp_r = cgba_parked_swi_leave(vram_mode ? 8u : 5u, &disp_ret_c);
        remaining -= (s32)(disp_ret_c + 2 * S0 + N0 +
                           (vram_mode ? 8u : 5u) * CGBA_WS_S(sp_r, 1));
      }
      break;
    }

    if (remaining <= 0) {
      /* Publish canonical loop-top registers and flush VRAM/SMC for slice. */
      u32 written = dst_off - slice_dst_start;
      if (written > 0u) {
        if (dst_r == 6u)
          mark_vram_dirty_span(slice_dst_start, written);
        else if (tags_base && cgba_smc_tags_nonzero(tags_base + slice_dst_start, written))
          flush_translation_cache_ram();
        slice_dst_start = dst_off;
      }
      reg[0] = src;
      reg[1] = dst;
      if (vram_mode) {
        reg[3] = wval;
        reg[4] = bcnt;
        reg[5] = len;
        reg[12] = bshf;
      } else {
        reg[2] = len;
      }
      {
        /* update_gba clamps remaining_cycles at -64; carry any deeper overshoot
         * forward so heavy backref flag groups never lose guest cycles. */
        s32 carry = 0;
        s32 pass = remaining;
        u32 ret;
        if (pass < -64) {
          carry = -pass;
          pass = 0;
        }
        ret = update_gba(pass);
        if (completed_frame(ret)) {
          cgba_lz_entry_cost = (u32)carry;
          return 0x80000000u;
        }
        remaining = (s32)cycles_to_run(ret);
        if (reg[REG_PC] != loop_pc) {
          cgba_lz_entry_cost = (u32)carry;
          return cgba_parked_swi_irq_exit(remaining, S0, N0);
        }
        remaining -= carry;
        /* update_gba() may have queued new deferred scanlines before we write
         * the next VRAM slice. */
        if (dst_r == 6u)
          cgba_video_flush_before_write();
      }
      continue;
    }

    /* Decode ONE flag group (up to 8 tokens) before rechecking remaining. */
    {
      u8 flags;
      u32 flag_src = src;
      u32 c = 4 * S0 + Nsrc;                    /* 0x0A74..0x0A80 / 0x0BA4..0x0BB0 */

      if (__builtin_expect(s_avail == 0u, 0)) {
        flags = cgba_lz_read_u8(src);
        sp = cgba_lz_src_page(src + 1u, &s_avail);
        if (!sp) s_avail = 0u;
        if ((src >> 24) != src_r) {
          src_r = src >> 24;
          Nsrc = CGBA_WS_N(src_r, 0);
        }
      } else {
        flags = *sp++;
        s_avail--;
      }
      src++;

      if (!vram_mode) {
        if (flags == 0u) {
          u32 n = (len < 8u) ? len : 8u;
          u32 i;
          c += N0 + 2 * S0;                     /* 0x0A80 taken + 0x0AD0,0x0AD4 */
          for (i = 0; i < n; i++) {
            u8 b;
            if (__builtin_expect(s_avail > 0u, 1)) {
              b = *sp++; s_avail--;
            } else {
              b = cgba_lz_read_u8(src);
              sp = cgba_lz_src_page(src + 1u, &s_avail);
              if (!sp) s_avail = 0u;
            }
            src++;
            dh_base[dst_off++] = b;
            dst++;
            len--;
            c += 4 * S0 + Nsrc + Ndst;          /* 0x0AD8..0x0AE4 */
            if (len == 0u) {
              c += N0;                          /* 0x0AE4 taken -> 0x0AC8 */
              exit_r0 = src;
              exit_r1 = dst;
              break;
            }
            c += 2 * S0;                        /* 0x0AE8, 0x0AEC */
            if (i + 1u < 8u)
              c += N0;                          /* 0x0AEC taken */
          }
          if (len > 0u)
            c += 2 * S0 + N0;                   /* 0x0AF0, 0x0AF4 taken -> 0x0A74 */
        } else {
          u32 bit;
          c += S0;                              /* 0x0A84 mov r8, #8 */
          for (bit = 0; bit < 8u; bit++) {
            c += 2 * S0;                        /* 0x0A88, 0x0A8C */
            if (flags & 0x80u) {
              u8 hi, lo;
              u32 disp, length, m, j;
              if (__builtin_expect(s_avail >= 2u, 1)) {
                hi = sp[0]; lo = sp[1]; sp += 2; s_avail -= 2;
              } else {
                hi = cgba_lz_read_u8(src);
                lo = cgba_lz_read_u8(src + 1u);
                sp = cgba_lz_src_page(src + 2u, &s_avail);
                if (!sp) s_avail = 0u;
              }
              src += 2u;
              length = ((u32)hi >> 4) + 3u;
              disp = ((((u32)hi & 0x0Fu) << 8) | (u32)lo) + 1u;
              m = (length < len) ? length : len;
              c += N0 + 13 * S0 + 2 * Nsrc + N0;/* 0x0A8C taken + 0x0AFC..0x0B2C */
              if (__builtin_expect(disp <= dst_off, 1)) {
                u8 *wp = dh_base + dst_off;
                for (j = 0; j < m; j++)
                  wp[j] = wp[j - disp];
              } else {
                int saved_extra = cgba_sh4_extra_cycles;
                for (j = 0; j < m; j++)
                  dh_base[dst_off + j] = (u8)execute_load_u8(dst + j - disp);
                cgba_sh4_extra_cycles = saved_extra;
              }
              dst_off += m;
              dst += m;
              len -= m;
              /* Iter 0: 6*S0 + 2*Ndst; iter 1..m-1: 7*S0 + 2*Ndst; loop tails. */
              c += m * (6 * S0 + 2 * Ndst) + (m - 1u) * (3 * S0 + N0);
              if (len == 0u) {
                c += N0;                        /* 0x0B48 taken -> 0x0AC8 */
                exit_r0 = 0u;
                exit_r1 = dst;
                break;
              }
              c += 2 * S0 + 2 * S0 + N0;        /* last 0x0B4C,0x0B50 + 0x0B54,0x0B58 */
            } else {
              u8 b;
              if (__builtin_expect(s_avail > 0u, 1)) {
                b = *sp++; s_avail--;
              } else {
                b = cgba_lz_read_u8(src);
                sp = cgba_lz_src_page(src + 1u, &s_avail);
                if (!sp) s_avail = 0u;
              }
              src++;
              dh_base[dst_off++] = b;
              dst++;
              len--;
              c += 6 * S0 + Nsrc + Ndst;        /* 0x0A90..0x0AA4 */
              if (len == 0u) {
                c += N0;                        /* 0x0AA4 taken -> 0x0AC8 */
                exit_r0 = dst;                  /* 0x0A98: r0 = old_r1 + 1 */
                exit_r1 = dst - 1u;             /* 0x0AA8 not reached */
                break;
              }
              c += S0;                          /* 0x0AA8 mov r1, r0 */
            }
            flags <<= 1;
            c += 4 * S0;                        /* 0x0AAC..0x0AB8 */
            if (bit + 1u < 8u)
              c += N0;                          /* 0x0AB8 taken */
          }
          if (len > 0u)
            c += 3 * S0 + N0;                   /* 0x0ABC..0x0AC4 taken */
        }
      } else {
        /* LZ77UnCompVram (16-bit halfword writes) */
        if (flags == 0u) {
          u32 n = (len < 8u) ? len : 8u;
          u32 i;
          c += N0 + 2 * S0;                     /* 0x0BB0 taken + 0x0C14,0x0C18 */
          for (i = 0; i < n; i++) {
            u8 b;
            if (__builtin_expect(s_avail > 0u, 1)) {
              b = *sp++; s_avail--;
            } else {
              b = cgba_lz_read_u8(src);
              sp = cgba_lz_src_page(src + 1u, &s_avail);
              if (!sp) s_avail = 0u;
            }
            src++;
            wval |= (u32)b << bshf;
            if (bcnt == 1u) {
              dh_base[dst_off]     = (u8)wval;
              dh_base[dst_off + 1] = (u8)(wval >> 8);
              dst_off += 2u;
              dst += 2u;
              wval = 0u; bcnt = 0u; bshf = 0u;
              c += Ndst;
            } else {
              bcnt = 1u; bshf = 8u;
            }
            len--;
            c += 11 * S0 + Nsrc;                /* 0x0C1C..0x0C44 */
            if (len == 0u) {
              c += N0;                          /* 0x0C44 taken -> 0x0C0C */
              exit_r0 = src;
              exit_r1 = dst;
              break;
            }
            c += 2 * S0;                        /* 0x0C48, 0x0C4C */
            if (i + 1u < 8u)
              c += N0;                          /* 0x0C4C taken */
          }
          if (len > 0u)
            c += 2 * S0 + N0;                   /* 0x0C50, 0x0C54 taken -> 0x0BA4 */
        } else {
          u32 bit;
          c += S0;                              /* 0x0BB4 mov fp, #8 */
          for (bit = 0; bit < 8u; bit++) {
            c += 2 * S0;                        /* 0x0BB8, 0x0BBC */
            if (flags & 0x80u) {
              u8 hi, lo;
              u32 disp, length, m, j;
              if (__builtin_expect(s_avail >= 2u, 1)) {
                hi = sp[0]; lo = sp[1]; sp += 2; s_avail -= 2;
              } else {
                hi = cgba_lz_read_u8(src);
                lo = cgba_lz_read_u8(src + 1u);
                sp = cgba_lz_src_page(src + 2u, &s_avail);
                if (!sp) s_avail = 0u;
              }
              src += 2u;
              length = ((u32)hi >> 4) + 3u;
              disp = ((((u32)hi & 0x0Fu) << 8) | (u32)lo) + 1u;
              m = (length < len) ? length : len;
              c += N0 + 14 * S0 + 2 * Nsrc + N0;/* 0x0BBC taken + 0x0C5C..0x0C90 */
              {
                u32 win_off = dst_off + bcnt - disp;
                int in_buf = (disp <= dst_off + bcnt);
                u32 wstores = (m + bcnt) >> 1;
                if (__builtin_expect(in_buf, 1)) {
                  for (j = 0; j < m; j++) {
                    u8 b = dh_base[win_off + j];
                    wval |= (u32)b << bshf;
                    if (bcnt == 1u) {
                      dh_base[dst_off]     = (u8)wval;
                      dh_base[dst_off + 1] = (u8)(wval >> 8);
                      dst_off += 2u;
                      dst += 2u;
                      wval = 0u; bcnt = 0u; bshf = 0u;
                    } else {
                      bcnt = 1u; bshf = 8u;
                    }
                  }
                } else {
                  int saved_extra = cgba_sh4_extra_cycles;
                  u32 win_addr = dst + bcnt - disp;
                  for (j = 0; j < m; j++) {
                    u8 b = (u8)execute_load_u8(win_addr + j);
                    wval |= (u32)b << bshf;
                    if (bcnt == 1u) {
                      dh_base[dst_off]     = (u8)wval;
                      dh_base[dst_off + 1] = (u8)(wval >> 8);
                      dst_off += 2u;
                      dst += 2u;
                      wval = 0u; bcnt = 0u; bshf = 0u;
                    } else {
                      bcnt = 1u; bshf = 8u;
                    }
                  }
                  cgba_sh4_extra_cycles = saved_extra;
                }
                len -= m;
                /* Iter 0: 13*S0 + Ndst; iter 1..m-1: 14*S0 + Ndst; wstores * Ndst; loop tails. */
                c += m * (13 * S0 + Ndst) + wstores * Ndst + (m - 1u) * (3 * S0 + N0);
              }
              if (len == 0u) {
                c += N0;                        /* 0x0CC8 taken -> 0x0C0C */
                exit_r0 = 0u;
                exit_r1 = dst;
                break;
              }
              c += 2 * S0 + 2 * S0 + N0;        /* last 0x0CCC,0x0CD0 + 0x0CD4,0x0CD8 */
            } else {
              u8 b;
              if (__builtin_expect(s_avail > 0u, 1)) {
                b = *sp++; s_avail--;
              } else {
                b = cgba_lz_read_u8(src);
                sp = cgba_lz_src_page(src + 1u, &s_avail);
                if (!sp) s_avail = 0u;
              }
              src++;
              wval |= (u32)b << bshf;
              if (bcnt == 1u) {
                dh_base[dst_off]     = (u8)wval;
                dh_base[dst_off + 1] = (u8)(wval >> 8);
                dst_off += 2u;
                dst += 2u;
                wval = 0u; bcnt = 0u; bshf = 0u;
                c += Ndst;
              } else {
                bcnt = 1u; bshf = 8u;
              }
              len--;
              c += 12 * S0 + Nsrc;              /* 0x0BC0..0x0BEC */
              if (len == 0u) {
                c += N0;                        /* 0x0BEC taken -> 0x0C0C */
                exit_r0 = flag_src;             /* r0 still holds flag_src until 0x0C04 */
                exit_r1 = dst;
                break;
              }
            }
            flags <<= 1;
            c += 4 * S0;                        /* 0x0BF0..0x0BFC */
            if (bit + 1u < 8u)
              c += N0;                          /* 0x0BFC taken */
          }
          if (len > 0u)
            c += 3 * S0 + N0;                   /* 0x0C00..0x0C08 taken */
        }
      }
      remaining -= (s32)c;
    }
  }

  return cgba_parked_swi_finish(remaining);
}

#if CGBA_SH4_SWI_HLE_VERIFY
static struct {
  int pending;
  u32 num, ret_pc, budget;
  u32 irq_in0;
  u32 a0, a1, a2;
  struct cgba_swi_pred pred;
} cgba_swi_verify;
u32 cgba_swi_verify_checked, cgba_swi_verify_bad;

static void cgba_swi_verify_report(const char *what, u32 got, u32 want)
{
  volatile unsigned char *d = (volatile unsigned char *)0xb7000000u;
  static const char h[] = "0123456789ABCDEF";
  const char *pfx = "SWIV ";
  int i;
  while (*pfx) *d = (unsigned char)*pfx++;
  while (*what) *d = (unsigned char)*what++;
  *d = ' ';
  for (i = 7; i >= 0; i--) *d = (unsigned char)h[(got >> (i * 4)) & 0xF];
  *d = '/';
  for (i = 7; i >= 0; i--) *d = (unsigned char)h[(want >> (i * 4)) & 0xF];
  *d = '\n';
}
#endif

/* Apply a successful prediction: memory effects at host speed, exact
 * post-registers, exact interp-equivalent cycle charge. The predictors
 * only accept ranges that resolve to linear host memory. */
static int cgba_swi_apply_faithful(u32 src, u32 dst, u32 cnt, int fastset,
                                   const struct cgba_swi_pred *pred)
{
  u32 count = cnt & 0x1FFFFFu;
  u32 fill = (cnt >> 24) & 1;
  u32 word = fastset ? 1 : ((cnt >> 26) & 1);
  u32 width = word ? 4 : 2;
  u32 eff_src = word ? (src & ~3u) : src;
  u32 eff_dst = word ? (dst & ~3u) : dst;
  u32 n = fastset ? ((count + 7) & ~7u) : count;
  u32 len = n * width;
  u8 *tags = NULL;

  if (n) {
    u8 *d = cgba_bulk_ram_host(eff_dst, len, &tags);
    const u8 *sp = NULL;
    if (!d) {
      if (!fill) {
        sp = cgba_bulk_src_host(eff_src, len);
        if (!sp || !cgba_swi_bulk_video_copy(eff_dst, sp, len))
          return 0;
        if (pred->r0_from_last_word)
          reg[0] = cgba_le32_read(sp + len - 4);
        else
          reg[0] = pred->r0;
      } else {
        const u8 *vp = cgba_bulk_src_host(eff_src, width);
        if (!vp || !cgba_swi_bulk_video_fill(eff_dst, vp, width, len))
          return 0;
        reg[0] = pred->r0;
      }
    } else {
      int smc_changed = 0;
      if (!fill) {
        sp = cgba_bulk_src_host(eff_src, len);
        if (!sp)
          return 0;
        if (cgba_bulk_ranges_overlap(sp, d, len)) {
#if CGBA_SH4_SWI_FORWARD_OVERLAP
          if (!cgba_sh4_swi_is_forward_overlap(d, sp, len))
            return 0;                            /* equal/backward: interpreter */
          smc_changed = cgba_smc_tags_nonzero(tags, len);
          cgba_sh4_swi_copy_forward(d, sp, len, width);
#else
          return 0;                              /* overlap: interpreter semantics */
#endif
        } else {
          smc_changed = cgba_smc_copy_changes_tagged(d, sp, tags, len);
          memmove(d, sp, len);
        }
      } else {
        const u8 *vp = cgba_bulk_src_host(eff_src, width);
        u32 i;
        u8 b[4];
        if (!vp)
          return 0;
        b[0] = vp[0]; b[1] = vp[1];
        if (word) { b[2] = vp[2]; b[3] = vp[3]; }
        smc_changed = cgba_smc_fill_changes_tagged(d, b, width, tags, len);
        for (i = 0; i < len; i += width)
          memcpy(d + i, b, width);
      }
      if (smc_changed)
        cgba_store_alert |= CPU_ALERT_SMC;
      if (pred->r0_from_last_word)
        reg[0] = cgba_le32_read(d + len - 4);
      else
        reg[0] = pred->r0;
    }
  } else {
    reg[0] = pred->r0;
  }
  reg[1] = pred->r1;
  return 1;
}

#ifdef CGBA_GPSP_HEADLESS_TEST
u32 cgba_bios_hle_swi_count;
u32 cgba_swi_miss[48];
u32 cgba_psr_fb[8];
u32 cgba_cap_src[8];
u32 cgba_bios_other_pc[8];
#endif

/* At the SWI vector in SVC mode: run the handled routines natively and
 * return-from-SWI (movs pc,lr: pc = lr_svc, CPSR = SPSR_svc). Returns 0 for
 * anything else — the interpreter path is unchanged for those. */
/* Per-SWI interp fallback census: [num] counts SWIs the HLE declined (so the
 * real BIOS interprets them). Defined unconditionally so diag prints link
 * everywhere; incremented only under CGBA_SH4_DIAG_COUNTERS. */
unsigned long cgba_swi_interp_n[32];
unsigned long cgba_swi_interp_words[32];   /* CpuSet/FastSet/UnComp payload */

enum cgba_swi_hle_action {
  CGBA_SWI_ACT_NONE      = 0,  /* fall through to interpreter */
  CGBA_SWI_ACT_FASTSET   = 3,
  CGBA_SWI_ACT_CPUSET    = 4,
  CGBA_SWI_ACT_OBJAFFINE = 5,
  CGBA_SWI_ACT_LZ77      = 6,
  CGBA_SWI_ACT_ATOMIC    = 7,  /* result committed; remaining in cgba_swi_arctan_rem */
  CGBA_SWI_ACT_HALT      = 8,
  CGBA_SWI_ACT_INTRWAIT  = 9,
  CGBA_SWI_ACT_VBLANKIW  = 10,
  CGBA_SWI_ACT_BGAFFINE  = 11,
  CGBA_SWI_ACT_RL        = 12
};

static int cgba_hle_bios_swi_num(u32 num, u32 budget);

static int cgba_hle_bios_swi(u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];
  u32 num;
  int handled;

  if (spsr_v & 0x20)
    num = execute_load_u16(lr - 2) & 0xFF;
  else
    num = (execute_load_u32(lr - 4) >> 16) & 0xFF;

  handled = cgba_hle_bios_swi_num(num, budget);
#if CGBA_SH4_DIAG_COUNTERS
  if (!handled && num < 32) {
    cgba_swi_interp_n[num]++;
    if (num == 0x0B || num == 0x0C)
      cgba_swi_interp_words[num] += reg[2] & 0x1FFFFFu;
    else if (num >= 0x11 && num <= 0x18 && reg[0] < 0x10000000u) {
      /* UnComp family: header word at [r0] has size<<8 */
      const u8 *hp = cgba_bulk_src_host(reg[0] & ~3u, 4);
      if (hp)
        cgba_swi_interp_words[num] += cgba_le32_read(hp) >> 8;
    }
  }
#endif
  return handled;
}

/* Exact open-BIOS ArcTan polynomial (0x04e4..0x0568) & SWI 0x09/0x0A HLE.
 * Reproduces r0, r1 (Div's unsigned remainder on ArcTan2), and exact GBA
 * cycle count when the call fits within the current event-slice budget. */
static s32 cgba_bios_arctan_poly(s32 r0)
{
  s32 r3 = (s32)((u32)r0 * (u32)r0) >> 14;
  r3 = (s32)(0u - (u32)r3);
  s32 r2 = (s32)((u32)r3 + ((u32)r3 << 1));
  r2 = (s32)(((u32)r2 << 3) - (u32)r2);
  r2 = (s32)((u32)r3 + ((u32)r2 << 3));
  r2 = (s32)((u32)(r2 >> 14) + 0x390u);
  r2 = (s32)((u32)r3 * (u32)r2) >> 14;
  r2 = (s32)((u32)r2 + 0x91cu);
  r2 = (s32)((u32)r3 * (u32)r2) >> 14;
  r2 = (s32)((u32)r2 + 0xfb6u);
  r2 = (s32)((u32)r3 * (u32)r2) >> 14;
  r2 = (s32)((u32)r2 + 0x16aau);
  r2 = (s32)((u32)r3 * (u32)r2) >> 14;
  r2 = (s32)((u32)r2 + 0x2080u);
  r2 = (s32)((u32)r3 * (u32)r2 + (u32)r3) >> 14;
  r2 = (s32)((u32)r2 + 0x3651u);
  r3 = (s32)((u32)r2 * (u32)r3) >> 14;
  r3 = (s32)((u32)r3 + 0xa2f9u);
  return (s32)((u32)r3 * (u32)r0) >> 16;
}

static s32 cgba_swi_arctan_rem;

static int cgba_swi_arctan_try(u32 num, u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];
  int thumb = (spsr_v & 0x20u) != 0u;
  u32 sp_r = REG_MODE(MODE_SYSTEM)[5] >> 24;
  u32 lr_r = (lr & ~1u) >> 24;
  u32 base_c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb);
  u32 total_c, out_r0, out_r1;
  s32 rem;
  int straddled_frame_done = 0;

  if (num == 0x08u) {
    /* SWI 0x08: Sqrt (open_gba_bios.bin 0x04E0 -> 0x0290..0x03C8, branchless).
     * 0x04E0 b 0x0290 (S0+N0) + 78 straight-line insns + 0x03C8 bx lr (S0+N0). */
    u32 n = reg[0], root = 0u, bit = 1u << 30;
    while (bit > n)
      bit >>= 2;
    while (bit != 0u) {
      if (n >= root + bit) {
        n -= root + bit;
        root = (root >> 1) + bit;
      } else {
        root >>= 1;
      }
      bit >>= 2;
    }
    total_c = base_c + 80u * CGBA_WS_S(0, 1) + 2u * CGBA_WS_N(0, 1);
    out_r0 = root;
    out_r1 = reg[1];
  } else if (num == 0x09u) {
    total_c = base_c + 35u;
    out_r0 = (u32)cgba_bios_arctan_poly((s32)reg[0]);
    out_r1 = reg[1];
  } else {
    /* SWI 0x0A: ArcTan2 (open_gba_bios.bin 0x056c..0x0610) */
    u32 sp_s = CGBA_WS_S(sp_r, 1);
    s32 x = (s32)reg[0];
    s32 y = (s32)reg[1];
    if (y == 0) {
      total_c = base_c + 10u + 8u * sp_s;
      out_r0 = (u32)((x >> 16) & 0x8000);
      out_r1 = reg[1];
    } else if (x == 0) {
      total_c = base_c + 17u + 8u * sp_s;
      out_r0 = (u32)(((y >> 16) & 0x8000) + 0x4000);
      out_r1 = reg[1];
    } else {
      u32 mx = (u32)(x >> 31);
      u32 my = (u32)(y >> 31);
      s32 ax = (s32)(((u32)x ^ mx) - mx);
      s32 ay = (s32)(((u32)y ^ my) - my);
      int use_path3 = (ax < ay) || (ax == ay && x < 0 && y < 0);
      s32 dnum = use_path3 ? (s32)((u32)x << 14) : (s32)((u32)y << 14);
      s32 dden = use_path3 ? y : x;
      u32 un = cgba_sh4_abs_s32_bits(dnum);
      u32 ud = cgba_sh4_abs_s32_bits(dden);
      u32 aq, ar, align;
      s32 q, a;
      if (__builtin_expect(un == 0x80000000u || ud == 0x80000000u, 0))
        return 0;
      if (un < ud) {
        aq = 0u; ar = un; align = 0u;
      } else if ((ud & (ud - 1u)) == 0u) {
        if (ud == 1u) {
          aq = un; ar = 0u;
        } else {
          u32 sh = cgba_sh4_bitlen32(ud) - 1u;
          aq = un >> sh;
          ar = un & (ud - 1u);
        }
        align = cgba_sh4_bitlen32(aq);
      } else {
        aq = un / ud;
        ar = un - aq * ud;
        align = cgba_sh4_bitlen32(aq);
      }
      total_c = base_c + 8u * sp_s + (24u + 12u * align) + 35u +
                (use_path3 ? ((ax == ay) ? 33u : 28u)
                           : ((ax == ay) ? 35u : 31u));
      q = ((u32)(dnum ^ dden) >> 31) ? (s32)(0u - aq) : (s32)aq;
      a = cgba_bios_arctan_poly(q);
      if (use_path3) {
        s32 r4 = ((y >> 16) & 0x8000) + 0x4000;
        out_r0 = (u32)(r4 - a);
      } else if (x >= 0) {
        out_r0 = (u32)a + (((u32)y >> 15) & 0x10000u);
      } else {
        out_r0 = (u32)a + 0x8000u;
      }
      out_r1 = ar;
    }
  }

  if ((s32)budget > (s32)total_c) {
    rem = (s32)budget - (s32)total_c;
  } else {
    /* Pure-math SWI straddling a single IRQ-free, DMA-free video event
     * boundary: verify no timer/serial/IRQ/DMA/frame-end can trigger at the
     * boundary, step update_gba(0) cleanly, and charge remaining cycles. */
    extern u32 serial_irq_cycles;
    extern int serial_mode;
    extern u32 cgba_timer_cap_mask;
    u32 dispstat, vcount, ie;
    int irq_en, safe;
    u32 uret;
    s32 new_bud;
    if ((s32)budget <= 0 || cgba_timer_cap_mask != 0u ||
        serial_irq_cycles != 0u || serial_mode != 0 ||
        reg[CPU_HALT_STATE] != CPU_ACTIVE)
      return 0;
    ie = read_ioreg(REG_IE);
    irq_en = !(spsr_v & 0x80u) && (read_ioreg(REG_IME) != 0u);
    if (irq_en && (ie & read_ioreg(REG_IF)) != 0u)
      return 0;
    dispstat = read_ioreg(REG_DISPSTAT);
    vcount = read_ioreg(REG_VCOUNT);
    if ((dispstat & 0x02u) == 0u) {
      safe = (!irq_en || !((dispstat & 0x10u) && (ie & IRQ_HBLANK))) &&
             ((s32)budget + 208 > (s32)total_c);
      if (safe && vcount < 160u && cgba_hblank_dma_mask != 0u) {
        u32 m = cgba_hblank_dma_mask;
        while (m) {
          u32 ch = (u32)__builtin_ctz(m);
          m &= m - 1u;
          if (dma[ch].start_type == DMA_START_HBLANK) {
            safe = 0;
            break;
          }
        }
      }
    } else {
      u32 next_vc = vcount + 1u;
      safe = (next_vc != 228u) &&
             (!irq_en ||
              (!((next_vc == 160u) && (dispstat & 0x08u) && (ie & IRQ_VBLANK)) &&
               !((next_vc == (dispstat >> 8)) && (dispstat & 0x20u) && (ie & IRQ_VCOUNT)))) &&
             ((s32)budget + 896 > (s32)total_c);
      if (safe && next_vc == 160u) {
        u32 ch;
        for (ch = 0u; ch < 4u; ch++) {
          if (dma[ch].start_type == DMA_START_VBLANK) {
            safe = 0;
            break;
          }
        }
      }
    }
    if (!safe)
      return 0;
    uret = update_gba(0);
    if (completed_frame(uret)) {
      straddled_frame_done = 1;
      rem = 0;
    } else {
      new_bud = (s32)cycles_to_run(uret);
      rem = new_bud - ((s32)total_c - (s32)budget);
    }
  }

  /* Once update_gba(0) has stepped hardware state, never decline back to the
   * interpreter with the stale pre-step cycle budget: commit the pure-math
   * result and forward frame completion or any remaining overrun via
   * cgba_parked_swi_finish(). */
  reg[0] = out_r0;
  reg[1] = out_r1;
  reg[REG_PC] = lr & (thumb ? ~1u : ~3u);
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
  if (straddled_frame_done)
    cgba_swi_arctan_rem = (s32)0x80000000u;
  else if (rem <= 0)
    cgba_swi_arctan_rem = (s32)cgba_parked_swi_finish(rem);
  else
    cgba_swi_arctan_rem = rem;
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_swi_count++;
#endif
  return 7;
}

/* ---- Parked/resumable Halt (SWI 0x02) & IntrWait/VBlankIntrWait (0x04/0x05)
 * Games with HBlank IRQs (e.g. G0157_BNCJ, G0047_AJ8J, G0029_B3EJ, G0227_ABIJ)
 * wake up 160-228 times/frame inside Halt (0x03E4) or IntrWait (0x0480).
 * Keeping the halt/IFBIOS loop in native C (with canonical open-BIOS stacks
 * and registers) eliminates 20k-45k execute_arm() entries per 240 frames. */
/* Halt/IntrWait resume PCs live in sh4_open_bios_layout.h. */
static u32 cgba_swi_halt_resume(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 disp_ret_c = 0u;

  if (reg[CPU_MODE] != MODE_SYSTEM || reg[REG_LR] != 0x00000094u)
    return CGBA_FS_DECLINE;
  cgba_parked_swi_leave(0u, &disp_ret_c);
  remaining -= (s32)(disp_ret_c + S0 + N0);
  return cgba_parked_swi_finish(remaining);
}

static u32 cgba_swi_halt_enter(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 lr_r, sp_r;

  sp_r = cgba_parked_swi_enter(0u, &lr_r);
  reg[2] = 0u;
  reg[3] = 0x04000000u;
  reg[12] = 0x000003D8u;
  reg[REG_PC] = CGBA_HALT_RESUME;
  remaining -= (s32)(cgba_swi_dispatch_entry_cycles(sp_r, lr_r) +
                     3u * S0 + CGBA_WS_N(4, 0));
  reg[CPU_HALT_STATE] = CPU_HALT;

  while (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
    u32 ret = update_gba(remaining);
    if (completed_frame(ret))
      return 0x80000000u;
    remaining = (s32)cycles_to_run(ret);
  }
  if (reg[REG_PC] == 0x00000018u && reg[CPU_MODE] == MODE_IRQ)
    return cgba_parked_swi_irq_exit(remaining, S0, N0);
  if (reg[REG_PC] == CGBA_HALT_RESUME)
    return cgba_swi_halt_resume(remaining);
  return (u32)remaining;
}

static u32 cgba_swi_intrwait_finish(s32 remaining, u32 S0, u32 N0,
                                    u32 pre_bx_cycles)
{
  u32 disp_ret_c = 0u;

  write_ioreg(REG_IME, 1);
  if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && !(reg[REG_CPSR] & 0x80u)) {
    reg[3] = 1u;
    reg[REG_PC] = 0x000004A4u;
    remaining -= (s32)pre_bx_cycles;
    if (check_and_raise_interrupts())
      return cgba_parked_swi_irq_exit(remaining, S0, N0);
  }
  cgba_parked_swi_leave(0u, &disp_ret_c);
  remaining -= (s32)(disp_ret_c + pre_bx_cycles + S0 + N0);
  return cgba_parked_swi_finish(remaining);
}

static u32 cgba_swi_intrwait_loop(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);

  for (;;) {
    /* 0x0470: REG_IME = 1 */
    write_ioreg(REG_IME, 1);
    if ((read_ioreg(REG_IE) & read_ioreg(REG_IF)) && !(reg[REG_CPSR] & 0x80u)) {
      /* Rare: an IRQ is already pending when IME becomes 1 (before HALTCNT).
       * Publish exact canonical state at 0x0478 and vector immediately. */
      reg[3] = 1u;
      reg[REG_PC] = 0x00000478u;
      remaining -= (s32)(2u * S0 + CGBA_WS_N(4, 0));
      if (check_and_raise_interrupts())
        return cgba_parked_swi_irq_exit(remaining, S0, N0);
    }
    /* 0x0478..0x047C: mov r3, #0; strb r3, [r12, #0x301] (HALTCNT = 0) */
    reg[3] = 0u;
    reg[REG_PC] = CGBA_INTRWAIT_RESUME;
    remaining -= (s32)(4u * S0 + 2u * CGBA_WS_N(4, 0));
    reg[CPU_HALT_STATE] = CPU_HALT;

    while (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
      u32 ret = update_gba(remaining);
      if (completed_frame(ret))
        return 0x80000000u;
      remaining = (s32)cycles_to_run(ret);
    }
    if (reg[REG_PC] == 0x00000018u && reg[CPU_MODE] == MODE_IRQ)
      return cgba_parked_swi_irq_exit(remaining, S0, N0);
    if (reg[REG_PC] != CGBA_INTRWAIT_RESUME)
      return (u32)remaining;

    /* 0x0480..0x048C: REG_IME = 0; check IFBIOS & mask */
    write_ioreg(REG_IME, 0);
    {
      u16 ifbios = readaddress16(iwram + 0x8000u, 0x7FF8u);
      u16 hit = ifbios & (u16)reg[1];
      if (!hit) {
        reg[0] = (u32)ifbios;
        reg[3] = 0u;
        remaining -= (s32)(4u * S0 + N0 + CGBA_WS_N(4, 0) + CGBA_WS_N(3, 0));
        continue;
      }
      /* 0x0490..0x04A4: clear matched IFBIOS bits, r0 = hit, REG_IME = 1, bx lr */
      address16(iwram + 0x8000u, 0x7FF8u) = eswap16((u16)(ifbios & (u16)~hit));
      reg[0] = (u32)hit;
      return cgba_swi_intrwait_finish(
        remaining, S0, N0,
        9u * S0 + 2u * CGBA_WS_N(4, 0) + 2u * CGBA_WS_N(3, 0));
    }
  }
}

static u32 cgba_swi_intrwait_resume(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u16 ifbios, hit;

  if (reg[CPU_MODE] != MODE_SYSTEM || reg[REG_LR] != 0x00000094u ||
      reg[12] != 0x04000000u || reg[2] != 0x00000208u || reg[3] != 0u)
    return CGBA_FS_DECLINE;

  /* 0x0480..0x048C: REG_IME = 0; check IFBIOS & r1 */
  write_ioreg(REG_IME, 0);
  ifbios = readaddress16(iwram + 0x8000u, 0x7FF8u);
  hit = ifbios & (u16)reg[1];
  if (!hit) {
    reg[0] = (u32)ifbios;
    reg[3] = 0u;
    remaining -= (s32)(4u * S0 + N0 + CGBA_WS_N(4, 0) + CGBA_WS_N(3, 0));
    return cgba_swi_intrwait_loop(remaining);
  }
  address16(iwram + 0x8000u, 0x7FF8u) = eswap16((u16)(ifbios & (u16)~hit));
  reg[0] = (u32)hit;
  return cgba_swi_intrwait_finish(
    remaining, S0, N0,
    9u * S0 + 2u * CGBA_WS_N(4, 0) + 2u * CGBA_WS_N(3, 0));
}

static u32 cgba_swi_intrwait_enter(int vblank_wait, s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 lr_r, sp_r;
  u32 discard, mask;
  u16 ifbios, hit;

  sp_r = cgba_parked_swi_enter(0u, &lr_r);
  remaining -= (s32)cgba_swi_dispatch_entry_cycles(sp_r, lr_r);
  if (vblank_wait) {
    discard = 1u;
    mask = 1u;
    remaining -= (s32)(3u * S0 + N0); /* 0x04D4..0x04DC */
  } else {
    discard = reg[0];
    mask = reg[1] & 0xFFFFu;
  }
  reg[0] = discard;
  reg[1] = mask;
  reg[12] = 0x04000000u;
  reg[2] = 0x00000208u;

  /* 0x0434..0x0454: REG_IME = 0; check IFBIOS & mask */
  write_ioreg(REG_IME, 0);
  ifbios = readaddress16(iwram + 0x8000u, 0x7FF8u);
  hit = ifbios & (u16)mask;
  if (!hit) {
    remaining -= (s32)(9u * S0 + N0 + CGBA_WS_N(4, 0) + CGBA_WS_N(3, 0));
    return cgba_swi_intrwait_loop(remaining);
  }
  /* 0x0458..0x046C: clear matched IFBIOS bits; if discard==0 return now! */
  address16(iwram + 0x8000u, 0x7FF8u) = eswap16((u16)(ifbios & (u16)~hit));
  if (discard == 0u) {
    return cgba_swi_intrwait_finish(
      remaining, S0, N0,
      17u * S0 + N0 + 2u * CGBA_WS_N(4, 0) + 3u * CGBA_WS_N(3, 0));
  }
  remaining -= (s32)(15u * S0 + CGBA_WS_N(4, 0) + 3u * CGBA_WS_N(3, 0));
  return cgba_swi_intrwait_loop(remaining);
}

/* ---- Parked/resumable BgAffineSet (SWI 0x0E) ------------------------------
 * Open-BIOS geometry (0x07E4..0x08D8):
 *   prologue 0x07E4..0x07F8: cmp r2,#0; push {r4-r11} (8 words); sub r2,r2,#1;
 *     beq 0x08D4; add r0,r0,#20; add r1,r1,#16.
 *   loop top 0x07FC: 54 ARM insns + 5*N(src,0) + 2*N(src,1) + 2*N(0,1) +
 *     2*N(0,0) + 4*N(dst,0) + 2*S(dst,1) + N0. Only r0/r1/r2 live across iters.
 *   epilogue 0x08D4..0x08D8: pop {r4-r11}; bx lr (-> 0x94). */
static u32 cgba_ba_entry_cost;

static inline void cgba_swi_bgaffine_step(const u8 *sh, const u8 *sin_tbl,
                                          s16 *pa, s16 *pb, s16 *pc, s16 *pd,
                                          s32 *sx, s32 *sy)
{
  s32 cx = (s32)cgba_le32_read(sh + 0);
  s32 cy = (s32)cgba_le32_read(sh + 4);
  s32 dispx = (s16)((u16)sh[8]  | ((u16)sh[9]  << 8));
  s32 dispy = (s16)((u16)sh[10] | ((u16)sh[11] << 8));
  s32 rx    = (s16)((u16)sh[12] | ((u16)sh[13] << 8));
  s32 ry    = (s16)((u16)sh[14] | ((u16)sh[15] << 8));
  u32 th    = (u32)sh[17]; /* u16[src+16] >> 8 */
  u32 ai    = ((th + 0x40u) & 255u) << 1;
  u32 bi    = (th & 255u) << 1;
  s32 a     = (s16)((u16)sin_tbl[ai] | ((u16)sin_tbl[ai + 1] << 8));
  s32 b     = (s16)((u16)sin_tbl[bi] | ((u16)sin_tbl[bi + 1] << 8));
  s16 dx    = (s16)((rx * a) >> 14);
  s16 dmx   = (s16)((rx * b) >> 14);
  s16 dy    = (s16)((ry * b) >> 14);
  s16 dmy   = (s16)((ry * a) >> 14);
  *pa = dx;
  *pb = (s16)(-dmx);
  *pc = dy;
  *pd = dmy;
  *sx = cx - (s32)dx * dispx + (s32)dmx * dispy;
  *sy = cy - (s32)dy * dispx - (s32)dmy * dispy;
}

static int cgba_swi_bgaffine_can_enter(u32 src, u32 dst, u32 count)
{
  if (count == 0u || count > 512u || ((src | dst) & 3u) != 0u)
    return 0;
  if (!cgba_swi_range_in_region(dst, count * 16u))
    return 0;
  if (!cgba_swi_exact_src_host(src, count * 20u))
    return 0;
  return 1;
}

static void cgba_swi_bgaffine_materialize(void)
{
  u32 S0 = CGBA_WS_S(0, 1);
  u32 src0 = reg[0], dst0 = reg[1], count = reg[2];
  u32 lr_r, sp_r;

  sp_r = cgba_parked_swi_enter(8u, &lr_r);
  reg[0] = src0 + 20u;
  reg[1] = dst0 + 16u;
  reg[2] = count - 1u;
  reg[12] = 0x000007E4u;
  reg[REG_PC] = CGBA_BA_LOOP;
  cgba_ba_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                     + 6u * S0 + 8u * CGBA_WS_S(sp_r, 1);
}

static u32 cgba_swi_bgaffine_engine(s32 remaining)
{
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 rem_count, src, dst, per_elem;
  const u8 *sh;
  const u8 *sin_tbl = bios_rom + 0x2150u;

  if (reg[REG_LR] != CGBA_BIOS_SWI_RET)
    return CGBA_FS_DECLINE;
  rem_count = reg[2] + 1u;
  if (rem_count == 0u || rem_count > 512u)
    return CGBA_FS_DECLINE;
  src = reg[0] - 20u;
  dst = reg[1] - 16u;
  if (((src | dst) & 3u) != 0u || !cgba_swi_range_in_region(dst, rem_count * 16u))
    return CGBA_FS_DECLINE;
  sh = cgba_swi_exact_src_host(src, rem_count * 20u);
  if (!sh)
    return CGBA_FS_DECLINE;

  remaining -= (s32)cgba_ba_entry_cost;
  cgba_ba_entry_cost = 0u;

  per_elem = 54u * S0
           + 5u * CGBA_WS_N(src >> 24, 0) + 2u * CGBA_WS_N(src >> 24, 1)
           + 2u * CGBA_WS_N(0, 1) + 2u * CGBA_WS_N(0, 0)
           + 4u * CGBA_WS_N(dst >> 24, 0) + 2u * CGBA_WS_S(dst >> 24, 1)
           + N0;

  while (1) {
    if (rem_count == 0u) {
      u32 disp_ret_c = 0u;
      u32 sp_r = cgba_parked_swi_leave(8u, &disp_ret_c);
      remaining -= (s32)(disp_ret_c + 2u * S0 + 8u * CGBA_WS_S(sp_r, 1));
      break;
    }
    if (remaining <= 0) {
      u32 park_exit;
      if (cgba_parked_swi_park(&remaining, CGBA_BA_LOOP, S0, N0, &park_exit))
        return park_exit;
      continue;
    }
    {
      s32 fit = remaining / (s32)per_elem;
      u32 k = (fit < 1) ? 1u : ((u32)fit > rem_count ? rem_count : (u32)fit);
      u32 span = k * 16u;
      u8 *tags = NULL;
      u8 *dh = cgba_bulk_ram_host(dst, span, &tags);
      u32 i;
      if (dh) {
        u8 *out = dh;
        int smc_hit = cgba_smc_tags_nonzero(tags, span);
        for (i = 0; i < k; i++) {
          s16 pa, pb, pc, pd;
          s32 sx, sy;
          cgba_swi_bgaffine_step(sh, sin_tbl, &pa, &pb, &pc, &pd, &sx, &sy);
          out[0] = (u8)pa; out[1] = (u8)((u16)pa >> 8);
          out[2] = (u8)pb; out[3] = (u8)((u16)pb >> 8);
          out[4] = (u8)pc; out[5] = (u8)((u16)pc >> 8);
          out[6] = (u8)pd; out[7] = (u8)((u16)pd >> 8);
          cgba_le32_write(out + 8, (u32)sx);
          cgba_le32_write(out + 12, (u32)sy);
          sh += 20;
          out += 16;
        }
        if (smc_hit)
          flush_translation_cache_ram();
      } else {
        int saved_extra = cgba_sh4_extra_cycles;
        for (i = 0; i < k; i++) {
          u32 d = dst + i * 16u;
          s16 pa, pb, pc, pd;
          s32 sx, sy;
          cgba_swi_bgaffine_step(sh, sin_tbl, &pa, &pb, &pc, &pd, &sx, &sy);
          execute_store_u16(d + 0, (u16)pa);
          execute_store_u16(d + 2, (u16)pb);
          execute_store_u16(d + 4, (u16)pc);
          execute_store_u16(d + 6, (u16)pd);
          execute_store_u32(d + 8, (u32)sx);
          execute_store_u32(d + 12, (u32)sy);
          sh += 20;
        }
        cgba_sh4_extra_cycles = saved_extra;
        cgba_parked_swi_drain_store_alert();
      }
      src += k * 20u;
      dst += k * 16u;
      rem_count -= k;
      reg[0] = src + 20u;
      reg[1] = dst + 16u;
      reg[2] = rem_count - 1u;
      remaining -= (s32)(k * per_elem);
    }
  }
  return cgba_parked_swi_finish(remaining);
}

/* ---- Parked/resumable RLUnCompWram (0x14) & RLUnCompVram (0x15) -----------
 * Open-BIOS geometry:
 *   SWI 0x14 (0x0F18..0x0FB8): push {r4,r5} (2 words), outer loop 0x0F48;
 *     r0 = src, r1 = dst, r2 = len > 0.
 *   SWI 0x15 (0x0FBC..0x10BC): push {r4-r8,r10} (6 words, skips r9!), outer
 *     loop 0x1000; r0 = len > 0, r1 = dst (even), r2 = byteShift (0/8),
 *     r3 = writeVal, r4 = byteCount (0/1), r6 = src. */
static u32 cgba_rl_entry_cost;

static int cgba_swi_rl_can_enter(int vram_mode, u32 *len_out)
{
  u32 src0 = reg[0], dst0 = reg[1];
  u32 avail, header, len, dst_off, dst_sz;
  const u8 *hp;
  u8 *tags_base;

  if (!vram_mode && (src0 & 3u) != 0u)
    return 0;
  if (vram_mode && (dst0 & 1u) != 0u)
    return 0;
  hp = cgba_lz_src_page(src0 & ~3u, &avail);
  if (hp && avail >= 4u)
    header = cgba_le32_read(hp);
  else if ((src0 >> 24) >= 8u && (src0 >> 24) <= 0xDu)
    header = read_memory32(src0 & ~3u);
  else
    return 0;
  len = header >> 8;
  if (len == 0u)
    return 0;
  if (((src0 + 4u) & 0x0E000000u) == 0u ||
      (((src0 + 4u) + (len & 0x1FFFFFu)) & 0x0E000000u) == 0u)
    return 0;
  if (!cgba_lz_dst_region(dst0, &dst_off, &dst_sz, &tags_base))
    return 0;
  if (len > dst_sz || dst_off + len > dst_sz)
    return 0;
  *len_out = len;
  return 1;
}

static void cgba_swi_rl_materialize(int vram_mode, u32 len)
{
  u32 S0 = CGBA_WS_S(0, 1);
  u32 src0 = reg[0], dst0 = reg[1];
  u32 lr_r, sp_r;

  if (vram_mode) {
    /* 0x0FBC pushes {r4,r5,r6,r7,r8,r10} (6 words, skipping r9!). */
    u32 sp_usr;
    int saved_extra;
    sp_r = cgba_parked_swi_enter(0u, &lr_r);
    saved_extra = cgba_sh4_extra_cycles;
    sp_usr = reg[REG_SP];
    execute_store_u32(sp_usr - 4,  reg[10]);
    execute_store_u32(sp_usr - 8,  reg[8]);
    execute_store_u32(sp_usr - 12, reg[7]);
    execute_store_u32(sp_usr - 16, reg[6]);
    execute_store_u32(sp_usr - 20, reg[5]);
    execute_store_u32(sp_usr - 24, reg[4]);
    reg[REG_SP] = sp_usr - 24u;
    cgba_sh4_extra_cycles = saved_extra;
    cgba_parked_swi_drain_store_alert();

    reg[0] = len;
    reg[1] = dst0;
    reg[2] = 0u;
    reg[3] = 0u;
    reg[4] = 0u;
    reg[6] = src0 + 4u;
    reg[12] = 0x00000FBCu;
    reg[REG_PC] = CGBA_RL16_LOOP;
    cgba_rl_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                       + 17u * S0 + 6u * CGBA_WS_S(sp_r, 1)
                       + CGBA_WS_N(src0 >> 24, 1);
  } else {
    sp_r = cgba_parked_swi_enter(2u, &lr_r);
    reg[0] = src0 + 4u;
    reg[1] = dst0;
    reg[2] = len;
    reg[12] = 0x00000F18u;
    reg[REG_PC] = CGBA_RL8_LOOP;
    cgba_rl_entry_cost = cgba_swi_dispatch_entry_cycles(sp_r, lr_r)
                       + 12u * S0 + 2u * CGBA_WS_S(sp_r, 1)
                       + CGBA_WS_N(src0 >> 24, 1);
  }
}

static u32 cgba_swi_rl_engine(s32 remaining)
{
  int vram_mode = (reg[REG_PC] == CGBA_RL16_LOOP);
  u32 loop_pc = vram_mode ? CGBA_RL16_LOOP : CGBA_RL8_LOOP;
  u32 S0 = CGBA_WS_S(0, 1), N0 = CGBA_WS_N(0, 1);
  u32 src = vram_mode ? reg[6] : reg[0];
  u32 dst = reg[1];
  u32 len = vram_mode ? reg[0] : reg[2];
  u32 bshf = vram_mode ? reg[2] : 0u;
  u32 wval = vram_mode ? reg[3] : 0u;
  u32 bcnt = vram_mode ? reg[4] : 0u;
  u32 dst_off, dst_sz;
  u8 *tags_base = NULL;
  u8 *dh_base;
  u32 src_r = src >> 24, dst_r = dst >> 24;
  u32 Nsrc, Ndst, slice_dst_start;
  u32 exit_r0 = src, exit_r1 = dst;
  const u8 *sp = NULL;
  u32 s_avail = 0u;

  if (reg[REG_LR] != CGBA_BIOS_SWI_RET || len == 0u)
    return CGBA_FS_DECLINE;
  if (vram_mode && ((dst & 1u) != 0u || bcnt > 1u || bshf != (bcnt << 3)))
    return CGBA_FS_DECLINE;
  dh_base = cgba_lz_dst_region(dst, &dst_off, &dst_sz, &tags_base);
  if (!dh_base || len > dst_sz || dst_off + len > dst_sz)
    return CGBA_FS_DECLINE;
  if (vram_mode && dst_off + ((len + bcnt) & ~1u) > dst_sz)
    return CGBA_FS_DECLINE;

  remaining -= (s32)cgba_rl_entry_cost;
  cgba_rl_entry_cost = 0u;

  sp = cgba_lz_src_page(src, &s_avail);
  if (!sp)
    s_avail = 0u;
  Nsrc = CGBA_WS_N(src_r, 0);
  Ndst = CGBA_WS_N(dst_r, 0);
  slice_dst_start = dst_off;

  if (dst_r == 6u)
    cgba_video_flush_before_write();

  while (1) {
    if (len == 0u) {
      u32 written = dst_off - slice_dst_start;
      if (written > 0u) {
        if (dst_r == 6u)
          mark_vram_dirty_span(slice_dst_start, written);
        else if (tags_base && cgba_smc_tags_nonzero(tags_base + slice_dst_start, written))
          flush_translation_cache_ram();
      }
      if (vram_mode) {
        u32 sp_usr = reg[REG_SP];
        u32 disp_ret_c = 0u, sp_r;
        int saved_extra = cgba_sh4_extra_cycles;
        reg[4]  = execute_load_u32(sp_usr + 0);
        reg[5]  = execute_load_u32(sp_usr + 4);
        reg[6]  = execute_load_u32(sp_usr + 8);
        reg[7]  = execute_load_u32(sp_usr + 12);
        reg[8]  = execute_load_u32(sp_usr + 16);
        reg[10] = execute_load_u32(sp_usr + 20);
        reg[REG_SP] = sp_usr + 24u;
        cgba_sh4_extra_cycles = saved_extra;
        reg[0] = 0u;
        reg[1] = dst;
        sp_r = cgba_parked_swi_leave(0u, &disp_ret_c);
        remaining -= (s32)(disp_ret_c + 2u * S0 + N0 + 6u * CGBA_WS_S(sp_r, 1));
      } else {
        u32 disp_ret_c = 0u, sp_r;
        reg[0] = exit_r0;
        reg[1] = exit_r1;
        sp_r = cgba_parked_swi_leave(2u, &disp_ret_c);
        remaining -= (s32)(disp_ret_c + 2u * S0 + N0 + 2u * CGBA_WS_S(sp_r, 1));
      }
      break;
    }

    if (remaining <= 0) {
      u32 written = dst_off - slice_dst_start;
      if (written > 0u) {
        if (dst_r == 6u)
          mark_vram_dirty_span(slice_dst_start, written);
        else if (tags_base && cgba_smc_tags_nonzero(tags_base + slice_dst_start, written))
          flush_translation_cache_ram();
        slice_dst_start = dst_off;
      }
      reg[1] = dst;
      if (vram_mode) {
        reg[0] = len;
        reg[2] = bshf;
        reg[3] = wval;
        reg[4] = bcnt;
        reg[6] = src;
      } else {
        reg[0] = src;
        reg[2] = len;
      }
      {
        s32 carry = 0, pass = remaining;
        u32 ret;
        if (pass < -64) {
          carry = -pass;
          pass = 0;
        }
        ret = update_gba(pass);
        if (completed_frame(ret)) {
          cgba_rl_entry_cost = (u32)carry;
          return 0x80000000u;
        }
        remaining = (s32)cycles_to_run(ret);
        if (reg[REG_PC] != loop_pc) {
          cgba_rl_entry_cost = (u32)carry;
          return cgba_parked_swi_irq_exit(remaining, S0, N0);
        }
        remaining -= carry;
        if (dst_r == 6u)
          cgba_video_flush_before_write();
      }
      continue;
    }

    /* Decode one RLE token (compressed or raw run). */
    {
      u32 run_start_src = src;
      u32 run_start_dst = dst;
      u32 old_bcnt = bcnt;
      u8 d;
      if (s_avail == 0u) {
        sp = cgba_lz_src_page(src, &s_avail);
        if (!sp) s_avail = 0u;
        if ((src >> 24) != src_r) {
          src_r = src >> 24;
          Nsrc = CGBA_WS_N(src_r, 0);
        }
      }
      d = s_avail ? (s_avail--, *sp++) : cgba_lz_read_u8(src);
      src++;

      if (d & 0x80u) {
        u32 run = (u32)(d & 0x7Fu) + 3u;
        u32 cnt = (run < len) ? run : len;
        u8 val;
        if (s_avail == 0u) {
          sp = cgba_lz_src_page(src, &s_avail);
          if (!sp) s_avail = 0u;
          if ((src >> 24) != src_r) {
            src_r = src >> 24;
            Nsrc = CGBA_WS_N(src_r, 0);
          }
        }
        val = s_avail ? (s_avail--, *sp++) : cgba_lz_read_u8(src);
        src++;
        if (!vram_mode) {
          memset(dh_base + dst_off, val, cnt);
          dst_off += cnt;
          dst += cnt;
          len -= cnt;
          if (len == 0u) {
            exit_r0 = run_start_src;
            exit_r1 = run_start_dst;
          }
          remaining -= (s32)(11u * S0 + 2u * Nsrc + cnt * (6u * S0 + Ndst + N0));
        } else {
          u32 k = cnt;
          if (bcnt) {
            u16 hw = (u16)(wval | ((u32)val << 8));
            dh_base[dst_off] = (u8)hw;
            dh_base[dst_off + 1u] = (u8)(hw >> 8);
            dst_off += 2u; dst += 2u;
            bcnt = 0u; bshf = 0u; wval = 0u;
            k--;
          }
          if (k >= 2u) {
            u32 n2 = k & ~1u;
            memset(dh_base + dst_off, val, n2);
            dst_off += n2; dst += n2;
            k -= n2;
          }
          if (k) {
            wval = (u32)val;
            bcnt = 1u;
            bshf = 8u;
          }
          len -= cnt;
          remaining -= (s32)(14u * S0 + 2u * Nsrc + cnt * (12u * S0 + N0) +
                             ((cnt + old_bcnt) >> 1) * Ndst);
        }
      } else {
        u32 run = (u32)(d & 0x7Fu) + 1u;
        u32 cnt = (run < len) ? run : len;
        u32 rem_b = cnt;
        while (rem_b > 0u) {
          if (s_avail == 0u) {
            sp = cgba_lz_src_page(src, &s_avail);
            if (!sp) s_avail = 0u;
            if ((src >> 24) != src_r) {
              src_r = src >> 24;
              Nsrc = CGBA_WS_N(src_r, 0);
            }
          }
          if (!vram_mode && s_avail > 0u) {
            u32 chunk = (rem_b < s_avail) ? rem_b : s_avail;
            u8 *dp = dh_base + dst_off;
            if ((uintptr_t)sp >= (uintptr_t)dp + chunk ||
                (uintptr_t)dp >= (uintptr_t)sp + chunk) {
              memcpy(dp, sp, chunk);
            } else {
              u32 bi;
              for (bi = 0u; bi < chunk; bi++)
                dp[bi] = sp[bi];
            }
            sp += chunk; s_avail -= chunk;
            src += chunk; dst += chunk; dst_off += chunk;
            rem_b -= chunk;
          } else {
            u8 b = s_avail ? (s_avail--, *sp++) : cgba_lz_read_u8(src);
            src++;
            if (!vram_mode) {
              dh_base[dst_off++] = b;
              dst++;
            } else {
              wval |= ((u32)b << bshf);
              if (bcnt) {
                dh_base[dst_off] = (u8)wval;
                dh_base[dst_off + 1u] = (u8)(wval >> 8);
                dst_off += 2u; dst += 2u;
                bcnt = 0u; bshf = 0u; wval = 0u;
              } else {
                bcnt = 1u; bshf = 8u;
              }
            }
            rem_b--;
          }
        }
        len -= cnt;
        if (!vram_mode) {
          if (len == 0u) {
            exit_r0 = cnt;
            exit_r1 = run_start_dst;
          }
          remaining -= (s32)(9u * S0 + Nsrc + cnt * (7u * S0 + Nsrc + Ndst + N0));
        } else {
          remaining -= (s32)(10u * S0 + Nsrc + cnt * (12u * S0 + Nsrc + N0) +
                             ((cnt + old_bcnt) >> 1) * Ndst);
        }
      }
    }
  }
  return cgba_parked_swi_finish(remaining);
}

/* ---- Atomic/fast BitUnPack (SWI 0x10) -------------------------------------
 * Expands len bytes of {1,2,4,8}-bpp source into 32-bit words of
 * {1,2,4,8,16,32}-bpp destination in IWRAM/EWRAM/VRAM when the total cost
 * fits cleanly in the current event slice (larger calls stay on interpreter
 * so mid-unpack IRQs remain exact). */
static int cgba_swi_bitunpack_try(u32 budget)
{
  u32 src = reg[0], dst = reg[1], hdr = reg[2];
  u32 avail, len, bits, data_size, base_raw, base_val, add_base;
  u32 elems_per_byte, out_bits, out_words, out_bytes;
  u32 dst_off, dst_sz, est_c;
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];
  int thumb = (spsr_v & 0x20u) != 0u;
  u32 sp_r = REG_MODE(MODE_SYSTEM)[5] >> 24;
  u32 lr_r = (lr & ~1u) >> 24;
  const u8 *hp, *sp;
  u8 *tags_base = NULL, *dh_base;

  if ((dst & 3u) != 0u || (hdr & 3u) != 0u)
    return 0;
  hp = cgba_resolve_guest_read_span(hdr, &avail);
  if (!hp || avail < 8u)
    return 0;
  len = (u32)hp[0] | ((u32)hp[1] << 8);
  bits = hp[2];
  data_size = hp[3];
  base_raw = cgba_le32_read(hp + 4);
  if (len == 0u ||
      (bits != 1u && bits != 2u && bits != 4u && bits != 8u) ||
      (data_size != 1u && data_size != 2u && data_size != 4u &&
       data_size != 8u && data_size != 16u && data_size != 32u))
    return 0;
  if ((src & 0x0E000000u) == 0u || ((src + len) & 0x0E000000u) == 0u)
    return 0;

  elems_per_byte = 8u / bits;
  out_bits = len * elems_per_byte * data_size;
  out_words = out_bits >> 5;
  out_bytes = out_words << 2;
  if (out_bytes == 0u)
    return 0;

  sp = cgba_swi_exact_src_host(src, len);
  if (!sp)
    return 0;
  dh_base = cgba_lz_dst_region(dst, &dst_off, &dst_sz, &tags_base);
  if (!dh_base || out_bytes > dst_sz || dst_off + out_bytes > dst_sz)
    return 0;

  est_c = cgba_swi_dispatch_cycles(sp_r, lr_r, thumb)
        + 26u + 16u * CGBA_WS_S(sp_r, 1)
        + len * (6u + CGBA_WS_N(src >> 24, 0) + elems_per_byte * 14u)
        + out_words * CGBA_WS_N(dst >> 24, 1);
  if (est_c + 64u > budget)
    return 0;

  add_base = base_raw >> 31;
  base_val = base_raw & 0x7FFFFFFFu;
  if ((dst >> 24) == 6u)
    cgba_video_flush_before_write();
  {
    u8 *out = dh_base + dst_off;
    u32 data = 0u, bwc = 0u, i;
    u32 init_mask = (1u << bits) - 1u;
    int smc_hit = (tags_base != NULL) &&
                  cgba_smc_tags_nonzero(tags_base + dst_off, out_bytes);
    for (i = 0u; i < len; i++) {
      u32 b = sp[i];
      u32 bc;
      for (bc = 0u; bc < 8u; bc += bits) {
        u32 d = (b >> bc) & init_mask;
        u32 temp = (d || add_base) ? (d + base_val) : 0u;
        data |= temp << bwc;
        bwc += data_size;
        if (bwc >= 32u) {
          cgba_le32_write(out, data);
          out += 4;
          data = 0u;
          bwc = 0u;
        }
      }
    }
    if ((dst >> 24) == 6u)
      mark_vram_dirty_span(dst_off, out_bytes);
    else if (smc_hit)
      flush_translation_cache_ram();
  }
  reg[0] = src + len;
  reg[1] = dst + out_bytes;
  reg[REG_PC] = lr & (thumb ? ~1u : ~3u);
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
  cgba_swi_arctan_rem = (s32)budget - (s32)est_c;
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_swi_count++;
#endif
  return 7;
}

static int cgba_hle_bios_swi_num(u32 num, u32 budget)
{
  u32 spsr_v = REG_SPSR(MODE_SUPERVISOR);
  u32 lr = reg[REG_LR];

  switch (num) {
#if !CGBA_SH4_SWI_HLE_VERIFY
  case 0x02:
    if (budget > 0 && reg[CPU_HALT_STATE] == CPU_ACTIVE)
      return CGBA_SWI_ACT_HALT;
    return CGBA_SWI_ACT_NONE;
  case 0x04:
  case 0x05:
    if (budget > 0 && reg[CPU_HALT_STATE] == CPU_ACTIVE)
      return (num == 0x05) ? CGBA_SWI_ACT_VBLANKIW : CGBA_SWI_ACT_INTRWAIT;
    return CGBA_SWI_ACT_NONE;
#endif
  case 0x08:
  case 0x09:
  case 0x0A:
    return cgba_swi_arctan_try(num, budget);
#if CGBA_SH4_SWI_CPUSET_FAITHFUL && !CGBA_SH4_SWI_HLE_VERIFY
  case 0x0E:
    if (budget > 0 && cgba_swi_bgaffine_can_enter(reg[0], reg[1], reg[2])) {
      cgba_swi_bgaffine_materialize();
      return CGBA_SWI_ACT_BGAFFINE;
    }
    return CGBA_SWI_ACT_NONE;
  case 0x0F:
    if (budget > 0 && cgba_swi_objaffine_can_enter(reg[0], reg[1], reg[2], reg[3])) {
      cgba_swi_objaffine_materialize();
      return CGBA_SWI_ACT_OBJAFFINE;
    }
    return CGBA_SWI_ACT_NONE;
  case 0x10:
    return cgba_swi_bitunpack_try(budget);
#endif
#if CGBA_SH4_SWI_CPUSET_FAITHFUL && !CGBA_SH4_SWI_HLE_VERIFY
  case 0x11:
  case 0x12: {
    u32 lz_len = 0;
    int vram_mode = (num == 0x12);
    if (budget > 0 && cgba_swi_lz77_can_enter(vram_mode, &lz_len)) {
      cgba_swi_lz77_materialize(vram_mode, lz_len);
      return CGBA_SWI_ACT_LZ77;
    }
    return CGBA_SWI_ACT_NONE;
  }
  case 0x14:
  case 0x15: {
    u32 rl_len = 0;
    int vram_mode = (num == 0x15);
    if (budget > 0 && cgba_swi_rl_can_enter(vram_mode, &rl_len)) {
      cgba_swi_rl_materialize(vram_mode, rl_len);
      return CGBA_SWI_ACT_RL;
    }
    return CGBA_SWI_ACT_NONE;
  }
#endif
#if CGBA_SH4_SWI_CPUSET_FAITHFUL
  case 0x0B:
  case 0x0C: {
    /* Register- and cycle-faithful fast path; anything unmodeled falls back
       to the interpreted BIOS below (return CGBA_SWI_ACT_NONE). */
    struct cgba_swi_pred pred;
    int ok = (num == 0x0B)
      ? cgba_swi_cpuset_predict(reg[0], reg[1], reg[2], &pred)
      : cgba_swi_cpufastset_predict(reg[0], reg[1], reg[2], &pred);
    if (!ok)
      return CGBA_SWI_ACT_NONE;
    if (pred.cycles + 64u > budget) {
#if !CGBA_SH4_SWI_HLE_VERIFY
      /* Oversized copies & fills: parked/resumable chunked execution,
       * canonical at every pause point so IRQs vector exactly as interpreted. */
      u32 cnt = reg[2] & 0x1FFFFFu, fill = (reg[2] >> 24) & 1;
      if ((num == 0x0C || num == 0x0B) && budget > 0 && cnt != 0) {
        u32 src0 = reg[0];
        u32 end0 = src0 + (((reg[2] << 11) >> 9) & ~0xE00000u);
        if ((src0 & 0x0E000000u) && (end0 & 0x0E000000u)) {
          if (num == 0x0C) {
            cgba_swi_fastset_materialize(fill);
            return CGBA_SWI_ACT_FASTSET;
          }
          cgba_swi_cpuset_materialize(((reg[2] >> 26) & 1) ? 0 : 1, (int)fill);
          return CGBA_SWI_ACT_CPUSET;
        }
      }
#endif
      return CGBA_SWI_ACT_NONE;
    }
#if CGBA_SH4_SWI_HLE_VERIFY
    if (pred.r0_from_last_word) {
      u32 wsrc = reg[0] & ~3u, cn = reg[2] & 0x1FFFFFu;
      u32 wi = cn - 1u;
      const u8 *sp2 = cgba_bulk_src_host(wsrc, cn * 4);
      if (!sp2)
        return CGBA_SWI_ACT_NONE;
#if CGBA_SH4_SWI_FORWARD_OVERLAP
      {
        u32 wdst = reg[1] & ~3u;
        if (wdst > wsrc && wdst - wsrc < cn * 4u) {
          u32 delta_words = (wdst - wsrc) >> 2;
          if (!delta_words)
            return CGBA_SWI_ACT_NONE;
          wi %= delta_words;                   /* earlier stores feed this load */
        }
      }
#endif
      pred.r0 = cgba_le32_read(sp2 + wi * 4u);
      pred.r0_from_last_word = 0;
    }
    cgba_swi_verify.pending = 1;
    cgba_swi_verify.num = num;
    cgba_swi_verify.ret_pc = reg[REG_LR] & ((spsr_v & 0x20u) ? ~1u : ~3u);
    cgba_swi_verify.pred = pred;
    cgba_swi_verify.irq_in0 = cgba_bios_hle_irq_in;
    cgba_swi_verify.a0 = reg[0]; cgba_swi_verify.a1 = reg[1];
    cgba_swi_verify.a2 = reg[2];
    return CGBA_SWI_ACT_NONE;          /* run the real BIOS; compare after */
#else
    if (!cgba_swi_apply_faithful(reg[0], reg[1], reg[2], num == 0x0C, &pred))
      return CGBA_SWI_ACT_NONE;
    cgba_swi_arctan_rem = (s32)budget - (s32)pred.cycles;
    break;                             /* handled -> common HLE tail */
#endif
  }
#endif
  default:
#ifdef CGBA_GPSP_HEADLESS_TEST
    if (num < 48) cgba_swi_miss[num]++;
#endif
    return CGBA_SWI_ACT_NONE;
  }

  if (cgba_store_alert) {
    cpu_alert_type a = cgba_store_alert;
    cgba_store_alert = CPU_ALERT_NONE;
    if (a & CPU_ALERT_SMC)
      flush_translation_cache_ram();
  }
  reg[REG_PC] = lr & ((spsr_v & 0x20u) ? ~1u : ~3u);
  reg[REG_CPSR] = spsr_v;
  set_cpu_mode(cpu_modes[spsr_v & 0xF]);
#ifdef CGBA_GPSP_HEADLESS_TEST
  cgba_bios_hle_swi_count++;
#endif
  return CGBA_SWI_ACT_ATOMIC;
}

u32 cgba_sh4_bios_fallback(u32 cycles)
{
#ifdef CGBA_GPSP_HEADLESS_TEST
  u32 cgba_entry_pc_bios = reg[REG_PC];
#endif
  /* IRQ/SWI HLE fast paths depend on fixed addresses inside the bundled
   * open_gba_bios.bin. When an external BIOS is loaded, bypass HLE and
   * interpret the BIOS image directly (stopping immediately on PC >= 0x4000). */
  if (!CGBA_SH4_DIFF_ACTIVE() && !cgba_using_external_bios) {
  if (reg[REG_PC] == CGBA_BIOS_SWI_VEC &&
      reg[CPU_MODE] == MODE_SUPERVISOR) {
    int h = cgba_hle_bios_swi(cycles);
    u32 r = CGBA_FS_DECLINE;
    switch (h) {
    case CGBA_SWI_ACT_NONE:      break;
    case CGBA_SWI_ACT_ATOMIC:    return (u32)cgba_swi_arctan_rem;
    case CGBA_SWI_ACT_FASTSET:   r = cgba_swi_fastset_engine((s32)cycles); break;
    case CGBA_SWI_ACT_CPUSET:    r = cgba_swi_cpuset_engine((s32)cycles); break;
    case CGBA_SWI_ACT_OBJAFFINE: r = cgba_swi_objaffine_engine((s32)cycles); break;
    case CGBA_SWI_ACT_LZ77:      r = cgba_swi_lz77_engine((s32)cycles); break;
    case CGBA_SWI_ACT_HALT:      r = cgba_swi_halt_enter((s32)cycles); break;
    case CGBA_SWI_ACT_INTRWAIT:  r = cgba_swi_intrwait_enter(0, (s32)cycles); break;
    case CGBA_SWI_ACT_VBLANKIW:  r = cgba_swi_intrwait_enter(1, (s32)cycles); break;
    case CGBA_SWI_ACT_BGAFFINE:  r = cgba_swi_bgaffine_engine((s32)cycles); break;
    case CGBA_SWI_ACT_RL:        r = cgba_swi_rl_engine((s32)cycles); break;
    default:                     return cycles;
    }
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM) {
    if (reg[REG_PC] == CGBA_HALT_RESUME) {
      u32 r = cgba_swi_halt_resume((s32)cycles);
      if (r != CGBA_FS_DECLINE)
        return r;
    } else if (reg[REG_PC] == CGBA_INTRWAIT_RESUME) {
      u32 r = cgba_swi_intrwait_resume((s32)cycles);
      if (r != CGBA_FS_DECLINE)
        return r;
    }
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_FS_COPY_LOOP || reg[REG_PC] == CGBA_FS_FILL_LOOP)) {
    /* Re-entry at a canonical FastSet chunk top (ISR returned into a parked
     * copy, or the interpreter stopped exactly there). Declined -> the
     * interpreter continues the real BIOS from the same state. */
    u32 r = cgba_swi_fastset_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_CS_WCOPY_LOOP || reg[REG_PC] == CGBA_CS_HCOPY_LOOP ||
       reg[REG_PC] == CGBA_CS_WFILL_LOOP || reg[REG_PC] == CGBA_CS_HFILL_LOOP)) {
    /* Re-entry at a canonical CpuSet chunk top (ISR returned mid-copy/fill). */
    u32 r = cgba_swi_cpuset_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      reg[REG_PC] == CGBA_BA_LOOP) {
    u32 r = cgba_swi_bgaffine_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      reg[REG_PC] == CGBA_OA_LOOP) {
    u32 r = cgba_swi_objaffine_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_LZ8_LOOP || reg[REG_PC] == CGBA_LZ16_LOOP)) {
    u32 r = cgba_swi_lz77_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_SYSTEM &&
      (reg[REG_PC] == CGBA_RL8_LOOP || reg[REG_PC] == CGBA_RL16_LOOP)) {
    u32 r = cgba_swi_rl_engine((s32)cycles);
    if (r != CGBA_FS_DECLINE)
      return r;
  }
  if (reg[CPU_MODE] == MODE_IRQ) {
    if (reg[REG_PC] == 0x00000018u) {
      if (cgba_hle_bios_irq_entry() != 0)
        return cycles;
    } else if (reg[REG_PC] == 0x00000030u) {
      u32 np = cgba_hle_bios_irq_exit();
      if (np == CGBA_HALT_RESUME && reg[CPU_MODE] == MODE_SYSTEM) {
        u32 r = cgba_swi_halt_resume((s32)cycles);
        if (r != CGBA_FS_DECLINE)
          return r;
      } else if (np == CGBA_INTRWAIT_RESUME && reg[CPU_MODE] == MODE_SYSTEM) {
        u32 r = cgba_swi_intrwait_resume((s32)cycles);
        if (r != CGBA_FS_DECLINE)
          return r;
      } else if (np != 0x00000018u)
        return cycles;
      if (np == 0x00000018u && cgba_hle_bios_irq_entry() != 0)   /* immediate pending re-entry */
        return cycles;
      /* odd handler: fall through and interpret from the vector */
    }
  }
  }
  /* Interpret ONLY while the PC is inside the BIOS (region-exit stop mode,
   * cpu.cc CGBA_DIFF_STOP_CHECK): the instant execution reaches game code
   * (PC >= 0x4000) control returns and the stub re-dispatches into the JIT.
   * This holds for every entry — SWI (BIOS body, incl. the IntrWait halt
   * loop, which the interpreter fast-forwards), and IRQ vectors (0x18 -> the
   * ~10-instruction BIOS wrapper -> the GAME's handler, which now runs
   * translated; the handler's return to the BIOS epilogue comes back here
   * for the final ldm/subs). The previous version only stopped at a known
   * SWI return LR, so every VBlank/HBlank IRQ interpreted the whole rest of
   * the frame — the profile showed 68%% of a JIT run inside execute_arm. */
  cgba_diff_stop_on_bios_exit = 1;
  cgba_diff_stop_active = 1;
  cgba_diff_stop_skip_initial = 0;
  cgba_diff_stop_cycles_remaining = (s32)cycles;
  execute_arm(cycles);
  cgba_diff_stop_active = 0;
  cgba_diff_stop_on_bios_exit = 0;
#if CGBA_SH4_SWI_HLE_VERIFY
  if (cgba_swi_verify.pending) {
    cgba_swi_verify.pending = 0;
    /* Only comparable when the interpreter ran the whole SWI and returned
       to the caller (an IRQ vector or budget exhaustion mid-BIOS lands
       elsewhere — discard those records). */
    {
      extern int cgba_diff_trace_cycles;
      cgba_diff_trace_cycles = 0;
    }
    if (reg[REG_PC] == cgba_swi_verify.ret_pc &&
        cgba_bios_hle_irq_in == cgba_swi_verify.irq_in0 &&
        cgba_diff_stop_cycles_remaining >= 0 &&
        (s32)cycles > cgba_diff_stop_cycles_remaining) {
      u32 used = (u32)((s32)cycles - cgba_diff_stop_cycles_remaining);
      cgba_swi_verify_checked++;
      if (reg[0] != cgba_swi_verify.pred.r0) {
        cgba_swi_verify_bad++;
        cgba_swi_verify_report("r0", reg[0], cgba_swi_verify.pred.r0);
      }
      if (reg[1] != cgba_swi_verify.pred.r1) {
        cgba_swi_verify_bad++;
        cgba_swi_verify_report("r1", reg[1], cgba_swi_verify.pred.r1);
      }
      if (used != cgba_swi_verify.pred.cycles) {
        static unsigned dumped;
        cgba_swi_verify_bad++;
        cgba_swi_verify_report(cgba_swi_verify.num == 0x0B ? "cycB" : "cycC",
                               used, cgba_swi_verify.pred.cycles);
        if (dumped < 8) {
          dumped++;
          cgba_swi_verify_report("args01", cgba_swi_verify.a0,
                                 cgba_swi_verify.a1);
          cgba_swi_verify_report("args2L", cgba_swi_verify.a2,
                                 cgba_swi_verify.ret_pc);
          cgba_swi_verify_report("budrem", cycles,
                                 (u32)cgba_diff_stop_cycles_remaining);
          cgba_swi_verify_report("pcr0r1",
                                 (reg[0] << 16) | (reg[1] & 0xFFFFu),
                                 reg[REG_PC]);
        }
      }
    }
  }
#endif
#if defined(CGBA_GPSP_HEADLESS_TEST) || defined(CGBA_SH4_PROFILE_COUNTERS)
  cgba_sh4_bios_fallback_call_count++;
  {
    s32 used = (s32)cycles - cgba_diff_stop_cycles_remaining;
    if (used > 0)
      cgba_sh4_bios_fallback_cycle_count += (u32)used;
  }
#endif
#ifdef CGBA_GPSP_HEADLESS_TEST
  /* classify entries: SWI vector / IRQ vector / mid-BIOS resume */
  if (cgba_entry_pc_bios == 0x08) cgba_bios_entry_swi++;
  else if (cgba_entry_pc_bios == 0x18) cgba_bios_entry_irq++;
  else {
    unsigned oi;
    cgba_bios_entry_other++;
    for (oi = 0; oi < 8; oi++) {
      if (cgba_bios_other_pc[oi] == cgba_entry_pc_bios) break;
      if (cgba_bios_other_pc[oi] == 0) { cgba_bios_other_pc[oi] = cgba_entry_pc_bios; break; }
    }
  }
#endif

  if (reg[REG_PC] >= 0x00004000u) {
    /* Left the BIOS. */
    s32 remaining = cgba_diff_stop_cycles_remaining;
    if (remaining <= 0) {
      u32 ret = update_gba(remaining);
      return completed_frame(ret) ? ret : cycles_to_run(ret);
    }
    return (u32)remaining;
  }
  return 0x80000000u;                    /* frame completed inside the BIOS */
}

