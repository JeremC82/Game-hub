"""Dessine docs/branchement-son.png : son par une broche PWM de l'ESP32 vers un ampli analogique.

GPIO 22 -> filtre RC (1 k + 10 nF) -> condensateur de liaison 10 uF -> entree de l'ampli (L et R reliees).
Amplis 7-15 V (NS4110B, PAM8610) : alimentation 12 V a part ou elevateur 5 V -> 12 V ; masse commune.
Option plus simple : PAM8403 directement en 5 V.
"""
import os

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'branchement-son.png')
W, H = 1800, 1100
img = Image.new('RGB', (W, H), (250, 250, 247))
d = ImageDraw.Draw(img)


def font(size, bold=False):
    for name in (('arialbd.ttf' if bold else 'arial.ttf'), 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


F, FB, FS, FT = font(20), font(20, True), font(16), font(30, True)
INK, GREY, RED, BLUE, GREEN, ORANGE = (30, 30, 30), (120, 120, 120), (200, 40, 30), (30, 90, 200), (30, 140, 60), (220, 120, 0)


def box(x0, y0, x1, y1, title, fill=(255, 255, 255)):
    d.rounded_rectangle((x0, y0, x1, y1), 14, fill=fill, outline=INK, width=3)
    d.text(((x0 + x1) // 2, y0 + 22), title, font=FB, fill=INK, anchor='mm')


def text(x, y, s, f=F, col=INK, anchor='la'):
    d.text((x, y), s, font=f, fill=col, anchor=anchor)


def wire(pts, col=INK, w=3):
    d.line(pts, fill=col, width=w, joint='curve')


def dot(x, y, col=INK):
    d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=col)


def gnd(x, y):
    for k, hw in enumerate((20, 13, 6)):
        d.line((x - hw, y + k * 7, x + hw, y + k * 7), fill=INK, width=3)


def resistor_h(x, y, w, label):  # resistance horizontale (rectangle)
    d.rectangle((x, y - 12, x + w, y + 12), outline=GREEN, width=3, fill=(255, 255, 255))
    text(x + w // 2, y - 18, label, FS, GREEN, 'mb')


def cap_h(x, y, label, col=BLUE):  # condensateur en serie (deux traits verticaux)
    d.line((x, y - 22, x, y + 22), fill=col, width=5)
    d.line((x + 14, y - 22, x + 14, y + 22), fill=col, width=5)
    text(x + 7, y - 28, label, FS, col, 'mb')


def cap_v(x, y, label, col=BLUE):  # condensateur vers la masse (deux traits horizontaux)
    d.line((x - 22, y, x + 22, y), fill=col, width=5)
    d.line((x - 22, y + 14, x + 22, y + 14), fill=col, width=5)
    text(x + 30, y + 7, label, FS, col, 'lm')


def speaker(x, y, label):
    d.rectangle((x, y - 22, x + 20, y + 22), outline=INK, width=3, fill=(220, 220, 230))
    d.polygon([(x + 20, y - 22), (x + 55, y - 48), (x + 55, y + 48), (x + 20, y + 22)], outline=INK, fill=(200, 200, 215))
    text(x + 27, y + 62, label, FS, INK, 'mt')


text(W // 2, 30, 'Game hub - branchement du son (une broche PWM de l\'ESP32)', FT, anchor='mt')
text(W // 2, 72, 'Pas d\'I2S : l\'ESP32 sort des notes carrees sur GPIO 22, filtrees puis amplifiees (sans risque pour la manette PS4)',
     FS, GREY, anchor='mt')

# ---------------------------------------------------------------- ESP32
ex0, ey0, ex1, ey1 = 60, 160, 360, 520
box(ex0, ey0, ex1, ey1, 'ESP32 DevKit (jeu)', (236, 242, 250))
py = 290
d.rectangle((ex1, py - 7, ex1 + 14, py + 7), fill=INK)
text(ex1 - 14, py, 'GPIO22', F, INK, 'rm')
gy = 430
d.rectangle((ex1, gy - 7, ex1 + 14, gy + 7), fill=INK)
text(ex1 - 14, gy, 'GND', F, INK, 'rm')
text((ex0 + ex1) // 2, ey1 - 40, '(dalles : voir branchement-dalles.png)', FS, GREY, 'mm')

# ---------------------------------------------------------------- filtre : 1 k, 10 nF a la masse, 10 uF en serie
x = ex1 + 14
wire([(x, py), (460, py)], ORANGE, 4)
resistor_h(460, py, 90, '1 kOhm')
wire([(550, py), (700, py)], INK)
dot(620, py)
wire([(620, py), (620, py + 60)])
cap_v(620, py + 60, '10 nF (filtre)')
wire([(620, py + 74), (620, gy)])
cap_h(700, py, '10 uF (liaison)')
wire([(714, py), (880, py)])
text(420, gy + 90, 'Filtre : adoucit les bords du signal carre.', FS, GREY)
text(420, gy + 114, 'Le 10 uF bloque la tension continue.', FS, GREY)
# masse commune
wire([(ex1 + 14, gy), (880, gy)], INK, 4)
dot(620, gy)

# ---------------------------------------------------------------- ampli : le module (carte verte avec sa puce)
ax0, ay0, ax1, ay1 = 880, 160, 1290, 560
PCB, SILK = (25, 105, 60), (235, 245, 235)
text((ax0 + ax1) // 2, ay0 - 30, 'Module ampli NS4110B ou PAM8610 (7 a 15 V)', FB, INK, 'mm')
d.rounded_rectangle((ax0, ay0, ax1, ay1), 12, fill=PCB, outline=INK, width=3)  # circuit imprime
for hx, hy in ((ax0 + 16, ay0 + 16), (ax1 - 16, ay0 + 16), (ax0 + 16, ay1 - 16), (ax1 - 16, ay1 - 16)):  # trous de fixation
    d.ellipse((hx - 7, hy - 7, hx + 7, hy + 7), fill=(250, 250, 247), outline=(200, 200, 160), width=2)
# la puce (boitier a pattes) au centre
cx0, cy0, cx1, cy1 = ax0 + 105, ay0 + 150, ax0 + 215, ay0 + 260
for k in range(9):  # pattes de la puce
    o = cy0 + 14 + k * 12
    d.rectangle((cx0 - 10, o, cx0, o + 5), fill=(200, 200, 200))
    d.rectangle((cx1, o, cx1 + 10, o + 5), fill=(200, 200, 200))
    o = cx0 + 14 + k * 12
    d.rectangle((o, cy0 - 10, o + 5, cy0), fill=(200, 200, 200))
    d.rectangle((o, cy1, o + 5, cy1 + 10), fill=(200, 200, 200))
d.rectangle((cx0, cy0, cx1, cy1), fill=(25, 25, 28))
d.ellipse((cx0 + 8, cy0 + 8, cx0 + 16, cy0 + 16), fill=(70, 70, 70))  # repere de la patte 1
text((cx0 + cx1) // 2, (cy0 + cy1) // 2 - 12, 'PAM8610', FS, (210, 210, 210), 'mm')
text((cx0 + cx1) // 2, (cy0 + cy1) // 2 + 12, 'NS4110B', FS, (150, 150, 150), 'mm')
text((cx0 + cx1) // 2, cy1 + 26, 'la puce de l\'ampli', FS, SILK, 'mm')
# potentiometre de volume
px_, py_ = ax0 + 330, ay1 - 100
d.ellipse((px_ - 30, py_ - 30, px_ + 30, py_ + 30), fill=(40, 90, 200), outline=INK, width=2)
d.ellipse((px_ - 14, py_ - 14, px_ + 14, py_ + 14), fill=(220, 220, 225), outline=INK, width=2)
d.line((px_, py_, px_, py_ - 13), fill=INK, width=3)
text(px_, py_ + 44, 'VOLUME', FS, SILK, 'mm')
# entree : connecteur 3 broches L / GND / R (cote gauche)
for name, y in (('L', py - 40), ('R', py), ('GND', gy)):
    d.rectangle((ax0 - 14, y - 9, ax0 + 6, y + 9), fill=(30, 30, 30))
    d.ellipse((ax0 - 9, y - 5, ax0 + 1, y + 5), fill=(215, 180, 70))
    text(ax0 + 16, y, 'IN ' + name, FB, SILK, 'lm')
wire([(860, py), (860, py - 40), (ax0 - 14, py - 40)])  # L et R reliees
dot(860, py)
text(850, py - 66, 'L et R reliees (mono)', FS, GREY, 'rm')
# sorties haut-parleurs : borniers a vis (cote droit)
for k, (lab, y) in enumerate((('L+ / L-', 250), ('R+ / R-', 380))):
    d.rectangle((ax1 - 46, y - 30, ax1 + 6, y + 30), fill=(40, 90, 200), outline=INK, width=2)
    for v in (y - 14, y + 14):
        d.ellipse((ax1 - 30, v - 9, ax1 - 12, v + 9), fill=(200, 200, 205), outline=INK, width=2)
        d.line((ax1 - 27, v, ax1 - 15, v), fill=INK, width=2)
    text(ax1 - 56, y, 'OUT ' + lab, FB, SILK, 'rm')
    wire([(ax1 + 6, y), (1400, y)])
    speaker(1400, y, 'haut-parleur 4-8 ohms' if k == 0 else '2e haut-parleur (option)')
# alimentation : bornier a vis + / - (en bas)
d.rectangle((ax0 + 40, ay1 - 52, ax0 + 240, ay1 + 6), fill=(40, 90, 200), outline=INK, width=2)
for k, (lab, col) in enumerate((('+ VCC', RED), ('- GND', INK))):
    vx = ax0 + 67 + k * 140
    d.ellipse((vx - 9, ay1 - 32, vx + 9, ay1 - 14), fill=(200, 200, 205), outline=INK, width=2)
    d.line((vx - 6, ay1 - 23, vx + 6, ay1 - 23), fill=INK, width=2)
    text(vx, ay1 - 62, lab, FS, SILK, 'mb')
d.rectangle((ax0 + 60, ay1, ax0 + 74, ay1 + 14), fill=INK)
d.rectangle((ax0 + 200, ay1, ax0 + 214, ay1 + 14), fill=INK)
# conseil
text(1110, ay1 + 26, 'Volume au minimum au debut (3,3 V = signal fort).', FS, GREY, 'lm')
text(1110, ay1 + 50, 'Suivre les reperes imprimes sur ta carte.', FS, GREY, 'lm')

# ---------------------------------------------------------------- alimentation de l'ampli : 2 choix
bx0, by0 = 700, 680
box(bx0, by0, bx0 + 1040, by0 + 330, 'Alimentation de l\'ampli : 7 a 15 V (pas le 5 V des dalles !)', (252, 240, 240))
text(bx0 + 30, by0 + 60, 'Choix 1 : bloc secteur 12 V (2 A) a part', FB, RED)
text(bx0 + 30, by0 + 92, '+12 V -> VCC de l\'ampli, masse -> GND de l\'ampli', F)
text(bx0 + 30, by0 + 140, 'Choix 2 : elevateur 5 V -> 12 V (module MT3608 ou XL6009)', FB, RED)
text(bx0 + 30, by0 + 172, 'entree : 5 V des dalles, sortie reglee a 12 V au tournevis (avant de brancher l\'ampli)', F)
text(bx0 + 30, by0 + 204, 'MT3608 : ~1 A, volume moyen. XL6009 : plus de puissance.', FS, GREY)
text(bx0 + 30, by0 + 256, 'Dans tous les cas : GND de l\'ampli relie au GND de l\'ESP32 (masse commune).', FB, INK)
text(bx0 + 30, by0 + 288, 'Sinon : bourdonnement ou pas de son.', FS, GREY)
wire([(ax0 + 67, ay1 + 14), (ax0 + 67, by0)], RED, 4)
wire([(ax0 + 207, ay1 + 14), (ax0 + 207, by0)], INK, 4)

# ---------------------------------------------------------------- option simple
ox0, oy0 = 60, 680
box(ox0, oy0, ox0 + 590, oy0 + 330, 'Plus simple : PAM8403 (2 x 3 W) en 5 V', (240, 250, 242))
for k, t in enumerate(('Meme branchement de l\'entree (filtre identique).',
                       'VCC sur le 5 V des dalles, GND commun.',
                       'Pas d\'alimentation 12 V ni d\'elevateur.',
                       'Largement assez fort pour ce jeu.',
                       'Haut-parleurs 4 ou 8 ohms, 3 W.')):
    text(ox0 + 30, oy0 + 64 + k * 36, '- ' + t, F, INK)
text(ox0 + 30, oy0 + 260, 'Choisir une version avec potentiometre de volume.', FS, GREY)
gnd(620, gy + 40)
wire([(620, gy), (620, gy + 40)])

img.save(OUT)
print(OUT)
