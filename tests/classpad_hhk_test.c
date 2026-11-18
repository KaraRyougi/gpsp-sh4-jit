/* Host test for ports/fxcg100/classpad_hhk.h: looking File_GetAddr up in
 * HollyHock-3's symbol table, and the check of the code found there.
 *
 *   classpad_hhk_test              synthetic tables and OS images only
 *   classpad_hhk_test NOR.bin      also check a real fx-CP400 flash dump (OS
 *                                  image at offset 0 = 0x80000000) at the
 *                                  known File_GetAddr address */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/classpad_hhk.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

/* ---- symbol table --------------------------------------------------------- */

struct table {
	char data[512];
	uint32_t len;
};

static void table_str(struct table *t, const char *s)
{
	size_t n = strlen(s) + 1;

	memcpy(t->data + t->len, s, n);
	t->len += (uint32_t)n;
}

static void table_add(struct table *t, const char *name, uint32_t address)
{
	uint8_t *p;

	table_str(t, name);
	p = (uint8_t *)t->data + t->len;
	p[0] = (uint8_t)(address >> 24); p[1] = (uint8_t)(address >> 16);
	p[2] = (uint8_t)(address >> 8); p[3] = (uint8_t)address;
	t->len += 4;
}

#define GUARD "2025.0422.1323"

static void test_table(void)
{
	struct table t = { {0}, 0 };
	uint32_t full;

	table_str(&t, GUARD);
	table_add(&t, "File_Open", 0x80058a1c);
	table_add(&t, "File_GetAddrX", 0x11111111);
	table_add(&t, "File_GetAddr", 0x80058c88);
	table_add(&t, "File_Get", 0x22222222);
	table_add(&t, "File_Lseek", 0x80058b6c);
	full = t.len;

	CHECK(cp_hhk_symbol(t.data, t.len, GUARD, "File_GetAddr") == 0x80058c88,
		"File_GetAddr not found among names sharing its prefix");
	CHECK(cp_hhk_symbol(t.data, t.len, GUARD, "File_Open") == 0x80058a1c,
		"first entry not found");
	CHECK(cp_hhk_symbol(t.data, t.len, GUARD, "File_Lseek") == 0x80058b6c,
		"last entry not found");
	CHECK(cp_hhk_symbol(t.data, t.len, GUARD, "File_Read") == 0,
		"absent name found");
	CHECK(cp_hhk_symbol(t.data, t.len, "2023.0209.1500", "File_GetAddr") == 0,
		"table for another OS accepted");
	CHECK(cp_hhk_symbol(t.data, t.len, "2025.0422.132", "File_GetAddr") == 0,
		"guard prefix accepted");

	/* An override appended by the launcher wins, as in gint. */
	table_add(&t, "File_GetAddr", 0x80123456);
	CHECK(cp_hhk_symbol(t.data, t.len, GUARD, "File_GetAddr") == 0x80123456,
		"later entry does not override");

	/* Truncated anywhere in the last entry: rejected, not read past. */
	for (uint32_t cut = full + 1; cut < t.len; cut++)
		CHECK(cp_hhk_symbol(t.data, cut, GUARD, "File_GetAddr") == 0,
			"table cut at %u accepted", cut);
	CHECK(cp_hhk_symbol(t.data, 0, GUARD, "File_GetAddr") == 0,
		"empty table accepted");
	CHECK(cp_hhk_symbol(t.data, 5, GUARD, "File_GetAddr") == 0,
		"table shorter than its guard accepted");
	CHECK(cp_hhk_symbol(NULL, t.len, GUARD, "File_GetAddr") == 0,
		"NULL table accepted");
}

static void test_env(void)
{
	char *env[] = {
		"PATH=x",
		"HHK_SYMBOL_TABLE_LEN=000004d2",
		"HHK_SYMBOL_TABLE=8cfe9a40",
		"HHK_OTHER=0x10",
		"BAD=12g4",
		"LONG=123456789",
		"EMPTY=",
		NULL
	};

	CHECK(cp_hhk_env_hex(env, "HHK_SYMBOL_TABLE") == 0x8cfe9a40,
		"table pointer");
	CHECK(cp_hhk_env_hex(env, "HHK_SYMBOL_TABLE_LEN") == 0x4d2,
		"table length");
	CHECK(cp_hhk_env_hex(env, "HHK_OTHER") == 0x10, "0x prefix");
	CHECK(cp_hhk_env_hex(env, "HHK_SYMBOL") == 0, "key prefix matched");
	CHECK(cp_hhk_env_hex(env, "BAD") == 0, "non-hex accepted");
	CHECK(cp_hhk_env_hex(env, "LONG") == 0, "more than 32 bits accepted");
	CHECK(cp_hhk_env_hex(env, "EMPTY") == 0, "empty value accepted");
	CHECK(cp_hhk_env_hex(env, "MISSING") == 0, "absent key found");
	CHECK(cp_hhk_env_hex(NULL, "HHK_SYMBOL_TABLE") == 0, "NULL envp");
}

/* ---- the File_GetAddr check ----------------------------------------------- */

struct os_image {
	uint8_t *data;
	uint32_t base, size;
};

static const uint8_t *image_view(void *ctx, uint32_t address, uint32_t size)
{
	const struct os_image *img = ctx;

	if (address < img->base || address - img->base > img->size ||
			size > img->size - (address - img->base))
		return NULL;
	return img->data + (address - img->base);
}

/* The three routines as in OS 02.01.7003. */
static const uint8_t entry_code[46] = {
	0x4f,0x22, 0x7f,0xec, 0x1f,0x62, 0x1f,0x51, 0x2f,0x42, 0x04,0x02,
	0xd1,0x12, 0x1f,0x43, 0x24,0x1b, 0x44,0x0e, 0xd7,0x24, 0x64,0xf2,
	0x55,0xf1, 0x47,0x0b, 0x56,0xf2, 0x54,0xf3, 0x1f,0x04, 0x44,0x0e,
	0x50,0xf4, 0x7f,0x14, 0x4f,0x26, 0x00,0x0b, 0x00,0x09
};
static const uint8_t forward_code[52] = {
	0x4f,0x22, 0x7f,0xf0, 0x26,0x68, 0x1f,0x62, 0x1f,0x51, 0xe0,0xfe,
	0x8d,0x12, 0x2f,0x42, 0xbb,0x51, 0x00,0x09, 0x20,0x08, 0x8d,0x0d,
	0xe0,0xee, 0xbb,0x09, 0x64,0xf2, 0x1f,0x03, 0xbb,0x0d, 0x64,0xf2,
	0xd1,0x17, 0x65,0x03, 0x56,0xf1, 0x57,0xf2, 0x54,0xf3, 0x7f,0x10,
	0x41,0x2b, 0x4f,0x26
};
static const uint8_t core_code[24] = {
	0x75,0xfe, 0x56,0xf4, 0x52,0x42, 0x57,0x47, 0x05,0x27, 0x05,0x1a,
	0x51,0x4d, 0x35,0x6c, 0x56,0xf1, 0x35,0x7c, 0x57,0xf8, 0x35,0x1c
};

static uint8_t *at(struct os_image *img, uint32_t address)
{
	return img->data + (address - img->base);
}

/* Store `value` where the mov.l @(disp,PC) at `insn` reads it. */
static void put_literal(struct os_image *img, uint32_t insn, uint32_t value)
{
	uint8_t *p = at(img, (insn & ~3u) + 4u + (uint32_t)at(img, insn)[1] * 4u);

	p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static void build(struct os_image *img, uint32_t entry, uint32_t forward,
	uint32_t core)
{
	memset(img->data, 0x09, img->size);
	memcpy(at(img, entry), entry_code, sizeof(entry_code));
	put_literal(img, entry + 12, 0x100000f0);
	put_literal(img, entry + 20, forward);
	memcpy(at(img, forward), forward_code, sizeof(forward_code));
	put_literal(img, forward + 0x24, core);
	memcpy(at(img, core + 0x94), core_code, sizeof(core_code));
}

static void test_check(void)
{
	static uint8_t mem[0x20000];
	struct os_image img = { mem, 0x80040000, sizeof(mem) };
	const uint32_t entry = 0x80058c88, forward = 0x8005876a,
		core = 0x80047c18;

	/* As laid out in OS 02.01.7003. */
	build(&img, entry, forward, core);
	CHECK(cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"7003 layout rejected");
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry + 2),
		"wrong entry accepted");
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry + 1),
		"odd entry accepted");
	CHECK(!cp_hhk_file_getaddr_ok(NULL, &img, entry), "NULL view accepted");

	/* Each piece changed in turn. */
	at(&img, entry + 30)[1] ^= 1;
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"changed entry code accepted");
	build(&img, entry, forward, core);
	put_literal(&img, entry + 12, 0x000000f0);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"call without BL accepted");
	build(&img, entry, forward, core);
	at(&img, forward + 0x2e)[0] ^= 0x10;
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"changed forwarding code accepted");
	build(&img, entry, forward, core);
	at(&img, core + 0x94 + 23)[0] ^= 1;
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"changed core arithmetic accepted");
	build(&img, entry, forward, core);
	put_literal(&img, entry + 20, forward + 4);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"entry leading elsewhere accepted");
	build(&img, entry, forward, core);
	put_literal(&img, entry + 20, 0x90000000);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"forward outside the image accepted");
	build(&img, entry, forward, core);
	put_literal(&img, forward + 0x24, 0x8005ffff);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"odd core address accepted");

	/* The same code elsewhere, with each routine at the other halfword
	 * alignment, so every literal sits at a different distance. */
	build(&img, 0x8004a002, 0x80051000, 0x8005c002);
	CHECK(cp_hhk_file_getaddr_ok(image_view, &img, 0x8004a002),
		"relocated routines rejected");
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, entry),
		"old address accepted after relocation");
}

static void test_dump(const char *path)
{
	FILE *f = fopen(path, "rb");
	static uint8_t data[0x2000000];
	struct os_image img = { data, 0x80000000, 0 };

	CHECK(f != NULL, "cannot open %s", path);
	if (!f)
		return;
	img.size = (uint32_t)fread(data, 1, sizeof(data), f);
	fclose(f);
	CHECK(cp_hhk_file_getaddr_ok(image_view, &img, CP_FILE_GETADDR_KNOWN),
		"%s: File_GetAddr at %08x rejected", path, CP_FILE_GETADDR_KNOWN);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, 0x80058a1c),
		"%s: File_Open accepted as File_GetAddr", path);
	printf("%s: guard \"%.15s\", File_GetAddr at %08x checked\n", path,
		(const char *)data + 0x14fffe0, CP_FILE_GETADDR_KNOWN);
}

int main(int argc, char **argv)
{
	test_table();
	test_env();
	test_check();
	if (argc == 2)
		test_dump(argv[1]);

	if (failures) {
		printf("classpad_hhk_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("classpad_hhk_test: all checks passed\n");
	return 0;
}
