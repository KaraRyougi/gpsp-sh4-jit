/* Host-side cartridge RTC regression tests.
 *
 * Include the production memory implementation directly so this test exercises
 * gpSP's real game database, calculator-only ROM loaders, GPIO register shadow,
 * and serial RTC state machine. Function/data section GC discards the unrelated
 * emulator machinery; the few live external globals are supplied below.
 */

#define time cgba_test_time
#define localtime cgba_test_localtime
#include "vendor/gpsp/gba_memory.c"
#undef localtime
#undef time

#include <stdio.h>

/* Globals normally owned by the CPU, scheduler, and serial translation units. */
u8 *memory_map_read[8 * 1024];
u32 reg[64];
u32 frame_counter;
u32 cpu_ticks;
u32 idle_loop_target_pc;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
int serial_mode;

#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
static const u8 *fake_filestream_data;
static u32 fake_filestream_size;
static u32 fake_filestream_offset;
#endif

/* The fragment DMA cases keep the production transfer switch live, so provide
 * its ordinary CPU/video/sound backing objects and side-effect stubs. */
u16 oam_ram[512];
u16 palette_ram[512];
u16 palette_ram_converted[512];
u16 io_registers[512];
u8 ewram[1024 * 256 * 2];
u8 iwram[1024 * 32 * 2];
u8 vram[1024 * 96];
s32 affine_reference_x[2];
s32 affine_reference_y[2];
timer_type timer[4];
u32 execute_cycles;
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

int filestream_close(RFILE *stream)
{
  (void)stream;
  return 0;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
  int64_t next;
  if (!stream || !fake_filestream_data)
    return -1;
  if (seek_position == SEEK_SET)
    next = offset;
  else if (seek_position == SEEK_CUR)
    next = (int64_t)fake_filestream_offset + offset;
  else if (seek_position == SEEK_END)
    next = (int64_t)fake_filestream_size + offset;
  else
    return -1;
  if (next < 0 || (u64)next > fake_filestream_size)
    return -1;
  fake_filestream_offset = (u32)next;
  return next;
#else
  (void)stream;
  (void)offset;
  (void)seek_position;
  return -1;
#endif
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
  u32 count;
  if (!stream || !data || len <= 0 || !fake_filestream_data ||
      fake_filestream_offset >= fake_filestream_size)
    return 0;
  count = fake_filestream_size - fake_filestream_offset;
  if ((u64)count > (u64)len)
    count = (u32)len;
  memcpy(data, fake_filestream_data + fake_filestream_offset, count);
  fake_filestream_offset += count;
  return count;
#else
  (void)stream;
  (void)data;
  (void)len;
  return 0;
#endif
}

void cgba_recompute_timer_cap_mask(void) { }
void render_gbc_sound(void) { }
void sound_timer_queue32(u32 channel, u32 value)
{ (void)channel; (void)value; }
void sound_reset_fifo(u32 channel) { (void)channel; }
cpu_alert_type write_siocnt(u16 value)
{ (void)value; return CPU_ALERT_NONE; }
cpu_alert_type write_rcnt(u16 value)
{ (void)value; return CPU_ALERT_NONE; }
cpu_alert_type check_interrupt(void) { return CPU_ALERT_NONE; }
cpu_alert_type flag_interrupt(irq_type irq_raised)
{ (void)irq_raised; return CPU_ALERT_NONE; }

static int failures;

#define CHECK(condition, ...) do {                                            \
  if (!(condition)) {                                                         \
    failures++;                                                               \
    printf("FAIL %s:%d: ", __FILE__, __LINE__);                              \
    printf(__VA_ARGS__);                                                      \
    putchar('\n');                                                            \
  }                                                                           \
} while (0)

/* Deterministic host wall clock used by the production RTC implementation. */
static time_t fake_wall_time;
static struct tm fake_local_tm;
static time_t fake_localtime_arg;
static unsigned fake_time_calls;
static unsigned fake_localtime_calls;

time_t cgba_test_time(time_t *out)
{
  fake_time_calls++;
  if (out)
    *out = fake_wall_time;
  return fake_wall_time;
}

struct tm *cgba_test_localtime(const time_t *timep)
{
  fake_localtime_calls++;
  fake_localtime_arg = *timep;
  return &fake_local_tm;
}

static u8 emerald_rom[0x8000];
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
static u8 maximum_mini_rom[CGBA_FXCG100_STATIC_ROM_MAX];
static u8 *fragment_alloc[8];
static u8 *fragment_rom[8];
static u8 fragment_reference[0x8000] __attribute__((aligned(32)));
static const u8 *fragment_table[8192];
static u8 signature_stream[CGBA_FXCG100_STATIC_ROM_MAX + 64u];
static u8 alias_page_tag[1000];
static u8 alias_unmapped_tag;
#endif

static void make_emerald_rom(void)
{
  memset(emerald_rom, 0xff, sizeof(emerald_rom));
  emerald_rom[3] = 0xea;
  emerald_rom[0xb2] = 0x96;
  memcpy(&emerald_rom[0xac], "BPEE", 4);
}

static void set_fake_datetime(time_t wall_time, int year, int month,
                              int month_day, int week_day, int hour,
                              int minute, int second)
{
  fake_wall_time = wall_time;
  memset(&fake_local_tm, 0, sizeof(fake_local_tm));
  fake_local_tm.tm_year = year - 1900;
  fake_local_tm.tm_mon = month - 1;
  fake_local_tm.tm_mday = month_day;
  fake_local_tm.tm_wday = week_day;
  fake_local_tm.tm_hour = hour;
  fake_local_tm.tm_min = minute;
  fake_local_tm.tm_sec = second;
  fake_time_calls = 0;
  fake_localtime_calls = 0;
  fake_localtime_arg = (time_t)-1;
}

static void rtc_protocol_clean(void)
{
  memset(gpio_regs, 0, sizeof(gpio_regs));
  rtc_state = RTC_DISABLED;
  rtc_write_mode = 0;
  rtc_command = 0;
  rtc_data = 0;
  rtc_data_bits = 0;
  rtc_status = 0x40;
  rtc_bit_count = 0;
  update_gpio_romregs();
}

static void rtc_transaction_begin(void)
{
  write_gpio(0xc8, 1); /* expose the GPIO registers in the ROM window */
  /* Match Emerald's SIIRTC library: SCK high/CS low, then CS high, then make
   * all three pins outputs. On the first transaction the pre-direction data
   * writes are ignored by the GPIO latch; the first command bit still raises
   * CS before its SCK sampling edge, exactly as it does in-game. */
  write_gpio(0xc4, GPIO_RTC_CLK);
  write_gpio(0xc4, GPIO_RTC_CLK | GPIO_RTC_CSS);
  write_gpio(0xc6, GPIO_RTC_CLK | GPIO_RTC_DAT | GPIO_RTC_CSS);
}

static void rtc_transaction_end(void)
{
  write_gpio(0xc4, 0);
}

static void rtc_send_bit(unsigned bit)
{
  u8 value = GPIO_RTC_CSS | (bit ? GPIO_RTC_DAT : 0);

  write_gpio(0xc4, value);
  write_gpio(0xc4, value | GPIO_RTC_CLK);
  write_gpio(0xc4, value);
}

static void rtc_send_command(u8 command)
{
  unsigned bit;
  u8 value;

  rtc_transaction_begin();
  for (bit = 0; bit < 7; bit++)
    rtc_send_bit((command >> (7 - bit)) & 1); /* commands are MSB-first */

  /* Leave SCK high after the final command bit. For read commands, the RTC
   * presents bit 0 on the following falling edge, after DAT becomes an input. */
  value = GPIO_RTC_CSS | ((command & 1) ? GPIO_RTC_DAT : 0);
  write_gpio(0xc4, value);
  write_gpio(0xc4, value | GPIO_RTC_CLK);
}

static void rtc_send_payload_lsb(u64 payload, unsigned bits)
{
  unsigned bit;

  for (bit = 0; bit < bits; bit++)
    rtc_send_bit((unsigned)(payload >> bit) & 1);
}

static u64 rtc_read_payload_lsb(unsigned bits)
{
  u64 payload = 0;
  unsigned bit;

  /* CLK and CS remain outputs; DAT becomes an input driven by the RTC. */
  write_gpio(0xc6, GPIO_RTC_CLK | GPIO_RTC_CSS);
  for (bit = 0; bit < bits; bit++) {
    /* The RTC advances DAT on the falling edge; the GBA samples it after the
     * next rising edge. Leave SCK high for the next iteration. */
    write_gpio(0xc4, GPIO_RTC_CSS);
    write_gpio(0xc4, GPIO_RTC_CSS | GPIO_RTC_CLK);
    if (gpio_regs[0] & GPIO_RTC_DAT)
      payload |= (u64)1 << bit;
  }
  return payload;
}

static u64 rtc_read_command(u8 command, unsigned bits)
{
  u64 payload;

  rtc_send_command(command);
  payload = rtc_read_payload_lsb(bits);
  rtc_transaction_end();
  return payload;
}

static void test_emerald_loader_autodetect(void)
{
  const u8 *pages[1];
  u32 result;

  make_emerald_rom();
  set_fake_datetime((time_t)1783658638, 2026, 7, 9, 4, 13, 45, 58);
  result = load_gamepak_from_memory(emerald_rom, sizeof(emerald_rom),
                                    FEAT_AUTODETECT, FEAT_DISABLE,
                                    SERIAL_MODE_DISABLED);
  CHECK(result == 0, "Emerald memory loader returned %lu",
        (unsigned long)result);
  CHECK(rtc_enabled, "BPEE memory loader did not autodetect RTC");
  CHECK(rtc_base_time == (s64)fake_wall_time,
        "memory loader RTC seed=%lld want %lld",
        (long long)rtc_base_time, (long long)fake_wall_time);
  CHECK(fake_time_calls == 1, "memory loader sampled time %u times, want 1",
        fake_time_calls);

  make_emerald_rom();
  pages[0] = emerald_rom;
  set_fake_datetime((time_t)1783745038, 2026, 7, 10, 5, 21, 43, 58);
  result = load_gamepak_from_pages(pages, sizeof(emerald_rom),
                                   FEAT_AUTODETECT, FEAT_DISABLE,
                                   SERIAL_MODE_DISABLED);
  CHECK(result == 0, "Emerald page loader returned %lu",
        (unsigned long)result);
  CHECK(rtc_enabled, "BPEE page loader did not autodetect RTC");
  CHECK(rtc_base_time == (s64)fake_wall_time,
        "page loader RTC seed=%lld want %lld",
        (long long)rtc_base_time, (long long)fake_wall_time);
  CHECK(fake_time_calls == 1, "page loader sampled time %u times, want 1",
        fake_time_calls);
}

#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
static void test_nondivisor_rom_alias_bounds(void)
{
  enum {
    PAGE_BYTES = 32 * 1024,
    MIRROR_PAGES = 1000,
    WAIT_WINDOW_PAGES = 1024,
    WAIT2_DIRECT_PAGES = 512
  };
  const u32 wait0 = 0x08000000u / PAGE_BYTES;
  const u32 wait1 = 0x0A000000u / PAGE_BYTES;
  const u32 wait2 = 0x0C000000u / PAGE_BYTES;
  u32 i;

  /* A non-divisor count leaves a partial repetition at each window end. The
   * old `mcount < limit` loop let `mcount + idx` cross the limit, so mapping
   * physical page 24 overwrote wait-state-1 page zero when the span was 1000.
   * Fill all three windows with a sentinel, publish distinct page identities,
   * then verify the complete alias shape rather than just the historical seam. */
  for (i = wait0; i < 0x0E000000u / PAGE_BYTES; i++)
    memory_map_read[i] = &alias_unmapped_tag;
  for (i = 0; i < MIRROR_PAGES; i++)
    map_rom_entry(read, i, &alias_page_tag[i], MIRROR_PAGES);

  for (i = 0; i < WAIT_WINDOW_PAGES; i++)
  {
    u8 *want = &alias_page_tag[i % MIRROR_PAGES];
    CHECK(memory_map_read[wait0 + i] == want,
          "non-divisor wait0 page %lu mapped to %p, want %p",
          (unsigned long)i, (void *)memory_map_read[wait0 + i], (void *)want);
    CHECK(memory_map_read[wait1 + i] == want,
          "non-divisor wait1 page %lu mapped to %p, want %p",
          (unsigned long)i, (void *)memory_map_read[wait1 + i], (void *)want);
  }
  for (i = 0; i < WAIT2_DIRECT_PAGES; i++)
  {
    u8 *want = &alias_page_tag[i];
    CHECK(memory_map_read[wait2 + i] == want,
          "non-divisor wait2 page %lu mapped to %p, want %p",
          (unsigned long)i, (void *)memory_map_read[wait2 + i], (void *)want);
  }
  for (i = WAIT2_DIRECT_PAGES; i < WAIT_WINDOW_PAGES; i++)
    CHECK(memory_map_read[wait2 + i] == &alias_unmapped_tag,
          "non-divisor mapping crossed into 0x0D at page %lu (%p)",
          (unsigned long)(i - WAIT2_DIRECT_PAGES),
          (void *)memory_map_read[wait2 + i]);

  /* Explicit seam labels make failures at all requested window ends obvious. */
  CHECK(memory_map_read[wait0 + 1023u] == &alias_page_tag[23],
        "wait0 end did not wrap to physical page 23");
  CHECK(memory_map_read[wait1] == &alias_page_tag[0] &&
        memory_map_read[wait1 + 1023u] == &alias_page_tag[23],
        "wait1 start/end were overwritten by wait0's partial repetition");
  CHECK(memory_map_read[wait2] == &alias_page_tag[0] &&
        memory_map_read[wait2 + 511u] == &alias_page_tag[511],
        "wait2 start/end were overwritten by wait1's partial repetition");
  CHECK(memory_map_read[wait2 + 512u] == &alias_unmapped_tag,
        "0x0D page zero was overwritten by the 0x0C mapping");

  map_null(read, 0x08000000u, 0x0E000000u);
}

static void test_calculator_mini_rom_scratch_isolation(void)
{
  enum { STATE_WORK_SIZE = 864 * 1024 };
  u8 saved_header[0xc0];
  u8 *mapped_page;
  u8 *scratch;
  u32 result;

  /* Exercise the transition from a live fragmented-page LRU entry to direct
   * mini-ROM mappings. The loader must retire the old queue metadata first. */
  gamepak_size = sizeof(emerald_rom);
  gamepak_file_blocks = 1;
  gamepak_blk_queue[0].phy_rom = 0;
  map_rom_entry(read, 0, gamepak_buffers[0], 1);

  make_emerald_rom();
  result = load_gamepak_from_memory(emerald_rom, sizeof(emerald_rom),
                                    FEAT_AUTODETECT, FEAT_DISABLE,
                                    SERIAL_MODE_DISABLED);
  CHECK(result == 0, "calculator mini-ROM loader returned %lu",
        (unsigned long)result);
  CHECK(gamepak_mini_rom == CGBA_STATIC_MINI_ROM_PTR,
        "mini ROM %p not parked in static mini buffer %p",
        (void *)gamepak_mini_rom, (void *)CGBA_STATIC_MINI_ROM_PTR);

  mapped_page = memory_map_read[0x08000000u / (32u * 1024u)];
  CHECK(mapped_page == gamepak_mini_rom,
        "ROM page zero %p does not map mini buffer %p",
        (void *)mapped_page, (void *)gamepak_mini_rom);
  CHECK(gamepak_blk_queue[0].phy_rom == -1,
        "direct mini-ROM load retained stale page-cache metadata");
  memcpy(saved_header, gamepak_mini_rom, sizeof(saved_header));

  scratch = cgba_gamepak_scratch_acquire(STATE_WORK_SIZE);
  CHECK(scratch == gamepak_buffers[0],
        "savestate scratch %p is not GamePak block zero %p",
        (void *)scratch, (void *)gamepak_buffers[0]);
  if (scratch)
    memset(scratch, 0x5a, STATE_WORK_SIZE);
  cgba_gamepak_scratch_release();

  CHECK(memcmp(saved_header, gamepak_mini_rom, sizeof(saved_header)) == 0,
        "savestate scratch overwrote embedded mini ROM");
  CHECK(memory_map_read[0x08000000u / (32u * 1024u)] == gamepak_mini_rom,
        "scratch release unmapped embedded ROM page zero");
  CHECK(cgba_gamepak_scratch_acquire(1024u * 1024u + 1u) == NULL,
        "oversized GamePak scratch request unexpectedly succeeded");

  /* The inclusive upper bound must fit exactly in the separate mini buffer. */
  memset(maximum_mini_rom, 0xff, sizeof(maximum_mini_rom));
  maximum_mini_rom[3] = 0xea;
  maximum_mini_rom[0xb2] = 0x96;
  memcpy(&maximum_mini_rom[0xac], "MAX0", 4);
  maximum_mini_rom[sizeof(maximum_mini_rom) - 1u] = 0x42;
  result = load_gamepak_from_memory(maximum_mini_rom,
                                    sizeof(maximum_mini_rom),
                                    FEAT_DISABLE, FEAT_DISABLE,
                                    SERIAL_MODE_DISABLED);
  CHECK(result == 0, "maximum-size mini-ROM loader returned %lu",
        (unsigned long)result);
  CHECK(gamepak_mini_rom == CGBA_STATIC_MINI_ROM_PTR,
        "maximum-size mini ROM did not use the static buffer");
  CHECK(gamepak_mini_rom[sizeof(maximum_mini_rom) - 1u] == 0x42,
        "maximum-size mini ROM lost its final byte");
  CHECK(memory_map_read[0x08038000u / (32u * 1024u)] ==
        gamepak_mini_rom + 7u * 32u * 1024u,
        "maximum-size mini ROM did not map its final page");
}

static void prepare_mixed_fragment_fallback(const u8 *fallback_file,
                                             u32 page_index)
{
  cgba_gamepak_cache_invalidate();
  memset(fragment_table, 0, sizeof(fragment_table));
  fragment_table[8] = fragment_rom[0];  /* logical 0x8000..0x8fff: direct */
  /* logical block 9 (0x9000..0x9fff) deliberately remains NULL */
  fake_filestream_data = fallback_file;
  fake_filestream_size = 0x10000u;
  fake_filestream_offset = 0;
  gamepak_file_large = (RFILE *)(uintptr_t)1u;
  cgba_gamepak_bind_fragment_table(fragment_table);

  CHECK(memory_map_read[page_index] == NULL,
        "mixed-seam fallback page was not cold");
  CHECK(cgba_gamepak_resolve_4k(0x08008FF0u) == fragment_rom[0],
        "mixed-seam direct block did not resolve directly");
  CHECK(memory_map_read[page_index] == NULL,
        "direct-side probe unexpectedly populated fallback cache");
}

static u32 mixed_fragment_ref16(u32 address)
{
  u32 off = (address & 0x01FFFFFFu) - 0x8000u;
  return (u32)fragment_reference[off] |
         ((u32)fragment_reference[off + 1u] << 8);
}

static u32 mixed_fragment_ref32(u32 address)
{
  u32 off = (address & 0x01FFFFFFu) - 0x8000u;
  return (u32)fragment_reference[off] |
         ((u32)fragment_reference[off + 1u] << 8) |
         ((u32)fragment_reference[off + 2u] << 16) |
         ((u32)fragment_reference[off + 3u] << 24);
}

static void test_mixed_fragment_dma16(const char *name,
                                      const u8 *fallback_file,
                                      u32 page_index, u32 src, u32 dst,
                                      int stride, u32 length)
{
  dma_transfer_type dmach;
  cpu_alert_type alert;
  u32 first_dest = dst & 0x7FFFu;
  u32 last_dest = (u32)((s32)dst + (s32)(length - 1u) * stride) & 0x7FFFu;
  u32 low_dest = MIN(first_dest, last_dest);
  u32 high_dest = MAX(first_dest, last_dest) + 2u;
  u32 last_src = (u32)((s32)src + (s32)(length - 1u) * stride);
  u32 i;

  prepare_mixed_fragment_fallback(fallback_file, page_index);
  memset(&dmach, 0, sizeof(dmach));
  memset(iwram, 0, 0x8000u);
  memset(iwram + 0x8000u, 0xD7, 0x8000u);
  dma_bus_val = 0;
  alert = dma_tf_loop16(src, dst, stride, stride, true, length, &dmach);

  CHECK(alert == CPU_ALERT_NONE, "%s alert=%u", name, alert);
  for (i = 0; i < length; i++)
  {
    u32 source = (u32)((s32)src + (s32)i * stride);
    u32 dest = (u32)((s32)dst + (s32)i * stride);
    CHECK(readaddress16(iwram + 0x8000u, dest & 0x7FFFu) ==
          mixed_fragment_ref16(source),
          "%s word %lu mismatch", name, (unsigned long)i);
  }
  CHECK(dmach.source_address == (u32)((s32)src + (s32)length * stride) &&
        dmach.dest_address == (u32)((s32)dst + (s32)length * stride),
        "%s writeback %08lX/%08lX", name,
        (unsigned long)dmach.source_address,
        (unsigned long)dmach.dest_address);
  CHECK(dma_bus_val == mixed_fragment_ref16(last_src),
        "%s bus value=%08lX", name, (unsigned long)dma_bus_val);
  CHECK(iwram[0x8000u + low_dest - 1u] == 0xD7 &&
        iwram[0x8000u + high_dest] == 0xD7,
        "%s overwrote a destination sentinel", name);
  CHECK(memory_map_read[page_index] != NULL,
        "%s never populated the NULL-side fallback", name);
}

static void test_mixed_fragment_dma32(const char *name,
                                      const u8 *fallback_file,
                                      u32 page_index, u32 src, u32 dst,
                                      int stride, u32 length)
{
  dma_transfer_type dmach;
  cpu_alert_type alert;
  u32 first_dest = dst & 0x7FFFu;
  u32 last_dest = (u32)((s32)dst + (s32)(length - 1u) * stride) & 0x7FFFu;
  u32 low_dest = MIN(first_dest, last_dest);
  u32 high_dest = MAX(first_dest, last_dest) + 4u;
  u32 last_src = (u32)((s32)src + (s32)(length - 1u) * stride);
  u32 i;

  prepare_mixed_fragment_fallback(fallback_file, page_index);
  memset(&dmach, 0, sizeof(dmach));
  memset(iwram, 0, 0x8000u);
  memset(iwram + 0x8000u, 0xD7, 0x8000u);
  dma_bus_val = 0;
  alert = dma_tf_loop32(src, dst, stride, stride, true, length, &dmach);

  CHECK(alert == CPU_ALERT_NONE, "%s alert=%u", name, alert);
  for (i = 0; i < length; i++)
  {
    u32 source = (u32)((s32)src + (s32)i * stride);
    u32 dest = (u32)((s32)dst + (s32)i * stride);
    CHECK(readaddress32(iwram + 0x8000u, dest & 0x7FFFu) ==
          mixed_fragment_ref32(source),
          "%s word %lu mismatch", name, (unsigned long)i);
  }
  CHECK(dmach.source_address == (u32)((s32)src + (s32)length * stride) &&
        dmach.dest_address == (u32)((s32)dst + (s32)length * stride),
        "%s writeback %08lX/%08lX", name,
        (unsigned long)dmach.source_address,
        (unsigned long)dmach.dest_address);
  CHECK(dma_bus_val == mixed_fragment_ref32(last_src),
        "%s bus value=%08lX", name, (unsigned long)dma_bus_val);
  CHECK(iwram[0x8000u + low_dest - 1u] == 0xD7 &&
        iwram[0x8000u + high_dest] == 0xD7,
        "%s overwrote a destination sentinel", name);
  CHECK(memory_map_read[page_index] != NULL,
        "%s never populated the NULL-side fallback", name);
}

static void test_calculator_fragment_table(void)
{
  const u8 *pages[2] = { emerald_rom, NULL };
  const u32 page_index = 0x08008000u >> 15;
  static const u32 aliases[] = { 0x08008000u, 0x0A008000u, 0x0C008000u };
  u8 *fallback_file = NULL;
  u32 result;
  u32 i;
  u32 alias;

  /* Keep each logical flash block in a distinct allocation with 32-byte
   * canaries on both sides. This makes stale 32-KiB-base arithmetic visible
   * even without ASan, while the sanitizer catches accesses past an entire
   * allocation. */
  for (i = 0; i < 8; i++)
  {
    uintptr_t aligned;

    fragment_alloc[i] = (u8 *)malloc(0x1000u + 96u);
    CHECK(fragment_alloc[i] != NULL,
          "could not allocate fragment block %lu", (unsigned long)i);
    if (!fragment_alloc[i])
      goto cleanup;
    aligned = ((uintptr_t)fragment_alloc[i] + 32u + 31u) & ~(uintptr_t)31u;
    fragment_rom[i] = (u8 *)aligned;
    memset(fragment_rom[i] - 32, 0xC3, 32);
    memset(fragment_rom[i] + 0x1000, 0x3C, 32);
  }

  make_emerald_rom();
  memset(fragment_table, 0, sizeof(fragment_table));
  for (i = 0; i < sizeof(fragment_reference); i++)
    fragment_reference[i] = (u8)((i * 37u + (i >> 8)) ^ 0xA5u);
  for (i = 0; i < 8; i++)
  {
    memcpy(fragment_rom[i], fragment_reference + i * 0x1000u, 0x1000u);
    fragment_table[8u + i] = fragment_rom[i];
  }

  cgba_gamepak_bind_fragment_table(fragment_table);
  result = load_gamepak_from_pages(pages, 0x10000u, FEAT_DISABLE,
                                   FEAT_DISABLE, SERIAL_MODE_DISABLED);
  CHECK(result == 0, "fragment-table loader returned %lu",
        (unsigned long)result);
  CHECK(memory_map_read[page_index] == NULL,
        "fragmented 32-KiB page unexpectedly gained a direct mapping");

  for (i = 0; i < 8; i++)
  {
    u32 address = 0x08008000u + i * 0x1000u;
    CHECK(cgba_gamepak_resolve_4k(address) == fragment_rom[i],
          "fragment block %lu resolved to %p instead of %p",
          (unsigned long)i, (void *)cgba_gamepak_resolve_4k(address),
          (void *)fragment_rom[i]);
    CHECK(cgba_memory_map_read_4k(address + 0x20u) == fragment_rom[i],
          "instruction fragment block %lu resolved incorrectly",
          (unsigned long)i);
  }

  /* Resolve every byte around a 4 KiB seam. Separate fragment allocations
   * plus ASan make accidental low-15-bit pointer arithmetic deterministic. */
  for (i = 0x0FF8u; i <= 0x1008u; i++)
  {
    u32 address = 0x08008000u + i;
    u8 *block = cgba_gamepak_resolve_4k(address);
    CHECK(block && block[address & 0x0FFFu] == fragment_reference[i],
          "fragment byte mismatch +%04lx", (unsigned long)i);
  }

  /* Scalar cartridge reads align before looking up the host block, then
   * rotate unaligned values. Exercise every byte around the seam and compare
   * all three wait-state aliases against a contiguous logical-ROM oracle. */
  for (alias = 0; alias < sizeof(aliases) / sizeof(aliases[0]); alias++)
    for (i = 0x0FF8u; i <= 0x1008u; i++)
    {
      u32 address = aliases[alias] + i;
      u32 a16 = i & ~1u;
      u32 a32 = i & ~3u;
      u32 want16 = (u32)fragment_reference[a16] |
                   ((u32)fragment_reference[a16 + 1u] << 8);
      u32 want32 = (u32)fragment_reference[a32] |
                   ((u32)fragment_reference[a32 + 1u] << 8) |
                   ((u32)fragment_reference[a32 + 2u] << 16) |
                   ((u32)fragment_reference[a32 + 3u] << 24);
      u32 rotate16 = (i & 1u) * 8u;
      u32 rotate32 = (i & 3u) * 8u;

      if (rotate16)
        want16 = (want16 >> rotate16) | (want16 << (32u - rotate16));
      if (rotate32)
        want32 = (want32 >> rotate32) | (want32 << (32u - rotate32));
      CHECK(read_memory8(address) == fragment_reference[i],
            "read8 alias %lu +%04lx mismatch",
            (unsigned long)alias, (unsigned long)i);
      CHECK(read_memory16(address) == want16,
            "read16 alias %lu +%04lx=%08lX want %08lX",
            (unsigned long)alias, (unsigned long)i,
            (unsigned long)read_memory16(address), (unsigned long)want16);
      CHECK(read_memory32(address) == want32,
            "read32 alias %lu +%04lx=%08lX want %08lX",
            (unsigned long)alias, (unsigned long)i,
            (unsigned long)read_memory32(address), (unsigned long)want32);
    }

  /* Exercise the production DMA loops in both directions. Their cached source
   * base must be refreshed at a 4-KiB seam, not the old 32-KiB boundary. */
  {
    dma_transfer_type dmach;
    cpu_alert_type alert;
    u32 src = 0x08008FF8u, dst = 0x03000100u, length = 12u;

    memset(&dmach, 0, sizeof(dmach));
    memset(iwram, 0, 0x8000u);
    memset(iwram + 0x8000u, 0xD7, 0x8000u);
    dma_bus_val = 0;
    alert = dma_tf_loop16(src, dst, 2, 2, true, length, &dmach);
    CHECK(alert == CPU_ALERT_NONE, "incrementing DMA16 alert=%u", alert);
    for (i = 0; i < length; i++)
    {
      u32 off = 0x0FF8u + i * 2u;
      u32 want = (u32)fragment_reference[off] |
                 ((u32)fragment_reference[off + 1u] << 8);
      CHECK(readaddress16(iwram + 0x8000u, (dst & 0x7FFFu) + i * 2u) == want,
            "incrementing DMA16 word %lu mismatch", (unsigned long)i);
    }
    CHECK(dmach.source_address == src + length * 2u &&
          dmach.dest_address == dst + length * 2u,
          "incrementing DMA16 writeback %08lX/%08lX",
          (unsigned long)dmach.source_address,
          (unsigned long)dmach.dest_address);
    CHECK(dma_bus_val == ((u32)fragment_reference[0x100Eu] |
          ((u32)fragment_reference[0x100Fu] << 8)),
          "incrementing DMA16 bus value=%08lX", (unsigned long)dma_bus_val);
    CHECK(iwram[0x8000u + (dst & 0x7FFFu) - 1u] == 0xD7 &&
          iwram[0x8000u + (dst & 0x7FFFu) + length * 2u] == 0xD7,
          "incrementing DMA16 overwrote a destination sentinel");
  }

  {
    dma_transfer_type dmach;
    cpu_alert_type alert;
    u32 src = 0x08008FF0u, dst = 0x03000200u, length = 8u;

    memset(&dmach, 0, sizeof(dmach));
    memset(iwram, 0, 0x8000u);
    memset(iwram + 0x8000u, 0xD7, 0x8000u);
    dma_bus_val = 0;
    alert = dma_tf_loop32(src, dst, 4, 4, true, length, &dmach);
    CHECK(alert == CPU_ALERT_NONE, "incrementing DMA32 alert=%u", alert);
    for (i = 0; i < length; i++)
    {
      u32 off = 0x0FF0u + i * 4u;
      u32 want = (u32)fragment_reference[off] |
                 ((u32)fragment_reference[off + 1u] << 8) |
                 ((u32)fragment_reference[off + 2u] << 16) |
                 ((u32)fragment_reference[off + 3u] << 24);
      CHECK(readaddress32(iwram + 0x8000u, (dst & 0x7FFFu) + i * 4u) == want,
            "incrementing DMA32 word %lu mismatch", (unsigned long)i);
    }
    CHECK(dmach.source_address == src + length * 4u &&
          dmach.dest_address == dst + length * 4u,
          "incrementing DMA32 writeback %08lX/%08lX",
          (unsigned long)dmach.source_address,
          (unsigned long)dmach.dest_address);
    CHECK(dma_bus_val == ((u32)fragment_reference[0x100Cu] |
          ((u32)fragment_reference[0x100Du] << 8) |
          ((u32)fragment_reference[0x100Eu] << 16) |
          ((u32)fragment_reference[0x100Fu] << 24)),
          "incrementing DMA32 bus value=%08lX", (unsigned long)dma_bus_val);
    CHECK(iwram[0x8000u + (dst & 0x7FFFu) - 1u] == 0xD7 &&
          iwram[0x8000u + (dst & 0x7FFFu) + length * 4u] == 0xD7,
          "incrementing DMA32 overwrote a destination sentinel");
  }

  {
    dma_transfer_type dmach;
    cpu_alert_type alert;
    u32 src = 0x08009008u, dst = 0x03000320u, length = 12u;

    memset(&dmach, 0, sizeof(dmach));
    memset(iwram, 0, 0x8000u);
    memset(iwram + 0x8000u, 0xD7, 0x8000u);
    dma_bus_val = 0;
    alert = dma_tf_loop16(src, dst, -2, -2, true, length, &dmach);
    CHECK(alert == CPU_ALERT_NONE, "decrementing DMA16 alert=%u", alert);
    for (i = 0; i < length; i++)
    {
      u32 off = 0x1008u - i * 2u;
      u32 doff = (dst & 0x7FFFu) - i * 2u;
      u32 want = (u32)fragment_reference[off] |
                 ((u32)fragment_reference[off + 1u] << 8);
      CHECK(readaddress16(iwram + 0x8000u, doff) == want,
            "decrementing DMA16 word %lu mismatch", (unsigned long)i);
    }
    CHECK(dmach.source_address == src - length * 2u &&
          dmach.dest_address == dst - length * 2u,
          "decrementing DMA16 writeback %08lX/%08lX",
          (unsigned long)dmach.source_address,
          (unsigned long)dmach.dest_address);
    CHECK(dma_bus_val == ((u32)fragment_reference[0x0FF2u] |
          ((u32)fragment_reference[0x0FF3u] << 8)),
          "decrementing DMA16 bus value=%08lX", (unsigned long)dma_bus_val);
    CHECK(iwram[0x8000u + (dst & 0x7FFFu) + 2u] == 0xD7 &&
          iwram[0x8000u + (dst & 0x7FFFu) - (length - 1u) * 2u - 1u] == 0xD7,
          "decrementing DMA16 overwrote a destination sentinel");
  }

  {
    dma_transfer_type dmach;
    cpu_alert_type alert;
    u32 src = 0x08009010u, dst = 0x03000440u, length = 8u;

    memset(&dmach, 0, sizeof(dmach));
    memset(iwram, 0, 0x8000u);
    memset(iwram + 0x8000u, 0xD7, 0x8000u);
    dma_bus_val = 0;
    alert = dma_tf_loop32(src, dst, -4, -4, true, length, &dmach);
    CHECK(alert == CPU_ALERT_NONE, "decrementing DMA32 alert=%u", alert);
    for (i = 0; i < length; i++)
    {
      u32 off = 0x1010u - i * 4u;
      u32 doff = (dst & 0x7FFFu) - i * 4u;
      u32 want = (u32)fragment_reference[off] |
                 ((u32)fragment_reference[off + 1u] << 8) |
                 ((u32)fragment_reference[off + 2u] << 16) |
                 ((u32)fragment_reference[off + 3u] << 24);
      CHECK(readaddress32(iwram + 0x8000u, doff) == want,
            "decrementing DMA32 word %lu mismatch", (unsigned long)i);
    }
    CHECK(dmach.source_address == src - length * 4u &&
          dmach.dest_address == dst - length * 4u,
          "decrementing DMA32 writeback %08lX/%08lX",
          (unsigned long)dmach.source_address,
          (unsigned long)dmach.dest_address);
    CHECK(dma_bus_val == ((u32)fragment_reference[0x0FF4u] |
          ((u32)fragment_reference[0x0FF5u] << 8) |
          ((u32)fragment_reference[0x0FF6u] << 16) |
          ((u32)fragment_reference[0x0FF7u] << 24)),
          "decrementing DMA32 bus value=%08lX", (unsigned long)dma_bus_val);
    CHECK(iwram[0x8000u + (dst & 0x7FFFu) + 4u] == 0xD7 &&
          iwram[0x8000u + (dst & 0x7FFFu) - (length - 1u) * 4u - 1u] == 0xD7,
          "decrementing DMA32 overwrote a destination sentinel");
  }

  CHECK(cgba_gamepak_resolve_4k(0x0A008FFCu) == fragment_rom[0] &&
        cgba_gamepak_resolve_4k(0x0C009002u) == fragment_rom[1],
        "fragment table did not preserve GamePak wait-state aliases");

  for (i = 0; i < 32u * gamepak_buffer_count; i++)
    CHECK(gamepak_blk_queue[i].phy_rom < 0,
          "direct fragment read populated fallback LRU slot %lu",
          (unsigned long)i);

  /* A rejected/NULL 4 KiB entry must retain the legacy aligned gather path.
   * Feed the production filestream shim a logical two-page ROM and verify that
   * the resolver publishes a cache-backed 32 KiB page only on first access. */
  fallback_file = (u8 *)malloc(0x10000u);
  CHECK(fallback_file != NULL, "could not allocate fallback ROM image");
  if (fallback_file)
  {
    bool found_page = false;
    u8 *fallback_block;

    memset(fallback_file, 0xFF, 0x10000u);
    memcpy(fallback_file, emerald_rom, sizeof(emerald_rom));
    memcpy(fallback_file + 0x8000u, fragment_reference,
           sizeof(fragment_reference));
    memset(fragment_table, 0, sizeof(fragment_table));
    fake_filestream_data = fallback_file;
    fake_filestream_size = 0x10000u;
    fake_filestream_offset = 0;
    gamepak_file_large = (RFILE *)(uintptr_t)1u;
    cgba_gamepak_bind_fragment_table(fragment_table);
    result = load_gamepak_from_pages(pages, 0x10000u, FEAT_DISABLE,
                                     FEAT_DISABLE, SERIAL_MODE_DISABLED);
    CHECK(result == 0, "fallback loader returned %lu", (unsigned long)result);
    CHECK(memory_map_read[page_index] == NULL,
          "fallback page was populated before first access");
    fallback_block = cgba_gamepak_resolve_4k(0x08009000u);
    CHECK(fallback_block != NULL,
          "NULL fragment did not enter aligned fallback cache");
    CHECK(fallback_block &&
          fallback_block[0] == fragment_reference[0x1000u] &&
          fallback_block[0x0FFFu] == fragment_reference[0x1FFFu],
          "fallback block content mismatch");
    CHECK(memory_map_read[page_index] != NULL,
          "fallback gather did not publish its 32 KiB page");
    for (i = 0; i < 32u * gamepak_buffer_count; i++)
      if (gamepak_blk_queue[i].phy_rom == 1)
        found_page = true;
    CHECK(found_page, "fallback gather did not enter the LRU");

    /* Mixed page: block 8 is a direct NOR fragment, block 9 is rejected and
     * must gather through the fallback cache. Re-cold the LRU before every
     * direction so the direct-to-NULL transition cannot be hidden by a prior
     * 32 KiB fallback mapping. */
    test_mixed_fragment_dma16("mixed incrementing DMA16", fallback_file,
                              page_index, 0x08008FF8u, 0x03000500u, 2, 8);
    test_mixed_fragment_dma32("mixed incrementing DMA32", fallback_file,
                              page_index, 0x08008FF0u, 0x03000600u, 4, 8);
    test_mixed_fragment_dma16("mixed decrementing DMA16", fallback_file,
                              page_index, 0x08009006u, 0x03000720u, -2, 8);
    test_mixed_fragment_dma32("mixed decrementing DMA32", fallback_file,
                              page_index, 0x0800900Cu, 0x03000840u, -4, 8);
  }

  for (i = 0; i < 8; i++)
  {
    u32 j;
    for (j = 0; j < 32; j++)
    {
      CHECK(fragment_rom[i][-32 + (s32)j] == 0xC3,
            "fragment %lu leading canary byte %lu changed",
            (unsigned long)i, (unsigned long)j);
      CHECK(fragment_rom[i][0x1000u + j] == 0x3C,
            "fragment %lu trailing canary byte %lu changed",
            (unsigned long)i, (unsigned long)j);
    }
  }

cleanup:
  cgba_gamepak_bind_fragment_table(NULL);
  cgba_gamepak_cache_invalidate();
  gamepak_file_large = NULL;
  fake_filestream_data = NULL;
  fake_filestream_size = 0;
  fake_filestream_offset = 0;
  free(fallback_file);
  for (i = 0; i < 8; i++)
  {
    free(fragment_alloc[i]);
    fragment_alloc[i] = NULL;
    fragment_rom[i] = NULL;
  }
}

static void test_calculator_streaming_signature_scan(void)
{
  const u32 boundary = CGBA_FXCG100_STATIC_ROM_MAX;
  u32 found;

  memset(signature_stream, 0xFF, sizeof(signature_stream));
  /* The first identifier straddles the loader's 256 KiB scratch boundary;
   * the second lives wholly beyond it. Both logical offsets stay word aligned. */
  memcpy(signature_stream + boundary - 4u, "FLASH1M_V", 9u);
  memcpy(signature_stream + boundary + 16u, "EEPROM_V", 8u);
  fake_filestream_data = signature_stream;
  fake_filestream_size = (u32)sizeof(signature_stream);
  fake_filestream_offset = 0;
  gamepak_file_large = (RFILE *)(uintptr_t)1u;
  gamepak_size = fake_filestream_size;
  cgba_gamepak_bind_fragment_table(fragment_table);

  found = rom_scan_signatures_in_memory();
  CHECK((found & ROM_SIG_FLASH1M) != 0,
        "streaming scan missed boundary-spanning FLASH1M signature");
  CHECK((found & ROM_SIG_EEPROM) != 0,
        "streaming scan missed post-boundary EEPROM signature");

  cgba_gamepak_bind_fragment_table(NULL);
  gamepak_file_large = NULL;
  fake_filestream_data = NULL;
  fake_filestream_size = 0;
  fake_filestream_offset = 0;
}
#endif

static void test_reset_and_status(void)
{
  u64 status;

  rtc_protocol_clean();
  rtc_send_command(RTC_COMMAND_RESET);
  CHECK(rtc_state == RTC_IDLE,
        "reset state=%lu want RTC_IDLE", (unsigned long)rtc_state);
  CHECK(rtc_data_bits == 0,
        "reset requested %lu payload bits, want none",
        (unsigned long)rtc_data_bits);
  CHECK(rtc_status == 0, "reset status=%02lX want 00",
        (unsigned long)rtc_status);
  rtc_transaction_end();

  rtc_send_command(RTC_COMMAND_WRITE_STATUS);
  rtc_send_payload_lsb(0x40, 8);
  CHECK(rtc_state == RTC_IDLE,
        "status-write state=%lu want RTC_IDLE", (unsigned long)rtc_state);
  CHECK(rtc_status == 0x40,
        "LSB-first status write produced %02lX want 40",
        (unsigned long)rtc_status);
  rtc_transaction_end();

  status = rtc_read_command(RTC_COMMAND_READ_STATUS, 8);
  CHECK(status == 0x40, "status readback=%02llX want 40",
        (unsigned long long)status);

  rtc_send_command(RTC_COMMAND_WRITE_STATUS);
  rtc_send_payload_lsb(0xff, 8);
  rtc_transaction_end();
  CHECK(rtc_status == 0x6a,
        "status writable-bit mask produced %02lX want 6A",
        (unsigned long)rtc_status);
  status = rtc_read_command(RTC_COMMAND_READ_STATUS, 8);
  CHECK(status == 0x6a, "masked status readback=%02llX want 6A",
        (unsigned long long)status);
}

static void test_datetime_reads(void)
{
  u64 full_time;
  u64 time_only;

  set_fake_datetime((time_t)1783658638, 2026, 7, 9, 4, 13, 45, 58);
  full_time = rtc_read_command(RTC_COMMAND_OUTPUT_TIME_FULL, 56);
  CHECK(full_time == 0x58459304090726ULL,
        "full datetime=%014llX want 58459304090726",
        (unsigned long long)full_time);
  CHECK(fake_time_calls == 1, "full datetime sampled time %u times, want 1",
        fake_time_calls);
  CHECK(fake_localtime_calls == 1,
        "full datetime converted time %u times, want 1",
        fake_localtime_calls);
  CHECK(fake_localtime_arg == fake_wall_time,
        "full datetime converted wall time %lld want %lld",
        (long long)fake_localtime_arg, (long long)fake_wall_time);

  fake_time_calls = 0;
  fake_localtime_calls = 0;
  fake_localtime_arg = (time_t)-1;
  time_only = rtc_read_command(RTC_COMMAND_OUTPUT_TIME, 24);
  CHECK(time_only == 0x584593ULL, "time=%06llX want 584593",
        (unsigned long long)time_only);
  CHECK(fake_time_calls == 1, "time-only sampled time %u times, want 1",
        fake_time_calls);
  CHECK(fake_localtime_calls == 1,
        "time-only converted time %u times, want 1",
        fake_localtime_calls);
  CHECK(fake_localtime_arg == fake_wall_time,
        "time-only converted wall time %lld want %lld",
        (long long)fake_localtime_arg, (long long)fake_wall_time);
}

static void test_forced_disable(void)
{
  const u8 *pages[1];
  u32 result;

  make_emerald_rom();
  set_fake_datetime((time_t)1783658638, 2026, 7, 9, 4, 21, 43, 58);
  result = load_gamepak_from_memory(emerald_rom, sizeof(emerald_rom),
                                    FEAT_DISABLE, FEAT_DISABLE,
                                    SERIAL_MODE_DISABLED);
  CHECK(result == 0, "force-disabled memory loader returned %lu",
        (unsigned long)result);
  CHECK(!rtc_enabled, "FEAT_DISABLE did not override BPEE memory autodetect");

  make_emerald_rom();
  pages[0] = emerald_rom;
  result = load_gamepak_from_pages(pages, sizeof(emerald_rom),
                                   FEAT_DISABLE, FEAT_DISABLE,
                                   SERIAL_MODE_DISABLED);
  CHECK(result == 0, "force-disabled page loader returned %lu",
        (unsigned long)result);
  CHECK(!rtc_enabled, "FEAT_DISABLE did not override BPEE page autodetect");
}

int main(void)
{
  init_gamepak_buffer();
#if defined(CGBA_FXCG100) || defined(CGBA_FXCG50)
  test_nondivisor_rom_alias_bounds();
  test_calculator_mini_rom_scratch_isolation();
  test_calculator_fragment_table();
  test_calculator_streaming_signature_scan();
#endif
  test_emerald_loader_autodetect();
  test_reset_and_status();
  test_datetime_reads();
  test_forced_disable();
  memory_term();

  if (failures) {
    printf("rtc_test: %d failure(s)\n", failures);
    return 1;
  }

  puts("RTC loader/GPIO tests passed");
  return 0;
}
