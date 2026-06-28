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

#define FXCG100_KEY_F1       0x91u
#define FXCG100_KEY_F2       0x92u
#define FXCG100_KEY_F3       0x93u
#define FXCG100_KEY_F4       0x94u
#define FXCG100_KEY_F5       0x95u
#define FXCG100_KEY_F6       0x96u

#define FXCG100_KEY_SHIFT    0x81u
#define FXCG100_KEY_TOOLS    0x82u
#define FXCG100_KEY_VARS     0x83u
#define FXCG100_KEY_HOME     0x84u
#define FXCG100_KEY_LEFT     0x85u
#define FXCG100_KEY_UP       0x86u

#define FXCG100_KEY_ALPHA    0x71u
#define FXCG100_KEY_SQUARE   0x72u
#define FXCG100_KEY_POWER    0x73u
#define FXCG100_KEY_BACK     0x74u
#define FXCG100_KEY_DOWN     0x75u
#define FXCG100_KEY_RIGHT    0x76u

#define FXCG100_KEY_XOT      0x61u
#define FXCG100_KEY_FRAC     0x51u
#define FXCG100_KEY_SQRT     0xaeu
#define FXCG100_KEY_EXP_FUN  0xafu
#define FXCG100_KEY_SIN      0x64u
#define FXCG100_KEY_COS      0x65u
#define FXCG100_KEY_TAN      0x66u
#define FXCG100_KEY_COMMA    0x55u
#define FXCG100_KEY_LEFTP    0x53u
#define FXCG100_KEY_RIGHTP   0x54u

#define FXCG100_KEY_7        0x41u
#define FXCG100_KEY_8        0x42u
#define FXCG100_KEY_9        0x43u
#define FXCG100_KEY_DEL      0x44u
#define FXCG100_KEY_AC       0x45u
#define FXCG100_KEY_4        0x31u
#define FXCG100_KEY_5        0x32u
#define FXCG100_KEY_6        0x33u
#define FXCG100_KEY_MUL      0x34u
#define FXCG100_KEY_DIV      0x35u
#define FXCG100_KEY_1        0x21u
#define FXCG100_KEY_2        0x22u
#define FXCG100_KEY_3        0x23u
#define FXCG100_KEY_ADD      0x24u
#define FXCG100_KEY_SUB      0x25u
#define FXCG100_KEY_0        0x11u
#define FXCG100_KEY_DOT      0x12u
#define FXCG100_KEY_EXP      0x13u
#define FXCG100_KEY_EXE      0x15u

#define FXCG100_KEY_ON       0xa6u
#define FXCG100_KEY_PREVTAB  0xa7u
#define FXCG100_KEY_NEXTTAB  0xa8u
#define FXCG100_KEY_PAGEUP   0xa9u
#define FXCG100_KEY_PAGEDOWN 0xaau
#define FXCG100_KEY_SETTINGS 0xabu
#define FXCG100_KEY_OK       0xacu
#define FXCG100_KEY_CATALOG  0xadu
#define FXCG100_KEY_FORMAT   0x52u

#define FXCG100_PHYSKEY_ON FXCG100_KEY_ON

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
  uint32_t selected[4];
  uint32_t scroll[4];
  uint32_t random_lfsr;
} fxcg100_menu_state;

int fxcg100_key_down(int keycode);
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

void fxcg100_menu_init(fxcg100_menu_state *state);
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash);
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
uint32_t fxcg100_frame_hash(const uint16_t *pixels);

/* Draw an FPS metrics overlay into the top-left of a 240x160 GBA frame buffer
 * (so it rides along with the DMA blit). emu_fps = emulated frame rate,
 * draw_fps = drawn/blitted frame rate after frameskip. */
void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
                             unsigned draw_fps);

int cgba_run_jit_probe(void);

#endif
