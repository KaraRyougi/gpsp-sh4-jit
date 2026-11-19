#ifndef CGBA_TEST_GINT_R61524_H
#define CGBA_TEST_GINT_R61524_H

#include <stdint.h>

enum { R61524_DMA, R61524_DMA_WAIT, R61524_CPU };
void r61524_display(uint16_t *vram, int start, int height, int method);
void r61524_start_frame(int xmin, int xmax, int ymin, int ymax);
void r61524_win_set(uint16_t HSA, uint16_t HEA, uint16_t VSA, uint16_t VEA);

#endif
