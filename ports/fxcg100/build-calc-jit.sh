#!/usr/bin/env bash
#
# Build a *playable* JIT (SH4 dynarec) .g3a add-in for the fx-CG100.
#
#   Usage:  ports/fxcg100/build-calc-jit.sh [build-dir]
#   Output: ports/fxcg100/gint-gpsp/CGBA-GPSP.g3a
#
# This is the real, hand-held build (NOT the headless emulator test harness):
#   - CGBA_DYNAREC=ON              the SH4 dynamic recompiler (the "JIT")
#   - CGBA_GPSP_HEADLESS_TEST=OFF  normal menu + ROM picker + live input
#   - Thumb-LDST native ON, ARM-LDST native OFF, 512K/256K translation cache
#     (the configuration the new-game freeze fix was verified against).
#
# Copy CGBA-GPSP.g3a to the calculator's main memory and put a GBA ROM named
# GAME.GBA alongside it (or use the in-app ROM picker).
#
# Override the fxSDK location with FXSDK_PREFIX=/path if it is not auto-found.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GG="$HERE/gint-gpsp"
BUILD="${1:-build-calc-jit}"

# Locate the fxSDK install prefix.
PREFIX="${FXSDK_PREFIX:-}"
if [ -z "$PREFIX" ]; then
  if command -v fxsdk >/dev/null 2>&1; then
    PREFIX="$(cd "$(dirname "$(command -v fxsdk)")/.." && pwd)"
  else
    PREFIX="$HOME/.local"
  fi
fi
TC="$PREFIX/lib/cmake/fxsdk/FXCG50.cmake"
[ -f "$TC" ] || { echo "error: fxSDK toolchain not found at $TC (set FXSDK_PREFIX)" >&2; exit 1; }

JOBS="$( (sysctl -n hw.ncpu 2>/dev/null) || (nproc 2>/dev/null) || echo 4 )"

cmake -S "$GG" -B "$GG/$BUILD" \
  -DCMAKE_MODULE_PATH="$PREFIX/lib/cmake/fxsdk" \
  -DCMAKE_TOOLCHAIN_FILE="$TC" \
  -DFXSDK_CMAKE_MODULE_PATH="$PREFIX/lib/cmake/fxsdk" \
  -DCGBA_DYNAREC=ON
cmake --build "$GG/$BUILD" -j"$JOBS"

G3A="$(find "$GG/$BUILD" "$GG" -maxdepth 1 -name 'CGBA-GPSP.g3a' 2>/dev/null | head -1)"
echo
echo "Built playable JIT add-in: ${G3A:-$GG/CGBA-GPSP.g3a}"
