/* Host test for ports/fxcg100/classpad_hhk.h: looking File_GetAddr up in
 * HollyHock-3's symbol table, and the check of the code found there.
 *
 *   classpad_hhk_test              synthetic tables and OS images only
 *   classpad_hhk_test NOR.bin      also check a real ClassPad II flash dump (OS
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

	/* cp_hhk_resolve_file_getaddr:
	 * 1. Direct outer wrapper (OS 02.01.7002 / 02.01.7003). */
	build(&img, entry, forward, core);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, entry, 0) == entry,
		"resolve direct 7003 wrapper failed");

	/* 2. Inner forward address from OS 02.01.2000 addresses.json (0x80057c6a):
	 *    must locate the outer interrupt-masking wrapper at +0x51e (0x80058188). */
	build(&img, 0x80058188u, 0x80057c6au, 0x80047118u);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0x80057c6au, 0) ==
		0x80058188u, "resolve OS 2000 inner forward -> outer wrapper failed");

	/* 3. Inner forward with non-standard offset to outer wrapper (scan path). */
	build(&img, 0x80058400u, 0x80057c6au, 0x80047118u);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0x80057c6au, 0) ==
		0x80058400u, "resolve inner forward via scan failed");

	/* 4. Absent File_GetAddr, resolved relative to File_Open (+0x26c and scan). */
	build(&img, 0x80058188u, 0x80057c6au, 0x80047118u);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0, 0x80057f1cu) ==
		0x80058188u, "resolve via File_Open + 0x26c failed");
	build(&img, 0x80058320u, 0x80057c6au, 0x80047118u);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0, 0x80057f1cu) ==
		0x80058320u, "resolve via File_Open scan failed");

	/* 5. Corrupt image: must return 0 rather than an unverified address. */
	memset(img.data, 0x09, img.size);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0x80057c6au,
		0x80057f1cu) == 0, "corrupt image resolved non-zero");
}

static void put16(struct os_image *img, uint32_t address, uint16_t value)
{
	uint8_t *p = at(img, address);

	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static void put32(struct os_image *img, uint32_t address, uint32_t value)
{
	uint8_t *p = at(img, address);

	p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

/* Emit a "mov.l @(disp,PC), Rn" at `insn` and store `value` at `lit_addr`. */
static void put_movl_pc(struct os_image *img, uint32_t insn, unsigned reg,
	uint32_t lit_addr, uint32_t value)
{
	uint32_t base = (insn & ~3u) + 4u;
	uint32_t disp = (lit_addr - base) / 4u;

	put16(img, insn, (uint16_t)(0xd000u | ((reg & 0xfu) << 8) | (disp & 0xffu)));
	put32(img, lit_addr, value);
}

static void build_heap_os(struct os_image *img, uint32_t mem_malloc,
	uint32_t malloc_fn, uint32_t mem_free, uint32_t mem_sbrk,
	uint32_t mem_sbrk_mode, uint32_t head, uint32_t brk, uint32_t limit,
	uint32_t unit_at, uint32_t unit, uint32_t mode_flag)
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
	static const uint8_t mode_tail[16] = {
		0x62,0x4c, 0x65,0x5c, 0x32,0x50, 0x8d,0x01,
		0x66,0x10, 0x21,0x40, 0x00,0x0b, 0x60,0x63
	};
	uint32_t morecore = malloc_fn + 0x90u;
	uint32_t bsr_disp = ((morecore - (malloc_fn + 0x2eu + 4u)) / 2u) & 0xfffu;

	memset(img->data, 0x09, img->size);

	/* Mem_Malloc */
	put_movl_pc(img, mem_malloc + 0x1cu, 11, (mem_malloc + 0x58u) & ~3u,
		malloc_fn);
	put16(img, mem_malloc + 0x22u, 0x4b0bu);

	/* malloc + morecore */
	memcpy(at(img, malloc_fn), malloc_prolog, sizeof(malloc_prolog));
	put_movl_pc(img, malloc_fn + 0x0eu, 2, (malloc_fn + 0xccu) & ~3u,
		0x7fffffffu);
	put_movl_pc(img, malloc_fn + 0x16u, 1, (malloc_fn + 0xd0u) & ~3u, head);
	put16(img, malloc_fn + 0x2eu, (uint16_t)(0xb000u | bsr_disp));
	put_movl_pc(img, malloc_fn + 0x40u, 2, (malloc_fn + 0xd4u) & ~3u, mem_free);
	memcpy(at(img, morecore), morecore_prolog, sizeof(morecore_prolog));
	put_movl_pc(img, morecore + 0x06u, 5, (malloc_fn + 0xd8u) & ~3u, unit_at);
	put_movl_pc(img, morecore + 0x20u, 2, (malloc_fn + 0xe0u) & ~3u, mem_sbrk);
	put32(img, unit_at, unit);

	/* Mem_Free */
	put16(img, mem_free, 0x2fe6u);
	put_movl_pc(img, mem_free + 0x08u, 2, (mem_free + 0x94u) & ~3u, head);

	/* Mem_Sbrk */
	put16(img, mem_sbrk, 0x4f22u);
	put_movl_pc(img, mem_sbrk + 0x02u, 7, (mem_sbrk + 0x96u) & ~3u, brk);
	put_movl_pc(img, mem_sbrk + 0x04u, 2, (mem_sbrk + 0x9au) & ~3u, limit);
	memcpy(at(img, mem_sbrk + 0x06u), sbrk_check, sizeof(sbrk_check));

	/* Mem_SbrkMode */
	put16(img, mem_sbrk_mode, 0xe5ffu);
	put_movl_pc(img, mem_sbrk_mode + 0x02u, 1,
		(mem_sbrk_mode + 0x7au) & ~3u, mode_flag);
	memcpy(at(img, mem_sbrk_mode + 0x04u), mode_tail, sizeof(mode_tail));
}

static void test_heap_discovery(void)
{
	static uint8_t mem[0x40000];
	struct os_image img = { mem, 0x800a0000u, sizeof(mem) };
	cp_hhk_heap_info info;
	uint32_t differs = 0;

	/* OS 02.01.7002 / 02.01.7003 layout */
	build_heap_os(&img, CP_MEM_MALLOC_KNOWN, 0x800a8744u, CP_MEM_FREE_KNOWN,
		CP_MEM_SBRK_KNOWN, CP_MEM_SBRK_MODE_KNOWN, 0x8c1a35f0u, 0x8c2233ccu,
		0x8ce80000u, 0x800de000u, 0x400u, 0x8c1a2dc0u);
	CHECK(cp_hhk_find_heap(image_view, &img, CP_MEM_MALLOC_KNOWN,
		CP_MEM_FREE_KNOWN, CP_MEM_SBRK_KNOWN, 0x8cde6da0u, &info, &differs),
		"7003 heap layout rejected (differs=0x%x)", (unsigned)differs);
	CHECK(info.head == 0x8c1a35f0u && info.brk == 0x8c2233ccu &&
		info.limit == 0x8ce80000u && info.unit == 0x400u &&
		info.malloc_fn == 0x800a8744u, "7003 extracted fields mismatch");
	CHECK(cp_hhk_sbrk_mode_ok(image_view, &img, CP_MEM_SBRK_MODE_KNOWN),
		"7003 Mem_SbrkMode rejected");

	/* OS 02.01.2000 layout (different code addresses AND different brk RAM address) */
	build_heap_os(&img, 0x800cfb00u, 0x800a7794u, 0x800a76fcu,
		0x800cdec6u, 0x800aedf6u, 0x8c1a35f0u, 0x8c2233a0u,
		0x8ce80000u, 0x800de004u, 0x400u, 0x8c1a2dc0u);
	CHECK(cp_hhk_find_heap(image_view, &img, 0x800cfb00u, 0x800a76fcu,
		0x800cdec6u, 0x8cde6da0u, &info, &differs),
		"2000 heap layout rejected (differs=0x%x)", (unsigned)differs);
	CHECK(info.head == 0x8c1a35f0u && info.brk == 0x8c2233a0u &&
		info.limit == 0x8ce80000u && info.unit == 0x400u &&
		info.malloc_fn == 0x800a7794u, "2000 extracted fields mismatch");
	CHECK(cp_hhk_sbrk_mode_ok(image_view, &img, 0x800aedf6u),
		"2000 Mem_SbrkMode rejected");

	/* Works without optional Mem_Free / Mem_Sbrk symbol hints too. */
	CHECK(cp_hhk_find_heap(image_view, &img, 0x800cfb00u, 0, 0,
		0x8cde6da0u, &info, &differs),
		"heap discovery failed when optional hints omitted");

	/* Limit below arena end must be rejected with bit 0x20. */
	build_heap_os(&img, 0x800cfb00u, 0x800a7794u, 0x800a76fcu,
		0x800cdec6u, 0x800aedf6u, 0x8c1a35f0u, 0x8c2233a0u,
		0x8cd00000u, 0x800de004u, 0x400u, 0x8c1a2dc0u);
	CHECK(!cp_hhk_find_heap(image_view, &img, 0x800cfb00u, 0x800a76fcu,
		0x800cdec6u, 0x8cde6da0u, &info, &differs) && (differs & 0x20u),
		"undersized heap limit accepted");

	/* Corrupted Mem_SbrkMode flag outside RAM must be rejected. */
	build_heap_os(&img, 0x800cfb00u, 0x800a7794u, 0x800a76fcu,
		0x800cdec6u, 0x800aedf6u, 0x8c1a35f0u, 0x8c2233a0u,
		0x8ce80000u, 0x800de004u, 0x400u, 0x80000000u);
	CHECK(!cp_hhk_sbrk_mode_ok(image_view, &img, 0x800aedf6u),
		"Mem_SbrkMode with ROM flag accepted");
}

static void test_dump(const char *path)
{
	FILE *f = fopen(path, "rb");
	static uint8_t data[0x2000000];
	struct os_image img = { data, 0x80000000, 0 };
	cp_hhk_heap_info heap;
	uint32_t differs = 0;
	const char *guard;
	int is_2000;
	uint32_t fga, fga_sym, mm, mf, ms, msm, fopen_addr, resolved;

	CHECK(f != NULL, "cannot open %s", path);
	if (!f)
		return;
	img.size = (uint32_t)fread(data, 1, sizeof(data), f);
	fclose(f);
	guard = (const char *)data + 0x14fffe0;
	is_2000 = memcmp(guard, "2017.0512.1515", 14) == 0;
	fga = is_2000 ? 0x80058188u : CP_FILE_GETADDR_KNOWN;
	/* What HollyHock-3's addresses.json actually lists for File_GetAddr:
	 * outer wrapper on 7002/7003, inner forward routine on 2000. */
	fga_sym = is_2000 ? 0x80057c6au : CP_FILE_GETADDR_KNOWN;
	mm  = is_2000 ? 0x800cfb00u : CP_MEM_MALLOC_KNOWN;
	mf  = is_2000 ? 0x800a76fcu : CP_MEM_FREE_KNOWN;
	ms  = is_2000 ? 0x800cdec6u : CP_MEM_SBRK_KNOWN;
	msm = is_2000 ? 0x800aedf6u : CP_MEM_SBRK_MODE_KNOWN;
	fopen_addr = is_2000 ? 0x80057f1cu : 0x80058a1cu;

	CHECK(cp_hhk_file_getaddr_ok(image_view, &img, fga),
		"%s: File_GetAddr at %08x rejected", path, fga);
	resolved = cp_hhk_resolve_file_getaddr(image_view, &img, fga_sym, fopen_addr);
	CHECK(resolved == fga,
		"%s: resolve(sym=%08x, open=%08x) returned %08x, expected %08x",
		path, fga_sym, fopen_addr, resolved, fga);
	CHECK(cp_hhk_resolve_file_getaddr(image_view, &img, 0, fopen_addr) == fga,
		"%s: resolve(0, open=%08x) failed", path, fopen_addr);
	CHECK(!cp_hhk_file_getaddr_ok(image_view, &img, fopen_addr),
		"%s: File_Open accepted as File_GetAddr", path);
	CHECK(cp_hhk_find_heap(image_view, &img, mm, mf, ms, 0x8cde6da0u,
		&heap, &differs),
		"%s: heap discovery failed (differs=0x%x)", path, (unsigned)differs);
	CHECK(cp_hhk_sbrk_mode_ok(image_view, &img, msm),
		"%s: Mem_SbrkMode at %08x rejected", path, msm);
	printf("%s: guard \"%.15s\", File_GetAddr sym=%08x -> %08x, heap head=%08x "
		"brk=%08x limit=%08x unit=%x\n", path, guard, fga_sym, resolved,
		heap.head, heap.brk, heap.limit, heap.unit);
}

int main(int argc, char **argv)
{
	test_table();
	test_env();
	test_check();
	test_heap_discovery();
	if (argc == 2)
		test_dump(argv[1]);

	if (failures) {
		printf("classpad_hhk_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("classpad_hhk_test: all checks passed\n");
	return 0;
}
