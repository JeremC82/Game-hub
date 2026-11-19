"""RENARD : apercu de la course sans fin (meme dessin et memes regles que le firmware, demo automatique).

  python docs/gif_renard.py   -> docs/jeu-renard.gif (ecran 128x128) et docs/renard-rendus.png (jour, nuit, etoile)
"""
import math
import os
import random
import re
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5)

SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
body = re.search(r'RU_FOX\[3\]\[10\] = \{(.*?)\};', SRC, re.S).group(1)
rows = re.findall(r'"([^"]*)"', body)
FOX = [rows[0:10], rows[10:20], rows[20:30]]
DUCK = re.findall(r'"([^"]*)"', re.search(r'RU_DUCK\[6\] = \{(.*?)\};', SRC, re.S).group(1))


class Game:
    def __init__(self, W=128, H=128, seed=3):
        self.W, self.H = W, H
        self.s = 2 if H >= 128 else 1
        self.gy = H - 12 * self.s
        self.y = self.vy = 0.0
        self.spd, self.dist, self.gap, self.scroll = 2.0, 0.0, 60.0, 0.0
        self.obs = []
        self.score, self.hi, self.wave, self.lives = 0, 4200, 1, 3
        self.star = self.inv = 0
        self.duck = False
        self.hold = False
        self.frame = 0
        self.rnd = random.Random(seed)

    def W_(self, o):
        return {0: 6, 2: 11, 3: 9}.get(o['t'], 5 * o['h'] if o['t'] == 1 else 5)

    def H_(self, o):
        return 9 if o['t'] == 1 else 5

    def OY(self, o):
        if o['t'] == 3:
            return (1, 6, 16)[o['h']]
        if o['t'] >= 4:
            return 14 + o['h']
        return 0

    def spawn(self):
        r, lvl = self.rnd.randrange(100), min(self.wave, 8)
        o = {'x': self.W + 4.0, 'h': 0}
        if r < 6:
            o['t'], o['h'] = 5, self.rnd.randrange(4)
        elif r < 22:
            o['t'], o['h'] = 4, self.rnd.randrange(6)
        elif r < 32 + 3 * lvl and self.wave >= 2:
            o['t'], o['h'] = 3, self.rnd.randrange(3)
        elif r < 70:
            o['t'], o['h'] = 1, 1 + self.rnd.randrange(min(3, 1 + self.wave // 2))
        elif r < 85:
            o['t'] = 0
        else:
            o['t'] = 2
        self.obs.append(o)

    def auto(self):
        k = 0
        fx = 18 * self.s
        for o in self.obs:
            if o['t'] >= 4:
                continue
            d = o['x'] - fx
            if d < -4 or d > self.spd * 14 * self.s:
                continue
            if o['t'] == 3 and o['h'] == 1:
                k |= 16
            elif o['t'] == 3 and o['h'] == 2:
                continue
            elif d < self.spd * 9 * self.s:
                k |= 4
        return k

    def step(self):
        s = self.s
        self.frame += 1
        self.star = max(0, self.star - 1)
        self.inv = max(0, self.inv - 1)
        k = self.auto()
        jump, down = bool(k & 4), bool(k & 16)
        g = .32
        on = self.y <= .01
        if jump and on:
            self.vy, self.hold = 3.0, True
        if not jump:
            self.hold = False
        if not on:
            self.vy -= g * .6 if (self.hold and self.vy > 0) else g
        if down and not on:
            self.vy -= .6
        self.y = max(0.0, self.y + self.vy)
        if self.y <= 0:
            self.vy = 0.0
        self.duck = down and self.y <= 0
        self.spd = min(5.5, self.spd + .0009 * 6)  # (apercu : 6 fois plus vite)
        self.scroll += self.spd * s
        self.dist += self.spd
        while self.dist >= 6:
            self.dist -= 6
            self.score += 4
            if self.score % 1000 < 4:
                self.wave += 1
        self.gap -= self.spd
        if self.gap <= 0:
            self.spawn()
            self.gap = max(30, 70 - self.spd * 6) + self.rnd.randrange(50)
        fx, fw, fh = 6 * s, (13 if self.duck else 9) * s, (5 if self.duck else 9) * s
        fb = self.gy - int(self.y * s)
        ft = fb - fh
        for o in self.obs:
            o['x'] -= self.spd * s
            ow, oh = self.W_(o) * s, self.H_(o) * s
            ob = self.gy - self.OY(o) * s
            ot = ob - oh
            if o['x'] + s < fx + fw and o['x'] + ow - s > fx + 2 * s and ot + s < fb and ob - s > ft:
                if o['t'] == 4:
                    o['x'] = -99
                    self.score += 100
                elif o['t'] == 5:
                    o['x'] = -99
                    self.star = 200
                elif self.star:
                    o['x'] = -99
                elif not self.inv:
                    self.inv = 90
        self.obs = [o for o in self.obs if o['x'] > -40]


def render(gm, night=None):
    W, H, s, gy = gm.W, gm.H, gm.s, gm.gy
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    night = (gm.score // 1000) & 1 if night is None else night
    white, orange, dorange = (255, 255, 255), (255, 140, 0), (200, 80, 0)
    sky = (0, 0, 0) if night else (0, 150, 255)
    hill, hill2 = ((0, 0, 150), (60, 0, 160)) if night else ((0, 150, 90), (40, 190, 60))
    ground, dirt = ((150, 100, 0), (150, 60, 0)) if night else ((220, 170, 60), (180, 110, 30))
    d.rectangle((0, 0, W - 1, gy - 1), fill=sky)
    sx = W - 18 * s // 2 - 6
    if night:
        d.ellipse((sx - 4 * s, 16 - 4 * s, sx + 4 * s, 16 + 4 * s), fill=(255, 240, 180))
        d.ellipse((sx + 2 - 3 * s, 15 - 3 * s, sx + 2 + 3 * s, 15 + 3 * s), fill=(0, 0, 0))
        for i in range(18):
            x, y = (i * 53 + int(gm.scroll * .05)) % W, 9 + (i * 37) % max(10, gy // 2)
            if ((gm.frame >> 3) + i) % 5:
                d.point((W - 1 - x, y), fill=white)
    else:
        d.ellipse((sx - 4 * s, 16 - 4 * s, sx + 4 * s, 16 + 4 * s), fill=(255, 230, 0))
        for i in range(4):
            x, y = W - 1 - (int(gm.scroll * .2) + i * 47) % (W + 30) + 15, 12 + (i * 13) % 20
            for ox, rr, oy in ((0, 3, 0), (4, 2, 1), (-4, 2, 1)):
                d.ellipse((x + ox * s - rr * s, y + oy - rr * s, x + ox * s + rr * s, y + oy + rr * s), fill=white)
    for x in range(W):
        wx = x + gm.scroll * .15
        h = int((8 + 5 * math.sin(wx * .045) + 3 * math.sin(wx * .11)) * s)
        d.line((x, gy - h, x, gy - 1), fill=hill)
        wx2 = x + gm.scroll * .35
        h2 = int((4 + 3 * math.sin(wx2 * .07 + 1) + 2 * math.sin(wx2 * .19)) * s)
        d.line((x, gy - h2, x, gy - 1), fill=hill2)
    d.rectangle((0, gy, W - 1, H - 1), fill=dirt)
    d.line((0, gy, W - 1, gy), fill=ground)
    d.line((0, gy + 1, W - 1, gy + 1), fill=ground)
    for i in range(24):
        x, y = W - 1 - (int(gm.scroll) + i * 29) % (W + 8), gy + 3 + (i * 7) % max(2, H - gy - 4)
        d.rectangle((x, y, x + s - 1, y + s - 1), fill=ground if i & 1 else (100, 40, 0))

    def spr(rows_, w, h, x, y, flash):
        for r in range(h):
            for q in range(w):
                ch = rows_[r][q] if q < len(rows_[r]) else '.'
                if ch == '.':
                    continue
                c = {'o': orange, 'd': dorange, 'w': white}.get(ch, (0, 0, 0))
                if flash:
                    c = (0, 0, 0) if ch == 'k' else (255, 255, 160)
                d.rectangle((x + q * s, y + r * s, x + q * s + s - 1, y + r * s + s - 1), fill=c)
    for o in gm.obs:
        x = int(o['x'])
        bot = gy - gm.OY(o) * s
        top = bot - gm.H_(o) * s
        t = o['t']
        if t == 0:
            d.rounded_rectangle((x, top + s, x + 6 * s - 1, top + 5 * s - 1), radius=s, fill=(150, 150, 160))
            d.rectangle((x + s, top, x + 4 * s - 1, top + 2 * s - 1), fill=(150, 150, 160))
            d.line((x + s, top, x + 4 * s - 1, top), fill=(220, 220, 230))
            d.rectangle((x + 4 * s, top + 3 * s, x + 6 * s - 1, top + 5 * s - 1), fill=(0, 0, 0))
        elif t == 1:
            for b in range(o['h']):
                bx = x + b * 5 * s
                g_ = (0, 150, 0)
                d.rectangle((bx + s, top + 2 * s, bx + 4 * s - 1, top + 9 * s - 1), fill=g_)
                d.rectangle((bx, top + 4 * s, bx + s - 1, top + 7 * s - 1), fill=g_)
                d.rectangle((bx + 4 * s, top + 3 * s, bx + 5 * s - 1, top + 6 * s - 1), fill=g_)
                d.rectangle((bx + 2 * s, top + s, bx + 3 * s - 1, top + 2 * s - 1), fill=(40, 220, 80))
                for sp in range(3):
                    d.point((bx + 4 * s + (s > 1), top + (3 + sp * 2) * s), fill=white)
                d.point((bx + s, top + 2 * s), fill=(255, 40, 40))
        elif t == 2:
            d.rectangle((x, top, x + 11 * s - 1, top + 5 * s - 1), fill=(150, 90, 30))
            d.line((x, top, x + 11 * s - 1, top), fill=(220, 170, 60))
            cx, cy = x + 9 * s, top + 2 * s + s // 2
            d.ellipse((cx - 2 * s, cy - 2 * s, cx + 2 * s, cy + 2 * s), fill=(220, 170, 60), outline=(150, 90, 30))
        elif t == 3:
            up = (gm.frame >> 2) & 1
            bc = (170, 60, 255)
            d.rectangle((x + 3 * s, top + s, x + 6 * s - 1, top + 4 * s - 1), fill=bc)
            d.polygon([(x, top if up else top + 4 * s), (x + 3 * s, top + s), (x + 3 * s, top + 3 * s)], fill=bc)
            d.polygon([(x + 9 * s, top if up else top + 4 * s), (x + 6 * s, top + s), (x + 6 * s, top + 3 * s)], fill=bc)
            d.point((x + 4 * s, top + 2 * s), fill=(255, 220, 0))
            d.point((x + 5 * s, top + 2 * s), fill=(255, 220, 0))
        elif t == 4:
            cx, cy = x + 2 * s, top + 2 * s
            d.polygon([(cx, cy - 2 * s), (cx - 2 * s, cy), (cx + 2 * s, cy)], fill=(0, 220, 255))
            d.polygon([(cx, cy + 2 * s), (cx - 2 * s, cy), (cx + 2 * s, cy)], fill=(0, 150, 255))
        else:
            cx, cy = x + 2 * s, top + 2 * s
            yc = (255, 255, 160) if gm.frame & 4 else (255, 220, 0)
            d.polygon([(cx, cy - 3 * s), (cx - 2 * s, cy + 2 * s), (cx + 2 * s, cy + 2 * s)], fill=yc)
            d.polygon([(cx - 3 * s, cy - s), (cx + 3 * s, cy - s), (cx, cy + s)], fill=yc)
    if not (gm.inv and gm.frame & 4):
        fx, bot = 6 * s, gy - int(gm.y * s)
        shine = gm.star and (gm.frame >> 1) & 1
        if gm.duck:
            spr(DUCK, 15, 6, fx, bot - 6 * s, shine)
        else:
            spr(FOX[2 if gm.y > 0 else (gm.frame >> 2) & 1], 12, 10, fx, bot - 10 * s + s, shine)
    d.rectangle((0, 0, W - 1, 6), fill=sky)
    b = '%05d' % gm.score
    GB.tiny(d, W - 2 - (len(b) * 4 - 1), 1, b, white if night else (0, 0, 0))
    GB.tiny(d, 2, 1, 'HI %05d' % gm.hi, (150, 150, 160) if night else (0, 0, 150))
    for i in range(gm.lives):
        d.rectangle((W // 2 - 8 + i * 4, 2, W // 2 - 6 + i * 4, 4), fill=orange)
    return im


if __name__ == '__main__':
    gm = Game()
    frames = []
    for f in range(700):
        gm.step()
        if f % 2 == 0 and f >= 40:
            frames.append(GB.leds(render(gm), P=5))
    pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames[:200]]
    out = os.path.join(DOC, 'jeu-renard.gif')
    q[0].save(out, save_all=True, append_images=q[1:], duration=60, loop=0, optimize=True)
    shots = []
    for night, label, star in ((0, 'Le jour', 0), (1, 'La nuit', 0), (0, 'Etoile : invincible', 1)):
        g2 = Game(seed=7 + night * 3 + star)
        for f in range(260 + night * 40):
            g2.step()
        if star:
            g2.star = 50
            g2.y, g2.vy = 12, 0.5
        im = GB.leds(render(g2, night), P=5)
        pg = Image.new('RGB', (im.width, im.height + 40), (16, 16, 22))
        pg.paste(im, (0, 40))
        ImageDraw.Draw(pg).text((10, 8), label, fill=(255, 220, 0), font=GB.font(22, True))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 3, h), (16, 16, 22))
    for i, sh in enumerate(shots):
        sheet.paste(sh, (i * w, 0))
    sheet.save(os.path.join(DOC, 'renard-rendus.png'))
    print(out, len(q), 'images')
