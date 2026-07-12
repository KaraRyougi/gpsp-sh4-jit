#!/usr/bin/env bash
#
# Build a *playable* JIT (SH4 dynarec) .g3a add-in for the fx-CG100.
#
#   Usage:  ports/fxcg100/build-calc-jit.sh [build-dir]
#   Output: ports/fxcg100/gint-gpsp/gpSP.g3a
#
# This is the real, hand-held build (NOT the headless emulator test harness):
#   - CGBA_DYNAREC=ON              the SH4 dynamic recompiler (the "JIT")
#   - CGBA_GPSP_HEADLESS_TEST=OFF  normal menu + ROM picker + live input
#   - Thumb LDST/block native ON, ARM-LDST native ON, 1024K/512K translation
#     cache, 12-bit (16 KiB) branch hash — the perf configuration.
#
# Copy gpSP.g3a to the calculator's main memory and put a GBA ROM named
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

# Perf config (see docs/sh4-jit-optimization-plan.md / the HW-perf diagnosis):
#  - ARM_LDST_NATIVE=ON  : native ARM load/store, avoids per-op far-call into a
#    NOR C helper (verified pixel-identical to the interpreter).
#  - ROM_BRANCH_HASH_BITS : defaults to 12 (16KB) in CMakeLists so the branch hash
#    fits the SH7305 cache instead of thrashing it at the upstream 16 (256KB).
#    Sweep with -DCGBA_GPSP_ROM_BRANCH_HASH_BITS=N below.
cmake -S "$GG" -B "$GG/$BUILD" \
  -DCMAKE_MODULE_PATH="$PREFIX/lib/cmake/fxsdk" \
  -DCMAKE_TOOLCHAIN_FILE="$TC" \
  -DFXSDK_CMAKE_MODULE_PATH="$PREFIX/lib/cmake/fxsdk" \
  -DCGBA_DYNAREC=ON \
  -DCGBA_SH4_ARM_LDST_NATIVE=ON \
  -DCGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE=1048576 \
  -DCGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE=524288
cmake --build "$GG/$BUILD" -j"$JOBS"

G3A="$(find "$GG/$BUILD" "$GG" -maxdepth 1 -name 'gpSP.g3a' 2>/dev/null | head -1)"
[ -n "$G3A" ] || { echo "error: gpSP.g3a was not produced" >&2; exit 1; }

# Make it visible on macOS. fxSDK's mkg3a marks the .g3a with the 'hidden' file
# flag, and this checkout may live under a dot-directory (e.g. .claude/worktrees)
# that Finder hides too -- either way you can't drag it to the calculator. Clear
# the flag and drop a clean, visible copy where you can reach it (override the
# destination with CGBA_G3A_OUT=/path).
command -v chflags >/dev/null 2>&1 && chflags nohidden "$G3A" 2>/dev/null || true
OUT_DIR="${CGBA_G3A_OUT:-$HOME/Desktop}"
[ -d "$OUT_DIR" ] || OUT_DIR="$HOME"
OUT="$OUT_DIR/gpSP.g3a"
cp -f "$G3A" "$OUT"
command -v xattr   >/dev/null 2>&1 && xattr -c "$OUT" 2>/dev/null || true
command -v chflags >/dev/null 2>&1 && chflags nohidden "$OUT" 2>/dev/null || true

echo
echo "Built playable JIT add-in:"
echo "  build output : $G3A"
echo "  VISIBLE copy : $OUT   <- drag THIS to the calculator"
