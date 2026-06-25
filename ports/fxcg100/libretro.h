#ifndef CGBA_MINIMAL_LIBRETRO_H
#define CGBA_MINIMAL_LIBRETRO_H

#include <stddef.h>
#include <stdint.h>

#define RETRO_DEVICE_JOYPAD 1

#define RETRO_VFS_FILE_ACCESS_READ            (1 << 0)
#define RETRO_VFS_FILE_ACCESS_WRITE           (1 << 1)
#define RETRO_VFS_FILE_ACCESS_READ_WRITE      \
  (RETRO_VFS_FILE_ACCESS_READ | RETRO_VFS_FILE_ACCESS_WRITE)
#define RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING (1 << 2)

#define RETRO_VFS_FILE_ACCESS_HINT_NONE            0
#define RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS (1 << 0)

#define RETRO_NETPACKET_UNRELIABLE  0
#define RETRO_NETPACKET_RELIABLE    (1 << 0)
#define RETRO_NETPACKET_UNSEQUENCED (1 << 1)
#define RETRO_NETPACKET_FLUSH_HINT  (1 << 2)
#define RETRO_NETPACKET_BROADCAST   0xffff

enum retro_device_id_joypad {
  RETRO_DEVICE_ID_JOYPAD_B = 0,
  RETRO_DEVICE_ID_JOYPAD_Y = 1,
  RETRO_DEVICE_ID_JOYPAD_SELECT = 2,
  RETRO_DEVICE_ID_JOYPAD_START = 3,
  RETRO_DEVICE_ID_JOYPAD_UP = 4,
  RETRO_DEVICE_ID_JOYPAD_DOWN = 5,
  RETRO_DEVICE_ID_JOYPAD_LEFT = 6,
  RETRO_DEVICE_ID_JOYPAD_RIGHT = 7,
  RETRO_DEVICE_ID_JOYPAD_A = 8,
  RETRO_DEVICE_ID_JOYPAD_X = 9,
  RETRO_DEVICE_ID_JOYPAD_L = 10,
  RETRO_DEVICE_ID_JOYPAD_R = 11,
  RETRO_DEVICE_ID_JOYPAD_L2 = 12,
  RETRO_DEVICE_ID_JOYPAD_R2 = 13,
  RETRO_DEVICE_ID_JOYPAD_L3 = 14,
  RETRO_DEVICE_ID_JOYPAD_R3 = 15,
  RETRO_DEVICE_ID_JOYPAD_MASK = 256
};

typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device,
                                       unsigned index, unsigned id);

struct retro_game_info {
  const char *path;
  const void *data;
  size_t size;
  const char *meta;
};

#endif
