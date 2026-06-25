#include "vendor/gpsp/common.h"

direct_sound_struct direct_sound_channel[2];
gbc_sound_struct gbc_sound_channel[4];
u32 gbc_sound_master_volume_left;
u32 gbc_sound_master_volume_right;
u32 gbc_sound_master_volume;
u32 gbc_sound_buffer_index;
u32 gbc_sound_last_cpu_ticks;
const u32 sound_frequency = GBA_SOUND_FREQUENCY;
u32 sound_on;

const s8 square_pattern_duty[4][8] = {
  { 0, 1, 1, 1, 1, 1, 1, 1 },
  { 0, 0, 1, 1, 1, 1, 1, 1 },
  { 0, 0, 0, 0, 1, 1, 1, 1 },
  { 1, 1, 1, 1, 1, 1, 0, 0 },
};

void sound_timer_queue32(u32 channel, u32 value)
{
  (void)channel;
  (void)value;
}

unsigned sound_timer(fixed8_24 frequency_step, u32 channel)
{
  (void)frequency_step;
  (void)channel;
  return 0;
}

void sound_reset_fifo(u32 channel)
{
  if (channel < 2)
    memset(direct_sound_channel[channel].fifo, 0,
           sizeof(direct_sound_channel[channel].fifo));
}

void render_gbc_sound(void)
{
}

void init_sound(void)
{
  reset_sound();
}

void reset_sound(void)
{
  memset(direct_sound_channel, 0, sizeof(direct_sound_channel));
  memset(gbc_sound_channel, 0, sizeof(gbc_sound_channel));
  gbc_sound_master_volume_left = 0;
  gbc_sound_master_volume_right = 0;
  gbc_sound_master_volume = 0;
  gbc_sound_buffer_index = 0;
  gbc_sound_last_cpu_ticks = 0;
  sound_on = 0;
}

u32 sound_read_samples(s16 *out, u32 frames)
{
  if (out)
    memset(out, 0, frames * 2 * sizeof(*out));
  return frames;
}

bool sound_check_savestate(const u8 *src)
{
  (void)src;
  return true;
}

unsigned sound_write_savestate(u8 *dst)
{
  (void)dst;
  return 0;
}

bool sound_read_savestate(const u8 *src)
{
  (void)src;
  reset_sound();
  return true;
}
