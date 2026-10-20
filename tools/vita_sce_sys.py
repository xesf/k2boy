#!/usr/bin/env python3
"""Builds the Vita package's LiveArea art from the retail install.

The art is the game's own (title screen, a movie thumbnail), so it is made
from the user's data at build time rather than kept in the repository.
LiveArea wants 8-bit palettised PNGs: icon0 128x128, bg 840x500, startup
280x158. Needs Pillow (a build-time tool only).

    tools/vita_sce_sys.py <data dir> <out dir>
"""
import os
import sys

from PIL import Image


def fit(img, w, h):
    sw, sh = img.size
    s = max(w / sw, h / sh)
    r = img.resize((round(sw * s), round(sh * s)), Image.LANCZOS)
    x, y = (r.width - w) // 2, (r.height - h) // 2
    return r.crop((x, y, x + w, y + h))


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)


def find(root, rel):
    """Case-insensitive lookup, as the install mixes cases."""
    cur = root
    for part in rel.split('/'):
        names = {n.lower(): n for n in os.listdir(cur)}
        if part.lower() not in names:
            sys.exit('vita_sce_sys: no %s under %s' % (rel, root))
        cur = os.path.join(cur, names[part.lower()])
    return cur


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data, out = sys.argv[1], sys.argv[2]
    title = Image.open(find(data, '2D/menus/Main Menu.bmp')).convert('RGB')
    save(fit(title, 840, 500), os.path.join(out, 'livearea/contents/bg.png'))
    save(fit(title, 280, 158), os.path.join(out, 'livearea/contents/startup.png'))
    # The first cutscene's thumbnail, a square from between its letterbox bars.
    thumb = Image.open(find(data, '2D/menus/Movie Thumbnails/Thumb #1.bmp')).convert('RGB')
    w = thumb.width
    icon = thumb.crop((w // 2 - 62, 17, w // 2 + 62, 141)).resize((128, 128), Image.LANCZOS)
    save(icon, os.path.join(out, 'icon0.png'))


if __name__ == '__main__':
    main()
