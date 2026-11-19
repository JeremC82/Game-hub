"""TURBO ROUTE : apercus du jeu de voiture, avec le meme dessin que le firmware (route, bas-cotes, decor, circulation).

  python docs/gif_turbo.py   -> docs/jeu-turbo.gif (dalle 128x64) et build/render/turbo-*.png (4 decors, 128x128)
"""
import math
import os
import random
import re
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5, polices)

SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()


def arr(name):
    body = re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\};', SRC, re.S).group(1)
    return re.findall(r'"([^"]*)"', body)


CAR, TRUCK, CONE, OIL, COIN, CAN = (arr(n) for n in ('V_SPR_CAR[11]', 'V_SPR_TRUCK[17]', 'V_SPR_CONE[5]', 'V_SPR_OIL[5]', 'V_SPR_COIN[5]', 'V_SPR_CAN[6]'))
SPR = [CAR, TRUCK, CONE, OIL, COIN, CAN]
V_W, V_H = (7, 7, 5, 7, 5, 5), (11, 17, 5, 5, 5, 6)
V_COL = [(40, 110, 255), (255, 210, 0), (40, 200, 80), (230, 230, 235), (170, 70, 230), (255, 130, 0), (0, 200, 220), (150, 150, 160)]
TH = [((40, 140, 50), (25, 100, 35), (70, 72, 80)), ((205, 170, 95), (170, 130, 70), (84, 78, 74)),
      ((215, 225, 240), (160, 175, 205), (78, 80, 92)), ((12, 38, 22), (8, 26, 14), (30, 32, 42))]
V_TOP = 10


class Scene:
    def __init__(self, w, h, theme):
        self.W, self.H, self.th = w, h, theme
        self.s = 2 if (w >= 96 and h >= 128) else 1  # double taille sur un grand ecran
        self.lanes = 4 if (w >= 96 and self.s == 1) else 3
        self.lw = 24 if self.s == 2 else 18 if w >= 96 else 13
        self.rl = (w - self.lanes * self.lw) // 2

    def lane_x(self, l):
        return self.rl + l * self.lw + self.lw / 2


def spr(px, rows, x, y, body, dark, W, H, S=1):
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            X, Y = x + c * S, y + r * S
            if ch == '.' or not (0 <= X < W and V_TOP <= Y < H):
                continue
            col = {'B': body, 'b': dark, 'k': (18, 18, 22), 'W': (150, 200, 240), 'L': (255, 240, 170), 'R': (255, 30, 20),
                   'S': (190, 195, 205), 'o': (255, 120, 0), 'y': (255, 215, 0)}.get(ch)
            if ch == 'h':
                col = tuple((v + 255) // 2 for v in body)
            if col is None:
                col = (255, 255, 255)
            for a in range(S):
                for b in range(S):
                    if X + a < W and Y + b < H:
                        px[X + a, Y + b] = col


def render(sc, scroll, player_x, cars, decos, frame, nitro=0, nitro_t=0, score=0, lives=3, prog=0.4, cop=None, msg=None):
    W, H, S = sc.W, sc.H, sc.s
    im = Image.new('RGB', (W, H))
    px = im.load()
    grass, tuft, road = TH[sc.th]
    rl, rr = sc.rl, sc.rl + sc.lanes * sc.lw
    for y in range(V_TOP, H):
        for x in range(W):
            px[x, y] = road if rl - 3 <= x < rr + 3 else grass
    off = int(scroll)
    cp = 8 * S
    for y0 in range(V_TOP - cp + off % cp, H, cp):
        for y in range(y0, y0 + cp):
            if V_TOP <= y < H:
                c = (220, 30, 30) if y < y0 + cp // 2 else (240, 240, 240)
                for k in range(3):
                    px[rl - 3 + k, y] = c
                    px[rr + k, y] = c
    line = (200, 200, 120) if sc.th == 3 else (235, 235, 235)
    for l in range(1, sc.lanes):
        x = rl + l * sc.lw
        dp = 12 * S
        for y0 in range(V_TOP - dp + off % dp, H, dp):
            for y in range(max(y0, V_TOP), min(y0 + dp // 2, H)):
                for a in range(S):
                    px[x + a, y] = line
    for (dx, dy, dt) in decos:
        x, y = dx, int(dy)

        def p(a, b, c):
            X, Y = x + (a - x) * S, y + (b - y) * S
            for i in range(S):
                for j in range(S):
                    if V_TOP <= Y + j < H and 0 <= X + i < W:
                        px[X + i, Y + j] = c
        if dt == 0:
            if sc.th == 1:
                g = (40, 150, 60)
                for k in range(6):
                    p(x + 2, y + k, g)
                for a, b in ((0, 2), (0, 3), (1, 3), (4, 1), (4, 2), (3, 2)):
                    p(x + a, y + b, g)
            elif sc.th == 3:
                for k in range(1, 7):
                    p(x + 2, y + k, (110, 110, 120))
                p(x + 1, y, (255, 240, 160)); p(x + 2, y, (255, 255, 220)); p(x + 3, y, (255, 240, 160)); p(x + 2, y - 1, (160, 140, 60))
            else:
                f, f2 = ((30, 90, 60), (240, 245, 255)) if sc.th == 2 else ((30, 120, 40), (60, 170, 60))
                for r in range(5):
                    for c in range(2 - r // 2, 3 + r // 2):
                        p(x + c, y + r, f2 if (r == 0 or c == 2 - r // 2) else f)
                p(x + 2, y + 5, (110, 70, 30)); p(x + 2, y + 6, (110, 70, 30))
        elif dt == 1:
            p(x, y, tuft); p(x + 2, y, tuft); p(x + 1, y + 1, tuft)
        else:
            p(x, y, (240, 240, 240)); p(x, y + 1, (220, 30, 30)); p(x, y + 2, (240, 240, 240)); p(x, y + 3, (150, 150, 150))
    for (cx, cy, t, col, blink) in cars:
        x, y = int(cx) - V_W[t] * S // 2, int(cy)
        cc = V_COL[col]
        body, dark = cc, tuple(v // 2 for v in cc)
        if t == 3:
            body = (70, 40, 110)
        if t == 5:
            body, dark = (40, 110, 255), (20, 55, 150)
        spr(px, SPR[t], x, y, body, dark, W, H, S)
        if blink and frame & 4:
            for yy in (y + S, y + (V_H[t] - 2) * S):
                for a in range(S):
                    for b in range(S):
                        if V_TOP <= yy + b < H:
                            px[x + (6 * S if blink > 0 else 0) + a, yy + b] = (255, 160, 0)
    if cop:  # voiture de police et gyrophare
        cx_, cy_ = int(cop[0]) - 3 * S, int(cop[1])
        spr(px, CAR, cx_, cy_, (235, 235, 240), (25, 25, 30), W, H, S)
        ph = (frame >> 2) & 1
        for k in range(2):
            for a in range(S):
                for b in range(S):
                    yy = cy_ + 5 * S + b
                    if V_TOP <= yy < H:
                        px[cx_ + (1 + k) * S + a, yy] = (255, 30, 30) if ph else (90, 0, 0)
                        px[cx_ + (4 + k) * S + a, yy] = (0, 0, 90) if ph else (40, 90, 255)
    py = H - 14 * S
    x = int(player_x) - 3 * S
    if sc.th == 3:
        for r in range(1, 10 * S + 1):
            for c in range(-r // 3, 7 * S + r // 3):
                if (c + r) & 1 and py - r >= V_TOP and 0 <= x + c < W:
                    px[x + c, py - r] = (90, 85, 40)
    spr(px, CAR, x, py, (230, 30, 30), (120, 10, 10), W, H, S)
    if nitro_t:
        for k in range((2 + (frame & 3)) * S):
            if py + 11 * S + k < H:
                c = (255, 255, 160) if k < 2 * S else (255, 120, 0)
                for a in range(S):
                    px[x + 2 * S + a, py + 11 * S + k] = c
                    px[x + 4 * S + a, py + 11 * S + k] = c
    d = ImageDraw.Draw(im)
    d.line((0, V_TOP - 2, W - 1, V_TOP - 2), fill=(40, 40, 50))
    if prog > 0:
        d.line((0, V_TOP - 2, int(W * prog) - 1, V_TOP - 2), fill=(0, 200, 255))
    for i in range(nitro):
        d.rectangle((1 + i * 4, H - 4, 3 + i * 4, H - 2), fill=(40, 110, 255))
    s = str(score)
    lw, nw = len('SCORE') * 4 - 1, len(s) * 4 - 1
    x0 = (W - (lw + 4 + nw)) // 2
    GB.tiny(d, x0, 1, 'SCORE', (255, 40, 40))
    GB.tiny(d, x0 + lw + 4, 1, s, (230, 230, 230))
    for i in range(lives):
        d.rectangle((W - 4 - i * 4, 2, W - 2 - i * 4, 4), fill=(230, 30, 30))
    if msg:
        tw = len(msg) * 4 - 1
        mx, my = (W - tw) // 2, V_TOP + 3
        d.rectangle((mx - 2, my - 2, mx + tw + 1, my + 6), fill=(0, 0, 0))
        GB.tiny(d, mx, my, msg, (255, 60, 60) if msg.startswith('POLICE !') else (255, 220, 0))
    return im


def scene_frame(sc, f, seed=1):
    """une course scriptee : la voiture double, change de voie, passe une piece ; circulation qui descend"""
    rnd = random.Random(seed)
    S = sc.s
    speed = 2.6 * S
    scroll = f * speed
    lanes = sc.lanes
    cars = []
    for i in range(9):
        lane = rnd.randrange(lanes)
        t = rnd.choice([0, 0, 0, 1, 2, 3, 4, 5])
        vy = (0.9 + rnd.random() * 0.5) if t <= 1 else 0
        y = -30 * S - i * 34 * S + f * (speed - vy * S)
        y = (y + 40) % (sc.H + 60) - 40
        blink = 0
        if t <= 1 and i % 4 == 1:
            blink = 1 if lane < lanes - 1 else -1
        cars.append((sc.lane_x(lane) + (blink * min(6 * S, max(0, y - 20) * 0.1) if blink else 0), y, t, rnd.randrange(7), blink))
    decos = []
    for i in range(12):
        side = i % 2
        room = sc.rl - 3
        x = (sc.rl + sc.lanes * sc.lw + 3 + rnd.randrange(max(1, room - 5 * S))) if side else rnd.randrange(max(1, room - 5 * S))
        decos.append((x, (rnd.randrange(sc.H) + scroll) % (sc.H + 20) - 10, rnd.randrange(3)))
    # ta voiture zigzague entre deux voies
    px_ = sc.lane_x(1) + math.sin(f * 0.07) * sc.lw * 0.9
    return scroll, px_, cars, decos


def page(panel, title, lines, sub):
    im = Image.new('RGB', (panel.width + 360, max(panel.height + 110, 420)), (16, 16, 22))
    d = ImageDraw.Draw(im)
    d.text((20, 16), title, fill=(235, 235, 245), font=GB.font(24, True))
    d.text((20, 50), sub, fill=(150, 150, 175), font=GB.font(16))
    im.paste(panel, (20, 90))
    for k, t in enumerate(lines):
        hdr = t.endswith(':')
        d.text((panel.width + 40, 110 + k * 28), t, fill=(255, 220, 0) if hdr else (200, 200, 215), font=GB.font(18, hdr))
    return im


if __name__ == '__main__':
    out_dir = os.path.join(DOC, '..', 'build', 'render')
    os.makedirs(out_dir, exist_ok=True)
    LINES = ['Commandes :', 'gauche / droite : braquer', 'X, R2 ou haut : accelerer', 'bas : freiner', 'rond : turbo',
             'A eviter :', 'voitures, camions, cones', 'huile, et la police !', 'A ramasser :', 'pieces, bidons de turbo']
    # GIF sur une dalle 128x64
    sc = Scene(128, 64, 0)
    frames = []
    for f in range(0, 160, 2):
        scroll, px_, cars, decos = scene_frame(sc, f)
        cop = (px_ + math.sin(f * 0.05) * 6, sc.H - 14 + max(0, 110 - f) * 0.5) if 70 <= f < 140 else None
        msg = 'POLICE !' if 70 <= f < 100 and f & 8 else 'SEMEE ! +2000' if f >= 140 else None
        im = render(sc, scroll, px_, cars, decos, f, nitro=2, nitro_t=1 if 100 <= f < 130 else 0, score=1240 + f * 3, prog=0.2 + f / 300, cop=cop, msg=msg)
        frames.append(page(GB.leds(im), 'TURBO ROUTE', LINES, 'Jeu de voiture original vu de dessus (128x64).'))
    pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    gif = os.path.join(DOC, 'jeu-turbo.gif')
    q[0].save(gif, save_all=True, append_images=q[1:], duration=80, loop=0, optimize=True)
    # les 4 decors sur un ecran 128x128
    shots = []
    for th, name in enumerate(('campagne', 'desert', 'neige', 'nuit')):
        sc = Scene(128, 128, th)
        scroll, px_, cars, decos = scene_frame(sc, 37 + th * 11, seed=3 + th)
        cop = (px_ + sc.lw, sc.H - 14 * sc.s + 3) if th in (2, 3) else None
        msg = 'POLICE !' if cop else None
        im = GB.leds(render(sc, scroll, px_, cars, decos, 40 + th, nitro=th % 3 + 1, nitro_t=th == 1, score=3000 + th * 2750, prog=0.3 + th * 0.18, cop=cop, msg=msg), P=5)
        pg = Image.new('RGB', (im.width, im.height + 40), (16, 16, 22))
        pg.paste(im, (0, 40))
        ImageDraw.Draw(pg).text((10, 8), 'Stage %d : %s' % (th + 1, name), fill=(255, 220, 0), font=GB.font(22, True))
        pg.save(os.path.join(out_dir, 'turbo-%s.png' % name))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 2, h * 2), (16, 16, 22))
    for i, s in enumerate(shots):
        sheet.paste(s, ((i % 2) * w, (i // 2) * h))
    sheet.save(os.path.join(out_dir, 'turbo-decors.png'))
    print(gif, len(frames), 'images ;', os.path.join(out_dir, 'turbo-decors.png'))
