#!/usr/bin/env bash
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASIO_EMU="${CASIO_EMU:-$HOME/Dev/casio-emu}"
G3A="${1:-$PORT_DIR/build/cgba.g3a}"
BASE_FLASH="${CGBA_BASE_FLASH:-/tmp/fls0_zelda.bin}"
FLASH="${CGBA_FLASH_COPY:-/tmp/cgba_fls0_zelda_$$.bin}"
FBDUMP="${CGBA_FBDUMP:-/tmp/cgba-zelda.ppm}"
LOG="${CGBA_LOG:-/tmp/cgba-zelda.log}"
EXIT_AT="${HLE_EXIT_AT:-1200000}"

if [[ "$G3A" != /* ]]; then
  G3A="$(cd "$(dirname "$G3A")" && pwd)/$(basename "$G3A")"
fi

if [[ ! -r "$BASE_FLASH" ]]; then
  echo "missing base flash image: $BASE_FLASH" >&2
  exit 1
fi

if [[ ! -x "$CASIO_EMU/build-hle/calcemu" ]]; then
  echo "missing emulator: $CASIO_EMU/build-hle/calcemu" >&2
  exit 1
fi

cp "$BASE_FLASH" "$FLASH"
echo "copied $BASE_FLASH -> $FLASH"
echo "log: $LOG"
echo "framebuffer: $FBDUMP"

cd "$CASIO_EMU"
env SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" \
  SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}" \
  HLE_TURBO="${HLE_TURBO:-1}" \
  HLE_FLASHFILE="$FLASH" \
  HLE_BADJUMP="${HLE_BADJUMP:-1}" \
  HLE_MPMWATCH="${HLE_MPMWATCH:-1}" \
  HLE_TLBLOG="${HLE_TLBLOG:-1}" \
  HLE_FBDUMP="$FBDUMP" \
  HLE_EXIT_AT="$EXIT_AT" \
  "$CASIO_EMU/build-hle/calcemu" "$G3A" 2>&1 | tee "$LOG"
