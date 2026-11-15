#ifndef CGBA_CLASSPAD_LOADER_H
#define CGBA_CLASSPAD_LOADER_H

#include <stdint.h>

/* Reserve the emulator arena from the OS heap and load the RAM-resident core
 * into it. Must be the first thing main() does: until it succeeds, no code
 * outside the launcher-loaded image may run and no variable of the emulator
 * may be touched. Returns NULL on success, otherwise a short reason; after a
 * failure only gint and the code in main.c may be used. */
const char *cgba_classpad_arena_init(void);

/* Give the memory back to the OS heap. Nothing in the arena may be used
 * afterwards. */
void cgba_classpad_arena_release(void);

/* RAM left over below the arena inside the reserved block, used to hold the
 * ROM. The size is a multiple of 32 KiB and may be zero. */
void cgba_classpad_rom_pool(uint8_t **base, uint32_t *size);

#endif
