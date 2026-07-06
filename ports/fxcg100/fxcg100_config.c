#include "fxcg100_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#define CGBA_CONFIG_VERSION 2u

#define CGBA_BFILE_FILE 1
#define CGBA_BFILE_READ_ONLY 0x01
#define CGBA_BFILE_WRITE_ONLY 0x02

#define CGBA_BFILE_OPEN   ((uintptr_t)0x803338d0u)
#define CGBA_BFILE_SIZE   ((uintptr_t)0x80333b04u)
#define CGBA_BFILE_CREATE ((uintptr_t)0x80333ef0u)
#define CGBA_BFILE_REMOVE ((uintptr_t)0x80334212u)
#define CGBA_BFILE_READ   ((uintptr_t)0x80333dc2u)
#define CGBA_BFILE_WRITE  ((uintptr_t)0x80333f9eu)
#define CGBA_BFILE_CLOSE  ((uintptr_t)0x80333a4eu)

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
  uint32_t backup_update;
  uint32_t show_fps;
  uint32_t input_record;      /* was reserved[0]: old configs read as OFF */
  uint32_t reserved[7];
} fxcg100_config_file;

static const uint16_t config_path[] = {
  '\\', '\\', 'f', 'l', 's', '0', '\\',
  'C', 'G', 'B', 'A', '.', 'C', 'F', 'G', 0
};

static int os_bfile_open(const uint16_t *path, int mode)
{
#if CGBA_FXCG100_STORAGE
  return gint_world_switch(GINT_CALL((void *)CGBA_BFILE_OPEN, path, mode));
#else
  (void)path;
  (void)mode;
  return -1;
#endif
}

static int os_bfile_size(int fd)
{
#if CGBA_FXCG100_STORAGE
  return gint_world_switch(GINT_CALL((void *)CGBA_BFILE_SIZE, fd));
#else
  (void)fd;
  return -1;
#endif
}

static int os_bfile_create(const uint16_t *path, int type, int *size)
{
#if CGBA_FXCG100_STORAGE
  return gint_world_switch(GINT_CALL((void *)CGBA_BFILE_CREATE,
                                     path, type, size));
#else
  (void)path;
  (void)type;
  (void)size;
  return -1;
#endif
}

static void os_bfile_remove(const uint16_t *path)
{
#if CGBA_FXCG100_STORAGE
  (void)gint_world_switch(GINT_CALL((void *)CGBA_BFILE_REMOVE, path));
#else
  (void)path;
#endif
}

static int os_bfile_read(int fd, void *dst, int size, int offset)
{
#if CGBA_FXCG100_STORAGE
  return gint_world_switch(GINT_CALL((void *)CGBA_BFILE_READ,
                                     fd, dst, size, offset));
#else
  (void)fd;
  (void)dst;
  (void)size;
  (void)offset;
  return -1;
#endif
}

static int bfile_read_exact_ok(int result, int size)
{
  /* Fugue returns bytes read; CASIOWIN returns bytes remaining after read. */
  return result == size || result == 0;
}

static int os_bfile_write(int fd, const void *src, int size)
{
#if CGBA_FXCG100_STORAGE
  return gint_world_switch(GINT_CALL((void *)CGBA_BFILE_WRITE,
                                     fd, src, size));
#else
  (void)fd;
  (void)src;
  (void)size;
  return -1;
#endif
}

static void os_bfile_close(int fd)
{
#if CGBA_FXCG100_STORAGE
  (void)gint_world_switch(GINT_CALL((void *)CGBA_BFILE_CLOSE, fd));
#else
  (void)fd;
#endif
}

static int config_options_valid(const fxcg100_config_file *config)
{
  return config->screen_scale < 4 &&
    config->screen_filter < 4 &&
    config->frameskip_type < 3 &&
    config->frameskip_value < 100 &&
    config->frameskip_variation < 2 &&
    config->savestate_slot < 10 &&
    config->quick_save_slot < 10 &&
    config->input_record < 2 &&
    config->backup_update < 2 &&
    config->show_fps < 2;
}

static int config_valid(const fxcg100_config_file *config)
{
  if (!config)
    return 0;
  if (config->magic != CGBA_CONFIG_MAGIC ||
      config->version != CGBA_CONFIG_VERSION ||
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
  config->backup_update = state->backup_update;
  config->show_fps = state->show_fps;
  config->input_record = state->input_record;
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
  state->backup_update = config->backup_update;
  state->show_fps = config->show_fps;
  state->input_record = config->input_record;
}

static int read_config(fxcg100_config_file *config)
{
  int fd;
  int file_size;
  int ok = 0;

  fd = os_bfile_open(config_path, CGBA_BFILE_READ_ONLY);
  if (fd < 0)
    return 0;

  file_size = os_bfile_size(fd);
  memset(config, 0, sizeof(*config));

  if (file_size == (int)sizeof(*config)) {
    int read_result = os_bfile_read(fd, config, (int)sizeof(*config), 0);

    ok = bfile_read_exact_ok(read_result, (int)sizeof(*config));
  }

  os_bfile_close(fd);
  return ok;
}

static int storage_blob_size(const uint16_t *path)
{
  int fd;
  int size;

  fd = os_bfile_open(path, CGBA_BFILE_READ_ONLY);
  if (fd < 0)
    return -1;
  size = os_bfile_size(fd);
  os_bfile_close(fd);
  return size;
}

static int write_blob_contents(const uint16_t *path, const void *src,
                               unsigned size)
{
  int fd;
  int ok;

  fd = os_bfile_open(path, CGBA_BFILE_WRITE_ONLY);
  if (fd < 0)
    return 0;
  ok = os_bfile_write(fd, src, (int)size) == (int)size;
  os_bfile_close(fd);
  return ok;
}

static int create_blob(const uint16_t *path, unsigned size)
{
  int create_size = (int)size;
  return os_bfile_create(path, CGBA_BFILE_FILE, &create_size) >= 0;
}

static int write_config(const fxcg100_config_file *config)
{
  int existing_size = storage_blob_size(config_path);

  if (existing_size == (int)sizeof(*config))
    return write_blob_contents(config_path, config, sizeof(*config));

  if (existing_size >= 0)
    os_bfile_remove(config_path);

  if (!create_blob(config_path, sizeof(*config))) {
    if (existing_size < 0) {
      os_bfile_remove(config_path);
      if (!create_blob(config_path, sizeof(*config)))
        return 0;
    } else {
      return 0;
    }
  }

  return write_blob_contents(config_path, config, sizeof(*config));
}

/* Generic whole-file storage blobs (savestates): world-switched BFile. */
int fxcg100_storage_blob_size(const uint16_t *path)
{
  return storage_blob_size(path);
}

int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{
  int fd;
  int ok = 0;

  fd = os_bfile_open(path, CGBA_BFILE_READ_ONLY);
  if (fd < 0)
    return 0;
  if (os_bfile_size(fd) == (int)size) {
    int r = os_bfile_read(fd, dst, (int)size, 0);
    ok = bfile_read_exact_ok(r, (int)size);
  }
  os_bfile_close(fd);
  return ok;
}

int fxcg100_storage_write_blob(const uint16_t *path, const void *src, unsigned size)
{
  int existing_size = storage_blob_size(path);

  if (existing_size == (int)size)
    return write_blob_contents(path, src, size);
  if (existing_size >= 0)
    os_bfile_remove(path);
  if (!create_blob(path, size)) {
    os_bfile_remove(path);
    if (!create_blob(path, size))
      return 0;
  }
  return write_blob_contents(path, src, size);
}

int fxcg100_config_load(fxcg100_menu_state *state)
{
  fxcg100_config_file config;

  if (!state)
    return 0;
  if (!read_config(&config) || !config_valid(&config))
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
  return write_config(&config);
}

#endif
