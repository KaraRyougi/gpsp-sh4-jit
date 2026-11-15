/* Host test for ports/fxcg100/classpad_elf.h: finding the RAM-resident core
 * section inside the fx-CP400 .hh3 by its link address.
 *
 *   classpad_elf_test                       synthetic images only
 *   classpad_elf_test gpSP.hh3 ADDR SIZE    also check a real build: the
 *                                           section must exist and start with
 *                                           the "gpSP core" stamp
 * (ADDR/SIZE in hex, from `nm`: _cgba_core_start and end - start.) */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/classpad_elf.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

struct image {
	const uint8_t *data;
	uint32_t size;
};

static int image_read(void *ctx, void *dst, uint32_t offset, uint32_t size)
{
	const struct image *img = ctx;

	if (offset > img->size || size > img->size - offset)
		return 0;
	memcpy(dst, img->data + offset, size);
	return 1;
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static void add_section(uint8_t *shdr, uint32_t type, uint32_t addr,
	uint32_t offset, uint32_t size)
{
	put32(shdr + 4, type);
	put32(shdr + 12, addr);
	put32(shdr + 16, offset);
	put32(shdr + 20, size);
}

static void test_synthetic(void)
{
	uint8_t file[52 + 4 * 40 + 64];
	struct image img = { file, sizeof(file) };
	uint32_t offset = 0;

	memset(file, 0, sizeof(file));
	memcpy(file, "\x7f" "ELF\x01\x02\x01", 7);
	put32(file + 0x20, 52);      /* e_shoff */
	put16(file + 0x2e, 40);      /* e_shentsize */
	put16(file + 0x30, 4);       /* e_shnum */
	add_section(file + 52 + 40, 1, 0x8c052800, 0x100, 0x200);
	/* Same address but NOBITS, and same address with another size, must
	 * both be skipped in favour of the exact PROGBITS match. */
	add_section(file + 52 + 80, 8, 0x8c880000, 0x300, 0x1000);
	add_section(file + 52 + 120, 1, 0x8c880000, 0x400, 0x1000);

	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x1000,
		&offset) == 0 && offset == 0x400, "exact section not found");
	CHECK(cp_elf_find_section(image_read, &img, 0x8c052800, 0x200,
		&offset) == 0 && offset == 0x100, "low section not found");
	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x2000,
		&offset) == -4, "size mismatch accepted");
	CHECK(cp_elf_find_section(image_read, &img, 0x8c900000, 0x1000,
		&offset) == -4, "wrong address accepted");

	file[5] = 1;                 /* little-endian: not ours */
	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x1000,
		&offset) == -2, "little-endian image accepted");
	file[5] = 2;
	file[0] = 0;
	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x1000,
		&offset) == -2, "bad magic accepted");
	file[0] = 0x7f;

	put16(file + 0x30, 0);       /* stripped of section headers */
	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x1000,
		&offset) == -3, "image without section headers accepted");
	put16(file + 0x30, 4);

	img.size = 52 + 60;          /* truncated section table */
	CHECK(cp_elf_find_section(image_read, &img, 0x8c880000, 0x1000,
		&offset) == -1, "truncated image accepted");
}

static void test_real(const char *path, uint32_t addr, uint32_t size)
{
	static const char stamp[] = "gpSP core ";
	FILE *f = fopen(path, "rb");
	struct image img;
	uint8_t *data;
	uint32_t offset = 0;
	long len;
	int rc;

	if (!f) {
		CHECK(0, "cannot open %s", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = malloc((size_t)len);
	if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
		CHECK(0, "cannot read %s", path);
		fclose(f);
		free(data);
		return;
	}
	fclose(f);
	img.data = data;
	img.size = (uint32_t)len;

	rc = cp_elf_find_section(image_read, &img, addr, size, &offset);
	CHECK(rc == 0, "%s: core section %08x+%x not found (%d)", path, addr,
		size, rc);
	if (rc == 0) {
		CHECK(offset + size <= img.size, "core section runs past the file");
		CHECK(memcmp(data + offset, stamp, sizeof(stamp) - 1) == 0,
			"core section does not start with the build stamp");
		printf("core section at file offset 0x%x, %u bytes, stamp \"%s\"\n",
			offset, size, (const char *)(data + offset));
	}
	free(data);
}

int main(int argc, char **argv)
{
	test_synthetic();
	if (argc == 4)
		test_real(argv[1], (uint32_t)strtoul(argv[2], NULL, 16),
			(uint32_t)strtoul(argv[3], NULL, 16));

	if (failures) {
		printf("classpad_elf_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("classpad_elf_test: all checks passed\n");
	return 0;
}
