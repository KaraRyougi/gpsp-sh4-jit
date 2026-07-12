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
u32 frame_counter;
u32 cpu_ticks;
u32 idle_loop_target_pc;
u32 translation_gate_targets;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
int serial_mode;

int filestream_close(RFILE *stream)
{
  (void)stream;
  return 0;
}

int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
  (void)stream;
  (void)offset;
  (void)seek_position;
  return -1;
}

int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
  (void)stream;
  (void)data;
  (void)len;
  return 0;
}

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
        "mini ROM %p not parked at GamePak tail %p",
        (void *)gamepak_mini_rom, (void *)CGBA_STATIC_MINI_ROM_PTR);

  mapped_page = memory_map_read[0x08000000u / (32u * 1024u)];
  CHECK(mapped_page == gamepak_mini_rom,
        "ROM page zero %p does not map mini tail %p",
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

  /* The inclusive upper bound must fit exactly in the second block's tail. */
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
        "maximum-size mini ROM did not use the shared tail");
  CHECK(gamepak_mini_rom[sizeof(maximum_mini_rom) - 1u] == 0x42,
        "maximum-size mini ROM lost its final byte");
  CHECK(memory_map_read[0x08038000u / (32u * 1024u)] ==
        gamepak_mini_rom + 7u * 32u * 1024u,
        "maximum-size mini ROM did not map its final page");
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
  test_calculator_mini_rom_scratch_isolation();
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
