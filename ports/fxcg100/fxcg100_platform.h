#ifndef CGBA_FXCG100_PLATFORM_H
#define CGBA_FXCG100_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

void fxcg100_debug_char(int c);
void fxcg100_debug_puts(const char *text);
void fxcg100_debug_hex32(uint32_t value);
void fxcg100_panic(const char *text);

#define FXCG100_APPKEY_SHIFT  (1u << 0)
#define FXCG100_APPKEY_HOME   (1u << 1)
#define FXCG100_APPKEY_AC     (1u << 2)
#define FXCG100_APPKEY_EXE    (1u << 3)
#define FXCG100_APPKEY_MENU   (1u << 4)
#define FXCG100_APPKEY_UP     (1u << 5)
#define FXCG100_APPKEY_DOWN   (1u << 6)
#define FXCG100_APPKEY_LEFT   (1u << 7)
#define FXCG100_APPKEY_RIGHT  (1u << 8)
#define FXCG100_APPKEY_BACK   (1u << 9)
#define FXCG100_APPKEY_ON     (1u << 10)
/* fx-CG interface only (cg_ui.c): the key that opens Settings (OPTN on the
 * fx-CG50, SETTINGS on the fx-CG100), DEL, and the digit keys. */
#define FXCG100_APPKEY_SETTINGS (1u << 11)
#define FXCG100_APPKEY_DEL      (1u << 12)
#define FXCG100_APPKEY_SORT     (1u << 13)
#define FXCG100_APPKEY_DIGIT(n) (1u << (16 + (n)))
#define FXCG100_APPKEY_DIGITS   (0x3ffu << 16)

#if defined(CGBA_CLASSPAD)
#define FXCG100_PHYSKEY_ON 0x74u   /* fx-CP400 KEY_CLEAR (Clear / power) */
#elif defined(CGBA_FXCG50)
#define FXCG100_PHYSKEY_ON 0x07u   /* fx-CG50 KEY_ACON (AC/ON) */
#else
#define FXCG100_PHYSKEY_ON 79u     /* fx-CG100 ON */
#endif

#define FXCG100_GBA_BUTTON_L      0x200u
#define FXCG100_GBA_BUTTON_R      0x100u
#define FXCG100_GBA_BUTTON_DOWN   0x080u
#define FXCG100_GBA_BUTTON_UP     0x040u
#define FXCG100_GBA_BUTTON_LEFT   0x020u
#define FXCG100_GBA_BUTTON_RIGHT  0x010u
#define FXCG100_GBA_BUTTON_START  0x008u
#define FXCG100_GBA_BUTTON_SELECT 0x004u
#define FXCG100_GBA_BUTTON_B      0x002u
#define FXCG100_GBA_BUTTON_A      0x001u
#define FXCG100_GBA_BUTTON_NONE   0x000u

typedef enum fxcg100_gba_key {
  FXCG100_GBA_KEY_A = 0,
  FXCG100_GBA_KEY_B,
  FXCG100_GBA_KEY_SELECT,
  FXCG100_GBA_KEY_START,
  FXCG100_GBA_KEY_RIGHT,
  FXCG100_GBA_KEY_LEFT,
  FXCG100_GBA_KEY_UP,
  FXCG100_GBA_KEY_DOWN,
  FXCG100_GBA_KEY_L,
  FXCG100_GBA_KEY_R,
  FXCG100_GBA_KEY_COUNT
} fxcg100_gba_key;

typedef enum fxcg100_hotkey {
  FXCG100_HOTKEY_FAST_FORWARD = 0,
  FXCG100_HOTKEY_LOAD_STATE,
  FXCG100_HOTKEY_SAVE_STATE,
  FXCG100_HOTKEY_SAVE_EXIT,
  FXCG100_HOTKEY_DISPLAY_FPS,
  FXCG100_HOTKEY_COUNT
} fxcg100_hotkey;

#define FXCG100_HOTKEY_BIT(index) (1u << (index))

typedef enum fxcg100_menu_result {
  FXCG100_MENU_CONTINUE = 0,
  FXCG100_MENU_RETURN,
  FXCG100_MENU_RESET,
  FXCG100_MENU_QUIT,
  FXCG100_MENU_LOAD_GAME,
  FXCG100_MENU_LOAD_STATE,
  FXCG100_MENU_SAVE_STATE
} fxcg100_menu_result;

#define FXCG100_MENU_PAGE_SLOTS 5
#define FXCG100_DEBUG_MENU_LINES 64
#define FXCG100_DEBUG_MENU_LINE_MAX 64

typedef struct fxcg100_debug_info {
  uint32_t count;
  char lines[FXCG100_DEBUG_MENU_LINES][FXCG100_DEBUG_MENU_LINE_MAX];
} fxcg100_debug_info;

typedef enum fxcg100_game_sort {
  FXCG100_GAME_SORT_NAME = 0,
  FXCG100_GAME_SORT_LAST_PLAYED,
  FXCG100_GAME_SORT_NEWLY_ADDED,
  FXCG100_GAME_SORT_COUNT
} fxcg100_game_sort;

typedef struct fxcg100_menu_state {
  uint32_t rom_source;
  uint32_t game_sort;
  uint32_t screen_scale;
  uint32_t screen_filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t frameskip_variation;
  uint32_t savestate_slot;
  uint32_t quick_save_slot;
  uint32_t show_fps;
  uint32_t frame_limit;
  /* fx-CP400 only: landscape direction (CP_ROTATION_*) and whether the
   * on-screen touch keys are shown. Unused on the fx-CG models. */
  uint32_t rotation;
  uint32_t touch_keys;
  /* fx-CP400 only: 0 sends the picture by DMA, 1 by CPU writes. */
  uint32_t lcd_cpu;
  /* fx-CP400 only: wait for the panel's refresh before each frame. */
  uint32_t lcd_sync;
  /* ... and where the picture is in the panel's row count, 0..15. */
  uint32_t lcd_sync_offset;
  /* fx-CP400 only: backlight level 1..16 and PWM rate 1..9; 0 leaves what
   * the system set. */
  uint32_t backlight;
  uint32_t backlight_pwm;
  /* Interface appearance: colour scheme (CP_THEME_*), dark mode (1 = dark,
   * 0 = light), and whether the fx-CP400 interface animates. */
  uint32_t theme;
  uint32_t dark_mode;
  uint32_t animations;
  uint16_t keymap[FXCG100_GBA_KEY_COUNT];
  uint16_t hotkey_map[FXCG100_HOTKEY_COUNT];
  uint32_t cheat_active[10];
  uint32_t selected[FXCG100_MENU_PAGE_SLOTS];
  uint32_t scroll[FXCG100_MENU_PAGE_SLOTS];
  uint32_t random_lfsr;
} fxcg100_menu_state;

int fxcg100_key_down(int basic_keycode);
uint32_t fxcg100_poll_app_keys(void);
uint16_t fxcg100_poll_physical_key(void);
uint32_t fxcg100_poll_gba_buttons(void);
uint32_t fxcg100_poll_gba_buttons_mapped(const uint16_t *keymap);
uint32_t fxcg100_poll_hotkeys_mapped(const uint16_t *hotkey_map);
void fxcg100_keymap_defaults(uint16_t *keymap);
void fxcg100_hotkey_defaults(uint16_t *hotkey_map);
int fxcg100_keymap_valid(const uint16_t *keymap);
int fxcg100_hotkey_map_valid(const uint16_t *hotkey_map);
int fxcg100_input_maps_valid(const uint16_t *keymap,
                             const uint16_t *hotkey_map);
int fxcg100_key_bindable(uint16_t key);
const char *fxcg100_key_label(uint16_t key);
const char *fxcg100_gba_key_label(uint32_t index);
const char *fxcg100_hotkey_label(uint32_t index);

int fxcg100_config_load(fxcg100_menu_state *state);
int fxcg100_config_save(const fxcg100_menu_state *state);

/* Whole-file storage blobs over world-switched BFile (savestates). */
int fxcg100_storage_blob_size(const uint16_t *path);
int fxcg100_storage_read_blob(const uint16_t *path, void *dst, unsigned size);
int fxcg100_storage_write_blob(const uint16_t *path, const void *src, unsigned size);
/* Monotonic notification for successful or partially failed storage-mutation
 * attempts; direct NOR consumers use it to refresh only when necessary. */
uint32_t fxcg100_storage_mutation_generation(void);
void fxcg100_storage_note_mutation(void);

void fxcg100_menu_init(fxcg100_menu_state *state);
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash,
                                     const fxcg100_debug_info *debug);
int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame);
uint32_t fxcg100_rom_source_count(void);
const char *fxcg100_rom_source_label(uint32_t index);
/* Monotonic play-order counter for ROM `index` (higher = played more recently;
 * 0 = never played) and monotonic added-order counter (higher = added later). */
uint32_t fxcg100_rom_source_last_played(uint32_t index);
uint32_t fxcg100_rom_source_added_order(uint32_t index);

void fxcg100_lcd_init(void);
void fxcg100_lcd_shutdown(void);
void fxcg100_lcd_clear(uint16_t color);
void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
                           uint16_t color);
void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
                           uint16_t fg, uint16_t bg);
void fxcg100_lcd_update(void);
/* Push only VRAM rows y..y+h-1 (widened to whole groups of four rows). */
void fxcg100_lcd_update_rows(unsigned y, unsigned h);
void fxcg100_lcd_status(const char *text);
/* OS BFile calls can draw a busy icon directly into LCD GRAM. Mark that
 * activity so the next gameplay presentation removes any stale border pixels. */
void fxcg100_lcd_note_os_activity(void);
void fxcg100_lcd_blit_gba(const uint16_t *pixels);
/* Exact unscaled streaming presenter. begin() returns zero for a scaled mode;
 * scanline() is called after a completed GBA row; end() returns one only when
 * all 160 rows were queued, otherwise the caller performs the normal blit. */
int fxcg100_lcd_stream_begin(void);
void fxcg100_lcd_stream_scanline(const uint16_t *row, unsigned y, int dirty);
int fxcg100_lcd_stream_end(void);
int fxcg100_lcd_is_synced(void);
/* Display scaling for the gameplay blit (menu DISPLAY SCALING):
 * 0 = 1:1 centered, 1 = 4:3 320x212, 2 = fullscreen 384x216. */
void fxcg100_lcd_set_scale(uint32_t mode);
/* Scale filter (menu SCALE FILTER), applied to the scaled modes:
 * 0 = SMOOTH, 1 = SHARP, 2 = CRISP (nearest). */
void fxcg100_lcd_set_filter(uint32_t filter);
#ifdef CGBA_CLASSPAD
/* fx-CP400: on that model fxcg100_lcd_set_scale() takes a CP_VIEW_* layout
 * (classpad_layout.h) instead of the fx-CG scale modes above. */
void fxcg100_lcd_set_rotation(uint32_t rotation);
void fxcg100_lcd_set_touch_keys(uint32_t enabled);
/* Paint the touch keys for the GBA buttons that are held, by touch or from
 * the keypad. */
void fxcg100_touch_show_buttons(uint32_t buttons);
/* Whether Clear opens the menu: it does until the key map gives it another
 * job. The Menu and Settings icons under the screen always do. */
void fxcg100_lcd_set_menu_key(uint32_t clear_opens_menu);
/* Colour scheme of the touch keys (CP_THEME_*, classpad_theme.h). */
void fxcg100_lcd_set_theme(uint32_t theme);
void fxcg100_lcd_set_transfer(uint32_t cpu_writes);
/* fx-CP400: start each frame where the panel's refresh will not meet it
 * (classpad_sync.h), and what is known about that refresh. */
void fxcg100_lcd_set_sync(uint32_t wait, uint32_t offset);
/* While the page that sets the offset is open: every frame waits, with
 * almost no room to spare, so that a wrong offset shows. Opening it counts
 * the panel's rows again. */
void fxcg100_lcd_sync_calibrate(uint32_t on);
/* What that count found, for the page to show: rows in one refresh and
 * refreshes a second. Returns 1 if the panel has a usable row counter, -1 if
 * it has none, 0 if it has not been read yet. */
int fxcg100_lcd_sync_info(uint32_t *rows, uint32_t *hz);
/* Compact diagnostic text shown on the calibration page when scan_probe()
 * fails to recognise a row counter (panel ID, max row, Hz, believed/samples). */
void fxcg100_lcd_sync_diag(char *buf, size_t size);
/* fx-CP400: backlight level (1..16, 16 = full) and PWM rate (1..9, 9 the
 * fastest); 0 for either leaves, or puts back, what the system had set. */
void fxcg100_lcd_set_backlight(uint32_t level, uint32_t pwm);
/* GBA buttons currently held on the touch keys (FXCG100_GBA_BUTTON_* bits),
 * as sampled by the last fxcg100_poll_app_keys(). */
uint32_t fxcg100_touch_gba_buttons(void);
/* Icons of the printed strip under the LCD being touched, as sampled by the
 * last fxcg100_poll_app_keys(). */
#define FXCG100_STRIP_SETTINGS (1u << 0)
#define FXCG100_STRIP_MENU     (1u << 1)
#define FXCG100_STRIP_MAIN     (1u << 2)
#define FXCG100_STRIP_RESIZE   (1u << 3)
#define FXCG100_STRIP_SWAP     (1u << 4)
#define FXCG100_STRIP_ROTATE   (1u << 5)
#define FXCG100_STRIP_ESC      (1u << 6)
uint32_t fxcg100_touch_strip_icons(void);
/* First touch on the LCD in the coordinates the player sees; zero when the
 * screen is not being touched. Sampled by fxcg100_poll_app_keys(). */
int fxcg100_touch_point(int *x, int *y);
/* Push gint's VRAM to the panel unchanged (the fx-CP400 interface draws it
 * already turned for the current orientation). */
void fxcg100_lcd_present_frame(void);
/* Clock and bus settings (classpad_clock.h). Writing waits for any display
 * transfer first; gint restores the OS's own settings around every OS call
 * and when gpSP exits. */
struct cp_clock;
void fxcg100_clock_read(struct cp_clock *clock);
void fxcg100_clock_write(const struct cp_clock *clock);
/* How many times a touch read that never finished had to be called off. */
uint32_t fxcg100_touch_recoveries(void);
/* Nonzero when gpSP reads the touch controller itself rather than through
 * gint's driver. */
int fxcg100_touch_is_polled(void);
/* The same, by kind: 0 stale flag at the start of a read, 1 read ended on a
 * bus error, 2 read ended for making no progress, 3 interrupt with no read
 * under way. */
uint32_t fxcg100_touch_recovery_count(int kind);
/* The clock test (classpad_platform.c). The RAM part returns zero if the
 * memory held everything; the signature must be the same on the stock clock
 * and on the clock under test. Together they take a few seconds. */
/* Rounds of a fixed loop in a quarter of a second of the RTC: a measure of
 * the real processor speed, or zero if the RTC does not count. */
uint32_t fxcg100_measure_speed(void);
int fxcg100_stress_ram(void);
uint32_t fxcg100_stress_signature(void);
/* Checksum of the RAM-resident emulator code, to detect read errors. */
uint32_t fxcg100_core_checksum(void);
/* Apply the clock setting saved for start-up, unless the menu key is held. */
void cp_ui_apply_saved_clock(void);
/* Tell the interface that a game is about to be shown: from then on it is
 * drawn the way round the game is, not upright as at start-up. */
void cp_ui_game_starting(void);
/* A full-screen message in the fx-CP400 interface's look (classpad_ui.c). */
void cp_ui_status(const fxcg100_menu_state *state, const char *line1,
                  const char *line2);
#endif
/* 128 Hz ticks. fx-CP400: from a timer, or from the RTC once it is seen to
 * run. fx-CG: the RTC. */
uint32_t fxcg100_ticks(void);
/* Wait until all physical keys are released (with debounce and safety timeout)
 * before entering an OS/Fugue world switch. */
void fxcg100_wait_keys_released(void);

/* Work around a libgint-cg.a _drv_tmu world-switch race on SH7305:
 * _drv_tmu's hsave double-reads volatile ETMU.TCNT and its hrestore sets
 * ETMU.TSTR=1 before spinning `while (ETMU.TCNT != saved)` at 236 MHz.
 * Pausing active ETMUs across gint_world_switch() keeps them stopped during
 * both hsave and hrestore, preventing the 32.768 kHz countdown deadlock. */
static inline void cgba_etmu_pause(uint8_t saved[6])
{
#if defined(__sh__) && (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && \
    !defined(CGBA_CLASSPAD) && !defined(CGBA_HOST_TEST) && \
    !defined(CGBA_GPSP_HEADLESS_TEST)
  volatile uint8_t *base = (volatile uint8_t *)0xa44d0030u;
  for (unsigned i = 0; i < 6u; i++) {
    volatile uint8_t *tstr = base + i * 0x20u;
    uint8_t v = *tstr;
    saved[i] = v;
    if (v & 1u) {
      *tstr = (uint8_t)(v & ~1u);
      for (unsigned spin = 0; spin < 4000u && (*tstr & 1u); spin++)
        ;
    }
  }
#else
  for (unsigned i = 0; i < 6u; i++)
    saved[i] = 0;
#endif
}

static inline void cgba_etmu_resume(const uint8_t saved[6])
{
#if defined(__sh__) && (defined(CGBA_FXCG100) || defined(CGBA_FXCG50)) && \
    !defined(CGBA_CLASSPAD) && !defined(CGBA_HOST_TEST) && \
    !defined(CGBA_GPSP_HEADLESS_TEST)
  volatile uint8_t *base = (volatile uint8_t *)0xa44d0030u;
  for (unsigned i = 0; i < 6u; i++) {
    if (saved[i] & 1u) {
      volatile uint8_t *tstr = base + i * 0x20u;
      *tstr = saved[i];
    }
  }
#else
  (void)saved;
#endif
}

#ifndef CGBA_CLASSPAD
/* The fx-CG interface (cg_ui.c). A message while something slow happens,
 * as a small box: over the game picture, dimmed and placed as the display
 * mode places it, when `frame` is the GBA frame, else over a plain page. */
void cg_ui_status(const fxcg100_menu_state *state, const char *line1,
                  const char *line2, const uint16_t *frame);
/* While main() saves or loads a state for the menu: show `text` in the
 * footer of the page that asked for it. */
void cg_ui_show_progress(const char *text);
/* The next fxcg100_menu_run() goes back to the page and row it left and
 * shows `text` in the footer for two seconds. */
void cg_ui_set_result(const char *text, int error);
#endif
uint32_t fxcg100_frame_hash(const uint16_t *pixels);

/* Draw an FPS metrics overlay into the top-left of a 240x160 GBA frame buffer
 * (so it rides along with the DMA blit). emu_fps = emulated frame rate,
 * draw_fps = drawn/blitted frame rate after frameskip. */
void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
                             unsigned draw_fps);

int cgba_run_jit_probe(void);

#endif
