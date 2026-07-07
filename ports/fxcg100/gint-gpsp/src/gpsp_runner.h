#ifndef CGBA_GINT_GPSP_RUNNER_H
#define CGBA_GINT_GPSP_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#include "fxcg100_platform.h"

#define CGBA_GBA_WIDTH 240
#define CGBA_GBA_HEIGHT 160
#define CGBA_GBA_PITCH 240
#define CGBA_GBA_BUFFER_PIXELS (CGBA_GBA_PITCH * (CGBA_GBA_HEIGHT + 1))

typedef enum cgba_gpsp_rom_id {
	/* LCD TEST is a generated pattern (no embedded ROM); kept as the built-in
	 * fallback when no storage ROM is present. Games load from \fls0\ storage. */
	CGBA_GPSP_ROM_LCD_TEST = 0,
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
void cgba_gpsp_debug_menu(fxcg100_debug_info *debug, unsigned frame,
	uint32_t last_hash, unsigned emu_fps, unsigned draw_fps,
	const uint16_t *framebuffer, uint32_t host_sp);
void cgba_gpsp_shutdown(void);

/* GBA in-game backup save (SRAM/Flash/EEPROM) persistence to \\fls0\<ROM>.SAV.
 * load runs when a ROM boots; flush(force) writes the save when it changed
 * (or always if force) -- driven by the UPDATE BACKUP menu setting + shutdown. */
void cgba_gpsp_backup_load(void);
void cgba_gpsp_backup_flush(int force);

/* Savestates: raw gba_save_state image at \\fls0\CGBAST<slot>.SAV (also
 * loadable by the emulator harness as its checkpoint blob). 1 = ok. */
int cgba_gpsp_state_save(unsigned slot);
int cgba_gpsp_state_load(unsigned slot);

/* Fill up to max_lines short strings with a pipeline snapshot (ROM load result,
 * first ROM bytes as seen by gpSP, gpSP PC/DISPCNT/VCOUNT, framebuffer state)
 * for on-screen hardware debugging. Returns the number of lines written. */
#define CGBA_DIAG_LINE_MAX 48
unsigned cgba_gpsp_diag(char out[][CGBA_DIAG_LINE_MAX], unsigned max_lines);

#define CGBA_STATE_LINE_MAX 512
unsigned cgba_gpsp_state_lines(unsigned frame, const char *phase,
	const uint16_t *framebuffer, char out[][CGBA_STATE_LINE_MAX],
	unsigned max_lines);

#ifdef CGBA_DYNAREC
/* Differential interp-vs-dynarec harness (subtask 2): run `cycles` of guest
 * code through both cores from one snapshot and format the first divergence
 * (or "MATCH") into out. Returns 1 if they diverged. */
int cgba_gpsp_diff_test(uint32_t cycles, char *out, unsigned out_len);
#endif

/* Install the CGBA panic handler: any CPU exception (and the JIT wild-jump
 * trap) renders guest CPU state alongside the hardware exception info. */
void cgba_crash_reporting_init(void);

#ifdef CGBA_DYNAREC
/* Overclock triage: hammer the translation-cache arena with the JIT's
 * write -> cache-sync -> execute pattern; 0 = pass. Destroys translated code —
 * flush the dynarec caches afterwards. Result text into out. */
uint32_t cgba_jit_canary(char *out, unsigned out_len);
#endif

#endif
