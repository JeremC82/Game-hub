"""GIF des nouveaux jeux (apercus simules, comme les demos du jeu) : RAID, COURSE.

Sorties : docs/jeu-raid.gif, docs/jeu-course.gif
Memes sprites, regles et pilotes automatiques que games.ino (version simplifiee pour l'apercu).
"""
import math
import os
import random

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)
Screen, SPR_W, SPR_SHIP, SPR_LIFE, PAL, font, SRC = (ns['Screen'], ns['SPR_W'], ns['SPR_SHIP'], ns['SPR_LIFE'], ns['PAL'],
                                                     ns['font'], ns['SRC'])
W = H = 128
P = 4
WHITE, SEL = (230, 230, 230), (255, 220, 0)
import re
run = re.findall(r'"([.wrybcgpov]{8})"', re.search(r'SPR_RUN\[3\]\[10\] = \{(.*?)\};', SRC, re.S).group(1))
SPR_RUN = [run[i * 10:(i + 1) * 10] for i in range(3)]


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


def hud(s, score, lives=3):
    s.tiny(1, 0, 'SCORE', PAL['r'])
    s.tiny(1, 6, str(score), WHITE)
    s.tiny(W // 2 - 11, 0, 'RECORD', PAL['r'])
    s.tiny(W // 2 - 11, 6, '30800', WHITE)
    s.tiny(W - 8, 1, 'X%d' % lives, WHITE)
    s.sprite(SPR_LIFE, W - 15, 1)


def line(s, x0, y0, x1, y1, col):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for k in range(n + 1):
        t = k / max(n, 1)
        s.px(int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)), col)


def boom(s, b):
    for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):
        s.px(b[0] + dx * b[2] // 2, b[1] + dy * b[2] // 2, PAL['w'] if b[2] < 4 else PAL['y'] if b[2] < 7 else PAL['r'])


def stars_init(rnd, n=70):
    return [[rnd.randrange(W), rnd.uniform(12, H), rnd.uniform(.2, 1)] for _ in range(n)]


def stars_draw(s, st, speed):
    for a in st:
        a[1] += a[2] * speed
        if a[1] >= H:
            a[1], a[0] = 12, random.randrange(W)
        g = int(70 + a[2] * 120)
        s.px(a[0], int(a[1]), (g, g, g))


def page(panel, title, sub, lines):
    im = Image.new('RGB', (panel.width + 330, panel.height + 110), (16, 16, 22))
    d = ImageDraw.Draw(im)
    d.text((20, 16), title, fill=(235, 235, 245), font=font(24, True))
    d.text((20, 50), sub, fill=(150, 150, 175), font=font(16))
    im.paste(panel, (20, 90))
    for k, t in enumerate(lines):
        d.text((panel.width + 40, 110 + k * 30), t, fill=(255, 220, 0) if t in ('Commandes :', 'Regles :') else (200, 200, 215),
               font=font(19, t in ('Commandes :', 'Regles :')))
    return im


def save(frames, name, ms=80):
    pal = frames[len(frames) // 2].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(DOC, name)
    q[0].save(path, save_all=True, append_images=q[1:], duration=ms, loop=0, optimize=True)
    print(path, len(frames), 'images', os.path.getsize(path) // 1024, 'Ko')


# ================================================================ RAID
def raid():
    rnd = random.Random(8)
    st = stars_init(rnd)
    px, py, tier = 58.0, 112.0, 1
    en, shots, bombs, booms = [], [], [], []
    boss = None
    score, frames, groups, spawn = 0, [], 0, 10
    for f in range(320):
        if boss is None:
            spawn -= 1
            if spawn <= 0 and groups < 3:
                pat = groups % 3
                for k in range(5):
                    e = {'x': W * (k + 1) / 6 - 4, 'y': -10, 't': -k * 12, 'pat': pat, 'type': pat, 'vx': 0}
                    if pat == 1:
                        e.update(x=-10, y=16, vx=1.1)
                    if pat == 2:
                        e.update(x=W / 2 - 4, vx=0.9 if k & 1 else -0.9)
                    en.append(e)
                groups += 1
                spawn = 70
            if groups >= 3 and not en:
                boss = {'x': W / 2 - 16, 'y': -18.0, 'hp': 60, 'max': 60, 't': 0}
        for e in list(en):
            e['t'] += 1
            if e['t'] < 0:
                continue
            if e['pat'] == 0:
                e['y'] += 0.7; e['x'] += math.sin(e['t'] * 0.05) * 0.9
            elif e['pat'] == 1:
                e['x'] += e['vx']; e['y'] += 0.45
            else:
                e['x'] += e['vx']; e['y'] += 0.8
                if e['x'] < 2 or e['x'] > W - 11:
                    e['vx'] = -e['vx']
            if e['y'] > H or e['x'] < -14 or e['x'] > W + 14:
                en.remove(e)
            elif rnd.random() < 0.008:
                d = math.hypot(px + 4 - e['x'] - 4, py - e['y']) + .01
                bombs.append([e['x'] + 4, e['y'] + 7, (px - e['x']) / d * 1.2, (py - e['y']) / d * 1.2])
        if boss:
            boss['t'] += 1
            if boss['y'] < 16:
                boss['y'] += 0.5
            else:
                boss['x'] = W / 2 - 16 + math.sin(boss['t'] * 0.02) * (W / 2 - 20)
                if boss['t'] % 45 == 0:
                    for i in range(-2, 3):
                        bombs.append([boss['x'] + 16, boss['y'] + 14, i * 0.45, 1.2])
        # pilote automatique
        tx = px
        cands = [e for e in en if e['t'] >= 0 and e['y'] > 10]
        if cands:
            tx = min(cands, key=lambda e: abs(e['x'] - px))['x']
        if boss:
            tx = boss['x'] + 12
        for b in bombs:
            if py - 26 < b[1] < py + 4 and abs(b[0] - px - 4) < 7:
                tx = px + 14 if b[0] < px + 4 else px - 14
        px += max(-2, min(2, tx - px))
        px = max(0, min(W - 9, px))
        if f % 6 == 0:
            for i in range(tier + 1):
                shots.append([px + 4 + (i * 4 - tier * 2), py - 2])
        for sh in shots:
            sh[1] -= 3.6
        for sh in shots:
            for e in list(en):
                if e['t'] >= 0 and e['x'] <= sh[0] <= e['x'] + 8 and e['y'] <= sh[1] <= e['y'] + 7:
                    en.remove(e); sh[1] = -10; score += 100
                    booms.append([int(e['x'] + 4), int(e['y'] + 3), 1])
                    break
            if boss and boss['x'] <= sh[0] <= boss['x'] + 31 and boss['y'] <= sh[1] <= boss['y'] + 14:
                sh[1] = -10; boss['hp'] -= 1
                if f & 1:
                    booms.append([int(sh[0]), int(boss['y'] + 10), 1])
                if boss['hp'] <= 0:
                    for k in range(6):
                        booms.append([int(boss['x'] + rnd.randrange(32)), int(boss['y'] + rnd.randrange(15)), 1])
                    boss, groups, spawn, score = None, 0, 40, score + 2000
        shots = [sh for sh in shots if sh[1] > 12]
        for b in bombs:
            b[0] += b[2]; b[1] += b[3]
        bombs = [b for b in bombs if 0 <= b[0] < W and b[1] < H]
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 1.4)
        for e in en:
            if e['t'] >= 0:
                s.sprite(SPR_W[1][e['type']][(f // 6) & 1], int(e['x']), int(e['y']))
        if boss:
            d = ImageDraw.Draw(s.im)
            x, y = int(boss['x']), int(boss['y'])
            body, dark = (220, 90, 20), (73, 30, 6)
            d.rectangle((x + 4, y + 2, x + 27, y + 9), fill=body)
            d.rectangle((x, y + 5, x + 31, y + 8), fill=body)
            d.rectangle((x + 8, y, x + 23, y + 2), fill=dark)
            for cx in (2, 26, 14):
                d.rectangle((x + cx, y + 9, x + cx + 3, y + 13), fill=dark)
            d.rectangle((x + 9, y + 4, x + 12, y + 6), fill=PAL['y'])
            d.rectangle((x + 19, y + 4, x + 22, y + 6), fill=PAL['y'])
            d.rectangle((10, 12, W - 11, 14), outline=(90, 90, 110))
            d.line((10, 13, 10 + (W - 21) * boss['hp'] // boss['max'], 13), fill=PAL['r'])
        for sh in shots:
            s.px(int(sh[0]), int(sh[1]), PAL['w']); s.px(int(sh[0]), int(sh[1]) + 1, PAL['y'])
        for b in bombs:
            s.px(int(b[0]), int(b[1]), PAL['r']); s.px(int(b[0]) + 1, int(b[1]), PAL['r'])
        for b in booms:
            boom(s, b)
        s.sprite(SPR_SHIP[tier], int(px), int(py))
        hud(s, score)
        frames.append(page(led(s.im), 'RAID', 'Shoot vertical : vagues d\'ennemis, capsules d\'arme, boss en fin de stage.',
                           ['Commandes :', 'croix / stick : bouger', '(partout en bas)', 'X : tirer', '', 'Regles :',
                            'capsule P : meilleure', 'arme (3 niveaux)', '7 vagues puis le boss', 'barre de vie en haut',
                            'boss detruit : stage', 'suivant, +1 vie']))
    save(frames, 'jeu-raid.gif')


# ================================================================ COURSE
def course():
    rnd = random.Random(2)
    st = stars_init(rnd, 50)
    GY = H - 6
    plats, items = [[-10, W + 20, GY]], []
    scroll, speed, y, vy, ground, nextx, lasty = 0.0, 1.3, GY - 10.0, 0.0, True, W + 20, GY
    score, frames, PX = 0, [], 22

    def gen():
        nonlocal nextx, lasty
        while nextx < scroll + W + 60:
            gap = rnd.randrange(14, 26) if rnd.random() < 0.5 else 0
            x0 = nextx + gap
            high = rnd.random() < 0.45
            yy = GY
            if high:
                yy = max(int(H * 0.4), lasty - rnd.randrange(8, 20))
            elif lasty < GY and rnd.random() < 0.5:
                yy = min(GY, lasty + rnd.randrange(6, 14))
            ln = rnd.randrange(22, 40) if high else rnd.randrange(36, 80)
            plats.append([x0, x0 + ln, yy])
            if ln > 40 and rnd.random() < 0.55:
                items.append([x0 + rnd.randrange(14, ln - 10), yy, 0])
            if rnd.random() < 0.6:
                items.append([x0 + ln / 2, yy - 14, 1])
            lasty, nextx = yy, x0 + ln
    gen()
    hold = False
    for f in range(300):
        scroll += speed
        wx = scroll + PX
        # pilote automatique : saute devant un trou, des piques ou une marche
        jump = False
        if ground:
            ahead = 10 + speed * 14
            if any(it[2] == 0 and wx < it[0] < wx + ahead for it in items):
                jump = True
            if not any(p[0] <= wx + 8 + speed * 6 <= p[1] and p[2] >= y + 9 for p in plats):
                jump = True
            if any(wx < p[0] < wx + ahead and p[2] < y + 8 for p in plats):
                jump = True
        if jump and ground:
            vy, ground, hold = -4.1, False, True
        if vy >= 0:
            hold = False
        vy = min(vy + (0.22 if hold and vy < 0 else 0.36), 4.5)
        old = y + 10
        y += vy
        ground = False
        for p in plats:
            if wx + 6 >= p[0] and wx + 1 <= p[1] and vy >= 0 and old <= p[2] + 1 and y + 10 >= p[2]:
                y, vy, ground = p[2] - 10, 0, True
        for it in list(items):
            sx = it[0] - scroll
            if it[2] == 1 and PX - 3 <= sx <= PX + 8 and y - 3 <= it[1] <= y + 10:
                items.remove(it); score += 50
        if y > H:  # (le pilote ne tombe pas, mais par securite)
            y, vy = GY - 10, 0
        if f % 4 == 0:
            score += 2
        plats = [p for p in plats if p[1] > scroll - 20]
        items = [it for it in items if it[0] > scroll - 20]
        gen()
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 0.15)
        d = ImageDraw.Draw(s.im)
        for p in plats:
            x0, x1 = max(0, int(p[0] - scroll)), min(W - 1, int(p[1] - scroll))
            if x1 <= x0:
                continue
            gnd = p[2] >= GY
            d.rectangle((x0, p[2], x1, H - 1 if gnd else p[2] + 2), fill=(110, 60, 25) if gnd else (120, 120, 140))
            d.line((x0, p[2], x1, p[2]), fill=(40, 200, 60))
        for it in items:
            sx = int(it[0] - scroll)
            if it[2] == 0:
                for t in range(3):
                    for r in range(4):
                        d.line((sx + t * 3 + (3 - r) // 2 - 1, it[1] - 1 - r, sx + t * 3 + (3 - r) // 2 - 1 + max(1, r // 2 + 1) - 1,
                                it[1] - 1 - r), fill=PAL['w'])
            else:
                c = PAL['c'] if f & 8 else PAL['p']
                for dx, dy in ((2, -3), (1, -2), (2, -2), (3, -2), (0, -1), (1, -1), (2, -1), (3, -1), (4, -1), (1, 0), (2, 0), (3, 0), (2, 1)):
                    s.px(sx + dx, it[1] + dy, c)
        fr = 2 if not ground else (f // 5) & 1
        s.sprite(SPR_RUN[fr], PX, int(y))
        hud(s, score)
        frames.append(page(led(s.im), 'COURSE', 'Un petit bonhomme court tout seul : sauter les trous, les piques, les marches.',
                           ['Commandes :', 'X ou haut : sauter', '(garder appuye :', 'saut plus haut)', '', 'Regles :',
                            'trou ou piques : -1 vie', 'cristal : 50 points', 'la distance rapporte', 'des points',
                            'stage tous les 1500 px :', 'plus vite, +1 vie']))
    save(frames, 'jeu-course.gif')


raid()
course()
