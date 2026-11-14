"""GIF anime : arrivee des ennemis en vol (trajectoires de games.ino, meme calcul Catmull-Rom).

Sortie : docs/arrivee-ennemis.gif (ecran 128x128, 2 dalles). Les trajectoires sont lues dans games.ino.
"""
import os
import re

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)
Screen, SPR_E, SPR_SHIP, SPR_LIFE, PAL, SRC = ns['Screen'], ns['SPR_E'], ns['SPR_SHIP'], ns['SPR_LIFE'], ns['PAL'], ns['SRC']

pts = [int(v) for v in re.findall(r'-?\d+', re.search(r'G_PATH\[2\]\[G_PATH_PTS\]\[2\] = \{(.*?)\};', SRC, re.S).group(1))]
NP = int(re.search(r'#define G_PATH_PTS (\d+)', SRC).group(1))
SEG = int(re.search(r'#define G_PATH_SEG (\d+)', SRC).group(1))
PATH = [[(pts[(k * NP + i) * 2], pts[(k * NP + i) * 2 + 1]) for i in range(NP)] for k in range(2)]
W = H = 128
GW, GH = 9, 7


def path_pos(kind, mirror, t):
    seg = t // SEG
    if seg >= NP - 1:
        return None
    u = (t % SEG) / SEG
    P = PATH[kind]
    i0, i1, i2, i3 = max(seg - 1, 0), seg, seg + 1, min(seg + 2, NP - 1)
    c = [0.5 * (2 * P[i1][k] + (-P[i0][k] + P[i2][k]) * u + (2 * P[i0][k] - 5 * P[i1][k] + 4 * P[i2][k] - P[i3][k]) * u * u +
                (-P[i0][k] + 3 * P[i1][k] - 3 * P[i2][k] + P[i3][k]) * u ** 3) for k in range(2)]
    hs = min(H, W * 1.2)
    x = c[0] * W / 100 - GW / 2
    if mirror:
        x = W - GW - x
    return x, c[1] * hs / 100


# formation (comme gNewWave : 5 rangees, meduses une place sur deux)
types = [0, 1, 1, 2, 2]
cols = max(4, min(20, (W - 6) // 11))
en = []
for r, t in enumerate(types):
    for c in range(cols):
        if t == 0 and c % 2:
            continue
        en.append({'row': r, 'col': c, 'type': t, 'state': 3, 'x': -50, 'y': -50})
gs = max(8, (len(en) + 5) // 6)
for i, e in enumerate(en):
    g = i // gs
    e['mirror'], e['path'], e['t'] = g & 1, (g // 2) & 1, -((g // 2) * 60 + (i % gs) * 6)

frames, trail = [], []
for f in range(560):
    sway = __import__('math').sin(f * 0.03) * max(1, min(6, (W - cols * 11) // 2 - 1))
    for e in en:
        sx, sy = (W - cols * 11) // 2 + 1 + e['col'] * 11 + sway, 13 + e['row'] * 9
        if e['state'] == 3:
            e['t'] += 1
            if e['t'] < 0:
                continue
            p = path_pos(e['path'], e['mirror'], e['t'])
            if p:
                e['x'], e['y'] = p
            else:
                e['state'] = 2
        elif e['state'] == 2:
            dx, dy = sx - e['x'], sy - e['y']
            e['x'] += max(-2.2, min(2.2, dx * 0.1))
            e['y'] += max(-2.2, min(2.2, dy * 0.1))
            if abs(dx) < 0.6 and abs(dy) < 0.6:
                e['state'] = 0
        else:
            e['x'], e['y'] = sx, sy
    if f % 4:
        continue
    s = Screen(W, H)
    for e in en:
        if e['state'] == 3 and e['t'] > 0:
            trail.append((int(e['x'] + 4), int(e['y'] + 3), f))
    trail = [p for p in trail if f - p[2] < 24]
    for x, y, t0 in trail:  # trainee pour montrer la trajectoire (pas dans le jeu)
        k = 1 - (f - t0) / 24
        s.px(x, y, (int(60 * k), int(60 * k), int(90 * k)))
    for e in en:
        if e['y'] < -20:
            continue
        fr = (f // 5) & 1 if e['state'] == 3 else ((f // 16) + e['col']) & 1
        s.sprite(SPR_E[e['type']][fr], int(e['x']), int(e['y']))
    s.sprite(SPR_SHIP[0], W // 2 - 4, H - 15)
    if f < 100:
        s.text(H // 2 - 22, 'STAGE 1', (255, 220, 0))
    ns['hud_top'](s, W, 0, 3)
    # LED simulees
    P = 4
    out = Image.new('RGB', (W * P + 20, H * P + 50), (12, 12, 16))
    d = ImageDraw.Draw(out)
    d.text((10, 10), 'Arrivee des ennemis en vol (apercu, trainee ajoutee)', fill=(200, 200, 215), font=ns['font'](15, True))
    for y in range(H):
        for x in range(W):
            col = s.im.getpixel((x, y))
            if sum(col) < 12:
                col = (22, 22, 28)
            d.rectangle((10 + x * P, 40 + y * P, 10 + x * P + P - 2, 40 + y * P + P - 2), fill=col)
    d.line((10, 40 + 64 * P - 1, 10 + W * P, 40 + 64 * P - 1), fill=(60, 60, 75), width=2)
    frames.append(out)
    if all(e['state'] == 0 for e in en):
        for _ in range(6):
            frames.append(out)
        break

pal = frames[len(frames) // 2].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
path = os.path.join(DOC, 'arrivee-ennemis.gif')
q[0].save(path, save_all=True, append_images=q[1:], duration=100, loop=0, optimize=True)
print(path, len(frames), 'images', os.path.getsize(path) // 1024, 'Ko')
