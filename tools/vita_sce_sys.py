#!/usr/bin/env python3
"""Builds the Vita package's LiveArea art from platforms/vita/art.

LiveArea wants 8-bit palettised PNGs: icon0 128x128, bg 840x500, startup
280x158. Needs Pillow (a build-time tool only).

    tools/vita_sce_sys.py <art dir> <out dir>
"""
import os
import sys

from PIL import Image


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


def background(heroes, logo):
    """The banner across the top, fading to black, the logo in the dark below."""
    w, h = 840, 500
    top = heroes.resize((w, round(heroes.height * w / heroes.width)), Image.LANCZOS)
    out = Image.new('RGB', (w, h), (0, 0, 0))
    out.paste(top, (0, 0))
    fade = 90
    mask = Image.new('L', top.size, 255)
    for y in range(top.height - fade, top.height):
        mask.paste(round(255 * (top.height - y) / fade), (0, y, w, y + 1))
    black = Image.new('RGB', top.size, (0, 0, 0))
    out.paste(Image.composite(top, black, mask), (0, 0))
    lh = 215
    lg = logo.resize((round(logo.width * lh / logo.height), lh), Image.LANCZOS)
    out.paste(lg, ((w - lg.width) // 2, top.height + (h - top.height - lh) // 2), lg)
    return out


def startup(grid):
    """Logo and Prince on black, letterboxed, never cropped."""
    w, h = 280, 158
    r = grid.resize((w, round(grid.height * w / grid.width)), Image.LANCZOS)
    out = Image.new('RGB', (w, h), (0, 0, 0))
    out.paste(r, (0, (h - r.height) // 2))
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    art, out = sys.argv[1], sys.argv[2]
    load = lambda n, m='RGB': Image.open(os.path.join(art, n)).convert(m)
    save(background(load('pop3d-heroes.jpg'), load('pop3d-logo.png', 'RGBA')), os.path.join(out, 'livearea/contents/bg.png'))
    save(startup(load('pop3d-grid.jpg')), os.path.join(out, 'livearea/contents/startup.png'))
    save(load('pop3d-icon.png').resize((128, 128), Image.LANCZOS), os.path.join(out, 'icon0.png'))


if __name__ == '__main__':
    main()
