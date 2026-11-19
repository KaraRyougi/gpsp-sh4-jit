#!/usr/bin/env python3
"""Run identical headless add-ins in casio-emu and record exact modeled windows.

Apply casio_emu_window.patch to a disposable casio-emu source copy first.
Build add-ins with HEADLESS_PROFILE_STOP=ON and SH4_BLOCK_PROFILE=OFF.
The ROM and optional checkpoint are copied into a fresh per-run BFile directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--state', type=Path, help='Checkpoint for a LOAD_STATE=1 build')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, required=True)
    parser.add_argument('--timeout', type=int, default=600)
    parser.add_argument('binaries', nargs='+', help='label=/path/to/addin.g3a')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for entry in args.binaries:
        label, binary = entry.split('=', 1)
        if not re.fullmatch(r'[A-Za-z0-9_-]+', label):
            parser.error('Labels must contain only letters, numbers, underscores or hyphens')
        binary = Path(binary).resolve()
        if not binary.is_file():
            raise SystemExit(f"Missing add-in: {binary}")
        run_dir = args.output / label
        run_dir.mkdir(exist_ok=True)
        fls = run_dir / 'fls0'
        fls.mkdir(exist_ok=True)
        # Do not reuse mutated save data from an earlier run.
        for child in fls.iterdir():
            if child.is_file():
                child.unlink()
        shutil.copyfile(args.rom, fls / 'GAME.GBA')
        if args.state:
            shutil.copyfile(args.state, fls / 'CGBACHK.SAV')
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                   HLE_TURBO='1', HLE_FORCE_R61524='1', HLE_FLS0=str(fls.resolve()),
                   HLE_CACHESIM='1', HLE_CACHESIM_EVERY='0', HLE_CACHESIM_WINDOW='1')
        start = time.monotonic()
        proc = subprocess.Popen([str(args.emulator.resolve()), str(binary)],
                                cwd=run_dir, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        timeout = threading.Timer(args.timeout, proc.kill)
        timeout.start()
        done = False
        logpath = run_dir / 'run.log'
        try:
            with logpath.open('wb') as out:
                for line in proc.stdout:
                    out.write(line)
                    out.flush()
                    if b'[CACHESIM] window-end:' in line:
                        done = True
                        proc.terminate()
                        break
            proc.wait(timeout=5)
        finally:
            timeout.cancel()
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        log = logpath.read_text(errors='replace')
        markers = re.findall(r'\[CACHESIM\] window-(start|end):[^\n]* cycles=(\d+)', log)
        if not done or [m[0] for m in markers] != ['start', 'end']:
            raise SystemExit(f'{label}: missing complete measurement window; inspect {logpath}')
        announced = re.search(r'loaded OK; running (\d+) frames', log)
        if not announced or int(announced[1]) != args.frames:
            raise SystemExit(f'{label}: binary frame count does not match --frames')
        if args.state and '@@CGBA_CHECKPOINT load ok=1 read=1' not in log:
            raise SystemExit(f'{label}: checkpoint was not loaded successfully')
        cycles = int(markers[1][1]) - int(markers[0][1])
        # Pre-frame framebuffer hashes include the host timing overlay. Compare
        # only freshly rendered output and the matching post-frame memory state.
        state_lines = re.findall(r'@@CGBA_(?:HASH frame=\d+ phase=post|FBSTAT)[^\n]*', log)
        row = dict(label=label, frames=args.frames, cycles=cycles,
                   cycles_per_frame=cycles / args.frames,
                   modeled_fps_118mhz=118e6 * args.frames / cycles,
                   wall_seconds=time.monotonic() - start,
                   binary_sha256=digest(binary), rom_sha256=digest(args.rom),
                   state_sha256=digest(args.state) if args.state else None,
                   observable_records=len(state_lines),
                   observable_sha256=(hashlib.sha256('\n'.join(state_lines).encode()).hexdigest()
                                      if state_lines else None),
                   log=str(logpath.resolve()))
        results.append(row)
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(f"{label}: {row['cycles_per_frame']:.1f} cycles/frame, "
              f"{row['modeled_fps_118mhz']:.3f} modeled fps", flush=True)


if __name__ == '__main__':
    main()
