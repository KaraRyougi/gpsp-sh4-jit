#!/usr/bin/env bash
# Playability harness for the fx-CG100 SH4 dynarec.
#
# Unlike run-jit-diff.sh (which chases cycle-for-cycle equivalence with the
# interpreter -- a goal no emulator pursues, see the dynarec-cycle-accuracy notes),
# this asks the question that actually matters: DOES THE GAME RUN? It drives the
# ROM with scripted input, dumps the live GBA framebuffer at intervals from BOTH
# the interpreter (the rendering reference) and the SH4 dynarec, converts them to
# PNGs, and assembles an interp-vs-JIT contact sheet so the JIT's output can be
# eyeballed and the first crash/divergence located.
#
# The bar is "renders the same screens and reaches gameplay without crashing,"
# NOT "zero cycle drift."
#
#   Usage:  ports/fxcg100/run-playtest.sh [/path/to/rom.gba]
#
#   Env knobs (defaults in parens):
#     FRAMES(540) DUMP_EVERY(60) STAT_EVERY(20)   # how long / how often to snapshot
#     START_FRAME(30) START_HOLD(8)               # press START
#     A_FRAME(120) A_HOLD(420) A_PERIOD(120) A_PRESS(6)   # tap A to navigate menus
#     ROM_CACHE(524288) RAM_CACHE(262144)         # match the build default; a small cache thrashes
#                                                 # (flush+re-translate) once blocks grow (native LDST)
#     SECS(540)                                   # wall-clock cap per emulator run
#     CASIO_EMU(~/Dev/casio-emu)  FXSDK_PREFIX(~/.local)  OUT(/tmp/cgba-playtest)
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GG="$PORT_DIR/gint-gpsp"
ROM="${1:-${ROM:-$HOME/Downloads/Metroid.gba}}"
CASIO_EMU="${CASIO_EMU:-$HOME/Dev/casio-emu}"
CALCEMU="$CASIO_EMU/build-hle/calcemu"
FXSDK_PREFIX="${FXSDK_PREFIX:-$HOME/.local}"
OUT="${OUT:-/tmp/cgba-playtest}"

FRAMES="${FRAMES:-540}"; DUMP_EVERY="${DUMP_EVERY:-60}"; STAT_EVERY="${STAT_EVERY:-20}"
START_FRAME="${START_FRAME:-30}"; START_HOLD="${START_HOLD:-8}"
A_FRAME="${A_FRAME:-120}"; A_HOLD="${A_HOLD:-420}"; A_PERIOD="${A_PERIOD:-120}"; A_PRESS="${A_PRESS:-6}"
ROM_CACHE="${ROM_CACHE:-524288}"; RAM_CACHE="${RAM_CACHE:-262144}"; SECS="${SECS:-540}"

[ -x "$CALCEMU" ] || { echo "casio-emu not built at $CALCEMU"; exit 1; }
[ -f "$ROM" ]     || { echo "ROM not found: $ROM"; exit 1; }
mkdir -p "$OUT/png"; FLS0="$OUT/fls0"; mkdir -p "$FLS0"; cp "$ROM" "$FLS0/GAME.GBA"

build() { # dir dynarec out
  rm -rf "$1"
  cmake -S "$GG" -B "$1" \
    -DCMAKE_MODULE_PATH="$FXSDK_PREFIX/lib/cmake/fxsdk" \
    -DCMAKE_TOOLCHAIN_FILE="$FXSDK_PREFIX/lib/cmake/fxsdk/FXCG50.cmake" \
    -DFXSDK_CMAKE_MODULE_PATH="$FXSDK_PREFIX/lib/cmake/fxsdk" \
    -DCGBA_DYNAREC=ON -DCGBA_GPSP_HEADLESS_TEST=ON -DCGBA_GPSP_HEADLESS_DYNAREC="$2" \
    -DCGBA_GPSP_ROM_TRANSLATION_CACHE_SIZE="$ROM_CACHE" -DCGBA_GPSP_RAM_TRANSLATION_CACHE_SIZE="$RAM_CACHE" \
    -DCGBA_GPSP_HEADLESS_FRAMES="$FRAMES" -DCGBA_GPSP_HEADLESS_DUMP_EVERY="$DUMP_EVERY" \
    -DCGBA_GPSP_HEADLESS_STAT_EVERY="$STAT_EVERY" -DCGBA_GPSP_HEADLESS_LOG_EVERY=0 \
    -DCGBA_GPSP_HEADLESS_STATE_EVERY=0 \
    -DCGBA_GPSP_HEADLESS_START_FRAME="$START_FRAME" -DCGBA_GPSP_HEADLESS_START_HOLD="$START_HOLD" \
    -DCGBA_GPSP_HEADLESS_A_FRAME="$A_FRAME" -DCGBA_GPSP_HEADLESS_A_HOLD="$A_HOLD" \
    -DCGBA_GPSP_HEADLESS_A_PERIOD="$A_PERIOD" -DCGBA_GPSP_HEADLESS_A_PRESS="$A_PRESS" >/dev/null 2>&1
  cmake --build "$1" -j4 >/dev/null 2>&1
  cp "$GG/CGBA-GPSP.g3a" "$3"
}

run() { # g3a log
  SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software HLE_TURBO=1 HLE_FLS0="$FLS0" \
    perl -e 'alarm shift; exec @ARGV or die $!' "$SECS" "$CALCEMU" "$1" >"$2" 2>&1 || true
}

report() { # tag log
  local last; last=$(grep -oE '@@CGBA_FBSTAT frame=[0-9]+' "$2" | tail -1 | grep -oE '[0-9]+')
  printf "  %-6s reached frame %-4s  done=%s  crash=%s\n" "$1" "${last:-?}" \
    "$(grep -c '=== done ===' "$2")" "$(grep -cE 'Unaligned|add-in returned' "$2")"
}

echo "ROM=$ROM  frames=$FRAMES  out=$OUT"
echo "[1/4] build interpreter (reference)"; build "$GG/build-pt0" 0 "$OUT/interp.g3a"
echo "[2/4] build JIT";                     build "$GG/build-pt1" 1 "$OUT/jit.g3a"
echo "[3/4] run both in casio-emu"
run "$OUT/interp.g3a" "$OUT/interp.log"; report interp "$OUT/interp.log"
run "$OUT/jit.g3a"    "$OUT/jit.log";    report jit    "$OUT/jit.log"
echo "[4/4] framebuffers -> PNG + contact sheet"
python3 "$PORT_DIR/fb2png.py" "$OUT/interp.log" "$OUT/png" interp
python3 "$PORT_DIR/fb2png.py" "$OUT/jit.log"    "$OUT/png" jit
python3 - "$OUT/png" <<'PY'
import sys, glob, re
from PIL import Image, ImageDraw
d = sys.argv[1]
def load(t):
    return {int(re.search(r'_f(\d+)', p).group(1)): Image.open(p) for p in glob.glob(f"{d}/{t}_f*.png")}
ip, jt = load('interp'), load('jit')
if not ip and not jt: sys.exit("no framebuffers dumped")
frames = sorted(set(ip) | set(jt)); W, H = 480, 320
sheet = Image.new('RGB', (W*len(frames), (H+24)*2+24), (20,20,20)); dr = ImageDraw.Draw(sheet)
dr.text((4,4), "row0 = INTERP (reference)   row1 = JIT", fill=(255,220,120))
for c, f in enumerate(frames):
    for row, src in ((0, ip), (1, jt)):
        x, y = c*W, row*(H+24)+24
        if f in src: sheet.paste(src[f], (x, y))
        dr.text((x+4, y-16), f"{'interp' if row==0 else 'jit'} f{f}" + ("" if f in src else " (none)"), fill=(230,230,230))
sheet.save(f"{d}/contact.png"); print(f"  contact sheet -> {d}/contact.png  (interp {sorted(ip)} | jit {sorted(jt)})")
PY
echo "done. open $OUT/png/contact.png"
