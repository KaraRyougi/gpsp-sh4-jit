#ifndef CGBA_GINT_GPSP_SAVE_PATH_H
#define CGBA_GINT_GPSP_SAVE_PATH_H

/*
 * Save (.SAV) and savestate (.SVS) path derivation for calculator storage.
 *
 * Files live under \\fls0\SAVES\ so the storage root stays uncluttered and
 * long filenames do not consume fixed FAT16 root-directory slots:
 *   - In-game backup save: \\fls0\SAVES\<TITLE>.SAV
 *   - Savestate slot 0..9: \\fls0\SAVES\<TITLE>_<slot>.SVS
 *
 * <TITLE> is extracted from the GBA cartridge's 12-byte internal title at
 * ROM header offset 0xA0..0xAB with spaces replaced by underscores (e.g.
 * "ZELDA MINISH" -> ZELDA_MINISH, "ZELDA ALTTP " -> ZELDA_ALTTP). Every
 * emitted filename uses only uppercase ASCII [A-Z0-9_-] so it works across
 * fx-CG50, fx-CG100, and ClassPad II regardless of ROM filename language.
 *
 * If the header has no valid internal title (homebrew), naming falls back to
 * the 4-character GBA game code at 0xAC..0xAF, and finally to up to 6 ASCII
 * alphanumeric characters from the ROM filename stem.
 *
 * Legacy root-directory files (\\fls0\<BASE6>.SAV and \\fls0\<BASE6><slot>.SVS)
 * are still accepted on load when no other scanned ROM maps to the same legacy
 * stem, allowing single-game saves to migrate without cross-loading between
 * colliding series titles.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nor_rom.h"

/* Max path length: "\\fls0\SAVES\" (13) + title (12) + "_0" (2) + ".SVS" (4)
 * + NUL (1) = 32 uint16_t entries. */
#define CGBA_SAVE_PATH_MAX 40u

static inline int cgba_save_extract_title(char out[13],
	const uint8_t *rom_header, size_t header_len)
{
	unsigned n = 0;
	int has_alnum = 0;

	if (!out)
		return 0;
	out[0] = 0;
	if (!rom_header || header_len < 0xACu)
		return 0;

	for (unsigned i = 0; i < 12u; i++) {
		uint8_t b = rom_header[0xA0u + i];
		if (b == 0)
			break;
		if (b >= 'a' && b <= 'z')
			b = (uint8_t)(b - 'a' + 'A');
		if ((b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9')) {
			out[n++] = (char)b;
			has_alnum = 1;
		} else if (b == '-') {
			if (n > 0 && out[n - 1u] != '-' && out[n - 1u] != '_')
				out[n++] = '-';
		} else if (b == ' ' || b == '_' || b == '.' || b == ':' ||
				b == '/' || b == '\\' || b == '&') {
			if (n > 0 && out[n - 1u] != '_' && out[n - 1u] != '-')
				out[n++] = '_';
		}
	}
	while (n > 0 && (out[n - 1u] == '_' || out[n - 1u] == '-'))
		n--;
	if (!has_alnum || n == 0) {
		out[0] = 0;
		return 0;
	}
	out[n] = 0;
	return 1;
}

static inline int cgba_save_extract_game_code(char out[5],
	const uint8_t *rom_header, size_t header_len)
{
	int has_alpha = 0;

	if (!out)
		return 0;
	out[0] = 0;
	if (!rom_header || header_len < 0xB0u)
		return 0;

	for (unsigned i = 0; i < 4u; i++) {
		uint8_t b = rom_header[0xACu + i];
		if (b >= 'a' && b <= 'z')
			b = (uint8_t)(b - 'a' + 'A');
		if (b >= 'A' && b <= 'Z') {
			has_alpha = 1;
		} else if (b < '0' || b > '9') {
			out[0] = 0;
			return 0;
		}
		out[i] = (char)b;
	}
	if (!has_alpha) {
		out[0] = 0;
		return 0;
	}
	out[4] = 0;
	return 1;
}

static inline unsigned cgba_save_legacy_stem(char out[7],
	const char *rom_name, const char *fallback)
{
	unsigned i, b = 0;

	for (i = 0; rom_name && rom_name[i] && rom_name[i] != '.' && b < 6u; i++) {
		char c = rom_name[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			out[b++] = c;
	}
	if (b == 0 && fallback) {
		for (i = 0; fallback[i] && b < 6u; i++)
			out[b++] = fallback[i];
	}
	out[b] = 0;
	return b;
}

static inline int cgba_save_legacy_stem_is_unique(
	const cgba_nor_rom_list *roms, const char *rom_name,
	const char *fallback)
{
	char target[7];
	unsigned matches = 0;

	if (!roms || roms->count == 0)
		return 1;

	cgba_save_legacy_stem(target, rom_name, fallback);
	for (uint32_t i = 0; i < roms->count; i++) {
		char stem[7];
		cgba_save_legacy_stem(stem, roms->entries[i].label, fallback);
		if (strcmp(stem, target) == 0) {
			matches++;
			if (matches > 1u)
				return 0;
		}
	}
	return 1;
}

static inline void cgba_save_primary_base(char out[13],
	const uint8_t *rom_header, size_t header_len,
	const char *rom_name, const char *fallback)
{
	if (cgba_save_extract_title(out, rom_header, header_len))
		return;
	if (cgba_save_extract_game_code(out, rom_header, header_len))
		return;
	cgba_save_legacy_stem(out, rom_name, fallback);
}

static inline void cgba_save_format_path(uint16_t *path, const char *prefix,
	const char *base, int slot, int slot_sep, const char *ext)
{
	unsigned i = 0, j;

	for (j = 0; prefix[j]; j++)
		path[i++] = (uint16_t)(unsigned char)prefix[j];
	for (j = 0; base[j]; j++)
		path[i++] = (uint16_t)(unsigned char)base[j];
	if (slot >= 0) {
		if (slot_sep)
			path[i++] = (uint16_t)(unsigned char)slot_sep;
		path[i++] = (uint16_t)('0' + ((unsigned)slot % 10u));
	}
	for (j = 0; ext[j]; j++)
		path[i++] = (uint16_t)(unsigned char)ext[j];
	path[i] = 0;
}

static inline void cgba_save_state_path(uint16_t *path,
	const uint8_t *rom_header, size_t header_len,
	const char *rom_name, unsigned slot)
{
	char base[13];

	cgba_save_primary_base(base, rom_header, header_len, rom_name, "CGBA");
	cgba_save_format_path(path, "\\\\fls0\\SAVES\\", base,
		(int)(slot % 10u), '_', ".SVS");
}

static inline void cgba_save_backup_path(uint16_t *path,
	const uint8_t *rom_header, size_t header_len,
	const char *rom_name)
{
	char base[13];

	cgba_save_primary_base(base, rom_header, header_len, rom_name, "GAME");
	cgba_save_format_path(path, "\\\\fls0\\SAVES\\", base, -1, 0, ".SAV");
}

/* Populate the previous root-directory 6-char ROM-stem savestate path when no
 * other scanned ROM shares that legacy stem. Returns 1 if written, 0 else. */
static inline int cgba_save_legacy_state_path(uint16_t *path,
	const uint8_t *rom_header, size_t header_len,
	const char *rom_name, const cgba_nor_rom_list *roms, unsigned slot)
{
	char legacy[7];

	(void)rom_header;
	(void)header_len;
	if (!cgba_save_legacy_stem_is_unique(roms, rom_name, "CGBA"))
		return 0;
	cgba_save_legacy_stem(legacy, rom_name, "CGBA");
	cgba_save_format_path(path, "\\\\fls0\\", legacy,
		(int)(slot % 10u), 0, ".SVS");
	return 1;
}

/* Populate the previous root-directory 6-char ROM-stem backup path under the
 * same uniqueness guard. Returns 1 if written, 0 otherwise. */
static inline int cgba_save_legacy_backup_path(uint16_t *path,
	const uint8_t *rom_header, size_t header_len,
	const char *rom_name, const cgba_nor_rom_list *roms)
{
	char legacy[7];

	(void)rom_header;
	(void)header_len;
	if (!cgba_save_legacy_stem_is_unique(roms, rom_name, "GAME"))
		return 0;
	cgba_save_legacy_stem(legacy, rom_name, "GAME");
	cgba_save_format_path(path, "\\\\fls0\\", legacy, -1, 0, ".SAV");
	return 1;
}

#endif
