#ifndef CGBA_GINT_GPSP_RUNNER_H
#define CGBA_GINT_GPSP_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#include "fxcg100_platform.h"

#define CGBA_GBA_WIDTH 240
#define CGBA_GBA_HEIGHT 160
#define CGBA_GBA_PITCH 240
#define CGBA_GBA_BUFFER_PIXELS (CGBA_GBA_PITCH * (CGBA_GBA_HEIGHT + 1))

/* Calculator savestates use a fixed 416 KiB raw BSON image. Compressed files
 * are rounded to 64 KiB buckets, so the largest legacy bucket is 448 KiB.
 * Keep raw and compressed staging adjacent in the non-JIT GamePak
 * scratch arena; a save must never perturb the JIT cache or its heat state. */
#define CGBA_STATE_RAW_SIZE       (416u * 1024u)
#define CGBA_STATE_FILE_BUCKET    0x10000u
#define CGBA_STATE_COMP_STREAM_MAX (CGBA_STATE_RAW_SIZE + 12u)
#define CGBA_STATE_COMP_FILE_MAX  \
	((CGBA_STATE_COMP_STREAM_MAX + CGBA_STATE_FILE_BUCKET - 1u) & \
	 ~(CGBA_STATE_FILE_BUCKET - 1u))
#define CGBA_STATE_WORK_SIZE \
	(CGBA_STATE_RAW_SIZE + CGBA_STATE_COMP_FILE_MAX)

_Static_assert((CGBA_STATE_FILE_BUCKET & (CGBA_STATE_FILE_BUCKET - 1u)) == 0,
	"savestate file bucket must be a power of two");
_Static_assert(CGBA_STATE_WORK_SIZE <= 1024u * 1024u,
	"savestate work area must fit one contiguous GamePak cache block");

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
/* Revalidate the active ROM's Fugue/NOR mappings after any storage mutation.
 * Returns 0 without allowing guest execution if no safe mapping can be made. */
int cgba_gpsp_storage_sync(void);
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

#if defined(CGBA_DYNAREC) && defined(CGBA_SH4_DIFF_HARNESS)
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
