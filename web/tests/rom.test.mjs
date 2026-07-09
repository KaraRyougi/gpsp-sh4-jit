import assert from "node:assert/strict";
import test from "node:test";

import "../rom.js";

const {
  DEFAULT_MIN_PADDING,
  MAX_ROM_SIZE,
  MIN_ROM_SIZE,
  RomInspectionError,
  findTrimPlan,
  formatBytes,
  formatHex,
  inspectRom,
  looksLikeArchive,
  makeTrimmedFilename,
  validateGbaHeader,
} = globalThis.RomTrimmer;

function makeSyntheticRom({ dataBytes = 5_123, padByte = 0xff, padBytes = 8_192 } = {}) {
  const bytes = new Uint8Array(MIN_ROM_SIZE + dataBytes + padBytes);
  bytes.fill(0x4d);
  bytes.fill(padByte, MIN_ROM_SIZE + dataBytes);
  bytes[MIN_ROM_SIZE + dataBytes - 1] = 0x3c;
  return bytes;
}

test("findTrimPlan removes a large 0xFF suffix and aligns upward to four bytes", () => {
  const rom = makeSyntheticRom({ dataBytes: 5_123, padByte: 0xff, padBytes: 8_192 });
  const plan = findTrimPlan(rom);
  const rawEnd = MIN_ROM_SIZE + 5_123;
  const expectedEnd = Math.ceil(rawEnd / 4) * 4;

  assert.equal(plan.status, "ready");
  assert.equal(plan.paddingByte, 0xff);
  assert.equal(plan.uniformPaddingBytes, 8_192);
  assert.equal(plan.trimmedSize, expectedEnd);
  assert.equal(plan.trimmedSize % 4, 0);
});

test("findTrimPlan removes a large 0x00 suffix", () => {
  const rom = makeSyntheticRom({ padByte: 0x00 });
  const plan = findTrimPlan(rom);

  assert.equal(plan.status, "ready");
  assert.equal(plan.paddingByte, 0x00);
  assert.ok(plan.savedSize >= DEFAULT_MIN_PADDING);
});

test("a non-padding final byte is left untouched", () => {
  const rom = makeSyntheticRom();
  rom[rom.length - 1] = 0x7e;
  const plan = findTrimPlan(rom);

  assert.equal(plan.status, "compact");
  assert.equal(plan.reason, "non-padding-tail");
  assert.equal(plan.savedSize, 0);
});

test("a padding run below the conservative threshold is left untouched", () => {
  const rom = makeSyntheticRom({ padBytes: DEFAULT_MIN_PADDING - 1 });
  const plan = findTrimPlan(rom);

  assert.equal(plan.status, "compact");
  assert.equal(plan.reason, "padding-below-threshold");
  assert.equal(plan.uniformPaddingBytes, DEFAULT_MIN_PADDING - 1);
});

test("internal padding is retained", () => {
  const prefix = new Uint8Array(MIN_ROM_SIZE + 600);
  prefix.fill(0x42);
  const internalPadding = new Uint8Array(5_000).fill(0xff);
  const laterData = new Uint8Array([0x11, 0x22, 0x33, 0x44]);
  const finalPadding = new Uint8Array(5_000).fill(0xff);
  const rom = new Uint8Array(
    prefix.length + internalPadding.length + laterData.length + finalPadding.length,
  );
  let offset = 0;
  for (const part of [prefix, internalPadding, laterData, finalPadding]) {
    rom.set(part, offset);
    offset += part.length;
  }

  const plan = findTrimPlan(rom);
  assert.equal(plan.status, "ready");
  assert.equal(plan.trimmedSize, prefix.length + internalPadding.length + laterData.length);
  assert.deepEqual(
    rom.slice(prefix.length, prefix.length + internalPadding.length),
    internalPadding,
  );
});

test("a mixed 0x00/0xFF tail removes only the final uniform run", () => {
  const body = new Uint8Array(MIN_ROM_SIZE + 400).fill(0x51);
  const zeros = new Uint8Array(6_000).fill(0x00);
  const ff = new Uint8Array(6_000).fill(0xff);
  const rom = new Uint8Array(body.length + zeros.length + ff.length);
  rom.set(body);
  rom.set(zeros, body.length);
  rom.set(ff, body.length + zeros.length);

  const plan = findTrimPlan(rom);
  assert.equal(plan.paddingByte, 0xff);
  assert.equal(plan.uniformPaddingBytes, ff.length);
  assert.equal(plan.trimmedSize, body.length + zeros.length);
});

test("the proposed output is an exact prefix of the input", () => {
  const rom = makeSyntheticRom();
  const plan = findTrimPlan(rom);
  const output = rom.slice(0, plan.trimmedSize);

  assert.deepEqual(output, rom.subarray(0, plan.trimmedSize));
  assert.equal(output.length, plan.trimmedSize);
});

test("inspectRom supports an explicit override for non-standard homebrew headers", () => {
  const rom = makeSyntheticRom();
  rom.set(new TextEncoder().encode("SAFE <ROM>  "), 0xa0);
  rom.set(new TextEncoder().encode("T123"), 0xac);

  assert.throws(
    () => inspectRom(rom),
    (error) => error instanceof RomInspectionError && error.code === "nonstandard-header",
  );

  const analysis = inspectRom(rom, { allowUnverified: true });
  assert.equal(analysis.verified, false);
  assert.equal(analysis.title, "SAFE <ROM>");
  assert.equal(analysis.gameCode, "T123");
});

test("short, oversized, and archived inputs are rejected", () => {
  assert.throws(
    () => inspectRom(new Uint8Array(MIN_ROM_SIZE - 1), { allowUnverified: true }),
    (error) => error.code === "too-small",
  );

  assert.throws(
    () => inspectRom(new Uint8Array(MAX_ROM_SIZE + 1), { allowUnverified: true }),
    (error) => error.code === "too-large",
  );

  const archive = new Uint8Array(MIN_ROM_SIZE);
  archive.set([0x50, 0x4b, 0x03, 0x04]);
  assert.equal(looksLikeArchive(archive), true);
  assert.throws(
    () => inspectRom(archive, { allowUnverified: true }),
    (error) => error.code === "archive",
  );
});

test("header validation reports individual checks", () => {
  const header = new Uint8Array(MIN_ROM_SIZE);
  header[0x03] = 0xea;
  header[0xb2] = 0x96;
  const checks = validateGbaHeader(header);

  assert.equal(checks.entryPointLikely, true);
  assert.equal(checks.fixedValueValid, true);
  assert.equal(checks.logoValid, false);
  assert.equal(checks.valid, false);
});

test("formatters and output names are stable", () => {
  assert.equal(formatBytes(0), "0 B");
  assert.equal(formatBytes(16 * 1024 * 1024), "16.00 MiB");
  assert.equal(formatHex(0xe3cf63), "0x00E3CF63");
  assert.equal(makeTrimmedFilename("Emerald.gba"), "Emerald.trimmed.gba");
  assert.equal(makeTrimmedFilename("folder/ROM.BIN"), "ROM.trimmed.gba");
});
