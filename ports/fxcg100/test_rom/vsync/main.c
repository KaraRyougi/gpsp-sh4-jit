/*
 * Vsync / tearing test ROM.
 *
 * Every GBA frame looks different from the one before, so wherever the
 * calculator's panel shows parts of two frames at once there is a visible
 * edge:
 *
 *   FLASH   the whole screen changes colour every frame (11 colours, so two
 *           frames drawn with any frameskip up to 9 never share one; B swaps
 *           to black/white, which shows how frameskip aliases). A tear is a
 *           horizontal boundary between two colours, at the row it happened.
 *   SCROLL  numbered vertical bars move sideways by SPEED pixels a frame.
 *           A tear breaks every bar along one row; how far the halves are
 *           apart, read off the numbers, is how many frames lie between them.
 *
 * The scale on the right is the GBA row (0..143) of each label's top edge.
 * The bottom bar shows the mode and a frame counter (hex, one per GBA frame)
 * that a slow-motion video can read.
 *
 * Everything that changes is a register or palette write made right after
 * VBlank (no per-pixel drawing), so the ROM costs the emulator almost
 * nothing and does not slow the measurement down.
 *
 * Keys: A mode, B pattern (FLASH), Up/Down speed, Left/Right direction
 * (SCROLL), Start freezes the picture (the counter keeps running).
 */

#include <stdint.h>

typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;

#define REG_DISPCNT  (*(vu16 *)0x04000000)
#define REG_DISPSTAT (*(vu16 *)0x04000004)
#define REG_VCOUNT   (*(vu16 *)0x04000006)
#define REG_BG0CNT   (*(vu16 *)0x04000008)
#define REG_BG1CNT   (*(vu16 *)0x0400000A)
#define REG_BG0HOFS  (*(vu16 *)0x04000010)
#define REG_BG0VOFS  (*(vu16 *)0x04000012)
#define REG_BG1HOFS  (*(vu16 *)0x04000014)
#define REG_BG1VOFS  (*(vu16 *)0x04000016)
#define REG_KEYINPUT (*(vu16 *)0x04000130)
#define REG_IE       (*(vu16 *)0x04000200)
#define REG_IME      (*(vu16 *)0x04000208)

#define PAL_BG   ((vu16 *)0x05000000)
#define TILES    ((vu32 *)0x06000000)          /* char block 0 */
#define MAP(sb)  ((vu16 *)(0x06000000 + (sb) * 0x800))

#define SB_TEXT   28                           /* BG0, 32x32 */
#define SB_BARS   30                           /* BG1, 64x32: 30 and 31 */

#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080

#define RGB(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))
#define WHITE RGB(31, 31, 31)
#define BLACK RGB(0, 0, 0)

#define ROWS 20                                /* text rows on screen */
#define COLS 30

/* 5x7 glyphs, bit 4 = leftmost column. */
static const char font_chars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-/%.+<>";
static const uint8_t font[][7] = {
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
	{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
	{0x1F,0x10,0x10,0x1E,0x01,0x11,0x0E}, /* 5 */
	{0x0E,0x11,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
	{0x0E,0x11,0x11,0x0F,0x01,0x11,0x0E}, /* 9 */
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
	{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
	{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
	{0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* N */
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
	{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
	{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
	{0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, /* Y */
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
	{0x00,0x04,0x00,0x00,0x04,0x00,0x00}, /* : */
	{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
	{0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
	{0x19,0x1A,0x04,0x0B,0x13,0x03,0x00}, /* % */
	{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
	{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
	{0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
	{0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
};

/* FLASH colours: 11 of them, a prime above the largest frame period
 * (frameskip 9 draws every 10th frame), so consecutive drawn frames always
 * differ. Ordered so that neighbours contrast. */
#define FLASH_N 11
static const uint16_t flash[FLASH_N] = {
	RGB(31, 31, 31), RGB(31, 0, 0), RGB(0, 31, 0), RGB(0, 0, 31),
	RGB(31, 31, 0), RGB(31, 0, 31), RGB(0, 31, 31), RGB(31, 16, 0),
	RGB(12, 0, 24), RGB(16, 16, 16), RGB(0, 0, 0),
};

/* SCROLL: column groups of 16 bars, each group its own accent, so the
 * 512 px of bars never repeat within one jump. Dark accents go on white
 * bars, bright ones on black bars. */
static const uint16_t accent_dark[4] = {
	RGB(22, 0, 0), RGB(0, 15, 0), RGB(0, 0, 24), RGB(22, 9, 0),
};
static const uint16_t accent_bright[4] = {
	RGB(31, 10, 10), RGB(10, 31, 10), RGB(12, 16, 31), RGB(31, 22, 4),
};

/* Wait for the start of the next vertical blank by watching VCOUNT, with
 * interrupts off and no BIOS call. VBlankIntrWait (SWI 5) would be the
 * usual way, but under gpSP's SH4 recompiler on the calculator it came back
 * only every eighth frame for this ROM, so the picture changed 7.5 times a
 * second instead of 60 and most tears could not be seen. The short pause
 * between looks (about a sixth of a scanline) keeps the emulator's work
 * per frame small. */
static void vblank_wait(void)
{
	unsigned i;

	while(REG_VCOUNT >= 160)
		for(i = 0; i < 40; i++)
			__asm__ volatile("");
	while(REG_VCOUNT < 160)
		for(i = 0; i < 40; i++)
			__asm__ volatile("");
}

/* Glyph pixels use colour 1, the rest of the 8x8 cell colour 2, so a text
 * cell is opaque with its palette bank's background. */
static void load_font(void)
{
	unsigned i, r, c;

	for(i = 0; i < 8; i++)
		TILES[i] = 0;                          /* tile 0: transparent */
	for(i = 0; font_chars[i]; i++) {
		vu32 *t = TILES + (unsigned)(uint8_t)font_chars[i] * 8;

		for(r = 0; r < 8; r++) {
			uint32_t bits = r < 7 ? font[i][r] : 0, word = 0;

			for(c = 0; c < 8; c++) {
				uint32_t on = c >= 1 && c <= 5 && (bits & (0x10u >> (c - 1)));

				word |= (on ? 1u : 2u) << (4 * c);
			}
			t[r] = word;
		}
	}
}

static uint16_t cell(char ch, unsigned bank)
{
	unsigned i;

	for(i = 0; font_chars[i]; i++)
		if(font_chars[i] == ch)
			return (uint16_t)((uint8_t)ch | (bank << 12));
	return (uint16_t)(' ' | (bank << 12));
}

static void text(unsigned row, unsigned col, const char *s)
{
	vu16 *m = MAP(SB_TEXT) + row * 32;

	for(; *s && col < COLS; s++, col++)
		m[col] = cell(*s, 0);
}

static void hex4(char *out, uint32_t v)
{
	static const char digits[] = "0123456789ABCDEF";
	int i;

	for(i = 3; i >= 0; i--, v >>= 4)
		out[i] = digits[v & 15];
}

static void setup(void)
{
	unsigned r, c, g;

	REG_DISPCNT = 0x0080;                      /* forced blank while loading */

	PAL_BG[0] = BLACK;
	PAL_BG[1] = WHITE;                         /* bank 0: text */
	PAL_BG[2] = BLACK;
	for(g = 0; g < 4; g++) {
		vu16 *light = PAL_BG + 16 * (1 + g * 2);
		vu16 *dark = PAL_BG + 16 * (2 + g * 2);

		light[1] = accent_dark[g];
		light[2] = WHITE;
		dark[1] = accent_bright[g];
		dark[2] = BLACK;
	}
	load_font();

	for(r = 0; r < 32 * 32; r++)
		MAP(SB_TEXT)[r] = 0;
	/* Bars: 64 columns of 8 px, alternately white and black, every fourth
	 * row labelled with the column's number (hex digit, group colour). */
	for(r = 0; r < 32; r++)
		for(c = 0; c < 64; c++) {
			unsigned bank = 1 + ((c >> 4) & 3) * 2 + (c & 1);
			char ch = (r & 3) == 1 ? "0123456789ABCDEF"[c & 15] : ' ';

			MAP(SB_BARS + (c >> 5))[r * 32 + (c & 31)] = cell(ch, bank);
		}

	/* Row scale on the right: the GBA row of each label's top edge. */
	text(0, 27, "  0");  text(2, 27, " 16");  text(4, 27, " 32");
	text(6, 27, " 48");  text(8, 27, " 64");  text(10, 27, " 80");
	text(12, 27, " 96"); text(14, 27, "112"); text(16, 27, "128");
	for(c = 0; c < COLS; c++) {
		MAP(SB_TEXT)[18 * 32 + c] = cell(' ', 0);
		MAP(SB_TEXT)[19 * 32 + c] = cell(' ', 0);
	}
	text(19, 0, "A MODE  B PAT  UD SPD  ST STOP");

	REG_BG0CNT = (uint16_t)(SB_TEXT << 8);                    /* priority 0 */
	REG_BG1CNT = (uint16_t)(1 | (SB_BARS << 8) | (1 << 14));  /* 512x256 */
	REG_BG0HOFS = 0;
	REG_BG0VOFS = 0;
	REG_BG1HOFS = 0;
	REG_BG1VOFS = 0;

	REG_DISPSTAT = 0;                          /* no interrupts: see vblank_wait */
	REG_IE = 0;
	REG_IME = 0;
}

static void status(int mode, int bw, unsigned speed, int dir, int paused)
{
	char line[24];
	unsigned i;

	for(i = 0; i < sizeof(line) - 1; i++)
		line[i] = ' ';
	line[sizeof(line) - 1] = 0;
	if(mode == 0) {
		const char *s = bw ? "FLASH  B/W" : "FLASH  COLOURS";

		for(i = 0; s[i]; i++)
			line[i] = s[i];
	} else {
		const char *s = "SCROLL  SPD";

		for(i = 0; s[i]; i++)
			line[i] = s[i];
		line[12] = speed >= 10 ? '1' : ' ';
		line[13] = (char)('0' + (speed >= 10 ? speed - 10 : speed));
		line[14] = 'P';
		line[15] = 'X';
		line[17] = dir > 0 ? '>' : '<';
	}
	if(paused) {
		line[19] = 'S'; line[20] = 'T'; line[21] = 'O'; line[22] = 'P';
	}
	text(18, 0, line);
}

int main(void)
{
	uint32_t frame = 0;
	unsigned colour = 0, speed = 4, prev = 0;
	int mode = 0, bw = 0, dir = 1, paused = 0, phase = 0, pos = 0;
	char counter[7] = "F 0000";

	setup();
	status(mode, bw, speed, dir, paused);
	REG_DISPCNT = 0x0100;                      /* mode 0, BG0 */

	for(;;) {
		unsigned keys, edge;

		vblank_wait();
		keys = ~REG_KEYINPUT & 0x03FFu;
		edge = keys & ~prev;
		prev = keys;

		if(edge & KEY_A)
			mode ^= 1;
		if(edge & KEY_B)
			bw ^= 1;
		if((edge & KEY_UP) && speed < 16)
			speed++;
		if((edge & KEY_DOWN) && speed > 1)
			speed--;
		if(edge & KEY_LEFT)
			dir = -1;
		if(edge & KEY_RIGHT)
			dir = 1;
		if(edge & KEY_START)
			paused ^= 1;

		if(!paused) {
			phase ^= 1;
			if(++colour == FLASH_N)
				colour = 0;
			pos += dir * (int)speed;
		}
		if(mode == 0) {
			REG_DISPCNT = 0x0100;
			PAL_BG[0] = bw ? (phase ? WHITE : BLACK) : flash[colour];
		} else {
			REG_DISPCNT = 0x0300;
			PAL_BG[0] = BLACK;
			REG_BG1HOFS = (uint16_t)(pos & 511);
		}

		frame++;
		hex4(counter + 2, frame);
		text(18, 24, counter);
		if(edge)
			status(mode, bw, speed, dir, paused);
	}
}
