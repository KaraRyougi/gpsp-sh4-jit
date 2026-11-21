/* Deterministic byte/seek/error oracle for the production storage transaction. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fake_open(const uint16_t *, int);
static int fake_size(int);
static int fake_create(const uint16_t *, int, int *);
static int fake_remove(const uint16_t *);
static int fake_read(int, void *, int, int);
static int fake_write(int, const void *, int);
static int fake_seek(int, int);
static int fake_close(int);
#define CGBA_FXCG100_STORAGE 1
#ifdef CGBA_FXCG50
#define BFile_Open fake_open
#define BFile_Size fake_size
#define BFile_Create fake_create
#define BFile_Remove fake_remove
#define BFile_Read fake_read
#define BFile_Write fake_write
#define BFile_Seek fake_seek
#define BFile_Close fake_close
#else
#define CGBA_BFILE_OPEN fake_open
#define CGBA_BFILE_SIZE fake_size
#define CGBA_BFILE_CREATE fake_create
#define CGBA_BFILE_REMOVE fake_remove
#define CGBA_BFILE_READ fake_read
#define CGBA_BFILE_WRITE fake_write
#define CGBA_BFILE_SEEK fake_seek
#define CGBA_BFILE_CLOSE fake_close
#endif
#include "ports/fxcg100/fxcg100_config.c"

#define CAPACITY (416 * 1024)
static uint8_t disk[CAPACITY], source[CAPACITY];
static const uint16_t path[] = {'S', 0};
static const uint16_t subdir_path[] = {
  '\\', '\\', 'f', 'l', 's', '0', '\\', 'S', 'A', 'V', 'E', 'S', '\\',
  'Z', 'E', 'L', 'D', 'A', '.', 'S', 'A', 'V', 0
};
static const uint16_t saves_dir[] = {
  '\\', '\\', 'f', 'l', 's', '0', '\\', 'S', 'A', 'V', 'E', 'S', 0
};
static int exists, dir_exists = 1, length, position, opened, mode, in_os;
static int switches, notes, reads, writes, bytes_written, creates, dir_creates, removes;
static int last_create_size, create_zero_bytes;
static int offsets[128], lengths[128];
static int fail_open, missing_dir_open_err, fail_size, fail_read, fail_seek, fail_write, fail_close;
static int fail_create, fail_remove, reject_zero_create, forbid_write_grow;
static int short_read, short_write;

static int fc_eq(const uint16_t *a, const uint16_t *b)
{
  while (*a && *b && *a == *b) { a++; b++; }
  return *a == *b;
}

int fxcg100_input_maps_valid(const uint16_t *a, const uint16_t *b)
{ (void)a; (void)b; return 1; }
int gint_world_switch(gint_call_t call)
{
  assert(!in_os);
  in_os = 1;
  switches++;
  int result = ((int (*)(void *))call.function)((void *)call.args[0]);
  in_os = 0;
  return result;
}
void fxcg100_lcd_note_os_activity(void) { assert(!in_os); notes++; }
static int fake_open(const uint16_t *p, int m)
{
  assert(in_os && p && !opened);
  if (fail_open) return fail_open;
  if (fc_eq(p, subdir_path) && !dir_exists)
    return missing_dir_open_err ? missing_dir_open_err : -1;
  if (!exists) return -1;
  opened = 1; mode = m; position = 0; return 7;
}
static int fake_size(int fd)
{ assert(in_os && opened && fd == 7); return fail_size ? -5 : length; }
static int fake_create(const uint16_t *p, int type, int *size)
{
  assert(in_os && !opened && p);
  if (type == CGBA_BFILE_FOLDER) {
    assert(size == NULL && fc_eq(p, saves_dir));
    dir_creates++;
    if (fail_create) return -4;
    if (dir_exists) return CGBA_BFILE_ALREADY_EXISTS;
    dir_exists = 1;
    return 0;
  }
  assert(type == CGBA_BFILE_FILE && size != NULL);
  creates++;
  last_create_size = *size;
  if (fail_create) return -4;
  if (fc_eq(p, subdir_path) && !dir_exists) return -1;
  if (reject_zero_create && *size == 0) return -3;
  assert(!exists && *size >= 0 && *size <= CAPACITY);
  exists = 1; length = *size;
  if (length > 0) {
    memset(disk, 0, length);
    create_zero_bytes += length;
  }
  return 0;
}
static int fake_remove(const uint16_t *p)
{
  assert(in_os && !opened && p); removes++;
  if (fail_remove) return -8;
  exists = 0; return 0;
}
static int fake_read(int fd, void *dst, int size, int offset)
{
  assert(in_os && opened && fd == 7 && (mode & 1));
  assert(writes == 0); /* never issue BFile_Read after BFile_Write on the same fd */
  assert(offset >= 0 && offset + size <= length);
  reads++;
  if (reads == fail_read) return -5;
  if (short_read) return size - 1;
  memcpy(dst, disk + offset, size); position = offset + size; return size;
}
static int fake_write(int fd, const void *src, int size)
{
  assert(in_os && opened && fd == 7 && (mode & 2));
  assert(position + size <= CAPACITY && writes < 128);
  offsets[writes] = position; lengths[writes] = size; writes++;
  if (fail_write) return -5;
  if (forbid_write_grow && position + size > length) return -5;
  int actual = short_write ? size / 2 : size;
  memcpy(disk + position, src, actual); position += actual;
  if (position > length) length = position;
  bytes_written += actual; return actual;
}
static int fake_seek(int fd, int offset)
{
  assert(in_os && opened && fd == 7 && offset <= length);
  if (fail_seek) return -5;
  position = offset;
  /* Fugue returns remaining allocation space, NOT the new position. */
  return length - offset;
}
static int fake_close(int fd)
{
  assert(in_os && opened && fd == 7); opened = 0;
  return fail_close ? -5 : 0;
}
static void counters(void)
{
  switches = notes = reads = writes = bytes_written = creates = dir_creates = removes = 0;
  last_create_size = -1; create_zero_bytes = 0;
  fail_open = missing_dir_open_err = fail_size = fail_read = fail_seek = fail_write = fail_close = 0;
  fail_create = fail_remove = reject_zero_create = forbid_write_grow = 0;
  short_read = short_write = 0;
}
static void reset(int size)
{
  assert(!opened && !in_os);
  counters(); exists = 1; dir_exists = 1; length = size;
  memset(disk, 0xAA, sizeof(disk)); memcpy(source, disk, sizeof(source));
  storage_mutation_generation = 0;
}
static int save(unsigned size)
{
  int result = fxcg100_storage_write_blob(path, source, size);
  assert(switches == 1 && notes == 1 && !opened);
  return result;
}
int main(void)
{
  reset(128 * 1024);
  assert(save(length));
  assert(writes == 0 && storage_mutation_generation == 0);
  reset(128 * 1024); source[70000] ^= 1;
  assert(save(length));
  assert(bytes_written == 4096 && writes == 1 && offsets[0] == 17 * 4096);
  assert(!memcmp(disk, source, length) && storage_mutation_generation == 1);
  counters(); assert(save(length));
  assert(writes == 0 && storage_mutation_generation == 1);

  reset(128 * 1024); source[4096] ^= 1; source[8192] ^= 1; source[20000] ^= 1;
  assert(save(length));
  assert(writes == 2 && lengths[0] == 8192 && lengths[1] == 4096);
  assert(!memcmp(disk, source, length));
  reset(CAPACITY); memset(source, 0xBB, sizeof(source));
  assert(save(length)); assert(writes == 1 && bytes_written == CAPACITY);
  reset(512); source[511] ^= 1;
  assert(save(length)); assert(bytes_written == 512 && !memcmp(disk, source, length));
  reset(4098); source[4097] ^= 1;
  assert(save(length)); assert(bytes_written == 2 && offsets[0] == 4096);

  /* Fast-path file creation: creates with size 0 (no zero-fill) and lets
   * BFile_Write grow the file in one pass. */
  reset(8192); exists = 0;
  assert(save(8192));
  assert(creates == 1 && last_create_size == 0 && create_zero_bytes == 0 &&
         removes == 0 && length == 8192 && bytes_written == 8192 &&
         !memcmp(disk, source, 8192));
  /* Fallback 1: OS rejects size 0 -> retries with size 1 (no full zero-fill). */
  reset(8192); exists = 0; reject_zero_create = 1;
  assert(save(8192));
  assert(creates == 2 && last_create_size == 1 && create_zero_bytes == 1 &&
         removes == 0 && length == 8192 && bytes_written == 8192 &&
         !memcmp(disk, source, 8192));
  /* Fallback 2: OS refuses to grow past create size -> cleans up and falls back
   * to full pre-allocation. */
  reset(8192); exists = 0; forbid_write_grow = 1;
  assert(save(8192));
  assert(creates == 3 && last_create_size == 8192 && removes == 2 &&
         length == 8192 && !memcmp(disk, source, 8192));
  reset(4096); assert(save(8192));
  assert(creates == 1 && last_create_size == 0 && removes == 1 &&
         length == 8192 && !memcmp(disk, source, 8192));
  reset(8192); assert(save(4096));
  assert(length == 4096 && last_create_size == 0 && bytes_written == 4096);

  reset(8192); fail_open = -8;
  assert(!save(length)); assert(!creates && !removes && !storage_mutation_generation);
  /* Subdirectory auto-creation: missing \\fls0\SAVES created in the same world
   * switch whether open reports -1 (EntryNotFound) or -3 (IllegalPath). */
  reset(8192); exists = 0; dir_exists = 0;
  assert(fxcg100_storage_write_blob(subdir_path, source, 8192));
  assert(switches == 1 && notes == 1 && !opened && dir_exists);
  assert(dir_creates == 1 && creates == 2 && bytes_written == 8192);
  reset(8192); exists = 0; dir_exists = 0; missing_dir_open_err = -3;
  assert(fxcg100_storage_write_blob(subdir_path, source, 8192));
  assert(switches == 1 && notes == 1 && !opened && dir_exists);
  assert(dir_creates == 1 && creates == 2 && bytes_written == 8192);
  /* Root paths still treat -3 as an error and never create directories. */
  reset(8192); exists = 0; fail_open = -3;
  assert(!save(length)); assert(!creates && !dir_creates && !storage_mutation_generation);
  reset(8192); fail_size = 1;
  assert(!save(length)); assert(!writes && !removes);
  reset(8192); source[0] ^= 1; fail_read = 1;
  assert(!save(length)); assert(!writes && !storage_mutation_generation);
  reset(8192); source[0] ^= 1; short_read = 1;
  assert(!save(length)); assert(!writes && !storage_mutation_generation);
  reset(8192); source[0] ^= 1; fail_seek = 1;
  assert(!save(length)); assert(!writes && !storage_mutation_generation);
  reset(8192); source[0] ^= 1; fail_write = 1;
  assert(!save(length)); assert(storage_mutation_generation == 1);
  reset(8192); source[0] ^= 1; short_write = 1;
  assert(!save(length)); assert(storage_mutation_generation == 1);
  counters(); assert(save(length)); assert(!memcmp(disk, source, length));
  reset(16384); source[0] ^= 1; source[12288] ^= 1; fail_read = 3;
  assert(!save(length)); assert(writes == 0 && storage_mutation_generation == 0);
  counters(); assert(save(length)); assert(writes == 2 && !memcmp(disk, source, length));
  reset(8192); source[0] ^= 1; fail_close = 1;
  assert(!save(length)); assert(storage_mutation_generation == 1);
  reset(8192); fail_remove = 1;
  assert(!save(4096)); assert(exists && !creates && !writes);
  reset(8192); exists = 0; fail_create = 1;
  assert(!save(length)); assert(!writes);

  reset(8192);
  assert(fxcg100_storage_blob_size(path) == length && switches == 1);
  counters(); memset(source, 0, length);
  assert(fxcg100_storage_read_blob(path, source, length));
  assert(switches == 1 && !memcmp(source, disk, length));
  reset(8192);
  assert(!fxcg100_storage_write_blob(path, source, UINT_MAX));
  assert(!fxcg100_storage_write_blob(NULL, source, 8192));
  assert(!fxcg100_storage_write_blob(path, NULL, 8192));
  assert(!switches);
  puts("storage: one world switch; sparse/coalesced writes; no-op and error/retry checks passed");
  return 0;
}
