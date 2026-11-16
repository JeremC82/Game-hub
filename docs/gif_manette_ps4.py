"""GIF anime : connexion de la manette PS4 a Game hub (version Games_PS4).

Etapes : adresse lue avec SixaxisPairTool -> commande 'a' dans le moniteur serie -> la dalle attend
-> bouton PS -> partie (tir, vibration, barre lumineuse). Sortie : docs/manette-ps4.gif
Reprend le dessin du jeu de docs/rendu.py (memes sprites).
"""
import math
import os

from PIL import Image, ImageDraw

DOC = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(DOC, 'rendu.py'), encoding='utf-8').read()
ns = {'__file__': os.path.join(DOC, 'rendu.py')}
exec(src[:src.index('CONFIGS = [')], ns)  # fonctions de dessin du jeu (sans generer les images)
Screen, game_frame, font, SPR_SHIP, PAL = ns['Screen'], ns['game_frame'], ns['font'], ns['SPR_SHIP'], ns['PAL']

WD, HT = 900, 560
BG, INK, GREY, CYAN = (16, 16, 22), (235, 235, 245), (140, 140, 165), (0, 220, 255)
F, FB, FT, FM = font(18), font(18, True), font(26, True), font(17)
MAC = '1a:2b:3c:4d:5e:6f'


# ---------------------------------------------------------------- elements
def led_screen(img, x0, y0, pitch=3):
    """Ecran LED 128x128 (2 dalles) dessine en points."""
    out = Image.new('RGB', (img.width * pitch + 8, img.height * pitch + 8), (8, 8, 12))
    d = ImageDraw.Draw(out)
    for y in range(img.height):
        for x in range(img.width):
            c = img.getpixel((x, y))
            if sum(c) < 30:
                continue
            cx, cy = 4 + x * pitch, 4 + y * pitch
            d.rectangle((cx, cy, cx + pitch - 2, cy + pitch - 2), fill=c)
    d.rectangle((0, 0, out.width - 1, out.height - 1), outline=(70, 70, 90), width=2)
    d.line((0, 4 + 64 * pitch - 1, out.width, 4 + 64 * pitch - 1), fill=(50, 50, 65), width=1)
    return out, (x0, y0)


def panel_img(texts=(), stage_ship=1, demo_bg=True, ship_x=None, shots=(), boom=None):
    """Image 128x128 du jeu avec textes centres (police 6x8 du jeu)."""
    s = Screen(128, 128)
    s.im = game_frame(128, 128, demo=False, stage=stage_ship, banner=False, seed=5) if demo_bg else Image.new('RGB', (128, 128))
    if ship_x is not None:  # vaisseau deplace : on efface la bande du bas et on le redessine
        ImageDraw.Draw(s.im).rectangle((0, 100, 127, 121), fill=(0, 0, 0))
        s.sprite(SPR_SHIP[min(stage_ship, 4) - 1], ship_x, 113)
        for sx, sy in shots:
            for k in range(3):
                s.px(sx, sy + k, (PAL['w'], PAL['y'], PAL['o'])[k])
    if boom:
        bx, by, t = boom
        for dx, dy in ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)):
            s.px(bx + dx * t, by + dy * t, PAL['y'] if t < 4 else PAL['r'])
    for y, t, col in texts:  # fond noir derriere le texte, pour la lisibilite du GIF
        x = (128 - len(t) * 6) // 2
        ImageDraw.Draw(s.im).rectangle((x - 2, y - 2, x + len(t) * 6 + 1, y + 9), fill=(0, 0, 0))
        s.text(y, t, col)
    return s.im


def controller(d, cx, cy, led=None, ps=False, cross=False, shake=0, stick=0):
    """Manette de jeu stylisee (vue de dessus)."""
    cx += shake
    body = (40, 42, 52)
    d.ellipse((cx - 150, cy - 10, cx - 70, cy + 110), fill=body)  # poignees
    d.ellipse((cx + 70, cy - 10, cx + 150, cy + 110), fill=body)
    d.rounded_rectangle((cx - 140, cy - 50, cx + 140, cy + 50), 40, fill=body)
    d.rounded_rectangle((cx - 55, cy - 52, cx + 55, cy - 44), 4, fill=led or (30, 30, 36))  # barre lumineuse
    if led:
        d.rounded_rectangle((cx - 60, cy - 56, cx + 60, cy - 40), 6, outline=led, width=2)
    d.rounded_rectangle((cx - 45, cy - 38, cx + 45, cy + 2), 6, fill=(28, 28, 36), outline=(70, 70, 85))  # pave tactile
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):  # croix
        x, y = cx - 100 + dx * 14, cy - 10 + dy * 14
        d.rectangle((x - 7, y - 7, x + 7, y + 7), fill=(70, 72, 85))
    for (dx, dy), sym, col in (((0, -1), 'tri', (60, 200, 140)), ((0, 1), 'x', (100, 150, 255)), ((-1, 0), 'sq', (230, 120, 200)),
                               ((1, 0), 'o', (255, 90, 90))):  # boutons (symboles dessines)
        x, y = cx + 100 + dx * 18, cy - 10 + dy * 18
        on = cross and sym == 'x'
        d.ellipse((x - 10, y - 10, x + 10, y + 10), fill=(230, 230, 240) if on else (60, 62, 75))
        c = (20, 20, 30) if on else col
        if sym == 'x':
            d.line((x - 4, y - 4, x + 4, y + 4), fill=c, width=2); d.line((x - 4, y + 4, x + 4, y - 4), fill=c, width=2)
        elif sym == 'o':
            d.ellipse((x - 5, y - 5, x + 5, y + 5), outline=c, width=2)
        elif sym == 'sq':
            d.rectangle((x - 4, y - 4, x + 4, y + 4), outline=c, width=2)
        else:
            d.polygon([(x, y - 5), (x - 5, y + 4), (x + 5, y + 4)], outline=c)
    for sx in (-50, 50):  # sticks
        ox = stick * 8 if sx < 0 else 0
        d.ellipse((cx + sx - 20, cy + 20, cx + sx + 20, cy + 60), fill=(25, 25, 32))
        d.ellipse((cx + sx - 13 + ox, cy + 27, cx + sx + 13 + ox, cy + 53), fill=(55, 57, 70))
    r = 11 if ps else 8  # bouton PS
    d.ellipse((cx - r, cy + 30 - r, cx + r, cy + 30 + r), fill=(240, 240, 255) if ps else (75, 77, 92))
    d.text((cx, cy + 30), 'PS', fill=(20, 20, 30), font=font(9, True), anchor='mm')
    return cx, cy


def frame(step, title, sub=''):
    im = Image.new('RGB', (WD, HT), BG)
    d = ImageDraw.Draw(im)
    d.text((24, 18), f'Manette PS4 - etape {step}/4' if step else 'Manette PS4', fill=GREY, font=F)
    d.text((24, 44), title, fill=INK, font=FT)
    if sub:
        d.text((24, 80), sub, fill=GREY, font=F)
    return im, d


frames, durations = [], []


def add(im, ms):
    frames.append(im)
    durations.append(ms)


# ---------------------------------------------------------------- 1. adresse lue sur PC
for k in range(8):
    im, d = frame(1, 'Branche la manette en USB sur le PC', 'SixaxisPairTool lit l\'adresse de la console qu\'elle connait')
    controller(d, 230, 340)
    # cable USB vers le PC
    n = min(k, 4) / 4
    x1 = 230 + (520 - 230) * n
    d.line((230, 290, 230, 220, x1, 220), fill=(200, 200, 210), width=4, joint='curve')
    d.rounded_rectangle((480, 130, 830, 400), 10, fill=(36, 38, 50), outline=(90, 90, 110), width=2)  # ecran PC
    d.rectangle((480, 130, 830, 160), fill=(60, 90, 160))
    d.text((495, 137), 'SixaxisPairTool', fill=INK, font=FB)
    if k >= 4:
        d.text((500, 190), 'Current Master:', fill=GREY, font=FM)
        d.text((500, 220), MAC.upper(), fill=CYAN, font=font(26, True))
        d.text((500, 270), 'Change Master:', fill=GREY, font=FM)
        d.rounded_rectangle((500, 300, 800, 330), 4, fill=(24, 24, 32), outline=(80, 80, 100))
    if k >= 6:
        d.rounded_rectangle((495, 212, 820, 256), 6, outline=(255, 220, 0), width=3)
        d.text((500, 350), 'Note cette adresse', fill=(255, 220, 0), font=FB)
    add(im, 2200 if k == 7 else 450)

# ---------------------------------------------------------------- 2. moniteur serie
cmd = 'a ' + MAC
for k in range(0, len(cmd) + 1, 2):
    im, d = frame(2, 'Tape-la dans le moniteur serie de l\'ESP32', 'Arduino IDE ou autre, 115200 bauds, puis Entree')
    d.rounded_rectangle((60, 120, 800, 420), 10, fill=(10, 12, 16), outline=(90, 90, 110), width=2)
    lines = ['===== Game hub v2.01 =====', 'Adresse Bluetooth de l\'ESP32 : 24:6f:28:aa:bb:cc',
             '  a xx:xx:xx:xx:xx:xx : adresse de la console connue par la manette', '']
    for i, t in enumerate(lines):
        d.text((80, 140 + i * 26), t, fill=(150, 200, 150), font=font(16))
    d.rounded_rectangle((80, 260, 780, 296), 4, fill=(26, 28, 36), outline=(90, 90, 110))
    d.text((92, 268), cmd[:k] + ('_' if k < len(cmd) else ''), fill=INK, font=font(20, True))
    add(im, 140)
for k in range(2):
    im, d = frame(2, 'Tape-la dans le moniteur serie de l\'ESP32', 'Arduino IDE ou autre, 115200 bauds, puis Entree')
    d.rounded_rectangle((60, 120, 800, 420), 10, fill=(10, 12, 16), outline=(90, 90, 110), width=2)
    d.text((80, 140), '> ' + cmd, fill=INK, font=font(18, True))
    d.text((80, 175), 'Adresse manette : ' + MAC + ', redemarrage', fill=(150, 200, 150), font=font(16))
    if k:
        d.text((80, 205), 'Manette PS4 : Bluetooth pret, adresse ' + MAC, fill=(150, 200, 150), font=font(16))
        d.text((80, 260), 'Une seule fois : l\'adresse est gardee en memoire.', fill=(255, 220, 0), font=FB)
        d.text((80, 290), 'La manette marche toujours sur la console.', fill=GREY, font=F)
    add(im, 2200 if k else 900)

# ---------------------------------------------------------------- 3 et 4. la dalle attend, bouton PS
for k in range(10):
    pressed = k >= 5
    im, d = frame(3 if not pressed else 4, 'La dalle attend la manette...' if not pressed else 'Appuie sur le bouton PS',
                  'L\'ESP32 se fait passer pour la console' if not pressed else 'Connectee : la barre lumineuse s\'allume')
    blink = k % 2 == 0
    if not pressed:  # demo sans manette : juste l'indication et l'adresse Bluetooth
        texts = [(70, 'MANETTE: BOUTON PS', CYAN), (82, MAC, (110, 110, 130))]
    else:  # manette connectee : le menu s'affiche
        texts = [(58, 'GAME HUB', (255, 220, 0)), (70, '> <SPACE DEFENDER>', CYAN)]
    scr, pos = led_screen(panel_img(texts), 470, 100)
    im.paste(scr, (480, 130))
    controller(d, 230, 340, led=(0, 0, 90) if pressed else None, ps=(k in (5, 6)))
    if k in (5, 6):
        d.ellipse((230 - 26, 370 - 26, 230 + 26, 370 + 26), outline=(255, 220, 0), width=3)
    add(im, 1500 if k in (4, 9) else 380)

# ---------------------------------------------------------------- 5. partie
ship = 58
shots = []
for k in range(16):
    hit = k in (11, 12, 13)
    im, d = frame(0, 'C\'est parti !', 'Stick ou croix : bouger.  Croix, rond, carre, triangle ou R1 : tirer.  Vibre si tu es touche.')
    move = -1 if k < 5 else 1 if k < 10 else 0
    ship = max(10, min(110, ship + move * 5))
    fire = k % 3 == 0 and k < 11
    if fire:
        shots.append([ship + 4, 105])
    for sh in shots:
        sh[1] -= 14
    shots = [s for s in shots if s[1] > 14]
    boom = (ship + 4, 116, k - 10) if hit else None
    texts = [(60, 'STAGE 2', (255, 220, 0))] if k < 3 else []
    scr, _ = led_screen(panel_img(texts, stage_ship=2, ship_x=ship if not hit else ship, shots=[tuple(s) for s in shots], boom=boom), 470, 100)
    im.paste(scr, (480, 130))
    shake = (6 if k % 2 else -6) if hit else 0
    controller(d, 230, 340, led=(40, 230, 80), cross=fire, shake=shake, stick=move)
    if hit:
        d.text((150, 470), 'Bzzz ! la manette vibre', fill=(255, 90, 90), font=FB)
    add(im, 1800 if k == 15 else 220)

# ---------------------------------------------------------------- GIF (palette commune, taille raisonnable)
pal_src = Image.new('RGB', (WD, HT * 3))
for i, f in enumerate((frames[7], frames[25], frames[-5])):
    pal_src.paste(f, (0, HT * i))
pal = pal_src.quantize(colors=128, method=Image.Quantize.MEDIANCUT)
q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
out = os.path.join(DOC, 'manette-ps4.gif')
q[0].save(out, save_all=True, append_images=q[1:], duration=durations, loop=0, optimize=True)
print(out, len(frames), 'images', os.path.getsize(out) // 1024, 'Ko')
