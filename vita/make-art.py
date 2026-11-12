#!/usr/bin/env python3
"""Generate LiveArea art from the game's own images (needs Pillow and pngquant):
  pic0.png     - the main menu flag, shown while the game starts
  startup.png  - the JA2 logo on a transparent gate (start button)
Usage: vita/make-art.py [JA2 Data dir]   (default: vita-data/ja2-stracciatella/data/Data)
icon0.png and bg.png are kept as they are (icon from the GOG installer, bg = darkened flag)."""
import os, subprocess, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'vita/tools'))
from sti import slf_get, sti_to_image

DATA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'vita-data/ja2-stracciatella/data/Data')
SCE = os.path.join(ROOT, 'src/vita/sce_sys')
SLF = os.path.join(DATA, 'Loadscreens.slf')


def cover(img, w, h):
    """Scale to fill w x h and crop the overflow evenly."""
    s = max(w / img.width, h / img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.Resampling.LANCZOS)
    x, y = (img.width - w) // 2, (img.height - h) // 2
    return img.crop((x, y, x + w, y + h))


def save8(img, path):
    """LiveArea wants 8-bit indexed PNGs (pngquant keeps the alpha channel)."""
    img.save(path)
    subprocess.run(['pngquant', '--force', '--ext', '.png', '256', path], check=True)


flag = sti_to_image(slf_get(SLF, 'mainmenubackground.sti'))
save8(cover(flag, 960, 544), os.path.join(SCE, 'pic0.png'))

logo = sti_to_image(slf_get(SLF, 'ja2logo.sti'), alpha=True)
logo = logo.crop(logo.getbbox())
logo.thumbnail((270, 150), Image.Resampling.LANCZOS)
gate = Image.new('RGBA', (280, 158), (0, 0, 0, 0))
gate.alpha_composite(logo, ((280 - logo.width) // 2, (158 - logo.height) // 2))
save8(gate, os.path.join(SCE, 'livearea/contents/startup.png'))
