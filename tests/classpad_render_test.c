/* Host end-to-end test for the ClassPad II presenter.
 *
 * Compiles gint-gpsp/src/classpad_platform.c against stub gint headers and a
 * simulated R61523 (window + sequential pixel writes), then checks what ends
 * up on the panel: picture placement and orientation for every layout and
 * rotation, exact scaler taps, touch-key hit testing, pressed highlights, the
 * landscape cursor-key remap and the rotated menu canvas.
 *
 *   classpad_render_test [outdir]
 *
 * With an output directory, each layout is also written as a PPM in the
 * orientation the player sees, for eyeballing the layouts. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/fxcg100_platform.h"
#include "ports/fxcg100/classpad_layout.h"
#include "ports/fxcg100/classpad_scale.h"

#include <gint/display.h>
#include <gint/drivers/touch.h>
#include <gint/keycodes.h>

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

/* ---- simulated panel ------------------------------------------------------ */

static uint16_t panel[CP_PANEL_H][CP_PANEL_W];
static int win_c0, win_c1, win_r0, win_r1, win_c, win_r;
static long win_left = 0;
static int win_log_active;
static int win_log_r0[128], win_log_r1[128], win_log_n;

void cp_host_lcd_begin(int c0, int c1, int r0, int r1)
{
	CHECK(win_left == 0, "window reopened with %ld pixels unwritten", win_left);
	CHECK(c0 >= 0 && c1 < CP_PANEL_W && c0 <= c1 &&
		r0 >= 0 && r1 < CP_PANEL_H && r0 <= r1,
		"window out of panel: c %d..%d r %d..%d", c0, c1, r0, r1);
	if (win_log_active && win_log_n < 128) {
		win_log_r0[win_log_n] = r0;
		win_log_r1[win_log_n] = r1;
		win_log_n++;
	}
	win_c0 = c0; win_c1 = c1; win_r0 = r0; win_r1 = r1;
	win_c = c0; win_r = r0;
	win_left = (long)(c1 - c0 + 1) * (r1 - r0 + 1);
}

void cp_host_lcd_write(uint16_t pixel)
{
	CHECK(win_left > 0, "pixel written past the end of the window");
	if (win_left <= 0)
		return;
	panel[win_r][win_c] = pixel;
	win_left--;
	if (++win_c > win_c1) {
		win_c = win_c0;
		win_r++;
	}
}

/* ---- stub gint ------------------------------------------------------------ */

static uint16_t vram[DWIDTH * DHEIGHT];
uint16_t *gint_vram = vram;

void dclear(int color)
{
	for (int i = 0; i < DWIDTH * DHEIGHT; i++)
		vram[i] = (uint16_t)color;
}

void drect(int x1, int y1, int x2, int y2, int color)
{
	for (int y = y1; y <= y2; y++)
		for (int x = x1; x <= x2; x++)
			if (x >= 0 && x < DWIDTH && y >= 0 && y < DHEIGHT)
				vram[y * DWIDTH + x] = (uint16_t)color;
}

/* Text is drawn as one 5x7 block per character: enough to see where it is. */
void dtext_opt(int x, int y, int fg, int bg, int halign, int valign,
	const char *text)
{
	(void)bg; (void)halign; (void)valign;
	for (; *text; text++, x += 7)
		if (*text != ' ')
			drect(x, y, x + 4, y + 6, fg);
}

void dupdate(void)
{
	cp_host_lcd_begin(0, DWIDTH - 1, 0, DHEIGHT - 1);
	for (int i = 0; i < DWIDTH * DHEIGHT; i++)
		cp_host_lcd_write(vram[i]);
}

static int key_held[256];
int keydown(int key) { return key_held[key & 255]; }
void clearevents(void) {}

static struct _touch_addots touch_state;
void touch_get_last_scan(struct _touch_adraw *raw,
	struct _touch_adconv *conv, struct _touch_addots *dots)
{
	(void)raw; (void)conv;
	*dots = touch_state;
}

/* DMA to the LCD port: checked for the hardware's constraints, then played
 * straight into the simulated panel. */
#include <gint/dma.h>
static long dma_transfers, dma_pixels;

bool dma_transfer_async(int channel, dma_size_t size, unsigned blocks,
	void const *src, dma_address_t src_mode, void *dst,
	dma_address_t dst_mode, gint_call_t callback)
{
	const uint16_t *pixels = src;

	(void)dst; (void)callback;
	CHECK(channel == 0 && size == DMA_32B && src_mode == DMA_INC &&
		dst_mode == DMA_FIXED, "unexpected DMA configuration");
	CHECK(((uintptr_t)src & 31) == 0, "DMA source not 32-byte aligned");
	CHECK(blocks > 0 && blocks * 16u <= 4096u, "DMA strip of %u blocks",
		blocks);
	CHECK((long)blocks * 16 == win_left,
		"DMA of %u pixels into a window with %ld left", blocks * 16u,
		win_left);
	for (unsigned i = 0; i < blocks * 16u; i++)
		cp_host_lcd_write(pixels[i]);
	dma_transfers++;
	dma_pixels += blocks * 16;
	return true;
}

void dma_transfer_wait(int channel) { (void)channel; }

void r61523_win_set(int x1, int x2, int y1, int y2)
{
	(void)x1; (void)x2; (void)y1; (void)y2;
}

#include "ports/fxcg100/gint-gpsp/src/classpad_platform.c"
#include "ports/fxcg100/fxcg100_keys.c"

/* What the interface needs from the runner and the config file. */
static const char *const fake_games[] = {
	"Advance Wars.gba", "Golden Sun.gba", "Kirby Nightmare in Dream Land.GBA",
	"Mario Kart Super Circuit.gba", "Metroid Fusion.gba",
	"Pokemon Emerald Version With A Very Long File Name.gba",
	"Tetris Worlds.gba", "The Legend of Zelda The Minish Cap.gba",
	"Wario Land 4.gba", "WarioWare Inc.gba", "Yoshi's Island.gba",
	"Zelda A Link to the Past.gba",
};
static const uint32_t fake_last_played[12] = {
	0, 5, 0, 0, 10, 0, 0, 0, 20, 0, 0, 0,
};
static const uint32_t fake_added_order[12] = {
	3, 1, 5, 2, 4, 8, 11, 6, 7, 9, 10, 12,
};
static uint32_t fake_game_count = 12;
static uint32_t fake_savestate_mask;
static int fake_external_bios;
static int config_saves;

uint32_t fxcg100_rom_source_count(void) { return fake_game_count; }
const char *fxcg100_rom_source_label(uint32_t i) { return fake_games[i % 12]; }
uint32_t fxcg100_rom_source_last_played(uint32_t i) { return fake_last_played[i % 12]; }
uint32_t fxcg100_rom_source_added_order(uint32_t i) { return fake_added_order[i % 12]; }
int fxcg100_savestate_exists(uint32_t slot)
{
	return (fake_savestate_mask & (1u << (slot % 10u))) != 0;
}
int fxcg100_external_bios_available(void)
{
	return fake_external_bios;
}
/* What a saved configuration would put in the view, if not negative. */
static int fake_saved_view = -1;
static uint32_t fake_saved_rotation = CP_ROTATION_KEYS_RIGHT;
int fxcg100_config_load(fxcg100_menu_state *state)
{
	if (fake_saved_view >= 0) {
		state->screen_scale = (uint32_t)fake_saved_view;
		state->rotation = fake_saved_rotation;
	}
	return fake_saved_view >= 0;
}
int fxcg100_config_save(const fxcg100_menu_state *state)
{
	(void)state;
	config_saves++;
	return 1;
}

/* A pretend clock generator, RTC and storage for the clock page. */
#include "ports/fxcg100/classpad_clock.h"
static cp_clock hw_clock;
static int hw_clock_writes;
static uint32_t fake_ticks;
static unsigned char fake_blob[256];
static unsigned fake_blob_size;
static unsigned char fake_txt_blob[4096];
static unsigned fake_txt_blob_size;
static uint32_t fake_txt_gen = 1;

static const char cp_fake_txt_doc[] =
	"CLASSPAD TXT READER WALKTHROUGH\n"
	"===============================\n"
	"Line 1: Welcome to Hoenn.\n"
	"Line 2: Check the clock in Littleroot.\n"
	"Line 3: Visit Prof. Birch on Route 101.\n"
	"Line 4: Heal in Oldale Town.\n"
	"Line 5: Head west to Petalburg City.\n"
	"Line 6: Help Wally catch Ralts.\n"
	"Line 7: Enter Petalburg Woods.\n"
	"Line 8: Reach Rustboro City Gym.\n"
	"Line 9: Defeat Roxanne for Stone Badge.\n"
	"Line 10: Sail to Dewford Town.\n"
	"Line 11: Explore Granite Cave.\n"
	"Line 12: Deliver letter to Steven.\n";

static int is_txt_meta_path(const uint16_t *path)
{
	char buf[64];
	size_t i = 0;

	while (path && path[i] && i + 1 < sizeof(buf)) {
		buf[i] = (char)(path[i] & 0x7fu);
		i++;
	}
	buf[i] = 0;
	return strstr(buf, "CGBATXT.DAT") != NULL;
}

uint32_t fxcg100_storage_mutation_generation(void) { return fake_txt_gen; }

void fxcg100_clock_read(struct cp_clock *clock) { *clock = hw_clock; }
void fxcg100_clock_write(const struct cp_clock *clock)
{
	hw_clock = *clock;
	hw_clock_writes++;
}
uint32_t fxcg100_ticks(void) { return fake_ticks; }
uint32_t fxcg100_touch_recoveries(void) { return 0; }
int fxcg100_touch_is_polled(void) { return 1; }
/* The pretend processor is exactly as fast as its clock setting says. */
uint32_t fxcg100_measure_speed(void)
{
	return cp_clock_hz(&hw_clock, CP_CLK_CPU) / 100000u;
}
int fxcg100_stress_ram(void) { return 0; }
uint32_t fxcg100_stress_signature(void) { return 0x5678u; }
uint32_t fxcg100_core_checksum(void) { return 0x1234u; }
int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size)
{
	if (is_txt_meta_path(path)) {
		if (size != fake_txt_blob_size)
			return 0;
		memcpy(dst, fake_txt_blob, size);
		return 1;
	}
	if (size != fake_blob_size)
		return 0;
	memcpy(dst, fake_blob, size);
	return 1;
}
int fxcg100_storage_write_blob(const uint16_t *path, const void *src,
	unsigned size)
{
	if (is_txt_meta_path(path)) {
		if (size > sizeof(fake_txt_blob))
			return 0;
		memcpy(fake_txt_blob, src, size);
		fake_txt_blob_size = size;
		fake_txt_gen++;
		return 1;
	}
	memcpy(fake_blob, src, size);
	fake_blob_size = size;
	fake_txt_gen++;
	return 1;
}

static void cp_fc_to_ascii(const uint16_t *src, char *dst, size_t max)
{
	size_t i = 0;
	while (src && src[i] && i + 1 < max) {
		dst[i] = (char)(src[i] & 0x7fu);
		i++;
	}
	dst[i] = 0;
}

static void cp_ascii_to_fc(const char *src, uint16_t *dst, size_t max)
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

	cp_fc_to_ascii(pattern, pat, sizeof(pat));
	if (strcmp(pat, "\\\\fls0\\*") == 0) {
		cp_ascii_to_fc("GUIDES", name, 64);
		cb(name, 1, 0, ctx);
		cp_ascii_to_fc("README.TXT", name, 64);
		cb(name, 0, (uint32_t)strlen(cp_fake_txt_doc), ctx);
		return 1;
	}
	if (strcmp(pat, "\\\\fls0\\GUIDES\\*") == 0) {
		cp_ascii_to_fc("HOENN.TXT", name, 64);
		cb(name, 0, (uint32_t)strlen(cp_fake_txt_doc), ctx);
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
	uint32_t sz = (uint32_t)strlen(cp_fake_txt_doc);

	(void)nor_blocks;
	(void)max_blocks;
	cp_fc_to_ascii(path, p, sizeof(p));
	if (strcmp(p, "\\\\fls0\\README.TXT") != 0 &&
			strcmp(p, "\\\\fls0\\GUIDES\\HOENN.TXT") != 0)
		return 0;
	if (out_size)
		*out_size = sz;
	if (out_mapped_blocks)
		*out_mapped_blocks = 0;
	if (tail_buf && out_tail_block_idx && out_tail_len) {
		memcpy(tail_buf, cp_fake_txt_doc, sz);
		*out_tail_block_idx = 0;
		*out_tail_len = sz;
	}
	return 1;
}

int fxcg100_fs_read_range(const uint16_t *path, uint32_t offset,
	void *dst, uint32_t len)
{
	char p[160];
	uint32_t sz = (uint32_t)strlen(cp_fake_txt_doc);

	cp_fc_to_ascii(path, p, sizeof(p));
	if ((strcmp(p, "\\\\fls0\\README.TXT") != 0 &&
			strcmp(p, "\\\\fls0\\GUIDES\\HOENN.TXT") != 0) ||
			offset >= sz)
		return 0;
	if (len > sz - offset)
		len = sz - offset;
	memcpy(dst, cp_fake_txt_doc + offset, len);
	return (int)len;
}

#include "ports/fxcg100/txt_reader.c"
#include "ports/fxcg100/classpad_ui.c"

/* ---- helpers -------------------------------------------------------------- */

static uint16_t gba[CP_GBA_W * CP_GBA_H] __attribute__((aligned(4)));

/* Every pixel distinct in a way that survives RGB565: x in the low 8 bits,
 * y in the high 8. */
static void fill_test_frame(void)
{
	for (int y = 0; y < CP_GBA_H; y++)
		for (int x = 0; x < CP_GBA_W; x++)
			gba[y * CP_GBA_W + x] = (uint16_t)((y << 8) | x);
}

/* A picture a person can orient: gradient, corner marks, an "up" arrow. */
static void fill_demo_frame(void)
{
	for (int y = 0; y < CP_GBA_H; y++)
		for (int x = 0; x < CP_GBA_W; x++) {
			unsigned r = (unsigned)x * 31 / (CP_GBA_W - 1);
			unsigned b = (unsigned)y * 31 / (CP_GBA_H - 1);
			unsigned g = ((x / 16 + y / 16) & 1) ? 20 : 34;
			uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);

			if (x < 24 && y < 24) c = 0xF800;                 /* TL red */
			if (x >= CP_GBA_W - 24 && y < 24) c = 0x07E0;     /* TR green */
			if (x < 24 && y >= CP_GBA_H - 24) c = 0x001F;     /* BL blue */
			if (x >= CP_GBA_W - 24 && y >= CP_GBA_H - 24) c = 0xFFFF;
			if (y >= 30 && y < 70 && abs(x - 120) <= (y - 30) / 2)
				c = 0xFFE0;                                   /* arrow head */
			if (y >= 70 && y < 120 && abs(x - 120) <= 6)
				c = 0xFFE0;                                   /* arrow stem */
			gba[y * CP_GBA_W + x] = c;
		}
}

static uint16_t user_pixel(unsigned view, unsigned rot, int ux, int uy)
{
	int px, py;

	cp_user_to_panel_point(view, rot, ux, uy, &px, &py);
	return panel[py][px];
}

static void set_mode(unsigned view, unsigned rot, unsigned filter)
{
	fxcg100_lcd_set_scale(view);
	fxcg100_lcd_set_rotation(rot);
	fxcg100_lcd_set_filter(filter);
	fxcg100_lcd_set_touch_keys(1);
}

static void write_ppm(const char *dir, const char *name, unsigned view,
	unsigned rot)
{
	cp_layout l = cp_layout_for(view, rot);
	char path[512];
	FILE *f;

	if (!dir || !name)
		return;
	snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
	f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", l.user_w, l.user_h);
	for (int y = 0; y < l.user_h; y++)
		for (int x = 0; x < l.user_w; x++) {
			uint16_t p = user_pixel(view, rot, x, y);
			unsigned char rgb[3] = {
				(unsigned char)(((p >> 11) & 31) * 255 / 31),
				(unsigned char)(((p >> 5) & 63) * 255 / 63),
				(unsigned char)((p & 31) * 255 / 31)
			};
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

/* ---- tests ---------------------------------------------------------------- */

/* Source pixel an exact (unblended) output tap must show, or -1 if the tap
 * is a blend for some filter. `o` is the output coordinate inside the
 * picture, `num`/`den` the scale. */
static int exact_source(int o, unsigned view)
{
	switch (view) {
	case CP_VIEW_PORTRAIT_1X:
	case CP_VIEW_LANDSCAPE_1X:
		return o;
	case CP_VIEW_LANDSCAPE_2X:
		return o / 2;
	case CP_VIEW_LANDSCAPE_15X:        /* {a, a|b, b} */
		return (o % 3 == 1) ? -1 : (o / 3) * 2 + (o % 3 == 2);
	default:                           /* 4:3 {a, ., ., c} */
		return (o % 4 == 0) ? (o / 4) * 3 :
			(o % 4 == 3) ? (o / 4) * 3 + 2 : -1;
	}
}

static void test_picture(unsigned view, unsigned rot, unsigned filter,
	unsigned cpu_transfer)
{
	cp_layout l = cp_layout_for(view, rot);
	long checked = 0;

	fill_test_frame();
	set_mode(view, rot, filter);
	fxcg100_lcd_set_transfer(cpu_transfer);
	fxcg100_lcd_update();              /* forces a fresh game screen */
	dma_pixels = dma_transfers = 0;
	fxcg100_lcd_blit_gba(gba);
	CHECK(win_left == 0, "view %u rot %u: frame left %ld pixels unwritten",
		view, rot, win_left);
	/* The whole picture, and nothing else, goes out by DMA - or none of it
	 * when CPU transfer is selected. */
	CHECK(dma_pixels == (cpu_transfer ? 0 : (long)l.image.w * l.image.h),
		"view %u: %ld pixels sent by DMA, picture has %d", view, dma_pixels,
		l.image.w * l.image.h);

	for (int y = 0; y < l.image.h; y++) {
		int sy = exact_source(y, view);

		if (sy < 0)
			continue;
		for (int x = 0; x < l.image.w; x++) {
			int sx = exact_source(x, view);
			uint16_t got;

			if (sx < 0)
				continue;
			got = user_pixel(view, rot, l.image.x + x, l.image.y + y);
			CHECK(got == gba[sy * CP_GBA_W + sx],
				"view %u rot %u filter %u: user (%d,%d) shows %04x, "
				"expected source (%d,%d) %04x", view, rot, filter, x, y,
				got, sx, sy, gba[sy * CP_GBA_W + sx]);
			checked++;
		}
	}
	CHECK(checked > 15000, "view %u: only %ld taps checked", view, checked);
}

/* Portrait held upside down (Swap in portrait): the whole panel - picture,
 * keys and captions - is the upright one turned half a turn. */
static void test_flipped(unsigned view)
{
	static uint16_t upright[CP_PANEL_H][CP_PANEL_W];
	long wrong = 0;
	int x, y;

	fill_test_frame();
	set_mode(view, CP_ROTATION_KEYS_RIGHT, 0);
	fxcg100_lcd_update();
	fxcg100_lcd_blit_gba(gba);
	memcpy(upright, panel, sizeof(upright));

	set_mode(view, CP_ROTATION_KEYS_LEFT, 0);
	fxcg100_lcd_update();
	fxcg100_lcd_blit_gba(gba);
	for (y = 0; y < CP_PANEL_H; y++)
		for (x = 0; x < CP_PANEL_W; x++)
			if (panel[y][x] != upright[CP_PANEL_H - 1 - y][CP_PANEL_W - 1 - x])
				wrong++;
	CHECK(wrong == 0, "view %u upside down: %ld pixels are not the upright "
		"panel turned half a turn", view, wrong);
	CHECK(cp_physical_direction(view, CP_ROTATION_KEYS_LEFT, 0) == 2,
		"view %u upside down: the keypad's directions must turn too", view);
}

static int rects_overlap(cp_rect a, cp_rect b)
{
	return !(a.x + a.w <= b.x || a.x >= b.x + b.w ||
		a.y + a.h <= b.y || a.y >= b.y + b.h);
}

static int rect_inside(cp_rect in, cp_rect out)
{
	return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w &&
		in.y + in.h <= out.y + out.h;
}

static void test_layout_geometry(unsigned view, unsigned rot)
{
	cp_layout l = cp_layout_for(view, rot);
	cp_rect screen = { 0, 0, (int16_t)l.user_w, (int16_t)l.user_h };

	CHECK(l.key_count <= CP_MAX_KEYS, "too many keys");
	CHECK(rect_inside(l.image, screen), "view %u picture off screen", view);

	for (unsigned i = 0; i < l.key_count; i++) {
		const cp_key *k = &l.keys[i];
		cp_rect p = cp_user_to_panel_rect(view, rot, k->frame);

		CHECK(rect_inside(k->hit, screen), "view %u key %u hit off screen",
			view, i);
		CHECK(rect_inside(k->frame, k->hit),
			"view %u key %u is drawn outside its touch range", view, i);
		CHECK(p.x >= 0 && p.y >= 0 && p.x + p.w <= CP_PANEL_W &&
			p.y + p.h <= CP_PANEL_H, "view %u key %u off panel", view, i);
		CHECK(k->frame.w * k->frame.h <= CP_MAX_KEY_PIXELS,
			"view %u key %u too large to draw", view, i);
		/* Pressability floor: nothing registers on less than 46 px (9 mm)
		 * in either direction - except the shoulder keys, which register
		 * exactly where they are drawn, 40 px high, so that they cannot be
		 * caught from the keys next to them. */
		if (k->mask == CP_GBA_L || k->mask == CP_GBA_R) {
			if (k->frame.w >= 76)     /* not the 2x strip's keys */
				CHECK(k->hit.x == k->frame.x && k->hit.y == k->frame.y &&
					k->hit.w == k->frame.w && k->hit.h == k->frame.h,
					"view %u key %u: a shoulder key's touch range is not "
					"what is drawn", view, i);
		} else {
			CHECK(k->hit.w >= 46 && k->hit.h >= 46,
				"view %u key %u touch range %dx%d is too small", view, i,
				k->hit.w, k->hit.h);
		}
		CHECK(!rects_overlap(k->hit, l.image),
			"view %u key %u touch range overlaps the picture", view, i);
		if (k->shape == CP_KEY_PILL) {
			cp_rect caption = { k->frame.x,
				(int16_t)(k->frame.y + k->frame.h + 2), k->frame.w,
				CP_KEY_CAPTION_H };

			CHECK(rect_inside(caption, k->hit),
				"view %u key %u caption outside its touch range", view, i);
		}
		for (unsigned j = i + 1; j < l.key_count; j++)
			CHECK(!rects_overlap(k->hit, l.keys[j].hit),
				"view %u keys %u and %u have overlapping touch ranges",
				view, i, j);
	}

	/* user <-> panel is a bijection on the whole screen. */
	for (int y = 0; y < l.user_h; y += 7)
		for (int x = 0; x < l.user_w; x += 5) {
			int px, py, bx, by;

			cp_user_to_panel_point(view, rot, x, y, &px, &py);
			CHECK(px >= 0 && px < CP_PANEL_W && py >= 0 && py < CP_PANEL_H,
				"view %u (%d,%d) maps off panel", view, x, y);
			cp_panel_to_user_point(view, rot, px, py, &bx, &by);
			CHECK(bx == x && by == y, "view %u point round trip", view);
		}
}

static void touch_user(unsigned view, unsigned rot, int n, int ux1, int uy1,
	int ux2, int uy2)
{
	memset(&touch_state, 0, sizeof(touch_state));
	touch_state.touches = n;
	if (n >= 1)
		cp_user_to_panel_point(view, rot, ux1, uy1,
			&touch_state.x1, &touch_state.y1);
	if (n >= 2)
		cp_user_to_panel_point(view, rot, ux2, uy2,
			&touch_state.x2, &touch_state.y2);
}

static void release_touch(void)
{
	uint16_t keymap[FXCG100_GBA_KEY_COUNT];

	memset(&touch_state, 0, sizeof(touch_state));
	fxcg100_poll_app_keys();
	/* The keys are repainted when the buttons are read. */
	fxcg100_keymap_defaults(keymap);
	fxcg100_poll_gba_buttons_mapped(keymap);
}

/* A point on a key's face that no caption or arrow covers. */
static void face_point(const cp_key *k, uint32_t direction, int *x, int *y)
{
	int cx = k->frame.x + k->frame.w / 2, cy = k->frame.y + k->frame.h / 2;

	*x = cx;
	*y = k->frame.y + 5;
	if (k->shape == CP_KEY_DPAD) {
		/* near the end of the arm, off its centre line */
		int reach = k->frame.w / 2 - 8;

		*x = cx + 12;
		*y = cy + 12;
		if (direction == CP_GBA_UP) *y = cy - reach;
		if (direction == CP_GBA_DOWN) *y = cy + reach;
		if (direction == CP_GBA_LEFT) *x = cx - reach;
		if (direction == CP_GBA_RIGHT) *x = cx + reach;
	} else if (k->shape == CP_KEY_SHOULDER_L || k->shape == CP_KEY_SHOULDER_R) {
		*y = k->frame.y + 6;
	}
}

static void expect_buttons(unsigned view, const char *what, uint32_t expected)
{
	uint16_t keymap[FXCG100_GBA_KEY_COUNT];

	fxcg100_keymap_defaults(keymap);
	fxcg100_poll_app_keys();
	CHECK(fxcg100_touch_gba_buttons() == expected,
		"view %u %s: touch gives %03x, expected %03x", view, what,
		(unsigned)fxcg100_touch_gba_buttons(), (unsigned)expected);
	CHECK(fxcg100_poll_gba_buttons_mapped(keymap) == expected,
		"view %u %s: merged buttons wrong", view, what);
}

static void test_touch(unsigned view, unsigned rot)
{
	static const uint32_t dirs[4] = {
		CP_GBA_UP, CP_GBA_DOWN, CP_GBA_LEFT, CP_GBA_RIGHT
	};
	cp_layout l = cp_layout_for(view, rot);
	const cp_key *dpad = NULL, *a = NULL, *b = NULL;
	char what[48];

	memset(key_held, 0, sizeof(key_held));
	fill_test_frame();
	set_mode(view, rot, 0);
	fxcg100_lcd_update();
	fxcg100_lcd_blit_gba(gba);

	for (unsigned i = 0; i < l.key_count; i++) {
		const cp_key *k = &l.keys[i];
		int cx = k->frame.x + k->frame.w / 2, cy = k->frame.y + k->frame.h / 2;
		int fx, fy;

		if (k->shape == CP_KEY_DPAD) { dpad = k; continue; }
		if (k->mask == CP_GBA_A) a = k;
		if (k->mask == CP_GBA_B) b = k;
		snprintf(what, sizeof(what), "key %u", i);

		/* The centre and all four corners of the touch range press it. */
		touch_user(view, rot, 1, cx, cy, 0, 0);
		expect_buttons(view, what, k->mask);
		face_point(k, 0, &fx, &fy);
		CHECK(user_pixel(view, rot, fx, fy) == KEY_FACE_LIT,
			"view %u key %u not lit (%04x)", view, i,
			user_pixel(view, rot, fx, fy));
		touch_user(view, rot, 1, k->hit.x, k->hit.y, 0, 0);
		expect_buttons(view, what, k->mask);
		touch_user(view, rot, 1, k->hit.x + k->hit.w - 1,
			k->hit.y + k->hit.h - 1, 0, 0);
		expect_buttons(view, what, k->mask);

		release_touch();
		CHECK(user_pixel(view, rot, fx, fy) == KEY_FACE,
			"view %u key %u still lit after release (%04x)", view, i,
			user_pixel(view, rot, fx, fy));
		CHECK(fxcg100_touch_gba_buttons() == 0, "touch stuck after release");
	}

	if (dpad) {
		cp_rect f = dpad->frame, h = dpad->hit;
		int cx = f.x + f.w / 2, cy = f.y + f.h / 2, fx, fy;

		for (unsigned d = 0; d < 4; d++) {
			int tx = cx, ty = cy;

			if (dirs[d] == CP_GBA_UP) ty = f.y + 10;
			if (dirs[d] == CP_GBA_DOWN) ty = f.y + f.h - 10;
			if (dirs[d] == CP_GBA_LEFT) tx = f.x + 10;
			if (dirs[d] == CP_GBA_RIGHT) tx = f.x + f.w - 10;
			touch_user(view, rot, 1, tx, ty, 0, 0);
			expect_buttons(view, "D-pad arm", dirs[d]);
			/* Only that arm is lit. */
			for (unsigned e = 0; e < 4; e++) {
				face_point(dpad, dirs[e], &fx, &fy);
				CHECK(user_pixel(view, rot, fx, fy) ==
					(e == d ? KEY_FACE_LIT : KEY_FACE),
					"view %u D-pad: arm %u wrong while %u is held", view,
					e, d);
			}
		}
		/* Corners give diagonals, out to the edge of the touch range. */
		touch_user(view, rot, 1, h.x, h.y, 0, 0);
		expect_buttons(view, "D-pad corner", CP_GBA_UP | CP_GBA_LEFT);
		touch_user(view, rot, 1, h.x + h.w - 1, h.y + h.h - 1, 0, 0);
		expect_buttons(view, "D-pad corner", CP_GBA_DOWN | CP_GBA_RIGHT);
		/* The hub is dead. */
		touch_user(view, rot, 1, cx, cy, 0, 0);
		expect_buttons(view, "D-pad hub", 0);
		release_touch();
		face_point(dpad, CP_GBA_UP, &fx, &fy);
		CHECK(user_pixel(view, rot, fx, fy) == KEY_FACE,
			"view %u D-pad still lit after release", view);
	}

	/* Portrait: the gap between the D-pad and B belongs to neither. */
	if (dpad && b && !cp_view_is_landscape(view)) {
		int gap0 = dpad->hit.x + dpad->hit.w, gap1 = b->hit.x;

		CHECK(gap1 - gap0 >= 16, "view %u: D-pad to B gap is only %d px",
			view, gap1 - gap0);
		touch_user(view, rot, 1, (gap0 + gap1) / 2,
			b->frame.y + b->frame.h / 2, 0, 0);
		expect_buttons(view, "D-pad/B gap", 0);
	}

	/* A key on the keypad lights its touch key too (EXE is A by default),
	 * and lets it go again. */
	if (a) {
		uint16_t keymap[FXCG100_GBA_KEY_COUNT];
		int fx, fy;

		fxcg100_keymap_defaults(keymap);
		face_point(a, 0, &fx, &fy);
		key_held[KEY_EXE] = 1;
		CHECK(fxcg100_poll_gba_buttons_mapped(keymap) == CP_GBA_A,
			"view %u: EXE is not A", view);
		CHECK(user_pixel(view, rot, fx, fy) == KEY_FACE_LIT,
			"view %u: A not lit from the keypad", view);
		key_held[KEY_EXE] = 0;
		CHECK(fxcg100_poll_gba_buttons_mapped(keymap) == 0,
			"view %u: EXE stuck", view);
		CHECK(user_pixel(view, rot, fx, fy) == KEY_FACE,
			"view %u: A still lit after the keypad key", view);
	}

	/* Two fingers: a direction and A together. */
	if (dpad && a) {
		touch_user(view, rot, 2, dpad->frame.x + 10,
			dpad->frame.y + dpad->frame.h / 2,
			a->frame.x + a->frame.w / 2, a->frame.y + a->frame.h / 2);
		expect_buttons(view, "two fingers", CP_GBA_A | CP_GBA_LEFT);
	}

	/* A touch on the picture presses nothing. */
	touch_user(view, rot, 1, l.image.x + l.image.w / 2,
		l.image.y + l.image.h / 2, 0, 0);
	expect_buttons(view, "picture", 0);
	release_touch();

	/* Touch keys off: nothing is painted and nothing registers. */
	fxcg100_lcd_set_touch_keys(0);
	fxcg100_lcd_blit_gba(gba);
	touch_user(view, rot, 1, l.keys[0].frame.x + 20, l.keys[0].frame.y + 20,
		0, 0);
	fxcg100_poll_app_keys();
	CHECK(fxcg100_touch_gba_buttons() == 0, "hidden touch keys still work");
	CHECK(user_pixel(view, rot, l.keys[0].frame.x + 20,
		l.keys[0].frame.y + 5) == 0x0000, "hidden touch key was painted");
	release_touch();
	fxcg100_lcd_set_touch_keys(1);
}

/* The printed icons under the LCD arrive as touches below row 527. */
static void test_icon_strip(void)
{
	static const int centre[CP_ICON_COUNT] = { 23, 69, 114, 160, 206, 251, 297 };
	uint16_t hotkeys[FXCG100_HOTKEY_COUNT];

	fxcg100_hotkey_defaults(hotkeys);
	for (int i = 0; i < CP_ICON_COUNT; i++) {
		uint32_t app, expected_app = 0;

		memset(&touch_state, 0, sizeof(touch_state));
		touch_state.touches = 1;
		touch_state.x1 = centre[i];
		touch_state.y1 = 559;
		/* The first poll only sees the touch arrive. */
		app = fxcg100_poll_app_keys();
		CHECK(fxcg100_touch_strip_icons() == 0 && app == 0,
			"icon %d acted on a single sample", i);
		app = fxcg100_poll_app_keys();
		CHECK(fxcg100_touch_strip_icons() == 1u << i,
			"icon %d: strip reads %02x", i,
			(unsigned)fxcg100_touch_strip_icons());
		CHECK(fxcg100_touch_gba_buttons() == 0, "icon %d presses a button", i);

		if (i == CP_ICON_MENU || i == CP_ICON_SETTINGS)
			expected_app = FXCG100_APPKEY_ON | FXCG100_APPKEY_AC;
		if (i == CP_ICON_ESC)
			expected_app = FXCG100_APPKEY_BACK;
		CHECK(app == expected_app, "icon %d: app keys %03x", i, (unsigned)app);
		CHECK(fxcg100_poll_hotkeys_mapped(hotkeys) == (i == CP_ICON_MAIN ?
			FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD) : 0u),
			"icon %d: hotkeys wrong", i);
		release_touch();
	}
	/* A touch that began on the screen and slid down onto an icon is not a
	 * press of that icon. */
	touch_state.touches = 1;
	touch_state.x1 = 120;
	touch_state.y1 = 500;
	fxcg100_poll_app_keys();
	touch_state.y1 = 555;
	fxcg100_poll_app_keys();
	fxcg100_poll_app_keys();
	CHECK(fxcg100_touch_strip_icons() == 0, "a slide off the screen pressed "
		"an icon");
	release_touch();
	touch_state.touches = 1;
	touch_state.x1 = 23;
	/* The last LCD rows and the margin under them are not icons. */
	touch_state.y1 = 527;
	fxcg100_poll_app_keys();
	CHECK(fxcg100_touch_strip_icons() == 0, "LCD row read as an icon");
	release_touch();
	touch_state.touches = 1;
	touch_state.x1 = 23;
	touch_state.y1 = CP_STRIP_Y - 1;
	fxcg100_poll_app_keys();
	fxcg100_poll_app_keys();
	CHECK(fxcg100_touch_strip_icons() == 0, "margin read as an icon");
	release_touch();
	CHECK(fxcg100_touch_strip_icons() == 0, "icon stuck after release");
}

/* The cursor key that points up on screen must read as UP. */
static void test_arrow_remap(void)
{
	static const struct { unsigned view, rot; int up, right; } cases[] = {
		{ CP_VIEW_PORTRAIT_43,   CP_ROTATION_KEYS_RIGHT, KEY_UP,    KEY_RIGHT },
		{ CP_VIEW_LANDSCAPE_2X,  CP_ROTATION_KEYS_RIGHT, KEY_RIGHT, KEY_DOWN },
		{ CP_VIEW_LANDSCAPE_15X, CP_ROTATION_KEYS_LEFT,  KEY_LEFT,  KEY_UP },
	};

	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		set_mode(cases[i].view, cases[i].rot, 0);
		memset(key_held, 0, sizeof(key_held));
		key_held[cases[i].up] = 1;
		CHECK(fxcg100_key_down(KEY_UP), "case %u: UP not read", i);
		CHECK(!fxcg100_key_down(KEY_DOWN) && !fxcg100_key_down(KEY_LEFT) &&
			!fxcg100_key_down(KEY_RIGHT), "case %u: extra direction", i);
		CHECK(fxcg100_poll_app_keys() & FXCG100_APPKEY_UP,
			"case %u: menu UP not read", i);
		memset(key_held, 0, sizeof(key_held));
		key_held[cases[i].right] = 1;
		CHECK(fxcg100_key_down(KEY_RIGHT), "case %u: RIGHT not read", i);
	}
	memset(key_held, 0, sizeof(key_held));
}

/* The menu canvas appears upright and centred in landscape. */
static void test_canvas(unsigned rot, const char *dir, const char *name)
{
	set_mode(CP_VIEW_LANDSCAPE_15X, rot, 0);
	fxcg100_lcd_clear(0x1234);
	fxcg100_lcd_fill_rect(0, 0, 320, 18, 0x4321);
	fxcg100_lcd_fill_rect(10, 100, 20, 30, 0xF800);
	fxcg100_lcd_draw_text(8, 40, "GPSP MENU LINE", 0xFFFF, 0x1234);
	fxcg100_lcd_draw_text(8, 60, "SECOND LINE", 0xFFE0, 0x1234);
	fxcg100_lcd_update();

	CHECK(user_pixel(CP_VIEW_LANDSCAPE_15X, rot, 104 + 15, 110) == 0xF800,
		"canvas rect not where expected");
	CHECK(user_pixel(CP_VIEW_LANDSCAPE_15X, rot, 104 + 5, 110) == 0x1234,
		"canvas background wrong");
	/* Margins repeat the canvas edge, so the header spans the screen. */
	CHECK(user_pixel(CP_VIEW_LANDSCAPE_15X, rot, 3, 5) == 0x4321 &&
		user_pixel(CP_VIEW_LANDSCAPE_15X, rot, 524, 5) == 0x4321,
		"header does not span the landscape screen");
	CHECK(user_pixel(CP_VIEW_LANDSCAPE_15X, rot, 3, 200) == 0x1234,
		"margin background wrong");
	write_ppm(dir, name, CP_VIEW_LANDSCAPE_15X, rot);
}

/* ---- interface ------------------------------------------------------------ */

static cp_clock stock_clock(void);

static cp_ui ui;
static fxcg100_menu_state ui_state;

/* One step, a frame (about 30 ms) later. */
static fxcg100_menu_result ui_frame(void)
{
	fake_ticks += 4;
	return cp_ui_step(&ui);
}

/* Let whatever is moving come to rest. Bounded: a countdown never does. */
static fxcg100_menu_result ui_settle(fxcg100_menu_result r)
{
	for (int i = 0; i < 80 && r == FXCG100_MENU_CONTINUE &&
			(ui.animating || (ui.redraw && i < 4)); i++)
		r = ui_frame();
	return r;
}

static void ui_open(int in_game, unsigned view)
{
	memset(key_held, 0, sizeof(key_held));
	memset(&touch_state, 0, sizeof(touch_state));
	fxcg100_menu_init(&ui_state);
	ui_state.screen_scale = view;
	ui_state.rom_source = 4;
	cp_ui_begin(&ui, &ui_state, in_game);
	CHECK(ui_settle(cp_ui_step(&ui)) == FXCG100_MENU_CONTINUE,
		"first draw returned");
}

/* A tap at a point the player sees: finger down for one step, then up. The
 * point is turned the way the interface was last drawn, which is not the
 * saved view while gpSP still starts upright. */
static fxcg100_menu_result ui_tap(int ux, int uy)
{
	fxcg100_menu_result r;

	touch_user(cp_view, cp_rotation, 1, ux, uy, 0, 0);
	r = ui_frame();
	if (r != FXCG100_MENU_CONTINUE)
		return r;
	memset(&touch_state, 0, sizeof(touch_state));
	return ui_settle(ui_frame());
}

/* Where the control that says `name` was drawn at the last step (the n-th
 * such control, from 0), or NULL. */
static const cp_rect *ui_find(const char *name, int n)
{
	for (int i = 0; i < ui_mark_count; i++)
		if (!strcmp(ui_marks[i].name, name) && n-- == 0)
			return &ui_marks[i].r;
	return NULL;
}

/* Tap the middle of a control found by what it says. */
static fxcg100_menu_result ui_tap_on(const char *name, int n)
{
	const cp_rect *r = ui_find(name, n);

	CHECK(r != NULL, "nothing on screen says \"%s\"", name);
	if (!r)
		return FXCG100_MENU_CONTINUE;
	return ui_tap(r->x + r->w / 2, r->y + r->h / 2);
}

static fxcg100_menu_result ui_key(int key)
{
	fxcg100_menu_result r;

	key_held[key] = 1;
	r = ui_frame();
	key_held[key] = 0;
	if (r == FXCG100_MENU_CONTINUE)
		r = ui_frame();
	return ui_settle(r);
}

static void ui_shot(const char *dir, const char *name)
{
	ui_settle(FXCG100_MENU_CONTINUE);
	write_ppm(dir, name, ui_state.screen_scale, ui_state.rotation);
}

/* Scroll the current page so its end is in view. */
static void ui_scroll_to_end(void)
{
	ui.scroll[ui.screen] = 100000;
	ui.redraw = 1;
	ui_settle(ui_frame());
}

static void test_ui(const char *dir)
{
	const cp_rect *r;
	uint16_t before;

	/* Library: a tap on a row starts that game. */
	ui_open(0, CP_VIEW_PORTRAIT_43);
	CHECK(ui.screen == SCREEN_LIBRARY, "no game: should open the library");
	ui_shot(dir, "ui-library-portrait");
	CHECK(ui_tap_on("Kirby Nightmare in Dream Land", 0) ==
		FXCG100_MENU_LOAD_GAME && ui_state.rom_source == 2,
		"tap on the third row loads game %u", (unsigned)ui_state.rom_source);

	/* Dragging scrolls and does not start a game; the list keeps moving for
	 * a moment after a flick and stops at its end. */
	ui_open(0, CP_VIEW_PORTRAIT_43);
	touch_user(CP_VIEW_PORTRAIT_43, 0, 1, 160, 400, 0, 0);
	ui_frame();
	touch_user(CP_VIEW_PORTRAIT_43, 0, 1, 160, 340, 0, 0);
	ui_frame();
	touch_user(CP_VIEW_PORTRAIT_43, 0, 1, 160, 300, 0, 0);
	ui_frame();
	memset(&touch_state, 0, sizeof(touch_state));
	CHECK(ui_frame() == FXCG100_MENU_CONTINUE, "drag started a game");
	CHECK(ui.scroll[SCREEN_LIBRARY] >= 100, "drag did not scroll (%d)",
		ui.scroll[SCREEN_LIBRARY]);
	{
		int at_release = ui.scroll[SCREEN_LIBRARY];

		ui_settle(FXCG100_MENU_CONTINUE);
		CHECK(ui.scroll[SCREEN_LIBRARY] > at_release,
			"a flick should carry on (%d -> %d)", at_release,
			ui.scroll[SCREEN_LIBRARY]);
		CHECK(!ui.animating && ui.fling == 0, "the flick never stopped");
	}
	ui_scroll_to_end();
	CHECK(ui_find("Zelda A Link to the Past", 0) != NULL &&
		ui_find("Zelda A Link to the Past", 0)->y + GAME_ROW_H <= 528,
		"the last game is not reachable");
	CHECK(ui.scroll[SCREEN_LIBRARY] < 100000, "scrolled past the end");

	/* gpSP starts upright even if it was last used in landscape, without
	 * forgetting the landscape view: the game gets it back. */
	fake_saved_view = CP_VIEW_LANDSCAPE_2X;
	memset(key_held, 0, sizeof(key_held));
	memset(&touch_state, 0, sizeof(touch_state));
	fxcg100_menu_init(&ui_state);
	fake_saved_view = -1;
	cp_ui_begin(&ui, &ui_state, 0);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.w == 320 && ui.h == 528, "the first screen is not upright");
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X,
		"the saved landscape view was lost (%u)",
		(unsigned)ui_state.screen_scale);
	CHECK(ui_tap_on("Golden Sun", 0) == FXCG100_MENU_LOAD_GAME &&
		ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X,
		"starting a game from the upright library");
	/* With the game running, the menu is the way round the game is. */
	cp_ui_begin(&ui, &ui_state, 1);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.w == 528 && ui.h == 320, "the pause menu did not turn back");
	/* Turning the screen in that first library is a choice, and shows. */
	fake_saved_view = CP_VIEW_LANDSCAPE_2X;
	fxcg100_menu_init(&ui_state);
	fake_saved_view = -1;
	cp_ui_begin(&ui, &ui_state, 0);
	ui_settle(cp_ui_step(&ui));
	memset(&touch_state, 0, sizeof(touch_state));
	touch_state.touches = 1;
	touch_state.x1 = 206;                       /* Swap */
	touch_state.y1 = 550;
	ui_frame();
	ui_frame();
	memset(&touch_state, 0, sizeof(touch_state));
	ui_settle(ui_frame());
	CHECK(ui.w == 528, "a turn in the first library did not show");
	/* Saved upside down, it starts upright as well, and the game and its
	 * pause menu get the upside-down view back. */
	fake_saved_view = CP_VIEW_PORTRAIT_43;
	fake_saved_rotation = CP_ROTATION_KEYS_LEFT;
	memset(&touch_state, 0, sizeof(touch_state));
	fxcg100_menu_init(&ui_state);
	fake_saved_view = -1;
	fake_saved_rotation = CP_ROTATION_KEYS_RIGHT;
	cp_ui_begin(&ui, &ui_state, 0);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.s.mode == CP_SURFACE_UPRIGHT && cp_rotation ==
		CP_ROTATION_KEYS_RIGHT, "the first screen is upside down");
	CHECK(ui_state.screen_scale == CP_VIEW_PORTRAIT_43 &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT,
		"the saved upside-down view was lost (%u, %u)",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	CHECK(ui_tap_on("Golden Sun", 0) == FXCG100_MENU_LOAD_GAME &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT,
		"starting a game from the upright library");
	cp_ui_begin(&ui, &ui_state, 1);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.s.mode == CP_SURFACE_FLIPPED && ui.w == 320,
		"the pause menu is not upside down like the game");

	/* Sizes and turning: Resize goes round the sizes of an orientation;
	 * turning keeps 1:1 as 1:1 and takes the larger sizes to the largest. */
	CHECK(cp_view_resized(CP_VIEW_PORTRAIT_1X) == CP_VIEW_PORTRAIT_43 &&
		cp_view_resized(CP_VIEW_PORTRAIT_43) == CP_VIEW_PORTRAIT_1X &&
		cp_view_resized(CP_VIEW_LANDSCAPE_1X) == CP_VIEW_LANDSCAPE_15X &&
		cp_view_resized(CP_VIEW_LANDSCAPE_15X) == CP_VIEW_LANDSCAPE_2X &&
		cp_view_resized(CP_VIEW_LANDSCAPE_2X) == CP_VIEW_LANDSCAPE_1X,
		"Resize order");
	CHECK(cp_view_turned(CP_VIEW_PORTRAIT_1X) == CP_VIEW_LANDSCAPE_1X &&
		cp_view_turned(CP_VIEW_LANDSCAPE_1X) == CP_VIEW_PORTRAIT_1X &&
		cp_view_turned(CP_VIEW_PORTRAIT_43) == CP_VIEW_LANDSCAPE_2X &&
		cp_view_turned(CP_VIEW_LANDSCAPE_2X) == CP_VIEW_PORTRAIT_43 &&
		cp_view_turned(CP_VIEW_LANDSCAPE_15X) == CP_VIEW_PORTRAIT_43,
		"turning");
	/* Landscape 2x: the strip is A, B, Select, Start, on the side away from
	 * the keypad. */
	{
		cp_layout ccw = cp_layout_for(CP_VIEW_LANDSCAPE_2X,
			CP_ROTATION_KEYS_RIGHT);
		cp_layout cw = cp_layout_for(CP_VIEW_LANDSCAPE_2X,
			CP_ROTATION_KEYS_LEFT);

		CHECK(ccw.key_count == 4 && ccw.keys[0].mask == CP_GBA_A &&
			ccw.keys[1].mask == CP_GBA_B && ccw.keys[2].mask == CP_GBA_SELECT
			&& ccw.keys[3].mask == CP_GBA_START, "2x strip keys");
		CHECK(ccw.keys[0].hit.x == 0 && ccw.image.x == 48,
			"keypad right: strip must be on the left");
		CHECK(cw.keys[0].hit.x == 480 && cw.image.x == 0,
			"keypad left: strip must be on the right");
	}

	/* Library sorting: tapping the top-right Sort icon opens the sort dialog;
	 * picking Last Played / Newly Added reorders the rows while preserving
	 * each game's stable rom_source index. */
	ui_open(0, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Sort icon", 0) == FXCG100_MENU_CONTINUE && ui.sort_open,
		"tap on Sort icon opens sort dialog");
	ui_shot(dir, "ui-library-sort");
	CHECK(ui_tap_on("Last Played", 0) == FXCG100_MENU_CONTINUE &&
		!ui.sort_open &&
		ui_state.game_sort == FXCG100_GAME_SORT_LAST_PLAYED &&
		ui.settings_dirty,
		"tap on Last Played sort option (sort=%u)",
		(unsigned)ui_state.game_sort);
	{
		const cp_rect *r_wario = ui_find("Wario Land 4", 0);
		const cp_rect *r_metroid = ui_find("Metroid Fusion", 0);
		const cp_rect *r_golden = ui_find("Golden Sun", 0);
		const cp_rect *r_aw = ui_find("Advance Wars", 0);

		CHECK(r_wario && r_metroid && r_golden && r_aw &&
			r_wario->y < r_metroid->y && r_metroid->y < r_golden->y &&
			r_golden->y < r_aw->y,
			"Last Played did not order recently played games first");
	}
	CHECK(ui_tap_on("Wario Land 4", 0) == FXCG100_MENU_LOAD_GAME &&
		ui_state.rom_source == 8,
		"tapping top Last Played game chose %u, want 8",
		(unsigned)ui_state.rom_source);

	ui_open(0, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Sort icon", 0) == FXCG100_MENU_CONTINUE && ui.sort_open,
		"tap on Sort icon opens sort dialog (2)");
	CHECK(ui_tap_on("Newly Added", 0) == FXCG100_MENU_CONTINUE &&
		!ui.sort_open &&
		ui_state.game_sort == FXCG100_GAME_SORT_NEWLY_ADDED,
		"tap on Newly Added sort option");
	{
		const cp_rect *r_zelda = ui_find("Zelda A Link to the Past", 0);
		const cp_rect *r_tetris = ui_find("Tetris Worlds", 0);

		CHECK(r_zelda && r_tetris && r_zelda->y < r_tetris->y,
			"Newly Added did not put newest games first");
	}
	CHECK(ui_tap_on("Zelda A Link to the Past", 0) == FXCG100_MENU_LOAD_GAME &&
		ui_state.rom_source == 11,
		"tapping top Newly Added game chose %u, want 11",
		(unsigned)ui_state.rom_source);

	/* Keypad: Down reveals the focus, Down moves it, EXE starts the game. */
	ui_open(0, CP_VIEW_PORTRAIT_43);
	ui_key(KEY_DOWN);
	CHECK(ui.focus_shown && ui.focus == 0, "first Down should show focus");
	ui_key(KEY_DOWN);
	ui_key(KEY_DOWN);
	ui_key(KEY_DOWN);    /* bar icons are 0 (Close), 1 (Sort), 2 (Settings); rows follow */
	ui_key(KEY_DOWN);
	CHECK(ui_key(KEY_EXE) == FXCG100_MENU_LOAD_GAME &&
		ui_state.rom_source == 1, "keypad chose game %u (focus %d)",
		(unsigned)ui_state.rom_source, ui.focus);

	/* Exit from the library with no game running. */
	ui_open(0, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Close", 0) == FXCG100_MENU_QUIT, "Close icon");

	/* An empty library says what to do. */
	fake_game_count = 0;
	ui_open(0, CP_VIEW_PORTRAIT_43);
	ui_shot(dir, "ui-library-empty");
	fake_game_count = 12;

	/* With a game running the library offers the way back to it. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_tap_on("Games", 0);
	CHECK(ui.screen == SCREEN_LIBRARY, "Games item");
	ui_shot(dir, "ui-library-playing");
	CHECK(ui_tap_on("Now playing", 0) == FXCG100_MENU_RETURN,
		"the now-playing card resumes");

	/* Pause menu. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui.screen == SCREEN_PAUSE, "in a game: should open the pause menu");
	ui_shot(dir, "ui-pause-portrait");
	CHECK(ui_tap_on("Resume", 0) == FXCG100_MENU_RETURN, "Resume");
	/* Empty slot 0 saves immediately without asking to overwrite. */
	fake_savestate_mask = 0;
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Save state", 0) == FXCG100_MENU_SAVE_STATE &&
		ui.confirm == CONFIRM_NONE, "empty slot should save without asking");
	/* Once slot 0 has a file, Save state asks before overwriting. */
	fake_savestate_mask = 1u << 0;
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Save state", 0) == FXCG100_MENU_CONTINUE &&
		ui.confirm == CONFIRM_SAVE, "Save state should ask when slot exists");
	CHECK(ui_tap_on("Cancel", 0) == FXCG100_MENU_CONTINUE &&
		ui.confirm == CONFIRM_NONE, "Save state Cancel");
	CHECK(ui_tap_on("Save state", 0) == FXCG100_MENU_CONTINUE &&
		ui.confirm == CONFIRM_SAVE, "Save state asks again");
	CHECK(ui_tap_on("Save", 0) == FXCG100_MENU_SAVE_STATE, "Save state confirm");
	fake_savestate_mask = 0;
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui_tap_on("Load state", 0) == FXCG100_MENU_LOAD_STATE, "Load state");
	/* Slot chips. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_tap_on("7", 0);
	CHECK(ui_state.savestate_slot == 7, "slot chip gives %u",
		(unsigned)ui_state.savestate_slot);
	/* Everything on the pause menu fits without scrolling. */
	r = ui_find("Exit gpSP", 0);
	CHECK(r && r->y + r->h <= 528, "pause menu runs off the screen");
	/* Restart asks first; Cancel goes back, Restart confirms. */
	CHECK(ui_tap_on("Restart", 0) == FXCG100_MENU_CONTINUE &&
		ui.confirm == CONFIRM_RESTART, "Restart should ask");
	ui_shot(dir, "ui-confirm");
	/* The page under the dialog is darkened and does not take the tap. */
	CHECK(ui.dim == SCRIM, "page not dimmed under the dialog (%d)", ui.dim);
	CHECK(ui_find("Resume", 0) == NULL, "page under the dialog is live");
	CHECK(ui_tap_on("Cancel", 0) == FXCG100_MENU_CONTINUE &&
		ui.confirm == CONFIRM_NONE, "Cancel");
	CHECK(ui.dim == 0, "scrim left behind (%d)", ui.dim);
	ui_tap_on("Restart", 0);
	CHECK(ui_tap_on("Restart", 0) == FXCG100_MENU_RESET, "confirmed Restart");
	/* Clear resumes. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui_key(KEY_CLEAR) == FXCG100_MENU_RETURN, "Clear resumes");
	ui_open(1, CP_VIEW_LANDSCAPE_15X);
	ui_shot(dir, "ui-pause-landscape");
	r = ui_find("Exit gpSP", 0);
	CHECK(r && r->y + r->h <= 320, "landscape pause menu runs off the screen");
	r = ui_find("Load state", 0);
	CHECK(r && r->y + r->h <= 320 && r->x + r->w <= 528,
		"landscape state card runs off the screen");

	/* Settings: choosing Landscape turns the interface and is saved on the
	 * way out. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_tap_on("Settings", 0);
	CHECK(ui.screen == SCREEN_SETTINGS, "Settings item");
	ui_shot(dir, "ui-settings-portrait");
	config_saves = 0;
	ui_tap_on("CCW", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X &&
		ui_state.rotation == CP_ROTATION_KEYS_RIGHT,
		"CCW gives view %u rotation %u",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	ui_tap_on("CW", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT,
		"CW gives view %u rotation %u",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	CHECK(ui.w == 528 && ui.h == 320, "interface did not turn");
	/* Landscape has three picture sizes. */
	ui_tap_on("Original", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_1X, "landscape 1:1 (%u)",
		(unsigned)ui_state.screen_scale);
	ui_tap_on("1.5x", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_15X, "landscape 1.5x");
	ui_tap_on("2x", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X, "landscape 2x");
	ui_shot(dir, "ui-settings-landscape");
	/* 180 is portrait held upside down: the interface turns over, keeps
	 * taking taps where they are seen, and Portrait turns it back. */
	ui_tap_on("180", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_PORTRAIT_43 &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT,
		"180 gives view %u rotation %u",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	CHECK(ui.w == 320 && ui.h == 528 && ui.s.mode == CP_SURFACE_FLIPPED,
		"interface did not turn over");
	CHECK(cp_physical_direction(cp_view, cp_rotation, 0) == 2,
		"upside down, up in the menu is not the DOWN key");
	ui_tap_on("Original", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_PORTRAIT_1X &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT,
		"upside-down 1:1 (%u, %u)", (unsigned)ui_state.screen_scale,
		(unsigned)ui_state.rotation);
	ui_tap_on("Portrait", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_PORTRAIT_1X &&
		ui_state.rotation == CP_ROTATION_KEYS_RIGHT &&
		ui.s.mode == CP_SURFACE_UPRIGHT, "Portrait gives view %u rotation %u",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	ui_tap_on("180", 0);
	ui_tap_on("CW", 0);
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_1X &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT && ui.w == 528,
		"CW from 180 gives view %u rotation %u",
		(unsigned)ui_state.screen_scale, (unsigned)ui_state.rotation);
	CHECK(ui_tap_on("Back", 0) == FXCG100_MENU_CONTINUE &&
		ui.screen == SCREEN_PAUSE, "Back from settings");
	CHECK(config_saves == 1, "settings saved %d times", config_saves);

	/* A switch row flips on a tap anywhere on it. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_tap_on("Settings", 0);
	ui_tap_on("Touch keys", 0);
	CHECK(ui_state.touch_keys == 0, "switch row did not turn off");
	ui_tap_on("Touch keys", 0);
	CHECK(ui_state.touch_keys == 1, "switch row did not turn back on");

	/* Sliders: a tap jumps to the nearest stop; a sideways drag moves the
	 * handle and leaves the page where it is; a drag up the page that
	 * starts on a slider scrolls the page and leaves the value alone. */
	r = ui_find("Backlight", 0);
	if (!r || r->y + r->h > 480) {
		ui.scroll[SCREEN_SETTINGS] += r ? r->y - 200 : 300;
		ui.redraw = 1;
		ui_settle(ui_frame());
		r = ui_find("Backlight", 0);
	}
	CHECK(r != NULL, "no Backlight slider");
	if (r) {
		cp_rect s = *r;
		int cy = s.y + s.h / 2, x0 = s.x + 8, span = s.w - 16, scroll;

		ui_tap(s.x + s.w - 4, cy);
		CHECK(ui_state.backlight == 16, "tap at the right end gives %u",
			(unsigned)ui_state.backlight);
		ui_tap(s.x + 4, cy);
		CHECK(ui_state.backlight == 0, "tap at the left end gives %u",
			(unsigned)ui_state.backlight);
		ui_tap(x0 + span * 5 / 16 + 3, cy);
		CHECK(ui_state.backlight == 5, "tap at the fifth stop gives %u",
			(unsigned)ui_state.backlight);

		scroll = ui.scroll[SCREEN_SETTINGS];
		touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span * 5 / 16, cy, 0, 0);
		ui_frame();
		for (int i = 6; i <= 12; i++) {
			touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span * i / 16,
				cy + (i & 1), 0, 0);
			ui_frame();
		}
		/* Drag to the right end: the held value label sits over the row's
		 * right-aligned value text and must paint on top of it, leaving its
		 * right padding clean ui.t.on_surface. */
		touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span, cy, 0, 0);
		ui_frame();
		CHECK(ui_state.backlight == 16 && ui.slider_tip,
			"drag to right end gave value %u, tip %d",
			(unsigned)ui_state.backlight, ui.slider_tip);
		{
			cp_rect b = ui.slider_tip_rect;
			int clean = 1;

			for (int y = b.y + b.h / 2 - 4; y <= b.y + b.h / 2 + 4; y++)
				for (int x = b.x + b.w - 8; x <= b.x + b.w - 3; x++)
					if (user_pixel(CP_VIEW_PORTRAIT_43, 0, x, y) !=
							ui.t.on_surface)
						clean = 0;
			CHECK(clean,
				"row value text painted over the held slider label");
		}
		ui_shot(dir, "ui-settings-slider-held-right");

		/* Drag back to 12 for the held screenshot and release check. */
		touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span * 12 / 16 + 6, cy,
			0, 0);
		ui_frame();
		CHECK(ui_state.backlight == 12, "drag to the 12th stop gives %u",
			(unsigned)ui_state.backlight);
		ui_shot(dir, "ui-settings-slider-held");
		memset(&touch_state, 0, sizeof(touch_state));
		ui_settle(ui_frame());
		CHECK(ui_state.backlight == 12, "letting go moved it to %u",
			(unsigned)ui_state.backlight);
		CHECK(ui.scroll[SCREEN_SETTINGS] == scroll,
			"a sideways drag scrolled the page (%d -> %d)", scroll,
			ui.scroll[SCREEN_SETTINGS]);

		/* With the slider right under the top bar, the held value label
		 * reaches above bar_h and must not be clipped by the scroll area. */
		{
			int saved_scroll = ui.scroll[SCREEN_SETTINGS];
			const cp_rect *top_r;

			ui.scroll[SCREEN_SETTINGS] += s.y - (ui.bar_h + 4);
			ui.redraw = 1;
			ui_settle(ui_frame());
			top_r = ui_find("Backlight", 0);
			CHECK(top_r != NULL, "no Backlight slider under top bar");
			if (top_r) {
				int tcy = top_r->y + top_r->h / 2;
				int tx = top_r->x + 8 + (top_r->w - 16) * 12 / 16;
				cp_rect b;

				touch_user(CP_VIEW_PORTRAIT_43, 0, 1, tx, tcy, 0, 0);
				ui_frame();
				b = ui.slider_tip_rect;
				CHECK(ui.slider_tip && b.y < ui.bar_h,
					"slider label did not reach over top bar (tip=%d, y=%d)",
					ui.slider_tip, (int)b.y);
				CHECK(user_pixel(CP_VIEW_PORTRAIT_43, 0, b.x + 6,
						b.y + b.h / 2) == ui.t.on_surface,
					"held slider label was clipped by the top bar");
				ui_shot(dir, "ui-settings-slider-held-top");
				memset(&touch_state, 0, sizeof(touch_state));
				ui_settle(ui_frame());
			}
			ui.scroll[SCREEN_SETTINGS] = saved_scroll;
			ui.redraw = 1;
			ui_settle(ui_frame());
		}

		touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span / 4, cy, 0, 0);
		ui_frame();
		for (int i = 1; i <= 4; i++) {
			touch_user(CP_VIEW_PORTRAIT_43, 0, 1, x0 + span / 4 + (i & 1),
				cy - 20 * i, 0, 0);
			ui_frame();
		}
		memset(&touch_state, 0, sizeof(touch_state));
		ui_settle(ui_frame());
		CHECK(ui_state.backlight == 12, "a drag up the page moved it to %u",
			(unsigned)ui_state.backlight);
		CHECK(ui.scroll[SCREEN_SETTINGS] > scroll,
			"a drag up the page from a slider did not scroll");
		ui_shot(dir, "ui-settings-sliders");

		/* High PWM rates enforce a minimum Backlight stop (9 -> 5, 8 -> 4,
		 * ..., 5 -> 1) both when raising PWM and when lowering Backlight. */
		{
			const cp_rect *br = ui_find("Backlight", 0);
			const cp_rect *pr;

			if (br) {
				ui.scroll[SCREEN_SETTINGS] += br->y - 200;
				ui.redraw = 1;
				ui_settle(ui_frame());
			}
			br = ui_find("Backlight", 0);
			pr = ui_find("Backlight PWM rate", 0);
			CHECK(pr != NULL && br != NULL, "no PWM/Backlight sliders");
			if (pr && br) {
				cp_rect ps = *pr, bs = *br;
				int pcy = ps.y + ps.h / 2, px0 = ps.x + 8, pspan = ps.w - 16;
				int bcy = bs.y + bs.h / 2;

				/* Start at Backlight = 0 (System); raising PWM to 9 bumps it to 5. */
				ui_tap(bs.x + 4, bcy);
				CHECK(ui_state.backlight == 0, "reset backlight to 0");
				ui_tap(ps.x + ps.w - 4, pcy);
				CHECK(ui_state.backlight_pwm == 9 && ui_state.backlight == 5,
					"pwm 9 bumped backlight to %u (pwm=%u)",
					(unsigned)ui_state.backlight,
					(unsigned)ui_state.backlight_pwm);
				/* Trying to tap Backlight to 0 stops at 5. */
				ui_tap(bs.x + 4, bcy);
				CHECK(ui_state.backlight == 5,
					"pwm 9 clamped backlight tap to %u",
					(unsigned)ui_state.backlight);
				/* Step PWM down 8..5 and verify the floor drops 4..1. */
				for (uint32_t pwm = 8; pwm >= 5; pwm--) {
					ui_tap(px0 + pspan * (int)pwm / 9, pcy);
					ui_tap(bs.x + 4, bcy);
					CHECK(ui_state.backlight_pwm == pwm &&
						ui_state.backlight == pwm - 4u,
						"pwm %u floor gave backlight %u (pwm=%u)",
						(unsigned)pwm, (unsigned)ui_state.backlight,
						(unsigned)ui_state.backlight_pwm);
				}
				/* PWM 0 (System) restores full 0..16 range. */
				ui_tap(ps.x + 4, pcy);
				ui_tap(bs.x + 4, bcy);
				CHECK(ui_state.backlight_pwm == 0 && ui_state.backlight == 0,
					"pwm 0 allowed backlight 0 (got %u, pwm=%u)",
					(unsigned)ui_state.backlight,
					(unsigned)ui_state.backlight_pwm);
			}
		}
	}

	/* Refresh position: asked first, because the page flashes; Cancel stays
	 * on Settings. The page has the slider, and draws a new colour with
	 * every frame whether there is input or not. */
	r = ui_find("VSync", 0);
	ui.scroll[SCREEN_SETTINGS] += r ? r->y - 200 : -200;
	ui.redraw = 1;
	ui_settle(ui_frame());
	{
		const cp_rect *hr = ui_find("Refresh rate", 0);

		CHECK(hr != NULL, "no Refresh rate slider");
		if (hr) {
			cp_rect hs = *hr;
			int hcy = hs.y + hs.h / 2;

			ui_tap(hs.x + hs.w - 4, hcy);
			CHECK(ui_state.lcd_refresh == CP_REFRESH_COUNT - 1u,
				"refresh max tap gave %u", (unsigned)ui_state.lcd_refresh);
			ui_tap(hs.x + 4, hcy);
			CHECK(ui_state.lcd_refresh == 0, "refresh min tap gave %u",
				(unsigned)ui_state.lcd_refresh);
		}
	}
	if (!ui_state.lcd_sync)
		ui_tap_on("VSync", 0);
	CHECK(ui_state.lcd_sync == 1, "refresh wait did not turn on");
	ui_tap_on("Refresh position", 0);
	CHECK(ui.screen == SCREEN_SYNC, "Refresh position did not open the page");
	{
		uint32_t before = ui.sync_frame;
		const cp_rect *p;

		ui_frame();
		ui_frame();
		CHECK(ui.sync_frame == before + 2 && ui.redraw,
			"the page does not advance the moving bar every frame");
		p = ui_find("Position", 0);
		CHECK(p != NULL, "no Position slider on the page");
		if (p) {
			ui_tap(p->x + p->w - 4, p->y + p->h / 2);
			CHECK(ui_state.lcd_sync_offset == 15, "tap at the end gives %u",
				(unsigned)ui_state.lcd_sync_offset);
			ui_tap(p->x + 8 + (p->w - 16) * 2 / 15 + 2, p->y + p->h / 2);
			CHECK(ui_state.lcd_sync_offset == 2, "tap at the 2nd stop gives %u",
				(unsigned)ui_state.lcd_sync_offset);
		}
		ui_shot(dir, "ui-sync-page");
	}
	ui_key(KEY_DEL);
	ui_frame();
	CHECK(ui.screen == SCREEN_SETTINGS, "back did not leave the page");
	ui.redraw = 1;
	ui_settle(ui_frame());

	/* Colour: every scheme can be chosen, the page takes it, and so do the
	 * touch keys once the game is back. */
	for (unsigned t = CP_THEME_COUNT; t-- > 0;) {
		r = ui_find(cp_theme_names[t], 0);
		if (!r || r->y + r->h > 520) {
			/* Bring it well inside the page, not past its top. */
			ui.scroll[SCREEN_SETTINGS] += r ? r->y + r->h - 400 : 200;
			ui.redraw = 1;
			ui_settle(ui_frame());
		}
		ui_tap_on(cp_theme_names[t], 0);
		CHECK(ui_state.theme == t, "swatch %s gives scheme %u",
			cp_theme_names[t], (unsigned)ui_state.theme);
		CHECK(ui.t.primary == cp_themes[t].primary &&
			user_pixel(CP_VIEW_PORTRAIT_43, 0, 2, 200) == cp_themes[t].surface,
			"page did not take scheme %s", cp_theme_names[t]);
		if (t == CP_THEME_VIOLET)
			ui_shot(dir, "ui-settings-violet");
	}
	CHECK(ui_state.theme == CP_THEME_GREEN, "the default scheme is green");
	CHECK(ui_state.dark_mode == 1, "dark mode is on by default");
	ui_shot(dir, "ui-settings-appearance");

	/* Dark mode off: switches to the light schemes, while in-game touch keys
	 * remain on the dark palette. */
	r = ui_find("Dark mode", 0);
	if (!r || r->y + r->h > 520) {
		ui.scroll[SCREEN_SETTINGS] += r ? r->y + r->h - 400 : 120;
		ui.redraw = 1;
		ui_settle(ui_frame());
	}
	ui_tap_on("Dark mode", 0);
	CHECK(ui_state.dark_mode == 0, "tapping Dark mode turns it off");
	for (unsigned t = CP_THEME_COUNT; t-- > 0;) {
		r = ui_find(cp_theme_names[t], 0);
		if (!r || r->y < ui.bar_h || r->y + r->h > 520) {
			ui.scroll[SCREEN_SETTINGS] += r ? r->y - 200 : -120;
			ui.redraw = 1;
			ui_settle(ui_frame());
		}
		ui_tap_on(cp_theme_names[t], 0);
		CHECK(ui.t.primary == cp_themes_light[t].primary &&
			user_pixel(CP_VIEW_PORTRAIT_43, 0, 2, 200) ==
			cp_themes_light[t].surface,
			"page did not take light scheme %s", cp_theme_names[t]);
	}
	ui_shot(dir, "ui-settings-appearance-light");
	r = ui_find("Dark mode", 0);
	if (!r || r->y + r->h > 520) {
		ui.scroll[SCREEN_SETTINGS] += r ? r->y + r->h - 400 : 120;
		ui.redraw = 1;
		ui_settle(ui_frame());
	}
	ui_tap_on("Dark mode", 0);
	CHECK(ui_state.dark_mode == 1 && ui.t.surface == cp_themes[0].surface,
		"tapping Dark mode restores dark scheme");

	/* Motion: a new screen starts faded and slid, and comes to rest. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	touch_user(CP_VIEW_PORTRAIT_43, 0, 1, ui_find("Settings", 0)->x + 40,
		ui_find("Settings", 0)->y + 20, 0, 0);
	ui_frame();
	memset(&touch_state, 0, sizeof(touch_state));
	ui_frame();
	CHECK(ui.screen == SCREEN_SETTINGS && ui.entering, "no transition began");
	ui_frame();
	CHECK(ui.animating && ui.slide > 0 && ui.t.on_surface !=
		cp_themes[0].on_surface, "screen is not fading and sliding in "
		"(slide %d)", ui.slide);
	before = ui.t.on_surface;
	ui_frame();
	CHECK(ui.t.on_surface != before, "transition does not advance");
	ui_settle(FXCG100_MENU_CONTINUE);
	CHECK(!ui.animating && !ui.redraw && ui.slide == 0 &&
		ui.t.on_surface == cp_themes[0].on_surface,
		"transition did not come to rest");
	CHECK(cp_ui_step(&ui) == FXCG100_MENU_CONTINUE && !ui.redraw,
		"an idle screen keeps redrawing");

	/* A tick count that stands still must not leave the menu stuck in its
	 * fade from black: every step still moves it along. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	cp_ui_begin(&ui, &ui_state, 1);
	{
		int steps = 0;

		while (steps < 200 && (cp_ui_step(&ui), ui.redraw))
			steps++;
		CHECK(steps > 2 && steps < 60 && !ui.entering &&
			ui.t.on_surface == cp_themes[0].on_surface &&
			user_pixel(CP_VIEW_PORTRAIT_43, 0, 2, 200) ==
			cp_themes[0].surface,
			"menu did not finish appearing without a clock (%d steps)", steps);
	}

	/* Animations off: everything lands at once. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_state.animations = 0;
	touch_user(CP_VIEW_PORTRAIT_43, 0, 1, ui_find("Settings", 0)->x + 40,
		ui_find("Settings", 0)->y + 20, 0, 0);
	ui_frame();
	memset(&touch_state, 0, sizeof(touch_state));
	ui_frame();
	ui_frame();
	CHECK(ui.screen == SCREEN_SETTINGS && !ui.animating && ui.slide == 0 &&
		ui.t.on_surface == cp_themes[0].on_surface,
		"animations off still animates");
	ui_state.animations = 1;

	/* Key mapping: tap a binding, press a key. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_goto(&ui, SCREEN_CONTROLS);
	ui_settle(ui_frame());
	ui_shot(dir, "ui-keys-portrait");
	ui_tap_on("A", 0);
	CHECK(ui.capture == 4, "capture for A (%d)", ui.capture);
	ui_frame();
	ui_shot(dir, "ui-key-capture");
	key_held[KEY_7] = 1;
	ui_frame();
	key_held[KEY_7] = 0;
	ui_frame();
	CHECK(ui.capture < 0 && ui_state.keymap[FXCG100_GBA_KEY_A] == KEY_7,
		"A was not rebound");
	/* A key that is already taken moves: the new function gets it, the
	 * old one is left without a key, and the player is told. */
	ui_settle(FXCG100_MENU_CONTINUE);
	ui_tap_on("B", 0);
	ui_frame();
	key_held[KEY_7] = 1;
	ui_frame();
	key_held[KEY_7] = 0;
	ui_frame();
	CHECK(ui.capture < 0 && ui_state.keymap[FXCG100_GBA_KEY_B] == KEY_7 &&
		ui_state.keymap[FXCG100_GBA_KEY_A] == 0 && ui.toast != NULL &&
		strstr(ui.toast, "A now has no key") != NULL,
		"a key in use did not move (%s)", ui.toast ? ui.toast : "-");
	CHECK(fxcg100_input_maps_valid(ui_state.keymap, ui_state.hotkey_map),
		"a map with an unassigned button is not valid");
	ui_shot(dir, "ui-keys-moved");
	/* Clear can be mapped like any other key, and then no longer opens the
	 * menu; the Menu icon still does. */
	ui_settle(FXCG100_MENU_CONTINUE);
	ui.scroll[SCREEN_CONTROLS] = 120;
	ui.redraw = 1;
	ui_settle(ui_frame());
	ui_tap_on("R", 0);
	ui_frame();
	key_held[KEY_CLEAR] = 1;
	ui_frame();
	key_held[KEY_CLEAR] = 0;
	ui_frame();
	CHECK(ui.capture < 0 && ui_state.keymap[FXCG100_GBA_KEY_R] == KEY_CLEAR,
		"Clear was not accepted for R");
	fxcg100_lcd_set_menu_key(0);
	key_held[KEY_CLEAR] = 1;
	CHECK(!(fxcg100_poll_app_keys() & FXCG100_APPKEY_ON),
		"a mapped Clear still opens the menu");
	key_held[KEY_CLEAR] = 0;
	touch_state.touches = 1;
	touch_state.x1 = 69;
	touch_state.y1 = 550;
	fxcg100_poll_app_keys();
	CHECK(fxcg100_poll_app_keys() & FXCG100_APPKEY_ON,
		"the Menu icon no longer opens the menu");
	release_touch();
	fxcg100_lcd_set_menu_key(1);
	/* A GBA button can be given no key on purpose as well. */
	ui_tap_on("L", 0);
	ui_frame();
	ui_tap_on("No key", 0);
	CHECK(ui.capture < 0 && ui_state.keymap[FXCG100_GBA_KEY_L] == 0,
		"No key did not clear L");
	/* A shortcut can be left without a key. */
	ui_settle(FXCG100_MENU_CONTINUE);
	ui_scroll_to_end();
	ui_tap_on("Show frame rate", 0);
	CHECK(ui.capture == 13, "capture for a shortcut (%d)", ui.capture);
	ui_frame();
	ui_tap_on("No key", 0);
	CHECK(ui.capture < 0 &&
		ui_state.hotkey_map[FXCG100_HOTKEY_DISPLAY_FPS] == 0,
		"No key did not clear the shortcut");
	/* Save & exit is a distinct shortcut binding at index 15. */
	ui_settle(FXCG100_MENU_CONTINUE);
	ui_tap_on("Save & exit", 0);
	CHECK(ui.capture == 15, "capture for Save & exit (%d)", ui.capture);
	ui_frame();
	key_held[KEY_9] = 1;
	ui_frame();
	key_held[KEY_9] = 0;
	ui_frame();
	CHECK(ui.capture < 0 &&
		ui_state.hotkey_map[FXCG100_HOTKEY_SAVE_EXIT] == KEY_9,
		"Save & exit was not bound");
	ui_shot(dir, "ui-keys-shortcuts");
	/* Fast forward mode & speed settings in Settings -> Speed. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_goto(&ui, SCREEN_SETTINGS);
	ui_scroll_to_end();
	ui_tap_on("Toggle", 0);
	CHECK(ui_state.fast_forward_mode == FXCG100_FF_MODE_TOGGLE,
		"ClassPad FF mode Toggle");
	ui_tap_on("2x", 0);
	CHECK(ui_state.fast_forward_speed == FXCG100_FF_SPEED_2X,
		"ClassPad FF speed 2x");
	ui_shot(dir, "ui-settings-ff");
	ui_tap_on("Hold", 0);
	ui_tap_on("Max", 0);
	CHECK(ui_state.fast_forward_mode == FXCG100_FF_MODE_HOLD &&
		ui_state.fast_forward_speed == FXCG100_FF_SPEED_UNLIMITED,
		"ClassPad FF reset to Hold / Max");

	/* Icon strip: Rotate turns the interface from any screen. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	memset(&touch_state, 0, sizeof(touch_state));
	touch_state.touches = 1;
	touch_state.x1 = 251;
	touch_state.y1 = 550;
	ui_frame();
	ui_frame();
	memset(&touch_state, 0, sizeof(touch_state));
	ui_frame();
	CHECK(ui_state.screen_scale == CP_VIEW_LANDSCAPE_2X &&
		ui_state.rotation == CP_ROTATION_KEYS_LEFT, "Rotate icon");
	/* Portrait -> CW -> 180 -> CCW -> Portrait. */
	{
		uint32_t view = ui_state.screen_scale, rot = ui_state.rotation;

		cp_rotate_next(&view, &rot);
		CHECK(view == CP_VIEW_PORTRAIT_43 && rot == CP_ROTATION_KEYS_LEFT,
			"second Rotate (180)");
		cp_rotate_next(&view, &rot);
		CHECK(view == CP_VIEW_LANDSCAPE_2X && rot == CP_ROTATION_KEYS_RIGHT,
			"third Rotate (CCW)");
		cp_rotate_next(&view, &rot);
		CHECK(view == CP_VIEW_PORTRAIT_43 && rot == CP_ROTATION_KEYS_RIGHT,
			"fourth Rotate (Portrait)");
	}

	/* The status card. */
	ui_state.screen_scale = CP_VIEW_PORTRAIT_43;
	cp_ui_status(&ui_state, "Loading game",
		"The Legend of Zelda The Minish Cap.gba");
	write_ppm(dir, "ui-status", CP_VIEW_PORTRAIT_43, 0);
}

/* A pressed key must be told apart from an idle one without shouting: in
 * every scheme its face is a step up from the idle face and well short of
 * the primary colour. */
static int luma(uint16_t c)
{
	return ((c >> 11) & 31) * 255 / 31 * 3 + ((c >> 5) & 63) * 255 / 63 * 6 +
		(c & 31) * 255 / 31;
}

static void test_key_colours(void)
{
	for (unsigned t = 0; t < CP_THEME_COUNT; t++) {
		cp_key_colors k = cp_theme_key_colors(&cp_themes[t]);
		int idle = luma(k.face), lit = luma(k.face_lit);
		int primary = luma(cp_themes[t].primary);

		CHECK(lit - idle >= 100, "scheme %s: pressed key too close to idle "
			"(%d vs %d)", cp_theme_names[t], lit, idle);
		CHECK(lit * 2 < primary + idle + (primary - idle) / 8,
			"scheme %s: pressed key too bright (%d, primary %d)",
			cp_theme_names[t], lit, primary);
		CHECK(luma(k.ink_lit) - lit >= 500,
			"scheme %s: label unreadable on a pressed key",
			cp_theme_names[t]);
	}
}

/* ---- clock ---------------------------------------------------------------- */

/* The stock state the benchmark reported on a ClassPad II: 118 / 59 / 29.5 MHz,
 * here as FLL 900, PLL x16, CPU /4, SuperHyway and bus /8, peripherals /16. */
static cp_clock stock_clock(void)
{
	cp_clock c;

	memset(&c, 0, sizeof(c));
	c.FLLFRQ = 900;
	c.FRQCR = (15u << 24) | (1u << 20) | (2u << 12) | (2u << 8) | 3u;
	c.CS0WCR = 4u << 7;
	c.CS3WCR = (1u << 7) | 1u;
	c.CS5aWCR = 7u << 7;
	return c;
}

static void test_clock_model(void)
{
	cp_clock stock = stock_clock(), x2, fast, step;
	cp_chain ch;
	cp_clock_file file;
	uint32_t low, high;
	int result;

	CHECK(cp_clock_hz(&stock, CP_CLK_CPU) == 117964800u &&
		cp_clock_hz(&stock, CP_CLK_BUS) == 58982400u &&
		cp_clock_hz(&stock, CP_CLK_PERIPH) == 29491200u,
		"stock frequencies: %u %u %u", cp_clock_hz(&stock, CP_CLK_CPU),
		cp_clock_hz(&stock, CP_CLK_BUS), cp_clock_hz(&stock, CP_CLK_PERIPH));
	CHECK(cp_clock_problem(&stock, cp_clock_limit) == NULL, "stock flagged: %s",
		cp_clock_problem(&stock, cp_clock_limit));

	/* Putting the stock chain back in gives the stock setting exactly. */
	cp_chain_decode(&stock, &ch);
	fast = cp_clock_for_chain(&stock, &ch);
	CHECK(cp_clock_equal(&fast, &stock), "stock chain does not round-trip");

	/* CPU x2 is one step on the CPU divider. The ratio table drags the
	 * SuperHyway clock along; the bus and peripheral clocks, and with them
	 * every memory timing, stay put. */
	CHECK(cp_clock_cpu_x2(&stock, &x2), "CPU x2 not derivable");
	CHECK(cp_clock_hz(&x2, CP_CLK_CPU) == 235929600u &&
		cp_clock_hz(&x2, CP_CLK_SH) == 117964800u &&
		cp_clock_hz(&x2, CP_CLK_BUS) == 58982400u &&
		cp_clock_hz(&x2, CP_CLK_PERIPH) == 29491200u,
		"CPU x2 frequencies wrong");
	CHECK(x2.CS0WCR == stock.CS0WCR && x2.CS3WCR == stock.CS3WCR &&
		x2.CS5aWCR == stock.CS5aWCR && x2.FLLFRQ == stock.FLLFRQ,
		"CPU x2 touched bus timings");
	/* That divider is then at its end. */
	cp_chain_decode(&x2, &ch);
	CHECK(cp_chain_step(&ch, CP_CLK_CPU, 1, 0, cp_clock_limit) == -1, "divider below /2");

	/* A faster bus: the SuperHyway divider, which was equal, follows; the
	 * flash wait and the SDRAM TRC are set for the new bus clock. */
	cp_chain_decode(&stock, &ch);
	CHECK(cp_chain_step(&ch, CP_CLK_BUS, 1, 0, cp_clock_limit) == 0 && ch.div[0] == 4 &&
		ch.div[1] == 4 && ch.div[2] == 4 && ch.div[3] == 16,
		"bus step: /%u /%u /%u /%u", ch.div[0], ch.div[1], ch.div[2],
		ch.div[3]);
	fast = cp_clock_for_chain(&stock, &ch);
	CHECK(cp_clock_hz(&fast, CP_CLK_BUS) == 117964800u &&
		cp_clock_problem(&fast, cp_clock_limit) == NULL, "118 MHz bus refused");
	CHECK(cp_clock_wait_cycles(cp_clock_flash_wait(&fast)) == 12 &&
		cp_clock_trc_cycles(cp_clock_trc(&fast)) == 6,
		"automatic waits at 118 MHz: %u cycles, TRC %u",
		cp_clock_wait_cycles(cp_clock_flash_wait(&fast)),
		cp_clock_trc_cycles(cp_clock_trc(&fast)));
	/* A slightly faster bus (FLL 900 -> 960: 62.9 MHz). The tables alone
	 * would give fewer SDRAM cycles than the calculator uses at its slower
	 * stock clock; it must not get fewer than stock has. */
	{
		cp_chain mild;
		cp_clock m;
		int k;

		cp_chain_decode(&stock, &mild);
		for (k = 0; k < 60; k++)
			cp_chain_step(&mild, CP_CLK_FLL, 1, 0, cp_clock_limit);
		m = cp_clock_for_chain(&stock, &mild);
		CHECK(cp_clock_hz(&m, CP_CLK_BUS) == 62914560u, "bus at FLL 960: %u",
			cp_clock_hz(&m, CP_CLK_BUS));
		CHECK(cp_clock_best_trc(62914560u) < cp_clock_trc(&stock),
			"this test needs a stock TRC above the table's");
		CHECK(cp_clock_trc(&m) == cp_clock_trc(&stock) &&
			cp_clock_flash_wait(&m) >= cp_clock_flash_wait(&stock),
			"a faster bus got shorter waits than stock: TRC %u, wait %u",
			cp_clock_trc(&m), cp_clock_flash_wait(&m));
	}
	/* One more would be 236 MHz: over the bus limit, refused, nothing moved. */
	result = cp_chain_step(&ch, CP_CLK_BUS, 1, 0, cp_clock_limit);
	CHECK(result == CP_CLK_SH || result == CP_CLK_BUS,
		"236 MHz bus accepted (%d)", result);
	CHECK(ch.div[2] == 4, "a refused step changed the chain");

	/* The PLL stops at its limit. */
	cp_chain_decode(&stock, &ch);
	while ((result = cp_chain_step(&ch, CP_CLK_PLL, 1, 0, cp_clock_limit)) == 0)
		;
	CHECK(result == CP_CLK_PLL && ch.pll == 27, "PLL stopped at x%u by %d",
		ch.pll, result);
	/* A hand-made setting over a limit is named. */
	fast = stock;
	fast.FRQCR = (fast.FRQCR & ~(63u << 24)) | (63u << 24);
	CHECK(cp_clock_problem(&fast, cp_clock_limit) != NULL, "943 MHz PLL accepted");

	/* A change goes: slowest waits, then the clock, then its own waits. */
	cp_chain_decode(&stock, &ch);
	cp_chain_step(&ch, CP_CLK_BUS, 1, 0, cp_clock_limit);
	fast = cp_clock_for_chain(&stock, &ch);
	step = cp_clock_apply_step(&stock, &fast, 0);
	CHECK(step.FRQCR == stock.FRQCR && cp_clock_flash_wait(&step) == 11 &&
		cp_clock_trc(&step) == 3, "step 0 must only slow the waits");
	step = cp_clock_apply_step(&stock, &fast, 1);
	CHECK(step.FRQCR == fast.FRQCR && cp_clock_flash_wait(&step) == 11 &&
		cp_clock_trc(&step) == 3, "step 1 must change the clock on slow waits");
	step = cp_clock_apply_step(&stock, &fast, 2);
	CHECK(cp_clock_equal(&step, &fast), "step 2 must be the setting itself");

	/* Unlocked: the same step goes through up to the hard limits, and no
	 * further. From a 118 MHz bus, the next step is 236 MHz: over even those. */
	cp_chain_decode(&stock, &ch);
	while ((result = cp_chain_step(&ch, CP_CLK_PLL, 1, 0,
			cp_clock_hard_limit)) == 0)
		;
	CHECK(result == CP_CLK_PLL && ch.pll == 33, "unlocked PLL stopped at "
		"x%u by %d", ch.pll, result);
	fast = cp_clock_for_chain(&stock, &ch);
	CHECK(cp_clock_problem(&fast, cp_clock_limit) != NULL &&
		cp_clock_problem(&fast, cp_clock_hard_limit) == NULL,
		"a setting between the two sets of limits");

	/* The touch bus keeps to its 393 kHz when the peripheral clock rises,
	 * and is never clocked with fewer counts than stock. */
	cp_clock_i2c_counts(29491200u, &low, &high);
	CHECK(low == 0x29 && high == 0x22, "stock I2C counts %u %u", low, high);
	cp_clock_i2c_counts(50000000u, &low, &high);
	CHECK(50000000u / (low + high) <= CP_CLK_I2C_HZ &&
		50000000u / (low + high) > 380000u && low > high,
		"I2C at 50 MHz: %u + %u", low, high);
	cp_clock_i2c_counts(14745600u, &low, &high);
	CHECK(low == 0x29 && high == 0x22, "I2C counts below stock");

	/* The saved file is checked before use. */
	memset(&file, 0, sizeof(file));
	file.magic = CP_CLOCK_FILE_MAGIC;
	file.at_startup = 1;
	file.clock = x2;
	file.check = cp_clock_file_sum(&file);
	CHECK(cp_clock_file_valid(&file), "good file rejected");
	file.clock.FRQCR ^= 1u << 24;
	CHECK(!cp_clock_file_valid(&file), "corrupted file accepted");
}

/* Tap a button of the clock page: Test and Apply are at the top of the page,
 * the presets at its end. */
static fxcg100_menu_result clock_tap(const char *name, int at_end)
{
	if (at_end) {
		ui_scroll_to_end();
	} else {
		ui.scroll[SCREEN_CLOCK] = 0;
		ui.redraw = 1;
		ui_settle(ui_frame());
	}
	return ui_tap_on(name, 0);
}

static void test_clock_page(const char *dir)
{
	cp_clock stock = stock_clock(), x2;
	int i;

	cp_clock_cpu_x2(&stock, &x2);
	/* As the page makes it: with the fast idle cycles. */
	x2.CS3BCR = cp_clock_fast_sdram_idle(&stock);
	x2.CS0BCR = cp_clock_fast_flash_idle(&stock);
	hw_clock = stock;
	fake_blob_size = 0;
	clock_stock_known = 0;

	ui_open(1, CP_VIEW_PORTRAIT_43);
	ui_goto(&ui, SCREEN_CLOCK);
	ui_settle(ui_frame());
	CHECK(cp_clock_equal(&ui.clock_edit, &stock), "page did not read the clock");
	/* The CPU divider's "/2" is the same thing as the preset. */
	ui_tap_on("/2", 0);
	CHECK(cp_clock_equal(&ui.clock_edit, &x2), "CPU divider /2");
	clock_tap("Restore stock", 1);
	CHECK(cp_clock_equal(&ui.clock_edit, &stock), "Stock preset");
	ui_shot(dir, "ui-clock-end");
	ui.scroll[SCREEN_CLOCK] = 0;
	ui.redraw = 1;
	ui_settle(ui_frame());
	/* A tap on a multiplier's value picks it for the cursor keys: one a
	 * step left and right, ten up and down, until EXE. */
	ui_tap_on("FLL multiplier", 0);
	CHECK(ui.edit != NULL, "FLL value not picked");
	ui_key(KEY_RIGHT);
	CHECK(ui.clock_chain.fll == 901, "FLL +1 gives %u", ui.clock_chain.fll);
	ui_shot(dir, "ui-clock-edit");
	ui_key(KEY_UP);
	CHECK(ui.clock_chain.fll == 911, "FLL +10 gives %u", ui.clock_chain.fll);
	ui_key(KEY_LEFT);
	ui_key(KEY_DOWN);
	CHECK(ui.clock_chain.fll == 900 && ui.screen == SCREEN_CLOCK,
		"FLL back gives %u", ui.clock_chain.fll);
	ui_key(KEY_EXE);
	CHECK(ui.edit == NULL && ui.screen == SCREEN_CLOCK, "EXE did not end it");
	ui_tap_on("PLL multiplier", 0);
	ui_key(KEY_RIGHT);
	CHECK(ui.clock_chain.pll == 17, "PLL +1 gives x%u", ui.clock_chain.pll);
	ui_key(KEY_UP);                             /* one for the PLL, not ten */
	CHECK(ui.clock_chain.pll == 18, "PLL up gives x%u", ui.clock_chain.pll);
	ui_key(KEY_DOWN);
	ui_key(KEY_LEFT);
	ui_key(KEY_DEL);                            /* back ends it, stays here */
	{
		/* Back at the stock chain, but made by hand: fast idle cycles. */
		cp_clock made = stock;

		made.CS3BCR = cp_clock_fast_sdram_idle(&stock);
		made.CS0BCR = cp_clock_fast_flash_idle(&stock);
		CHECK(ui.edit == NULL && ui.screen == SCREEN_CLOCK &&
			cp_clock_equal(&ui.clock_edit, &made), "back left the page");
	}
	ui_tap_on("/2", 0);
	CHECK(cp_clock_equal(&ui.clock_edit, &x2), "CPU divider /2 again");
	ui_shot(dir, "ui-clock-portrait");
	/* A step past a limit changes nothing and says why. */
	ui.scroll[SCREEN_CLOCK] = 200;
	ui.redraw = 1;
	ui_settle(ui_frame());
	ui_tap_on("/2", 1);
	CHECK(cp_clock_equal(&ui.clock_edit, &x2) && ui.toast != NULL,
		"236 MHz SuperHyway clock accepted");

	/* Test and Apply are at the end of the page. */
	ui_scroll_to_end();
	hw_clock_writes = 0;
	clock_tap("Test", 0);
	CHECK(cp_clock_equal(&hw_clock, &stock) && hw_clock_writes >= 2,
		"Test must apply and return (%d writes)", hw_clock_writes);
	/* The result stays on the page, and its message gets its full time
	 * although the test itself took a while. */
	CHECK(ui.clock_tested != NULL && !ui.clock_test_failed &&
		ui.toast != NULL, "no test result on the page");
	fake_ticks += 128;
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.toast != NULL, "the test message went at once");
	ui_shot(dir, "ui-clock-tested");
	/* Also when it is the setting in use that was tested. */
	clock_tap("Restore stock", 1);
	clock_tap("Test", 0);
	CHECK(ui.clock_tested != NULL && cp_clock_equal(&ui.clock_edit,
		&ui.clock_tested_on) && cp_clock_equal(&ui.clock_edit,
		&ui.clock_running), "no result for the setting in use");
	ui_shot(dir, "ui-clock-tested-running");
	ui.scroll[SCREEN_CLOCK] = 120;
	ui.redraw = 1;
	ui_settle(ui_frame());
	ui_tap_on("/2", 0);
	CHECK(cp_clock_equal(&ui.clock_edit, &x2), "CPU divider /2 once more");

	/* Apply, then do nothing: after ten seconds the old setting is back. */
	clock_tap("Apply", 0);
	CHECK(ui.clock_pending && cp_clock_equal(&hw_clock, &x2),
		"Apply did not switch the clock");
	ui_shot(dir, "ui-clock-confirm");
	fake_ticks = ui.clock_deadline - 3 * 128;
	cp_ui_step(&ui);
	CHECK(ui.clock_pending, "reverted too early");
	fake_ticks = ui.clock_deadline + 1;
	cp_ui_step(&ui);
	CHECK(!ui.clock_pending && cp_clock_equal(&hw_clock, &stock),
		"clock was not restored after the countdown");
	CHECK(fake_blob_size == 0, "an unconfirmed setting was saved");

	/* Apply and keep. */
	ui_settle(FXCG100_MENU_CONTINUE);
	clock_tap("Apply", 0);
	CHECK(ui.clock_pending, "second Apply");
	ui_tap_on("Keep", 0);
	CHECK(!ui.clock_pending && cp_clock_equal(&hw_clock, &x2) &&
		cp_clock_equal(&ui.clock_running, &x2), "Keep did not keep");
	CHECK(fake_blob_size == sizeof(cp_clock_file), "kept setting not saved");
	/* The kept setting is measured, and the page says what it found. */
	CHECK(clock_measured_hz > 235000000u && clock_measured_hz < 237000000u,
		"measured %u Hz on CPU x2", clock_measured_hz);
	/* The message about it goes away by itself. */
	CHECK(ui.toast != NULL, "no message after Keep");
	fake_ticks += 3 * 128;
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.toast == NULL && !ui.redraw, "the message stayed up");

	/* The limits are always in force: the bus cannot be taken past 134 MHz. */
	clock_tap("Restore stock", 1);
	ui.scroll[SCREEN_CLOCK] = 330;
	ui.redraw = 1;
	ui_settle(ui_frame());
	ui_tap_on("/4", 2);                         /* bus 118 MHz */
	CHECK(cp_clock_hz(&ui.clock_edit, CP_CLK_BUS) == 117964800u,
		"bus /4: %u Hz", cp_clock_hz(&ui.clock_edit, CP_CLK_BUS));
	ui_tap_on("/2", 2);
	CHECK(cp_clock_hz(&ui.clock_edit, CP_CLK_BUS) == 117964800u,
		"236 MHz bus accepted");
	/* What is set by hand has the fast idle cycles; stock is stock. */
	CHECK(ui.clock_edit.CS3BCR == cp_clock_fast_sdram_idle(&stock) &&
		ui.clock_edit.CS0BCR == cp_clock_fast_flash_idle(&stock),
		"a setting made by hand lacks the fast idle cycles");
	CHECK(ui_find("Limits", 0) == NULL && ui_find("Fast SDRAM bus", 0) == NULL,
		"the Limits or the bus switch is still there");
	clock_tap("Restore stock", 1);
	CHECK(cp_clock_equal(&ui.clock_edit, &stock), "Restore stock is not stock");
	ui_scroll_to_end();

	/* "Apply when gpSP starts" is on unless it was switched off. */
	CHECK(ui.clock_at_startup == 1, "start-up switch is not on by default");

	/* Next launch: applied on top of stock, not otherwise. */
	for (i = 0; i < 3; i++) {
		hw_clock = stock;
		clock_stock_known = 0;
		memset(key_held, 0, sizeof(key_held));
		if (i == 1)
			key_held[KEY_CLEAR] = 1;                     /* held at start */
		if (i == 2)
			hw_clock.FRQCR ^= 1u << 8;                   /* not stock */
		cp_ui_apply_saved_clock();
		CHECK(cp_clock_equal(&hw_clock, &x2) == (i == 0),
			"start-up apply, case %d", i);
	}
	/* Nothing saved: the default speed, Ptune 4's F5, unless Clear is held. */
	fake_blob_size = 0;
	for (i = 0; i < 2; i++) {
		cp_clock f5;

		hw_clock = stock;
		clock_stock_known = 0;
		memset(key_held, 0, sizeof(key_held));
		key_held[KEY_CLEAR] = i == 1;
		cp_ui_apply_saved_clock();
		CHECK(cp_clock_f5(&stock, &f5), "F5 is not possible from stock");
		CHECK(cp_clock_equal(&hw_clock, &f5) == (i == 0),
			"default speed at start, case %d", i);
		CHECK(cp_clock_hz(&f5, CP_CLK_CPU) == 235929600u &&
			cp_clock_hz(&f5, CP_CLK_BUS) == 117964800u &&
			cp_clock_hz(&f5, CP_CLK_PERIPH) == 29491200u &&
			f5.CS3BCR == cp_clock_fast_sdram_idle(&stock) &&
			f5.CS0BCR == cp_clock_fast_flash_idle(&stock),
			"F5 is %u/%u/%u Hz", cp_clock_hz(&f5, CP_CLK_CPU),
			cp_clock_hz(&f5, CP_CLK_BUS), cp_clock_hz(&f5, CP_CLK_PERIPH));
	}
	memset(key_held, 0, sizeof(key_held));
	hw_clock = stock;
	clock_stock_known = 0;
}

/* Strips must go bottom-up when the panel refreshes bottom-up (0x16/0x52)
 * and top-down when it refreshes top-down (R61523), producing identical
 * pixels on the panel either way. */
static void test_refresh_direction(void)
{
	static uint16_t snap[CP_PANEL_H][CP_PANEL_W];

	/* Menu/calibration frame presenter: 33 contiguous 16-row bands. */
	for (int i = 0; i < DWIDTH * DHEIGHT; i++)
		vram[i] = (uint16_t)((i * 2654435761u) >> 16);
	cp_host_refresh_bottom_up = 1;
	win_log_n = 0;
	win_log_active = 1;
	fxcg100_lcd_present_frame();
	win_log_active = 0;
	CHECK(win_log_n == CP_PANEL_H / 16, "present_frame bottom-up bands: %d",
		win_log_n);
	for (int i = 1; i < win_log_n; i++)
		CHECK(win_log_r1[i] + 1 == win_log_r0[i - 1],
			"present_frame bottom-up band %d: %d..%d after %d..%d",
			i, win_log_r0[i], win_log_r1[i],
			win_log_r0[i - 1], win_log_r1[i - 1]);
	memcpy(snap, panel, sizeof(panel));

	memset(panel, 0, sizeof(panel));
	cp_host_refresh_bottom_up = 0;
	win_log_n = 0;
	win_log_active = 1;
	fxcg100_lcd_present_frame();
	win_log_active = 0;
	CHECK(win_log_n == CP_PANEL_H / 16, "present_frame top-down bands: %d",
		win_log_n);
	for (int i = 1; i < win_log_n; i++)
		CHECK(win_log_r0[i] == win_log_r1[i - 1] + 1,
			"present_frame top-down band %d: %d..%d after %d..%d",
			i, win_log_r0[i], win_log_r1[i],
			win_log_r0[i - 1], win_log_r1[i - 1]);
	CHECK(memcmp(snap, panel, sizeof(panel)) == 0,
		"present_frame pixels differed between bottom-up and top-down");

	/* Every gameplay view and rotation. */
	fill_test_frame();
	for (unsigned view = 0; view < CP_VIEW_COUNT; view++) {
		for (unsigned rot = 0; rot < CP_ROTATION_COUNT; rot++) {
			set_mode(view, rot, 0);
			fxcg100_lcd_set_transfer(0);
			cp_host_refresh_bottom_up = 1;
			fxcg100_lcd_blit_gba(gba);   /* paints background + keys once */
			win_log_n = 0;
			win_log_active = 1;
			fxcg100_lcd_blit_gba(gba);
			win_log_active = 0;
			CHECK(win_log_n > 1, "view %u rot %u bottom-up strips: %d",
				view, rot, win_log_n);
			for (int i = 1; i < win_log_n; i++)
				CHECK(win_log_r1[i] + 1 == win_log_r0[i - 1],
					"view %u rot %u bottom-up strip %d: %d..%d after %d..%d",
					view, rot, i, win_log_r0[i], win_log_r1[i],
					win_log_r0[i - 1], win_log_r1[i - 1]);
			memcpy(snap, panel, sizeof(panel));

			cp_host_refresh_bottom_up = 0;
			fxcg100_lcd_update();        /* force fresh game screen */
			fxcg100_lcd_blit_gba(gba);
			win_log_n = 0;
			win_log_active = 1;
			fxcg100_lcd_blit_gba(gba);
			win_log_active = 0;
			CHECK(win_log_n > 1, "view %u rot %u top-down strips: %d",
				view, rot, win_log_n);
			for (int i = 1; i < win_log_n; i++)
				CHECK(win_log_r0[i] == win_log_r1[i - 1] + 1,
					"view %u rot %u top-down strip %d: %d..%d after %d..%d",
					view, rot, i, win_log_r0[i], win_log_r1[i],
					win_log_r0[i - 1], win_log_r1[i - 1]);
			CHECK(memcmp(snap, panel, sizeof(panel)) == 0,
				"view %u rot %u pixels differed between refresh directions",
				view, rot);
		}
	}
	cp_host_refresh_bottom_up = 1;
}

static void test_txt_reader_ui(const char *dir)
{
	txt_reader *r;
	const cp_rect *btn;

	fake_txt_blob_size = 0;

	/* Portrait pause menu: TXT Reader, Settings, Games, Restart, Exit fit cleanly. */
	ui_open(1, CP_VIEW_PORTRAIT_43);
	CHECK(ui.screen == SCREEN_PAUSE, "pause screen");
	btn = ui_find("TXT Reader", 0);
	CHECK(btn != NULL && btn->y + btn->h <= 528, "portrait TXT Reader button fits");
	btn = ui_find("Exit gpSP", 0);
	CHECK(btn != NULL && btn->y + btn->h <= 528, "portrait Exit gpSP fits");

	/* Tap TXT Reader -> opens file browser -> tap GUIDES/ -> tap HOENN.TXT. */
	ui_tap_on("TXT Reader", 0);
	CHECK(ui.screen == SCREEN_TXT_BROWSER, "opened TXT browser");
	ui_shot(dir, "ui-txt-browser-portrait");
	ui_tap_on("GUIDES/", 0);
	r = txt_reader_global();
	CHECK(r->dir_depth == 1, "entered GUIDES/");
	ui_tap_on("HOENN.TXT", 0);
	CHECK(ui.screen == SCREEN_TXT_READER && r->has_doc, "opened HOENN.TXT");
	CHECK(r->show_hints == 1, "bottom hint bar shown on entry");
	ui_shot(dir, "ui-txt-reader-portrait");

	/* Tap right side pages down (or hides hints on scroll/page). */
	ui_tap(ui.w - 20, ui.h / 2);
	CHECK(r->show_hints == 0, "tap-page auto-hid bottom hint bar");

	/* Tap center opens floating overlay; toggle font, wrap, add bookmark, close. */
	ui_tap(ui.w / 2, ui.h / 2);
	CHECK(ui.txt_overlay == 1, "center tap opened reader overlay");
	ui_shot(dir, "ui-txt-overlay-portrait");
	ui_tap_on("8x14", 0);
	CHECK(r->font_idx == TXT_FONT_8X14, "switched to 8x14");
	ui_tap_on("No wrap", 0);
	CHECK(r->wrap == 0, "switched to No wrap");
	ui_tap_on("+ Mark", 0);
	CHECK(txt_reader_bookmark_count(r) == 1, "added bookmark from overlay");
	ui_tap_on("Bookmarks (1)", 0);
	CHECK(ui.txt_bm_open == 1, "opened dynamic bookmarks modal");
	ui_tap_on("Close", 0);
	CHECK(ui.txt_bm_open == 0, "closed bookmarks modal");
	ui_tap_on("Close", 0);
	CHECK(ui.txt_overlay == 0, "closed overlay");

	/* Landscape pause menu & reader fit inside 528x320. */
	ui_open(1, CP_VIEW_LANDSCAPE_2X);
	btn = ui_find("TXT Reader", 0);
	CHECK(btn != NULL && btn->y + btn->h <= 320, "landscape TXT Reader fits");
	btn = ui_find("Exit gpSP", 0);
	CHECK(btn != NULL && btn->y + btn->h <= 320, "landscape Exit gpSP fits");

	/* Direct hotkey open: with no doc yet -> browser; once a doc is open -> reader. */
	ui_state.hotkey_map[FXCG100_HOTKEY_TXT_READER] = KEY_EQUALS;
	fxcg100_menu_open_reader();
	cp_ui_begin(&ui, &ui_state, 1);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.screen == SCREEN_TXT_BROWSER && ui.txt_direct == 1,
		"direct hotkey with no doc opened browser");
	ui_tap_on("README.TXT", 0);
	CHECK(ui.screen == SCREEN_TXT_READER && txt_reader_global()->has_doc,
		"opened README.TXT in landscape");
	ui_shot(dir, "ui-txt-reader-landscape");

	fxcg100_menu_open_reader();
	cp_ui_begin(&ui, &ui_state, 1);
	ui_settle(cp_ui_step(&ui));
	CHECK(ui.screen == SCREEN_TXT_READER && ui.txt_direct == 1,
		"direct hotkey opened reader");
	key_held[KEY_EQUALS] = 1;
	CHECK(ui_frame() == FXCG100_MENU_RETURN, "hotkey toggled back to game");
	key_held[KEY_EQUALS] = 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : NULL;
	static const char *const names[CP_VIEW_COUNT] = {
		"portrait-1x", "portrait-4x3", "landscape-1.5x", "landscape-2x",
		"landscape-1x"
	};

	{
		uint16_t keymap[FXCG100_GBA_KEY_COUNT];
		uint16_t hotkeys[FXCG100_HOTKEY_COUNT];

		fxcg100_keymap_defaults(keymap);
		fxcg100_hotkey_defaults(hotkeys);
		CHECK(fxcg100_input_maps_valid(keymap, hotkeys),
			"default ClassPad II key map is not valid");
		CHECK(fxcg100_key_bindable(FXCG100_PHYSKEY_ON),
			"Clear must be bindable on the ClassPad II");
	}

	fxcg100_lcd_init();
	for (unsigned view = 0; view < CP_VIEW_COUNT; view++)
		for (unsigned rot = 0; rot < CP_ROTATION_COUNT; rot++) {
			test_layout_geometry(view, rot);
			for (unsigned filter = 0; filter < 3; filter++) {
				test_picture(view, rot, filter, 0);
				test_picture(view, rot, filter, 1);
			}
			fxcg100_lcd_set_transfer(0);
			test_touch(view, rot);
		}
	test_flipped(CP_VIEW_PORTRAIT_1X);
	test_flipped(CP_VIEW_PORTRAIT_43);
	test_refresh_direction();
	test_arrow_remap();
	test_icon_strip();
	test_key_colours();
	hw_clock = stock_clock();
	test_ui(dir);
	test_txt_reader_ui(dir);
	test_clock_model();
	test_clock_page(dir);
	test_canvas(CP_ROTATION_KEYS_RIGHT, dir, "menu-canvas-keys-right");
	test_canvas(CP_ROTATION_KEYS_LEFT, dir, NULL);

	/* Pictures for a person: demo frame, with A and a D-pad direction held
	 * where the layout has them so the pressed look is visible. */
	if (dir) {
		for (unsigned view = 0; view < CP_VIEW_COUNT; view++) {
			cp_layout l = cp_layout_for(view, CP_ROTATION_KEYS_RIGHT);
			int n = 0, tx[2] = { 0, 0 }, ty[2] = { 0, 0 };

			fill_demo_frame();
			set_mode(view, CP_ROTATION_KEYS_RIGHT, 0);
			fxcg100_lcd_update();
			fxcg100_lcd_blit_gba(gba);
			for (unsigned i = 0; i < l.key_count && n < 2; i++) {
				cp_rect f = l.keys[i].frame;

				if (l.keys[i].shape == CP_KEY_DPAD) {
					tx[n] = f.x + f.w - 10;
					ty[n++] = f.y + f.h / 2;
				} else if (l.keys[i].mask == CP_GBA_A ||
						(view == CP_VIEW_LANDSCAPE_2X && i == 0)) {
					tx[n] = f.x + f.w / 2;
					ty[n++] = f.y + f.h / 2;
				}
			}
			touch_user(view, CP_ROTATION_KEYS_RIGHT, n, tx[0], ty[0], tx[1],
				ty[1]);
			fxcg100_poll_app_keys();
			fxcg100_poll_gba_buttons();
			write_ppm(dir, names[view], view, CP_ROTATION_KEYS_RIGHT);
			release_touch();
		}
		/* 2x the other way round: the strip changes sides. */
		fill_demo_frame();
		set_mode(CP_VIEW_LANDSCAPE_2X, CP_ROTATION_KEYS_LEFT, 0);
		fxcg100_lcd_update();
		fxcg100_lcd_blit_gba(gba);
		write_ppm(dir, "landscape-2x-cw", CP_VIEW_LANDSCAPE_2X,
			CP_ROTATION_KEYS_LEFT);
	}

	if (failures) {
		printf("classpad_render_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("classpad_render_test: all checks passed\n");
	return 0;
}
