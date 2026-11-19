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
s = Sheet(2000, 2010, 'Game hub - branchement des dalles, du son, du bouton et des manettes USB',
          'ESP32 DevKit (38 broches) - meme brochage dalle que l\'horloge ClockV1 et le VU-metre - manette PS4 en Bluetooth (sans fil)')
LEFT = [('5V / VIN', '5v'), ('GND', 'gnd'), ('GPIO22', 'snd'), ('GPIO34', 'btn'), ('GPIO36 (VP)', 'usb')]
RIGHT = [('GPIO25', 'R1'), ('GPIO26', 'G1'), ('GPIO27', 'B1'), ('GPIO14', 'R2'), ('GPIO12', 'G2'), ('GPIO13', 'B2'),
         ('GPIO23', 'A'), ('GPIO19', 'B'), ('GPIO5', 'C'), ('GPIO17', 'D'), ('GPIO32', 'E'), ('GPIO16', 'CLK'),
         ('GPIO4', 'LAT'), ('GPIO15', 'OE'), ('GND', 'GND')]
lp, rp = s.esp32(760, 140, 1080, 940, 'ESP32 DevKit (jeu)', LEFT, RIGHT, 150, 50, lstart=150)
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
# son (option) : une broche PWM vers un ampli analogique
ax0, ay0, ax1, ay1 = 40, 600, 680, 940
s.box(ax0, ay0, ax1, ay1, 'Son (option) : GPIO 22 en PWM vers un ampli', (240, 246, 255))
ex, ey = lp['snd']
s.wire([(ax1, ay0 + 120), (720, ay0 + 120), (720, ey), (ex, ey)], ORANGE, 4)
s.dot(ex, ey, ORANGE)
s.text(ax0 + 30, ay0 + 70, 'GPIO 22 -> 1 kOhm -> (10 nF vers la masse) -> 10 uF', F, ORANGE)
s.text(ax0 + 30, ay0 + 100, '-> entree de l\'ampli (L et R reliees)', F, ORANGE)
s.text(ax0 + 30, ay0 + 150, 'Ampli NS4110B / PAM8610 : 7 a 15 V (bloc 12 V ou elevateur)', FS)
s.text(ax0 + 30, ay0 + 176, 'ou PAM8403 directement en 5 V. Masse commune avec l\'ESP32.', FS)
s.text(ax0 + 30, ay0 + 226, 'Schema detaille : docs/branchement-son.png', FB, BLUE)
s.text(ax0 + 30, ay0 + 270, 'Volume : potentiometre de l\'ampli + commande v 0 a v 10.', FS, GREY)

# chainage : 3 configurations
cy = 1010
s.text(40, cy - 30, 'Chainage des dalles 128x64 (reglage dans le moniteur serie : commande c NDFS)', FB)
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
s.text(40, oy, 'Commande c NDFS du moniteur serie : 1 a 4 dalles, l\'une sous l\'autre, cote a cote ou carre 2x2 (4 dalles).', FB)
s.text(40, oy + 32, '- Ecran retourne (180) : la 1re dalle est en bas (ou a droite), ou tout l\'ensemble est monte a l\'envers.', FS)
s.text(40, oy + 58, '- Une dalle sur deux retournee (serpentin) : les dalles 2 et 4 sont tournees de 180 pour garder des nappes courtes.', FS)
# broches 34 et 36 : reperes A et B, detail en bas de la page
for key, tag, col in (('btn', 'A', GREEN), ('usb', 'B', BLUE)):
    x, y = lp[key]
    s.wire([(x, y), (706, y)], col, 4)
    s.d.ellipse((682, y - 12, 706, y + 12), fill=col)
    s.text(694, y, tag, FB, (255, 255, 255), 'mm')

by = 1580
# (A) bouton d'association
s.box(40, by, 960, by + 400, '(A) Bouton d\'association (bouton "config wifi" du shield, comme l\'horloge)', (238, 250, 240))
s.text(80, by + 70, '3,3 V', FB, RED, 'lm')
s.wire([(150, by + 70), (300, by + 70)], RED, 4)
s.d.rectangle((300, by + 52, 380, by + 88), outline=INK, width=3, fill=(255, 255, 255))
s.text(340, by + 70, 'bouton', FS, INK, 'mm')
s.wire([(380, by + 70), (560, by + 70)], GREEN, 4)
s.dot(560, by + 70, GREEN)
s.text(580, by + 70, 'GPIO34 de l\'ESP32 (A)', FB, GREEN, 'lm')
s.wire([(470, by + 70), (470, by + 120)], INK, 3)
s.d.rectangle((455, by + 120, 485, by + 180), outline=INK, width=3, fill=(255, 255, 255))
s.text(500, by + 150, '10 kOhm vers la masse (deja sur le shield)', FS, INK, 'lm')
s.wire([(470, by + 180), (470, by + 205)], INK, 3)
s.gnd(470, by + 207)
for i, t in enumerate(('Un appui : mode association 60 s (la dalle affiche ASSOCIATION, son coupe,',
                       'luminosite a 10 %). Seul moment ou une nouvelle manette peut s\'associer.',
                       'Les manettes deja associees se reconnectent toujours avec PS.',
                       'GPIO 34 : entree seule, sans tirage interne : la resistance est obligatoire.',
                       'Aussi : commande m du moniteur serie.')):
    s.text(80, by + 250 + i * 28, t, FS, GREY if i >= 3 else INK)

# (B) manettes en USB par un RP2040-Zero
bx = 1000
s.box(bx, by, 1960, by + 400, '(B) Manettes USB (option) : RP2040-Zero', (238, 244, 255))
s.d.rounded_rectangle((bx + 40, by + 70, bx + 200, by + 150), 10, outline=INK, width=3, fill=(255, 255, 255))
s.text(bx + 120, by + 100, 'Manette', FB, INK, 'mm')
s.text(bx + 120, by + 126, 'PS4, PS5, Xbox', FS, GREY, 'mm')
s.arrow([(bx + 200, by + 110), (bx + 300, by + 110)], GREY, 4)
s.text(bx + 250, by + 92, 'cable USB', FS, GREY, 'mb')
s.d.rounded_rectangle((bx + 300, by + 80, bx + 420, by + 140), 8, outline=INK, width=3, fill=(250, 250, 250))
s.text(bx + 360, by + 110, 'OTG', FB, INK, 'mm')
s.text(bx + 360, by + 160, 'adaptateur USB-C', FS, GREY, 'mt')
s.text(bx + 360, by + 182, 'femelle A', FS, GREY, 'mt')
s.wire([(bx + 420, by + 110), (bx + 470, by + 110)], INK, 4)
s.d.rounded_rectangle((bx + 470, by + 60, bx + 680, by + 220), 12, outline=INK, width=3, fill=(220, 236, 255))
s.text(bx + 575, by + 84, 'RP2040-Zero', FB, BLUE, 'mm')
for i, (pin, col) in enumerate((('5V', RED), ('GND', INK), ('GP0 (TX)', BLUE))):
    y = by + 125 + i * 32
    s.d.rectangle((bx + 680, y - 6, bx + 692, y + 6), fill=INK)
    s.text(bx + 668, y, pin, FS, col, 'rm')
for i, (t, col) in enumerate((('+5 V commun', RED), ('GND commun', INK), ('GPIO36 / VP de l\'ESP32 (B)', BLUE))):
    y = by + 125 + i * 32
    s.wire([(bx + 692, y), (bx + 740, y)], col, 4)
    s.dot(bx + 740, y, col)
    s.text(bx + 752, y, t, FS, col, 'lm')
for i, t in enumerate(('Le RP2040 lit la manette en USB et envoie ses touches a l\'ESP32 par un seul fil',
                       '(liaison serie 3,3 V : TX du RP2040 -> GPIO 36, une entree libre de l\'ESP32).',
                       'Avantages : plus de decrochage Bluetooth, la manette se recharge en jouant.',
                       'A faire : programme du RP2040 et prise en charge dans le Game hub',
                       '(les manettes Bluetooth continueront de marcher a cote).')):
    s.text(bx + 40, by + 250 + i * 28, t, FS, RED if i >= 3 else INK)
s.save('branchement-dalles.png')

