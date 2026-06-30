#!/usr/bin/env python3
"""Convert the headless test's framebuffer dumps into PNGs.

The headless build (CGBA_GPSP_HEADLESS_DUMP_EVERY > 0) streams the live GBA
framebuffer to the casio-emu debug port between @@CGBA_FRAME_BEGIN / _END
markers as RGB565 hex (one 4-hex-digit pixel each, W pixels per row, H rows).
This parses a captured run log and writes one PNG per dumped frame so the JIT's
actual output can be eyeballed (and diffed against the interpreter oracle).

Usage: fb2png.py <run.log> <out_dir> [tag]   ->  <out_dir>/<tag>_fNNNN.png
"""
import sys, re, os
from PIL import Image

_BEGIN = re.compile(r'@@CGBA_FRAME_BEGIN frame=(\d+) w=(\d+) h=(\d+) pitch=(\d+)')
_HEX = set('0123456789ABCDEFabcdef')


def parse(logpath):
    frames, cur, w, h, px = {}, None, 0, 0, []
    for line in open(logpath, errors='replace'):
        m = _BEGIN.search(line)
        if m:
            cur, w, h, px = int(m.group(1)), int(m.group(2)), int(m.group(3)), []
            continue
        if cur is None:
            continue
        if '@@CGBA_FRAME_END' in line:
            frames[cur] = (w, h, px)
            cur = None
            continue
        s = line.strip()
        if s and all(c in _HEX for c in s):
            for i in range(0, len(s) - 3, 4):
                px.append(int(s[i:i + 4], 16))
    return frames


def to_img(w, h, px, scale=2):
    img = Image.new('RGB', (w, h))
    out = img.load()
    for idx in range(min(len(px), w * h)):
        v = px[idx]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out[idx % w, idx // w] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    return img.resize((w * scale, h * scale), Image.NEAREST)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    log, outdir = sys.argv[1], sys.argv[2]
    tag = sys.argv[3] if len(sys.argv) > 3 else 'fb'
    os.makedirs(outdir, exist_ok=True)
    fr = parse(log)
    ok = 0
    for f in sorted(fr):
        w, h, px = fr[f]
        if len(px) < w * h:
            print(f'  {tag} f{f}: SHORT ({len(px)}/{w*h} px) -- skipped')
            continue
        to_img(w, h, px).save(f'{outdir}/{tag}_f{f:04d}.png')
        ok += 1
    print(f'  {tag}: {ok}/{len(fr)} frames -> {outdir}')


if __name__ == '__main__':
    main()
