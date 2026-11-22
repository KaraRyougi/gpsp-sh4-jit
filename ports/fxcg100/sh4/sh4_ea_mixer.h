#ifndef CGBA_SH4_EA_MIXER_H
#define CGBA_SH4_EA_MIXER_H

/*
 * Silent-backend fast fold for the EA GBA self-modifying ARM IWRAM PCM mixer
 * (used in Need for Speed - Most Wanted and related EA GBA titles).
 *
 * The original IWRAM routine (996 bytes / 249 ARM words, entry
 * `stmdb sp!, {r1-r12, lr}; cmp r2, #0; beq +0x3a8; add r0, r0, #28`):
 *   - Takes r0 = mixer struct (28-byte scratch header + 20-byte channel array
 *     terminated by vol == 255 with ptr == rewind_bytes), r1 = PCM output
 *     buffer, r2 = sample count.
 *   - Self-modifies two instructions inside its own 16-sample unrolled loop to
 *     handle 4/8/12-sample tails, then restores them before returning r0 = r1 + r2.
 *   - On the muted calculator port (CGBA_SH4_M4A_MIX_SKIP), the PCM output
 *     buffer feeds only silenced DirectSound DMA. Advancing active channel
 *     (ptr, frac) phases in O(channels) without writing PCM or touching IWRAM
 *     code eliminates both all SMC RAM-cache flushes and interpreter fallback.
 */

#include <stdint.h>

enum {
  CGBA_SH4_EA_MIXER_WORDS = 249u,
  CGBA_SH4_EA_MIXER_BYTES = CGBA_SH4_EA_MIXER_WORDS * 4u,
  CGBA_SH4_EA_MIXER_W0 = 0xE92D5FFEu, /* stmdb sp!, {r1-r12, lr} */
  CGBA_SH4_EA_MIXER_W1 = 0xE3520000u, /* cmp   r2, #0            */
  CGBA_SH4_EA_MIXER_W2 = 0x0A0000E8u, /* beq   +0x3a8            */
  CGBA_SH4_EA_MIXER_W3 = 0xE280001Cu, /* add   r0, r0, #28       */
  CGBA_SH4_EA_MIXER_FNV1A = 0x680E2999u,
  CGBA_SH4_EA_MIXER_MAX_CHANNELS = 64u,
  CGBA_SH4_EA_MIXER_MAX_SAMPLES = 4096u
};

static inline uint32_t cgba_sh4_ea_mixer_fnv1a_step(uint32_t h, uint32_t w)
{
  return (h ^ w) * 0x01000193u;
}

static inline int cgba_sh4_ea_mixer_match_words(const uint32_t *words)
{
  uint32_t h = 0x811C9DC5u;
  uint32_t i;
  if (words[0] != CGBA_SH4_EA_MIXER_W0 ||
      words[1] != CGBA_SH4_EA_MIXER_W1 ||
      words[2] != CGBA_SH4_EA_MIXER_W2 ||
      words[3] != CGBA_SH4_EA_MIXER_W3)
    return 0;
  for (i = 0u; i < CGBA_SH4_EA_MIXER_WORDS; i++)
    h = cgba_sh4_ea_mixer_fnv1a_step(h, words[i]);
  return h == CGBA_SH4_EA_MIXER_FNV1A;
}

static inline void cgba_sh4_ea_mixer_advance_channel(
  uint32_t *ptr, uint32_t *frac, uint32_t frac_inc, uint32_t int_inc,
  uint32_t count, uint32_t tail_bytes)
{
  uint64_t phase = ((uint64_t)(*ptr) << 32) | (uint64_t)(*frac);
  uint64_t step = ((uint64_t)int_inc << 32) | (uint64_t)frac_inc;
  phase += step * (uint64_t)count;
  /* The trailing 0..3-sample loop uses `ldrsb lr, [r3], #1`, adding +1 per
   * trailing sample on top of int_inc + carry. */
  *ptr = (uint32_t)(phase >> 32) + tail_bytes;
  *frac = (uint32_t)phase;
}

static inline void cgba_sh4_ea_mixer_split_counts(
  uint32_t count, uint32_t r1, uint32_t *lead_out, uint32_t *body_out,
  uint32_t *tail_out)
{
  uint32_t lead = (r1 & 3u) ? (4u - (r1 & 3u)) : 0u;
  uint32_t rem, tail;
  if (lead > count)
    lead = count;
  rem = count - lead;
  tail = rem & 3u;
  *lead_out = lead;
  *body_out = rem - tail;
  *tail_out = tail;
}

static inline uint32_t cgba_sh4_ea_mixer_estimate_cycles(
  uint32_t count, uint32_t r1, uint32_t active_ch, uint32_t skipped_ch)
{
  uint32_t lead, body, tail, chunks, per_byte, per_chunk;
  if (count == 0u)
    return 18u;
  cgba_sh4_ea_mixer_split_counts(count, r1, &lead, &body, &tail);
  chunks = (body + 15u) >> 4;
  per_byte = 16u + skipped_ch * 10u + active_ch * 22u;
  per_chunk = 40u + skipped_ch * 10u + active_ch * 176u;
  return 32u + (lead + tail) * per_byte + chunks * per_chunk;
}

#endif /* CGBA_SH4_EA_MIXER_H */
