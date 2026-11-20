"""GIF des jeux (apercus simules, comme les demos du jeu) : RAID, RAQUETTES, BULLES, LABYRINTHE, ORBITUS, BRIQUES, BLOCS, CRUCH.

Sorties : docs/jeu-raid.gif, docs/jeu-raquettes.gif, docs/jeu-bulles.gif, docs/jeu-labyrinthe.gif, docs/jeu-orbite.gif
Usage : python docs/gif_jeux.py [raid course raquettes bulles labyrinthe orbite briques blocs cruch]
Memes sprites, regles et pilotes automatiques que games.ino (version simplifiee pour l'apercu).
"""
import math
import os
import random
import re

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
import sys
GIF_ARGS = sys.argv[1:]  # jeux a generer (rendu.py lit sys.argv : on le vide avant de l'executer)
sys.argv = sys.argv[:1]
src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)
Screen, SPR_W, SPR_SHIP, SPR_LIFE, PAL, font, SRC, GLCD = (ns['Screen'], ns['SPR_W'], ns['SPR_SHIP'], ns['SPR_LIFE'], ns['PAL'],
                                                           ns['font'], ns['SRC'], ns['GLCD_BYTES'])
W = H = 128
P = 4
WHITE = (230, 230, 230)
GOLD = (230, 180, 40)


def led(img):
    out = Image.new('RGB', (W * P + 8, H * P + 8), (10, 10, 14))
    d = ImageDraw.Draw(out)
    for y in range(H):
        for x in range(W):
            c = img.getpixel((x, y))
            if sum(c) < 12:
                c = (22, 22, 28)
            d.rectangle((4 + x * P, 4 + y * P, 4 + x * P + P - 2, 4 + y * P + P - 2), fill=c)
    d.line((4, 4 + 64 * P - 1, 4 + W * P, 4 + 64 * P - 1), fill=(55, 55, 70), width=2)
    return out


def led64(img):  # une seule dalle 128x64
    w, h = img.size
    out = Image.new('RGB', (w * P + 8, h * P + 8), (10, 10, 14))
    d = ImageDraw.Draw(out)
    for y in range(h):
        for x in range(w):
            c = img.getpixel((x, y))
            if sum(c) < 12:
                c = (22, 22, 28)
            d.rectangle((4 + x * P, 4 + y * P, 4 + x * P + P - 2, 4 + y * P + P - 2), fill=c)
    return out


def hud(s, score, lives=3, life=None):  # score centre sur une ligne, vies en icones (comme le jeu)
    ns['hud_top'](s, W, score, lives, life)


def char6(s, x, y, ch, col):  # une lettre de la police 6x8 du jeu
    for i in range(5):
        line = GLCD[ord(ch) * 5 + i]
        for j in range(8):
            if line & (1 << j):
                s.px(x + i, y + j, col)


def boom(s, b):
    for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):
        s.px(b[0] + dx * b[2] // 2, b[1] + dy * b[2] // 2, PAL['w'] if b[2] < 4 else PAL['y'] if b[2] < 7 else PAL['r'])


def stars_init(rnd, n=70):
    return [[rnd.randrange(W), rnd.uniform(12, H), rnd.uniform(.2, 1)] for _ in range(n)]


def stars_draw(s, st, speed, rnd):
    for a in st:
        a[1] += a[2] * speed
        if a[1] >= H:
            a[1], a[0] = 12, rnd.randrange(W)
        g = int(70 + a[2] * 120)
        s.px(a[0], int(a[1]), (g, g, g))


def page(panel, title, sub, lines):
    im = Image.new('RGB', (panel.width + 330, panel.height + 110), (16, 16, 22))
    d = ImageDraw.Draw(im)
    d.text((20, 16), title, fill=(235, 235, 245), font=font(24, True))
    d.text((20, 50), sub, fill=(150, 150, 175), font=font(16))
    im.paste(panel, (20, 90))
    for k, t in enumerate(lines):
        hdr = t in ('Commandes :', 'Regles :')
        d.text((panel.width + 40, 110 + k * 30), t, fill=(255, 220, 0) if hdr else (200, 200, 215), font=font(19, hdr))
    return im


def save(frames, name, ms=80):
    pal = frames[len(frames) // 2].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(DOC, name)
    q[0].save(path, save_all=True, append_images=q[1:], duration=ms, loop=0, optimize=True)
    print(path, len(frames), 'images', os.path.getsize(path) // 1024, 'Ko')


# ================================================================ RAID
SRC_INO = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
SPR_BOSS_ROWS = re.findall(r'"([^"]*)"', re.search(r'SPR_BOSS\[9\] = \{(.*?)\};', open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read(), re.S).group(1))


def raid():
    rnd = random.Random(8)
    st = stars_init(rnd)
    far = [[rnd.randrange(W), rnd.randrange(12, H)] for _ in range(24)]
    px, py, tier, shield, bombs_left = 58.0, 112.0, 1, 1, 2
    en, shots, bombs, booms = [], [], [], []
    cap, boss, flash = None, None, 0
    score, frames, groups, spawn = 0, [], 0, 10
    for f in range(420):
        if flash:
            flash -= 1
        if boss is None:
            spawn -= 1
            if spawn <= 0 and groups < 4:
                pat = (0, 3, 4, 2)[groups]
                n = 2 if pat == 4 else 5
                for k in range(n):
                    e = {'x': W * (k + 1) / (n + 1) - 4, 'y': -10, 't': -k * 12, 'pat': pat, 'type': groups % 3, 'vx': 0, 'vy': 0,
                         'hp': 5 if pat == 4 else 1, 'flash': 0, 'carrier': k == n - 1}
                    if pat == 2:
                        e.update(x=W / 2 - 4, vx=0.9 if k & 1 else -0.9)
                    if pat == 3:
                        e.update(x=rnd.randrange(8, W - 16))
                    en.append(e)
                groups += 1
                spawn = 75
            if groups >= 4 and not en:
                boss = {'x': W / 2 - 16, 'y': -18.0, 'hp': 70, 'max': 70, 't': 0, 'flash': 0}
        for e in list(en):
            e['t'] += 1
            e['flash'] = max(0, e['flash'] - 1)
            t = e['t']
            if t < 0:
                continue
            if e['pat'] == 0:
                e['y'] += 0.7; e['x'] += math.sin(t * 0.05) * 0.9
            elif e['pat'] == 2:
                e['x'] += e['vx']; e['y'] += 0.8
                if e['x'] < 2 or e['x'] > W - 11:
                    e['vx'] = -e['vx']
            elif e['pat'] == 3:  # kamikaze
                if t < 50:
                    e['y'] += 0.6
                else:
                    if t == 50:
                        dx, dy = px - e['x'], py - e['y']
                        d = math.hypot(dx, dy) + .01
                        e['vx'], e['vy'] = dx / d * 2.2, dy / d * 2.2
                    e['x'] += e['vx']; e['y'] += e['vy']
            else:  # tourelle
                if t < 40:
                    e['y'] += 0.8
                elif t > 260:
                    e['y'] += 0.9
                elif t % 60 == 0:
                    for i in range(3):
                        a = math.pi / 2 + (i - 1) * 0.32
                        bombs.append([e['x'] + 4, e['y'] + 7, math.cos(a) * 1.1, math.sin(a) * 1.1])
            if e['y'] > H or e['x'] < -14 or e['x'] > W + 14:
                en.remove(e)
        if boss:
            boss['t'] += 1
            boss['flash'] = max(0, boss['flash'] - 1)
            ph2 = boss['hp'] < boss['max'] / 2
            if boss['y'] < 16:
                boss['y'] += 0.5
            else:
                boss['x'] = W / 2 - 16 + math.sin(boss['t'] * (0.035 if ph2 else 0.02)) * (W / 2 - 20)
                if boss['t'] % (32 if ph2 else 45) == 0:
                    n = 7 if ph2 else 5
                    for i in range(n):
                        a = math.pi / 2 + (i - (n - 1) / 2) * 0.32
                        bombs.append([boss['x'] + 16, boss['y'] + 14, math.cos(a) * 1.2, math.sin(a) * 1.2])
                if ph2 and boss['t'] % 6 == 0:  # 2e phase : spirale
                    a = boss['t'] * 0.31
                    bombs.append([boss['x'] + 16, boss['y'] + 8, math.cos(a) * 1.1, abs(math.sin(a)) * 1.1 + 0.3])
        # pilote automatique
        tx = px
        cands = [e for e in en if e['t'] >= 0 and e['y'] > 10]
        if cands:
            tx = min(cands, key=lambda e: abs(e['x'] - px))['x']
        if boss:
            tx = boss['x'] + 12
        if cap:
            tx = cap[0]
        for b in bombs:
            if py - 26 < b[1] < py + 4 and abs(b[0] - px - 4) < 7:
                tx = px + 14 if b[0] < px + 4 else px - 14
        px = max(0, min(W - 9, px + max(-2, min(2, tx - px))))
        if f == 300 and bombs_left:  # bombe : eclair, ecran nettoye, boss blesse
            bombs_left -= 1
            flash = 4
            for e in en:
                booms.append([int(e['x'] + 4), int(e['y'] + 3), 1])
            en, bombs = [], []
            if boss:
                boss['hp'] -= 12
        if f % 6 == 0:
            n = (1, 2, 3, 5)[tier]
            for i in range(n):
                vx = (i - (n - 1) / 2) * 0.55 if n >= 3 else 0
                shots.append([px + 4 + ((2 if i else -2) if n == 2 else 0), py - 2, vx])
        for sh in shots:
            sh[0] += sh[2]; sh[1] -= 3.6
            for e in list(en):
                if e['t'] >= 0 and e['x'] <= sh[0] <= e['x'] + 8 and e['y'] <= sh[1] <= e['y'] + 7:
                    sh[1] = -10; e['flash'] = 3; e['hp'] -= 1
                    if e['hp'] <= 0:
                        en.remove(e); score += 100
                        booms.append([int(e['x'] + 4), int(e['y'] + 3), 1])
                        if e['carrier'] and not cap:
                            cap = [e['x'] + 1, e['y'], 'PSB'[groups % 3]]
                    break
            if boss and boss['x'] <= sh[0] <= boss['x'] + 31 and boss['y'] <= sh[1] <= boss['y'] + 14:
                sh[1] = -10; boss['hp'] -= 1; boss['flash'] = 2
                if boss['hp'] <= 0:
                    for k in range(8):
                        booms.append([int(boss['x'] + rnd.randrange(32)), int(boss['y'] + rnd.randrange(15)), 1])
                    boss, groups, spawn, score = None, 0, 40, score + 2000
        shots = [sh for sh in shots if sh[1] > 12]
        if cap:
            cap[1] += 0.6
            if abs(cap[0] - px) < 8 and abs(cap[1] - py) < 7:
                if cap[2] == 'P':
                    tier = min(3, tier + 1)
                elif cap[2] == 'S':
                    shield = 1
                else:
                    bombs_left = min(3, bombs_left + 1)
                cap, score = None, score + 500
            elif cap[1] > H:
                cap = None
        for b in bombs:
            b[0] += b[2]; b[1] += b[3]
        bombs = [b for b in bombs if 0 <= b[0] < W and 10 < b[1] < H]
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 1.4, rnd)
        for a in far:
            a[1] = 12 + (a[1] - 11) % (H - 12)
            s.px(a[0], a[1], (45, 45, 60))
        d = ImageDraw.Draw(s.im)
        if flash:
            d.rectangle((0, 11, W - 1, H - 1), outline=PAL['w'])
            d.rectangle((2, 13, W - 3, H - 3), outline=PAL['y'])
        for e in en:
            if e['t'] >= 0:
                rows = SPR_W[1][e['type']][(f // 6) & 1]
                if e['flash']:
                    rows = [re.sub('[^.]', 'w', r) for r in rows]
                s.sprite(rows, int(e['x']), int(e['y']))
        if boss:  # boss : le vaisseau-mere du 1er jeu, en double taille (V3.93)
            x, y = int(boss['x']), int(boss['y'])
            p2 = boss['hp'] < boss['max'] / 2
            for r_, line in enumerate(SPR_BOSS_ROWS):
                for c_, ch in enumerate(line):
                    if ch == '.':
                        continue
                    if boss['flash'] and ch == 'p':
                        ch = 'w'
                    elif p2 and ch == 'b':
                        ch = 'r'
                    d.rectangle((x + c_ * 2, y + r_ * 2, x + c_ * 2 + 1, y + r_ * 2 + 1), fill=PAL[ch])
            d.rectangle((10, 12, W - 11, 14), outline=(90, 90, 110))
            d.line((10, 13, 10 + (W - 21) * max(0, boss['hp']) // boss['max'], 13),
                   fill=PAL['o'] if boss['hp'] < boss['max'] / 2 else PAL['r'])
        if cap:
            col = PAL['w'] if f & 4 else {'P': PAL['y'], 'S': PAL['c'], 'B': PAL['r']}[cap[2]]
            d.rounded_rectangle((int(cap[0]) - 1, int(cap[1]) - 1, int(cap[0]) + 7, int(cap[1]) + 8), radius=2, outline=col)
            icon = {'P': ("...y...", "..yyy..", ".y.y.y.", "...y...", "..yyy..", ".y.y.y.", "...y..."),
                    'S': ("ccccccc", "cwwcccc", "cwccccc", "ccccccc", ".ccccc.", "..ccc..", "...c..."),
                    'B': ("....o..", "...o.o.", "..rrr..", ".rwrrr.", ".rrrrr.", ".rrrrr.", "..rrr..")}[cap[2]]
            for r_, line in enumerate(icon):
                for c_, ch in enumerate(line):
                    if ch != '.':
                        s.px(int(cap[0]) + c_, int(cap[1]) + 1 + r_, PAL[ch])
        for sh in shots:
            s.px(int(sh[0]), int(sh[1]), PAL['w']); s.px(int(sh[0]), int(sh[1]) + 1, PAL['y'])
        for b in bombs:
            s.px(int(b[0]), int(b[1]), PAL['r']); s.px(int(b[0]) + 1, int(b[1]), PAL['r'])
        for b in booms:
            boom(s, b)
        s.sprite(SPR_SHIP[tier], int(px), int(py))
        if shield:
            d.ellipse((int(px) - 3, int(py) - 3, int(px) + 11, int(py) + 11), outline=PAL['c'] if f & 2 else PAL['b'])
        for i in range(bombs_left):
            d.ellipse((1 + i * 7, H - 5, 5 + i * 7, H - 1), fill=PAL['r'])
        hud(s, score)
        frames.append(page(led(s.im), 'RAID', 'Shoot vertical : kamikazes, tourelles, capsules P / S / B, boss a 2 phases.',
                           ['Commandes :', 'croix / stick : bouger', 'X : tirer', 'rond : bombe', '', 'Regles :',
                            'P : arme (3 niveaux)', 'S : bouclier (1 tir)', 'B : une bombe de plus', '8 vagues puis le boss',
                            '2e phase sous 50 % :', 'tirs en spirale']))
    save(frames, 'jeu-raid.gif')


# ================================================================ RAQUETTES
def raquettes_old():  # ancienne version verticale (plus utilisee)
    pass


# ================================================================ BULLES
def bulles():
    rnd = random.Random(11)
    st = stars_init(rnd, 25)
    blob = re.findall(r'"([.wrybcgpov]{8})"', re.search(r'SPR_BLOB\[2\]\[8\] = \{(.*?)\};', SRC, re.S).group(1))
    BLOB = [blob[0:8], blob[8:16]]  # heros original : blob cyan
    FL = H - 4
    plats = [(0, W, FL), (6, 50, FL - 22), (78, W - 6, FL - 22), (40, 88, FL - 44), (6, 30, FL - 66), (98, W - 6, FL - 66),
             (34, 94, FL - 88)]

    def enemy(x, y, vx, t):
        return {'x': x, 'y': y, 'vx': vx, 'vy': 0.0, 't': t, 'on': True}
    en = [enemy(10.0, FL - 29.0, 0.5, 0), enemy(90.0, FL - 29.0, -0.5, 1), enemy(50.0, FL - 51.0, 0.5, 2), enemy(100.0, FL - 73.0, -0.5, 0)]
    px, py, vy, dr_ = 10.0, FL - 8.0, 0.0, 1
    bubs, gems, booms, score, frames, ground, cool = [], [], [], 0, [], True, 0

    def land(x, y, v, w, h):
        if v < 0:
            return y, v, False
        for p in plats:
            if x + w - 2 >= p[0] and x + 1 <= p[1] and y + h >= p[2] and y + h - v <= p[2] + 2:
                return p[2] - h, 0.0, True
        return y, v, False
    for f in range(420):
        full = [b for b in bubs if b['en'] is not None]
        live = [e for e in en if e['on']]
        if full:  # pilote automatique : va eclater une bulle pleine, sinon vise l'ennemi le plus proche
            tx, ty = full[0]['x'], full[0]['y']
        elif live:
            e = min(live, key=lambda e: abs(e['x'] - px) + abs(e['y'] - py) * 2)
            tx, ty = e['x'], e['y']
        else:
            tx, ty = px, py
        if tx < px - 2:
            px -= 1.3
            dr_ = -1
        elif tx > px + 2:
            px += 1.3
            dr_ = 1
        px = max(4, min(W - 12, px))
        if ground and ty < py - 10 and rnd.random() < 0.08:
            vy, ground = -4.2, False
        cool -= 1
        if not full and live and abs(ty - py) < 6 and abs(tx - px) < 50 and cool <= 0:
            bubs.append({'x': px + (8 if dr_ > 0 else -6), 'y': py + 1, 'vx': 3.2 * dr_, 't': 0, 'en': None})
            cool = 12
        vy = min(vy + 0.3, 3.5)
        py += vy
        py, vy, ground = land(px, py, vy, 8, 8)
        for b in list(bubs):
            b['t'] += 1
            if b['t'] < 14:
                b['x'] += b['vx']
            else:
                b['y'] = max(16, b['y'] - 0.6)
                b['x'] += math.sin(b['t'] * 0.08) * 0.3
            b['x'] = max(6, min(W - 6, b['x']))
            if b['en'] is None and b['t'] < 14:
                for e in en:
                    if e['on'] and abs(e['x'] + 4 - b['x']) < 7 and abs(e['y'] + 3 - b['y']) < 7:
                        e['on'] = False
                        b['en'], b['t'] = e, 14
                        break
            if abs(b['x'] - px - 4) < 8 and abs(b['y'] - py - 4) < 8 and b['t'] > 6:
                bubs.remove(b)
                booms.append([int(b['x']), int(b['y']), 1])
                if b['en'] is not None:
                    gems.append([b['x'] - 2, b['y'], -1.5])
                    score += 100
                continue
            if b['t'] > 260:
                bubs.remove(b)
        for e in en:
            if not e['on']:
                continue
            e['x'] += e['vx']
            if e['x'] < 4 or e['x'] > W - 13:
                e['vx'] = -e['vx']
            e['vy'] = min(e['vy'] + 0.3, 3)
            e['y'] += e['vy']
            e['y'], e['vy'], _ = land(e['x'], e['y'], e['vy'], 9, 7)
        for gm in list(gems):
            gm[2] = min(gm[2] + 0.25, 3)
            gm[1] += gm[2]
            gm[1], gm[2], _ = land(gm[0], gm[1], gm[2], 5, 5)
            if abs(gm[0] + 2 - px - 4) < 7 and abs(gm[1] + 2 - py - 4) < 8:
                gems.remove(gm)
                score += 500
        if not any(e['on'] for e in en) and not full and not gems:  # niveau suivant (dans l'apercu : nouveaux ennemis)
            for k, e in enumerate(en):
                e.update(on=True, x=float(rnd.randrange(10, W - 20)), y=float(plats[1 + k % 6][2] - 7), vx=0.6 if k & 1 else -0.6)
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 0.05, rnd)
        dw = ImageDraw.Draw(s.im)
        PC, PH, PD = (200, 80, 160), (255, 160, 240), (66, 26, 53)
        for p in plats:  # plateformes epaisses en briques (comme bDraw)
            th = min(5, H - p[2])
            dw.rectangle((p[0], p[2], p[1] - 1, p[2] + th - 1), fill=PC)
            dw.line((p[0], p[2], p[1] - 1, p[2]), fill=PH)
            if th >= 5:
                dw.line((p[0], p[2] + 2, p[1] - 1, p[2] + 2), fill=PD)
                dw.line((p[0], p[2] + 4, p[1] - 1, p[2] + 4), fill=PD)
                for x in range(p[0] + 4, p[1], 8):
                    s.px(x, p[2] + 1, PD)
                for x in range(p[0] + 8, p[1], 8):
                    s.px(x, p[2] + 3, PD)
        for side in (0, 1):  # murs de 5 pixels en briques
            x = W - 5 if side else 0
            dw.rectangle((x, 10, x + 4, H - 1), fill=PC)
            dw.line((x if side else x + 4, 10, x if side else x + 4, H - 1), fill=PH)
            for y in range(12, H, 4):
                dw.line((x, y, x + 4, y), fill=PD)
        for e in en:
            if e['on']:
                s.sprite(SPR_W[0][e['t']][(f // 6) & 1], int(e['x']), int(e['y']))
        for b in bubs:
            r = 6 if b['en'] is not None else 4
            if b['en'] is not None:
                s.sprite(SPR_W[0][b['en']['t']][(f // 10) & 1], int(b['x']) - 4, int(b['y']) - 3)
            dw.ellipse((int(b['x']) - r, int(b['y']) - r, int(b['x']) + r, int(b['y']) + r), outline=PAL['g'])
            s.px(int(b['x']) - r // 2, int(b['y']) - r // 2, PAL['w'])
        for gm in gems:
            c = PAL['c'] if f & 8 else PAL['p']
            for dx, dy in ((2, 0), (1, 1), (2, 1), (3, 1), (0, 2), (1, 2), (2, 2), (3, 2), (4, 2), (1, 3), (2, 3), (3, 3), (2, 4)):
                s.px(int(gm[0]) + dx, int(gm[1]) + dy, c)
        for b in booms:
            boom(s, b)
        s.sprite(BLOB[(f // 6) & 1 if ground else 0], int(px), int(py))
        hud(s, score)
        frames.append(page(led(s.im), 'BULLES', 'Un petit blob souffle des bulles : enfermer les ennemis, puis faire eclater les bulles.',
                           ['Commandes :', 'gauche / droite : marcher', 'haut ou rond : sauter', 'X : souffler une bulle', '',
                            'Regles :', 'bulle + ennemi = piege', 'toucher la bulle : elle', "eclate, l'ennemi devient",
                            'un cristal (500)', "sinon il s'echappe", 'tous elimines : niveau', 'suivant, +1 vie']))
    save(frames, 'jeu-bulles.gif')




# ================================================================ LABYRINTHE
def labyrinthe():
    rnd = random.Random(3)
    maze = re.findall(r'"([# .oPE]{21})"', re.search(r'L_MAZE\[L_ROWS\] = \{(.*?)\};', SRC, re.S).group(1))
    R, C, T = len(maze), 21, 6
    OX, OY = (W - C * T) // 2, 8 + ((H - 8) - R * T) // 2
    dots = {(r, c): (1 if maze[r][c] == '.' else 2) for r in range(R) for c in range(C) if maze[r][c] in '.o'}

    def opened(c, r, player):
        if r < 0 or r >= R:
            return False
        if c < 0 or c >= C:
            return r == 9
        if maze[r][c] == '#':
            return False
        if player and ((r == 8 and c == 10) or (r == 9 and 9 <= c <= 11)):
            return False
        return True
    DIRS = ((0, -1), (-1, 0), (0, 1), (1, 0))
    # scarabee (pilote automatique : plus court chemin vers la graine la plus proche) et 4 creatures
    p = {'c': 10, 'r': 11, 'x': 0.0, 'y': 0.0, 'dx': -1, 'dy': 0}
    en = [{'c': 10, 'r': 7, 'dx': 0, 'dy': 0, 'wait': 0}, {'c': 9, 'r': 9, 'dx': 0, 'dy': 0, 'wait': 60},
          {'c': 10, 'r': 9, 'dx': 0, 'dy': 0, 'wait': 140}, {'c': 11, 'r': 9, 'dx': 0, 'dy': 0, 'wait': 220}]
    for m in [p] + en:
        m['x'], m['y'] = OX + m['c'] * T + T / 2, OY + m['r'] * T + T / 2
    fright, score, frames, booms = 0, 0, [], []

    def bfs_step():
        from collections import deque
        q, seen = deque([(p['c'], p['r'])]), {(p['c'], p['r']): None}
        while q:
            c, r = q.popleft()
            if (r, c) in dots and (c, r) != (p['c'], p['r']):
                while seen[(c, r)] and seen[(c, r)][0] != (p['c'], p['r']):
                    c, r = seen[(c, r)][0]
                return seen[(c, r)][1] if seen[(c, r)] else (0, 0)
            for dx, dy in DIRS:
                nc, nr = (c + dx) % C, r + dy
                if opened(c + dx, nr, True) and (nc, nr) not in seen:
                    if any(abs(e['c'] - nc) + abs(e['r'] - nr) <= 1 for e in en if not fright and not e['wait']):
                        continue
                    seen[(nc, nr)] = ((c, r), (dx, dy))
                    q.append((nc, nr))
        return (0, 0)

    def move(m, sp, chooser):
        while sp > 0.001:
            tx, ty = OX + m['c'] * T + T / 2, OY + m['r'] * T + T / 2
            d = abs(tx - m['x']) + abs(ty - m['y'])
            if d < 0.01:
                m['x'], m['y'] = tx, ty
                dx, dy = chooser(m)
                if not dx and not dy:
                    return
                m['dx'], m['dy'] = dx, dy
                nc = m['c'] + dx
                if nc < 0:
                    nc, m['x'] = C - 1, m['x'] + C * T
                if nc >= C:
                    nc, m['x'] = 0, m['x'] - C * T
                m['c'], m['r'] = nc, m['r'] + dy
                continue
            st = min(sp, d)
            m['x'] += st if tx > m['x'] else -st if tx < m['x'] else 0
            m['y'] += st if ty > m['y'] else -st if ty < m['y'] else 0
            sp -= st

    def choose_player(m):
        nonlocal score, fright
        k = (m['r'], m['c'])
        if k in dots:
            if dots.pop(k) == 2:
                fright = 220
                score += 50
            else:
                score += 10
        return bfs_step()

    def chooser_enemy(i):
        def ch(m):
            opts = [(dx, dy) for dx, dy in DIRS if (dx, dy) != (-m['dx'], -m['dy']) and opened(m['c'] + dx, m['r'] + dy, False)
                    and not (m['r'] == 7 and m['c'] == 10 and dy == 1)]
            if not opts:
                return (-m['dx'], -m['dy'])
            if fright or rnd.random() < 0.2:
                return rnd.choice(opts)
            tc, tr = p['c'] + p['dx'] * (4 if i == 1 else 0), p['r'] + p['dy'] * (4 if i == 1 else 0)
            return min(opts, key=lambda o: (m['c'] + o[0] - tc) ** 2 + (m['r'] + o[1] - tr) ** 2)
        return ch
    for f in range(480):
        move(p, 0.95, choose_player)
        if fright:
            fright -= 1
        for i, e in enumerate(en):
            if e['wait']:
                e['wait'] -= 1
                if not e['wait']:
                    e['c'], e['r'] = 10, 7
                    e['x'], e['y'] = OX + 10 * T + T / 2, OY + 7 * T + T / 2
                continue
            move(e, 0.5 if fright else 0.82, chooser_enemy(i))
            if abs(e['x'] - p['x']) + abs(e['y'] - p['y']) < T * 0.8:
                if fright:
                    score += 200
                    booms.append([int(e['x']), int(e['y']), 1])
                    e.update(c=10, r=9, dx=0, dy=0, wait=120)
                    e['x'], e['y'] = OX + 10 * T + T / 2, OY + 9 * T + T / 2
                else:  # dans l'apercu : le scarabee repart du depart
                    p.update(c=10, r=11, dx=-1, dy=0)
                    p['x'], p['y'] = OX + 10 * T + T / 2, OY + 11 * T + T / 2
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        HS = 64  # une dalle 128x64 : le labyrinthe defile pour suivre le scarabee (comme lDraw)
        cam = max(OY - 8, min(OY + R * T - HS, int(p['y']) - 8 - (HS - 8) // 2))
        s = Screen(W, HS)
        d = ImageDraw.Draw(s.im)
        NEON, INNER = (40, 120, 255), (6, 17, 36)

        def flo(r, c):
            return 0 <= r < R and 0 <= c < C and maze[r][c] != '#'

        def px8(x, y, col):
            if y >= 8:
                s.px(x, y, col)

        def rect8(x0, y0, w, h, col):
            y1 = y0 + h - 1
            y0 = max(8, y0)
            if w > 0 and y1 >= y0:
                d.rectangle((x0, y0, x0 + w - 1, y1), fill=col)
        for r in range(R):
            for c in range(C):
                x, y = OX + c * T, OY + r * T - cam
                if y + T <= 8 or y >= HS:
                    continue
                if maze[r][c] == '#':
                    rect8(x, y, T, T, INNER)
                    up, dn, lf, rt = flo(r - 1, c), flo(r + 1, c), flo(r, c - 1), flo(r, c + 1)
                    if up: rect8(x + lf, y, T - lf - rt, 1, NEON)
                    if dn: rect8(x + lf, y + T - 1, T - lf - rt, 1, NEON)
                    if lf: rect8(x, y + up, 1, T - up - dn, NEON)
                    if rt: rect8(x + T - 1, y + up, 1, T - up - dn, NEON)
                    if not up and not lf and flo(r - 1, c - 1): px8(x, y, NEON)
                    if not up and not rt and flo(r - 1, c + 1): px8(x + T - 1, y, NEON)
                    if not dn and not lf and flo(r + 1, c - 1): px8(x, y + T - 1, NEON)
                    if not dn and not rt and flo(r + 1, c + 1): px8(x + T - 1, y + T - 1, NEON)
                elif dots.get((r, c)) == 1:
                    rect8(x + 2, y + 2, 2, 2, (255, 200, 150))
                elif dots.get((r, c)) == 2:
                    gc = PAL['w'] if f & 8 else PAL['y']
                    rect8(x + 2, y + 1, 2, 4, gc); rect8(x + 1, y + 2, 4, 2, gc)
        rect8(OX + 10 * T, OY + 8 * T + 3 - cam, T, 1, PAL['o'])
        SPK = ((0x15, 0x0E, 0x1F, 0x0E, 0x15), (0x0A, 0x1F, 0x0E, 0x1F, 0x0A))
        cols = ((170, 70, 255), (255, 140, 0), (170, 230, 40), (230, 230, 230))
        for i, e in enumerate(en):
            fr_ = fright and not e['wait']
            col = ((40, 90, 255) if not (fright < 60 and f & 4) else PAL['w']) if fr_ else cols[i]
            core = (20, 40, 140) if fr_ else tuple(v // 2 for v in cols[i])
            x, y = int(e['x']) - 2, int(e['y']) - 2 - cam
            sp = SPK[(f // 6 + i) & 1]
            for rr in range(5):
                for cc in range(5):
                    if sp[rr] & (0x10 >> cc):
                        px8(x + cc, y + rr, core if 1 <= rr <= 3 and 1 <= cc <= 3 else col)
            px8(x + 2, y + 2, col)
            if fr_:
                px8(x + 1, y + 1, PAL['w']); px8(x + 3, y + 1, PAL['w'])
            else:
                ex, ey = max(-1, min(1, e['dx'])), max(-1, min(1, e['dy']))
                px8(x + 1 + (ex > 0), y + 1 + (ey > 0), PAL['w']); px8(x + 3 - (ex < 0), y + 1 + (ey > 0), PAL['w'])
        x, y = int(p['x']) - 2, int(p['y']) - 2 - cam  # scarabee : carapace, reflet, tete, pattes
        g, hi, dk, head = (40, 210, 70), (170, 255, 170), (10, 90, 30), (20, 60, 25)
        rect8(x + 1, y, 3, 5, g); rect8(x, y + 1, 5, 3, g); rect8(x + 2, y + 1, 1, 3, dk); px8(x + 1, y + 1, hi)
        dx_, dy_ = (p['dx'], p['dy']) if (p['dx'] or p['dy']) else (0, -1)
        hx, hy = x + 2 + dx_ * 2, y + 2 + dy_ * 2
        px8(hx, hy, head)
        if dx_:
            px8(hx, hy - 1, PAL['w']); px8(hx, hy + 1, PAL['w'])
        else:
            px8(hx - 1, hy, PAL['w']); px8(hx + 1, hy, PAL['w'])
        legs = ((x - 1, y + 1), (x + 5, y + 3), (x - 1, y + 4), (x + 5, y)) if (f // 4) & 1 else ((x - 1, y + 3), (x + 5, y + 1), (x - 1, y), (x + 5, y + 4))
        for lx, ly in legs:
            px8(lx, ly, dk)
        for b in booms:
            boom(s, [b[0], b[1] - cam, b[2]])
        hud(s, score, 0)
        life_icons(s, ['p.p.p', '.pwp.', 'ppppp', 'p.p.p'], 3, 'pog')  # vies : creatures de couleurs
        frames.append(page(led64(s.im), 'LABYRINTHE', 'Une dalle 128x64 : le labyrinthe defile pour suivre le scarabee.',
                           ['Commandes :', 'croix / stick : diriger', '', 'Regles :', 'graine : 10 points',
                            'grosse graine : les', 'creatures deviennent', 'bleues et fuient', 'en attraper une :',
                            '200, 400, 800, 1600', 'couloir du milieu ouvert', 'tout mange : stage +1 vie']))
    save(frames, 'jeu-labyrinthe.gif')




# ================================================================ SAUTERELLE
def sauterelle():
    from collections import deque
    rnd = random.Random(5)
    maze = re.findall(r'"([# .oE-]{31})"', re.search(r'J_MAZE\[J_ROWS\] = \{(.*?)\};', SRC, re.S).group(1))
    R, C, T, JUMP = len(maze), 31, 8, 26
    VH = H - 8
    dots = {(r, c): (1 if maze[r][c] == '.' else 2) for r in range(R) for c in range(C) if maze[r][c] in '.o'}

    def opened(c, r, player):
        if r < 0 or r >= R:
            return False
        if c < 0 or c >= C:
            return r == 11
        if maze[r][c] == '#':
            return False
        if player and ((r == 10 and c == 15) or (r == 11 and 14 <= c <= 16)):
            return False
        return True

    def wall(r, c):
        return 0 <= r < R and 0 <= c < C and maze[r][c] == '#'

    def height(t):
        return math.sin(math.pi * (JUMP - t) / JUMP) * (T + 1) if t > 0 else 0
    DIRS = ((0, -1), (-1, 0), (0, 1), (1, 0))
    p = {'c': 15, 'r': 15, 'dx': -1, 'dy': 0, 'j': 0}
    en = [{'c': 15, 'r': 9, 'dx': 0, 'dy': 0, 'wait': 0, 'j': 0}, {'c': 14, 'r': 11, 'dx': 0, 'dy': 0, 'wait': 60, 'j': 0},
          {'c': 15, 'r': 11, 'dx': 0, 'dy': 0, 'wait': 140, 'j': 0}, {'c': 16, 'r': 11, 'dx': 0, 'dy': 0, 'wait': 220, 'j': 0}]
    for m in [p] + en:
        m['x'], m['y'] = m['c'] * T + T / 2, m['r'] * T + T / 2
    fright, score, frames, booms = 0, 0, [], []

    def bfs_step():
        q, seen = deque([(p['c'], p['r'])]), {(p['c'], p['r']): None}
        while q:
            c, r = q.popleft()
            if (r, c) in dots and (c, r) != (p['c'], p['r']):
                while seen[(c, r)] and seen[(c, r)][0] != (p['c'], p['r']):
                    c, r = seen[(c, r)][0]
                return seen[(c, r)][1] if seen[(c, r)] else (0, 0)
            for dx, dy in DIRS:
                nc, nr = (c + dx) % C, r + dy
                if opened(c + dx, nr, True) and (nc, nr) not in seen:
                    seen[(nc, nr)] = ((c, r), (dx, dy))
                    q.append((nc, nr))
        return (0, 0)

    def move(m, sp, chooser):
        while sp > 0.001:
            tx, ty = m['c'] * T + T / 2, m['r'] * T + T / 2
            d = abs(tx - m['x']) + abs(ty - m['y'])
            if d < 0.01:
                m['x'], m['y'] = tx, ty
                dx, dy = chooser(m)
                if not dx and not dy:
                    return
                m['dx'], m['dy'] = dx, dy
                nc = m['c'] + dx
                if nc < 0:
                    nc, m['x'] = C - 1, m['x'] + C * T
                if nc >= C:
                    nc, m['x'] = 0, m['x'] - C * T
                m['c'], m['r'] = nc, m['r'] + dy
                continue
            st = min(sp, d)
            m['x'] += st if tx > m['x'] else -st if tx < m['x'] else 0
            m['y'] += st if ty > m['y'] else -st if ty < m['y'] else 0
            sp -= st

    def choose_player(m):
        nonlocal score, fright
        k = (m['r'], m['c'])
        if k in dots and height(p['j']) <= 2:
            if dots.pop(k) == 2:
                fright = 220
                score += 50
            else:
                score += 10
        return bfs_step()

    def chooser_enemy(i):
        def ch(m):
            opts = [(dx, dy) for dx, dy in DIRS if (dx, dy) != (-m['dx'], -m['dy']) and opened(m['c'] + dx, m['r'] + dy, False)
                    and not (m['r'] == 9 and m['c'] == 15 and dy == 1)]
            if not opts:
                return (-m['dx'], -m['dy'])
            if fright or rnd.random() < 0.2:
                return rnd.choice(opts)
            tc, tr = p['c'] + p['dx'] * (4 if i == 1 else 0), p['r'] + p['dy'] * (4 if i == 1 else 0)
            return min(opts, key=lambda o: (m['c'] + o[0] - tc) ** 2 + (m['r'] + o[1] - tr) ** 2)
        return ch
    WALLC = (60, 90, 255)
    top = tuple(v * 3 // 5 for v in WALLC)
    front = tuple(v // 3 for v in WALLC)
    base = tuple(v // 6 for v in WALLC)
    side = tuple(v * 2 // 5 for v in WALLC)
    shadow = (50, 50, 60)
    for f in range(560):
        near = any(abs(e['x'] - p['x']) + abs(e['y'] - p['y']) < T * 1.6 for e in en if not fright and not e['wait'])
        if near and p['j'] == 0:  # saut par-dessus la creature ; la violette saute parfois aussi
            p['j'] = JUMP
            if en[0]['j'] == 0 and not fright and abs(en[0]['c'] - p['c']) + abs(en[0]['r'] - p['r']) <= 4 and rnd.random() < 0.3:
                en[0]['j'] = JUMP
        for m in [p] + en:
            if m['j']:
                m['j'] -= 1
        move(p, 0.95 * T / 6, choose_player)
        if fright:
            fright -= 1
        for i, e in enumerate(en):
            if e['wait']:
                e['wait'] -= 1
                if not e['wait']:
                    e['c'], e['r'] = 15, 9
                    e['x'], e['y'] = 15 * T + T / 2, 9 * T + T / 2
                continue
            move(e, (0.5 if fright else 0.8) * T / 6, chooser_enemy(i))
            if (height(p['j']) > 2) != (height(e['j']) > 2):
                continue
            if abs(e['x'] - p['x']) + abs(e['y'] - p['y']) < T * 0.8:
                if fright:
                    score += 200
                    booms.append([e['x'], e['y'], 1])
                    e.update(c=15, r=11, dx=0, dy=0, wait=120)
                    e['x'], e['y'] = 15 * T + T / 2, 11 * T + T / 2
                else:  # dans l'apercu : la sauterelle repart du depart
                    p.update(c=15, r=15, dx=-1, dy=0, j=0)
                    p['x'], p['y'] = 15 * T + T / 2, 15 * T + T / 2
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        camx = max(0, min(C * T - W, int(p['x']) - W // 2))
        camy = max(0, min(R * T - VH, int(p['y']) - VH // 2))
        s = Screen(W, H)
        d = ImageDraw.Draw(s.im)

        def rect(x, y, w, h, col):
            x0, y0, x1, y1 = max(0, x), max(8, y), min(W, x + w), min(H, y + h)
            if x1 > x0 and y1 > y0:
                d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=col)

        def px(x, y, col):
            if 0 <= x < W and 8 <= y < H:
                s.px(x, y, col)
        for r in range(R):
            for c in range(C):
                x, y = c * T - camx, 8 + r * T - camy
                if x <= -T or x >= W or y <= 8 - T or y >= H:
                    continue
                if maze[r][c] == '#':
                    rect(x, y, T, T, top)
                    if not wall(r, c + 1):
                        rect(x + T - 1, y, 1, T, side)
                    if not wall(r - 1, c):
                        rect(x, y, T, 1, WALLC)
                    if not wall(r, c - 1):
                        rect(x, y, 1, T, WALLC)
                    if not wall(r + 1, c):
                        rect(x, y + T - T // 2, T, T // 2, front)
                        rect(x, y + T - T // 2, T, 1, WALLC)
                        rect(x, y + T - 1, T, 1, base)
                elif dots.get((r, c)) == 1:
                    rect(x + T // 2 - 1, y + T // 2 - 1, 2, 2, (230, 200, 140))
                elif dots.get((r, c)) == 2 and f & 8:
                    rect(x + T // 2 - 2, y + T // 2 - 2, 4, 4, PAL['w'])
        rect(15 * T - camx, 8 + 10 * T + T // 2 - camy, T, 1, PAL['o'])
        spike = (0x15, 0x0E, 0x1F, 0x0E, 0x15)
        cols = ((170, 70, 255), (255, 140, 0), (170, 230, 40), (230, 230, 230))
        for i, e in enumerate(en):
            col = ((40, 90, 255) if not (fright < 60 and f & 4) else PAL['w']) if fright and not e['wait'] else cols[i]
            h = int(height(e['j']))
            x, y = int(e['x']) - camx - 2, int(e['y']) + 8 - camy - 2
            if h:
                for k in range(5):
                    px(x + k, y + 4, shadow)
            y -= h
            for rr in range(5):
                for cc in range(5):
                    if spike[rr] & (0x10 >> cc):
                        px(x + cc, y + rr, col)
            if not fright or e['wait']:
                px(x + 1, y + 2, PAL['w']); px(x + 3, y + 2, PAL['w'])
        h = int(height(p['j']))  # sauterelle
        cx, cy = int(p['x']) - camx, int(p['y']) + 8 - camy
        if h:
            for k in range(-2, 3):
                px(cx + k, cy + 2, shadow)
        cy -= h
        g, dk, eye = (60, 220, 60), (20, 110, 30), (255, 230, 0)
        dx, dy = p['dx'], p['dy']
        if not dx and not dy:
            dx = 1
        up = 1 if (h > 0 or (f // 4) & 1) else 0
        if dx:
            rect(cx - 2, cy - 1, 5, 2, g)
            px(cx + 2 * dx, cy - 1, eye)
            px(cx - 2 * dx, cy - 2, dk); px(cx - dx, cy - 3 + (1 - up), dk); px(cx, cy - 2, dk)
            px(cx + dx, cy + 1, dk); px(cx - dx, cy + 1 + up, dk)
            px(cx + 3 * dx, cy - 2, dk)
        else:
            rect(cx - 1, cy - 2, 2, 5, g)
            px(cx - 1, cy + 2 * dy, eye); px(cx, cy + 2 * dy, eye)
            px(cx - 2, cy - dy, dk); px(cx + 1, cy - dy, dk)
            px(cx - 2 - up, cy - 2 * dy, dk); px(cx + 1 + up, cy - 2 * dy, dk)
            px(cx, cy + 3 * dy, dk)
        for b in booms:
            boom(s, [int(b[0]) - camx, int(b[1]) + 8 - camy, b[2]])
        hud(s, score)
        frames.append(page(led(s.im), 'SAUTERELLE', 'Grand labyrinthe en relief qui defile ; on saute par-dessus les creatures.',
                           ['Commandes :', 'croix / stick : diriger', 'X ou rond : sauter', '', 'Regles :', 'graine : 10 points',
                            'en l\'air : on passe', 'au-dessus de tout', 'grosse graine : les', 'creatures fuient',
                            'stage 2+ : la violette', 'saute aussi !', 'tout mange : stage +1 vie']))
    save(frames, 'jeu-sauterelle.gif')




# ================================================================ ORBITE
def _orows(name, n):
    return re.findall(r'"([.wrybcgpov]{%d})"' % n, re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\};', SRC, re.S).group(1))


_o = _orows('O_SPR[3][2][7]', 7)
O_SPR = [[_o[(t * 2 + k) * 7:(t * 2 + k + 1) * 7] for k in range(2)] for t in range(3)]
_t = _orows('O_SAT[2][7]', 7)
O_SAT = [_t[:7], _t[7:]]


def orbite():
    rnd = random.Random(11)
    cx, cy, rx, ry = W / 2, 8 + (H - 8) / 2, W / 2 - 6, (H - 8) / 2 - 4
    TAU = 2 * math.pi

    def X(a, r): return cx + math.cos(a) * r * rx
    def Y(a, r): return cy + math.sin(a) * r * ry

    def adiff(a, b):
        d = (a - b) % TAU
        return d - TAU if d > math.pi else d
    stars = [{'a': rnd.random() * TAU, 'r': rnd.random()} for _ in range(40)]
    en = []
    for i in range(32):
        w, k = i // 8, i % 8
        en.append({'on': True, 'state': 0, 't': -(w * 110 + k * 9), 'a0': w * math.pi / 2 + 0.7, 'dir': -1 if w & 1 else 1,
                   'type': (w + 1) % 3, 'carrier': i == 13, 'r': 0, 'a': 0, 'sa': 0, 'shot': False, 'i': i})
    shots, buls, booms, frames = [], [], [], []
    pa, rot, cool, dive, score, power, cap, stage_t = math.pi / 2, 0, 0, 150, 0, 0, None, 60
    SPR = (((0x0E, 0x1F, 0x15, 0x1F, 0x0A), (0x0E, 0x1F, 0x15, 0x1F, 0x11)),
           ((0x0E, 0x11, 0x15, 0x11, 0x0E), (0x0E, 0x1B, 0x11, 0x1B, 0x0E)),
           ((0x11, 0x1B, 0x0E, 0x1B, 0x11), (0x0A, 0x1B, 0x0E, 0x1B, 0x0A)))
    EC = ((255, 60, 200), (0, 220, 255), (255, 200, 0))
    for f in range(620):
        warp = 4 if stage_t else 1
        for s in stars:
            s['r'] += warp * (0.004 + s['r'] * 0.03)
            if s['r'] > 1.25:
                s['r'], s['a'] = 0.02, rnd.random() * TAU
        if stage_t:
            stage_t -= 1
        else:
            # pilote automatique : vise l'ennemi le plus proche, evite les tirs
            ta, best = pa, -1
            for e in en:
                if e['on'] and e['state'] >= 1 and e['r'] > best:
                    best, ta = e['r'], e['a']
            for b in buls:
                if b['r'] > 0.55 and abs(adiff(b['a'], pa)) < 0.3:
                    ta = pa + (-0.9 if adiff(b['a'], pa) > 0 else 0.9)
            d = adiff(ta, pa)
            pa = (pa + max(-0.075, min(0.075, d))) % TAU
            cool = max(0, cool - 1)
            if cool == 0:
                for k in ((-0.07, 0.07) if power else (0,)):
                    if len(shots) < 8:
                        shots.append({'a': pa + k, 'r': 0.92})
                cool = 7
            for s in shots:
                s['r'] -= 0.02 + 0.035 * s['r']
            shots = [s for s in shots if s['r'] >= 0.06]
            rot += 0.006
            for e in en:
                if not e['on']:
                    continue
                e['t'] += 1
                if e['state'] == 0:
                    if e['t'] >= 0:
                        e['state'], e['t'] = 1, 0
                    continue
                fr = 0.44 + (e['i'] & 1) * 0.11
                if e['state'] == 1:
                    u = min(1, e['t'] / 100)
                    e['r'] = fr * u + 0.5 * math.sin(math.pi * u)
                    e['a'] = e['a0'] + e['dir'] * 4.4 * u
                    if u >= 1:
                        e['state'], e['sa'] = 2, e['a'] - rot
                elif e['state'] == 2:
                    e['a'] = e['sa'] + rot
                    e['r'] = fr + 0.04 * math.sin(f * 0.05 + e['sa'])
                elif e['state'] == 3:
                    e['r'] += 0.006 + 0.016 * e['r'] + 0.002
                    e['a'] += max(-0.025, min(0.025, adiff(pa, e['a'])))
                    if not e['shot'] and e['r'] > 0.45:
                        e['shot'] = True
                        if rnd.random() < 0.65:
                            buls.append({'a': e['a'], 'r': e['r']})
                    if e['r'] > 1.2:
                        e['state'], e['r'] = 4, 0
                elif e['state'] == 4:
                    e['a'] = e['sa'] + rot
                    e['r'] += 0.01
                    if e['r'] >= fr:
                        e['state'] = 2
                for s in shots:
                    if abs(s['r'] - e['r']) <= 0.07 and abs(adiff(s['a'], e['a'])) * max(e['r'], 0.15) < 0.075:
                        s['r'] = -1
                        e['on'] = False
                        booms.append([int(X(e['a'], e['r'])), int(Y(e['a'], e['r'])), 1])
                        score += (2 if e['state'] == 3 else 1) * (50, 80, 100)[e['type']]
                        if e['carrier']:
                            cap = {'a': e['a'], 'r': max(0.2, e['r'])}
                        break
                shots = [s for s in shots if s['r'] >= 0]
            dive -= 1
            if dive <= 0:
                dive = 80 + rnd.randrange(40)
                cand = [e for e in en if e['on'] and e['state'] == 2]
                if cand:
                    e = rnd.choice(cand)
                    e['state'], e['shot'] = 3, False
            for b in buls:
                b['r'] += 0.012 + 0.018 * b['r']
            buls = [b for b in buls if b['r'] <= 1.2]
            if cap:
                cap['r'] += 0.012
                if cap['r'] > 0.92 and abs(adiff(cap['a'], pa)) < 0.2:
                    cap, power, score = None, 1, score + 500
                elif cap['r'] > 1.15:
                    cap = None
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, H)
        for i, st in enumerate(stars):
            x, y = int(X(st['a'], st['r'])), int(Y(st['a'], st['r']))
            g = int(40 + min(1, st['r']) * 200)
            if y >= 8:
                s.px(x, y, (g // 2, g // 2, g) if i % 4 == 0 else (g, g, g))
        for e in en:
            if not e['on'] or e['state'] == 0:
                continue
            x, y = int(X(e['a'], e['r'])), int(Y(e['a'], e['r']))
            c = ((255, 255, 255) if f & 8 else (150, 150, 150)) if e['carrier'] else EC[e['type']]
            fr8 = (f // 8) & 1
            if e['r'] >= 0.55:  # de pres : sprite 7x7 en couleurs (comme oDraw)
                rows = O_SAT[fr8] if e['carrier'] else O_SPR[e['type']][fr8]
                for r_, line in enumerate(rows):
                    for c_, ch in enumerate(line):
                        if ch != '.':
                            s.px(x - 3 + c_, y - 3 + r_, PAL[ch])
                continue
            if e['r'] < 0.18:
                s.px(x, y, c)
            elif e['r'] < 0.36:
                for dx, dy in ((0, 0), (-1, 0), (1, 0), (0, -1), (0, 1)):
                    s.px(x + dx, y + dy, c)
            else:
                sp = SPR[e['type']][(f // 8) & 1]
                for r in range(5):
                    for cc in range(5):
                        if sp[r] & (0x10 >> cc):
                            s.px(x - 2 + cc, y - 2 + r, c)
        for sh in shots:
            s.px(int(X(sh['a'], sh['r'])), int(Y(sh['a'], sh['r'])), PAL['y'])
            s.px(int(X(sh['a'], sh['r'] + 0.04)), int(Y(sh['a'], sh['r'] + 0.04)), PAL['w'])
        for b in buls:
            x, y = int(X(b['a'], b['r'])), int(Y(b['a'], b['r']))
            s.px(x, y, PAL['r'])
            if b['r'] > 0.5:
                s.px(x + 1, y, PAL['r']); s.px(x, y + 1, PAL['w'])
        if cap:
            x, y = int(X(cap['a'], cap['r'])), int(Y(cap['a'], cap['r']))
            c = PAL['y'] if f & 4 else PAL['g']
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                s.px(x + dx, y + dy, c)
            s.px(x, y, PAL['w'])
        d = ImageDraw.Draw(s.im)
        px, py = X(pa, 1), Y(pa, 1)
        ux, uy = cx - px, cy - py
        n = math.hypot(ux, uy)
        ux, uy = ux / n, uy / n
        vx, vy = -uy, ux
        nose = (int(px + 3 * ux), int(py + 3 * uy))
        lw = (int(px - 2 * ux + 3 * vx), int(py - 2 * uy + 3 * vy))
        rw = (int(px - 2 * ux - 3 * vx), int(py - 2 * uy - 3 * vy))
        body = PAL['c'] if power else (60, 140, 255)
        d.line((nose, lw), fill=body); d.line((nose, rw), fill=body)
        d.line((lw, (int(px), int(py))), fill=body); d.line((rw, (int(px), int(py))), fill=body)
        s.px(int(px + ux), int(py + uy), PAL['w'])
        s.px(int(px - 3 * ux), int(py - 3 * uy), PAL['o'])
        for b in booms:
            boom(s, b)
        if stage_t:
            s.text(H // 2 - 22, 'STAGE 1', (255, 220, 0))
            s.text(H // 2 - 11, 'ORBITE', (255, 60, 200))
        hud(s, score)
        frames.append(page(led(s.im), 'ORBITE', "Le vaisseau tourne sur le bord et tire vers le centre, d'où sortent les ennemis.",
                           ['Commandes :', 'stick / croix : direction', '  (le vaisseau y glisse)', 'X : tirer', '',
                            'Regles :', 'ennemis en spirale,', 'puis en anneau ;', 'ils plongent et tirent', 'en pique : points x2',
                            'blanc : capsule', '  = double tir', 'tout detruit : stage +1']))
    save(frames, 'jeu-orbite.gif')




# ================================================================ RAQUETTES (horizontale, une dalle 128x64)
def raquettes():
    rnd = random.Random(6)
    HS = 64
    st = [[rnd.randrange(W), rnd.uniform(12, HS), rnd.uniform(.2, 1)] for _ in range(20)]
    top, ph = 9, 10
    lost = 0
    bx, by, vx, vy = W / 2, 36.0, -1.2, 0.7
    ppy = cpy = (top + HS) / 2 - ph / 2
    trail, booms, score, won, frames, serve = [], [], 0, 0, [], 20
    for f in range(360):
        c = ppy + ph / 2
        ty = by if vx < 0 else (top + HS) / 2
        ppy = max(top, min(HS - ph, ppy + max(-2.2, min(2.2, ty - c))))
        cc = cpy + ph / 2
        tgt = by if vx > 0 else (top + HS) / 2
        if abs(tgt - cc) > 2:
            cpy = max(top, min(HS - ph, cpy + max(-0.75, min(0.75, tgt - cc))))
        if serve:
            serve -= 1
        else:
            trail = ([(bx, by)] + trail)[:3]
            bx += vx; by += vy
            if by < top or by > HS - 2:
                vy = -vy
                by = max(top, min(HS - 2, by))
            for pady, right, xx in ((ppy, True, 3), (cpy, False, W - 5)):
                hit = (vx < 0 and right and xx - 1 <= bx <= xx + 2) or (vx > 0 and not right and xx <= bx + 2 <= xx + 3)
                if hit and by + 2 >= pady and by <= pady + ph:
                    rel = max(-1, min(1, (by + 1 - (pady + ph / 2)) / (ph / 2)))
                    sp = min(math.hypot(vx, vy) * 1.06, 3.6)
                    vy = math.sin(rel * 0.95) * sp
                    vx = (1 if right else -1) * max(0.7, math.cos(rel * 0.95)) * sp
                    bx = xx + 2 if right else xx - 2
            if bx > W:
                score += 100; won += 1
                booms.append([W - 3, int(by), 1])
                bx, by, vx, vy, serve = W / 2, 36.0, 1.2, rnd.choice((-0.6, 0.6)), 30
            elif bx < -2:
                lost += 1
                bx, by, vx, vy, serve = W / 2, 36.0, -1.2, rnd.choice((-0.6, 0.6)), 30
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, HS)
        for sx, sy, sv in st:
            g_ = int(70 + sv * 120)
            s.px(sx, int(sy), (g_, g_, g_))
        d = ImageDraw.Draw(s.im)
        for y in range(top, HS, 6):
            d.line((W // 2, y, W // 2, y + 2), fill=(50, 50, 80))
        py_, cy_ = int(ppy), int(cpy)
        for x_, y_, left, (r_, g_, b_), fl in ((3, py_, True, (0, 220, 255), bx < 12 and vx > 0), (W - 6, cy_, False, (255, 50, 50), bx > W - 14 and vx < 0)):
            dark, main = (r_ // 4, g_ // 4, b_ // 4), (r_, g_, b_)
            lite = (min(255, r_ // 2 + 140), min(255, g_ // 2 + 140), min(255, b_ // 2 + 140))
            if fl:
                main = lite = (255, 255, 255)
            xo, xi = (x_, x_ + 2) if left else (x_ + 2, x_)
            d.line((xo, y_ + 2, xo, y_ + ph - 3), fill=dark)
            d.line((x_ + 1, y_ + 1, x_ + 1, y_ + ph - 2), fill=main)
            d.line((xi, y_ + 1, xi, y_ + ph - 2), fill=lite)
            s.px(x_ + 1, y_, lite); s.px(x_ + 1, y_ + ph - 1, dark)
            d.line((x_ + 1, y_ + ph // 2 - 1, x_ + 1, y_ + ph // 2), fill=main if fl else dark)
        for i, (tx_, ty_) in enumerate(reversed(trail)):
            v = 40 + 30 * i
            d.rectangle((int(tx_), int(ty_), int(tx_) + 1, int(ty_) + 1), fill=(v, v, v))
        if not serve or f & 4:
            d.rectangle((int(bx), int(by), int(bx) + 1, int(by) + 1), fill=PAL['w'])
        s.tiny(W // 2 - 6, top + 1, str(won % 5), PAL['c'])
        s.tiny(W // 2 + 3, top + 1, str(lost % 3), PAL['r'])
        for b in booms:
            boom(s, b)
        hud(s, score, 0)  # pas de vies affichees
        frames.append(page(led64(s.im), 'RAQUETTES', "A l'horizontale : ta raquette a gauche, l'ordinateur a droite.",
                           ['Commandes :', 'haut / bas : bouger', 'stick : vitesse selon', "  l'inclinaison", '', 'Regles :',
                            "angle selon le point", "d'impact sur la raquette", 'point gagne : 100 x stage', 'point perdu : -1 vie',
                            '5 points : stage suivant']))
    save(frames, 'jeu-raquettes.gif')


# ================================================================ BRIQUES (une dalle 128x64)
def briques():
    rnd = random.Random(4)
    HS = 64
    st = [[rnd.randrange(W), rnd.uniform(12, HS), rnd.uniform(.2, 1)] for _ in range(18)]
    COL = [None, (255, 50, 50), (255, 150, 0), (255, 220, 0), (40, 220, 80), (0, 200, 255), (200, 70, 255), (170, 170, 185), (230, 180, 40)]
    cols, rows, ox, oy = 12, 6, 5, 11
    bricks = {}
    for r in range(rows):
        for c in range(cols):
            bricks[(r, c)] = [7 if r == 0 else 1 + r % 6, 2 if r == 0 else 1]
    pw, py = 18, HS - 4
    px = W / 2 - 9
    balls = [[px + 8, py - 2.0, 0.6, -1.3]]
    caps, booms, score, frames, stuck, wide = [], [], 0, [], 30, 0
    for f in range(420):
        tx = W / 2
        low = [b for b in balls if b[3] > 0]
        if low:
            tx = max(low, key=lambda b: b[1])[0] + 1
        elif balls:
            tx = balls[0][0] + 1
        c_ = px + pw / 2 + 3 * math.sin(f * 0.05)
        px += -2.6 if tx < c_ - 2 else 2.6 if tx > c_ + 2 else 0
        pw = 28 if wide else 18
        wide = max(0, wide - 1)
        px = max(0, min(W - pw, px))
        if stuck:
            stuck -= 1
            balls[0][0], balls[0][1] = px + pw / 2 - 1, py - 2
        else:
            for b in balls:
                n = int(math.ceil(max(abs(b[2]), abs(b[3]))))
                for _ in range(n):
                    b[0] += b[2] / n
                    if b[0] < 0 or b[0] > W - 2:
                        b[2] = -b[2]; b[0] = max(0, min(W - 2, b[0]))
                    for axis in (0, 1):
                        if axis:
                            b[1] += b[3] / n
                            if b[1] < 8:
                                b[1], b[3] = 8, abs(b[3])
                        cc_, rr_ = int((b[0] + 1 - ox) // 10), int((b[1] + 1 - oy) // 4)
                        k = (rr_, cc_)
                        if k in bricks:
                            if axis:
                                b[1] -= b[3] / n; b[3] = -b[3]
                            else:
                                b[0] -= b[2] / n; b[2] = -b[2]
                            bricks[k][1] -= 1
                            if bricks[k][1] <= 0:
                                score += 50 if bricks[k][0] == 7 else 10 * (rows - rr_) + 10
                                booms.append([ox + cc_ * 10 + 4, oy + rr_ * 4 + 1, 1])
                                if rnd.random() < 0.15:
                                    caps.append([ox + cc_ * 10 + 2, oy + rr_ * 4, rnd.randrange(3)])
                                del bricks[k]
                    if b[3] > 0 and py <= b[1] + 2 <= py + 2 and px - 2 <= b[0] <= px + pw:
                        rel = max(-1, min(1, (b[0] + 1 - (px + pw / 2)) / (pw / 2)))
                        v = math.hypot(b[2], b[3])
                        b[2], b[3], b[1] = math.sin(rel * 1.05) * v, -max(0.5, math.cos(rel * 1.05)) * v, py - 2
            balls = [b for b in balls if b[1] <= HS]
            if not balls:
                balls, stuck = [[px + 8, py - 2.0, 0.6, -1.3]], 30
        for cp in caps:
            cp[1] += 0.6
            if py <= cp[1] + 3 <= py + 3 and px <= cp[0] + 5 and cp[0] <= px + pw:
                cp[1] = 99
                if cp[2] == 0:
                    wide = 400
                elif cp[2] == 2 and balls:
                    b0 = balls[0]
                    balls += [[b0[0], b0[1], b0[2] * 0.6 + 0.8, b0[3]], [b0[0], b0[1], -b0[2], b0[3]]]
        caps = [c for c in caps if c[1] < HS]
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, HS)
        for sx, sy, sv in st:
            g_ = int(60 + sv * 100)
            s.px(sx, int(sy), (g_, g_, g_))
        d = ImageDraw.Draw(s.im)
        for (r, c), (t, hp) in bricks.items():  # briques en relief (comme le firmware V3.93)
            x, y = ox + c * 10, oy + r * 4
            cc = COL[t]
            lv = 7 if (t == 7 and hp < 2) else 9

            def tone(k, add):
                return tuple(min(255, v * k * lv // 90 + add) for v in cc)
            sh_ = (6, 6, 12)
            d.line((x + 1, y + 3, x + 9, y + 3), fill=sh_)
            d.line((x + 9, y + 1, x + 9, y + 2), fill=sh_)
            d.line((x, y, x + 7, y), fill=tone(12, 50))
            s.px(x + 8, y, tone(8, 0))
            s.px(x, y + 1, tone(10, 25))
            d.line((x + 1, y + 1, x + 7, y + 1), fill=tone(8, 0))
            s.px(x + 8, y + 1, tone(5, 0))
            s.px(x, y + 2, tone(5, 0))
            d.line((x + 1, y + 2, x + 8, y + 2), fill=tone(3, 0))
            s.px(x + 2, y + 1, tone(6, 150))
            s.px(x + 3, y + 1, tone(8, 70))
            if t >= 7:
                sp_ = (f // 2 + r * 3 + c * 5) % 48
                for k in range(3):
                    if x <= x + sp_ - k < x + 9:
                        s.px(x + sp_ - k, y + k, (255, 255, 255) if k == 1 else tone(8, 120))
            if lv == 7:
                s.px(x + 5, y, tone(3, 0)); s.px(x + 4, y + 1, sh_); s.px(x + 5, y + 2, sh_)
        ip, iw = int(px), int(pw)
        d.line((ip + 1, py, ip + iw - 2, py), fill=(225, 230, 245))
        d.line((ip, py + 1, ip + iw - 1, py + 1), fill=(140, 150, 175))
        d.line((ip + 1, py + 2, ip + iw - 2, py + 2), fill=(60, 66, 90))
        for ex in (ip, ip + iw - 4):
            d.line((ex + 1, py, ex + 3, py), fill=(255, 130, 130))
            d.line((ex, py + 1, ex + 3, py + 1), fill=(230, 40, 40))
            d.line((ex + 1, py + 2, ex + 3, py + 2), fill=(120, 20, 20))
        d.line((ip + iw // 2 - 3, py + 1, ip + iw // 2 + 2, py + 1), fill=PAL['c'] if f & 8 else (120, 255, 255))
        for b in balls:
            d.rectangle((int(b[0]), int(b[1]), int(b[0]) + 1, int(b[1]) + 1), fill=PAL['w'])
        CC = ((0, 220, 255), (255, 140, 0), (220, 60, 220))
        for cp in caps:
            x, y = int(cp[0]), int(cp[1])
            d.rectangle((x + 1, y, x + 4, y + 2), fill=CC[cp[2]]); s.px(x, y + 1, CC[cp[2]]); s.px(x + 5, y + 1, CC[cp[2]])
            if f & 8:
                s.px(x + 2, y + 1, PAL['w'])
        for b in booms:
            boom(s, b)
        hud(s, score, 0)
        life_icons(s, ['.ww..', 'wwww.', 'wwww.', '.ww..'])  # vies : balles blanches
        frames.append(page(led64(s.im), 'CASSE BRIQUES', 'Casse-briques : 6 tableaux qui reviennent en boucle, balle plus rapide.',
                           ['Commandes :', 'gauche / droite :', '  raquette', 'X : lancer la balle', '', 'Regles :',
                            'argent : 2 ou 3 coups', 'or : incassable', 'capsules : cyan large,', '  orange lente,',
                            '  magenta 3 balles,', '  rouge une vie']))
    save(frames, 'jeu-casse-briques.gif')


# ================================================================ BLOCS (une dalle 128x64)
def blocs():
    rnd = random.Random(9)
    HS = 64
    SH = [(0x0F00, 0x2222, 0x00F0, 0x4444), (0x6600,) * 4, (0x4E00, 0x4640, 0x0E40, 0x4C40), (0x6C00, 0x4620, 0x06C0, 0x8C40),
          (0xC600, 0x2640, 0x0C60, 0x4C80), (0x8E00, 0x6440, 0x0E20, 0x44C0), (0x2E00, 0x4460, 0x0E80, 0xC440)]
    COL = [None, (0, 220, 255), (255, 220, 0), (200, 70, 255), (40, 220, 80), (255, 50, 50), (50, 100, 255), (255, 140, 0)]
    NC, NR, S = 10, 18, 3
    BX, BY = (W - NC * S) // 2, 8 + ((HS - 8) - NR * S) // 2
    B = [[0] * NC for _ in range(NR)]

    def cell(p, rot, r, c):
        return SH[p][rot & 3] & (0x8000 >> (r * 4 + c))

    def fits(p, rot, x, y):
        for r in range(4):
            for c in range(4):
                if cell(p, rot, r, c):
                    bx, by = x + c, y + r
                    if bx < 0 or bx >= NC or by >= NR or (by >= 0 and B[by][bx]):
                        return False
        return True

    def drop(p, rot, x, y):
        while fits(p, rot, x, y + 1):
            y += 1
        return y

    def plan(p):
        best, br, bx_ = -1e9, 0, 3
        for rot in range(4):
            for x in range(-2, NC):
                if not fits(p, rot, x, 0):
                    continue
                y = drop(p, rot, x, 0)
                tmp = [row[:] for row in B]
                for r in range(4):
                    for c in range(4):
                        if cell(p, rot, r, c) and y + r >= 0:
                            tmp[y + r][x + c] = 1
                lines = sum(all(row) for row in tmp)
                holes = agg = bump = 0
                prev = None
                for c in range(NC):
                    h, seen = 0, False
                    for r in range(NR):
                        if tmp[r][c]:
                            if not seen:
                                h, seen = NR - r, True
                        elif seen:
                            holes += 1
                    agg += h
                    if prev is not None:
                        bump += abs(h - prev)
                    prev = h
                sc = -0.51 * agg + 2.28 * lines - 0.72 * holes - 0.18 * bump
                if sc > best:
                    best, br, bx_ = sc, rot, x
        return br, bx_
    bag = []

    def nxt():
        if not bag:
            bag.extend(rnd.sample(range(7), 7))
        return bag.pop()
    piece, nextp = nxt(), nxt()
    rot, px, py, fall, tgt, aiT, flash = 0, 3, -1, 0, plan(piece), 0, 0
    lines_, score, frames = 0, 0, []
    for _ in range(6):  # un peu de pile au depart
        B[NR - 1 - _ // 3][rnd.randrange(NC)] = rnd.randrange(1, 8)
    for f in range(520):
        if flash:
            flash -= 1
            if not flash:
                keep = [row for row in B if not all(v == 8 for v in row)]
                B[:] = [[0] * NC for _ in range(NR - len(keep))] + keep
                piece, nextp = nextp, nxt()
                rot, px, py, tgt = 0, 3, -1, plan(piece)
        else:
            aiT += 1
            if aiT % 3 == 0:
                if rot != tgt[0] and fits(piece, rot + 1, px, py):
                    rot = (rot + 1) & 3
                elif px < tgt[1] and fits(piece, rot, px + 1, py):
                    px += 1
                elif px > tgt[1] and fits(piece, rot, px - 1, py):
                    px -= 1
            fast = rot == tgt[0] and px == tgt[1]
            fall += 1
            if fall >= (2 if fast else 12):
                fall = 0
                if fits(piece, rot, px, py + 1):
                    py += 1
                else:
                    for r in range(4):
                        for c in range(4):
                            if cell(piece, rot, r, c) and py + r >= 0:
                                B[py + r][px + c] = piece + 1
                    full = [r for r in range(NR) if all(B[r])]
                    if full:
                        for r in full:
                            B[r] = [8] * NC
                        lines_ += len(full)
                        score += (0, 100, 300, 500, 800)[len(full)]
                        flash = 14
                    else:
                        piece, nextp = nextp, nxt()
                        rot, px, py, tgt = 0, 3, -1, plan(piece)
                        if not fits(piece, rot, px, py):
                            B[:] = [[0] * NC for _ in range(NR)]
        if f % 2:
            continue
        s = Screen(W, HS)
        d = ImageDraw.Draw(s.im)

        def cdraw(x, y, sz, v, ghost=False):
            if not v:
                return
            if v == 8:
                d.rectangle((x, y, x + sz - 1, y + sz - 1), fill=(255, 255, 255) if f & 2 else (120, 120, 120))
                return
            c = COL[v]
            if ghost:
                d.rectangle((x, y, x + sz - 1, y + sz - 1), outline=tuple(k // 3 for k in c))
                return
            d.rectangle((x, y, x + sz - 1, y + sz - 1), fill=tuple(k * 3 // 4 for k in c))
            d.line((x, y, x + sz - 1, y), fill=tuple(min(255, k + 90) for k in c))
            d.line((x, y, x, y + sz - 1), fill=tuple(min(255, k + 60) for k in c))
            if sz >= 4:
                d.line((x + 1, y + sz - 1, x + sz - 1, y + sz - 1), fill=tuple(k // 3 for k in c))
                d.line((x + sz - 1, y + 1, x + sz - 1, y + sz - 1), fill=tuple(k // 3 for k in c))
        d.rectangle((BX - 1, BY - 1, BX + NC * S, BY + NR * S), outline=(70, 80, 140))
        gy = drop(piece, rot, px, py) if not flash else -99
        for r in range(NR):
            for c in range(NC):
                v, gh = B[r][c], False
                if not flash:
                    pr, pc, qr = r - py, c - px, r - gy
                    if 0 <= pr < 4 and 0 <= pc < 4 and cell(piece, rot, pr, pc):
                        v = piece + 1
                    elif not v and 0 <= qr < 4 and 0 <= pc < 4 and cell(piece, rot, qr, pc):
                        v, gh = piece + 1, True
                cdraw(BX + c * S, BY + r * S, S, v, gh)
        lab, val = (110, 110, 140), (230, 230, 230)
        lx, ly = BX - 6 - 23, BY + 2
        ns['TF'].update({'L': [4, 4, 4, 4, 7], 'I': [7, 2, 2, 2, 7], 'V': [5, 5, 5, 5, 2]})
        s.tiny(lx, ly, 'LIGNES', lab); s.tiny(lx, ly + 7, str(lines_), val)
        s.tiny(lx, ly + 16, 'NIVEAU', lab); s.tiny(lx, ly + 23, str(1 + lines_ // 10), val)
        nx = BX + NC * S + 5
        s.tiny(nx, ly, 'APRES', lab)
        for r in range(4):
            for c in range(4):
                if cell(nextp, 0, r, c):
                    cdraw(nx + c * 2, ly + 8 + r * 2, 2, nextp + 1)
        hud(s, score, 0)  # pas de vies affichees
        frames.append(page(led64(s.im), 'BLOCS', 'Blocs qui tombent : complete des lignes, elles disparaissent.',
                           ['Commandes :', 'gauche / droite : bouger', 'bas : descendre vite', "haut : tomber d'un coup",
                            'X / rond : tourner', '', 'Regles :', '1 a 4 lignes : 100, 300,', '  500, 800 x niveau',
                            '10 lignes : niveau +1', 'pile en haut : -1 vie']))
    save(frames, 'jeu-blocs.gif')


# ================================================================ CRUCH (une dalle 128x64)
def cruch():
    rnd = random.Random(21)
    HS, N = 64, 8
    YS, BX, BY = 7, 2, 8
    COL = [(255, 50, 60), (255, 150, 0), (255, 225, 0), (40, 220, 80), (40, 140, 255), (200, 70, 255)]
    SPR = re.findall(r'"([.ahd]{6})"', re.search(r'Y_SPR\[6\]\[6\] = \{(.*?)\};', SRC, re.S).group(1))
    SPR = [SPR[k * 6:(k + 1) * 6] for k in range(6)]
    G = [[0] * N for _ in range(N)]
    off = [[0.0] * N for _ in range(N)]

    def run_at(r, c):
        t = G[r][c]
        if t is None:
            return False
        h = v = 1
        k = c - 1
        while k >= 0 and G[r][k] == t:
            h += 1; k -= 1
        k = c + 1
        while k < N and G[r][k] == t:
            h += 1; k += 1
        k = r - 1
        while k >= 0 and G[k][c] == t:
            v += 1; k -= 1
        k = r + 1
        while k < N and G[k][c] == t:
            v += 1; k += 1
        return h >= 3 or v >= 3

    for r in range(N):
        for c in range(N):
            while True:
                G[r][c] = rnd.randrange(6)
                if not run_at(r, c):
                    break
            off[r][c] = -(N - r) * YS - rnd.randrange(YS)

    def best_move():
        best, mv = 0, None
        for r in range(N):
            for c in range(N):
                for dr, dc in ((0, 1), (1, 0)):
                    r2, c2 = r + dr, c + dc
                    if r2 >= N or c2 >= N:
                        continue
                    G[r][c], G[r2][c2] = G[r2][c2], G[r][c]
                    n = sum(run_at(a, b) for a in range(N) for b in range(N)) * 10 + rnd.randrange(10)
                    G[r][c], G[r2][c2] = G[r2][c2], G[r][c]
                    if n > best:
                        best, mv = n, (r, c, dr, dc)
        return mv if best >= 30 else None
    state, t, cur, sel, mv, mark, combo = 4, 0, [3, 3], False, None, set(), 0
    score, moves, goal, frames = 0, 20, 1500, []
    swap = None
    for f in range(520):
        if state == 0:
            if mv is None:
                mv = best_move()
            if f % 6 == 0 and mv:
                r, c, dr, dc = mv
                if cur[0] != r:
                    cur[0] += 1 if cur[0] < r else -1
                elif cur[1] != c:
                    cur[1] += 1 if cur[1] < c else -1
                elif not sel:
                    sel = True
                else:
                    swap, sel, state, t = (r, c, r + dr, c + dc), False, 1, 0
                    cur = [r + dr, c + dc]
                    mv = None
        elif state == 1:
            t += 1
            if t >= 7:
                a, b, c2, d2 = swap
                G[a][b], G[c2][d2] = G[c2][d2], G[a][b]
                mark = {(r, c) for r in range(N) for c in range(N) if run_at(r, c)}
                moves -= 1
                combo = 1
                score += len(mark) * 60
                state, t = 3, 12
        elif state == 3:
            t -= 1
            if t <= 0:
                for r, c in mark:
                    G[r][c] = None
                for c in range(N):  # chute : chaque bonbon garde descend de sa place, les nouveaux arrivent d'en haut
                    kept = [(r, G[r][c]) for r in range(N) if G[r][c] is not None]
                    miss = N - len(kept)
                    for k, (orow, v) in enumerate(kept):
                        nr = miss + k
                        G[nr][c], off[nr][c] = v, (orow - nr) * YS
                    for r in range(miss):
                        G[r][c], off[r][c] = rnd.randrange(6), -(miss) * YS - 2 * (miss - r)
                state = 4
        elif state == 4:
            moving = False
            for r in range(N):
                for c in range(N):
                    if off[r][c] < 0:
                        off[r][c] = min(0.0, off[r][c] + 2.0)
                        moving = True
            if not moving:
                mark = {(r, c) for r in range(N) for c in range(N) if run_at(r, c)}
                if mark:
                    combo += 1
                    score += len(mark) * 60 * combo
                    state, t = 3, 12
                else:
                    state = 0
                    if moves <= 0:
                        moves = 20
        if f % 2:
            continue
        s = Screen(W, HS)
        d = ImageDraw.Draw(s.im)
        for r in range(N):
            for c in range(N):
                d.rectangle((BX + c * YS, BY + r * YS, BX + c * YS + YS - 1, BY + r * YS + YS - 1),
                            fill=(28, 18, 52) if (r + c) & 1 else (40, 26, 72))
        u = min(1, t / 7) if state == 1 else 0
        for r in range(N):
            for c in range(N):
                v = G[r][c]
                if v is None:
                    continue
                dx, dy = 0, off[r][c]
                if u and swap and (r, c) == swap[:2]:
                    dx, dy = (swap[3] - swap[1]) * YS * u, dy + (swap[2] - swap[0]) * YS * u
                if u and swap and (r, c) == swap[2:]:
                    dx, dy = (swap[1] - swap[3]) * YS * u, dy + (swap[0] - swap[2]) * YS * u
                x, y = BX + c * YS + int(dx), BY + r * YS + int(dy)
                if y < BY:
                    continue
                flash = state == 3 and (r, c) in mark and t & 2
                cc = COL[v]
                for rr in range(6):
                    for k in range(6):
                        ch = SPR[v][rr][k]
                        if ch != '.':
                            col = (255, 255, 255) if flash else (tuple(min(255, q + 120) for q in cc) if ch == 'h' else
                                                                  tuple(q // 3 for q in cc) if ch == 'd' else cc)
                            s.px(x + k, y + rr, col)
        if state == 0 or sel:
            cc = (PAL['y'] if f & 4 else PAL['o']) if sel else PAL['w']
            d.rectangle((BX + cur[1] * YS - 1, BY + cur[0] * YS - 1, BX + cur[1] * YS + YS, BY + cur[0] * YS + YS), outline=cc)
        px_ = BX + N * YS + 4
        lab, val = (110, 110, 140), (230, 230, 230)
        s.tiny(px_, BY + 1, 'COUPS', lab); s.tiny(px_, BY + 8, str(moves), val)
        s.tiny(px_, BY + 17, 'BUT', lab); s.tiny(px_, BY + 24, str(goal), val)
        gy, gh = BY + 33, 22
        d.rectangle((px_, gy, px_ + 9, gy + gh - 1), outline=(90, 90, 120))
        fill = min(gh - 2, int((gh - 2) * score / goal))
        if fill > 0:
            d.rectangle((px_ + 1, gy + gh - 1 - fill, px_ + 8, gy + gh - 2), fill=PAL['g'] if score >= goal else PAL['p'])
        hud(s, score, 0)
        life_icons(s, ['..g..', '.rrr.', 'rwrrr', '.rrr.'])  # vies : des pommes
        frames.append(page(led64(s.im), 'CRUCH', 'Aligne au moins 3 bonbons identiques : ils disparaissent, les autres tombent.',
                           ['Commandes :', 'croix / stick : curseur', 'X : prendre un bonbon', 'puis une direction :',
                            '  echanger', 'rond : annuler', '', 'Regles :', '4 alignes : bonbon raye', '5 alignes : bombe',
                            'score a atteindre en', 'un nombre de coups']))
    save(frames, 'jeu-cruch.gif')




def life_icons(s, rows, n=3, colors=None):  # vies en haut a droite avec un dessin donne (une couleur par icone si colors)
    for i in range(n):
        for r, line in enumerate(rows):
            for c, ch in enumerate(line):
                if ch != '.':
                    col = PAL[colors[i % len(colors)]] if (colors and ch == 'p') else PAL[ch]
                    s.px(W - 6 - i * 7 + c, 1 + r, col)


# ================================================================ TAUPE (une dalle 128x64)
def taupe():
    rnd = random.Random(12)
    HS, TS, Y0 = 64, 5, 14
    COLS, ROWS = 25, 10
    X0 = (W - COLS * TS) // 2
    grid = [[1] * COLS for _ in range(ROWS)]
    mc = COLS // 2
    for r in range(ROWS // 2 + 1):
        grid[r][mc] = 0
    en = []
    for i, (r, c, hz) in enumerate(((3, 3, 1), (6, 17, 1), (8, 6, 0), (4, 20, 0))):
        for k in range(4):
            if hz:
                grid[r][c + k] = 0
            else:
                grid[min(ROWS - 1, r + k // 2)][c] = 0
        en.append({'c': c, 'r': r, 'x': X0 + c * TS + 2.5, 'y': Y0 + r * TS + 2.5, 'dx': 1 if hz else 0, 'dy': 0 if hz else 1,
                   'type': i % 3 == 2, 'stun': 0, 'on': True})
    rocks = [{'c': 8, 'r': 2, 'y': Y0 + 2 * TS, 'st': 0, 'wob': 0, 'vy': 0}, {'c': 15, 'r': 5, 'y': Y0 + 5 * TS, 'st': 0, 'wob': 0, 'vy': 0}]
    pc, pr = mc, 0
    px, py = X0 + pc * TS + 2.5, Y0 + pr * TS + 2.5
    face, ddx, ddy = 1, 0, 1
    shot, booms, score, frames = None, [], 0, []
    SOIL = ((170, 110, 50), (200, 120, 40), (150, 70, 40), (110, 50, 90))
    MOLE = (("..ooo.", ".oooob", "oooooo", "ooooop", ".w..w."), ("..ooo.", ".oooob", "oooooo", "ooooop", "w..w.."))
    WORM = ((".ppp..", "pwpppp", "pppppp", "..pp.."), ("..ppp.", "ppwppp", "pppppp", ".p..p."))
    BAT = (("v.vv.v", "vvwwvv", ".vvvv.", "..vv.."), ("..vv..", ".vwwv.", "vvvvvv", "v....v"))
    for f in range(480):
        live = [e for e in en if e['on']]
        if not live:
            break
        tgt = min(live, key=lambda e: abs(e['c'] - pc) + abs(e['r'] - pr))
        cx, cy = X0 + pc * TS + 2.5, Y0 + pr * TS + 2.5
        if abs(px - cx) < 0.01 and abs(py - cy) < 0.01:
            dc, dr = tgt['c'] - pc, tgt['r'] - pr
            aligned = (dr == 0 and 0 < abs(dc) <= 3) or (dc == 0 and 0 < abs(dr) <= 3)
            if aligned and shot is None:
                ddx, ddy = (1 if dc > 0 else -1, 0) if dr == 0 else (0, 1 if dr > 0 else -1)
                face = ddx or face
                shot = {'x': px, 'y': py, 'dx': ddx, 'dy': ddy, 'left': TS * 3}
            elif not aligned:
                if abs(dc) > abs(dr):
                    ddx, ddy = (1 if dc > 0 else -1), 0
                else:
                    ddx, ddy = 0, (1 if dr > 0 else -1)
                if ddx:
                    face = ddx
                nc, nr = pc + ddx, pr + ddy
                if 0 <= nc < COLS and 0 <= nr < ROWS and not any(k['c'] == nc and int((k['y'] - Y0) // TS) == nr for k in rocks):
                    pc, pr = nc, nr
        sp = 0.55 if grid[pr][pc] else 0.95
        tx, ty = X0 + pc * TS + 2.5, Y0 + pr * TS + 2.5
        px += max(-sp, min(sp, tx - px))
        py += max(-sp, min(sp, ty - py))
        if grid[pr][pc] and abs(px - tx) + abs(py - ty) < 3:
            grid[pr][pc] = 0
            score += 1
        if shot:
            for _ in range(2):
                shot['x'] += shot['dx']; shot['y'] += shot['dy']; shot['left'] -= 1
                c, r = int((shot['x'] - X0) // TS), int((shot['y'] - Y0) // TS)
                if shot['left'] <= 0 or not (0 <= c < COLS and 0 <= r < ROWS) or grid[r][c]:
                    shot = None
                    break
                hit = [e for e in en if e['on'] and abs(e['x'] - shot['x']) < 3.5 and abs(e['y'] - shot['y']) < 3.5]
                if hit:
                    e = hit[0]
                    if e['stun']:
                        e['on'] = False
                        score += 200 + 100 * (e['r'] * 4 // ROWS)
                        booms.append([int(e['x']), int(e['y']), 1])
                    else:
                        e['stun'] = 140
                    shot = None
                    break
        for e in en:
            if not e['on']:
                continue
            if e['stun']:
                e['stun'] -= 1
                continue
            ex, ey = X0 + e['c'] * TS + 2.5, Y0 + e['r'] * TS + 2.5
            if abs(e['x'] - ex) < 0.01 and abs(e['y'] - ey) < 0.01:
                opts = [(dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if 0 <= e['c'] + dx < COLS and 0 <= e['r'] + dy < ROWS
                        and grid[e['r'] + dy][e['c'] + dx] == 0 and (dx, dy) != (-e['dx'], -e['dy'])]
                if not opts:
                    opts = [(-e['dx'], -e['dy'])]
                e['dx'], e['dy'] = rnd.choice(opts)
                e['c'] += e['dx']; e['r'] += e['dy']
            e['x'] += max(-0.45, min(0.45, X0 + e['c'] * TS + 2.5 - e['x']))
            e['y'] += max(-0.45, min(0.45, Y0 + e['r'] * TS + 2.5 - e['y']))
        for k in rocks:
            r = int((k['y'] - Y0) // TS)
            if k['st'] == 0 and r + 1 < ROWS and grid[r + 1][k['c']] == 0 and not (pc == k['c'] and pr == r + 1):
                k['st'], k['wob'] = 1, 40
            elif k['st'] == 1:
                k['wob'] -= 1
                if k['wob'] <= 0:
                    k['st'], k['vy'] = 2, 0.3
            elif k['st'] == 2:
                k['vy'] = min(k['vy'] + 0.08, 2.5)
                k['y'] += k['vy']
                for e in en:
                    if e['on'] and e['c'] == k['c'] and abs(e['y'] - (k['y'] + TS / 2)) < TS:
                        e['on'] = False
                        score += 600
                        booms.append([int(e['x']), int(e['y']), 1])
                rr = int((k['y'] + TS - 1 - Y0) // TS)
                if rr >= ROWS or grid[rr][k['c']]:
                    k['st'] = 3
                    booms.append([X0 + k['c'] * TS + 2, int(k['y']) + 2, 1])
        rocks = [k for k in rocks if k['st'] != 3]
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, HS)
        d = ImageDraw.Draw(s.im)
        for y in range(8, Y0 - 1):
            d.line((0, y, W - 1, y), fill=(20, 40 + (y - 8) * 8, 120 + (y - 8) * 10))
        d.line((0, Y0 - 1, W - 1, Y0 - 1), fill=(40, 200, 60))
        d.rectangle((W - 12, 9, W - 9, 12), fill=(255, 220, 60))
        for r in range(ROWS):
            so = SOIL[r * 4 // ROWS]
            dk = tuple(v * 2 // 3 for v in so)
            for c in range(COLS):
                x, y = X0 + c * TS, Y0 + r * TS
                if grid[r][c]:
                    d.rectangle((x, y, x + TS - 1, y + TS - 1), fill=so)
                    s.px(x + (r * 3 + c * 7) % TS, y + (r * 5 + c * 3) % TS, dk)
                    s.px(x + (r * 7 + c * 5 + 2) % TS, y + (r + c * 4 + 1) % TS, dk)
        for k in rocks:
            x, y = X0 + k['c'] * TS + (((f // 2) & 1) if k['st'] == 1 else 0), int(k['y'])
            d.rectangle((x, y + 1, x + TS - 1, y + TS - 1), fill=(130, 130, 140))
            d.line((x + 1, y, x + TS - 2, y), fill=(130, 130, 140))
            d.line((x + 1, y + 1, x + 2, y + 1), fill=(220, 220, 230))
            d.line((x + 1, y + TS - 1, x + TS - 2, y + TS - 1), fill=(60, 60, 70))

        def spr(rows, x, y, flip):
            for rr, line in enumerate(rows):
                for cc, ch in enumerate(line[::-1] if flip else line):
                    if ch != '.':
                        s.px(x + cc, y + rr, PAL[ch])
        for e in en:
            if not e['on']:
                continue
            fr = (f // 8) & 1
            spr((BAT if e['type'] else WORM)[0 if e['stun'] else fr], int(e['x']) - 3, int(e['y']) - 2, e['dx'] < 0)
            if e['stun'] and f & 4:
                d.line((int(e['x']) - 2, int(e['y']) - 4, int(e['x']) + 1, int(e['y']) - 4), fill=PAL['y'])
        if shot:
            d.rectangle((int(shot['x']) - 1, int(shot['y']) - 1, int(shot['x']), int(shot['y'])), fill=(150, 90, 40))
        spr(MOLE[(f // 6) & 1], int(px) - 3, int(py) - 2, face < 0)
        for b in booms:
            boom(s, b)
        hud(s, score)
        frames.append(page(led64(s.im), 'TAUPE', 'Une taupe creuse ses galeries ; jet de boue, rochers qui tombent.',
                           ['Commandes :', 'croix / stick : creuser', 'X : jet de boue', '', 'Regles :', '1er jet : assommee',
                            '2e jet : elle eclate', '(plus profond = plus', '  de points)', 'creuser sous un rocher :',
                            '  il tombe et ecrase', 'tout elimine : stage +1']))
    save(frames, 'jeu-taupe.gif')


# ================================================================ BILLES (une dalle 128x64)
def billes():
    rnd = random.Random(8)
    HS, B, RH, Y0, NC, NR = 64, 6, 5, 9, 15, 14
    X0 = 4
    CXL = X0 + (NC * B + B // 2) / 2  # canon au milieu du puits
    VIS = (HS - Y0 - B - 6) // RH
    COLS = [None, (255, 50, 60), (255, 220, 0), (40, 220, 80), (40, 120, 255), (200, 70, 255), (0, 220, 255)]
    G = [[0] * NC for _ in range(NR)]
    for r in range(5):
        for c in range(NC - (r & 1)):
            G[r][c] = 1 + rnd.randrange(4)

    def rc(r):
        return NC - (r & 1)

    def cxy(r, c):
        return X0 + c * B + (B / 2 if r & 1 else 0) + B / 2, Y0 + r * RH + B / 2

    def nbrs(r, c):
        o = r & 1
        dd = ((0, -1), (0, 1), (-1, 0), (-1, 1), (1, 0), (1, 1)) if o else ((0, -1), (0, 1), (-1, -1), (-1, 0), (1, -1), (1, 0))
        return [(r + a, c + b) for a, b in dd if 0 <= r + a < NR and 0 <= c + b < rc(r + a)]

    def flood(r0, c0, col):
        seen, q = {(r0, c0)}, [(r0, c0)]
        while q:
            r, c = q.pop()
            for nr, nc in nbrs(r, c):
                if (nr, nc) not in seen and G[nr][nc] and (not col or G[nr][nc] == col):
                    seen.add((nr, nc))
                    q.append((nr, nc))
        return seen

    def trace(ang):
        x, y, vx, vy = CXL, HS - B / 2 - 3, math.sin(ang), -math.cos(ang)
        pts = []
        for st in range(400):
            x += vx; y += vy
            if x < X0 + B / 2:
                x, vx = X0 + B / 2, -vx
            if x > X0 + NC * B:
                x, vx = X0 + NC * B, -vx
            pts.append((x, y))
            if y <= Y0 + B / 2 or any(G[r][c] and abs(cxy(r, c)[0] - x) < B * .9 and abs(cxy(r, c)[1] - y) < B * .9
                                     for r in range(VIS + 1) for c in range(rc(r))):
                best = min(((r, c) for r in range(NR) for c in range(rc(r)) if not G[r][c]),
                           key=lambda rc_: (cxy(*rc_)[0] - x) ** 2 + (cxy(*rc_)[1] - y) ** 2)
                return best, pts
        return None, pts
    cur, nxt, ang, target, flying, fb, score, frames, booms, shots = 1 + rnd.randrange(4), 1 + rnd.randrange(4), 0.0, None, None, 0, 0, [], [], 0
    for f in range(560):
        if flying is None:
            if target is None:
                best = -99
                for a in range(-70, 71, 4):
                    land, _ = trace(a * 0.01745)
                    if not land:
                        continue
                    G[land[0]][land[1]] = cur
                    n = len(flood(land[0], land[1], cur)) * 10 - land[0] + rnd.randrange(5)
                    G[land[0]][land[1]] = 0
                    if n > best:
                        best, target = n, a * 0.01745
            if abs(target - ang) > 0.03:
                ang += 0.035 if target > ang else -0.035
            else:
                land, pts = trace(ang)
                flying, fb, target = (land, pts), 0, None
        else:
            fb += 3
            if fb >= len(flying[1]):
                (r, c), _ = flying
                G[r][c] = cur
                grp = flood(r, c, cur)
                if len(grp) >= 3:
                    for rr, cc in grp:
                        G[rr][cc] = 0
                        if rnd.random() < 0.4:
                            booms.append([int(cxy(rr, cc)[0]), int(cxy(rr, cc)[1]), 1])
                    score += 10 * len(grp)
                    keep = set()
                    for cc in range(rc(0)):
                        if G[0][cc]:
                            keep |= flood(0, cc, 0)
                    fall = [(rr, cc) for rr in range(NR) for cc in range(rc(rr)) if G[rr][cc] and (rr, cc) not in keep]
                    for rr, cc in fall:
                        G[rr][cc] = 0
                        booms.append([int(cxy(rr, cc)[0]), int(cxy(rr, cc)[1]), 1])
                    if fall:
                        score += 20 << min(len(fall), 10)
                shots += 1
                cur, nxt, flying = nxt, 1 + rnd.randrange(4), None
                if not any(G[rr][cc] for rr in range(NR) for cc in range(rc(rr))):
                    for rr in range(5):
                        for cc in range(rc(rr)):
                            G[rr][cc] = 1 + rnd.randrange(4)
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, HS)
        d = ImageDraw.Draw(s.im)
        bw = NC * B + B // 2
        by1 = Y0 + VIS * RH + B // 2
        d.rectangle((X0, 8, X0 + bw - 1, HS - 1), fill=(10, 12, 30))
        d.rectangle((X0 - 2, 8, X0 - 1, HS - 1), fill=(90, 100, 160))
        d.rectangle((X0 + bw, 8, X0 + bw + 1, HS - 1), fill=(90, 100, 160))
        d.rectangle((X0, 8, X0 + bw - 1, Y0 - 1), fill=(150, 120, 60))
        for x in range(X0, X0 + bw, 3):
            s.px(x, by1, (255, 60, 60))

        def ball(cx, cy, col):
            c = COLS[col]
            cx, cy = int(cx), int(cy)
            d.rectangle((cx - 2, cy - 1, cx + 2, cy + 1), fill=c)
            d.rectangle((cx - 1, cy - 2, cx + 1, cy + 2), fill=c)
            s.px(cx - 1, cy - 1, (255, 255, 255))
            s.px(cx + 1, cy + 2, tuple(v // 3 for v in c)); s.px(cx + 2, cy + 1, tuple(v // 3 for v in c))
        for r in range(NR):
            for c in range(rc(r)):
                if G[r][c]:
                    ball(*cxy(r, c), G[r][c])
        cx, cy = int(CXL), HS - B // 2 - 3
        if flying is None:
            _, pts = trace(ang)
            for i, (x, y) in enumerate(pts):
                if i % 4 == 0 and i > 6:
                    s.px(int(x), int(y), (140, 140, 160))
            ball(cx, cy, cur)
        else:
            x, y = flying[1][min(fb, len(flying[1]) - 1)]
            ball(x, y, cur)
        d.line((cx, cy, cx + int(math.sin(ang) * (B + 2)), cy - int(math.cos(ang) * (B + 2))), fill=PAL['w'])
        d.rectangle((cx - 4, HS - 3, cx + 4, HS - 1), fill=(120, 120, 140))
        pxx = X0 + bw + 6
        s.tiny(W - 32, 1, 'APRES', (110, 110, 140))  # sur la ligne du score
        ball(W - 6, 3, nxt)
        for i in range(8 - shots % 8):
            d.rectangle((pxx + i * 3, HS - 6, pxx + i * 3 + 1, HS - 4), fill=(255, 160, 40))
        for b in booms:
            boom(s, b)
        hud(s, score, 0)
        frames.append(page(led64(s.im), 'BILLES', 'Vise et tire : 3 billes de meme couleur ou plus eclatent.',
                           ['Commandes :', 'gauche / droite : viser', 'stick : angle direct', 'X : tirer', '', 'Regles :',
                            '3 ou plus : eclatent', 'detachees : tombent', '  (gros bonus)', 'tous les 8 tirs, le',
                            '  plafond descend', 'ligne rouge : fin']))
    save(frames, 'jeu-billes.gif')


# ================================================================ ESCADRILLE (une dalle 128x64)
def escadrille():
    rnd = random.Random(11)
    HS, TOP = 64, 10
    SEA, SHAL, SAND, LEAF, LEAF2 = (10, 60, 150), (40, 160, 200), (230, 205, 90), (60, 190, 60), (0, 150, 0)
    GV = (((205, 75, 50), (150, 30, 20)), ((190, 190, 205), (150, 150, 160)), ((245, 200, 40), (190, 150, 0)))
    ME = (("...........", ".....B.....", "....BcB....", "BBBBBBBBBBB", "BWBBBbBBBWB", ".bbBBBBBbb.", ".....B.....", "...BBBBB...", "....bBb...."),
          ("...........", ".....B.....", "....BcB....", "BBBBBBBBBBB", "BWBBBbBBBWB", ".bbBBBBBbb.", ".....B.....", "...BBBBB...", "....bBb...."))
    ROLL = ("...........", ".....B.....", "....BcB....", "..BBBBBBB..", "..bWBbBWb..", "...bBBBb...", ".....B.....", "...BBBBB...", "....bBb....")
    FIGHT = (("...gGg...", "....G....", "GGGGGGGGG", "gGGGgGGGg", "...GcG...", "....G....", "........."),
             ("...gGg...", "....G....", "GGGGGGGGG", "gGGGgGGGg", "...GcG...", "....G....", "........."))
    BOMBER = tuple(re.findall(r'"([^"]*)"', re.search(r'SPR_A_BOMBER\[11\] = \{(.*?)\};', SRC_INO, re.S).group(1)))
    BOAT = ("..S..", ".SSS.", ".SkS.", "SSSSS", "SSsSS", "SsssS", "SSsSS", "SSSSS", ".SSS.", ".SkS.", ".SSS.", ".www.")
    DEST = ("...S...", "..SSS..", "..SkS..", ".SSSSS.", ".SSkSS.", "SSSSSSS", "SSsssSS", "SSsssSS", "SSskSSS", "SSsssSS",
            "SSSSSSS", "SSSkSSS", "SSSSSSS", "SSsssSS", "SSSSSSS", ".SSkSS.", ".SSSSS.", "..SSS..", "..www..", ".w...w.")
    LIFE = ("..b..", "bwbwb", "..b..", ".bbb.")
    SIZE = {0: (9, 7), 1: (17, 11), 2: (7, 18), 3: (9, 28), 4: (15, 40)}

    def col(ch, var):
        return {'B': (0, 90, 20) if var == 3 else (20, 150, 40), 'b': (0, 90, 20), 'W': (255, 255, 255), 'y': (255, 220, 0),
                'o': (255, 140, 0), 'c': (120, 220, 255), 'k': (40, 40, 50), 'G': GV[var % 3][0], 'g': GV[var % 3][1],
                'S': (188, 192, 202), 's': (150, 155, 165), 'n': (160, 105, 55), 'N': (230, 210, 170), 'L': (235, 240, 250),
                'H': ((255, 140, 110), (235, 235, 250), (255, 240, 130))[var % 3]}.get(ch, (200, 230, 255))

    def spr(sc, rows, x0, y0, var=0, white=False):
        for r, line in enumerate(rows):
            for cc, ch in enumerate(line):
                if ch != '.':
                    if int(y0) + r >= TOP:
                        sc.px(int(x0) + cc, int(y0) + r, (255, 255, 255) if white else col(ch, var))
    waves = [[rnd.randrange(W), rnd.randrange(TOP, HS)] for _ in range(56)]
    isles = [[30, 26.0, 9, 7], [95, -30.0, 7, 3]]
    x, y, score, roll, rolls = 60.0, 52.0, 0, 0, 3
    en, shots, bombs, booms, frames = [], [], [], [], []
    spawn, grp, cap = 10, 0, None
    for f in range(560):
        spawn -= 1
        if spawn <= 0:
            pat, var = grp % 5, (grp // 2) % 3
            if pat == 0:
                for k in range(5):
                    en.append({'t': 0, 'x': W / 2 - 4 + (k - 2) * 11, 'y': -8 - abs(k - 2) * 6, 'type': 0, 'hp': 1, 'pat': 0, 'fl': 0, 'var': var, 'cap': k == 4})
            elif pat == 1:
                en.append({'t': 0, 'x': W / 2 - 8, 'y': -12, 'type': 1, 'hp': 8, 'pat': 3, 'fl': 0, 'var': grp % 2, 'cap': True})
            elif pat == 2:
                en.append({'t': 0, 'x': 18, 'y': -20, 'type': 2, 'hp': 4, 'pat': 4, 'fl': 0, 'var': 0, 'cap': False})
                en.append({'t': 0, 'x': 92, 'y': -54, 'type': 3, 'hp': 6, 'pat': 4, 'fl': 0, 'var': 0, 'cap': False})
            elif pat == 3:
                for k in range(5):
                    en.append({'t': 0, 'x': W / 4, 'y': -8 - k * 10, 'type': 0, 'hp': 1, 'pat': 1, 'fl': 0, 'var': var, 'cap': False})
            else:
                en.append({'t': 0, 'x': 70, 'y': -42, 'type': 4, 'hp': 14, 'pat': 4, 'fl': 0, 'var': 0, 'cap': True})
            grp += 1
            spawn = 105
        tgt = min((e for e in en if e['y'] > 8), key=lambda e: abs(e['x'] + SIZE[e['type']][0] / 2 - x - 5), default=None)
        if cap:
            x += max(-1.6, min(1.6, (cap[0] - 2 - x) * 0.2))
        elif tgt:
            x += max(-1.6, min(1.6, (tgt['x'] + SIZE[tgt['type']][0] / 2 - 5 - x) * 0.2))
        x = max(0, min(W - 11, x))
        if roll:
            roll -= 1
        elif rolls and any(abs(b[0] - x - 5) < 6 and y - 6 < b[1] < y + 2 for b in bombs):
            roll, rolls = 30, rolls - 1
        if f % 6 == 0 and not roll:
            shots += [[x + 3, y - 1], [x + 7, y - 1]]
        for sh in shots:
            sh[1] -= 3.8
        shots = [sh for sh in shots if sh[1] > TOP]
        for wv in waves:
            wv[1] += f & 1
            if wv[1] >= HS:
                wv[0], wv[1] = rnd.randrange(-2, W), TOP
        for isl in isles:
            isl[1] += 0.5
            if isl[1] - isl[2] * 2 - 6 > HS:
                isl[0], isl[1], isl[2], isl[3] = rnd.randrange(14, W - 14), -34.0, rnd.randrange(6, 12), rnd.randrange(65536)
        if cap:
            cap[1] += 0.6
            if cap[1] > HS:
                cap = None
            elif abs(cap[0] + 3 - x - 5) < 8 and abs(cap[1] + 3 - y - 4) < 8:
                score += 500
                booms.append([int(cap[0]) + 3, int(cap[1]) + 3, 1])
                cap = None
        for e in en:
            e['t'] += 1
            e['fl'] = max(0, e['fl'] - 1)
            w_, h_ = SIZE[e['type']]
            if e['pat'] == 0:
                e['y'] += 0.75
            elif e['pat'] == 1:
                e['y'] += 0.7; e['x'] += math.sin(e['t'] * 0.06) * 1.1
            elif e['pat'] == 3:
                e['y'] += 0.5 if e['y'] < 14 else 0.18
                if e['t'] % 50 == 0:
                    for a in (-0.3, 0, 0.3):
                        bombs.append([e['x'] + 8, e['y'] + 11, math.sin(a) * 1.1, math.cos(a) * 1.1])
            elif e['pat'] == 5:
                e['x'] += e['vx']; e['y'] += e['vy']
            else:
                e['y'] += 0.35
                if e['type'] < 4 and e['t'] % 60 == 30 and e['y'] > TOP:
                    dx, dy = x + 5 - e['x'], y + 4 - e['y']
                    dd = math.hypot(dx, dy) + .01
                    bombs.append([e['x'] + w_ // 2, e['y'] + 6, dx / dd * 1.3, dy / dd * 1.3])
                if e['type'] == 4 and e['t'] % 110 == 60:  # porte-avions : un chasseur decolle et pique
                    dx, dy = x - e['x'], y - e['y'] - 20
                    dd = math.hypot(dx, dy) + .01
                    en.append({'t': 0, 'x': e['x'] + 1, 'y': e['y'] + 20, 'type': 0, 'hp': 1, 'pat': 5, 'fl': 0, 'var': 1, 'cap': False,
                               'vx': dx / dd * 2, 'vy': dy / dd * 2})
            for sh in shots:
                if e['x'] <= sh[0] <= e['x'] + w_ and e['y'] <= sh[1] <= e['y'] + h_:
                    sh[1] = -99
                    e['hp'] -= 1
                    e['fl'] = 3
                    if e['hp'] <= 0:
                        score += (100, 500, 300, 600, 1500)[e['type']]
                        booms.append([int(e['x'] + w_ / 2), int(e['y'] + h_ / 2), 1])
                        if e['type']:
                            booms.append([int(e['x'] + w_ / 2) - 3, int(e['y']) + 2, 3])
                        if e['cap'] and not cap:
                            cap = [e['x'] + w_ / 2 - 3, e['y'], 2 if e['type'] == 4 else (grp % 2)]
                    break
        en = [e for e in en if e['hp'] > 0 and -60 < e['y'] < HS and -20 < e['x'] < W + 20]
        for b in bombs:
            b[0] += b[2]; b[1] += b[3]
        bombs = [b for b in bombs if TOP <= b[1] < HS and 0 <= b[0] < W]
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 15]
        if f % 2:
            continue
        sc = Screen(W, HS)
        d = ImageDraw.Draw(sc.im)
        d.rectangle((0, TOP, W - 1, HS - 1), fill=SEA)

        def P_(x_, y_, c_):  # dessin limite a la mer
            if 0 <= x_ < W and TOP <= y_ < HS:
                sc.px(x_, y_, c_)

        def disc(cx_, cy_, r_, c_):
            for dy in range(-r_, r_ + 1):
                dx = int(math.sqrt(r_ * r_ - dy * dy + 0.5))
                for xx in range(cx_ - dx, cx_ + dx + 1):
                    P_(xx, cy_ + dy, c_)
        for k, (wx, wy) in enumerate(waves):  # houle, cretes, moutons, reflets
            kind = k % 7
            if wy < TOP + 3:
                continue
            if kind < 2:
                for xx in range(wx, wx + 6 + k % 5):
                    P_(xx, wy, (4, 28, 80))
            elif kind < 6:
                c_ = (150, 175, 205) if kind == 5 else (80, 150, 230)
                P_(wx, wy, c_); P_(wx + 3, wy, c_); P_(wx + 1, wy - 1, c_); P_(wx + 2, wy - 1, c_)
                if kind == 5:
                    P_(wx + 1 + ((f >> 3) & 1), wy - 2, (190, 205, 225))
            else:
                P_(wx, wy, (170, 170, 150) if ((f >> 2) + k) % 6 < 2 else (80, 150, 230))
        for ix, iy, r, seed in isles:  # iles (comme aIsland)
            iy = int(iy)
            sd = [seed + 1]

            def rn(n):
                sd[0] = (sd[0] * 1103515245 + 12345) & 0xFFFFFFFF
                return (sd[0] >> 16) % max(1, n)
            nb = 2 + rn(3)
            blobs = [(0, 0, r)] + [(rn(r * 2 + 1) - r, rn(r * 2 + 1) - r, r // 2 + rn(r // 2 + 1)) for _ in range(nb - 1)]
            for off, c_ in ((4, (40, 160, 200)), (2, (150, 210, 230)), (0, (230, 205, 90)), (-2, (60, 190, 60)), (-4, (20, 150, 40)), (-6, (0, 150, 0))):
                for bx_, by_, br_ in blobs:
                    if br_ + off > 0:
                        disc(ix + bx_, iy + by_, br_ + off, c_)
            for _ in range(1 + rn(3)):
                px_, py_ = ix + rn(r) - r // 2, iy + rn(r) - r // 2
                P_(px_, py_ + 1, (150, 90, 30))
                for qx, qy in ((-1, 0), (1, 0), (0, -1)):
                    P_(px_ + qx, py_ + qy, (60, 210, 80))
                P_(px_, py_, (35, 150, 50))
            if r >= 8 and rn(2):
                hx, hy = ix - r // 2 + rn(r // 2), iy + r // 3
                for xx in range(3):
                    P_(hx + xx, hy, (235, 230, 215)); P_(hx + xx, hy + 1, (235, 230, 215)); P_(hx + xx, hy - 1, (205, 55, 40))
                for xx in range(2):
                    P_(hx + 4 + xx, hy + 1, (235, 230, 215)); P_(hx + 4 + xx, hy + 2, (235, 230, 215)); P_(hx + 4 + xx, hy, (205, 55, 40))
            P_(ix + r + 3, iy - r // 2, (160, 155, 145)); P_(ix + r + 4, iy - r // 2, (160, 155, 145)); P_(ix + r + 3, iy - r // 2 + 1, (90, 88, 80))

        def turret(tx, ty, c_):
            disc(tx, ty, 1, c_)
            dx, dy = x + 5 - tx, y + 4 - ty
            dd = math.hypot(dx, dy) + .01
            for k in range(1, 4):
                P_(tx + round(dx / dd * k), ty + round(dy / dd * k), (35, 35, 45))
        for e in en:  # navires (comme aShip V3.99)
            if e['type'] < 2:
                continue
            ex, ey = int(e['x']), int(e['y'])
            w_, h_ = SIZE[e['type']]
            cx_, hw0 = ex + w_ // 2, w_ // 2
            fl = e['fl'] > 0
            wh = (255, 255, 255)
            edge, hull, lite = (wh, wh, wh) if fl else ((45, 50, 62), (178, 182, 194), (225, 228, 236))
            shade, rail, dark = (wh, wh, wh) if fl else ((150, 155, 166), (225, 228, 235), (38, 40, 50))
            glass, boatc, redc = (wh, wh, wh) if fl else ((0, 210, 250), (255, 130, 20), (200, 40, 30))
            foam, foam2, seash = (170, 195, 225), (55, 100, 160), (3, 22, 64)
            for k in range(14):
                yy, sp_ = ey + h_ + k, hw0 - 1 + k // 2
                c_ = foam if k < 5 else foam2
                if (k + (f >> 2)) % 5:
                    P_(cx_ - sp_, yy, c_); P_(cx_ + sp_, yy, c_)
                if k < 10 and (k + (f >> 1)) % 3 == 0:
                    P_(cx_ + int(math.sin(k * 1.7 + f * 0.25) * 1.5), yy, foam2)
            P_(cx_, ey - 1, foam)
            for k in range(1, 4):
                c_ = foam if ((f >> 2) + k) & 1 else foam2
                P_(cx_ - k - 1, ey + k * 2 - 1, c_); P_(cx_ + k + 1, ey + k * 2 - 1, c_)
            for j_ in range(h_):
                hw = min(hw0, j_ // 2 + (j_ > 0))
                if j_ >= h_ - 2:
                    hw = max(1, hw - (j_ - (h_ - 3)))
                for xx in range(cx_ - hw, cx_ + hw + 1):
                    P_(xx, ey + j_, edge if xx in (cx_ - hw, cx_ + hw) else hull)
                P_(cx_ + hw + 1, ey + j_ + 1, seash)
                if hw >= 2:
                    P_(cx_ - hw + 1, ey + j_, rail if (j_ % 3 == 1 and j_ > 2) else lite)
                    P_(cx_ + hw - 1, ey + j_, rail if (j_ % 3 == 1 and j_ > 2) else shade)

            def radar(a_, b_):
                P_(a_, b_, dark)
                q = (f >> 2) & 3
                rx_, ry_ = (-1, 0, 1, 0)[q], (0, -1, 0, 1)[q]
                P_(a_ + rx_, b_ + ry_, lite); P_(a_ - rx_, b_ - ry_, lite)
            if e['type'] == 2:
                turret(cx_, ey + 5, lite)
                for yy in range(4):
                    for xx in range(3):
                        P_(cx_ - 1 + xx, ey + 8 + yy, glass if yy == 0 else lite)
                P_(cx_, ey + 11, dark)
                radar(cx_, ey + 12)
                turret(cx_, ey + 15, lite)
            elif e['type'] == 3:
                turret(cx_, ey + 5, lite); turret(cx_, ey + 9, lite)
                for yy in range(4):
                    for xx in range(5):
                        P_(cx_ - 2 + xx, ey + 12 + yy, glass if yy == 0 else lite)
                radar(cx_, ey + 15)
                for fy in (17, 20):
                    for xx in range(3):
                        P_(cx_ - 1 + xx, ey + fy, redc); P_(cx_ - 1 + xx, ey + fy + 1, shade)
                    P_(cx_, ey + fy + 1, dark)
                P_(cx_ - 3, ey + 18, boatc); P_(cx_ - 3, ey + 19, boatc); P_(cx_ + 3, ey + 18, boatc); P_(cx_ + 3, ey + 19, boatc)
                for p in range(3):
                    g_ = 140 - p * 25
                    P_(cx_ + ((p + (f >> 3)) & 1) - (p == 2), ey + 22 + p * 2 + ((f >> 2) & 1), (g_, g_, g_ + 10))
                turret(cx_, ey + 25, lite)
            else:
                deck, mark, yel_ = ((wh, wh, wh) if fl else ((150, 152, 160), (230, 230, 230), (240, 200, 30)))
                for yy in range(32):
                    for xx in range(11):
                        P_(ex + 2 + xx, ey + 6 + yy, mark if xx in (0, 10) else deck)
                for j_ in range(ey + 8, ey + 36, 4):
                    P_(cx_, j_, yel_); P_(cx_, j_ + 1, yel_)
                for k in range(8):
                    P_(ex + 4 + k, ey + 21 + k * 2, deck if k & 1 else mark)
                for xx in range(7):
                    P_(ex + 4 + xx, ey + 35, mark)
                for yy in range(3):
                    for xx in range(3):
                        P_(ex + 4 + xx, ey + 10 + yy, dark)
                for yy in range(10):
                    for xx in range(3):
                        P_(ex + 11 + xx, ey + 15 + yy, glass if yy == 2 else (shade if xx == 0 else lite))
                radar(ex + 12, ey + 21)
                for p in range(3):
                    px_, py_ = ex + 4 + (p & 1) * 3, ey + 26 + p * 3
                    pc = wh if fl else (150, 160, 150)
                    for xx in range(3):
                        P_(px_ + xx, py_, pc)
                    P_(px_ + 1, py_ - 1, pc); P_(px_ + 1, py_ + 1, pc); P_(px_ + 1, py_, glass)

        def sil(rows, x0, y0, c_):
            for r_, line in enumerate(rows):
                for cc, ch in enumerate(line):
                    if ch != '.':
                        P_(int(x0) + cc, int(y0) + r_, c_)
        SH = (3, 22, 64)
        for e in en:  # ombres des avions
            if e['type'] == 0:
                sil(FIGHT[0], e['x'] + 3, e['y'] + 5, SH)
            elif e['type'] == 1:
                sil(BOMBER, e['x'] + 4, e['y'] + 6, SH)
        sil(ROLL if roll else ME[0], x + 4, y + 6, SH)
        if cap:
            cx_, cy_ = int(cap[0]), int(cap[1])
            fillc = ((255, 200, 0), (60, 120, 255), (230, 30, 30))[cap[2]]
            d.rectangle((cx_, cy_, cx_ + 6, cy_ + 6), fill=fillc, outline=(255, 255, 255) if f & 4 else (160, 90, 20))
            L = (("kk.", "k.k", "kk.", "k..", "k.."), ("kkk", ".k.", ".k.", ".k.", ".k."), ("...", ".k.", "kkk", ".k.", "..."))[cap[2]]
            for r, line in enumerate(L):
                for q, ch in enumerate(line):
                    if ch == 'k':
                        sc.px(cx_ + 2 + q, cy_ + 1 + r, (90, 40, 0) if cap[2] == 0 else (255, 255, 255))
        def prop(px_, py_, phase):  # helice qui tourne (comme aProp)
            q = phase & 3
            dx_, dy_ = (1, 1, 0, -1)[q], (0, 1, 1, 1)[q]
            P_(px_ - dx_, py_ - dy_, (255, 210, 0)); P_(px_ + dx_, py_ + dy_, (255, 210, 0)); P_(px_, py_, (215, 215, 225))
        for e in en:
            if e['type'] == 0:
                spr(sc, FIGHT[0], e['x'], e['y'], e['var'], e['fl'] > 0)
                prop(int(e['x']) + 4, int(e['y']) + 6, f >> 1)
            elif e['type'] == 1:
                spr(sc, BOMBER, e['x'], e['y'], e['var'] % 2, e['fl'] > 0)
                prop(int(e['x']) + 5, int(e['y']) + 9, f >> 1)
                prop(int(e['x']) + 11, int(e['y']) + 9, (f >> 1) + 1)
        for sh in shots:
            sc.px(int(sh[0]), int(sh[1]), (255, 255, 255)); sc.px(int(sh[0]), int(sh[1]) + 1, (255, 230, 60))
        for b in bombs:
            bx_, by_ = int(b[0]), int(b[1])
            sc.px(bx_, by_, PAL['r']); sc.px(bx_ + 1, by_, PAL['r']); sc.px(bx_, by_ + 1, PAL['o']); sc.px(bx_ + 1, by_ + 1, PAL['w'])
        for bx_, by_, t in booms:  # boules de feu
            r = 1 + t // 2 if t < 5 else 3 + (t - 5) // 3 if t < 10 else 4
            out = (255, 230, 120) if t < 4 else (255, 140, 0) if t < 8 else (220, 40, 20) if t < 11 else (90, 80, 80)
            inn = (255, 255, 255) if t < 4 else (255, 230, 60) if t < 8 else (255, 140, 0) if t < 11 else (60, 55, 55)
            d.ellipse((bx_ - r, by_ - r, bx_ + r, by_ + r), fill=out)
            ri = r - 2 + (t & 1)
            if r > 1 and ri >= 0:
                d.ellipse((bx_ - ri, by_ - ri, bx_ + ri, by_ + ri), fill=inn)
        me_ = ROLL if roll else ME[(f >> 1) & 1]
        for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1)):  # contour sombre de ton avion
            for r_, line in enumerate(me_):
                for cc, ch in enumerate(line):
                    if ch != '.':
                        sc.px(int(x) + ox + cc, int(y) + oy + r_, (0, 8, 30))
        spr(sc, me_, x, y, 3 if roll and (roll >> 2) & 1 else 0)
        prop(int(x) + 5, int(y), f >> 1)
        for k in range(rolls):
            d.ellipse((1 + k * 7, HS - 6, 5 + k * 7, HS - 2), outline=PAL['c'])
        hud(sc, score, 3, LIFE)
        frames.append(page(led64(sc.im), 'ESCADRILLE', 'Shoot vertical au-dessus de la mer : chasseurs, bombardiers, navires.',
                           ['Commandes :', 'croix / stick : voler', 'X, R1, R2 : tirer', 'rond : tonneau', '  (invincible, 3 par stage)',
                            'Regles :', 'capsules : P arme,', '  T tonneau, + vie', '10 vagues puis un boss', '  (2 avions en alternance)',
                            'porte-avions : 1500']))
    save(frames, 'jeu-escadrille.gif')


todo = GIF_ARGS or ['raid', 'raquettes', 'bulles', 'labyrinthe', 'orbite', 'briques', 'blocs', 'cruch', 'taupe', 'billes', 'escadrille']
for name in todo:
    globals()[name]()
