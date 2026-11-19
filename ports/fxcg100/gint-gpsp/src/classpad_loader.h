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

/* Addresses that HollyHock-3's symbol table gives for File_GetAddr and
 * File_Open, read at the ELF entry; 0 if the table has no such entry. Not
 * yet checked: see cp_hhk_resolve_file_getaddr in classpad_hhk.h. */
uint32_t cgba_classpad_file_getaddr(void);
uint32_t cgba_classpad_file_open(void);

#endif
