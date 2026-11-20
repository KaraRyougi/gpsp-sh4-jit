#ifndef CGBA_FXCG100_ROM_SORT_H
#define CGBA_FXCG100_ROM_SORT_H

/*
 * Display-order sorting for the ROM library (Name, Last Played, Newly Added).
 *
 * The underlying rom_id indices stay stable so rom_source, the running game
 * badge, and main()'s current_rom never shift when the player switches sort
 * order. Callers map display position -> rom_id through order[].
 */

#include <stdint.h>

#include "fxcg100_platform.h"

#define FXCG100_ROM_SORT_MAX 64u

static inline const char *fxcg100_game_sort_label(uint32_t sort)
{
	switch (sort) {
	case FXCG100_GAME_SORT_LAST_PLAYED: return "Last Played";
	case FXCG100_GAME_SORT_NEWLY_ADDED: return "Newly Added";
	default:                            return "Name";
	}
}

static inline int fxcg100_rom_label_cmp(const char *a, const char *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";
	while (*a && *b) {
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;

		if (ca >= 'a' && ca <= 'z')
			ca = (unsigned char)(ca - 'a' + 'A');
		if (cb >= 'a' && cb <= 'z')
			cb = (unsigned char)(cb - 'a' + 'A');
		if (ca != cb)
			return (int)ca - (int)cb;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline int fxcg100_rom_sort_before(uint32_t a, uint32_t b, uint32_t sort)
{
	if (sort == FXCG100_GAME_SORT_LAST_PLAYED) {
		uint32_t pa = fxcg100_rom_source_last_played(a);
		uint32_t pb = fxcg100_rom_source_last_played(b);

		if (pa != pb)
			return pa > pb;
	} else if (sort == FXCG100_GAME_SORT_NEWLY_ADDED) {
		uint32_t aa = fxcg100_rom_source_added_order(a);
		uint32_t ab = fxcg100_rom_source_added_order(b);

		if (aa != ab)
			return aa > ab;
	}

	int cmp = fxcg100_rom_label_cmp(fxcg100_rom_source_label(a),
		fxcg100_rom_source_label(b));
	if (cmp != 0)
		return cmp < 0;
	return a < b;
}

static inline uint32_t fxcg100_rom_sort_order(uint32_t *order, uint32_t cap,
	uint32_t sort)
{
	uint32_t count = fxcg100_rom_source_count();
	uint32_t i;

	if (!order || cap == 0)
		return 0;
	if (count > cap)
		count = cap;
	for (i = 0; i < count; i++)
		order[i] = i;
	for (i = 1; i < count; i++) {
		uint32_t id = order[i];
		uint32_t j = i;

		while (j > 0 && fxcg100_rom_sort_before(id, order[j - 1], sort)) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = id;
	}
	return count;
}

static inline uint32_t fxcg100_rom_sort_find(const uint32_t *order,
	uint32_t count, uint32_t rom_id)
{
	uint32_t i;

	for (i = 0; i < count; i++)
		if (order[i] == rom_id)
			return i;
	return 0;
}

#endif /* CGBA_FXCG100_ROM_SORT_H */
