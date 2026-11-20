/* Host test for the fx-CG50 / fx-CG100 interface (ports/fxcg100/cg_ui.c).
 *
 * Compiles the interface with stub gint headers (tests/stubs_cgui), a fake
 * keypad and clock, and a simulated panel that receives what
 * fxcg100_lcd_update() and fxcg100_lcd_update_rows() send. It drives the
 * pages with scripted key presses and checks
 *   - what each key does (exit confirmation, slots, settings, key swaps),
 *   - that after every step the panel shows exactly what is in VRAM, so a
 *     partial push never leaves a changed row behind,
 *   - that moving the selection sends only bands, not the whole screen,
 *   - that no text ink lands under the highlight bar of another row.
 *
 *   fxcg_ui_test [outdir]
 *
 * With an output directory, the screens of the design draft are written as
 * PPMs of the 384x216 the panel shows. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/fxcg100_platform.h"

#ifdef CGBA_FXCG50
#include <gint/keycodes.h>
#define K_A KEY_MENU           /* default A */
#define K_B KEY_EXIT           /* default B */
#define K_FREE KEY_SIN         /* not bound by default */
#define K_FREE2 KEY_COS
#define MODEL "cg50"
#else
#define K_A 48                 /* OK */
#define K_B 37                 /* CAT */
#define K_FREE 65              /* SIN */
#define K_FREE2 55             /* COS */
#define MODEL "cg100"
#endif

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

/* ---- the fake platform ----------------------------------------------------- */

static uint16_t vram[396 * 224], panel[396 * 224];
uint16_t *gint_vram = vram;
static uint32_t now_ticks = 1000;
static uint32_t held_app;      /* FXCG100_APPKEY_* held */
static int held_phys;          /* physical key held, for the capture box */
static int full_pushes, band_pushes, config_saves;

uint32_t rtc_ticks(void)
{
	return now_ticks;
}

uint32_t fxcg100_poll_app_keys(void)
{
	return held_app;
}

void fxcg100_wait_keys_released(void) {}

int fxcg100_key_down(int key)
{
	return key && key == held_phys;
}

void fxcg100_lcd_update(void)
{
	memcpy(panel, vram, sizeof(panel));
	full_pushes++;
}

void fxcg100_lcd_update_rows(unsigned y, unsigned h)
{
	unsigned y1 = y + h;

	CHECK(y < 224 && h > 0, "row push %u+%u", y, h);
	if (y1 > 224)
		y1 = 224;
	y &= ~3u;
	y1 = (y1 + 3u) & ~3u;
	if (y1 > 224)
		y1 = 224;
	memcpy(&panel[y * 396], &vram[y * 396], (y1 - y) * 396 * sizeof(uint16_t));
	band_pushes++;
}

static const char *const games[] = {
	"Advance Wars.gba", "Castlevania Aria of Sorrow.gba",
	"Final Fantasy Tactics Advance.gba", "Fire Emblem.gba", "Golden Sun.gba",
	"Kirby Nightmare in Dream Land.gba", "Mario Kart Super Circuit.gba",
	"Metroid Fusion.gba", "Pokemon Emerald Version.gba", "Tetris Worlds.gba",
	"The Minish Cap.gba", "WarioWare Twisted.gba",
};
static const uint32_t game_last_played[12] = {
	0, 0, 0, 5, 0, 0, 0, 10, 20, 0, 0, 0,
};
static const uint32_t game_added_order[12] = {
	3, 1, 5, 2, 4, 8, 11, 6, 7, 9, 10, 12,
};
static uint32_t game_count = 12;
static uint32_t fake_savestate_mask;

uint32_t fxcg100_rom_source_count(void)
{
	return game_count;
}

const char *fxcg100_rom_source_label(uint32_t index)
{
	return index < 12 ? games[index] : "?";
}

uint32_t fxcg100_rom_source_last_played(uint32_t index)
{
	return index < 12 ? game_last_played[index] : 0;
}

uint32_t fxcg100_rom_source_added_order(uint32_t index)
{
	return index < 12 ? game_added_order[index] : 0;
}

int fxcg100_savestate_exists(uint32_t slot)
{
	return (fake_savestate_mask & (1u << (slot % 10u))) != 0;
}

int fxcg100_config_load(fxcg100_menu_state *state)
{
	(void)state;
	return 0;
}

int fxcg100_config_save(const fxcg100_menu_state *state)
{
	CHECK(fxcg100_input_maps_valid(state->keymap, state->hotkey_map),
		"saved an invalid key map");
	config_saves++;
	return 1;
}

/* ---- fake TXT filesystem for cg_ui TXT reader tests ------------------------ */

static const char fake_txt_doc[] =
	"POKEMON EMERALD WALKTHROUGH\n"
	"===========================\n"
	"\n"
	"1. Littleroot Town\n"
	"Set the clock in your bedroom, then visit Prof. Birch.\n"
	"Pick Treecko, Torchic, or Mudkip from the bag.\n"
	"\n"
	"2. Route 101 & Oldale Town\n"
	"Heal at the Pokemon Center and head north to Route 103.\n"
	"+---------------+---------------+\n"
	"| North: Rt 103 | West: Petalbg |\n"
	"+---------------+---------------+\n"
	"3. Petalburg City\n"
	"Talk to Norman in the Gym and help Wally catch Ralts.\n"
	"4. Petalburg Woods\n"
	"Defeat Team Aqua Grunt to save the Devon researcher.\n"
	"5. Rustboro City\n"
	"Challenge Roxanne for the Stone Badge.\n"
	"6. Dewford Town\n"
	"Deliver the letter to Steven in Granite Cave.\n";

static uint32_t fake_txt_gen = 1;
static uint8_t fake_txt_meta[4096];
static unsigned fake_txt_meta_size;

uint32_t fxcg100_storage_mutation_generation(void) { return fake_txt_gen; }

int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{
	(void)path;
	if (fake_txt_meta_size != size)
		return 0;
	memcpy(dst, fake_txt_meta, size);
	return 1;
}

int fxcg100_storage_write_blob(const uint16_t *path, const void *src,
	unsigned size)
{
	(void)path;
	if (size > sizeof(fake_txt_meta))
		return 0;
	memcpy(fake_txt_meta, src, size);
	fake_txt_meta_size = size;
	fake_txt_gen++;
	return 1;
}

static void fc_to_ascii_ui(const uint16_t *src, char *dst, size_t max)
{
	size_t i = 0;
	while (src && src[i] && i + 1 < max) {
		dst[i] = (char)(src[i] & 0x7fu);
		i++;
	}
	dst[i] = 0;
}

static void ascii_to_fc_ui(const char *src, uint16_t *dst, size_t max)
{
	size_t i = 0;
	while (src && src[i] && i + 1 < max) {
		dst[i] = (uint16_t)(unsigned char)src[i];
		i++;
	}
	dst[i] = 0;
}

int fxcg100_fs_list_dir(const uint16_t *pattern, fxcg100_fs_dir_cb cb,
	void *ctx)
{
	char pat[160];
	uint16_t name[64];

	fc_to_ascii_ui(pattern, pat, sizeof(pat));
	if (strcmp(pat, "\\\\fls0\\*") == 0) {
		ascii_to_fc_ui("GUIDES", name, 64);
		cb(name, 1, 0, ctx);
		ascii_to_fc_ui("README.TXT", name, 64);
		cb(name, 0, (uint32_t)strlen(fake_txt_doc), ctx);
		return 1;
	}
	if (strcmp(pat, "\\\\fls0\\GUIDES\\*") == 0) {
		ascii_to_fc_ui("EMERALD.TXT", name, 64);
		cb(name, 0, (uint32_t)strlen(fake_txt_doc), ctx);
		return 1;
	}
	return 1;
}

int fxcg100_fs_open_map(const uint16_t *path, uint32_t *out_size,
	const uint8_t **nor_blocks, unsigned max_blocks,
	unsigned *out_mapped_blocks, uint8_t *tail_buf,
	uint32_t *out_tail_block_idx, uint32_t *out_tail_len)
{
	char p[160];
	uint32_t sz = (uint32_t)strlen(fake_txt_doc);

	(void)nor_blocks;
	(void)max_blocks;
	fc_to_ascii_ui(path, p, sizeof(p));
	if (strcmp(p, "\\\\fls0\\README.TXT") != 0 &&
			strcmp(p, "\\\\fls0\\GUIDES\\EMERALD.TXT") != 0)
		return 0;
	if (out_size)
		*out_size = sz;
	if (out_mapped_blocks)
		*out_mapped_blocks = 0;
	if (tail_buf && out_tail_block_idx && out_tail_len) {
		memcpy(tail_buf, fake_txt_doc, sz);
		*out_tail_block_idx = 0;
		*out_tail_len = sz;
	}
	return 1;
}

int fxcg100_fs_read_range(const uint16_t *path, uint32_t offset,
	void *dst, uint32_t len)
{
	char p[160];
	uint32_t sz = (uint32_t)strlen(fake_txt_doc);

	fc_to_ascii_ui(path, p, sizeof(p));
	if ((strcmp(p, "\\\\fls0\\README.TXT") != 0 &&
			strcmp(p, "\\\\fls0\\GUIDES\\EMERALD.TXT") != 0) ||
			offset >= sz)
		return 0;
	if (len > sz - offset)
		len = sz - offset;
	memcpy(dst, fake_txt_doc + offset, len);
	return (int)len;
}

#include "ports/fxcg100/fxcg100_keys.c"
#include "ports/fxcg100/txt_reader.c"
#include "ports/fxcg100/cg_ui.c"

/* ---- driving it ------------------------------------------------------------ */

static fxcg100_menu_state st;
static const char *outdir;

static fxcg100_menu_result step(void)
{
	fxcg100_menu_result r = cg_ui_step(&ui_main);

	CHECK(!memcmp(panel, vram, sizeof(panel)),
		"the panel does not show what is in VRAM (page %d)", ui_main.page);
	return r;
}

/* Press and release app keys, a tick apart. Returns the first result. */
static fxcg100_menu_result press(uint32_t keys)
{
	fxcg100_menu_result r;

	held_app = keys;
	now_ticks++;
	r = step();
	held_app = 0;
	now_ticks++;
	if (r == FXCG100_MENU_CONTINUE)
		r = step();
	return r;
}

static void begin(int in_game)
{
	cg_ui_begin(&ui_main, &st, in_game, NULL);
	CHECK(step() == FXCG100_MENU_CONTINUE, "first step acted");
}

static void snap(const char *name)
{
	char path[512];
	FILE *f;
	int x, y;

	if (!outdir)
		return;
	snprintf(path, sizeof(path), "%s/%s-%s.ppm", outdir, MODEL, name);
	f = fopen(path, "wb");
	if (!f) {
		printf("cannot write %s\n", path);
		return;
	}
	fprintf(f, "P6\n384 216\n255\n");
	for (y = 0; y < 216; y++)
		for (x = 0; x < 384; x++) {
			uint16_t p = panel[(y + 4) * 396 + x + 6];
			unsigned char rgb[3] = {
				(unsigned char)(((p >> 11) & 31) * 255 / 31),
				(unsigned char)(((p >> 5) & 63) * 255 / 63),
				(unsigned char)((p & 31) * 255 / 31) };

			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

/* Every pixel of the rows y0..y1-1 (layout coordinates) equal to `c`. */
static int band_is(int y0, int y1, uint16_t c)
{
	int x, y;

	for (y = y0; y < y1; y++)
		for (x = 0; x < 396; x++)
			if (panel[(y + 4) * 396 + x] != c)
				return 0;
	return 1;
}

/* ---- the checks ------------------------------------------------------------ */

static void test_library_start(void)
{
	int full;

	fxcg100_menu_init(&st);
	st.rom_source = 1;
	begin(0);
	CHECK(ui_main.page == PAGE_LIBRARY, "starts in the library");
	CHECK(ui_main.game_sel == 1, "starts on rom_source");
	snap("library-start");

	full = full_pushes;
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	CHECK(ui_main.game_sel == 3, "down moves: %u", (unsigned)ui_main.game_sel);
	CHECK(full_pushes == full, "moving the selection sent the whole screen");
	CHECK(band_pushes > 0, "no bands sent");

	/* Back does nothing before a game runs; MENU / HOME asks first. */
	CHECK(press(FXCG100_APPKEY_BACK) == FXCG100_MENU_CONTINUE, "back acted");
	press(FXCG100_APPKEY_HOME);
	CHECK(ui_main.dialog == DIALOG_EXIT, "home did not ask");
	snap("exit-start");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_CONTINUE,
		"Cancel is not the default");
	CHECK(ui_main.dialog == DIALOG_NONE, "cancel left the box up");
	press(FXCG100_APPKEY_HOME);
	CHECK(press(FXCG100_APPKEY_HOME) == FXCG100_MENU_QUIT,
		"home twice does not exit");

	/* Pages, and picking a game. */
	begin(0);
	press(FXCG100_APPKEY_RIGHT);
	CHECK(ui_main.game_sel == 9, "right pages by eight: %u",
		(unsigned)ui_main.game_sel);
	press(FXCG100_APPKEY_RIGHT);
	CHECK(ui_main.game_sel == 11, "paging stops at the end");
	press(FXCG100_APPKEY_LEFT);
	press(FXCG100_APPKEY_LEFT);
	CHECK(ui_main.game_sel == 0, "left pages back: %u",
		(unsigned)ui_main.game_sel);
	press(FXCG100_APPKEY_UP);
	CHECK(ui_main.game_sel == 11, "up at the top goes round to the end");
	press(FXCG100_APPKEY_DOWN);
	CHECK(ui_main.game_sel == 0, "down at the end goes round to the top");
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_LOAD_GAME &&
		st.rom_source == 1, "picking a game");

	/* SORT (CATALOG on CG100, SHIFT on CG50) cycles sort mode. */
	fxcg100_menu_init(&st);
	begin(0);
	press(FXCG100_APPKEY_SORT);
	CHECK(st.game_sort == FXCG100_GAME_SORT_LAST_PLAYED &&
		ui_main.game_sel == 0 && ui_main.settings_dirty,
		"SORT switches to Last Played");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_LOAD_GAME &&
		st.rom_source == 8, "Last Played top game: %u",
		(unsigned)st.rom_source);
	begin(0);
	press(FXCG100_APPKEY_SORT);
	CHECK(st.game_sort == FXCG100_GAME_SORT_NEWLY_ADDED &&
		ui_main.game_sel == 0, "SORT switches to Newly Added");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_LOAD_GAME &&
		st.rom_source == 11, "Newly Added top game: %u",
		(unsigned)st.rom_source);
	begin(0);
	press(FXCG100_APPKEY_SORT);
	CHECK(st.game_sort == FXCG100_GAME_SORT_NAME, "SORT wraps back to Name");

	game_count = 0;
	begin(0);
	snap("library-empty");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_CONTINUE, "empty library");
	game_count = 12;
}

static void test_library_in_game(void)
{
	fxcg100_menu_init(&st);
	st.rom_source = 7;
	begin(1);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_GAMES, "pause rows");
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.page == PAGE_LIBRARY && ui_main.game_sel == 7,
		"Games opens on the game being played");
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	snap("library-playing");
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_SWITCH, "another game asks first");
	press(FXCG100_APPKEY_DOWN);
	snap("switch");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_LOAD_GAME &&
		st.rom_source == 4, "switching game: %u", (unsigned)st.rom_source);

	st.rom_source = 7;
	begin(1);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_EXE);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_RETURN,
		"the game being played resumes");
	begin(1);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_EXE);
	press(FXCG100_APPKEY_BACK);
	CHECK(ui_main.page == PAGE_PAUSE, "back from the library");
}

static void test_pause(void)
{
	fxcg100_menu_init(&st);
	st.rom_source = 7;
	st.savestate_slot = 3;
	begin(1);
	CHECK(ui_main.page == PAGE_PAUSE && selected_row(&ui_main)->id == ID_RESUME,
		"pause opens on Resume");
	snap("pause");
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_RETURN, "Resume");
	begin(1);
	CHECK(press(FXCG100_APPKEY_BACK) == FXCG100_MENU_RETURN, "back resumes");
	begin(1);
	CHECK(press(FXCG100_APPKEY_ON) == FXCG100_MENU_RETURN, "AC/ON resumes");

	fake_savestate_mask = 0;
	begin(1);
	press(FXCG100_APPKEY_DIGIT(5));
	CHECK(st.savestate_slot == 5, "digit picks the slot");
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_LEFT);
	press(FXCG100_APPKEY_LEFT);
	CHECK(st.savestate_slot == 3, "left lowers the slot");
	/* Empty slot saves immediately without an overwrite prompt. */
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_SAVE_STATE &&
		ui_main.dialog == DIALOG_NONE, "empty slot saves without asking");
	fake_savestate_mask |= (1u << 3);

	/* main() saves and comes back with the result in the footer. */
	cg_ui_show_progress("Saving state to slot 3...");
	CHECK(!memcmp(panel, vram, sizeof(panel)), "progress not shown");
	cg_ui_set_result("State saved to slot 3", 0);
	begin(1);
	CHECK(selected_row(&ui_main)->id == ID_SAVE, "came back on Save state");
	snap("pause-saved");
	now_ticks += 2 * 128;
	step();
	CHECK(!ui_main.message[0], "the message stayed past two seconds");

	/* Occupied slot asks before overwriting; an empty slot (0) does not. */
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_CONTINUE &&
		ui_main.dialog == DIALOG_SAVE, "Save state asks when slot exists");
	press(FXCG100_APPKEY_EXE);    /* Cancel is selected by default */
	CHECK(ui_main.dialog == DIALOG_NONE, "Save state Cancel");
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_SAVE, "Save state asks again");
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_SAVE_STATE, "Save state");
	begin(1);
	press(FXCG100_APPKEY_DIGIT(0));
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_SAVE_STATE &&
		ui_main.dialog == DIALOG_NONE, "empty slot 0 saves without asking");
	fake_savestate_mask = 0;

	begin(1);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_LOAD_STATE, "Load state");

	begin(1);
	press(FXCG100_APPKEY_UP);       /* goes round to the bottom */
	CHECK(selected_row(&ui_main)->id == ID_EXIT, "up at the top did not wrap");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_RESUME, "down at the end did not wrap");
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_RESTART, "Restart asks");
	CHECK(press(FXCG100_APPKEY_HOME) == FXCG100_MENU_CONTINUE,
		"home confirmed a restart");
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_RESET, "restart");

	begin(1);
	press(FXCG100_APPKEY_HOME);
	snap("exit");
	press(FXCG100_APPKEY_DOWN);
	CHECK(press(FXCG100_APPKEY_EXE) == FXCG100_MENU_QUIT, "exit from the box");
}

static void test_settings(void)
{
	int saves, i;

	fxcg100_menu_init(&st);
	st.rom_source = 7;
	begin(1);
	press(FXCG100_APPKEY_SETTINGS);
	CHECK(ui_main.page == PAGE_SETTINGS, "the settings key");
	CHECK(selected_row(&ui_main)->id == ID_SIZE, "settings open on the top");
	press(FXCG100_APPKEY_RIGHT);
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.screen_scale == 2, "picture size");
	snap("settings");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_FILTER, "filter row");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_VSYNC, "vsync row: %d",
		selected_row(&ui_main)->id);
	CHECK(st.lcd_sync == 0, "vsync default off");
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.lcd_sync == 1, "vsync right toggles on");
	press(FXCG100_APPKEY_EXE);
	CHECK(st.lcd_sync == 0, "vsync EXE toggles back off");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_REFRESH, "refresh row: %d",
		selected_row(&ui_main)->id);
	CHECK(st.lcd_refresh == 0, "refresh default auto");
	press(FXCG100_APPKEY_RIGHT);
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.lcd_refresh == 2, "refresh cycles to 59.7 Hz");
	press(FXCG100_APPKEY_LEFT);
	press(FXCG100_APPKEY_LEFT);
	CHECK(st.lcd_refresh == 0, "refresh back to auto");
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.theme == 1, "colour");
	press(FXCG100_APPKEY_LEFT);
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_DARK_MODE, "dark mode row: %d",
		selected_row(&ui_main)->id);
	CHECK(st.dark_mode == 1, "dark mode default");
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.dark_mode == 0 && ui_main.t.surface == cp_themes_light[0].surface,
		"dark mode off");
	snap("settings-light");
	press(FXCG100_APPKEY_EXE);
	CHECK(st.dark_mode == 1 && ui_main.t.surface == cp_themes[0].surface,
		"EXE toggles dark mode back on");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_FRAMESKIP, "rows: %d",
		selected_row(&ui_main)->id);
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_SKIP_N, "manual shows its rows");
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.frameskip_value == 2, "frames to skip");
	snap("settings-down");
	for (i = 0; i < 12; i++)
		press(FXCG100_APPKEY_RIGHT);
	CHECK(st.frameskip_value == 9, "frames to skip stops at 9");
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_LEFT);
	press(FXCG100_APPKEY_LEFT);
	CHECK(st.frameskip_type == 2, "frame skipping off");
	CHECK(row_index(&ui_main, ID_SKIP_N) < 0, "manual rows stayed");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_LIMIT, "next row after Off");
	press(FXCG100_APPKEY_EXE);
	CHECK(st.frame_limit == 0, "EXE toggles");

	/* Held down: one move, then repeats after 400 ms every 80 ms. */
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	CHECK(selected_row(&ui_main)->id == ID_SIZE, "back at the top");
	held_app = FXCG100_APPKEY_DOWN;
	step();
	CHECK(selected_row(&ui_main)->id == ID_FILTER, "first move");
	now_ticks += 50;
	step();
	CHECK(selected_row(&ui_main)->id == ID_FILTER, "repeated too soon");
	now_ticks += 1;
	step();
	CHECK(selected_row(&ui_main)->id == ID_VSYNC, "no repeat after 400 ms");
	now_ticks += 10;
	step();
	CHECK(selected_row(&ui_main)->id == ID_REFRESH, "no repeat after 80 ms");
	/* Held to the end, it stops there rather than going round. */
	for (i = 0; i < 20; i++) {
		now_ticks += 10;
		step();
	}
	CHECK(selected_row(&ui_main)->id == ID_KEYMAP, "held key went round: %d",
		selected_row(&ui_main)->id);
	held_app = 0;
	step();
	for (i = 0; i < 9; i++)
		press(FXCG100_APPKEY_UP);
	CHECK(selected_row(&ui_main)->id == ID_SIZE, "back up: %d",
		selected_row(&ui_main)->id);

	saves = config_saves;
	press(FXCG100_APPKEY_BACK);
	CHECK(config_saves == saves + 1, "leaving settings did not save");
	CHECK(ui_main.page == PAGE_PAUSE && !strcmp(ui_main.message,
		"Settings saved"), "back to the pause menu with a message");
	press(FXCG100_APPKEY_DOWN);
	CHECK(!ui_main.message[0] && selected_row(&ui_main)->id == ID_SAVE,
		"a key did not clear the message, or did not act");

	/* Original is not scaled: no filter row, VSync stays right after size. */
	begin(1);
	press(FXCG100_APPKEY_SETTINGS);
	CHECK(st.screen_scale == 2 && row_index(&ui_main, ID_FILTER) >= 0,
		"filter row while scaled");
	press(FXCG100_APPKEY_RIGHT);
	CHECK(st.screen_scale == 0 && row_index(&ui_main, ID_FILTER) < 0,
		"filter row shown for Original");
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_VSYNC, "row after size: %d",
		selected_row(&ui_main)->id);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_LEFT);
	CHECK(st.screen_scale == 2 && row_index(&ui_main, ID_FILTER) >= 0,
		"filter row back");
	press(FXCG100_APPKEY_BACK);

	/* From the library before a game, and back there. */
	begin(0);
	press(FXCG100_APPKEY_SETTINGS);
	press(FXCG100_APPKEY_BACK);
	CHECK(ui_main.page == PAGE_LIBRARY, "settings returns to the library");

	/* Leaving the menu saves too. */
	begin(1);
	press(FXCG100_APPKEY_SETTINGS);
	press(FXCG100_APPKEY_RIGHT);
	saves = config_saves;
	CHECK(press(FXCG100_APPKEY_ON) == FXCG100_MENU_RETURN, "AC/ON in settings");
	CHECK(config_saves == saves + 1, "closing the menu did not save");
}

static void open_keys(void)
{
	int i;

	begin(1);
	press(FXCG100_APPKEY_SETTINGS);
	for (i = 0; i < 20 && selected_row(&ui_main)->id != ID_KEYMAP; i++)
		press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_KEYMAP, "key mapping row");
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.page == PAGE_KEYS, "key mapping page");
}

static void capture(int key)
{
	held_phys = key;
	now_ticks++;
	step();
	held_phys = 0;
	now_ticks++;
	step();
}

static void test_keys(void)
{
	int i;

	fxcg100_menu_init(&st);
	st.rom_source = 7;
	open_keys();
	for (i = 0; i < 4; i++)
		press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_BIND0 + 4, "row A");
	snap("keys");
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_CAPTURE, "capture box");
	snap("capture");

	/* The key B uses: A and B swap. */
	capture(K_B);
	CHECK(ui_main.dialog == DIALOG_NONE, "capture box stayed");
	CHECK(st.keymap[FXCG100_GBA_KEY_A] == K_B &&
		st.keymap[FXCG100_GBA_KEY_B] == K_A, "A and B did not swap");
	snap("keys-swapped");

	/* AC/ON cancels. */
	press(FXCG100_APPKEY_EXE);
	capture(FXCG100_PHYSKEY_ON);
	CHECK(ui_main.dialog == DIALOG_NONE &&
		st.keymap[FXCG100_GBA_KEY_A] == K_B, "cancel changed the key");

	/* A shortcut with no key may not take a GBA button's key. */
	for (i = 0; i < 7; i++)
		press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_BIND0 + 11, "row Save state");
	press(FXCG100_APPKEY_EXE);
	capture(K_A);
	CHECK(st.hotkey_map[FXCG100_HOTKEY_SAVE_STATE] == 0 &&
		st.keymap[FXCG100_GBA_KEY_B] == K_A, "a GBA button lost its key");
	CHECK(ui_main.message_error, "no reason given");
	snap("keys-refused");

	/* A free key is fine, and DEL takes it away again. */
	press(FXCG100_APPKEY_EXE);
	capture(K_FREE);
	CHECK(st.hotkey_map[FXCG100_HOTKEY_SAVE_STATE] == K_FREE, "free key");
	press(FXCG100_APPKEY_DEL);
	CHECK(st.hotkey_map[FXCG100_HOTKEY_SAVE_STATE] == 0, "DEL");

	/* Two shortcuts swap, even with no key on one side. */
	press(FXCG100_APPKEY_EXE);
	capture(K_FREE);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_EXE);
	capture(K_FREE);
	CHECK(st.hotkey_map[FXCG100_HOTKEY_LOAD_STATE] == K_FREE &&
		st.hotkey_map[FXCG100_HOTKEY_SAVE_STATE] == 0, "shortcut move");
	press(FXCG100_APPKEY_EXE);
	capture(K_FREE2);

	/* DEL does nothing to a GBA button. */
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_UP);
	press(FXCG100_APPKEY_DEL);
	CHECK(st.keymap[FXCG100_GBA_KEY_SELECT] != 0, "DEL cleared a GBA button");
	CHECK(fxcg100_input_maps_valid(st.keymap, st.hotkey_map),
		"the key map went invalid");

	for (i = 0; i < 20 && selected_row(&ui_main)->id != ID_RESET_KEYS; i++)
		press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_RESET_KEYS, "reset row");
	press(FXCG100_APPKEY_EXE);
	CHECK(st.keymap[FXCG100_GBA_KEY_A] == K_A &&
		st.hotkey_map[FXCG100_HOTKEY_LOAD_STATE] == 0, "reset");
	snap("keys-bottom");
	press(FXCG100_APPKEY_BACK);
	CHECK(ui_main.page == PAGE_SETTINGS, "back to settings");
}

/* No text drawn in one row may reach into the next band: draw a row's
 * label and value over a band, then check the rows around it. */
static void test_baselines(void)
{
	cg_ui *ui = &ui_main;
	ui_row r;
	int y = HEAD + ROW, x, yy;

	fxcg100_menu_init(&st);
	begin(1);
	memset(&r, 0, sizeof(r));
	r.kind = RK_VALUE;
	r.label = "gjpqy Typography (gpSP)";
	snprintf(r.value, sizeof(r.value), "jpgy");
	cp_fill(&ui->s, -6, -4, 396, 224, 0x0000);
	draw_row(ui, y, &r, 0);
	section_row(ui, y + ROW, "Display gpqy");
	for (yy = -4; yy < 220; yy++)
		for (x = 0; x < 396; x++) {
			int in_row = yy >= y && yy < y + ROW + SEC;

			if (!in_row && vram[(yy + 4) * 396 + x])
				CHECK(0, "ink at row %d outside its band", yy);
		}
	cp_fill(&ui->s, -6, -4, 396, 224, 0x0000);
	ui->t = *cp_theme_get(0);
	hint_bar(ui, NULL, 0, NULL, 0);
	footer_message(ui, "gjpqy State saved to slot 3", NOTE_DONE);
	for (x = 0; x < 396; x++)
		CHECK(vram[(H - 1 + 4) * 396 + x] == ui->t.container,
			"footer text reaches the screen's last row");
	(void)band_is;
}

static void test_status(void)
{
	static uint16_t frame[240 * 160];
	int x, y;

	for (y = 0; y < 160; y++)
		for (x = 0; x < 240; x++)
			frame[y * 240 + x] = (uint16_t)(((x * 31 / 239) << 11) |
				((y * 63 / 159) << 5) | 16);
	fxcg100_menu_init(&st);
	st.screen_scale = 2;
	cg_ui_status(&st, "Saving state", "Slot 3", frame);
	CHECK(!memcmp(panel, vram, sizeof(panel)), "status not sent");
	snap("status-game");
	st.screen_scale = 0;
	cg_ui_status(&st, "loading ROM", "Metroid Fusion.gba", NULL);
	snap("status-plain");
	CHECK(band_is(-4, 20, cp_theme_get(0)->surface), "plain page");
	st.dark_mode = 0;
	cg_ui_status(&st, "loading ROM", "Metroid Fusion.gba", NULL);
	CHECK(band_is(-4, 20, cp_theme_get_mode(0, 0)->surface),
		"plain page light");
}

static void test_txt_reader(void)
{
	txt_reader *r;

	fake_txt_meta_size = 0;
	fxcg100_menu_init(&st);
	st.rom_source = 7;
	st.hotkey_map[FXCG100_HOTKEY_TXT_READER] = K_FREE;
	begin(1);

	/* Pause menu has 8 rows fitting exact VIEW_H with no scroll, TXT Reader is row 3. */
	CHECK(ui_main.list_h == VIEW_H, "pause fits exactly without scroll: %d",
		ui_main.list_h);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN);
	CHECK(selected_row(&ui_main)->id == ID_READER &&
		strcmp(selected_row(&ui_main)->label, "TXT Reader") == 0,
		"TXT Reader row in pause menu");

	/* Opening with no doc goes to file browser at root (\fls0\). */
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.page == PAGE_TXT_BROWSER, "opened TXT browser");
	r = txt_reader_global();
	CHECK(r->dir_depth == 0 && r->dir_count == 2, "scanned root dir");
	snap("txt-browser");

	/* Enter subfolder GUIDES/, verify .. row + EMERALD.TXT, then open it. */
	press(FXCG100_APPKEY_EXE);
	CHECK(r->dir_depth == 1 && r->dir_count == 1, "entered GUIDES/");
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.page == PAGE_TXT_READER && r->has_doc, "opened EMERALD.TXT");
	CHECK(r->show_hints == 1, "footer hints shown on entry");
	snap("txt-reader-hints");

	/* First scroll auto-hides the footer hints and expands body lines. */
	int lines_with_hints = r->layout_vis;
	press(FXCG100_APPKEY_DOWN);
	CHECK(r->show_hints == 0, "scroll auto-hid footer hints");
	CHECK(r->layout_vis > lines_with_hints, "body reclaimed footer band");
	snap("txt-reader-clean");

	/* Digit jump + DEL undo. */
	press(FXCG100_APPKEY_DIGIT(5));
	CHECK(txt_reader_percent(r) >= 40, "digit 5 jumped forward");
	CHECK(ui_main.message[0] != 0, "jump toast displayed");
	press(FXCG100_APPKEY_DEL);
	CHECK(r->top_offset < 60u, "DEL undid jump");

	/* EXE opens Reader Options (Jump, Bookmarks, Font, Wrap, Files). */
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_TXT_OPTIONS, "options dialog opened");
	snap("txt-options");
	press(FXCG100_APPKEY_DOWN); /* Bookmarks */
	press(FXCG100_APPKEY_EXE);
	CHECK(ui_main.dialog == DIALOG_TXT_BOOKMARKS, "bookmarks dialog opened");
	press(FXCG100_APPKEY_EXE); /* + Add bookmark here */
	CHECK(txt_reader_bookmark_count(r) == 1, "added dynamic bookmark");
	CHECK(ui_main.dialog == DIALOG_NONE, "closed dialog after add");

	/* Toggle Font & Wrap inside Options. */
	press(FXCG100_APPKEY_EXE);
	press(FXCG100_APPKEY_DOWN);
	press(FXCG100_APPKEY_DOWN); /* Font */
	press(FXCG100_APPKEY_RIGHT);
	CHECK(r->font_idx == TXT_FONT_8X14, "switched to Terminus 8x14");
	press(FXCG100_APPKEY_DOWN); /* Wrap */
	press(FXCG100_APPKEY_RIGHT);
	CHECK(r->wrap == 0, "switched to Wrap Off");
	press(FXCG100_APPKEY_BACK);
	CHECK(ui_main.dialog == DIALOG_NONE, "closed options");

	/* Back returns to pause menu and flushes metadata. */
	press(FXCG100_APPKEY_BACK);
	CHECK(ui_main.page == PAGE_PAUSE && fake_txt_meta_size == sizeof(txt_meta_blob),
		"returned to pause and flushed CGBATXT.DAT");

	/* Direct hotkey open jumps straight to reader; pressing hotkey again returns to game. */
	fxcg100_menu_open_reader();
	begin(1);
	CHECK(ui_main.page == PAGE_TXT_READER && ui_main.txt_direct == 1,
		"hotkey opened reader directly");
	CHECK(r->show_hints == 1, "re-entry restored footer hints");
	held_phys = K_FREE;
	now_ticks++;
	CHECK(step() == FXCG100_MENU_RETURN, "pressing hotkey again returns to game");
	held_phys = 0;
}

int main(int argc, char **argv)
{
	int t, dark;

	outdir = argc > 1 ? argv[1] : NULL;
	test_library_start();
	test_library_in_game();
	test_pause();
	test_settings();
	test_keys();
	test_baselines();
	test_status();
	test_txt_reader();

	/* Every colour scheme draws every page, in dark and light mode. */
	for (dark = 1; dark >= 0; dark--) {
		for (t = 0; t < CP_THEME_COUNT; t++) {
			fxcg100_menu_init(&st);
			st.theme = (uint32_t)t;
			st.dark_mode = (uint32_t)dark;
			st.rom_source = 7;
			begin(1);
			press(FXCG100_APPKEY_SETTINGS);
			press(FXCG100_APPKEY_HOME);
			press(FXCG100_APPKEY_BACK);
		}
	}

	CHECK(hint_overflows == 0, "%d footers did not fit", hint_overflows);
	printf("fxcg_ui (%s): %s (%d full pushes, %d band pushes)\n", MODEL,
		failures ? "FAILED" : "ok", full_pushes, band_pushes);
	return failures ? 1 : 0;
}
