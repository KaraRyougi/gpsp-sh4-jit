#include <stddef.h>
#include <stdint.h>

#include "ports/fxcg100/sh4/sh4_cache.h"

extern uint8_t lcgba_hottext[];
extern uint8_t rcgba_hottext[];
extern uint8_t ecgba_hottext[];

int cgba_hottext_init(void)
{
	volatile const uint32_t *src =
		(volatile const uint32_t *)(const void *)lcgba_hottext;
	volatile uint32_t *dst = (volatile uint32_t *)(void *)rcgba_hottext;
	size_t bytes = (size_t)(ecgba_hottext - rcgba_hottext);
	size_t words = bytes / sizeof(uint32_t);

	for(size_t i = 0; i < words; i++)
		dst[i] = src[i];

	volatile const uint8_t *src8 =
		(volatile const uint8_t *)(const void *)(src + words);
	volatile uint8_t *dst8 = (volatile uint8_t *)(void *)(dst + words);
	for(size_t i = words * sizeof(uint32_t); i < bytes; i++)
		dst8[i - words * sizeof(uint32_t)] =
			src8[i - words * sizeof(uint32_t)];

	for(size_t i = 0; i < words; i++) {
		if(dst[i] != src[i])
			return 0;
	}
	for(size_t i = words * sizeof(uint32_t); i < bytes; i++) {
		if(dst8[i - words * sizeof(uint32_t)] !=
				src8[i - words * sizeof(uint32_t)])
			return 0;
	}

	cgba_sh4_cache_sync(rcgba_hottext, ecgba_hottext);
	return 1;
}
