"""Plans de cablage de Game hub.

docs/branchement-dalles.png  : ESP32 du jeu -> connecteur HUB75, chainage des dalles (3 configurations), alimentation
docs/branchement-manette.png : ESP32 de la manette -> boutons, joystick en option, alimentation
"""
import os

from PIL import Image, ImageDraw, ImageFont

DOC = os.path.dirname(os.path.abspath(__file__))


def font(size, bold=False):
    for name in (('arialbd.ttf' if bold else 'arial.ttf'), 'DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


F, FB, FS, FT = font(20), font(20, True), font(16), font(30, True)
INK, GREY, RED, BLUE, GREEN, ORANGE, PURPLE = (30, 30, 30), (120, 120, 120), (200, 40, 30), (30, 90, 200), (30, 140, 60), (220, 120, 0), (130, 40, 160)
COLORS = {'R1': (220, 40, 40), 'G1': (30, 160, 60), 'B1': (40, 90, 220), 'R2': (220, 40, 40), 'G2': (30, 160, 60), 'B2': (40, 90, 220),
          'A': (140, 70, 20), 'B': (140, 70, 20), 'C': (140, 70, 20), 'D': (140, 70, 20), 'E': (140, 70, 20),
          'CLK': PURPLE, 'LAT': PURPLE, 'OE': PURPLE}


class Sheet:
    def __init__(self, w, h, title, sub):
        self.img = Image.new('RGB', (w, h), (250, 250, 247))
        self.d = ImageDraw.Draw(self.img)
        self.text(w // 2, 30, title, FT, anchor='mt')
        self.text(w // 2, 72, sub, FS, GREY, anchor='mt')

    def box(self, x0, y0, x1, y1, title, fill=(255, 255, 255)):
        self.d.rounded_rectangle((x0, y0, x1, y1), 14, fill=fill, outline=INK, width=3)
        self.text((x0 + x1) // 2, y0 + 22, title, FB, anchor='mm')

    def text(self, x, y, s, f=F, col=INK, anchor='la'):
        self.d.text((x, y), s, font=f, fill=col, anchor=anchor)

    def wire(self, pts, col, w=3):
        self.d.line(pts, fill=col, width=w, joint='curve')

    def dot(self, x, y, col=INK):
        self.d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=col)

    def gnd(self, x, y):
        for k, hw in enumerate((18, 12, 6)):
            self.d.line((x - hw, y + k * 6, x + hw, y + k * 6), fill=INK, width=3)

    def esp32(self, x0, y0, x1, y1, title, left, right, lstep, rstep, lstart=80, rstart=70):
        """Carte ESP32 avec ses broches utilisees ; renvoie les points d'accroche gauche et droite."""
        self.box(x0, y0, x1, y1, title, (236, 242, 250))
        lp, rp = {}, {}
        for i, (p, key) in enumerate(left):
            y = y0 + lstart + i * lstep
            self.d.rectangle((x0 - 12, y - 6, x0, y + 6), fill=INK)
            self.text(x0 + 12, y, p, F, INK, 'lm')
            lp[key] = (x0 - 12, y)
        for i, (p, key) in enumerate(right):
            y = y0 + rstart + i * rstep
            if key is None:
                continue
            self.d.rectangle((x1, y - 6, x1 + 12, y + 6), fill=INK)
            self.text(x1 - 12, y, p, F, INK, 'rm')
            rp[key] = (x1 + 12, y)
        return lp, rp

    def panel(self, x, y, w, h, label, sub='IN -> OUT'):
        self.d.rounded_rectangle((x, y, x + w, y + h), 8, outline=INK, width=3, fill=(20, 20, 30))
        self.text(x + w // 2, y + h // 2 - 12, label, FB, (240, 240, 240), 'mm')
        self.text(x + w // 2, y + h // 2 + 16, sub, FS, (200, 200, 200), 'mm')

    def arrow(self, pts, col=ORANGE, w=5):
        self.wire(pts, col, w)
        (xa, ya), (xb, yb) = pts[-2], pts[-1]
        import math
        a = math.atan2(yb - ya, xb - xa)
        p1 = (xb - 14 * math.cos(a - 0.45), yb - 14 * math.sin(a - 0.45))
        p2 = (xb - 14 * math.cos(a + 0.45), yb - 14 * math.sin(a + 0.45))
        self.d.polygon([(xb, yb), p1, p2], fill=col)

    def save(self, name):
        path = os.path.join(DOC, name)
        self.img.save(path)
        print(path)


# ================================================================ jeu : ESP32 -> dalles
s = Sheet(2000, 1560, 'Game hub - branchement des dalles',
          'ESP32 DevKit (38 broches) - meme brochage dalle que l\'horloge ClockV1 et le VU-metre')
LEFT = [('BOOT (bouton carte)', 'boot'), ('5V / VIN', '5v'), ('GND', 'gnd')]
RIGHT = [('GPIO25', 'R1'), ('GPIO26', 'G1'), ('GPIO27', 'B1'), ('GPIO14', 'R2'), ('GPIO12', 'G2'), ('GPIO13', 'B2'),
         ('GPIO23', 'A'), ('GPIO19', 'B'), ('GPIO5', 'C'), ('GPIO17', 'D'), ('GPIO32', 'E'), ('GPIO16', 'CLK'),
         ('GPIO4', 'LAT'), ('GPIO15', 'OE'), ('GND', 'GND')]
lp, rp = s.esp32(760, 140, 1080, 940, 'ESP32 DevKit (jeu)', LEFT, RIGHT, 250, 50, lstart=200)
hx0, hy0 = 1440, 140
s.box(hx0, hy0, hx0 + 260, hy0 + 590, 'HUB75 (entree)', (255, 252, 240))
HUB = ['R1', 'G1', 'B1', 'GND', 'R2', 'G2', 'B2', 'E', 'A', 'B', 'C', 'D', 'CLK', 'LAT', 'OE', 'GND']
hp = {}
for n, sig in enumerate(HUB):
    y = hy0 + 70 + n * 32
    s.d.rectangle((hx0 + 20, y - 11, hx0 + 42, y + 11), outline=INK, width=2, fill=(255, 255, 255))
    s.text(hx0 + 56, y, '%2d' % (n + 1), FS, GREY, 'lm')
    s.text(hx0 + 92, y, sig, FB, COLORS.get(sig, GREY), 'lm')
    hp.setdefault(sig, (hx0 + 20, y))
s.text(hx0 + 130, hy0 + 610, 'Nappe 2 x 8 vue de face :', FS, GREY, 'mt')
s.text(hx0 + 130, hy0 + 634, 'impairs a gauche, pairs a droite', FS, GREY, 'mt')
for i, sig in enumerate(['R1', 'G1', 'B1', 'R2', 'G2', 'B2', 'A', 'B', 'C', 'D', 'E', 'CLK', 'LAT', 'OE', 'GND']):
    (x0, y0), (hx, hy) = rp[sig], hp[sig]
    mid = 1130 + i * 19
    s.wire([(x0, y0), (mid, y0), (mid, hy), (hx, hy)], COLORS.get(sig, INK))
    s.dot(hx, hy, COLORS.get(sig, INK))
s.text(1440, 790, 'Dalles 1/32 (128x64) : la broche E est obligatoire.', FS, RED)
s.text(1440, 815, 'La nappe fournie avec la dalle va sur son entree IN.', FS, GREY)

# alimentation
px0, py0 = 40, 140
s.box(px0, py0, px0 + 640, py0 + 420, 'Alimentation 5 V', (252, 240, 240))
for i, (t, col) in enumerate((('+5 V  ->  borniers d\'alimentation de chaque dalle', RED),
                              ('+5 V  ->  broche 5 V / VIN de l\'ESP32', RED),
                              ('GND   ->  dalles + ESP32 (masse commune)', INK))):
    s.text(px0 + 30, py0 + 70 + i * 40, t, F, col)
for i, t in enumerate(('Puissance conseillee :', '   2 dalles (128x128 ou 256x64) : 5 V 8 A', '   4 dalles : 5 V 15 A',
                       'Le jeu est surtout noir : il consomme bien moins,', 'mais un ecran blanc demande ~4 A par dalle.',
                       'Fils de 1,5 mm2 pour le 5 V des dalles.')):
    s.text(px0 + 30, py0 + 210 + i * 30, t, FB if i == 0 else FS, GREY if i in (3, 4) else INK)
s.text(px0 + 30, py0 + 390, 'Ne pas alimenter les dalles par l\'USB de l\'ESP32.', FS, RED)
vx, vy = lp['5v']
s.wire([(px0 + 640, py0 + 120), (720, py0 + 120), (720, vy), (vx, vy)], RED, 4)
gx, gy = lp['gnd']
s.wire([(px0 + 640, py0 + 160), (700, py0 + 160), (700, gy), (gx, gy)], INK, 4)
bx, by = lp['boot']
s.text(px0 + 30, py0 + 470, 'Bouton BOOT de la carte, appuye 5 s :', FB)
s.text(px0 + 30, py0 + 500, 'oublie le wifi et redemarre (reseau Games-Setup).', FS, GREY)
s.wire([(px0 + 420, py0 + 482), (740, py0 + 482), (740, by), (bx, by)], GREY, 2)

# chainage : 3 configurations
cy = 1010
s.text(40, cy - 30, 'Chainage des dalles 128x64 (choix dans la page web, Reglages -> Dalles)', FB)
s.text(40, cy, 'Toutes les dalles sont posees dans le meme sens (fleches imprimees au dos dans la meme direction). '
               'Nappe : sortie OUT d\'une dalle -> entree IN de la suivante.', FS, GREY)
PW, PH = 200, 100
# 128x128
x, y = 110, cy + 60
s.text(x, y, '128x128 : 2 dalles l\'une sous l\'autre', FB, BLUE)
s.panel(x, y + 40, PW, PH, 'Dalle 1')
s.panel(x, y + 40 + PH + 20, PW, PH, 'Dalle 2', 'IN')
s.arrow([(x - 40, y + 90), (x, y + 90)], GREY)
s.text(x - 40, y + 76, 'ESP32', FS, GREY, 'rb')
s.arrow([(x + PW, y + 90), (x + PW + 40, y + 90), (x + PW + 40, y + 40 + PH + 20 + 50), (x + PW, y + 40 + PH + 20 + 50)])
# 256x64
x = 600
s.text(x, y, '256x64 : 2 dalles cote a cote', FB, BLUE)
s.panel(x, y + 40, PW, PH, 'Dalle 1')
s.panel(x + PW + 40, y + 40, PW, PH, 'Dalle 2', 'IN')
s.arrow([(x - 40, y + 90), (x, y + 90)], GREY)
s.text(x - 40, y + 76, 'ESP32', FS, GREY, 'rb')
s.arrow([(x + PW, y + 90), (x + PW + 40, y + 90)])
# 128x256 : 4 dalles l'une sous l'autre (vertical)
x = 1300
s.text(x - 60, y, '128x256 : 4 dalles verticales', FB, BLUE)
VH = 64
for n in range(4):
    s.panel(x, y + 40 + n * (VH + 14), PW, VH, f'Dalle {n + 1}', '')
    if n < 3:
        yy = y + 40 + n * (VH + 14) + VH // 2
        s.arrow([(x + PW, yy), (x + PW + 36, yy), (x + PW + 36, yy + VH + 14), (x + PW, yy + VH + 14)])
s.arrow([(x - 40, y + 40 + VH // 2), (x, y + 40 + VH // 2)], GREY)
s.text(x - 40, y + 40 + VH // 2 - 14, 'ESP32', FS, GREY, 'rb')
s.text(x + PW + 60, y + 50, 'Facon borne d\'arcade.', FS)
s.text(x + PW + 60, y + 76, '5 V 15 A.', FS, RED)

# options du menu Dalles
oy = y + 40 + 4 * (VH + 14) + 20
s.text(40, oy, 'Menu Dalles de la page web : 1 a 4 dalles, l\'une sous l\'autre, cote a cote ou carre 2x2 (4 dalles).', FB)
s.text(40, oy + 32, '- Ecran retourne (180) : la 1re dalle est en bas (ou a droite), ou tout l\'ensemble est monte a l\'envers.', FS)
s.text(40, oy + 58, '- Une dalle sur deux retournee (serpentin) : les dalles 2 et 4 sont tournees de 180 pour garder des nappes courtes.', FS)
s.save('branchement-dalles.png')

# ================================================================ manette
s = Sheet(1700, 1100, 'Game hub - branchement de la manette',
          'ESP32 DevKit - radio ESP-NOW directe vers le jeu, sans box ni wifi a regler')
LEFT = [('GPIO32', 'left'), ('GPIO33', 'right'), ('GPIO25', 'fire'), ('GPIO26', 'fire2'), ('GND', 'gnd')]
RIGHT = [('3V3', '3v3'), ('GPIO34', 'joyx'), ('GND', 'jgnd'), ('', None), ('5V / VIN', '5v'), ('GND', 'pgnd')]
lp, rp = s.esp32(640, 160, 960, 860, 'ESP32 DevKit (manette)', LEFT, RIGHT, 120, 100, lstart=110, rstart=110)
s.text(800, 820, 'LED GPIO 2 (sur la carte) :', FS, INK, 'mm')
s.text(800, 842, 'clignote = recherche, fixe = reliee', FS, GREY, 'mm')

# boutons vers la masse
bx0, by0 = 40, 160
s.box(bx0, by0, bx0 + 500, by0 + 700, 'Boutons (vers la masse)', (240, 250, 242))
names = {'left': ('GAUCHE', '<'), 'right': ('DROITE', '>'), 'fire': ('FEU', 'F'), 'fire2': ('FEU 2 (option)', 'F')}
gnd_x = bx0 + 120
for key, (label, sym) in names.items():
    px, py = lp[key]
    cx = bx0 + 250
    # bouton poussoir : 2 pattes, une vers la GPIO, l'autre vers la masse
    s.d.ellipse((cx - 26, py - 26, cx + 26, py + 26), outline=INK, width=3, fill=(255, 210, 210) if 'FEU' in label else (220, 225, 245))
    s.text(cx, py, sym, FB, INK, 'mm')
    s.text(cx, py + 32, label, FS, INK, 'mt')
    s.wire([(cx + 26, py), (px, py)], ORANGE, 4)
    s.dot(px, py, ORANGE)
    s.wire([(cx - 26, py), (gnd_x, py)], INK, 3)
    s.dot(gnd_x, py)
gy_top, gy_bot = lp['left'][1], lp['gnd'][1]
s.wire([(gnd_x, gy_top), (gnd_x, gy_bot)], INK, 3)
s.wire([(gnd_x, gy_bot), lp['gnd']], INK, 3)
s.text(bx0 + 30, by0 + 620, 'Pas de resistance : rappel interne de l\'ESP32.', FS, GREY)
s.text(bx0 + 30, by0 + 645, 'Boutons d\'arcade ou poussoirs, 2 pattes.', FS, GREY)

# joystick en option
jx0, jy0 = 1080, 160
s.box(jx0, jy0, jx0 + 580, jy0 + 440, 'Joystick analogique (option, KY-023)', (255, 252, 240))
jpins = [('+5V (VCC)', '3v3', RED, '-> 3,3 V (pas 5 V !)'), ('VRx', 'joyx', ORANGE, '-> GPIO 34'), ('GND', 'jgnd', INK, '-> GND')]
for i, (pin, key, col, note) in enumerate(jpins):
    ex, ey = rp[key]
    y = ey
    s.d.rectangle((jx0 + 30, y - 10, jx0 + 50, y + 10), outline=INK, width=2, fill=(255, 255, 255))
    s.text(jx0 + 60, y, pin, FB, col, 'lm')
    s.text(jx0 + 220, y, note, FS, GREY, 'lm')
    s.wire([(ex, ey), (jx0 + 30, y)], col, 4)
    s.dot(jx0 + 30, y, col)
s.text(jx0 + 30, jy0 + 345, 'SW (bouton du stick) -> GPIO 26 = 2e FEU.', FS)
s.text(jx0 + 30, jy0 + 370, 'VRy n\'est pas utilise.', FS, GREY)
s.text(jx0 + 30, jy0 + 400, 'Dans manette.ino : #define JOYSTICK 1', FB, BLUE)

# alimentation
ax0, ay0 = 1080, 640
s.box(ax0, ay0, ax0 + 580, ay0 + 240, 'Alimentation', (252, 240, 240))
for i, t in enumerate(('USB : chargeur ou batterie externe (le plus simple).', 'Ou batterie 18650 + module chargeur / 5 V',
                       '   -> broche 5 V / VIN, masse -> GND.', 'Consommation ~100 mA.')):
    s.text(ax0 + 30, ay0 + 70 + i * 36, t, FS, GREY if i == 3 else INK)
vx, vy = rp['5v']
s.wire([(vx, vy), (1040, vy), (1040, ay0 + 160), (ax0, ay0 + 160)], RED, 4)
gx, gy = rp['pgnd']
s.wire([(gx, gy), (1020, gy), (1020, ay0 + 196), (ax0, ay0 + 196)], INK, 4)

s.text(40, 920, 'Mise en route : allumer le jeu, puis la manette. La LED clignote pendant la recherche (canaux 1 a 13),', FS)
s.text(40, 946, 'puis reste allumee. Si le jeu change de canal (box reglee) ou s\'eteint, la manette le recherche toute seule.', FS)
s.text(40, 980, 'Plusieurs manettes et la page web peuvent piloter le meme vaisseau.', FS, GREY)
s.save('branchement-manette.png')
