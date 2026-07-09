(() => {
"use strict";

const MIN_ROM_SIZE = 0xc0;
const MAX_ROM_SIZE = 32 * 1024 * 1024;
const DEFAULT_MIN_PADDING = 4 * 1024;

// FNV-1a of the standard 156-byte GBA logo area after masking the documented
// variable bits at 0x9C and 0x9E. This validates the signature without storing
// or rendering the logo artwork.
const MASKED_LOGO_FNV1A = 0xaf665756;

class RomInspectionError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "RomInspectionError";
    this.code = code;
  }
}

function asBytes(input) {
  if (input instanceof Uint8Array) return input;
  if (input instanceof ArrayBuffer) return new Uint8Array(input);
  if (ArrayBuffer.isView(input)) {
    return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  }
  throw new TypeError("Expected an ArrayBuffer or byte array.");
}

function maskedLogoHash(bytes) {
  let hash = 0x811c9dc5;

  for (let offset = 0x04; offset < 0xa0; offset += 1) {
    let value = bytes[offset];
    if (offset === 0x9c) value &= 0x7b;
    if (offset === 0x9e) value &= 0xfc;
    hash = Math.imul(hash ^ value, 0x01000193) >>> 0;
  }

  return hash;
}

function calculateHeaderChecksum(bytes) {
  let sum = 0;
  for (let offset = 0xa0; offset <= 0xbc; offset += 1) {
    sum = (sum + bytes[offset]) & 0xff;
  }
  return (-0x19 - sum) & 0xff;
}

function readHeaderText(bytes, start, end) {
  let value = "";
  for (let offset = start; offset < end; offset += 1) {
    const byte = bytes[offset];
    if (byte === 0) break;
    value += byte >= 0x20 && byte <= 0x7e ? String.fromCharCode(byte) : "?";
  }
  return value.trim();
}

function validateGbaHeader(input) {
  const bytes = asBytes(input);
  if (bytes.byteLength < MIN_ROM_SIZE) {
    return {
      valid: false,
      logoValid: false,
      fixedValueValid: false,
      checksumValid: false,
      entryPointLikely: false,
      storedChecksum: null,
      calculatedChecksum: null,
    };
  }

  const calculatedChecksum = calculateHeaderChecksum(bytes);
  const storedChecksum = bytes[0xbd];
  const logoValid = maskedLogoHash(bytes) === MASKED_LOGO_FNV1A;
  const fixedValueValid = bytes[0xb2] === 0x96;
  const checksumValid = storedChecksum === calculatedChecksum;

  return {
    valid: logoValid && fixedValueValid && checksumValid,
    logoValid,
    fixedValueValid,
    checksumValid,
    entryPointLikely: bytes[0x03] === 0xea,
    storedChecksum,
    calculatedChecksum,
  };
}

function looksLikeArchive(input) {
  const bytes = asBytes(input);
  const startsWith = (...signature) =>
    bytes.byteLength >= signature.length &&
    signature.every((value, index) => bytes[index] === value);

  return (
    startsWith(0x50, 0x4b, 0x03, 0x04) ||
    startsWith(0x1f, 0x8b) ||
    startsWith(0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c) ||
    startsWith(0x52, 0x61, 0x72, 0x21)
  );
}

function findTrimPlan(input, minPaddingBytes = DEFAULT_MIN_PADDING) {
  const bytes = asBytes(input);
  const originalSize = bytes.byteLength;

  if (originalSize <= MIN_ROM_SIZE) {
    return {
      status: "compact",
      reason: "minimum-size",
      originalSize,
      trimmedSize: originalSize,
      savedSize: 0,
      paddingByte: null,
      uniformPaddingBytes: 0,
      dataEnd: originalSize,
    };
  }

  const paddingByte = bytes[originalSize - 1];
  if (paddingByte !== 0x00 && paddingByte !== 0xff) {
    return {
      status: "compact",
      reason: "non-padding-tail",
      originalSize,
      trimmedSize: originalSize,
      savedSize: 0,
      paddingByte: null,
      uniformPaddingBytes: 0,
      dataEnd: originalSize,
    };
  }

  let dataEnd = originalSize;
  while (dataEnd > MIN_ROM_SIZE && bytes[dataEnd - 1] === paddingByte) {
    dataEnd -= 1;
  }

  const uniformPaddingBytes = originalSize - dataEnd;
  const alignedEnd = Math.min(originalSize, Math.ceil(dataEnd / 4) * 4);
  const savedSize = originalSize - alignedEnd;

  if (uniformPaddingBytes < minPaddingBytes || savedSize <= 0) {
    return {
      status: "compact",
      reason: "padding-below-threshold",
      originalSize,
      trimmedSize: originalSize,
      savedSize: 0,
      paddingByte,
      uniformPaddingBytes,
      dataEnd,
    };
  }

  return {
    status: "ready",
    reason: "uniform-padding",
    originalSize,
    trimmedSize: alignedEnd,
    savedSize,
    paddingByte,
    uniformPaddingBytes,
    dataEnd,
  };
}

function inspectRom(input, options = {}) {
  const bytes = asBytes(input);
  const { allowUnverified = false, minPaddingBytes = DEFAULT_MIN_PADDING } = options;

  if (looksLikeArchive(bytes)) {
    throw new RomInspectionError("archive", "Compressed archives must be unpacked first.");
  }
  if (bytes.byteLength < MIN_ROM_SIZE) {
    throw new RomInspectionError("too-small", "The file is smaller than a GBA header.");
  }
  if (bytes.byteLength > MAX_ROM_SIZE) {
    throw new RomInspectionError("too-large", "The file is larger than the GBA ROM window.");
  }

  const header = validateGbaHeader(bytes);
  if (!header.valid && !allowUnverified) {
    throw new RomInspectionError("nonstandard-header", "The standard GBA header checks failed.");
  }

  const plan = findTrimPlan(bytes, minPaddingBytes);
  const title = readHeaderText(bytes, 0xa0, 0xac) || "Untitled ROM";
  const gameCode = readHeaderText(bytes, 0xac, 0xb0) || "—";
  const makerCode = readHeaderText(bytes, 0xb0, 0xb2) || "—";

  return {
    ...plan,
    title,
    gameCode,
    makerCode,
    header,
    verified: header.valid,
  };
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(2)} KiB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MiB`;
}

function formatExactBytes(bytes) {
  return `${new Intl.NumberFormat("en-US").format(bytes)} bytes`;
}

function formatHex(value, width = 8) {
  if (!Number.isInteger(value) || value < 0) return "—";
  return `0x${value.toString(16).toUpperCase().padStart(width, "0")}`;
}

function makeTrimmedFilename(filename) {
  const cleanName = String(filename || "rom").split(/[\\/]/).pop() || "rom";
  const base = cleanName.replace(/\.[^.]*$/, "") || "rom";
  return `${base}.trimmed.gba`;
}

globalThis.RomTrimmer = Object.freeze({
  MIN_ROM_SIZE,
  MAX_ROM_SIZE,
  DEFAULT_MIN_PADDING,
  RomInspectionError,
  validateGbaHeader,
  looksLikeArchive,
  findTrimPlan,
  inspectRom,
  formatBytes,
  formatExactBytes,
  formatHex,
  makeTrimmedFilename,
});
})();
