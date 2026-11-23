#ifndef CGBA_HEADLESS_TEST_H
#define CGBA_HEADLESS_TEST_H

#include <stdint.h>

#ifdef CGBA_GPSP_HEADLESS_TEST
int cgba_start_gpsp(uint16_t *framebuffer, unsigned rom_id);
int cgba_headless_test(uint16_t *framebuffer);
#endif

#endif /* CGBA_HEADLESS_TEST_H */
