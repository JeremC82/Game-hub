"""Logo GAMEHUB du demarrage (meme dessin que bootLogo du firmware).

  python docs/logo_boot.py   -> docs/logo-boot.png (ecran 128x128)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5)

src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)
GLCD = ns['GLCD_BYTES']
RAINBOW = [(255, 40, 40), (255, 150, 0), (255, 230, 0), (40, 230, 60), (0, 210, 255), (60, 110, 255), (220, 60, 255)]
W = H = 128


def word(im, txt, size, y, col0, shine=-99):
    px = im.load()

    def P(x, yy, c):
        if 0 <= x < W and 0 <= yy < H:
            px[x, yy] = c
    tw, th = len(txt) * 6 * size - size, 7 * size
    bits = set()
    for i, ch in enumerate(txt):
        for cx in range(5):
            line = GLCD[ord(ch) * 5 + cx]
            for cy in range(8):
                if line & (1 << cy):
                    for a in range(size):
                        for b in range(size):
                            bits.add((1 + i * 6 * size + cx * size + a, 1 + cy * size + b))
    x0 = (W - tw) // 2 - 1
    for (xx, yy) in bits:
        for a in range(2):
            for b in range(2):
                P(x0 + xx + 1 + a, y + yy + 1 + b, (60, 0, 150))
        P(x0 + xx - 1, y + yy, (0, 0, 150))
        P(x0 + xx, y + yy - 1, (0, 0, 150))
    for (xx, yy) in bits:
        band = max(0, min(2, (yy - 1) * 3 // th))
        k = RAINBOW[(col0 + max(0, min(6, (xx - 1) // (6 * size)))) % 7]
        c = tuple(min(255, v + 90) for v in k) if band == 0 else k if band == 1 else tuple(v * 7 // 10 for v in k)
        d = shine - (xx + yy)
        if 0 <= d < 3:
            c = (255, 255, 255)
        elif 3 <= d < 6:
            c = tuple(min(255, v + 120) for v in k)
        if yy <= size and (xx, yy - 1) not in bits:
            c = (255, 255, 255)
        P(x0 + xx, y + yy, c)


def pad(d, cx, y):
    body, dark = (190, 195, 210), (110, 110, 130)
    d.rounded_rectangle((cx - 16, y, cx + 15, y + 11), 5, fill=body)
    d.rounded_rectangle((cx - 18, y + 4, cx - 9, y + 14), 4, fill=body)
    d.rounded_rectangle((cx + 8, y + 4, cx + 17, y + 14), 4, fill=body)
    d.line((cx - 13, y, cx + 12, y), fill=(255, 255, 255))
    d.rectangle((cx - 14, y + 5, cx - 8, y + 6), fill=dark)
    d.rectangle((cx - 12, y + 3, cx - 11, y + 8), fill=dark)
    for (dx, dy, c) in ((11, 3, (0, 220, 120)), (9, 5, (255, 60, 200)), (13, 5, (255, 50, 50)), (11, 7, (60, 140, 255))):
        d.point((cx + dx, y + dy), fill=c)
    for sx in (-5, 5):
        d.ellipse((cx + sx - 2, y + 7, cx + sx + 2, y + 11), fill=(40, 40, 60))
    d.rectangle((cx - 2, y + 4, cx + 1, y + 5), fill=(0, 150, 255))


def render(t=-1):
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    shine = -99 if t < 0 else int((t % 1100) * .16) - 20
    y = H // 2 - 52
    pad(d, W // 2, y)
    word(im, 'GAME', 3, y + 20, 0, shine)
    word(im, 'HUB', 3, y + 46, 4, shine - 26)
    SP = ((-50, 22), (52, 26), (-46, 52), (47, 58), (-30, 4), (32, 6), (-56, 40), (57, 44))
    for i, (dx, dy) in enumerate(SP):
        k = RAINBOW[(i * 3) % 7]
        x, yy = W // 2 + dx, y + dy
        ph = 0 if t < 0 else (t // 70 + i * 5) % 14
        if ph < 4:
            for a, b in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                d.point((x + a, yy + b), fill=k)
            d.point((x, yy), fill=(255, 255, 255))
        elif ph < 7:
            d.point((x, yy), fill=k)
    for i in range(9):
        k = RAINBOW[(i + (0 if t < 0 else t // 120)) % 7]
        d.rectangle((W // 2 - 41 + i * 10, y + 72, W // 2 - 40 + i * 10, y + 73), fill=k)
    yb = y + 74
    for tx, yy, c in (('V4.19', yb + 4, (150, 150, 170)), ('ECRAN 128X128', yb + 16, (0, 200, 255)), ('COULEURS 3 BITS', yb + 24, (0, 200, 255)), ('LUMI 50%  VOL 60%', yb + 32, (0, 200, 255))):
        GB.tiny(d, (W - (len(tx) * 4 - 1)) // 2, yy, tx, c)
    if t >= 0:  # la dalle pulse doucement
        f = .78 + .22 * math.sin(t * .009)
        im = im.point(lambda v: int(v * f))
    return im


if __name__ == '__main__':
    GB.leds(render(), P=5).save(os.path.join(DOC, 'logo-boot.png'))
    frames = [GB.leds(render(t), P=4) for t in range(0, 2200, 40)]
    pal = frames[10].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    q[0].save(os.path.join(DOC, 'logo-boot.gif'), save_all=True, append_images=q[1:], duration=40, loop=0, optimize=True)
    print('ok')
