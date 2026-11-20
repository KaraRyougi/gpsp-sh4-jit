/*
 * Host test for ROM library filtering (omitting VSYNCTST.GBA and AppleDouble),
 * directory scan_order preservation, CGBALIB.DAT metadata reconciliation
 * (last_played / newly added tracking), and library display sorting.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct BFile_FileInfo;

static int fake_open(const uint16_t *path, int mode);
static int fake_size(int fd);
static int fake_close(int fd);
static int fake_find_first(const uint16_t *pattern, int *handle,
	uint16_t *found, struct BFile_FileInfo *info);
static int fake_find_next(int handle, uint16_t *found,
	struct BFile_FileInfo *info);
static int fake_find_close(int handle);

#define CGBA_HOST_TEST 1
#define CGBA_FXCG100_STORAGE 1
#define CGBA_BFILE_OPEN              fake_open
#define CGBA_BFILE_SIZE              fake_size
#define CGBA_BFILE_READ              ((uintptr_t)3u)
#define CGBA_BFILE_CLOSE             fake_close
#define CGBA_BFILE_GET_BLOCK_ADDRESS ((uintptr_t)4u)
#define CGBA_BFILE_FIND_FIRST        fake_find_first
#define CGBA_BFILE_FIND_NEXT         fake_find_next
#define CGBA_BFILE_FIND_CLOSE        fake_find_close
#include "ports/fxcg100/gint-gpsp/src/nor_rom.c"
#include "ports/fxcg100/gint-gpsp/src/rom_meta.h"
#include "ports/fxcg100/fxcg100_rom_sort.h"

static int failures;

#define CHECK(condition, ...) do { \
	if (!(condition)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		putchar('\n'); \
	} \
} while (0)

int gint_world_switch(gint_call_t call)
{
	typedef int (*fn0)(void);
	typedef int (*fn1)(uintptr_t);
	typedef int (*fn2)(uintptr_t, uintptr_t);
	typedef int (*fn3)(uintptr_t, uintptr_t, uintptr_t);
	typedef int (*fn4)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

	if (call.function == (void *)fake_open)
		return ((fn2)call.function)(call.args[0], call.args[1]);
	if (call.function == (void *)fake_size || call.function == (void *)fake_close ||
			call.function == (void *)fake_find_close)
		return ((fn1)call.function)(call.args[0]);
	if (call.function == (void *)fake_find_first)
		return ((fn4)call.function)(call.args[0], call.args[1],
			call.args[2], call.args[3]);
	if (call.function == (void *)fake_find_next)
		return ((fn3)call.function)(call.args[0], call.args[1], call.args[2]);
	return ((fn0)call.function)();
}

void fxcg100_lcd_note_os_activity(void) {}

static const char *const *fake_dir;
static unsigned fake_dir_count;
static unsigned fake_find_pos;

static void copy_ascii_to_fc(uint16_t *dst, const char *src)
{
	unsigned i = 0;
	while (src[i]) {
		dst[i] = (uint16_t)(unsigned char)src[i];
		i++;
	}
	dst[i] = 0;
}

static int path_matches_dir_entry(const uint16_t *path, const char *name)
{
	size_t b = basename_start(path), i = 0;

	while (path[b + i] && name[i]) {
		if (ascii_upper(fc_ascii(path[b + i])) != ascii_upper(name[i]))
			return 0;
		i++;
	}
	return path[b + i] == 0 && name[i] == 0;
}

static int fake_open(const uint16_t *path, int mode)
{
	(void)mode;
	for (unsigned i = 0; i < fake_dir_count; i++) {
		if (path_matches_dir_entry(path, fake_dir[i]))
			return (int)(i + 1);
	}
	return -1;
}

static int fake_size(int fd)
{
	return fd > 0 ? 1024 * 1024 : -1;
}

static int fake_close(int fd)
{
	(void)fd;
	return 0;
}

static int pattern_is_star(const uint16_t *pattern)
{
	size_t b = basename_start(pattern);
	return pattern[b] == '*' && pattern[b + 1] == 0;
}

static int fake_find_first(const uint16_t *pattern, int *handle,
	uint16_t *found, struct BFile_FileInfo *info)
{
	(void)info;
	if (!pattern_is_star(pattern) || fake_dir_count == 0)
		return -1;
	*handle = 1;
	fake_find_pos = 0;
	copy_ascii_to_fc(found, fake_dir[fake_find_pos++]);
	return 0;
}

static int fake_find_next(int handle, uint16_t *found,
	struct BFile_FileInfo *info)
{
	(void)handle;
	(void)info;
	if (fake_find_pos >= fake_dir_count)
		return -1;
	copy_ascii_to_fc(found, fake_dir[fake_find_pos++]);
	return 0;
}

static int fake_find_close(int handle)
{
	(void)handle;
	return 0;
}

static cgba_nor_rom_list active_list;

uint32_t fxcg100_rom_source_count(void) { return active_list.count; }
const char *fxcg100_rom_source_label(uint32_t i)
{
	return i < active_list.count ? active_list.entries[i].label : "";
}
uint32_t fxcg100_rom_source_last_played(uint32_t i)
{
	return i < active_list.count ? active_list.entries[i].last_played : 0;
}
uint32_t fxcg100_rom_source_added_order(uint32_t i)
{
	return i < active_list.count ? active_list.entries[i].added_order : 0;
}

static int find_label(const cgba_nor_rom_list *list, const char *label)
{
	for (uint32_t i = 0; i < list->count; i++)
		if (strcmp(list->entries[i].label, label) == 0)
			return (int)i;
	return -1;
}

static void test_filter_and_scan_order(void)
{
	static const char *const dir[] = {
		"Zelda.gba",
		"VSYNCTST.GBA",
		"._Metroid.gba",
		"vsynctst.gba",
		"Mario.GBA",
		"readme.txt",
		"Advance.gba",
	};
	unsigned n;
	int i_adv, i_mar, i_zel;

	fake_dir = dir;
	fake_dir_count = (unsigned)(sizeof(dir) / sizeof(dir[0]));
	n = cgba_nor_rom_scan_gba(&active_list);

	CHECK(n == 3, "expected 3 ROMs after omitting VSYNCTST and ._*, got %u", n);
	CHECK(find_label(&active_list, "VSYNCTST.GBA") < 0 &&
		find_label(&active_list, "vsynctst.gba") < 0,
		"VSYNCTST must be omitted from scan");
	CHECK(find_label(&active_list, "._Metroid.gba") < 0,
		"AppleDouble sidecar must be omitted from scan");

	/* Entries stay alphabetically sorted for stable Name rom_ids. */
	CHECK(strcmp(active_list.entries[0].label, "Advance.gba") == 0 &&
		strcmp(active_list.entries[1].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[2].label, "Zelda.gba") == 0,
		"entries not sorted alphabetically");

	/* Directory scan_order reflects original order: Zelda(1), Mario(2), Advance(3). */
	i_adv = find_label(&active_list, "Advance.gba");
	i_mar = find_label(&active_list, "Mario.GBA");
	i_zel = find_label(&active_list, "Zelda.gba");
	CHECK(active_list.entries[i_zel].scan_order <
			active_list.entries[i_mar].scan_order &&
		active_list.entries[i_mar].scan_order <
			active_list.entries[i_adv].scan_order,
		"scan_order did not preserve directory order (%u, %u, %u)",
		(unsigned)active_list.entries[i_zel].scan_order,
		(unsigned)active_list.entries[i_mar].scan_order,
		(unsigned)active_list.entries[i_adv].scan_order);
}

static void test_meta_and_sorting(void)
{
	static const char *const dir1[] = {
		"Zelda.gba",
		"Mario.GBA",
		"Advance.gba",
	};
	static const char *const dir2[] = {
		"BrandNew.gba",  /* reused first FAT directory slot */
		"Zelda.gba",
		"Mario.GBA",
		"Advance.gba",
	};
	cgba_rom_meta_file meta;
	uint32_t order[FXCG100_ROM_SORT_MAX];
	uint32_t n;
	int i_mar, i_zel;

	memset(&meta, 0, sizeof(meta));
	fake_dir = dir1;
	fake_dir_count = 3;
	cgba_nor_rom_scan_gba(&active_list);

	/* First reconcile seeds metadata from scan_order and marks dirty. */
	CHECK(cgba_rom_meta_reconcile(&meta, &active_list) == 1,
		"first reconcile should be dirty");
	CHECK(cgba_rom_meta_reconcile(&meta, &active_list) == 0,
		"repeat reconcile with no new ROMs must not write");

	/* Newly Added puts Advance (scan 3) > Mario (scan 2) > Zelda (scan 1). */
	n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		FXCG100_GAME_SORT_NEWLY_ADDED);
	CHECK(n == 3 &&
		strcmp(active_list.entries[order[0]].label, "Advance.gba") == 0 &&
		strcmp(active_list.entries[order[1]].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[order[2]].label, "Zelda.gba") == 0,
		"initial Newly Added order wrong");

	/* Playing Mario then Zelda puts Zelda first, Mario second, Advance last. */
	i_mar = find_label(&active_list, "Mario.GBA");
	i_zel = find_label(&active_list, "Zelda.gba");
	CHECK(cgba_rom_meta_record_play(&meta, &active_list,
		&active_list.entries[i_mar]) == 1, "first play should be dirty");
	CHECK(cgba_rom_meta_record_play(&meta, &active_list,
		&active_list.entries[i_mar]) == 0,
		"replaying the most recent game must not write");
	CHECK(cgba_rom_meta_record_play(&meta, &active_list,
		&active_list.entries[i_zel]) == 1, "new game play should be dirty");

	n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		FXCG100_GAME_SORT_LAST_PLAYED);
	CHECK(n == 3 &&
		strcmp(active_list.entries[order[0]].label, "Zelda.gba") == 0 &&
		strcmp(active_list.entries[order[1]].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[order[2]].label, "Advance.gba") == 0,
		"Last Played order wrong");

	/* Next launch: BrandNew.gba appears in FAT slot 0 (reused slot), yet
	 * persistent first_seen must rank it above all previously seen ROMs. */
	fake_dir = dir2;
	fake_dir_count = 4;
	cgba_nor_rom_scan_gba(&active_list);
	CHECK(cgba_rom_meta_reconcile(&meta, &active_list) == 1,
		"new ROM should dirty metadata");
	CHECK(cgba_rom_meta_reconcile(&meta, &active_list) == 0,
		"steady state after new ROM must not write");

	n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		FXCG100_GAME_SORT_NEWLY_ADDED);
	CHECK(n == 4 &&
		strcmp(active_list.entries[order[0]].label, "BrandNew.gba") == 0 &&
		strcmp(active_list.entries[order[1]].label, "Advance.gba") == 0 &&
		strcmp(active_list.entries[order[2]].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[order[3]].label, "Zelda.gba") == 0,
		"Newly Added after adding BrandNew.gba wrong");

	/* Last Played keeps Zelda > Mario > alphabetical unplayed (Advance, BrandNew). */
	n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		FXCG100_GAME_SORT_LAST_PLAYED);
	CHECK(n == 4 &&
		strcmp(active_list.entries[order[0]].label, "Zelda.gba") == 0 &&
		strcmp(active_list.entries[order[1]].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[order[2]].label, "Advance.gba") == 0 &&
		strcmp(active_list.entries[order[3]].label, "BrandNew.gba") == 0,
		"Last Played tie-breaking wrong");

	/* Name sort stays strictly alphabetical. */
	n = fxcg100_rom_sort_order(order, FXCG100_ROM_SORT_MAX,
		FXCG100_GAME_SORT_NAME);
	CHECK(n == 4 &&
		strcmp(active_list.entries[order[0]].label, "Advance.gba") == 0 &&
		strcmp(active_list.entries[order[1]].label, "BrandNew.gba") == 0 &&
		strcmp(active_list.entries[order[2]].label, "Mario.GBA") == 0 &&
		strcmp(active_list.entries[order[3]].label, "Zelda.gba") == 0,
		"Name sort order wrong");
}

int main(void)
{
	test_filter_and_scan_order();
	test_meta_and_sorting();

	if (failures) {
		printf("rom_library_test: %d failure(s)\n", failures);
		return 1;
	}

	puts("ROM library filtering, metadata, and sorting passed");
	return 0;
}
