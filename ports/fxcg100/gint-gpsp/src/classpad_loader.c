/*
 * fx-CP400 memory bring-up.
 *
 * The emulator needs about 4.7 MiB at fixed link addresses (JIT caches, GBA
 * memory, the hot emulator code), but under HollyHock-3 only the 330 KiB
 * "VRAM backup" area is known to be free. Everything else in RAM that is not
 * the OS's own data is the OS heap, so that is where the memory has to come
 * from, and it has to be obtained through the heap allocator for the OS file
 * calls (which allocate from the same heap) to stay safe.
 *
 * The heap hands out addresses we do not control, while the arena is linked
 * at a fixed address near the top of the heap window (classpad_gpsp.ld). So
 * one block is requested that is large enough to reach from wherever the heap
 * currently ends up to the end of the arena, and the result is checked: the
 * block must contain the whole arena. Only then is anything written there.
 * The part of the block below the arena is not wasted; it holds the ROM.
 *
 * The launcher loads only what fits the VRAM backup area. The hot emulator
 * code is linked into the arena and sits in a section of the .hh3 that no
 * program header covers; it is read from the file once the arena exists.
 *
 * Everything in this file runs before the arena is valid, so it must keep
 * its own state in .data (the linker script sends all other .bss to the
 * arena) and call nothing but gint.
 */

#include "classpad_loader.h"
#include "classpad_elf.h"
#include "ports/fxcg100/sh4/sh4_cache.h"

#include <gint/gint.h>

#include <stdlib.h>
#include <string.h>

#define LOW_DATA __attribute__((section(".data.cgba_loader")))

extern uint8_t cgba_arena_start[], cgba_arena_end[];
extern uint8_t cgba_core_start[], cgba_core_end[];

extern int BFile_Open(const char *path, int flags);
extern int BFile_Read(int fd, void *buf, int size);
extern int BFile_Seek(int fd, int offset, int whence);
extern int BFile_Close(int fd);

/* Stamp linked into both halves of the image. Reading it back from the loaded
 * core proves the file on storage is the build that is running. */
#define CORE_STAMP "gpSP core " __DATE__ " " __TIME__
static const char low_stamp[] = CORE_STAMP;
const char cgba_core_stamp[sizeof(CORE_STAMP)]
	__attribute__((section(".cgba.core.stamp"), used)) = CORE_STAMP;

#define PROBE_BYTES   (1024u * 1024u)
#define RETRY_STEP    (256u * 1024u)
#define ROM_PAGE      0x8000u
#define SELF_PATH_MAX 128

/* ILRAM holds the JIT dispatch stubs. On the fx-CG gint reloads on-chip
 * memory from the add-in image after every OS call; on this model it cannot
 * (the launcher loaded it once), so have gint save and restore it around each
 * world switch instead. Lives in the arena like any other .bss here. */
static uint8_t onchip_backup[GINT_ONCHIP_BUFSIZE];

static uint8_t *reserved_block LOW_DATA = NULL;
static uint32_t reserved_size LOW_DATA = 0;
static char self_path[SELF_PATH_MAX] LOW_DATA = "\\fls0\\gpSP.hh3";

/* ELF entry point: remember which file the launcher started, then continue
 * into gint's normal HollyHock entry. */
extern int hhk3_entry(int argc, char **argv, char **envp);

int cgba_hhk3_entry(int argc, char **argv, char **envp)
{
	if(argc > 0 && argv && argv[0] && argv[0][0] == '\\') {
		unsigned i = 0;

		while(i + 1 < SELF_PATH_MAX && argv[0][i]) {
			self_path[i] = argv[0][i];
			i++;
		}
		self_path[i] = 0;
	}
	return hhk3_entry(argc, argv, envp);
}

/* ---- reserving the arena -------------------------------------------------- */

static int block_covers_arena(const uint8_t *block, uint32_t size)
{
	return block <= cgba_arena_start &&
		(uintptr_t)block + size >= (uintptr_t)cgba_arena_end;
}

static const char *reserve_arena(void)
{
	uint32_t arena_size = (uint32_t)(cgba_arena_end - cgba_arena_start);
	uint8_t *probe, *block;
	uint32_t size;

	/* Where does the heap currently hand out a large block? */
	probe = malloc(PROBE_BYTES);
	if(!probe)
		return "not enough free RAM";
	free(probe);
	if(probe > cgba_arena_start)
		return "OS heap already uses the gpSP area";

	/* Ask for everything from there to the end of the arena. If the heap
	 * did not reuse the probe's slot the block starts higher and overshoots;
	 * the linker script leaves room above the arena for that. Should the
	 * request still be refused, a smaller one can only succeed by starting
	 * higher, so step down until the block no longer reaches the arena. */
	for(size = (uint32_t)(cgba_arena_end - probe); size >= arena_size;
			size -= RETRY_STEP) {
		block = malloc(size);
		if(!block)
			continue;
		if(block_covers_arena(block, size)) {
			reserved_block = block;
			reserved_size = size;
			return NULL;
		}
		free(block);
		if(block > cgba_arena_start)
			break;
	}
	return "cannot reserve the gpSP RAM area";
}

void cgba_classpad_rom_pool(uint8_t **base, uint32_t *size)
{
	uintptr_t first = ((uintptr_t)reserved_block + ROM_PAGE - 1u) &
		~(uintptr_t)(ROM_PAGE - 1u);
	uintptr_t end = (uintptr_t)cgba_arena_start & ~(uintptr_t)(ROM_PAGE - 1u);

	if(!reserved_block || first >= end) {
		*base = NULL;
		*size = 0;
		return;
	}
	*base = (uint8_t *)first;
	*size = (uint32_t)(end - first);
}

/* ---- loading the core ----------------------------------------------------- */

static int self_read(void *ctx, void *dst, uint32_t offset, uint32_t size)
{
	int fd = *(int *)ctx;

	if(BFile_Seek(fd, (int)offset, 0) < 0)
		return 0;
	return BFile_Read(fd, dst, (int)size) == (int)size;
}

/* Runs in the OS world. */
static int load_core(void)
{
	uint32_t size = (uint32_t)(cgba_core_end - cgba_core_start);
	uint32_t offset = 0;
	int fd = BFile_Open(self_path, 1);
	int ok;

	if(fd < 0)
		return -10;
	ok = cp_elf_find_section(self_read, &fd,
		(uint32_t)(uintptr_t)cgba_core_start, size, &offset);
	if(ok == 0 && !self_read(&fd, cgba_core_start, offset, size))
		ok = -5;
	BFile_Close(fd);
	return ok;
}

const char *cgba_classpad_arena_init(void)
{
	const char *error = reserve_arena();

	if(error)
		return error;

	/* The linker script puts every emulator .bss in the arena. */
	memset(cgba_arena_start, 0, (size_t)(cgba_arena_end - cgba_arena_start));
	gint_set_onchip_save_mode(GINT_ONCHIP_BACKUP, onchip_backup);

	if(gint_world_switch(GINT_CALL(load_core)) != 0) {
		cgba_classpad_arena_release();
		return "cannot read the core from gpSP.hh3";
	}
	if(memcmp(cgba_core_stamp, low_stamp, sizeof(low_stamp)) != 0) {
		cgba_classpad_arena_release();
		return "gpSP.hh3 on storage is a different build";
	}
	/* The core arrived through the data cache; make it executable. */
	cgba_sh4_cache_sync(cgba_core_start, cgba_core_end);
	return NULL;
}

void cgba_classpad_arena_release(void)
{
	gint_set_onchip_save_mode(GINT_ONCHIP_NOTHING, NULL);
	if(reserved_block)
		free(reserved_block);
	reserved_block = NULL;
	reserved_size = 0;
}
