#ifndef CGBA_CLASSPAD_HEAP_H
#define CGBA_CLASSPAD_HEAP_H

/* Getting one block of the fx-CP400 OS heap that contains a fixed address
 * range, without ever making a request the heap cannot meet.
 *
 * It has to be done this way because of two things the OS does. A request
 * that cannot be met is not answered with NULL: Mem_Malloc calls a handler,
 * like operator new, and the handler puts up "Insufficient System Memory to
 * Run". And where a block lands depends on the history of the heap, so a
 * request cannot be sized by trying.
 *
 * The OS heap (02.01.7003, read from the code under Mem_Malloc 0x800d0ad8:
 * malloc 0x800a8744, free 0x800a86ac, sbrk 0x800cee9e):
 *
 *   - 0x8c280000..0x8ce80000, with a break below which it is in use;
 *   - free areas are a list in address order, each
 *     starting with two words: its size (the bytes after the first word)
 *     and the next area. Neighbours are joined when a block is freed;
 *   - a request, rounded up to 4 bytes, takes the first free area that is
 *     exactly its size or at least 8 bytes larger, and is cut from the END
 *     of it, with its size in the word before it;
 *   - if none fits, the break moves up by the size rounded to a unit (always
 *     at least 4 bytes more than asked), the new space is freed into the
 *     list, and the search runs again.
 *
 * So on a fresh heap a block comes from above the break, and once it has
 * been freed it is a free area whose top every later request is cut from.
 * That is why a second start of gpSP found its blocks somewhere else.
 *
 * This header is arithmetic over three callbacks; the host tests run it
 * against a model of the allocator above. */

#include <stdint.h>

#define CP_HEAP_NODES  512u
#define CP_HEAP_BLOCKERS 16u

/* Where the allocator keeps its state differs between builds of the OS (two
 * fx-CP400 with the same code were seen to differ in the break's address
 * and in the rounding unit), so these are read out of the code's own
 * literals rather than assumed: see os_heap_find in classpad_loader.c. */
typedef struct cp_heap_os {
	uint32_t head;       /* address of the free list's head */
	uint32_t brk;        /* address of the break */
	uint32_t limit;      /* end of the heap */
	uint32_t unit;       /* the break moves in multiples of this */
	uint32_t (*read32)(uint32_t address);
	uint32_t (*alloc)(uint32_t size);      /* Mem_Malloc */
	void (*release)(uint32_t address);     /* Mem_Free */
} cp_heap_os;

enum {
	CP_HEAP_OK = 0,
	CP_HEAP_BLOCKED,     /* something else is in the range, or above it */
	CP_HEAP_SURPRISE     /* the block was not where it was worked out */
};

typedef struct cp_heap_result {
	uint32_t block, size;   /* the block, when CP_HEAP_OK; else the last one */
	uint32_t brk;           /* the break before anything was done */
} cp_heap_result;

static inline int cp_heap_fits(uint32_t area, uint32_t size)
{
	return area == size || area >= size + 8u;
}

/* The free area that contains [start, end), or 0. A block cut from its top
 * needs a size word below `start`, and what is left of the area its two. */
static inline uint32_t cp_heap_area_over(const cp_heap_os *os, uint32_t start,
	uint32_t end)
{
	uint32_t node = os->read32(os->head);
	unsigned guard;

	for(guard = 0; node && guard < CP_HEAP_NODES; guard++) {
		uint32_t top = node + 4u + os->read32(node);

		if(top >= end && node + 12u <= start)
			return node;
		node = os->read32(node + 4u);
	}
	return 0;
}

/* Take whole every free area before `stop` (all of them if 0) that could
 * serve a request of `size`, so that the request goes past them. Each is
 * asked for at its exact size, which the area itself is the first to match:
 * the ones before it that were left alone are too small for `size`, so for
 * anything this large too. */
static inline unsigned cp_heap_take_blockers(const cp_heap_os *os,
	uint32_t size, uint32_t stop, uint32_t *blockers)
{
	unsigned count = 0;

	while(count < CP_HEAP_BLOCKERS) {
		uint32_t node = os->read32(os->head);
		unsigned guard;

		for(guard = 0; node && node != stop && guard < CP_HEAP_NODES;
				guard++) {
			if(cp_heap_fits(os->read32(node), size))
				break;
			node = os->read32(node + 4u);
		}
		if(!node || node == stop || guard == CP_HEAP_NODES)
			break;
		blockers[count++] = os->alloc(os->read32(node));
	}
	return count;
}

static inline void cp_heap_free_blockers(const cp_heap_os *os,
	uint32_t *blockers, unsigned count)
{
	while(count > 0)
		os->release(blockers[--count]);
}

/* One block that contains [start, end). `grow_to` is where the break is
 * raised to when no free area contains the range yet (a fresh heap): past
 * `end`, so that small blocks taken off the top before the next start still
 * leave an area that contains it. The break moves in whole units, so it
 * may end up above `grow_to`, or below it when the heap ends first. */
static inline int cp_heap_reserve(const cp_heap_os *os, uint32_t start,
	uint32_t end, uint32_t grow_to, cp_heap_result *result)
{
	uint32_t blockers[CP_HEAP_BLOCKERS];
	uint32_t brk = os->read32(os->brk);
	uint32_t node = cp_heap_area_over(os, start, end);
	uint32_t size, block;
	unsigned count;

	result->block = 0;
	result->size = 0;
	result->brk = brk;

	if(!node) {
		uint32_t grow, top;

		if(brk >= end || brk >= grow_to || brk + 12u > start)
			return CP_HEAP_BLOCKED;
		grow = (grow_to - brk + 3u) & ~3u;
		top = brk + ((grow + 3u) / os->unit + 1u) * os->unit;
		if(top > os->limit) {
			/* A coarse unit overshoots: go as far as the heap allows. */
			uint32_t units = (os->limit - brk) / os->unit;

			if(units == 0)
				return CP_HEAP_BLOCKED;
			grow = (units - 1u) * os->unit + 4u;
			top = brk + units * os->unit;
		}
		if(top < end)
			return CP_HEAP_BLOCKED;
		count = cp_heap_take_blockers(os, grow, 0, blockers);
		os->release(os->alloc(grow));
		cp_heap_free_blockers(os, blockers, count);
		node = cp_heap_area_over(os, start, end);
		if(!node)
			return CP_HEAP_BLOCKED;
	}

	/* Everything from `start` to the end of that area. */
	size = node + 4u + os->read32(node) - start;
	count = cp_heap_take_blockers(os, size, node, blockers);
	block = os->alloc(size);
	cp_heap_free_blockers(os, blockers, count);
	result->block = block;
	result->size = size;
	if(block == 0 || block > start || block + size < end) {
		if(block)
			os->release(block);
		return CP_HEAP_SURPRISE;
	}
	return CP_HEAP_OK;
}

#endif /* CGBA_CLASSPAD_HEAP_H */
