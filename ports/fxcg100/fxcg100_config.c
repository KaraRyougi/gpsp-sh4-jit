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
#define CGBA_CONFIG_VERSION 3u
#define CGBA_CONFIG_VERSION_LEGACY 2u

#define CGBA_BFILE_FILE 1
#define CGBA_BFILE_READ_ONLY 0x01
#define CGBA_BFILE_WRITE_ONLY 0x02
#define CGBA_BFILE_READ_WRITE 0x03

#if defined(CGBA_CLASSPAD)
/* fx-CP400: the OS file API behind Fugue-style adapters. */
#include "classpad_bfile.h"
#define CGBA_BFILE_OPEN   cp_bfile_open
#define CGBA_BFILE_SIZE   cp_bfile_size
#define CGBA_BFILE_CREATE cp_bfile_create
#define CGBA_BFILE_REMOVE cp_bfile_remove
#define CGBA_BFILE_READ   cp_bfile_read
#define CGBA_BFILE_WRITE  cp_bfile_write
#define CGBA_BFILE_CLOSE  cp_bfile_close
#define CGBA_BFILE_SEEK   cp_bfile_seek
#elif defined(CGBA_FXCG50)
/* fx-CG50 uses gint's portable BFile entry points. Both models run all
 * calls inside the same OS-world transaction below. */
#include <gint/bfile.h>
#define CGBA_BFILE_OPEN   BFile_Open
#define CGBA_BFILE_SIZE   BFile_Size
#define CGBA_BFILE_CREATE BFile_Create
#define CGBA_BFILE_REMOVE BFile_Remove
#define CGBA_BFILE_READ   BFile_Read
#define CGBA_BFILE_WRITE  BFile_Write
#define CGBA_BFILE_CLOSE  BFile_Close
#define CGBA_BFILE_SEEK   BFile_Seek
#else
#ifndef CGBA_BFILE_OPEN
#define CGBA_BFILE_OPEN   ((uintptr_t)0x803338d0u)
#define CGBA_BFILE_SIZE   ((uintptr_t)0x80333b04u)
#define CGBA_BFILE_CREATE ((uintptr_t)0x80333ef0u)
#define CGBA_BFILE_REMOVE ((uintptr_t)0x80334212u)
#define CGBA_BFILE_READ   ((uintptr_t)0x80333dc2u)
#define CGBA_BFILE_WRITE  ((uintptr_t)0x80333f9eu)
#define CGBA_BFILE_CLOSE  ((uintptr_t)0x80333a4eu)
#define CGBA_BFILE_SEEK   ((uintptr_t)0x80333c6cu)
#endif
#endif

typedef struct fxcg100_config_file {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint16_t keymap[FXCG100_GBA_KEY_COUNT];
  uint16_t hotkey_map[FXCG100_HOTKEY_COUNT];
  uint32_t screen_scale;
  uint32_t screen_filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t frameskip_variation;
  uint32_t savestate_slot;
  uint32_t quick_save_slot;
  /* Version-2 compatibility slot. Older builds stored the removed automatic
   * backup policy here; keep the word so existing CGBA.CFG files still load. */
  uint32_t reserved_backup_update;
  uint32_t show_fps;
  uint32_t frame_limit;
  /* fx-CP400 display options, carved out of the reserved words so the file
   * size and version stay unchanged. Both are zero in files written by the
   * fx-CG builds; the touch-key flag is stored inverted so that zero keeps
   * the keys on. */
  uint32_t rotation;
  uint32_t touch_keys_hidden;
  uint32_t lcd_cpu;
  /* fx-CP400 interface: colour scheme, and animations (stored inverted so
   * that zero keeps them on). */
  uint32_t theme;
  uint32_t animations_off;
  uint32_t reserved[2];
} fxcg100_config_file;

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

static int config_options_valid(const fxcg100_config_file *config)
{
  return config->screen_scale < 5 &&
    config->screen_filter < 4 &&
    config->frameskip_type < 3 &&
    config->frameskip_value < 100 &&
    config->frameskip_variation < 2 &&
    config->savestate_slot < 10 &&
    config->quick_save_slot < 10 &&
    config->show_fps < 2 &&
    config->rotation < 2 &&
    config->touch_keys_hidden < 2 &&
    config->lcd_cpu < 2 &&
    config->theme < 16 &&
    config->animations_off < 2 &&
    (config->version == CGBA_CONFIG_VERSION_LEGACY ||
      config->frame_limit < 2);
}

static int config_valid(const fxcg100_config_file *config)
{
  if (!config)
    return 0;
  if (config->magic != CGBA_CONFIG_MAGIC ||
      (config->version != CGBA_CONFIG_VERSION &&
       config->version != CGBA_CONFIG_VERSION_LEGACY) ||
      config->size != sizeof(*config))
    return 0;
  return fxcg100_input_maps_valid(config->keymap, config->hotkey_map) &&
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
  memcpy(config->hotkey_map, state->hotkey_map,
         sizeof(config->hotkey_map));
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
  config->theme = state->theme;
  config->animations_off = state->animations ? 0 : 1;
}

static void state_from_config(fxcg100_menu_state *state,
                              const fxcg100_config_file *config)
{
  memcpy(state->keymap, config->keymap, sizeof(state->keymap));
  memcpy(state->hotkey_map, config->hotkey_map,
         sizeof(state->hotkey_map));
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
  state->theme = config->theme;
  state->animations = config->animations_off ? 0 : 1;
  /* Version 2 used this word as zeroed reserved space.  Preserve the new
   * normal-play default when loading an existing configuration. */
  state->frame_limit = config->version >= CGBA_CONFIG_VERSION
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

static int update_existing_blob(storage_request *request, int fd)
{
  unsigned run_start = 0, run_size = 0;
  for (unsigned offset = 0; offset < request->size;) {
    unsigned size = request->size - offset;
    if (size > STORAGE_COMPARE_BLOCK)
      size = STORAGE_COMPARE_BLOCK;
    /* Both supported calculators use Fugue (byte-count return value).
     * Never compare an incompletely filled scratch buffer. */
    if (os_bfile_read(fd, storage_compare_buffer, (int)size,
                      (int)offset) != (int)size)
      return 0;
    if (memcmp(storage_compare_buffer,
               (const uint8_t *)request->src + offset, size) != 0) {
      if (!run_size)
        run_start = offset;
      run_size += size;
    } else if (run_size) {
      if (!write_range(request, fd, run_start, run_size))
        return 0;
      run_size = 0;
    }
    offset += size;
  }
  return !run_size || write_range(request, fd, run_start, run_size);
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
  } else if (fd != -1) {
    /* Fugue -1 is EntryNotFound. Permission/locking/I/O errors must not
     * cause an existing save to be deleted. */
    return 0;
  }

  int create_size = (int)request->size;
  storage_will_mutate(request);
  if (os_bfile_create(request->path, CGBA_BFILE_FILE, &create_size) < 0)
    return 0;
  fd = os_bfile_open(request->path, CGBA_BFILE_WRITE_ONLY);
  if (fd < 0)
    return 0;
  int ok = os_bfile_write(fd, request->src, (int)request->size)
    == (int)request->size;
  int closed = os_bfile_close(fd);
  return ok && closed >= 0;
}

static int run_storage_request(storage_request *request)
{
  if (!request->path || request->size > INT_MAX)
    return request->operation == STORAGE_SIZE ? -1 : 0;
#if CGBA_FXCG100_STORAGE
  int result = gint_world_switch(GINT_CALL(storage_transaction, (void *)request));
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
