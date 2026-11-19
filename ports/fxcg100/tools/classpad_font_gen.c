/* Rasterises TrueType / OpenType fonts into the 4-bit-per-pixel glyph
 * tables of the calculator interfaces: ports/fxcg100/classpad_font.h
 * (fx-CP400) and ports/fxcg100/cg_font.h (fx-CG50 / fx-CG100).
 *
 *   cc classpad_font_gen.c $(pkg-config --cflags --libs freetype2) -o gen
 *   ./gen [-p PREFIX] NAME SIZE FONT[,FACE[,WGHT]] ... > header.h
 *
 * Each table is named PREFIX_font_NAME (PREFIX is "cp" unless given) and
 * covers ASCII 32..126. FACE picks a face in a collection (.ttc); WGHT sets
 * the wght axis of a variable font. Glyphs are rendered with
 * FT_LOAD_TARGET_LIGHT. A glyph is stored as its tight bitmap, two pixels to
 * a byte (first pixel in the top four bits), rows padded to a byte, coverage
 * quantised to 0..15. The table layout is in classpad_font_types.h.
 *
 * classpad_font.h was generated from Source Sans 3 (SIL Open Font License
 * 1.1, see ports/fxcg100/fonts/; the TTFs are at
 * github.com/adobe-fonts/source-sans, release branch, TTF/):
 *
 *   ./gen small 15 SourceSans3-Medium.ttf body 18 SourceSans3-Regular.ttf \
 *         title 24 SourceSans3-Semibold.ttf
 *
 * The small size uses the Medium weight: at 15 px a Regular stem is barely
 * over a pixel wide and comes out uneven.
 *
 * cg_font.h is one weight heavier throughout (a 16 px Regular stem is about
 * 1.5 px and looks faint on the dark schemes). It was generated from the
 * Latin glyphs of the Source Han Sans variable font, which are Source Sans:
 *
 *   ./gen -p cg title 16 SourceHanSans-VF.otf.ttc,0,700 \
 *         body 16 SourceHanSans-VF.otf.ttc,0,500 \
 *         small 14 SourceHanSans-VF.otf.ttc,0,600 \
 *         key 11 SourceHanSans-VF.otf.ttc,0,700 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST 32
#define LAST 126

static const char *prefix = "cp";

/* FONT[,FACE[,WGHT]]: open the face and set its weight. */
static FT_Face open_face(FT_Library lib, const char *spec)
{
	char path[1024];
	const char *comma = strchr(spec, ',');
	long index = 0, wght = 0;
	FT_Face face;

	snprintf(path, sizeof(path), "%.*s",
		comma ? (int)(comma - spec) : (int)strlen(spec), spec);
	if (comma) {
		index = strtol(comma + 1, NULL, 10);
		comma = strchr(comma + 1, ',');
		if (comma)
			wght = strtol(comma + 1, NULL, 10);
	}
	if (FT_New_Face(lib, path, index, &face)) {
		fprintf(stderr, "cannot load %s\n", path);
		exit(1);
	}
	if (wght) {
		FT_MM_Var *mm;
		FT_Fixed coords[16];
		unsigned a;

		if (!FT_HAS_MULTIPLE_MASTERS(face) || FT_Get_MM_Var(face, &mm)) {
			fprintf(stderr, "%s has no weight axis\n", path);
			exit(1);
		}
		for (a = 0; a < mm->num_axis && a < 16; a++) {
			coords[a] = mm->axis[a].def;
			if (mm->axis[a].tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
				coords[a] = (FT_Fixed)(wght << 16);
		}
		FT_Set_Var_Design_Coordinates(face, a, coords);
		FT_Done_MM_Var(lib, mm);
	}
	return face;
}

static void emit_font(FT_Library lib, const char *name, int size,
	const char *spec)
{
	static unsigned char data[1 << 18];
	struct { int off, w, h, left, top, adv; } g[LAST - FIRST + 1];
	FT_Face face = open_face(lib, spec);
	int used = 0, c, ascent, height;

	if (FT_Set_Pixel_Sizes(face, 0, size)) {
		fprintf(stderr, "cannot set %d px for %s\n", size, spec);
		exit(1);
	}
	ascent = (int)((face->size->metrics.ascender + 32) >> 6);
	height = (int)((face->size->metrics.height + 32) >> 6);

	for (c = FIRST; c <= LAST; c++) {
		FT_GlyphSlot slot;
		int x, y;

		if (FT_Load_Char(face, (FT_ULong)c,
				FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) {
			fprintf(stderr, "no glyph for %d in %s\n", c, spec);
			exit(1);
		}
		slot = face->glyph;
		g[c - FIRST].off = used;
		g[c - FIRST].w = (int)slot->bitmap.width;
		g[c - FIRST].h = (int)slot->bitmap.rows;
		g[c - FIRST].left = slot->bitmap_left;
		g[c - FIRST].top = ascent - slot->bitmap_top;
		g[c - FIRST].adv = (int)((slot->advance.x + 32) >> 6);
		for (y = 0; y < (int)slot->bitmap.rows; y++) {
			int bytes = ((int)slot->bitmap.width + 1) / 2;

			memset(data + used, 0, (size_t)bytes);
			for (x = 0; x < (int)slot->bitmap.width; x++) {
				int v = slot->bitmap.buffer[y * slot->bitmap.pitch + x];
				int q = (v * 15 + 127) / 255;

				data[used + x / 2] |= (unsigned char)(q << (4 - 4 * (x % 2)));
			}
			used += bytes;
		}
	}

	printf("static const uint8_t %s_font_%s_data[%d] = {", prefix, name, used);
	for (c = 0; c < used; c++)
		printf("%s%u,", c % 20 ? "" : "\n\t", data[c]);
	printf("\n};\n\n");
	printf("static const cp_glyph %s_font_%s_glyphs[%d] = {\n", prefix, name,
		LAST - FIRST + 1);
	for (c = FIRST; c <= LAST; c++)
		printf("\t{ %d, %d, %d, %d, %d, %d },\n", g[c - FIRST].off,
			g[c - FIRST].w, g[c - FIRST].h, g[c - FIRST].left,
			g[c - FIRST].top, g[c - FIRST].adv);
	printf("};\n\n");
	printf("static const cp_font %s_font_%s = {\n"
		"\t%s_font_%s_glyphs, %s_font_%s_data, %d, %d\n};\n\n",
		prefix, name, prefix, name, prefix, name, height, ascent);
	FT_Done_Face(face);
}

int main(int argc, char **argv)
{
	FT_Library lib;
	char guard[64];
	int i, first = 1;

	if (argc > 2 && !strcmp(argv[1], "-p")) {
		prefix = argv[2];
		first = 3;
	}
	if (argc - first < 3 || (argc - first) % 3 || FT_Init_FreeType(&lib)) {
		fprintf(stderr, "usage: %s [-p PREFIX] NAME SIZE FONT[,FACE[,WGHT]] ...\n",
			argv[0]);
		return 1;
	}
	if (!strcmp(prefix, "cp"))
		snprintf(guard, sizeof(guard), "CGBA_CLASSPAD_FONT_H");
	else {
		char *g;

		snprintf(guard, sizeof(guard), "CGBA_%s_FONT_H", prefix);
		for (g = guard; *g; g++)
			if (*g >= 'a' && *g <= 'z')
				*g = (char)(*g - 'a' + 'A');
	}
	printf("#ifndef %s\n#define %s\n\n"
		"/* Generated by ports/fxcg100/tools/classpad_font_gen.c - do not edit.\n"
		" * See that file for the command line and the fonts used (SIL Open\n"
		" * Font License 1.1; the licence is in ports/fxcg100/fonts/OFL.txt).\n"
		" *\n", guard, guard);
	for (i = first; i + 2 < argc; i += 3) {
		const char *base = strrchr(argv[i + 2], '/');

		printf(" *   %s_font_%s: %s px, %s\n", prefix, argv[i], argv[i + 1],
			base ? base + 1 : argv[i + 2]);
	}
	printf(" *\n"
		" * ASCII %d..%d, 4 bits of coverage per pixel (classpad_font_types.h). */\n\n"
		"#include \"classpad_font_types.h\"\n\n", FIRST, LAST);
	for (i = first; i + 2 < argc; i += 3)
		emit_font(lib, argv[i], atoi(argv[i + 1]), argv[i + 2]);
	printf("#endif /* %s */\n", guard);
	return 0;
}
