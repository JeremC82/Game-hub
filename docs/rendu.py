"""Rendus du jeu sur les 3 configurations de dalles (LED simulees).

Les sprites, la palette et la petite police sont lus dans games/games.ino ; le texte 6x8 vient de la police
d'Adafruit GFX (glcdfont.c). Les positions suivent le code du jeu (formation, vaisseau, score, vies, vague).
Usage : python docs/rendu.py [chemin de glcdfont.c]
"""
import os
import random
import re
import sys

from PIL import Image, ImageDraw, ImageFont

DOC = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
GLCD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    DOC, '..', '..', 'clockV1', 'build', 'sketchbook', 'libraries', 'Adafruit_GFX_Library', 'glcdfont.c')


# ---------------------------------------------------------------- donnees lues dans games.ino
def strings(block):
    return re.findall(r'"([^"]*)"', block)


def c_block(name):
    m = re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\};', SRC, re.S)
    return m.group(1)


e = strings(c_block('SPR_E[3][2][GH]'))
SPR_E = [[e[(t * 2 + f) * 7:(t * 2 + f + 1) * 7] for f in range(2)] for t in range(3)]
SPR_SHIP = strings(c_block('SPR_SHIP[8]'))
SPR_LIFE = strings(c_block('SPR_LIFE[4]'))
PAL_CH = re.search(r'G_PAL_CH\[\] = "(\w+)"', SRC).group(1)
rgbs = [tuple(map(int, t)) for t in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', c_block('G_PAL_RGB[9][3]'))]
PAL = dict(zip(PAL_CH, rgbs))
TF = {str(i): list(map(int, g)) for i, g in enumerate(re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', c_block('TF_DIG[10][5]')))}
for ch, vals in re.findall(r'TF_([A-Z])\[5\] = \{([^}]*)\}', SRC):
    TF[ch] = [int(v) for v in vals.split(',')]
TF['O'] = TF['0']
GLCD_BYTES = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', open(GLCD, encoding='utf-8').read())]


# ---------------------------------------------------------------- dessin de l'ecran du jeu
class Screen:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new('RGB', (w, h))

    def px(self, x, y, col):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.im.putpixel((int(x), int(y)), col)

    def sprite(self, rows, x, y, swap=None):
        for r, line in enumerate(rows):
            for c, ch in enumerate(line):
                if ch == '.':
                    continue
                if swap and ch == swap[0]:
                    ch = swap[1]
                self.px(x + c, y + r, PAL[ch])

    def tiny(self, x, y, txt, col):
        for ch in txt:
            for r, bits in enumerate(TF.get(ch, [0] * 5)):
                for c in range(3):
                    if bits & (4 >> c):
                        self.px(x + c, y + r, col)
            x += 4

    def text(self, y, txt, col):  # texte 6x8 centre (police d'Adafruit GFX, comme gText())
        x = (self.w - len(txt) * 6) // 2
        for ch in txt:
            for i in range(5):
                line = GLCD_BYTES[ord(ch) * 5 + i]
                for j in range(8):
                    if line & (1 << j):
                        self.px(x + i, y + j, col)
            x += 6


def game_frame(w, h, demo=False, seed=3):
    s = Screen(w, h)
    rnd = random.Random(seed)
    for _ in range(min(128, w * h // 220)):  # etoiles de couleur
        col = PAL[rnd.choice(PAL_CH)]
        k = rnd.uniform(.15, .45)
        s.px(rnd.randrange(w), rnd.randrange(12, h), tuple(int(v * k) for v in col))
    tall = h >= 128
    types = [0, 1, 1, 2, 2] if tall else [0, 1, 2]
    cols = max(4, min(20, (w - 6) // 11))
    x0 = (w - cols * 11) // 2 + 1 + 2
    dead = {(r, c) for r in range(5) for c in range(cols) if rnd.random() < 0.18}
    for r, t in enumerate(types):
        for c in range(cols):
            if (t == 0 and c % 2) or (r, c) in dead:
                continue
            s.sprite(SPR_E[t][(c + r) % 2], x0 + c * 11, 13 + r * 9, ('p', 'o') if (t == 0 and c == 4) else None)
    py = h - 15 if tall else h - 8
    shipx = w // 2 - 6
    if tall:  # ennemis en pique
        s.sprite(SPR_E[1][1], w // 4 - 2, 70)
        s.sprite(SPR_E[2][0], w * 3 // 4 - 8, 84)
    else:
        s.sprite(SPR_E[2][0], w // 4, 40)
    s.sprite(SPR_SHIP, shipx, py)
    for k, y in enumerate((py - 14, py - 13, py - 12)):  # tir du vaisseau
        s.px(shipx + 4, y, (PAL['w'], PAL['y'], PAL['o'])[k])
    for bx, by in ((w * 3 // 4 - 4, py - 30), (w // 4 + 1, py - 20)):  # tirs ennemis
        for k in range(3):
            s.px(bx, by + k, PAL['r'] if k < 2 else PAL['w'])
    for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):  # explosion
        s.px(w // 2 - 14 + dx * 3, (52 if tall else 40) + dy * 3, PAL['y'])
    s.tiny(1, 0, 'SCORE', PAL['r'])
    s.tiny(1, 6, '0' if demo else '12450', (230, 230, 230))
    rx = w // 2 - 11
    s.tiny(rx, 0, 'RECORD', PAL['r'])
    s.tiny(rx, 6, '30800', (230, 230, 230))
    lives, wave = 3, 3
    if tall:  # ligne du bas : vies a gauche, vague a droite
        for i in range(lives - 1):
            s.sprite(SPR_LIFE, 1 + i * 7, h - 5)
        for i in range(wave):
            x = w - 4 - i * 4
            for xx, yy, ch in ((x, h - 5, 'y'), (x + 1, h - 5, 'y'), (x, h - 4, 'r'), (x + 1, h - 4, 'r'),
                               (x, h - 3, 'r'), (x + 1, h - 3, 'r'), (x, h - 2, 'b')):
                s.px(xx, yy, PAL[ch])
    else:  # ecran bas : vies en haut a droite
        for i in range(lives - 1):
            s.sprite(SPR_LIFE, w - 6 - i * 7, 1)
    if demo:  # textes de la demo (gOverlay)
        s.text(h // 2 + 6, 'APPUIE SUR FEU', (0, 220, 255))
        s.text(h // 2 + 18, '192.168.1.42', (110, 110, 130))
    return s.im


# ---------------------------------------------------------------- dalles LED simulees
def font(size, bold=False):
    for name in (('arialbd.ttf' if bold else 'arial.ttf'), 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def panels(img, cols, rows, title, sub, pitch=5):
    """Ecran en LED rondes, limites des dalles et ordre de la chaine (1 = dalle reliee a l'ESP32)."""
    w, h = img.size
    m, top = 40, 100
    out = Image.new('RGB', (w * pitch + 2 * m, h * pitch + top + m + 10), (12, 12, 16))
    d = ImageDraw.Draw(out)
    d.text((m, 14), title, fill=(235, 235, 245), font=font(22, True))
    d.text((m, 42), sub, fill=(150, 150, 175), font=font(14))
    rad = pitch * 0.42
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))
            if r + g + b < 30:
                r, g, b = 26, 26, 32  # LED eteinte
            cx, cy = m + x * pitch + pitch / 2, top + y * pitch + pitch / 2
            d.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), fill=(r, g, b))
    f = font(13, True)
    for pr in range(rows):
        for pc in range(cols):
            x0, y0 = m + pc * 128 * pitch, top + pr * 64 * pitch
            d.rectangle((x0, y0, x0 + 128 * pitch - 1, y0 + 64 * pitch - 1), outline=(80, 80, 100), width=2)
            n = pr * cols + pc + 1  # numero hors de l'ecran : au-dessus (rangee du haut) ou en dessous
            lab = f' Dalle {n}{" (reliee a l ESP32)" if n == 1 else ""} '
            tw = d.textlength(lab, font=f)
            ly = y0 - 26 if pr == 0 else y0 + 64 * pitch + 6
            d.rounded_rectangle((x0, ly, x0 + tw, ly + 20), 5, fill=(40, 40, 70))
            d.text((x0, ly + 3), lab, fill=(255, 220, 0), font=f)
    return out


CONFIGS = [
    ('128x128', 1, 2, False, 'Game hub - 128x128 : 2 dalles 128x64 l\'une sous l\'autre',
     'Chaine : 1 (reliee a l\'ESP32) en haut, puis 2 en bas. 5 rangees d\'ennemis, vies et vague en bas.'),
    ('256x64', 2, 1, False, 'Game hub - 256x64 : 2 dalles 128x64 cote a cote',
     'Chaine : 1 (reliee a l\'ESP32) a gauche, puis 2 a droite. 3 rangees d\'ennemis, vies en haut a droite.'),
    ('256x128', 2, 2, True, 'Game hub - 256x128 : 4 dalles 128x64',
     'Chaine : 1 haut gauche, 2 haut droite, 3 bas gauche, 4 bas droite (meme sens). Ici en demo, en attente d\'un joueur.'),
]

imgs = []
for name, cols, rows, demo, title, sub in CONFIGS:
    im = panels(game_frame(128 * cols, 64 * rows, demo), cols, rows, title, sub)
    path = os.path.join(DOC, f'rendu-{name}.png')
    im.save(path)
    imgs.append(im)
    print(path)

# planche des 3 configurations
gap = 20
W = max(i.width for i in imgs)
sheet = Image.new('RGB', (W, sum(i.height for i in imgs) + gap * (len(imgs) - 1)), (12, 12, 16))
y = 0
for im in imgs:
    sheet.paste(im, (0, y))
    y += im.height + gap
path = os.path.join(DOC, 'rendu.png')
sheet.save(path)
print(path)
