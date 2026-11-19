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
#include <Bluepad32.h>  // manettes Bluetooth (PS4, PS5, Xbox...) : jusqu'a 4 en meme temps (carte "ESP32 + Bluepad32")
#include <esp_timer.h>
#include "game_types.h"
extern "C" void BTM_SetDefaultLinkPolicy(uint16_t settings);  // pile Bluetooth (Bluedroid) : sans mode veille

#define GAMES_VERSION "3.93"

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
int8_t padStickX = 0;               // stick gauche horizontal (-127 gauche .. 127 droite), BRIQUES : vitesse proportionnelle
int8_t padStickY = 0;               // stick gauche vertical (-127 bas .. 127 haut), RAQUETTES : vitesse proportionnelle
bool padDigital = false;            // la croix numerique est appuyee (ORBITE : gauche / droite = tourner en continu)
uint8_t keysP2 = 0;                 // touches de la 2e manette (memes bits), pour les jeux a deux joueurs
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
#define MENU_N 7
uint8_t gPlayers = 1;  // 1 ou 2 joueurs (menu) : seuls les jeux a deux joueurs utilisent la 2e manette
int brightPct() { return (bright * 100 + 127) / 255 / 10 * 10; }  // luminosite en %, arrondie a 10 %
void setBrightPct(int p) {  // 0 % : tres faible mais jamais noir (le menu reste visible)
  p = constrain(p, 0, 100);
  bright = p == 0 ? 3 : p * 255 / 100;
  dma->setBrightness8(bright);
  prefs.putUChar("bright", bright);
}
bool gAttractHold = false;         // demo lancee depuis le menu : on attend que les boutons soient relaches
bool gAttract = false;              // mode demo : menu cache (ligne DEMO, ou 30 s sans toucher la manette)
#define MENU_IDLE_MS 30000UL        // menu sans activite : passage en mode demo
bool menuScores = false;            // page des meilleurs scores affichee
// numeros des jeux (ordre du menu)
enum GameId : uint8_t { G_SPACE, G_RAID, G_RAQ, G_BUL, G_LAB, G_ORB, G_BRI, G_BLO, G_CRU, G_TAU, G_BIL, G_ESQ, G_BUN };
#define N_GAMES 13
// ===== NOMS DES JEUX : a modifier ici, ils s'affichent dans le menu, les scores et la demo =====
// Majuscules, sans accents (la police n'en a pas), 14 caracteres au plus. Garder l'ordre : il suit la liste GameId ci-dessus.
// Au-dela de 10 caracteres, le menu affiche le nom seul (sans le mot JOUER devant).
const char *const GAME_NAME[N_GAMES] = {
  "GALAGA",  // G_SPACE
  "SHOOTME",            // G_RAID
  "PONG",       // G_RAQ
  "BULLES",          // G_BUL
  "PAC MAN",      // G_LAB
  "ORBITUS",         // G_ORB
  "CASSE BRIQUES",   // G_BRI
  "TETRIS",           // G_BLO
  "CANDY CRUCH",           // G_CRU
  "DIG DUG",           // G_TAU
  "BILLES",          // G_BIL
  "1945",      // G_ESQ
  "BUNKER 3D",       // G_BUN
};
uint8_t gGame = 0;                  // jeu choisi dans le menu (gauche / droite sur la ligne JOUER)
const char *topKey() { static const char *const K[N_GAMES] = { "top", "top1", "top3", "top4", "top5", "top7", "top8", "top9", "top10", "top11", "top12", "top14", "top15" }; return K[gGame % N_GAMES]; }  // top2 (COURSE), top6 (SAUTERELLE), top13 (ESCALADE) : jeux retires
#define TOP_N 5
HiScore gBest[TOP_N];                // meilleurs scores (gardes en memoire)
uint8_t gLastRank = 0;              // place obtenue a la derniere partie (0 : hors tableau)
bool gNameEntry = false;            // saisie du nom apres un score qui entre au tableau
char gName[4] = "AAA";              // nom en cours de saisie (3 lettres)
uint8_t gNamePos = 0;               // lettre en cours (0 a 2)
const char NAME_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
#define IDLE_TO_DEMO_MS 60000UL     // partie abandonnee : retour a la demo apres 1 min sans appui
char ipText[20] = "";  // adresse Bluetooth de l'ESP32 (moniteur serie)
ControllerPtr gPads[BP32_MAX_GAMEPADS];  // manettes connectees : [0] joueur 1, [1] joueur 2...
// Adaptation : les jeux gardent PS4.Cross(), PS4.LStickX()... qui lisent la manette du joueur 1 (gPads[0]).
// Croix (PS) = BUTTON_A, rond = B, carre = X, triangle = Y ; Share = SELECT, Options = START ; sticks -512..511 (Y vers le bas).
struct PadShim {
  uint8_t idx;
  uint8_t ledR = 0, ledG = 0, ledB = 40, rumW = 0, rumS = 0;
  explicit PadShim(uint8_t i) : idx(i) {}
  ControllerPtr c() const { ControllerPtr p = gPads[idx]; return (p && p->isConnected()) ? p : nullptr; }
  bool isConnected() const { return c() != nullptr; }
  uint16_t btn() const { ControllerPtr p = c(); return p ? p->buttons() : 0; }
  uint8_t dp() const { ControllerPtr p = c(); return p ? p->dpad() : 0; }
  uint16_t misc() const { ControllerPtr p = c(); return p ? p->miscButtons() : 0; }
  bool Up() const { return (dp() & (DPAD_UP | DPAD_LEFT | DPAD_RIGHT)) == DPAD_UP; }
  bool Down() const { return (dp() & (DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT)) == DPAD_DOWN; }
  bool Left() const { return (dp() & (DPAD_LEFT | DPAD_UP | DPAD_DOWN)) == DPAD_LEFT; }
  bool Right() const { return (dp() & (DPAD_RIGHT | DPAD_UP | DPAD_DOWN)) == DPAD_RIGHT; }
  bool UpLeft() const { return (dp() & (DPAD_UP | DPAD_LEFT)) == (DPAD_UP | DPAD_LEFT); }
  bool UpRight() const { return (dp() & (DPAD_UP | DPAD_RIGHT)) == (DPAD_UP | DPAD_RIGHT); }
  bool DownLeft() const { return (dp() & (DPAD_DOWN | DPAD_LEFT)) == (DPAD_DOWN | DPAD_LEFT); }
  bool DownRight() const { return (dp() & (DPAD_DOWN | DPAD_RIGHT)) == (DPAD_DOWN | DPAD_RIGHT); }
  bool Cross() const { return btn() & BUTTON_A; }
  bool Circle() const { return btn() & BUTTON_B; }
  bool Square() const { return btn() & BUTTON_X; }
  bool Triangle() const { return btn() & BUTTON_Y; }
  bool L1() const { return btn() & BUTTON_SHOULDER_L; }
  bool R1() const { return btn() & BUTTON_SHOULDER_R; }
  int R2Value() const { ControllerPtr p = c(); return p ? p->throttle() / 4 : 0; }
  int L2Value() const { ControllerPtr p = c(); return p ? p->brake() / 4 : 0; }
  int RStickX() const { ControllerPtr p = c(); return p ? constrain(p->axisRX() / 4, -128, 127) : 0; }
  bool Share() const { return misc() & MISC_BUTTON_SELECT; }
  bool Options() const { return misc() & MISC_BUTTON_START; }
  int LStickX() const { ControllerPtr p = c(); return p ? constrain(p->axisX() / 4, -128, 127) : 0; }
  int LStickY() const { ControllerPtr p = c(); return p ? constrain(-p->axisY() / 4, -128, 127) : 0; }  // vers le haut : positif
  uint8_t Battery() const { ControllerPtr p = c(); return p && p->battery() ? (p->battery() * 10 + 127) / 255 : 0; }  // 0 a 10
  bool Charging() const { return false; }  // pas donne par Bluepad32
  void setLed(uint8_t r, uint8_t g, uint8_t b) { ledR = r; ledG = g; ledB = b; }
  void setRumble(uint8_t w, uint8_t s) { rumW = w; rumS = s; }
  void sendToController() {
    ControllerPtr p = c();
    if (!p) return;
    p->setColorLED(ledR, ledG, ledB);
    if (rumW || rumS) p->playDualRumble(0, 250, rumW ? rumW : 0x40, rumS);
  }
};
PadShim PS4(0), PAD2(1);
char padToast[20] = "";      // message sur la dalle : MANETTE 2 OK...
uint32_t padToastUntil = 0;
bool rumbleReq2 = false;  // le vaisseau du joueur 2 est touche : sa manette vibre
int8_t padScanWant = -1;  // nouvelles connexions a activer (1) ou couper (0) dans ps4Poll, -1 : rien a faire
uint32_t padScanAt = 0;
void onPadConnected(ControllerPtr c) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    if (!gPads[i]) {
      gPads[i] = c;
      c->setColorLED(i == 0 ? 0 : 255, i == 0 ? 0 : 30, i == 0 ? 40 : 30);
      Serial.printf("Manette %d connectee : %s, memoire libre %u\n", i + 1, c->getModelName().c_str(), ESP.getFreeHeap());
      snprintf(padToast, sizeof(padToast), "MANETTE %d OK", i + 1);
      padToastUntil = millis() + 3000;
      pairStop("manette connectee");
      return;
    }
}
void onPadDisconnected(ControllerPtr c) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    if (gPads[i] == c) {
      gPads[i] = nullptr;
      Serial.printf("Manette %d deconnectee\n", i + 1);
      snprintf(padToast, sizeof(padToast), "MANETTE %d PARTIE", i + 1);
      padToastUntil = millis() + 3000;
    }
  if (!gPads[0])  // le joueur 1 est parti : le joueur 2 prend sa place
    for (int i = 1; i < BP32_MAX_GAMEPADS; i++)
      if (gPads[i]) { gPads[0] = gPads[i]; gPads[i] = nullptr; break; }
}

// ---------------------------------------------------------------- petite police 3x5 (score)
const uint8_t TF_DIG[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
                                { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
const uint8_t TF_R[5] = { 6, 5, 6, 5, 5 }, TF_E[5] = { 7, 4, 6, 4, 7 }, TF_S[5] = { 7, 4, 7, 1, 7 },
              TF_C[5] = { 7, 4, 4, 4, 7 }, TF_D[5] = { 6, 5, 5, 5, 6 }, TF_A[5] = { 2, 5, 7, 5, 5 },
              TF_B[5] = { 6, 5, 6, 5, 6 }, TF_F[5] = { 7, 4, 6, 4, 4 }, TF_M[5] = { 5, 7, 7, 5, 5 },
              TF_N[5] = { 6, 5, 5, 5, 5 }, TF_T[5] = { 7, 2, 2, 2, 2 }, TF_U[5] = { 5, 5, 5, 5, 7 },
              TF_P[5] = { 6, 5, 6, 4, 4 }, TF_COL[5] = { 0, 2, 0, 2, 0 }, TF_X[5] = { 5, 5, 2, 5, 5 },
              TF_G[5] = { 7, 4, 5, 5, 7 }, TF_PCT[5] = { 5, 1, 2, 4, 5 }, TF_L[5] = { 4, 4, 4, 4, 7 }, TF_I[5] = { 7, 2, 2, 2, 7 }, TF_V[5] = { 5, 5, 5, 5, 2 },
              TF_Q[5] = { 7, 5, 5, 6, 3 }, TF_H[5] = { 5, 5, 7, 5, 5 }, TF_K[5] = { 5, 5, 6, 5, 5 }, TF_Y[5] = { 5, 5, 2, 2, 2 },
              TF_W[5] = { 5, 5, 7, 7, 5 }, TF_J[5] = { 1, 1, 1, 5, 7 }, TF_Z[5] = { 7, 1, 2, 4, 7 }, TF_EXC[5] = { 2, 2, 2, 0, 2 },
              TF_PLUS[5] = { 0, 2, 7, 2, 0 }, TF_PO[5] = { 1, 2, 2, 2, 1 }, TF_PF[5] = { 4, 2, 2, 2, 4 };
const uint8_t *tfGlyph(char c) {
  if (c >= '0' && c <= '9') return TF_DIG[c - '0'];
  switch (c) {
    case 'R': return TF_R; case 'E': return TF_E; case 'S': return TF_S; case 'C': return TF_C;
    case 'O': return TF_DIG[0]; case 'D': return TF_D; case 'A': return TF_A; case 'B': return TF_B;
    case 'F': return TF_F; case 'M': return TF_M; case 'N': return TF_N; case 'T': return TF_T;
    case 'U': return TF_U; case 'P': return TF_P; case ':': return TF_COL; case 'X': return TF_X; case 'G': return TF_G; case '%': return TF_PCT; case 'L': return TF_L; case 'I': return TF_I; case 'V': return TF_V;
    case 'Q': return TF_Q; case 'H': return TF_H; case 'K': return TF_K; case 'Y': return TF_Y; case 'W': return TF_W; case 'J': return TF_J;
    case 'Z': return TF_Z; case '!': return TF_EXC; case '+': return TF_PLUS; case '(': return TF_PO; case ')': return TF_PF;
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
void sfxHit() {  // ton vaisseau explose : long grondement grave + sirene qui chute (bien distinct d'un ennemi)
  sndPlay(1600, 40, 1200, 1.0f, true, 3);
  sndPlay(1000, 70, 950, 0.85f, false, 3);
}
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
// ecran d'arcade : SCORE centre et vies en haut (le record est au menu et sur la page des scores).
// Sans joueur, un pilote automatique joue (demo). Les ennemis pulsent doucement.
// Dessin sans scintillement : seuls les rectangles dessines a l'image precedente sont effaces.
#define G_MAXE 100
#define G_SHOTS 14  // tirs a l'ecran (deux vaisseaux en mode 2 joueurs)
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
// ---- memoire des jeux : chaque jeu (sauf le 1er, dont les etoiles servent aussi au menu) range ses tableaux dans un bloc
// reserve seulement quand il tourne (partie ou demo) et rendu des qu'on passe a un autre jeu : le Bluetooth garde la memoire.
void *gMem = nullptr;     // bloc du jeu en cours
int8_t gMemGame = -1;     // jeu a qui appartient le bloc
#define GMEM(T) ((T *)gMem)
GRect gOld[G_RECTS];
int gNE = 0, gCols = 4, gRows = 3, gTop = 13, gPY = 49, gNStar = 0, gNOld = 0, gLives = 0, gWave = 0, gDead = 0, gCool = 0;
uint32_t gFrame = 0, gScore = 0, gHi = 0, gLastStep = 0;
float gPx = 0, gSway = 0;
int8_t gRowType[G_MAXROWS];

// sprites : '.' transparent, sinon une lettre de la palette ci-dessous
// 4 mondes (un par stage, en boucle) : 3 types d'ennemis x 2 images (9x7), sprites originaux
// type 0 : rang du haut (2 tirs), types 1 et 2 : rangs suivants
const char *const SPR_W[4][3][2][GH] = {  // sprites originaux (multicolores)
  {  // nebuleuse : meduse, papillon, guepe
    { { "..ccccc..", ".cpcwcpc.", "ccccccccc", ".v.v.v.v.", ".v.v.v.v.", "v..v.v..v", "...v.v..." },
      { "..ccccc..", ".cpcwcpc.", "ccccccccc", ".v.v.v.v.", "v..v.v..v", ".v.v.v.v.", "v.......v" } },
    { { "..b...b..", "vvv.b.vvv", "vyvvbvvyv", "vvvvcvvvv", ".vv.b.vv.", "..v.p.v..", "........." },
      { "..b...b..", ".vv.b.vv.", ".vyvbvyv.", "..vvcvv..", "..v.b.v..", "....p....", "........." } },
    { { "cc.....cc", "ccc.g.ccc", ".cggwggc.", "..gyyyg..", "..ggggg..", "..gyyyg..", "...g.g..." },
      { ".........", "ccc.g.ccc", "cccgwgccc", "..gyyyg..", "..ggggg..", "..gyyyg..", "..g...g.." } },
  },
  {  // crabes : crabe, mante, araignee
    { { "o.......o", "oo.w.w.oo", ".ooooooo.", "..oyoyo..", ".ooooooo.", "o.o...o.o", "..o...o.." },
      { "oo.....oo", ".o.w.w.o.", ".ooooooo.", "..oyoyo..", ".ooooooo.", ".o.o.o.o.", "o.......o" } },
    { { "r.......r", "rr..r..rr", ".rrrrrrr.", "..rcrcr..", "...rrr...", "..y.r.y..", ".y.....y." },
      { ".r.....r.", "rr..r..rr", "rrrrrrrrr", "..rcrcr..", "...rrr...", "...yry...", "..y...y.." } },
    { { "o.......o", ".o.bbb.o.", "..bbcbb..", "obbbwbbbo", "..bbbbb..", ".o.b.b.o.", "o.......o" },
      { ".o.....o.", "o..bbb..o", "..bbcbb..", ".obbwbbo.", "o.bbbbb.o", "..o...o..", "........." } },
  },
  {  // cristaux : fleur de cristal, soucoupe, raie
    { { "....o....", "y..ooo..y", ".oyooooy.", "ooobcbooo", ".oyooooy.", "y..ooo..y", "....o...." },
      { "y...o...y", ".y.ooo.y.", "..ooooo..", "ooobcbooo", "..ooooo..", ".y.ooo.y.", "y...o...y" } },
    { { "...ccc...", "..cwccc..", "..ppppp..", "yyyyyyyyy", ".ypppppy.", "..o.o.o..", "........." },
      { "...ccc...", "..cwccc..", "..ppppp..", "yyyyyyyyy", ".ypppppy.", "...o.o...", "........." } },
    { { "....b....", "...bbb...", "b.bbybb.b", "bbbbbbbbb", ".bby.ybb.", "..b...b..", "....b...." },
      { "....b....", "...bbb...", "..bbybb..", "bbbbbbbbb", "b.by.yb.b", "...b.b...", "....b...." } },
  },
  {  // orbes : orbe, araignee a cornes, insecte
    { { "..rrrrr..", ".rrwwwrr.", "rrwwbwwrr", "rrwbbbwrr", "rrwwbwwrr", ".rrwwwrr.", "..rrrrr.." },
      { "..rrrrr..", ".rrrrrrr.", "rrwwwwwrr", "rrwbbbwrr", "rrwwwwwrr", ".rrrrrrr.", "..rrrrr.." } },
    { { "v.......v", ".v.ppp.v.", "..ppppp..", ".pcpppcp.", "..ppppp..", ".y.y.y.y.", "y.......y" },
      { ".v.....v.", "v..ppp..v", "..ppppp..", ".pcpppcp.", "..ppppp..", "y.y...y.y", "........." } },
    { { "c.......c", ".c.ooo.c.", "..cowoc..", ".ccccccc.", "c.ccccc.c", "..c.c.c..", ".c.....c." },
      { ".c.....c.", "c..ooo..c", "..cowoc..", "ccccccccc", "..ccccc..", ".c..c..c.", "c.......c" } },
  },
};
// boss d'ESSAIM (tous les 4 stages, apres la formation) : vaisseau-mere original 17x9 ; detruit, il eclate en 5 petits vaisseaux
const char *const SPR_BOSS[9] = {
  "......ppppp......", "....ppwwwwwpp....", "..pppwwcccwwppp..", ".ppbbbbbbbbbbbpp.", "ppbwbwbwbwbwbwbpp",
  "ppbbbbbbbbbbbbbpp", ".pp.ooo.o.ooo.pp.", "..p..o..o..o..p..", ".....y..y..y....." };
bool gBossOn = false, gBossDone = false;  // boss en cours / deja vaincu ce stage
float gBX = 0, gBY = -12;
int gBHp = 0, gBHpMax = 1, gBFlash = 0, gBT = 0;
const char *const WORLD_NAME[4] = { "NEBULEUSE", "CRABES", "CRISTAUX", "ORBES" };
const char WORLD_HURT[4][2] = { { 'c', 'o' }, { 'o', 'r' }, { 'o', 'p' }, { 'r', 'y' } };  // couleur changee quand l'ennemi est touche
int gWorld = 0;  // monde du stage : 0 nebuleuse (piques en boucle), 1 crabes (piques a deux, balancement large),
                 // 2 cristaux (plus resistants, piques en zigzag), 3 orbes (formation qui descend, tirs vises)
int gBaseHp(int type) { return (type == 0 ? 2 : 1) + (gWorld == 2 ? 1 : 0); }
// vaisseau du joueur : il evolue avec les stages (1 base, 2 tir rapide, 3 double canon, 4+ triple canon)
const char *const SPR_SHIP[4][8] = {
  { "....w....", "...wbw...", "...wbw...", "..rwwwr..", ".rwwbwwr.", "rwwwwwwwr", "r.ro.or.r", "...o.o..." },
  { "....w....", "...wbw...", "...wbw...", "..gwwwg..", ".gwwbwwg.", "gwwwwwwwg", "g.go.og.g", "...o.o..." },
  { "..c...c..", "..c.c.c..", ".cc.w.cc.", ".cbcwcbc.", "bbbcccbbb", "bybcbcbyb", "b.o...o.b", "..o...o.." },
  { "c...c...c", "c..ccc..c", "cc.cwc.cc", "rbbcwcbbr", "rbbcccbbr", "bbcbcbcbb", "b.oy.yo.b", "..o...o.." }
};
int gStageT = 0;
bool gLifeUp = false;     // vie gagnee a la fin du stage (affichee avec l'annonce)          // images restantes de l'annonce "STAGE n"
uint32_t gBonus = 0;      // bonus de fin de stage affiche avec l'annonce
int gTier() { return min(gWave, 4) - 1; }
const char *const SPR_LIFE_CREATURE[4] = { "p.p.p", ".pwp.", "ppppp", "p.p.p" };  // LABYRINTHE : creature piquante (couleur par icone)
const char *const SPR_LIFE_BALL[4] = { ".ww..", "wwww.", "wwww.", ".ww.." };      // CASSE BRIQUES : une balle blanche
const char *const SPR_LIFE_FRUIT[4] = { "..g..", ".rrr.", "rwrrr", ".rrr." };     // CRUCH : une pomme
const char *const SPR_LIFE_BLOB[4] = { ".ccc.", "cwcwc", "ccccc", "c.c.c" };    // BULLES : le blob
const char *const SPR_LIFE[4] = { "..c..", ".ccc.", "bcwcb", "b...b" };
const char *const SPR_LIFE_PLANE[4] = { "..b..", "bwbwb", "..b..", ".bbb." };  // ESCADRILLE : ton avion
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
  gCols = constrain((W - 6) / 11 - 1, 4, 20);  // une colonne de moins que la place : l'essaim se balance plus
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
  gBossOn = false; gBossDone = false;
  gStageT = 100;  // annonce pendant 2,5 s : les ennemis commencent leur arrivee sans tirer
  if (gWave >= 2 && gWave <= 4) sndMelody(JINGLE_ARME, 4);  // nouvelle arme
  else sndMelody(JINGLE_STAGE, 4);
}
int gInv = 0, g2Inv = 0;  // nouveau vaisseau : invincible (il clignote) pendant 4 s
bool g2On = false;    // SPACE DEFENDER a deux : un 2e vaisseau (vert), vies et score communs
float g2Px = 0;
int g2PY = 0, g2Cool = 0, g2Dead = 0;
void gReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;  // facile : 5 vies
  gPaused = false;
  gScore = 0; gWave = 0; gDead = 0; gCool = 0; gBonus = 0; gLifeUp = false; gInv = 0; g2Inv = 0;
  gPx = W / 2 - 4;
  if (gPYBase) gPY = gPYBase;
  g2On = !gDemo && gPlayers == 2 && PAD2.isConnected();
  if (g2On) { gPx = W / 3 - 4; g2Px = W * 2 / 3 - 4; g2PY = gPY; g2Cool = 0; g2Dead = 0; }
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
bool gFireAt(float px, int py, int &cool);
bool gFire() { return gFireAt(gPx, gPY, gCool); }
bool gFireAt(float px, int py, int &cool) {
  static const int8_t N[4] = { 1, 1, 2, 3 }, COOL[4] = { 8, 5, 6, 7 };
  int t = gTier(), n = N[t], freeShots = 0;
  for (auto &s : gShot) freeShots += !s.on;
  if (freeShots < n) return false;
  for (int k = 0; k < n; k++) {
    float x = px + 4, vx = 0;
    if (n == 2) x = px + (k ? 6 : 2);          // double canon : 2 tirs paralleles
    if (n == 3) vx = (k - 1) * 0.5f;            // triple canon : eventail
    for (auto &s : gShot)
      if (!s.on) { s = { x, (float)py - 3, true, vx }; break; }
  }
  cool = COOL[t];
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
void gHitPlayer2() {  // vaisseau du joueur 2 touche : vies communes
  rumbleReq2 = true;
  sfxHit();
  if (gLives == 1) sndMelody(JINGLE_OVER, 4);
  gBoomAt(g2Px + 4, g2PY + 3);
  gLives--;
  g2Dead = 50;
  g2PY = gPYBase;
  if (gLives <= 0) { gDead = 120; topAdd(); }  // plus de vies : fin de partie pour les deux
}
void gPlayer2Control() {
  uint8_t k = keysP2;
  float sp = W > 128 ? 3 : 2;
  if ((k & 1) && !(k & 2)) g2Px -= sp;
  if ((k & 2) && !(k & 1)) g2Px += sp;
  g2Px = constrain(g2Px, 0.0f, (float)(W - GW));
  if ((k & 8) && !(k & 16)) g2PY = max(gPYMin, g2PY - 2);
  if ((k & 16) && !(k & 8)) g2PY = min(gPYBase, g2PY + 2);
  if (g2Cool > 0) g2Cool--;
  if ((k & 4) && g2Cool == 0) gFireAt(g2Px, g2PY, g2Cool);
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
// boss : va et vient en haut, tire en eventail (plus vite et plus large en 2e phase)
void gBossStep() {
  gBT++;
  if (gBFlash) gBFlash--;
  float ty = gTop + 1 + 3 * sinf(gBT * 0.05f);
  gBY += (gBY < ty - 0.5f) ? 0.5f : ty - gBY;
  gBX = W / 2 - 8 + sinf(gBT * 0.02f) * (W / 2 - 12);
  if (gDead || gStageT || gBY < gTop - 1) return;
  bool p2 = gBHp * 2 < gBHpMax;
  int every = max(30, (p2 ? 50 : 70) - 3 * (gWave / 4) - 10 * (gEffDiff() - 1));  // boss : salves plus espacees (V3.93)
  if (gBT % every == 0) {
    int n = p2 ? 5 : 3;
    for (int k = 0; k < n; k++)
      for (auto &b : gBomb)
        if (!b.on) { b = { gBX + 8, gBY + 9, true, (k - (n - 1) / 2.0f) * 0.45f }; break; }
  }
}
void gBossKill() {  // explosion geante, gros bonus, et 5 petits vaisseaux qui plongent aussitot
  gBossOn = false;
  gScore += 2000UL * (gWave / 4);
  if (!gDemo && gScore > gHi) gHi = gScore;
  for (int k = 0; k < 5; k++) gBoomAt(gBX + 2 + k * 3, gBY + 2 + (k & 1) * 4);
  sfxHit();
  if (!gDemo) rumbleReq = true;
  gNE = 5;
  int r = gRows - 1;
  for (int k = 0; k < 5; k++) {
    GEnemy &e = gEn[k];
    e = {};
    e.row = r; e.col = constrain(gCols / 2 - 2 + k, 0, gCols - 1); e.type = gRowType[r];
    e.alive = true; e.hp = 1; e.path = 9;  // eclat : petit vaisseau qui replonge
    e.x = gBX + 4 + (k - 2) * 3; e.y = gBY + 2;
    e.state = 1; e.vx = (k - 2) * 0.8f; e.vy = -0.8f; e.t = -k * 4;
    e.dir = k < 2 ? -1 : k > 2 ? 1 : (random(2) ? 1 : -1);
  }
}
// une meduse detruite libere 4 petits vaisseaux qui plongent aussitot (marques path 9 : ils replongent sans fin)
void gSplit(float x, float y) {
  int r = gRows - 1;
  for (int k = 0; k < 4; k++) {
    int i = 0;
    while (i < gNE && gEn[i].alive) i++;  // une place libre (ennemi deja detruit) ou une nouvelle
    if (i >= G_MAXE) return;
    if (i == gNE) gNE++;
    GEnemy &e = gEn[i];
    e = {};
    e.row = r; e.col = 0; e.type = gRowType[r];
    e.alive = true; e.hp = 1; e.path = 9;
    e.x = x + (k - 1.5f) * 3; e.y = y;
    e.state = 1; e.vx = (k - 1.5f) * 0.9f; e.vy = -0.7f; e.t = -k * 3;
    e.dir = k < 2 ? -1 : 1;
  }
}
void gameStep() {
  gFrame++;
  if (gInv > 0) gInv--;
  if (g2Inv > 0) g2Inv--;
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
  int diveEvery = max(24, 70 - min(gWave, 12) * 4);  // piques : la frequence monte doucement et plafonne (V3.93)
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
  float shipC = gPx + 4;  // vaisseau du joueur 1 (pilote automatique de la demo)
  bool p2 = g2On && g2Dead == 0 && gLives > 0;
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    float shipC = (p2 && ((i & 1) || gDead)) ? g2Px + 4 : gPx + 4;  // NOLINT a deux : un ennemi sur deux vise le joueur 2
    float sx = gSlotX(e.col), sy = gSlotY(e.row);
    if (e.state == 0) {
      e.x = sx; e.y = sy;
    } else if (e.state == 1) {  // pique en courbe : boucle puis descente en visant le vaisseau
      e.t++;
      if (e.t < 0) continue;  // depart decale (pique a deux)
      float curve = gWorld == 2 ? e.dir * 1.6f * sinf(e.t * 0.15f)                   // cristaux : zigzag
                                : e.t < 50 ? e.dir * 1.2f * cosf(e.t * 0.063f) : 0;  // boucle
      e.vx = constrain(e.vx * 0.96f + curve * 0.06f + (shipC - (e.x + 4)) * (e.t < 50 ? 0.0005f : 0.004f), -1.4f, 1.4f);
      e.vy = min(e.vy + 0.03f, 1.2f + 0.05f * min(gWave, 12));
      e.x += e.vx + curve; e.y += e.vy;
      if (e.y > H) {
        if (e.path == 9) { e.y = -8; e.x = random(W - GW); e.vx = 0; e.vy = 0.6f; e.t = 50; }  // eclat : replonge du haut
        else { e.y = -8; e.state = 2; }
      }
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
    int chance = e.state == 1 ? 12 : e.state == 3 ? 3 : 1 + min(gWave, 12) / 2;  // tirs ennemis (moins nombreux, plafonnes)
    if (gEffDiff() == 0) chance = max(1, chance / 2);
    else if (gEffDiff() == 2) chance = chance * 8 / 5;
    if (gDead == 0 && gStageT == 0 && e.y > 0 && e.y < H * 0.6f && random(1000) < chance)
      for (auto &b : gBomb)
        if (!b.on) {
          b = { e.x + 4, e.y + GH, true, 0 };
          if (gWorld == 3) b.vx = constrain((shipC - b.x) / max(20.0f, gPY - b.y) * 1.4f, -0.9f, 0.9f);  // orbes : tir vise
          break;
        }
    if (gDead == 0 && gInv == 0 && e.state == 1 && e.x + GW - 1 >= gPx && e.x <= gPx + 8 && e.y + GH - 1 >= gPY && e.y <= gPY + 7) {
      e.alive = false;  // collision avec le vaisseau
      gBoomAt(e.x + 4, e.y + 3);
      gHitPlayer();
    } else if (p2 && g2Inv == 0 && e.state == 1 && e.x + GW - 1 >= g2Px && e.x <= g2Px + 8 && e.y + GH - 1 >= g2PY && e.y <= g2PY + 7) {
      e.alive = false;
      gBoomAt(e.x + 4, e.y + 3);
      gHitPlayer2();
    }
  }
  if (g2On && gLives > 0) {  // joueur 2 : commandes, ou attente avant de revenir
    if (g2Dead > 0) { if (--g2Dead == 0) { g2Px = W * 2 / 3 - 4; g2Inv = 160; } }
    else gPlayer2Control();
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
      else if (gBossOn) target = gBX + 4 + 3 * sinf(gFrame * 0.1f);
    }
    if (fabsf(target - gPx) >= 1) gPx += constrain(target - gPx, -1.5f, 1.5f);  // pas de micro-mouvement
    gPx = constrain(gPx, 0.0f, (float)(W - GW));
    if (gCool > 0) gCool--;
    if (gCool == 0 && gBossOn && fabsf(gBX + 8 - shipC) < 7) gFire();
    if (gCool == 0)  // tir quand un ennemi est au-dessus
      for (int i = 0; i < gNE; i++)
        if (gEn[i].alive && gEn[i].y > 0 && fabsf(gEn[i].x - gPx) < 4) {
          gFire();
          break;
        }
  } else if (--gDead == 0 && gLives > 0) {
    gInv = 160;  // le nouveau vaisseau est invincible 4 s
  } else if (gDead == 0 && gLives <= 0) {
    if (gDemo) gReset();  // demo : on recommence
    else if (gLastRank) startNameEntry();  // score au tableau : saisie du nom
    else toDemo();        // fin de partie du joueur : retour a la demo
  }
  for (auto &s : gShot) {  // tirs du vaisseau
    if (!s.on) continue;
    s.y -= 3;
    s.x += s.vx;
    if (s.y < 0 || s.x < 0 || s.x >= W) { s.on = false; continue; }
    if (gBossOn && gBY > 0 && s.x >= gBX + 1 && s.x <= gBX + 15 && s.y <= gBY + 8 && s.y + 2 >= gBY) {
      s.on = false;
      gBFlash = 3;
      gScore += 10;
      if (--gBHp <= 0) gBossKill();
      else sndPlay(900, 500, 40, 0.25f, true, 1);
      continue;
    }
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
        if (e.type == 0 && e.path != 9) gSplit(e.x, e.y);  // les plus rares (rangee du haut) eclatent en 4 petits vaisseaux
        break;
      }
    }
  }
  for (auto &b : gBomb) {  // tirs ennemis
    if (!b.on) continue;
    b.y += 1.1f + 0.05f * min(gWave, 12) + (gEffDiff() - 1) * 0.3f;  // tirs plus lents en facile, plus rapides en difficile
    b.x += b.vx;
    if (b.y >= H || b.x < 0 || b.x >= W) { b.on = false; continue; }
    if (gDead == 0 && gInv == 0 && b.x >= gPx && b.x <= gPx + 8 && b.y + 2 >= gPY + 2 && b.y <= gPY + 7) { b.on = false; gHitPlayer(); }
    else if (g2On && g2Dead == 0 && g2Inv == 0 && gLives > 0 && b.x >= g2Px && b.x <= g2Px + 8 && b.y + 2 >= g2PY + 2 && b.y <= g2PY + 7) { b.on = false; gHitPlayer2(); }
  }
  for (auto &b : gBoom)
    if (b.t) b.t = b.t >= 12 ? 0 : b.t + 1;
  if (gBossOn) gBossStep();
  bool any = gBossOn;
  for (int i = 0; i < gNE; i++) any |= gEn[i].alive;
  if (!any && gWave % 4 == 0 && !gBossDone) {  // formation detruite au stage 4, 8, 12... : le boss arrive
    gBossOn = gBossDone = true;
    gBHpMax = gBHp = (20 + 6 * (gWave / 4)) * (gEffDiff() == 0 ? 7 : gEffDiff() == 2 ? 13 : 10) / 10;
    gBX = W / 2 - 8; gBY = -10; gBT = 0; gBFlash = 0;
    sndMelody(JINGLE_ARME, 4);
    any = true;
  }
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
// haut de l'ecran (tous les jeux) : SCORE sur une ligne, centre ; vies en haut a droite.
// Plus de RECORD pendant le jeu (il est au menu et sur la page des scores).
// Vies : une petite icone par vie jusqu'a 5 ; au-dela, une icone et le nombre (X7), pour ne jamais toucher le score.
void hudTop(uint16_t *palFull) {
  char num[12];
  snprintf(num, sizeof(num), "%lu", (unsigned long)gScore);
  int lw = tinyWidth("SCORE"), nw = tinyWidth(num), tw = lw + 4 + nw;
  int x = (W - tw) / 2;
  tinyText(*gfx, x, 1, "SCORE", rgb(255, 40, 40));
  tinyText(*gfx, x + lw + 4, 1, num, rgb(230, 230, 230));
  gMark(x, 1, tw, 5);
  if (gGame == G_RAQ || gGame == G_BLO || gGame == G_BIL) return;  // RAQUETTES, BLOCS, BILLES : pas de vies affichees
  const char *const *life = gGame == G_BUL ? SPR_LIFE_BLOB : gGame == G_BRI ? SPR_LIFE_BALL : gGame == G_CRU ? SPR_LIFE_FRUIT
                          : gGame == G_LAB ? SPR_LIFE_CREATURE : gGame == G_ESQ ? SPR_LIFE_PLANE : SPR_LIFE;
  static const char LABC[6] = { 'p', 'o', 'g', 'c', 'y', 'r' };  // LABYRINTHE : une couleur par icone
  int lives = max(0, gLives);
  if (lives <= 5) {
    for (int i = 0; i < lives; i++) gSprite(life, 5, 4, W - 6 - i * 7, 1, palFull, 'p', gGame == G_LAB ? LABC[i % 6] : 'p');
  } else {
    snprintf(num, sizeof(num), "X%d", lives);
    int vw = tinyWidth(num), vx = W - 1 - vw;
    tinyText(*gfx, vx, 1, num, rgb(230, 230, 230));
    gMark(vx, 1, vw, 5);
    gSprite(life, 5, 4, vx - 7, 1, palFull, 0, 0);
  }
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
    if (e.path == 9) {  // eclat (meduse ou boss) : petit vaisseau 5x5 aux couleurs du monde, ailes qui battent
      static const uint8_t SM[2][5] = { { 0x04, 0x0E, 0x1F, 0x15, 0x11 }, { 0x04, 0x0E, 0x1F, 0x0E, 0x0A } };
      uint16_t c = gPal[gPalIdx(WORLD_HURT[gWorld][0])];
      int x = (int)e.x + 2, y = (int)e.y + 1;
      for (int r = 0; r < 5; r++)
        for (int cc = 0; cc < 5; cc++)
          if (SM[fr][r] & (0x10 >> cc)) gfx->drawPixel(x + cc, y + r, r == 2 && cc == 2 ? gPal[0] : c);
      gMark(x, y, 5, 5);
      continue;
    }
    bool hurt = e.hp < gBaseHp(e.type);  // touche mais pas encore detruit : change de couleur
    gSprite(SPR_W[gWorld][e.type][fr], GW, GH, (int)e.x, (int)e.y, gPal, WORLD_HURT[gWorld][0], hurt ? WORLD_HURT[gWorld][1] : WORLD_HURT[gWorld][0]);
  }
  if (gBossOn) {  // boss : blanc quand il est touche, coque rouge en 2e phase ; barre de vie sous le score
    bool p2 = gBHp * 2 < gBHpMax;
    gSprite(SPR_BOSS, 17, 9, (int)gBX, (int)gBY, palFull, gBFlash ? 'p' : p2 ? 'b' : 0, gBFlash ? 'w' : p2 ? 'r' : 0);
    int bw = W / 2, bx = W / 4, fw = bw * gBHp / gBHpMax;
    gfx->fillRect(bx, 8, fw, 2, p2 ? palFull[1] : palFull[7]);
    gMark(bx, 8, bw, 2);
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
  if (gDead == 0 && (gInv == 0 || (gFrame & 4))) gSprite(SPR_SHIP[gTier()], 9, 8, (int)gPx, gPY, palFull, 0, 0);  // clignote si invincible
  if (g2On && g2Dead == 0 && gLives > 0 && (g2Inv == 0 || (gFrame & 4))) gSprite(SPR_SHIP[gTier()], 9, 8, (int)g2Px, g2PY, palFull, 'w', 'g');  // joueur 2 : vaisseau vert
  hudTop(palFull);  // score centre et vies
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
  int y = tall ? H / 2 - 22 : 10, step = tall ? 10 : 7;  // 7 lignes : un peu plus serrees
  gBox(y - 6, tall ? y + MENU_N * step + 16 : y + (MENU_N - 1) * step + 11);
  const char *items[MENU_N] = { nullptr, nullptr, nullptr, nullptr, nullptr, "SCORES", "DEMO" };
  for (int i = 0; i < MENU_N; i++) {
    const char *t = items[i];
    if (i == 0) { snprintf(b, sizeof(b), "%s%s%s%s", strlen(GAME_NAME[gGame]) > 10 ? "" : "JOUER ", i == menuSel ? "<" : "", GAME_NAME[gGame], i == menuSel ? ">" : ""); t = b; }  // nom long : sans JOUER
    if (i == 1) { snprintf(b, sizeof(b), "JOUEURS %s%d%s %s", i == menuSel ? "<" : "", gPlayers, i == menuSel ? ">" : "", PAD2.isConnected() ? "**" : PS4.isConnected() ? "*" : ""); t = b; }  // * : manettes connectees
    if (i == 2) { snprintf(b, sizeof(b), "NIVEAU %s%s%s", i == menuSel ? "<" : "", DIFF_NAME[gDiff], i == menuSel ? ">" : ""); t = b; }
    if (i == 3) { snprintf(b, sizeof(b), "LUMINOSITE %s%d%%%s", i == menuSel ? "<" : "", brightPct(), i == menuSel ? ">" : ""); t = b; }
    if (i == 4) { snprintf(b, sizeof(b), "VOLUME %s%d%%%s", i == menuSel ? "<" : "", volume * 10, i == menuSel ? ">" : ""); t = b; }
    char line[26];
    snprintf(line, sizeof(line), "%s%s", i == menuSel ? "> " : "", t);
    gText(y + i * step, line, i == menuSel ? sel : off);
  }
  if (tall) gText(y + MENU_N * step + 4, "OPTIONS : JOUER", dim);
}
void gOverlay() {
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  pairOverlay();
  if (padToastUntil && millis() < padToastUntil) gText(H - 10, padToast, rgb(0, 220, 255));  // manette connectee / partie
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
    gText(y + (tall ? 13 : 11), "L2 : REPRENDRE", rgb(0, 160, 200));
    gText(y + (tall ? 24 : 21), "ROND : MENU", rgb(0, 160, 200));
  }
  bool stageMsg = gStageT > 0 && gLives > 0;
  if (stageMsg) {  // annonce du stage, bonus et nouvelle arme
    char b[20];
    int y = H >= 128 ? H / 2 - 22 : H / 2 - 10;
    snprintf(b, sizeof(b), "STAGE %d", gWave);
    if (gStageT > 12 || (gStageT & 2)) gText(y, b, rgb(255, 220, 0));
    if (gGame == 0) snprintf(b, sizeof(b), "MONDE %s", WORLD_NAME[gWorld]);
    else snprintf(b, sizeof(b), "%s", GAME_NAME[gGame]);
    gText(y + 11, b, rgb(255, 60, 200));
    int yy = y + 22;
    if (gBonus && H >= 128) { snprintf(b, sizeof(b), "BONUS %lu", (unsigned long)gBonus); gText(yy, b, rgb(230, 230, 230)); yy += 11; }
    if (gLifeUp) gText(yy, "VIE +1", rgb(0, 220, 255));
  }
  if (!gDemo) return;
  if (stageMsg) return;  // demo : pendant l'annonce du stage, pas d'indication manette (elles se chevauchaient)
  if (PS4.isConnected() && !gAttract) return;  // manette la : le menu a son propre ecran (menuScreen)
  if (PS4.isConnected()) return;  // mode demo : aucun texte (le menu revient au premier appui)
  // pas de manette : petite indication pour la connecter (mini-police) et l'adresse Bluetooth
  const char *hint = "MANETTE : PS + SHARE";
  int hy = H >= 128 ? H / 2 + 20 : H - 14;
  uint16_t grey = rgb(100, 100, 120);
  tinyText(*gfx, (W - tinyWidth(hint)) / 2, hy, hint, grey);
  gMark((W - tinyWidth(hint)) / 2, hy, tinyWidth(hint), 5);
  if (false) {  // plus d'adresse a recopier avec Bluepad32
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
void altHud(uint16_t *palFull) { hudTop(palFull); }
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
struct RMem { REnemy en[R_EN]; RDot shot[R_SHOTS], bomb[R_BOMBS]; int16_t fr[R_FAR][2]; };
#define rEn (GMEM(RMem)->en)
#define rShot (GMEM(RMem)->shot)
#define rBomb (GMEM(RMem)->bomb)
#define rFar (GMEM(RMem)->fr)
RCap rCap;
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
  // cible gardee 20 images (sinon elle change a chaque image) et zone morte plus large que le pas : plus de tremblement
  static float tx = 0;
  static uint8_t hold = 0;
  static int8_t dodge = 0;
  static uint8_t dodgeT = 0;
  if (hold) hold--;
  else {
    float best = 1e9;
    tx = rX;
    for (auto &e : rEn)
      if (e.on && e.t >= 0 && e.y > 10) {
        float d = fabsf(e.x - rX) + (H - e.y) * 0.2f;
        if (d < best) { best = d; tx = e.x; }
      }
    if (rBoss) tx = rBX + 13;
    if (rCap.on) tx = rCap.x;
    hold = 20;
  }
  if (dodgeT) dodgeT--;
  else
    for (auto &b : rBomb)
      if (b.on && b.y > rY - 26 && b.y < rY + 4 && fabsf(b.x - (rX + 4)) < 7) { dodge = b.x < rX + 4 ? 1 : -1; dodgeT = 15; break; }
  float t = dodgeT ? rX + dodge * 14 : tx;
  uint8_t k = 4;
  if (t < rX - 3) k |= 1;
  else if (t > rX + 3) k |= 2;
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
  // boss : le meme vaisseau-mere que le 1er jeu (en double taille) ; il se balance, tire en eventail (3 tirs, 5 en 2e phase,
  // sous la moitie de sa vie) ; detruit, il eclate en 5 petits vaisseaux qui plongent sur toi
  if (rBoss) {
    rBT++;
    if (rBFlash > 0) rBFlash--;
    bool phase2 = rBHp < rBMax / 2;
    if (rBY < 16) rBY += 0.5f;
    else rBX = W / 2 - 17 + sinf(rBT * 0.02f) * (W / 2 - 20);
    if (rBY >= 16 && gDead == 0) {
      int every = max(30, (phase2 ? 40 : 60) - 3 * (gWave / 2) - 10 * (gEffDiff() - 1));
      if (rBT % every == 0) {
        int n = phase2 ? 5 : 3;
        for (int k = 0; k < n; k++) rBombAt(rBX + 17, rBY + 17, (k - (n - 1) / 2.0f) * 0.45f, 1.3f * diff);
      }
    }
    for (auto &s : rShot)
      if (s.on && s.x >= rBX && s.x <= rBX + 33 && s.y >= rBY && s.y <= rBY + 17) {
        s.on = false;
        rBHp--;
        rBFlash = 2;
        if (gFrame & 1) gBoomAt(s.x, s.y);
        break;
      }
    if (rBHp <= 0) {  // boss detruit : stage gagne
      rBoss = false;
      for (int i = 0; i < 8; i++) gBoomAt(rBX + random(34), rBY + random(18));
      int k = 0;
      for (auto &e : rEn)  // il eclate en 5 petits vaisseaux qui plongent (kamikazes)
        if (!e.on && k < 5) {
          e = {};
          e.on = true; e.pat = 3; e.type = 0; e.hp = 1; e.t = -k * 4;
          e.x = rBX + 13 + (k - 2) * 6; e.y = rBY + 8;
          k++;
        }
      sfxHit();
      gScore += 2000UL * gWave;
      altStageClear(1000UL * gWave);
      for (auto &b : rBomb) b.on = false;
      rNewStage();
    }
    if (rBoss && gDead == 0 && rInv == 0 && rBX + 33 >= rX && rBX <= rX + 8 && rBY + 17 >= rY && rBY <= rY + 7) rHit();
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
void rDrawBoss(uint16_t *palFull) {  // le vaisseau-mere du 1er jeu, en double taille : blanc quand il est touche, coque rouge en 2e phase
  bool p2 = rBHp < rBMax / 2;
  char from = rBFlash ? 'p' : p2 ? 'b' : 0, to = rBFlash ? 'w' : p2 ? 'r' : 0;
  int x = rBX, y = rBY;
  for (int r = 0; r < 9; r++)
    for (int c = 0; c < 17; c++) {
      char ch = SPR_BOSS[r][c];
      if (ch == '.') continue;
      if (ch == from) ch = to;
      int i = gPalIdx(ch);
      if (i >= 0) gfx->fillRect(x + c * 2, y + r * 2, 2, 2, palFull[i]);
    }
  gMark(x - 1, y - 1, 36, 20);
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
  if (rCap.on) {  // capsule : cadre qui clignote et icone (double fleche : arme, bouclier, bombe)
    static const char *const ICON[3][7] = {
      { "...y...", "..yyy..", ".y.y.y.", "...y...", "..yyy..", ".y.y.y.", "...y..." },
      { "ccccccc", "cwwcccc", "cwccccc", "ccccccc", ".ccccc.", "..ccc..", "...c..." },
      { "....o..", "...o.o.", "..rrr..", ".rwrrr.", ".rrrrr.", ".rrrrr.", "..rrr.." } };
    uint16_t c = (gFrame & 4) ? palFull[0] : rCap.kind == 1 ? palFull[4] : rCap.kind == 2 ? palFull[1] : palFull[2];
    gfx->drawRoundRect((int)rCap.x - 1, (int)rCap.y - 1, 9, 10, 2, c);
    gSprite(ICON[rCap.kind], 7, 7, (int)rCap.x, (int)rCap.y + 1, palFull, 0, 0);
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

// ---------------------------------------------------------------- RAQUETTES
// A l'horizontale : ta raquette a gauche (haut / bas, ou le stick : plus tu le pousses, plus elle va vite),
// celle de l'ordinateur a droite. L'angle depend de l'endroit ou la balle touche ; elle accelere a chaque echange.
// Point gagne : des points ; point perdu : une vie. 5 points gagnes : stage suivant (ordinateur plus fort).
float qBX, qBY, qVX, qVY, qPY, qCY, qTrail[4][2];
int qWon = 0, qPH = 10, qServe = 0, qStartLives = 3, qP1 = 0, qP2 = 0, qWinner = 0, qFP = 0, qFC = 0;
float qPV = 0, qCV = 0;  // vitesse des raquettes (effet donne a la balle)
bool q2p = false;  // match a deux joueurs (2e manette sur la raquette de droite)
bool qToPlayer = true;
int qTop() { return 9; }
int qPX() { return 3; }          // raquette du joueur (gauche)
int qCX() { return W - 6; }      // raquette de l'ordinateur (droite)
void qServeBall() {
  qBX = W / 2; qBY = (qTop() + H) / 2;
  float sp = 1.2f + 0.12f * gWave + 0.15f * (gEffDiff() - 1);
  float a = (random(2) ? 1 : -1) * (0.25f + random(30) / 100.0f);
  qVY = sinf(a) * sp;
  qVX = (qToPlayer ? -1 : 1) * cosf(a) * sp;
  for (auto &t : qTrail) { t[0] = qBX; t[1] = qBY; }
  qServe = 40;
}
void qReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  qWon = 0;
  qStartLives = gLives;
  q2p = !gDemo && gPlayers == 2 && PAD2.isConnected();
  qP1 = qP2 = qWinner = 0;
  qPH = H >= 128 ? 16 : 10;
  qPY = qCY = (qTop() + H) / 2 - qPH / 2;
  qToPlayer = true;
  qServeBall();
}
uint8_t qAuto() {  // demo : suit la balle
  float c = qPY + qPH / 2.0f, ty = qVX < 0 ? qBY : (qTop() + H) / 2;
  return ty < c - 2 ? 8 : ty > c + 2 ? 16 : 0;
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
  float sp = H >= 128 ? 3.2f : 2.2f, oldP = qPY, oldC = qCY;
  if (qFP > 0) qFP--;
  if (qFC > 0) qFC--;
  if (!gDemo && padStickY) qPY -= sp * 1.3f * padStickY / 127.0f;  // stick : vitesse selon l'inclinaison
  else {
    if (k & 8) qPY -= sp;
    if (k & 16) qPY += sp;
  }
  qPY = constrain(qPY, (float)qTop(), (float)(H - qPH));
  // l'ordinateur suit la balle avec un temps de reaction et une vitesse limitee (selon stage et difficulte)
  if (q2p) {  // 2 joueurs : la 2e manette tient la raquette de droite (stick ou croix)
    int sy = PAD2.LStickY();
    if (abs(sy) > 18) qCY -= sp * 1.3f * sy / 127.0f;
    else { if (PAD2.Up()) qCY -= sp; if (PAD2.Down()) qCY += sp; }
  } else {  // l'ordinateur suit la balle avec un temps de reaction et une vitesse limitee (selon stage et difficulte)
    float cs = 0.6f + 0.15f * gWave + 0.22f * (gEffDiff() - 1), target = qVX > 0 ? qBY : (qTop() + H) / 2;
    float cc = qCY + qPH / 2.0f;
    if (fabsf(target - cc) > 2) qCY += constrain(target - cc, -cs, cs);
  }
  qCY = constrain(qCY, (float)qTop(), (float)(H - qPH));
  qPV = qPY - oldP; qCV = qCY - oldC;
  if (qServe > 0) { qServe--; if (gStageT > 0) gStageT--; return; }
  for (int i = 3; i > 0; i--) { qTrail[i][0] = qTrail[i - 1][0]; qTrail[i][1] = qTrail[i - 1][1]; }
  qTrail[0][0] = qBX; qTrail[0][1] = qBY;
  qBX += qVX; qBY += qVY;
  if (qBY < qTop()) { qBY = qTop(); qVY = fabsf(qVY); sfxMenu(); }
  if (qBY > H - 2) { qBY = H - 2; qVY = -fabsf(qVY); sfxMenu(); }
  auto bounce = [&](float padY, float padV, bool right) {  // rebond : angle selon le point d'impact et le mouvement de la raquette
    float rel = (qBY + 1 - (padY + qPH / 2.0f)) / (qPH / 2.0f);
    float speed = min(sqrtf(qVX * qVX + qVY * qVY) * 1.06f, 3.6f);
    float a = constrain(rel + padV * 0.12f, -1.0f, 1.0f) * 0.95f;
    (right ? qFP : qFC) = 6;
    qVY = sinf(a) * speed;
    qVX = (right ? 1 : -1) * max(0.7f, cosf(a)) * speed;
    sfxShot();
  };
  int px = qPX(), cx = qCX();
  if (qVX < 0 && qBX <= px + 3 && qBX >= px - 1 && qBY + 2 >= qPY && qBY <= qPY + qPH) { qBX = px + 3; bounce(qPY, qPV, true); }
  if (qVX > 0 && qBX + 2 >= cx && qBX + 2 <= cx + 4 && qBY + 2 >= qCY && qBY <= qCY + qPH) { qBX = cx - 2; bounce(qCY, qCV, false); }
  if (q2p && (qBX < -2 || qBX > W)) {  // match a deux : un point pour celui qui n'a pas rate, 7 points pour gagner
    bool p1 = qBX > W;
    if (p1) qP1++; else qP2++;
    gBoomAt(p1 ? W - 3 : 2, qBY);
    sfxBoom();
    qToPlayer = !p1;
    if (qP1 >= 7 || qP2 >= 7) {
      qWinner = qP1 >= 7 ? 1 : 2;
      sndMelody(JINGLE_STAGE, 4);
      gLives = 0;
      gLastRank = 0;
      gDead = 200;  // le vainqueur s'affiche, puis retour au menu (pas de tableau des scores a deux)
    } else qServeBall();
  } else if (qBX < -2) {  // point perdu : une vie
    altLoseLife(2, qBY);
    qToPlayer = true;
  } else if (qBX > W) {  // point gagne
    gScore += 100UL * gWave;
    if (!gDemo && gScore > gHi) gHi = gScore;
    gBoomAt(W - 3, qBY);
    sfxBoom();
    qToPlayer = false;
    if (++qWon >= 5) {
      qWon = 0;
      gWave++;
      gStageT = 100;
      altStageClear(500UL * gWave);
      sndMelody(JINGLE_STAGE, 4);
      qPH = max(H >= 128 ? 11 : 7, qPH - 1);
    }
    qServeBall();
  }
  if (gStageT > 0) gStageT--;
}
void qDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(0.05f, palFull);
  int mid = W / 2;
  for (int y = qTop(); y < H; y += 6) gfx->drawFastVLine(mid, y, 3, rgb(50, 50, 80));  // filet vertical en pointilles
  gMark(mid, qTop(), 1, H - qTop());
  int px = qPX(), cx = qCX(), py = (int)qPY, cy = (int)qCY;
  // raquettes en relief sur 3 colonnes : ombre cote mur, couleur, reflet cote balle ; bouts arrondis,
  // poignee au milieu, eclair blanc au contact de la balle
  auto paddle = [&](int x, int y, bool left, int flash, uint8_t r, uint8_t g, uint8_t b) {
    uint16_t dark = rgb(r / 4, g / 4, b / 4), main = rgb(r, g, b), lite = rgb(min(255, r / 2 + 140), min(255, g / 2 + 140), min(255, b / 2 + 140));
    if (flash) main = lite = rgb(255, 255, 255);
    int xo = left ? x : x + 2, xi = left ? x + 2 : x;  // colonne cote mur, colonne cote balle
    gfx->drawFastVLine(xo, y + 2, qPH - 4, dark);
    gfx->drawFastVLine(x + 1, y + 1, qPH - 2, main);
    gfx->drawFastVLine(xi, y + 1, qPH - 2, lite);
    gfx->drawPixel(x + 1, y, lite);                     // bouts arrondis
    gfx->drawPixel(x + 1, y + qPH - 1, dark);
    gfx->drawFastVLine(x + 1, y + qPH / 2 - 1, 2, flash ? main : dark);  // poignee
    gMark(x, y, 3, qPH);
  };
  paddle(px, py, true, qFP, 0, 220, 255);
  paddle(cx, cy, false, qFC, 255, 50, 50);
  if (gDead == 0) {
    for (int i = 3; i >= 1; i--) {  // trainee de la balle
      uint8_t v = 40 + 30 * (3 - i);
      gfx->fillRect((int)qTrail[i][0], (int)qTrail[i][1], 2, 2, rgb(v, v, v));
      gMark((int)qTrail[i][0], (int)qTrail[i][1], 2, 2);
    }
    if (qServe == 0 || (gFrame & 4)) { gfx->fillRect((int)qBX, (int)qBY, 2, 2, palFull[0]); gMark((int)qBX, (int)qBY, 2, 2); }
  }
  char b[20];  // tes points (ce stage) a gauche du filet, ceux de l'ordinateur ou du joueur 2 a droite
  snprintf(b, sizeof(b), "%d", q2p ? qP1 : qWon);
  tinyText(*gfx, mid - 6, qTop() + 1, b, palFull[4]);
  snprintf(b, sizeof(b), "%d", q2p ? qP2 : max(0, qStartLives - gLives));
  tinyText(*gfx, mid + 3, qTop() + 1, b, palFull[1]);
  gMark(mid - 6, qTop() + 1, 13, 5);
  if (qWinner) {  // fin du match a deux
    snprintf(b, sizeof(b), "JOUEUR %d GAGNE", qWinner);
    gText(H / 2 - 4, b, qWinner == 1 ? palFull[4] : palFull[1]);
  }
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
struct BMem { BPlat plat[B_PLAT]; BEnemy en[B_EN]; BBubble bub[B_BUB]; BGem gem[B_GEM]; };
#define bPlat (GMEM(BMem)->plat)
#define bEn (GMEM(BMem)->en)
#define bBub (GMEM(BMem)->bub)
#define bGem (GMEM(BMem)->gem)
int bNPlat = 0;
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
      bPlat[bNPlat++] = { 6, (int16_t)(half - g), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(half + g), (int16_t)(W - 6), (int16_t)y };
    } else if (pattern == 1) {  // une plateforme au centre
      int w = random(W / 4, W / 2);
      bPlat[bNPlat++] = { (int16_t)(half - w / 2), (int16_t)(half + w / 2), (int16_t)y };
    } else if (pattern == 2) {  // trois morceaux
      int w = W / 5;
      bPlat[bNPlat++] = { 6, (int16_t)(6 + w), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(half - w / 2), (int16_t)(half + w / 2), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(W - 6 - w), (int16_t)(W - 6), (int16_t)y };
    } else {  // bords seulement
      int w = random(W / 5, W / 3);
      bPlat[bNPlat++] = { 6, (int16_t)(6 + w), (int16_t)y };
      bPlat[bNPlat++] = { (int16_t)(W - 6 - w), (int16_t)(W - 6), (int16_t)y };
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
  bX = constrain(bX, 6.0f, (float)(W - 14));  // murs de 5 pixels
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
    b.x = constrain(b.x, 8.0f, (float)(W - 8));
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
      if (e.x < 6 || e.x > W - 15) e.vx = -e.vx;
      if (e.y < 14 || e.y > bFloor() - 7) e.vy = -e.vy;
    } else {
      e.x += e.vx * sp;
      if (e.x < 6 || e.x > W - 15) { e.vx = -e.vx; e.x = constrain(e.x, 6.0f, (float)(W - 15)); }
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
  uint16_t platH = rgb(min(255, c[0] + 80), min(255, c[1] + 80), min(255, c[2] + 80));
  for (int i = 0; i < bNPlat; i++) {  // plateformes epaisses en briques : arete claire, joints sombres, ombre dessous
    BPlat &p = bPlat[i];
    int th = min(5, H - p.y), w = p.x1 - p.x0;
    gfx->fillRect(p.x0, p.y, w, th, plat);
    gfx->drawFastHLine(p.x0, p.y, w, platH);
    if (th >= 5) {
      gfx->drawFastHLine(p.x0, p.y + 2, w, platD);
      gfx->drawFastHLine(p.x0, p.y + 4, w, platD);
      for (int x = p.x0 + 4; x < p.x1; x += 8) gfx->drawFastVLine(x, p.y + 1, 1, platD);
      for (int x = p.x0 + 8; x < p.x1; x += 8) gfx->drawFastVLine(x, p.y + 3, 1, platD);
    }
  }
  for (int side = 0; side < 2; side++) {  // murs de 5 pixels en briques
    int x = side ? W - 5 : 0;
    gfx->fillRect(x, 10, 5, H - 10, plat);
    gfx->drawFastVLine(side ? x : x + 4, 10, H - 10, platH);
    for (int y = 12; y < H; y += 4) gfx->drawFastHLine(x, y, 5, platD);
    for (int y = 10; y < H; y += 8) { gfx->drawPixel(x + 2, y, platD); gfx->drawPixel(x + 2, y + 1, platD); }
  }
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

// rectangle limite a la zone de jeu (sous la ligne du score)
void jRect(int x, int y, int w, int h, uint16_t c) {
  int x0 = max(0, x), y0 = max(8, y), x1 = min(W, x + w), y1 = min(H, y + h);
  if (x1 > x0 && y1 > y0) gfx->fillRect(x0, y0, x1 - x0, y1 - y0, c);
}
void jPx(int x, int y, uint16_t c) {
  if (x >= 0 && x < W && y >= 8 && y < H) gfx->drawPixel(x, y, c);
}

// ---------------------------------------------------------------- LABYRINTHE
// Un petit scarabee vert (original) ramasse les graines d'un labyrinthe (plan original) en evitant
// 4 creatures piquantes qui le poursuivent chacune a sa facon. Une grosse graine lumineuse rend les
// creatures vulnerables quelques secondes (bleues, elles fuient) : les attraper rapporte de plus en plus.
// Le couloir du milieu est ouvert sur les cotes : on ressort de l'autre cote.
#define L_COLS 21
#define L_ROWS 19
const char *const L_MAZE[L_ROWS] = {
  "#####################",
  "#o.......#.#.......o#",
  "#.###.##.#.#.##.###.#",
  "#.....#.......#.....#",
  "###.#.#.#####.#.#.###",
  "#...#.....#.....#...#",
  "#.#####.#.#.#.#####.#",
  "#.......#...#.......#",
  "###.###.## ##.###.###",
  "   .#...#E E#...#.   ",
  "###.#.#.#####.#.#.###",
  "#.....#...P...#.....#",
  "#.###.###.#.###.###.#",
  "#o..#.....#.....#..o#",
  "###.#.#.#####.#.#.###",
  "#.....#...#...#.....#",
  "#.#######.#.#######.#",
  "#...................#",
  "#####################"
};
struct LMem { uint8_t dot[L_ROWS][L_COLS], from[L_ROWS][L_COLS]; int16_t q[L_ROWS * L_COLS]; };
#define lDot (GMEM(LMem)->dot)  // 0 rien, 1 graine, 2 grosse graine
LMob lP, lE[4];
int lTile = 6, lOX = 0, lOY = 12, lDots = 0, lFright = 0, lChain = 0, lCamY = 0;  // lCamY : defilement vertical (une seule dalle)
int8_t lWantDx = 0, lWantDy = 0;
float lcx(int c) { return lOX + c * lTile + lTile / 2.0f; }
float lcy(int r) { return lOY + r * lTile + lTile / 2.0f; }
bool lOpen(int c, int r, bool player) {
  if (r < 0 || r >= L_ROWS) return false;
  if (c < 0 || c >= L_COLS) return r == 9;  // couloir du milieu ouvert sur les cotes
  char t = L_MAZE[r][c];
  if (t == '#') return false;
  if (player && ((r == 8 && c == 10) || (r == 9 && c >= 9 && c <= 11))) return false;  // la maison des creatures
  return true;
}
void lPlace(LMob &m, int c, int r) { m.c = c; m.r = r; m.x = lcx(c); m.y = lcy(r); m.dx = m.dy = 0; }
void lPositions() {  // scarabee au depart, creatures a la maison (sorties echelonnees)
  lPlace(lP, 10, 11);
  lWantDx = -1; lWantDy = 0;
  static const int8_t HC[4] = { 10, 9, 10, 11 }, HR[4] = { 7, 9, 9, 9 };
  for (int i = 0; i < 4; i++) {
    lPlace(lE[i], HC[i], HR[i]);
    lE[i].state = i == 0 ? 0 : 2;
    lE[i].t = 40 + i * 90;
    lE[i].fr = false;
  }
  lFright = 0;
}
void lNewMaze() {
  lDots = 0;
  for (int r = 0; r < L_ROWS; r++)
    for (int c = 0; c < L_COLS; c++) {
      char t = L_MAZE[r][c];
      lDot[r][c] = t == '.' ? 1 : t == 'o' ? 2 : 0;
      lDots += lDot[r][c] != 0;
    }
  lPositions();
}
void lReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  // cases d'au moins 5 pixels : si le labyrinthe ne tient pas en hauteur (une dalle 128x64), il defile verticalement
  lTile = min(W / L_COLS, (H - 8) / L_ROWS);
  if (lTile < 5) lTile = min(6, W / L_COLS);
  lOX = (W - L_COLS * lTile) / 2;
  lOY = 8 + max(0, ((H - 8) - L_ROWS * lTile) / 2);
  lCamY = 0;
  lNewMaze();
}
// choix de la direction en arrivant au centre d'une case
void lChoose(LMob &m, int who, int8_t &ndx, int8_t &ndy) {
  ndx = ndy = 0;
  if (who < 0) {  // scarabee : direction voulue, sinon tout droit, sinon arret
    if ((lWantDx || lWantDy) && lOpen(m.c + lWantDx, m.r + lWantDy, true)) { ndx = lWantDx; ndy = lWantDy; }
    else if ((m.dx || m.dy) && lOpen(m.c + m.dx, m.r + m.dy, true)) { ndx = m.dx; ndy = m.dy; }
    return;
  }
  // creature : cible selon son caractere (pas de demi-tour, sauf impasse)
  int tc = lP.c, tr = lP.r;
  if (m.state == 3) { tc = 10; tr = 7; }  // sortie de la maison
  else if (who == 1) { tc = lP.c + lP.dx * 4; tr = lP.r + lP.dy * 4; }           // vise devant le scarabee
  else if (who == 2) { tc = 2 * lP.c - lE[0].c; tr = 2 * lP.r - lE[0].r; }       // prend en tenaille
  else if (who == 3 && abs(m.c - lP.c) + abs(m.r - lP.r) < 7) { tc = 1; tr = 17; }  // timide : repart dans son coin
  static const int8_t DX[4] = { 0, -1, 0, 1 }, DY[4] = { -1, 0, 1, 0 };
  int best = -1;
  long bestD = 0x7fffffff;
  int opts[4], nOpts = 0;
  for (int k = 0; k < 4; k++) {
    if (DX[k] == -m.dx && DY[k] == -m.dy && (m.dx || m.dy)) continue;
    if (!lOpen(m.c + DX[k], m.r + DY[k], false)) continue;
    if (m.state == 0 && m.r == 7 && m.c == 10 && DY[k] == 1) continue;  // ne rentre pas dans la maison
    opts[nOpts++] = k;
    long dx = m.c + DX[k] - tc, dy = m.r + DY[k] - tr, d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = k; }
  }
  if (!nOpts) { ndx = -m.dx; ndy = -m.dy; return; }  // impasse : demi-tour
  int rnd = gEffDiff() == 0 ? 35 : gEffDiff() == 1 ? 18 : 6;  // facile : creatures plus hesitantes
  if (m.fr || random(100) < rnd) best = opts[random(nOpts)];  // effrayee : au hasard
  ndx = DX[best]; ndy = DY[best];
}
void lArrive(LMob &m, int who) {
  if (who >= 0) {
    if (m.state == 3 && m.c == 10 && m.r == 7) m.state = 0;  // sortie de la maison
    return;
  }
  uint8_t &d = lDot[m.r][m.c];  // le scarabee mange la graine
  if (!d) return;
  if (d == 2) {  // grosse graine : creatures vulnerables
    gScore += 50;
    lFright = max(90, 260 - 20 * gWave);
    lChain = 0;
    for (auto &e : lE) if (e.state != 2) { e.fr = true; e.dx = -e.dx; e.dy = -e.dy; }
    sfxDive();
  } else {
    gScore += 10;
    if ((lDots & 1) == 0) sfxMenu();
  }
  d = 0;
  lDots--;
}
void lMove(LMob &m, float sp, int who) {
  for (int guard = 0; sp > 0.001f && guard < 8; guard++) {
    float tx = lcx(m.c), ty = lcy(m.r);
    float d = fabsf(tx - m.x) + fabsf(ty - m.y);
    if (d < 0.01f) {  // centre de la case atteint : on choisit la suite
      m.x = tx; m.y = ty;
      lArrive(m, who);
      int8_t ndx, ndy;
      lChoose(m, who, ndx, ndy);
      if (!ndx && !ndy) { m.dx = m.dy = 0; return; }
      m.dx = ndx; m.dy = ndy;
      int nc = m.c + ndx;
      if (nc < 0) { nc = L_COLS - 1; m.x += L_COLS * lTile; }  // couloir du milieu : on ressort de l'autre cote
      if (nc >= L_COLS) { nc = 0; m.x -= L_COLS * lTile; }
      m.c = nc; m.r += ndy;
      continue;
    }
    float step = min(sp, d);
    m.x += (tx > m.x ? step : tx < m.x ? -step : 0);
    m.y += (ty > m.y ? step : ty < m.y ? -step : 0);
    sp -= step;
  }
}
// pilote automatique (demo) : plus court chemin vers la graine la plus proche, en evitant les creatures
uint8_t lAuto() {
  auto &from = GMEM(LMem)->from;
  int16_t *q = GMEM(LMem)->q;
  memset(from, 0xFF, sizeof(from));
  int head = 0, tail = 0;
  q[tail++] = lP.r * L_COLS + lP.c;
  from[lP.r][lP.c] = 4;
  static const int8_t DX[4] = { 0, -1, 0, 1 }, DY[4] = { -1, 0, 1, 0 };
  int found = -1;
  while (head < tail) {
    int cur = q[head++], r = cur / L_COLS, c = cur % L_COLS;
    if (lDot[r][c] && !(r == lP.r && c == lP.c)) { found = cur; break; }
    for (int k = 0; k < 4; k++) {
      int nr = r + DY[k], nc = (c + DX[k] + L_COLS) % L_COLS;
      if (!lOpen(c + DX[k], nr, true) || from[nr][nc] != 0xFF) continue;
      bool danger = false;
      for (auto &e : lE) if (!e.fr && e.state != 2 && abs(e.c - nc) + abs(e.r - nr) <= 2) danger = true;
      if (danger) continue;
      from[nr][nc] = k;
      q[tail++] = nr * L_COLS + nc;
    }
  }
  if (found < 0) return 0;
  int r = found / L_COLS, c = found % L_COLS, k = 4;
  while (!(r == lP.r && c == lP.c)) {  // remonte jusqu'au premier pas
    k = from[r][c];
    int pr = r - DY[k], pc = (c - DX[k] + L_COLS) % L_COLS;
    if (pr == lP.r && pc == lP.c) break;
    r = pr; c = pc;
  }
  static const uint8_t KEY[4] = { 8, 1, 16, 2 };
  return k < 4 ? KEY[k] : 0;
}
void lStep() {
  gFrame++;
  uint8_t k = gDemo ? lAuto() : keysPs4;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) lReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      lPositions();
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }  // annonce : tout le monde attend
  if (k & 1) { lWantDx = -1; lWantDy = 0; }
  if (k & 2) { lWantDx = 1; lWantDy = 0; }
  if (k & 8) { lWantDx = 0; lWantDy = -1; }
  if (k & 16) { lWantDx = 0; lWantDy = 1; }
  if ((lWantDx || lWantDy) && lWantDx == -lP.dx && lWantDy == -lP.dy) {  // demi-tour immediat
    lP.c -= lP.dx; lP.r -= lP.dy;
    if (lP.c < 0) { lP.c += L_COLS; lP.x += L_COLS * lTile; }
    if (lP.c >= L_COLS) { lP.c -= L_COLS; lP.x -= L_COLS * lTile; }
    lP.dx = lWantDx; lP.dy = lWantDy;
  }
  float unit = lTile / 6.0f;
  lMove(lP, 0.95f * unit, -1);
  if (lFright > 0 && --lFright == 0)
    for (auto &e : lE) e.fr = false;
  float es = (0.82f + 0.04f * min(gWave, 8) + 0.08f * (gEffDiff() - 1)) * unit;
  for (int i = 0; i < 4; i++) {
    LMob &e = lE[i];
    if (e.state == 2) { if (--e.t <= 0) e.state = 3; continue; }  // attend a la maison
    lMove(e, e.fr ? 0.5f * unit : es, i);
    if (fabsf(e.x - lP.x) + fabsf(e.y - lP.y) < lTile * 0.8f) {
      if (e.fr) {  // attrapee : points (200, 400, 800, 1600), retour a la maison
        lChain = min(lChain + 1, 4);
        gScore += 100UL << lChain;
        gBoomAt(e.x, e.y - lCamY);
        sfxBoom();
        e.fr = false;
        lPlace(e, 10, 9);
        e.state = 2; e.t = 120;
      } else {
        altLoseLife(lP.x, lP.y - lCamY);
        return;
      }
    }
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  if (lDots <= 0) {  // labyrinthe vide : stage suivant (creatures plus rapides), une vie en plus
    gWave++;
    gStageT = 100;
    altStageClear(1000UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    lNewMaze();
  }
}
bool lWall(int r, int c) { return r >= 0 && r < L_ROWS && c >= 0 && c < L_COLS && L_MAZE[r][c] == '#'; }
bool lFloor(int r, int c) { return r >= 0 && r < L_ROWS && c >= 0 && c < L_COLS && L_MAZE[r][c] != '#'; }
void lDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  int t = lTile, VH = H - 8, mh = L_ROWS * t;
  lCamY = mh > VH ? constrain((int)lP.y - 8 - VH / 2, 0, mh - VH) : 0;  // la camera suit le scarabee
  // murs "neon" : interieur sombre, bord lumineux cote couloir, coins arrondis ; couleur differente a chaque stage
  static const uint8_t WC[4][3] = { { 40, 120, 255 }, { 220, 60, 220 }, { 30, 210, 120 }, { 255, 140, 30 } };
  const uint8_t *wc = WC[(gWave - 1) % 4];
  uint16_t neon = rgb(wc[0], wc[1], wc[2]), inner = rgb(wc[0] / 7, wc[1] / 7, wc[2] / 7);
  uint16_t seed = rgb(255, 200, 150), glowA = palFull[0], glowB = palFull[2];
  for (int r = 0; r < L_ROWS; r++) {
    int y = lOY + r * t - lCamY;
    if (y + t <= 8 || y >= H) continue;
    for (int c = 0; c < L_COLS; c++) {
      int x = lOX + c * t;
      if (L_MAZE[r][c] == '#') {
        jRect(x, y, t, t, inner);
        bool up = lFloor(r - 1, c), dn = lFloor(r + 1, c), lf = lFloor(r, c - 1), rt = lFloor(r, c + 1);
        if (up) jRect(x + (lf ? 1 : 0), y, t - (lf ? 1 : 0) - (rt ? 1 : 0), 1, neon);
        if (dn) jRect(x + (lf ? 1 : 0), y + t - 1, t - (lf ? 1 : 0) - (rt ? 1 : 0), 1, neon);
        if (lf) jRect(x, y + (up ? 1 : 0), 1, t - (up ? 1 : 0) - (dn ? 1 : 0), neon);
        if (rt) jRect(x + t - 1, y + (up ? 1 : 0), 1, t - (up ? 1 : 0) - (dn ? 1 : 0), neon);
        // coin interieur (couloir en diagonale seulement) : un point pour fermer l'angle
        if (!up && !lf && lFloor(r - 1, c - 1)) jPx(x, y, neon);
        if (!up && !rt && lFloor(r - 1, c + 1)) jPx(x + t - 1, y, neon);
        if (!dn && !lf && lFloor(r + 1, c - 1)) jPx(x, y + t - 1, neon);
        if (!dn && !rt && lFloor(r + 1, c + 1)) jPx(x + t - 1, y + t - 1, neon);
      } else {
        jRect(x, y, t, t, 0);
        if (lDot[r][c] == 1) jRect(x + t / 2 - 1, y + t / 2 - 1, 2, 2, seed);
        else if (lDot[r][c] == 2) {  // grosse graine : ronde, qui pulse
          uint16_t gc = (gFrame & 8) ? glowA : glowB;
          jRect(x + t / 2 - 1, y + t / 2 - 2, 2, 4, gc);
          jRect(x + t / 2 - 2, y + t / 2 - 1, 4, 2, gc);
        }
      }
    }
  }
  jRect(0, 8, lOX, VH, 0);  // bandes hors labyrinthe (couloir du milieu) : effacees a chaque image
  jRect(lOX + L_COLS * t, 8, W, VH, 0);
  jRect(lOX + 10 * t, lOY + 8 * t + t / 2 - lCamY, t, 1, palFull[7]);  // porte de la maison
  // creatures piquantes : piquants qui tournent (2 images), yeux qui regardent ou elles vont ; bleues quand elles fuient
  static const uint8_t EC[4][3] = { { 170, 70, 255 }, { 255, 140, 0 }, { 170, 230, 40 }, { 230, 230, 230 } };
  static const uint8_t SPK[2][5] = { { 0x15, 0x0E, 0x1F, 0x0E, 0x15 }, { 0x0A, 0x1F, 0x0E, 0x1F, 0x0A } };
  for (int i = 0; i < 4; i++) {
    LMob &e = lE[i];
    uint16_t col = rgb(EC[i][0], EC[i][1], EC[i][2]), core = rgb(EC[i][0] / 2, EC[i][1] / 2, EC[i][2] / 2);
    if (e.fr) { col = (lFright < 60 && (gFrame & 4)) ? palFull[0] : rgb(40, 90, 255); core = rgb(20, 40, 140); }
    int x = (int)e.x - 2, y = (int)e.y - 2 - lCamY;
    const uint8_t *sp = SPK[(gFrame / 6 + i) & 1];
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 5; c++)
        if (sp[r] & (0x10 >> c)) jPx(x + c, y + r, (r >= 1 && r <= 3 && c >= 1 && c <= 3) ? core : col);
    jPx(x + 2, y + 2, col);
    if (e.fr) { jPx(x + 1, y + 1, palFull[0]); jPx(x + 3, y + 1, palFull[0]); }  // effrayee : yeux ecarquilles
    else {
      int ex = constrain((int)e.dx, -1, 1), ey = constrain((int)e.dy, -1, 1);
      jPx(x + 1 + (ex > 0), y + 1 + (ey > 0), palFull[0]);
      jPx(x + 3 - (ex < 0), y + 1 + (ey > 0), palFull[0]);
    }
  }
  if (gDead == 0) {  // scarabee : carapace verte avec reflet, tete sombre du cote ou il va, pattes qui bougent
    int x = (int)lP.x - 2, y = (int)lP.y - 2 - lCamY;
    uint16_t g = rgb(40, 210, 70), hi = rgb(170, 255, 170), dk = rgb(10, 90, 30), head = rgb(20, 60, 25);
    jRect(x + 1, y, 3, 5, g);
    jRect(x, y + 1, 5, 3, g);
    jRect(x + 2, y + 1, 1, 3, dk);  // separation des elytres
    jPx(x + 1, y + 1, hi);          // reflet
    int dx = lP.dx, dy = lP.dy;
    if (!dx && !dy) dy = -1;
    int hx = x + 2 + dx * 2, hy = y + 2 + dy * 2;  // tete
    jPx(hx, hy, head);
    if (dx) { jPx(hx, hy - 1, palFull[0]); jPx(hx, hy + 1, palFull[0]); }
    else { jPx(hx - 1, hy, palFull[0]); jPx(hx + 1, hy, palFull[0]); }
    if ((gFrame / 4) & 1) { jPx(x - 1, y + 1, dk); jPx(x + 5, y + 3, dk); jPx(x - 1, y + 4, dk); jPx(x + 5, y, dk); }
    else { jPx(x - 1, y + 3, dk); jPx(x + 5, y + 1, dk); jPx(x - 1, y, dk); jPx(x + 5, y + 4, dk); }
  }
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- ORBITE
// Le vaisseau tourne sur le bord de l'ecran (une ellipse) et tire vers le centre. Les etoiles jaillissent du
// centre (profondeur) ; les ennemis en sortent en spirale, se rangent en anneau puis plongent vers toi en
// tirant. Plus ils sont pres, plus ils sont gros. Un ennemi blanc par stage lache une capsule : double tir.
// Coordonnees polaires : angle a (radians) et profondeur r (0 = centre, 1 = orbite du vaisseau).
#define O_EN 56  // jusqu'a 7 vagues de 8
#define O_SHOTS 8
#define O_BUL 12
#define O_STARS 40
struct OMem { OEn en[O_EN]; OShot shot[O_SHOTS], bul[O_BUL], star[O_STARS]; };
#define oEn (GMEM(OMem)->en)
#define oShot (GMEM(OMem)->shot)
#define oBul (GMEM(OMem)->bul)
#define oStar (GMEM(OMem)->star)
OShot oCap;
float oPa = 1.5708f, oRot = 0;  // angle du vaisseau (en bas), rotation de l'anneau
int oCool = 0, oSpawnT = 0, oDiveT = 0, oPower = 0;
float ocx() { return W / 2.0f; }
float ocy() { return 8 + (H - 8) / 2.0f; }
float orx() { return W / 2.0f - 6; }
float ory() { return (H - 8) / 2.0f - 4; }
float oX(float a, float r) { return ocx() + cosf(a) * r * orx(); }
float oY(float a, float r) { return ocy() + sinf(a) * r * ory(); }
float oAngDiff(float a, float b) {  // ecart d'angle ramene entre -pi et pi
  float d = fmodf(a - b, 6.2832f);
  if (d > 3.1416f) d -= 6.2832f;
  if (d < -3.1416f) d += 6.2832f;
  return d;
}
// sprites d'ORBITUS (originaux) : meduse, soucoupe, papillon en 2 images, satellite porteur de la capsule
const char *const O_SPR[3][2][7] = {
  { { "..ppp..", ".ppwpp.", "pppvppp", ".ppppp.", "v.v.v.v", ".v.v.v.", "v.....v" },
    { "..ppp..", ".ppwpp.", "pppvppp", ".ppppp.", ".v.v.v.", "v.v.v.v", ".v...v." } },
  { { "..ccc..", ".cwwcc.", "bbbbbbb", "wbybybw", ".bbbbb.", "..c.c..", ".c...c." },
    { "..ccc..", ".cwwcc.", "bbbbbbb", "ybwbwby", ".bbbbb.", "..c.c..", "..c.c.." } },
  { { "o.....o", "oy.r.yo", "oyyryyo", ".oywyo.", "oyyryyo", "oy...yo", "o.....o" },
    { ".......", ".o.r.o.", "oyyryyo", "yoywyoy", "oyyryyo", ".o...o.", "......." } }
};
const char *const O_SAT[2][7] = { { "...w...", "..wcw..", "b.wrw.b", "bwwwwwb", "b.wrw.b", "..wcw..", "...w..." },
                                  { "...w...", "..wrw..", "b.wcw.b", "bwwwwwb", "b.wcw.b", "..wrw..", "...w..." } };
void oNewStage() {
  int waves = min(7, 4 + gWave / 2);     // 4 vagues de 8 au stage 1, puis une de plus tous les 2 stages (7 au plus)
  for (int i = 0; i < O_EN; i++) {
    OEn &e = oEn[i];
    int w = i / 8, k = i % 8;
    e.on = w < waves;
    e.state = 0;                         // attend son tour
    e.t = -(w * 95 + k * 8);             // les vagues se suivent de pres
    e.a0 = w * 1.5708f + gWave * 0.7f;   // chaque vague sort dans une direction differente
    e.dir = (w & 1) ? -1 : 1;
    e.type = (w + gWave) % 3;
    e.carrier = (i == 13);
    e.r = 0; e.a = e.a0;
  }
  for (auto &b : oBul) b.on = false;
  for (auto &s : oShot) s.on = false;
  oCap.on = false;
  oDiveT = 150;
}
void oReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  for (auto &s : oStar) { s.on = true; s.a = random(6283) / 1000.0f; s.r = random(1000) / 1000.0f; }
  oPa = 1.5708f; oPower = 0; oCool = 0;
  oNewStage();
}
void oFire() {
  int n = oPower ? 2 : 1;
  for (int k = 0; k < n; k++)
    for (auto &s : oShot)
      if (!s.on) { s.on = true; s.r = 0.92f; s.a = oPa + (n == 2 ? (k ? 0.07f : -0.07f) : 0); break; }
  sfxShot();
}
// pilote automatique (demo) : vise l'ennemi le plus proche, s'ecarte des tirs qui arrivent
uint8_t oAuto() {
  float ta = oPa;
  float best = -1;
  for (auto &e : oEn)
    if (e.on && e.state >= 1 && e.r > best) { best = e.r; ta = e.a; }
  for (auto &b : oBul)
    if (b.on && b.r > 0.55f && fabsf(oAngDiff(b.a, oPa)) < 0.3f) ta = oPa + (oAngDiff(b.a, oPa) > 0 ? -0.9f : 0.9f);
  uint8_t k = 4;
  if (fabsf(oAngDiff(ta, oPa)) > 0.06f) {
    float dx = cosf(ta), dy = sinf(ta);
    if (dx < -0.38f) k |= 1;
    if (dx > 0.38f) k |= 2;
    if (dy < -0.38f) k |= 8;
    if (dy > 0.38f) k |= 16;
  }
  return k;
}
void oKill(OEn &e, bool byPlayer) {
  e.on = false;
  gBoomAt(oX(e.a, e.r), oY(e.a, e.r));
  sfxBoom();
  if (!byPlayer) return;
  static const uint8_t PTS[3] = { 50, 80, 100 };
  gScore += (e.state == 3 ? 2 : 1) * PTS[e.type];
  if (e.carrier) { oCap.on = true; oCap.a = e.a; oCap.r = max(0.2f, e.r); }
}
void oStep() {
  gFrame++;
  uint8_t k = gDemo ? oAuto() : keysPs4;
  // etoiles : toujours en mouvement (plus vite pendant l'annonce du stage : saut en hyperespace)
  float warp = gStageT > 0 ? 4.0f : 1.0f;
  for (auto &s : oStar) {
    s.r += warp * (0.004f + s.r * 0.03f);
    if (s.r > 1.25f) { s.r = 0.02f; s.a = random(6283) / 1000.0f; }
  }
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) oReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      for (auto &b : oBul) b.on = false;
      for (auto &e : oEn) if (e.on && e.state == 3) { e.state = 4; e.r = 0; }
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  // vaisseau : glisse sur l'orbite vers la direction poussee
  int dx = ((k & 2) ? 1 : 0) - ((k & 1) ? 1 : 0), dy = ((k & 16) ? 1 : 0) - ((k & 8) ? 1 : 0);
  float sp = 0.075f;
  if (!gDemo && padDigital && dx && !dy) {  // croix numerique gauche / droite : tourne en continu (tour complet)
    oPa += dx < 0 ? sp : -sp;
    if (oPa > 6.2832f) oPa -= 6.2832f;
    if (oPa < 0) oPa += 6.2832f;
  } else if (dx || dy) {  // stick (ou croix haut / bas / diagonale) : glisse vers la direction poussee
    float d = oAngDiff(atan2f(dy, dx), oPa);
    oPa += d > sp ? sp : d < -sp ? -sp : d;
    if (oPa > 6.2832f) oPa -= 6.2832f;
    if (oPa < 0) oPa += 6.2832f;
  }
  if (oCool > 0) oCool--;
  if ((k & 4) && oCool == 0) { oFire(); oCool = 7; }
  for (auto &s : oShot) {  // tirs vers le centre (ralentissent avec la profondeur)
    if (!s.on) continue;
    s.r -= 0.02f + 0.035f * s.r;
    if (s.r < 0.06f) s.on = false;
  }
  oRot += 0.006f;
  int diff = gEffDiff();
  int alive = 0;
  for (auto &e : oEn) {
    if (!e.on) continue;
    alive++;
    e.t++;
    if (e.state == 0) {  // en attente au centre
      if (e.t >= 0) { e.state = 1; e.t = 0; }
      continue;
    }
    float fr = 0.44f + ((&e - oEn) & 1) * 0.11f;  // anneau sur 2 rangs
    if (e.state == 1) {  // sortie en spirale : va presque jusqu'au bord puis revient se ranger
      float u = min(1.0f, e.t / 100.0f);
      e.r = fr * u + 0.5f * sinf(3.1416f * u);
      e.a = e.a0 + e.dir * 4.4f * u;
      if (u >= 1.0f) { e.state = 2; e.sa = e.a - oRot; }
    } else if (e.state == 2) {  // dans l'anneau : tourne doucement et respire
      e.a = e.sa + oRot;
      e.r = fr + 0.04f * sinf(gFrame * 0.05f + e.sa);
    } else if (e.state == 3) {  // pique vers le vaisseau
      e.r += 0.006f + 0.016f * e.r + 0.002f * diff;
      float d = oAngDiff(oPa, e.a);
      e.a += d > 0.025f ? 0.025f : d < -0.025f ? -0.025f : d;
      if (!e.shot && e.r > 0.45f) {
        e.shot = true;
        if (random(100) < 45 + 20 * diff)
          for (auto &b : oBul) if (!b.on) { b.on = true; b.a = e.a; b.r = e.r; break; }
      }
      if (e.r > 1.2f) { e.state = 4; e.r = 0; }
    } else if (e.state == 4) {  // ressort du centre et reprend sa place
      e.a = e.sa + oRot;
      e.r += 0.01f;
      if (e.r >= fr) e.state = 2;
    }
    // touche par un tir ?
    for (auto &s : oShot) {
      if (!s.on || fabsf(s.r - e.r) > 0.07f) continue;
      if (fabsf(oAngDiff(s.a, e.a)) * max(e.r, 0.15f) < 0.075f) { s.on = false; oKill(e, true); break; }
    }
    if (!e.on) continue;
    if (e.state == 3 && e.r > 0.9f && e.r < 1.08f && fabsf(oAngDiff(e.a, oPa)) < 0.16f) {  // collision
      oKill(e, false);
      oPower = 0;
      altLoseLife(oX(oPa, 1), oY(oPa, 1));
      return;
    }
  }
  // un ennemi de l'anneau pique de temps en temps (plus souvent avec les stages et la difficulte)
  if (--oDiveT <= 0) {
    oDiveT = max(25, 100 - 20 * diff - 6 * gWave) + random(40);
    int n = 0, pick = -1;
    for (int i = 0; i < O_EN; i++)
      if (oEn[i].on && oEn[i].state == 2 && random(++n) == 0) pick = i;
    if (pick >= 0) { oEn[pick].state = 3; oEn[pick].shot = false; sfxDive(); }
  }
  for (auto &b : oBul) {  // tirs ennemis vers l'exterieur
    if (!b.on) continue;
    b.r += 0.012f + 0.018f * b.r;
    if (b.r > 0.95f && b.r < 1.06f && fabsf(oAngDiff(b.a, oPa)) < 0.1f) {
      b.on = false;
      oPower = 0;
      altLoseLife(oX(oPa, 1), oY(oPa, 1));
      return;
    }
    if (b.r > 1.2f) b.on = false;
  }
  if (oCap.on) {  // capsule : double tir si on la rattrape sur l'orbite
    oCap.r += 0.012f;
    if (oCap.r > 0.92f && fabsf(oAngDiff(oCap.a, oPa)) < 0.2f) { oCap.on = false; oPower = 1; gScore += 500; sndMelody(JINGLE_STAGE, 3); }
    if (oCap.r > 1.15f) oCap.on = false;
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  if (alive == 0) {  // stage gagne : saut en hyperespace vers le suivant
    gWave++;
    gStageT = 100;
    altStageClear(1000UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    oNewStage();
  }
}
void oDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  for (int i = 0; i < O_STARS; i++) {  // etoiles : plus claires en s'approchant
    OShot &s = oStar[i];
    int x = oX(s.a, s.r), y = oY(s.a, s.r);
    if (y < 8) continue;
    uint8_t g = 40 + min(1.0f, s.r) * 200;
    gPix(x, y, (i & 3) == 0 ? rgb(g / 2, g / 2, g) : rgb(g, g, g));
  }
  // ennemis : 1 point au loin, une petite croix, puis le sprite 5x5 (2 images)
  static const uint8_t SPR[3][2][5] = {
    { { 0x0E, 0x1F, 0x15, 0x1F, 0x0A }, { 0x0E, 0x1F, 0x15, 0x1F, 0x11 } },  // meduse
    { { 0x0E, 0x11, 0x15, 0x11, 0x0E }, { 0x0E, 0x1B, 0x11, 0x1B, 0x0E } },  // disque
    { { 0x11, 0x1B, 0x0E, 0x1B, 0x11 }, { 0x0A, 0x1B, 0x0E, 0x1B, 0x0A } }   // papillon
  };
  // couleurs differentes a chaque stage (4 jeux de couleurs qui reviennent en boucle)
  static const uint8_t EC4[4][3][3] = { { { 255, 60, 200 }, { 0, 220, 255 }, { 255, 200, 0 } },
                                        { { 40, 230, 80 }, { 255, 140, 0 }, { 40, 120, 255 } },
                                        { { 255, 220, 0 }, { 170, 70, 255 }, { 255, 50, 60 } },
                                        { { 0, 220, 255 }, { 255, 60, 200 }, { 40, 230, 80 } } };
  static const char MAINC[3] = { 'p', 'c', 'y' }, SWC[4][3] = { { 'p', 'c', 'y' }, { 'g', 'o', 'b' }, { 'y', 'v', 'r' }, { 'c', 'p', 'g' } };
  int st = (gWave - 1) & 3;
  for (auto &e : oEn) {
    if (!e.on || e.state == 0) continue;
    int x = oX(e.a, e.r), y = oY(e.a, e.r);
    const uint8_t *ec = EC4[st][e.type];
    uint16_t c = e.carrier ? ((gFrame & 8) ? palFull[0] : rgb(150, 150, 150)) : rgb(ec[0], ec[1], ec[2]);
    int fr = (gFrame / 8) & 1;
    if (e.r < 0.18f) gPix(x, y, c);
    else if (e.r < 0.36f) { gPix(x, y, palFull[0]); gPix(x - 1, y, c); gPix(x + 1, y, c); gPix(x, y - 1, c); gPix(x, y + 1, c); }
    else if (e.r >= 0.55f) {  // de pres : sprite 7x7 en couleurs (la couleur principale change avec le stage)
      if (e.carrier) gSprite(O_SAT[fr], 7, 7, x - 3, y - 3, palFull, 0, 0);
      else gSprite(O_SPR[e.type][fr], 7, 7, x - 3, y - 3, palFull, MAINC[e.type], SWC[st][e.type]);
    } else {
      const uint8_t *sp = SPR[e.type][(gFrame / 8) & 1];
      for (int r = 0; r < 5; r++)
        for (int cc = 0; cc < 5; cc++)
          if (sp[r] & (0x10 >> cc)) { int px = x - 2 + cc, py = y - 2 + r; if (py >= 8 && px >= 0 && px < W && py < H) gfx->drawPixel(px, py, c); }
      gMark(x - 2, y - 2, 5, 5);
    }
  }
  for (auto &s : oShot) {  // tirs : petit trait jaune dans l'axe
    if (!s.on) continue;
    gPix(oX(s.a, s.r), oY(s.a, s.r), palFull[2]);
    gPix(oX(s.a, s.r + 0.04f), oY(s.a, s.r + 0.04f), palFull[0]);
  }
  for (auto &b : oBul) {
    if (!b.on) continue;
    int x = oX(b.a, b.r), y = oY(b.a, b.r);
    gPix(x, y, palFull[1]);
    if (b.r > 0.5f) { gPix(x + 1, y, palFull[1]); gPix(x, y + 1, palFull[0]); }
  }
  if (oCap.on) {  // capsule : losange qui clignote
    int x = oX(oCap.a, oCap.r), y = oY(oCap.a, oCap.r);
    uint16_t c = (gFrame & 4) ? palFull[2] : palFull[5];
    gPix(x, y - 1, c); gPix(x - 1, y, c); gPix(x + 1, y, c); gPix(x, y + 1, c); gPix(x, y, palFull[0]);
  }
  if (gDead == 0) {  // vaisseau : fleche tournee vers le centre
    float px = oX(oPa, 1), py = oY(oPa, 1);
    float ux = ocx() - px, uy = ocy() - py, n = sqrtf(ux * ux + uy * uy);
    ux /= n; uy /= n;
    float vx = -uy, vy = ux;
    int nx = px + 3 * ux, ny = py + 3 * uy;
    int lx = px - 2 * ux + 3 * vx, ly = py - 2 * uy + 3 * vy, rx = px - 2 * ux - 3 * vx, ry = py - 2 * uy - 3 * vy;
    uint16_t edge = oPower ? palFull[5] : palFull[4], back = oPower ? rgb(20, 120, 40) : rgb(60, 110, 255);
    gfx->drawLine(nx, ny, lx, ly, edge);   // bords avant cyan (verts avec le double tir)
    gfx->drawLine(nx, ny, rx, ry, edge);
    gfx->drawLine(lx, ly, (int)px, (int)py, back);
    gfx->drawLine(rx, ry, (int)px, (int)py, back);
    gfx->drawPixel(lx, ly, palFull[1]);    // feux rouges au bout des ailes
    gfx->drawPixel(rx, ry, palFull[1]);
    gfx->drawPixel(px + ux, py + uy, palFull[0]);       // cockpit
    gfx->drawPixel(px - 3 * ux, py - 3 * uy, (gFrame & 2) ? palFull[2] : palFull[7]);  // reacteur qui scintille
    gMark((int)px - 5, (int)py - 5, 11, 11);
  }
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- BRIQUES
// Casse-briques original : une raquette en bas (stick ou croix gauche / droite), X lance la balle.
// Briques de couleur (1 coup), d'argent (2 ou 3 coups), d'or (incassables, a partir du stage 4).
// Capsules qui tombent : cyan raquette large, orange balle lente, magenta 3 balles, rouge une vie.
// Tableau vide (sauf l'or) : stage suivant, 6 dessins qui reviennent en boucle, balle plus rapide.
#define K_MAXC 25
#define K_MAXR 12
#define K_BALLS 3
#define K_CAPS 4
struct KMem { uint8_t brick[K_MAXR][K_MAXC], hp[K_MAXR][K_MAXC]; };
#define kBrick (GMEM(KMem)->brick)  // 0 vide, 1 a 6 couleur, 7 argent (vie dans kHp), 8 or
#define kHp (GMEM(KMem)->hp)
KBall kBall[K_BALLS];
KCap kCap[K_CAPS];
int kCols = 12, kRows = 6, kOX = 4, kOY = 11, kLeft = 0, kWide = 0, kSlow = 0, kStuck = 0, kLaser = 0, kLzCool = 0;
struct KLz { float x, y; bool on; } kLz[6];  // tirs de la raquette (capsule jaune)
float kPX = 0, kPW = 18;
const uint8_t K_COL[9][3] = { { 0, 0, 0 }, { 255, 50, 50 }, { 255, 150, 0 }, { 255, 220, 0 }, { 40, 220, 80 }, { 0, 200, 255 },
                              { 200, 70, 255 }, { 170, 170, 185 }, { 230, 180, 40 } };
int kPY() { return H - 4; }  // haut de la raquette (3 rangees)
void kBuild() {  // dessin du tableau selon le stage
  int pat = (gWave - 1) % 6;
  kLeft = 0;
  for (int r = 0; r < K_MAXR; r++)
    for (int c = 0; c < K_MAXC; c++) {
      uint8_t b = 0;
      if (r < kRows && c < kCols) {
        int mc = c < kCols / 2 ? c : kCols - 1 - c;  // dessins symetriques
        int col = 1 + r % 6;
        switch (pat) {
          case 0: b = col; break;                                                   // mur plein
          case 1: b = ((r + c) & 1) ? col : 0; break;                               // damier
          case 2: b = mc >= kRows - 1 - r ? col : 0; break;                         // pyramide
          case 3: b = abs(mc - kCols / 2 + 1) + abs(r - kRows / 2) <= kRows / 2 + 1 ? col : 0; break;  // losange
          case 4: b = r == 1 ? 7 : (r & 1) ? 0 : col; break;                        // rangees avec argent
          default: b = ((mc * 7 + r * 3 + gWave) % 5) < 3 ? col : (r == 0 ? 7 : 0); break;  // motif
        }
        if (b && b < 7 && gWave >= 3 && r == 0) b = 7;                            // rangee d'argent en haut
        if (b && gWave >= 4 && r == kRows - 1 && (c % 5) == 2) b = 8;            // quelques briques d'or
      }
      kBrick[r][c] = b;
      kHp[r][c] = b == 7 ? (gWave >= 6 ? 3 : 2) : 1;
      if (b && b != 8) kLeft++;
    }
}
void kNewBall() {  // une balle posee sur la raquette, lancee avec X
  for (auto &b : kBall) b.on = false;
  kBall[0].on = true;
  kStuck = 1;
  kWide = kSlow = kLaser = 0;
  for (auto &z : kLz) z.on = false;
  kPW = W >= 256 ? 26 : 18;
  for (auto &cp : kCap) cp.on = false;
}
float kSpeed() { return (1.3f + 0.1f * min(gWave, 10) + 0.15f * (gEffDiff() - 1)) * (kSlow ? 0.7f : 1.0f); }
void kLaunch() {
  KBall &b = kBall[0];
  float a = (random(2) ? 1 : -1) * (0.3f + random(25) / 100.0f), sp = kSpeed();
  b.vx = sinf(a) * sp; b.vy = -cosf(a) * sp;
  kStuck = 0;
  sfxShot();
}
void kReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  kCols = min(K_MAXC, (W - 8) / 10);
  kOX = (W - kCols * 10) / 2 + 1;
  kRows = H >= 128 ? 10 : 6;
  kOY = H >= 128 ? 14 : 11;
  kPX = W / 2 - 9;
  kBuild();
  kNewBall();
}
uint8_t kAuto() {  // demo : la raquette suit la balle la plus basse qui descend, et lance la balle
  float tx = W / 2, best = -1;
  for (auto &b : kBall)
    if (b.on && b.vy > 0 && b.y > best) { best = b.y; tx = b.x + 1; }
  if (best < 0) for (auto &b : kBall) if (b.on) tx = b.x + 1;
  float c = kPX + kPW / 2 + 3 * sinf(gFrame * 0.05f);  // touche un peu de cote : angles varies
  uint8_t k = tx < c - 2 ? 1 : tx > c + 2 ? 2 : 0;
  if (kStuck && kStuck > 40) k |= 4;
  return k;
}
void kHitBrick(int r, int c) {
  uint8_t &b = kBrick[r][c];
  if (b == 8) { sfxMenu(); return; }  // or : incassable
  if (--kHp[r][c] > 0) { sndPlay(1800, 1500, 40, 0.25f, false, 1); return; }  // argent : il faut encore frapper
  gScore += b == 7 ? 50 : 10 * (kRows - r) + 10;
  int x = kOX + c * 10, y = kOY + r * 4;
  gBoomAt(x + 4, y + 1);
  gMark(x, y, 10, 4);  // la brique et son ombre disparaissent (effacees a la prochaine image)
  if (random(100) < 12)
    for (auto &cp : kCap)
      if (!cp.on) { int r = random(100); cp.on = true; cp.x = x + 2; cp.y = y; cp.type = r < 8 ? 3 : r < 30 ? 4 : random(3); break; }  // 4 : tir
  b = 0;
  kLeft--;
  sfxBoom();
}
void kStep() {
  gFrame++;
  uint8_t k = gDemo ? kAuto() : keysPs4;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) kReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      kNewBall();
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  float sp = W > 128 ? 3.4f : 2.6f;
  if (!gDemo && padStickX) kPX += sp * 1.3f * padStickX / 127.0f;  // stick : vitesse selon l'inclinaison
  else {
    if (k & 1) kPX -= sp;
    if (k & 2) kPX += sp;
  }
  kPW = (W >= 256 ? 26 : 18) + (kWide ? 10 : 0);
  kPX = constrain(kPX, 0.0f, W - kPW);
  if (kWide > 0) kWide--;
  if (kLaser > 0) kLaser--;
  if (kLzCool > 0) kLzCool--;
  if (kLaser && !kStuck && (k & 4) && kLzCool == 0) {  // X : les deux canons tirent
    for (int s = 0; s < 2; s++)
      for (auto &z : kLz)
        if (!z.on) { z.on = true; z.x = kPX + (s ? kPW - 2 : 1); z.y = kPY() - 3; break; }
    kLzCool = 12;
    sndPlay(1800, 900, 50, 0.25f, false, 1);
  }
  for (auto &z : kLz) {
    if (!z.on) continue;
    z.y -= 3;
    int c = (int)floorf((z.x - kOX) / 10), r = (int)floorf((z.y - kOY) / 4);
    if (z.y < 8) z.on = false;
    else if (r >= 0 && r < kRows && c >= 0 && c < kCols && kBrick[r][c]) { z.on = false; kHitBrick(r, c); }
  }
  if (kSlow > 0) kSlow--;
  int py = kPY();
  if (kStuck) {  // balle posee : suit la raquette
    kStuck++;
    kBall[0].x = kPX + kPW / 2 - 1; kBall[0].y = py - 2;
    if ((k & (4 | 32)) || kStuck > 300) kLaunch();
  } else {
    int alive = 0;
    for (auto &b : kBall) {
      if (!b.on) continue;
      float sp2 = sqrtf(b.vx * b.vx + b.vy * b.vy), want = kSpeed();  // vitesse ramenee doucement a la normale
      if (fabsf(sp2 - want) > 0.05f) { b.vx *= want / sp2; b.vy *= want / sp2; }
      int n = (int)ceilf(max(fabsf(b.vx), fabsf(b.vy)));
      for (int s = 0; s < n && b.on; s++) {  // petits pas : la balle ne traverse pas les briques
        b.x += b.vx / n;
        if (b.x < 0) { b.x = 0; b.vx = fabsf(b.vx); }
        if (b.x > W - 2) { b.x = W - 2; b.vx = -fabsf(b.vx); }
        int c = (int)floorf((b.x + 1 - kOX) / 10), r = (int)floorf((b.y + 1 - kOY) / 4);
        if (r >= 0 && r < kRows && c >= 0 && c < kCols && kBrick[r][c]) { b.x -= b.vx / n; b.vx = -b.vx; kHitBrick(r, c); }
        b.y += b.vy / n;
        if (b.y < 8) { b.y = 8; b.vy = fabsf(b.vy); }
        c = (int)floorf((b.x + 1 - kOX) / 10); r = (int)floorf((b.y + 1 - kOY) / 4);
        if (r >= 0 && r < kRows && c >= 0 && c < kCols && kBrick[r][c]) { b.y -= b.vy / n; b.vy = -b.vy; kHitBrick(r, c); }
        if (b.vy > 0 && b.y + 2 >= py && b.y + 2 <= py + 2 && b.x + 2 >= kPX && b.x <= kPX + kPW) {  // raquette
          float rel = constrain((b.x + 1 - (kPX + kPW / 2)) / (kPW / 2), -1.0f, 1.0f), v = sqrtf(b.vx * b.vx + b.vy * b.vy);
          b.vx = sinf(rel * 1.05f) * v; b.vy = -max(0.5f, cosf(rel * 1.05f)) * v;
          b.y = py - 2;
          sndPlay(700, 900, 40, 0.3f, false, 1);
        }
        if (b.y > H) b.on = false;
      }
      alive += b.on;
    }
    if (!alive) { altLoseLife(kPX + kPW / 2, py); return; }
  }
  for (auto &cp : kCap) {  // capsules
    if (!cp.on) continue;
    cp.y += 0.6f;
    if (cp.y + 3 >= py && cp.y <= py + 2 && cp.x + 5 >= kPX && cp.x <= kPX + kPW) {
      cp.on = false;
      gScore += 100;
      sndMelody(JINGLE_STAGE, 3);
      if (cp.type == 4) kLaser = 600;  // tir pendant 15 s
      else if (cp.type == 0) kWide = 900;
      else if (cp.type == 1) kSlow = 600;
      else if (cp.type == 2) {  // 3 balles
        KBall src = kBall[0];
        for (auto &b : kBall) if (b.on) { src = b; break; }
        for (int i = 0; i < K_BALLS; i++)
          if (!kBall[i].on) { kBall[i] = src; kBall[i].on = true; kBall[i].vx = src.vx * (i == 1 ? 0.6f : -1.0f) + (i == 1 ? 0.8f : 0); }
      } else if (gLives < 9) gLives++;
    }
    if (cp.y > H) cp.on = false;
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  if (kLeft <= 0) {  // tableau fini
    gWave++;
    gStageT = 100;
    altStageClear(500UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    kBuild();
    kNewBall();
  }
}
void kDraw() {
  eraseOld();
  uint16_t palFull[9];
  altBackground(0.04f, palFull);
  const uint16_t kShadow = rgb(6, 6, 12);
  for (int r = 0; r < kRows; r++)  // briques en relief : arete claire, reflet, bords dans l'ombre, ombre portee ; eclat sur l'argent et l'or
    for (int c = 0; c < kCols; c++) {
      uint8_t b = kBrick[r][c];
      if (!b) continue;
      int x = kOX + c * 10, y = kOY + r * 4;
      const uint8_t *cc = K_COL[b];
      bool dmg = b == 7 && kHp[r][c] < (gWave >= 6 ? 3 : 2);
      int lv = dmg ? 7 : 9;  // une brique d'argent abimee est plus terne
      auto tone = [&](int k, int add) {  // k : dixiemes de la couleur, add : eclaircissement
        return rgb(min(255, cc[0] * k * lv / 90 + add), min(255, cc[1] * k * lv / 90 + add), min(255, cc[2] * k * lv / 90 + add));
      };
      uint16_t hi = tone(12, 50), base = tone(8, 0), side = tone(5, 0), low = tone(3, 0);
      gfx->drawFastHLine(x + 1, y + 3, 9, kShadow);  // ombre portee, dans l'espace entre les briques
      gfx->drawFastVLine(x + 9, y + 1, 2, kShadow);
      gfx->drawFastHLine(x, y, 8, hi);  // arete du haut
      gfx->drawPixel(x + 8, y, base);
      gfx->drawPixel(x, y + 1, tone(10, 25));  // bord gauche eclaire
      gfx->drawFastHLine(x + 1, y + 1, 7, base);
      gfx->drawPixel(x + 8, y + 1, side);  // bord droit dans l'ombre
      gfx->drawPixel(x, y + 2, side);
      gfx->drawFastHLine(x + 1, y + 2, 8, low);  // dessous dans l'ombre
      gfx->drawPixel(x + 2, y + 1, tone(6, 150));  // reflet brillant
      gfx->drawPixel(x + 3, y + 1, tone(8, 70));
      if (b >= 7) {  // argent et or : un eclat passe en diagonale
        int s = (gFrame / 2 + r * 3 + c * 5) % 48;
        for (int k = 0; k < 3; k++) {
          int px = x + s - k;
          if (px >= x && px < x + 9) gfx->drawPixel(px, y + k, k == 1 ? rgb(255, 255, 255) : tone(8, 120));
        }
      }
      if (dmg) {  // fissure
        gfx->drawPixel(x + 5, y, low);
        gfx->drawPixel(x + 4, y + 1, kShadow);
        gfx->drawPixel(x + 5, y + 2, kShadow);
      }
    }
  int py = kPY();
  // raquette : 3 rangees en degrade (reflet en haut), embouts rouges arrondis, centre cyan lumineux ; canons jaunes avec la capsule de tir
  int px = (int)kPX, pw = (int)kPW;
  gfx->drawFastHLine(px + 1, py, pw - 2, rgb(225, 230, 245));
  gfx->drawFastHLine(px, py + 1, pw, rgb(140, 150, 175));
  gfx->drawFastHLine(px + 1, py + 2, pw - 2, rgb(60, 66, 90));
  gfx->drawFastHLine(px + 1, py, 3, rgb(255, 130, 130));  // embouts
  gfx->drawFastHLine(px, py + 1, 4, rgb(230, 40, 40));
  gfx->drawFastHLine(px + 1, py + 2, 3, rgb(120, 20, 20));
  gfx->drawFastHLine(px + pw - 4, py, 3, rgb(255, 130, 130));
  gfx->drawFastHLine(px + pw - 4, py + 1, 4, rgb(230, 40, 40));
  gfx->drawFastHLine(px + pw - 4, py + 2, 3, rgb(120, 20, 20));
  gfx->drawFastHLine(px + pw / 2 - 3, py + 1, 6, (gFrame & 8) ? palFull[4] : rgb(120, 255, 255));  // centre qui pulse
  if (kLaser) {
    gfx->drawFastVLine(px + 1, py - 2, 2, palFull[2]);
    gfx->drawFastVLine(px + pw - 2, py - 2, 2, palFull[2]);
  }
  gMark(px, py - 2, pw, 5);
  for (auto &z : kLz)
    if (z.on) { gfx->drawFastVLine((int)z.x, (int)z.y, 3, palFull[2]); gMark((int)z.x, (int)z.y, 1, 3); }
  for (auto &b : kBall)
    if (b.on && gDead == 0) {
      gfx->fillRect((int)b.x, (int)b.y, 2, 2, palFull[0]);
      gMark((int)b.x, (int)b.y, 2, 2);
    }
  static const uint8_t CC[5][3] = { { 0, 220, 255 }, { 255, 140, 0 }, { 220, 60, 220 }, { 255, 40, 40 }, { 255, 220, 0 } };
  for (auto &cp : kCap) {  // capsules qui clignotent
    if (!cp.on) continue;
    int x = (int)cp.x, y = (int)cp.y;
    gfx->fillRect(x + 1, y, 4, 3, rgb(CC[cp.type][0], CC[cp.type][1], CC[cp.type][2]));
    gfx->drawFastVLine(x, y + 1, 1, rgb(CC[cp.type][0], CC[cp.type][1], CC[cp.type][2]));
    gfx->drawFastVLine(x + 5, y + 1, 1, rgb(CC[cp.type][0], CC[cp.type][1], CC[cp.type][2]));
    if (gFrame & 8) gfx->drawPixel(x + 2, y + 1, palFull[0]);
    gMark(x, y, 6, 3);
  }
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- BLOCS
// Jeu de blocs qui tombent (original) : gauche / droite deplacent la piece, bas accelere, haut la fait tomber d'un coup,
// X tourne dans un sens, rond dans l'autre. Une ligne complete disparait (flash). 10 lignes : niveau suivant, plus rapide.
// Pile jusqu'en haut : une vie perdue, le plateau se vide. Piece fantome : ou la piece va tomber.
#define T_COLS 10
#define T_MAXR 22
const uint16_t T_SHAPE[7][4] = {  // 4x4, bit 15 = ligne 0 colonne 0
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },  // barre
  { 0x6600, 0x6600, 0x6600, 0x6600 },  // carre
  { 0x4E00, 0x4640, 0x0E40, 0x4C40 },  // T
  { 0x6C00, 0x4620, 0x06C0, 0x8C40 },  // S
  { 0xC600, 0x2640, 0x0C60, 0x4C80 },  // Z
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 },  // J
  { 0x2E00, 0x4460, 0x0E80, 0xC440 }   // L
};
const uint8_t T_COL[8][3] = { { 0, 0, 0 }, { 0, 220, 255 }, { 255, 220, 0 }, { 200, 70, 255 }, { 40, 220, 80 }, { 255, 50, 50 },
                              { 50, 100, 255 }, { 255, 140, 0 } };
struct TMem { uint8_t b[T_MAXR][T_COLS], save[T_MAXR][T_COLS]; };
#define tB (GMEM(TMem)->b)
int tRows = 18, tC = 3, tBX = 0, tBY = 8, tPiece = 0, tNext = 0, tRot = 0, tX = 3, tY = 0, tFall = 0, tLock = 0, tLines = 0;
int tFlash = 0, tDas = 0, tAiRot = 0, tAiX = 3, tAiT = 0;
uint8_t tBag[7], tBagN = 0, tPrevK = 0;
bool tCell(int p, int rot, int r, int c) { return T_SHAPE[p][rot & 3] & (0x8000 >> (r * 4 + c)); }
bool tFits(int p, int rot, int x, int y) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tCell(p, rot, r, c)) {
        int bx = x + c, by = y + r;
        if (bx < 0 || bx >= T_COLS || by >= tRows) return false;
        if (by >= 0 && tB[by][bx]) return false;
      }
  return true;
}
int tDraw7() {  // sac de 7 pieces melangees : jamais trop longtemps sans la barre
  if (tBagN == 0) {
    for (int i = 0; i < 7; i++) tBag[i] = i;
    for (int i = 6; i > 0; i--) { int j = random(i + 1); uint8_t t = tBag[i]; tBag[i] = tBag[j]; tBag[j] = t; }
    tBagN = 7;
  }
  return tBag[--tBagN];
}
int tDropY(int p, int rot, int x, int y) { while (tFits(p, rot, x, y + 1)) y++; return y; }
// demo : cherche la meilleure place (hauteur, trous, lignes, relief)
void tAiPlan() {
  float best = -1e9;
  tAiRot = tRot; tAiX = tX;
  auto &save = GMEM(TMem)->save;
  for (int rot = 0; rot < 4; rot++)
    for (int x = -2; x < T_COLS; x++) {
      if (!tFits(tPiece, rot, x, 0)) continue;
      int y = tDropY(tPiece, rot, x, 0);
      memcpy(save, tB, sizeof(tB));
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          if (tCell(tPiece, rot, r, c) && y + r >= 0) tB[y + r][x + c] = 1;
      int lines = 0, holes = 0, agg = 0, bump = 0, prevH = -1;
      for (int r = 0; r < tRows; r++) { bool full = true; for (int c = 0; c < T_COLS; c++) full &= tB[r][c] != 0; lines += full; }
      for (int c = 0; c < T_COLS; c++) {
        int h = 0; bool seen = false;
        for (int r = 0; r < tRows; r++) {
          if (tB[r][c]) { if (!seen) { h = tRows - r; seen = true; } }
          else if (seen) holes++;
        }
        agg += h;
        if (prevH >= 0) bump += abs(h - prevH);
        prevH = h;
      }
      float s = -0.51f * agg + 0.76f * lines * 3 - 0.36f * holes * 2 - 0.18f * bump;
      if (s > best) { best = s; tAiRot = rot; tAiX = x; }
      memcpy(tB, save, sizeof(tB));
    }
}
void tSpawn() {
  tPiece = tNext;
  tNext = tDraw7();
  tRot = 0; tX = 3; tY = -1; tFall = 0; tLock = 0;
  if (!tFits(tPiece, tRot, tX, tY)) {  // plus de place : une vie perdue, le plateau se vide
    altLoseLife(tBX + T_COLS * tC / 2, tBY + 4);
    return;
  }
  if (gDemo) { tAiPlan(); tAiT = 0; }
}
void tClearBoard() { memset(tB, 0, sizeof(tB)); }
void tReset() {
  gLives = 1;  // une seule partie : la pile en haut termine la partie
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 80;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  tC = max(3, min((H - 8) / 20, (W / 2) / T_COLS));
  tRows = min(20, (H - 8) / tC);
  tBX = (W - T_COLS * tC) / 2;
  tBY = 8 + ((H - 8) - tRows * tC) / 2;
  tLines = 0; tFlash = 0; tBagN = 0;
  tClearBoard();
  tNext = tDraw7();
  tSpawn();
}
bool tTryRotate(int dir) {
  int nr = (tRot + dir + 4) & 3;
  static const int8_t KICK[5] = { 0, -1, 1, -2, 2 };
  for (int k = 0; k < 5; k++)
    if (tFits(tPiece, nr, tX + KICK[k], tY)) { tRot = nr; tX += KICK[k]; tLock = min(tLock, 10); return true; }
  return false;
}
void tLockPiece() {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      if (tCell(tPiece, tRot, r, c) && tY + r >= 0) tB[tY + r][tX + c] = tPiece + 1;
  int full = 0;
  for (int r = 0; r < tRows; r++) {
    bool f = true;
    for (int c = 0; c < T_COLS; c++) f &= tB[r][c] != 0;
    if (f) { full++; for (int c = 0; c < T_COLS; c++) tB[r][c] = 8; }  // 8 : ligne qui clignote avant de disparaitre
  }
  if (full) {
    static const uint16_t PTS[5] = { 0, 100, 300, 500, 800 };
    gScore += (uint32_t)PTS[full] * gWave;
    tLines += full;
    tFlash = 14;
    if (full == 4) { sndMelody(JINGLE_STAGE, 3); if (!gDemo) rumbleReq = true; }
    else sfxBoom();
  } else {
    sndPlay(300, 200, 50, 0.3f, false, 1);
    tSpawn();
  }
}
void tStep() {
  gFrame++;
  uint8_t k = gDemo ? 0 : keysPs4;
  uint8_t edge = k & ~tPrevK;
  tPrevK = k;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) tReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      tClearBoard();
      tSpawn();
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  if (tFlash > 0) {  // lignes qui clignotent puis disparaissent
    if (--tFlash == 0) {
      for (int r = 0; r < tRows; r++)
        if (tB[r][0] == 8) {
          for (int rr = r; rr > 0; rr--) memcpy(tB[rr], tB[rr - 1], T_COLS);
          memset(tB[0], 0, T_COLS);
        }
      int lvl = 1 + tLines / 10;
      if (lvl > gWave) {  // niveau suivant : plus rapide, une vie en plus
        gWave = lvl;
        gStageT = 70;
        altStageClear(200UL * gWave);
        sndMelody(JINGLE_STAGE, 4);
      }
      tSpawn();
    }
    return;
  }
  bool soft = false;
  if (gDemo) {  // pilote automatique : tourne, se place, puis descend vite
    if (++tAiT % 4 == 0) {
      if (tRot != tAiRot) tTryRotate(1);
      else if (tX < tAiX && tFits(tPiece, tRot, tX + 1, tY)) tX++;
      else if (tX > tAiX && tFits(tPiece, tRot, tX - 1, tY)) tX--;
    }
    soft = tRot == tAiRot && tX == tAiX;
  } else {
    if (edge & 4) tTryRotate(1);
    if (edge & 32) tTryRotate(-1);
    int dx = (k & 1) ? -1 : (k & 2) ? 1 : 0;
    if (dx) {
      if ((edge & 3) || (++tDas > 8 && (tDas & 1))) {  // appui : un pas, puis repetition rapide
        if (tFits(tPiece, tRot, tX + dx, tY)) { tX += dx; tLock = min(tLock, 10); }
      }
    } else tDas = 0;
    if (edge & 8) {  // haut : tombe d'un coup
      int ny = tDropY(tPiece, tRot, tX, tY);
      gScore += 2 * (ny - tY);
      tY = ny;
      tLockPiece();
      return;
    }
    soft = k & 16;
  }
  int every = max(2, 36 - 3 * gWave - 4 * (gEffDiff() - 1));
  if (soft) every = min(every, 2);
  if (++tFall >= every) {
    tFall = 0;
    if (tFits(tPiece, tRot, tX, tY + 1)) { tY++; if (soft && !gDemo) gScore++; tLock = 0; }
  }
  if (!tFits(tPiece, tRot, tX, tY + 1)) {  // posee : petit delai pour la glisser encore
    if (++tLock > 18) tLockPiece();
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
}
void tCellDraw(int x, int y, int s, uint8_t v, bool ghost) {  // bloc en relief : reflet en haut a gauche, ombre en bas a droite
  if (!v) { gfx->fillRect(x, y, s, s, 0); if (s >= 5) gfx->drawPixel(x + s / 2, y + s / 2, rgb(25, 25, 40)); return; }
  const uint8_t *c = T_COL[v == 8 ? 0 : v];
  if (v == 8) { gfx->fillRect(x, y, s, s, (gFrame & 2) ? rgb(255, 255, 255) : rgb(120, 120, 120)); return; }
  if (ghost) { gfx->fillRect(x, y, s, s, 0); gfx->drawRect(x, y, s, s, rgb(c[0] / 3, c[1] / 3, c[2] / 3)); return; }
  gfx->fillRect(x, y, s, s, rgb(c[0] * 3 / 4, c[1] * 3 / 4, c[2] * 3 / 4));
  gfx->drawFastHLine(x, y, s, rgb(min(255, c[0] + 90), min(255, c[1] + 90), min(255, c[2] + 90)));
  gfx->drawFastVLine(x, y, s, rgb(min(255, c[0] + 60), min(255, c[1] + 60), min(255, c[2] + 60)));
  if (s >= 4) {
    gfx->drawFastHLine(x + 1, y + s - 1, s - 1, rgb(c[0] / 3, c[1] / 3, c[2] / 3));
    gfx->drawFastVLine(x + s - 1, y + 1, s - 1, rgb(c[0] / 3, c[1] / 3, c[2] / 3));
  }
}
void tDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  int s = tC, bw = T_COLS * s, bh = tRows * s;
  uint16_t frame = rgb(70, 80, 140);
  gfx->drawRect(tBX - 1, tBY - 1, bw + 2, bh + 2, frame);  // cadre du plateau
  int gy = gDead == 0 && tFlash == 0 ? tDropY(tPiece, tRot, tX, tY) : -99;
  for (int r = 0; r < tRows; r++)
    for (int c = 0; c < T_COLS; c++) {
      uint8_t v = tB[r][c];
      bool ghost = false;
      int pr = r - tY, pc = c - tX, qr = r - gy;
      if (gDead == 0 && tFlash == 0) {
        if (pr >= 0 && pr < 4 && pc >= 0 && pc < 4 && tCell(tPiece, tRot, pr, pc)) v = tPiece + 1;
        else if (!v && qr >= 0 && qr < 4 && pc >= 0 && pc < 4 && tCell(tPiece, tRot, qr, pc)) { v = tPiece + 1; ghost = true; }
      }
      tCellDraw(tBX + c * s, tBY + r * s, s, v, ghost);
    }
  // a gauche : lignes et niveau ; a droite : la piece suivante
  uint16_t lab = rgb(110, 110, 140), val = rgb(230, 230, 230);
  char b[8];
  int lx = max(1, tBX - 6 - tinyWidth("LIGNES")), ly = tBY + 2;
  tinyText(*gfx, lx, ly, "LIGNES", lab);
  snprintf(b, sizeof(b), "%d", tLines);
  tinyText(*gfx, lx, ly + 7, b, val);
  tinyText(*gfx, lx, ly + 16, "NIVEAU", lab);
  snprintf(b, sizeof(b), "%d", gWave);
  tinyText(*gfx, lx, ly + 23, b, val);
  gMark(lx, ly, tinyWidth("NIVEAU") + 2, 30);
  int nx = tBX + bw + 5, ns = max(2, s - 1);
  tinyText(*gfx, nx, ly, "APRES", lab);
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      tCellDraw(nx + c * ns, ly + 8 + r * ns, ns, tCell(tNext, 0, r, c) ? tNext + 1 : 0, false);
  gMark(nx, ly, max(tinyWidth("APRES"), 4 * ns), 8 + 4 * ns);
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- CRUCH
// Bonbons a aligner (original) : la croix ou le stick deplace le curseur, X prend un bonbon, une direction l'echange
// avec son voisin (rond : annuler). 3 identiques alignes ou plus : ils disparaissent, ceux du dessus tombent (chaines).
// 4 alignes : bonbon raye (vide sa ligne ou sa colonne) ; 5 : bombe (echangee, elle vide toute une couleur).
// Chaque niveau : un score a atteindre en un nombre de coups limite ; rate : une vie perdue.
#define Y_N 8
#define Y_EMPTY 0xFF
uint8_t yG[Y_N][Y_N];       // bits 0-2 : sorte (0-5, 6 = bombe) ; bit 4 : raye horizontal ; bit 5 : raye vertical
struct YMem { uint8_t mark[Y_N][Y_N]; float off[Y_N][Y_N]; };
#define yMark (GMEM(YMem)->mark)
#define yOff (GMEM(YMem)->off)  // decalage vertical pendant la chute (pixels)
int yS = 7, yBX = 2, yBY = 8, yCR = 3, yCC = 3, yState = 0, yT = 0, yCombo = 0, yMoves = 20, yGoal = 1500, yRep = 0;
int yAR = 0, yAC = 0, yBR = 0, yBC = 0;  // echange en cours (a <-> b)
uint32_t yStageScore = 0;
bool ySel = false;
uint8_t yPrevK = 0;
const uint8_t Y_COL[7][3] = { { 255, 50, 60 }, { 255, 150, 0 }, { 255, 225, 0 }, { 40, 220, 80 }, { 40, 140, 255 }, { 200, 70, 255 }, { 60, 60, 70 } };
const char *const Y_SPR[6][6] = {  // bonbons 6x6 (petit ecran) : a couleur, h reflet, d ombre
  { ".aaaa.", "ahhaaa", "ahaaaa", "aaaaad", "aaaadd", ".addd." },   // rond
  { "aaaaaa", "ahhaaa", "ahaaaa", "aaaaad", "aaaadd", "adddd." },   // carre
  { "..aa..", ".ahaa.", "ahaaaa", "aaaaad", ".aadd.", "..dd.." },   // losange
  { "..aa..", "..ha..", ".haaa.", ".aaaad", "aaaadd", "dddddd" },   // triangle
  { "..a...", ".aha..", ".haaa.", "ahaaaa", "aaaadd", ".ddd.." },   // goutte
  { "..a...", ".aha..", "aahaaa", ".aaad.", ".a.dd.", "a...d." }    // etoile
};
uint8_t yType(uint8_t v) { return v == Y_EMPTY ? 7 : v & 7; }
bool yRunAt(int r, int c) {  // le bonbon (r, c) fait-il partie d'un alignement de 3 ?
  uint8_t t = yType(yG[r][c]);
  if (t >= 6) return false;
  int h = 1, v = 1;
  for (int k = c - 1; k >= 0 && yType(yG[r][k]) == t; k--) h++;
  for (int k = c + 1; k < Y_N && yType(yG[r][k]) == t; k++) h++;
  for (int k = r - 1; k >= 0 && yType(yG[k][c]) == t; k--) v++;
  for (int k = r + 1; k < Y_N && yType(yG[k][c]) == t; k++) v++;
  return h >= 3 || v >= 3;
}
bool yAnyRun() { for (int r = 0; r < Y_N; r++) for (int c = 0; c < Y_N; c++) if (yRunAt(r, c)) return true; return false; }
void ySwapCells(int r1, int c1, int r2, int c2) { uint8_t t = yG[r1][c1]; yG[r1][c1] = yG[r2][c2]; yG[r2][c2] = t; }
int yGain(int r1, int c1, int r2, int c2) {  // interet d'un echange (pour la demo et pour savoir s'il reste un coup)
  if (yType(yG[r1][c1]) == 6 || yType(yG[r2][c2]) == 6) return 10;
  ySwapCells(r1, c1, r2, c2);
  int n = 0;
  for (int r = 0; r < Y_N; r++) for (int c = 0; c < Y_N; c++) n += yRunAt(r, c);
  ySwapCells(r1, c1, r2, c2);
  return n;
}
bool yBestMove(int &br, int &bc, int &dr, int &dc) {
  int best = 0;
  for (int r = 0; r < Y_N; r++)
    for (int c = 0; c < Y_N; c++)
      for (int d = 0; d < 2; d++) {
        int r2 = r + (d == 1), c2 = c + (d == 0);
        if (r2 >= Y_N || c2 >= Y_N) continue;
        int g = yGain(r, c, r2, c2) * 10 + random(10);
        if (g > best) { best = g; br = r; bc = c; dr = r2 - r; dc = c2 - c; }
      }
  return best >= 30;
}
void yFill() {  // nouveau plateau sans alignement de depart, avec au moins un coup possible
  int br, bc, dr, dc;
  do {
    for (int r = 0; r < Y_N; r++)
      for (int c = 0; c < Y_N; c++) {
        do yG[r][c] = random(6); while (yRunAt(r, c));
        yOff[r][c] = -(float)(Y_N - r) * yS - random(yS);  // les bonbons tombent en place
      }
  } while (!yBestMove(br, bc, dr, dc));
  yState = 4;  // chute
}
void yNewLevel() {
  yMoves = gEffDiff() == 0 ? 25 : gEffDiff() == 2 ? 16 : 20;
  yGoal = 1500 + 700 * (gWave - 1);
  yStageScore = 0;
  yCombo = 0;
  ySel = false;
  yFill();
}
void yReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 80;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  yS = min(14, min((H - 8) / Y_N, (W - 44) / Y_N));
  yBX = H >= 128 ? 4 : 2;
  yBY = 8 + ((H - 8) - Y_N * yS) / 2;
  yCR = yCC = 3;
  yNewLevel();
}
// marque les alignements (et cree les bonbons speciaux), renvoie le nombre de bonbons qui vont disparaitre
int yFindMatches() {
  memset(yMark, 0, sizeof(yMark));
  int n = 0;
  for (int r = 0; r < Y_N; r++)
    for (int c = 0; c < Y_N; c++)
      if (yRunAt(r, c)) { yMark[r][c] = 1; n++; }
  // rayures et bombes : un alignement de 4 ou 5 laisse un bonbon special a la place de l'echange (ou au milieu)
  for (int pass = 0; pass < 2; pass++)
    for (int a = 0; a < Y_N; a++) {
      int run = 1;
      for (int b = 1; b <= Y_N; b++) {
        int r = pass ? b : a, c = pass ? a : b, pr = pass ? b - 1 : a, pc = pass ? a : b - 1;
        bool same = b < Y_N && yType(yG[r][c]) == yType(yG[pr][pc]) && yType(yG[pr][pc]) < 6;
        if (same) { run++; continue; }
        if (run >= 4) {
          int kr = pass ? b - 1 - run / 2 : a, kc = pass ? a : b - 1 - run / 2;  // milieu de l'alignement
          for (int k = 0; k < run; k++) {
            int rr = pass ? b - 1 - k : a, cc = pass ? a : b - 1 - k;
            if ((rr == yAR && cc == yAC) || (rr == yBR && cc == yBC)) { kr = rr; kc = cc; }
          }
          uint8_t t = yType(yG[kr][kc]);
          yG[kr][kc] = run >= 5 ? 6 : (t | (pass ? 0x20 : 0x10));
          yMark[kr][kc] = 2;  // reste en place (special)
          n--;
        }
        run = 1;
      }
    }
  // un bonbon raye qui disparait vide sa ligne ou sa colonne (en chaine)
  for (int loop = 0; loop < 4; loop++)
    for (int r = 0; r < Y_N; r++)
      for (int c = 0; c < Y_N; c++)
        if (yMark[r][c] == 1 && yG[r][c] != Y_EMPTY && (yG[r][c] & 0x30)) {
          bool hz = yG[r][c] & 0x10;
          yG[r][c] &= 7;  // une seule fois
          for (int k = 0; k < Y_N; k++) {
            int rr = hz ? r : k, cc = hz ? k : c;
            if (!yMark[rr][cc]) { yMark[rr][cc] = 1; n++; }
          }
          sfxDive();
        }
  return n;
}
void yClearColor(uint8_t t) {  // bombe : tous les bonbons d'une couleur
  memset(yMark, 0, sizeof(yMark));
  for (int r = 0; r < Y_N; r++)
    for (int c = 0; c < Y_N; c++)
      if (yType(yG[r][c]) == t || yType(yG[r][c]) == 6) yMark[r][c] = 1;
}
void yCollapse() {  // les bonbons tombent, de nouveaux arrivent par le haut
  for (int c = 0; c < Y_N; c++) {
    int w = Y_N - 1;
    for (int r = Y_N - 1; r >= 0; r--)
      if (yG[r][c] != Y_EMPTY) {
        if (w != r) { yG[w][c] = yG[r][c]; yOff[w][c] = yOff[r][c] - (w - r) * yS; yG[r][c] = Y_EMPTY; }
        w--;
      }
    for (int r = w; r >= 0; r--) { yG[r][c] = random(6); yOff[r][c] = -(float)(w + 1) * yS - (w - r) * 2; }
  }
}
void yStartClear(int n) {
  yCombo++;
  uint32_t pts = (uint32_t)n * 60 * yCombo;
  gScore += pts;
  yStageScore += pts;
  yState = 3; yT = 12;
  if (yCombo > 1) sndPlay(900 + 150 * yCombo, 1400 + 150 * yCombo, 80, 0.3f, false, 1);
  else sfxBoom();
}
void yStep() {
  gFrame++;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) yReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      yNewLevel();
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  uint8_t k = gDemo ? 0 : keysPs4, edge = k & ~yPrevK;
  if (!gDemo) {  // repetition quand on garde la direction appuyee
    if (k & 27) { if (++yRep > 12 && (yRep & 3) == 0) edge |= k & 27; } else yRep = 0;
  }
  yPrevK = k;
  if (yState == 0) {  // a toi de jouer
    if (gDemo) {  // demo : va vers le meilleur coup, le prend, l'echange
      static int tr = -1, tc = 0, dr = 0, dc = 0;
      if (tr < 0 && !yBestMove(tr, tc, dr, dc)) { yFill(); return; }
      if ((gFrame & 7) == 0) {
        if (yCR != tr) yCR += yCR < tr ? 1 : -1;
        else if (yCC != tc) yCC += yCC < tc ? 1 : -1;
        else if (!ySel) ySel = true;
        else { edge = dr ? 16 : 2; tr = -1; }
      }
      if (!(edge & 18)) return;
    }
    int dr = (edge & 8) ? -1 : (edge & 16) ? 1 : 0, dc = (edge & 1) ? -1 : (edge & 2) ? 1 : 0;
    if (dr && dc) dc = 0;
    if (edge & 32) { ySel = false; sfxMenu(); }
    if (!gDemo && (edge & 4)) { ySel = !ySel; sfxMenu(); }
    if ((dr || dc)) {
      int nr = yCR + dr, nc = yCC + dc;
      if (nr < 0 || nr >= Y_N || nc < 0 || nc >= Y_N) return;
      if (ySel) {  // echange avec le voisin
        yAR = yCR; yAC = yCC; yBR = nr; yBC = nc;
        ySel = false;
        yState = 1; yT = 0;
        yCR = nr; yCC = nc;
        sfxShot();
      } else { yCR = nr; yCC = nc; }
    }
    return;
  }
  if (yState == 1 || yState == 2) {  // echange anime (2 : retour, coup impossible)
    if (++yT < 7) return;
    ySwapCells(yAR, yAC, yBR, yBC);
    if (yState == 2) { yState = 0; return; }
    uint8_t ta = yType(yG[yAR][yAC]), tb = yType(yG[yBR][yBC]);
    yCombo = 0;
    if (ta == 6 || tb == 6) {  // bombe echangee : toute la couleur de l'autre bonbon
      yMoves--;
      yClearColor(ta == 6 ? tb : ta);
      int n = 0;
      for (int r = 0; r < Y_N; r++) for (int c = 0; c < Y_N; c++) n += yMark[r][c] == 1;
      if (!gDemo) rumbleReq = true;
      yStartClear(n);
      return;
    }
    int n = yFindMatches();
    if (n <= 0 && !yAnyRun()) { yState = 2; yT = 0; sndPlay(300, 200, 120, 0.3f, false, 1); return; }  // pas d'alignement : retour
    yMoves--;
    yStartClear(max(n, 1));
    return;
  }
  if (yState == 3) {  // les bonbons alignes clignotent puis disparaissent
    if (--yT > 0) return;
    for (int r = 0; r < Y_N; r++)
      for (int c = 0; c < Y_N; c++)
        if (yMark[r][c] == 1) { yG[r][c] = Y_EMPTY; if (random(4) == 0) gBoomAt(yBX + c * yS + yS / 2, yBY + r * yS + yS / 2); }
    yCollapse();
    yState = 4;
    return;
  }
  if (yState == 4) {  // chute
    bool moving = false;
    for (int r = 0; r < Y_N; r++)
      for (int c = 0; c < Y_N; c++)
        if (yOff[r][c] < 0) { yOff[r][c] = min(0.0f, yOff[r][c] + 1.0f + yS / 7.0f); moving = true; }
    if (moving) return;
    int n = yFindMatches();
    if (n > 0) { yStartClear(n); return; }  // reaction en chaine
    yState = 0;
    yCombo = 0;
    if (!gDemo && gScore > gHi) gHi = gScore;
    if (yStageScore >= (uint32_t)yGoal) {  // niveau gagne : bonus des coups restants
      gWave++;
      gStageT = 90;
      altStageClear(100UL * yMoves);
      sndMelody(JINGLE_STAGE, 4);
      yNewLevel();
    } else if (yMoves <= 0) {  // plus de coups : une vie perdue, on recommence le niveau
      altLoseLife(yBX + Y_N * yS / 2, yBY + Y_N * yS / 2);
    } else {
      int a, b, c, d;
      if (!yBestMove(a, b, c, d)) yFill();  // plus aucun coup possible : nouveau melange
    }
  }
}
void yGem(int x, int y, uint8_t v, int s, bool flash) {  // un bonbon (forme selon la sorte), reflet et ombre
  uint8_t t = yType(v);
  if (t > 6) return;
  const uint8_t *c = Y_COL[t];
  uint16_t main = flash ? rgb(255, 255, 255) : rgb(c[0], c[1], c[2]);
  uint16_t hi = flash ? main : rgb(min(255, c[0] + 120), min(255, c[1] + 120), min(255, c[2] + 120));
  uint16_t dk = flash ? main : rgb(c[0] / 3, c[1] / 3, c[2] / 3);
  if (t == 6) {  // bombe : boule sombre et etincelles de toutes les couleurs qui tournent
    int r = max(2, s / 2 - 1);
    gfx->fillCircle(x + s / 2, y + s / 2, r, rgb(50, 40, 70));
    for (int k = 0; k < 4; k++) {
      float a = gFrame * 0.15f + k * 1.5708f;
      const uint8_t *cc = Y_COL[(k + gFrame / 10) % 6];
      gfx->drawPixel(x + s / 2 + (int)(cosf(a) * (r - 1)), y + s / 2 + (int)(sinf(a) * (r - 1)), rgb(cc[0], cc[1], cc[2]));
    }
    gfx->drawPixel(x + s / 2, y + s / 2, rgb(255, 255, 255));
    return;
  }
  if (s <= 7) {  // petit ecran : dessin 6x6
    for (int r = 0; r < 6; r++)
      for (int cc = 0; cc < 6; cc++) {
        char ch = Y_SPR[t][r][cc];
        if (ch != '.') gfx->drawPixel(x + cc, y + r, ch == 'h' ? hi : ch == 'd' ? dk : main);
      }
  } else {  // grand ecran : formes pleines
    int m = 1, w = s - 2, cx = x + s / 2, cy = y + s / 2, R = w / 2;
    switch (t) {
      case 0: gfx->fillCircle(cx, cy, R, main); break;
      case 1: gfx->fillRoundRect(x + m, y + m, w, w, 2, main); break;
      case 2: gfx->fillTriangle(cx, y + m, x + m, cy, x + m + w - 1, cy, main); gfx->fillTriangle(cx, y + m + w - 1, x + m, cy, x + m + w - 1, cy, main); break;
      case 3: gfx->fillTriangle(cx, y + m, x + m, y + m + w - 1, x + m + w - 1, y + m + w - 1, main); break;
      case 4: gfx->fillCircle(cx, cy + R / 3, R * 2 / 3 + 1, main); gfx->fillTriangle(cx, y + m, cx - R * 2 / 3 - 1, cy + R / 3, cx + R * 2 / 3 + 1, cy + R / 3, main); break;
      default: gfx->fillTriangle(cx, y + m, x + m, y + m + w * 3 / 4, x + m + w - 1, y + m + w * 3 / 4, main);
               gfx->fillTriangle(cx, y + m + w - 1, x + m, y + m + w / 4, x + m + w - 1, y + m + w / 4, main); break;
    }
    gfx->fillRect(cx - R / 2, cy - R / 2, 2, 2, hi);  // reflet
    gfx->drawPixel(cx + R / 2, cy + R / 2, dk);
  }
  if (v & 0x10) { gfx->drawFastHLine(x + 1, y + s / 2 - 2, s - 2, rgb(255, 255, 255)); gfx->drawFastHLine(x + 1, y + s / 2 + 1, s - 2, rgb(255, 255, 255)); }
  if (v & 0x20) { gfx->drawFastVLine(x + s / 2 - 2, y + 1, s - 2, rgb(255, 255, 255)); gfx->drawFastVLine(x + s / 2 + 1, y + 1, s - 2, rgb(255, 255, 255)); }
}
void yDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  int s = yS;
  uint16_t bgA = rgb(28, 18, 52), bgB = rgb(40, 26, 72);
  for (int r = 0; r < Y_N; r++)  // fond en damier, puis les bonbons (redessines a chaque image)
    for (int c = 0; c < Y_N; c++) gfx->fillRect(yBX + c * s, yBY + r * s, s, s, ((r + c) & 1) ? bgA : bgB);
  float u = (yState == 1 || yState == 2) ? min(1.0f, yT / 7.0f) : 0;
  for (int r = 0; r < Y_N; r++)
    for (int c = 0; c < Y_N; c++) {
      if (yG[r][c] == Y_EMPTY) continue;
      float dx = 0, dy = yOff[r][c];
      if (u > 0 && r == yAR && c == yAC) { dx = (yBC - yAC) * s * u; dy += (yBR - yAR) * s * u; }
      if (u > 0 && r == yBR && c == yBC) { dx = (yAC - yBC) * s * u; dy += (yAR - yBR) * s * u; }
      int x = yBX + c * s + (int)dx, y = yBY + r * s + (int)dy;
      if (y < yBY) continue;  // encore au-dessus du plateau
      bool flash = yState == 3 && yMark[r][c] == 1 && (yT & 2);
      yGem(x, y, yG[r][c], s, flash);
    }
  if (yState == 0 || ySel) {  // curseur : blanc, jaune qui clignote quand un bonbon est pris
    uint16_t cc = ySel ? ((gFrame & 4) ? palFull[2] : palFull[7]) : palFull[0];
    gfx->drawRect(yBX + yCC * s - 1, yBY + yCR * s - 1, s + 2, s + 2, cc);
    if (ySel) gfx->drawRect(yBX + yCC * s, yBY + yCR * s, s, s, cc);
  }
  int bw = Y_N * s;  // seul le contour du plateau (ou passe le curseur) est efface : le plateau est redessine en entier
  gMark(yBX - 1, yBY - 1, bw + 2, 1); gMark(yBX - 1, yBY + bw, bw + 2, 1);
  gMark(yBX - 1, yBY - 1, 1, bw + 2); gMark(yBX + bw, yBY - 1, 1, bw + 2);
  // a droite : coups restants, objectif et jauge de progression
  int px = yBX + Y_N * s + 4, pw = W - px - 2, py = yBY + 1;
  uint16_t lab = rgb(110, 110, 140), val = rgb(230, 230, 230);
  char b[10];
  tinyText(*gfx, px, py, "COUPS", lab);
  snprintf(b, sizeof(b), "%d", yMoves);
  tinyText(*gfx, px, py + 7, b, yMoves <= 3 ? palFull[1] : val);
  tinyText(*gfx, px, py + 16, "BUT", lab);
  snprintf(b, sizeof(b), "%d", yGoal);
  tinyText(*gfx, px, py + 23, b, val);
  int gy = py + 32, gh = max(4, Y_N * s - 34);
  if (gy + gh > H) gh = H - gy - 1;
  gfx->drawRect(px, gy, min(pw, 10), gh, rgb(90, 90, 120));  // jauge verticale qui monte vers l'objectif
  int fill = (int)min((float)(gh - 2), (gh - 2) * (float)yStageScore / yGoal);
  gfx->fillRect(px + 1, gy + gh - 1 - fill, min(pw, 10) - 2, fill, yStageScore >= (uint32_t)yGoal ? palFull[5] : palFull[6]);
  gMark(px, py, pw, gy + gh - py);
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- TAUPE
// Jeu de creusage original : une taupe creuse des galeries sous terre (4 couches de couleur). X lance un jet de boue
// (3 cases) : une creature touchee est assommee, touchee encore une fois elle eclate (plus de points en profondeur).
// Vers roses (dans les galeries) et chauves-souris (traversent parfois la terre). Rochers : creuser dessous les fait
// tomber (ils ecrasent les creatures). Toutes les creatures eliminees : stage suivant.
#define D_MAXC 26
#define D_MAXR 18
#define D_EN 8
#define D_ROCKS 5
int dTS = 5, dCols = 25, dRows = 10, dX0 = 1, dY0 = 14;
float dPX, dPY;
int8_t dPC, dPR, dDX = 1, dDY = 0, dFace = 1;
int8_t dWantX = 0, dWantY = 0;
struct DShot { float x, y; int8_t dx, dy; uint8_t left; bool on; } dShot;
struct DMem { uint8_t grid[D_MAXR][D_MAXC]; DEn en[D_EN]; };
#define dGrid (GMEM(DMem)->grid)  // 1 terre, 0 galerie
#define dEn (GMEM(DMem)->en)
DRock dRock[D_ROCKS];
bool dPrevFire = false;
float dcx(int c) { return dX0 + c * dTS + dTS / 2.0f; }
float dcy(int r) { return dY0 + r * dTS + dTS / 2.0f; }
bool dDug(int c, int r) { return c >= 0 && c < dCols && r >= 0 && r < dRows && dGrid[r][c] == 0; }
bool dRockAt(int c, int r) {
  for (auto &k : dRock) if (k.on && k.c == c && (int)((k.y - dY0) / dTS) == r) return true;
  return false;
}
void dLevel() {
  for (int r = 0; r < dRows; r++) for (int c = 0; c < dCols; c++) dGrid[r][c] = 1;
  int mc = dCols / 2;
  for (int r = 0; r <= dRows / 2; r++) dGrid[r][mc] = 0;  // puits de depart, depuis la surface
  dPC = mc; dPR = 0; dPX = dcx(dPC); dPY = dcy(dPR); dDX = 0; dDY = 1; dFace = 1;
  dWantX = 0; dWantY = 0;
  int n = min(D_EN, 3 + gWave);
  randomSeed(777 + gWave * 31);
  for (int i = 0; i < D_EN; i++) {
    DEn &e = dEn[i];
    e.on = i < n;
    if (!e.on) continue;
    int r = 2 + random(dRows - 3), c = random(2, dCols - 6);
    if (abs(c - mc) < 4) c = (c + 8) % (dCols - 5);
    bool horiz = random(2);
    for (int k = 0; k < 4; k++) { if (horiz) dGrid[r][min(dCols - 1, c + k)] = 0; else dGrid[min(dRows - 1, r + k / 2)][c] = 0; }  // sa galerie
    e.c = c; e.r = r; e.x = dcx(c); e.y = dcy(r); e.dx = horiz ? 1 : 0; e.dy = horiz ? 0 : 1;
    e.type = i % 3 == 2 ? 1 : 0; e.stun = 0; e.ghost = 0; e.t = random(200);
  }
  for (int i = 0; i < D_ROCKS; i++) {
    DRock &k = dRock[i];
    k.on = i < 2 + min(gWave, 3);
    k.c = random(1, dCols - 1);
    int r = 1 + random(dRows - 3);
    if (k.c == mc) k.c++;
    k.y = dY0 + r * dTS; k.vy = 0; k.wob = 0; k.state = 0;
  }
  randomSeed(micros());
  dShot.on = false;
}
void dReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 90;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  dTS = H >= 128 ? 8 : 5;
  dY0 = H >= 128 ? 20 : 14;
  dCols = min(D_MAXC, W / dTS);
  dRows = min(D_MAXR, (H - dY0) / dTS);
  dX0 = (W - dCols * dTS) / 2;
  dLevel();
}
uint8_t dAuto() {  // demo : va vers la creature la plus proche, tire quand elle est alignee
  int best = 999, bi = -1;
  for (int i = 0; i < D_EN; i++)
    if (dEn[i].on) { int d = abs(dEn[i].c - dPC) + abs(dEn[i].r - dPR); if (d < best) { best = d; bi = i; } }
  if (bi < 0) return 0;
  DEn &e = dEn[bi];
  uint8_t k = 0;
  int dc = e.c - dPC, dr = e.r - dPR;
  if ((dr == 0 && abs(dc) <= 3 && dFace == (dc > 0 ? 1 : -1) && dDY == 0) || (dc == 0 && abs(dr) <= 3 && dDY == (dr > 0 ? 1 : -1))) k |= 4;
  if (abs(dc) > abs(dr) || dr == 0) k |= dc < 0 ? 1 : 2;
  else k |= dr < 0 ? 8 : 16;
  if (best <= 1 && !e.stun) k = (k & 4) | (dc < 0 ? 2 : 1);  // trop pres : on recule en tirant
  return k;
}
void dKill(DEn &e, int mult) {
  e.on = false;
  gScore += (200UL + 100UL * (e.r * 4 / dRows)) * mult;
  gBoomAt(e.x, e.y);
  sfxBoom();
}
void dStep() {
  gFrame++;
  uint8_t k = gDemo ? dAuto() : keysPs4;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) dReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      int mc = dCols / 2;
      dPC = mc; dPR = 0; dPX = dcx(dPC); dPY = dcy(dPR); dDX = 0; dDY = 0;
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  if (k & 1) { dWantX = -1; dWantY = 0; }
  else if (k & 2) { dWantX = 1; dWantY = 0; }
  else if (k & 8) { dWantX = 0; dWantY = -1; }
  else if (k & 16) { dWantX = 0; dWantY = 1; }
  else { dWantX = 0; dWantY = 0; }
  // deplacement de case en case : on change de direction au centre d'une case ; creuser ralentit
  float tx = dcx(dPC), ty = dcy(dPR);
  bool atCenter = fabsf(dPX - tx) < 0.01f && fabsf(dPY - ty) < 0.01f;
  if (atCenter) {
    dPX = tx; dPY = ty;
    if (dWantX || dWantY) {
      int nc = dPC + dWantX, nr = dPR + dWantY;
      if (nc >= 0 && nc < dCols && nr >= 0 && nr < dRows && !dRockAt(nc, nr)) { dPC = nc; dPR = nr; dDX = dWantX; dDY = dWantY; }
      if (dWantX) dFace = dWantX;
    }
  }
  float sp = (dGrid[dPR][dPC] ? 0.55f : 0.95f) * dTS / 5.0f;
  tx = dcx(dPC); ty = dcy(dPR);
  dPX += constrain(tx - dPX, -sp, sp);
  dPY += constrain(ty - dPY, -sp, sp);
  if (dGrid[dPR][dPC] && fabsf(dPX - tx) + fabsf(dPY - ty) < dTS * 0.6f) { dGrid[dPR][dPC] = 0; gScore += 1; }  // la taupe creuse
  // jet de boue
  bool fire = k & (4 | 32);
  if (fire && !dPrevFire && !dShot.on) {
    dShot.on = true; dShot.x = dPX; dShot.y = dPY; dShot.dx = dDY ? 0 : dFace; dShot.dy = dDY; dShot.left = dTS * 3;
    sndPlay(500, 900, 70, 0.3f, false, 1);
  }
  dPrevFire = fire;
  if (dShot.on) {
    for (int s = 0; s < 2 && dShot.on; s++) {
      dShot.x += dShot.dx; dShot.y += dShot.dy;
      int c = (int)((dShot.x - dX0) / dTS), r = (int)((dShot.y - dY0) / dTS);
      if (--dShot.left == 0 || c < 0 || c >= dCols || r < 0 || r >= dRows || dGrid[r][c]) { dShot.on = false; break; }
      for (auto &e : dEn)
        if (e.on && fabsf(e.x - dShot.x) < dTS * 0.7f && fabsf(e.y - dShot.y) < dTS * 0.7f) {
          dShot.on = false;
          if (e.stun) dKill(e, 1);  // deuxieme jet : elle eclate
          else { e.stun = 180; e.ghost = 0; sndPlay(1200, 600, 120, 0.3f, false, 1); }
          break;
        }
    }
  }
  // creatures
  float es = (0.45f + 0.05f * min(gWave, 8) + 0.07f * (gEffDiff() - 1)) * dTS / 5.0f;
  static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  for (auto &e : dEn) {
    if (!e.on) continue;
    if (e.stun) { e.stun--; continue; }
    e.t++;
    if (e.ghost) {  // traverse la terre, droit vers la taupe
      e.ghost--;
      e.x += constrain(dPX - e.x, -es * 0.6f, es * 0.6f);
      e.y += constrain(dPY - e.y, -es * 0.6f, es * 0.6f);
      e.c = constrain((int)((e.x - dX0) / dTS), 0, dCols - 1); e.r = constrain((int)((e.y - dY0) / dTS), 0, dRows - 1);
      if (!e.ghost) { if (dGrid[e.r][e.c]) e.ghost = 20; else { e.x = dcx(e.c); e.y = dcy(e.r); } }  // ressort dans une galerie
    } else {
      float ex = dcx(e.c), ey = dcy(e.r);
      if (fabsf(e.x - ex) < 0.01f && fabsf(e.y - ey) < 0.01f) {
        if (e.type == 1 && e.t % 360 == 200) { e.ghost = 160; continue; }  // chauve-souris : passe dans la terre
        int opts[4], n = 0, best = -1, bd = 9999;
        for (int d = 0; d < 4; d++) {
          int nc = e.c + DX[d], nr = e.r + DY[d];
          if (!dDug(nc, nr) || dRockAt(nc, nr)) continue;
          if (DX[d] == -e.dx && DY[d] == -e.dy && n) continue;
          opts[n++] = d;
          int dd = abs(nc - dPC) + abs(nr - dPR);
          if (dd < bd) { bd = dd; best = d; }
        }
        if (n) {
          int d = random(100) < 55 ? best : opts[random(n)];
          e.dx = DX[d]; e.dy = DY[d]; e.c += e.dx; e.r += e.dy;
        } else if (e.type == 0 && e.t % 300 == 150) e.ghost = 120;  // coince : finit par traverser
      }
      e.x += constrain(dcx(e.c) - e.x, -es, es);
      e.y += constrain(dcy(e.r) - e.y, -es, es);
    }
    if (fabsf(e.x - dPX) < dTS * 0.8f && fabsf(e.y - dPY) < dTS * 0.8f) { altLoseLife(dPX, dPY); dShot.on = false; return; }
  }
  // rochers : creuser dessous les fait trembler puis tomber
  for (auto &rk : dRock) {
    if (!rk.on) continue;
    int r = (int)((rk.y - dY0) / dTS);
    if (rk.state == 0) {
      if (r + 1 < dRows && dGrid[r + 1][rk.c] == 0 && !(dPC == rk.c && dPR == r + 1)) { rk.state = 1; rk.wob = 40; }
    } else if (rk.state == 1) {
      if (--rk.wob <= 0) { rk.state = 2; rk.vy = 0.3f; sndPlay(300, 120, 300, 0.4f, true, 1); }
    } else {
      rk.vy = min(rk.vy + 0.08f, 2.5f);
      rk.y += rk.vy;
      int rr = (int)((rk.y + dTS - 1 - dY0) / dTS);
      int crushed = 0;
      for (auto &e : dEn)
        if (e.on && e.c == rk.c && fabsf(e.y - (rk.y + dTS / 2)) < dTS) dKill(e, 2 + crushed++);  // ecrasees : bonus
      if (fabsf(dPX - dcx(rk.c)) < dTS * 0.7f && fabsf(dPY - (rk.y + dTS / 2)) < dTS * 0.8f) { rk.on = false; altLoseLife(dPX, dPY); return; }
      if (rr >= dRows || dGrid[rr][rk.c]) { rk.on = false; gBoomAt(dcx(rk.c), rk.y + dTS / 2); }
    }
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  bool any = false;
  for (auto &e : dEn) any |= e.on;
  if (!any) {
    gWave++;
    gStageT = 90;
    altStageClear(1000UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    dLevel();
  }
}
void dSpr(const char *const *rows, int w, int h, int x, int y, bool flip, uint16_t *pal) {  // petit sprite, miroir possible
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      char ch = rows[r][flip ? w - 1 - c : c];
      if (ch == '.') continue;
      int i = gPalIdx(ch);
      if (i >= 0) jPx(x + c, y + r, pal[i]);
    }
}
void dDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  // ciel (degrade) et herbe
  for (int y = 8; y < dY0 - 1; y++) jRect(0, y, W, 1, rgb(20, 40 + (y - 8) * 8, 120 + (y - 8) * 10));
  jRect(0, dY0 - 1, W, 1, rgb(40, 200, 60));
  jRect(W - 12, 9, 4, 4, rgb(255, 220, 60));  // soleil
  static const uint8_t SOIL[4][3] = { { 170, 110, 50 }, { 200, 120, 40 }, { 150, 70, 40 }, { 110, 50, 90 } };
  for (int r = 0; r < dRows; r++) {
    const uint8_t *s = SOIL[r * 4 / dRows];
    uint16_t c1 = rgb(s[0], s[1], s[2]), c2 = rgb(s[0] * 2 / 3, s[1] * 2 / 3, s[2] * 2 / 3);
    for (int c = 0; c < dCols; c++) {
      int x = dX0 + c * dTS, y = dY0 + r * dTS;
      if (dGrid[r][c]) {
        jRect(x, y, dTS, dTS, c1);
        jPx(x + ((r * 3 + c * 7) % dTS), y + ((r * 5 + c * 3) % dTS), c2);  // grains de terre
        jPx(x + ((r * 7 + c * 5 + 2) % dTS), y + ((r + c * 4 + 1) % dTS), c2);
      } else jRect(x, y, dTS, dTS, 0);
    }
  }
  jRect(0, dY0, dX0, H, 0);
  jRect(dX0 + dCols * dTS, dY0, W, H, 0);
  for (auto &rk : dRock) {  // rochers gris, ombres, ils tremblent avant de tomber
    if (!rk.on) continue;
    int x = dX0 + rk.c * dTS + (rk.state == 1 ? ((gFrame / 2) & 1) : 0), y = (int)rk.y;
    jRect(x, y + 1, dTS, dTS - 1, rgb(130, 130, 140));
    jRect(x + 1, y, dTS - 2, 1, rgb(130, 130, 140));
    jRect(x + 1, y + 1, 2, 1, rgb(220, 220, 230));
    jRect(x + 1, y + dTS - 1, dTS - 2, 1, rgb(60, 60, 70));
  }
  static const char *const WORM[2][4] = { { ".ppp..", "pwpppp", "pppppp", "..pp.." }, { "..ppp.", "ppwppp", "pppppp", ".p..p." } };
  static const char *const BAT[2][4] = { { "v.vv.v", "vvwwvv", ".vvvv.", "..vv.." }, { "..vv..", ".vwwv.", "vvvvvv", "v....v" } };
  for (auto &e : dEn) {
    if (!e.on) continue;
    int x = (int)e.x - 3, y = (int)e.y - 2, fr = (gFrame / 8) & 1;
    if (e.ghost) {  // dans la terre : seulement les yeux
      jPx(x + 2, y + 1, palFull[0]); jPx(x + 4, y + 1, palFull[0]);
      continue;
    }
    if (e.stun && (gFrame & 4)) { dSpr(e.type ? BAT[0] : WORM[0], 6, 4, x, y, e.dx < 0, palFull); jRect(x + 1, y - 2, 4, 1, palFull[2]); continue; }
    dSpr(e.type ? BAT[fr] : WORM[fr], 6, 4, x, y, e.dx < 0, palFull);
  }
  if (dShot.on) { jRect((int)dShot.x - 1, (int)dShot.y - 1, 2, 2, rgb(150, 90, 40)); jPx((int)dShot.x, (int)dShot.y - 1, palFull[2]); }
  if (gDead == 0) {  // taupe : fourrure brune, nez rose, griffes blanches
    static const char *const MOLE[2][5] = { { "..ooo.", ".oooob", "oooooo", "ooooop", ".w..w." }, { "..ooo.", ".oooob", "oooooo", "ooooop", "w..w.." } };
    dSpr(MOLE[(gFrame / 6) & 1], 6, 5, (int)dPX - 3, (int)dPY - 2, dFace < 0, palFull);
  }
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- BILLES
// Tir de billes original : un canon en bas se vise (gauche / droite, ou le stick qui donne l'angle), X tire. La bille
// rebondit sur les murs et se colle a la grappe ; 3 billes de meme couleur ou plus eclatent, et celles qui ne tiennent
// plus au plafond tombent (gros bonus). Le plafond descend tous les 8 tirs ; une bille sur la ligne du bas : fin.
#define P_COLS 16  // colonnes au plus (le puits prend la largeur de l'ecran : pNC colonnes)
#define P_ROWS 14
struct PMem { uint8_t g[P_ROWS][P_COLS], mk1[P_ROWS][P_COLS], mk2[P_ROWS][P_COLS]; int16_t q[P_ROWS * P_COLS]; };
#define pG (GMEM(PMem)->g)  // 0 vide, 1..6 couleur
int pNC = 8, pB = 6, pRH = 5, pX0 = 30, pY0 = 9, pRowsVis = 9, pShots = 0, pDrop = 0, pNColors = 4;
float pAng = 0, pBX, pBY, pVX, pVY;
uint8_t pCur = 1, pNext = 2, pPrevK = 0;
bool pFlying = false;
int8_t pPopT = 0;
const uint8_t P_COL[7][3] = { { 0, 0, 0 }, { 255, 50, 60 }, { 255, 220, 0 }, { 40, 220, 80 }, { 40, 120, 255 }, { 200, 70, 255 }, { 0, 220, 255 } };
int pRowCols(int r) { return (r + pDrop) & 1 ? pNC - 1 : pNC; }  // une rangee sur deux decalee d'une demi-bille
float pCX(int r, int c) { return pX0 + c * pB + ((r + pDrop) & 1 ? pB / 2.0f : 0) + pB / 2.0f; }
float pCY(int r) { return pY0 + r * pRH + pB / 2.0f; }
void pNeighbors(int r, int c, int nb[6][2]) {
  bool odd = (r + pDrop) & 1;
  static const int8_t E[6][2] = { { 0, -1 }, { 0, 1 }, { -1, -1 }, { -1, 0 }, { 1, -1 }, { 1, 0 } }, O[6][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { -1, 1 }, { 1, 0 }, { 1, 1 } };
  for (int k = 0; k < 6; k++) { nb[k][0] = r + (odd ? O : E)[k][0]; nb[k][1] = c + (odd ? O : E)[k][1]; }
}
bool pIn(int r, int c) { return r >= 0 && r < P_ROWS && c >= 0 && c < pRowCols(r); }
uint8_t pPick() {  // couleur parmi celles encore presentes
  uint8_t have[7] = { 0 };
  int n = 0;
  for (int r = 0; r < P_ROWS; r++) for (int c = 0; c < P_COLS; c++) if (pG[r][c]) have[pG[r][c]] = 1;
  for (int k = 1; k <= 6; k++) n += have[k];
  if (!n) return 1 + random(pNColors);
  int pick = random(n);
  for (int k = 1; k <= 6; k++) if (have[k] && pick-- == 0) return k;
  return 1;
}
void pLevel() {
  memset(pG, 0, sizeof(pG));
  pDrop = 0; pShots = 0;
  pNColors = min(6, 4 + gWave / 3);  // rouge, jaune, vert, bleu des le 1er stage
  int rows = min(pRowsVis - 3, 4 + gWave / 2);
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < pRowCols(r); c++) pG[r][c] = 1 + random(pNColors);
  pCur = pPick(); pNext = pPick();
  pFlying = false; pAng = 0;
}
void pReset() {
  gLives = 1;  // une seule partie
  gPaused = false;
  gScore = 0; gWave = 1; gDead = 0; gBonus = 0; gLifeUp = false;
  gStageT = 80;
  sndMelody(JINGLE_STAGE, 4);
  for (auto &b : gBoom) b.t = 0;
  pB = H >= 128 ? 10 : 6;
  pRH = H >= 128 ? 9 : 5;
  pY0 = H >= 128 ? 12 : 9;
  pRowsVis = min(P_ROWS, (H - pY0 - pB - 6) / pRH);
  pNC = min(P_COLS, (W - 32 - pB / 2) / pB);  // place pour le canon a droite
  pX0 = 4;
  pLevel();
}
// trouve les billes de meme couleur reliees (ou toutes celles reliees au plafond si col = 0) ; marque 1 dans mk
int pFlood(int r0, int c0, uint8_t col, uint8_t mk[P_ROWS][P_COLS]) {
  int16_t *q = GMEM(PMem)->q;
  int h = 0, t = 0, n = 0;
  q[t++] = r0 * P_COLS + c0; mk[r0][c0] = 1;
  while (h < t) {
    int r = q[h] / P_COLS, c = q[h] % P_COLS;
    h++; n++;
    int nb[6][2];
    pNeighbors(r, c, nb);
    for (auto &v : nb)
      if (pIn(v[0], v[1]) && !mk[v[0]][v[1]] && pG[v[0]][v[1]] && (!col || pG[v[0]][v[1]] == col)) { mk[v[0]][v[1]] = 1; q[t++] = v[0] * P_COLS + v[1]; }
  }
  return n;
}
void pLand(int r, int c) {
  pG[r][c] = pCur;
  pFlying = false;
  auto &mk = GMEM(PMem)->mk1;
  memset(mk, 0, sizeof(mk));
  int n = pFlood(r, c, pCur, mk);
  if (n >= 3) {  // eclatent
    for (int rr = 0; rr < P_ROWS; rr++)
      for (int cc = 0; cc < P_COLS; cc++)
        if (mk[rr][cc]) { pG[rr][cc] = 0; if (random(3) == 0) gBoomAt(pCX(rr, cc), pCY(rr)); }
    gScore += 10UL * n;
    sfxBoom();
    memset(mk, 0, sizeof(mk));  // celles qui ne tiennent plus au plafond tombent
    for (int cc = 0; cc < P_COLS; cc++) if (pG[0][cc] && !mk[0][cc]) pFlood(0, cc, 0, mk);
    int fall = 0;
    for (int rr = 0; rr < P_ROWS; rr++)
      for (int cc = 0; cc < P_COLS; cc++)
        if (pG[rr][cc] && !mk[rr][cc]) { pG[rr][cc] = 0; fall++; gBoomAt(pCX(rr, cc), pCY(rr)); }
    if (fall) { gScore += 20UL << min(fall, 10); sndMelody(JINGLE_STAGE, 3); }
  } else sndPlay(400, 300, 50, 0.3f, false, 1);
  if (++pShots >= 8) {  // le plafond descend d'un rang
    pShots = 0;
    for (int rr = P_ROWS - 1; rr > 0; rr--) memcpy(pG[rr], pG[rr - 1], P_COLS);
    memset(pG[0], 0, P_COLS);
    pDrop++;
    for (int cc = 0; cc < pRowCols(0); cc++) pG[0][cc] = 1 + random(pNColors);
    sndPlay(200, 120, 200, 0.5f, true, 2);
  }
  bool any = false, over = false;
  for (int rr = 0; rr < P_ROWS; rr++)
    for (int cc = 0; cc < P_COLS; cc++)
      if (pG[rr][cc]) { any = true; if (rr >= pRowsVis) over = true; }
  if (!gDemo && gScore > gHi) gHi = gScore;
  if (over) { altLoseLife(W / 2, H - 6); return; }
  if (!any) {
    gWave++;
    gStageT = 80;
    altStageClear(1000UL * gWave);
    sndMelody(JINGLE_STAGE, 4);
    pLevel();
    return;
  }
  pCur = pNext; pNext = pPick();
}
// trajectoire simulee (demo et pointilles) : renvoie la case d'arrivee
bool pTrace(float ang, int &lr, int &lc, bool draw, uint16_t col) {
  float x = pX0 + (pNC * pB + pB / 2) / 2.0f, y = H - pB / 2.0f - 3, vx = sinf(ang), vy = -cosf(ang);
  float minX = pX0 + pB / 2.0f, maxX = pX0 + pNC * pB;
  for (int s = 0; s < 400; s++) {
    x += vx; y += vy;
    if (x < minX) { x = minX; vx = -vx; }
    if (x > maxX) { x = maxX; vx = -vx; }
    if (draw && s % 4 == 0 && s > 6) jPx((int)x, (int)y, col);
    bool hit = y <= pY0 + pB / 2.0f;
    for (int r = 0; r < pRowsVis + 1 && !hit; r++)
      for (int c = 0; c < pRowCols(r) && !hit; c++)
        if (pG[r][c] && fabsf(pCX(r, c) - x) < pB * 0.9f && fabsf(pCY(r) - y) < pB * 0.9f) hit = true;
    if (hit) {  // case vide la plus proche
      float bd = 1e9;
      lr = -1;
      for (int r = 0; r < P_ROWS; r++)
        for (int c = 0; c < pRowCols(r); c++) {
          if (pG[r][c]) continue;
          float d = (pCX(r, c) - x) * (pCX(r, c) - x) + (pCY(r) - y) * (pCY(r) - y);
          if (d < bd) { bd = d; lr = r; lc = c; }
        }
      return lr >= 0;
    }
  }
  return false;
}
uint8_t pAuto() {  // demo : essaie les angles, garde celui qui fait eclater le plus
  static float target = 0;
  static uint8_t plan = 0;
  if (!plan) {
    int best = -1;
    for (int a = -70; a <= 70; a += 4) {
      int r, c;
      if (!pTrace(a * 0.01745f, r, c, false, 0)) continue;
      auto &mk = GMEM(PMem)->mk2;
      memset(mk, 0, sizeof(mk));
      pG[r][c] = pCur;
      int n = pFlood(r, c, pCur, mk) * 10 + random(5) - r;
      pG[r][c] = 0;
      if (n > best) { best = n; target = a * 0.01745f; }
    }
    plan = 1;
  }
  if (fabsf(target - pAng) > 0.03f) return target < pAng ? 1 : 2;
  plan = 0;
  return 4;
}
void pStep() {
  gFrame++;
  uint8_t k = 0;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gDemo) pReset();
      else if (gLastRank) startNameEntry();
      else toDemo();
    }
    return;
  }
  if (gStageT > 0) { gStageT--; return; }
  if (!pFlying) k = gDemo ? pAuto() : keysPs4;
  uint8_t edge = k & ~pPrevK;
  pPrevK = k;
  if (!gDemo && padStickX) pAng = constrain(padStickX / 127.0f * 1.3f, -1.3f, 1.3f);  // stick : l'angle suit le stick
  else {
    if (k & 1) pAng -= 0.035f;
    if (k & 2) pAng += 0.035f;
  }
  pAng = constrain(pAng, -1.3f, 1.3f);
  if (!pFlying && (edge & (4 | 32) || (gDemo && (k & 4)))) {
    pFlying = true;
    pBX = pX0 + (pNC * pB + pB / 2) / 2.0f; pBY = H - pB / 2.0f - 3;
    float sp = H >= 128 ? 4.0f : 2.6f;
    pVX = sinf(pAng) * sp; pVY = -cosf(pAng) * sp;
    sfxShot();
  }
  if (pFlying) {
    float minX = pX0 + pB / 2.0f, maxX = pX0 + pNC * pB;
    for (int s = 0; s < 4 && pFlying; s++) {
      pBX += pVX / 4; pBY += pVY / 4;
      if (pBX < minX) { pBX = minX; pVX = -pVX; }
      if (pBX > maxX) { pBX = maxX; pVX = -pVX; }
      bool hit = pBY <= pY0 + pB / 2.0f;
      for (int r = 0; r < P_ROWS && !hit; r++)
        for (int c = 0; c < pRowCols(r) && !hit; c++)
          if (pG[r][c] && fabsf(pCX(r, c) - pBX) < pB * 0.9f && fabsf(pCY(r) - pBY) < pB * 0.9f) hit = true;
      if (hit) {
        float bd = 1e9;
        int lr = 0, lc = 0;
        for (int r = 0; r < P_ROWS; r++)
          for (int c = 0; c < pRowCols(r); c++) {
            if (pG[r][c]) continue;
            float d = (pCX(r, c) - pBX) * (pCX(r, c) - pBX) + (pCY(r) - pBY) * (pCY(r) - pBY);
            if (d < bd) { bd = d; lr = r; lc = c; }
          }
        pLand(lr, lc);
      }
    }
  }
}
void pBall(int cx, int cy, uint8_t col) {  // bille ronde avec reflet
  const uint8_t *c = P_COL[col];
  uint16_t main = rgb(c[0], c[1], c[2]), dk = rgb(c[0] / 3, c[1] / 3, c[2] / 3);
  int r = pB / 2 - 1;
  if (pB <= 6) {
    jRect(cx - 2, cy - 1, 5, 3, main); jRect(cx - 1, cy - 2, 3, 5, main);
    jPx(cx - 1, cy - 1, rgb(255, 255, 255)); jPx(cx + 1, cy + 2, dk); jPx(cx + 2, cy + 1, dk);
  } else {
    gfx->fillCircle(cx, cy, r, main);
    gfx->drawCircle(cx, cy, r, dk);
    gfx->fillRect(cx - r / 2, cy - r / 2, 2, 2, rgb(255, 255, 255));
  }
}
void pDraw() {
  eraseOld();
  uint16_t palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  int bw = pNC * pB + pB / 2, by1 = pY0 + pRowsVis * pRH + pB / 2;
  jRect(pX0, 8, bw, H - 8, rgb(10, 12, 30));  // fond du puits (redessine a chaque image)
  jRect(pX0 - 2, 8, 2, H - 8, rgb(90, 100, 160));
  jRect(pX0 + bw, 8, 2, H - 8, rgb(90, 100, 160));
  jRect(pX0, 8, bw, pY0 - 8, rgb(150, 120, 60));  // plafond qui descend
  for (int x = pX0 + 2; x < pX0 + bw; x += 6) jPx(x, pY0 - 1, rgb(80, 60, 30));
  for (int x = pX0; x < pX0 + bw; x += 3) jPx(x, by1, rgb(255, 60, 60));  // ligne a ne pas depasser
  for (int r = 0; r < P_ROWS; r++)
    for (int c = 0; c < pRowCols(r); c++)
      if (pG[r][c] && pCY(r) < H) pBall((int)pCX(r, c), (int)pCY(r), pG[r][c]);
  int lr, lc;
  if (!pFlying && gDead == 0) pTrace(pAng, lr, lc, true, rgb(140, 140, 160));  // pointilles de visee
  int cx = pX0 + bw / 2, cy = H - pB / 2 - 3;  // canon : socle, tube oriente
  gfx->drawLine(cx, cy, cx + (int)(sinf(pAng) * (pB + 2)), cy - (int)(cosf(pAng) * (pB + 2)), palFull[0]);
  jRect(cx - 4, H - 3, 9, 3, rgb(120, 120, 140));
  if (pFlying) pBall((int)pBX, (int)pBY, pCur);
  else pBall(cx, cy, pCur);
  // a droite : bille suivante, tirs avant que le plafond descende
  int px = pX0 + bw + 6;
  tinyText(*gfx, W - 32, 1, "APRES", rgb(110, 110, 140));  // en haut a droite, sur la ligne du score
  gMark(W - 32, 1, 19, 5);
  if (pB <= 6) pBall(W - 6, 3, pNext);
  else pBall(W - 7, 4, pNext);
  gMark(W - 12, 0, 12, 8);
  for (int i = 0; i < 8 - pShots; i++) jRect(px + i * 3, H - 6, 2, 3, rgb(255, 160, 40));  // tirs avant que le plafond descende
  gMark(px, H - 6, 26, 3);
  altBooms(palFull);
  altHud(palFull);
}

// ---------------------------------------------------------------- ESCADRILLE
// Shoot vertical original au-dessus de la mer (vagues, hauts-fonds, iles) : ton chasseur bleu a etoiles blanches
// affronte des chasseurs (verts, argent, jaunes), des bombardiers bimoteurs et des navires (vedette, destroyer,
// porte-avions qui lance des chasseurs). Capsules : P arme, T tonneau, + vie. Rond : tonneau (invincible, sans tirer).
// Apres 10 vagues : un grand quadrimoteur vert, plus rapide et en spirale sous la moitie de sa vie.
#define A_EN 26
#define A_SHOTS 18
#define A_BOMBS 32
#define A_WAVE 56
#define A_GROUPS 10
#define A_TOP 10  // bande du score (noire), la mer en dessous
struct AMem { AEn en[A_EN]; RDot shot[A_SHOTS], bomb[A_BOMBS]; int16_t wave[A_WAVE][2]; };
#define aEn (GMEM(AMem)->en)
#define aShot (GMEM(AMem)->shot)
#define aBomb (GMEM(AMem)->bomb)
#define aWave (GMEM(AMem)->wave)
RCap aCap;  // kind 0 : arme (P), 1 : tonneau (T), 2 : vie (+)
float aIsY[2], aX, aY;
int8_t aIsX[2], aIsR[2];
uint16_t aIsS[2];  // forme de chaque ile (graine)
int aTier = 0, aCool = 0, aGroups = 0, aSpawnT = 0, aInv = 0, aRoll = 0, aRolls = 3;
bool aBoss = false, aPrevRoll = false, aPaint = true;
float aBX, aBY;
int aBHp, aBMax, aBT = 0, aBFlash = 0;
// sprites : B/b bleu, W blanc, y/o helice et feu, c vitrage, k sombre, G/g couleur de l'ennemi (vert, argent, jaune),
// S/s gris des navires, n/N pont du porte-avions, w sillage
const char *const SPR_A_ME[2][9] = {  // ton chasseur vert fonce (helice qui tourne)
  { "....yyy....", ".....B.....", "....BcB....", "BBBBBBBBBBB", "BWBBBbBBBWB", ".bbBBBBBbb.", ".....B.....", "...BBBBB...", "....bBb...." },
  { "...y...y...", ".....B.....", "....BcB....", "BBBBBBBBBBB", "BWBBBbBBBWB", ".bbBBBBBbb.", ".....B.....", "...BBBBB...", "....bBb...." } };
const char *const SPR_A_ROLL[9] = { "....yyy....", ".....B.....", "....BcB....", "..BBBBBBB..", "..bWBbBWb..", "...bBBBb...", ".....B.....", "...BBBBB...", "....bBb...." };
const char *const SPR_A_FIGHT[2][7] = {  // chasseur ennemi, nez vers le bas
  { "...gGg...", "....G....", "GGGGGGGGG", "gGGGgGGGg", "...GcG...", "....G....", "...ooo..." },
  { "...gGg...", "....G....", "GGGGGGGGG", "gGGGgGGGg", "...GcG...", "....G....", "..o...o.." } };
const char *const SPR_A_BOMBER[11] = {  // bombardier bimoteur
  "......GGGGG......", "........G........", "........G........", "...g....G....g...", "GGGGGGGGGGGGGGGGG",
  "gGGGGgGGGGGgGGGGg", "....ggg.G.ggg....", "....ggg.G.ggg....", "....ooo.G.ooo....", "........G........", ".......GcG......." };
const char *const SPR_A_BOSS[15] = {  // grand quadrimoteur
  "...........GGGGGGG...........", "..............G..............", "..............G..............", "..............G..............",
  ".............GGG.............", "GGGGGGGGGGGGGGGGGGGGGGGGGGGGG", "GGgGGGGGGGGGGGGGGGGGGGGGGGgGG", "gGGGGgGGGGGGGGGGGGGGGGgGGGGGg",
  "..ggg..ggg...GGG...ggg..ggg..", "..ggg..ggg...GGG...ggg..ggg..", "..ooo..ooo...GGG...ooo..ooo..", ".............GGG.............",
  ".............GcG.............", ".............GcG.............", "..............G.............." };
// taille de chaque type : chasseur, bombardier, vedette, destroyer, porte-avions
const uint8_t A_W[5] = { 9, 17, 7, 9, 15 }, A_H[5] = { 7, 11, 18, 28, 40 };

uint16_t aCol(char ch, uint8_t var) {
  static const uint8_t G[3][2][3] = { { { 205, 75, 50 }, { 125, 35, 25 } },  // ennemis rouge brique (ton avion est vert)
                                       { { 190, 190, 205 }, { 105, 105, 120 } }, { { 245, 200, 40 }, { 190, 120, 20 } } };
  switch (ch) {
    case 'B': return var == 3 ? rgb(16, 62, 24) : rgb(34, 118, 46);  // ton avion vert fonce ; var 3 : dessous (tonneau)
    case 'b': return rgb(16, 62, 24);
    case 'W': return rgb(255, 255, 255);
    case 'y': return rgb(255, 220, 0);
    case 'o': return rgb(255, 140, 0);
    case 'c': return rgb(120, 220, 255);
    case 'k': return rgb(40, 40, 50);
    case 'G': return rgb(G[var % 3][0][0], G[var % 3][0][1], G[var % 3][0][2]);
    case 'g': return rgb(G[var % 3][1][0], G[var % 3][1][1], G[var % 3][1][2]);
    case 'S': return rgb(150, 155, 165);
    case 's': return rgb(90, 95, 105);
    case 'n': return rgb(160, 105, 55);
    case 'N': return rgb(230, 210, 170);
    default: return rgb(200, 230, 255);  // w : sillage
  }
}
void aSpr(const char *const *rows, int w, int h, int x, int y, uint8_t var, bool flash) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++) {
      char ch = rows[r][c];
      int px = x + c, py = y + r;
      if (ch == '.' || px < 0 || px >= W || py < A_TOP || py >= H) continue;  // jamais sur la bande du score
      gfx->drawPixel(px, py, flash ? rgb(255, 255, 255) : aCol(ch, var));
    }
  gMark(x, y, w, h);
}

void aNewStage() {
  gWave++;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  aGroups = 0; aSpawnT = 40; aBoss = false; aRolls = max(aRolls, 3);
}
void aIsle(int i, float y) { aIsY[i] = y; aIsX[i] = random(14, W - 14); aIsR[i] = random(6, H >= 128 ? 16 : 12); aIsS[i] = random(65536); }
void aReset() {
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 0; gDead = 0; gBonus = 0; gLifeUp = false;
  aTier = 0; aCool = 0; aInv = 0; aRoll = 0; aRolls = 3;
  aX = W / 2 - 5; aY = H - 12;
  for (auto &e : aEn) e.on = false;
  for (auto &s : aShot) s.on = false;
  for (auto &b : aBomb) b.on = false;
  aCap.on = false;
  for (auto &b : gBoom) b.t = 0;
  for (auto &v : aWave) { v[0] = random(W); v[1] = random(A_TOP, H); }
  aIsle(0, H / 3); aIsle(1, -H / 2);
  aPaint = true;
  aNewStage();
}
void aBombAt(float x, float y, float vx, float vy) {
  for (auto &b : aBomb)
    if (!b.on) { b = { x, y, vx, vy, true }; return; }
}
void aAimed(float x, float y, float speed) {
  float dx = aX + 5 - x, dy = aY + 4 - y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
  aBombAt(x, y, dx / d * speed, dy / d * speed);
}
void aSpread(float x, float y, int n, float speed) {
  for (int i = 0; i < n; i++) {
    float a = PI / 2 + (i - (n - 1) / 2.0f) * 0.3f;
    aBombAt(x, y, cosf(a) * speed, sinf(a) * speed);
  }
}
AEn *aNew() {
  for (auto &e : aEn)
    if (!e.on) { e = {}; e.on = true; return &e; }
  return nullptr;
}
// une vague : V, ligne sinueuse, passage par le cote, bombardier(s), navires, piqueurs
void aSpawnGroup() {
  uint8_t pat = aGroups % 6, var = (aGroups / 2 + gWave) % 3;
  bool left = aGroups & 2;
  if (pat == 4) {  // navires : vedette et destroyer, ou un porte-avions une fois sur deux
    if ((aGroups / 6) & 1) {
      if (AEn *e = aNew()) { e->type = 4; e->pat = 4; e->hp = 24 + 4 * gWave; e->x = random(8, W - 23); e->y = -42; e->carrier = true; }
    } else
      for (int k = 0; k < 2; k++)
        if (AEn *e = aNew()) { e->type = 2 + k; e->pat = 4; e->hp = k ? 9 + gWave : 5 + gWave; e->x = k ? W * 2 / 3 + random(-8, 4) : W / 4 + random(-8, 8); e->y = -20 - k * 34; }
  } else {
    int n = pat == 3 ? (gWave >= 3 ? 2 : 1) : pat == 5 ? 4 : 5;
    for (int k = 0; k < n; k++) {
      AEn *e = aNew();
      if (!e) break;
      e->pat = pat; e->var = var;
      e->type = pat == 3 ? 1 : 0;
      e->hp = e->type ? 10 + 2 * gWave : 1 + (gWave >= 4);
      e->carrier = k == n - 1 && (pat == 0 || pat == 2 || pat == 3);
      if (pat == 0) { e->x = W / 2 - 4 + (k - 2) * 11; e->y = -8 - abs(k - 2) * 6; }               // en V
      else if (pat == 1) { e->x = left ? W / 4 : W * 3 / 4; e->y = -8 - k * 10; }                    // ligne sinueuse
      else if (pat == 2) { e->x = left ? -10 - k * 12 : W + 1 + k * 12; e->y = 16 + k * 3; e->vx = left ? 1.2f : -1.2f; }
      else if (pat == 3) { e->x = n == 1 ? W / 2 - 8 : W * (k + 1) / 3 - 8; e->y = -12; }           // bombardier
      else { e->x = W * (k + 1) / (n + 1) - 4; e->y = -8 - k * 8; }                                   // piqueurs
    }
  }
  aGroups++;
}
void aHit() {
  altLoseLife(aX + 5, aY + 4);
  aTier = max(0, aTier - 1);
}
uint8_t aAuto() {  // demo : suit une cible, esquive les tirs (tonneau si un tir arrive trop pres)
  static float tx = 0;
  static uint8_t hold = 0, dodgeT = 0;
  static int8_t dodge = 0;
  if (hold) hold--;
  else {
    float best = 1e9;
    tx = aX;
    for (auto &e : aEn)
      if (e.on && e.y > 10) {
        float d = fabsf(e.x + A_W[e.type] / 2 - aX - 5) + (H - e.y) * 0.2f;
        if (d < best) { best = d; tx = e.x + A_W[e.type] / 2 - 5; }
      }
    if (aBoss) tx = aBX + 9;
    if (aCap.on) tx = aCap.x - 2;
    hold = 20;
  }
  uint8_t k = 4;
  if (dodgeT) dodgeT--;
  else
    for (auto &b : aBomb)
      if (b.on && b.y > aY - 24 && b.y < aY + 4 && fabsf(b.x - (aX + 5)) < 8) {
        dodge = b.x < aX + 5 ? 1 : -1; dodgeT = 15;
        if (b.y > aY - 8 && aRolls > 1) k |= 32;
        break;
      }
  float t = dodgeT ? aX + dodge * 14 : tx;
  if (t < aX - 3) k |= 1;
  else if (t > aX + 3) k |= 2;
  if (aY < H - 14) k |= 16;
  return k;
}
bool aBox(float x, float y, const AEn &e) { return x >= e.x && x <= e.x + A_W[e.type] && y >= e.y && y <= e.y + A_H[e.type]; }
void aStep() {
  gFrame++;
  uint8_t k = gDemo ? aAuto() : keysPs4;
  float diff = 1 + 0.25f * (gEffDiff() - 1), sea = 0.5f;
  if (gDead == 0) {
    float sp = W > 128 ? 2.5f : 2;
    if (k & 1) aX -= sp;
    if (k & 2) aX += sp;
    if (k & 8) aY -= sp;
    if (k & 16) aY += sp;
    aX = constrain(aX, 0.0f, (float)(W - 11));
    aY = constrain(aY, (float)(H * 4 / 10), (float)(H - 10));
    if (aCool > 0) aCool--;
    if ((k & 4) && aCool == 0 && !aRoll) {  // deux mitrailleuses, puis tirs en eventail selon l'arme
      static const int8_t N[4] = { 2, 3, 4, 5 };
      int n = N[aTier];
      for (int i = 0; i < n; i++)
        for (auto &s : aShot)
          if (!s.on) {
            float vx = 0, x = aX + 5;
            if (i < 2) x += i ? 2 : -2;
            else if (i < 4) vx = (i & 1) ? -0.6f : 0.6f;
            s = { x, aY - 1, vx, -3.8f, true };
            break;
          }
      aCool = 6;
      sfxShot();
    }
    bool rollKey = k & 32;
    if (rollKey && !aPrevRoll && aRolls > 0 && !aRoll) { aRoll = 40; aRolls--; sfxDive(); }
    aPrevRoll = rollKey;
    if (aRoll > 0) aRoll--;
    if (aInv > 0) aInv--;
  } else if (--gDead == 0) {
    if (gLives <= 0) {
      if (gDemo) aReset();
      else if (gLastRank) startNameEntry();
      else toDemo();
      return;
    }
    aX = W / 2 - 5; aY = H - 12; aInv = 90; aRolls = max(aRolls, 2);
  }
  bool safe = aInv > 0 || aRoll > 0 || gDead > 0;
  // mer : vagues et iles qui defilent
  for (auto &v : aWave)
    if ((v[1] += (gFrame & 1)) >= H) { v[1] = A_TOP; v[0] = random(-2, W); }
  for (int i = 0; i < 2; i++) {
    aIsY[i] += sea;
    if (aIsY[i] - aIsR[i] * 2 - 6 > H) aIsle(i, min(aIsY[1 - i] - H * 0.8f - random(H / 2), -30.0f));
  }
  if (gStageT == 0 && !aBoss) {  // vagues puis boss
    bool any = false;
    for (auto &e : aEn) any |= e.on;
    if (aGroups < A_GROUPS) {
      if (--aSpawnT <= 0) { aSpawnGroup(); aSpawnT = 120; }
    } else if (!any) {
      aBoss = true; aBX = W / 2 - 14; aBY = -16; aBT = 0;
      aBMax = aBHp = (50 + 25 * gWave) * (gEffDiff() + 1) / 2;
      sfxDive();
    }
  }
  for (auto &s : aShot) {
    if (!s.on) continue;
    s.x += s.vx; s.y += s.vy;
    if (s.y < A_TOP || s.x < 0 || s.x >= W) s.on = false;
  }
  for (auto &e : aEn) {
    if (!e.on) continue;
    e.t++;
    if (e.flash > 0) e.flash--;
    if (e.pat == 0) e.y += 0.75f * diff;
    else if (e.pat == 1) { e.y += 0.7f * diff; e.x += sinf(e.t * 0.06f) * 1.1f; }
    else if (e.pat == 2) { e.x += e.vx * diff; e.y += 0.3f * diff + (e.t > 60 ? 0.4f : 0); }
    else if (e.pat == 3) { e.y += (e.y < 16 ? 0.5f : 0.22f) * diff; e.x += sinf(e.t * 0.02f) * 0.3f; }
    else if (e.pat == 4) e.y += sea - 0.15f;  // navire : il avance, un peu moins vite que le defilement
    else {  // piqueur : descend, puis pique sur toi
      if (e.t < 45) e.y += 0.6f;
      else {
        if (e.t == 45) { float dx = aX - e.x, dy = aY - e.y, d = sqrtf(dx * dx + dy * dy) + .01f; e.vx = dx / d * 2.2f * diff; e.vy = dy / d * 2.2f * diff; }
        e.x += e.vx; e.y += e.vy;
      }
    }
    if (e.y > H || e.x < -24 || e.x > W + 24) { e.on = false; continue; }
    int cx = e.x + A_W[e.type] / 2;
    if (e.y > A_TOP - 4 && e.y < H * 0.7f && gDead == 0) {  // tirs
      if (e.type == 0 && e.pat != 5 && random(1000) < 5 * diff) aAimed(cx, e.y + 7, 1.2f * diff);
      if (e.type == 1 && e.t % max(35, int(60 / diff)) == 0) aSpread(cx, e.y + 11, 3, 1.1f * diff);
      if (e.type == 2 && e.t % max(45, int(75 / diff)) == 0) aAimed(cx, e.y + 5, 1.3f * diff);
      if (e.type == 3 && e.t % max(40, int(65 / diff)) == 0) { aAimed(cx, e.y + 5, 1.3f * diff); aAimed(cx, e.y + 24, 1.3f * diff); }
      if (e.type == 4 && e.t % 110 == 60)  // porte-avions : lance un chasseur qui pique sur toi
        if (AEn *f = aNew()) { f->type = 0; f->pat = 5; f->var = 1; f->hp = 1; f->x = e.x + 3; f->y = e.y + 6; f->t = 40; }
    }
    for (auto &s : aShot)
      if (s.on && aBox(s.x, s.y, e)) {
        s.on = false;
        e.flash = 3;
        if (--e.hp > 0) break;
        e.on = false;
        static const uint16_t PTS[5] = { 100, 500, 300, 600, 1500 };
        gScore += e.type == 0 && e.pat == 5 ? 150 : PTS[e.type];
        if (!gDemo && gScore > gHi) gHi = gScore;
        gBoomAt(cx, e.y + A_H[e.type] / 2);
        if (e.type) { gBoomAt(cx - 3, e.y + 2); gBoomAt(cx + 3, e.y + A_H[e.type] - 3); }
        sfxBoom();
        if (e.carrier && !aCap.on) aCap = { e.x + A_W[e.type] / 2 - 3, e.y, (uint8_t)(e.type == 4 ? 2 : random(3) == 0 ? 1 : 0), true };
        break;
      }
    // collision avec ton avion (les navires sont sur la mer : pas de choc)
    if (e.on && e.type < 2 && !safe && (aBox(aX + 2, aY + 3, e) || aBox(aX + 8, aY + 3, e) || aBox(aX + 5, aY + 7, e))) {
      e.on = false;
      gBoomAt(cx, e.y + 4);
      aHit();
      safe = true;
    }
  }
  if (aCap.on) {
    aCap.y += 0.6f;
    if (aCap.y > H) aCap.on = false;
    else if (gDead == 0 && aCap.x + 7 >= aX && aCap.x <= aX + 10 && aCap.y + 7 >= aY && aCap.y <= aY + 8) {
      aCap.on = false;
      if (aCap.kind == 0) aTier = min(3, aTier + 1);
      else if (aCap.kind == 1) aRolls = min(5, aRolls + 1);
      else gLives = min(9, gLives + 1);
      gScore += 500;
      sndMelody(JINGLE_ARME, 4);
    }
  }
  if (aBoss) {  // grand quadrimoteur
    aBT++;
    if (aBFlash > 0) aBFlash--;
    bool phase2 = aBHp < aBMax / 2;
    if (aBY < 14) aBY += 0.4f;
    else aBX = W / 2 - 14 + sinf(aBT * (phase2 ? 0.03f : 0.018f)) * (W / 2 - 18);
    if (aBY >= 14) {
      if (aBT % max(55, int((phase2 ? 70 : 95) / diff)) == 0) aSpread(aBX + 14, aBY + 15, phase2 ? 5 : 3, 1.1f * diff);
      if (aBT % 90 == 45) aAimed((aBT / 90) & 1 ? aBX + 24 : aBX + 4, aBY + 10, 1.4f * diff);
      if (phase2 && aBT % 18 == 0) {
        float a = aBT * 0.29f;
        aBombAt(aBX + 14, aBY + 8, cosf(a) * 1.1f * diff, fabsf(sinf(a)) * 1.1f * diff + 0.3f);
      }
    }
    for (auto &s : aShot)
      if (s.on && s.x >= aBX && s.x <= aBX + 28 && s.y >= aBY && s.y <= aBY + 14) {
        s.on = false;
        aBHp--;
        aBFlash = 2;
        if (gFrame & 1) gBoomAt(s.x, s.y);
        break;
      }
    if (aBHp <= 0) {
      aBoss = false;
      for (int i = 0; i < 10; i++) gBoomAt(aBX + random(29), aBY + random(15));
      sfxHit();
      gScore += 2000UL * gWave;
      altStageClear(1000UL * gWave);
      for (auto &b : aBomb) b.on = false;
      aNewStage();
    }
    if (aBoss && !safe && aBX + 28 >= aX + 2 && aBX <= aX + 8 && aBY + 13 >= aY + 1 && aBY <= aY + 8) { aHit(); safe = true; }
  }
  for (auto &b : aBomb) {
    if (!b.on) continue;
    b.x += b.vx; b.y += b.vy;
    if (b.y >= H || b.y < A_TOP || b.x < 0 || b.x >= W) { b.on = false; continue; }
    if (!safe && b.x >= aX + 2 && b.x <= aX + 8 && b.y >= aY + 1 && b.y <= aY + 8) {
      b.on = false;
      aHit();
      safe = true;
    }
  }
  if (gStageT > 0) gStageT--;
}
// efface l'image precedente : noir dans la bande du score, bleu de la mer en dessous
void aErase(uint16_t sea) {
  if (gOverflow || aPaint) {
    gOverflow = false; aPaint = false; gNOld = 0;
    gfx->fillRect(0, 0, W, A_TOP, 0);
    gfx->fillRect(0, A_TOP, W, H - A_TOP, sea);
    return;
  }
  for (int i = 0; i < gNOld; i++) {
    GRect &rc = gOld[i];
    int x0 = max(0, (int)rc.x), y0 = max(0, (int)rc.y), x1 = min(W, rc.x + rc.w), y1 = min(H, rc.y + rc.h);
    if (x1 <= x0 || y1 <= y0) continue;
    if (y0 < A_TOP) gfx->fillRect(x0, y0, x1 - x0, min(y1, A_TOP) - y0, 0);
    if (y1 > A_TOP) gfx->fillRect(x0, max(y0, A_TOP), x1 - x0, y1 - max(y0, A_TOP), sea);
  }
  gNOld = 0;
}
// dessin limite a la mer (jamais dans la bande du score, meme un instant)
void aP(int x, int y, uint16_t c) { if (x >= 0 && x < W && y >= A_TOP && y < H) gfx->drawPixel(x, y, c); }
void aH(int x, int y, int w, uint16_t c) {
  if (y < A_TOP || y >= H) return;
  int x0 = max(0, x), x1 = min(W, x + w);
  if (x1 > x0) gfx->drawFastHLine(x0, y, x1 - x0, c);
}
void aV(int x, int y, int h, uint16_t c) { for (int k = 0; k < h; k++) aP(x, y + k, c); }
void aR(int x, int y, int w, int h, uint16_t c) { for (int k = 0; k < h; k++) aH(x, y + k, w, c); }
void aDisc(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy) + 0.5f);
    aH(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}
// silhouette d'un sprite d'une seule couleur (ombre sur la mer, contour de ton avion)
void aSil(const char *const *rows, int w, int h, int x, int y, uint16_t c) {
  for (int r = 0; r < h; r++)
    for (int q = 0; q < w; q++)
      if (rows[r][q] != '.' && x + q >= 0 && x + q < W && y + r >= A_TOP && y + r < H) gfx->drawPixel(x + q, y + r, c);
  gMark(x, y, w, h);
}
void aTurret(int x, int y, uint16_t c) {  // tourelle ronde, canon tourne vers ton avion
  aDisc(x, y, 1, c);
  float dx = aX + 5 - x, dy = aY + 4 - y, d = sqrtf(dx * dx + dy * dy) + .01f;
  for (int k = 1; k <= 3; k++) aP(x + lroundf(dx / d * k), y + lroundf(dy / d * k), rgb(35, 35, 45));
}
// navires : coque effilee (etrave en haut), vague d'etrave, sillage anime ; vedette, destroyer, porte-avions
void aShip(const AEn &e) {
  int x = e.x, y = e.y, w = A_W[e.type], h = A_H[e.type], cx = x + w / 2, hw0 = w / 2;
  bool fl = e.flash;
  uint16_t wh = rgb(255, 255, 255), edge = fl ? wh : rgb(55, 60, 72), hull = fl ? wh : rgb(150, 156, 170), lite = fl ? wh : rgb(200, 205, 215);
  uint16_t foam = rgb(225, 240, 255), foam2 = rgb(110, 175, 235), dark = rgb(40, 42, 52);
  for (int k = 0; k < 14; k++) {  // sillage : deux traines d'ecume qui s'ecartent, remous au milieu
    int yy = y + h + k, sp = hw0 - 1 + k / 2;
    uint16_t c = k < 6 ? foam : foam2;
    if (((k + (gFrame >> 2)) % 5) != 0) { aP(cx - sp, yy, c); aP(cx + sp, yy, c); }
    if (k < 10 && ((k + (gFrame >> 1)) % 3) == 0) aP(cx + (int)(sinf(k * 1.7f + gFrame * 0.25f) * 1.5f), yy, c);
  }
  aP(cx, y - 1, foam);  // vague d'etrave
  for (int k = 1; k <= 3; k++) {
    uint16_t c = ((gFrame >> 2) + k) & 1 ? foam : foam2;
    aP(cx - k - 1, y + k * 2 - 1, c);
    aP(cx + k + 1, y + k * 2 - 1, c);
  }
  for (int j = 0; j < h; j++) {  // coque
    int hw = min(hw0, j / 2 + (j > 0));
    if (j == h - 1) hw = max(0, hw - 1);
    aH(cx - hw, y + j, 2 * hw + 1, hull);
    aP(cx - hw, y + j, edge);
    aP(cx + hw, y + j, edge);
  }
  if (e.type == 2) {  // vedette : canon avant, passerelle, canon arriere
    aTurret(cx, y + 5, lite);
    aR(cx - 1, y + 8, 3, 4, lite);
    aH(cx - 1, y + 8, 3, dark);
    aTurret(cx, y + 14, lite);
  } else if (e.type == 3) {  // destroyer : deux canons, passerelle, deux cheminees qui fument, canon arriere
    aTurret(cx, y + 5, lite);
    aTurret(cx, y + 9, lite);
    aR(cx - 2, y + 12, 5, 4, lite);
    aH(cx - 2, y + 12, 5, dark);
    aR(cx - 1, y + 17, 3, 2, dark);
    aR(cx - 1, y + 20, 3, 2, dark);
    for (int p = 0; p < 3; p++)  // fumee qui part vers l'arriere
      aP(cx + ((p + (gFrame >> 3)) & 1) - (p == 2), y + 19 + p * 2 + ((gFrame >> 2) & 1), rgb(150 - p * 25, 150 - p * 25, 160 - p * 25));
    aTurret(cx, y + 24, lite);
  } else {  // porte-avions : pont de bois, marquages, ilot, avions
    uint16_t deck = fl ? wh : rgb(165, 110, 60), mark = fl ? wh : rgb(235, 220, 180);
    aR(x + 2, y + 7, 11, 31, deck);
    aV(x + 2, y + 7, 31, mark);
    for (int j = y + 9; j < y + 36; j += 4) aV(cx, j, 2, mark);  // ligne d'envol
    aH(x + 4, y + 33, 7, mark);
    aR(x + 11, y + 17, 3, 9, lite);  // ilot et mat
    aH(x + 11, y + 18, 3, dark);
    aP(x + 12, y + 15 + ((gFrame >> 4) & 1), dark);
    for (int p = 0; p < 3; p++) {  // avions sur le pont
      int px = x + 4 + (p & 1) * 3, py = y + 26 + p * 4;
      uint16_t pc = fl ? wh : aCol('G', 1);
      aH(px, py, 3, pc);
      aP(px + 1, py - 1, pc);
      aP(px + 1, py + 1, pc);
    }
  }
  gMark(cx - hw0 - 7, y - 2, w + 14, h + 16);
}
// iles : formes differentes (plusieurs ronds), lagon en deux tons, ressac, sable, prairie, foret, palmiers, maisons, rochers
void aIsland(int i) {
  int r = aIsR[i], x0 = aIsX[i], y0 = aIsY[i];
  if (y0 + r * 2 + 6 < A_TOP || y0 - r * 2 - 6 > H) return;
  uint32_t sd = aIsS[i] + 1;
  auto rn = [&](int n) { sd = sd * 1103515245u + 12345u; return (int)((sd >> 16) % (uint32_t)max(1, n)); };
  int nb = 2 + rn(3);
  int8_t bx[4], by[4], br[4];
  for (int k = 0; k < nb; k++) {
    bx[k] = k ? rn(r * 2 + 1) - r : 0;
    by[k] = k ? rn(r * 2 + 1) - r : 0;
    br[k] = k ? r / 2 + rn(r / 2 + 1) : r;
  }
  static const int8_t OFF[6] = { 4, 2, 0, -2, -4, -6 };
  uint16_t COL[6] = { rgb(25, 105, 185), rgb(60, 170, 215), rgb(230, 205, 125), rgb(70, 170, 60), rgb(30, 120, 45), rgb(20, 85, 35) };
  for (int l = 0; l < 6; l++)
    for (int k = 0; k < nb; k++)
      if (br[k] + OFF[l] > 0) aDisc(x0 + bx[k], y0 + by[k], br[k] + OFF[l], COL[l]);
  int np = 1 + rn(3);
  for (int p = 0; p < np; p++) {  // palmiers
    int px = x0 + rn(r) - r / 2, py = y0 + rn(r) - r / 2;
    aP(px, py + 1, rgb(130, 85, 35));
    aP(px - 1, py, rgb(60, 210, 80)); aP(px + 1, py, rgb(60, 210, 80));
    aP(px, py - 1, rgb(60, 210, 80)); aP(px, py, rgb(35, 150, 50));
  }
  if (r >= 8 && rn(2)) {  // petites maisons au toit rouge
    int hx = x0 - r / 2 + rn(r / 2), hy = y0 + r / 3;
    aR(hx, hy, 3, 2, rgb(235, 230, 215));
    aH(hx, hy - 1, 3, rgb(205, 55, 40));
    aR(hx + 4, hy + 1, 2, 2, rgb(235, 230, 215));
    aH(hx + 4, hy, 2, rgb(205, 55, 40));
  }
  int rx = x0 + r + 3, ry = y0 - r / 2;  // rochers dans le lagon
  aH(rx, ry, 2, rgb(125, 120, 110));
  aP(rx, ry + 1, rgb(90, 88, 80));
  gMark(x0 - r * 2 - 6, y0 - r * 2 - 6, r * 4 + 13, r * 4 + 13);
}
void aFire() {  // explosions en boules de feu (blanc, jaune, orange, rouge, fumee)
  for (auto &b : gBoom) {
    if (!b.t) continue;
    if (!gPaused) b.t = b.t >= 14 ? 0 : b.t + 1;
    if (!b.t) continue;
    int r = b.t < 5 ? 1 + b.t / 2 : b.t < 10 ? 3 + (b.t - 5) / 3 : 4;
    uint16_t out = b.t < 4 ? rgb(255, 230, 120) : b.t < 8 ? rgb(255, 140, 0) : b.t < 11 ? rgb(220, 40, 20) : rgb(90, 80, 80);
    uint16_t in = b.t < 4 ? rgb(255, 255, 255) : b.t < 8 ? rgb(255, 230, 60) : b.t < 11 ? rgb(255, 140, 0) : rgb(60, 55, 55);
    aDisc(b.x, b.y, r, out);
    if (r > 1) aDisc(b.x, b.y, r - 2 + (b.t & 1), in);
    gMark(b.x - r, b.y - r, 2 * r + 1, 2 * r + 1);
  }
}
void aDraw() {
  uint16_t sea = rgb(10, 60, 150), shadow = rgb(4, 34, 95), palFull[9];
  for (int i = 0; i < 9; i++) palFull[i] = rgb(G_PAL_RGB[i][0], G_PAL_RGB[i][1], G_PAL_RGB[i][2]);
  aErase(sea);
  // mer : houle sombre (longs traits), cretes claires, moutons d'ecume, reflets du soleil qui scintillent
  for (int i = 0; i < A_WAVE; i++) {
    int x = aWave[i][0], y = aWave[i][1], kind = i % 7;
    if (y < A_TOP + 3) continue;
    if (kind < 2) {
      int len = 6 + (i % 5);
      gfx->drawFastHLine(x, y, len, rgb(5, 44, 122));
      gMark(x, y, len, 1);
    } else if (kind < 6) {
      uint16_t c = kind == 5 ? rgb(215, 235, 255) : rgb(70, 140, 225);
      gfx->drawPixel(x, y, c); gfx->drawPixel(x + 3, y, c);
      gfx->drawFastHLine(x + 1, y - 1, 2, c);
      if (kind == 5) gfx->drawPixel(x + 1 + ((gFrame >> 3) & 1), y - 2, rgb(255, 255, 255));
      gMark(x, y - 2, 4, 3);
    } else {
      gPix(x, y, ((gFrame >> 2) + i) % 6 < 2 ? rgb(255, 250, 220) : rgb(60, 120, 210));
    }
  }
  for (int i = 0; i < 2; i++) aIsland(i);
  for (auto &e : aEn)  // navires (sur la mer)
    if (e.on && e.type >= 2) aShip(e);
  // ombres des avions sur la mer (en bas a droite) : on voit qu'ils volent
  for (auto &e : aEn)
    if (e.on && e.type == 0) aSil(SPR_A_FIGHT[0], 9, 7, (int)e.x + 3, (int)e.y + 5, shadow);
    else if (e.on && e.type == 1) aSil(SPR_A_BOMBER, 17, 11, (int)e.x + 4, (int)e.y + 6, shadow);
  if (aBoss) aSil(SPR_A_BOSS, 29, 15, (int)aBX + 5, (int)aBY + 8, shadow);
  bool me = gDead == 0 && !(aInv > 0 && (gFrame & 4));
  if (me) aSil(aRoll ? SPR_A_ROLL : SPR_A_ME[0], 11, 9, (int)aX + 4, (int)aY + 6, shadow);
  if (aCap.on && aCap.y >= A_TOP) {  // capsule : case de couleur et lettre (P arme, T tonneau, + vie), bord qui clignote
    static const char *const L[3][5] = { { "kk.", "k.k", "kk.", "k..", "k.." }, { "kkk", ".k.", ".k.", ".k.", ".k." }, { "...", ".k.", "kkk", ".k.", "..." } };
    int x = aCap.x, y = aCap.y;
    uint16_t c = aCap.kind == 0 ? rgb(255, 200, 0) : aCap.kind == 1 ? rgb(60, 120, 255) : rgb(230, 30, 30);
    gfx->fillRect(x, y, 7, 7, c);
    gfx->drawRect(x, y, 7, 7, (gFrame & 4) ? rgb(255, 255, 255) : rgb(160, 90, 20));
    for (int r = 0; r < 5; r++)
      for (int q = 0; q < 3; q++)
        if (L[aCap.kind][r][q] == 'k') gfx->drawPixel(x + 2 + q, y + 1 + r, aCap.kind ? rgb(255, 255, 255) : rgb(90, 40, 0));
    gMark(x, y, 7, 7);
  }
  for (auto &e : aEn)
    if (e.on && e.type == 0) aSpr(SPR_A_FIGHT[(gFrame / 3) & 1], 9, 7, (int)e.x, (int)e.y, e.var, e.flash);
    else if (e.on && e.type == 1) aSpr(SPR_A_BOMBER, 17, 11, (int)e.x, (int)e.y, e.var % 2, e.flash);
  if (aBoss) {
    aSpr(SPR_A_BOSS, 29, 15, (int)aBX, (int)aBY, 0, aBFlash);
    int bw = W - 20, fill = bw * max(0, aBHp) / aBMax;  // barre de vie
    gfx->drawRect(10, A_TOP + 1, bw, 3, rgb(90, 90, 110));
    gfx->fillRect(10, A_TOP + 2, fill, 1, aBHp < aBMax / 2 ? palFull[7] : palFull[1]);
    gMark(10, A_TOP + 1, bw, 3);
  }
  for (auto &s : aShot)
    if (s.on) { gPix(s.x, s.y, palFull[2]); gPix(s.x, s.y + 1, palFull[7]); }
  for (auto &b : aBomb)
    if (b.on) { gPix(b.x, b.y, palFull[1]); gPix(b.x + 1, b.y, palFull[1]); gPix(b.x, b.y + 1, palFull[7]); gPix(b.x + 1, b.y + 1, palFull[0]); }
  aFire();
  if (me) {  // ton avion : contour sombre tout autour (il se detache de la mer), puis l'avion
    const char *const *spr = aRoll ? SPR_A_ROLL : SPR_A_ME[(gFrame >> 1) & 1];
    uint16_t ring = rgb(0, 8, 30);
    aSil(spr, 11, 9, (int)aX - 1, (int)aY, ring); aSil(spr, 11, 9, (int)aX + 1, (int)aY, ring);
    aSil(spr, 11, 9, (int)aX, (int)aY - 1, ring); aSil(spr, 11, 9, (int)aX, (int)aY + 1, ring);
    aSpr(spr, 11, 9, (int)aX, (int)aY, aRoll && ((aRoll >> 2) & 1) ? 3 : 0, false);
  }
  for (int i = 0; i < aRolls; i++) {  // tonneaux restants en bas a gauche
    gfx->drawCircle(3 + i * 7, H - 4, 2, palFull[4]);
    gMark(1 + i * 7, H - 6, 5, 5);
  }
  altHud(palFull);
}

// ---------------------------------------------------------------- BUNKER 3D
// Jeu de tir original en vue subjective (lancer de rayons) : tu avances dans une base souterraine, murs textures,
// sol et plafond en perspective, brouillard avec la distance. Robots ennemis (drone, sentinelle, colosse) qui te
// traquent et tirent du plasma ; portes coulissantes ; soins, munitions, armure, arme lourde ; carte rouge pour
// actionner la sortie ; barils explosifs (en chaine). Chaque stage : nouveau bunker genere, plus d'ennemis.
// Commandes : stick gauche / croix avancer et pivoter, stick droit pivoter, L1 / R1 pas de cote, X ou R2 tirer,
// carre ouvrir / actionner, rond changer d'arme, triangle mini-carte. L2 ou OPTIONS : pause.
#define Z_N 24       // carte Z_N x Z_N cases
#define Z_MOBS 22
#define Z_SHOTS 12
#define Z_ITEMS 24
#define Z_ROOMS 9
#define Z_TEXS 8     // textures : 1 briques, 2 metal, 3 technique, 4 pierre, 5 porte, 6 porte rouge, 7 sortie
#define Z_SPR (Z_MOBS + Z_ITEMS + Z_SHOTS + 6 + 8)  // objets visibles : robots, objets, tirs, etincelles, 8 fumees
struct ZSpr { float tx, ty, wx, wy, lr, lg, lb; int16_t sx, w, h, top, fy; const char *const *art; uint8_t aw, ah, kind; int8_t flash; };
// Toute la memoire de BUNKER 3D (~12 Ko) dans un bloc reserve au lancement du jeu et rendu des qu'on change de jeu :
// les autres jeux et le Bluetooth (connexion des manettes) gardent cette memoire libre.
struct ZData {
  uint8_t map[Z_N][Z_N], open[Z_N][Z_N], roomOf[Z_N][Z_N];
  uint16_t doorT[Z_N][Z_N];  // images avant fermeture d'une porte ouverte
  ZMob mob[Z_MOBS];
  ZShot shot[Z_SHOTS];
  ZItem item[Z_ITEMS];
  ZShot spark[6];  // etincelles d'impact (t : age)
  float zbuf[256], vigY[256], rowD[256];
  ZSpr spr[Z_SPR];
  uint8_t bfsFrom[Z_N][Z_N];
  int16_t bfsQ[Z_N * Z_N];
};
ZData *zD = nullptr;
#define zMap (zD->map)
#define zOpen (zD->open)
#define zRoomOf (zD->roomOf)
#define zDoorT (zD->doorT)
#define zMob (zD->mob)
#define zShot (zD->shot)
#define zItem (zD->item)
#define zSpark (zD->spark)
#define zZbuf (zD->zbuf)
int8_t zRoom[Z_ROOMS][4];  // x, y, largeur, hauteur
uint8_t zNRooms = 0;
float zVX = 0, zVY = 0, zPX, zPY, zA, zBob = 0;
int zHp = 100, zArmor = 0, zAmmo = 50, zWeapon = 0, zFireT = 0, zFlash = 0, zHurt = 0, zPick = 0, zMsgT = 0;
int zShake = 0, zHurtDir = 0, zHitT = 0, zHitKill = 0, zCombo = 0, zKillT = 0;  // tremblement, cote du tir recu, viseur touche, combo
bool zKey = false, zHeavy = false, zMapOn = false, zPrevUse = false, zPrevSw = false, zPrevMap = false;
int8_t zExitX = 0, zExitY = 0;
char zMsg[24] = "";
GFXcanvas16 *zHudCv = nullptr;
int zHudKey = -1;
int zVH() { return H - (H >= 128 ? 12 : 8); }  // hauteur de la vue 3D (barre d'etat en bas)

// ---- sprites (16 x 16 sauf objets) : k contour, S/s metal, c cyan, R/r rouge, W blanc, o orange, v armure du colosse,
// y jaune, G vert, g gant, b bleu, h reflet, d metal sombre, m metal moyen, a blindage clair, e ombre profonde
const char *const ZS_DRONE[2][24] = {  // drone flottant (24 x 24) : rotor qui tourne, flamme du reacteur
  { "..sShhhhhhkddkhhhhhhSs..", "..........kddk..........", "........kkkSSkkk........", "......kkhhSSSShhkk......", ".....khhSSSSSSSShhk.....", "....khSSSSmmmmSSSShk....", "...khSSmmkkkkkkmmSShk...", "...kSSmkkeeeeeekkmSSk...", "kk.kSmkeerrrrrreekmSk.kk", "kckkSmkerrRRRRrrekmSkkck", "kcSSSmkerRRWWRRrekmSSSck", "kcSmSmkerRWWWWRrekmSmSck", "kkkkmmkerRRRRRRrekmmkkkk", "...kmmkeerrrrrreekmmk...", "...kmmmkkeeeeeekkmmmk...", "...kemmmmkkkkkkmmmmek...", "....kemmssmmmmssmmek....", ".....kessssssssssek.....", "......kkessssssekk......", "........kkddddkk........", "..........kddk..........", "...........oo...........", "...........yy...........", "........................" },
  { "......sShhkddkhhSs......", "..........kddk..........", "........kkkSSkkk........", "......kkhhSSSShhkk......", ".....khhSSSSSSSShhk.....", "....khSSSSmmmmSSSShk....", "...khSSmmkkkkkkmmSShk...", "...kSSmkkeeeeeekkmSSk...", "kk.kSmkeerrrrrreekmSk.kk", "kckkSmkerrRRRRrrekmSkkck", "kcSSSmkerRRWWRRrekmSSSck", "kcSmSmkerRWWWWRrekmSmSck", "kkkkmmkerRRRRRRrekmmkkkk", "...kmmkeerrrrrreekmmk...", "...kmmmkkeeeeeekkmmmk...", "...kemmmmkkkkkkmmmmek...", "....kemmssmmmmssmmek....", ".....kessssssssssek.....", "......kkessssssekk......", "........kkddddkk........", "..........kddk..........", "...........yy...........", "........................", "........................" } };
const char *const ZS_SENT[2][24] = {  // sentinelle (24 x 24) : 2 images de marche ; visiere et canons (c) flambent au tir
  { "........kkkkkkkk........", "......kkhhSSSShhkk......", ".....khSSSSSSSSSShk.....", ".....kSkkkkkkkkkkSk.....", ".....kSkcccccccckSk.....", ".....kSkcWccccWckSk.....", ".....kmkkkkkkkkkkmk.....", "..kkkkkdmmmmmmmmdkkkkk..", ".khhSSkkSSSSSSSSkkSShhk.", "khSSSSSkShkkkkhSkSSSSShk", "kSkkkkSkSkooookSkSkkkkSk", "kSkcckSkSkoWWokSkSkcckSk", "kskcckskmkooookmkskccksk", "kskkkksksmkkkkmskskkkksk", ".kk..kkdkmmmmmmkdkk..kk.", ".....kmmkkddddkkmmk.....", "....kmSSk......kSSmk....", "....kmSSk......kSSmk....", "....kesk........ksek....", "....kmSk........kSmk....", "....kmSk........kSmk....", "...kmSSk........kSSmk...", "..kmSSSk........kSSSmk..", "..kkkkkk........kkkkkk.." },
  { "........kkkkkkkk........", "......kkhhSSSShhkk......", ".....khSSSSSSSSSShk.....", ".....kSkkkkkkkkkkSk.....", ".....kSkcccccccckSk.....", ".....kSkcWccccWckSk.....", ".....kmkkkkkkkkkkmk.....", "..kkkkkdmmmmmmmmdkkkkk..", ".khhSSkkSSSSSSSSkkSShhk.", "khSSSSSkShkkkkhSkSSSSShk", "kSkkkkSkSkooookSkSkkkkSk", "kSkcckSkSkoWWokSkSkcckSk", "kskcckskmkooookmkskccksk", "kskkkksksmkkkkmskskkkksk", ".kk..kkdkmmmmmmkdkk..kk.", "....kmSSk...ddkkmmk.....", "....kmSSk......kSSmk....", "....kesk.......kSSmk....", "....kmSk........ksek....", "....kmSk........kSmk....", "...kmSSk........kSmk....", "..kmSSSk........kSSmk...", "..kkkkkk........kSSSmk..", "................kkkkkk.." } };
const char *const ZS_BRUTE[2][24] = {  // colosse blinde (24 x 24) : 2 images de marche
  { "......kkkkkkkkkkkk......", ".....kaavvvvvvvvaak.....", "....kavvkkkkkkkkvvak....", "....kavkRRrkkrRRkvak....", "....kvvkkkkkkkkkkvvk....", "..kkkkvvvvvvvvvvvvkkkk..", ".kaaavkkavvvvvvakkvaaak.", "kaavvvvkavoooovakvvvvaak", "kavvvvvkavokkovakvvvvvak", "kvvkkvvkvvoooovvkvvkkvvk", "kvk.kvvkvvvvvvvvkvvk.kvk", "kvk.kvvkkvvkkvvkkvvk.kvk", "kgk.kvvvkkkkkkkkvvvk.kgk", "kggk.kvvvvvvvvvvvvk.kggk", "kggk..kvvkvvvvkvvk..kggk", ".kk...kvvkkkkkkvvk...kk.", ".....kavvk....kvvak.....", ".....kvvvk....kvvvk.....", ".....kdvvk....kvvdk.....", ".....kvvvk....kvvvk.....", "....kavvvk....kvvvak....", "...kaavvvk....kvvvaak...", "...kkkkkkk....kkkkkkk...", "........................" },
  { "......kkkkkkkkkkkk......", ".....kaavvvvvvvvaak.....", "....kavvkkkkkkkkvvak....", "....kavkRRrkkrRRkvak....", "....kvvkkkkkkkkkkvvk....", "..kkkkvvvvvvvvvvvvkkkk..", ".kaaavkkavvvvvvakkvaaak.", "kaavvvvkavoooovakvvvvaak", "kavvvvvkavokkovakvvvvvak", "kvvkkvvkvvoooovvkvvkkvvk", "kvk.kvvkvvvvvvvvkvvk.kvk", "kvk.kvvkkvvkkvvkkvvk.kvk", "kgk.kvvvkkkkkkkkvvvk.kgk", "kggk.kvvvvvvvvvvvvk.kggk", "kggk..kvvkvvvvkvvk..kggk", ".kk...kvvkkkkkkvvk...kk.", ".....kvvvk....kvvak.....", ".....kdvvk....kvvvk.....", ".....kvvvk....kvvdk.....", "....kavvvk....kvvvk.....", "...kaavvvk....kvvvak....", "...kkkkkkk....kvvvaak...", "..............kkkkkkk...", "........................" } };
const char *const ZS_SCRAP[16] = {  // debris d'un robot detruit
  "................", "................", "................", "................", "................", "................",
  "................", "................", "................", "................", "................", "...k.....k......",
  "..kSk..kSSk..k..", ".kSsSkkSrSSkkSk.", "kSSsSSSSSsSSSSSk", "kkkkkkkkkkkkkkkk" };
const char *const ZS_COLUMN[16] = {  // colonne technique lumineuse
  "....kkkkkkkk....", "....kSSSSSSk....", ".....kSSSSk.....", ".....kcccck.....", ".....kcWWck.....", ".....kcccck.....",
  ".....kSSSSk.....", ".....kcccck.....", ".....kcWWck.....", ".....kcccck.....", ".....kSSSSk.....", ".....kcccck.....",
  ".....kcWWck.....", ".....kcccck.....", "....kSSSSSSk....", "....kkkkkkkk...." };
const char *const ZS_BARREL[16] = {  // baril explosif
  "................", "....kkkkkkkk....", "...kyyyyyyyyk...", "...kokkkkkkok...", "...kyyyyyyyyk...", "...kyykkkkyyk...",
  "...kyykWWkyyk...", "...kyyykkyyyk...", "...kyyykkyyyk...", "...kyyyyyyyyk...", "...kyyykkyyyk...", "...kyyyyyyyyk...",
  "...kokkkkkkok...", "...kyyyyyyyyk...", "....kkkkkkkk....", "................" };
const char *const ZS_ITEM[5][8] = {
  { "kkkkkkkk", "kWWWWWWk", "kWWRRWWk", "kRRRRRRk", "kRRRRRRk", "kWWRRWWk", "kWWWWWWk", "kkkkkkkk" },  // soin
  { "kkkkkkkk", "kyyyyyyk", "kyokokyk", "kyyyyyyk", "kyokokyk", "kyyyyyyk", "kyyyyyyk", "kkkkkkkk" },  // munitions
  { "..k..k..", ".kGkkGk.", "kGGGGGGk", "kGGWWGGk", "kGGGGGGk", "kGGGGGGk", ".kGGGGk.", "..kkkk.." },  // armure
  { "........", "kkkkkkkk", "kRRRRRRk", "kRWWRRRk", "kRRRRRRk", "kkkkkkkk", "........", "........" },  // carte rouge
  { "........", "kkkkkkk.", "kSSSSSok", "kSccSSSk", "kkkSkkk.", "..kSk...", "..kkk...", "........" } };  // arme lourde
const char *const ZS_GUN[2][16] = {  // arme en main (28 x 16), tenue a deux mains : blaster, puis arme lourde a double canon
  { "...........kkkkkk...........", "..........kSWWWWSk..........", "..........kSccccSk..........", "..........kSccccSk..........", ".........kkSccccSkk.........", ".........kSSSccSSSk.........", "........kSSsSccSsSSk........", "........kSSsSSSSsSSk........", ".......kkSSSSkkSSSSkk.......", ".......kSSSSSkkSSSSSk.......", "......kSSssSSSSSSssSSk......", ".....kggkSSSSSSSSSSkggk.....", "....kggggkkSSSSSSkkggggk....", "...kgggggggkkkkkkgggggggk...", "..kggggggggggggggggggggggk..", ".kggggggggggggggggggggggggk." },
  { "........kkk......kkk........", "........kSk......kSk........", "........kok......kok........", "........kSk......kSk........", ".......kkSkkkkkkkkSkk.......", ".......kSSSSSSSSSSSSk.......", "......kSoSSoSSSSoSSoSk......", "......kSSSSSSSSSSSSSSk......", ".....kkSSSkkSSSSkkSSSkk.....", ".....kSSSSkkSSSSkkSSSSk.....", "....kSSssSSSSSSSSSSssSSk....", "...kggkSSSSSSSSSSSSSSkggk...", "..kggggkkSSSSSSSSSSkkggggk..", ".kgggggggkkkkkkkkkkgggggggk.", "kggggggggggggggggggggggggggk", "kggggggggggggggggggggggggggk" } };

void zPal(char ch, uint8_t &r, uint8_t &g, uint8_t &b) {
  static const char C[] = "kSscRrWovyGgbhdmae";
  static const uint8_t P[18][3] = { { 18, 18, 22 }, { 150, 155, 170 }, { 95, 100, 115 }, { 80, 230, 255 }, { 255, 40, 30 }, { 150, 20, 20 },
                                    { 255, 255, 255 }, { 255, 140, 0 }, { 120, 95, 70 }, { 255, 210, 0 }, { 60, 200, 70 }, { 85, 95, 60 }, { 60, 120, 255 }, { 210, 215, 230 }, { 62, 65, 78 },
                                    { 120, 126, 142 }, { 176, 146, 104 }, { 34, 36, 44 } };
  const char *p = strchr(C, ch);
  int i = p && *p ? p - C : 0;
  r = P[i][0]; g = P[i][1]; b = P[i][2];
}
uint32_t zHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
// textures des murs calculees a pleine resolution (32 points par case) : u, v de 0 a 1 ; id : variation de chaque mur
void zTexel(int t, float u, float v, uint32_t id, float &r, float &g, float &b) {
  int px = (int)(u * 32), py = (int)(v * 32);
  float n = (zHash(id * 1031 + py * 37 + px) & 31) - 16;  // grain fin
  if (t == 1) {  // briques bleues en relief
    int row = py / 5, iy = py % 5, bx = px + (row & 1) * 5, col = bx / 10, ix = bx % 10;
    uint32_t bh = zHash(id * 97 + row * 13 + col);
    if (ix == 9 || iy == 4) { r = 30; g = 36 + n * 0.2f; b = 62 + n * 0.3f; }  // joint sombre
    else {
      float k = 0.82f + (bh & 15) / 50.0f;
      r = 62 * k + n * 0.3f; g = 98 * k + n * 0.5f; b = 210 * k + n;
      if (iy == 0 || ix == 0) { r += 18; g += 28; b += 40; }      // arete eclairee
      else if (iy == 3 || ix == 8) { r *= 0.75f; g *= 0.75f; b *= 0.75f; }  // ombre
      if ((bh >> 8) % 11 == 0) { r *= 0.62f; g *= 0.58f; b *= 0.55f; }    // brique noircie
    }
  } else if (t == 2) {  // plaques de metal rivetees
    int ix = px % 16, iy = py % 16;
    float c = 118 + n * 0.8f + (iy < 8 ? 8 : 0);
    if (ix == 0 || iy == 0) c = 52;
    else if (ix == 1 || iy == 1) c = 180;
    else if (ix == 15 || iy == 15) c = 82;
    if ((ix == 3 || ix == 12) && (iy == 3 || iy == 12)) c = 68;
    if ((ix == 2 || ix == 11) && (iy == 2 || iy == 11)) c = 235;
    r = c * 0.95f; g = c; b = c * 1.1f;
    if ((zHash(id) & 3) == 0 && py >= 13 && py <= 18) {  // bande de danger
      bool hz = ((px + py) / 3) & 1;
      r = hz ? 230 : 30; g = hz ? 180 : 30; b = hz ? 20 : 30;
    }
  } else if (t == 3) {  // panneau technique : ecran, bande lumineuse qui defile, grille
    r = 25 + n * 0.5f; g = 45 + n; b = 95 + n;
    if (px < 2 || px > 29 || py < 2 || py > 29) { r = 55; g = 60; b = 75; if (px == 0 || py == 0) { r = 125; g = 130; b = 145; } }
    else if (py >= 6 && py <= 13 && px >= 5 && px <= 26) {
      r = 5; g = 30; b = 15;
      if (py % 2 == 0 && ((zHash(id + py * 7 + (gFrame >> 4)) >> (px & 15)) & 1)) { r = 50; g = 235; b = 100; }
    } else if (py == 20 || py == 21) {
      bool on = ((px + (gFrame >> 1)) % 12) < 3;
      r = on ? 255 : 40; g = on ? 130 : 70; b = on ? 40 : 160;
    } else if (py >= 24 && py <= 27 && (px % 4) == 0) { r = 10; g = 15; b = 30; }
  } else if (t == 4) {  // pierre taillee, mousse en bas
    int row = py / 8, iy = py % 8, bx = px + zHash(id + row * 7) % 8, col = bx / 8, ix = bx % 8;
    float c = 92 + (zHash(id * 31 + row * 5 + col) & 31);
    if (iy == 7 || ix == 7) c = 42;
    else if (iy == 0 || ix == 0) c += 30;
    c += n * 0.6f;
    r = c * 0.86f; g = c * 0.9f; b = c * 0.8f;
    if (py > 22 && (int)(zHash(px * 3 + id) % 9) < py - 22) { r *= 0.55f; g = g * 0.55f + 55; b *= 0.5f; }
  } else if (t == 5 || t == 6) {  // portes : nervures, joint central, bandes, voyants
    float c = 125 + n * 0.6f;
    if (py % 6 == 0) c = 178;
    else if (py % 6 == 5) c = 70;
    if (px == 15 || px == 16) c = 40;
    r = c; g = c + 2; b = c + 10;
    if (py >= 13 && py <= 18) {
      bool hz = ((px + py) / 3) & 1;
      if (t == 5) { r = hz ? 245 : 25; g = hz ? 190 : 25; b = hz ? 0 : 25; }
      else { r = hz ? 230 : 70; g = hz ? 25 : 10; b = hz ? 25 : 10; }
    }
    if ((px == 3 || px == 28) && py >= 4 && py <= 7) {
      float on = (gFrame >> 3) & 1 ? 1 : 0.4f;
      r = (t == 6 ? 255 : 40) * on; g = (t == 6 ? 40 : 255) * on; b = 40 * on;
    }
  } else {  // sortie : panneau vert, fleche, cadre lumineux qui tourne
    r = 15; g = 110 + n; b = 35;
    if (px < 2 || px > 29 || py < 2 || py > 29) {
      bool on = ((px + py + (gFrame >> 1)) % 6) < 3;
      r = on ? 255 : 30; g = on ? 255 : 80; b = on ? 120 : 30;
    }
    int ax = abs(px - 16);
    if ((py >= 6 && py <= 14 && ax <= py - 6) || (py > 14 && py <= 25 && ax <= 2)) { r = 230; g = 255; b = 230; }
  }
  if ((t == 1 || t == 4) && zHash(id + 5) % 4 == 0) {  // applique lumineuse et son halo sur le mur
    float du = u - 0.5f, dv = v - 0.34f, d = sqrtf(du * du + dv * dv * 1.4f);
    if (fabsf(du) < 0.07f && dv > -0.06f && dv < 0.05f) { r = 255; g = 235; b = 160; }
    else if (fabsf(du) < 0.1f && dv >= 0.05f && dv < 0.09f) { r = 70; g = 60; b = 50; }
    else if (d < 0.35f) { float k = (0.35f - d) / 0.35f; r += 120 * k; g += 85 * k; b += 30 * k; }
  }
}
bool zWall(int x, int y) { return x < 0 || y < 0 || x >= Z_N || y >= Z_N || (zMap[y][x] && !((zMap[y][x] == 5 || zMap[y][x] == 6) && zOpen[y][x] >= 14)); }
bool zFree(float x, float y, float r) {
  if (zWall((int)(x - r), (int)(y - r)) || zWall((int)(x + r), (int)(y - r)) || zWall((int)(x - r), (int)(y + r)) || zWall((int)(x + r), (int)(y + r))) return false;
  for (auto &m : zMob)  // colonnes (objets solides)
    if (m.on && (m.type == 3 || (m.type == 4 && m.state < 3)) && fabsf(m.x - x) < 0.35f + r && fabsf(m.y - y) < 0.35f + r) return false;
  return true;
}
void zMsgSet(const char *s) { strncpy(zMsg, s, sizeof(zMsg) - 1); zMsgT = 80; }
int zFindCell(int room, int &cx, int &cy) {  // case libre au hasard dans une salle
  for (int k = 0; k < 30; k++) {
    cx = zRoom[room][0] + random(zRoom[room][2]); cy = zRoom[room][1] + random(zRoom[room][3]);
    bool used = false;
    for (auto &m : zMob) used |= m.on && (int)m.x == cx && (int)m.y == cy;
    for (auto &it : zItem) used |= it.on && it.x == cx && it.y == cy;
    if (!used && !zMap[cy][cx]) return 1;
  }
  return 0;
}
ZMob *zNewMob() { for (auto &m : zMob) if (!m.on) { m = {}; m.on = true; return &m; } return nullptr; }
ZItem *zNewItem() { for (auto &i : zItem) if (!i.on) { i = {}; i.on = true; return &i; } return nullptr; }
// nouveau bunker : salles reliees par des couloirs, portes, carte rouge, sortie, ennemis, objets, colonnes
void zGenerate() {
  randomSeed(esp_random());
  uint8_t corr[Z_N][Z_N];
  memset(zMap, 1, sizeof(zMap)); memset(zOpen, 0, sizeof(zOpen)); memset(zDoorT, 0, sizeof(zDoorT));
  memset(zRoomOf, 255, sizeof(zRoomOf)); memset(corr, 0, sizeof(corr));
  zNRooms = 0;
  for (int tries = 0; (tries < 300 || zNRooms < 3) && zNRooms < Z_ROOMS; tries++) {
    if (tries > 2000) { tries = 0; zNRooms = 0; }
    int w = random(3, 7), h = random(3, 7), x = random(1, Z_N - w - 1), y = random(1, Z_N - h - 1);
    bool ok = true;
    for (int k = 0; k < zNRooms && ok; k++)
      ok = x + w + 1 < zRoom[k][0] || zRoom[k][0] + zRoom[k][2] + 1 < x || y + h + 1 < zRoom[k][1] || zRoom[k][1] + zRoom[k][3] + 1 < y;
    if (!ok) continue;
    zRoom[zNRooms][0] = x; zRoom[zNRooms][1] = y; zRoom[zNRooms][2] = w; zRoom[zNRooms][3] = h;
    zNRooms++;
  }
  for (int k = 0; k < zNRooms; k++) {  // salles : murs d'une texture par salle
    uint8_t tex = 1;  // murs en briques bleues (textures 2 metal, 3 technique, 4 pierre : en reserve)
    for (int y = zRoom[k][1] - 1; y <= zRoom[k][1] + zRoom[k][3]; y++)
      for (int x = zRoom[k][0] - 1; x <= zRoom[k][0] + zRoom[k][2]; x++) {
        bool in = x >= zRoom[k][0] && x < zRoom[k][0] + zRoom[k][2] && y >= zRoom[k][1] && y < zRoom[k][1] + zRoom[k][3];
        if (in) { zMap[y][x] = 0; zRoomOf[y][x] = k; }
        else if (zMap[y][x]) zMap[y][x] = tex;
      }
  }
  for (int k = 0; k + 1 < zNRooms; k++) {  // couloirs en L d'une salle a la suivante
    int x = zRoom[k][0] + zRoom[k][2] / 2, y = zRoom[k][1] + zRoom[k][3] / 2;
    int tx = zRoom[k + 1][0] + zRoom[k + 1][2] / 2, ty = zRoom[k + 1][1] + zRoom[k + 1][3] / 2;
    bool hFirst = random(2);
    while (x != tx || y != ty) {
      if ((hFirst && x != tx) || y == ty) x += x < tx ? 1 : -1;
      else y += y < ty ? 1 : -1;
      if (zRoomOf[y][x] == 255) { if (zMap[y][x]) zMap[y][x] = 0; corr[y][x] = corr[y][x] ? 200 : k + 1; }
    }
  }
  int last = zNRooms - 1;
  for (int y = 1; y < Z_N - 1; y++)  // portes : case de couloir a l'entree d'une salle, entre deux murs
    for (int x = 1; x < Z_N - 1; x++) {
      if (!corr[y][x] || zMap[y][x]) continue;
      bool horiz = zMap[y - 1][x] && zMap[y + 1][x] && !zMap[y][x - 1] && !zMap[y][x + 1];
      bool vert = zMap[y][x - 1] && zMap[y][x + 1] && !zMap[y - 1][x] && !zMap[y + 1][x];
      if (!horiz && !vert) continue;
      int r1 = horiz ? zRoomOf[y][x - 1] : zRoomOf[y - 1][x], r2 = horiz ? zRoomOf[y][x + 1] : zRoomOf[y + 1][x];
      if (r1 == 255 && r2 == 255) continue;
      bool toExit = (r1 == last || r2 == last) && corr[y][x] == last;  // couloir propre a la salle de sortie : porte rouge
      zMap[y][x] = toExit ? 6 : 5;
    }
  // sortie : un mur de la derniere salle devient le panneau de sortie
  zExitX = zRoom[last][0] + zRoom[last][2] / 2; zExitY = zRoom[last][1] - 1;
  for (int k = 0; k < zRoom[last][2] * 2; k++) {  // mur du haut, sinon du bas, qui n'est pas un passage
    int x = zRoom[last][0] + (k % zRoom[last][2]), y = k < zRoom[last][2] ? zRoom[last][1] - 1 : zRoom[last][1] + zRoom[last][3];
    if (zMap[y][x] >= 1 && zMap[y][x] <= 4) { zExitX = x; zExitY = y; break; }
  }
  zMap[zExitY][zExitX] = 7;
  for (auto &m : zMob) m.on = false;
  for (auto &s : zShot) s.on = false;
  for (auto &s : zSpark) s.on = false;
  for (auto &i : zItem) i.on = false;
  // toi : au centre de la premiere salle
  zPX = zRoom[0][0] + zRoom[0][2] / 2 + 0.5f; zPY = zRoom[0][1] + zRoom[0][3] / 2 + 0.5f;
  zA = atan2f(zRoom[1][1] - zRoom[0][1], zRoom[1][0] - zRoom[0][0]);
  int cx, cy;
  for (int k = 0; k < zNRooms; k++)  // colonnes dans les grandes salles
    if (zRoom[k][2] >= 5 && zRoom[k][3] >= 5 && k != 0)
      for (int c = 0; c < 2; c++)
        if (ZMob *m = zNewMob()) { m->type = 3; m->hp = 99; m->x = zRoom[k][0] + 1.5f + c * (zRoom[k][2] - 3); m->y = zRoom[k][1] + 1.5f; m->state = 4; }
  for (int k = 1; k < zNRooms; k++)  // barils explosifs
    if (random(3) && zFindCell(k, cx, cy))
      if (ZMob *m = zNewMob()) { m->type = 4; m->hp = 10; m->x = cx + 0.5f; m->y = cy + 0.5f; }
  int keyRoom = zNRooms > 2 ? 1 + random(zNRooms - 2) : 0;  // carte rouge : pas dans la salle de sortie
  if (zFindCell(keyRoom, cx, cy)) if (ZItem *it = zNewItem()) { it->x = cx; it->y = cy; it->type = 3; }
  if (!zHeavy && zNRooms > 2 && zFindCell(2, cx, cy)) if (ZItem *it = zNewItem()) { it->x = cx; it->y = cy; it->type = 4; }
  for (int k = 1; k < zNRooms; k++)  // un objet par salle
    if (zFindCell(k, cx, cy)) if (ZItem *it = zNewItem()) { it->x = cx; it->y = cy; it->type = random(3); }
  int n = 5 + 2 * gWave;
  for (int e = 0; e < n; e++) {  // ennemis : jamais dans la premiere salle
    int k = 1 + random(max(1, zNRooms - 1));
    if (!zFindCell(k, cx, cy)) continue;
    if (ZMob *m = zNewMob()) {
      int r = random(10);
      m->type = gWave >= 3 && r < 2 ? 2 : gWave >= 2 && r < 6 ? 1 : r < 7 ? 0 : 1;
      m->hp = m->type == 0 ? 20 : m->type == 1 ? 35 : 90;
      m->x = cx + 0.5f; m->y = cy + 0.5f;
      m->cool = random(40, 90);
    }
  }
  zKey = false;
  zHudKey = -1;
}
void zNewStage() {
  gWave++;
  gStageT = 100;
  sndMelody(JINGLE_STAGE, 4);
  zGenerate();
}
// memoire de BUNKER 3D : reservee au lancement, rendue quand on passe a un autre jeu
bool zAlloc() {
  if (!zD) zD = (ZData *)calloc(1, sizeof(ZData));
  if (zD && !zHudCv) {
    zHudCv = new GFXcanvas16(W, H - zVH());
    if (!zHudCv->getBuffer()) { delete zHudCv; zHudCv = nullptr; }
  }
  if (!zD || !zHudCv) { Serial.printf("BUNKER 3D : memoire insuffisante (%u libres)\n", ESP.getFreeHeap()); return false; }
  return true;
}
void zRelease() {
  if (zHudCv) { delete zHudCv; zHudCv = nullptr; }
  if (zD) { free(zD); zD = nullptr; }
}
void zReset() {
  if (!zAlloc()) { gGame = 0; topLoad(); gReset(); return; }  // pas assez de memoire : on joue au premier jeu
  gLives = (!gDemo && gDiff == 0) ? 5 : 3;
  gPaused = false;
  gScore = 0; gWave = 0; gDead = 0; gBonus = 0; gLifeUp = false;
  zHp = 100; zArmor = 0; zAmmo = 50; zWeapon = 0; zHeavy = false; zHurt = 0; zFlash = 0; zPick = 0; zMsgT = 0; zMapOn = false;
  zShake = 0; zHurtDir = 0; zHitT = 0; zCombo = 0; zKillT = 0;
  zNewStage();
}
// ligne de vue entre deux points (pas de mur ni de porte fermee)
bool zSight(float x0, float y0, float x1, float y1) {
  float dx = x1 - x0, dy = y1 - y0, d = sqrtf(dx * dx + dy * dy);
  int n = (int)(d * 4) + 1;
  for (int i = 1; i < n; i++) {
    float t = (float)i / n;
    if (zWall((int)(x0 + dx * t), (int)(y0 + dy * t))) return false;
  }
  return true;
}
void zHurtMe(int dmg, int dir = 0) {  // dir : -1 tir venu de gauche, 1 de droite, 0 de face
  if (gDead || gStageT > 60) return;
  zHurtDir = dir;
  int a = min(zArmor, dmg / 2);
  zArmor -= a; zHp -= dmg - a;
  zHurt = 10;
  zShake = max(zShake, 5);
  if (!gDemo) rumbleReq = true;
  sfxBoom();
  if (zHp <= 0) {
    zHp = 0;
    altLoseLife(W / 2, zVH() / 2);
  }
}
void zFireShot(float x, float y, float tx, float ty, float sp) {
  for (auto &s : zShot)
    if (!s.on) {
      float dx = tx - x, dy = ty - y, d = sqrtf(dx * dx + dy * dy) + 0.01f;
      s = { x, y, dx / d * sp, dy / d * sp, 0, true };
      return;
    }
}
void zKill(ZMob &m);
// baril qui explose : blesse les robots et toi autour, fait sauter les barils voisins
void zBoom(ZMob &b) {
  b.state = 3; b.t = 0;
  sfxHit();
  if (!gDemo) rumbleReq = true;
  gScore += 30;
  { float db = hypotf(zPX - b.x, zPY - b.y); zShake = max(zShake, db < 5 ? 14 : db < 9 ? 7 : 0); }  // l'explosion secoue la vue
  for (auto &m : zMob) {
    if (!m.on || &m == &b || m.state >= 3 || m.type == 3) continue;
    float d = hypotf(m.x - b.x, m.y - b.y);
    if (d >= 2.2f) continue;
    m.hp -= (int)(70 * (1 - d / 2.2f)) + 1;
    m.flash = 4;
    if (m.type != 4 && m.state == 0) m.state = 1;
    if (m.hp <= 0) zKill(m);
  }
  float d = hypotf(zPX - b.x, zPY - b.y);
  if (d < 2.2f) zHurtMe((int)(40 * (1 - d / 2.2f)));
}
void zKill(ZMob &m) {
  if (m.state >= 3) return;
  if (m.type == 4) { zBoom(m); return; }
  m.state = 3; m.t = 0;
  gScore += m.type == 0 ? 100 : m.type == 1 ? 200 : 600;
  zHitKill = 10;
  for (int i = 0; i < 3; i++)  // debris qui volent
    for (auto &k : zSpark)
      if (!k.on) { k = { m.x + (random(21) - 10) * 0.03f, m.y + (random(21) - 10) * 0.03f, 0, 0, (uint16_t)random(3), true }; break; }
  zCombo = zKillT > 0 ? zCombo + 1 : 1;  // combo : robots detruits a moins de 3 s d'intervalle
  zKillT = 120;
  if (zCombo >= 2) {
    gScore += 150UL * zCombo;
    zMsgSet(zCombo == 2 ? "DOUBLE ! +300" : zCombo == 3 ? "TRIPLE ! +450" : "CARNAGE !");
  }
  if (!gDemo && gScore > gHi) gHi = gScore;
  sfxHit();
}
// ton tir (instantane) : l'ennemi le plus proche sous le viseur, sans mur devant
void zShoot() {
  float dirX = cosf(zA), dirY = sinf(zA);
  int rays = zWeapon ? 3 : 1, dmg = zWeapon ? 12 : 10;
  for (int r = 0; r < rays; r++) {
    float off = rays == 1 ? 0 : (r - 1) * 0.07f;
    float ca = cosf(zA + off), sa = sinf(zA + off);
    ZMob *best = nullptr;
    float bestD = 99;
    for (auto &m : zMob) {
      if (!m.on || m.type == 3 || m.state >= 3) continue;
      float dx = m.x - zPX, dy = m.y - zPY, along = dx * ca + dy * sa, side = -dx * sa + dy * ca;
      float half = m.type == 2 ? 0.45f : m.type == 4 ? 0.28f : 0.32f;
      if (along > 0.1f && fabsf(side) < half && along < bestD && zSight(zPX, zPY, m.x, m.y)) { best = &m; bestD = along; }
    }
    if (!best) {  // impact dans le mur : etincelles
      float dw = zZbuf[W / 2] - 0.08f;
      for (auto &k : zSpark)
        if (!k.on) { k = { zPX + ca * dw, zPY + sa * dw, 0, 0, 0, true }; break; }
      if (r == 0)  // tir manque de peu : ceux qui sont vises font un pas de cote
        for (auto &m : zMob) {
          if (!m.on || m.type >= 2 || m.state != 1 || m.dodgeT) continue;
          float dx = m.x - zPX, dy = m.y - zPY, along = dx * ca + dy * sa, side = -dx * sa + dy * ca;
          if (along > 0 && fabsf(side) < 1.2f && random(100) < 45) { m.dodgeT = 12 + random(12); m.dx = side > 0 ? 1 : -1; }
        }
      continue;
    }
    zHitT = 6;
    if (best->type == 2 && bestD > 4.5f && best->state < 3) {  // colosse en garde : la moitie des degats, ca ricoche
      dmg /= 2;
      best->guard = 5;
      for (auto &k : zSpark)
        if (!k.on) { k = { best->x - ca * 0.4f, best->y - sa * 0.4f, 0, 0, 2, true }; break; }
    }
    best->hp -= dmg;
    best->flash = best->guard ? 0 : 4;
    if (best->type < 2 && zFree(best->x + ca * 0.12f, best->y + sa * 0.12f, 0.2f)) { best->x += ca * 0.12f; best->y += sa * 0.12f; }  // recul sous l'impact
    if (best->type != 4) {
      best->state = max<uint8_t>(best->state, 1);
      if (best->type != 2) { best->dodgeT = 14 + random(16); best->dx = random(2) ? 1 : -1; }
      best->cool = min<uint8_t>(best->cool, 12);  // riposte
      for (auto &o : zMob)  // il previent ses voisins
        if (o.on && o.type < 3 && o.state == 0 && hypotf(o.x - best->x, o.y - best->y) < 6) o.state = 1;
    }
    if (best->hp <= 0) zKill(*best);
  }
  (void)dirX; (void)dirY;
}
// demo : chemin le plus court (portes comprises) vers la carte, puis vers la sortie ; vise et tire les ennemis vus
uint8_t zBfsDir(int tx, int ty, int &nx, int &ny) {
  auto &from = zD->bfsFrom;
  int16_t *q = zD->bfsQ;
  memset(from, 255, sizeof(from));
  int sx = zPX, sy = zPY, h = 0, t = 0;
  from[sy][sx] = 4;
  q[t++] = sy * Z_N + sx;
  static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
  while (h < t) {
    int c = q[h++], cx = c % Z_N, cy = c / Z_N;
    if (cx == tx && cy == ty) break;
    for (int d = 0; d < 4; d++) {
      int x = cx + DX[d], y = cy + DY[d];
      if (x < 0 || y < 0 || x >= Z_N || y >= Z_N || from[y][x] != 255) continue;
      uint8_t v = zMap[y][x];
      if (v && v != 5 && !(v == 6 && zKey) && !(x == tx && y == ty)) continue;
      from[y][x] = d;
      q[t++] = y * Z_N + x;
    }
  }
  if (from[ty][tx] == 255) return 0;
  int x = tx, y = ty;
  while (true) {  // on remonte jusqu'a la case voisine de la tienne
    int d = from[y][x];
    if (d == 4) return 0;
    int px = x - DX[d], py = y - DY[d];
    if (px == sx && py == sy) { nx = x; ny = y; return 1; }
    x = px; y = py;
  }
}
void zAuto(float &mv, float &st, float &tn, bool &fire, bool &use) {
  mv = st = tn = 0; fire = use = false;
  ZMob *tgt = nullptr;
  float bd = 7;
  for (auto &m : zMob)
    if (m.on && m.type < 3 && m.state < 3) {
      float d = hypotf(m.x - zPX, m.y - zPY);
      if (d < bd && zSight(zPX, zPY, m.x, m.y)) { bd = d; tgt = &m; }
    }
  float want;
  if (tgt) {
    want = atan2f(tgt->y - zPY, tgt->x - zPX);
    float da = remainderf(want - zA, 2 * PI);
    tn = constrain(da * 2, -1.0f, 1.0f);
    fire = fabsf(da) < 0.12f && zAmmo > 0;
    if (bd > 3) mv = 0.4f;
    return;
  }
  int tx = zExitX, ty = zExitY;
  if (!zKey)
    for (auto &it : zItem) if (it.on && it.type == 3) { tx = it.x; ty = it.y; }
  int nx, ny;
  if (!zBfsDir(tx, ty, nx, ny)) { tn = 0.5f; mv = 0.3f; return; }
  if (nx == zExitX && ny == zExitY) { want = atan2f(ny + 0.5f - zPY, nx + 0.5f - zPX); use = true; }
  else want = atan2f(ny + 0.5f - zPY, nx + 0.5f - zPX);
  float da = remainderf(want - zA, 2 * PI);
  tn = constrain(da * 2.5f, -1.0f, 1.0f);
  mv = fabsf(da) < 0.6f ? 1 : 0.2f;
  if ((zMap[ny][nx] == 5 || zMap[ny][nx] == 6) && zOpen[ny][nx] < 14) use = true;
}
void zUse() {  // carre : ouvre la porte en face, ou actionne la sortie
  float cx = zPX + cosf(zA) * 0.9f, cy = zPY + sinf(zA) * 0.9f;
  int x = cx, y = cy;
  if (x < 0 || y < 0 || x >= Z_N || y >= Z_N) return;
  uint8_t v = zMap[y][x];
  if (v == 5 || (v == 6 && zKey)) { if (zOpen[y][x] == 0) sfxDive(); zDoorT[y][x] = 160; }
  else if (v == 6) zMsgSet("CARTE ROUGE REQUISE");
  else if (v == 7) {
    if (!zKey) { zMsgSet("CARTE ROUGE REQUISE"); return; }
    sfxHit();
    gScore += 1000UL * gWave;
    altStageClear(500UL * gWave + zHp * 5);
    zNewStage();
  }
}
void zStep() {
  if (!zD) return;
  gFrame++;
  if (zFlash > 0) zFlash--;
  if (zHurt > 0) zHurt--;
  if (zPick > 0) zPick--;
  if (zMsgT > 0) zMsgT--;
  if (zFireT > 0) zFireT--;
  if (zShake > 0) zShake--;
  if (zHitT > 0) zHitT--;
  if (zHitKill > 0) zHitKill--;
  if (zKillT > 0) zKillT--;
  if (gDead > 0) {
    if (--gDead == 0) {
      if (gLives <= 0) {
        if (gDemo) zReset();
        else if (gLastRank) startNameEntry();
        else toDemo();
        return;
      }
      zHp = 100; zHurt = 0;  // on repart du debut du bunker
      zPX = zRoom[0][0] + zRoom[0][2] / 2 + 0.5f; zPY = zRoom[0][1] + zRoom[0][3] / 2 + 0.5f;
      for (auto &s : zShot) s.on = false;
    }
    return;
  }
  // commandes
  float mv = 0, st = 0, tn = 0;
  bool fire, use, sw = false, mapK = false;
  if (gDemo) zAuto(mv, st, tn, fire, use);
  else {
    int ly = PS4.LStickY(), lx = PS4.LStickX(), rx = PS4.RStickX();
    if (abs(ly) > 20) mv = ly / 127.0f;
    if (abs(lx) > 20) st = lx / 127.0f;
    if (abs(rx) > 20) tn = rx / 127.0f;
    if (PS4.Up() || PS4.UpLeft() || PS4.UpRight()) mv = 1;
    if (PS4.Down() || PS4.DownLeft() || PS4.DownRight()) mv = -1;
    if (PS4.Left() || PS4.UpLeft() || PS4.DownLeft()) tn = -1;
    if (PS4.Right() || PS4.UpRight() || PS4.DownRight()) tn = 1;
    if (PS4.L1()) st = -1;
    if (PS4.R1()) st = 1;
    fire = PS4.Cross() || PS4.R2Value() > 60;
    use = PS4.Square();
    sw = PS4.Circle();
    mapK = PS4.Triangle();
  }
  zA += tn * 0.065f;
  float dx = cosf(zA), dy = sinf(zA), sp = 0.075f;
  float vx = (dx * mv - dy * st) * sp, vy = (dy * mv + dx * st) * sp;
  float ox = zPX, oy = zPY;
  if (zFree(zPX + vx, zPY, 0.22f)) zPX += vx;
  if (zFree(zPX, zPY + vy, 0.22f)) zPY += vy;
  zVX = zPX - ox; zVY = zPY - oy;
  if (fabsf(mv) + fabsf(st) > 0.1f) zBob += 0.28f;
  if (use && !zPrevUse) zUse();
  zPrevUse = use;
  if (sw && !zPrevSw && zHeavy) { zWeapon ^= 1; sfxMenu(); zHudKey = -1; }
  zPrevSw = sw;
  if (mapK && !zPrevMap) zMapOn = !zMapOn;
  zPrevMap = mapK;
  if (fire && zFireT == 0) {
    int cost = zWeapon ? 3 : 1;
    if (zAmmo >= cost) {
      zAmmo -= cost;
      zShoot();
      zFlash = 4;
      zFireT = zWeapon ? 16 : 8;
      sfxShot();
    } else if (zFireT == 0) { zMsgSet("PLUS DE MUNITIONS"); zFireT = 20; }
  }
  // portes : s'ouvrent, restent ouvertes, se referment (jamais sur quelqu'un)
  for (int y = 0; y < Z_N; y++)
    for (int x = 0; x < Z_N; x++) {
      if (zMap[y][x] != 5 && zMap[y][x] != 6) continue;
      if (zDoorT[y][x]) {
        if (zOpen[y][x] < 16) zOpen[y][x]++;
        else {
          bool occ = (int)zPX == x && (int)zPY == y;
          for (auto &m : zMob) occ |= m.on && (int)m.x == x && (int)m.y == y;
          if (!occ) zDoorT[y][x]--;
        }
      } else if (zOpen[y][x] > 0) zOpen[y][x]--;
    }
  // objets
  for (auto &it : zItem) {
    if (!it.on || fabsf(it.x + 0.5f - zPX) > 0.5f || fabsf(it.y + 0.5f - zPY) > 0.5f) continue;
    if (it.type == 0 && zHp >= 100) continue;
    if (it.type == 2 && zArmor >= 100) continue;
    it.on = false;
    zPick = 8;
    gScore += 50;
    sndMelody(JINGLE_ARME, 4);
    if (it.type == 0) { zHp = min(100, zHp + 30); zMsgSet("SOIN +30"); }
    else if (it.type == 1) { zAmmo = min(200, zAmmo + 25); zMsgSet("MUNITIONS +25"); }
    else if (it.type == 2) { zArmor = min(100, zArmor + 50); zMsgSet("ARMURE +50"); }
    else if (it.type == 3) { zKey = true; zMsgSet("CARTE ROUGE"); }
    else { zHeavy = true; zWeapon = 1; zAmmo = min(200, zAmmo + 20); zMsgSet("ARME LOURDE (ROND)"); }
  }
  // ennemis
  float diff = 1 + 0.2f * (gEffDiff() - 1);
  for (auto &m : zMob) {
    if (!m.on || m.type == 3) continue;
    m.t++;
    if (m.flash) m.flash--;
    if (m.type == 4) { if (m.state == 3 && m.t > 16) m.on = false; continue; }
    if (m.state == 3) { if (m.t > 14) m.state = 4; continue; }  // explosion puis debris
    if (m.state == 4) continue;
    float ex = zPX - m.x, ey = zPY - m.y, d = sqrtf(ex * ex + ey * ey) + 0.001f;
    bool see = (m.t % 6 == 0) ? zSight(m.x, m.y, zPX, zPY) : (m.state == 1);
    if (m.state == 0) {
      if (see && d < 9) { m.state = 1; sfxDive(); if (m.cool < 20) m.cool = 20; }  // repere : un court temps de reaction
      else {  // ronde : il patrouille dans sa salle, sans passer les portes
        if (m.t % 80 == 1) m.hd = random(628) / 100.0f;
        float nx = m.x + cosf(m.hd) * 0.012f, ny = m.y + sinf(m.hd) * 0.012f;
        int cx = nx, cy = ny;
        if (cx >= 0 && cy >= 0 && cx < Z_N && cy < Z_N && zMap[cy][cx] == 0 && zFree(nx, ny, 0.25f)) { m.x = nx; m.y = ny; m.walk++; }
        else m.hd += 1.7f;
        continue;
      }
    }
    if (m.state != 1) continue;
    if (m.atk) m.atk--;
    if (m.guard) m.guard--;
    int maxhp = m.type == 0 ? 20 : m.type == 1 ? 35 : 90;
    // tu le vises : il fait un pas de cote (le colosse, lui, fonce)
    float aim = fabsf(remainderf(atan2f(-ey, -ex) - zA + PI, 2 * PI));
    if (!m.dodgeT && m.type != 2 && see && aim < 0.2f && random(100) < (m.type == 0 ? 9 : 6)) { m.dodgeT = 16 + random(20); m.dx = random(2) ? 1 : -1; }
    float want = m.type == 0 ? 3.2f : m.type == 1 ? 4.0f : 0.7f, spd = (m.type == 0 ? 0.035f : m.type == 1 ? 0.027f : 0.03f) * diff;
    float ux = ex / d, uy = ey / d, mvx = 0, mvy = 0;
    bool flee = m.type != 2 && m.hp < maxhp * 0.35f && d < 6;
    if (flee) { mvx = -ux; mvy = -uy; }  // blesse : il recule en tirant
    else if (d > want) { mvx = ux; mvy = uy; }
    else if (m.type != 2 && d < want * 0.6f) { mvx = -ux * 0.7f; mvy = -uy * 0.7f; }  // trop pres : il garde ses distances
    // au combat, jamais immobile : il tourne autour de toi et change de sens au hasard
    if (m.strafeT) m.strafeT--;
    else { m.strafeT = 25 + random(55); if (random(3)) m.dx = random(2) ? 1 : -1; }
    if (m.dx == 0) m.dx = 1;
    if (m.type != 2) {
      float s = m.type == 0 ? 0.75f + sinf(m.t * 0.13f) * 0.55f : 0.6f;  // le drone louvoie
      mvx += -uy * m.dx * s; mvy += ux * m.dx * s;
    }
    if (m.dodgeT) { m.dodgeT--; mvx += -uy * m.dx * 1.6f; mvy += ux * m.dx * 1.6f; }  // pas de cote
    if (m.type == 2 && d < 4.5f) { spd *= 1.9f; float z = sinf(m.t * 0.25f) * 0.7f; mvx += -uy * z; mvy += ux * z; }  // charge en zigzag
    for (auto &o : zMob) {  // ils s'ecartent les uns des autres
      if (&o == &m || !o.on || o.type >= 3 || o.state >= 3) continue;
      float sx = m.x - o.x, sy = m.y - o.y, s2 = sx * sx + sy * sy;
      if (s2 < 0.5f && s2 > 1e-4f) { float k = (0.5f - s2) * 3 / sqrtf(s2); mvx += sx * k; mvy += sy * k; }
    }
    if (mvx != 0 || mvy != 0) {
      float nx = m.x + mvx * spd, ny = m.y + mvy * spd;
      int cx = nx, cy = ny;
      if (cx >= 0 && cy >= 0 && cx < Z_N && cy < Z_N && (zMap[cy][cx] == 5 || (zMap[cy][cx] == 6 && zKey)) && zOpen[cy][cx] < 14) zDoorT[cy][cx] = 160;  // ils ouvrent les portes
      bool okx = zFree(nx, m.y, 0.2f), oky = zFree(m.x, ny, 0.2f);
      if (okx) m.x = nx;
      if (oky) m.y = ny;
      if (!okx && !oky) { m.dx = -m.dx; m.strafeT = 20; }  // bloque : il repart de l'autre cote
      else if ((!okx || !oky) && m.strafeT > 20) { m.dx = -m.dx; m.strafeT = 20; }
      m.walk++;
    }
    if (m.cool > 0) m.cool--;
    else if (m.type == 2) {
      if (d < 1.1f) { zHurtMe(15); m.cool = 45; m.atk = 8; }
    } else if (see && d < 10 && gStageT == 0) {
      if (m.type == 1) { m.burst = 3; m.cool = 75 + random(35); }  // sentinelle : rafale de 3
      else {  // drone : il vise la ou tu vas etre
        float sp2 = 0.09f * diff, t = d / sp2 * 0.7f;
        zFireShot(m.x, m.y, zPX + zVX * t, zPY + zVY * t, sp2);
        m.atk = 5;
        m.cool = 70 + random(35);
      }
    }
    if (m.burst && m.t % 6 == 0) {
      float a = atan2f(ey, ex) + (random(3) - 1) * 0.06f;
      zFireShot(m.x, m.y, m.x + cosf(a), m.y + sinf(a), 0.12f * diff);
      m.atk = 4;
      if (--m.burst == 0) { m.dodgeT = 14 + random(14); m.dx = random(2) ? 1 : -1; }  // apres la rafale, il se decale
    }
  }
  for (auto &k : zSpark)
    if (k.on && ++k.t > 7) k.on = false;
  for (auto &s : zShot) {  // plasma ennemi
    if (!s.on) continue;
    s.x += s.vx; s.y += s.vy; s.t++;
    if (zWall((int)s.x, (int)s.y) || s.t > 200) { s.on = false; continue; }
    if (fabsf(s.x - zPX) < 0.3f && fabsf(s.y - zPY) < 0.3f) {
      s.on = false;
      float rel = remainderf(atan2f(-s.vy, -s.vx) - zA, 2 * PI);  // d'ou vient le tir
      zHurtMe(8 + gWave, rel < -0.5f ? -1 : rel > 0.5f ? 1 : 0);
    }
  }
  if (gStageT > 0) gStageT--;
}
// ---- dessin : chaque colonne est calculee entierement (murs, sol, plafond, objets, arme), puis envoyee : aucun effacement
void zHudDraw() {
  int hh = H - zVH(), key = zHp * 7 + zArmor * 1009 + zAmmo * 104729 + zKey * 3 + zWeapon * 5 + gLives * 11 + (int)gScore * 13 + zMsgT * 17 + gWave * 19;
  if (key == zHudKey) return;
  zHudKey = key;
  GFXcanvas16 &c = *zHudCv;
  for (int yy = 0; yy < hh; yy++) c.drawFastHLine(0, yy, W, rgb(50 - yy * 2, 50 - yy * 2, 62 - yy * 2));  // degrade metal
  c.drawFastHLine(0, 0, W, rgb(140, 140, 155));
  c.drawFastHLine(0, 1, W, rgb(16, 16, 22));
  for (int sx : { 20, 41, 62 }) { c.drawFastVLine(sx, 2, hh - 2, rgb(16, 16, 22)); c.drawFastVLine(sx + 1, 2, hh - 2, rgb(70, 70, 84)); }
  int y = hh >= 12 ? 3 : 2;
  if (hh >= 12) {  // jauges : sante (vert -> rouge), armure, munitions
    auto bar = [&](int x, int v, int vmax, uint16_t col) {
      c.drawFastHLine(x, 9, 18, rgb(16, 16, 22));
      c.drawFastHLine(x, 10, 18, rgb(16, 16, 22));
      int w = constrain(v * 18 / vmax, 0, 18);
      if (w) { c.drawFastHLine(x, 9, w, col); c.drawFastHLine(x, 10, w, (col >> 1) & 0x7BEF); }
    };
    bar(1, zHp, 100, zHp > 60 ? rgb(60, 220, 70) : zHp > 30 ? rgb(255, 200, 0) : rgb(255, 40, 30));
    bar(22, zArmor, 100, rgb(60, 140, 255));
    bar(43, zAmmo, 200, rgb(255, 200, 0));
  }
  char b[16];
  auto icon = [&](int x, const char *rows[5], uint16_t col) {
    for (int r = 0; r < 5; r++)
      for (int q = 0; q < 5; q++)
        if (rows[r][q] != '.') c.drawPixel(x + q, y + r, col);
  };
  static const char *HEART[5] = { ".r.r.", "rrrrr", "rrrrr", ".rrr.", "..r.." };
  static const char *SHIELD[5] = { "bbbbb", "bbbbb", "bbbbb", ".bbb.", "..b.." };
  static const char *AMMO[5] = { ".y.y.", "yyyyy", "yyyyy", "yyyyy", "yyyyy" };
  icon(1, HEART, rgb(255, 50, 40));
  snprintf(b, sizeof(b), "%d", zHp);
  tinyText(c, 7, y, b, zHp > 30 || (gFrame & 8) ? rgb(255, 90, 80) : rgb(255, 255, 255));
  icon(22, SHIELD, rgb(60, 140, 255));
  snprintf(b, sizeof(b), "%d", zArmor);
  tinyText(c, 28, y, b, rgb(120, 180, 255));
  icon(43, AMMO, zWeapon ? rgb(255, 140, 0) : rgb(255, 210, 0));
  snprintf(b, sizeof(b), "%d", zAmmo);
  tinyText(c, 49, y, b, rgb(255, 220, 80));
  if (zKey) { c.fillRect(65, y, 6, 4, rgb(230, 30, 30)); c.drawFastHLine(66, y + 1, 2, rgb(255, 255, 255)); }
  for (int i = 0; i < min(gLives, 4); i++) c.fillRect(74 + i * 4, y + 1, 3, 3, rgb(60, 200, 70));  // vies
  snprintf(b, sizeof(b), "%lu", (unsigned long)gScore);
  tinyText(c, W - 1 - tinyWidth(b), y, b, rgb(230, 230, 230));
}
void zDraw() {
  if (!zD || !zHudCv) return;
  if (gPaused) return;  // image figee sous le cadre de pause
  zHudDraw();
  int VH = zVH(), hh = H - VH;
  float plane = min(1.0f, W / (2.0f * VH * 1.35f)), K = W / (2 * plane);
  float shA = zShake ? (random(-zShake, zShake + 1)) * 0.0035f : 0;  // tremblement (explosion, coup recu)
  float dirX = cosf(zA + shA), dirY = sinf(zA + shA), plX = -dirY * plane, plY = dirX * plane;
  int horizon = VH / 2 + (int)(sinf(zBob) * 1.2f) + (zShake ? (int)random(-(zShake + 2) / 3, (zShake + 2) / 3 + 1) : 0);
  float bobX = sinf(zBob * 0.5f) * 2.0f, bobY = fabsf(cosf(zBob * 0.5f)) * 2.0f;
  // sprites visibles, du plus loin au plus proche
  typedef ZSpr Spr;
  Spr *spr = zD->spr;
  int ns = 0;
  float inv = 1.0f / (plX * dirY - dirX * plY);
  auto addSpr = [&](float x, float y, float hgt, float lift, const char *const *art, uint8_t aw, uint8_t ah, uint8_t kind, int8_t fl) {
    float sx = x - zPX, sy = y - zPY, tx = inv * (dirY * sx - dirX * sy), ty = inv * (-plY * sx + plX * sy);
    if (ty < 0.15f || ns >= Z_SPR) return;
    Spr &s = spr[ns++];
    s.tx = tx; s.ty = ty;
    s.sx = (int)(W / 2 * (1 + tx / ty));
    s.h = max(1, (int)(K * hgt / ty));
    s.w = max(1, s.h * aw / ah);
    s.top = horizon + (int)(K * 0.5f / ty) - s.h - (int)(K * lift / ty);
    s.art = art; s.aw = aw; s.ah = ah; s.kind = kind; s.flash = fl;
    s.wx = x; s.wy = y; s.lr = s.lg = s.lb = 0;
    s.fy = horizon + (int)(K * 0.5f / ty);  // niveau du sol sous l'objet (ombre)
  };
  for (auto &m : zMob) {
    if (!m.on) continue;
    if (m.type == 3) addSpr(m.x, m.y, 1.0f, 0, ZS_COLUMN, 16, 16, 0, 0);
    else if (m.type == 4 && m.state == 3) addSpr(m.x, m.y, 0.4f + m.t * 0.07f, 0.1f, nullptr, 1, 1, 2, m.t);  // grosse explosion
    else if (m.type == 4) addSpr(m.x, m.y, 0.55f, 0, ZS_BARREL, 16, 16, 0, m.flash);
    else if (m.state == 4) addSpr(m.x, m.y, 0.6f, 0, ZS_SCRAP, 16, 16, 0, 0);
    else if (m.state == 3) addSpr(m.x, m.y, 0.25f + m.t * 0.04f, 0.25f, nullptr, 1, 1, 2, m.t);  // explosion
    else if (m.type == 0) addSpr(m.x, m.y, 0.5f, 0.35f + sinf(m.t * 0.1f) * 0.06f, ZS_DRONE[(m.t >> 2) & 1], 24, 24, m.atk ? 3 : 0, m.flash);
    else addSpr(m.x, m.y, m.type == 2 ? 0.95f : 0.8f, 0, (m.type == 1 ? ZS_SENT : ZS_BRUTE)[(m.walk >> 3) & 1], 24, 24, m.atk ? 3 : 0, m.guard ? -1 : m.flash);
  }
  for (auto &it : zItem)
    if (it.on) addSpr(it.x + 0.5f, it.y + 0.5f, 0.28f, 0, ZS_ITEM[it.type], 8, 8, 0, 0);
  for (auto &s : zShot)
    if (s.on) addSpr(s.x, s.y, 0.18f, 0.35f, nullptr, 1, 1, 1, 0);  // boule de plasma
  for (auto &k : zSpark)
    if (k.on) addSpr(k.x, k.y, 0.08f + k.t * 0.02f, 0.45f, nullptr, 1, 1, 4, k.t);  // etincelles
  for (auto &m : zMob)  // fumee qui monte des epaves (en dernier : elle ne prend jamais la place d'un tir ou d'un objet)
    if (m.on && m.type < 3 && m.state == 4 && m.t < 100) addSpr(m.x, m.y + 0.01f, 0.3f + (m.t - 15) * 0.006f, 0.25f + (m.t - 15) * 0.012f, nullptr, 1, 1, 6, m.t);
  for (int i = 1; i < ns; i++)  // tri du plus loin au plus proche
    for (int j = i; j > 0 && spr[j].ty > spr[j - 1].ty; j--) { Spr t = spr[j]; spr[j] = spr[j - 1]; spr[j - 1] = t; }
  struct Lt { float x, y, r, g, b, rad; };
  Lt lt[8];
  int nl = 0;
  for (auto &s : zShot)
    if (s.on && nl < 8) lt[nl++] = { s.x, s.y, 20, 80, 30, 1.6f };
  for (auto &m : zMob)
    if (m.on && m.state == 3 && nl < 8) lt[nl++] = { m.x, m.y, 150 - m.t * 9.0f, 80 - m.t * 5.0f, 15, m.type == 4 ? 3.2f : 2.4f };
  for (auto &k : zSpark)
    if (k.on && nl < 8) lt[nl++] = { k.x, k.y, 90, 70, 20, 1.0f };
  if (zFlash > 1 && nl < 8) lt[nl++] = { zPX + dirX * 0.5f, zPY + dirY * 0.5f, 55, 40, 15, 2.5f };
  auto lightAt = [&](float x, float y, float &r, float &g, float &b) {
    for (int i = 0; i < nl; i++) {
      float dx = x - lt[i].x, dy = y - lt[i].y, d2 = dx * dx + dy * dy;
      if (d2 >= lt[i].rad * lt[i].rad) continue;
      float k = 1 - sqrtf(d2) / lt[i].rad;
      r += lt[i].r * k; g += lt[i].g * k; b += lt[i].b * k;
    }
  };
  if (nl)  // lumiere recue par chaque objet (plasma, explosions, tir)
    for (int i = 0; i < ns; i++)
      if (spr[i].art) lightAt(spr[i].wx, spr[i].wy, spr[i].lr, spr[i].lg, spr[i].lb);
  float *vigY = zD->vigY;  // vignettage : les bords de l'image un peu plus sombres
  for (int y = 0; y < VH; y++) { float v = (y - VH / 2.0f) / (VH / 2.0f); vigY[y] = v * v * 0.28f; }
  float *rowD = zD->rowD;
  for (int y = 0; y < VH; y++) rowD[y] = y == horizon ? 99 : (K * 0.5f) / fabsf(y - horizon + 0.5f);
  float light = 1 + (zFlash > 2 ? 0.25f : 0);
  int gs = VH >= 100 ? 2 : 1, gw = 28 * gs, ghh = 16 * gs, gx = W / 2 - gw / 2 + (int)bobX, gy = VH - ghh + 2 + (int)bobY + (zFlash > 1 ? (zFlash - 1) * gs : 0);  // recul au tir
  int mapS = VH >= 100 ? 3 : 2, mapX0 = W - Z_N * mapS - 2, mapY0 = 1;
  uint16_t col[256];
  for (int x = 0; x < W; x++) {
    float cam = 2.0f * x / W - 1, rdx = dirX + plX * cam, rdy = dirY + plY * cam;
    int mx = zPX, my = zPY, stx = rdx < 0 ? -1 : 1, sty = rdy < 0 ? -1 : 1, side = 0, hit = 0;
    float ddx = fabsf(1 / (rdx + 1e-6f)), ddy = fabsf(1 / (rdy + 1e-6f));
    float sdx = (rdx < 0 ? zPX - mx : mx + 1 - zPX) * ddx, sdy = (rdy < 0 ? zPY - my : my + 1 - zPY) * ddy;
    float dist = 30, wx = 0;
    for (int it = 0; it < 64 && !hit; it++) {
      if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; } else { sdy += ddy; my += sty; side = 1; }
      if (mx < 0 || my < 0 || mx >= Z_N || my >= Z_N) { hit = 1; dist = 30; break; }
      uint8_t v = zMap[my][mx];
      if (!v) continue;
      if (v == 5 || v == 6) {  // porte au milieu de la case, qui coulisse
        float d = side == 0 ? (mx + 0.5f - zPX) / rdx : (my + 0.5f - zPY) / rdy;
        float hx = side == 0 ? zPY + d * rdy : zPX + d * rdx;
        if ((int)floorf(hx) != (side == 0 ? my : mx)) continue;
        float fr = hx - floorf(hx), op = zOpen[my][mx] / 16.0f;
        if (fr < op) continue;
        dist = d; wx = fr - op; hit = v;
      } else {
        dist = side == 0 ? sdx - ddx : sdy - ddy;
        float hx = side == 0 ? zPY + dist * rdy : zPX + dist * rdx;
        wx = hx - floorf(hx); hit = v;
      }
    }
    dist = max(dist, 0.05f);
    zZbuf[x] = dist;
    int lh = (int)(K / dist), top = horizon - lh / 2, bot = top + lh;
    int tex = min(hit, Z_TEXS - 1);
    float hX = zPX + rdx * dist, hY = zPY + rdy * dist, lr = 0, lg = 0, lb = 0;
    if (nl) lightAt(hX, hY, lr, lg, lb);  // lumiere recue par ce mur
    float vx = (x - W / 2.0f) / (W / 2.0f), vigX = vx * vx * 0.28f;
    float hurtK = 0;  // voile rouge : sur tout l'ecran, ou du cote d'ou vient le tir
    if (zHurt) {
      float side = zHurtDir < 0 ? -vx : zHurtDir > 0 ? vx : 0;
      hurtK = constrain(zHurt * (zHurtDir ? 0.02f + max(0.0f, side - 0.2f) * 0.09f : 0.05f), 0.0f, 0.85f);
    }
    uint32_t wid = mx * 131 + my * 71 + side * 7;
    float fog = light / (1 + dist * dist * 0.035f), shade = (side ? 0.72f : 1.0f) * fog;
    for (int y = 0; y < VH; y++) {
      float r, g, b;
      if (y >= top && y < bot) {  // mur texture
        float tv = (y - top + 0.5f) / lh, ao = min(tv, 1 - tv);
        zTexel(tex, wx, tv, wid, r, g, b);
        float s = shade * (ao < 0.12f ? 0.55f + ao * 3.75f : 1.0f);  // coins plus sombres en haut et en bas
        if (r > 200 && g > 200) s = max(s, 0.85f);  // lumieres : visibles meme de loin
        r = (r + lr) * s; g = (g + lg) * s; b = (b + lb) * s;
      } else {  // sol (dalles) et plafond (dalles avec plafonniers)
        float rd = rowD[y], fx = zPX + rdx * rd, fy = zPY + rdy * rd, fg = light / (1 + rd * rd * 0.035f);
        int cx = (int)floorf(fx), cy = (int)floorf(fy);
        float ux = fx - cx, uy = fy - cy;
        bool edge = ux < 0.06f || uy < 0.06f;
        int n = zHash(cx * 73 + cy * 151 + (y > horizon) * 7) & 15;
        if (y > horizon) {
          float sx = fx * 2 - floorf(fx * 2), sy = fy * 2 - floorf(fy * 2);
          int tn = zHash((int)floorf(fx * 2) * 57 + (int)floorf(fy * 2) * 113) & 15;
          float v = (sx < 0.05f || sy < 0.05f) ? 40 : (sx < 0.12f || sy < 0.12f) ? 105 + tn : 78 + tn + n / 2;
          float wall = 1;
          if (cx >= 0 && cy >= 0 && cx < Z_N && cy < Z_N) {  // ombre au pied des murs (jamais hors de la carte)
            if (ux < 0.25f && zMap[cy][max(0, cx - 1)]) wall = min(wall, 0.55f + ux * 1.8f);
            if (ux > 0.75f && zMap[cy][min(Z_N - 1, cx + 1)]) wall = min(wall, 0.55f + (1 - ux) * 1.8f);
            if (uy < 0.25f && zMap[max(0, cy - 1)][cx]) wall = min(wall, 0.55f + uy * 1.8f);
            if (uy > 0.75f && zMap[min(Z_N - 1, cy + 1)][cx]) wall = min(wall, 0.55f + (1 - uy) * 1.8f);
          }
          r = v * wall; g = v * wall; b = (v + 7) * wall;
        } else {
          bool lamp = (cx % 3 == 1) && (cy % 3 == 1) && ux > 0.3f && ux < 0.7f && uy > 0.3f && uy < 0.7f;
          float lx = ux - 0.5f, ly = uy - 0.5f, ld = lx * lx + ly * ly;
          if (lamp) { r = 255; g = 245; b = 210; fg = max(fg, 0.75f); }
          else {
            float v = edge ? 25 : 48 + n / 2;
            r = v; g = v; b = v + 4;
            if ((cx % 3 == 1) && (cy % 3 == 1) && ld < 0.25f) { float k = (0.25f - ld) * 4; r += 70 * k; g += 65 * k; b += 45 * k; }  // halo du plafonnier
          }
        }
        if (nl && rd < 8) { float a = 0, c = 0, e = 0; lightAt(fx, fy, a, c, e); r += a * 0.6f; g += c * 0.6f; b += e * 0.6f; }
        r *= fg; g *= fg; b *= fg;
      }
      { float v = 1 - vigX - vigY[y]; r *= v; g *= v; b *= v; }
      if (zHurt) { r += (255 - r) * hurtK; g *= 1 - hurtK; b *= 1 - hurtK; }
      if (zPick) { float k = zPick * 0.03f; g += (255 - g) * k; }
      col[y] = rgb(constrain(r, 0.0f, 255.0f), constrain(g, 0.0f, 255.0f), constrain(b, 0.0f, 255.0f));
    }
    for (int i = 0; i < ns; i++) {  // objets et ennemis de cette colonne (devant le mur)
      Spr &s = spr[i];
      if (s.ty >= dist) continue;
      int x0 = s.sx - s.w / 2;
      if (x < x0 || x >= x0 + s.w) continue;
      float fog2 = light / (1 + s.ty * s.ty * 0.035f);
      if (s.art) {  // ombre au sol, sous l'objet
        float hw = s.w * 0.42f + 0.5f, du = (x - s.sx) / hw;
        if (du > -1 && du < 1) {
          int hh2 = max(1, (int)(s.w * 0.09f)), y0 = max(0, s.fy - hh2), y1 = min(VH - 1, s.fy + hh2);
          for (int y = y0; y <= y1; y++) {
            float dv = (float)(y - s.fy) / hh2;
            if (du * du + dv * dv < 1) col[y] = (col[y] >> 1) & 0x7BEF;
          }
        }
      }
      if (s.kind == 6) {  // fumee : nuage trame gris, de plus en plus leger
        float u = (x - s.sx) / (s.w / 2.0f + 0.01f);
        static const uint8_t BAYER[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
        float dens = s.flash < 50 ? 10 : 10 - (s.flash - 50) * 0.16f;  // la fumee se dissipe
        uint16_t gr = (rgb(110, 110, 118) >> 1) & 0x7BEF;
        for (int y = max(0, (int)s.top); y < min(VH, (int)(s.top + s.h)); y++) {
          float v = (y - s.top - s.h / 2.0f) / (s.h / 2.0f + 0.01f);
          float d2 = u * u + v * v;
          if (d2 > 1 || BAYER[(y + (s.flash >> 3)) & 3][x & 3] >= (int)(dens * (1 - d2 * 0.75f))) continue;  // plus dense au centre
          col[y] = ((col[y] >> 1) & 0x7BEF) + gr;
        }
        continue;
      }
      if (s.kind == 1 || s.kind == 2 || s.kind == 4) {  // plasma, explosion ou etincelle : disque lumineux
        float u = (x - s.sx) / (s.w / 2.0f + 0.01f);
        for (int y = max(0, (int)s.top); y < min(VH, (int)(s.top + s.h)); y++) {
          float v = (y - s.top - s.h / 2.0f) / (s.h / 2.0f + 0.01f), d2 = u * u + v * v;
          if (d2 > 1) continue;
          if (s.kind == 1) col[y] = d2 < 0.35f ? rgb(255, 255, 255) : rgb(120, 255, 120);
          else if (s.kind == 4) { if (((x + y + s.flash) & 1) || d2 < 0.3f) col[y] = s.flash < 3 ? rgb(255, 255, 220) : rgb(255, 170, 40); }
          else col[y] = s.flash < 5 ? (d2 < 0.5f ? rgb(255, 255, 200) : rgb(255, 180, 40)) : s.flash < 10 ? (d2 < 0.4f ? rgb(255, 200, 0) : rgb(230, 70, 10)) : rgb(90, 80, 80);
        }
        continue;
      }
      int ax = (x - x0) * s.aw / s.w;
      for (int y = max(0, (int)s.top); y < min(VH, (int)(s.top + s.h)); y++) {
        int ay = (y - s.top) * s.ah / s.h;
        const char *row = s.art[ay];
        char ch = row[ax];
        if (ch == '.') continue;
        uint8_t r, g, b;
        zPal(ch, r, g, b);
        if (s.flash > 0) col[y] = rgb(255, 255, 255);
        else if (s.flash < 0) col[y] = (ch == 'v' || ch == 'h') ? rgb(150, 200, 255) : rgb(60, 90, 160);  // garde du colosse : le tir ricoche
        else if (s.kind == 3 && (ch == 'c' || ch == 'R' || ch == 'W')) col[y] = rgb(255, 245, 160);  // canon ou oeil qui flambe au tir
        else if (ch == 'c' || ch == 'R' || ch == 'W' || ch == 'o' || ch == 'y') {  // yeux, voyants, reacteur : brillent dans le noir
          float f = max(fog2, 0.8f);
          col[y] = rgb(r * f, g * f, b * f);
        } else {  // volume : bord gauche et dessus eclaires, bord droit assombri, lumiere du plafond en haut
          float sh = (ax + 1 >= s.aw || row[ax + 1] == '.') ? 0.68f : (ax == 0 || row[ax - 1] == '.') ? 1.3f : (ay == 0 || s.art[ay - 1][ax] == '.') ? 1.25f : 1.0f;
          float f = fog2 * sh * (1.12f - 0.3f * ay / s.ah);
          col[y] = rgb(constrain(r * f + s.lr * fog2 * 0.45f, 0.0f, 255.0f), constrain(g * f + s.lg * fog2 * 0.45f, 0.0f, 255.0f), constrain(b * f + s.lb * fog2 * 0.45f, 0.0f, 255.0f));
        }
      }
    }
    if (x >= gx && x < gx + gw) {  // arme en main (et flash du tir)
      int ax = (x - gx) / gs;
      for (int y = max(0, gy); y < VH; y++) {
        int ay = (y - gy) / gs;
        if (ay >= 16) continue;
        char ch = ZS_GUN[zWeapon][ay][ax];
        if (ch == '.') continue;
        uint8_t r, g, b;
        zPal(ch, r, g, b);
        if (zFlash > 1) col[y] = rgb(min(255, r * 3 / 2 + 30), min(255, g * 4 / 3 + 15), b);  // l'arme eclairee par le tir
        else col[y] = rgb(r, g, b);
      }
      if (zFlash > 1) {
        int fx = gx + gw / 2, fy = gy - 2 * gs, rr = (zFlash + 2) * gs;
        for (int y = max(0, fy - rr); y <= min(VH - 1, fy + rr); y++)
          if ((x - fx) * (x - fx) + (y - fy) * (y - fy) <= rr * rr) col[y] = (x - fx) * (x - fx) + (y - fy) * (y - fy) <= rr * rr / 3 ? rgb(255, 255, 220) : rgb(255, 170, 30);
      }
    }
    if (!zMapOn && horizon >= 4 && horizon < VH - 4) {  // viseur : rouge quand tu touches, il s'ouvre quand tu detruis
      int sp = zHitKill ? 3 : zHitT ? 2 : 1, cxh = W / 2;
      uint16_t vc = zHitKill ? rgb(255, 210, 0) : zHitT ? rgb(255, 40, 40) : rgb(255, 255, 255);
      if (x == cxh) { col[horizon - sp - 1] = vc; col[horizon + sp + 1] = vc; if (!zHitT && !zHitKill) col[horizon] = vc; }
      else if (abs(x - cxh) == sp + 1 || (zHitT && abs(x - cxh) == sp + 2)) col[horizon] = vc;
    }
    if (zMapOn && x >= mapX0 && x < mapX0 + Z_N * mapS) {  // mini-carte
      int cxm = (x - mapX0) / mapS;
      for (int y = mapY0; y < mapY0 + Z_N * mapS && y < VH; y++) {
        int cym = (y - mapY0) / mapS;
        uint8_t v = zMap[cym][cxm];
        uint16_t c = v == 0 ? rgb(10, 14, 20) : v == 5 ? rgb(230, 190, 0) : v == 6 ? rgb(230, 30, 30) : v == 7 ? rgb(40, 230, 70) : rgb(110, 110, 125);
        if (cxm == (int)zPX && cym == (int)zPY) c = rgb(255, 255, 255);
        for (auto &m : zMob)
          if (m.on && m.type < 3 && m.state < 3 && (int)m.x == cxm && (int)m.y == cym) c = rgb(255, 40, 40);
        col[y] = c;
      }
    }
    for (int y = 0; y < VH; y++) gfx->drawPixel(x, y, col[y]);
  }
  if (zMsgT) {  // message (objet ramasse, carte requise) en haut de la vue
    int tw = tinyWidth(zMsg);
    tinyText(*gfx, (W - tw) / 2, 2, zMsg, (zMsgT & 4) ? rgb(255, 255, 255) : rgb(255, 220, 0));
  }
  gfx->drawRGBBitmap(0, VH, zHudCv->getBuffer(), W, hh);  // barre d'etat
  gNOld = 0;  // tout l'ecran est redessine a chaque image
}

// taille du bloc memoire de chaque jeu (0 : rien a reserver)
size_t gMemSize(uint8_t g) {
  switch (g) {
    case G_RAID: return sizeof(RMem);
    case G_BUL: return sizeof(BMem);
    case G_LAB: return sizeof(LMem);
    case G_ORB: return sizeof(OMem);
    case G_BRI: return sizeof(KMem);
    case G_BLO: return sizeof(TMem);
    case G_CRU: return sizeof(YMem);
    case G_TAU: return sizeof(DMem);
    case G_BIL: return sizeof(PMem);
    case G_ESQ: return sizeof(AMem);
    default: return 0;
  }
}
// rend la memoire de l'ancien jeu, reserve (a zero) celle du jeu choisi ; false si elle manque
bool gMemFor(uint8_t g) {
  if (gMemGame == g && (gMem || !gMemSize(g))) return true;
  if (gMem) { free(gMem); gMem = nullptr; }
  gMemGame = -1;
  size_t n = gMemSize(g);
  if (n) {
    gMem = calloc(1, n);
    if (!gMem) { Serial.printf("Jeu %u : memoire insuffisante (%u octets demandes, %u libres)\n", g, (unsigned)n, ESP.getFreeHeap()); return false; }
  }
  gMemGame = g;
  return true;
}
// aiguillage selon le jeu choisi
void altReset() {
  switch (gGame) {
    case G_RAID: rReset(); break;
    case G_RAQ: qReset(); break;
    case G_BUL: bReset(); break;
    case G_LAB: lReset(); break;
    case G_ORB: oReset(); break;
    case G_BRI: kReset(); break;
    case G_BLO: tReset(); break;
    case G_CRU: yReset(); break;
    case G_TAU: dReset(); break;
    case G_BIL: pReset(); break;
    case G_ESQ: aReset(); break;
    default: zReset(); break;
  }
}
void resetCurrent();
void altStep() {
  if (gMemGame != gGame) { resetCurrent(); return; }  // jeu change sans remise a zero : sa memoire d'abord
  switch (gGame) {
    case G_RAID: rStep(); break;
    case G_RAQ: qStep(); break;
    case G_BUL: bStep(); break;
    case G_LAB: lStep(); break;
    case G_ORB: oStep(); break;
    case G_BRI: kStep(); break;
    case G_BLO: tStep(); break;
    case G_CRU: yStep(); break;
    case G_TAU: dStep(); break;
    case G_BIL: pStep(); break;
    case G_ESQ: aStep(); break;
    default: zStep(); break;
  }
}
void altDraw() {
  if (gMemGame != gGame) return;
  switch (gGame) {
    case G_RAID: rDraw(); break;
    case G_RAQ: qDraw(); break;
    case G_BUL: bDraw(); break;
    case G_LAB: lDraw(); break;
    case G_ORB: oDraw(); break;
    case G_BRI: kDraw(); break;
    case G_BLO: tDraw(); break;
    case G_CRU: yDraw(); break;
    case G_TAU: dDraw(); break;
    case G_BIL: pDraw(); break;
    case G_ESQ: aDraw(); break;
    default: zDraw(); break;
  }
}
// remise a zero du jeu choisi (partie ou demo)
void resetCurrent() {
  if (gGame != G_BUN) zRelease();  // la memoire de BUNKER 3D est rendue aux autres jeux et au Bluetooth
  if (!gMemFor(gGame)) { gGame = 0; topLoad(); gMemFor(0); }  // memoire insuffisante : on revient au premier jeu
  if (gGame == 0) gReset();
  else altReset();
  Serial.printf("Jeu %s : memoire libre %u (plus grand bloc %u)\n", GAME_NAME[gGame], ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

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

// Ecran du menu : fond d'etoiles qui defile, titre ARCADE GAMES, record, puis le menu.
// La demo ne tourne plus derriere (sprites, annonces et scores de la demo melanges au menu).
// batteries des manettes en haut du menu : manette 1 a gauche, manette 2 a droite. Pourcentage + petite pile
// (verte, orange, rouge qui clignote) ; en charge : la jauge se remplit en boucle en jaune, avec un eclair.
void drawBat(uint8_t idx) {
  PadShim &p = idx ? PAD2 : PS4;
  if (!p.isConnected()) return;
  int pct = min(100, p.Battery() * 10);
  bool chg = p.Charging(), right = idx;
  uint16_t col = chg ? rgb(255, 220, 0) : pct > 50 ? rgb(40, 230, 80) : pct > 20 ? rgb(255, 160, 0) : rgb(255, 40, 40);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  int tw = tinyWidth(b), bx = right ? W - 10 : 0, by = 0;
  gfx->drawRect(bx, by, 8, 5, rgb(150, 150, 170));  // pile
  gfx->drawFastVLine(bx + 8, by + 1, 3, rgb(150, 150, 170));
  int fill = chg && pct < 100 ? (int)((gFrame / 6) % 7) : (pct * 6 + 50) / 100;
  if (fill > 0 && (chg || pct > 20 || (gFrame & 16))) gfx->fillRect(bx + 1, by + 1, fill, 3, col);
  int tx = right ? bx - 3 - tw : bx + 12;
  tinyText(*gfx, tx, by, b, chg ? rgb(255, 220, 0) : idx ? rgb(255, 150, 150) : rgb(150, 200, 255));  // couleur du joueur
  int x0 = min(tx, bx), x1 = max(tx + tw, bx + 9);
  if (chg) {  // eclair
    int ex = right ? tx - 5 : tx + tw + 1;
    gfx->drawPixel(ex + 2, by, rgb(255, 220, 0)); gfx->drawPixel(ex + 1, by + 1, rgb(255, 220, 0));
    gfx->drawFastHLine(ex, by + 2, 3, rgb(255, 220, 0)); gfx->drawPixel(ex + 1, by + 3, rgb(255, 220, 0));
    gfx->drawPixel(ex, by + 4, rgb(255, 220, 0));
    x0 = min(x0, ex); x1 = max(x1, ex + 3);
  }
  gMark(x0, by, x1 - x0, 5);
}
void drawPadBattery() { drawBat(0); drawBat(1); }
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
    int ty = H / 2 - 62;  // titre ARCADE GAMES et record au-dessus du cadre du menu (qui commence a H/2 - 28)
    gfx->setTextSize(2);
    gfx->setTextColor(rgb(40, 230, 80));
    int tw = 6 * 12;
    gfx->setCursor((W - tw) / 2, ty);
    gfx->print("ARCADE");
    gMark((W - tw) / 2, ty, tw, 16);
    gfx->setTextSize(1);
    gText(ty + 17, "GAMES", rgb(140, 255, 150));
    char b[24];
    snprintf(b, sizeof(b), "RECORD %lu %s", (unsigned long)gHi, gBest[0].score ? gBest[0].name : "");
    gText(ty + 27, b, rgb(255, 40, 40));
  }
  gfx->setTextSize(1);
  if (gNameEntry) drawNameEntry();
  else { drawMenu(); drawPadBattery(); pairOverlay(); }
  if (padToastUntil && millis() < padToastUntil) gText(H - 9, padToast, rgb(0, 220, 255));  // manette connectee / partie
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
  // demo : un autre jeu au hasard toutes les 5 min ; au retour au menu, le jeu choisi est remis
  static uint32_t demoSince = 0;
  static int8_t menuGame = -1;
  if (screen == 1) {
    if (!demoSince) demoSince = now;
    if (now - demoSince > 300000UL) {  // demo : un autre jeu au hasard toutes les 5 min
      if (menuGame < 0) menuGame = gGame;
      int ng = random(N_GAMES - 1);
      if (ng >= gGame) ng++;
      gGame = ng;
      topLoad();
      gfx->fillScreen(0);
      gNOld = 0;
      resetCurrent();
      demoSince = now;
    }
  } else {
    demoSince = 0;
    if (menuGame >= 0 && screen == 0) { gGame = menuGame; menuGame = -1; topLoad(); resetCurrent(); }
  }
  if (menuMode) {  // menu sur son propre ecran : aucun jeu ne tourne, sa memoire est rendue (reprise au lancement)
    if (gMemGame != -1 || zD) { zRelease(); gMemFor(255); }
    menuScreen();
    return;
  }
  if (gGame) { altStep(); altDraw(); }
  else { gameStep(); gameDraw(); }
  gOverlay();
}

uint32_t restartAt = 0;
uint32_t brightSaveAt = 0;  // luminosite changee : enregistree 2 s plus tard

uint32_t pairUntil = 0;  // mode association en cours jusqu'a (millis), 0 : non
#define PAIR_BRIGHT 25  // luminosite pendant l'association (~10 %) : moins de courant dans les dalles, Bluetooth plus stable
void pairStart(uint32_t ms) {
  pairUntil = millis() + ms;
  if (dma) dma->setBrightness8(min<uint8_t>(bright, PAIR_BRIGHT));  // ton reglage (bright) n'est pas modifie
  if (sndTimer) timerAlarmDisable(sndTimer);  // son coupe : tout le coeur 0 pour le Bluetooth pendant l'association
  ledcWrite(SND_LEDC, 128);
  padScanWant = 1; padScanAt = millis();  // nouvelles manettes acceptees pendant l'association
  Serial.printf("Mode association %lu s : Share + PS sur la manette (PS5 : Create + PS), son coupe\n", (unsigned long)(ms / 1000));
}
void pairStop(const char *why) {
  if (!pairUntil) return;
  pairUntil = 0;
  padScanWant = 0; padScanAt = millis() + 2000;  // puis plus de recherche (2 s apres : jamais au milieu d'une connexion)
  if (dma) dma->setBrightness8(bright);  // luminosite normale de retour
  if (sndTimer) timerAlarmEnable(sndTimer);
  Serial.printf("Fin du mode association : %s\n", why);
}
void pairOverlay() {  // sur la dalle, dans un cadre : ASSOCIATION 45 / SHARE + PS
  if (!pairUntil) return;
  char b[20];
  snprintf(b, sizeof(b), "ASSOCIATION %lu", (unsigned long)((pairUntil - millis()) / 1000 + 1));
  gBox(H / 2 - 14, H / 2 + 12);
  gText(H / 2 - 9, b, rgb(255, 220, 0));
  gText(H / 2 + 1, "SHARE + PS", rgb(0, 220, 255));
}
// bouton "config wifi" du shield (le meme que l'horloge, GPIO 34, actif a l'etat haut, resistance de tirage sur le shield) :
// un appui ouvre le mode association 60 s (nouvel appui : 60 s de nouveau).
// L'appui est capte par interruption : meme bref, il n'est jamais perdu pendant le calcul d'une image (BUNKER 3D...).
#define CFG_BTN_PIN 34
volatile bool cfgBtnHit = false;
volatile uint32_t cfgBtnAt = 0;
void IRAM_ATTR cfgBtnIsr() {
  uint32_t t = millis();
  if (t - cfgBtnAt > 400) cfgBtnHit = true;  // anti-rebond : un seul appui par 0,4 s
  cfgBtnAt = t;
}
void cfgButtonBegin() {
  pinMode(CFG_BTN_PIN, INPUT);  // GPIO 34 : entree seule, tirage par la resistance du shield
  attachInterrupt(digitalPinToInterrupt(CFG_BTN_PIN), cfgBtnIsr, RISING);
}
void cfgButton() {
  if (!cfgBtnHit) return;
  cfgBtnHit = false;
  if (millis() < 3000) return;  // parasites au demarrage
  lastInput = millis();
  sfxMenu();
  pairStart(60000);
  Serial.println("Bouton du shield : mode association");
}
// Bluepad32 : association simple, PS + Share sur la manette (la barre lumineuse clignote vite), plus d'adresse a recopier
void ps4Begin() {
  BP32.setup(&onPadConnected, &onPadDisconnected);
  BP32.enableVirtualDevice(false);  // pas de souris virtuelle pour le pave tactile (elle prendrait une place de manette)
  BP32.enableNewBluetoothConnections(false);  // nouvelles manettes : seulement en mode association (bouton du shield) ;
                                              // les manettes deja associees se reconnectent toujours (bouton PS)
  const uint8_t *a = BP32.localBdAddress();
  if (a) snprintf(ipText, sizeof(ipText), "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
  Serial.printf("Bluepad32 %s pret, adresse %s, memoire libre %u\n", BP32.firmwareVersion(), ipText, ESP.getFreeHeap());
}

// lecture de la manette a chaque tour de boucle ; barre lumineuse selon le stage, vibration quand on est touche
void ps4Poll() {
  BP32.update();  // nouvelles donnees des manettes (Bluepad32)
  if (pairUntil && (long)(millis() - pairUntil) > 0) pairStop("temps ecoule");
  // nouvelles manettes acceptees ou non : seulement au debut et a la fin du mode association (plus de relance toutes les 5 s :
  // appelee pendant qu'une manette se connectait, elle faisait planter la pile Bluetooth -> redemarrage de l'ESP)
  if (padScanWant >= 0 && (long)(millis() - padScanAt) >= 0) { BP32.enableNewBluetoothConnections(padScanWant == 1); padScanWant = -1; }
  static bool was = false;
  static int lastTier = -2;
  static uint32_t rumbleOff = 0, lastBr = 0, combo = 0, lastLog = 0, since = 0;
  if (!PS4.isConnected()) {
    keysPs4 = 0;
    if (was) {
      was = false;
      Serial.printf("Manette deconnectee apres %lu s, memoire libre %u (plus petit bloc %u)\n", (millis() - since) / 1000,
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
    return;
  }
  if (!was) {
    was = true;
    lastTier = -2;
    since = millis();
    Serial.printf("Manette connectee, memoire libre %u (plus petit bloc %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  if (millis() - lastLog > 5000) {  // etat de la manette toutes les 5 s
    lastLog = millis();
    Serial.printf("  Manette ok %lu s, batterie %u/10, 2e manette %s, memoire libre %u (min %u)\n", (millis() - since) / 1000,
                  PS4.Battery(), PAD2.isConnected() ? "oui" : "non", ESP.getFreeHeap(), ESP.getMinFreeHeap());
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
  { int sy2 = PS4.LStickY(); padStickY = abs(sy2) > 18 ? constrain(sy2, -127, 127) : 0; }
  { int sx2 = PS4.LStickX(); padStickX = abs(sx2) > 18 ? constrain(sx2, -127, 127) : 0; }
  padDigital = PS4.Left() || PS4.Right() || PS4.Up() || PS4.Down() || PS4.UpLeft() || PS4.UpRight() || PS4.DownLeft() || PS4.DownRight();
  keysPs4 = gPaused ? 0 : k;
  {  // 2e manette : memes touches (stick avec hysteresis, croix, tir, rond)
    uint8_t k2 = 0;
    if (PAD2.isConnected()) {
      int x2 = PAD2.LStickX(), y2 = PAD2.LStickY();
      if (PAD2.Left() || PAD2.UpLeft() || PAD2.DownLeft() || x2 < ((keysP2 & 1) ? -30 : -50)) k2 |= 1;
      if (PAD2.Right() || PAD2.UpRight() || PAD2.DownRight() || x2 > ((keysP2 & 2) ? 30 : 50)) k2 |= 2;
      if (PAD2.Up() || PAD2.UpLeft() || PAD2.UpRight() || y2 > ((keysP2 & 8) ? 30 : 50)) k2 |= 8;
      if (PAD2.Down() || PAD2.DownLeft() || PAD2.DownRight() || y2 < ((keysP2 & 16) ? -30 : -50)) k2 |= 16;
      if (PAD2.Cross() || PAD2.Square() || PAD2.Triangle() || PAD2.R1() || PAD2.L1() || PAD2.R2Value() > 60) k2 |= 4;
      if (PAD2.Circle()) k2 |= 32;
    }
    keysP2 = gPaused ? 0 : k2;
  }
  // boutons du menu et START (Options) : on agit a l'appui
  static uint8_t prevB = 0;
  uint8_t bt = PS4.Up() | PS4.Down() << 1 | PS4.Left() << 2 | PS4.Right() << 3 | PS4.Cross() << 4 | PS4.Circle() << 5 |
               (PS4.Options() && !PS4.Share()) << 6 | (PS4.L2Value() > 120) << 7;  // L2 : pause (touche libre)
  uint8_t edge = bt & ~prevB;
  prevB = bt;
  if (edge) lastInput = millis();
  if (gNameEntry) nameInput(edge);  // saisie du nom
  else if (gDemo && gAttract) {  // mode demo : n'importe quel bouton ou le stick fait revenir le menu (sans autre effet)
    if (gAttractHold) { if (!k && !bt) gAttractHold = false; }  // la croix qui a lance la demo est encore enfoncee
    else if (edge || k) { gAttract = false; lastInput = millis(); sfxMenu(); prevB = 0xFF; }
  } else if (gDemo) menuInput(edge);
  else if (edge & (64 | 128) && gLives > 0) { gPaused = !gPaused; sfxMenu(); }  // OPTIONS ou L2 : pause / reprise
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
  int tier = gDemo ? -1 : (gWave - 1) % 6;  // une couleur par stage (6 couleurs en boucle), bleu sombre en demo
  if (tier != lastTier) {  // barre lumineuse : bleu sombre en demo, puis couleur du vaisseau
    static const uint8_t LED[7][3] = { { 0, 0, 40 }, { 0, 90, 255 }, { 40, 230, 80 }, { 255, 200, 0 }, { 0, 220, 255 }, { 255, 40, 40 }, { 200, 60, 255 } };
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
  if (rumbleReq2) { rumbleReq2 = false; PAD2.setRumble(0, 220); PAD2.sendToController(); PAD2.setRumble(0, 0); }
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
  if (menuSel == 1 && (edge & (4 | 8 | 16))) {  // 1 ou 2 joueurs
    gPlayers = gPlayers == 1 ? 2 : 1;
    prefs.putUChar("players", gPlayers);
    sfxMenu();
  }
  if (menuSel == 3 && (edge & (4 | 8 | 16))) {  // luminosite : gauche -10 %, droite ou X +10 % (X boucle 100 -> 0)
    int p = brightPct();
    if (edge & 4) p -= 10;
    else if (edge & 8) p += 10;
    else p = p >= 100 ? 0 : p + 10;
    setBrightPct(p);
    sfxMenu();
  }
  if (menuSel == 4 && (edge & (4 | 8 | 16))) {  // volume du son (rapport cyclique du PWM) : 0 a 100 %, pas de 10 %
    int v = volume;
    if (edge & 4) v--;
    else if (edge & 8) v++;
    else v = v >= 10 ? 0 : v + 1;
    volume = constrain(v, 0, 10);
    prefs.putUChar("vol", volume);
    sfxMenu();  // bip au nouveau volume (rien a 0 %)
  }
  if (menuSel == 2 && (edge & (4 | 8 | 16))) {  // niveau
    gDiff = (gDiff + ((edge & 4) ? 2 : 1)) % 3;
    prefs.putUChar("diff", gDiff);
    sfxMenu();
  }
  if (edge & 16) {
    if (menuSel == 0) startReq = true;
    else if (menuSel == 5) { menuScores = true; sfxMenu(); }
    else if (menuSel == 6) { menuSel = 0; sfxMenu(); startAttract(); gAttractHold = true; }  // ligne DEMO : demo du jeu choisi
  }
}

// reglages par le moniteur serie
void serialMenu() {
  Serial.println();
  Serial.println("===== Game hub v" GAMES_VERSION " =====");
  Serial.printf("Adresse Bluetooth de l'ESP32 : %s\n", ipText);
  Serial.printf("Dalles : %u, disposition %u, retourne %u, serpentin %u -> ecran %dx%d, couleurs %u bits\n", nPanels, arrange, flipAll,
                serpent, W, H, colorBits);
  Serial.println("  a : oublier les manettes associees (pour en associer d'autres : PS + Share)");
  Serial.println("  m : mode association 60 s (comme le bouton du shield : son coupe, luminosite reduite)");
  Serial.println("  c NDFS : dalles. N nombre 1-4, D 0 l'une sous l'autre / 1 cote a cote / 2 carre,");
  Serial.println("           F ecran retourne 0/1, S une dalle sur deux retournee 0/1. Ex. : c 2000");
  Serial.println("  p N : couleurs des dalles sur N bits (3 a 6, redemarre). 3 = le plus sur pour le Bluetooth,");
  Serial.println("        4 = plus de nuances. 2 plantages de suite : retour automatique a 3.");
  Serial.printf("  v N : volume du son, 0 (muet) a 10. Actuel : %u\n", volume);
  Serial.printf("  l N : luminosite des dalles, 0 a 100 %%. Actuelle : %d %%\n", (bright * 100 + 127) / 255);
  Serial.printf("  j N : test, demo du jeu N (0 a %d)\n", N_GAMES - 1);
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
      BP32.forgetBluetoothKeys();
      Serial.println("Manettes associees oubliees : bouton du shield (ou m), puis PS + Share sur chaque manette pour l'associer de nouveau");
    } else if (cmd == 'm') {
      pairStart(60000);
    } else if (cmd == 'l') {  // luminosite des dalles en %, gardee en memoire (comme la ligne LUMINOSITE du menu)
      int p = atoi(arg);
      if (*arg && p >= 0 && p <= 100) {
        setBrightPct(p);
        Serial.printf("Luminosite %d %% (valeur %u/255), enregistree\n", p, bright);
      } else Serial.printf("Format : l 0 a l 100 (en %%). Actuelle : %d %% (valeur %u/255)\n", (bright * 100 + 127) / 255, bright);
    } else if (cmd == 'j') {  // test : demo du jeu N (0 a N_GAMES - 1)
      int g = atoi(arg);
      if (*arg && g >= 0 && g < N_GAMES) {
        gGame = g; topLoad(); gDemo = true; startAttract();
        Serial.printf("Demo du jeu %d (%s), memoire libre %u\n", g, GAME_NAME[g], ESP.getFreeHeap());
      } else Serial.printf("Format : j 0 a j %d\n", N_GAMES - 1);
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
  // ecran de demarrage : ARCADE GAMES et la version, pendant 2 s (le Bluetooth et le son demarrent pendant ce temps)
  uint32_t bootT0 = millis();
  {
    int y = H / 2 - 18;
    gfx->setFont(NULL);
    gfx->setTextSize(2);
    gfx->setTextColor(rgb(40, 230, 80));
    gfx->setCursor((W - 6 * 12) / 2, y);
    gfx->print("ARCADE");
    gfx->setTextSize(1);
    gText(y + 18, "GAMES", rgb(140, 255, 150));
    gText(y + 28, "V" GAMES_VERSION, rgb(150, 150, 170));
  }

  gDiff = prefs.getUChar("diff", 1) % 3;
  gPlayers = constrain(prefs.getUChar("players", 1), 1, 2);
  gGame = 0;  // demarrage toujours sur ESSAIM (choix d'un autre jeu : gauche / droite dans le menu)
  cfgButtonBegin();  // bouton du shield (GPIO 34)
  ps4Begin();
  audioBegin();
  // pas de mode association au demarrage : seulement a la demande, avec le bouton du shield
  serialMenu();
  while (millis() - bootT0 < 2000) delay(10);  // la version reste 2 s a l'ecran

  gameSetup();
  resetCurrent();  // demo du jeu choisi au demarrage
  gDemo = true;
}

void loop() {
  if (restartAt && millis() > restartAt) ESP.restart();
  ps4Poll();
  cfgButton();
  sndUpdate();
  handleSerial();
  if (brightSaveAt && millis() > brightSaveAt) { brightSaveAt = 0; prefs.putUChar("bright", bright); }
  gameUpdate();
  vTaskDelay(1);
}
