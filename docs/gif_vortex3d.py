"""VORTEX 3D : apercu du tir sur route en 3D (meme dessin que vxDraw du firmware, demo automatique simplifiee).

  python docs/gif_vortex3d.py   -> docs/vortex3d-rendus.png (3 stages) et docs/jeu-vortex3d.gif
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402

W = H = 128
import re  # noqa: E402
SHIP_SPR = re.findall(r'"([^"]*)"', re.search(r'VX_SHIP_SPR\[21\] = \{(.*?)\};', open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read(), re.S).group(1))
SHIP = 6.0
RA = [(0, 150, 220), (190, 0, 170), (0, 180, 60), (220, 110, 0)]
RB = [(0, 0, 0)] * 4
RL = [(255, 255, 255), (255, 230, 0), (255, 255, 255), (255, 255, 120)]
PL = [(230, 120, 40), (60, 170, 255), (220, 60, 200), (150, 220, 255)]


class Game:
    def __init__(self, wave=1, seed=5):
        self.rnd = random.Random(seed)
        self.wave, self.pos, self.spd, self.x, self.z, self.vz = wave, 0.0, .46 + .035 * wave, 0.0, 0.0, 0.0
        self.curve, self.target, self.ct, self.nxt, self.frame, self.score = 0.0, 0.0, 0, 20.0, 0, 1200 * wave
        self.objs, self.shots, self.dbl, self.head, self.bank = [], [], 0, 0.0, 0.0

    def spawn(self):
        r, d = self.rnd.randrange(100), self.pos + 46
        rx = lambda a: self.rnd.randint(-a, a) / 100
        if r < 26:
            self.objs.append([d, rx(80), 0, 0, 2, 0])
        elif r < 40:
            self.objs.append([d, rx(80), 0, 2, 1, 0])
        elif r < 52:
            self.objs.append([d, rx(60), 0, 1, 255, 0])
        elif r < 62:
            self.objs.append([d, rx(90), 0, 6, 255, 0])
        elif r < 72 and self.wave >= 2:
            self.objs.append([d, 0, 3.4, 5, 255, 0])
        elif r < 86:
            self.objs.append([d, rx(70), 0, 3, 3, 0])
        else:
            self.objs.append([d, rx(80), 0, 4, 1, 0])
        self.nxt = self.pos + max(7, 17 - 1.3 * self.wave) + self.rnd.randrange(6)

    def step(self):
        self.frame += 1
        tx, best, jump = 0, 99, False
        for o in self.objs:
            dz = o[0] - self.pos
            if dz < SHIP - .5 or dz > 22:
                continue
            if o[3] == 5:
                jump |= dz < SHIP + 3.5
                continue
            if dz < best:
                best = dz
                if o[3] in (1, 4):
                    tx = o[1]
                elif o[3] == 6 or (o[3] <= 2 and abs(o[1] - self.x) < .35 and dz < 5):
                    tx = o[1] - .6 if o[1] > 0 else o[1] + .6
                else:
                    tx = o[1]
        st_ = -1 if tx < self.x - .06 else 1 if tx > self.x + .06 else 0
        self.x += .06 * st_
        self.bank += (st_ - self.bank) * .2
        self.x = max(-1.05, min(1.05, self.x - curv(self, self.pos + SHIP) * self.spd * 4))
        if jump and self.z == 0:
            self.vz = .085
        if self.z > 0 or self.vz > 0:
            self.z += self.vz
            self.vz -= .0055
            if self.z <= 0:
                self.z = self.vz = 0
        self.pos += self.spd
        self.head += curv(self, self.pos + SHIP) * self.spd
        self.curve = curv(self, self.pos + 20)
        if self.pos >= self.nxt:
            self.spawn()
        if self.frame % 9 == 0:
            self.shots.append([self.pos + SHIP, self.x])
        for s in self.shots:
            s[0] += 1.1
            for o in self.objs:
                if o[3] in (0, 2, 3) and abs(o[0] - s[0]) < 1.2 and abs(o[1] - s[1]) < .3:
                    o[4] -= 1
                    o[5] = 6
                    s[0] = 1e9
                    if o[4] <= 0:
                        o[0] = -1e9
                    break
        self.shots = [s for s in self.shots if s[0] - self.pos < 40]
        for o in self.objs:
            if o[5]:
                o[5] -= 1
            if o[3] == 3:
                o[0] += self.spd * .55
                o[1] = max(-.9, min(.9, o[1] + math.sin((self.frame + o[0] * 10) * .04) * .012))
        self.objs = [o for o in self.objs if o[0] - self.pos > SHIP - 2 and not (o[3] == 5 and self.pos + SHIP > o[0] + o[2] + 2)]


def curv(g, w):
    return (.0065 + .0010 * min(g.wave, 8)) * math.sin(w * .011 + g.wave) * (.6 + .4 * math.sin(w * .029))


def elev(g, w):
    return (.25 + .05 * min(g.wave, 8)) * (math.sin(w * .045) + .5 * math.sin(w * .017 + 1.3))


NP = 56


def proj(g, yh, S):
    e0, xw, dx, prev, out = elev(g, g.pos + SHIP), 0.0, 0.0, 4.0, []
    for i in range(NP):
        dz = 4.0 * 11.5 ** (i / (NP - 1))
        step = dz - prev
        prev = dz
        dx += curv(g, g.pos + dz) * step
        xw += dx * step
        out.append((dz, xw, yh + (5 * (H - yh) - (elev(g, g.pos + dz) - e0 - .0024 * (dz - SHIP) ** 2) * S) / dz))
    return out


def render(g):
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    st = (g.wave - 1) % 4
    ca, cb, rail = RA[st], RB[st], RL[st]
    yh, S = H * .30, W * 2.25
    P = proj(g, yh, S)

    def lerp(k, dz):
        i = 0
        while i < NP - 2 and P[i + 1][0] < dz:
            i += 1
        t = max(0, min(1, (dz - P[i][0]) / (P[i + 1][0] - P[i][0])))
        return P[i][k] + (P[i + 1][k] - P[i][k]) * t
    cxf = lambda dz: W / 2 + (lerp(1, dz) - g.x) * S / dz
    yf = lambda dz: lerp(2, dz)
    SKY = [[(0, 0, 150), (0, 0, 220), (0, 150, 255), (170, 230, 255)], [(0, 0, 150), (150, 0, 150), (255, 0, 150), (255, 170, 220)],
           [(0, 0, 150), (0, 150, 150), (0, 230, 150), (200, 255, 220)], [(0, 0, 150), (200, 0, 0), (255, 140, 0), (255, 230, 150)]]
    skyH = int(yh) - 8
    bands = [skyH * 45 // 100, skyH * 25 // 100, skyH * 18 // 100]
    bands.append(skyH - sum(bands))
    skyTop = 8 + bands[0]
    y = 8
    for b, hgt in enumerate(bands):
        if hgt > 0:
            d.rectangle((0, y, W - 1, y + hgt - 1), fill=SKY[st][b])
        y += hgt
    for i in range(40):
        x = ((i * 53 + 11) * 7 + int(g.head * 220 + 4000) * (1 + (i & 1))) % W
        y = 9 + ((i * 37 + 3) * 13) % int(yh - 6)
        if y < skyTop:
            d.point((x, y), fill=(150, 150, 170) if i % 5 else (255, 255, 255))
    mx = (int(W * .18 - g.head * 90) % (W + 30)) - 15
    gx = (int(W * .45 - g.head * 130) % (W + 40)) - 20
    d.ellipse((mx - 5, 15, mx + 5, 25), fill=(170, 170, 190))
    d.ellipse((mx - 2, 17, mx + 6, 25), fill=(150, 150, 170))
    d.point((mx - 2, 18), fill=(255, 255, 255))
    GBc = [(255, 150, 60), (150, 220, 255), (255, 220, 120), (200, 150, 255)][(st + 1) % 4]
    d.ellipse((gx - 9, 21, gx + 9, 39), fill=GBc)
    for k in (-6, -2, 2, 6):
        hw_ = int(math.sqrt(max(0, 81 - k * k)))
        d.line((gx - hw_ + 1, 30 + k, gx + hw_ - 1, 30 + k), fill=tuple(v * 2 // 3 for v in GBc))
    d.point((gx - 4, 25), fill=(255, 255, 255))
    px, py, pr = W * 7 // 10 - int(g.curve * 2000), int(yh) - 4, 16
    pc = PL[st]
    d.ellipse((px - pr, py - pr, px + pr, py + pr), fill=pc)
    d.ellipse((px + pr // 3 - pr + 2, py + pr // 4 - pr + 2, px + pr // 3 + pr - 2, py + pr // 4 + pr - 2), fill=tuple(v * 2 // 3 for v in pc))
    d.ellipse((px - pr + 3, py - pr + 3, px + pr - 3, py + pr - 3), fill=pc)
    d.line((px - pr - 6, py + 2, px + pr + 5, py + 2), fill=(255, 255, 255))
    d.line((px - pr - 3, py + 3, px + pr + 2, py + 3), fill=(150, 150, 170))
    d.rectangle((0, int(yh) + 1, W, H), fill=(0, 0, 0))
    gy = int(yh) + 1
    d.rectangle((0, gy, W - 1, gy + 1), fill=SKY[st][2])
    d.rectangle((0, gy + 2, W - 1, gy + 7), fill=SKY[st][1])
    d.rectangle((0, gy + 8, W - 1, H - 1), fill=SKY[st][0])
    def quad(pts, c):
        d.polygon([(round(x), round(y)) for x, y in pts], fill=c)
    for i in range(NP - 2, -1, -1):
        zf, xf, yf_ = P[i + 1]
        zn, xn, yn = P[i]
        wm = g.pos + (zf + zn) / 2
        if any(o[3] == 5 and o[0] <= wm <= o[0] + o[2] for o in g.objs):
            continue
        cf, cn = W / 2 + (xf - g.x) * S / zf, W / 2 + (xn - g.x) * S / zn
        hf, hn, rf, rn = 1.2 * S / zf, 1.2 * S / zn, .08 * S / zf + .5, .08 * S / zn + .5
        band = int(math.floor(wm * 1.1)) & 1
        c = ca if band else cb
        if zf > 24 and band:
            c = cb
        quad([(cf - hf - rf, yf_), (cf + hf + rf, yf_), (cn + hn + rn, yn), (cn - hn - rn, yn)], rail)
        quad([(cf - hf, yf_), (cf + hf, yf_), (cn + hn, yn), (cn - hn, yn)], c)
        if not band and zf < 30:
            for sd in (-1, 1):
                lf, ln, wf, wn = cf + sd * hf / 3, cn + sd * hn / 3, .012 * S / zf + .3, .012 * S / zn + .3
                quad([(lf - wf, yf_), (lf + wf, yf_), (ln + wn, yn), (ln - wn, yn)], rail)
    for o in sorted([o for o in g.objs if o[3] != 5], key=lambda o: -o[0]):
        dz = o[0] - g.pos
        if dz < 4 or dz > 46:
            continue
        sc = S / dz
        cx, gy, t, fl = int(cxf(dz) + o[1] * sc), int(yf(dz)), o[3], o[5]
        if t == 0:
            w, h = max(2, int(.44 * sc)), max(2, int(.34 * sc))
            tt = max(1, h // 3)
            d.rectangle((cx - w // 2, gy - h, cx - w // 2 + w - 1, gy - 1), fill=(255, 255, 255) if fl else (230, 140, 30))
            d.rectangle((cx - w // 2, gy - h - tt, cx - w // 2 + w - 1, gy - h - 1), fill=(255, 255, 255) if fl else (255, 210, 90))
            if w > 5:
                d.line((cx - w // 2, gy - h, cx + w // 2 - 1, gy - 1), fill=(150, 70, 0))
                d.line((cx + w // 2 - 1, gy - h, cx - w // 2, gy - 1), fill=(150, 70, 0))
                d.rectangle((cx - w // 2, gy - h, cx - w // 2 + w - 1, gy - 1), outline=(150, 70, 0))
        elif t == 1:
            hw, gap, h = 1.2 * sc, .3 * sc, max(2, int(.36 * sc))
            xl, xr, g0, g1 = int(cxf(dz) - hw), int(cxf(dz) + hw), cx - int(gap), cx + int(gap)
            for a, b in ((xl, g0), (g1, xr)):
                if b > a:
                    d.rectangle((a, gy - h, b - 1, gy - 1), fill=(255, 40, 60))
                    d.line((a, gy - h, b - 1, gy - h), fill=(255, 180, 180))
                    for x in range(a + 2, b, 4):
                        d.line((x, gy - h + 1, x, gy - 1), fill=(160, 0, 30))
        elif t == 2:
            r = max(1, int(.1 * sc))
            d.ellipse((cx - r, gy - 2 * r, cx + r, gy), fill=(150, 150, 170))
            lc = (255, 30, 30) if g.frame & 8 else (255, 230, 0)
            for p in ((cx, gy - r), (cx - r - 1, gy - r), (cx + r + 1, gy - r), (cx, gy - 2 * r - 1)):
                d.point(p, fill=lc)
        elif t == 3:
            w, h, fy = max(3, int(.52 * sc)), max(2, int(.16 * sc)), gy - int(.16 * sc)
            d.line((cx - w // 3, gy, cx + w // 3, gy), fill=(0, 0, 0))
            d.rounded_rectangle((cx - w // 2, fy - h // 2, cx - w // 2 + w - 1, fy - h // 2 + h - 1), h // 2, fill=(255, 255, 255) if fl else (190, 195, 210))
            r = max(1, w // 5)
            d.ellipse((cx - r, fy - h // 2 - r, cx + r, fy - h // 2 + r), fill=(0, 220, 255))
            for i in range(3):
                d.point((cx - w // 3 + i * w // 3, fy), fill=(255, 40, 40) if (g.frame // 4 + i) % 3 else (255, 255, 0))
        elif t == 4:
            r, fy = max(1, int(.1 * sc)) + ((g.frame >> 2) & 1), gy - int(.14 * sc)
            d.ellipse((cx - r, fy - r, cx + r, fy + r), fill=(255, 220, 0))
        elif t == 7:
            w, h, fy = max(5, int(.62 * sc)), max(2, int(.24 * sc)), gy - int(.22 * sc)
            d.line((cx - w // 3, gy, cx + w // 3, gy), fill=(0, 0, 0))
            d.polygon([(cx - w // 2, fy), (cx + w // 2, fy), (cx, fy + h)], fill=(200, 60, 255))
            d.line((cx - w // 2, fy, cx + w // 2, fy), fill=(255, 170, 255))
            r = max(1, w // 8)
            d.ellipse((cx - r, fy + h // 3 - r, cx + r, fy + h // 3 + r), fill=(255, 40, 40))
            eng = (255, 230, 0)
            ew, eh = max(2, w // 8), max(1, h // 3)
            d.rectangle((cx - w // 3 - 1, fy - eh, cx - w // 3 - 2 + ew, fy - 1), fill=eng)
            d.rectangle((cx + w // 3 - ew + 1, fy - eh, cx + w // 3, fy - 1), fill=eng)
        elif t == 6:
            w, h = max(1, int(.18 * sc)), max(3, int(.6 * sc))
            d.rectangle((cx - w // 2, gy - h, cx - w // 2 + w - 1, gy - 1), fill=(170, 60, 255))
            d.line((cx - w // 2, gy - h, cx - w // 2, gy - 1), fill=(230, 170, 255))
            for y in range(gy - h, gy, max(2, h // 5)):
                d.line((cx - w // 2, y, cx - w // 2 + w - 1, y), fill=(255, 255, 255))
    if getattr(g, 'descent', None) is not None:  # chasseur qui descend des etoiles (60 images)
        t = g.descent / 60
        sx0, ex, ey = 30, cxf(46) + .5 * S / 46, yf(46) - .45 * S / 46
        for k in range(4, 0, -1):
            tk = max(0, t - k * .05)
            d.point((int(sx0 + (ex - sx0) * tk * tk), int(10 + (ey - 10) * tk * tk)), fill=(255, 150, 0) if k < 3 else (150, 0, 150))
        x, y = int(sx0 + (ex - sx0) * t * t), int(10 + (ey - 10) * t * t)
        d.line((x - 2, y, x + 2, y), fill=(200, 60, 255))
        d.point((x, y + 1), fill=(255, 40, 40))
        d.point((x, y - 1), fill=(255, 255, 255))
    for s in g.shots:
        dz = s[0] - g.pos
        if dz > 4:
            w, h = max(2, int(.14 * S / dz)), max(3, int(.45 * S / dz))
            x, y = int(cxf(dz) + s[1] * S / dz) - w // 2, int(yf(dz)) - int(.25 * S / dz) - h
            d.rounded_rectangle((x - 1, y - 1, x + w, y + h), (w + 2) // 2, fill=(255, 30, 20))
            d.rounded_rectangle((x, y, x + w - 1, y + h - 1), max(0, w // 2 - 1), fill=(255, 140, 0))
            d.line((x + w // 4, y, x + w // 4 + max(1, w // 2) - 1, y), fill=(255, 230, 0))
    sx, sy = W // 2, int(yf(SHIP))
    jz = int(g.z * S / SHIP * .6)
    sw = 15 - jz // 6
    d.rounded_rectangle((sx - sw, sy - 1, sx + sw, sy + 1), 1, fill=(0, 0, 0))
    x0, y0, fl = sx - 13, sy - 20 - jz, g.frame & 2
    pix = im.load()
    for r, line in enumerate(SHIP_SPR):  # epaisseur : meme forme 2 points plus bas, teintes sombres
        for c, ch in enumerate(line):
            if ch == '.':
                continue
            col = (170, 30, 0) if ch in 'oy' else (150, 0, 0) if ch == 'R' else (20, 20, 30) if ch == 'k' else (150, 150, 175)
            px, py = x0 + c, y0 + r + round((c - 13) * g.bank * .18)
            for dy_, cc in ((1, col), (2, col if ch == 'k' else (20, 20, 30))):
                if 0 <= px < W and 0 <= py + dy_ < H:
                    pix[px, py + dy_] = cc
    for r, line in enumerate(SHIP_SPR):
        for c, ch in enumerate(line):
            if ch == '.':
                continue
            col = {'k': (20, 20, 30), 'W': (255, 255, 255), 'L': (225, 230, 245), 'S': (170, 175, 195), 's': (150, 150, 170),
                   'R': (255, 40, 60), 'c': (0, 170, 255), 'C': (160, 240, 255),
                   'o': (255, 230, 0) if fl else (255, 120, 0)}.get(ch, (255, 120, 0) if fl else (255, 230, 0))
            px, py = x0 + c, y0 + r + round((c - 13) * g.bank * .18)
            if 0 <= px < W and 0 <= py < H:
                pix[px, py] = col
    d.rectangle((0, 0, W, 7), fill=(0, 0, 0))
    GB.tiny(d, 1, 1, 'S%d' % g.wave, (255, 220, 0))
    sc_ = 'SCORE %d' % g.score
    GB.tiny(d, (W - len(sc_) * 4) // 2, 1, sc_, (230, 230, 230))
    d.line((1, 7, W - 2, 7), fill=(0, 0, 150))
    d.line((1, 7, 1 + int((W - 3) * min(1, (g.pos % 650) / 650)), 7), fill=(0, 255, 120))
    return im


if __name__ == '__main__':
    shots = []
    for wave, label, n in ((1, 'Stage 1', 220), (2, 'Stage 2 : trous', 300), (3, 'Stage 3', 260)):
        g = Game(wave, seed=3 + wave)
        for _ in range(n):
            g.step()
        P0 = g.pos
        g.objs = [[P0 + 9, -.5, 0, 0, 2, 0], [P0 + 13, .45, 0, 3, 3, 0], [P0 + 20, .2, 0, 1, 255, 0], [P0 + 11, .75, 0, 6, 255, 0],
                  [P0 + 15, -.2, 0, 2, 1, 0], [P0 + 26, -.6, 0, 4, 1, 0], [P0 + SHIP + 4, -.25, 0, 7, 3, 0]]
        if wave == 2:
            g.objs.append([P0 + 30, 0, 3.4, 5, 255, 0])
        g.descent = 40
        im = GB.leds(render(g), P=4)
        pg = Image.new('RGB', (im.width, im.height + 34), (16, 16, 22))
        pg.paste(im, (0, 34))
        ImageDraw.Draw(pg).text((8, 6), label, fill=(255, 220, 0), font=GB.font(20, True))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 3, h), (16, 16, 22))
    for i, sh in enumerate(shots):
        sheet.paste(sh, (i * w, 0))
    sheet.save(os.path.join(DOC, 'vortex3d-rendus.png'))
    g = Game(2, seed=9)
    frames = []
    for f in range(360):
        g.step()
        if f % 2 == 0 and f > 40:
            frames.append(GB.leds(render(g), P=3))
    pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    q[0].save(os.path.join(DOC, 'jeu-vortex3d.gif'), save_all=True, append_images=q[1:], duration=50, loop=0, optimize=True)
    print('ok')
