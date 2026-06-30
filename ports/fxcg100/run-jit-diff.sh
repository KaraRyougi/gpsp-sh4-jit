#!/usr/bin/env bash
# Differential JIT-accuracy harness for the fx-CG100 SH4 dynarec.
#
# Builds two headless casio-emu add-ins that differ ONLY in the active core
# (interpreter vs SH4 dynarec), runs the same GBA ROM through both, and diffs
# the per-frame region hashes (@@CGBA_HASH lines emitted by the headless test)
# to report the first frame at which the dynarec state diverges from the
# interpreter oracle.
#
# It also runs the in-emulator lockstep block diff (cgba_sh4_diff_blocks_here)
# in a separate diagnostic emulator pass. The block diff compares the two cores
# block-by-block from the selected live state and reports the first block whose
# registers / memory / retired cycles differ; keeping it separate prevents the
# destructive single-step diagnostic from contaminating the frame-hash A/B run.
#
# The interpreter is the oracle: any divergence is a dynarec bug.
#
#   Usage:  ports/fxcg100/run-jit-diff.sh [/path/to/rom.gba]
#
#   Env knobs (defaults in parens):
#     FRAMES(600) STATE_EVERY(5) START_FRAME(30) START_HOLD(6)
#     SHIFT_FRAME(200) SHIFT_HOLD(FRAMES) SHIFT_PERIOD(200) SHIFT_PRESS(2)
#       Presses calculator SHIFT's default GBA binding (A) on exact frame counts.
#       Legacy A_FRAME/A_HOLD/A_PERIOD/A_PRESS aliases are still accepted.
#     DIFF_FRAME(120) DIFF_BLOCKS(256)        # in-emu lockstep block diff
#     DIFF_DUMP_OPS(OFF)                      # dump block ops/cycle trace
#     WINDOW_DIFF_FRAME(-1)                   # preserving one-frame diff
#     DUMP_EVERY(0)                           # dump RGB565 framebuffer hex
#     THUMB_LDST_NATIVE(ON)                   # toggle native Thumb byte LDR fast path
#     EXACT_CYCLES(ON)                        # harness-only Thumb instruction
#                                                boundary checks for cycle diffing
#     TRACE_PC(0) TRACE_MASK(4095) TRACE_JIT(0)
#                                                targeted helper/JIT debug trace
#     TRACE_TIMER_IO(0)                          timer register write trace
#     SCREEN_ONLY(OFF)                           verdict compares only the
#                                                rendered GBA framebuffer hash
#     SECS_INTERP(240) SECS_JIT(240) SECS_DIFF(SECS_INTERP)
#                                                wall-clock caps per run
#     CASIO_EMU(~/Dev/casio-emu)              # uses build-hle/calcemu
#     CALCEMU($CASIO_EMU/build-hle/calcemu)   # direct emulator binary override
#     FXSDK_PREFIX(~/.local)                  # fxsdk cmake module/toolchain
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GG="$PORT_DIR/gint-gpsp"
ROM="${1:-${ROM:-$HOME/Downloads/Metroid.gba}}"

CASIO_EMU="${CASIO_EMU:-$HOME/Dev/casio-emu}"
CALCEMU="${CALCEMU:-$CASIO_EMU/build-hle/calcemu}"
FXSDK_PREFIX="${FXSDK_PREFIX:-$HOME/.local}"

FRAMES="${FRAMES:-600}";            STATE_EVERY="${STATE_EVERY:-5}"
START_FRAME="${START_FRAME:-30}";   START_HOLD="${START_HOLD:-6}"
SHIFT_FRAME="${SHIFT_FRAME:-${A_FRAME:-200}}"
SHIFT_HOLD="${SHIFT_HOLD:-${A_HOLD:-$FRAMES}}"
SHIFT_PERIOD="${SHIFT_PERIOD:-${A_PERIOD:-200}}"
SHIFT_PRESS="${SHIFT_PRESS:-${A_PRESS:-2}}"
DIFF_FRAME="${DIFF_FRAME:-120}";    DIFF_BLOCKS="${DIFF_BLOCKS:-256}"
DIFF_DUMP_OPS="${DIFF_DUMP_OPS:-OFF}"
WINDOW_DIFF_FRAME="${WINDOW_DIFF_FRAME:--1}"
DUMP_EVERY="${DUMP_EVERY:-0}"
THUMB_LDST_NATIVE="${THUMB_LDST_NATIVE:-ON}"
EXACT_CYCLES="${EXACT_CYCLES:-ON}"
TRACE_PC="${TRACE_PC:-0}"
TRACE_MASK="${TRACE_MASK:-4095}"
TRACE_JIT="${TRACE_JIT:-0}"
TRACE_TIMER_IO="${TRACE_TIMER_IO:-0}"
SCREEN_ONLY="${SCREEN_ONLY:-OFF}"
SECS_INTERP="${SECS_INTERP:-240}";  SECS_JIT="${SECS_JIT:-240}"
SECS_DIFF="${SECS_DIFF:-$SECS_INTERP}"

OUT="${OUT:-$(mktemp -d)}"
[[ -x "$CALCEMU" ]] || { echo "missing emulator: $CALCEMU" >&2; exit 1; }
[[ -r "$ROM" ]]     || { echo "missing ROM: $ROM" >&2; exit 1; }

# casio-emu's HLE backs \fls0\ with a host dir and find_first is stubbed, so the
# port falls back to fixed names; GAME.GBA is the first that the boot path opens.
FLS0="$OUT/fls0"; mkdir -p "$FLS0"; cp "$ROM" "$FLS0/GAME.GBA"

cfg() { # build_dir dynarec(0|1) [extra diff args...]
  cmake -S "$GG" -B "$1" \
    -DCMAKE_MODULE_PATH="$FXSDK_PREFIX/lib/cmake/fxsdk" \
    -DCMAKE_TOOLCHAIN_FILE="$FXSDK_PREFIX/lib/cmake/fxsdk/FXCG50.cmake" \
    -DFXSDK_CMAKE_MODULE_PATH="$FXSDK_PREFIX/lib/cmake/fxsdk" \
    -DCGBA_DYNAREC=ON -DCGBA_GPSP_HEADLESS_TEST=ON \
    -DCGBA_SH4_THUMB_LDST_NATIVE="$THUMB_LDST_NATIVE" \
    -DCGBA_SH4_EXACT_CYCLE_BOUNDARIES="$EXACT_CYCLES" \
    -DCGBA_SH4_DIFF_DUMP_OPS="$DIFF_DUMP_OPS" \
    -DCGBA_GPSP_HEADLESS_FRAMES="$FRAMES" \
    -DCGBA_GPSP_HEADLESS_STATE_EVERY="$STATE_EVERY" \
    -DCGBA_GPSP_HEADLESS_LOG_EVERY=0 \
    -DCGBA_GPSP_HEADLESS_DUMP_EVERY="$DUMP_EVERY" \
    -DCGBA_GPSP_HEADLESS_START_FRAME="$START_FRAME" \
    -DCGBA_GPSP_HEADLESS_START_HOLD="$START_HOLD" \
    -DCGBA_GPSP_HEADLESS_A_FRAME="$SHIFT_FRAME" \
    -DCGBA_GPSP_HEADLESS_A_HOLD="$SHIFT_HOLD" \
    -DCGBA_GPSP_HEADLESS_A_PERIOD="$SHIFT_PERIOD" \
    -DCGBA_GPSP_HEADLESS_A_PRESS="$SHIFT_PRESS" \
    -DCGBA_GPSP_HEADLESS_DYNAREC="$2" \
    -DCGBA_GPSP_HEADLESS_DIFF_FRAME=-1 \
    -DCGBA_GPSP_HEADLESS_DIFF_BLOCKS=0 \
    -DCGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME=-1 \
    -DCGBA_GPSP_HEADLESS_TRACE_PC="$TRACE_PC" \
    -DCGBA_GPSP_HEADLESS_TRACE_MASK="$TRACE_MASK" \
    -DCGBA_GPSP_HEADLESS_TRACE_JIT="$TRACE_JIT" \
    -DCGBA_GPSP_HEADLESS_TRACE_TIMER_IO="$TRACE_TIMER_IO" \
    "${@:3}" >/dev/null
}

build() { # build_dir tag
  cmake --build "$1" --target clean >/dev/null
  rm -f "$GG/CGBA-GPSP.g3a"
  cmake --build "$1" -j4 >/dev/null
  [[ -r "$GG/CGBA-GPSP.g3a" ]] || { echo "build did not produce $GG/CGBA-GPSP.g3a" >&2; exit 1; }
  cp "$GG/CGBA-GPSP.g3a" "$OUT/$2.g3a"
}

run() { # g3a logfile secs
  SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software HLE_TURBO=1 HLE_FLS0="$FLS0" \
    perl -e 'alarm shift; exec @ARGV or die $!' "$3" "$CALCEMU" "$1" > "$2" 2>&1 || true
}

post_hashes() { # logfile
  grep -oE "CGBA_HASH frame=[0-9]+ phase=post iw=[0-9A-F]+ ew=[0-9A-F]+ vr=[0-9A-F]+ pal=[0-9A-F]+ pconv=[0-9A-F]+ oam=[0-9A-F]+ io=[0-9A-F]+ fb=[0-9A-F]+" "$1" \
    | sed -E 's/ phase=post//'
}

echo "harness output dir: $OUT"
echo "ROM: $ROM   frames: $FRAMES   sample: every $STATE_EVERY"
echo "input: START frame $START_FRAME hold $START_HOLD; SHIFT frame $SHIFT_FRAME hold $SHIFT_HOLD period $SHIFT_PERIOD press $SHIFT_PRESS"
echo "jit knobs: thumb_ldst=$THUMB_LDST_NATIVE exact_cycles=$EXACT_CYCLES"
echo "trace: pc=$TRACE_PC mask=$TRACE_MASK jit=$TRACE_JIT timer_io=$TRACE_TIMER_IO"
echo "dump: every $DUMP_EVERY frame(s)"
echo "verdict: screen_only=$SCREEN_ONLY"

run_block_diff=0
if [[ "$DIFF_BLOCKS" != "0" && "$DIFF_FRAME" -ge 0 ]]; then
  run_block_diff=1
fi
run_window_diff=0
if [[ "$WINDOW_DIFF_FRAME" -ge 0 ]]; then
  run_window_diff=1
fi
run_diagnostic=0
if [[ "$run_block_diff" == "1" || "$run_window_diff" == "1" ]]; then
  run_diagnostic=1
fi

echo "[1/5] configure + build interpreter (oracle) ..."
cfg "$GG/build-cg-jitdiff-interp" 0
build "$GG/build-cg-jitdiff-interp" interp
echo "[2/5] configure + build dynarec ..."
cfg "$GG/build-cg-jitdiff-jit" 1
build "$GG/build-cg-jitdiff-jit" jit
if [[ "$run_diagnostic" == "1" ]]; then
  echo "[3/5] configure + build diagnostic pass ..."
  cfg "$GG/build-cg-jitdiff-block" 0 \
      -DCGBA_GPSP_HEADLESS_DIFF_FRAME="$DIFF_FRAME" \
      -DCGBA_GPSP_HEADLESS_DIFF_BLOCKS="$DIFF_BLOCKS" \
      -DCGBA_GPSP_HEADLESS_WINDOW_DIFF_FRAME="$WINDOW_DIFF_FRAME"
  build "$GG/build-cg-jitdiff-block" blockdiff
else
  echo "[3/5] skip diagnostic pass ..."
fi

echo "[4/5] run in casio-emu ..."
run "$OUT/interp.g3a" "$OUT/interp.log" "$SECS_INTERP"
run "$OUT/jit.g3a"    "$OUT/jit.log"    "$SECS_JIT"
if [[ "$run_diagnostic" == "1" ]]; then
  run "$OUT/blockdiff.g3a" "$OUT/blockdiff.log" "$SECS_DIFF"
fi

post_hashes "$OUT/interp.log" > "$OUT/h-interp.txt"
post_hashes "$OUT/jit.log"    > "$OUT/h-jit.txt"

echo "[5/5] verdict"
echo "--- in-emu lockstep block diff @ frame $DIFF_FRAME (separate diagnostic run) ---"
if [[ "$run_block_diff" == "1" ]]; then
  sed -n '/=== live block diff frame/,/=== live block diff done/p' "$OUT/blockdiff.log" || true
else
  echo "disabled"
fi
echo "--- preserving window diff @ frame $WINDOW_DIFF_FRAME (separate diagnostic run) ---"
if [[ "$run_window_diff" == "1" ]]; then
  sed -n '/@@CGBA_WINDOW_DIFF_BEGIN/,/@@CGBA_WINDOW_DIFF_END/p' "$OUT/blockdiff.log" || true
else
  echo "disabled"
fi
echo "--- end-to-end region-hash A/B (first divergent frame) ---"
case "$SCREEN_ONLY" in
  1|ON|on|true|TRUE|yes|YES)
    join -j1 \
      <(sed -E 's/CGBA_HASH frame=([0-9]+) (.*)/\1 \2/' "$OUT/h-interp.txt" | sort -n) \
      <(sed -E 's/CGBA_HASH frame=([0-9]+) (.*)/\1 \2/' "$OUT/h-jit.txt"    | sort -n) \
      | awk '{ half=int((NF-1)/2); i=""; j=""; ifb=""; jfb="";
               for(k=2;k<=1+half;k++){ i=i" "$k; if($k ~ /^fb=/) ifb=$k; }
               for(k=2+half;k<=NF;k++){ j=j" "$k; if($k ~ /^fb=/) jfb=$k; }
               if(ifb!=jfb){ printf "SCREEN DIVERGE at frame %s\n  interp: %s\n  jit:    %s\n  full interp:%s\n  full jit:   %s\n",$1,ifb,jfb,i,j; bad=1; exit }
             }
             END{ if(!bad) printf "SCREEN MATCH across %d common sampled frames (framebuffer only; non-screen state ignored)\n", NR }'
    ;;
  *)
    join -j1 \
      <(sed -E 's/CGBA_HASH frame=([0-9]+) (.*)/\1 \2/' "$OUT/h-interp.txt" | sort -n) \
      <(sed -E 's/CGBA_HASH frame=([0-9]+) (.*)/\1 \2/' "$OUT/h-jit.txt"    | sort -n) \
      | awk '{ half=int((NF-1)/2); i=""; j="";
               for(k=2;k<=1+half;k++) i=i" "$k; for(k=2+half;k<=NF;k++) j=j" "$k;
               if(i!=j){ printf "DIVERGE at frame %s\n  interp:%s\n  jit:   %s\n",$1,i,j; bad=1; exit }
             }
             END{ if(!bad) printf "MATCH across %d common sampled frames (no divergence)\n", NR }'
    ;;
esac
echo "logs + g3a + framebuffers under: $OUT"
