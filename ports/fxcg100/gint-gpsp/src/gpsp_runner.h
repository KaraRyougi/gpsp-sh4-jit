#ifndef CGBA_GINT_GPSP_RUNNER_H
#define CGBA_GINT_GPSP_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#define CGBA_GBA_WIDTH 240
#define CGBA_GBA_HEIGHT 160
#define CGBA_GBA_PITCH 240
#define CGBA_GBA_BUFFER_PIXELS (CGBA_GBA_PITCH * (CGBA_GBA_HEIGHT + 1))

typedef enum cgba_gpsp_rom_id {
	CGBA_GPSP_ROM_MODE3_SMOKE = 0,
	CGBA_GPSP_ROM_INPUT_PROBE,
	CGBA_GPSP_ROM_LCD_TEST,
	CGBA_GPSP_ROM_BUILTIN_COUNT
} cgba_gpsp_rom_id;

const char *cgba_gpsp_rom_name(unsigned rom_id);
unsigned cgba_gpsp_rom_count(void);
unsigned cgba_gpsp_refresh_roms(void);
int cgba_gpsp_init(uint16_t *framebuffer, unsigned rom_id);
const char *cgba_gpsp_last_error(void);
void cgba_gpsp_run_frame(uint32_t gba_buttons, int render_video);
uint32_t cgba_gpsp_keyinput(void);
uint32_t cgba_gpsp_frame_hash(const uint16_t *pixels);
void cgba_gpsp_shutdown(void);

#endif
