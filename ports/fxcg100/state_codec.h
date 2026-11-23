#ifndef CGBA_STATE_CODEC_H
#define CGBA_STATE_CODEC_H

#include <stdint.h>
#include <string.h>

/*
 * Savestate compression codecs shared across fx-CG50, fx-CG100, and ClassPad II.
 *
 *   CZS2 (current): 12-byte header { u32 magic, u32 raw_size, u32 comp_size }
 *                   followed by a standard LZ4 block stream. Zero external
 *                   dependencies, unaligned-safe for SH-4A, uses a caller-
 *                   provided 2048-entry hash table (8 KiB, zero heap).
 *
 *   CZS1 (legacy):  8-byte header { u32 magic, u32 raw_size } followed by
 *                   32-bit word-RLE tokens. Kept for backward-compatible
 *                   loading of existing .SVS files.
 */

#define CGBA_STATE_MAGIC_RLE  0x435A5331u /* 'CZS1' (legacy word-RLE) */
#define CGBA_STATE_MAGIC_LZ4  0x435A5332u /* 'CZS2' (LZ4 block) */
#define CGBA_STATE_LZ4_HDR_SZ 12u
#define CGBA_STATE_LZ4_HASH_LOG 11u
#define CGBA_STATE_LZ4_HASH_LEN (1u << CGBA_STATE_LZ4_HASH_LOG) /* 2048 u32s = 8 KiB */

static inline uint32_t cgba_codec_load32(const uint8_t *p)
{
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static inline void cgba_codec_store32(uint8_t *p, uint32_t v)
{
  memcpy(p, &v, sizeof(v));
}

static inline unsigned cgba_lz4_hash4(uint32_t v)
{
  return (unsigned)((v * 2654435761u) >> (32u - CGBA_STATE_LZ4_HASH_LOG));
}

/* Compress `raw[0..rawsz)` into `out[0..cap)` as a CZS2 (LZ4 block) stream.
 * `hash_tab` must point to CGBA_STATE_LZ4_HASH_LEN uint32_t entries.
 * Returns total encoded bytes (including 12-byte header), or 0 if the stream
 * would exceed `cap` (caller falls back to storing raw). */
static inline unsigned cgba_state_compress_lz4(const uint8_t *raw,
                                               unsigned rawsz,
                                               uint8_t *out,
                                               unsigned cap,
                                               uint32_t *hash_tab)
{
  const uint8_t *ip = raw;
  const uint8_t *anchor = raw;
  const uint8_t *const iend = raw + rawsz;
  const uint8_t *const mflimit = (rawsz > 12u) ? (iend - 12u) : raw;
  const uint8_t *const matchlimit = (rawsz > 5u) ? (iend - 5u) : raw;
  uint8_t *op = out + CGBA_STATE_LZ4_HDR_SZ;
  uint8_t *const oend = out + cap;

  if (!raw || !out || !hash_tab || rawsz == 0u || cap <= CGBA_STATE_LZ4_HDR_SZ)
    return 0u;

  memset(hash_tab, 0, CGBA_STATE_LZ4_HASH_LEN * sizeof(uint32_t));

  if (rawsz > 12u) {
    hash_tab[cgba_lz4_hash4(cgba_codec_load32(ip))] = 0u;
    ip++;

    while (ip < mflimit) {
      const uint8_t *ref;
      uint32_t cur_val;
      unsigned pos, ref_pos, offset, step = 1u, search_miss = 64u;
      unsigned lit_len, match_len;
      uint8_t *token;

      /* Advance until we find a 4-byte match within the 64 KiB LZ4 window. */
      for (;;) {
        cur_val = cgba_codec_load32(ip);
        unsigned h = cgba_lz4_hash4(cur_val);
        pos = (unsigned)(ip - raw);
        ref_pos = hash_tab[h];
        hash_tab[h] = pos;
        offset = pos - ref_pos;
        if (offset > 0u && offset <= 0xffffu &&
            cgba_codec_load32(raw + ref_pos) == cur_val) {
          ref = raw + ref_pos;
          break;
        }
        ip += step;
        if (ip >= mflimit)
          goto emit_last_literals;
        step = (++search_miss) >> 6;
      }

      /* Emit literals from anchor..ip. */
      lit_len = (unsigned)(ip - anchor);
      token = op;
      {
        unsigned extra_lit = (lit_len >= 15u) ? (1u + (lit_len - 15u) / 255u) : 0u;
        if ((unsigned)(oend - op) < 1u + extra_lit + lit_len + 2u)
          return 0u;
      }
      op++;
      if (lit_len >= 15u) {
        unsigned rem = lit_len - 15u;
        *token = 0xf0u;
        while (rem >= 255u) {
          *op++ = 255u;
          rem -= 255u;
        }
        *op++ = (uint8_t)rem;
      } else {
        *token = (uint8_t)(lit_len << 4);
      }
      if (lit_len) {
        memcpy(op, anchor, lit_len);
        op += lit_len;
      }

      /* Write 16-bit little-endian match offset. */
      *op++ = (uint8_t)(offset & 0xffu);
      *op++ = (uint8_t)((offset >> 8) & 0xffu);

      /* Extend match as far as possible up to matchlimit. */
      ip += 4u;
      ref += 4u;
      while (ip < matchlimit && *ip == *ref) {
        ip++;
        ref++;
      }
      match_len = (unsigned)(ip - (anchor + lit_len)) - 4u;
      if (match_len >= 15u) {
        unsigned rem = match_len - 15u;
        *token |= 0x0fu;
        if ((unsigned)(oend - op) < (rem / 255u) + 1u)
          return 0u;
        while (rem >= 255u) {
          *op++ = 255u;
          rem -= 255u;
        }
        *op++ = (uint8_t)rem;
      } else {
        *token |= (uint8_t)match_len;
      }

      anchor = ip;
      if (ip < mflimit) {
        /* Index position just before anchor for better chained matches. */
        const uint8_t *prev = ip - 2u;
        hash_tab[cgba_lz4_hash4(cgba_codec_load32(prev))] =
          (unsigned)(prev - raw);
      }
    }
  }

emit_last_literals:
  {
    unsigned lit_len = (unsigned)(iend - anchor);
    unsigned extra_lit = (lit_len >= 15u) ? (1u + (lit_len - 15u) / 255u) : 0u;
    if ((unsigned)(oend - op) < 1u + extra_lit + lit_len)
      return 0u;
    if (lit_len >= 15u) {
      unsigned rem = lit_len - 15u;
      *op++ = 0xf0u;
      while (rem >= 255u) {
        *op++ = 255u;
        rem -= 255u;
      }
      *op++ = (uint8_t)rem;
    } else {
      *op++ = (uint8_t)(lit_len << 4);
    }
    if (lit_len) {
      memcpy(op, anchor, lit_len);
      op += lit_len;
    }
  }

  {
    unsigned total = (unsigned)(op - out);
    unsigned comp_size = total - CGBA_STATE_LZ4_HDR_SZ;
    cgba_codec_store32(out + 0u, CGBA_STATE_MAGIC_LZ4);
    cgba_codec_store32(out + 4u, rawsz);
    cgba_codec_store32(out + 8u, comp_size);
    return total;
  }
}

/* Decompress a CZS2 (LZ4 block) stream into `raw[0..rawsz)`. Returns 1 on ok. */
static inline int cgba_state_decompress_lz4(const uint8_t *in, unsigned insz,
                                            uint8_t *raw, unsigned rawsz)
{
  uint32_t magic, stored_raw, comp_size;
  const uint8_t *ip, *iend;
  uint8_t *op = raw;
  uint8_t *const oend = raw + rawsz;

  if (!in || !raw || insz < CGBA_STATE_LZ4_HDR_SZ)
    return 0;
  magic = cgba_codec_load32(in + 0u);
  stored_raw = cgba_codec_load32(in + 4u);
  comp_size = cgba_codec_load32(in + 8u);
  if (magic != CGBA_STATE_MAGIC_LZ4 || stored_raw != rawsz ||
      comp_size == 0u || comp_size > insz - CGBA_STATE_LZ4_HDR_SZ)
    return 0;

  ip = in + CGBA_STATE_LZ4_HDR_SZ;
  iend = ip + comp_size;

  while (ip < iend) {
    uint8_t token = *ip++;
    unsigned lit_len = (unsigned)(token >> 4);
    unsigned match_len, offset;

    if (lit_len == 15u) {
      uint8_t extra;
      do {
        if (ip >= iend)
          return 0;
        extra = *ip++;
        lit_len += extra;
      } while (extra == 255u);
    }
    if (lit_len > (unsigned)(iend - ip) || lit_len > (unsigned)(oend - op))
      return 0;
    if (lit_len) {
      memcpy(op, ip, lit_len);
      ip += lit_len;
      op += lit_len;
    }

    if (ip == iend)
      break;

    if ((unsigned)(iend - ip) < 2u)
      return 0;
    offset = (unsigned)ip[0] | ((unsigned)ip[1] << 8);
    ip += 2u;
    if (offset == 0u || offset > (unsigned)(op - raw))
      return 0;

    match_len = (unsigned)(token & 0x0fu) + 4u;
    if ((token & 0x0fu) == 15u) {
      uint8_t extra;
      do {
        if (ip >= iend)
          return 0;
        extra = *ip++;
        match_len += extra;
      } while (extra == 255u);
    }
    if (match_len > (unsigned)(oend - op))
      return 0;

    {
      const uint8_t *ref = op - offset;
      while (match_len--)
        *op++ = *ref++;
    }
  }

  return op == oend;
}

/* Legacy CZS1 (32-bit word-RLE) decompressor kept for existing savestates. */
static inline int cgba_state_decompress_rle(const uint8_t *in, unsigned insz,
                                            uint8_t *raw, unsigned rawsz)
{
  unsigned iw = 0u, icap = insz / 4u, n = rawsz / 4u, i = 0u;

  if (!in || !raw || (rawsz & 3u) != 0u || icap < 2u)
    return 0;
  if (cgba_codec_load32(in + 0u) != CGBA_STATE_MAGIC_RLE ||
      cgba_codec_load32(in + 4u) != rawsz)
    return 0;
  iw = 2u;
  while (i < n && iw < icap) {
    uint32_t hdr = cgba_codec_load32(in + iw * 4u);
    iw++;
    if (hdr & 0x80000000u) {
      uint32_t run = hdr & 0x7fffffffu, v;
      if (iw >= icap || run > n - i)
        return 0;
      v = cgba_codec_load32(in + iw * 4u);
      iw++;
      while (run--) {
        cgba_codec_store32(raw + i * 4u, v);
        i++;
      }
    } else {
      if (hdr > icap - iw || hdr > n - i)
        return 0;
      while (hdr--) {
        cgba_codec_store32(raw + i * 4u, cgba_codec_load32(in + iw * 4u));
        iw++;
        i++;
      }
    }
  }
  return i == n;
}

/* Auto-detect CZS2 (LZ4) or legacy CZS1 (word-RLE) and decompress. */
static inline int cgba_state_decompress_any(const uint8_t *in, unsigned insz,
                                            uint8_t *raw, unsigned rawsz)
{
  uint32_t magic;

  if (!in || !raw || insz < 8u)
    return 0;
  magic = cgba_codec_load32(in);
  if (magic == CGBA_STATE_MAGIC_LZ4)
    return cgba_state_decompress_lz4(in, insz, raw, rawsz);
  if (magic == CGBA_STATE_MAGIC_RLE)
    return cgba_state_decompress_rle(in, insz, raw, rawsz);
  return 0;
}

/* Choose the on-disk file size for a compressed savestate:
 *   - Rounds new/resized files up to `bucket` (16 KiB, 4 Fugue blocks) to cut
 *     zero-padding on first creation.
 *   - Reuses `existing_size` when the slot already exists and fits the new
 *     stream with <= 4 buckets (64 KiB) of slack, so repeat saves hit the fast
 *     in-place 4 KiB diff overwrite path instead of remove + create.
 *   - Falls back to `raw_size` if compression cannot beat raw. */
static inline unsigned cgba_state_choose_file_size(unsigned comp_size,
                                                   int existing_size,
                                                   unsigned raw_size,
                                                   unsigned max_file_size,
                                                   unsigned bucket)
{
  unsigned fsz;

  if (comp_size == 0u || comp_size >= raw_size || bucket == 0u)
    return raw_size;

  if (existing_size > 0 &&
      (unsigned)existing_size >= comp_size &&
      (unsigned)existing_size < raw_size &&
      (unsigned)existing_size <= max_file_size &&
      ((unsigned)existing_size - comp_size) <= 4u * bucket)
    return (unsigned)existing_size;

  fsz = (comp_size + bucket - 1u) & ~(bucket - 1u);
  if (fsz >= raw_size || fsz > max_file_size)
    return raw_size;
  return fsz;
}

#endif
