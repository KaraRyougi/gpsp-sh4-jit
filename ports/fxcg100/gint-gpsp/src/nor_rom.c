#include "nor_rom.h"

#include <gint/bfile.h>

#include <stdio.h>
#include <string.h>

#define CGBA_FLASH_BLOCK_SIZE 0x1000u
#define CGBA_FLASH_BLOCKS_PER_PAGE \
	(CGBA_NOR_ROM_PAGE_SIZE / CGBA_FLASH_BLOCK_SIZE)

#define CGBA_NOR_BASE_P1 ((uintptr_t)0x80b00000u)
#define CGBA_NOR_BASE_P2 ((uintptr_t)0xa0b00000u)
#define CGBA_NOR_END_P1  ((uintptr_t)0x81500000u)
#define CGBA_NOR_END_P2  ((uintptr_t)0xa1500000u)

typedef int (*cgba_bfile_open_t)(const uint16_t *path, int mode);
typedef int (*cgba_bfile_size_t)(int fd);
typedef int (*cgba_bfile_read_t)(int fd, void *dst, int size, int offset);
typedef int (*cgba_bfile_close_t)(int fd);
typedef int (*cgba_bfile_get_block_address_t)(int fd, int offset,
	unsigned char **address);
typedef int (*cgba_bfile_find_first_t)(const uint16_t *pattern, int *handle,
	uint16_t *found, struct BFile_FileInfo *fileinfo);
typedef int (*cgba_bfile_find_next_t)(int handle, uint16_t *found,
	struct BFile_FileInfo *fileinfo);
typedef int (*cgba_bfile_find_close_t)(int handle);

#define CGBA_BFILE_OPEN              ((uintptr_t)0x803338d0u)
#define CGBA_BFILE_SIZE              ((uintptr_t)0x80333b04u)
#define CGBA_BFILE_READ              ((uintptr_t)0x80333dc2u)
#define CGBA_BFILE_CLOSE             ((uintptr_t)0x80333a4eu)
#define CGBA_BFILE_GET_BLOCK_ADDRESS ((uintptr_t)0x80333cf2u)
#define CGBA_BFILE_FIND_FIRST        ((uintptr_t)0x803345c8u)
#define CGBA_BFILE_FIND_NEXT         ((uintptr_t)0x80334846u)
#define CGBA_BFILE_FIND_CLOSE        ((uintptr_t)0x80334950u)

#ifdef CGBA_FXCG100
#define CGBA_HIGH_BSS __attribute__((section(".cgba.highbss"), aligned(32)))
#else
#define CGBA_HIGH_BSS
#endif

static uint8_t cgba_nor_single_page[CGBA_NOR_ROM_PAGE_SIZE] CGBA_HIGH_BSS;

static const uint16_t cgba_storage_root_prefix[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\', 0
};
static const uint16_t cgba_storage_root_pattern[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\', '*', 0
};
static const uint16_t cgba_storage_gba_pattern[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\', '*', '.', 'G', 'B', 'A', 0
};
static const uint16_t cgba_storage_fallback_cgba[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'C', 'G', 'B', 'A', 'I', 'N', 'P', '.', 'G', 'B', 'A', 0
};
static const uint16_t cgba_storage_fallback_game[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'G', 'A', 'M', 'E', '.', 'G', 'B', 'A', 0
};
static const uint16_t cgba_storage_fallback_rom[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'R', 'O', 'M', '.', 'G', 'B', 'A', 0
};

static int os_bfile_open(const uint16_t *path, int mode)
{
	cgba_bfile_open_t fn = (cgba_bfile_open_t)CGBA_BFILE_OPEN;
	return fn(path, mode);
}

static int os_bfile_size(int fd)
{
	cgba_bfile_size_t fn = (cgba_bfile_size_t)CGBA_BFILE_SIZE;
	return fn(fd);
}

static int os_bfile_read(int fd, void *dst, int size, int offset)
{
	cgba_bfile_read_t fn = (cgba_bfile_read_t)CGBA_BFILE_READ;
	return fn(fd, dst, size, offset);
}

static void os_bfile_close(int fd)
{
	cgba_bfile_close_t fn = (cgba_bfile_close_t)CGBA_BFILE_CLOSE;
	fn(fd);
}

static int os_bfile_get_block_address(int fd, int offset,
	unsigned char **address)
{
	cgba_bfile_get_block_address_t fn =
		(cgba_bfile_get_block_address_t)CGBA_BFILE_GET_BLOCK_ADDRESS;
	return fn(fd, offset, address);
}

static int os_bfile_find_first(const uint16_t *pattern, int *handle,
	uint16_t *found, struct BFile_FileInfo *fileinfo)
{
	cgba_bfile_find_first_t fn =
		(cgba_bfile_find_first_t)CGBA_BFILE_FIND_FIRST;
	return fn(pattern, handle, found, fileinfo);
}

static int os_bfile_find_next(int handle, uint16_t *found,
	struct BFile_FileInfo *fileinfo)
{
	cgba_bfile_find_next_t fn =
		(cgba_bfile_find_next_t)CGBA_BFILE_FIND_NEXT;
	return fn(handle, found, fileinfo);
}

static void os_bfile_find_close(int handle)
{
	cgba_bfile_find_close_t fn =
		(cgba_bfile_find_close_t)CGBA_BFILE_FIND_CLOSE;
	fn(handle);
}

static char ascii_upper(char c)
{
	return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static char fc_ascii(uint16_t c)
{
	return c >= 0x20 && c < 0x7f ? (char)c : '?';
}

static uint16_t fc_upper(uint16_t c)
{
	return c >= 'a' && c <= 'z' ? (uint16_t)(c - 'a' + 'A') : c;
}

static size_t fc_len(const uint16_t *text, size_t max)
{
	size_t len = 0;

	if(!text)
		return 0;
	while(len < max && text[len])
		len++;
	return len;
}

static int fc_copy(uint16_t *dst, const uint16_t *src, size_t max)
{
	size_t i = 0;

	if(!dst || !src || max == 0)
		return 0;

	for(; i + 1 < max && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
	return src[i] == 0;
}

static int fc_append(uint16_t *dst, const uint16_t *src, size_t max)
{
	size_t len = fc_len(dst, max);

	if(len >= max)
		return 0;
	return fc_copy(dst + len, src, max - len);
}

static int fc_equal_ignore_case(const uint16_t *a, const uint16_t *b)
{
	size_t i = 0;

	if(!a || !b)
		return 0;
	while(a[i] || b[i]) {
		if(fc_upper(a[i]) != fc_upper(b[i]))
			return 0;
		i++;
	}
	return 1;
}

static int found_path_is_absolute(const uint16_t *path)
{
	return path && path[0] == '\\' && path[1] == '\\';
}

static int path_from_found(uint16_t *dst, const uint16_t *found, size_t max)
{
	if(found_path_is_absolute(found))
		return fc_copy(dst, found, max);
	if(!fc_copy(dst, cgba_storage_root_prefix, max))
		return 0;
	return fc_append(dst, found, max);
}

static int path_ends_with_ascii(const uint16_t *path, const char *suffix)
{
	size_t path_len = fc_len(path, CGBA_NOR_ROM_PATH_MAX);
	size_t suffix_len = 0;
	size_t start;

	if(!path || !suffix)
		return 0;
	while(suffix[suffix_len])
		suffix_len++;
	if(path_len < suffix_len)
		return 0;

	start = path_len - suffix_len;
	for(size_t i = 0; i < suffix_len; i++) {
		if(ascii_upper(fc_ascii(path[start + i])) !=
				ascii_upper(suffix[i]))
			return 0;
	}
	return 1;
}

static int path_has_gba_extension(const uint16_t *path)
{
	return path_ends_with_ascii(path, ".GBA");
}

static size_t basename_start(const uint16_t *path)
{
	size_t start = 0;

	if(!path)
		return 0;
	for(size_t i = 0; path[i]; i++) {
		if(path[i] == '\\' || path[i] == '/')
			start = i + 1;
	}
	return start;
}

static void label_from_path(char *label, size_t label_size,
	const uint16_t *path)
{
	size_t start;
	size_t out = 0;

	if(!label || label_size == 0)
		return;

	start = basename_start(path);
	for(size_t i = start; path[i] && out + 1 < label_size; i++)
		label[out++] = fc_ascii(path[i]);
	label[out] = 0;
	if(out == 0 && label_size > 1) {
		label[0] = '?';
		label[1] = 0;
	}
}

static const uint8_t *cached_nor_pointer(unsigned char *address)
{
	uintptr_t value = (uintptr_t)address;

	if(value >= CGBA_NOR_BASE_P2 && value < CGBA_NOR_END_P2)
		value = value - CGBA_NOR_BASE_P2 + CGBA_NOR_BASE_P1;
	if(value < CGBA_NOR_BASE_P1 || value >= CGBA_NOR_END_P1)
		return NULL;

	return (const uint8_t *)value;
}

static int make_root_path(uint16_t *path, size_t path_count,
	const char *name8dot3, unsigned variant)
{
	static const uint16_t prefix[] = {
		'\\', '\\', 'f', 'l', 's', '0', '\\', 0
	};
	size_t out = 0;
	int after_dot = 0;

	if(!path || !name8dot3 || path_count == 0)
		return -1;

	for(size_t i = 0; prefix[i]; i++) {
		if(out + 1 >= path_count)
			return -1;
		path[out++] = prefix[i];
	}

	for(size_t i = 0; name8dot3[i]; i++) {
		char c = name8dot3[i];

		if(c < 0x20 || c >= 0x7f || c == '\\' || c == '/')
			return -1;
		if(variant == 1 && c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		else if(variant == 2 && c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		else if(variant == 3 && after_dot &&
				c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if(c == '.')
			after_dot = 1;
		if(out + 1 >= path_count)
			return -1;
		path[out++] = (uint16_t)(unsigned char)c;
	}

	path[out] = 0;
	return 0;
}

void cgba_nor_rom_reset(cgba_nor_rom *rom)
{
	if(!rom)
		return;

	memset(rom, 0, sizeof(*rom));
	rom->fd = -1;
	rom->block_result = 0;
	rom->open_result = 0;
	rom->size_result = 0;
}

static void close_fd_preserve_status(cgba_nor_rom *rom)
{
	if(!rom)
		return;

	if(rom->fd >= 0)
		os_bfile_close(rom->fd);
	rom->fd = -1;
}

void cgba_nor_rom_close(cgba_nor_rom *rom)
{
	if(!rom)
		return;

	close_fd_preserve_status(rom);
	cgba_nor_rom_reset(rom);
}

static int open_path_variants(cgba_nor_rom *rom, uint16_t *path,
	size_t path_count, const char *name8dot3)
{
	for(unsigned variant = 0; variant < 4; variant++) {
		if(make_root_path(path, path_count, name8dot3, variant) != 0)
			continue;

		int fd = os_bfile_open(path, BFile_ReadOnly);
		rom->open_result = fd;
		rom->path_variant = variant;
		if(fd >= 0)
			return fd;
	}

	return -1;
}

static int load_single_page_fallback(cgba_nor_rom *rom)
{
	if(!rom || rom->fd < 0 || rom->size == 0 ||
			rom->size > CGBA_NOR_ROM_PAGE_SIZE)
		return -1;

	memset(cgba_nor_single_page, 0xff, sizeof(cgba_nor_single_page));
	int got = os_bfile_read(rom->fd, cgba_nor_single_page,
		(int)rom->size, 0);
	if(got != (int)rom->size) {
		rom->last_error = -6;
		rom->block_result = got;
		return -1;
	}

	rom->padded_size = CGBA_NOR_ROM_PAGE_SIZE;
	rom->page_count = 1;
	rom->pages[0] = cgba_nor_single_page;
	rom->direct_page_count = 0;
	rom->fallback_used = 1;
	return 0;
}

static int resolve_page(cgba_nor_rom *rom, uint32_t page)
{
	const uint8_t *base = NULL;

	for(uint32_t block = 0; block < CGBA_FLASH_BLOCKS_PER_PAGE; block++) {
		unsigned char *raw = NULL;
		int offset = (int)(page * CGBA_NOR_ROM_PAGE_SIZE +
			block * CGBA_FLASH_BLOCK_SIZE);
		int result = os_bfile_get_block_address(rom->fd, offset, &raw);
		const uint8_t *ptr = result < 0 ? NULL : cached_nor_pointer(raw);

		if(!ptr) {
			rom->block_result = result;
			rom->fail_page = page;
			rom->fail_block = block;
			rom->first_address = (uintptr_t)raw;
			return -1;
		}

		if(page == 0 && block == 0) {
			rom->block_result = result;
			rom->first_address = (uintptr_t)ptr;
		}

		if(block == 0) {
			base = ptr;
			continue;
		}

		if(ptr != base + block * CGBA_FLASH_BLOCK_SIZE) {
			rom->block_result = result;
			rom->fail_page = page;
			rom->fail_block = block;
			rom->first_address = (uintptr_t)ptr;
			return -1;
		}
	}

	rom->pages[page] = base;
	rom->direct_page_count++;
	return 0;
}

static int map_open_fd(cgba_nor_rom *rom, int fd)
{
	int size;

	size = os_bfile_size(fd);
	rom->fd = fd;
	rom->size_result = size;
	if(size <= 0 || (uint32_t)size > 32u * 1024u * 1024u) {
		rom->last_error = -3;
		close_fd_preserve_status(rom);
		return -3;
	}

	rom->size = (uint32_t)size;
	rom->padded_size = (rom->size + CGBA_NOR_ROM_PAGE_SIZE - 1u) &
		~(CGBA_NOR_ROM_PAGE_SIZE - 1u);
	rom->page_count = rom->padded_size / CGBA_NOR_ROM_PAGE_SIZE;
	if(rom->page_count == 0 || rom->page_count > CGBA_NOR_ROM_MAX_PAGES) {
		rom->last_error = -4;
		close_fd_preserve_status(rom);
		return -4;
	}

	if(rom->size != rom->padded_size && rom->page_count == 1) {
		if(load_single_page_fallback(rom) == 0)
			return 0;
		close_fd_preserve_status(rom);
		return -6;
	}
	if(rom->size != rom->padded_size) {
		rom->last_error = -7;
		close_fd_preserve_status(rom);
		return -7;
	}

	for(uint32_t page = 0; page < rom->page_count; page++) {
		if(resolve_page(rom, page) != 0) {
			if(rom->page_count == 1 &&
					load_single_page_fallback(rom) == 0)
				return 0;
			rom->last_error = -5;
			close_fd_preserve_status(rom);
			return -5;
		}
	}

	return 0;
}

int cgba_nor_rom_open_root_file(cgba_nor_rom *rom, const char *name8dot3)
{
	uint16_t path[64];
	int fd;

	if(!rom)
		return -1;

	cgba_nor_rom_close(rom);

	if(make_root_path(path, sizeof(path) / sizeof(path[0]), name8dot3, 0) != 0) {
		rom->last_error = -1;
		return -1;
	}

	fd = open_path_variants(rom, path,
		sizeof(path) / sizeof(path[0]), name8dot3);
	if(fd < 0) {
		rom->last_error = -2;
		return -2;
	}

	return map_open_fd(rom, fd);
}

int cgba_nor_rom_open_path(cgba_nor_rom *rom, const uint16_t *path)
{
	int fd;

	if(!rom)
		return -1;

	cgba_nor_rom_close(rom);
	if(!path || fc_len(path, CGBA_NOR_ROM_PATH_MAX) == 0) {
		rom->last_error = -1;
		return -1;
	}

	fd = os_bfile_open(path, BFile_ReadOnly);
	rom->open_result = fd;
	rom->path_variant = 0;
	if(fd < 0) {
		rom->last_error = -2;
		return -2;
	}

	return map_open_fd(rom, fd);
}

static int rom_entry_less(const cgba_nor_rom_entry *a,
	const cgba_nor_rom_entry *b)
{
	for(size_t i = 0; i < CGBA_NOR_ROM_LABEL_MAX; i++) {
		char ca = ascii_upper(a->label[i]);
		char cb = ascii_upper(b->label[i]);

		if(ca != cb)
			return ca < cb;
		if(ca == 0)
			return 0;
	}
	return 0;
}

static void sort_rom_entries(cgba_nor_rom_entry *entries, uint32_t count)
{
	for(uint32_t i = 1; i < count; i++) {
		cgba_nor_rom_entry entry = entries[i];
		uint32_t j = i;

		while(j > 0 && rom_entry_less(&entry, &entries[j - 1])) {
			entries[j] = entries[j - 1];
			j--;
		}
		entries[j] = entry;
	}
}

static int rom_entry_exists(const cgba_nor_rom_list *list,
	const uint16_t *path)
{
	for(uint32_t i = 0; i < list->count; i++) {
		if(fc_equal_ignore_case(list->entries[i].path, path))
			return 1;
	}
	return 0;
}

static int add_rom_entry_from_path(cgba_nor_rom_list *list,
	const uint16_t *path)
{
	int fd;
	int size;

	if(!list || list->count >= CGBA_NOR_ROM_MAX_ENTRIES || !path ||
			!path_has_gba_extension(path) ||
			rom_entry_exists(list, path))
		return 0;

	fd = os_bfile_open(path, BFile_ReadOnly);
	if(fd < 0)
		return 0;

	size = os_bfile_size(fd);
	os_bfile_close(fd);
	if(size <= 0 || (uint32_t)size > 32u * 1024u * 1024u)
		return 0;

	if(!fc_copy(list->entries[list->count].path, path,
			CGBA_NOR_ROM_PATH_MAX))
		return 0;
	label_from_path(list->entries[list->count].label,
		CGBA_NOR_ROM_LABEL_MAX, path);
	list->entries[list->count].size = (uint32_t)size;
	list->count++;
	return 1;
}

static int list_storage_roms_with_pattern(cgba_nor_rom_list *list,
	const uint16_t *pattern)
{
	uint16_t found[256];
	struct BFile_FileInfo info;
	int handle = -1;
	int result;

	if(!list || list->count >= CGBA_NOR_ROM_MAX_ENTRIES)
		return 0;

	result = os_bfile_find_first(pattern, &handle, found, &info);
	list->find_result = result;
	while(result == 0) {
		uint16_t path[CGBA_NOR_ROM_PATH_MAX];

		if(path_from_found(path, found, CGBA_NOR_ROM_PATH_MAX)) {
			add_rom_entry_from_path(list, path);
			if(list->count == CGBA_NOR_ROM_MAX_ENTRIES)
				break;
		}

		result = os_bfile_find_next(handle, found, &info);
		list->find_result = result;
	}

	if(handle >= 0)
		os_bfile_find_close(handle);

	return result;
}

unsigned cgba_nor_rom_scan_gba(cgba_nor_rom_list *list)
{
	static const uint16_t *const fallback_paths[] = {
		cgba_storage_fallback_cgba,
		cgba_storage_fallback_game,
		cgba_storage_fallback_rom,
	};

	if(!list)
		return 0;

	memset(list, 0, sizeof(*list));
	list->find_result = 0;
	list_storage_roms_with_pattern(list, cgba_storage_gba_pattern);
	list_storage_roms_with_pattern(list, cgba_storage_root_pattern);

	for(size_t i = 0; i < sizeof(fallback_paths) / sizeof(fallback_paths[0]);
			i++)
		add_rom_entry_from_path(list, fallback_paths[i]);

	sort_rom_entries(list->entries, list->count);
	return list->count;
}

void cgba_nor_rom_status(const cgba_nor_rom *rom, char *dst, size_t dst_size)
{
	if(!dst || dst_size == 0)
		return;

	if(!rom) {
		snprintf(dst, dst_size, "NOR: no state");
		return;
	}

	snprintf(dst, dst_size,
		"NOR e%d o%d s%d p%u/%u b%d:%u a%08lx%s",
		rom->last_error, rom->open_result, rom->size_result,
		(unsigned)rom->direct_page_count, (unsigned)rom->page_count,
		rom->block_result, (unsigned)rom->fail_block,
		(unsigned long)rom->first_address,
		rom->fallback_used ? " R" : "");
}
