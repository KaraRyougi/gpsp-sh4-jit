#ifndef CGBA_TEST_GINT_DMA_H
#define CGBA_TEST_GINT_DMA_H

/* Host-test stand-in for gint's DMA API. The test implements the transfer
 * synchronously against its simulated panel. */

#include <stdbool.h>
#include <gint/gint.h>

typedef enum { DMA_32B = 4 } dma_size_t;
typedef enum { DMA_FIXED = 0, DMA_INC = 1 } dma_address_t;

#ifndef GINT_CALL_NULL
#define GINT_CALL_NULL ((gint_call_t){ 0 })
#endif

bool dma_transfer_async(int channel, dma_size_t size, unsigned blocks,
	void const *src, dma_address_t src_mode, void *dst,
	dma_address_t dst_mode, gint_call_t callback);
void dma_transfer_wait(int channel);

#endif
