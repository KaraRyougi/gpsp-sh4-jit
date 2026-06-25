#ifndef CGBA_GINT_GPSP_NOR_ROM_H
#define CGBA_GINT_GPSP_NOR_ROM_H

#include <stddef.h>
#include <stdint.h>

#define CGBA_NOR_ROM_MAX_PAGES 1024u
#define CGBA_NOR_ROM_PAGE_SIZE 0x8000u
#define CGBA_NOR_ROM_MAX_ENTRIES 48u
#define CGBA_NOR_ROM_PATH_MAX 128u
#define CGBA_NOR_ROM_LABEL_MAX 44u

typedef struct cgba_nor_rom {
	int fd;
	int last_error;
	int open_result;
	int size_result;
	int fallback_used;
	uint32_t size;
	uint32_t padded_size;
	uint32_t page_count;
	uint32_t direct_page_count;
	int block_result;
	uint32_t fail_page;
	uint32_t fail_block;
	uint32_t path_variant;
	uintptr_t first_address;
	const uint8_t *pages[CGBA_NOR_ROM_MAX_PAGES];
} cgba_nor_rom;

typedef struct cgba_nor_rom_entry {
	uint16_t path[CGBA_NOR_ROM_PATH_MAX];
	char label[CGBA_NOR_ROM_LABEL_MAX];
	uint32_t size;
} cgba_nor_rom_entry;

typedef struct cgba_nor_rom_list {
	cgba_nor_rom_entry entries[CGBA_NOR_ROM_MAX_ENTRIES];
	uint32_t count;
	int find_result;
} cgba_nor_rom_list;

void cgba_nor_rom_reset(cgba_nor_rom *rom);
int cgba_nor_rom_open_root_file(cgba_nor_rom *rom, const char *name8dot3);
int cgba_nor_rom_open_path(cgba_nor_rom *rom, const uint16_t *path);
void cgba_nor_rom_close(cgba_nor_rom *rom);
void cgba_nor_rom_status(const cgba_nor_rom *rom, char *dst, size_t dst_size);
unsigned cgba_nor_rom_scan_gba(cgba_nor_rom_list *list);

#endif
