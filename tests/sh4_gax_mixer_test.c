/* Host tests for Shin'en GAX, Vicarious Visions / Krawall, and Eurocom /
 * Factor 5 soundless IWRAM audio mixer skip matchers and phase models. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/fxcg100/sh4/sh4_gax_mixer.h"

static unsigned checks;

static void check(int ok, const char *what)
{
  checks++;
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
  }
}

static void test_gax_std_and_pingpong(void)
{
  static const uint32_t gax_std_bcnj[29] = {
    0xE92D0FF0u, 0xE89000FEu, 0xE7D185C4u, 0xE0944007u,
    0x50444005u, 0xE7D195C4u, 0xE0944007u, 0x50444005u,
    0xE1888809u, 0xE0080896u, 0xE3C8B807u, 0xE7D185C4u,
    0xE0944007u, 0x50444005u, 0xE7D195C4u, 0xE0944007u,
    0x50444005u, 0xE1888809u, 0xE0080896u, 0xE3C88807u,
    0xE8920600u, 0xE08990ABu, 0xE08AA0A8u, 0xE8A20600u,
    0xE2533004u, 0xCAFFFFE7u, 0xE580400Cu, 0xE8BD0FF0u,
    0xE12FFF1Eu
  };
  static const uint32_t gax_pp_a9ce[46] = {
    0xE92D4FF0u, 0xE89000FEu, 0xE3570000u, 0xEB000020u,
    0xE7D185C4u, 0xE0944007u, 0x4B000019u, 0xE7D195C4u,
    0xE0944007u, 0x4B000016u, 0xE1888809u, 0xE0080896u,
    0xE3C8B807u, 0xE7D185C4u, 0xE0944007u, 0x4B000010u,
    0xE7D195C4u, 0xE0944007u, 0x4B00000Du, 0xE1888809u,
    0xE0080896u, 0xE3C88807u, 0xE8920600u, 0xE089912Bu,
    0xE08AA128u, 0xE8A20600u, 0xE2533004u, 0xCAFFFFE7u,
    0xE580400Cu, 0xE5807018u, 0xE5801000u, 0xE8BD4FF0u,
    0xE12FFF1Eu, 0xE2777000u, 0xE0844087u, 0x50444005u,
    0x508115A5u, 0x43A0A04Bu, 0x40844005u, 0x404115A5u,
    0x53A0A05Bu, 0xE54FA091u, 0xE54FA089u, 0xE54FA075u,
    0xE54FA06Du, 0xE12FFF1Eu
  };
  uint32_t nw = 0u, buf[46];
  uint32_t r1, r4, r5, r7, bounces = 0u;

  check(cgba_sh4_audio_mixer_match_words(gax_std_bcnj, 29u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_GAX_STD && nw == 29u,
        "match GAX standard mixer (BCNJ lsr #1)");
  memcpy(buf, gax_std_bcnj, sizeof(gax_std_bcnj));
  buf[21] = 0xE089912Bu; buf[22] = 0xE08AA128u; /* A9CE lsr #2 */
  check(cgba_sh4_audio_mixer_match_words(buf, 29u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_GAX_STD, "match GAX standard mixer (A9CE lsr #2)");
  buf[10] ^= 1u;
  check(cgba_sh4_audio_mixer_match_words(buf, 29u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_NONE, "reject corrupted GAX standard mixer");

  check(cgba_sh4_audio_mixer_match_words(gax_pp_a9ce, 46u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_GAX_PINGPONG && nw == 46u,
        "match GAX ping-pong mixer (BLMI state)");
  memcpy(buf, gax_pp_a9ce, sizeof(gax_pp_a9ce));
  buf[6] |= 0x10000000u; buf[9] |= 0x10000000u;
  buf[15] |= 0x10000000u; buf[18] |= 0x10000000u; /* BLPL state */
  check(cgba_sh4_audio_mixer_match_words(buf, 46u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_GAX_PINGPONG, "match GAX ping-pong mixer (BLPL state)");
  buf[40] ^= 1u;
  check(cgba_sh4_audio_mixer_match_words(buf, 46u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_NONE, "reject corrupted GAX ping-pong mixer");

  /* Phase wrap test for GAX std */
  r4 = (uint32_t)(-0x2000);
  r5 = 0x4000u;
  r7 = 0x0C00u;
  check(cgba_sh4_gax_std_advance(&r4, r5, r7, 4u) == 1u, "GAX std 1 iter");
  check((int32_t)r4 == -0x3000, "GAX std wrap exact (-0x2000 + 4*0xC00 - 0x4000)");

  /* Bounce test for GAX ping-pong */
  r1 = 0x08100000u;
  r4 = (uint32_t)(-0x0500);
  r5 = 0x10000u;
  r7 = 0x0400u;
  check(cgba_sh4_gax_pingpong_advance(&r1, &r4, r5, &r7, 4u, &bounces) == 1u,
        "GAX ping-pong 1 iter");
  check(bounces == 1u && (int32_t)r7 == -0x0400 && r1 == 0x08100000u - (r5 >> 11),
        "GAX ping-pong bounce flips r7 and adjusts r1");
}

static void test_vv_and_eurocom(void)
{
  static const uint32_t vv_a[76] = {
    0xE92D0FF1u, 0xE5907018u, 0xE590801Cu, 0xE5901004u,
    0xE5902008u, 0xE590300Cu, 0xE5904010u, 0xE1A04084u,
    0xE5905014u, 0xE1A05085u, 0xE590A020u, 0xE590B024u,
    0xE5900000u, 0xE79FA10Au, 0xE08FF00Au, 0x00000024u,
    0x00000068u, 0x000000C8u, 0xE1A065C2u, 0xE19060D6u,
    0xE0060697u, 0xE1A06446u, 0xE18160B5u, 0xE2855002u,
    0xE0822008u, 0xE1550004u, 0xAA000015u, 0xE1520003u,
    0xBAFFFFF4u, 0xE35B0000u, 0x0A000011u, 0xE042200Bu,
    0xEAFFFFF0u, 0xE1A065C2u, 0xE19060D6u, 0xE0060697u,
    0xE1A06446u, 0xE19190F5u, 0xE0866009u, 0xE18160B5u,
    0xE2855002u, 0xE0822008u, 0xE1550004u, 0xAA000004u,
    0xE1520003u, 0xBAFFFFF2u, 0xE35B0000u, 0x1042200Bu,
    0x1AFFFFEFu, 0xE1A030A5u, 0xE8BD0FF1u, 0xE5802008u,
    0xE5803014u, 0xE1A0000Eu, 0xE12FFF10u, 0xE1A065C2u,
    0xE19060D6u, 0xE0060697u, 0xE1A06446u, 0xE1A06806u,
    0xE1866826u, 0xE7919005u, 0xE0869009u, 0xE7819005u,
    0xE2855004u, 0xE0822088u, 0xE1520003u, 0xB1550004u,
    0xBAFFFFF1u, 0xE1520003u, 0xCA000001u, 0xE1550004u,
    0xDAFFFFE7u, 0xE2455002u, 0xE0422008u, 0xEAFFFFE4u
  };
  static const uint32_t euro_raw[55] = {
    0xE92D5FFEu, 0xE5902004u, 0xE590A008u, 0xE1D0B1B6u,
    0xE1D0E1B4u, 0xE5D04018u, 0xE5D05019u, 0xE3540000u,
    0x12844001u, 0xE3550000u, 0x12855001u, 0xE5D0801Au,
    0xE0188008u, 0x12655000u, 0xE3E08A0Fu, 0xE3A09080u,
    0xE15A0002u, 0x4A000025u, 0xE1D230D0u,
    0xE0060493u, 0xE1D170F0u, 0xE1D1C0F2u, 0xE0877446u,
    0xE0060593u, 0xE08CC446u, 0xE0C170B2u, 0xE0C1C0B2u,
    0xE08BB00Eu, 0xE35B0A01u, 0x3A000003u, 0xE082262Bu,
    0xE00BB008u, 0xE152000Au, 0x2A000006u, 0xE2599001u,
    0x1AFFFFEDu, 0xE5802004u, 0xE1C0B1B6u, 0xE3A00000u,
    0xE8BD5FFEu, 0xE12FFF1Eu, 0xE590C00Cu, 0xE01CC00Cu,
    0x1A000002u, 0xE3A00001u, 0xE8BD5FFEu, 0xE12FFF1Eu,
    0xE042200Au, 0xE082200Cu, 0xE5906010u, 0xE08CA006u,
    0xE580A008u, 0xE152000Au, 0x3AFFFFEBu, 0xE1A0200Cu,
  };
  uint32_t nw = 0u, pos, written, iters = 0u;
  uint32_t cur, endp, done = 0u;
  uint16_t frac;
  uint32_t euro_full[56];

  check(cgba_sh4_audio_mixer_match_words(vv_a, 76u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_VV_A && nw == 76u, "match VV Variant A mixer");

  pos = 0x0000u; written = 0u;
  check(cgba_sh4_vv_advance(1u, 0, &pos, 0x1000u, 10u, &written, 0x300u, 0x800u, &iters),
        "VV mode 1 looping");
  check(written == 10u && iters == 10u && pos == 0x0E00u, "VV mode 1 exact pos/written");

  pos = 0x0000u; written = 0u;
  check(cgba_sh4_vv_advance(2u, 0, &pos, 0x0A00u, 10u, &written, 0x400u, 0u, &iters),
        "VV mode 2 odd-tail");
  check(written == 3u && pos == 0x0C00u, "VV mode 2 stops cleanly on end_pos");

  memcpy(euro_full, euro_raw, sizeof(euro_raw));
  euro_full[55] = 0xEAFFFFE9u;
  check(cgba_sh4_audio_mixer_match_words(euro_full, 56u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_EUROCOM_RAW, "match Eurocom raw mixer");

  /* Eurocom forward advance with wrap and without loop */
  cur = 0x08100000u; endp = 0x08100040u; frac = 0u;
  check(cgba_sh4_eurocom_advance_fwd(&cur, &endp, &frac, 0x1000u,
                                     0x08100010u, 0x30u, &done) == 0,
        "Eurocom looping channel stays active");
  check(done == 128u && endp == 0x08100040u && cur == 0x08100020u && frac == 0u,
        "Eurocom looping wrap exact");

  cur = 0x08100000u; endp = 0x08100010u; frac = 0u;
  check(cgba_sh4_eurocom_advance_fwd(&cur, &endp, &frac, 0x1000u,
                                     0u, 0u, &done) == 1,
        "Eurocom non-looping channel ends");
  check(done == 16u && cur == 0x08100000u && frac == 0u,
        "Eurocom ended channel leaves cur/frac untouched");

  /* Eurocom ARM w[14] mask (0xFFFF0FFF) when step=0xEFFF stays < 0x10000 */
  cur = 0x08100000u; endp = 0x08200000u; frac = 0x0FFFu;
  check(cgba_sh4_eurocom_advance_fwd(&cur, &endp, &frac, 0xEFFFu,
                                     0u, 0u, &done) == 0,
        "Eurocom high-step active");
  check(done == 128u && cur == 0x08100000u + 128u * 15u && frac == 0x0F7Fu,
        "Eurocom high-step exact");
}

static void test_4ch_mixers(void)
{
  static const uint32_t sum4[14] = {
    0xE92D4000u, 0xE0D300D1u, 0xE0D5B0D1u, 0xE080000Bu,
    0xE0D7B0D1u, 0xE080000Bu, 0xE0D9B0D1u, 0xE080000Bu,
    0xE7DC0000u, 0xE4C20001u, 0xE2511001u, 0xCAFFFFF4u,
    0xE3A0B0FFu, 0xE8BD8000u
  };
  static const uint32_t sum1[8] = {
    0xE92D4000u, 0xE0D300D1u, 0xE7DC0000u, 0xE4C20001u,
    0xE2511001u, 0xCAFFFFFAu, 0xE3A0B0FFu, 0xE8BD8000u
  };
  uint32_t bad[14];
  uint32_t nw = 0u;

  check(cgba_sh4_audio_mixer_match_words(sum4, 14u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_4CH_SUM4 && nw == 14u, "match 4ch sum mixer");
  check(cgba_sh4_audio_mixer_match_words(sum1, 8u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_4CH_SUM1 && nw == 8u, "match 1ch sum mixer");
  memcpy(bad, sum4, sizeof(sum4));
  bad[8] ^= 1u;
  check(cgba_sh4_audio_mixer_match_words(bad, 14u, &nw) ==
        CGBA_SH4_AUDIO_MIXER_NONE, "reject mutated 4ch sum mixer");

  /* Exhaustively cross-check cgba_sh4_4ch_chan_advance against step-by-step */
  {
    const uint32_t silence = 0x080F382Cu;
    struct {
      uint32_t src, end, step, pos;
      uint16_t active;
      uint8_t loop;
      uint32_t lstart, lend, n;
    } cases[] = {
      { 0x08010000u, 0x50000u, 0x1800u, 0x0400u, 1u, 0u, 0u, 0u, 20u },
      { 0x08010000u, 0x0A000u, 0x1800u, 0x0400u, 1u, 0u, 0u, 0u, 20u },
      { 0x08010000u, 0x0A000u, 0x1333u, 0x0400u, 1u, 1u, 0x2000u, 0x9000u, 128u },
      { 0x08010000u, 0x0A000u, 0x1800u, 0x0400u, 0u, 0u, 0u, 0u, 20u },
      { 0x08010000u, 0x02000u, 0x0700u, 0x2500u, 1u, 1u, 0x1000u, 0x4000u, 50u },
      { 0x08010000u, 0x02000u, 0x0700u, 0x2500u, 1u, 0u, 0u, 0u, 50u },
      { 0x08010000u, 0x02000u, 0x0000u, 0x0100u, 1u, 1u, 0x0000u, 0x2000u, 16u },
      { 0x08010000u, 0x02000u, 0x0700u, 0x1F00u, 1u, 1u, 0x1000u, 0x2000u, 1u },
      { 0x08010000u, 0xFFFFFFF0u, 0x0100u, 0xFFFFFFE0u, 1u, 0u, 0u, 0u, 10u },
    };
    unsigned ci;
    for (ci = 0u; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
      uint32_t src = cases[ci].src, end = cases[ci].end;
      uint32_t step = cases[ci].step, pos = cases[ci].pos;
      uint16_t act = cases[ci].active;
      uint32_t r_src = src, r_end = end, r_step = act ? step : 0u, r_pos = pos;
      uint16_t r_act = act;
      uint32_t i;

      for (i = 0u; i < cases[ci].n; i++) {
        if (r_pos >= r_end) {
          if (cases[ci].loop) {
            r_end = cases[ci].lend;
            r_pos = cases[ci].lstart + (r_pos - cases[ci].lend);
          } else {
            r_act = 0u;
            r_src = silence;
            r_end = 0x2000u;
            r_step = 0u;
            r_pos = 0u;
          }
        }
        r_pos += r_step;
      }

      cgba_sh4_4ch_chan_advance(&src, &end, &step, &pos, &act, silence,
                                cases[ci].loop, cases[ci].lstart,
                                cases[ci].lend, cases[ci].n);
      check(src == r_src && end == r_end && step == r_step &&
            pos == r_pos && act == r_act,
            "4ch resample channel advance matches step-by-step reference");
    }
  }
}

int main(void)
{
  test_gax_std_and_pingpong();
  test_vv_and_eurocom();
  test_4ch_mixers();
  printf("sh4_gax_mixer_test: all %u checks passed\n", checks);
  return 0;
}
