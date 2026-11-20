#ifndef CGBA_CLASSPAD_HHK_H
#define CGBA_CLASSPAD_HHK_H

/* ClassPad II: the HollyHock-3 symbol table, and the check that the OS routine
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

static inline uint16_t cp_hhk_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
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
 * Nonzero if `forward` is the inner File_GetAddr forwarding routine
 * (rejects NULL `address`, checks/splits the fd, jumps to the Fugue core)
 * and its core matches the cluster -> sector arithmetic of Bfile_GetBlockAddress.
 */
static inline int cp_hhk_file_getaddr_forward_ok(cp_os_view view, void *ctx,
	uint32_t forward)
{
	/* Up to and including "jmp @r1" into the core and its delay slot.
	 * Byte 37 (0x25) is the 8-bit PC-relative displacement of "mov.l @(pc), r1"
	 * and may vary if the literal pool shifts; the loaded core address is
	 * verified separately via cp_hhk_pc_literal(). */
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
	uint32_t core;

	if(!view || !forward || (forward & 1u))
		return 0;
	code = view(ctx, forward, sizeof(forward_code));
	if(!code || memcmp(code, forward_code, 37u) != 0 ||
			memcmp(code + 38u, forward_code + 38u, sizeof(forward_code) - 38u) != 0)
		return 0;

	core = cp_hhk_pc_literal(view, ctx, forward + 0x24u);
	if(!core || (core & 1u))
		return 0;
	code = view(ctx, core + 0x94u, sizeof(core_code));
	return code && memcmp(code, core_code, sizeof(core_code)) == 0;
}

/*
 * Nonzero if `entry` is the File_GetAddr this code was written against
 * (OS 02.01.2000 / 02.01.7002 / 02.01.7003, disassembled in docs/classpad-port.md):
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
	/* Bytes 13 and 21 are the 8-bit PC-relative displacements of "mov.l @(pc), r1"
	 * and "mov.l @(pc), r7"; their loaded values are checked via cp_hhk_pc_literal(). */
	static const uint8_t entry_code[46] = {
		0x4f,0x22, 0x7f,0xec, 0x1f,0x62, 0x1f,0x51, 0x2f,0x42, 0x04,0x02,
		0xd1,0x12, 0x1f,0x43, 0x24,0x1b, 0x44,0x0e, 0xd7,0x24, 0x64,0xf2,
		0x55,0xf1, 0x47,0x0b, 0x56,0xf2, 0x54,0xf3, 0x1f,0x04, 0x44,0x0e,
		0x50,0xf4, 0x7f,0x14, 0x4f,0x26, 0x00,0x0b, 0x00,0x09
	};
	const uint8_t *code;
	uint32_t forward;

	if(!view || !entry || (entry & 1u))
		return 0;
	code = view(ctx, entry, sizeof(entry_code));
	if(!code || memcmp(code, entry_code, 13u) != 0 ||
			memcmp(code + 14u, entry_code + 14u, 7u) != 0 ||
			memcmp(code + 22u, entry_code + 22u, sizeof(entry_code) - 22u) != 0)
		return 0;
	if(cp_hhk_pc_literal(view, ctx, entry + 12u) != 0x100000f0u)
		return 0;

	forward = cp_hhk_pc_literal(view, ctx, entry + 20u);
	return cp_hhk_file_getaddr_forward_ok(view, ctx, forward);
}

/*
 * Resolve the verified outer File_GetAddr(fd, offset, &addr) entry point.
 *
 * Handles three cases across HollyHock-3 OS builds:
 *  1. `named_getaddr` is already the outer interrupt-masking wrapper
 *     (OS 02.01.7002 / 02.01.7003: 0x80058c88).
 *  2. `named_getaddr` points to the inner forwarding routine instead
 *     (OS 02.01.2000's addresses.json records 0x80057c6a while the outer
 *     wrapper sits at 0x80058188 = +0x51e): locate the nearby outer wrapper
 *     that calls `named_getaddr` and verify the full chain.
 *  3. `named_getaddr` is absent or unusable: probe near `named_open`
 *     (File_Open + 0x26c in OS 2000 / 7002 / 7003, or a small scan window),
 *     then fall back to CP_FILE_GETADDR_KNOWN.
 *
 * Returns 0 if no verified File_GetAddr wrapper is found.
 */
static inline uint32_t cp_hhk_resolve_file_getaddr(cp_os_view view, void *ctx,
	uint32_t named_getaddr, uint32_t named_open)
{
	uint32_t cand, off;

	if(!view)
		return 0;

	/* 1. Direct outer wrapper from the symbol table (7002 / 7003). */
	if(named_getaddr && cp_hhk_file_getaddr_ok(view, ctx, named_getaddr))
		return named_getaddr;

	/* 2. Symbol table points to the inner forward routine (OS 02.01.2000). */
	if(named_getaddr &&
			cp_hhk_file_getaddr_forward_ok(view, ctx, named_getaddr)) {
		cand = named_getaddr + 0x51eu;
		if(cp_hhk_file_getaddr_ok(view, ctx, cand) &&
				cp_hhk_pc_literal(view, ctx, cand + 20u) == named_getaddr)
			return cand;
		for(off = 0x40u; off < 0x2000u; off += 2u) {
			const uint8_t *op;

			cand = named_getaddr + off;
			op = view(ctx, cand, 4u);
			if(!op || op[0] != 0x4fu || op[1] != 0x22u ||
					op[2] != 0x7fu || op[3] != 0xecu)
				continue;
			if(cp_hhk_file_getaddr_ok(view, ctx, cand) &&
					cp_hhk_pc_literal(view, ctx, cand + 20u) == named_getaddr)
				return cand;
		}
	}

	/* 3. Locate relative to File_Open when File_GetAddr was omitted. */
	if(named_open && !(named_open & 1u)) {
		cand = named_open + 0x26cu;
		if(cp_hhk_file_getaddr_ok(view, ctx, cand))
			return cand;
		for(off = 0x20u; off < 0x1000u; off += 2u) {
			const uint8_t *op;

			cand = named_open + off;
			op = view(ctx, cand, 4u);
			if(!op || op[0] != 0x4fu || op[1] != 0x22u ||
					op[2] != 0x7fu || op[3] != 0xecu)
				continue;
			if(cp_hhk_file_getaddr_ok(view, ctx, cand))
				return cand;
		}
	}

	/* 4. Known 7002/7003 fallback when the symbol table had neither. */
	if(cp_hhk_file_getaddr_ok(view, ctx, CP_FILE_GETADDR_KNOWN))
		return CP_FILE_GETADDR_KNOWN;

	return 0;
}

/* ---- OS heap allocator discovery (OS-version independent) ----------------- */

/* Known OS 02.01.7002 / 02.01.7003 addresses, used only when the launcher's
 * symbol table does not provide the corresponding Mem_* entry. */
#define CP_MEM_MALLOC_KNOWN    0x800d0ad8u
#define CP_MEM_FREE_KNOWN      0x800a86acu
#define CP_MEM_SBRK_KNOWN      0x800cee9eu
#define CP_MEM_SBRK_MODE_KNOWN 0x800afdc6u

/* Target of an SH4 "bsr disp" (0xbxxx) at `insn`, or 0 if not a bsr. */
static inline uint32_t cp_hhk_bsr_target(cp_os_view view, void *ctx,
	uint32_t insn)
{
	const uint8_t *op = view ? view(ctx, insn, 2) : NULL;
	int32_t disp;

	if(!op || (op[0] & 0xf0u) != 0xb0u)
		return 0;
	disp = ((int32_t)(op[0] & 0x0fu) << 8) | op[1];
	if(disp & 0x800)
		disp -= 0x1000;
	return insn + 4u + (uint32_t)(disp * 2);
}

static inline int cp_hhk_in_ram_aligned(uint32_t a)
{
	return a >= 0x8c000000u && a < 0x8d000000u && (a & 3u) == 0;
}

/* Nonzero if `entry` is the 10-instruction Mem_SbrkMode(signed char) leaf
 * routine (OS 02.01.2000 / 02.01.7002 / 02.01.7003):
 *   mov #-1, r5; mov.l @(pc), r1 (RAM byte); extu.b r4,r2; extu.b r5,r5;
 *   cmp/eq r5,r2; bt.s +2; mov.b @r1,r6; mov.b r4,@r1; rts; mov r6,r0 */
static inline int cp_hhk_sbrk_mode_ok(cp_os_view view, void *ctx,
	uint32_t entry)
{
	static const uint8_t tail_code[16] = {
		0x62,0x4c, 0x65,0x5c, 0x32,0x50, 0x8d,0x01,
		0x66,0x10, 0x21,0x40, 0x00,0x0b, 0x60,0x63
	};
	const uint8_t *code;
	uint32_t flag;

	if(!view || !entry || (entry & 1u))
		return 0;
	code = view(ctx, entry, 20u);
	if(!code || code[0] != 0xe5u || code[1] != 0xffu || code[2] != 0xd1u)
		return 0;
	if(memcmp(code + 4, tail_code, sizeof(tail_code)) != 0)
		return 0;
	flag = cp_hhk_pc_literal(view, ctx, entry + 2u);
	return flag >= 0x8c000000u && flag < 0x8d000000u;
}

typedef struct cp_hhk_heap_info {
	uint32_t head;       /* RAM address of free_list_head */
	uint32_t brk;        /* RAM address of sbrk's current break pointer */
	uint32_t limit;      /* heap ceiling value (e.g. 0x8ce80000) */
	uint32_t unit;       /* sbrk rounding unit (e.g. 0x400) */
	uint32_t malloc_fn;  /* inner C malloc (no C++ new_handler loop) */
	uint32_t free_fn;    /* Mem_Free */
	uint32_t sbrk_fn;    /* Mem_Sbrk */
} cp_hhk_heap_info;

/*
 * Locate the OS heap allocator's state (head, brk, limit, unit) by following
 * PC-relative literals starting from Mem_Malloc (resolved from HollyHock-3's
 * symbol table). Works across OS versions whose code and RAM addresses differ
 * (verified on OS 02.01.2000 and 02.01.7002/7003) as long as the allocator
 * matches the one classpad_heap.h models.
 *
 * Returns nonzero on match with *out populated; otherwise returns 0 and sets
 * *differs to a bitmask of checks that failed (same bits as the former
 * fixed-address check).
 */
static inline int cp_hhk_find_heap(cp_os_view view, void *ctx,
	uint32_t mem_malloc, uint32_t mem_free, uint32_t mem_sbrk,
	uint32_t min_limit, cp_hhk_heap_info *out, uint32_t *differs)
{
	static const uint8_t malloc_prolog[8] = {
		0x2f,0xc6, 0x2f,0xd6, 0x2f,0xe6, 0x4f,0x22
	};
	static const uint8_t morecore_prolog[6] = {
		0x2f,0xe6, 0x4f,0x22, 0x4f,0x12
	};
	static const uint8_t sbrk_check[8] = {
		0x60,0x72, 0x66,0x03, 0x36,0x4c, 0x36,0x26
	};
	uint32_t d = 0;
	uint32_t malloc_fn = 0, free_fn = 0, morecore = 0, sbrk_fn = 0;
	uint32_t head = 0, free_head = 0, brk = 0, limit = 0;
	uint32_t unit_at = 0, unit = 0;
	const uint8_t *code;

	if(!view || !out) {
		if(differs)
			*differs = 0xffu;
		return 0;
	}

	/* 1. Mem_Malloc -> inner C malloc (mov.l @(pc), r11 at +0x1c; jsr @r11 at +0x22) */
	if(!mem_malloc || (mem_malloc & 1u)) {
		d |= 0x80u;
	} else {
		code = view(ctx, mem_malloc + 0x1cu, 8u);
		if(!code || code[0] != 0xdbu || code[6] != 0x4bu || code[7] != 0x0bu) {
			d |= 0x80u;
		} else {
			malloc_fn = cp_hhk_pc_literal(view, ctx, mem_malloc + 0x1cu);
			if(!malloc_fn || (malloc_fn & 1u))
				d |= 0x80u;
		}
	}

	/* 2. Inner malloc: prologue, 0x7fffffff cap, free_list_head, Mem_Free, morecore */
	if(malloc_fn) {
		code = view(ctx, malloc_fn, sizeof(malloc_prolog));
		if(!code || memcmp(code, malloc_prolog, sizeof(malloc_prolog)) != 0 ||
				cp_hhk_pc_literal(view, ctx, malloc_fn + 0x0eu) != 0x7fffffffu)
			d |= 0x01u;
		head = cp_hhk_pc_literal(view, ctx, malloc_fn + 0x16u);
		free_fn = cp_hhk_pc_literal(view, ctx, malloc_fn + 0x40u);
		morecore = cp_hhk_bsr_target(view, ctx, malloc_fn + 0x2eu);
	} else {
		d |= 0x01u;
	}

	/* 3. Mem_Free: matches symbol table (if given) and agrees on free_list_head */
	if(!free_fn || (free_fn & 1u) || (mem_free && free_fn != mem_free)) {
		d |= 0x02u;
	} else {
		code = view(ctx, free_fn, 2u);
		if(!code || cp_hhk_be16(code) != 0x2fe6u)
			d |= 0x02u;
		free_head = cp_hhk_pc_literal(view, ctx, free_fn + 0x08u);
	}
	if(!cp_hhk_in_ram_aligned(head) || free_head != head)
		d |= 0x10u;

	/* 4. morecore: prologue, unit pointer, Mem_Sbrk */
	if(!morecore || (morecore & 1u)) {
		d |= 0x04u;
	} else {
		code = view(ctx, morecore, sizeof(morecore_prolog));
		if(!code || memcmp(code, morecore_prolog, sizeof(morecore_prolog)) != 0) {
			d |= 0x04u;
		} else {
			unit_at = cp_hhk_pc_literal(view, ctx, morecore + 0x06u);
			sbrk_fn = cp_hhk_pc_literal(view, ctx, morecore + 0x20u);
			if(!sbrk_fn || (sbrk_fn & 1u) || (mem_sbrk && sbrk_fn != mem_sbrk))
				d |= 0x04u;
		}
	}

	if(!unit_at || (unit_at & 3u)) {
		d |= 0x40u;
	} else {
		code = view(ctx, unit_at, 4u);
		if(!code) {
			d |= 0x40u;
		} else {
			unit = cp_hhk_be32(code);
			if(unit < 0x10u || unit > 0x100000u || (unit & 3u))
				d |= 0x40u;
		}
	}

	/* 5. Mem_Sbrk: prologue, brk + size > limit check, brk & limit literals */
	if(!sbrk_fn || (sbrk_fn & 1u)) {
		d |= 0x08u;
	} else {
		code = view(ctx, sbrk_fn, 14u);
		if(!code || cp_hhk_be16(code) != 0x4f22u ||
				memcmp(code + 6, sbrk_check, sizeof(sbrk_check)) != 0)
			d |= 0x08u;
		brk = cp_hhk_pc_literal(view, ctx, sbrk_fn + 0x02u);
		limit = cp_hhk_pc_literal(view, ctx, sbrk_fn + 0x04u);
	}
	if(!cp_hhk_in_ram_aligned(brk) || !limit || (limit & 3u) ||
			limit < min_limit || limit > 0x8d000000u)
		d |= 0x20u;

	if(differs)
		*differs = d;
	if(d != 0)
		return 0;

	out->head = head;
	out->brk = brk;
	out->limit = limit;
	out->unit = unit;
	out->malloc_fn = malloc_fn;
	out->free_fn = free_fn;
	out->sbrk_fn = sbrk_fn;
	return 1;
}

#endif /* CGBA_CLASSPAD_HHK_H */
