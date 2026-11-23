#ifndef CGBA_HASH_H
#define CGBA_HASH_H

#include <stddef.h>
#include <stdint.h>

/* Shared FNV-1a 32-bit hashes used by frame overlays, runner diagnostics, and
 * headless tests. RGB565 pixel hashes feed high byte then low byte so hashes
 * are independent of host endianness. */

static inline uint32_t cgba_fnv1a32_bytes(const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

static inline uint32_t cgba_fnv1a32_rgb565(const uint16_t *pixels, size_t count)
{
	uint32_t h = 2166136261u;

	if (!pixels)
		return 0u;
	for (size_t i = 0; i < count; i++) {
		uint16_t px = pixels[i];
		h ^= (uint8_t)(px >> 8);
		h *= 16777619u;
		h ^= (uint8_t)px;
		h *= 16777619u;
	}
	return h;
}

static inline uint32_t cgba_fnv1a32_rgb565_rect(const uint16_t *pixels,
	unsigned width, unsigned height, unsigned pitch, unsigned *out_black)
{
	uint32_t h = 2166136261u;
	unsigned black = 0u;

	if (!pixels) {
		if (out_black)
			*out_black = 0u;
		return 0u;
	}
	for (unsigned y = 0; y < height; y++) {
		const uint16_t *row = pixels + y * pitch;
		for (unsigned x = 0; x < width; x++) {
			uint16_t px = row[x];
			black += (px == 0u);
			h = (h ^ (uint8_t)(px >> 8)) * 16777619u;
			h = (h ^ (uint8_t)px) * 16777619u;
		}
	}
	if (out_black)
		*out_black = black;
	return h;
}

#endif /* CGBA_HASH_H */
