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


e = strings(c_block('SPR_W[4][3][2][GH]'))
SPR_W = [[[e[((w * 3 + t) * 2 + f) * 7:((w * 3 + t) * 2 + f + 1) * 7] for f in range(2)] for t in range(3)] for w in range(4)]
SPR_E = SPR_W[0]  # monde 1 (nebuleuse) pour les apercus
WORLD_NAME = strings(c_block('WORLD_NAME[4]'))
sh = strings(c_block('SPR_SHIP[4][8]'))
SPR_SHIP = [sh[i * 8:(i + 1) * 8] for i in range(4)]
TIER_NAME = strings(c_block('TIER_NAME[4]'))
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


def game_frame(w, h, demo=False, stage=1, banner=False, seed=3):
    """Une image du jeu ; stage : evolution du vaisseau ; banner : annonce du stage (gOverlay)."""
    s = Screen(w, h)
    rnd = random.Random(seed)
    for _ in range(min(128, w * h // 220)):  # etoiles en noir et blanc
        g = int(70 + rnd.uniform(.2, 1.0) * 120)
        s.px(rnd.randrange(w), rnd.randrange(12, h), (g, g, g))
    tall = h >= 128
    types = [0, 0, 1, 1, 1, 2, 2] if h >= 256 else [0, 1, 1, 2, 2] if tall else [0, 1, 2]
    cols = max(4, min(20, (w - 6) // 11))
    x0 = (w - cols * 11) // 2 + 1 + 2
    dead = {(r, c) for r in range(7) for c in range(cols) if rnd.random() < 0.18}
    for r, t in enumerate(types):
        for c in range(cols):
            if (t == 0 and c % 2) or (r, c) in dead:
                continue
            s.sprite(SPR_E[t][(c + r) % 2], x0 + c * 11, 13 + r * 9, ('p', 'o') if (t == 0 and c == 4) else None)
    py = h - 15 if tall else h - 8
    shipx = w // 2 - 6
    if not banner:
        if tall:  # ennemis en pique
            s.sprite(SPR_E[1][1], w // 4 - 2, h - 58)
            s.sprite(SPR_E[2][0], w * 3 // 4 - 8, h - 44)
        else:
            s.sprite(SPR_E[2][0], w // 4, 40)
        for bx, by in ((w * 3 // 4 - 4, py - 30), (w // 4 + 1, py - 20)):  # tirs ennemis
            for k in range(3):
                s.px(bx, by + k, PAL['r'] if k < 2 else PAL['w'])
        for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):  # explosion
            s.px(w // 2 - 14 + dx * 3, (h - 76 if tall else 40) + dy * 3, PAL['y'])
    tier = min(stage, 4) - 1
    s.sprite(SPR_SHIP[tier], shipx, py)
    shots = {0: [(4, 0)], 1: [(4, 0)], 2: [(2, 0), (6, 0)], 3: [(4, -1), (4, 0), (4, 1)]}[tier]
    for sx, vx in shots:  # tirs du vaisseau (eventail au triple canon)
        for n in (0, 1):
            for k in range(3):
                yy = py - 14 - n * 22 + k
                s.px(shipx + sx + vx * (5 + n * 7), yy, (PAL['w'], PAL['y'], PAL['o'])[k])
    s.tiny(1, 0, 'SCORE', PAL['r'])
    s.tiny(1, 6, '0' if demo else str(4250 * stage), (230, 230, 230))
    rx = w // 2 - 11
    s.tiny(rx, 0, 'RECORD', PAL['r'])
    s.tiny(rx, 6, '30800', (230, 230, 230))
    lives = 3
    lw = len('X3') * 4 - 1  # vies en haut a droite : icone + nombre
    s.tiny(w - 1 - lw, 1, 'X3', (230, 230, 230))
    s.sprite(SPR_LIFE, w - 1 - lw - 7, 1)
    if tall:  # stage en bas a droite
        t = f'STAGE {stage}'
        s.tiny(w - 1 - (len(t) * 4 - 1), h - 6, t, PAL['y'])
    if banner:  # annonce du stage
        y = h // 2 - 22 if tall else h // 2 - 10
        s.text(y, f'STAGE {stage}', (255, 220, 0))
        s.text(y + 11, f'BONUS {500 * (stage - 1)}', (230, 230, 230))
        if TIER_NAME[tier]:
            s.text(y + 22, TIER_NAME[tier], (40, 230, 80))
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


CONFIGS = [  # nom, dalles par ligne, lignes, demo, stage, annonce, titre, legende
    ('128x128', 1, 2, False, 3, False, 'Game hub - 128x128 : 2 dalles l\'une sous l\'autre',
     'Chaine : 1 (reliee a l\'ESP32) en haut, puis 2. Stage 3 : double canon.'),
    ('256x64', 2, 1, False, 2, False, 'Game hub - 256x64 : 2 dalles cote a cote',
     'Chaine : 1 (reliee a l\'ESP32) a gauche, puis 2. 3 rangees, vies en haut a droite. Stage 2 : tir rapide.'),
    ('128x256', 1, 4, False, 4, True, 'Game hub - 128x256 : 4 dalles (vertical)',
     'Chaine : 1 en haut, puis 2, 3, 4. 7 rangees.'),
]

imgs = []
for name, cols, rows, demo, stage, banner, title, sub in CONFIGS:
    im = panels(game_frame(128 * cols, 64 * rows, demo, stage, banner), cols, rows, title, sub)
    path = os.path.join(DOC, f'rendu-{name}.png')
    im.save(path)
    imgs.append(im)
    print(path)

# planche : 128x128 et 256x64 a gauche, l'ecran vertical 128x256 a droite
gap = 30
lw = max(imgs[0].width, imgs[1].width)
sheet = Image.new('RGB', (lw + gap + imgs[2].width, max(imgs[0].height + gap + imgs[1].height, imgs[2].height)), (12, 12, 16))
sheet.paste(imgs[0], (0, 0))
sheet.paste(imgs[1], (0, imgs[0].height + gap))
sheet.paste(imgs[2], (lw + gap, 0))
path = os.path.join(DOC, 'rendu.png')
sheet.save(path)
print(path)

# evolution du vaisseau, en grand
P = 14
out = Image.new('RGB', (4 * 200 + 40, 260), (12, 12, 16))
d = ImageDraw.Draw(out)
d.text((20, 14), 'Le vaisseau evolue a chaque stage', fill=(235, 235, 245), font=font(22, True))
for t in range(4):
    sc = Screen(9, 8)
    sc.sprite(SPR_SHIP[t], 0, 0)
    ox, oy = 40 + t * 200, 70
    for y in range(8):
        for x in range(9):
            col = sc.im.getpixel((x, y))
            if col == (0, 0, 0):
                col = (26, 26, 32)
            d.ellipse((ox + x * P + 2, oy + y * P + 2, ox + x * P + P - 2, oy + y * P + P - 2), fill=col)
    d.text((ox, oy + 8 * P + 14), f'Stage {t + 1}' + ('+' if t == 3 else ''), fill=(255, 220, 0), font=font(16, True))
    d.text((ox, oy + 8 * P + 36), TIER_NAME[t] or 'base', fill=(170, 170, 190), font=font(14))
path = os.path.join(DOC, 'rendu-vaisseaux.png')
out.save(path)
print(path)

# menu et meilleurs scores (128x128, par-dessus la demo), comme drawMenu()
def text_bg(s, y, t, col):
    w = len(t) * 6 + 4
    x = (s.w - w) // 2
    ImageDraw.Draw(s.im).rectangle((x, y - 2, x + w - 1, y + 8), fill=(0, 0, 0))
    s.text(y, t, col)


SEL, OFF, DIM = (255, 220, 0), (0, 160, 200), (110, 110, 130)


def box(s, y0, y1):  # cadre noir borde de bleu, comme gBox()
    ImageDraw.Draw(s.im).rectangle((4, y0, 123, y1 - 1), fill=(0, 0, 0), outline=(40, 70, 170))
pages = []
for page in ('menu', 'scores'):
    s = Screen(128, 128)
    s.im = game_frame(128, 128, demo=False, stage=1, seed=11)
    if page == 'menu':
        y = 64 - 22
        box(s, y - 6, y + 4 * 13 + 16)
        for i, t in enumerate(('> JOUER', 'NIVEAU NORMAL', 'SCORES', 'DEMO')):
            text_bg(s, y + i * 13, t, SEL if i == 0 else OFF)
        text_bg(s, y + 4 * 13 + 4, 'OPTIONS : JOUER', DIM)
    else:
        y = 64 - 36
        box(s, y - 5, y + 6 * 11 + 18)
        text_bg(s, y, 'MEILLEURS SCORES', SEL)
        rows = ['1  30800 N S6', '2  21450 D S4', '3  12750 N S3', '4   8500 F S2', '5   4200 N S1']
        for i, t in enumerate(rows):
            text_bg(s, y + (i + 1) * 11 + 2, t, (230, 230, 230) if i == 0 else OFF)
        text_bg(s, y + 6 * 11 + 6, 'X : RETOUR', DIM)
    pages.append(panels(s.im, 1, 2, 'Menu' if page == 'menu' else 'Meilleurs scores',
                        'Croix haut / bas, X : valider, OPTIONS : jouer' if page == 'menu' else 'Rang, score, niveau (F/N/D), stage atteint', pitch=4))
out = Image.new('RGB', (pages[0].width * 2 + 20, pages[0].height), (12, 12, 16))
out.paste(pages[0], (0, 0))
out.paste(pages[1], (pages[0].width + 20, 0))
path = os.path.join(DOC, 'rendu-menu.png')
out.save(path)
print(path)

# les 4 mondes : ennemis en grand, nom et comportement
BEHAV = [['piques en boucle'], ['piques a deux,', 'balancement large'], ['un tir de plus,', 'piques en zigzag'], ['formation qui', 'descend, tirs vises']]
P = 9
out = Image.new('RGB', (4 * 230 + 30, 330), (12, 12, 16))
d = ImageDraw.Draw(out)
d.text((20, 12), 'Un nouveau monde a chaque stage (en boucle, de plus en plus dur)', fill=(235, 235, 245), font=font(20, True))
for w in range(4):
    ox = 25 + w * 230
    d.text((ox, 50), f'Stages {w + 1}, {w + 5}, {w + 9}...', fill=(150, 150, 175), font=font(13))
    d.text((ox, 68), WORLD_NAME[w], fill=(255, 220, 0), font=font(18, True))
    for t in range(3):
        sc = Screen(9, 7)
        sc.sprite(SPR_W[w][t][0], 0, 0)
        for y in range(7):
            for x in range(9):
                col = sc.im.getpixel((x, y))
                if col == (0, 0, 0):
                    col = (26, 26, 32)
                cx, cy = ox + x * P, 100 + t * 72 + y * P
                d.ellipse((cx + 1, cy + 1, cx + P - 1, cy + P - 1), fill=col)
    for k, line in enumerate(BEHAV[w]):
        d.text((ox + 100, 110 + k * 18), line, fill=(170, 170, 190), font=font(13))
path = os.path.join(DOC, 'rendu-mondes.png')
out.save(path)
print(path)
