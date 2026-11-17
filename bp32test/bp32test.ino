// Essai Bluepad32 + dalles HUB75 : jusqu'a 4 manettes (PS4, PS5, Xbox...) en meme temps.
// Affiche sur la 1re dalle 128x64 l'etat de chaque manette (boutons, croix, batterie), une raquette par manette
// pilotee au stick gauche (comme RAQUETTES a deux), et la memoire libre (pour savoir si les dalles et le
// Bluetooth tiennent ensemble). X : la manette vibre. Nombre de dalles et couleurs : memes reglages que Game hub.
// Association : PS4 -> appuyer PS + Share jusqu'au clignotement rapide de la barre lumineuse.
#include <Arduino.h>
#include <Preferences.h>
#include <Bluepad32.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

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

MatrixPanel_I2S_DMA *dma = nullptr;
Preferences prefs;
ControllerPtr pads[BP32_MAX_GAMEPADS];
float padY[2] = { 24, 24 };
uint8_t nPanels = 2, colorBits = 3;
uint32_t heapMin = 0xFFFFFFFF;

static const uint8_t LEDC[4][3] = { { 0, 80, 255 }, { 255, 30, 30 }, { 40, 230, 80 }, { 255, 200, 0 } };

void onConnected(ControllerPtr c) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    if (!pads[i]) {
      pads[i] = c;
      c->setColorLED(LEDC[i % 4][0], LEDC[i % 4][1], LEDC[i % 4][2]);
      Serial.printf("Manette %d connectee : %s\n", i + 1, c->getModelName().c_str());
      return;
    }
  Serial.println("Manette refusee : deja 4 manettes");
}
void onDisconnected(ControllerPtr c) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    if (pads[i] == c) { pads[i] = nullptr; Serial.printf("Manette %d deconnectee\n", i + 1); }
}

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return dma->color565(r, g, b); }

void setup() {
  Serial.begin(115200);
  prefs.begin("games", true);  // memes reglages que Game hub (lecture seule)
  nPanels = constrain(prefs.getUChar("nb", 2), 1, 4);
  colorBits = constrain(prefs.getUChar("bits", 3), 3, 6);
  prefs.end();
  HUB75_I2S_CFG::i2s_pins pins = { R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN };
  HUB75_I2S_CFG cfg(128, 64, nPanels, pins);
  cfg.setPixelColorDepthBits(colorBits);
  cfg.clkphase = false;
  dma = new MatrixPanel_I2S_DMA(cfg);
  if (!dma->begin()) Serial.println("Dalles : echec d'allocation memoire");
  dma->setBrightness8(90);
  dma->clearScreen();
  Serial.printf("Dalles : %d x 128x64, %d bits, memoire libre %u\n", nPanels, colorBits, ESP.getFreeHeap());
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableNewBluetoothConnections(true);
  Serial.printf("Bluepad32 %s pret, memoire libre %u, adresse %s\n", BP32.firmwareVersion(), ESP.getFreeHeap(),
                BP32.localBdAddress() ? "ok" : "?");
}

void loop() {
  BP32.update();
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    ControllerPtr c = pads[i];
    if (!c || !c->isConnected() || !c->hasData()) continue;
    if (i < 2) padY[i] = constrain(padY[i] + c->axisY() / 512.0f * 2.5f, 8.0f, 50.0f);  // stick gauche : vitesse selon l'inclinaison
    static uint16_t prevBtn[BP32_MAX_GAMEPADS];
    uint16_t b = c->buttons();
    if ((b & BUTTON_A) && !(prevBtn[i] & BUTTON_A)) c->playDualRumble(0, 200, 0x80, 0x40);  // X (croix) : vibration
    prevBtn[i] = b;
  }
  uint32_t fh = ESP.getFreeHeap();
  if (fh < heapMin) heapMin = fh;
  static uint32_t last = 0;
  if (millis() - last < 100) { delay(1); return; }
  last = millis();
  dma->fillRect(0, 0, 128, 64, 0);
  dma->setTextSize(1);
  dma->setTextWrap(false);
  char s[40];
  for (int i = 0; i < 2; i++) {
    ControllerPtr c = pads[i];
    int y = 1 + i * 20;
    uint16_t col = rgb(LEDC[i][0], LEDC[i][1], LEDC[i][2]);
    dma->setTextColor(col);
    dma->setCursor(8, y);
    if (c && c->isConnected()) {
      snprintf(s, sizeof(s), "J%d %s", i + 1, c->getModelName().c_str());
      dma->print(s);
      snprintf(s, sizeof(s), "B%04X D%X bat%d", c->buttons(), c->dpad(), c->battery() * 100 / 255);
      dma->setCursor(8, y + 9);
      dma->print(s);
    } else {
      snprintf(s, sizeof(s), "J%d : PS + SHARE", i + 1);
      dma->print(s);
    }
    int px = i == 0 ? 1 : 124;  // raquettes (comme RAQUETTES a deux)
    dma->fillRect(px, (int)padY[i], 3, 10, col);
  }
  dma->setTextColor(rgb(200, 200, 200));
  snprintf(s, sizeof(s), "RAM %uk min %uk", ESP.getFreeHeap() / 1024, heapMin / 1024);
  dma->setCursor(8, 44);
  dma->print(s);
  snprintf(s, sizeof(s), "bloc %uk  %db", ESP.getMaxAllocHeap() / 1024, colorBits);
  dma->setCursor(8, 54);
  dma->print(s);
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 3000) {
    lastLog = millis();
    Serial.printf("RAM libre %u, min %u, plus grand bloc %u\n", ESP.getFreeHeap(), heapMin, ESP.getMaxAllocHeap());
  }
}
