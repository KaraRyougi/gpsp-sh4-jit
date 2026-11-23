/* Internal module included by sh4_interp_helpers.c: guest memory accessors and flags. */
/* ---- guest memory accessors (backend-provided wrappers over gba_memory.c) --
 * The MIPS/ARM/x86 backends supply the execute_load / execute_store family that
 * translated code calls. For SH4, the C wrappers also accumulate the GBA memory
 * access cycles so emitted code can debit R13 after a helper returns. */
/* A store can raise a CPU alert: DMA/HALT idle (CPU_HALT_STATE set), a newly
 * pending IRQ, or SMC (the guest overwrote code). write_memory* return it; we
 * accumulate it here and the ldst helpers consume it after their store(s) via
 * cgba_store_alert_break(). Dropping it (the old behavior) meant the dynarec
 * never idled, never took a store-raised IRQ, and never flushed self-modified
 * code — it just ran past, diverging from the interpreter. */
static cpu_alert_type cgba_store_alert;
int cgba_sh4_extra_cycles;
static int cgba_sh4_mem_cycle_seq;

static void cgba_sh4_reset_mem_cycles(int seq)
{
  cgba_sh4_extra_cycles = 0;
  cgba_sh4_mem_cycle_seq = seq;
}

/* Generated blocks keep their live PC in host state and commit reg[REG_PC]
 * only at dispatch/event boundaries. Generic memory fallbacks are older than
 * that ABI: open-bus reads and BIOS protection inspect reg[REG_PC] directly.
 * Publish the interpreter-visible PC before the fallback. Generated control
 * flow remains authoritative and overwrites it at the normal commit point. */
static inline void cgba_sh4_begin_mem_helper(u32 access_pc)
{
  reg[REG_PC] = access_pc;
}

static void cgba_sh4_charge_mem(u32 address, unsigned size_index)
{
  if (address < 0x10000000u) {
    u32 region = address >> 24;
    cgba_sh4_extra_cycles += cgba_sh4_mem_cycle_seq ?
      ws_cyc_seq[region][size_index] : ws_cyc_nseq[region][size_index];
  }
}

u32 function_cc execute_load_u8(u32 address)  { cgba_sh4_charge_mem(address, 0); return read_memory8(address); }
u32 function_cc execute_load_u16(u32 address) { cgba_sh4_charge_mem(address, 0); return read_memory16(address); }
u32 function_cc execute_load_u32(u32 address) { cgba_sh4_charge_mem(address, 1); return read_memory32(address); }
u32 function_cc execute_load_s8(u32 address)  { cgba_sh4_charge_mem(address, 0); return read_memory8s(address); }
u32 function_cc execute_load_s16(u32 address) { cgba_sh4_charge_mem(address, 0); return read_memory16s(address); }

static u8 *cgba_bulk_ram_host(u32 addr, u32 len, u8 **tags);
static int cgba_swi_bulk_video_copy(u32 dst, const u8 *sh, u32 bytes);
static int cgba_swi_bulk_video_fill(u32 dst, const u8 *vp, u32 width, u32 bytes);

static inline u32 cgba_le32_read(const u8 *p)
{
  return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline void cgba_le32_write(u8 *p, u32 v)
{
  p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 cgba_sh4_align_store_address(u32 address, unsigned bytes)
{
  return address & ~(u32)(bytes - 1);
}

/* Self-modifying-code detection for RAM stores. gpSP marks every byte that
 * belongs to a translated block in a parallel "tag" mirror (IWRAM: iwram[off];
 * EWRAM: ewram[off + 0x40000]); a nonzero tag over the written range means the
 * guest just overwrote code, so the RAM translation cache must be dropped (done
 * by cgba_store_alert_break on CPU_ALERT_SMC). write_memory*() returns
 * CPU_ALERT_NONE for RAM — the native gpSP backends do this check inline, but the
 * SH4 port stores through these C helpers, so it lives here. The tag is read
 * byte-wise (the target faults on unaligned wide loads). Mirrors
 * dma_write_iwram / dma_write_ewram in gba_memory.c. */
#ifdef SH4_ARCH
extern void cgba_sh4_note_iwram_smc(u32 iwram_off);
#endif

static cpu_alert_type cgba_sh4_smc_precheck(u32 address, unsigned bytes,
                                            u32 new_val)
{
  const u8 *tag;
  const u8 *data;
  /* Width-align like the ARM bus (and the gpSP interpreter's fast_write_memory,
   * cpu.cc) so the tag read lands on the same code bytes the access really hits
   * and never walks past the end of the mirror for an unaligned half/word. */
  address = cgba_sh4_align_store_address(address, bytes);
  switch (address >> 24) {
  case 0x03: {
    u32 off = address & 0x7FFFu;
    tag = iwram + off;
    data = iwram + 0x8000u + off;
    break;
  }
  case 0x02: {
    u32 off = address & 0x3FFFFu;
    tag = ewram + off + 0x40000u;
    data = ewram + off;
    break;
  }
  default:
    return CPU_ALERT_NONE;
  }
  if (cgba_smc_scalar_changes_tagged(data, tag, bytes, new_val)) {
#ifdef SH4_ARCH
    if ((address >> 24) == 0x03u)
      cgba_sh4_note_iwram_smc(address & 0x7FFFu);
#endif
    return CPU_ALERT_SMC;
  }
  return CPU_ALERT_NONE;
}

void function_cc execute_store_u8(u32 address, u32 source)
{
  cgba_sh4_charge_mem(address, 0);
  cgba_store_alert |= cgba_sh4_smc_precheck(address, 1, source);
  cgba_store_alert |= write_memory8(address, (u8)source);
}

void function_cc execute_store_u16(u32 address, u32 source)
{
  u32 region = address >> 24;
  if (region == 0x0Eu || region == 0x0Fu) {
    cgba_sh4_charge_mem(address, 0);
    cgba_store_alert |= write_memory16(address, (u16)source);
    return;
  }
  u32 aligned = cgba_sh4_align_store_address(address, 2);
  cgba_sh4_charge_mem(aligned, 0);
  cgba_sh4_note_dma_ctl16(aligned, source);
  cgba_store_alert |= cgba_sh4_smc_precheck(aligned, 2, source);
  cgba_store_alert |= write_memory16(aligned, (u16)source);
}

void function_cc execute_store_u32(u32 address, u32 source)
{
  u32 region = address >> 24;
  if (region == 0x0Eu || region == 0x0Fu) {
    cgba_sh4_charge_mem(address, 1);
    cgba_store_alert |= write_memory32(address, source);
    return;
  }
  u32 aligned = cgba_sh4_align_store_address(address, 4);
  cgba_sh4_charge_mem(aligned, 1);
  cgba_store_alert |= cgba_sh4_smc_precheck(aligned, 4, source);
  cgba_store_alert |= write_memory32(aligned, source);
}

/* Consume any pending store alert. If set, point PC at the next instruction,
 * flush the RAM code cache on SMC, and return 2 so the emitter glue exits the
 * block through sh4_block_exit -> update_gba (which idles on HALT/DMA and
 * vectors a pending IRQ). Returns 0 — continue the block — when there is no
 * alert, which is always the case after a pure load.
 *
 * Helper return contract (consumed by sh4_helper_exit in sh4_stub.S):
 *   0 = fall through to the next translated instruction;
 *   1 = pure PC change -> re-dispatch WITHOUT an update_gba pass;
 *   2 = store alert    -> exit via sh4_block_exit so update_gba runs now. */
#define CGBA_SH4_HELPER_ALERT 2
static int cgba_store_alert_break(u32 next_pc)
{
  cpu_alert_type a = cgba_store_alert;
  cgba_store_alert = CPU_ALERT_NONE;
  if (!a)
    return 0;
  if (a & CPU_ALERT_SMC)
    flush_translation_cache_ram();
  /* A bare IRQ alert (for example an IME/IE store that unmasks an already
   * pending IRQ) must be serviced at the same scheduler boundary as the
   * interpreter. Vectoring immediately here changes LR_irq and can return into
   * the wrong game routine after the handler's SUBS PC,LR,#4. SMC still exits
   * immediately because stale translated RAM must not continue running. */
  if (a & ~(cpu_alert_type)CPU_ALERT_IRQ) {
    reg[REG_PC] = next_pc;
    /* WAITCNT already invalidated both caches. It needs a pure redispatch,
     * while HALT/SMC/DMA must still enter the scheduler immediately. */
    if ((a & ~(cpu_alert_type)(CPU_ALERT_IRQ | CPU_ALERT_TIMING)) == 0)
      return 1;
    return CGBA_SH4_HELPER_ALERT;
  }
  return 0;
}

/* CPSR flag bits (canonical packed form, matching the interpreter). */
#define CF_N (1u << 31)
#define CF_Z (1u << 30)
#define CF_C (1u << 29)
#define CF_V (1u << 28)

static inline void set_nz(u32 result)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  reg[REG_CPSR] = cpsr;
}

static inline void set_nzcv(u32 result, u32 c, u32 v)
{
  u32 cpsr = reg[REG_CPSR] & ~(CF_N | CF_Z | CF_C | CF_V);
  if (result & 0x80000000u) cpsr |= CF_N;
  if (result == 0)          cpsr |= CF_Z;
  if (c)                    cpsr |= CF_C;
  if (v)                    cpsr |= CF_V;
  reg[REG_CPSR] = cpsr;
}

