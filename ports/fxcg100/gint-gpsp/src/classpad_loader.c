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
#include "classpad_heap.h"
#include "ports/fxcg100/sh4/sh4_cache.h"

#include <gint/gint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOW_DATA __attribute__((section(".data.cgba_loader")))

extern uint8_t cgba_arena_start[], cgba_arena_end[];
/* End of what is linked into the arena; the rest of it is spare. */
extern uint8_t cgba_highbss_end[];
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
static uint32_t heap_mark LOW_DATA = 0;
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
		(uintptr_t)block + size >= (uintptr_t)cgba_highbss_end;
}

/* gint's direct wrappers of the OS heap calls (Mem_Malloc, Mem_Free). */
extern void *__malloc(size_t size);
extern void __free(void *ptr);

/* ---- the OS heap, when it is the one classpad_heap.h describes ------------ */

static uint32_t heap_read32(uint32_t address)
{
	return *(const uint32_t *)(uintptr_t)address;
}

static uint32_t heap_alloc(uint32_t size)
{
	return (uint32_t)(uintptr_t)__malloc(size);
}

static void heap_release(uint32_t address)
{
	__free((void *)(uintptr_t)address);
}

/* The allocator's state, read out of its code (OS 02.01.7003: malloc
 * 0x800a8744, free 0x800a86ac, sbrk 0x800cee9e, Mem_Malloc 0x800d0ad8).
 * The code is checked to be that code; where it keeps its state is taken
 * from its literals, because that differs between calculators. Returns
 * zero if this is the allocator classpad_heap.h describes; otherwise
 * heap_differs has a bit for each thing that is not as expected (shown with
 * the error and on the Settings page). */
static uint32_t heap_differs LOW_DATA = 0;
static cp_heap_os heap_os LOW_DATA = { 0, 0, 0, 0, heap_read32, heap_alloc,
	heap_release };

static int in_ram(uint32_t a)
{
	return a >= 0x8c000000u && a < 0x8d000000u && (a & 3u) == 0;
}

static int os_heap_known(void)
{
	uint32_t d = 0, unit_at;

	if(*(const uint16_t *)0x800a8744u != 0x2fc6u) d |= 0x01u;       /* malloc */
	if(*(const uint32_t *)0x800a8818u != 0x800a86acu) d |= 0x02u;   /* -> free */
	if(*(const uint32_t *)0x800a8820u != 0x800a7f14u) d |= 0x04u;   /* division */
	if(*(const uint32_t *)0x800a8824u != 0x800cee9eu) d |= 0x04u;   /* -> sbrk */
	if(*(const uint16_t *)0x800cee9eu != 0x4f22u) d |= 0x08u;       /* sbrk */
	if(*(const uint32_t *)0x800d0b30u != 0x800a8744u) d |= 0x80u;   /* Mem_Malloc */

	heap_os.head = *(const uint32_t *)0x800a8814u;
	heap_os.brk = *(const uint32_t *)0x800cef34u;
	heap_os.limit = *(const uint32_t *)0x800cef38u;
	unit_at = *(const uint32_t *)0x800a881cu;
	if(!in_ram(heap_os.head) || *(const uint32_t *)0x800a8740u != heap_os.head)
		d |= 0x10u;                               /* malloc and free agree */
	if(!in_ram(heap_os.brk))
		d |= 0x20u;
	if(!in_ram(heap_os.limit - 4u) || heap_os.limit <=
			(uint32_t)(uintptr_t)cgba_arena_end)
		d |= 0x20u;
	if(unit_at < 0x80000000u || unit_at >= 0x82000000u || (unit_at & 3u)) {
		d |= 0x40u;
	} else {
		heap_os.unit = *(const uint32_t *)(uintptr_t)unit_at;
		if(heap_os.unit < 0x10u || heap_os.unit > 0x100000u ||
				(heap_os.unit & 3u))
			d |= 0x40u;
	}
	heap_differs = d;
	return d == 0;
}

/* The arena is linked at fixed addresses, so one heap block has to be found
 * that contains what is linked into it. The break is raised 373 KiB
 * past that on a fresh heap: 26 KiB of small blocks were seen at the top of
 * the old block on a second start, and the tests allow for 330 KiB. */
#define HEAP_GROW_TO 0x8ce40000u

static const char *reserve_arena_known(void)
{
	static char message[48] LOW_DATA;
	cp_heap_result r;
	int status = cp_heap_reserve(&heap_os, (uint32_t)(uintptr_t)cgba_arena_start,
		(uint32_t)(uintptr_t)cgba_highbss_end, HEAP_GROW_TO, &r);

	heap_mark = r.brk;
	if(status == CP_HEAP_OK) {
		reserved_block = (uint8_t *)(uintptr_t)r.block;
		reserved_size = r.size;
		return NULL;
	}
	if(status == CP_HEAP_BLOCKED)
		snprintf(message, sizeof(message), "Heap in use: K %08X",
			(unsigned)r.brk);
	else
		snprintf(message, sizeof(message), "Heap: %08X %X %08X",
			(unsigned)r.block, (unsigned)r.size, (unsigned)r.brk);
	return message;
}

/* Any other OS: its heap is not known, so ask and see. A block is assumed
 * to start where a probe did, or just above it. */
static const char *reserve_arena_unknown(void)
{
	uint32_t used_size = (uint32_t)(cgba_highbss_end - cgba_arena_start);
	uint8_t *probe, *block;
	uint32_t size;

	probe = malloc(PROBE_BYTES);
	if(!probe)
		return "not enough free RAM";
	free(probe);
	heap_mark = (uint32_t)(uintptr_t)probe;
	if(probe > cgba_arena_start) {
		static char message[48] LOW_DATA;

		snprintf(message, sizeof(message), "Heap in use: U%02X %08X",
			(unsigned)heap_differs, (unsigned)(uintptr_t)probe);
		return message;
	}

	for(size = (uint32_t)(cgba_arena_end - probe); size >= used_size;
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

static const char *reserve_arena(void)
{
	return os_heap_known() ? reserve_arena_known() : reserve_arena_unknown();
}

uint32_t cgba_classpad_heap_differs(void)
{
	return heap_differs;
}

uint32_t cgba_classpad_heap_mark(void)
{
	return heap_mark;
}

uint32_t cgba_classpad_reserved_start(void)
{
	return (uint32_t)(uintptr_t)reserved_block;
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
	memset(cgba_arena_start, 0, (size_t)(cgba_highbss_end - cgba_arena_start));
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
