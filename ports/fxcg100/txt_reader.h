#ifndef CGBA_TXT_READER_H
#define CGBA_TXT_READER_H

#include <stddef.h>
#include <stdint.h>

#include "txt_font_terminus.h"

#define TXT_MAX_DIR_ENTRIES  64u
#define TXT_MAX_DIR_DEPTH    6u
#define TXT_NAME_MAX         36u
#define TXT_PATH_MAX         144u

#define TXT_FLASH_BLOCK_SIZE 4096u
#define TXT_MAX_NOR_BLOCKS   512u   /* up to 2 MiB direct NOR mapped */
#define TXT_MAX_VIS_LINES    44u
#define TXT_LINE_TEXT_MAX    96u
#define TXT_PREV_STACK_SIZE  64u

#define TXT_META_MAGIC       0x43475458u /* "CGTX" */
#define TXT_META_VERSION     1u
#define TXT_META_MAX_FILES   32u
#define TXT_META_MAX_MARKS   256u
#define TXT_MAX_BOOKMARKS    32u

enum {
	TXT_FONT_6X12 = 0,
	TXT_FONT_8X14 = 1,
	TXT_FONT_COUNT = 2
};

enum {
	TXT_BM_ADDED = 0,
	TXT_BM_EXISTS = 1,
	TXT_BM_FULL = 2,
	TXT_BM_ERROR = -1
};

typedef struct txt_dir_entry {
	char name[TXT_NAME_MAX];
	uint32_t size;
	uint8_t is_dir;
} txt_dir_entry;

typedef struct txt_vis_line {
	uint32_t offset;
	uint32_t next_offset;
	char text[TXT_LINE_TEXT_MAX];
} txt_vis_line;

typedef struct txt_meta_file_rec {
	uint32_t hash_lo;
	uint32_t hash_hi;
	uint32_t offset;
	uint32_t file_size;
	uint32_t last_opened;
	uint8_t font_idx;
	uint8_t wrap;
	uint8_t reserved[2];
} txt_meta_file_rec;

typedef struct txt_meta_mark_rec {
	uint8_t file_idx; /* 0..31 = owner file slot, 0xFF = free */
	uint8_t reserved[3];
	uint32_t offset;
} txt_meta_mark_rec;

typedef struct txt_meta_blob {
	uint32_t magic;
	uint16_t version;
	uint16_t file_count;
	uint32_t open_seq;
	uint32_t reserved;
	txt_meta_file_rec files[TXT_META_MAX_FILES];
	txt_meta_mark_rec marks[TXT_META_MAX_MARKS];
} txt_meta_blob;

typedef struct txt_reader {
	/* Directory browser */
	char dir_path[TXT_PATH_MAX]; /* relative to \fls0\, "" = root */
	uint8_t dir_depth;
	uint8_t dir_scanned;
	uint16_t dir_count;
	txt_dir_entry dir_entries[TXT_MAX_DIR_ENTRIES];

	/* Active document I/O */
	uint8_t has_doc;
	uint8_t font_idx;            /* TXT_FONT_6X12 or TXT_FONT_8X14 */
	uint8_t wrap;                /* 1 = word wrap, 0 = no-wrap with pan */
	uint8_t show_hints;          /* 1 on enter, 0 once scrolled/paged */
	char doc_path[TXT_PATH_MAX]; /* relative to \fls0\ */
	char doc_name[TXT_NAME_MAX]; /* basename */
	uint16_t doc_fc_path[160];   /* "\\fls0\\..." FONTCHARACTER path */
	uint32_t doc_size;
	uint32_t storage_gen;

	const uint8_t *nor_blocks[TXT_MAX_NOR_BLOCKS];
	uint32_t mapped_blocks;
	uint8_t tail_buf[TXT_FLASH_BLOCK_SIZE];
	uint32_t tail_block_idx;
	uint32_t tail_len;
	uint8_t cache_buf[TXT_FLASH_BLOCK_SIZE];
	uint32_t cache_block_idx;
	uint32_t cache_len;

	/* Reading viewport & history */
	uint32_t top_offset;
	uint32_t pan_x;
	uint32_t undo_offset;
	uint8_t has_undo;
	uint32_t prev_stack[TXT_PREV_STACK_SIZE];
	uint8_t prev_head;
	uint8_t prev_count;

	int layout_cols;
	int layout_vis;
	int layout_dirty;
	int line_count;
	uint32_t end_offset;
	txt_vis_line lines[TXT_MAX_VIS_LINES];

	/* Persistent metadata (\fls0\CGBATXT.DAT) */
	uint8_t meta_loaded;
	uint8_t meta_dirty;
	int16_t active_meta_slot;
	txt_meta_blob meta;
} txt_reader;

/* Shared singleton instance (placed in ordinary BSS: 491 KiB RAM on fx-CG,
 * arena .cgba.bss on ClassPad II so it never consumes ClassPad low RAM). */
txt_reader *txt_reader_global(void);

void txt_reader_init(txt_reader *r);
const txt_bitmap_font *txt_reader_font(const txt_reader *r);
void txt_format_size(uint32_t bytes, char *dst, size_t dst_size);

/* Directory navigation (relative to \fls0\). */
int txt_reader_scan_dir(txt_reader *r);
int txt_reader_enter_dir(txt_reader *r, const char *subdir);
int txt_reader_up_dir(txt_reader *r);
void txt_reader_display_dir(const txt_reader *r, char *dst, size_t dst_size);

/* Document lifecycle & storage sync. */
int txt_reader_open(txt_reader *r, const char *rel_path);
void txt_reader_on_enter(txt_reader *r);
void txt_reader_flush(txt_reader *r);
void txt_reader_sync_storage(txt_reader *r);

/* Layout & navigation. */
void txt_reader_layout(txt_reader *r, int cols, int vis_lines);
int txt_reader_scroll_lines(txt_reader *r, int delta);
int txt_reader_page_down(txt_reader *r);
int txt_reader_page_up(txt_reader *r);
int txt_reader_pan(txt_reader *r, int delta_cols);
int txt_reader_jump_percent(txt_reader *r, int percent);
int txt_reader_jump_offset(txt_reader *r, uint32_t offset, int record_undo);
int txt_reader_undo_jump(txt_reader *r);
int txt_reader_percent(const txt_reader *r);
void txt_reader_set_font(txt_reader *r, int font_idx);
void txt_reader_set_wrap(txt_reader *r, int wrap);

/* Dynamic bookmarks for the active document (0..32). */
unsigned txt_reader_bookmark_count(const txt_reader *r);
unsigned txt_reader_get_bookmarks(const txt_reader *r, uint32_t *out_offsets,
                                  unsigned max_count);
int txt_reader_has_visible_bookmark(const txt_reader *r);
int txt_reader_add_bookmark(txt_reader *r);
int txt_reader_remove_bookmark(txt_reader *r, unsigned index);
void txt_reader_preview_at(txt_reader *r, uint32_t offset, char *dst,
                           size_t dst_size);

#endif /* CGBA_TXT_READER_H */
