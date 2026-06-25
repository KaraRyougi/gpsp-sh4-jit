#!/usr/bin/env bash
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASIO_EMU="${CASIO_EMU:-$HOME/Dev/casio-emu}"
G3A="${1:-$PORT_DIR/build/cgba.g3a}"
FBDUMP="${HLE_FBDUMP:-/tmp/cgba-hle.ppm}"

cd "$CASIO_EMU"
env SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" \
  SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}" \
  HLE_HB=1 HLE_MPMWATCH=1 HLE_TLBLOG=1 HLE_FBDUMP="$FBDUMP" \
  "$CASIO_EMU/build-hle/calcemu" "$G3A"
