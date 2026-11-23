#ifndef CGBA_SH4_GAX_MIXER_H
#define CGBA_SH4_GAX_MIXER_H

/*
 * Silent-backend fast skips for common non-MP2K IWRAM ARM PCM mixers
 * (active only under CGBA_SH4_M4A_MIX_SKIP):
 *
 *  1. Shin'en GAX Sound Engine (standard + self-modifying ping-pong mixers,
 *     e.g. Crash Nitro Kart / G0127_BCNJ, CT Special Forces 2 / G0014_A9CE).
 *     Intercepting the ping-pong mixer at entry prevents its 4 internal
 *     `strb r10, [pc, #-imm]` self-modifications from ever executing,
 *     eliminating both RAM-cache flushes and IWRAM SMC page quarantine.
 *  2. Vicarious Visions / Krawall IWRAM mixer (variants A & B, e.g. Jackie
 *     Chan / G0216_AJCE, Crash Bandicoot 1 / G0131_ACUJ).
 *  3. Eurocom / Factor 5 stereo IWRAM mixer (interpolated & raw variants,
 *     e.g. Harry Potter 2 / G0148_A7HJ, Jurassic Park III / G0046_ADNJ).
 *
 * Every routine advances only the exact channel-struct phase/loop/direction
 * fields and caller-visible registers while skipping discarded PCM buffer
 * writes and ROM sample reads.  Header has no target dependencies so it can
 * be exhaustively unit-tested on the host (`tests/sh4_gax_mixer_test.c`).
 */

#include <stdint.h>

enum {
  CGBA_SH4_AUDIO_MIXER_NONE           = 0,
  CGBA_SH4_AUDIO_MIXER_GAX_STD        = 1, /* 29 words */
  CGBA_SH4_AUDIO_MIXER_GAX_PINGPONG   = 2, /* 46 words */
  CGBA_SH4_AUDIO_MIXER_VV_A           = 3, /* 76 words (G0216_AJCE) */
  CGBA_SH4_AUDIO_MIXER_VV_B           = 4, /* 55 words (G0131_ACUJ) */
  CGBA_SH4_AUDIO_MIXER_EUROCOM_INTERP = 5, /* 64 words forward path (G0148_A7HJ) */
  CGBA_SH4_AUDIO_MIXER_EUROCOM_RAW    = 6, /* 55 words forward path (G0046_ADNJ) */
  CGBA_SH4_AUDIO_MIXER_4CH_SUM1       = 7, /* 8 words: 1-ch byte sum + LUT clamp */
  CGBA_SH4_AUDIO_MIXER_4CH_SUM2       = 8, /* 10 words: 2-ch byte sum + LUT clamp */
  CGBA_SH4_AUDIO_MIXER_4CH_SUM3       = 9, /* 12 words: 3-ch byte sum + LUT clamp */
  CGBA_SH4_AUDIO_MIXER_4CH_SUM4       = 10,/* 14 words: 4-ch byte sum + LUT clamp */
  CGBA_SH4_AUDIO_MIXER_4CH_RESAMPLE   = 11 /* 195 words: 4-ch resample+mix+clamp (G0226_B6GE) */
};

enum {
  CGBA_SH4_GAX_STD_WORDS        = 29u,
  CGBA_SH4_GAX_PINGPONG_WORDS   = 46u,
  CGBA_SH4_VV_A_WORDS           = 76u,
  CGBA_SH4_VV_B_WORDS           = 55u,
  CGBA_SH4_EUROCOM_INTERP_WORDS = 64u,
  CGBA_SH4_EUROCOM_RAW_WORDS    = 56u,
  CGBA_SH4_4CH_RESAMPLE_WORDS   = 195u,
  CGBA_SH4_AUDIO_MIXER_MAX_WORDS = 76u,
  CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES = 4096u
};

/* ---- Structural matchers ------------------------------------------------- */

static inline int cgba_sh4_gax_std_match(const uint32_t *w, unsigned avail)
{
  static const uint32_t head[21] = {
    0xE92D0FF0u, 0xE89000FEu,
    0xE7D185C4u, 0xE0944007u, 0x50444005u,
    0xE7D195C4u, 0xE0944007u, 0x50444005u,
    0xE1888809u, 0xE0080896u, 0xE3C8B807u,
    0xE7D185C4u, 0xE0944007u, 0x50444005u,
    0xE7D195C4u, 0xE0944007u, 0x50444005u,
    0xE1888809u, 0xE0080896u, 0xE3C88807u,
    0xE8920600u
  };
  unsigned i;
  if (avail < CGBA_SH4_GAX_STD_WORDS)
    return 0;
  for (i = 0u; i < 21u; i++) {
    if (w[i] != head[i])
      return 0;
  }
  /* w[21]: add r9, r9, r11, lsr #S; w[22]: add r10, r10, r8, lsr #S */
  if ((w[21] & 0xFFFFF07Fu) != 0xE089902Bu ||
      (w[22] & 0xFFFFF07Fu) != 0xE08AA028u)
    return 0;
  return w[23] == 0xE8A20600u && w[24] == 0xE2533004u &&
         w[25] == 0xCAFFFFE7u && w[26] == 0xE580400Cu &&
         w[27] == 0xE8BD0FF0u && w[28] == 0xE12FFF1Eu;
}

static inline int cgba_sh4_gax_pingpong_match(const uint32_t *w, unsigned avail)
{
  static const uint32_t bounce[13] = {
    0xE2777000u, 0xE0844087u, 0x50444005u, 0x508115A5u,
    0x43A0A04Bu, 0x40844005u, 0x404115A5u, 0x53A0A05Bu,
    0xE54FA091u, 0xE54FA089u, 0xE54FA075u, 0xE54FA06Du,
    0xE12FFF1Eu
  };
  unsigned i;
  if (avail < CGBA_SH4_GAX_PINGPONG_WORDS)
    return 0;
  if (w[0] != 0xE92D4FF0u || w[1] != 0xE89000FEu ||
      w[2] != 0xE3570000u || w[3] != 0xEB000020u)
    return 0;
  /* Loop body: 4 samples; w[6,9,15,18] are self-modified BLPL(5B)/BLMI(4B). */
  if (w[4]  != 0xE7D185C4u || w[5]  != 0xE0944007u ||
      (w[6]  & 0xEFFFFFFFu) != 0x4B000019u ||
      w[7]  != 0xE7D195C4u || w[8]  != 0xE0944007u ||
      (w[9]  & 0xEFFFFFFFu) != 0x4B000016u ||
      w[10] != 0xE1888809u || w[11] != 0xE0080896u || w[12] != 0xE3C8B807u ||
      w[13] != 0xE7D185C4u || w[14] != 0xE0944007u ||
      (w[15] & 0xEFFFFFFFu) != 0x4B000010u ||
      w[16] != 0xE7D195C4u || w[17] != 0xE0944007u ||
      (w[18] & 0xEFFFFFFFu) != 0x4B00000Du ||
      w[19] != 0xE1888809u || w[20] != 0xE0080896u || w[21] != 0xE3C88807u ||
      w[22] != 0xE8920600u)
    return 0;
  if ((w[23] & 0xFFFFF07Fu) != 0xE089902Bu ||
      (w[24] & 0xFFFFF07Fu) != 0xE08AA028u)
    return 0;
  if (w[25] != 0xE8A20600u || w[26] != 0xE2533004u || w[27] != 0xCAFFFFE7u ||
      w[28] != 0xE580400Cu || w[29] != 0xE5807018u || w[30] != 0xE5801000u ||
      w[31] != 0xE8BD4FF0u || w[32] != 0xE12FFF1Eu)
    return 0;
  for (i = 0u; i < 13u; i++) {
    if (w[33u + i] != bounce[i])
      return 0;
  }
  return 1;
}

/* Vicarious Visions / Krawall mixer (Variant A: G0216_AJCE, 76 words;
 * Variant B: G0131_ACUJ, 55 words).  Allows caller-patched direction words
 * (`add`/`sub r2, r2, r8` and `blt`/`bgt` loop back-edge) in modes 0 & 1. */
static inline int cgba_sh4_vv_a_match(const uint32_t *w, unsigned avail)
{
  static const uint32_t pro[18] = {
    0xE92D0FF1u, 0xE5907018u, 0xE590801Cu, 0xE5901004u,
    0xE5902008u, 0xE590300Cu, 0xE5904010u, 0xE1A04084u,
    0xE5905014u, 0xE1A05085u, 0xE590A020u, 0xE590B024u,
    0xE5900000u, 0xE79FA10Au, 0xE08FF00Au, 0x00000024u,
    0x00000068u, 0x000000C8u
  };
  unsigned i;
  if (avail < CGBA_SH4_VV_A_WORDS)
    return 0;
  for (i = 0u; i < 18u; i++) {
    if (w[i] != pro[i])
      return 0;
  }
  /* Mode 0 (w[18..32]) */
  if (w[18] != 0xE1A065C2u || w[19] != 0xE19060D6u || w[20] != 0xE0060697u ||
      w[21] != 0xE1A06446u || w[22] != 0xE18160B5u || w[23] != 0xE2855002u ||
      (w[24] != 0xE0822008u && w[24] != 0xE0422008u) ||
      w[25] != 0xE1550004u || w[26] != 0xAA000015u || w[27] != 0xE1520003u ||
      (w[28] != 0xBAFFFFF4u && w[28] != 0xCAFFFFF4u) ||
      w[29] != 0xE35B0000u || w[30] != 0x0A000011u || w[31] != 0xE042200Bu ||
      w[32] != 0xEAFFFFF0u)
    return 0;
  /* Mode 1 (w[33..49]) */
  if (w[33] != 0xE1A065C2u || w[34] != 0xE19060D6u || w[35] != 0xE0060697u ||
      w[36] != 0xE1A06446u || w[37] != 0xE19190F5u || w[38] != 0xE0866009u ||
      w[39] != 0xE18160B5u || w[40] != 0xE2855002u ||
      (w[41] != 0xE0822008u && w[41] != 0xE0422008u) ||
      w[42] != 0xE1550004u || w[43] != 0xAA000004u || w[44] != 0xE1520003u ||
      (w[45] != 0xBAFFFFF2u && w[45] != 0xCAFFFFF2u) ||
      w[46] != 0xE35B0000u || w[47] != 0x1042200Bu || w[48] != 0x1AFFFFEFu)
    return 0;
  /* Epilogue (w[49..54]) */
  if (w[49] != 0xE1A030A5u || w[50] != 0xE8BD0FF1u || w[51] != 0xE5802008u ||
      w[52] != 0xE5803014u || w[53] != 0xE1A0000Eu || w[54] != 0xE12FFF10u)
    return 0;
  /* Mode 2 (w[55..75]) */
  return w[55] == 0xE1A065C2u && w[56] == 0xE19060D6u && w[64] == 0xE2855004u &&
         w[65] == 0xE0822088u && w[66] == 0xE1520003u && w[67] == 0xB1550004u &&
         w[68] == 0xBAFFFFF1u && w[69] == 0xE1520003u && w[70] == 0xCA000001u &&
         w[71] == 0xE1550004u && w[72] == 0xDAFFFFE7u && w[73] == 0xE2455002u &&
         w[74] == 0xE0422008u && w[75] == 0xEAFFFFE4u;
}

static inline int cgba_sh4_vv_b_match(const uint32_t *w, unsigned avail)
{
  static const uint32_t pro[18] = {
    0xE92D0FF1u, 0xE5907018u, 0xE590801Cu, 0xE5901004u,
    0xE5902008u, 0xE590300Cu, 0xE5904010u, 0xE0814084u,
    0xE5905014u, 0xE0811085u, 0xE590A020u, 0xE590B024u,
    0xE5900000u, 0xE79FA10Au, 0xE08FF00Au, 0x00000020u,
    0x00000060u, 0x000000C4u
  };
  unsigned i;
  if (avail < CGBA_SH4_VV_B_WORDS)
    return 0;
  for (i = 0u; i < 18u; i++) {
    if (w[i] != pro[i])
      return 0;
  }
  /* Mode 0 (w[18..31]) */
  if (w[18] != 0xE1A065C2u || w[19] != 0xE19060D6u || w[20] != 0xE0060697u ||
      w[21] != 0xE1A06446u || w[22] != 0xE0C160B2u ||
      (w[23] != 0xE0822008u && w[23] != 0xE0422008u) ||
      w[24] != 0xE1510004u || w[25] != 0xAA000014u || w[26] != 0xE1520003u ||
      (w[27] != 0xBAFFFFF5u && w[27] != 0xCAFFFFF5u) ||
      w[28] != 0xE35B0000u || w[29] != 0x0A000010u || w[30] != 0xE042200Bu ||
      w[31] != 0xEAFFFFF1u)
    return 0;
  /* Mode 1 (w[32..46]) */
  if (w[32] != 0xE1A065C2u || w[33] != 0xE19060D6u || w[34] != 0xE0060697u ||
      w[35] != 0xE1A06446u || w[36] != 0xE1D190F0u || w[37] != 0xE0866009u ||
      w[38] != 0xE0C160B2u ||
      (w[39] != 0xE0822008u && w[39] != 0xE0422008u) ||
      w[40] != 0xE1510004u || w[41] != 0xAA000004u || w[42] != 0xE1520003u ||
      (w[43] != 0xBAFFFFF3u && w[43] != 0xCAFFFFF3u) ||
      w[44] != 0xE35B0000u || w[45] != 0x1042200Bu || w[46] != 0x1AFFFFF0u)
    return 0;
  /* Epilogue (w[47..54]) */
  return w[47] == 0xE8BD0FF1u && w[48] == 0xE5903004u && w[49] == 0xE0413003u &&
         w[50] == 0xE1A030A3u && w[51] == 0xE5802008u && w[52] == 0xE5803014u &&
         w[53] == 0xE1A0000Eu && w[54] == 0xE12FFF10u;
}

/* Eurocom / Factor 5 stereo mixer (128 samples/call).
 * Shared 17-word prologue + shared 36-word mix/loop/epilogue tail; differs
 * only in whether linear interpolation (9 words) or single ldrsb (1 word)
 * precedes the stereo multiply-accumulate. */
static inline int cgba_sh4_eurocom_match(const uint32_t *w, unsigned avail)
{
  static const uint32_t pro[17] = {
    0xE92D5FFEu, 0xE5902004u, 0xE590A008u, 0xE1D0B1B6u,
    0xE1D0E1B4u, 0xE5D04018u, 0xE5D05019u, 0xE3540000u,
    0x12844001u, 0xE3550000u, 0x12855001u, 0xE5D0801Au,
    0xE0188008u, 0x12655000u, 0xE3E08A0Fu, 0xE3A09080u,
    0xE15A0002u
  };
  static const uint32_t tail[34] = {
    0xE0060493u, 0xE1D170F0u, 0xE1D1C0F2u, 0xE0877446u,
    0xE0060593u, 0xE08CC446u, 0xE0C170B2u, 0xE0C1C0B2u,
    0xE08BB00Eu, 0xE35B0A01u, 0x3A000003u, 0xE082262Bu,
    0xE00BB008u, 0xE152000Au, 0x2A000006u, 0xE2599001u,
    /* w[base+16] = bne loop (1AFFFFE5 or 1AFFFFED) checked separately */
    0u,
    0xE5802004u, 0xE1C0B1B6u, 0xE3A00000u, 0xE8BD5FFEu,
    0xE12FFF1Eu, 0xE590C00Cu, 0xE01CC00Cu, 0x1A000002u,
    0xE3A00001u, 0xE8BD5FFEu, 0xE12FFF1Eu, 0xE042200Au,
    0xE082200Cu, 0xE5906010u, 0xE08CA006u, 0xE580A008u,
    0xE152000Au
  };
  unsigned i, base;
  if (avail < CGBA_SH4_EUROCOM_RAW_WORDS)
    return CGBA_SH4_AUDIO_MIXER_NONE;
  for (i = 0u; i < 17u; i++) {
    if (w[i] != pro[i])
      return CGBA_SH4_AUDIO_MIXER_NONE;
  }
  if (avail >= CGBA_SH4_EUROCOM_INTERP_WORDS &&
      w[17] == 0x4A000032u &&
      w[18] == 0xE2823001u && w[19] == 0xE153000Au && w[20] == 0x2A00002Au &&
      w[21] == 0xE1D260D0u && w[22] == 0xE1D370D0u && w[23] == 0xE0477006u &&
      w[24] == 0xE00B3008u && w[25] == 0xE0030397u && w[26] == 0xE0863643u) {
    base = 27u;
    if (w[base + 16u] != 0x1AFFFFE5u ||
        w[base + 34u] != 0x3AFFFFEBu || w[base + 35u] != 0xE1A0200Cu ||
        w[base + 36u] != 0xEAFFFFE9u)
      return CGBA_SH4_AUDIO_MIXER_NONE;
    for (i = 0u; i < 34u; i++) {
      if (i == 16u) continue;
      if (w[base + i] != tail[i])
        return CGBA_SH4_AUDIO_MIXER_NONE;
    }
    return CGBA_SH4_AUDIO_MIXER_EUROCOM_INTERP;
  }
  if (w[17] == 0x4A000025u && w[18] == 0xE1D230D0u) {
    base = 19u;
    if (w[base + 16u] != 0x1AFFFFEDu ||
        w[base + 34u] != 0x3AFFFFEBu || w[base + 35u] != 0xE1A0200Cu ||
        w[base + 36u] != 0xEAFFFFE9u)
      return CGBA_SH4_AUDIO_MIXER_NONE;
    for (i = 0u; i < 34u; i++) {
      if (i == 16u) continue;
      if (w[base + i] != tail[i])
        return CGBA_SH4_AUDIO_MIXER_NONE;
    }
    return CGBA_SH4_AUDIO_MIXER_EUROCOM_RAW;
  }
  return CGBA_SH4_AUDIO_MIXER_NONE;
}

/* 1..4 channel byte-PCM sum + LUT clamp mixer (e.g. G0226_B6GE):
 *   stmfd sp!, {lr}
 *   ldrsb r0, [r3], #1
 *   [ldrsb r11, [r5/r7/r9], #1; add r0, r0, r11] * (N-1)
 *   ldrb r0, [r12, r0]; strb r0, [r2], #1; subs r1, r1, #1; bgt loop
 *   mov r11, #0xff; ldmfd sp!, {pc} */
static inline int cgba_sh4_4ch_sum_match(const uint32_t *w, unsigned avail,
                                         uint32_t *words_out)
{
  static const uint32_t ch_ld[3] = {
    0xE0D5B0D1u, 0xE0D7B0D1u, 0xE0D9B0D1u
  };
  unsigned nch;
  if (avail < 8u || w[0] != 0xE92D4000u || w[1] != 0xE0D300D1u)
    return CGBA_SH4_AUDIO_MIXER_NONE;
  for (nch = 4u; nch >= 1u; nch--) {
    unsigned k = 2u * nch;
    unsigned i;
    int ok = 1;
    if (avail < k + 6u)
      continue;
    for (i = 0u; i + 1u < nch; i++) {
      if (w[2u + 2u * i] != ch_ld[i] || w[3u + 2u * i] != 0xE080000Bu) {
        ok = 0;
        break;
      }
    }
    if (!ok)
      continue;
    if (w[k + 0u] == 0xE7DC0000u &&
        w[k + 1u] == 0xE4C20001u &&
        w[k + 2u] == 0xE2511001u &&
        w[k + 3u] == (0xCAFFFFFCu - k) &&
        w[k + 4u] == 0xE3A0B0FFu &&
        w[k + 5u] == 0xE8BD8000u) {
      if (words_out) *words_out = k + 6u;
      return (int)(CGBA_SH4_AUDIO_MIXER_4CH_SUM1 + (nch - 1u));
    }
  }
  return CGBA_SH4_AUDIO_MIXER_NONE;
}

/* 4-channel fixed-point resampling + mix + clamp IWRAM mixer (195 words,
 * e.g. G0226_B6GE).  Uses a callback or flat word array so callers do not
 * need a 780-byte stack buffer on every ARM block prologue. */
static inline int cgba_sh4_4ch_resample_match_words(const uint32_t *w,
                                                    unsigned avail)
{
  static const uint32_t load_ch0[8] = {
    0xE59A0004u, 0xE59A1008u, 0xE59A300Cu, 0xE5DA4033u,
    0xE59A2010u, 0xE1DAC1B6u, 0xE35C0000u, 0x03A02000u
  };
  static const uint32_t load_ch1[8] = {
    0xE59A5004u, 0xE59A6008u, 0xE59A800Cu, 0xE5DA9033u,
    0xE59A7010u, 0xE1DAC1B6u, 0xE35C0000u, 0x03A07000u
  };
  static const uint32_t setup[7] = {
    0xE1DBA4B2u, 0xE004049Au, 0xE009099Au, 0xE59BE048u,
    0xE1DBA4B0u, 0xE35A0000u, 0x0A00009Fu
  };
  static const uint32_t clamp_epilogue[13] = {
    0xE49B6004u, 0xE1A06746u, 0xE356007Fu, 0xC3A0607Fu,
    0xE376007Fu, 0xB3E0607Fu, 0xE4CC6001u, 0xE2577001u,
    0xCAFFFFF6u, 0xE3A00064u, 0xE8BD1FFFu, 0xE8BD4000u,
    0xE12FFF1Eu
  };
  unsigned i;
  if (avail < CGBA_SH4_4CH_RESAMPLE_WORDS)
    return 0;
  if (w[0] != 0xE92D4000u || w[1] != 0xE92D1FFFu || w[2] != 0xE92D0800u ||
      (w[3] & 0xFFFFF000u) != 0xE59F0000u ||
      w[4] != 0xE3A01000u || w[5] != 0xE5801000u ||
      (w[6] & 0xFFFFF000u) != 0xE59FA000u ||
      (w[15] & 0xFFFFF000u) != 0xE59FA000u)
    return 0;
  for (i = 0u; i < 8u; i++) {
    if (w[7u + i] != load_ch0[i] || w[16u + i] != load_ch1[i])
      return 0;
  }
  for (i = 0u; i < 7u; i++) {
    if (w[24u + i] != setup[i])
      return 0;
  }
  /* Loop 1 ch0 & ch1 fast-path core + writeback */
  if (w[31] != 0xE1530001u || w[32] != 0x3A000011u || w[33] != 0xE92D5C00u ||
      (w[46] & 0xFFFFF000u) != 0xE59F0000u ||
      w[47] != 0xE3A01D80u || w[48] != 0xE3A02000u || w[49] != 0xE3A03000u ||
      w[50] != 0xE8BD5C00u || w[51] != 0xE1A0B6A3u || w[52] != 0xE190B0DBu ||
      w[53] != 0xE00C049Bu || w[54] != 0xE0833002u ||
      w[55] != 0xE1580006u || w[56] != 0x3A000011u || w[57] != 0xE92D5C00u ||
      w[75] != 0xE1A0B6A8u || w[76] != 0xE195B0DBu || w[77] != 0xE02CC99Bu ||
      w[78] != 0xE0888007u || w[79] != 0xE48EC004u || w[80] != 0xE25AA001u ||
      w[81] != 0xCAFFFFCCu)
    return 0;
  /* Loop 2 channel loads, accumulate tail, and 32->8 clamp epilogue */
  if ((w[90] & 0xFFFFF000u) != 0xE59FA000u ||
      (w[99] & 0xFFFFF000u) != 0xE59FA000u ||
      w[163] != 0xE59EB000u || w[164] != 0xE08CC00Bu ||
      w[165] != 0xE48EC004u || w[166] != 0xE25AA001u ||
      w[167] != 0xCAFFFFCAu)
    return 0;
  for (i = 0u; i < 13u; i++) {
    if (w[182u + i] != clamp_epilogue[i])
      return 0;
  }
  return 1;
}

static inline int cgba_sh4_audio_mixer_match_words(const uint32_t *w,
                                                   unsigned avail,
                                                   uint32_t *words_out)
{
  if (avail >= 8u && w[0] == 0xE92D4000u) {
    int sk = cgba_sh4_4ch_sum_match(w, avail, words_out);
    if (sk != CGBA_SH4_AUDIO_MIXER_NONE)
      return sk;
  }
  if (avail < 29u)
    return CGBA_SH4_AUDIO_MIXER_NONE;
  if (w[0] == 0xE92D0FF0u && cgba_sh4_gax_std_match(w, avail)) {
    if (words_out) *words_out = CGBA_SH4_GAX_STD_WORDS;
    return CGBA_SH4_AUDIO_MIXER_GAX_STD;
  }
  if (w[0] == 0xE92D4FF0u && cgba_sh4_gax_pingpong_match(w, avail)) {
    if (words_out) *words_out = CGBA_SH4_GAX_PINGPONG_WORDS;
    return CGBA_SH4_AUDIO_MIXER_GAX_PINGPONG;
  }
  if (w[0] == 0xE92D0FF1u) {
    if (cgba_sh4_vv_a_match(w, avail)) {
      if (words_out) *words_out = CGBA_SH4_VV_A_WORDS;
      return CGBA_SH4_AUDIO_MIXER_VV_A;
    }
    if (cgba_sh4_vv_b_match(w, avail)) {
      if (words_out) *words_out = CGBA_SH4_VV_B_WORDS;
      return CGBA_SH4_AUDIO_MIXER_VV_B;
    }
  }
  if (w[0] == 0xE92D5FFEu) {
    int ek = cgba_sh4_eurocom_match(w, avail);
    if (ek != CGBA_SH4_AUDIO_MIXER_NONE) {
      if (words_out)
        *words_out = (ek == CGBA_SH4_AUDIO_MIXER_EUROCOM_INTERP)
                       ? CGBA_SH4_EUROCOM_INTERP_WORDS
                       : CGBA_SH4_EUROCOM_RAW_WORDS;
      return ek;
    }
  }
  return CGBA_SH4_AUDIO_MIXER_NONE;
}

/* ---- Pure state-advance models (host-testable) --------------------------- */

static inline uint32_t cgba_sh4_gax_std_advance(
  uint32_t *r4_io, uint32_t r5, uint32_t r7, uint32_t count)
{
  uint32_t n_iters = ((int32_t)count <= 0) ? 1u : ((count + 3u) >> 2);
  uint32_t samples = n_iters << 2;
  uint32_t r4 = *r4_io, i;
  for (i = 0u; i < samples; i++) {
    int32_t nr = (int32_t)(r4 + r7);
    if (nr >= 0)
      nr = (int32_t)((uint32_t)nr - r5);
    r4 = (uint32_t)nr;
  }
  *r4_io = r4;
  return n_iters;
}

static inline uint32_t cgba_sh4_gax_pingpong_advance(
  uint32_t *r1_io, uint32_t *r4_io, uint32_t r5, uint32_t *r7_io,
  uint32_t count, uint32_t *bounces_out)
{
  uint32_t n_iters = ((int32_t)count <= 0) ? 1u : ((count + 3u) >> 2);
  uint32_t samples = n_iters << 2;
  uint32_t r1 = *r1_io, r4 = *r4_io, r7 = *r7_io;
  uint32_t bounces = 0u, i;
  int dir_neg = ((int32_t)r7 < 0);

  /* Entry w[2..3]: cmp r7, #0; bl w[37] (movmi/addmi/submi/movpl) */
  if (dir_neg) {
    r4 += r5;
    r1 -= (r5 >> 11);
  }
  for (i = 0u; i < samples; i++) {
    int32_t nr = (int32_t)(r4 + r7);
    r4 = (uint32_t)nr;
    if ((!dir_neg && nr >= 0) || (dir_neg && nr < 0)) {
      bounces++;
      r7 = 0u - r7;
      r4 += (r7 << 1);
      if ((int32_t)r7 >= 0) {
        r4 -= r5;
        r1 += (r5 >> 11);
        dir_neg = 0;
      } else {
        r4 += r5;
        r1 -= (r5 >> 11);
        dir_neg = 1;
      }
    }
  }
  *r1_io = r1;
  *r4_io = r4;
  *r7_io = r7;
  if (bounces_out) *bounces_out = bounces;
  return n_iters;
}

static inline int cgba_sh4_vv_advance(
  uint32_t mode, int rev, uint32_t *pos_io, uint32_t end_pos,
  uint32_t count_samples, uint32_t *written_io, uint32_t step,
  uint32_t loop_len, uint32_t *iters_out)
{
  uint32_t r2 = *pos_io;
  uint32_t r4 = count_samples << 1;
  uint32_t r5 = (*written_io) << 1;
  uint32_t iters = 0u;
  uint32_t guard = CGBA_SH4_AUDIO_MIXER_MAX_SAMPLES + 4u;

  if (mode <= 1u) {
    for (;;) {
      iters++;
      if (iters > guard)
        return 0;
      r5 += 2u;
      r2 = rev ? (r2 - step) : (r2 + step);
      if ((int32_t)r5 >= (int32_t)r4)
        break;
      if (rev ? ((int32_t)r2 > (int32_t)end_pos)
              : ((int32_t)r2 < (int32_t)end_pos))
        continue;
      if (loop_len == 0u)
        break;
      r2 -= loop_len;
    }
  } else if (mode == 2u) {
    for (;;) {
      iters++;
      if (iters > guard)
        return 0;
      r5 += 4u;
      r2 += (step << 1);
      if ((int32_t)r2 < (int32_t)end_pos && (int32_t)r5 < (int32_t)r4)
        continue;
      if ((int32_t)r2 > (int32_t)end_pos || (int32_t)r5 > (int32_t)r4) {
        r5 -= 2u;
        r2 -= step;
      }
      break;
    }
  } else {
    return 0;
  }
  *pos_io = r2;
  *written_io = r5 >> 1;
  if (iters_out) *iters_out = iters;
  return 1;
}

/* Eurocom / Factor 5 forward-only 128-sample phase & loop advance.
 * Returns 0 if active (cur_io / frac_io / end_io updated), 1 if channel ended
 * (cur_io / frac_io left untouched, matching ARM routine return r0 = 1). */
static inline int cgba_sh4_eurocom_advance_fwd(
  uint32_t *cur_io, uint32_t *end_io, uint16_t *frac_io, uint16_t step,
  uint32_t loop_start, uint32_t loop_len, uint32_t *samples_done_out)
{
  uint32_t r2 = *cur_io;
  uint32_t r10 = *end_io;
  uint32_t r11 = *frac_io;
  uint32_t lr = step;
  unsigned rem;

  for (rem = 128u; rem > 0u; rem--) {
    r11 += lr;
    if (r11 >= 0x1000u) {
      r2 += (r11 >> 12);
      r11 &= 0x0FFFu;
      if (r2 >= r10) {
        if (loop_start == 0u) {
          if (samples_done_out) *samples_done_out = 129u - rem;
          return 1;
        }
        r2 = (r2 - r10) + loop_start;
        r10 = loop_start + loop_len;
        *end_io = r10;
        if (r2 >= r10)
          r2 = loop_start;
      }
    }
  }
  *cur_io = r2;
  *frac_io = (uint16_t)r11;
  if (samples_done_out) *samples_done_out = 128u;
  return 0;
}

/* Exact O(1)-fast-forward state model for one channel of the 4-channel
 * resampling mixer (CGBA_SH4_AUDIO_MIXER_4CH_RESAMPLE).  Updates src, end,
 * step, pos, and active exactly as the ARM loop does over n_samples samples. */
static inline void cgba_sh4_4ch_chan_advance(
  uint32_t *src_io, uint32_t *end_io, uint32_t *step_io, uint32_t *pos_io,
  uint16_t *active_io, uint32_t silence_ptr, uint8_t loop_flag,
  uint32_t loop_start, uint32_t loop_end, uint32_t n_samples)
{
  uint32_t src = *src_io;
  uint32_t end = *end_io;
  uint32_t step = (*active_io == 0u) ? 0u : *step_io;
  uint32_t pos = *pos_io;
  uint16_t active = *active_io;
  uint32_t rem = n_samples;

  while (rem > 0u) {
    if (pos < end) {
      if (step == 0u)
        break;
      {
        uint32_t gap = end - pos;
        uint32_t k = (gap - 1u) / step + 1u;
        if (k > rem)
          k = rem;
        pos += k * step;
        rem -= k;
      }
    } else {
      if (loop_flag == 0u) {
        active = 0u;
        src = silence_ptr;
        end = 0x2000u;
        step = 0u;
        pos = 0u;
        break;
      }
      end = loop_end;
      pos = loop_start + (pos - loop_end);
      pos += step;
      rem--;
    }
  }
  *src_io = src;
  *end_io = end;
  *step_io = step;
  *pos_io = pos;
  *active_io = active;
}

#endif /* CGBA_SH4_GAX_MIXER_H */
