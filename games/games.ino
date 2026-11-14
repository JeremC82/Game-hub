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

#define GAMES_VERSION "2.24"

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
uint8_t keysPs4 = 0;                // touches de la manette PS4 : 1 gauche, 2 droite, 4 feu, 8 haut, 16 bas
int gPYBase = 0;                    // hauteur de depart du vaisseau (en bas) ; il peut monter jusqu'a gPYMin
int gPYMin = 0;
volatile bool rumbleReq = false;    // vaisseau touche : la manette PS4 vibre
volatile uint32_t lastInput = 0;    // dernier appui d'un joueur (millis)
volatile bool startReq = false;     // FEU pendant la demo : nouvelle partie
bool gDemo = true;                  // le jeu joue tout seul (menu par-dessus quand la manette est la)
bool gPaused = false;               // START pendant une partie : pause
uint8_t gDiff = 1;                  // difficulte : 0 facile, 1 normal, 2 difficile
const char *const DIFF_NAME[3] = { "FACILE", "NORMAL", "DIFFICILE" };
uint8_t menuSel = 0;                // ligne choisie : 0 jouer, 1 niveau, 2 luminosite, 3 scores, 4 demo
#define MENU_N 5
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
// Le generateur PWM materiel (LEDC) joue une note carree ; une minuterie de 1 ms (esp_timer) fait varier la
// frequence (glissandos, bruit des explosions) et le rapport cyclique (decroissance du volume).
// Pas d'I2S ni de tache son : l'ancien son I2S faisait perdre ses rapports a la manette PS4.
// Branchement : GPIO 22 -> 1 k -> (10 nF vers la masse) -> 10 uF -> entree de l'ampli (voir docs/branchement-son.png).
#define SON_ACTIF 1
#define SND_PIN 22
#define SND_LEDC 6        // canal LEDC (timer 3) : libre (les dalles n'utilisent pas le LEDC)
#define SND_BITS 8        // rapport cyclique sur 8 bits : 128 = 50 % (le plus fort)
uint8_t volume = 6;       // 0 a 10 (moniteur serie : v N)
const Note *melody = nullptr;  // jingle en cours (notes jouees l'une apres l'autre)
uint8_t melodyLen = 0, melodyPos = 0;
uint32_t melodyNext = 0;
// son en cours (une seule voix ; un son plus important remplace un son mineur)
volatile float sF0 = 0, sF1 = 0, sVol = 0;
volatile uint32_t sStart = 0, sDur = 0;
volatile bool sNoise = false, sOn = false;
volatile uint8_t sPrio = 0;
portMUX_TYPE sndMux = portMUX_INITIALIZER_UNLOCKED;
bool sndReady = false;

void sndRaw(float f0, float f1, uint16_t ms, float vol, bool noise, uint8_t prio) {
  if (!SON_ACTIF || !sndReady || volume == 0) return;
  uint32_t now = millis();
  portENTER_CRITICAL(&sndMux);
  bool busy = sOn && now - sStart < sDur;
  if (!busy || prio >= sPrio) {
    sF0 = f0; sF1 = f1; sDur = ms; sVol = vol; sNoise = noise; sPrio = prio; sStart = now; sOn = true;
  }
  portEXIT_CRITICAL(&sndMux);
}
void sndPlay(float f0, float f1, uint16_t ms, float vol, bool noise, uint8_t prio) {
  if (!gDemo) sndRaw(f0, f1, ms, vol, noise, prio);  // demo muette
}
void sfxMenu() { sndRaw(1200, 1200, 35, 0.4f, false, 1); }

// toutes les ms : frequence et volume du son en cours
void sndTick(void *) {
  static bool silent = true;
  static uint32_t rnd = 12345;
  float f0, f1, vol;
  uint32_t start, dur;
  bool noise, on;
  portENTER_CRITICAL(&sndMux);
  f0 = sF0; f1 = sF1; vol = sVol; start = sStart; dur = sDur; noise = sNoise; on = sOn;
  portEXIT_CRITICAL(&sndMux);
  uint32_t t = millis() - start;
  if (!on || t >= dur || volume == 0) {
    if (!silent) { ledcWrite(SND_LEDC, 0); silent = true; }
    if (on && t >= dur) sOn = false;
    return;
  }
  float k = (float)t / dur;
  float f = f0 + (f1 - f0) * k;
  if (noise) {  // bruit : frequence tiree au hasard autour de la frequence du moment
    rnd = rnd * 1103515245 + 12345;
    f *= 0.35f + ((rnd >> 16) & 1023) / 760.0f;
  }
  f = constrain(f, 40.0f, 9000.0f);
  uint32_t duty = (uint32_t)(128 * vol * (volume / 10.0f) * (1 - 0.7f * k));  // decroissance douce
  ledcChangeFrequency(SND_LEDC, (uint32_t)f, SND_BITS);
  ledcWrite(SND_LEDC, constrain(duty, 1u, 128u));
  silent = false;
}

void sndMelody(const Note *m, uint8_t len) {
  if (gDemo || volume == 0) return;
  melody = m; melodyLen = len; melodyPos = 0; melodyNext = 0;
}
void sndUpdate() {  // notes des jingles (appele a chaque tour de boucle)
  if (!melody || millis() < melodyNext) return;
  if (melodyPos >= melodyLen) { melody = nullptr; return; }
  const Note &n = melody[melodyPos++];
  if (n.hz) sndPlay(n.hz, n.hz, n.ms * 9 / 10, 0.5f, false, 2);
  melodyNext = millis() + n.ms;
}
const Note JINGLE_STAGE[] = { { 523, 90 }, { 659, 90 }, { 784, 90 }, { 1047, 220 } };
const Note JINGLE_OVER[] = { { 523, 200 }, { 392, 200 }, { 330, 200 }, { 262, 450 } };
const Note JINGLE_ARME[] = { { 784, 70 }, { 988, 70 }, { 1175, 70 }, { 1568, 200 } };
void sfxShot() { sndPlay(1400, 500, 70, 0.35f, false, 1); }
void sfxBoom() { sndPlay(900, 120, 260, 0.8f, true, 2); }
void sfxHit() { sndPlay(700, 50, 700, 1.0f, true, 3); }
void sfxDive() { sndPlay(1100, 350, 450, 0.25f, false, 1); }

void audioBegin() {
#if !SON_ACTIF
  volume = 0;
  Serial.println("Son : desactive (SON_ACTIF 0)");
  return;
#endif
  volume = constrain(prefs.getUChar("vol", 6), 0, 10);
  ledcSetup(SND_LEDC, 1000, SND_BITS);
  ledcAttachPin(SND_PIN, SND_LEDC);
  ledcWrite(SND_LEDC, 0);
  esp_timer_create_args_t args = {};
  args.callback = sndTick;
  args.name = "son";
  esp_timer_handle_t tm;
  if (esp_timer_create(&args, &tm) != ESP_OK || esp_timer_start_periodic(tm, 1000) != ESP_OK) {
    Serial.println("Son : minuterie indisponible");
    return;
  }
  sndReady = true;
  Serial.printf("Son : PWM sur GPIO %d (ampli analogique), volume %u/10\n", SND_PIN, volume);
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
  size_t len = prefs.isKey("top") ? prefs.getBytesLength("top") : 0;
  if (len == sizeof(gBest)) {
    prefs.getBytes("top", gBest, sizeof(gBest));
  } else if (len == sizeof(HiScoreV1) * TOP_N) {  // ancien tableau sans noms : repris avec le nom "---"
    HiScoreV1 old[TOP_N];
    prefs.getBytes("top", old, sizeof(old));
    for (int i = 0; i < TOP_N; i++) setBest(gBest[i], old[i].score, old[i].stage, old[i].diff, "---");
  } else {  // 1er lancement : ancien record seul
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
  prefs.putBytes("top", gBest, sizeof(gBest));
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
  // vies en haut a droite : une icone + le nombre (X3)
  snprintf(sc, sizeof(sc), "X%d", max(0, gLives));
  int lw = tinyWidth(sc), lx = W - 1 - lw;
  tinyText(*gfx, lx, 1, sc, white);
  gMark(lx, 1, lw, 5);
  gSprite(SPR_LIFE, 5, 4, lx - 7, 1, palFull, 0, 0);
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
    gText(y, "MEILLEURS SCORES", sel);
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
  int y = tall ? H / 2 - 22 : 12, step = tall ? 13 : 9;
  gBox(y - 6, tall ? y + MENU_N * step + 16 : y + (MENU_N - 1) * step + 12);
  const char *items[MENU_N] = { "JOUER", nullptr, nullptr, "SCORES", "DEMO" };
  for (int i = 0; i < MENU_N; i++) {
    const char *t = items[i];
    if (i == 1) { snprintf(b, sizeof(b), "NIVEAU %s%s%s", i == menuSel ? "<" : "", DIFF_NAME[gDiff], i == menuSel ? ">" : ""); t = b; }
    if (i == 2) { snprintf(b, sizeof(b), "LUMINOSITE %s%d%%%s", i == menuSel ? "<" : "", brightPct(), i == menuSel ? ">" : ""); t = b; }
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
    snprintf(b, sizeof(b), "MONDE %s", WORLD_NAME[gWorld]);
    gText(y + 11, b, rgb(255, 60, 200));
    int yy = y + 22;
    if (gBonus && H >= 128) { snprintf(b, sizeof(b), "BONUS %lu", (unsigned long)gBonus); gText(yy, b, rgb(230, 230, 230)); yy += 11; }
    if (gWave <= 4 && TIER_NAME[gTier()][0]) { gText(yy, TIER_NAME[gTier()], rgb(40, 230, 80)); yy += 11; }
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

void startGame() {
  gDemo = false;
  menuScores = false;
  gfx->fillScreen(0);
  gNOld = 0;
  gReset();
}
void toDemo() {
  gDemo = true;
  gAttract = false;  // fin de partie : le menu d'abord, la demo seule apres 30 s
  lastInput = millis();
  gfx->fillScreen(0);
  gNOld = 0;
  gReset();
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
    prefs.putBytes("top", gBest, sizeof(gBest));
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
    gameDraw();
    gOverlay();
    return;
  }
  if (!gDemo && now - lastInput > IDLE_TO_DEMO_MS) toDemo();
  if (gDemo && !gAttract && now - lastInput > MENU_IDLE_MS) { gAttract = true; menuScores = false; }  // menu sans activite
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
  gameStep();
  gameDraw();
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
  if (PS4.Cross() || PS4.Circle() || PS4.Square() || PS4.Triangle() || PS4.R1() || PS4.L1() || PS4.R2Value() > 60) k |= 4;
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
  if (menuSel == 1 && (edge & (4 | 8 | 16))) {
    gDiff = (gDiff + ((edge & 4) ? 2 : 1)) % 3;
    prefs.putUChar("diff", gDiff);
    sfxMenu();
  }
  if (edge & 16) {
    if (menuSel == 0) startReq = true;
    else if (menuSel == 3) { menuScores = true; sfxMenu(); }
    else if (menuSel == 4) { gAttract = true; menuSel = 0; sfxMenu(); }  // ligne DEMO
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
  ps4Begin();
  audioBegin();
  serialMenu();

  gameSetup();
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
