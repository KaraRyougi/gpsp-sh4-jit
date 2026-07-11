/* Behavioral regressions for JIT invalidation boundaries.
 *
 * Include the production scheduler and memory implementation directly, as the
 * RTC regression does, then provide only their platform/CPU dependencies.
 * Section GC keeps unrelated emulator entry points out of this host binary.
 */

#define HAVE_DYNAREC 1
#define CGBA_DYNAREC 1
#define ROM_TRANSLATION_CACHE_SIZE (64 * 1024)
#define RAM_TRANSLATION_CACHE_SIZE (64 * 1024)
#define ROM_BRANCH_HASH_BITS 8
#define ROM_BUFFER_SIZE 1

#include "vendor/gpsp/gba_memory.c"
#include "vendor/gpsp/main.c"
#include "vendor/gpsp/savestate.c"

#include <stdio.h>

/* State normally owned by cpu.cc and the platform translation units. */
u32 reg[64];
u32 reg_mode[7][7];
u32 spsr[6];
u32 instruction_count;
u16 oam_ram[512];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u16 io_registers[512];
u8 ewram[1024 * 256 * 2];
u8 iwram[1024 * 32 * 2];
u8 vram[1024 * 96];
u8 *memory_map_read[8 * 1024];

s32 affine_reference_x[2];
s32 affine_reference_y[2];
u16 *gba_screen_pixels;

direct_sound_struct direct_sound_channel[2];
gbc_sound_struct gbc_sound_channel[4];
const s8 square_pattern_duty[4][8] = {{0}};
const u32 sound_frequency = GBA_SOUND_FREQUENCY;
u32 gbc_sound_master_volume_left;
u32 gbc_sound_master_volume_right;
u32 gbc_sound_master_volume;
u32 gbc_sound_buffer_index;
u32 gbc_sound_last_cpu_ticks;
u32 sound_on;

int serial_mode = SERIAL_MODE_DISABLED;
u32 serial_irq_cycles;
u32 netplay_num_clients;
u32 netplay_client_id;
u32 cheat_master_hook = ~0U;

static unsigned full_flushes;
static unsigned ram_flushes;
static int failures;
static u8 state_doc[GBA_STATE_MEM_SIZE];

#define CHECK(condition, ...) do {                                            \
  if (!(condition)) {                                                         \
    failures++;                                                               \
    printf("FAIL %s:%d: ", __FILE__, __LINE__);                              \
    printf(__VA_ARGS__);                                                      \
    putchar('\n');                                                            \
  }                                                                           \
} while (0)

/* Cache functions are the observable test boundary: production WAITCNT calls
 * the full flush, while update_gba performs the scheduled-DMA RAM flush. */
void flush_dynarec_caches(void) { full_flushes++; }
void flush_translation_cache_ram(void) { ram_flushes++; }
void flush_translation_cache_rom(void) { }
void init_dynarec_caches(void) { }
void init_emitter(bool must_swap) { (void)must_swap; }

void update_scanline(void) { }
void video_reload_counters(void) { }
void render_gbc_sound(void) { }
void process_cheats(void) { }
void sound_timer_queue32(u32 channel, u32 value)
{ (void)channel; (void)value; }
void sound_reset_fifo(u32 channel) { (void)channel; }
unsigned sound_timer(fixed8_24 frequency_step, u32 channel)
{ (void)frequency_step; (void)channel; return 0; }

cpu_alert_type write_siocnt(u16 value)
{ (void)value; return CPU_ALERT_NONE; }
cpu_alert_type write_rcnt(u16 value)
{ (void)value; return CPU_ALERT_NONE; }
u32 serial_next_event(void) { return ~0U; }
bool update_serial(unsigned cycles) { (void)cycles; return false; }
u32 serial_get_irq_cycles(void) { return serial_irq_cycles; }
void serial_set_irq_cycles(u32 cycles) { serial_irq_cycles = cycles; }
u32 gbp_get_state(void) { return 0; }
void gbp_set_state(u32 state) { (void)state; }

cpu_alert_type check_interrupt(void) { return CPU_ALERT_NONE; }
cpu_alert_type flag_interrupt(irq_type irq_raised)
{ (void)irq_raised; return CPU_ALERT_NONE; }
u32 check_and_raise_interrupts(void) { return 0; }

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{ (void)stream; (void)offset; (void)seek_position; return -1; }
int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{ (void)stream; (void)data; (void)len; return -1; }

static void reset_fixture(void)
{
  memset(reg, 0, sizeof(reg));
  memset(io_registers, 0, sizeof(io_registers));
  memset(iwram, 0, sizeof(iwram));
  memset(ewram, 0, sizeof(ewram));
  memset(dma, 0, sizeof(dma));
  memset(timer, 0, sizeof(timer));
  full_flushes = 0;
  ram_flushes = 0;
  serial_irq_cycles = 0;
  serial_mode = SERIAL_MODE_DISABLED;
  netplay_client_id = 0;
  cgba_idle_wait = 0;
  cgba_timer_active_mask = 0;
  cgba_timer_cap_mask = 0;
  reg[CPU_HALT_STATE] = CPU_ACTIVE;
}

static void configure_iwram_smc_dma(u32 start_type)
{
  address16(ewram, 0x100) = eswap16(0xBEEF);
  address16(iwram, 0) = 0xFFFF; /* valid translated-block mirror tag */
  dma[0].source_address = 0x02000100;
  dma[0].dest_address = 0x03000000;
  dma[0].length = 1;
  dma[0].repeat_type = DMA_NO_REPEAT;
  dma[0].source_direction = DMA_INCREMENT;
  dma[0].dest_direction = DMA_INCREMENT;
  dma[0].length_type = DMA_16BIT;
  dma[0].start_type = start_type;
  dma[0].irq = DMA_NO_IRQ;
}

static void configure_waitcnt_dma(u32 start_type)
{
  address16(ewram, 0x100) = eswap16(0x0014);
  dma[0].source_address = 0x02000100;
  dma[0].dest_address = 0x04000204;
  dma[0].length = 1;
  dma[0].repeat_type = DMA_NO_REPEAT;
  dma[0].source_direction = DMA_INCREMENT;
  dma[0].dest_direction = DMA_INCREMENT;
  dma[0].length_type = DMA_16BIT;
  dma[0].start_type = start_type;
  dma[0].irq = DMA_NO_IRQ;
}

static void test_waitcnt_invalidation(void)
{
  cpu_alert_type alert;
  u8 old_seq;

  reset_fixture();
  write_ioreg(REG_WAITCNT, 0);
  reload_timing_info();
  old_seq = ws_cyc_seq[0x8][0];

  alert = write_io_register16(0x04000204, 0x0014);
  CHECK(alert == CPU_ALERT_TIMING,
        "effective WAITCNT returned alert 0x%02X", alert);
  CHECK(full_flushes == 1, "effective WAITCNT flushes=%u", full_flushes);
  CHECK(ws_cyc_seq[0x8][0] != old_seq,
        "effective WAITCNT left sequential timing unchanged");

  alert = write_io_register16(0x04000204, 0x4014); /* prefetch bit only */
  CHECK(alert == CPU_ALERT_NONE,
        "ineffective WAITCNT returned alert 0x%02X", alert);
  CHECK(full_flushes == 1, "ineffective WAITCNT flushes=%u", full_flushes);

  alert = write_io_register16(0x04000204, 0x4014); /* identical write */
  CHECK(alert == CPU_ALERT_NONE,
        "identical WAITCNT returned alert 0x%02X", alert);
  CHECK(full_flushes == 1, "identical WAITCNT flushes=%u", full_flushes);
}

static void test_waitcnt_savestate_restore(void)
{
  u8 *p;
  u8 restored_seq;

  reset_fixture();
  write_ioreg(REG_WAITCNT, 0x0010);
  reload_timing_info();
  restored_seq = ws_cyc_seq[0x8][1];

  p = state_doc + 4;                 /* root BSON size header */
  p += memory_write_savestate(p);
  *p++ = 0;                          /* root BSON terminator */
  {
    u8 *hdr = state_doc;
    bson_write_u32(hdr, (u32)(p - state_doc));
  }

  write_ioreg(REG_WAITCNT, 0);
  reload_timing_info();
  CHECK(ws_cyc_seq[0x8][1] != restored_seq,
        "WAITCNT fixture did not change timing");
  CHECK(memory_read_savestate(state_doc), "memory savestate restore failed");
  CHECK(read_ioreg(REG_WAITCNT) == 0x0010,
        "savestate WAITCNT=%04X", read_ioreg(REG_WAITCNT));
  CHECK(ws_cyc_seq[0x8][1] == restored_seq,
        "savestate left derived timing=%u expected=%u",
        ws_cyc_seq[0x8][1], restored_seq);
}

static void test_dma_smc_alert(void)
{
  cpu_alert_type alert;
  int cycles = 0;

  reset_fixture();
  configure_iwram_smc_dma(DMA_START_HBLANK);
  alert = dma_transfer(0, &cycles);
  CHECK((alert & CPU_ALERT_SMC) != 0,
        "tagged IWRAM DMA returned alert 0x%02X", alert);
  CHECK(readaddress16(iwram + 0x8000, 0) == 0xBEEF,
        "DMA did not copy through the production transfer loop");
  CHECK(cycles > 0, "DMA did not report consumed cycles");
}

static void test_scheduled_dma_redispatch(void)
{
  u32 ret;

  reset_fixture();
  configure_iwram_smc_dma(DMA_START_HBLANK);
  execute_cycles = 1;
  video_count = 1;
  write_ioreg(REG_DISPSTAT, 0); /* visible hrefresh -> HBlank */
  write_ioreg(REG_VCOUNT, 0);

  ret = update_gba(0);
  CHECK(ram_flushes == 1, "scheduled DMA RAM flushes=%u", ram_flushes);
  CHECK((ret & 0x40000000u) != 0,
        "scheduled DMA return lacked changed-PC bit: 0x%08X", ret);
  CHECK(readaddress16(iwram + 0x8000, 0) == 0xBEEF,
        "scheduled DMA did not write IWRAM");

  reset_fixture();
  configure_iwram_smc_dma(DMA_START_VBLANK);
  execute_cycles = 1;
  video_count = 1;
  write_ioreg(REG_DISPSTAT, 0x0002); /* HBlank after visible line 159 */
  write_ioreg(REG_VCOUNT, 159);
  ret = update_gba(0);
  CHECK(ram_flushes == 1, "VBlank DMA RAM flushes=%u", ram_flushes);
  CHECK((ret & 0x40000000u) != 0,
        "VBlank DMA return lacked changed-PC bit: 0x%08X", ret);

  reset_fixture();
  configure_iwram_smc_dma(DMA_START_HBLANK);
  address16(iwram, 0) = 0; /* untagged destination */
  execute_cycles = 1;
  video_count = 1;
  ret = update_gba(0);
  CHECK(ram_flushes == 0, "untagged scheduled DMA flushed RAM");
  CHECK((ret & 0x40000000u) == 0,
        "untagged scheduled DMA forced redispatch: 0x%08X", ret);
}

static void test_timer_reload_is_guest_visible(void)
{
  irq_type raised;

  /* Exact prescaled overflow: hardware exposes the programmed reload, not
   * the transient internal zero used to detect the event. */
  reset_fixture();
  timer[0].status = TIMER_PRESCALE;
  timer[0].prescale = 0;
  timer[0].reload = 0x120;
  timer[0].count = 1;
  timer[0].irq = 1;
  raised = IRQ_NONE;
  update_timers(&raised, 1);
  CHECK(timer[0].count == 0x120,
        "prescaled timer count=%d", timer[0].count);
  CHECK(read_ioreg(REG_TM0D) == 0xFEE0,
        "prescaled timer exposed %04X after reload", read_ioreg(REG_TM0D));
  CHECK((raised & IRQ_TIMER0) != 0,
        "prescaled timer failed to raise IRQ: 0x%X", raised);

  /* SimCity's failure mode: TM0 clocks cascaded TM1 exactly to overflow.
   * The old code wrote TM1D=0 while processing TM0, reloaded TM1 internally,
   * then returned without replacing that guest-visible zero. */
  reset_fixture();
  timer[0].status = TIMER_PRESCALE;
  timer[0].prescale = 0;
  timer[0].reload = 1;
  timer[0].count = 1;
  timer[1].status = TIMER_CASCADE;
  timer[1].prescale = 10; /* ignored in count-up mode */
  timer[1].reload = 0x120;
  timer[1].count = 1;
  timer[1].irq = 1;
  raised = IRQ_NONE;
  update_timers(&raised, 1);
  CHECK(timer[1].count == 0x120,
        "cascaded timer count=%d", timer[1].count);
  CHECK(read_ioreg(REG_TM1D) == 0xFEE0,
        "cascaded timer exposed %04X after reload", read_ioreg(REG_TM1D));
  CHECK((raised & IRQ_TIMER1) != 0,
        "cascaded timer failed to raise IRQ: 0x%X", raised);

  /* TM0 cannot cascade, while TM1 count-up must ignore clock-select bits. */
  reset_fixture();
  timer[0].reload = 0x120;
  trigger_timer(0, 0x87);
  CHECK(timer[0].status == TIMER_PRESCALE && timer[0].prescale == 10,
        "timer 0 accepted cascade: status=%u shift=%u",
        timer[0].status, timer[0].prescale);
  timer[1].reload = 0x120;
  trigger_timer(1, 0x87);
  CHECK(timer[1].status == TIMER_CASCADE && timer[1].prescale == 0,
        "timer 1 retained count-up clock select: status=%u shift=%u",
        timer[1].status, timer[1].prescale);
  CHECK(timer[1].count == 0x120,
        "timer 1 count-up period=%d", timer[1].count);
  CHECK(read_ioreg(REG_TM1CNT) == 0x87,
        "timer 1 lost raw control bits: %04X", read_ioreg(REG_TM1CNT));

  /* Import the phase from a state written by builds that scaled cascades. */
  timer[1].prescale = 10;
  timer[1].count = (0x120 << 10) - 5;
  {
    u8 *p = state_doc + 4;
    p += main_write_savestate(p);
    *p++ = 0;
    {
      u8 *hdr = state_doc;
      bson_write_u32(hdr, (u32)(p - state_doc));
    }
  }
  timer[1].status = TIMER_INACTIVE;
  timer[1].prescale = 6;
  timer[1].count = 0;
  CHECK(main_read_savestate(state_doc), "timer savestate restore failed");
  CHECK(timer[1].status == TIMER_CASCADE && timer[1].prescale == 0,
        "restored cascade not normalized: status=%u shift=%u",
        timer[1].status, timer[1].prescale);
  CHECK(timer[1].count == 0x11B,
        "restored cascade phase=%d", timer[1].count);
  write_ioreg(REG_TM1D, 0); /* legacy raw I/O image restored after main state */
  main_finalize_savestate_load();
  CHECK(read_ioreg(REG_TM1D) == 0xFEE5,
        "restored cascade exposed %04X", read_ioreg(REG_TM1D));

  /* A running canonical cascade may retain a count above a newly written
   * reload latch.  Loading it must not mistake that valid phase for legacy
   * prescaler scaling. */
  timer[1].prescale = 0;
  timer[1].reload = 4;
  timer[1].count = 7;
  {
    u8 *p = state_doc + 4;
    p += main_write_savestate(p);
    *p++ = 0;
    {
      u8 *hdr = state_doc;
      bson_write_u32(hdr, (u32)(p - state_doc));
    }
  }
  timer[1].status = TIMER_INACTIVE;
  timer[1].count = 0;
  CHECK(main_read_savestate(state_doc),
        "canonical timer savestate restore failed");
  CHECK(timer[1].count == 7,
        "canonical cascade phase changed to %d", timer[1].count);
  main_finalize_savestate_load();
  CHECK(read_ioreg(REG_TM1D) == 0xFFF9,
        "canonical cascade exposed %04X", read_ioreg(REG_TM1D));

  /* An IRQ-less timer can batch several periods in one event slice.  Its
   * register must describe the final remainder, not the pre-reload underflow. */
  reset_fixture();
  timer[0].status = TIMER_PRESCALE;
  timer[0].prescale = 0;
  timer[0].reload = 4;
  timer[0].count = 1;
  raised = IRQ_NONE;
  update_timers(&raised, 10);
  CHECK(timer[0].count == 3, "batched timer count=%d", timer[0].count);
  CHECK(read_ioreg(REG_TM0D) == 0xFFFD,
        "batched timer exposed %04X", read_ioreg(REG_TM0D));
}

static void test_scheduled_waitcnt_redispatch(void)
{
  u32 ret;

  reset_fixture();
  write_ioreg(REG_WAITCNT, 0);
  reload_timing_info();
  configure_waitcnt_dma(DMA_START_HBLANK);
  execute_cycles = 1;
  video_count = 1;
  write_ioreg(REG_DISPSTAT, 0); /* visible hrefresh -> HBlank */

  ret = update_gba(0);
  CHECK(read_ioreg(REG_WAITCNT) == 0x0014,
        "scheduled DMA WAITCNT=%04X", read_ioreg(REG_WAITCNT));
  CHECK(full_flushes == 1,
        "scheduled WAITCNT DMA full flushes=%u", full_flushes);
  CHECK(ram_flushes == 0,
        "scheduled WAITCNT DMA performed extra RAM flushes=%u", ram_flushes);
  CHECK((ret & 0x40000000u) != 0,
        "scheduled WAITCNT DMA return lacked changed-PC bit: 0x%08X", ret);
}

int main(void)
{
  test_waitcnt_invalidation();
  test_waitcnt_savestate_restore();
  test_dma_smc_alert();
  test_scheduled_dma_redispatch();
  test_scheduled_waitcnt_redispatch();
  test_timer_reload_is_guest_visible();

  if (failures) {
    printf("JIT boundary regressions failed: %d\n", failures);
    return 1;
  }
  puts("JIT timing/WAITCNT/DMA boundary regressions passed");
  return 0;
}
