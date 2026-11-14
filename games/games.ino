/**************************************************************************************************/
/*  Game hub : jeux d'arcade sur dalles LED HUB75 128x64, ESP32 classique, manette PS4 Bluetooth  */
/*                                                                                                */
/*  Pas de wifi : la manette PS4 se connecte en Bluetooth directement a l'ESP32 des dalles.       */
/*  Partition unique de 3 Mo (Huge APP, pas de mise a jour par wifi) : place pour d'autres jeux.  */
/*                                                                                                */
/*  Jeu : shoot'em up (repris du mode Jeu du VU-metre). Sans joueur, demo automatique ; un bouton */
/*  de tir lance une partie. Stages, vaisseau qui evolue, arrivee des ennemis en vol.             */
/*  Manette : stick / croix = bouger (aussi avancer / reculer), croix rond carre triangle R1 L1 R2 */
/*            = tirer, START = menu / pause, Share + haut / bas = luminosite,                     */
/*            Share + Options 3 s = redemarrer.                                                   */
/*                                                                                                */
/*  Ecran : 1 a 4 dalles 128x64 chainees, l'une sous l'autre, cote a cote ou en carre 2x2,        */
/*  ecran retourne, une dalle sur deux retournee (serpentin). Meme brochage que l'horloge.        */
/*                                                                                                */
/*  Reglages par le moniteur serie (115200 bauds, taper ?) : adresse Bluetooth de la manette,     */
/*  dalles, nombre de bits de couleur, volume.                                                   */
/*                                                                                                */
/*  Son (option) : GPIO 22 en PWM -> filtre RC -> ampli analogique (docs/branchement-son.png).   */
/*  Bruitages 8 bits (tir, explosions, pique, jingles, menu), muets pendant la demo.             */
/**************************************************************************************************/
#include <Arduino.h>
#include <Preferences.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include "src/PS4Controller/PS4Controller.h"  // copie corrigee de PS4Controller 1.1.0 (GPL-3), voir ps4_l2cap.c
#include <esp_gap_bt_api.h>
#include <esp_timer.h>
#include "game_types.h"
extern "C" void BTM_SetDefaultLinkPolicy(uint16_t settings);  // pile Bluetooth (Bluedroid) : sans mode veille

#define GAMES_VERSION "3.20"

// ---------------------------------------------------------------- brochage (identique a l'horloge)
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN 32
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

Preferences prefs;
MatrixPanel_I2S_DMA *dma = nullptr;
// Dalles 128x64 chainees, rangees en grille (cols x rows) : la chaine fait (128 x nombre) x 64,
// presentee comme un ecran (128 x cols) x (64 x rows). Ordre : ligne du haut de gauche a droite, puis la suivante.
// flip : tout l'ecran tourne de 180 degres ; serp : une dalle sur deux (2e, 4e) tournee de 180 degres.
class TiledPanels : public Adafruit_GFX {
 public:
  TiledPanels(MatrixPanel_I2S_DMA *d, uint8_t cols, uint8_t rows, bool flip, bool serp)
    : Adafruit_GFX(128 * cols, 64 * rows), m(d), nc(cols), fl(flip), sp(serp) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    if (fl) { x = _width - 1 - x; y = _height - 1 - y; }
    int p = (y >> 6) * nc + (x >> 7), lx = x & 127, ly = y & 63;
    if (sp && (p & 1)) { lx = 127 - lx; ly = 63 - ly; }
    m->drawPixel(p * 128 + lx, ly, c);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int x0 = max(0, (int)x), x1 = min((int)_width, x + w), y0 = max(0, (int)y), y1 = min((int)_height, y + h);
    if (x1 <= x0 || y1 <= y0) return;
    if (fl) { int t = x0; x0 = _width - x1; x1 = _width - t; t = y0; y0 = _height - y1; y1 = _height - t; }
    for (int ty = y0; ty < y1; ty = (ty & ~63) + 64)  // morceau de rectangle sur chaque dalle
      for (int tx = x0; tx < x1; tx = (tx & ~127) + 128) {
        int ex = min(x1, (tx & ~127) + 128), ey = min(y1, (ty & ~63) + 64);
        int p = (ty >> 6) * nc + (tx >> 7), lx = tx & 127, ly = ty & 63, rw = ex - tx, rh = ey - ty;
        if (sp && (p & 1)) { lx = 128 - lx - rw; ly = 64 - ly - rh; }
        m->fillRect(p * 128 + lx, ly, rw, rh, c);
      }
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
  void fillScreen(uint16_t c) override { m->fillScreen(c); }
 private:
  MatrixPanel_I2S_DMA *m;
  uint8_t nc;
  bool fl, sp;
};
Adafruit_GFX *gfx = nullptr;
// configuration des dalles (moniteur serie, commande c)
uint8_t nPanels = 2;   // 1 a 4 dalles 128x64
uint8_t arrange = 0;   // 0 l'une sous l'autre, 1 cote a cote, 2 carre 2x2 (4 dalles)
bool flipAll = false;  // ecran retourne (180 degres)
bool serpent = false;  // une dalle sur deux retournee
uint8_t panelCols() { return arrange == 2 && nPanels == 4 ? 2 : arrange == 1 ? nPanels : 1; }
uint8_t panelRows() { return nPanels / panelCols(); }
int W = 128, H = 128;
uint8_t bright = 120;
uint8_t colorBits = 0;  // profondeur de couleur obtenue pour les dalles
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return dma->color565(r, g, b); }

// ---------------------------------------------------------------- commandes (manette -> jeu)
uint8_t keysPs4 = 0;                // touches de la manette PS4 : 1 gauche, 2 droite, 4 feu, 8 haut, 16 bas, 32 rond
int gPYBase = 0;                    // hauteur de depart du vaisseau (en bas) ; il peut monter jusqu'a gPYMin
int gPYMin = 0;
volatile bool rumbleReq = false;    // vaisseau touche : la manette PS4 vibre
volatile uint32_t lastInput = 0;    // dernier appui d'un joueur (millis)
volatile bool startReq = false;     // FEU pendant la demo : nouvelle partie
bool gDemo = true;                  // le jeu joue tout seul (menu par-dessus quand la manette est la)
bool gPaused = false;               // START pendant une partie : pause
uint8_t gDiff = 1;                  // difficulte : 0 facile, 1 normal, 2 difficile
const char *const DIFF_NAME[3] = { "FACILE", "NORMAL", "DIFFICILE" };
uint8_t menuSel = 0;                // ligne choisie : 0 jouer, 1 niveau, 2 luminosite, 3 volume, 4 scores, 5 demo
#define MENU_N 6
int brightPct() { return (bright * 100 + 127) / 255 / 10 * 10; }  // luminosite en %, arrondie a 10 %
void setBrightPct(int p) {  // 0 % : tres faible mais jamais noir (le menu reste visible)
  p = constrain(p, 0, 100);
  bright = p == 0 ? 3 : p * 255 / 100;
  dma->setBrightness8(bright);
  prefs.putUChar("bright", bright);
}
bool gAttract = false;              // mode demo : menu cache (ligne DEMO, ou 30 s sans toucher la manette)
#define MENU_IDLE_MS 30000UL        // menu sans activite : passage en mode demo
bool menuScores = false;            // page des meilleurs scores affichee
#define N_GAMES 5
const char *const GAME_NAME[N_GAMES] = { "ESSAIM", "RAID", "COURSE", "RAQUETTES", "BULLES" };
uint8_t gGame = 0;                  // jeu choisi dans le menu (gauche / droite sur la ligne JOUER)
const char *topKey() { static const char *const K[N_GAMES] = { "top", "top1", "top2", "top3", "top4" }; return K[gGame % N_GAMES]; }
#define TOP_N 5
HiScore gBest[TOP_N];                // meilleurs scores (gardes en memoire)
uint8_t gLastRank = 0;              // place obtenue a la derniere partie (0 : hors tableau)
bool gNameEntry = false;            // saisie du nom apres un score qui entre au tableau
char gName[4] = "AAA";              // nom en cours de saisie (3 lettres)
uint8_t gNamePos = 0;               // lettre en cours (0 a 2)
const char NAME_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
#define IDLE_TO_DEMO_MS 60000UL     // partie abandonnee : retour a la demo apres 1 min sans appui
char ipText[20] = "";  // adresse Bluetooth de l'ESP32, affichee pendant la demo

// ---------------------------------------------------------------- petite police 3x5 (score)
const uint8_t TF_DIG[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
                                { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
const uint8_t TF_R[5] = { 6, 5, 6, 5, 5 }, TF_E[5] = { 7, 4, 6, 4, 7 }, TF_S[5] = { 7, 4, 7, 1, 7 },
              TF_C[5] = { 7, 4, 4, 4, 7 }, TF_D[5] = { 6, 5, 5, 5, 6 }, TF_A[5] = { 2, 5, 7, 5, 5 },
              TF_B[5] = { 6, 5, 6, 5, 6 }, TF_F[5] = { 7, 4, 6, 4, 4 }, TF_M[5] = { 5, 7, 7, 5, 5 },
              TF_N[5] = { 6, 5, 5, 5, 5 }, TF_T[5] = { 7, 2, 2, 2, 2 }, TF_U[5] = { 5, 5, 5, 5, 7 },
              TF_P[5] = { 6, 5, 6, 4, 4 }, TF_COL[5] = { 0, 2, 0, 2, 0 }, TF_X[5] = { 5, 5, 2, 5, 5 },
              TF_G[5] = { 7, 4, 5, 5, 7 };
const uint8_t *tfGlyph(char c) {
  if (c >= '0' && c <= '9') return TF_DIG[c - '0'];
  switch (c) {
    case 'R': return TF_R; case 'E': return TF_E; case 'S': return TF_S; case 'C': return TF_C;
    case 'O': return TF_DIG[0]; case 'D': return TF_D; case 'A': return TF_A; case 'B': return TF_B;
    case 'F': return TF_F; case 'M': return TF_M; case 'N': return TF_N; case 'T': return TF_T;
    case 'U': return TF_U; case 'P': return TF_P; case ':': return TF_COL; case 'X': return TF_X; case 'G': return TF_G;
  }
  return nullptr;
}
int tinyWidth(const char *s) { return strlen(s) * 4 - 1; }
void tinyText(Adafruit_GFX &g, int x, int y, const char *s, uint16_t col) {
  for (; *s; s++, x += 4) {
    const uint8_t *gl = tfGlyph(*s);
    if (!gl) continue;
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 3; c++)
        if (gl[r] & (4 >> c)) g.drawPixel(x + c, y + r, col);
  }
}

// ---------------------------------------------------------------- son : une broche PWM (GPIO 22) vers un ampli analogique
// La broche sert de petite sortie audio : PWM tres rapide (250 kHz, 8 bits) dont le rapport cyclique est mis a jour
// 16 000 fois par seconde par une interruption materielle (sur le coeur 0). Le filtre RC (1 k + 10 nF) ne garde
// que le son. Synthese en nombres entiers, 3 voix melangees :
//   voix 0 : bruitages (onde carree avec glissando), voix 1 : bruit (explosions), voix 2 : jingles (notes).
// Volume et decroissance agissent sur l'amplitude (le timbre ne change pas), pas de craquement pendant les glissandos.
// Branchement : GPIO 22 -> 1 k -> (10 nF vers la masse) -> 10 uF -> entree de l'ampli (voir docs/branchement-son.png).
#include <soc/ledc_struct.h>
#define SON_ACTIF 1
#define SND_PIN 22
#define SND_LEDC 6           // canal LEDC 6 (groupe rapide 0) : libre, les dalles n'utilisent pas le LEDC
#define SND_RATE 16000       // echantillons par seconde
#define SND_CARRIER 250000   // frequence du PWM (bien au-dessus de l'audible, filtree par le RC)
uint8_t volume = 6;          // 0 a 10 (menu VOLUME ou moniteur serie : v N)
struct SynthVoice {          // une voix (modifiee par sndRaw, lue par l'interruption)
  volatile uint32_t phase, inc;   // phase sur 32 bits, increment par echantillon (frequence)
  volatile int32_t incStep;       // variation de l'increment par echantillon (glissando)
  volatile int32_t amp, ampStep;  // amplitude << 8 et sa decroissance par echantillon
  volatile uint32_t left;         // echantillons restants (0 : voix muette)
  volatile uint8_t prio;
  volatile bool noise;
};
SynthVoice sv[3];
volatile uint16_t sndLfsr = 0xACE1;  // generateur de bruit
const Note *melody = nullptr;  // jingle en cours (notes jouees l'une apres l'autre)
uint8_t melodyLen = 0, melodyPos = 0;
uint32_t melodyNext = 0;
bool sndReady = false;
hw_timer_t *sndTimer = nullptr;

// interruption : un echantillon (somme des voix) -> rapport cyclique du PWM
void IRAM_ATTR sndIsr() {
  int32_t mix = 0;
  for (int i = 0; i < 3; i++) {
    SynthVoice &v = sv[i];
    if (!v.left) continue;
    uint32_t old = v.phase;
    v.phase += v.inc;
    int32_t s;
    if (v.noise) {  // bruit : le generateur avance a chaque tour de phase (la frequence regle le grain)
      if (v.phase < old) {
        uint16_t l = sndLfsr;
        sndLfsr = (l >> 1) ^ (-(int16_t)(l & 1) & 0xB400);
      }
      s = (sndLfsr & 1) ? 1 : -1;
    } else {
      s = (v.phase & 0x80000000u) ? 1 : -1;  // onde carree
    }
    mix += s * (v.amp >> 8);
    v.inc += v.incStep;
    v.amp -= v.ampStep;
    if (v.amp < 0) v.amp = 0;
    v.left--;
  }
  int32_t out = 128 + mix / 2;
  if (out < 0) out = 0;
  if (out > 255) out = 255;
  LEDC.channel_group[0].channel[SND_LEDC].duty.duty = out << 4;  // rapport cyclique (4 bits de fraction)
  LEDC.channel_group[0].channel[SND_LEDC].conf1.duty_start = 1;
}

// lance un son : frequence de depart, d'arrivee, duree, volume 0-1, bruit ou carre, priorite
void sndRaw(float f0, float f1, uint16_t ms, float vol, bool noise, uint8_t prio) {
  if (!SON_ACTIF || !sndReady || volume == 0) return;
  int vi = noise ? 1 : prio >= 2 ? 2 : 0;  // bruit : voix 1 ; jingles : voix 2 ; bruitages : voix 0
  SynthVoice &v = sv[vi];
  if (v.left && prio < v.prio) return;  // un son plus important joue deja
  uint32_t n = (uint32_t)SND_RATE * ms / 1000;
  if (!n) return;
  float k = 4294967296.0f / SND_RATE;
  int32_t amp = (int32_t)(vol * volume / 10.0f * 120) << 8;  // 120 : marge pour le melange des 3 voix
  v.left = 0;  // voix coupee pendant la mise a jour (l'interruption l'ignore)
  v.inc = (uint32_t)(f0 * k);
  v.incStep = (int32_t)((f1 - f0) * k / n);
  v.amp = amp;
  v.ampStep = (int32_t)(amp * 0.75f / n);  // decroissance jusqu'a 25 % de l'amplitude
  v.noise = noise;
  v.prio = prio;
  v.left = n;  // la voix demarre
}
void sndPlay(float f0, float f1, uint16_t ms, float vol, bool noise, uint8_t prio) {
  if (!gDemo) sndRaw(f0, f1, ms, vol, noise, prio);  // demo muette
}
void sfxMenu() { sndRaw(1320, 1320, 40, 0.35f, false, 1); }

void sndMelody(const Note *m, uint8_t len) {
  if (gDemo || volume == 0) return;
  melody = m; melodyLen = len; melodyPos = 0; melodyNext = 0;
}
void sndUpdate() {  // notes des jingles (appele a chaque tour de boucle)
  if (!melody || millis() < melodyNext) return;
  if (melodyPos >= melodyLen) { melody = nullptr; return; }
  const Note &n = melody[melodyPos++];
  if (n.hz) sndPlay(n.hz, n.hz, n.ms * 9 / 10, 0.45f, false, 2);
  melodyNext = millis() + n.ms;
}
const Note JINGLE_STAGE[] = { { 523, 90 }, { 659, 90 }, { 784, 90 }, { 1047, 220 } };
const Note JINGLE_OVER[] = { { 523, 200 }, { 392, 200 }, { 330, 200 }, { 262, 450 } };
const Note JINGLE_ARME[] = { { 784, 70 }, { 988, 70 }, { 1175, 70 }, { 1568, 200 } };
void sfxShot() { sndPlay(1600, 420, 60, 0.30f, false, 1); }                               // piou
void sfxBoom() { sndPlay(3200, 260, 320, 0.9f, true, 2); }                                // pschhh
void sfxHit() { sndPlay(2400, 120, 700, 1.0f, true, 3); sndPlay(300, 55, 600, 0.6f, false, 3); }  // gros boum + grondement
void sfxDive() { sndPlay(1200, 380, 450, 0.22f, false, 1); }                              // piqué qui descend

void sndTimerStart(void *) {  // cree l'interruption depuis le coeur 0 (elle y reste)
  sndTimer = timerBegin(0, 80, true);  // 1 MHz
  timerAttachInterrupt(sndTimer, &sndIsr, true);
  timerAlarmWrite(sndTimer, 1000000 / SND_RATE, true);
  timerAlarmEnable(sndTimer);
  sndReady = true;
  vTaskDelete(NULL);
}
void audioBegin() {
#if !SON_ACTIF
  volume = 0;
  Serial.println("Son : desactive (SON_ACTIF 0)");
  return;
#endif
  volume = constrain(prefs.getUChar("vol", 6), 0, 10);
  ledcSetup(SND_LEDC, SND_CARRIER, 8);
  ledcAttachPin(SND_PIN, SND_LEDC);
  ledcWrite(SND_LEDC, 128);  // repos au milieu (le condensateur de liaison bloque le continu)
  xTaskCreatePinnedToCore(sndTimerStart, "son", 2048, NULL, 5, NULL, 0);
  delay(20);
  Serial.printf("Son : synthese 3 voix, %d Hz, PWM %d kHz sur GPIO %d, volume %u/10\n", SND_RATE, SND_CARRIER / 1000, SND_PIN, volume);
}

// ---------------------------------------------------------------- jeu : shoot'em up d'arcade (repris du VU-metre)
// Sprites originaux multicolores (9x7), formation qui oscille, ennemis qui piquent en courbe,
// ecran d'arcade : SCORE et RECORD en haut, vies et vague en bas (128x128).
// Sans joueur, un pilote automatique joue (demo). Les ennemis pulsent doucement.
// Dessin sans scintillement : seuls les rectangles dessines a l'image precedente sont effaces.
#define G_MAXE 100
#define G_SHOTS 9
#define G_BOMBS 10
#define G_BOOMS 10
#define G_STARS 128
#define G_RECTS 800
#define G_MAXROWS 7
const int GW = 9, GH = 7;  // taille des ennemis
struct GEnemy { float x, y, vx, vy; int16_t t; int8_t row, col, type, hp, dir; uint8_t state, path; bool alive; };
// state : 0 rang, 1 pique, 2 retour a sa place, 3 arrivee en vol (trajectoire courbe, en file)
struct GDot { float x, y; bool on; float vx; };  // vx : tirs en eventail
struct GBoom { int16_t x, y; uint8_t t; };
struct GStar { int16_t x; float y, v; uint8_t hue; };
struct GRect { int16_t x, y; uint8_t w, h; };
GEnemy gEn[G_MAXE];
GDot gShot[G_SHOTS], gBomb[G_BOMBS];
GBoom gBoom[G_BOOMS];
GStar gStar[G_STARS];
GRect gOld[G_RECTS];
int gNE = 0, gCols = 4, gRows = 3, gTop = 13, gPY = 49, gNStar = 0, gNOld = 0, gLives = 0, gWave = 0, gDead = 0, gCool = 0;
uint32_t gFrame = 0, gScore = 0, gHi = 0, gLastStep = 0;
float gPx = 0, gSway = 0;
int8_t gRowType[G_MAXROWS];

// sprites : '.' transparent, sinon une lettre de la palette ci-dessous
// 4 mondes (un par stage, en boucle) : 3 types d'ennemis x 2 images (9x7), sprites originaux
// type 0 : rang du haut (2 tirs), types 1 et 2 : rangs suivants
const char *const SPR_W[4][3][2][GH] = {
  {  // nebuleuse
    { { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", "..c.c.c..", ".c..c..c.", "c...c...c" },
      { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", ".c..c..c.", "..c.c.c..", "...c.c..." } },
    { { "v...o...v", ".v.ooo.v.", "..oowoo..", "vvooooovv", "v.ooooo.v", "..o.o.o..", ".o.....o." },
      { ".v..o..v.", "..vooov..", "..oowoo..", ".vooooov.", "v.ooooo.v", ".o..o..o.", "o.......o" } },
    { { "c.......c", ".c.ggg.c.", "..gwywg..", "cgyyyyygc", "c.ggggg.c", "..g.y.g..", "...g.g..." },
      { "cc.....cc", "..cgggc..", "..gwywg..", ".gyyyyyg.", "..ggggg..", "..g.y.g..", "...g.g..." } },
  },
  {  // crabes
    { { "...ooo...", ".oowwwoo.", "ooooooooo", "o.y.y.y.o", ".ooooooo.", "..o...o..", ".o.....o." },
      { "...ooo...", ".oowwwoo.", "ooooooooo", "o..y.y..o", ".ooooooo.", ".o.....o.", "o.......o" } },
    { { "r.......r", "r..rrr..r", ".rrwrwrr.", "..rrrrr..", ".rr.r.rr.", "r.r...r.r", "..r...r.." },
      { ".r.....r.", "r..rrr..r", "rrrwrwrrr", "..rrrrr..", ".rr.r.rr.", ".r.....r.", "r.......r" } },
    { { "..c...c..", "...ccc...", ".bbcwcbb.", "bbbbbbbbb", ".b.b.b.b.", "..b...b..", "........." },
      { "c.......c", ".c.ccc.c.", ".bbcwcbb.", "bbbbbbbbb", "b.b.b.b.b", ".b.....b.", "........." } },
  },
  {  // cristaux
    { { "....v....", "...vvv...", "..vvwvv..", ".vvvwvvv.", "..vvvvv..", "...vvv...", "....v...." },
      { "....v....", "...vwv...", "..vvvvv..", ".vvvvvvv.", "..vvwvv..", "...vvv...", "....v...." } },
    { { "y.......y", ".y.yyy.y.", "..yywyy..", ".yyyyyyy.", "y.y.y.y.y", "..y.y.y..", "...y.y..." },
      { ".y.....y.", "y..yyy..y", "..yywyy..", ".yyyyyyy.", ".y.y.y.y.", "..y.y.y..", "...y.y..." } },
    { { "..ggggg..", ".gwgggwg.", "ggggggggg", "g.g.g.g.g", ".g.g.g.g.", "g.......g", "........." },
      { "..ggggg..", ".gwgggwg.", "ggggggggg", ".g.g.g.g.", "g.g.g.g.g", ".g.....g.", "........." } },
  },
  {  // orbes
    { { "..rrrrr..", ".rrwwwrr.", "rrwwbwwrr", "rrwbbbwrr", "rrwwbwwrr", ".rrwwwrr.", "..rrrrr.." },
      { "..rrrrr..", ".rrrrrrr.", "rrwwwwwrr", "rrwbbbwrr", "rrwwwwwrr", ".rrrrrrr.", "..rrrrr.." } },
    { { "..ppppp..", ".pp.p.pp.", "ppppppppp", "ppwpppwpp", ".p.p.p.p.", "p..p.p..p", ".p.....p." },
      { "..ppppp..", ".pp.p.pp.", "ppppppppp", "ppwpppwpp", ".p.p.p.p.", ".p.p.p.p.", "p.......p" } },
    { { "c.......c", ".c.ooo.c.", "..cowoc..", ".ccccccc.", "c.ccccc.c", "..c.c.c..", ".c.....c." },
      { ".c.....c.", "c..ooo..c", "..cowoc..", "ccccccccc", "..ccccc..", ".c..c..c.", "c.......c" } },
  },
};
const char *const WORLD_NAME[4] = { "NEBULEUSE", "CRABES", "CRISTAUX", "ORBES" };
const char WORLD_HURT[4][2] = { { 'p', 'o' }, { 'o', 'r' }, { 'v', 'p' }, { 'r', 'y' } };  // couleur changee quand l'ennemi est touche
int gWorld = 0;  // monde du stage : 0 nebuleuse (piques en boucle), 1 crabes (piques a deux, balancement large),
                 // 2 cristaux (plus resistants, piques en zigzag), 3 orbes (formation qui descend, tirs vises)
int gBaseHp(int type) { return (type == 0 ? 2 : 1) + (gWorld == 2 ? 1 : 0); }
// vaisseau du joueur : il evolue avec les stages (1 base, 2 tir rapide, 3 double canon, 4+ triple canon)
const char *const SPR_SHIP[4][8] = {
  { "....c....", "...ccc...", "...cwc...", "..bcwcb..", ".bbcccbb.", "bbcbcbcbb", "b.o...o.b", "..o...o.." },
  { "....c....", "...ccc...", "...cwc...", "..gcwcg..", ".ggcccgg.", "ggcgcgcgg", "g.o...o.g", "..o...o.." },
  { "..c...c..", "..c.c.c..", ".cc.w.cc.", ".cbcwcbc.", "bbbcccbbb", "bybcbcbyb", "b.o...o.b", "..o...o.." },
  { "c...c...c", "c..ccc..c", "cc.cwc.cc", "rbbcwcbbr", "rbbcccbbr", "bbcbcbcbb", "b.oy.yo.b", "..o...o.." }
};
const char *const TIER_NAME[4] = { "", "TIR RAPIDE", "DOUBLE CANON", "TRIPLE CANON" };
int gStageT = 0;
bool gLifeUp = false;     // vie gagnee a la fin du stage (affichee avec l'annonce)          // images restantes de l'annonce "STAGE n"
uint32_t gBonus = 0;      // bonus de fin de stage affiche avec l'annonce
int gTier() { return min(gWave, 4) - 1; }
const char *const SPR_LIFE[4] = { "..c..", ".ccc.", "bcwcb", "b...b" };
// palette : w blanc, r rouge, y jaune, b bleu, c cyan, g vert, p magenta, o orange, v violet
const char G_PAL_CH[] = "wrybcgpov";
const uint8_t G_PAL_RGB[9][3] = { { 255, 255, 255 }, { 255, 40, 40 }, { 255, 220, 0 }, { 40, 90, 255 }, { 0, 220, 255 },
                                  { 40, 230, 80 }, { 255, 60, 200 }, { 255, 140, 0 }, { 150, 60, 255 } };
uint16_t gPal[9];  // palette de l'image en cours (luminosite qui suit les basses)
int gPalIdx(char ch) {
  for (int i = 0; i < 9; i++)
    if (G_PAL_CH[i] == ch) return i;
  return -1;
}
bool gOverflow = false;  // trop de zones dessinees : tout l'ecran sera efface a l'image suivante
void gMark(int x, int y, int w, int h) {
  if (gNOld < G_RECTS) gOld[gNOld++] = { (int16_t)x, (int16_t)y, (uint8_t)w, (uint8_t)h };
  else gOverflow = true;
}
void gPix(int x, int y, uint16_t c) {
  if (x >= 0 && x < W && y >= 0 && y < H) gfx->drawPixel(x, y, c);
  gMark(x, y, 1, 1);
}
// dessine un sprite ; swapFrom -> swapTo permet de changer une couleur (meduse touchee)
void gSprite(const char *const *rows, int w, int h, int x, int y, const uint16_t *pal, char swapFrom, char swapTo) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      char ch = rows[r][c];
      if (ch == '.') continue;
      if (ch == swapFrom) ch = swapTo;
      int i = gPalIdx(ch), px = x + c, py = y + r;
      if (i >= 0 && px >= 0 && px < W && py >= 0 && py < H) gfx->drawPixel(px, py, pal[i]);
    }
  gMark(x, y, w, h);
}
float gSlotX(int col) { return (W - gCols * 11) / 2 + 1 + col * 11 + gSway; }
float gDescend = 0;  // monde des orbes : la formation descend et remonte
float gSlotY(int row) { return gTop + row * 9 + gDescend; }
void gBoomAt(int x, int y) {
  for (int i = 0; i < G_BOOMS; i++)
    if (gBoom[i].t == 0) { gBoom[i] = { (int16_t)x, (int16_t)y, 1 }; return; }
}
// Trajectoires d'arrivee (en % de l'ecran), parcourues en file par chaque groupe puis retour a sa place.
// 0 : plongee depuis le haut avec une boucle sur le cote ; 1 : entree par le cote, boucle au centre.
// Chaque trajectoire existe aussi en miroir (groupes qui arrivent par paires, de chaque cote).
#define G_PATH_PTS 9
#define G_PATH_SEG 10  // images par segment
const int8_t G_PATH[2][G_PATH_PTS][2] = {
  { { 42, -12 }, { 45, 12 }, { 52, 34 }, { 66, 52 }, { 84, 58 }, { 92, 44 }, { 82, 30 }, { 66, 36 }, { 58, 50 } },
  { { -10, 78 }, { 14, 74 }, { 32, 64 }, { 44, 48 }, { 44, 32 }, { 34, 24 }, { 24, 32 }, { 26, 46 }, { 40, 52 } }
};
// position sur la trajectoire a l'instant t (Catmull-Rom) ; false une fois la trajectoire finie
bool gPathPos(uint8_t kind, bool mirror, int t, float &x, float &y) {
  int seg = t / G_PATH_SEG;
  if (seg >= G_PATH_PTS - 1) return false;
  float u = (t % G_PATH_SEG) / (float)G_PATH_SEG, u2 = u * u, u3 = u2 * u;
  const int8_t(*P)[2] = G_PATH[kind];
  int i0 = max(seg - 1, 0), i1 = seg, i2 = seg + 1, i3 = min(seg + 2, G_PATH_PTS - 1);
  float c[2];
  for (int k = 0; k < 2; k++)
    c[k] = 0.5f * (2 * P[i1][k] + (-P[i0][k] + P[i2][k]) * u + (2 * P[i0][k] - 5 * P[i1][k] + 4 * P[i2][k] - P[i3][k]) * u2 +
                   (-P[i0][k] + 3 * P[i1][k] - 3 * P[i2][k] + P[i3][k]) * u3);
  float hs = min((float)H, W * 1.2f);  // ecran tres haut : boucles pas trop etirees
  x = c[0] * W / 100.0f - GW / 2;
  if (mirror) x = W - GW - x;
  y = c[1] * hs / 100.0f;
  return true;
}

void gNewWave() {
  gWorld = gWave % 4;  // gWave vaut encore le stage precedent : stage 1 -> monde 0, stage 2 -> monde 1...
  gCols = constrain((W - 6) / 11, 4, 20);
  gNE = 0;
  for (int r = 0; r < gRows; r++)
    for (int c = 0; c < gCols && gNE < G_MAXE; c++) {
      if (gRowType[r] == 0 && (c & 1)) continue;  // meduses une place sur deux
      GEnemy &e = gEn[gNE++];
      e = {};
      e.row = r; e.col = c; e.type = gRowType[r]; e.alive = true;
      e.hp = gBaseHp(e.type);
      e.x = -50; e.y = -50;  // hors ecran jusqu'a son tour
      e.state = 3;
    }
  // arrivee en vol : groupes en file, par paires miroir (gauche / droite), une paire toutes les 60 images
  int gs = max(8, (gNE + 5) / 6);
  for (int i = 0; i < gNE; i++) {
    int g = i / gs, pair = g / 2;
    GEnemy &e = gEn[i];
    e.dir = (g & 1) ? 1 : 0;   // miroir
    e.path = (pair + gWorld) & 1;  // trajectoire 0 ou 1, en alternance (ordre change selon le monde)
    e.t = -(pair * 60 + (i % gs) * 6);  // depart decale : la file s'etire sur la trajectoire
  }
  gWave++;
  gStageT = 100;  // annonce pendant 2,5 s : les ennemis commencent leur arrivee sans tirer
  if (gWave >= 2 && gWave <= 4) sndMelody(JINGLE_ARME, 4);  // nouvelle arme
  else sndMelody(JINGLE_STAGE, 4);
}
void gReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;  // facile : 5 vies
  gPaused = false;
  gScore = 0; gWave = 0; gDead = 0; gCool = 0; gBonus = 0; gLifeUp = false;
  gPx = W / 2 - 4;
  if (gPYBase) gPY = gPYBase;
  for (auto &s : gShot) s.on = false;
  for (auto &b : gBomb) b.on = false;
  for (auto &b : gBoom) b.t = 0;
  gNewWave();
}
void gameSetup() {
  gfx->fillScreen(0);
  gNOld = 0;
  bool tall = H >= 128;
  bool huge = H >= 256;  // ecran vertical 128x256 : 7 rangees
  gRows = huge ? 7 : tall ? 5 : 3;
  static const int8_t T7[7] = { 0, 0, 1, 1, 1, 2, 2 }, T5[5] = { 0, 1, 1, 2, 2 }, T3[3] = { 0, 1, 2 };
  for (int r = 0; r < gRows; r++) gRowType[r] = huge ? T7[r] : tall ? T5[r] : T3[r];
  gTop = 13;
  gPY = tall ? H - 10 : H - 8;  // vaisseau en bas de l'ecran (vies en haut a droite)
  gPYBase = gPY;
  gPYMin = max(gTop + gRows * 9 + 12, H * 55 / 100);  // zone du vaisseau : les 45 % du bas, sous la formation
  topLoad();
  if (gLives <= 0 && gDead == 0) gReset();
  gNStar = min(G_STARS, W * H / 220);
  for (int i = 0; i < gNStar; i++) gStar[i] = { (int16_t)random(W), (float)random(12, H), 0.2f + random(100) / 120.0f, (uint8_t)random(9) };
  gLastStep = millis();
}
// tir du vaisseau selon son evolution ; renvoie false si trop de tirs deja a l'ecran
bool gFire() {
  static const int8_t N[4] = { 1, 1, 2, 3 }, COOL[4] = { 8, 5, 6, 7 };
  int t = gTier(), n = N[t], freeShots = 0;
  for (auto &s : gShot) freeShots += !s.on;
  if (freeShots < n) return false;
  for (int k = 0; k < n; k++) {
    float x = gPx + 4, vx = 0;
    if (n == 2) x = gPx + (k ? 6 : 2);          // double canon : 2 tirs paralleles
    if (n == 3) vx = (k - 1) * 0.5f;            // triple canon : eventail
    for (auto &s : gShot)
      if (!s.on) { s = { x, (float)gPY - 3, true, vx }; break; }
  }
  gCool = COOL[t];
  sfxShot();
  return true;
}
int gEffDiff() { return gDemo ? 1 : gDiff; }  // la demo joue en normal

// meilleurs scores : chargement (ancien record repris) et ajout en fin de partie
void setBest(HiScore &h, uint32_t score, uint8_t stage, uint8_t diff, const char *name) {  // remplit une ligne du tableau
  h.score = score;
  h.stage = stage;
  h.diff = diff;
  strncpy(h.name, name, 3);
  h.name[3] = 0;
}
void topLoad() {
  memset(gBest, 0, sizeof(gBest));
  size_t len = prefs.isKey(topKey()) ? prefs.getBytesLength(topKey()) : 0;
  if (len == sizeof(gBest)) {
    prefs.getBytes(topKey(), gBest, sizeof(gBest));
  } else if (gGame == 0 && len == sizeof(HiScoreV1) * TOP_N) {  // ancien tableau sans noms : repris avec le nom "---"
    HiScoreV1 old[TOP_N];
    prefs.getBytes("top", old, sizeof(old));
    for (int i = 0; i < TOP_N; i++) setBest(gBest[i], old[i].score, old[i].stage, old[i].diff, "---");
  } else if (gGame == 0) {  // 1er lancement : ancien record seul
    setBest(gBest[0], prefs.getUInt("hiscore", 0), 0, 1, "---");
  }
  for (auto &b : gBest) b.name[3] = 0;
  gHi = gBest[0].score;
  String last = prefs.getString("lastname", "AAA");  // dernier nom saisi, propose la fois suivante
  for (int i = 0; i < 3; i++) gName[i] = i < (int)last.length() ? last[i] : 'A';
  gName[3] = 0;
}
void topAdd() {
  gLastRank = 0;
  if (!gScore) return;
  int pos = TOP_N;
  while (pos > 0 && gScore > gBest[pos - 1].score) pos--;
  if (pos >= TOP_N) return;
  for (int i = TOP_N - 1; i > pos; i--) gBest[i] = gBest[i - 1];
  setBest(gBest[pos], gScore, (uint8_t)min(gWave, 255), gDiff, "---");  // nom saisi juste apres
  prefs.putBytes(topKey(), gBest, sizeof(gBest));
  gLastRank = pos + 1;
  gHi = gBest[0].score;
}
void gHitPlayer() {
  int deadY = gPY;
  if (!gDemo) rumbleReq = true;  // vibration seulement pendant une partie
  sfxHit();
  if (gLives == 1) sndMelody(JINGLE_OVER, 4);  // derniere vie perdue
  gBoomAt(gPx + 4, deadY + 3);
  gPY = gPYBase;  // le vaisseau suivant repart d'en bas
  gLives--;
  gDead = gLives > 0 ? 50 : 120;  // pause avant le vaisseau suivant (plus longue apres GAME OVER)
  if (!gDemo && gLives <= 0) topAdd();  // fin de partie : meilleurs scores (joueurs seulement)
}
void gameStep() {
  gFrame++;
  float loud = gDemo ? 0.2f : 0.35f;  // vitesse du defilement des etoiles
  for (int i = 0; i < gNStar; i++) {  // etoiles
    GStar &s = gStar[i];
    s.y += s.v * (0.4f + 2.5f * loud);
    if (s.y >= H) { s.y = 12; s.x = random(W); }
  }
  // formation et piques
  gSway = sinf(gFrame * (gWorld == 1 ? 0.05f : 0.03f)) * max(1, min(gWorld == 1 ? 10 : 6, (W - gCols * 11) / 2 - 1));
  gDescend = gWorld == 3 ? (1 - cosf(gFrame * 0.012f)) * min(14, H / 9) : 0;
  int alive = 0, entering = 0;
  for (int i = 0; i < gNE; i++) { alive += gEn[i].alive; entering += gEn[i].alive && gEn[i].state == 3; }
  int diveEvery = max(16, 60 - gWave * 7);
  if (gEffDiff() == 0) diveEvery = diveEvery * 3 / 2;               // facile : piques plus rares
  else if (gEffDiff() == 2) diveEvery = max(12, diveEvery * 2 / 3);  // difficile : plus frequents
  if (gDead == 0 && gStageT == 0 && !entering && gFrame % diveEvery == 0 && alive) {  // pas de pique pendant l'arrivee
    int k = random(gNE);
    for (int t = 0; t < gNE; t++, k = (k + 1) % gNE)
      if (gEn[k].alive && gEn[k].state == 0) {
        GEnemy &e = gEn[k];
        e.state = 1; e.vx = 0; e.vy = -0.6f; e.t = 0;
        sfxDive();
        e.dir = e.x < W / 2 ? -1 : 1;  // part en boucle vers l'exterieur
        if (gWorld == 1)  // monde des crabes : le voisin pique avec lui
          for (int n = 0; n < gNE; n++) {
            GEnemy &o = gEn[n];
            if (o.alive && o.state == 0 && o.row == e.row && abs(o.col - e.col) == 1) {
              o.state = 1; o.vx = 0; o.vy = -0.6f; o.t = -6; o.dir = e.dir;
              break;
            }
          }
        break;
      }
  }
  float shipC = gPx + 4;
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    float sx = gSlotX(e.col), sy = gSlotY(e.row);
    if (e.state == 0) {
      e.x = sx; e.y = sy;
    } else if (e.state == 1) {  // pique en courbe : boucle puis descente en visant le vaisseau
      e.t++;
      if (e.t < 0) continue;  // depart decale (pique a deux)
      float curve = gWorld == 2 ? e.dir * 1.6f * sinf(e.t * 0.15f)                   // cristaux : zigzag
                                : e.t < 50 ? e.dir * 1.2f * cosf(e.t * 0.063f) : 0;  // boucle
      e.vx = constrain(e.vx * 0.96f + curve * 0.06f + (shipC - (e.x + 4)) * (e.t < 50 ? 0.0005f : 0.004f), -1.4f, 1.4f);
      e.vy = min(e.vy + 0.03f, 1.3f + 0.1f * gWave);
      e.x += e.vx + curve; e.y += e.vy;
      if (e.y > H) { e.y = -8; e.state = 2; }
    } else if (e.state == 3) {  // arrivee en vol sur sa trajectoire
      if (++e.t < 0) continue;  // pas encore son tour (reste hors ecran)
      float px, py;
      if (gPathPos(e.path, e.dir, e.t, px, py)) { e.x = px; e.y = py; }
      else e.state = 2;  // trajectoire finie : rejoint sa place
    } else {  // retour a sa place
      float dx = sx - e.x, dy = sy - e.y;
      e.x += constrain(dx * 0.1f, -2.2f, 2.2f);
      e.y += constrain(dy * 0.1f, -2.2f, 2.2f);
      if (fabsf(dx) < 0.6f && fabsf(dy) < 0.6f) e.state = 0;
    }
    int chance = e.state == 1 ? 18 : e.state == 3 ? 5 : 2 + gWave;  // tirs ennemis
    if (gEffDiff() == 0) chance = max(1, chance / 2);
    else if (gEffDiff() == 2) chance = chance * 8 / 5;
    if (gDead == 0 && gStageT == 0 && e.y > 0 && e.y < H * 0.6f && random(1000) < chance)
      for (auto &b : gBomb)
        if (!b.on) {
          b = { e.x + 4, e.y + GH, true, 0 };
          if (gWorld == 3) b.vx = constrain((shipC - b.x) / max(20.0f, gPY - b.y) * 1.4f, -0.9f, 0.9f);  // orbes : tir vise
          break;
        }
    if (gDead == 0 && e.state == 1 && e.x + GW - 1 >= gPx && e.x <= gPx + 8 && e.y + GH - 1 >= gPY && e.y <= gPY + 7) {
      e.alive = false;  // collision avec le vaisseau
      gBoomAt(e.x + 4, e.y + 3);
      gHitPlayer();
    }
  }
  // joueur aux commandes, sinon pilote automatique : esquive les tirs, vise un ennemi en pique ou la colonne la plus proche
  if (gDead == 0 && !gDemo) {
    gPlayerControl();
  } else if (gDead == 0) {
    // decisions gardees un moment (esquive 15 images, cible 25 images) : sinon le vaisseau hesite et tremble
    static int8_t dodgeDir = 0;
    static uint8_t dodgeT = 0, aimT = 0;
    static int aim = -1;
    float target = gPx;
    if (dodgeT) dodgeT--;
    else
      for (auto &b : gBomb)
        if (b.on && b.y > gPY - 24 && fabsf(b.x - shipC) < 6) { dodgeDir = b.x < shipC ? 1 : -1; dodgeT = 15; break; }
    if (dodgeT) {
      target = gPx + dodgeDir * 7;
    } else {
      if (aimT) aimT--;
      if (aim < 0 || aim >= gNE || !gEn[aim].alive || gEn[aim].y < 0 || !aimT) {
        float best = 1e9;
        aim = -1;
        for (int i = 0; i < gNE; i++) {
          GEnemy &e = gEn[i];
          if (!e.alive || e.y < 0) continue;
          float d = fabsf(e.x - gPx) - (e.state == 1 ? 40 : 0) - (e.type == 0 ? 8 : 0);  // priorite : piques, meduses
          if (d < best) { best = d; aim = i; }
        }
        aimT = 25;
      }
      if (aim >= 0) target = gEn[aim].x;
    }
    if (fabsf(target - gPx) >= 1) gPx += constrain(target - gPx, -1.5f, 1.5f);  // pas de micro-mouvement
    gPx = constrain(gPx, 0.0f, (float)(W - GW));
    if (gCool > 0) gCool--;
    if (gCool == 0)  // tir quand un ennemi est au-dessus
      for (int i = 0; i < gNE; i++)
        if (gEn[i].alive && gEn[i].y > 0 && fabsf(gEn[i].x - gPx) < 4) {
          gFire();
          break;
        }
  } else if (--gDead == 0 && gLives <= 0) {
    if (gDemo) gReset();  // demo : on recommence
    else if (gLastRank) startNameEntry();  // score au tableau : saisie du nom
    else toDemo();        // fin de partie du joueur : retour a la demo
  }
  for (auto &s : gShot) {  // tirs du vaisseau
    if (!s.on) continue;
    s.y -= 3;
    s.x += s.vx;
    if (s.y < 0 || s.x < 0 || s.x >= W) { s.on = false; continue; }
    for (int i = 0; i < gNE; i++) {
      GEnemy &e = gEn[i];
      if (e.alive && s.x >= e.x && s.x <= e.x + GW - 1 && s.y <= e.y + GH - 1 && s.y + 2 >= e.y) {
        s.on = false;
        if (--e.hp > 0) break;  // meduse : il faut 2 tirs
        e.alive = false;
        static const uint16_t PTS[3][2] = { { 150, 400 }, { 80, 160 }, { 50, 100 } };
        gScore += PTS[e.type][e.state == 1 || e.state == 3];  // abattu en vol : points doubles
        if (!gDemo && gScore > gHi) gHi = gScore;
        gBoomAt(e.x + 4, e.y + 3);
        sfxBoom();
        break;
      }
    }
  }
  for (auto &b : gBomb) {  // tirs ennemis
    if (!b.on) continue;
    b.y += 1.3f + 0.1f * gWave + (gEffDiff() - 1) * 0.3f;  // tirs plus lents en facile, plus rapides en difficile
    b.x += b.vx;
    if (b.y >= H || b.x < 0 || b.x >= W) { b.on = false; continue; }
    if (gDead == 0 && b.x >= gPx && b.x <= gPx + 8 && b.y + 2 >= gPY + 2 && b.y <= gPY + 7) { b.on = false; gHitPlayer(); }
  }
  for (auto &b : gBoom)
    if (b.t) b.t = b.t >= 12 ? 0 : b.t + 1;
  bool any = false;
  for (int i = 0; i < gNE; i++) any |= gEn[i].alive;
  if (!any) {  // stage termine : bonus, puis stage suivant
    gBonus = 500UL * gWave;
    gScore += gBonus;
    gLifeUp = gLives < 9;
    if (gLifeUp) gLives++;  // stage gagne : une vie en plus
    if (!gDemo && gScore > gHi) gHi = gScore;
    for (auto &b : gBomb) b.on = false;
    gNewWave();
  }
  if (gStageT > 0) gStageT--;
}
void gameDraw() {
  if (gOverflow) {  // des zones n'ont pas ete notees : on efface tout (plus de restes, ex. icones de vies)
    gOverflow = false;
    gNOld = 0;
    gfx->fillScreen(0);
  }
  for (int i = 0; i < gNOld; i++) {  // efface l'image precedente
    GRect &r = gOld[i];
    int x0 = max(0, (int)r.x), y0 = max(0, (int)r.y), x1 = min(W, r.x + r.w), y1 = min(H, r.y + r.h);
    if (x1 > x0 && y1 > y0) gfx->fillRect(x0, y0, x1 - x0, y1 - y0, 0);
  }
  gNOld = 0;
  float glow = 0.8f + 0.2f * sinf(gFrame * 0.08f);  // les ennemis pulsent doucement
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) {
    gPal[i] = rgb(G_PAL_RGB[i][0] * glow, G_PAL_RGB[i][1] * glow, G_PAL_RGB[i][2] * glow);
    palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  }
  for (int i = 0; i < gNStar; i++) {  // etoiles en noir et blanc qui scintillent (plus vives = plus rapides)
    GStar &s = gStar[i];
    if (((gFrame + i * 7) & 31) < 3) continue;
    uint8_t g = 70 + s.v * 120;
    gPix(s.x, (int)s.y, rgb(g, g, g));
  }
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    int fr = e.state == 1 || e.state == 3 ? (gFrame / 5) & 1 : ((gFrame / 16) + e.col) & 1;  // animation (ailes rapides en vol)
    bool hurt = e.hp < gBaseHp(e.type);  // touche mais pas encore detruit : change de couleur
    gSprite(SPR_W[gWorld][e.type][fr], GW, GH, (int)e.x, (int)e.y, gPal, WORLD_HURT[gWorld][0], hurt ? WORLD_HURT[gWorld][1] : WORLD_HURT[gWorld][0]);
  }
  for (auto &s : gShot)
    if (s.on) { gPix(s.x, s.y, palFull[0]); gPix(s.x, s.y + 1, palFull[2]); gPix(s.x, s.y + 2, palFull[7]); }
  for (auto &b : gBomb)
    if (b.on) { gPix(b.x, b.y, palFull[1]); gPix(b.x, b.y + 1, palFull[1]); gPix(b.x, b.y + 2, palFull[0]); }
  static const int8_t DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
  for (auto &b : gBoom) {  // explosions : anneau qui grandit, du blanc au rouge
    if (!b.t) continue;
    uint16_t c = b.t < 4 ? palFull[0] : b.t < 7 ? palFull[2] : b.t < 10 ? palFull[7] : palFull[1];
    for (int k = 0; k < 8; k++) gPix(b.x + DX[k] * b.t / 2, b.y + DY[k] * b.t / 2, c);
    if (b.t < 6) gPix(b.x, b.y, palFull[0]);
  }
  if (gDead == 0) gSprite(SPR_SHIP[gTier()], 9, 8, (int)gPx, gPY, palFull, 0, 0);
  // ecran d'arcade : SCORE a gauche, RECORD au centre
  char sc[12];
  uint16_t red = rgb(255, 40, 40), white = rgb(230, 230, 230);
  tinyText(*gfx, 1, 0, "SCORE", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gScore);
  tinyText(*gfx, 1, 6, sc, white);
  gMark(1, 6, tinyWidth(sc), 5);
  int rx = W >= 96 ? W / 2 - 11 : W - 24;
  tinyText(*gfx, rx, 0, "RECORD", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gHi);
  tinyText(*gfx, rx, 6, sc, white);
  gMark(rx, 6, tinyWidth(sc), 5);
  // vies en haut a droite : une petite icone par vie (6 au plus a l'ecran)
  for (int i = 0; i < gLives && i < 6; i++) gSprite(SPR_LIFE, 5, 4, W - 6 - i * 7, 1, palFull, 0, 0);
}

// joueur : deplacement aux touches, tir tant que FEU est appuye (3 tirs a l'ecran au plus)
void gPlayerControl() {
  uint8_t k = keysPs4;
  float sp = W > 128 ? 3 : 2;  // pas entier : deplacement regulier a l'ecran
  if ((k & 1) && !(k & 2)) gPx -= sp;
  if ((k & 2) && !(k & 1)) gPx += sp;
  gPx = constrain(gPx, 0.0f, (float)(W - GW));
  if ((k & 8) && !(k & 16)) gPY = max(gPYMin, gPY - 2);     // avancer
  if ((k & 16) && !(k & 8)) gPY = min(gPYBase, gPY + 2);    // reculer
  if (gCool > 0) gCool--;
  if ((k & 4) && gCool == 0) gFire();
}

// texte 6x8 centre, efface a l'image suivante
void gText(int y, const char *s, uint16_t c) {
  int x = (W - (int)strlen(s) * 6) / 2;
  gfx->setTextColor(c);
  gfx->setCursor(x, y);
  gfx->print(s);
  gMark(x, y, strlen(s) * 6, 8);
}
void gTextBg(int y, const char *s, uint16_t c) {  // texte sur un fond noir (lisible par-dessus la demo)
  int w = strlen(s) * 6 + 4, x = (W - w) / 2;
  gfx->fillRect(x, y - 2, w, 11, 0);
  gMark(x, y - 2, w, 11);
  gText(y, s, c);
}
void gBox(int y0, int y1) {  // cadre noir borde de bleu derriere le menu
  int w = min(W - 4, 120), x = (W - w) / 2;
  gfx->fillRect(x, y0, w, y1 - y0, 0);
  gfx->drawRect(x, y0, w, y1 - y0, rgb(40, 70, 170));
  gMark(x, y0, w, y1 - y0);
}
void drawMenu() {
  char b[24];
  uint16_t sel = rgb(255, 220, 0), off = rgb(0, 160, 200), dim = rgb(110, 110, 130);
  bool tall = H >= 128;
  if (menuScores) {  // page des meilleurs scores
    int y = tall ? H / 2 - 36 : 11, n = tall ? TOP_N : 4, step = tall ? 11 : 9;
    gBox(y - 5, tall ? y + (n + 1) * step + 18 : y + n * step + 13);
    snprintf(b, sizeof(b), "SCORES %s", GAME_NAME[gGame]);
    gText(y, b, sel);
    for (int i = 0; i < n; i++) {
      if (gBest[i].score)
        snprintf(b, sizeof(b), "%d %-3s %6lu %c S%u", i + 1, gBest[i].name, (unsigned long)gBest[i].score, DIFF_NAME[gBest[i].diff % 3][0],
                 gBest[i].stage);
      else snprintf(b, sizeof(b), "%d ---      -      ", i + 1);
      gText(y + (i + 1) * step + 2, b, i == 0 ? rgb(230, 230, 230) : off);
    }
    if (tall) gText(y + (n + 1) * step + 6, "X : RETOUR", dim);
    return;
  }
  int y = tall ? H / 2 - 22 : 12, step = tall ? 11 : 8;
  gBox(y - 6, tall ? y + MENU_N * step + 16 : y + (MENU_N - 1) * step + 12);
  const char *items[MENU_N] = { nullptr, nullptr, nullptr, nullptr, "SCORES", "DEMO" };
  for (int i = 0; i < MENU_N; i++) {
    const char *t = items[i];
    if (i == 0) { snprintf(b, sizeof(b), "JOUER %s%s%s", i == menuSel ? "<" : "", GAME_NAME[gGame], i == menuSel ? ">" : ""); t = b; }
    if (i == 1) { snprintf(b, sizeof(b), "NIVEAU %s%s%s", i == menuSel ? "<" : "", DIFF_NAME[gDiff], i == menuSel ? ">" : ""); t = b; }
    if (i == 2) { snprintf(b, sizeof(b), "LUMINOSITE %s%d%%%s", i == menuSel ? "<" : "", brightPct(), i == menuSel ? ">" : ""); t = b; }
    if (i == 3) { snprintf(b, sizeof(b), "VOLUME %s%d%%%s", i == menuSel ? "<" : "", volume * 10, i == menuSel ? ">" : ""); t = b; }
    char line[26];
    snprintf(line, sizeof(line), "%s%s", i == menuSel ? "> " : "", t);
    gText(y + i * step, line, i == menuSel ? sel : off);
  }
  if (tall) gText(y + MENU_N * step + 4, "OPTIONS : JOUER", dim);
}
void gOverlay() {
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  if (gLives <= 0 && gDead > 0 && !gDemo) {
    gText(H / 2 - 4, "GAME OVER", rgb(255, 40, 40));
    if (gLastRank == 1) gText(H / 2 + 8, "NOUVEAU RECORD !", rgb(255, 220, 0));
    else if (gLastRank) { char b[12]; snprintf(b, sizeof(b), "RANG %u", gLastRank); gText(H / 2 + 8, b, rgb(0, 220, 255)); }
  }
  if (gPaused) {  // pause : reprendre ou revenir au menu
    bool tall = H >= 128;
    int y = tall ? H / 2 - 14 : H / 2 - 12;
    gBox(y - 6, y + (tall ? 38 : 32));
    gText(y, "PAUSE", rgb(255, 220, 0));
    gText(y + (tall ? 13 : 11), tall ? "OPTIONS : REPRENDRE" : "OPTIONS: REPRENDRE", rgb(0, 160, 200));
    gText(y + (tall ? 24 : 21), "ROND : MENU", rgb(0, 160, 200));
  }
  if (gStageT > 0 && gLives > 0) {  // annonce du stage, bonus et nouvelle arme
    char b[20];
    int y = H >= 128 ? H / 2 - 22 : H / 2 - 10;
    snprintf(b, sizeof(b), "STAGE %d", gWave);
    if (gStageT > 12 || (gStageT & 2)) gText(y, b, rgb(255, 220, 0));
    if (gGame == 0) snprintf(b, sizeof(b), "MONDE %s", WORLD_NAME[gWorld]);
    else snprintf(b, sizeof(b), "%s", GAME_NAME[gGame]);
    gText(y + 11, b, rgb(255, 60, 200));
    int yy = y + 22;
    if (gBonus && H >= 128) { snprintf(b, sizeof(b), "BONUS %lu", (unsigned long)gBonus); gText(yy, b, rgb(230, 230, 230)); yy += 11; }
    if (gGame == 0 && gWave <= 4 && TIER_NAME[gTier()][0]) { gText(yy, TIER_NAME[gTier()], rgb(40, 230, 80)); yy += 11; }
    if (gLifeUp) gText(yy, "VIE +1", rgb(0, 220, 255));
  }
  if (!gDemo) return;
  if (PS4.isConnected() && !gAttract) return;  // manette la : le menu a son propre ecran (menuScreen)
  if (PS4.isConnected()) {  // mode demo : le menu revient au premier appui
    if ((gFrame / 40) & 1) gText(H / 2 + 6, "DEMO", rgb(255, 220, 0));
    else gText(H / 2 + 6, "APPUIE : MENU", rgb(0, 220, 255));
    return;
  }
  // pas de manette : demo, avec une petite indication pour la connecter (mini-police) et l'adresse Bluetooth
  if ((gFrame / 40) & 1) gText(H / 2 + 6, "DEMO", rgb(255, 220, 0));
  const char *hint = "MANETTE : BOUTON PS";
  int hy = H >= 128 ? H / 2 + 20 : H - 14;
  uint16_t grey = rgb(100, 100, 120);
  tinyText(*gfx, (W - tinyWidth(hint)) / 2, hy, hint, grey);
  gMark((W - tinyWidth(hint)) / 2, hy, tinyWidth(hint), 5);
  if (ipText[0] && H >= 128) {
    char mac[20];
    snprintf(mac, sizeof(mac), "%s", ipText);
    for (char *c = mac; *c; c++) *c = toupper(*c);
    tinyText(*gfx, (W - tinyWidth(mac)) / 2, hy + 8, mac, grey);
    gMark((W - tinyWidth(mac)) / 2, hy + 8, tinyWidth(mac), 5);
  }
}

// ================================================================ autres jeux : RAID et COURSE
// Memes regles communes que ESSAIM : gScore, gLives, gWave (stage), gStageT (annonce), gBonus, gLifeUp,
// gDead (pause apres une vie perdue / fin de partie), scores par jeu, saisie du nom, pause, vibration, son.

// efface les zones dessinees a l'image precedente (comme au debut de gameDraw)
void eraseOld() {
  if (gOverflow) { gOverflow = false; gNOld = 0; gfx->fillScreen(0); }
  for (int i = 0; i < gNOld; i++) {
    GRect &rc = gOld[i];
    int x0 = max(0, (int)rc.x), y0 = max(0, (int)rc.y), x1 = min(W, rc.x + rc.w), y1 = min(H, rc.y + rc.h);
    if (x1 > x0 && y1 > y0) gfx->fillRect(x0, y0, x1 - x0, y1 - y0, 0);
  }
  gNOld = 0;
}
// etoiles, score, record, vies (commun aux autres jeux) ; vitesse des etoiles en parametre
void altBackground(float speed, uint16_t *palFull) {
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  for (int i = 0; i < gNStar; i++) {
    GStar &s = gStar[i];
    if (!gPaused) {
      s.y += s.v * speed;
      if (s.y >= H) { s.y = 12; s.x = random(W); }
    }
    if (((gFrame + i * 7) & 31) < 3) continue;
    uint8_t g = 70 + s.v * 120;
    gPix(s.x, (int)s.y, rgb(g, g, g));
  }
}
void altHud(uint16_t *palFull) {
  char sc[12];
  uint16_t red = rgb(255, 40, 40), white = rgb(230, 230, 230);
  tinyText(*gfx, 1, 0, "SCORE", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gScore);
  tinyText(*gfx, 1, 6, sc, white);
  gMark(1, 0, 24, 11);
  int rx = W >= 96 ? W / 2 - 11 : W - 24;
  tinyText(*gfx, rx, 0, "RECORD", red);
  snprintf(sc, sizeof(sc), "%lu", (unsigned long)gHi);
  tinyText(*gfx, rx, 6, sc, white);
  gMark(rx, 0, 26, 11);
  snprintf(sc, sizeof(sc), "X%d", max(0, gLives));
  int lw = tinyWidth(sc), lx = W - 1 - lw;
  tinyText(*gfx, lx, 1, sc, white);
  gMark(lx, 1, lw, 5);
  gSprite(SPR_LIFE, 5, 4, lx - 7, 1, palFull, 0, 0);
}
// une vie perdue (commun) : vibration, son, explosion, fin de partie eventuelle
void altLoseLife(int x, int y) {
  if (!gDemo) rumbleReq = true;  // vibration seulement pendant une partie
  sfxHit();
  gBoomAt(x, y);
  if (gLives == 1) sndMelody(JINGLE_OVER, 4);
  gLives--;
  gDead = gLives > 0 ? 60 : 120;
  if (gLives <= 0 && !gDemo) topAdd();  // fin de partie : meilleurs scores (pas en demo)
}
// stage gagne (commun) : bonus, une vie en plus, annonce
void altStageClear(uint32_t bonus) {
  gBonus = bonus;
  gScore += gBonus;
  if (!gDemo && gScore > gHi) gHi = gScore;
  gLifeUp = gLives < 9;
  if (gLifeUp) gLives++;
}
void altBooms(uint16_t *palFull) {  // explosions (anneau qui grandit)
  static const int8_t DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
  for (auto &b : gBoom) {
    if (!b.t) continue;
    if (!gPaused) b.t = b.t >= 12 ? 0 : b.t + 1;
    if (!b.t) continue;
    uint16_t c = b.t < 4 ? palFull[0] : b.t < 7 ? palFull[2] : b.t < 10 ? palFull[7] : palFull[1];
    for (int k = 0; k < 8; k++) gPix(b.x + DX[k] * b.t / 2, b.y + DY[k] * b.t / 2, c);
  }
}

// ---------------------------------------------------------------- RAID
// Shoot vertical : le vaisseau se deplace partout dans le bas de l'ecran, des vagues d'ennemis descendent
// (lignes, diagonales, eventails, kamikazes, tourelles). Capsules : P arme, S bouclier, B bombe (rond).
// Un boss different selon le stage attend apres 8 vagues ; sous la moitie de sa vie il passe en 2e phase.
#define R_EN 28
#define R_SHOTS 16
#define R_BOMBS 36
#define R_GROUPS 8  // vagues d'ennemis avant le boss
#define R_FAR 24    // etoiles lointaines (defilement lent)
struct REnemy { float x, y, vx, vy, t; uint8_t type, pat; int8_t hp, flash; bool on, carrier; };
struct RDot { float x, y, vx, vy; bool on; };
struct RCap { float x, y; uint8_t kind; bool on; };  // kind 0 : arme (P), 1 : bouclier (S), 2 : bombe (B)
REnemy rEn[R_EN];
RDot rShot[R_SHOTS], rBomb[R_BOMBS];
RCap rCap;
int16_t rFar[R_FAR][2];
float rX, rY;
int rTier = 0, rCool = 0, rGroups = 0, rSpawnT = 0, rInv = 0, rShield = 0, rBombs = 0, rFlash = 0;
bool rBoss = false, rPrevBomb = false;
float rBX, rBY;
int rBHp, rBMax, rBT = 0, rBFlash = 0;

void rNewStage() {
  gWave++;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  rGroups = 0; rSpawnT = 40; rBoss = false;
}
void rReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 0; gDead = 0; gBonus = 0; gLifeUp = false;
  rTier = 0; rCool = 0; rInv = 0; rShield = 0; rBombs = 1; rFlash = 0;
  rX = W / 2 - 4; rY = H - 12;
  for (auto &e : rEn) e.on = false;
  for (auto &s : rShot) s.on = false;
  for (auto &b : rBomb) b.on = false;
  rCap.on = false;
  for (auto &b : gBoom) b.t = 0;
  for (auto &f : rFar) { f[0] = random(W); f[1] = random(12, H); }
  rNewStage();
}
void rBombAt(float x, float y, float vx, float vy) {
  for (auto &b : rBomb)
    if (!b.on) { b = { x, y, vx, vy, true }; return; }
}
void rAimed(float x, float y, float speed) {  // tir vise sur le vaisseau
  float dx = rX + 4 - x, dy = rY + 4 - y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
  rBombAt(x, y, dx / d * speed, dy / d * speed);
}
void rSpread(float x, float y, int n, float speed) {  // eventail vers le bas
  for (int i = 0; i < n; i++) {
    float a = PI / 2 + (i - (n - 1) / 2.0f) * 0.32f;
    rBombAt(x, y, cosf(a) * speed, sinf(a) * speed);
  }
}
// une vague : motif selon le numero de vague (ligne, diagonale, eventail, kamikazes, tourelles)
void rSpawnGroup() {
  uint8_t pat = rGroups % 5, type = rGroups % 3;
  int n = pat == 4 ? 2 : 5;
  for (int k = 0; k < n; k++)
    for (auto &e : rEn)
      if (!e.on) {
        e = {};
        e.on = true; e.pat = pat; e.type = type; e.t = -k * 12;
        e.hp = (type == 0 ? 2 : 1) + (gWave >= 3 ? 1 : 0) + (pat == 4 ? 4 : 0);
        e.carrier = k == n - 1 && (rGroups % 2 == 0);  // la derniere de certaines vagues porte une capsule
        if (pat == 0) { e.x = W * (k + 1) / (n + 1) - 4; e.y = -10; }
        else if (pat == 1) { e.x = (rGroups & 2) ? -10 : W + 2; e.y = 16; e.vx = (rGroups & 2) ? 1.1f : -1.1f; }
        else if (pat == 2) { e.x = W / 2 - 4; e.y = -10; e.vx = (k & 1) ? 0.9f : -0.9f; }
        else if (pat == 3) { e.x = random(8, W - 16); e.y = -10; }               // kamikaze
        else { e.x = W * (k + 1) / (n + 1) - 4; e.y = -10; }                     // tourelle
        break;
      }
  rGroups++;
}
void rHit() {  // vaisseau touche : le bouclier encaisse, sinon une vie (et l'arme baisse d'un niveau)
  if (rShield) { rShield = 0; rInv = 40; sfxBoom(); return; }
  altLoseLife(rX + 4, rY + 3);
  rTier = max(0, rTier - 1);
}
void rSmartBomb() {  // bombe : detruit les ennemis a l'ecran, efface les tirs, blesse le boss
  rBombs--;
  rFlash = 4;
  sfxHit();
  for (auto &e : rEn)
    if (e.on && e.t >= 0 && e.y > 0) { e.on = false; gScore += 100; gBoomAt(e.x + 4, e.y + 3); }
  for (auto &b : rBomb) b.on = false;
  if (rBoss) rBHp -= rBMax / 6;
  if (!gDemo && gScore > gHi) gHi = gScore;
}
// pilote automatique (demo) : suit l'ennemi le plus proche ou le boss, esquive les tirs, tire sans arret
uint8_t rAuto() {
  float tx = rX, best = 1e9;
  for (auto &e : rEn)
    if (e.on && e.t >= 0 && e.y > 10) {
      float d = fabsf(e.x - rX) + (H - e.y) * 0.2f;
      if (d < best) { best = d; tx = e.x; }
    }
  if (rBoss) tx = rBX + 12;
  if (rCap.on) tx = rCap.x;
  for (auto &b : rBomb)
    if (b.on && b.y > rY - 26 && b.y < rY + 4 && fabsf(b.x - (rX + 4)) < 7) tx = b.x < rX + 4 ? rX + 14 : rX - 14;
  uint8_t k = 4;
  if (tx < rX - 1) k |= 1;
  else if (tx > rX + 1) k |= 2;
  if (rY < H - 14) k |= 16;
  return k;
}
void rStep() {
  gFrame++;
  uint8_t k = gDemo ? rAuto() : keysPs4;
  float diff = 1 + 0.25f * (gEffDiff() - 1);
  if (rFlash > 0) rFlash--;
  if (gDead == 0) {
    float sp = W > 128 ? 2.5f : 2;
    if (k & 1) rX -= sp;
    if (k & 2) rX += sp;
    if (k & 8) rY -= sp;
    if (k & 16) rY += sp;
    rX = constrain(rX, 0.0f, (float)(W - 9));
    rY = constrain(rY, (float)(H * 4 / 10), (float)(H - 9));
    if (rCool > 0) rCool--;
    if ((k & 4) && rCool == 0) {  // tir selon l'arme (capsules P)
      static const int8_t N[4] = { 1, 2, 3, 5 }, COOL[4] = { 7, 6, 6, 5 };
      int n = N[rTier];
      for (int i = 0; i < n; i++)
        for (auto &s : rShot)
          if (!s.on) {
            float vx = n >= 3 ? (i - (n - 1) / 2.0f) * 0.55f : 0, x = rX + 4 + (n == 2 ? (i ? 2 : -2) : 0);
            s = { x, rY - 2, vx, -3.6f, true };
            break;
          }
      rCool = COOL[rTier];
      sfxShot();
    }
    bool bombKey = k & 32;  // rond : bombe
    if (bombKey && !rPrevBomb && rBombs > 0) rSmartBomb();
    rPrevBomb = bombKey;
    if (rInv > 0) rInv--;
  } else if (--gDead == 0) {
    if (gLives <= 0) {
      if (gDemo) rReset();  // demo : on recommence
      else if (gLastRank) startNameEntry();
      else toDemo();
      return;
    }
    rX = W / 2 - 4; rY = H - 12; rInv = 90;
  }
  // vagues puis boss
  if (gStageT == 0 && !rBoss) {
    bool any = false;
    for (auto &e : rEn) any |= e.on;
    if (rGroups < R_GROUPS) {
      if (--rSpawnT <= 0) { rSpawnGroup(); rSpawnT = 140; }
    } else if (!any) {  // le boss arrive
      rBoss = true; rBX = W / 2 - 16; rBY = -18; rBT = 0;
      rBMax = rBHp = (40 + 25 * gWave) * (gEffDiff() + 1) / 2;
      sfxDive();
    }
  }
  for (auto &f : rFar)  // etoiles lointaines
    if (++f[1] >= H && (gFrame & 1)) { f[1] = 12; f[0] = random(W); }
  for (auto &s : rShot) {
    if (!s.on) continue;
    s.x += s.vx; s.y += s.vy;
    if (s.y < 12 || s.x < 0 || s.x >= W) s.on = false;
  }
  // ennemis
  for (auto &e : rEn) {
    if (!e.on) continue;
    e.t++;
    if (e.flash > 0) e.flash--;
    if (e.t < 0) continue;
    if (e.pat == 0) { e.y += 0.7f * diff; e.x += sinf(e.t * 0.05f) * 0.9f; }
    else if (e.pat == 1) { e.x += e.vx * diff; e.y += 0.45f * diff; }
    else if (e.pat == 2) { e.x += e.vx * diff; e.y += 0.8f * diff; if (e.x < 2 || e.x > W - 11) e.vx = -e.vx; }
    else if (e.pat == 3) {  // kamikaze : descend un peu, puis fonce sur le vaisseau
      if (e.t < 50) e.y += 0.6f;
      else {
        if (e.t == 50) { float dx = rX - e.x, dy = rY - e.y, d = sqrtf(dx * dx + dy * dy) + .01f; e.vx = dx / d * 2.2f * diff; e.vy = dy / d * 2.2f * diff; }
        e.x += e.vx; e.y += e.vy;
      }
    } else {  // tourelle : s'arrete en haut et tire en eventail, puis repart
      if (e.t < 40) e.y += 0.8f;
      else if (e.t > 260) e.y += 0.9f;
      else if ((int)e.t % max(40, int(70 / diff)) == 0) rSpread(e.x + 4, e.y + 7, 3, 1.1f * diff);
    }
    if (e.y > H || e.x < -14 || e.x > W + 14) { e.on = false; continue; }
    if (e.pat < 3 && e.y > 12 && e.y < H * 0.6f && random(1000) < 6 * diff) rAimed(e.x + 4, e.y + 7, 1.2f * diff);
    for (auto &s : rShot)
      if (s.on && s.x >= e.x && s.x <= e.x + 8 && s.y >= e.y && s.y <= e.y + 7) {
        s.on = false;
        e.flash = 3;
        if (--e.hp > 0) break;
        e.on = false;
        gScore += e.pat == 4 ? 300 : e.pat == 3 ? 150 : 100;
        if (!gDemo && gScore > gHi) gHi = gScore;
        gBoomAt(e.x + 4, e.y + 3);
        sfxBoom();
        if (e.carrier && !rCap.on) {  // capsule : P (arme) 50 %, S (bouclier) 25 %, B (bombe) 25 %
          int r = random(4);
          rCap = { e.x + 1, e.y, (uint8_t)(r < 2 ? 0 : r == 2 ? 1 : 2), true };
        }
        break;
      }
    if (e.on && gDead == 0 && rInv == 0 && e.x + 8 >= rX && e.x <= rX + 8 && e.y + 6 >= rY && e.y <= rY + 7) {
      e.on = false;
      gBoomAt(e.x + 4, e.y + 3);
      rHit();
    }
  }
  // capsule
  if (rCap.on) {
    rCap.y += 0.6f;
    if (rCap.y > H) rCap.on = false;
    else if (gDead == 0 && rCap.x + 6 >= rX && rCap.x <= rX + 8 && rCap.y + 6 >= rY && rCap.y <= rY + 7) {
      rCap.on = false;
      if (rCap.kind == 0) rTier = min(3, rTier + 1);
      else if (rCap.kind == 1) rShield = 1;
      else rBombs = min(3, rBombs + 1);
      gScore += 500;
      sndMelody(JINGLE_ARME, 4);
    }
  }
  // boss : 3 formes selon le stage ; 2e phase sous la moitie de sa vie (plus vite, tirs en spirale)
  if (rBoss) {
    rBT++;
    if (rBFlash > 0) rBFlash--;
    bool phase2 = rBHp < rBMax / 2;
    float swing = phase2 ? 0.035f : 0.02f;
    if (rBY < 16) rBY += 0.5f;
    else rBX = W / 2 - 16 + sinf(rBT * swing) * (W / 2 - 20);
    if (rBY >= 16) {
      if (rBT % max(18, int((phase2 ? 32 : 45) / diff)) == 0) rSpread(rBX + 16, rBY + 14, phase2 ? 7 : 5, 1.2f * diff);
      if (rBT % 33 == 0) rAimed(rBX + 16, rBY + 14, 1.6f * diff);
      if (phase2 && rBT % 6 == 0) {
        float a = rBT * 0.31f;
        rBombAt(rBX + 16, rBY + 8, cosf(a) * 1.1f * diff, fabsf(sinf(a)) * 1.1f * diff + 0.3f);
      }
    }
    for (auto &s : rShot)
      if (s.on && s.x >= rBX && s.x <= rBX + 31 && s.y >= rBY && s.y <= rBY + 14) {
        s.on = false;
        rBHp--;
        rBFlash = 2;
        if (gFrame & 1) gBoomAt(s.x, s.y);
        break;
      }
    if (rBHp <= 0) {  // boss detruit : stage gagne
      rBoss = false;
      for (int i = 0; i < 8; i++) gBoomAt(rBX + random(32), rBY + random(15));
      sfxHit();
      gScore += 2000UL * gWave;
      altStageClear(1000UL * gWave);
      for (auto &b : rBomb) b.on = false;
      rNewStage();
    }
    if (rBoss && gDead == 0 && rInv == 0 && rBX + 31 >= rX && rBX <= rX + 8 && rBY + 14 >= rY && rBY <= rY + 7) rHit();
  }
  for (auto &b : rBomb) {
    if (!b.on) continue;
    b.x += b.vx; b.y += b.vy;
    if (b.y >= H || b.y < 10 || b.x < 0 || b.x >= W) { b.on = false; continue; }
    if (gDead == 0 && rInv == 0 && b.x >= rX + 1 && b.x <= rX + 7 && b.y >= rY + 1 && b.y <= rY + 7) {
      b.on = false;
      rHit();
    }
  }
  if (gStageT > 0) gStageT--;
}
void rDrawBoss(uint16_t *palFull) {  // 3 boss : coque a canons, crabe a pinces, noyau a bras qui tournent
  static const uint8_t BC[4][3] = { { 200, 40, 160 }, { 220, 90, 20 }, { 110, 60, 230 }, { 30, 170, 90 } };
  const uint8_t *c = BC[(gWave - 1) % 4];
  uint16_t body = rBFlash ? palFull[0] : rgb(c[0], c[1], c[2]), dark = rgb(c[0] / 3, c[1] / 3, c[2] / 3);
  int x = rBX, y = rBY, kind = (gWave - 1) % 3;
  if (kind == 0) {
    gfx->fillRect(x + 4, y + 2, 24, 8, body);
    gfx->fillRect(x, y + 5, 32, 4, body);
    gfx->fillRect(x + 8, y, 16, 3, dark);
    gfx->fillRect(x + 2, y + 9, 4, 5, dark);
    gfx->fillRect(x + 26, y + 9, 4, 5, dark);
    gfx->fillRect(x + 14, y + 10, 4, 5, dark);
  } else if (kind == 1) {
    gfx->fillRect(x + 8, y + 3, 16, 9, body);  // carapace
    gfx->fillRect(x + 2, y + 1, 5, 4, body);   // pinces qui s'ouvrent
    gfx->fillRect(x + 25, y + 1, 5, 4, body);
    int o = (gFrame / 8) & 1;
    gfx->fillRect(x + 1, y + 5 + o, 3, 3, dark);
    gfx->fillRect(x + 28, y + 5 + o, 3, 3, dark);
    for (int l = 0; l < 3; l++) { gfx->drawFastVLine(x + 10 + l * 6, y + 12, 3, dark); }
  } else {
    gfx->fillCircle(x + 16, y + 7, 7, body);  // noyau
    gfx->fillCircle(x + 16, y + 7, 3, dark);
    for (int a = 0; a < 4; a++) {  // bras qui tournent
      float ang = rBT * 0.05f + a * PI / 2;
      gfx->drawLine(x + 16, y + 7, x + 16 + cosf(ang) * 15, y + 7 + sinf(ang) * 7, body);
    }
  }
  if (kind != 2) {
    gfx->fillRect(x + 9, y + 4, 4, 3, palFull[2]);  // yeux
    gfx->fillRect(x + 19, y + 4, 4, 3, palFull[2]);
  }
  gMark(x - 1, y - 1, 34, 17);
  int bw = W - 20, fill = bw * max(0, rBHp) / rBMax;  // barre de vie
  gfx->drawRect(10, 12, bw, 3, rgb(90, 90, 110));
  gfx->fillRect(10, 13, fill, 1, rBHp < rBMax / 2 ? palFull[7] : palFull[1]);
  gMark(10, 12, bw, 3);
}
void rDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(1.4f, palFull);  // defilement rapide : on avance
  for (auto &f : rFar) gPix(f[0], f[1], rgb(45, 45, 60));  // etoiles lointaines (plus lentes)
  if (rFlash > 0) {  // bombe : eclair
    gfx->drawRect(0, 11, W, H - 11, palFull[0]);
    gfx->drawRect(2, 13, W - 4, H - 15, palFull[2]);
    gMark(0, 11, W, H - 11);
  }
  uint16_t white[9];
  for (auto &c : white) c = palFull[0];
  int w = (gWave - 1) % 4;
  for (auto &e : rEn)
    if (e.on && e.t >= 0) gSprite(SPR_W[w][e.type][(gFrame / 6) & 1], GW, GH, (int)e.x, (int)e.y, e.flash ? white : palFull, 0, 0);
  if (rBoss) rDrawBoss(palFull);
  if (rCap.on) {  // capsule clignotante avec sa lettre (P, S ou B)
    static const char CAP[3] = { 'P', 'S', 'B' };
    uint16_t c = (gFrame & 4) ? palFull[2] : rCap.kind == 1 ? palFull[4] : rCap.kind == 2 ? palFull[1] : palFull[5];
    gfx->drawRect((int)rCap.x - 1, (int)rCap.y - 1, 9, 10, c);
    gfx->setFont(NULL);
    gfx->setTextSize(1);
    gfx->setTextColor(c);
    gfx->setCursor((int)rCap.x + 1, (int)rCap.y);
    gfx->print(CAP[rCap.kind]);
    gMark((int)rCap.x - 1, (int)rCap.y - 1, 9, 10);
  }
  for (auto &s : rShot)
    if (s.on) { gPix(s.x, s.y, palFull[0]); gPix(s.x, s.y + 1, palFull[2]); }
  for (auto &b : rBomb)
    if (b.on) { gPix(b.x, b.y, palFull[1]); gPix(b.x + 1, b.y, palFull[1]); gPix(b.x, b.y + 1, palFull[7]); }
  altBooms(palFull);
  if (gDead == 0 && !(rInv > 0 && (gFrame & 4))) {
    gSprite(SPR_SHIP[min(rTier, 3)], 9, 8, (int)rX, (int)rY, palFull, 0, 0);
    if (rShield) { gfx->drawCircle((int)rX + 4, (int)rY + 4, 7, (gFrame & 2) ? palFull[4] : palFull[3]); gMark((int)rX - 3, (int)rY - 3, 15, 15); }
  }
  for (int i = 0; i < rBombs; i++) {  // bombes restantes en bas a gauche
    gfx->fillCircle(3 + i * 7, H - 3, 2, palFull[1]);
    gMark(1 + i * 7, H - 5, 5, 5);
  }
  altHud(palFull);
}

// ---------------------------------------------------------------- COURSE
// Un petit bonhomme court tout seul vers la droite : sauter (X ou haut, plus haut si on garde appuye).
// Trous, piques, rochers qui roulent (sauter par-dessus ou les ecraser en retombant), rangees de blocs
// (on monte dessus ; un coup de tete sous un bloc bonus fait jaillir une piece), pieces (30 = une vie),
// cristaux. Montagnes au loin qui defilent lentement.
#define C_PLAT 16
#define C_ITEMS 28
#define C_BLOCKS 24
#define C_ROCKS 4
struct CPlat { float x0, x1; int16_t y; bool on; };
struct CItem { float x; int16_t y; uint8_t kind; bool on; };  // kind 0 : piques, 1 : cristal, 2 : piece
struct CBlock { float x; int16_t y; uint8_t kind; bool on; };  // kind 0 : pierre, 1 : bonus, 2 : bonus vide
struct CRock { float x, y, vy; bool on; };
struct CPop { float x, y, vy; int t; };  // piece qui jaillit d'un bloc bonus
CPlat cPlat[C_PLAT];
CItem cItem[C_ITEMS];
CBlock cBlock[C_BLOCKS];
CRock cRock[C_ROCKS];
CPop cPop = { 0, 0, 0, 0 };
float cScroll = 0, cSpeed = 1.2f, cY = 0, cVY = 0, cNextX = 0, cStageAt = 0, cRockAt = 0;
int16_t cLastY = 0;
bool cGround = false;
int cInv = 0, cCoins = 0;
const int C_PX = 22;  // position du bonhomme a l'ecran (c'est le decor qui defile)
const char *const SPR_RUN[3][10] = {  // bonhomme original 8x10 : casquette rouge, t-shirt bleu, pantalon cyan
  { "..rrr...", ".rrrrr..", "..wwy...", "..www...", ".bbbbb..", "w.bbb.w.", "..ccc...", ".c...c..", ".c....c.", "oo....oo" },
  { "..rrr...", ".rrrrr..", "..wwy...", "..www...", "..bbb...", ".wbbbw..", "..ccc...", "..c.c...", "..c.c...", ".oo.oo.." },
  { "..rrr...", ".rrrrr..", "..wwy...", "w.www.w.", ".bbbbb..", "..bbb...", "..ccc...", ".c...c..", "oo...oo.", "........" }
};
int cGroundY() { return H - 6; }
void cAddPlat(float x0, float x1, int16_t y) {
  for (auto &p : cPlat)
    if (!p.on) { p = { x0, x1, y, true }; return; }
}
void cAddItem(float x, int16_t y, uint8_t kind) {
  for (auto &it : cItem)
    if (!it.on) { it = { x, y, kind, true }; return; }
}
void cAddBlock(float x, int16_t y, uint8_t kind) {
  for (auto &b : cBlock)
    if (!b.on) { b = { x, y, kind, true }; return; }
}
// construit le parcours en avance : sol, trous, plateformes, blocs, piques, pieces, cristaux
void cGenerate() {
  while (cNextX < cScroll + W + 60) {
    int gap = random(100) < 45 ? random(14, 22 + (int)(cSpeed * 4)) : 0;
    float x0 = cNextX + gap;
    bool high = random(100) < 35;
    int16_t y = cGroundY();
    if (high) y = constrain(cLastY - (int16_t)random(8, 18), (int16_t)(H * 45 / 100), (int16_t)cGroundY());
    else if (cLastY < cGroundY() && random(2)) y = min((int)cGroundY(), cLastY + (int)random(6, 14));
    float len = high ? random(24, 42) : random(40, 86);
    cAddPlat(x0, x0 + len, y);
    if (gap > 0)  // arc de pieces au-dessus du trou
      for (int c = 0; c < 4; c++) cAddItem(x0 - gap + gap * (c + 0.5f) / 4, y - 14 - (c == 1 || c == 2 ? 6 : 0), 2);
    int r = random(100);
    if (!high && len > 50 && r < 40) {  // rangee de blocs (avec des blocs bonus) et pieces dessus
      int n = random(3, 6);
      float bx = x0 + random(10, (int)len - n * 6 - 4);
      int16_t by = y - 26;
      for (int b = 0; b < n; b++) cAddBlock(bx + b * 6, by, random(100) < 35 ? 1 : 0);
      for (int b = 0; b < n; b += 2) cAddItem(bx + b * 6 + 1, by - 6, 2);
    } else if (len > 40 && r < 70) {
      cAddItem(x0 + random(14, (int)len - 10), y, 0);  // piques
    } else {
      for (int c = 0; c < 3; c++) cAddItem(x0 + len / 2 - 8 + c * 8, y - 8, 2);  // rangee de pieces
    }
    if (random(100) < 15) cAddItem(x0 + len / 2, y - 22, 1);  // cristal (rare)
    cLastY = y;
    cNextX = x0 + len;
  }
}
bool cPlatAt(float wx, int16_t &py) {  // sol ou plateforme sous la position wx (la plus haute)
  bool found = false;
  for (auto &p : cPlat)
    if (p.on && p.x0 <= wx && p.x1 >= wx && (!found || p.y < py)) { py = p.y; found = true; }
  return found;
}
void cRespawn() {
  float wx = cScroll + C_PX;
  int16_t py = cGroundY();
  if (!cPlatAt(wx + 3, py)) cAddPlat(wx - 10, wx + 40, cGroundY());
  cY = py - 10;
  cVY = 0;
  cInv = 90;
  for (auto &it : cItem)
    if (it.on && it.kind == 0 && it.x > wx - 10 && it.x < wx + 40) it.on = false;
  for (auto &rk : cRock) rk.on = false;
}
void cReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &p : cPlat) p.on = false;
  for (auto &it : cItem) it.on = false;
  for (auto &b : cBlock) b.on = false;
  for (auto &rk : cRock) rk.on = false;
  for (auto &b : gBoom) b.t = 0;
  cPop.t = 0;
  cScroll = 0; cCoins = 0;
  cSpeed = 1.1f + 0.25f * (gEffDiff() - 1);
  cLastY = cGroundY();
  cAddPlat(-10, W + 20, cGroundY());  // depart sur un long sol
  cNextX = W + 20;
  cStageAt = 1500;
  cRockAt = 400;
  cY = cGroundY() - 10; cVY = 0; cInv = 0;
  cGenerate();
}
void cCoin(int pts) {  // piece ramassee : 30 pieces = une vie
  gScore += pts;
  sfxMenu();
  if (++cCoins >= 30) {
    cCoins = 0;
    if (gLives < 9) { gLives++; sndMelody(JINGLE_ARME, 4); }
  }
}
// pilote automatique (demo) : saute devant un trou, des piques, un rocher ou une marche
uint8_t cAuto() {
  float wx = cScroll + C_PX;
  if (!cGround) return cVY < 0 ? 4 : 0;
  float ahead = 10 + cSpeed * 14;
  for (auto &it : cItem)
    if (it.on && it.kind == 0 && it.x > wx && it.x < wx + ahead) return 4;
  for (auto &rk : cRock)
    if (rk.on && rk.x > wx && rk.x < wx + ahead + 10) return 4;
  int16_t py;
  if (!cPlatAt(wx + 8 + cSpeed * 6, py) || py > cY + 11) return 4;  // trou devant
  for (auto &p : cPlat)
    if (p.on && p.x0 > wx && p.x0 < wx + ahead && p.y < cY + 8) return 4;  // marche plus haute
  return 0;
}
void cStep() {
  gFrame++;
  uint8_t k = gDemo ? cAuto() : keysPs4;
  bool jumpKey = k & (4 | 8);
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) cReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      cRespawn();
    }
    return;
  }
  cScroll += cSpeed;
  if ((gFrame & 3) == 0) gScore += 1 + (int)cSpeed;  // la distance rapporte des points
  static bool prevJump = false;
  if (jumpKey && !prevJump && cGround) { cVY = -4.1f; cGround = false; sfxShot(); }
  prevJump = jumpKey;
  cVY += (jumpKey && cVY < 0) ? 0.22f : 0.36f;  // bouton garde : saut plus haut
  cVY = min(cVY, 4.5f);
  float oldFeet = cY + 10, oldHead = cY;
  cY += cVY;
  float feet = cY + 10, wx = cScroll + C_PX;
  cGround = false;
  for (auto &p : cPlat) {  // atterrir sur une plateforme
    if (!p.on) continue;
    if (p.x1 < cScroll - 20) { p.on = false; continue; }
    if (wx + 6 >= p.x0 && wx + 1 <= p.x1 && cVY >= 0 && oldFeet <= p.y + 1 && feet >= p.y) {
      cY = p.y - 10; cVY = 0; cGround = true;
    }
  }
  for (auto &b : cBlock) {  // blocs : on se pose dessus, coup de tete par-dessous
    if (!b.on) continue;
    if (b.x < cScroll - 20) { b.on = false; continue; }
    if (wx + 6 < b.x || wx + 1 > b.x + 6) continue;
    if (cVY >= 0 && oldFeet <= b.y + 1 && cY + 10 >= b.y) { cY = b.y - 10; cVY = 0; cGround = true; }
    else if (cVY < 0 && oldHead >= b.y + 6 && cY <= b.y + 6) {
      cY = b.y + 6; cVY = 0.5f;
      if (b.kind == 1) {  // bloc bonus : une piece jaillit
        b.kind = 2;
        cPop = { b.x + 1, (float)b.y - 6, -2.2f, 18 };
        cCoin(100);
      }
    }
  }
  if (cPop.t > 0) { cPop.t--; cPop.y += cPop.vy; cPop.vy += 0.25f; }
  if (cInv > 0) cInv--;
  for (auto &it : cItem) {
    if (!it.on) continue;
    if (it.x < cScroll - 20) { it.on = false; continue; }
    float sx = it.x - cScroll;
    if (it.kind >= 1) {  // cristal ou piece
      if (sx >= C_PX - 4 && sx <= C_PX + 8 && it.y >= cY - 4 && it.y <= cY + 11) {
        it.on = false;
        if (it.kind == 1) { gScore += 200; sfxMenu(); }
        else cCoin(10);
      }
    } else if (cInv == 0 && sx >= C_PX - 3 && sx <= C_PX + 6 && cY + 10 > it.y - 5 && cY < it.y) {  // piques
      altLoseLife(C_PX + 4, (int)cY + 5);
      return;
    }
  }
  // rochers : arrivent de la droite en roulant, tombent dans les trous ; on les ecrase en retombant dessus
  if (cScroll > cRockAt && gStageT == 0) {
    cRockAt = cScroll + random(260, 520) / (0.8f + 0.2f * gWave);
    for (auto &rk : cRock)
      if (!rk.on) { rk = { cScroll + W + 8, (float)cGroundY() - 40, 0, true }; break; }
  }
  for (auto &rk : cRock) {
    if (!rk.on) continue;
    rk.x -= 0.9f;  // il roule vers le bonhomme (le decor defile en plus)
    rk.vy = min(rk.vy + 0.3f, 4.0f);
    rk.y += rk.vy;
    int16_t py;
    if (cPlatAt(rk.x + 3, py) && rk.y + 7 >= py && rk.y + 7 - rk.vy <= py + 2) { rk.y = py - 7; rk.vy = 0; }
    if (rk.y > H || rk.x < cScroll - 20) { rk.on = false; continue; }
    float sx = rk.x - cScroll;
    if (sx + 7 >= C_PX + 1 && sx <= C_PX + 6 && rk.y + 7 >= cY && rk.y <= cY + 10) {
      if (cVY > 0 && cY + 10 <= rk.y + 4) {  // ecrase par dessus : rebond
        rk.on = false;
        cVY = -3.0f;
        gScore += 200;
        gBoomAt(sx + 3, rk.y + 3);
        sfxBoom();
      } else if (cInv == 0) {
        rk.on = false;
        altLoseLife(C_PX + 4, (int)cY + 5);
        return;
      }
    }
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  if (cY > H) {  // tombe dans un trou
    altLoseLife(C_PX + 4, H - 4);
    return;
  }
  if (cScroll >= cStageAt) {  // stage suivant : plus vite, une vie en plus
    gWave++;
    gStageT = 100;
    altStageClear(200UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    cSpeed += 0.2f;
    cStageAt += 1500;
  }
  cGenerate();
  if (gStageT > 0) gStageT--;
}
void cDrawCoin(int x, int y, uint16_t c, uint16_t hi) {  // piece ronde 5x5 avec reflet
  gfx->drawFastHLine(x + 1, y, 3, c);
  gfx->fillRect(x, y + 1, 5, 3, c);
  gfx->drawFastHLine(x + 1, y + 4, 3, c);
  gfx->drawPixel(x + 1, y + 1, hi);
  gMark(x, y, 5, 5);
}
void cDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(0.12f, palFull);
  // montagnes au loin (defilement lent) : lignes de crete
  uint16_t mount = rgb(40, 50, 90);
  float m = cScroll * 0.3f;
  int base = H - 20;
  for (int x = 0; x < W; x += 2) {
    float wx = x + m;
    int h = 10 + (int)(8 * sinf(wx * 0.045f) + 5 * sinf(wx * 0.11f + 1));
    gPix(x, base - h, mount);
    gPix(x + 1, base - h, mount);
  }
  uint16_t grass = rgb(40, 200, 60), dirt = rgb(110, 60, 25), stone = rgb(120, 120, 140);
  for (auto &p : cPlat) {
    if (!p.on) continue;
    int x0 = max(0, (int)(p.x0 - cScroll)), x1 = min(W, (int)(p.x1 - cScroll));
    if (x1 <= x0) continue;
    bool ground = p.y >= cGroundY();
    int hgt = ground ? H - p.y : 3;
    gfx->fillRect(x0, p.y, x1 - x0, hgt, ground ? dirt : stone);
    gfx->drawFastHLine(x0, p.y, x1 - x0, grass);
    gMark(x0, p.y, x1 - x0, hgt);
  }
  uint16_t blockC = rgb(150, 110, 70), blockD = rgb(80, 55, 30), gold = rgb(230, 180, 40), used = rgb(70, 60, 50);
  for (auto &b : cBlock) {  // blocs de pierre et blocs bonus (bordure doree, cristal grave)
    if (!b.on) continue;
    int sx = b.x - cScroll;
    if (sx < -6 || sx > W) continue;
    if (b.kind == 0) {
      gfx->fillRect(sx, b.y, 6, 6, blockC);
      gfx->drawFastHLine(sx, b.y + 5, 6, blockD);
      gfx->drawFastVLine(sx + 5, b.y, 6, blockD);
    } else {
      gfx->fillRect(sx, b.y, 6, 6, b.kind == 1 ? rgb(60, 40, 10) : used);
      gfx->drawRect(sx, b.y, 6, 6, b.kind == 1 ? gold : blockD);
      if (b.kind == 1) { gfx->drawPixel(sx + 2, b.y + 2, palFull[4]); gfx->drawPixel(sx + 3, b.y + 3, palFull[4]); }
    }
    gMark(sx, b.y, 6, 6);
  }
  for (auto &it : cItem) {
    if (!it.on) continue;
    int sx = it.x - cScroll;
    if (sx < -6 || sx > W) continue;
    if (it.kind == 0) {  // piques
      for (int t = 0; t < 3; t++)
        for (int r = 0; r < 4; r++) gfx->drawFastHLine(sx + t * 3 + (3 - r) / 2 - 1, it.y - 1 - r, max(1, r / 2 + 1), palFull[0]);
      gMark(sx - 1, it.y - 5, 11, 5);
    } else if (it.kind == 2) {
      cDrawCoin(sx, it.y - 2, gold, (gFrame & 16) ? palFull[0] : gold);
    } else {  // cristal qui scintille
      uint16_t c = (gFrame & 8) ? palFull[4] : palFull[6];
      gfx->drawPixel(sx + 2, it.y - 3, c);
      gfx->drawFastHLine(sx + 1, it.y - 2, 3, c);
      gfx->drawFastHLine(sx, it.y - 1, 5, c);
      gfx->drawFastHLine(sx + 1, it.y, 3, c);
      gfx->drawPixel(sx + 2, it.y + 1, c);
      gMark(sx, it.y - 3, 5, 5);
    }
  }
  if (cPop.t > 0) cDrawCoin(cPop.x - cScroll, cPop.y, gold, palFull[0]);
  for (auto &rk : cRock) {  // rochers ronds qui tournent
    if (!rk.on) continue;
    int sx = rk.x - cScroll;
    gfx->fillCircle(sx + 3, rk.y + 3, 3, rgb(130, 120, 110));
    float a = -rk.x * 0.3f;
    gfx->drawLine(sx + 3, rk.y + 3, sx + 3 + cosf(a) * 3, rk.y + 3 + sinf(a) * 3, rgb(60, 55, 50));
    gMark(sx - 1, rk.y - 1, 9, 9);
  }
  altBooms(palFull);
  if (gDead == 0 && !(cInv > 0 && (gFrame & 4))) {
    int fr = !cGround ? 2 : (gFrame / 5) & 1;
    gSprite(SPR_RUN[fr], 8, 10, C_PX, (int)cY, palFull, 0, 0);
  }
  altHud(palFull);
  char b[8];  // pieces ramassees (sous les vies)
  snprintf(b, sizeof(b), "%d", cCoins);
  int tw = tinyWidth(b);
  cDrawCoin(W - 1 - tw - 7, 7, gold, gold);
  tinyText(*gfx, W - 1 - tw, 7, b, palFull[0]);
  gMark(W - 1 - tw, 7, tw, 5);
}

// ---------------------------------------------------------------- RAQUETTES
// Une balle, deux raquettes : la tienne en bas (gauche / droite), celle de l'ordinateur en haut.
// L'angle depend de l'endroit ou la balle touche la raquette ; elle accelere a chaque echange.
// Point gagne : des points ; point perdu : une vie. 5 points gagnes : stage suivant (ordinateur plus fort).
float qBX, qBY, qVX, qVY, qPX, qCX, qTrail[4][2];
int qWon = 0, qPW = 22, qServe = 0;
bool qToPlayer = true;
int qTop() { return 13; }
void qServeBall() {
  qBX = W / 2; qBY = (qTop() + H) / 2;
  float sp = 1.1f + 0.12f * gWave + 0.15f * (gEffDiff() - 1);
  float a = (random(2) ? 1 : -1) * (0.35f + random(30) / 100.0f);
  qVX = sinf(a) * sp;
  qVY = (qToPlayer ? 1 : -1) * cosf(a) * sp;
  for (auto &t : qTrail) { t[0] = qBX; t[1] = qBY; }
  qServe = 40;  // la balle attend un instant avant de partir
}
void qReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  qWon = 0;
  qPW = W >= 256 ? 32 : 22;
  qPX = qCX = W / 2 - qPW / 2;
  qToPlayer = true;
  qServeBall();
}
uint8_t qAuto() {  // pilote automatique (demo) : suit la balle
  float c = qPX + qPW / 2.0f, tx = qVY > 0 ? qBX : W / 2;
  return tx < c - 2 ? 1 : tx > c + 2 ? 2 : 0;
}
void qStep() {
  gFrame++;
  uint8_t k = gDemo ? qAuto() : keysPs4;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) qReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      qServeBall();
    }
    return;
  }
  float sp = W > 128 ? 3.2f : 2.4f;
  if (k & 1) qPX -= sp;
  if (k & 2) qPX += sp;
  qPX = constrain(qPX, 0.0f, (float)(W - qPW));
  // l'ordinateur suit la balle avec un temps de reaction et une vitesse limitee (selon stage et difficulte)
  float cs = 0.7f + 0.18f * gWave + 0.25f * (gEffDiff() - 1), target = qVY < 0 ? qBX : W / 2;
  float cc = qCX + qPW / 2.0f;
  if (fabsf(target - cc) > 3) qCX += constrain(target - cc, -cs, cs);
  qCX = constrain(qCX, 0.0f, (float)(W - qPW));
  if (qServe > 0) { qServe--; if (gStageT > 0) gStageT--; return; }
  for (int i = 3; i > 0; i--) { qTrail[i][0] = qTrail[i - 1][0]; qTrail[i][1] = qTrail[i - 1][1]; }
  qTrail[0][0] = qBX; qTrail[0][1] = qBY;
  qBX += qVX; qBY += qVY;
  if (qBX < 0) { qBX = 0; qVX = fabsf(qVX); sfxMenu(); }
  if (qBX > W - 2) { qBX = W - 2; qVX = -fabsf(qVX); sfxMenu(); }
  int py = H - 5, cy = qTop() + 2;
  auto bounce = [&](float padX, bool down) {  // rebond : angle selon le point d'impact, balle plus rapide
    float rel = (qBX + 1 - (padX + qPW / 2.0f)) / (qPW / 2.0f);
    float speed = min(sqrtf(qVX * qVX + qVY * qVY) * 1.06f, 3.6f);
    float a = constrain(rel, -1.0f, 1.0f) * 1.0f;
    qVX = sinf(a) * speed;
    qVY = (down ? 1 : -1) * max(0.6f, cosf(a)) * speed;
    sfxShot();
  };
  if (qVY > 0 && qBY + 2 >= py && qBY + 2 <= py + 3 && qBX + 2 >= qPX && qBX <= qPX + qPW) { qBY = py - 2; bounce(qPX, false); }
  if (qVY < 0 && qBY <= cy + 2 && qBY >= cy - 1 && qBX + 2 >= qCX && qBX <= qCX + qPW) { qBY = cy + 2; bounce(qCX, true); }
  if (qBY > H) {  // point perdu : une vie
    altLoseLife(qBX, H - 4);
    qToPlayer = true;
  } else if (qBY < qTop() - 2) {  // point gagne
    gScore += 100UL * gWave;
    if (!gDemo && gScore > gHi) gHi = gScore;
    gBoomAt(qBX, qTop());
    sfxBoom();
    qToPlayer = false;
    if (++qWon >= 5) {  // stage suivant : ordinateur plus fort, une vie en plus
      qWon = 0;
      gWave++;
      gStageT = 100;
      altStageClear(500UL * gWave);
      sndMelody(JINGLE_STAGE, 4);
      qPW = max(W >= 256 ? 20 : 14, qPW - 2);
    }
    qServeBall();
  }
  if (gStageT > 0) gStageT--;
}
void qDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(0.05f, palFull);
  int mid = (qTop() + H) / 2;
  for (int x = 0; x < W; x += 6) gfx->drawFastHLine(x, mid, 3, rgb(50, 50, 70));  // filet en pointilles
  gMark(0, mid, W, 1);
  int py = H - 5, cy = qTop() + 2;
  gfx->fillRect((int)qPX, py, qPW, 2, palFull[4]);  // ta raquette (cyan)
  gfx->fillRect((int)qCX, cy, qPW, 2, palFull[1]);  // ordinateur (rouge)
  gMark((int)qPX, py, qPW, 2);
  gMark((int)qCX, cy, qPW, 2);
  if (gDead == 0) {
    for (int i = 3; i >= 1; i--) {  // trainee de la balle
      uint8_t v = 40 + 30 * (3 - i);
      gfx->fillRect((int)qTrail[i][0], (int)qTrail[i][1], 2, 2, rgb(v, v, v));
      gMark((int)qTrail[i][0], (int)qTrail[i][1], 2, 2);
    }
    if (qServe == 0 || (gFrame & 4)) { gfx->fillRect((int)qBX, (int)qBY, 2, 2, palFull[0]); gMark((int)qBX, (int)qBY, 2, 2); }
  }
  char b[6];  // points gagnes dans ce stage (sur 5), a gauche du filet
  snprintf(b, sizeof(b), "%d", qWon);
  tinyText(*gfx, 2, mid - 6, b, rgb(110, 110, 130));
  gMark(2, mid - 6, 4, 5);
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- BULLES
// Un petit blob (original) souffle des bulles sur un ecran de plateformes. Une bulle qui touche un ennemi
// l'enferme et monte ; toucher la bulle la fait eclater et l'ennemi devient un cristal a ramasser.
// Sans reaction, l'ennemi s'echappe (plus rapide). Niveau gagne quand tous les ennemis sont elimines.
// Commandes : gauche / droite marcher, haut ou rond sauter, X souffler une bulle.
#define B_PLAT 24
#define B_EN 8
#define B_BUB 8
#define B_GEM 8
struct BPlat { int16_t x0, x1, y; };
struct BEnemy { float x, y, vx, vy; uint8_t type; bool on, fly, angry; };
struct BBubble { float x, y, vx; int16_t t; int8_t en; bool on; };  // en : ennemi enferme (-1 : vide)
struct BGem { float x, y, vy; int16_t t; bool on; };
BPlat bPlat[B_PLAT];
int bNPlat = 0;
BEnemy bEn[B_EN];
BBubble bBub[B_BUB];
BGem bGem[B_GEM];
float bX, bY, bVY;
int bDir = 1, bCool = 0, bInv = 0, bLevelT = 0, bChain = 0, bChainT = 0;
bool bGround = false, bPrevJump = false, bPrevFire = false;
const char *const SPR_BLOB[2][8] = {  // heros original : blob cyan aux grands yeux
  { ".cccccc.", "cwwccwwc", "cbwccbwc", "cccccccc", "cccyyccc", ".cccccc.", ".c.cc.c.", "cc....cc" },
  { ".cccccc.", "cwwccwwc", "cbwccbwc", "cccccccc", "cccyyccc", ".cccccc.", "..c..c..", ".cc..cc." }
};
int bFloor() { return H - 4; }
// plan du niveau : sol, rangees de plateformes avec des trous (symetriques), selon le numero du niveau
void bBuildLevel() {
  randomSeed(1234 + gWave * 97);
  bNPlat = 0;
  bPlat[bNPlat++] = { 0, (int16_t)W, (int16_t)bFloor() };
  int rows = 0;
  for (int y = bFloor() - 22; y > 24 && bNPlat < B_PLAT - 4; y -= 22, rows++) {
    int pattern = random(4);
    int half = W / 2;
    if (pattern == 0) {  // deux longues plateformes, trou au centre
      int g = random(10, 22);
      bPlat[bNPlat++] = { 4, (int16_t)(half - g), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(half + g), (int16_t)(W - 4), (int16_t)y };
    } else if (pattern == 1) {  // une plateforme au centre
      int w = random(W / 4, W / 2);
      bPlat[bNPlat++] = { (int16_t)(half - w / 2), (int16_t)(half + w / 2), (int16_t)y };
    } else if (pattern == 2) {  // trois morceaux
      int w = W / 5;
      bPlat[bNPlat++] = { 4, (int16_t)(4 + w), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(half - w / 2), (int16_t)(half + w / 2), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(W - 4 - w), (int16_t)(W - 4), (int16_t)y };
    } else {  // bords seulement
      int w = random(W / 5, W / 3);
      bPlat[bNPlat++] = { 4, (int16_t)(4 + w), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(W - 4 - w), (int16_t)(W - 4), (int16_t)y };
    }
  }
  randomSeed(esp_random());
  // ennemis : un par plateforme en hauteur (pas sur le sol), plus nombreux au fil des niveaux
  for (auto &e : bEn) e.on = false;
  int n = min(B_EN, 3 + gWave / 2 + (gEffDiff() - 1));
  for (int i = 0; i < n; i++) {
    BPlat &p = bPlat[1 + (i % max(1, bNPlat - 1))];
    BEnemy &e = bEn[i];
    e = {};
    e.on = true;
    e.type = i % 3;
    e.fly = e.type == 2 && gWave >= 2;  // les insectes volent a partir du niveau 2
    e.x = p.x0 + random(0, max(1, p.x1 - p.x0 - 9));
    e.y = p.y - 7;
    e.vx = (random(2) ? 1 : -1) * (0.45f + 0.05f * gWave) * (0.8f + 0.2f * gEffDiff());
    e.vy = e.fly ? 0.5f : 0;
  }
  for (auto &b : bBub) b.on = false;
  for (auto &g : bGem) g.on = false;
  bX = 8; bY = bFloor() - 8; bVY = 0; bDir = 1; bInv = 60; bLevelT = 0;
}
void bReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  bBuildLevel();
}
// reste sur les plateformes : atterrissage en venant du dessus (on traverse par-dessous)
bool bLand(float x, float &y, float &vy, int w, int h) {
  if (vy < 0) return false;
  for (int i = 0; i < bNPlat; i++) {
    BPlat &p = bPlat[i];
    if (x + w - 2 >= p.x0 && x + 1 <= p.x1 && y + h >= p.y && y + h - vy <= p.y + 2) {
      y = p.y - h; vy = 0;
      return true;
    }
  }
  return false;
}
void bShoot() {
  for (auto &b : bBub)
    if (!b.on) { b = { bX + (bDir > 0 ? 8 : -6), bY + 1, 3.2f * bDir, 0, -1, true }; sfxShot(); return; }
}
// pilote automatique (demo) : va vers l'ennemi ou la bulle pleine la plus proche, souffle quand il est aligne
uint8_t bAuto() {
  uint8_t k = 0;
  float tx = bX, ty = bY;
  bool target = false;
  for (auto &b : bBub)
    if (b.on && b.en >= 0) { tx = b.x; ty = b.y; target = true; break; }
  float best = 1e9;
  if (!target)
    for (auto &e : bEn)
      if (e.on) {
        float d = fabsf(e.x - bX) + fabsf(e.y - bY) * 2;
        if (d < best) { best = d; tx = e.x; ty = e.y; }
      }
  if (tx < bX - 2) k |= 1;
  else if (tx > bX + 2) k |= 2;
  if (ty < bY - 10 && bGround && random(100) < 8) k |= 8;  // monte vers la cible
  if (!target && fabsf(ty - bY) < 6 && fabsf(tx - bX) < 50 && (gFrame & 7) == 0) k |= 4;  // souffle
  return k;
}
void bStep() {
  gFrame++;
  uint8_t k = gDemo ? bAuto() : keysPs4;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) bReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      bX = 8; bY = bFloor() - 8; bVY = 0; bInv = 90;
    }
    return;
  }
  bLevelT++;
  bool angryAll = bLevelT > 40 * 40;  // apres 40 s, les ennemis s'enervent
  // joueur
  if (k & 1) { bX -= 1.3f; bDir = -1; }
  if (k & 2) { bX += 1.3f; bDir = 1; }
  bX = constrain(bX, 4.0f, (float)(W - 12));
  bool jumpKey = k & (8 | 32), fireKey = k & 4;
  if (jumpKey && !bPrevJump && bGround) { bVY = -4.2f; bGround = false; sfxDive(); }
  bPrevJump = jumpKey;
  if (bCool > 0) bCool--;
  if (fireKey && !bPrevFire && bCool == 0) { bShoot(); bCool = 10; }
  bPrevFire = fireKey;
  bVY = min(bVY + 0.3f, 3.5f);
  bY += bVY;
  bGround = bLand(bX, bY, bVY, 8, 8);
  if (bY < 14) { bY = 14; bVY = 0; }
  if (bInv > 0) bInv--;
  if (bChainT > 0 && --bChainT == 0) bChain = 0;
  // bulles : lancee horizontale, puis montee au plafond, derive, eclate apres un moment
  for (auto &b : bBub) {
    if (!b.on) continue;
    b.t++;
    if (b.t < 14) b.x += b.vx;
    else { b.y -= 0.6f; b.x += sinf(b.t * 0.08f) * 0.3f; }
    if (b.y < 16) b.y = 16;
    b.x = constrain(b.x, 6.0f, (float)(W - 6));
    if (b.en < 0 && b.t < 14)  // la bulle attrape un ennemi
      for (int i = 0; i < B_EN; i++)
        if (bEn[i].on && fabsf(bEn[i].x + 4 - b.x) < 7 && fabsf(bEn[i].y + 3 - b.y) < 7) {
          bEn[i].on = false; b.en = i; b.t = 14; sfxMenu();
          break;
        }
    int life = b.en >= 0 ? (angryAll ? 200 : 280) : 260;
    if (b.t > life) {  // eclate seule : l'ennemi s'echappe, en colere
      if (b.en >= 0) {
        BEnemy &e = bEn[b.en];
        e.on = true; e.angry = true; e.x = b.x - 4; e.y = b.y - 3;
        e.vx = (random(2) ? 1 : -1) * 1.0f;
      }
      b.on = false;
      continue;
    }
    // le joueur touche la bulle : elle eclate (ennemi -> cristal)
    if (fabsf(b.x - (bX + 4)) < 8 && fabsf(b.y - (bY + 4)) < 8 && b.t > 6) {
      b.on = false;
      gBoomAt(b.x, b.y);
      if (b.en >= 0) {
        bChain++; bChainT = 40;
        gScore += 100UL * bChain;
        for (auto &g : bGem)
          if (!g.on) { g = { b.x - 2, b.y, -1.5f, 0, true }; break; }
        sfxBoom();
      } else {
        gScore += 10;
      }
    }
  }
  // ennemis : marchent, font demi-tour au bord, sautent parfois ; les insectes volent en diagonale
  for (auto &e : bEn) {
    if (!e.on) continue;
    float sp = (e.angry || angryAll) ? 1.6f : 1;
    if (e.fly) {
      e.x += e.vx * sp; e.y += e.vy * sp;
      if (e.x < 4 || e.x > W - 13) e.vx = -e.vx;
      if (e.y < 14 || e.y > bFloor() - 7) e.vy = -e.vy;
    } else {
      e.x += e.vx * sp;
      if (e.x < 4 || e.x > W - 13) { e.vx = -e.vx; e.x = constrain(e.x, 4.0f, (float)(W - 13)); }
      e.vy = min(e.vy + 0.3f, 3.0f);
      e.y += e.vy;
      bool g = bLand(e.x, e.y, e.vy, 9, 7);
      if (g && random(1000) < 6) e.vy = -4.0f;  // saute vers l'etage du dessus
      if (g) {  // au bord d'une plateforme : demi-tour (sauf parfois)
        bool under = false;
        float fx = e.x + (e.vx > 0 ? 10 : -1);
        for (int i = 0; i < bNPlat; i++)
          if (fx >= bPlat[i].x0 && fx <= bPlat[i].x1 && bPlat[i].y == (int16_t)(e.y + 7)) under = true;
        if (!under && random(100) < 70) e.vx = -e.vx;
      }
    }
    if (gDead == 0 && bInv == 0 && e.x + 8 >= bX + 1 && e.x <= bX + 7 && e.y + 6 >= bY + 1 && e.y <= bY + 7) {
      altLoseLife(bX + 4, bY + 4);
      return;
    }
  }
  // cristaux : tombent, rebondissent, a ramasser
  for (auto &g : bGem) {
    if (!g.on) continue;
    g.t++;
    g.vy = min(g.vy + 0.25f, 3.0f);
    g.y += g.vy;
    bLand(g.x, g.y, g.vy, 5, 5);
    if (fabsf(g.x + 2 - (bX + 4)) < 7 && fabsf(g.y + 2 - (bY + 4)) < 8) {
      g.on = false;
      gScore += 500;
      sfxMenu();
    } else if (g.t > 400) g.on = false;
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  // niveau gagne : plus d'ennemi (ni libre, ni enferme)
  bool left = false;
  for (auto &e : bEn) left |= e.on;
  for (auto &b : bBub) left |= b.on && b.en >= 0;
  if (!left && gStageT == 0) {
    gWave++;
    gStageT = 100;
    altStageClear(1000UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    bBuildLevel();
  }
  if (gStageT > 0) gStageT--;
}
void bDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(0.05f, palFull);
  static const uint8_t PC[4][3] = { { 200, 80, 160 }, { 60, 160, 220 }, { 220, 140, 40 }, { 90, 200, 90 } };
  const uint8_t *c = PC[(gWave - 1) % 4];
  uint16_t plat = rgb(c[0], c[1], c[2]), platD = rgb(c[0] / 3, c[1] / 3, c[2] / 3);
  for (int i = 0; i < bNPlat; i++) {  // plateformes (briques de la couleur du niveau)
    BPlat &p = bPlat[i];
    gfx->fillRect(p.x0, p.y, p.x1 - p.x0, 3, plat);
    for (int x = p.x0 + 3; x < p.x1; x += 6) gfx->drawPixel(x, p.y + 1, platD);
    gMark(p.x0, p.y, p.x1 - p.x0, 3);
  }
  gfx->fillRect(0, 12, 3, H - 12, plat);  // murs
  gfx->fillRect(W - 3, 12, 3, H - 12, plat);
  gMark(0, 12, 3, H - 12);
  gMark(W - 3, 12, 3, H - 12);
  uint16_t white[9], red[9];
  for (int i = 0; i < 9; i++) { white[i] = palFull[0]; red[i] = palFull[1]; }
  int w = (gWave - 1) % 4;
  for (auto &e : bEn)
    if (e.on) gSprite(SPR_W[w][e.type][(gFrame / 6) & 1], GW, GH, (int)e.x, (int)e.y, e.angry ? red : palFull, 0, 0);
  for (auto &b : bBub) {  // bulles : contour, reflet, ennemi dedans
    if (!b.on) continue;
    bool full = b.en >= 0;
    int r = full ? 6 : 4;
    if (full) gSprite(SPR_W[w][bEn[b.en].type][(gFrame / 10) & 1], GW, GH, (int)b.x - 4, (int)b.y - 3, palFull, 0, 0);
    bool warn = full && b.t > 200 && (gFrame & 4);  // va bientot eclater
    gfx->drawCircle((int)b.x, (int)b.y, r, warn ? palFull[1] : palFull[5]);
    gfx->drawPixel((int)b.x - r / 2, (int)b.y - r / 2, palFull[0]);
    gMark((int)b.x - r, (int)b.y - r, 2 * r + 1, 2 * r + 1);
  }
  for (auto &g : bGem) {  // cristaux
    if (!g.on) continue;
    uint16_t gc = (gFrame & 8) ? palFull[4] : palFull[6];
    int x = g.x, y = g.y;
    gfx->drawPixel(x + 2, y, gc);
    gfx->drawFastHLine(x + 1, y + 1, 3, gc);
    gfx->drawFastHLine(x, y + 2, 5, gc);
    gfx->drawFastHLine(x + 1, y + 3, 3, gc);
    gfx->drawPixel(x + 2, y + 4, gc);
    gMark(x, y, 5, 5);
  }
  altBooms(palFull);
  if (gDead == 0 && !(bInv > 0 && (gFrame & 4))) gSprite(SPR_BLOB[bGround ? (gFrame / 6) & 1 : 0], 8, 8, (int)bX, (int)bY, palFull, 0, 0);
  altHud(palFull);
}

// aiguillage selon le jeu choisi
void altReset() { if (gGame == 1) rReset(); else if (gGame == 2) cReset(); else if (gGame == 3) qReset(); else bReset(); }
void altStep() { if (gGame == 1) rStep(); else if (gGame == 2) cStep(); else if (gGame == 3) qStep(); else bStep(); }
void altDraw() { if (gGame == 1) rDraw(); else if (gGame == 2) cDraw(); else if (gGame == 3) qDraw(); else bDraw(); }
// remise a zero du jeu choisi (partie ou demo)
void resetCurrent() { if (gGame == 0) gReset(); else altReset(); }

void startGame() {
  gDemo = false;
  menuScores = false;
  gfx->fillScreen(0);
  gNOld = 0;
  resetCurrent();
}
// mode demo du jeu choisi (ligne DEMO du menu, ou 30 s sans toucher la manette)
void startAttract() {
  gAttract = true;
  menuScores = false;
  gfx->fillScreen(0);
  gNOld = 0;
  resetCurrent();
}
void toDemo() {
  gDemo = true;
  gAttract = false;  // fin de partie : le menu d'abord, la demo seule apres 30 s
  lastInput = millis();
  gfx->fillScreen(0);
  gNOld = 0;
  resetCurrent();
}

// Ecran du menu : fond d'etoiles qui defile, titre GAME HUB, record, puis le menu.
// La demo ne tourne plus derriere (sprites, annonces et scores de la demo melanges au menu).
void menuScreen() {
  gFrame++;
  for (int i = 0; i < gNStar; i++) {  // etoiles qui defilent doucement
    GStar &s = gStar[i];
    s.y += s.v * 0.5f;
    if (s.y >= H) { s.y = 0; s.x = random(W); }
  }
  if (gOverflow) { gOverflow = false; gNOld = 0; gfx->fillScreen(0); }
  for (int i = 0; i < gNOld; i++) {  // efface l'image precedente
    GRect &rc = gOld[i];
    int x0 = max(0, (int)rc.x), y0 = max(0, (int)rc.y), x1 = min(W, rc.x + rc.w), y1 = min(H, rc.y + rc.h);
    if (x1 > x0 && y1 > y0) gfx->fillRect(x0, y0, x1 - x0, y1 - y0, 0);
  }
  gNOld = 0;
  for (int i = 0; i < gNStar; i++) {
    GStar &s = gStar[i];
    if (((gFrame + i * 7) & 31) < 3) continue;
    uint8_t gr = 70 + s.v * 120;
    gPix(s.x, (int)s.y, rgb(gr, gr, gr));
  }
  gfx->setFont(NULL);
  if (H >= 128 && !menuScores && !gNameEntry) {  // titre et record au-dessus du menu
    int ty = H / 2 - 56;  // titre et record au-dessus du cadre du menu (qui commence a H/2 - 28)
    gfx->setTextSize(2);
    gfx->setTextColor(rgb(255, 220, 0));
    int tw = 8 * 12;
    gfx->setCursor((W - tw) / 2, ty);
    gfx->print("GAME HUB");
    gMark((W - tw) / 2, ty, tw, 16);
    gfx->setTextSize(1);
    char b[24];
    snprintf(b, sizeof(b), "RECORD %lu %s", (unsigned long)gHi, gBest[0].score ? gBest[0].name : "");
    gText(ty + 18, b, rgb(255, 40, 40));
  }
  gfx->setTextSize(1);
  if (gNameEntry) drawNameEntry();
  else drawMenu();
}

// saisie du nom (3 lettres, facon borne d'arcade) apres un score qui entre au tableau
void startNameEntry() {
  gNameEntry = true;
  gNamePos = 0;
  gPaused = false;
  lastInput = millis();
  sndMelody(JINGLE_STAGE, 4);
}
void finishNameEntry() {
  gNameEntry = false;
  if (gLastRank) {
    memcpy(gBest[gLastRank - 1].name, gName, 4);
    prefs.putBytes(topKey(), gBest, sizeof(gBest));
    prefs.putString("lastname", gName);
  }
  toDemo();
  menuScores = true;  // on montre le tableau avec le nouveau nom
  lastInput = millis();
}
void drawNameEntry() {
  bool tall = H >= 128;
  int y = tall ? H / 2 - 40 : 6;
  gBox(y - 6, tall ? y + 74 : y + 54);
  char b[24];
  if (gLastRank == 1) snprintf(b, sizeof(b), "NOUVEAU RECORD !");
  else snprintf(b, sizeof(b), "BRAVO ! RANG %u", gLastRank);
  gText(y, b, rgb(255, 220, 0));
  snprintf(b, sizeof(b), "SCORE %lu", (unsigned long)gScore);
  gText(y + 11, b, rgb(230, 230, 230));
  int ly = y + (tall ? 26 : 22), lx = (W - 44) / 2;  // 3 grandes lettres (taille 2)
  gfx->setTextSize(2);
  for (int i = 0; i < 3; i++) {
    gfx->setTextColor(i == gNamePos ? rgb(255, 220, 0) : rgb(0, 220, 255));
    gfx->setCursor(lx + i * 16, ly);
    gfx->print(gName[i]);
  }
  gfx->setTextSize(1);
  gMark(lx, ly, 44, 16);
  if ((gFrame / 10) & 1) {  // curseur qui clignote sous la lettre en cours
    gfx->fillRect(lx + gNamePos * 16, ly + 17, 10, 2, rgb(255, 220, 0));
  }
  gMark(lx, ly + 17, 44, 2);
  if (tall) {
    gText(y + 50, "HAUT/BAS : LETTRE", rgb(110, 110, 130));
    gText(y + 61, "OPTIONS : VALIDER", rgb(110, 110, 130));
  } else {
    gText(y + 42, "OPTIONS: VALIDER", rgb(110, 110, 130));
  }
}
// croix haut / bas : lettre ; gauche / droite ou X : lettre precedente / suivante ; rond : retour ; OPTIONS : valider
void nameInput(uint8_t edge) {
  if (!edge) return;
  int n = strlen(NAME_CHARS);
  const char *p = strchr(NAME_CHARS, gName[gNamePos]);
  int c = p ? p - NAME_CHARS : 0;
  if (edge & 1) { gName[gNamePos] = NAME_CHARS[(c + 1) % n]; sfxMenu(); }
  if (edge & 2) { gName[gNamePos] = NAME_CHARS[(c + n - 1) % n]; sfxMenu(); }
  if (edge & (4 | 32)) { if (gNamePos > 0) gNamePos--; sfxMenu(); }
  if (edge & 8) { if (gNamePos < 2) gNamePos++; sfxMenu(); }
  if (edge & 16) {  // X : lettre suivante, ou valider sur la derniere
    if (gNamePos < 2) { gNamePos++; sfxMenu(); }
    else { finishNameEntry(); return; }
  }
  if (edge & 64) finishNameEntry();
}

void gameUpdate() {
  uint32_t now = millis();
  if (now - gLastStep < 25) return;  // 40 images par seconde
  gLastStep = now - gLastStep > 100 ? now : gLastStep + 25;
  if (startReq) {
    startReq = false;
    if (gDemo || (gLives <= 0)) startGame();
  }
  if (gNameEntry) {  // saisie du nom : ecran a part (60 s sans toucher : nom valide tel quel)
    static bool shown = false;
    if (!shown) { shown = true; gfx->fillScreen(0); gNOld = 0; }
    if (now - lastInput > 60000UL) finishNameEntry();
    else { menuScreen(); return; }
    shown = false;
  }
  if (gPaused) {  // pause : l'image reste figee
    if (gGame) altDraw();
    else gameDraw();
    gOverlay();
    return;
  }
  if (!gDemo && now - lastInput > IDLE_TO_DEMO_MS) toDemo();
  if (gDemo && !gAttract && now - lastInput > MENU_IDLE_MS) startAttract();  // menu sans activite : demo du jeu choisi
  // changement d'ecran (menu / demo / partie) : on efface tout, aucun reste de l'ecran precedent
  bool menuMode = gDemo && PS4.isConnected() && !gAttract;
  static int lastScreen = -1;
  int screen = menuMode ? 0 : gDemo ? 1 : 2;  // (saisie du nom : ecran 3, gere plus haut)
  if (screen != lastScreen) {
    lastScreen = screen;
    gfx->fillScreen(0);
    gNOld = 0;
  }
  if (menuMode) { menuScreen(); return; }
  if (gGame) { altStep(); altDraw(); }
  else { gameStep(); gameDraw(); }
  gOverlay();
}

uint32_t restartAt = 0;
uint32_t brightSaveAt = 0;  // luminosite changee : enregistree 2 s plus tard

// Adresse Bluetooth que l'ESP32 prend : celle que la manette connait deja (sa console, ou ecrite avec
// SixaxisPairTool). Vide : adresse d'origine de l'ESP32, a ecrire dans la manette.
void ps4Begin() {
  String mac = prefs.getString("ps4mac", "");
  bool ok = mac.length() == 17 ? PS4.begin(mac.c_str()) : PS4.begin();
  // pas de mode veille Bluetooth (sniff) : sinon les rapports de la manette n'arrivent que toutes les 1 a 2 s
  BTM_SetDefaultLinkPolicy(0x0001);  // HCI_ENABLE_MASTER_SLAVE_SWITCH seul (sniff, hold et park interdits)
  // anciens appairages gardes par l'ESP32 : cause connue de deconnexion juste apres la connexion
  int nb = esp_bt_gap_get_bond_device_num();
  if (nb > 0) {
    esp_bd_addr_t list[8];
    int n = min(nb, 8);
    esp_bt_gap_get_bond_device_list(&n, list);
    for (int i = 0; i < n; i++) esp_bt_gap_remove_bond_device(list[i]);
    Serial.printf("%d ancien(s) appairage(s) efface(s)\n", n);
  }
  snprintf(ipText, sizeof(ipText), "%s", PS4.getAddress().c_str());
  Serial.printf("Manette PS4 : Bluetooth %s, adresse %s, memoire libre %u\n", ok ? "pret" : "ECHEC", ipText, ESP.getFreeHeap());
}

// lecture de la manette a chaque tour de boucle ; barre lumineuse selon le stage, vibration quand on est touche
extern "C" uint8_t ps4DbgHdr[24];
extern "C" volatile int ps4DbgPos;
extern "C" volatile uint8_t ps4DbgType;
extern "C" volatile uint8_t ps4DbgRaw[12];
extern "C" volatile int ps4DbgLen;
extern "C" volatile uint32_t ps4DbgCount;
extern "C" volatile uint32_t ps4DbgSent;
extern "C" volatile int ps4DbgPmPolicy, ps4DbgPmMode;
void ps4Poll() {
  static bool dbgShown = false;
  if (!dbgShown && ps4DbgPos != -2) {  // diagnostic : debut du 1er paquet de la manette
    dbgShown = true;
    Serial.printf("1er paquet manette : entete 0xA1 en position %d, rapport 0x%02X, octets :", ps4DbgPos, ps4DbgType);
    for (int i = 0; i < 24; i++) Serial.printf(" %02X", ps4DbgHdr[i]);
    Serial.println();
  }
  static bool was = false;
  static int lastTier = -2;
  static uint32_t rumbleOff = 0, lastBr = 0, combo = 0, lastLog = 0, since = 0;
  static bool pmShown = false;
  if (!PS4.isConnected()) {
    keysPs4 = 0;
    if (was) {
      was = false;
      Serial.printf("Manette PS4 deconnectee apres %lu s, memoire libre %u (plus petit bloc %u)\n", (millis() - since) / 1000,
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
    return;
  }
  if (!was) {
    was = true;
    lastTier = -2;
    since = millis();
    Serial.printf("Manette PS4 connectee, memoire libre %u (plus petit bloc %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    pmShown = false;
  }
  if (!pmShown && ps4DbgPmMode != -1) {  // lien force en mode actif (code 0 = ok)
    pmShown = true;
    Serial.printf("Lien manette : veille interdite (code %d), mode actif demande (code %d)\n", ps4DbgPmPolicy, ps4DbgPmMode);
  }
  if (millis() - lastLog > 5000) {  // etat de la manette toutes les 5 s
    lastLog = millis();
    static uint32_t lastCount = 0, lastSent = 0;
    uint32_t cnt = ps4DbgCount, sent = ps4DbgSent, rx = cnt - lastCount, tx = sent - lastSent;
    Serial.printf("  PS4 ok %lu s, %lu paquets/s recus sur ~%lu envoyes (%lu%% perdus), batterie %u/15, memoire libre %u (min %u)\n",
                  (millis() - since) / 1000, (unsigned long)(rx / 5), (unsigned long)(tx / 5), (unsigned long)(tx > rx ? (tx - rx) * 100 / tx : 0),
                  PS4.Battery(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    lastCount = cnt;
    lastSent = sent;
  }
  static bool stable = false;
  if (!stable && millis() - since > 15000) {  // connexion stable : les couleurs choisies tiennent
    stable = true;
    if (prefs.getUChar("crash", 0)) prefs.putUChar("crash", 0);
  }
  if (millis() - since < 1000) return;  // laisse la connexion s'etablir avant d'envoyer quoi que ce soit a la manette
  uint8_t k = 0;
  // stick avec hysteresis : pousse au-dela de 50, relache en dessous de 30 (pas d'aller-retour a la limite)
  int sx = PS4.LStickX();
  bool stL = sx < ((keysPs4 & 1) ? -30 : -50), stR = sx > ((keysPs4 & 2) ? 30 : 50);
  if (PS4.Left() || PS4.UpLeft() || PS4.DownLeft() || stL) k |= 1;
  if (PS4.Right() || PS4.UpRight() || PS4.DownRight() || stR) k |= 2;
  // haut / bas (avancer / reculer) ; Share + croix haut / bas reste pour la luminosite
  int sy = PS4.LStickY();
  bool stU = sy > ((keysPs4 & 8) ? 30 : 50), stD = sy < ((keysPs4 & 16) ? -30 : -50);
  if (!PS4.Share()) {
    if (PS4.Up() || PS4.UpLeft() || PS4.UpRight() || stU) k |= 8;
    if (PS4.Down() || PS4.DownLeft() || PS4.DownRight() || stD) k |= 16;
  }
  if (PS4.Cross() || PS4.Square() || PS4.Triangle() || PS4.R1() || PS4.L1() || PS4.R2Value() > 60) k |= 4;  // tir / saut
  if (PS4.Circle()) k |= 32;  // rond : action speciale (bombe dans RAID)
  if (k) lastInput = millis();
  keysPs4 = gPaused ? 0 : k;
  // boutons du menu et START (Options) : on agit a l'appui
  static uint8_t prevB = 0;
  uint8_t bt = PS4.Up() | PS4.Down() << 1 | PS4.Left() << 2 | PS4.Right() << 3 | PS4.Cross() << 4 | PS4.Circle() << 5 |
               (PS4.Options() && !PS4.Share()) << 6;
  uint8_t edge = bt & ~prevB;
  prevB = bt;
  if (edge) lastInput = millis();
  if (gNameEntry) nameInput(edge);  // saisie du nom
  else if (gDemo && gAttract) {  // mode demo : n'importe quel bouton ou le stick fait revenir le menu (sans autre effet)
    if (edge || k) { gAttract = false; lastInput = millis(); sfxMenu(); prevB = 0xFF; }
  } else if (gDemo) menuInput(edge);
  else if (edge & 64 && gLives > 0) { gPaused = !gPaused; sfxMenu(); }  // OPTIONS : pause / reprise
  else if (gPaused && (edge & 32)) {  // pause + rond : on quitte la partie (score enregistre s'il entre au tableau)
    gPaused = false;
    topAdd();
    sfxMenu();
    if (gLastRank) startNameEntry();
    else toDemo();
  }
  if (PS4.Share() && (PS4.Up() || PS4.Down()) && millis() - lastBr > 150) {  // Share + croix haut / bas : luminosite
    lastBr = millis();
    bright = constrain(bright + (PS4.Up() ? 15 : -15), 5, 255);
    dma->setBrightness8(bright);
    brightSaveAt = millis() + 2000;
  }
  if (PS4.Share() && PS4.Options()) {  // Share + Options 3 s : redemarrage
    if (!combo) combo = millis();
    else if (millis() - combo > 3000) restartAt = millis();
  } else combo = 0;
  bool send = false;
  int tier = gDemo ? -1 : gTier();
  if (tier != lastTier) {  // barre lumineuse : bleu sombre en demo, puis couleur du vaisseau
    static const uint8_t LED[5][3] = { { 0, 0, 40 }, { 0, 90, 255 }, { 40, 230, 80 }, { 0, 220, 255 }, { 255, 40, 40 } };
    lastTier = tier;
    PS4.setLed(LED[tier + 1][0], LED[tier + 1][1], LED[tier + 1][2]);
    send = true;
  }
  if (rumbleReq) {
    rumbleReq = false;
    PS4.setRumble(0, 220);
    rumbleOff = millis() + 300;
    send = true;
  } else if (rumbleOff && millis() > rumbleOff) {
    rumbleOff = 0;
    PS4.setRumble(0, 0);
    send = true;
  }
  if (send) PS4.sendToController();
}

// menu (demo + manette) : haut / bas = ligne, gauche / droite = niveau, X = valider, START = jouer
void menuInput(uint8_t edge) {
  if (!edge) return;
  if (menuScores) {  // page des scores : X, rond ou START pour revenir
    if (edge & (16 | 32 | 64)) { menuScores = false; sfxMenu(); }
    return;
  }
  if (edge & 64) { startReq = true; return; }
  if (menuSel == 0 && (edge & (4 | 8))) {  // choix du jeu : gauche / droite
    gGame = (gGame + ((edge & 4) ? N_GAMES - 1 : 1)) % N_GAMES;
    prefs.putUChar("game", gGame);
    topLoad();  // meilleurs scores de ce jeu
    sfxMenu();
  }
  if (edge & 1) { menuSel = (menuSel + MENU_N - 1) % MENU_N; sfxMenu(); }
  if (edge & 2) { menuSel = (menuSel + 1) % MENU_N; sfxMenu(); }
  if (menuSel == 2 && (edge & (4 | 8 | 16))) {  // luminosite : gauche -10 %, droite ou X +10 % (X boucle 100 -> 0)
    int p = brightPct();
    if (edge & 4) p -= 10;
    else if (edge & 8) p += 10;
    else p = p >= 100 ? 0 : p + 10;
    setBrightPct(p);
    sfxMenu();
  }
  if (menuSel == 3 && (edge & (4 | 8 | 16))) {  // volume du son (rapport cyclique du PWM) : 0 a 100 %, pas de 10 %
    int v = volume;
    if (edge & 4) v--;
    else if (edge & 8) v++;
    else v = v >= 10 ? 0 : v + 1;
    volume = constrain(v, 0, 10);
    prefs.putUChar("vol", volume);
    sfxMenu();  // bip au nouveau volume (rien a 0 %)
  }
  if (menuSel == 1 && (edge & (4 | 8 | 16))) {
    gDiff = (gDiff + ((edge & 4) ? 2 : 1)) % 3;
    prefs.putUChar("diff", gDiff);
    sfxMenu();
  }
  if (edge & 16) {
    if (menuSel == 0) startReq = true;
    else if (menuSel == 4) { menuScores = true; sfxMenu(); }
    else if (menuSel == 5) { menuSel = 0; sfxMenu(); startAttract(); }  // ligne DEMO : demo du jeu choisi
  }
}

// reglages par le moniteur serie
void serialMenu() {
  Serial.println();
  Serial.println("===== Game hub v" GAMES_VERSION " =====");
  Serial.printf("Adresse Bluetooth de l'ESP32 : %s\n", ipText);
  Serial.printf("Dalles : %u, disposition %u, retourne %u, serpentin %u -> ecran %dx%d, couleurs %u bits\n", nPanels, arrange, flipAll,
                serpent, W, H, colorBits);
  Serial.println("  a xx:xx:xx:xx:xx:xx : adresse de la console connue par la manette (redemarre)");
  Serial.println("  a                   : revenir a l'adresse d'origine de l'ESP32 (redemarre)");
  Serial.println("  c NDFS : dalles. N nombre 1-4, D 0 l'une sous l'autre / 1 cote a cote / 2 carre,");
  Serial.println("           F ecran retourne 0/1, S une dalle sur deux retournee 0/1. Ex. : c 2000");
  Serial.println("  p N : couleurs des dalles sur N bits (3 a 6, redemarre). 3 = le plus sur pour le Bluetooth,");
  Serial.println("        4 = plus de nuances. 2 plantages de suite : retour automatique a 3.");
  Serial.printf("  v N : volume du son, 0 (muet) a 10. Actuel : %u\n", volume);
  Serial.println("  e 0 / e 1 : test, LED eteintes / rallumees ; e x : transfert vers les dalles arrete (jusqu'au redemarrage)");
  Serial.println("  ? : ce menu");
}
void handleSerial() {
  static char line[32];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (n < sizeof(line) - 1) line[n++] = ch;
      continue;
    }
    line[n] = 0;
    char *p = line;
    while (*p == ' ') p++;
    char cmd = *p;
    char *arg = p + (cmd ? 1 : 0);
    while (*arg == ' ') arg++;
    n = 0;
    if (cmd == 'a') {
      if (strlen(arg) == 17 || strlen(arg) == 0) {
        prefs.putString("ps4mac", arg);
        Serial.printf("Adresse manette : %s, redemarrage\n", arg[0] ? arg : "origine");
        restartAt = millis() + 300;
      } else Serial.println("Format : a xx:xx:xx:xx:xx:xx");
    } else if (cmd == 'c') {
      if (strlen(arg) == 4 && arg[0] >= '1' && arg[0] <= '4' && arg[1] >= '0' && arg[1] <= '2') {
        prefs.putUChar("nb", arg[0] - '0');
        prefs.putUChar("arr", arg[1] - '0');
        prefs.putBool("flip", arg[2] == '1');
        prefs.putBool("serp", arg[3] == '1');
        Serial.println("Dalles enregistrees, redemarrage");
        restartAt = millis() + 300;
      } else Serial.println("Format : c NDFS (ex. c 2000)");
    } else if (cmd == 'e') {  // test des pertes Bluetooth : parasites des LED ou charge du transfert d'image ?
      if (*arg == '0') { dma->setBrightness8(0); Serial.println("Test : LED eteintes (transfert d'image actif)"); }
      else if (*arg == '1') { dma->setBrightness8(bright); Serial.println("Test : LED rallumees"); }
      else if (*arg == 'x') { dma->stopDMAoutput(); Serial.println("Test : transfert vers les dalles arrete (redemarrer pour revenir)"); }
      else Serial.println("Format : e 0, e 1 ou e x");
    } else if (cmd == 'v') {
      int v = atoi(arg);
      if (*arg && v >= 0 && v <= 10) {
        volume = v;
        prefs.putUChar("vol", v);
        Serial.printf("Volume %d/10\n", v);
        bool demo = gDemo;
        gDemo = false;
        sfxBoom();  // pour entendre le niveau
        gDemo = demo;
      } else Serial.println("Format : v 0 a v 10");
    } else if (cmd == 'p') {
      int b = atoi(arg);
      if (b >= 3 && b <= 6) {
        prefs.putUChar("bits", b);
        prefs.putUChar("crash", 0);
        Serial.printf("Couleurs %d bits, redemarrage\n", b);
        restartAt = millis() + 300;
      } else Serial.println("Format : p 3 a p 6 (ex. p 4)");
    } else if (cmd == '?') serialMenu();
    else if (cmd) Serial.println("Commande inconnue, tapez ?");
  }
}

// ---------------------------------------------------------------- demarrage
void setup() {
  Serial.begin(115200);
  Serial.println("Game hub v" GAMES_VERSION " (manette PS4)");
  prefs.begin("games", false);
  bright = prefs.getUChar("bright", 120);
  nPanels = constrain(prefs.getUChar("nb", 2), 1, 4);
  arrange = prefs.getUChar("arr", 0) % 3;
  flipAll = prefs.getBool("flip", false);
  serpent = prefs.getBool("serp", false);

  HUB75_I2S_CFG::i2s_pins pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN };
  // profondeur de couleur choisie, en laissant assez de memoire au Bluetooth
  // Couleurs choisies au moniteur serie (p 3 a p 6), 3 bits par defaut : au-dela, le Bluetooth peut manquer de
  // memoire a la connexion de la manette (assert hci_hal_h4.c:553). 2 plantages de suite : retour a 3 bits.
  uint8_t wantBits = constrain(prefs.getUChar("bits", 3), 3, 6);
  esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT) {
    uint8_t crashes = prefs.getUChar("crash", 0) + 1;
    if (crashes >= 2 && wantBits > 3) {
      wantBits = 3;
      prefs.putUChar("bits", 3);
      crashes = 0;
      Serial.println("2 plantages de suite : retour a 3 bits de couleur");
    }
    prefs.putUChar("crash", crashes);
  }
  const int maxDepth = wantBits, needHeap = 0;
  for (int depth = maxDepth; depth >= 3; depth--) {
    HUB75_I2S_CFG cfg(128, 64, nPanels, pins);
    cfg.setPixelColorDepthBits(depth);
    cfg.clkphase = false;
    dma = new MatrixPanel_I2S_DMA(cfg);
    if (dma->begin()) {
      if (ESP.getFreeHeap() >= needHeap || depth == 3) {
        colorBits = depth;
        Serial.printf("Ecran %dx%d (%d dalles), couleurs %d bits, memoire libre %u\n", 128 * panelCols(), 64 * panelRows(), nPanels, depth,
                      ESP.getFreeHeap());
        break;
      }
      dma->stopDMAoutput();
    }
    delete dma;
    dma = nullptr;
  }
  if (!dma) {  // trop de dalles pour la memoire : retour a 2 dalles plutot que rester bloque
    Serial.println("Memoire insuffisante pour les dalles");
    if (nPanels > 2) { prefs.putUChar("nb", 2); delay(50); ESP.restart(); }
    for (;;) delay(1000);
  }
  W = 128 * panelCols();
  H = 64 * panelRows();
  gfx = new TiledPanels(dma, panelCols(), panelRows(), flipAll, serpent);
  dma->setBrightness8(bright);
  gfx->fillScreen(0);

  gDiff = prefs.getUChar("diff", 1) % 3;
  gGame = 0;  // demarrage toujours sur ESSAIM (choix d'un autre jeu : gauche / droite dans le menu)
  ps4Begin();
  audioBegin();
  serialMenu();

  gameSetup();
  resetCurrent();  // demo du jeu choisi au demarrage
  gDemo = true;
}

void loop() {
  if (restartAt && millis() > restartAt) ESP.restart();
  ps4Poll();
  sndUpdate();
  handleSerial();
  if (brightSaveAt && millis() > brightSaveAt) { brightSaveAt = 0; prefs.putUChar("bright", bright); }
  gameUpdate();
  vTaskDelay(1);
}
