#!/usr/bin/env bash
# Build the playable JIT using a pinned SH GCC/fxSDK/gint container.
# Usage: ports/fxcg100/build-docker.sh [cg100|cg50]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
MODEL="${1:-cg100}"
case "$MODEL" in
  cg100) CG50=OFF ;;
  cg50) CG50=ON ;;
  *) echo "Usage: $0 [cg100|cg50]" >&2; exit 2 ;;
esac
IMAGE="cgba-fxsdk:14.1-gint-2bc6f7e"
command -v docker >/dev/null || { echo 'Docker is required.' >&2; exit 1; }
docker info >/dev/null 2>&1 || {
  echo 'Start Docker first (on this Mac: colima start).' >&2; exit 1;
}
docker build -t "$IMAGE" "$HERE/toolchain"
docker run --rm -v "$ROOT:/workspace" -w /workspace \
  -e CGBA_BUILD_MODEL="$MODEL" -e CGBA_BUILD_CG50="$CG50" \
  --entrypoint /bin/bash "$IMAGE" -eu -c '
  source_dir=ports/fxcg100/gint-gpsp
  build_dir="$source_dir/build-docker-$CGBA_BUILD_MODEL"
  cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE=/root/.local/lib/cmake/fxsdk/FXCG50.cmake \
    -DCMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    -DFXSDK_CMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    -DCGBA_FXCG50="$CGBA_BUILD_CG50" \
    -DCGBA_DYNAREC=ON -DCGBA_GPSP_HEADLESS_TEST=OFF \
    -DCGBA_SH4_ARM_LDST_NATIVE=ON \
    -DCGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE=1048576 \
    -DCGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE=524288
  cmake --build "$build_dir" --parallel 4
  cp "$source_dir/gpSP.g3a" "$build_dir/gpSP-$CGBA_BUILD_MODEL.g3a"
  sh-elf-gcc --version | head -1
  sha256sum "$build_dir/gpSP-$CGBA_BUILD_MODEL.g3a"
'
echo "Built: $HERE/gint-gpsp/build-docker-$MODEL/gpSP-$MODEL.g3a"
