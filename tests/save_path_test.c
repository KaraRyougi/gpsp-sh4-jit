/*
 * Host test for GBA internal-title save (.SAV) and savestate (.SVS) naming
 * under \\fls0\SAVES\, space-to-underscore sanitization, homebrew header
 * fallbacks, and collision-safe legacy root-directory fallback.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/gint-gpsp/src/save_path.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		putchar('\n'); \
	} \
} while (0)

static void make_header(uint8_t hdr[0xC0], const char *title, const char *code)
{
	memset(hdr, 0, 0xC0u);
	hdr[0xB2] = 0x96;
	if (title) {
		size_t n = strlen(title);
		if (n > 12u)
			n = 12u;
		memcpy(hdr + 0xA0, title, n);
	}
	if (code) {
		size_t n = strlen(code);
		if (n > 4u)
			n = 4u;
		memcpy(hdr + 0xAC, code, n);
	}
}

static void fc_to_ascii(char *dst, size_t dst_size, const uint16_t *src)
{
	size_t i = 0;

	while (src[i] && i + 1u < dst_size) {
		dst[i] = (src[i] >= 0x20u && src[i] < 0x7Fu) ? (char)src[i] : '?';
		i++;
	}
	dst[i] = 0;
}

static void check_safe_ascii_path(const char *path_str)
{
	const char *name;
	const char *dot;

	CHECK(strncmp(path_str, "\\\\fls0\\", 7u) == 0,
		"missing \\\\fls0\\ prefix: %s", path_str);
	name = strrchr(path_str, '\\');
	CHECK(name != NULL && name[1] != 0, "missing filename: %s", path_str);
	if (!name)
		return;
	name++;
	dot = strchr(name, '.');
	CHECK(dot != NULL && strchr(dot + 1, '.') == NULL,
		"expected single dot in filename: %s", path_str);
	if (!dot)
		return;
	CHECK(dot > name && strlen(dot + 1) == 3u,
		"bad stem/ext lengths: %s", path_str);
	for (const char *p = name; p < dot; p++) {
		char c = *p;
		CHECK((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-',
			"unsafe char '%c' in filename stem: %s", c, path_str);
	}
	for (const char *p = dot + 1; *p; p++) {
		CHECK(*p >= 'A' && *p <= 'Z',
			"non-upper char '%c' in ext: %s", *p, path_str);
	}
}

static void check_path_eq(const uint16_t *path, const char *expected)
{
	char got[64];

	fc_to_ascii(got, sizeof(got), path);
	CHECK(strcmp(got, expected) == 0, "expected %s, got %s", expected, got);
	check_safe_ascii_path(got);
}

static void add_rom(cgba_nor_rom_list *list, const char *label)
{
	cgba_nor_rom_entry *e = &list->entries[list->count++];

	memset(e, 0, sizeof(*e));
	snprintf(e->label, sizeof(e->label), "%s", label);
}

static void test_zelda_series_separation(void)
{
	uint8_t minish_hdr[0xC0], alttp_hdr[0xC0], nes_hdr[0xC0], emerald_hdr[0xC0];
	cgba_nor_rom_list roms;
	uint16_t path[CGBA_SAVE_PATH_MAX];

	make_header(minish_hdr, "ZELDA MINISH", "BZME");
	make_header(alttp_hdr, "ZELDA ALTTP ", "AZLE");
	make_header(nes_hdr, "NES ZELDA   ", "FZLE");
	make_header(emerald_hdr, "POKEMON EMER", "BPEE");

	memset(&roms, 0, sizeof(roms));
	add_rom(&roms, "The Legend of Zelda - The Minish Cap (USA).gba");
	add_rom(&roms, "The Legend of Zelda - A Link to the Past (USA).gba");
	add_rom(&roms, "The Legend of Zelda - Classic NES Series.gba");
	add_rom(&roms, "Pokemon - Emerald Version (USA).gba");

	/* Each Zelda title gets its own readable SAVES\<TITLE> files. */
	cgba_save_backup_path(path, minish_hdr, sizeof(minish_hdr),
		roms.entries[0].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_MINISH.SAV");

	cgba_save_backup_path(path, alttp_hdr, sizeof(alttp_hdr),
		roms.entries[1].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_ALTTP.SAV");

	cgba_save_backup_path(path, nes_hdr, sizeof(nes_hdr),
		roms.entries[2].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\NES_ZELDA.SAV");

	for (unsigned slot = 0; slot < 10u; slot++) {
		char exp_minish[48], exp_alttp[48];

		snprintf(exp_minish, sizeof(exp_minish),
			"\\\\fls0\\SAVES\\ZELDA_MINISH_%u.SVS", slot);
		snprintf(exp_alttp, sizeof(exp_alttp),
			"\\\\fls0\\SAVES\\ZELDA_ALTTP_%u.SVS", slot);

		cgba_save_state_path(path, minish_hdr, sizeof(minish_hdr),
			roms.entries[0].label, slot);
		check_path_eq(path, exp_minish);

		cgba_save_state_path(path, alttp_hdr, sizeof(alttp_hdr),
			roms.entries[1].label, slot);
		check_path_eq(path, exp_alttp);
	}

	/* Slot wraps modulo 10. */
	cgba_save_state_path(path, minish_hdr, sizeof(minish_hdr),
		roms.entries[0].label, 17u);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_MINISH_7.SVS");

	/* Because all three Zelda ROMs share the legacy "THELEG" prefix, legacy
	 * fallback must be refused so they cannot cross-load each other's saves. */
	CHECK(cgba_save_legacy_backup_path(path, minish_hdr, sizeof(minish_hdr),
		roms.entries[0].label, &roms) == 0,
		"colliding Zelda backup legacy fallback must be suppressed");
	CHECK(cgba_save_legacy_state_path(path, alttp_hdr, sizeof(alttp_hdr),
		roms.entries[1].label, &roms, 0u) == 0,
		"colliding Zelda savestate legacy fallback must be suppressed");

	/* Emerald is the only ROM with legacy stem "POKEMO", so its old root
	 * saves still migrate safely on load. */
	cgba_save_backup_path(path, emerald_hdr, sizeof(emerald_hdr),
		roms.entries[3].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\POKEMON_EMER.SAV");

	CHECK(cgba_save_legacy_backup_path(path, emerald_hdr, sizeof(emerald_hdr),
		roms.entries[3].label, &roms) == 1,
		"unambiguous Emerald backup legacy fallback should work");
	check_path_eq(path, "\\\\fls0\\POKEMO.SAV");

	CHECK(cgba_save_legacy_state_path(path, emerald_hdr, sizeof(emerald_hdr),
		roms.entries[3].label, &roms, 4u) == 1,
		"unambiguous Emerald savestate legacy fallback should work");
	check_path_eq(path, "\\\\fls0\\POKEMO4.SVS");
}

static void test_title_sanitization(void)
{
	uint8_t hdr[0xC0];
	uint16_t path[CGBA_SAVE_PATH_MAX];

	/* Lowercase title and existing underscores. */
	make_header(hdr, "Golden_Sun_A", "AGSE");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "Golden Sun.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\GOLDEN_SUN_A.SAV");
	cgba_save_state_path(path, hdr, sizeof(hdr), "Golden Sun.gba", 2u);
	check_path_eq(path, "\\\\fls0\\SAVES\\GOLDEN_SUN_A_2.SVS");

	/* Leading, trailing, and repeated spaces collapse cleanly. */
	make_header(hdr, "  ZELDA  2  ", "FLBE");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "Zelda2.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_2.SAV");

	/* Trailing digit in title stays visually separated from slot digit. */
	make_header(hdr, "SUPER MARIO4", "AX4E");
	cgba_save_state_path(path, hdr, sizeof(hdr), "Mario4.gba", 0u);
	check_path_eq(path, "\\\\fls0\\SAVES\\SUPER_MARIO4_0.SVS");

	/* Punctuation: apostrophe dropped, separators become single '_', '-' kept. */
	make_header(hdr, "YOSHI'S ISL!", "A3AE");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "Yoshi.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\YOSHIS_ISL.SAV");

	make_header(hdr, "DR. MARIO:DX", "ADME");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "DrMario.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\DR_MARIO_DX.SAV");

	make_header(hdr, "MARIO-LUIGI ", "A88E");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "MarioLuigi.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\MARIO-LUIGI.SAV");
}

static void test_fallbacks_and_legacy_migration(void)
{
	uint8_t hdr[0xC0];
	cgba_nor_rom_list roms;
	uint16_t path[CGBA_SAVE_PATH_MAX];

	/* Single Zelda ROM: legacy \\fls0\THELEG.* migrates cleanly. */
	make_header(hdr, "ZELDA MINISH", "BZME");
	memset(&roms, 0, sizeof(roms));
	add_rom(&roms, "The Legend of Zelda - The Minish Cap (USA).gba");
	add_rom(&roms, "Metroid Fusion.gba");
	CHECK(cgba_save_legacy_backup_path(path, hdr, sizeof(hdr),
		roms.entries[0].label, &roms) == 1,
		"single Zelda backup legacy fallback should succeed");
	check_path_eq(path, "\\\\fls0\\THELEG.SAV");
	CHECK(cgba_save_legacy_state_path(path, hdr, sizeof(hdr),
		roms.entries[0].label, &roms, 2u) == 1,
		"single Zelda savestate legacy fallback should succeed");
	check_path_eq(path, "\\\\fls0\\THELEG2.SVS");

	/* Empty title with valid game code falls back to game code in SAVES\. */
	make_header(hdr, "            ", "Home");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "MyHomebrew.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\HOME.SAV");
	cgba_save_state_path(path, hdr, sizeof(hdr), "MyHomebrew.gba", 1u);
	check_path_eq(path, "\\\\fls0\\SAVES\\HOME_1.SVS");

	/* Empty title AND invalid/placeholder game code fall back to ROM stem. */
	make_header(hdr, "", "0000");
	memset(&roms, 0, sizeof(roms));
	add_rom(&roms, "MyHomebrew.gba");
	cgba_save_backup_path(path, hdr, sizeof(hdr), "MyHomebrew.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\MYHOME.SAV");
	cgba_save_state_path(path, hdr, sizeof(hdr), "MyHomebrew.gba", 5u);
	check_path_eq(path, "\\\\fls0\\SAVES\\MYHOME_5.SVS");
	/* Legacy root file \\fls0\MYHOME.SAV still migrates to SAVES\MYHOME.SAV. */
	CHECK(cgba_save_legacy_backup_path(path, hdr, sizeof(hdr),
		"MyHomebrew.gba", &roms) == 1,
		"single homebrew root save should still migrate");
	check_path_eq(path, "\\\\fls0\\MYHOME.SAV");

	/* Truncated / NULL header with non-ASCII ROM name uses safe defaults. */
	make_header(hdr, "ZELDA MINISH", "BZME");
	cgba_save_backup_path(path, hdr, 0xABu, "MyHomebrew.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\MYHOME.SAV");
	cgba_save_state_path(path, NULL, 0u, "???.gba", 3u);
	check_path_eq(path, "\\\\fls0\\SAVES\\CGBA_3.SVS");
	cgba_save_backup_path(path, NULL, 0u, "???.gba");
	check_path_eq(path, "\\\\fls0\\SAVES\\GAME.SAV");
}

static void test_non_ascii_roms_with_titles(void)
{
	uint8_t minish_jp[0xC0], alttp_jp[0xC0];
	cgba_nor_rom_list roms;
	uint16_t path[CGBA_SAVE_PATH_MAX];

	make_header(minish_jp, "ZELDA MINISH", "BZMJ");
	make_header(alttp_jp, "ZELDA ALTTP ", "AZLJ");

	/* label_from_path turns non-ASCII chars into '?', so both ROMs share
	 * the fallback legacy stem ("GAME" / "CGBA"). */
	memset(&roms, 0, sizeof(roms));
	add_rom(&roms, "???.gba");
	add_rom(&roms, "???????.gba");

	cgba_save_backup_path(path, minish_jp, sizeof(minish_jp),
		roms.entries[0].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_MINISH.SAV");
	cgba_save_state_path(path, minish_jp, sizeof(minish_jp),
		roms.entries[0].label, 0u);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_MINISH_0.SVS");

	cgba_save_backup_path(path, alttp_jp, sizeof(alttp_jp),
		roms.entries[1].label);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_ALTTP.SAV");
	cgba_save_state_path(path, alttp_jp, sizeof(alttp_jp),
		roms.entries[1].label, 0u);
	check_path_eq(path, "\\\\fls0\\SAVES\\ZELDA_ALTTP_0.SVS");

	CHECK(cgba_save_legacy_backup_path(path, minish_jp, sizeof(minish_jp),
		roms.entries[0].label, &roms) == 0,
		"multiple non-ASCII ROMs must not cross-load legacy GAME.SAV");
	CHECK(cgba_save_legacy_state_path(path, alttp_jp, sizeof(alttp_jp),
		roms.entries[1].label, &roms, 0u) == 0,
		"multiple non-ASCII ROMs must not cross-load legacy CGBA0.SVS");
}

int main(void)
{
	test_zelda_series_separation();
	test_title_sanitization();
	test_fallbacks_and_legacy_migration();
	test_non_ascii_roms_with_titles();

	if (failures) {
		printf("%d save path test failure(s)\n", failures);
		return 1;
	}
	puts("save path naming and legacy fallback passed");
	return 0;
}
