#include <gint/bfile.h>
#include <gint/cpu.h>
#include <gint/display.h>
#include <gint/gint.h>
#include <gint/hardware.h>
#include <gint/keyboard.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PROBE_START 0x8c6f0000u
#define PROBE_END 0x8c800000u
#define PAGE_SIZE 4096u
#define PAGE_WORDS (PAGE_SIZE / sizeof(uint32_t))
#define RESULT_SIZE 2048u

typedef struct probe_result {
	uint32_t stack_address;
	uint32_t vbr_address;
	uint32_t pages_target;
	uint32_t pages_tested;
	uint32_t first_failed;
	uint32_t write_errors;
	uint32_t restore_errors;
	uint32_t original_hash;
	uint32_t restored_hash;
	int refused;
	int saved;
	char output_name[20];
} probe_result;

static uint32_t page_backup[PAGE_WORDS] __attribute__((aligned(32)));
static char result_text[RESULT_SIZE] __attribute__((aligned(4)));

static uint32_t read_stack_pointer(void)
{
	uint32_t value;
	__asm__ volatile("mov r15,%0" : "=r"(value));
	return value;
}

static uint32_t read_vbr(void)
{
	uint32_t value;
	__asm__ volatile("stc vbr,%0" : "=r"(value));
	return value;
}

static int in_probe_range(uint32_t address)
{
	return address >= PROBE_START && address < PROBE_END;
}

static uint32_t fnv1a_words(const volatile uint32_t *words)
{
	uint32_t hash = 2166136261u;
	for(unsigned i = 0; i < PAGE_WORDS; i++) {
		uint32_t value = words[i];
		for(unsigned byte = 0; byte < 4u; byte++) {
			hash ^= value >> 24;
			hash *= 16777619u;
			value <<= 8;
		}
	}
	return hash;
}

static void writeback_p1_page(uintptr_t p1)
{
	for(uintptr_t line = p1; line < p1 + PAGE_SIZE; line += 32u)
		__asm__ volatile("ocbwb @%0" :: "r"(line) : "memory");
	__asm__ volatile("synco" ::: "memory");
}

static void invalidate_p1_page(uintptr_t p1)
{
	for(uintptr_t line = p1; line < p1 + PAGE_SIZE; line += 32u) {
		__asm__ volatile("ocbi @%0" :: "r"(line) : "memory");
		__asm__ volatile("icbi @%0" :: "r"(line) : "memory");
	}
	__asm__ volatile("synco" ::: "memory");
}

static uint32_t test_pattern(uintptr_t address, unsigned index)
{
	uint32_t x = (uint32_t)address ^ ((uint32_t)index * 0x9e3779b9u);
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x ^ 0xa55a3cc3u;
}

static int probe_page(uintptr_t p1, probe_result *result)
{
	volatile uint32_t *p2 = (volatile uint32_t *)(p1 | 0x20000000u);
	uint32_t write_errors = 0;
	uint32_t restore_errors = 0;

	cpu_atomic_start();
	writeback_p1_page(p1);
	for(unsigned i = 0; i < PAGE_WORDS; i++)
		page_backup[i] = p2[i];
	uint32_t original_hash = fnv1a_words(page_backup);

	for(unsigned i = 0; i < PAGE_WORDS; i++)
		p2[i] = test_pattern(p1, i);
	__asm__ volatile("synco" ::: "memory");
	for(unsigned i = 0; i < PAGE_WORDS; i++) {
		if(p2[i] != test_pattern(p1, i))
			write_errors++;
	}

	for(unsigned i = 0; i < PAGE_WORDS; i++)
		p2[i] = page_backup[i];
	__asm__ volatile("synco" ::: "memory");
	uint32_t restored_hash = fnv1a_words(p2);
	for(unsigned i = 0; i < PAGE_WORDS; i++) {
		if(p2[i] != page_backup[i])
			restore_errors++;
	}
	invalidate_p1_page(p1);
	cpu_atomic_end();

	result->original_hash ^= original_hash + (uint32_t)p1;
	result->restored_hash ^= restored_hash + (uint32_t)p1;
	result->write_errors += write_errors;
	result->restore_errors += restore_errors;
	return write_errors == 0 && restore_errors == 0 &&
		original_hash == restored_hash;
}

static void draw_progress(uint32_t address, uint32_t page, uint32_t total)
{
	char line[64];
	dclear(C_WHITE);
	dtext(8, 8, C_BLACK, "CG50 upper-RAM probe");
	(void)snprintf(line, sizeof(line), "Page %lu / %lu",
		(unsigned long)page, (unsigned long)total);
	dtext(8, 48, C_BLACK, line);
	(void)snprintf(line, sizeof(line), "Address: %08lX",
		(unsigned long)address);
	dtext(8, 72, C_BLACK, line);
	dtext(8, 112, C_BLACK, "Each page is immediately restored.");
	dupdate();
}

static void ascii_path(const char *name, uint16_t *path, size_t count)
{
	static const char prefix[] = "\\\\fls0\\";
	size_t out = 0;
	for(size_t i = 0; prefix[i] && out + 1u < count; i++)
		path[out++] = (uint16_t)(unsigned char)prefix[i];
	for(size_t i = 0; name[i] && out + 1u < count; i++)
		path[out++] = (uint16_t)(unsigned char)name[i];
	path[out] = 0;
}

static int os_call(gint_call_t call)
{
	return gint_world_switch(call);
}

static int save_result(probe_result *result)
{
	uint16_t path[32];
	for(unsigned index = 0; index < 100u; index++) {
		(void)snprintf(result->output_name, sizeof(result->output_name),
			"RAMPRB%02u.TXT", index);
		ascii_path(result->output_name, path, 32u);
		int existing = os_call(GINT_CALL(BFile_Open, path, BFile_ReadOnly));
		if(existing >= 0) {
			(void)os_call(GINT_CALL(BFile_Close, existing));
			continue;
		}
		break;
	}

	int length = snprintf(result_text, sizeof(result_text),
		"CG50 upper-RAM probe\n"
		"format_version=1\n"
		"build_id=%s\n"
		"range.start=0x%08lX\n"
		"range.end=0x%08lX\n"
		"range.page_size=%u\n"
		"range.pages_target=%lu\n"
		"range.pages_tested=%lu\n"
		"range.first_failed=0x%08lX\n"
		"range.write_errors=%lu\n"
		"range.restore_errors=%lu\n"
		"range.original_hash=0x%08lX\n"
		"range.restored_hash=0x%08lX\n"
		"guard.refused=%d\n"
		"cpu.stack=0x%08lX\n"
		"cpu.vbr=0x%08lX\n"
		"hw.calc=%lu\n"
		"hw.ram=%lu\n",
		RAMPROBE_BUILD_ID,
		(unsigned long)PROBE_START, (unsigned long)PROBE_END, PAGE_SIZE,
		(unsigned long)result->pages_target,
		(unsigned long)result->pages_tested,
		(unsigned long)result->first_failed,
		(unsigned long)result->write_errors,
		(unsigned long)result->restore_errors,
		(unsigned long)result->original_hash,
		(unsigned long)result->restored_hash,
		result->refused,
		(unsigned long)result->stack_address,
		(unsigned long)result->vbr_address,
		(unsigned long)gint[HWCALC],
		(unsigned long)gint[HWRAM]);
	if(length < 0 || (size_t)length >= sizeof(result_text))
		return 0;
	if(length & 1)
		result_text[length++] = '\n';

	int create_size = length;
	if(os_call(GINT_CALL(BFile_Create, path, BFile_File, &create_size)) < 0)
		return 0;
	int fd = os_call(GINT_CALL(BFile_Open, path, BFile_WriteOnly));
	if(fd < 0)
		return 0;
	int wrote = os_call(GINT_CALL(BFile_Write, fd, result_text, length));
	(void)os_call(GINT_CALL(BFile_Close, fd));
	return wrote >= 0;
}

int main(void)
{
	probe_result result;
	memset(&result, 0, sizeof(result));
	result.stack_address = read_stack_pointer();
	result.vbr_address = read_vbr();
	result.pages_target = (PROBE_END - PROBE_START) / PAGE_SIZE;

	dclear(C_WHITE);
	dtext(8, 8, C_BLACK, "CG50 upper-RAM probe");
	dtext(8, 42, C_BLACK, "Tests 1088 KiB above the proven arena.");
	dtext(8, 66, C_BLACK, "A reset is possible on unknown OS layouts.");
	dtext(8, 104, C_BLACK, "EXE: begin   EXIT: cancel");
	dupdate();

	key_event_t event;
	do {
		event = getkey();
		if(event.key == KEY_EXIT)
			return 1;
	} while(event.key != KEY_EXE);

	if(gint[HWCALC] != 5u || gint[HWRAM] < 8u * 1024u * 1024u ||
			in_probe_range(result.stack_address) ||
			in_probe_range(result.vbr_address)) {
		result.refused = 1;
	}
	else {
		for(uint32_t address = PROBE_START; address < PROBE_END;
				address += PAGE_SIZE) {
			uint32_t page = (address - PROBE_START) / PAGE_SIZE;
			if((page & 15u) == 0u)
				draw_progress(address, page, result.pages_target);
			if(!probe_page(address, &result)) {
				result.first_failed = address;
				break;
			}
			result.pages_tested++;
		}
	}

	result.saved = save_result(&result);
	dclear(C_WHITE);
	dtext(8, 8, C_BLACK, "CG50 upper-RAM probe complete");
	char line[64];
	(void)snprintf(line, sizeof(line), "Pages: %lu / %lu",
		(unsigned long)result.pages_tested,
		(unsigned long)result.pages_target);
	dtext(8, 48, C_BLACK, line);
	(void)snprintf(line, sizeof(line), "Write/restore errors: %lu / %lu",
		(unsigned long)result.write_errors,
		(unsigned long)result.restore_errors);
	dtext(8, 72, C_BLACK, line);
	dtext(8, 104, C_BLACK,
		result.saved ? result.output_name : "Result file save failed");
	dtext(8, 144, C_BLACK, "EXIT returns to the OS.");
	dupdate();
	do event = getkey(); while(event.key != KEY_EXIT);
	return 1;
}
