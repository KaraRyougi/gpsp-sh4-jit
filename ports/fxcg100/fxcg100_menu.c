#include "fxcg100_platform.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define MENU_TOP 28
#define MENU_MAIN_TOP 50
#define MENU_ROW_H 10
#define MENU_VISIBLE_ROWS 15
#define MENU_MAIN_VISIBLE_ROWS 13

#define RGB565(r, g, b) \
  (uint16_t)((((r) & 0x1f) << 11) | (((g) & 0x3f) << 5) | ((b) & 0x1f))

typedef enum menu_page_id {
  MENU_PAGE_MAIN = 0,
  MENU_PAGE_GRAPHICS,
  MENU_PAGE_DEBUG,
  MENU_PAGE_GAMEPAD,
  MENU_PAGE_CHEATS,
  MENU_PAGE_COUNT
} menu_page_id;

typedef char menu_state_page_slots_mismatch[
  FXCG100_MENU_PAGE_SLOTS >= MENU_PAGE_COUNT ? 1 : -1];

typedef enum menu_value_id {
  MENU_VALUE_NONE = 0,
  MENU_VALUE_ROM_SOURCE,
  MENU_VALUE_SCALE,
  MENU_VALUE_FRAMESKIP_TYPE,
  MENU_VALUE_FRAMESKIP_VALUE,
  MENU_VALUE_FRAMESKIP_VARIATION,
  MENU_VALUE_SHOW_FPS,
  MENU_VALUE_SAVE_SLOT,
  MENU_VALUE_BACKUP_UPDATE,
  MENU_VALUE_INPUT_RECORD
} menu_value_id;

typedef enum menu_item_kind {
  MENU_ITEM_ACTION = 0,
  MENU_ITEM_SUBMENU,
  MENU_ITEM_CHOICE,
  MENU_ITEM_NUMBER,
  MENU_ITEM_NUMBER_ACTION,
  MENU_ITEM_GBA_KEY,
  MENU_ITEM_HOTKEY,
  MENU_ITEM_DEBUG,
  MENU_ITEM_INFO
} menu_item_kind;

typedef enum menu_action_id {
  MENU_ACTION_NONE = 0,
  MENU_ACTION_BACK,
  MENU_ACTION_RETURN,
  MENU_ACTION_RESET,
  MENU_ACTION_QUIT,
  MENU_ACTION_LOAD_STATE,
  MENU_ACTION_SAVE_STATE,
  MENU_ACTION_LOAD_GAME,
  MENU_ACTION_RESET_KEYS,
  MENU_ACTION_SAVE_CONFIG,
  MENU_ACTION_LOAD_CONFIG,
  MENU_ACTION_CHEAT
} menu_action_id;

typedef struct menu_item {
  const char *label;
  menu_item_kind kind;
  menu_action_id action;
  menu_page_id submenu;
  menu_value_id value;
  const char * const *choices;
  uint32_t count;
  const char *info;
} menu_item;

static const fxcg100_debug_info *menu_debug_info;

static const char * const scale_options[] = {
  "UNSCALED 1:1",
  "4:3 SMOOTH",
  "FULLSCREEN"
};

static const char * const rom_options[] = {
	"MODE3 SMOKE",
	"INPUT PROBE",
	"LCD TEST"
};

uint32_t __attribute__((weak)) fxcg100_rom_source_count(void)
{
  return sizeof(rom_options) / sizeof(rom_options[0]);
}

const char *__attribute__((weak)) fxcg100_rom_source_label(uint32_t index)
{
  uint32_t count = fxcg100_rom_source_count();

  if (count == 0)
    return "?";
  return rom_options[index % count];
}

static const char * const frameskip_options[] = {
  "AUTOMATIC",
  "MANUAL",
  "OFF"
};

static const char * const frameskip_variation_options[] = {
  "UNIFORM",
  "RANDOM"
};

static const char * const on_off_options[] = {
  "OFF",
  "ON"
};

static const char * const backup_options[] = {
  "EXIT ONLY",
  "AUTOMATIC"
};

static const menu_item main_items[] = {
  { "ROM SOURCE", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_MAIN, MENU_VALUE_ROM_SOURCE, rom_options,
    sizeof(rom_options) / sizeof(rom_options[0]), NULL },
  { "GRAPHICS OPTIONS", MENU_ITEM_SUBMENU, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_NONE, NULL, 0, NULL },
  { "DEBUG INFO", MENU_ITEM_SUBMENU, MENU_ACTION_NONE,
    MENU_PAGE_DEBUG, MENU_VALUE_NONE, NULL, 0, NULL },
  { "LOAD STATE FROM SLOT", MENU_ITEM_NUMBER_ACTION, MENU_ACTION_LOAD_STATE,
    MENU_PAGE_MAIN, MENU_VALUE_SAVE_SLOT, NULL, 10, NULL },
  { "SAVE STATE TO SLOT", MENU_ITEM_NUMBER_ACTION, MENU_ACTION_SAVE_STATE,
    MENU_PAGE_MAIN, MENU_VALUE_SAVE_SLOT, NULL, 10, NULL },
  { "CONFIGURE GAMEPAD INPUT", MENU_ITEM_SUBMENU, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, NULL },
  { "SAVE CONFIG", MENU_ITEM_ACTION, MENU_ACTION_SAVE_CONFIG,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL },
  { "LOAD CONFIG", MENU_ITEM_ACTION, MENU_ACTION_LOAD_CONFIG,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL },
  { "CHEATS AND MISC OPTIONS", MENU_ITEM_SUBMENU, MENU_ACTION_NONE,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, NULL },
  { "LOAD NEW GAME", MENU_ITEM_ACTION, MENU_ACTION_LOAD_GAME,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL },
  { "RESTART GAME", MENU_ITEM_ACTION, MENU_ACTION_RESET,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL },
  { "RETURN TO GAME", MENU_ITEM_ACTION, MENU_ACTION_RETURN,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL },
  { "EXIT GPSP", MENU_ITEM_ACTION, MENU_ACTION_QUIT,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL }
};

static const menu_item graphics_items[] = {
  { "DISPLAY SCALING", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_SCALE, scale_options, 3, NULL },
  { "FRAMESKIP TYPE", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_FRAMESKIP_TYPE, frameskip_options, 3, NULL },
  { "FRAMESKIP VALUE", MENU_ITEM_NUMBER, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_FRAMESKIP_VALUE, NULL, 100, NULL },
  { "FRAMESKIP VARIATION", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_FRAMESKIP_VARIATION,
    frameskip_variation_options, 2, NULL },
  { "SHOW FPS", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_GRAPHICS, MENU_VALUE_SHOW_FPS, on_off_options, 2, NULL },
  { "BACK", MENU_ITEM_ACTION, MENU_ACTION_BACK,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL }
};

#define DEBUG_ITEM(index) \
  { "", MENU_ITEM_DEBUG, MENU_ACTION_NONE, \
    MENU_PAGE_DEBUG, MENU_VALUE_NONE, NULL, (index), NULL }

static const menu_item debug_items[] = {
  DEBUG_ITEM(0),
  DEBUG_ITEM(1),
  DEBUG_ITEM(2),
  DEBUG_ITEM(3),
  DEBUG_ITEM(4),
  DEBUG_ITEM(5),
  DEBUG_ITEM(6),
  DEBUG_ITEM(7),
  DEBUG_ITEM(8),
  DEBUG_ITEM(9),
  DEBUG_ITEM(10),
  DEBUG_ITEM(11),
  DEBUG_ITEM(12),
  DEBUG_ITEM(13),
  DEBUG_ITEM(14),
  DEBUG_ITEM(15),
  DEBUG_ITEM(16),
  DEBUG_ITEM(17),
  DEBUG_ITEM(18),
  DEBUG_ITEM(19),
  DEBUG_ITEM(20),
  DEBUG_ITEM(21),
  DEBUG_ITEM(22),
  DEBUG_ITEM(23),
  DEBUG_ITEM(24),
  DEBUG_ITEM(25),
  DEBUG_ITEM(26),
  DEBUG_ITEM(27),
  DEBUG_ITEM(28),
  DEBUG_ITEM(29),
  DEBUG_ITEM(30),
  DEBUG_ITEM(31),
  DEBUG_ITEM(32),
  DEBUG_ITEM(33),
  DEBUG_ITEM(34),
  DEBUG_ITEM(35),
  DEBUG_ITEM(36),
  DEBUG_ITEM(37),
  DEBUG_ITEM(38),
  DEBUG_ITEM(39),
  DEBUG_ITEM(40),
  DEBUG_ITEM(41),
  DEBUG_ITEM(42),
  DEBUG_ITEM(43),
  DEBUG_ITEM(44),
  DEBUG_ITEM(45),
  DEBUG_ITEM(46),
  DEBUG_ITEM(47),
  DEBUG_ITEM(48),
  DEBUG_ITEM(49),
  DEBUG_ITEM(50),
  DEBUG_ITEM(51),
  DEBUG_ITEM(52),
  DEBUG_ITEM(53),
  DEBUG_ITEM(54),
  DEBUG_ITEM(55),
  DEBUG_ITEM(56),
  DEBUG_ITEM(57),
  DEBUG_ITEM(58),
  DEBUG_ITEM(59),
  DEBUG_ITEM(60),
  DEBUG_ITEM(61),
  DEBUG_ITEM(62),
  DEBUG_ITEM(63),
  { "BACK", MENU_ITEM_ACTION, MENU_ACTION_BACK,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL }
};

#undef DEBUG_ITEM

static const menu_item gamepad_items[] = {
  { "MENU", MENU_ITEM_INFO, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "ON" },
  { "GBA A", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_A, NULL },
  { "GBA B", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_B, NULL },
  { "GBA SELECT", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_SELECT, NULL },
  { "GBA START", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_START, NULL },
  { "GBA RIGHT", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_RIGHT, NULL },
  { "GBA LEFT", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_LEFT, NULL },
  { "GBA UP", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_UP, NULL },
  { "GBA DOWN", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_DOWN, NULL },
  { "GBA L", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_L, NULL },
  { "GBA R", MENU_ITEM_GBA_KEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, FXCG100_GBA_KEY_R, NULL },
  { "FAST FORWARD", MENU_ITEM_HOTKEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL,
    FXCG100_HOTKEY_FAST_FORWARD, NULL },
  { "LOAD STATE", MENU_ITEM_HOTKEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL,
    FXCG100_HOTKEY_LOAD_STATE, NULL },
  { "SAVE STATE", MENU_ITEM_HOTKEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL,
    FXCG100_HOTKEY_SAVE_STATE, NULL },
  { "SAVE+EXIT", MENU_ITEM_HOTKEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL,
    FXCG100_HOTKEY_SAVE_EXIT, NULL },
  { "DISPLAY FPS", MENU_ITEM_HOTKEY, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL,
    FXCG100_HOTKEY_DISPLAY_FPS, NULL },
  { "RESET DEFAULT KEYS", MENU_ITEM_ACTION, MENU_ACTION_RESET_KEYS,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, NULL },
  { "BACK", MENU_ITEM_ACTION, MENU_ACTION_BACK,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL }
};

static const menu_item cheats_items[] = {
  { "CHEAT 0", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 1", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 2", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 3", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 4", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 5", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 6", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 7", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 8", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "CHEAT 9", MENU_ITEM_INFO, MENU_ACTION_CHEAT,
    MENU_PAGE_CHEATS, MENU_VALUE_NONE, NULL, 0, "NONE" },
  { "UPDATE BACKUP", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_CHEATS, MENU_VALUE_BACKUP_UPDATE, backup_options, 2, NULL },
  { "RECORD INPUT LOG", MENU_ITEM_CHOICE, MENU_ACTION_NONE,
    MENU_PAGE_CHEATS, MENU_VALUE_INPUT_RECORD, on_off_options, 2, NULL },
  { "BACK", MENU_ITEM_ACTION, MENU_ACTION_BACK,
    MENU_PAGE_MAIN, MENU_VALUE_NONE, NULL, 0, NULL }
};

static uint32_t *menu_value_ptr(fxcg100_menu_state *state, menu_value_id id)
{
  switch (id) {
  case MENU_VALUE_ROM_SOURCE:
    return &state->rom_source;
  case MENU_VALUE_SCALE:
    return &state->screen_scale;
  case MENU_VALUE_FRAMESKIP_TYPE:
    return &state->frameskip_type;
  case MENU_VALUE_FRAMESKIP_VALUE:
    return &state->frameskip_value;
  case MENU_VALUE_FRAMESKIP_VARIATION:
    return &state->frameskip_variation;
  case MENU_VALUE_SHOW_FPS:
    return &state->show_fps;
  case MENU_VALUE_SAVE_SLOT:
    return &state->savestate_slot;
  case MENU_VALUE_BACKUP_UPDATE:
    return &state->backup_update;
  case MENU_VALUE_INPUT_RECORD:
    return &state->input_record;
  default:
    return NULL;
  }
}

static uint32_t menu_choice_count(const menu_item *item)
{
  if (item->value == MENU_VALUE_ROM_SOURCE)
    return fxcg100_rom_source_count();
  return item->count;
}

static const char *menu_choice_label(const menu_item *item, uint32_t index)
{
  if (item->value == MENU_VALUE_ROM_SOURCE)
    return fxcg100_rom_source_label(index);
  if (item->choices && item->count)
    return item->choices[index % item->count];
  return "?";
}

static const menu_item *menu_page_items(menu_page_id page, size_t *count,
                                        const char **title)
{
  switch (page) {
  case MENU_PAGE_GRAPHICS:
    *count = sizeof(graphics_items) / sizeof(graphics_items[0]);
    *title = "GRAPHICS OPTIONS";
    return graphics_items;
  case MENU_PAGE_DEBUG:
    *count = sizeof(debug_items) / sizeof(debug_items[0]);
    *title = "DEBUG INFO";
    return debug_items;
  case MENU_PAGE_GAMEPAD:
    *count = sizeof(gamepad_items) / sizeof(gamepad_items[0]);
    *title = "MAP GAMEPAD INPUT";
    return gamepad_items;
  case MENU_PAGE_CHEATS:
    *count = sizeof(cheats_items) / sizeof(cheats_items[0]);
    *title = "CHEATS AND MISC OPTIONS";
    return cheats_items;
  case MENU_PAGE_MAIN:
  default:
    *count = sizeof(main_items) / sizeof(main_items[0]);
    *title = "CGBA SETTINGS";
    return main_items;
  }
}

static uint32_t menu_visible_rows(menu_page_id page)
{
  return page == MENU_PAGE_MAIN ? MENU_MAIN_VISIBLE_ROWS : MENU_VISIBLE_ROWS;
}

static uint32_t menu_top(menu_page_id page)
{
  return page == MENU_PAGE_MAIN ? MENU_MAIN_TOP : MENU_TOP;
}

static void menu_delay(void)
{
  volatile unsigned i;

  for (i = 0; i < 12000; i++)
    __asm__ volatile("nop");
}

static uint32_t menu_wait_edge(uint32_t *previous, uint32_t *edge_out)
{
  for (;;) {
    uint32_t keys = fxcg100_poll_app_keys();
    uint32_t edge = keys & ~*previous;

    *previous = keys;
    if (edge) {
      *edge_out = edge;
      return keys;
    }

    menu_delay();
  }
}

static void menu_wait_no_physical_keys(void)
{
  for (;;) {
    if (fxcg100_poll_physical_key() == 0)
      return;
    menu_delay();
  }
}

static uint16_t menu_wait_physical_key(void)
{
  for (;;) {
    uint16_t key = fxcg100_poll_physical_key();

    if (key)
      return key;
    menu_delay();
  }
}

static void menu_clamp_cursor(fxcg100_menu_state *state, menu_page_id page,
                              size_t count)
{
  uint32_t *selected = &state->selected[page];
  uint32_t *scroll = &state->scroll[page];
  uint32_t visible_rows = menu_visible_rows(page);

  if (count == 0) {
    *selected = 0;
    *scroll = 0;
    return;
  }

  if (*selected >= count)
    *selected = (uint32_t)count - 1;
  if (*selected < *scroll)
    *scroll = *selected;
  if (*selected >= *scroll + visible_rows)
    *scroll = *selected - visible_rows + 1;
  if (count <= visible_rows)
    *scroll = 0;
  else if (*scroll + visible_rows > count)
    *scroll = (uint32_t)count - visible_rows;
}

static void menu_adjust_value(fxcg100_menu_state *state, const menu_item *item,
                              int delta)
{
  uint32_t *value = menu_value_ptr(state, item->value);
  uint32_t count = menu_choice_count(item);

  if (!value || count == 0)
    return;

  if (delta < 0)
    *value = (*value == 0) ? count - 1 : *value - 1;
  else
    *value = (*value + 1) % count;
}

static void menu_format_item(fxcg100_menu_state *state, const menu_item *item,
                             char *line, size_t line_size)
{
  uint32_t *value;

  switch (item->kind) {
  case MENU_ITEM_CHOICE:
    value = menu_value_ptr(state, item->value);
    if (value && menu_choice_count(item))
      snprintf(line, line_size, "%s: %s", item->label,
               menu_choice_label(item, *value));
    else
      snprintf(line, line_size, "%s", item->label);
    break;
  case MENU_ITEM_NUMBER:
  case MENU_ITEM_NUMBER_ACTION:
    value = menu_value_ptr(state, item->value);
    snprintf(line, line_size, "%s: %u", item->label,
             value ? (unsigned)(*value % item->count) : 0);
    break;
  case MENU_ITEM_GBA_KEY:
    if (item->count < FXCG100_GBA_KEY_COUNT)
      snprintf(line, line_size, "%s: %s", item->label,
               fxcg100_key_label(state->keymap[item->count]));
    else
      snprintf(line, line_size, "%s: ?", item->label);
    break;
  case MENU_ITEM_HOTKEY:
    if (item->count < FXCG100_HOTKEY_COUNT)
      snprintf(line, line_size, "%s: %s", item->label,
               fxcg100_key_label(state->hotkey_map[item->count]));
    else
      snprintf(line, line_size, "%s: ?", item->label);
    break;
  case MENU_ITEM_DEBUG:
    if (menu_debug_info && item->count < menu_debug_info->count)
      snprintf(line, line_size, "%s", menu_debug_info->lines[item->count]);
    else if (item->count == 0)
      snprintf(line, line_size, "NO DEBUG SNAPSHOT");
    else
      if (line_size)
        line[0] = '\0';
    break;
  case MENU_ITEM_INFO:
    snprintf(line, line_size, "%s: %s", item->label,
             item->info ? item->info : "");
    break;
  default:
    snprintf(line, line_size, "%s", item->label);
    break;
  }
}

static void menu_draw(fxcg100_menu_state *state, menu_page_id page,
                      const char *message, uint32_t frame, uint32_t last_hash)
{
  const uint16_t bg = RGB565(1, 2, 4);
  const uint16_t panel = RGB565(3, 6, 10);
  const uint16_t selected_bg = RGB565(6, 24, 22);
  const uint16_t text = RGB565(30, 58, 31);
  const uint16_t dim = RGB565(17, 32, 22);
  const uint16_t accent = RGB565(31, 48, 12);
  const menu_item *items;
  const char *title;
  size_t count;
  uint32_t selected;
  uint32_t scroll;
  uint32_t top;
  uint32_t visible_rows;
  char line[96];
  unsigned row;

  items = menu_page_items(page, &count, &title);
  menu_clamp_cursor(state, page, count);
  selected = state->selected[page];
  scroll = state->scroll[page];
  top = menu_top(page);
  visible_rows = menu_visible_rows(page);

  fxcg100_lcd_clear(bg);
  fxcg100_lcd_fill_rect(0, 0, LCD_W, 18, panel);
  fxcg100_lcd_draw_text(4, 5, "CGBA", text, panel);
  snprintf(line, sizeof(line), "F:%06u H:%08x",
           (unsigned)frame, (unsigned)last_hash);
  fxcg100_lcd_draw_text(246, 5, line, dim, panel);
  if (page == MENU_PAGE_MAIN) {
    fxcg100_lcd_draw_text(8, 20, "C", RGB565(31, 4, 4), bg);
    fxcg100_lcd_draw_text(14, 20, "G", RGB565(31, 26, 0), bg);
    fxcg100_lcd_draw_text(20, 20, "B", RGB565(0, 26, 8), bg);
    fxcg100_lcd_draw_text(26, 20, "A", RGB565(5, 14, 31), bg);
    fxcg100_lcd_draw_text(8, 34,
                          "a gpSP port for CASIO fx-CG calculators",
                          text, bg);
  } else {
    fxcg100_lcd_draw_text(8, 20, title, accent, bg);
  }

  for (row = 0; row < visible_rows; row++) {
    uint32_t item_index = scroll + row;
    unsigned y = top + row * MENU_ROW_H;
    uint16_t row_bg = item_index == selected ? selected_bg : bg;

    if (item_index >= count)
      break;

    fxcg100_lcd_fill_rect(4, y - 1, LCD_W - 8, MENU_ROW_H, row_bg);
    menu_format_item(state, &items[item_index], line, sizeof(line));
    fxcg100_lcd_draw_text(10, y, line,
                          item_index == selected ? accent : text, row_bg);
  }

  fxcg100_lcd_fill_rect(0, 180, LCD_W, 36, bg);
  if (message && message[0])
    fxcg100_lcd_draw_text(8, 184, message, accent, bg);
  fxcg100_lcd_draw_text(8, 198, "UP/DOWN MOVE  LEFT/RIGHT CHANGE", dim, bg);
  fxcg100_lcd_draw_text(8, 208, "EXE SELECT  BACK RETURN  HOME EXIT", dim, bg);
  fxcg100_lcd_update();
}

static void menu_draw_capture(const char *target, const char *message)
{
  const uint16_t bg = RGB565(1, 2, 4);
  const uint16_t panel = RGB565(3, 6, 10);
  const uint16_t text = RGB565(30, 58, 31);
  const uint16_t dim = RGB565(17, 32, 22);
  const uint16_t accent = RGB565(31, 48, 12);

  fxcg100_lcd_clear(bg);
  fxcg100_lcd_fill_rect(0, 0, LCD_W, 18, panel);
  fxcg100_lcd_draw_text(4, 5, "CGBA GPSP", text, panel);
  fxcg100_lcd_draw_text(8, 28, "MAP INPUT", accent, bg);
  fxcg100_lcd_draw_text(8, 58, "PRESS KEY FOR", text, bg);
  fxcg100_lcd_draw_text(8, 76, target ? target : "?", accent, bg);
  fxcg100_lcd_draw_text(8, 118, "PRESS ON TO CANCEL", dim, bg);
  if (message && message[0])
    fxcg100_lcd_draw_text(8, 148, message, accent, bg);
  fxcg100_lcd_update();
}

static int menu_binding_key_used(const fxcg100_menu_state *state,
                                 const uint16_t *binding, uint16_t key)
{
  uint32_t i;

  if (!state || key == 0)
    return 0;

  for (i = 0; i < FXCG100_GBA_KEY_COUNT; i++) {
    if (&state->keymap[i] != binding && state->keymap[i] == key)
      return 1;
  }
  for (i = 0; i < FXCG100_HOTKEY_COUNT; i++) {
    if (&state->hotkey_map[i] != binding && state->hotkey_map[i] == key)
      return 1;
  }
  return 0;
}

static int menu_capture_binding(fxcg100_menu_state *state, const char *target,
                                uint16_t *binding)
{
  if (!state || !binding)
    return 0;
  menu_wait_no_physical_keys();

  for (;;) {
    uint16_t key;

    menu_draw_capture(target, "");
    key = menu_wait_physical_key();
    if (key == FXCG100_PHYSKEY_ON) {
      menu_wait_no_physical_keys();
      return 0;
    }
    if (fxcg100_key_bindable(key)) {
      if (menu_binding_key_used(state, binding, key)) {
        menu_draw_capture(target, "KEY ALREADY USED");
        menu_wait_no_physical_keys();
        continue;
      }
      *binding = key;
      menu_wait_no_physical_keys();
      return 1;
    }
    menu_draw_capture(target, "ON IS THE MENU KEY");
    menu_wait_no_physical_keys();
  }
}

static fxcg100_menu_result menu_activate_item(fxcg100_menu_state *state,
                                              menu_page_id *page,
                                              const menu_item *item,
                                              const char **message)
{
  switch (item->kind) {
  case MENU_ITEM_SUBMENU:
    *page = item->submenu;
    *message = "";
    return FXCG100_MENU_CONTINUE;
  case MENU_ITEM_CHOICE:
  case MENU_ITEM_NUMBER:
    menu_adjust_value(state, item, 1);
    *message = "";
    return FXCG100_MENU_CONTINUE;
  case MENU_ITEM_NUMBER_ACTION:
    break;
  case MENU_ITEM_GBA_KEY:
    if (item->count < FXCG100_GBA_KEY_COUNT &&
        menu_capture_binding(state, item->label, &state->keymap[item->count]))
      *message = "KEY MAPPING APPLIED";
    else
      *message = "KEY MAPPING CANCELLED";
    return FXCG100_MENU_CONTINUE;
  case MENU_ITEM_HOTKEY:
    if (item->count < FXCG100_HOTKEY_COUNT &&
        menu_capture_binding(state, item->label, &state->hotkey_map[item->count]))
      *message = "HOTKEY MAPPING APPLIED";
    else
      *message = "HOTKEY MAPPING CANCELLED";
    return FXCG100_MENU_CONTINUE;
  case MENU_ITEM_INFO:
  case MENU_ITEM_DEBUG:
    break;
  default:
    break;
  }

  switch (item->action) {
  case MENU_ACTION_BACK:
    if (*page == MENU_PAGE_MAIN)
      return FXCG100_MENU_RETURN;
    *page = MENU_PAGE_MAIN;
    *message = "";
    return FXCG100_MENU_CONTINUE;
  case MENU_ACTION_RETURN:
    return FXCG100_MENU_RETURN;
  case MENU_ACTION_RESET:
    return FXCG100_MENU_RESET;
  case MENU_ACTION_QUIT:
    return FXCG100_MENU_QUIT;
  case MENU_ACTION_LOAD_STATE:
    return FXCG100_MENU_LOAD_STATE;
  case MENU_ACTION_SAVE_STATE:
    return FXCG100_MENU_SAVE_STATE;
  case MENU_ACTION_LOAD_GAME:
    return FXCG100_MENU_LOAD_GAME;
  case MENU_ACTION_RESET_KEYS:
    fxcg100_keymap_defaults(state->keymap);
    fxcg100_hotkey_defaults(state->hotkey_map);
    *message = "DEFAULT KEYS RESTORED";
    return FXCG100_MENU_CONTINUE;
  case MENU_ACTION_SAVE_CONFIG:
    *message = fxcg100_config_save(state) ?
      "CONFIG SAVED" : "CONFIG SAVE FAILED";
    return FXCG100_MENU_CONTINUE;
  case MENU_ACTION_LOAD_CONFIG:
    *message = fxcg100_config_load(state) ?
      "CONFIG LOADED" : "NO SAVED CONFIG";
    return FXCG100_MENU_CONTINUE;
  case MENU_ACTION_CHEAT:
    *message = "CHEAT LOADER TODO";
    return FXCG100_MENU_CONTINUE;
  case MENU_ACTION_NONE:
  default:
    return FXCG100_MENU_CONTINUE;
  }
}

void fxcg100_menu_init(fxcg100_menu_state *state)
{
  memset(state, 0, sizeof(*state));
  state->rom_source = 0;
  state->screen_scale = 0;
  state->input_record = 0;
  state->frameskip_type = 1;
  state->frameskip_value = 1;
  state->frameskip_variation = 0;
  state->backup_update = 0;
  state->show_fps = 0;
  fxcg100_keymap_defaults(state->keymap);
  fxcg100_hotkey_defaults(state->hotkey_map);
  state->random_lfsr = 0x00c6ba5eu;
  fxcg100_config_load(state);
}

fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash,
                                     const fxcg100_debug_info *debug)
{
  menu_page_id page = MENU_PAGE_MAIN;
  const char *message = "";
  uint32_t previous = fxcg100_poll_app_keys();

  menu_debug_info = debug;
  for (;;) {
    const menu_item *items;
    const char *title;
    size_t count;
    uint32_t selected;
    uint32_t edge;

    menu_draw(state, page, message, frame, last_hash);
    items = menu_page_items(page, &count, &title);
    (void)title;
    menu_clamp_cursor(state, page, count);
    selected = state->selected[page];

    menu_wait_edge(&previous, &edge);

    if (edge & FXCG100_APPKEY_UP) {
      state->selected[page] = selected == 0 ? (uint32_t)count - 1 : selected - 1;
      message = "";
    } else if (edge & FXCG100_APPKEY_DOWN) {
      state->selected[page] = (selected + 1) % (uint32_t)count;
      message = "";
    } else if (edge & FXCG100_APPKEY_LEFT) {
      menu_adjust_value(state, &items[selected], -1);
      message = "";
    } else if (edge & FXCG100_APPKEY_RIGHT) {
      menu_adjust_value(state, &items[selected], 1);
      message = "";
    } else if (edge & FXCG100_APPKEY_HOME) {
      return FXCG100_MENU_QUIT;
    } else if (edge & FXCG100_APPKEY_BACK) {
      if (page == MENU_PAGE_MAIN)
        return FXCG100_MENU_RETURN;
      page = MENU_PAGE_MAIN;
      message = "";
    } else if (edge & FXCG100_APPKEY_EXE) {
      fxcg100_menu_result result =
        menu_activate_item(state, &page, &items[selected], &message);

      if (result != FXCG100_MENU_CONTINUE)
        return result;
    }
  }
}

int fxcg100_menu_should_blit(fxcg100_menu_state *state, uint32_t frame)
{
  uint32_t period;

  if (state->frameskip_type == 2 || state->frameskip_value == 0)
    return 1;

  period = state->frameskip_value + 1;
  if (state->frameskip_variation) {
    state->random_lfsr = state->random_lfsr * 1664525u + 1013904223u;
    return (state->random_lfsr % period) == 0;
  }

  return (frame % period) == 1;
}
