/*
 * ClassPad II memory bring-up.
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
#include "classpad_hhk.h"
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
extern uint8_t cgba_ilram_start[], cgba_ilram_end[];

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
#define CP_ILRAM_SIZE 4096u

/* ILRAM holds the JIT dispatch stubs. On the fx-CG gint reloads on-chip
 * memory from the add-in image after every OS call; on this model it cannot
 * (we load .ilram ourselves in cgba_classpad_arena_init so the ELF has no
 * PT_LOAD outside SDRAM), so have gint save and restore it around each
 * world switch instead. Lives in the arena like any other .bss here. */
static uint8_t onchip_backup[GINT_ONCHIP_BUFSIZE];

static uint8_t *reserved_block LOW_DATA = NULL;
static uint32_t reserved_size LOW_DATA = 0;
static uint32_t heap_mark LOW_DATA = 0;
static char self_path[SELF_PATH_MAX] LOW_DATA = "\\fls0\\gpSP.hh3";
static uint32_t file_getaddr LOW_DATA = 0;
static uint32_t sym_file_open LOW_DATA = 0;
static uint32_t sym_mem_malloc LOW_DATA = 0;
static uint32_t sym_mem_free LOW_DATA = 0;
static uint32_t sym_mem_sbrk LOW_DATA = 0;
static uint32_t sym_mem_sbrk_mode LOW_DATA = 0;

/* OS guard string; HollyHock-3's symbol table starts with the same one. */
#define OS_GUARD ((const char *)0x814fffe0u)

/* ELF entry point: remember which file the launcher started and the OS
 * symbols gpSP needs beyond gint's own bindings (the symbol table may be
 * overwritten once the arena is in use), then continue into gint's normal
 * HollyHock entry. */
extern int hhk3_entry(int argc, char **argv, char **envp);

int cgba_hhk3_entry(int argc, char **argv, char **envp)
{
	file_getaddr = cp_hhk_env_symbol(envp, OS_GUARD, "File_GetAddr");
	sym_file_open = cp_hhk_env_symbol(envp, OS_GUARD, "File_Open");
	sym_mem_malloc = cp_hhk_env_symbol(envp, OS_GUARD, "Mem_Malloc");
	sym_mem_free = cp_hhk_env_symbol(envp, OS_GUARD, "Mem_Free");
	sym_mem_sbrk = cp_hhk_env_symbol(envp, OS_GUARD, "Mem_Sbrk");
	sym_mem_sbrk_mode = cp_hhk_env_symbol(envp, OS_GUARD, "Mem_SbrkMode");
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

typedef void *(*cp_malloc_fn)(size_t);
typedef signed char (*cp_sbrk_mode_fn)(signed char);

static cp_malloc_fn heap_malloc_fn LOW_DATA = NULL;

static const uint8_t *cp_loader_os_code(void *ctx, uint32_t address,
	uint32_t size)
{
	(void)ctx;
	if(address < 0x80000000u || address >= 0x82000000u ||
			size > 0x82000000u - address)
		return NULL;
	return (const uint8_t *)(uintptr_t)address;
}

static uint32_t heap_read32(uint32_t address)
{
	return *(const uint32_t *)(uintptr_t)address;
}

static uint32_t heap_alloc(uint32_t size)
{
	if(heap_malloc_fn)
		return (uint32_t)(uintptr_t)heap_malloc_fn(size);
	return (uint32_t)(uintptr_t)__malloc(size);
}

static void heap_release(uint32_t address)
{
	__free((void *)(uintptr_t)address);
}

/* The allocator's state, discovered dynamically by following PC-relative
 * literals starting from Mem_Malloc in HollyHock-3's symbol table (verified
 * on OS 02.01.2000, 02.01.7002 and 02.01.7003). Returns nonzero if this is
 * the allocator classpad_heap.h describes; otherwise heap_differs has a bit
 * for each check that failed (shown with the error and on the Settings
 * page). */
static uint32_t heap_differs LOW_DATA = 0;
static cp_heap_os heap_os LOW_DATA = { 0, 0, 0, 0, heap_read32, heap_alloc,
	heap_release };

static int os_heap_known(void)
{
	uint32_t mem_malloc = sym_mem_malloc ? sym_mem_malloc : CP_MEM_MALLOC_KNOWN;
	cp_hhk_heap_info info;

	heap_malloc_fn = NULL;
	if(!cp_hhk_find_heap(cp_loader_os_code, NULL, mem_malloc, sym_mem_free,
			sym_mem_sbrk, (uint32_t)(uintptr_t)cgba_highbss_end, &info,
			&heap_differs))
		return 0;

	heap_os.head = info.head;
	heap_os.brk = info.brk;
	heap_os.limit = info.limit;
	heap_os.unit = info.unit;
	heap_malloc_fn = (cp_malloc_fn)(uintptr_t)info.malloc_fn;
	return 1;
}

static cp_sbrk_mode_fn os_sbrk_mode_call(void)
{
	uint32_t entry = sym_mem_sbrk_mode;

	if(!entry && (!sym_mem_malloc || sym_mem_malloc == CP_MEM_MALLOC_KNOWN))
		entry = CP_MEM_SBRK_MODE_KNOWN;
	if(!cp_hhk_sbrk_mode_ok(cp_loader_os_code, NULL, entry))
		return NULL;
	return (cp_sbrk_mode_fn)(uintptr_t)entry;
}

/* The arena is linked at fixed addresses, so one heap block has to be found
 * that contains what is linked into it. On a fresh heap the break is raised
 * to 256 KiB below the heap ceiling (0x8ce40000 on a 0x8ce80000 heap):
 * 26 KiB of small blocks were seen at the top of the old block on a second
 * start, and the tests allow for 330 KiB, while keeping 256 KiB free above
 * the break for OS allocations. */
#define HEAP_TOP_MARGIN 0x40000u

static const char *reserve_arena_known(void)
{
	static char message[48] LOW_DATA;
	uint32_t used_end = (uint32_t)(uintptr_t)cgba_highbss_end;
	uint32_t grow_to = heap_os.limit > used_end + HEAP_TOP_MARGIN
		? heap_os.limit - HEAP_TOP_MARGIN : heap_os.limit;
	cp_heap_result r;
	int status = cp_heap_reserve(&heap_os, (uint32_t)(uintptr_t)cgba_arena_start,
		used_end, grow_to, &r);

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
 * to start where a probe did, or just above it. Runs under Mem_SbrkMode(1)
 * whenever available so oversized attempts return NULL instead of raising
 * the OS "Insufficient System Memory to Run" dialog. */
static const char *reserve_arena_unknown(int sbrk_guarded)
{
	uint32_t used_size = (uint32_t)(cgba_highbss_end - cgba_arena_start);
	uint8_t *probe, *block;
	uint32_t size, max_size;

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

	max_size = (uint32_t)(cgba_arena_end - probe);
	/* Without Mem_SbrkMode(1), avoid overshooting the 0x8ce80000 ceiling
	 * when the 1 MiB probe advanced the break to probe + PROBE_BYTES. */
	if(!sbrk_guarded && max_size > PROBE_BYTES &&
			max_size - PROBE_BYTES >= used_size)
		max_size -= PROBE_BYTES;

	for(size = max_size; size >= used_size; size -= RETRY_STEP) {
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
	cp_sbrk_mode_fn sbrk_mode = os_sbrk_mode_call();
	signed char prev_mode = 0;
	const char *error;

	/* Suppress the OS "Insufficient System Memory to Run" dialog while
	 * reserving the arena so any failed allocation returns NULL cleanly. */
	if(sbrk_mode)
		prev_mode = sbrk_mode(1);
	error = os_heap_known() ? reserve_arena_known()
		: reserve_arena_unknown(sbrk_mode != NULL);
	if(sbrk_mode)
		(void)sbrk_mode(prev_mode);
	return error;
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

uint32_t cgba_classpad_file_getaddr(void)
{
	return file_getaddr;
}

uint32_t cgba_classpad_file_open(void)
{
	return sym_file_open;
}

/* ---- loading the core ----------------------------------------------------- */

static int self_read(void *ctx, void *dst, uint32_t offset, uint32_t size)
{
	int fd = *(int *)ctx;

	if(BFile_Seek(fd, (int)offset, 0) < 0)
		return 0;
	return BFile_Read(fd, dst, (int)size) == (int)size;
}

/* Runs in the OS world. Reads .cgba.core directly into the reserved arena and
 * stages .ilram in the leading 4 KiB of onchip_backup (copied to 0xe5200000
 * after leaving the OS world so OS file calls cannot clobber ILRAM). */
static int load_core(void)
{
	uint32_t core_size = (uint32_t)(cgba_core_end - cgba_core_start);
	uint32_t ilram_size = (uint32_t)(cgba_ilram_end - cgba_ilram_start);
	uint32_t offset = 0;
	int fd, ok;

	if(ilram_size == 0 || ilram_size > CP_ILRAM_SIZE)
		return -11;
	fd = BFile_Open(self_path, 1);
	if(fd < 0)
		return -10;
	ok = cp_elf_find_section(self_read, &fd,
		(uint32_t)(uintptr_t)cgba_core_start, core_size, &offset);
	if(ok == 0 && !self_read(&fd, cgba_core_start, offset, core_size))
		ok = -5;
	if(ok == 0) {
		ok = cp_elf_find_section(self_read, &fd,
			(uint32_t)(uintptr_t)cgba_ilram_start, ilram_size, &offset);
		if(ok == 0 && !self_read(&fd, onchip_backup, offset, ilram_size))
			ok = -6;
	}
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
	/* Keep on-chip save/restore off while load_core stages .ilram into
	 * onchip_backup, so gint_world_switch does not overwrite it on entry. */
	gint_set_onchip_save_mode(GINT_ONCHIP_NOTHING, NULL);

	if(gint_world_switch(GINT_CALL(load_core)) != 0) {
		cgba_classpad_arena_release();
		return "cannot read the core from gpSP.hh3";
	}
	if(memcmp(cgba_core_stamp, low_stamp, sizeof(low_stamp)) != 0) {
		cgba_classpad_arena_release();
		return "gpSP.hh3 on storage is a different build";
	}
	/* Populate ILRAM (and zero its unused tail) now that we are back in the
	 * gint world, then enable backup/restore around later OS calls. */
	memcpy(cgba_ilram_start, onchip_backup, CP_ILRAM_SIZE);
	/* The core and ILRAM stubs arrived through the data cache; make them
	 * executable. */
	cgba_sh4_cache_sync(cgba_core_start, cgba_core_end);
	cgba_sh4_cache_sync(cgba_ilram_start, cgba_ilram_end);
	gint_set_onchip_save_mode(GINT_ONCHIP_BACKUP, onchip_backup);
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
