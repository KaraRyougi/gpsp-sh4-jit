#include "fxcg100_platform.h"

#include <stdio.h>

#if CGBA_FULL_GPSP
#include "test_rom/mode3_smoke.h"
#include "vendor/gpsp/common.h"
void gpsp_set_input_state_bits(u32 gba_buttons);
#endif

#ifndef CGBA_RUN_JIT_PROBE
#define CGBA_RUN_JIT_PROBE 0
#endif

#ifndef CGBA_FULL_GPSP
#define CGBA_FULL_GPSP 0
#endif

#define CGBA_FAST_FORWARD_RENDER_PERIOD 8u

#if CGBA_FULL_GPSP
static uint16_t fxcg100_framebuffer[GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1)]
    __attribute__((aligned(32)));
#endif

static void print_hex_line(const char *label, uint32_t value)
{
  fxcg100_debug_puts(label);
  fxcg100_debug_hex32(value);
  fxcg100_debug_puts("\n");
}

static unsigned app_popcount(uint32_t value)
{
  unsigned count = 0;

  while (value) {
    count += value & 1u;
    value >>= 1;
  }

  return count;
}

static int app_keys_plausible(uint32_t keys)
{
  return app_popcount(keys) <= 3;
}

static void app_delay(void)
{
  volatile unsigned i;

  for (i = 0; i < 16000; i++)
    __asm__ volatile("nop");
}

#if !CGBA_FULL_GPSP
static uint32_t read_stack_pointer(void)
{
  uint32_t sp;

  __asm__ volatile("mov r15, %0" : "=r"(sp));
  return sp;
}

static void draw_safe_boot_screen(uint32_t keys, uint32_t ticks)
{
  char line[64];
  const uint16_t bg = (uint16_t)((1u << 11) | (2u << 5) | 5u);
  const uint16_t stripe0 = (uint16_t)((4u << 11) | (8u << 5) | 18u);
  const uint16_t stripe1 = (uint16_t)((1u << 11) | (18u << 5) | 10u);
  const uint16_t stripe2 = (uint16_t)((24u << 11) | (18u << 5) | 2u);
  const uint16_t text = (uint16_t)((30u << 11) | (58u << 5) | 31u);
  const uint16_t dim = (uint16_t)((16u << 11) | (32u << 5) | 24u);
  const uint16_t accent = (uint16_t)((31u << 11) | (48u << 5) | 12u);

  fxcg100_lcd_clear(bg);
  fxcg100_lcd_fill_rect(0, 0, 396, 18, stripe0);
  fxcg100_lcd_fill_rect(0, 206, 132, 18, stripe0);
  fxcg100_lcd_fill_rect(132, 206, 132, 18, stripe1);
  fxcg100_lcd_fill_rect(264, 206, 132, 18, stripe2);
  fxcg100_lcd_status("CGBA SAFE BOOT");
  fxcg100_lcd_draw_text(12, 34, "PHYSICAL SMOKE BUILD", accent, bg);
  fxcg100_lcd_draw_text(12, 50, "GPSP CORE NOT STARTED", text, bg);
  fxcg100_lcd_draw_text(12, 66, "ON SETTINGS", text, bg);
  fxcg100_lcd_draw_text(12, 82, "HOME RETURN AFTER BOOT", text, bg);

  snprintf(line, sizeof(line), "SP:%08x", (unsigned)read_stack_pointer());
  fxcg100_lcd_draw_text(12, 112, line, dim, bg);
  snprintf(line, sizeof(line), "KEYS:%08x T:%08x",
           (unsigned)keys, (unsigned)ticks);
  fxcg100_lcd_draw_text(12, 128, line, dim, bg);
  if (!app_keys_plausible(keys))
    fxcg100_lcd_draw_text(12, 144, "KEYSCAN IGNORED", accent, bg);
}

static int run_safe_boot_app(void)
{
  fxcg100_menu_state menu_state;
  uint32_t previous_app_keys = 0;
  uint32_t ticks = 0;

  fxcg100_debug_puts("\n[cgba] fx-CG100 safe boot\n");
  print_hex_line("[cgba] entry sp ", read_stack_pointer());

  fxcg100_lcd_init();
  fxcg100_menu_init(&menu_state);
  draw_safe_boot_screen(0, 0);

#if CGBA_RUN_JIT_PROBE
  {
    int jit_probe = cgba_run_jit_probe();
    if (jit_probe == 0) {
      fxcg100_debug_puts("[cgba] sh4 jit probe ok\n");
      fxcg100_lcd_status("CGBA JIT OK");
    } else {
      print_hex_line("[cgba] sh4 jit probe fail ", (uint32_t)jit_probe);
      fxcg100_lcd_status("CGBA JIT FAIL");
    }
  }
#else
  fxcg100_debug_puts("[cgba] sh4 jit probe skipped\n");
#endif

  for (;;) {
    uint32_t raw_app_keys = fxcg100_poll_app_keys();
    uint32_t app_keys = app_keys_plausible(raw_app_keys) ? raw_app_keys : 0;
    int menu_open_edge =
      (app_keys & FXCG100_APPKEY_ON) &&
      !(previous_app_keys & FXCG100_APPKEY_ON);

    if (ticks > 256 && (app_keys & FXCG100_APPKEY_HOME)) {
      fxcg100_debug_puts("[cgba] safe boot return requested\n");
      break;
    }

    if (ticks > 256 && menu_open_edge) {
      fxcg100_menu_result menu_result;

      fxcg100_debug_puts("[cgba] safe menu open\n");
      menu_result = fxcg100_menu_run(&menu_state, ticks, raw_app_keys, NULL);
      previous_app_keys = fxcg100_poll_app_keys();
      draw_safe_boot_screen(previous_app_keys, ticks);

      if (menu_result == FXCG100_MENU_QUIT) {
        fxcg100_debug_puts("[cgba] safe menu quit requested\n");
        break;
      }
      continue;
    }

    if ((ticks & 0x1ffu) == 0)
      draw_safe_boot_screen(raw_app_keys, ticks);

    previous_app_keys = app_keys;
    ticks++;
    app_delay();
  }

  fxcg100_lcd_status("CGBA RETURN");
  fxcg100_lcd_shutdown();
  return 0;
}
#endif

#if CGBA_FULL_GPSP
static void run_one_frame(uint32_t gba_buttons)
{
  gpsp_set_input_state_bits(gba_buttons);
  update_input();

  clear_gamepak_stickybits();
  execute_arm(execute_cycles);
}
#endif

int emain(void)
{
#if !CGBA_FULL_GPSP
  return run_safe_boot_app();
#else
  uint32_t last_hash = 0;
  uint32_t previous_app_keys = 0;
  uint32_t previous_hotkeys = 0;
  fxcg100_menu_state menu_state;
  char status[64];
  unsigned frame;

  fxcg100_debug_puts("\n[cgba] fx-CG100 gpSP port boot\n");
  fxcg100_lcd_init();
  fxcg100_lcd_clear(0);
  fxcg100_lcd_status("CGBA BOOT");
  fxcg100_menu_init(&menu_state);

#if CGBA_RUN_JIT_PROBE
  {
    int jit_probe = cgba_run_jit_probe();
    if (jit_probe == 0) {
      fxcg100_debug_puts("[cgba] sh4 jit probe ok\n");
      fxcg100_lcd_status("CGBA JIT OK");
    } else {
      print_hex_line("[cgba] sh4 jit probe fail ", (uint32_t)jit_probe);
      fxcg100_lcd_status("CGBA JIT FAIL");
    }
  }
#else
  fxcg100_debug_puts("[cgba] sh4 jit probe skipped\n");
  fxcg100_lcd_status("CGBA JIT OFF");
#endif

  gba_screen_pixels = fxcg100_framebuffer;
  memset(gba_screen_pixels, 0, sizeof(fxcg100_framebuffer));

  init_gamepak_buffer();
  init_sound();
  memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));

  if (load_gamepak_from_memory(cgba_mode3_smoke_gba,
                               cgba_mode3_smoke_gba_len,
                               FEAT_DISABLE, FEAT_DISABLE,
                               SERIAL_MODE_DISABLED) != 0) {
    fxcg100_lcd_status("CGBA ROM FAIL");
    fxcg100_panic("load embedded rom");
  }
  fxcg100_lcd_status("CGBA ROM OK");

  selected_boot_mode = boot_game;
  dynarec_enable = 0;
  sprite_limit = 1;

  reset_gba();

  fxcg100_debug_puts("[cgba] interpreter start\n");
  fxcg100_lcd_status("CGBA INT RUN");
  previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);

  for (frame = 1;; frame++) {
    uint32_t app_keys = fxcg100_poll_app_keys();
    uint32_t hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
    uint32_t hotkey_edge = hotkeys & ~previous_hotkeys;
    uint32_t gba_buttons = FXCG100_GBA_BUTTON_NONE;
    int render_video;
    int menu_open_edge =
      (app_keys & FXCG100_APPKEY_ON) &&
      !(previous_app_keys & FXCG100_APPKEY_ON);

    if (menu_open_edge) {
      fxcg100_menu_result menu_result;

      fxcg100_debug_puts("[cgba] menu open\n");
      menu_result = fxcg100_menu_run(&menu_state, frame, last_hash, NULL);
      previous_app_keys = fxcg100_poll_app_keys();
      previous_hotkeys = fxcg100_poll_hotkeys_mapped(menu_state.hotkey_map);
      fxcg100_lcd_clear(0);

      if (menu_result == FXCG100_MENU_QUIT) {
        fxcg100_debug_puts("[cgba] menu quit requested\n");
        break;
      }
      if (menu_result == FXCG100_MENU_RESET) {
        reset_gba();
        frame = 0;
        fxcg100_lcd_status("CGBA RESET");
        fxcg100_debug_puts("[cgba] menu reset requested\n");
        continue;
      }

      fxcg100_lcd_status("CGBA INT RUN");
      fxcg100_debug_puts("[cgba] menu close\n");
      continue;
    }

    if (hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_DISPLAY_FPS)) {
      menu_state.show_fps = menu_state.show_fps ? 0 : 1;
      fxcg100_lcd_status(menu_state.show_fps ? "CGBA FPS ON" :
                         "CGBA FPS OFF");
    }
    if (hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_LOAD_STATE))
      fxcg100_lcd_status("CGBA LOAD STATE TODO");
    if (hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_STATE))
      fxcg100_lcd_status("CGBA SAVE STATE TODO");
    if (hotkey_edge & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_SAVE_EXIT))
      fxcg100_lcd_status("CGBA SAVE+EXIT TODO");

    gba_buttons = fxcg100_poll_gba_buttons_mapped(menu_state.keymap);

    previous_app_keys = app_keys;
    run_one_frame(gba_buttons);
    last_hash = fxcg100_frame_hash(gba_screen_pixels);
    if (hotkeys & FXCG100_HOTKEY_BIT(FXCG100_HOTKEY_FAST_FORWARD))
      render_video = frame == 1 ||
        (frame % CGBA_FAST_FORWARD_RENDER_PERIOD) == 0;
    else
      render_video = fxcg100_menu_should_blit(&menu_state, frame);
    if (render_video)
      fxcg100_lcd_blit_gba(gba_screen_pixels);
    previous_hotkeys = hotkeys;

    if (frame == 1) {
      print_hex_line("[cgba] frame1 hash ", last_hash);
      snprintf(status, sizeof(status), "CGBA F:%06u H:%08x", frame, last_hash);
      fxcg100_lcd_status(status);
    } else if ((frame % 60) == 0) {
      print_hex_line("[cgba] frame hash ", last_hash);
      snprintf(status, sizeof(status), "CGBA F:%06u H:%08x", frame, last_hash);
      fxcg100_lcd_status(status);
    }
  }

  fxcg100_lcd_status("CGBA EXIT");
  fxcg100_lcd_shutdown();
  return 0;
#endif
}
