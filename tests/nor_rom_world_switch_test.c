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

static int fake_open(const uint16_t *path, int mode);
static int fake_size(int fd);
static int fake_close(int fd);
static int fake_get_block_address(int fd, int offset, void **address);

#define CGBA_HOST_TEST 1
#define CGBA_FXCG100_STORAGE 1
#define CGBA_BFILE_OPEN              fake_open
#define CGBA_BFILE_SIZE              fake_size
#define CGBA_BFILE_READ              ((uintptr_t)3u)
#define CGBA_BFILE_CLOSE             fake_close
#define CGBA_BFILE_GET_BLOCK_ADDRESS fake_get_block_address
#define CGBA_BFILE_FIND_FIRST        ((uintptr_t)5u)
#define CGBA_BFILE_FIND_NEXT         ((uintptr_t)6u)
#define CGBA_BFILE_FIND_CLOSE        ((uintptr_t)7u)
#include "ports/fxcg100/gint-gpsp/src/nor_rom.c"

static int failures;
static uint32_t world_switches;
static uint32_t overlay_notes;
static uint32_t block_calls;
static uint32_t open_calls;
static uint32_t size_calls;
static uint32_t close_calls;
static int expected_fd;
static int reopen_fd;
static int fake_file_size;
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

static int fake_open(const uint16_t *path, int mode)
{
	(void)path;
	(void)mode;
	open_calls++;
	expected_fd = reopen_fd;
	return reopen_fd;
}

static int fake_size(int fd)
{
	size_calls++;
	if(fd != expected_fd)
		bad_offset = 1;
	return fake_file_size;
}

static int fake_close(int fd)
{
	close_calls++;
	if(fd != expected_fd)
		bad_offset = 1;
	return 0;
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
	open_calls = 0;
	size_calls = 0;
	close_calls = 0;
	expected_fd = fd;
	reopen_fd = fd + 10;
	fake_file_size = 0;
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

static void test_refresh_batches_reopen_size_and_blocks(void)
{
	static const uint16_t path[] = { '\\', '\\', 'f', 'l', 's', '0', '\\',
		'G', 'A', 'M', 'E', '.', 'G', 'B', 'A', 0 };
	cgba_block_query query;
	uint32_t blocks = 16u;
	int result;

	reset_probe(5, -1);
	fake_file_size = (int)(blocks * CGBA_FLASH_BLOCK_SIZE);
	result = os_bfile_get_block_addresses_ex(5, path,
		(uint32_t)fake_file_size, 1, blocks, &query);

	CHECK(result == 0, "batched refresh returned %d", result);
	CHECK(world_switches == 1,
		"batched refresh used %lu world switches, want 1",
		(unsigned long)world_switches);
	CHECK(close_calls == 1 && open_calls == 1 && size_calls == 1,
		"expected 1 close/open/size, got %lu/%lu/%lu",
		(unsigned long)close_calls, (unsigned long)open_calls,
		(unsigned long)size_calls);
	CHECK(query.fd == 15 && block_calls == blocks && !bad_offset,
		"refresh did not query blocks on reopened fd (%d, %lu, %d)",
		query.fd, (unsigned long)block_calls, bad_offset);
}

int main(void)
{
	test_large_rom_uses_one_world_switch();
	test_batch_retains_first_failure();
	test_refresh_batches_reopen_size_and_blocks();

	if(failures) {
		printf("nor_rom_world_switch_test: %d failure(s)\n", failures);
		return 1;
	}

	puts("NOR block queries use one OS world switch");
	return 0;
}
