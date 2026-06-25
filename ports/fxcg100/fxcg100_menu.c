#include "fxcg100_platform.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LCD_W 384
#define LCD_H 216
#define MENU_TOP 28
#define MENU_ROW_H 10
#define MENU_VISIBLE_ROWS 15

#define RGB565(r, g, b) \
  (uint16_t)((((r) & 0x1f) << 11) | (((g) & 0x3f) << 5) | ((b) & 0x1f))

typedef enum menu_page_id {
  MENU_PAGE_MAIN = 0,
  MENU_PAGE_GRAPHICS,
  MENU_PAGE_GAMEPAD,
  MENU_PAGE_CHEATS,
  MENU_PAGE_COUNT
} menu_page_id;

typedef enum menu_value_id {
  MENU_VALUE_NONE = 0,
  MENU_VALUE_ROM_SOURCE,
  MENU_VALUE_SCALE,
  MENU_VALUE_FRAMESKIP_TYPE,
  MENU_VALUE_FRAMESKIP_VALUE,
  MENU_VALUE_FRAMESKIP_VARIATION,
  MENU_VALUE_SHOW_FPS,
  MENU_VALUE_SAVE_SLOT,
  MENU_VALUE_BACKUP_UPDATE
} menu_value_id;

typedef enum menu_item_kind {
  MENU_ITEM_ACTION = 0,
  MENU_ITEM_SUBMENU,
  MENU_ITEM_CHOICE,
  MENU_ITEM_NUMBER,
  MENU_ITEM_NUMBER_ACTION,
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
  MENU_ACTION_REMAP,
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

static const char * const scale_options[] = {
  "UNSCALED 3:2",
  "SCALED BLEND TODO",
  "FULL BLEND TODO",
  "SCALED FAST TODO"
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
  { "LOAD STATE FROM SLOT", MENU_ITEM_NUMBER_ACTION, MENU_ACTION_LOAD_STATE,
    MENU_PAGE_MAIN, MENU_VALUE_SAVE_SLOT, NULL, 10, NULL },
  { "SAVE STATE TO SLOT", MENU_ITEM_NUMBER_ACTION, MENU_ACTION_SAVE_STATE,
    MENU_PAGE_MAIN, MENU_VALUE_SAVE_SLOT, NULL, 10, NULL },
  { "CONFIGURE GAMEPAD INPUT", MENU_ITEM_SUBMENU, MENU_ACTION_NONE,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, NULL },
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
    MENU_PAGE_GRAPHICS, MENU_VALUE_SCALE, scale_options, 4, NULL },
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

static const menu_item gamepad_items[] = {
  { "D-PAD UP", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "UP" },
  { "D-PAD DOWN", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "DOWN" },
  { "D-PAD LEFT", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "LEFT" },
  { "D-PAD RIGHT", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "RIGHT" },
  { "A", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "EXE OR OK" },
  { "B", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "ALPHA OR AC" },
  { "LEFT TRIGGER", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "BEGIN" },
  { "RIGHT TRIGGER", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "END" },
  { "START", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "PGUP" },
  { "SELECT", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "HOME" },
  { "MENU", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "TOOLS" },
  { "FAST FORWARD", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "TODO" },
  { "LOAD STATE", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "TODO" },
  { "SAVE STATE", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "TODO" },
  { "SAVE+EXIT", MENU_ITEM_INFO, MENU_ACTION_REMAP,
    MENU_PAGE_GAMEPAD, MENU_VALUE_NONE, NULL, 0, "TODO" },
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
  case MENU_PAGE_GAMEPAD:
    *count = sizeof(gamepad_items) / sizeof(gamepad_items[0]);
    *title = "CONFIGURE GAMEPAD INPUT";
    return gamepad_items;
  case MENU_PAGE_CHEATS:
    *count = sizeof(cheats_items) / sizeof(cheats_items[0]);
    *title = "CHEATS AND MISC OPTIONS";
    return cheats_items;
  case MENU_PAGE_MAIN:
  default:
    *count = sizeof(main_items) / sizeof(main_items[0]);
    *title = "GPSP MAIN MENU";
    return main_items;
  }
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

static void menu_clamp_cursor(fxcg100_menu_state *state, menu_page_id page,
                              size_t count)
{
  uint32_t *selected = &state->selected[page];
  uint32_t *scroll = &state->scroll[page];

  if (count == 0) {
    *selected = 0;
    *scroll = 0;
    return;
  }

  if (*selected >= count)
    *selected = (uint32_t)count - 1;
  if (*selected < *scroll)
    *scroll = *selected;
  if (*selected >= *scroll + MENU_VISIBLE_ROWS)
    *scroll = *selected - MENU_VISIBLE_ROWS + 1;
  if (count <= MENU_VISIBLE_ROWS)
    *scroll = 0;
  else if (*scroll + MENU_VISIBLE_ROWS > count)
    *scroll = (uint32_t)count - MENU_VISIBLE_ROWS;
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
  char line[96];
  unsigned row;

  items = menu_page_items(page, &count, &title);
  menu_clamp_cursor(state, page, count);
  selected = state->selected[page];
  scroll = state->scroll[page];

  fxcg100_lcd_clear(bg);
  fxcg100_lcd_fill_rect(0, 0, LCD_W, 18, panel);
  fxcg100_lcd_draw_text(4, 5, "CGBA GPSP", text, panel);
  snprintf(line, sizeof(line), "F:%06u H:%08x",
           (unsigned)frame, (unsigned)last_hash);
  fxcg100_lcd_draw_text(246, 5, line, dim, panel);
  fxcg100_lcd_draw_text(8, 20, title, accent, bg);

  for (row = 0; row < MENU_VISIBLE_ROWS; row++) {
    uint32_t item_index = scroll + row;
    unsigned y = MENU_TOP + row * MENU_ROW_H;
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
  else
  fxcg100_lcd_draw_text(8, 184, "NSPIRE STYLE MENU OPTIONS", dim, bg);
  fxcg100_lcd_draw_text(8, 198, "UP/DOWN MOVE  LEFT/RIGHT CHANGE", dim, bg);
  fxcg100_lcd_draw_text(8, 208, "EXE SELECT  MENU/BACK RETURN", dim, bg);
  fxcg100_lcd_update();
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
  case MENU_ITEM_INFO:
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
  case MENU_ACTION_REMAP:
    *message = "REMAP PERSISTENCE TODO";
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
  state->rom_source = 1;
  state->screen_scale = 0;
  state->frameskip_type = 1;
  state->frameskip_value = 1;
  state->frameskip_variation = 0;
  state->backup_update = 0;
  state->random_lfsr = 0x00c6ba5eu;
}

fxcg100_menu_result fxcg100_menu_run(fxcg100_menu_state *state,
                                     uint32_t frame, uint32_t last_hash)
{
  menu_page_id page = MENU_PAGE_MAIN;
  const char *message = "";
  uint32_t previous = fxcg100_poll_app_keys();

  for (;;) {
    const menu_item *items;
    const char *title;
    size_t count;
    uint32_t selected;
    uint32_t keys;
    uint32_t edge;

    menu_draw(state, page, message, frame, last_hash);
    items = menu_page_items(page, &count, &title);
    (void)title;
    menu_clamp_cursor(state, page, count);
    selected = state->selected[page];

    keys = menu_wait_edge(&previous, &edge);

    if ((keys & (FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_HOME)) ==
        (FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_HOME))
      return FXCG100_MENU_QUIT;
    if ((keys & (FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_AC)) ==
        (FXCG100_APPKEY_SHIFT | FXCG100_APPKEY_AC))
      return FXCG100_MENU_RESET;

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
    } else if (edge & (FXCG100_APPKEY_MENU | FXCG100_APPKEY_BACK |
                       FXCG100_APPKEY_AC)) {
      if (page == MENU_PAGE_MAIN)
        return FXCG100_MENU_CONTINUE;
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
