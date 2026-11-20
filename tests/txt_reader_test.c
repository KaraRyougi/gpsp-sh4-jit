/* Host unit test for the platform-independent TXT reader engine
 * (ports/fxcg100/txt_reader.c + ports/fxcg100/txt_font_terminus.h). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/fxcg100_platform.h"
#include "ports/fxcg100/txt_reader.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		if (failures++ < 40) { \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} \
} while (0)

/* ---- virtual filesystem & storage stubs ----------------------------------- */

typedef struct vfs_entry {
	char path[160];       /* relative to \fls0\, backslash-separated */
	int is_dir;
	const uint8_t *data;
	uint32_t size;
	int allow_nor;        /* 1 = direct NOR mapping, 0 = BFile fallback */
} vfs_entry;

static vfs_entry vfs[64];
static int vfs_count;
static uint32_t fake_gen = 1;
static int nor_remap_calls;
static int bfile_read_calls;
static uint8_t meta_blob_buf[4096];
static unsigned meta_blob_size;
static int meta_writes;

static void vfs_reset(void)
{
	vfs_count = 0;
	fake_gen = 1;
	nor_remap_calls = 0;
	bfile_read_calls = 0;
	meta_blob_size = 0;
	meta_writes = 0;
}

static void vfs_add_dir(const char *rel)
{
	vfs_entry *e = &vfs[vfs_count++];
	memset(e, 0, sizeof(*e));
	snprintf(e->path, sizeof(e->path), "%s", rel);
	e->is_dir = 1;
}

static void vfs_add_file(const char *rel, const uint8_t *data, uint32_t size,
	int allow_nor)
{
	vfs_entry *e = &vfs[vfs_count++];
	memset(e, 0, sizeof(*e));
	snprintf(e->path, sizeof(e->path), "%s", rel);
	e->is_dir = 0;
	e->data = data;
	e->size = size;
	e->allow_nor = allow_nor;
}

static void fc_to_ascii(const uint16_t *src, char *dst, size_t max)
{
	size_t i = 0;
	while (src && src[i] && i + 1 < max) {
		dst[i] = (char)(src[i] & 0x7fu);
		i++;
	}
	dst[i] = 0;
}

static void ascii_to_fc(const char *src, uint16_t *dst, size_t max)
{
	size_t i = 0;
	while (src && src[i] && i + 1 < max) {
		dst[i] = (uint16_t)(unsigned char)src[i];
		i++;
	}
	dst[i] = 0;
}

static int str_eq_ci(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
		char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
		if (ca != cb)
			return 0;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static const vfs_entry *vfs_find_fc(const uint16_t *fc_path)
{
	char full[200];
	const char *rel;

	fc_to_ascii(fc_path, full, sizeof(full));
	if (strncmp(full, "\\\\fls0\\", 7) != 0)
		return NULL;
	rel = full + 7;
	for (int i = 0; i < vfs_count; i++) {
		if (!vfs[i].is_dir && str_eq_ci(vfs[i].path, rel))
			return &vfs[i];
	}
	return NULL;
}

void fxcg100_wait_keys_released(void) {}

uint32_t fxcg100_storage_mutation_generation(void)
{
	return fake_gen;
}

int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{
	(void)path;
	if (meta_blob_size != size)
		return 0;
	memcpy(dst, meta_blob_buf, size);
	return 1;
}

int fxcg100_storage_write_blob(const uint16_t *path, const void *src,
	unsigned size)
{
	(void)path;
	if (size > sizeof(meta_blob_buf))
		return 0;
	memcpy(meta_blob_buf, src, size);
	meta_blob_size = size;
	meta_writes++;
	fake_gen++;
	return 1;
}

int fxcg100_fs_list_dir(const uint16_t *pattern, fxcg100_fs_dir_cb cb,
	void *ctx)
{
	char pat[200], dir[200];
	size_t dir_len = 0;

	fc_to_ascii(pattern, pat, sizeof(pat));
	if (strncmp(pat, "\\\\fls0\\", 7) != 0)
		return 0;
	const char *p = pat + 7;
	const char *star = strrchr(p, '*');
	if (!star)
		return 0;
	dir_len = (size_t)(star - p);
	if (dir_len > 0 && p[dir_len - 1] == '\\')
		dir_len--;
	memcpy(dir, p, dir_len);
	dir[dir_len] = 0;

	for (int i = 0; i < vfs_count; i++) {
		const char *ep = vfs[i].path;
		const char *child;
		if (dir_len == 0) {
			if (strchr(ep, '\\') != NULL)
				continue;
			child = ep;
		} else {
			if (strlen(ep) <= dir_len + 1 || ep[dir_len] != '\\')
				continue;
			char prefix[200];
			memcpy(prefix, ep, dir_len);
			prefix[dir_len] = 0;
			if (!str_eq_ci(prefix, dir))
				continue;
			child = ep + dir_len + 1;
			if (strchr(child, '\\') != NULL)
				continue;
		}
		uint16_t found[160];
		ascii_to_fc(child, found, sizeof(found) / sizeof(found[0]));
		if (!cb(found, vfs[i].is_dir, vfs[i].size, ctx))
			break;
	}
	return 1;
}

int fxcg100_fs_open_map(const uint16_t *path, uint32_t *out_size,
	const uint8_t **nor_blocks, unsigned max_blocks,
	unsigned *out_mapped_blocks, uint8_t *tail_buf,
	uint32_t *out_tail_block_idx, uint32_t *out_tail_len)
{
	const vfs_entry *e = vfs_find_fc(path);

	if (!e)
		return 0;
	if (out_size)
		*out_size = e->size;
	if (out_mapped_blocks)
		*out_mapped_blocks = 0;
	if (out_tail_block_idx)
		*out_tail_block_idx = UINT32_MAX;
	if (out_tail_len)
		*out_tail_len = 0;

	if (!e->allow_nor || !nor_blocks || max_blocks == 0)
		return 1;

	nor_remap_calls++;
	uint32_t full_blocks = e->size / 4096u;
	uint32_t rem = e->size % 4096u;
	if (full_blocks > max_blocks)
		full_blocks = max_blocks;
	for (uint32_t b = 0; b < full_blocks; b++)
		nor_blocks[b] = e->data + b * 4096u;
	if (out_mapped_blocks)
		*out_mapped_blocks = full_blocks;
	if (rem > 0 && tail_buf && out_tail_block_idx && out_tail_len) {
		memcpy(tail_buf, e->data + full_blocks * 4096u, rem);
		*out_tail_block_idx = full_blocks;
		*out_tail_len = rem;
	}
	return 1;
}

int fxcg100_fs_read_range(const uint16_t *path, uint32_t offset,
	void *dst, uint32_t len)
{
	const vfs_entry *e = vfs_find_fc(path);

	if (!e || offset >= e->size)
		return 0;
	bfile_read_calls++;
	uint32_t avail = e->size - offset;
	if (len > avail)
		len = avail;
	memcpy(dst, e->data + offset, len);
	return (int)len;
}

#include "ports/fxcg100/txt_reader.c"

/* ---- tests ---------------------------------------------------------------- */

static void test_dir_navigation(void)
{
	txt_reader r;
	char disp[64];
	static const uint8_t dummy[] = "Hello";

	vfs_reset();
	vfs_add_dir("@MainMem");
	vfs_add_dir("Z_DIR");
	vfs_add_dir("GUIDES");
	vfs_add_dir("GUIDES\\RPG");
	vfs_add_file("NOTES.TXT", dummy, 5, 1);
	vfs_add_file("ALPHA.txt", dummy, 5, 1);
	vfs_add_file("GAME.GBA", dummy, 5, 1);
	vfs_add_file("GUIDES\\ZELDA.TXT", dummy, 5, 1);
	vfs_add_file("GUIDES\\RPG\\FF6.TXT", dummy, 5, 1);

	txt_reader_init(&r);
	CHECK(txt_reader_scan_dir(&r), "scan root");
	/* Folders first (sorted), then .txt files (sorted); @MainMem & .GBA skipped. */
	CHECK(r.dir_count == 4, "expected 4 entries, got %u", r.dir_count);
	CHECK(r.dir_entries[0].is_dir && strcmp(r.dir_entries[0].name, "GUIDES") == 0,
		"entry 0 should be GUIDES");
	CHECK(r.dir_entries[1].is_dir && strcmp(r.dir_entries[1].name, "Z_DIR") == 0,
		"entry 1 should be Z_DIR");
	CHECK(!r.dir_entries[2].is_dir && strcmp(r.dir_entries[2].name, "ALPHA.txt") == 0,
		"entry 2 should be ALPHA.txt");
	CHECK(!r.dir_entries[3].is_dir && strcmp(r.dir_entries[3].name, "NOTES.TXT") == 0,
		"entry 3 should be NOTES.TXT");
	txt_reader_display_dir(&r, disp, sizeof(disp));
	CHECK(strcmp(disp, "\\fls0\\") == 0, "root display dir: %s", disp);

	CHECK(txt_reader_enter_dir(&r, "GUIDES"), "enter GUIDES");
	CHECK(r.dir_depth == 1 && r.dir_count == 2, "GUIDES has RPG/ + ZELDA.TXT");
	CHECK(r.dir_entries[0].is_dir && strcmp(r.dir_entries[0].name, "RPG") == 0,
		"subfolder RPG");
	CHECK(!r.dir_entries[1].is_dir && strcmp(r.dir_entries[1].name, "ZELDA.TXT") == 0,
		"ZELDA.TXT");
	txt_reader_display_dir(&r, disp, sizeof(disp));
	CHECK(strcmp(disp, "\\fls0\\GUIDES\\") == 0, "GUIDES display dir: %s", disp);

	CHECK(txt_reader_enter_dir(&r, "RPG"), "enter RPG");
	CHECK(r.dir_depth == 2 && r.dir_count == 1, "RPG has FF6.TXT");
	CHECK(strcmp(r.dir_entries[0].name, "FF6.TXT") == 0, "FF6.TXT");

	CHECK(txt_reader_up_dir(&r), "up to GUIDES");
	CHECK(r.dir_depth == 1 && strcmp(r.dir_path, "GUIDES") == 0, "at GUIDES");
	CHECK(txt_reader_up_dir(&r), "up to root");
	CHECK(r.dir_depth == 0 && r.dir_path[0] == 0, "at root");
	CHECK(!txt_reader_up_dir(&r), "up at root returns 0");
}

static void test_wrap_pan_paging_and_hints(void)
{
	txt_reader r;
	static char doc[16384];
	size_t pos = 0;

	/* Build a multi-block (> 8 KiB) text file with tabs, CRLF, long line, ASCII map. */
	pos += (size_t)snprintf(doc + pos, sizeof(doc) - pos,
		"Line 1: The quick brown fox jumps over the lazy dog.\r\n"
		"\tIndented tab line.\n"
		"+--------------------+--------------------+\n"
		"| WIDE MAP COL 1     | WIDE MAP COL 2     |\n"
		"+--------------------+--------------------+\n");
	for (int i = 0; i < 240; i++) {
		pos += (size_t)snprintf(doc + pos, sizeof(doc) - pos,
			"Paragraph %03d: Walk north to the shrine and open chest #%03d.\n",
			i, i);
	}

	vfs_reset();
	vfs_add_file("GUIDE.TXT", (const uint8_t *)doc, (uint32_t)pos, 1);

	txt_reader_init(&r);
	CHECK(txt_reader_open(&r, "GUIDE.TXT"), "open GUIDE.TXT");
	CHECK(r.has_doc && r.doc_size == (uint32_t)pos, "doc size matches");
	CHECK(r.mapped_blocks == (uint32_t)pos / 4096u, "direct NOR blocks mapped");
	CHECK(r.tail_len == (uint32_t)pos % 4096u, "tail pre-read");
	CHECK(r.show_hints == 1, "hints visible on open");
	CHECK(bfile_read_calls == 0, "direct NOR did not call BFile read");

	txt_reader_layout(&r, 30, 8);
	CHECK(r.line_count == 8, "laid out 8 lines");
	CHECK(strncmp(r.lines[0].text, "Line 1: The quick brown fox", 27) == 0,
		"word wrapped at space: '%s'", r.lines[0].text);
	/* Tab expanded to 4 spaces on line 2. */
	CHECK(strncmp(r.lines[2].text, "    Indented tab line.", 22) == 0,
		"tab expanded: '%s'", r.lines[2].text);

	/* Scroll hides footer hints immediately. */
	CHECK(txt_reader_scroll_lines(&r, 1), "scroll down 1");
	CHECK(r.show_hints == 0, "hints auto-hidden on scroll");
	CHECK(r.top_offset == r.lines[0].offset && r.top_offset > 0, "advanced offset");

	/* Re-entering reader restores hints. */
	txt_reader_on_enter(&r);
	CHECK(r.show_hints == 1, "hints restored on re-enter");

	/* Page down with 1-line overlap and page up exact round-trip. */
	uint32_t start_off = r.top_offset;
	uint32_t last_line_off = r.lines[r.line_count - 1].offset;
	CHECK(txt_reader_page_down(&r), "page down");
	CHECK(r.show_hints == 0, "hints hidden on page down");
	CHECK(r.top_offset == last_line_off, "1-line overlap on page down");
	CHECK(txt_reader_page_up(&r), "page up");
	CHECK(r.top_offset == start_off, "page up restored exact offset");

	/* Jump % + Undo + Lookback page up without stack. */
	CHECK(txt_reader_jump_percent(&r, 50), "jump to 50%%");
	int pct = txt_reader_percent(&r);
	CHECK(pct >= 48 && pct <= 52, "percent near 50: %d", pct);
	uint32_t mid_off = r.top_offset;
	CHECK(txt_reader_page_up(&r), "lookback page up after jump");
	CHECK(r.top_offset < mid_off, "moved backward via lookback");
	CHECK(txt_reader_undo_jump(&r), "undo jump");
	CHECK(r.top_offset == start_off, "undo restored pre-jump offset");

	/* Wrap Off + Horizontal Pan. */
	txt_reader_jump_offset(&r, 0, 0);
	txt_reader_set_wrap(&r, 0);
	txt_reader_layout(&r, 20, 6);
	CHECK(strncmp(r.lines[2].text, "+-------------------", 20) == 0,
		"no-wrap preserves raw map: '%s'", r.lines[2].text);
	CHECK(txt_reader_pan(&r, 20), "pan right 20");
	txt_reader_layout(&r, 20, 6);
	CHECK(strncmp(r.lines[3].text, " | WIDE MAP COL 2", 17) == 0,
		"panned right: '%s'", r.lines[3].text);
	CHECK(txt_reader_pan(&r, -40), "pan back to 0");
	CHECK(r.pan_x == 0, "pan clamped at 0");

	/* Storage mutation generation triggers NOR remap. */
	int before_remaps = nor_remap_calls;
	fake_gen++;
	txt_reader_sync_storage(&r);
	CHECK(nor_remap_calls == before_remaps + 1, "mutation generation triggered remap");
}

static void test_bfile_fallback_streaming(void)
{
	txt_reader r;
	static uint8_t big[10000];
	for (uint32_t i = 0; i < sizeof(big); i++)
		big[i] = (i % 40 == 39) ? '\n' : (uint8_t)('A' + (i % 26));

	vfs_reset();
	vfs_add_file("FALLBACK.TXT", big, sizeof(big), 0); /* allow_nor = 0 */

	txt_reader_init(&r);
	CHECK(txt_reader_open(&r, "FALLBACK.TXT"), "open fallback file");
	CHECK(r.mapped_blocks == 0, "no NOR blocks mapped in fallback mode");
	txt_reader_layout(&r, 40, 10);
	CHECK(r.line_count == 10 && bfile_read_calls > 0, "read via cached BFile fallback");
	CHECK(r.lines[0].text[0] == 'A' && r.lines[0].text[25] == 'Z', "content accurate");

	/* Jump into third 4 KiB block. */
	CHECK(txt_reader_jump_percent(&r, 90), "jump near end in fallback");
	txt_reader_layout(&r, 40, 10);
	CHECK(r.line_count > 0 && r.top_offset > 8192u, "laid out third block");
}

static void test_dynamic_bookmarks_and_persistence(void)
{
	txt_reader r;
	static char doc[4096];
	size_t pos = 0;
	for (int i = 0; i < 80; i++)
		pos += (size_t)snprintf(doc + pos, sizeof(doc) - pos,
			"Section %02d: Boss strategy for room %02d.\n", i, i);

	vfs_reset();
	vfs_add_file("BOSS.TXT", (const uint8_t *)doc, (uint32_t)pos, 1);

	txt_reader_init(&r);
	CHECK(txt_reader_open(&r, "BOSS.TXT"), "open BOSS.TXT");
	txt_reader_layout(&r, 40, 8);
	CHECK(txt_reader_bookmark_count(&r) == 0, "starts with 0 bookmarks");

	/* Add 32 bookmarks at distinct offsets, verify dedup and 32 max cap. */
	CHECK(txt_reader_add_bookmark(&r) == TXT_BM_ADDED, "add bookmark #1");
	CHECK(txt_reader_add_bookmark(&r) == TXT_BM_EXISTS, "duplicate rejected");
	CHECK(txt_reader_has_visible_bookmark(&r), "visible bookmark detected");

	for (int i = 1; i < 32; i++) {
		txt_reader_jump_offset(&r, (uint32_t)(i * 60), 0);
		CHECK(txt_reader_add_bookmark(&r) == TXT_BM_ADDED, "add bookmark #%d", i + 1);
	}
	CHECK(txt_reader_bookmark_count(&r) == 32, "reached 32 bookmarks");
	txt_reader_jump_offset(&r, 2500u, 0);
	CHECK(txt_reader_add_bookmark(&r) == TXT_BM_FULL, "33rd bookmark rejected");

	/* Remove one, confirm count drops to 31 and new add succeeds. */
	CHECK(txt_reader_remove_bookmark(&r, 5), "remove bookmark 5");
	CHECK(txt_reader_bookmark_count(&r) == 31, "31 bookmarks remain");
	CHECK(txt_reader_add_bookmark(&r) == TXT_BM_ADDED, "re-added 32nd bookmark");

	/* Preview snippet extraction. */
	char preview[40];
	txt_reader_preview_at(&r, 0, preview, sizeof(preview));
	CHECK(strncmp(preview, "Section 00: Boss strategy", 25) == 0,
		"preview snippet: '%s'", preview);

	/* Change font & wrap, flush to CGBATXT.DAT, re-init and reopen. */
	txt_reader_set_font(&r, TXT_FONT_8X14);
	txt_reader_set_wrap(&r, 0);
	txt_reader_jump_offset(&r, 1200u, 0);
	txt_reader_flush(&r);
	CHECK(meta_writes == 1 && meta_blob_size == sizeof(txt_meta_blob),
		"flushed 2832-byte metadata blob");

	txt_reader r2;
	txt_reader_init(&r2);
	CHECK(txt_reader_open(&r2, "BOSS.TXT"), "reopen BOSS.TXT");
	CHECK(r2.top_offset == 1200u, "restored reading offset: %u", r2.top_offset);
	CHECK(r2.font_idx == TXT_FONT_8X14, "restored font");
	CHECK(r2.wrap == 0, "restored wrap");
	CHECK(txt_reader_bookmark_count(&r2) == 32, "restored all 32 bookmarks");
}

static void test_terminus_font_rendering(void)
{
	static uint16_t fb[128 * 64];
	cp_surface s;
	int lit6 = 0, lit8 = 0;

	memset(fb, 0, sizeof(fb));
	cp_surface_init(&s, fb, 128, 64, CP_SURFACE_UPRIGHT);
	txt_draw_text(&s, &txt_font_terminus_6x12, 4, 4, "Hello 6x12!", 0xFFFF);
	for (int y = 4; y < 16; y++)
		for (int x = 4; x < 70; x++)
			if (fb[y * 128 + x] == 0xFFFF)
				lit6++;
	CHECK(lit6 > 80, "Terminus 6x12 drew pixels: %d", lit6);

	txt_draw_text_fit(&s, &txt_font_terminus_8x14, 4, 24, 64, "ABCDEFGHIJKL",
		0x07E0);
	for (int y = 24; y < 38; y++)
		for (int x = 4; x < 68; x++)
			if (fb[y * 128 + x] == 0x07E0)
				lit8++;
	CHECK(lit8 > 60, "Terminus 8x14 drew fitted pixels: %d", lit8);
	/* Ensure nothing leaked past max_cols * 8 = 64 px. */
	for (int y = 24; y < 38; y++)
		for (int x = 68; x < 128; x++)
			CHECK(fb[y * 128 + x] == 0, "fit overflow at (%d,%d)", x, y);
}

int main(void)
{
	test_dir_navigation();
	test_wrap_pan_paging_and_hints();
	test_bfile_fallback_streaming();
	test_dynamic_bookmarks_and_persistence();
	test_terminus_font_rendering();
	printf("txt_reader_test: %s\n", failures ? "FAILED" : "ok");
	return failures ? 1 : 0;
}
