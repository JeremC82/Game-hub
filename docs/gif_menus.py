"""GIF animes pour le README : reglage du volume, reglage de la luminosite, mode demo.

Sorties : docs/reglage-volume.gif, docs/reglage-luminosite.gif, docs/mode-demo.gif
Apercus simules (ecran 128x128, 2 dalles), memes sprites, police et mise en page que games.ino.
"""
import math
import os
import random

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)  # sprites, police, palette (sans generer les images)
Screen, SPR_W, SPR_SHIP, SPR_LIFE, PAL, font, GLCD = (ns['Screen'], ns['SPR_W'], ns['SPR_SHIP'], ns['SPR_LIFE'], ns['PAL'],
                                                      ns['font'], ns['GLCD_BYTES'])
W = H = 128
SEL, OFF, DIM, WHITE = (255, 220, 0), (0, 160, 200), (110, 110, 130), (230, 230, 230)
P = 4  # pas des LED dans le GIF


def text_big(s, x, y, t, col):  # police 6x8 en taille 2
    for ch in t:
        for i in range(5):
            line = GLCD[ord(ch) * 5 + i]
            for j in range(8):
                if line & (1 << j):
                    for a in range(2):
                        for b in range(2):
                            s.px(x + i * 2 + a, y + j * 2 + b, col)
        x += 12


def led(img, gain=1.0):
    """Image 128x128 -> LED (pas P) ; gain = luminosite des dalles."""
    out = Image.new('RGB', (W * P + 8, H * P + 8), (10, 10, 14))
    d = ImageDraw.Draw(out)
    for y in range(H):
        for x in range(W):
            c = img.getpixel((x, y))
            if sum(c) < 12:
                c = (22, 22, 28)
            else:
                c = tuple(int(v * gain) for v in c)
            d.rectangle((4 + x * P, 4 + y * P, 4 + x * P + P - 2, 4 + y * P + P - 2), fill=c)
    d.line((4, 4 + 64 * P - 1, 4 + W * P, 4 + 64 * P - 1), fill=(55, 55, 70), width=2)
    return out


STARS = [(random.Random(7).randrange(W), random.Random(i).randrange(H), 0.2 + (i % 9) / 10) for i in range(74)]


def menu_screen(f, sel, bright, vol):
    """Ecran du menu comme menuScreen() : etoiles, titre, record, cadre et 6 lignes."""
    s = Screen(W, H)
    rnd = random.Random(3)
    for i in range(74):
        x, y0, v = rnd.randrange(W), rnd.randrange(H), 0.2 + rnd.random() * 0.8
        g = int(70 + v * 120)
        s.px(x, int(y0 + f * v * 0.5) % H, (g, g, g))
    text_big(s, (W - 96) // 2, 8, 'GAME HUB', SEL)
    s.text(26, 'RECORD 30800 JER', (255, 40, 40))
    y, step = H // 2 - 22, 11
    ImageDraw.Draw(s.im).rectangle((4, y - 6, 123, y + 6 * step + 15), fill=(0, 0, 0), outline=(40, 70, 170))
    for i in range(6):
        if i == 1:
            t = 'NIVEAU ' + ('<NORMAL>' if sel == 1 else 'NORMAL')
        elif i == 2:
            t = 'LUMINOSITE ' + (f'<{bright}%>' if sel == 2 else f'{bright}%')
        elif i == 3:
            t = 'VOLUME ' + (f'<{vol}%>' if sel == 3 else f'{vol}%')
        else:
            t = ('JOUER', '', '', '', 'SCORES', 'DEMO')[i]
        s.text(y + i * step, ('> ' if i == sel else '') + t, SEL if i == sel else OFF)
    s.text(y + 6 * step + 4, 'OPTIONS : JOUER', DIM)
    return s.im


def dpad(d, cx, cy, press):
    """Croix de la manette ; press = 'H', 'B', 'G', 'D' ou ''."""
    for k, (dx, dy) in zip('HBGD', ((0, -1), (0, 1), (-1, 0), (1, 0))):
        x, y = cx + dx * 34, cy + dy * 34
        on = k == press
        d.rounded_rectangle((x - 16, y - 16, x + 16, y + 16), 5, fill=(240, 220, 90) if on else (70, 72, 85))
        tri = {'H': [(x, y - 8), (x - 8, y + 6), (x + 8, y + 6)], 'B': [(x, y + 8), (x - 8, y - 6), (x + 8, y - 6)],
               'G': [(x - 8, y), (x + 6, y - 8), (x + 6, y + 8)], 'D': [(x + 8, y), (x - 6, y - 8), (x - 6, y + 8)]}[k]
        d.polygon(tri, fill=(30, 30, 30) if on else (150, 150, 165))
    d.rectangle((cx - 16, cy - 16, cx + 16, cy + 16), fill=(70, 72, 85))


def frame(panel, title, sub, side):
    """Planche : titre, ecran LED a gauche, illustration a droite (fonction side(d, x0, y0))."""
    im = Image.new('RGB', (panel.width + 360, panel.height + 110), (16, 16, 22))
    d = ImageDraw.Draw(im)
    d.text((20, 16), title, fill=(235, 235, 245), font=font(24, True))
    d.text((20, 50), sub, fill=(150, 150, 175), font=font(16))
    im.paste(panel, (20, 90))
    side(d, panel.width + 40, 90)
    return im


def save(frames, durations, name):
    pal = frames[len(frames) // 2].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    path = os.path.join(DOC, name)
    q[0].save(path, save_all=True, append_images=q[1:], duration=durations, loop=0, optimize=True)
    print(path, len(frames), 'images', os.path.getsize(path) // 1024, 'Ko')


# ================================================================ reglage : volume ou luminosite
def reglage(kind):
    line = 3 if kind == 'volume' else 2
    seq = [('', 0, 60, 60)] * 3  # (touche, ligne choisie, luminosite, volume)
    sel = 0
    for _ in range(line):  # descendre jusqu'a la ligne
        sel += 1
        seq += [('B', sel, 60, 60), ('', sel, 60, 60), ('', sel, 60, 60)]
    val = 60
    path_vals = [70, 80, 90, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0, 10, 20, 30, 40, 50, 60]
    for v in path_vals:
        key = 'D' if v > val else 'G'
        val = v
        b = val if kind == 'luminosite' else 60
        vo = val if kind == 'volume' else 60
        seq += [(key, sel, b, vo), ('', sel, b, vo)] + ([('', sel, b, vo)] * 3 if v in (100, 0) else [])
    seq += [('', sel, 60, 60)] * 4
    frames, durs = [], []
    for f, (key, s_, b, vo) in enumerate(seq):
        img = menu_screen(f, s_, b, vo)
        gain = (b if b > 0 else 1.2) / 100 if kind == 'luminosite' else 0.6 + 0.4 * (60 / 100)
        if kind == 'volume':
            gain = 0.85
        panel = led(img, max(gain, 0.012) if kind == 'luminosite' else gain)
        cur = b if kind == 'luminosite' else vo

        def side(d, x0, y0, key=key, cur=cur):
            dpad(d, x0 + 150, y0 + 110, key)
            d.text((x0 + 150, y0 + 175), 'croix de la manette', fill=(150, 150, 175), font=font(15), anchor='mt')
            cx, cy = x0 + 110, y0 + 330
            if kind == 'volume':  # haut-parleur et ondes selon le volume
                d.rectangle((cx - 40, cy - 22, cx - 18, cy + 22), fill=(200, 200, 215))
                d.polygon([(cx - 18, cy - 22), (cx + 12, cy - 50), (cx + 12, cy + 50), (cx - 18, cy + 22)], fill=(170, 170, 190))
                for k in range(cur // 20 + (1 if cur % 20 else 0)):
                    r = 30 + k * 18
                    d.arc((cx + 12 - r, cy - r, cx + 12 + r, cy + r), -40, 40, fill=(0, 220, 255), width=5)
                if cur == 0:
                    d.line((cx + 30, cy - 25, cx + 80, cy + 25), fill=(255, 60, 60), width=7)
                    d.line((cx + 30, cy + 25, cx + 80, cy - 25), fill=(255, 60, 60), width=7)
            else:  # soleil plus ou moins lumineux
                g = 0.1 + 0.9 * cur / 100
                col = (int(255 * g), int(220 * g), int(60 * g))
                d.ellipse((cx - 26, cy - 26, cx + 26, cy + 26), fill=col)
                for k in range(12):
                    a = k * math.pi / 6
                    d.line((cx + 36 * math.cos(a), cy + 36 * math.sin(a), cx + (40 + 30 * g) * math.cos(a),
                            cy + (40 + 30 * g) * math.sin(a)), fill=col, width=5)
            d.rounded_rectangle((x0 + 30, y0 + 410, x0 + 290, y0 + 450), 8, fill=(40, 40, 60))
            d.rectangle((x0 + 34, y0 + 414, x0 + 34 + int(252 * cur / 100), y0 + 446), fill=(0, 220, 255) if kind == 'volume' else (255, 220, 0))
            d.text((x0 + 160, y0 + 475), f'{cur} %', fill=(235, 235, 245), font=font(26, True), anchor='mt')

        title = 'Reglage du volume' if kind == 'volume' else 'Reglage de la luminosite'
        sub = ('Menu, ligne VOLUME : croix gauche / droite, 0 a 100 % (0 % = muet). Un bip fait entendre le niveau.'
               if kind == 'volume' else 'Menu, ligne LUMINOSITE : croix gauche / droite, 0 a 100 % (0 % = tres faible, jamais noir).')
        frames.append(frame(panel, title, sub, side))
        durs.append(1200 if f in (2, len(seq) - 1) else 260)
    save(frames, durs, 'reglage-volume.gif' if kind == 'volume' else 'reglage-luminosite.gif')


# ================================================================ mode demo (jeu simule, pilote automatique)
def mode_demo():
    rnd = random.Random(5)
    cols, types = 11, [0, 1, 1, 2, 2]
    en = [{'r': r, 'c': c, 't': t, 'st': 0, 'x': 0.0, 'y': 0.0, 'tt': 0, 'dir': 1, 'alive': True}
          for r, t in enumerate(types) for c in range(cols) if not (t == 0 and c % 2)]
    ship_x, shots, booms, bombs = 58.0, [], [], []
    stars = [[rnd.randrange(W), rnd.uniform(12, H), rnd.uniform(.2, 1)] for _ in range(70)]
    score, frames, durs = 0, [], []
    for f in range(240):
        sway = math.sin(f * 0.03) * 6
        for st in stars:
            st[1] += st[2] * 0.6
            if st[1] >= H:
                st[1], st[0] = 12, rnd.randrange(W)
        if f % 45 == 20:  # un ennemi pique
            cand = [e for e in en if e['alive'] and e['st'] == 0]
            if cand:
                e = rnd.choice(cand)
                e['st'], e['tt'], e['dir'] = 1, 0, -1 if e['x'] < W / 2 else 1
        for e in en:
            if not e['alive']:
                continue
            sx, sy = (W - cols * 11) // 2 + 1 + e['c'] * 11 + sway, 13 + e['r'] * 9
            if e['st'] == 0:
                e['x'], e['y'] = sx, sy
            elif e['st'] == 1:  # pique en boucle puis descente
                e['tt'] += 1
                t = e['tt']
                e['x'] += e['dir'] * 1.4 * math.cos(t * 0.07) + (ship_x - e['x']) * (0.004 if t > 45 else 0)
                e['y'] += min(0.4 + t * 0.03, 1.6) - (1.2 if t < 12 else 0)
                if e['y'] > H:
                    e['y'], e['st'] = -8, 2
            else:  # retour a sa place
                e['x'] += max(-2, min(2, (sx - e['x']) * 0.1))
                e['y'] += max(-2, min(2, (sy - e['y']) * 0.1))
                if abs(sx - e['x']) < 1 and abs(sy - e['y']) < 1:
                    e['st'] = 0
            if e['st'] == 1 and rnd.random() < 0.02:
                bombs.append([e['x'] + 4, e['y'] + 7])
        # pilote automatique : vise l'ennemi le plus proche, esquive les tirs
        tgt = min((e for e in en if e['alive'] and e['y'] > 0), key=lambda e: abs(e['x'] - ship_x) - (40 if e['st'] == 1 else 0),
                  default=None)
        goal = tgt['x'] if tgt else ship_x
        for b in bombs:
            if b[1] > 90 and abs(b[0] - ship_x - 4) < 6:
                goal = ship_x + (7 if b[0] < ship_x + 4 else -7)
        ship_x += max(-1.5, min(1.5, goal - ship_x))
        if f % 8 == 0 and tgt and abs(tgt['x'] - ship_x) < 5:
            shots.append([ship_x + 4, 112.0])
        for sh in shots:
            sh[1] -= 3
            for e in en:
                if e['alive'] and e['x'] <= sh[0] <= e['x'] + 8 and e['y'] <= sh[1] <= e['y'] + 6:
                    e['alive'], sh[1] = False, -10
                    booms.append([int(e['x'] + 4), int(e['y'] + 3), 1])
                    score += 160 if e['st'] == 1 else 80
        shots = [sh for sh in shots if sh[1] > 0]
        for b in bombs:
            b[1] += 1.4
        bombs = [b for b in bombs if b[1] < H]
        for bm in booms:
            bm[2] += 1
        booms = [bm for bm in booms if bm[2] < 12]
        if not any(e['alive'] for e in en):  # vague detruite : elle revient
            for e in en:
                e['alive'], e['st'], e['y'] = True, 2, -10
        if f % 2:
            continue
        s = Screen(W, H)
        for k, (x, y, v) in enumerate(stars):
            g = int(70 + v * 120)
            s.px(x, int(y), (g, g, g))
        for e in en:
            if e['alive']:
                fr = (f // 5) & 1 if e['st'] == 1 else ((f // 16) + e['c']) & 1
                s.sprite(SPR_W[0][e['t']][fr], int(e['x']), int(e['y']))
        for sh in shots:
            for k in range(3):
                s.px(int(sh[0]), int(sh[1]) + k, (PAL['w'], PAL['y'], PAL['o'])[k])
        for b in bombs:
            for k in range(3):
                s.px(int(b[0]), int(b[1]) + k, PAL['r'] if k < 2 else PAL['w'])
        for bx, by, t in booms:
            for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):
                s.px(bx + dx * t // 2, by + dy * t // 2, PAL['w'] if t < 4 else PAL['y'] if t < 7 else PAL['r'])
        s.sprite(SPR_SHIP[0], int(ship_x), H - 10)
        ns['hud_top'](s, W, score, 3)
        if (f // 40) & 1:
            s.text(H // 2 + 6, 'DEMO', SEL)
        else:
            s.text(H // 2 + 6, 'APPUIE : MENU', (0, 220, 255))
        panel = led(s.im, 0.9)

        def side(d, x0, y0):
            for k, t in enumerate(('Le jeu tourne tout seul :', 'pilote automatique qui', 'tire et esquive, ennemis',
                                   'qui piquent, explosions.', '', 'Quand ?', '- 30 s sans toucher', '  la manette au menu',
                                   '- ou ligne DEMO du menu', '- ou sans manette', '', 'Un appui sur un bouton', 'ramene le menu.')):
                d.text((x0 + 10, y0 + 20 + k * 30), t, fill=(255, 220, 0) if t in ('Quand ?',) else (200, 200, 215),
                       font=font(19, t == 'Quand ?'))

        frames.append(frame(panel, 'Mode demo', 'Sans joueur, le jeu se joue tout seul, muet. Le menu revient au premier appui.', side))
        durs.append(80)
    save(frames, durs, 'mode-demo.gif')


reglage('volume')
reglage('luminosite')
mode_demo()
