#ifndef CGBA_GINT_GPSP_ROM_META_H
#define CGBA_GINT_GPSP_ROM_META_H

/*
 * Persistent library metadata (\\fls0\\CGBALIB.DAT): tracks per-ROM
 * last_played and first_seen (newly added) sequence numbers across reboots.
 *
 * Fixed-size blob (1040 bytes, well inside one 4 KiB Fugue block) so repeat
 * saves hit fxcg100_storage_write_blob()'s fast in-place overwrite path.
 */

#include <stdint.h>
#include <string.h>

#include "nor_rom.h"

#define CGBA_ROM_META_MAGIC   0x43474C42u /* "CGLB" */
#define CGBA_ROM_META_VERSION 1u
#define CGBA_ROM_META_MAX     64u

typedef struct cgba_rom_meta_entry {
	uint32_t hash_lo;
	uint32_t hash_hi;
	uint32_t last_played;
	uint32_t first_seen;
} cgba_rom_meta_entry;

typedef struct cgba_rom_meta_file {
	uint32_t magic;
	uint16_t version;
	uint16_t count;
	uint32_t play_seq;
	uint32_t seen_seq;
	cgba_rom_meta_entry entries[CGBA_ROM_META_MAX];
} cgba_rom_meta_file;

static inline void cgba_rom_meta_hash(const char *label,
	uint32_t *hash_lo, uint32_t *hash_hi)
{
	uint64_t h = 14695981039346656037ULL;

	if (label) {
		for (const unsigned char *p = (const unsigned char *)label; *p; p++) {
			unsigned char c = *p;
			if (c >= 'a' && c <= 'z')
				c = (unsigned char)(c - 'a' + 'A');
			h ^= (uint64_t)c;
			h *= 1099511628211ULL;
		}
	}
	*hash_lo = (uint32_t)h;
	*hash_hi = (uint32_t)(h >> 32);
}

static inline void cgba_rom_meta_reset(cgba_rom_meta_file *meta)
{
	memset(meta, 0, sizeof(*meta));
	meta->magic = CGBA_ROM_META_MAGIC;
	meta->version = CGBA_ROM_META_VERSION;
}

static inline int cgba_rom_meta_valid(const cgba_rom_meta_file *meta)
{
	return meta &&
		meta->magic == CGBA_ROM_META_MAGIC &&
		meta->version == CGBA_ROM_META_VERSION &&
		meta->count <= CGBA_ROM_META_MAX;
}

static inline int cgba_rom_meta_find(const cgba_rom_meta_file *meta,
	uint32_t hash_lo, uint32_t hash_hi)
{
	for (uint32_t i = 0; i < meta->count; i++) {
		if (meta->entries[i].hash_lo == hash_lo &&
				meta->entries[i].hash_hi == hash_hi)
			return (int)i;
	}
	return -1;
}

static inline int cgba_rom_meta_present_in_list(const cgba_nor_rom_list *roms,
	uint32_t hash_lo, uint32_t hash_hi)
{
	if (!roms)
		return 0;
	for (uint32_t i = 0; i < roms->count; i++) {
		uint32_t lo, hi;
		cgba_rom_meta_hash(roms->entries[i].label, &lo, &hi);
		if (lo == hash_lo && hi == hash_hi)
			return 1;
	}
	return 0;
}

/* Allocate a slot in meta->entries[], evicting a stale (missing) entry first
 * when full, or the oldest entry if all slots are still present on storage. */
static inline cgba_rom_meta_entry *cgba_rom_meta_alloc_slot(
	cgba_rom_meta_file *meta, const cgba_nor_rom_list *roms)
{
	if (meta->count < CGBA_ROM_META_MAX)
		return &meta->entries[meta->count++];

	uint32_t victim = 0;
	int victim_missing = -1;
	for (uint32_t i = 0; i < meta->count; i++) {
		int missing = !cgba_rom_meta_present_in_list(roms,
			meta->entries[i].hash_lo, meta->entries[i].hash_hi);
		if (missing > victim_missing) {
			victim = i;
			victim_missing = missing;
		} else if (missing == victim_missing) {
			const cgba_rom_meta_entry *a = &meta->entries[i];
			const cgba_rom_meta_entry *b = &meta->entries[victim];
			if (a->last_played < b->last_played ||
					(a->last_played == b->last_played &&
					 a->first_seen < b->first_seen))
				victim = i;
		}
	}
	return &meta->entries[victim];
}

/* Reconcile scanned storage ROMs against `meta`, populating each ROM entry's
 * last_played and added_order. Newly discovered ROMs receive monotonic
 * first_seen numbers ordered by their directory scan_order (earliest first,
 * newest last). Returns 1 if `meta` was modified and should be persisted. */
static inline int cgba_rom_meta_reconcile(cgba_rom_meta_file *meta,
	cgba_nor_rom_list *roms)
{
	uint32_t new_ids[CGBA_NOR_ROM_MAX_ENTRIES];
	uint32_t new_count = 0;
	int dirty = 0;

	if (!meta || !roms)
		return 0;

	if (!cgba_rom_meta_valid(meta)) {
		cgba_rom_meta_reset(meta);
		if (roms->count > 0)
			dirty = 1;
	}

	for (uint32_t i = 0; i < roms->count; i++) {
		uint32_t lo, hi;
		int idx;

		cgba_rom_meta_hash(roms->entries[i].label, &lo, &hi);
		idx = cgba_rom_meta_find(meta, lo, hi);
		if (idx >= 0) {
			cgba_rom_meta_entry *m = &meta->entries[idx];
			if (m->first_seen == 0) {
				m->first_seen = ++meta->seen_seq;
				dirty = 1;
			}
			if (m->last_played > meta->play_seq)
				meta->play_seq = m->last_played;
			if (m->first_seen > meta->seen_seq)
				meta->seen_seq = m->first_seen;
			roms->entries[i].last_played = m->last_played;
			roms->entries[i].added_order = m->first_seen;
		} else if (new_count < CGBA_NOR_ROM_MAX_ENTRIES) {
			new_ids[new_count++] = i;
		}
	}

	/* Sort newly discovered ROMs by directory scan_order ascending so the
	 * newest file on storage gets the largest first_seen value. */
	for (uint32_t i = 1; i < new_count; i++) {
		uint32_t id = new_ids[i];
		uint32_t j = i;

		while (j > 0 &&
				roms->entries[id].scan_order <
				roms->entries[new_ids[j - 1]].scan_order) {
			new_ids[j] = new_ids[j - 1];
			j--;
		}
		new_ids[j] = id;
	}

	for (uint32_t i = 0; i < new_count; i++) {
		cgba_nor_rom_entry *rom = &roms->entries[new_ids[i]];
		cgba_rom_meta_entry *slot = cgba_rom_meta_alloc_slot(meta, roms);
		uint32_t lo, hi;

		cgba_rom_meta_hash(rom->label, &lo, &hi);
		slot->hash_lo = lo;
		slot->hash_hi = hi;
		slot->last_played = 0;
		slot->first_seen = ++meta->seen_seq;
		rom->last_played = 0;
		rom->added_order = slot->first_seen;
		dirty = 1;
	}

	return dirty;
}

/* Mark `rom` as the most recently played game. Returns 1 if `meta` changed
 * and should be saved (0 if `rom` was already the most recently played game). */
static inline int cgba_rom_meta_record_play(cgba_rom_meta_file *meta,
	const cgba_nor_rom_list *roms, cgba_nor_rom_entry *rom)
{
	uint32_t lo, hi;
	int idx;
	cgba_rom_meta_entry *slot;

	if (!meta || !rom)
		return 0;

	if (!cgba_rom_meta_valid(meta))
		cgba_rom_meta_reset(meta);

	cgba_rom_meta_hash(rom->label, &lo, &hi);
	idx = cgba_rom_meta_find(meta, lo, hi);
	if (idx >= 0) {
		slot = &meta->entries[idx];
		if (slot->last_played > 0 && slot->last_played == meta->play_seq) {
			rom->last_played = slot->last_played;
			return 0;
		}
	} else {
		slot = cgba_rom_meta_alloc_slot(meta, roms);
		slot->hash_lo = lo;
		slot->hash_hi = hi;
		slot->first_seen = rom->added_order ? rom->added_order
			: ++meta->seen_seq;
		rom->added_order = slot->first_seen;
	}

	slot->last_played = ++meta->play_seq;
	rom->last_played = slot->last_played;
	return 1;
}

#endif /* CGBA_GINT_GPSP_ROM_META_H */
