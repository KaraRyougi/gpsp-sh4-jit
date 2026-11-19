#ifndef CGBA_CLASSPAD_ELF_H
#define CGBA_CLASSPAD_ELF_H

/* Locate one section of a big-endian ELF32 file by its link address.
 *
 * The fx-CP400 build keeps its RAM-resident emulator core in a section that
 * belongs to no program header, so the HollyHock loader leaves it in the file.
 * At startup gpSP finds that section in its own .hh3 and reads it into the
 * memory it has reserved (gint-gpsp/src/classpad_loader.c). This header is the
 * file-format half of that and is shared with tests/classpad_elf_test.c. */

#include <stdint.h>

/* Read `size` bytes at file `offset` into dst; return nonzero on success. */
typedef int (*cp_elf_read_fn)(void *ctx, void *dst, uint32_t offset,
	uint32_t size);

static inline uint32_t cp_elf_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t cp_elf_be16(const uint8_t *p)
{
	return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

#define CP_ELF_SHT_PROGBITS 1u

/* Return 0 and the file offset of the PROGBITS section whose address is
 * `addr` and whose size is `size`; a negative code says which step failed. */
static inline int cp_elf_find_section(cp_elf_read_fn read, void *ctx,
	uint32_t addr, uint32_t size, uint32_t *file_offset)
{
	uint8_t ehdr[52];
	uint8_t shdr[40];
	uint32_t shoff, shentsize, shnum, i;

	if(!read(ctx, ehdr, 0, sizeof(ehdr)))
		return -1;
	/* 0x7f 'E' 'L' 'F', 32-bit, big-endian */
	if(ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' ||
			ehdr[3] != 'F' || ehdr[4] != 1 || ehdr[5] != 2)
		return -2;

	shoff = cp_elf_be32(ehdr + 0x20);
	shentsize = cp_elf_be16(ehdr + 0x2e);
	shnum = cp_elf_be16(ehdr + 0x30);
	if(shoff == 0 || shentsize < sizeof(shdr) || shnum == 0)
		return -3;

	for(i = 0; i < shnum; i++) {
		if(!read(ctx, shdr, shoff + i * shentsize, sizeof(shdr)))
			return -1;
		if(cp_elf_be32(shdr + 4) == CP_ELF_SHT_PROGBITS &&
				cp_elf_be32(shdr + 12) == addr &&
				cp_elf_be32(shdr + 20) == size) {
			*file_offset = cp_elf_be32(shdr + 16);
			return 0;
		}
	}
	return -4;
}

#endif /* CGBA_CLASSPAD_ELF_H */
