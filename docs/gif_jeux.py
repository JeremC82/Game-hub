"""GIF des jeux (apercus simules, comme les demos du jeu) : RAID, COURSE, RAQUETTES, BULLES, LABYRINTHE, SAUTERELLE, ORBITE.

Sorties : docs/jeu-raid.gif, docs/jeu-course.gif, docs/jeu-raquettes.gif, docs/jeu-bulles.gif, docs/jeu-labyrinthe.gif, docs/jeu-sauterelle.gif, docs/jeu-orbite.gif
Usage : python docs/gif_jeux.py [raid course raquettes bulles labyrinthe sauterelle orbite]
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


def hud(s, score, lives=3):  # score centre sur une ligne, vies en icones (comme le jeu)
    ns['hud_top'](s, W, score, lives)


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
        if boss:  # boss crabe a pinces
            x, y = int(boss['x']), int(boss['y'])
            body = PAL['w'] if boss['flash'] else (220, 90, 20)
            dark = (73, 30, 6)
            d.rectangle((x + 8, y + 3, x + 23, y + 11), fill=body)
            d.rectangle((x + 2, y + 1, x + 6, y + 4), fill=body)
            d.rectangle((x + 25, y + 1, x + 29, y + 4), fill=body)
            o = (f // 8) & 1
            d.rectangle((x + 1, y + 5 + o, x + 3, y + 7 + o), fill=dark)
            d.rectangle((x + 28, y + 5 + o, x + 30, y + 7 + o), fill=dark)
            d.rectangle((x + 9, y + 4, x + 12, y + 6), fill=PAL['y'])
            d.rectangle((x + 19, y + 4, x + 22, y + 6), fill=PAL['y'])
            d.rectangle((10, 12, W - 11, 14), outline=(90, 90, 110))
            d.line((10, 13, 10 + (W - 21) * max(0, boss['hp']) // boss['max'], 13),
                   fill=PAL['o'] if boss['hp'] < boss['max'] / 2 else PAL['r'])
        if cap:
            col = PAL['y'] if f & 4 else {'P': PAL['g'], 'S': PAL['c'], 'B': PAL['r']}[cap[2]]
            d.rectangle((int(cap[0]) - 1, int(cap[1]) - 1, int(cap[0]) + 7, int(cap[1]) + 8), outline=col)
            char6(s, int(cap[0]) + 1, int(cap[1]), cap[2], col)
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


# ================================================================ COURSE
def course():
    rnd = random.Random(2)
    st = stars_init(rnd, 40)
    GY = H - 6
    plats, items, blocks, rocks = [[-10, W + 20, GY]], [], [], []
    scroll, speed, y, vy, ground, nextx, lasty = 0.0, 1.3, GY - 10.0, 0.0, True, W + 20, GY
    score, coins, frames, PX, rock_at, pop = 0, 0, [], 22, 220, None

    def plat_at(wx):
        ys = [p[2] for p in plats if p[0] <= wx <= p[1]]
        return min(ys) if ys else None

    def gen():
        nonlocal nextx, lasty
        while nextx < scroll + W + 60:
            gap = rnd.randrange(14, 24) if rnd.random() < 0.45 else 0
            x0 = nextx + gap
            high = rnd.random() < 0.35
            yy = GY
            if high:
                yy = max(int(H * 0.45), lasty - rnd.randrange(8, 18))
            elif lasty < GY and rnd.random() < 0.5:
                yy = min(GY, lasty + rnd.randrange(6, 14))
            ln = rnd.randrange(24, 42) if high else rnd.randrange(40, 86)
            plats.append([x0, x0 + ln, yy])
            if gap:  # arc de pieces au-dessus du trou
                for c in range(4):
                    items.append([x0 - gap + gap * (c + 0.5) / 4, yy - 14 - (6 if c in (1, 2) else 0), 2])
            r = rnd.random()
            if not high and ln > 50 and r < 0.45:  # rangee de blocs (avec blocs bonus) et pieces dessus
                n = rnd.randrange(3, 6)
                bx = x0 + rnd.randrange(10, int(ln) - n * 6 - 3)
                for b in range(n):
                    blocks.append([bx + b * 6, yy - 26, 1 if rnd.random() < 0.35 else 0])
                for b in range(0, n, 2):
                    items.append([bx + b * 6 + 1, yy - 32, 2])
            elif ln > 40 and r < 0.7:
                items.append([x0 + rnd.randrange(14, int(ln) - 10), yy, 0])
            else:
                for c in range(3):
                    items.append([x0 + ln / 2 - 8 + c * 8, yy - 8, 2])
            lasty, nextx = yy, x0 + ln
    gen()
    hold = False
    for f in range(420):
        scroll += speed
        wx = scroll + PX
        jump = False
        if ground:  # pilote automatique
            ahead = 10 + speed * 14
            if any(it[2] == 0 and wx < it[0] < wx + ahead for it in items):
                jump = True
            if any(wx < rk[0] < wx + ahead + 10 for rk in rocks):
                jump = True
            py_ = plat_at(wx + 8 + speed * 6)
            if py_ is None or py_ > y + 11:
                jump = True
            if any(wx < p[0] < wx + ahead and p[2] < y + 8 for p in plats):
                jump = True
            if any(b[0] - 3 < wx < b[0] + 3 and b[2] == 1 for b in blocks):  # coup de tete sous un bloc bonus
                jump = True
        if jump and ground:
            vy, ground, hold = -4.1, False, True
        if vy >= 0:
            hold = False
        vy = min(vy + (0.22 if hold and vy < 0 else 0.36), 4.5)
        old_feet, old_head = y + 10, y
        y += vy
        ground = False
        for p in plats:
            if wx + 6 >= p[0] and wx + 1 <= p[1] and vy >= 0 and old_feet <= p[2] + 1 and y + 10 >= p[2]:
                y, vy, ground = p[2] - 10, 0, True
        for b in blocks:
            if wx + 6 < b[0] or wx + 1 > b[0] + 6:
                continue
            if vy >= 0 and old_feet <= b[1] + 1 and y + 10 >= b[1]:
                y, vy, ground = b[1] - 10, 0, True
            elif vy < 0 and old_head >= b[1] + 6 and y <= b[1] + 6:
                y, vy = b[1] + 6, 0.5
                if b[2] == 1:
                    b[2] = 2
                    pop = [b[0] + 1, b[1] - 6, -2.2, 18]
                    coins += 1; score += 100
        if pop:
            pop[3] -= 1; pop[1] += pop[2]; pop[2] += 0.25
            if pop[3] <= 0:
                pop = None
        for it in list(items):
            sx = it[0] - scroll
            if it[2] >= 1 and PX - 4 <= sx <= PX + 8 and y - 4 <= it[1] <= y + 11:
                items.remove(it)
                coins += 1; score += 10
        if scroll > rock_at:  # rocher qui arrive en roulant
            rock_at = scroll + rnd.randrange(220, 420)
            rocks.append([scroll + W + 8, GY - 40.0, 0.0])
        for rk in list(rocks):
            rk[0] -= 0.9
            rk[2] = min(rk[2] + 0.3, 4)
            rk[1] += rk[2]
            py_ = plat_at(rk[0] + 3)
            if py_ is not None and rk[1] + 7 >= py_ and rk[1] + 7 - rk[2] <= py_ + 2:
                rk[1], rk[2] = py_ - 7, 0
            sx = rk[0] - scroll
            if sx + 7 >= PX + 1 and sx <= PX + 6 and rk[1] + 7 >= y and rk[1] <= y + 10 and vy > 0 and y + 10 <= rk[1] + 4:
                rocks.remove(rk); vy = -3; score += 200  # ecrase en retombant dessus
                continue
            if rk[1] > H or rk[0] < scroll - 20:
                rocks.remove(rk)
        if y > H:
            y, vy = GY - 10, 0
        if f % 4 == 0:
            score += 2
        plats = [p for p in plats if p[1] > scroll - 20]
        items = [it for it in items if it[0] > scroll - 20]
        blocks = [b for b in blocks if b[0] > scroll - 20]
        gen()
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 0.12, rnd)
        d = ImageDraw.Draw(s.im)
        m = scroll * 0.3
        for x in range(0, W, 2):  # montagnes au loin
            h = 10 + int(8 * math.sin((x + m) * 0.045) + 5 * math.sin((x + m) * 0.11 + 1))
            s.px(x, H - 20 - h, (40, 50, 90)); s.px(x + 1, H - 20 - h, (40, 50, 90))
        for p in plats:
            x0, x1 = max(0, int(p[0] - scroll)), min(W - 1, int(p[1] - scroll))
            if x1 <= x0:
                continue
            gnd = p[2] >= GY
            d.rectangle((x0, p[2], x1, H - 1 if gnd else p[2] + 2), fill=(110, 60, 25) if gnd else (120, 120, 140))
            d.line((x0, p[2], x1, p[2]), fill=(40, 200, 60))
        for b in blocks:
            sx = int(b[0] - scroll)
            if b[2] == 0:  # bloc de pierre
                d.rectangle((sx, b[1], sx + 5, b[1] + 5), fill=(150, 110, 70))
                d.line((sx, b[1] + 5, sx + 5, b[1] + 5), fill=(80, 55, 30))
                d.line((sx + 5, b[1], sx + 5, b[1] + 5), fill=(80, 55, 30))
            else:  # bloc bonus (bordure doree, cristal grave) ou eteint
                d.rectangle((sx, b[1], sx + 5, b[1] + 5), fill=(60, 40, 10) if b[2] == 1 else (70, 60, 50),
                            outline=GOLD if b[2] == 1 else (80, 55, 30))
                if b[2] == 1:
                    s.px(sx + 2, b[1] + 2, PAL['c']); s.px(sx + 3, b[1] + 3, PAL['c'])

        def coin(x, yy):
            d.line((x + 1, yy, x + 3, yy), fill=GOLD)
            d.rectangle((x, yy + 1, x + 4, yy + 3), fill=GOLD)
            d.line((x + 1, yy + 4, x + 3, yy + 4), fill=GOLD)
            s.px(x + 1, yy + 1, PAL['w'] if f & 16 else GOLD)
        for it in items:
            sx = int(it[0] - scroll)
            if it[2] == 0:  # piques
                for t in range(3):
                    for r in range(4):
                        x = sx + t * 3 + (3 - r) // 2 - 1
                        d.line((x, it[1] - 1 - r, x + max(1, r // 2 + 1) - 1, it[1] - 1 - r), fill=PAL['w'])
            else:
                coin(sx, int(it[1]) - 2)
        if pop:
            coin(int(pop[0] - scroll), int(pop[1]))
        for rk in rocks:
            sx = int(rk[0] - scroll)
            d.ellipse((sx, int(rk[1]), sx + 6, int(rk[1]) + 6), fill=(130, 120, 110))
            a = -rk[0] * 0.3
            d.line((sx + 3, int(rk[1]) + 3, sx + 3 + math.cos(a) * 3, int(rk[1]) + 3 + math.sin(a) * 3), fill=(60, 55, 50))
        fr = 2 if not ground else (f // 5) & 1
        s.sprite(SPR_RUN[fr], PX, int(y))
        hud(s, score)
        tw = len(str(coins)) * 4 - 1
        coin(W - 1 - tw - 7, 7)
        s.tiny(W - 1 - tw, 7, str(coins), WHITE)
        frames.append(page(led(s.im), 'COURSE', 'Pieces, blocs, rochers qui roulent : le petit bonhomme court tout seul.',
                           ['Commandes :', 'X ou haut : sauter', 'garder appuye : plus haut', '', 'Regles :',
                            'piece : 10 (30 = 1 vie)', 'bloc dore : coup de tete', '  = une piece', 'rocher : sauter ou',
                            '  ecraser (200)', 'trou ou piques : -1 vie', 'stage : plus vite, +1 vie']))
    save(frames, 'jeu-course.gif')


# ================================================================ RAQUETTES
def raquettes():
    rnd = random.Random(6)
    st = stars_init(rnd, 30)
    top, pw = 13, 22
    bx, by, vx, vy = W / 2, 70.0, 0.9, 1.2
    ppx = cpx = W / 2 - pw / 2
    trail, booms, score, won, frames, serve = [], [], 0, 0, [], 20
    for f in range(360):
        c = ppx + pw / 2  # joueur (pilote automatique) et ordinateur suivent la balle
        tx = bx if vy > 0 else W / 2
        ppx = max(0, min(W - pw, ppx + max(-2.4, min(2.4, tx - c))))
        cc = cpx + pw / 2
        tgt = bx if vy < 0 else W / 2
        if abs(tgt - cc) > 3:
            cpx = max(0, min(W - pw, cpx + max(-0.95, min(0.95, tgt - cc))))
        if serve:
            serve -= 1
        else:
            trail = ([(bx, by)] + trail)[:3]
            bx += vx; by += vy
            if bx < 0 or bx > W - 2:
                vx = -vx
            pyy, cy = H - 5, top + 2
            for padx, down, yy in ((ppx, False, pyy), (cpx, True, cy)):
                hit_low = vy > 0 and not down and yy <= by + 2 <= yy + 3
                hit_top = vy < 0 and down and yy - 1 <= by <= yy + 2
                if (hit_low or hit_top) and bx + 2 >= padx and bx <= padx + pw:
                    rel = max(-1, min(1, (bx + 1 - (padx + pw / 2)) / (pw / 2)))
                    sp = min(math.hypot(vx, vy) * 1.06, 3.6)
                    vx = math.sin(rel) * sp
                    vy = (1 if down else -1) * max(0.6, math.cos(rel)) * sp
            if by < top - 2:
                score += 100; won += 1
                booms.append([int(bx), top, 1])
                bx, by, vx, vy, serve = W / 2, 70.0, rnd.choice((-0.8, 0.8)), 1.2, 30
            elif by > H:
                bx, by, vx, vy, serve = W / 2, 70.0, rnd.choice((-0.8, 0.8)), -1.2, 30
        for b in booms:
            b[2] += 1
        booms = [b for b in booms if b[2] < 12]
        if f % 2:
            continue
        s = Screen(W, H)
        stars_draw(s, st, 0.05, rnd)
        d = ImageDraw.Draw(s.im)
        mid = (top + H) // 2
        for x in range(0, W, 6):
            d.line((x, mid, x + 2, mid), fill=(50, 50, 70))
        d.rectangle((int(ppx), H - 5, int(ppx) + pw - 1, H - 4), fill=PAL['c'])
        d.rectangle((int(cpx), top + 2, int(cpx) + pw - 1, top + 3), fill=PAL['r'])
        for i, (tx_, ty_) in enumerate(reversed(trail)):
            v = 40 + 30 * i
            d.rectangle((int(tx_), int(ty_), int(tx_) + 1, int(ty_) + 1), fill=(v, v, v))
        if not serve or f & 4:
            d.rectangle((int(bx), int(by), int(bx) + 1, int(by) + 1), fill=PAL['w'])
        s.tiny(2, mid - 6, str(won % 5), (110, 110, 130))
        for b in booms:
            boom(s, b)
        hud(s, score)
        frames.append(page(led(s.im), 'RAQUETTES', 'Ta raquette en bas, l\'ordinateur en haut : la balle accelere a chaque echange.',
                           ['Commandes :', 'gauche / droite : bouger', '', 'Regles :', 'angle selon le point',
                            'd\'impact sur la raquette', 'point gagne : 100 x stage', 'point perdu : -1 vie',
                            '5 points gagnes : stage', 'suivant, ordinateur', 'plus fort, +1 vie']))
    save(frames, 'jeu-raquettes.gif')


# ================================================================ BULLES
def bulles():
    rnd = random.Random(11)
    st = stars_init(rnd, 25)
    blob = re.findall(r'"([.wrybcgpov]{8})"', re.search(r'SPR_BLOB\[2\]\[8\] = \{(.*?)\};', SRC, re.S).group(1))
    BLOB = [blob[0:8], blob[8:16]]  # heros original : blob cyan
    FL = H - 4
    plats = [(0, W, FL), (4, 50, FL - 22), (78, W - 4, FL - 22), (40, 88, FL - 44), (4, 30, FL - 66), (98, W - 4, FL - 66),
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
        for p in plats:
            dw.rectangle((p[0], p[2], p[1] - 1, p[2] + 2), fill=(200, 80, 160))
            for x in range(p[0] + 3, p[1], 6):
                s.px(x, p[2] + 1, (66, 26, 53))
        dw.rectangle((0, 12, 2, H - 1), fill=(200, 80, 160))
        dw.rectangle((W - 3, 12, W - 1, H - 1), fill=(200, 80, 160))
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
        s = Screen(W, H)
        d = ImageDraw.Draw(s.im)
        for r in range(R):
            for c in range(C):
                x, y = OX + c * T, OY + r * T
                if maze[r][c] == '#':
                    d.rectangle((x, y, x + T - 1, y + T - 1), fill=(40, 60, 200))
                    d.rectangle((x + 1, y + 1, x + T - 2, y + T - 2), fill=(10, 15, 50))
                elif dots.get((r, c)) == 1:
                    d.rectangle((x + 2, y + 2, x + 3, y + 3), fill=(230, 200, 140))
                elif dots.get((r, c)) == 2 and f & 8:
                    d.rectangle((x + 1, y + 1, x + 4, y + 4), fill=PAL['w'])
        d.line((OX + 10 * T, OY + 8 * T + 3, OX + 11 * T - 1, OY + 8 * T + 3), fill=PAL['o'])
        spike = (0x15, 0x0E, 0x1F, 0x0E, 0x15)
        cols = ((170, 70, 255), (255, 140, 0), (170, 230, 40), (230, 230, 230))
        for i, e in enumerate(en):
            col = ((40, 90, 255) if not (fright < 60 and f & 4) else PAL['w']) if fright and not e['wait'] else cols[i]
            x, y = int(e['x']) - 2, int(e['y']) - 2
            for rr in range(5):
                for cc in range(5):
                    if spike[rr] & (0x10 >> cc):
                        s.px(x + cc, y + rr, col)
            if not fright or e['wait']:
                s.px(x + 1, y + 2, PAL['w']); s.px(x + 3, y + 2, PAL['w'])
        x, y = int(p['x']) - 2, int(p['y']) - 2  # scarabee vert
        g, dk = PAL['g'], (10, 90, 30)
        d.rectangle((x + 1, y, x + 3, y + 4), fill=g)
        d.rectangle((x, y + 1, x + 4, y + 3), fill=g)
        d.line((x + 2, y + 1, x + 2, y + 3), fill=dk)
        ex, ey = x + 2 + p['dx'] * 2, y + 2 + p['dy'] * 2
        if p['dx']:
            s.px(ex, ey - 1, PAL['w']); s.px(ex, ey + 1, PAL['w'])
        else:
            s.px(ex - 1, ey, PAL['w']); s.px(ex + 1, ey, PAL['w'])
        legs = ((x - 1, y + 1), (x + 5, y + 3)) if (f // 4) & 1 else ((x - 1, y + 3), (x + 5, y + 1))
        for lx, ly in legs:
            s.px(lx, ly, dk)
        for b in booms:
            boom(s, b)
        hud(s, score)
        frames.append(page(led(s.im), 'LABYRINTHE', 'Un scarabee ramasse les graines ; 4 creatures piquantes le poursuivent.',
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
def c_rows(name, n):  # sprites d'ORBITE lus dans games.ino
    return re.findall(r'"([.wrybcgpov]{%d})"' % n, re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\};', SRC, re.S).group(1))


_o = c_rows('O_SPR[3][2][7]', 7)
O_SPR = [[_o[(t * 2 + k) * 7:(t * 2 + k + 1) * 7] for k in range(2)] for t in range(3)]
_t = c_rows('O_SAT[2][7]', 7)
O_SAT = [_t[:7], _t[7:]]
_s = c_rows('O_SHIP[2][11]', 11)
O_SHIP = [_s[:11], _s[11:]]


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
    rings = 2 if H >= 128 else 1
    N = 32 if rings == 2 else 20
    per, wave = N // rings, N // 4
    acc = [0]
    for k in range(1, 65):
        a = (k - 0.5) * TAU / 64
        acc.append(acc[-1] + math.hypot(rx * math.sin(a), ry * math.cos(a)))

    def slot_angle(i):  # places a egale distance le long de l'ellipse (comme oNewStage)
        ring, slot = (i & 1, i // 2) if rings == 2 else (0, i)
        target = (slot + 0.5 * ring + 0.25) / per * acc[64]
        k = 1
        while k < 64 and acc[k] < target:
            k += 1
        return (k - 1 + (target - acc[k - 1]) / max(0.001, acc[k] - acc[k - 1])) * TAU / 64

    def ring_r(i):
        return 0.62 if rings == 1 else 0.44 + (i & 1) * 0.12
    en = []
    for i in range(N):
        w, k = i // wave, i % wave
        en.append({'on': True, 'state': 0, 't': -(w * 110 + k * 9), 'a0': w * math.pi / 2 + 0.7, 'dir': -1 if w & 1 else 1,
                   'type': (w + 1) % 3, 'carrier': i == wave + wave // 2, 'r': 0, 'a': 0, 'sa': slot_angle(i), 'shot': False, 'i': i})
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
                cool = 16  # apercu : le pilote tire moins vite, pour qu'on voie l'anneau se former
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
                fr = ring_r(e['i'])
                if e['state'] == 1:
                    u = min(1, e['t'] / 100)
                    e['r'] = fr * u + 0.45 * math.sin(math.pi * u)
                    a_s, wgt = e['a0'] + e['dir'] * 4.4 * u, max(0, (u - 0.55) / 0.45)
                    e['a'] = a_s + adiff(e['sa'] + rot, a_s) * wgt * wgt
                    if u >= 1:
                        e['state'] = 2
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
            fr = (f // 8) & 1
            if e['r'] < 0.1:
                s.px(x, y, c)
            elif e['r'] < 0.2:
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    s.px(x + dx, y + dy, c)
                s.px(x, y, PAL['w'])
            elif e['r'] < 0.36:
                sp = SPR[e['type']][fr]
                for r in range(5):
                    for cc in range(5):
                        if sp[r] & (0x10 >> cc):
                            s.px(x - 2 + cc, y - 2 + r, c)
            else:
                rows = O_SAT[fr] if e['carrier'] else O_SPR[e['type']][fr]
                for r, line in enumerate(rows):
                    for cc, ch in enumerate(line):
                        if ch != '.':
                            s.px(x - 3 + cc, y - 3 + r, PAL[ch])
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
        px, py = X(pa, 1), Y(pa, 1)
        q = round((math.atan2(cy - py, cx - px) + math.pi / 2) / (math.pi / 4)) & 7
        spr, rot, ix, iy = O_SHIP[q & 1], q >> 1, round(px), round(py)
        for dy in range(-5, 6):
            for dx in range(-5, 6):
                sx, sy = dx, dy
                for _ in range(rot):
                    sx, sy = sy, -sx
                ch = spr[sy + 5][sx + 5]
                if ch == '.':
                    continue
                if ch == 'o' and (f // 3) & 1:
                    ch = 'y'
                if ch == 'b' and power:
                    ch = 'g'
                if iy + dy >= 8:
                    s.px(ix + dx, iy + dy, PAL[ch])
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


todo = GIF_ARGS or ['raid', 'course', 'raquettes', 'bulles', 'labyrinthe', 'sauterelle', 'orbite']
for name in todo:
    globals()[name]()
