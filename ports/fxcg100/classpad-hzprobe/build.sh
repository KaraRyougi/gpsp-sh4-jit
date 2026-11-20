#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
IMAGE="cgba-fxsdk:14.1-gint-2bc6f7e"

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
command -v docker >/dev/null || { echo "Docker is required." >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo "Start Docker first (e.g. colima start)." >&2; exit 1; }

docker run --rm -v "$ROOT:/workspace" -w /workspace \
  --entrypoint /bin/bash "$IMAGE" -eu -c '
  src=ports/fxcg100/classpad-hzprobe
  bld="$src/build-docker"
  cmake -S "$src" -B "$bld" \
    -DCMAKE_TOOLCHAIN_FILE=/root/.local/lib/cmake/fxsdk/FXCP.cmake \
    -DCMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    -DFXSDK_CMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk
  cmake --build "$bld" --parallel 4
  cp -f "$src/HZProbe.hh3" "$bld/HZProbe.hh3"
  sh-elf-gcc --version | head -1
  ls -lh "$src/HZProbe.hh3"
  sha256sum "$src/HZProbe.hh3"
'

HH3="$HERE/HZProbe.hh3"
command -v xattr   >/dev/null 2>&1 && xattr -c "$HH3" 2>/dev/null || true
command -v chflags >/dev/null 2>&1 && chflags nohidden "$HH3" 2>/dev/null || true

echo
echo "Built: $HH3"
