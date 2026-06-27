# fx-CG100 gpSP Port

This directory contains the freestanding fx-CG100 frontend for the patched
`vendor/gpsp` core.

## Build

```sh
make -C ports/fxcg100
```

Outputs:

- `build/cgba.elf`
- `build/cgba.bin`
- `build/cgba.g3a` - installable Casio add-in packaged with `mkg3a`
- `build/cgba.raw.g3a` - zero-header direct-loader image for low-level probes

The build uses `sh-elf-gcc/g++ -mb -m4a-nofpu` and links the add-in body at
`0x00300000`. The default linker uses the standard fx-CG add-in RAM window
starting at `0x08101400` instead of the emulator-only high physical RAM window.
Override `MKG3A=/path/to/mkg3a` if the packer is not on `PATH`.

Hardware-safe defaults:

- `CGBA_FULL_GPSP=0`: build only the physical boot/menu smoke app
- `CGBA_DEBUG_PORT=0`: do not write the emulator-only `0xfffff000` print register
- `CGBA_RUN_JIT_PROBE=0`: do not execute generated RAM code at startup
- `CGBA_USE_LOADER_STACK=1`: keep the launcher-provided stack
- `CGBA_RETURN_TO_LOADER=1`: allow the smoke app to return through the loader

For the full gpSP interpreter bring-up build:

```sh
make -C ports/fxcg100 BUILD=build-full CGBA_FULL_GPSP=1
```

## gint Display Isolate

The physical calculator showed that the freestanding direct-R61524 smoke build
can touch the LCD but only draws a single line. The next display candidate is a
small fxSDK/gint add-in under `gint-smoke`, matching the initialization style
used by the working CGBC port.

```sh
cd ports/fxcg100/gint-smoke
fxsdk build-cg -c
fxsdk build-cg
```

Output:

- `ports/fxcg100/gint-smoke/CGBA-GINT.g3a`

Runtime shape:

- Draws `CGBA GINT SMOKE` with full-screen color bands using
  `dclear()`/`dtext()`/`dupdate()`
- Holds briefly, then returns through the loader
- Does not use raw LCD init, raw KEYSC, or the emulator-only debug port
- Does not initialize gint keyboard in this isolate; direct HLE rejects the
  keyscan timer register write at `0xa54cffd4`

## gint gpSP Interpreter Isolate

The current correctness target is `gint-gpsp`, a gint/fxSDK add-in that links
the patched gpSP interpreter without libretro callbacks.

```sh
cd ports/fxcg100/gint-gpsp
fxsdk build-cg
```

Output:

- `ports/fxcg100/gint-gpsp/CGBA-GPSP.g3a`

Runtime shape:

- Uses stock gint startup and hardware detection, matching the physical-safe
  `gint-smoke` path
- Uses gint display calls for all calculator LCD output
- Opens the settings menu as the first screen; physical `ON` reopens it after
  returning to the game
- Builds with direct storage access disabled by default. `CGBA_FXCG100_STORAGE`
  is currently an emulator/harness opt-in because the direct BFile entry points
  are not safe to call from inside gint on hardware.
- Reserves only `ON` during gameplay; all other fx-CG100 keys are bindable as
  GBA inputs or gpSP hotkeys through the settings menu
- Keeps gpSP's large GBA memories, framebuffer, and embedded smoke ROM buffer in
  `.cgba.highbss` at `0x8c200000`
- Refuses to clear/run the gpSP high arena if the linked range falls outside
  `0x8c200000..0x8c780000`
- Disables gint's fallback OS heap arena so allocation exhaustion returns NULL
  instead of using the unsupported `0x80020070` syscall gate
- Skips gpSP's 1 MiB streaming ROM cache for the embedded-ROM path; larger
  cartridge support should use the later NOR/direct mapping path
- Boots the embedded Mode 3 smoke ROM through the interpreter and blits the
  resulting 240x160 RGB565 frame centered on the 384x216 LCD

## Current Runtime Shape

- Default app: physical smoke screen with `CGBA SAFE BOOT`, stack/key
  diagnostics, and bottom color bars
- Video: direct R61524 GRAM writes
- Input: raw fx-CG100 KEYSC reads are used only for the smoke UI; implausible
  all-set scans are ignored
- App shell: `ON` opens the menu after the startup grace period; `HOME` returns
  through the loader on the safe-boot screen
- Menu: settings for graphics/frameskip/FPS display, load/save state slot,
  gpSP-style per-action key mapping, config save/load, cheats/misc,
  load/restart/return/quit. Defaults are `A=SHIFT`, `B=ALPHA`, `SELECT=VAR`,
  `START=EXE`, D-pad arrows, `L=BEGIN`, and `R=END`; gpSP hotkeys default to
  `NONE`; saved settings live at `\\fls0\\CGBA.CFG`. Manual frameskip is wired
  to skip LCD blits; game save/state storage-backed items currently show TODO
  markers.
- Full gpSP path: embedded `test_rom/mode3_smoke.h` and
  `test_rom/input_probe.h`, plus a NOR root-file source for
  `test_rom/CGBAINP.GBA`; interpreter only, audio disabled,
  serial/netplay/rumble inert, JIT probe opt-in only
- gpSP gameplay video uses a CGBC-style direct R61524 frame window instead of
  full-screen `dupdate()`; menus and status screens still use gint VRAM.

## Smoke Tests

Host emitter bytes:

```sh
cc -std=c11 -Wall -Wextra -I. tests/sh4_codegen_smoke.c -o /tmp/cgba-sh4-smoke
/tmp/cgba-sh4-smoke
```

fx-CG100 HLE homebrew launch:

```sh
make -C ports/fxcg100
env HLE_EXIT_AT=20000 HLE_FORCE_R61524=1 HLE_GRAMLOG=1 \
  HLE_FBDUMP=/tmp/cgba-safe.ppm \
  ~/Dev/casio-emu/build-hle/calcemu ports/fxcg100/build/cgba.g3a
```

Expected log shape:

```text
[disp] f=1 driven=1 hot=1 use=1 h[0..395] (... writes)
[EXIT] HLE_EXIT_AT=20000 ticks reached
```

CGBC-style direct flash-copy launch:

```sh
make -C ports/fxcg100
ports/fxcg100/run-zelda-flash.sh ports/fxcg100/build/cgba.g3a
```

This copies `/tmp/fls0_zelda.bin` to a temporary flash image, sets
`HLE_FLASHFILE` to that copy, and passes only the `.g3a` to
`~/Dev/casio-emu/build-hle/calcemu`. It avoids the slower USB install path.
Run with `HLE_FORCE_R61524=1 CGBA_FBDUMP=/tmp/cgba-zelda-safe.ppm` to capture
the smoke screen.

gint display isolate through the same flash-copy launch:

```sh
cd ports/fxcg100/gint-smoke
fxsdk build-cg
cd ../../..
env HLE_EXIT_AT=120000 HLE_GRAMLOG=1 \
  CGBA_FBDUMP=/tmp/cgba-gint-zelda-final.ppm \
  CGBA_LOG=/tmp/cgba-gint-zelda-final.log \
  ports/fxcg100/run-zelda-flash.sh ports/fxcg100/gint-smoke/CGBA-GINT.g3a
```

Expected log shape:

```text
[FLASHLOAD] loaded NOR filesystem (...)
[disp] f=1 driven=1 hot=1 use=0 h[0..395] (88704 writes)
[EXIT] HLE_EXIT_AT=120000 ticks reached
```

JIT-vs-interpreter Metroid accuracy harness:

```sh
ports/fxcg100/run-jit-diff.sh ~/Downloads/Metroid.gba
```

This builds two headless `gint-gpsp` add-ins that differ only by
`dynarec_enable`, runs both under `~/Dev/casio-emu/build-hle/calcemu`, and
diffs sampled post-frame memory/framebuffer hashes. It can also run separate
diagnostic passes for live block lockstep (`DIFF_FRAME`/`DIFF_BLOCKS`) and a
state-preserving one-frame diff (`WINDOW_DIFF_FRAME`), so diagnostic stepping
does not contaminate the clean interpreter/JIT A/B run. The default input stream
presses calculator `SHIFT`'s default GBA binding (`A`) for two frames every
200 GBA frames, after a short `START` press to leave the title flow. Tune with
`SHIFT_FRAME`, `SHIFT_HOLD`, `SHIFT_PERIOD`, and `SHIFT_PRESS`; the older
`A_*` environment variable names still work. `THUMB_LDST_NATIVE=OFF` disables
the native Thumb byte-load fast path for isolation runs. Harness `.g3a` files
under the output directory are headless diagnostics for casio-emu; do not install
them on hardware. The script preserves/restores the source-tree
`gint-gpsp/CGBA-GPSP.g3a` so it remains the calculator-launch build.

The full MPM flow is scaffolded in `run-mpm-smoke.sh`. It provisions a temporary
flash image under `/tmp`, installs `MPM.BIN`, attempts to install `CGBA.G3A`,
then boots the patched OS image and launches through TOOLS/EXE.

gint gpSP interpreter isolate through the flash-copy launch:

```sh
cd ports/fxcg100/gint-gpsp
fxsdk build-cg
cd ../../..
env HLE_HB=1 HLE_TLBLOG=1 HLE_EXIT_AT=3000000 HLE_GRAMLOG=1 \
  CGBA_FBDUMP=/tmp/cgba-gpsp-mode3-fixed.ppm \
  CGBA_LOG=/tmp/cgba-gpsp-mode3-fixed.log \
  ports/fxcg100/run-zelda-flash.sh ports/fxcg100/gint-gpsp/CGBA-GPSP.g3a
```

Expected result:

- Log shows `[HLE] add-in returned`
- Framebuffer dump contains a centered 240x160 GBA frame. The default
  `INPUT PROBE` ROM draws green while no GBA button is pressed.
- Pixel count sanity check on the dump should find exactly `38400` GBA-frame
  pixels in the centered direct-LCD window.

Experimental NOR input-probe ROM:

```text
ports/fxcg100/test_rom/CGBAINP.GBA
```

The direct NOR loader is disabled in the default calculator build because the
underlying BFile entry points can crash when called from inside gint. Emulator
diagnostic builds can opt in with `-DCGBA_FXCG100_STORAGE=ON`; in that mode,
copy `CGBAINP.GBA` to the storage root, choose `ROM SOURCE: NOR CGBAINP.GBA`,
then `LOAD NEW GAME`. The loader opens `\\fls0\CGBAINP.GBA`, resolves its
fx-CG100 NOR blocks through the direct OS block-address function, and maps
gpSP's 32 KiB GBA ROM pages directly to cached NOR aliases when each page is
physically contiguous. The bundled test ROM is padded to exactly one 32 KiB GBA
page.

Safety note: the previous gpSP isolate placed `.cgba.highbss` at `0x8c800000`
and installed HLE-only UTLB mappings before entering gint. On the real fx-CG100
that address is past the conservative 8 MiB RAM window, and touching it can cold
boot the calculator. The default `CGBA-GPSP.g3a` no longer contains that
trampoline or fake hardware detector.

Current MPM status: the harness reaches `[MPM] reached mpmMain`, but the add-in
USB install phase does not yet complete reliably. The validated emulator route
for the gint isolates is the CGBC-style `run-zelda-flash.sh` launch, which
passes only the `.g3a` to `calcemu`.
