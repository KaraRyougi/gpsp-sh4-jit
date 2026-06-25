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
  uint32_t cheat_active[10];
  uint32_t selected[4];
  uint32_t scroll[4];
  uint32_t random_lfsr;
} fxcg100_menu_state;

int fxcg100_key_down(int basic_keycode);
uint32_t fxcg100_poll_app_keys(void);
uint32_t fxcg100_poll_gba_buttons(void);

void fxcg100_menu_init(fxcg100_menu_state *state);
fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash);
int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame);
uint32_t fxcg100_rom_source_count(void);
const char *fxcg100_rom_source_label(uint32_t index);

void fxcg100_lcd_init(void);
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
 * (so it rides along with the DMA blit). emu_fps = emulated frame rate, vid_fps
 * = rendered/blitted frame rate after frameskip. */
void fxcg100_lcd_overlay_fps(uint16_t *pixels, unsigned emu_fps,
                             unsigned vid_fps);

int cgba_run_jit_probe(void);

#endif
