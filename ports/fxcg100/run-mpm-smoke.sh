#!/usr/bin/env bash
set -euo pipefail

# Experimental OS/MPM harness. Current validation reaches mpmMain, but the
# no-format add-in USB install timing still needs tuning.

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASIO_EMU="${CASIO_EMU:-$HOME/Dev/casio-emu}"
G3A="${1:-$PORT_DIR/build/cgba.g3a}"

FLASH="${CGBA_MPM_FLASH:-/tmp/cgba_fls0_mpm.bin}"
BASE_FLASH="${CGBA_BASE_FLASH:-$HOME/Dev/cg100-flash-gc/fls0_16MiB.bin}"
OSROM="${CGBA_OSROM:-$HOME/Dev/cg100-flash-gc/os200_correct.bin}"
PATCHED_OS="${CGBA_PATCHED_OS:-/tmp/os200_mpm_fixed.bin}"
MPM_BIN="${CGBA_MPM_BIN:-$HOME/Dev/mpm-installer/mpm.bin}"
ADDIN_NAME="${CGBA_ADDIN_NAME:-CGBA.G3A}"
FBDUMP="${CGBA_FBDUMP:-/tmp/cgba-mpm.ppm}"
MPM_INSTALL_EXIT_AT="${CGBA_MPM_INSTALL_EXIT_AT:-2200000}"
ADDIN_INSTALL_EXIT_AT="${CGBA_ADDIN_INSTALL_EXIT_AT:-25000000}"

COMMON_ENV=(
  SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
  SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
  HLE_COLDBOOT=1
  HLE_TURBO=1
  HLE_USB=1
  HLE_USB_ARM_MAIN=1
  HLE_WAKE_PERIOD=80
  HLE_FLASHFILE="$FLASH"
  HLE_USB_SAVE_FLASH="$FLASH"
  HLE_OSROM="$OSROM"
  HLE_CBVRAM=0x8c000000
  HLE_CB_KEYSEQ=48,48,39,39,39,48,48,39
  HLE_CB_KEY_AT=300000
  HLE_CB_KEY_EVERY=20000
  HLE_USB_CABLE_AT=550000
  HLE_USB_FLASH_AT=650000
)

put_file() {
  local file="$1"
  local name="$2"
  local fmt="$3"
  local exit_at="$4"

  if [[ "$fmt" == "1" ]]; then
    env "${COMMON_ENV[@]}" HLE_FMTNOR=1 HLE_EXIT_AT="$exit_at" \
      HLE_USB_PUT_FILE="$file" HLE_USB_PUT_NAME="$name" \
      "$CASIO_EMU/build-hle/calcemu"
  else
    env "${COMMON_ENV[@]}" HLE_EXIT_AT="$exit_at" \
      HLE_USB_PUT_FILE="$file" HLE_USB_PUT_NAME="$name" \
      "$CASIO_EMU/build-hle/calcemu"
  fi
}

cp "$BASE_FLASH" "$FLASH"

cd "$CASIO_EMU"
put_file "$MPM_BIN" MPM.BIN 1 "$MPM_INSTALL_EXIT_AT"
put_file "$G3A" "$ADDIN_NAME" 0 "$ADDIN_INSTALL_EXIT_AT"

env SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" \
  SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}" \
  HLE_COLDBOOT=1 HLE_TURBO=1 HLE_FMTNOR=1 \
  HLE_FLASHFILE="$FLASH" HLE_OSROM="$PATCHED_OS" HLE_CBVRAM=0x8c000000 \
  HLE_MPMWATCH=1 HLE_TLBLOG=1 \
  HLE_CB_KEYSEQ=48,48,39,39,39,48,48,39 \
  HLE_CB_KEY_AT=64000 HLE_CB_KEY_EVERY=9000 \
  HLE_CB_KEY2SEQ=27,31 HLE_CB_KEY2_AT=250000 HLE_CB_KEY2_EVERY=120000 \
  HLE_FBDUMP="$FBDUMP" HLE_EXIT_AT=900000 \
  "$CASIO_EMU/build-hle/calcemu"
