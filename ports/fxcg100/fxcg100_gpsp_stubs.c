#include "vendor/gpsp/common.h"
#include <streams/file_stream.h>

int dynarec_enable = 0;
boot_mode selected_boot_mode = boot_game;
int sprite_limit = 1;

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;

u32 skip_next_frame = 0;
u32 num_skipped_frames = 0;

u32 netplay_num_clients = 0;
u32 netplay_client_id = 0;

void set_fastforward_override(bool fastforward)
{
  (void)fastforward;
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
  (void)client_id;
  (void)buf;
  (void)len;
}

void netpacket_poll_receive(void)
{
}

/* Weak fallback for cpu.cc's wild-guest-PC guard: the gint port overrides
 * this with a gint_panic crash report (src/gpsp_runner.c). A PC outside the
 * GBA bus means emulation already went wild; spinning here is still better
 * than the out-of-bounds memory_map_read[] host read it replaces. */
__attribute__((weak, noreturn)) void cgba_wild_pc_trap(u32 pc)
{
  (void)pc;
  for(;;)
    ;
}

/* Weak fallbacks: the gint NOR port overrides these with a NOR-backed
 * filestream (src/nor_filestream.c). The freestanding build keeps these stubs. */
__attribute__((weak)) RFILE *filestream_open(const char *path, unsigned mode, unsigned hints)
{
  (void)path;
  (void)mode;
  (void)hints;
  return NULL;
}

__attribute__((weak)) int64_t filestream_get_size(RFILE *stream)
{
  (void)stream;
  return -1;
}

__attribute__((weak)) int64_t filestream_seek(RFILE *stream, int64_t offset, int seek_position)
{
  (void)stream;
  (void)offset;
  (void)seek_position;
  return -1;
}

__attribute__((weak)) int64_t filestream_read(RFILE *stream, void *data, int64_t len)
{
  (void)stream;
  if (data && len > 0)
    memset(data, 0xff, (size_t)len);
  return 0;
}

__attribute__((weak)) int filestream_close(RFILE *stream)
{
  (void)stream;
  return 0;
}
