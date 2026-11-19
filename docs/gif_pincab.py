"""PINCAB : simulation de la table (meme physique que le firmware) et GIF de la demo.

  python docs/gif_pincab.py   -> docs/jeu-pincab.gif (ecran 128x128) ; affiche aussi une verification de la physique
"""
import math
import os
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5)

SEG = [(0, 88, 0, 14), (0, 14, 8, 4), (8, 4, 16, 1), (16, 1, 48, 1), (48, 1, 56, 4), (56, 4, 64, 12), (64, 12, 64, 96),
       (56, 96, 56, 24), (0, 74, 13, 82.6), (56, 74, 43, 82.6), (5, 58, 5, 70), (5, 70, 11, 76), (51, 58, 51, 70), (51, 70, 45, 76),
       (56, 95, 64, 95)]
KICK = [(11, 76, 5, 58), (45, 76, 51, 58)]
BUMP = [(18, 26, 4), (38, 26, 4), (28, 40, 4)]
TGT = [(17, 14, 23, 14), (25, 14, 31, 14), (33, 14, 39, 14)]
PIV = [(13, 84), (43, 84)]
POST = [(9, 44, 1.6), (47, 44, 1.6)]
FL, REST, UP, R = 13.0, 0.45, -0.45, 1.7


class Table:
    def __init__(self):
        self.bx, self.by, self.vx, self.vy = 60.0, 92.0, 0.0, 0.0
        self.al = self.ar = REST
        self.tgt = [True] * 3
        self.flash = [0] * 5
        self.score, self.mult, self.hold, self.pow = 0, 1, 0, 0.0
        self.frame = 0
        self.drains = 0
        self.fx, self.fd, self.fflash, self.hold, self.cd, self.pop, self.popt, self.popxy, self.roar = 28.0, 1, 0, 0, 0, '', 0, (0, 0), 0

    def hit(self, x0, y0, x1, y1, rr, e, svx=0.0, svy=0.0):
        dx, dy = x1 - x0, y1 - y0
        l2 = dx * dx + dy * dy
        t = max(0.0, min(1.0, ((self.bx - x0) * dx + (self.by - y0) * dy) / l2)) if l2 else 0.0
        qx, qy = x0 + dx * t, y0 + dy * t
        cx, cy = self.bx - qx, self.by - qy
        d = math.hypot(cx, cy)
        if d >= rr or d < 1e-4:
            return None
        nx, ny = cx / d, cy / d
        self.bx, self.by = qx + nx * rr, qy + ny * rr
        rvx, rvy = self.vx - svx, self.vy - svy
        vn = rvx * nx + rvy * ny
        if vn < 0:
            self.vx -= (1 + e) * vn * nx
            self.vy -= (1 + e) * vn * ny
        return nx, ny

    def step(self):
        self.frame += 1
        self.flash = [max(0, f - 1) for f in self.flash]
        self.fflash, self.popt, self.roar, self.cd = max(0, self.fflash - 1), max(0, self.popt - 1), max(0, self.roar - 1), max(0, self.cd - 1)
        self.fx += self.fd * .12
        if self.fx < 15: self.fx, self.fd = 15, 1
        if self.fx > 41: self.fx, self.fd = 41, -1
        if self.hold:
            self.hold -= 1
            if self.hold == 0:
                self.bx, self.by, self.vx, self.vy, self.cd = 28, 21.5, ((self.frame * 7) % 31 - 15) / 10, 2.6, 40
            return
        fl = self.by > 72 and self.vy > -0.5 and self.bx < 56 and 8 < self.bx < 29
        fr = self.by > 72 and self.vy > -0.5 and self.bx < 56 and 27 <= self.bx < 50
        if self.bx > 56.5 and self.by > 85 and abs(self.vy) < 0.4:
            self.hold += 1
            if self.hold < 30 + self.frame % 25:
                self.pow = min(1.0, self.pow + 0.025)
            else:
                self.vy = -(5.2 + 2.3 * max(self.pow, 0.5))
                self.pow, self.hold = 0.0, 0
        tl, tr = (UP if fl else REST), (UP if fr else REST)
        dl, dr = max(-0.33, min(0.18, tl - self.al)), max(-0.33, min(0.18, tr - self.ar))
        for _ in range(4):
            self.al += dl / 4
            self.ar += dr / 4
            self.vy += 0.06 / 4
            sp = math.hypot(self.vx, self.vy)
            if sp > 7:
                self.vx, self.vy = self.vx * 7 / sp, self.vy * 7 / sp
            self.bx += self.vx / 4
            self.by += self.vy / 4
            for s in SEG:
                self.hit(*s, R, 0.5)
            for i, s in enumerate(KICK):
                n = self.hit(*s, R, 0.5)
                if n and self.flash[3 + i] == 0:
                    self.vx += n[0] * 2.4
                    self.vy += n[1] * 2.4
                    self.flash[3 + i] = 6
                    self.score += 10 * self.mult
            for i, (bx, by, br) in enumerate(BUMP):
                cx, cy = self.bx - bx, self.by - by
                d = math.hypot(cx, cy)
                rr = br + R
                if 1e-3 < d < rr:
                    nx, ny = cx / d, cy / d
                    self.bx, self.by = bx + nx * rr, by + ny * rr
                    self.vx, self.vy = nx * 3.6, ny * 3.6
                    if not self.flash[i]:
                        self.score += 100 * self.mult
                    self.flash[i] = 8
            for (px_, py_, pr) in POST:
                cx, cy = self.bx - px_, self.by - py_
                d = math.hypot(cx, cy)
                if 1e-3 < d < pr + R:
                    nx, ny = cx / d, cy / d
                    self.bx, self.by = px_ + nx * (pr + R), py_ + ny * (pr + R)
                    vn = self.vx * nx + self.vy * ny
                    if vn < 0:
                        self.vx -= 1.8 * vn * nx - nx * .8
                        self.vy -= 1.8 * vn * ny - ny * .8
            cx, cy = self.bx - self.fx, self.by - 62
            d = math.hypot(cx, cy)
            if 1e-3 < d < 1.8 + R:
                nx, ny = cx / d, cy / d
                self.bx, self.by = self.fx + nx * (1.8 + R), 62 + ny * (1.8 + R)
                vn = self.vx * nx + self.vy * ny
                if vn < 0:
                    self.vx -= 1.6 * vn * nx
                    self.vy -= 1.6 * vn * ny
                self.vx += nx * 1.5
                self.vy += ny * 1.5
                if not self.fflash:
                    self.fflash, self.fd = 12, -self.fd
                    self.score += 500 * self.mult
                    self.pop, self.popt, self.popxy = '+%d' % (500 * self.mult), 22, (self.fx, 59)
            if not self.cd and math.hypot(self.bx - 28, self.by - 19) < 2.4 and math.hypot(self.vx, self.vy) < 4.5:
                self.bx, self.by, self.vx, self.vy, self.hold, self.roar = 28, 19, 0, 0, 50, 60
                self.score += 1500 * self.mult
                self.pop, self.popt, self.popxy = '+%d' % (1500 * self.mult), 22, (28, 16)
                return
            for i, t in enumerate(TGT):
                if self.tgt[i] and self.hit(*t, R + 0.5, 0.6):
                    self.tgt[i] = False
                    self.score += 250 * self.mult
                    if not any(self.tgt):
                        self.score += 2000 * self.mult
                        self.mult = min(5, self.mult + 1)
                        self.tgt = [True] * 3
            for f in range(2):
                a = self.ar if f else self.al
                w = dr if f else dl
                px, py = PIV[f]
                ux, uy = (-math.cos(a) if f else math.cos(a)), math.sin(a)
                tx, ty = px + ux * FL, py + uy * FL
                dx, dy = tx - px, ty - py
                t = max(0.0, min(1.0, ((self.bx - px) * dx + (self.by - py) * dy) / (dx * dx + dy * dy)))
                s = t * FL
                svx = s * w * math.sin(a) if f else -s * w * math.sin(a)
                svy = s * w * math.cos(a)
                self.hit(px, py, tx, ty, R + 1.2, 0.35, svx, svy)
        if abs(self.vx) + abs(self.vy) < 0.08 and not (self.bx > 56.5 and self.by > 85):
            self.still = getattr(self, 'still', 0) + 1
        else:
            self.still = 0
        if self.still > 80:
            self.still = 0
            self.vx += ((self.frame * 7) % 21 - 10) / 10
            self.vy -= 1.2
        if self.by > 99:
            self.drains += 1
            self.bx, self.by, self.vx, self.vy = 60.0, 92.0, 0.0, 0.0


LOGO = None


def logo_bits():
    global LOGO
    if LOGO is None:
        from PIL import ImageFont
        im = Image.new('1', (80, 16))
        d = ImageDraw.Draw(im)
        try:
            f = ImageFont.truetype('C:/Windows/Fonts/consolab.ttf', 15)
        except OSError:
            f = ImageFont.load_default()
        d.text((4, 0), 'PINCAB', fill=1, font=f)
        LOGO = im.load()
    return LOGO


class View:
    def __init__(self, W=128, H=128):
        self.W, self.H = W, H
        self.glass = H >= 128 and W >= 96
        self.gh = 34 if self.glass else 0
        self.sb = min((W - 4) / 64, 1.7 * H / 96)
        self.st = self.sb * 0.86
        self.py0 = self.gh + 1
        self.ph = int(96 * self.sb * 0.95)
        self.scene = self.py0 + self.ph + 1
        self.cam = max(0, self.scene - H)

    def s(self, y):
        return self.st + (self.sb - self.st) * y / 96

    def X(self, x, y):
        return self.W // 2 + round((x - 32) * self.s(y))

    def Y(self, y):
        return self.py0 + round(y * self.ph / 96) - int(self.cam)

    def follow(self, tb):
        by = self.py0 + tb.by * self.ph / 96
        want = max(0.0, min(by - self.H * .55, max(0, self.scene - self.H)))
        self.cam += (want - self.cam) * .12


BEAST = [".......FF.", "......FGGF", "....FGGGGG", "..FGGgGGGG", ".FGGgGGGGg", "FGGgYYYGGG", "FGGYYkYGGG", ".GGYYkYGGg",
         ".GGgYYYGGG", "..GGGGGGgG", "..GgGGGGGG", "...GGkkkkk", "...GkWkWkW", "....Gkkkkk", "....GGGGGG", ".....gGGGG",
         "......gGGG", "........gG"]


def render(tb, v, frame=0, extra=()):
    """theme lagon maudit (meme dessin que le firmware) ; extra : balles en plus (multiball)"""
    W, H = v.W, v.H
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    px = im.load()
    white, yel, green, dgreen = (255, 255, 255), (255, 220, 0), (40, 220, 80), (0, 150, 0)
    teal, pink, orange, red = (0, 160, 140), (255, 80, 180), (255, 150, 0), (255, 30, 20)
    cam = int(v.cam)
    deep = (0, 0, 170)
    for y in range(H):
        vy = (y + cam - v.py0) * 96 / v.ph
        if 0 <= vy <= 96:
            d.line((v.X(-1, vy), y, v.X(65, vy), y), fill=deep)
    for row in range(40):
        vy0 = 3 + row * 2.4
        y0 = v.Y(vy0)
        if y0 < -2 or y0 > H + 2:
            continue
        for x in range(v.X(1, vy0), v.X(63, vy0) + 1):
            ph = x * .35 + row * 1.7 + frame * .06
            if math.sin(ph * .5) < -.2:
                continue
            yy = y0 + round(math.sin(ph) * 1.2)
            if 0 <= x < W and 0 <= yy < H:
                px[x, yy] = teal
    for k in range(2):
        for j in range(4):
            rx, ry = (53 - j * 1.6 if k else 3 + j * 1.6), 88 - (j & 1) * 3
            x0, y0, x1, y1 = v.X(rx, ry + 6), v.Y(ry + 6), v.X(rx + (-1 if k else 1), ry), v.Y(ry)
            d.line((x0, y0, x1, y1), fill=dgreen)
            d.ellipse((x1 - 1, y1 - 1, x1 + 1, y1 + 1), fill=(150, 90, 0))
    for lx_, ly_ in ((9, 16), (50, 42), (6, 50), (30, 26), (49, 14)):
        x, y, r = v.X(lx_, ly_), v.Y(ly_), max(1, round(1.8 * v.s(ly_)))
        d.ellipse((x - r, y - r, x + r, y + r), fill=dgreen)
        d.line((x, y, x + r, y - 1), fill=deep)
    if v.glass and cam < v.gh:
        oy = -cam
        d.rectangle((0, oy, W - 1, oy + 33), outline=dgreen)
        d.rectangle((1, oy + 1, W - 2, oy + 32), outline=(0, 0, 150))
        d.ellipse((5, oy + 3, 15, oy + 13), fill=(255, 230, 150))
        d.ellipse((7, oy + 6, 9, oy + 8), fill=(200, 180, 100))
        sd = 777
        for i in range(14):
            sd = (sd * 1103515245 + 12345) & 0xFFFFFFFF
            if ((frame >> 3) + i) % 6:
                d.point((18 + (sd >> 16) % (W - 40), oy + 2 + (sd >> 8) % 8), fill=white)
        for k in range(7):
            x, h = 3 + k * 4 + (k & 1), 6 + (k * 5) % 7
            d.line((x, oy + 20 - h, x, oy + 19), fill=dgreen)
            d.point((x, oy + 19 - h), fill=orange)
        bx, by = W - 26, oy + 2
        blink = (frame % 160) < 6
        for r in range(18):
            for q in range(20):
                ch = BEAST[r][q if q < 10 else 19 - q]
                if ch == '.':
                    continue
                anger = any(tb.flash[:3]) or tb.roar
                c = {'F': teal, 'G': green, 'g': dgreen, 'Y': green if blink else (red if anger else yel), 'W': white}.get(ch, (0, 0, 0))
                if tb.roar and 11 <= r <= 13 and ch in 'kW':
                    c = red if (r == 12 and tb.roar & 4) else (0, 0, 0)
                d.point((bx + q, by + r), fill=c)
        for x in range(2, W - 2):
            if (x + (frame >> 2)) % 6 < 4:
                d.point((x, oy + 19 + ((x + (frame >> 3)) & 1)), fill=teal)
        lb = logo_bits()
        lx, ly = (W - 80) // 2 - 14, oy + 2
        for ps in range(2):
            for r in range(16):
                sh = (15 - r) // 3
                c = (0, 0, 0) if ps == 0 else yel if r < 6 else (150, 230, 0) if r < 11 else green
                for x in range(80):
                    if lb[x, r]:
                        d.point((lx + x + sh + (0 if ps else 1), ly + r + (0 if ps else 1)), fill=c)
                        if ps and r == 15 and (x * 7) % 5 == 0:
                            d.line((lx + x + sh, ly + 16, lx + x + sh, ly + 16 + (x * 3) % 3), fill=green)
        d.rectangle((W // 2 - 50, oy + 21, W // 2 + 49, oy + 32), outline=green)
        for k in range(2):
            gx = W // 2 + 42 if k else W // 2 - 50
            for f in range(3):
                d.line((gx + f * 3, oy + 18, gx + f * 3, oy + 21), fill=green)
                d.point((gx + f * 3, oy + 22), fill=white)
            d.line((gx - 1, oy + 18, gx + 7, oy + 18), fill=dgreen)
        n = str(tb.score)
        b = ''
        for i, ch in enumerate(n):
            b += ch
            if (len(n) - i - 1) % 3 == 0 and i < len(n) - 1:
                b += '.'
        GB.tiny(d, (W - (len(b) * 4 - 1)) // 2, oy + 22, b, (40, 255, 80))
        GB.tiny(d, W // 2 - 46, oy + 27, 'BALL 1', yel)
        GB.tiny(d, W // 2 + 39, oy + 27, 'X%d' % tb.mult, yel)

    def P(x, y):
        return v.X(x, y), v.Y(y)

    def R(r, y):
        return max(1, round(r * v.s(y)))

    def line(x0, y0, x1, y1, c):
        d.line((*P(x0, y0), *P(x1, y1)), fill=c)

    def dot(x, y, r, c, outline=False):
        X, Y = P(x, y)
        rr = R(r, y)
        if outline:
            d.ellipse((X - rr, Y - rr, X + rr, Y + rr), outline=c)
        else:
            d.ellipse((X - rr, Y - rr, X + rr, Y + rr), fill=c)
    cx, cy = P(28, 51)
    rx, ry = R(7, 51), R(4.5, 51)
    glow = any(tb.tgt) and ((frame >> 3) & 1)
    d.ellipse((cx - rx - 3, cy - ry - 3, cx + rx + 3, cy + ry + 3), fill=orange if glow else dgreen)
    d.ellipse((cx - rx - 2, cy - ry - 2, cx + rx + 2, cy + ry + 2), fill=green)
    for k in range(-rx, rx + 1, 3):
        d.point((cx + k, cy - ry - 2), fill=dgreen)
    if (frame % 200) < 8:
        d.line((cx - rx, cy, cx + rx, cy), fill=dgreen)
    else:
        d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=orange)
        d.ellipse((cx - rx + 2, cy - ry + 1, cx + rx - 2, cy + ry - 1), fill=yel)
        w = max(1, rx // 6)
        d.rectangle((cx - w, cy - ry + 1, cx + w, cy + ry - 1), fill=(0, 0, 0))
        d.point((cx - rx // 2, cy - ry // 2), fill=white)
        d.point((cx - rx // 2 + 1, cy - ry // 2), fill=white)
    BX = (12, 10, 46, 47, 20, 36, 17, 39, 15, 41, 28, 24, 32)
    for i in range(13):
        y = 64 - math.fmod(frame * .08 + i * 4.7, 46)
        dot(BX[i] + math.sin(frame * .05 + i) * .8, y, .9, (0, 220, 255), outline=True)
    for x, y in ((13, 46), (43, 46), (28, 72)):
        dot(x, y, 1, green)
    for i in range(3):
        dot(22 + i * 6, 7, 1, (0, 220, 255) if ((frame >> 2) + i) % 3 == 0 else (0, 0, 150))
    for sg in SEG:
        a, b_ = P(sg[0], sg[1])
        c, e = P(sg[2], sg[3])
        d.line((a, b_, c, e), fill=green)
        d.line((a + (-1 if sg[0] < 32 else 1), b_, c + (-1 if sg[2] < 32 else 1), e), fill=dgreen)
    line(1, 75, 12, 82.6, orange)
    line(55, 75, 44, 82.6, orange)
    for i, k0 in enumerate(KICK):
        A, B, C = P(k0[0], k0[1]), P(k0[2], k0[3]), P(51 if i else 5, 70)
        fl = tb.flash[3 + i]
        d.polygon([A, B, C], fill=white if fl else (150, 150, 160))
        ma, mb = (A[0] + B[0] + C[0]) // 3, (A[1] + B[1] + C[1]) // 3
        d.polygon([(ma, mb - 4), (ma - 3, mb + 3), (ma + 3, mb + 3)], fill=dgreen)
        d.line((*A, *B), fill=yel if fl else white)
        d.line((A[0] + (1 if i else -1), A[1], B[0] + (1 if i else -1), B[1]), fill=yel if fl else white)
    for i, (bx, by, br) in enumerate(BUMP):
        x, y = P(bx, by)
        r = max(2, R(br, by))
        f = tb.flash[i]
        if f:
            rr = r + 1 + (8 - tb.flash[i]) // 2
            d.ellipse((x - rr, y - rr, x + rr, y + rr), outline=white if tb.flash[i] & 2 else teal)
        d.ellipse((x - r, y - r, x + r, y + r), fill=white if f else green, outline=dgreen)
        d.polygon([(x, y), (x + r, y - r // 2), (x + r, y + r // 3)], fill=(0, 0, 0))
        if r >= 3:
            q = max(1, r // 2)
            d.ellipse((x - q, y - q, x + q, y + q), fill=yel if f else pink)
            q = max(1, r // 4)
            d.ellipse((x - q, y - q, x + q, y + q), fill=yel)
    for (px_, py_, pr) in POST:
        X, Y = P(px_, py_)
        r = max(1, R(pr, py_))
        d.ellipse((X - r, Y - r, X + r, Y + r), fill=white)
        if r >= 2:
            d.ellipse((X - r + 1, Y - r + 1, X + r - 1, Y + r - 1), fill=orange)
    X, Y = P(28, 19)
    r = max(2, R(2.6, 19))
    d.ellipse((X - r - 1, Y - r - 1, X + r + 1, Y + r + 1), fill=orange if tb.hold else green)
    d.ellipse((X - r, Y - r, X + r, Y + r), fill=(0, 0, 0))
    for k in range(-r + 1, r, 2):
        d.point((X + k, Y - r + 1), fill=white)
        d.point((X + k, Y + r - 1), fill=white)
    X, Y = P(tb.fx, 62)
    u = max(1, R(1.0, 62))
    dr = 1 if tb.fd > 0 else -1
    fc = white if tb.fflash else orange
    d.ellipse((X - u - 1, Y - u - 1, X + u + 1, Y + u + 1), fill=fc)
    d.polygon([(X - dr * (u + 1), Y), (X - dr * (u + 4), Y - u - 1), (X - dr * (u + 4), Y + u + 1)], fill=fc)
    d.point((X + dr * u, Y - 1), fill=(0, 0, 0))
    for i, t in enumerate(TGT):
        (x0, y0), (x1, _) = P(t[0], t[1]), P(t[2], t[3])
        u = max(1, R(.8, t[1]))
        if tb.tgt[i]:
            d.rectangle((x0, y0 - u, x1, y0 + u), fill=orange)
            d.line((x0, y0 - u, x1, y0 - u), fill=white)
            d.line((x0, y0 + u, x1, y0 + u), fill=(160, 0, 0))
        else:
            d.line((x0, y0, x1, y0), fill=dgreen)
    for i in range(5):
        dot(20 + i * 4, 66 - (1 if i in (0, 4) else 0), 1.1, (40, 255, 80) if i < tb.mult else dgreen)
    for f in range(2):
        an = tb.ar if f else tb.al
        px_, py_ = PIV[f]
        ux, uy = (-math.cos(an) if f else math.cos(an)), math.sin(an)
        nx, ny = -uy, ux
        tx, ty = px_ + ux * FL, py_ + uy * FL
        A, B = P(px_ + nx * 1.7, py_ + ny * 1.7), P(px_ - nx * 1.7, py_ - ny * 1.7)
        C, D = P(tx + nx * .8, ty + ny * .8), P(tx - nx * .8, ty - ny * .8)
        d.polygon([A, B, D, C], fill=white)
        d.line((*A, *C), fill=dgreen)
        d.line((*B, *D), fill=dgreen)
        X, Y = P(px_, py_)
        q = max(1, R(1.2, py_))
        d.ellipse((X - q, Y - q, X + q, Y + q), fill=green)
    for (bxx, byy) in [(tb.bx, tb.by)] + list(extra):
        X, Y = P(bxx, byy)
        r = max(1, R(1.7, byy))
        d.ellipse((X - r, Y - r, X + r, Y + r), fill=(150, 155, 170))
        if r >= 2:
            d.ellipse((X - r, Y - r, X + r - 2, Y + r - 2), fill=(220, 225, 235))
        d.point((X - 1, Y - 1), fill=white)
    if tb.popt:
        X, Y = P(tb.popxy[0], tb.popxy[1] - (22 - tb.popt) * .15)
        GB.tiny(d, X - (len(tb.pop) * 4 - 1) // 2, Y, tb.pop, white if tb.popt & 4 else yel)
    if cam > v.gh - 8:
        d.rectangle((0, 0, W - 1, 6), fill=(0, 0, 0))
        GB.tiny(d, 2, 1, str(tb.score), (40, 255, 80))
        GB.tiny(d, W - 9, 1, 'X%d' % tb.mult, yel)
    return im


if __name__ == '__main__':
    tb = Table()
    v = View()
    frames = []
    for f in range(1500):
        tb.step()
        v.follow(tb)
        if f % 3 == 0 and 40 <= f < 40 + 3 * 160:
            frames.append(GB.leds(render(tb, v, f), P=5))
    print('score %d, multiplicateur x%d, pertes %d' % (tb.score, tb.mult, tb.drains))
    pal = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    out = os.path.join(DOC, 'jeu-pincab.gif')
    q[0].save(out, save_all=True, append_images=q[1:], duration=75, loop=0, optimize=True)
    # planche : vue en haut (fronton), au milieu, en bas
    tb2 = Table()
    shots = []
    for f in range(1400):
        tb2.step()
    for camf, label in ((0.0, 'Fronton : la creature'), (0.5, 'Multiball'), (1.0, 'Flippers et lanceur')):
        vv = View()
        vv.cam = camf * max(0, vv.scene - vv.H)
        im = GB.leds(render(tb2, vv, 40, extra=((20, 30), (40, 58)) if camf == 0.5 else ()), P=5)
        pg = Image.new('RGB', (im.width, im.height + 40), (16, 16, 22))
        pg.paste(im, (0, 40))
        ImageDraw.Draw(pg).text((10, 8), label, fill=(255, 220, 0), font=GB.font(22, True))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 3, h), (16, 16, 22))
    for i, sh in enumerate(shots):
        sheet.paste(sh, (i * w, 0))
    sheet.save(os.path.join(DOC, '..', 'build', 'render', 'pincab-rendus.png'))
    print(out, len(frames), 'images')
