/* Exercise the real cartridge command decoder; dead stripping removes the
 * unrelated emulator code just as in rtc_test.c. */
#include "vendor/gpsp/gba_memory.c"
#include <assert.h>
#include <stdio.h>

static u8 original[sizeof(gamepak_backup)];

static void reset_backup(u32 type)
{
  memset(gamepak_backup, 0xFF, sizeof(gamepak_backup));
  memcpy(original, gamepak_backup, sizeof(original));
  backup_type = type;
  flash_mode = FLASH_BASE_MODE;
  flash_command_position = 0;
  flash_bank_num = 0;
  flash_bank_cnt = FLASH_SIZE_128KB;
  cgba_backup_dirty = 0;
}

static void command(u32 value)
{
  write_backup(0x5555, 0xAA);
  write_backup(0x2AAA, 0x55);
  write_backup(0x5555, value);
}

static void assert_clean(void)
{
  assert(cgba_backup_dirty == 0);
  assert(memcmp(original, gamepak_backup, sizeof(original)) == 0);
}

static void erase_sector(u32 address)
{
  command(0x80);
  write_backup(0x5555, 0xAA);
  write_backup(0x2AAA, 0x55);
  write_backup(address, 0x30);
}

int main(void)
{
  reset_backup(BACKUP_FLASH);
  command(0x90);
  assert(flash_mode == FLASH_ID_MODE);
  (void)read_backup(0);
  (void)read_backup(1);
  command(0xF0);
  assert(flash_mode == FLASH_BASE_MODE);
  assert_clean();

  command(0xB0);
  write_backup(0, 1);
  assert(flash_bank_num == 1);
  assert_clean();
  write_backup(0x1234, 0x42); /* Ignored outside program mode. */
  assert_clean();

  command(0xA0);
  write_backup(0x5555, 0xFF); /* Command address is data in program mode. */
  assert(flash_mode == FLASH_BASE_MODE);
  assert_clean();
  command(0xA0);
  write_backup(0x5555, 0x42);
  assert(cgba_backup_dirty == 1);
  assert(gamepak_backup[0x15555] == 0x42);
  assert(gamepak_backup[0x5555] == 0xFF);
  command(0x90); /* Commands must not clear an existing dirty flag. */
  command(0xF0);
  assert(cgba_backup_dirty == 1);

  reset_backup(BACKUP_FLASH);
  erase_sector(0x1000);
  assert_clean();
  gamepak_backup[0x1234] = 0;
  gamepak_backup[0x2234] = 0;
  erase_sector(0x1000);
  assert(cgba_backup_dirty == 1);
  assert(gamepak_backup[0x1234] == 0xFF);
  assert(gamepak_backup[0x2234] == 0);

  reset_backup(BACKUP_FLASH);
  command(0xB0);
  write_backup(0, 1);
  gamepak_backup[0x11234] = 0;
  gamepak_backup[0x1234] = 0;
  erase_sector(0x1000);
  assert(cgba_backup_dirty == 1);
  assert(gamepak_backup[0x11234] == 0xFF);
  assert(gamepak_backup[0x1234] == 0);

  reset_backup(BACKUP_FLASH);
  command(0x80);
  command(0x10);
  assert_clean();
  gamepak_backup[sizeof(gamepak_backup) - 1] = 0;
  command(0x80);
  command(0x10);
  assert(cgba_backup_dirty == 1);
  assert(memcmp(original, gamepak_backup, sizeof(original)) == 0);

  reset_backup(BACKUP_SRAM);
  write_backup(0, 0xFF);
  write_backup(0x5555, 0xFF);
  assert_clean();
  write_backup(0x5555, 0x42);
  assert(cgba_backup_dirty == 1 && gamepak_backup[0x5555] == 0x42);
  cgba_backup_dirty = 0;
  write_backup(0x1234, 0x42);
  assert(cgba_backup_dirty == 1 && gamepak_backup[0x1234] == 0x42);
  cgba_backup_dirty = 0;
  write_backup(0x1234, 0x142); /* Only the low byte is stored. */
  assert(cgba_backup_dirty == 0);
  write_backup(0x1234, 0x43);
  write_backup(0x1234, 0x43);
  assert(cgba_backup_dirty == 1);

  reset_backup(BACKUP_UNKN);
  write_backup(0, 0x42);
  assert(backup_type == BACKUP_SRAM && cgba_backup_dirty == 1);
  reset_backup(BACKUP_EEPROM);
  write_backup(0, 0x42); /* SRAM/Flash handler must ignore EEPROM. */
  assert_clean();
  puts("backup dirty tracking passed");
  return 0;
}
