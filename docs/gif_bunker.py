"""BUNKER 3D : GIF du jeu sur une dalle 128x64, avec le meme moteur que le firmware (lancer de rayons, textures
calculees, sol et plafond en perspective, objets, arme, barre d'etat).

  python docs/gif_bunker.py   -> docs/jeu-bunker.gif
"""
import math
import os
import re

from PIL import Image, ImageDraw, ImageFont

DOC = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
W, H = 128, 64
HUDH = 8
VH = H - HUDH


def arr(name, n):
    body = re.search(re.escape(name) + r'[^=]*=\s*\{(.*?)\};', SRC, re.S).group(1)
    return re.findall(r'"([^"]*)"', body)


SCRAP, COLUMN, BARREL = (arr(n, 16) for n in ('ZS_SCRAP[16]', 'ZS_COLUMN[16]', 'ZS_BARREL[16]'))
DRONE, SENT, BRUTE = (arr(n, 32)[:16] for n in ('ZS_DRONE[2][16]', 'ZS_SENT[2][16]', 'ZS_BRUTE[2][16]'))
items = arr('ZS_ITEM[5][8]', 40)
ITEM = [items[i * 8:(i + 1) * 8] for i in range(5)]
guns = arr('ZS_GUN[2][16]', 32)
GUN = [guns[:16], guns[16:]]
PALC = "kSscRrWovyGgbhd"
PAL = [(18, 18, 22), (150, 155, 170), (95, 100, 115), (80, 230, 255), (255, 40, 30), (150, 20, 20), (255, 255, 255), (255, 140, 0),
       (120, 95, 70), (255, 210, 0), (60, 200, 70), (85, 95, 60), (60, 120, 255), (210, 215, 230), (62, 65, 78)]


def pal(ch):
    i = PALC.find(ch)
    return PAL[i if i >= 0 else 0]


def zhash(x):
    x &= 0xFFFFFFFF
    x ^= x >> 16; x = (x * 0x7feb352d) & 0xFFFFFFFF
    x ^= x >> 15; x = (x * 0x846ca68b) & 0xFFFFFFFF
    x ^= x >> 16
    return x


def texel(t, u, v, wid, frame):  # zTexel du firmware
    px, py = int(u * 32), int(v * 32)
    n = (zhash(wid * 1031 + py * 37 + px) & 31) - 16
    if t == 1:
        row, iy = py // 5, py % 5
        bx = px + (row & 1) * 5
        col, ix = bx // 10, bx % 10
        bh = zhash(wid * 97 + row * 13 + col)
        if ix == 9 or iy == 4:
            r, g, b = 62 + n * 0.3, 50, 42
        else:
            k = 0.82 + (bh & 15) / 50
            r, g, b = 158 * k + n, 92 * k + n * 0.6, 58 * k + n * 0.4
            if iy == 0 or ix == 0:
                r, g, b = r + 32, g + 22, b + 14
            elif iy == 3 or ix == 8:
                r, g, b = r * .75, g * .75, b * .75
            if (bh >> 8) % 11 == 0:
                r, g, b = r * .62, g * .58, b * .55
    elif t == 2:
        ix, iy = px % 16, py % 16
        c = 118 + n * 0.8 + (8 if iy < 8 else 0)
        if ix == 0 or iy == 0:
            c = 52
        elif ix == 1 or iy == 1:
            c = 180
        elif ix == 15 or iy == 15:
            c = 82
        if ix in (3, 12) and iy in (3, 12):
            c = 68
        if ix in (2, 11) and iy in (2, 11):
            c = 235
        r, g, b = c * .95, c, c * 1.1
        if zhash(wid) & 3 == 0 and 13 <= py <= 18:
            hz = ((px + py) // 3) & 1
            r, g, b = (230, 180, 20) if hz else (30, 30, 30)
    elif t == 3:
        r, g, b = 25 + n * .5, 45 + n, 95 + n
        if px < 2 or px > 29 or py < 2 or py > 29:
            r, g, b = (125, 130, 145) if (px == 0 or py == 0) else (55, 60, 75)
        elif 6 <= py <= 13 and 5 <= px <= 26:
            r, g, b = 5, 30, 15
            if py % 2 == 0 and (zhash(wid + py * 7 + (frame >> 4)) >> (px & 15)) & 1:
                r, g, b = 50, 235, 100
        elif py in (20, 21):
            on = (px + (frame >> 1)) % 12 < 3
            r, g, b = (255, 130, 40) if on else (40, 70, 160)
        elif 24 <= py <= 27 and px % 4 == 0:
            r, g, b = 10, 15, 30
    elif t == 4:
        row, iy = py // 8, py % 8
        bx = px + zhash(wid + row * 7) % 8
        col, ix = bx // 8, bx % 8
        c = 92 + (zhash(wid * 31 + row * 5 + col) & 31)
        if iy == 7 or ix == 7:
            c = 42
        elif iy == 0 or ix == 0:
            c += 30
        c += n * 0.6
        r, g, b = c * .86, c * .9, c * .8
        if py > 22 and zhash(px * 3 + wid) % 9 < py - 22:
            r, g, b = r * .55, g * .55 + 55, b * .5
    elif t in (5, 6):
        c = 125 + n * .6
        if py % 6 == 0:
            c = 178
        elif py % 6 == 5:
            c = 70
        if px in (15, 16):
            c = 40
        r, g, b = c, c + 2, c + 10
        if 13 <= py <= 18:
            hz = ((px + py) // 3) & 1
            if t == 5:
                r, g, b = (245, 190, 0) if hz else (25, 25, 25)
            else:
                r, g, b = (230, 25, 25) if hz else (70, 10, 10)
        if px in (3, 28) and 4 <= py <= 7:
            on = 1 if (frame >> 3) & 1 else 0.4
            r, g, b = (255 if t == 6 else 40) * on, (40 if t == 6 else 255) * on, 40 * on
    else:
        r, g, b = 15, 110 + n, 35
        if px < 2 or px > 29 or py < 2 or py > 29:
            on = (px + py + (frame >> 1)) % 6 < 3
            r, g, b = (255, 255, 120) if on else (30, 80, 30)
        ax = abs(px - 16)
        if (6 <= py <= 14 and ax <= py - 6) or (14 < py <= 25 and ax <= 2):
            r, g, b = 230, 255, 230
    if t in (1, 4) and zhash(wid + 5) % 4 == 0:
        du, dv = u - .5, v - .34
        d = math.sqrt(du * du + dv * dv * 1.4)
        if abs(du) < .07 and -.06 < dv < .05:
            r, g, b = 255, 235, 160
        elif abs(du) < .1 and .05 <= dv < .09:
            r, g, b = 70, 60, 50
        elif d < .35:
            k = (.35 - d) / .35
            r, g, b = r + 120 * k, g + 85 * k, b + 30 * k
    return r, g, b


# carte d'exemple : salles en briques, metal, technique, pierre ; portes ; sortie
MAP = [
    "1111111111111111",
    "1000000100000003",
    "1000000000000003",
    "1000000100000003",
    "1111511122222223",
    "4000004000000003",
    "4000004000000007",
    "4000005000000003",
    "4000004000000003",
    "4444444333333333",
]
GRID = [[int(c) for c in row] for row in MAP]


def wall(x, y):
    return not (0 <= x < len(GRID[0]) and 0 <= y < len(GRID)) or GRID[y][x] != 0


def render(px_, py_, a, frame, sprites, weapon=0, flash=0, bob=0.0):
    im = Image.new('RGB', (W, H))
    pix = im.load()
    plane = min(1.0, W / (2.0 * VH * 1.35))
    K = W / (2 * plane)
    dx, dy = math.cos(a), math.sin(a)
    plx, ply = -dy * plane, dx * plane
    horizon = VH // 2 + int(math.sin(bob) * 1.2)
    inv = 1 / (plx * dy - dx * ply)
    spr = []
    for (sx, sy, hgt, lift, art, aw, ah, kind) in sprites:
        rx, ry = sx - px_, sy - py_
        tx, ty = inv * (dy * rx - dx * ry), inv * (-ply * rx + plx * ry)
        if ty < .15:
            continue
        h = max(1, int(K * hgt / ty))
        spr.append((ty, int(W / 2 * (1 + tx / ty)), max(1, h * aw // ah), h, horizon + int(K * .5 / ty) - h - int(K * lift / ty), art, aw, ah, kind))
    spr.sort(key=lambda s: -s[0])
    lights = [(sx, sy, 20, 80, 30, 1.6) for (sx, sy, hgt, lift, art, aw, ah, kind) in sprites if kind == 1]
    if flash > 1:
        lights.append((px_ + dx * .5, py_ + dy * .5, 55, 40, 15, 2.5))

    def light_at(x0, y0):
        r = g = b = 0
        for lx, ly, lr, lg, lb, rad in lights:
            dd = math.hypot(x0 - lx, y0 - ly)
            if dd < rad:
                k = 1 - dd / rad
                r, g, b = r + lr * k, g + lg * k, b + lb * k
        return r, g, b
    gs, gw, gh = 1, 28, 16
    gx, gy = W // 2 - gw // 2 + int(math.sin(bob * .5) * 2), VH - gh + 2 + int(abs(math.cos(bob * .5)) * 2)
    light = 1.25 if flash > 2 else 1.0
    for x in range(W):
        cam = 2 * x / W - 1
        rdx, rdy = dx + plx * cam, dy + ply * cam
        mx, my = int(px_), int(py_)
        stx, sty = (-1 if rdx < 0 else 1), (-1 if rdy < 0 else 1)
        ddx, ddy = abs(1 / (rdx + 1e-6)), abs(1 / (rdy + 1e-6))
        sdx = (px_ - mx if rdx < 0 else mx + 1 - px_) * ddx
        sdy = (py_ - my if rdy < 0 else my + 1 - py_) * ddy
        side, hit = 0, 0
        while not hit:
            if sdx < sdy:
                sdx += ddx; mx += stx; side = 0
            else:
                sdy += ddy; my += sty; side = 1
            if wall(mx, my):
                hit = GRID[my][mx] if 0 <= my < len(GRID) and 0 <= mx < len(GRID[0]) else 1
        dist = max(.05, (sdx - ddx) if side == 0 else (sdy - ddy))
        hx = py_ + dist * rdy if side == 0 else px_ + dist * rdx
        wx = hx - math.floor(hx)
        lh = int(K / dist)
        top = horizon - lh // 2
        fog = light / (1 + dist * dist * .035)
        shade = (0.72 if side else 1.0) * fog
        wid = mx * 131 + my * 71 + side * 7
        lr, lg, lb = light_at(px_ + rdx * dist, py_ + rdy * dist)
        vgx = ((x - W / 2) / (W / 2)) ** 2 * .28
        col = []
        for y in range(VH):
            if top <= y < top + lh:
                tv = (y - top + .5) / lh
                ao = min(tv, 1 - tv)
                r, g, b = texel(min(hit, 7), wx, tv, wid, frame)
                s = shade * (.55 + ao * 3.75 if ao < .12 else 1)
                if r > 200 and g > 200:
                    s = max(s, .85)
                r, g, b = (r + lr) * s, (g + lg) * s, (b + lb) * s
            else:
                rd = (K * .5) / abs(y - horizon + .5)
                fx, fy = px_ + rdx * rd, py_ + rdy * rd
                fg = light / (1 + rd * rd * .035)
                cx, cy = math.floor(fx), math.floor(fy)
                ux, uy = fx - cx, fy - cy
                n = zhash(cx * 73 + cy * 151 + (y > horizon) * 7) & 15
                if y > horizon:
                    sx2, sy2 = fx * 2 - math.floor(fx * 2), fy * 2 - math.floor(fy * 2)
                    tn = zhash(math.floor(fx * 2) * 57 + math.floor(fy * 2) * 113) & 15
                    v = 40 if (sx2 < .05 or sy2 < .05) else (105 + tn if (sx2 < .12 or sy2 < .12) else 78 + tn + n / 2)
                    wl = 1
                    if ux < .25 and wall(cx - 1, cy):
                        wl = min(wl, .55 + ux * 1.8)
                    if ux > .75 and wall(cx + 1, cy):
                        wl = min(wl, .55 + (1 - ux) * 1.8)
                    if uy < .25 and wall(cx, cy - 1):
                        wl = min(wl, .55 + uy * 1.8)
                    if uy > .75 and wall(cx, cy + 1):
                        wl = min(wl, .55 + (1 - uy) * 1.8)
                    r, g, b = v * wl, v * wl, (v + 7) * wl
                else:
                    lamp = cx % 3 == 1 and cy % 3 == 1 and .3 < ux < .7 and .3 < uy < .7
                    ld = (ux - .5) ** 2 + (uy - .5) ** 2
                    if lamp:
                        r, g, b = 255, 245, 210
                        fg = max(fg, .75)
                    else:
                        v = 25 if (ux < .06 or uy < .06) else 48 + n / 2
                        r, g, b = v, v, v + 4
                        if cx % 3 == 1 and cy % 3 == 1 and ld < .25:
                            k = (.25 - ld) * 4
                            r, g, b = r + 70 * k, g + 65 * k, b + 45 * k
                if lights and rd < 8:
                    a_, c_, e_ = light_at(fx, fy)
                    r, g, b = r + a_ * .6, g + c_ * .6, b + e_ * .6
                r, g, b = r * fg, g * fg, b * fg
            vg = 1 - vgx - ((y - VH / 2) / (VH / 2)) ** 2 * .28
            r, g, b = r * vg, g * vg, b * vg
            col.append((r, g, b))
        for (ty, ssx, sw, sh, stop, art, aw, ah, kind) in spr:
            if ty >= dist:
                continue
            x0 = ssx - sw // 2
            if not x0 <= x < x0 + sw:
                continue
            f2 = light / (1 + ty * ty * .035)
            if kind == 1:
                u = (x - ssx) / (sw / 2 + .01)
                for y in range(max(0, stop), min(VH, stop + sh)):
                    vv = (y - stop - sh / 2) / (sh / 2 + .01)
                    d2 = u * u + vv * vv
                    if d2 <= 1:
                        col[y] = (255, 255, 255) if d2 < .35 else (120, 255, 120)
                continue
            ax = (x - x0) * aw // sw
            for y in range(max(0, stop), min(VH, stop + sh)):
                ch = art[(y - stop) * ah // sh][ax]
                if ch == '.':
                    continue
                f = max(f2, .8) if ch in 'cRW' else f2
                c = pal(ch)
                col[y] = (c[0] * f, c[1] * f, c[2] * f)
        if gx <= x < gx + gw:
            ax = x - gx
            for y in range(max(0, gy), VH):
                ay = y - gy
                if ay < 16 and GUN[weapon][ay][ax] != '.':
                    col[y] = pal(GUN[weapon][ay][ax])
            if flash > 1:
                fx_, fy_, rr = gx + gw // 2, gy - 2, flash + 2
                for y in range(max(0, fy_ - rr), min(VH, fy_ + rr + 1)):
                    d2 = (x - fx_) ** 2 + (y - fy_) ** 2
                    if d2 <= rr * rr:
                        col[y] = (255, 255, 220) if d2 <= rr * rr / 3 else (255, 170, 30)
        if abs(x - W // 2) <= 1:
            col[horizon] = (255, 255, 255)
            if x == W // 2:
                col[horizon - 2] = col[horizon + 2] = (255, 255, 255)
        for y in range(VH):
            pix[x, y] = tuple(max(0, min(255, int(c))) for c in col[y])
    d = ImageDraw.Draw(im)
    d.rectangle((0, VH, W - 1, H - 1), fill=(36, 36, 44))
    d.line((0, VH, W - 1, VH), fill=(110, 110, 125))
    for xx, c in ((1, (255, 50, 40)), (22, (60, 140, 255)), (43, (255, 210, 0))):
        d.rectangle((xx, VH + 2, xx + 4, VH + 6), fill=c)
    return im


def leds(img, P=6):
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


def font(size, bold=False):
    try:
        return ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf' if bold else 'C:/Windows/Fonts/segoeui.ttf', size)
    except OSError:
        return ImageFont.load_default()


def page(panel, lines):
    im = Image.new('RGB', (panel.width + 330, panel.height + 110), (16, 16, 22))
    d = ImageDraw.Draw(im)
    d.text((20, 16), 'BUNKER 3D', fill=(235, 235, 245), font=font(24, True))
    d.text((20, 50), 'Jeu de tir en vue subjective dans une base souterraine (128x64).', fill=(150, 150, 175), font=font(16))
    im.paste(panel, (20, 90))
    for k, t in enumerate(lines):
        hdr = t in ('Commandes :', 'Regles :')
        d.text((panel.width + 40, 110 + k * 30), t, fill=(255, 220, 0) if hdr else (200, 200, 215), font=font(19, hdr))
    return im


if __name__ == '__main__':
    LINES = ['Commandes :', 'stick / croix : avancer', 'stick droit : pivoter', 'L1 / R1 : pas de cote', 'X ou R2 : tirer',
             'carre : porte, sortie', 'rond : arme, triangle : carte', 'Regles :', 'carte rouge -> sortie', 'barils explosifs']
    frames = []
    # trajet : salle en briques, porte, couloir de metal, sortie
    path = [(1.6 + t * 0.17, 2.5, 0.3 * math.sin(t * 0.12)) for t in range(0, 62)]
    path += [(12.1, 2.5, t * 0.05) for t in range(0, 30)]
    sprites_base = [(9.5, 1.7, .5, .35, DRONE, 16, 16, 0), (14.3, 3.3, .8, 0, SENT, 16, 16, 0), (11.5, 6.5, .95, 0, BRUTE, 16, 16, 0),
                    (5.5, 1.4, .55, 0, BARREL, 16, 16, 0), (6.1, 1.3, .55, 0, BARREL, 16, 16, 0), (13.6, 1.5, 1.0, 0, COLUMN, 16, 16, 0),
                    (8.5, 3.3, .28, 0, ITEM[0], 8, 8, 0), (11.0, 1.5, .28, 0, ITEM[3], 8, 8, 0)]
    for f, (x, y, a) in enumerate(path):
        if f % 2:
            continue
        sp = list(sprites_base)
        sp.append((14.0 - (f % 30) * .1, 3.2 - (f % 30) * .02, .18, .35, None, 1, 1, 1))  # plasma
        fl = 4 - (f % 12) if f % 12 < 4 else 0
        frames.append(page(leds(render(x, y, a, f, sp, f > 60, fl, f * .28)), LINES))
    pal_ = frames[len(frames) // 2].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    q = [fr.quantize(palette=pal_, dither=Image.Dither.NONE) for fr in frames]
    out = os.path.join(DOC, 'jeu-bunker.gif')
    q[0].save(out, save_all=True, append_images=q[1:], duration=100, loop=0, optimize=True)
    frames[0].save(os.path.join(DOC, '..', 'build', 'bunker_apercu.png'))
    print(out, len(frames), 'images')
