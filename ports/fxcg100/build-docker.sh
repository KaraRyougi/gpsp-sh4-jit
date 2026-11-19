#!/usr/bin/env bash
# Build the playable JIT using a pinned SH GCC/fxSDK/gint container.
# Usage: ports/fxcg100/build-docker.sh [cg100|cg50|classpad]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MODEL="${1:-cg100}"
# TOOLCHAIN: fxSDK CMake toolchain file. FLAGS: model selection. OUT: the
# add-in file CMake leaves next to the sources.
case "$MODEL" in
  cg100)    TOOLCHAIN=FXCG50; FLAGS="-DCGBA_FXCG50=OFF";  OUT=gpSP.g3a ;;
  cg50)     TOOLCHAIN=FXCG50; FLAGS="-DCGBA_FXCG50=ON";   OUT=gpSP.g3a ;;
  classpad) TOOLCHAIN=FXCP;   FLAGS="-DCGBA_CLASSPAD=ON"; OUT=gpSP.hh3 ;;
  *) echo "Usage: $0 [cg100|cg50|classpad]" >&2; exit 2 ;;
esac
# CGBA_SAVE_TIMING=1: a build that shows where a savestate save's time goes,
# in its own build directory (gpSP-<model>-timing.*).
SUFFIX=""
if [ "${CGBA_SAVE_TIMING:-0}" = 1 ]; then
  FLAGS="$FLAGS -DCGBA_SAVE_TIMING=ON"; SUFFIX="-timing"
else
  FLAGS="$FLAGS -DCGBA_SAVE_TIMING=OFF"
fi
IMAGE="cgba-fxsdk:14.1-gint-2bc6f7e"
command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
docker info >/dev/null 2>&1 || {
  echo 'Start Docker first (on this Mac: colima start).' >&2; exit 1;
}
docker build -t "$IMAGE" "$HERE/toolchain"
docker run --rm -v "$ROOT:/workspace" -w /workspace \
  -e CGBA_BUILD_MODEL="$MODEL" -e CGBA_BUILD_TOOLCHAIN="$TOOLCHAIN" \
  -e CGBA_BUILD_FLAGS="$FLAGS" -e CGBA_BUILD_OUT="$OUT" \
  -e CGBA_BUILD_SUFFIX="$SUFFIX" \
  --entrypoint /bin/bash "$IMAGE" -eu -c '
  source_dir=ports/fxcg100/gint-gpsp
  build_dir="$source_dir/build-docker-$CGBA_BUILD_MODEL$CGBA_BUILD_SUFFIX"
  artifact="$build_dir/gpSP-$CGBA_BUILD_MODEL$CGBA_BUILD_SUFFIX.${CGBA_BUILD_OUT##*.}"
  cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE=/root/.local/lib/cmake/fxsdk/$CGBA_BUILD_TOOLCHAIN.cmake \
    -DCMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    -DFXSDK_CMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    $CGBA_BUILD_FLAGS \
    -DCGBA_DYNAREC=ON -DCGBA_GPSP_HEADLESS_TEST=OFF \
    -DCGBA_SH4_ARM_LDST_NATIVE=ON \
    -DCGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE=1048576 \
    -DCGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE=524288
  cmake --build "$build_dir" --parallel 4
  cp "$source_dir/$CGBA_BUILD_OUT" "$artifact"
  sh-elf-gcc --version | head -1
  sha256sum "$artifact"
'
case "$MODEL" in
  classpad) echo "Built: $HERE/gint-gpsp/build-docker-$MODEL$SUFFIX/gpSP-$MODEL$SUFFIX.hh3"
            echo "Copy it to the calculator as gpSP.hh3." ;;
  *)        echo "Built: $HERE/gint-gpsp/build-docker-$MODEL$SUFFIX/gpSP-$MODEL$SUFFIX.g3a" ;;
esac
