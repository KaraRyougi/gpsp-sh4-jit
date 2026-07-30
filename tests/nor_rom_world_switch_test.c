/*
 * Regression for calculator RTC drift while mapping large GBA ROMs.
 *
 * The production NOR mapper is included directly so the test can call its
 * internal batched BFile helper and count gint world switches. On hardware,
 * each switch changes the CPG twice when gpSP is overclocked; keeping the whole
 * block-address scan in one switch prevents thousands of accumulated RTC gaps.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fake_get_block_address(int fd, int offset, void **address);

#define CGBA_HOST_TEST 1
#define CGBA_FXCG100_STORAGE 1
#define CGBA_BFILE_OPEN              ((uintptr_t)1u)
#define CGBA_BFILE_SIZE              ((uintptr_t)2u)
#define CGBA_BFILE_READ              ((uintptr_t)3u)
#define CGBA_BFILE_CLOSE             ((uintptr_t)4u)
#define CGBA_BFILE_GET_BLOCK_ADDRESS fake_get_block_address
#define CGBA_BFILE_FIND_FIRST        ((uintptr_t)5u)
#define CGBA_BFILE_FIND_NEXT         ((uintptr_t)6u)
#define CGBA_BFILE_FIND_CLOSE        ((uintptr_t)7u)
#include "ports/fxcg100/gint-gpsp/src/nor_rom.c"

static int failures;
static uint32_t world_switches;
static uint32_t overlay_notes;
static uint32_t block_calls;
static int expected_fd;
static int failed_block;
static int bad_offset;

#define CHECK(condition, ...) do { \
	if(!(condition)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		putchar('\n'); \
	} \
} while(0)

int gint_world_switch(gint_call_t call)
{
	int (*function)(void *) = (int (*)(void *))call.function;

	world_switches++;
	return function((void *)call.args[0]);
}

void fxcg100_lcd_note_os_activity(void)
{
	overlay_notes++;
}

static int fake_get_block_address(int fd, int offset, void **address)
{
	uint32_t block = block_calls++;

	if(fd != expected_fd ||
			offset != (int)(block * CGBA_FLASH_BLOCK_SIZE))
		bad_offset = 1;
	if((int)block == failed_block) {
		*address = NULL;
		return -77;
	}

	*address = (void *)(uintptr_t)(0xa1000000u +
		block * CGBA_FLASH_BLOCK_SIZE);
	return 0;
}

static void reset_probe(int fd, int fail_at)
{
	world_switches = 0;
	overlay_notes = 0;
	block_calls = 0;
	expected_fd = fd;
	failed_block = fail_at;
	bad_offset = 0;
	memset(cgba_block_addr, 0, sizeof(cgba_block_addr));
}

static void test_large_rom_uses_one_world_switch(void)
{
	cgba_block_query query;
	uint32_t blocks = 32u * 1024u * 1024u / CGBA_FLASH_BLOCK_SIZE;
	int result;

	reset_probe(42, -1);
	result = os_bfile_get_block_addresses(expected_fd, blocks, &query);

	CHECK(result == 0, "batch callback returned %d", result);
	CHECK(world_switches == 1,
		"32 MiB scan used %lu world switches, want 1",
		(unsigned long)world_switches);
	CHECK(overlay_notes == 1,
		"32 MiB scan noted %lu OS overlays, want 1",
		(unsigned long)overlay_notes);
	CHECK(block_calls == blocks,
		"32 MiB scan made %lu block calls, want %lu",
		(unsigned long)block_calls, (unsigned long)blocks);
	CHECK(!bad_offset, "batch used a wrong descriptor or block offset");
	CHECK(query.first_result == 0 &&
			query.first_failed_block == UINT32_MAX,
		"successful batch diagnostics were %d/%lu",
		query.first_result, (unsigned long)query.first_failed_block);
	CHECK((uintptr_t)cgba_block_addr[0] == 0xa1000000u &&
			(uintptr_t)cgba_block_addr[blocks - 1u] ==
				0xa1000000u + (blocks - 1u) * CGBA_FLASH_BLOCK_SIZE,
		"batch did not retain the first/last raw NOR addresses");
}

static void test_batch_retains_first_failure(void)
{
	cgba_block_query query;
	int result;

	reset_probe(7, 5);
	result = os_bfile_get_block_addresses(expected_fd, 12, &query);

	CHECK(result == 0, "failure-tolerant batch returned %d", result);
	CHECK(world_switches == 1 && block_calls == 12,
		"failed block split the batch (%lu switches, %lu calls)",
		(unsigned long)world_switches, (unsigned long)block_calls);
	CHECK(query.first_failed_block == 5 &&
			query.first_failed_result == -77,
		"first failure diagnostics were block %lu/result %d",
		(unsigned long)query.first_failed_block,
		query.first_failed_result);
	CHECK(cgba_block_addr[5] == NULL &&
			cgba_block_addr[6] != NULL,
		"failed block stopped or polluted the remaining scan");
}

int main(void)
{
	test_large_rom_uses_one_world_switch();
	test_batch_retains_first_failure();

	if(failures) {
		printf("nor_rom_world_switch_test: %d failure(s)\n", failures);
		return 1;
	}

	puts("NOR block queries use one OS world switch");
	return 0;
}
