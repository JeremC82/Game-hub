"""RALLYE 3D : apercu de la course sur circuit en relief (meme dessin et memes regles que xrDraw / xrStep du firmware).

  python docs/gif_rallye3d.py   -> docs/rallye3d-rendus.png (4 decors) et docs/jeu-rallye3d.gif (demo)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5)

C, R, TW, TD, SK, TOP = 30, 20, 10, 7, 4, 14
WP = [(5, 3), (15, 3), (24, 3), (27, 6), (27, 11), (23, 13), (17, 11), (12, 14), (7, 17), (3, 14), (3, 7)]
NWP = len(WP)
RW = 1.15
TH = [((30, 160, 40), (0, 200, 60), (150, 150, 165), (0, 190, 0)),
      ((230, 190, 90), (255, 220, 140), (160, 150, 150), (150, 190, 0)),
      ((225, 230, 245), (255, 255, 255), (150, 150, 175), (0, 150, 90)),
      ((150, 30, 20), (220, 80, 0), (150, 150, 160), (150, 160, 0))]
CC = [(255, 40, 40), (40, 110, 255), (255, 220, 0), (230, 230, 230)]
RF = [(220, 60, 50), (150, 160, 190), (240, 200, 90), (60, 170, 230)]


def mul(c, n, d):
    return tuple(v * n // d for v in c)


def add(c, a):
    return tuple(min(255, v + a) for v in c)


class Race:
    def __init__(self, wave=1):
        self.wave = wave
        self.mir = wave % 2 == 0
        self.frame = 0
        self.t = [[0] * C for _ in range(R)]
        for r in range(R):
            for c in range(C):
                dmin = self.road_d(c + .5, r + .5)
                h = (((r * 7919 + c * 104729 + wave * 31337) & 0xFFFFFFFF) * 2654435761 & 0xFFFFFFFF) >> 24
                v = 0
                if dmin < RW + .9:
                    v = 0
                elif r in (0, R - 1) or c in (0, C - 1):
                    v = 3 if h & 3 else 0
                elif dmin > RW + 1.5 and h < 22:
                    v = 2
                elif h < 70:
                    v = 3
                self.t[r][c] = v
        a = math.atan2(self.wy(1) - self.wy(0), self.wx(1) - self.wx(0))
        self.cars = []
        for i in range(4):
            back, side = .9 + (i // 2) * 1.1, (.55 if i & 1 else -.55)
            self.cars.append(dict(x=self.wx(0) - math.cos(a) * back - math.sin(a) * side,
                                  y=self.wy(0) - math.sin(a) * back + math.cos(a) * side, a=a, v=0.0, wp=1, lap=0, col=i))
        self.cx = self.cy = None

    def road_d(self, px, py):
        dmin = 99
        for i in range(NWP):
            ax, ay = self.wx(i), self.wy(i)
            bx, by = self.wx((i + 1) % NWP), self.wy((i + 1) % NWP)
            vx, vy = bx - ax, by - ay
            t = max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy)))
            dmin = min(dmin, math.hypot(px - ax - t * vx, py - ay - t * vy))
        return dmin

    def wx(self, i):
        return C - WP[i][0] if self.mir else WP[i][0]

    def wy(self, i):
        return WP[i][1]

    def tile(self, x, y):
        c, r = math.floor(x), math.floor(y)
        if c < 0 or r < 0 or c >= C or r >= R:
            return 3
        return self.t[r][c]

    def step(self):
        self.frame += 1
        for i, c in enumerate(self.cars):
            n = (c['wp'] + 1) % NWP
            tx, ty = self.wx(c['wp']), self.wy(c['wp'])
            if (tx - c['x']) ** 2 + (ty - c['y']) ** 2 < 9:
                tx, ty = (tx + self.wx(n)) / 2, (ty + self.wy(n)) / 2
            off, sa = (i - 1.5) * .25, math.atan2(ty - c['y'], tx - c['x'])
            tx -= math.sin(sa) * off
            ty += math.cos(sa) * off
            da = math.atan2(ty - c['y'], tx - c['x']) - c['a']
            while da > math.pi:
                da -= 2 * math.pi
            while da < -math.pi:
                da += 2 * math.pi
            steer = max(-1, min(1, da * 2.5))
            gas = abs(da) < .9 or c['v'] < .06
            brake = abs(da) > 1.3 and c['v'] > .1
            maxv = .15 if i == 0 else .123 + (i - 2) * .004
            turn = min(1, abs(c['v']) / .06) * (1 if c['v'] >= 0 else -1)
            c['a'] += steer * .075 * turn
            if gas:
                c['v'] += .0035
            elif brake:
                c['v'] -= .008
            else:
                c['v'] *= .985
            lim = maxv if self.road_d(c['x'], c['y']) < RW + .1 else maxv * .45
            if c['v'] > lim:
                c['v'] = max(lim, c['v'] - .006)
            nx, ny = c['x'] + math.cos(c['a']) * c['v'], c['y'] + math.sin(c['a']) * c['v']
            if self.tile(nx, ny) >= 2:
                c['v'] = -c['v'] * .3
            else:
                c['x'], c['y'] = nx, ny
            dx, dy = self.wx(c['wp']) - c['x'], self.wy(c['wp']) - c['y']
            if dx * dx + dy * dy < (7 if i == 0 else 4.8):
                if c['wp'] == 0:
                    c['lap'] += 1
                c['wp'] = (c['wp'] + 1) % NWP
        for i in range(4):
            for j in range(i + 1, 4):
                a, b = self.cars[i], self.cars[j]
                dx, dy = b['x'] - a['x'], b['y'] - a['y']
                d = math.hypot(dx, dy)
                if 1e-3 < d < .6:
                    p = (.6 - d) / 2 / d
                    a['x'] -= dx * p
                    a['y'] -= dy * p
                    b['x'] += dx * p
                    b['y'] += dy * p
                    a['v'] *= .92
                    b['v'] *= .92


def render(rc, score=1250, lives=3, W=128, H=128):
    im = Image.new('RGB', (W, H), (0, 0, 150))
    dr = ImageDraw.Draw(im)
    px_ = im.load()

    def P(x, y, col):
        if 0 <= x < W and 0 <= y < H:
            px_[x, y] = col

    me = rc.cars[0]
    VH = H - 8
    MW, MH = C * TW + R * SK, TOP - 8 + R * TD + 6
    raw_x = me['x'] * TW + (R - me['y']) * SK
    raw_y = TOP + me['y'] * TD
    la = me['v'] * 60
    tx = max(0, min(MW - W, raw_x + math.cos(me['a']) * la * 3 - W / 2))
    ty = max(0, min(MH - VH, raw_y + math.sin(me['a']) * la * 2 - 8 - VH / 2))
    if rc.cx is None:
        rc.cx, rc.cy = tx, ty
    rc.cx += (tx - rc.cx) * .15
    rc.cy += (ty - rc.cy) * .15

    def PX(X, Y):
        return X * TW + (R - Y) * SK - rc.cx

    def PY(Y):
        return TOP + Y * TD - rc.cy

    def quad(pts, col):
        dr.polygon([(round(x), round(y)) for x, y in pts], fill=col)

    th = TH[(rc.wave - 1) % 4]
    grass, tuft, road, leaf = th
    leafhi = (min(255, leaf[0] + 110), min(255, leaf[1] + 70), min(255, leaf[2] + 110))
    quad([(PX(0, 0), PY(0)), (PX(0, R), PY(R)), (PX(0, R), PY(R) + 5), (PX(0, 0), PY(0) + 5)], (150, 100, 60))
    quad([(PX(0, R), PY(R)), (PX(C, R), PY(R)), (PX(C, R), PY(R) + 5), (PX(0, R), PY(R) + 5)], (190, 130, 70))
    quad([(PX(0, 0), PY(0)), (PX(C, 0), PY(0)), (PX(C, R), PY(R)), (PX(0, R), PY(R))], grass)
    for r in range(1, R - 1):
        for c in range(1, C - 1):
            if rc.t[r][c] == 0 and (r * 5 + c * 3) % 7 == 0:
                x2, y2 = round(PX(c + .5, r + .5)), round(PY(r + .5))
                P(x2, y2, tuft)
                P(x2 + 2, y2 - 1, tuft)

    def disc(x, y, rad, col):
        pts = [(PX(x + math.cos(k * .785398) * rad, y + math.sin(k * .785398) * rad), PY(y + math.sin(k * .785398) * rad)) for k in range(8)]
        quad(pts, col)

    def band(ax, ay, bx, by, w, col):
        dx, dy = bx - ax, by - ay
        ln = math.hypot(dx, dy)
        nx, ny = -dy / ln * w, dx / ln * w
        quad([(PX(ax + nx, ay + ny), PY(ay + ny)), (PX(bx + nx, by + ny), PY(by + ny)), (PX(bx - nx, by - ny), PY(by - ny)), (PX(ax - nx, ay - ny), PY(ay - ny))], col)
    red, white = (255, 30, 30), (255, 255, 255)
    segs = [(rc.wx(i), rc.wy(i), rc.wx((i + 1) % NWP), rc.wy((i + 1) % NWP)) for i in range(NWP)]
    for ax, ay, bx, by in segs:
        n = max(1, int(math.hypot(bx - ax, by - ay) / .5))
        for k in range(n):
            band(ax + (bx - ax) * k / n, ay + (by - ay) * k / n, ax + (bx - ax) * (k + 1) / n, ay + (by - ay) * (k + 1) / n, RW + .2, red if k & 1 else white)
        disc(ax, ay, RW + .2, red)
    for ax, ay, bx, by in segs:
        band(ax, ay, bx, by, RW, road)
        disc(ax, ay, RW, road)
    for ax, ay, bx, by in segs:
        ln = math.hypot(bx - ax, by - ay)
        t = .6
        while t + .5 < ln - .6:
            x0, y0 = ax + (bx - ax) * t / ln, ay + (by - ay) * t / ln
            x1, y1 = ax + (bx - ax) * (t + .5) / ln, ay + (by - ay) * (t + .5) / ln
            dr.line([(round(PX(x0, y0)), round(PY(y0))), (round(PX(x1, y1)), round(PY(y1)))], fill=(230, 230, 230))
            t += 1.2
    ax, ay, bx, by = segs[0]
    ln = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / ln, (by - ay) / ln
    nx, ny = -uy, ux
    for k in range(-5, 5):
        for m in range(2):
            sd, f = (k + .5) * RW / 5, (m - .5) * .25
            x, y = ax + nx * sd + ux * f, ay + ny * sd + uy * f
            X, Y = round(PX(x, y)) - 1, round(PY(y)) - 1
            dr.rectangle((X, Y, X + 1, Y + 1), fill=white if (k + m) & 1 else (0, 0, 0))
    for r in range(R):
        for c in range(C - 1, -1, -1):
            t = rc.t[r][c]
            if t < 2:
                continue
            if t == 2:
                rf = RF[(r * 3 + c) & 3]
                x0, x1, y0, y1, h = c + .1, c + .9, r + .1, r + .9, 12
                quad([(PX(x0, y0) + 3, PY(y0) + 1), (PX(x1, y0) + 3, PY(y0) + 1), (PX(x1, y1) + 3, PY(y1) + 1), (PX(x0, y1) + 3, PY(y1) + 1)], (0, 0, 0))
                quad([(PX(x0, y0), PY(y0) - h), (PX(x0, y1), PY(y1) - h), (PX(x0, y1), PY(y1)), (PX(x0, y0), PY(y0))], (150, 150, 165))
                quad([(PX(x0, y1), PY(y1) - h), (PX(x1, y1), PY(y1) - h), (PX(x1, y1), PY(y1)), (PX(x0, y1), PY(y1))], (200, 200, 215))
                fx, fy = round(PX(x0, y1)), round(PY(y1))
                for wy in range(3):
                    for wx in range(3):
                        P(fx + 1 + wx * 3, fy - 3 - wy * 3, (255, 230, 100) if (r + c + wx + wy) % 3 else (0, 0, 150))
                quad([(PX(x0, y0), PY(y0) - h), (PX(x1, y0), PY(y0) - h), (PX(x1, y1), PY(y1) - h), (PX(x0, y1), PY(y1) - h)], rf)
                dr.line([(round(PX(x0, y0)), round(PY(y0) - h)), (round(PX(x1, y0)) - 1, round(PY(y0) - h))], fill=add(rf, 60))
            else:
                x2, y2 = round(PX(c + .5, r + .5)), round(PY(r + .5))
                dr.line([(x2 - 1, y2 + 1), (x2 + 4, y2 + 1)], fill=(0, 0, 0))
                dr.line([(x2, y2 + 2), (x2 + 3, y2 + 2)], fill=(0, 0, 0))
                dr.line([(x2, y2 - 3), (x2, y2)], fill=(150, 90, 30))
                fy = y2 - 7
                for dy in range(-4, 5):
                    for dx in range(-4, 5):
                        if dx * dx + dy * dy <= 17:
                            P(x2 + dx, fy + dy, leafhi if dx + dy <= -3 else leaf)
                P(x2 + 2, fy + 2, mul(leaf, 2, 3))
                P(x2 + 3, fy, mul(leaf, 2, 3))
        for car in reversed(rc.cars):
            if max(0, min(R - 1, math.floor(car['y']))) != r:
                continue
            k = CC[car['col']]
            ca, sa = math.cos(car['a']), math.sin(car['a'])

            def box(l0, l1, w, z, ox, oy, col):
                pts = []
                for lx, ly in ((l1, -w), (l1, w), (l0, w), (l0, -w)):
                    X, Y = car['x'] + ca * lx - sa * ly, car['y'] + sa * lx + ca * ly
                    pts.append((PX(X, Y) + ox, PY(Y) - z + oy))
                quad(pts, col)
            box(-.38, .38, .22, 0, 1, 1, (0, 0, 0))
            box(-.38, .38, .22, 1, 0, 0, mul(k, 2, 3))
            box(-.38, .38, .22, 3, 0, 0, k)
            box(-.2, .12, .17, 5, 0, 0, (150, 200, 255))
            box(-.16, .06, .15, 6, 0, 0, add(k, 80))
            for s in (-1, 1):
                fx, fy = car['x'] + ca * .38 - sa * .14 * s, car['y'] + sa * .38 + ca * .14 * s
                P(round(PX(fx, fy)), round(PY(fy) - 3), (255, 255, 170))
                bx, by = car['x'] - ca * .38 - sa * .14 * s, car['y'] - sa * .38 + ca * .14 * s
                P(round(PX(bx, by)), round(PY(by) - 3), (255, 0, 0))
    dr.rectangle((0, 0, W - 1, 7), fill=(0, 0, 0))
    prog = [c['lap'] * NWP + (c['wp'] - 1) % NWP - math.hypot(rc.wx(c['wp']) - c['x'], rc.wy(c['wp']) - c['y']) / 100 for c in rc.cars]
    place = 1 + sum(1 for p in prog[1:] if p > prog[0])
    GB.tiny(dr, 1, 1, 'TOUR%d' % min(3, me['lap'] + 1), (255, 220, 0))
    s = str(score)
    GB.tiny(dr, (W - (len(s) * 4 - 1)) // 2, 1, s, (230, 230, 230))
    pl = '1ER' if place == 1 else '%dE' % place
    GB.tiny(dr, W - 1 - (len(pl) * 4 - 1), 1, pl, (255, 220, 0) if place == 1 else (255, 60, 60))
    for i in range(lives):
        dr.rectangle((W - 22 - i * 5, 2, W - 20 - i * 5, 4), fill=(255, 40, 40))
    return im


if __name__ == '__main__':
    shots = []
    for wave, label, n in ((1, 'Verdure', 60), (2, 'Desert (miroir)', 200), (3, 'Neige', 330), (4, 'Volcan', 120)):
        rc = Race(wave)
        for f in range(n):
            rc.step()
        for _ in range(20):
            render(rc)
        im = GB.leds(render(rc, score=500 * wave + 2 * n), P=4)
        pg = Image.new('RGB', (im.width, im.height + 34), (16, 16, 22))
        pg.paste(im, (0, 34))
        ImageDraw.Draw(pg).text((8, 6), label, fill=(255, 220, 0), font=GB.font(20, True))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 4, h), (16, 16, 22))
    for i, sh in enumerate(shots):
        sheet.paste(sh, (i * w, 0))
    sheet.save(os.path.join(DOC, 'rallye3d-rendus.png'))
    rc = Race(1)
    frames = []
    for f in range(420):
        rc.step()
        im = render(rc, score=10 * f)
        if f % 2 == 0:
            frames.append(GB.leds(im, P=3))
    pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    q[0].save(os.path.join(DOC, 'jeu-rallye3d.gif'), save_all=True, append_images=q[1:], duration=50, loop=0, optimize=True)
    print('ok')
