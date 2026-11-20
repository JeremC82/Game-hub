"""SCARABEE 3D : apercu du labyrinthe en relief (meme dessin que iDraw du firmware).

  python docs/gif_scarabee3d.py   -> docs/scarabee3d-rendus.png (3 vues 128x128) et docs/jeu-scarabee3d.gif
"""
import math
import os
import re
import sys

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402  (leds, police 3x5)

SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
MAZE = re.findall(r'"([^"]*)"', re.search(r'L_MAZE\[L_ROWS\] = \{(.*?)\};', SRC, re.S).group(1))
R, C = len(MAZE), len(MAZE[0])
TW, TD, SK, WH = 10, 7, 4, 5
WC = [(255, 150, 0), (0, 200, 255), (230, 60, 230), (60, 230, 90)]
FC = [(0, 0, 150)] * 4
EC = [(200, 60, 255), (255, 140, 0), (170, 230, 40), (230, 230, 230)]
WHITE, YEL = (255, 255, 255), (255, 220, 0)


def wall(r, c):
    return 0 <= r < R and 0 <= c < C and MAZE[r][c] == '#'


class Cv:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.im = Image.new('RGB', (W, H))
        self.px = self.im.load()

    def p(self, x, y, col):
        if 0 <= x < self.W and 0 <= y < self.H:
            self.px[x, y] = col

    def rect(self, x, y, w, h, col):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.p(xx, yy, col)


def mul(c, n, d):
    return tuple(v * n // d for v in c)


def add(c, a):
    return tuple(min(255, v + a) for v in c)


def render(st, dots, player, ghosts, frame, score, W=128, H=128):
    cv = Cv(W, H)
    dr = ImageDraw.Draw(cv.im)
    VH = H - 8
    px_, py_, pdx, pdy, jump = player
    MW, MH = C * TW + R * SK, R * TD + WH + 6
    rawx = (px_ / 6) * TW + (R - py_ / 6) * SK
    rawy = 8 + WH + (py_ / 6) * TD
    cx = -(W - MW) / 2 if MW <= W else max(0, min(MW - W, rawx - W / 2))
    cy = -(VH - MH) / 2 if MH <= VH else max(0, min(MH - VH, rawy - 8 - VH / 2))

    def PX(X, Y):
        return X * TW + (R - Y) * SK - cx

    def PY(Y):
        return 8 + WH + Y * TD - cy

    def quad(pts, col):
        q = [(round(x), round(y)) for x, y in pts]
        dr.polygon(q, fill=col)

    wc, fc = WC[st], FC[st]
    top, edge, front, side = wc, add(wc, 120), mul(wc, 2, 3), mul(wc, 3, 5)
    for i in range(36):
        sx = ((i * 53 + 17) * 7 - int(cx * 0.4)) % W
        sy = 8 + ((i * 29 + 5) * 11 - int(cy * 0.4)) % (H - 8)
        cv.p(sx, sy, (150, 150, 170) if (i + frame // 20) % 7 else WHITE)
    SL = 5
    slabF, slabS = mul(wc, 3, 5), tuple(v // 2 + 20 for v in wc)
    quad([(PX(0, 0), PY(0)), (PX(0, R), PY(R)), (PX(0, R), PY(R) + SL), (PX(0, 0), PY(0) + SL)], slabS)
    quad([(PX(0, R), PY(R)), (PX(C, R), PY(R)), (PX(C, R), PY(R) + SL), (PX(0, R), PY(R) + SL)], slabF)
    for k in range(0, C, 2):
        cv.rect(round(PX(k, R)), round(PY(R)) + 1, 1, SL - 1, (0, 0, 0))
    quad([(PX(0, 0), PY(0)), (PX(C, 0), PY(0)), (PX(C, R), PY(R)), (PX(0, R), PY(R))], fc)
    dr.line([(round(PX(0, R)), round(PY(R))), (round(PX(C, R)) - 1, round(PY(R)))], fill=(150, 150, 255))
    M = 0.32
    for r in range(R):
        for c in range(C):
            if MAZE[r][c] != '#':
                continue
            wl, wr, wu, wd = wall(r, c - 1), wall(r, c + 1), wall(r - 1, c), wall(r + 1, c)
            x0, x1 = c + (0 if wl else M), min(C, c + 1 - (0 if wr else M) + .3)
            y0, y1 = r + (0 if wu else M), min(R, r + 1 - (0 if wd else M) + .3)
            quad([(PX(x0, y0) + 2, PY(y0) + 1), (PX(x1, y0), PY(y0) + 1), (PX(x1, y1), PY(y1)), (PX(x0, y1) + 2, PY(y1))], (0, 0, 0))
    for r in range(R):
        for c in range(C):
            if MAZE[r][c] == '#':
                continue
            d = dots.get((r, c), 0)
            bx, by = round(PX(c + .5, r + .5)), round(PY(r + .5))
            if d == 1:
                cv.p(bx + 1, by + 1, (0, 0, 0))
                cv.rect(bx - 1, by - 1, 2, 2, (255, 230, 170))
                cv.p(bx - 1, by - 1, WHITE)
            elif d == 2:
                gc = WHITE if frame & 8 else YEL
                bb = by - 3 - ((frame >> 3) & 1)
                cv.rect(bx - 2, by + 1, 5, 1, (0, 0, 0))
                cv.rect(bx - 1, bb - 1, 3, 3, gc)
                for a, b in ((0, -2), (0, 2), (-2, 0), (2, 0)):
                    cv.p(bx + a, bb + b, gc)
                cv.p(bx - 1, bb - 1, WHITE)
                if frame & 8:
                    for a, b in ((-3, -3), (3, -3), (3, 3), (-3, 3)):
                        cv.p(bx + a, bb + b, YEL)
    dr.line([(round(PX(10, 8.5)), round(PY(8.5))), (round(PX(11, 8.5)) - 1, round(PY(8.5)))], fill=(255, 120, 200))
    for r in range(R):
        for c in range(C - 1, -1, -1):
            if MAZE[r][c] != '#':
                continue
            wl, wr, wu, wd = wall(r, c - 1), wall(r, c + 1), wall(r - 1, c), wall(r + 1, c)
            x0, x1 = c + (0 if wl else M), c + 1 - (0 if wr else M)
            y0, y1 = r + (0 if wu else M), r + 1 - (0 if wd else M)
            h = WH
            if not wl:
                quad([(PX(x0, y0), PY(y0) - h), (PX(x0, y1), PY(y1) - h), (PX(x0, y1), PY(y1)), (PX(x0, y0), PY(y0))], side)
            if not wd:
                quad([(PX(x0, y1), PY(y1) - h), (PX(x1, y1), PY(y1) - h), (PX(x1, y1), PY(y1)), (PX(x0, y1), PY(y1))], front)
            quad([(PX(x0, y0), PY(y0) - h), (PX(x1, y0), PY(y0) - h), (PX(x1, y1), PY(y1) - h), (PX(x0, y1), PY(y1) - h)], top)
            if not wu:
                dr.line([(round(PX(x0, y0)), round(PY(y0) - h)), (round(PX(x1, y0)) - 1, round(PY(y0) - h))], fill=edge)
            if not wl:
                dr.line([(round(PX(x0, y0)), round(PY(y0) - h)), (round(PX(x0, y1)), round(PY(y1) - h))], fill=edge)
            if not wd:
                dr.line([(round(PX(x0, y1)), round(PY(y1)) - 1), (round(PX(x1, y1)) - 1, round(PY(y1)) - 1)], fill=(0, 0, 0))
        for i, (gx, gy, gdx, gdy, fr) in enumerate(ghosts):
            if min(R - 1, int(gy / 6)) != r:
                continue
            sx, sy = round(PX(gx / 6, gy / 6)), round(PY(gy / 6))
            col, hi, dk = EC[i], add(EC[i], 90), mul(EC[i], 2, 3)
            if fr:
                col, hi, dk = (40, 90, 255), (150, 190, 255), (0, 0, 170)
            cv.rect(sx - 3, sy + 1, 7, 1, (0, 0, 0))
            cv.rect(sx - 2, sy + 2, 5, 1, (0, 0, 0))
            by = sy - 4
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    if dx * dx + dy * dy <= 18:
                        cv.p(sx + dx, by + dy, hi if dx + dy <= -4 else dk if dx + dy >= 4 else col)
            if (frame // 6 + i) & 1:
                for a, b in ((-5, 0), (5, 0), (0, -5), (-5, -1), (5, -1), (-1, -5)):
                    cv.p(sx + a, by + b, col)
            else:
                for a, b, cc in ((-4, -4, col), (4, -4, col), (-4, 4, dk), (4, 4, dk)):
                    cv.p(sx + a, by + b, cc)
            if fr:
                cv.rect(sx - 2, by - 2, 2, 2, WHITE)
                cv.rect(sx + 1, by - 2, 2, 2, WHITE)
                cv.rect(sx - 2, by + 2, 5, 1, WHITE)
            else:
                cv.rect(sx - 3 + gdx, by - 2 + gdy, 3, 3, WHITE)
                cv.rect(sx + 1 + gdx, by - 2 + gdy, 3, 3, WHITE)
                ox, oyy = (1 if gdx > 0 else 0), (1 if gdy > 0 else 0)
                cv.rect(sx - 3 + gdx + ox, by - 2 + gdy + oyy, 2, 2, (0, 0, 170))
                cv.rect(sx + 1 + gdx + ox, by - 2 + gdy + oyy, 2, 2, (0, 0, 170))
        if min(R - 1, int(py_ / 6)) == r:
            sx, sy = round(PX(px_ / 6, py_ / 6)), round(PY(py_ / 6))
            jh = int(math.sin(math.pi * (26 - jump) / 26) * 11 + .5) if jump else 0
            sw = 2 if jh > 6 else 3
            cv.rect(sx - sw, sy + 1, sw * 2 + 1, 1, (0, 0, 0))
            cv.rect(sx - sw + 1, sy + 2, sw * 2 - 1, 1, (0, 0, 0))
            by = sy - 4 - jh
            g, hi, dk, head = (40, 220, 70), (190, 255, 190), (0, 150, 30), (20, 60, 25)
            dx, dy = pdx, pdy
            if not dx and not dy:
                dy = 1
            leg = (frame // 4) & 1
            for k in (-1, 1):
                cv.p(sx - 5, by + k * 2 + leg, dk)
                cv.p(sx + 5, by + k * 2 + 1 - leg, dk)
            cv.p(sx - 5, by + 1 - leg, dk)
            cv.p(sx + 5, by + leg, dk)
            for yy in range(-3, 4):
                for xx in range(-4, 5):
                    if xx * xx * 9 + yy * yy * 16 <= 150:
                        cv.p(sx + xx, by + yy, hi if xx + yy <= -4 else dk if xx + yy >= 4 else g)
            if dx:
                cv.rect(sx - 3, by, 7, 1, dk)
            else:
                cv.rect(sx, by - 2, 1, 5, dk)
            cv.rect(sx - 2, by - 2, 2, 1, hi)
            cv.p(sx - 2, by + 1, WHITE)
            cv.p(sx + 2, by - 1, head)
            if dx:
                cv.rect(sx + dx * 5 - (1 if dx < 0 else 0), by - 2, 2, 5, head)
                cv.p(sx + dx * 6, by - 1, WHITE)
                cv.p(sx + dx * 6, by + 1, WHITE)
                cv.p(sx + dx * 7, by - 3, dk)
                cv.p(sx + dx * 7, by + 3, dk)
            else:
                hy = by - 4 if dy < 0 else by + 4
                cv.rect(sx - 2, hy - (1 if dy < 0 else 0), 5, 2, head)
                if dy > 0:
                    cv.p(sx - 1, hy + 1, WHITE)
                    cv.p(sx + 1, hy + 1, WHITE)
    cv.rect(0, 0, W, 8, (0, 0, 0))
    num = str(score)
    lw, nw = 5 * 4 - 1, len(num) * 4 - 1
    x = (W - lw - 4 - nw) // 2
    GB.tiny(dr, x, 1, 'SCORE', (255, 40, 40))
    GB.tiny(dr, x + lw + 4, 1, num, (230, 230, 230))
    for i in range(3):
        lx = W - 5 - i * 6
        cv.rect(lx - 1, 2, 4, 3, (40, 220, 70))
        cv.rect(lx, 1, 2, 1, (40, 220, 70))
        cv.p(lx, 2, (190, 255, 190))
    return cv.im


def all_dots(eaten_rows=()):
    d = {}
    for r in range(R):
        for c in range(C):
            if r in eaten_rows:
                continue
            t = MAZE[r][c]
            if t in '.o':
                d[(r, c)] = 1 if t == '.' else 2
    return d


def ctr(c, r):
    return c * 6 + 3, r * 6 + 3


if __name__ == '__main__':
    shots = []
    scenes = [
        ('Stage 1', 0, all_dots((11, 15, 17)), (*ctr(10, 11), -1, 0, 0),
         [(*ctr(10, 7), 1, 0, False), (*ctr(9, 9), 0, -1, False), (*ctr(4, 3), 0, 1, False), (*ctr(16, 15), -1, 0, False)], 2380),
        ('Saut par-dessus', 1, all_dots((17, 15, 13, 11, 3)), (*ctr(5, 13), 1, 0, 13),
         [(*ctr(6, 13), -1, 0, False), (*ctr(3, 15), 0, -1, False), (*ctr(12, 17), 1, 0, False), (*ctr(17, 5), 0, 1, False)], 5120),
        ('Grosse graine', 2, all_dots((1, 3, 5, 7)), (*ctr(3, 13), 0, -1, 0),
         [(*ctr(5, 11), 0, 1, True), (*ctr(7, 15), -1, 0, True), (*ctr(14, 13), 0, 1, True), (*ctr(9, 9), 0, -1, False)], 9840),
    ]
    for label, st, dots, pl, gh, sc in scenes:
        im = GB.leds(render(st, dots, pl, gh, 3, sc), P=5)
        pg = Image.new('RGB', (im.width, im.height + 40), (16, 16, 22))
        pg.paste(im, (0, 40))
        ImageDraw.Draw(pg).text((10, 8), label, fill=(255, 220, 0), font=GB.font(22, True))
        shots.append(pg)
    w, h = shots[0].size
    sheet = Image.new('RGB', (w * 3, h), (16, 16, 22))
    for i, sh in enumerate(shots):
        sheet.paste(sh, (i * w, 0))
    sheet.save(os.path.join(DOC, 'scarabee3d-rendus.png'))
    # petite animation : le scarabee traverse le couloir du bas en sautant une creature
    frames = []
    dots = all_dots()
    for f in range(90):
        x = 1 * 6 + 3 + f * 1.3
        jump = 26 - (f - 40) if 40 <= f < 66 else 0
        if (17, int(x // 6)) in dots:
            del dots[(17, int(x // 6))]
        gx = 19 * 6 + 3 - f * 0.9
        frames.append(GB.leds(render(0, dots, (x, 17 * 6 + 3, 1, 0, jump), [(gx, 17 * 6 + 3, -1, 0, False), (*ctr(10, 7), 1, 0, False),
                                                                              (*ctr(9, 9), 0, -1, False), (*ctr(11, 9), 0, -1, False)], f, 10 * f), P=4))
    pal = frames[45].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    q[0].save(os.path.join(DOC, 'jeu-scarabee3d.gif'), save_all=True, append_images=q[1:], duration=50, loop=0, optimize=True)
    print('ok')
