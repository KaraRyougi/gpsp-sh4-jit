# VSync on the fx-CG50 / fx-CG100

The fx-CP400 port's refresh sync ("VSync", [classpad-port.md](classpad-port.md)
→ Tearing) on the two fx-CG models. The arithmetic is shared
(`classpad_sync.h`); the panel access and the presenter are the fx-CG's own
(`gint-gpsp/src/gint_platform.c`), and the settings and calibration page are
in the keypad interface (`cg_ui.c`, [fxcg-ui.md](fxcg-ui.md)). The old list
menu (`fxcg100_menu.c`) also has them in gint builds (`CGBA_GINT_PLATFORM`),
but those builds now use `cg_ui.c`; the bare-metal build has neither.

Nothing here has been run on an fx-CG yet. In casio-emu the pages work and
the panel has no row counter, so VSync reports that and does nothing.

## VSync

**Settings → Display → VSync** (off by default) and, while it is on,
**Refresh position** (0..15, "flipped" when the direction is flipped; the
confirm key opens the page that sets it, after a photosensitive-epilepsy
warning whose default is Cancel). Saved with the other settings on leaving
Settings, in `CGBA.CFG` words the file already had (`lcd_sync`, and bits 0–4
of `lcd_extra`).

As on the fx-CP400, the controller is asked which row it is showing (DCS
get_scanline, 0x45), and each frame is started at a moment from which the
refresh and the strips being written do not meet (`cp_sync_window_part`, a
sixth of a refresh clear for the game). What the fx-CG adds:

- **Whether there is a counter at all.** gint drives the 2025–2026 fx-CG
  panels (ID 50 and 82, 0x32/0x52) with the same DCS commands as the
  fx-CP400's later panels (0x16/0x52), whose 0x45 is a row counter. On the
  R61524 those numbers are registers. Nothing is assumed: the first time a
  frame is to wait (and whenever the calibration page opens) a quarter of a
  second of reads decides, with the fx-CP400's probe and a lower floor for
  the highest row (`cp_scan_probe_result_min`, 200 instead of 320: the fx-CG
  shows 216 rows). Without VSync nothing is read; without a counter nothing
  waits.
- **Which way the refresh runs.** Positions are counted in refresh order.
  On a DCS panel the address mode (0x0b) is read; with MY set the OS has the
  rows the other way round, which on the fx-CP400 means the refresh starts at
  the last row. The calibration page can flip that (UP/DOWN), in case that is
  not the whole story; the flip is saved with the position. A frame that
  waits sends its strips the way the refresh goes, in all three scalings
  (each strip still filled from its top row; going up, the window keeps a
  strip clear for that, as on the fx-CP400). Without VSync the strips go down
  as before.
- **The calibration page's test points.** The fx-CP400 page starts frames at
  the two ends of the window in turn. Its picture covers 528 of 548 refresh
  rows; the fx-CG's covers 160 to 216 of about 250, so most of the refresh is
  safe to start from, and an offset far out could still put both ends (or
  any three points) somewhere safe: in a simulation the 1:1 page looked clean
  at the right position and at one half a refresh away, which tore in game.
  The fx-CG page therefore starts frames at points a step apart across the
  whole window (`cp_sync_window_point`): any position that is out puts one of
  them where it tears.

The calibration page shows the game picture in the scaling chosen, through
the real presenter, changing colour every frame, with a three-line readout at
the top left of the picture (11 px keycap font, so it covers little): position
and direction, the rows and rate counted (or "No row counter: VSync cannot
work"), the panel ID and address mode, and the keys (Left / Right position,
Up / Down flip, confirm, back, the menu key or `MENU`/`HOME` to finish). The page needs a GBA frame buffer
to paint (`fxcg100_menu_set_frame`, the game's own; the game draws over it
again).

`tests/fxcg_present_test.c` runs `gint_platform.c` against a simulated
panel with a refresh of its own: every scaling and filter paints the same
picture whichever way its strips go; at the right position no frame tears
with VSync, either way round, MY set or flipped by hand; without VSync, or
taken the wrong way round, frames tear; every position that is clean on the
calibration page is clean in game (1 or 2 adjacent positions are, for
pictures on and between steps); and a panel without a counter is left alone.

### What to report from hardware

The calibration page's readout. If it says "NO ROW COUNTER", the panel
(with that ID) has no usable 0x45 and VSync cannot work on it as built. If it
shows rows and a rate, which positions (and direction) give a clean page, and
whether the game then still tears. A count of 396 or more would mean the
refresh runs along the columns, across the strips; whole-row strips cannot
avoid that.

## No built-in overclock

The fx-CP400's clock page was not brought over, and the fx-CG no longer
applies the Ptune 4 F5 profile at start-up: gpSP runs on the clock it was
launched with (stock, or whatever Ptune set before). `overclock.c` and the
`CGBA_DEFAULT_OVERCLOCK` option are gone. The fx-CP400 keeps its clock page
and its F5 default.
