#include "fxcg100_platform.h"

#define LCD_PRDR (*(volatile uint8_t *)0xa405013c)
#define LCD_DATA (*(volatile uint16_t *)0xb4000000)

#define LCD_REG_DATA       0x202
#define LCD_VISIBLE_WIDTH 396
#define LCD_VISIBLE_HEIGHT 224
#define LCD_PANEL_BORDER_X 0
#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define LCD_STATUS_HEIGHT 16

#define RGB565(r, g, b) \
  (uint16_t)((((r) & 0x1f) << 11) | (((g) & 0x3f) << 5) | ((b) & 0x1f))

static void lcd_sync(void)
{
  __asm__ volatile("synco" ::: "memory");
}

static void lcd_index(uint16_t reg)
{
  LCD_PRDR &= (uint8_t)~0x10;
  lcd_sync();
  LCD_DATA = reg;
  lcd_sync();
  LCD_PRDR |= 0x10;
  lcd_sync();
}

static void lcd_data(uint16_t value)
{
  LCD_PRDR |= 0x10;
  lcd_sync();
  LCD_DATA = value;
  lcd_sync();
}

static void lcd_write(uint16_t reg, uint16_t value)
{
  lcd_index(reg);
  lcd_data(value);
}

static void lcd_set_window(unsigned x, unsigned y, unsigned w, unsigned h)
{
  unsigned panel_start = x + LCD_PANEL_BORDER_X;
  unsigned panel_end = panel_start + w - 1;

  lcd_write(0x210, (uint16_t)(395 - panel_end));
  lcd_write(0x211, (uint16_t)(395 - panel_start));
  lcd_write(0x212, (uint16_t)y);
  lcd_write(0x213, (uint16_t)(y + h - 1));
  lcd_write(0x200, 0);
  lcd_write(0x201, 0);
  lcd_index(LCD_REG_DATA);
}

static uint64_t glyph5(char ch)
{
#define GLYPH(a,b,c,d,e,f,g) \
  ((((uint64_t)(a)) << 30) | (((uint64_t)(b)) << 25) | \
   (((uint64_t)(c)) << 20) | (((uint64_t)(d)) << 15) | \
   (((uint64_t)(e)) << 10) | (((uint64_t)(f)) << 5) | (uint64_t)(g))
  if (ch >= 'a' && ch <= 'z')
    ch = (char)(ch - ('a' - 'A'));

  switch (ch) {
  case 'A': return GLYPH(0x0e,0x11,0x11,0x1f,0x11,0x11,0x11);
  case 'B': return GLYPH(0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e);
  case 'C': return GLYPH(0x0e,0x11,0x10,0x10,0x10,0x11,0x0e);
  case 'D': return GLYPH(0x1e,0x11,0x11,0x11,0x11,0x11,0x1e);
  case 'E': return GLYPH(0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f);
  case 'F': return GLYPH(0x1f,0x10,0x10,0x1e,0x10,0x10,0x10);
  case 'G': return GLYPH(0x0e,0x11,0x10,0x17,0x11,0x11,0x0f);
  case 'H': return GLYPH(0x11,0x11,0x11,0x1f,0x11,0x11,0x11);
  case 'I': return GLYPH(0x0e,0x04,0x04,0x04,0x04,0x04,0x0e);
  case 'J': return GLYPH(0x01,0x01,0x01,0x01,0x11,0x11,0x0e);
  case 'K': return GLYPH(0x11,0x12,0x14,0x18,0x14,0x12,0x11);
  case 'L': return GLYPH(0x10,0x10,0x10,0x10,0x10,0x10,0x1f);
  case 'M': return GLYPH(0x11,0x1b,0x15,0x15,0x11,0x11,0x11);
  case 'N': return GLYPH(0x11,0x19,0x15,0x13,0x11,0x11,0x11);
  case 'O': return GLYPH(0x0e,0x11,0x11,0x11,0x11,0x11,0x0e);
  case 'P': return GLYPH(0x1e,0x11,0x11,0x1e,0x10,0x10,0x10);
  case 'Q': return GLYPH(0x0e,0x11,0x11,0x11,0x15,0x12,0x0d);
  case 'R': return GLYPH(0x1e,0x11,0x11,0x1e,0x14,0x12,0x11);
  case 'S': return GLYPH(0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e);
  case 'T': return GLYPH(0x1f,0x04,0x04,0x04,0x04,0x04,0x04);
  case 'U': return GLYPH(0x11,0x11,0x11,0x11,0x11,0x11,0x0e);
  case 'V': return GLYPH(0x11,0x11,0x11,0x11,0x11,0x0a,0x04);
  case 'W': return GLYPH(0x11,0x11,0x11,0x15,0x15,0x15,0x0a);
  case 'X': return GLYPH(0x11,0x11,0x0a,0x04,0x0a,0x11,0x11);
  case 'Y': return GLYPH(0x11,0x11,0x0a,0x04,0x04,0x04,0x04);
  case 'Z': return GLYPH(0x1f,0x01,0x02,0x04,0x08,0x10,0x1f);
  case '0': return GLYPH(0x0e,0x11,0x13,0x15,0x19,0x11,0x0e);
  case '1': return GLYPH(0x04,0x0c,0x04,0x04,0x04,0x04,0x0e);
  case '2': return GLYPH(0x0e,0x11,0x01,0x02,0x04,0x08,0x1f);
  case '3': return GLYPH(0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e);
  case '4': return GLYPH(0x02,0x06,0x0a,0x12,0x1f,0x02,0x02);
  case '5': return GLYPH(0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e);
  case '6': return GLYPH(0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e);
  case '7': return GLYPH(0x1f,0x01,0x02,0x04,0x08,0x08,0x08);
  case '8': return GLYPH(0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e);
  case '9': return GLYPH(0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e);
  case ':': return GLYPH(0x00,0x04,0x04,0x00,0x04,0x04,0x00);
  case '.': return GLYPH(0x00,0x00,0x00,0x00,0x00,0x0c,0x0c);
  case '-': return GLYPH(0x00,0x00,0x00,0x1f,0x00,0x00,0x00);
  case '+': return GLYPH(0x00,0x04,0x04,0x1f,0x04,0x04,0x00);
  case '/': return GLYPH(0x01,0x01,0x02,0x04,0x08,0x10,0x10);
  default:  return GLYPH(0x00,0x00,0x00,0x00,0x00,0x00,0x00);
  }
#undef GLYPH
}

static void lcd_draw_char(unsigned x, unsigned y, char ch,
                          uint16_t fg, uint16_t bg)
{
  uint64_t bits = glyph5(ch);
  unsigned row;

  lcd_set_window(x, y, 6, 8);
  for (row = 0; row < 8; row++) {
    unsigned col;
    uint8_t line = row < 7 ? (uint8_t)((bits >> ((6 - row) * 5)) & 0x1f) : 0;

    for (col = 0; col < 6; col++) {
      if (col < 5 && (line & (uint8_t)(1u << (4 - col))))
        lcd_data(fg);
      else
        lcd_data(bg);
    }
  }
}

void fxcg100_lcd_init(void)
{
  lcd_set_window(0, 0, LCD_VISIBLE_WIDTH, LCD_VISIBLE_HEIGHT);
}

void fxcg100_lcd_shutdown(void)
{
  lcd_set_window(0, 0, LCD_VISIBLE_WIDTH, LCD_VISIBLE_HEIGHT);
}

void fxcg100_lcd_fill_rect(unsigned x, unsigned y, unsigned w, unsigned h,
                           uint16_t color)
{
  unsigned i;

  if (x >= LCD_VISIBLE_WIDTH || y >= LCD_VISIBLE_HEIGHT || w == 0 || h == 0)
    return;
  if (x + w > LCD_VISIBLE_WIDTH)
    w = LCD_VISIBLE_WIDTH - x;
  if (y + h > LCD_VISIBLE_HEIGHT)
    h = LCD_VISIBLE_HEIGHT - y;

  lcd_set_window(x, y, w, h);
  for (i = 0; i < w * h; i++)
    lcd_data(color);
}

void fxcg100_lcd_clear(uint16_t color)
{
  fxcg100_lcd_fill_rect(0, 0, LCD_VISIBLE_WIDTH, LCD_VISIBLE_HEIGHT, color);
}

void fxcg100_lcd_draw_text(unsigned x, unsigned y, const char *text,
                           uint16_t fg, uint16_t bg)
{
  while (*text && x + 6 <= LCD_VISIBLE_WIDTH) {
    lcd_draw_char(x, y, *text++, fg, bg);
    x += 6;
  }
}

void fxcg100_lcd_update(void)
{
}

void fxcg100_lcd_status(const char *text)
{
  uint16_t bg = RGB565(1, 3, 5);
  uint16_t fg = RGB565(30, 58, 31);

  fxcg100_lcd_fill_rect(0, 0, LCD_VISIBLE_WIDTH, LCD_STATUS_HEIGHT, bg);
  fxcg100_lcd_draw_text(4, 4, text, fg, bg);
}

void fxcg100_lcd_set_scale(uint32_t mode)
{
  (void)mode;                     /* PIO presenter: 1:1 only */
}

void fxcg100_lcd_blit_gba(const uint16_t *pixels)
{
  unsigned x = (LCD_VISIBLE_WIDTH - GBA_WIDTH) / 2;
  unsigned y = (LCD_VISIBLE_HEIGHT - GBA_HEIGHT) / 2;
  unsigned i;

  lcd_set_window(x, y, GBA_WIDTH, GBA_HEIGHT);
  for (i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++)
    lcd_data(pixels[i]);
}

uint32_t fxcg100_frame_hash(const uint16_t *pixels)
{
  uint32_t hash = 2166136261u;
  unsigned i;

  for (i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++) {
    uint16_t px = pixels[i];
    hash ^= (uint8_t)(px >> 8);
    hash *= 16777619u;
    hash ^= (uint8_t)px;
    hash *= 16777619u;
  }

  return hash;
}
