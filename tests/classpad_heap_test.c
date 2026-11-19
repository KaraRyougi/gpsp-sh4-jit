/* The arena reservation of classpad_heap.h against a model of the fx-CP400
 * OS allocator, written from the disassembly of malloc 0x800a8744, free
 * 0x800a86ac and sbrk 0x800cee9e (OS 02.01.7003).
 *
 * The first two launches are also checked against numbers obtained by
 * running the same steps on the real OS in casio-emu (Mem_Malloc and
 * Mem_Free called in the guest): the model is only worth anything if it
 * puts blocks where the OS does. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/classpad_heap.h"

#define CP_HEAP_HEAD  0x8c1a35f0u
#define CP_HEAP_BREAK 0x8c2233ccu
#define CP_HEAP_LIMIT 0x8ce80000u
#define CP_HEAP_UNIT  0x400u
static uint32_t model_read32(uint32_t address);
static uint32_t model_malloc(uint32_t size);
static void model_free(uint32_t address);
static cp_heap_os os = { CP_HEAP_HEAD, CP_HEAP_BREAK, CP_HEAP_LIMIT,
	CP_HEAP_UNIT, model_read32, model_malloc, model_free };

#define BASE  0x8c280000u
#define WORDS ((CP_HEAP_LIMIT - BASE) / 4u)

static uint32_t mem[WORDS];
static uint32_t head, brk;
static int dialogs;          /* requests the OS would have refused */
static int failures;

#define CHECK(cond, ...) do { if(!(cond)) { \
	printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
	printf("\n"); failures++; } } while(0)

static uint32_t *at(uint32_t address)
{
	if(address < BASE || address >= CP_HEAP_LIMIT || (address & 3u)) {
		printf("bad heap address %08x\n", (unsigned)address);
		exit(2);
	}
	return &mem[(address - BASE) / 4u];
}

static uint32_t model_read32(uint32_t address)
{
	if(address == CP_HEAP_HEAD)
		return head;
	if(address == CP_HEAP_BREAK)
		return brk;
	return *at(address);
}

static void model_free(uint32_t address)
{
	uint32_t block, end, prev = 0, next;

	if(!address)
		return;
	block = address - 4u;
	end = block + 4u + *at(block);
	for(next = head; next && next < block; next = *at(next + 4u))
		prev = next;
	if(next && end == next) {                 /* join the area above */
		*at(block) += 4u + *at(next);
		*at(block + 4u) = *at(next + 4u);
	} else {
		*at(block + 4u) = next;
	}
	if(prev && prev + 4u + *at(prev) == block) {   /* and the one below */
		*at(prev) += 4u + *at(block);
		*at(prev + 4u) = *at(block + 4u);
	} else if(prev) {
		*at(prev + 4u) = block;
	} else {
		head = block;
	}
}

static uint32_t model_malloc(uint32_t size)
{
	uint32_t want, node, prev;

	if(size == 0 || size > 0x7fffffffu)
		return 0;
	want = ((size - 1u) & ~3u) + 4u;
	for(;;) {
		prev = 0;
		for(node = head; node; prev = node, node = *at(node + 4u)) {
			uint32_t have = *at(node);

			if(have == want) {
				if(prev)
					*at(prev + 4u) = *at(node + 4u);
				else
					head = *at(node + 4u);
				return node + 4u;
			}
			if(have >= want + 8u) {
				uint32_t left = have - (want + 4u);

				*at(node) = left;
				node += left + 4u;
				*at(node) = want;
				return node + 4u;
			}
		}
		{
			uint32_t total = ((want + 3u) / os.unit + 1u) * os.unit;

			if(brk + total > CP_HEAP_LIMIT) {
				dialogs++;        /* "Insufficient System Memory to Run" */
				return 0;
			}
			node = brk;
			brk += total;
			*at(node) = total - 4u;
			if(total - 4u == want)
				return node + 4u;
			model_free(node + 4u);
		}
	}
}



#define ARENA    0x8c880000u
#define USED_END 0x8cde2a00u
#define GROW_TO  0x8ce40000u

static uint32_t seed;
static uint32_t rnd(uint32_t n)
{
	seed = seed * 1103515245u + 12345u;
	return (seed >> 8) % n;
}

/* As the OS is found at the main menu in casio-emu: one small free area at
 * the bottom of the heap, everything else below the break in use. */
static void fresh_heap(void)
{
	memset(mem, 0, sizeof(mem));
	head = BASE;
	*at(BASE) = 0x3f8u;
	*at(BASE + 4u) = 0;
	brk = 0x8c2d2c00u;
	dialogs = 0;
}

/* One run of gpSP: something small is on the heap before the arena is
 * reserved, the OS allocates and frees while the game runs and may keep a
 * block, then everything of ours is given back. */
static int launch(uint32_t pre_size, int may_keep, cp_heap_result *r)
{
	uint32_t pre = model_malloc(pre_size), tmp[12];
	unsigned count = 0, i;
	int status = cp_heap_reserve(&os, ARENA, USED_END, GROW_TO, r);

	if(status == CP_HEAP_OK) {
		for(i = 0; i < 12; i++) {
			tmp[count++] = model_malloc(0x400u + rnd(0x8000u));
			if(rnd(3) == 0) {
				unsigned k = rnd(count);

				model_free(tmp[k]);
				tmp[k] = tmp[--count];
			}
		}
		if(may_keep && rnd(2) && count)
			count--;                           /* kept by the OS */
		while(count > 0)
			model_free(tmp[--count]);
		model_free(r->block);
	}
	model_free(pre);
	return status;
}

int main(void)
{
	cp_heap_result r;
	unsigned run;
	uint32_t s;

	/* The two launches measured on the real OS (pre = 0x6800). */
	seed = 99;
	fresh_heap();
	CHECK(launch(0x6800u, 0, &r) == CP_HEAP_OK, "first launch refused");
	CHECK(r.block == 0x8c880000u && r.size == 0x005c0400u,
		"first launch: block %08x size %x, the OS gave 8c880000 5c0400",
		(unsigned)r.block, (unsigned)r.size);
	CHECK(launch(0x6800u, 0, &r) == CP_HEAP_OK, "second launch refused");
	CHECK(r.block == 0x8c880000u && r.size == 0x005b9bfcu,
		"second launch: block %08x size %x, the OS gave 8c880000 5b9bfc",
		(unsigned)r.block, (unsigned)r.size);
	CHECK(dialogs == 0, "a request was refused");

	/* Many launches, with blocks the OS keeps, and with more or less on the
	 * heap before gpSP gets to run. */
	for(s = 1; s <= 200; s++) {
		static const uint32_t pre[4] = { 0x100u, 0x6800u, 0x20000u, 0x52800u };

		seed = s;
		fresh_heap();
		for(run = 1; run <= 40; run++) {
			int status = launch(pre[s & 3u], 1, &r);

			if(status != CP_HEAP_OK || dialogs ||
					r.block > ARENA || r.block + r.size < USED_END) {
				CHECK(0, "seed %u run %u: status %d dialogs %d block %08x "
					"size %x brk %08x", (unsigned)s, run, status, dialogs,
					(unsigned)r.block, (unsigned)r.size, (unsigned)r.brk);
				break;
			}
		}
	}

	/* What the old code did on a second start, and what it cost: a request
	 * sized by trying is refused, and the OS puts up its dialog. */
	fresh_heap();
	CHECK(launch(0x6800u, 0, &r) == CP_HEAP_OK, "launch refused");
	CHECK(model_malloc(12u << 20) == 0 && dialogs == 1,
		"an oversized request must be the one thing the model refuses");

	/* Something else in the range: refused without asking for anything. */
	fresh_heap();
	brk = 0x8c900000u;
	CHECK(cp_heap_reserve(&os, ARENA, USED_END, GROW_TO, &r) ==
		CP_HEAP_BLOCKED && dialogs == 0, "a break inside the arena must refuse");

	/* After a version that raised the break less far. If what it left free
	 * still contains the range below what is held at its top, that is used
	 * (the build before this one: break at 0x8ce00400). If not (the first
	 * build: 0x8cdc0400), the start is
	 * refused without asking for anything, and the heap has to be made
	 * afresh by the OS. */
	{
		static const uint32_t old_top[2] = { 0x8ce00000u, 0x8cdc0000u };
		static const int want[2] = { CP_HEAP_OK, CP_HEAP_BLOCKED };
		unsigned v;

		for(v = 0; v < 2; v++) {
			uint32_t pre;

			seed = 5;
			fresh_heap();
			pre = model_malloc(0x6800u);
			CHECK(cp_heap_reserve(&os, ARENA, 0x8ccde9a0u, old_top[v], &r) ==
				CP_HEAP_OK, "the older version's launch refused");
			model_free(r.block);
			model_free(pre);
			CHECK(launch(0x6800u, 0, &r) == want[v] && dialogs == 0,
				"after an older version (%08x): dialogs %d block %08x",
				(unsigned)old_top[v], dialogs, (unsigned)r.block);
		}
	}

	/* A unit so coarse that rounding the wanted break up would pass the end
	 * of the heap: the break goes as far as it can instead, and later
	 * starts still find the arena. */
	{
		static const uint32_t units[3] = { 0x10000u, 0x40000u, 0x80000u };
		unsigned u;

		for(u = 0; u < 3; u++) {
			os.unit = units[u];
			seed = 7;
			fresh_heap();
			for(run = 1; run <= 10; run++) {
				int status = launch(0x6800u, 1, &r);

				CHECK(status == CP_HEAP_OK && dialogs == 0 &&
					r.block <= ARENA && r.block + r.size >= USED_END,
					"unit %x run %u: status %d dialogs %d block %08x size %x",
					(unsigned)units[u], run, status, dialogs,
					(unsigned)r.block, (unsigned)r.size);
			}
		}
		os.unit = CP_HEAP_UNIT;
	}

	if(failures)
		return 1;
	printf("classpad_heap_test: all checks passed\n");
	return 0;
}
