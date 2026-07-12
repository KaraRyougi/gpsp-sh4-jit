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

#ifdef CGBA_FXCG50
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

typedef struct fxcg100_menu_state {
  uint32_t rom_source;
  uint32_t screen_scale;
  uint32_t screen_filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t frameskip_variation;
  uint32_t savestate_slot;
  uint32_t quick_save_slot;
  uint32_t backup_update;
  uint32_t show_fps;
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

void fxcg100_menu_init(fxcg100_menu_state *state);
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash,
                                     const fxcg100_debug_info *debug);
int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame);
uint32_t fxcg100_rom_source_count(void);
const char *fxcg100_rom_source_label(uint32_t index);

void fxcg100_lcd_init(void);
void fxcg100_lcd_shutdown(void);
void fxcg100_lcd_clear(uint16_t color);
void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
                           uint16_t color);
void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
                           uint16_t fg, uint16_t bg);
void fxcg100_lcd_update(void);
void fxcg100_lcd_status(const char *text);
void fxcg100_lcd_blit_gba(const uint16_t *pixels);
/* Display scaling for the gameplay blit (menu DISPLAY SCALING):
 * 0 = 1:1 centered, 1 = 4:3 320x212, 2 = fullscreen 384x216. */
void fxcg100_lcd_set_scale(uint32_t mode);
/* Scale filter (menu SCALE FILTER), applied to the scaled modes:
 * 0 = SMOOTH, 1 = SHARP, 2 = CRISP (nearest). */
void fxcg100_lcd_set_filter(uint32_t filter);
uint32_t fxcg100_frame_hash(const uint16_t *pixels);

/* Draw an FPS metrics overlay into the top-left of a 240x160 GBA frame buffer
 * (so it rides along with the DMA blit). emu_fps = emulated frame rate,
 * draw_fps = drawn/blitted frame rate after frameskip. */
void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
                             unsigned draw_fps);

int cgba_run_jit_probe(void);

#endif
