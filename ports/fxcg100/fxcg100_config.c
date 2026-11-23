#include "fxcg100_platform.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t storage_mutation_generation;

uint32_t fxcg100_storage_mutation_generation(void)
{
  return storage_mutation_generation;
}

void fxcg100_storage_note_mutation(void)
{
  storage_mutation_generation++;
}

#ifdef CGBA_GPSP_DISABLE_STORAGE

int fxcg100_config_load(fxcg100_menu_state *state)
{
  (void)state;
  return 0;
}

int fxcg100_config_save(const fxcg100_menu_state *state)
{
  (void)state;
  return 0;
}

int fxcg100_storage_blob_size(const uint16_t *path)
{ (void)path; return -1; }
int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{ (void)path; (void)dst; (void)size; return 0; }
int fxcg100_storage_write_blob(const uint16_t *path, const void *src, unsigned size)
{ (void)path; (void)src; (void)size; return 0; }

#else

#ifndef CGBA_FXCG100_STORAGE
#define CGBA_FXCG100_STORAGE 0
#endif

#if CGBA_FXCG100_STORAGE
#include <gint/gint.h>
#endif

#define CGBA_CONFIG_MAGIC 0x43474241u
#define CGBA_CONFIG_VERSION 4u
#define CGBA_CONFIG_VERSION_V3 3u
#define CGBA_CONFIG_VERSION_LEGACY 2u
#define CGBA_CONFIG_LEGACY_HOTKEY_COUNT 5u

#define CGBA_BFILE_FILE 1
#define CGBA_BFILE_FOLDER 5
#define CGBA_BFILE_ALREADY_EXISTS (-13)
#define CGBA_BFILE_READ_ONLY 0x01
#define CGBA_BFILE_WRITE_ONLY 0x02
#define CGBA_BFILE_READ_WRITE 0x03
#define CGBA_STORAGE_PATH_MAX 160u

#if defined(CGBA_CLASSPAD)
/* ClassPad II: the OS file API behind Fugue-style adapters. */
#include "classpad_bfile.h"
#define CGBA_BFILE_OPEN   cp_bfile_open
#define CGBA_BFILE_SIZE   cp_bfile_size
#define CGBA_BFILE_CREATE cp_bfile_create
#define CGBA_BFILE_REMOVE cp_bfile_remove
#define CGBA_BFILE_READ   cp_bfile_read
#define CGBA_BFILE_WRITE  cp_bfile_write
#define CGBA_BFILE_CLOSE  cp_bfile_close
#define CGBA_BFILE_SEEK   cp_bfile_seek
#elif CGBA_FXCG100_STORAGE
/* Both fx-CG50 and fx-CG100 use gint's portable BFile entry points inside the
 * OS-world transaction below (dispatched dynamically by OS version in gint). */
#include <gint/bfile.h>
#ifndef CGBA_BFILE_OPEN
#define CGBA_BFILE_OPEN   BFile_Open
#define CGBA_BFILE_SIZE   BFile_Size
#define CGBA_BFILE_CREATE BFile_Create
#define CGBA_BFILE_REMOVE BFile_Remove
#define CGBA_BFILE_READ   BFile_Read
#define CGBA_BFILE_WRITE  BFile_Write
#define CGBA_BFILE_CLOSE  BFile_Close
#define CGBA_BFILE_SEEK   BFile_Seek
#endif
#endif

typedef struct fxcg100_config_file {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint16_t keymap[FXCG100_GBA_KEY_COUNT];
  uint16_t hotkey_map[CGBA_CONFIG_LEGACY_HOTKEY_COUNT];
  uint32_t screen_scale;
  uint32_t screen_filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t frameskip_variation;
  uint32_t savestate_slot;
  uint32_t quick_save_slot;
  /* Version-2/3 compatibility slot. Version 2 stored the removed automatic
   * backup policy here; version 3 zeroed it; version 4 stores the 6th hotkey
   * (FXCG100_HOTKEY_SAVE_EXIT) in bits 0..15 while keeping sizeof unchanged. */
  uint32_t hotkey_extra;
  uint32_t show_fps;
  uint32_t frame_limit;
  /* ClassPad II display options, carved out of the reserved words so the file
   * size and version stay unchanged. Both are zero in files written by the
   * fx-CG builds; the touch-key flag is stored inverted so that zero keeps
   * the keys on. */
  uint32_t rotation;
  uint32_t touch_keys_hidden;
  uint32_t lcd_cpu;
  /* ClassPad II interface: colour scheme, and animations (stored inverted so
   * that zero keeps them on). */
  uint32_t theme;
  uint32_t animations_off;
  /* ClassPad II: wait for the panel's refresh before each frame. */
  uint32_t lcd_sync;
  /* The last free word, shared: bits 0-3 the refresh position, bits 4-5 the
   * GBA BIOS mode (FXCG100_BIOS_*, 0 = Auto), bits 8-12 the backlight level
   * (0 leaves the system's), bits 16-19 the backlight PWM rate (0 leaves the
   * system's), bits 20-23 the panel refresh rate preset (0 = Auto 59.73 Hz w/
   * VSync, 1 = System, 2 = 59.73 Hz (GBA), 3..9 = platform high-refresh
   * presets), bits 24-25 the library sort mode (FXCG100_GAME_SORT_*, 0 = Name),
   * bit 26 light mode (stored inverted from dark_mode so that zero keeps dark
   * mode on), bit 27 fast-forward mode (0 = Hold, 1 = Toggle), bits 28-30
   * fast-forward speed (0..4). */
  uint32_t lcd_extra;
} fxcg100_config_file;

_Static_assert(FXCG100_HOTKEY_COUNT == CGBA_CONFIG_LEGACY_HOTKEY_COUNT + 1u,
               "update fxcg100_config_file for extra hotkeys");

static const uint16_t config_path[] = {
  '\\', '\\', 'f', 'l', 's', '0', '\\',
  'C', 'G', 'B', 'A', '.', 'C', 'F', 'G', 0
};

/* These helpers run only inside storage_transaction(), already in the OS
 * world. Never wrap individual BFile calls in another world switch. */
static int os_bfile_open(const uint16_t *path, int mode)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(const uint16_t *, int))CGBA_BFILE_OPEN)(path, mode);
#else
  (void)path; (void)mode; return -1;
#endif
}
static int os_bfile_size(int fd)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(int))CGBA_BFILE_SIZE)(fd);
#else
  (void)fd; return -1;
#endif
}
static int os_bfile_create(const uint16_t *path, int type, int *size)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(const uint16_t *, int, int *))CGBA_BFILE_CREATE)(path, type, size);
#else
  (void)path; (void)type; (void)size; return -1;
#endif
}
static int os_bfile_remove(const uint16_t *path)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(const uint16_t *))CGBA_BFILE_REMOVE)(path);
#else
  (void)path; return -1;
#endif
}
static int os_bfile_read(int fd, void *dst, int size, int offset)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(int, void *, int, int))CGBA_BFILE_READ)(fd, dst, size, offset);
#else
  (void)fd; (void)dst; (void)size; (void)offset; return -1;
#endif
}
static int bfile_read_exact_ok(int result, int size)
{
#ifdef CGBA_CLASSPAD
  return result == size;
#else
  /* Fugue returns bytes read; CASIOWIN returns bytes remaining after read. */
  return result == size || result == 0;
#endif
}
static int os_bfile_write(int fd, const void *src, int size)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(int, const void *, int))CGBA_BFILE_WRITE)(fd, src, size);
#else
  (void)fd; (void)src; (void)size; return -1;
#endif
}
static int os_bfile_seek(int fd, int offset)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(int, int))CGBA_BFILE_SEEK)(fd, offset);
#else
  (void)fd; (void)offset; return -1;
#endif
}
static int os_bfile_close(int fd)
{
#if CGBA_FXCG100_STORAGE
  return ((int (*)(int))CGBA_BFILE_CLOSE)(fd);
#else
  (void)fd; return -1;
#endif
}

static void config_extract_hotkeys(uint16_t out[FXCG100_HOTKEY_COUNT],
                                   const fxcg100_config_file *config)
{
  memcpy(out, config->hotkey_map, sizeof(config->hotkey_map));
  out[FXCG100_HOTKEY_SAVE_EXIT] = config->version >= CGBA_CONFIG_VERSION
    ? (uint16_t)(config->hotkey_extra & 0xffffu) : 0u;
}

static int config_options_valid(const fxcg100_config_file *config)
{
  return config->screen_scale < 5 &&
    config->screen_filter < 3 &&
    config->frameskip_type < 3 &&
    config->frameskip_value < 100 &&
    config->frameskip_variation < 2 &&
    config->savestate_slot < 10 &&
    config->quick_save_slot < 10 &&
    config->show_fps < 2 &&
    config->rotation < 2 &&
    config->touch_keys_hidden < 2 &&
    config->lcd_cpu < 2 &&
    config->lcd_sync < 2 &&
    (config->lcd_extra & ~0x7fff1f3fu) == 0 &&
    ((config->lcd_extra >> 4) & 3u) < FXCG100_BIOS_MODE_COUNT &&
    ((config->lcd_extra >> 8) & 31u) <= 16u &&
    ((config->lcd_extra >> 16) & 15u) <= 9u &&
    ((config->lcd_extra >> 20) & 15u) <= 9u &&
    ((config->lcd_extra >> 24) & 3u) < FXCG100_GAME_SORT_COUNT &&
    ((config->lcd_extra >> 28) & 7u) < FXCG100_FF_SPEED_COUNT &&
    config->theme < 16 &&
    config->animations_off < 2 &&
    (config->version < CGBA_CONFIG_VERSION ||
      (config->hotkey_extra >> 16) == 0u) &&
    (config->version == CGBA_CONFIG_VERSION_LEGACY ||
      config->frame_limit < 2);
}

static int config_valid(const fxcg100_config_file *config)
{
  uint16_t full_hotkeys[FXCG100_HOTKEY_COUNT];

  if (!config)
    return 0;
  if (config->magic != CGBA_CONFIG_MAGIC ||
      (config->version != CGBA_CONFIG_VERSION &&
       config->version != CGBA_CONFIG_VERSION_V3 &&
       config->version != CGBA_CONFIG_VERSION_LEGACY) ||
      config->size != sizeof(*config))
    return 0;
  config_extract_hotkeys(full_hotkeys, config);
  return fxcg100_input_maps_valid(config->keymap, full_hotkeys) &&
    config_options_valid(config);
}

static void config_from_state(fxcg100_config_file *config,
                              const fxcg100_menu_state *state)
{
  memset(config, 0, sizeof(*config));
  config->magic = CGBA_CONFIG_MAGIC;
  config->version = CGBA_CONFIG_VERSION;
  config->size = sizeof(*config);
  memcpy(config->keymap, state->keymap, sizeof(config->keymap));
  memcpy(config->hotkey_map, state->hotkey_map, sizeof(config->hotkey_map));
  config->hotkey_extra = state->hotkey_map[FXCG100_HOTKEY_SAVE_EXIT];
  config->screen_scale = state->screen_scale;
  config->screen_filter = state->screen_filter;
  config->frameskip_type = state->frameskip_type;
  config->frameskip_value = state->frameskip_value;
  config->frameskip_variation = state->frameskip_variation;
  config->savestate_slot = state->savestate_slot;
  config->quick_save_slot = state->quick_save_slot;
  config->show_fps = state->show_fps;
  config->frame_limit = state->frame_limit;
  config->rotation = state->rotation;
  config->touch_keys_hidden = state->touch_keys ? 0 : 1;
  config->lcd_cpu = state->lcd_cpu;
  config->lcd_sync = state->lcd_sync;
  config->lcd_extra = (state->lcd_sync_offset & 15u) |
    ((state->bios_mode & 3u) << 4) |
    ((state->backlight & 31u) << 8) | ((state->backlight_pwm & 15u) << 16) |
    ((state->lcd_refresh & 15u) << 20) |
    ((state->game_sort & 3u) << 24) | ((state->dark_mode ? 0u : 1u) << 26) |
    ((state->fast_forward_mode & 1u) << 27) |
    ((state->fast_forward_speed & 7u) << 28);
  config->theme = state->theme;
  config->animations_off = state->animations ? 0 : 1;
}

static void state_from_config(fxcg100_menu_state *state,
                              const fxcg100_config_file *config)
{
  memcpy(state->keymap, config->keymap, sizeof(state->keymap));
  config_extract_hotkeys(state->hotkey_map, config);
  state->screen_scale = config->screen_scale;
  state->screen_filter = config->screen_filter;
  state->frameskip_type = config->frameskip_type;
  state->frameskip_value = config->frameskip_value;
  state->frameskip_variation = config->frameskip_variation;
  state->savestate_slot = config->savestate_slot;
  state->quick_save_slot = config->quick_save_slot;
  state->show_fps = config->show_fps;
  state->rotation = config->rotation;
  state->touch_keys = config->touch_keys_hidden ? 0 : 1;
  state->lcd_cpu = config->lcd_cpu;
  state->lcd_sync = config->lcd_sync;
  state->lcd_sync_offset = config->lcd_extra & 15u;
  state->bios_mode = (config->lcd_extra >> 4) & 3u;
  state->backlight = (config->lcd_extra >> 8) & 31u;
  state->backlight_pwm = (config->lcd_extra >> 16) & 15u;
  state->lcd_refresh = (config->lcd_extra >> 20) & 15u;
  state->game_sort = (config->lcd_extra >> 24) & 3u;
  state->dark_mode = (config->lcd_extra & (1u << 26)) ? 0u : 1u;
  state->fast_forward_mode = (config->lcd_extra >> 27) & 1u;
  state->fast_forward_speed = (config->lcd_extra >> 28) & 7u;
  state->theme = config->theme;
  state->animations = config->animations_off ? 0 : 1;
  /* Version 2 used this word as zeroed reserved space.  Preserve the new
   * normal-play default when loading an existing configuration. */
  state->frame_limit = config->version >= CGBA_CONFIG_VERSION_V3
    ? config->frame_limit : 1;
}

enum storage_operation { STORAGE_SIZE, STORAGE_READ, STORAGE_WRITE };
typedef struct storage_request {
  enum storage_operation operation;
  const uint16_t *path;
  void *dst;
  const void *src;
  unsigned size;
  int mutated;
} storage_request;

/* One Fugue allocation block. Static scratch avoids adding 4 KiB to the
 * world-switch stack; storage is synchronous and non-reentrant. */
#define STORAGE_COMPARE_BLOCK 4096u
static uint32_t storage_compare_buffer[STORAGE_COMPARE_BLOCK / sizeof(uint32_t)];

static void storage_will_mutate(storage_request *request)
{
  if (!request->mutated) {
    fxcg100_storage_note_mutation();
    request->mutated = 1;
  }
}

static int write_range(storage_request *request, int fd,
                       unsigned offset, unsigned size)
{
  if (os_bfile_seek(fd, (int)offset) < 0)
    return 0;
  /* Even a failed/short write can have relocated storage. */
  storage_will_mutate(request);
  return os_bfile_write(fd, (const uint8_t *)request->src + offset,
                        (int)size) == (int)size;
}

#define STORAGE_MAX_BITMAP_BLOCKS 128u /* 512 KiB (configs, backups, savestates) */

static int update_existing_blob(storage_request *request, int fd)
{
  unsigned blocks = (request->size + STORAGE_COMPARE_BLOCK - 1u) /
                    STORAGE_COMPARE_BLOCK;
  if (blocks <= STORAGE_MAX_BITMAP_BLOCKS) {
    uint32_t dirty[STORAGE_MAX_BITMAP_BLOCKS / 32u] = { 0 };
    /* Phase 1: pure read/compare pass so BFile_Read is never issued after
     * BFile_Write on the same Fugue handle, and a read failure leaves the
     * existing file untouched. */
    for (unsigned b = 0; b < blocks; b++) {
      unsigned offset = b * STORAGE_COMPARE_BLOCK;
      unsigned size = request->size - offset;
      if (size > STORAGE_COMPARE_BLOCK)
        size = STORAGE_COMPARE_BLOCK;
      /* Both supported calculators use Fugue (byte-count return value).
       * Never compare an incompletely filled scratch buffer. */
      if (os_bfile_read(fd, storage_compare_buffer, (int)size,
                        (int)offset) != (int)size)
        return 0;
      if (memcmp(storage_compare_buffer,
                 (const uint8_t *)request->src + offset, size) != 0)
        dirty[b >> 5] |= 1u << (b & 31u);
    }
    /* Phase 2: pure write pass over coalesced dirty runs. */
    unsigned run_start = 0, run_size = 0;
    for (unsigned b = 0; b < blocks; b++) {
      unsigned offset = b * STORAGE_COMPARE_BLOCK;
      unsigned size = request->size - offset;
      if (size > STORAGE_COMPARE_BLOCK)
        size = STORAGE_COMPARE_BLOCK;
      if (dirty[b >> 5] & (1u << (b & 31u))) {
        if (!run_size)
          run_start = offset;
        run_size += size;
      } else if (run_size) {
        if (!write_range(request, fd, run_start, run_size))
          return 0;
        run_size = 0;
      }
    }
    return !run_size || write_range(request, fd, run_start, run_size);
  }
  /* For files larger than the 512 KiB dirty-bitmap limit, never interleave
   * os_bfile_read after os_bfile_write on the same Fugue handle; fall back to
   * a single sequential write of the full payload. */
  return write_range(request, fd, 0, request->size);
}

/* Return the index of the last backslash after the \\fls0\ device prefix, or 0
 * when the path lies directly in the storage root. */
static unsigned path_subdir_end(const uint16_t *path)
{
  static const char prefix[] = "\\\\fls0\\";
  unsigned i, last = 0;

  if (!path)
    return 0;
  for (i = 0; prefix[i]; i++) {
    if (path[i] != (uint16_t)(unsigned char)prefix[i])
      return 0;
  }
  for (; path[i]; i++) {
    if (path[i] == '\\')
      last = i;
  }
  return last > 7u ? last : 0u;
}

static int os_bfile_ensure_parent_dir(const uint16_t *path)
{
  unsigned end = path_subdir_end(path);
  uint16_t dir[CGBA_STORAGE_PATH_MAX];

  if (end == 0 || end >= CGBA_STORAGE_PATH_MAX)
    return 0;
  for (unsigned i = 0; i < end; i++)
    dir[i] = path[i];
  dir[end] = 0;

  for (unsigned i = 7u; i <= end; i++) {
    if (i == end || dir[i] == '\\') {
      uint16_t saved = dir[i];
      int rc;
      dir[i] = 0;
      rc = os_bfile_create(dir, CGBA_BFILE_FOLDER, NULL);
      dir[i] = saved;
      if (rc < 0 && rc != CGBA_BFILE_ALREADY_EXISTS)
        return 0;
    }
  }
  return 1;
}

static int create_file_with_dir(const uint16_t *path, int initial_size,
                                int *dir_ensured)
{
  int create_size = initial_size;

  if (os_bfile_create(path, CGBA_BFILE_FILE, &create_size) >= 0)
    return 1;
  if (!*dir_ensured) {
    *dir_ensured = 1;
    if (os_bfile_ensure_parent_dir(path)) {
      create_size = initial_size;
      if (os_bfile_create(path, CGBA_BFILE_FILE, &create_size) >= 0)
        return 1;
    }
  }
  return 0;
}

/* On Fugue (fx-CG50 / fx-CG100), BFile_Create with the full file size
 * zero-fills every cluster on NOR flash, and the immediate BFile_Write then
 * rewrites every 4 KiB block a second time (doubling flash programming and
 * triggering early GC / sector erases). Create a 0-byte (or 1-byte) file first
 * so BFile_Write allocates and writes the real payload in a single pass; fall
 * back to full pre-allocation if an OS version rejects size-0 creation or
 * refuses to grow the file on write. */
static int create_and_write_blob(storage_request *request)
{
  int dir_ensured = 0;
  int full_size = (int)request->size;
  int sizes[3];
  unsigned count = 0;

  if (full_size > 0)
    sizes[count++] = 0;
  if (full_size > 1)
    sizes[count++] = 1;
  sizes[count++] = full_size;

  storage_will_mutate(request);
  for (unsigned i = 0; i < count; i++) {
    int initial_size = sizes[i];
    if (!create_file_with_dir(request->path, initial_size, &dir_ensured))
      continue;
    int fd = os_bfile_open(request->path, CGBA_BFILE_WRITE_ONLY);
    if (fd < 0) {
      (void)os_bfile_remove(request->path);
      return 0;
    }
    int ok = os_bfile_write(fd, request->src, full_size) == full_size;
    int closed = os_bfile_close(fd);
    if (ok) {
      if (closed >= 0)
        return 1;
      (void)os_bfile_remove(request->path);
      return 0;
    }
    if (initial_size < full_size) {
      if (os_bfile_remove(request->path) < 0)
        return 0;
      continue;
    }
    (void)os_bfile_remove(request->path);
    return 0;
  }
  return 0;
}

/* The entire operation runs in one OS-world visit, including all changed
 * ranges. Absolute reads remain correct after a preceding range write. */
static int storage_transaction(void *opaque)
{
  storage_request *request = opaque;
  int writing = request->operation == STORAGE_WRITE;
  int fd = os_bfile_open(request->path,
    writing ? CGBA_BFILE_READ_WRITE : CGBA_BFILE_READ_ONLY);
  if (fd >= 0) {
    int size = os_bfile_size(fd);
    int ok = 0;
    if (request->operation == STORAGE_SIZE) {
      int closed = os_bfile_close(fd);
      return closed >= 0 ? size : -1;
    }
    if (size < 0) {
      os_bfile_close(fd);
      return 0;
    }
    if (size == (int)request->size) {
      if (writing)
        ok = update_existing_blob(request, fd);
      else
        ok = bfile_read_exact_ok(os_bfile_read(fd, request->dst,
          size, 0), size);
      int closed = os_bfile_close(fd);
      return ok && closed >= 0;
    }
    if (os_bfile_close(fd) < 0 || !writing)
      return 0;
    storage_will_mutate(request);
    if (os_bfile_remove(request->path) < 0)
      return 0;
  } else if (!writing) {
    return request->operation == STORAGE_SIZE ? -1 : 0;
  } else if (fd != -1 && !(fd == -3 && path_subdir_end(request->path))) {
    /* Fugue -1 is EntryNotFound; -3 (IllegalPath) can also occur when a
     * subdirectory such as \\fls0\SAVES does not exist yet. Permission,
     * locking, and I/O errors must not proceed to file creation. */
    return 0;
  }

  return create_and_write_blob(request);
}

static int run_storage_request(storage_request *request)
{
  if (!request->path || request->size > INT_MAX)
    return request->operation == STORAGE_SIZE ? -1 : 0;
#if CGBA_FXCG100_STORAGE
  uint8_t saved_etmu[6];
  cgba_etmu_pause(saved_etmu);
  int result = gint_world_switch(GINT_CALL(storage_transaction, (void *)request));
  cgba_etmu_resume(saved_etmu);
  fxcg100_lcd_note_os_activity();
  return result;
#else
  return storage_transaction(request);
#endif
}

int fxcg100_storage_blob_size(const uint16_t *path)
{
  storage_request request = { .operation = STORAGE_SIZE, .path = path };
  return run_storage_request(&request);
}

int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{
  storage_request request = {
    .operation = STORAGE_READ, .path = path, .dst = dst, .size = size
  };
  return dst && run_storage_request(&request);
}

int fxcg100_storage_write_blob(const uint16_t *path, const void *src, unsigned size)
{
  storage_request request = {
    .operation = STORAGE_WRITE, .path = path, .src = src, .size = size
  };
  return src && run_storage_request(&request);
}

int fxcg100_config_load(fxcg100_menu_state *state)
{
  fxcg100_config_file config;

  if (!state)
    return 0;
  if (!fxcg100_storage_read_blob(config_path, &config, sizeof(config)) ||
      !config_valid(&config))
    return 0;

  state_from_config(state, &config);
  return 1;
}

int fxcg100_config_save(const fxcg100_menu_state *state)
{
  fxcg100_config_file config;

  if (!state ||
      !fxcg100_input_maps_valid(state->keymap, state->hotkey_map))
    return 0;

  config_from_state(&config, state);
  if (!config_valid(&config))
    return 0;
  return fxcg100_storage_write_blob(config_path, &config, sizeof(config));
}

#endif
