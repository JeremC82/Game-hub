"""1945 : planche des gros avions (bombardier bimoteur en 3 couleurs, les 2 boss), lus dans le firmware.

  python docs/avions_1945.py   -> docs/escadrille-avions.png
"""
import os
import re
import sys

from PIL import Image

DOC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, DOC)
import gif_bunker as GB  # noqa: E402

SRC = open(os.path.join(DOC, '..', 'games', 'games.ino'), encoding='utf-8').read()
BOMBER = re.findall(r'"([^"]*)"', re.search(r'SPR_A_BOMBER\[11\] = \{(.*?)\};', SRC, re.S).group(1))
BOSS = []
for i in range(3):
    blk = re.search(r'SPR_A_BOSS%d\[2\]\[(\d+)\] = \{(.*?)\};' % i, SRC, re.S)
    n = int(blk.group(1))
    BOSS.append(re.findall(r'"([^"]*)"', blk.group(2))[:n])
GV = (((205, 75, 50), (150, 30, 20)), ((190, 190, 205), (150, 150, 160)), ((245, 200, 40), (190, 150, 0)))
HL = ((255, 140, 110), (235, 235, 250), (255, 240, 130))


def col(ch, var):
    return {'W': (255, 255, 255), 'y': (255, 220, 0), 'o': (255, 140, 0), 'c': (120, 220, 255), 'k': (40, 40, 50),
            'G': GV[var % 3][0], 'g': GV[var % 3][1], 'S': (188, 192, 202), 's': (150, 155, 165), 'L': (235, 240, 250),
            'H': HL[var % 3], 'O': (150, 170, 30), 'Q': (200, 220, 80), 'R': (235, 30, 30), 'r': (160, 0, 0),
            'U': (60, 150, 255), 'u': (0, 0, 200)}[ch]


def put(im, spr, x0, y0, var):
    for r, line in enumerate(spr):
        for c, ch in enumerate(line):
            if ch != '.':
                im.putpixel((x0 + c, y0 + r), col(ch, var))


im = Image.new('RGB', (128, 64), (10, 60, 150))
for i in range(3):
    put(im, BOMBER, 4 + i * 21, 3, i)
put(im, BOSS[0], 2, 22, 0)
put(im, BOSS[1], 30, 20, 0)
put(im, BOSS[2], 72, 26, 0)
GB.leds(im, P=6).save(os.path.join(DOC, 'escadrille-avions.png'))
print('ok')
