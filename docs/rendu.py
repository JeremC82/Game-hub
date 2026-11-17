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
TIER_NAME = ['', 'tir rapide', 'double canon', 'triple canon']  # legendes de l'image des vaisseaux (plus affichees en jeu)
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


def hud_top(s, w, score, lives=3, life=None):
    """Haut de l'ecran comme hudTop() : SCORE centre sur une ligne, une icone par vie en haut a droite."""
    num = str(score)
    lw, nw = len('SCORE') * 4 - 1, len(num) * 4 - 1
    x = (w - (lw + 4 + nw)) // 2
    s.tiny(x, 1, 'SCORE', PAL['r'])
    s.tiny(x + lw + 4, 1, num, (230, 230, 230))
    for i in range(min(lives, 5)):
        s.sprite(life or SPR_LIFE, w - 6 - i * 7, 1)


def game_frame(w, h, demo=False, stage=1, banner=False, seed=3):
    """Une image du jeu ; stage : evolution du vaisseau ; banner : annonce du stage (gOverlay)."""
    s = Screen(w, h)
    rnd = random.Random(seed)
    for _ in range(min(128, w * h // 220)):  # etoiles en noir et blanc
        g = int(70 + rnd.uniform(.2, 1.0) * 120)
        s.px(rnd.randrange(w), rnd.randrange(12, h), (g, g, g))
    tall = h >= 128
    types = [0, 0, 1, 1, 1, 2, 2] if h >= 256 else [0, 1, 1, 2, 2] if tall else [0, 1, 2]
    cols = max(4, min(20, (w - 6) // 11 - 1))
    x0 = (w - cols * 11) // 2 + 1 + 2
    dead = {(r, c) for r in range(7) for c in range(cols) if rnd.random() < 0.18}
    for r, t in enumerate(types):
        for c in range(cols):
            if (t == 0 and c % 2) or (r, c) in dead:
                continue
            s.sprite(SPR_E[t][(c + r) % 2], x0 + c * 11, 13 + r * 9, ('p', 'o') if (t == 0 and c == 4) else None)
    py = h - 10 if tall else h - 8
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
    hud_top(s, w, 0 if demo else 4250 * stage)
    if banner:  # annonce du stage
        y = h // 2 - 22 if tall else h // 2 - 10
        s.text(y, f'STAGE {stage}', (255, 220, 0))
        s.text(y + 11, f'BONUS {500 * (stage - 1)}', (230, 230, 230))
    if demo:  # demo sans manette : seulement l'indication pour la connecter (gOverlay)
        hint = 'MANETTE : BOUTON PS'
        s.tiny((w - (len(hint) * 4 - 1)) // 2, h // 2 + 20 if h >= 128 else h - 14, hint, (100, 100, 120))
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


def text_big(s, x, y, t, col):  # police 6x8 en taille 2 (12x16), comme setTextSize(2)
    for ch in t:
        for i in range(5):
            line = GLCD_BYTES[ord(ch) * 5 + i]
            for j in range(8):
                if line & (1 << j):
                    for a in range(2):
                        for b in range(2):
                            s.px(x + i * 2 + a, y + j * 2 + b, col)
        x += 12


def star_screen(seed):  # fond du menu : etoiles en noir et blanc
    s = Screen(128, 128)
    rnd = random.Random(seed)
    for _ in range(74):
        g = int(70 + rnd.uniform(.2, 1.0) * 120)
        s.px(rnd.randrange(128), rnd.randrange(128), (g, g, g))
    return s


def pad_battery(s, w, pct, chg=False, frame=0):
    """Batterie de la manette en haut a droite du menu, comme drawPadBattery()."""
    TF.setdefault('%', [5, 1, 2, 4, 5])
    d = ImageDraw.Draw(s.im)
    col = (40, 230, 80) if pct > 50 else (255, 160, 0) if pct > 20 else (255, 40, 40)
    bx = w - 10
    d.rectangle((bx, 0, bx + 7, 4), outline=(150, 150, 170))
    d.line((bx + 8, 1, bx + 8, 3), fill=(150, 150, 170))
    fill = frame % 7 if chg and pct < 100 else (pct * 6 + 50) // 100
    if chg:
        col = (255, 220, 0)
    if fill > 0:
        d.rectangle((bx + 1, 1, bx + fill, 3), fill=col)
    t = '%d%%' % pct
    tx = bx - 3 - (len(t) * 4 - 1)
    s.tiny(tx, 0, t, (255, 220, 0) if chg else (200, 200, 210))
    if chg:
        ex, y = tx - 5, (255, 220, 0)
        for p in ((ex + 2, 0), (ex + 1, 1), (ex, 2), (ex + 1, 2), (ex + 2, 2), (ex + 1, 3), (ex, 4)):
            s.px(p[0], p[1], y)


pages = []
for page in ('menu', 'scores', 'nom'):
    s = star_screen(11)
    if page == 'menu':  # titre, record avec le nom, puis le menu
        text_big(s, (128 - 72) // 2, 2, 'ARCADE', (40, 230, 80))
        s.text(19, 'GAMES', (140, 255, 150))
        s.text(29, 'RECORD 30800 JER', (255, 40, 40))
        y = 64 - 22
        box(s, y - 6, y + 6 * 11 + 16)
        for i, t in enumerate(('> JOUER <RAID>', 'NIVEAU NORMAL', 'LUMINOSITE 60%', 'VOLUME 60%', 'SCORES', 'DEMO')):
            text_bg(s, y + i * 11, t, SEL if i == 0 else OFF)
        text_bg(s, y + 6 * 11 + 4, 'OPTIONS : JOUER', DIM)
        pad_battery(s, 128, 70)
        title, sub = 'Menu', 'Gauche / droite sur JOUER : choix du jeu. En haut a droite : batterie de la manette'
    elif page == 'scores':  # tableau avec les noms (3 lettres)
        y = 64 - 36
        box(s, y - 5, y + 6 * 11 + 18)
        text_bg(s, y, 'SCORES SPACE DEFENDER', SEL)
        rows = ['1 JER  30800 N S6', '2 DEN  21450 D S4', '3 LEA  12750 N S3', '4 MAX   8500 F S2', '5 ---   4200 N S1']
        for i, t in enumerate(rows):
            text_bg(s, y + (i + 1) * 11 + 2, t, (230, 230, 230) if i == 0 else OFF)
        text_bg(s, y + 6 * 11 + 6, 'X : RETOUR', DIM)
        pad_battery(s, 128, 40, True, 3)
        title, sub = 'Meilleurs scores', 'Rang, nom, score, niveau (F/N/D), stage'
    else:  # saisie du nom, facon borne d'arcade
        y = 64 - 40
        box(s, y - 6, y + 74)
        s.text(y, 'NOUVEAU RECORD !', SEL)
        s.text(y + 11, 'SCORE 31250', (230, 230, 230))
        ly, lx = y + 26, (128 - 44) // 2
        for k, ch in enumerate('JER'):
            text_big(s, lx + k * 16, ly, ch, SEL if k == 1 else (0, 220, 255))
        ImageDraw.Draw(s.im).rectangle((lx + 16, ly + 17, lx + 16 + 9, ly + 18), fill=SEL)
        s.text(y + 50, 'HAUT/BAS : LETTRE', DIM)
        s.text(y + 61, 'OPTIONS : VALIDER', DIM)
        title, sub = 'Saisie du nom', '3 lettres, a chaque record (top 5)'
    pages.append(panels(s.im, 1, 2, title, sub, pitch=4))
gap = 20
out = Image.new('RGB', (sum(p.width for p in pages) + gap * 2, pages[0].height), (12, 12, 16))
x = 0
for p in pages:
    out.paste(p, (x, 0))
    x += p.width + gap
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
