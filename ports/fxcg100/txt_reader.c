#include "txt_reader.h"
#include "fxcg100_platform.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(txt_meta_blob) == 2832u,
	"txt_meta_blob must stay fixed at 2832 bytes (< 4 KiB Fugue block)");

static txt_reader txt_reader_instance;

static const uint16_t txt_meta_path[] = {
	'\\', '\\', 'f', 'l', 's', '0', '\\',
	'C', 'G', 'B', 'A', 'T', 'X', 'T', '.', 'D', 'A', 'T', 0
};

txt_reader *txt_reader_global(void)
{
	return &txt_reader_instance;
}

/* ---- small ASCII / path helpers ------------------------------------------- */

static char ascii_up(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int str_casecmp(const char *a, const char *b)
{
	for (;;) {
		unsigned char ca = (unsigned char)ascii_up(*a++);
		unsigned char cb = (unsigned char)ascii_up(*b++);
		if (ca != cb)
			return (int)ca - (int)cb;
		if (!ca)
			return 0;
	}
}

static int has_txt_extension(const char *name)
{
	size_t len = strlen(name);

	if (len < 4u)
		return 0;
	return ascii_up(name[len - 4u]) == '.' &&
		ascii_up(name[len - 3u]) == 'T' &&
		ascii_up(name[len - 2u]) == 'X' &&
		ascii_up(name[len - 1u]) == 'T';
}

static void path_hash64(const char *path, uint32_t *lo, uint32_t *hi)
{
	uint64_t h = 14695981039346656037ULL;

	if (path) {
		for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
			unsigned char c = (unsigned char)ascii_up((char)*p);
			if (c == '/')
				c = '\\';
			h ^= (uint64_t)c;
			h *= 1099511628211ULL;
		}
	}
	*lo = (uint32_t)h;
	*hi = (uint32_t)(h >> 32);
}

static int build_fc_path(uint16_t *dst, size_t max_u16, const char *rel,
	const char *suffix)
{
	static const char prefix[] = "\\\\fls0\\";
	size_t out = 0, i;

	if (!dst || max_u16 < 16u)
		return 0;
	for (i = 0; prefix[i]; i++) {
		if (out + 1u >= max_u16)
			return 0;
		dst[out++] = (uint16_t)(unsigned char)prefix[i];
	}
	if (rel && rel[0]) {
		for (i = 0; rel[i]; i++) {
			char c = rel[i] == '/' ? '\\' : rel[i];
			if (out + 1u >= max_u16)
				return 0;
			dst[out++] = (uint16_t)(unsigned char)c;
		}
		if (suffix && suffix[0] && dst[out - 1u] != '\\') {
			if (out + 1u >= max_u16)
				return 0;
			dst[out++] = '\\';
		}
	}
	if (suffix) {
		for (i = 0; suffix[i]; i++) {
			if (out + 1u >= max_u16)
				return 0;
			dst[out++] = (uint16_t)(unsigned char)suffix[i];
		}
	}
	dst[out] = 0;
	return 1;
}

static void extract_basename_fc(const uint16_t *found, char *dst, size_t max)
{
	size_t start = 0, i, out = 0;

	if (!dst || max == 0)
		return;
	if (!found) {
		dst[0] = 0;
		return;
	}
	for (i = 0; found[i]; i++) {
		if (found[i] == '\\' || found[i] == '/')
			start = i + 1u;
	}
	for (i = start; found[i] && out + 1u < max; i++) {
		uint16_t c = found[i];
		dst[out++] = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
	}
	dst[out] = 0;
}

void txt_format_size(uint32_t bytes, char *dst, size_t dst_size)
{
	if (!dst || dst_size == 0)
		return;
	if (bytes < 1024u) {
		snprintf(dst, dst_size, "%u B", (unsigned)bytes);
	} else if (bytes < 1024u * 1024u) {
		uint32_t kb = (bytes + 512u) / 1024u;
		if (kb == 0u)
			kb = 1u;
		snprintf(dst, dst_size, "%u KB", (unsigned)kb);
	} else {
		uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10u + 524288u) /
			(1024u * 1024u));
		snprintf(dst, dst_size, "%u.%u MB",
			(unsigned)(tenths / 10u), (unsigned)(tenths % 10u));
	}
}

const txt_bitmap_font *txt_reader_font(const txt_reader *r)
{
	if (r && r->font_idx == TXT_FONT_8X14)
		return &txt_font_terminus_8x14;
	return &txt_font_terminus_6x12;
}

void txt_reader_init(txt_reader *r)
{
	if (!r)
		return;
	memset(r, 0, sizeof(*r));
	r->font_idx = TXT_FONT_6X12;
	r->wrap = 1;
	r->show_hints = 1;
	r->tail_block_idx = UINT32_MAX;
	r->cache_block_idx = UINT32_MAX;
	r->active_meta_slot = -1;
	r->layout_dirty = 1;
}

/* ---- persistent metadata (\\fls0\\CGBATXT.DAT) ---------------------------- */

static void meta_reset(txt_meta_blob *m)
{
	memset(m, 0, sizeof(*m));
	m->magic = TXT_META_MAGIC;
	m->version = TXT_META_VERSION;
	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++)
		m->marks[i].file_idx = 0xFFu;
}

static int meta_valid(const txt_meta_blob *m)
{
	return m &&
		m->magic == TXT_META_MAGIC &&
		m->version == TXT_META_VERSION &&
		m->file_count <= TXT_META_MAX_FILES;
}

static void meta_ensure_loaded(txt_reader *r)
{
	if (r->meta_loaded)
		return;
	r->meta_loaded = 1;
	fxcg100_wait_keys_released();
	if (!fxcg100_storage_read_blob(txt_meta_path, &r->meta, sizeof(r->meta)) ||
			!meta_valid(&r->meta)) {
		meta_reset(&r->meta);
	} else {
		for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
			if (r->meta.marks[i].file_idx >= r->meta.file_count)
				r->meta.marks[i].file_idx = 0xFFu;
		}
	}
}

static void meta_clear_file_marks(txt_meta_blob *m, uint8_t slot)
{
	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
		if (m->marks[i].file_idx == slot)
			m->marks[i].file_idx = 0xFFu;
	}
}

static int meta_find_or_alloc(txt_reader *r, const char *rel_path,
	uint32_t file_size, int *was_existing)
{
	uint32_t lo, hi;
	txt_meta_blob *m = &r->meta;

	meta_ensure_loaded(r);
	path_hash64(rel_path, &lo, &hi);
	for (unsigned i = 0; i < m->file_count; i++) {
		if (m->files[i].hash_lo == lo && m->files[i].hash_hi == hi) {
			if (was_existing)
				*was_existing = 1;
			if (m->files[i].file_size != file_size) {
				/* File was replaced on storage: clamp offset & clear stale marks. */
				m->files[i].file_size = file_size;
				if (m->files[i].offset >= file_size)
					m->files[i].offset = 0;
				meta_clear_file_marks(m, (uint8_t)i);
				r->meta_dirty = 1;
			}
			m->open_seq++;
			m->files[i].last_opened = m->open_seq;
			return (int)i;
		}
	}

	if (was_existing)
		*was_existing = 0;

	unsigned slot;
	if (m->file_count < TXT_META_MAX_FILES) {
		slot = m->file_count++;
	} else {
		/* Evict least recently opened entry and reclaim its bookmarks. */
		slot = 0;
		for (unsigned i = 1; i < m->file_count; i++) {
			if (m->files[i].last_opened < m->files[slot].last_opened)
				slot = i;
		}
		meta_clear_file_marks(m, (uint8_t)slot);
	}
	memset(&m->files[slot], 0, sizeof(m->files[slot]));
	m->files[slot].hash_lo = lo;
	m->files[slot].hash_hi = hi;
	m->files[slot].file_size = file_size;
	m->open_seq++;
	m->files[slot].last_opened = m->open_seq;
	m->files[slot].font_idx = r->font_idx;
	m->files[slot].wrap = r->wrap;
	r->meta_dirty = 1;
	return (int)slot;
}

static void meta_sync_active_fields(txt_reader *r)
{
	if (!r->has_doc || r->active_meta_slot < 0 ||
			(unsigned)r->active_meta_slot >= r->meta.file_count)
		return;
	txt_meta_file_rec *rec = &r->meta.files[r->active_meta_slot];
	if (rec->offset != r->top_offset ||
			rec->font_idx != r->font_idx ||
			rec->wrap != r->wrap) {
		rec->offset = r->top_offset;
		rec->font_idx = r->font_idx;
		rec->wrap = r->wrap;
		r->meta_dirty = 1;
	}
}

void txt_reader_flush(txt_reader *r)
{
	if (!r)
		return;
	meta_sync_active_fields(r);
	if (!r->meta_dirty)
		return;
	fxcg100_wait_keys_released();
	if (fxcg100_storage_write_blob(txt_meta_path, &r->meta, sizeof(r->meta)))
		r->meta_dirty = 0;
}

/* ---- directory browser ---------------------------------------------------- */

static int dir_cb(const uint16_t *found, int is_dir, uint32_t file_size,
	void *ctx)
{
	txt_reader *r = ctx;
	char name[TXT_NAME_MAX];

	if (r->dir_count >= TXT_MAX_DIR_ENTRIES)
		return 0;
	extract_basename_fc(found, name, sizeof(name));
	if (!name[0])
		return 1;
	if (is_dir) {
		if ((name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) ||
				name[0] == '@')
			return 1;
	} else {
		if (!has_txt_extension(name))
			return 1;
	}

	/* Avoid duplicates if firmware enumerates the same entry twice. */
	for (unsigned i = 0; i < r->dir_count; i++) {
		if (r->dir_entries[i].is_dir == (uint8_t)is_dir &&
				str_casecmp(r->dir_entries[i].name, name) == 0)
			return 1;
	}

	txt_dir_entry *e = &r->dir_entries[r->dir_count++];
	snprintf(e->name, sizeof(e->name), "%s", name);
	e->size = is_dir ? 0u : file_size;
	e->is_dir = (uint8_t)(is_dir ? 1 : 0);
	return r->dir_count < TXT_MAX_DIR_ENTRIES;
}

static void sort_dir_entries(txt_dir_entry *entries, unsigned count)
{
	for (unsigned i = 1; i < count; i++) {
		txt_dir_entry key = entries[i];
		unsigned j = i;
		while (j > 0) {
			const txt_dir_entry *prev = &entries[j - 1u];
			int before = 0;
			if (key.is_dir != prev->is_dir)
				before = key.is_dir > prev->is_dir;
			else
				before = str_casecmp(key.name, prev->name) < 0;
			if (!before)
				break;
			entries[j] = entries[j - 1u];
			j--;
		}
		entries[j] = key;
	}
}

int txt_reader_scan_dir(txt_reader *r)
{
	uint16_t pattern[160];

	if (!r)
		return -1;
	r->dir_count = 0;
	r->dir_scanned = 1;
	if (!build_fc_path(pattern, sizeof(pattern) / sizeof(pattern[0]),
			r->dir_path, "*"))
		return -1;

	fxcg100_wait_keys_released();
	int rc = fxcg100_fs_list_dir(pattern, dir_cb, r);
	sort_dir_entries(r->dir_entries, r->dir_count);
	return rc;
}

int txt_reader_enter_dir(txt_reader *r, const char *subdir)
{
	char next[TXT_PATH_MAX];

	if (!r || !subdir || !subdir[0] || r->dir_depth >= TXT_MAX_DIR_DEPTH)
		return 0;
	if (r->dir_path[0]) {
		int n = snprintf(next, sizeof(next), "%s\\%s", r->dir_path, subdir);
		if (n <= 0 || (size_t)n >= sizeof(next))
			return 0;
	} else {
		int n = snprintf(next, sizeof(next), "%s", subdir);
		if (n <= 0 || (size_t)n >= sizeof(next))
			return 0;
	}
	/* Verify full wildcard path fits ClassPad's 160-byte BFile limit. */
	if (strlen(next) + 12u >= 159u)
		return 0;
	snprintf(r->dir_path, sizeof(r->dir_path), "%s", next);
	r->dir_depth++;
	txt_reader_scan_dir(r);
	return 1;
}

int txt_reader_up_dir(txt_reader *r)
{
	if (!r || !r->dir_path[0])
		return 0;
	char *sep = strrchr(r->dir_path, '\\');
	if (sep)
		*sep = 0;
	else
		r->dir_path[0] = 0;
	if (r->dir_depth > 0)
		r->dir_depth--;
	txt_reader_scan_dir(r);
	return 1;
}

void txt_reader_display_dir(const txt_reader *r, char *dst, size_t dst_size)
{
	if (!dst || dst_size == 0)
		return;
	if (!r || !r->dir_path[0])
		snprintf(dst, dst_size, "\\fls0\\");
	else
		snprintf(dst, dst_size, "\\fls0\\%s\\", r->dir_path);
}

/* ---- streaming document I/O ----------------------------------------------- */

static int remap_active_doc(txt_reader *r)
{
	uint32_t new_size = 0;

	if (!r->has_doc)
		return 0;
	fxcg100_wait_keys_released();
	r->cache_block_idx = UINT32_MAX;
	r->cache_len = 0;
	if (fxcg100_fs_open_map(r->doc_fc_path, &new_size,
			r->nor_blocks, TXT_MAX_NOR_BLOCKS, &r->mapped_blocks,
			r->tail_buf, &r->tail_block_idx, &r->tail_len) < 0)
		return 0;
	r->doc_size = new_size;
	r->storage_gen = fxcg100_storage_mutation_generation();
	return 1;
}

void txt_reader_sync_storage(txt_reader *r)
{
	if (!r || !r->has_doc)
		return;
	if (r->storage_gen != fxcg100_storage_mutation_generation()) {
		if (!remap_active_doc(r)) {
			/* Fallback: drop NOR pointers so reads use safe BFile ranges. */
			memset(r->nor_blocks, 0, sizeof(r->nor_blocks));
			r->mapped_blocks = 0;
			r->tail_block_idx = UINT32_MAX;
			r->cache_block_idx = UINT32_MAX;
			r->storage_gen = fxcg100_storage_mutation_generation();
		}
	}
}

static uint32_t doc_read(txt_reader *r, uint32_t offset, uint8_t *dst,
	uint32_t len)
{
	uint32_t done = 0;

	if (!r || !r->has_doc || !dst || offset >= r->doc_size)
		return 0;
	if (len > r->doc_size - offset)
		len = r->doc_size - offset;
	txt_reader_sync_storage(r);

	while (done < len) {
		uint32_t pos = offset + done;
		uint32_t blk = pos / TXT_FLASH_BLOCK_SIZE;
		uint32_t in_blk = pos % TXT_FLASH_BLOCK_SIZE;
		uint32_t avail = TXT_FLASH_BLOCK_SIZE - in_blk;
		uint32_t chunk = (len - done < avail) ? (len - done) : avail;

		if (blk < r->mapped_blocks && r->nor_blocks[blk] != NULL) {
			memcpy(dst + done, r->nor_blocks[blk] + in_blk, chunk);
			done += chunk;
			continue;
		}
		if (blk == r->tail_block_idx && in_blk < r->tail_len) {
			uint32_t tavail = r->tail_len - in_blk;
			if (chunk > tavail)
				chunk = tavail;
			memcpy(dst + done, r->tail_buf + in_blk, chunk);
			done += chunk;
			continue;
		}
		if (blk != r->cache_block_idx) {
			uint32_t blk_off = blk * TXT_FLASH_BLOCK_SIZE;
			uint32_t want = r->doc_size - blk_off;
			if (want > TXT_FLASH_BLOCK_SIZE)
				want = TXT_FLASH_BLOCK_SIZE;
			fxcg100_wait_keys_released();
			int got = fxcg100_fs_read_range(r->doc_fc_path, blk_off,
				r->cache_buf, want);
			if (got <= 0)
				break;
			r->cache_block_idx = blk;
			r->cache_len = (uint32_t)got;
		}
		if (in_blk >= r->cache_len)
			break;
		if (chunk > r->cache_len - in_blk)
			chunk = r->cache_len - in_blk;
		memcpy(dst + done, r->cache_buf + in_blk, chunk);
		done += chunk;
	}
	return done;
}

int txt_reader_open(txt_reader *r, const char *rel_path)
{
	uint16_t fc_path[160];
	uint32_t sz = 0;
	int existed = 0;

	if (!r || !rel_path || !rel_path[0])
		return 0;
	if (r->has_doc)
		meta_sync_active_fields(r);

	if (!build_fc_path(fc_path, sizeof(fc_path) / sizeof(fc_path[0]),
			rel_path, NULL))
		return 0;

	fxcg100_wait_keys_released();
	r->cache_block_idx = UINT32_MAX;
	r->cache_len = 0;
	if (fxcg100_fs_open_map(fc_path, &sz, r->nor_blocks, TXT_MAX_NOR_BLOCKS,
			&r->mapped_blocks, r->tail_buf, &r->tail_block_idx,
			&r->tail_len) < 0)
		return 0;

	r->has_doc = 1;
	r->doc_size = sz;
	r->storage_gen = fxcg100_storage_mutation_generation();
	snprintf(r->doc_path, sizeof(r->doc_path), "%s", rel_path);
	{
		const char *base = strrchr(rel_path, '\\');
		if (!base)
			base = strrchr(rel_path, '/');
		snprintf(r->doc_name, sizeof(r->doc_name), "%s",
			base ? base + 1 : rel_path);
	}
	memcpy(r->doc_fc_path, fc_path, sizeof(r->doc_fc_path));

	r->active_meta_slot = (int16_t)meta_find_or_alloc(r, rel_path, sz, &existed);
	if (existed && r->active_meta_slot >= 0) {
		const txt_meta_file_rec *rec = &r->meta.files[r->active_meta_slot];
		r->top_offset = rec->offset < sz ? rec->offset : 0u;
		r->font_idx = rec->font_idx < TXT_FONT_COUNT ? rec->font_idx : TXT_FONT_6X12;
		r->wrap = rec->wrap ? 1u : 0u;
	} else {
		r->top_offset = 0;
	}
	r->pan_x = 0;
	r->has_undo = 0;
	r->prev_head = 0;
	r->prev_count = 0;
	r->show_hints = 1;
	r->layout_dirty = 1;
	return 1;
}

void txt_reader_on_enter(txt_reader *r)
{
	if (!r)
		return;
	r->show_hints = 1;
	r->layout_dirty = 1;
}

/* ---- line parsing, wrapping & paging -------------------------------------- */

static void push_prev(txt_reader *r, uint32_t off)
{
	r->prev_stack[r->prev_head] = off;
	r->prev_head = (uint8_t)((r->prev_head + 1u) % TXT_PREV_STACK_SIZE);
	if (r->prev_count < TXT_PREV_STACK_SIZE)
		r->prev_count++;
}

static int pop_prev(txt_reader *r, uint32_t *out)
{
	if (r->prev_count == 0)
		return 0;
	r->prev_head = (uint8_t)((r->prev_head + TXT_PREV_STACK_SIZE - 1u) %
		TXT_PREV_STACK_SIZE);
	r->prev_count--;
	*out = r->prev_stack[r->prev_head];
	return 1;
}

/* Parse one visual line starting at `off`. Always advances next_offset > off
 * while off < doc_size so layout loops can never stall. */
static void parse_visual_line(txt_reader *r, uint32_t off, int cols, int wrap,
	uint32_t pan_x, txt_vis_line *out)
{
	uint8_t buf[256];
	int text_max = cols < (int)(TXT_LINE_TEXT_MAX - 1u)
		? cols : (int)(TXT_LINE_TEXT_MAX - 1u);

	out->offset = off;
	out->next_offset = off;
	out->text[0] = 0;
	if (off >= r->doc_size || text_max <= 0)
		return;

	if (wrap) {
		uint32_t got = doc_read(r, off, buf, sizeof(buf));
		int col = 0;
		int last_sp_col = -1;
		uint32_t last_sp_next = 0;
		uint32_t i = 0;

		while (i < got) {
			uint8_t c = buf[i];
			if (c == '\r') {
				i++;
				if (i < got && buf[i] == '\n')
					i++;
				out->text[col] = 0;
				out->next_offset = off + i;
				return;
			}
			if (c == '\n') {
				i++;
				out->text[col] = 0;
				out->next_offset = off + i;
				return;
			}
			if (c == '\t') {
				int spaces = 4 - (col & 3);
				if (col + spaces > text_max) {
					if (last_sp_col > 0) {
						out->text[last_sp_col] = 0;
						out->next_offset = off + last_sp_next;
					} else {
						out->text[col] = 0;
						out->next_offset = off + i + (i == 0 ? 1u : 0u);
					}
					return;
				}
				last_sp_col = col;
				last_sp_next = i + 1u;
				while (spaces-- > 0 && col < text_max)
					out->text[col++] = ' ';
				i++;
				continue;
			}
			char ch = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
			if (col >= text_max) {
				if (c == ' ') {
					out->text[col] = 0;
					out->next_offset = off + i + 1u;
					return;
				}
				if (last_sp_col > 0) {
					out->text[last_sp_col] = 0;
					out->next_offset = off + last_sp_next;
					return;
				}
				out->text[col] = 0;
				out->next_offset = off + i;
				return;
			}
			if (ch == ' ') {
				last_sp_col = col;
				last_sp_next = i + 1u;
			}
			out->text[col++] = ch;
			i++;
		}
		out->text[col] = 0;
		out->next_offset = off + (i > 0 ? i : 1u);
		return;
	}

	/* Wrap Off: scan to the next newline (bounded at 4 KiB per logical line),
	 * capturing columns pan_x .. pan_x + text_max - 1. */
	uint32_t pos = off;
	uint32_t vcol = 0;
	int out_len = 0;
	uint32_t scanned = 0;
	while (pos < r->doc_size && scanned < 4096u) {
		uint32_t got = doc_read(r, pos, buf, sizeof(buf));
		if (got == 0)
			break;
		for (uint32_t i = 0; i < got; i++) {
			uint8_t c = buf[i];
			pos++;
			scanned++;
			if (c == '\r') {
				if (i + 1u < got) {
					if (buf[i + 1u] == '\n')
						pos++;
				} else if (pos < r->doc_size) {
					uint8_t lf = 0;
					if (doc_read(r, pos, &lf, 1u) == 1u && lf == '\n')
						pos++;
				}
				out->text[out_len] = 0;
				out->next_offset = pos;
				return;
			}
			if (c == '\n') {
				out->text[out_len] = 0;
				out->next_offset = pos;
				return;
			}
			if (c == '\t') {
				uint32_t spaces = 4u - (vcol & 3u);
				while (spaces-- > 0) {
					if (vcol >= pan_x && out_len < text_max)
						out->text[out_len++] = ' ';
					vcol++;
				}
				continue;
			}
			char ch = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
			if (vcol >= pan_x && out_len < text_max)
				out->text[out_len++] = ch;
			vcol++;
		}
	}
	out->text[out_len] = 0;
	out->next_offset = (pos > off) ? pos : (off + 1u);
}

void txt_reader_layout(txt_reader *r, int cols, int vis_lines)
{
	if (!r || !r->has_doc)
		return;
	if (cols < 8)
		cols = 8;
	if (cols > (int)(TXT_LINE_TEXT_MAX - 1u))
		cols = (int)(TXT_LINE_TEXT_MAX - 1u);
	if (vis_lines < 1)
		vis_lines = 1;
	if (vis_lines > (int)TXT_MAX_VIS_LINES)
		vis_lines = (int)TXT_MAX_VIS_LINES;

	if (!r->layout_dirty && r->layout_cols == cols && r->layout_vis == vis_lines)
		return;

	r->layout_cols = cols;
	r->layout_vis = vis_lines;
	r->layout_dirty = 0;
	r->line_count = 0;
	if (r->top_offset > r->doc_size)
		r->top_offset = 0;

	uint32_t off = r->top_offset;
	while (r->line_count < vis_lines && off < r->doc_size) {
		txt_vis_line *ln = &r->lines[r->line_count];
		parse_visual_line(r, off, cols, r->wrap, r->pan_x, ln);
		r->line_count++;
		if (ln->next_offset <= off)
			break;
		off = ln->next_offset;
	}
	r->end_offset = off;
}

/* Walk backwards up to `lookback` bytes to a clean line boundary, then flow
 * forward to `target_off`, keeping the last `want_lines` line starts in a ring
 * and returning the start offset `step_back` visual lines before `target_off`. */
static uint32_t find_offset_lines_before(txt_reader *r, uint32_t target_off,
	int step_back)
{
	uint32_t ring[TXT_MAX_VIS_LINES + 4u];
	unsigned ring_cap = (unsigned)(TXT_MAX_VIS_LINES + 4u);
	unsigned ring_head = 0, ring_count = 0;
	uint32_t lookback = (uint32_t)(step_back > 1 ? 4096u : 2048u);
	uint32_t start = target_off > lookback ? target_off - lookback : 0u;

	if (target_off == 0u || step_back <= 0)
		return 0u;

	if (start > 0u) {
		uint8_t scan[256];
		uint32_t p = start;
		int found_nl = 0;
		while (p < target_off && !found_nl) {
			uint32_t want = target_off - p;
			if (want > sizeof(scan))
				want = sizeof(scan);
			uint32_t got = doc_read(r, p, scan, want);
			if (got == 0)
				break;
			for (uint32_t i = 0; i < got; i++) {
				if (scan[i] == '\n') {
					start = p + i + 1u;
					found_nl = 1;
					break;
				}
			}
			p += got;
		}
		if (!found_nl)
			start = 0u;
	}

	int cols = r->layout_cols > 0 ? r->layout_cols : 40;
	uint32_t off = start;
	while (off < target_off) {
		txt_vis_line tmp;
		ring[ring_head] = off;
		ring_head = (ring_head + 1u) % ring_cap;
		if (ring_count < ring_cap)
			ring_count++;
		parse_visual_line(r, off, cols, r->wrap, 0u, &tmp);
		if (tmp.next_offset <= off)
			break;
		off = tmp.next_offset;
	}
	if (ring_count == 0u)
		return 0u;
	unsigned back = (unsigned)step_back;
	if (back > ring_count)
		back = ring_count;
	unsigned idx = (ring_head + ring_cap - back) % ring_cap;
	return ring[idx];
}

int txt_reader_scroll_lines(txt_reader *r, int delta)
{
	int moved = 0;

	if (!r || !r->has_doc || delta == 0)
		return 0;
	r->show_hints = 0;
	if (r->layout_dirty)
		txt_reader_layout(r, r->layout_cols > 0 ? r->layout_cols : 40,
			r->layout_vis > 0 ? r->layout_vis : 12);

	if (delta > 0) {
		for (int s = 0; s < delta; s++) {
			if (r->end_offset >= r->doc_size || r->line_count == 0)
				break;
			uint32_t next = r->lines[0].next_offset;
			if (next <= r->top_offset || next >= r->doc_size)
				break;
			r->top_offset = next;
			r->layout_dirty = 1;
			txt_reader_layout(r, r->layout_cols, r->layout_vis);
			moved = 1;
		}
	} else {
		int steps = -delta;
		if (r->top_offset > 0u) {
			uint32_t prev = find_offset_lines_before(r, r->top_offset, steps);
			if (prev != r->top_offset) {
				r->top_offset = prev;
				r->layout_dirty = 1;
				moved = 1;
			}
		}
	}
	if (moved) {
		r->prev_count = 0; /* single-line scroll invalidates page history */
		meta_sync_active_fields(r);
	}
	return moved;
}

int txt_reader_page_down(txt_reader *r)
{
	if (!r || !r->has_doc)
		return 0;
	r->show_hints = 0;
	if (r->layout_dirty)
		txt_reader_layout(r, r->layout_cols > 0 ? r->layout_cols : 40,
			r->layout_vis > 0 ? r->layout_vis : 12);
	if (r->end_offset >= r->doc_size || r->line_count <= 1)
		return 0;

	/* Keep 1-line overlap so reading across page boundaries never drops context. */
	uint32_t next = r->lines[r->line_count - 1].offset;
	if (next <= r->top_offset)
		next = r->lines[0].next_offset;
	if (next <= r->top_offset || next >= r->doc_size)
		return 0;

	push_prev(r, r->top_offset);
	r->top_offset = next;
	r->layout_dirty = 1;
	meta_sync_active_fields(r);
	return 1;
}

int txt_reader_page_up(txt_reader *r)
{
	uint32_t prev = 0;

	if (!r || !r->has_doc)
		return 0;
	r->show_hints = 0;
	if (r->top_offset == 0u)
		return 0;
	if (pop_prev(r, &prev) && prev < r->top_offset) {
		r->top_offset = prev;
		r->layout_dirty = 1;
		meta_sync_active_fields(r);
		return 1;
	}
	int step = r->layout_vis > 2 ? r->layout_vis - 1 : 1;
	prev = find_offset_lines_before(r, r->top_offset, step);
	if (prev == r->top_offset)
		return 0;
	r->top_offset = prev;
	r->layout_dirty = 1;
	meta_sync_active_fields(r);
	return 1;
}

int txt_reader_pan(txt_reader *r, int delta_cols)
{
	if (!r || !r->has_doc || r->wrap)
		return 0;
	r->show_hints = 0;
	int64_t next = (int64_t)r->pan_x + delta_cols;
	if (next < 0)
		next = 0;
	if (next > 512)
		next = 512;
	if ((uint32_t)next == r->pan_x)
		return 0;
	r->pan_x = (uint32_t)next;
	r->layout_dirty = 1;
	return 1;
}

int txt_reader_jump_offset(txt_reader *r, uint32_t offset, int record_undo)
{
	if (!r || !r->has_doc)
		return 0;
	if (offset >= r->doc_size)
		offset = r->doc_size > 0u ? r->doc_size - 1u : 0u;
	if (record_undo) {
		r->undo_offset = r->top_offset;
		r->has_undo = 1;
	}
	r->show_hints = 0;
	r->top_offset = offset;
	r->prev_count = 0;
	r->layout_dirty = 1;
	meta_sync_active_fields(r);
	return 1;
}

int txt_reader_jump_percent(txt_reader *r, int percent)
{
	if (!r || !r->has_doc)
		return 0;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;

	uint32_t target = 0;
	if (percent == 0 || r->doc_size == 0u) {
		target = 0u;
	} else if (percent >= 100) {
		int step = r->layout_vis > 1 ? r->layout_vis - 1 : 1;
		target = find_offset_lines_before(r, r->doc_size, step);
	} else {
		target = (uint32_t)(((uint64_t)r->doc_size * (uint64_t)percent) / 100u);
		/* Align cleanly to the start of the line containing `target`. */
		if (target > 0u && target < r->doc_size) {
			uint32_t back = target > 256u ? 256u : target;
			uint8_t buf[256];
			uint32_t got = doc_read(r, target - back, buf, back);
			uint32_t aligned = target - back;
			for (uint32_t i = got; i > 0u; i--) {
				if (buf[i - 1u] == '\n') {
					aligned = (target - back) + i;
					break;
				}
			}
			target = aligned;
		}
	}
	return txt_reader_jump_offset(r, target, 1);
}

int txt_reader_undo_jump(txt_reader *r)
{
	if (!r || !r->has_doc || !r->has_undo)
		return 0;
	uint32_t target = r->undo_offset;
	r->has_undo = 0;
	return txt_reader_jump_offset(r, target, 0);
}

int txt_reader_percent(const txt_reader *r)
{
	if (!r || !r->has_doc || r->doc_size == 0u)
		return 100;
	if (r->top_offset == 0u)
		return r->end_offset >= r->doc_size ? 100 : 0;
	if (r->end_offset >= r->doc_size)
		return 100;
	int p = (int)(((uint64_t)r->top_offset * 100u) / (uint64_t)r->doc_size);
	if (p < 0) p = 0;
	if (p > 99) p = 99;
	return p;
}

void txt_reader_set_font(txt_reader *r, int font_idx)
{
	uint8_t next = (font_idx == TXT_FONT_8X14) ? TXT_FONT_8X14 : TXT_FONT_6X12;

	if (!r || r->font_idx == next)
		return;
	r->font_idx = next;
	r->prev_count = 0;
	r->layout_dirty = 1;
	meta_sync_active_fields(r);
}

void txt_reader_set_wrap(txt_reader *r, int wrap)
{
	uint8_t next = wrap ? 1u : 0u;

	if (!r || r->wrap == next)
		return;
	r->wrap = next;
	r->pan_x = 0;
	r->prev_count = 0;
	r->layout_dirty = 1;
	meta_sync_active_fields(r);
}

/* ---- dynamic bookmarks (0..32 per file in shared 256-slot pool) ----------- */

unsigned txt_reader_bookmark_count(const txt_reader *r)
{
	unsigned count = 0;

	if (!r || !r->has_doc || r->active_meta_slot < 0)
		return 0;
	uint8_t slot = (uint8_t)r->active_meta_slot;
	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
		if (r->meta.marks[i].file_idx == slot)
			count++;
	}
	return count;
}

unsigned txt_reader_get_bookmarks(const txt_reader *r, uint32_t *out_offsets,
	unsigned max_count)
{
	unsigned count = 0;

	if (!r || !r->has_doc || r->active_meta_slot < 0 || !out_offsets ||
			max_count == 0u)
		return 0;
	uint8_t slot = (uint8_t)r->active_meta_slot;
	for (unsigned i = 0; i < TXT_META_MAX_MARKS && count < max_count; i++) {
		if (r->meta.marks[i].file_idx == slot)
			out_offsets[count++] = r->meta.marks[i].offset;
	}
	/* Insertion sort ascending by byte offset. */
	for (unsigned i = 1; i < count; i++) {
		uint32_t v = out_offsets[i];
		unsigned j = i;
		while (j > 0u && out_offsets[j - 1u] > v) {
			out_offsets[j] = out_offsets[j - 1u];
			j--;
		}
		out_offsets[j] = v;
	}
	return count;
}

int txt_reader_has_visible_bookmark(const txt_reader *r)
{
	if (!r || !r->has_doc || r->active_meta_slot < 0)
		return 0;
	uint8_t slot = (uint8_t)r->active_meta_slot;
	uint32_t lo = r->top_offset;
	uint32_t hi = r->end_offset > lo ? r->end_offset : lo + 1u;
	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
		if (r->meta.marks[i].file_idx == slot &&
				r->meta.marks[i].offset >= lo &&
				r->meta.marks[i].offset < hi)
			return 1;
	}
	return 0;
}

int txt_reader_add_bookmark(txt_reader *r)
{
	if (!r || !r->has_doc || r->active_meta_slot < 0)
		return TXT_BM_ERROR;

	uint8_t slot = (uint8_t)r->active_meta_slot;
	unsigned owned = 0;
	int free_idx = -1;

	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
		if (r->meta.marks[i].file_idx == slot) {
			if (r->meta.marks[i].offset == r->top_offset)
				return TXT_BM_EXISTS;
			owned++;
		} else if (r->meta.marks[i].file_idx == 0xFFu && free_idx < 0) {
			free_idx = (int)i;
		}
	}
	if (owned >= TXT_MAX_BOOKMARKS)
		return TXT_BM_FULL;

	if (free_idx < 0) {
		/* Shared pool full: reclaim a slot from the oldest other file. */
		uint32_t oldest_seq = UINT32_MAX;
		int victim = -1;
		for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
			uint8_t owner = r->meta.marks[i].file_idx;
			if (owner != slot && owner < r->meta.file_count &&
					r->meta.files[owner].last_opened < oldest_seq) {
				oldest_seq = r->meta.files[owner].last_opened;
				victim = (int)i;
			}
		}
		if (victim < 0)
			return TXT_BM_FULL;
		free_idx = victim;
	}

	r->meta.marks[free_idx].file_idx = slot;
	r->meta.marks[free_idx].offset = r->top_offset;
	r->meta_dirty = 1;
	return TXT_BM_ADDED;
}

int txt_reader_remove_bookmark(txt_reader *r, unsigned index)
{
	uint32_t offsets[TXT_MAX_BOOKMARKS];
	unsigned count;

	if (!r || !r->has_doc || r->active_meta_slot < 0)
		return 0;
	count = txt_reader_get_bookmarks(r, offsets, TXT_MAX_BOOKMARKS);
	if (index >= count)
		return 0;

	uint8_t slot = (uint8_t)r->active_meta_slot;
	uint32_t target = offsets[index];
	for (unsigned i = 0; i < TXT_META_MAX_MARKS; i++) {
		if (r->meta.marks[i].file_idx == slot &&
				r->meta.marks[i].offset == target) {
			r->meta.marks[i].file_idx = 0xFFu;
			r->meta_dirty = 1;
			return 1;
		}
	}
	return 0;
}

void txt_reader_preview_at(txt_reader *r, uint32_t offset, char *dst,
	size_t dst_size)
{
	uint8_t buf[96];
	uint32_t got;
	size_t out = 0;

	if (!dst || dst_size == 0)
		return;
	dst[0] = 0;
	if (!r || !r->has_doc || offset >= r->doc_size)
		return;

	got = doc_read(r, offset, buf, sizeof(buf));
	/* Skip leading whitespace/newlines so preview always shows readable text. */
	uint32_t i = 0;
	while (i < got && (buf[i] == ' ' || buf[i] == '\t' ||
			buf[i] == '\r' || buf[i] == '\n'))
		i++;
	while (i < got && out + 1u < dst_size) {
		uint8_t c = buf[i++];
		if (c == '\r' || c == '\n')
			break;
		if (c == '\t')
			c = ' ';
		dst[out++] = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
	}
	dst[out] = 0;
	if (out == 0)
		snprintf(dst, dst_size, "Offset %u", (unsigned)offset);
}
