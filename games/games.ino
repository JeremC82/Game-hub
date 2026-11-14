/**************************************************************************************************/
/*  Games : shoot'em up d'arcade sur dalles LED HUB75 128x64, ESP32 classique                    */
/*                                                                                                */
/*  Le jeu vient du mode Jeu du VU-metre. On y joue depuis une page web (telephone, tablette ou  */
/*  PC) : http://games.local ou l'adresse IP affichee sur la dalle.                               */
/*    gauche / droite, FEU (tir automatique en restant appuye)                                    */
/*    clavier : fleches ou Q / D, espace pour tirer ; manette de jeu reconnue par le navigateur   */
/*  Ou avec la manette ESP32 (dossier manette/) : radio ESP-NOW directe, sans box ni page web.    */
/*  Sans joueur, le jeu tourne tout seul (demo). FEU lance une partie. Le record est garde.      */
/*                                                                                                */
/*  Ecran (choix dans la page web, garde en memoire, redemarre) :                                */
/*    128x128 : 2 dalles 128x64 chainees, l'une sous l'autre (1re dalle = celle reliee a l'ESP32)  */
/*    256x64  : 2 dalles 128x64 chainees, l'une a cote de l'autre (1re dalle a gauche)           */
/*    256x128 : 4 dalles 128x64 chainees, ordre de la chaine : haut gauche, haut droite,          */
/*              bas gauche, bas droite (toutes dans le meme sens)                                  */
/*  Meme brochage que l'horloge ClockV1 et le VU-metre.                                          */
/*                                                                                                */
/*  Wifi : essaie d'abord les reseaux de secrets.h (fichier local, jamais publie), puis le        */
/*  dernier reseau regle. Sinon l'ESP32 cree le reseau                                            */
/*  "Games-Setup" : s'y connecter et choisir sa box sur la page qui s'ouvre (192.168.4.1).         */
/*  Bouton BOOT appuye 5 s : oublie le wifi et redemarre.                                        */
/**************************************************************************************************/
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <WebServer.h>  // avant ESPAsyncWebServer.h (utilise par WiFiManager, evite le conflit HTTP_GET)
#include <ESPAsyncWebServer.h>
#include <WiFiManager.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include "web_page.h"
#if __has_include("secrets.h")
#include "secrets.h"  // reseaux wifi connus (local, jamais publie)
#else
#define WIFI_SEED_COUNT 0
const char *WIFI_SEED[1][2] = { { "", "" } };
#endif

#define GAMES_VERSION "1.00"

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
#define BOOT_PIN 0  // bouton BOOT : appui 5 s = oublier le wifi

Preferences prefs;
MatrixPanel_I2S_DMA *dma = nullptr;
// Dalles 128x64 chainees, rangees en grille (cols x rows) : la chaine fait (128 x nombre) x 64,
// presentee comme un ecran (128 x cols) x (64 x rows). Ordre : ligne du haut de gauche a droite, puis la suivante.
class TiledPanels : public Adafruit_GFX {
 public:
  TiledPanels(MatrixPanel_I2S_DMA *d, uint8_t cols, uint8_t rows) : Adafruit_GFX(128 * cols, 64 * rows), m(d), nc(cols) {}
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    m->drawPixel(((y >> 6) * nc + (x >> 7)) * 128 + (x & 127), y & 63, c);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int x0 = max(0, (int)x), x1 = min((int)_width, x + w), y0 = max(0, (int)y), y1 = min((int)_height, y + h);
    for (int ty = y0; ty < y1; ty = (ty & ~63) + 64)  // morceau de rectangle sur chaque dalle
      for (int tx = x0; tx < x1; tx = (tx & ~127) + 128) {
        int ex = min(x1, (tx & ~127) + 128), ey = min(y1, (ty & ~63) + 64);
        m->fillRect(((ty >> 6) * nc + (tx >> 7)) * 128 + (tx & 127), ty & 63, ex - tx, ey - ty, c);
      }
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
  void fillScreen(uint16_t c) override { m->fillScreen(c); }
 private:
  MatrixPanel_I2S_DMA *m;
  uint8_t nc;
};
Adafruit_GFX *gfx = nullptr;
#define N_LAYOUTS 3
const char *LAYOUT_NAME[N_LAYOUTS] = { "128x128", "256x64", "256x128" };
const uint8_t LAYOUT_COLS[N_LAYOUTS] = { 1, 2, 2 };  // dalles par ligne
const uint8_t LAYOUT_ROWS[N_LAYOUTS] = { 2, 1, 2 };  // lignes de dalles
uint8_t layout = 0;
int W = 128, H = 128;
uint8_t bright = 120;
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return dma->color565(r, g, b); }

// ---------------------------------------------------------------- commandes (page web -> jeu)
volatile uint8_t keys = 0;          // touches de la page web : 1 gauche, 2 droite, 4 feu
volatile uint8_t keysNow = 0;       // touches de la manette ESP-NOW
volatile uint32_t lastNowRx = 0;    // derniere trame de la manette (sans nouvelles : touches relachees)
volatile uint32_t lastInput = 0;    // dernier appui d'un joueur (millis)
volatile bool startReq = false;     // FEU pendant la demo : nouvelle partie
bool gDemo = true;                  // le jeu joue tout seul
#define IDLE_TO_DEMO_MS 60000UL     // partie abandonnee : retour a la demo apres 1 min sans appui
char ipText[20] = "";

// ---------------------------------------------------------------- petite police 3x5 (score)
const uint8_t TF_DIG[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
                                { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
const uint8_t TF_R[5] = { 6, 5, 6, 5, 5 }, TF_E[5] = { 7, 4, 6, 4, 7 }, TF_S[5] = { 7, 4, 7, 1, 7 },
              TF_C[5] = { 7, 4, 4, 4, 7 }, TF_D[5] = { 6, 5, 5, 5, 6 };
const uint8_t *tfGlyph(char c) {
  if (c >= '0' && c <= '9') return TF_DIG[c - '0'];
  switch (c) {
    case 'R': return TF_R; case 'E': return TF_E; case 'S': return TF_S; case 'C': return TF_C;
    case 'O': return TF_DIG[0]; case 'D': return TF_D;
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

// ---------------------------------------------------------------- jeu : shoot'em up d'arcade (repris du VU-metre)
// Sprites originaux multicolores (9x7), formation qui oscille, ennemis qui piquent en courbe,
// ecran d'arcade : SCORE et RECORD en haut, vies et vague en bas (128x128).
// Sans joueur, un pilote automatique joue (demo). Les ennemis pulsent doucement.
// Dessin sans scintillement : seuls les rectangles dessines a l'image precedente sont effaces.
#define G_MAXE 100
#define G_SHOTS 3
#define G_BOMBS 10
#define G_BOOMS 10
#define G_STARS 128
#define G_RECTS 460
#define G_MAXROWS 5
const int GW = 9, GH = 7;  // taille des ennemis
struct GEnemy { float x, y, vx, vy; int16_t t; int8_t row, col, type, hp, dir; uint8_t state; bool alive; };  // state : 0 rang, 1 pique, 2 retour
struct GDot { float x, y; bool on; };
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
// type 0 : meduse (2 tirs), type 1 : scarabee, type 2 : insecte ailé
const char *const SPR_E[3][2][GH] = {
  { { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", "..c.c.c..", ".c..c..c.", "c...c...c" },
    { "..ppppp..", ".pwpppwp.", "ppppppppp", "p.c.c.c.p", ".c..c..c.", "..c.c.c..", "...c.c..." } },
  { { "v...o...v", ".v.ooo.v.", "..oowoo..", "vvooooovv", "v.ooooo.v", "..o.o.o..", ".o.....o." },
    { ".v..o..v.", "..vooov..", "..oowoo..", ".vooooov.", "v.ooooo.v", ".o..o..o.", "o.......o" } },
  { { "c.......c", ".c.ggg.c.", "..gwywg..", "cgyyyyygc", "c.ggggg.c", "..g.y.g..", "...g.g..." },
    { "cc.....cc", "..cgggc..", "..gwywg..", ".gyyyyyg.", "..ggggg..", "..g.y.g..", "...g.g..." } }
};
const char *const SPR_SHIP[8] = { "....c....", "...ccc...", "...cwc...", "..bcwcb..", ".bbcccbb.", "bbcbcbcbb", "b.o...o.b", "..o...o.." };
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
void gMark(int x, int y, int w, int h) {
  if (gNOld < G_RECTS) gOld[gNOld++] = { (int16_t)x, (int16_t)y, (uint8_t)w, (uint8_t)h };
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
float gSlotY(int row) { return gTop + row * 9; }
void gBoomAt(int x, int y) {
  for (int i = 0; i < G_BOOMS; i++)
    if (gBoom[i].t == 0) { gBoom[i] = { (int16_t)x, (int16_t)y, 1 }; return; }
}
void gNewWave() {
  gCols = constrain((W - 6) / 11, 4, 20);
  gNE = 0;
  for (int r = 0; r < gRows; r++)
    for (int c = 0; c < gCols && gNE < G_MAXE; c++) {
      if (gRowType[r] == 0 && (c & 1)) continue;  // meduses une place sur deux
      GEnemy &e = gEn[gNE++];
      e = {};
      e.row = r; e.col = c; e.type = gRowType[r]; e.alive = true;
      e.hp = e.type == 0 ? 2 : 1;
      e.x = gSlotX(c); e.y = -10 - 8 * (gRows - r);  // arrivent du haut
      e.state = 2;
    }
  gWave++;
}
void gReset() {
  gLives = 3; gScore = 0; gWave = 0; gDead = 0; gCool = 0;
  gPx = W / 2 - 4;
  for (auto &s : gShot) s.on = false;
  for (auto &b : gBomb) b.on = false;
  for (auto &b : gBoom) b.t = 0;
  gNewWave();
}
void gameSetup() {
  gfx->fillScreen(0);
  gNOld = 0;
  bool tall = H >= 128;
  gRows = tall ? 5 : 3;
  static const int8_t T5[5] = { 0, 1, 1, 2, 2 }, T3[3] = { 0, 1, 2 };
  for (int r = 0; r < gRows; r++) gRowType[r] = tall ? T5[r] : T3[r];
  gTop = 13;
  gPY = tall ? H - 15 : H - 8;  // sur 128x128 : la ligne du bas montre vies et vague
  gHi = prefs.getUInt("hiscore", 0);
  if (gLives <= 0 && gDead == 0) gReset();
  gNStar = min(G_STARS, W * H / 220);
  for (int i = 0; i < gNStar; i++) gStar[i] = { (int16_t)random(W), (float)random(12, H), 0.2f + random(100) / 120.0f, (uint8_t)random(9) };
  gLastStep = millis();
}
void gHitPlayer() {
  gBoomAt(gPx + 4, gPY + 3);
  gLives--;
  gDead = gLives > 0 ? 50 : 120;  // pause avant le vaisseau suivant (plus longue apres GAME OVER)
  if (!gDemo && gLives <= 0 && gScore >= gHi && gScore) prefs.putUInt("hiscore", gHi);  // nouveau record (joueurs seulement)
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
  gSway = sinf(gFrame * 0.03f) * max(1, min(6, (W - gCols * 11) / 2 - 1));
  int alive = 0;
  for (int i = 0; i < gNE; i++) alive += gEn[i].alive;
  int diveEvery = max(16, 60 - gWave * 7);
  if (gDead == 0 && gFrame % diveEvery == 0 && alive) {
    int k = random(gNE);
    for (int t = 0; t < gNE; t++, k = (k + 1) % gNE)
      if (gEn[k].alive && gEn[k].state == 0) {
        GEnemy &e = gEn[k];
        e.state = 1; e.vx = 0; e.vy = -0.6f; e.t = 0;
        e.dir = e.x < W / 2 ? -1 : 1;  // part en boucle vers l'exterieur
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
      float curve = e.t < 50 ? e.dir * 1.2f * cosf(e.t * 0.063f) : 0;
      e.vx = constrain(e.vx * 0.96f + curve * 0.06f + (shipC - (e.x + 4)) * (e.t < 50 ? 0.0005f : 0.004f), -1.4f, 1.4f);
      e.vy = min(e.vy + 0.03f, 1.3f + 0.1f * gWave);
      e.x += e.vx + curve; e.y += e.vy;
      if (e.y > H) { e.y = -8; e.state = 2; }
    } else {  // retour a sa place
      float dx = sx - e.x, dy = sy - e.y;
      e.x += constrain(dx * 0.08f, -1.5f, 1.5f);
      e.y += constrain(dy * 0.08f, -1.5f, 1.5f);
      if (fabsf(dx) < 0.6f && fabsf(dy) < 0.6f) e.state = 0;
    }
    int chance = e.state == 1 ? 18 : 2 + gWave;  // tirs ennemis
    if (gDead == 0 && e.y > 0 && e.y < H * 0.6f && random(1000) < chance)
      for (auto &b : gBomb)
        if (!b.on) { b = { e.x + 4, e.y + GH, true }; break; }
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
    float target = gPx;
    bool dodge = false;
    for (auto &b : gBomb)
      if (b.on && b.y > gPY - 24 && fabsf(b.x - shipC) < 6) { target = b.x < shipC ? gPx + 7 : gPx - 7; dodge = true; break; }
    if (!dodge) {
      float best = 1e9;
      for (int i = 0; i < gNE; i++) {
        GEnemy &e = gEn[i];
        if (!e.alive || e.y < 0) continue;
        float d = fabsf(e.x - gPx) - (e.state == 1 ? 40 : 0) - (e.type == 0 ? 8 : 0);  // priorite : piques, meduses
        if (d < best) { best = d; target = e.x; }
      }
    }
    gPx += constrain(target - gPx, -1.3f, 1.3f);
    gPx = constrain(gPx, 0.0f, (float)(W - GW));
    if (gCool > 0) gCool--;
    if (gCool == 0)  // tir quand un ennemi est au-dessus
      for (int i = 0; i < gNE; i++)
        if (gEn[i].alive && gEn[i].y > 0 && fabsf(gEn[i].x - gPx) < 4) {
          for (auto &s : gShot)
            if (!s.on) { s = { gPx + 4, (float)gPY - 3, true }; gCool = 7; break; }
          break;
        }
  } else if (--gDead == 0 && gLives <= 0) {
    if (gDemo) gReset();  // demo : on recommence
    else toDemo();        // fin de partie du joueur : retour a la demo
  }
  for (auto &s : gShot) {  // tirs du vaisseau
    if (!s.on) continue;
    s.y -= 3;
    if (s.y < 0) { s.on = false; continue; }
    for (int i = 0; i < gNE; i++) {
      GEnemy &e = gEn[i];
      if (e.alive && s.x >= e.x && s.x <= e.x + GW - 1 && s.y <= e.y + GH - 1 && s.y + 2 >= e.y) {
        s.on = false;
        if (--e.hp > 0) break;  // meduse : il faut 2 tirs
        e.alive = false;
        static const uint16_t PTS[3][2] = { { 150, 400 }, { 80, 160 }, { 50, 100 } };
        gScore += PTS[e.type][e.state == 1];
        if (!gDemo && gScore > gHi) gHi = gScore;
        gBoomAt(e.x + 4, e.y + 3);
        break;
      }
    }
  }
  for (auto &b : gBomb) {  // tirs ennemis
    if (!b.on) continue;
    b.y += 1.3f + 0.1f * gWave;
    if (b.y >= H) { b.on = false; continue; }
    if (gDead == 0 && b.x >= gPx && b.x <= gPx + 8 && b.y + 2 >= gPY + 2 && b.y <= gPY + 7) { b.on = false; gHitPlayer(); }
  }
  for (auto &b : gBoom)
    if (b.t) b.t = b.t >= 12 ? 0 : b.t + 1;
  bool any = false;
  for (int i = 0; i < gNE; i++) any |= gEn[i].alive;
  if (!any) gNewWave();
}
void gameDraw() {
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
  for (int i = 0; i < gNStar; i++) {  // etoiles de couleur qui scintillent
    GStar &s = gStar[i];
    if (((gFrame + i * 7) & 31) < 3) continue;
    const uint8_t *c = G_PAL_RGB[s.hue];
    float k = 0.15f + s.v * 0.3f;
    gPix(s.x, (int)s.y, rgb(c[0] * k, c[1] * k, c[2] * k));
  }
  for (int i = 0; i < gNE; i++) {
    GEnemy &e = gEn[i];
    if (!e.alive) continue;
    int fr = e.state == 1 ? (gFrame / 5) & 1 : ((gFrame / 16) + e.col) & 1;  // animation
    gSprite(SPR_E[e.type][fr], GW, GH, (int)e.x, (int)e.y, gPal, 'p', e.hp == 1 && e.type == 0 ? 'o' : 'p');
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
  if (gDead == 0) gSprite(SPR_SHIP, 9, 8, (int)gPx, gPY, palFull, 0, 0);
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
  if (H >= 128) {  // ligne du bas : vies a gauche, vague a droite
    for (int i = 0; i < gLives - 1 && i < 6; i++) gSprite(SPR_LIFE, 5, 4, 1 + i * 7, H - 5, palFull, 0, 0);
    for (int i = 0; i < gWave && i < 10; i++) {
      int x = W - 4 - i * 4;
      gPix(x, H - 5, palFull[2]); gPix(x + 1, H - 5, palFull[2]);
      gPix(x, H - 4, palFull[1]); gPix(x + 1, H - 4, palFull[1]);
      gPix(x, H - 3, palFull[1]); gPix(x + 1, H - 3, palFull[1]);
      gPix(x, H - 2, palFull[3]);
    }
  } else if (W >= 96) {  // ecran bas : vies en haut a droite
    for (int i = 0; i < gLives - 1 && i < 4; i++) gSprite(SPR_LIFE, 5, 4, W - 6 - i * 7, 1, palFull, 0, 0);
  }
}

// joueur : deplacement aux touches, tir tant que FEU est appuye (3 tirs a l'ecran au plus)
void gPlayerControl() {
  uint8_t k = keys | (millis() - lastNowRx < 500 ? keysNow : 0);
  float sp = W > 128 ? 2.4f : 1.7f;
  if ((k & 1) && !(k & 2)) gPx -= sp;
  if ((k & 2) && !(k & 1)) gPx += sp;
  gPx = constrain(gPx, 0.0f, (float)(W - GW));
  if (gCool > 0) gCool--;
  if ((k & 4) && gCool == 0)
    for (auto &s : gShot)
      if (!s.on) { s = { gPx + 4, (float)gPY - 3, true }; gCool = 8; break; }
}

// texte 6x8 centre, efface a l'image suivante
void gText(int y, const char *s, uint16_t c) {
  int x = (W - (int)strlen(s) * 6) / 2;
  gfx->setTextColor(c);
  gfx->setCursor(x, y);
  gfx->print(s);
  gMark(x, y, strlen(s) * 6, 8);
}
void gOverlay() {
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  if (gLives <= 0 && gDead > 0 && !gDemo) gText(H / 2 - 4, "GAME OVER", rgb(255, 40, 40));
  if (!gDemo) return;
  if ((gFrame / 40) & 1) gText(H / 2 + 6, "DEMO", rgb(255, 220, 0));
  else gText(H / 2 + 6, "APPUIE SUR FEU", rgb(0, 220, 255));
  if (ipText[0]) gText(H / 2 + 18, ipText, rgb(110, 110, 130));
}

void startGame() {
  gDemo = false;
  gfx->fillScreen(0);
  gNOld = 0;
  gReset();
}
void toDemo() {
  gDemo = true;
  gfx->fillScreen(0);
  gNOld = 0;
  gReset();
}

void gameUpdate() {
  uint32_t now = millis();
  if (now - gLastStep < 25) return;  // 40 images par seconde
  gLastStep = now - gLastStep > 100 ? now : gLastStep + 25;
  if (startReq) {
    startReq = false;
    if (gDemo || (gLives <= 0)) startGame();
  }
  if (!gDemo && now - lastInput > IDLE_TO_DEMO_MS) toDemo();
  gameStep();
  gameDraw();
  gOverlay();
}

// ---------------------------------------------------------------- manette ESP-NOW (2e ESP32, sans box)
// Le jeu emet une balise 2 fois par seconde sur son canal ; la manette la cherche sur les canaux 1 a 13,
// puis envoie ses touches (a chaque changement et toutes les 100 ms). Paquets identiques dans manette.ino.
#define GH_MAGIC 0x42554847UL  // "GHUB"
enum : uint8_t { GH_BEACON = 1, GH_KEYS = 2 };
struct __attribute__((packed)) GhPacket {
  uint32_t magic;
  uint8_t type;
  uint8_t keys;
};
const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
bool nowOn = false;

void onNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(GhPacket)) return;
  const GhPacket *p = (const GhPacket *)data;
  if (p->magic != GH_MAGIC || p->type != GH_KEYS) return;
  uint8_t nk = p->keys & 7;
  bool fresh = millis() - lastNowRx < 500;
  if ((nk & 4) && !(fresh && (keysNow & 4))) startReq = true;  // FEU : lance une partie
  if (nk) lastInput = millis();
  keysNow = nk;
  lastNowRx = millis();
}
void nowBegin() {
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW indisponible"); return; }
  esp_now_register_recv_cb(onNowRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0;  // canal actuel (celui de la box, ou du point d'acces de reglage)
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);
  nowOn = true;
  uint8_t ch;
  wifi_second_chan_t sc;
  esp_wifi_get_channel(&ch, &sc);
  Serial.printf("Manette ESP-NOW : balise sur le canal %u\n", ch);
}
void nowBeacon() {
  static uint32_t last = 0;
  if (!nowOn || millis() - last < 500) return;
  last = millis();
  GhPacket p = { GH_MAGIC, GH_BEACON, 0 };
  esp_now_send(BROADCAST, (const uint8_t *)&p, sizeof(p));
}

// ---------------------------------------------------------------- serveur web (page + WebSocket)
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t restartAt = 0;  // redemarrage demande (changement d'ecran)
bool serverOn = false;
WiFiManager wm;
bool portalOn = false;  // portail de reglage wifi ouvert (non bloquant)

void onWs(AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DISCONNECT) {
    if (s->count() == 0) keys = 0;  // plus personne : on relache tout
    return;
  }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->opcode != WS_TEXT || len < 2 || len > 8) return;
  char b[9];
  memcpy(b, data, len);
  b[len] = 0;
  int v = atoi(b + 1);
  if (b[0] == 'k') {  // touches : k<masque>
    uint8_t nk = v & 7;
    if ((nk & 4) && !(keys & 4)) startReq = true;  // FEU : lance une partie (demo ou fin de partie)
    keys = nk;
    lastInput = millis();
  } else if (b[0] == 'p') {  // ecran 128x128 / 256x128 : enregistre puis redemarre
    if (v >= 0 && v < N_LAYOUTS && v != layout) { prefs.putUChar("layout", v); restartAt = millis() + 500; }
  } else if (b[0] == 'v' || b[0] == 'V') {  // luminosite (V = enregistrer)
    bright = constrain(v, 5, 255);
    dma->setBrightness8(bright);
    if (b[0] == 'V') prefs.putUChar("bright", bright);
  }
}

// etat du jeu envoye aux pages ouvertes :
// {"m":0 demo/1 partie/2 perdu,"s":score,"h":record,"l":vies,"w":vague,"b":luminosite,"p":ecran}
void sendStatus() {
  static uint32_t last = 0;
  static char prev[96] = "";
  if (!serverOn || millis() - last < 150) return;
  last = millis();
  ws.cleanupClients();
  if (!ws.count()) return;
  char js[96];
  int m = gDemo ? 0 : gLives <= 0 ? 2 : 1;
  snprintf(js, sizeof(js), "{\"m\":%d,\"s\":%lu,\"h\":%lu,\"l\":%d,\"w\":%d,\"b\":%u,\"p\":%u}", m, (unsigned long)gScore, (unsigned long)gHi,
           max(0, gLives), gWave, bright, layout);
  static uint32_t lastFull = 0;
  if (strcmp(js, prev) == 0 && millis() - lastFull < 2000) return;  // rien de neuf (rappel toutes les 2 s)
  lastFull = millis();
  strcpy(prev, js);
  ws.textAll(js);
}

void startServer() {
  ws.onEvent(onWs);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse_P(200, "text/html", WEB_PAGE);
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });
  server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("/"); });
  server.begin();
  MDNS.begin("games");
  MDNS.addService("http", "tcp", 80);
  serverOn = true;
  snprintf(ipText, sizeof(ipText), "%s", WiFi.localIP().toString().c_str());
  Serial.printf("Page de jeu : http://games.local ou http://%s\n", ipText);
}

// ---------------------------------------------------------------- ecran de connexion wifi
void wifiScreen(const char *l1, const char *l2, const char *l3) {
  gfx->fillScreen(0);
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  gfx->setTextColor(rgb(255, 220, 0));
  gfx->setCursor(2, H / 2 - 24);
  gfx->print(l1);
  gfx->setTextColor(rgb(0, 220, 255));
  gfx->setCursor(2, H / 2 - 8);
  gfx->print(l2);
  gfx->setTextColor(rgb(150, 150, 170));
  gfx->setCursor(2, H / 2 + 8);
  gfx->print(l3);
}

void handleBootButton() {
  static uint32_t down = 0;
  if (digitalRead(BOOT_PIN) == LOW) {
    if (!down) down = millis();
    else if (millis() - down > 5000) {
      wifiScreen("WIFI OUBLIE", "redemarrage...", "");
      wm.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else down = 0;
}

// ---------------------------------------------------------------- demarrage
void setup() {
  Serial.begin(115200);
  Serial.println("Games v" GAMES_VERSION);
  btStop();
  pinMode(BOOT_PIN, INPUT_PULLUP);
  prefs.begin("games", false);
  bright = prefs.getUChar("bright", 120);
  layout = prefs.getUChar("layout", 0) % N_LAYOUTS;

  HUB75_I2S_CFG::i2s_pins pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN };
  // profondeur de couleur la plus haute qui laisse assez de memoire au wifi et au serveur web
  for (int depth = 6; depth >= 3; depth--) {
    HUB75_I2S_CFG cfg(128, 64, LAYOUT_COLS[layout] * LAYOUT_ROWS[layout], pins);
    cfg.setPixelColorDepthBits(depth);
    cfg.clkphase = false;
    dma = new MatrixPanel_I2S_DMA(cfg);
    if (dma->begin()) {
      if (ESP.getFreeHeap() >= 110000 || depth == 3) {
        Serial.printf("Ecran %s, couleurs %d bits, memoire libre %u\n", LAYOUT_NAME[layout], depth, ESP.getFreeHeap());
        break;
      }
      dma->stopDMAoutput();
    }
    delete dma;
    dma = nullptr;
  }
  if (!dma) {  // 4 dalles impossibles : retour a 128x128 plutot que rester bloque
    Serial.println("Memoire insuffisante pour les dalles");
    if (layout == 2) { prefs.putUChar("layout", 0); delay(50); ESP.restart(); }
    for (;;) delay(1000);
  }
  W = 128 * LAYOUT_COLS[layout];
  H = 64 * LAYOUT_ROWS[layout];
  gfx = new TiledPanels(dma, LAYOUT_COLS[layout], LAYOUT_ROWS[layout]);
  dma->setBrightness8(bright);
  gfx->fillScreen(0);

  // wifi : reseau connu, sinon portail "Games-Setup" pendant 3 min (puis demo seule, sans page web)
  wifiScreen("CONNEXION WIFI", "", "");
  WiFi.mode(WIFI_STA);
  bool ok = false;
  if (WIFI_SEED_COUNT) {  // reseaux connus de secrets.h : le plus fort a portee
    WiFiMulti multi;
    for (int i = 0; i < WIFI_SEED_COUNT; i++) multi.addAP(WIFI_SEED[i][0], WIFI_SEED[i][1]);
    ok = multi.run(15000) == WL_CONNECTED;
  }
  // sinon dernier reseau regle, puis portail "Games-Setup" pendant 3 min, sans bloquer le jeu ni la manette
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);
  wm.setConfigPortalBlocking(false);
  wm.setAPCallback([](WiFiManager *) { strcpy(ipText, "WIFI:Games-Setup"); });
  if (ok || wm.autoConnect("Games-Setup")) {
    WiFi.setSleep(false);  // reponse plus rapide aux touches
    startServer();
  } else {
    portalOn = true;
    Serial.println("Wifi a regler : reseau Games-Setup (192.168.4.1)");
  }
  nowBegin();

  gameSetup();
  gDemo = true;
}

void loop() {
  handleBootButton();
  if (restartAt && millis() > restartAt) ESP.restart();
  if (portalOn) {
    wm.process();
    if (WiFi.status() == WL_CONNECTED) {  // reseau choisi : on ferme le portail, la page de jeu demarre
      portalOn = false;
      wm.stopConfigPortal();
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      startServer();
    } else if (!wm.getConfigPortalActive()) {  // 3 min ecoulees : jeu et manette seuls
      portalOn = false;
      ipText[0] = 0;
    }
  }
  gameUpdate();
  sendStatus();
  nowBeacon();
  vTaskDelay(1);
}
