#ifndef CGBA_CLASSPAD_HHK_H
#define CGBA_CLASSPAD_HHK_H

/* fx-CP400: the HollyHock-3 symbol table, and the check that the OS routine
 * named File_GetAddr is the one nor_rom.c was written against.
 *
 * HollyHock-3 starts an add-in with envp holding HHK_SYMBOL_TABLE and
 * HHK_SYMBOL_TABLE_LEN (hex). The table is the OS guard string (the one at
 * 0x814fffe0) and a NUL, then for each OS routine its name, a NUL and its
 * address as a big-endian u32. Later entries win, as in gint's hhk3_entry,
 * so an override file appended by the launcher replaces an address.
 *
 * gint binds 16 of these names (Mem_Malloc, Mem_Free and the File_* calls it
 * wraps as BFile_*) but not File_GetAddr, so gpSP looks that one up itself.
 *
 * Nothing here calls libc except memcmp, so the lookup is safe in the ELF
 * entry before gint has started. */

#include <stdint.h>
#include <string.h>

static inline uint32_t cp_hhk_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
		((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Address of `name` in a symbol table of `len` bytes, or 0 if it is not
 * there, the table is malformed, or its guard is not `guard`. */
static inline uint32_t cp_hhk_symbol(const char *table, uint32_t len,
	const char *guard, const char *name)
{
	uint32_t pos = 0, found = 0;
	uint32_t i;

	if(!table || !guard || !name)
		return 0;
	for(i = 0; ; i++) {
		if(i >= len || table[i] != guard[i])
			return 0;
		if(!guard[i])
			break;
	}
	pos = i + 1;

	while(pos < len) {
		int match = 1;

		/* While `match` holds, name[0..i) equals the entry so far and so
		 * has no NUL: name[i] is in bounds. */
		for(i = 0; pos < len && table[pos]; i++, pos++) {
			if(match && table[pos] != name[i])
				match = 0;
		}
		if(pos >= len)
			return 0;
		if(match && name[i])
			match = 0;
		pos++;  /* the NUL */
		if(len - pos < 4u)
			return 0;
		if(match)
			found = cp_hhk_be32((const uint8_t *)table + pos);
		pos += 4u;
	}
	return found;
}

/* Value of a hex env entry "<key>=<hex>" (8 digits from the HollyHock
 * launchers; "0x" accepted, as gint's strtoul does), 0 if absent or not
 * hex. */
static inline uint32_t cp_hhk_env_hex(char **envp, const char *key)
{
	char **env;

	for(env = envp; env && *env; env++) {
		const char *s = *env;
		uint32_t value = 0;
		unsigned i = 0, digits = 0;

		while(key[i] && s[i] == key[i])
			i++;
		if(key[i] || s[i] != '=')
			continue;
		s += i + 1;
		if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
			s += 2;
		for(; *s && digits < 8; s++, digits++) {
			char c = *s;

			if(c >= '0' && c <= '9')
				value = (value << 4) | (uint32_t)(c - '0');
			else if(c >= 'a' && c <= 'f')
				value = (value << 4) | (uint32_t)(c - 'a' + 10);
			else if(c >= 'A' && c <= 'F')
				value = (value << 4) | (uint32_t)(c - 'A' + 10);
			else
				break;
		}
		return (*s || digits == 0) ? 0 : value;
	}
	return 0;
}

/* Look `name` up in the table the launcher passed in envp. */
static inline uint32_t cp_hhk_env_symbol(char **envp, const char *guard,
	const char *name)
{
	uint32_t table = cp_hhk_env_hex(envp, "HHK_SYMBOL_TABLE");
	uint32_t len = cp_hhk_env_hex(envp, "HHK_SYMBOL_TABLE_LEN");

	if(!table || !len)
		return 0;
	return cp_hhk_symbol((const char *)(uintptr_t)table, len, guard, name);
}

/* ---- the File_GetAddr check ----------------------------------------------- */

/* Where File_GetAddr is in OS 02.01.7002 (HollyHock-3's table) and 02.01.7003
 * (same code at the same address). Used only when the table has no entry. */
#define CP_FILE_GETADDR_KNOWN 0x80058c88u

/* Returns `size` readable bytes of OS code at `address`, or NULL. */
typedef const uint8_t *(*cp_os_view)(void *ctx, uint32_t address,
	uint32_t size);

/* The word that the SH4 "mov.l @(disp,PC),Rn" at `insn` loads, or 0 if the
 * instruction is something else. */
static inline uint32_t cp_hhk_pc_literal(cp_os_view view, void *ctx,
	uint32_t insn)
{
	const uint8_t *op = view(ctx, insn, 2);
	const uint8_t *lit;

	if(!op || (op[0] & 0xf0u) != 0xd0u)
		return 0;
	lit = view(ctx, (insn & ~3u) + 4u + (uint32_t)op[1] * 4u, 4);
	return lit ? cp_hhk_be32(lit) : 0;
}

/*
 * Nonzero if `entry` is the File_GetAddr this code was written against
 * (OS 02.01.7003, disassembled in docs/classpad-port.md):
 *
 *   entry      int File_GetAddr(int fd, int offset, void **address):
 *              SR |= BL|IMASK (literal 0x100000f0), call `forward`, restore
 *   forward    rejects a NULL `address`, checks and splits the fd as the
 *              read call does, then jumps to `core` (literal at +0x24)
 *   core       the Fugue routine under Bfile_GetBlockAddress on the
 *              fx-CG100 (0x80167358 there); its cluster -> sector arithmetic
 *              at +0x94 is compared
 *
 * The three routines are found through the literals that link them, not at
 * fixed addresses, so the same code anywhere passes.
 */
static inline int cp_hhk_file_getaddr_ok(cp_os_view view, void *ctx,
	uint32_t entry)
{
	static const uint8_t entry_code[46] = {
		0x4f,0x22, 0x7f,0xec, 0x1f,0x62, 0x1f,0x51, 0x2f,0x42, 0x04,0x02,
		0xd1,0x12, 0x1f,0x43, 0x24,0x1b, 0x44,0x0e, 0xd7,0x24, 0x64,0xf2,
		0x55,0xf1, 0x47,0x0b, 0x56,0xf2, 0x54,0xf3, 0x1f,0x04, 0x44,0x0e,
		0x50,0xf4, 0x7f,0x14, 0x4f,0x26, 0x00,0x0b, 0x00,0x09
	};
	/* Up to and including "jmp @r1" into the core and its delay slot. */
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
	const uint8_t *code;
	uint32_t forward, core;

	if(!view || (entry & 1u))
		return 0;
	code = view(ctx, entry, sizeof(entry_code));
	if(!code || memcmp(code, entry_code, sizeof(entry_code)) != 0)
		return 0;
	if(cp_hhk_pc_literal(view, ctx, entry + 12u) != 0x100000f0u)
		return 0;

	forward = cp_hhk_pc_literal(view, ctx, entry + 20u);
	if(forward & 1u)
		return 0;
	code = view(ctx, forward, sizeof(forward_code));
	if(!code || memcmp(code, forward_code, sizeof(forward_code)) != 0)
		return 0;

	core = cp_hhk_pc_literal(view, ctx, forward + 0x24u);
	if(core & 1u)
		return 0;
	code = view(ctx, core + 0x94u, sizeof(core_code));
	return code && memcmp(code, core_code, sizeof(core_code)) == 0;
}

#endif /* CGBA_CLASSPAD_HHK_H */
