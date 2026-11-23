/* Host test for ports/fxcg100/state_codec.h:
 *   - CZS2 (LZ4 block) round-trip across zeros, structured GBA-like state,
 *     overlapping matches, and incompressible PRNG data
 *   - Trailing zero-padding tolerance (bucketed file sizes)
 *   - Corruption / truncation rejection
 *   - Legacy CZS1 (word-RLE) backward-compatible decoding
 *   - 16 KiB bucketing + existing-size reuse policy */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ports/fxcg100/state_codec.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		failures++; \
	} \
} while (0)

#define RAW_SZ (416u * 1024u)
#define MAX_SZ (448u * 1024u)

static uint8_t raw_in[RAW_SZ];
static uint8_t comp_buf[MAX_SZ];
static uint8_t raw_out[RAW_SZ];
static uint32_t hash_tab[CGBA_STATE_LZ4_HASH_LEN];

static void verify_lz4_roundtrip(const char *label, unsigned max_expected_bytes)
{
	unsigned csz = cgba_state_compress_lz4(raw_in, RAW_SZ, comp_buf, MAX_SZ,
		hash_tab);

	CHECK(csz > CGBA_STATE_LZ4_HDR_SZ, "%s: compress returned %u", label, csz);
	if (max_expected_bytes)
		CHECK(csz <= max_expected_bytes, "%s: compressed %u > expected %u",
			label, csz, max_expected_bytes);

	/* Exact size decode */
	memset(raw_out, 0xA5, sizeof(raw_out));
	CHECK(cgba_state_decompress_any(comp_buf, csz, raw_out, RAW_SZ),
		"%s: exact decode failed", label);
	CHECK(memcmp(raw_in, raw_out, RAW_SZ) == 0,
		"%s: exact roundtrip mismatch", label);

	/* Bucket-padded decode (trailing zeros ignored) */
	if (csz + 16384u <= MAX_SZ) {
		unsigned padded = (csz + 0x3fffu) & ~0x3fffu;
		memset(comp_buf + csz, 0, padded - csz);
		memset(raw_out, 0x5A, sizeof(raw_out));
		CHECK(cgba_state_decompress_any(comp_buf, padded, raw_out, RAW_SZ),
			"%s: padded decode failed", label);
		CHECK(memcmp(raw_in, raw_out, RAW_SZ) == 0,
			"%s: padded roundtrip mismatch", label);
	}
}

static void test_lz4_patterns(void)
{
	unsigned i;
	uint32_t lfsr = 0x12345678u;

	/* 1. All zeros (extreme overlapping match run) */
	memset(raw_in, 0, sizeof(raw_in));
	verify_lz4_roundtrip("all-zeros", 4096u);

	/* 2. Synthetic GBA-like state: 256 KiB EWRAM (sparse + repeating tile/struct
	 * patterns), 32 KiB IWRAM, 96 KiB VRAM (tilemap runs), 32 KiB regs/misc. */
	memset(raw_in, 0, sizeof(raw_in));
	for (i = 0; i < 64u * 1024u; i++)
		raw_in[i] = (uint8_t)((i % 37u) ^ ((i >> 8) & 0x0fu));
	for (i = 128u * 1024u; i < 160u * 1024u; i++) {
		lfsr = lfsr * 1664525u + 1013904223u;
		raw_in[i] = (uint8_t)(lfsr >> 24);
	}
	for (i = 256u * 1024u; i < 352u * 1024u; i += 4u) {
		raw_in[i + 0u] = (uint8_t)(i >> 10);
		raw_in[i + 1u] = 0x11u;
		raw_in[i + 2u] = (uint8_t)((i >> 2) & 0x07u);
		raw_in[i + 3u] = 0x00u;
	}
	verify_lz4_roundtrip("gba-like", 100u * 1024u);

	/* 3. Pure incompressible PRNG noise (must still round-trip safely). */
	for (i = 0; i < RAW_SZ; i++) {
		lfsr = lfsr * 1664525u + 1013904223u;
		raw_in[i] = (uint8_t)(lfsr >> 24);
	}
	verify_lz4_roundtrip("prng-noise", MAX_SZ);

	/* 4. Tight output cap returns 0 without overflowing. */
	CHECK(cgba_state_compress_lz4(raw_in, RAW_SZ, comp_buf, 1024u, hash_tab) == 0u,
		"tight cap must fail cleanly");
}

static void test_corruption_and_legacy_czs1(void)
{
	/* Build a valid CZS2 stream, then test truncated / bad header cases. */
	unsigned csz;
	memset(raw_in, 0x3Cu, sizeof(raw_in));
	raw_in[0] = 0x01u;
	raw_in[RAW_SZ - 1u] = 0xFEu;
	csz = cgba_state_compress_lz4(raw_in, RAW_SZ, comp_buf, MAX_SZ, hash_tab);
	CHECK(csz > 12u, "setup CZS2");
	CHECK(!cgba_state_decompress_any(comp_buf, csz - 1u, raw_out, RAW_SZ),
		"truncated CZS2 rejected");
	CHECK(!cgba_state_decompress_any(comp_buf, csz, raw_out, RAW_SZ - 4u),
		"wrong raw_size rejected");
	comp_buf[0] ^= 0xffu;
	CHECK(!cgba_state_decompress_any(comp_buf, csz, raw_out, RAW_SZ),
		"bad magic rejected");

	/* Build a legacy CZS1 (word-RLE) stream and verify backward compatibility. */
	{
		uint8_t rle[32];
		unsigned words = RAW_SZ / 4u;

		cgba_codec_store32(rle + 0u, CGBA_STATE_MAGIC_RLE);
		cgba_codec_store32(rle + 4u, RAW_SZ);
		cgba_codec_store32(rle + 8u, 0x80000000u | (words - 2u));
		cgba_codec_store32(rle + 12u, 0x11223344u);
		cgba_codec_store32(rle + 16u, 2u); /* 2 literal words */
		cgba_codec_store32(rle + 20u, 0xAABBCCDDu);
		cgba_codec_store32(rle + 24u, 0x55667788u);

		memset(raw_out, 0, sizeof(raw_out));
		CHECK(cgba_state_decompress_any(rle, 28u, raw_out, RAW_SZ),
			"legacy CZS1 decode succeeded");
		CHECK(cgba_codec_load32(raw_out) == 0x11223344u &&
			cgba_codec_load32(raw_out + RAW_SZ - 8u) == 0xAABBCCDDu &&
			cgba_codec_load32(raw_out + RAW_SZ - 4u) == 0x55667788u,
			"legacy CZS1 payload matches");
	}
}

static void test_bucketing_policy(void)
{
	const unsigned bucket = 0x4000u; /* 16 KiB */

	/* New slot (-1): rounds up to 16 KiB buckets. */
	CHECK(cgba_state_choose_file_size(50000u, -1, RAW_SZ, MAX_SZ, bucket) == 65536u,
		"new slot rounds to 16 KiB");
	CHECK(cgba_state_choose_file_size(70000u, -1, RAW_SZ, MAX_SZ, bucket) == 81920u,
		"new slot 70K -> 80K");

	/* Existing slot within 64 KiB slack: reuses existing size for in-place diff. */
	CHECK(cgba_state_choose_file_size(70000u, 114688, RAW_SZ, MAX_SZ, bucket) == 114688u,
		"reuses existing size within 64 KiB slack");

	/* Existing slot much larger (> 64 KiB slack, e.g. old CZS1 256K -> new LZ4 80K):
	 * shrinks to new 16 KiB bucket. */
	CHECK(cgba_state_choose_file_size(70000u, 262144, RAW_SZ, MAX_SZ, bucket) == 81920u,
		"shrinks when slack exceeds 64 KiB");

	/* Existing slot too small: grows to new 16 KiB bucket. */
	CHECK(cgba_state_choose_file_size(90000u, 81920, RAW_SZ, MAX_SZ, bucket) == 98304u,
		"grows when stream exceeds existing size");

	/* Incompressible / >= raw_size falls back to raw_size. */
	CHECK(cgba_state_choose_file_size(0u, 65536, RAW_SZ, MAX_SZ, bucket) == RAW_SZ,
		"zero falls back to raw");
	CHECK(cgba_state_choose_file_size(RAW_SZ - 100u, -1, RAW_SZ, MAX_SZ, bucket) == RAW_SZ,
		"rounded >= raw falls back to raw");
}

static void test_lz4_trailing_capacity_bounds(void)
{
	/* Incompressible lengths 15 and 270 exercise the exact literal-length
	 * extension boundaries (15 => +1 extra len byte; 270 => +2 extra len
	 * bytes).  Verify cap == exact succeeds, cap == exact - 1 fails cleanly,
	 * and neither writes past out[cap - 1]. */
	static const unsigned lens[] = { 15u, 270u };

	for (unsigned k = 0; k < 2; k++) {
		unsigned n = lens[k];
		uint8_t src[300];
		uint8_t out[320];
		uint8_t dec[300];
		uint32_t s = 0x13579BDFu + k * 0x10101u;
		for (unsigned i = 0; i < n; i++) {
			s = s * 1664525u + 1013904223u;
			src[i] = (uint8_t)(s >> 24);
		}
		unsigned exact = cgba_state_compress_lz4(src, n, out, sizeof(out), hash_tab);
		CHECK(exact > CGBA_STATE_LZ4_HDR_SZ,
			"exact compressed size valid for trailing boundary len=%u", n);

		/* Tight exact capacity must succeed without touching canary at out[exact]. */
		memset(out, 0xCC, sizeof(out));
		unsigned got_exact = cgba_state_compress_lz4(src, n, out, exact, hash_tab);
		CHECK(got_exact == exact, "exact cap=%u succeeds for len=%u (got %u)",
			exact, n, got_exact);
		CHECK(out[exact] == 0xCC, "canary intact at exact cap for len=%u", n);
		CHECK(cgba_state_decompress_any(out, got_exact, dec, n) == 1 &&
			memcmp(src, dec, n) == 0,
			"roundtrip exact cap for len=%u", n);

		/* One byte short must fail cleanly without writing out[exact - 1]. */
		memset(out, 0xCC, sizeof(out));
		unsigned got_short = cgba_state_compress_lz4(src, n, out, exact - 1u, hash_tab);
		CHECK(got_short == 0u, "cap-1=%u must reject len=%u (got %u)",
			exact - 1u, n, got_short);
		CHECK(out[exact - 1u] == 0xCC && out[exact] == 0xCC,
			"no OOB write at cap-1 for len=%u", n);
	}
}

int main(void)
{
	test_lz4_patterns();
	test_corruption_and_legacy_czs1();
	test_bucketing_policy();
	test_lz4_trailing_capacity_bounds();

	if (failures)
		return 1;
	printf("state_codec_test: all checks passed\n");
	return 0;
}
